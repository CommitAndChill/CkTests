#pragma once

#include "CkRuntimeMesh/CkRuntimeMesh_Fragment_Data.h"

#include "CkTest_RuntimeMeshRenderListener.generated.h"

UCLASS()
class UCk_Test_RuntimeMeshRenderListener_UE : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void OnSliceResolved(FCk_RuntimeMesh_SliceResult InResult)
    {
        ++Calls;
        Result = InResult;
    }

    UPROPERTY()
    FCk_RuntimeMesh_SliceResult Result;

    int32 Calls = 0;
};
