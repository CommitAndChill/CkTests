#include "CkSelectionGym_WireValidation.h"

#include "CkSelectionGym_Wire.h"

namespace
{
    bool Finite(const FVector& V)
    {
        return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) &&
            FMath::IsFinite(V.Z);
    }

    bool Finite(const FQuat& Q)
    {
        return FMath::IsFinite(Q.X) && FMath::IsFinite(Q.Y) &&
            FMath::IsFinite(Q.Z) && FMath::IsFinite(Q.W);
    }

    bool FinitePose(const FTransform& Pose)
    {
        // Unreal's component getters run NaN diagnostics and can sanitize
        // malformed data. Reject before touching any individual component.
        if (Pose.ContainsNaN() || !Pose.IsValid()) { return false; }
        const FQuat Rotation = Pose.GetRotation();
        return Finite(Pose.GetTranslation()) && Finite(Pose.GetScale3D()) &&
            Finite(Rotation) && Rotation.IsNormalized();
    }

    const FCkSelectionGym_NodeRow* FindNode(
        const TMap<int64, const FCkSelectionGym_NodeRow*>& Nodes, int64 Key)
    {
        const auto* Found = Nodes.Find(Key);
        return Found != nullptr ? *Found : nullptr;
    }

    // All walks are iterative and bounded by the received node count.
    bool Acyclic(const TMap<int64, const FCkSelectionGym_NodeRow*>& Nodes,
        bool SceneGraph)
    {
        TMap<int64, uint8> Colors;
        for (const auto& Pair : Nodes)
        {
            TArray<int64> Path;
            int64 Current = Pair.Key;
            while (Current > 0)
            {
                const auto* Node = FindNode(Nodes, Current);
                if (Node == nullptr) { return false; }
                if (SceneGraph && !Node->HasSceneNode) { break; }
                const uint8 Color = Colors.FindRef(Current);
                if (Color == 2) { break; }
                if (Color == 1 || Path.Num() >= Nodes.Num()) { return false; }
                Colors.Add(Current, 1);
                Path.Add(Current);
                Current = SceneGraph ? Node->SceneNodeParentKey :
                    Node->LifetimeParentKey;
            }
            for (int64 Key : Path) { Colors.Add(Key, 2); }
        }
        return true;
    }

    bool ContextIsSelfOrAncestor(
        const TMap<int64, const FCkSelectionGym_NodeRow*>& Nodes,
        const FCkSelectionGym_NodeRow& Node)
    {
        if (Node.ContextRootKey == 0) { return true; }
        int64 Current = Node.Key;
        for (int32 Steps = 0; Steps <= Nodes.Num() && Current > 0; ++Steps)
        {
            if (Current == Node.ContextRootKey) { return true; }
            const auto* Ancestor = FindNode(Nodes, Current);
            if (Ancestor == nullptr) { return false; }
            Current = Ancestor->LifetimeParentKey;
        }
        return false;
    }
}

