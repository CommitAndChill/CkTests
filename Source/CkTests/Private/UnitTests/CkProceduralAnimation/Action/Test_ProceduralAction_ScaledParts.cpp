#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/CkProceduralAnimation_Utils.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkProceduralAnimation/Limb/CkProceduralLimb_Utils.h"
#include "CkProceduralAnimation/LimbControl/CkProceduralLimbControl_Fragment_Data.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace ck_test_procedural_action_scaled_parts
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;
    const auto LimbId = FName{TEXT("ScaledArm")};
    const auto LegId = FName{TEXT("Leg0")};
    const auto SegmentScale = FVector{1.5, 0.75, 2.0};
    const auto TerminalScale = FVector{0.8, 1.25, 1.1};

    struct FState
    {
        FCk_Handle Root;
        FCk_Handle_Transform Body;
        FCk_Handle_Transform Segment;
        FCk_Handle_Transform Terminal;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralGait Gait;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        FCk_Handle Floor;
        FCk_Handle_JoltBody FloorBody;
    };

    auto
        VerifyAcceptedScaledPose(
            FAutomationTestBase& InTest,
            const FState& InState,
            float InLastLinkLength)
        -> void
    {
        const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(InState.Action);
        if (NOT Receipt.Get_HasAcceptedPose())
        {
            const auto Result = UCk_Utils_ProceduralAction_UE::Get_Result(InState.Action);
            InTest.AddInfo(FString::Printf(TEXT("Scaled pose missing: state=%d outcome=%d recovery=%d reason=%s"),
                static_cast<int32>(UCk_Utils_ProceduralAction_UE::Get_State(InState.Action)),
                static_cast<int32>(Result.Get_Outcome()), static_cast<int32>(Result.Get_Recovery()),
                *Result.Get_Reason().ToString()));
        }
        InTest.TestTrue(TEXT("Scaled action publishes an accepted settled receipt"),
            Receipt.Get_HasAcceptedPose() && Receipt.Get_Stamp().Get_SubjectPoseRevision() > 0);
        InTest.TestEqual(TEXT("Accepted receipt contains exactly the claimed limb"),
            Receipt.Get_CurrentPose().Get_Limbs().Num(), 1);
        if (Receipt.Get_CurrentPose().Get_Limbs().Num() != 1) { return; }
        const auto& Limb = Receipt.Get_CurrentPose().Get_Limbs()[0];
        InTest.TestTrue(TEXT("Accepted receipt includes solved segment geometry"),
            NOT Limb.Get_Segments().IsEmpty());
        if (Limb.Get_Segments().IsEmpty()) { return; }
        const auto LiveSegment = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(InState.Segment);
        const auto LiveTerminal = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(InState.Terminal);
        InTest.TestTrue(TEXT("Live segment keeps its nonuniform visual scale"),
            LiveSegment.GetScale3D().Equals(SegmentScale, 1.0e-5));
        InTest.TestTrue(TEXT("Live terminal keeps its nonuniform visual scale"),
            LiveTerminal.GetScale3D().Equals(TerminalScale, 1.0e-5));
        InTest.TestTrue(TEXT("Accepted segment is the observed live transform"),
            Limb.Get_Segments()[0].Equals(LiveSegment, 0.02));
        InTest.TestTrue(TEXT("Accepted endpoint uses the observed terminal position and rotation"),
            Limb.Get_AcceptedEndpoint().GetLocation().Equals(LiveTerminal.GetLocation(), 0.02)
                && Limb.Get_AcceptedEndpoint().GetRotation().Equals(
                    LiveTerminal.GetRotation(), 1.0e-3));
        InTest.TestTrue(TEXT("Accepted kinematic endpoint remains unit scale"),
            Limb.Get_AcceptedEndpoint().GetScale3D().Equals(FVector::OneVector, 1.0e-6));
        const auto& Last = Limb.Get_Segments().Last();
        const auto KinematicEndpoint = Last.GetLocation()
            + Last.GetRotation().GetAxisX() * (InLastLinkLength * 0.5);
        InTest.TestTrue(TEXT("Endpoint uses authored link length, not visual segment scale"),
            KinematicEndpoint.Equals(Limb.Get_AcceptedEndpoint().GetLocation(), 0.5));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralActionScaledDedicatedPartsTest,
    "Ck.ProceduralAnimation.Action.Manual.ScaledDedicatedPartsPreserveVisuals",
    ck_test_procedural_action_scaled_parts::TestFlags)

