// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: A REFUSED RE-PARENT NAMES THE LONGEST BRANCH
//============================================================================
//
// X has two branches: a short one made first (S1) and a long one
// (L1..L<LongBranchLength>). Attached under the deepest node of a chain A
// (A0..A<ChainLength - 1>), only the long branch would reach Get_MaxDepth(). The refusal's error must name the chain
// through the long branch down to its leaf, not the first branch it met.
//============================================================================

namespace ck_scene_node_depth_branch
{
    const int32 ChainLength = 7;
    const int32 LongBranchLength = 9;
}

class UCk_AutoTest_SceneNode_Depth_ReparentPastMaxDepthNamesTheLongestBranch : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();

        const auto MaxDepth = utils_scene_node::Get_MaxDepth();
        if (ck_scene_node_depth_branch::ChainLength + ck_scene_node_depth_branch::LongBranchLength < MaxDepth ||
            ck_scene_node_depth_branch::ChainLength + 1 >= MaxDepth)
        {
            FinishFailure(f"The test geometry needs only the long branch to pass the maximum depth [{MaxDepth}]");
            return;
        }

        auto ChainLeaf = Make_Root(InHandle, n"DepthBranch_RootA");
        for (int32 Index = 0; Index < ck_scene_node_depth_branch::ChainLength; ++Index)
        { ChainLeaf = Make_Node(ChainLeaf, FName(f"DepthBranch_A{Index}")); }

        auto SubtreeRoot = Make_Node(Make_Root(InHandle, n"DepthBranch_RootX"), n"DepthBranch_X");
        Make_Node(SubtreeRoot, n"DepthBranch_S1");

        auto LongLeaf = SubtreeRoot;
        for (int32 Index = 1; Index <= ck_scene_node_depth_branch::LongBranchLength; ++Index)
        { LongLeaf = Make_Node(LongLeaf, FName(f"DepthBranch_L{Index}")); }

        if (IsFinished())
        { return; }

        Assert_False(utils_scene_node::Get_CanAttach(SubtreeRoot, ChainLeaf), "Get_CanAttach must answer no: the long branch does not fit");

        utils_scene_node::Request_Detach(SubtreeRoot.As_SceneNode());
        auto Attached = utils_scene_node::Add(SubtreeRoot, ChainLeaf, FTransform::Identity);
        Assert_True(ck::Is_NOT_Valid(Attached), "Add must refuse an attach that puts the long branch past the maximum depth");
        if (IsFinished())
        { return; }

        FinishSuccess();
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

class ACk_AutoTest_SceneNode_Depth_ReparentPastMaxDepthNamesTheLongestBranch_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_SceneNode_Depth_ReparentPastMaxDepthNamesTheLongestBranch;
    default _TimeoutSeconds = 5.0f;

    // The chain runs through the long branch to its leaf; a chain through S1 would not match.
    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_RequiredLogErrors() const
    {
        auto Chain = FString("Chain: [DepthBranch_RootA");
        for (int32 Index = 0; Index < ck_scene_node_depth_branch::ChainLength; ++Index)
        { Chain += f" > DepthBranch_A{Index}"; }
        Chain += " > DepthBranch_X";
        for (int32 Index = 1; Index <= ck_scene_node_depth_branch::LongBranchLength; ++Index)
        { Chain += f" > DepthBranch_L{Index}"; }
        Chain += "]";

        TArray<FString> Errors;
        Errors.Add(Chain);
        return Errors;
    }
}
