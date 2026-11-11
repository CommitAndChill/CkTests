#include "CkTestsBridge/Server/CkTestBridge_RequestProcessor.h"

#include "CkCore/Macros/CkMacros.h"
#include "CkCore/Time/CkTime.h"

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <HAL/FileManager.h>
#include <HAL/PlatformFile.h>
#include <HAL/PlatformFileManager.h>
#include <Misc/AutomationTest.h>
#include <Misc/DateTime.h>
#include <Misc/FileHelper.h>
#include <Misc/Guid.h>
#include <Misc/Paths.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_bridge_tenet_integrity
{
    inline constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

    class FProtocolFixture
    {
    public:
        CK_GENERATED_BODY(FProtocolFixture);

    public:
        FProtocolFixture()
            : _Root{FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
                TEXT("CkTestsBridgeRegression"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))}
        {
        }

        ~FProtocolFixture()
        {
            auto& FileManager = IFileManager::Get();
            // Delete only this fixture's known files and then its empty directories. No shared
            // protocol directory, recursive deletion, or production server state is touched.
            FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Get_RequestPath(), false);
            FileManager.Delete(*Get_RequestPath(), false, false, true);
            FileManager.Delete(*Get_ResultPath(), false, false, true);
            FileManager.Delete(*(Get_ResultPath() + TEXT(".tmp")), false, false, true);
            FileManager.DeleteDirectory(*(Get_ResultPath() + TEXT(".tmp")), false, false);
            FileManager.DeleteDirectory(*Get_ResultPath(), false, false);
            FileManager.Delete(*FPaths::Combine(_Root, TEXT("Progress"), TEXT("request.jsonl")), false);
            FileManager.Delete(*FPaths::Combine(_Root, TEXT("server.json")), false);
            FileManager.DeleteDirectory(*FPaths::Combine(_Root, TEXT("Requests")), false, false);
            FileManager.DeleteDirectory(*FPaths::Combine(_Root, TEXT("Results")), false, false);
            FileManager.DeleteDirectory(*FPaths::Combine(_Root, TEXT("Progress")), false, false);
            FileManager.DeleteDirectory(*_Root, false, false);
        }

        auto
            Get_HasRequest() const
            -> bool
        { return IFileManager::Get().FileExists(*Get_RequestPath()); }

        auto
            Get_HasResult() const
            -> bool
        { return IFileManager::Get().FileExists(*Get_ResultPath()); }

        auto
            Request_WriteResult(
                const FString& InJson) const
            -> bool
        { return FFileHelper::SaveStringToFile(InJson, *Get_ResultPath()); }

        auto
            Request_RemoveResult() const
            -> bool
        { return IFileManager::Get().Delete(*Get_ResultPath(), true, false, true); }

        auto
            Request_SetRequestReadOnly(
                bool InReadOnly) const
            -> bool
        { return FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Get_RequestPath(), InReadOnly); }

        auto
            Request_BlockTempWrite() const
            -> bool
        { return IFileManager::Get().MakeDirectory(*(Get_ResultPath() + TEXT(".tmp")), false); }

        auto
            Request_UnblockTempWrite() const
            -> bool
        { return IFileManager::Get().DeleteDirectory(*(Get_ResultPath() + TEXT(".tmp")), false, false); }

        auto
            Request_BlockInstall() const
            -> bool
        { return IFileManager::Get().MakeDirectory(*Get_ResultPath(), false); }

        auto
            Request_UnblockInstall() const
            -> bool
        { return IFileManager::Get().DeleteDirectory(*Get_ResultPath(), false, false); }

        auto
            Request_WriteRequest(
                const FString& InJson) const
            -> bool
        {
            auto& FileManager = IFileManager::Get();
            for (const auto* Directory : {TEXT("Requests"), TEXT("Results"), TEXT("Progress")})
            {
                if (NOT FileManager.MakeDirectory(*FPaths::Combine(_Root, Directory), true))
                { return false; }
            }

            if (NOT FFileHelper::SaveStringToFile(InJson, *Get_RequestPath()))
            { return false; }

            // Exercise the existing settled-file gate without sleeping or depending on editor FPS.
            FileManager.SetTimeStamp(*Get_RequestPath(),
                FDateTime::UtcNow() - FTimespan::FromSeconds(FCk_Time{2.0}.Get_Seconds()));
            return true;
        }

        auto
            Get_Result() const
            -> TSharedPtr<FJsonObject>
        { return DoReadJson(Get_ResultPath()); }

        auto
            Get_Status() const
            -> TSharedPtr<FJsonObject>
        { return DoReadJson(FPaths::Combine(_Root, TEXT("server.json"))); }

    private:
        auto
            DoReadJson(
                const FString& InPath) const
            -> TSharedPtr<FJsonObject>
        {
            auto Json = FString{};
            if (NOT FFileHelper::LoadFileToString(Json, *InPath))
            { return {}; }

            auto Result = TSharedPtr<FJsonObject>{};
            const auto Reader = TJsonReaderFactory<>::Create(Json);
            if (NOT FJsonSerializer::Deserialize(Reader, Result))
            { return {}; }

            return Result;
        }

        auto
            Get_RequestPath() const
            -> FString
        { return FPaths::Combine(_Root, TEXT("Requests"), TEXT("request.json")); }

        auto
            Get_ResultPath() const
            -> FString
        { return FPaths::Combine(_Root, TEXT("Results"), TEXT("request.json")); }

    private:
        FString _Root;

    public:
        CK_PROPERTY_GET(_Root);
    };

    // --------------------------------------------------------------------------------------------------------------------

    auto
        Get_CleanEnvironment(
            bool,
            bool,
            FCk_TestBridge_Env& OutEnv)
        -> TOptional<FString>
    {
        OutEnv = FCk_TestBridge_Env{};
        return {};
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_TestBridge_TenetIntegrity_InfrastructureFailureIsNotSuccess,
    "Ck.TestsBridge.TenetIntegrity.InfrastructureFailureIsNotSuccess", ck_test_bridge_tenet_integrity::TestFlags)

