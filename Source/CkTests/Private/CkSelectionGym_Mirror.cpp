#include "CkSelectionGym_Mirror.h"

#if !UE_BUILD_SHIPPING

#include "CkSelectionGym_BankCensus.h"
#include "CkSelectionGym_WireValidation.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEntityDebugOverlay/Provider/CkDebugOverlay_ObservationBank.h"
#include "CkEntityDebugOverlay/Selection/CkDebugOverlay_SourceAdmission.h"
#include "CkEcs/ContextOwner/CkContextOwner_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Handle/CkHandle_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkLabel/CkLabel_Utils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/BodyInstance.h"

namespace ck
{
    enum class ESelectionMirrorPhase : uint8
    { Staging, Installed, HoldingRetiring, RetiringLeaf, RetiringAll, AwaitingNew, Failed };

    struct FFragment_SelectionGym_Mirror
    {
        CK_GENERATED_BODY(FFragment_SelectionGym_Mirror);
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AGameStateBase> GameState;
        TWeakObjectPtr<AActor> SpawnOwnerIdentity;
        FCk_Handle WorldRoot;
        FCkSelectionGym_WireSnapshot Pending;
        FCkSelectionGym_WireSnapshot Installed;
        TMap<int64, FCk_Handle> Nodes;
        TMap<int64, TWeakObjectPtr<AActor>> Visuals;
        TArray<FCk_Handle> DirectRoots;
        TArray<FCk_Handle> FrozenAll;
        bool HasPendingActive = false;
        FCk_Handle RetiringLeaf;
        TWeakObjectPtr<AActor> RetiringVisual;
        int64 RetiringLeafKey = 0;
        int64 RetiringVisualKey = 0;
        TSubclassOf<AActor> MarkerClass;
        TSubclassOf<AActor> WallClass;
        ESelectionMirrorPhase Phase = ESelectionMirrorPhase::Staging;
        int64 AdmissionGeneration = 0;
        int64 LastEnvelopeSourceGeneration = 0;
        int64 LastEnvelopeScenarioGeneration = 0;
        int64 LastEnvelopeSequence = 0;
        FCkSelectionGym_WireSnapshot AcceptedFailedEnvelope;
        bool HasAcceptedFailedEnvelope = false;
        bool ScopeBegun = false;
        double Deadline = 0.0;
        FString Failure;
    };
}

namespace
{
    using FState = ck::FFragment_SelectionGym_Mirror;

    bool Allocated(const FCk_Handle& H)
    { return H.IsRegistryValid() && H.Get_RegistryView().IsValid(H.Get_Entity()); }

    bool Live(const FCk_Handle& H)
    {
        return Allocated(H) && ck::IsValid(H) &&
            !H.Has_Any<ck::FTag_DestroyEntity_Initiate,
                ck::FTag_DestroyEntity_EndPlay, ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>();
    }

