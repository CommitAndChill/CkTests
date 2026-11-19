// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: AN EARLY CONSUMER'S PREVIOUS STAYS CONTIGUOUS
//============================================================================
//
// A consumer that runs between the frame's early ECS pass and its late
// re-sample (a TG_DuringPhysics tick here; in the framework a TG_PostPhysics
// probe sweep, a ray-sense sweep, an ISM's previous transform) reads each
// node's (Previous, Current) pair. Frame to frame those pairs must chain: this
// frame's Previous is the Current that consumer read last frame, or a step of
// the motion falls in no pair.
//
// The rig makes every frame write a late anchor twice: the pose driver moves
// hand_r in TG_PostPhysics (so the late re-sample writes), and the probe moves
// the driver's actor after everything else in the frame (so next frame's early
// sample writes again before anything reads). A node and its child are read;
// for k_Frames frames each pair must start within 0.01 cm of the last frame's
// Current, while the bone travels more than 10 cm a frame.
//
// See CkAutoTest_SceneNode_AnimatedSocketAnchor_Common.as for the rig.
//============================================================================

class ACk_AutoTest_SceneNode_AnimatedSocketAnchor_EarlyReader : AActor
{
    default bReplicates = false;

    TArray<FCk_Handle_Transform> Nodes;
    TArray<FString> Names;
    bool IsReading = false;

    int32 Samples = 0;
    // The largest distance between a pair's Previous and the Current read the frame before.
    float64 MaxGapCm = 0.0;
    FString WorstNode = "";
    // The smallest step a pair showed, to prove the nodes moved every frame.
    float64 MinStepCm = 1000000.0;

    private TArray<FVector> _LastCurrent;
    private TArray<bool> _HasLast;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        SetTickGroup(ETickingGroup::TG_DuringPhysics);
    }

    void Start_Reading()
    {
        _LastCurrent.SetNum(Nodes.Num());
        _HasLast.SetNum(Nodes.Num());
        for (int32 Index = 0; Index < Nodes.Num(); ++Index)
        { _HasLast[Index] = false; }

        Samples = 0;
        MaxGapCm = 0.0;
        WorstNode = "";
        MinStepCm = 1000000.0;
        IsReading = true;
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (IsReading == false)
        { return; }

        for (int32 Index = 0; Index < Nodes.Num(); ++Index)
        {
            if (ck::Is_NOT_Valid(Nodes[Index]))
            { continue; }

            auto Previous = FTransform();
            if (UCk_Utils_AutoTest_SceneNodeLatePose_UE::TryGet_PreviousTransform(Nodes[Index], Previous) == false)
            { continue; }

            const auto Current = utils_transform::Get_EntityCurrentTransform(Nodes[Index]);

            if (_HasLast[Index])
            {
                const auto Gap = (Previous.GetLocation() - _LastCurrent[Index]).Size();
                if (Gap > MaxGapCm)
                {
                    MaxGapCm = Gap;
                    WorstNode = Names[Index];
                }

                MinStepCm = Math::Min(MinStepCm, (Current.GetLocation() - Previous.GetLocation()).Size());
            }

            _LastCurrent[Index] = Current.GetLocation();
            _HasLast[Index] = true;
        }

        Samples += 1;
    }
}

// --------------------------------------------------------------------------------------------------------------------

