// Language=angelscript

namespace CkGoapAutoReplanGym
{
    FString Get_BaseTag(int32 Index)
    {
        if (Index == 0) { return "Gym.Goap.AutoReplan.Explicit"; }
        if (Index == 1) { return "Gym.Goap.AutoReplan.OnWSDirty"; }
        if (Index == 2) { return "Gym.Goap.AutoReplan.OnCostDirty"; }
        return "";
    }

    FTransform Get_StationTransform(int32 Index)
    {
        if (Index < 0 || Index >= 3) { return FTransform::Identity; }
        return FTransform(FRotator(0.0, 180.0, 0.0),
            FVector(500.0, -800.0 + Index * 800.0, 0.0));
    }

    // The authored AutoSize station starts at its two-unit minimum height.
    // Its original PanelCenter anchor was therefore 100 cm above the footprint.
    FTransform Get_SourceTransform(int32 Index)
    {
        auto Station = Get_StationTransform(Index);
        if (Index < 0 || Index >= 3) { return Station; }
        return FTransform(Station.Rotator(),
            Station.GetLocation() + FVector(0.0, 0.0, 100.0),
            FVector(1.0, 1.0, 1.0));
    }

    FString Get_Title(int32 Index)
    {
        if (Index == 0) { return "STATION A / EXPLICIT"; }
        if (Index == 1) { return "STATION B / OnWSDirty"; }
        if (Index == 2) { return "STATION C / OnCostDirty"; }
        return "";
    }

    FString Get_Description(int32 Index)
    {
        if (Index == 0) { return "Manual replan only.\nUse the control panel's [R] row."; }
        if (Index == 1) { return "Throttled to one replan per 0.5s window."; }
        if (Index == 2) { return "Replan triggered by cost changes."; }
        return "";
    }

    bool IsValidFrame(const FCkGoapAutoReplanGym_WireSnapshot&in Frame)
    {
        if (Frame.SourceGeneration != 1 || Frame.ControlGeneration <= 0 ||
            Frame.Sequence <= 0 || Frame.Replacing != -1) { return false; }
        if (Frame.Phase == 4) { return true; }
        if ((Frame.Phase != 2 && Frame.Phase != 3) ||
            Frame.Stations.Num() != 3) { return false; }
        for (int32 I = 0; I < 3; ++I)
        {
            auto Station = Frame.Stations[I];
            if (Station.Index != I || Station.Generation <= 0 ||
                Station.Title.Len() > 128 || Station.Detail.Len() > 4096 ||
                Station.Footer.Len() > 1024 || Station.Mode != I ||
                Station.PlanCount < 0 || Station.PlanCount > 2147483647 ||
                Station.WorldStateChangeCount < 0 ||
                Station.WorldStateChangeCount > 2147483647 ||
                Station.PlanStatus < 0 || Station.PlanStatus > 4 ||
                Station.PlanLength < 0 || Station.PlanLength > 2 ||
                Station.FirstAction < 0 || Station.FirstAction > 2 ||
                (Station.Mode != 2 && Station.AltCostLowered))
            { return false; }
        }
        return true;
    }
}
