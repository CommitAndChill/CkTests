// Language=angelscript

// One authored world layout shared by authority source origins and each
// player's independent three Common station shells.
namespace CkSwayGymCatalog
{
    const int32 StationCount = 3;

    FString Get_Tag(int32 Station)
    {
        if (Station == 0) { return "Gym.CkSway.Turntable"; }
        if (Station == 1) { return "Gym.CkSway.Shuttle"; }
        if (Station == 2) { return "Gym.CkSway.Compare"; }
        return "";
    }

    FString Get_Title(int32 Station)
    {
        if (Station == 0) { return "TURNTABLE - YAW SWAY"; }
        if (Station == 1) { return "SHUTTLE - LINEAR SWAY"; }
        if (Station == 2) { return "COMPARE - DEFAULT / HEAVY / DISABLED"; }
        return "";
    }

    bool TryGet_Station(int32 Station, FCkGym_Station_SpawnParams_Payload&out Out)
    {
        Out = FCkGym_Station_SpawnParams_Payload();
        if (Station < 0 || Station >= StationCount) { return false; }
        Out.Tags.Add(FName(Get_Tag(Station)));
        Out.Transform = FTransform(FVector(100.0 + 3600.0 * (Station % 2),
            3000.0 * Math::IntegerDivisionTrunc(Station, 2), 0.0));
        Out.AutoSize = true;
        Out.Title = FText::FromString(Get_Title(Station));
        if (Station == 0)
        {
            Out.Description.Add(FText::FromString("The orange head yaws -90..+90 deg every 2 s. The large cube hangs on a default sway; the small cube is its rigid twin."));
            Out.Description.Add(FText::FromString("Watch the large cube lag opposite the turn, overshoot slightly, and settle on the twin at each end."));
        }
        else if (Station == 1)
        {
            Out.Description.Add(FText::FromString("The head shuttles +-400 cm on X every 2 s. The large cube trails the move and catches up at each end."));
        }
        else
        {
            Out.Description.Add(FText::FromString("Turn and shuttle together. Left: default spec. Middle: heavy (slower springs, less damping, double gains). Right: disabled."));
            Out.Description.Add(FText::FromString("The disabled cube must stay rigid on the head. O draws each sway's offset readout (red = disabled)."));
        }
        return true;
    }

    bool TryGet_Origin(int32 Station, FVector&out Out)
    {
        Out = FVector::ZeroVector;
        FCkGym_Station_SpawnParams_Payload Recipe;
        if (!TryGet_Station(Station, Recipe)) { return false; }
        Out = Recipe.Transform.GetLocation() + FVector(1300.0, 0.0, 150.0);
        return true;
    }
}
