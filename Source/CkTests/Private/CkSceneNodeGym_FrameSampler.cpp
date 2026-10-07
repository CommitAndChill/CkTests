#include "CkSceneNodeGym_FrameSampler.h"

#include "CkSceneNodeGym_ActorLedger.h"
#include "CkSceneNodeGym_Motion.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"

#include "Components/StaticMeshComponent.h"
#include "CoreGlobals.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"

namespace CkSceneNodeFrameSamplerPrivate
{
    constexpr int32 RootCount = 6;
    constexpr int32 NodeCount = 11;
    constexpr int32 VisualCount = 17;
    constexpr int32 RootRole[RootCount] = {1, 3, 5, 10, 13, 16};
    constexpr int32 NodeRole[NodeCount] = {2, 4, 6, 7, 8, 9, 11, 12, 14, 15, 17};
    constexpr double AppliedPoseTolerance = 0.01;

    bool Live(const UObject* Object)
    {
        return ck::IsValid(Object) &&
            !Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
    }

    bool HasPassiveRoot(const AActor* Actor, const UStaticMeshComponent* Root,
        const UWorld* World)
    {
        return Live(Actor) && !Actor->IsActorBeingDestroyed() &&
            Actor->GetWorld() == World && !Actor->GetIsReplicated() &&
            !Actor->IsReplicatingMovement() && Actor->GetRemoteRole() == ROLE_None &&
            Live(Root) && Actor->GetRootComponent() == Root &&
            Root->GetOwner() == Actor && Root->GetWorld() == World &&
            Root->IsRegistered() &&
            Root->GetMobility() == EComponentMobility::Movable &&
            Root->GetCollisionEnabled() == ECollisionEnabled::NoCollision &&
            !Root->IsSimulatingPhysics();
    }
}

void UCk_SceneNodeGym_FrameSampler_UE::CloseAndUnbind()
{
    bClosed = true;
    bHasFrame = false;
    Latest = {};
    PublishedEngineFrame = 0;
    if (PostTickHandle.IsValid())
    {
        FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle);
        PostTickHandle.Reset();
    }
    if (WorldCleanupHandle.IsValid())
    {
        FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
        WorldCleanupHandle.Reset();
    }
    auto* const CurrentSource = Source.Get();
    if (CkSceneNodeFrameSamplerPrivate::Live(CurrentSource))
    {
        CurrentSource->OnEndPlay.RemoveDynamic(this,
            &UCk_SceneNodeGym_FrameSampler_UE::OnSourceEndPlay);
    }
}

void UCk_SceneNodeGym_FrameSampler_UE::BeginDestroy()
{
    CloseAndUnbind();
    Super::BeginDestroy();
}

void UCk_SceneNodeGym_FrameSampler_UE::OnSourceEndPlay(
    AActor* Actor, EEndPlayReason::Type)
{
    if (!IsInGameThread() || Actor != Source.GetEvenIfUnreachable())
    { return; }
    UCk_SceneNodeGym_FrameSamplerRuntime_UE::Fail(this);
}

void UCk_SceneNodeGym_FrameSampler_UE::OnWorldCleanup(
    UWorld* InWorld, bool, bool)
{
    if (InWorld == World.GetEvenIfUnreachable()) { CloseAndUnbind(); }
}

void UCk_SceneNodeGym_FrameSampler_UE::OnWorldPostActorTick(
    UWorld* InWorld, ELevelTick TickType, float)
{
    if (bClosed || InWorld != World.Get() || TickType != LEVELTICK_All)
    { return; }
    if (bInSample)
    { UCk_SceneNodeGym_FrameSamplerRuntime_UE::Fail(this); return; }
    TGuardValue<bool> SamplingGuard(bInSample, true);
    UCk_SceneNodeGym_FrameSamplerRuntime_UE::Sample(this, InWorld);
}

