// Language=angelscript

// Fixture policy, not production policy: pieces have 1 g/cm3 density, a cut splits the parent mass by the halves'
// volume, and the release kick is mass x speed along +-plane normal (no momentum-transfer model).
const float k_RuntimeConvexGym_DensityKgPerCm3 = 0.001;
const int32 k_RuntimeConvexGym_MaxPieces = 4;
const int32 k_RuntimeConvexGym_CapMaterialID = 2;
const float k_RuntimeConvexGym_HullHalfExtentCm = 50.0;
const float k_RuntimeConvexGym_HullMassKg = 50.0;

enum ECkRuntimeConvexGym_CutInterrupt
{
    None,
    // Request_Reset on the call stack that queued the slice, so the slice resolves against a destroyed source.
    ResetAfterSliceSubmitted,
    // Request_Reset on the slice receiver's call stack, after both halves are composed and promised.
    ResetAfterHalvesComposed,
    // Destroys the negative half's entity after its promise is registered, before its body can be admitted.
    DestroyNegativeHalfBeforeAdmission
}

struct FCkRuntimeConvexGym_CutRequest
{
    int32 PieceIndex = 0;
    float SeparationSpeedCmS = 60.0;
    ECkRuntimeConvexGym_CutInterrupt Interrupt = ECkRuntimeConvexGym_CutInterrupt::None;
}

struct FCkRuntimeConvexGym_Piece
{
    FCk_Handle_Transform Entity;
    FCk_Handle_RuntimeMesh Geometry;
    FCk_Handle_RuntimeMeshDisplay Display;
    FCk_Handle_JoltBody Body;
    float MassKg = 0.0;
}

struct FCkRuntimeConvexGym_HalfResolution
{
    bool Resolved = false;
    ECk_JoltBody_SetupState State = ECk_JoltBody_SetupState::Pending;
    ECk_JoltBody_SetupFailure Failure = ECk_JoltBody_SetupFailure::None;
}

struct FCkRuntimeConvexGym_PendingCut
{
    bool Active = false;
    bool Submitted = false;
    bool Sliced = false;
    FGuid OperationID;
    FCk_Handle_Transform Source;
    FCk_Handle_RuntimeMesh SourceGeometry;
    FCk_Handle_JoltBody SourceBody;
    float SourceMassKg = 0.0;
    FVector WorldNormal = FVector(0.0, 1.0, 0.0);
    FCkRuntimeConvexGym_CutRequest Request;
    FCkRuntimeConvexGym_Piece Positive;
    FCkRuntimeConvexGym_Piece Negative;
    FCkRuntimeConvexGym_HalfResolution PositiveResolution;
    FCkRuntimeConvexGym_HalfResolution NegativeResolution;
}

namespace CkRuntimeConvexGym
{
    FSoftObjectPath Get_CheckerMeshPath()
    {
        return FSoftObjectPath("/CkTests/CkRuntimeMesh/Render/SM_Checker_CPU.SM_Checker_CPU");
    }

