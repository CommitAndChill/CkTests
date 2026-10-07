#include "CkSceneNodeGym_NodeGraph.h"

#include "CkSceneNodeGym_EcsRoots.h"
#include "CkSceneNodeGym_ActorLedger.h"
#include "CkSceneNodeGym_QueueRuntime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkRecord/Record/CkRecord_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"

namespace CkSceneNodeNodeGraphPrivate
{
    constexpr int32 NodeCount = 11;
    constexpr int32 RootSlot[NodeCount] = {0, 1, 2, 2, 2, 2, 3, 3, 4, 4, 5};
    constexpr int32 ParentNode[NodeCount] = {
        INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE,
        INDEX_NONE, INDEX_NONE, 6, INDEX_NONE, 8, INDEX_NONE};

    bool Live(const UObject* Value)
    {
        return ck::IsValid(Value) &&
            !Value->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
    }

    FTransform Offset(int32 Slot)
    {
        FVector Position;
        float Scale = 1.0f;
        switch (Slot)
        {
        case 0: Position = FVector(150, 0, 0); Scale = .5f; break;
        case 1: Position = FVector(100, 0, 50); Scale = .5f; break;
        case 2: Position = FVector(120, 0, 0); Scale = .4f; break;
        case 3: Position = FVector(0, 120, 0); Scale = .4f; break;
        case 4: Position = FVector(-120, 0, 0); Scale = .4f; break;
        case 5: Position = FVector(0, -120, 0); Scale = .4f; break;
        case 6: Position = FVector(120, 0, 0); Scale = .7f; break;
        case 7: Position = FVector(100, 0, 0); Scale = .6f; break;
        case 8: Position = FVector(160, 0, 0); Scale = .6f; break;
        case 9: Position = FVector(80, 0, 0); Scale = .4f; break;
        case 10: Position = FVector(180, 0, 40); Scale = .5f; break;
        default: return FTransform::Identity;
        }
        return FTransform(FRotator::ZeroRotator, Position,
            FVector(Scale, Scale, Scale));
    }

