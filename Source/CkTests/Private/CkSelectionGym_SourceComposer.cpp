#include "CkSelectionGym_SourceComposer.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/ContextOwner/CkContextOwner_Utils.h"
#include "CkEcs/ContextOwner/CkContextOwner_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Handle/CkHandle_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkLabel/CkLabel_Utils.h"
#include "CkTimer/CkTimer_Utils.h"
#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Utils.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment.h"
#include "CkStateMachine/State/EntityScripts/CkSmState_EntityScript.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    auto Allocated(const FCk_Handle& H) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    auto ExactAuthority(UWorld* World, AGameStateBase* GS,
        const FCk_Handle& Root) -> bool
    {
        return IsInGameThread() && IsValid(World) && IsValid(GS) &&
            !World->HasAnyFlags(RF_ClassDefaultObject | RF_BeginDestroyed | RF_FinishDestroyed) &&
            World->IsGameWorld() && !World->bIsTearingDown &&
            !World->IsBeingCleanedUp() && World->GetGameState() == GS &&
            GS->GetWorld() == World && !GS->IsActorBeingDestroyed() &&
            GS->HasAuthority() && Allocated(Root) && ck::IsValid(Root) &&
            UCk_Utils_EcsWorld_Subsystem_UE::
                TryGet_TransientEntityForWorld(*World) == Root;
    }

    auto ExactCapturedWorld(UWorld* World, const FCk_Handle& Root) -> bool
    {
        return IsInGameThread() && IsValid(World) &&
            !World->HasAnyFlags(RF_ClassDefaultObject | RF_BeginDestroyed | RF_FinishDestroyed) &&
            World->IsGameWorld() && !World->bIsTearingDown &&
            !World->IsBeingCleanedUp() && Allocated(Root) && ck::IsValid(Root) &&
            Root.Has<TWeakObjectPtr<UWorld>>() &&
            Root.Get<TWeakObjectPtr<UWorld>>().Get() == World &&
            UCk_Utils_EcsWorld_Subsystem_UE::
                TryGet_TransientEntityForWorld(*World) == Root;
    }

    auto ValidOwner(UWorld* World, AGameStateBase* GS, AActor* Actor) -> bool
    {
        return IsValid(Actor) && !Actor->IsActorBeingDestroyed() &&
            Actor->GetWorld() == World && Actor->GetOwner() == GS;
    }
}

bool UCkSelectionGym_SourceComposer_UE::ProveKnown(
    const UCkSelectionGym_SourceComposition* C)
{
    if (!IsValid(C) ||
        !ExactAuthority(C->World.Get(), C->GameState.Get(), C->WorldRoot) ||
        C->AuthoredNodes.Num() != C->AuthoredParents.Num())
    { return false; }
    const auto Registry = C->WorldRoot.Get_RegistryView().Get_RegistryHandle();
    TSet<FCk_Handle> Seen;
    for (int32 I = 0; I < C->AuthoredNodes.Num(); ++I)
    {
        const auto& H = C->AuthoredNodes[I];
        const auto& Parent = C->AuthoredParents[I];
        if (!Allocated(H) || !ck::IsValid(H) || Seen.Contains(H) ||
            H.Has_Any<ck::FTag_DestroyEntity_Initiate,
                ck::FTag_DestroyEntity_EndPlay, ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>() ||
            H.Get_RegistryView().Get_RegistryHandle() != Registry ||
            !H.Has<ck::FFragment_LifetimeOwner>() ||
            H.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Parent ||
            (Parent != C->WorldRoot && !Seen.Contains(Parent)))
        { return false; }
        Seen.Add(H);
    }
    return true;
}

bool UCkSelectionGym_SourceComposer_UE::Fail(
    UCkSelectionGym_SourceComposition* C, const FString& Reason,
    FString& OutReason)
{
    OutReason = Reason;
    if (!IsValid(C)) { return false; }
    C->Failed = true;
    if (IsValid(C->Session))
    {
        FString Closure;
        if (!UCkSelectionGym_SourceSession_UE::TryFail(
            C->Session, Reason, Closure) && !Closure.IsEmpty())
        { OutReason += TEXT("; ") + Closure; }
    }
    // Actors created before the session accepted their receipt stay hidden
    // and collision-free. Their exact world and owner must still be proven.
    auto* World = C->World.Get();
    if (ExactCapturedWorld(World, C->WorldRoot))
    {
        for (const auto& R : C->Visuals)
        {
            auto* Actor = R.Actor.Get();
            const UClass* Expected = R.Kind ==
                ECkSelectionGym_VisualKind::OcclusionWall ?
                    C->WallClass.Get() : C->MarkerClass.Get();
            auto ExactActor = [&]() -> bool
            {
                if (!ExactCapturedWorld(World, C->WorldRoot) ||
                    !IsValid(Actor) || Actor->IsActorBeingDestroyed() ||
                    Actor->GetWorld() != World ||
                    Actor->GetClass() != Expected || Actor->GetOwner() == nullptr)
                { return false; }
                const TWeakObjectPtr<AActor> OwnerIdentity(Actor->GetOwner());
                return OwnerIdentity.HasSameIndexAndSerialNumber(C->GameState);
            };
            if (!ExactActor()) { continue; }
            Actor->SetActorHiddenInGame(true);
            if (ExactActor())
            { Actor->SetActorEnableCollision(false); }
        }
    }
    return false;
}

