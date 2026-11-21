#include "CkTests/Net/CkAutoTest_NetSubject_LifetimeTransfer.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"

#include <StructUtils/InstancedStruct.h>

// --------------------------------------------------------------------------------------------------------------------

namespace ck_auto_test_lifetime_transfer_subject
{
    template <typename T_RoleTag>
    auto
        Add_MemberTags(
            FCk_Handle& InHandle)
        -> void
    {
        InHandle.Add<ck::auto_test::lifetime_transfer::FTag_LifetimeTransfer_Member>();
        InHandle.Add<T_RoleTag>();
    }

    auto
        Request_SpawnOnHost(
            FCk_Handle& InOwner,
            TSubclassOf<UCk_EntityScript_UE> InEntityScriptClass)
        -> void
    {
        if (NOT UCk_Utils_Net_UE::Get_IsEntityNetMode_Host(InOwner))
        { return; }

        UCk_Utils_EntityScript_UE::Request_SpawnEntity(InOwner, InEntityScriptClass, FInstancedStruct{}, {});
    }
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_AutoTest_LifetimeTransfer_DefinitionChild_ConstructionScript_UE::
    DoConstruct_Implementation(
        FCk_Handle& InHandle) const
    -> void
{
    ck_auto_test_lifetime_transfer_subject::Add_MemberTags<ck::auto_test::lifetime_transfer::FTag_LifetimeTransfer_DefinitionChild>(InHandle);
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_AutoTest_LifetimeTransfer_Child_EntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);
    ck_auto_test_lifetime_transfer_subject::Add_MemberTags<ck::auto_test::lifetime_transfer::FTag_LifetimeTransfer_Child>(InHandle);
    ++InHandle.AddOrGet<ck::auto_test::lifetime_transfer::FFragment_LifetimeTransfer_ConstructCount>()._Count;
    return Flow;
}

auto
    UCk_AutoTest_LifetimeTransfer_ChildOfMid_EntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);
    ck_auto_test_lifetime_transfer_subject::Add_MemberTags<ck::auto_test::lifetime_transfer::FTag_LifetimeTransfer_ChildOfMid>(InHandle);
    return Flow;
}

auto
    UCk_AutoTest_LifetimeTransfer_OwnerA_EntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);
    ck_auto_test_lifetime_transfer_subject::Add_MemberTags<ck::auto_test::lifetime_transfer::FTag_LifetimeTransfer_OwnerA>(InHandle);
    ck_auto_test_lifetime_transfer_subject::Request_SpawnOnHost(InHandle, UCk_AutoTest_LifetimeTransfer_Child_EntityScript_UE::StaticClass());

    // The inventory-item path: built synchronously, and only on the host (the call is a no-op elsewhere).
    if (UCk_Utils_Net_UE::Get_IsEntityNetMode_Host(InHandle))
    {
        UCk_Utils_EntityReplicationDriver_UE::Request_BuildAndReplicate(InHandle,
            FCk_EntityReplicationDriver_Spec{UCk_AutoTest_LifetimeTransfer_DefinitionChild_ConstructionScript_UE::StaticClass()});
    }

    return Flow;
}

auto
    UCk_AutoTest_LifetimeTransfer_OwnerB_EntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);
    ck_auto_test_lifetime_transfer_subject::Add_MemberTags<ck::auto_test::lifetime_transfer::FTag_LifetimeTransfer_OwnerB>(InHandle);

    // The inventory shape: a plain child built locally on every machine, with no replication driver of its own.
    auto Holder = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InHandle);
    Holder.Add<ck::auto_test::lifetime_transfer::FTag_LifetimeTransfer_Holder>();

    return Flow;
}

auto
    UCk_AutoTest_LifetimeTransfer_MidOwner_EntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);
    ck_auto_test_lifetime_transfer_subject::Add_MemberTags<ck::auto_test::lifetime_transfer::FTag_LifetimeTransfer_MidOwner>(InHandle);
    ck_auto_test_lifetime_transfer_subject::Request_SpawnOnHost(InHandle, UCk_AutoTest_LifetimeTransfer_ChildOfMid_EntityScript_UE::StaticClass());
    return Flow;
}

auto
    UCk_AutoTest_LifetimeTransfer_GrandOwner_EntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);
    ck_auto_test_lifetime_transfer_subject::Add_MemberTags<ck::auto_test::lifetime_transfer::FTag_LifetimeTransfer_GrandOwner>(InHandle);
    ck_auto_test_lifetime_transfer_subject::Request_SpawnOnHost(InHandle, UCk_AutoTest_LifetimeTransfer_MidOwner_EntityScript_UE::StaticClass());
    return Flow;
}

auto
    UCk_AutoTest_LifetimeTransfer_Root_EntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    // Super first: it links the bridge, which every child driver walks up to for its Outer actor.
    const auto Flow = Super::Construct(InHandle, InSpawnParams);
    InHandle.Add<ck::auto_test::lifetime_transfer::FTag_LifetimeTransfer_Root>();

    ck_auto_test_lifetime_transfer_subject::Request_SpawnOnHost(InHandle, UCk_AutoTest_LifetimeTransfer_OwnerA_EntityScript_UE::StaticClass());
    ck_auto_test_lifetime_transfer_subject::Request_SpawnOnHost(InHandle, UCk_AutoTest_LifetimeTransfer_OwnerB_EntityScript_UE::StaticClass());
    ck_auto_test_lifetime_transfer_subject::Request_SpawnOnHost(InHandle, UCk_AutoTest_LifetimeTransfer_GrandOwner_EntityScript_UE::StaticClass());

    return Flow;
}

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_NetSubject_LifetimeTransfer_UE::
    ACk_AutoTest_NetSubject_LifetimeTransfer_UE()
{
    _EntityScriptClass = UCk_AutoTest_LifetimeTransfer_Root_EntityScript_UE::StaticClass();
}

// --------------------------------------------------------------------------------------------------------------------