    FCk_RuntimeMeshDisplay_Spec Make_CheckerDisplaySpec(FCk_Handle_RuntimeMesh InGeometry)
    {
        TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
        Materials.Add(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath("/CkTests/CkRuntimeMesh/Render/M_Checker_Blue.M_Checker_Blue")));
        Materials.Add(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath("/CkTests/CkRuntimeMesh/Render/M_Checker_Red.M_Checker_Red")));
        Materials.Add(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath("/CkTests/CkRuntimeMesh/Render/M_Checker_Cap.M_Checker_Cap")));
        auto Visuals = FCk_RuntimeMeshDisplay_Visuals();
        Visuals.Set_Materials(Materials);
        Visuals.Set_CastShadow(ECk_EnableDisable::Enable);
        auto Spec = FCk_RuntimeMeshDisplay_Spec();
        Spec.Set_Geometry(InGeometry);
        Spec.Set_Visuals(Visuals);
        return Spec;
    }

    FName Get_PieceProfile()
    {
        return n"PhysicsActor";
    }

    // While a cut is in flight its source is Kinematic on this profile, so the halves admitted at its pose never
    // touch it: the Jolt pair filter is min(A->B, B->A), and Spectator is the only engine profile that ignores
    // PhysicsBody. NoCollision does not: the Jolt signature takes a profile's responses, not CollisionEnabled, and
    // UE fills NoCollision's from the all-Block default container.
    FName Get_IsolatedProfile()
    {
        return n"Spectator";
    }

    FCk_JoltBody_Spec Make_ConvexBodySpec(const TArray<FVector>&in InPointsCm, float InMassKg)
    {
        auto Convex = FCk_JoltBody_RuntimeConvexSpec();
        Convex.Set_PointsCm(InPointsCm);
        auto Spec = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::RuntimeConvex);
        Spec.Set_RuntimeConvex(Convex);
        Spec.Set_MotionType(ECk_MotionType::Dynamic);
        Spec.Set_MassSource(ECk_JoltBody_MassSource::Explicit);
        Spec.Set_MassKg(float32(InMassKg));
        Spec.Set_SurfaceSource(ECk_JoltBody_SurfaceSource::Explicit);
        Spec.Set_Friction(0.6f);
        Spec.Set_Restitution(0.0f);
        Spec.Set_CollisionProfileName(Get_PieceProfile());
        return Spec;
    }

    // The eight corners of the engine's 100 cm cube plus its centre: the hull drops the interior point and must
    // come out as exactly the cube the visible mesh draws.
    TArray<FVector> Make_HullPoints()
    {
        const auto H = k_RuntimeConvexGym_HullHalfExtentCm;
        TArray<FVector> Points;
        Points.Add(FVector(-H, -H, -H));
        Points.Add(FVector( H, -H, -H));
        Points.Add(FVector( H,  H, -H));
        Points.Add(FVector(-H,  H, -H));
        Points.Add(FVector(-H, -H,  H));
        Points.Add(FVector( H, -H,  H));
        Points.Add(FVector( H,  H,  H));
        Points.Add(FVector(-H,  H,  H));
        Points.Add(FVector::ZeroVector);
        return Points;
    }

    TArray<FVector> Make_CoplanarPoints()
    {
        TArray<FVector> Points;
        Points.Add(FVector(-5.0, -5.0, 0.0));
        Points.Add(FVector( 5.0, -5.0, 0.0));
        Points.Add(FVector( 5.0,  5.0, 0.0));
        Points.Add(FVector(-5.0,  5.0, 0.0));
        return Points;
    }

    UStaticMesh Load_EngineCube(UObject InOuter)
    {
        return Cast<UStaticMesh>(LoadObject(InOuter, "/Engine/BasicShapes/Cube.Cube"));
    }

    // Presentation only: the component never collides and is never baked into the Jolt static world.
    FCk_Handle_UnrealComponent Add_VisualCube(FCk_Handle_Transform InEntity, UObject InListener, FName InOnAddedFunction)
    {
        auto Params = utils_unreal_component::Make_Params(UStaticMeshComponent,
            ECk_UnrealComponent_TickPolicy::DoNotTick, n"RuntimeConvexGym_Cube");
        Params.Set_StaticWorldBakePolicy(ECk_UnrealComponent_StaticWorldBakePolicy::DoNotBake);
        auto Component = utils_unreal_component::Add(FCk_Handle(InEntity), Params);
        utils_unreal_component::BindTo_OnAdded(Component, FCk_Delegate_UnrealComponent_OnAdded(InListener, InOnAddedFunction));
        return Component;
    }

    bool Get_IsAlive(FCk_Handle InHandle)
    {
        return ck::IsValid(InHandle)
            && utils_entity_lifetime::Get_IsPendingDestroy(InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy) == false;
    }
}

// One fixture owns one root entity and everything a station shows; Request_Reset destroys the root whole and
// builds a fresh one. Generation counts roots: every async completion is checked against the CURRENT root's
// operation or handles, and a stale one destroys what it produced and changes nothing.
class UCk_RuntimeConvexGym_FixtureBase : UObject
{
    protected FCk_Handle _Owner;
    protected FCk_Handle_Transform _Root;
    protected FVector _Origin = FVector::ZeroVector;
    protected int32 _Generation = 0;

    FCk_Handle_Transform Get_Root() const
    {
        return _Root;
    }

    int32 Get_Generation() const
    {
        return _Generation;
    }

    // The centre of the floor's top face; every station is laid out from it.
    FVector Get_Origin() const
    {
        return _Origin;
    }

    void Request_Teardown()
    {
        DoDestroy_Root();
    }

    protected void DoCreate_Root()
    {
        _Generation++;
        _Root = utils_transform::Create(_Owner, FTransform(_Origin), ECk_Replication::DoesNotReplicate);
    }

    protected void DoDestroy_Root()
    {
        if (ck::IsValid(_Root))
        {
            utils_entity_lifetime::Request_DestroyEntity(_Root);
        }
        _Root = FCk_Handle_Transform();
    }

    // A real Jolt floor (Static ExplicitShape box) whose top face sits at _Origin, plus a matching NoCollision mesh on
    // a separate entity so the body entity keeps unit scale.
    protected void DoAdd_Floor(FVector InHalfExtents)
    {
        const auto Center = _Origin - FVector(0.0, 0.0, InHalfExtents.Z);

        auto FloorBody = utils_transform::Create(_Root, FTransform(Center), ECk_Replication::DoesNotReplicate);
        auto Shape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        Shape.Set_HalfExtents(InHalfExtents);
        auto Spec = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(ECk_MotionType::Static);
        Spec.Set_SurfaceSource(ECk_JoltBody_SurfaceSource::Explicit);
        Spec.Set_Friction(0.8f);
        Spec.Set_Restitution(0.0f);
        utils_jolt_body::Add(FCk_Handle(FloorBody), Spec);

        auto FloorVisual = utils_transform::Create(_Root,
            FTransform(FRotator::ZeroRotator, Center, InHalfExtents * 0.02), ECk_Replication::DoesNotReplicate);
        CkRuntimeConvexGym::Add_VisualCube(FloorVisual, this, n"OnFloorMeshAdded");
    }

