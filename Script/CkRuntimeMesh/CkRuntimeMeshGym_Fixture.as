// Language=angelscript

//============================================================================
// RUNTIME MESH GYM - fixtures
//
// One fixture object per station. A fixture owns ONE root entity per generation and everything under it; a reset
// destroys that root whole and composes a fresh one on the same call stack.
//
// Generation guard: every slice a fixture requests carries an operation ID minted from (station, generation,
// sequence), and the fixture remembers the one it is waiting for. A resolution whose ID is not that pending ID - a
// reset happened, or the gym is closing - destroys any result entities it names and changes nothing else. A reset
// also forgets the pending ID, so a cut still in flight when the root goes resolves FailedCancelled and is rejected.
//
// Fixtures are UObjects rather than structs because the slice receiver is a dynamic delegate bound to a UFUNCTION,
// and the focused autotests drive the same fixture without the gym's PlayerController.
//
// Coordinate spaces: slice planes, metrics and copied vertices are in SOURCE-MESH-LOCAL cm (the checker cube spans
// 0..10 cm). Results keep the source's local frame, so a display placed at the source's pose reproduces the cut in
// place. Displays follow their entity Transform, scale included - every station shows the 10 cm cube at a uniform
// display scale so it is inspectable. Nothing here simulates physics.
//============================================================================

namespace CkRuntimeMeshGym
{
    // 10 cm cube, corners (0,0,0)..(10,10,10) mesh-local, CPU access, slot 0 blue checker and slot 1 red checker
    // (authored by Test_RuntimeMesh_RenderAuthoring.cpp).
    FSoftObjectPath Get_CheckerMeshPath()
    {
        return FSoftObjectPath("/CkTests/CkRuntimeMesh/Render/SM_Checker_CPU.SM_Checker_CPU");
    }

    FVector Get_CheckerCenterCm()
    {
        return FVector(5.0, 5.0, 5.0);
    }

    // Cap triangles get material ID 2: the green checker M_Checker_Cap in the third display slot.
    const int32 CapMaterialID = 2;

    FCk_RuntimeMeshDisplay_Spec Make_DisplaySpec(FCk_Handle_RuntimeMesh InGeometry)
    {
        TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
        Materials.Add(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
            "/CkTests/CkRuntimeMesh/Render/M_Checker_Blue.M_Checker_Blue")));
        Materials.Add(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
            "/CkTests/CkRuntimeMesh/Render/M_Checker_Red.M_Checker_Red")));
        Materials.Add(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
            "/CkTests/CkRuntimeMesh/Render/M_Checker_Cap.M_Checker_Cap")));

        auto Visuals = FCk_RuntimeMeshDisplay_Visuals();
        Visuals.Set_Materials(Materials);
        Visuals.Set_CastShadow(ECk_EnableDisable::Enable);

        auto Spec = FCk_RuntimeMeshDisplay_Spec();
        Spec.Set_Geometry(InGeometry);
        Spec.Set_Visuals(Visuals);
        return Spec;
    }

    // 10 cm per UV unit matches the cube faces (one face = one UV unit), so cap and face checkers share a density.
    FCk_RuntimeMesh_Cap Make_Cap()
    {
        auto Cap = FCk_RuntimeMesh_Cap();
        Cap.Set_MaterialID(CapMaterialID);
        Cap.Set_CmPerUVUnit(10.0);
        return Cap;
    }

    FCk_RuntimeMesh_PlaneLocal Make_Plane(FVector InPositionCm, FVector InNormal, FVector InTangentHint)
    {
        const auto Normal = InNormal.GetSafeNormal();
        auto Tangent = InTangentHint - Normal * InTangentHint.DotProduct(Normal);
        if (Tangent.SizeSquared() < 0.0001)
        {
            const auto Fallback = Math::Abs(Normal.Z) < 0.9 ? FVector(0.0, 0.0, 1.0) : FVector(1.0, 0.0, 0.0);
            Tangent = Fallback - Normal * Fallback.DotProduct(Normal);
        }

        auto Plane = FCk_RuntimeMesh_PlaneLocal();
        Plane.Set_PositionCm(InPositionCm);
        Plane.Set_Normal(Normal);
        Plane.Set_Tangent(Tangent.GetSafeNormal());
        return Plane;
    }

    // The tuned plane of the Plane station: the +X normal rotated by yaw then pitch, offset along itself from the
    // cube centre.
    FCk_RuntimeMesh_PlaneLocal Make_TunedPlane(float InOffsetCm, float InYawDeg, float InPitchDeg)
    {
        const auto Rotation = FRotator(InPitchDeg, InYawDeg, 0.0);
        const auto Normal = Rotation.Vector();
        const auto Tangent = Rotation.RotateVector(FVector(0.0, 1.0, 0.0));
        return Make_Plane(Get_CheckerCenterCm() + Normal * InOffsetCm, Normal, Tangent);
    }

    // A display pose that puts the cube's centre at InWorldCenter.
    FTransform Make_CenteredPose(FVector InWorldCenter, FRotator InRotation, float InScale)
    {
        const auto LocalCenter = Get_CheckerCenterCm() * InScale;
        return FTransform(InRotation, InWorldCenter - InRotation.RotateVector(LocalCenter),
            FVector(InScale, InScale, InScale));
    }

    FTransform Make_TranslatedPose(FTransform InPose, FVector InWorldOffset)
    {
        return FTransform(InPose.Rotator(), InPose.GetLocation() + InWorldOffset, InPose.GetScale3D());
    }

    // The production conservation bound, read from the default cut limits so it cannot drift from them:
    // max(absolute, relative x parent volume).
    float Get_AllowedVolumeError(float InParentVolumeCm3)
    {
        auto Limits = FCk_RuntimeMesh_CutLimits();
        return Math::Max(Limits.Get_AbsoluteVolumeToleranceCm3(), Limits.Get_RelativeVolumeTolerance() * InParentVolumeCm3);
    }

    void Request_DestroyResults(FCk_RuntimeMesh_SliceResult InResult)
    {
        auto Positive = InResult.Get_Positive();
        if (ck::IsValid(Positive))
        {
            utils_entity_lifetime::Request_DestroyEntity(Positive);
        }
        auto Negative = InResult.Get_Negative();
        if (ck::IsValid(Negative))
        {
            utils_entity_lifetime::Request_DestroyEntity(Negative);
        }
    }

    FString Get_DisplayStateText(FCk_Handle_RuntimeMeshDisplay InDisplay)
    {
        if (ck::Is_NOT_Valid(InDisplay))
        {
            return "none";
        }
        const auto State = utils_runtime_mesh_display::Get_SetupState(InDisplay);
        if (State == ECk_RuntimeMesh_SetupState::Failed)
        {
            const auto Failure = utils_runtime_mesh_display::Get_SetupFailure(InDisplay);
            return f"Failed ({Failure :n})";
        }
        return f"{State :n}";
    }

    FCkRuntimeMeshGym_WorldBox Make_WorldBox(FCkRuntimeMeshGym_Piece InPiece)
    {
        auto Box = FCkRuntimeMeshGym_WorldBox();
        if (ck::Is_NOT_Valid(InPiece.Mesh))
        {
            return Box;
        }
        const auto Metrics = InPiece.Metrics;
        const auto LocalCenter = (Metrics.Get_BoundsMinCm() + Metrics.Get_BoundsMaxCm()) * 0.5;
        const auto LocalExtent = (Metrics.Get_BoundsMaxCm() - Metrics.Get_BoundsMinCm()) * 0.5;
        Box.Center = InPiece.DisplayPose.TransformPosition(LocalCenter);
        Box.Extent = LocalExtent * InPiece.DisplayPose.GetScale3D().X;
        Box.Rotation = InPiece.DisplayPose.Rotator();
        Box.Valid = true;
        return Box;
    }
}

