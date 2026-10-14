#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Fragment.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralArticulationTransformAdmissionTest,
    "Ck.ProceduralAnimation.Action.Admission.InvalidTransformsLeaveNoPartialState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkProceduralArticulationTransformAdmissionTest::
    RunTest(
        const FString&)
    -> bool
{
    // The ensure logger and automation reporter both emit the diagnostic. Assert each rejection's
    // atomic state below rather than coupling the test to diagnostic transport multiplicity.
    AddExpectedError(TEXT("Procedural articulation Add rejected root"),
        EAutomationExpectedErrorFlags::Contains, 0);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this](UWorld* InWorld)
        {
            const auto Check = [this, InWorld](const TCHAR* InCase, const FTransform& InRootPose,
                const FTransform& InSegmentPose, const FTransform& InTerminalPose,
                const FTransform& InPresentationPose, bool InExpectedValid)
            {
                auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
                auto Root = UCk_Utils_Transform_UE::Add(RootEntity, InRootPose,
                    ECk_Replication::DoesNotReplicate);
                const auto AddPart = [&Root](const FTransform& InPose)
                {
                    auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
                    return UCk_Utils_Transform_UE::Add(Entity, InPose, ECk_Replication::DoesNotReplicate);
                };
                const auto Segment = AddPart(InSegmentPose);
                const auto Terminal = AddPart(InTerminalPose);
                const auto Presentation = AddPart(InPresentationPose);
                auto Binding = FCk_ProceduralLimb_Binding{FName{TEXT("Arm")}, {Segment}};
                Binding.Set_Terminal(Terminal);
                const auto Limb = FCk_ProceduralLimb_Layout{Binding.Get_LimbId(), FTransform::Identity,
                    {100.0f}, FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector};
                auto Spec = FCk_ProceduralArticulation_Spec{
                    FCk_ProceduralArticulation_Layout{{Limb}}, {Binding}};
                Spec.Set_Presentation(Presentation);
                const auto ChildrenBefore = UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Root).Num();
                const auto Subject = UCk_Utils_ProceduralArticulation_UE::Add(Root, Spec);
                TestEqual(FString::Printf(TEXT("%s: composition result"), InCase),
                    ck::IsValid(Subject), InExpectedValid);
                if (NOT InExpectedValid)
                {
                    TestFalse(FString::Printf(TEXT("%s: no immutable runtime params"), InCase),
                        Root.Has<ck::FFragment_ProceduralArticulation_Params>());
                    TestFalse(FString::Printf(TEXT("%s: no runtime bindings"), InCase),
                        Root.Has<ck::FFragment_ProceduralArticulation_Bindings>());
                    TestEqual(FString::Printf(TEXT("%s: no partial limb children"), InCase),
                        UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Root).Num(), ChildrenBefore);
                }
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(RootEntity);
            };
            const auto Identity = FTransform::Identity;
            const auto ZeroScale = FTransform{FQuat::Identity, FVector::ZeroVector, FVector{0.0, 1.0, 1.0}};
            const auto InvalidRotation = FTransform{FQuat{0.0, 0.0, 0.0, 0.0}, FVector::ZeroVector};
            const auto AlmostUnitScale = FTransform{FQuat::Identity, FVector::ZeroVector,
                FVector{1.00001, 1.0, 1.0}};
            Check(TEXT("Root scale outside rigid tolerance"), AlmostUnitScale, Identity, Identity, Identity, false);
            Check(TEXT("Root rotation invalid"), InvalidRotation, Identity, Identity, Identity, false);
            Check(TEXT("Segment scale zero"), Identity, ZeroScale, Identity, Identity, false);
            Check(TEXT("Segment rotation invalid"), Identity, InvalidRotation, Identity, Identity, false);
            Check(TEXT("Terminal scale zero"), Identity, Identity, ZeroScale, Identity, false);
            Check(TEXT("Presentation scale outside rigid tolerance"), Identity, Identity, Identity, AlmostUnitScale, false);
            Check(TEXT("Presentation rotation invalid"), Identity, Identity, Identity, InvalidRotation, false);
            const auto MirroredVisual = FTransform{FQuat::Identity, FVector::ZeroVector, FVector{-2.0, 0.5, 1.5}};
            Check(TEXT("Finite mirrored visual parts remain admissible"),
                Identity, MirroredVisual, MirroredVisual, Identity, true);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
