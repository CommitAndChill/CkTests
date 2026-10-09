#include "CkCameraGym_LocalAdmission.h"

#include "CkCamera/Camera/CkCamera_Component.h"
#include "CkCamera/Camera/CkCamera_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"

#include <Engine/LocalPlayer.h>
#include <Engine/World.h>
#include <Engine/NetDriver.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <Iris/ReplicationSystem/ObjectReplicationBridge.h>
#include <Iris/ReplicationSystem/ReplicationSystem.h>

namespace
{
    bool HasNoDestructionPhase(const FCk_Handle& Entity)
    {
        return ck::IsValid(Entity) &&
            NOT Entity.Has_Any<ck::FTag_DestroyEntity_Initiate,
                ck::FTag_DestroyEntity_EndPlay, ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>();
    }

    bool HasExactLocalNetInfo(const FCk_Handle& Entity,
        const FCk_Handle& NetOwner)
    {
        if (NOT ck::IsValid(Entity) || NOT ck::IsValid(NetOwner) ||
            NOT Entity.Has<ck::FFragment_Net_Params>() ||
            NOT NetOwner.Has<ck::FFragment_Net_Params>()) { return false; }
        const auto& Actual = Entity.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings();
        const auto& Expected = NetOwner.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings();
        return Actual.Get_Replication() == ECk_Replication::DoesNotReplicate &&
            Actual.Get_NetMode() == Expected.Get_NetMode() &&
            Actual.Get_NetRole() == ECk_Net_EntityNetRole::Authority &&
            Entity.Has<ck::FTag_HasAuthority>() &&
            Entity.Has<ck::FTag_NetMode_IsHost>() ==
                NetOwner.Has<ck::FTag_NetMode_IsHost>() &&
            Entity.Has<ck::FTag_NetMode_IsClient>() ==
                NetOwner.Has<ck::FTag_NetMode_IsClient>();
    }