bool UCk_SceneNodeGym_FrameSamplerRuntime_UE::HasCurrentContext(
    const UCk_SceneNodeGym_FrameSampler_UE* Sampler)
{
    if (!IsInGameThread() || !CkSceneNodeFrameSamplerPrivate::Live(Sampler) || Sampler->bClosed ||
        Sampler->Generation <= 0 || Sampler->Motion.IsStale() ||
        Sampler->Ledger.IsStale() || Sampler->Source.IsStale() ||
        Sampler->World.IsStale())
    { return false; }
    auto* const Motion = Sampler->Motion.Get();
    auto* const Ledger = Sampler->Ledger.Get();
    auto* const Source = Sampler->Source.Get();
    auto* const World = Sampler->World.Get();
    if (!CkSceneNodeFrameSamplerPrivate::Live(Motion) || !CkSceneNodeFrameSamplerPrivate::Live(Ledger) || !CkSceneNodeFrameSamplerPrivate::Live(Source) || !CkSceneNodeFrameSamplerPrivate::Live(World) ||
        World->bIsTearingDown || Source->IsActorBeingDestroyed() ||
        Source->GetWorld() != World || World->GetGameState() != Source ||
        !Source->HasAuthority() || World->GetNetMode() == NM_Client ||
        Motion->FrameSampler != Sampler || !Motion->bFrameSamplerAttempted ||
        Motion->Ledger.Get() != Ledger || Motion->Generation != Sampler->Generation ||
        UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_Source(Ledger) != Source ||
        UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_Generation(Ledger) !=
            Sampler->Generation ||
        !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(Motion) ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
            Ledger, true))
    { return false; }
    return true;
}

void UCk_SceneNodeGym_FrameSamplerRuntime_UE::Fail(
    UCk_SceneNodeGym_FrameSampler_UE* Sampler)
{
    if (!IsInGameThread() || !CkSceneNodeFrameSamplerPrivate::Live(Sampler)) { return; }
    auto* const Motion = Sampler->Motion.Get();
    Sampler->CloseAndUnbind(); // invalidate publication before stopping producers
    if (CkSceneNodeFrameSamplerPrivate::Live(Motion))
    { UCk_SceneNodeGym_MotionRuntime_UE::TryClose(Motion); }
}

UCk_SceneNodeGym_FrameSampler_UE*
UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryBegin(
    UCk_SceneNodeGym_Motion_UE* Motion)
{
    if (!IsInGameThread() || !CkSceneNodeFrameSamplerPrivate::Live(Motion) ||
        Motion->bFrameSamplerAttempted || Motion->FrameSampler != nullptr ||
        !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(Motion))
    { return nullptr; }
    auto* const Ledger = Motion->Ledger.Get();
    auto* const Source = CkSceneNodeFrameSamplerPrivate::Live(Ledger)
        ? UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_Source(Ledger) : nullptr;
    auto* const World = CkSceneNodeFrameSamplerPrivate::Live(Source) ? Source->GetWorld() : nullptr;
    if (!CkSceneNodeFrameSamplerPrivate::Live(Source) || !CkSceneNodeFrameSamplerPrivate::Live(World) || World->bIsTearingDown ||
        Source->IsActorBeingDestroyed() || World->GetGameState() != Source ||
        !Source->HasAuthority() || World->GetNetMode() == NM_Client ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
            Ledger, true))
    { return nullptr; }

    Motion->bFrameSamplerAttempted = true; // no second receipt after allocation
    auto* const Sampler = NewObject<UCk_SceneNodeGym_FrameSampler_UE>(Motion);
    if (!CkSceneNodeFrameSamplerPrivate::Live(Sampler))
    { UCk_SceneNodeGym_MotionRuntime_UE::TryClose(Motion); return nullptr; }
    Motion->FrameSampler = Sampler;
    Sampler->Motion = Motion;
    Sampler->Ledger = Ledger;
    Sampler->Source = Source;
    Sampler->World = World;
    Sampler->Generation = Motion->Generation;
    Sampler->bClosed = false;

    Source->OnEndPlay.AddUniqueDynamic(Sampler,
        &UCk_SceneNodeGym_FrameSampler_UE::OnSourceEndPlay);
    Sampler->WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
        Sampler, &UCk_SceneNodeGym_FrameSampler_UE::OnWorldCleanup);
    if (!Sampler->WorldCleanupHandle.IsValid() || !HasCurrentContext(Sampler))
    { Fail(Sampler); return Sampler; }
    Sampler->PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(
        Sampler, &UCk_SceneNodeGym_FrameSampler_UE::OnWorldPostActorTick);
    if (!Sampler->PostTickHandle.IsValid() || !HasCurrentContext(Sampler))
    { Fail(Sampler); }
    return Sampler;
}