auto
FCkProceduralActionScaledDedicatedPartsTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_scaled_parts;
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Body = UCk_Utils_Transform_UE::Add(State->Root,
                FTransform{FVector{0.0, 0.0, 2000.0}}, ECk_Replication::DoesNotReplicate);
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Body);
            State->Segment = UCk_Utils_Transform_UE::Add(SegmentEntity,
                FTransform{FQuat::Identity, FVector{50.0, 0.0, 2000.0}, SegmentScale},
                ECk_Replication::DoesNotReplicate);
            auto TerminalEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Body);
            State->Terminal = UCk_Utils_Transform_UE::Add(TerminalEntity,
                FTransform{FQuat::Identity, FVector{100.0, 0.0, 2000.0}, TerminalScale},
                ECk_Replication::DoesNotReplicate);
            const auto Limb = FCk_ProceduralLimb_Layout{LimbId, FTransform::Identity,
                TArray<float>{100.0f}, FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector};
            auto Binding = FCk_ProceduralLimb_Binding{LimbId,
                TArray<FCk_Handle_Transform>{State->Segment}};
            Binding.Set_Terminal(State->Terminal);
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Body,
                FCk_ProceduralArticulation_Spec{
                    FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Limb}},
                    TArray<FCk_ProceduralLimb_Binding>{Binding}});
            TestTrue(TEXT("Scaled dedicated bindings are admitted"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Scaled dedicated subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                State->Subject, FCk_ProceduralActionPlayer_Spec{});
            TestTrue(TEXT("Scaled dedicated player attaches"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Body);
            State->Action = UCk_Utils_ProceduralAction_UE::Create_Manual(State->Scope,
                State->Player, FCk_ProceduralAction_ControlSpec{TArray<FName>{LimbId}});
            TestTrue(TEXT("Scaled dedicated action reserves"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
                    == ECk_ProceduralAction_State::Ready;
        }), WaitSeconds, TEXT("Scaled dedicated action captures settled entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            auto Target = FCk_ProceduralAction_LimbTarget{LimbId,
                FTransform{FQuat::Identity,
                    FVector{FMath::Sqrt(100.0 * 100.0 - 0.5 * 0.5), 0.5, 0.0}}};
            Target.Set_Space(ECk_ProceduralAction_TrackSpace::RootAtStart);
            auto Batch = FCk_ProceduralAction_PoseBatch{};
            Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{Target});
            UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(State->Action,
                FCk_Request_ProceduralAction_SubmitPoseBatch{Batch}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action).Get_HasAcceptedPose();
        }), WaitSeconds, TEXT("Scaled dedicated action receives actual accepted receipt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            VerifyAcceptedScaledPose(*this, *State, 100.0f);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralActionScaledLegacyPartsTest,
    "Ck.ProceduralAnimation.Action.Manual.ScaledLegacyPartsPreserveVisuals",
    ck_test_procedural_action_scaled_parts::TestFlags)

