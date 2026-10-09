#include "CkGym_AdmissionRuntime.h"

#include "CkDynamic/CkDynamic_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "Engine/World.h"

namespace ck_gym_admission_runtime
{
    // Read the root's direct weak-world fragment. Never walk its lifetime-owner
    // chain: malformed/self-owned roots can recurse through Get_WorldForEntity.
    auto HasExactLiveWorldRoot(const FCk_Handle& Root) -> bool
    {
        if (!IsInGameThread()) { return false; }
        if (!ck::IsValid(Root)) { return false; }
        if (UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                Root, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
        { return false; }
        if (!Root.Has<TWeakObjectPtr<UWorld>>()) { return false; }
        auto* World = Root.Get<TWeakObjectPtr<UWorld>>().Get();
        if (!IsValid(World)) { return false; }
        if (!World->IsGameWorld()) { return false; }
        if (World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            World->bIsTearingDown || World->IsBeingCleanedUp())
        { return false; }
        return UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World) == Root;
    }

    auto TryReadSlot(const FCk_Handle& Root) -> const FCkGym_StationAdmission*
    {
        const auto* Type = FCkGym_StationAdmission::StaticStruct();
        // CkDynamic TryGet diagnoses an absent named fragment. Presence is a
        // normal query outcome here and must be checked before calling it.
        if (!UCk_Utils_DynamicFragment_UE::Has_Fragment(Root, Type))
        { return nullptr; }
        const auto* Storage = UCk_Utils_DynamicFragment_UE::
            TryGet_Fragment_TypeUnsafe(Root, Type);
        if (!Storage || Storage->GetScriptStruct() != Type) { return nullptr; }
        return Storage->GetPtr<FCkGym_StationAdmission>();
    }
}

bool UCkGym_AdmissionRuntime_UE::TryGet_IsClosedUnproven(
    const FCk_Handle& InWorldRoot, bool& OutClosed)
{
    OutClosed = true;
    if (!ck_gym_admission_runtime::HasExactLiveWorldRoot(InWorldRoot))
    { return false; }
    if (!UCk_Utils_DynamicFragment_UE::Has_Fragment(
            InWorldRoot, FCkGym_StationAdmission::StaticStruct()))
    { OutClosed = false; return true; }
    const auto* Slot = ck_gym_admission_runtime::TryReadSlot(InWorldRoot);
    if (!Slot) { return false; }
    OutClosed = Slot->ClosedUnproven;
    return true;
}

bool UCkGym_AdmissionRuntime_UE::Get_IsClosedUnproven(
    const FCk_Handle& InWorldRoot)
{
    bool Closed = true;
    return TryGet_IsClosedUnproven(InWorldRoot, Closed) && Closed;
}

bool UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(
    const FCk_Handle& InWorldRoot)
{
    if (!ck_gym_admission_runtime::HasExactLiveWorldRoot(InWorldRoot))
    { return false; }
    const auto* Type = FCkGym_StationAdmission::StaticStruct();
    const bool HasSlot = UCk_Utils_DynamicFragment_UE::
        Has_Fragment(InWorldRoot, Type);
    if (HasSlot)
    {
        // Reject malformed cached payloads before acquiring mutable storage.
        const auto* Existing = ck_gym_admission_runtime::TryReadSlot(InWorldRoot);
        if (!Existing) { return false; }
        if (Existing->ClosedUnproven) { return true; }
    }
    else if (!UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(InWorldRoot))
    { return false; }
    auto MutableRoot = InWorldRoot;
    // AddOrGet bumps CkDynamic's dirty version on existing storage, too.
    auto* Storage = UCk_Utils_DynamicFragment_UE::
        TryAddOrGet_Fragment_TypeUnsafe(MutableRoot, Type);
    if (!Storage || Storage->GetScriptStruct() != Type) { return false; }
    auto* Slot = Storage->GetMutablePtr<FCkGym_StationAdmission>();
    if (!Slot) { return false; }
    Slot->ClosedUnproven = true;
    return true;
}
