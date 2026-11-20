#pragma once

#include "CkTests/Net/CkAutoTest_NetSubject.h"
#include "CkGrid/2dGridSystem/Grid/Ck2dGridSystem_Fragment_Data.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Readiness_Data.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Fragment_Data.h"
#include "CkAutoTest_NetSubject_MixedGrid.generated.h"

// Each actor owns an independent replication driver and snapshot respawn recipe.
// These handles are local observations of the feature composed by that world's driver.
UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_MixedGridObject_UE : public ACk_AutoTest_NetSubject
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_MixedGridObject_UE();

    UFUNCTION(BlueprintPure, Category = "Ck|AutoTest")
    FCk_Handle_HexGridObject Get_HexObject() const { return _HexObject; }

private:
    friend class UCk_AutoTest_NetSubject_MixedGridObjectEntityScript_UE;

    UPROPERTY(Transient)
    FCk_Handle_HexGridObject _HexObject;
};

UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_MixedGrid_UE : public ACk_AutoTest_NetSubject
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_MixedGrid_UE();

    UFUNCTION(BlueprintPure, Category = "Ck|AutoTest")
    FCk_Handle_HexGrid Get_HexGrid() const { return _HexGrid; }

    UFUNCTION(BlueprintPure, Category = "Ck|AutoTest")
    FCk_Handle_2dGridSystem Get_SquareGrid() const { return _SquareGrid; }

    UFUNCTION(BlueprintPure, Category = "Ck|AutoTest")
    FCk_HexGridReadiness_Result Get_ConstructReadiness() const { return _ConstructReadiness; }

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ck|AutoTest")
    bool Request_PlaceSquare(ACk_AutoTest_NetSubject_MixedGridObject_UE* InObject, FIntPoint InAnchor);

    // Returns true when the normal placement request was submitted. Check the placement after the FIFO drains.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ck|AutoTest")
    bool Request_PlaceHex(ACk_AutoTest_NetSubject_MixedGridObject_UE* InObject, FCk_HexCoord InAnchor, ECk_HexRotation InRotation);

private:
    friend class UCk_AutoTest_NetSubject_MixedGridEntityScript_UE;

    UPROPERTY(Transient)
    FCk_Handle_HexGrid _HexGrid;
    UPROPERTY(Transient)
    FCk_Handle_2dGridSystem _SquareGrid;
    UPROPERTY(Transient)
    FCk_HexGridReadiness_Result _ConstructReadiness;
};

// Snapshot respawn uses this class to select the same cooked R32 recipe on both peers.
UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_DenseHexBoard_UE : public ACk_AutoTest_NetSubject_MixedGrid_UE
{
    GENERATED_BODY()
};

UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_DenseHexSecondBoard_UE : public ACk_AutoTest_NetSubject_DenseHexBoard_UE
{
    GENERATED_BODY()
};
