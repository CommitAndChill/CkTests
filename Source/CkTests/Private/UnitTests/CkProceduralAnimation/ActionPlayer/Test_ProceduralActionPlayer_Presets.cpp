#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Fragment.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "../CkTest_ProceduralWalkerFixture.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_action_player_presets
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 20.0;
    const auto ArmId = FName{TEXT("PresetArm")};
    const auto CountOnlyEnsure = FString{TEXT("Count-only support rejected minimum trusted contacts")};

    auto
        MakeTuning()
        -> FCk_ProceduralAction_Tuning
    {
        auto Tuning = FCk_ProceduralAction_Tuning{};
        auto Playback = Tuning.Get_Playback();
        Playback.Set_PlayRate(1.5f);
        Tuning.Set_Playback(Playback);
        auto Transition = Tuning.Get_Transition();
        Transition.Set_DedicatedReturn(FCk_Time{0.3});
        Transition.Set_ReturnTimeout(FCk_Time{1.2});
        Tuning.Set_Transition(Transition);
        return Tuning;
    }

    template <typename T>
    auto
        Get_AreEqual(
            const T& InA,
            const T& InB)
        -> bool
    {
        return T::StaticStruct()->CompareScriptStruct(&InA, &InB, PPF_None);
    }

    struct FDedicatedState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
    };

    auto
        AddArmSubject(
            UWorld* InWorld,
            FCk_Handle_Transform& OutRoot)
        -> FCk_Handle_ProceduralArticulation
    {
        auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        OutRoot = UCk_Utils_Transform_UE::Add(RootEntity,
            FTransform{FVector{100.0, 200.0, 2000.0}}, ECk_Replication::DoesNotReplicate);
        auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(OutRoot);
        const auto Segment = UCk_Utils_Transform_UE::Add(SegmentEntity,
            FTransform::Identity, ECk_Replication::DoesNotReplicate);
        const auto Limb = FCk_ProceduralLimb_Layout{ArmId, FTransform::Identity,
            TArray<float>{100.0f}, FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector};
        return UCk_Utils_ProceduralArticulation_UE::Add(OutRoot, FCk_ProceduralArticulation_Spec{
            FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Limb}},
            TArray<FCk_ProceduralLimb_Binding>{FCk_ProceduralLimb_Binding{
                ArmId, TArray<FCk_Handle_Transform>{Segment}}}});
    }

    struct FBorrowingState
    {
        ck_test_procedural_walker_fixture::FFloor Floor;
        ck_test_procedural_walker_fixture::FWalker Walker;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
    };
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionPlayerPresetDedicatedTest,
    "Ck.ProceduralAnimation.ActionPlayer.Presets.DedicatedSpecAddsAndMatchesTuning",
    ck_test_procedural_action_player_presets::TestFlags)

auto
    FCkProceduralActionPlayerPresetDedicatedTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_player_presets;
    const auto Tuning = MakeTuning();
    const auto Spec = UCk_Utils_ProceduralActionPlayer_UE::Make_DedicatedPlayerSpec(Tuning);
    TestTrue(TEXT("Dedicated preset carries the given tuning"), Get_AreEqual(Spec.Get_Tuning(), Tuning));
    TestTrue(TEXT("Dedicated preset keeps default capacity"),
        Get_AreEqual(Spec.Get_Capacity(), FCk_ProceduralAction_Capacity{}));
    TestEqual(TEXT("Dedicated preset never borrows walking limbs"),
        Spec.Get_Locomotion().Get_Support().Get_Mode(), ECk_ProceduralLimbControl_SupportMode::Disabled);

    const auto State = MakeShared<FDedicatedState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Subject = AddArmSubject(InWorld, State->Root);
            TestTrue(TEXT("Dedicated subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Dedicated subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Tuning = MakeTuning();
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject,
                UCk_Utils_ProceduralActionPlayer_UE::Make_DedicatedPlayerSpec(Tuning));
            TestTrue(TEXT("Dedicated preset is admitted by Add"), ck::IsValid(State->Player));
            if (NOT ck::IsValid(State->Player))
            { return; }
            TestTrue(TEXT("Player defaults equal the preset tuning"), Get_AreEqual(
                State->Player.Get<ck::FFragment_ProceduralActionPlayer_Tunables>().Get_Tuning(), Tuning));
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionPlayerPresetBorrowingTest,
    "Ck.ProceduralAnimation.ActionPlayer.Presets.BorrowingSpecCarriesSupportAndReturn",
    ck_test_procedural_action_player_presets::TestFlags)

auto
    FCkProceduralActionPlayerPresetBorrowingTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_player_presets;
    auto Support = UCk_Utils_ProceduralActionPlayer_UE::Make_CountOnlySupport(3);
    Support.Set_SupportHoldTimeout(FCk_Time{0.4});
    auto Return = FCk_ProceduralLimbControl_Return{};
    Return.Set_Duration(FCk_Time{0.25});
    Return.Set_Timeout(FCk_Time{0.9});
    Return.Set_MaxProbeAttempts(4);
    const auto Tuning = MakeTuning();
    auto Borrowing = FCk_ProceduralActionPlayer_BorrowingSpec{Support, Return};
    Borrowing.Set_Tuning(Tuning);

    const auto Spec = UCk_Utils_ProceduralActionPlayer_UE::Make_BorrowingPlayerSpec(Borrowing);
    TestEqual(TEXT("Count-only convenience selects count-only support"),
        Support.Get_Mode(), ECk_ProceduralLimbControl_SupportMode::CountOnly);
    TestEqual(TEXT("Count-only convenience keeps the requested minimum"), Support.Get_MinimumTrustedContacts(), 3);
    TestTrue(TEXT("Borrowing preset carries the support policy unchanged"),
        Get_AreEqual(Spec.Get_Locomotion().Get_Support(), Support));
    TestTrue(TEXT("Borrowing preset carries the return policy unchanged"),
        Get_AreEqual(Spec.Get_Locomotion().Get_Return(), Return));
    TestTrue(TEXT("Borrowing preset carries the tuning unchanged"), Get_AreEqual(Spec.Get_Tuning(), Tuning));
    TestTrue(TEXT("Borrowing preset keeps capacity independent"),
        Get_AreEqual(Spec.Get_Capacity(), FCk_ProceduralAction_Capacity{}));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionPlayerPresetCountOnlyEnsureTest,
    "Ck.ProceduralAnimation.ActionPlayer.Presets.CountOnlyBelowOneEnsures",
    ck_test_procedural_action_player_presets::TestFlags)