auto
    FCk_TestBridge_TenetIntegrity_InfrastructureFailureIsNotSuccess::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_bridge_tenet_integrity;

    AddExpectedErrorPlain(TEXT("[RunController] AutomationController manager is null"),
        EAutomationExpectedErrorFlags::Contains, -1);

    const auto Fixture = FProtocolFixture{};
    if (NOT TestTrue(TEXT("a settled valid request exists"), Fixture.Request_WriteRequest(
        TEXT("{\"op\":\"runTests\",\"tests\":[\"Ck.TestsBridge.RequestedLeaf\"]}"))))
    { return false; }

    auto ResolveCount = int32{0};
    auto Processor = FCk_TestBridge_RequestProcessor{Fixture.Get_Root(), [&ResolveCount]()
        -> TSharedPtr<IAutomationControllerManager>
    {
        ++ResolveCount;
        return {};
    }, Get_CleanEnvironment};

    // No Startup: the fixture supplies actual protocol directories without claiming server.json
    // or waiting on the shared editor AssetRegistry. Begin is reached through real admission.
    Processor.ProcessPending(0.0f);
    Processor.ProcessPending(0.0f);
    TestEqual(TEXT("the actual runner tried to resolve its controller once"), ResolveCount, 1);

    const auto Result = Fixture.Get_Result();
    if (NOT TestTrue(TEXT("infrastructure failure still publishes a terminal response"), Result.IsValid()))
    { return false; }

    auto Ok = true;
    TestTrue(TEXT("the response contains an explicit verdict"), Result->TryGetBoolField(TEXT("ok"), Ok));
    TestFalse(TEXT("a requested test cannot pass when the automation controller was unavailable"), Ok);
    auto Error = FString{};
    TestTrue(TEXT("infrastructure failure has a run-level diagnostic"), Result->TryGetStringField(TEXT("error"), Error));
    TestFalse(TEXT("the diagnostic is nonempty"), Error.IsEmpty());
    const TArray<TSharedPtr<FJsonValue>>* PerTest = nullptr;
    if (TestTrue(TEXT("requested tests are accounted for"), Result->TryGetArrayField(TEXT("perTest"), PerTest))
        && PerTest != nullptr && TestEqual(TEXT("one requested leaf has one outcome"), PerTest->Num(), 1)
        && (*PerTest)[0].IsValid() && (*PerTest)[0]->Type == EJson::Object)
    {
        auto State = FString{};
        (*PerTest)[0]->AsObject()->TryGetStringField(TEXT("result"), State);
        TestEqual(TEXT("the unavailable controller did not run the leaf"), State, FString{TEXT("NotRun")});
    }
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_TestBridge_TenetIntegrity_MalformedElementRejectsEntireRequest,
    "Ck.TestsBridge.TenetIntegrity.MalformedElementRejectsEntireRequest", ck_test_bridge_tenet_integrity::TestFlags)

