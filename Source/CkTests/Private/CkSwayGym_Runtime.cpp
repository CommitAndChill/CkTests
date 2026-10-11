#include "CkSwayGym_Runtime.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkSway/CkSway_Fragment.h"
#include "CkSway/CkSway_Utils.h"
#include "CkTween/CkTween_Fragment.h"
#include "CkTween/CkTween_Utils.h"
#include "CkUnrealComponent/CkUnrealComponent_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Utils.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/UObjectGlobals.h"

namespace CkSwayGymRuntimePrivate
{
    constexpr int32 KnownCount = 29;
    constexpr double MaxRetireSeconds = 20.0;
    TArray<TWeakObjectPtr<AGameStateBase>> ClaimedSourceStates;

    template <typename T> bool Live(const T* Object)
    {
        return IsInGameThread() && ck::IsValid(Object) &&
            !Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
    }

    bool SafePose(const FTransform& Pose)
    {
        if (Pose.ContainsNaN() || !Pose.IsValid()) { return false; }
        const auto& P = Pose.GetTranslation();
        const auto& S = Pose.GetScale3D();
        // Authoring envelope for this fixed gym's visual poses, not a global
        // Transform policy. It leaves ample room around the 4,000 cm layout.
        constexpr double MaxPositionCm = 10000000.0;
        constexpr double MaxScale = 10000.0;
        return FMath::Abs(P.X) <= MaxPositionCm &&
            FMath::Abs(P.Y) <= MaxPositionCm &&
            FMath::Abs(P.Z) <= MaxPositionCm &&
            FMath::Abs(S.X) <= MaxScale &&
            FMath::Abs(S.Y) <= MaxScale &&
            FMath::Abs(S.Z) <= MaxScale;
    }

