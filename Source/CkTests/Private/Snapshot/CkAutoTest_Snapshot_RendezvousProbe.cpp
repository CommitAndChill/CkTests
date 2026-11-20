#include "CkTests/Snapshot/CkAutoTest_Snapshot_RendezvousProbe.h"

#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"
#include "CkEntityTag/CkEntityTag_Fragment_Data.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkLabel/CkLabel_Utils.h"
#include <NativeGameplayTags.h>
#endif

namespace
{
    void ObserveAndSeed(FCk_Handle& InEntity, const ck::persistence::FInitializationIdentity& InIdentity)
    {
        auto Status = ck::persistence::RequestInitialization(InEntity, FCk_SaveData_EntityTags::StaticStruct(), InIdentity);
        InEntity.AddOrGet<ck::FFragment_AutoTest_RendezvousObservation>().DuringConstruction = Status;
        UCk_Utils_EntityTag_UE::Add(InEntity, FName{TEXT("Rendezvous.ConstructSeed")});
    }

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    UE_DEFINE_GAMEPLAY_TAG_STATIC(BridgeChild0, "Ck.AutoTest.Rendezvous.BridgeChild0");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(BridgeChild1, "Ck.AutoTest.Rendezvous.BridgeChild1");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(BridgeChild2, "Ck.AutoTest.Rendezvous.BridgeChild2");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(BridgeChild3, "Ck.AutoTest.Rendezvous.BridgeChild3");

    void ConstructSlotChildren(FCk_Handle& InOwner, int32 InSlot)
    {
        const FGameplayTag Labels[] = {BridgeChild0, BridgeChild1, BridgeChild2, BridgeChild3};
        if (InSlot < 0 || InSlot >= UE_ARRAY_COUNT(Labels)) { return; }

        const auto ConstructChild = [&](FGameplayTag InLabel)
        {
            auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InOwner);
            UCk_Utils_GameplayLabel_UE::Add(Child, InLabel);
            ObserveAndSeed(Child, {});
        };

        // At capture, each owner has a different labeled child. Ask again synchronously while
        // the rebuilt owner's Construct is still on the stack and its bridge row is pending.
        ConstructChild(Labels[InSlot]);
        if (InOwner.Get<ck::FFragment_AutoTest_RendezvousObservation>().DuringConstruction.Get_Epoch() != 0)
        {
            ConstructChild(Labels[(InSlot + 1) % UE_ARRAY_COUNT(Labels)]);
        }
    }
#endif
}

auto UCk_AutoTest_Snapshot_BridgeRendezvous_UE::Construct(
    FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);
    auto Identity = ck::persistence::FInitializationIdentity{};
    if (const auto* Pawn = Cast<APawn>(UCk_Utils_EntityBridge_UE::TryGet_Actor(InHandle)))
    {
        Identity._Kind = ck::persistence::EInitializationIdentity::Player;
        const auto* Player = Pawn->GetPlayerState();
        if (Player && Player->GetUniqueId().IsValid())
        { Identity._PlayerId = Player->GetUniqueId().ToString(); }
    }
    ObserveAndSeed(InHandle, Identity);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    if (const auto* Actor = Cast<ACk_AutoTest_Snapshot_BridgeRendezvousActor_UE>(
        UCk_Utils_EntityBridge_UE::TryGet_Actor(InHandle)))
    { ConstructSlotChildren(InHandle, Actor->RestoreProbeSlot); }
#endif
    auto Pending = FCk_Handle_PendingEntityScript{InHandle};
    auto Delegate = FCk_Delegate_EntityScript_Constructed{};
    Delegate.BindDynamic(this, &ThisClass::OnProbeConstructed);
    UCk_Utils_PendingEntityScript_UE::Promise_OnConstructed(Pending, Delegate);
    return Flow;
}

void UCk_AutoTest_Snapshot_BridgeRendezvous_UE::OnProbeConstructed(FCk_Handle_EntityScript InEntity)
{
    auto& Observation = InEntity.Get<ck::FFragment_AutoTest_RendezvousObservation>();
    Observation.OnConstructed = ck::persistence::Query(InEntity, FCk_SaveData_EntityTags::StaticStruct());
    Observation.ConstructedCallbackRan = true;
}

auto UCk_AutoTest_Snapshot_BridgeRendezvous_UE::Get_IsSnapshotRespawnable() const -> bool { return true; }
auto UCk_AutoTest_Snapshot_PlayerRendezvous_UE::Get_IsSnapshotRespawnable() const -> bool { return false; }

ACk_AutoTest_Snapshot_BridgeRendezvousActor_UE::ACk_AutoTest_Snapshot_BridgeRendezvousActor_UE()
{
    bReplicates = false;
    _EntityScriptClass = UCk_AutoTest_Snapshot_BridgeRendezvous_UE::StaticClass();
}

auto UCk_AutoTest_Snapshot_DefinitionRendezvous_UE::DoConstruct_Implementation(FCk_Handle& InHandle) const -> void
{
    ObserveAndSeed(InHandle, {});
    InHandle.Add<ck::FTag_AutoTest_DefinitionProbe>();
}
