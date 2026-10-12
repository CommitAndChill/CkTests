#include "CkCrowdRemoteBodyProjector.h"

#include "CkCrowd/Settings/CkCrowd_DebugSettings.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkPmg/CkPmg_Fragment.h"
#include "CkPmg/CkPmg_Utils_DebugShapes.h"

#include <Engine/LocalPlayer.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>

auto
    UCkCrowdRemoteBodyProjector::
    GetWorld() const
    -> UWorld*
{
    return _World.Get();
}

auto
    UCkCrowdRemoteBodyProjector::
    TryBind(UWorld* InWorld, APlayerController* InLocalController)
    -> bool
{
    if (!IsInGameThread() || !::IsValid(this) ||
        HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        _World.IsValid() || _RetireRequested || _Failed ||
        !::IsValid(InWorld) || !::IsValid(InLocalController) ||
        InWorld->bIsTearingDown ||
        InWorld->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        InLocalController->IsActorBeingDestroyed() ||
        InWorld->GetNetMode() != NM_Client ||
        InLocalController->GetWorld() != InWorld ||
        !InLocalController->IsLocalController() ||
        InLocalController->GetLocalPlayer() == nullptr ||
        ck::Is_NOT_Valid(UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*InWorld)))
    { return false; }
    _World = InWorld;
    _LocalController = InLocalController;
    return true;
}

auto
    UCkCrowdRemoteBodyProjector::
    IsClientWorldReady() const
    -> bool
{
    UWorld* World = _World.Get();
    APlayerController* PC = _LocalController.Get();
    return IsInGameThread() && ::IsValid(World) && ::IsValid(PC) &&
        !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        !World->bIsTearingDown &&
        !PC->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        !PC->IsActorBeingDestroyed() && World->GetNetMode() == NM_Client &&
        PC->GetWorld() == World && PC->IsLocalController() && PC->GetLocalPlayer() != nullptr &&
        ck::IsValid(UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World));
}

auto
    UCkCrowdRemoteBodyProjector::
    HasStageContext(const FCk_Handle& InFrozenRoot,
        const FCkCrowdRemoteAgentId& InId, const FBody& InExpected) const
    -> bool
{
    if (!IsInGameThread() || !::IsValid(this) ||
        HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        _Failed || _RetireRequested || !_Staging || _StageRoot != InFrozenRoot ||
        !IsClientWorldReady())
    { return false; }
    const FBody* Slot = _StageBodies.Find(InId);
    if (Slot == nullptr || Slot->Row.Id != InId ||
        Slot->Entity != InExpected.Entity ||
        FCk_Handle{Slot->Transform} != FCk_Handle{InExpected.Transform} ||
        Slot->Capsule != InExpected.Capsule || Slot->Cone != InExpected.Cone)
    { return false; }
    if (InExpected.Entity != FCk_Handle{} &&
        (!IsLive(InExpected.Entity) ||
         !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(InExpected.Entity) ||
         UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(InExpected.Entity) != InFrozenRoot))
    { return false; }
    if (FCk_Handle{InExpected.Transform} != FCk_Handle{} &&
        FCk_Handle{InExpected.Transform} != InExpected.Entity)
    { return false; }
    for (const FCk_Handle Shape : {FCk_Handle{InExpected.Capsule}, FCk_Handle{InExpected.Cone}})
    {
        if (Shape == FCk_Handle{}) { continue; }
        if (!IsLive(Shape) ||
            Shape.Get_RegistryView().Get_RegistryHandle() !=
                InFrozenRoot.Get_RegistryView().Get_RegistryHandle() ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Shape) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Shape) != InExpected.Entity)
        { return false; }
    }
    return true;
}

auto
    UCkCrowdRemoteBodyProjector::
    HasQueueBody(const FBody& InExpected, bool bStage) const
    -> bool
{
    if (!IsInGameThread() || !::IsValid(this) ||
        HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        _Failed || _RetireRequested || !IsClientWorldReady() ||
        (bStage ? !_Staging || !_Revealing : _Staging))
    { return false; }
    const FBody* Current = (bStage ? _StageBodies : _Bodies).Find(InExpected.Row.Id);
    return Current != nullptr && Current->Entity == InExpected.Entity &&
        FCk_Handle{Current->Transform} == FCk_Handle{InExpected.Transform} &&
        Current->Capsule == InExpected.Capsule && Current->Cone == InExpected.Cone &&
        IsLive(bStage ? _StageRoot : _Root);
}

auto
    UCkCrowdRemoteBodyProjector::
    IsLive(const FCk_Handle& InHandle) const
    -> bool
{
    return ck::IsValid(InHandle) &&
        !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
            InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
}

