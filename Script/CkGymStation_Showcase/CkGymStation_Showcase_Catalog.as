// Full authored station definitions extracted without reducing tuners to the common payload.
namespace CkStationShowcase
{
    const int32 SlotCount = 8;
    const int32 CatalogVersion = 1;

    FName Get_StationTag(int32 InSlot)
    {
        if (InSlot == 0) { return n"Gym.StationShowcase.AutoSize.Wide"; }
        if (InSlot == 1) { return n"Gym.StationShowcase.AutoSize.Tall"; }
        if (InSlot == 2) { return n"Gym.StationShowcase.AutoSize.Runtime"; }
        if (InSlot == 3) { return n"Gym.StationShowcase.Default"; }
        if (InSlot == 4) { return n"Gym.StationShowcase.Small"; }
        if (InSlot == 5) { return n"Gym.StationShowcase.Large"; }
        if (InSlot == 6) { return n"Gym.StationShowcase.NoSpotlight"; }
        if (InSlot == 7) { return n"Gym.StationShowcase.RightAlign"; }
        return n"";
    }

    FCk_GymStation_SpawnParams Get_Params(int32 InSlot)
    {
        FCk_GymStation_SpawnParams Params;
        if (InSlot == 0) { Params = Get_AutoSize_Wide(); }
        if (InSlot == 1) { Params = Get_AutoSize_Tall(); }
        if (InSlot == 2) { Params = Get_AutoSize_Runtime(); }
        if (InSlot == 3) { Params = Get_Default(); }
        if (InSlot == 4) { Params = Get_Small(); }
        if (InSlot == 5) { Params = Get_Large(); }
        if (InSlot == 6) { Params = Get_NoSpotlight(); }
        if (InSlot == 7) { Params = Get_RightAlign(); }
        auto Tag = Get_StationTag(InSlot);
        if (Tag != n"" && !Params.StationTags.Contains(Tag))
        { Params.StationTags.Add(Tag); }
        return Params;
    }

