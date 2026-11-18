// Which spawns the Ck Entity Bridge and the raw EntityScript spawn and Add refuse, that a refusal never destroys an
// entity the caller owns, that a request whose script object cannot be created leaves the bridge free to request again,
// and that an Actor did request during BeginPlay.
//
// Each refusal is decided by its observable outcome (no entity, an invalid pending handle, Failed_NotEnqueued, the
// bridge state unchanged) and by the ensure occurrence tracker, which counts every fire, so an ensure whose log line an
// earlier test already produced still counts here. The log line itself is tolerated, as Test_Signal_InvalidHandle does.
//
// Surface in Session Frontend: Ck.EntityBridge.Admission.*

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Ensure/CkEnsure_Utils.h"
#include "CkCore/Format/CkFormat.h"

#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkTests/Net/CkAutoTest_EntityBridgeSubject.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

#include <StructUtils/InstancedStruct.h>
#include <tuple>

// --------------------------------------------------------------------------------------------------------------------

namespace ck_entity_bridge_admission
{
    // The listen server's own world; none of these tests needs a remote client.
    constexpr auto NumPIEClients = 1;
    constexpr auto PIEReadyTimeoutSeconds = 30.0f;
    constexpr auto BridgeReadyTimeoutSeconds = 15.0;
    constexpr auto FramesAfterRefusal = 5;
    constexpr auto FramesBeforeLateRequest = 30;

    constexpr auto BridgeSourceFileFragment = TEXT("CkEntityBridge");

    using SubjectClassType = TSubclassOf<ACk_AutoTest_EntityBridgeSubject>;

