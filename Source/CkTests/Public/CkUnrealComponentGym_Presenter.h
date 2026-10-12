#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "CkUnrealComponentGym_Presenter.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UPointLightComponent;
class USpotLightComponent;
class UTextRenderComponent;
class UArrowComponent;
class UBoxComponent;
class USphereComponent;
class UCapsuleComponent;
class APlayerController;
class UWorld;

// One local player's passive display. The owning client/controller creates and
// retires this actor; it never creates components after its native constructor.
// Slot order matches ECk_UnrealComponentGym_Type in the authored AS driver.
UCLASS(NotBlueprintable)
class CKTESTS_API ACk_UnrealComponentGym_Presenter_UE final : public AActor
{
    GENERATED_BODY()

public:
    static constexpr int32 SlotCount = 8;

    ACk_UnrealComponentGym_Presenter_UE();

    // The caller invokes this only for a strictly newer global sequence and
    // supplies all eight world poses in authored enum order. Same-sequence
    // replay is a caller error, including an otherwise identical payload.
    // A rejected frame closes this presenter permanently and hides its known
    // owned visuals. Failure remains terminal until a new world session;
    // the caller must not replace this presenter in the same session.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|UnrealComponent")
    bool TryInstall(int32 Generation, int64 Sequence,
        const TArray<FTransform>& WorldPoses);

    // Call when the owner checks liveness without a new frame. There is no
    // internal tick; loss between calls is caught at the next call/EndPlay.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|UnrealComponent")
    bool TryCheckHealth();

    // Healthy pause for an Initializing reset. Preserves the captured cohort
    // and installed identity; the next new Active frame may reveal it again.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|UnrealComponent")
    bool Request_Suspend();

    // An explicit terminal close for failed wire, lost source, or world
    // closure. Hides known owned visuals without fabricating a bad frame.
    // Returns true only when this call transitions from open to failed.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|UnrealComponent")
    bool Request_CloseFailure();

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|UnrealComponent")
    bool Get_IsFailed() const { return bFailed; }

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|UnrealComponent")
    bool Get_IsSuspended() const { return bSuspended; }

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|UnrealComponent")
    int32 Get_InstalledGeneration() const { return InstalledGeneration; }

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|UnrealComponent")
    int64 Get_InstalledSequence() const { return InstalledSequence; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    bool HasIntactCohort() const;
    bool HasAuthoredStyle() const;
    bool HasExpectedVisibilityState(bool bExpectSuspended) const;
    bool HasLocalContext() const;
    USceneComponent* GetSlot(int32 Index) const;
    bool IsKnownSlot(int32 Index) const;
    void HideKnownOwnedVisuals();
    void FailClosed();

    UPROPERTY(VisibleAnywhere, Category="Ck|Tests|Gym|UnrealComponent")
    TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere, Category="Ck|Tests|Gym|UnrealComponent")
    TObjectPtr<UStaticMeshComponent> StaticMesh;
    UPROPERTY(VisibleAnywhere, Category="Ck|Tests|Gym|UnrealComponent")
    TObjectPtr<UPointLightComponent> PointLight;
    UPROPERTY(VisibleAnywhere, Category="Ck|Tests|Gym|UnrealComponent")
    TObjectPtr<USpotLightComponent> SpotLight;
    UPROPERTY(VisibleAnywhere, Category="Ck|Tests|Gym|UnrealComponent")
    TObjectPtr<UTextRenderComponent> TextRender;
    UPROPERTY(VisibleAnywhere, Category="Ck|Tests|Gym|UnrealComponent")
    TObjectPtr<UArrowComponent> Arrow;
    UPROPERTY(VisibleAnywhere, Category="Ck|Tests|Gym|UnrealComponent")
    TObjectPtr<UBoxComponent> Box;
    UPROPERTY(VisibleAnywhere, Category="Ck|Tests|Gym|UnrealComponent")
    TObjectPtr<USphereComponent> Sphere;
    UPROPERTY(VisibleAnywhere, Category="Ck|Tests|Gym|UnrealComponent")
    TObjectPtr<UCapsuleComponent> Capsule;

    TWeakObjectPtr<APlayerController> CapturedLocalController;
    TWeakObjectPtr<UWorld> CapturedWorld;

    // Optional client query shapes; default presentation is collision-free.
    UPROPERTY(EditDefaultsOnly, Category="Ck|Tests|Gym|UnrealComponent")
    bool bEnableClientShapeQueries = false;

    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeAsset;
    UPROPERTY(Transient)
    bool bFailed = false;
    UPROPERTY(Transient)
    bool bSuspended = true;
    bool bInOperation = false;
    UPROPERTY(Transient)
    int32 InstalledGeneration = 0;
    UPROPERTY(Transient)
    int64 InstalledSequence = 0;
};