    bool FreshChild(const FCk_Handle& Child, const FCk_Handle& Parent)
    {
        return Child != FCk_Handle{} && Parent != FCk_Handle{} &&
            ck::IsValid(Child) && ck::IsValid(Parent) &&
            Child.Get_RegistryView().Get_RegistryHandle() ==
                Parent.Get_RegistryView().Get_RegistryHandle() &&
            Child.Has<ck::FFragment_LifetimeOwner>() &&
            Child.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == Parent &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Child,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    bool IsNonreplicatedNode(const FCk_Handle& Node)
    {
        if (!Node.Has<ck::FFragment_Net_Params>() ||
            Node.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>())
        { return false; }
        const auto& Settings = Node.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings();
        const bool WantAuthority =
            Settings.Get_NetRole() == ECk_Net_EntityNetRole::Authority;
        const bool WantHost =
            Settings.Get_NetMode() == ECk_Net_NetModeType::Host ||
            Settings.Get_NetMode() == ECk_Net_NetModeType::ClientAndHost;
        const bool WantClient =
            Settings.Get_NetMode() == ECk_Net_NetModeType::Client ||
            Settings.Get_NetMode() == ECk_Net_NetModeType::ClientAndHost;
        return Settings.Get_Replication() == ECk_Replication::DoesNotReplicate &&
            Node.Has<ck::FTag_HasAuthority>() == WantAuthority &&
            Node.Has<ck::FTag_NetMode_IsHost>() == WantHost &&
            Node.Has<ck::FTag_NetMode_IsClient>() == WantClient;
    }
}

bool UCk_SceneNodeGym_NodeGraphRuntime_UE::HasContext(
    const UCk_SceneNodeGym_NodeGraph_UE* Graph)
{
    if (!IsInGameThread() || !CkSceneNodeNodeGraphPrivate::Live(Graph) || Graph->Roots.IsStale())
    { return false; }
    const auto* Roots = Graph->Roots.Get();
    return CkSceneNodeNodeGraphPrivate::Live(Roots) && Roots->NodeGraph == Graph &&
        Roots->bNodeGraphAttempted && Graph->Generation == Roots->Generation &&
        Graph->Slots.Num() == CkSceneNodeNodeGraphPrivate::NodeCount && !Graph->bFailed && !Graph->bUnproven &&
        UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(Roots);
}

void UCk_SceneNodeGym_NodeGraphRuntime_UE::Fail(
    UCk_SceneNodeGym_NodeGraph_UE* Graph, bool bOpaque)
{
    if (!IsInGameThread() || !CkSceneNodeNodeGraphPrivate::Live(Graph)) { return; }
    Graph->bFailed = true;
    Graph->bUnproven |= bOpaque;
    auto* Roots = Graph->Roots.Get();
    if (CkSceneNodeNodeGraphPrivate::Live(Roots))
    { UCk_SceneNodeGym_EcsRootsRuntime_UE::Fail(Roots, bOpaque); }
}

UCk_SceneNodeGym_NodeGraph_UE* UCk_SceneNodeGym_NodeGraphRuntime_UE::TryBegin(
    UCk_SceneNodeGym_EcsRoots_UE* Roots)
{
    if (!IsInGameThread() || !CkSceneNodeNodeGraphPrivate::Live(Roots) ||
        !UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(Roots) ||
        Roots->bNodeGraphAttempted || Roots->NodeGraph != nullptr)
    { return nullptr; }

    FCk_Handle RootHandles[6];
    for (int32 I = 0; I < 6; ++I)
    {
        if (!UCk_SceneNodeGym_EcsRootsRuntime_UE::TryGet_CompletedRoot(
                Roots, I, RootHandles[I]) ||
            !RootHandles[I].Has<ck::FFragment_Transform>() ||
            !UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(RootHandles[I]))
        { return nullptr; }
    }
    auto* const Ledger = Roots->Ledger.Get();
    auto* const Source = UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_Source(Ledger);
    if (!CkSceneNodeNodeGraphPrivate::Live(Source)) { return nullptr; }
    auto* const World = Source->GetWorld();
    AActor* AnchorActor = nullptr;
    if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
            Ledger, 16, AnchorActor)) { return nullptr; }
    if (!CkSceneNodeNodeGraphPrivate::Live(World) || !CkSceneNodeNodeGraphPrivate::Live(AnchorActor) ||
        AnchorActor->IsActorBeingDestroyed() || AnchorActor->GetWorld() != World)
    { return nullptr; }
    auto* const Anchor = Cast<UStaticMeshComponent>(AnchorActor->GetRootComponent());
    if (!CkSceneNodeNodeGraphPrivate::Live(Anchor) || !Anchor->IsRegistered() ||
        Anchor->GetOwner() != AnchorActor || Anchor->GetWorld() != World ||
        !RootHandles[5].Has<ck::FFragment_Transform_RootComponent>() ||
        RootHandles[5].Get<ck::FFragment_Transform_RootComponent>()
            .Get_RootComponent().Get() != Anchor)
    { return nullptr; }

    // No raw entity has been requested yet. Reserve the generation's single
    // attempt and retain the record before any allocation or observer call.
    Roots->bNodeGraphAttempted = true;
    auto* Graph = NewObject<UCk_SceneNodeGym_NodeGraph_UE>(Roots);
    if (!CkSceneNodeNodeGraphPrivate::Live(Graph))
    {
        UCk_SceneNodeGym_EcsRootsRuntime_UE::Fail(Roots, true);
        return nullptr;
    }
    Roots->NodeGraph = Graph;
    Graph->Roots = Roots;
    Graph->Anchor = Anchor;
    Graph->Generation = Roots->Generation;
    Graph->Slots.SetNum(CkSceneNodeNodeGraphPrivate::NodeCount);
    for (int32 I = 0; I < CkSceneNodeNodeGraphPrivate::NodeCount; ++I)
    {
        auto& Slot = Graph->Slots[I];
        Slot.RootSlot = CkSceneNodeNodeGraphPrivate::RootSlot[I];
        Slot.ParentNodeSlot = CkSceneNodeNodeGraphPrivate::ParentNode[I];
        Slot.InitialOffset = CkSceneNodeNodeGraphPrivate::Offset(I);
    }

    for (int32 I = 0; I < CkSceneNodeNodeGraphPrivate::NodeCount; ++I)
    {
        AActor* CurrentAnchorActor = nullptr;
        if (!HasContext(Graph) || Graph->Anchor.Get() != Anchor ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                Ledger, 16, CurrentAnchorActor) ||
            CurrentAnchorActor != AnchorActor ||
            !CkSceneNodeNodeGraphPrivate::Live(Anchor) || !Anchor->IsRegistered() ||
            AnchorActor->GetRootComponent() != Anchor)
        { Fail(Graph, false); return Graph; }
        auto& Slot = Graph->Slots[I];
        Slot.Parent = Slot.ParentNodeSlot == INDEX_NONE
            ? RootHandles[Slot.RootSlot]
            : Graph->Slots[Slot.ParentNodeSlot].Raw;
        if (!ck::IsValid(Slot.Parent) ||
            !Slot.Parent.Has<ck::FFragment_Transform>() ||
            !UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(Slot.Parent))
        { Fail(Graph, false); return Graph; }
        Slot.bAllocationAttempted = true;
        Slot.Raw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Slot.Parent);
        // The raw return is retained before any feature write or callback.
        if (!HasContext(Graph) || !CkSceneNodeNodeGraphPrivate::FreshChild(Slot.Raw, Slot.Parent))
        { Fail(Graph, true); return Graph; }
        FCk_Handle_Transform TypedParent = UCk_Utils_Transform_UE::Cast(Slot.Parent);
        FCk_Handle_SceneNode Composed;
        const bool Added = I == 10
            ? UCk_Utils_SceneNode_UE::TryAdd_AnchorToUnrealComponent(
                Slot.Raw, TypedParent, Anchor, Slot.InitialOffset, Composed)
            : UCk_Utils_SceneNode_UE::TryAdd_Anchor(
                Slot.Raw, TypedParent, Slot.InitialOffset, Composed);
        // TryAdd publishes a synchronous editor observer after its commit.
        if (!HasContext(Graph) || Graph->Anchor.Get() != Anchor ||
            AnchorActor->GetRootComponent() != Anchor ||
            !Added || !ck::IsValid(Slot.Raw) ||
            FCk_Handle(Composed) != Slot.Raw)
        { Fail(Graph, false); return Graph; }
        Slot.bComposed = true;
    }
    return Graph;
}