    bool HasExactPawnChain(const FCk_Handle& PawnEntity, const FCk_Handle& WorldRoot,
        const UWorld* World)
    {
        if (NOT HasNoDestructionPhase(PawnEntity) ||
            NOT HasNoDestructionPhase(WorldRoot)) { return false; }
        const auto Registry = WorldRoot.Get_RegistryView().Get_RegistryHandle();
        if (PawnEntity.Get_RegistryView().Get_RegistryHandle() != Registry) { return false; }
        TArray<FCk_Handle> Seen;
        auto Current = PawnEntity;
        while (true)
        {
            if (NOT HasNoDestructionPhase(Current) ||
                Current.Get_RegistryView().Get_RegistryHandle() != Registry ||
                Seen.Contains(Current)) { return false; }
            Seen.Add(Current);
            if (Current.Has<TWeakObjectPtr<UWorld>>() &&
                Current.Get<TWeakObjectPtr<UWorld>>().Get() != World) { return false; }
            if (Current == WorldRoot) { return true; }
            if (NOT Current.Has<ck::FFragment_LifetimeOwner>()) { return false; }
            auto Next = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Current);
            if (Next == Current) { return false; }
            Current = Next;
        }
    }

    bool HasCurrentInputs(APawn* Pawn, APlayerController* PC, ULocalPlayer* LP,
        UWorld* World, const FCk_Handle& PawnEntity, AActor* Beacon,
        UCk_CameraComponent* Output, bool RequireEmptyOutput,
        FCk_Handle& OutWorldRoot, FVector& OutBeaconLocation)
    {
        OutWorldRoot = {};
        OutBeaconLocation = FVector::ZeroVector;
        if (NOT IsInGameThread() || NOT ck::IsValid(World) ||
            World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            NOT World->IsGameWorld() || World->bIsTearingDown ||
            World->IsBeingCleanedUp()) { return false; }
        auto* const Subsystem = World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
        if (NOT ck::IsValid(Subsystem) ||
            Subsystem->Get_LoadHold() == ECk_EcsWorld_LoadHold::Teardown)
        { return false; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (NOT HasNoDestructionPhase(Root) || NOT Root.Has<TWeakObjectPtr<UWorld>>() ||
            Root.Get<TWeakObjectPtr<UWorld>>().Get() != World)
        { return false; }
        if (NOT Root.Has<ck::FFragment_Net_Params>()) { return false; }
        const auto& RootNet = Root.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings();
        const auto ExpectedMode = World->IsNetMode(NM_Client)
            ? ECk_Net_NetModeType::Client
            : ECk_Net_NetModeType::ClientAndHost;
        if ((NOT World->IsNetMode(NM_Client) &&
                NOT World->IsNetMode(NM_ListenServer) &&
                NOT World->IsNetMode(NM_Standalone)) ||
            RootNet.Get_Replication() != ECk_Replication::DoesNotReplicate ||
            RootNet.Get_NetMode() != ExpectedMode ||
            RootNet.Get_NetRole() != ECk_Net_EntityNetRole::Authority ||
            NOT Root.Has<ck::FTag_HasAuthority>() ||
            NOT Root.Has<ck::FTag_NetMode_IsClient>() ||
            Root.Has<ck::FTag_NetMode_IsHost>() !=
                (ExpectedMode == ECk_Net_NetModeType::ClientAndHost))
        { return false; }
        if (NOT ck::IsValid(Pawn) || Pawn->IsActorBeingDestroyed() ||
            Pawn->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Pawn->GetWorld() != World) { return false; }
        if (NOT ck::IsValid(PC) || PC->IsActorBeingDestroyed() ||
            PC->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            PC->GetWorld() != World || NOT PC->IsLocalController() ||
            PC->GetPawn() != Pawn || Pawn->GetController() != PC)
        { return false; }
        if (NOT ck::IsValid(LP) ||
            LP->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            LP->GetWorld() != World ||
            PC->GetLocalPlayer() != LP || LP->GetPlayerController(World) != PC)
        { return false; }
        if (NOT HasExactPawnChain(PawnEntity, Root, World) ||
            NOT PawnEntity.Has<ck::FFragment_OwningActor>() ||
            UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(PawnEntity) != Pawn ||
            UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Pawn) != PawnEntity ||
            NOT PawnEntity.Has<ck::FFragment_Transform>())
        { return false; }
        const auto& PawnPose = PawnEntity.Get<ck::FFragment_Transform>().Get_Transform();
        if (PawnPose.ContainsNaN() || NOT PawnPose.IsValid() ||
            NOT PawnPose.GetRotation().IsNormalized() ||
            NOT UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(PawnEntity))
        { return false; }
        auto* const GS = World->GetGameState();
        if (NOT ck::IsValid(GS) || GS->IsActorBeingDestroyed() ||
            GS->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            GS->GetWorld() != World) { return false; }
        if (NOT ck::IsValid(Beacon) || Beacon->IsActorBeingDestroyed() ||
            Beacon->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Beacon->GetWorld() != World || Beacon->GetOwner() != GS)
        { return false; }
        const auto BeaconPose = Beacon->GetActorTransform();
        if (BeaconPose.ContainsNaN() || NOT BeaconPose.IsValid() ||
            NOT BeaconPose.GetRotation().IsNormalized()) { return false; }
        if (NOT ck::IsValid(Output) ||
            Output->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Output->GetOwner() != Pawn || Output->GetWorld() != World ||
            (RequireEmptyOutput && Output->Get_DirectorEntity() != FCk_Handle_Camera{}))
        { return false; }
        OutWorldRoot = Root;
        OutBeaconLocation = BeaconPose.GetLocation();
        return true;
    }

    bool IsExactLiveChild(const FCk_Handle& Child, const FCk_Handle& Parent)
    {
        return HasNoDestructionPhase(Child) && HasNoDestructionPhase(Parent) &&
            Child.Get_RegistryView().Get_RegistryHandle() ==
                Parent.Get_RegistryView().Get_RegistryHandle() &&
            Child.Has<ck::FFragment_LifetimeOwner>() &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Parent &&
            NOT Child.Has<ck::FFragment_OwningActor>();
    }

    bool HasExactScopeAnchor(const FCk_Handle& Scope,
        const FCk_Handle& PawnEntity, const FCk_Handle& WorldRoot)
    {
        if (NOT IsExactLiveChild(Scope, PawnEntity) ||
            NOT Scope.Has_All<ck::FFragment_SceneNode,
                ck::SceneNodeParent, ck::FFragment_Transform>() ||
            Scope.Has<ck::FFragment_SceneNode_UnrealAnchor>() ||
            NOT HasExactLocalNetInfo(Scope, WorldRoot)) { return false; }
        const auto Node = UCk_Utils_SceneNode_UE::Cast(Scope);
        if (FCk_Handle{UCk_Utils_SceneNode_UE::Get_Parent(Node)} != PawnEntity ||
            NOT UCk_Utils_SceneNode_UE::Get_Offset(Node).Equals(
                FTransform::Identity, 0.0)) { return false; }
        const auto& ScopePose =
            Scope.Get<ck::FFragment_Transform>().Get_Transform();
        if (ScopePose.ContainsNaN() || NOT ScopePose.IsValid() ||
            NOT ScopePose.GetRotation().IsNormalized()) { return false; }
        // TryAdd_Anchor connected this Scope as one direct unlabeled entry.
        // With extensions explicitly absent, Get_Entries reads only this record.
        if (PawnEntity.Has<ck::FFragment_RecordOfEntityExtensions>() ||
            NOT PawnEntity.Has<ck::FFragment_RecordOfSceneNodes>())
        { return false; }
        int32 Matches = 0;
        for (const auto& Entry :
            ck::FUtils_RecordOfSceneNodes::Get_Entries(PawnEntity))
        { if (Entry == Scope) { ++Matches; } }
        return Matches == 1;
    }

    bool HasExactTarget(const FCk_Handle_Transform& Target,
        const FCk_Handle& Scope, const FVector& BeaconLocation)
    {
        if (NOT IsExactLiveChild(FCk_Handle{Target}, Scope) ||
            NOT Target.Has<ck::FFragment_Transform>() ||
            NOT HasExactLocalNetInfo(Target, Scope)) { return false; }
        const auto& Pose = Target.Get<ck::FFragment_Transform>().Get_Transform();
        return NOT Pose.ContainsNaN() && Pose.IsValid() &&
            Pose.GetRotation().IsNormalized() &&
            Pose.Equals(FTransform{FQuat::Identity, BeaconLocation,
                FVector::OneVector}, 0.0);
    }
}