void UCk_SceneNodeGym_FrameSamplerRuntime_UE::Sample(
    UCk_SceneNodeGym_FrameSampler_UE* Sampler, UWorld* InWorld)
{
    if (!HasCurrentContext(Sampler) || InWorld != Sampler->World.Get() ||
        Sampler->Latest.FrameSerial < 0 ||
        Sampler->Latest.FrameSerial == TNumericLimits<int64>::Max() ||
        (Sampler->bHasFrame &&
            (Sampler->Latest.Generation != Sampler->Generation ||
             Sampler->Latest.FrameSerial == 0 ||
             Sampler->Latest.WorldPoses.Num() != CkSceneNodeFrameSamplerPrivate::VisualCount)) ||
        (!Sampler->bHasFrame &&
            (Sampler->Latest.FrameSerial != 0 ||
             !Sampler->Latest.WorldPoses.IsEmpty())))
    { Fail(Sampler); return; }
    auto* const Motion = Sampler->Motion.Get();
    auto* const Ledger = Sampler->Ledger.Get();
    UStaticMeshComponent* Components[CkSceneNodeFrameSamplerPrivate::VisualCount] = {};
    AActor* Actors[CkSceneNodeFrameSamplerPrivate::VisualCount] = {};
    FTransform InitialComponentPoses[CkSceneNodeFrameSamplerPrivate::VisualCount];
    for (int32 Role = 1; Role <= CkSceneNodeFrameSamplerPrivate::VisualCount; ++Role)
    {
        AActor* Actor = nullptr;
        if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                Ledger, Role, Actor) || !CkSceneNodeFrameSamplerPrivate::Live(Actor))
        { Fail(Sampler); return; }
        auto* const Root = Cast<UStaticMeshComponent>(Actor->GetRootComponent());
        if (!CkSceneNodeFrameSamplerPrivate::HasPassiveRoot(Actor, Root, InWorld))
        { Fail(Sampler); return; }
        const FTransform Pose = Root->GetComponentTransform();
        if (!Pose.IsValid()) { Fail(Sampler); return; }
        Actors[Role - 1] = Actor;
        Components[Role - 1] = Root;
        InitialComponentPoses[Role - 1] = Pose;
    }

    FTransform RootPoses[CkSceneNodeFrameSamplerPrivate::RootCount];
    FTransform NodePoses[CkSceneNodeFrameSamplerPrivate::NodeCount];
    for (int32 I = 0; I < CkSceneNodeFrameSamplerPrivate::RootCount; ++I)
    {
        RootPoses[I] = InitialComponentPoses[CkSceneNodeFrameSamplerPrivate::RootRole[I] - 1];
    }
    for (int32 I = 0; I < CkSceneNodeFrameSamplerPrivate::NodeCount; ++I)
    {
        const auto& Node = Motion->Nodes[I];
        if (!ck::IsValid(Node) || !Node.Has<ck::FFragment_Transform>())
        { Fail(Sampler); return; }
        NodePoses[I] = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::Cast(Node));
        if (!NodePoses[I].IsValid()) { Fail(Sampler); return; }
    }

    // Actor writes can invoke external callbacks. The only atomic operation
    // here is the eventual value-frame commit, never the sequence of writes.
    for (int32 I = 0; I < CkSceneNodeFrameSamplerPrivate::NodeCount; ++I)
    {
        if (!HasCurrentContext(Sampler) ||
            !CkSceneNodeFrameSamplerPrivate::HasPassiveRoot(Actors[CkSceneNodeFrameSamplerPrivate::NodeRole[I] - 1],
                Components[CkSceneNodeFrameSamplerPrivate::NodeRole[I] - 1], InWorld) ||
            !Actors[CkSceneNodeFrameSamplerPrivate::NodeRole[I] - 1]->SetActorTransform(NodePoses[I], false,
                nullptr, ETeleportType::TeleportPhysics) ||
            !HasCurrentContext(Sampler) ||
            !CkSceneNodeFrameSamplerPrivate::HasPassiveRoot(Actors[CkSceneNodeFrameSamplerPrivate::NodeRole[I] - 1],
                Components[CkSceneNodeFrameSamplerPrivate::NodeRole[I] - 1], InWorld) ||
            !Components[CkSceneNodeFrameSamplerPrivate::NodeRole[I] - 1]->GetComponentTransform().IsValid() ||
            !Components[CkSceneNodeFrameSamplerPrivate::NodeRole[I] - 1]->GetComponentTransform().Equals(
                NodePoses[I], CkSceneNodeFrameSamplerPrivate::AppliedPoseTolerance))
        { Fail(Sampler); return; }
    }

    FCkSceneNodeGym_Frame Candidate;
    Candidate.Generation = Sampler->Generation;
    Candidate.FrameSerial = Sampler->Latest.FrameSerial + 1;
    Candidate.WorldPoses.Reserve(CkSceneNodeFrameSamplerPrivate::VisualCount);
    if (!HasCurrentContext(Sampler)) { Fail(Sampler); return; }
    for (int32 I = 0; I < CkSceneNodeFrameSamplerPrivate::RootCount; ++I)
    {
        if (!CkSceneNodeFrameSamplerPrivate::HasPassiveRoot(Actors[CkSceneNodeFrameSamplerPrivate::RootRole[I] - 1],
                Components[CkSceneNodeFrameSamplerPrivate::RootRole[I] - 1], InWorld) ||
            !RootPoses[I].Equals(
                Components[CkSceneNodeFrameSamplerPrivate::RootRole[I] - 1]->GetComponentTransform(),
                CkSceneNodeFrameSamplerPrivate::AppliedPoseTolerance))
        { Fail(Sampler); return; }
    }
    for (int32 I = 0; I < CkSceneNodeFrameSamplerPrivate::NodeCount; ++I)
    {
        const auto& Node = Motion->Nodes[I];
        if (!ck::IsValid(Node) || !Node.Has<ck::FFragment_Transform>())
        { Fail(Sampler); return; }
        const FTransform Current = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::Cast(Node));
        if (!Current.IsValid() ||
            !Current.Equals(NodePoses[I], CkSceneNodeFrameSamplerPrivate::AppliedPoseTolerance))
        { Fail(Sampler); return; }
    }
    for (int32 Role = 1; Role <= CkSceneNodeFrameSamplerPrivate::VisualCount; ++Role)
    {
        if (!CkSceneNodeFrameSamplerPrivate::HasPassiveRoot(Actors[Role - 1], Components[Role - 1], InWorld))
        { Fail(Sampler); return; }
        const FTransform Pose = Components[Role - 1]->GetComponentTransform();
        if (!Pose.IsValid()) { Fail(Sampler); return; }
        Candidate.WorldPoses.Add(Pose);
    }
    for (int32 I = 0; I < CkSceneNodeFrameSamplerPrivate::NodeCount; ++I)
    {
        if (!Candidate.WorldPoses[CkSceneNodeFrameSamplerPrivate::NodeRole[I] - 1].Equals(
                NodePoses[I], CkSceneNodeFrameSamplerPrivate::AppliedPoseTolerance))
        { Fail(Sampler); return; }
    }
    if (!HasCurrentContext(Sampler) || Candidate.WorldPoses.Num() != CkSceneNodeFrameSamplerPrivate::VisualCount)
    { Fail(Sampler); return; }
    Sampler->Latest = MoveTemp(Candidate);
    Sampler->PublishedEngineFrame = GFrameCounter;
    Sampler->bHasFrame = true;
}

