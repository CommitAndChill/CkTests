#include "CkTests/Net/CkAutoTest_NetSubject_MixedGrid.h"

#include "CkTests/Net/CkAutoTest_NetSubject_MixedGridEntityScript.h"
#include "CkGrid/2dGridSystem/Placement/Ck2dGridPlacement_Utils.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"

ACk_AutoTest_NetSubject_MixedGrid_UE::ACk_AutoTest_NetSubject_MixedGrid_UE()
{
    _EntityScriptClass = UCk_AutoTest_NetSubject_MixedGridEntityScript_UE::StaticClass();
}

ACk_AutoTest_NetSubject_MixedGridObject_UE::ACk_AutoTest_NetSubject_MixedGridObject_UE()
{
    _EntityScriptClass = UCk_AutoTest_NetSubject_MixedGridObjectEntityScript_UE::StaticClass();
}

auto
    ACk_AutoTest_NetSubject_MixedGrid_UE::
    Request_PlaceSquare(ACk_AutoTest_NetSubject_MixedGridObject_UE* InObject, FIntPoint InAnchor)
    -> bool
{
    if (!HasAuthority() || !IsValid(InObject) || InObject->GetWorld() != GetWorld() ||
        !ck::IsValid(_SquareGrid) || !ck::IsValid(InObject->Get_HexObject())) { return false; }

    FCk_Handle Object = InObject->Get_HexObject();
    return ck::IsValid(UCk_Utils_2dGridPlacement_UE::Request_Place(
        _SquareGrid, Object, InAnchor, ECk_CardinalRotation::None, {}));
}

auto
    ACk_AutoTest_NetSubject_MixedGrid_UE::
    Request_PlaceHex(ACk_AutoTest_NetSubject_MixedGridObject_UE* InObject, FCk_HexCoord InAnchor, ECk_HexRotation InRotation)
    -> bool
{
    if (!HasAuthority() || !IsValid(InObject) || InObject->GetWorld() != GetWorld() || !ck::IsValid(_HexGrid))
    { return false; }

    FCk_HexGridPlacement_Spec Spec;
    Spec.Set_Object(InObject->Get_HexObject()).Set_Anchor(InAnchor).Set_Rotation(InRotation);
    if (!UCk_Utils_HexGridPlacement_UE::Get_CanPlace(_HexGrid, Spec).Get_Succeeded()) { return false; }

    UCk_Utils_HexGridPlacement_UE::Request_Place(_HexGrid, FCk_Request_HexGridPlacement_Place{Spec}, {});
    return true;
}