    bool SafeWorldRoot(AGameStateBase* Source, const FCk_Handle& Root)
    {
        if (!Live(Source) || Source->IsActorBeingDestroyed() ||
            !ck::IsValid(Root)) { return false; }
        UWorld* const World = Source->GetWorld();
        if (!Live(World) || World->bIsTearingDown ||
            !World->IsGameWorld() || World->GetNetMode() == NM_Client ||
            World->GetGameState() != Source || !Source->HasAuthority())
        { return false; }
        const auto Registry = Root.Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        if (ck::registry_table::Get_IsInParallelRegion(RegistryHandle) ||
            Root != FCk_Handle{Registry.Get_TransientEntity(), RegistryHandle} ||
            !Root.Has<TWeakObjectPtr<UWorld>>()) { return false; }
        if (Root.Get<TWeakObjectPtr<UWorld>>().Get() != World) { return false; }
        auto* const Subsystem = World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
        return Live(Subsystem) &&
            Subsystem->Get_LoadHold() != ECk_EcsWorld_LoadHold::Teardown &&
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) == Root;
    }

    bool SafeOwnerRoot(AGameStateBase* Source, const FCk_Handle& WorldRoot,
        const FCk_Handle& OwnerRoot)
    {
        if (!SafeWorldRoot(Source, WorldRoot) || !ck::IsValid(OwnerRoot) ||
            OwnerRoot.Get_RegistryView().Get_RegistryHandle() !=
                WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(OwnerRoot,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !OwnerRoot.Has<ck::FFragment_LifetimeOwner>() ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(OwnerRoot) != WorldRoot ||
            UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Source) != OwnerRoot ||
            UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(OwnerRoot) != Source ||
            !Cast<UCk_EntityScript_WithActor_UE>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(OwnerRoot)) ||
            UCk_Utils_Net_UE::Get_EntityReplication(OwnerRoot) != ECk_Replication::Replicates ||
            !OwnerRoot.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>())
        { return false; }
        auto* const Driver = OwnerRoot.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
        return Live(Driver) && Driver->Get_AssociatedEntity() == OwnerRoot &&
            Driver->GetOwningActor() == Source;
    }

    void AppendRoles(const FCkSwayGym_SourceRoles& Roles,
        TArray<FCk_Handle>& Known, TArray<FCk_Handle>& Parents,
        const FCk_Handle& OwnerRoot)
    {
        Known.Reset(); Parents.Reset();
        for (const auto& H : Roles.Roots) { Known.Add(H); Parents.Add(OwnerRoot); }
        for (int32 I = 0; I < Roles.Heads.Num(); ++I)
        { Known.Add(Roles.Heads[I]); Parents.Add(Roles.Roots[I]); }
        constexpr int32 TweenHead[4] = {0, 1, 2, 2};
        for (int32 I = 0; I < Roles.Tweens.Num(); ++I)
        { Known.Add(Roles.Tweens[I]); Parents.Add(Roles.Heads[TweenHead[I]]); }
        constexpr int32 SwayHead[5] = {0, 1, 2, 2, 2};
        for (int32 I = 0; I < Roles.Sways.Num(); ++I)
        { Known.Add(Roles.Sways[I]); Parents.Add(Roles.Heads[SwayHead[I]]); }
        // Rigid twins are direct head children. Other cubes follow their sway.
        constexpr int32 CubeSway[7] = {0, -1, 1, -1, 2, 3, 4};
        constexpr int32 CubeHead[7] = {0, 0, 1, 1, 2, 2, 2};
        for (int32 I = 0; I < Roles.CubeNodes.Num(); ++I)
        {
            Known.Add(Roles.CubeNodes[I]);
            Parents.Add(CubeSway[I] >= 0 ? Roles.Sways[CubeSway[I]] :
                Roles.Heads[CubeHead[I]]);
        }
        for (int32 I = 0; I < Roles.Components.Num(); ++I)
        { Known.Add(Roles.Components[I]); Parents.Add(Roles.CubeNodes[I]); }
    }

    bool CorrectRoleFeature(const FCkSwayGym_SourceRoles& Roles)
    {
        const auto HasScriptWire = [](const FCk_Handle& H)
        {
            if (!H.Has<ck::FFragment_EntityScript>() ||
                UCk_Utils_Net_UE::Get_EntityReplication(H) != ECk_Replication::Replicates ||
                !H.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>())
            { return false; }
            auto* const Driver = H.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
            return Live(Driver) && Driver->Get_AssociatedEntity() == H;
        };
        for (const auto& H : Roles.Roots)
        { if (!HasScriptWire(H) || !H.Has<ck::FFragment_Transform>() ||
            H.Has<ck::FFragment_SceneNode>()) { return false; } }
        for (const auto& H : Roles.Heads)
        { if (!HasScriptWire(H) || !H.Has<ck::FFragment_Transform>() ||
            H.Has<ck::FFragment_SceneNode>()) { return false; } }
        for (const auto& H : Roles.Tweens)
        { if (!H.Has<ck::FFragment_Tween>() || !H.Has<ck::FFragment_Tween_Params>()) { return false; } }
        for (const auto& H : Roles.Sways)
        { if (!HasScriptWire(H) || !H.Has<ck::FFragment_Sway>() ||
            !H.Has<ck::FFragment_SceneNode>()) { return false; } }
        for (const auto& H : Roles.CubeNodes)
        { if (!HasScriptWire(H) || !H.Has<ck::FFragment_SceneNode>() ||
            H.Has<ck::FFragment_Sway>()) { return false; } }
        for (const auto& H : Roles.Components)
        { if (!H.Has<ck::FFragment_UnrealComponent>()) { return false; } }
        return true;
    }

    bool SafeSourceRoles(const FCkSwayGym_SourceRoles& Roles)
    {
        return Roles.Roots.Num() == 3 && Roles.Heads.Num() == 3 &&
            Roles.Tweens.Num() == 4 && Roles.Sways.Num() == 5 &&
            Roles.CubeNodes.Num() == 7 && Roles.Components.Num() == 7;
    }
}

void UCk_SwayGym_SourceReceipt_UE::Unbind()
{
    if (PostTickHandle.IsValid())
    { FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle); PostTickHandle.Reset(); }
    if (WorldCleanupHandle.IsValid())
    { FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle); WorldCleanupHandle.Reset(); }
    if (auto* const Owner = Source.GetEvenIfUnreachable())
    { Owner->OnEndPlay.RemoveDynamic(this, &UCk_SwayGym_SourceReceipt_UE::OnSourceEndPlay); }
}

void UCk_SwayGym_SourceReceipt_UE::BeginDestroy()
{ Unbind(); Super::BeginDestroy(); }

void UCk_SwayGym_SourceReceipt_UE::OnSourceEndPlay(AActor* Actor,
    EEndPlayReason::Type)
{
    if (!IsInGameThread()) { return; }
    if (Actor == Source.GetEvenIfUnreachable())
    { UCk_SwayGym_SourceRuntime_UE::Fail(this, TEXT("SourceEndPlay")); }
}