// One displayed geometry: the RuntimeMesh it shows, the entity carrying its Transform + display, and the metrics read
// when it was composed.
struct FCkRuntimeMeshGym_Piece
{
    FCk_Handle_RuntimeMesh Mesh;
    FCk_Handle_Transform DisplayOwner;
    FCk_Handle_RuntimeMeshDisplay Display;
    FCk_RuntimeMesh_Metrics Metrics;
    FTransform DisplayPose;
    FString Label;
    bool IsSource = false;
}

struct FCkRuntimeMeshGym_WorldBox
{
    FVector Center = FVector::ZeroVector;
    FVector Extent = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    bool Valid = false;
}

enum ECkRuntimeMeshGym_ValidationCase
{
    NoIntersection,
    TouchingOnly,
    RejectedTooSmall
}

//============================================================================
// Base: root ownership, generation guard, source import, slice requests
//============================================================================

class UCkRuntimeMeshGym_FixtureBase : UObject
{
    // ---- configuration (set once by Create) ----
    FCk_Handle Owner;
    FTransform Pose;
    int32 StationIndex = 0;

    // ---- runtime state ----
    FCk_Handle_Transform Root;
    FCk_Handle_RuntimeMesh Source;
    int32 Generation = 0;
    int32 Sequence = 0;
    bool Closed = false;

    bool HasPendingSlice = false;
    FGuid PendingOperation;

    bool HasLastOutcome = false;
    ECk_RuntimeMesh_SliceOutcome LastOutcome = ECk_RuntimeMesh_SliceOutcome::Succeeded;

    // Resolutions rejected by the generation guard, across generations.
    int32 StaleResolutions = 0;
    bool HasLastStaleOutcome = false;
    ECk_RuntimeMesh_SliceOutcome LastStaleOutcome = ECk_RuntimeMesh_SliceOutcome::Succeeded;

    private FCk_Handle_Timer _Tick;

    // InOwner owns every generation's root (the gym PlayerController's entity, or an autotest's entity). InPose is the
    // display pose of the station's specimen, scale included.
    bool Create(FCk_Handle InOwner, FTransform InPose, int32 InStationIndex)
    {
        Owner = InOwner;
        Pose = InPose;
        StationIndex = InStationIndex;
        Closed = false;
        return DoBuildGeneration();
    }

    // Destroys this generation's root whole and composes a fresh one on the same call stack. Returns false, leaving the
    // fixture Closed, when there is no live owner to build under; a Closed fixture is never reopened - its owner makes
    // a new one (the gym PlayerController does on R / X / restart).
    bool Request_Reset()
    {
        if (Closed)
        {
            return false;
        }
        DoEndGeneration();
        return DoBuildGeneration();
    }

