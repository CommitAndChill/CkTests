#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Ensure/CkEnsure_Utils.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Processor/CkProcessor.h"
#include "CkEcs/Scheduler/CkProcessorGroups.h"
#include "CkEcs/Scheduler/CkProcessorRegistration.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Fragment.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Control.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Fragment.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Processor.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "CkTest_ProceduralActionClipFixture.h"
#include "CkTest_ProceduralActionReentry.h"
#include "../CkTest_ProceduralWalkerFixture.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck
{
    struct FFragment_TestProceduralActionLifecycle_Probe
    {
        CK_GENERATED_BODY(FFragment_TestProceduralActionLifecycle_Probe);

        struct FSample
        {
            ECk_ProceduralArticulation_Health Health = ECk_ProceduralArticulation_Health::Operational;
            ECk_ProceduralArticulation_Fault Fault = ECk_ProceduralArticulation_Fault::None;
            int64 SettledRevision = 0;
            uint64 ClaimantInstance = 0;
            ECk_ProceduralAction_State ActionState = ECk_ProceduralAction_State::PendingLoad;
        };

        FCk_Handle_Transform WriteTarget;
        bool WriteArmed = false;
        bool Written = false;
        bool Recording = false;
        FName ClaimLimb;
        FCk_Handle_ProceduralAction Action;
        TArray<FSample> Samples;
    };

    // Moves one bound part away from the pose the articulation just issued, so this frame's settlement disagrees.
    class FProcessor_TestProceduralActionLifecycle_Write : public ck_exp::TProcessor<
        FProcessor_TestProceduralActionLifecycle_Write,
        FCk_Handle_ProceduralArticulation,
        TReadWrite<FFragment_TestProceduralActionLifecycle_Probe>>
    {
    public:
        using Group = FGroup_Transform_Derived;
        using RunAfter = TDepList<FProcessor_ProceduralArticulation_Update>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType,
            HandleType InSubject,
            FFragment_TestProceduralActionLifecycle_Probe& InProbe)
            -> void
        {
            if (NOT InProbe.WriteArmed || InProbe.Written || NOT ck::IsValid(InProbe.WriteTarget))
            { return; }
            const auto& Pose = InSubject.Get<FFragment_ProceduralArticulation_Pose>();
            if (NOT Pose.Get_PendingObservation())
            { return; }
            auto Moved = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(InProbe.WriteTarget);
            Moved.AddToTranslation(FVector{0.0, 0.0, 25.0});
            UCk_Utils_Transform_UE::Request_SetTransform(InProbe.WriteTarget,
                FCk_Request_Transform_SetTransform{Moved}, {});
            InProbe.Written = true;
            InProbe.Recording = true;
        }
    };

    // Records the articulation once per frame, after its settlement has been observed.
    class FProcessor_TestProceduralActionLifecycle_Record : public ck_exp::TProcessor<
        FProcessor_TestProceduralActionLifecycle_Record,
        FCk_Handle_ProceduralArticulation,
        TReadWrite<FFragment_TestProceduralActionLifecycle_Probe>>
    {
    public:
        using Group = FGroup_Gameplay_Camera;
        using RunAfter = TDepList<FProcessor_ProceduralArticulation_Observe>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType,
            HandleType InSubject,
            FFragment_TestProceduralActionLifecycle_Probe& InProbe)
            -> void
        {
            if (NOT InProbe.Recording)
            { return; }
            auto Sample = FFragment_TestProceduralActionLifecycle_Probe::FSample{};
            Sample.Health = UCk_Utils_ProceduralArticulation_UE::Get_Health(InSubject);
            Sample.Fault = UCk_Utils_ProceduralArticulation_UE::Get_Fault(InSubject);
            Sample.SettledRevision = UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(InSubject);
            if (NOT InProbe.ClaimLimb.IsNone())
            {
                Sample.ClaimantInstance = FCk_ProceduralArticulation_Control::GetClaimant(
                    InSubject, InProbe.ClaimLimb, false).Get_InstanceId();
            }
            Sample.ActionState = UCk_Utils_ProceduralAction_UE::Get_State(InProbe.Action);
            InProbe.Samples.Add(Sample);
        }
    };
}