void UCk_SwayGym_SourceReceipt_UE::OnWorldCleanup(UWorld* InWorld, bool, bool)
{
    if (!IsInGameThread()) { return; }
    if (InWorld == World.GetEvenIfUnreachable())
    { UCk_SwayGym_SourceRuntime_UE::Fail(this, TEXT("WorldCleanup")); }
}

void UCk_SwayGym_SourceReceipt_UE::OnWorldPostActorTick(UWorld* InWorld,
    ELevelTick TickType, float)
{
    if (bClosed || InWorld != World.Get()) { return; }
    if (State == ECk_SwayGym_RetirementState::Pending &&
        FPlatformTime::Seconds() - StartedAtSeconds >
            CkSwayGymRuntimePrivate::MaxRetireSeconds)
    { UCk_SwayGym_SourceRuntime_UE::Fail(this, TEXT("RetirementTimeout")); return; }
    if (TickType != LEVELTICK_All) { return; }
    if (bInTick)
    { UCk_SwayGym_SourceRuntime_UE::Fail(this, TEXT("ReentrantPostTick")); return; }
    TGuardValue<bool> Guard(bInTick, true);
    UCk_SwayGym_SourceRuntime_UE::Observe(this, InWorld);
}

void UCk_SwayGym_StopReceiver_UE::OnCompleted(FCk_Handle Owner,
    ECk_Request_OperationResult Result)
{
    if (!CkSwayGymRuntimePrivate::Live(this)) { return; }
    auto* const R = Receipt.Get();
    if (!CkSwayGymRuntimePrivate::Live(R) ||
        R->State != ECk_SwayGym_RetirementState::Pending ||
        !R->bStopIssued || Slot < 0 || Slot >= 4 ||
        R->StopReceivers.Num() != 4 || R->Roles.Tweens.Num() != 4 ||
        R->StopReceivers[Slot] != this ||
        bReceived || Owner != R->Roles.Tweens[Slot] ||
        Result != ECk_Request_OperationResult::Succeeded)
    { if (CkSwayGymRuntimePrivate::Live(R)) { R->bStopFailed = true; } return; }
    bReceived = true;
    ++R->StopCallbackCount;
}

bool UCk_SwayGym_SourceRuntime_UE::HasAuthorityContext(
    const UCk_SwayGym_SourceReceipt_UE* R)
{
    if (!CkSwayGymRuntimePrivate::Live(R) || R->Source.IsStale() ||
        R->World.IsStale()) { return false; }
    auto* const Source = R->Source.Get();
    auto* const World = R->World.Get();
    return CkSwayGymRuntimePrivate::Live(Source) &&
        CkSwayGymRuntimePrivate::Live(World) &&
        Source->GetWorld() == World &&
        CkSwayGymRuntimePrivate::SafeOwnerRoot(Source, R->WorldRoot, R->OwnerRoot) &&
        R->Roles.OwnerRoot == R->OwnerRoot;
}

bool UCk_SwayGym_SourceRuntime_UE::HasExactAllocatedClosure(
    const UCk_SwayGym_SourceReceipt_UE* R, bool bBeforeDestroy)
{
    if (!HasAuthorityContext(R) || R->Known.Num() != CkSwayGymRuntimePrivate::KnownCount ||
        R->ExpectedParents.Num() != R->Known.Num()) { return false; }
    const auto Registry = R->WorldRoot.Get_RegistryView();
    const auto RegistryHandle = Registry.Get_RegistryHandle();
    TSet<FCk_Handle> Frozen;
    for (const auto& H : R->Known)
    {
        if (H == FCk_Handle{} || Frozen.Contains(H) ||
            H.Get_RegistryView().Get_RegistryHandle() != RegistryHandle)
        { return false; }
        Frozen.Add(H);
    }
    for (int32 I = 0; I < R->Known.Num(); ++I)
    {
        const auto& H = R->Known[I];
        const bool Allocated =
            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H);
        if (bBeforeDestroy && (!Allocated ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(H,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy)))
        { return false; }
        if (!Allocated) { continue; }
        if (!H.Has<ck::FFragment_LifetimeOwner>() ||
            H.Get<ck::FFragment_LifetimeOwner,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entity() !=
                R->ExpectedParents[I]) { return false; }
        if (H.Has<ck::FFragment_LifetimeDependents>())
        {
            for (const auto& Child : H.Get<ck::FFragment_LifetimeDependents,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entities())
            {
                if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child) &&
                    !Frozen.Contains(Child)) { return false; }
            }
        }
    }
    bool Unknown = false;
    Registry.View<ck::FFragment_LifetimeOwner>().ForEach(
        [&](FCk_Entity Entity, const ck::FFragment_LifetimeOwner& Owner)
        {
            if (!Frozen.Contains(Owner.Get_Entity())) { return; }
            const FCk_Handle Child{Entity, RegistryHandle};
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child) &&
                !Frozen.Contains(Child)) { Unknown = true; }
        });
    return !Unknown && (!bBeforeDestroy ||
        CkSwayGymRuntimePrivate::CorrectRoleFeature(R->Roles));
}