    // Final teardown (gym EndPlay). Anything resolving afterwards is rejected by the guard and creates nothing.
    void Request_Close()
    {
        if (Closed)
        {
            return;
        }
        DoEndGeneration();
        Closed = true;
    }

    bool Get_IsSourceReady()
    {
        return ck::IsValid(Source)
            && utils_runtime_mesh::Get_SetupState(Source) == ECk_RuntimeMesh_SetupState::Ready;
    }

    FString Get_SourceStateText()
    {
        if (ck::Is_NOT_Valid(Source))
        {
            return "none";
        }
        const auto State = utils_runtime_mesh::Get_SetupState(Source);
        if (State == ECk_RuntimeMesh_SetupState::Failed)
        {
            const auto Failure = utils_runtime_mesh::Get_SetupFailure(Source);
            return f"Failed ({Failure :n})";
        }
        return f"{State :n}";
    }

    // ---- station hooks ----

    protected void DoBuild()
    {
    }

    // Forget this generation's handles; the root destroy takes the entities.
    protected void DoForget()
    {
    }

    protected void DoTick()
    {
    }

    // A resolution of THIS generation's pending slice. Results the station does not adopt must be destroyed.
    protected void DoOnSliceResolved(FCk_RuntimeMesh_SliceResult InResult)
    {
        CkRuntimeMeshGym::Request_DestroyResults(InResult);
    }

    // ---- helpers for stations ----

