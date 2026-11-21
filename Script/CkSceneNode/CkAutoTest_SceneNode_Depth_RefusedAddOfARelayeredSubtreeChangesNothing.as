// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: A REFUSED ADD OF A RE-LAYERED SUBTREE CHANGES NOTHING
//============================================================================
//
// X holds a two-node chain (X1, X2). A detach re-layers that chain from 0
// below X (X1 at 0, X2 at 1). Attached under the node at depth MaxDepth - 3, X
// would sit at MaxDepth - 2 and X2 at MaxDepth, one past the deepest depth:
// the node's depth plus its subtree's height decides, whatever layers the
// subtree holds now. The Add is refused, logs the chain once, and X stays a
// plain root with X1 and X2 at 0 and 1.
//============================================================================

class UCk_AutoTest_SceneNode_Depth_RefusedAddOfARelayeredSubtreeChangesNothing : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();

        const auto MaxDepth = utils_scene_node::Get_MaxDepth();

        auto ChainLeaf = Make_Root(InHandle, n"DepthRelayered_RootA");
        for (int32 Depth = 0; Depth <= MaxDepth - 3; ++Depth)
        { ChainLeaf = Make_Node(ChainLeaf, FName(f"DepthRelayered_A{Depth}")); }

        auto SubtreeRoot = Make_Node(Make_Root(InHandle, n"DepthRelayered_RootX"), n"DepthRelayered_X");
        auto X1 = Make_Node(SubtreeRoot, n"DepthRelayered_X1");
        auto X2 = Make_Node(X1, n"DepthRelayered_X2");
        if (IsFinished())
        { return; }

        utils_scene_node::Request_Detach(SubtreeRoot.As_SceneNode());
        Assert_True(Get_ReadDepth(X1) == 0 && Get_ReadDepth(X2) == 1,
            f"The detach must re-layer X1 and X2 from 0 (got [{Get_ReadDepth(X1)}], [{Get_ReadDepth(X2)}])");
        Assert_False(utils_scene_node::Get_CanAttach(SubtreeRoot, ChainLeaf),
            "Get_CanAttach must answer no: X2 would land one past the deepest depth");

        auto Attached = utils_scene_node::Add(SubtreeRoot, ChainLeaf, FTransform::Identity);
        Assert_True(ck::Is_NOT_Valid(Attached), "Add must refuse the attach");
        Assert_False(utils_scene_node::Has(SubtreeRoot), "A refused Add must leave X a plain root");
        Assert_True(Get_ReadDepth(X1) == 0 && Get_ReadDepth(X2) == 1,
            f"A refused Add must leave X1 and X2 at 0 and 1 (got [{Get_ReadDepth(X1)}], [{Get_ReadDepth(X2)}])");
        Assert_True(FCk_Handle(utils_scene_node::Get_Parent(X1.As_SceneNode())) == FCk_Handle(SubtreeRoot),
            "A refused Add must leave X1's parent X");
        if (IsFinished())
        { return; }

        FinishSuccess();
    }

    private int32 Get_ReadDepth(const FCk_Handle_Transform& InNode) const
    {
        const auto Depth = utils_scene_node::Get_Depth(InNode);
        return Depth.IsSet() ? Depth.GetValue() : -1;
    }

    private FCk_Handle_Transform Make_Root(FCk_Handle InOwner, FName InName)
    {
        auto Root = utils_transform::Add(utils_entity_lifetime::Request_CreateEntity(InOwner), FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(Root))
        {
            FinishFailure(f"Failed to add the Transform feature to [{InName}]");
            return Root;
        }

        utils_handle::Set_DebugName(Root, InName);
        return Root;
    }

    private FCk_Handle_Transform Make_Node(FCk_Handle_Transform InParent, FName InName)
    {
        if (ck::Is_NOT_Valid(InParent))
        { return InParent; }

        auto Node = utils_scene_node::Create(InParent, FTransform::Identity);
        if (ck::Is_NOT_Valid(Node))
        {
            FinishFailure(f"Create of [{InName}] must succeed while building the test hierarchy");
            return FCk_Handle_Transform();
        }

        utils_handle::Set_DebugName(Node, InName);
        return Node.As_Transform();
    }
}

class ACk_AutoTest_SceneNode_Depth_RefusedAddOfARelayeredSubtreeChangesNothing_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_SceneNode_Depth_RefusedAddOfARelayeredSubtreeChangesNothing;
    default _TimeoutSeconds = 5.0f;

    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_RequiredLogErrors() const
    {
        auto Chain = FString("Chain: [DepthRelayered_RootA");
        for (int32 Depth = 0; Depth <= utils_scene_node::Get_MaxDepth() - 3; ++Depth)
        { Chain += f" > DepthRelayered_A{Depth}"; }
        Chain += " > DepthRelayered_X > DepthRelayered_X1 > DepthRelayered_X2]";

        TArray<FString> Errors;
        Errors.Add(Chain);
        return Errors;
    }
}
