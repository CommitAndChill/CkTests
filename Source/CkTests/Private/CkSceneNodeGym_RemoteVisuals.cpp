#include "CkSceneNodeGym_RemoteVisuals.h"
#include "CkSceneNodeGym_ActorLedger.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"

namespace CkSceneNodeRemotePrivate
{
    bool LiveActor(const AActor* Actor)
    { return IsValid(Actor) && !Actor->IsActorBeingDestroyed() &&
        !Actor->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed); }

    bool Client(AGameStateBase* Source)
    { return IsInGameThread() && LiveActor(Source) && !Source->HasAuthority() &&
        IsValid(Source->GetWorld()) && !Source->GetWorld()->bIsTearingDown &&
        Source->GetWorld()->GetNetMode() == NM_Client && Source->GetWorld()->GetGameState() == Source; }

    bool AppliedPose(const AActor* Actor, FCk_Handle& Entity)
    {
        Entity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor);
        if (!ck::IsValid(Entity) || !Entity.Has_All<ck::FFragment_Transform, ck::FFragment_Transform_RootComponent, ck::FTag_Transform_Movable,
                TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>() ||
            Entity.Has_Any<ck::FFragment_SceneNode, ck::SceneNodeParent,
                ck::FFragment_SceneNode_UnrealAnchor, ck::FTag_RepFragments_PendingApply,
                ck::FFragment_EntityScript_PendingRetry>() ||
            UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Entity) != Actor)
        { return false; }
        if (Entity.Has<ck::FFragment_Transform_Requests>())
        {
            const auto& Requests = Entity.Get<ck::FFragment_Transform_Requests>();
            if (!Requests.Get_LocationRequests().IsEmpty() || !Requests.Get_RotationRequests().IsEmpty() ||
                Requests.Get_ScaleRequests().IsSet() || !Requests.Get_ForceRefreshRequests().IsEmpty())
            { return false; }
        }
        auto* const Driver = Entity.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
        if (!IsValid(Driver) || Driver->Get_AssociatedEntity() != Entity) { return false; }
        const auto* L = Driver->FindEntry(FCk_RepData_Location::StaticStruct());
        const auto* R = Driver->FindEntry(FCk_RepData_Rotation::StaticStruct());
        const auto* S = Driver->FindEntry(FCk_RepData_Scale::StaticStruct());
        if (!L || !R || !S || L->_PendingApply || R->_PendingApply || S->_PendingApply ||
            !L->_WasEverApplied || !R->_WasEverApplied || !S->_WasEverApplied ||
            L->_LastAppliedData.GetScriptStruct() != FCk_RepData_Location::StaticStruct() ||
            R->_LastAppliedData.GetScriptStruct() != FCk_RepData_Rotation::StaticStruct() ||
            S->_LastAppliedData.GetScriptStruct() != FCk_RepData_Scale::StaticStruct()) { return false; }
        const FTransform Applied(R->_LastAppliedData.Get<FCk_RepData_Rotation>().Value,
            L->_LastAppliedData.Get<FCk_RepData_Location>().Value,
            S->_LastAppliedData.Get<FCk_RepData_Scale>().Value);
        const auto Pose = Entity.Get<ck::FFragment_Transform>().Get_Transform();
        const auto* Root = Cast<UStaticMeshComponent>(Actor->GetRootComponent());
        return IsValid(Root) && Root->GetOwner() == Actor && Root->GetWorld() == Actor->GetWorld() &&
            Root->IsRegistered() && Entity.Get<ck::FFragment_Transform_RootComponent>().Get_RootComponent().Get() == Root && Root->Mobility == EComponentMobility::Movable &&
            IsValid(Root->GetStaticMesh()) && Pose.IsValid() && !Pose.ContainsNaN() &&
            Pose.Equals(Applied) && Root->GetComponentTransform().Equals(Pose);
    }
}