    protected FCk_Handle_RuntimeMesh DoImport(FName InDebugName)
    {
        auto Entity = utils_entity_lifetime::Request_CreateEntity(FCk_Handle(Root));
        Entity.Set_DebugName(InDebugName);
        auto Spec = FCk_RuntimeMesh_Spec();
        Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>(CkRuntimeMeshGym::Get_CheckerMeshPath()));
        return utils_runtime_mesh::Add(Entity, Spec);
    }

    // Adds a Transform at InPose and a display of the Ready InGeometry to InEntity.
    protected FCkRuntimeMeshGym_Piece DoCompose_Piece(FCk_Handle InEntity, FCk_Handle_RuntimeMesh InGeometry,
        FTransform InPose, FString InLabel)
    {
        auto Piece = FCkRuntimeMeshGym_Piece();
        Piece.Mesh = InGeometry;
        Piece.Metrics = utils_runtime_mesh::Get_Metrics(InGeometry);
        Piece.DisplayPose = InPose;
        Piece.Label = InLabel;
        Piece.DisplayOwner = utils_transform::Add(InEntity, InPose, ECk_Replication::DoesNotReplicate);
        Piece.Display = utils_runtime_mesh_display::Add(Piece.DisplayOwner, CkRuntimeMeshGym::Make_DisplaySpec(InGeometry));
        return Piece;
    }

    // One slice in flight per station. Results are owned by the root, never by the target, so they outlive it.
    protected bool DoRequest_Slice(FCk_Handle_RuntimeMesh InTarget, FCk_RuntimeMesh_PlaneLocal InPlane,
        FCk_RuntimeMesh_CutLimits InLimits)
    {
        if (Closed || HasPendingSlice || ck::Is_NOT_Valid(Root) || ck::Is_NOT_Valid(InTarget))
        {
            return false;
        }
        if (utils_runtime_mesh::Get_SetupState(InTarget) != ECk_RuntimeMesh_SetupState::Ready)
        {
            return false;
        }

        Sequence++;
        auto Request = FCk_Request_RuntimeMesh_Slice();
        Request.Set_OperationID(FGuid(uint32(7100 + StationIndex), uint32(Generation + 1), uint32(Sequence), uint32(4243)));
        Request.Set_ResultOwner(FCk_Handle(Root));
        Request.Set_Plane(InPlane);
        Request.Set_Cap(CkRuntimeMeshGym::Make_Cap());
        Request.Set_Limits(InLimits);

        // Armed before the call: a rejection resolves on this call stack.
        HasPendingSlice = true;
        PendingOperation = Request.Get_OperationID();

        auto Target = InTarget;
        utils_runtime_mesh::Request_Slice(Target, Request,
            FCk_Delegate_RuntimeMesh_OnSliceResolved(this, n"OnSliceResolved"));
        return true;
    }

    UFUNCTION()
    private void OnSliceResolved(FCk_RuntimeMesh_SliceResult InResult)
    {
        const auto IsCurrent = Closed == false && HasPendingSlice && InResult.Get_OperationID() == PendingOperation;
        if (IsCurrent == false)
        {
            StaleResolutions++;
            HasLastStaleOutcome = true;
            LastStaleOutcome = InResult.Get_Outcome();
            CkRuntimeMeshGym::Request_DestroyResults(InResult);
            return;
        }

        HasPendingSlice = false;
        HasLastOutcome = true;
        LastOutcome = InResult.Get_Outcome();
        DoOnSliceResolved(InResult);
    }

    UFUNCTION()
    private void OnFixtureTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        // A destroyed generation's timer may still fire once; only the live one drives the station.
        if (Closed || InTimer != _Tick)
        {
            return;
        }
        DoTick();
    }

    private bool DoBuildGeneration()
    {
        if (ck::Is_NOT_Valid(Owner)
            || utils_entity_lifetime::Get_IsPendingDestroy(Owner, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
        {
            Closed = true;
            return false;
        }

        Root = utils_transform::Create(Owner, FTransform(Pose.GetLocation()), ECk_Replication::DoesNotReplicate);
        _Tick = utils_timer::Create_Tick(FCk_Handle(Root), FCk_Delegate_Timer(this, n"OnFixtureTick"));
        DoBuild();
        return true;
    }

    private void DoEndGeneration()
    {
        Generation++;
        HasPendingSlice = false;
        HasLastOutcome = false;
        DoForget();
        if (ck::IsValid(Root))
        {
            utils_entity_lifetime::Request_DestroyEntity(Root);
        }
        Root = FCk_Handle_Transform();
        Source = FCk_Handle_RuntimeMesh();
        _Tick = FCk_Handle_Timer();
    }
}

//============================================================================
// Plane station: tune a plane against an off-origin, rotated source; cut the immutable source; each successful cut
// replaces the previous result pair, shown beside the source at the same rotation and scale.
//============================================================================

class UCkRuntimeMeshGym_PlaneFixture : UCkRuntimeMeshGym_FixtureBase
{
    // ---- configuration ----
    // World cm between the source and each result, along world Y. Display placement only.
    float ResultSpacingCm = 380.0;
    // Guide quad edge, in source-local cm (the cube is 10).
    float GuideSizeCm = 16.0;

    // ---- runtime state ----
    FCk_RuntimeMesh_PlaneLocal Plane;
    // The plane of the cut in flight, then - once it commits - the plane the displayed pair was cut with. The guide
    // follows the tuners, so the two disagree whenever the plane was dialled after the last cut.
    FCk_RuntimeMesh_PlaneLocal LastCutPlane;
    FCk_RuntimeMesh_PlaneLocal PairPlane;
    FCkRuntimeMeshGym_Piece SourcePiece;
    FCkRuntimeMeshGym_Piece Positive;
    FCkRuntimeMeshGym_Piece Negative;
    bool HasPair = false;
    int32 PositiveCapTriangles = 0;
    int32 NegativeCapTriangles = 0;
    FCk_Handle_Transform Guide;

    private bool _HasPlane = false;
    private bool _SourceComposed = false;

    void Set_Plane(FCk_RuntimeMesh_PlaneLocal InPlane)
    {
        Plane = InPlane;
        _HasPlane = true;
        if (ck::IsValid(Guide))
        {
            utils_transform::Request_SetTransform(Guide,
                FCk_Request_Transform_SetTransform(Get_GuideWorldTransform()));
        }
    }

    bool Get_CanCut()
    {
        return Closed == false && HasPendingSlice == false && Get_IsSourceReady();
    }

    // Cuts the SOURCE with the current plane. The previous pair is destroyed only once a new pair exists.
    bool Request_Cut()
    {
        if (Get_CanCut() == false)
        {
            return false;
        }
        LastCutPlane = Plane;
        return DoRequest_Slice(Source, Plane, FCk_RuntimeMesh_CutLimits());
    }

    // 0 = source, 1 = positive, 2 = negative.
    FCkRuntimeMeshGym_Piece Get_Piece(int32 InIndex)
    {
        if (InIndex == 1)
        {
            return Positive;
        }
        if (InIndex == 2)
        {
            return Negative;
        }
        return SourcePiece;
    }

    // The guide quad's pose, derived from the same source-local plane the cut uses.
    FTransform Get_GuideWorldTransform()
    {
        const auto WorldNormal = Pose.TransformVectorNoScale(Plane.Get_Normal()).GetSafeNormal();
        const auto WorldTangent = Pose.TransformVectorNoScale(Plane.Get_Tangent()).GetSafeNormal();
        const auto Size = GuideSizeCm * Pose.GetScale3D().X / 100.0;
        return FTransform(FRotator::MakeFromZX(WorldNormal, WorldTangent),
            Pose.TransformPosition(Plane.Get_PositionCm()), FVector(Size, Size, 0.002));
    }

    bool Get_PairMatchesGuide()
    {
        return HasPair
            && PairPlane.Get_PositionCm().Equals(Plane.Get_PositionCm(), 0.0001)
            && PairPlane.Get_Normal().Equals(Plane.Get_Normal(), 0.0001);
    }

    FVector Get_PlaneWorldCenter()
    {
        return Pose.TransformPosition(Plane.Get_PositionCm());
    }

    FVector Get_PlaneWorldNormal()
    {
        return Pose.TransformVectorNoScale(Plane.Get_Normal()).GetSafeNormal();
    }

    protected void DoBuild() override
    {
        if (_HasPlane == false)
        {
            Plane = CkRuntimeMeshGym::Make_TunedPlane(0.0, 0.0, 0.0);
            _HasPlane = true;
        }
        Source = DoImport(n"RuntimeMeshGym.Plane.Source");

        Guide = utils_transform::Create(Root, Get_GuideWorldTransform(), ECk_Replication::DoesNotReplicate);
        const auto Params = utils_unreal_component::Make_Params(UStaticMeshComponent,
            ECk_UnrealComponent_TickPolicy::DoNotTick, n"RuntimeMeshGym_PlaneGuide");
        auto Component = utils_unreal_component::Add(FCk_Handle(Guide), Params);
        utils_unreal_component::BindTo_OnAdded(Component,
            FCk_Delegate_UnrealComponent_OnAdded(this, n"OnGuideAdded"));
    }

    protected void DoForget() override
    {
        SourcePiece = FCkRuntimeMeshGym_Piece();
        Positive = FCkRuntimeMeshGym_Piece();
        Negative = FCkRuntimeMeshGym_Piece();
        HasPair = false;
        PositiveCapTriangles = 0;
        NegativeCapTriangles = 0;
        Guide = FCk_Handle_Transform();
        _SourceComposed = false;
    }

    protected void DoTick() override
    {
        if (_SourceComposed == false && Get_IsSourceReady())
        {
            _SourceComposed = true;
            SourcePiece = DoCompose_Piece(FCk_Handle(Source), Source, Pose, "Source");
            SourcePiece.IsSource = true;
        }
    }

    protected void DoOnSliceResolved(FCk_RuntimeMesh_SliceResult InResult) override
    {
        if (InResult.Get_Outcome() != ECk_RuntimeMesh_SliceOutcome::Succeeded)
        {
            CkRuntimeMeshGym::Request_DestroyResults(InResult);
            return;
        }

        DoDestroy_Pair();
        Positive = DoCompose_Piece(FCk_Handle(InResult.Get_Positive()), InResult.Get_Positive(),
            CkRuntimeMeshGym::Make_TranslatedPose(Pose, FVector(0.0, ResultSpacingCm, 0.0)), "Positive");
        Negative = DoCompose_Piece(FCk_Handle(InResult.Get_Negative()), InResult.Get_Negative(),
            CkRuntimeMeshGym::Make_TranslatedPose(Pose, FVector(0.0, -ResultSpacingCm, 0.0)), "Negative");
        PositiveCapTriangles = InResult.Get_PositiveCapTriangles();
        NegativeCapTriangles = InResult.Get_NegativeCapTriangles();
        // One cut in flight at a time, so the committed pair was cut with the plane recorded at its request.
        PairPlane = LastCutPlane;
        HasPair = true;
    }

    private void DoDestroy_Pair()
    {
        if (ck::IsValid(Positive.Mesh))
        {
            utils_entity_lifetime::Request_DestroyEntity(Positive.Mesh);
        }
        if (ck::IsValid(Negative.Mesh))
        {
            utils_entity_lifetime::Request_DestroyEntity(Negative.Mesh);
        }
        Positive = FCkRuntimeMeshGym_Piece();
        Negative = FCkRuntimeMeshGym_Piece();
        HasPair = false;
    }

    UFUNCTION()
    private void OnGuideAdded(FCk_Handle_UnrealComponent InHandle)
    {
        auto Mesh = Cast<UStaticMeshComponent>(utils_unreal_component::Get_Component(InHandle));
        if (ck::Is_NOT_Valid(Mesh))
        {
            return;
        }
        // A flattened cube rather than the one-sided engine plane, so the guide reads from either side.
        Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh.SetCastShadow(false);
        Mesh.SetStaticMesh(Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Cube.Cube")));
        auto Material = Cast<UMaterialInterface>(LoadObject(this,
            "/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent"));
        if (ck::IsValid(Material))
        {
            auto Tinted = Mesh.CreateDynamicMaterialInstance(0, Material);
            if (ck::IsValid(Tinted))
            {
                Tinted.SetVectorParameterValue(n"Color", FLinearColor(0.1, 0.8, 1.0, 0.3));
            }
        }
    }
}

//============================================================================
// Surface station: the source beside one cut pair, exploded along the plane normal by a DISPLAY offset; distinct cap
// material, checker UVs, a receiver plane for cast shadows (the lights belong to the gym PlayerController).
//============================================================================

class UCkRuntimeMeshGym_SurfaceFixture : UCkRuntimeMeshGym_FixtureBase
{
    // ---- configuration ----
    // The source sits this far to world -Y of the pair. Display placement only.
    float SourceSpacingCm = 420.0;
    // The receiver plane sits this far below the pair's centre, edge length in world cm.
    float ReceiverDropCm = 230.0;
    float ReceiverSizeCm = 1600.0;

    // ---- tuner mirror (the PlayerController owns the value; the fixture applies it) ----
    float ExplodeCm = 1.5;

    // ---- runtime state ----
    FCk_RuntimeMesh_PlaneLocal Plane;
    FCkRuntimeMeshGym_Piece SourcePiece;
    FCkRuntimeMeshGym_Piece Positive;
    FCkRuntimeMeshGym_Piece Negative;
    bool HasPair = false;
    int32 PositiveCapTriangles = 0;
    int32 NegativeCapTriangles = 0;
    FCk_Handle_Transform Receiver;

    private bool _SourceComposed = false;
    private bool _AutoCutIssued = false;

    bool Get_CanCut()
    {
        return Closed == false && HasPendingSlice == false && Get_IsSourceReady();
    }

    // Re-cuts the source with the fixed oblique plane; the new pair replaces the old one.
    bool Request_Recut()
    {
        if (Get_CanCut() == false)
        {
            return false;
        }
        return DoRequest_Slice(Source, Plane, FCk_RuntimeMesh_CutLimits());
    }

    // Moves the pair's display entities only. The geometry is untouched and nothing simulates.
    void Set_ExplodeCm(float InExplodeCm)
    {
        ExplodeCm = Math::Clamp(InExplodeCm, 0.0, 8.0);
        if (HasPair == false)
        {
            return;
        }
        Positive.DisplayPose = Get_HalfPose(1.0);
        Negative.DisplayPose = Get_HalfPose(-1.0);
        if (ck::IsValid(Positive.DisplayOwner))
        {
            utils_transform::Request_SetLocation(Positive.DisplayOwner,
                FCk_Request_Transform_SetLocation(Positive.DisplayPose.GetLocation()));
        }
        if (ck::IsValid(Negative.DisplayOwner))
        {
            utils_transform::Request_SetLocation(Negative.DisplayOwner,
                FCk_Request_Transform_SetLocation(Negative.DisplayPose.GetLocation()));
        }
    }

    // 0 = source, 1 = positive, 2 = negative.
    FCkRuntimeMeshGym_Piece Get_Piece(int32 InIndex)
    {
        if (InIndex == 1)
        {
            return Positive;
        }
        if (InIndex == 2)
        {
            return Negative;
        }
        return SourcePiece;
    }

    FVector Get_PairWorldCenter()
    {
        return Pose.TransformPosition(CkRuntimeMeshGym::Get_CheckerCenterCm());
    }

    FTransform Get_HalfPose(float InSign)
    {
        const auto WorldNormal = Pose.TransformVectorNoScale(Plane.Get_Normal()).GetSafeNormal();
        return CkRuntimeMeshGym::Make_TranslatedPose(Pose, WorldNormal * (InSign * ExplodeCm * Pose.GetScale3D().X));
    }

    protected void DoBuild() override
    {
        Plane = CkRuntimeMeshGym::Make_Plane(CkRuntimeMeshGym::Get_CheckerCenterCm(),
            FVector(1.0, 0.55, 0.8), FVector(0.0, 1.0, 0.0));
        Source = DoImport(n"RuntimeMeshGym.Surface.Source");

        const auto ReceiverScale = ReceiverSizeCm / 100.0;
        Receiver = utils_transform::Create(Root,
            FTransform(FRotator::ZeroRotator, Get_PairWorldCenter() - FVector(0.0, 0.0, ReceiverDropCm),
                FVector(ReceiverScale, ReceiverScale, 1.0)),
            ECk_Replication::DoesNotReplicate);
        const auto Params = utils_unreal_component::Make_Params(UStaticMeshComponent,
            ECk_UnrealComponent_TickPolicy::DoNotTick, n"RuntimeMeshGym_ShadowReceiver");
        auto Component = utils_unreal_component::Add(FCk_Handle(Receiver), Params);
        utils_unreal_component::BindTo_OnAdded(Component,
            FCk_Delegate_UnrealComponent_OnAdded(this, n"OnReceiverAdded"));
    }

    protected void DoForget() override
    {
        SourcePiece = FCkRuntimeMeshGym_Piece();
        Positive = FCkRuntimeMeshGym_Piece();
        Negative = FCkRuntimeMeshGym_Piece();
        HasPair = false;
        PositiveCapTriangles = 0;
        NegativeCapTriangles = 0;
        Receiver = FCk_Handle_Transform();
        _SourceComposed = false;
        _AutoCutIssued = false;
    }

    protected void DoTick() override
    {
        if (_SourceComposed == false && Get_IsSourceReady())
        {
            _SourceComposed = true;
            SourcePiece = DoCompose_Piece(FCk_Handle(Source), Source,
                CkRuntimeMeshGym::Make_TranslatedPose(Pose, FVector(0.0, -SourceSpacingCm, 0.0)), "Source");
            SourcePiece.IsSource = true;
        }

        // The station opens on a pair: one automatic cut per generation, the same request the T key issues.
        if (_AutoCutIssued == false && Get_CanCut())
        {
            _AutoCutIssued = true;
            Request_Recut();
        }
    }

    protected void DoOnSliceResolved(FCk_RuntimeMesh_SliceResult InResult) override
    {
        if (InResult.Get_Outcome() != ECk_RuntimeMesh_SliceOutcome::Succeeded)
        {
            CkRuntimeMeshGym::Request_DestroyResults(InResult);
            return;
        }

        if (ck::IsValid(Positive.Mesh))
        {
            utils_entity_lifetime::Request_DestroyEntity(Positive.Mesh);
        }
        if (ck::IsValid(Negative.Mesh))
        {
            utils_entity_lifetime::Request_DestroyEntity(Negative.Mesh);
        }
        Positive = DoCompose_Piece(FCk_Handle(InResult.Get_Positive()), InResult.Get_Positive(), Get_HalfPose(1.0), "Positive");
        Negative = DoCompose_Piece(FCk_Handle(InResult.Get_Negative()), InResult.Get_Negative(), Get_HalfPose(-1.0), "Negative");
        PositiveCapTriangles = InResult.Get_PositiveCapTriangles();
        NegativeCapTriangles = InResult.Get_NegativeCapTriangles();
        HasPair = true;
    }

    UFUNCTION()
    private void OnReceiverAdded(FCk_Handle_UnrealComponent InHandle)
    {
        auto Mesh = Cast<UStaticMeshComponent>(utils_unreal_component::Get_Component(InHandle));
        if (ck::Is_NOT_Valid(Mesh))
        {
            return;
        }
        Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh.SetCastShadow(false);
        Mesh.SetStaticMesh(Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Plane.Plane")));
    }
}

//============================================================================
// Validation station: repeat cuts of a selected leaf up to a piece bound, volume conservation against the production
// tolerance, and the contract's deterministic terminal outcomes on the source.
//============================================================================

class UCkRuntimeMeshGym_ValidationFixture : UCkRuntimeMeshGym_FixtureBase
{
    // ---- configuration ----
    int32 MaxLeaves = 16;
    // Each leaf is displayed offset from the cube centre by this multiple of its centroid offset. Display only.
    float ExplodeFactor = 0.9;

    // ---- runtime state ----
    // The source stays alive as geometry for the outcome cases even after it is cut; while it is a leaf it is shown
    // through a separate display entity so that display can go without the geometry.
    TArray<FCkRuntimeMeshGym_Piece> Leaves;
    float SourceVolumeCm3 = 0.0;
    int32 CutCount = 0;
    float AccumulatedAllowedCm3 = 0.0;

    bool HasLastCut = false;
    float LastParentVolumeCm3 = 0.0;
    float LastPositiveVolumeCm3 = 0.0;
    float LastNegativeVolumeCm3 = 0.0;

    bool HasLastCase = false;
    ECkRuntimeMeshGym_ValidationCase LastCase = ECkRuntimeMeshGym_ValidationCase::NoIntersection;
    ECk_RuntimeMesh_SliceOutcome LastCaseOutcome = ECk_RuntimeMesh_SliceOutcome::Succeeded;

    private bool _SourceComposed = false;
    private bool _PendingIsCase = false;
    private ECkRuntimeMeshGym_ValidationCase _PendingCase = ECkRuntimeMeshGym_ValidationCase::NoIntersection;
    private FCk_Handle_RuntimeMesh _PendingLeafMesh;

    bool Get_CanCutLeaf(int32 InLeafIndex)
    {
        return Closed == false && HasPendingSlice == false && Leaves.IsValidIndex(InLeafIndex)
            && Leaves.Num() < MaxLeaves;
    }

    bool Get_CanRunCase()
    {
        return Closed == false && HasPendingSlice == false && Get_IsSourceReady();
    }

    // InAxis: 0 = X, 1 = Y, 2 = Z, 3 = diagonal; the plane passes through the leaf's centroid (source-local cm).
    bool Request_CutLeaf(int32 InLeafIndex, int32 InAxis)
    {
        if (Get_CanCutLeaf(InLeafIndex) == false)
        {
            return false;
        }
        const auto Leaf = Leaves[InLeafIndex];
        const auto Normal = Get_AxisNormal(InAxis);
        const auto TangentHint = InAxis == 2 ? FVector(1.0, 0.0, 0.0) : FVector(0.0, 0.0, 1.0);
        const auto Plane = CkRuntimeMeshGym::Make_Plane(Leaf.Metrics.Get_CentroidCm(), Normal, TangentHint);
        _PendingIsCase = false;
        _PendingLeafMesh = Leaf.Mesh;
        return DoRequest_Slice(Leaf.Mesh, Plane, FCk_RuntimeMesh_CutLimits());
    }

    // The contract's deterministic outcomes on the 0..10 cm cube with a +X normal (Test_RuntimeMesh_Slice.cpp,
    // MissTouchAndInvalid). Always cuts the SOURCE; nothing is ever adopted from these.
    bool Request_Case(ECkRuntimeMeshGym_ValidationCase InCase)
    {
        if (Get_CanRunCase() == false)
        {
            return false;
        }
        auto Limits = FCk_RuntimeMesh_CutLimits();
        auto PlaneX = 20.0;
        if (InCase == ECkRuntimeMeshGym_ValidationCase::TouchingOnly)
        {
            PlaneX = 10.0;
        }
        else if (InCase == ECkRuntimeMeshGym_ValidationCase::RejectedTooSmall)
        {
            PlaneX = 0.001;
            Limits.Set_MinimumNormalExtentCm(0.01);
        }
        const auto Plane = CkRuntimeMeshGym::Make_Plane(FVector(PlaneX, 0.0, 0.0),
            FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0));
        _PendingIsCase = true;
        _PendingCase = InCase;
        return DoRequest_Slice(Source, Plane, Limits);
    }

    ECk_RuntimeMesh_SliceOutcome Get_ExpectedOutcome(ECkRuntimeMeshGym_ValidationCase InCase)
    {
        if (InCase == ECkRuntimeMeshGym_ValidationCase::TouchingOnly)
        {
            return ECk_RuntimeMesh_SliceOutcome::TouchingOnly;
        }
        if (InCase == ECkRuntimeMeshGym_ValidationCase::RejectedTooSmall)
        {
            return ECk_RuntimeMesh_SliceOutcome::RejectedTooSmall;
        }
        return ECk_RuntimeMesh_SliceOutcome::NoIntersection;
    }

    float Get_LeafVolumeSumCm3()
    {
        auto Sum = 0.0;
        for (auto Leaf : Leaves)
        {
            Sum += Leaf.Metrics.Get_VolumeCm3();
        }
        return Sum;
    }

    // Every leaf display Ready, counted against the leaves.
    int32 Get_ReadyDisplayCount()
    {
        auto Ready = 0;
        for (auto Leaf : Leaves)
        {
            if (ck::IsValid(Leaf.Display)
                && utils_runtime_mesh_display::Get_SetupState(Leaf.Display) == ECk_RuntimeMesh_SetupState::Ready)
            {
                Ready++;
            }
        }
        return Ready;
    }

    FVector Get_AxisNormal(int32 InAxis)
    {
        if (InAxis == 1)
        {
            return FVector(0.0, 1.0, 0.0);
        }
        if (InAxis == 2)
        {
            return FVector(0.0, 0.0, 1.0);
        }
        if (InAxis == 3)
        {
            return FVector(1.0, 1.0, 1.0).GetSafeNormal();
        }
        return FVector(1.0, 0.0, 0.0);
    }

    protected void DoBuild() override
    {
        Source = DoImport(n"RuntimeMeshGym.Validation.Source");
    }

    protected void DoForget() override
    {
        Leaves.Reset();
        SourceVolumeCm3 = 0.0;
        CutCount = 0;
        AccumulatedAllowedCm3 = 0.0;
        HasLastCut = false;
        HasLastCase = false;
        _SourceComposed = false;
        _PendingIsCase = false;
        _PendingLeafMesh = FCk_Handle_RuntimeMesh();
    }

    protected void DoTick() override
    {
        if (_SourceComposed == false && Get_IsSourceReady())
        {
            _SourceComposed = true;
            SourceVolumeCm3 = utils_runtime_mesh::Get_Metrics(Source).Get_VolumeCm3();
            auto DisplayEntity = utils_entity_lifetime::Request_CreateEntity(FCk_Handle(Root));
            DisplayEntity.Set_DebugName(n"RuntimeMeshGym.Validation.SourceDisplay");
            auto Leaf = DoCompose_Piece(DisplayEntity, Source, Pose, "S");
            Leaf.IsSource = true;
            Leaves.Add(Leaf);
        }
    }

    protected void DoOnSliceResolved(FCk_RuntimeMesh_SliceResult InResult) override
    {
        if (_PendingIsCase)
        {
            HasLastCase = true;
            LastCase = _PendingCase;
            LastCaseOutcome = InResult.Get_Outcome();
            CkRuntimeMeshGym::Request_DestroyResults(InResult);
            return;
        }

        auto LeafIndex = -1;
        for (auto Index = 0; Index < Leaves.Num(); Index++)
        {
            if (Leaves[Index].Mesh == _PendingLeafMesh)
            {
                LeafIndex = Index;
                break;
            }
        }
        if (InResult.Get_Outcome() != ECk_RuntimeMesh_SliceOutcome::Succeeded || LeafIndex < 0)
        {
            CkRuntimeMeshGym::Request_DestroyResults(InResult);
            return;
        }

        const auto Parent = Leaves[LeafIndex];
        Leaves.RemoveAt(LeafIndex);
        if (ck::IsValid(Parent.DisplayOwner))
        {
            utils_entity_lifetime::Request_DestroyEntity(Parent.DisplayOwner);
        }
        if (Parent.IsSource == false && ck::IsValid(Parent.Mesh))
        {
            utils_entity_lifetime::Request_DestroyEntity(Parent.Mesh);
        }

        auto PositiveMesh = InResult.Get_Positive();
        auto NegativeMesh = InResult.Get_Negative();
        Leaves.Add(DoCompose_Piece(FCk_Handle(PositiveMesh), PositiveMesh,
            Get_LeafPose(InResult.Get_PositiveMetrics()), f"{Parent.Label}+"));
        Leaves.Add(DoCompose_Piece(FCk_Handle(NegativeMesh), NegativeMesh,
            Get_LeafPose(InResult.Get_NegativeMetrics()), f"{Parent.Label}-"));

        HasLastCut = true;
        LastParentVolumeCm3 = Parent.Metrics.Get_VolumeCm3();
        LastPositiveVolumeCm3 = InResult.Get_PositiveMetrics().Get_VolumeCm3();
        LastNegativeVolumeCm3 = InResult.Get_NegativeMetrics().Get_VolumeCm3();
        AccumulatedAllowedCm3 += CkRuntimeMeshGym::Get_AllowedVolumeError(LastParentVolumeCm3);
        CutCount++;
    }

    // A leaf is displayed offset from the cube centre along its own centroid offset, so leaves separate visibly.
    private FTransform Get_LeafPose(FCk_RuntimeMesh_Metrics InMetrics)
    {
        const auto LocalOffset = (InMetrics.Get_CentroidCm() - CkRuntimeMeshGym::Get_CheckerCenterCm()) * ExplodeFactor;
        return CkRuntimeMeshGym::Make_TranslatedPose(Pose, Pose.TransformVector(LocalOffset));
    }
}
