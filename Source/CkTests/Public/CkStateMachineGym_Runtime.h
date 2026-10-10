#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment_Data.h"

#include <Kismet/BlueprintFunctionLibrary.h>

#include "CkStateMachineGym_Runtime.generated.h"

// Checked context lookup for StateMachine gym callbacks. In contrast to
// FCk_Handle::Get_ContextOwner, this accepts malformed live handles without
// reading an absent ContextOwner fragment. Success proves a live same-current-
// game-world context in the callback's bounded lifetime-owner ancestry;
// failure clears OutContext, including aliased input/output arguments.
class AActor;
class UCk_EntityScript_UE;
UCLASS()
class CKTESTS_API UCk_StateMachineGym_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][StateMachineGym] Try Get Source Context",
              Category = "Ck|Tests|StateMachineGym")
    static bool TryGet_SourceContext(
        const FCk_Handle& InCallback, FCk_Handle& OutContext);

    // Resolve the immediate child beneath an exact, current primary context.
    // The actor must still compare this value to its retained A/B receipts.
    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][StateMachineGym] Try Get Direct Pass Child",
              Category = "Ck|Tests|StateMachineGym")
    static bool TryGet_DirectPassChild(
        const FCk_Handle& InCallback, const FCk_Handle& InPrimary,
        FCk_Handle& OutPassChild);

    // Checks the exact authored class and its already-existing CDO. Never creates a CDO.
    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][StateMachineGym] Is Divergence Source Class DNR",
              Category = "Ck|Tests|StateMachineGym")
    static bool Get_IsDivergenceSourceClassNonReplicating(
        TSubclassOf<UCk_EntityScript_UE> InClass, bool bPrimary);

    // Read-only constructed-receipt checks. No HasBegunPlay requirement:
    // Promise_OnConstructed may fire before BeginPlay.
    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][StateMachineGym] Try Validate Divergence Primary",
              Category = "Ck|Tests|StateMachineGym")
    static bool TryValidate_DivergencePrimary(
        const FCk_Handle& InPrimary, AActor* InActor);

    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][StateMachineGym] Try Validate Divergence Pass",
              Category = "Ck|Tests|StateMachineGym")
    static bool TryValidate_DivergencePass(
        const FCk_Handle& InPass, const FCk_Handle& InPrimary);
    // Cancellation accepts only this closed actor's exact unconstructed request.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|StateMachineGym")
    static bool TryCancel_DivergencePendingPrimary(
        const FCk_Handle_PendingEntityScript& InPending, AActor* InActor);

    // Freeze production-held healthy members in leaf-first order, primary last.
    // Both outputs are cleared on rejection; aliased outputs are rejected.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|StateMachineGym")
    static bool TryCollect_DivergenceHealthyForest(
        const FCk_Handle& InPrimary, AActor* InActor,
        const TArray<FCk_Handle>& InPasses,
        TArray<FCk_Handle>& OutForest, TArray<FCk_Handle>& OutParents);

    // Requires terminal closure; success means requests, not physical retirement.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|StateMachineGym")
    static bool TryRetire_DivergenceHealthySource(
        const FCk_Handle& InPrimary, AActor* InActor,
        const TArray<FCk_Handle>& InPasses);

};
