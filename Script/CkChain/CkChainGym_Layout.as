namespace CkChainGym
{
    FString Get_Tag(int32 Index)
    {
        if (Index == 0) { return "Gym.CkChain.Train"; }
        if (Index == 1) { return "Gym.CkChain.Rope"; }
        if (Index == 2) { return "Gym.CkChain.Held"; }
        if (Index == 3) { return "Gym.CkChain.WagonsWithChildren"; }
        return "";
    }

    FTransform Get_StationTransform(int32 Index)
    {
        if (Index < 0 || Index >= 4) { return FTransform::Identity; }
        return FTransform(FVector(100.0 + 3600.0 * (Index % 2),
            3000.0 * Math::IntegerDivisionTrunc(Index, 2), 0.0));
    }

    FVector Get_Origin(int32 Index)
    { return Get_StationTransform(Index).GetLocation() + FVector(1300.0, 0.0, 100.0); }

    bool IsFiniteVector(FVector Value)
    { return Math::IsFinite(Value.X) && Math::IsFinite(Value.Y) && Math::IsFinite(Value.Z); }

    bool IsFinitePose(FTransform Pose)
    {
        return Pose.IsValid() && !Pose.ContainsNaN() &&
            IsFiniteVector(Pose.GetLocation()) && IsFiniteVector(Pose.GetScale3D());
    }

    bool IsValidFrame(const FCkChainGym_WireSnapshot&in Frame)
    {
        if (Frame.SourceGeneration != 1 || Frame.ControlGeneration <= 0 ||
            Frame.Sequence <= 0) { return false; }
        if (Frame.Phase == 4) { return Frame.Replacing == -1; }
        if ((Frame.Phase != 2 && Frame.Phase != 3) || Frame.Stations.Num() != 4 ||
            (Frame.Phase == 2 && Frame.Replacing != -1) ||
            (Frame.Phase == 3 && Frame.Replacing != -1 && Frame.Replacing != 0))
        { return false; }
        for (int32 I = 0; I < 4; ++I)
        {
            auto S = Frame.Stations[I];
            const int32 Count = I == 1 ? 12 : 8;
            if (S.Index != I || S.Generation <= 0 || S.Origin != Get_Origin(I) ||
                !IsFinitePose(S.Head) || S.Links.Num() != Count ||
                S.Lamps.Num() != (I == 3 ? 8 : 0) || S.History.Num() > 512 ||
                S.SplitHistory.Num() > 512 ||
                (S.Spacing != 4.0f && S.Spacing != 10.0f && S.Spacing != 25.0f) ||
                (S.FollowingPawn && (I != 1 || !S.Moving))) { return false; }
            if (S.HasSplit)
            {
                if (I != 0 || S.LinkCount != 3 || S.SplitLinkCount != 4) { return false; }
            }
            else if (S.LinkCount != Count || S.SplitLinkCount != 0 ||
                !S.SplitHistory.IsEmpty()) { return false; }
            for (auto L : S.Links)
            {
                if (!IsFinitePose(L.Pose) || !IsFiniteVector(L.TargetLocation)) { return false; }
            }
            for (auto Pose : S.Lamps) { if (!IsFinitePose(Pose)) { return false; } }
            for (auto Point : S.History) { if (!IsFiniteVector(Point)) { return false; } }
            for (auto Point : S.SplitHistory) { if (!IsFiniteVector(Point)) { return false; } }
        }
        return true;
    }
}
