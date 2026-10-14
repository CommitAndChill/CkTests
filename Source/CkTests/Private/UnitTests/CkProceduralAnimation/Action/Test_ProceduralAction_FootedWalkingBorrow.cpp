#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Debug/CkProceduralAnimation_Debug.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkProceduralAnimation/LimbControl/CkProceduralLimbControl_Fragment_Data.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "CkTest_ProceduralActionClipFixture.h"
#include "../CkTest_ProceduralWalkerFixture.h"

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_action_footed_walking_borrow
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 20.0;
    constexpr auto DistalSegmentIndex = 1;
    // A clip that authors no bend must keep the rig's own pole: a captured pole flips the distal roll by 180 degrees.
    constexpr auto MaxDistalRollDriftDegrees = 3.0;
    constexpr auto HeldRecords = 1024;
    const auto ClipDuration = FCk_Time{2.0};

    struct FState
    {
        ck_test_procedural_walker_fixture::FFloor Floor;
        ck_test_procedural_walker_fixture::FWalker Walker;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        FQuat DistalBeforeAdmission = FQuat::Identity;
        TOptional<FQuat> DistalOnFirstAcceptedSample;
    };

    auto
        Get_DistalSegmentRotation(
            const FState& InState)
        -> TOptional<FQuat>
    {
        const auto Snapshot = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(InState.Walker.Root);
        if (Snapshot.Get_Legs().IsEmpty()
            || NOT Snapshot.Get_Legs()[0].Get_Rig().Get_Segments().IsValidIndex(DistalSegmentIndex))
        { return {}; }
        return Snapshot.Get_Legs()[0].Get_Rig().Get_Segments()[DistalSegmentIndex].Get_Transform().GetRotation();
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionFootedWalkingBorrowKeepsSegmentRollTest,
    "Ck.ProceduralAnimation.Action.Gait.FootedWalkingBorrowEntersPlayingWithoutRollFlip",
    ck_test_procedural_action_footed_walking_borrow::TestFlags)

auto
    FCkProceduralActionFootedWalkingBorrowKeepsSegmentRollTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_footed_walking_borrow;
    using namespace ck_test_procedural_walker_fixture;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { State->Floor = AddFloor(InWorld); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsFloorReady(State->Floor); }),
        WaitSeconds, TEXT("Footed borrow floor exists in Jolt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { State->Walker = AddWalker(InWorld, TArray<FName>{LegId(0)}); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsWalkerSettled(State->Walker); }),
        WaitSeconds, TEXT("Footed walker settles over four planted feet")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto DistalBefore = Get_DistalSegmentRotation(*State);
            if (NOT TestTrue(TEXT("The borrowed leg's distal segment is readable before admission"),
                DistalBefore.IsSet()))
            { return; }
            State->DistalBeforeAdmission = *DistalBefore;

            auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
            auto Support = FCk_ProceduralLimbControl_Support{ECk_ProceduralLimbControl_SupportMode::CountOnly, 3};
            Support.Set_SupportHoldTimeout(FCk_Time{0.5});
            Locomotion.Set_Support(Support);
            auto Spec = FCk_ProceduralActionPlayer_Spec{};
            Spec.Set_Locomotion(Locomotion);
            auto Capacity = Spec.Get_Capacity();
            Capacity.Set_RecordsPerInstance(HeldRecords);
            Spec.Set_Capacity(Capacity);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Walker.Subject, Spec);
            TestTrue(TEXT("Footed borrow player attaches"), ck::IsValid(State->Player));

            const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(State->Walker.Root, LegId(0));
            const auto FootWorld = UCk_Utils_ProceduralLeg_UE::Get_Foot(Leg).Get_Position();
            const auto RootWorld = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Walker.Body);
            State->Clip = ck_test_procedural_action_clip_fixture::MakeHoldClip(LegId(0),
                ECk_ProceduralAction_TrackSpace::RootAtStart, RootWorld.InverseTransformPosition(FootWorld),
                ClipDuration);
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Walker.Root);
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Scope, State->Player,
                ck_test_procedural_action_clip_fixture::MakePlay(State->Clip.Get(), TArray<FName>{LegId(0)}));
            TestTrue(TEXT("Footed borrow hold clip is admitted"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            if (NOT ck::IsValid(State->Action))
            { return true; }
            if (NOT State->DistalOnFirstAcceptedSample.IsSet())
            {
                const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action);
                if (Receipt.Get_HasAcceptedPose() && Receipt.Get_CurrentPose().Get_Limbs().Num() == 1
                    && Receipt.Get_CurrentPose().Get_Limbs()[0].Get_Segments().IsValidIndex(DistalSegmentIndex))
                {
                    State->DistalOnFirstAcceptedSample = Receipt.Get_CurrentPose().Get_Limbs()[0]
                        .Get_Segments()[DistalSegmentIndex].GetRotation();
                }
            }
            const auto ActionState = UCk_Utils_ProceduralAction_UE::Get_State(State->Action);
            return ActionState == ECk_ProceduralAction_State::Playing
                || ActionState == ECk_ProceduralAction_State::Cancelling
                || ActionState == ECk_ProceduralAction_State::Returning
                || ActionState == ECk_ProceduralAction_State::Finished;
        }), WaitSeconds, TEXT("Footed walking borrow leaves its entry blend")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Result = UCk_Utils_ProceduralAction_UE::Get_Result(State->Action);
            AddInfo(FString::Printf(TEXT("[FootedBorrow] state=%d result=%s"),
                static_cast<int32>(UCk_Utils_ProceduralAction_UE::Get_State(State->Action)),
                *UCk_Utils_ProceduralAction_UE::Get_ResultText(Result, {}).ToString()));
            TestEqual(TEXT("A borrowed footed walking leg reaches Playing under the default sampling budget"),
                UCk_Utils_ProceduralAction_UE::Get_State(State->Action), ECk_ProceduralAction_State::Playing);
            if (TestTrue(TEXT("The first accepted sample carries the distal segment"),
                State->DistalOnFirstAcceptedSample.IsSet()))
            {
                const auto DriftDegrees = FMath::RadiansToDegrees(
                    State->DistalBeforeAdmission.AngularDistance(*State->DistalOnFirstAcceptedSample));
                AddInfo(FString::Printf(TEXT("[FootedBorrow] distal drift %.3f degrees"), DriftDegrees));
                TestTrue(FString::Printf(TEXT("Distal segment keeps its roll on the first accepted sample (%.3f deg)"),
                    DriftDegrees), DriftDegrees <= MaxDistalRollDriftDegrees);
            }
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Walker.Root);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor.Entity);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
