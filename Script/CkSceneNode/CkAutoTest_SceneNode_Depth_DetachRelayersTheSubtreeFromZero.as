// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: A DETACH RE-LAYERS THE SUBTREE FROM ZERO
//============================================================================
//
// A node near the bottom of the depth range (depth MaxDepth - 4) holds a
// three-node chain below it (C1, C2, C3 at the three deepest depths). Detaching
// it leaves a plain Transform root holding that chain, so the chain's depths
// must start again from 0 under it: C1, C2, C3 read 0, 1, 2. A Create under C3
// then has room, and the chain still follows its root when the root moves.
//============================================================================

class UCk_AutoTest_SceneNode_Depth_DetachRelayersTheSubtreeFromZero : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;

    private const int32 SubtreeHeight = 3;
    private const float32 PositionToleranceCm = 1.0f;

    private FCk_Handle_Transform _Detached;
    private TArray<FCk_Handle_Transform> _Below;
    private FTransform _MovedRootWorld = FTransform(FRotator(0.0f, 30.0f, 0.0f), FVector(250.0f, -400.0f, 20.0f), FVector::OneVector);

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();

        const auto MaxDepth = utils_scene_node::Get_MaxDepth();
        const auto DetachedDepth = MaxDepth - 1 - SubtreeHeight;

        auto Root = utils_transform::Add(utils_entity_lifetime::Request_CreateEntity(InHandle), FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(Root))
        {
            FinishFailure("Failed to add the Transform feature to the bare root");
            return;
        }

        auto Parent = Root;
        for (int32 Depth = 0; Depth < MaxDepth; ++Depth)
        {
            auto Node = utils_scene_node::Create(Parent,
                FTransform(FRotator(0.0f, 5.0f, 0.0f), FVector(20.0f, 0.0f, 0.0f), FVector::OneVector));
            if (ck::Is_NOT_Valid(Node))
            {
                FinishFailure(f"Create of the node at depth [{Depth}] must succeed");
                return;
            }

            Parent = Node.As_Transform();
            if (Depth == DetachedDepth)
            { _Detached = Parent; }
            else if (Depth > DetachedDepth)
            { _Below.Add(Parent); }
        }

        utils_scene_node::Request_Detach(_Detached.As_SceneNode());
        Assert_False(utils_scene_node::Has(_Detached), "Request_Detach must leave the detached node a plain Transform");

        for (int32 Index = 0; Index < _Below.Num(); ++Index)
        {
            const auto Depth = utils_scene_node::Get_Depth(_Below[Index]);
            const auto ReadDepth = Depth.IsSet() ? Depth.GetValue() : -1;
            Assert_True(ReadDepth == Index,
                f"After the detach, the node {Index + 1} below the detached node must sit at depth [{Index}] under its now plain root (got [{ReadDepth}], -1 for none)");
        }
        if (IsFinished())
        { return; }

        auto Grandchild = utils_scene_node::Create(_Below.Last(), FTransform::Identity);
        Assert_True(ck::IsValid(Grandchild), "A Create under the leaf of the detached chain must have room");
        if (IsFinished())
        { return; }
        _Below.Add(Grandchild.As_Transform());

        utils_transform::Request_SetTransform(_Detached, _MovedRootWorld);
        WaitUntil(n"Check_ChainFollows", n"OnChainFollows", 120);
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
    private void Check_ChainFollows(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_transform::Get_EntityCurrentLocation(_Detached).Equals(_MovedRootWorld.GetLocation(), PositionToleranceCm) &&
            Get_FirstUncomposedIndex() == -1);
    }

    UFUNCTION()
    private void OnChainFollows(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished())
        { return; }

        Assert_True(Get_FirstUncomposedIndex() == -1, "Every node below the detached root must follow it once it moves");
        if (IsFinished())
        { return; }

        FinishSuccess();
    }
}