    bool ExactClientWorldRoot(UWorld* World, FCk_Handle& OutWorldRoot)
    {
        OutWorldRoot = {};
        if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() ||
            World->GetNetMode() != NM_Client ||
            World->HasAnyFlags(RF_ClassDefaultObject | RF_BeginDestroyed | RF_FinishDestroyed) ||
            World->bIsTearingDown || World->IsBeingCleanedUp())
        { return false; }
        auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!Live(Root) || !Root.Has<TWeakObjectPtr<UWorld>>() ||
            Root.Get<TWeakObjectPtr<UWorld>>().Get() != World ||
            FCk_Handle{Root.Get_RegistryView().Get_TransientEntity(),
                Root.Get_RegistryView().Get_RegistryHandle()} != Root)
        { return false; }
        OutWorldRoot = Root;
        return true;
    }

    bool Context(UWorld* World, AGameStateBase* GS, FCk_Handle& OutWorldRoot)
    {
        return ExactClientWorldRoot(World, OutWorldRoot) &&
            IsValid(GS) && !GS->IsActorBeingDestroyed() &&
            GS->GetWorld() == World && World->GetGameState() == GS;
    }

    bool Bound(const FState& S, UWorld* World, AGameStateBase* GS,
        const FCk_Handle& WorldRoot)
    {
        return S.World.Get() == World && S.GameState.Get() == GS &&
            S.WorldRoot == WorldRoot && S.Phase != ck::ESelectionMirrorPhase::Failed;
    }

    bool SameRegistry(const FCk_Handle& A, const FCk_Handle& B)
    {
        return Allocated(A) && Allocated(B) &&
            A.Get_RegistryView().Get_RegistryHandle() ==
                B.Get_RegistryView().Get_RegistryHandle();
    }

    bool Direct(const FCk_Handle& Child, const FCk_Handle& Parent,
        const FCk_Handle& WorldRoot)
    {
        return Live(Child) && Live(Parent) && SameRegistry(Child, WorldRoot) &&
            SameRegistry(Parent, WorldRoot) &&
            Child.Has<ck::FFragment_LifetimeOwner>() &&
            Child.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == Parent;
    }

    bool FailValue(FString& Why, const TCHAR* Message)
    { Why = Message; return false; }

    bool RetainedVisualActor(const FState& S, AActor* Actor);

    struct FMirrorMutationLease
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AGameStateBase> GameState;
        FCk_Handle WorldRoot;
        const FState* State = nullptr;
    };

    FMirrorMutationLease CaptureMirrorMutationLease(const FState& S)
    { return {S.World, S.GameState, S.WorldRoot, &S}; }

    FState* FindMirrorCapturedState(const FMirrorMutationLease& Lease)
    {
        auto* World = Lease.World.Get();
        FCk_Handle Exact;
        if (!ExactClientWorldRoot(World, Exact) || Exact != Lease.WorldRoot ||
            !Exact.Has<FState>())
        { return nullptr; }
        auto* State = &Exact.Get<FState>();
        return State == Lease.State && State->World.Get() == World &&
            State->WorldRoot == Exact ? State : nullptr;
    }

    const FState* ProveMirrorMutationLease(const FMirrorMutationLease& Lease)
    {
        auto* State = FindMirrorCapturedState(Lease);
        if (State == nullptr) { return nullptr; }
        auto* World = Lease.World.Get();
        auto* GS = Lease.GameState.Get();
        FCk_Handle Exact;
        return Context(World, GS, Exact) && Exact == Lease.WorldRoot &&
            Bound(*State, World, GS, Exact) ? State : nullptr;
    }

    void CloseFailed(const FMirrorMutationLease& Lease, const TCHAR* Message)
    {
        auto* State = FindMirrorCapturedState(Lease);
        if (State == nullptr) { return; }
        State->Phase = ck::ESelectionMirrorPhase::Failed;
        State->Failure = Message;
        const auto Visuals = State->Visuals;
        const auto SpawnOwner = State->SpawnOwnerIdentity;
        const bool ScopeBegun = State->ScopeBegun;
        const auto Generation = State->AdmissionGeneration;
        auto* World = Lease.World.Get();
        const auto Append = [&Lease](const TCHAR* Suffix)
        {
            if (auto* Current = FindMirrorCapturedState(Lease))
            { Current->Failure += Suffix; }
        };
        FCk_Handle Exact;
        if (ExactClientWorldRoot(World, Exact) && Exact == Lease.WorldRoot)
        {
            FString CloseReason;
            if (ScopeBegun)
            {
                if (!UCk_DebugOverlay_SourceAdmission_UE::TryClose(
                        World, Exact, Generation, CloseReason))
                {
                    if (auto* Current = FindMirrorCapturedState(Lease))
                    { Current->Failure += TEXT("; scope closure unproven: ") + CloseReason; }
                }
            }
        }
        else { Append(TEXT("; captured world unavailable")); return; }
        for (const auto& Pair : Visuals)
        {
            auto* Actor = Pair.Value.Get();
            if (!IsValid(Actor)) { continue; }
            FCk_Handle CurrentRoot;
            const auto ExactActor = [&Lease, &SpawnOwner, Actor, World,
                &CurrentRoot]()
            {
                return FindMirrorCapturedState(Lease) != nullptr &&
                    ExactClientWorldRoot(World, CurrentRoot) &&
                    CurrentRoot == Lease.WorldRoot && IsValid(Actor) &&
                    !Actor->IsActorBeingDestroyed() &&
                    Actor->GetWorld() == World &&
                    TWeakObjectPtr<AActor>(Actor->GetOwner()) == SpawnOwner;
            };
            if (!ExactActor())
            { Append(TEXT("; visual cleanup provenance changed")); break; }
            Actor->SetActorHiddenInGame(true);
            if (!ExactActor())
            { Append(TEXT("; visual hide callback changed provenance")); break; }
            Actor->SetActorEnableCollision(false);
            if (!ExactActor())
            { Append(TEXT("; visual collision callback changed provenance")); break; }
            Actor->Destroy();
            if (FindMirrorCapturedState(Lease) == nullptr)
            { return; }
        }
        // Retained receipts stay inert until exact-world teardown. A callback can add
        // descendants during composition, so blind recursive destruction is unsafe.
    }

    void CloseFailed(FState& S, const TCHAR* Message)
    { CloseFailed(CaptureMirrorMutationLease(S), Message); }

    bool FailClosedMirrorLease(const FMirrorMutationLease& Lease,
        const TCHAR* Message, FString& Why)
    {
        const FString FailureMessage(Message);
        CloseFailed(Lease, *FailureMessage);
        if (auto* State = FindMirrorCapturedState(Lease))
        { Why = State->Failure; }
        else { Why = FailureMessage + TEXT("; captured mirror state unavailable"); }
        return false;
    }

    void CloseChangedBinding(UWorld* World)
    {
        FCk_Handle Root;
        if (!ExactClientWorldRoot(World, Root) || !Root.Has<FState>())
        { return; }
        auto& S = Root.Get<FState>();
        if (S.World.Get() == World && S.WorldRoot == Root &&
            S.Phase != ck::ESelectionMirrorPhase::Failed &&
            (S.GameState.Get() == nullptr ||
             World->GetGameState() != S.GameState.Get()))
        { CloseFailed(S, TEXT("Exact GameState binding changed")); }
    }

    bool ProveFrozenContext(const FState& S, const TArray<FCk_Handle>& Frozen)
    {
        FCk_Handle Exact;
        if (S.Phase == ck::ESelectionMirrorPhase::Failed ||
            !Context(S.World.Get(), S.GameState.Get(), Exact) ||
            Exact != S.WorldRoot ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::
                TryProve_FrozenRetirementLedger(Frozen, Frozen, Exact))
        { return false; }
        for (const auto& Row : S.Installed.Nodes)
        {
            const auto* Child = S.Nodes.Find(Row.Key);
            if (Child == nullptr) { return false; }
            if (!Allocated(*Child)) { continue; }
            const FCk_Handle* Parent = Row.LifetimeParentKey == 0
                ? &S.WorldRoot : S.Nodes.Find(Row.LifetimeParentKey);
            if (Parent == nullptr || !Allocated(*Parent) ||
                !SameRegistry(*Child, Exact) ||
                !Child->Has<ck::FFragment_LifetimeOwner>() ||
                Child->Get<ck::FFragment_LifetimeOwner,
                    ck::IsValid_Policy_IncludePendingKill>().Get_Entity() != *Parent)
            { return false; }
        }
        return true;
    }

    bool RetainedVisualActor(const FState& S, AActor* Actor)
    {
        return IsValid(Actor) && Actor->GetWorld() == S.World.Get() &&
            TWeakObjectPtr<AActor>(Actor->GetOwner()) == S.SpawnOwnerIdentity;
    }

    bool RetainedVisualSet(const FState& S,
        const TSet<int64>* AlreadyDestroyed = nullptr)
    {
        for (const auto& Pair : S.Visuals)
        {
            if (AlreadyDestroyed != nullptr && AlreadyDestroyed->Contains(Pair.Key))
            { continue; }
            if (!RetainedVisualActor(S, Pair.Value.Get())) { return false; }
        }
        return true;
    }

    bool Preflight(const FCkSelectionGym_WireSnapshot& Frame,
        TSubclassOf<AActor> MarkerClass, TSubclassOf<AActor> WallClass,
        FString& Why)
    {
        if (!ck_selection_gym_wire::TryValidateActiveValue(Frame, Why))
        { return false; }
        if (!MarkerClass || !WallClass || MarkerClass == WallClass ||
            MarkerClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
            WallClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        { return FailValue(Why, TEXT("Exact marker/wall classes unavailable")); }
        TSet<int64> MachinesByOwner;
        TMap<int64, const FCkSelectionGym_NodeRow*> Nodes;
        for (const auto& Row : Frame.Nodes) { Nodes.Add(Row.Key, &Row); }
        for (const auto& Row : Frame.StateMachines)
        {
            if (MachinesByOwner.Contains(Row.OwnerNodeKey))
            { return FailValue(Why, TEXT("Multiple state-machine facts for one owner")); }
            MachinesByOwner.Add(Row.OwnerNodeKey);
        }
        for (const auto& Row : Frame.Nodes)
        {
            // The authored mixed hierarchy uses the fresh direct lifetime child
            // contract; a more general scene graph needs a separate adapter.
            if (Row.HasSceneNode && Row.SceneNodeParentKey != Row.LifetimeParentKey)
            { return FailValue(Why, TEXT("Unsupported non-direct SceneNode relation")); }
        }
        for (const auto& Row : Frame.Visuals)
        {
            if (Row.Mesh.LoadSynchronous() == nullptr ||
                (!Row.Material.IsNull() && Row.Material.LoadSynchronous() == nullptr))
            { return FailValue(Why, TEXT("Source visual asset unavailable")); }
        }
        return true;
    }

    FCk_Handle FindNode(const FState& S, int64 Key)
    {
        const FCk_Handle* Found = S.Nodes.Find(Key);
        return Found != nullptr ? *Found : FCk_Handle{};
    }

    // Re-prove the exact captured client context and every retained node before
    // the next callback-capable composition or motion write. A newly allocated
    // node need not have its context override yet; CompletedContextKeys does.
    bool ProveMirrorMutationNodes(const FMirrorMutationLease& Lease,
        const FCkSelectionGym_WireSnapshot& Frame,
        const TSet<int64>& CompletedContextKeys)
    {
        const auto* State = ProveMirrorMutationLease(Lease);
        if (State == nullptr || State->Nodes.Num() != Frame.Nodes.Num())
        { return false; }
        const auto& S = *State;
        const auto Exact = Lease.WorldRoot;
        auto* World = Lease.World.Get();
        TSet<int64> Seen;
        for (const auto& Row : Frame.Nodes)
        {
            if (Seen.Contains(Row.Key)) { return false; }
            Seen.Add(Row.Key);
            const auto H = FindNode(S, Row.Key);
            const auto Parent = Row.LifetimeParentKey == 0
                ? Exact : FindNode(S, Row.LifetimeParentKey);
            if (!Direct(H, Parent, Exact)) { return false; }
            if (CompletedContextKeys.Contains(Row.Key))
            {
                const auto ExpectedContext = Row.OwnContextRoot ? H
                    : Row.ContextRootKey > 0 ? FindNode(S, Row.ContextRootKey)
                    : Exact;
                if (!Live(ExpectedContext) ||
                    !UCk_Utils_ContextOwner_UE::Has(H) ||
                    UCk_Utils_ContextOwner_UE::Get_ContextOwner(H) != ExpectedContext)
                { return false; }
            }
        }
        if (Seen.Num() != S.Nodes.Num()) { return false; }
        for (const auto Key : CompletedContextKeys)
        { if (!Seen.Contains(Key)) { return false; } }
        TArray<FCk_Handle> Outliers;
        for (const auto& Row : Frame.Nodes)
        {
            if (Row.LifetimeParentKey == 0 && Row.Key != Frame.ScenarioRootKey)
            { Outliers.Add(FindNode(S, Row.Key)); }
        }
        TArray<FCk_Handle> Actual;
        FString CensusReason;
        if (!ck_selection_gym_census::TryCensusAllocatedBank(
                World, FindNode(S, Frame.ScenarioRootKey), Outliers,
                Actual, CensusReason) || Actual.Num() != S.Nodes.Num())
        { return false; }
        TSet<FCk_Handle> Expected;
        for (const auto& Pair : S.Nodes) { Expected.Add(Pair.Value); }
        for (const auto& H : Actual)
        { if (!Expected.Remove(H)) { return false; } }
        if (!Expected.IsEmpty()) { return false; }
        return true;
    }

    bool ProveMirrorMutationBankTopology(const FMirrorMutationLease& Lease,
        const FCkSelectionGym_WireSnapshot& ExpectedFrame)
    {
        const auto* State = ProveMirrorMutationLease(Lease);
        if (State == nullptr ||
            !FCkSelectionGym_WireSnapshot::StaticStruct()->
                CompareScriptStruct(&State->Pending, &ExpectedFrame, PPF_None))
        { return false; }
        TSet<int64> AllContexts;
        for (const auto& Row : ExpectedFrame.Nodes) { AllContexts.Add(Row.Key); }
        return ProveMirrorMutationNodes(Lease, ExpectedFrame, AllContexts);
    }

    bool ProveMirrorMutationActor(const FMirrorMutationLease& Lease,
        const FCkSelectionGym_VisualRow& Row, AActor* Actor)
    {
        const auto* State = ProveMirrorMutationLease(Lease);
        if (State == nullptr) { return false; }
        const auto& S = *State;
        auto* World = Lease.World.Get();
        auto* GS = Lease.GameState.Get();
        const auto* Retained = S.Visuals.Find(Row.VisualKey);
        const auto ExpectedClass = Row.Kind == ECkSelectionGym_VisualKind::LabelMarker
            ? S.MarkerClass.Get() : S.WallClass.Get();
        return IsValid(Actor) &&
            !Actor->IsActorBeingDestroyed() && Actor->GetWorld() == World &&
            Actor->GetClass() == ExpectedClass &&
            S.SpawnOwnerIdentity.Get() == GS && Actor->GetOwner() == GS &&
            TWeakObjectPtr<AActor>(Actor->GetOwner()) == S.SpawnOwnerIdentity &&
            Retained != nullptr && Retained->Get() == Actor;
    }

    bool ProveMirrorMutationVisuals(const FMirrorMutationLease& Lease,
        const FCkSelectionGym_WireSnapshot& Frame)
    {
        const auto* State = ProveMirrorMutationLease(Lease);
        if (State == nullptr) { return false; }
        const auto& S = *State;
        if (S.Visuals.Num() != Frame.Visuals.Num()) { return false; }
        for (const auto& Row : Frame.Visuals)
        {
            const auto* Retained = S.Visuals.Find(Row.VisualKey);
            if (Retained == nullptr ||
                !ProveMirrorMutationActor(Lease, Row, Retained->Get()))
            { return false; }
        }
        return true;
    }

    bool ComposeNode(FState& S, const FCkSelectionGym_NodeRow& InputRow,
        const TSet<int64>& CompletedContextKeys)
    {
        const auto Row = InputRow;
        const auto CompositionFrame = S.Pending;
        const auto Lease = CaptureMirrorMutationLease(S);
        const auto ProveCompositionNodes =
            [&Lease, &CompositionFrame](const TSet<int64>& ReadyKeys)
        {
            const auto* State = ProveMirrorMutationLease(Lease);
            return State != nullptr &&
                FCkSelectionGym_WireSnapshot::StaticStruct()->
                    CompareScriptStruct(&State->Pending, &CompositionFrame, PPF_None) &&
                ProveMirrorMutationNodes(Lease, CompositionFrame, ReadyKeys);
        };
        if (!ProveCompositionNodes(CompletedContextKeys))
        { return false; }
        auto H = FindNode(S, Row.Key);
        if (!Live(H)) { return false; }
        if (Row.HasSceneNode)
        {
            auto Parent = FindNode(S, Row.SceneNodeParentKey);
            if (!Live(Parent) || !Parent.Has<ck::FFragment_Transform>())
            { return false; }
            auto TypedParent = UCk_Utils_Transform_UE::Cast(Parent);
            FCk_Handle_SceneNode Node;
            if (!UCk_Utils_SceneNode_UE::TryAdd_Anchor(
                    H, TypedParent, Row.SceneNodeLocalOffset, Node) ||
                !ProveCompositionNodes(CompletedContextKeys) ||
                !Live(H) || !H.Has<ck::FFragment_SceneNode>() ||
                !H.Has<ck::SceneNodeParent>() ||
                !H.Has<ck::FFragment_Transform>())
            { return false; }
            if (UCk_Utils_SceneNode_UE::Get_Parent(
                    UCk_Utils_SceneNode_UE::Cast(H)) != TypedParent)
            { return false; }
            if (!ProveCompositionNodes(CompletedContextKeys))
            { return false; }
        }
        else if (Row.HasTransform)
        {
            auto Typed = UCk_Utils_Transform_UE::Add(
                H, Row.WorldPose, ECk_Replication::DoesNotReplicate);
            if (!ProveCompositionNodes(CompletedContextKeys) ||
                !Live(H) || !ck::IsValid(Typed) ||
                !H.Has<ck::FFragment_Transform>())
            { return false; }
            if (!ProveCompositionNodes(CompletedContextKeys))
            { return false; }
        }
        if (!ProveCompositionNodes(CompletedContextKeys))
        { return false; }
        if (Row.OwnContextRoot)
        { UCk_Utils_ContextOwner_UE::Request_OverrideToSelf(H, {}); }
        else if (Row.ContextRootKey > 0)
        {
            const auto Context = FindNode(S, Row.ContextRootKey);
            if (!Live(Context)) { return false; }
            UCk_Utils_ContextOwner_UE::Request_Override(H, Context, {});
        }
        else
        { UCk_Utils_ContextOwner_UE::Request_Override(H, S.WorldRoot, {}); }
        auto ContextReady = CompletedContextKeys;
        ContextReady.Add(Row.Key);
        if (!ProveCompositionNodes(ContextReady) ||
            !UCk_Utils_ContextOwner_UE::Has(H))
        { return false; }
        const auto ExpectedContext = Row.ContextRootKey > 0
            ? FindNode(S, Row.ContextRootKey) : S.WorldRoot;
        if (UCk_Utils_ContextOwner_UE::Get_ContextOwner(H) != ExpectedContext)
        { return false; }
        if (Row.DebugName != NAME_None)
        {
            if (!ProveCompositionNodes(ContextReady))
            { return false; }
            UCk_Utils_Handle_UE::Set_DebugName(H, Row.DebugName);
            if (!ProveCompositionNodes(ContextReady) ||
                UCk_Utils_Handle_UE::Get_DebugName(H) != Row.DebugName)
            { return false; }
        }
        if (Row.RootLabel.IsValid())
        {
            if (!ProveCompositionNodes(ContextReady))
            { return false; }
            UCk_Utils_GameplayLabel_UE::Add(H, Row.RootLabel);
            if (!ProveCompositionNodes(ContextReady) ||
                !UCk_Utils_GameplayLabel_UE::Has(H) ||
                UCk_Utils_GameplayLabel_UE::Get_Label(H) != Row.RootLabel)
            { return false; }
        }
        return true;
    }

    bool ComposeVisual(FState& S, const FCkSelectionGym_VisualRow& InputRow,
        TSubclassOf<AActor> MarkerClass, TSubclassOf<AActor> WallClass)
    {
        const auto Row = InputRow;
        const auto ExpectedFrame = S.Pending;
        const auto Lease = CaptureMirrorMutationLease(S);
        auto* World = Lease.World.Get();
        auto* GS = Lease.GameState.Get();
        FCk_Handle Root;
        if (!Context(World, GS, Root) || !Bound(S, World, GS, Root) ||
            !ProveMirrorMutationBankTopology(Lease, ExpectedFrame) ||
            S.SpawnOwnerIdentity.Get() != GS || S.Visuals.Contains(Row.VisualKey))
        { return false; }
        const auto Class = Row.Kind == ECkSelectionGym_VisualKind::LabelMarker
            ? MarkerClass : WallClass;
        auto* Actor = World->SpawnActorDeferred<AActor>(Class.Get(),
            Row.ActorPose, GS, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!IsValid(Actor)) { return false; }
        // If world teardown removed this exact state, the world owns actor
        // disposal. Do not write a receipt into a freed or replacement fragment.
        auto* CurrentState = FindMirrorCapturedState(Lease);
        if (CurrentState == nullptr) { return false; }
        CurrentState->Visuals.Add(Row.VisualKey, Actor); // first return retained before construction
        if (!ProveMirrorMutationLease(Lease)) { return false; }
        const TWeakObjectPtr<AActor> ActorIdentity(Actor);
        auto ExactActor = [&]() -> bool
        {
            if (!ProveMirrorMutationBankTopology(Lease, ExpectedFrame))
            { return false; }
            const auto* Current = ProveMirrorMutationLease(Lease);
            if (Current == nullptr) { return false; }
            const auto* Retained = Current->Visuals.Find(Row.VisualKey);
            return Current->SpawnOwnerIdentity.Get() == GS &&
                Retained != nullptr && Retained->Get() == Actor &&
                ActorIdentity.Get() == Actor &&
                IsValid(Actor) && !Actor->IsActorBeingDestroyed() &&
                Actor->GetWorld() == World && Actor->GetOwner() == GS &&
                TWeakObjectPtr<AActor>(Actor->GetOwner()).
                    HasSameIndexAndSerialNumber(Current->SpawnOwnerIdentity) &&
                Actor->GetClass() == Class.Get();
        };
        if (!ExactActor()) { return false; }
        Actor->SetActorHiddenInGame(true);
        if (!ExactActor() || !Actor->IsHidden()) { return false; }
        Actor->SetActorEnableCollision(false);
        if (!ExactActor() || Actor->GetActorEnableCollision() ||
            UGameplayStatics::FinishSpawningActor(Actor, Row.ActorPose) != Actor ||
            !ExactActor())
        { return false; }
        // Construction scripts may restore the CDO visibility or collision.
        Actor->SetActorHiddenInGame(true);
        if (!ExactActor() || !Actor->IsHidden()) { return false; }
        Actor->SetActorEnableCollision(false);
        if (!ExactActor() || Actor->GetActorEnableCollision()) { return false; }

        TArray<UStaticMeshComponent*> Meshes;
        TArray<UTextRenderComponent*> Texts;
        Actor->GetComponents<UStaticMeshComponent>(Meshes);
        Actor->GetComponents<UTextRenderComponent>(Texts);
        if (Meshes.Num() != 1 ||
            Texts.Num() != (Row.Kind == ECkSelectionGym_VisualKind::LabelMarker ? 1 : 0))
        { return false; }
        auto* Mesh = Meshes[0];
        auto* Label = Texts.IsEmpty() ? nullptr : Texts[0];
        auto ExactMesh = [&]() -> bool
        {
            if (!ExactActor() || !IsValid(Mesh) || Mesh->IsBeingDestroyed() ||
                Mesh->GetOwner() != Actor || !Mesh->IsRegistered())
            { return false; }
            TArray<UStaticMeshComponent*> Current;
            Actor->GetComponents<UStaticMeshComponent>(Current);
            return Current.Num() == 1 && Current[0] == Mesh;
        };
        auto ExactLabel = [&]() -> bool
        {
            if (!ExactMesh() || !IsValid(Label) || Label->IsBeingDestroyed() ||
                Label->GetOwner() != Actor || !Label->IsRegistered())
            { return false; }
            TArray<UTextRenderComponent*> Current;
            Actor->GetComponents<UTextRenderComponent>(Current);
            return Current.Num() == 1 && Current[0] == Label;
        };
        if (!ExactMesh() || (Label != nullptr && !ExactLabel()))
        { return false; }
        Mesh->SetStaticMesh(Row.Mesh.Get());
        if (!ExactMesh()) { return false; }
        Mesh->SetMaterial(0, Row.Material.Get());
        if (!ExactMesh()) { return false; }
        Mesh->SetRelativeTransform(Row.MeshRelativePose);
        if (!ExactMesh()) { return false; }
        Mesh->SetVisibility(Row.MeshVisible);
        if (!ExactMesh()) { return false; }
        Mesh->SetCollisionEnabled(Row.MeshCollisionMode);
        if (!ExactMesh()) { return false; }
        Mesh->SetCollisionObjectType(Row.ObjectChannel);
        if (!ExactMesh()) { return false; }
        Mesh->SetCollisionResponseToChannel(ECC_Visibility, Row.VisibilityResponse);
        if (!ExactMesh()) { return false; }
        if (Label != nullptr)
        {
            if (!ExactLabel()) { return false; }
            Label->SetRelativeTransform(Row.LabelRelativePose);
            if (!ExactLabel()) { return false; }
            Label->SetWorldSize(Row.LabelWorldSize);
            if (!ExactLabel()) { return false; }
            Label->SetTextRenderColor(Row.LabelColor);
            if (!ExactLabel()) { return false; }
            Label->SetText(FText::FromString(Row.LabelText));
            if (!ExactLabel()) { return false; }
        }
        const auto* Body = Mesh->GetBodyInstance();
        return ExactMesh() && Body != nullptr && Actor->IsHidden() &&
            !Actor->GetActorEnableCollision() &&
            Mesh->GetStaticMesh() == Row.Mesh.Get() &&
            Mesh->GetMaterial(0) == Row.Material.Get() &&
            Mesh->GetRelativeTransform().Equals(Row.MeshRelativePose, 0.001) &&
            Mesh->IsVisible() == Row.MeshVisible &&
            Body->GetCollisionEnabled(false) == Row.MeshCollisionMode &&
            Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision &&
            Mesh->GetCollisionObjectType() == Row.ObjectChannel &&
            Mesh->GetCollisionResponseToChannel(ECC_Visibility) ==
                Row.VisibilityResponse;
    }

    bool BuildObservations(const FState& S,
        const FCkSelectionGym_WireSnapshot& Frame,
        TArray<FCk_DebugOverlay_SourceObservation>& Out)
    {
        Out.Reset();
        TMap<int64, int32> Index;
        for (const auto& Node : Frame.Nodes)
        {
            auto Handle = FindNode(S, Node.Key);
            if (!Live(Handle)) { Out.Reset(); return false; }
            Index.Add(Node.Key, Out.AddDefaulted());
            Out.Last().Source = Handle;
        }
        for (const auto& Fact : Frame.Timers)
        {
            const auto* At = Index.Find(Fact.OwnerNodeKey);
            if (At == nullptr) { Out.Reset(); return false; }
            FCk_DebugOverlay_TimerFact Value;
            Value.Name = Fact.NameTag;
            Value.State = Fact.State;
            Out[*At].Timers.Add(Value);
        }
        for (const auto& Fact : Frame.Integers)
        {
            const auto* At = Index.Find(Fact.OwnerNodeKey);
            if (At == nullptr) { Out.Reset(); return false; }
            FCk_DebugOverlay_IntegerFact Value;
            Value.Label = Fact.NameTag;
            Value.Base = Fact.BaseValue;
            Value.Final = Fact.FinalValue;
            Out[*At].Integers.Add(Value);
        }
        for (const auto& Fact : Frame.StateMachines)
        {
            const auto* At = Index.Find(Fact.OwnerNodeKey);
            if (At == nullptr || Out[*At].HasSm) { Out.Reset(); return false; }
            auto& Row = Out[*At];
            Row.HasSm = true;
            Row.HasDebugHistory = Fact.HasDebugHistory;
            for (const auto& Link : Fact.Chain)
            {
                FCk_DebugOverlay_SmStateFact Value;
                Value.Depth = Link.Depth;
                Value.StateName = Link.StateClassName;
                Value.Status = Link.RunStatus;
                Row.SmStates.Add(Value);
            }
            for (const auto& Link : Fact.History)
            {
                FCk_DebugOverlay_SmHistoryFact Value;
                Value.FromStateName = Link.FromStateName;
                Value.ToStateName = Link.ToStateName;
                Row.SmHistory.Add(Value);
            }
        }
        return true;
    }

    bool SameFacts(const FCk_DebugOverlay_SourceObservation& A,
        const FCk_DebugOverlay_SourceObservation& B)
    {
        if (A.Source != B.Source || A.Timers.Num() != B.Timers.Num() ||
            A.Integers.Num() != B.Integers.Num() || A.HasSm != B.HasSm ||
            A.HasDebugHistory != B.HasDebugHistory ||
            A.SmStates.Num() != B.SmStates.Num() ||
            A.SmHistory.Num() != B.SmHistory.Num())
        { return false; }
        for (int32 I = 0; I < A.Timers.Num(); ++I)
        {
            if (A.Timers[I].Name != B.Timers[I].Name ||
                A.Timers[I].State != B.Timers[I].State)
            { return false; }
        }
        for (int32 I = 0; I < A.Integers.Num(); ++I)
        {
            if (A.Integers[I].Label != B.Integers[I].Label ||
                A.Integers[I].Base != B.Integers[I].Base ||
                A.Integers[I].Final != B.Integers[I].Final)
            { return false; }
        }
        for (int32 I = 0; I < A.SmStates.Num(); ++I)
        {
            if (A.SmStates[I].Depth != B.SmStates[I].Depth ||
                A.SmStates[I].StateName != B.SmStates[I].StateName ||
                A.SmStates[I].Status != B.SmStates[I].Status)
            { return false; }
        }
        for (int32 I = 0; I < A.SmHistory.Num(); ++I)
        {
            if (A.SmHistory[I].FromStateName != B.SmHistory[I].FromStateName ||
                A.SmHistory[I].ToStateName != B.SmHistory[I].ToStateName)
            { return false; }
        }
        return true;
    }

    bool PublishedFactsMatch(const FState& S)
    {
        if (!S.WorldRoot.Has<FCk_DebugOverlay_ObservationBank>())
        { return false; }
        const auto& Bank = S.WorldRoot.Get<FCk_DebugOverlay_ObservationBank>();
        if (Bank.ScenarioRoot != FindNode(S, S.Installed.ScenarioRootKey) ||
            Bank.Generation != static_cast<uint64>(S.AdmissionGeneration) ||
            Bank.Sequence != static_cast<uint64>(S.Installed.Sequence) ||
            Bank.Sources.Num() != S.Installed.Nodes.Num())
        { return false; }
        TArray<FCk_DebugOverlay_SourceObservation> Expected;
        if (!BuildObservations(S, S.Installed, Expected))
        { return false; }
        for (const auto& Row : Expected)
        {
            const auto* Actual = Bank.Sources.Find(Row.Source);
            if (Actual == nullptr || !SameFacts(Row, *Actual))
            { return false; }
        }
        return true;
    }

    bool AuthoredWallDefaults(const UStaticMeshComponent* Mesh)
    {
        if (!IsValid(Mesh) || !Mesh->CanEverAffectNavigation())
        { return false; }
        for (int32 Channel = 0; Channel < ECC_OverlapAll_Deprecated; ++Channel)
        {
            if (Mesh->GetCollisionResponseToChannel(
                    static_cast<ECollisionChannel>(Channel)) != ECR_Block)
            { return false; }
        }
        return true;
    }

    bool CompletePhysical(const FState& S,
        const FCkSelectionGym_WireSnapshot& Frame,
        bool ExpectExposed, FString& Why)
    {
        TArray<FCk_Handle> Outliers;
        for (const auto& Row : Frame.Nodes)
        {
            if (Row.LifetimeParentKey == 0 && Row.Key != Frame.ScenarioRootKey)
            { Outliers.Add(FindNode(S, Row.Key)); }
        }
        TArray<FCk_Handle> Actual;
        if (!ck_selection_gym_census::TryCensusAllocatedBank(
                S.World.Get(), FindNode(S, Frame.ScenarioRootKey),
                Outliers, Actual, Why) ||
            Actual.Num() != Frame.Nodes.Num())
        { return false; }
        TSet<FCk_Handle> Expected;
        for (const auto& Pair : S.Nodes) { Expected.Add(Pair.Value); }
        for (const auto& H : Actual)
        {
            if (!Expected.Remove(H)) { Why = TEXT("Unexpected local mirror member"); return false; }
        }
        if (!Expected.IsEmpty()) { Why = TEXT("Missing local mirror member"); return false; }
        for (const auto& Row : Frame.Nodes)
        {
            const auto H = FindNode(S, Row.Key);
            if (!Live(H)) { Why = TEXT("Local node lost"); return false; }
            if (Row.HasTransform)
            {
                if (!H.Has<ck::FFragment_Transform>() ||
                    !UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                        UCk_Utils_Transform_UE::Cast(H))
                        .Equals(Row.WorldPose, 0.001))
                { Why = TEXT("Local transform not settled"); return false; }
            }
            if (Row.HasSceneNode)
            {
                const auto Parent = FindNode(S, Row.SceneNodeParentKey);
                if (!Live(Parent) || !SameRegistry(Parent, S.WorldRoot) ||
                    !Parent.Has<ck::FFragment_Transform>() ||
                    !H.Has<ck::FFragment_SceneNode>() ||
                    !H.Has<ck::SceneNodeParent>() ||
                    UCk_Utils_SceneNode_UE::Get_Parent(
                        UCk_Utils_SceneNode_UE::Cast(H)) !=
                        UCk_Utils_Transform_UE::Cast(Parent) ||
                    !UCk_Utils_SceneNode_UE::Get_Offset(
                        UCk_Utils_SceneNode_UE::Cast(H))
                        .Equals(Row.SceneNodeLocalOffset, 0.001))
                { Why = TEXT("Local SceneNode not settled"); return false; }
            }
        }
        for (const auto& Row : Frame.Visuals)
        {
            const auto* Weak = S.Visuals.Find(Row.VisualKey);
            auto* Actor = Weak != nullptr ? Weak->Get() : nullptr;
            if (!IsValid(Actor) || Actor->GetWorld() != S.World.Get() ||
                TWeakObjectPtr<AActor>(Actor->GetOwner()) !=
                    S.SpawnOwnerIdentity ||
                !Actor->GetActorTransform().Equals(Row.ActorPose, 0.001))
            { Why = TEXT("Local visual not settled"); return false; }
            if (Actor->IsHidden() == ExpectExposed ||
                Actor->GetActorEnableCollision() !=
                    (ExpectExposed && Row.ActorCollisionEnabled))
            { Why = TEXT("Local visual visibility/collision changed"); return false; }
            TArray<UStaticMeshComponent*> Meshes;
            TArray<UTextRenderComponent*> Texts;
            Actor->GetComponents<UStaticMeshComponent>(Meshes);
            Actor->GetComponents<UTextRenderComponent>(Texts);
            if (Meshes.Num() != 1 ||
                Texts.Num() != (Row.Kind == ECkSelectionGym_VisualKind::LabelMarker ? 1 : 0))
            { Why = TEXT("Local visual component topology changed"); return false; }
            const auto* Mesh = Meshes[0];
            if (!IsValid(Mesh) || Mesh->GetOwner() != Actor ||
                Mesh->GetStaticMesh() != Row.Mesh.Get() ||
                Mesh->GetMaterial(0) != Row.Material.Get() ||
                !Mesh->GetRelativeTransform().Equals(Row.MeshRelativePose, 0.001) ||
                Mesh->IsVisible() != Row.MeshVisible ||
                Mesh->GetBodyInstance() == nullptr ||
                Mesh->GetBodyInstance()->GetCollisionEnabled(false) !=
                    Row.MeshCollisionMode ||
                Mesh->GetCollisionEnabled() !=
                    (ExpectExposed ? Row.MeshCollisionMode.GetValue() :
                        ECollisionEnabled::NoCollision) ||
                Mesh->GetCollisionObjectType() != Row.ObjectChannel ||
                Mesh->GetCollisionResponseToChannel(ECC_Visibility) !=
                    Row.VisibilityResponse)
            { Why = TEXT("Local mesh physical value changed"); return false; }
            if (Row.Kind == ECkSelectionGym_VisualKind::OcclusionWall &&
                !AuthoredWallDefaults(Mesh))
            { Why = TEXT("Occlusion wall lost all-channel/nav defaults"); return false; }
            if (!Texts.IsEmpty())
            {
                const auto* Label = Texts[0];
                if (!IsValid(Label) || Label->GetOwner() != Actor ||
                    !Label->GetRelativeTransform().Equals(Row.LabelRelativePose, 0.001) ||
                    !FMath::IsNearlyEqual(Label->WorldSize, Row.LabelWorldSize) ||
                    Label->TextRenderColor != Row.LabelColor ||
                    Label->Text.ToString() != Row.LabelText)
                { Why = TEXT("Local text physical value changed"); return false; }
            }
        }
        Why.Reset();
        return true;
    }

    bool ApplyMotion(FState& S, const FCkSelectionGym_WireSnapshot& Frame)
    {
        const auto MotionFrame = Frame;
        const auto Lease = CaptureMirrorMutationLease(S);
        const auto FrameCurrent = [&Frame, &MotionFrame]()
        {
            return FCkSelectionGym_WireSnapshot::StaticStruct()->
                CompareScriptStruct(&Frame, &MotionFrame, PPF_None);
        };
        TSet<int64> AllContexts;
        for (const auto& Row : MotionFrame.Nodes) { AllContexts.Add(Row.Key); }
        if (!ProveMirrorMutationNodes(Lease, MotionFrame, AllContexts) ||
            !FrameCurrent() ||
            !ProveMirrorMutationVisuals(Lease, MotionFrame))
        { return false; }
        for (const auto& Row : MotionFrame.Nodes)
        {
            if (!Row.HasTransform || Row.HasSceneNode) { continue; }
            if (!ProveMirrorMutationNodes(Lease, MotionFrame, AllContexts) ||
                !FrameCurrent() ||
                !ProveMirrorMutationVisuals(Lease, MotionFrame))
            { return false; }
            auto H = FindNode(S, Row.Key);
            if (!Live(H) || !H.Has<ck::FFragment_Transform>() ||
                !H.Has<ck::FFragment_Transform_Previous>())
            { return false; }
            auto& Current = H.Get<ck::FFragment_Transform>();
            auto& Previous = H.Get<ck::FFragment_Transform_Previous>();
            const auto Modified = UCk_Utils_Transform_UE::
                Apply_SetTransform_DirectWrite(Current, Previous, Row.WorldPose);
            if (Modified != ECk_TransformComponents::None)
            { H.AddOrGet<ck::FTag_Transform_Updated>(); }
            if (!ProveMirrorMutationNodes(Lease, MotionFrame, AllContexts) ||
                !FrameCurrent() ||
                !ProveMirrorMutationVisuals(Lease, MotionFrame))
            { return false; }
        }
        for (const auto& Row : MotionFrame.Visuals)
        {
            if (!ProveMirrorMutationNodes(Lease, MotionFrame, AllContexts) ||
                !FrameCurrent() ||
                !ProveMirrorMutationVisuals(Lease, MotionFrame))
            { return false; }
            const auto* Weak = S.Visuals.Find(Row.VisualKey);
            auto* Actor = Weak != nullptr ? Weak->Get() : nullptr;
            Actor->SetActorTransform(Row.ActorPose);
            if (!ProveMirrorMutationNodes(Lease, MotionFrame, AllContexts) ||
                !FrameCurrent() ||
                !ProveMirrorMutationVisuals(Lease, MotionFrame))
            { return false; }
        }
        return true;
    }

    bool HoldRetiring(FState& S, const FCkSelectionGym_WireSnapshot& Envelope,
        FString& Why)
    {
        const auto Lease = CaptureMirrorMutationLease(S);
        const auto FrozenEnvelope = Envelope;
        if (S.Phase != ck::ESelectionMirrorPhase::Installed ||
            !CompletePhysical(S, S.Installed, true, Why))
        { return FailValue(Why, TEXT("Retiring envelope requires a healthy installed bank")); }
        TArray<FCk_Handle> Frozen;
        Frozen.Reserve(S.Installed.Nodes.Num());
        for (const auto& Row : S.Installed.Nodes)
        {
            const auto H = FindNode(S, Row.Key);
            if (!UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(H))
            { return FailValue(Why, TEXT("Retiring envelope found unpublished source")); }
            Frozen.Add(H);
        }
        if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, Frozen) ||
            !UCk_DebugOverlay_SourceAdmission_UE::TryClose(
                S.World.Get(), S.WorldRoot, S.AdmissionGeneration, Why))
        { return FailClosedMirrorLease(Lease, TEXT("Retiring envelope failed to close exact bank"), Why); }
        if (!ProveMirrorMutationLease(Lease))
        { return FailClosedMirrorLease(Lease, TEXT("Retiring closure changed exact mirror"), Why); }
        S.Phase = ck::ESelectionMirrorPhase::HoldingRetiring;
        S.Deadline = FPlatformTime::Seconds() + 20.0;
        const auto Visuals = S.Visuals;
        for (const auto& Pair : Visuals)
        {
            auto* Actor = Pair.Value.Get();
            if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, Frozen) || !RetainedVisualSet(S))
            { return FailClosedMirrorLease(Lease, TEXT("Retiring hold visual provenance changed"), Why); }
            Actor->SetActorHiddenInGame(true);
            if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, Frozen) || !RetainedVisualSet(S))
            { return FailClosedMirrorLease(Lease, TEXT("Retiring hold hide callback changed source"), Why); }
            Actor->SetActorEnableCollision(false);
            if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, Frozen) || !RetainedVisualSet(S))
            { return FailClosedMirrorLease(Lease, TEXT("Retiring hold collision callback changed source"), Why); }
        }
        if (!ProveMirrorMutationLease(Lease) ||
            !CompletePhysical(S, S.Installed, false, Why))
        { return FailClosedMirrorLease(Lease, TEXT("Retiring hold bank did not settle hidden"), Why); }
        S.LastEnvelopeSourceGeneration = FrozenEnvelope.SourceGeneration;
        S.LastEnvelopeScenarioGeneration = FrozenEnvelope.ScenarioGeneration;
        S.LastEnvelopeSequence = FrozenEnvelope.Sequence;
        return true;
    }

    bool QueueRespawn(FState& S, const FCkSelectionGym_WireSnapshot& Next,
        FString& Why)
    {
        const auto Lease = CaptureMirrorMutationLease(S);
        const auto FrozenNext = Next;
        const bool AlreadyClosed = S.Phase == ck::ESelectionMirrorPhase::HoldingRetiring;
        if (S.Phase != ck::ESelectionMirrorPhase::Installed && !AlreadyClosed)
        { return FailValue(Why, TEXT("X replacement requires a settled retained bank")); }
        if (Next.Scenario != 4 ||
            Next.SourceGeneration != S.Installed.SourceGeneration ||
            Next.ScenarioGeneration != S.Installed.ScenarioGeneration ||
            Next.ScenarioRootKey != S.Installed.ScenarioRootKey ||
            Next.Sequence <= S.Installed.Sequence)
        { return FailValue(Why, TEXT("Only same-scenario X leaf replacement is supported")); }
        TMap<int64, const FCkSelectionGym_NodeRow*> NewNodes;
        for (const auto& Row : Next.Nodes) { NewNodes.Add(Row.Key, &Row); }
        TMap<int64, const FCkSelectionGym_VisualRow*> NewVisuals;
        for (const auto& Row : Next.Visuals) { NewVisuals.Add(Row.VisualKey, &Row); }
        const FCkSelectionGym_NodeRow* LostNode = nullptr;
        const FCkSelectionGym_NodeRow* AddedNode = nullptr;
        for (const auto& Row : S.Installed.Nodes)
        { if (!NewNodes.Contains(Row.Key)) { if (LostNode) { return false; } LostNode = &Row; } }
        for (const auto& Row : Next.Nodes)
        { if (!S.Nodes.Contains(Row.Key)) { if (AddedNode) { return false; } AddedNode = &Row; } }
        const FCkSelectionGym_VisualRow* LostVisual = nullptr;
        const FCkSelectionGym_VisualRow* AddedVisual = nullptr;
        for (const auto& Row : S.Installed.Visuals)
        { if (!NewVisuals.Contains(Row.VisualKey)) { if (LostVisual) { return false; } LostVisual = &Row; } }
        for (const auto& Row : Next.Visuals)
        { if (!S.Visuals.Contains(Row.VisualKey)) { if (AddedVisual) { return false; } AddedVisual = &Row; } }
        if (!LostNode || !AddedNode || !LostVisual || !AddedVisual ||
            Next.Nodes.Num() != S.Installed.Nodes.Num() ||
            Next.Visuals.Num() != S.Installed.Visuals.Num() ||
            LostNode->LifetimeParentKey != Next.ScenarioRootKey ||
            AddedNode->LifetimeParentKey != Next.ScenarioRootKey ||
            LostVisual->NodeKey != LostNode->Key ||
            AddedVisual->NodeKey != AddedNode->Key ||
            LostVisual->Kind != ECkSelectionGym_VisualKind::LabelMarker ||
            AddedVisual->Kind != ECkSelectionGym_VisualKind::LabelMarker)
        { return FailValue(Why, TEXT("X replacement is not one authored direct leaf and marker")); }
        for (const auto& Old : S.Installed.Nodes)
        {
            const auto* Found = NewNodes.Find(Old.Key);
            if (!Found && Old.Key == LostNode->Key) { continue; }
            if (!Found || (*Found)->LifetimeParentKey != Old.LifetimeParentKey ||
                (*Found)->ContextRootKey != Old.ContextRootKey ||
                (*Found)->OwnContextRoot != Old.OwnContextRoot ||
                (*Found)->HasTransform != Old.HasTransform ||
                (*Found)->HasSceneNode != Old.HasSceneNode ||
                (*Found)->SceneNodeParentKey != Old.SceneNodeParentKey ||
                !(*Found)->SceneNodeLocalOffset.Equals(Old.SceneNodeLocalOffset, 0.0) ||
                (*Found)->DebugName != Old.DebugName ||
                (*Found)->RootLabel != Old.RootLabel)
        { return FailValue(Why, TEXT("X replacement altered a surviving node")); }
        }
        for (const auto& Old : S.Installed.Visuals)
        {
            const auto* Found = NewVisuals.Find(Old.VisualKey);
            if (!Found && Old.VisualKey == LostVisual->VisualKey) { continue; }
            if (!Found) { return FailValue(Why, TEXT("X replacement lost a stable visual")); }
            auto Frozen = Old;
            Frozen.ActorPose = (*Found)->ActorPose;
            if (!FCkSelectionGym_VisualRow::StaticStruct()->
                    CompareScriptStruct(&Frozen, *Found, PPF_None))
            { return FailValue(Why, TEXT("X replacement changed a stable visual style")); }
        }
        FString Physical;
        if (!CompletePhysical(S, S.Installed, !AlreadyClosed, Physical))
        { return FailClosedMirrorLease(Lease, TEXT("X source bank was not healthy before retirement"), Why); }
        auto OldLeaf = FindNode(S, LostNode->Key);
        const auto* Weak = S.Visuals.Find(LostVisual->VisualKey);
        auto* OldActor = Weak != nullptr ? Weak->Get() : nullptr;
        if (!Direct(OldLeaf, FindNode(S, Next.ScenarioRootKey), S.WorldRoot) ||
            !IsValid(OldActor) || OldActor->GetWorld() != S.World.Get() ||
            OldActor->GetOwner() != S.GameState.Get() ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::
                TryProve_FrozenRetirementLedger(
                    {OldLeaf}, {OldLeaf}, S.WorldRoot))
        { return FailClosedMirrorLease(Lease, TEXT("X retiring leaf provenance failed"), Why); }
        const int64 LostNodeKey = LostNode->Key;
        const int64 LostVisualKey = LostVisual->VisualKey;
        if (!AlreadyClosed && !UCk_DebugOverlay_SourceAdmission_UE::TryClose(
                S.World.Get(), S.WorldRoot, S.AdmissionGeneration, Why))
        { return FailClosedMirrorLease(Lease, TEXT("X failed to close source bank"), Why); }
        if (!ProveMirrorMutationLease(Lease))
        { return FailClosedMirrorLease(Lease, TEXT("X closure changed exact mirror"), Why); }
        S.RetiringLeaf = OldLeaf;
        S.RetiringVisual = OldActor;
        S.RetiringLeafKey = LostNodeKey;
        S.RetiringVisualKey = LostVisualKey;
        S.Pending = FrozenNext;
        S.Deadline = FPlatformTime::Seconds() + 15.0;
        S.Phase = ck::ESelectionMirrorPhase::RetiringLeaf;
        const auto Visuals = S.Visuals;
        for (const auto& Pair : Visuals)
        {
            auto* Actor = Pair.Value.Get();
            if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, {OldLeaf}) || !RetainedVisualSet(S))
            { return FailClosedMirrorLease(Lease, TEXT("X visual closure lost exact provenance"), Why); }
            Actor->SetActorHiddenInGame(true);
            if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, {OldLeaf}) || !RetainedVisualSet(S))
            { return FailClosedMirrorLease(Lease, TEXT("X visual hide callback changed source"), Why); }
            Actor->SetActorEnableCollision(false);
            if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, {OldLeaf}) || !RetainedVisualSet(S))
            { return FailClosedMirrorLease(Lease, TEXT("X visual collision callback changed source"), Why); }
        }
        if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, {OldLeaf}) ||
            !Direct(OldLeaf, FindNode(S, FrozenNext.ScenarioRootKey), S.WorldRoot))
        { return FailClosedMirrorLease(Lease, TEXT("X leaf changed during visual closure"), Why); }
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(OldLeaf);
        if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, {OldLeaf}) || !RetainedVisualSet(S))
        { return FailClosedMirrorLease(Lease, TEXT("X leaf destroy callback changed source"), Why); }
        const bool DestroyRequested = OldActor->Destroy();
        TSet<int64> DestroyedVisual;
        DestroyedVisual.Add(LostVisualKey);
        if (!DestroyRequested || !ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, {OldLeaf}) ||
            !RetainedVisualSet(S, &DestroyedVisual))
        { return FailClosedMirrorLease(Lease, TEXT("X marker destroy callback changed source"), Why); }
        return true;
    }

    bool QueueRetireAll(FState& S, FString& Why)
    {
        const auto Lease = CaptureMirrorMutationLease(S);
        const bool AlreadyClosed = S.Phase == ck::ESelectionMirrorPhase::HoldingRetiring;
        if ((S.Phase != ck::ESelectionMirrorPhase::Installed && !AlreadyClosed) ||
            !CompletePhysical(S, S.Installed, !AlreadyClosed, Why))
        { return FailValue(Why, TEXT("Existing bank is not healthy for retirement")); }
        TArray<FCk_Handle> Frozen;
        Frozen.Reserve(S.Installed.Nodes.Num());
        for (const auto& Row : S.Installed.Nodes)
        {
            const auto H = FindNode(S, Row.Key);
            if (!Live(H)) { return FailValue(Why, TEXT("Retirement ledger lost a live node")); }
            Frozen.Add(H);
        }
        if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, Frozen))
        { return FailValue(Why, TEXT("Retirement ledger includes unknown children")); }
        const auto InstalledRows = S.Installed.Nodes;
        const auto Visuals = S.Visuals;
        if (!AlreadyClosed && !UCk_DebugOverlay_SourceAdmission_UE::TryClose(
                S.World.Get(), S.WorldRoot, S.AdmissionGeneration, Why))
        { return FailClosedMirrorLease(Lease,
            TEXT("Full retirement failed to close source bank"), Why); }
        if (!ProveMirrorMutationLease(Lease))
        { return FailClosedMirrorLease(Lease,
            TEXT("Full retirement closure changed exact mirror"), Why); }
        S.FrozenAll = Frozen;
        S.Deadline = FPlatformTime::Seconds() + 20.0;
        S.Phase = ck::ESelectionMirrorPhase::RetiringAll;
        // World-local actors are only changed when both the retained receipt and
        // exact original owner identity still match. Closure precedes callbacks.
        for (const auto& Pair : Visuals)
        {
            auto* Actor = Pair.Value.Get();
            if (!ProveMirrorMutationLease(Lease) || S.FrozenAll != Frozen ||
                !ProveFrozenContext(S, Frozen) || !RetainedVisualSet(S))
            { return FailClosedMirrorLease(Lease, TEXT("Visual owner changed during full retirement"), Why); }
            Actor->SetActorHiddenInGame(true);
            if (!ProveMirrorMutationLease(Lease) || S.FrozenAll != Frozen ||
                !ProveFrozenContext(S, Frozen) || !RetainedVisualSet(S))
            { return FailClosedMirrorLease(Lease, TEXT("Visual hide callback changed full retirement"), Why); }
            Actor->SetActorEnableCollision(false);
            if (!ProveMirrorMutationLease(Lease) || S.FrozenAll != Frozen ||
                !ProveFrozenContext(S, Frozen) || !RetainedVisualSet(S))
            { return FailClosedMirrorLease(Lease, TEXT("Visual collision callback changed full retirement"), Why); }
        }
        // A native destroy recursively walks a target's current children.
        // Request only known leaves, re-proving the entire frozen read-set
        // before and after every callback-producing request.
        TSet<int64> RemainingKeys;
        for (const auto& Row : InstalledRows) { RemainingKeys.Add(Row.Key); }
        TArray<FCk_Handle> LeafFirst;
        LeafFirst.Reserve(InstalledRows.Num());
        while (!RemainingKeys.IsEmpty())
        {
            const FCkSelectionGym_NodeRow* Leaf = nullptr;
            for (const auto& Candidate : InstalledRows)
            {
                if (!RemainingKeys.Contains(Candidate.Key)) { continue; }
                const bool HasRemainingChild = InstalledRows.ContainsByPredicate(
                    [&RemainingKeys, &Candidate](const FCkSelectionGym_NodeRow& Other)
                    { return RemainingKeys.Contains(Other.Key) &&
                        Other.LifetimeParentKey == Candidate.Key; });
                if (!HasRemainingChild) { Leaf = &Candidate; break; }
            }
            if (Leaf == nullptr)
            { return FailClosedMirrorLease(Lease, TEXT("Full retirement topology became cyclic"), Why); }
            LeafFirst.Add(FindNode(S, Leaf->Key));
            RemainingKeys.Remove(Leaf->Key);
        }
        for (auto Target : LeafFirst)
        {
            if (!ProveMirrorMutationLease(Lease) || S.FrozenAll != Frozen ||
                !ProveFrozenContext(S, Frozen) || !Live(Target))
            { return FailClosedMirrorLease(Lease, TEXT("Full retirement source changed before leaf request"), Why); }
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Target);
            if (!ProveMirrorMutationLease(Lease) || S.FrozenAll != Frozen ||
                !ProveFrozenContext(S, Frozen))
            { return FailClosedMirrorLease(Lease, TEXT("Full retirement callback changed frozen source"), Why); }
        }
        TSet<int64> DestroyedVisuals;
        for (const auto& Pair : Visuals)
        {
            auto* Actor = Pair.Value.Get();
            if (!ProveMirrorMutationLease(Lease) || S.FrozenAll != Frozen ||
                !ProveFrozenContext(S, Frozen) ||
                !RetainedVisualSet(S, &DestroyedVisuals))
            { return FailClosedMirrorLease(Lease, TEXT("Visual receipt changed before full destroy"), Why); }
            const bool DestroyRequested = Actor->Destroy();
            DestroyedVisuals.Add(Pair.Key);
            if (!DestroyRequested || !ProveMirrorMutationLease(Lease) ||
                S.FrozenAll != Frozen || !ProveFrozenContext(S, Frozen) ||
                !RetainedVisualSet(S, &DestroyedVisuals))
            { return FailClosedMirrorLease(Lease, TEXT("Visual destroy callback changed frozen source"), Why); }
        }
        return true;
    }

    bool StageNewBank(FState& S, const FCkSelectionGym_WireSnapshot& InputSnapshot,
        TSubclassOf<AActor> MarkerClass, TSubclassOf<AActor> WallClass,
        FString& Why)
    {
        const auto Frame = InputSnapshot;
        const auto Lease = CaptureMirrorMutationLease(S);
        const auto PendingMatches = [&Lease, &Frame]()
        {
            const auto* Current = ProveMirrorMutationLease(Lease);
            return Current != nullptr &&
                FCkSelectionGym_WireSnapshot::StaticStruct()->
                    CompareScriptStruct(&Current->Pending, &Frame, PPF_None);
        };
        auto* World = S.World.Get();
        auto* GS = S.GameState.Get();
        FCk_Handle Exact;
        if (!Context(World, GS, Exact) || Exact != S.WorldRoot ||
            !S.Nodes.IsEmpty() || !S.Visuals.IsEmpty() ||
            !S.DirectRoots.IsEmpty() || S.ScopeBegun)
        { return FailValue(Why, TEXT("Fresh mirror bank precondition failed")); }
        S.Pending = Frame;
        S.MarkerClass = MarkerClass;
        S.WallClass = WallClass;
        S.SpawnOwnerIdentity = GS;
        S.Deadline = FPlatformTime::Seconds() + 15.0;
        S.Phase = ck::ESelectionMirrorPhase::Staging;
        for (const auto& Row : Frame.Nodes)
        {
            if (Row.LifetimeParentKey != 0) { continue; }
            auto H = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S.WorldRoot);
            auto* Retained = FindMirrorCapturedState(Lease);
            if (Retained == nullptr)
            { return FailClosedMirrorLease(Lease,
                TEXT("Bare local root creation changed mirror lifetime"), Why); }
            Retained->Nodes.Add(Row.Key, H);
            Retained->DirectRoots.Add(H);
            if (!PendingMatches() || !Live(H) ||
                !Direct(H, Lease.WorldRoot, Lease.WorldRoot))
            { return FailClosedMirrorLease(Lease, TEXT("Bare local root creation failed"), Why); }
        }
        if (!UCk_DebugOverlay_SourceAdmission_UE::TryBegin(
                World, S.WorldRoot, S.AdmissionGeneration,
                S.DirectRoots, Why))
        { return FailClosedMirrorLease(Lease, TEXT("Local source admission begin failed"), Why); }
        if (FindMirrorCapturedState(Lease) == nullptr)
        { return FailClosedMirrorLease(Lease,
            TEXT("Local source admission changed mirror lifetime"), Why); }
        S.ScopeBegun = true;
        if (!PendingMatches())
        { return FailClosedMirrorLease(Lease,
            TEXT("Local source admission changed client binding"), Why); }
        for (int32 Remaining = Frame.Nodes.Num() - S.Nodes.Num(); Remaining > 0;)
        {
            const int32 Before = Remaining;
            for (const auto& Row : Frame.Nodes)
            {
                if (S.Nodes.Contains(Row.Key) || Row.LifetimeParentKey == 0) { continue; }
                const auto Parent = FindNode(S, Row.LifetimeParentKey);
                if (!Live(Parent)) { continue; }
                auto H = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Parent);
                auto* Retained = FindMirrorCapturedState(Lease);
                if (Retained == nullptr)
                { return FailClosedMirrorLease(Lease,
                    TEXT("Local descendant creation changed mirror lifetime"), Why); }
                Retained->Nodes.Add(Row.Key, H);
                if (!PendingMatches() || !Live(H) ||
                    !Direct(H, Parent, Lease.WorldRoot))
                { return FailClosedMirrorLease(Lease, TEXT("Local descendant creation failed"), Why); }
                --Remaining;
            }
            if (Before == Remaining)
            { return FailClosedMirrorLease(Lease, TEXT("Local topology did not resolve"), Why); }
        }
        TSet<int64> Composed;
        for (int32 Remaining = Frame.Nodes.Num(); Remaining > 0;)
        {
            const int32 Before = Remaining;
            for (const auto& Row : Frame.Nodes)
            {
                if (Composed.Contains(Row.Key) ||
                    (Row.LifetimeParentKey > 0 && !Composed.Contains(Row.LifetimeParentKey)))
                { continue; }
                if (!ComposeNode(S, Row, Composed))
                { return FailClosedMirrorLease(Lease, TEXT("Local node composition failed"), Why); }
                Composed.Add(Row.Key);
                --Remaining;
            }
            if (Before == Remaining)
            { return FailClosedMirrorLease(Lease, TEXT("Local composition order did not resolve"), Why); }
        }
        for (const auto& Row : Frame.Visuals)
        {
            if (!ComposeVisual(S, Row, MarkerClass, WallClass))
            { return FailClosedMirrorLease(Lease, TEXT("Local visual composition failed"), Why); }
            if (!PendingMatches())
            { return FailClosedMirrorLease(Lease,
                TEXT("Local visual callback changed accepted frame"), Why); }
        }
        return PendingMatches() || FailClosedMirrorLease(Lease,
            TEXT("Local bank changed during composition"), Why);
    }
}

