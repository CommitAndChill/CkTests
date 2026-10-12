namespace CkTweenGym
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        const int32 Count = int32(ECk_TweenEasing::ECk_MAX);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            auto Easing = ECk_TweenEasing(Index);
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(f"Gym.Tween.{Easing}"));
            Station.Title = FText::FromString(f"{Easing}");
            Station.Description.Add(FText::FromString("Demonstrates tween easing behavior."));
            Station.Description.Add(FText::FromString("Cube oscillates vertically using this easing function."));
            const int32 Row = Math::IntegerDivisionTrunc(Index, 7);
            const int32 Column = Index % 7;
            const int32 InRow = Math::Min(7, Count - Row * 7);
            const float Y = -(InRow - 1) * 400.0f + Column * 800.0f;
            Station.Transform = FTransform(FRotator(0.0f, 180.0f, 0.0f),
                FVector(500.0f + Row * 800.0f, Y, 0.0f));
            Stations.Add(Station);
        }
        return Stations;
    }

    FVector Get_ActorLocation(int32 InIndex)
    {
        auto Stations = Get_Stations();
        if (InIndex < 0 || InIndex >= Stations.Num()) { return FVector::ZeroVector; }
        // Default GymStation PanelCenter: 5 m height, halfway up.
        return Stations[InIndex].Transform.TransformPosition(FVector(0.0f, 0.0f, 250.0f));
    }
}