bool UCk_CameraGym_LocalAdmission_UE::TryCreate_LocalCamera(APawn* Pawn,
    APlayerController* PC, ULocalPlayer* LP, UWorld* World,
    const FCk_Handle& PawnEntity, AActor* Beacon,
    UCk_CameraComponent* OutputComponent,
    FCkCameraGym_LocalAdmission& OutAdmission)
{
    // Never discard a prior live cleanup obligation supplied in the output slot.
    if (ck::IsValid(OutAdmission.Scope)) { return false; }
    OutAdmission = {};
    FCk_Handle WorldRoot;
    FVector BeaconLocation;
    if (NOT HasCurrentInputs(Pawn, PC, LP, World, PawnEntity, Beacon,
            OutputComponent, true, WorldRoot, BeaconLocation)) { return false; }

    auto Working = FCkCameraGym_LocalAdmission{};
    Working.WorldRoot = WorldRoot;
    Working.PawnEntity = PawnEntity;
    Working.World = World;
    Working.Pawn = Pawn;
    Working.PC = PC;
    Working.LP = LP;
    Working.GS = World->GetGameState();
    Working.Beacon = Beacon;
    Working.Output = OutputComponent;
    const auto LocalSettings = WorldRoot.Get<ck::FFragment_Net_Params>()
        .Get_ConnectionSettings();
    Working.Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(PawnEntity);
    const auto FailOwned = [&]() -> bool
    {
        TryClose_LocalCamera(Working);
        return false;
    };
    if (NOT IsExactLiveChild(Working.Scope, PawnEntity))
    { return FailOwned(); }
    FCk_Handle CheckRoot;
    FVector CheckBeacon;
    if (NOT HasCurrentInputs(Pawn, PC, LP, World, PawnEntity, Beacon,
            OutputComponent, true, CheckRoot, CheckBeacon) ||
        CheckRoot != WorldRoot || NOT CheckBeacon.Equals(BeaconLocation, 0.0))
    { return FailOwned(); }
    // Request_CreateEntity copies the replicated Pawn's Net Params. Replace
    // only that copied fragment on this fresh private Scope with the exact
    // world's local DNR settings. Net::Add supplies the local Authority tag
    // on a client, so its DNR layer children inherit a coherent local role.
    if (Working.Scope.Has<ck::FFragment_Net_Params>())
    { Working.Scope.Remove<ck::FFragment_Net_Params>(); }
    UCk_Utils_Net_UE::Add(Working.Scope, LocalSettings);
    if (NOT IsExactLiveChild(Working.Scope, PawnEntity) ||
        NOT HasExactLocalNetInfo(Working.Scope, WorldRoot))
    { return FailOwned(); }
    auto ParentTransform = UCk_Utils_Transform_UE::Cast(PawnEntity);
    auto Node = FCk_Handle_SceneNode{};
    if (NOT UCk_Utils_SceneNode_UE::TryAdd_Anchor(
            Working.Scope, ParentTransform, FTransform::Identity, Node) ||
        NOT IsExactLiveChild(Working.Scope, PawnEntity) ||
        NOT ck::IsValid(Node) || FCk_Handle{Node} != Working.Scope)
    { return FailOwned(); }
    if (NOT HasCurrentInputs(Pawn, PC, LP, World, PawnEntity, Beacon,
            OutputComponent, true, CheckRoot, CheckBeacon) ||
        CheckRoot != WorldRoot || NOT CheckBeacon.Equals(BeaconLocation, 0.0) ||
        NOT HasExactScopeAnchor(Working.Scope, PawnEntity, WorldRoot))
    { return FailOwned(); }

    Working.Target = UCk_Utils_Transform_UE::Create(Working.Scope,
        FTransform{FQuat::Identity, BeaconLocation, FVector::OneVector},
        ECk_Replication::DoesNotReplicate);
    if (NOT HasExactTarget(Working.Target, Working.Scope, BeaconLocation))
    { return FailOwned(); }
    if (NOT HasCurrentInputs(Pawn, PC, LP, World, PawnEntity, Beacon,
            OutputComponent, true, CheckRoot, CheckBeacon) ||
        CheckRoot != WorldRoot || NOT CheckBeacon.Equals(BeaconLocation, 0.0) ||
        NOT HasExactScopeAnchor(Working.Scope, PawnEntity, WorldRoot) ||
        NOT HasExactTarget(Working.Target, Working.Scope, BeaconLocation))
    { return FailOwned(); }

    auto ScopeTransform = UCk_Utils_Transform_UE::Cast(Working.Scope);
    Working.Director = UCk_Utils_Camera_UE::Add(
        ScopeTransform, FCk_Camera_Spec{OutputComponent});
    if (NOT ck::IsValid(Working.Director) ||
        FCk_Handle{Working.Director} != Working.Scope ||
        NOT Get_IsExactLocalAdmission(Working))
    { return FailOwned(); }
    OutAdmission = Working;
    return true;
}