class UCk_AutoTest_SceneNode_AnimatedSocketAnchor_PreviousStaysContiguousForAnEarlyConsumer : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;

    private const int32 k_Frames = 30;
    private const int32 k_WarmupFrames = 5;
    private const float64 k_GapToleranceCm = 0.01;
    private const FVector k_ActorStepPerFrame = FVector(4.0, 0.0, 0.0);

    private const FTransform k_NodeOffset = FTransform(FRotator(10.0, 30.0, 0.0), FVector(4.0, -2.0, 6.0));
    private const FTransform k_ChildOffset = FTransform(FRotator(0.0, -45.0, 20.0), FVector(12.0, 0.0, 3.0));

    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver _Driver;
    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe _Probe;
    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_EarlyReader _Reader;
    private FCk_Handle_SceneNode _Node;
    private FCk_Handle_SceneNode _Child;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the pose driver, the probe and the early reader", n"Step_Spawn");
        Add_Step_WaitUntil("the driver's mesh has hand_r", n"Check_HasBone");
        Add_Step("compose the node and its child", n"Step_Compose");
        Add_Step("drive the pose and move the actor after each frame", n"Step_StartDriving");
        Add_Step_WaitFrames("let the chain take the moving pose", k_WarmupFrames);
        Add_Step("start reading", n"Step_StartReading");
        Add_Step_WaitUntil("read the frames", n"Check_Read");
        Add_Step("every pair started where the last one ended", n"Step_Assert");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Spawn(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver = Cast<ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver>(SpawnActor(
            ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver, ck_scene_node_late_pose::k_DriverLocation, FRotator::ZeroRotator));
        _Probe = Cast<ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe>(SpawnActor(
            ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe, ck_scene_node_late_pose::k_DriverLocation, FRotator::ZeroRotator));
        _Reader = Cast<ACk_AutoTest_SceneNode_AnimatedSocketAnchor_EarlyReader>(SpawnActor(
            ACk_AutoTest_SceneNode_AnimatedSocketAnchor_EarlyReader, ck_scene_node_late_pose::k_DriverLocation, FRotator::ZeroRotator));

        if (ck::Is_NOT_Valid(_Driver) || ck::Is_NOT_Valid(_Probe) || ck::Is_NOT_Valid(_Reader))
        { FinishFailure("the pose driver, the probe or the early reader did not spawn"); }
    }

    UFUNCTION()
    private void Check_HasBone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Driver.Get_HasBone());
    }

    UFUNCTION()
    private void Step_Compose(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto Root = utils_transform::Add(RootEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);

        _Node = utils_scene_node::CreateAndAttachToUnrealMesh(Root, _Driver.Mesh, ck_scene_node_late_pose::k_Bone, k_NodeOffset);
        if (ck::Is_NOT_Valid(_Node))
        {
            FinishFailure("CreateAndAttachToUnrealMesh returned an invalid handle");
            return;
        }

        auto NodeTransform = _Node.As_Transform();
        _Child = utils_scene_node::Create(NodeTransform, k_ChildOffset);
    }

    UFUNCTION()
    private void Step_StartDriving(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Probe.Driver = _Driver;
        _Probe.MoveDriverAfterEachFrame = k_ActorStepPerFrame;
        _Probe.Reset_Samples();
        _Probe.IsSampling = true;
        _Driver.IsDriving = true;
    }

    UFUNCTION()
    private void Step_StartReading(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Reader.Nodes.Add(_Node.As_Transform());
        _Reader.Names.Add("node");
        _Reader.Nodes.Add(_Child.As_Transform());
        _Reader.Names.Add("child");
        _Reader.Start_Reading();
        _Probe.Reset_Samples();
    }

    UFUNCTION()
    private void Check_Read(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Reader.Samples >= k_Frames);
    }

    UFUNCTION()
    private void Step_Assert(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Reader.IsReading = false;
        _Probe.IsSampling = false;
        _Probe.MoveDriverAfterEachFrame = FVector::ZeroVector;
        _Driver.IsDriving = false;

        Log(f"[AnimatedSocketAnchor] early reader samples={_Reader.Samples} maxGap={_Reader.MaxGapCm :.4}cm worst={_Reader.WorstNode} minStep={_Reader.MinStepCm :.2}cm socketTravel={_Probe.MaxSocketTravelCm :.2}cm");

        Assert_True(_Probe.MaxSocketTravelCm >= 10.0,
            f"the pose moved hand_r at least 10 cm a frame (max {_Probe.MaxSocketTravelCm :.2} cm)");
        Assert_True(_Reader.MinStepCm >= 1.0,
            f"every pair the early reader saw moved (smallest step {_Reader.MinStepCm :.2} cm)");
        Assert_True(_Reader.MaxGapCm <= k_GapToleranceCm,
            f"every pair started at the Current read the frame before, within {k_GapToleranceCm} cm (max gap {_Reader.MaxGapCm :.4} cm, {_Reader.WorstNode})");

        _Reader.DestroyActor();
        _Probe.DestroyActor();
        _Driver.DestroyActor();
    }
}