auto
    FCk_TestBridge_TenetIntegrity_MalformedElementRejectsEntireRequest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_bridge_tenet_integrity;

    // The old parser reaches Begin despite the malformed list; suppress only that known
    // infrastructure diagnostic so the regression fails on the admission assertions themselves.
    AddExpectedErrorPlain(TEXT("[RunController] AutomationController manager is null"),
        EAutomationExpectedErrorFlags::Contains, -1);

    const auto Fixture = FProtocolFixture{};
    if (NOT TestTrue(TEXT("a settled mixed-type request exists"), Fixture.Request_WriteRequest(
        TEXT("{\"op\":\"runTests\",\"tests\":[\"Ck.TestsBridge.RequestedLeaf\",42]}"))))
    { return false; }

    auto ResolveCount = int32{0};
    auto Processor = FCk_TestBridge_RequestProcessor{Fixture.Get_Root(), [&ResolveCount]()
        -> TSharedPtr<IAutomationControllerManager>
    {
        ++ResolveCount;
        return {};
    }, Get_CleanEnvironment};

    Processor.ProcessPending(0.0f);
    TestEqual(TEXT("malformed admission must not reach the automation controller"), ResolveCount, 0);
    TestFalse(TEXT("no successful subset may be left runnable"), Processor.Get_IsBusy());
    Processor.ProcessPending(0.0f);

    const auto Result = Fixture.Get_Result();
    if (NOT TestTrue(TEXT("rejection publishes a terminal response"), Result.IsValid()))
    { return false; }

    auto Ok = true;
    TestTrue(TEXT("the response contains an explicit verdict"), Result->TryGetBoolField(TEXT("ok"), Ok));
    TestFalse(TEXT("a mixed-type test list is rejected as a whole"), Ok);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_TestBridge_TenetIntegrity_PublicationFaultRetainsCapturedOutcome,
    "Ck.TestsBridge.TenetIntegrity.PublicationFaultRetainsCapturedOutcome", ck_test_bridge_tenet_integrity::TestFlags)

