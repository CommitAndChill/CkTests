// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: A RE-PARENT PAST THE MAXIMUM DEPTH CHANGES NOTHING
//============================================================================
//
// Re-parenting is Request_Detach then Add. A subtree X (X1..X<SubtreeHeight>
// below X) is detached and attached under the deepest node of a chain A
// (A0..A<ChainLength - 1>). X itself would fit at depth ChainLength, but its
// deepest node would land at ChainLength + SubtreeHeight, at or past
// Get_MaxDepth().
// Get_CanAttach says so before the detach, and the attach must be refused whole:
//
//   - Add returns an invalid handle and X stays detached;
//   - no node of the subtree changes its parent or the depth the detach gave it
//     (X1..X9 at 0..8 under the plain root X);
//   - one error names the chain from A's root to X9;
//   - X is still a working root: moving it moves every node below it.
//============================================================================

namespace ck_scene_node_depth_reparent
{
    // The subtree's depth once attached runs from ChainLength to ChainLength + SubtreeHeight.
    const int32 ChainLength = 7;
    const int32 SubtreeHeight = 9;
}

class UCk_AutoTest_SceneNode_Depth_ReparentPastMaxDepthChangesNothing : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 8.0f;

    private const int32 ChainLength = ck_scene_node_depth_reparent::ChainLength;
    private const int32 SubtreeHeight = ck_scene_node_depth_reparent::SubtreeHeight;
    private const float32 PositionToleranceCm = 1.0f;

    private FCk_Handle_Transform _ChainLeaf;
    private FCk_Handle_Transform _SubtreeRoot;
    private TArray<FCk_Handle_Transform> _Below;
    private TArray<FCk_Handle> _ParentsBefore;
    private FTransform _MovedSubtreeRootWorld = FTransform(FRotator(0.0f, 50.0f, 0.0f), FVector(-300.0f, 820.0f, 40.0f), FVector::OneVector);

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();

        const auto MaxDepth = utils_scene_node::Get_MaxDepth();
        if (ChainLength >= MaxDepth || ChainLength + SubtreeHeight < MaxDepth || SubtreeHeight >= MaxDepth)
        {
            FinishFailure(f"The test geometry needs ChainLength < MaxDepth <= ChainLength + SubtreeHeight and SubtreeHeight < MaxDepth (MaxDepth [{MaxDepth}])");
            return;
        }

        auto ChainRoot = Make_Root(InHandle, FTransform::Identity, n"DepthReparent_RootA");
        auto SubtreeOwner = Make_Root(InHandle, FTransform(FRotator::ZeroRotator, FVector(0.0f, 500.0f, 0.0f), FVector::OneVector), n"DepthReparent_RootX");
        if (ck::Is_NOT_Valid(ChainRoot) || ck::Is_NOT_Valid(SubtreeOwner))
        {
            FinishFailure("Failed to add the Transform feature to the bare roots");
            return;
        }

        _ChainLeaf = ChainRoot;
        for (int32 Index = 0; Index < ChainLength; ++Index)
        {
            _ChainLeaf = Make_Node(_ChainLeaf, FName(f"DepthReparent_A{Index}"));
            if (ck::Is_NOT_Valid(_ChainLeaf))
            { return; }
        }

        _SubtreeRoot = Make_Node(SubtreeOwner, n"DepthReparent_X");
        if (ck::Is_NOT_Valid(_SubtreeRoot))
        { return; }

        auto Parent = _SubtreeRoot;
        for (int32 Index = 1; Index <= SubtreeHeight; ++Index)
        {
            Parent = Make_Node(Parent, FName(f"DepthReparent_X{Index}"));
            if (ck::Is_NOT_Valid(Parent))
            { return; }
            _Below.Add(Parent);
        }

        WaitUntil(n"Check_SubtreeComposed", n"OnBuilt", 120);
    }

    private FCk_Handle_Transform Make_Root(FCk_Handle InOwner, const FTransform& InWorld, FName InName)
    {
        auto Entity = utils_entity_lifetime::Request_CreateEntity(InOwner);
        auto Root = utils_transform::Add(Entity, InWorld, ECk_Replication::DoesNotReplicate);
        if (ck::IsValid(Root))
        { utils_handle::Set_DebugName(Root, InName); }
        return Root;
    }

    private FCk_Handle_Transform Make_Node(FCk_Handle_Transform InParent, FName InName)
    {
        const auto Local = FTransform(FRotator(0.0f, 9.0f, 0.0f), FVector(30.0f, 0.0f, 4.0f), FVector::OneVector);
        auto Node = utils_scene_node::Create(InParent, Local);
        if (ck::Is_NOT_Valid(Node))
        {
            FinishFailure(f"Create of [{InName}] must succeed while building the test hierarchy");
            return FCk_Handle_Transform();
        }

        utils_handle::Set_DebugName(Node, InName);
        return Node.As_Transform();
    }

    UFUNCTION()
    private void Check_SubtreeComposed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(Get_FirstUncomposedIndex() == -1);
    }

    // The index into _Below of the first node not at its offset composed onto its parent's current world; -1 when
    // every node is.
    private int32 Get_FirstUncomposedIndex() const
    {
        for (int32 Index = 0; Index < _Below.Num(); ++Index)
        {
            auto Node = _Below[Index].As_SceneNode();
            const auto ParentWorld = utils_transform::Get_EntityCurrentTransform(utils_scene_node::Get_Parent(Node));
            const auto Expected = utils_scene_node::Get_Offset(Node) * ParentWorld;
            if (!utils_transform::Get_EntityCurrentLocation(_Below[Index]).Equals(Expected.GetLocation(), PositionToleranceCm))
            { return Index; }
        }

        return -1;
    }

    UFUNCTION()
    private void OnBuilt(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished())
        { return; }

        for (const auto& Node : _Below)
        { _ParentsBefore.Add(FCk_Handle(utils_scene_node::Get_Parent(Node.As_SceneNode()))); }

        Assert_False(utils_scene_node::Get_CanAttach(_SubtreeRoot, _ChainLeaf),
            "Get_CanAttach must answer no before the detach: the subtree does not fit under the chain's leaf");

        auto SubtreeRootNode = _SubtreeRoot.As_SceneNode();
        utils_scene_node::Request_Detach(SubtreeRootNode);
        Assert_False(utils_scene_node::Has(_SubtreeRoot), "Request_Detach must leave X a plain Transform");
        if (IsFinished())
        { return; }

        auto Attached = utils_scene_node::Add(_SubtreeRoot, _ChainLeaf, FTransform::Identity);

        Assert_True(ck::Is_NOT_Valid(Attached),
            f"Add must refuse an attach that puts DepthReparent_X{SubtreeHeight} at depth [{ChainLength + SubtreeHeight}], past the maximum depth [{utils_scene_node::Get_MaxDepth()}]");
        Assert_False(utils_scene_node::Has(_SubtreeRoot), "A refused Add must leave X detached");

        for (int32 Index = 0; Index < _Below.Num(); ++Index)
        {
            auto Node = _Below[Index].As_SceneNode();
            Assert_True(FCk_Handle(utils_scene_node::Get_Parent(Node)) == _ParentsBefore[Index],
                f"A refused Add must leave the parent of DepthReparent_X{Index + 1} unchanged");

            const auto Depth = utils_scene_node::Get_Depth(Node);
            Assert_True(Depth.IsSet() && Depth.GetValue() == Index,
                f"A refused Add must leave DepthReparent_X{Index + 1} at the depth the detach gave it, [{Index}]");
        }
        if (IsFinished())
        { return; }

        utils_transform::Request_SetTransform(_SubtreeRoot, _MovedSubtreeRootWorld);
        WaitUntil(n"Check_SubtreeFollowsItsMovedRoot", n"OnSubtreeFollowsItsMovedRoot", 120);
    }

    UFUNCTION()
    private void Check_SubtreeFollowsItsMovedRoot(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_transform::Get_EntityCurrentLocation(_SubtreeRoot).Equals(_MovedSubtreeRootWorld.GetLocation(), PositionToleranceCm) &&
            Get_FirstUncomposedIndex() == -1);
    }

    UFUNCTION()
    private void OnSubtreeFollowsItsMovedRoot(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished())
        { return; }

        const auto Uncomposed = Get_FirstUncomposedIndex();
        Assert_True(Uncomposed == -1, f"Every node below X must follow X once it moves; DepthReparent_X{Uncomposed + 1} did not");
        if (IsFinished())
        { return; }

        FinishSuccess();
    }
}

class ACk_AutoTest_SceneNode_Depth_ReparentPastMaxDepthChangesNothing_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_SceneNode_Depth_ReparentPastMaxDepthChangesNothing;
    default _TimeoutSeconds = 8.0f;

    // The refusal is loud: one error naming the chain from the new parent's root to the node that would overflow.
    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_RequiredLogErrors() const
    {
        auto Chain = FString("Chain: [DepthReparent_RootA");
        for (int32 Index = 0; Index < ck_scene_node_depth_reparent::ChainLength; ++Index)
        { Chain += f" > DepthReparent_A{Index}"; }
        Chain += " > DepthReparent_X";
        for (int32 Index = 1; Index <= ck_scene_node_depth_reparent::SubtreeHeight; ++Index)
        { Chain += f" > DepthReparent_X{Index}"; }
        Chain += "]";

        TArray<FString> Errors;
        Errors.Add(Chain);
        return Errors;
    }
}
