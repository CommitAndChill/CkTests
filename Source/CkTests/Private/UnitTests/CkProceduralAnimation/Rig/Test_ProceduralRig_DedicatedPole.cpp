#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Rig/CkProceduralRig_Processor.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace ck_test_procedural_rig_dedicated_pole
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const auto RootPose = FTransform{FQuat::Identity, FVector{100.0, 200.0, 2000.0}};
    const auto LimbId = FName{TEXT("NearStraightArm")};

    auto
    MakeSubject(
        UWorld* InWorld,
        const FVector& InBendLocal)
        -> FCk_Handle_ProceduralArticulation
    {
        auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        auto Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose, ECk_Replication::DoesNotReplicate);
        auto Segments = TArray<FCk_Handle_Transform>{};
        for (auto Index = 0; Index < 2; ++Index)
        {
            auto Part = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            Segments.Add(UCk_Utils_Transform_UE::Add(Part, RootPose, ECk_Replication::DoesNotReplicate));
        }
        const auto Limb = FCk_ProceduralLimb_Layout{LimbId, FTransform::Identity,
            TArray<float>{50.0f, 50.0f},
            FTransform{FQuat::Identity, FVector{90.0, 0.0, 0.0}}, InBendLocal};
        const auto Spec = FCk_ProceduralArticulation_Spec{
            FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Limb}},
            TArray<FCk_ProceduralLimb_Binding>{FCk_ProceduralLimb_Binding{LimbId, Segments}}};
        return UCk_Utils_ProceduralArticulation_UE::Add(Root, Spec);
    }

    auto
    Evaluate(
        const FCk_Handle_ProceduralArticulation& InSubject,
        const double InReach,
        ck::FProceduralRig_SelectedChain& OutChain)
        -> bool
    {
        const auto Target = ck::FProceduralRig_WorldTarget{LimbId,
            FTransform{FQuat::Identity, RootPose.GetLocation() + FVector{InReach, 0.0, 0.0}},
            TOptional<FVector>{}, TOptional<FQuat>{}};
        const auto Targets = TArray<ck::FProceduralRig_WorldTarget>{Target};
        auto Chains = TArray<ck::FProceduralRig_SelectedChain>{};
        if (NOT ck::FProcessor_ProceduralRig_Update::EvaluateDedicatedCandidate(
            InSubject, RootPose, MakeArrayView(Targets), Chains) || Chains.Num() != 1)
        { return false; }
        OutChain = MoveTemp(Chains[0]);
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralRigDedicatedPoleTest,
    "Ck.ProceduralAnimation.Action.Rig.DedicatedPoleMagnitudeAndFullReach",
    ck_test_procedural_rig_dedicated_pole::TestFlags)

auto
FCkProceduralRigDedicatedPoleTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_rig_dedicated_pole;
    AddExpectedError(TEXT("Procedural articulation Add rejected root"),
        EAutomationExpectedErrorFlags::Contains, 0);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this](UWorld* InWorld)
        {
            const auto ShortBend = MakeSubject(InWorld, FVector::UpVector);
            const auto LongBend = MakeSubject(InWorld, FVector::UpVector * 100.0);
            const auto OverflowingBend = MakeSubject(InWorld, FVector{0.0, 1.0e300, 0.0});
            if (NOT TestTrue(TEXT("Both dedicated layouts admit"),
                ck::IsValid(ShortBend) && ck::IsValid(LongBend)))
            { return; }
            TestFalse(TEXT("Finite bend whose squared magnitude overflows is rejected"),
                ck::IsValid(OverflowingBend));

            auto BeforeThreshold = ck::FProceduralRig_SelectedChain{};
            auto AfterThreshold = ck::FProceduralRig_SelectedChain{};
            auto FullReach = ck::FProceduralRig_SelectedChain{};
            auto ScaledBend = ck::FProceduralRig_SelectedChain{};
            if (NOT TestTrue(TEXT("Two-bone default pole solves across old roll threshold and full reach"),
                Evaluate(ShortBend, 99.99, BeforeThreshold)
                && Evaluate(ShortBend, 99.999, AfterThreshold)
                && Evaluate(ShortBend, 100.0, FullReach)
                && Evaluate(LongBend, 99.999, ScaledBend)))
            { return; }

            TestTrue(TEXT("Dedicated default pole lies one full chain length from the hip"),
                FullReach.Get_Pole().Equals(RootPose.GetLocation() + FVector{0.0, 0.0, 100.0}, 0.001));
            TestTrue(TEXT("Bend vector magnitude does not change the selected pole"),
                ScaledBend.Get_Pole().Equals(AfterThreshold.Get_Pole(), 0.001));
            TestTrue(TEXT("Full reach preserves the requested endpoint"),
                FullReach.Get_AcceptedEndpoint().Equals(
                    RootPose.GetLocation() + FVector{100.0, 0.0, 0.0}, 0.01));
            if (NOT TestEqual(TEXT("Both solved chains retain two link poses"),
                AfterThreshold.Get_PartPoses().Num(), 2))
            { return; }
            const auto LowerBefore = BeforeThreshold.Get_PartPoses()[1].GetRotation();
            const auto LowerAfter = AfterThreshold.Get_PartPoses()[1].GetRotation();
            const auto LowerFull = FullReach.Get_PartPoses()[1].GetRotation();
            const auto LowerScaled = ScaledBend.Get_PartPoses()[1].GetRotation();
            TestTrue(TEXT("Lower-link roll stays continuous across the old pole crossing"),
                FMath::RadiansToDegrees(LowerBefore.AngularDistance(LowerAfter)) < 5.0);
            TestTrue(TEXT("Lower-link roll stays continuous at full extension"),
                FMath::RadiansToDegrees(LowerAfter.AngularDistance(LowerFull)) < 5.0);
            TestTrue(TEXT("Scaling the bend direction does not rotate the lower link"),
                FMath::RadiansToDegrees(LowerAfter.AngularDistance(LowerScaled)) < 0.01);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