bool UCk_SceneNodeGym_NodeGraphRuntime_UE::HasCurrentTopology(
    const UCk_SceneNodeGym_NodeGraph_UE* Graph, bool bRequireDrained)
{
    if (!HasContext(Graph)) { return false; }
    const auto* Roots = Graph->Roots.Get();
    const auto* Ledger = Roots->Ledger.Get();
    AActor* Actor = nullptr;
    if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
            Ledger, 16, Actor) || !CkSceneNodeNodeGraphPrivate::Live(Actor) ||
        Actor->IsActorBeingDestroyed()) { return false; }
    auto* const Source = UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_Source(Ledger);
    if (!CkSceneNodeNodeGraphPrivate::Live(Source)) { return false; }
    auto* const World = Source->GetWorld();
    auto* const Anchor = Cast<UStaticMeshComponent>(Actor->GetRootComponent());
    if (!CkSceneNodeNodeGraphPrivate::Live(World) || !CkSceneNodeNodeGraphPrivate::Live(Anchor) || Graph->Anchor.IsStale() ||
        Graph->Anchor.Get() != Anchor || !Anchor->IsRegistered() ||
        Anchor->GetOwner() != Actor || Anchor->GetWorld() != World ||
        !Roots->Slots[5].Target.Has<ck::FFragment_Transform_RootComponent>() ||
        Roots->Slots[5].Target.Get<ck::FFragment_Transform_RootComponent>()
            .Get_RootComponent().Get() != Anchor)
    { return false; }

    FCk_Handle RootHandles[6];
    for (int32 I = 0; I < 6; ++I)
    {
        if (!UCk_SceneNodeGym_EcsRootsRuntime_UE::TryGet_CompletedRoot(
                Roots, I, RootHandles[I])) { return false; }
    }
    TArray<FCk_Handle> Known;
    Known.Reserve(17);
    for (const auto& Root : RootHandles) { Known.Add(Root); }
    for (const auto& Root : RootHandles)
    { if (Root.Has<ck::FFragment_Timer>()) { return false; } }
    for (int32 I = 0; I < CkSceneNodeNodeGraphPrivate::NodeCount; ++I)
    {
        const auto& Slot = Graph->Slots[I];
        if (Slot.RootSlot != CkSceneNodeNodeGraphPrivate::RootSlot[I] ||
            Slot.ParentNodeSlot != CkSceneNodeNodeGraphPrivate::ParentNode[I] ||
            !Slot.InitialOffset.Equals(CkSceneNodeNodeGraphPrivate::Offset(I), 0.0) ||
            !Slot.bAllocationAttempted || !Slot.bComposed)
        { return false; }
        const FCk_Handle Parent = CkSceneNodeNodeGraphPrivate::ParentNode[I] == INDEX_NONE
            ? RootHandles[CkSceneNodeNodeGraphPrivate::RootSlot[I]] : Graph->Slots[CkSceneNodeNodeGraphPrivate::ParentNode[I]].Raw;
        if (Slot.Parent != Parent || !CkSceneNodeNodeGraphPrivate::FreshChild(Slot.Raw, Parent) ||
            !Slot.Raw.Has_All<ck::FFragment_Transform,
                ck::FFragment_SceneNode, ck::SceneNodeParent,
                ck::FFragment_SceneNode_PropagationState>() ||
            (bRequireDrained &&
                Slot.Raw.Has_Any<ck::FTag_SceneNode_PropagationQueued,
                    ck::FFragment_SceneNode_Requests,
                    ck::FFragment_Transform_Requests>()) ||
            Slot.Raw.Has<ck::FFragment_Timer>() ||
            Slot.Raw.Has_Any<ck::FFragment_Transform_RootComponent,
                ck::FFragment_Transform_MeshSocket,
                ck::FTag_Transform_ExternallyDriven>() ||
            !Slot.Raw.Get<ck::FFragment_SceneNode>()
                .Get_RelativeTransform().IsValid() ||
            !CkSceneNodeNodeGraphPrivate::IsNonreplicatedNode(Slot.Raw) ||
            FCk_Handle(Slot.Raw.Get<ck::SceneNodeParent>().Get_Entity()) != Parent ||
            Slot.Raw.Has<ck::FFragment_SceneNode_UnrealAnchor>() != (I == 10) ||
            Slot.Raw.Has<ck::FTag_SceneNode_Layer0>() != (CkSceneNodeNodeGraphPrivate::ParentNode[I] == INDEX_NONE) ||
            Slot.Raw.Has<ck::FTag_SceneNode_Layer1>() != (CkSceneNodeNodeGraphPrivate::ParentNode[I] != INDEX_NONE) ||
            Slot.Raw.Has_Any<ck::FTag_SceneNode_Layer2,
                ck::FTag_SceneNode_Layer3, ck::FTag_SceneNode_Layer4,
                ck::FTag_SceneNode_Layer5, ck::FTag_SceneNode_Layer6,
                ck::FTag_SceneNode_Layer7, ck::FTag_SceneNode_Layer8,
                ck::FTag_SceneNode_Layer9>())
        { return false; }
        if (I == 10)
        {
            const auto& Link = Slot.Raw.Get<ck::FFragment_SceneNode_UnrealAnchor>();
            if (Link.Get_Component().Get() != Anchor || !Link.Get_Socket().IsNone())
            { return false; }
        }
        for (int32 J = 0; J < I; ++J)
        { if (Graph->Slots[J].Raw == Slot.Raw) { return false; } }
        Known.Add(Slot.Raw);
    }

    if (bRequireDrained &&
        !UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
            Known, Roots->WorldRoot)) { return false; }

    // Compare the actual forward SceneNode records with the frozen topology.
    for (int32 ParentIndex = -6; ParentIndex < CkSceneNodeNodeGraphPrivate::NodeCount; ++ParentIndex)
    {
        const FCk_Handle Parent = ParentIndex < 0
            ? RootHandles[ParentIndex + 6] : Graph->Slots[ParentIndex].Raw;
        // Get_Entries recursively follows record extensions; reject that
        // topology before invoking it on an externally changed parent.
        if (Parent.Has<ck::FFragment_RecordOfEntityExtensions>())
        { return false; }
        int32 Expected = 0;
        for (int32 I = 0; I < CkSceneNodeNodeGraphPrivate::NodeCount; ++I)
        { if (Graph->Slots[I].Parent == Parent) { ++Expected; } }
        if (Expected == 0)
        {
            if (Parent.Has<ck::FFragment_RecordOfSceneNodes>() &&
                !ck::FUtils_RecordOfSceneNodes::Get_Entries(Parent).IsEmpty())
            { return false; }
            continue;
        }
        if (!Parent.Has<ck::FFragment_RecordOfSceneNodes>()) { return false; }
        const auto Entries = ck::FUtils_RecordOfSceneNodes::Get_Entries(Parent);
        if (Entries.Num() != Expected) { return false; }
        for (const auto& Entry : Entries)
        {
            int32 Matches = 0;
            for (const auto& Slot : Graph->Slots)
            { if (Slot.Parent == Parent && FCk_Handle(Entry) == Slot.Raw) { ++Matches; } }
            if (Matches != 1) { return false; }
        }
    }
    return true;
}

