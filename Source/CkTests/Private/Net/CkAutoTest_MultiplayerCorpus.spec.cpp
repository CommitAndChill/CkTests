#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkAutoTestRunner.h"
#include "CkAutoTest_Bridge.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkCore/Reflection/CkReflection_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "FunctionalTest.h"
#include "FunctionalTestBase.h"
#include "FunctionalTestingModule.h"
#include "Misc/App.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#if WITH_ANGELSCRIPT_CK
#include "ClassGenerator/ASClass.h"
#endif

namespace ck_multiplayer_corpus
{
    constexpr int32 ExpectedWorlds = 2;
    constexpr double RunnerWatchdogSeconds = 120.0;

    // The map populator uses the same suffix when it labels a placed wrapper.
    auto WrapperLabel(const UClass* InClass) -> FString
    {
        auto Label = InClass->GetName();
        Label.RemoveFromEnd(TEXT("_Actor"));
        return Label;
    }

    auto IsLiveStandaloneWrapper(UClass* InClass) -> bool
    {
        if (InClass == nullptr || InClass == ACk_AutoTestRunner::StaticClass()
            || NOT InClass->IsChildOf(ACk_AutoTestRunner::StaticClass())
            || NOT InClass->GetName().EndsWith(TEXT("_Actor"))
            || InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
            || InClass->IsUnreachable()
            || InClass->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
#if WITH_ANGELSCRIPT_CK
        if (auto* ASClass = UASClass::GetFirstASClass(InClass))
        {
            if (ASClass->NewerVersion != nullptr)
            { return false; }
            const auto SourcePath = ASClass->GetSourceFilePath();
            if (NOT SourcePath.IsEmpty() && NOT FPaths::FileExists(SourcePath))
            { return false; }
        }
#endif

        return true;
    }

    auto TryReadBodyNetMode(UClass* InClass, uint64& OutMode) -> bool
    {
        if (InClass == nullptr)
        { return false; }
        const auto* CDO = InClass->GetDefaultObject();
        const auto* Property = InClass->FindPropertyByName(TEXT("_NetMode"));
        if (CDO == nullptr || Property == nullptr)
        { return false; }
        if (const auto* ByteProperty = CastField<FByteProperty>(Property))
        {
            OutMode = ByteProperty->GetPropertyValue_InContainer(CDO);
            return OutMode <= static_cast<uint64>(ECk_AutoTest_NetMode::Replicated);
        }
        if (const auto* EnumProperty = CastField<FEnumProperty>(Property))
        {
            const auto* Underlying = EnumProperty->GetUnderlyingProperty();
            if (Underlying == nullptr)
            { return false; }
            OutMode = Underlying->GetUnsignedIntPropertyValue(
                EnumProperty->ContainerPtrToValuePtr<void>(CDO));
            return OutMode <= static_cast<uint64>(ECk_AutoTest_NetMode::Replicated);
        }
        return false;
    }

    // Enumerating wrappers alone cannot notice a body that never got a wrapper.
    // Match the generator's live-body filters and require one wrapper per standalone body.
    auto ValidateBodyCoverage(const TArray<UClass*>& InWrappers,
        TArray<FString>& OutNames, TArray<FString>& OutCommands) -> void
    {
        const auto Report = [&OutNames, &OutCommands](const FString& InName, const FString& InReason)
        {
            OutNames.Add(InName + TEXT(".DiscoveryBodyFailure"));
            OutCommands.Add(TEXT("DISCOVERY_FAILURE|") + InReason);
        };
        auto* Base = FSoftClassPath(TEXT("/Script/Angelscript.Ck_AutoTest_Base"))
            .TryLoadClass<UCk_EntityScript_UE>();
        if (Base == nullptr || Base->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
        { Report(TEXT("Base"), TEXT("live AutoTest body base class is unavailable")); return; }

        TMap<UClass*, int32> WrapperCountByBody;
        for (auto* Wrapper : InWrappers)
        {
            const auto* CDO = Wrapper->GetDefaultObject<ACk_AutoTestRunner>();
            if (CDO != nullptr)
            { ++WrapperCountByBody.FindOrAdd(CDO->Get_TestEntityScriptClass().Get()); }
        }
        for (TObjectIterator<UClass> It; It; ++It)
        {
            auto* Body = *It;
            if (Body == Base || NOT Body->IsChildOf(Base)
                || Body->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
                || Body->IsUnreachable() || Body->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
                || UCk_Utils_Reflection_UE::Is_PlaceholderClass(Body))
            { continue; }
#if WITH_ANGELSCRIPT_CK
            if (auto* ASClass = UASClass::GetFirstASClass(Body))
            {
                const auto SourcePath = ASClass->GetSourceFilePath();
                if (ASClass->NewerVersion != nullptr ||
                    (NOT SourcePath.IsEmpty() && NOT FPaths::FileExists(SourcePath)))
                { continue; }
            }
#endif
            uint64 NetMode = 0;
            if (NOT TryReadBodyNetMode(Body, NetMode))
            { Report(Body->GetName(), TEXT("body has missing or invalid network metadata: ") + Body->GetName()); continue; }
            // Replicated and independent net bodies are exercised by the generated net stubs.
            if (NetMode != static_cast<uint64>(ECk_AutoTest_NetMode::Standalone))
            { continue; }
            const int32 Count = WrapperCountByBody.FindRef(Body);
            if (Count != 1)
            {
                Report(Body->GetName(), FString::Printf(TEXT("body %s has %d live wrappers; expected one"),
                    *Body->GetName(), Count));
            }
        }
    }

    auto ReadRequirements(UClass* InWrapper, UClass*& OutBodyClass,
        bool& OutLocalPlayer, bool& OutRenderer, bool& OutStandalone) -> bool
    {
        OutBodyClass = nullptr;
        OutLocalPlayer = false;
        OutRenderer = false;
        OutStandalone = false;
        if (InWrapper == nullptr)
        { return false; }
        const auto* WrapperCDO = InWrapper->GetDefaultObject<ACk_AutoTestRunner>();
        if (WrapperCDO == nullptr)
        { return false; }
        UClass* BodyClass = WrapperCDO->Get_TestEntityScriptClass().Get();
        auto* Base = FSoftClassPath(TEXT("/Script/Angelscript.Ck_AutoTest_Base"))
            .TryLoadClass<UCk_EntityScript_UE>();
        if (BodyClass == nullptr || Base == nullptr || BodyClass == Base
            || NOT BodyClass->IsChildOf(Base)
            || BodyClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
            || BodyClass->IsUnreachable() || BodyClass->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
            || UCk_Utils_Reflection_UE::Is_PlaceholderClass(BodyClass))
        { return false; }
#if WITH_ANGELSCRIPT_CK
        if (auto* ASClass = UASClass::GetFirstASClass(BodyClass))
        {
            const auto SourcePath = ASClass->GetSourceFilePath();
            if (ASClass->NewerVersion != nullptr ||
                (NOT SourcePath.IsEmpty() && NOT FPaths::FileExists(SourcePath)))
            { return false; }
        }
#endif
        uint64 NetMode = 0;
        if (NOT TryReadBodyNetMode(BodyClass, NetMode))
        { return false; }
        const auto* BodyCDO = BodyClass->GetDefaultObject();
        const auto* LocalProperty = FindFProperty<FBoolProperty>(BodyClass, TEXT("_RequiresLocalPlayer"));
        const auto* RendererProperty = FindFProperty<FBoolProperty>(BodyClass, TEXT("_RequiresRenderer"));
        if (BodyCDO == nullptr || LocalProperty == nullptr || RendererProperty == nullptr)
        { return false; }
        OutLocalPlayer = LocalProperty->GetPropertyValue_InContainer(BodyCDO);
        OutRenderer = RendererProperty->GetPropertyValue_InContainer(BodyCDO);
        OutBodyClass = BodyClass;
        OutStandalone = NetMode == static_cast<uint64>(ECk_AutoTest_NetMode::Standalone);
        return true;
    }

    // The engine's map-test discovery reads the same OFPA actor tags used by
    // Session Frontend. Its command includes the map package and actor name.
    auto ResolveMapForWrapper(const FString& InLabel, FString& OutMap, FString& OutActorName) -> bool
    {
        TArray<FFunctionalTestInfo> MapTests;
        TArray<FString> MapAssets;
        IFunctionalTestingModule::Get().GetMapTests(false, MapTests, MapAssets);

        int32 Matches = 0;
        for (const auto& Info : MapTests)
        {
            if (NOT Info.BeautifiedName.EndsWith(FString::Printf(TEXT(".%s"), *InLabel)))
            { continue; }

            TArray<FString> Fields;
            Info.TestCommand.ParseIntoArray(Fields, TEXT(";"), true);
            if (Fields.Num() != 3 || NOT Fields[1].StartsWith(TEXT("/")) || Fields[2].IsEmpty())
            { continue; }

            ++Matches;
            OutMap = Fields[1];
            OutActorName = Fields[2];
        }
        return Matches == 1;
    }

    class FRunPlacedWrapper final : public IAutomationLatentCommand
    {
    public:
        FRunPlacedWrapper(FAutomationTestBase* InTest, UClass* InClass, UClass* InBodyClass,
            const FString& InActorName, bool InDedicated, bool InServerRole,
            bool InRequiresLocalPlayer, bool InRequiresRenderer)
            : _Test(InTest), _Class(InClass), _BodyClass(InBodyClass), _ActorName(InActorName),
              _Dedicated(InDedicated), _ServerRole(InServerRole),
              _RequiresLocalPlayer(InRequiresLocalPlayer), _RequiresRenderer(InRequiresRenderer) {}

        virtual bool Update() override
        {
            if (_Test == nullptr || NOT _Class.IsValid() || NOT _BodyClass.IsValid())
            { return Fail(TEXT("test context, wrapper class, or body class was lost")); }
            if (ck::auto_test::net::Get_HasSessionFailed())
            { return Fail(TEXT("PIE startup or readiness failed")); }
            if (_RequiresRenderer && NOT FApp::CanEverRender())
            { return Fail(TEXT("renderer-required row has no real rendering capability")); }

            if (NOT _Started)
            {
                const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
                auto* Server = ck::auto_test::net::Get_ServerWorld();
                auto* Client = ck::auto_test::net::Get_ClientWorld(0);
                if (Worlds.Num() != ExpectedWorlds || Server == nullptr || Client == nullptr
                    || NOT Server->HasBegunPlay() || NOT Client->HasBegunPlay())
                { return Fail(TEXT("two begun-play PIE worlds are required")); }

                const auto ExpectedServerMode = _Dedicated ? NM_DedicatedServer : NM_ListenServer;
                if (Server->GetNetMode() != ExpectedServerMode || Client->GetNetMode() != NM_Client)
                { return Fail(TEXT("observed world net modes do not match the requested topology")); }

                auto* World = _ServerRole ? Server : Client;
                if (_RequiresLocalPlayer && World->GetFirstLocalPlayerFromController() == nullptr)
                {
                    if (_ReadinessStartSeconds < 0.0)
                    { _ReadinessStartSeconds = FPlatformTime::Seconds(); }
                    if (FPlatformTime::Seconds() - _ReadinessStartSeconds > 45.0)
                    { return Fail(TEXT("required local player did not become ready")); }
                    return false;
                }
                ACk_AutoTestRunner* Match = nullptr;
                int32 MatchCount = 0;
                for (TActorIterator<ACk_AutoTestRunner> It(World); It; ++It)
                {
                    if (It->GetClass() != _Class.Get())
                    { continue; }
                    Match = *It;
                    ++MatchCount;
                }
                if (MatchCount != 1 || Match == nullptr)
                { return Fail(FString::Printf(TEXT("expected one placed wrapper [%s], found %d"),
                    *_ActorName, MatchCount)); }
                if (Match->GetName() != _ActorName)
                { return Fail(FString::Printf(TEXT("placed wrapper actor name [%s] differs from on-disk discovery [%s]"),
                    *Match->GetName(), *_ActorName)); }
                if (NOT Match->IsEnabledInWorld(World))
                { return Fail(TEXT("placed wrapper is disabled in the selected world")); }
                if (Match->Get_TestEntityScriptClass().Get() != _BodyClass.Get())
                { return Fail(TEXT("placed wrapper resolves a different body class than discovery")); }

                _Runner = Match;
                _Started = true;
                _StartSeconds = FPlatformTime::Seconds();
                if (NOT Match->RunTest())
                { return Fail(TEXT("AFunctionalTest::RunTest rejected the placed wrapper")); }
            }

            if (NOT _Runner.IsValid())
            { return Fail(TEXT("runner actor disappeared before reporting a result")); }
            if (_Runner->IsRunning())
            {
                const auto Limit = FMath::Max(RunnerWatchdogSeconds,
                    static_cast<double>(_Runner->TimeLimit) + 30.0);
                if (FPlatformTime::Seconds() - _StartSeconds > Limit)
                { return Fail(TEXT("runner did not finish before the adapter watchdog")); }
                return false;
            }

            if (_Runner->Result != EFunctionalTestResult::Succeeded)
            { return Fail(FString::Printf(TEXT("runner completed with result %d"),
                static_cast<int32>(_Runner->Result))); }
            return true;
        }

    private:
        auto Fail(const FString& InReason) -> bool
        {
            if (_Test != nullptr)
            { _Test->AddError(FString::Printf(TEXT("Multiplayer corpus [%s]: %s"),
                *_ActorName, *InReason)); }
            return true; // The queued EndPIE command still runs.
        }

        FAutomationTestBase* _Test = nullptr;
        TWeakObjectPtr<UClass> _Class;
        TWeakObjectPtr<UClass> _BodyClass;
        TWeakObjectPtr<ACk_AutoTestRunner> _Runner;
        FString _ActorName;
        bool _Dedicated = false;
        bool _ServerRole = false;
        bool _RequiresLocalPlayer = false;
        bool _RequiresRenderer = false;
        bool _Started = false;
        double _StartSeconds = 0.0;
        double _ReadinessStartSeconds = -1.0;
    };

    auto GetPlacedWrapperLabel(const FFunctionalTestInfo& InInfo) -> FString
    {
        int32 LastDot = INDEX_NONE;
        if (NOT InInfo.BeautifiedName.FindLastChar(TEXT('.'), LastDot))
        { return FString{}; }
        const auto Label = InInfo.BeautifiedName.Mid(LastDot + 1);
        return Label.StartsWith(TEXT("Ck_AutoTest_")) ? Label : FString{};
    }

    class FCkMultiplayerCorpusBase : public FFunctionalTestBase
    {
    public:
        FCkMultiplayerCorpusBase(const FString& InName, bool InComplexTask)
            : FFunctionalTestBase(InName, InComplexTask),
              _RenderedCorpus(InName.Contains(TEXT("RenderedCorpus"))) {}

        virtual bool SuppressLogs() override
        { return false; }

        virtual bool ElevateLogWarningsToErrors() override
        { return IsFunctionalTestRunning() && FFunctionalTestBase::ElevateLogWarningsToErrors(); }

        virtual void GetTests(TArray<FString>& OutBeautifiedNames,
            TArray<FString>& OutTestCommands) const override
        {
            TArray<UClass*> Classes;
            for (TObjectIterator<UClass> It; It; ++It)
            {
                if (IsLiveStandaloneWrapper(*It))
                { Classes.Add(*It); }
            }
            Classes.Sort([](const UClass& A, const UClass& B)
            { return A.GetPathName() < B.GetPathName(); });

            if (NOT _RenderedCorpus)
            { ValidateBodyCoverage(Classes, OutBeautifiedNames, OutTestCommands); }

            TArray<FFunctionalTestInfo> MapTests;
            TArray<FString> MapAssets;
            IFunctionalTestingModule::Get().GetMapTests(false, MapTests, MapAssets);
            TMap<FString, int32> PlacedCountByLabel;
            TSet<FString> InvalidPlacedLabels;
            for (const auto& Info : MapTests)
            {
                const auto Label = GetPlacedWrapperLabel(Info);
                if (Label.IsEmpty())
                { continue; }
                ++PlacedCountByLabel.FindOrAdd(Label);
                TArray<FString> Fields;
                Info.TestCommand.ParseIntoArray(Fields, TEXT(";"), true);
                if (Fields.Num() != 3 || NOT Fields[1].StartsWith(TEXT("/")) || Fields[2].IsEmpty())
                { InvalidPlacedLabels.Add(Label); }
            }

            if (Classes.IsEmpty() && NOT _RenderedCorpus)
            {
                OutBeautifiedNames.Add(TEXT("Discovery.Empty"));
                OutTestCommands.Add(TEXT("DISCOVERY_FAILURE|no live standalone wrappers were discovered"));
            }

            TSet<FString> LiveLabels;
            for (const auto* Class : Classes)
            { LiveLabels.Add(WrapperLabel(Class)); }
            if (NOT _RenderedCorpus)
            {
                for (const auto& Pair : PlacedCountByLabel)
                {
                    if (LiveLabels.Contains(Pair.Key))
                    { continue; }
                    OutBeautifiedNames.Add(Pair.Key + TEXT(".DiscoveryOrphan"));
                    OutTestCommands.Add(TEXT("DISCOVERY_FAILURE|placed wrapper has no live class: ") + Pair.Key);
                }
            }

            for (auto* Class : Classes)
            {
                const FString ClassPath = Class->GetPathName();
                const FString Label = WrapperLabel(Class);
                bool RequiresLocalPlayer = false;
                bool RequiresRenderer = false;
                UClass* BodyClass = nullptr;
                bool StandaloneBody = false;
                if (NOT ReadRequirements(Class, BodyClass, RequiresLocalPlayer, RequiresRenderer, StandaloneBody))
                {
                    if (NOT _RenderedCorpus)
                    {
                        OutBeautifiedNames.Add(Label + TEXT(".DiscoveryFailure"));
                        OutTestCommands.Add(TEXT("DISCOVERY_FAILURE|wrapper metadata is missing: ") + Label);
                    }
                    continue;
                }
                // A preserved functional wrapper can intentionally use a network body.
                // Its metadata is valid; that body runs through its authored multiplayer harness.
                if (NOT StandaloneBody) { continue; }
                if (RequiresRenderer != _RenderedCorpus)
                { continue; }
                const int32 PlacedCount = PlacedCountByLabel.FindRef(Label);
                if (PlacedCount != 1 || InvalidPlacedLabels.Contains(Label))
                {
                    OutBeautifiedNames.Add(Label + TEXT(".DiscoveryPlacementFailure"));
                    OutTestCommands.Add(FString::Printf(
                        TEXT("DISCOVERY_FAILURE|wrapper %s has %d placed AutoTests actors (or invalid command)"),
                        *Label, PlacedCount));
                    continue;
                }
                for (const FString Topology : { FString(TEXT("ListenServer")), FString(TEXT("ClientServer")) })
                {
                    for (const FString Role : { FString(TEXT("Server")), FString(TEXT("Client")) })
                    {
                        if ((RequiresLocalPlayer || RequiresRenderer) &&
                            Topology == TEXT("ClientServer") && Role == TEXT("Server"))
                        { continue; }
                        OutBeautifiedNames.Add(Label + TEXT(".") + Topology + TEXT(".") + Role);
                        OutTestCommands.Add(ClassPath + TEXT("|") + Topology + TEXT("|") + Role);
                    }
                }
            }
        }

        virtual bool RunTest(const FString& InParameters) override
        {
            if (InParameters.StartsWith(TEXT("DISCOVERY_FAILURE|")))
            {
                AddError(InParameters.RightChop(FString(TEXT("DISCOVERY_FAILURE|")).Len()));
                return false;
            }
            TArray<FString> Fields;
            InParameters.ParseIntoArray(Fields, TEXT("|"), true);
            if (Fields.Num() != 3 || (Fields[2] != TEXT("Server") && Fields[2] != TEXT("Client")))
            { AddError(TEXT("Malformed multiplayer corpus test command")); return false; }

            bool Dedicated = false;
            if (NOT ck::auto_test::net::TryGet_DedicatedServer(Fields[1], Dedicated))
            { AddError(TEXT("Unknown multiplayer corpus topology")); return false; }

            auto* Class = FSoftClassPath(Fields[0]).TryLoadClass<ACk_AutoTestRunner>();
            if (NOT IsLiveStandaloneWrapper(Class))
            { AddError(TEXT("Wrapper class is missing or stale")); return false; }
            bool RequiresLocalPlayer = false;
            bool RequiresRenderer = false;
            UClass* BodyClass = nullptr;
            bool StandaloneBody = false;
            if (NOT ReadRequirements(Class, BodyClass, RequiresLocalPlayer, RequiresRenderer, StandaloneBody))
            { AddError(TEXT("Wrapper body class or prerequisite metadata is missing")); return false; }
            if (NOT StandaloneBody)
            { AddError(TEXT("Network body requires its authored multiplayer harness")); return false; }
            if (RequiresRenderer != _RenderedCorpus ||
                (Dedicated && Fields[2] == TEXT("Server") && (RequiresLocalPlayer || RequiresRenderer)))
            { AddError(TEXT("Corpus row does not match declared prerequisites")); return false; }

            FString Map;
            FString ActorName;
            if (NOT ResolveMapForWrapper(WrapperLabel(Class), Map, ActorName))
            { AddError(TEXT("Wrapper has zero or multiple AutoTests map actors")); return false; }

            SetLogErrorAndWarningHandlingToDefault();
            AddCommand(new FCk_Latent_StartPIEMultiClient(ExpectedWorlds, Map, Fields[1]));
            AddCommand(new FCk_Latent_WaitForPIEReady(ExpectedWorlds, 45.0f));
            AddCommand(new FRunPlacedWrapper(this, Class, BodyClass, ActorName, Dedicated,
                Fields[2] == TEXT("Server"), RequiresLocalPlayer, RequiresRenderer));
            AddCommand(new FCk_Latent_EndPIE());
            return true;
        }

    private:
        bool _RenderedCorpus = false;
    };
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FCkMultiplayerCorpus,
    ck_multiplayer_corpus::FCkMultiplayerCorpusBase, "Ck.AutoTest.Net.Corpus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FCkMultiplayerCorpus::GetTests(TArray<FString>& OutBeautifiedNames,
    TArray<FString>& OutTestCommands) const
{
    ck_multiplayer_corpus::FCkMultiplayerCorpusBase::GetTests(OutBeautifiedNames, OutTestCommands);
}

bool FCkMultiplayerCorpus::RunTest(const FString& InParameters)
{
    return ck_multiplayer_corpus::FCkMultiplayerCorpusBase::RunTest(InParameters);
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FCkMultiplayerRenderedCorpus,
    ck_multiplayer_corpus::FCkMultiplayerCorpusBase, "Ck.AutoTest.Net.RenderedCorpus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
    EAutomationTestFlags::NonNullRHI)

void FCkMultiplayerRenderedCorpus::GetTests(TArray<FString>& OutBeautifiedNames,
    TArray<FString>& OutTestCommands) const
{
    ck_multiplayer_corpus::FCkMultiplayerCorpusBase::GetTests(OutBeautifiedNames, OutTestCommands);
}

bool FCkMultiplayerRenderedCorpus::RunTest(const FString& InParameters)
{
    return ck_multiplayer_corpus::FCkMultiplayerCorpusBase::RunTest(InParameters);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkDivergenceCorpusEligibility,
    "Ck.StateMachine.Gym.Divergence.CorpusEligibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkDivergenceCorpusEligibility::RunTest(const FString& Parameters)
{
    UClass* Body = ACk_AutoTestRunner::StaticClass();
    bool LocalPlayer = true;
    bool Renderer = true;
    bool Standalone = true;
    TestFalse(TEXT("Missing wrapper is rejected"),
        ck_multiplayer_corpus::ReadRequirements(nullptr, Body, LocalPlayer, Renderer, Standalone));
    TestTrue(TEXT("Rejected requirements clear every output"),
        Body == nullptr && !LocalPlayer && !Renderer && !Standalone);

    for (const auto* Name : { TEXT("Ck_AutoTest_StateMachine_DivergenceFirstBranch"),
        TEXT("Ck_AutoTest_StateMachine_DivergenceFirstBranchTimed"),
        TEXT("Ck_AutoTest_2dGridBlocker_Create_MakesDistinctChild") })
    {
        const FString BodyPath = FString(TEXT("/Script/Angelscript.")) + Name;
        auto* ExpectedBody = FSoftClassPath(BodyPath).TryLoadClass<UCk_EntityScript_UE>();
        auto* Wrapper = FSoftClassPath(BodyPath + TEXT("_Actor"))
            .TryLoadClass<ACk_AutoTestRunner>();
        if (!TestNotNull(TEXT("Authored body resolves"), ExpectedBody) ||
            !TestNotNull(TEXT("Existing functional wrapper resolves"), Wrapper)) { return false; }
        const bool Valid = ck_multiplayer_corpus::ReadRequirements(
            Wrapper, Body, LocalPlayer, Renderer, Standalone);
        TestTrue(TEXT("Real wrapper has valid prerequisite metadata"), Valid);
        TestTrue(TEXT("Requirements retain the exact real body"), Valid && Body == ExpectedBody);
        const bool ExpectedStandalone = FString(Name).StartsWith(TEXT("Ck_AutoTest_2dGridBlocker"));
        TestTrue(TEXT("Standalone eligibility is distinct from metadata validity"),
            Valid && Standalone == ExpectedStandalone);
    }
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
