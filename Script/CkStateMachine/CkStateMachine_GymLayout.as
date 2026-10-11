namespace CkStateMachineGym
{
    FString Get_Tag(int32 Index)
    {
        if (Index == 0) { return "Gym.StateMachine.AutoCycle"; }
        if (Index == 1) { return "Gym.StateMachine.PauseResume"; }
        if (Index == 2) { return "Gym.StateMachine.Complex"; }
        if (Index == 3) { return "Gym.StateMachine.Hierarchical"; }
        if (Index == 4) { return "Gym.StateMachine.GraphWalkRegression"; }
        if (Index == 5) { return "Gym.StateMachine.DivergenceFirstBranch"; }
        if (Index == 6) { return "Gym.StateMachine.DivergenceTimed"; }
        if (Index == 7) { return "Gym.StateMachine.RacingEventDriven"; }
        if (Index == 8) { return "Gym.StateMachine.EventDrivenMultiCondition"; }
        return "";
    }

    FTransform Get_StationTransform(int32 Index)
    {
        if (Index < 0 || Index >= 9) { return FTransform::Identity; }
        // The station shell's default dimensions are 6 x 5 x 5 metres.
        // Three rows keep the same 800 cm spacing as the base gym grid.
        const int32 Row = Math::IntegerDivisionTrunc(Index, 3);
        const int32 Column = Index % 3;
        return FTransform(FRotator(0.0, 180.0, 0.0),
            FVector(500.0 + Row * 800.0, (Column - 1) * 800.0, 0.0));
    }

    FTransform Get_SourceTransform(int32 Index)
    {
        auto Station = Get_StationTransform(Index);
        if (Index < 0 || Index >= 9) { return Station; }
        // PanelCenter is half the station's default 500 cm height.
        return FTransform(Station.Rotator(),
            Station.GetLocation() + FVector(0.0, 0.0, 250.0),
            FVector(1.0, 1.0, 1.0));
    }
}