auto
    UCkCrowdRemoteBodyProjector::
    HasExactAllocatedChildren(const FCk_Handle& InParent,
        const TSet<FCk_Handle>& InExpected) const
    -> bool
{
    if (!IsLive(InParent)) { return false; }
    TSet<FCk_Handle> Seen;
    if (InParent.Has<ck::FFragment_LifetimeDependents>())
    {
        const auto& Raw = InParent.Get<ck::FFragment_LifetimeDependents>().Get_Entities();
        for (const FCk_Handle& Child : Raw)
        {
            if (!ck::IsValid(Child, ck::IsValid_Policy_IncludePendingKill{})) { continue; }
            if (!IsLive(Child) || !InExpected.Contains(Child) || Seen.Contains(Child) ||
                Child.Get_RegistryView().Get_RegistryHandle() !=
                    InParent.Get_RegistryView().Get_RegistryHandle() ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) != InParent)
            { return false; }
            Seen.Add(Child);
        }
    }
    return Seen.Num() == InExpected.Num();
}

auto
    UCkCrowdRemoteBodyProjector::
    HasNoNewAllocatedChildren(const FCk_Handle& InParent,
        const TSet<FCk_Handle>& InExpected) const
    -> bool
{
    if (!ck::IsValid(InParent, ck::IsValid_Policy_IncludePendingKill{}))
    {
        for (const FCk_Handle& Expected : InExpected)
        { if (ck::IsValid(Expected, ck::IsValid_Policy_IncludePendingKill{})) { return false; } }
        return true;
    }
    TSet<FCk_Handle> Seen;
    if (InParent.Has<ck::FFragment_LifetimeDependents>())
    {
        const auto& Raw = InParent.Get<ck::FFragment_LifetimeDependents,
            ck::IsValid_Policy_IncludePendingKill>().Get_Entities();
        for (const FCk_Handle& Child : Raw)
        {
            if (!ck::IsValid(Child, ck::IsValid_Policy_IncludePendingKill{})) { continue; }
            if (!InExpected.Contains(Child) || Seen.Contains(Child) ||
                Child.Get_RegistryView().Get_RegistryHandle() !=
                    InParent.Get_RegistryView().Get_RegistryHandle() ||
                !Child.Has<ck::FFragment_LifetimeOwner>() ||
                Child.Get<ck::FFragment_LifetimeOwner,
                    ck::IsValid_Policy_IncludePendingKill>().Get_Entity() != InParent)
            { return false; }
            Seen.Add(Child);
        }
    }
    for (const FCk_Handle& Expected : InExpected)
    {
        if (ck::IsValid(Expected, ck::IsValid_Policy_IncludePendingKill{}) &&
            !Seen.Contains(Expected))
        { return false; }
    }
    return true;
}

auto
    UCkCrowdRemoteBodyProjector::
    HasNoNewAllocatedChildren(const FRetiringTree& InTree) const
    -> bool
{
    TSet<FCk_Handle> ExpectedBodies;
    for (const auto& Pair : InTree.Bodies)
    {
        const FBody& Body = Pair.Value;
        ExpectedBodies.Add(Body.Entity);
        TSet<FCk_Handle> ExpectedShapes;
        ExpectedShapes.Add(Body.Capsule);
        ExpectedShapes.Add(Body.Cone);
        if (!HasNoNewAllocatedChildren(Body.Entity, ExpectedShapes) ||
            !HasNoNewAllocatedChildren(Body.Capsule, {}) ||
            !HasNoNewAllocatedChildren(Body.Cone, {}))
        { return false; }
    }
    return HasNoNewAllocatedChildren(InTree.Root, ExpectedBodies);
}

