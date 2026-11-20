#pragma once

#include "CkTests/HexGrid/CkHexPersistenceGym.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Readiness_Data.h"
#include "CkHexPersistenceReadinessProbe.generated.h"

UCLASS()
class CKTESTS_API UCk_HexPersistenceReadinessProbe : public UCk_HexPersistenceGymBoard
{
    GENERATED_BODY()
public:
    bool AwaitingAtConstruct = false;
    bool RequestRejectedNotReady = false;
    bool FreshChildReady = false;
    bool CommittedWhileAwaiting = false;
    bool EmptyAtCommit = false;
    bool EarlyTerminalNotification = false;
    bool HydratedSawReady = false;
    int32 TerminalNotifications = 0;
    int32 HydratedNotifications = 0;

protected:
    auto Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;

private:
    UFUNCTION()
    void OnReadiness(FCk_Handle_HexGrid InGrid, const FCk_HexGridReadiness_Result& InResult);
    UFUNCTION()
    void OnRequest(const FCk_HexGridPlacement_Result& InResult);
    UFUNCTION()
    void OnHydrated(FCk_Handle InHandle);
};
