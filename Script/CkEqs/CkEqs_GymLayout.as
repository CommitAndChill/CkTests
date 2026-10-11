// Language=angelscript

namespace CkEqsGym
{
    FString Get_BaseTag(int32 Index)
    {
        if (Index == 0) { return "Gym.Eqs.SimpleGrid"; }
        if (Index == 1) { return "Gym.Eqs.Donut"; }
        if (Index == 2) { return "Gym.Eqs.Cone"; }
        if (Index == 3) { return "Gym.Eqs.Immediate"; }
        if (Index == 4) { return "Gym.Eqs.RandomBest"; }
        if (Index == 5) { return "Gym.Eqs.NavProjection"; }
        if (Index == 6) { return "Gym.Eqs.OnCircle"; }
        if (Index == 7) { return "Gym.Eqs.VolumeCheck"; }
        if (Index == 8) { return "Gym.Eqs.Random"; }
        if (Index == 9) { return "Gym.Eqs.Trace"; }
        if (Index == 10) { return "Gym.Eqs.EntitiesWithTag"; }
        if (Index == 11) { return "Gym.Eqs.Overlap"; }
        return "";
    }

    FTransform Get_StationTransform(int32 Index)
    {
        if (Index < 0 || Index >= 12) { return FTransform::Identity; }
        int32 Row = Index < 7 ? 0 : 1;
        int32 Column = Index < 7 ? Index : Index - 7;
        int32 RowCount = Row == 0 ? 7 : 5;
        return FTransform(FRotator(0.0, 180.0, 0.0),
            FVector(500.0 + Row * 800.0,
                -(RowCount - 1) * 400.0 + Column * 800.0, 0.0));
    }

    FTransform Get_SourceTransform(int32 Index)
    {
        auto Station = Get_StationTransform(Index);
        if (Index < 0 || Index >= 12) { return Station; }
        return FTransform(Station.Rotator(),
            Station.GetLocation() + FVector(0.0, 0.0, 250.0),
            FVector(1.0, 1.0, 1.0));
    }

    FString Get_Title(int32 Index)
    {
        if (Index == 0) { return "SIMPLEGRID + DISTANCE"; }
        if (Index == 1) { return "DONUT + DOT"; }
        if (Index == 2) { return "CONE GENERATOR"; }
        if (Index == 3) { return "IMMEDIATE SYNC"; }
        if (Index == 4) { return "RANDOM BEST 5%"; }
        if (Index == 5) { return "NAVPROJECTION"; }
        if (Index == 6) { return "ONCIRCLE GENERATOR"; }
        if (Index == 7) { return "VOLUMECHECK FILTER"; }
        if (Index == 8) { return "RANDOM TIEBREAKER"; }
        if (Index == 9) { return "TRACE (LOS)"; }
        if (Index == 10) { return "ENTITIESWITHTAG"; }
        if (Index == 11) { return "OVERLAP TEST"; }
        return "";
    }

    FString Get_Description(int32 Index)
    {
        if (Index == 0) { return "5x5 grid centered on station, 100uu spacing.\nDistance test (InverseLinear) scores nearest highest.\nSingleBest mode returns one candidate."; }
        if (Index == 1) { return "3 rings (200-500uu) x 8 points = 24 candidates.\nDot test (NormalizedDot) favours forward-facing.\nWatch best-pick rotate as querier rotation changes."; }
        if (Index == 2) { return "Forward 90deg cone, 500uu range, 15deg/100uu step.\nNo tests - generator output only.\nAllMatchingSorted returns every candidate."; }
        if (Index == 3) { return "Same query as SimpleGrid + Distance,\nrouted through Request_RunQuery_Immediate.\nResults available before the call returns.\nNo OnComplete broadcast (P3-E5)."; }
        if (Index == 4) { return "SimpleGrid + Distance, RandomBest5Pct mode.\nFinal pick is sampled from the top 5%\nof scored candidates each run."; }
        if (Index == 5) { return "SimpleGrid 5x5 + _ProjectOntoNav=true.\nCandidates snap to the floor's navmesh.\nThe floor must be navmesh-bakeable\n(scale Z >= 0.5)."; }
        if (Index == 6) { return "8 evenly-spaced points on a 300uu ring.\nDistance test for scoring; AllMatchingSorted\nreturns every candidate ranked."; }
        if (Index == 7) { return "SimpleGrid filtered by an AABB at the\nstation origin (HalfExtent 150,150,50).\nFilterMin=0.5 eliminates raw=0 candidates.\nResult: 3x3 = 9 of 25 candidates survive."; }
        if (Index == 8) { return "SimpleGrid + Distance score + Random tiebreaker.\nRandom test (Weight=0.1) breaks deterministic\nordering on equal-distance candidates each run."; }
        if (Index == 9) { return "SimpleGrid + LineTrace LOS test.\nThe wall spawned beside this station blocks\nLOS for some candidates; FilterMin=0.5\ndrops the blocked ones."; }
        if (Index == 10) { return "Generator returns 5 child entities\nspawned by the station on a 250uu ring,\neach tagged Gym.Eqs.Target.\nDistance test picks the closest."; }
        if (Index == 11) { return "SimpleGrid + Overlap test.\nThree probe markers (Gym.Eqs.OverlapMarker)\nspawned by the station near the grid;\ncandidates near a marker score higher."; }
        return "";
    }

    bool IsValidFrame(const FCkEqsGym_WireSnapshot&in Frame)
    {
        if (Frame.SourceGeneration != 1 || Frame.ControlGeneration <= 0 ||
            Frame.Sequence <= 0 || Frame.Replacing != -1) { return false; }
        if (Frame.Phase == 4) { return true; }
        if ((Frame.Phase != 2 && Frame.Phase != 3) ||
            Frame.Stations.Num() != 12) { return false; }
        for (int32 I = 0; I < 12; ++I)
        {
            auto Station = Frame.Stations[I];
            if (Station.Index != I || Station.Generation <= 0 ||
                Station.Title.Len() > 128 || Station.Detail.Len() > 4096 ||
                Station.Footer.Len() > 1024 ||
                Station.RunCount < 0 || Station.RunCount > 2147483647 ||
                Station.CandidateCount < 0 || Station.CandidateCount > 10000 ||
                !Math::IsFinite(Station.BestLocation.X) ||
                !Math::IsFinite(Station.BestLocation.Y) ||
                !Math::IsFinite(Station.BestLocation.Z) ||
                !Math::IsFinite(Station.SecondsLeft) ||
                Station.SecondsLeft < 0.0f || Station.SecondsLeft > 6.5f ||
                Station.IsImmediate != (I == 3)) { return false; }
        }
        return true;
    }
}
