class UCk_AutoTest_HexGrid_Placement : UCk_AutoTest_Base
{
    private FCk_Handle_HexGrid _Board;
    private FCk_Handle_HexGridObject _Object;
    private FGuid _Id;
    private int _Typed = 0;
    private int _Generic = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        FCk_HexDomain Domain;
        Domain.Set_RingRadius(1);
        FCk_HexGrid_Spec BoardSpec;
        BoardSpec.Set_Domain(Domain);
        auto BoardFailure = ECk_HexBoardFailure::None;
        auto ObjectFailure = ECk_HexGridObject_Failure::None;
        FCk_HexGridObject_Spec ObjectSpec;
        TArray<FCk_HexCoord> Offsets;
        Offsets.Add(FCk_HexCoord());
        ObjectSpec.Set_FootprintOffsets(Offsets);
        if (!utils_hex_grid::TryCreate(InHandle, FTransform::Identity, BoardSpec, _Board, BoardFailure)
            || !utils_hex_grid_object::TryCreate(InHandle, ObjectSpec, _Object, ObjectFailure))
        { FinishFailure("Placement fixture failed to compose"); return; }
        FCk_HexGridPlacement_Spec Spec;
        Spec.Set_Object(_Object);
        auto Preview = utils_hex_grid_placement::Get_CanPlace(_Board, Spec);
        if (!Preview._Succeeded || Preview._PlacementId.IsValid() || Preview._Placement._Cells.Num() != 1)
        { FinishFailure("Fresh preview must have one cell and no ID"); return; }
        auto Req = FCk_Request_HexGridPlacement_Place(Spec);
        Req.Set_OnResult(FCk_Delegate_HexGridPlacement_OnResult(this, n"OnTyped"));
        utils_hex_grid_placement::Request_Place(_Board, Req, FCk_Delegate_Request_OnCompleted(this, n"OnGeneric"));
    }

    UFUNCTION()
    void OnTyped(const FCk_HexGridPlacement_Result&in InResult)
    {
        ++_Typed;
        if (!InResult._Succeeded || !InResult._PlacementId.IsValid())
        { FinishFailure("Expected successful terminal result with a placement ID"); return; }
        if (_Typed == 1) { _Id = InResult._PlacementId; }
        if (_Id != InResult._PlacementId) { FinishFailure("Placement ID changed"); return; }
        auto Readback = utils_hex_grid_occupancy::TryGet_Placement(_Board, _Id);
        if (_Typed == 3)
        {
            if (Readback._Succeeded || Readback._Failure != ECk_HexGridFailure::InvalidPlacementId)
            { FinishFailure("Removed ID still resolves"); }
            return;
        }
        auto Handle = Readback._Placement._Handle;
        if (!Readback._Succeeded || !ck::IsValid(Handle) || !ck::IsValid(Handle.H())
            || Readback._Placement._Anchor._Q != _Typed - 1)
        { FinishFailure("Typed terminal did not observe committed placement"); }
    }

    UFUNCTION()
    void OnGeneric(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_Generic;
        if (_Generic != _Typed || InResult != ECk_Request_OperationResult::Succeeded || InOwner != _Board.H())
        { FinishFailure("Expected one typed result before each successful generic completion"); return; }
        auto Typed = FCk_Delegate_HexGridPlacement_OnResult(this, n"OnTyped");
        auto Done = FCk_Delegate_Request_OnCompleted(this, n"OnGeneric");
        if (_Generic == 1)
        {
            FCk_HexCoord Anchor;
            Anchor.Set_Q(1);
            auto Req = FCk_Request_HexGridPlacement_Move(_Id, Anchor, ECk_HexRotation::Turn0);
            Req.Set_OnResult(Typed);
            utils_hex_grid_placement::Request_Move(_Board, Req, Done);
        }
        else if (_Generic == 2)
        {
            auto Req = FCk_Request_HexGridPlacement_Remove(_Id);
            Req.Set_OnResult(Typed);
            utils_hex_grid_placement::Request_Remove(_Board, Req, Done);
        }
        else if (_Generic == 3) { FinishSuccess(); }
    }
}
