#include "CkSceneNodeGym_ActorLedger.h"

#include "Components/SceneComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

#include <cfloat>

namespace CkSceneNodeActorLedgerPrivate
{
    constexpr int32 RoleCount = 18;

    // A removed/destroyed instance component cannot erase a source claim.
    // Weak serials are pruned only after the actual Source UObject is gone;
    // there is intentionally no generation reset or public release in this
    // construction-only slice.
    TArray<TWeakObjectPtr<AGameStateBase>> ClaimedSources;

    bool HasDelayedSpawnNotification()
    {
        const auto* const Delay = IConsoleManager::Get().FindConsoleVariable(
            TEXT("s.DelayOnActorSpawnedUntilFinishedSpawning"));
        return Delay != nullptr && Delay->GetInt() != 0;
    }

    bool HasSafeClassDefault(UClass* Type)
    {
        if (!IsValid(Type) || Type->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Type->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        { return false; }
        const AActor* const Default = Cast<AActor>(Type->GetDefaultObject(false));
        if (!IsValid(Default) ||
            Default->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Default->GetIsReplicated() ||
            Default->IsReplicatingMovement())
        { return false; }
        const USceneComponent* const Root = Default->GetRootComponent();
        if (Root == nullptr) { return true; }
        if (!IsValid(Root) || Root->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        // The three getters above are literal field reads, before any pose
        // construction or multiplication in Unreal's deferred-spawn path.
        const FVector Location = Root->GetRelativeLocation();
        const FRotator Rotation = Root->GetRelativeRotation();
        const FVector Scale = Root->GetRelativeScale3D();
        if (Location.ContainsNaN() || Rotation.ContainsNaN() || Scale.ContainsNaN())
        { return false; }
        const double Bound = FMath::Sqrt(FMath::Sqrt(DBL_MAX)) / 32.0;
        return FMath::Abs(Location.X) <= Bound && FMath::Abs(Location.Y) <= Bound &&
            FMath::Abs(Location.Z) <= Bound && FMath::Abs(Scale.X) <= Bound &&
            FMath::Abs(Scale.Y) <= Bound && FMath::Abs(Scale.Z) <= Bound &&
            FMath::Abs(Rotation.Pitch) <= Bound && FMath::Abs(Rotation.Yaw) <= Bound &&
            FMath::Abs(Rotation.Roll) <= Bound;
    }

    bool SameActor(const TWeakObjectPtr<AActor>& Captured, const AActor* Actor)
    {
        return Actor != nullptr &&
            Captured.HasSameIndexAndSerialNumber(TWeakObjectPtr<AActor>{const_cast<AActor*>(Actor)});
    }

    enum class EScopeRelation : uint8 { Unrelated, Descendant, Malformed };

    // This only observes a current chain. Reparenting that occurs and
    // disappears between calls is outside this point-in-time census.
    EScopeRelation GetScopeRelation(const AActor* Candidate, const AActor* Scope,
        bool bIncomingDestroyCallback = false)
    {
        if (Candidate == nullptr || Scope == nullptr || Candidate == Scope)
        { return EScopeRelation::Unrelated; }
        TSet<const AActor*> Seen;
        const AActor* Current = Candidate;
        while (Current != nullptr)
        {
            // UWorld broadcasts the incoming destroyed actor after marking it
            // destroying, but before clearing Owner. This exception is never
            // used for an owner link or a pure census read.
            const bool bIncoming = Current == Candidate && bIncomingDestroyCallback;
            if ((bIncoming
                    ? Current->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
                    : (!IsValid(Current) ||
                       Current->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
                       Current->IsActorBeingDestroyed())) ||
                Seen.Contains(Current))
            { return EScopeRelation::Malformed; }
            Seen.Add(Current);
            const AActor* const Parent = Current->GetOwner();
            if (Parent == Scope) { return EScopeRelation::Descendant; }
            if (Parent == nullptr) { return EScopeRelation::Unrelated; }
            Current = Parent;
        }
        return EScopeRelation::Unrelated;
    }

    bool IsInLoadedLevel(UWorld* World, AActor* Actor)
    {
        if (!IsValid(World) || !IsValid(Actor) ||
            World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Actor->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        ULevel* const Level = Actor->GetLevel();
        return IsValid(Level) &&
            !Level->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
            World->GetLevels().Contains(Level) &&
            Level->Actors.ContainsByPredicate([Actor](const TObjectPtr<AActor>& Entry)
            { return Entry.Get() == Actor; });
    }
}

UCk_SceneNodeGym_ActorLedger_UE::UCk_SceneNodeGym_ActorLedger_UE()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UCk_SceneNodeGym_ActorLedger_UE::Close(FName Reason)
{
    if (!bClosed && FirstClosureReason.IsNone()) { FirstClosureReason = Reason; }
    bClosed = true;
}

void UCk_SceneNodeGym_ActorLedger_UE::OnActorDestroyed(AActor* Actor)
{
    if (!IsInGameThread()) { return; }
    if (Actor == nullptr)
    { Close(TEXT("InvalidDestroyedCallback")); return; }
    int32 KnownRole = -1;
    if (UCk_SceneNodeGym_ActorLedgerRuntime_UE::IsKnownIdentity(this, Actor, KnownRole))
    {
        Roles[KnownRole].bDestroyedObserved = true;
        bExternalLoss = true; // this slice never initiates actor destruction
        Close(TEXT("KnownActorDestroyed"));
        return;
    }
    AActor* const Scope = Roles.Num() == CkSceneNodeActorLedgerPrivate::RoleCount ? Roles[0].Actor.Get() : nullptr;
    if (Scope != nullptr &&
        CkSceneNodeActorLedgerPrivate::GetScopeRelation(Actor, Scope, true) != CkSceneNodeActorLedgerPrivate::EScopeRelation::Unrelated)
    { bUnknownOwnedActor = true; Close(TEXT("UnknownOwnedActorDestroyed")); }
}

void UCk_SceneNodeGym_ActorLedger_UE::OnActorRemovedFromWorld(AActor* Actor)
{
    if (!IsInGameThread()) { return; }
    if (Actor == nullptr)
    { Close(TEXT("InvalidRemovedCallback")); return; }
    int32 KnownRole = -1;
    if (UCk_SceneNodeGym_ActorLedgerRuntime_UE::IsKnownIdentity(this, Actor, KnownRole))
    {
        Roles[KnownRole].bRemovedObserved = true;
        bExternalLoss = true;
        Close(TEXT("KnownActorRemoved"));
    }
    // The engine has already cleared Owner for an unknown removed actor here;
    // absence of a match is never treated as proof of historical absence.
}

void UCk_SceneNodeGym_ActorLedger_UE::OnActorSpawned(AActor* Actor)
{
    if (!IsInGameThread()) { return; }
    if (Actor == nullptr)
    { Close(TEXT("InvalidSpawnCallback")); return; }
    int32 KnownRole = -1;
    if (UCk_SceneNodeGym_ActorLedgerRuntime_UE::IsKnownIdentity(this, Actor, KnownRole))
    {
        auto& Slot = Roles[KnownRole];
        if (ActiveRole != KnownRole || !Slot.bFinishing || Slot.bFinishNotified)
        { Close(TEXT("UnexpectedKnownSpawnNotification")); return; }
        Slot.bFinishNotified = true;
        return;
    }
    AActor* const Scope = Roles.Num() == CkSceneNodeActorLedgerPrivate::RoleCount ? Roles[0].Actor.Get() : nullptr;
    if (Scope != nullptr && CkSceneNodeActorLedgerPrivate::GetScopeRelation(Actor, Scope) != CkSceneNodeActorLedgerPrivate::EScopeRelation::Unrelated)
    { bUnknownOwnedActor = true; Close(TEXT("UnknownOwnedActorSpawned")); }
}

void UCk_SceneNodeGym_ActorLedger_UE::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Close(TEXT("LedgerEndPlay"));
    bWorldTeardown = true;
    UnbindWorldCallbacks();
    Super::EndPlay(EndPlayReason);
}

void UCk_SceneNodeGym_ActorLedger_UE::UnbindWorldCallbacks()
{
    if (auto* const World = SourceWorld.Get())
    {
        if (DestroyedHandle.IsValid()) { World->RemoveOnActorDestroyedHandler(DestroyedHandle); }
        if (RemovedHandle.IsValid()) { World->RemoveOnActorRemovedFromWorldHandler(RemovedHandle); }
        if (SpawnedHandle.IsValid()) { World->RemoveOnActorSpawnedHandler(SpawnedHandle); }
    }
    DestroyedHandle.Reset();
    RemovedHandle.Reset();
    SpawnedHandle.Reset();
}

void UCk_SceneNodeGym_ActorLedger_UE::BeginDestroy()
{
    Close(TEXT("LedgerBeginDestroy"));
    UnbindWorldCallbacks();
    Super::BeginDestroy();
}

ACk_SceneNodeGym_SourceActor_UE::ACk_SceneNodeGym_SourceActor_UE()
{
    bReplicates = false;
    SetReplicateMovement(false);
}

UCk_SceneNodeGym_ActorLedger_UE* ACk_SceneNodeGym_SourceActor_UE::Get_ActorLedger() const
{
    if (!IsInGameThread()) { return nullptr; }
    auto* const Current = Ledger.Get();
    return IsValid(Current) &&
        !Current->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
        ? Current : nullptr;
}

bool UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasLiveSourceContext(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger)
{
    if (!IsInGameThread() || !IsValid(Ledger) ||
        Ledger->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        !Ledger->IsRegistered() || Ledger->Generation <= 0 ||
        Ledger->Roles.Num() != CkSceneNodeActorLedgerPrivate::RoleCount || Ledger->VisualClasses.Num() != CkSceneNodeActorLedgerPrivate::RoleCount - 1 ||
        Ledger->Source.IsStale() || Ledger->SourceWorld.IsStale())
    { return false; }
    AGameStateBase* const Source = Ledger->Source.Get();
    UWorld* const World = Ledger->SourceWorld.Get();
    if (!IsValid(Source) || !IsValid(World) ||
        Source->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Source->IsActorBeingDestroyed() || World->bIsTearingDown ||
        !World->IsGameWorld() || World->GetNetMode() == NM_Client ||
        !IsValid(World->GetAuthGameMode()) || !Source->HasAuthority() ||
        !Source->HasActorBegunPlay() || Source->GetWorld() != World ||
        World->GetGameState() != Source || Ledger->GetOwner() != Source ||
        !Source->GetInstanceComponents().Contains(
            const_cast<UCk_SceneNodeGym_ActorLedger_UE*>(Ledger)))
    { return false; }
    return true;
}

bool UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasCurrentSource(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger)
{
    return HasLiveSourceContext(Ledger) && !Ledger->bClosed &&
        CkSceneNodeActorLedgerPrivate::HasDelayedSpawnNotification();
}

bool UCk_SceneNodeGym_ActorLedgerRuntime_UE::IsSafePose(const FTransform& Pose)
{
    if (Pose.ContainsNaN() || !Pose.IsValid()) { return false; }
    const double Bound = FMath::Sqrt(FMath::Sqrt(DBL_MAX)) / 32.0;
    const auto P = Pose.GetTranslation();
    const auto S = Pose.GetScale3D();
    return Pose.GetRotation().IsNormalized() &&
        FMath::Abs(P.X) <= Bound && FMath::Abs(P.Y) <= Bound && FMath::Abs(P.Z) <= Bound &&
        FMath::Abs(S.X) <= Bound && FMath::Abs(S.Y) <= Bound && FMath::Abs(S.Z) <= Bound;
}

bool UCk_SceneNodeGym_ActorLedgerRuntime_UE::IsKnownIdentity(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger, const AActor* Actor, int32& OutRole)
{
    OutRole = -1;
    if (Ledger == nullptr || Actor == nullptr) { return false; }
    for (int32 Role = 0; Role < Ledger->Roles.Num(); ++Role)
    {
        if (Ledger->Roles[Role].bCaptured && CkSceneNodeActorLedgerPrivate::SameActor(Ledger->Roles[Role].Actor, Actor))
        { OutRole = Role; return true; }
    }
    return false;
}

UCk_SceneNodeGym_ActorLedger_UE*
UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
    AGameStateBase* Source, int32 Generation,
    const TArray<TSubclassOf<AActor>>& VisualClasses)
{
    if (!IsInGameThread() || !IsValid(Source) || Generation <= 0 ||
        VisualClasses.Num() != CkSceneNodeActorLedgerPrivate::RoleCount - 1 ||
        Source->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Source->IsActorBeingDestroyed() || !Source->HasAuthority() ||
        !Source->HasActorBegunPlay() || !CkSceneNodeActorLedgerPrivate::HasDelayedSpawnNotification())
    { return nullptr; }
    UWorld* const World = Source->GetWorld();
    if (!IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        World->bIsTearingDown || !World->IsGameWorld() ||
        World->GetNetMode() == NM_Client || !IsValid(World->GetAuthGameMode()) ||
        World->GetGameState() != Source)
    { return nullptr; }
    for (const auto& Class : VisualClasses)
    {
        UClass* const Type = Class.Get();
        if (!IsValid(Type) ||
            !Type->IsChildOf(ACk_SceneNodeGym_SourceActor_UE::StaticClass()) ||
            !CkSceneNodeActorLedgerPrivate::HasSafeClassDefault(Type))
        { return nullptr; }
    }
    if (!CkSceneNodeActorLedgerPrivate::HasSafeClassDefault(AActor::StaticClass())) { return nullptr; }
    for (UActorComponent* const Component : Source->GetInstanceComponents())
    {
        if (!IsValid(Component) ||
            Component->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Component->GetClass() == UCk_SceneNodeGym_ActorLedger_UE::StaticClass())
        { return nullptr; }
    }
    for (UActorComponent* const Component : Source->GetComponents())
    {
        if (!IsValid(Component) ||
            Component->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Component->GetClass() == UCk_SceneNodeGym_ActorLedger_UE::StaticClass())
        { return nullptr; }
    }

    CkSceneNodeActorLedgerPrivate::ClaimedSources.RemoveAll([](const TWeakObjectPtr<AGameStateBase>& Prior)
    { return Prior.IsStale(false); });
    const TWeakObjectPtr<AGameStateBase> Claim{Source};
    for (const auto& Prior : CkSceneNodeActorLedgerPrivate::ClaimedSources)
    {
        if (Prior.HasSameIndexAndSerialNumber(Claim)) { return nullptr; }
    }
    // Reserve before NewObject. Even an opaque allocation or registration
    // failure must not let a second ledger reinterpret this source generation.
    CkSceneNodeActorLedgerPrivate::ClaimedSources.Add(Claim);

    auto* const Ledger = NewObject<UCk_SceneNodeGym_ActorLedger_UE>(Source);
    if (!IsValid(Ledger)) { return nullptr; }
    Ledger->Source = Source;
    Ledger->SourceWorld = World;
    Ledger->Generation = Generation;
    Ledger->VisualClasses = VisualClasses;
    Ledger->Roles.SetNum(CkSceneNodeActorLedgerPrivate::RoleCount);
    Source->AddInstanceComponent(Ledger);
    Ledger->DestroyedHandle = World->AddOnActorDestroyedHandler(
        FOnActorDestroyed::FDelegate::CreateUObject(Ledger,
            &UCk_SceneNodeGym_ActorLedger_UE::OnActorDestroyed));
    Ledger->RemovedHandle = World->AddOnActorRemovedFromWorldHandler(
        FOnActorRemovedFromWorld::FDelegate::CreateUObject(Ledger,
            &UCk_SceneNodeGym_ActorLedger_UE::OnActorRemovedFromWorld));
    Ledger->SpawnedHandle = World->AddOnActorSpawnedHandler(
        FOnActorSpawned::FDelegate::CreateUObject(Ledger,
            &UCk_SceneNodeGym_ActorLedger_UE::OnActorSpawned));
    if (!Ledger->DestroyedHandle.IsValid() || !Ledger->RemovedHandle.IsValid() ||
        !Ledger->SpawnedHandle.IsValid())
    {
        Ledger->Close(TEXT("DelegateBindFailed"));
        Ledger->UnbindWorldCallbacks();
        // Retain the failed native claim. No second generation may obscure it.
        return nullptr;
    }
    Ledger->RegisterComponent();
    if (!Ledger->IsRegistered() || !HasCurrentSource(Ledger))
    {
        Ledger->Close(TEXT("LedgerRegistrationFailed"));
        // A failed post-registration claim is retained on Source. There may
        // have been reentrant callbacks; no second claim is made over it.
        return nullptr;
    }
    return Ledger;
}

bool UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
    UCk_SceneNodeGym_ActorLedger_UE* Ledger, int32 Role,
    const FTransform& SpawnPose, AActor*& OutActor)
{
    OutActor = nullptr;
    if (!IsInGameThread()) { return false; }
    if (!IsValid(Ledger) || Role < 0 || Role >= CkSceneNodeActorLedgerPrivate::RoleCount ||
        !IsSafePose(SpawnPose))
    { return false; }
    if (!HasCurrentSource(Ledger) || Ledger->ActiveRole != -1 ||
        Ledger->Roles[Role].bCaptured)
    { Ledger->Close(TEXT("RoleSequenceInvalid")); return false; }
    if (Role > 0 && (!Ledger->Roles[0].bFinished ||
        !Ledger->Roles[0].Actor.IsValid()))
    { Ledger->Close(TEXT("ScopeRoleUnavailable")); return false; }
    if (!TryProve_CurrentCohort(Ledger, false))
    { Ledger->Close(TEXT("PreSpawnCohortMismatch")); return false; }

    UWorld* const World = Ledger->SourceWorld.Get();
    AActor* const Owner = Role == 0
        ? static_cast<AActor*>(Ledger->Source.Get()) : Ledger->Roles[0].Actor.Get();
    UClass* const ExpectedClass = Role == 0
        ? AActor::StaticClass() : Ledger->VisualClasses[Role - 1].Get();
    if (!IsValid(World) || !IsValid(Owner) || !IsValid(ExpectedClass) ||
        Owner->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Owner->IsActorBeingDestroyed() || Owner->GetWorld() != World ||
        !CkSceneNodeActorLedgerPrivate::HasDelayedSpawnNotification())
    { Ledger->Close(TEXT("SpawnContextInvalid")); return false; }
    if (!CkSceneNodeActorLedgerPrivate::HasSafeClassDefault(ExpectedClass))
    { Ledger->Close(TEXT("ClassDefaultChanged")); return false; }

    auto& Slot = Ledger->Roles[Role];
    Slot.SpawnPose = SpawnPose;
    Ledger->ActiveRole = Role;
    FActorSpawnParameters Params;
    Params.Owner = Owner;
    Params.bDeferConstruction = true;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> WeakLedger{Ledger};
    Params.CustomPreSpawnInitalization = [WeakLedger, Role](AActor* Raw)
    {
        if (auto* const Current = WeakLedger.Get())
        {
            auto& Captured = Current->Roles[Role];
            if (Raw == nullptr || Captured.bCaptured || Current->ActiveRole != Role)
            { Current->bOpaqueAllocation = true; Current->Close(TEXT("PreSpawnCaptureConflict")); return; }
            Captured.Actor = Raw; // before insertion into the level actor list
            Captured.bCaptured = true;
            if (Role > 0)
            {
                auto* const Visual = Cast<ACk_SceneNodeGym_SourceActor_UE>(Raw);
                if (Visual == nullptr)
                { Current->Close(TEXT("PreSpawnClassMismatch")); return; }
                Visual->Ledger = Current;
                Visual->Role = Role;
                Visual->Generation = Current->Generation;
            }
        }
    };
    AActor* const Spawned = World->SpawnActor<AActor>(ExpectedClass, SpawnPose, Params);
    if (Spawned == nullptr || !Slot.bCaptured || !CkSceneNodeActorLedgerPrivate::SameActor(Slot.Actor, Spawned))
    {
        if (Spawned != nullptr && !Slot.bCaptured)
        { Slot.Actor = Spawned; Slot.bCaptured = true; }
        Ledger->bOpaqueAllocation = true;
        Ledger->Close(TEXT("OpaqueSpawnReturn"));
        Ledger->ActiveRole = -1;
        return false;
    }
    if (Ledger->bClosed || !HasCurrentSource(Ledger) ||
        !IsValid(Spawned) ||
        Spawned->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Spawned->IsActorBeingDestroyed() ||
        Spawned->GetClass() != ExpectedClass || Spawned->GetWorld() != World ||
        Spawned->GetOwner() != Owner || !CkSceneNodeActorLedgerPrivate::IsInLoadedLevel(World, Spawned) ||
        Spawned->IsActorInitialized() || Spawned->HasActorBegunPlay())
    { Ledger->Close(TEXT("DeferredActorInvalid")); Ledger->ActiveRole = -1; return false; }
    if (Role > 0)
    {
        auto* const Visual = Cast<ACk_SceneNodeGym_SourceActor_UE>(Spawned);
        if (!IsValid(Visual) || Visual->Ledger.Get() != Ledger ||
            Visual->Role != Role || Visual->Generation != Ledger->Generation)
        { Ledger->Close(TEXT("DeferredClaimMismatch")); Ledger->ActiveRole = -1; return false; }
    }
    if (Spawned->GetIsReplicated() || Spawned->IsReplicatingMovement() ||
        Spawned->GetRemoteRole() != ROLE_None ||
        !CkSceneNodeActorLedgerPrivate::HasDelayedSpawnNotification())
    { Ledger->Close(TEXT("DeferredReplicationMismatch")); Ledger->ActiveRole = -1; return false; }

    Slot.bFinishing = true;
    AActor* const Finished = UGameplayStatics::FinishSpawningActor(Spawned, Slot.SpawnPose);
    Slot.bFinishing = false;
    Ledger->ActiveRole = -1;
    if (Ledger->bClosed || Finished != Spawned || !Slot.bFinishNotified ||
        !HasCurrentSource(Ledger) || !IsValid(Spawned) ||
        Spawned->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Spawned->IsActorBeingDestroyed() || Spawned->GetClass() != ExpectedClass ||
        Spawned->GetWorld() != World || Spawned->GetOwner() != Owner ||
        Spawned->GetIsReplicated() || Spawned->IsReplicatingMovement() ||
        Spawned->GetRemoteRole() != ROLE_None ||
        !CkSceneNodeActorLedgerPrivate::IsInLoadedLevel(World, Spawned))
    { Ledger->Close(TEXT("FinishActorMismatch")); return false; }
    Slot.bFinished = true;
    if (!TryProve_CurrentCohort(Ledger, false))
    { Ledger->Close(TEXT("PostFinishCohortMismatch")); return false; }
    OutActor = Spawned;
    return true;
}

