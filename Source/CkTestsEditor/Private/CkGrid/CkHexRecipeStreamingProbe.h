#pragma once

#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_EntityScript.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkHexRecipeStreamingProbe.generated.h"

// Native receiver methods are invoked by compiled K2 overrides, not by the test itself.
UCLASS(Blueprintable)
class UCkHexRecipeStreamingProbe : public UCk_HexGrid_EntityScript
{
    GENERATED_BODY()
public:
    UCkHexRecipeStreamingProbe()
    {
        _Replication = ECk_Replication::DoesNotReplicate;
        _InstancingPolicy = ECk_EntityScript_InstancingPolicy::InstancedPerEntity;
    }

    int32 ConstructCalls = 0;
    int32 ContinueCalls = 0;
    int32 BeginCalls = 0;
    int32 EndCalls = 0;
    bool ReadyInsideConstruct = false;

    UFUNCTION(BlueprintCallable, Category = "CkTests")
    ECk_EntityScript_ConstructionFlow RecordConstruct(FCk_Handle InHandle)
    {
        ++ConstructCalls;
        ReadyInsideConstruct = UCk_Utils_HexGrid_UE::Get_IsReady(UCk_Utils_HexGrid_UE::Cast(InHandle));
        return ECk_EntityScript_ConstructionFlow::Continue;
    }

    UFUNCTION(BlueprintCallable, Category = "CkTests")
    void RecordContinue(FCk_Handle InHandle)
    {
        ++ContinueCalls;
        DoFinishConstruction();
    }

    UFUNCTION(BlueprintCallable, Category = "CkTests")
    void RecordBegin(FCk_Handle InHandle) { ++BeginCalls; }

    UFUNCTION(BlueprintCallable, Category = "CkTests")
    void RecordEnd(FCk_Handle InHandle) { ++EndCalls; }
};
