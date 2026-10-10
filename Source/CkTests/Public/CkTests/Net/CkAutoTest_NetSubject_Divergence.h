#pragma once

#include "CoreMinimal.h"
#include "CkTests/Net/CkAutoTest_NetSubject.h"

#include "CkAutoTest_NetSubject_Divergence.generated.h"

// One completed authority readback. The twelve counters are actual source
// snapshots, not expected test values. The enclosing property is one
// replicated frame; clients still validate every field before accepting it.
USTRUCT(BlueprintType)
struct CKTESTS_API FCk_AutoTest_DivergenceFrame
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) int32 SchemaVersion = 0;
    UPROPERTY(BlueprintReadOnly) int64 SourceGeneration = 0;
    UPROPERTY(BlueprintReadOnly) int64 Sequence = 0;
    UPROPERTY(BlueprintReadOnly) bool bTimed = false;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> SourceActor;
    UPROPERTY(BlueprintReadOnly) int64 SourceNetIdentity = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_A_Enter = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_A_Idle = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_A_Branch = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_A_Left = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_A_Right = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_A_Finish = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_B_Enter = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_B_Idle = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_B_Branch = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_B_Left = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_B_Right = 0;
    UPROPERTY(BlueprintReadOnly) int32 Snap_B_Finish = 0;
    UPROPERTY(BlueprintReadOnly) bool bCompleted = false;
};

// The inherited NetSubject still creates its normal WithActor bridge. This
// subclass owns only the authority's one Divergence source actor and its
// completed replicated value frame; clients never create a source here.
UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_Divergence_UE final
    : public ACk_AutoTest_NetSubject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Ck|AutoTest|Divergence")
    bool TryRetain_SourceActor(AActor* Actor, bool bTimed);

    UFUNCTION(BlueprintCallable, Category = "Ck|AutoTest|Divergence")
    bool TryPublish_CompletedSource();

    UFUNCTION(BlueprintPure, Category = "Ck|AutoTest|Divergence")
    FCk_AutoTest_DivergenceFrame Get_Snapshot() const { return Snapshot; }

    UFUNCTION(BlueprintPure, Category = "Ck|AutoTest|Divergence")
    static int64 Get_IrisActorIdentity(AActor* Actor);

    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    bool HasLiveAuthorityWorld() const;
    bool HasExactSourceActor() const;

    UPROPERTY(Replicated)
    FCk_AutoTest_DivergenceFrame Snapshot;

    UPROPERTY()
    TObjectPtr<AActor> SourceActor;

    TWeakObjectPtr<AActor> SourceIdentity;
    bool bAdmissionAttempted = false;
    bool bFailed = false;
    bool bSourceTimed = false;
};
