#pragma once

#include "CkEcs/EntityConstructionScript/CkEntity_ConstructionScript.h"
#include "CkEntityTag/CkEntityTag_Fragment_Data.h"

#include "CkTenetIntegrity_RequestProbe.generated.h"

// Exact reflected property shape written before EntityTag gained explicit gameplay counts.
USTRUCT()
struct FCkTenetIntegrity_LegacyEntityTags
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(FCkTenetIntegrity_LegacyEntityTags);

private:
    UPROPERTY()
    TArray<FName> _TagNames;

    UPROPERTY()
    TArray<int32> _Counts;

public:
    CK_PROPERTY(_TagNames);
    CK_PROPERTY(_Counts);
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS()
class CKTESTS_API UCkTenetIntegrity_RequestProbe : public UObject
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(UCkTenetIntegrity_RequestProbe);

private:
    int32 _CompletionCount = 0;
    ECk_Request_OperationResult _Result = ECk_Request_OperationResult::Failed;
    FGameplayTagContainer _ExpectedGameplay;
    int32 _TagCallbackCount = 0;
    bool _ObservedConsistentGameplay = true;

public:
    CK_PROPERTY_GET(_CompletionCount);
    CK_PROPERTY_GET(_Result);
    CK_PROPERTY(_ExpectedGameplay);
    CK_PROPERTY_GET(_TagCallbackCount);
    CK_PROPERTY_GET(_ObservedConsistentGameplay);

    UFUNCTION()
    void
    OnCompleted(
        FCk_Handle InHandle,
        ECk_Request_OperationResult InResult);

    UFUNCTION()
    void
    OnTagUpdated(
        FCk_Handle InHandle,
        FName InTag,
        ECk_EntityTagUpdate InUpdate);
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS()
class CKTESTS_API UCkTenetIntegrity_ConstructionProbe : public UCk_Entity_ConstructionScript_PDA
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(UCkTenetIntegrity_ConstructionProbe);

protected:
    virtual auto
    DoConstruct_Implementation(
        FCk_Handle& InHandle) const
        -> void override;
};