bool UCkSelectionGym_Mirror_UE::TryAcceptActive(UWorld* World,
    AGameStateBase* ExactGameState, const FCkSelectionGym_WireSnapshot& InputSnapshot,
    TSubclassOf<AActor> MarkerClass, TSubclassOf<AActor> WallClass,
    FString& OutReason)
{
    OutReason.Reset();
    const auto Snapshot = InputSnapshot;
    FCk_Handle WorldRoot;
    if (!Context(World, ExactGameState, WorldRoot))
    {
        CloseChangedBinding(World);
        OutReason = TEXT("Exact client context unavailable");
        return false;
    }
    const bool ValueValid = Preflight(Snapshot, MarkerClass, WallClass, OutReason);
    FCk_Handle AfterPreflight;
    if (!Context(World, ExactGameState, AfterPreflight) ||
        AfterPreflight != WorldRoot)
    {
        CloseChangedBinding(World);
        return FailValue(OutReason, TEXT("Client context changed during Active preflight"));
    }
    if (!ValueValid)
    {
        if (WorldRoot.Has<FState>())
        {
            auto& Existing = WorldRoot.Get<FState>();
            if (Bound(Existing, World, ExactGameState, WorldRoot))
            { CloseFailed(Existing, TEXT("Malformed Active intake")); }
        }
        return false;
    }
    if (WorldRoot.Has<FState>())
    {
        auto& S = WorldRoot.Get<FState>();
        const auto Lease = CaptureMirrorMutationLease(S);
        if (!Bound(S, World, ExactGameState, WorldRoot))
        { return FailValue(OutReason, TEXT("Mirror session failed or bound elsewhere")); }
        const bool NewGeneration =
            Snapshot.SourceGeneration > S.Installed.SourceGeneration ||
            (Snapshot.SourceGeneration == S.Installed.SourceGeneration &&
             Snapshot.ScenarioGeneration > S.Installed.ScenarioGeneration);
        const bool HoldingRetiring =
            S.Phase == ck::ESelectionMirrorPhase::HoldingRetiring;
        if (HoldingRetiring)
        {
            const bool OlderThanEnvelope =
                Snapshot.SourceGeneration < S.LastEnvelopeSourceGeneration ||
                (Snapshot.SourceGeneration == S.LastEnvelopeSourceGeneration &&
                 Snapshot.ScenarioGeneration < S.LastEnvelopeScenarioGeneration);
            const bool SameEnvelopeGeneration =
                Snapshot.SourceGeneration == S.LastEnvelopeSourceGeneration &&
                Snapshot.ScenarioGeneration == S.LastEnvelopeScenarioGeneration;
            if (OlderThanEnvelope ||
                (SameEnvelopeGeneration && Snapshot.Sequence <= S.LastEnvelopeSequence))
            { return FailValue(OutReason, TEXT("Active replayed or preceded Retiring envelope")); }
        }
        if (S.Phase == ck::ESelectionMirrorPhase::RetiringAll ||
            S.Phase == ck::ESelectionMirrorPhase::AwaitingNew)
        {
            if (!NewGeneration)
            { return FailValue(OutReason, TEXT("Retiring bank requires newer generation")); }
            if (S.HasPendingActive)
            {
                if (FCkSelectionGym_WireSnapshot::StaticStruct()->
                        CompareScriptStruct(&Snapshot, &S.Pending, PPF_None))
                { return true; }
                return FailValue(OutReason, TEXT("New generation already queued"));
            }
            S.Pending = Snapshot;
            S.MarkerClass = MarkerClass;
            S.WallClass = WallClass;
            S.HasPendingActive = true;
            if (S.Phase == ck::ESelectionMirrorPhase::RetiringAll)
            { return FailValue(OutReason, TEXT("Old bank physical retirement pending")); }
            S.HasPendingActive = false;
            if (!StageNewBank(S, Snapshot, MarkerClass, WallClass, OutReason))
            { return false; }
            return TryAdvance(World, ExactGameState, OutReason);
        }
        if (S.Phase != ck::ESelectionMirrorPhase::Installed && !HoldingRetiring)
        {
            if (NewGeneration)
            {
                return FailClosedMirrorLease(Lease,
                    TEXT("New generation overtook incomplete local bank"), OutReason);
            }
            return FailValue(OutReason, TEXT("Mirror session busy"));
        }
        if (NewGeneration)
        {
            const auto PreviousPending = S.Pending;
            S.Pending = Snapshot;
            S.MarkerClass = MarkerClass;
            S.WallClass = WallClass;
            S.HasPendingActive = true;
            const bool RetireQueued = QueueRetireAll(S, OutReason);
            if (!ProveMirrorMutationLease(Lease))
            { return FailClosedMirrorLease(Lease,
                TEXT("Mirror state changed during full retirement request"), OutReason); }
            if (!RetireQueued)
            {
                if (S.Phase == ck::ESelectionMirrorPhase::Installed)
                { S.Pending = PreviousPending; S.HasPendingActive = false; }
                else if (S.Phase == ck::ESelectionMirrorPhase::HoldingRetiring)
                { return FailClosedMirrorLease(Lease,
                    TEXT("Held bank failed full retirement preflight"), OutReason); }
                return false;
            }
            return FailValue(OutReason, TEXT("Old bank physical retirement pending"));
        }
        if (Snapshot.Sequence == S.Installed.Sequence &&
            Snapshot.SourceGeneration == S.Installed.SourceGeneration &&
            Snapshot.ScenarioGeneration == S.Installed.ScenarioGeneration &&
            FCkSelectionGym_WireSnapshot::StaticStruct()->
                CompareScriptStruct(&Snapshot, &S.Installed, PPF_None))
        { return true; }
        if (Snapshot.SourceGeneration != S.Installed.SourceGeneration ||
            Snapshot.ScenarioGeneration != S.Installed.ScenarioGeneration ||
            Snapshot.Scenario != S.Installed.Scenario ||
            Snapshot.Sequence <= S.Installed.Sequence)
        { return FailValue(OutReason, TEXT("Unsupported or stale mirror generation")); }
        // Same-key motion/fact update. A changed leaf enters the separate
        // closed-scope retirement transaction, not this immediate path.
        if (Snapshot.Nodes.Num() != S.Installed.Nodes.Num() ||
            Snapshot.Visuals.Num() != S.Installed.Visuals.Num())
        { return FailValue(OutReason, TEXT("Unsupported membership cardinality change")); }
        bool ChangedIdentity = false;
        for (const auto& Row : Snapshot.Nodes)
        { ChangedIdentity |= !S.Nodes.Contains(Row.Key); }
        for (const auto& Row : Snapshot.Visuals)
        { ChangedIdentity |= !S.Visuals.Contains(Row.VisualKey); }
        if (ChangedIdentity)
        {
            const bool Accepted = QueueRespawn(S, Snapshot, OutReason);
            if (!ProveMirrorMutationLease(Lease))
            { return FailClosedMirrorLease(Lease,
                TEXT("Mirror state changed during X retirement request"), OutReason); }
            if (!Accepted && HoldingRetiring &&
                S.Phase == ck::ESelectionMirrorPhase::HoldingRetiring)
            { return FailClosedMirrorLease(Lease,
                TEXT("Held bank rejected X replacement"), OutReason); }
            return Accepted;
        }
        if (HoldingRetiring)
        {
            return FailClosedMirrorLease(Lease,
                TEXT("Retiring envelope requires X replacement or newer scenario"), OutReason);
        }
        TMap<int64, const FCkSelectionGym_NodeRow*> OldNodes;
        for (const auto& Row : S.Installed.Nodes) { OldNodes.Add(Row.Key, &Row); }
        for (const auto& Row : Snapshot.Nodes)
        {
            const auto* Old = OldNodes.Find(Row.Key);
            if (Old == nullptr || (*Old)->LifetimeParentKey != Row.LifetimeParentKey ||
                (*Old)->ContextRootKey != Row.ContextRootKey ||
                (*Old)->OwnContextRoot != Row.OwnContextRoot ||
                (*Old)->HasTransform != Row.HasTransform ||
                (*Old)->HasSceneNode != Row.HasSceneNode ||
                (*Old)->SceneNodeParentKey != Row.SceneNodeParentKey ||
                !(*Old)->SceneNodeLocalOffset.Equals(Row.SceneNodeLocalOffset, 0.0) ||
                (*Old)->DebugName != Row.DebugName ||
                (*Old)->RootLabel != Row.RootLabel ||
                (Row.HasSceneNode && !(*Old)->WorldPose.Equals(Row.WorldPose, 0.0)))
            { return FailValue(OutReason, TEXT("Unsupported same-key structural change")); }
        }
        TMap<int64, const FCkSelectionGym_VisualRow*> OldVisuals;
        for (const auto& Row : S.Installed.Visuals) { OldVisuals.Add(Row.VisualKey, &Row); }
        for (const auto& Row : Snapshot.Visuals)
        {
            const auto* Old = OldVisuals.Find(Row.VisualKey);
            if (Old == nullptr || (*Old)->Kind != Row.Kind ||
                (*Old)->NodeKey != Row.NodeKey)
            { return FailValue(OutReason, TEXT("Unsupported visual value change")); }
            auto Frozen = **Old;
            Frozen.ActorPose = Row.ActorPose;
            if (!FCkSelectionGym_VisualRow::StaticStruct()->
                    CompareScriptStruct(&Frozen, &Row, PPF_None))
            { return FailValue(OutReason, TEXT("Unsupported visual style change")); }
        }
        FString PhysicalReason;
        if (!CompletePhysical(S, S.Installed, true, PhysicalReason))
        { return FailClosedMirrorLease(Lease, TEXT("Installed bank changed before update"), OutReason); }
        for (const auto& Row : S.Installed.Nodes)
        {
            if (!UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(FindNode(S, Row.Key)))
            { return FailClosedMirrorLease(Lease, TEXT("Installed source admission closed unexpectedly"), OutReason); }
        }
        TArray<FCk_DebugOverlay_SourceObservation> Facts;
        if (!BuildObservations(S, Snapshot, Facts))
        { return FailClosedMirrorLease(Lease, TEXT("Provider fact mapping failed"), OutReason); }
        if (!ApplyMotion(S, Snapshot))
        { return FailClosedMirrorLease(Lease, TEXT("Moving frame mutation failed"), OutReason); }
        S.Pending = Snapshot;
        FCk_Handle ExactBeforePublish;
        if (!Context(World, ExactGameState, ExactBeforePublish) ||
            ExactBeforePublish != WorldRoot ||
            !CompletePhysical(S, Snapshot, true, PhysicalReason) ||
            !FCk_DebugOverlay_ObservationBankUtils::TryPublish(
                World, WorldRoot, FindNode(S, Snapshot.ScenarioRootKey),
                static_cast<uint64>(S.AdmissionGeneration),
                static_cast<uint64>(Snapshot.Sequence), Facts))
        { return FailClosedMirrorLease(Lease, TEXT("Moving frame physical or provider publication failed"), OutReason); }
        FCk_Handle ExactAfterPublish;
        if (!Context(World, ExactGameState, ExactAfterPublish) ||
            ExactAfterPublish != WorldRoot ||
            !ProveMirrorMutationLease(Lease))
        { return FailClosedMirrorLease(Lease, TEXT("Moving frame binding changed during publication"), OutReason); }
        S.Installed = Snapshot;
        FString AcceptedPhysical;
        if (!CompletePhysical(S, S.Installed, true, AcceptedPhysical) ||
            !PublishedFactsMatch(S))
        { return FailClosedMirrorLease(Lease, TEXT("Moving frame changed during publication"), OutReason); }
        return true;
    }

    auto& S = WorldRoot.AddOrGet<FState>();
    S.World = World;
    S.GameState = ExactGameState;
    S.WorldRoot = WorldRoot;
    S.AdmissionGeneration = Snapshot.ScenarioGeneration;
    const auto Lease = CaptureMirrorMutationLease(S);
    if (!StageNewBank(S, Snapshot, MarkerClass, WallClass, OutReason))
    { return false; }
    return TryAdvance(World, ExactGameState, OutReason);
}