auto
    UCkCrowdRemoteBodyProjector::
    IsComplete(const FCk_Handle& InRoot,
        const TMap<FCkCrowdRemoteAgentId, FBody>& InBodies) const
    -> bool
{
    if (!IsLive(InRoot)) { return false; }
    TSet<FCk_Handle> ExpectedBodies;
    for (const auto& Pair : InBodies)
    {
        const FBody& Body = Pair.Value;
        const FCk_Handle Capsule = Body.Capsule;
        const FCk_Handle Cone = Body.Cone;
        if (!IsLive(Body.Entity) || !IsLive(Capsule) || !IsLive(Cone) ||
            ExpectedBodies.Contains(Body.Entity) || Capsule == Cone ||
            !UCk_Utils_Transform_UE::Has(Body.Entity) ||
            static_cast<FCk_Handle>(Body.Transform) != Body.Entity ||
            !UCk_Utils_Pmg_DebugShape_UE::Has(Capsule) ||
            !UCk_Utils_Pmg_DebugShape_UE::Has(Cone) ||
            !UCk_Utils_Transform_UE::Has(Capsule) ||
            !UCk_Utils_Transform_UE::Has(Cone) ||
            !UCk_Utils_SceneNode_UE::Has(Capsule) ||
            !UCk_Utils_SceneNode_UE::Has(Cone) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Body.Entity) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Capsule) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Cone) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Body.Entity) != InRoot ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Capsule) != Body.Entity ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Cone) != Body.Entity)
        { return false; }
        ExpectedBodies.Add(Body.Entity);
        const auto CapsuleNode = UCk_Utils_SceneNode_UE::CastChecked(Capsule);
        const auto ConeNode = UCk_Utils_SceneNode_UE::CastChecked(Cone);
        if (!IsLive(static_cast<FCk_Handle>(CapsuleNode)) ||
            !IsLive(static_cast<FCk_Handle>(ConeNode)) ||
            UCk_Utils_SceneNode_UE::Get_Parent(CapsuleNode) != Body.Transform ||
            UCk_Utils_SceneNode_UE::Get_Parent(ConeNode) != Body.Transform)
        { return false; }
        if (Body.bMeshesCaptured)
        {
            const auto* CapsuleMesh = Capsule.Get<ck::FFragment_Pmg_DebugShape>()
                .Get_MeshComponent().Get();
            const auto* ConeMesh = Cone.Get<ck::FFragment_Pmg_DebugShape>()
                .Get_MeshComponent().Get();
            UWorld* World = _World.Get();
            if (!::IsValid(World) || !Body.CapsuleMesh.IsValid() ||
                !Body.ConeMesh.IsValid() ||
                CapsuleMesh != Body.CapsuleMesh.Get() ||
                ConeMesh != Body.ConeMesh.Get() ||
                !::IsValid(CapsuleMesh) || !::IsValid(ConeMesh) ||
                CapsuleMesh->GetWorld() != World || ConeMesh->GetWorld() != World ||
                CapsuleMesh->GetOuter() != World || ConeMesh->GetOuter() != World)
            { return false; }
        }
        TSet<FCk_Handle> ExpectedShapes;
        ExpectedShapes.Add(Capsule);
        ExpectedShapes.Add(Cone);
        if (!HasExactAllocatedChildren(Body.Entity, ExpectedShapes) ||
            !HasExactAllocatedChildren(Capsule, {}) ||
            !HasExactAllocatedChildren(Cone, {}))
        { return false; }
    }
    return HasExactAllocatedChildren(InRoot, ExpectedBodies);
}

auto
    UCkCrowdRemoteBodyProjector::
    ResolveTint(const FCkCrowdRemoteAgentRow& InRow)
    -> FLinearColor
{
    if (InRow.bIsPathPending)
    { return FMath::Lerp(InRow.BaseColor, FLinearColor(1.0f, 0.92f, 0.20f, 1.0f), 0.55f); }
    if (InRow.bIsDisabled)
    { return FMath::Lerp(InRow.BaseColor, FLinearColor(0.45f, 0.45f, 0.45f, 1.0f), 0.65f); }
    return InRow.BaseColor;
}

