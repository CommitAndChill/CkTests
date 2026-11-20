#pragma once

#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_EntityScript.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGridObject_EntityScript.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Fragment_Data.h"
#include "CkSnapshot/Snapshot/CkSnapshot_LoadReport.h"
#include <Subsystems/GameInstanceSubsystem.h>
#include "CkHexPersistenceGym.generated.h"

// Dedicated recipe classes make discovery unambiguous without changing RuntimeSpawned rows into keyed EngineOwned rows.
UCLASS()
class CKTESTS_API UCk_HexPersistenceGymBoard : public UCk_HexGrid_EntityScript
{
    GENERATED_BODY()
public:
    UCk_HexPersistenceGymBoard();
    auto Get_IsSnapshotRespawnable() const -> bool override { return true; }
};

UCLASS()
class CKTESTS_API UCk_HexPersistenceGymObject : public UCk_HexGridObject_EntityScript
{
    GENERATED_BODY()
public:
    UCk_HexPersistenceGymObject();
    auto Get_IsSnapshotRespawnable() const -> bool override { return true; }
};

// The operation owner survives level travel; controllers reacquire through this session in the new world.
UCLASS()
class CKTESTS_API UCk_HexPersistenceGymSession : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "CkTests|Hex Persistence")
    void Request_Start(const FTransform& InPivot);
    UFUNCTION(BlueprintCallable, Category = "CkTests|Hex Persistence")
    void Request_Place();
    UFUNCTION(BlueprintCallable, Category = "CkTests|Hex Persistence")
    void Request_Save();
    UFUNCTION(BlueprintCallable, Category = "CkTests|Hex Persistence")
    void Request_Load();
    UFUNCTION(BlueprintPure, Category = "CkTests|Hex Persistence")
    FCk_Handle_HexGrid Get_Board() const;
    UFUNCTION(BlueprintPure, Category = "CkTests|Hex Persistence")
    FString Get_Status() const;
    UFUNCTION(BlueprintPure, Category = "CkTests|Hex Persistence")
    FString Get_Report() const { return _Report; }
    UFUNCTION(BlueprintPure, Category = "CkTests|Hex Persistence")
    bool Get_IsBusy() const { return _Busy; }
    UFUNCTION(BlueprintPure, Category = "CkTests|Hex Persistence")
    bool Get_HasSave() const { return _HasSave; }
    UFUNCTION(BlueprintPure, Category = "CkTests|Hex Persistence")
    bool Get_PhysicallyReacquired() const { return _PhysicallyReacquired; }

private:
    UFUNCTION()
    void OnPlaced(const FCk_HexGridPlacement_Result& InResult);
    UFUNCTION()
    void OnSaved(ECk_SnapshotResult InResult);
    UFUNCTION()
    void OnLoaded(FCk_Snapshot_LoadReport InReport);

    TWeakObjectPtr<UWorld> _SpawnWorld;
    FCk_Handle_HexGrid _BeforeLoad;
    bool _Busy = false;
    bool _HasSave = false;
    bool _PhysicallyReacquired = false;
    FString _Report = TEXT("No snapshot yet");
};
