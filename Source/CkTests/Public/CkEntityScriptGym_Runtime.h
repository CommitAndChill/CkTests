#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/EntityScript/CkEntityScript.h"

#include <Kismet/BlueprintFunctionLibrary.h>

#include "CkEntityScriptGym_Runtime.generated.h"

// Narrow EntityScript gym barrier query. A default/stale handle returns false.
UCLASS()
class CKTESTS_API UCk_EntityScriptGym_Spawn_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Captures a typed station request carrier synchronously at admission, while
    // the request fragment still exists. False clears OutCarrier.
    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][EntityScriptGym] Try Capture Station Spawn Carrier",
              Category = "Ck|Tests|EntityScriptGym")
    static bool TryCapture_StationSpawnCarrier(
        const FCk_Handle& InTarget, const FCk_Handle& InOwner,
        TSubclassOf<UCk_EntityScript_UE> InExpectedClass,
        const FCk_Handle& InWorld, FCk_Handle& OutCarrier);

    // Same carrier proof for a local cohort owner whose explicitly supplied
    // parent is either the world root or a live direct child of that root.
    // The original API above retains its strict direct-world-owner contract.
    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][EntityScriptGym] Try Capture Station Spawn Carrier With Parent",
              Category = "Ck|Tests|EntityScriptGym")
    static bool TryCapture_StationSpawnCarrierWithParent(
        const FCk_Handle& InTarget, const FCk_Handle& InOwner,
        TSubclassOf<UCk_EntityScript_UE> InExpectedClass,
        const FCk_Handle& InWorld, const FCk_Handle& InOwnerParent,
        FCk_Handle& OutCarrier);

    // After capture the request fragment may have been removed by the processor.
    // Proves allocated carrier ownership and leaf shape without reading that fragment.
    // False clears OutAllocated; true with OutAllocated=false means actually gone.
    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][EntityScriptGym] Try Check Captured Station Carrier",
              Category = "Ck|Tests|EntityScriptGym")
    static bool TryCheck_CapturedStationCarrier(
        const FCk_Handle& InCarrier, const FCk_Handle& InTarget,
        const FCk_Handle& InOwner, const FCk_Handle& InWorld,
        bool& OutAllocated);

    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][EntityScriptGym] Try Check Captured Station Carrier With Parent",
              Category = "Ck|Tests|EntityScriptGym")
    static bool TryCheck_CapturedStationCarrierWithParent(
        const FCk_Handle& InCarrier, const FCk_Handle& InTarget,
        const FCk_Handle& InOwner, const FCk_Handle& InWorld,
        const FCk_Handle& InOwnerParent, bool& OutAllocated);

    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][EntityScriptGym] Is Entity Allocated",
              Category = "Ck|Tests|EntityScriptGym")
    static bool
    Get_IsEntityAllocated(
        const FCk_Handle& InHandle);

    // Total allocated-inclusive direct query of the represented reverse dependent list.
    // Filters by current owner; it is not an independent registry-wide child census.
    // InContext is a represented allocated registry anchor.
    // False clears OutHandles for rejected roots/contexts; true with empty output is a legitimate leaf.
    // Pending-kill roots and children remain observable until actual deallocation. No mutation/recursion.
    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][EntityScriptGym] Try Get Allocated Direct Lifetime Dependents",
              Category = "Ck|Tests|EntityScriptGym")
    static bool
    TryGet_AllocatedDirectLifetimeDependents(
        const FCk_Handle& InRoot,
        const FCk_Handle& InContext,
        TArray<FCk_Handle>& OutHandles);

    // Read-only membership proof of a frozen, allocated-inclusive retirement ledger.
    // Checks current owners independently and raw destroy read-sets in pending phases.
    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][EntityScriptGym] Try Prove Frozen Retirement Ledger",
              Category = "Ck|Tests|EntityScriptGym")
    static bool
    TryProve_FrozenRetirementLedger(
        const TArray<FCk_Handle>& InKnownAllocated,
        const TArray<FCk_Handle>& InRetiring,
        const FCk_Handle& InContext);
};
