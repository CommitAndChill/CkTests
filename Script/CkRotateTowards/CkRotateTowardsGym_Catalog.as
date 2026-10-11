USTRUCT()
struct FCkRotateTowardsGym_VisualRow
{
    UPROPERTY() int32 Slot=-1;
    UPROPERTY() int32 Station=-1;
}
USTRUCT()
struct FCkRotateTowardsGym_TurretRow
{
    UPROPERTY() int32 Slot=-1;
    UPROPERTY() int32 Station=-1;
    UPROPERTY() FRotator Desired;
    UPROPERTY() FRotator Remaining;
    UPROPERTY() bool Enabled=false;
    UPROPERTY() bool AtTarget=false;
    UPROPERTY() bool Clamped=false;
    UPROPERTY() int32 Mode=0;
    UPROPERTY() bool YawLocked=false;
    UPROPERTY() bool PitchLocked=false;
    UPROPERTY() float TurnRate=0;
    UPROPERTY() float PitchRate=0;
    UPROPERTY() float RollRate=0;
    UPROPERTY() float ReachedTolerance=1;
}
USTRUCT()
struct FCkRotateTowardsGym_Snapshot
{
    UPROPERTY() int32 CatalogVersion=0;
    UPROPERTY() int32 Generation=0;
    UPROPERTY() int32 Sequence=0;
    UPROPERTY() bool Ready=false;
    UPROPERTY() bool Failed=false;
    UPROPERTY() FString FailureReason;
    // Authored identity stays separate from the applied authority layout.
    UPROPERTY() TArray<FCk_GymStation_SpawnParams> Stations;
    UPROPERTY() TArray<FCk_GymStation_SpawnParams> PassiveStations;
    UPROPERTY() TArray<FCkGym_StationVisibleGeometry> StationGeometry;
    UPROPERTY() TArray<FCkRotateTowardsGym_VisualRow> Visuals;
    UPROPERTY() TArray<FCkRotateTowardsGym_TurretRow> Turrets;
    UPROPERTY() TArray<FVector> Origins;
    UPROPERTY() TArray<FString> StationTitles;
    UPROPERTY() TArray<FString> StationBodies;
    UPROPERTY() TArray<FString> StationInstructions;
}
USTRUCT()
struct FCkRotateTowardsGym_ControlCommit
{
    UPROPERTY() int32 DeliverySerial=0;
    UPROPERTY() int32 RequestSerial=0;
    UPROPERTY() int32 OriginGeneration=0;
    UPROPERTY() ACk_RotateTowardsGym_GameState OriginState;
    UPROPERTY() ACk_RotateTowardsGym_PlayerController Owner;
    UPROPERTY() ACk_Gym_Base_Pawn OwnerPawn;
    UPROPERTY() FCkRotateTowardsGym_Snapshot Snapshot;
}
namespace CkRotateTowardsShared
{
    const int32 SlotCount = 3;
    const int32 CatalogVersion = 1;
    bool IsLive(FCk_Handle H)
    { return ck::IsValid(H) && !utils_entity_lifetime::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    ACk_RotateTowardsGym_GameState Get_State(UWorld World)
    {
        if (ck::Is_NOT_Valid(World)) { return nullptr; }
        auto State = Cast<ACk_RotateTowardsGym_GameState>(World.GetGameState());
        return ck::IsValid(State) && State.GetWorld() == World && State.GetClass() == ACk_RotateTowardsGym_GameState ? State : nullptr;
    }
    FName Get_StationTag(int32 InIndex)
    {
        if (InIndex == 0)
        {
            return n"Gym.CkRotateTowards.Tracker";
        }
        if (InIndex == 1)
        {
            return n"Gym.CkRotateTowards.Clamped";
        }
        return n"Gym.CkRotateTowards.Modes";
    }

    TArray<FCkGym_Station_SpawnParams_Payload> Get_Payloads()
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
                Station.Title = FText::FromString("TRACKER - RATE-LIMITED VS INSTANT");
                Station.Description.Add(FText::FromString("The small cube sweeps +-300 cm sideways, 400 cm ahead. Left turret: rate-limited at 60 deg/s. Right turret: Instant."));
                Station.Description.Add(FText::FromString("Watch the left turret lag and catch up smoothly at each end with no stepping; the right one stays locked on. O draws each turret's target line (green at target, yellow while turning)."));
            }
            else if (Index == 1)
            {
                Station.Title = FText::FromString("CLAMPED - +-45 DEG YAW ABOUT THE REST LINE");
                Station.Description.Add(FText::FromString("The rest point is the tiny cube 400 cm straight ahead. The target sweeps across the front between +70 and -70 deg of yaw, beyond the clamp at both ends."));
                Station.Description.Add(FText::FromString("The turret tracks the target while it is inside +-45 deg, then stops at the edge and reports at target there until the target comes back. O draws the blue rest line and the cyan range edges."));
            }
            else
            {
                Station.Title = FText::FromString("MODES - FREE / YAW-LOCKED / PITCH-LOCKED / DISABLED");
                Station.Description.Add(FText::FromString("One cube sweeps sideways (+-300 cm) and up and down (+-150 cm). Left to right: free, yaw locked, pitch locked, disabled."));
                Station.Description.Add(FText::FromString("The yaw-locked turret only pitches, the pitch-locked one only yaws, and the disabled one never moves (red with O on)."));
            }
            Stations.Add(Station);
        }
        return Stations;
    }

    FCk_GymStation_SpawnParams Get_Params(int32 Slot)
    {
        FCk_GymStation_SpawnParams P; auto All=Get_Payloads();
        if (Slot<0 || Slot>=All.Num()) { return P; } auto V=All[Slot];
        P.InitialTransform=V.Transform; P.Width=double(V.Width); P.Depth=double(V.Depth); P.Height=double(V.Height);
        P.TitleText=V.Title; P.DescriptionText=V.Description; P.TitleScale=double(V.TitleScale); P.DescriptionScale=double(V.DescriptionScale);
        P.TitleColour=V.TitleColour.ToFColor(true); P.DescriptionColour=V.DescriptionColour.ToFColor(true);
        P.AutoSize=V.AutoSize; P.ShowDebugOverlays=V.ShowDebugOverlays; P.StationTags=V.Tags; return P;
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

    // Normalize only the proven exact serializer representation in a local copy.
    bool Get_IsExactWireParams(FCk_GymStation_SpawnParams InParams, int32 InSlot)
    {
        if (!Get_HasFiniteRecipe(InParams, InSlot)) { return false; }
        auto Expected = Get_Params(InSlot);
        if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
            Expected.InitialTransform, InParams.InitialTransform)) { return false; }
        auto Normalized = InParams;
        Normalized.InitialTransform = Expected.InitialTransform;
        return FInstancedStruct::Make(Normalized) == FInstancedStruct::Make(Expected);
    }

    bool Get_IsExactStationScript(FCk_Handle InStation)
    {
        if (!IsLive(InStation) || !utils_entity_script::Has(InStation)) { return false; }
        auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(InStation);
        return ck::IsValid(Script) && Script.GetClass() == UCk_EntityScript_GymStation;
    }
    // The station's literal body/text/light layout, shared by the source-applied
    // check and the received source-versus-client geometry comparison.
    bool TryGet_AuthoredPieceWorldPoses(FCk_GymStation_SpawnParams P,
        TArray<FTransform>&out Out)
    {
        Out.Empty();
        if (!P.InitialTransform.IsValid() || P.InitialTransform.ContainsNaN() ||
            !Math::IsFinite(P.Width) || P.Width <= 0 ||
            !Math::IsFinite(P.Depth) || P.Depth <= 0 ||
            !Math::IsFinite(P.Height) || P.Height <= 0 ||
            !Math::IsFinite(P.WallThickness) || !Math::IsFinite(P.FloorThickness) ||
            !Math::IsFinite(P.TrimDepth) || !Math::IsFinite(P.TitleScale)) { return false; }
        const auto W = P.Width * 100.0;
        const auto D = P.Depth * 100.0;
        const auto H = P.Height * 100.0;
        const auto WT = P.WallThickness;
        const auto FT = P.FloorThickness;
        const auto TrimX = -D * 0.5 + WT + P.TrimDepth * 0.5;
        const auto TextX = -P.Depth * 50.0 + WT + 5.0;
        const auto WallTextY = P.TextAlignment == EHorizTextAligment::EHTA_Center ? 0.0 :
            (P.TextAlignment == EHorizTextAligment::EHTA_Left ? 1.0 : -1.0) * (P.Width - 1.0) * 50.0;
        const auto FloorTextY = P.FloorTextAlignment == EHorizTextAligment::EHTA_Center ? 0.0 :
            (P.FloorTextAlignment == EHorizTextAligment::EHTA_Left ? 1.0 : -1.0) * (P.Width - 1.0) * 50.0;
        const auto FloorTextX = P.FloorTextPlacement == ECk_GymStation_FloorTextPlacement::Center ? 0.0 :
            (P.FloorTextPlacement == ECk_GymStation_FloorTextPlacement::Front ? 1.0 : -1.0) * (P.Depth - 1.0) * 50.0;
        TArray<FTransform> Local;
        Local.Add(FTransform(FRotator::ZeroRotator, FVector(-D * 0.5 + WT * 0.5, 0, H * 0.5), FVector(WT / 100.0, P.Width, P.Height)));
        Local.Add(FTransform(FRotator::ZeroRotator, FVector(0, 0, FT * 0.5), FVector(P.Depth, P.Width, FT / 100.0)));
        Local.Add(FTransform(FRotator::ZeroRotator, FVector(TrimX, W * 0.5 - WT * 0.5, H * 0.5), FVector(P.TrimDepth / 100.0, WT / 100.0, P.Height)));
        Local.Add(FTransform(FRotator::ZeroRotator, FVector(TrimX, -W * 0.5 + WT * 0.5, H * 0.5), FVector(P.TrimDepth / 100.0, WT / 100.0, P.Height)));
        Local.Add(FTransform(FRotator::ZeroRotator, FVector(TrimX, 0, H - WT * 0.5), FVector(P.TrimDepth / 100.0, P.Width, WT / 100.0)));
        Local.Add(FTransform(FRotator::ZeroRotator, FVector(TrimX, 0, FT + WT * 0.5), FVector(P.TrimDepth / 100.0, P.Width, WT / 100.0)));
        Local.Add(FTransform(FRotator::ZeroRotator, FVector(TextX, WallTextY, H - (P.TitleScale + 60.0) / 2.0)));
        Local.Add(FTransform(FRotator::ZeroRotator, FVector(TextX, WallTextY, H - (P.TitleScale + 60.0))));
        Local.Add(FTransform(FRotator(90.0, 0, 0), FVector(FloorTextX, FloorTextY, FT + 2.0)));
        Local.Add(FTransform(FRotator(-30.0, 180.0, 0), FVector(P.Depth * 30.0, 0, P.Height * 90.0)));
        if (Local.Num() != 10) { return false; }
        TArray<FTransform> Candidate;
        for (auto Pose : Local)
        {
            auto WorldPose = Pose * P.InitialTransform;
            if (!WorldPose.IsValid() || WorldPose.ContainsNaN()) { return false; }
            Candidate.Add(WorldPose);
        }
        if (Candidate.Num() != 10) { return false; }
        Out = Candidate;
        return true;
    }

    // An oracle for applied values; the captured geometry is still the native readback.
    bool Get_IsAppliedStationLayout(FCk_GymStation_SpawnParams P, FCkGym_StationVisibleGeometry G)
    {
        if (G.Pieces.Num() != 10 || G.Anchors.Num() != 7 ||
            P.Width != G.Width || P.Depth != G.Depth || P.Height != G.Height) { return false; }
        TArray<FTransform> Expected;
        if (!TryGet_AuthoredPieceWorldPoses(P, Expected)) { return false; }
        for (int32 i = 0; i < Expected.Num(); ++i)
        {
            auto R = G.Pieces[i];
            if (int32(R.Role) != i || !R.NodeWorldTransform.Equals(Expected[i], 0.0) ||
                !R.ComponentWorldTransform.Equals(R.NodeWorldTransform, 0.0)) { return false; }
            if (i < 6 && (R.MeshAssetPath != "/Engine/BasicShapes/Cube.Cube" ||
                R.CollisionEnabled != int32(ECollisionEnabled::NoCollision) ||
                R.MeshColor != (i < 2 ? P.BodyColour : P.TrimColour) || !R.Visible || R.HiddenInGame)) { return false; }
        }
        auto Light = G.Pieces[9];
        if (Light.Visible != P.ShowSpotlight || (P.ShowSpotlight &&
            (Light.SpotIntensity != 50000.0f || Light.SpotAttenuationRadius != 2000.0f ||
                Light.SpotInnerConeAngle != 20.0f || Light.SpotOuterConeAngle != 40.0f))) { return false; }
        return true;
    }

    // Fallback to explicit field comparison: native visible-piece properties are
    // BlueprintReadOnly, so AngelScript cannot safely rewrite their transforms
    // in copies for an FInstancedStruct comparison. This lists every non-pose
    // FCkGym_StationVisiblePiece field in the reflected native row.
    bool Get_IsExactVisiblePieceStyle(FCkGym_StationVisiblePiece A,
        FCkGym_StationVisiblePiece B)
    {
        return A.Visible == B.Visible && A.HiddenInGame == B.HiddenInGame &&
            A.MeshAssetPath == B.MeshAssetPath &&
            A.ParentMaterialPath == B.ParentMaterialPath &&
            A.BaseMaterialPath == B.BaseMaterialPath && A.MeshColor == B.MeshColor &&
            A.CollisionEnabled == B.CollisionEnabled && A.Text == B.Text &&
            A.FontPath == B.FontPath && A.TextMaterialPath == B.TextMaterialPath &&
            A.ShownTextLocalSize == B.ShownTextLocalSize &&
            A.TextWorldSize == B.TextWorldSize && A.TextXScale == B.TextXScale &&
            A.TextYScale == B.TextYScale &&
            A.TextHorizontalSpacing == B.TextHorizontalSpacing &&
            A.TextVerticalSpacing == B.TextVerticalSpacing &&
            A.TextColor == B.TextColor &&
            A.HorizontalAlignment == B.HorizontalAlignment &&
            A.VerticalAlignment == B.VerticalAlignment &&
            A.SpotIntensity == B.SpotIntensity &&
            A.SpotAttenuationRadius == B.SpotAttenuationRadius &&
            A.SpotInnerConeAngle == B.SpotInnerConeAngle &&
            A.SpotOuterConeAngle == B.SpotOuterConeAngle &&
            A.SpotColor == B.SpotColor &&
            A.SpotIntensityUnits == B.SpotIntensityUnits;
    }

    // Diagnostic values are read only after the original authored-or-Iris
    // predicate rejected a pose. This never normalizes a received transform.
    FString Get_SourcePassivePoseFailure(FString Stage, int32 Slot, int32 Index,
        FTransform Expected, FTransform SourcePose, FTransform LocalPose)
    {
        if (!Expected.IsValid() || Expected.ContainsNaN() ||
            !SourcePose.IsValid() || SourcePose.ContainsNaN() ||
            !LocalPose.IsValid() || LocalPose.ContainsNaN())
        { return f"{Stage} slot={Slot} index={Index} invalid-pose"; }
        return f"{Stage} slot={Slot} index={Index} " +
            f"sourceExact0={SourcePose.Equals(Expected, 0.0)} " +
            f"sourceDefault={SourcePose.Equals(Expected)} " +
            f"localExact0={LocalPose.Equals(Expected, 0.0)} " +
            f"localDefault={LocalPose.Equals(Expected)} " +
            f"sameWireExact0={SourcePose.Equals(LocalPose, 0.0)} " +
            f"sameWireDefault={SourcePose.Equals(LocalPose)}";
    }

    // Compare two actual readbacks against the SAME authored effective poses.
    // A received quaternion is never used as the source of another Iris pass.
    bool Get_IsExactSourceAndPassiveGeometry(int32 Slot,
        FCk_GymStation_SpawnParams Passive,
        FCkGym_StationVisibleGeometry Source,
        FCkGym_StationVisibleGeometry Local)
    {
        FString IgnoredFailure;
        return TryValidate_SourceAndPassiveGeometry(
            Slot, Passive, Source, Local, IgnoredFailure);
    }

    bool TryValidate_SourceAndPassiveGeometry(int32 Slot,
        FCk_GymStation_SpawnParams Passive,
        FCkGym_StationVisibleGeometry Source,
        FCkGym_StationVisibleGeometry Local, FString&out Failure)
    {
        Failure = "";
        if (Slot < 0 || Slot >= SlotCount || Passive.AutoSize || !Source.AutoSize ||
            Local.AutoSize || Source.Pieces.Num() != 10 || Local.Pieces.Num() != 10 ||
            Source.Anchors.Num() != 7 || Local.Anchors.Num() != 7 ||
            !Math::IsFinite(Passive.Width) || Passive.Width <= 0 ||
            !Math::IsFinite(Passive.Depth) || Passive.Depth <= 0 ||
            !Math::IsFinite(Passive.Height) || Passive.Height <= 0 ||
            Source.Width != Passive.Width || Local.Width != Passive.Width ||
            Source.Depth != Passive.Depth || Local.Depth != Passive.Depth ||
            Source.Height != Passive.Height || Local.Height != Passive.Height)
        { Failure = f"header-dimensions slot={Slot}"; return false; }
        auto Authored = Get_Params(Slot);
        auto Identity = Passive;
        Identity.AutoSize = Authored.AutoSize;
        Identity.Width = Authored.Width;
        Identity.Height = Authored.Height;
        if (!Get_IsExactWireParams(Identity, Slot))
        { Failure = f"identity-wire slot={Slot}"; return false; }
        auto Effective = Passive;
        Effective.InitialTransform = Authored.InitialTransform;
        TArray<FTransform> Expected;
        if (!TryGet_AuthoredPieceWorldPoses(Effective, Expected))
        { Failure = f"expected-poses slot={Slot}"; return false; }
        if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                Authored.InitialTransform, Source.StationWorldTransform))
        { Failure = Get_SourcePassivePoseFailure("source-world", Slot, -1,
              Authored.InitialTransform, Source.StationWorldTransform,
              Local.StationWorldTransform); return false; }
        if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                Authored.InitialTransform, Local.StationWorldTransform))
        { Failure = Get_SourcePassivePoseFailure("local-world", Slot, -1,
              Authored.InitialTransform, Source.StationWorldTransform,
              Local.StationWorldTransform); return false; }
        for (int32 i = 0; i < Expected.Num(); ++i)
        {
            auto S = Source.Pieces[i]; auto C = Local.Pieces[i];
            if (int32(S.Role) != i || C.Role != S.Role)
            { Failure = f"piece-role slot={Slot} index={i}"; return false; }
            if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                    Expected[i], S.NodeWorldTransform))
            { Failure = Get_SourcePassivePoseFailure("piece-source-node", Slot, i,
                  Expected[i], S.NodeWorldTransform, C.NodeWorldTransform); return false; }
            if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                    Expected[i], S.ComponentWorldTransform))
            { Failure = Get_SourcePassivePoseFailure("piece-source-component", Slot, i,
                  Expected[i], S.ComponentWorldTransform, C.ComponentWorldTransform); return false; }
            if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                    Expected[i], C.NodeWorldTransform))
            { Failure = Get_SourcePassivePoseFailure("piece-local-node", Slot, i,
                  Expected[i], S.NodeWorldTransform, C.NodeWorldTransform); return false; }
            if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                    Expected[i], C.ComponentWorldTransform))
            { Failure = Get_SourcePassivePoseFailure("piece-local-component", Slot, i,
                  Expected[i], S.ComponentWorldTransform, C.ComponentWorldTransform); return false; }
            if (!Get_IsExactVisiblePieceStyle(S, C))
            { Failure = f"piece-style slot={Slot} index={i}"; return false; }
        }
        const auto D = Passive.Depth * 100.0;
        const auto W = Passive.Width * 100.0;
        const auto H = Passive.Height * 100.0;
        TArray<FVector> Offsets;
        Offsets.Add(FVector::ZeroVector);
        Offsets.Add(FVector(D * 0.5 + Passive.AgentSpawnOffset, 0, 0));
        Offsets.Add(FVector(-D * 0.5 - Passive.AgentSpawnOffset, 0, 0));
        Offsets.Add(FVector(0, W * 0.5 + Passive.AgentSpawnOffset, 0));
        Offsets.Add(FVector(0, -W * 0.5 - Passive.AgentSpawnOffset, 0));
        Offsets.Add(FVector(D * 0.5, 0, H));
        Offsets.Add(FVector(0, 0, H * 0.5));
        for (int32 i = 0; i < Offsets.Num(); ++i)
        {
            auto Pose = FTransform(Offsets[i]) * Authored.InitialTransform;
            if (!Pose.IsValid() || Pose.ContainsNaN())
            { Failure = f"anchor-expected-pose slot={Slot} index={i}"; return false; }
            if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                    Pose, Source.Anchors[i]))
            { Failure = Get_SourcePassivePoseFailure("anchor-source", Slot, i,
                  Pose, Source.Anchors[i], Local.Anchors[i]); return false; }
            if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                    Pose, Local.Anchors[i]))
            { Failure = Get_SourcePassivePoseFailure("anchor-local", Slot, i,
                  Pose, Source.Anchors[i], Local.Anchors[i]); return false; }
        }
        return true;
    }

    // Read every retained station recipe field only after the exact live script
    // and its native entity association have been validated.
    bool TryReadActualStationParams(FCk_Handle H, int32 Slot,
        FCk_GymStation_SpawnParams&out Out)
    {
        Out = FCk_GymStation_SpawnParams();
        if (Slot < 0 || Slot >= SlotCount || !Get_IsExactStationScript(H) ||
            !utils_transform::Has(H)) { return false; }
        auto S = Cast<UCk_EntityScript_GymStation>(utils_entity_script::TryGet_EntityScript(H));
        if (ck::Is_NOT_Valid(S) ||
            UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(S) != H) { return false; }
        FCk_GymStation_SpawnParams Actual;
        Actual.InitialTransform = S.InitialTransform;
        Actual.Width = S.Width;
        Actual.Depth = S.Depth;
        Actual.Height = S.Height;
        Actual.WallThickness = S.WallThickness;
        Actual.FloorThickness = S.FloorThickness;
        Actual.TrimDepth = S.TrimDepth;
        Actual.AgentSpawnOffset = S.AgentSpawnOffset;
        Actual.BodyColour = S.BodyColour;
        Actual.TrimColour = S.TrimColour;
        Actual.TitleText = S.TitleText;
        Actual.TitleScale = S.TitleScale;
        Actual.TitleColour = S.TitleColour;
        Actual.DescriptionText = S.DescriptionText;
        Actual.DescriptionScale = S.DescriptionScale;
        Actual.DescriptionColour = S.DescriptionColour;
        Actual.TextAlignment = S.TextAlignment;
        Actual.FloorDescriptionText = S.FloorDescriptionText;
        Actual.FloorDescriptionScale = S.FloorDescriptionScale;
        Actual.FloorDescriptionColour = S.FloorDescriptionColour;
        Actual.FloorTextAlignment = S.FloorTextAlignment;
        Actual.FloorTextPlacement = S.FloorTextPlacement;
        Actual.ShowSpotlight = S.ShowSpotlight;
        Actual.ShowAnchors = S.ShowAnchors;
        Actual.ShowDebugOverlays = S.ShowDebugOverlays;
        Actual.AutoSize = S.AutoSize;
        Actual.StationTags = S.StationTags;
        if (!Get_HasFiniteRecipe(Actual, Slot)) { return false; }
        Out = Actual;
        return true;
    }

    // Capture the actual authority layout after native setup. No text-length reconstruction.
    bool TryCapture_StationPresentation(FCk_Handle H, int32 Slot,
        FCk_GymStation_SpawnParams&out Passive, FCkGym_StationVisibleGeometry&out Geometry, FString&out Failure)
    {
        Failure = "";
        Passive = FCk_GymStation_SpawnParams();
        Geometry = FCkGym_StationVisibleGeometry();
        if (Slot < 0 || Slot >= SlotCount || !Get_IsExactStationScript(H) ||
            !utils_transform::Has(H)) { Failure = "station-schema"; return false; }
        auto S = Cast<UCk_EntityScript_GymStation>(utils_entity_script::TryGet_EntityScript(H));
        if (ck::Is_NOT_Valid(S)) { Failure = "station-script"; return false; }
        FCkGym_StationVisibleGeometry Captured;
        if (!S.TryGet_VisibleGeometry(Captured) || !Captured.AutoSize ||
            Captured.Pieces.Num() != 10 || Captured.Anchors.Num() != 7) { Failure = "visible-readback-or-shape"; return false; }
        FCk_GymStation_SpawnParams Actual;
        if (!TryReadActualStationParams(H, Slot, Actual)) { Failure = "actual-params"; return false; }
        if (!Get_HasFiniteRecipe(Actual, Slot) || Actual.Width <= 0 ||
            Actual.Depth <= 0 || Actual.Height <= 0 ||
            Actual.Width != Captured.Width || Actual.Depth != Captured.Depth ||
            Actual.Height != Captured.Height ||
            !Captured.StationWorldTransform.Equals(Actual.InitialTransform, 0.0)) { Failure = "dimensions-or-world-pose"; return false; }
        auto Authored = Get_Params(Slot);
        auto Identity = Actual;
        Identity.Width = Authored.Width;
        if (Actual.Depth != Authored.Depth) { Failure = "authored-depth"; return false; }
        Identity.Height = Authored.Height;
        if (!Get_IsExactAuthoredParams(Identity, Slot)) { Failure = "authored-identity"; return false; }
        for (int32 i = 0; i < Captured.Pieces.Num(); ++i)
        {
            if (int32(Captured.Pieces[i].Role) != i ||
                !Captured.Pieces[i].NodeWorldTransform.IsValid() ||
                Captured.Pieces[i].NodeWorldTransform.ContainsNaN() ||
                !Captured.Pieces[i].ComponentWorldTransform.IsValid() ||
                Captured.Pieces[i].ComponentWorldTransform.ContainsNaN() ||
                !Captured.Pieces[i].ComponentWorldTransform.Equals(
                    Captured.Pieces[i].NodeWorldTransform, 0.0)) { Failure = "piece-role-or-pose"; return false; }
        }
        // Dimension fields and anchor fragments update before queued SceneNode offsets drain.
        // Require the actual anchor nodes to have caught up, and actual component pushes above.
        const auto D = Captured.Depth * 100.0;
        const auto W = Captured.Width * 100.0;
        const auto Height = Captured.Height * 100.0;
        TArray<FVector> ExpectedAnchorOffsets;
        ExpectedAnchorOffsets.Add(FVector::ZeroVector);
        ExpectedAnchorOffsets.Add(FVector(D * 0.5 + Actual.AgentSpawnOffset, 0, 0));
        ExpectedAnchorOffsets.Add(FVector(-D * 0.5 - Actual.AgentSpawnOffset, 0, 0));
        ExpectedAnchorOffsets.Add(FVector(0, W * 0.5 + Actual.AgentSpawnOffset, 0));
        ExpectedAnchorOffsets.Add(FVector(0, -W * 0.5 - Actual.AgentSpawnOffset, 0));
        ExpectedAnchorOffsets.Add(FVector(D * 0.5, 0, Height));
        ExpectedAnchorOffsets.Add(FVector(0, 0, Height * 0.5));
        for (int32 i = 0; i < Captured.Anchors.Num(); ++i)
        {
            auto Transform = Captured.Anchors[i];
            if (!Transform.IsValid() || Transform.ContainsNaN() ||
                Transform.GetTranslation() != Actual.InitialTransform.TransformPosition(ExpectedAnchorOffsets[i]) ||
                Transform.GetRotation() != Actual.InitialTransform.GetRotation() ||
                Transform.GetScale3D() != Actual.InitialTransform.GetScale3D()) { Failure = "anchor-pose"; return false; }
        }
        if (!Get_IsAppliedStationLayout(Actual, Captured)) { Failure = "applied-layout"; return false; }
        // Passive clients consume these measured dimensions; they never run AutoSize.
        Actual.AutoSize = false;
        Passive = Actual;
        Geometry = Captured;
        return true;
    }

    // The client station is a distinct, passive entity. Read its actual applied
    // recipe and native geometry, then compare both to one received source row.
    bool TryCapture_PassiveStationPresentation(FCk_Handle H, FCk_Handle ExpectedParent,
        int32 Slot, FCkRotateTowardsGym_Snapshot Snapshot,
        FCkGym_StationVisibleGeometry&out LocalGeometry)
    {
        LocalGeometry = FCkGym_StationVisibleGeometry();
        if (Slot < 0 || Slot >= SlotCount || Snapshot.CatalogVersion != CatalogVersion ||
            Snapshot.Generation <= 0 || Snapshot.Sequence <= 0 || !Snapshot.Ready ||
            Snapshot.Failed || Snapshot.PassiveStations.Num() != SlotCount ||
            Snapshot.StationGeometry.Num() != SlotCount ||
            !IsLive(ExpectedParent) || !IsLive(H) || H == ExpectedParent ||
            !utils_entity_lifetime::Has_LifetimeOwner(H) ||
            utils_entity_lifetime::Get_LifetimeOwner(H) != ExpectedParent ||
            !Get_IsExactStationScript(H)) { return false; }
        FCk_GymStation_SpawnParams Actual;
        if (!TryReadActualStationParams(H, Slot, Actual)) { return false; }
        auto Passive = Snapshot.PassiveStations[Slot];
        if (!Get_HasFiniteRecipe(Passive, Slot) || !Get_HasFiniteRecipe(Actual, Slot) ||
            Passive.AutoSize || Actual.AutoSize ||
            !UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                Passive.InitialTransform, Actual.InitialTransform)) { return false; }
        // Only the independently validated Iris pose is normalized in a copy.
        auto Compared = Actual;
        Compared.InitialTransform = Passive.InitialTransform;
        if (FInstancedStruct::Make(Compared) != FInstancedStruct::Make(Passive)) { return false; }
        auto S = Cast<UCk_EntityScript_GymStation>(utils_entity_script::TryGet_EntityScript(H));
        if (ck::Is_NOT_Valid(S) ||
            UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(S) != H) { return false; }
        FCkGym_StationVisibleGeometry Captured;
        if (!S.TryGet_VisibleGeometry(Captured) ||
            !Get_IsExactSourceAndPassiveGeometry(Slot, Passive,
                Snapshot.StationGeometry[Slot], Captured)) { return false; }
        LocalGeometry = Captured;
        return true;
    }

    bool Get_IsExactStationRecipe(FCk_Handle H, int32 Slot)
    {
        if (Slot < 0 || Slot >= SlotCount || !Get_IsExactStationScript(H) ||
            !H.Has_Fragment(FCkGym_Station_Anchors) || !utils_transform::Has(H)) { return false; }
        auto S = Cast<UCk_EntityScript_GymStation>(utils_entity_script::TryGet_EntityScript(H));
        if (ck::Is_NOT_Valid(S)) { return false; }
        FCk_GymStation_SpawnParams Actual;
        if (!TryReadActualStationParams(H, Slot, Actual)) { return false; }
        auto Effective=Actual;
        if (!Math::IsFinite(Actual.Width) || Actual.Width<=0 || !Math::IsFinite(Actual.Depth) || Actual.Depth<=0 ||
            !Math::IsFinite(Actual.Height) || Actual.Height<=0) { return false; }
        auto Recipe=Get_Params(Slot);
        if (Actual.Depth != Recipe.Depth) { return false; }
        Actual.Width=Recipe.Width; Actual.Height=Recipe.Height;
        if (!Get_IsExactWireParams(Actual, Slot) ||
            !utils_transform::Get_EntityCurrentTransform(H).Equals(Actual.InitialTransform, 0.0)) { return false; }
        auto A = H.Get_Fragment(FCkGym_Station_Anchors);
        auto T = Actual.InitialTransform;
        double W = Effective.Width * 100.0, D = Effective.Depth * 100.0, Height = Effective.Height * 100.0;
        return A.FootprintCenter.Equals(T.TransformPosition(FVector(0,0,0)),0.0) &&
            A.AgentSpawnFront.Equals(T.TransformPosition(FVector(D*0.5+Actual.AgentSpawnOffset,0,0)),0.0) &&
            A.AgentSpawnBack.Equals(T.TransformPosition(FVector(-D*0.5-Actual.AgentSpawnOffset,0,0)),0.0) &&
            A.AgentSpawnLeft.Equals(T.TransformPosition(FVector(0,W*0.5+Actual.AgentSpawnOffset,0)),0.0) &&
            A.AgentSpawnRight.Equals(T.TransformPosition(FVector(0,-W*0.5-Actual.AgentSpawnOffset,0)),0.0) &&
            A.PanelTopFront.Equals(T.TransformPosition(FVector(D*0.5,0,Height)),0.0) &&
            A.PanelCenter.Equals(T.TransformPosition(FVector(0,0,Height*0.5)),0.0);
    }
    bool TryGet_RequiredGeometry(FCk_Handle Station, TArray<FCk_Handle>& Handles, TArray<FCk_Handle>& Parents)
    {
        Handles.Empty(); Parents.Empty();
        if (!IsLive(Station)) { return false; }
        TArray<FCk_Handle> Queue; Queue.Add(Station); TArray<FCk_Handle> Seen;
        int32 SceneNodes=0, Components=0;
        for (int32 i=0; i<Queue.Num(); ++i)
        {
            auto H=Queue[i]; if (Seen.Contains(H)) { continue; } Seen.Add(H);
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H,ck::TransientEntity(),Children))
            { Handles.Empty(); Parents.Empty(); return false; }
            for (auto Child:Children)
            {
                if (!IsLive(Child)) { continue; }
                if (utils_scene_node::Has(Child) || utils_unreal_component::Has(Child))
                {
                    if (!utils_entity_lifetime::Has_LifetimeOwner(Child) || utils_entity_lifetime::Get_LifetimeOwner(Child)!=H)
                    { Handles.Empty(); Parents.Empty(); return false; }
                    Handles.Add(Child); Parents.Add(H);
                    if (utils_scene_node::Has(Child)) { ++SceneNodes; }
                    if (utils_unreal_component::Has(Child)) { ++Components; }
                    Queue.Add(Child);
                }
            }
        }
        if (SceneNodes==0 || Components==0) { Handles.Empty(); Parents.Empty(); return false; }
        return true;
    }

}