bool UCkSelectionGym_Mirror_UE::TryAcceptEnvelope(UWorld* World,
    AGameStateBase* ExactGameState,
    const FCkSelectionGym_WireSnapshot& InputSnapshot, FString& OutReason)
{
    OutReason.Reset();
    const auto Snapshot = InputSnapshot;
    FCk_Handle WorldRoot;
    if (!Context(World, ExactGameState, WorldRoot))
    {
        CloseChangedBinding(World);
        return FailValue(OutReason, TEXT("Exact client context unavailable"));
    }
    const auto EmptyDeclaredCount = [](int32 Count)
    { return Count == 0 || Count == -1; };
    if (Snapshot.SchemaVersion != 1 || Snapshot.SourceGeneration <= 0 ||
        Snapshot.ScenarioGeneration <= 0 || Snapshot.Sequence < 0 ||
        (Snapshot.Lifecycle != ECkSelectionGym_WireLifecycle::Initializing &&
         Snapshot.Lifecycle != ECkSelectionGym_WireLifecycle::Retiring &&
         Snapshot.Lifecycle != ECkSelectionGym_WireLifecycle::Failed) ||
        !EmptyDeclaredCount(Snapshot.DeclaredNodeCount) ||
        !EmptyDeclaredCount(Snapshot.DeclaredVisualCount) ||
        !EmptyDeclaredCount(Snapshot.DeclaredTimerCount) ||
        !EmptyDeclaredCount(Snapshot.DeclaredIntegerCount) ||
        !EmptyDeclaredCount(Snapshot.DeclaredStateMachineCount) ||
        !Snapshot.Nodes.IsEmpty() || !Snapshot.Visuals.IsEmpty() ||
        !Snapshot.Timers.IsEmpty() || !Snapshot.Integers.IsEmpty() ||
        !Snapshot.StateMachines.IsEmpty() ||
        (Snapshot.Lifecycle == ECkSelectionGym_WireLifecycle::Failed) !=
            !Snapshot.FailureReason.IsEmpty())
    {
        if (WorldRoot.Has<FState>())
        {
            auto& S = WorldRoot.Get<FState>();
            if (Bound(S, World, ExactGameState, WorldRoot))
            { CloseFailed(S, TEXT("Malformed lifecycle envelope")); }
        }
        return FailValue(OutReason, TEXT("Malformed lifecycle envelope"));
    }
    if (!WorldRoot.Has<FState>())
    {
        if (Snapshot.Lifecycle != ECkSelectionGym_WireLifecycle::Failed)
        { return true; }
        auto& S = WorldRoot.AddOrGet<FState>();
        S.World = World;
        S.GameState = ExactGameState;
        S.SpawnOwnerIdentity = ExactGameState;
        S.WorldRoot = WorldRoot;
        S.AcceptedFailedEnvelope = Snapshot;
        S.HasAcceptedFailedEnvelope = true;
        CloseFailed(S, *Snapshot.FailureReason);
        return true;
    }
    auto& S = WorldRoot.Get<FState>();
    const auto Lease = CaptureMirrorMutationLease(S);
    if (S.Phase == ck::ESelectionMirrorPhase::Failed)
    {
        const bool ExactReplay =
            S.World.Get() == World && S.GameState.Get() == ExactGameState &&
            S.WorldRoot == WorldRoot && S.HasAcceptedFailedEnvelope &&
            FCkSelectionGym_WireSnapshot::StaticStruct()->CompareScriptStruct(
                &Snapshot, &S.AcceptedFailedEnvelope, PPF_None);
        return ExactReplay ||
            FailValue(OutReason, TEXT("Failed mirror accepts only its exact terminal replay"));
    }
    if (!Bound(S, World, ExactGameState, WorldRoot))
    { return FailValue(OutReason, TEXT("Mirror session failed or bound elsewhere")); }
    if (Snapshot.SourceGeneration < S.Installed.SourceGeneration ||
        (Snapshot.SourceGeneration == S.Installed.SourceGeneration &&
         Snapshot.ScenarioGeneration < S.Installed.ScenarioGeneration))
    { return FailValue(OutReason, TEXT("Stale lifecycle generation")); }
    if (Snapshot.Lifecycle == ECkSelectionGym_WireLifecycle::Failed)
    {
        const bool OlderThanEnvelope =
            Snapshot.SourceGeneration < S.LastEnvelopeSourceGeneration ||
            (Snapshot.SourceGeneration == S.LastEnvelopeSourceGeneration &&
             Snapshot.ScenarioGeneration < S.LastEnvelopeScenarioGeneration);
        const bool SameEnvelopeGeneration =
            Snapshot.SourceGeneration == S.LastEnvelopeSourceGeneration &&
            Snapshot.ScenarioGeneration == S.LastEnvelopeScenarioGeneration;
        const bool SameInstalledGeneration =
            Snapshot.SourceGeneration == S.Installed.SourceGeneration &&
            Snapshot.ScenarioGeneration == S.Installed.ScenarioGeneration;
        if (OlderThanEnvelope ||
            (SameEnvelopeGeneration && Snapshot.Sequence <= S.LastEnvelopeSequence) ||
            (SameInstalledGeneration && Snapshot.Sequence <= S.Installed.Sequence))
        { return FailValue(OutReason, TEXT("Failed envelope replayed or preceded accepted bank")); }
        S.LastEnvelopeSourceGeneration = Snapshot.SourceGeneration;
        S.LastEnvelopeScenarioGeneration = Snapshot.ScenarioGeneration;
        S.LastEnvelopeSequence = Snapshot.Sequence;
        S.AcceptedFailedEnvelope = Snapshot;
        S.HasAcceptedFailedEnvelope = true;
        CloseFailed(S, *Snapshot.FailureReason);
        return true;
    }
    if (S.Phase == ck::ESelectionMirrorPhase::Installed)
    {
        const bool SameGeneration =
            Snapshot.SourceGeneration == S.Installed.SourceGeneration &&
            Snapshot.ScenarioGeneration == S.Installed.ScenarioGeneration;
        const bool NewGeneration =
            Snapshot.SourceGeneration > S.Installed.SourceGeneration ||
            (Snapshot.SourceGeneration == S.Installed.SourceGeneration &&
             Snapshot.ScenarioGeneration > S.Installed.ScenarioGeneration);
        if (!((Snapshot.Lifecycle == ECkSelectionGym_WireLifecycle::Retiring &&
               SameGeneration && Snapshot.Sequence > S.Installed.Sequence) ||
              (Snapshot.Lifecycle == ECkSelectionGym_WireLifecycle::Initializing &&
               NewGeneration)))
        { return FailValue(OutReason, TEXT("Lifecycle envelope contradicts installed bank")); }
        return HoldRetiring(S, Snapshot, OutReason);
    }
    if (S.Phase == ck::ESelectionMirrorPhase::HoldingRetiring)
    {
        const bool NewGeneration =
            Snapshot.SourceGeneration > S.LastEnvelopeSourceGeneration ||
            (Snapshot.SourceGeneration == S.LastEnvelopeSourceGeneration &&
             Snapshot.ScenarioGeneration > S.LastEnvelopeScenarioGeneration);
        if (Snapshot.Lifecycle != ECkSelectionGym_WireLifecycle::Initializing ||
            !NewGeneration)
        { return FailValue(OutReason, TEXT("Lifecycle envelope replayed or contradicted held bank")); }
        S.LastEnvelopeSourceGeneration = Snapshot.SourceGeneration;
        S.LastEnvelopeScenarioGeneration = Snapshot.ScenarioGeneration;
        S.LastEnvelopeSequence = Snapshot.Sequence;
        return true;
    }
    if (S.Phase == ck::ESelectionMirrorPhase::RetiringAll ||
        S.Phase == ck::ESelectionMirrorPhase::AwaitingNew)
    {
        const bool NewGeneration =
            Snapshot.SourceGeneration > S.Installed.SourceGeneration ||
            (Snapshot.SourceGeneration == S.Installed.SourceGeneration &&
             Snapshot.ScenarioGeneration > S.Installed.ScenarioGeneration);
        const bool OlderThanEnvelope =
            Snapshot.SourceGeneration < S.LastEnvelopeSourceGeneration ||
            (Snapshot.SourceGeneration == S.LastEnvelopeSourceGeneration &&
             Snapshot.ScenarioGeneration < S.LastEnvelopeScenarioGeneration);
        const bool SameEnvelopeGeneration =
            Snapshot.SourceGeneration == S.LastEnvelopeSourceGeneration &&
            Snapshot.ScenarioGeneration == S.LastEnvelopeScenarioGeneration;
        if (Snapshot.Lifecycle != ECkSelectionGym_WireLifecycle::Initializing ||
            !NewGeneration || OlderThanEnvelope ||
            (SameEnvelopeGeneration && Snapshot.Sequence <= S.LastEnvelopeSequence))
        { return FailValue(OutReason, TEXT("Lifecycle envelope contradicts full retirement")); }
        S.LastEnvelopeSourceGeneration = Snapshot.SourceGeneration;
        S.LastEnvelopeScenarioGeneration = Snapshot.ScenarioGeneration;
        S.LastEnvelopeSequence = Snapshot.Sequence;
        return true;
    }
    return FailClosedMirrorLease(Lease,
        TEXT("Lifecycle envelope overtook incomplete local bank"), OutReason);
}

