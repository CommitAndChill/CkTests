#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CkCueGym_SourceVisuals.h"
#include "CkCueGym_Presenter.generated.h"

class APlayerController;
class UProceduralMeshComponent;
class USceneComponent;
class UWorld;

USTRUCT(BlueprintType)
struct CKTESTS_API FCkCueGym_PassiveReceipt
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int64 Key = 0;
    UPROPERTY(BlueprintReadOnly) int32 Role = -1;
    UPROPERTY(BlueprintReadOnly) ECkCueGym_SourceVisualKind Kind = ECkCueGym_SourceVisualKind::Invalid;
    UPROPERTY(BlueprintReadOnly) int32 FillVertices = 0;
    UPROPERTY(BlueprintReadOnly) int32 FillTriangles = 0;
    UPROPERTY(BlueprintReadOnly) int32 LineVertices = 0;
    UPROPERTY(BlueprintReadOnly) int32 LineTriangles = 0;
    UPROPERTY(BlueprintReadOnly) bool bRegistered = false;
    UPROPERTY(BlueprintReadOnly) bool bHasFillMaterial = false;
    UPROPERTY(BlueprintReadOnly) bool bHasLineMaterial = false;
    UPROPERTY(BlueprintReadOnly) bool bVisibleByPolicy = false;
    UPROPERTY(BlueprintReadOnly) FTransform Pose = FTransform::Identity;
    UPROPERTY(BlueprintReadOnly) FLinearColor FillColor = FLinearColor::White;
    UPROPERTY(BlueprintReadOnly) FLinearColor LineColor = FLinearColor::White;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> Mesh;
};

// One local client's passive presentation; no Cue scripts, source timers, or
// source ECS entities. Caller owns spawn/retirement and sets Owner=local PC.
UCLASS(NotBlueprintable)
class CKTESTS_API ACk_CueGym_Presenter_UE final : public AActor
{
    GENERATED_BODY()
public:
    ACk_CueGym_Presenter_UE();

    // Caller supplies a complete native source census. No fixed row cap.
    // Every rejected frame permanently closes this presenter for its world.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Cue")
    bool TryInstall(UWorld* CallerWorld, int32 SourceGeneration, int64 Sequence,
        const TArray<FCkCueGym_SourceVisualRow>& CompleteRows);
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Cue") bool TryCheckHealth();
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Cue") bool Request_Suspend();
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Cue") bool Request_CloseFailure();
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Cue")
    bool Get_CanAccept() const { return !bFailed && !bInOperation && HasLocalContext(GetWorld()); }
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Cue") bool Get_IsFailed() const { return bFailed; }
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Cue") bool Get_IsSuspended() const { return bSuspended; }
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Cue") int32 Get_InstalledGeneration() const { return InstalledGeneration; }
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Cue") int64 Get_InstalledSequence() const { return InstalledSequence; }
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Cue") int32 Get_MaterializedRowCount() const { return Receipts.Num(); }
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Cue")
    bool TryGet_Receipt(int64 Key, FCkCueGym_PassiveReceipt& OutReceipt);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    bool HasLocalContext(UWorld* CallerWorld) const;
    bool IsKnownOwnedMesh(const UProceduralMeshComponent* Mesh) const;
    bool CheckCohort(bool bExpectSuspended) const;
    bool CheckMesh(UProceduralMeshComponent* Mesh,
        const FCkCueGym_SourceVisualRow& Row,
        const FCkCueGym_PassiveReceipt& Receipt, bool bExpectVisible,
        bool bExpectSuspended) const;
    void HideKnownOwned();
    void DestroyKnownOwned();
    void FailClosed();

    UPROPERTY(VisibleAnywhere, Category="Ck|Tests|Cue") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> OwnedMeshes;
    UPROPERTY(Transient) TArray<FCkCueGym_SourceVisualRow> InstalledRows;
    UPROPERTY(Transient) TArray<FCkCueGym_PassiveReceipt> Receipts;
    TWeakObjectPtr<APlayerController> CapturedController;
    TWeakObjectPtr<UWorld> CapturedWorld;
    UPROPERTY(Transient) bool bFailed = false;
    UPROPERTY(Transient) bool bSuspended = true;
    bool bInOperation = false;
    UPROPERTY(Transient) int32 InstalledGeneration = 0;
    UPROPERTY(Transient) int64 InstalledSequence = 0;
};
