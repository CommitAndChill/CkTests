// Language=angelscript

//============================================================================
// CK HEX GRID - AUTOMATION TEST: A BOARD ON THE DEEPEST SCENE NODE IS REFUSED
//============================================================================
//
// A board's pivot is a scene node one layer below the board's owner. TryAdd on
// a scene node at the deepest depth has no layer left for the pivot, so it is
// refused with CapacityExceeded and composes nothing, quietly (the board asks
// Get_CanCreateChild first, so no depth error is logged). On the node one
// shallower the same TryAdd succeeds.
//============================================================================

class UCk_AutoTest_HexGrid_SceneNode_Depth_BoardUnderTheDeepestNodeIsRefused : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();

        const auto MaxDepth = utils_scene_node::Get_MaxDepth();

        auto Parent = utils_transform::Add(utils_entity_lifetime::Request_CreateEntity(InHandle), FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(Parent))
        {
            FinishFailure("Failed to add the Transform feature to the bare root");
            return;
        }

        auto OneAboveDeepest = FCk_Handle_Transform();
        for (int32 Depth = 0; Depth < MaxDepth; ++Depth)
        {
            auto Node = utils_scene_node::Create(Parent, FTransform::Identity);
            if (ck::Is_NOT_Valid(Node))
            {
                FinishFailure(f"Create of the node at depth [{Depth}] must succeed");
                return;
            }

            if (Depth == MaxDepth - 2)
            { OneAboveDeepest = Node.As_Transform(); }
            Parent = Node.As_Transform();
        }

        FCk_HexGrid_Spec Spec;

        FCk_Handle_HexGrid RefusedBoard;
        auto RefusedFailure = ECk_HexBoardFailure::None;
        const auto Refused = utils_hex_grid::TryAdd(Parent, Spec, RefusedBoard, RefusedFailure);
        Assert_False(Refused, "TryAdd on the deepest scene node must be refused");
        Assert_True(RefusedFailure == ECk_HexBoardFailure::CapacityExceeded,
            f"The refusal must be CapacityExceeded (got [{RefusedFailure}])");
        Assert_False(utils_hex_grid::Has(Parent), "A refused TryAdd must compose no board");

        FCk_Handle_HexGrid Board;
        auto Failure = ECk_HexBoardFailure::None;
        Assert_True(utils_hex_grid::TryAdd(OneAboveDeepest, Spec, Board, Failure),
            f"TryAdd on the node one above the deepest must succeed (failure [{Failure}])");
        if (IsFinished())
        { return; }

        FinishSuccess();
    }
}