bool UCk_CameraGym_LocalAdmission_UE::Get_IsExactLocalAdmission(
    const FCkCameraGym_LocalAdmission& Admission)
{
    if (Admission.CloseRequested) { return false; }
    auto* const World = Admission.World.Get();
    auto* const Pawn = Admission.Pawn.Get();
    auto* const PC = Admission.PC.Get();
    auto* const LP = Admission.LP.Get();
    auto* const Beacon = Admission.Beacon.Get();
    auto* const Output = Admission.Output.Get();
    FCk_Handle Root;
    FVector Location;
    if (NOT HasCurrentInputs(Pawn, PC, LP, World, Admission.PawnEntity,
            Beacon, Output, false, Root, Location) ||
        Root != Admission.WorldRoot ||
        World->GetGameState() != Admission.GS.Get() ||
        NOT HasExactScopeAnchor(Admission.Scope, Admission.PawnEntity,
            Admission.WorldRoot) ||
        NOT HasExactTarget(Admission.Target, Admission.Scope, Location) ||
        NOT ck::IsValid(Admission.Director) ||
        FCk_Handle{Admission.Director} != Admission.Scope ||
        NOT UCk_Utils_Camera_UE::Has(Admission.Director) ||
        Output->Get_DirectorEntity() != Admission.Director)
    { return false; }
    return true;
}

