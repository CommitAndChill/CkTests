// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: A CREATE PAST THE MAXIMUM DEPTH CREATES NOTHING
//============================================================================
//
// Creates scene nodes, each under the last, until a Create is refused. Each
// accepted Create adds exactly one lifetime dependent to its owner at once (the
// positive control for the count the refusal is judged by). The refusal must
// come exactly at the maximum depth, return an invalid handle,
// leave no entity behind under the owner it was asked for, and log one error
// naming the whole chain from the root to the would-be node.
//============================================================================

class UCk_AutoTest_SceneNode_Depth_CreatePastMaxDepthCreatesNothing : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    // Bounds the loop when nothing refuses; past the maximum by a margin.
    private const int32 AttemptLimit = 64;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();

        const auto MaxDepth = utils_scene_node::Get_MaxDepth();

        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto Root = utils_transform::Add(RootEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(Root))
        {
            FinishFailure("Failed to add the Transform feature to the bare root");
            return;
        }
        utils_handle::Set_DebugName(Root, n"DepthCreate_Root");

        auto Parent = Root;
        auto Created = 0;
        auto DependentsBeforeRefusal = -1;
        auto DependentsAfterRefusal = -1;

        for (int32 Attempt = 0; Attempt < AttemptLimit; ++Attempt)
        {
            const auto DependentsBefore = utils_entity_lifetime::Get_LifetimeDependents(Parent).Num();
            const auto Local = FTransform(FRotator::ZeroRotator, FVector(25.0f, 0.0f, 0.0f), FVector::OneVector);
            auto Node = utils_scene_node::Create(Parent, Local);

            if (ck::Is_NOT_Valid(Node))
            {
                DependentsBeforeRefusal = DependentsBefore;
                DependentsAfterRefusal = utils_entity_lifetime::Get_LifetimeDependents(Parent).Num();
                break;
            }

            Assert_Equals_Int(utils_entity_lifetime::Get_LifetimeDependents(Parent).Num(), DependentsBefore + 1,
                f"An accepted Create must add exactly one lifetime dependent to its owner at once (depth [{Created}])");
            if (IsFinished())
            { return; }

            utils_handle::Set_DebugName(Node, FName(f"DepthCreate_N{Created}"));
            Parent = Node.As_Transform();
            ++Created;
        }

        Assert_Equals_Int(Created, MaxDepth,
            "Create must succeed at every depth below the maximum and be refused at the maximum: nodes created");
        Assert_True(DependentsBeforeRefusal >= 0, "A Create past the maximum depth must be refused");
        Assert_Equals_Int(DependentsAfterRefusal, DependentsBeforeRefusal,
            "A refused Create must leave no entity under the owner it was asked for: the owner's lifetime dependents");

        const auto LeafDepth = utils_scene_node::Get_Depth(Parent);
        Assert_True(LeafDepth.IsSet() && LeafDepth.GetValue() == MaxDepth - 1,
            f"The deepest node must sit at depth [{MaxDepth - 1}]");
        if (IsFinished())
        { return; }

        FinishSuccess();
    }
}

class ACk_AutoTest_SceneNode_Depth_CreatePastMaxDepthCreatesNothing_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_SceneNode_Depth_CreatePastMaxDepthCreatesNothing;
    default _TimeoutSeconds = 5.0f;

    // The refusal is loud: one error per refused Create, naming the chain from the root to the would-be node.
    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_RequiredLogErrors() const
    {
        auto Chain = FString("Chain: [DepthCreate_Root");
        for (int32 Depth = 0; Depth < utils_scene_node::Get_MaxDepth(); ++Depth)
        { Chain += f" > DepthCreate_N{Depth}"; }
        Chain += " > (new scene node)]";

        TArray<FString> Errors;
        Errors.Add(Chain);
        return Errors;
    }
}