CK_REGISTER_PROCESSOR(ck::FProcessor_TestProceduralActionLifecycle_Write);
CK_REGISTER_PROCESSOR(ck::FProcessor_TestProceduralActionLifecycle_Record);

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_action_lifecycle
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 20.0;
    // Nothing consumes the held streams, so each needs room for every frame it holds and returns.
    constexpr auto HeldRecords = 4096;
    const auto MissingLimbId = FName{TEXT("NoSuchLimb")};

    using FProbe = ck::FFragment_TestProceduralActionLifecycle_Probe;

    struct FState
    {
        ck_test_procedural_walker_fixture::FFloor Floor;
        ck_test_procedural_walker_fixture::FWalker Walker;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        FCk_Handle_ProceduralLeg Leg;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE> Listener;
        int64 RevisionAtDetach = 0;
        int64 RevisionAtClear = 0;
        int32 SamplesAtDestroy = 0;
        int32 SamplesAtClear = 0;
        int32 CreatedBeforeRejection = 0;
        int32 EnsuresOnRejection = INDEX_NONE;
    };

    auto
        MakePlayerSpec(
            int32 InSupportCount,
            FCk_Time InReturnTimeout)
        -> FCk_ProceduralActionPlayer_Spec
    {
        auto Support = FCk_ProceduralLimbControl_Support{
            ECk_ProceduralLimbControl_SupportMode::CountOnly, InSupportCount};
        auto Return = FCk_ProceduralLimbControl_Return{};
        Return.Set_Duration(FMath::Min(FCk_Time{0.2}, InReturnTimeout));
        Return.Set_Timeout(InReturnTimeout);
        auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
        Locomotion.Set_Support(Support);
        Locomotion.Set_Return(Return);
        auto Spec = FCk_ProceduralActionPlayer_Spec{};
        Spec.Set_Locomotion(Locomotion);
        auto Capacity = Spec.Get_Capacity();
        Capacity.Set_RecordsPerInstance(HeldRecords);
        Spec.Set_Capacity(Capacity);
        return Spec;
    }

    // Lifts the bound leg's foot slightly off its rest pose for InDuration.
    auto
        DoPlayLift(
            FState& InOutState,
            FCk_Time InDuration)
        -> FCk_Handle_ProceduralAction
    {
        const auto LegId = ck_test_procedural_walker_fixture::LegId(0);
        InOutState.Clip = ck_test_procedural_action_clip_fixture::MakeHoldClip(LegId,
            ECk_ProceduralAction_TrackSpace::LimbRestNormalized, FVector{0.0, 0.0, 0.005}, InDuration);
        return UCk_Utils_ProceduralAction_UE::Create_FromClip(InOutState.Scope, InOutState.Player,
            ck_test_procedural_action_clip_fixture::MakePlay(InOutState.Clip.Get(), TArray<FName>{LegId}));
    }

    auto
        Get_TerminalRecordCount(
            const FCk_Handle_ProceduralActionPlayer& InPlayer,
            FCk_ProceduralAction_Identity InIdentity)
        -> int32
    {
        const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(InPlayer, InIdentity, 0, HeldRecords + 2);
        return Page.Get_Records().FilterByPredicate([](const FCk_ProceduralAction_Record& InRecord)
        { return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal; }).Num();
    }

    auto
        Get_Probe(
            const FState& InState)
        -> const FProbe&
    {
        return InState.Walker.Subject.Get<FProbe>();
    }

    auto
        DoBindListener(
            FState& InState)
        -> void
    {
        InState.Listener = TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE>{
            NewObject<UCk_Test_ProceduralActionReentry_UE>()};
    }

    auto
        MakeCompletion(
            const FState& InState)
        -> FCk_Delegate_Request_OnCompleted
    {
        auto Completion = FCk_Delegate_Request_OnCompleted{};
        Completion.BindDynamic(InState.Listener.Get(), &UCk_Test_ProceduralActionReentry_UE::OnRequestCompleted);
        return Completion;
    }

    // Adds the floor and a walker whose InBoundLegs are its articulation limbs, then waits for four planted feet.
    auto
        AddSettledWalker(
            FAutomationTestBase* InTest,
            const TSharedRef<FState>& InState,
            const TArray<FName>& InBoundLegs)
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
            [InState, InBoundLegs](UWorld* InWorld)
            {
                InState->Walker = AddWalker(InWorld, InBoundLegs);
                InState->Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(InState->Walker.Root, LegId(0));
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([InState] { return Get_IsWalkerSettled(InState->Walker); }),
            WaitSeconds, TEXT("Walker settles over four planted feet")));
    }

    auto
        DoCleanup(
            const TSharedRef<FState>& InState)
        -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [InState](UWorld*)
            {
                for (auto Entity : TArray<FCk_Handle>{InState->Scope, InState->Walker.Root, InState->Floor.Entity})
                {
                    if (ck::IsValid(Entity))
                    { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Entity); }
                }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionLifecycleDetachedClaimedLegTest,
    "Ck.ProceduralAnimation.Action.Lifecycle.DetachedClaimedLegReleasesItsClaim",
    ck_test_procedural_action_lifecycle::TestFlags)

