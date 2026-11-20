#include "CkTests/Net/CkAutoTest_NetSubject_MixedGridEntityScript.h"

#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"
#include "CkTests/Net/CkAutoTest_NetSubject_MixedGrid.h"
#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkGrid/2dGridSystem/Grid/Ck2dGridSystem_Utils.h"
#include "CkGrid/2dGridSystem/Object/Ck2dGridObject_Utils.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_Spec.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGridInitialization_Data.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"

auto
    UCk_AutoTest_NetSubject_MixedGridEntityScript_UE::
    Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);
    auto* Actor = Cast<ACk_AutoTest_NetSubject_MixedGrid_UE>(UCk_Utils_EntityBridge_UE::TryGet_Actor(InHandle));
    if (!IsValid(Actor)) { return Flow; }

    const bool Dense = Actor->IsA<ACk_AutoTest_NetSubject_DenseHexBoard_UE>();
    const auto* AssetPath = Dense ? ck::tests::hex_persistence::Radius32BoardAssetPath : ck::tests::hex_persistence::BoardAssetPath;
    const auto* Asset = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, AssetPath);
    FCk_HexGrid_Spec Spec;
    FCk_HexGridRecipeIdentity Recipe;
    ECk_HexBoardFailure Failure{};
    if (!IsValid(Asset) || !Asset->TryResolveWithIdentity(Spec, Recipe, Failure)) { return Flow; }
    const FCk_HexGridInitialization Initialization;
    if (!UCk_Utils_HexGrid_UE::TryAddForAuthoring(InHandle, Actor->GetActorTransform(), Spec, Actor->_HexGrid,
        Failure, &Initialization, &Recipe)) { return Flow; }
    Actor->_ConstructReadiness = UCk_Utils_HexGrid_UE::Get_Readiness(Actor->_HexGrid);
    if (Dense) { return Flow; }

    auto Transform = UCk_Utils_Transform_UE::Cast(InHandle);
    auto SquareSpec = FCk_2dGridSystem_Spec{FIntPoint{10,10}, FVector2D{100.0,100.0}};
    SquareSpec.Set_DefaultCellState(ECk_EnableDisable::Enable);
    Actor->_SquareGrid = UCk_Utils_2dGridSystem_UE::Add(Transform, SquareSpec);
    return Flow;
}

auto
    UCk_AutoTest_NetSubject_MixedGridObjectEntityScript_UE::
    Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);
    auto* Actor = Cast<ACk_AutoTest_NetSubject_MixedGridObject_UE>(UCk_Utils_EntityBridge_UE::TryGet_Actor(InHandle));
    if (!IsValid(Actor)) { return Flow; }

    ECk_HexGridObject_Failure Failure{};
    if (!UCk_Utils_HexGridObject_UE::TryAdd(InHandle, {}, Actor->_HexObject, Failure)) { return Flow; }
    UCk_Utils_2dGridObject_UE::Add(InHandle, FCk_2dGridObject_Spec{FIntPoint{1,1}});
    return Flow;
}