bool UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(
    const UCk_SceneNodeGym_NodeGraph_UE* Graph)
{ return HasCurrentTopology(Graph, true); }

bool UCk_SceneNodeGym_NodeGraphRuntime_UE::TryGetMotionContext(
    const UCk_SceneNodeGym_NodeGraph_UE* Graph, bool bRequireDrained,
    UCk_SceneNodeGym_ActorLedger_UE*& OutLedger, FCk_Handle& OutWorldRoot,
    FCk_Handle (&OutRoots)[6], FCk_Handle (&OutNodes)[11])
{
    OutLedger = nullptr;
    OutWorldRoot = {};
    for (auto& Root : OutRoots) { Root = {}; }
    for (auto& Node : OutNodes) { Node = {}; }
    if (!HasCurrentTopology(Graph, bRequireDrained)) { return false; }
    const auto* Roots = Graph->Roots.Get();
    auto* const Ledger = Roots->Ledger.Get();
    if (!CkSceneNodeNodeGraphPrivate::Live(Ledger)) { return false; }
    // The single proof above has established exact six-root and eleven-node
    // construction identities. Copy them without another census.
    for (int32 I = 0; I < 6; ++I)
    { OutRoots[I] = Roots->Slots[I].Target; }
    for (int32 I = 0; I < 11; ++I)
    { OutNodes[I] = Graph->Slots[I].Raw; }
    OutLedger = Ledger;
    OutWorldRoot = Roots->WorldRoot;
    return true;
}

