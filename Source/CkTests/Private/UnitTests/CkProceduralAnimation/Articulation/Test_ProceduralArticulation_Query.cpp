#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Gait/CkProceduralGaitParticipation_Control.h"
#include "CkProceduralAnimation/Limb/CkProceduralLimb_Utils.h"
#include "CkProceduralAnimation/LimbControl/CkProceduralLimbControl_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "../Action/CkTest_ProceduralActionClipFixture.h"
#include "../CkTest_ProceduralWalkerFixture.h"
#include "../../CkTest_CompletionListener.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_articulation_query
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 20.0;
    const auto ArmId = FName{TEXT("QueryArm")};
    const auto OtherArmId = FName{TEXT("QueryOtherArm")};
    const auto RejectionEnsure = FString{TEXT("ProceduralArticulation RecoverUnplantedLimbs rejected articulation")};
    // Nothing consumes the clips' streams, so each needs room for every frame it plays and returns.
    constexpr auto HeldRecords = 512;

    struct FArmState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_Transform BareRoot;
        FCk_Handle_ProceduralArticulation BareSubject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> Listener;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> RejectedListener;
        bool ObservedReturning = false;
        bool ReturningMatchedObservation = false;
        bool ControlledWhileReturning = true;
    };

    struct FWalkerState
    {
        ck_test_procedural_walker_fixture::FFloor Floor;
        ck_test_procedural_walker_fixture::FWalker Walker;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> FirstListener;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> SecondListener;
    };

    auto
        AddArmSubject(
            UWorld* InWorld,
            const TArray<FName>& InArmIds,
            FCk_Handle_Transform& OutRoot)
        -> FCk_Handle_ProceduralArticulation
    {
        auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        OutRoot = UCk_Utils_Transform_UE::Add(RootEntity, FTransform{FVector{100.0, 200.0, 2000.0}},
            ECk_Replication::DoesNotReplicate);
        auto Layouts = TArray<FCk_ProceduralLimb_Layout>{};
        auto Bindings = TArray<FCk_ProceduralLimb_Binding>{};
        for (auto Index = 0; Index < InArmIds.Num(); ++Index)
        {
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(OutRoot);
            const auto Segment = UCk_Utils_Transform_UE::Add(SegmentEntity, FTransform::Identity,
                ECk_Replication::DoesNotReplicate);
            Layouts.Emplace(InArmIds[Index], FTransform{FVector{0.0, 100.0 * Index, 0.0}},
                TArray<float>{100.0f}, FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector);
            Bindings.Emplace(InArmIds[Index], TArray<FCk_Handle_Transform>{Segment});
        }
        return UCk_Utils_ProceduralArticulation_UE::Add(OutRoot,
            FCk_ProceduralArticulation_Spec{FCk_ProceduralArticulation_Layout{Layouts}, Bindings});
    }

    auto
        Get_UnplantedFromObservations(
            const FCk_Handle_ProceduralArticulation& InSubject)
        -> TArray<FName>
    {
        auto Ids = TArray<FName>{};
        for (const auto& Limb : UCk_Utils_ProceduralArticulation_UE::Get_Limbs(InSubject))
        {
            auto Layout = FCk_ProceduralLimb_Layout{};
            if (UCk_Utils_ProceduralLimb_UE::Try_GetLayout(Limb, Layout)
                && UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(InSubject, Layout.Get_Id()).Get_State()
                    == ECk_ProceduralLimbControl_State::Unplanted)
            { Ids.Add(Layout.Get_Id()); }
        }
        return Ids;
    }

    auto
        Get_ControllerLimbSets(
            const FCk_Handle_ProceduralArticulation& InSubject)
        -> TArray<TArray<FName>>
    {
        auto Sets = TArray<TArray<FName>>{};
        if (NOT InSubject.Has<ck::FFragment_ProceduralLimbControl>())
        { return Sets; }
        for (const auto& Controller : InSubject.Get<ck::FFragment_ProceduralLimbControl>().Get_Controllers())
        {
            if (Controller.Get_ReturnState() == ECk_ProceduralLimbControl_ReturnState::Returning)
            { Sets.Add(Controller.Get_Channels().Get_LimbIds()); }
        }
        return Sets;
    }

    // Borrows the two diagonal legs Leg0 and Leg3 together for a short clip and removes the floor while it plays, so
    // both returns fail without ground and leave the limbs unplanted.
    auto
        AddUnplantedPairCommands(
            FAutomationTestBase* InTest,
            const TSharedRef<FWalkerState>& InState)
        -> void
    {
        using namespace ck_test_procedural_walker_fixture;
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [InState](UWorld* InWorld) { InState->Floor = AddFloor(InWorld); })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([InState] { return Get_IsFloorReady(InState->Floor); }),
            WaitSeconds, TEXT("Walker floor is present in Jolt")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [InState](UWorld* InWorld) { InState->Walker = AddWalker(InWorld, TArray<FName>{LegId(0), LegId(3)}); })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([InState] { return Get_IsWalkerSettled(InState->Walker); }),
            WaitSeconds, TEXT("Walker settles over four planted feet")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [InState](UWorld*)
            {
                auto Support = FCk_ProceduralLimbControl_Support{ECk_ProceduralLimbControl_SupportMode::CountOnly, 2};
                Support.Set_SupportHoldTimeout(FCk_Time{0.5});
                auto Return = FCk_ProceduralLimbControl_Return{};
                Return.Set_Duration(FCk_Time{0.2});
                Return.Set_Timeout(FCk_Time{0.8});
                Return.Set_MaxProbeAttempts(2);
                auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
                Locomotion.Set_Support(Support);
                Locomotion.Set_Return(Return);
                auto Spec = FCk_ProceduralActionPlayer_Spec{};
                Spec.Set_Locomotion(Locomotion);
                auto Capacity = Spec.Get_Capacity();
                Capacity.Set_RecordsPerInstance(HeldRecords);
                Spec.Set_Capacity(Capacity);
                InState->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(InState->Walker.Subject, Spec);
                InState->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InState->Walker.Root);
                const auto Legs = TArray<FName>{LegId(0), LegId(3)};
                InState->Clip = ck_test_procedural_action_clip_fixture::MakeNodClip(Legs, FCk_Time{1.0});
                InState->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(InState->Scope, InState->Player,
                    ck_test_procedural_action_clip_fixture::MakePlay(InState->Clip.Get(), Legs));
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([InState]
            {
                return ck::IsValid(InState->Action)
                    && UCk_Utils_ProceduralAction_UE::Get_State(InState->Action) == ECk_ProceduralAction_State::Playing;
            }), WaitSeconds, TEXT("Both diagonal walking limbs are borrowed")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [InState](UWorld*) { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(InState->Floor.Entity); })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([InState] { return NOT ck::IsValid(InState->Floor.Body); }),
            WaitSeconds, TEXT("Jolt floor removal completes before return")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([InState]
            {
                return UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(InState->Walker.Subject, LegId(0))
                        .Get_State() == ECk_ProceduralLimbControl_State::Unplanted
                    && UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(InState->Walker.Subject, LegId(3))
                        .Get_State() == ECk_ProceduralLimbControl_State::Unplanted;
            }), WaitSeconds, TEXT("No-floor return leaves both borrowed limbs unplanted")));
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationQueryControlledReturningTest,
    "Ck.ProceduralAnimation.Articulation.Query.ControlledAndReturningMatchObservations",
    ck_test_procedural_articulation_query::TestFlags)

