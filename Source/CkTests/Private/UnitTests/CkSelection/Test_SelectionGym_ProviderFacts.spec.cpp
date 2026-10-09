#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/Net/CkAutoTest_Sm_RecordingState.h"
#include "CkEntityDebugOverlay/Provider/CkDebugOverlay_ObservationBank.h"
#include "CkEntityDebugOverlay/Provider/CkDebugOverlay_Registry.h"
#include "CkEntityDebugOverlay/Settings/CkDebugOverlay_Settings.h"
#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkLabel/CkLabel_Utils.h"
#include "CkStateMachine/Debug/CkStateMachine_Debug_Fragment.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"
#include "CkEntityScriptGym_Runtime.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"

namespace ck_selection_provider_facts_test
{
    struct FRun
    {
        FCk_Handle ServerRoot;
        FCk_Handle AuthorityScenario;
        FCk_Handle AuthorityIntegerOwner;
        FCk_Handle AuthorityInteger;
        FCk_Handle_StateMachine AuthoritySm;
        FCk_Handle ClientRoot;
        FCk_Handle PassiveScenario;
        FCk_Handle PassiveIntegerOwner;
        FCk_Handle PassiveSm;
        bool Built = false;
    };

    auto Allocated(const FCk_Handle& H) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    auto FindProvider(const FString& TagName) -> TSharedPtr<ICk_DebugOverlay_Provider>
    {
        TSharedPtr<ICk_DebugOverlay_Provider> Found;
        for (const auto& Provider : FCk_DebugOverlay_Registry::Get().CreateAll())
        {
            if (Provider.IsValid() && Provider->Get_ProviderTag().ToString() == TagName)
            {
                if (Found.IsValid()) { return {}; }
                Found = Provider;
            }
        }
        return Found;
    }

    auto Config(const ICk_DebugOverlay_Provider& Provider) -> FCk_DebugOverlay_ProviderConfig
    {
        FCk_DebugOverlay_ProviderConfig Result;
        for (const auto& Field : Provider.Get_FieldTags())
        { Result.EnabledFields.AddTag(Field.Tag); }
        return Result;
    }

    auto SameRows(const FCk_DebugOverlay_Section& A, const FCk_DebugOverlay_Section& B)
        -> bool
    {
        if (A.ProviderTag != B.ProviderTag || A.Rows.Num() != B.Rows.Num())
        { return false; }
        for (int32 I = 0; I < A.Rows.Num(); ++I)
        {
            const auto& X = A.Rows[I];
            const auto& Y = B.Rows[I];
            if (X.FieldTag != Y.FieldTag || X.Value.ToString() != Y.Value.ToString() ||
                X.Severity != Y.Severity ||
                X.ExplicitHistory.Num() != Y.ExplicitHistory.Num())
            { return false; }
            for (int32 J = 0; J < X.ExplicitHistory.Num(); ++J)
            {
                if (X.ExplicitHistory[J].ToString() != Y.ExplicitHistory[J].ToString())
                { return false; }
            }
        }
        return true;
    }

    auto SameProvider(ICk_DebugOverlay_Provider& Provider,
        const FCk_Handle& Authority, const FCk_Handle& Passive,
        const FCk_DebugOverlay_ProviderConfig& Cfg) -> bool
    {
        if (!Provider.CanProvide(Authority) || !Provider.CanProvide(Passive))
        { return false; }
        FCk_DebugOverlay_Section Live, Mirror;
        Provider.Collect(Authority, Cfg, Live);
        Provider.Collect(Passive, Cfg, Mirror);
        return SameRows(Live, Mirror) &&
            Provider.Get_CompactToken(Authority, Cfg) ==
                Provider.Get_CompactToken(Passive, Cfg);
    }

    auto RowCount(ICk_DebugOverlay_Provider& Provider, const FCk_Handle& Source,
        const FCk_DebugOverlay_ProviderConfig& Cfg) -> int32
    {
        FCk_DebugOverlay_Section Section;
        Provider.Collect(Source, Cfg, Section);
        return Section.Rows.Num();
    }

