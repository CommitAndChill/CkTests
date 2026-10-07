#include "CkSceneNode_EcsRootsTestAccess.h"

#include "CkSceneNode_ActorLedgerProbe.h"
#include "CkSceneNodeGym_ActorLedger.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Fragment_Data.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Registry/CkRegistry.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor_Data.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

namespace ck_scene_ecs_roots_test
{
    constexpr int32 ActorRoles[6] = {1, 3, 5, 10, 13, 16};

    struct FState
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AGameStateBase> Source;
        TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
        TWeakObjectPtr<AActor> Scope;
        TWeakObjectPtr<UCk_SceneNodeGym_EcsRoots_UE> Receipt;
        TArray<TWeakObjectPtr<ACk_SceneNode_ActorLedgerProbe>> Visuals;
        FCk_Handle WorldRoot;
        FCk_Handle GenerationOwner;
        TArray<FCk_SceneNodeEcsRoots_TestAccess::FSlot> FrozenSlots;
        bool bCaptured = false;
    };

    TArray<TSubclassOf<AActor>> MakeClasses()
    {
        TArray<TSubclassOf<AActor>> Classes;
        Classes.Init(ACk_SceneNode_ActorLedgerProbe::StaticClass(), 17);
        return Classes;
    }

    int32 Population(const FCk_Handle& Root)
    {
        if (!ck::IsValid(Root)) { return -1; }
        int32 Count = 0;
        Root.Get_RegistryView().View<ck::FTag_EntityJustCreated>().ForEach(
            [&Count](FCk_Entity) { ++Count; });
        return Count;
    }

    bool SameFrozen(const FState& State)
    {
        FCk_Handle World, Owner;
        TArray<FCk_SceneNodeEcsRoots_TestAccess::FSlot> Slots;
        if (!FCk_SceneNodeEcsRoots_TestAccess::TryCapture(
            State.Receipt.Get(), World, Owner, Slots) ||
            World != State.WorldRoot || Owner != State.GenerationOwner ||
            Slots.Num() != State.FrozenSlots.Num())
        { return false; }
        for (int32 I = 0; I < Slots.Num(); ++I)
        {
            if (Slots[I].Target != State.FrozenSlots[I].Target ||
                Slots[I].Carrier != State.FrozenSlots[I].Carrier ||
                Slots[I].bAllocationAttempted != State.FrozenSlots[I].bAllocationAttempted ||
                Slots[I].bIssued != State.FrozenSlots[I].bIssued ||
                Slots[I].bReturned != State.FrozenSlots[I].bReturned)
            { return false; }
        }
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeEcsRoots,
    "Ck.SceneNode.Network.EcsRoots.Construction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkSceneNodeEcsRoots::RunTest(const FString& Parameters)
{
    using namespace ck_scene_ecs_roots_test;
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]()
    {
        const auto* Server = ck::auto_test::net::Get_ServerWorld();
        const auto* Client = ck::auto_test::net::Get_ClientWorld(0);
        return IsValid(Server) && IsValid(Client) &&
            IsValid(Server->GetAuthGameMode()) &&
            IsValid(Server->GetGameState()) &&
            Server->GetGameState()->HasActorBegunPlay() &&
            IsValid(Client->GetGameState());
    }), 15.0, TEXT("real authority and client GameStates reach BeginPlay")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
    {
        const auto Fail = [this](const TCHAR* Reason)
        { AddError(Reason); ck::auto_test::net::FailSession(Reason); };
        auto* Client = ck::auto_test::net::Get_ClientWorld(0);
        if (!IsValid(World) || !IsValid(Client) ||
            !IsValid(World->GetGameState()) || !IsValid(Client->GetGameState()))
        { Fail(TEXT("EcsRoots requires real ready authority and client worlds")); return; }
        State->World = World;
        State->Source = World->GetGameState();
        const auto* ProbeCDO = ACk_SceneNode_ActorLedgerProbe::StaticClass()
            ->GetDefaultObject<ACk_SceneNode_ActorLedgerProbe>();
        const auto* ScriptCDO = UCk_SceneNodeGym_WithActorScript_UE::StaticClass()
            ->GetDefaultObject<UCk_SceneNodeGym_WithActorScript_UE>();
        const auto* ScopeCDO = AActor::StaticClass()->GetDefaultObject<AActor>();
        if (!IsValid(ProbeCDO) || !IsValid(ProbeCDO->Cube) ||
            !IsValid(ProbeCDO->Cube->GetStaticMesh()) ||
            !IsValid(ScriptCDO) || !IsValid(ScopeCDO) ||
            ProbeCDO->GetIsReplicated() || ScopeCDO->GetIsReplicated() ||
            ScriptCDO->Get_EffectiveReplication() != ECk_Replication::DoesNotReplicate)
        { Fail(TEXT("EcsRoots requires materialized nonreplicated native class CDOs")); return; }
        State->WorldRoot =
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!ck::IsValid(State->WorldRoot))
        { Fail(TEXT("authority ECS transient world root unavailable")); return; }
        const auto Classes = MakeClasses();
        const int32 BeforeInvalid = Population(State->WorldRoot);
        const auto* ClientLedger =
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
                Client->GetGameState(), 1, Classes);
        const auto* DefaultReceipt =
            UCk_SceneNodeGym_EcsRootsRuntime_UE::TryBegin(nullptr,
                UCk_SceneNodeGym_WithActorScript_UE::StaticClass());
        if (ClientLedger != nullptr || DefaultReceipt != nullptr ||
            Population(State->WorldRoot) != BeforeInvalid)
        { Fail(TEXT("foreign client source and default ledger reject without ECS allocation")); return; }
        auto* Ledger = UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
            World->GetGameState(), 1, Classes);
        if (!IsValid(Ledger) ||
            !World->GetGameState()->GetInstanceComponents().Contains(Ledger))
        { Fail(TEXT("real authority ActorLedger claim failed")); return; }
        State->Ledger = Ledger;
        AActor* Scope = nullptr;
        if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
            Ledger, 0, FTransform::Identity, Scope) || !IsValid(Scope) ||
            Scope->GetOwner() != World->GetGameState())
        { Fail(TEXT("real role-zero actor scope did not finish")); return; }
        State->Scope = Scope;
        const int32 BeforePartial = Population(State->WorldRoot);
        if (UCk_SceneNodeGym_EcsRootsRuntime_UE::TryBegin(
                Ledger, UCk_SceneNodeGym_WithActorScript_UE::StaticClass()) != nullptr ||
            Population(State->WorldRoot) != BeforePartial ||
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) != 1)
        { Fail(TEXT("incomplete actual actor cohort must reject before ECS allocation or claim")); return; }
        for (int32 Role = 1; Role <= 17; ++Role)
        {
            AActor* Actor = nullptr;
            if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
                Ledger, Role, FTransform(FVector(Role * 10.0, 0.0, 0.0)), Actor) ||
                !IsValid(Actor) || Actor->GetOwner() != Scope ||
                Actor->GetClass() != ACk_SceneNode_ActorLedgerProbe::StaticClass())
            { Fail(TEXT("one of seventeen real configured visual actors failed")); return; }
            auto* Probe = Cast<ACk_SceneNode_ActorLedgerProbe>(Actor);
            if (!IsValid(Probe) || Probe->BeginPlayCount != 1 ||
                !Probe->bObservedNativeClaimAtBeginPlay ||
                !IsValid(Probe->Cube) || !Probe->Cube->IsRegistered())
            { Fail(TEXT("configured visual actor did not observe its native claim at BeginPlay")); return; }
            State->Visuals.Add(Probe);
        }
        if (State->Visuals.Num() != 17 ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(Ledger, true))
        { Fail(TEXT("real eighteen-actor cohort is not exact")); return; }
        // A public, test-created actor-link component is an actual conflicting
        // construction input. Remove that exact unlinked component before the
        // healthy attempt; its default ECS handle makes EndPlay a no-op.
        auto* LinkedActor = State->Visuals[ActorRoles[0] - 1].Get();
        auto* ExistingLink = IsValid(LinkedActor)
            ? NewObject<UCk_EntityOwningActor_ActorComponent_UE>(LinkedActor) : nullptr;
        if (!IsValid(ExistingLink))
        { Fail(TEXT("could not create test-owned preexisting actor-link component")); return; }
        LinkedActor->AddInstanceComponent(ExistingLink);
        ExistingLink->RegisterComponent();
        const int32 BeforeLink = Population(State->WorldRoot);
        const bool LinkRejected =
            LinkedActor->GetComponentByClass<UCk_EntityOwningActor_ActorComponent_UE>() == ExistingLink &&
            ExistingLink->Get_EntityHandle() == FCk_Handle{} &&
            UCk_SceneNodeGym_EcsRootsRuntime_UE::TryBegin(
                Ledger, UCk_SceneNodeGym_WithActorScript_UE::StaticClass()) == nullptr &&
            Population(State->WorldRoot) == BeforeLink &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger);
        ExistingLink->DestroyComponent();
        if (!LinkRejected ||
            LinkedActor->GetComponentByClass<UCk_EntityOwningActor_ActorComponent_UE>() != nullptr)
        { Fail(TEXT("preexisting actor link did not reject cleanly before allocation")); return; }
        const int32 BeforeDefaultClass = Population(State->WorldRoot);
        if (UCk_SceneNodeGym_EcsRootsRuntime_UE::TryBegin(Ledger, {}) != nullptr ||
            Population(State->WorldRoot) != BeforeDefaultClass ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger))
        { Fail(TEXT("default script class rejects without claiming a healthy complete cohort")); return; }

        auto* Receipt = UCk_SceneNodeGym_EcsRootsRuntime_UE::TryBegin(
            Ledger, UCk_SceneNodeGym_WithActorScript_UE::StaticClass());
        State->Receipt = Receipt;
        TArray<FCk_SceneNodeEcsRoots_TestAccess::FSlot> Slots;
        FCk_Handle RecordedWorld, GenerationOwner;
        if (!IsValid(Receipt) ||
            !FCk_SceneNodeEcsRoots_TestAccess::TryCapture(
                Receipt, RecordedWorld, GenerationOwner, Slots) ||
            RecordedWorld != State->WorldRoot || Slots.Num() != 6 ||
            !ck::IsValid(GenerationOwner) ||
            !GenerationOwner.Has<ck::FFragment_LifetimeOwner>() ||
            GenerationOwner.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != State->WorldRoot ||
            Population(State->WorldRoot) != BeforeDefaultClass + 13)
        { Fail(TEXT("EcsRoots failed to retain exactly thirteen real allocator returns")); return; }
        State->GenerationOwner = GenerationOwner;
        State->FrozenSlots = Slots;
        if (UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(GenerationOwner).Num() != 6 ||
            UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(Receipt) ||
            UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_SucceededCount(Receipt) != 0 ||
            UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsFailed(Receipt) ||
            UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsUnproven(Receipt))
        { Fail(TEXT("new EcsRoots receipt is not an exact pending six-request record")); return; }
        for (int32 I = 0; I < 6; ++I)
        {
            const auto& Slot = Slots[I];
            auto* Actor = State->Visuals[ActorRoles[I] - 1].Get();
            if (!IsValid(Actor) ||
                !Slot.bIssued || !Slot.bReturned || !Slot.bAllocationAttempted ||
                Slot.bCallback || Slot.bSucceeded ||
                !ck::IsValid(Slot.Target) || !ck::IsValid(Slot.Carrier) ||
                Slot.Pending.Get_EntityUnderConstruction() != Slot.Target ||
                !Slot.Target.Has<ck::FFragment_LifetimeOwner>() ||
                Slot.Target.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != GenerationOwner ||
                !Slot.Carrier.Has<ck::FFragment_LifetimeOwner>() ||
                Slot.Carrier.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Slot.Target ||
                !Slot.Carrier.Has<ck::FFragment_EntityScript_RequestSpawnEntity>() ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Slot.Target).Num() != 1 ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Slot.Target)[0] != Slot.Carrier)
            { Fail(TEXT("one root/carrier identity, direct parent or queued callback state is wrong")); return; }
            const auto& Request = Slot.Carrier.Get<ck::FFragment_EntityScript_RequestSpawnEntity>();
            const auto* Payload = Request.Get_SpawnParams()
                .GetPtr<FCk_EntityScript_WithActor_SpawnParams>();
            if (Request.Get_NewEntity() != Slot.Target ||
                Request.Get_Owner() != GenerationOwner ||
                Request.Get_ContextOwner() != GenerationOwner ||
                Request.Get_EntityScriptClassArchetype().Get() != ScriptCDO ||
                Request.Get_DestroyTargetOnFailure() || !Payload ||
                Payload->Get_OwningActor().Get() != Actor ||
                Slot.Target.Has<ck::FFragment_EntityScript>() ||
                !Slot.Target.Has<ck::FFragment_Net_Params>() ||
                Slot.Target.Get<ck::FFragment_Net_Params>()
                    .Get_ConnectionSettings().Get_Replication() !=
                    ECk_Replication::DoesNotReplicate ||
                UCk_Utils_EntityReplicationDriver_UE::Has(Slot.Target))
            { Fail(TEXT("one queued WithActor request payload or source-only boundary is wrong")); return; }
        }
        const int32 BeforeSecond = Population(State->WorldRoot);
        if (UCk_SceneNodeGym_EcsRootsRuntime_UE::TryBegin(
                Ledger, UCk_SceneNodeGym_WithActorScript_UE::StaticClass()) != nullptr ||
            Population(State->WorldRoot) != BeforeSecond || !SameFrozen(*State))
        { Fail(TEXT("second EcsRoots begin must not allocate or replace captured identities")); return; }
        State->bCaptured = true;
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
        const auto* Receipt = State->Receipt.Get();
        if (!IsValid(Receipt)) { return false; }
        return UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(Receipt) &&
            UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_SucceededCount(Receipt) == 6;
    }), 20.0, TEXT("six real queued WithActor requests drain into exact actor-linked roots")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
    {
        if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
        const auto Fail = [this](const TCHAR* Reason)
        { AddError(Reason); ck::auto_test::net::FailSession(Reason); };
        auto* Receipt = State->Receipt.Get();
        auto* Ledger = State->Ledger.Get();
        if (!TestTrue(TEXT("EcsRoots real drain retains its source context"),
            State->bCaptured && IsValid(World) && World == State->World.Get() &&
            IsValid(Receipt) && IsValid(Ledger) && SameFrozen(*State) &&
            ck::IsValid(State->GenerationOwner) &&
            UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(Receipt) &&
            !UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsFailed(Receipt) &&
            !UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsUnproven(Receipt)))
        { Fail(TEXT("EcsRoots lost its exact source context after real request drain")); return; }
        for (int32 I = 0; I < 6; ++I)
        {
            const auto& Slot = State->FrozenSlots[I];
            auto* Actor = State->Visuals[ActorRoles[I] - 1].Get();
            FCk_Handle Completed;
            auto* Script = ck::IsValid(Slot.Target)
                ? Cast<UCk_SceneNodeGym_WithActorScript_UE>(
                    UCk_Utils_EntityScript_UE::TryGet_EntityScript(Slot.Target))
                : nullptr;
            if (!IsValid(Actor) || !IsValid(Script) ||
                !UCk_SceneNodeGym_EcsRootsRuntime_UE::TryGet_CompletedRoot(
                    Receipt, I, Completed) || Completed != Slot.Target ||
                !ck::IsValid(Slot.Target, ck::IsValid_Policy_IncludePendingKill{}) ||
                ck::IsValid(Slot.Carrier, ck::IsValid_Policy_IncludePendingKill{}) ||
                Script->GetClass() != UCk_SceneNodeGym_WithActorScript_UE::StaticClass() ||
                Script->Get_AssociatedEntity() != Slot.Target ||
                Script->Get_OwningActor().Get() != Actor ||
                Script->Get_EffectiveReplication() != ECk_Replication::DoesNotReplicate ||
                !UCk_Utils_OwningActor_UE::Has(Slot.Target) ||
                UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor) != Slot.Target ||
                !Slot.Target.Has<ck::FFragment_Transform>() ||
                !Slot.Target.Has<ck::FFragment_Transform_RootComponent>() ||
                Slot.Target.Get<ck::FFragment_Transform_RootComponent>()
                    .Get_RootComponent().Get() != Actor->GetRootComponent() ||
                UCk_Utils_EntityReplicationDriver_UE::Has(Slot.Target))
            { Fail(*FString::Printf(TEXT("WithActor root %d missed a real binding or exact retained carrier retirement"), I)); return; }
        }
        // A known actor loss invalidates current construction proof. Its own
        // EntityOwningActor component may independently retire the linked ECS
        // root in EndPlay; the raw creator identity must still be retained.
        auto* LostActor = State->Visuals[ActorRoles[0] - 1].Get();
        if (!IsValid(LostActor) || !LostActor->Destroy())
        { Fail(TEXT("known visual actor loss could not be exercised")); return; }
        if (!SameFrozen(*State) ||
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) ||
            UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(Receipt))
        { Fail(TEXT("known actor loss did not close actor admission and invalidate current EcsRoots proof")); return; }
        for (int32 I = 1; I < 6; ++I)
        {
            if (!ck::IsValid(State->FrozenSlots[I].Target,
                    ck::IsValid_Policy_IncludePendingKill{}))
            { Fail(TEXT("unrelated actor-linked root was lost after one known actor destruction")); return; }
        }
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
