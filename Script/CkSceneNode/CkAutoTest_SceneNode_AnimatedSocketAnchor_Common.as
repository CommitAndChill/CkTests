// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: SHARED RIG FOR ANIMATED SOCKET ANCHORS
//============================================================================
//
// A scene node made with CreateAndAttachToUnrealMesh on a skinned mesh follows
// a bone that animation moves. Animation publishes a skinned mesh's pose late
// in the frame (as late as the end of TG_PostPhysics), after every TG_PrePhysics
// processor ran, so the anchored node must be re-sampled once the pose is final.
//
// The rig:
//   - PoseDriver: an actor with a UPoseableMeshComponent (SKM_Manny_Simple). It
//     ticks in TG_PostPhysics, where a skeletal mesh's animation would finish,
//     and moves hand_r to a new component-space transform every frame it is
//     driving (about 15 cm and several degrees a frame), publishing the pose
//     synchronously with RefreshBoneTransforms. Works under -nullrhi.
//   - Probe: an actor ticking in TG_LastDemotable, after everything else in the
//     frame. Each frame the driver moved the bone, it compares every registered
//     entity's ECS transform (and, optionally, a component's world transform)
//     with where its offsets put it on hand_r as the mesh reads now. It also
//     counts frames, and how often a test's signal listener fired in each one.
//     When MoveDriverAfterEachFrame is set it then moves the driver's actor by
//     that much: after the frame's late pass, before the next frame's early one.
//============================================================================

namespace ck_scene_node_late_pose
{
    const FName k_Bone = n"hand_r";
    const FVector k_DriverLocation = FVector(400.0, 600.0, 100.0);
}

// --------------------------------------------------------------------------------------------------------------------

class ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver : AActor
{
    default bReplicates = false;

    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent SceneRoot;

    UPROPERTY(DefaultComponent, Attach = SceneRoot)
    UPoseableMeshComponent Mesh;

    default SceneRoot.Mobility = EComponentMobility::Movable;
    default Mesh.Mobility = EComponentMobility::Movable;

    // While true, every tick moves hand_r and counts one pose change.
    bool IsDriving = false;
    int32 PoseChanges = 0;

    // One-shot teardown done from this tick (TG_PostPhysics): between the frame's early ECS pass and its late one.
    FCk_Handle DestroyEntityOnNextTick;
    AActor DestroyActorOnNextTick;
    bool DestroySelfOnNextTick = false;

    private FTransform _RestPose;
    private float64 _Phase = 0.0;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        SetTickGroup(ETickingGroup::TG_PostPhysics);
        Mesh.SetSkinnedAssetAndUpdate(assets::load::SKM_Manny_Simple());
        Mesh.AllocateTransformData();
        _RestPose = Mesh.GetBoneTransformByName(ck_scene_node_late_pose::k_Bone, EBoneSpaces::ComponentSpace);
    }

    bool Get_HasBone() const
    {
        return ck::IsValid(Mesh) && Mesh.DoesSocketExist(ck_scene_node_late_pose::k_Bone);
    }

    FTransform Get_Socket() const
    {
        return Mesh.GetSocketTransform(ck_scene_node_late_pose::k_Bone);
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (ck::IsValid(DestroyEntityOnNextTick))
        {
            auto Entity = DestroyEntityOnNextTick;
            DestroyEntityOnNextTick = FCk_Handle();
            utils_entity_lifetime::Request_DestroyEntity(Entity);
        }

        if (ck::IsValid(DestroyActorOnNextTick))
        {
            auto ToDestroy = DestroyActorOnNextTick;
            DestroyActorOnNextTick = nullptr;
            ToDestroy.DestroyActor();
        }

        if (DestroySelfOnNextTick)
        {
            DestroySelfOnNextTick = false;
            DestroyActor();
            return;
        }

        if (IsDriving == false)
        { return; }

        _Phase += 0.5;
        const auto Location = _RestPose.GetLocation()
            + FVector(Math::Cos(_Phase) * 30.0, Math::Sin(_Phase) * 30.0, Math::Sin(_Phase * 0.5) * 10.0);
        const auto Rotation = FRotator(Math::Sin(_Phase) * 25.0, _Phase * 20.0, 0.0).Quaternion() * _RestPose.GetRotation();

        Mesh.SetBoneTransformByName(ck_scene_node_late_pose::k_Bone,
            FTransform(Rotation, Location, _RestPose.GetScale3D()), EBoneSpaces::ComponentSpace);
        Mesh.RefreshBoneTransforms();
        PoseChanges += 1;
    }
}

// --------------------------------------------------------------------------------------------------------------------