bool UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasExactCapturedRoles(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger, bool bRequireComplete)
{
    if (!HasCurrentSource(Ledger)) { return false; }
    UWorld* const World = Ledger->SourceWorld.Get();
    AGameStateBase* const Source = Ledger->Source.Get();
    AActor* const Scope = Ledger->Roles[0].Actor.Get();
    if (bRequireComplete && !Ledger->Roles[0].bFinished) { return false; }

    int32 Captured = 0;
    for (int32 Role = 0; Role < CkSceneNodeActorLedgerPrivate::RoleCount; ++Role)
    {
        const auto& Slot = Ledger->Roles[Role];
        if (!Slot.bCaptured)
        {
            if (Slot.Actor.IsValid() || Slot.bFinishing || Slot.bFinishNotified ||
                Slot.bFinished || bRequireComplete)
            { return false; }
            continue;
        }
        ++Captured;
        AActor* const Actor = Slot.Actor.Get();
        if (!IsValid(Actor) ||
            Actor->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Actor->IsActorBeingDestroyed() ||
            Slot.bDestroyedObserved || Slot.bRemovedObserved ||
            Actor->GetWorld() != World || !CkSceneNodeActorLedgerPrivate::IsInLoadedLevel(World, Actor) ||
            Actor->GetClass() != (Role == 0 ? AActor::StaticClass()
                : Ledger->VisualClasses[Role - 1].Get()) ||
            Actor->GetOwner() != (Role == 0 ? static_cast<AActor*>(Source) : Scope) ||
            Actor->GetIsReplicated() || Actor->IsReplicatingMovement() ||
            Actor->GetRemoteRole() != ROLE_None ||
            (bRequireComplete && !Slot.bFinished) ||
            (!Slot.bFinishing && Slot.bFinishNotified && !Slot.bFinished))
        { return false; }
        if (Role > 0)
        {
            const auto* const Visual = Cast<ACk_SceneNodeGym_SourceActor_UE>(Actor);
            if (!IsValid(Visual) || Visual->Ledger.Get() != Ledger ||
                Visual->Role != Role || Visual->Generation != Ledger->Generation)
            { return false; }
        }
        for (int32 Prior = 0; Prior < Role; ++Prior)
        {
            if (Ledger->Roles[Prior].bCaptured &&
                Slot.Actor.HasSameIndexAndSerialNumber(Ledger->Roles[Prior].Actor))
            { return false; }
        }
    }
    if (bRequireComplete && Captured != CkSceneNodeActorLedgerPrivate::RoleCount) { return false; }

    return true;
}