auto
    UCkCrowdRemoteBodyProjector::
    TryStage(const TArray<FCkCrowdRemoteAgentRow>& InRows)
    -> bool
{
    UWorld* World = _World.Get();
    if (!IsInGameThread() || !::IsValid(this) ||
        HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        !::IsValid(World) || !IsClientWorldReady()) { return false; }
    const TArray<FCkCrowdRemoteAgentRow> FrozenRows = InRows;
    const FCk_Handle FrozenRoot =
        UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
    if (!::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    _StageRoot = FrozenRoot;
    if (_Failed || _RetireRequested || _World.Get() != World ||
        !IsClientWorldReady() || !IsLive(FrozenRoot))
    { FailAndRetire(); return false; }
    _Staging = true;
    _StageBodies.Reset();
    for (const FCkCrowdRemoteAgentRow& Row : FrozenRows)
    {
        FBody Body;
        Body.Row = Row;
        _StageBodies.Add(Row.Id, Body);
        if (!HasStageContext(FrozenRoot, Row.Id, Body)) { break; }
        const FCk_Handle Created = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(FrozenRoot);
        if (!::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        FBody* Slot = _StageBodies.Find(Row.Id);
        if (Slot == nullptr) { FailAndRetire(); return false; }
        Slot->Entity = Created;
        Body.Entity = Created;
        if (!HasStageContext(FrozenRoot, Row.Id, Body) || !IsLive(Created)) { break; }
        auto MutableCreated = Created;
        auto Transform = UCk_Utils_Transform_UE::Add(
            MutableCreated, Row.WorldTransform, ECk_Replication::DoesNotReplicate);
        if (!::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        Slot = _StageBodies.Find(Row.Id);
        if (Slot == nullptr) { FailAndRetire(); return false; }
        Slot->Transform = Transform;
        Body.Transform = Transform;
        if (!HasStageContext(FrozenRoot, Row.Id, Body) ||
            !IsLive(FCk_Handle{Transform}) || !IsLive(Created)) { break; }

        const float HalfHeight = Row.HeightCm * 0.5f;
        const FLinearColor Color = ResolveTint(Row);
        MutableCreated = Created;
        const auto CapsuleReturn = UCk_Utils_Pmg_BasicShapes::Create_Capsule(
            MutableCreated, FTransform::Identity, Row.RadiusCm, HalfHeight,
            16, 8, ECk_Plane_Axis::XY, Color, true, 2.0f, -1.0f);
        if (!::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        Slot = _StageBodies.Find(Row.Id);
        if (Slot == nullptr) { FailAndRetire(); return false; }
        Slot->Capsule = CapsuleReturn;
        Body.Capsule = CapsuleReturn;
        if (!HasStageContext(FrozenRoot, Row.Id, Body) ||
            !IsLive(FCk_Handle{CapsuleReturn}) || !IsLive(Created)) { break; }
        MutableCreated = Created;
        const auto ConeReturn = UCk_Utils_Pmg_BasicShapes::Create_Cone(
            MutableCreated, FTransform::Identity, Row.RadiusCm * 0.36f,
            HalfHeight * 0.625f, 12, ECk_Plane_Axis::XY, Color,
            true, 1.5f, -1.0f, ECk_Pmg_ConeOrientation::Forward);
        if (!::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        Slot = _StageBodies.Find(Row.Id);
        if (Slot == nullptr) { FailAndRetire(); return false; }
        Slot->Cone = ConeReturn;
        Body.Cone = ConeReturn;
        if (!HasStageContext(FrozenRoot, Row.Id, Body) || !IsLive(Created)) { break; }
        FCk_Handle Capsule = CapsuleReturn;
        FCk_Handle Cone = ConeReturn;
        if (!IsLive(Capsule) || !IsLive(Cone) ||
            !Capsule.Has<ck::FFragment_Pmg_DebugShape_Common>() ||
            !Cone.Has<ck::FFragment_Pmg_DebugShape_Common>())
        { break; }
        // New shapes are hidden before their NeedsSetup processors can construct mesh components.
        Capsule.Get<ck::FFragment_Pmg_DebugShape_Common>().Set_RenderMode(ECk_Pmg_RenderMode::Hidden);
        Cone.Get<ck::FFragment_Pmg_DebugShape_Common>().Set_RenderMode(ECk_Pmg_RenderMode::Hidden);
        if (!UCk_Utils_Transform_UE::Has(Capsule) ||
            !UCk_Utils_Transform_UE::Has(Cone))
        { break; }
        auto CapsuleTransform = UCk_Utils_Transform_UE::CastChecked(Capsule);
        auto ConeTransform = UCk_Utils_Transform_UE::CastChecked(Cone);
        if (!IsLive(static_cast<FCk_Handle>(CapsuleTransform)) ||
            !IsLive(static_cast<FCk_Handle>(ConeTransform)))
        { break; }
        const FTransform CapsuleOffset(FRotator::ZeroRotator,
            FVector(0.0f, 0.0f, HalfHeight), FVector::OneVector);
        const FTransform ConeOffset(FRotator::ZeroRotator,
            FVector(Row.RadiusCm * 1.4f, 0.0f, HalfHeight), FVector::OneVector);
        auto ParentTransform = Body.Transform;
        const auto CapsuleNode = UCk_Utils_SceneNode_UE::Add(
            CapsuleTransform, ParentTransform, CapsuleOffset);
        if (!::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        if (!HasStageContext(FrozenRoot, Row.Id, Body) ||
            !IsLive(FCk_Handle{CapsuleNode}) || !IsLive(Capsule) ||
            !UCk_Utils_SceneNode_UE::Has(Capsule) ||
            UCk_Utils_SceneNode_UE::Get_Parent(CapsuleNode) != Body.Transform)
        { break; }
        const auto ConeNode = UCk_Utils_SceneNode_UE::Add(
            ConeTransform, ParentTransform, ConeOffset);
        if (!::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        if (!HasStageContext(FrozenRoot, Row.Id, Body) ||
            !IsLive(FCk_Handle{ConeNode}) || !IsLive(Cone) ||
            !UCk_Utils_SceneNode_UE::Has(Cone) ||
            UCk_Utils_SceneNode_UE::Get_Parent(ConeNode) != Body.Transform)
        { break; }
    }
    if (_Failed || _RetireRequested || !IsClientWorldReady() ||
        _StageRoot != FrozenRoot || _StageBodies.Num() != FrozenRows.Num() ||
        !IsComplete(FrozenRoot, _StageBodies))
    {
        FailAndRetire();
        return false;
    }
    return true;
}

auto
    UCkCrowdRemoteBodyProjector::
    TryExpectOwner(const FCk_Handle& InOwner)
    -> bool
{
    if (!IsLive(InOwner) || _Outstanding == MAX_int32)
    { FailAndRetire(); return false; }
    int32& Count = _PendingOwners.FindOrAdd(InOwner);
    if (Count == MAX_int32) { FailAndRetire(); return false; }
    ++Count;
    ++_Outstanding;
    return true;
}

auto
    UCkCrowdRemoteBodyProjector::
    QueueTransform(FBody& InBody, const FTransform& InTransform)
    -> bool
{
    const FBody Frozen = InBody;
    const bool bStage = _Staging;
    FCk_Delegate_Request_OnCompleted Completion;
    Completion.BindDynamic(this, &UCkCrowdRemoteBodyProjector::OnVisualRequestCompleted);
    auto Transform = Frozen.Transform;
    // Request_SetTransform synchronously fires an existing scale request's delegate
    // before it replaces that request, while holding a reference to the fragment.
    // A foreign delegate could retire this exact body on that stack.
    if (Transform.Has<ck::FFragment_Transform_Requests>() &&
        Transform.Get<ck::FFragment_Transform_Requests>()
            .Get_ScaleRequests().IsSet())
    { FailAndRetire(); return false; }
    if (!TryExpectOwner(FCk_Handle{Frozen.Transform})) { return false; }
    UCk_Utils_Transform_UE::Request_SetTransform(
        Transform, FCk_Request_Transform_SetTransform{InTransform}, Completion);
    return HasQueueBody(Frozen, bStage);
}

auto
    UCkCrowdRemoteBodyProjector::
    QueueColor(FBody& InBody, const FLinearColor& InColor)
    -> bool
{
    const FBody Frozen = InBody;
    const bool bStage = _Staging;
    FCk_Delegate_Request_OnCompleted Completion;
    Completion.BindDynamic(this, &UCkCrowdRemoteBodyProjector::OnVisualRequestCompleted);
    if (!TryExpectOwner(FCk_Handle{Frozen.Capsule})) { return false; }
    auto Capsule = Frozen.Capsule;
    UCk_Utils_Pmg_DebugShape_UE::Request_SetColor(
        Capsule, FCk_Request_Pmg_DebugShape_SetColor{InColor}, Completion);
    if (!HasQueueBody(Frozen, bStage) ||
        !TryExpectOwner(FCk_Handle{Frozen.Cone})) { return false; }
    auto Cone = Frozen.Cone;
    UCk_Utils_Pmg_DebugShape_UE::Request_SetColor(
        Cone, FCk_Request_Pmg_DebugShape_SetColor{InColor}, Completion);
    return HasQueueBody(Frozen, bStage);
}

auto
    UCkCrowdRemoteBodyProjector::
    QueueVisibility(FBody& InBody, bool InVisible)
    -> bool
{
    const FBody Frozen = InBody;
    const bool bStage = _Staging;
    FCk_Delegate_Request_OnCompleted Completion;
    Completion.BindDynamic(this, &UCkCrowdRemoteBodyProjector::OnVisualRequestCompleted);
    if (!TryExpectOwner(FCk_Handle{Frozen.Capsule})) { return false; }
    auto Capsule = Frozen.Capsule;
    UCk_Utils_Pmg_DebugShape_UE::Request_SetVisible(Capsule, InVisible, Completion);
    if (!HasQueueBody(Frozen, bStage) ||
        !TryExpectOwner(FCk_Handle{Frozen.Cone})) { return false; }
    auto Cone = Frozen.Cone;
    UCk_Utils_Pmg_DebugShape_UE::Request_SetVisible(Cone, InVisible, Completion);
    return HasQueueBody(Frozen, bStage);
}

auto
    UCkCrowdRemoteBodyProjector::
    TryQueueUpdates(const TArray<FCkCrowdRemoteAgentRow>& InRows)
    -> bool
{
    _Submitting = true;
    const bool Visible = UCk_Utils_Crowd_DebugSettings_UE::Get_DrawAgentBody();
    _CandidateVisible = Visible;
    for (const FCkCrowdRemoteAgentRow& Row : InRows)
    {
        FBody* Body = _Bodies.Find(Row.Id);
        if (Body == nullptr) { FailAndRetire(); break; }
        if (!Body->Row.WorldTransform.Equals(Row.WorldTransform, 0.0f) &&
            !QueueTransform(*Body, Row.WorldTransform)) { break; }
        if (ResolveTint(Body->Row) != ResolveTint(Row) &&
            !QueueColor(*Body, ResolveTint(Row))) { break; }
        if (Visible != _AppliedVisible && !QueueVisibility(*Body, Visible)) { break; }
    }
    if (!::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    _Submitting = false;
    if (_Failed || _RetireRequested) { return false; }
    FinishIfComplete();
    return true;
}

auto
    UCkCrowdRemoteBodyProjector::
    QueueReveal()
    -> bool
{
    _Revealing = true;
    _Submitting = true;
    const bool Visible = UCk_Utils_Crowd_DebugSettings_UE::Get_DrawAgentBody();
    _CandidateVisible = Visible;
    if (Visible)
    {
        for (auto& Pair : _StageBodies)
        { if (!QueueVisibility(Pair.Value, true)) { break; } }
    }
    if (!::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    _Submitting = false;
    if (_Failed || _RetireRequested) { return false; }
    FinishIfComplete();
    return true;
}

auto
    UCkCrowdRemoteBodyProjector::
    FinishIfComplete()
    -> void
{
    if (_Submitting || _Outstanding != 0 || _Failed || _RetireRequested) { return; }
    if (!_PendingOwners.IsEmpty()) { FailAndRetire(); return; }
    if (_Staging)
    {
        if (!_Revealing) { return; }
        if (!IsComplete(_StageRoot, _StageBodies) ||
            !RequestRetireRoot(_Root, _Bodies) || _RetireRequested)
        { FailAndRetire(); return; }
        _Root = _StageRoot;
        _StageRoot = {};
        _Bodies = MoveTemp(_StageBodies);
        _StageBodies.Reset();
        _Staging = false;
        _Revealing = false;
    }
    else if (!IsComplete(_Root, _Bodies))
    { FailAndRetire(); return; }

    for (const FCkCrowdRemoteAgentRow& Row : _CandidateRows)
    { if (FBody* Body = _Bodies.Find(Row.Id)) { Body->Row = Row; } }
    _Generation = _CandidateGeneration;
    _Sequence = _CandidateSequence;
    _CandidateRows.Reset();
    _AppliedVisible = _CandidateVisible;
    _Ready = true;
}

auto
    UCkCrowdRemoteBodyProjector::
    TryApply(int64 InGeneration, int64 InSequence,
        const TArray<FCkCrowdRemoteAgentRow>& InRows)
    -> bool
{
    if (!IsClientWorldReady()) { FailAndRetire(); return false; }
    if (_RetireRequested || _Failed || _Staging || _Outstanding != 0 ||
        !_PendingOwners.IsEmpty() ||
        _Retiring.FindByPredicate([](const FCk_Handle& Handle) { return ck::IsValid(Handle, ck::IsValid_Policy_IncludePendingKill{}); }) != nullptr ||
        InGeneration <= 0 || InSequence <= 0 ||
        InGeneration < _Generation ||
        (InGeneration == _Generation && InSequence <= _Sequence))
    { return false; }

    TSet<FCkCrowdRemoteAgentId> Seen;
    for (const FCkCrowdRemoteAgentRow& Row : InRows)
    {
        if (!FCkCrowdRemoteDebugValidation::IsValid(Row) ||
            Row.Id.WorldGeneration != InGeneration ||
            Row.SampleSequence != InSequence || Seen.Contains(Row.Id) ||
            Row.RadiusCm <= 0.0f || Row.HeightCm <= 0.0f)
        { return false; }
        Seen.Add(Row.Id);
    }

    if (IsLive(_Root) && !IsComplete(_Root, _Bodies))
    { FailAndRetire(); return false; }

    bool Rebuild = !IsLive(_Root) || InGeneration != _Generation || _Bodies.Num() != InRows.Num();
    if (!Rebuild)
    {
        for (const FCkCrowdRemoteAgentRow& Row : InRows)
        {
            const FBody* Body = _Bodies.Find(Row.Id);
            if (Body == nullptr || Body->Row.RadiusCm != Row.RadiusCm ||
                Body->Row.HeightCm != Row.HeightCm)
            { Rebuild = true; break; }
        }
    }

    _CandidateGeneration = InGeneration;
    _CandidateSequence = InSequence;
    _CandidateRows = InRows;
    _Ready = false;
    if (Rebuild)
    {
        if (!TryStage(InRows))
        {
            _CandidateRows.Reset();
            _Ready = !_Failed && IsLive(_Root) && IsComplete(_Root, _Bodies);
            return false;
        }
        return true;
    }
    return TryQueueUpdates(InRows);
}

auto
    UCkCrowdRemoteBodyProjector::
    TickProjection()
    -> void
{
    if (_RetireRequested)
    {
        if (!_Failed)
        {
            for (const FRetiringTree& Tree : _RetiringTrees)
            { if (!HasNoNewAllocatedChildren(Tree)) { _Failed = true; break; } }
        }
        return;
    }
    if (_Failed) { return; }
    if (!IsClientWorldReady()) { FailAndRetire(); return; }
    _Retiring.RemoveAll([](const FCk_Handle& Handle) { return !ck::IsValid(Handle, ck::IsValid_Policy_IncludePendingKill{}); });
    if (_Staging && !_Revealing)
    {
        if (!IsComplete(_StageRoot, _StageBodies)) { FailAndRetire(); return; }
        for (auto& Pair : _StageBodies)
        {
            const FCk_Handle Capsule = Pair.Value.Capsule;
            const FCk_Handle Cone = Pair.Value.Cone;
            if (Capsule.Has<ck::FTag_Pmg_DebugShape_NeedsSetup>() ||
                Cone.Has<ck::FTag_Pmg_DebugShape_NeedsSetup>())
            { return; }
            auto* CapsuleMesh = Capsule.Get<ck::FFragment_Pmg_DebugShape>()
                .Get_MeshComponent().Get();
            auto* ConeMesh = Cone.Get<ck::FFragment_Pmg_DebugShape>()
                .Get_MeshComponent().Get();
            UWorld* World = _World.Get();
            if (!::IsValid(CapsuleMesh) || !::IsValid(ConeMesh) ||
                !::IsValid(World) || CapsuleMesh->GetWorld() != World ||
                ConeMesh->GetWorld() != World || CapsuleMesh->GetOuter() != World ||
                ConeMesh->GetOuter() != World)
            { FailAndRetire(); return; }
            Pair.Value.CapsuleMesh = CapsuleMesh;
            Pair.Value.ConeMesh = ConeMesh;
            Pair.Value.bMeshesCaptured = true;
        }
        QueueReveal();
        return;
    }
    if (_Ready && _Outstanding == 0)
    {
        if (!IsComplete(_Root, _Bodies)) { FailAndRetire(); return; }
        const bool Visible = UCk_Utils_Crowd_DebugSettings_UE::Get_DrawAgentBody();
        if (Visible != _AppliedVisible)
        {
            _Ready = false;
            _CandidateGeneration = _Generation;
            _CandidateSequence = _Sequence;
            _CandidateVisible = Visible;
            _CandidateRows.Reset();
            for (const auto& Pair : _Bodies) { _CandidateRows.Add(Pair.Value.Row); }
            _Submitting = true;
            for (auto& Pair : _Bodies)
            { if (!QueueVisibility(Pair.Value, Visible)) { break; } }
            if (!::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
            { return; }
            _Submitting = false;
            if (_Failed || _RetireRequested) { return; }
            FinishIfComplete();
        }
    }
}

auto
    UCkCrowdRemoteBodyProjector::
    OnVisualRequestCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    -> void
{
    if (_RetireRequested || _Failed) { return; }
    int32* Count = _PendingOwners.Find(InRequestOwner);
    if (!IsLive(InRequestOwner) || _Outstanding <= 0 || Count == nullptr || *Count <= 0 ||
        InResult != ECk_Request_OperationResult::Succeeded)
    { FailAndRetire(); return; }
    --*Count;
    --_Outstanding;
    if (*Count == 0) { _PendingOwners.Remove(InRequestOwner); }
    FinishIfComplete();
}

auto
    UCkCrowdRemoteBodyProjector::
    HideCaptured(const FCk_Handle& InRoot,
        const TMap<FCkCrowdRemoteAgentId, FBody>& InBodies)
    -> void
{
    UWorld* World = _World.Get();
    const auto HasRootContext = [this, World, InRoot]() -> bool
    {
        if (!IsInGameThread() || !::IsValid(this) ||
            HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            _World.Get() != World || !IsClientWorldReady() || !IsLive(InRoot) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(InRoot))
        { return false; }
        const FCk_Handle Transient =
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        return IsLive(Transient) &&
            InRoot.Get_RegistryView().Get_RegistryHandle() ==
                Transient.Get_RegistryView().Get_RegistryHandle() &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(InRoot) == Transient;
    };
    if (!HasRootContext()) { return; }
    TArray<FBody> FrozenBodies;
    for (const auto& Pair : InBodies) { FrozenBodies.Add(Pair.Value); }
    for (const FBody& Body : FrozenBodies)
    {
        if (!HasRootContext()) { return; }
        if (!IsLive(Body.Entity) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Body.Entity) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Body.Entity) != InRoot)
        { continue; }
        for (const FCk_Handle Shape : {FCk_Handle{Body.Capsule}, FCk_Handle{Body.Cone}})
        {
            if (!HasRootContext()) { return; }
            if (!IsLive(Body.Entity) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Body.Entity) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Body.Entity) != InRoot ||
                !IsLive(Shape) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Shape) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Shape) != Body.Entity ||
                !Shape.Has<ck::FFragment_Pmg_DebugShape_Common>())
            { continue; }
            if (UCk_Utils_Pmg_DebugShape_UE::Has(Shape))
            {
                const auto* Mesh = Shape.Get<ck::FFragment_Pmg_DebugShape>()
                    .Get_MeshComponent().Get();
                const auto& CapturedMesh = Shape == FCk_Handle{Body.Capsule} ?
                    Body.CapsuleMesh : Body.ConeMesh;
                if (!Body.bMeshesCaptured || !CapturedMesh.IsValid() ||
                    Mesh != CapturedMesh.Get() || !::IsValid(Mesh) ||
                    Mesh->GetWorld() != World || Mesh->GetOuter() != World)
                { continue; }
            }
            auto MutableShape = Shape;
            MutableShape.Get<ck::FFragment_Pmg_DebugShape_Common>().Set_RenderMode(
                ECk_Pmg_RenderMode::Hidden);
            if (HasRootContext() && IsLive(Body.Entity) &&
                UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Body.Entity) &&
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Body.Entity) == InRoot &&
                IsLive(Shape) &&
                UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Shape) &&
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Shape) == Body.Entity &&
                UCk_Utils_Pmg_DebugShape_UE::Has(Shape))
            {
                auto Typed = ck::StaticCast<FCk_Handle_Pmg_DebugShape>(Shape);
                UCk_Utils_Pmg_DebugShape_UE::Request_SetVisible(Typed, false, {});
            }
        }
    }
}

