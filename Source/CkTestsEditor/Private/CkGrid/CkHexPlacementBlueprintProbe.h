#pragma once

#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Fragment_Data.h"
#include "CkHexPlacementBlueprintProbe.generated.h"

UCLASS(Blueprintable)
class UCkHexPlacementBlueprintProbe : public UObject
{
    GENERATED_BODY()

public:
    FGuid Id;
    int32 Accepted = 0;
    int32 Rejected = 0;
    int32 PlacedSignals = 0;
    ECk_HexGridFailure Failure = ECk_HexGridFailure::None;

    UFUNCTION(BlueprintCallable, Category="CkTests")
    void AcceptPlacement(FGuid InId) { Id = InId; ++Accepted; }

    UFUNCTION(BlueprintCallable, Category="CkTests")
    void RejectPlacement(ECk_HexGridFailure InFailure) { Failure = InFailure; ++Rejected; }

    UFUNCTION()
    void OnPlaced(FCk_Handle_HexGrid InGrid, const FCk_HexGridPlacement_View& InPlacement) { ++PlacedSignals; }
};
