// Language=angelscript

class UCk_AutoTest_Net_ProjectionConsumer : UCk_AutoTest_NetBase
{
    default _NetSubjectClass = ACk_AutoTest_NetSubject_MixedGrid_UE;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        ObserveBoard();
    }

    private void ObserveBoard()
    {
        auto Entity = Get_SubjectEntity();
        if (!ck::IsValid(Entity))
        {
            WaitOneFrame(n"ObserveNextFrame");
            return;
        }
        auto Actor = Cast<ACk_AutoTest_NetSubject_MixedGrid_UE>(utils_entity_bridge::Get_Actor(Entity));
        if (Actor == nullptr)
        {
            FinishFailure("Missing replicated mixed-grid subject");
            return;
        }
        auto Board = Actor.Get_HexGrid();
        if (!ck::IsValid(Board) || utils_hex_grid::Get_IsPending(Board))
        {
            WaitOneFrame(n"ObserveNextFrame");
            return;
        }
        auto InitialState = Actor.Get_ConstructReadiness()._State;
        auto ExpectedState = Actor.GetWorld().GetNetMode() == ENetMode::NM_Client
            ? ECk_HexGridReadiness::AwaitingReplication : ECk_HexGridReadiness::Ready;
        if (!ck::IsValid(Board.H()) || InitialState != ExpectedState)
        {
            FinishFailure("Board construction exposed the wrong initial readiness for its role");
            return;
        }
        auto Result = utils_hex_grid_occupancy::Get_Placements(Board);
        if (!utils_hex_grid::Get_IsReady(Board) || !Result._Succeeded || Result._Placements.Num() != 0)
        {
            FinishFailure("Expected a ready board with an accepted empty projection");
            return;
        }
        FinishSuccess();
    }

    UFUNCTION()
    private void ObserveNextFrame(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (!IsFinished()) { ObserveBoard(); }
    }
}
