#pragma once

#include "CoreMinimal.h"
#include "CkCamera/Camera/CkCamera_Fragment.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcsExt/Transform/CkTransform_Fragment_Data.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkCameraGym_LocalAdmission.generated.h"

class AActor;
class AGameStateBase;
class APlayerController;
class APawn;
class ULocalPlayer;
class UCk_CameraComponent;
class UWorld;

// Structural admission of one gym-private, locally owned camera subtree.
// The gym exclusively owns its camera, layer, tuner, view-anchor, target, and
// local input-layer descendants; it never transfers shared/external entities in.
// It is not a claim that the asynchronous default/mode scripts have Constructed.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkCameraGym_LocalAdmission
{
    GENERATED_BODY()

private:
    friend class UCk_CameraGym_LocalAdmission_UE;

    UPROPERTY(Transient) FCk_Handle WorldRoot;
    UPROPERTY(Transient) FCk_Handle PawnEntity;
    UPROPERTY(Transient) FCk_Handle Scope;
    UPROPERTY(Transient) FCk_Handle_Camera Director;
    UPROPERTY(Transient) FCk_Handle_Transform Target;
    UPROPERTY(Transient) TWeakObjectPtr<UWorld> World;
    UPROPERTY(Transient) TWeakObjectPtr<APawn> Pawn;
    UPROPERTY(Transient) TWeakObjectPtr<APlayerController> PC;
    UPROPERTY(Transient) TWeakObjectPtr<ULocalPlayer> LP;
    UPROPERTY(Transient) TWeakObjectPtr<AGameStateBase> GS;
    UPROPERTY(Transient) TWeakObjectPtr<AActor> Beacon;
    UPROPERTY(Transient) TWeakObjectPtr<UCk_CameraComponent> Output;
    bool CloseRequested = false;
};

UCLASS()
class CKTESTS_API UCk_CameraGym_LocalAdmission_UE final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Native read seams avoid ambiguous/inaccessible AngelScript bindings.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Camera")
    static bool Get_HasExactLocalPlayerWorld(ULocalPlayer* LP, UWorld* World);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Camera")
    static bool Get_IsReplicatingMovement(AActor* Actor);

    // Server/standalone actors must have enabled replication. A client proves
    // actual Iris delivery instead of reading its non-replicated CDO flag.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Camera")
    static bool Get_HasReplicationDelivery(AActor* Actor);

    // OutAdmission must be empty; an admitted receipt must be closed explicitly.
    // Rejected preflight leaves caller-owned state unchanged. An internal failure
    // after Camera Add clears only its exact director binding and retires Scope.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Camera")
    static bool TryCreate_LocalCamera(APawn* Pawn, APlayerController* PC,
        ULocalPlayer* LP, UWorld* World, const FCk_Handle& PawnEntity,
        AActor* Beacon, UCk_CameraComponent* OutputComponent,
        FCkCameraGym_LocalAdmission& OutAdmission);

    // Current structural identity/readiness, including reciprocal local possession.
    // Default/mode EntityScript construction is observed separately by the gym.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Camera")
    static bool Get_IsExactLocalAdmission(const FCkCameraGym_LocalAdmission& Admission);

    // Cleanup uses retained provenance and remains callable after possession loss.
    // It clears only the exact installed director and initiates destruction only
    // for the retained Scope still directly parented to its retained PawnEntity.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Camera")
    static bool TryClose_LocalCamera(UPARAM(ref) FCkCameraGym_LocalAdmission& Admission);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Camera")
    static FCk_Handle Get_Scope(const FCkCameraGym_LocalAdmission& Admission)
    { return Admission.Scope; }

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Camera")
    static FCk_Handle_Camera Get_Director(const FCkCameraGym_LocalAdmission& Admission)
    { return Admission.Director; }

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Camera")
    static FCk_Handle_Transform Get_Target(const FCkCameraGym_LocalAdmission& Admission)
    { return Admission.Target; }
};
