class UCk_AutoTest_HexGrid_Persistence : UCk_AutoTest_Base
{
    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        FCk_Handle_HexGrid Invalid;
        auto Missing = utils_hex_grid::Get_Readiness(Invalid);
        if (Missing._State != ECk_HexGridReadiness::Unavailable
            || Missing._Failure != ECk_HexGridFailure::InvalidGrid
            || utils_hex_grid::Get_IsReady(Invalid)
            || utils_hex_grid::Get_IsPending(Invalid))
        { FinishFailure("Default handle readiness must be unavailable, invalid, and not pending"); return; }

        FCk_Handle_HexGrid Board;
        FCk_HexGrid_Spec Spec;
        auto Failure = ECk_HexBoardFailure::None;
        if (!utils_hex_grid::TryCreate(InHandle, FTransform::Identity, Spec, Board, Failure))
        { FinishFailure("Fresh readiness fixture did not compose"); return; }
        auto Readiness = utils_hex_grid::Get_Readiness(Board);
        auto Placements = utils_hex_grid_occupancy::Get_Placements(Board);
        if (Readiness._State != ECk_HexGridReadiness::Ready
            || Readiness._Failure != ECk_HexGridFailure::None
            || !utils_hex_grid::Get_IsReady(Board)
            || utils_hex_grid::Get_IsPending(Board)
            || !Placements._Succeeded || Placements._Placements.Num() != 0)
        { FinishFailure("Fresh readiness and occupancy readback disagree"); return; }
        FinishSuccess();
    }
}