auto
    FCkProceduralArticulationQueryControlledReturningTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_articulation_query;
    const auto State = MakeShared<FArmState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Subject = AddArmSubject(InWorld, TArray<FName>{ArmId, OtherArmId}, State->Root);
            TestTrue(TEXT("Two-arm subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Two-arm subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestFalse(TEXT("No player means no controlled limb"),
                UCk_Utils_ProceduralArticulation_UE::Get_IsAnyLimbControlled(State->Subject));
            auto Tuning = FCk_ProceduralAction_Tuning{};
            auto Transition = Tuning.Get_Transition();
            Transition.Set_DedicatedReturn(FCk_Time{0.5});
            Transition.Set_ReturnTimeout(FCk_Time{3.0});
            Tuning.Set_Transition(Transition);
            auto Spec = FCk_ProceduralActionPlayer_Spec{};
            Spec.Set_Tuning(Tuning);
            auto Capacity = Spec.Get_Capacity();
            Capacity.Set_RecordsPerInstance(HeldRecords);
            Spec.Set_Capacity(Capacity);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject, Spec);
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Clip = ck_test_procedural_action_clip_fixture::MakeNodClip(TArray<FName>{ArmId}, FCk_Time{1.0});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Scope, State->Player,
                ck_test_procedural_action_clip_fixture::MakePlay(State->Clip.Get(), TArray<FName>{ArmId}));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Playing;
        }), WaitSeconds, TEXT("The clip holds one arm")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("The held arm observes Controlled"),
                UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, ArmId).Get_State(),
                ECk_ProceduralLimbControl_State::Controlled);
            TestTrue(TEXT("Controlled query matches the held arm"),
                UCk_Utils_ProceduralArticulation_UE::Get_IsAnyLimbControlled(State->Subject));
            TestFalse(TEXT("Nothing returns while the clip plays"),
                UCk_Utils_ProceduralArticulation_UE::Get_IsAnyLimbReturning(State->Subject));
            TestTrue(TEXT("Dedicated arms are never unplanted"),
                UCk_Utils_ProceduralArticulation_UE::Get_UnplantedLimbIds(State->Subject).IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            if (UCk_Utils_ProceduralArticulation_UE::Get_IsAnyLimbReturning(State->Subject))
            {
                State->ObservedReturning = true;
                State->ReturningMatchedObservation =
                    UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, ArmId).Get_State()
                        == ECk_ProceduralLimbControl_State::Returning;
                State->ControlledWhileReturning =
                    UCk_Utils_ProceduralArticulation_UE::Get_IsAnyLimbControlled(State->Subject);
            }
            return State->ObservedReturning;
        }), WaitSeconds, TEXT("The clip's end starts the arm's return")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Finished;
        }), WaitSeconds, TEXT("Return completes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestTrue(TEXT("Returning query matched the arm's Returning observation"), State->ReturningMatchedObservation);
            TestFalse(TEXT("A returning arm is not reported as controlled"), State->ControlledWhileReturning);
            TestFalse(TEXT("No limb is controlled after the return"),
                UCk_Utils_ProceduralArticulation_UE::Get_IsAnyLimbControlled(State->Subject));
            TestFalse(TEXT("No limb is returning after the return"),
                UCk_Utils_ProceduralArticulation_UE::Get_IsAnyLimbReturning(State->Subject));
            TestEqual(TEXT("The arm is back to Default"),
                UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, ArmId).Get_State(),
                ECk_ProceduralLimbControl_State::Default);
            TestFalse(TEXT("An invalid handle is neither controlled nor returning"),
                UCk_Utils_ProceduralArticulation_UE::Get_IsAnyLimbControlled({})
                    || UCk_Utils_ProceduralArticulation_UE::Get_IsAnyLimbReturning({}));
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationQueryUnplantedOrderTest,
    "Ck.ProceduralAnimation.Articulation.Query.UnplantedIdsInTopologyOrder",
    ck_test_procedural_articulation_query::TestFlags)