    bool IsLive(FCk_Handle InHandle)
    {
        return ck::IsValid(InHandle) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    ACk_StationShowcaseGym_GameState Get_State(UWorld InWorld)
    {
        if (ck::Is_NOT_Valid(InWorld)) { return nullptr; }
        auto State = Cast<ACk_StationShowcaseGym_GameState>(InWorld.GetGameState());
        return ck::IsValid(State) && State.GetWorld() == InWorld &&
            State.GetClass() == ACk_StationShowcaseGym_GameState ? State : nullptr;
    }

	FCk_GymStation_SpawnParams Get_AutoSize_Wide()
	{
		auto Params = FCk_GymStation_SpawnParams();
		Params.InitialTransform = FTransform(FRotator::ZeroRotator, FVector(-1000.0, -7500.0, 0.0), FVector(1.0, 1.0, 1.0));
		Params.TitleText = FText::FromString("AUTO-SIZE WIDE — Long Title Stress For Width-Axis Growth Verification");
		Params.DescriptionText.Add(FText::FromString("This description line is intentionally extremely long to verify the alcove grows along the Y (width) axis."));
		Params.DescriptionText.Add(FText::FromString("The Apply_AutoSize_FromSpawnParams heuristic walks every line and picks the longest character count."));
		Params.DescriptionText.Add(FText::FromString("Width = max(MinWidth, (LongestChars * Scale * CharWidthFactor + 2*Padding) / 100)."));
		Params.DescriptionText.Add(FText::FromString("If anything here gets truncated against the back wall, the heuristic under-estimates and needs tuning."));
		Params.AutoSize = true;
		Params.ShowDebugOverlays = true;
		Params.StationTags.Add(n"Gym.StationShowcase.AutoSize.Wide");

		return Params;
	}

	FCk_GymStation_SpawnParams Get_AutoSize_Tall()
	{
		auto Params = FCk_GymStation_SpawnParams();
		Params.InitialTransform = FTransform(FRotator::ZeroRotator, FVector(-1000.0, -6000.0, 0.0), FVector(1.0, 1.0, 1.0));
		Params.TitleText = FText::FromString("AUTO-SIZE TALL");
		Params.DescriptionText.Add(FText::FromString("Line  1: alpha"));
		Params.DescriptionText.Add(FText::FromString("Line  2: bravo"));
		Params.DescriptionText.Add(FText::FromString("Line  3: charlie"));
		Params.DescriptionText.Add(FText::FromString("Line  4: delta"));
		Params.DescriptionText.Add(FText::FromString("Line  5: echo"));
		Params.DescriptionText.Add(FText::FromString("Line  6: foxtrot"));
		Params.DescriptionText.Add(FText::FromString("Line  7: golf"));
		Params.DescriptionText.Add(FText::FromString("Line  8: hotel"));
		Params.DescriptionText.Add(FText::FromString("Line  9: india"));
		Params.DescriptionText.Add(FText::FromString("Line 10: juliet"));
		Params.DescriptionText.Add(FText::FromString("Line 11: kilo"));
		Params.DescriptionText.Add(FText::FromString("Line 12: lima"));
		Params.DescriptionText.Add(FText::FromString("Line 13: mike"));
		Params.DescriptionText.Add(FText::FromString("Line 14: november"));
		Params.DescriptionText.Add(FText::FromString("Line 15: oscar"));
		Params.AutoSize = true;
		Params.ShowDebugOverlays = true;
		Params.StationTags.Add(n"Gym.StationShowcase.AutoSize.Tall");

		return Params;
	}

	FCk_GymStation_SpawnParams Get_AutoSize_Runtime()
	{
		auto Params = FCk_GymStation_SpawnParams();
		Params.InitialTransform = FTransform(FRotator::ZeroRotator, FVector(-1000.0, -4500.0, 0.0), FVector(1.0, 1.0, 1.0));
		Params.TitleText = FText::FromString("AUTO-SIZE RUNTIME");
		Params.DescriptionText.Add(FText::FromString("Initial small content."));
		Params.DescriptionText.Add(FText::FromString("Watch the alcove grow as the ticker reveals lines."));
		Params.AutoSize = true;
		Params.ShowDebugOverlays = true;
		Params.StationTags.Add(n"Gym.StationShowcase.AutoSize.Runtime");


		return Params;
	}

	FCk_GymStation_SpawnParams Get_Default()
	{
		auto Params = FCk_GymStation_SpawnParams();
		Params.InitialTransform = FTransform(FRotator::ZeroRotator, FVector(-1000.0, -3000.0, 0.0), FVector(1.0, 1.0, 1.0));
		Params.TitleText = FText::FromString("Default");
		Params.DescriptionText.Add(FText::FromString("Width=6  Depth=5  Height=5"));
		Params.DescriptionText.Add(FText::FromString("Spotlight on, EHTA_Left."));
		Params.DescriptionText.Add(FText::FromString("Baseline tuner config."));

		// Exercise the optional floor description channel + placement/alignment.
		Params.FloorDescriptionText.Add(FText::FromString("Floor text"));
		Params.FloorDescriptionText.Add(FText::FromString("Front + Right"));
		Params.FloorTextPlacement = ECk_GymStation_FloorTextPlacement::Front;
		Params.FloorTextAlignment = EHorizTextAligment::EHTA_Right;

		// Show anchor visualisation spheres so we can eyeball anchor positions.
		Params.ShowAnchors = true;

		// Showcase convention: every station here turns on debug overlays so
		// each piece's expected vs actual transform is visible for verification.
		// Production gyms should leave ShowDebugOverlays at its default (false).
		Params.ShowDebugOverlays = true;

		return Params;
	}

	FCk_GymStation_SpawnParams Get_Small()
	{
		auto Params = FCk_GymStation_SpawnParams();
		Params.InitialTransform = FTransform(FRotator::ZeroRotator, FVector(-1000.0, -1500.0, 0.0), FVector(1.0, 1.0, 1.0));
		Params.Width = 3.0;
		Params.Depth = 3.0;
		Params.Height = 3.0;
		Params.TitleText = FText::FromString("Small");
		Params.DescriptionText.Add(FText::FromString("Width=3  Depth=3  Height=3"));
		Params.DescriptionText.Add(FText::FromString("Verifies down-scaling + text fit."));
		Params.ShowDebugOverlays = true;

		return Params;
	}

	FCk_GymStation_SpawnParams Get_Large()
	{
		auto Params = FCk_GymStation_SpawnParams();
		Params.InitialTransform = FTransform(FRotator::ZeroRotator, FVector(-1000.0, 0.0, 0.0), FVector(1.0, 1.0, 1.0));
		Params.Width = 10.0;
		Params.Depth = 8.0;
		Params.Height = 8.0;
		Params.TitleText = FText::FromString("Large");
		Params.DescriptionText.Add(FText::FromString("Width=10  Depth=8  Height=8"));
		Params.DescriptionText.Add(FText::FromString("Verifies up-scaling + spotlight reach."));
		Params.ShowDebugOverlays = true;

		return Params;
	}

	FCk_GymStation_SpawnParams Get_NoSpotlight()
	{
		auto Params = FCk_GymStation_SpawnParams();
		Params.InitialTransform = FTransform(FRotator::ZeroRotator, FVector(-1000.0, 1500.0, 0.0), FVector(1.0, 1.0, 1.0));
		Params.ShowSpotlight = false;
		Params.TitleText = FText::FromString("No Spotlight");
		Params.DescriptionText.Add(FText::FromString("ShowSpotlight=false"));
		Params.DescriptionText.Add(FText::FromString("Back wall should look noticeably darker."));
		Params.ShowDebugOverlays = true;

		return Params;
	}

	FCk_GymStation_SpawnParams Get_RightAlign()
	{
		auto Params = FCk_GymStation_SpawnParams();
		Params.InitialTransform = FTransform(FRotator::ZeroRotator, FVector(-1000.0, 3000.0, 0.0), FVector(1.0, 1.0, 1.0));
		Params.TextAlignment = EHorizTextAligment::EHTA_Right;
		Params.TitleText = FText::FromString("Right Align");
		Params.DescriptionText.Add(FText::FromString("EHTA_Right alignment."));
		Params.DescriptionText.Add(FText::FromString("Text should hug the -Y side"));
		Params.DescriptionText.Add(FText::FromString("of the back wall, not centre."));
		Params.ShowDebugOverlays = true;

		return Params;
	}

	FCk_GymStation_Showcase_AutoSizeTickerParams Get_TickerParams(int32 InGeneration)
	{
		auto TickerParams = FCk_GymStation_Showcase_AutoSizeTickerParams();
		TickerParams.StationTag = n"Gym.StationShowcase.AutoSize.Runtime";
		TickerParams.Title = FText::FromString("AUTO-SIZE RUNTIME — Long Title Pushed By Ticker");
		TickerParams.LineRevealInterval = 1.0f;
		TickerParams.DescriptionLines.Add(FText::FromString("Tick reveal step 1: short line."));
		TickerParams.DescriptionLines.Add(FText::FromString("Tick reveal step 2: a noticeably longer line that should force a width-axis grow."));
		TickerParams.DescriptionLines.Add(FText::FromString("Tick reveal step 3: line three of the runtime grow stress test."));
		TickerParams.DescriptionLines.Add(FText::FromString("Tick reveal step 4: extending the description further to verify height growth."));
		TickerParams.DescriptionLines.Add(FText::FromString("Tick reveal step 5: another line — alcove must now be tall and wide."));
		TickerParams.DescriptionLines.Add(FText::FromString("Tick reveal step 6: yet another, the back wall should keep up."));
		TickerParams.DescriptionLines.Add(FText::FromString("Tick reveal step 7: a deliberately enormous line that easily exceeds the previous longest content for the runtime width-grow path."));
		TickerParams.DescriptionLines.Add(FText::FromString("Tick reveal step 8: penultimate."));
		TickerParams.DescriptionLines.Add(FText::FromString("Tick reveal step 9: final reveal — beyond this point only the tick counter mutates."));

		TickerParams.Generation = InGeneration;
		return TickerParams;
	}

    // Validate the entire frozen recipe before construction or presentation.
    bool Get_HasFiniteRecipe(FCk_GymStation_SpawnParams InParams, int32 InSlot)
    {
        if (InSlot < 0 || InSlot >= SlotCount || !InParams.InitialTransform.IsValid() ||
            InParams.InitialTransform.ContainsNaN() ||
            !Math::IsFinite(InParams.Width) || !Math::IsFinite(InParams.Depth) ||
            !Math::IsFinite(InParams.Height) || !Math::IsFinite(InParams.WallThickness) ||
            !Math::IsFinite(InParams.FloorThickness) || !Math::IsFinite(InParams.TrimDepth) ||
            !Math::IsFinite(InParams.AgentSpawnOffset) || !Math::IsFinite(InParams.TitleScale) ||
            !Math::IsFinite(InParams.DescriptionScale) || !Math::IsFinite(InParams.FloorDescriptionScale) ||
            !Math::IsFinite(InParams.BodyColour.R) || !Math::IsFinite(InParams.BodyColour.G) ||
            !Math::IsFinite(InParams.BodyColour.B) || !Math::IsFinite(InParams.BodyColour.A) ||
            !Math::IsFinite(InParams.TrimColour.R) || !Math::IsFinite(InParams.TrimColour.G) ||
            !Math::IsFinite(InParams.TrimColour.B) || !Math::IsFinite(InParams.TrimColour.A))
        { return false; }
        return true;
    }

    // Locally generated authority recipes must retain the exact authored transform.
    bool Get_IsExactAuthoredParams(FCk_GymStation_SpawnParams InParams, int32 InSlot)
    {
        if (!Get_HasFiniteRecipe(InParams, InSlot)) { return false; }
        auto Expected = Get_Params(InSlot);
        if (!InParams.InitialTransform.Equals(Expected.InitialTransform, 0.0)) { return false; }
        auto ActualValue = FInstancedStruct::Make(InParams);
        auto ExpectedValue = FInstancedStruct::Make(Expected);
        return ActualValue.IsValid() && ExpectedValue.IsValid() && ActualValue == ExpectedValue;
    }

    // Wire quaternions may normalize tiny components or choose the equivalent opposite sign.
    bool Get_IsExactWireParams(FCk_GymStation_SpawnParams InParams, int32 InSlot)
    {
        if (!Get_HasFiniteRecipe(InParams, InSlot)) { return false; }
        auto Expected = Get_Params(InSlot);
        auto Position = InParams.InitialTransform.GetTranslation();
        auto Scale = InParams.InitialTransform.GetScale3D();
        auto Rotation = InParams.InitialTransform.GetRotation();
        auto ExpectedPosition = Expected.InitialTransform.GetTranslation();
        auto ExpectedScale = Expected.InitialTransform.GetScale3D();
        auto ExpectedRotation = Expected.InitialTransform.GetRotation();
        if (!Math::IsFinite(Position.X) || !Math::IsFinite(Position.Y) || !Math::IsFinite(Position.Z) ||
            !Math::IsFinite(Scale.X) || !Math::IsFinite(Scale.Y) || !Math::IsFinite(Scale.Z) ||
            !Math::IsFinite(Rotation.X) || !Math::IsFinite(Rotation.Y) ||
            !Math::IsFinite(Rotation.Z) || !Math::IsFinite(Rotation.W)) { return false; }
        if (Position.X != ExpectedPosition.X || Position.Y != ExpectedPosition.Y ||
            Position.Z != ExpectedPosition.Z || Scale.X != ExpectedScale.X ||
            Scale.Y != ExpectedScale.Y || Scale.Z != ExpectedScale.Z ||
            !Rotation.IsNormalized() || Math::Abs(Rotation.SizeSquared() - 1.0) > 1.0e-12 ||
            !Rotation.Equals(ExpectedRotation, 1.0e-12)) { return false; }
        // Only the already-verified wire-equivalent transform is normalized in this local copy.
        auto Normalized = InParams;
        Normalized.InitialTransform = Expected.InitialTransform;
        auto ActualValue = FInstancedStruct::Make(Normalized);
        auto ExpectedValue = FInstancedStruct::Make(Expected);
        return ActualValue.IsValid() && ExpectedValue.IsValid() && ActualValue == ExpectedValue;
    }

    bool Get_IsExactStationScript(FCk_Handle InStation)
    {
        if (!IsLive(InStation)) { return false; }
        auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(InStation);
        return ck::IsValid(Script) && Script.GetClass() == UCk_EntityScript_GymStation;
    }
}