auto
    UCkCrowdRemoteBodyProjector::
    RequestRetireRoot(FCk_Handle& InRoot,
        const TMap<FCkCrowdRemoteAgentId, FBody>& InBodies)
    -> bool
{
    if (!IsInGameThread() || !::IsValid(this) ||
        HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    if (InRoot == FCk_Handle{}) { return InBodies.IsEmpty(); }
    if (!ck::IsValid(InRoot, ck::IsValid_Policy_IncludePendingKill{}))
    { return false; }
    if (!IsClientWorldReady() || !IsComplete(InRoot, InBodies)) { return false; }
    UWorld* World = _World.Get();
    const FCk_Handle Transient =
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!IsLive(Transient) ||
        Transient.Get_RegistryView().Get_RegistryHandle() !=
            InRoot.Get_RegistryView().Get_RegistryHandle() ||
        !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(InRoot) ||
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(InRoot) != Transient)
    { return false; }
    TArray<FCk_Handle> Frozen;
    Frozen.Add(InRoot);
    for (const auto& Pair : InBodies)
    {
        Frozen.Add(Pair.Value.Entity);
        Frozen.Add(Pair.Value.Capsule);
        Frozen.Add(Pair.Value.Cone);
    }
    _Retiring.Append(Frozen);
    const int32 RetiringIndex = _RetiringTrees.AddDefaulted();
    FRetiringTree& Retiring = _RetiringTrees[RetiringIndex];
    Retiring.Root = InRoot;
    Retiring.Bodies = InBodies;
    const bool WasRetireRequested = _RetireRequested;
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(InRoot);
    if (!::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    if (_Failed || _RetireRequested != WasRetireRequested ||
        InRoot != Frozen[0] || !::IsValid(World) ||
        _World.Get() != World || !IsClientWorldReady() ||
        !HasNoNewAllocatedChildren(_RetiringTrees[RetiringIndex]))
    { return false; }
    InRoot = {};
    return true;
}

auto
    UCkCrowdRemoteBodyProjector::
    FailAndRetire()
    -> void
{
    _Failed = true;
    _RetireRequested = true;
    _Ready = false;
    HideCaptured(_StageRoot, _StageBodies);
    HideCaptured(_Root, _Bodies);
}

auto
    UCkCrowdRemoteBodyProjector::
    RequestRetire()
    -> void
{
    if (_RetireRequested || _Failed) { return; }
    _RetireRequested = true;
    _Ready = false;
    const bool StageSafe = !IsLive(_StageRoot) || IsComplete(_StageRoot, _StageBodies);
    const bool RootSafe = !IsLive(_Root) || IsComplete(_Root, _Bodies);
    if (!StageSafe || !RootSafe) { FailAndRetire(); return; }
    if (!RequestRetireRoot(_StageRoot, _StageBodies) ||
        !RequestRetireRoot(_Root, _Bodies))
    { FailAndRetire(); return; }
    _StageBodies.Reset();
    _Bodies.Reset();
    _CandidateRows.Reset();
    _PendingOwners.Reset();
    _Outstanding = 0;
}

auto
    UCkCrowdRemoteBodyProjector::
    IsReadyFor(int64 InGeneration, int64 InSequence) const
    -> bool
{
    return _Ready && !_Failed && !_RetireRequested && !_Staging && _Outstanding == 0 &&
        _PendingOwners.IsEmpty() &&
        _Generation == InGeneration && _Sequence == InSequence && IsClientWorldReady() &&
        IsComplete(_Root, _Bodies) &&
        _Retiring.FindByPredicate([](const FCk_Handle& Handle) { return ck::IsValid(Handle, ck::IsValid_Policy_IncludePendingKill{}); }) == nullptr;
}

auto
    UCkCrowdRemoteBodyProjector::
    IsRetired()
    -> bool
{
    if (!_RetireRequested || _Failed ||
        ck::IsValid(_Root, ck::IsValid_Policy_IncludePendingKill{}) ||
        ck::IsValid(_StageRoot, ck::IsValid_Policy_IncludePendingKill{})) { return false; }
    for (const FRetiringTree& Tree : _RetiringTrees)
    {
        if (!HasNoNewAllocatedChildren(Tree))
        { _Failed = true; return false; }
    }
    for (const FCk_Handle Handle : _Retiring)
    { if (ck::IsValid(Handle, ck::IsValid_Policy_IncludePendingKill{})) { return false; } }
    return true;
}
