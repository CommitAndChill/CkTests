#pragma once

#include "CkProceduralAnimation/Action/CkProceduralAction_Fragment_Data.h"

#include "CkTest_ProceduralAction_FinishedObserver.generated.h"

// Counts OnFinished broadcasts and keeps the last terminal.
UCLASS()
class UCk_Test_ProceduralAction_FinishedObserver_UE : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void OnFinished(FCk_Handle_ProceduralAction InAction, FCk_ProceduralAction_Terminal InTerminal);

    int32 Calls = 0;
    FCk_ProceduralAction_Terminal Terminal;
};