bool UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsFailed(
    const UCk_SceneNodeGym_NodeGraph_UE* Graph)
{ return IsInGameThread() && CkSceneNodeNodeGraphPrivate::Live(Graph) && Graph->bFailed; }

bool UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsUnproven(
    const UCk_SceneNodeGym_NodeGraph_UE* Graph)
{ return IsInGameThread() && CkSceneNodeNodeGraphPrivate::Live(Graph) && Graph->bUnproven; }

int32 UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_ComposedCount(
    const UCk_SceneNodeGym_NodeGraph_UE* Graph)
{
    if (!IsInGameThread() || !CkSceneNodeNodeGraphPrivate::Live(Graph)) { return 0; }
    int32 Result = 0;
    for (const auto& Slot : Graph->Slots) { Result += Slot.bComposed ? 1 : 0; }
    return Result;
}

bool UCk_SceneNodeGym_NodeGraphRuntime_UE::TryGet_CompletedNode(
    const UCk_SceneNodeGym_NodeGraph_UE* Graph, int32 Slot,
    FCk_Handle_SceneNode& OutNode)
{
    OutNode = {};
    if (!Get_IsConstructionComplete(Graph) || Slot < 0 || Slot >= CkSceneNodeNodeGraphPrivate::NodeCount)
    { return false; }
    OutNode = UCk_Utils_SceneNode_UE::Cast(Graph->Slots[Slot].Raw);
    return true;
}