bool UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger)
{
    return HasExactCapturedRoles(Ledger, false);
}

int32 UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_Generation(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger)
{
    if (!IsInGameThread()) { return 0; }
    return IsValid(Ledger) &&
        !Ledger->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
        ? Ledger->Generation : 0;
}

int32 UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger)
{
    if (!IsInGameThread()) { return 0; }
    if (!IsValid(Ledger) ||
        Ledger->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Ledger->Roles.Num() != CkSceneNodeActorLedgerPrivate::RoleCount)
    { return 0; }
    int32 Count = 0;
    for (const auto& Role : Ledger->Roles) { Count += Role.bCaptured ? 1 : 0; }
    return Count;
}

FName UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_ClosureReason(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger)
{
    if (!IsInGameThread()) { return NAME_None; }
    return IsValid(Ledger) &&
        !Ledger->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
        ? Ledger->FirstClosureReason : NAME_None;
}

AGameStateBase* UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_Source(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger)
{
    return HasCurrentSource(Ledger) ? Ledger->Source.Get() : nullptr;
}

bool UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger, int32 Role, AActor*& OutActor)
{
    OutActor = nullptr;
    if (!HasLiveSourceContext(Ledger) || Ledger->ActiveRole != -1 ||
        Role < 0 || Role >= CkSceneNodeActorLedgerPrivate::RoleCount || !Ledger->Roles[Role].bCaptured ||
        !Ledger->Roles[Role].bFinished ||
        Ledger->Roles[Role].bDestroyedObserved ||
        Ledger->Roles[Role].bRemovedObserved)
    { return false; }
    AActor* const Candidate = Ledger->Roles[Role].Actor.Get();
    UWorld* const World = Ledger->SourceWorld.Get();
    AGameStateBase* const Source = Ledger->Source.Get();
    AActor* const Scope = Ledger->Roles[0].Actor.Get();
    if (!IsValid(Candidate) ||
        Candidate->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Candidate->IsActorBeingDestroyed() ||
        Candidate->GetWorld() != World || !CkSceneNodeActorLedgerPrivate::IsInLoadedLevel(World, Candidate) ||
        Candidate->GetClass() != (Role == 0 ? AActor::StaticClass()
            : Ledger->VisualClasses[Role - 1].Get()) ||
        Candidate->GetIsReplicated() || Candidate->IsReplicatingMovement() ||
        Candidate->GetRemoteRole() != ROLE_None)
    { return false; }
    if (Role > 0 && (!IsValid(Scope) ||
        Scope->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Scope->IsActorBeingDestroyed() ||
        Scope->GetClass() != AActor::StaticClass() || Scope->GetWorld() != World ||
        Scope->GetOwner() != Source || Scope->GetIsReplicated() ||
        Scope->IsReplicatingMovement() || Scope->GetRemoteRole() != ROLE_None ||
        !CkSceneNodeActorLedgerPrivate::IsInLoadedLevel(World, Scope)))
    { return false; }
    if (Candidate->GetOwner() != (Role == 0
        ? static_cast<AActor*>(Source) : Scope))
    { return false; }
    if (Role > 0)
    {
        const auto* const Visual = Cast<ACk_SceneNodeGym_SourceActor_UE>(Candidate);
        if (!IsValid(Visual) || Visual->Ledger.Get() != Ledger ||
            Visual->Role != Role || Visual->Generation != Ledger->Generation)
        { return false; }
    }
    OutActor = Candidate;
    return true;
}

