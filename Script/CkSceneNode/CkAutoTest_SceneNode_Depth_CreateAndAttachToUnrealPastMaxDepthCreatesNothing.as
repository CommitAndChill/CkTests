// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: AN UNREAL-ANCHORED CREATE PAST THE MAXIMUM DEPTH CREATES NOTHING
//============================================================================
//
// CreateAndAttachToUnrealComponent and CreateAndAttachToUnrealMesh take a
// scene-node parent like Create does. Under a node at the deepest depth both
// must be refused the same way: an invalid handle, no entity left under the
// owner, and one error each naming the chain (two chains, so each error is
// told apart). The anchor is a skinned mesh's bone, which counts as a socket.
//============================================================================

class UCk_AutoTest_SceneNode_Depth_CreateAndAttachToUnrealPastMaxDepthCreatesNothing : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 8.0f;

    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver _Driver;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the anchor actor", n"Step_Spawn");
        Add_Step_WaitUntil("the anchor mesh has its bone", n"Check_HasBone");
        Add_Step("both Unreal-anchored creates past the deepest depth are refused", n"Step_CreateBoth");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Spawn(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver = Cast<ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver>(SpawnActor(
            ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver, ck_scene_node_late_pose::k_DriverLocation, FRotator::ZeroRotator));
        if (ck::Is_NOT_Valid(_Driver))
        { FinishFailure("the anchor actor did not spawn"); }
    }

    UFUNCTION()
    private void Check_HasBone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Driver.Get_HasBone());
    }

    UFUNCTION()
    private void Step_CreateBoth(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto ComponentLeaf = Make_DeepestNode(InHandle, "DepthUnrealComponent");
        auto MeshLeaf = Make_DeepestNode(InHandle, "DepthUnrealMesh");
        if (IsFinished())
        { return; }

        const auto ComponentDependentsBefore = utils_entity_lifetime::Get_LifetimeDependents(ComponentLeaf).Num();
        auto ComponentNode = utils_scene_node::CreateAndAttachToUnrealComponent(ComponentLeaf, _Driver.SceneRoot, FTransform::Identity);
        Assert_True(ck::Is_NOT_Valid(ComponentNode), "CreateAndAttachToUnrealComponent under the deepest node must return an invalid handle");
        Assert_Equals_Int(utils_entity_lifetime::Get_LifetimeDependents(ComponentLeaf).Num(), ComponentDependentsBefore,
            "A refused CreateAndAttachToUnrealComponent must leave no entity under its owner: the owner's lifetime dependents");

        const auto MeshDependentsBefore = utils_entity_lifetime::Get_LifetimeDependents(MeshLeaf).Num();
        auto MeshNode = utils_scene_node::CreateAndAttachToUnrealMesh(MeshLeaf, _Driver.Mesh, ck_scene_node_late_pose::k_Bone, FTransform::Identity);
        Assert_True(ck::Is_NOT_Valid(MeshNode), "CreateAndAttachToUnrealMesh under the deepest node must return an invalid handle");
        Assert_Equals_Int(utils_entity_lifetime::Get_LifetimeDependents(MeshLeaf).Num(), MeshDependentsBefore,
            "A refused CreateAndAttachToUnrealMesh must leave no entity under its owner: the owner's lifetime dependents");

        _Driver.DestroyActor();
        if (IsFinished())
        { return; }

        FinishSuccess();
    }

    // A chain under a bare root named InPrefix_Root, with nodes InPrefix_N0.. down to the deepest depth; its last node.
    private FCk_Handle_Transform Make_DeepestNode(FCk_Handle InOwner, const FString& InPrefix)
    {
        auto Parent = utils_transform::Add(utils_entity_lifetime::Request_CreateEntity(InOwner), FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(Parent))
        {
            FinishFailure("Failed to add the Transform feature to a bare root");
            return Parent;
        }
        utils_handle::Set_DebugName(Parent, FName(f"{InPrefix}_Root"));

        for (int32 Depth = 0; Depth < utils_scene_node::Get_MaxDepth(); ++Depth)
        {
            auto Node = utils_scene_node::Create(Parent, FTransform::Identity);
            if (ck::Is_NOT_Valid(Node))
            {
                FinishFailure(f"Create of {InPrefix}_N{Depth} must succeed");
                return FCk_Handle_Transform();
            }

            utils_handle::Set_DebugName(Node, FName(f"{InPrefix}_N{Depth}"));
            Parent = Node.As_Transform();
        }

        return Parent;
    }
}

class ACk_AutoTest_SceneNode_Depth_CreateAndAttachToUnrealPastMaxDepthCreatesNothing_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_SceneNode_Depth_CreateAndAttachToUnrealPastMaxDepthCreatesNothing;
    default _TimeoutSeconds = 8.0f;

    // One error per refused create, each naming its own chain down to the would-be node.
    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_RequiredLogErrors() const
    {
        TArray<FString> Errors;
        Errors.Add(Get_Chain("DepthUnrealComponent"));
        Errors.Add(Get_Chain("DepthUnrealMesh"));
        return Errors;
    }

    private FString Get_Chain(const FString& InPrefix) const
    {
        auto Chain = f"Chain: [{InPrefix}_Root";
        for (int32 Depth = 0; Depth < utils_scene_node::Get_MaxDepth(); ++Depth)
        { Chain += f" > {InPrefix}_N{Depth}"; }
        Chain += " > (new scene node)]";
        return Chain;
    }
}
