#pragma once
#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkVoxelNavGym_Wire.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkVoxelNavGym_Runtime.generated.h"

UCLASS()
class CKTESTS_API UCk_VoxelNavGym_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="Ck|Tests|VoxelNavGym")
    static bool Get_IsLiveInWorld(const FCk_Handle& Handle, UWorld* World);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|VoxelNavGym")
    static bool Get_IsLiveVolume(const FCk_Handle& Handle, UWorld* World);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|VoxelNavGym")
    static bool Get_IsLiveCrowdAgent(const FCk_Handle& Handle, UWorld* World);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|VoxelNavGym")
    static bool Get_IsLiveVoxelPath(const FCk_Handle& Handle, UWorld* World);
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|VoxelNavGym")
    static bool TryGet_SourceForest(const FCk_Handle& Root, const FCk_Handle& WorldRoot,
        TArray<FCk_Handle>& OutForest);
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|VoxelNavGym")
    static bool TryGet_AgentForest(const FCk_Handle& SourceRoot, const FCk_Handle& AgentRoot,
        const FCk_Handle& WorldRoot, TArray<FCk_Handle>& OutForest);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|VoxelNavGym")
    static bool TryGet_AgentFrame(const FCk_Handle& Handle, UWorld* World, int32 Index,
        FCkVoxelNavGym_AgentFrame& OutFrame);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|VoxelNavGym")
    static bool Get_IsValidFrame(const FCkVoxelNavGym_WireSnapshot& Frame);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|VoxelNavGym")
    static bool Get_IsSameFrame(const FCkVoxelNavGym_WireSnapshot& Left,
        const FCkVoxelNavGym_WireSnapshot& Right);
};