bool UCk_SwayGym_SourceRuntime_UE::HasKnownQueuesDrained(
    const UCk_SwayGym_SourceReceipt_UE* R)
{
    if (!HasExactAllocatedClosure(R, true)) { return false; }
    for (const auto& H : R->Known)
    {
        if ((H.Has<ck::FFragment_Transform_Requests>() &&
             (!H.Get<ck::FFragment_Transform_Requests>().Get_RotationRequests().IsEmpty() ||
              !H.Get<ck::FFragment_Transform_Requests>().Get_LocationRequests().IsEmpty() ||
              H.Get<ck::FFragment_Transform_Requests>().Get_ScaleRequests().IsSet() ||
              !H.Get<ck::FFragment_Transform_Requests>().Get_ForceRefreshRequests().IsEmpty())) ||
            (H.Has<ck::FFragment_SceneNode_Requests>() &&
             !H.Get<ck::FFragment_SceneNode_Requests>().Get_Requests().IsEmpty()) ||
            (H.Has<ck::FFragment_Sway_Requests>() &&
             !H.Get<ck::FFragment_Sway_Requests>().Get_Requests().IsEmpty()) ||
            (H.Has<ck::FFragment_Tween_Requests>() &&
             !H.Get<ck::FFragment_Tween_Requests>().Get_Requests().IsEmpty()))
        { return false; }
    }
    return true;
}

void UCk_SwayGym_SourceRuntime_UE::Fail(UCk_SwayGym_SourceReceipt_UE* R,
    FName Reason)
{
    if (!CkSwayGymRuntimePrivate::Live(R) ||
        R->State == ECk_SwayGym_RetirementState::Complete) { return; }
    R->State = ECk_SwayGym_RetirementState::Unproven;
    if (R->Reason.IsNone()) { R->Reason = Reason; }
    R->bClosed = true;
    R->bHasFrame = false;
    R->Unbind();
}