bool UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
    const UCk_SceneNodeGym_FrameSampler_UE* Sampler,
    FCkSceneNodeGym_Frame& OutFrame)
{
    OutFrame = {};
    if (!HasCurrentContext(Sampler) || Sampler->bInSample ||
        !Sampler->bHasFrame ||
        Sampler->Latest.Generation != Sampler->Generation ||
        Sampler->Latest.FrameSerial <= 0 ||
        Sampler->Latest.WorldPoses.Num() != CkSceneNodeFrameSamplerPrivate::VisualCount ||
        GFrameCounter < Sampler->PublishedEngineFrame ||
        GFrameCounter - Sampler->PublishedEngineFrame > 1)
    { return false; }
    auto* const Ledger = Sampler->Ledger.Get();
    auto* const World = Sampler->World.Get();
    for (int32 Role = 1; Role <= CkSceneNodeFrameSamplerPrivate::VisualCount; ++Role)
    {
        AActor* Actor = nullptr;
        if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                Ledger, Role, Actor) || !CkSceneNodeFrameSamplerPrivate::Live(Actor))
        { return false; }
        auto* const Root = Cast<UStaticMeshComponent>(Actor->GetRootComponent());
        if (!CkSceneNodeFrameSamplerPrivate::HasPassiveRoot(Actor, Root, World) ||
            !Root->GetComponentTransform().IsValid())
        { return false; }
    }
    for (const auto& Pose : Sampler->Latest.WorldPoses)
    { if (!Pose.IsValid()) { return false; } }
    OutFrame = Sampler->Latest;
    return true;
}

bool UCk_SceneNodeGym_FrameSamplerRuntime_UE::Request_Close(
    UCk_SceneNodeGym_FrameSampler_UE* Sampler)
{
    if (!IsInGameThread() || !CkSceneNodeFrameSamplerPrivate::Live(Sampler)) { return false; }
    Fail(Sampler); // callback gate and handles close before Motion/Stop admission
    return true;
}
