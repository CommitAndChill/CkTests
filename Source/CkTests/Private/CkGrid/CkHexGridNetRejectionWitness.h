#pragma once

#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkHexGridNetRejectionWitness.generated.h"

UCLASS()
class UCk_HexGridNetRejectionWitness : public UObject
{
    GENERATED_BODY()

public:
    TArray<FCk_HexGridReadiness_Result> Rejections;
    bool AllNotificationsSawReady = true;

    UFUNCTION()
    void OnRejected(FCk_Handle_HexGrid InGrid, const FCk_HexGridReadiness_Result& InRejection)
    {
        Rejections.Add(InRejection);
        AllNotificationsSawReady &= UCk_Utils_HexGrid_UE::Get_IsReady(InGrid);
    }
};
