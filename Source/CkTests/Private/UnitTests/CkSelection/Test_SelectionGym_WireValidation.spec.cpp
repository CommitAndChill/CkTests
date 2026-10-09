#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "CkSelectionGym_Wire.h"
#include "CkSelectionGym_WireValidation.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "UObject/SoftObjectPath.h"
#include <limits>

namespace ck_selection_wire_test
{
    FCkSelectionGym_WireSnapshot Value()
    {
        FCkSelectionGym_WireSnapshot S;
        S.SchemaVersion = 1;
        S.SourceGeneration = 1;
        S.ScenarioGeneration = 1;
        S.Sequence = 1;
        S.Lifecycle = ECkSelectionGym_WireLifecycle::Active;
        S.Scenario = 0;
        S.ScenarioRootKey = 1;

        FCkSelectionGym_NodeRow Root;
        Root.Key = 1;
        Root.LifetimeParentKey = 0;
        Root.ContextRootKey = 0;
        S.Nodes.Add(Root);

        FCkSelectionGym_NodeRow Candidate;
        Candidate.Key = 2;
        Candidate.LifetimeParentKey = 1;
        Candidate.ContextRootKey = 2;
        Candidate.OwnContextRoot = true;
        Candidate.HasTransform = true;
        Candidate.WorldPose = FTransform(FVector(10.0, 0.0, 0.0));
        S.Nodes.Add(Candidate);

        FCkSelectionGym_VisualRow Marker;
        Marker.VisualKey = 1;
        Marker.NodeKey = 2;
        Marker.Kind = ECkSelectionGym_VisualKind::LabelMarker;
        Marker.Mesh = TSoftObjectPtr<UStaticMesh>(
            FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
        Marker.LabelText = TEXT("Candidate");
        Marker.LabelWorldSize = 32.0f;
        S.Visuals.Add(Marker);

        S.DeclaredNodeCount = S.Nodes.Num();
        S.DeclaredVisualCount = S.Visuals.Num();
        S.DeclaredTimerCount = 0;
        S.DeclaredIntegerCount = 0;
        S.DeclaredStateMachineCount = 0;
        return S;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkSelectionWireActiveValues,
    "Ck.Selection.Gym.Wire.ActiveValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSelectionWireActiveValues::RunTest(const FString&)
{
    using namespace ck_selection_wire_test;
    FString Reason;
    FCkSelectionGym_WireSnapshot Default;
    TestTrue(TEXT("default wire rejects with a reason"),
        !ck_selection_gym_wire::TryValidateActiveValue(Default, Reason) &&
        !Reason.IsEmpty());

    const auto Base = Value();
    TestTrue(TEXT("internally well-formed Active value accepts"),
        ck_selection_gym_wire::TryValidateActiveValue(Base, Reason) &&
        Reason.IsEmpty());
    const auto Reject = [&](FCkSelectionGym_WireSnapshot S)
    {
        Reason.Reset();
        return !ck_selection_gym_wire::TryValidateActiveValue(S, Reason) &&
            !Reason.IsEmpty();
    };

    auto Broken = Base;
    Broken.Lifecycle = ECkSelectionGym_WireLifecycle::Failed;
    TestTrue(TEXT("terminal lifecycle cannot present an Active prefix"), Reject(Broken));
    Broken = Base;
    Broken.DeclaredNodeCount = 1;
    TestTrue(TEXT("declared transport count mismatch rejects"), Reject(Broken));
    Broken = Base;
    Broken.ScenarioRootKey = 2;
    TestTrue(TEXT("scenario root must be directly transient"), Reject(Broken));
    Broken = Base;
    Broken.Nodes[1].Key = 1;
    TestTrue(TEXT("duplicate node identity rejects"), Reject(Broken));

    auto MultipleRoots = Base;
    FCkSelectionGym_NodeRow Independent;
    Independent.Key = 3;
    Independent.LifetimeParentKey = 0;
    Independent.ContextRootKey = 3;
    Independent.OwnContextRoot = true;
    MultipleRoots.Nodes.Add(Independent);
    MultipleRoots.DeclaredNodeCount = MultipleRoots.Nodes.Num();
    TestTrue(TEXT("scenario zero allows separately retained transient roots"),
        ck_selection_gym_wire::TryValidateActiveValue(MultipleRoots, Reason));
    MultipleRoots.Scenario = 1;
    TestTrue(TEXT("other scenarios reject extra transient roots"),
        Reject(MultipleRoots));

    auto Cycle = Base;
    FCkSelectionGym_NodeRow A, B;
    A.Key = 3; A.LifetimeParentKey = 4; A.ContextRootKey = 0;
    B.Key = 4; B.LifetimeParentKey = 3; B.ContextRootKey = 0;
    Cycle.Nodes.Add(A); Cycle.Nodes.Add(B);
    Cycle.DeclaredNodeCount = Cycle.Nodes.Num();
    TestTrue(TEXT("disconnected lifetime cycle rejects"), Reject(Cycle));

    auto Mixed = Base;
    FCkSelectionGym_NodeRow Ordinary;
    Ordinary.Key = 3;
    Ordinary.LifetimeParentKey = 2;
    Ordinary.ContextRootKey = 2;
    Mixed.Nodes.Add(Ordinary); // A legitimate transformless descendant.
    FCkSelectionGym_NodeRow TimerNode;
    TimerNode.Key = 4; TimerNode.LifetimeParentKey = 2;
    TimerNode.ContextRootKey = 2;
    Mixed.Nodes.Add(TimerNode);
    FCkSelectionGym_TimerFact Timer;
    Timer.OwnerNodeKey = 2; Timer.TimerKey = 4;
    Timer.State = ECk_Timer_State::Running;
    Mixed.Timers.Add(Timer);
    FCkSelectionGym_NodeRow SmNode;
    SmNode.Key = 5; SmNode.LifetimeParentKey = 2;
    SmNode.ContextRootKey = 2;
    Mixed.Nodes.Add(SmNode);
    FCkSelectionGym_StateMachineFact Sm;
    Sm.OwnerNodeKey = 5; Sm.StateMachineKey = 5;
    FCkSelectionGym_StateChainFact State;
    State.Depth = 0; State.RunStatus = ECk_SmRunStatus::Stopped;
    Sm.Chain.Add(State);
    Mixed.StateMachines.Add(Sm);
    Mixed.DeclaredNodeCount = Mixed.Nodes.Num();
    Mixed.DeclaredTimerCount = Mixed.Timers.Num();
    Mixed.DeclaredStateMachineCount = Mixed.StateMachines.Num();
    TestTrue(TEXT("transformless and provider-owned descendants are representable"),
        ck_selection_gym_wire::TryValidateActiveValue(Mixed, Reason));
    TestTrue(TEXT("absent current state class remains a valid raw provider fact"),
        Mixed.StateMachines[0].Chain[0].StateClassName.IsEmpty() &&
        ck_selection_gym_wire::TryValidateActiveValue(Mixed, Reason));
    Broken = Mixed;
    Broken.Timers[0].State = static_cast<ECk_Timer_State>(255);
    TestTrue(TEXT("unknown timer enum rejects"), Reject(Broken));
    Broken = Mixed;
    Broken.StateMachines[0].Chain.Reset();
    TestTrue(TEXT("present state machine requires its root observation"), Reject(Broken));
    Broken = Mixed;
    Broken.StateMachines[0].Chain.Add(FCkSelectionGym_StateChainFact{});
    Broken.StateMachines[0].Chain[1].Depth = 2;
    TestTrue(TEXT("state-machine depth jump rejects"), Reject(Broken));
    Broken = Mixed;
    Broken.SourceOrigin.X = std::numeric_limits<double>::quiet_NaN();
    TestTrue(TEXT("nonfinite source origin rejects"), Reject(Broken));

    auto Wall = Base;
    Wall.Scenario = 3;
    FCkSelectionGym_VisualRow Obstacle;
    Obstacle.VisualKey = 2;
    Obstacle.Kind = ECkSelectionGym_VisualKind::OcclusionWall;
    Obstacle.Mesh = Wall.Visuals[0].Mesh;
    Obstacle.ActorCollisionEnabled = true;
    Obstacle.MeshCollisionMode = ECollisionEnabled::QueryAndPhysics;
    Obstacle.ObjectChannel = ECC_WorldStatic;
    Obstacle.VisibilityResponse = ECR_Block;
    Wall.Visuals.Add(Obstacle);
    Wall.DeclaredVisualCount = Wall.Visuals.Num();
    TestTrue(TEXT("value-shaped visibility wall accepts without claiming a physical hit"),
        ck_selection_gym_wire::TryValidateActiveValue(Wall, Reason));
    Wall.Visuals[1].VisibilityResponse = ECR_Ignore;
    TestTrue(TEXT("wall that cannot block Visibility rejects"), Reject(Wall));
    return true;
}
#endif