auto
    FCkProceduralActionLifecycleDetachedClaimedLegTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_lifecycle;
    using namespace ck_test_procedural_walker_fixture;
    const auto State = MakeShared<FState>();
    constexpr auto FramesBeforeAnyReturn = 5;
    constexpr auto FramesToFinish = 60;
    constexpr auto RevisionsAfterDetach = 3;

    // Leg1 stays rigged, so the articulation still has a chain to publish once Leg0 is gone.
    AddSettledWalker(this, State, TArray<FName>{LegId(0), LegId(1)});
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Walker.Subject,
                MakePlayerSpec(2, FCk_Time{0.25}));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Walker.Root);
            State->Action = DoPlayLift(*State, FCk_Time{3.0});
            TestTrue(TEXT("The lift admits the one bound leg"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Playing
                && UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action).Get_HasAcceptedPose();
        }), WaitSeconds, TEXT("The lift is playing with an accepted pose")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("The lift holds Leg0's claim before the sever"),
                FCk_ProceduralArticulation_Control::GetClaimant(State->Walker.Subject, LegId(0), false)
                    .Get_InstanceId(),
                static_cast<uint64>(UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action).Get_Value()));
            auto& Probe = State->Walker.Subject.AddOrGet<FProbe>();
            Probe.ClaimLimb = LegId(0);
            Probe.Action = State->Action;
            Probe.Recording = true;
            State->RevisionAtDetach = UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Walker.Subject);
            auto Detach = FCk_Request_ProceduralLeg_Detach{ECk_ProceduralLeg_ReleasedPartsOwnership::TransferToLeg};
            Detach.Set_LegOwnership(ECk_ProceduralLeg_DetachedLegOwnership::TransferToWorld);
            UCk_Utils_ProceduralLeg_UE::Request_Detach(State->Leg, Detach, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return Get_Probe(*State).Samples.Num() >= FramesBeforeAnyReturn; }),
        WaitSeconds, TEXT("The sever is observed for five frames")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Samples = Get_Probe(*State).Samples;
            for (auto Index = 0; Index < FramesBeforeAnyReturn; ++Index)
            {
                TestEqual(*FString::Printf(TEXT("A severed claimed leg leaves the rig operational (frame %d, fault %s)"),
                        Index, *StaticEnum<ECk_ProceduralArticulation_Fault>()->GetNameStringByValue(
                            static_cast<int64>(Samples[Index].Fault))),
                    Samples[Index].Health, ECk_ProceduralArticulation_Health::Operational);
            }
            State->SamplesAtDestroy = Samples.Num();
            auto Leg = State->Leg.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Leg);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            const auto& Samples = Get_Probe(*State).Samples;
            if (Samples.Num() - State->SamplesAtDestroy >= FramesToFinish)
            { return true; }
            const auto& Last = Samples.Last();
            return Last.ActionState == ECk_ProceduralAction_State::Finished && Last.ClaimantInstance == 0
                && Last.SettledRevision >= State->RevisionAtDetach + RevisionsAfterDetach;
        }), WaitSeconds, TEXT("The action ends or sixty frames pass after the leg is destroyed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Last = Get_Probe(*State).Samples.Last();
            TestEqual(TEXT("A destroyed claimed leg leaves the rig operational"),
                Last.Health, ECk_ProceduralArticulation_Health::Operational);
            TestEqual(TEXT("The dead leg's claim is released"), Last.ClaimantInstance, uint64{0});
            TestEqual(TEXT("The action ends within sixty frames"),
                Last.ActionState, ECk_ProceduralAction_State::Finished);
            TestTrue(TEXT("The rig keeps publishing after the sever"),
                Last.SettledRevision >= State->RevisionAtDetach + RevisionsAfterDetach);
        })));
    DoCleanup(State);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionLifecycleTransientFaultTest,
    "Ck.ProceduralAnimation.Action.Lifecycle.TransientFaultRearms",
    ck_test_procedural_action_lifecycle::TestFlags)

