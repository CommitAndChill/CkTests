#pragma once

#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Fragment_Data.h"
#include "CkHexGridNetConflictWitness.generated.h"

UCLASS()
class UCk_HexGridNetConflictWitness : public UObject
{
    GENERATED_BODY()

public:
    int32 Count = 0;
    int32 PlacedCount = 0;
    FCk_Handle_HexGrid Board;
    FGuid PlacementId;
    ECk_HexGridFailure Failure{};

    UFUNCTION()
    void OnPlaced(FCk_Handle_HexGrid InBoard, const FCk_HexGridPlacement_View& InPlacement)
    { ++PlacedCount; }

    UFUNCTION()
    void OnBindingFailed(FCk_Handle_HexGrid InBoard, FGuid InPlacementId, ECk_HexGridFailure InFailure)
    {
        ++Count;
        Board = InBoard;
        PlacementId = InPlacementId;
        Failure = InFailure;
    }
};