void UCk_SwayGym_SourceRuntime_UE::Observe(UCk_SwayGym_SourceReceipt_UE* R,
    UWorld* World)
{
    if (!HasAuthorityContext(R) || R->World.Get() != World)
    { Fail(R, TEXT("SourceContextLost")); return; }
    if (R->State == ECk_SwayGym_RetirementState::Active)
    {
        if (!HasExactAllocatedClosure(R, true))
        { Fail(R, TEXT("ActiveSourceChanged")); return; }
        FCkSwayGym_Frame Next;
        Next.Generation = R->Generation;
        if (R->Latest.FrameSerial == MAX_int64)
        { Fail(R, TEXT("FrameSerialOverflow")); return; }
        Next.FrameSerial = R->Latest.FrameSerial + 1;
        for (const auto& H : R->Roles.Heads)
        {
            auto Typed = UCk_Utils_Transform_UE::CastChecked(H);
            const auto Pose = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Typed);
            if (!CkSwayGymRuntimePrivate::SafePose(Pose))
            { Fail(R, TEXT("InvalidHeadPose")); return; }
            Next.HeadWorldPoses.Add(Pose);
        }
        for (const auto& WeakMesh : R->SourceMeshes)
        {
            auto* const Mesh = WeakMesh.Get();
            if (!CkSwayGymRuntimePrivate::Live(Mesh) || !Mesh->IsRegistered() ||
                Mesh->GetWorld() != World)
            { Fail(R, TEXT("SourceMeshLost")); return; }
            const auto Pose = Mesh->GetComponentTransform();
            if (!CkSwayGymRuntimePrivate::SafePose(Pose))
            { Fail(R, TEXT("InvalidCubePose")); return; }
            Next.CubeWorldPoses.Add(Pose);
        }
        for (const auto& H : R->Roles.Sways)
        {
            auto Typed = UCk_Utils_Sway_UE::CastChecked(H);
            const auto Yaw = UCk_Utils_Sway_UE::Get_RotationOffset(Typed).Yaw;
            const auto X = UCk_Utils_Sway_UE::Get_LocationOffset(Typed).X;
            if (!FMath::IsFinite(Yaw) || !FMath::IsFinite(X))
            { Fail(R, TEXT("InvalidSwayOffset")); return; }
            Next.SwayYawDegrees.Add(Yaw);
            Next.SwayXCm.Add(X);
            Next.SwaySettled.Add(UCk_Utils_Sway_UE::Get_IsSettled(Typed));
        }
        if (!HasAuthorityContext(R) || R->State != ECk_SwayGym_RetirementState::Active ||
            Next.HeadWorldPoses.Num() != 3 || Next.CubeWorldPoses.Num() != 7 ||
            Next.SwayYawDegrees.Num() != 5)
        { Fail(R, TEXT("ReentrantSourceSample")); return; }
        R->Latest = MoveTemp(Next);
        R->PublishedEngineFrame = GFrameCounter;
        R->bHasFrame = true;
        return;
    }
    if (R->State != ECk_SwayGym_RetirementState::Pending ||
        R->bStopFailed || !R->bStopIssued)
    { Fail(R, TEXT("RetirementReceiptLost")); return; }
    if (!R->bDestroyIssued)
    {
        if (R->Roles.Tweens.Num() != 4 || R->StopReceivers.Num() != 4 ||
            !HasExactAllocatedClosure(R, true))
        { Fail(R, TEXT("PendingSourceChanged")); return; }
        if (R->StopCallbackCount != 4) { return; }
        for (int32 I = 0; I < 4; ++I)
        {
            if (!CkSwayGymRuntimePrivate::Live(R->StopReceivers[I].Get()) ||
                !R->StopReceivers[I]->bReceived)
            { Fail(R, TEXT("StopCallbackMissing")); return; }
            const auto& H = R->Roles.Tweens[I];
            auto Typed = UCk_Utils_Tween_UE::CastChecked(H);
            if (!ck::IsValid(Typed) ||
                UCk_Utils_Tween_UE::Get_State(Typed) != ECk_TweenState::Cancelled ||
                !H.Has<ck::FTag_Tween_Completed>() ||
                H.Has_Any<ck::FTag_Tween_Playing, ck::FTag_Tween_Paused,
                    ck::FTag_Tween_InYoyoDelay>())
            { Fail(R, TEXT("TweenStillRunnable")); return; }
        }
        if (!HasKnownQueuesDrained(R)) { return; }
        if (!HasExactAllocatedClosure(R, true))
        { Fail(R, TEXT("RetirementClosureChanged")); return; }
        R->bDestroyIssued = true;
        for (int32 I = 0; I < 3; ++I)
        {
            if (!HasAuthorityContext(R) || R->State != ECk_SwayGym_RetirementState::Pending ||
                !HasExactAllocatedClosure(R, I == 0))
            { Fail(R, TEXT("RootDestroyReentry")); return; }
            auto Root = R->Roles.Roots[I];
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        }
        return;
    }
    if (!HasExactAllocatedClosure(R, false))
    { Fail(R, TEXT("UnknownRetiringChild")); return; }
    for (const auto& H : R->Known)
    { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return; } }
    for (const auto& WeakMesh : R->SourceMeshes)
    {
        auto* const Mesh = WeakMesh.Get();
        if (CkSwayGymRuntimePrivate::Live(Mesh) && Mesh->IsRegistered())
        { return; }
    }
    R->State = ECk_SwayGym_RetirementState::Complete;
    R->bClosed = true;
    R->Unbind();
}