// A static mesh on a movable root, for anchors that move without any animated pose.
class ACk_AutoTest_SceneNode_AnimatedSocketAnchor_StaticMover : AActor
{
    default bReplicates = false;

    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent SceneRoot;

    UPROPERTY(DefaultComponent, Attach = SceneRoot)
    UStaticMeshComponent Mesh;

    default SceneRoot.Mobility = EComponentMobility::Movable;
    default Mesh.Mobility = EComponentMobility::Movable;
}

// --------------------------------------------------------------------------------------------------------------------

struct FCk_AutoTest_SceneNode_LatePose_Check
{
    FString Name;
    FCk_Handle_Transform Node;
    // The node's world is FromSocket * hand_r: its offsets composed down the scene-node chain.
    FTransform FromSocket;
    // When set, this component's world transform must match the node's expected world too.
    USceneComponent Component;
}

class ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe : AActor
{
    default bReplicates = false;

    ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver Driver;
    TArray<FCk_AutoTest_SceneNode_LatePose_Check> Checks;
    bool IsSampling = false;

    int32 Samples = 0;
    float64 MaxLocationErrorCm = 0.0;
    float64 MaxRotationErrorDeg = 0.0;
    FString WorstCheck = "";
    // How far hand_r moved between sampled frames, the lag the checks would show if they trailed by one.
    float64 MaxSocketTravelCm = 0.0;

    // Frames this probe has ticked (every frame, sampling or not).
    int32 Frames = 0;
    // When non-zero, the driver's actor moves by this much at the end of every probe tick.
    FVector MoveDriverAfterEachFrame = FVector::ZeroVector;
    // A test's listener adds to PendingSignalFires; each frame the probe folds it into the totals.
    int32 PendingSignalFires = 0;
    int32 TotalSignalFires = 0;
    int32 MaxSignalFiresPerFrame = 0;

    private int32 _LastPoseChanges = -1;
    private FVector _LastSocket = FVector::ZeroVector;
    private bool _HasLastSocket = false;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        SetTickGroup(ETickingGroup::TG_LastDemotable);
    }

    void Reset_Samples()
    {
        Samples = 0;
        MaxLocationErrorCm = 0.0;
        MaxRotationErrorDeg = 0.0;
        WorstCheck = "";
        MaxSocketTravelCm = 0.0;
        PendingSignalFires = 0;
        TotalSignalFires = 0;
        MaxSignalFiresPerFrame = 0;
        _HasLastSocket = false;
        _LastPoseChanges = ck::IsValid(Driver) ? Driver.PoseChanges : -1;
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        Frames += 1;

        Sample();

        if (MoveDriverAfterEachFrame.IsZero() == false && ck::IsValid(Driver))
        { Driver.SetActorLocation(Driver.GetActorLocation() + MoveDriverAfterEachFrame); }
    }

    private void Sample()
    {
        if (IsSampling == false || ck::Is_NOT_Valid(Driver))
        { return; }

        TotalSignalFires += PendingSignalFires;
        MaxSignalFiresPerFrame = Math::Max(MaxSignalFiresPerFrame, PendingSignalFires);
        PendingSignalFires = 0;

        if (Driver.PoseChanges == _LastPoseChanges)
        { return; }

        _LastPoseChanges = Driver.PoseChanges;

        const auto Socket = Driver.Get_Socket();
        if (_HasLastSocket)
        { MaxSocketTravelCm = Math::Max(MaxSocketTravelCm, (Socket.GetLocation() - _LastSocket).Size()); }

        _LastSocket = Socket.GetLocation();
        _HasLastSocket = true;

        for (const auto& Check : Checks)
        {
            if (ck::Is_NOT_Valid(Check.Node))
            { continue; }

            const auto Expected = Check.FromSocket * Socket;
            Record(Check.Name, Expected, utils_transform::Get_EntityCurrentTransform(Check.Node));

            if (ck::IsValid(Check.Component))
            { Record(Check.Name + " component", Expected, Check.Component.GetWorldTransform()); }
        }

        Samples += 1;
    }

    private void Record(const FString& InName, FTransform InExpected, FTransform InActual)
    {
        const auto LocationError = (InActual.GetLocation() - InExpected.GetLocation()).Size();
        const auto RotationError = Math::RadiansToDegrees(InActual.GetRotation().AngularDistance(InExpected.GetRotation()));

        if (LocationError > MaxLocationErrorCm || RotationError > MaxRotationErrorDeg)
        { WorstCheck = InName; }

        MaxLocationErrorCm = Math::Max(MaxLocationErrorCm, LocationError);
        MaxRotationErrorDeg = Math::Max(MaxRotationErrorDeg, RotationError);
    }
}