auto
    FCk_TestBridge_TenetIntegrity_PublicationFaultRetainsCapturedOutcome::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_bridge_tenet_integrity;
    AddExpectedErrorPlain(TEXT("[RunController] AutomationController manager is null"), EAutomationExpectedErrorFlags::Contains, -1);
    // The real file helper and our diagnostic both name this deliberately obstructed path.
    AddExpectedErrorPlain(TEXT("request.json.tmp"), EAutomationExpectedErrorFlags::Contains, -1);

    const auto Fixture = FProtocolFixture{};
    if (NOT TestTrue(TEXT("valid request written"), Fixture.Request_WriteRequest(
        TEXT("{\"op\":\"runTests\",\"tests\":[\"Ck.TestsBridge.RequestedLeaf\"]}")))
        || NOT TestTrue(TEXT("real temp-file obstruction installed"), Fixture.Request_BlockTempWrite()))
    { return false; }

    auto ResolveCount = int32{0};
    auto Processor = FCk_TestBridge_RequestProcessor{Fixture.Get_Root(), [&ResolveCount]()
        -> TSharedPtr<IAutomationControllerManager>
    {
        ++ResolveCount;
        return {};
    }, Get_CleanEnvironment};
    Processor.ProcessPending(0.0f);
    const auto Fault = Processor.ProcessPending(0.0f);
    TestTrue(TEXT("publication failure is an explicit terminal fault"), Fault.TerminalFault);
    TestTrue(TEXT("the request remains owned"), Processor.Get_IsBusy() && Fixture.Get_HasRequest());
    TestFalse(TEXT("no partial result is exposed"), Fixture.Get_HasResult());

    const auto Status = Fixture.Get_Status();
    if (NOT TestTrue(TEXT("ownership heartbeat remains readable"), Status.IsValid()))
    { return false; }
    auto Faulted = false;
    TestTrue(TEXT("heartbeat classifies the fault"), Status->TryGetBoolField(TEXT("faulted"), Faulted) && Faulted);

    if (NOT TestTrue(TEXT("obstruction removed"), Fixture.Request_UnblockTempWrite())
        || NOT TestTrue(TEXT("the original input changes after capture"), Fixture.Request_WriteRequest(TEXT("{\"op\":\"quit\"}"))))
    { return false; }
    TestTrue(TEXT("ordinary polling does not retry even after access recovers"), Processor.ProcessPending(0.0f).TerminalFault);
    TestFalse(TEXT("ordinary polling leaves the result absent"), Fixture.Get_HasResult());

    const auto Recovery = Processor.Request_RecoverPublication();
    TestFalse(TEXT("explicit recovery delivers the retained response"), Recovery.TerminalFault);
    TestFalse(TEXT("recovery does not reparse the changed quit request"), Recovery.QuitRequested);
    TestFalse(TEXT("the original request is removed after delivery"), Fixture.Get_HasRequest());
    TestFalse(TEXT("recovery releases the request slot"), Processor.Get_IsBusy());
    TestEqual(TEXT("automation is never re-executed"), ResolveCount, 1);
    const auto Result = Fixture.Get_Result();
    if (NOT TestTrue(TEXT("the captured outcome is now published"), Result.IsValid()))
    { return false; }
    auto Ok = true;
    Result->TryGetBoolField(TEXT("ok"), Ok);
    TestFalse(TEXT("recovery preserves the original failed outcome"), Ok);
    TestFalse(TEXT("a second recovery has no work"), Processor.Request_RecoverPublication().AnyProcessed);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_TestBridge_TenetIntegrity_QuitWaitsForPublicationRecovery,
    "Ck.TestsBridge.TenetIntegrity.QuitWaitsForPublicationRecovery", ck_test_bridge_tenet_integrity::TestFlags)

auto
    FCk_TestBridge_TenetIntegrity_QuitWaitsForPublicationRecovery::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_bridge_tenet_integrity;
    AddExpectedErrorPlain(TEXT("could not install terminal response"), EAutomationExpectedErrorFlags::Contains, -1);
    const auto Fixture = FProtocolFixture{};
    if (NOT TestTrue(TEXT("quit request written"), Fixture.Request_WriteRequest(TEXT("{\"op\":\"quit\"}")))
        || NOT TestTrue(TEXT("real destination obstruction installed"), Fixture.Request_BlockInstall()))
    { return false; }

    auto ResolveCount = int32{0};
    auto Processor = FCk_TestBridge_RequestProcessor{Fixture.Get_Root(), [&ResolveCount]()
        -> TSharedPtr<IAutomationControllerManager>
    {
        ++ResolveCount;
        return {};
    }, Get_CleanEnvironment};
    const auto Fault = Processor.ProcessPending(0.0f);
    TestTrue(TEXT("failed install faults the request"), Fault.TerminalFault);
    TestFalse(TEXT("failed install cannot release the serving claim for quit"), Fault.QuitRequested);
    TestTrue(TEXT("quit input is retained"), Fixture.Get_HasRequest());
    if (NOT TestTrue(TEXT("destination repaired"), Fixture.Request_UnblockInstall()))
    { return false; }
    TestTrue(TEXT("ordinary poll remains faulted"), Processor.ProcessPending(0.0f).TerminalFault);
    const auto Recovery = Processor.Request_RecoverPublication();
    TestFalse(TEXT("recovery installed the response"), Recovery.TerminalFault);
    TestTrue(TEXT("quit is delivered only after response and deletion complete"), Recovery.QuitRequested);
    TestFalse(TEXT("quit input is now consumed"), Fixture.Get_HasRequest());
    TestTrue(TEXT("quit result exists"), Fixture.Get_HasResult());
    TestEqual(TEXT("quit never starts automation"), ResolveCount, 0);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_TestBridge_TenetIntegrity_DeletionFaultRecoversWithoutRepublication,
    "Ck.TestsBridge.TenetIntegrity.DeletionFaultRecoversWithoutRepublication", ck_test_bridge_tenet_integrity::TestFlags)