    auto
        Do_StartPIE()
        -> void
    {
        // CkActorRelay x Iris ambient noise under PIE, as in the other net specs. Restored by Do_EndPIE.
        FAutomationTestBase::bSuppressLogErrors = true;
        FAutomationTestBase::bSuppressLogWarnings = true;

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(NumPIEClients, FString{TEXT("/Engine/Maps/Entry")}));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(NumPIEClients, PIEReadyTimeoutSeconds));
    }

    auto
        Do_EndPIE(
            FAutomationTestBase& InTest)
        -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

        // Restore static log suppression after teardown so subsequent tests see default semantics.
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(&InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
            {
                FAutomationTestBase::bSuppressLogErrors = false;
                FAutomationTestBase::bSuppressLogWarnings = false;
                return true;
            }),
            TEXT("restore log suppression statics")));
    }

    auto
        Do_Spawn(
            FAutomationTestBase& InTest,
            UWorld* InWorld,
            SubjectClassType InClass)
        -> ACk_AutoTest_EntityBridgeSubject*
    {
        auto SpawnInfo = FActorSpawnParameters{};
        SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        auto* Subject = InWorld->SpawnActor<ACk_AutoTest_EntityBridgeSubject>(InClass, FTransform::Identity, SpawnInfo);

        if (ck::Is_NOT_Valid(Subject))
        { InTest.AddError(ck::Format_UE(TEXT("spawn of [{}] returned null"), GetNameSafe(InClass.Get()))); }

        return Subject;
    }

    auto
        Do_TestState(
            FAutomationTestBase& InTest,
            const ACk_AutoTest_EntityBridgeSubject& InSubject,
            ECk_EntityBridge_State InExpected,
            const TCHAR* InWhen)
        -> void
    {
        const auto State = InSubject.Get_EntityBridge()->Get_State();

        InTest.TestTrue(*ck::Format_UE(TEXT("{}: bridge state is [{}] (is [{}])"), InWhen, InExpected, State),
            State == InExpected);
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeAdmission_SecondRequestRefused,
    "Ck.EntityBridge.Admission.SecondRequestRefused",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeAdmission_SecondRequestRefused::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_admission;

    // Occurrences < 0: every matching line is tolerated, including none when the log line was already consumed.
    AddExpectedError(TEXT("called more than once"), EAutomationExpectedErrorFlags::Contains, -1);

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject_NeverRequests::StaticClass()};
    const auto ScriptClass = UCk_AutoTest_EntityBridge_EntityScript_UE::StaticClass();

    Do_StartPIE();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass, ScriptClass](UWorld* InServer) -> void
        {
            auto* Subject = Do_Spawn(*this, InServer, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return; }

            auto* Bridge = Subject->Get_EntityBridge().Get();

            const auto EnsuresBeforeFirst = UCk_Utils_Ensure_UE::Get_EnsureCount();
            Bridge->Request_SpawnLinkedEntity(ScriptClass);
            const auto EnsuresAfterFirst = UCk_Utils_Ensure_UE::Get_EnsureCount();
            Bridge->Request_SpawnLinkedEntity(ScriptClass);
            const auto EnsuresAfterSecond = UCk_Utils_Ensure_UE::Get_EnsureCount();

            TestEqual(TEXT("the first Request_SpawnLinkedEntity did not ensure"), EnsuresAfterFirst - EnsuresBeforeFirst, 0);
            TestEqual(TEXT("the second Request_SpawnLinkedEntity ensured once"), EnsuresAfterSecond - EnsuresAfterFirst, 1);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SubjectClass]() -> bool
        {
            const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(ck::auto_test::net::Get_ServerWorld(), SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return false; }

            return Subject->Get_EntityBridge()->Get_State() == ECk_EntityBridge_State::Ready;
        }),
        BridgeReadyTimeoutSeconds,
        TEXT("the first request's entity is Ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass, ScriptClass]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(Server, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            {
                AddError(TEXT("the subject is missing"));
                return false;
            }

            Do_TestState(*this, *Subject, ECk_EntityBridge_State::Ready, TEXT("after both requests"));
            TestEqual(TEXT("exactly one entity is linked to the subject"),
                ck::auto_test::entity_bridge::Get_LinkedEntityCount(Server, Subject), 1);
            TestEqual(TEXT("exactly one entity runs the script"),
                ck::auto_test::entity_bridge::Get_ScriptEntityCount(Server, ScriptClass), 1);

            return true;
        }),
        TEXT("a second Request_SpawnLinkedEntity is refused and the first one's entity stands")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeAdmission_PlainScriptThroughBridgeRefused,
    "Ck.EntityBridge.Admission.PlainScriptThroughBridgeRefused",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeAdmission_PlainScriptThroughBridgeRefused::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_admission;

    // Occurrences < 0: every matching line is tolerated, including none when the log line was already consumed.
    AddExpectedError(TEXT("is not an actor-bound EntityScript"), EAutomationExpectedErrorFlags::Contains, -1);

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject_NeverRequests::StaticClass()};
    const auto PlainScriptClass = UCk_AutoTest_EntityBridge_PlainEntityScript_UE::StaticClass();

    Do_StartPIE();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass, PlainScriptClass](UWorld* InServer) -> void
        {
            auto* Subject = Do_Spawn(*this, InServer, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return; }

            const auto EnsuresBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();
            Subject->Get_EntityBridge()->Request_SpawnLinkedEntity(PlainScriptClass);
            const auto EnsuresAfter = UCk_Utils_Ensure_UE::Get_EnsureCount();

            TestEqual(TEXT("passing a non-actor-bound script to the bridge ensured once"), EnsuresAfter - EnsuresBefore, 1);
            Do_TestState(*this, *Subject, ECk_EntityBridge_State::NotRequested, TEXT("right after the refused request"));
            TestFalse(TEXT("right after the refused request: the bridge has no entity"),
                ck::IsValid(Subject->Get_EntityBridge()->Get_Entity()));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterRefusal));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass, PlainScriptClass]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(Server, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            {
                AddError(TEXT("the subject is missing"));
                return false;
            }

            Do_TestState(*this, *Subject, ECk_EntityBridge_State::NotRequested, TEXT("frames later"));
            TestFalse(TEXT("frames later: the bridge has no entity"), ck::IsValid(Subject->Get_EntityBridge()->Get_Entity()));
            TestEqual(TEXT("no entity is linked to the subject"),
                ck::auto_test::entity_bridge::Get_LinkedEntityCount(Server, Subject), 0);
            TestEqual(TEXT("no entity runs the plain script"),
                ck::auto_test::entity_bridge::Get_ScriptEntityCount(Server, PlainScriptClass), 0);

            return true;
        }),
        TEXT("the bridge refuses a script that is not actor-bound")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeAdmission_ActorBoundThroughRawSpawnRefused,
    "Ck.EntityBridge.Admission.ActorBoundThroughRawSpawnRefused",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeAdmission_ActorBoundThroughRawSpawnRefused::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_admission;

    // Occurrences < 0: every matching line is tolerated, including none when the log line was already consumed.
    AddExpectedError(TEXT("is actor-bound. Add a Ck Entity Bridge"), EAutomationExpectedErrorFlags::Contains, -1);

    const auto ScriptClass = UCk_AutoTest_EntityBridge_EntityScript_UE::StaticClass();

    // Owned by the spec for the whole run; the bound dynamic delegate only weak-references it.
    auto Capturer = MakeShared<TStrongObjectPtr<UCk_AutoTest_EntityBridge_Capturer_UE>>(
        NewObject<UCk_AutoTest_EntityBridge_Capturer_UE>(GetTransientPackage()));

    Do_StartPIE();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, ScriptClass, Capturer](UWorld* InServer) -> void
        {
            auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InServer);

            auto Delegate = FCk_Delegate_Request_OnCompleted{};
            Delegate.BindDynamic(Capturer->Get(), &UCk_AutoTest_EntityBridge_Capturer_UE::OnRequestCompleted);

            const auto EnsuresBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();
            const auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(TransientEntity, ScriptClass, FInstancedStruct{}, Delegate);
            const auto EnsuresAfter = UCk_Utils_Ensure_UE::Get_EnsureCount();

            TestEqual(TEXT("a raw spawn of an actor-bound script ensured once"), EnsuresAfter - EnsuresBefore, 1);
            TestFalse(TEXT("the raw spawn returned an invalid pending handle"), UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending));
            TestEqual(TEXT("the completion delegate fired synchronously, once"), (*Capturer)->_RequestCompletedCount, 1);
            TestTrue(*ck::Format_UE(TEXT("the completion delegate reported Failed_NotEnqueued (reported [{}])"),
                    (*Capturer)->_RequestResult),
                (*Capturer)->_RequestResult == ECk_Request_OperationResult::Failed_NotEnqueued);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterRefusal));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, ScriptClass, Capturer]() -> bool
        {
            TestEqual(TEXT("no entity runs the actor-bound script"),
                ck::auto_test::entity_bridge::Get_ScriptEntityCount(ck::auto_test::net::Get_ServerWorld(), ScriptClass), 0);
            TestEqual(TEXT("the completion delegate fired once in total"), (*Capturer)->_RequestCompletedCount, 1);

            return true;
        }),
        TEXT("the raw EntityScript spawn refuses an actor-bound script up front")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeAdmission_LateRequestIsAccepted,
    "Ck.EntityBridge.Admission.LateRequestIsAccepted",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeAdmission_LateRequestIsAccepted::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_admission;

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject_NeverRequests::StaticClass()};
    const auto ScriptClass = UCk_AutoTest_EntityBridge_EntityScript_UE::StaticClass();

    struct FObservations
    {
        int64 _BridgeEnsuresBefore = 0;
    };

    auto Observations = MakeShared<FObservations>();

    Do_StartPIE();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Observations, SubjectClass](UWorld* InServer) -> void
        {
            Observations->_BridgeEnsuresBefore = ck::auto_test::entity_bridge::Get_EnsureCount_FromFiles(BridgeSourceFileFragment);
            std::ignore = Do_Spawn(*this, InServer, SubjectClass);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesBeforeLateRequest));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Observations, SubjectClass]() -> bool
        {
            auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(ck::auto_test::net::Get_ServerWorld(), SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            {
                AddError(TEXT("the subject is missing"));
                return false;
            }

            Do_TestState(*this, *Subject, ECk_EntityBridge_State::NotRequested, TEXT("before the late request"));
            TestEqual(TEXT("a bridge whose Actor has not requested yet did not ensure"),
                ck::auto_test::entity_bridge::Get_EnsureCount_FromFiles(BridgeSourceFileFragment) - Observations->_BridgeEnsuresBefore,
                int64{0});

            return true;
        }),
        TEXT("an unrequested bridge stays quiet")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([SubjectClass, ScriptClass](UWorld* InServer) -> void
        {
            auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(InServer, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return; }

            Subject->Get_EntityBridge()->Request_SpawnLinkedEntity(ScriptClass);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SubjectClass]() -> bool
        {
            const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(ck::auto_test::net::Get_ServerWorld(), SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return false; }

            return Subject->Get_EntityBridge()->Get_State() == ECk_EntityBridge_State::Ready;
        }),
        BridgeReadyTimeoutSeconds,
        TEXT("the late request's entity is Ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(Server, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            {
                AddError(TEXT("the subject is missing"));
                return false;
            }

            TestEqual(TEXT("exactly one entity is linked to the subject after the late request"),
                ck::auto_test::entity_bridge::Get_LinkedEntityCount(Server, Subject), 1);

            return true;
        }),
        TEXT("a request made long after BeginPlay builds the Actor's entity")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeAdmission_ActorBoundThroughAddRefusedAndEntityKept,
    "Ck.EntityBridge.Admission.ActorBoundThroughAddRefusedAndEntityKept",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeAdmission_ActorBoundThroughAddRefusedAndEntityKept::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_admission;

    // Occurrences < 0: every matching line is tolerated, including none when the log line was already consumed.
    AddExpectedError(TEXT("cannot be added to Entity"), EAutomationExpectedErrorFlags::Contains, -1);

    const auto ScriptClass = UCk_AutoTest_EntityBridge_EntityScript_UE::StaticClass();

    // Owned by the spec for the whole run; the bound dynamic delegate only weak-references it.
    auto Capturer = MakeShared<TStrongObjectPtr<UCk_AutoTest_EntityBridge_Capturer_UE>>(
        NewObject<UCk_AutoTest_EntityBridge_Capturer_UE>(GetTransientPackage()));
    auto CallerEntity = MakeShared<FCk_Handle>();

    Do_StartPIE();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, ScriptClass, Capturer, CallerEntity](UWorld* InServer) -> void
        {
            const auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InServer);
            *CallerEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);

            auto Delegate = FCk_Delegate_Request_OnCompleted{};
            Delegate.BindDynamic(Capturer->Get(), &UCk_AutoTest_EntityBridge_Capturer_UE::OnRequestCompleted);

            const auto EnsuresBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();
            const auto Pending = UCk_Utils_EntityScript_UE::Add(*CallerEntity, ScriptClass, FInstancedStruct{}, nullptr, Delegate);
            const auto EnsuresAfter = UCk_Utils_Ensure_UE::Get_EnsureCount();

            TestEqual(TEXT("adding an actor-bound script to an existing entity ensured once"), EnsuresAfter - EnsuresBefore, 1);
            TestFalse(TEXT("the Add returned an invalid pending handle"), UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending));
            TestEqual(TEXT("the completion delegate fired synchronously, once"), (*Capturer)->_RequestCompletedCount, 1);
            TestTrue(*ck::Format_UE(TEXT("the completion delegate reported Failed_NotEnqueued (reported [{}])"),
                    (*Capturer)->_RequestResult),
                (*Capturer)->_RequestResult == ECk_Request_OperationResult::Failed_NotEnqueued);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterRefusal));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, ScriptClass, Capturer, CallerEntity]() -> bool
        {
            TestTrue(TEXT("the caller's entity is still alive"), ck::IsValid(*CallerEntity));
            TestFalse(TEXT("the caller's entity has no EntityScript"),
                ck::IsValid(*CallerEntity) && UCk_Utils_EntityScript_UE::Has(*CallerEntity));
            TestEqual(TEXT("no entity runs the actor-bound script"),
                ck::auto_test::entity_bridge::Get_ScriptEntityCount(ck::auto_test::net::Get_ServerWorld(), ScriptClass), 0);
            TestEqual(TEXT("the completion delegate fired once in total"), (*Capturer)->_RequestCompletedCount, 1);

            return true;
        }),
        TEXT("Add refuses an actor-bound script up front and leaves the caller's entity alone")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeAdmission_ScriptCreationFailureReleasesTheRequest,
    "Ck.EntityBridge.Admission.ScriptCreationFailureReleasesTheRequest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeAdmission_ScriptCreationFailureReleasesTheRequest::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_admission;

    // Occurrences < 0: every matching line is tolerated, including none when the log line was already consumed.
    AddExpectedError(TEXT("Failed to Spawn New Entity using EntityScript"), EAutomationExpectedErrorFlags::Contains, -1);

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject_NeverRequests::StaticClass()};
    const auto FailingScriptClass = UCk_AutoTest_EntityBridge_PoolExhaustedEntityScript_UE::StaticClass();
    const auto ScriptClass = UCk_AutoTest_EntityBridge_EntityScript_UE::StaticClass();

    struct FObservations
    {
        int32 _EnsuresBefore = 0;
    };

    auto Observations = MakeShared<FObservations>();

    Do_StartPIE();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Observations, SubjectClass, FailingScriptClass](UWorld* InServer) -> void
        {
            auto* Subject = Do_Spawn(*this, InServer, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return; }

            Observations->_EnsuresBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();
            Subject->Get_EntityBridge()->Request_SpawnLinkedEntity(FailingScriptClass);
            Do_TestState(*this, *Subject, ECk_EntityBridge_State::AwaitingSpawn, TEXT("right after the request"));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterRefusal));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Observations, SubjectClass, FailingScriptClass]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(Server, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            {
                AddError(TEXT("the subject is missing"));
                return false;
            }

            TestEqual(TEXT("the failed script creation ensured once"),
                UCk_Utils_Ensure_UE::Get_EnsureCount() - Observations->_EnsuresBefore, 1);
            Do_TestState(*this, *Subject, ECk_EntityBridge_State::NotRequested, TEXT("after the failed spawn"));
            TestFalse(TEXT("after the failed spawn: the bridge has no entity"), ck::IsValid(Subject->Get_EntityBridge()->Get_Entity()));
            TestEqual(TEXT("no entity is linked to the subject"),
                ck::auto_test::entity_bridge::Get_LinkedEntityCount(Server, Subject), 0);
            TestEqual(TEXT("no entity runs the failing script"),
                ck::auto_test::entity_bridge::Get_ScriptEntityCount(Server, FailingScriptClass), 0);

            return true;
        }),
        TEXT("a spawn whose script object cannot be created links nothing")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass, ScriptClass](UWorld* InServer) -> void
        {
            auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(InServer, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return; }

            const auto EnsuresBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();
            Subject->Get_EntityBridge()->Request_SpawnLinkedEntity(ScriptClass);

            TestEqual(TEXT("the retry after the failed spawn did not ensure"), UCk_Utils_Ensure_UE::Get_EnsureCount() - EnsuresBefore, 0);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SubjectClass]() -> bool
        {
            const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(ck::auto_test::net::Get_ServerWorld(), SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return false; }

            return Subject->Get_EntityBridge()->Get_State() == ECk_EntityBridge_State::Ready;
        }),
        BridgeReadyTimeoutSeconds,
        TEXT("the retry's entity is Ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(Server, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            {
                AddError(TEXT("the subject is missing"));
                return false;
            }

            TestEqual(TEXT("exactly one entity is linked to the subject after the retry"),
                ck::auto_test::entity_bridge::Get_LinkedEntityCount(Server, Subject), 1);

            return true;
        }),
        TEXT("the bridge accepts a new request after a failed spawn")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeAdmission_SecondBridgeOnActorRefused,
    "Ck.EntityBridge.Admission.SecondBridgeOnActorRefused",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeAdmission_SecondBridgeOnActorRefused::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_admission;

    // Occurrences < 0: every matching line is tolerated, including none when the log line was already consumed.
    AddExpectedError(TEXT("may have only one bridge"), EAutomationExpectedErrorFlags::Contains, -1);

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject_TwoBridges::StaticClass()};
    const auto ScriptClass = UCk_AutoTest_EntityBridge_EntityScript_UE::StaticClass();

    Do_StartPIE();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass, ScriptClass](UWorld* InServer) -> void
        {
            auto* Subject = Cast<ACk_AutoTest_EntityBridgeSubject_TwoBridges>(Do_Spawn(*this, InServer, SubjectClass));

            if (ck::Is_NOT_Valid(Subject))
            { return; }

            const auto EnsuresBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();
            Subject->Get_EntityBridge()->Request_SpawnLinkedEntity(ScriptClass);
            const auto EnsuresAfterFirst = UCk_Utils_Ensure_UE::Get_EnsureCount();
            Subject->Get_SecondEntityBridge()->Request_SpawnLinkedEntity(ScriptClass);
            const auto EnsuresAfterSecond = UCk_Utils_Ensure_UE::Get_EnsureCount();

            TestEqual(TEXT("the first bridge's request ensured once"), EnsuresAfterFirst - EnsuresBefore, 1);
            TestEqual(TEXT("the second bridge's request ensured once"), EnsuresAfterSecond - EnsuresAfterFirst, 1);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterRefusal));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass, ScriptClass]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* Subject = Cast<ACk_AutoTest_EntityBridgeSubject_TwoBridges>(
                ACk_AutoTest_EntityBridgeSubject::Find(Server, SubjectClass));

            if (ck::Is_NOT_Valid(Subject))
            {
                AddError(TEXT("the subject is missing"));
                return false;
            }

            TestTrue(TEXT("the first bridge stayed NotRequested"),
                Subject->Get_EntityBridge()->Get_State() == ECk_EntityBridge_State::NotRequested);
            TestTrue(TEXT("the second bridge stayed NotRequested"),
                Subject->Get_SecondEntityBridge()->Get_State() == ECk_EntityBridge_State::NotRequested);
            TestEqual(TEXT("no entity is linked to the subject"),
                ck::auto_test::entity_bridge::Get_LinkedEntityCount(Server, Subject), 0);
            TestEqual(TEXT("no entity runs the script"),
                ck::auto_test::entity_bridge::Get_ScriptEntityCount(Server, ScriptClass), 0);

            return true;
        }),
        TEXT("an Actor with two bridges gets no entity from either")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