bool UCk_CameraGym_LocalAdmission_UE::TryClose_LocalCamera(
    FCkCameraGym_LocalAdmission& Admission)
{
    if (NOT IsInGameThread() || Admission.CloseRequested) { return false; }
    auto* const Output = Admission.Output.Get();
    if (ck::IsValid(Output) && Admission.Director != FCk_Handle_Camera{} &&
        Output->Get_DirectorEntity() == Admission.Director)
    { Output->Set_DirectorEntity({}); }
    if (NOT ck::IsValid(Admission.Scope))
    { return false; }
    // This is the only destructive permission. Do not infer ownership from
    // current possession, World, or actor lookup after the session is lost.
    if (NOT ck::IsValid(Admission.PawnEntity) ||
        Admission.Scope.Get_RegistryView().Get_RegistryHandle() !=
            Admission.PawnEntity.Get_RegistryView().Get_RegistryHandle() ||
        NOT Admission.Scope.Has<ck::FFragment_LifetimeOwner>() ||
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Admission.Scope,
            ECk_PendingKill_Policy::IncludePendingKill) != Admission.PawnEntity ||
        Admission.Scope.Has<ck::FFragment_OwningActor>() ||
        (ck::IsValid(Admission.Target) &&
            Admission.Target.Has<ck::FFragment_OwningActor>()))
    { return false; }
    Admission.CloseRequested = true;
    auto Scope = Admission.Scope;
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Scope);
    return true;
}

bool
UCk_CameraGym_LocalAdmission_UE::Get_HasExactLocalPlayerWorld(
    ULocalPlayer* LP, UWorld* World)
{
    return ck::IsValid(LP) && ck::IsValid(World) && LP->GetWorld() == World;
}

bool
UCk_CameraGym_LocalAdmission_UE::Get_IsReplicatingMovement(AActor* Actor)
{
    return ck::IsValid(Actor) && NOT Actor->IsActorBeingDestroyed() &&
        Actor->IsReplicatingMovement();
}

bool
UCk_CameraGym_LocalAdmission_UE::Get_HasReplicationDelivery(AActor* Actor)
{
    if (NOT IsInGameThread() || NOT ck::IsValid(Actor) ||
        Actor->IsActorBeingDestroyed() ||
        Actor->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject |
            RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    auto* const World = Actor->GetWorld();
    if (NOT ck::IsValid(World) ||
        World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        NOT World->IsGameWorld() || World->bIsTearingDown ||
        World->IsBeingCleanedUp())
    { return false; }
    if (World->IsNetMode(NM_Client))
    {
        if (NOT Actor->HasActorBegunPlay()) { return false; }
        auto* const Driver = World->GetNetDriver();
        if (NOT ck::IsValid(Driver) || Driver->GetWorld() != World)
        { return false; }
        auto* const System = Driver->GetReplicationSystem();
        if (NOT ck::IsValid(System)) { return false; }
        auto* const Bridge = System->GetReplicationBridge();
        if (NOT ck::IsValid(Bridge)) { return false; }
        const auto Handle = Bridge->GetReplicatedRefHandle(Actor);
        return Handle.IsValid() && Bridge->GetReplicatedObject(Handle) == Actor;
    }
    if (NOT World->IsNetMode(NM_DedicatedServer) &&
        NOT World->IsNetMode(NM_ListenServer) &&
        NOT World->IsNetMode(NM_Standalone))
    { return false; }
    return Actor->HasAuthority() && Actor->GetIsReplicated();
}
