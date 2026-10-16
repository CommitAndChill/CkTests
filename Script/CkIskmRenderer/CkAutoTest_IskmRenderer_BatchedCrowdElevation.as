// Language=angelscript

//============================================================================
// CK ISKM RENDERER - AUTOMATION TEST: BATCHED CROWD TILE BOUNDS FOLLOW MEMBER HEIGHT
//============================================================================
//
// Tiles bucket members by XY only and sit at world Z 0. Their primitive bounds used to span only the mesh
// height around Z 0, so a member standing on a hill was wholly outside its tile's bounds and frustum/HZB
// culling dropped the whole tile - far NPCs on BusterBlock's main-map hill rendered as floating cosmetics.
//
// Asserts the tile's culling box (what the scene receives) contains every visible member's position and body
// across: two members at different heights in one tile, an in-tile climb, an in-tile descent below zero, the
// box shrinking back once the member returns to ground level, and a member that moved while hidden.
//
// NOT covered (needs a human with RHI): that the hill NPCs actually draw - see the main-map hill repro.
//============================================================================

class UCk_AutoTest_IskmRenderer_BatchedCrowdElevation : UCk_AutoTest_Base
{
    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Collection = iskm_assets::AnimCollection_Demo();
        if (ck::Is_NOT_Valid(Collection))
        {
            FinishFailure("iskm_assets::AnimCollection_Demo() invalid - registry may need regeneration.");
            return;
        }
        UCk_Utils_IskmAnimCollection_UE::Build_BakedPoseData(Collection);

        // Two members 200cm apart around (500, 500): both inside tile (0, 0) with 2000cm tiles.
        auto Crowd = UCk_Utils_IskmBatched_UE::Debug_SpawnScatteredCrowd(
            Collection, FTransform(FVector(500.0f, 500.0f, 0.0f)), 2, 100.0f, 2000.0f, 0, 1.0f);
        if (ck::Is_NOT_Valid(Crowd))
        {
            FinishFailure("Debug_SpawnScatteredCrowd should return a valid crowd actor");
            return;
        }
        Assert_Equals_Int(UCk_Utils_IskmBatched_UE::Get_CrowdTileCount(Crowd), 1, "both members share one tile");

        const auto Ground0 = UCk_Utils_IskmBatched_UE::Get_CrowdMemberTransform(Crowd, 0).GetTranslation();
        const auto Ground1 = UCk_Utils_IskmBatched_UE::Get_CrowdMemberTransform(Crowd, 1).GetTranslation();
        Assert_Contains(Crowd, 0, "ground-level member");

        // In-tile climb onto a hill: the tile must follow member 0 up and keep member 1 at ground level.
        MoveTo(Crowd, 0, Ground0 + FVector(0.0f, 0.0f, 5000.0f));
        Assert_Equals_Int(UCk_Utils_IskmBatched_UE::Get_CrowdTileCount(Crowd), 1, "a vertical move stays in its tile");
        Assert_Contains(Crowd, 0, "member climbed to Z+5000");
        Assert_Contains(Crowd, 1, "ground member sharing the tile with a climbed one");

        MoveTo(Crowd, 0, Ground0 + FVector(0.0f, 0.0f, 8000.0f));
        Assert_Contains(Crowd, 0, "member climbed further to Z+8000");

        // Below zero (a basement / a pit): both directions are covered.
        MoveTo(Crowd, 0, Ground0 + FVector(0.0f, 0.0f, -3000.0f));
        Assert_Contains(Crowd, 0, "member descended to Z-3000");
        Assert_Contains(Crowd, 1, "ground member while the other is below zero");

        // Back to ground. Moving INSIDE the box re-fits nothing (still conservative, never hidden); the next tile
        // rebuild - which the LOD flip's hide/show triggers constantly - re-fits tightly, so the box does not stay
        // hill-tall forever.
        MoveTo(Crowd, 0, Ground0);
        Assert_Contains(Crowd, 0, "member back at ground level");
        UCk_Utils_IskmBatched_UE::Set_CrowdMemberVisible(Crowd, 0, false);
        UCk_Utils_IskmBatched_UE::Set_CrowdMemberVisible(Crowd, 0, true);
        const auto Settled = UCk_Utils_IskmBatched_UE::Get_CrowdMemberTileWorldBounds(Crowd, 0);
        Assert_True(Settled.Max.Z < Ground0.Z + 3000.0f && Settled.Min.Z > Ground0.Z - 2000.0f,
            f"tile Z range re-fits to the members after a rebuild once they are back on the ground (got [{Settled.Min.Z}, {Settled.Max.Z}])");

        // Moved while hidden (the LOD flip hides far members while their SKMC stand-in walks): showing it rebuilds
        // the tile, which must re-fit to where the member now stands.
        UCk_Utils_IskmBatched_UE::Set_CrowdMemberVisible(Crowd, 1, false);
        MoveTo(Crowd, 1, Ground1 + FVector(0.0f, 0.0f, 6000.0f));
        UCk_Utils_IskmBatched_UE::Set_CrowdMemberVisible(Crowd, 1, true);
        Assert_Contains(Crowd, 1, "member shown after moving to Z+6000 while hidden");
        Assert_Contains(Crowd, 0, "ground member after its tile-mate reappeared up high");

        Crowd.DestroyActor();
        FinishSuccess();
    }

    void MoveTo(ACk_Iskm_BatchedCrowd_Actor InCrowd, int32 InIndex, FVector InLocation)
    {
        UCk_Utils_IskmBatched_UE::Set_CrowdMemberTransform(InCrowd, InIndex, FTransform(InLocation));
    }

    void Assert_Contains(ACk_Iskm_BatchedCrowd_Actor InCrowd, int32 InIndex, const FString& InWhat)
    {
        const auto Loc = UCk_Utils_IskmBatched_UE::Get_CrowdMemberTransform(InCrowd, InIndex).GetTranslation();
        const auto Bounds = UCk_Utils_IskmBatched_UE::Get_CrowdMemberTileWorldBounds(InCrowd, InIndex);
        const bool ContainsFeet =
            Loc.X >= Bounds.Min.X && Loc.X <= Bounds.Max.X &&
            Loc.Y >= Bounds.Min.Y && Loc.Y <= Bounds.Max.Y &&
            Loc.Z >= Bounds.Min.Z && Loc.Z <= Bounds.Max.Z;
        // 100cm is below any character's height, so "box top clears this above the feet" means the body is inside.
        const float32 MinBodyHeight = 100.0f;
        Assert_True(ContainsFeet && Bounds.Max.Z >= Loc.Z + MinBodyHeight,
            f"{InWhat}: member {InIndex} at {Loc.ToString()} must be inside its tile's culling box [{Bounds.Min.ToString()} .. {Bounds.Max.ToString()}]");
    }
}

class ACk_AutoTest_IskmRenderer_BatchedCrowdElevation_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_IskmRenderer_BatchedCrowdElevation;
    default _TimeoutSeconds = 15.0f;
}
