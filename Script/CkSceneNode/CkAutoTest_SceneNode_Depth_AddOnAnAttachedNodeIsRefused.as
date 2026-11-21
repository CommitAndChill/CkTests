// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: AN ADD ON A NODE THAT IS ALREADY ATTACHED IS REFUSED
//============================================================================
//
// Re-parenting is Request_Detach then Add. An Add on a node that is still a
// scene node skips the detach: it is a caller error, refused with one ensure
// that tells the caller to detach first. The node keeps its depth and its
// parent.
//============================================================================

class UCk_AutoTest_SceneNode_Depth_AddOnAnAttachedNodeIsRefused : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();

        auto FirstRoot = utils_transform::Add(utils_entity_lifetime::Request_CreateEntity(InHandle), FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        auto SecondRoot = utils_transform::Add(utils_entity_lifetime::Request_CreateEntity(InHandle), FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(FirstRoot) || ck::Is_NOT_Valid(SecondRoot))
        {
            FinishFailure("Failed to add the Transform feature to the bare roots");
            return;
        }

        auto Parent = utils_scene_node::Create(FirstRoot, FTransform::Identity);
        auto Node = utils_scene_node::Create(Parent.As_Transform(), FTransform::Identity);
        auto NewParent = utils_scene_node::Create(SecondRoot, FTransform::Identity);
        if (ck::Is_NOT_Valid(Parent) || ck::Is_NOT_Valid(Node) || ck::Is_NOT_Valid(NewParent))
        {
            FinishFailure("Failed to create the test scene nodes");
            return;
        }
        utils_handle::Set_DebugName(Node, n"DepthAddAttached_Node");

        // A deeper parent, so a second layer tag would differ from the first.
        auto DeeperParent = utils_scene_node::Create(NewParent.As_Transform(), FTransform::Identity);

        auto Readded = utils_scene_node::Add(Node.As_Transform(), DeeperParent.As_Transform(), FTransform::Identity);

        Assert_True(ck::Is_NOT_Valid(Readded), "Add on a node that is still a scene node must return an invalid handle");

        const auto Depth = utils_scene_node::Get_Depth(Node);
        const auto ReadDepth = Depth.IsSet() ? Depth.GetValue() : -1;
        Assert_True(ReadDepth == 1, f"The refused node must keep its depth [1] (got [{ReadDepth}], -1 for none)");
        Assert_True(FCk_Handle(utils_scene_node::Get_Parent(Node)) == FCk_Handle(Parent),
            "The refused node must keep its parent");
        Assert_True(utils_scene_node::Get_CanCreateChild(Node.As_Transform()),
            "The refused node must still compose as a node at depth 1 (room below it)");
        if (IsFinished())
        { return; }

        FinishSuccess();
    }
}

class ACk_AutoTest_SceneNode_Depth_AddOnAnAttachedNodeIsRefused_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_SceneNode_Depth_AddOnAnAttachedNodeIsRefused;
    default _TimeoutSeconds = 5.0f;

    // The refusal is one ensure naming the node and telling the caller to detach first.
    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_RequiredLogErrors() const
    {
        TArray<FString> Errors;
        Errors.Add("SceneNode Add refused: [");
        return Errors;
    }
}
