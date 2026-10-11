#include "CkGoapAutoReplanGym_Runtime.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkSelectionGym_BankCensus.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkGoap/Planner/CkGoap_Planner_Utils.h"
#include "CkGoap/WorldState/CkGoap_WorldState_Utils.h"
#include "CkGoap/Action/CkGoap_Action_Utils.h"
#include "CkTimer/CkTimer_Utils.h"
#include "Engine/World.h"

bool UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLiveInWorld(const FCk_Handle& Handle, UWorld* World)
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

bool UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLiveTimer(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) && UCk_Utils_Timer_UE::Has(Handle);
}

bool UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLivePlanner(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) && UCk_Utils_Goap_Planner_UE::Has(Handle);
}

bool UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLiveWorldState(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) && UCk_Utils_Goap_WorldState_UE::Has(Handle);
}

bool UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLiveAction(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) && UCk_Utils_Goap_Action_UE::Has(Handle);
}

bool UCk_GoapAutoReplanGym_Runtime_UE::TryGet_SourceForest(
    UWorld* World, const FCk_Handle& Root, TArray<FCk_Handle>& OutForest)
{
    FString Reason;
    return ck_selection_gym_census::TryCensusAllocatedBank(World, Root, {}, OutForest, Reason);
}

bool UCk_GoapAutoReplanGym_Runtime_UE::Get_IsSameFrame(
    const FCkGoapAutoReplanGym_WireSnapshot& Left, const FCkGoapAutoReplanGym_WireSnapshot& Right)
{
    return FCkGoapAutoReplanGym_WireSnapshot::StaticStruct()->CompareScriptStruct(&Left, &Right, PPF_None);
}