bool UCkSelectionGym_SourceComposer_UE::TryCompose(
    UWorld* World, AGameStateBase* ExactGameState,
    const FCk_Handle& WorldRoot, int64 SourceGeneration,
    int64 ScenarioGeneration, int64 AdmissionGeneration, int32 Scenario,
    FVector Origin, TSubclassOf<AActor> MarkerClass,
    TSubclassOf<AActor> WallClass,
    TSubclassOf<UCk_SmState_EntityScript> IdleStateClass,
    UCkSelectionGym_SourceComposition*& OutComposition,
    FString& OutReason)
{
    OutComposition = nullptr;
    OutReason.Reset();
#if UE_BUILD_SHIPPING
    OutReason = TEXT("Selection gym source composition unavailable in Shipping");
    return false;
#else
    if (!ExactAuthority(World, ExactGameState, WorldRoot) ||
        Scenario < 0 || Scenario > 4 || SourceGeneration <= 0 ||
        ScenarioGeneration <= 0 || AdmissionGeneration <= 0 ||
        !FMath::IsFinite(Origin.X) || !FMath::IsFinite(Origin.Y) ||
        !FMath::IsFinite(Origin.Z) || !IsValid(MarkerClass.Get()) ||
        !IsValid(WallClass.Get()) || MarkerClass == WallClass ||
        !IsValid(IdleStateClass.Get()) ||
        MarkerClass.Get()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
        WallClass.Get()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
        IdleStateClass.Get()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    { OutReason = TEXT("Invalid authority composer context or classes"); return false; }
    const auto RootLabel = FGameplayTag::RequestGameplayTag(
        FName(TEXT("Ck.Debug.Selection.Root")), false);
    const auto CounterLabel = FGameplayTag::RequestGameplayTag(
        FName(TEXT("Ck.Debug.Selection.Composite.Counter")), false);
    auto* Cube = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!RootLabel.IsValid() || !CounterLabel.IsValid() || !IsValid(Cube))
    { OutReason = TEXT("Authored Selection tag or cube asset unavailable"); return false; }

    auto* C = NewObject<UCkSelectionGym_SourceComposition>(ExactGameState);
    if (!IsValid(C))
    { OutReason = TEXT("Source composition object unavailable"); return false; }
    OutComposition = C;
    C->World = World;
    C->GameState = ExactGameState;
    C->WorldRoot = WorldRoot;
    C->Scenario = Scenario;
    C->Origin = Origin;
    C->MarkerClass = MarkerClass;
    C->WallClass = WallClass;
    auto Abort = [&](const TCHAR* Why) -> bool
    {
        const FString Cause = OutReason.IsEmpty() ? FString(Why) : OutReason;
        return Fail(C, Cause, OutReason);
    };

    // Bare roots are first-return receipts. Begin registers the complete root
    // set before any root is given a selectable label or Transform.
    const int32 RootCount = Scenario == 0 ? 3 : 1;
    for (int32 I = 0; I < RootCount; ++I)
    {
        const auto Root = UCk_Utils_EntityLifetime_UE::
            Request_CreateEntity(WorldRoot);
        C->BareRoots.Add(Root);
        C->AuthoredNodes.Add(Root);
        C->AuthoredParents.Add(WorldRoot);
        if (!Allocated(Root))
        { return Abort(TEXT("Bare source root creation failed")); }
    }
    UCkSelectionGym_SourceSession* BegunSession = nullptr;
    const bool Begun = UCkSelectionGym_SourceSession_UE::TryBegin(
        World, ExactGameState, WorldRoot, SourceGeneration,
        ScenarioGeneration, AdmissionGeneration, Scenario, Origin,
        C->BareRoots, MarkerClass, WallClass, IdleStateClass,
        BegunSession, OutReason);
    C->Session = BegunSession;
    if (!Begun)
    { return Fail(C, OutReason, OutReason); }
    UCk_Utils_Handle_UE::Set_DebugName(C->BareRoots[0],
        FName(TEXT("Selection.Gym.FixtureRoot")));
    if (!ProveKnown(C))
    { return Abort(TEXT("Bare source root changed after registration")); }

    auto NewNode = [&](const FCk_Handle& Parent, const TCHAR* Name)
        -> FCk_Handle
    {
        if (!Allocated(Parent) || !ck::IsValid(Parent)) { return {}; }
        auto H = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Parent);
        C->AuthoredNodes.Add(H); // first return retained even on failure
        C->AuthoredParents.Add(Parent);
        if (Allocated(H))
        { UCk_Utils_Handle_UE::Set_DebugName(H, FName(Name)); }
        return ProveKnown(C) ? H : FCk_Handle{};
    };
    auto AddTransform = [&](FCk_Handle& H, const FVector& Location) -> bool
    {
        if (!Allocated(H) || !ck::IsValid(H)) { return false; }
        const auto T = UCk_Utils_Transform_UE::Add(H,
            FTransform(FRotator::ZeroRotator, Location, FVector::OneVector),
            ECk_Replication::DoesNotReplicate);
        return Allocated(T) && ProveKnown(C);
    };
    auto Candidate = [&](const FCk_Handle& Parent, const TCHAR* Name,
        const FVector& Location) -> FCk_Handle
    {
        auto H = NewNode(Parent, Name);
        if (!Allocated(H) || !ck::IsValid(H)) { return H; }
        UCk_Utils_ContextOwner_UE::Request_OverrideToSelf(H, {});
        if (!ProveKnown(C) || !UCk_Utils_ContextOwner_UE::Has(H) ||
            H.Get<ck::FFragment_ContextOwner>().Get_Entity() != H ||
            !AddTransform(H, Location))
        { return {}; }
        UCk_Utils_GameplayLabel_UE::Add(H, RootLabel);
        if (!ProveKnown(C) || !UCk_Utils_GameplayLabel_UE::Has(H) ||
            !UCk_Utils_GameplayLabel_UE::MatchesExact(H, RootLabel))
        { return {}; }
        return H;
    };
    auto Visual = [&](const TCHAR* Label, const FVector& Location,
        const FCk_Handle& Node, bool Wall = false) -> bool
    {
        if (!Wall && (!Allocated(Node) || !ck::IsValid(Node)))
        { return false; }
        const FTransform Pose(FRotator::ZeroRotator, Location);
        AActor* Actor = World->SpawnActorDeferred<AActor>(
            Wall ? WallClass.Get() : MarkerClass.Get(), Pose,
            ExactGameState, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FCkSelectionGym_SourceVisualReceipt R;
        R.VisualKey = C->NextVisualKey++;
        R.Kind = Wall ? ECkSelectionGym_VisualKind::OcclusionWall :
            ECkSelectionGym_VisualKind::LabelMarker;
        R.Node = Wall ? FCk_Handle{} : Node;
        R.Actor = Actor;
        C->Visuals.Add(R); // retain actor before finishing its construction
        auto ExactActor = [&]() -> bool
        {
            return ProveKnown(C) &&
                ValidOwner(World, ExactGameState, Actor) &&
                Actor->GetClass() ==
                    (Wall ? WallClass.Get() : MarkerClass.Get());
        };
        if (!ExactActor()) { return false; }
        Actor->SetActorHiddenInGame(true);
        if (!ExactActor()) { return false; }
        Actor->SetActorEnableCollision(false);
        if (!ExactActor() ||
            UGameplayStatics::FinishSpawningActor(Actor, Pose) != Actor ||
            !ExactActor())
        { return false; }
        TArray<UStaticMeshComponent*> Meshes;
        Actor->GetComponents<UStaticMeshComponent>(Meshes);
        if (Meshes.Num() != 1 || !IsValid(Meshes[0]) ||
            Meshes[0]->IsBeingDestroyed() || !Meshes[0]->IsRegistered())
        { return false; }
        auto* Mesh = Meshes[0];
        R.Mesh = Mesh;
        C->Visuals.Last().Mesh = Mesh;
        Actor->SetActorHiddenInGame(true);
        if (!ExactActor()) { return false; }
        Actor->SetActorEnableCollision(false);
        if (!ExactActor() || !IsValid(Mesh) || Mesh->GetOwner() != Actor)
        { return false; }
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (!ExactActor() || !IsValid(Mesh) || Mesh->GetOwner() != Actor)
        { return false; }
        if (Wall)
        {
            Actor->SetActorScale3D(FVector(0.25f, 2.4f, 2.0f));
            if (!ExactActor()) { return false; }
        }
        else
        {
            TArray<UTextRenderComponent*> Texts;
            Actor->GetComponents<UTextRenderComponent>(Texts);
            if (Texts.Num() != 1 || !IsValid(Texts[0]) ||
                Texts[0]->IsBeingDestroyed() || !Texts[0]->IsRegistered())
            { return false; }
            R.Text = Texts[0];
            C->Visuals.Last().Text = Texts[0];
            Mesh->SetMobility(EComponentMobility::Movable);
            if (!ExactActor() || !IsValid(Mesh) || Mesh->GetOwner() != Actor)
            { return false; }
            Mesh->SetStaticMesh(Cube);
            if (!ExactActor() || !IsValid(Mesh) || Mesh->GetOwner() != Actor)
            { return false; }
            Mesh->SetRelativeScale3D(FVector(0.35f, 0.35f, 0.35f));
            if (!ExactActor() || !IsValid(Texts[0]) ||
                Texts[0]->GetOwner() != Actor)
            { return false; }
            Texts[0]->SetText(FText::FromString(Label));
        }
        if (!ExactActor() ||
            Mesh->GetOwner() != Actor || !Mesh->IsRegistered() ||
            !IsValid(Mesh->GetStaticMesh()) || !Actor->IsHidden() ||
            Actor->GetActorEnableCollision())
        { return false; }
        if (!UCkSelectionGym_SourceSession_UE::TryRetainVisual(
            C->Session, R, OutReason))
        { return false; }
        return true;
    };

    if (Scenario == 0)
    {
        const FVector A = Origin + FVector(-500, -220, 120);
        const FVector B = Origin + FVector(-500, 220, 120);
        auto RootA = C->BareRoots[1];
        auto RootB = C->BareRoots[2];
        UCk_Utils_Handle_UE::Set_DebugName(RootA,
            FName(TEXT("Selection.Topology.TransientA")));
        UCk_Utils_Handle_UE::Set_DebugName(RootB,
            FName(TEXT("Selection.Topology.TransientB")));
        UCk_Utils_ContextOwner_UE::Request_OverrideToSelf(RootA, {});
        if (!ProveKnown(C))
        { return Abort(TEXT("Topology root A context callback changed source")); }
        UCk_Utils_ContextOwner_UE::Request_OverrideToSelf(RootB, {});
        if (!ProveKnown(C) || !UCk_Utils_ContextOwner_UE::Has(RootA) ||
            !UCk_Utils_ContextOwner_UE::Has(RootB) ||
            RootA.Get<ck::FFragment_ContextOwner>().Get_Entity() != RootA ||
            RootB.Get<ck::FFragment_ContextOwner>().Get_Entity() != RootB ||
            !AddTransform(RootA, A) || !AddTransform(RootB, B))
        { return Abort(TEXT("Topology direct-root transform failed")); }
        UCk_Utils_GameplayLabel_UE::Add(RootA, RootLabel);
        if (!ProveKnown(C))
        { return Abort(TEXT("Topology root A label callback changed source")); }
        UCk_Utils_GameplayLabel_UE::Add(RootB, RootLabel);
        if (!ProveKnown(C) || !UCk_Utils_GameplayLabel_UE::Has(RootA) ||
            !UCk_Utils_GameplayLabel_UE::Has(RootB) ||
            !UCk_Utils_GameplayLabel_UE::MatchesExact(RootA, RootLabel) ||
            !UCk_Utils_GameplayLabel_UE::MatchesExact(RootB, RootLabel))
        { return Abort(TEXT("Topology direct-root label failed")); }
        if (!Visual(TEXT("Transient Root A"), A, RootA) ||
            !Visual(TEXT("Transient Root B"), B, RootB))
        { return Abort(TEXT("Topology direct-root visual failed")); }
        auto Parent = NewNode(C->BareRoots[0],
            TEXT("Selection.Topology.OrdinaryParent"));
        if (!Allocated(Parent))
        { return Abort(TEXT("Topology ordinary parent failed")); }
        const FVector N = Origin + FVector(-720, 0, 120);
        auto Nested = Candidate(Parent,
            TEXT("Selection.Topology.NestedIndependent"), N);
        if (!Allocated(Nested) || !Visual(
            TEXT("Nested independent root"), N, Nested))
        { return Abort(TEXT("Topology nested candidate failed")); }
    }
    else if (Scenario == 1)
    {
        const struct { const TCHAR* Label; const TCHAR* Name; FVector Offset; } Items[] = {
            {TEXT("Centered near"), TEXT("Selection.Distance.CenteredNear"), FVector(-500,0,120)},
            {TEXT("Co-located A"), TEXT("Selection.Distance.CoLocatedA"), FVector(-620,-130,120)},
            {TEXT("Co-located B"), TEXT("Selection.Distance.CoLocatedB"), FVector(-620,-130,120)},
            {TEXT("Centered far"), TEXT("Selection.Distance.CenteredFar"), FVector(-2400,0,120)} };
        for (const auto& Item : Items)
        {
            const FVector P = Origin + Item.Offset;
            const auto H = Candidate(C->BareRoots[0], Item.Name, P);
            if (!Allocated(H) || !Visual(Item.Label, P, H))
            { return Abort(TEXT("Distance/overlap candidate failed")); }
        }
    }
    else if (Scenario == 2)
    {
        const FVector P = Origin + FVector(-600, 0, 120);
        auto Composite = Candidate(C->BareRoots[0],
            TEXT("Selection.Composite.Root"), P);
        if (!Allocated(Composite))
        { return Abort(TEXT("Composite candidate failed")); }
        auto CompositeTransform = ck::StaticCast<FCk_Handle_Transform>(Composite);
        auto RawScene = UCk_Utils_EntityLifetime_UE::
            Request_CreateEntity(Composite);
        C->AuthoredNodes.Add(RawScene); // retain before synchronous observers
        C->AuthoredParents.Add(Composite);
        FCk_Handle_SceneNode Scene;
        if (!Allocated(RawScene) ||
            !UCk_Utils_SceneNode_UE::TryAdd_Anchor(
                RawScene, CompositeTransform,
                FTransform(FRotator::ZeroRotator, FVector(0,150,100)),
                Scene) || !Allocated(Scene) || !ck::IsValid(Scene) ||
            FCk_Handle(Scene) != RawScene || !ProveKnown(C))
        { return Abort(TEXT("Composite SceneNode failed")); }
        auto Ordinary = NewNode(Composite,
            TEXT("Selection.Composite.OrdinaryChild"));
        if (!Allocated(Ordinary) || !AddTransform(
            Ordinary, P + FVector(0,-150,0)))
        { return Abort(TEXT("Composite ordinary child failed")); }
        auto NoTransform = NewNode(Ordinary,
            TEXT("Selection.Composite.NoTransformDescendant"));
        if (!Allocated(NoTransform))
        { return Abort(TEXT("Composite no-transform child failed")); }
        auto TimerSpec = FCk_Timer_Spec(FCk_Time(1.0f));
        TimerSpec.Set_StartingState(ECk_Timer_State::Running)
                 .Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        const auto Timer = UCk_Utils_Timer_UE::Add(Composite, TimerSpec);
        C->AuthoredNodes.Add(Timer);
        C->AuthoredParents.Add(Composite);
        if (!Allocated(Timer) || !ProveKnown(C))
        { return Abort(TEXT("Composite timer failed")); }
        const auto Integer = UCk_Utils_IntegerAttribute_UE::Add(
            Composite, FCk_IntegerAttribute_Spec(CounterLabel, 7),
            ECk_Replication::DoesNotReplicate);
        C->AuthoredNodes.Add(Integer);
        C->AuthoredParents.Add(Composite);
        if (!Allocated(Integer) || !ProveKnown(C))
        { return Abort(TEXT("Composite integer attribute failed")); }
        const auto Sm = UCk_Utils_StateMachine_UE::Add(
            Composite, FCk_StateMachine_Spec(IdleStateClass));
        // StateMachine::Add adds FFragment_Sm to Composite and returns that
        // same handle; only its runtime Idle state is a new descendant.
        if (!Allocated(Sm) || FCk_Handle(Sm) != Composite ||
            !Composite.Has<ck::FFragment_Sm>() || !ProveKnown(C))
        { return Abort(TEXT("Composite state machine failed")); }
        if (!Visual(TEXT("Composite root"), P, Composite) ||
            !Visual(TEXT("SceneNode descendant"), P + FVector(0,150,100), Scene) ||
            !Visual(TEXT("Ordinary child"), P + FVector(0,-150,0), Ordinary) ||
            !Visual(TEXT("No-transform descendant"), P + FVector(0,-150,150), NoTransform))
        { return Abort(TEXT("Composite visual failed")); }
    }
    else if (Scenario == 3)
    {
        const FVector Offscreen = Origin + FVector(900, 0, 120);
        auto Parent = Candidate(C->BareRoots[0],
            TEXT("Selection.Visibility.OffscreenParent"), Offscreen);
        if (!Allocated(Parent) || !Visual(
            TEXT("Offscreen parent"), Offscreen, Parent))
        { return Abort(TEXT("Visibility parent failed")); }
        const FVector ChildP = Origin + FVector(-600,-180,120);
        auto Child = NewNode(Parent,
            TEXT("Selection.Visibility.VisibleChild"));
        if (!Allocated(Child) || !AddTransform(Child, ChildP) ||
            !Visual(TEXT("Visible ordinary child promotes parent"), ChildP, Child))
        { return Abort(TEXT("Visibility ordinary child failed")); }
        const FVector Occluded = Origin + FVector(-900,180,120);
        auto OccludedNode = Candidate(C->BareRoots[0],
            TEXT("Selection.Visibility.Occluded"), Occluded);
        if (!Allocated(OccludedNode) || !Visual(
            TEXT("Occluded root"), Occluded, OccludedNode) ||
            !Visual(TEXT("Occlusion wall"),
                Origin + FVector(-550,180,150), {}, true))
        { return Abort(TEXT("Visibility/occlusion source failed")); }
    }
    else
    {
        const FVector Moving = Origin + FVector(-650,0,120);
        C->MovingNode = Candidate(C->BareRoots[0],
            TEXT("Selection.Lifecycle.Moving"), Moving);
        if (!Allocated(C->MovingNode) || !Visual(
            TEXT("Moving root"), Moving, C->MovingNode))
        { return Abort(TEXT("Moving candidate failed")); }
        C->MovingVisualKey = C->Visuals.Last().VisualKey;
        const FVector Respawn = Origin + FVector(-650,260,120);
        C->RespawnNode = Candidate(C->BareRoots[0],
            TEXT("Selection.Lifecycle.Respawnable"), Respawn);
        if (!Allocated(C->RespawnNode) || !Visual(
            TEXT("Delete / respawn root"), Respawn, C->RespawnNode))
        { return Abort(TEXT("Respawn candidate failed")); }
        C->RespawnVisualKey = C->Visuals.Last().VisualKey;
    }
    return true;
#endif
}