bool UCkSelectionGym_Mirror_UE::TryAdvance(UWorld* World,
    AGameStateBase* ExactGameState, FString& OutReason)
{
    OutReason.Reset();
    FCk_Handle WorldRoot;
    if (!Context(World, ExactGameState, WorldRoot) || !WorldRoot.Has<FState>())
    {
        CloseChangedBinding(World);
        return FailValue(OutReason, TEXT("No exact client mirror session"));
    }
    auto& S = WorldRoot.Get<FState>();
    const auto Lease = CaptureMirrorMutationLease(S);
    if (!Bound(S, World, ExactGameState, WorldRoot))
    { return FailValue(OutReason, TEXT("Client mirror session failed or changed owner")); }
    if (S.Phase == ck::ESelectionMirrorPhase::Installed) { return true; }
    if (FPlatformTime::Seconds() > S.Deadline)
    { return FailClosedMirrorLease(Lease, TEXT("Local materialization deadline expired"), OutReason); }
    if (S.Phase == ck::ESelectionMirrorPhase::HoldingRetiring)
    { return FailValue(OutReason, TEXT("Retiring bank awaits a complete Active replacement")); }
    if (S.Phase == ck::ESelectionMirrorPhase::RetiringAll)
    {
        if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, S.FrozenAll))
        { return FailClosedMirrorLease(Lease, TEXT("Full retirement membership changed"), OutReason); }
        for (const auto& H : S.FrozenAll)
        {
            if (Allocated(H))
            { return FailValue(OutReason, TEXT("Old source forest retirement pending")); }
        }
        for (const auto& Pair : S.Visuals)
        {
            if (IsValid(Pair.Value.Get()))
            { return FailValue(OutReason, TEXT("Old source visual retirement pending")); }
        }
        if (S.AdmissionGeneration == MAX_int64)
        { return FailClosedMirrorLease(Lease, TEXT("Local admission generation exhausted"), OutReason); }
        S.Nodes.Reset();
        S.Visuals.Reset();
        S.DirectRoots.Reset();
        S.FrozenAll.Reset();
        S.ScopeBegun = false;
        ++S.AdmissionGeneration;
        S.Phase = ck::ESelectionMirrorPhase::AwaitingNew;
        if (!S.HasPendingActive)
        { return FailValue(OutReason, TEXT("Awaiting next complete Active generation")); }
        const auto Next = S.Pending;
        const auto Marker = S.MarkerClass;
        const auto Wall = S.WallClass;
        S.HasPendingActive = false;
        if (!StageNewBank(S, Next, Marker, Wall, OutReason))
        { return false; }
        if (!ProveMirrorMutationLease(Lease))
        { return FailClosedMirrorLease(Lease,
            TEXT("New local bank changed mirror lifetime"), OutReason); }
    }
    if (S.Phase == ck::ESelectionMirrorPhase::RetiringLeaf)
    {
        if (Allocated(S.RetiringLeaf))
        {
            if (!ProveMirrorMutationLease(Lease) || !ProveFrozenContext(S, {S.RetiringLeaf}))
            { return FailClosedMirrorLease(Lease, TEXT("X frozen retirement membership changed"), OutReason); }
            return FailValue(OutReason, TEXT("X old leaf retirement pending"));
        }
        if (IsValid(S.RetiringVisual.Get()))
        { return FailValue(OutReason, TEXT("X old marker retirement pending")); }
        S.Nodes.Remove(S.RetiringLeafKey);
        S.Visuals.Remove(S.RetiringVisualKey);
        const FCkSelectionGym_NodeRow* NewNode = nullptr;
        const FCkSelectionGym_VisualRow* NewVisual = nullptr;
        for (const auto& Row : S.Pending.Nodes)
        { if (!S.Nodes.Contains(Row.Key)) { if (NewNode) { return FailClosedMirrorLease(Lease, TEXT("X has multiple new nodes"), OutReason); } NewNode = &Row; } }
        for (const auto& Row : S.Pending.Visuals)
        { if (!S.Visuals.Contains(Row.VisualKey)) { if (NewVisual) { return FailClosedMirrorLease(Lease, TEXT("X has multiple new visuals"), OutReason); } NewVisual = &Row; } }
        if (!NewNode || !NewVisual ||
            NewNode->LifetimeParentKey != S.Pending.ScenarioRootKey ||
            NewVisual->NodeKey != NewNode->Key)
        { return FailClosedMirrorLease(Lease, TEXT("X pending leaf value changed"), OutReason); }
        auto Parent = FindNode(S, NewNode->LifetimeParentKey);
        if (!Live(Parent) || !Direct(Parent, WorldRoot, WorldRoot))
        { return FailClosedMirrorLease(Lease, TEXT("X surviving root ownership lost"), OutReason); }
        const auto FrozenNewNode = *NewNode;
        const auto FrozenNewVisual = *NewVisual;
        const auto MarkerClass = S.MarkerClass;
        const auto WallClass = S.WallClass;
        auto NewLeaf = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Parent);
        auto* Retained = FindMirrorCapturedState(Lease);
        if (Retained == nullptr)
        { return FailClosedMirrorLease(Lease,
            TEXT("X new leaf creation changed mirror lifetime"), OutReason); }
        Retained->Nodes.Add(FrozenNewNode.Key, NewLeaf);
        TSet<int64> ComposedSurvivors;
        for (const auto& Row : S.Pending.Nodes)
        { if (Row.Key != FrozenNewNode.Key) { ComposedSurvivors.Add(Row.Key); } }
        if (!ProveMirrorMutationLease(Lease) ||
            !Direct(NewLeaf, Parent, WorldRoot) ||
            !ComposeNode(S, FrozenNewNode, ComposedSurvivors) ||
            !ComposeVisual(S, FrozenNewVisual, MarkerClass, WallClass) ||
            !ApplyMotion(S, S.Pending))
        { return FailClosedMirrorLease(Lease, TEXT("X replacement composition failed"), OutReason); }
        S.RetiringLeaf = {};
        S.RetiringVisual.Reset();
        S.RetiringLeafKey = 0;
        S.RetiringVisualKey = 0;
        S.Phase = ck::ESelectionMirrorPhase::Staging;
    }
    if (!CompletePhysical(S, S.Pending, false, OutReason))
    {
        // A newly made SceneNode may wait for ordered propagation. Every other
        // census or actor failure is terminal, never a partially installed bank.
        if (OutReason != TEXT("Local transform not settled") &&
            OutReason != TEXT("Local SceneNode not settled"))
        {
            const FString PhysicalFailure =
                TEXT("Local bank physical proof failed: ") + OutReason;
            return FailClosedMirrorLease(Lease, *PhysicalFailure, OutReason);
        }
        return false;
    }
    TArray<FCk_DebugOverlay_SourceObservation> Facts;
    FCk_Handle ExactBeforePublish;
    if (!Context(World, ExactGameState, ExactBeforePublish) ||
        ExactBeforePublish != WorldRoot ||
        !BuildObservations(S, S.Pending, Facts) ||
        !FCk_DebugOverlay_ObservationBankUtils::TryPublish(
            World, WorldRoot, FindNode(S, S.Pending.ScenarioRootKey),
            static_cast<uint64>(S.AdmissionGeneration),
            static_cast<uint64>(S.Pending.Sequence), Facts))
    { return FailClosedMirrorLease(Lease, TEXT("Passive provider bank publication failed"), OutReason); }
    FCk_Handle ExactAfterFacts;
    if (!Context(World, ExactGameState, ExactAfterFacts) ||
        ExactAfterFacts != WorldRoot ||
        !ProveMirrorMutationLease(Lease))
    { return FailClosedMirrorLease(Lease, TEXT("Binding changed during provider publication"), OutReason); }
    TArray<FCk_Handle> Members;
    Members.Reserve(S.Pending.Nodes.Num());
    for (const auto& Row : S.Pending.Nodes) { Members.Add(FindNode(S, Row.Key)); }
    const auto RevealFrame = S.Pending;
    const auto RevealLease = CaptureMirrorMutationLease(S);
    TSet<int64> RevealContexts;
    for (const auto& Row : RevealFrame.Nodes) { RevealContexts.Add(Row.Key); }
    for (const auto& Row : RevealFrame.Visuals)
    {
        if (!ProveMirrorMutationNodes(RevealLease, RevealFrame, RevealContexts) ||
            !FCkSelectionGym_WireSnapshot::StaticStruct()->
                CompareScriptStruct(&S.Pending, &RevealFrame, PPF_None) ||
            !ProveMirrorMutationVisuals(RevealLease, RevealFrame))
        { return FailClosedMirrorLease(Lease, TEXT("Visual receipt lost before reveal"), OutReason); }
        const auto* Weak = S.Visuals.Find(Row.VisualKey);
        auto* Actor = Weak != nullptr ? Weak->Get() : nullptr;
        Actor->SetActorHiddenInGame(false);
        if (!ProveMirrorMutationNodes(RevealLease, RevealFrame, RevealContexts) ||
            !FCkSelectionGym_WireSnapshot::StaticStruct()->
                CompareScriptStruct(&S.Pending, &RevealFrame, PPF_None) ||
            !ProveMirrorMutationVisuals(RevealLease, RevealFrame))
        { return FailClosedMirrorLease(Lease, TEXT("Visual hide callback changed provenance"), OutReason); }
        Actor->SetActorEnableCollision(Row.ActorCollisionEnabled);
        if (!ProveMirrorMutationNodes(RevealLease, RevealFrame, RevealContexts) ||
            !FCkSelectionGym_WireSnapshot::StaticStruct()->
                CompareScriptStruct(&S.Pending, &RevealFrame, PPF_None) ||
            !ProveMirrorMutationVisuals(RevealLease, RevealFrame))
        { return FailClosedMirrorLease(Lease, TEXT("Visual collision callback changed provenance"), OutReason); }
        if (Actor->IsHidden() ||
            Actor->GetActorEnableCollision() != Row.ActorCollisionEnabled)
        { return FailClosedMirrorLease(Lease, TEXT("Visual reveal did not settle"), OutReason); }
    }
    FCk_Handle ExactBeforeAdmission;
    FString RevealedPhysical;
    if (!Context(World, ExactGameState, ExactBeforeAdmission) ||
        ExactBeforeAdmission != WorldRoot ||
        !ProveMirrorMutationLease(Lease) ||
        !CompletePhysical(S, S.Pending, true, RevealedPhysical))
    { return FailClosedMirrorLease(Lease, TEXT("Whole bank changed during visual reveal"), OutReason); }
    if (!UCk_DebugOverlay_SourceAdmission_UE::TryPublish(
            World, WorldRoot, S.AdmissionGeneration, Members, OutReason))
    { return FailClosedMirrorLease(Lease,
        TEXT("Whole mirror admission failed"), OutReason); }
    FCk_Handle ExactAfterAdmission;
    if (!Context(World, ExactGameState, ExactAfterAdmission) ||
        ExactAfterAdmission != WorldRoot ||
        !ProveMirrorMutationLease(Lease))
    { return FailClosedMirrorLease(Lease, TEXT("Binding changed during mirror admission"), OutReason); }
    S.Installed = S.Pending;
    S.Phase = ck::ESelectionMirrorPhase::Installed;
    FString AcceptedPhysical;
    if (!CompletePhysical(S, S.Installed, true, AcceptedPhysical) ||
        !PublishedFactsMatch(S))
    { return FailClosedMirrorLease(Lease, TEXT("Accepted mirror bank changed during publication"), OutReason); }
    return true;
}