    auto CaptureAuthority(const FRun& Run,
        FCk_DebugOverlay_SourceObservation& IntegerSource,
        FCk_DebugOverlay_SourceObservation& SmSource) -> bool
    {
        IntegerSource = {}; SmSource = {};
        if (!Allocated(Run.AuthorityIntegerOwner) || !Allocated(Run.AuthorityInteger) ||
            !Allocated(Run.AuthoritySm) ||
            !Run.AuthoritySm.Has<ck::FFragment_Sm>())
        { return false; }

        auto MutableOwner = Run.AuthorityIntegerOwner;
        UCk_Utils_IntegerAttribute_UE::ForEach(MutableOwner,
            [&IntegerSource](FCk_Handle_IntegerAttribute Attribute)
            {
                FCk_DebugOverlay_IntegerFact Fact;
                Fact.Label = UCk_Utils_GameplayLabel_UE::Get_Label(Attribute);
                Fact.Base = UCk_Utils_IntegerAttribute_UE::Get_BaseValue(
                    Attribute, ECk_MinMaxCurrent::Current);
                Fact.Final = UCk_Utils_IntegerAttribute_UE::Get_FinalValue(
                    Attribute, ECk_MinMaxCurrent::Current);
                IntegerSource.Integers.Add(MoveTemp(Fact));
            });
        if (IntegerSource.Integers.Num() != 1 ||
            !IntegerSource.Integers[0].Label.IsValid() ||
            IntegerSource.Integers[0].Base != 7 ||
            IntegerSource.Integers[0].Final != 7)
        { return false; }

        const auto& Sm = Run.AuthoritySm.Get<ck::FFragment_Sm>();
        const auto* CurrentClass = Sm.Get_CurrentStateClass().Get();
        if (!IsValid(CurrentClass) || !Allocated(Sm.Get_CurrentStateHandle()) ||
            Sm.Get_RunStatus() != ECk_SmRunStatus::Running)
        { return false; }
        SmSource.HasSm = true;
        FCk_DebugOverlay_SmStateFact State;
        State.Depth = 0;
        State.StateName = CurrentClass->GetName();
        State.Status = Sm.Get_RunStatus();
        SmSource.SmStates.Add(MoveTemp(State));
        SmSource.HasDebugHistory = Run.AuthoritySm.Has<ck::FFragment_Sm_Debug>();
        if (SmSource.HasDebugHistory)
        {
            for (const auto& Entry :
                Run.AuthoritySm.Get<ck::FFragment_Sm_Debug>().Get_History())
            {
                FCk_DebugOverlay_SmHistoryFact Fact;
                Fact.FromStateName = Entry.FromStateName;
                Fact.ToStateName = Entry.ToStateName;
                SmSource.SmHistory.Add(MoveTemp(Fact));
            }
        }
        return true;
    }