auto
    FCkProceduralActionLifecycleTransientFaultTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_lifecycle;
    const auto State = MakeShared<FState>();
    constexpr auto FramesToRearm = 5;

    AddSettledWalker(this, State, TArray<FName>{ck_test_procedural_walker_fixture::LegId(0)});
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Bindings = State->Walker.Subject.Get<ck::FFragment_ProceduralArticulation_Bindings>()
                .Get_Bindings();
            if (NOT TestTrue(TEXT("The walker binds a segmented limb"),
                Bindings.IsValidIndex(0) && Bindings[0].Get_Segments().IsValidIndex(0)))
            { return; }
            auto& Probe = State->Walker.Subject.AddOrGet<FProbe>();
            Probe.WriteTarget = Bindings[0].Get_Segments()[0];
            Probe.WriteArmed = true;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return Get_Probe(*State).Samples.Num() > FramesToRearm; }),
        WaitSeconds, TEXT("The foreign write is observed for six frames")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Samples = Get_Probe(*State).Samples;
            const auto Mismatches = Samples.FilterByPredicate([](const FProbe::FSample& InSample)
            {
                return InSample.Health == ECk_ProceduralArticulation_Health::Faulted
                    && InSample.Fault == ECk_ProceduralArticulation_Fault::SettlementMismatch;
            }).Num();
            TestEqual(TEXT("The foreign write reports one settlement mismatch"), Mismatches, 1);
            TestEqual(TEXT("The mismatch is reported on the frame of the write"),
                Samples[0].Fault, ECk_ProceduralArticulation_Fault::SettlementMismatch);
            TestEqual(TEXT("The rig is operational again within five frames"),
                Samples[FramesToRearm].Health, ECk_ProceduralArticulation_Health::Operational);
        })));
    DoCleanup(State);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionLifecycleClearFaultTest,
    "Ck.ProceduralAnimation.Action.Lifecycle.ClearFaultRequestRecovers",
    ck_test_procedural_action_lifecycle::TestFlags)

