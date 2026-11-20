#pragma once

#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment_Data.h"
#include "CkEcs/Persistence/CkPersistenceInitialization.h"
#include "CkGrid/HexGridSystem/Blocker/CkHexGridBlocker_Fragment_Data.h"
#include "GameplayTagContainer.h"

#include "CkAutoTest_Snapshot_InitializationProbe.generated.h"

USTRUCT()
struct FCk_AutoTest_InitializationSlot
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag Label;

    UPROPERTY()
    bool ExplicitFresh = false;
};

namespace ck
{
    struct FFragment_AutoTest_InitializationObservation
    {
        persistence::FInitializationStatus DuringConstruct;
        persistence::FInitializationStatus OnConstructed;
        bool ConstructedCallbackRan = false;
        bool ComposedBoard = false;
        ECk_HexGridBlocker_Failure ConstructWrite = ECk_HexGridBlocker_Failure::InvalidGrid;
        ECk_HexGridBlocker_Failure ConstructedWrite = ECk_HexGridBlocker_Failure::InvalidGrid;
        TArray<FCk_Handle> Children;
    };
}

UCLASS()
class CKTESTS_API UCk_AutoTest_Snapshot_InitializationProbe_UE : public UCk_EntityScript_UE
{
    GENERATED_BODY()

public:
    UCk_AutoTest_Snapshot_InitializationProbe_UE();

    auto Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
        -> ECk_EntityScript_ConstructionFlow override;

protected:
    auto Get_IsSnapshotRespawnable() const -> bool override;

private:
    UFUNCTION()
    void OnSelfConstructed(FCk_Handle_EntityScript InEntity);

    UFUNCTION()
    void OnChildConstructed(FCk_Handle_EntityScript InChild);

    TMap<FCk_Handle, FGameplayTag> _PendingLabels;
};

// A distinct root recipe lets the test reacquire only the new world's root.
UCLASS()
class CKTESTS_API UCk_AutoTest_Snapshot_InitializationRoot_UE final : public UCk_AutoTest_Snapshot_InitializationProbe_UE
{
    GENERATED_BODY()
};
