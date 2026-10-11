// Language=angelscript

namespace CkDialogGym
{
    FString Get_Tag(int32 Index)
    {
        if (Index == 0) { return "Gym.Dialog.Basics"; }
        if (Index == 1) { return "Gym.Dialog.Filters"; }
        if (Index == 2) { return "Gym.Dialog.Cooldowns"; }
        if (Index == 3) { return "Gym.Dialog.Chains"; }
        if (Index == 4) { return "Gym.Dialog.Graph"; }
        return "";
    }

    FTransform Get_StationTransform(int32 Index)
    {
        if (Index < 0 || Index >= 5) { return FTransform::Identity; }
        return FTransform(FRotator(0.0, 180.0, 0.0),
            FVector(500.0, (Index - 2) * 800.0, 0.0));
    }

    FTransform Get_SourceTransform(int32 Index)
    {
        auto Station = Get_StationTransform(Index);
        if (Index < 0 || Index >= 5) { return Station; }
        return FTransform(Station.Rotator(),
            Station.GetLocation() + FVector(0.0, 0.0, 250.0),
            FVector(1.0, 1.0, 1.0));
    }

    bool IsValidFrame(const FCkDialogGym_WireSnapshot&in Frame)
    {
        if (Frame.SourceGeneration != 1 || Frame.ControlGeneration <= 0 ||
            Frame.Sequence <= 0) { return false; }
        if (Frame.Phase == 4) { return Frame.Replacing == -1; }
        if (Frame.Stations.Num() != 5 ||
            (Frame.Phase != 2 && Frame.Phase != 3) ||
            (Frame.Phase == 2 && Frame.Replacing != -1) ||
            (Frame.Phase == 3 && (Frame.Replacing < -1 || Frame.Replacing >= 5)))
        { return false; }
        for (int32 I = 0; I < 5; ++I)
        {
            auto Station = Frame.Stations[I];
            if (Station.Index != I || Station.Generation <= 0 ||
                Station.Title.Len() > 128 || Station.Detail.Len() > 4096 ||
                Station.Footer.Len() > 256 ||
                Station.QueryCount < 0 || Station.QueryCount > 2147483647 ||
                !Math::IsFinite(Station.CooldownRemaining) ||
                Station.CooldownRemaining < 0.0f || Station.CooldownRemaining > 3.1f ||
                Station.ChainStep < 0 || Station.ChainStep > 3 ||
                (I != 2 && Station.CooldownRemaining != 0.0f) ||
                (I != 1 && I != 2 && Station.OtherResults.Num() != 0) ||
                (I != 3 && Station.ChainStep != 0) ||
                (I != 3 && I != 4 && !Station.ActiveLineID.IsNone()) ||
                Station.Results.Num() > 16 || Station.OtherResults.Num() > 16 ||
                (I == 4 && Station.GraphNodeStates.Num() != 4) ||
                (I != 4 && Station.GraphNodeStates.Num() != 0)) { return false; }
            for (auto Result : Station.Results)
            {
                if (Result.LineID.IsNone() || Result.LineID.ToString().Len() > 128 ||
                    Result.Text.Len() > 1024 || Result.Result < 0 || Result.Result > 2)
                { return false; }
            }
            for (auto Result : Station.OtherResults)
            {
                if (Result.LineID.IsNone() || Result.LineID.ToString().Len() > 128 ||
                    Result.Text.Len() > 1024 || Result.Result < 0 || Result.Result > 2)
                { return false; }
            }
            for (auto State : Station.GraphNodeStates)
            { if (State < 0 || State > 2) { return false; } }
            if (I == 3 && Station.ActiveLineID != FName() &&
                Station.ActiveLineID != n"DialogGym.Chains.A" &&
                Station.ActiveLineID != n"DialogGym.Chains.B" &&
                Station.ActiveLineID != n"DialogGym.Chains.C") { return false; }
            if (I == 4 && Station.ActiveLineID != FName() &&
                Station.ActiveLineID != n"Graph.Greeting" &&
                Station.ActiveLineID != n"Graph.Friendly" &&
                Station.ActiveLineID != n"Graph.Farewell") { return false; }
        }
        return true;
    }
}
