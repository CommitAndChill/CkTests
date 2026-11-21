// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: CAN ATTACH ANSWERS FOR THE WHOLE SUBTREE
//============================================================================
//
// A subtree X (X1..X3 below X) and a chain A deep enough to hold it. Under the
// chain node at depth MaxDepth - 5, X's deepest node lands exactly on the
// deepest depth (MaxDepth - 1): Get_CanAttach says yes. Under the node one
// deeper it would land one past: Get_CanAttach says no. Both answers hold while
// X is still attached and again after its detach. An INVALID node or
// parent answers no. The accepted Add then
// re-layers every node of X below its new parent, and the deepest one follows
// the chain's root when it moves.
//============================================================================

class UCk_AutoTest_SceneNode_Depth_CanAttachAnswersForTheWholeSubtree : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;

    private const int32 SubtreeHeight = 3;
    private const float32 PositionToleranceCm = 1.0f;

    private FCk_Handle_Transform _ChainRoot;
    private FCk_Handle_Transform _SubtreeRoot;
    private TArray<FCk_Handle_Transform> _Moved;
    private FTransform _MovedChainRootWorld = FTransform(FRotator(0.0f, -40.0f, 0.0f), FVector(-120.0f, 350.0f, 15.0f), FVector::OneVector);

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();

        const auto MaxDepth = utils_scene_node::Get_MaxDepth();
        const auto FitDepth = MaxDepth - 2 - SubtreeHeight;

        _ChainRoot = Make_Root(InHandle);
        auto SubtreeOwner = Make_Root(InHandle);
        if (ck::Is_NOT_Valid(_ChainRoot) || ck::Is_NOT_Valid(SubtreeOwner))
        {
            FinishFailure("Failed to add the Transform feature to the bare roots");
            return;
        }

        auto FitParent = FCk_Handle_Transform();
        auto PastParent = FCk_Handle_Transform();
        auto Parent = _ChainRoot;
        for (int32 Depth = 0; Depth <= FitDepth + 1; ++Depth)
        {
            Parent = Make_Node(Parent);
            if (ck::Is_NOT_Valid(Parent))
            { return; }

            if (Depth == FitDepth)
            { FitParent = Parent; }
            else if (Depth == FitDepth + 1)
            { PastParent = Parent; }
        }

        _SubtreeRoot = Make_Node(SubtreeOwner);
        if (ck::Is_NOT_Valid(_SubtreeRoot))
        { return; }
        _Moved.Add(_SubtreeRoot);

        Parent = _SubtreeRoot;
        for (int32 Index = 0; Index < SubtreeHeight; ++Index)
        {
            Parent = Make_Node(Parent);
            if (ck::Is_NOT_Valid(Parent))
            { return; }
            _Moved.Add(Parent);
        }

        Assert_True(utils_scene_node::Get_CanAttach(_SubtreeRoot, FitParent),
            f"While attached: X under the node at depth [{FitDepth}] puts its deepest node at [{MaxDepth - 1}] and fits");
        Assert_False(utils_scene_node::Get_CanAttach(_SubtreeRoot, PastParent),
            f"While attached: X under the node at depth [{FitDepth + 1}] puts its deepest node at [{MaxDepth}] and does not fit");

        utils_scene_node::Request_Detach(_SubtreeRoot.As_SceneNode());

        Assert_False(utils_scene_node::Get_CanAttach(_SubtreeRoot, FCk_Handle_Transform()),
            "Get_CanAttach under an INVALID parent must answer no: Add refuses it");
        Assert_False(utils_scene_node::Get_CanAttach(FCk_Handle_Transform(), FitParent),
            "Get_CanAttach of an INVALID node must answer no: Add refuses it");

        Assert_True(utils_scene_node::Get_CanAttach(_SubtreeRoot, FitParent),
            f"Detached: X under the node at depth [{FitDepth}] fits");
        Assert_False(utils_scene_node::Get_CanAttach(_SubtreeRoot, PastParent),
            f"Detached: X under the node at depth [{FitDepth + 1}] does not fit");
        if (IsFinished())
        { return; }

        auto Attached = utils_scene_node::Add(_SubtreeRoot, FitParent, FTransform(FRotator::ZeroRotator, FVector(10.0f, 0.0f, 0.0f), FVector::OneVector));
        Assert_True(ck::IsValid(Attached), "The Add Get_CanAttach accepted must succeed");
        if (IsFinished())
        { return; }

        for (int32 Index = 0; Index < _Moved.Num(); ++Index)
        {
            const auto Depth = utils_scene_node::Get_Depth(_Moved[Index]);
            const auto ReadDepth = Depth.IsSet() ? Depth.GetValue() : -1;
            Assert_True(ReadDepth == FitDepth + 1 + Index,
                f"After the Add, the node {Index} of X must sit at depth [{FitDepth + 1 + Index}] (got [{ReadDepth}], -1 for none)");
        }
        if (IsFinished())
        { return; }

        utils_transform::Request_SetTransform(_ChainRoot, _MovedChainRootWorld);
        WaitUntil(n"Check_SubtreeFollows", n"OnSubtreeFollows", 120);
    }

    private FCk_Handle_Transform Make_Root(FCk_Handle InOwner)
    {
        return utils_transform::Add(utils_entity_lifetime::Request_CreateEntity(InOwner), FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
    }

    private FCk_Handle_Transform Make_Node(FCk_Handle_Transform InParent)
    {
        auto Node = utils_scene_node::Create(InParent,
            FTransform(FRotator(0.0f, 6.0f, 0.0f), FVector(15.0f, 0.0f, 2.0f), FVector::OneVector));
        if (ck::Is_NOT_Valid(Node))
        {
            FinishFailure("A Create while building the test hierarchy must succeed");
            return FCk_Handle_Transform();
        }

        return Node.As_Transform();
    }

    UFUNCTION()
    private void Check_SubtreeFollows(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(Get_FirstUncomposedIndex() == -1 &&
            utils_transform::Get_EntityCurrentLocation(_ChainRoot).Equals(_MovedChainRootWorld.GetLocation(), PositionToleranceCm));
    }

    // The index into _Moved of the first node not at its offset composed onto its parent's current world; -1 when
    // every node is.
    private int32 Get_FirstUncomposedIndex() const
    {
        for (int32 Index = 0; Index < _Moved.Num(); ++Index)
        {
            auto Node = _Moved[Index].As_SceneNode();
            const auto ParentWorld = utils_transform::Get_EntityCurrentTransform(utils_scene_node::Get_Parent(Node));
            const auto Expected = utils_scene_node::Get_Offset(Node) * ParentWorld;
            if (!utils_transform::Get_EntityCurrentLocation(_Moved[Index]).Equals(Expected.GetLocation(), PositionToleranceCm))
            { return Index; }
        }

        return -1;
    }

    UFUNCTION()
    private void OnSubtreeFollows(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished())
        { return; }

        Assert_True(Get_FirstUncomposedIndex() == -1, "The deepest node of X must follow the chain's root once it moves");
        if (IsFinished())
        { return; }

        FinishSuccess();
    }
}