auto
    FCkProceduralActionLifecycleClearFaultTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_lifecycle;
    const auto State = MakeShared<FState>();
    constexpr auto PublishesAfterClear = 3;

    AddExpectedError(TEXT("lost an authored part; rig has failed without partially posing its chain."),
        EAutomationExpectedErrorFlags::Contains, 0);
    // Leg1 stays rigged, so the articulation still has a chain to publish once Leg0 is gone.
    AddSettledWalker(this, State, TArray<FName>{ck_test_procedural_walker_fixture::LegId(0),
        ck_test_procedural_walker_fixture::LegId(1)});
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Bindings = State->Walker.Subject.Get<ck::FFragment_ProceduralArticulation_Bindings>()
                .Get_Bindings();
            if (NOT TestTrue(TEXT("The walker binds a segmented limb"),
                Bindings.IsValidIndex(0) && Bindings[0].Get_Segments().IsValidIndex(0)))
            { return; }
            auto Segment = Bindings[0].Get_Segments()[0].ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Segment);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralArticulation_UE::Get_Health(State->Walker.Subject)
                == ECk_ProceduralArticulation_Health::Faulted;
        }), WaitSeconds, TEXT("A destroyed bound segment faults the rig")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("A destroyed bound segment is a structural fault"),
                UCk_Utils_ProceduralArticulation_UE::Get_Fault(State->Walker.Subject),
                ECk_ProceduralArticulation_Fault::InvalidBinding);
            // The fault's cause goes with the limb that owned the segment, so its binding is dead from here on.
            auto Leg = State->Leg.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Leg);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return ck::Is_NOT_Valid(State->Leg); }),
        WaitSeconds, TEXT("The faulted limb is destroyed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("A structural fault stays until it is cleared"),
                UCk_Utils_ProceduralArticulation_UE::Get_Health(State->Walker.Subject),
                ECk_ProceduralArticulation_Health::Faulted);
            DoBindListener(*State);
            auto& Probe = State->Walker.Subject.AddOrGet<FProbe>();
            Probe.Recording = true;
            State->RevisionAtClear = UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Walker.Subject);
            UCk_Utils_ProceduralArticulation_UE::Request_ClearFault(State->Walker.Subject,
                FCk_Request_ProceduralArticulation_ClearFault{}, MakeCompletion(*State));
            TestEqual(TEXT("ClearFault completes once"), State->Listener->CompletionResults.Num(), 1);
            TestTrue(TEXT("ClearFault completes Succeeded"), State->Listener->CompletionResults.Num() == 1
                && State->Listener->CompletionResults[0] == ECk_Request_OperationResult::Succeeded);
            TestEqual(TEXT("ClearFault leaves the rig operational"),
                UCk_Utils_ProceduralArticulation_UE::Get_Health(State->Walker.Subject),
                ECk_ProceduralArticulation_Health::Operational);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            const auto& Samples = Get_Probe(*State).Samples;
            return NOT Samples.IsEmpty() && (Samples.Last().SettledRevision >= State->RevisionAtClear + PublishesAfterClear
                || Samples.Last().Health == ECk_ProceduralArticulation_Health::Faulted);
        }), WaitSeconds, TEXT("The cleared rig publishes three times or faults again")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Samples = Get_Probe(*State).Samples;
            const auto Refaulted = Samples.ContainsByPredicate([](const FProbe::FSample& InSample)
            { return InSample.Health == ECk_ProceduralArticulation_Health::Faulted; });
            TestFalse(TEXT("The next publishes skip the dead binding and do not fault again"), Refaulted);
            TestTrue(TEXT("The cleared rig publishes again"),
                Samples.Last().SettledRevision >= State->RevisionAtClear + PublishesAfterClear);
        })));
    DoCleanup(State);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionLifecycleCapacityExhaustionTest,
    "Ck.ProceduralAnimation.Action.Lifecycle.CapacityExhaustionEnsures",
    ck_test_procedural_action_lifecycle::TestFlags)