UCk_SceneNodeGym_RemoteVisuals_UE::UCk_SceneNodeGym_RemoteVisuals_UE()
{ PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(false); }
void UCk_SceneNodeGym_RemoteVisuals_UE::Fail(FName Reason, const AActor*)
{
    bClosed = true; FailureReason = Reason; State = ECk_SceneNodeGym_RemoteVisualState::Unproven;
    for (int32 Role = 1; Role < Roles.Num(); ++Role)
    { UCk_SceneNodeGym_RemoteVisualRuntime_UE::Request_SetVisualVisible(Roles[Role].Actor.Get(), false); }
}
void UCk_SceneNodeGym_RemoteVisualRuntime_UE::Request_SetVisualVisible(AActor* Actor, bool Visible)
{
    if (!IsInGameThread() || !CkSceneNodeRemotePrivate::LiveActor(Actor) ||
        !Actor->IsA<ACk_SceneNodeGym_SourceActor_UE>()) { return; }
    TArray<USceneComponent*> Components;
    Actor->GetComponents(Components);
    for (auto* Component : Components)
    { if (IsValid(Component) && Component->GetOwner() == Actor) { Component->SetVisibility(Visible, false); } }
}
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::HasClientContext(const UCk_SceneNodeGym_RemoteVisuals_UE* R)
{ return IsValid(R) && CkSceneNodeRemotePrivate::Client(R->Source.Get()) &&
    R->GetOwner() == R->Source.Get() && R->ClientWorld.Get() == R->Source->GetWorld(); }
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryMakeFrame(int32 Generation, int64 Serial,
    const TArray<FTransform>& Poses, FCkSceneNodeGym_Frame& Out)
{
    Out = {};
    if (Generation <= 0 || Serial <= 0 || !Poses.IsEmpty()) { return false; }
    Out.Generation = Generation; Out.FrameSerial = Serial;
    return true;
}
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::HasSafeFrame(int64 Sequence, const FCkSceneNodeGym_Frame& Frame)
{ return Sequence > 0 && Frame.Generation > 0 && Frame.FrameSerial > 0 && Frame.WorldPoses.IsEmpty(); }
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::HasSafeClasses(const TArray<TSubclassOf<AActor>>& Classes)
{
    if (Classes.Num() != 17) { return false; }
    for (const auto& Class : Classes)
    {
        if (!IsValid(Class.Get()) || !Class->IsChildOf(ACk_SceneNodeGym_SourceActor_UE::StaticClass()))
        { return false; }
        const auto* Default = Class->GetDefaultObject<AActor>();
        if (!IsValid(Default) || !Default->GetIsReplicated() || Default->IsReplicatingMovement())
        { return false; }
    }
    return true;
}
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::HasNoOtherRemoteReceipt(AGameStateBase* Source,
    UCk_SceneNodeGym_RemoteVisuals_UE* Allowed)
{
    TArray<UCk_SceneNodeGym_RemoteVisuals_UE*> Receipts;
    Source->GetComponents(Receipts);
    for (auto* R : Receipts)
    { if (R != Allowed && R->State != ECk_SceneNodeGym_RemoteVisualState::Complete) { return false; } }
    return true;
}
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::HasExactCurrentRoles(
    const UCk_SceneNodeGym_RemoteVisuals_UE* R, bool)
{
    if (!HasClientContext(R) || R->Roles.Num() != 18 || R->VisualClasses.Num() != 17) { return false; }
    const auto* Scope = Cast<ACk_SceneNodeGym_ScopeActor_UE>(R->Roles[0].Actor.Get());
    if (!CkSceneNodeRemotePrivate::LiveActor(Scope) || Scope->GetClass() != ACk_SceneNodeGym_ScopeActor_UE::StaticClass() ||
        Scope->Get_ActorSource() != R->Source.Get() || Scope->GetOwner() != R->Source.Get() ||
        Scope->Get_ActorGeneration() != R->Generation || !Scope->GetIsReplicated() ||
        Scope->IsReplicatingMovement()) { return false; }
    TSet<const AActor*> Unique;
    for (int32 Role = 1; Role <= 17; ++Role)
    {
        auto* Actor = Cast<ACk_SceneNodeGym_SourceActor_UE>(R->Roles[Role].Actor.Get());
        FCk_Handle Entity;
        if (!CkSceneNodeRemotePrivate::LiveActor(Actor) || Unique.Contains(Actor) ||
            Actor->GetWorld() != R->ClientWorld.Get() || Actor->GetClass() != R->VisualClasses[Role - 1].Get() ||
            Actor->Get_ActorSource() != R->Source.Get() || Actor->GetOwner() != Scope ||
            Actor->Get_ActorRole() != Role || Actor->Get_ActorGeneration() != R->Generation ||
            !Actor->GetIsReplicated() || Actor->IsReplicatingMovement() ||
            !CkSceneNodeRemotePrivate::AppliedPose(Actor, Entity) || Entity != R->Roles[Role].Entity)
        { return false; }
        Unique.Add(Actor);
    }
    return true;
}
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::HasNoUnknownDescendants(const UCk_SceneNodeGym_RemoteVisuals_UE* R)
{
    if (!HasClientContext(R)) { return false; }
    for (TActorIterator<AActor> It(R->ClientWorld.Get()); It; ++It)
    {
        if (!CkSceneNodeRemotePrivate::LiveActor(*It) || It->GetOwner() != R->FrozenScopeIdentity.Get()) { continue; }
        bool Known = false;
        for (int32 Role = 1; Role <= 17; ++Role)
        { if (R->Roles[Role].Actor.Get() == *It) { Known = true; break; } }
        if (!Known) { return false; }
    }
    return true;
}
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::HasAllRecordedActorsAbsent(const UCk_SceneNodeGym_RemoteVisuals_UE* R)
{
    if (!IsValid(R) || R->Roles.Num() != 18) { return false; }
    for (const auto& Role : R->Roles)
    {
        if (CkSceneNodeRemotePrivate::LiveActor(Role.Actor.Get()) ||
            ck::IsValid(Role.Entity, ck::IsValid_Policy_IncludePendingKill{})) { return false; }
    }
    return true;
}
UCk_SceneNodeGym_RemoteVisuals_UE* UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryBegin(
    AGameStateBase* Source, int64 Sequence, const FCkSceneNodeGym_Frame& Received,
    const TArray<TSubclassOf<AActor>>& Classes)
{
    if (!CkSceneNodeRemotePrivate::Client(Source) || !HasSafeFrame(Sequence, Received) ||
        !HasSafeClasses(Classes) || !HasNoOtherRemoteReceipt(Source)) { return nullptr; }
    TArray<AActor*> Actors; Actors.SetNumZeroed(18);
    for (TActorIterator<ACk_SceneNodeGym_ScopeActor_UE> It(Source->GetWorld()); It; ++It)
    {
        if (It->Get_ActorSource() != Source || It->Get_ActorGeneration() != Received.Generation) { continue; }
        if (Actors[0] || !CkSceneNodeRemotePrivate::LiveActor(*It) || It->GetOwner() != Source) { return nullptr; }
        Actors[0] = *It;
    }
    if (!Actors[0]) { return nullptr; } // Cross-channel metadata/owner arrival can be later than construction.
    TArray<FCk_Handle> Entities; Entities.SetNum(18);
    for (TActorIterator<ACk_SceneNodeGym_SourceActor_UE> It(Source->GetWorld()); It; ++It)
    {
        if (It->Get_ActorSource() != Source || It->Get_ActorGeneration() != Received.Generation) { continue; }
        const int32 Role = It->Get_ActorRole();
        if (Role < 1 || Role > 17 || Actors[Role] || It->GetOwner() != Actors[0] ||
            It->GetClass() != Classes[Role - 1].Get() ||
            !CkSceneNodeRemotePrivate::AppliedPose(*It, Entities[Role])) { return nullptr; }
        Actors[Role] = *It;
    }
    for (auto* Actor : Actors) { if (!Actor) { return nullptr; } }
    auto* R = NewObject<UCk_SceneNodeGym_RemoteVisuals_UE>(Source);
    if (!IsValid(R)) { return nullptr; }
    R->Source = Source; R->ClientWorld = Source->GetWorld(); R->Generation = Received.Generation;
    R->VisualClasses = Classes; R->FrozenScopeIdentity = Actors[0]; R->Roles.SetNum(18);
    for (int32 Role = 0; Role < 18; ++Role)
    { R->Roles[Role].Actor = Actors[Role]; R->Roles[Role].Entity = Entities[Role];
      R->Roles[Role].bCaptured = true; R->Roles[Role].bFinished = true; }
    Source->AddInstanceComponent(R); R->RegisterComponent();
    if (!R->IsRegistered() || !HasExactCurrentRoles(R, true) || !HasNoUnknownDescendants(R))
    { R->Fail(TEXT("ProductionCohortMismatch")); return R; }
    R->State = ECk_SceneNodeGym_RemoteVisualState::Ready;
    R->InstalledSequence = Sequence; R->InstalledFrameSerial = Received.FrameSerial; R->InstalledFrame = Received;
    for (int32 Role = 1; Role <= 17; ++Role) { Request_SetVisualVisible(R->Roles[Role].Actor.Get(), true); }
    return R;
}
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryInstall(UCk_SceneNodeGym_RemoteVisuals_UE* R,
    int64 Sequence, const FCkSceneNodeGym_Frame& Received)
{
    if (!HasClientContext(R) || R->bClosed || R->State != ECk_SceneNodeGym_RemoteVisualState::Ready ||
        !HasSafeFrame(Sequence, Received) || Received.Generation != R->Generation ||
        Sequence <= R->InstalledSequence || Received.FrameSerial <= R->InstalledFrameSerial ||
        !HasExactCurrentRoles(R, true)) { return false; }
    if (!HasNoUnknownDescendants(R)) { R->Fail(TEXT("UnknownScopedActor")); return false; }
    R->InstalledSequence = Sequence; R->InstalledFrameSerial = Received.FrameSerial; R->InstalledFrame = Received;
    return true; // Metadata receipt only. Production processors own every visible pose write.
}
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_IsInstalledConformant(
    const UCk_SceneNodeGym_RemoteVisuals_UE* R, int64 Sequence, const FCkSceneNodeGym_Frame& Received)
{
    return HasSafeFrame(Sequence, Received) && IsValid(R) && !R->bClosed &&
        R->State == ECk_SceneNodeGym_RemoteVisualState::Ready && R->Generation == Received.Generation &&
        R->InstalledSequence == Sequence && R->InstalledFrameSerial == Received.FrameSerial &&
        HasExactCurrentRoles(R, true) && HasNoUnknownDescendants(R);
}
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryGet_FinishedRole(
    const UCk_SceneNodeGym_RemoteVisuals_UE* R, int32 Role, AActor*& Out)
{
    Out = nullptr;
    if (!IsValid(R) || Role < 0 || Role >= 18 || R->Roles.Num() != 18) { return false; }
    Out = R->Roles[Role].Actor.Get();
    return CkSceneNodeRemotePrivate::LiveActor(Out);
}
bool UCk_SceneNodeGym_RemoteVisualRuntime_UE::Request_Retire(UCk_SceneNodeGym_RemoteVisuals_UE* R)
{
    if (!IsInGameThread() || !IsValid(R) || R->Roles.Num() != 18 ||
        R->State == ECk_SceneNodeGym_RemoteVisualState::Unproven) { return false; }
    R->bClosed = true; R->State = ECk_SceneNodeGym_RemoteVisualState::Retiring;
    for (int32 Role = 1; Role <= 17; ++Role) { Request_SetVisualVisible(R->Roles[Role].Actor.Get(), false); }
    return true; // Authority retirement/actor-channel teardown owns destruction, never this receipt.
}
ECk_SceneNodeGym_RemoteVisualState UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_State(
    const UCk_SceneNodeGym_RemoteVisuals_UE* R)
{
    if (!IsValid(R)) { return ECk_SceneNodeGym_RemoteVisualState::Unproven; }
    if (R->State == ECk_SceneNodeGym_RemoteVisualState::Retiring && HasAllRecordedActorsAbsent(R))
    { auto* Mutable = const_cast<UCk_SceneNodeGym_RemoteVisuals_UE*>(R);
      Mutable->State = ECk_SceneNodeGym_RemoteVisualState::Complete; }
    return R->State;
}
FName UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_FailureReason(const UCk_SceneNodeGym_RemoteVisuals_UE* R)
{ return IsValid(R) ? R->FailureReason : TEXT("InvalidReceipt"); }
int32 UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_Generation(const UCk_SceneNodeGym_RemoteVisuals_UE* R)
{ return IsValid(R) ? R->Generation : 0; }
int64 UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_InstalledSequence(const UCk_SceneNodeGym_RemoteVisuals_UE* R)
{ return IsValid(R) ? R->InstalledSequence : 0; }
int64 UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_InstalledFrameSerial(const UCk_SceneNodeGym_RemoteVisuals_UE* R)
{ return IsValid(R) ? R->InstalledFrameSerial : 0; }
UCk_SceneNodeGym_RemoteVisuals_UE* UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryBeginSuccessor(
    AGameStateBase* Source, UCk_SceneNodeGym_RemoteVisuals_UE* Old, int64 Sequence,
    const FCkSceneNodeGym_Frame& Received, const TArray<TSubclassOf<AActor>>& Classes)
{
    if (!HasClientContext(Old) || Old->Source.Get() != Source ||
        Get_State(Old) != ECk_SceneNodeGym_RemoteVisualState::Complete ||
        Received.Generation <= Old->Generation || Sequence <= Old->InstalledSequence) { return nullptr; }
    return TryBegin(Source, Sequence, Received, Classes);
}