    class FRetireRoots final : public IAutomationLatentCommand
    {
    public:
        explicit FRetireRoots(TSharedPtr<FRun> InRun) : Run(MoveTemp(InRun)) {}
        bool Update() override
        {
            if (Allocated(Run->PassiveScenario))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->PassiveScenario); }
            if (Allocated(Run->AuthorityScenario))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->AuthorityScenario); }
            return true;
        }
    private:
        TSharedPtr<FRun> Run;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSelectionGym_ProviderFacts,
    "Ck.Selection.Gym.ProviderFacts.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkSelectionGym_ProviderFacts::RunTest(const FString& Parameters)
{
    using namespace ck_selection_provider_facts_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Selection provider topology")); return false; }
    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            return IsValid(Server) && IsValid(Client) &&
                IsValid(Server->GetGameState()) && IsValid(Client->GetGameState()) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*Server)) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*Client));
        }), 60.0, TEXT("Selection provider authority and passive client worlds ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            Run->ServerRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
            if (!Allocated(Run->ServerRoot))
            { TestTrue(TEXT("authority transient root exists"), false); return; }
            Run->AuthorityScenario = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run->ServerRoot);
            if (!Allocated(Run->AuthorityScenario))
            { TestTrue(TEXT("authority scenario root composed"), false); return; }
            Run->AuthorityIntegerOwner = UCk_Utils_EntityLifetime_UE::
                Request_CreateEntity(Run->AuthorityScenario);
            if (!Allocated(Run->AuthorityIntegerOwner))
            { TestTrue(TEXT("authority integer source composed"), false); return; }
            Run->AuthorityInteger = UCk_Utils_IntegerAttribute_UE::Add(
                Run->AuthorityIntegerOwner,
                FCk_IntegerAttribute_Spec(TAG_Label_IntegerAttribute, 7),
                ECk_Replication::DoesNotReplicate);
            Run->AuthoritySm = UCk_Utils_StateMachine_UE::Add(
                Run->AuthorityScenario,
                FCk_StateMachine_Spec(UCk_AutoTest_Sm_RecordingState_A::StaticClass()));
            Run->Built = Allocated(Run->AuthorityInteger) && Allocated(Run->AuthoritySm);
            TestTrue(TEXT("real authority Integer and SM composition admitted"), Run->Built);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (!Run->Built) { return true; }
            if (!Allocated(Run->AuthoritySm) ||
                !Run->AuthoritySm.Has<ck::FFragment_Sm>()) { return false; }
            const auto& Sm = Run->AuthoritySm.Get<ck::FFragment_Sm>();
            return Sm.Get_RunStatus() == ECk_SmRunStatus::Running &&
                IsValid(Sm.Get_CurrentStateClass().Get()) &&
                Allocated(Sm.Get_CurrentStateHandle());
        }), 15.0, TEXT("real authority SM state materialized")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (!Run->Built) { return; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            TestTrue(TEXT("authority and passive client worlds remain distinct"),
                IsValid(Server) && IsValid(World) && Server != World &&
                Server->GetNetMode() != NM_Client && World->GetNetMode() == NM_Client);
            if (!IsValid(Server) || !IsValid(World) || Server == World ||
                World->GetNetMode() != NM_Client) { return; }

            FCk_DebugOverlay_SourceObservation IntegerSource, SmSource;
            const bool Captured = CaptureAuthority(*Run, IntegerSource, SmSource);
            TestTrue(TEXT("real settled authority Integer and SM raw facts captured"), Captured);
            if (!Captured) { return; }

            Run->ClientRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
            if (!Allocated(Run->ClientRoot))
            { TestTrue(TEXT("passive client transient root exists"), false); return; }
            Run->PassiveScenario = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run->ClientRoot);
            if (!Allocated(Run->PassiveScenario))
            { TestTrue(TEXT("passive scenario root composed"), false); return; }
            Run->PassiveIntegerOwner = UCk_Utils_EntityLifetime_UE::
                Request_CreateEntity(Run->PassiveScenario);
            Run->PassiveSm = UCk_Utils_EntityLifetime_UE::
                Request_CreateEntity(Run->PassiveScenario);
            const bool PassiveSources = Allocated(Run->PassiveIntegerOwner) &&
                Allocated(Run->PassiveSm) && !Run->PassiveSm.Has<ck::FFragment_Sm>();
            TestTrue(TEXT("two passive client ECS source receipts, no local SM"), PassiveSources);
            if (!PassiveSources) { return; }
            IntegerSource.Source = Run->PassiveIntegerOwner;
            SmSource.Source = Run->PassiveSm;
            const TArray<FCk_DebugOverlay_SourceObservation> Sources{
                IntegerSource, SmSource};
            const bool Published = FCk_DebugOverlay_ObservationBankUtils::TryPublish(
                World, Run->ClientRoot, Run->PassiveScenario, 1, 1, Sources);
            TestTrue(TEXT("complete typed authority facts publish on client world"), Published);
            if (!Published) { return; }

            auto IntegerProvider = FindProvider(
                TEXT("Ck.OnScreenDebugger.Provider.IntegerAttributes"));
            auto SmProvider = FindProvider(
                TEXT("Ck.OnScreenDebugger.Provider.StateMachine"));
            const bool UniqueProviders = IntegerProvider.IsValid() && SmProvider.IsValid();
            TestTrue(TEXT("canonical registry returns one Integer and one SM provider"),
                UniqueProviders);
            if (!UniqueProviders) { return; }

            auto* Settings = GetMutableDefault<UCk_DebugOverlay_Settings>();
            const auto OldPatterns = Settings->AttributeFilterPatterns;
            const bool OldExclude = Settings->bAttributeFilterIsExclusion;
            const int32 OldNameDepth = Settings->SmStateNameDepth;
            const int32 OldRecursionDepth = Settings->SmMaxRecursionDepth;
            ON_SCOPE_EXIT {
                Settings->AttributeFilterPatterns = OldPatterns;
                Settings->bAttributeFilterIsExclusion = OldExclude;
                Settings->SmStateNameDepth = OldNameDepth;
                Settings->SmMaxRecursionDepth = OldRecursionDepth;
            };

            Settings->AttributeFilterPatterns.Reset();
            Settings->bAttributeFilterIsExclusion = false;
            Settings->SmStateNameDepth = 0;
            Settings->SmMaxRecursionDepth = 0;
            const auto IntegerConfig = Config(*IntegerProvider);
            const auto SmConfig = Config(*SmProvider);
            TestTrue(TEXT("authority Integer rows/token equal passive client bank rows/token"),
                SameProvider(*IntegerProvider, Run->AuthorityIntegerOwner,
                    Run->PassiveIntegerOwner, IntegerConfig) &&
                RowCount(*IntegerProvider, Run->PassiveIntegerOwner, IntegerConfig) == 1);
            TestTrue(TEXT("authority SM root rows/history/token equal passive client bank"),
                SameProvider(*SmProvider, Run->AuthoritySm, Run->PassiveSm, SmConfig));

            auto Matching = IntegerConfig;
            auto MatchExpr = FGameplayTagQueryExpression{};
            MatchExpr.AllTagsMatch().AddTag(IntegerSource.Integers[0].Label);
            Matching.EntryFilter.Build(MatchExpr);
            TestTrue(TEXT("matching Integer entry query has authority/client parity"),
                SameProvider(*IntegerProvider, Run->AuthorityIntegerOwner,
                    Run->PassiveIntegerOwner, Matching) &&
                RowCount(*IntegerProvider, Run->PassiveIntegerOwner, Matching) == 1);
            auto Excluding = IntegerConfig;
            auto ExcludeExpr = FGameplayTagQueryExpression{};
            ExcludeExpr.AllTagsMatch().AddTag(IntegerProvider->Get_ProviderTag());
            Excluding.EntryFilter.Build(ExcludeExpr);
            TestTrue(TEXT("nonmatching Integer entry query has authority/client parity"),
                SameProvider(*IntegerProvider, Run->AuthorityIntegerOwner,
                    Run->PassiveIntegerOwner, Excluding) &&
                RowCount(*IntegerProvider, Run->PassiveIntegerOwner, Excluding) == 0);

            Settings->AttributeFilterPatterns.Reset();
            Settings->AttributeFilterPatterns.Add(
                IntegerSource.Integers[0].Label.GetTagName().ToString());
            Settings->bAttributeFilterIsExclusion = false;
            TestTrue(TEXT("local Integer include setting retains authority/client parity"),
                SameProvider(*IntegerProvider, Run->AuthorityIntegerOwner,
                    Run->PassiveIntegerOwner, IntegerConfig) &&
                RowCount(*IntegerProvider, Run->PassiveIntegerOwner, IntegerConfig) == 1);
            Settings->bAttributeFilterIsExclusion = true;
            TestTrue(TEXT("local Integer exclude setting retains authority/client parity"),
                SameProvider(*IntegerProvider, Run->AuthorityIntegerOwner,
                    Run->PassiveIntegerOwner, IntegerConfig) &&
                RowCount(*IntegerProvider, Run->PassiveIntegerOwner, IntegerConfig) == 0);

            Settings->SmStateNameDepth = 1;
            Settings->SmMaxRecursionDepth = 3;
            TestTrue(TEXT("shortened-name SM setting retains authority/client parity"),
                SameProvider(*SmProvider, Run->AuthoritySm, Run->PassiveSm, SmConfig));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() -> bool {
            return Allocated(Run->PassiveScenario) &&
                Allocated(Run->AuthorityScenario);
        }), TEXT("both source roots remained live through provider comparison")));
    ADD_LATENT_AUTOMATION_COMMAND(FRetireRoots(Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            return !Allocated(Run->PassiveScenario) &&
                !Allocated(Run->AuthorityScenario);
        }), 30.0, TEXT("Selection provider fixture source roots physically retired")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