auto
    FCkProceduralActionPlayerPresetCountOnlyEnsureTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_player_presets;
    // The ensure logger and automation reporter both emit the diagnostic; assert the rejected value instead of
    // coupling the test to diagnostic transport multiplicity.
    AddExpectedError(CountOnlyEnsure, EAutomationExpectedErrorFlags::Contains, 0);

    for (const auto Minimum : TArray<int32>{0, -2, 65})
    {
        const auto Support = UCk_Utils_ProceduralActionPlayer_UE::Make_CountOnlySupport(Minimum);
        TestEqual(*FString::Printf(TEXT("Minimum %d keeps the requested count-only mode"), Minimum),
            Support.Get_Mode(), ECk_ProceduralLimbControl_SupportMode::CountOnly);
        TestEqual(*FString::Printf(TEXT("Minimum %d is returned unchanged for Add to reject"), Minimum),
            Support.Get_MinimumTrustedContacts(), Minimum);
    }
    const auto One = UCk_Utils_ProceduralActionPlayer_UE::Make_CountOnlySupport(1);
    TestEqual(TEXT("One trusted contact is the smallest valid count-only policy"),
        One.Get_Mode(), ECk_ProceduralLimbControl_SupportMode::CountOnly);
    TestEqual(TEXT("One trusted contact is carried"), One.Get_MinimumTrustedContacts(), 1);

    const auto State = MakeShared<FDedicatedState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { State->Subject = AddArmSubject(InWorld, State->Root); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Dedicated subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Borrowing = FCk_ProceduralActionPlayer_BorrowingSpec{
                UCk_Utils_ProceduralActionPlayer_UE::Make_CountOnlySupport(0), FCk_ProceduralLimbControl_Return{}};
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject,
                UCk_Utils_ProceduralActionPlayer_UE::Make_BorrowingPlayerSpec(Borrowing));
            TestFalse(TEXT("Add rejects the invalid count-only policy"), ck::IsValid(State->Player));
            TestFalse(TEXT("The rejected Add leaves no player on the subject"),
                UCk_Utils_ProceduralActionPlayer_UE::Has(State->Subject));
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionPlayerPresetInsufficientSupportTest,
    "Ck.ProceduralAnimation.ActionPlayer.Presets.BorrowingWithInsufficientContactsRejectsAtAdmission",
    ck_test_procedural_action_player_presets::TestFlags)

auto
    FCkProceduralActionPlayerPresetInsufficientSupportTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_player_presets;
    using namespace ck_test_procedural_walker_fixture;
    const auto State = MakeShared<FBorrowingState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { State->Floor = AddFloor(InWorld); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsFloorReady(State->Floor); }),
        WaitSeconds, TEXT("Walker floor is present in Jolt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Walker = AddWalker(InWorld, TArray<FName>{LegId(0)});
            TestTrue(TEXT("Walker composes with one bound leg"), ck::IsValid(State->Walker.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsWalkerSettled(State->Walker); }),
        WaitSeconds, TEXT("Walker settles over four planted feet")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            // Five exceeds the four feet this walker has, yet stays within the support policy's contact ceiling, so
            // the player composes and the shortfall is found where it belongs: at borrowing admission.
            constexpr auto MoreContactsThanFeet = 5;
            const auto Borrowing = FCk_ProceduralActionPlayer_BorrowingSpec{
                UCk_Utils_ProceduralActionPlayer_UE::Make_CountOnlySupport(MoreContactsThanFeet),
                FCk_ProceduralLimbControl_Return{}};
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Walker.Subject,
                UCk_Utils_ProceduralActionPlayer_UE::Make_BorrowingPlayerSpec(Borrowing));
            TestTrue(TEXT("Borrowing preset composes a player"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Walker.Root);
            State->Action = UCk_Utils_ProceduralAction_UE::Create_Manual(State->Scope, State->Player,
                FCk_ProceduralAction_ControlSpec{TArray<FName>{LegId(0)}});
            TestTrue(TEXT("Manual action reserves an identity before admission"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Finished;
        }), WaitSeconds, TEXT("Borrowing admission completes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Result = UCk_Utils_ProceduralAction_UE::Get_Result(State->Action);
            TestEqual(TEXT("Insufficient trusted support rejects the action"),
                Result.Get_Outcome(), ECk_ProceduralAction_Outcome::Rejected);
            TestEqual(TEXT("Rejection names the support shortfall"),
                Result.Get_Reason(), FName{TEXT("SupportUnavailable")});
            TestEqual(TEXT("A rejected admission needs no recovery"),
                Result.Get_Recovery(), ECk_ProceduralAction_Recovery::NotRequired);
            TestEqual(TEXT("The walking limb stays under the gait"),
                UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Walker.Subject, LegId(0)).Get_State(),
                ECk_ProceduralLimbControl_State::Default);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Walker.Root);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor.Entity);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