auto
    FCkProceduralActionLifecycleCapacityExhaustionTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_lifecycle;
    const auto State = MakeShared<FState>();
    constexpr auto MaxAttempts = 8;
    AddExpectedError(TEXT("ProceduralAction Create rejected: player"), EAutomationExpectedErrorFlags::Contains, 0);
    AddSettledWalker(this, State, TArray<FName>{ck_test_procedural_walker_fixture::LegId(0)});
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Spec = MakePlayerSpec(3, FCk_Time{3.0});
            auto Capacity = Spec.Get_Capacity();
            Capacity.Set_MaxConcurrentInstances(1);
            Spec.Set_Capacity(Capacity);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Walker.Subject, Spec);
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Walker.Root);
            for (auto Attempt = 0; Attempt < MaxAttempts; ++Attempt)
            {
                const auto EnsuresBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();
                const auto Created = DoPlayLift(*State, FCk_Time{3.0});
                if (ck::IsValid(Created))
                {
                    ++State->CreatedBeforeRejection;
                    continue;
                }
                State->EnsuresOnRejection = UCk_Utils_Ensure_UE::Get_EnsureCount() - EnsuresBefore;
                break;
            }
            TestEqual(TEXT("One unacknowledged action fills a one-instance player"), State->CreatedBeforeRejection, 1);
            TestEqual(TEXT("The rejected Create fires exactly one ensure"), State->EnsuresOnRejection, 1);
        })));
    DoCleanup(State);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionLifecycleExplicitTerminalExpiryTest,
    "Ck.ProceduralAnimation.Action.Lifecycle.ExplicitTerminalExpiresUnacknowledged",
    ck_test_procedural_action_lifecycle::TestFlags)

auto
    FCkProceduralActionLifecycleExplicitTerminalExpiryTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_lifecycle;
    const auto State = MakeShared<FState>();
    // One concurrent instance retains at most four streams.
    constexpr auto RetainedStreams = 4;

    AddExpectedMessage(TEXT("expired unacknowledged"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, RetainedStreams);
    AddSettledWalker(this, State, TArray<FName>{ck_test_procedural_walker_fixture::LegId(0)});
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Spec = MakePlayerSpec(3, FCk_Time{3.0});
            auto Capacity = Spec.Get_Capacity();
            Capacity.Set_MaxConcurrentInstances(1);
            Spec.Set_Capacity(Capacity);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Walker.Subject, Spec);
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Walker.Root);
            // Bound to a limb the subject does not have, each action finishes Rejected as it is created and leaves a
            // terminal Explicit stream nobody acknowledges.
            State->Clip = ck_test_procedural_action_clip_fixture::MakeHoldClip(
                ck_test_procedural_walker_fixture::LegId(0), ECk_ProceduralAction_TrackSpace::LimbRestNormalized,
                FVector::ZeroVector, FCk_Time{0.2});
            for (auto Index = 0; Index < RetainedStreams; ++Index)
            {
                const auto Rejected = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Scope, State->Player,
                    ck_test_procedural_action_clip_fixture::MakePlay(State->Clip.Get(), TArray<FName>{MissingLimbId}));
                TestEqual(TEXT("The unbindable action finishes at once"),
                    UCk_Utils_ProceduralAction_UE::Get_State(Rejected), ECk_ProceduralAction_State::Finished);
            }
            TestEqual(TEXT("Every terminal stream is retained unacknowledged"),
                UCk_Utils_ProceduralActionPlayer_UE::Get_RetainedReceiptIdentities(State->Player).Num(),
                RetainedStreams);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return UCk_Utils_ProceduralActionPlayer_UE::Get_RetainedReceiptIdentities(State->Player).IsEmpty(); }),
        WaitSeconds, TEXT("Unacknowledged terminal streams expire")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            State->Action = DoPlayLift(*State, FCk_Time{0.2});
            TestTrue(TEXT("A new action is admitted once the expired streams are released"),
                ck::IsValid(State->Action));
        })));
    DoCleanup(State);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionLifecyclePlayerEndPlayTest,
    "Ck.ProceduralAnimation.Action.Lifecycle.PlayerEndPlayFinishesActions",
    ck_test_procedural_action_lifecycle::TestFlags)