bool UCk_SwayGym_SourceRuntime_UE::Get_IsSourceConstructionDrained(
    AGameStateBase* Source, const FCkSwayGym_SourceRoles& Roles)
{
    if (!CkSwayGymRuntimePrivate::Live(Source) ||
        !CkSwayGymRuntimePrivate::SafeSourceRoles(Roles)) { return false; }
    auto* const World = Source->GetWorld();
    if (!CkSwayGymRuntimePrivate::Live(World)) { return false; }
    const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!CkSwayGymRuntimePrivate::SafeOwnerRoot(Source,WorldRoot,Roles.OwnerRoot))
    { return false; }
    TArray<FCk_Handle> Known, Parents;
    CkSwayGymRuntimePrivate::AppendRoles(Roles,Known,Parents,Roles.OwnerRoot);
    if (Known.Num()!=CkSwayGymRuntimePrivate::KnownCount || Parents.Num()!=Known.Num())
    { return false; }
    const auto Registry=WorldRoot.Get_RegistryView();
    const auto RegistryHandle=Registry.Get_RegistryHandle();
    TSet<FCk_Handle> Frozen;
    for (int32 I=0;I<Known.Num();++I)
    {
        const auto& H=Known[I];
        if (H==FCk_Handle{} || Frozen.Contains(H) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) ||
            H.Get_RegistryView().Get_RegistryHandle()!=RegistryHandle ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(H,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !H.Has<ck::FFragment_LifetimeOwner>() ||
            H.Get<ck::FFragment_LifetimeOwner>().Get_Entity()!=Parents[I])
        { return false; }
        Frozen.Add(H);
    }
    for (const auto& H:Known)
    {
        if (H.Has<ck::FFragment_LifetimeDependents>())
        {
            for (const auto& Child:H.Get<ck::FFragment_LifetimeDependents>().Get_Entities())
            {
                if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child) &&
                    !Frozen.Contains(Child)) { return false; }
            }
        }
        if ((H.Has<ck::FFragment_Transform_Requests>() &&
             (!H.Get<ck::FFragment_Transform_Requests>().Get_RotationRequests().IsEmpty() ||
              !H.Get<ck::FFragment_Transform_Requests>().Get_LocationRequests().IsEmpty() ||
              H.Get<ck::FFragment_Transform_Requests>().Get_ScaleRequests().IsSet() ||
              !H.Get<ck::FFragment_Transform_Requests>().Get_ForceRefreshRequests().IsEmpty())) ||
            (H.Has<ck::FFragment_SceneNode_Requests>() &&
             !H.Get<ck::FFragment_SceneNode_Requests>().Get_Requests().IsEmpty()) ||
            (H.Has<ck::FFragment_Sway_Requests>() &&
             !H.Get<ck::FFragment_Sway_Requests>().Get_Requests().IsEmpty()) ||
            (H.Has<ck::FFragment_Tween_Requests>() &&
             !H.Get<ck::FFragment_Tween_Requests>().Get_Requests().IsEmpty()))
        { return false; }
    }
    bool Unknown=false;
    Registry.View<ck::FFragment_LifetimeOwner>().ForEach(
        [&](FCk_Entity Entity,const ck::FFragment_LifetimeOwner& Owner)
        {
            if (!Frozen.Contains(Owner.Get_Entity())) { return; }
            const FCk_Handle Child{Entity,RegistryHandle};
            Unknown |= UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child) &&
                !Frozen.Contains(Child);
        });
    return !Unknown && CkSwayGymRuntimePrivate::CorrectRoleFeature(Roles);
}

UCk_SwayGym_SourceReceipt_UE* UCk_SwayGym_SourceRuntime_UE::TryBegin(
    AGameStateBase* Source, int32 Generation, const FCk_Handle& WorldRoot,
    const FCkSwayGym_SourceRoles& Roles)
{
    if (Generation != 1 || !CkSwayGymRuntimePrivate::SafeOwnerRoot(Source, WorldRoot, Roles.OwnerRoot) ||
        !CkSwayGymRuntimePrivate::SafeSourceRoles(Roles)) { return nullptr; }
    for (auto It = CkSwayGymRuntimePrivate::ClaimedSourceStates.CreateIterator(); It; ++It)
    { if (It->IsStale()) { It.RemoveCurrent(); } }
    const TWeakObjectPtr<AGameStateBase> SourceIdentity{Source};
    for (const auto& Claimed : CkSwayGymRuntimePrivate::ClaimedSourceStates)
    { if (Claimed.HasSameIndexAndSerialNumber(SourceIdentity)) { return nullptr; } }
    CkSwayGymRuntimePrivate::ClaimedSourceStates.Add(SourceIdentity);
    auto* const R = NewObject<UCk_SwayGym_SourceReceipt_UE>(Source);
    if (!CkSwayGymRuntimePrivate::Live(R)) { return nullptr; }
    R->Source = Source; R->World = Source->GetWorld();
    R->WorldRoot = WorldRoot; R->OwnerRoot = Roles.OwnerRoot;
    R->Generation = Generation; R->Roles = Roles;
    CkSwayGymRuntimePrivate::AppendRoles(Roles, R->Known, R->ExpectedParents, R->OwnerRoot);
    if (!HasExactAllocatedClosure(R, true))
    { Fail(R, TEXT("InitialRolesRejected")); return R; }
    for (const auto& H : Roles.Components)
    {
        auto Typed = UCk_Utils_UnrealComponent_UE::CastChecked(H);
        auto* const Mesh = Cast<UStaticMeshComponent>(
            UCk_Utils_UnrealComponent_UE::Get_Component(Typed));
        if (!CkSwayGymRuntimePrivate::Live(Mesh) || !Mesh->IsRegistered() ||
            Mesh->GetWorld() != R->World.Get())
        { Fail(R, TEXT("InitialMeshRejected")); return R; }
        R->SourceMeshes.Add(Mesh);
    }
    R->State = ECk_SwayGym_RetirementState::Active;
    R->bClosed = false;
    Source->OnEndPlay.AddUniqueDynamic(R, &UCk_SwayGym_SourceReceipt_UE::OnSourceEndPlay);
    R->WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
        R, &UCk_SwayGym_SourceReceipt_UE::OnWorldCleanup);
    R->PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(
        R, &UCk_SwayGym_SourceReceipt_UE::OnWorldPostActorTick);
    if (!HasAuthorityContext(R)) { Fail(R, TEXT("InitialContextChanged")); }
    return R;
}

