#include "CkEqsGym_Runtime.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkSelectionGym_BankCensus.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEqs/Query/CkEqs_Utils.h"
#include "CkTimer/CkTimer_Utils.h"
#include "Engine/World.h"

bool UCk_EqsGym_Runtime_UE::Get_IsLiveInWorld(const FCk_Handle& Handle, UWorld* World)
{
    if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() ||
        World->bIsTearingDown || World->IsBeingCleanedUp() ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle) || !ck::IsValid(Handle))
    { return false; }
    if (Handle.Has_Any<ck::FTag_DestroyEntity_Initiate, ck::FTag_DestroyEntity_EndPlay,
        ck::FTag_DestroyEntity_Teardown, ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>())
    { return false; }
    const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(WorldRoot) ||
        !ck::IsValid(WorldRoot) || !WorldRoot.Has<TWeakObjectPtr<UWorld>>())
    { return false; }
    return WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get() == World &&
        WorldRoot.Get_RegistryView().Get_RegistryHandle() == Handle.Get_RegistryView().Get_RegistryHandle();
}

bool UCk_EqsGym_Runtime_UE::Get_IsLiveTimer(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) && UCk_Utils_Timer_UE::Has(Handle);
}

bool UCk_EqsGym_Runtime_UE::Get_IsLiveQuery(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) && UCk_Utils_Eqs_UE::Has(Handle);
}

bool UCk_EqsGym_Runtime_UE::Get_IsQueryOwnedBy(
    const FCk_Handle& Query, const FCk_Handle& Root, UWorld* World)
{
    if (!Get_IsLiveQuery(Query, World) || !Get_IsLiveInWorld(Root, World) ||
        !Query.Has_All<ck::FFragment_LifetimeOwner, ck::FFragment_ContextOwner>())
    { return false; }
    return Query.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == Root &&
        Query.Get<ck::FFragment_ContextOwner>().Get_Entity() == Root;
}

bool UCk_EqsGym_Runtime_UE::TryGet_SourceForest(
    UWorld* World, const FCk_Handle& Root, TArray<FCk_Handle>& OutForest)
{
    FString Reason;
    return ck_selection_gym_census::TryCensusAllocatedBank(World, Root, {}, OutForest, Reason);
}

bool UCk_EqsGym_Runtime_UE::Get_IsSameFrame(
    const FCkEqsGym_WireSnapshot& Left, const FCkEqsGym_WireSnapshot& Right)
{
    return FCkEqsGym_WireSnapshot::StaticStruct()->CompareScriptStruct(&Left, &Right, PPF_None);
}
