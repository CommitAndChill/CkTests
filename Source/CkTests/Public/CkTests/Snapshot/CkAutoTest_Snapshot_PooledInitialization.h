#pragma once

#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/Persistence/CkPersistenceInitialization.h"

#include "CkAutoTest_Snapshot_PooledInitialization.generated.h"

USTRUCT()
struct CKTESTS_API FCk_AutoTest_PooledInitializationParams
{
    GENERATED_BODY()

    UPROPERTY()
    bool TrackInitialization = true;

    UPROPERTY()
    bool TwoProducers = false;
};

USTRUCT()
struct CKTESTS_API FCk_SaveData_AutoTest_PooledValue
{
    GENERATED_BODY()
    CK_GENERATED_BODY(FCk_SaveData_AutoTest_PooledValue);

private:
    UPROPERTY()
    int32 _Value = -1;

public:
    CK_PROPERTY(_Value);
};

USTRUCT()
struct CKTESTS_API FCk_SaveData_AutoTest_PooledReferences
{
    GENERATED_BODY()
    CK_GENERATED_BODY(FCk_SaveData_AutoTest_PooledReferences);

private:
    UPROPERTY()
    TArray<FCk_Handle> _Sources;

public:
    CK_PROPERTY(_Sources);
};

namespace ck
{
    struct FFragment_AutoTest_PooledValue
    {
        bool _Produces = true;
        int32 _Value = -1;
        TArray<int32> _AppliedValues;
        persistence::FInitializationStatus _DuringConstruct;
    };

    struct FFragment_AutoTest_PooledRoot
    {
        TArray<FCk_Handle> _ConstructedChildren;
        TArray<FCk_Handle> _RestoredReferences;
    };
}

// Uses the same shared-key production helper as ActorRelay. The source has two interchangeable owners;
// the reconstructed world has one, so both source handles must remap onto that one live representative.
UCLASS()
class CKTESTS_API UCk_AutoTest_Snapshot_PooledInitialization_UE final : public UCk_EntityScript_UE
{
    GENERATED_BODY()

public:
    UCk_AutoTest_Snapshot_PooledInitialization_UE();

    auto Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
        -> ECk_EntityScript_ConstructionFlow override;

protected:
    auto Get_IsSnapshotRespawnable() const -> bool override;
};