auto
    FCk_TestBridge_TenetIntegrity_DeletionFaultRecoversWithoutRepublication::
    RunTest(const FString&)
    -> bool
{
#if PLATFORM_WINDOWS
    using namespace ck_test_bridge_tenet_integrity;
    AddExpectedErrorPlain(TEXT("could not delete published request"), EAutomationExpectedErrorFlags::Contains, -1);
    const auto Fixture = FProtocolFixture{};
    if (NOT TestTrue(TEXT("invalid-op request written"), Fixture.Request_WriteRequest(TEXT("{\"op\":\"invalid-op\"}")))
        || NOT TestTrue(TEXT("request made read-only"), Fixture.Request_SetRequestReadOnly(true)))
    { return false; }

    auto Processor = FCk_TestBridge_RequestProcessor{Fixture.Get_Root(), {}, Get_CleanEnvironment};
    const auto Fault = Processor.ProcessPending(0.0f);
    TestTrue(TEXT("real request deletion failure is terminal"), Fault.TerminalFault);
    TestTrue(TEXT("installed result is retained along with original input"), Fixture.Get_HasResult() && Fixture.Get_HasRequest());
    if (NOT TestTrue(TEXT("delete access restored"), Fixture.Request_SetRequestReadOnly(false))
        || NOT TestTrue(TEXT("temp writes remain unavailable during deletion recovery"), Fixture.Request_BlockTempWrite()))
    { return false; }

    const auto Recovery = Processor.Request_RecoverPublication();
    TestFalse(TEXT("recovery retries deletion without needing another temp write"), Recovery.TerminalFault);
    TestTrue(TEXT("published result is preserved"), Fixture.Get_HasResult());
    TestFalse(TEXT("original input is consumed after explicit recovery"), Fixture.Get_HasRequest());
#else
    AddInfo(TEXT("The read-only deletion fixture exercises the selected Windows file manager contract."));
#endif
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_TestBridge_TenetIntegrity_ConflictingResultIsPreserved,
    "Ck.TestsBridge.TenetIntegrity.ConflictingResultIsPreserved", ck_test_bridge_tenet_integrity::TestFlags)