UCk_SwayGym_SourceReceipt_UE* UCk_SwayGym_SourceRuntime_UE::TryBeginSuccessor(
    AGameStateBase* Source, UCk_SwayGym_SourceReceipt_UE* Previous,
    int32 Generation, const FCk_Handle& WorldRoot,
    const FCkSwayGym_SourceRoles& Roles)
{
    if (!CkSwayGymRuntimePrivate::Live(Previous) ||
        Previous->State != ECk_SwayGym_RetirementState::Complete ||
        Previous->bSuccessorAttempted ||
        Previous->Source.Get() != Source || Previous->WorldRoot != WorldRoot ||
        Roles.OwnerRoot != Previous->OwnerRoot ||
        Previous->Generation <= 0 || Previous->Generation >= MAX_int32 ||
        Generation != Previous->Generation + 1 ||
        !HasExactAllocatedClosure(Previous, false) ||
        !CkSwayGymRuntimePrivate::SafeSourceRoles(Roles))
    { return nullptr; }
    for (const auto& H : Previous->Known)
    { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return nullptr; } }
    if (!CkSwayGymRuntimePrivate::SafeOwnerRoot(Source, WorldRoot, Roles.OwnerRoot)) { return nullptr; }
    Previous->bSuccessorAttempted = true; // one-way before fallible allocation
    auto* const R = NewObject<UCk_SwayGym_SourceReceipt_UE>(Source);
    if (!CkSwayGymRuntimePrivate::Live(R)) { return nullptr; }
    R->Source = Source; R->World = Source->GetWorld();
    R->WorldRoot = WorldRoot; R->OwnerRoot = Roles.OwnerRoot;
    R->Generation = Generation; R->Roles = Roles;
    CkSwayGymRuntimePrivate::AppendRoles(Roles, R->Known, R->ExpectedParents, R->OwnerRoot);
    if (!HasExactAllocatedClosure(R, true))
    { Fail(R, TEXT("SuccessorRolesRejected")); return R; }
    for (const auto& H : Roles.Components)
    {
        auto Typed = UCk_Utils_UnrealComponent_UE::CastChecked(H);
        auto* const Mesh = Cast<UStaticMeshComponent>(
            UCk_Utils_UnrealComponent_UE::Get_Component(Typed));
        if (!CkSwayGymRuntimePrivate::Live(Mesh) || !Mesh->IsRegistered() ||
            Mesh->GetWorld() != R->World.Get())
        { Fail(R, TEXT("SuccessorMeshRejected")); return R; }
        R->SourceMeshes.Add(Mesh);
    }
    R->State = ECk_SwayGym_RetirementState::Active;
    R->bClosed = false;
    Source->OnEndPlay.AddUniqueDynamic(R, &UCk_SwayGym_SourceReceipt_UE::OnSourceEndPlay);
    R->WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
        R, &UCk_SwayGym_SourceReceipt_UE::OnWorldCleanup);
    R->PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(
        R, &UCk_SwayGym_SourceReceipt_UE::OnWorldPostActorTick);
    if (!HasAuthorityContext(R)) { Fail(R, TEXT("SuccessorContextChanged")); }
    return R;
}

