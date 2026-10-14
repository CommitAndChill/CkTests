#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Control.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Persistence/CkPersistenceHandlerRegistry.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace ck_test_procedural_action_snapshot
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;

    struct FState
    {
        FCk_Handle_Transform OldRoot;
        FCk_Handle_ProceduralActionPlayer OldPlayer;
        FCk_Handle_ProceduralAction OldAction;
        FCk_ProceduralAction_Identity OldIdentity;
        FCk_Handle_Transform NewRoot;
        FCk_Handle_ProceduralArticulation NewSubject;
        FCk_Handle_ProceduralActionPlayer NewPlayer;
    };

    auto BuildBodySubject(UWorld* InWorld, FCk_Handle_Transform& OutRoot)
        -> FCk_Handle_ProceduralArticulation
    {
        auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        OutRoot = UCk_Utils_Transform_UE::Add(RootEntity,
            FTransform{FVector{100.0, 200.0, 2000.0}}, ECk_Replication::DoesNotReplicate);
        auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(OutRoot);
        const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
            FTransform::Identity, ECk_Replication::DoesNotReplicate);
        auto Layout = FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}};
        Layout.Set_BodyEnvelopeMin(FVector{-2.0, -2.0, -2.0});
        Layout.Set_BodyEnvelopeMax(FVector{2.0, 2.0, 2.0});
        auto Spec = FCk_ProceduralArticulation_Spec{Layout,
            TArray<FCk_ProceduralLimb_Binding>{}};
        Spec.Set_Presentation(Presentation);
        return UCk_Utils_ProceduralArticulation_UE::Add(OutRoot, Spec);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionTransientSnapshotPostureTest,
    "Ck.ProceduralAnimation.Action.Snapshot.TransientExecutionAndNeutralReconstruction",
    ck_test_procedural_action_snapshot::TestFlags)

auto
FCkProceduralActionTransientSnapshotPostureTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_snapshot;
    const auto SaveTypes = FCk_PersistenceHandlerRegistry::Get_SaveHandlerTypes();
    for (const auto* Type : SaveTypes)
    {
        if (Type == nullptr) { continue; }
        const auto Name = Type->GetName();
        TestFalse(FString::Printf(TEXT("Action execution type %s is excluded from durable payload"),
            *Name), Name.Contains(TEXT("ProceduralAction"))
                || Name.Contains(TEXT("ProceduralLimbControl"))
                || Name.Contains(TEXT("ProceduralArticulation")));
    }

    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto OldSubject = BuildBodySubject(InWorld, State->OldRoot);
            State->OldPlayer = UCk_Utils_ProceduralActionPlayer_UE::Add(
                OldSubject, FCk_ProceduralActionPlayer_Spec{});
            auto Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->OldRoot);
            auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{}};
            Control.Set_BodyChannel(ECk_EnableDisable::Enable);
            Control.Set_InputTimeout(FCk_Time{30.0});
            State->OldAction = UCk_Utils_ProceduralAction_UE::Create_Manual(
                Scope, State->OldPlayer, Control);
            State->OldIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->OldAction);
            TestTrue(TEXT("Old execution has a real action"), ck::IsValid(State->OldAction));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->OldAction)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->OldAction)
                    == ECk_ProceduralAction_State::Ready;
        }), WaitSeconds, TEXT("Old action acquires body claim")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            TestTrue(TEXT("Old player has a live retained stream"),
                UCk_Utils_ProceduralActionPlayer_UE::Get_RetainedReceiptIdentities(
                    State->OldPlayer).Num() == 1);
            auto OldRoot = State->OldRoot.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(OldRoot);
            State->NewSubject = BuildBodySubject(InWorld, State->NewRoot);
            State->NewPlayer = UCk_Utils_ProceduralActionPlayer_UE::Add(
                State->NewSubject, FCk_ProceduralActionPlayer_Spec{});
            TestTrue(TEXT("Reconstructed topology accepts a neutral player"),
                ck::IsValid(State->NewPlayer));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->NewSubject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->NewSubject) > 0;
        }), WaitSeconds, TEXT("Reconstructed neutral subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestTrue(TEXT("Fresh player reconstructs with no action receipts"),
                UCk_Utils_ProceduralActionPlayer_UE::Get_RetainedReceiptIdentities(
                    State->NewPlayer).IsEmpty());
            const auto Claimant = FCk_ProceduralArticulation_Control::GetClaimant(
                State->NewSubject, NAME_None, true);
            TestEqual(TEXT("Fresh body channel has no inherited lease"),
                Claimant.Get_InstanceId(), uint64{0});
            auto NewRoot = State->NewRoot.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(NewRoot);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