bool UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger, bool bRequireComplete)
{
    if (!HasCurrentSource(Ledger)) { return false; }
    if (!HasExactCapturedRoles(Ledger, bRequireComplete)) { return false; }
    UWorld* const World = Ledger->SourceWorld.Get();
    AActor* const Scope = Ledger->Roles[0].Actor.Get();

    // AllActors includes pending-kill actors and inactive loaded levels. The
    // captured slots are also checked above because engine destruction clears
    // Owner before level removal; a census alone could miss a lost known role.
    for (TActorIterator<AActor> It(World, AActor::StaticClass(),
        EActorIteratorFlags::AllActors); It; ++It)
    {
        AActor* const Actor = *It;
        if (Actor == nullptr || Actor == Scope) { continue; }
        const CkSceneNodeActorLedgerPrivate::EScopeRelation Relation = Scope != nullptr
            ? CkSceneNodeActorLedgerPrivate::GetScopeRelation(Actor, Scope) : CkSceneNodeActorLedgerPrivate::EScopeRelation::Unrelated;
        if (Relation == CkSceneNodeActorLedgerPrivate::EScopeRelation::Malformed) { return false; }
        if (Relation == CkSceneNodeActorLedgerPrivate::EScopeRelation::Descendant)
        {
            int32 KnownRole = -1;
            if (!IsKnownIdentity(Ledger, Actor, KnownRole) || KnownRole == 0 ||
                Actor->GetOwner() != Scope)
            { return false; }
        }
    }
    return true;
}

bool UCk_SceneNodeGym_ActorLedgerRuntime_UE::Request_Close(
    UCk_SceneNodeGym_ActorLedger_UE* Ledger)
{
    if (!IsInGameThread() || !IsValid(Ledger) ||
        Ledger->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    Ledger->Close(TEXT("SourceRequestedClose"));
    return true;
}