bool UCk_SwayGym_SourceRuntime_UE::TryCopyLatestFrame(
    const UCk_SwayGym_SourceReceipt_UE* R, FCkSwayGym_Frame& OutFrame)
{
    OutFrame = {};
    if (!HasAuthorityContext(R) || R->bClosed ||
        R->State != ECk_SwayGym_RetirementState::Active || !R->bHasFrame ||
        R->Latest.Generation != R->Generation || R->Latest.FrameSerial <= 0 ||
        GFrameCounter > R->PublishedEngineFrame + 1)
    { return false; }
    OutFrame = R->Latest;
    return true;
}

bool UCk_SwayGym_SourceRuntime_UE::TryBeginHealthyRetirement(
    UCk_SwayGym_SourceReceipt_UE* R)
{
    if (!HasAuthorityContext(R) || R->bClosed ||
        R->State != ECk_SwayGym_RetirementState::Active ||
        !HasExactAllocatedClosure(R, true))
    { return false; }
    R->State = ECk_SwayGym_RetirementState::Pending;
    R->bHasFrame = false; // close publication before any stop callback
    R->StartedAtSeconds = FPlatformTime::Seconds();
    for (int32 I = 0; I < 4; ++I)
    {
        auto* const Receiver = NewObject<UCk_SwayGym_StopReceiver_UE>(R);
        if (!CkSwayGymRuntimePrivate::Live(Receiver))
        { Fail(R, TEXT("StopReceiverAllocation")); return false; }
        Receiver->Receipt = R; Receiver->Slot = I;
        R->StopReceivers.Add(Receiver);
    }
    TArray<FCk_Delegate_Request_OnCompleted> Delegates;
    for (int32 I = 0; I < 4; ++I)
    {
        FCk_Delegate_Request_OnCompleted Delegate;
        Delegate.BindDynamic(R->StopReceivers[I],
            &UCk_SwayGym_StopReceiver_UE::OnCompleted);
        if (!Delegate.IsBound())
        { Fail(R, TEXT("StopDelegateBinding")); return false; }
        Delegates.Add(Delegate);
    }
    R->bStopIssued = true;
    for (int32 I = 0; I < 4; ++I)
    {
        if (!HasAuthorityContext(R) || R->State != ECk_SwayGym_RetirementState::Pending)
        { Fail(R, TEXT("StopIssueReentry")); return false; }
        auto Tween = UCk_Utils_Tween_UE::CastChecked(R->Roles.Tweens[I]);
        const auto Returned = UCk_Utils_Tween_UE::Stop(
            Tween, ECk_TweenStopBehavior::DoNothing, Delegates[I]);
        if (Returned != R->Roles.Tweens[I] || Tween != R->Roles.Tweens[I] ||
            R->bStopFailed)
        { Fail(R, TEXT("StopIssueRejected")); return false; }
    }
    return true;
}

ECk_SwayGym_RetirementState UCk_SwayGym_SourceRuntime_UE::Get_RetirementState(
    const UCk_SwayGym_SourceReceipt_UE* R)
{ return CkSwayGymRuntimePrivate::Live(R) ? R->State : ECk_SwayGym_RetirementState::Unproven; }

FName UCk_SwayGym_SourceRuntime_UE::Get_ClosureReason(
    const UCk_SwayGym_SourceReceipt_UE* R)
{ return CkSwayGymRuntimePrivate::Live(R) ? R->Reason : NAME_None; }

TArray<FCk_Handle> UCk_SwayGym_SourceRuntime_UE::Get_CapturedKnown(
    const UCk_SwayGym_SourceReceipt_UE* R)
{ return CkSwayGymRuntimePrivate::Live(R) ? R->Known : TArray<FCk_Handle>{}; }

FCkSwayGym_SourceRoles UCk_SwayGym_SourceRuntime_UE::Get_CapturedRoles(
    const UCk_SwayGym_SourceReceipt_UE* R)
{ return CkSwayGymRuntimePrivate::Live(R) ? R->Roles : FCkSwayGym_SourceRoles{}; }

bool UCk_SwayGym_SourceRuntime_UE::Request_Close(UCk_SwayGym_SourceReceipt_UE* R)
{
    if (!CkSwayGymRuntimePrivate::Live(R)) { return false; }
    Fail(R, TEXT("SourceClosed"));
    return true;
}