bool ck_selection_gym_wire::TryValidateActiveValue(
    const FCkSelectionGym_WireSnapshot& S, FString& OutReason)
{
    OutReason.Reset();
    const auto Fail = [&OutReason](const TCHAR* Why)
    {
        OutReason = Why;
        return false;
    };

    if (S.SchemaVersion != 1 || S.SourceGeneration <= 0 ||
        S.ScenarioGeneration <= 0 || S.Sequence <= 0 ||
        S.Lifecycle != ECkSelectionGym_WireLifecycle::Active ||
        S.Scenario < 0 || S.Scenario > 4 || !S.FailureReason.IsEmpty() ||
        S.ScenarioRootKey <= 0 || !Finite(S.SourceOrigin))
    { return Fail(TEXT("Invalid Active envelope")); }
    if (S.DeclaredNodeCount < 0 || S.DeclaredVisualCount < 0 ||
        S.DeclaredTimerCount < 0 || S.DeclaredIntegerCount < 0 ||
        S.DeclaredStateMachineCount < 0 ||
        S.DeclaredNodeCount != S.Nodes.Num() ||
        S.DeclaredVisualCount != S.Visuals.Num() ||
        S.DeclaredTimerCount != S.Timers.Num() ||
        S.DeclaredIntegerCount != S.Integers.Num() ||
        S.DeclaredStateMachineCount != S.StateMachines.Num() ||
        S.Nodes.IsEmpty())
    { return Fail(TEXT("Declared row counts do not describe one Active value")); }

    TMap<int64, const FCkSelectionGym_NodeRow*> Nodes;
    Nodes.Reserve(S.Nodes.Num());
    for (const auto& Node : S.Nodes)
    {
        if (Node.Key <= 0 || Nodes.Contains(Node.Key) ||
            Node.LifetimeParentKey < 0 ||
            Node.LifetimeParentKey == Node.Key ||
            Node.ContextRootKey < 0 ||
            !FinitePose(Node.WorldPose) ||
            !FinitePose(Node.SceneNodeLocalOffset) ||
            (Node.OwnContextRoot && Node.ContextRootKey != Node.Key))
        { return Fail(TEXT("Invalid or duplicate node value")); }
        if (Node.HasTransform)
        {
            if (Node.HasSceneNode)
            {
                if (Node.SceneNodeParentKey <= 0 ||
                    Node.SceneNodeParentKey == Node.Key)
                { return Fail(TEXT("Invalid SceneNode relationship")); }
            }
            else if (Node.SceneNodeParentKey != -1 ||
                !Node.SceneNodeLocalOffset.Equals(FTransform::Identity, 0.0))
            { return Fail(TEXT("Non-SceneNode has SceneNode data")); }
        }
        else if (Node.HasSceneNode || Node.SceneNodeParentKey != -1 ||
            !Node.WorldPose.Equals(FTransform::Identity, 0.0) ||
            !Node.SceneNodeLocalOffset.Equals(FTransform::Identity, 0.0))
        { return Fail(TEXT("Transformless node has transform data")); }
        Nodes.Add(Node.Key, &Node);
    }
    const auto* Root = FindNode(Nodes, S.ScenarioRootKey);
    if (Root == nullptr || Root->LifetimeParentKey != 0)
    { return Fail(TEXT("Missing direct-transient scenario root")); }

    for (const auto& Node : S.Nodes)
    {
        if (Node.LifetimeParentKey == 0 &&
            Node.Key != S.ScenarioRootKey && S.Scenario != 0)
        { return Fail(TEXT("Unexpected direct transient root")); }
        if (Node.LifetimeParentKey > 0 &&
            FindNode(Nodes, Node.LifetimeParentKey) == nullptr)
        { return Fail(TEXT("Missing lifetime parent")); }
        if (Node.ContextRootKey > 0 &&
            FindNode(Nodes, Node.ContextRootKey) == nullptr)
        { return Fail(TEXT("Missing context root")); }
        if (Node.HasSceneNode)
        {
            const auto* Parent = FindNode(Nodes, Node.SceneNodeParentKey);
            if (Parent == nullptr || !Parent->HasTransform)
            { return Fail(TEXT("Missing transformed SceneNode parent")); }
        }
    }
    if (!Acyclic(Nodes, false) || !Acyclic(Nodes, true))
    { return Fail(TEXT("Cycle in node topology")); }
    for (const auto& Node : S.Nodes)
    {
        if (!ContextIsSelfOrAncestor(Nodes, Node))
        { return Fail(TEXT("Context root is not a lifetime ancestor")); }
    }

    TSet<int64> TimerKeys;
    for (const auto& Timer : S.Timers)
    {
        if (Timer.OwnerNodeKey <= 0 || Timer.TimerKey <= 0 ||
            FindNode(Nodes, Timer.OwnerNodeKey) == nullptr ||
            FindNode(Nodes, Timer.TimerKey) == nullptr ||
            TimerKeys.Contains(Timer.TimerKey) ||
            (Timer.State != ECk_Timer_State::Paused &&
             Timer.State != ECk_Timer_State::Running))
        { return Fail(TEXT("Invalid timer fact")); }
        TimerKeys.Add(Timer.TimerKey);
    }

    TSet<int64> IntegerKeys;
    for (const auto& Integer : S.Integers)
    {
        if (Integer.OwnerNodeKey <= 0 || Integer.AttributeKey <= 0 ||
            FindNode(Nodes, Integer.OwnerNodeKey) == nullptr ||
            FindNode(Nodes, Integer.AttributeKey) == nullptr ||
            IntegerKeys.Contains(Integer.AttributeKey))
        { return Fail(TEXT("Invalid integer-attribute fact")); }
        IntegerKeys.Add(Integer.AttributeKey);
    }

    TSet<int64> StateMachineKeys;
    for (const auto& Machine : S.StateMachines)
    {
        if (Machine.OwnerNodeKey <= 0 || Machine.StateMachineKey <= 0 ||
            FindNode(Nodes, Machine.OwnerNodeKey) == nullptr ||
            FindNode(Nodes, Machine.StateMachineKey) == nullptr ||
            StateMachineKeys.Contains(Machine.StateMachineKey) ||
            Machine.Chain.IsEmpty() || Machine.Chain[0].Depth != 0 ||
            (!Machine.HasDebugHistory && !Machine.History.IsEmpty()))
        { return Fail(TEXT("Invalid state-machine fact")); }
        StateMachineKeys.Add(Machine.StateMachineKey);
        for (int32 I = 0; I < Machine.Chain.Num(); ++I)
        {
            const auto& Link = Machine.Chain[I];
            if ((Link.RunStatus != ECk_SmRunStatus::Stopped &&
                 Link.RunStatus != ECk_SmRunStatus::Running &&
                 Link.RunStatus != ECk_SmRunStatus::Paused) ||
                Link.Depth > I ||
                (I > 0 && (Link.Depth < 1 ||
                    Link.Depth > Machine.Chain[I - 1].Depth + 1)))
            { return Fail(TEXT("Invalid state-machine chain")); }
        }
    }

    TSet<int64> VisualKeys;
    for (const auto& Visual : S.Visuals)
    {
        if (Visual.VisualKey <= 0 || VisualKeys.Contains(Visual.VisualKey) ||
            !FinitePose(Visual.ActorPose) ||
            !FinitePose(Visual.MeshRelativePose) ||
            !FinitePose(Visual.LabelRelativePose) || Visual.Mesh.IsNull() ||
            Visual.ObjectChannel.GetValue() >= ECC_OverlapAll_Deprecated ||
            (Visual.VisibilityResponse != ECR_Ignore &&
             Visual.VisibilityResponse != ECR_Overlap &&
             Visual.VisibilityResponse != ECR_Block))
        { return Fail(TEXT("Invalid visual value")); }
        VisualKeys.Add(Visual.VisualKey);
        switch (Visual.Kind)
        {
        case ECkSelectionGym_VisualKind::LabelMarker:
            if (Visual.NodeKey <= 0 ||
                FindNode(Nodes, Visual.NodeKey) == nullptr ||
                Visual.LabelText.IsEmpty() ||
                !FMath::IsFinite(Visual.LabelWorldSize) ||
                Visual.LabelWorldSize <= 0.0f ||
                Visual.ActorCollisionEnabled ||
                Visual.MeshCollisionMode != ECollisionEnabled::NoCollision)
            { return Fail(TEXT("Invalid label marker")); }
            break;
        case ECkSelectionGym_VisualKind::OcclusionWall:
            if (Visual.NodeKey != 0 || !Visual.ActorCollisionEnabled ||
                Visual.MeshCollisionMode != ECollisionEnabled::QueryAndPhysics ||
                Visual.ObjectChannel != ECC_WorldStatic ||
                Visual.VisibilityResponse != ECR_Block ||
                !Visual.LabelText.IsEmpty() || Visual.LabelWorldSize != 0.0f)
            { return Fail(TEXT("Invalid visibility wall")); }
            break;
        default:
            return Fail(TEXT("Unknown visual kind"));
        }
    }
    return true;
}
