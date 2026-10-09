// Language=angelscript

// Floor tops sit this far above each station's footprint centre, clear of the alcove floor.
const float k_RuntimeConvexGym_FloorTopZ = 40.0;

class ACk_RuntimeConvexGym_PlayerController : ACk_Gym_Base_PlayerController
{
    private UCk_RuntimeConvexGym_HullFixture _Hull;
    private UCk_RuntimeConvexGym_SliceFixture _Slice;
    private UCk_RuntimeConvexGym_SliceFixture _Lifecycle;
    private bool _Started = false;
    private int32 _Focus = 0;
    private int32 _SliceSelection = 0;
    private int32 _LifecycleSelection = 0;
    private int32 _SeparationPreset = 2;

    // The override stack restores the value ck.Jolt.DebugDraw.Enabled had before the gym touched it, on EndPlay.
    private bool _JoltDrawOverridden = false;

    FString Get_StationTag(int32 InIndex) const
    {
        if (InIndex == 0)
        {
            return "Gym.CkJolt.RuntimeConvex.Hull";
        }
        if (InIndex == 1)
        {
            return "Gym.CkJolt.RuntimeConvex.SliceToPhysics";
        }
        return "Gym.CkJolt.RuntimeConvex.Lifecycle";
    }

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        for (auto Index = 0; Index < 3; Index++)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(Index)));
            Station.Transform = FTransform(FVector(100.0, 1600.0 * float(Index - 1), 0.0));
            Station.AutoSize = true;
            if (Index == 0)
            {
                Station.Title = FText::FromString("HULL - RUNTIME CONVEX FROM A POINT SET");
                Station.Description.Add(FText::FromString("A Dynamic Jolt body built through the public API from 9 points: the 8 corners of a 100 cm cube and its centre. The hull drops the centre, so the unscaled engine cube drawn on the body entity is exactly the collision shape."));
                Station.Description.Add(FText::FromString("It falls onto a Static Jolt floor, tumbles and sleeps. G kicks it, J shows the Jolt hull view, K submits 4 coplanar points that resolve HullFailed (logs an ensure by design)."));
            }
            else if (Index == 1)
            {
                Station.Title = FText::FromString("SLICE TO PHYSICS - ONE CUBE, TWO BODIES");
                Station.Description.Add(FText::FromString("A 10 cm CPU checker cube is imported, displayed and given a RuntimeConvex body. A cut composes each half with its own Transform, display and body, mass split by volume."));
                Station.Description.Add(FText::FromString("While a cut is in flight the source is a Kinematic body that ignores the halves; it stays until both half bodies are Ready, then it is destroyed and the halves get a mass x speed kick along the plane normal. Fixture policy, not a food or momentum model. Up to 4 pieces; X resets."));
            }
            else
            {
                Station.Title = FText::FromString("LIFECYCLE - RESET, FAILURE AND INDEPENDENT LIFETIMES");
                Station.Description.Add(FText::FromString("Reset with a slice or body setup still pending (forced on the same call stack), a cut whose second half dies before admission, and destroying one piece while the others live."));
                Station.Description.Add(FText::FromString("The panel counts what the fixture owns: stale completions must be discarded and abandoned bodies must reach zero. Travel out and back (Ck_Gym_Next / Ck_Gym_Prev) must rebuild the same counts."));
            }
            Stations.Add(Station);
        }
        return Stations;
    }

    FVector Get_Origin(int32 InIndex)
    {
        return Get_StationAnchorLocation(Get_StationTag(InIndex), ECk_GymStation_Anchor::FootprintCenter)
            + FVector(0.0, 0.0, k_RuntimeConvexGym_FloorTopZ);
    }

    void Request_StartGym() override
    {
        auto FixtureOwner = ck::ToEntity(this);
        if (ck::Is_NOT_Valid(FixtureOwner))
        {
            ck::Error("Runtime Convex gym cannot start: the player controller has no entity.");
            return;
        }
        if (ck::Is_NOT_Valid(_Hull))
        {
            _Hull = NewObject(this, UCk_RuntimeConvexGym_HullFixture);
            _Slice = NewObject(this, UCk_RuntimeConvexGym_SliceFixture);
            _Lifecycle = NewObject(this, UCk_RuntimeConvexGym_SliceFixture);
        }
        _Hull.Request_Build(FixtureOwner, Get_Origin(0));
        _Slice.Request_Build(FixtureOwner, Get_Origin(1));
        _Lifecycle.Request_Build(FixtureOwner, Get_Origin(2));
        _SliceSelection = 0;
        _LifecycleSelection = 0;
        _Started = true;
        Request_Focus(_Focus);
    }

    UCk_RuntimeConvexGym_SliceFixture Get_FocusedSliceFixture()
    {
        return _Focus == 1 ? _Slice : _Lifecycle;
    }

    float Get_SeparationSpeedCmS() const
    {
        if (_SeparationPreset == 0)
        {
            return 0.0;
        }
        if (_SeparationPreset == 1)
        {
            return 30.0;
        }
        if (_SeparationPreset == 2)
        {
            return 60.0;
        }
        return 120.0;
    }

    int32 Get_Selection() const
    {
        return _Focus == 1 ? _SliceSelection : _LifecycleSelection;
    }

    void Set_Selection(int32 InSelection)
    {
        if (_Focus == 1)
        {
            _SliceSelection = InSelection;
        }
        else
        {
            _LifecycleSelection = InSelection;
        }
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float InDeltaSeconds)
    {
        if (_Started == false)
        {
            return;
        }
        _SliceSelection = Get_ClampedSelection(_Slice, _SliceSelection);
        _LifecycleSelection = Get_ClampedSelection(_Lifecycle, _LifecycleSelection);
        DoDraw_SelectionMarker(_Slice, _SliceSelection);
        DoDraw_SelectionMarker(_Lifecycle, _LifecycleSelection);
    }

    private int32 Get_ClampedSelection(UCk_RuntimeConvexGym_SliceFixture InFixture, int32 InSelection) const
    {
        if (ck::Is_NOT_Valid(InFixture) || InFixture.Get_PieceCount() == 0)
        {
            return 0;
        }
        return Math::Clamp(InSelection, 0, InFixture.Get_PieceCount() - 1);
    }

    private void DoDraw_SelectionMarker(UCk_RuntimeConvexGym_SliceFixture InFixture, int32 InSelection)
    {
        if (ck::Is_NOT_Valid(InFixture) || InFixture.Get_PieceCount() < 2)
        {
            return;
        }
        const auto Piece = InFixture.Get_Piece(InSelection);
        if (ck::Is_NOT_Valid(Piece.Entity) || ck::Is_NOT_Valid(Piece.Geometry))
        {
            return;
        }
        const auto Pose = utils_transform::Get_EntityCurrentTransform(Piece.Entity);
        const auto Centroid = Pose.TransformPosition(utils_runtime_mesh::Get_Metrics(Piece.Geometry).Get_CentroidCm());
        utils_debug_draw::DrawDebugString(Centroid + FVector(0.0, 0.0, 12.0), f"selected {InSelection}", FLinearColor(1.0, 0.9, 0.2, 1.0));
    }

    //------------------------------------------------------------------------
    // Control panel
    //------------------------------------------------------------------------

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        Rows.Add(CkGym_Control::Header("RUNTIME CONVEX"));
        Rows.Add(CkGym_Control::Choice(EKeys::One, "1", "Hull station", _Focus == 0));
        Rows.Add(CkGym_Control::Choice(EKeys::Two, "2", "Slice-to-physics station", _Focus == 1));
        Rows.Add(CkGym_Control::Choice(EKeys::Three, "3", "Lifecycle station", _Focus == 2));
        Rows.Add(CkGym_Control::Toggle(EKeys::J, "J", "Jolt hull view (ck.Jolt.DebugDraw.Enabled)", Get_IsJoltDrawOn()));
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", "Reset this station", _Started));
        if (_Started == false)
        {
            Rows.Add(CkGym_Control::Status("State", "Waiting for the stations", true));
            return Rows;
        }

        if (_Focus == 0)
        {
            DoAdd_HullRows(Rows);
        }
        else
        {
            DoAdd_SliceRows(Rows);
        }
        return Rows;
    }

    private void DoAdd_HullRows(TArray<FCkGym_ControlRow>& OutRows)
    {
        OutRows.Add(CkGym_Control::Header("HULL - 9 POINTS, 100 CM CUBE, 50 KG"));
        const auto Hull = _Hull.Get_Hull();
        if (ck::IsValid(Hull))
        {
            const auto State = utils_jolt_body::Get_SetupState(Hull);
            const auto Failure = utils_jolt_body::Get_SetupFailure(Hull);
            const auto Speed = utils_jolt_body::Get_LinearVelocity(Hull).Size();
            const auto Sleep = utils_jolt_body::Get_SleepState(Hull);
            OutRows.Add(CkGym_Control::Status("Setup", f"{State :n} / {Failure :n}", State == ECk_JoltBody_SetupState::Failed));
            OutRows.Add(CkGym_Control::Status("Motion", f"{Speed :.1} cm/s, {Sleep :n}"));
        }
        OutRows.Add(CkGym_Control::Action(EKeys::G, "G", "Kick: impulse = mass x (0, 150, 300) cm/s", _Hull.Get_IsHullReady()));
        OutRows.Add(CkGym_Control::Action(EKeys::K, "K", "Rejected hull: 4 coplanar points (logs an ensure by design)"));
        const auto Rejected = _Hull.Get_Rejected();
        if (ck::IsValid(Rejected))
        {
            const auto State = utils_jolt_body::Get_SetupState(Rejected);
            const auto Failure = utils_jolt_body::Get_SetupFailure(Rejected);
            const auto Diagnostic = utils_jolt_body::Get_SetupDiagnostic(Rejected);
            OutRows.Add(CkGym_Control::Status("Rejected hull", f"{State :n} / {Failure :n} {Diagnostic}"));
        }
    }

    private void DoAdd_SliceRows(TArray<FCkGym_ControlRow>& OutRows)
    {
        auto Fixture = Get_FocusedSliceFixture();
        const auto Selection = Get_Selection();
        const auto CanCut = Fixture.Get_CanCut(Selection);
        const auto Pieces = Fixture.Get_PieceCount();
        const auto ReadyBodies = Fixture.Get_ReadyBodyCount();
        const auto Phase = Fixture.Get_CutPhase();
        const auto Outcome = Fixture.Get_LastOutcome();
        const auto MaxPieces = Fixture.Get_MaxPieces();

        OutRows.Add(CkGym_Control::Header(_Focus == 1 ? "SLICE TO PHYSICS" : "LIFECYCLE"));
        OutRows.Add(CkGym_Control::Status("Fixture-owned", f"{Pieces} of {MaxPieces} pieces, {ReadyBodies} bodies Ready, cut {Phase}"));
        if (Pieces > 0)
        {
            const auto Piece = Fixture.Get_Piece(Selection);
            const auto Mass = Piece.MassKg;
            const auto Settled = Fixture.Get_IsSettled(Selection) ? "asleep" : "awake or not Ready";
            OutRows.Add(CkGym_Control::Status("Selected", f"piece {Selection}: {Mass :.3} kg, {Settled}"));
        }
        if (_Focus == 2)
        {
            const auto Stale = Fixture.Get_StaleCompletions();
            const auto Abandoned = Fixture.Get_AbandonedAliveCount();
            const auto Partial = Fixture.Get_PartialFailures();
            OutRows.Add(CkGym_Control::Status("Stale completions discarded", f"{Stale}; abandoned entities not yet torn down {Abandoned}"));
            OutRows.Add(CkGym_Control::Status("Partial compositions", f"{Partial}"));
        }
        OutRows.Add(CkGym_Control::Status("Last outcome", Outcome.IsEmpty() ? "-" : Outcome));
        OutRows.Add(CkGym_Control::Status("Cut policy", "the selected piece must be Ready and asleep (Jolt's rest verdict); it turns Kinematic and stops colliding while the cut is in flight; the cut goes through its centroid, alternating world Y and world X normals"));

        OutRows.Add(CkGym_Control::Action(EKeys::G, "G", "Cut the selected piece (source destroyed once both halves are Ready)", CanCut));
        OutRows.Add(CkGym_Control::Action(EKeys::N, "N", "Select the next piece", Pieces > 1));
        if (_Focus == 1)
        {
            const auto Speed = Get_SeparationSpeedCmS();
            OutRows.Add(CkGym_Control::Cycle(EKeys::V, "V", "Separation speed (mass x speed per half)", f"{Speed :.0} cm/s"));
            return;
        }
        OutRows.Add(CkGym_Control::Action(EKeys::K, "K", "Destroy the selected piece (the others live)", Pieces > 0 && Fixture.Get_IsCutInFlight() == false));
        OutRows.Add(CkGym_Control::Action(EKeys::Z, "Z", "Cut, then reset as the slice is queued (same call stack)", CanCut));
        OutRows.Add(CkGym_Control::Action(EKeys::B, "B", "Cut, then reset with both half bodies pending (same call stack)", CanCut));
        OutRows.Add(CkGym_Control::Action(EKeys::M, "M", "Cut, then destroy one half before its body is admitted", CanCut));
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        auto Rows = Get_ControlRows();
        if (Rows.IsValidIndex(InRowIndex) == false)
        {
            return;
        }
        const auto Key = Rows[InRowIndex].Key;
        if (Key == EKeys::One)
        {
            Request_Focus(0);
        }
        else if (Key == EKeys::Two)
        {
            Request_Focus(1);
        }
        else if (Key == EKeys::Three)
        {
            Request_Focus(2);
        }
        else if (Key == EKeys::J)
        {
            Request_ToggleJoltDraw();
        }
        else if (_Started == false)
        {
            return;
        }
        else if (Key == EKeys::X)
        {
            Request_ResetFocused();
        }
        else if (_Focus == 0)
        {
            DoDispatch_Hull(Key);
        }
        else
        {
            DoDispatch_Slice(Key);
        }
    }

    private void DoDispatch_Hull(FKey InKey)
    {
        if (InKey == EKeys::G)
        {
            _Hull.Request_Kick();
        }
        else if (InKey == EKeys::K)
        {
            _Hull.Request_RejectedHull();
        }
    }

    private void DoDispatch_Slice(FKey InKey)
    {
        auto Fixture = Get_FocusedSliceFixture();
        auto Cut = FCkRuntimeConvexGym_CutRequest();
        Cut.PieceIndex = Get_Selection();
        Cut.SeparationSpeedCmS = Get_SeparationSpeedCmS();

        if (InKey == EKeys::G)
        {
            Fixture.Request_Cut(Cut);
        }
        else if (InKey == EKeys::N)
        {
            Set_Selection(Fixture.Get_PieceCount() > 0 ? (Get_Selection() + 1) % Fixture.Get_PieceCount() : 0);
        }
        else if (InKey == EKeys::V && _Focus == 1)
        {
            _SeparationPreset = (_SeparationPreset + 1) % 4;
        }
        else if (InKey == EKeys::K && _Focus == 2)
        {
            Fixture.Request_DestroyPiece(Get_Selection());
        }
        else if (InKey == EKeys::Z && _Focus == 2)
        {
            Cut.Interrupt = ECkRuntimeConvexGym_CutInterrupt::ResetAfterSliceSubmitted;
            Fixture.Request_Cut(Cut);
        }
        else if (InKey == EKeys::B && _Focus == 2)
        {
            Cut.Interrupt = ECkRuntimeConvexGym_CutInterrupt::ResetAfterHalvesComposed;
            Fixture.Request_Cut(Cut);
        }
        else if (InKey == EKeys::M && _Focus == 2)
        {
            Cut.Interrupt = ECkRuntimeConvexGym_CutInterrupt::DestroyNegativeHalfBeforeAdmission;
            Fixture.Request_Cut(Cut);
        }
    }

    void Request_ResetFocused()
    {
        if (_Focus == 0)
        {
            _Hull.Request_Reset();
            return;
        }
        Get_FocusedSliceFixture().Request_Reset();
        Set_Selection(0);
    }

    void Request_Focus(int32 InIndex)
    {
        _Focus = InIndex;
        auto Pawn = GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn) || _Started == false)
        {
            return;
        }
        auto Target = Get_Origin(InIndex);
        auto Eye = Target + FVector(70.0, -55.0, 45.0);
        if (InIndex == 0)
        {
            Target = Target + FVector(0.0, 0.0, 60.0);
            Eye = Target + FVector(520.0, -420.0, 260.0);
        }
        Pawn.SetActorLocation(Eye);
        SetControlRotation((Target - Eye).Rotation());
    }

    void Request_ToggleJoltDraw()
    {
        auto DrawCVarName = n"ck.Jolt.DebugDraw.Enabled";
        if (UCk_Utils_AutoTest_UE::Get_CVarExists(DrawCVarName) == false)
        {
            ck::Error("Runtime Convex gym: ck.Jolt.DebugDraw.Enabled does not exist; the Jolt module is not loaded.");
            return;
        }
        const auto WasOn = Get_IsJoltDrawOn();
        if (_JoltDrawOverridden)
        {
            UCk_Utils_AutoTest_UE::Request_PopCVarOverride(DrawCVarName);
        }
        UCk_Utils_AutoTest_UE::Request_PushCVarOverride(DrawCVarName, WasOn ? "0" : "1");
        _JoltDrawOverridden = true;
    }

    bool Get_IsJoltDrawOn() const
    {
        return System::GetConsoleVariableBoolValue("ck.Jolt.DebugDraw.Enabled");
    }

    UFUNCTION(Exec)
    void Ck_RuntimeConvex_Control(int32 InRowIndex)
    {
        Request_ControlActivated(InRowIndex);
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        _Started = false;
        if (ck::IsValid(_Hull))
        {
            _Hull.Request_Teardown();
            _Slice.Request_Teardown();
            _Lifecycle.Request_Teardown();
        }
        if (_JoltDrawOverridden)
        {
            UCk_Utils_AutoTest_UE::Request_PopCVarOverride(n"ck.Jolt.DebugDraw.Enabled");
            _JoltDrawOverridden = false;
        }
    }
}