bool UCkSelectionGym_Mirror_UE::TryGetInstalled(UWorld* World,
    AGameStateBase* ExactGameState, FCkSelectionGym_WireSnapshot& OutSnapshot,
    FCk_Handle& OutScenarioRoot, FString& OutReason)
{
    OutSnapshot = {};
    OutScenarioRoot = {};
    OutReason.Reset();
    FCk_Handle WorldRoot;
    if (!Context(World, ExactGameState, WorldRoot) || !WorldRoot.Has<FState>())
    {
        CloseChangedBinding(World);
        return FailValue(OutReason, TEXT("Exact client mirror unavailable"));
    }
    const auto& S = WorldRoot.Get<FState>();
    if (!Bound(S, World, ExactGameState, WorldRoot) ||
        S.Phase != ck::ESelectionMirrorPhase::Installed)
    { return FailValue(OutReason, TEXT("Mirror is not fully installed")); }
    FString PhysicalReason;
    if (!CompletePhysical(S, S.Installed, true, PhysicalReason))
    { return FailValue(OutReason, TEXT("Installed mirror physical proof failed")); }
    for (const auto& Row : S.Installed.Nodes)
    {
        if (!UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(FindNode(S, Row.Key)))
        { return FailValue(OutReason, TEXT("Installed mirror source admission closed")); }
    }
    if (!PublishedFactsMatch(S))
    { return FailValue(OutReason, TEXT("Installed mirror provider bank changed")); }
    OutSnapshot = S.Installed;
    OutScenarioRoot = FindNode(S, S.Installed.ScenarioRootKey);
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool UCkSelectionGym_Mirror_UE::TryGetLocalNodeForTest(UWorld* World,
    AGameStateBase* ExactGameState, int64 NodeKey, FCk_Handle& OutNode)
{
    OutNode = {};
    if (NodeKey <= 0) { return false; }
    FCk_Handle WorldRoot;
    if (!Context(World, ExactGameState, WorldRoot) || !WorldRoot.Has<FState>())
    { return false; }
    const auto& S = WorldRoot.Get<FState>();
    if (!Bound(S, World, ExactGameState, WorldRoot) ||
        S.Phase != ck::ESelectionMirrorPhase::Installed ||
        !S.Installed.Nodes.ContainsByPredicate(
            [NodeKey](const FCkSelectionGym_NodeRow& Row)
            { return Row.Key == NodeKey; }))
    { return false; }
    FString Why;
    if (!CompletePhysical(S, S.Installed, true, Why) ||
        !PublishedFactsMatch(S))
    { return false; }
    const FCk_Handle* Local = S.Nodes.Find(NodeKey);
    if (Local == nullptr || !Live(*Local) ||
        !UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(*Local))
    { return false; }
    OutNode = *Local;
    return true;
}
#endif

bool UCkSelectionGym_Mirror_UE::TryGetStatus(UWorld* World,
    AGameStateBase* ExactGameState,
    ECkSelectionGym_MirrorStatus& OutStatus, FString& OutReason)
{
    OutStatus = ECkSelectionGym_MirrorStatus::Unavailable;
    OutReason.Reset();
    FCk_Handle WorldRoot;
    if (!Context(World, ExactGameState, WorldRoot) || !WorldRoot.Has<FState>())
    {
        CloseChangedBinding(World);
        return FailValue(OutReason, TEXT("Exact client mirror session unavailable"));
    }
    const auto& S = WorldRoot.Get<FState>();
    if (S.World.Get() != World || S.GameState.Get() != ExactGameState ||
        S.WorldRoot != WorldRoot)
    { return FailValue(OutReason, TEXT("Client mirror belongs to another exact session")); }
    if (S.Phase == ck::ESelectionMirrorPhase::Failed)
    {
        OutStatus = ECkSelectionGym_MirrorStatus::Failed;
        OutReason = S.Failure;
        return true;
    }
    OutStatus = S.Phase == ck::ESelectionMirrorPhase::Installed
        ? ECkSelectionGym_MirrorStatus::Installed
        : ECkSelectionGym_MirrorStatus::Pending;
    return true;
}

#else

#if WITH_DEV_AUTOMATION_TESTS
bool UCkSelectionGym_Mirror_UE::TryGetLocalNodeForTest(UWorld*,
    AGameStateBase*, int64, FCk_Handle& OutNode)
{
    OutNode = {};
    return false;
}
#endif

bool UCkSelectionGym_Mirror_UE::TryAcceptActive(UWorld*, AGameStateBase*,
    const FCkSelectionGym_WireSnapshot&, TSubclassOf<AActor>,
    TSubclassOf<AActor>, FString& OutReason)
{
    OutReason = TEXT("Selection debug mirror is unavailable in Shipping");
    return false;
}

bool UCkSelectionGym_Mirror_UE::TryAcceptEnvelope(UWorld*, AGameStateBase*,
    const FCkSelectionGym_WireSnapshot&, FString& OutReason)
{
    OutReason = TEXT("Selection debug mirror is unavailable in Shipping");
    return false;
}

bool UCkSelectionGym_Mirror_UE::TryAdvance(UWorld*, AGameStateBase*,
    FString& OutReason)
{
    OutReason = TEXT("Selection debug mirror is unavailable in Shipping");
    return false;
}

bool UCkSelectionGym_Mirror_UE::TryGetInstalled(UWorld*, AGameStateBase*,
    FCkSelectionGym_WireSnapshot& OutSnapshot,
    FCk_Handle& OutScenarioRoot, FString& OutReason)
{
    OutSnapshot = {};
    OutScenarioRoot = {};
    OutReason = TEXT("Selection debug mirror is unavailable in Shipping");
    return false;
}

bool UCkSelectionGym_Mirror_UE::TryGetStatus(UWorld*, AGameStateBase*,
    ECkSelectionGym_MirrorStatus& OutStatus, FString& OutReason)
{
    OutStatus = ECkSelectionGym_MirrorStatus::Unavailable;
    OutReason = TEXT("Selection debug mirror is unavailable in Shipping");
    return false;
}

#endif