ECkSelectionGym_SourceOutcome UCkSelectionGym_SourceComposer_UE::TryPollStage(
    UCkSelectionGym_SourceComposition* C, FString& OutReason)
{
    OutReason.Reset();
    if (!IsValid(C) || C->Failed || !IsValid(C->Session))
    { OutReason = TEXT("Source composition unavailable"); return ECkSelectionGym_SourceOutcome::Invalid; }
    if (C->Staged)
    { return ECkSelectionGym_SourceOutcome::Complete; }
    const auto Readiness = UCkSelectionGym_SourceSession_UE::
        TryCheckStageReadiness(C->Session, C->AuthoredNodes, OutReason);
    if (Readiness == ECkSelectionGym_SourceOutcome::Pending)
    { return Readiness; }
    if (Readiness != ECkSelectionGym_SourceOutcome::Complete)
    {
        const FString Why = OutReason;
        Fail(C, Why, OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    if (!UCkSelectionGym_SourceSession_UE::TryStage(
        C->Session, C->AuthoredNodes, OutReason))
    {
        const FString Why = OutReason;
        Fail(C, Why, OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    C->Staged = true;
    return ECkSelectionGym_SourceOutcome::Complete;
}

ECkSelectionGym_SourceOutcome
UCkSelectionGym_SourceComposer_UE::TryActivateAndSample(
    UCkSelectionGym_SourceComposition* C,
    FCkSelectionGym_WireSnapshot& OutSnapshot, FString& OutReason)
{
    OutSnapshot = {};
    OutReason.Reset();
    if (!IsValid(C) || C->Failed || !C->Staged || C->Activated ||
        !IsValid(C->Session) ||
        !ExactAuthority(C->World.Get(), C->GameState.Get(), C->WorldRoot))
    {
        Fail(C, TEXT("Exact staged authority source unavailable"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    for (const auto& R : C->Visuals)
    {
        FCkSelectionGym_VisualRow Before;
        if (!ProveKnown(C) ||
            !UCkSelectionGym_SourceSession_UE::TryReadVisual(
            C->Session, R, Before, OutReason) ||
            !ValidOwner(C->World.Get(), C->GameState.Get(), R.Actor.Get()) ||
            R.Actor->GetClass() !=
                (R.Kind == ECkSelectionGym_VisualKind::OcclusionWall ?
                    C->WallClass.Get() : C->MarkerClass.Get()) ||
            !IsValid(R.Mesh.Get()) || !R.Actor->IsHidden() ||
            Before.MeshVisible || Before.ActorCollisionEnabled ||
            Before.MeshCollisionMode != ECollisionEnabled::NoCollision)
        {
            const FString Why = OutReason.IsEmpty() ?
                TEXT("Exact visual changed before activation") : OutReason;
            Fail(C, Why, OutReason);
            return ECkSelectionGym_SourceOutcome::Invalid;
        }
        if (R.Kind == ECkSelectionGym_VisualKind::OcclusionWall)
        {
            R.Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            if (!ProveKnown(C) ||
                !ValidOwner(C->World.Get(), C->GameState.Get(), R.Actor.Get()) ||
                !IsValid(R.Mesh.Get()) || R.Mesh->GetOwner() != R.Actor.Get())
            {
                Fail(C, TEXT("Wall collision callback changed owner"), OutReason);
                return ECkSelectionGym_SourceOutcome::Invalid;
            }
            R.Actor->SetActorEnableCollision(true);
            if (!ProveKnown(C) ||
                !ValidOwner(C->World.Get(), C->GameState.Get(), R.Actor.Get()))
            {
                Fail(C, TEXT("Wall actor collision callback changed source"), OutReason);
                return ECkSelectionGym_SourceOutcome::Invalid;
            }
        }
        R.Actor->SetActorHiddenInGame(false);
        if (!ProveKnown(C) ||
            !ValidOwner(C->World.Get(), C->GameState.Get(), R.Actor.Get()))
        {
            Fail(C, TEXT("Visual visibility callback changed owner"), OutReason);
            return ECkSelectionGym_SourceOutcome::Invalid;
        }
    }
    const auto Outcome = UCkSelectionGym_SourceSession_UE::TrySample(
        C->Session, OutSnapshot, OutReason);
    if (Outcome != ECkSelectionGym_SourceOutcome::Complete)
    {
        OutSnapshot = {};
        const FString Why = OutReason.IsEmpty() ?
            TEXT("Physical Selection source sample unavailable") : OutReason;
        Fail(C, Why, OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    C->Activated = true;
    return Outcome;
}

ECkSelectionGym_SourceOutcome UCkSelectionGym_SourceComposer_UE::TryMoveAndSample(
    UCkSelectionGym_SourceComposition* C, double DeltaSeconds,
    FCkSelectionGym_WireSnapshot& OutSnapshot, FString& OutReason)
{
    OutSnapshot = {};
    OutReason.Reset();
    if (!IsValid(C) || C->Failed || !C->Staged || !C->Activated ||
        C->Scenario != 4 ||
        C->RespawnPending || C->RespawnReady ||
        !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0 ||
        !ProveKnown(C) ||
        !Allocated(C->MovingNode) || !ck::IsValid(C->MovingNode) ||
        !C->MovingNode.Has<ck::FFragment_Transform>())
    {
        Fail(C, TEXT("Moving source request or owner invalid"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    FCkSelectionGym_SourceVisualReceipt* Moving = nullptr;
    for (auto& R : C->Visuals)
    { if (R.VisualKey == C->MovingVisualKey) { Moving = &R; break; } }
    if (Moving == nullptr || Moving->Node != C->MovingNode ||
        !ValidOwner(C->World.Get(), C->GameState.Get(), Moving->Actor.Get()))
    {
        Fail(C, TEXT("Moving visual identity lost"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    FCkSelectionGym_VisualRow CurrentVisual;
    if (!UCkSelectionGym_SourceSession_UE::TryReadVisual(
        C->Session, *Moving, CurrentVisual, OutReason))
    {
        const FString Why = OutReason;
        Fail(C, Why, OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    // A deferred Transform request would expose a new ECS position to the
    // overlay before the retained actor had moved. Update both in this one
    // authority action, then sample actual current values before returning.
    if (!C->MovingNode.Has<ck::FFragment_Transform_Previous>())
    {
        Fail(C, TEXT("Moving transform previous fragment lost"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    const double NextElapsed = C->MovingElapsed + DeltaSeconds;
    if (!FMath::IsFinite(NextElapsed))
    {
        Fail(C, TEXT("Moving source elapsed overflow"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    const FVector Target = C->Origin + FVector(-650.0,
        FMath::Sin(NextElapsed) * 280.0, 120.0);
    auto T = ck::StaticCast<FCk_Handle_Transform>(C->MovingNode);
    FTransform Current = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(T);
    if (Current.ContainsNaN() || !Current.IsValid() ||
        !FMath::IsFinite(Target.X) || !FMath::IsFinite(Target.Y) ||
        !FMath::IsFinite(Target.Z))
    {
        Fail(C, TEXT("Moving source transform invalid"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    Current.SetLocation(Target);
    auto& Transform = C->MovingNode.Get<ck::FFragment_Transform>();
    auto& Previous = C->MovingNode.Get<ck::FFragment_Transform_Previous>();
    UCk_Utils_Transform_UE::Apply_SetTransform_DirectWrite(
        Transform, Previous, Current);
    C->MovingNode.AddOrGet<ck::FTag_Transform_Updated>();
    if (!ProveKnown(C) ||
        !ValidOwner(C->World.Get(), C->GameState.Get(), Moving->Actor.Get()))
    {
        Fail(C, TEXT("Moving source changed after direct Transform write"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    Moving->Actor->SetActorLocation(Target);
    if (!ProveKnown(C) ||
        !ValidOwner(C->World.Get(), C->GameState.Get(), Moving->Actor.Get()) ||
        !Moving->Actor->GetActorLocation().Equals(Target, 0.01))
    {
        Fail(C, TEXT("Moving visual did not reach physical transform"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    C->MovingElapsed = NextElapsed;
    const auto Outcome = UCkSelectionGym_SourceSession_UE::TrySample(
        C->Session, OutSnapshot, OutReason);
    if (Outcome != ECkSelectionGym_SourceOutcome::Complete)
    {
        OutSnapshot = {};
        const FString Why = OutReason;
        Fail(C, Why, OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    return Outcome;
}

bool UCkSelectionGym_SourceComposer_UE::TryStartRespawn(
    UCkSelectionGym_SourceComposition* C, FString& OutReason)
{
    OutReason.Reset();
    if (!IsValid(C) || C->Failed || !C->Staged || !C->Activated ||
        C->Scenario != 4 ||
        C->RespawnPending || C->RespawnReady ||
        !IsValid(C->Session) || !Allocated(C->RespawnNode))
    { return Fail(C, TEXT("Exact respawn source unavailable"), OutReason); }
    if (!UCkSelectionGym_SourceSession_UE::TryStartRespawnRetirement(
        C->Session, C->RespawnNode, C->RespawnVisualKey, OutReason))
    {
        const FString Why = OutReason;
        return Fail(C, Why, OutReason);
    }
    C->RespawnPending = true;
    C->RespawnReady = false;
    C->RespawnAttempted = false;
    return true;
}

ECkSelectionGym_SourceOutcome UCkSelectionGym_SourceComposer_UE::TryAdvanceRespawn(
    UCkSelectionGym_SourceComposition* C, FString& OutReason)
{
    OutReason.Reset();
    if (!IsValid(C) || C->Failed || !C->RespawnPending ||
        !IsValid(C->Session))
    {
        Fail(C, TEXT("Respawn retirement was not started"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    const auto Outcome = UCkSelectionGym_SourceSession_UE::
        TryAdvanceRespawnRetirement(C->Session, OutReason);
    if (Outcome == ECkSelectionGym_SourceOutcome::Complete)
    {
        C->RespawnPending = false;
        C->RespawnReady = true;
        const int32 OldIndex = C->AuthoredNodes.Find(C->RespawnNode);
        if (OldIndex == INDEX_NONE || C->AuthoredParents.Num() !=
            C->AuthoredNodes.Num())
        {
            Fail(C, TEXT("Retired respawn receipt missing from composer"), OutReason);
            return ECkSelectionGym_SourceOutcome::Invalid;
        }
        C->AuthoredNodes.RemoveAt(OldIndex);
        C->AuthoredParents.RemoveAt(OldIndex);
    }
    else if (Outcome == ECkSelectionGym_SourceOutcome::Invalid)
    {
        const FString Why = OutReason;
        Fail(C, Why, OutReason);
    }
    return Outcome;
}

ECkSelectionGym_SourceOutcome UCkSelectionGym_SourceComposer_UE::TryFinishRespawn(
    UCkSelectionGym_SourceComposition* C,
    FCkSelectionGym_WireSnapshot& OutSnapshot, FString& OutReason)
{
    OutSnapshot = {};
    OutReason.Reset();
    if (!IsValid(C) || C->Failed || C->Scenario != 4 ||
        !C->RespawnReady || C->RespawnAttempted ||
        !IsValid(C->Session) ||
        !ExactAuthority(C->World.Get(), C->GameState.Get(), C->WorldRoot) ||
        C->BareRoots.Num() != 1 || Allocated(C->RespawnNode))
    {
        Fail(C, TEXT("Physically retired respawn identity unavailable"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    C->RespawnAttempted = true;
    auto* World = C->World.Get();
    auto* GS = C->GameState.Get();
    const FVector P = C->Origin + FVector(-650,260,120);
    auto New = UCk_Utils_EntityLifetime_UE::
        Request_CreateEntity(C->BareRoots[0]);
    C->AuthoredNodes.Add(New); // first-return receipt before any feature
    C->AuthoredParents.Add(C->BareRoots[0]);
    if (!Allocated(New) || !ck::IsValid(New))
    {
        Fail(C, TEXT("Respawn replacement entity creation failed"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    UCk_Utils_Handle_UE::Set_DebugName(New,
        FName(TEXT("Selection.Lifecycle.Respawned")));
    if (!ProveKnown(C))
    {
        Fail(C, TEXT("Respawn first-return owner changed"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    UCk_Utils_ContextOwner_UE::Request_OverrideToSelf(New, {});
    if (!ProveKnown(C) || !UCk_Utils_ContextOwner_UE::Has(New) ||
        New.Get<ck::FFragment_ContextOwner>().Get_Entity() != New ||
        !Allocated(UCk_Utils_Transform_UE::Add(New,
        FTransform(FRotator::ZeroRotator, P),
        ECk_Replication::DoesNotReplicate)) || !ProveKnown(C))
    {
        Fail(C, TEXT("Respawn replacement transform failed"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    const auto RootLabel = FGameplayTag::RequestGameplayTag(
        FName(TEXT("Ck.Debug.Selection.Root")), false);
    auto* Cube = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!RootLabel.IsValid() || !IsValid(Cube))
    {
        Fail(C, TEXT("Respawn authored label or mesh unavailable"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    UCk_Utils_GameplayLabel_UE::Add(New, RootLabel);
    if (!ProveKnown(C) || !UCk_Utils_GameplayLabel_UE::Has(New) ||
        !UCk_Utils_GameplayLabel_UE::MatchesExact(New, RootLabel))
    {
        Fail(C, TEXT("Respawn replacement label failed"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }

    const FTransform Pose(FRotator::ZeroRotator, P);
    AActor* Actor = World->SpawnActorDeferred<AActor>(C->MarkerClass.Get(),
        Pose, GS, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    FCkSelectionGym_SourceVisualReceipt R;
    R.VisualKey = C->NextVisualKey++;
    R.Kind = ECkSelectionGym_VisualKind::LabelMarker;
    R.Node = New;
    R.Actor = Actor;
    C->Visuals.Add(R); // retain first return before finishing construction
    auto ExactActor = [&]() -> bool
    {
        return ProveKnown(C) &&
            ValidOwner(World, GS, Actor) &&
            Actor->GetClass() == C->MarkerClass.Get();
    };
    if (!ExactActor())
    {
        Fail(C, TEXT("Respawn replacement actor failed"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    Actor->SetActorHiddenInGame(true);
    if (!ExactActor())
    {
        Fail(C, TEXT("Respawn actor owner changed during staging"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    Actor->SetActorEnableCollision(false);
    if (!ExactActor() ||
        UGameplayStatics::FinishSpawningActor(Actor, Pose) != Actor ||
        !ExactActor())
    {
        Fail(C, TEXT("Respawn replacement actor construction failed"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    TArray<UStaticMeshComponent*> Meshes;
    TArray<UTextRenderComponent*> Texts;
    Actor->GetComponents<UStaticMeshComponent>(Meshes);
    Actor->GetComponents<UTextRenderComponent>(Texts);
    if (Meshes.Num() != 1 || Texts.Num() != 1 ||
        !IsValid(Meshes[0]) || !IsValid(Texts[0]) ||
        !Meshes[0]->IsRegistered() || !Texts[0]->IsRegistered())
    {
        Fail(C, TEXT("Respawn physical components unavailable"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    R.Mesh = Meshes[0];
    R.Text = Texts[0];
    C->Visuals.Last().Mesh = Meshes[0];
    C->Visuals.Last().Text = Texts[0];
    Actor->SetActorHiddenInGame(true);
    if (!ExactActor())
    {
        Fail(C, TEXT("Respawn actor owner changed after construction"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    Actor->SetActorEnableCollision(false);
    if (!ExactActor())
    {
        Fail(C, TEXT("Respawn actor collision callback changed owner"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    Meshes[0]->SetMobility(EComponentMobility::Movable);
    if (!ExactActor() || !IsValid(Meshes[0]) ||
        Meshes[0]->GetOwner() != Actor)
    {
        Fail(C, TEXT("Respawn mesh mobility callback changed source"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    Meshes[0]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (!ExactActor() || !IsValid(Meshes[0]) ||
        Meshes[0]->GetOwner() != Actor)
    {
        Fail(C, TEXT("Respawn mesh collision callback changed source"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    Meshes[0]->SetStaticMesh(Cube);
    if (!ExactActor() || !IsValid(Meshes[0]) ||
        Meshes[0]->GetOwner() != Actor)
    {
        Fail(C, TEXT("Respawn mesh assignment changed source"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    Meshes[0]->SetRelativeScale3D(FVector(0.35f, 0.35f, 0.35f));
    if (!ExactActor() || !IsValid(Texts[0]) ||
        Texts[0]->GetOwner() != Actor)
    {
        Fail(C, TEXT("Respawn component scale changed source"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    Texts[0]->SetText(FText::FromString(TEXT("Respawned root")));
    if (!ExactActor() || !Actor->IsHidden() ||
        Actor->GetActorEnableCollision() ||
        !UCkSelectionGym_SourceSession_UE::TryInstallRespawnReplacement(
            C->Session, New, R, OutReason))
    {
        const FString Why = OutReason.IsEmpty() ?
            TEXT("Respawn replacement receipt invalid") : OutReason;
        Fail(C, Why, OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    C->RespawnNode = New;
    C->RespawnVisualKey = R.VisualKey;
    C->Visuals.RemoveAll([&](const auto& V) {
        return V.VisualKey != R.VisualKey &&
            V.Kind == ECkSelectionGym_VisualKind::LabelMarker &&
            V.Node != C->MovingNode;
    });
    C->RespawnReady = false;
    Actor->SetActorHiddenInGame(false);
    if (!ExactActor())
    {
        Fail(C, TEXT("Respawn visibility callback changed owner"), OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    const auto Outcome = UCkSelectionGym_SourceSession_UE::TrySample(
        C->Session, OutSnapshot, OutReason);
    if (Outcome != ECkSelectionGym_SourceOutcome::Complete)
    {
        OutSnapshot = {};
        const FString Why = OutReason;
        Fail(C, Why, OutReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    return Outcome;
}

UCkSelectionGym_SourceSession* UCkSelectionGym_SourceComposer_UE::Get_Session(
    const UCkSelectionGym_SourceComposition* C)
{ return IsValid(C) ? C->Session : nullptr; }

#if WITH_DEV_AUTOMATION_TESTS
bool UCkSelectionGym_SourceComposer_UE::TryGet_PrimaryBareRootForTest(
    const UCkSelectionGym_SourceComposition* C, FCk_Handle& OutRoot)
{
    OutRoot = {};
    if (!IsValid(C) || C->Failed || C->BareRoots.IsEmpty() ||
        C->AuthoredNodes.IsEmpty() || !ProveKnown(C) ||
        C->BareRoots[0] != C->AuthoredNodes[0] ||
        !C->BareRoots[0].Has<ck::FFragment_LifetimeOwner>() ||
        C->BareRoots[0].Get<ck::FFragment_LifetimeOwner>().Get_Entity() !=
            C->WorldRoot)
    { return false; }
    OutRoot = C->BareRoots[0];
    return true;
}
#endif
