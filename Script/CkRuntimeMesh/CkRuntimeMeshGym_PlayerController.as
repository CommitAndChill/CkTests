// Language=angelscript

//============================================================================
// RUNTIME MESH GYM
//
// Three stations on the shared gym level, one fixture object each (CkRuntimeMeshGym_Fixture.as):
//   1 Plane      - tune a plane against an off-origin, rotated source; cut; inspect source and results.
//   2 Surface    - source beside an exploded cut pair: cap material, checker UVs, seams, cast shadows.
//   3 Validation - repeat cuts up to 16 leaves, volume conservation, the contract's deterministic outcomes.
//
// This controller owns the gym input, the tuners (plane offset/yaw/pitch, explode distance, re-cut axis, selections,
// orbit view), camera framing, the lights, and EndPlay teardown. The fixtures own every entity. Keys 1-3 pick the
// station the panel controls; the station rows below them change with it, so dispatch is by key, not by row index.
//============================================================================

class ACk_RuntimeMeshGym_PlayerController : ACk_Gym_Base_PlayerController
{
    // ---- configuration ----
    private float _PlaneDisplayScale = 20.0;
    private float _SurfaceDisplayScale = 20.0;
    private float _ValidationDisplayScale = 15.0;

    // ---- runtime ----
    private UCkRuntimeMeshGym_PlaneFixture _Plane;
    private UCkRuntimeMeshGym_SurfaceFixture _Surface;
    private UCkRuntimeMeshGym_ValidationFixture _Validation;
    private TArray<FVector> _Centers;
    private bool _LightingBuilt = false;
    private bool _FramePending = false;

    // ---- tuners ----
    private int32 _Focus = 0;
    private float _PlaneOffsetCm = 0.0;
    private float _PlaneYawDeg = 0.0;
    private float _PlanePitchDeg = 0.0;
    private float _ExplodeCm = 1.5;
    private int32 _RecutAxis = 0;
    private int32 _PlaneSelection = 0;
    private int32 _SurfaceSelection = 0;
    private int32 _LeafSelection = 0;
    private int32 _OrbitView = 0;

