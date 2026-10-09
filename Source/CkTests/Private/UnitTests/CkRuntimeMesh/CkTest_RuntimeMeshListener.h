#pragma once

#include "CkRuntimeMesh/CkRuntimeMesh_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Request/CkRequest_Completion.h"

#include "CkTest_RuntimeMeshListener.generated.h"

UCLASS()
class UCk_Test_RuntimeMeshListener_UE : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void OnSliceResolved(FCk_RuntimeMesh_SliceResult InResult)
    {
        Results.Add(InResult);
        CallbackOrder.Add(TEXT("typed"));
        if (DestroySourceOnFirst && Results.Num() == 1)
        { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Source); }
        if (DestroyOwnerOnFirst && Results.Num() == 1)
        { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(ResultOwner); }
        if (DestroyGenericReceiverOnFirst && Results.Num() == 1
            && GenericReceiverToDestroy.IsValid())
        { GenericReceiverToDestroy.Get()->MarkAsGarbage(); }
        if (EnqueueOnFirst && Results.Num() == 1)
        {
            auto Request = FCk_Request_RuntimeMesh_Slice{};
            Request.Set_OperationID(ReentrantOperationID);
            Request.Set_ResultOwner(ResultOwner);
            auto Plane = FCk_RuntimeMesh_PlaneLocal{};
            Plane.Set_PositionCm(FVector{30, 0, 0});
            Plane.Set_Normal(FVector::ForwardVector);
            Plane.Set_Tangent(FVector::RightVector);
            Request.Set_Plane(Plane);
            auto Typed = FCk_Delegate_RuntimeMesh_OnSliceResolved{};
            Typed.BindDynamic(this, &UCk_Test_RuntimeMeshListener_UE::OnSliceResolved);
            auto Generic = FCk_Delegate_Request_OnCompleted{};
            Generic.BindDynamic(this, &UCk_Test_RuntimeMeshListener_UE::OnRequestCompleted);
            UCk_Utils_RuntimeMesh_UE::Request_Slice(Source, Request, Typed, Generic);
        }
    }

    UFUNCTION()
    void OnRequestCompleted(
        FCk_Handle InSource, ECk_Request_OperationResult InResult)
    {
        Completions.Add(InResult);
        CallbackOrder.Add(TEXT("generic"));
    }

    TArray<FCk_RuntimeMesh_SliceResult> Results;
    TArray<ECk_Request_OperationResult> Completions;
    TArray<FName> CallbackOrder;
    FCk_Handle_RuntimeMesh Source;
    FCk_Handle ResultOwner;
    FGuid ReentrantOperationID;
    bool EnqueueOnFirst = false;
    bool DestroySourceOnFirst = false;
    bool DestroyOwnerOnFirst = false;
    bool DestroyGenericReceiverOnFirst = false;
    TWeakObjectPtr<UCk_Test_RuntimeMeshListener_UE> GenericReceiverToDestroy;
};