auto
FCkProceduralActionScaledLegacyPartsTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_scaled_parts;
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Floor = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            UCk_Utils_Transform_UE::Add(State->Floor,
                FTransform{FVector{0.0, 47000.0, 4975.0}}, ECk_Replication::DoesNotReplicate);
            auto Shape = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box};
            Shape.Set_HalfExtents(FVector{800.0, 800.0, 25.0});
            auto FloorSpec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
            FloorSpec.Set_ShapeDimensions(Shape);
            FloorSpec.Set_MotionType(ECk_MotionType::Static);
            FloorSpec.Set_CollisionProfileName(TEXT("BlockAll"));
            State->FloorBody = UCk_Utils_JoltBody_UE::Add(State->Floor, FloorSpec);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->FloorBody) && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(State->FloorBody); }),
        WaitSeconds, TEXT("Scaled legacy floor is present in Jolt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Body = UCk_Utils_Transform_UE::Add(State->Root,
                FTransform{FVector{0.0, 47000.0, 5060.0}}, ECk_Replication::DoesNotReplicate);
            auto Legs = TArray<FCk_ProceduralLeg_Spec>{};
            const auto Corners = TArray<FVector2D>{{40.0, 30.0}, {40.0, -30.0}, {-40.0, 30.0}, {-40.0, -30.0}};
            for (auto Index = 0; Index < Corners.Num(); ++Index)
            {
                const auto& Corner = Corners[Index];
                auto Placement = FCk_ProceduralLeg_Placement{
                    FVector{Corner.X, Corner.Y, 0.0}, FVector{Corner.X, Corner.Y * 2.0, -60.0}};
                Placement.Set_PhaseOffset(Index == 0 || Index == 3 ? 0.0f : 0.5f);
                Legs.Emplace(FName{*FString::Printf(TEXT("Leg%d"), Index)}, Placement,
                    FCk_ProceduralLeg_ChainGeometry{TArray<float>{60.0f, 80.0f}});
            }
            auto* Rig = NewObject<UCk_ProceduralRig_Data>();
            Rig->Set_Legs(Legs);
            auto Segments = TArray<FCk_Handle_Transform>{};
            for (auto Index = 0; Index < 2; ++Index)
            {
                auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Body);
                const auto Scale = Index == 0 ? SegmentScale : FVector{0.9, 1.4, 0.7};
                Segments.Add(UCk_Utils_Transform_UE::Add(Entity,
                    FTransform{FQuat::Identity, FVector{0.0, 47000.0, 5060.0}, Scale},
                    ECk_Replication::DoesNotReplicate));
            }
            State->Segment = Segments[0];
            auto TerminalEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Body);
            State->Terminal = UCk_Utils_Transform_UE::Add(TerminalEntity,
                FTransform{FQuat::Identity, FVector{0.0, 47000.0, 5060.0}, TerminalScale},
                ECk_Replication::DoesNotReplicate);
            auto BoundRig = FCk_ProceduralRig_Spec{Segments};
            BoundRig.Set_Foot(State->Terminal);
            const auto Walker = UCk_Utils_ProceduralAnimation_UE::Add_Walker(State->Body,
                Rig, NewObject<UCk_ProceduralGait_Data>(),
                TArray<FCk_ProceduralWalker_LegChain>{FCk_ProceduralWalker_LegChain{LegId, BoundRig}});
            State->Gait = Walker.Get_Gait();
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Cast(State->Root);
            TestTrue(TEXT("Scaled legacy walker and articulation compose"),
                ck::IsValid(State->Gait) && ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0
                && UCk_Utils_ProceduralGait_UE::Get_PlantedCount(State->Gait) == 4;
        }), WaitSeconds, TEXT("Scaled legacy walker settles over four planted feet")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
            auto Support = FCk_ProceduralLimbControl_Support{
                ECk_ProceduralLimbControl_SupportMode::CountOnly, 3};
            Support.Set_SupportHoldTimeout(FCk_Time{0.5});
            Locomotion.Set_Support(Support);
            auto PlayerSpec = FCk_ProceduralActionPlayer_Spec{};
            PlayerSpec.Set_Locomotion(Locomotion);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject, PlayerSpec);
            TestTrue(TEXT("Scaled legacy player attaches"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LegId}};
            Control.Set_InputTimeout(FCk_Time{10.0});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_Manual(State->Scope,
                State->Player, Control);
            TestTrue(TEXT("Scaled legacy action reserves"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
                    == ECk_ProceduralAction_State::Ready;
        }), WaitSeconds, TEXT("Scaled legacy action borrows settled walking limb")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(State->Root, LegId);
            const auto Foot = UCk_Utils_ProceduralLeg_UE::Get_Foot(Leg);
            const auto RootWorld = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Body);
            const auto TargetWorld = Foot.Get_Position() + FVector{0.25, 0.0, 0.0};
            auto Target = FCk_ProceduralAction_LimbTarget{LegId,
                FTransform{RootWorld.InverseTransformRotation(Foot.Get_Rotation()),
                    RootWorld.InverseTransformPosition(TargetWorld)}};
            Target.Set_Space(ECk_ProceduralAction_TrackSpace::RootAtStart);
            auto Layout = FCk_ProceduralLimb_Layout{};
            const auto Limb = UCk_Utils_ProceduralArticulation_UE::Get_Limb(State->Subject, LegId);
            if (NOT TestTrue(TEXT("Scaled legacy target uses its existing authored bend plane"),
                UCk_Utils_ProceduralLimb_UE::Try_GetLayout(Limb, Layout)))
            { return; }
            Target.Set_BendDirection(Layout.Get_BendLocal().GetSafeNormal());
            auto Batch = FCk_ProceduralAction_PoseBatch{};
            Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{Target});
            UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(State->Action,
                FCk_Request_ProceduralAction_SubmitPoseBatch{Batch}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action).Get_HasAcceptedPose();
        }), WaitSeconds, TEXT("Scaled legacy action receives actual accepted receipt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            VerifyAcceptedScaledPose(*this, *State, 80.0f);
            const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action);
            if (Receipt.Get_CurrentPose().Get_Limbs().Num() == 1)
            {
                const auto& Segments = Receipt.Get_CurrentPose().Get_Limbs()[0].Get_Segments();
                TestEqual(TEXT("Legacy receipt retains both solved links"), Segments.Num(), 2);
                if (Segments.Num() == 2)
                {
                    TestTrue(TEXT("Second visual link keeps its independent scale"),
                        Segments[1].GetScale3D().Equals(FVector{0.9, 1.4, 0.7}, 1.0e-5));
                }
            }
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Root);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