    FString Get_StationTag(int32 InIndex)
    {
        if (InIndex == 0)
        {
            return "Gym.CkRuntimeMesh.Plane";
        }
        if (InIndex == 1)
        {
            return "Gym.CkRuntimeMesh.Surface";
        }
        return "Gym.CkRuntimeMesh.Validation";
    }

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        for (auto Index = 0; Index < 3; Index++)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(Index)));
            Station.Transform = FTransform(FVector(100.0 + 3600.0 * (Index % 2), 3000.0 * Math::IntegerDivisionTrunc(Index, 2), 0.0));
            Station.AutoSize = true;
            if (Index == 0)
            {
                Station.Title = FText::FromString("RUNTIME MESH - PLANE");
                Station.Description.Add(FText::FromString("A 10 cm checker cube shown at x20, rotated and off-origin.\nThe cyan quad is the cut plane, posed from the same\nsource-local plane the slice uses."));
                Station.Description.Add(FText::FromString("I/K offset, J/L yaw, U/O pitch, T cut the source.\nResults appear beside it: positive +Y, negative -Y.\nV selects source / positive / negative."));
            }
            else if (Index == 1)
            {
                Station.Title = FText::FromString("RUNTIME MESH - SURFACE");
                Station.Description.Add(FText::FromString("The source beside one oblique cut pair, lit by a\nshadow-casting sun over a grey receiver plane."));
                Station.Description.Add(FText::FromString("Green checker = cap material, blue/red = faces.\nI/K explode the pair (display offset, not physics).\nF orbits, V selects a piece to frame."));
            }
            else
            {
                Station.Title = FText::FromString("RUNTIME MESH - VALIDATION");
                Station.Description.Add(FText::FromString("Re-cut the selected leaf (T) up to 16 leaves; the\npanel checks volume against the production tolerance."));
                Station.Description.Add(FText::FromString("N / M / Z run the contract cases on the source:\nNoIntersection, TouchingOnly, RejectedTooSmall."));
            }
            Stations.Add(Station);
        }
        return Stations;
    }

    void Request_StartGym() override
    {
        Build_Lighting();

        if (ck::Is_NOT_Valid(ck::ToEntity(this)))
        {
            ck::Error("Runtime Mesh gym: the PlayerController has no entity; the stations cannot be composed.");
            return;
        }

        if (_Centers.Num() != 3)
        {
            _Centers.Reset();
            for (auto Index = 0; Index < 3; Index++)
            {
                _Centers.Add(Get_StationAnchorLocation(Get_StationTag(Index), ECk_GymStation_Anchor::FootprintCenter)
                    + FVector(1300.0, 0.0, 250.0));
            }
        }

        Request_ResetAll();
        Request_Frame();
    }

    void Request_ResetAll()
    {
        for (auto Index = 0; Index < 3; Index++)
        {
            DoRebuild_Station(Index);
        }
    }

    void Request_ResetFocused()
    {
        DoRebuild_Station(_Focus);
    }

    // Resets a live station on the same call stack. A missing fixture, or one left Closed because a rebuild found no
    // live owner, is replaced by a new fixture object instead; the old one keeps rejecting whatever still resolves to it.
    private void DoRebuild_Station(int32 InIndex)
    {
        auto GymEntity = ck::ToEntity(this);
        if (InIndex == 0)
        {
            _PlaneSelection = 0;
            if (ck::IsValid(_Plane) && _Plane.Closed == false && _Plane.Request_Reset())
            {
                return;
            }
            if (ck::Is_NOT_Valid(GymEntity) || _Centers.IsValidIndex(0) == false)
            {
                return;
            }
            _Plane = Cast<UCkRuntimeMeshGym_PlaneFixture>(NewObject(this, UCkRuntimeMeshGym_PlaneFixture));
            _Plane.Set_Plane(Get_TunedPlane());
            _Plane.Create(GymEntity, CkRuntimeMeshGym::Make_CenteredPose(_Centers[0], FRotator(15.0, 30.0, 0.0), _PlaneDisplayScale), 0);
        }
        else if (InIndex == 1)
        {
            _SurfaceSelection = 0;
            if (ck::IsValid(_Surface) && _Surface.Closed == false && _Surface.Request_Reset())
            {
                return;
            }
            if (ck::Is_NOT_Valid(GymEntity) || _Centers.IsValidIndex(1) == false)
            {
                return;
            }
            _Surface = Cast<UCkRuntimeMeshGym_SurfaceFixture>(NewObject(this, UCkRuntimeMeshGym_SurfaceFixture));
            _Surface.Set_ExplodeCm(_ExplodeCm);
            _Surface.Create(GymEntity, CkRuntimeMeshGym::Make_CenteredPose(_Centers[1], FRotator(0.0, 20.0, 0.0), _SurfaceDisplayScale), 1);
        }
        else
        {
            _LeafSelection = 0;
            if (ck::IsValid(_Validation) && _Validation.Closed == false && _Validation.Request_Reset())
            {
                return;
            }
            if (ck::Is_NOT_Valid(GymEntity) || _Centers.IsValidIndex(2) == false)
            {
                return;
            }
            _Validation = Cast<UCkRuntimeMeshGym_ValidationFixture>(NewObject(this, UCkRuntimeMeshGym_ValidationFixture));
            _Validation.Create(GymEntity, CkRuntimeMeshGym::Make_CenteredPose(_Centers[2], FRotator(10.0, -25.0, 0.0), _ValidationDisplayScale), 2);
        }
    }

    // ---- lighting: lit geometry and a receiver are what make cast shadows judgeable (VisualLod precedent) ----

    private void Build_Lighting()
    {
        if (_LightingBuilt)
        {
            return;
        }
        _LightingBuilt = true;

        auto KeyLight = UDirectionalLightComponent::Create(this);
        KeyLight.SetRelativeRotation(FRotator(-50.0f, -40.0f, 0.0f));
        KeyLight.SetIntensity(6.0f);
        KeyLight.SetLightColor(FLinearColor(1.0f, 0.97f, 0.9f, 1.0f));
        KeyLight.SetCastShadows(true);
        KeyLight.SetForwardShadingPriority(100);

        auto FillLight = USkyLightComponent::Create(this);
        FillLight.SetIntensity(0.6f);
        FillLight.SetLightColor(FLinearColor(0.55f, 0.6f, 0.72f, 1.0f));
    }

    // ---- tuners ----

    private FCk_RuntimeMesh_PlaneLocal Get_TunedPlane()
    {
        return CkRuntimeMeshGym::Make_TunedPlane(_PlaneOffsetCm, _PlaneYawDeg, _PlanePitchDeg);
    }

    private void Apply_Plane()
    {
        if (ck::IsValid(_Plane))
        {
            _Plane.Set_Plane(Get_TunedPlane());
        }
    }

    private float Wrap_Degrees(float InDegrees)
    {
        auto Value = InDegrees;
        if (Value > 180.0)
        {
            Value -= 360.0;
        }
        if (Value <= -180.0)
        {
            Value += 360.0;
        }
        return Value;
    }

    private FString Get_AxisName(int32 InAxis)
    {
        if (InAxis == 1)
        {
            return "Y";
        }
        if (InAxis == 2)
        {
            return "Z";
        }
        if (InAxis == 3)
        {
            return "diagonal (1,1,1)";
        }
        return "X";
    }

    private FString Get_OrbitName()
    {
        if (_OrbitView == 1)
        {
            return "front-right";
        }
        if (_OrbitView == 2)
        {
            return "behind";
        }
        if (_OrbitView == 3)
        {
            return "top-down";
        }
        return "front-left";
    }

    private FString Get_PieceName(int32 InIndex)
    {
        if (InIndex == 1)
        {
            return "positive";
        }
        if (InIndex == 2)
        {
            return "negative";
        }
        return "source";
    }

    // ---- framing ----

    private FCkRuntimeMeshGym_WorldBox Get_SelectedBox()
    {
        if (_Focus == 0 && ck::IsValid(_Plane))
        {
            return CkRuntimeMeshGym::Make_WorldBox(_Plane.Get_Piece(_PlaneSelection));
        }
        if (_Focus == 1 && ck::IsValid(_Surface))
        {
            return CkRuntimeMeshGym::Make_WorldBox(_Surface.Get_Piece(_SurfaceSelection));
        }
        if (_Focus == 2 && ck::IsValid(_Validation) && _Validation.Leaves.IsValidIndex(_LeafSelection))
        {
            return CkRuntimeMeshGym::Make_WorldBox(_Validation.Leaves[_LeafSelection]);
        }
        return FCkRuntimeMeshGym_WorldBox();
    }

    private void Request_Frame()
    {
        auto Pawn = GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn) || _Centers.IsValidIndex(_Focus) == false)
        {
            _FramePending = true;
            return;
        }
        _FramePending = false;

        auto Target = _Centers[_Focus];
        auto Distance = 1100.0;
        auto Box = Get_SelectedBox();
        // The Validation station frames its whole cluster; the others frame the selected piece.
        if (Box.Valid && _Focus != 2)
        {
            Target = Box.Center;
            Distance = Math::Max(Box.Extent.Size() * 4.5, 450.0);
        }

        auto Yaw = 45.0;
        auto Pitch = -25.0;
        if (_OrbitView == 1)
        {
            Yaw = -45.0;
        }
        else if (_OrbitView == 2)
        {
            Yaw = -135.0;
            Pitch = -35.0;
        }
        else if (_OrbitView == 3)
        {
            Pitch = -75.0;
        }
        const auto LookDirection = FRotator(Pitch, Yaw, 0.0).Vector();
        const auto Eye = Target - LookDirection * Distance;
        Pawn.SetActorLocation(Eye);
        SetControlRotation((Target - Eye).Rotation());
    }

    private void Set_Focus(int32 InFocus)
    {
        _Focus = InFocus;
        Request_Frame();
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float InDeltaSeconds)
    {
        if (_FramePending)
        {
            Request_Frame();
        }

        // The plane's normal: the side the POSITIVE result comes from.
        if (ck::IsValid(_Plane) && _Plane.Closed == false)
        {
            const auto Center = _Plane.Get_PlaneWorldCenter();
            const auto Tip = Center + _Plane.Get_PlaneWorldNormal() * 140.0;
            utils_debug_draw::DrawDebugLine(Center, Tip, FLinearColor(1.0, 0.85, 0.1, 1.0), 0.0f, 3.0f);
            utils_debug_draw::DrawDebugString(Tip, "+ positive side", FLinearColor(1.0, 0.85, 0.1, 1.0), 0.0f);
        }

        auto Box = Get_SelectedBox();
        if (Box.Valid)
        {
            utils_debug_draw::DrawDebugBox(Box.Center, Box.Extent + FVector(3.0, 3.0, 3.0),
                FLinearColor(1.0, 1.0, 1.0, 1.0), Box.Rotation, 0.0f, 2.0f);
        }
    }

    // ---- control panel ----

    FString Get_ControlPanelTitle() override
    {
        return "RUNTIME MESH";
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        if (ck::Is_NOT_Valid(_Plane) || ck::Is_NOT_Valid(_Surface) || ck::Is_NOT_Valid(_Validation))
        {
            Rows.Add(CkGym_Control::Header("RUNTIME MESH"));
            Rows.Add(CkGym_Control::Status("Verdict", "starting", true));
            return Rows;
        }

        Rows.Add(CkGym_Control::Header("RUNTIME MESH"));
        if (Get_FocusedFixture().Closed)
        {
            // A Closed fixture ignores every station key; only R / X (or Ck_Gym_Restart) replace it.
            Rows.Add(CkGym_Control::Status("Verdict", "station closed: no live owner to build under - R rebuilds it", true));
        }
        else if (_Focus == 0)
        {
            Rows.Add(Get_PlaneVerdict());
        }
        else if (_Focus == 1)
        {
            Rows.Add(Get_SurfaceVerdict());
        }
        else
        {
            Rows.Add(Get_ValidationVerdict());
        }
        Rows.Add(CkGym_Control::Choice(EKeys::One, "1", "Plane station", _Focus == 0));
        Rows.Add(CkGym_Control::Choice(EKeys::Two, "2", "Surface station", _Focus == 1));
        Rows.Add(CkGym_Control::Choice(EKeys::Three, "3", "Validation station", _Focus == 2));
        Rows.Add(CkGym_Control::Cycle(EKeys::F, "F", "Frame, next orbit view", Get_OrbitName()));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Reset this station (same call stack)"));
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", "Reset all stations"));

        if (_Focus == 0)
        {
            Add_PlaneRows(Rows);
        }
        else if (_Focus == 1)
        {
            Add_SurfaceRows(Rows);
        }
        else
        {
            Add_ValidationRows(Rows);
        }
        return Rows;
    }

    private UCkRuntimeMeshGym_FixtureBase Get_FocusedFixture()
    {
        if (_Focus == 0)
        {
            return _Plane;
        }
        if (_Focus == 1)
        {
            return _Surface;
        }
        return _Validation;
    }

    private FCkGym_ControlRow Get_PlaneVerdict()
    {
        if (_Plane.Get_IsSourceReady() == false)
        {
            const auto State = _Plane.Get_SourceStateText();
            return CkGym_Control::Status("Verdict", f"source import {State}", true);
        }
        if (_Plane.HasPendingSlice)
        {
            return CkGym_Control::Status("Verdict", "cut in flight");
        }
        if (_Plane.HasLastOutcome && _Plane.LastOutcome != ECk_RuntimeMesh_SliceOutcome::Succeeded)
        {
            const auto Outcome = _Plane.LastOutcome;
            return CkGym_Control::Status("Verdict", f"last cut resolved {Outcome :n}: no pieces made");
        }
        if (_Plane.HasPair)
        {
            const auto Source = _Plane.SourcePiece.Metrics.Get_VolumeCm3();
            const auto Sum = _Plane.Positive.Metrics.Get_VolumeCm3() + _Plane.Negative.Metrics.Get_VolumeCm3();
            const auto Diff = Math::Abs(Sum - Source);
            const auto Allowed = CkRuntimeMeshGym::Get_AllowedVolumeError(Source);
            const auto Holds = Diff <= Allowed;
            const auto Word = Holds ? "conserves" : "DOES NOT conserve";
            return CkGym_Control::Status("Verdict", f"pair {Word} volume: diff {Diff :.4} cm3, allowed {Allowed :.4}", Holds == false);
        }
        return CkGym_Control::Status("Verdict", "dial the plane, then T to cut the source");
    }

    private void Add_PlaneRows(TArray<FCkGym_ControlRow>& Rows)
    {
        Rows.Add(CkGym_Control::Header("PLANE - cut the immutable source"));

        const auto SourceState = _Plane.Get_SourceStateText();
        const auto SourceDisplay = CkRuntimeMeshGym::Get_DisplayStateText(_Plane.SourcePiece.Display);
        const auto SourceVolume = _Plane.SourcePiece.Metrics.Get_VolumeCm3();
        const auto SourceTris = _Plane.SourcePiece.Metrics.Get_TriangleCount();
        Rows.Add(CkGym_Control::Status("Source", f"{SourceState}, {SourceTris} tris, {SourceVolume :.2} cm3, display {SourceDisplay}"));

        const auto Plane = _Plane.Plane;
        const auto P = Plane.Get_PositionCm();
        const auto N = Plane.Get_Normal();
        Rows.Add(CkGym_Control::Status("Plane (source-local cm)", f"at ({P.X :.2}, {P.Y :.2}, {P.Z :.2}) normal ({N.X :.2}, {N.Y :.2}, {N.Z :.2})"));

        Rows.Add(CkGym_Control::Action(EKeys::I, "I", f"Offset +0.5 cm (now {_PlaneOffsetCm :.1})"));
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Offset -0.5 cm"));
        Rows.Add(CkGym_Control::Action(EKeys::L, "L", f"Yaw +15 (now {_PlaneYawDeg :.0})"));
        Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Yaw -15"));
        Rows.Add(CkGym_Control::Action(EKeys::U, "U", f"Pitch +15 (now {_PlanePitchDeg :.0})"));
        Rows.Add(CkGym_Control::Action(EKeys::O, "O", "Pitch -15"));
        Rows.Add(CkGym_Control::Action(EKeys::T, "T", "Cut the source", _Plane.Get_CanCut()));
        Rows.Add(CkGym_Control::Cycle(EKeys::V, "V", "Select and frame", Get_PieceName(_PlaneSelection)));

        auto LastCut = "none";
        if (_Plane.HasPendingSlice)
        {
            LastCut = "in flight";
        }
        else if (_Plane.HasLastOutcome)
        {
            const auto Outcome = _Plane.LastOutcome;
            LastCut = f"{Outcome :n}";
        }
        Rows.Add(CkGym_Control::Status("Last cut", LastCut));

        if (_Plane.HasPair)
        {
            const auto PositiveVolume = _Plane.Positive.Metrics.Get_VolumeCm3();
            const auto NegativeVolume = _Plane.Negative.Metrics.Get_VolumeCm3();
            const auto PositiveDisplay = CkRuntimeMeshGym::Get_DisplayStateText(_Plane.Positive.Display);
            const auto NegativeDisplay = CkRuntimeMeshGym::Get_DisplayStateText(_Plane.Negative.Display);
            Rows.Add(CkGym_Control::Status("Pair", f"+{PositiveVolume :.2} / -{NegativeVolume :.2} cm3, caps {_Plane.PositiveCapTriangles}/{_Plane.NegativeCapTriangles} tris, displays {PositiveDisplay}/{NegativeDisplay}"));

            // The pair keeps the plane it was cut with; the guide follows the tuners and may have moved since.
            const auto CutAt = _Plane.PairPlane.Get_PositionCm();
            const auto CutNormal = _Plane.PairPlane.Get_Normal();
            const auto Agreement = _Plane.Get_PairMatchesGuide() ? "matches the guide" : "guide has moved since - T re-cuts";
            Rows.Add(CkGym_Control::Status("Pair cut at (source-local cm)", f"({CutAt.X :.2}, {CutAt.Y :.2}, {CutAt.Z :.2}) normal ({CutNormal.X :.2}, {CutNormal.Y :.2}, {CutNormal.Z :.2}), {Agreement}"));
        }

        const auto Selected = _Plane.Get_Piece(_PlaneSelection);
        if (ck::IsValid(Selected.Mesh))
        {
            const auto Min = Selected.Metrics.Get_BoundsMinCm();
            const auto Max = Selected.Metrics.Get_BoundsMaxCm();
            Rows.Add(CkGym_Control::Status("Selected bounds (local cm)", f"({Min.X :.1}, {Min.Y :.1}, {Min.Z :.1}) .. ({Max.X :.1}, {Max.Y :.1}, {Max.Z :.1})"));
        }
        else
        {
            Rows.Add(CkGym_Control::Status("Selected", "nothing to show yet - cut first"));
        }
        Add_StaleRow(Rows, _Plane);
    }

    private FCkGym_ControlRow Get_SurfaceVerdict()
    {
        if (_Surface.Get_IsSourceReady() == false)
        {
            const auto State = _Surface.Get_SourceStateText();
            return CkGym_Control::Status("Verdict", f"source import {State}", true);
        }
        if (_Surface.HasPair == false)
        {
            if (_Surface.HasLastOutcome && _Surface.LastOutcome != ECk_RuntimeMesh_SliceOutcome::Succeeded)
            {
                const auto Outcome = _Surface.LastOutcome;
                return CkGym_Control::Status("Verdict", f"cut resolved {Outcome :n}", true);
            }
            return CkGym_Control::Status("Verdict", "cutting");
        }
        const auto PositiveDisplay = CkRuntimeMeshGym::Get_DisplayStateText(_Surface.Positive.Display);
        const auto NegativeDisplay = CkRuntimeMeshGym::Get_DisplayStateText(_Surface.Negative.Display);
        return CkGym_Control::Status("Verdict", f"pair displayed ({PositiveDisplay}/{NegativeDisplay}): judge caps, seams and shadows by eye");
    }

    private void Add_SurfaceRows(TArray<FCkGym_ControlRow>& Rows)
    {
        Rows.Add(CkGym_Control::Header("SURFACE - caps, seams, UVs, shadows"));
        if (_Surface.HasPair)
        {
            const auto PositiveVolume = _Surface.Positive.Metrics.Get_VolumeCm3();
            const auto NegativeVolume = _Surface.Negative.Metrics.Get_VolumeCm3();
            Rows.Add(CkGym_Control::Status("Pair", f"+{PositiveVolume :.2} / -{NegativeVolume :.2} cm3, caps {_Surface.PositiveCapTriangles}/{_Surface.NegativeCapTriangles} tris"));
        }
        Rows.Add(CkGym_Control::Status("Explode", f"{_ExplodeCm :.1} cm per half along the plane normal - display offset, not physics"));
        Rows.Add(CkGym_Control::Action(EKeys::I, "I", "Explode +0.5 cm"));
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Explode -0.5 cm"));
        Rows.Add(CkGym_Control::Action(EKeys::T, "T", "Re-cut the source (replaces the pair)", _Surface.Get_CanCut()));
        Rows.Add(CkGym_Control::Cycle(EKeys::V, "V", "Select and frame", Get_PieceName(_SurfaceSelection)));
        Rows.Add(CkGym_Control::Status("Look for", "green checker caps, blue/red checker faces, clean seams, shadows on the grey plane"));
        Add_StaleRow(Rows, _Surface);
    }

    private FCkGym_ControlRow Get_ValidationVerdict()
    {
        if (_Validation.Get_IsSourceReady() == false)
        {
            const auto State = _Validation.Get_SourceStateText();
            return CkGym_Control::Status("Verdict", f"source import {State}", true);
        }
        if (_Validation.HasLastCase)
        {
            const auto Expected = _Validation.Get_ExpectedOutcome(_Validation.LastCase);
            if (Expected != _Validation.LastCaseOutcome)
            {
                const auto Observed = _Validation.LastCaseOutcome;
                return CkGym_Control::Status("Verdict", f"case expected {Expected :n} but observed {Observed :n}", true);
            }
        }
        const auto Drift = Math::Abs(_Validation.Get_LeafVolumeSumCm3() - _Validation.SourceVolumeCm3);
        const auto Bound = Math::Max(_Validation.AccumulatedAllowedCm3, CkRuntimeMeshGym::Get_AllowedVolumeError(_Validation.SourceVolumeCm3));
        if (Drift > Bound)
        {
            return CkGym_Control::Status("Verdict", f"leaves drift {Drift :.4} cm3 from the source, beyond {Bound :.4}", true);
        }
        return CkGym_Control::Status("Verdict", f"{_Validation.Leaves.Num()} leaves conserve the source volume (drift {Drift :.4} cm3)");
    }

    private void Add_ValidationRows(TArray<FCkGym_ControlRow>& Rows)
    {
        Rows.Add(CkGym_Control::Header("VALIDATION - repeat cuts, conservation, outcomes"));

        const auto LeafCount = _Validation.Leaves.Num();
        const auto ReadyDisplays = _Validation.Get_ReadyDisplayCount();
        Rows.Add(CkGym_Control::Status("Leaves", f"{LeafCount} / {_Validation.MaxLeaves}, {ReadyDisplays} displays Ready, {_Validation.CutCount} cuts", LeafCount >= _Validation.MaxLeaves));

        if (_Validation.HasLastCut)
        {
            const auto Parent = _Validation.LastParentVolumeCm3;
            const auto Sum = _Validation.LastPositiveVolumeCm3 + _Validation.LastNegativeVolumeCm3;
            const auto Diff = Math::Abs(Sum - Parent);
            const auto Allowed = CkRuntimeMeshGym::Get_AllowedVolumeError(Parent);
            Rows.Add(CkGym_Control::Status("Last cut", f"{Parent :.3} -> {_Validation.LastPositiveVolumeCm3 :.3} + {_Validation.LastNegativeVolumeCm3 :.3} cm3, diff {Diff :.5} <= {Allowed :.4}", Diff > Allowed));
        }
        const auto LeafSum = _Validation.Get_LeafVolumeSumCm3();
        Rows.Add(CkGym_Control::Status("All leaves", f"sum {LeafSum :.3} vs source {_Validation.SourceVolumeCm3 :.3} cm3, summed bound {_Validation.AccumulatedAllowedCm3 :.4}"));

        // Read-only: the selection is clamped where it changes (V, T and the resets), never by drawing the panel.
        auto SelectedLabel = "none";
        if (_Validation.Leaves.IsValidIndex(_LeafSelection))
        {
            const auto Leaf = _Validation.Leaves[_LeafSelection];
            const auto Volume = Leaf.Metrics.Get_VolumeCm3();
            SelectedLabel = f"{Leaf.Label} ({Volume :.2} cm3)";
        }
        Rows.Add(CkGym_Control::Cycle(EKeys::V, "V", "Select leaf", SelectedLabel));
        Rows.Add(CkGym_Control::Cycle(EKeys::Y, "Y", "Re-cut axis (through the leaf centroid)", Get_AxisName(_RecutAxis)));
        Rows.Add(CkGym_Control::Action(EKeys::T, "T", "Cut the selected leaf", _Validation.Get_CanCutLeaf(_LeafSelection)));

        auto LastLeafCut = "none";
        if (_Validation.HasPendingSlice)
        {
            LastLeafCut = "in flight";
        }
        else if (_Validation.HasLastOutcome)
        {
            const auto Outcome = _Validation.LastOutcome;
            LastLeafCut = f"{Outcome :n}";
        }
        Rows.Add(CkGym_Control::Status("Last resolution", LastLeafCut));

        const auto CanRunCase = _Validation.Get_CanRunCase();
        Rows.Add(CkGym_Control::Action(EKeys::N, "N", "Case: source, plane x=20 (expect NoIntersection)", CanRunCase));
        Rows.Add(CkGym_Control::Action(EKeys::M, "M", "Case: source, plane x=10 (expect TouchingOnly)", CanRunCase));
        Rows.Add(CkGym_Control::Action(EKeys::Z, "Z", "Case: source, x=0.001, min extent 0.01 (expect RejectedTooSmall)", CanRunCase));
        if (_Validation.HasLastCase)
        {
            const auto Expected = _Validation.Get_ExpectedOutcome(_Validation.LastCase);
            const auto Observed = _Validation.LastCaseOutcome;
            Rows.Add(CkGym_Control::Status("Case result", f"expected {Expected :n}, observed {Observed :n}", Expected != Observed));
        }
        Add_StaleRow(Rows, _Validation);
    }

    private void Add_StaleRow(TArray<FCkGym_ControlRow>& Rows, UCkRuntimeMeshGym_FixtureBase InFixture)
    {
        if (InFixture.HasLastStaleOutcome == false)
        {
            Rows.Add(CkGym_Control::Status("Stale resolutions rejected", f"{InFixture.StaleResolutions} (generation {InFixture.Generation})"));
            return;
        }
        const auto Outcome = InFixture.LastStaleOutcome;
        Rows.Add(CkGym_Control::Status("Stale resolutions rejected", f"{InFixture.StaleResolutions}, last {Outcome :n} (generation {InFixture.Generation})"));
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        auto Rows = Get_ControlRows();
        if (Rows.IsValidIndex(InRowIndex) == false || Rows[InRowIndex].Enabled == false)
        {
            return;
        }
        if (ck::Is_NOT_Valid(_Plane) || ck::Is_NOT_Valid(_Surface) || ck::Is_NOT_Valid(_Validation))
        {
            return;
        }

        const auto Key = Rows[InRowIndex].Key;
        if (Key == EKeys::One)
        {
            Set_Focus(0);
        }
        else if (Key == EKeys::Two)
        {
            Set_Focus(1);
        }
        else if (Key == EKeys::Three)
        {
            Set_Focus(2);
        }
        else if (Key == EKeys::F)
        {
            _OrbitView = (_OrbitView + 1) % 4;
            Request_Frame();
        }
        else if (Key == EKeys::R)
        {
            Request_ResetFocused();
        }
        else if (Key == EKeys::X)
        {
            Request_ResetAll();
        }
        else if (_Focus == 0)
        {
            DoActivate_Plane(Key);
        }
        else if (_Focus == 1)
        {
            DoActivate_Surface(Key);
        }
        else
        {
            DoActivate_Validation(Key);
        }
    }

    private void DoActivate_Plane(FKey InKey)
    {
        if (InKey == EKeys::I)
        {
            _PlaneOffsetCm = Math::Clamp(_PlaneOffsetCm + 0.5, -9.0, 9.0);
            Apply_Plane();
        }
        else if (InKey == EKeys::K)
        {
            _PlaneOffsetCm = Math::Clamp(_PlaneOffsetCm - 0.5, -9.0, 9.0);
            Apply_Plane();
        }
        else if (InKey == EKeys::L)
        {
            _PlaneYawDeg = Wrap_Degrees(_PlaneYawDeg + 15.0);
            Apply_Plane();
        }
        else if (InKey == EKeys::J)
        {
            _PlaneYawDeg = Wrap_Degrees(_PlaneYawDeg - 15.0);
            Apply_Plane();
        }
        else if (InKey == EKeys::U)
        {
            _PlanePitchDeg = Math::Clamp(_PlanePitchDeg + 15.0, -90.0, 90.0);
            Apply_Plane();
        }
        else if (InKey == EKeys::O)
        {
            _PlanePitchDeg = Math::Clamp(_PlanePitchDeg - 15.0, -90.0, 90.0);
            Apply_Plane();
        }
        else if (InKey == EKeys::T)
        {
            _Plane.Request_Cut();
        }
        else if (InKey == EKeys::V)
        {
            _PlaneSelection = (_PlaneSelection + 1) % 3;
            Request_Frame();
        }
    }

    private void DoActivate_Surface(FKey InKey)
    {
        if (InKey == EKeys::I)
        {
            _ExplodeCm = Math::Clamp(_ExplodeCm + 0.5, 0.0, 8.0);
            _Surface.Set_ExplodeCm(_ExplodeCm);
        }
        else if (InKey == EKeys::K)
        {
            _ExplodeCm = Math::Clamp(_ExplodeCm - 0.5, 0.0, 8.0);
            _Surface.Set_ExplodeCm(_ExplodeCm);
        }
        else if (InKey == EKeys::T)
        {
            _Surface.Request_Recut();
        }
        else if (InKey == EKeys::V)
        {
            _SurfaceSelection = (_SurfaceSelection + 1) % 3;
            Request_Frame();
        }
    }

    private void DoActivate_Validation(FKey InKey)
    {
        // Leaves only grow within a generation and the resets zero the selection, so this clamp is the one place an
        // out-of-range selection could be repaired.
        const auto LeafCount = _Validation.Leaves.Num();
        _LeafSelection = Math::Clamp(_LeafSelection, 0, Math::Max(LeafCount - 1, 0));

        if (InKey == EKeys::V)
        {
            _LeafSelection = LeafCount > 0 ? (_LeafSelection + 1) % LeafCount : 0;
        }
        else if (InKey == EKeys::Y)
        {
            _RecutAxis = (_RecutAxis + 1) % 4;
        }
        else if (InKey == EKeys::T)
        {
            _Validation.Request_CutLeaf(_LeafSelection, _RecutAxis);
        }
        else if (InKey == EKeys::N)
        {
            _Validation.Request_Case(ECkRuntimeMeshGym_ValidationCase::NoIntersection);
        }
        else if (InKey == EKeys::M)
        {
            _Validation.Request_Case(ECkRuntimeMeshGym_ValidationCase::TouchingOnly);
        }
        else if (InKey == EKeys::Z)
        {
            _Validation.Request_Case(ECkRuntimeMeshGym_ValidationCase::RejectedTooSmall);
        }
    }

    // Drives one panel row by index, as the panel would - for console and scripted PIE checks.
    UFUNCTION(Exec)
    void Ck_RuntimeMeshGym_Control(int32 InRowIndex)
    {
        Request_ControlActivated(InRowIndex);
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        // Close, not reset: anything still in flight resolves into a closed fixture and creates nothing.
        if (ck::IsValid(_Plane))
        {
            _Plane.Request_Close();
        }
        if (ck::IsValid(_Surface))
        {
            _Surface.Request_Close();
        }
        if (ck::IsValid(_Validation))
        {
            _Validation.Request_Close();
        }
    }
}
