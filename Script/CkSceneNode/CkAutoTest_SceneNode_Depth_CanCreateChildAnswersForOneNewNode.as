// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: CAN CREATE CHILD ANSWERS FOR ONE NEW NODE
//============================================================================
//
// Get_CanCreateChild is the quiet question a caller asks before creating one
// node. A plain Transform that is not a layered scene node is a valid parent
// (its child lands at depth 0), the node one above the deepest depth still
// has room for a child, and the node at the deepest depth has none. An
// INVALID parent has no room either.
//============================================================================

class UCk_AutoTest_SceneNode_Depth_CanCreateChildAnswersForOneNewNode : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();

        const auto MaxDepth = utils_scene_node::Get_MaxDepth();

        auto Root = utils_transform::Add(utils_entity_lifetime::Request_CreateEntity(InHandle), FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(Root))
        {
            FinishFailure("Failed to add the Transform feature to the bare root");
            return;
        }

        Assert_True(utils_scene_node::Get_CanCreateChild(Root),
            "A plain Transform that is not a layered scene node can take a child at depth 0");
        Assert_False(utils_scene_node::Get_CanCreateChild(FCk_Handle_Transform()),
            "An INVALID parent can take no child: Create refuses it");

        auto Nodes = TArray<FCk_Handle_Transform>();
        auto Parent = Root;
        for (int32 Depth = 0; Depth < MaxDepth; ++Depth)
        {
            auto Node = utils_scene_node::Create(Parent, FTransform::Identity);
            if (ck::Is_NOT_Valid(Node))
            {
                FinishFailure(f"Create of the node at depth [{Depth}] must succeed");
                return;
            }

            Parent = Node.As_Transform();
            Nodes.Add(Parent);
        }

        Assert_True(utils_scene_node::Get_CanCreateChild(Nodes[MaxDepth - 2]),
            f"The node at depth [{MaxDepth - 2}] can take a child at the deepest depth [{MaxDepth - 1}]");
        Assert_False(utils_scene_node::Get_CanCreateChild(Nodes[MaxDepth - 1]),
            f"The node at the deepest depth [{MaxDepth - 1}] can take no child");
        if (IsFinished())
        { return; }

        FinishSuccess();
    }
}
