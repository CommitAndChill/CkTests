#include "CkDialogGym_Runtime.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkSelectionGym_BankCensus.h"
#include "CkDialog/Emitter/CkDialogEmitter_Utils.h"
#include "CkDialog/Line/CkDialogLine_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkTimer/CkTimer_Utils.h"
#include "Engine/World.h"

bool UCk_DialogGym_Runtime_UE::Get_IsLiveInWorld(const FCk_Handle& Handle, UWorld* World)
{
    if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() ||
        World->bIsTearingDown || World->IsBeingCleanedUp() ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle) ||
        !ck::IsValid(Handle))
    { return false; }
    if (Handle.Has_Any<ck::FTag_DestroyEntity_Initiate, ck::FTag_DestroyEntity_EndPlay,
        ck::FTag_DestroyEntity_Teardown, ck::FTag_DestroyEntity_Await,
        ck::FTag_DestroyEntity_Finalize>())
    { return false; }
    const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(WorldRoot) ||
        !ck::IsValid(WorldRoot) || !WorldRoot.Has<TWeakObjectPtr<UWorld>>())
    { return false; }
    return WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get() == World &&
        WorldRoot.Get_RegistryView().Get_RegistryHandle() ==
            Handle.Get_RegistryView().Get_RegistryHandle();
}

bool UCk_DialogGym_Runtime_UE::Get_IsLiveEmitter(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) && UCk_Utils_DialogEmitter_UE::Has(Handle);
}

bool UCk_DialogGym_Runtime_UE::Get_IsLiveTimer(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) && UCk_Utils_Timer_UE::Has(Handle);
}

bool UCk_DialogGym_Runtime_UE::Get_IsLiveLine(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) && UCk_Utils_DialogLine_UE::Has(Handle);
}

bool UCk_DialogGym_Runtime_UE::TryGet_SourceForest(UWorld* World,
    const FCk_Handle& Root, TArray<FCk_Handle>& OutForest)
{
    FString Reason;
    return ck_selection_gym_census::TryCensusAllocatedBank(World, Root, {}, OutForest, Reason);
}

bool UCk_DialogGym_Runtime_UE::Get_IsSameFrame(const FCkDialogGym_WireSnapshot& Left,
    const FCkDialogGym_WireSnapshot& Right)
{
    return FCkDialogGym_WireSnapshot::StaticStruct()->CompareScriptStruct(&Left, &Right, PPF_None);
}