auto
    FCkProceduralArticulationQueryUnplantedOrderTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_articulation_query;
    using namespace ck_test_procedural_walker_fixture;
    const auto State = MakeShared<FWalkerState>();
    AddUnplantedPairCommands(this, State);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Expected = Get_UnplantedFromObservations(State->Walker.Subject);
            const auto Unplanted = UCk_Utils_ProceduralArticulation_UE::Get_UnplantedLimbIds(State->Walker.Subject);
            TestEqual(TEXT("Both failed returns are unplanted"), Unplanted.Num(), 2);
            TestTrue(TEXT("Unplanted IDs follow Get_Limbs order filtered by observation"), Unplanted == Expected);
            TestFalse(TEXT("Unplanted limbs are not controlled"),
                UCk_Utils_ProceduralArticulation_UE::Get_IsAnyLimbControlled(State->Walker.Subject));
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Walker.Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationQueryRecoverEmptyTest,
    "Ck.ProceduralAnimation.Articulation.Query.RecoverUnplantedEmptySetSucceeds",
    ck_test_procedural_articulation_query::TestFlags)

auto
    FCkProceduralArticulationQueryRecoverEmptyTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_articulation_query;
    const auto State = MakeShared<FArmState>();
    // The ensure logger and automation reporter both emit the diagnostic; assert the synchronous result instead.
    AddExpectedError(RejectionEnsure, EAutomationExpectedErrorFlags::Contains, 0);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld)
        {
            State->Subject = AddArmSubject(InWorld, TArray<FName>{ArmId}, State->Root);
            State->BareSubject = AddArmSubject(InWorld, TArray<FName>{ArmId}, State->BareRoot);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->BareSubject) > 0;
        }), WaitSeconds, TEXT("Both subjects settle")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject, FCk_ProceduralActionPlayer_Spec{});
            State->Listener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{
                NewObject<UCk_Test_CompletionListener_UE>()};
            auto Completion = FCk_Delegate_Request_OnCompleted{};
            Completion.BindDynamic(State->Listener.Get(), &UCk_Test_CompletionListener_UE::OnRequestCompleted);
            UCk_Utils_ProceduralArticulation_UE::Request_RecoverUnplantedLimbs(State->Subject,
                FCk_Request_ProceduralArticulation_RecoverUnplantedLimbs{}, Completion);
            TestEqual(TEXT("A valid request completes during the drain, not at enqueue"),
                State->Listener->_TimesRequestCompleted, 0);

            State->RejectedListener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{
                NewObject<UCk_Test_CompletionListener_UE>()};
            auto Rejected = FCk_Delegate_Request_OnCompleted{};
            Rejected.BindDynamic(State->RejectedListener.Get(), &UCk_Test_CompletionListener_UE::OnRequestCompleted);
            UCk_Utils_ProceduralArticulation_UE::Request_RecoverUnplantedLimbs(State->BareSubject,
                FCk_Request_ProceduralArticulation_RecoverUnplantedLimbs{}, Rejected);
            TestEqual(TEXT("A subject without limb control rejects synchronously once"),
                State->RejectedListener->_TimesRequestCompleted, 1);
            TestEqual(TEXT("The rejection is reported as not enqueued"),
                State->RejectedListener->_LastRequestResult, ECk_Request_OperationResult::Failed_NotEnqueued);
            TestFalse(TEXT("The rejection leaves no request queue behind"),
                State->BareSubject.Has<ck::FFragment_ProceduralLimbControl_Requests>());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return State->Listener->_TimesRequestCompleted > 0; }),
        WaitSeconds, TEXT("Empty recovery request drains")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("Empty recovery completes once"), State->Listener->_TimesRequestCompleted, 1);
            TestEqual(TEXT("Empty recovery succeeds"), State->Listener->_LastRequestResult,
                ECk_Request_OperationResult::Succeeded);
            TestTrue(TEXT("Empty recovery takes no claim"), Get_ControllerLimbSets(State->Subject).IsEmpty()
                && State->Subject.Get<ck::FFragment_ProceduralLimbControl>().Get_Controllers().IsEmpty());
            TestEqual(TEXT("The arm remains Default"),
                UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, ArmId).Get_State(),
                ECk_ProceduralLimbControl_State::Default);
            TestEqual(TEXT("The rejected request never completes again"),
                State->RejectedListener->_TimesRequestCompleted, 1);
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
            auto BareRoot = State->BareRoot.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(BareRoot);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationQueryRecoverDrainTimeTest,
    "Ck.ProceduralAnimation.Articulation.Query.RecoverUnplantedUsesDrainTimeSet",
    ck_test_procedural_articulation_query::TestFlags)