auto
    FCk_TestBridge_TenetIntegrity_ConflictingResultIsPreserved::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_bridge_tenet_integrity;
    AddExpectedErrorPlain(TEXT("contains a conflicting response"), EAutomationExpectedErrorFlags::Contains, -1);
    const auto Fixture = FProtocolFixture{};
    if (NOT TestTrue(TEXT("request written"), Fixture.Request_WriteRequest(TEXT("{\"op\":\"invalid-op\"}")))
        || NOT TestTrue(TEXT("prior verdict installed"), Fixture.Request_WriteResult(TEXT("{\"ok\":true,\"marker\":\"prior verdict\"}"))))
    { return false; }

    auto Processor = FCk_TestBridge_RequestProcessor{Fixture.Get_Root(), {}, Get_CleanEnvironment};
    TestTrue(TEXT("conflicting installed result faults delivery"), Processor.ProcessPending(0.0f).TerminalFault);
    const auto Prior = Fixture.Get_Result();
    if (NOT TestTrue(TEXT("prior verdict remains readable"), Prior.IsValid()))
    { return false; }
    auto Marker = FString{};
    Prior->TryGetStringField(TEXT("marker"), Marker);
    TestEqual(TEXT("conflict never overwrites prior bytes"), Marker, FString{TEXT("prior verdict")});
    TestTrue(TEXT("input remains owned"), Fixture.Get_HasRequest());
    if (NOT TestTrue(TEXT("fixture owner removes conflicting result"), Fixture.Request_RemoveResult()))
    { return false; }

    TestFalse(TEXT("explicit recovery installs the captured response"), Processor.Request_RecoverPublication().TerminalFault);
    const auto Recovered = Fixture.Get_Result();
    if (NOT TestTrue(TEXT("captured rejection is readable"), Recovered.IsValid()))
    { return false; }
    auto Ok = true;
    Recovered->TryGetBoolField(TEXT("ok"), Ok);
    TestFalse(TEXT("the captured rejection replaces only the now-vacant slot"), Ok);
    TestFalse(TEXT("original input is consumed after delivery"), Fixture.Get_HasRequest());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_TestBridge_TenetIntegrity_DirtyPackagesRequireExplicitOptIn,
    "Ck.TestsBridge.TenetIntegrity.DirtyPackagesRequireExplicitOptIn", ck_test_bridge_tenet_integrity::TestFlags)

auto
    FCk_TestBridge_TenetIntegrity_DirtyPackagesRequireExplicitOptIn::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_bridge_tenet_integrity;
    AddExpectedErrorPlain(TEXT("[RunController] AutomationController manager is null"), EAutomationExpectedErrorFlags::Contains, -1);
    for (const auto AllowDirty : {false, true})
    {
        const auto Fixture = FProtocolFixture{};
        const auto Json = FString::Printf(TEXT("{\"op\":\"runTests\",\"tests\":[\"Ck.TestsBridge.RequestedLeaf\"],"
            "\"options\":{\"allowDirtyWorld\":%s}}"), AllowDirty ? TEXT("true") : TEXT("false"));
        if (NOT TestTrue(TEXT("request written"), Fixture.Request_WriteRequest(Json)))
        { return false; }

        auto ResolveCount = int32{0};
        auto Processor = FCk_TestBridge_RequestProcessor{Fixture.Get_Root(), [&ResolveCount]()
            -> TSharedPtr<IAutomationControllerManager>
        {
            ++ResolveCount;
            return {};
        }, [](bool InAllowDirty, bool InBusy, FCk_TestBridge_Env& OutEnv) -> TOptional<FString>
        {
            OutEnv = FCk_TestBridge_Env{};
            OutEnv.DirtyPackages.Add(TEXT("/Game/AutoTests_UnsavedUserWork"));
            return FCk_TestBridge_Preconditions::Evaluate_Environment(InAllowDirty, InBusy, false, OutEnv);
        }};
        Processor.ProcessPending(0.0f);
        Processor.ProcessPending(0.0f);
        TestEqual(TEXT("only explicit opt-in can reach automation with dirty packages"), ResolveCount, AllowDirty ? 1 : 0);
        const auto Result = Fixture.Get_Result();
        if (NOT TestTrue(TEXT("request has a terminal response"), Result.IsValid()))
        { return false; }
        auto Refused = false;
        TestTrue(TEXT("response identifies refusal state"), Result->TryGetBoolField(TEXT("refused"), Refused));
        TestEqual(TEXT("AutoTests in a package name grants no exemption"), Refused, NOT AllowDirty);
        if (NOT AllowDirty)
        {
            auto Reason = FString{};
            Result->TryGetStringField(TEXT("refusalReason"), Reason);
            TestEqual(TEXT("the production policy names unsaved work"), Reason, FString{TEXT("dirtyWorld")});
        }
    }
    return true;
}

#endif
