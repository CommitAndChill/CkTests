#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Control.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Fragment.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationUnclaimedTest,
    "Ck.ProceduralAnimation.Action.Articulation.UnclaimedStateTotality",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkProceduralArticulationUnclaimedTest::
    RunTest(
        const FString&)
    -> bool
{
    struct FState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_Transform Part;
        FCk_Handle_Transform Presentation;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_Transform DetachRoot;
        FCk_Handle_ProceduralLeg DetachedLeg;
    };
    const auto State = MakeShared<FState>();
    auto Revision = TOptional<uint64>{17};
    auto Parts = TArray<FCk_Handle>{FCk_Handle{}};
    TestFalse(TEXT("Default subject rejects without looking up a claimant"),
        FCk_ProceduralArticulation_Control::TryGet_UnclaimedState({}, {}, Revision, Parts));
    TestTrue(TEXT("Failure clears all outputs atomically"), NOT Revision.IsSet() && Parts.IsEmpty());

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(Entity, FTransform{FVector{0.0, 0.0, 2000.0}},
                ECk_Replication::DoesNotReplicate);
            auto Revision = TOptional<uint64>{17};
            auto Parts = TArray<FCk_Handle>{Entity};
            TestTrue(TEXT("Complete optional absence is permitted"),
                FCk_ProceduralArticulation_Control::TryGet_UnclaimedState(Entity, {}, Revision, Parts));
            TestTrue(TEXT("Absence is unset revision, not revision zero"), NOT Revision.IsSet() && Parts.IsEmpty());
            const auto MakePart = [&]()
            {
                auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
                return UCk_Utils_Transform_UE::Add(Child, FTransform::Identity, ECk_Replication::DoesNotReplicate);
            };
            State->Part = MakePart();
            State->Presentation = MakePart();
            auto Limb = FCk_ProceduralLimb_Layout{FName{TEXT("UnclaimedArm")}, FTransform::Identity,
                TArray<float>{100.0f}, FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector};
            auto Spec = FCk_ProceduralArticulation_Spec{
                FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Limb}},
                TArray<FCk_ProceduralLimb_Binding>{FCk_ProceduralLimb_Binding{
                    Limb.Get_Id(), TArray<FCk_Handle_Transform>{State->Part}}}};
            Spec.Set_Presentation(State->Presentation);
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root, Spec);
            TestTrue(TEXT("Public composition creates the complete subject"), ck::IsValid(State->Subject));
            TestFalse(TEXT("Unsettled public composition is not Ready absence"),
                FCk_ProceduralArticulation_Control::TryGet_UnclaimedState(Entity, {}, Revision, Parts));
            TestTrue(TEXT("Pending rejection clears outputs"), NOT Revision.IsSet() && Parts.IsEmpty());
            auto DetachEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->DetachRoot = UCk_Utils_Transform_UE::Add(DetachEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);
            State->DetachedLeg = UCk_Utils_ProceduralLeg_UE::Create(State->DetachRoot,
                FCk_ProceduralLeg_Spec{FName{TEXT("DetachWitness")}, FCk_ProceduralLeg_Placement{},
                    FCk_ProceduralLeg_ChainGeometry{TArray<float>{100.0f}}});
            if (NOT TestTrue(TEXT("Detached-path control starts with a real public leg"), ck::IsValid(State->DetachedLeg)))
            { return; }
            const auto Attached = TArray<FCk_Handle>{State->DetachedLeg.ConvertToHandle()};
            TestTrue(TEXT("Attached owned leg permits optional articulation absence"),
                FCk_ProceduralArticulation_Control::TryGet_UnclaimedState(DetachEntity, Attached, Revision, Parts));
            UCk_Utils_ProceduralLeg_UE::Request_Detach(State->DetachedLeg,
                FCk_Request_ProceduralLeg_Detach{ECk_ProceduralLeg_ReleasedPartsOwnership::KeepBodyOwned}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_Status(State->Subject) == ECk_ProceduralAnimation_Status::Ready
                && ck::IsValid(State->DetachedLeg)
                && UCk_Utils_ProceduralLeg_UE::Get_Status(State->DetachedLeg) == ECk_ProceduralLeg_Status::Detached;
        }), 10.0f, TEXT("Public articulation settles before unclaimed observation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT ck::IsValid(State->Subject)
                || NOT State->Subject.Has<ck::FFragment_ProceduralArticulation_Claims>())
            {
                AddError(TEXT("Public test subject did not survive composition and settlement"));
                return;
            }
            auto Revision = TOptional<uint64>{};
            auto Parts = TArray<FCk_Handle>{};
            const auto Detached = TArray<FCk_Handle>{State->DetachedLeg.ConvertToHandle()};
            TestFalse(TEXT("Public detached leg cannot count as a live required limb even without articulation"),
                FCk_ProceduralArticulation_Control::TryGet_UnclaimedState(
                    State->DetachRoot.ConvertToHandle(), Detached, Revision, Parts));
            TestTrue(TEXT("Detached rejection returns no revision or part subset"), NOT Revision.IsSet() && Parts.IsEmpty());
            const auto Limb = UCk_Utils_ProceduralArticulation_UE::Get_Limb(State->Subject, FName{TEXT("UnclaimedArm")});
            const auto Required = TArray<FCk_Handle>{Limb.ConvertToHandle()};
            const auto Observe = [&]()
            {
                return FCk_ProceduralArticulation_Control::TryGet_UnclaimedState(
                    State->Root.ConvertToHandle(), Required, Revision, Parts);
            };
            const auto Before = State->Subject.Get<ck::FFragment_ProceduralArticulation_Claims>().Get_Revision();
            TestTrue(TEXT("Ready exact unclaimed limb and Body are observed"), Observe());
            TestTrue(TEXT("Observation returns complete bound parts and exact claim revision"),
                Revision.IsSet() && *Revision == Before && Parts.Num() == 2
                    && Parts.Contains(State->Part.ConvertToHandle()) && Parts.Contains(State->Presentation.ConvertToHandle()));
            TestEqual(TEXT("Read-only observation leaves claims revision unchanged"),
                State->Subject.Get<ck::FFragment_ProceduralArticulation_Claims>().Get_Revision(), Before);
            const auto Duplicate = TArray<FCk_Handle>{Required[0], Required[0]};
            TestFalse(TEXT("Duplicate requested identities reject"), FCk_ProceduralArticulation_Control::TryGet_UnclaimedState(
                State->Root.ConvertToHandle(), Duplicate, Revision, Parts));
            const auto Unknown = TArray<FCk_Handle>{State->Part.ConvertToHandle()};
            TestFalse(TEXT("Live owned but unbound part is not a limb channel"),
                FCk_ProceduralArticulation_Control::TryGet_UnclaimedState(
                    State->Root.ConvertToHandle(), Unknown, Revision, Parts));
            auto TooMany = TArray<FCk_Handle>{};
            TooMany.Init(Required[0], 65);
            TestFalse(TEXT("Requested roster bound rejects before claimant indexing"),
                FCk_ProceduralArticulation_Control::TryGet_UnclaimedState(
                    State->Root.ConvertToHandle(), TooMany, Revision, Parts));
            const auto Token = FCk_ArticulationControlToken{7031, 1};
            const auto Channels = FCk_ArticulationChannelSet{TArray<FName>{FName{TEXT("UnclaimedArm")}}, true};
            if (NOT TestEqual(TEXT("Real public reservation takes Body and limb atomically"),
                FCk_ProceduralArticulation_Control::TryReserveClaims(State->Subject, Token, Channels),
                ECk_ArticulationControlResult::Accepted))
            { return; }
            const auto ClaimedRevision = State->Subject.Get<ck::FFragment_ProceduralArticulation_Claims>().Get_Revision();
            TestFalse(TEXT("An actual Body/limb owner rejects new authority"), Observe());
            TestTrue(TEXT("Claim rejection publishes no partial parts or revision"), NOT Revision.IsSet() && Parts.IsEmpty());
            TestEqual(TEXT("Rejected observation does not release or revise the owner"),
                State->Subject.Get<ck::FFragment_ProceduralArticulation_Claims>().Get_Revision(), ClaimedRevision);
            TestTrue(TEXT("Claimant remains the exact original token"),
                FCk_ProceduralArticulation_Control::GetClaimant(State->Subject, NAME_None, true) == Token);
            TestEqual(TEXT("Public release restores unclaimed state"),
                FCk_ProceduralArticulation_Control::ReleaseClaims(State->Subject, Token, Channels),
                ECk_ArticulationControlResult::Accepted);
            TestTrue(TEXT("Released complete structure qualifies again"), Observe());
            const auto LimbOnly = FCk_ArticulationChannelSet{TArray<FName>{FName{TEXT("UnclaimedArm")}}, false};
            TestEqual(TEXT("Public limb-only reservation leaves Body unclaimed"),
                FCk_ProceduralArticulation_Control::TryReserveClaims(State->Subject, Token, LimbOnly),
                ECk_ArticulationControlResult::Accepted);
            TestTrue(TEXT("Body absence alone cannot prove an unclaimed required limb"),
                FCk_ProceduralArticulation_Control::GetClaimant(State->Subject, NAME_None, true) == FCk_ArticulationControlToken{}
                    && NOT Observe());
            TestEqual(TEXT("Limb-only owner releases normally"),
                FCk_ProceduralArticulation_Control::ReleaseClaims(State->Subject, Token, LimbOnly),
                ECk_ArticulationControlResult::Accepted);
            const auto BodyOnly = FCk_ArticulationChannelSet{TArray<FName>{}, true};
            TestEqual(TEXT("Public Body-only claim reserves its channel"),
                FCk_ProceduralArticulation_Control::TryReserveClaims(State->Subject, Token, BodyOnly),
                ECk_ArticulationControlResult::Accepted);
            TestFalse(TEXT("Body ownership rejects even an empty required-limb set"),
                FCk_ProceduralArticulation_Control::TryGet_UnclaimedState(
                    State->Root.ConvertToHandle(), {}, Revision, Parts));
            TestEqual(TEXT("Body-only owner releases normally"),
                FCk_ProceduralArticulation_Control::ReleaseClaims(State->Subject, Token, BodyOnly),
                ECk_ArticulationControlResult::Accepted);
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
            TestFalse(TEXT("Destroy-initiated subject immediately rejects"), Observe());
            TestTrue(TEXT("Stale rejection clears all prior success output"), NOT Revision.IsSet() && Parts.IsEmpty());
            auto DetachRoot = State->DetachRoot.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(DetachRoot);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
