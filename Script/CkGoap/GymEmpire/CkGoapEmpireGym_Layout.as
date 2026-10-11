// Language=angelscript

namespace CkGoapEmpireGym
{
    FString Get_BaseTag(int32 Index)
    { return Index == 0 ? "Gym.Goap.Empire.Research" : ""; }

    // One station occupies the center of the base controller's 800 cm grid.
    FTransform Get_StationTransform(int32 Index)
    {
        if (Index != 0) { return FTransform::Identity; }
        return FTransform(FRotator(0.0, 180.0, 0.0),
            FVector(500.0, 0.0, 0.0));
    }

    // The one-line AutoSize payload starts at minimum height 200 cm.
    FTransform Get_SourceTransform(int32 Index)
    {
        auto Station = Get_StationTransform(Index);
        if (Index != 0) { return Station; }
        return FTransform(Station.Rotator(),
            Station.GetLocation() + FVector(0.0, 0.0, 100.0),
            FVector(1.0, 1.0, 1.0));
    }

    FString Get_Title(int32 Index)
    { return Index == 0 ? "EMPIRE / FEUDAL RESEARCH" : ""; }

    FString Get_Description(int32 Index)
    { return Index == 0 ? "5-step empire plan. Goal: FeudalResearched=true." : ""; }

    bool IsValidFrame(const FCkGoapEmpireGym_WireSnapshot&in Frame)
    {
        if (Frame.SourceGeneration != 1 || Frame.ControlGeneration <= 0 ||
            Frame.Sequence <= 0 || Frame.Replacing != -1) { return false; }
        if (Frame.Phase == 4) { return true; }
        if ((Frame.Phase != 2 && Frame.Phase != 3) ||
            Frame.Stations.Num() != 1) { return false; }
        auto Station = Frame.Stations[0];
        if (Station.Index != 0 || Station.Generation != 1 ||
            Station.Title.Len() > 128 || Station.Detail.Len() > 4096 ||
            Station.Footer.Len() > 1024 ||
            Station.WorldStateMask < 0 || Station.WorldStateMask > 31 ||
            Station.PlanCount < 0 || Station.PlanCount > 2147483647 ||
            Station.PlanStatus < 0 || Station.PlanStatus > 4 ||
            Station.PlanActions.Num() > 6 ||
            !Math::IsFinite(Station.PlanCost) || Station.PlanCost < 0.0f)
        { return false; }
        for (auto Action : Station.PlanActions)
        { if (Action < 0 || Action > 5) { return false; } }
        return true;
    }
}
