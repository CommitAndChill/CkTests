#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/LimbControl/CkProceduralLimbControl_Control.h"
#include "CkProceduralAnimation/LimbControl/CkProceduralLimbControl_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_limb_control_deadline
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const auto ArmId = FName{TEXT("DeadlineArm")};
    const auto Token = FCk_ArticulationControlToken{810, 1};
    const auto Timeout = FCk_Time{0.15};

    struct FState
    {
        FCk_Handle_Transform Root;
        FCk_Handle Scope;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Time Started;
        FCk_Time LastObserved;
        FCk_Time MaximumObservedStep;
        bool Acquired = false;
        bool RepeatedReturnPreservedReason = true;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralLimbControlRootDeadlineTest,
    "Ck.ProceduralAnimation.Action.LimbControl.RootOwnedDeadlineCannotBeExtended",
    ck_test_procedural_limb_control_deadline::TestFlags)

auto
    FCkProceduralLimbControlRootDeadlineTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_limb_control_deadline;
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity,
                FTransform{FVector{0.0, 0.0, 2000.0}}, ECk_Replication::DoesNotReplicate);
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto Segment = UCk_Utils_Transform_UE::Add(SegmentEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            const auto Limb = FCk_ProceduralLimb_Layout{ArmId, FTransform::Identity,
                TArray<float>{100.0f}, FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector};
            const auto Spec = FCk_ProceduralArticulation_Spec{
                FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Limb}},
                TArray<FCk_ProceduralLimb_Binding>{FCk_ProceduralLimb_Binding{
                    ArmId, TArray<FCk_Handle_Transform>{Segment}}}};
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root, Spec);
            TestTrue(TEXT("Subject composes"), ck::IsValid(State->Subject));
            TestTrue(TEXT("Root-owned control attaches without a player"),
                FCk_ProceduralLimbControl_Control::Attach(State->Subject));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), 10.0, TEXT("Subject settles before acquisition")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Policy = FCk_ProceduralLimbControl_Return{};
            Policy.Set_Duration(FCk_Time{0.05});
            Policy.Set_Timeout(Timeout);
            auto Motion = FCk_ProceduralLimbControl_MotionBudget{};
            Motion.Set_MaxEndpointTravelCm(0.75f);
            Motion.Set_MaxSegmentAngularStepDegrees(3.0f);
            Policy.Set_Motion(Motion);
            auto Entry = FCk_ProceduralArticulation_Pose{};
            auto InvalidPolicy = Policy;
            InvalidPolicy.Set_ContactToleranceCm(-1.0f);
            const auto Channels = FCk_ArticulationChannelSet{TArray<FName>{ArmId}, false};
            TestEqual(TEXT("Negative contact tolerance cannot acquire a channel"),
                FCk_ProceduralLimbControl_Control::TryAcquire(State->Subject, Token,
                    State->Scope, Channels, InvalidPolicy, Entry),
                ECk_ProceduralLimbControl_AcquireResult::InvalidChannels);
            TestTrue(TEXT("Rejected recovery policy leaves the channel unclaimed"),
                FCk_ProceduralArticulation_Control::GetClaimant(State->Subject, ArmId, false)
                    == FCk_ArticulationControlToken{});
            State->Acquired = FCk_ProceduralLimbControl_Control::TryAcquire(State->Subject, Token,
                State->Scope, Channels, Policy, Entry)
                    == ECk_ProceduralLimbControl_AcquireResult::Accepted;
            TestTrue(TEXT("Dedicated channel acquires from actual settlement"), State->Acquired);
            const auto Observation = UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, ArmId);
            TestEqual(TEXT("Public observation reports the admitted control"), Observation.Get_State(),
                ECk_ProceduralLimbControl_State::Controlled);
            TestTrue(TEXT("Public observation identifies the scope owner"), Observation.Get_Owner() == State->Scope);
            TestFalse(TEXT("A dedicated arm is not a locomotor"), Observation.Get_Locomotor());
            State->Started = State->Subject.Get<ck::FFragment_ProceduralLimbControl>().Get_ServiceTime();
            State->LastObserved = State->Started;
            TestEqual(TEXT("Return starts once"),
                FCk_ProceduralLimbControl_Control::BeginReturn(State->Subject, Token, TEXT("ExplicitEnd")),
                ECk_ProceduralLimbControl_ReturnState::Returning);
            TestTrue(TEXT("Root return work is discoverable without an action entity"),
                FCk_ProceduralLimbControl_Control::HasReturnWork(State->Subject));
            auto PinnedMotion = FCk_ProceduralLimbControl_MotionBudget{};
            Policy.Set_Motion(FCk_ProceduralLimbControl_MotionBudget{});
            TestTrue(TEXT("Return exposes its captured geometry budget"),
                FCk_ProceduralLimbControl_Control::Get_ReturnMotionBudget(State->Subject, Token, PinnedMotion));
            TestEqual(TEXT("Caller policy changes cannot alter admitted travel limit"),
                PinnedMotion.Get_MaxEndpointTravelCm(), 0.75f);
            TestEqual(TEXT("Caller policy changes cannot alter admitted angular limit"),
                PinnedMotion.Get_MaxSegmentAngularStepDegrees(), 3.0f);
            auto Actual = FCk_ProceduralArticulation_Pose{};
            TestTrue(TEXT("Return exposes actual settled geometry without an action entity"),
                FCk_ProceduralLimbControl_Control::Get_ReturnActualPose(State->Subject, Token, Actual));
            TestTrue(TEXT("Return starts from the captured actual endpoint"),
                Actual.Get_Limbs().Num() == 1 && Entry.Get_Limbs().Num() == 1
                    && Actual.Get_Limbs()[0].Get_AcceptedEndpoint().Equals(
                        Entry.Get_Limbs()[0].Get_AcceptedEndpoint(), 1.0e-6));
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Scope);
        })));
    // No action player submits return poses. The registered Physics reconciler must enforce its own finite deadline.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            if (NOT State->Acquired || NOT ck::IsValid(State->Subject))
            { return false; }
            const auto Now = State->Subject.Get<ck::FFragment_ProceduralLimbControl>().Get_ServiceTime();
            State->MaximumObservedStep = FMath::Max(State->MaximumObservedStep, Now - State->LastObserved);
            State->LastObserved = Now;
            const auto ReturnState = FCk_ProceduralLimbControl_Control::Get_ReturnState(State->Subject, Token);
            if (ReturnState == ECk_ProceduralLimbControl_ReturnState::Returning)
            {
                FCk_ProceduralLimbControl_Control::BeginReturn(State->Subject, Token, TEXT("RepeatedCancel"));
                State->RepeatedReturnPreservedReason &= FCk_ProceduralLimbControl_Control::Get_ReturnReason(
                    State->Subject, Token) == FName{TEXT("ExplicitEnd")};
            }
            return ReturnState == ECk_ProceduralLimbControl_ReturnState::Failed;
        }), 3.0, TEXT("Deadline survives owner destruction and repeated cancellation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestTrue(TEXT("Repeated cancellation preserved the original transition"),
                State->RepeatedReturnPreservedReason);
            const auto Elapsed = State->LastObserved - State->Started;
            TestTrue(TEXT("Failure occurs at the original service deadline"),
                Elapsed >= Timeout && Elapsed <= Timeout + State->MaximumObservedStep);
            TestEqual(TEXT("Recovery failure has its own observable reason"),
                FCk_ProceduralLimbControl_Control::Get_ReturnReason(State->Subject, Token),
                FName{TEXT("ReturnTimeout")});
            TestTrue(TEXT("Finite deadline releases the claim"),
                FCk_ProceduralArticulation_Control::GetClaimant(State->Subject, ArmId, false)
                    == FCk_ArticulationControlToken{});
            TestFalse(TEXT("Terminal recovery does not keep the player doing work"),
                FCk_ProceduralLimbControl_Control::HasReturnWork(State->Subject));
            TestEqual(TEXT("No unacknowledged publication requires faulting this subject"),
                UCk_Utils_ProceduralArticulation_UE::Get_Health(State->Subject),
                ECk_ProceduralArticulation_Health::Operational);
            TestTrue(TEXT("Terminal recovery persists until explicitly acknowledged"),
                FCk_ProceduralLimbControl_Control::ForgetTerminal(State->Subject, Token));
            TestEqual(TEXT("Acknowledgement removes the terminal controller record"),
                FCk_ProceduralLimbControl_Control::Get_ReturnState(State->Subject, Token),
                ECk_ProceduralLimbControl_ReturnState::None);
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
