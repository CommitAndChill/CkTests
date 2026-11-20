#pragma once

#include "CkEcs/EntityConstructionScript/CkEntity_ConstructionScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment_Data.h"
#include "CkEcs/Persistence/CkPersistenceInitialization.h"
#include "CkEcs/Tag/CkTag.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"
#include "CkTests/Net/CkAutoTest_NetSubject.h"

#include "CkAutoTest_Snapshot_RendezvousProbe.generated.h"

namespace ck
{
    struct FFragment_AutoTest_RendezvousObservation
    {
        persistence::FInitializationStatus DuringConstruction;
        persistence::FInitializationStatus OnConstructed;
        bool ConstructedCallbackRan = false;
    };

    CK_DEFINE_ECS_TAG(FTag_AutoTest_DefinitionProbe);
}

UCLASS()
class CKTESTS_API UCk_AutoTest_Snapshot_BridgeRendezvous_UE : public UCk_EntityScript_WithActor_UE
{
    GENERATED_BODY()
public:
    auto Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;

protected:
    auto Get_IsSnapshotRespawnable() const -> bool override;

private:
    UFUNCTION()
    void OnProbeConstructed(FCk_Handle_EntityScript InEntity);
};

UCLASS()
class CKTESTS_API UCk_AutoTest_Snapshot_PlayerRendezvous_UE final : public UCk_AutoTest_Snapshot_BridgeRendezvous_UE
{
    GENERATED_BODY()
protected:
    auto Get_IsSnapshotRespawnable() const -> bool override;
};

UCLASS()
class CKTESTS_API ACk_AutoTest_Snapshot_BridgeRendezvousActor_UE final : public ACk_AutoTest_NetSubject
{
    GENERATED_BODY()
public:
    ACk_AutoTest_Snapshot_BridgeRendezvousActor_UE();

    // Distinguishes otherwise identical runtime actor recipes independently of their ECS tag payload.
    UPROPERTY(SaveGame)
    int32 RestoreProbeSlot = INDEX_NONE;
};

UCLASS()
class CKTESTS_API UCk_AutoTest_Snapshot_DefinitionRendezvous_UE final : public UCk_Entity_ConstructionScript_PDA
{
    GENERATED_BODY()
protected:
    auto DoConstruct_Implementation(FCk_Handle& InHandle) const -> void override;
};