auto
    FCkProceduralActionLifecyclePlayerEndPlayTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_lifecycle;
    const auto State = MakeShared<FState>();

    AddSettledWalker(this, State, TArray<FName>{ck_test_procedural_walker_fixture::LegId(0)});
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Walker.Subject,
                MakePlayerSpec(3, FCk_Time{3.0}));
            // The scope does not belong to the player, so the action outlives the player's entity.
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Action = DoPlayLift(*State, FCk_Time{3.0});
            TestTrue(TEXT("The lift is admitted"), ck::IsValid(State->Action));
            DoBindListener(*State);
            auto Finished = FCk_Delegate_ProceduralAction_OnFinished{};
            Finished.BindDynamic(State->Listener.Get(), &UCk_Test_ProceduralActionReentry_UE::OnFinished);
            UCk_Utils_ProceduralAction_UE::BindTo_OnFinished(State->Action, Finished);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Playing; }),
        WaitSeconds, TEXT("The lift is playing")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            auto PlayerEntity = State->Walker.Root;
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(PlayerEntity);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::Is_NOT_Valid(State->Walker.Root)
                || UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Finished;
        }), WaitSeconds, TEXT("The player's entity is gone or the action finished")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestTrue(TEXT("The action outlives its player"), ck::IsValid(State->Action));
            TestEqual(TEXT("Losing the player finishes the action"),
                UCk_Utils_ProceduralAction_UE::Get_State(State->Action), ECk_ProceduralAction_State::Finished);
            TestEqual(TEXT("A lost player reports SubjectLost"),
                UCk_Utils_ProceduralAction_UE::Get_Result(State->Action).Get_Outcome(),
                ECk_ProceduralAction_Outcome::SubjectLost);
            TestEqual(TEXT("OnFinished fires once"), State->Listener->FinishedCount, 1);
        })));
    DoCleanup(State);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionLifecycleCancelWhileCancellingTest,
    "Ck.ProceduralAnimation.Action.Lifecycle.CancelWhileCancellingSucceeds",
    ck_test_procedural_action_lifecycle::TestFlags)

auto
    FCkProceduralActionLifecycleCancelWhileCancellingTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_lifecycle;
    const auto State = MakeShared<FState>();
    constexpr auto Cancels = 2;

    AddSettledWalker(this, State, TArray<FName>{ck_test_procedural_walker_fixture::LegId(0)});
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Walker.Subject,
                MakePlayerSpec(3, FCk_Time{3.0}));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Walker.Root);
            State->Action = DoPlayLift(*State, FCk_Time{3.0});
            TestTrue(TEXT("The lift is admitted"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Playing; }),
        WaitSeconds, TEXT("The lift is playing")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            DoBindListener(*State);
            for (auto Index = 0; Index < Cancels; ++Index)
            {
                UCk_Utils_ProceduralAction_UE::Request_Cancel(State->Action,
                    FCk_Request_ProceduralAction_Cancel{TEXT("Test")}, MakeCompletion(*State));
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return State->Listener->CompletionResults.Num() == Cancels
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Finished;
        }), WaitSeconds, TEXT("Both cancels complete and the action finishes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            for (const auto Result : State->Listener->CompletionResults)
            {
                TestEqual(TEXT("A cancel whose intent holds completes Succeeded"),
                    Result, ECk_Request_OperationResult::Succeeded);
            }
            TestEqual(TEXT("Two cancels publish one terminal record"),
                Get_TerminalRecordCount(State->Player, UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action)), 1);
        })));
    DoCleanup(State);
    return true;
}

#endif

// --------------------------------------------------------------------------------------------------------------------