    UFUNCTION()
    private void OnFloorMeshAdded(FCk_Handle_UnrealComponent InHandle)
    {
        auto MeshComponent = Cast<UStaticMeshComponent>(utils_unreal_component::Get_Component(InHandle));
        if (ck::Is_NOT_Valid(MeshComponent))
        {
            return;
        }
        MeshComponent.SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MeshComponent.SetStaticMesh(CkRuntimeConvexGym::Load_EngineCube(this));
        MeshComponent.SetMaterial(0, Cast<UMaterialInterface>(LoadObject(this, "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial")));
    }
}

// Hull station: a Dynamic RuntimeConvex body built from a known point set, drawn by an unscaled engine cube on the
// body entity itself, plus the deterministic coplanar rejection.
class UCk_RuntimeConvexGym_HullFixture : UCk_RuntimeConvexGym_FixtureBase
{
    private FCk_Handle_JoltBody _Hull;
    private FCk_Handle_JoltBody _Rejected;

    void Request_Build(FCk_Handle InOwner, FVector InOrigin)
    {
        _Owner = InOwner;
        _Origin = InOrigin;
        Request_Reset();
    }

    void Request_Reset()
    {
        DoDestroy_Root();
        _Hull = FCk_Handle_JoltBody();
        _Rejected = FCk_Handle_JoltBody();
        if (ck::Is_NOT_Valid(_Owner))
        {
            return;
        }
        DoCreate_Root();
        DoAdd_Floor(FVector(250.0, 250.0, 10.0));

        const auto Pose = FTransform(FRotator(25.0, 30.0, 15.0), _Origin + FVector(0.0, 0.0, 150.0));
        auto Entity = utils_transform::Create(_Root, Pose, ECk_Replication::DoesNotReplicate);
        _Hull = utils_jolt_body::Add(FCk_Handle(Entity),
            CkRuntimeConvexGym::Make_ConvexBodySpec(CkRuntimeConvexGym::Make_HullPoints(), k_RuntimeConvexGym_HullMassKg));
        CkRuntimeConvexGym::Add_VisualCube(Entity, this, n"OnHullMeshAdded");
    }

    bool Request_Kick()
    {
        if (Get_IsHullReady() == false)
        {
            return false;
        }
        utils_jolt_body::Request_AddImpulse(_Hull,
            FCk_Request_JoltBody_AddImpulse(FVector(0.0, 150.0, 300.0) * k_RuntimeConvexGym_HullMassKg));
        return true;
    }

    // Coplanar input spans no volume, so setup resolves Failed/HullFailed and CkJolt logs its ensure.
    void Request_RejectedHull()
    {
        if (ck::Is_NOT_Valid(_Root))
        {
            return;
        }
        if (ck::IsValid(_Rejected))
        {
            utils_entity_lifetime::Request_DestroyEntity(_Rejected);
        }
        auto Entity = utils_transform::Create(_Root, FTransform(_Origin + FVector(0.0, 0.0, 300.0)), ECk_Replication::DoesNotReplicate);
        _Rejected = utils_jolt_body::Add(FCk_Handle(Entity),
            CkRuntimeConvexGym::Make_ConvexBodySpec(CkRuntimeConvexGym::Make_CoplanarPoints(), 1.0));
    }

    FCk_Handle_JoltBody Get_Hull() const
    {
        return _Hull;
    }

    FCk_Handle_JoltBody Get_Rejected() const
    {
        return _Rejected;
    }

    bool Get_IsHullReady() const
    {
        return ck::IsValid(_Hull) && utils_jolt_body::Get_SetupState(_Hull) == ECk_JoltBody_SetupState::Ready;
    }

    UFUNCTION()
    private void OnHullMeshAdded(FCk_Handle_UnrealComponent InHandle)
    {
        auto MeshComponent = Cast<UStaticMeshComponent>(utils_unreal_component::Get_Component(InHandle));
        if (ck::Is_NOT_Valid(MeshComponent))
        {
            return;
        }
        MeshComponent.SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MeshComponent.SetStaticMesh(CkRuntimeConvexGym::Load_EngineCube(this));
    }
}

// Slice-to-physics and lifecycle stations: a CPU checker cube imported, displayed and given a RuntimeConvex body,
// then cut into two independently owned pieces. A cut keeps its source until BOTH halves' bodies are Ready; any
// failed half destroys both halves and keeps the source.
class UCk_RuntimeConvexGym_SliceFixture : UCk_RuntimeConvexGym_FixtureBase
{
    private FCk_Handle _PiecesOwner;
    private TArray<FCkRuntimeConvexGym_Piece> _Pieces;
    private FCk_Handle_Transform _Specimen;
    private FCk_Handle_RuntimeMesh _SpecimenGeometry;
    private FCk_Handle_Timer _ImportPoll;
    private FCkRuntimeConvexGym_PendingCut _Cut;
    private int32 _CutSerial = 0;