auto
    FCkProceduralArticulationQueryRecoverDrainTimeTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_articulation_query;
    using namespace ck_test_procedural_walker_fixture;
    const auto State = MakeShared<FWalkerState>();
    AddUnplantedPairCommands(this, State);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { State->Floor = AddFloor(InWorld); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return Get_IsFloorReady(State->Floor)
                && FCk_ProceduralGaitParticipation_Control::GetLimbObservation(
                    State->Walker.Gait, LegId(1)).Get_TrustedSupport()
                && FCk_ProceduralGaitParticipation_Control::GetLimbObservation(
                    State->Walker.Gait, LegId(2)).Get_TrustedSupport();
        }), WaitSeconds, TEXT("The two remaining feet trust the restored floor")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("Both limbs are unplanted at enqueue"),
                UCk_Utils_ProceduralArticulation_UE::Get_UnplantedLimbIds(State->Walker.Subject).Num(), 2);
            State->FirstListener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{
                NewObject<UCk_Test_CompletionListener_UE>()};
            State->SecondListener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{
                NewObject<UCk_Test_CompletionListener_UE>()};
            auto First = FCk_Delegate_Request_OnCompleted{};
            First.BindDynamic(State->FirstListener.Get(), &UCk_Test_CompletionListener_UE::OnRequestCompleted);
            auto Second = FCk_Delegate_Request_OnCompleted{};
            Second.BindDynamic(State->SecondListener.Get(), &UCk_Test_CompletionListener_UE::OnRequestCompleted);
            // Same queue, drained in order: the first request takes the whole unplanted set, so a set captured at
            // enqueue ({Leg0, Leg3}) would collide with that claim, while the second's drain-time set is empty.
            UCk_Utils_ProceduralArticulation_UE::Request_RecoverUnplantedLimbs(State->Walker.Subject,
                FCk_Request_ProceduralArticulation_RecoverUnplantedLimbs{}, First);
            UCk_Utils_ProceduralArticulation_UE::Request_RecoverUnplantedLimbs(State->Walker.Subject,
                FCk_Request_ProceduralArticulation_RecoverUnplantedLimbs{}, Second);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return State->FirstListener->_TimesRequestCompleted > 0
                && State->SecondListener->_TimesRequestCompleted > 0;
        }), WaitSeconds, TEXT("Both recovery requests drain")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("First recovery completes once"), State->FirstListener->_TimesRequestCompleted, 1);
            TestEqual(TEXT("Second recovery completes once"), State->SecondListener->_TimesRequestCompleted, 1);
            const auto Sets = Get_ControllerLimbSets(State->Walker.Subject);
            const auto ClaimsOf = [&Sets](FName InLimb)
            {
                return Sets.FilterByPredicate([InLimb](const TArray<FName>& InSet) { return InSet.Contains(InLimb); }).Num();
            };
            TestTrue(TEXT("No limb is claimed by two recoveries"), ClaimsOf(LegId(0)) <= 1 && ClaimsOf(LegId(3)) <= 1);
            if (State->FirstListener->_LastRequestResult == ECk_Request_OperationResult::Succeeded)
            {
                TestTrue(TEXT("The first drain-time set recovers both unplanted limbs as one set"),
                    Sets.ContainsByPredicate([](const TArray<FName>& InSet)
                    { return InSet.Num() == 2 && InSet.Contains(LegId(0)) && InSet.Contains(LegId(3)); }));
                TestEqual(TEXT("The second drain-time set is empty and succeeds without a claim"),
                    State->SecondListener->_LastRequestResult, ECk_Request_OperationResult::Succeeded);
                TestEqual(TEXT("Only one recovery holds a claim"), Sets.Num(), 1);
            }
            else
            {
                TestEqual(TEXT("A failed recovery reports Failed"), State->FirstListener->_LastRequestResult,
                    ECk_Request_OperationResult::Failed);
                TestEqual(TEXT("A failed recovery leaves Leg3 unplanted"),
                    UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Walker.Subject, LegId(3)).Get_State(),
                    ECk_ProceduralLimbControl_State::Unplanted);
            }
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Walker.Root);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor.Entity);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
