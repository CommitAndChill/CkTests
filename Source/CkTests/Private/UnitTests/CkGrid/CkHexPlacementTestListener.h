#pragma once

#include "CkEcs/Request/CkRequest_Completion.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Result_Data.h"
#include "CkHexPlacementTestListener.generated.h"

UCLASS()
class UCk_HexPlacementTestListener : public UObject
{
    GENERATED_BODY()

public:
    TFunction<void(const FCk_HexGridPlacement_Result&)> Result;
    TFunction<void(FCk_Handle, ECk_Request_OperationResult)> Completed;
    TFunction<void(FName, FCk_Handle_HexGrid, const FCk_HexGridPlacement_View&)> Changed;
    TFunction<void(FCk_Handle_HexGrid, const FCk_HexGridReadiness_Result&)> Readiness;

    UFUNCTION()
    void OnResult(const FCk_HexGridPlacement_Result& InResult)
    { if (Result) { Result(InResult); } }

    UFUNCTION()
    void OnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    { if (Completed) { Completed(InOwner, InResult); } }

    UFUNCTION()
    void OnPlaced(FCk_Handle_HexGrid InGrid, const FCk_HexGridPlacement_View& InPlacement)
    { if (Changed) { Changed(TEXT("Placed"), InGrid, InPlacement); } }

    UFUNCTION()
    void OnMoved(FCk_Handle_HexGrid InGrid, const FCk_HexGridPlacement_View& InPlacement)
    { if (Changed) { Changed(TEXT("Moved"), InGrid, InPlacement); } }

    UFUNCTION()
    void OnRemoved(FCk_Handle_HexGrid InGrid, const FCk_HexGridPlacement_View& InPlacement)
    { if (Changed) { Changed(TEXT("Removed"), InGrid, InPlacement); } }

    UFUNCTION()
    void OnReadiness(FCk_Handle_HexGrid InGrid, const FCk_HexGridReadiness_Result& InResult)
    { if (Readiness) { Readiness(InGrid, InResult); } }
};