    private int32 _CutsCommitted = 0;
    private int32 _PartialFailures = 0;
    private int32 _StaleCompletions = 0;
    private TArray<FCk_Handle> _Abandoned;
    private TArray<FCk_Handle> _LastHalves;
    private FCkRuntimeConvexGym_HalfResolution _LastPositiveResolution;
    private FCkRuntimeConvexGym_HalfResolution _LastNegativeResolution;
    private FString _LastOutcome = "";
    private FVector _LastCutWorldNormal = FVector(0.0, 1.0, 0.0);
    private FCk_Handle_JoltBody _RestoreBody;
    private TArray<FCk_Handle> _RestoreAfter;
    private FCk_Handle_Timer _RestorePoll;

    void Request_Build(FCk_Handle InOwner, FVector InOrigin)
    {
        _Owner = InOwner;
        _Origin = InOrigin;
        Request_Reset();
    }

    void Request_Reset()
    {
        DoAbandon_PendingCut();
        DoDestroy_Root();
        DoForget_SourceRestore();
        _Pieces.Empty();
        _Specimen = FCk_Handle_Transform();
        _SpecimenGeometry = FCk_Handle_RuntimeMesh();
        _ImportPoll = FCk_Handle_Timer();
        _CutSerial = 0;
        if (ck::Is_NOT_Valid(_Owner))
        {
            return;
        }

        DoCreate_Root();
        DoAdd_Floor(FVector(60.0, 60.0, 5.0));
        _PiecesOwner = utils_entity_lifetime::Request_CreateEntity(_Root);

        // The checker cube's local origin is a corner (0..10 cm), so the pose puts its centre over the floor centre.
        const auto Rotation = FRotator(0.0, 20.0, 0.0);
        const auto Location = _Origin - Rotation.RotateVector(FVector(5.0, 5.0, 0.0)) + FVector(0.0, 0.0, 15.0);
        _Specimen = utils_transform::Create(_PiecesOwner, FTransform(Rotation, Location), ECk_Replication::DoesNotReplicate);
        auto Spec = FCk_RuntimeMesh_Spec();
        Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>(CkRuntimeConvexGym::Get_CheckerMeshPath()));
        _SpecimenGeometry = utils_runtime_mesh::Add(FCk_Handle(_Specimen), Spec);
        _ImportPoll = utils_timer::Create_Tick(FCk_Handle(_Root), FCk_Delegate_Timer(this, n"OnImportPoll"));
    }

    UFUNCTION()
    private void OnImportPoll(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (InTimer != _ImportPoll || ck::Is_NOT_Valid(_SpecimenGeometry))
        {
            return;
        }
        const auto State = utils_runtime_mesh::Get_SetupState(_SpecimenGeometry);
        if (State == ECk_RuntimeMesh_SetupState::Pending)
        {
            return;
        }
        // The pause is a deferred request, so the timer can fire again before it lands; forgetting the handle is
        // what makes this poll one-shot.
        utils_timer::Request_Pause(_ImportPoll);
        _ImportPoll = FCk_Handle_Timer();
        if (State == ECk_RuntimeMesh_SetupState::Failed)
        {
            const auto Failure = utils_runtime_mesh::Get_SetupFailure(_SpecimenGeometry);
            _LastOutcome = f"specimen import failed: {Failure :n}";
            return;
        }
        const auto Volume = utils_runtime_mesh::Get_Metrics(_SpecimenGeometry).Get_VolumeCm3();
        _Pieces.Add(DoCompose_Piece(_Specimen, _SpecimenGeometry, Volume * k_RuntimeConvexGym_DensityKgPerCm3));
    }

    private FCkRuntimeConvexGym_Piece DoCompose_Piece(FCk_Handle_Transform InEntity, FCk_Handle_RuntimeMesh InGeometry, float InMassKg)
    {
        auto Piece = FCkRuntimeConvexGym_Piece();
        Piece.Entity = InEntity;
        Piece.Geometry = InGeometry;
        Piece.MassKg = InMassKg;
        Piece.Display = utils_runtime_mesh_display::Add(InEntity, CkRuntimeConvexGym::Make_CheckerDisplaySpec(InGeometry));
        Piece.Body = utils_jolt_body::Add(FCk_Handle(InEntity),
            CkRuntimeConvexGym::Make_ConvexBodySpec(utils_runtime_mesh::Copy_LocalVerticesCm(InGeometry), InMassKg));
        return Piece;
    }

    //------------------------------------------------------------------------
    // Cutting
    //------------------------------------------------------------------------

    // A piece may be cut when no cut is in flight, the bound leaves room for one more piece, and its body is Ready
    // and asleep. Sleep is Jolt's own rest verdict; a speed threshold is not, because a body is at rest on the frame
    // it is admitted, before gravity has acted on it.
    bool Get_CanCut(int32 InPieceIndex) const
    {
        return ck::IsValid(_Root)
            && _Cut.Active == false
            && _Pieces.IsValidIndex(InPieceIndex)
            && _Pieces.Num() < k_RuntimeConvexGym_MaxPieces
            && Get_IsSettled(InPieceIndex);
    }

    bool Get_IsSettled(int32 InPieceIndex) const
    {
        if (_Pieces.IsValidIndex(InPieceIndex) == false)
        {
            return false;
        }
        const auto Body = _Pieces[InPieceIndex].Body;
        if (ck::Is_NOT_Valid(Body) || utils_jolt_body::Get_SetupState(Body) != ECk_JoltBody_SetupState::Ready)
        {
            return false;
        }
        return utils_jolt_body::Get_SleepState(Body) == ECk_Jolt_SleepState::Asleep;
    }

    // Cuts the piece through its centroid. The source is first made Kinematic on the isolated profile, and the
    // slice is queued only once that swap has been applied: the halves are admitted at the source's pose while it
    // still exists, and in the frame a cut commits the source is already tagged for destruction, which excludes it
    // from the JoltBody request drain, so a swap queued any later could be dropped. Cuts alternate a world-Y and a
    // world-X plane normal, converted into the piece's mesh-local frame (results keep the source's local origin, so
    // they are posed at the source's world transform).
    bool Request_Cut(FCkRuntimeConvexGym_CutRequest InRequest)
    {
        if (Get_CanCut(InRequest.PieceIndex) == false)
        {
            return false;
        }
        auto Source = _Pieces[InRequest.PieceIndex];
        const auto WorldNormal = _CutSerial % 2 == 0 ? FVector(0.0, 1.0, 0.0) : FVector(1.0, 0.0, 0.0);

        _CutSerial++;
        _Cut = FCkRuntimeConvexGym_PendingCut();
        _Cut.Active = true;
        _Cut.OperationID = FGuid(uint32(_Generation), uint32(_CutSerial), 0x52434f4e, 0x47594d00);
        _Cut.Source = Source.Entity;
        _Cut.SourceGeometry = Source.Geometry;
        _Cut.SourceBody = Source.Body;
        _Cut.SourceMassKg = Source.MassKg;
        _Cut.WorldNormal = WorldNormal;
        _LastCutWorldNormal = WorldNormal;
        _Cut.Request = InRequest;
        _LastHalves.Empty();

        utils_jolt_body::Request_SetMotionType(Source.Body, FCk_Request_JoltBody_SetMotionType(ECk_MotionType::Kinematic));
        utils_jolt_body::Request_SetCollisionProfile(Source.Body,
            FCk_Request_JoltBody_SetCollisionProfile(CkRuntimeConvexGym::Get_IsolatedProfile()),
            FCk_Delegate_Request_OnCompleted(this, n"OnSourceIsolated"));
        return true;
    }

    UFUNCTION()
    private void OnSourceIsolated(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        const auto IsCurrent = ck::IsValid(_Root)
            && _Cut.Active
            && _Cut.Submitted == false
            && InRequestOwner == FCk_Handle(_Cut.SourceBody);
        if (IsCurrent == false)
        {
            _StaleCompletions++;
            return;
        }
        if (InResult != ECk_Request_OperationResult::Succeeded)
        {
            _LastOutcome = f"source isolation {InResult :n}; cut abandoned";
            DoRestore_Source(_Cut.SourceBody);
            _Cut = FCkRuntimeConvexGym_PendingCut();
            return;
        }
        DoSubmit_Slice();
    }

    // A synchronous rejection resolves on this call stack.
    private void DoSubmit_Slice()
    {
        const auto Pose = utils_transform::Get_EntityCurrentTransform(_Cut.Source);
        auto Plane = FCk_RuntimeMesh_PlaneLocal();
        Plane.Set_PositionCm(utils_runtime_mesh::Get_Metrics(_Cut.SourceGeometry).Get_CentroidCm());
        Plane.Set_Normal(Pose.GetRotation().UnrotateVector(_Cut.WorldNormal));
        Plane.Set_Tangent(Pose.GetRotation().UnrotateVector(FVector(0.0, 0.0, 1.0)));

        auto Cap = FCk_RuntimeMesh_Cap();
        Cap.Set_MaterialID(k_RuntimeConvexGym_CapMaterialID);
        Cap.Set_CmPerUVUnit(10.0);
        auto Request = FCk_Request_RuntimeMesh_Slice();
        Request.Set_OperationID(_Cut.OperationID);
        Request.Set_ResultOwner(_PiecesOwner);
        Request.Set_Plane(Plane);
        Request.Set_Cap(Cap);

        const auto OperationID = _Cut.OperationID;
        const auto Interrupt = _Cut.Request.Interrupt;
        _Cut.Submitted = true;
        utils_runtime_mesh::Request_Slice(_Cut.SourceGeometry, Request,
            FCk_Delegate_RuntimeMesh_OnSliceResolved(this, n"OnSliceResolved"));

        const auto SliceIsStillQueued = _Cut.Active && _Cut.Sliced == false && _Cut.OperationID == OperationID;
        if (SliceIsStillQueued && Interrupt == ECkRuntimeConvexGym_CutInterrupt::ResetAfterSliceSubmitted)
        {
            Request_Reset();
        }
    }

    private void DoRestore_Source(FCk_Handle_JoltBody InSourceBody)
    {
        if (CkRuntimeConvexGym::Get_IsAlive(InSourceBody) == false)
        {
            return;
        }
        auto Body = InSourceBody;
        utils_jolt_body::Request_SetCollisionProfile(Body, FCk_Request_JoltBody_SetCollisionProfile(CkRuntimeConvexGym::Get_PieceProfile()));
        utils_jolt_body::Request_SetMotionType(Body, FCk_Request_JoltBody_SetMotionType(ECk_MotionType::Dynamic));
    }

    // After a partial composition the admitted half still steps until its teardown, so the source rejoins the
    // simulation only once both halves are gone. A handle reads invalid from the Teardown phase on, which runs after
    // FGroup_EndPlay, where the JoltBody EndPlay has already removed the body.
    private void DoSchedule_SourceRestore(FCk_Handle_JoltBody InSourceBody, const TArray<FCk_Handle>&in InHalves)
    {
        _RestoreBody = InSourceBody;
        _RestoreAfter = InHalves;
        _RestorePoll = utils_timer::Create_Tick(FCk_Handle(_Root), FCk_Delegate_Timer(this, n"OnRestorePoll"));
    }

    UFUNCTION()
    private void OnRestorePoll(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (InTimer != _RestorePoll)
        {
            return;
        }
        for (auto Half : _RestoreAfter)
        {
            if (ck::IsValid(Half))
            {
                return;
            }
        }
        utils_timer::Request_Pause(_RestorePoll);
        _RestorePoll = FCk_Handle_Timer();
        DoRestore_Source(_RestoreBody);
        _RestoreBody = FCk_Handle_JoltBody();
        _RestoreAfter.Empty();
    }

    UFUNCTION()
    private void OnSliceResolved(FCk_RuntimeMesh_SliceResult InResult)
    {
        const auto OperationGeneration = int32(InResult.Get_OperationID()[0]);
        const auto IsCurrent = ck::IsValid(_Root)
            && OperationGeneration == _Generation
            && _Cut.Active
            && _Cut.Submitted
            && _Cut.Sliced == false
            && InResult.Get_OperationID() == _Cut.OperationID;
        if (IsCurrent == false)
        {
            _StaleCompletions++;
            DoDiscard(FCk_Handle(InResult.Get_Positive()));
            DoDiscard(FCk_Handle(InResult.Get_Negative()));
            return;
        }

        _Cut.Sliced = true;
        if (InResult.Get_Outcome() != ECk_RuntimeMesh_SliceOutcome::Succeeded)
        {
            const auto Outcome = InResult.Get_Outcome();
            _LastOutcome = f"slice {Outcome :n}; source kept";
            DoRestore_Source(_Cut.SourceBody);
            _Cut = FCkRuntimeConvexGym_PendingCut();
            return;
        }
        if (CkRuntimeConvexGym::Get_IsAlive(_Cut.Source) == false)
        {
            _LastOutcome = "source destroyed before its halves were composed; halves discarded";
            DoDiscard(FCk_Handle(InResult.Get_Positive()));
            DoDiscard(FCk_Handle(InResult.Get_Negative()));
            _Cut = FCkRuntimeConvexGym_PendingCut();
            return;
        }

        const auto Pose = utils_transform::Get_EntityCurrentTransform(_Cut.Source);
        const auto PositiveVolume = InResult.Get_PositiveMetrics().Get_VolumeCm3();
        const auto NegativeVolume = InResult.Get_NegativeMetrics().Get_VolumeCm3();
        const auto TotalVolume = PositiveVolume + NegativeVolume;

        auto PositiveEntity = utils_transform::Add(FCk_Handle(InResult.Get_Positive()), Pose, ECk_Replication::DoesNotReplicate);
        auto NegativeEntity = utils_transform::Add(FCk_Handle(InResult.Get_Negative()), Pose, ECk_Replication::DoesNotReplicate);
        _Cut.Positive = DoCompose_Piece(PositiveEntity, InResult.Get_Positive(), _Cut.SourceMassKg * PositiveVolume / TotalVolume);
        _Cut.Negative = DoCompose_Piece(NegativeEntity, InResult.Get_Negative(), _Cut.SourceMassKg * NegativeVolume / TotalVolume);
        _LastHalves.Add(FCk_Handle(PositiveEntity));
        _LastHalves.Add(FCk_Handle(NegativeEntity));

        // Both are composed before either promise is registered: a promise on an already-failed body fires on this
        // call stack, and the cut must not finish while the other half does not exist yet.
        const auto OperationID = _Cut.OperationID;
        const auto Interrupt = _Cut.Request.Interrupt;
        const auto NegativeHalf = FCk_Handle(NegativeEntity);
        DoPromise_Half(_Cut.Positive.Body, true);
        DoPromise_Half(_Cut.Negative.Body, false);

        const auto CutIsStillPending = _Cut.Active && _Cut.OperationID == OperationID;
        if (CutIsStillPending == false)
        {
            return;
        }
        if (Interrupt == ECkRuntimeConvexGym_CutInterrupt::ResetAfterHalvesComposed)
        {
            Request_Reset();
        }
        else if (Interrupt == ECkRuntimeConvexGym_CutInterrupt::DestroyNegativeHalfBeforeAdmission)
        {
            utils_entity_lifetime::Request_DestroyEntity(NegativeHalf);
        }
    }

    private void DoPromise_Half(FCk_Handle_JoltBody InBody, bool InIsPositive)
    {
        const auto Promised = ck::IsValid(InBody)
            && utils_jolt_body::TryPromise_OnSetupResolved(InBody, FCk_Delegate_JoltBody_OnSetupResolved(this, n"OnHalfBodyResolved"));
        if (Promised)
        {
            return;
        }
        DoRecord_Half(InIsPositive, ECk_JoltBody_SetupState::Failed,
            ck::IsValid(InBody) ? utils_jolt_body::Get_SetupFailure(InBody) : ECk_JoltBody_SetupFailure::InvalidInput);
    }

    UFUNCTION()
    private void OnHalfBodyResolved(FCk_Handle_JoltBody InBody, ECk_JoltBody_SetupState InState, ECk_JoltBody_SetupFailure InFailure)
    {
        const auto IsPending = ck::IsValid(_Root) && _Cut.Active && _Cut.Sliced;
        const auto IsPositive = IsPending && InBody == _Cut.Positive.Body;
        const auto IsNegative = IsPending && InBody == _Cut.Negative.Body;
        if (IsPositive == false && IsNegative == false)
        {
            _StaleCompletions++;
            DoDiscard(FCk_Handle(InBody));
            return;
        }
        DoRecord_Half(IsPositive, InState, InFailure);
    }

    private void DoRecord_Half(bool InIsPositive, ECk_JoltBody_SetupState InState, ECk_JoltBody_SetupFailure InFailure)
    {
        auto Resolution = FCkRuntimeConvexGym_HalfResolution();
        Resolution.Resolved = true;
        Resolution.State = InState;
        Resolution.Failure = InFailure;
        if (InIsPositive)
        {
            _Cut.PositiveResolution = Resolution;
        }
        else
        {
            _Cut.NegativeResolution = Resolution;
        }
        if (_Cut.PositiveResolution.Resolved && _Cut.NegativeResolution.Resolved)
        {
            DoFinish_Cut();
        }
    }

    private void DoFinish_Cut()
    {
        auto Cut = _Cut;
        _Cut = FCkRuntimeConvexGym_PendingCut();
        _LastPositiveResolution = Cut.PositiveResolution;
        _LastNegativeResolution = Cut.NegativeResolution;

        const auto BothReady = Cut.PositiveResolution.State == ECk_JoltBody_SetupState::Ready
            && Cut.NegativeResolution.State == ECk_JoltBody_SetupState::Ready;
        if (BothReady == false)
        {
            utils_entity_lifetime::Request_DestroyEntity(FCk_Handle(Cut.Positive.Entity));
            utils_entity_lifetime::Request_DestroyEntity(FCk_Handle(Cut.Negative.Entity));
            TArray<FCk_Handle> Halves;
            Halves.Add(FCk_Handle(Cut.Positive.Entity));
            Halves.Add(FCk_Handle(Cut.Negative.Entity));
            DoSchedule_SourceRestore(Cut.SourceBody, Halves);
            _PartialFailures++;
            const auto PositiveFailure = Cut.PositiveResolution.Failure;
            const auto NegativeFailure = Cut.NegativeResolution.Failure;
            _LastOutcome = f"partial composition (positive {PositiveFailure :n}, negative {NegativeFailure :n}): both halves destroyed, source kept";
            return;
        }

        for (auto Index = _Pieces.Num() - 1; Index >= 0; Index--)
        {
            if (_Pieces[Index].Entity == Cut.Source)
            {
                _Pieces.RemoveAt(Index);
            }
        }
        utils_entity_lifetime::Request_DestroyEntity(FCk_Handle(Cut.Source));
        _Pieces.Add(Cut.Positive);
        _Pieces.Add(Cut.Negative);

        const auto Speed = Cut.Request.SeparationSpeedCmS;
        if (Speed > 0.0)
        {
            auto PositiveBody = Cut.Positive.Body;
            auto NegativeBody = Cut.Negative.Body;
            utils_jolt_body::Request_AddImpulse(PositiveBody, FCk_Request_JoltBody_AddImpulse(Cut.WorldNormal * (Speed * Cut.Positive.MassKg)));
            utils_jolt_body::Request_AddImpulse(NegativeBody, FCk_Request_JoltBody_AddImpulse(Cut.WorldNormal * (-Speed * Cut.Negative.MassKg)));
        }
        _CutsCommitted++;
        const auto PositiveMass = Cut.Positive.MassKg;
        const auto NegativeMass = Cut.Negative.MassKg;
        _LastOutcome = f"cut committed: halves {PositiveMass :.3} kg + {NegativeMass :.3} kg, source destroyed, {Speed :.0} cm/s separation";
    }

    void Request_Teardown() override
    {
        DoAbandon_PendingCut();
        DoForget_SourceRestore();
        Super::Request_Teardown();
    }

    // The source a pending restore would rejoin is destroyed with the old root, so there is nothing to restore.
    private void DoForget_SourceRestore()
    {
        if (ck::IsValid(_RestorePoll))
        {
            utils_timer::Request_Pause(_RestorePoll);
        }
        _RestorePoll = FCk_Handle_Timer();
        _RestoreBody = FCk_Handle_JoltBody();
        _RestoreAfter.Empty();
    }

    // A reset forgets the pending cut; whatever it already produced is destroyed with the old root and its late
    // completions arrive stale.
    private void DoAbandon_PendingCut()
    {
        for (auto Index = _Abandoned.Num() - 1; Index >= 0; Index--)
        {
            if (ck::Is_NOT_Valid(_Abandoned[Index]))
            {
                _Abandoned.RemoveAt(Index);
            }
        }
        if (_Cut.Active && _Cut.Sliced)
        {
            _Abandoned.Add(FCk_Handle(_Cut.Positive.Entity));
            _Abandoned.Add(FCk_Handle(_Cut.Negative.Entity));
        }
        _Cut = FCkRuntimeConvexGym_PendingCut();
    }

    private void DoDiscard(FCk_Handle InEntity)
    {
        if (ck::Is_NOT_Valid(InEntity))
        {
            return;
        }
        _Abandoned.AddUnique(InEntity);
        utils_entity_lifetime::Request_DestroyEntity(InEntity);
    }

    //------------------------------------------------------------------------
    // Lifetime
    //------------------------------------------------------------------------

    bool Request_DestroyPiece(int32 InPieceIndex)
    {
        if (_Cut.Active || _Pieces.IsValidIndex(InPieceIndex) == false)
        {
            return false;
        }
        utils_entity_lifetime::Request_DestroyEntity(FCk_Handle(_Pieces[InPieceIndex].Entity));
        _Pieces.RemoveAt(InPieceIndex);
        _LastOutcome = "piece destroyed; the others keep their own lifetimes";
        return true;
    }

    //------------------------------------------------------------------------
    // Readback
    //------------------------------------------------------------------------

    int32 Get_PieceCount() const
    {
        return _Pieces.Num();
    }

    int32 Get_MaxPieces() const
    {
        return k_RuntimeConvexGym_MaxPieces;
    }

    FCkRuntimeConvexGym_Piece Get_Piece(int32 InPieceIndex) const
    {
        if (_Pieces.IsValidIndex(InPieceIndex) == false)
        {
            return FCkRuntimeConvexGym_Piece();
        }
        return _Pieces[InPieceIndex];
    }

    int32 Get_ReadyBodyCount() const
    {
        auto Count = 0;
        for (auto Piece : _Pieces)
        {
            if (ck::IsValid(Piece.Body) && utils_jolt_body::Get_SetupState(Piece.Body) == ECk_JoltBody_SetupState::Ready)
            {
                Count++;
            }
        }
        return Count;
    }

    FCk_Handle Get_PiecesOwner() const
    {
        return _PiecesOwner;
    }

    FCk_Handle_RuntimeMesh Get_SpecimenGeometry() const
    {
        return _SpecimenGeometry;
    }

    bool Get_IsCutInFlight() const
    {
        return _Cut.Active;
    }

    FString Get_CutPhase() const
    {
        if (_Cut.Active == false)
        {
            return "idle";
        }
        if (_Cut.Submitted == false)
        {
            return "isolating the source (Kinematic, ignores the halves)";
        }
        return _Cut.Sliced ? "halves composed, waiting for both bodies" : "slice queued";
    }

    int32 Get_CutsCommitted() const
    {
        return _CutsCommitted;
    }

    int32 Get_PartialFailures() const
    {
        return _PartialFailures;
    }

    int32 Get_StaleCompletions() const
    {
        return _StaleCompletions;
    }

    // Entities a reset or a stale completion handed over for destruction that have not reached Teardown. Teardown
    // runs after FGroup_EndPlay, where the JoltBody EndPlay removes the body, so zero means none of their bodies is
    // left in the Jolt world.
    int32 Get_AbandonedAliveCount() const
    {
        auto Count = 0;
        for (auto Entity : _Abandoned)
        {
            if (ck::IsValid(Entity))
            {
                Count++;
            }
        }
        return Count;
    }

    // The two half entities of the most recent cut that got as far as composition.
    TArray<FCk_Handle> Get_LastHalves() const
    {
        return _LastHalves;
    }

    FCkRuntimeConvexGym_HalfResolution Get_LastPositiveResolution() const
    {
        return _LastPositiveResolution;
    }

    FCkRuntimeConvexGym_HalfResolution Get_LastNegativeResolution() const
    {
        return _LastNegativeResolution;
    }

    FVector Get_LastCutWorldNormal() const
    {
        return _LastCutWorldNormal;
    }

    FString Get_LastOutcome() const
    {
        return _LastOutcome;
    }
}
