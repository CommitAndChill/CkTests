#pragma once

#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Result_Data.h"
#include "CkHexGridBindingConflictWitness.generated.h"

UCLASS()
class UCk_HexGridBindingConflictWitness : public UObject
{
    GENERATED_BODY()
public:
    int32 Count = 0;
    FCk_HexGridPlacement_Result Last;

    int32 PlaceResults = 0;
    FCk_HexGridPlacement_Result PlaceResult;

    UFUNCTION()
    void OnPlace(const FCk_HexGridPlacement_Result& InResult)
    {
        ++PlaceResults;
        PlaceResult = InResult;
    }

    UFUNCTION()
    void OnConflict(FCk_Handle_HexGrid InGrid, const FCk_HexGridPlacement_Result& InResult)
    {
        ++Count;
        Last = InResult;
    }
};
