#include "CkSelectionGym_SourceSession.h"

#include "CkSelectionGym_BankCensus.h"
#include "CkSelectionGym_WireValidation.h"
#include "CkEntityScriptGym_Runtime.h"
#if !UE_BUILD_SHIPPING
#include "CkEntityDebugOverlay/Selection/CkDebugOverlay_SourceAdmission.h"
#endif
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/ContextOwner/CkContextOwner_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkLabel/CkLabel_Fragment.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"
#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Fragment.h"
#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Utils.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment.h"
#include "CkStateMachine/Debug/CkStateMachine_Debug_Fragment.h"
#include "CkStateMachine/State/CkSmState_Fragment.h"
#include "CkStateMachine/State/EntityScripts/CkSmState_EntityScript.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInterface.h"

#if !UE_BUILD_SHIPPING
namespace
{
    auto Allocated(const FCk_Handle& H) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    auto Live(const FCk_Handle& H) -> bool
    {
        return Allocated(H) && ck::IsValid(H) &&
            !H.Has_Any<ck::FTag_DestroyEntity_Initiate,
                ck::FTag_DestroyEntity_EndPlay, ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>();
    }

    auto LiveWorld(UWorld* World, AGameStateBase* GS) -> bool
    {
        return IsInGameThread() && IsValid(World) && IsValid(GS) &&
            !World->HasAnyFlags(RF_ClassDefaultObject | RF_BeginDestroyed | RF_FinishDestroyed) &&
            World->IsGameWorld() && !World->bIsTearingDown &&
            !World->IsBeingCleanedUp() &&
            !GS->IsActorBeingDestroyed() && GS->GetWorld() == World &&
            World->GetGameState() == GS && GS->HasAuthority();
    }

    auto ExactRoot(UWorld* World, const FCk_Handle& Root) -> bool
    {
        if (!IsValid(World) || !Live(Root) ||
            !Root.Has<TWeakObjectPtr<UWorld>>() ||
            Root.Get<TWeakObjectPtr<UWorld>>().Get() != World)
        { return false; }
        const auto Registry = Root.Get_RegistryView();
        return FCk_Handle{Registry.Get_TransientEntity(),
            Registry.Get_RegistryHandle()} == Root &&
            UCk_Utils_EcsWorld_Subsystem_UE::
                TryGet_TransientEntityForWorld(*World) == Root;
    }

    auto Direct(const FCk_Handle& Child, const FCk_Handle& Owner) -> bool
    {
        return Live(Child) && Live(Owner) &&
            Child.Get_RegistryView().Get_RegistryHandle() ==
                Owner.Get_RegistryView().Get_RegistryHandle() &&
            Child.Has<ck::FFragment_LifetimeOwner>() &&
            Child.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == Owner;
    }

    auto ExactIdleRequest(const FCk_Handle& Carrier,
        const FCk_Handle& State, const FCk_Handle& Machine,
        TSubclassOf<UCk_SmState_EntityScript> IdleClass,
        const FCk_Handle& WorldRoot, FString* OutDiagnostic = nullptr) -> bool
    {
        auto Reject = [&](const TCHAR* Cause)
        {
            if (OutDiagnostic != nullptr) { *OutDiagnostic = Cause; }
            return false;
        };
        if (!Allocated(Carrier) || !ck::IsValid(Carrier,
                ck::IsValid_Policy_IncludePendingKill{}))
        { return Reject(TEXT("Idle carrier is no longer allocated")); }
        if (!Live(State) || !Live(Machine) || !IsValid(IdleClass.Get()))
        { return Reject(TEXT("Idle carrier state, machine, or class is not live")); }
        if (Carrier.Get_RegistryView().Get_RegistryHandle() !=
            WorldRoot.Get_RegistryView().Get_RegistryHandle())
        { return Reject(TEXT("Idle carrier registry differs from source root")); }
        if (!Carrier.Has<ck::FFragment_LifetimeOwner>())
        { return Reject(TEXT("Idle carrier has no lifetime owner fragment")); }
        if (Carrier.Get<ck::FFragment_LifetimeOwner,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entity() != State)
        { return Reject(TEXT("Idle carrier lifetime owner is not current state")); }
        if (!Carrier.Has<ck::FFragment_EntityScript_RequestSpawnEntity>())
        { return Reject(TEXT("Idle carrier has no EntityScript spawn request fragment")); }
        const auto& Request = Carrier.Get<
            ck::FFragment_EntityScript_RequestSpawnEntity,
            ck::IsValid_Policy_IncludePendingKill>();
        if (Request.Get_NewEntity() != State)
        { return Reject(TEXT("Idle carrier request target is not current state")); }
        if (Request.Get_Owner() != Machine)
        { return Reject(TEXT("Idle carrier request owner is not authored machine")); }
        if (Request.Get_ContextOwner() != Machine)
        { return Reject(TEXT("Idle carrier request context owner differs from authored machine")); }
        if (Request.Get_EntityScriptClassArchetype().Get() !=
            IdleClass.GetDefaultObject())
        { return Reject(TEXT("Idle carrier request archetype is not captured Idle CDO")); }
        if (Request.Get_SpawnParams().IsValid())
        { return Reject(TEXT("Idle carrier request spawn params are unexpectedly populated")); }
        if (Request.Get_DestroyTargetOnFailure())
        { return Reject(TEXT("Idle carrier request owns the target unexpectedly")); }
        TArray<FCk_Handle> Children;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryGet_AllocatedDirectLifetimeDependents(Carrier, WorldRoot, Children))
        { return Reject(TEXT("Idle carrier child census is unreadable")); }
        return Children.IsEmpty() ? true :
            Reject(TEXT("Idle carrier has allocated child descendants"));
    }

    // The EntityScript processor removes its request fragment immediately
    // after marking that carrier for destruction. Without a first-return
    // receipt its former request identity is unknowable. This branch only
    // waits for one dying, unadmitted leaf and never treats it as bank data.
    auto ExactDyingUnadmittedLeaf(const FCk_Handle& Child,
        const FCk_Handle& State, const FCk_Handle& WorldRoot) -> bool
    {
        if (!Allocated(Child) || Live(Child) || !Live(State) ||
            !Child.Has<ck::FTag_DestroyEntity_Initiate>() ||
            Child.Get_RegistryView().Get_RegistryHandle() !=
                WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
            !Child.Has<ck::FFragment_LifetimeOwner>() ||
            Child.Get<ck::FFragment_LifetimeOwner,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entity() != State ||
            Child.Has<ck::FFragment_EntityScript_RequestSpawnEntity>() ||
            Child.Has<ck::FFragment_EntityScript>() ||
            !State.Has<ck::FFragment_EntityScript>())
        { return false; }
        TArray<FCk_Handle> Children;
        return UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryGet_AllocatedDirectLifetimeDependents(
                Child, WorldRoot, Children) && Children.IsEmpty();
    }

}

bool UCkSelectionGym_SourceSession_UE::Current(
    const UCkSelectionGym_SourceSession* S)
{
    if (!IsValid(S) || S->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    auto* World = S->World.Get();
    auto* GS = S->GameState.Get();
    return LiveWorld(World, GS) && ExactRoot(World, S->WorldRoot) &&
        S->GetOuter() == GS;
}

bool UCkSelectionGym_SourceSession_UE::ValidVisual(
    const UCkSelectionGym_SourceSession* S,
    const FCkSelectionGym_SourceVisualReceipt& R)
{
        auto* Actor = R.Actor.Get();
        auto* Mesh = R.Mesh.Get();
        auto* Text = R.Text.Get();
        if (!Current(S) || R.VisualKey <= 0 || !IsValid(Actor) ||
            Actor->IsActorBeingDestroyed() ||
            Actor->GetWorld() != S->World.Get() ||
            Actor->GetOwner() != S->GameState.Get() ||
            !IsValid(Mesh) || Mesh->IsBeingDestroyed() ||
            Mesh->GetOwner() != Actor || !Mesh->IsRegistered() ||
            Mesh->GetStaticMesh() == nullptr)
        { return false; }
        switch (R.Kind)
        {
        case ECkSelectionGym_VisualKind::LabelMarker:
            return Actor->GetClass() == S->MarkerClass.Get() &&
                Live(R.Node) &&
                R.Node.Get_RegistryView().Get_RegistryHandle() ==
                    S->WorldRoot.Get_RegistryView().Get_RegistryHandle() &&
                IsValid(Text) && !Text->IsBeingDestroyed() &&
                Text->GetOwner() == Actor && Text->IsRegistered() &&
                Actor->GetRootComponent() != nullptr &&
                Mesh->GetAttachParent() == Actor->GetRootComponent() &&
                Text->GetAttachParent() == Actor->GetRootComponent();
        case ECkSelectionGym_VisualKind::OcclusionWall:
            return Actor->GetClass() == S->WallClass.Get() &&
                R.Node == FCk_Handle{} && Text == nullptr &&
                Actor->GetRootComponent() == Mesh;
        default:
            return false;
        }
}

bool UCkSelectionGym_SourceSession_UE::ExactOwnedVisualCensus(
    const UCkSelectionGym_SourceSession* S)
{
    if (!Current(S)) { return false; }
    int32 Owned = 0;
    for (TActorIterator<AActor> It(S->World.Get()); It; ++It)
    {
        auto* Actor = *It;
        if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() ||
            Actor->GetOwner() != S->GameState.Get() ||
            (Actor->GetClass() != S->MarkerClass.Get() &&
             Actor->GetClass() != S->WallClass.Get()))
        { continue; }
        ++Owned;
        bool Found = false;
        for (const auto& R : S->Visuals)
        { Found |= R.Actor.Get() == Actor; }
        if (!Found) { return false; }
    }
    int32 RetainedAlive = 0;
    for (const auto& R : S->Visuals)
    {
        auto* Actor = R.Actor.Get();
        if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) { continue; }
        if (Actor->GetWorld() != S->World.Get() ||
            Actor->GetOwner() != S->GameState.Get() ||
            Actor->GetClass() !=
                (R.Kind == ECkSelectionGym_VisualKind::OcclusionWall ?
                    S->WallClass.Get() : S->MarkerClass.Get()))
        { return false; }
        ++RetainedAlive;
    }
    return Owned == RetainedAlive;
}

bool UCkSelectionGym_SourceSession_UE::ProveFrozenRetirement(
    const UCkSelectionGym_SourceSession* S)
{
    if (!Current(S) || !S->Retiring || S->FrozenRetirement.IsEmpty() ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
            S->FrozenRetirement, S->FrozenRetirement, S->WorldRoot))
    { return false; }
    TSet<FCk_Handle> Known;
    for (const auto& H : S->FrozenRetirement)
    {
        if (Known.Contains(H)) { return false; }
        Known.Add(H);
    }
    TSet<FCk_Handle> Roots;
    for (const auto& Root : S->BareRoots) { Roots.Add(Root); }
    const auto Registry = S->WorldRoot.Get_RegistryView().Get_RegistryHandle();
    for (const auto& H : S->FrozenRetirement)
    {
        if (!Allocated(H)) { continue; }
        if (H.Get_RegistryView().Get_RegistryHandle() != Registry ||
            !H.Has<ck::FFragment_LifetimeOwner>())
        { return false; }
        const auto Parent = H.Get<ck::FFragment_LifetimeOwner,
            ck::IsValid_Policy_IncludePendingKill>().Get_Entity();
        if (Roots.Contains(H) ? Parent != S->WorldRoot :
            (!Known.Contains(Parent) || !Allocated(Parent)))
        { return false; }
        TArray<FCk_Handle> Children;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryGet_AllocatedDirectLifetimeDependents(H, S->WorldRoot, Children))
        { return false; }
        TSet<FCk_Handle> Filtered;
        for (const auto& Child : Children)
        {
            if (Filtered.Contains(Child) || !Known.Contains(Child) ||
                !Allocated(Child))
            { return false; }
            Filtered.Add(Child);
        }
        if (H.Has<ck::FFragment_LifetimeDependents>())
        {
            TSet<FCk_Handle> RawCurrent;
            for (const auto& Child : H.Get<ck::FFragment_LifetimeDependents,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entities())
            {
                if (!Allocated(Child)) { continue; }
                if (Child.Get_RegistryView().Get_RegistryHandle() != Registry ||
                    !Known.Contains(Child) ||
                    !Child.Has<ck::FFragment_LifetimeOwner>())
                { return false; }
                const auto Owner = Child.Get<ck::FFragment_LifetimeOwner,
                    ck::IsValid_Policy_IncludePendingKill>().Get_Entity();
                if (Owner != H) { continue; } // historical transferred edge
                if (RawCurrent.Contains(Child)) { return false; }
                RawCurrent.Add(Child);
            }
            if (RawCurrent.Num() != Filtered.Num()) { return false; }
            for (const auto& Child : Filtered)
            { if (!RawCurrent.Contains(Child)) { return false; } }
        }
        else if (!Filtered.IsEmpty())
        { return false; }
    }
    return ExactOwnedVisualCensus(S);
}

namespace
{
    auto ExpectedNodes(int32 Scenario) -> int32
    {
        static constexpr int32 Counts[] = {5, 5, 7, 4, 3};
        return Scenario >= 0 && Scenario < UE_ARRAY_COUNT(Counts) ?
            Counts[Scenario] : -1;
    }

    auto ExpectedVisuals(int32 Scenario) -> int32
    {
        static constexpr int32 Counts[] = {3, 4, 4, 4, 2};
        return Scenario >= 0 && Scenario < UE_ARRAY_COUNT(Counts) ?
            Counts[Scenario] : -1;
    }

    auto Fail(FString& OutReason, const TCHAR* Why) -> bool
    { OutReason = Why; return false; }
}

ECkSelectionGym_SourceOutcome UCkSelectionGym_SourceSession_UE::CheckExactNodes(
    const UCkSelectionGym_SourceSession* S,
    const TSet<FCk_Handle>& Authored, const TSet<FCk_Handle>& Whole,
    bool AllowInitialPending, FCk_Handle& OutIdleState, FString& OutReason)
{
    OutIdleState = {};
    auto Invalid = [&](const TCHAR* Why)
    {
        OutReason = Why;
        return ECkSelectionGym_SourceOutcome::Invalid;
    };
    for (const auto& H : Authored)
    { if (!Whole.Contains(H)) { return Invalid(TEXT("Authored node escaped source forest")); } }
    if (S->Scenario != 2)
    {
        if (Whole.Num() != Authored.Num())
        { return Invalid(TEXT("Unclaimed source descendant")); }
        return ECkSelectionGym_SourceOutcome::Complete;
    }
    FCk_Handle Machine;
    for (const auto& H : Authored)
    {
        if (!H.Has<ck::FFragment_Sm>()) { continue; }
        if (Machine != FCk_Handle{})
        { return Invalid(TEXT("Multiple authored state machines")); }
        Machine = H;
    }
    if (!Live(Machine) || !Machine.Has<ck::FFragment_Sm_Params>() ||
        !IsValid(S->IdleStateClass.Get()) ||
        Machine.Get<ck::FFragment_Sm_Params>().Get_InitialStateClass() !=
            S->IdleStateClass)
    { return Invalid(TEXT("Authored Idle machine specification changed")); }
    const auto& Sm = Machine.Get<ck::FFragment_Sm>();
    const FCk_Handle State = Sm.Get_CurrentStateHandle();
    if (!Live(State))
    {
        if (AllowInitialPending && State == FCk_Handle{} &&
            Whole.Num() == Authored.Num())
        {
            OutReason = TEXT("Authored Idle state awaits native creation");
            return ECkSelectionGym_SourceOutcome::Pending;
        }
        return Invalid(TEXT("Current Idle state missing or retiring"));
    }
    if (S->FrameworkIdleState != FCk_Handle{} &&
        State != S->FrameworkIdleState)
    { return Invalid(TEXT("Pinned Idle state identity changed")); }
    if (Sm.Get_CurrentStateClass() != S->IdleStateClass ||
        !Direct(State, Machine) ||
        !State.Has<ck::FFragment_SmState_Params>() ||
        !State.Has<ck::FFragment_SmState_Hierarchy>() ||
        !State.Has<ck::FFragment_Sm_OwningStateMachine>() ||
        !State.Has<ck::FTag_SmState_FullyEventDriven>())
    { return Invalid(TEXT("Current Idle state provenance changed")); }
    const auto& Params = State.Get<ck::FFragment_SmState_Params>();
    if (Params.Get_RequestedScriptClass() != S->IdleStateClass ||
        Params.Get_ResolvedScriptClass() != S->IdleStateClass ||
        State.Get<ck::FFragment_Sm_OwningStateMachine>().Get_Entity() != Machine)
    { return Invalid(TEXT("Current Idle state class or owning machine changed")); }
    // Before EntityScript construction retires its single request carrier,
    // that carrier may still be live and appear in a successful strict census.
    // It is never a published node or an arbitrary extra descendant.
    if (AllowInitialPending && Whole.Num() == Authored.Num() + 2 &&
        Whole.Contains(State))
    {
        FCk_Handle Carrier;
        for (const auto& H : Whole)
        {
            if (Authored.Contains(H) || H == State) { continue; }
            if (Carrier != FCk_Handle{})
            { return Invalid(TEXT("Multiple unclaimed initial state children")); }
            Carrier = H;
        }
        if (Live(Carrier) && ExactIdleRequest(Carrier, State, Machine,
            S->IdleStateClass, S->WorldRoot))
        {
            OutReason = TEXT("Exact Idle script request awaits retirement");
            return ECkSelectionGym_SourceOutcome::Pending;
        }
        return Invalid(TEXT("Unclaimed child beside current Idle state"));
    }
    if (!State.Has<ck::FFragment_EntityScript>())
    {
        if (State.Has_Any<ck::FTag_EntityScript_HasBegunPlay,
            ck::FTag_EntityScript_HasEndedPlay>())
        { return Invalid(TEXT("Idle state has lifecycle tags without a script")); }
        if (AllowInitialPending && Whole.Num() == Authored.Num() + 1 &&
            Whole.Contains(State))
        {
            OutReason = TEXT("Exact Idle state script awaits construction");
            return ECkSelectionGym_SourceOutcome::Pending;
        }
        return Invalid(TEXT("Current Idle state script missing"));
    }
    const auto* Script = State.Get<ck::FFragment_EntityScript>().Get_Script().Get();
    if (!IsValid(Script) ||
        Script->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Script->GetClass() != S->IdleStateClass.Get() ||
        UCk_Utils_EntityScript_UE::TryGet_EntityScript(State) != Script ||
        State.Has<ck::FTag_EntityScript_HasEndedPlay>())
    { return Invalid(TEXT("Idle state script identity or lifecycle changed")); }
    const bool PendingConstruction =
        !State.Has<ck::FTag_EntityScript_HasBegunPlay>() ||
        State.Has_Any<ck::FTag_EntityScript_ContinueConstruction,
            ck::FTag_EntityScript_FinishConstruction,
            ck::FTag_EntityScript_BeginPlay,
            ck::FTag_EntityScript_ConstructedThisFrame>();
    if (PendingConstruction)
    {
        if (AllowInitialPending && Whole.Num() == Authored.Num() + 1 &&
            Whole.Contains(State))
        {
            OutReason = TEXT("Exact Idle script awaits finished BeginPlay");
            return ECkSelectionGym_SourceOutcome::Pending;
        }
        return Invalid(TEXT("Pinned Idle script construction incomplete"));
    }
    if (Whole.Num() != Authored.Num() + 1 || !Whole.Contains(State) ||
        Authored.Contains(State))
    { return Invalid(TEXT("Unclaimed descendant beside current Idle state")); }
    OutIdleState = State;
    return ECkSelectionGym_SourceOutcome::Complete;
}

bool UCkSelectionGym_SourceSession_UE::TryBegin(UWorld* World,
    AGameStateBase* ExactGameState, const FCk_Handle& WorldRoot,
    int64 SourceGeneration, int64 ScenarioGeneration,
    int64 AdmissionGeneration, int32 Scenario, FVector Origin,
    const TArray<FCk_Handle>& BareRoots,
    TSubclassOf<AActor> MarkerClass, TSubclassOf<AActor> WallClass,
    TSubclassOf<UCk_SmState_EntityScript> IdleStateClass,
    UCkSelectionGym_SourceSession*& OutSession, FString& OutReason)
{
    OutSession = nullptr;
    OutReason.Reset();
    if (!LiveWorld(World, ExactGameState) || !ExactRoot(World, WorldRoot) ||
        SourceGeneration <= 0 || ScenarioGeneration <= 0 ||
        AdmissionGeneration <= 0 || ExpectedNodes(Scenario) < 0 ||
        !FMath::IsFinite(Origin.X) || !FMath::IsFinite(Origin.Y) ||
        !FMath::IsFinite(Origin.Z) || !IsValid(MarkerClass.Get()) ||
        !IsValid(WallClass.Get()) || MarkerClass == WallClass ||
        (Scenario == 2 && !IsValid(IdleStateClass.Get())) ||
        (IsValid(MarkerClass.Get()) && MarkerClass.Get()->HasAnyClassFlags(
            CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) ||
        (IsValid(WallClass.Get()) && WallClass.Get()->HasAnyClassFlags(
            CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) ||
        (Scenario == 2 && IsValid(IdleStateClass.Get()) &&
            IdleStateClass.Get()->HasAnyClassFlags(
                CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) ||
        BareRoots.Num() != (Scenario == 0 ? 3 : 1))
    { return Fail(OutReason, TEXT("Invalid exact source context or authored bare roots")); }

    TSet<FCk_Handle> Unique;
    for (const auto& Root : BareRoots)
    {
        if (!Direct(Root, WorldRoot) || Unique.Contains(Root))
        { return Fail(OutReason, TEXT("Bare source root missing, duplicate, or not directly owned")); }
        Unique.Add(Root);
    }
    // TryBegin itself verifies bare roots have no selectable composition and
    // rejects a previous source whose full known forest remains allocated.
    if (!UCk_DebugOverlay_SourceAdmission_UE::TryBegin(World,
        WorldRoot, AdmissionGeneration, BareRoots, OutReason))
    { return false; }

    auto* S = NewObject<UCkSelectionGym_SourceSession>(ExactGameState);
    if (!IsValid(S))
    {
        FString Ignored;
        UCk_DebugOverlay_SourceAdmission_UE::TryClose(World, WorldRoot,
            AdmissionGeneration, Ignored);
        return Fail(OutReason, TEXT("Source session allocation failed"));
    }
    S->World = World;
    S->GameState = ExactGameState;
    S->WorldRoot = WorldRoot;
    S->BareRoots = BareRoots;
    S->SourceGeneration = SourceGeneration;
    S->ScenarioGeneration = ScenarioGeneration;
    S->AdmissionGeneration = AdmissionGeneration;
    S->Scenario = Scenario;
    S->Origin = Origin;
    S->MarkerClass = MarkerClass;
    S->WallClass = WallClass;
    S->IdleStateClass = IdleStateClass;
    OutSession = S;
    return true;
}

bool UCkSelectionGym_SourceSession_UE::TryRetainVisual(
    UCkSelectionGym_SourceSession* Session,
    const FCkSelectionGym_SourceVisualReceipt& Visual,
    FString& OutReason)
{
    OutReason.Reset();
    auto Reject = [&](const TCHAR* Why)
    {
        OutReason = Why;
        // The rejected actor was never retained. Its creator owns it; the
        // source may close only the receipts it already accepted.
        FString Ignored;
        TryFail(Session, OutReason, Ignored);
        return false;
    };
    if (!Current(Session) || Session->Failed || Session->Retiring ||
        Session->Staged || Session->Visuals.Num() >= ExpectedVisuals(Session->Scenario) ||
        !ValidVisual(Session, Visual))
    { return Reject(TEXT("Visual receipt has no exact live source provenance")); }
    for (const auto& Previous : Session->Visuals)
    {
        if (Previous.VisualKey == Visual.VisualKey ||
            Previous.Actor == Visual.Actor || Previous.Mesh == Visual.Mesh ||
            (Visual.Text != nullptr && Previous.Text == Visual.Text))
        { return Reject(TEXT("Visual receipt identity was reused")); }
    }
    // A visual is retained while physically hidden and collision-free. The
    // final synchronous activation/sampling boundary is a separate slice.
    if (!Visual.Actor->IsHidden() || Visual.Actor->GetActorEnableCollision())
    { return Reject(TEXT("Unadmitted source visual is physically exposed")); }
    Session->Visuals.Add(Visual);
    return true;
}

bool UCkSelectionGym_SourceSession_UE::TryStage(
    UCkSelectionGym_SourceSession* Session,
    const TArray<FCk_Handle>& AuthoredNodes, FString& OutReason)
{
    OutReason.Reset();
    if (!Current(Session) || Session->Failed || Session->Retiring ||
        Session->Staged ||
        AuthoredNodes.Num() != ExpectedNodes(Session->Scenario) ||
        Session->Visuals.Num() != ExpectedVisuals(Session->Scenario))
    { return Fail(OutReason, TEXT("Incomplete or stale authored source receipts")); }

    if (TryCheckStageReadiness(Session, AuthoredNodes, OutReason) !=
        ECkSelectionGym_SourceOutcome::Complete)
    { return false; }

    TSet<FCk_Handle> Authored;
    for (const auto& Node : AuthoredNodes)
    {
        if (!Live(Node) ||
            Node.Get_RegistryView().Get_RegistryHandle() !=
                Session->WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
            Authored.Contains(Node))
        { return Fail(OutReason, TEXT("Authored node lost, foreign, or duplicated")); }
        Authored.Add(Node);
    }
    for (const auto& Root : Session->BareRoots)
    {
        if (!Authored.Contains(Root) || !Direct(Root, Session->WorldRoot))
        { return Fail(OutReason, TEXT("Retained bare source root missing")); }
    }
    TArray<FCk_Handle> ExtraRoots;
    for (int32 I = 1; I < Session->BareRoots.Num(); ++I)
    { ExtraRoots.Add(Session->BareRoots[I]); }
    TArray<FCk_Handle> Census;
    if (!ck_selection_gym_census::TryCensusAllocatedBank(
        Session->World.Get(), Session->BareRoots[0], ExtraRoots,
        Census, OutReason))
    { return false; }
    TSet<FCk_Handle> Whole;
    for (const auto& Node : Census) { Whole.Add(Node); }
    FCk_Handle ExactIdle;
    if (CheckExactNodes(Session, Authored, Whole, false, ExactIdle,
        OutReason) != ECkSelectionGym_SourceOutcome::Complete)
    { return false; }
    int32 Walls = 0;
    for (const auto& Visual : Session->Visuals)
    {
        if (!ValidVisual(Session, Visual) ||
            (Visual.Kind == ECkSelectionGym_VisualKind::LabelMarker &&
                !Authored.Contains(Visual.Node)))
        { return Fail(OutReason, TEXT("Retained visual association changed")); }
        Walls += Visual.Kind == ECkSelectionGym_VisualKind::OcclusionWall ? 1 : 0;
    }
    if (Walls != (Session->Scenario == 3 ? 1 : 0))
    { return Fail(OutReason, TEXT("Authored wall count changed")); }

    // Exact GS-owned class census prevents a creator omitting one spawned
    // visual from its submitted receipt set.
    int32 OwnedVisuals = 0;
    for (TActorIterator<AActor> It(Session->World.Get()); It; ++It)
    {
        AActor* Actor = *It;
        if (IsValid(Actor) && !Actor->IsActorBeingDestroyed() &&
            Actor->GetOwner() == Session->GameState.Get() &&
            (Actor->GetClass() == Session->MarkerClass.Get() ||
             Actor->GetClass() == Session->WallClass.Get()))
        {
            ++OwnedVisuals;
            bool Found = false;
            for (const auto& Visual : Session->Visuals)
            { Found |= Visual.Actor.Get() == Actor; }
            if (!Found)
            { return Fail(OutReason, TEXT("Unclaimed GS-owned visual actor")); }
        }
    }
    if (OwnedVisuals != Session->Visuals.Num())
    { return Fail(OutReason, TEXT("Retained visual actor census changed")); }
    Session->AuthoredNodes = AuthoredNodes;
    Session->FrameworkIdleState = ExactIdle;
    Session->Staged = true;
    return true;
}

ECkSelectionGym_SourceOutcome
UCkSelectionGym_SourceSession_UE::TryCheckStageReadiness(
    UCkSelectionGym_SourceSession* Session,
    const TArray<FCk_Handle>& AuthoredNodes, FString& OutReason)
{
    OutReason.Reset();
    auto Invalid = [&](const TCHAR* Why)
    {
        OutReason = Why;
        return ECkSelectionGym_SourceOutcome::Invalid;
    };
    if (!Current(Session) || Session->Staged || Session->Failed ||
        Session->Retiring || Session->Replacing ||
        AuthoredNodes.Num() != ExpectedNodes(Session->Scenario) ||
        Session->Visuals.Num() != ExpectedVisuals(Session->Scenario))
    { return Invalid(TEXT("Incomplete or stale initial source receipts")); }
    TSet<FCk_Handle> Authored;
    const auto RegistryHandle = Session->WorldRoot.Get_RegistryView().Get_RegistryHandle();
    for (const auto& Node : AuthoredNodes)
    {
        if (!Live(Node) ||
            Node.Get_RegistryView().Get_RegistryHandle() != RegistryHandle ||
            Authored.Contains(Node))
        { return Invalid(TEXT("Authored node lost, foreign, or duplicated")); }
        Authored.Add(Node);
    }
    TArray<FCk_Handle> Extras;
    for (int32 I = 0; I < Session->BareRoots.Num(); ++I)
    {
        const auto Root = Session->BareRoots[I];
        if (!Direct(Root, Session->WorldRoot) || !Authored.Contains(Root))
        { return Invalid(TEXT("Bare source root lost or omitted")); }
        if (I > 0) { Extras.Add(Root); }
    }
    TArray<FCk_Handle> Complete;
    FString CensusReason;
    if (ck_selection_gym_census::TryCensusAllocatedBank(
        Session->World.Get(), Session->BareRoots[0], Extras,
        Complete, CensusReason))
    {
        TSet<FCk_Handle> Whole;
        for (const auto& H : Complete) { Whole.Add(H); }
        FCk_Handle ExactIdle;
        return CheckExactNodes(Session, Authored, Whole, true,
            ExactIdle, OutReason);
    }

    // The strict live-only census can reject the ordinary, still-allocated
    // request carrier while SM construction retires it. Prove that exact
    // category and every remaining current-owner edge; no generic census
    // error is reclassified as Pending.
    TArray<FCk_Handle> Queue = Session->BareRoots;
    TSet<FCk_Handle> Seen;
    for (const auto& Root : Queue)
    {
        if (Seen.Contains(Root))
        { return Invalid(TEXT("Duplicate source root during initial settle")); }
        Seen.Add(Root);
    }
    int32 PendingCarrierCount = 0;
    TSet<FCk_Handle> Carriers;
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const auto Parent = Queue[Index];
        if (!Live(Parent))
        { return Invalid(TEXT("Ordinary source parent became non-live")); }
        TArray<FCk_Handle> Children;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryGet_AllocatedDirectLifetimeDependents(
                Parent, Session->WorldRoot, Children))
        { return Invalid(TEXT("Initial direct source dependents unreadable")); }
        TSet<FCk_Handle> RawCurrent;
        if (Parent.Has<ck::FFragment_LifetimeDependents>())
        {
            for (const auto& Child : Parent.Get<ck::FFragment_LifetimeDependents,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entities())
            {
                if (!Allocated(Child)) { continue; }
                if (Child.Get_RegistryView().Get_RegistryHandle() != RegistryHandle ||
                    !Child.Has<ck::FFragment_LifetimeOwner>())
                { return Invalid(TEXT("Initial raw reverse child foreign or ownerless")); }
                const auto Owner = Child.Get<ck::FFragment_LifetimeOwner,
                    ck::IsValid_Policy_IncludePendingKill>().Get_Entity();
                if (Owner != Parent) { continue; }
                if (RawCurrent.Contains(Child))
                { return Invalid(TEXT("Duplicate initial raw reverse child")); }
                RawCurrent.Add(Child);
            }
        }
        if (RawCurrent.Num() != Children.Num())
        { return Invalid(TEXT("Initial reverse and filtered child sets differ")); }
        for (const auto& Child : Children)
        {
            if (!RawCurrent.Contains(Child) || Seen.Contains(Child) ||
                Child.Get_RegistryView().Get_RegistryHandle() != RegistryHandle)
            { return Invalid(TEXT("Initial source child duplicated or escaped")); }
            Seen.Add(Child);
            if (!Live(Child))
            {
                FCk_Handle OwningMachine;
                if (Parent.Has<ck::FFragment_Sm_OwningStateMachine>())
                { OwningMachine = Parent.Get<ck::FFragment_Sm_OwningStateMachine>().Get_Entity(); }
                if (Session->Scenario != 2)
                { return Invalid(TEXT("Non-live child outside mixed-hierarchy scenario")); }
                if (!Parent.Has<ck::FFragment_SmState_Params>())
                { return Invalid(TEXT("Non-live child parent is not an SM state")); }
                if (!Live(OwningMachine) || !Authored.Contains(OwningMachine) ||
                    !OwningMachine.Has<ck::FFragment_Sm>())
                { return Invalid(TEXT("Non-live child state has no authored machine")); }
                if (OwningMachine.Get<ck::FFragment_Sm>().Get_CurrentStateHandle() != Parent)
                { return Invalid(TEXT("Non-live child parent is not current SM state")); }
                if (Child.Has<ck::FFragment_EntityScript_RequestSpawnEntity>())
                {
                    if (!ExactIdleRequest(Child, Parent, OwningMachine,
                        Session->IdleStateClass, Session->WorldRoot, &OutReason))
                    { return ECkSelectionGym_SourceOutcome::Invalid; }
                }
                else if (!ExactDyingUnadmittedLeaf(
                    Child, Parent, Session->WorldRoot))
                { return Invalid(TEXT("Unproven retiring child under current Idle state")); }
                ++PendingCarrierCount;
                if (PendingCarrierCount > 1)
                { return Invalid(TEXT("Multiple retiring SM request carriers")); }
                Carriers.Add(Child);
                continue;
            }
            Queue.Add(Child);
        }
    }
    for (const auto& H : Authored)
    {
        if (!Seen.Contains(H))
        { return Invalid(TEXT("Authored node omitted from initial source forest")); }
    }
    bool InverseInvalid = false;
    const auto Registry = Session->WorldRoot.Get_RegistryView();
    Registry.View<ck::FFragment_LifetimeOwner>().ForEach(
        [&](FCk_Entity Id, const ck::FFragment_LifetimeOwner& Owner)
        {
            if (!Seen.Contains(Owner.Get_Entity())) { return; }
            const FCk_Handle Child{Id, RegistryHandle};
            if (Allocated(Child) && !Seen.Contains(Child))
            { InverseInvalid = true; }
        });
    if (InverseInvalid)
    { return Invalid(TEXT("Inverse initial owner census found unclaimed child")); }
    TSet<FCk_Handle> Whole = Seen;
    for (const auto& Carrier : Carriers) { Whole.Remove(Carrier); }
    FCk_Handle ExactIdle;
    const auto Membership = CheckExactNodes(Session, Authored, Whole,
        true, ExactIdle, OutReason);
    if (Membership == ECkSelectionGym_SourceOutcome::Invalid)
    { return ECkSelectionGym_SourceOutcome::Invalid; }
    if (PendingCarrierCount <= 0)
    { return Invalid(TEXT("Strict source census failed without known SM carrier")); }
    OutReason = TEXT("One unadmitted dying Idle-state leaf awaits physical deallocation; no request provenance is inferred");
    return ECkSelectionGym_SourceOutcome::Pending;
}

bool UCkSelectionGym_SourceSession_UE::TryReadVisual(
    UCkSelectionGym_SourceSession* Session,
    const FCkSelectionGym_SourceVisualReceipt& Visual,
    FCkSelectionGym_VisualRow& OutRow, FString& OutReason)
{
    OutRow = {};
    OutReason.Reset();
    if (!Current(Session) || !Session->Staged || Session->Failed ||
        Session->Retiring || !ValidVisual(Session, Visual) ||
        !Session->Visuals.ContainsByPredicate(
            [&](const auto& R) { return R.VisualKey == Visual.VisualKey &&
                R.Actor == Visual.Actor && R.Mesh == Visual.Mesh &&
                R.Text == Visual.Text && R.Node == Visual.Node &&
                R.Kind == Visual.Kind; }))
    { return Fail(OutReason, TEXT("Current exact visual receipt unavailable")); }
    int64 NodeKey = 0;
    if (Visual.Kind == ECkSelectionGym_VisualKind::LabelMarker &&
        !ck_selection_gym_census::TryGetOpaqueEntityKey(
            Visual.Node, Session->WorldRoot, NodeKey))
    { return Fail(OutReason, TEXT("Visual node identity unavailable")); }

    FCkSelectionGym_VisualRow Row;
    Row.VisualKey = Visual.VisualKey;
    Row.NodeKey = NodeKey;
    Row.Kind = Visual.Kind;
    Row.ActorPose = Visual.Actor->GetActorTransform();
    Row.MeshRelativePose = Visual.Mesh->GetRelativeTransform();
    Row.Mesh = TSoftObjectPtr<UStaticMesh>(Visual.Mesh->GetStaticMesh().Get());
    Row.Material = TSoftObjectPtr<UMaterialInterface>(Visual.Mesh->GetMaterial(0));
    Row.MeshVisible = Visual.Mesh->IsVisible() && !Visual.Actor->IsHidden();
    Row.ActorCollisionEnabled = Visual.Actor->GetActorEnableCollision();
    Row.MeshCollisionMode = Visual.Mesh->GetCollisionEnabled();
    Row.ObjectChannel = Visual.Mesh->GetCollisionObjectType();
    Row.VisibilityResponse = Visual.Mesh->GetCollisionResponseToChannel(ECC_Visibility);
    if (Visual.Kind == ECkSelectionGym_VisualKind::LabelMarker)
    {
        Row.LabelText = Visual.Text->Text.ToString();
        Row.LabelRelativePose = Visual.Text->GetRelativeTransform();
        Row.LabelWorldSize = Visual.Text->WorldSize;
        Row.LabelColor = Visual.Text->TextRenderColor;
    }
    if (Row.ActorPose.ContainsNaN() || !Row.ActorPose.IsValid() ||
        Row.MeshRelativePose.ContainsNaN() || !Row.MeshRelativePose.IsValid() ||
        Row.LabelRelativePose.ContainsNaN() || !Row.LabelRelativePose.IsValid() ||
        (Visual.Kind == ECkSelectionGym_VisualKind::LabelMarker &&
            (!FMath::IsFinite(Row.LabelWorldSize) || Row.LabelWorldSize <= 0)))
    { return Fail(OutReason, TEXT("Physical visual pose or text size invalid")); }
    OutRow = MoveTemp(Row);
    return true;
}

ECkSelectionGym_SourceOutcome UCkSelectionGym_SourceSession_UE::TrySample(
    UCkSelectionGym_SourceSession* Session,
    FCkSelectionGym_WireSnapshot& OutSnapshot, FString& OutReason)
{
    OutSnapshot = {};
    OutReason.Reset();
    auto Invalid = [&](const TCHAR* Why)
    {
        OutSnapshot = {};
        OutReason = Why;
        FString CloseReason;
        TryFail(Session, OutReason, CloseReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    };
    if (!Current(Session) || !Session->Staged || Session->Failed ||
        Session->Retiring || Session->Replacing || Session->NextSequence <= 0 ||
        Session->NextSequence == MAX_int64)
    { return Invalid(TEXT("Current staged source session unavailable")); }

    TArray<FCk_Handle> ExtraRoots;
    for (int32 I = 1; I < Session->BareRoots.Num(); ++I)
    { ExtraRoots.Add(Session->BareRoots[I]); }
    TArray<FCk_Handle> Census;
    if (!ck_selection_gym_census::TryCensusAllocatedBank(
        Session->World.Get(), Session->BareRoots[0], ExtraRoots,
        Census, OutReason))
    {
        OutSnapshot = {};
        const FString Why = OutReason;
        FString CloseReason;
        TryFail(Session, Why, CloseReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    TMap<FCk_Handle, int64> Keys;
    TSet<FCk_Handle> Authored;
    for (const auto& H : Session->AuthoredNodes)
    {
        if (!Live(H) || Authored.Contains(H))
        { return Invalid(TEXT("Retained authored node lost or duplicated")); }
        Authored.Add(H);
    }
    TSet<FCk_Handle> Whole;
    for (const auto& H : Census) { Whole.Add(H); }
    FCk_Handle ExactIdle;
    if (CheckExactNodes(Session, Authored, Whole, false, ExactIdle,
        OutReason) != ECkSelectionGym_SourceOutcome::Complete)
    { return Invalid(TEXT("Current source descendant membership changed")); }
    for (const auto& Entity : Census)
    {
        int64 Key = 0;
        if (!ck_selection_gym_census::TryGetOpaqueEntityKey(
            Entity, Session->WorldRoot, Key) || Keys.Contains(Entity))
        { return Invalid(TEXT("Live source node identity changed")); }
        Keys.Add(Entity, Key);
    }
    for (const auto& AuthoredNode : Session->AuthoredNodes)
    {
        if (!Keys.Contains(AuthoredNode))
        { return Invalid(TEXT("Retained authored node left source census")); }
    }
    int32 OwnedVisuals = 0;
    for (TActorIterator<AActor> It(Session->World.Get()); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() ||
            Actor->GetOwner() != Session->GameState.Get() ||
            (Actor->GetClass() != Session->MarkerClass.Get() &&
             Actor->GetClass() != Session->WallClass.Get()))
        { continue; }
        ++OwnedVisuals;
        bool Found = false;
        for (const auto& Visual : Session->Visuals)
        { Found |= Visual.Actor.Get() == Actor; }
        if (!Found)
        { return Invalid(TEXT("Unclaimed source visual appeared after staging")); }
    }
    if (OwnedVisuals != Session->Visuals.Num())
    { return Invalid(TEXT("Retained source visual disappeared")); }
    int32 Hidden = 0;
    for (const auto& Visual : Session->Visuals)
    {
        if (!ValidVisual(Session, Visual))
        { return Invalid(TEXT("Retained source visual lost physical receipt")); }
        Hidden += Visual.Actor->IsHidden() ? 1 : 0;
    }
    if (Hidden == Session->Visuals.Num() && !Session->EverComplete)
    {
        OutReason = TEXT("Exact retained visuals await final activation");
        return ECkSelectionGym_SourceOutcome::Pending;
    }
    if (Hidden != 0)
    { return Invalid(TEXT("Partial or regressed physical visual activation")); }

    FCkSelectionGym_WireSnapshot Next;
    Next.SchemaVersion = 1;
    Next.SourceGeneration = Session->SourceGeneration;
    Next.ScenarioGeneration = Session->ScenarioGeneration;
    Next.Sequence = Session->NextSequence;
    Next.Lifecycle = ECkSelectionGym_WireLifecycle::Active;
    Next.Scenario = Session->Scenario;
    Next.ScenarioRootKey = Keys.FindRef(Session->BareRoots[0]);
    Next.SourceOrigin = Session->Origin;

    for (const auto& Entity : Census)
    {
        const int64 Key = Keys.FindRef(Entity);
        if (!Entity.Has<ck::FFragment_LifetimeOwner>())
        { return Invalid(TEXT("Source node lost lifetime owner")); }
        const auto Parent = Entity.Get<ck::FFragment_LifetimeOwner>().Get_Entity();
        FCkSelectionGym_NodeRow Node;
        Node.Key = Key;
        Node.LifetimeParentKey = Parent == Session->WorldRoot ? 0 : Keys.FindRef(Parent);
        if (Parent != Session->WorldRoot && Node.LifetimeParentKey <= 0)
        { return Invalid(TEXT("Source node parent escaped retained forest")); }
        Node.DebugName = Entity.Get_DebugName();
        if (!Entity.Has<ck::FFragment_ContextOwner>())
        { return Invalid(TEXT("Source node lost physical context owner")); }
        const auto Context = Entity.Get<ck::FFragment_ContextOwner>().Get_Entity();
        if (!Live(Context) ||
            Context.Get_RegistryView().Get_RegistryHandle() !=
                Session->WorldRoot.Get_RegistryView().Get_RegistryHandle())
        { return Invalid(TEXT("Source context owner is stale or foreign")); }
        Node.ContextRootKey = Context == Session->WorldRoot ? 0 : Keys.FindRef(Context);
        if (Context != Session->WorldRoot && Node.ContextRootKey <= 0)
        { return Invalid(TEXT("Source context root escaped retained forest")); }
        Node.OwnContextRoot = Context == Entity;
        if (Entity.Has<ck::FFragment_GameplayLabel>())
        { Node.RootLabel = Entity.Get<ck::FFragment_GameplayLabel>().Get_Label(); }
        Node.HasTransform = Entity.Has<ck::FFragment_Transform>();
        Node.HasSceneNode = Entity.Has<ck::FFragment_SceneNode>();
        if (Node.HasSceneNode && !Node.HasTransform)
        { return Invalid(TEXT("SceneNode lost its transform")); }
        if (Node.HasTransform)
        {
            const auto Transform = ck::StaticCast<FCk_Handle_Transform>(Entity);
            Node.WorldPose = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Transform);
            if (Node.WorldPose.ContainsNaN() || !Node.WorldPose.IsValid())
            { return Invalid(TEXT("Current source transform invalid")); }
            if (Node.HasSceneNode)
            {
                if (!Entity.Has<ck::SceneNodeParent>())
                { return Invalid(TEXT("SceneNode lost stored parent")); }
                const FCk_Handle StoredParent =
                    Entity.Get<ck::SceneNodeParent>().Get_Entity();
                if (!Live(StoredParent) ||
                    StoredParent.Get_RegistryView().Get_RegistryHandle() !=
                        Session->WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
                    Keys.FindRef(StoredParent) <= 0 ||
                    !StoredParent.Has<ck::FFragment_Transform>())
                { return Invalid(TEXT("SceneNode stored parent is stale or foreign")); }
                const auto Scene = ck::StaticCast<FCk_Handle_SceneNode>(Entity);
                const FCk_Handle SceneParent = UCk_Utils_SceneNode_UE::Get_Parent(Scene);
                if (SceneParent != StoredParent)
                { return Invalid(TEXT("SceneNode parent disagrees with stored fragment")); }
                Node.SceneNodeParentKey = Keys.FindRef(SceneParent);
                Node.SceneNodeLocalOffset = UCk_Utils_SceneNode_UE::Get_Offset(Scene);
                if (Node.SceneNodeParentKey <= 0 ||
                    Node.SceneNodeLocalOffset.ContainsNaN() ||
                    !Node.SceneNodeLocalOffset.IsValid())
                { return Invalid(TEXT("Current SceneNode parent or offset invalid")); }
            }
        }
        Next.Nodes.Add(MoveTemp(Node));

        if (Entity.Has<ck::FFragment_Timer>())
        {
            if (Parent == Session->WorldRoot)
            { return Invalid(TEXT("Timer lost authored owner")); }
            const auto Timer = ck::StaticCast<FCk_Handle_Timer>(Entity);
            FCkSelectionGym_TimerFact Fact;
            Fact.OwnerNodeKey = Keys.FindRef(Parent);
            Fact.TimerKey = Key;
            Fact.NameTag = UCk_Utils_Timer_UE::Get_Name(Timer);
            Fact.State = UCk_Utils_Timer_UE::Get_CurrentState(Timer);
            if (Fact.OwnerNodeKey <= 0)
            { return Invalid(TEXT("Current timer fact invalid")); }
            Next.Timers.Add(MoveTemp(Fact));
        }
        if (Entity.Has<ck::FFragment_IntegerAttribute>())
        {
            if (!Entity.Has<ck::FFragment_GameplayLabel>() ||
                Parent == Session->WorldRoot)
            { return Invalid(TEXT("Integer attribute lost authored name or owner")); }
            const auto Attribute = ck::StaticCast<FCk_Handle_IntegerAttribute>(Entity);
            if (!UCk_Utils_IntegerAttribute_UE::Has_Component(
                Attribute, ECk_MinMaxCurrent::Current))
            { return Invalid(TEXT("Integer attribute Current component unavailable")); }
            FCkSelectionGym_IntegerFact Fact;
            Fact.OwnerNodeKey = Keys.FindRef(Parent);
            Fact.AttributeKey = Key;
            Fact.NameTag = Entity.Get<ck::FFragment_GameplayLabel>().Get_Label();
            Fact.BaseValue = UCk_Utils_IntegerAttribute_UE::Get_BaseValue(
                Attribute, ECk_MinMaxCurrent::Current);
            Fact.FinalValue = UCk_Utils_IntegerAttribute_UE::Get_FinalValue(
                Attribute, ECk_MinMaxCurrent::Current);
            if (Fact.OwnerNodeKey <= 0 || !Fact.NameTag.IsValid())
            { return Invalid(TEXT("Current integer attribute fact invalid")); }
            Next.Integers.Add(MoveTemp(Fact));
        }
        if (Entity.Has<ck::FFragment_Sm>())
        {
            if (Parent == Session->WorldRoot)
            { return Invalid(TEXT("State machine lost authored owner")); }
            const auto& Machine = Entity.Get<ck::FFragment_Sm>();
            FCkSelectionGym_StateMachineFact Fact;
            Fact.OwnerNodeKey = Key;
            Fact.StateMachineKey = Key;
            FCkSelectionGym_StateChainFact Link;
            Link.Depth = 0;
            const auto* StateClass = Machine.Get_CurrentStateClass().Get();
            Link.StateClassName = IsValid(StateClass) ? StateClass->GetName() : FString{};
            Link.RunStatus = Machine.Get_RunStatus();
            Fact.Chain.Add(MoveTemp(Link));
            Fact.HasDebugHistory = Entity.Has<ck::FFragment_Sm_Debug>();
            if (Fact.HasDebugHistory)
            {
                for (const auto& Entry :
                    Entity.Get<ck::FFragment_Sm_Debug>().Get_History())
                {
                    FCkSelectionGym_StateTransitionFact History;
                    History.FromStateName = Entry.FromStateName;
                    History.ToStateName = Entry.ToStateName;
                    Fact.History.Add(MoveTemp(History));
                }
            }
            Next.StateMachines.Add(MoveTemp(Fact));
        }
    }

    if (Next.Timers.Num() != (Session->Scenario == 2 ? 1 : 0) ||
        Next.Integers.Num() != (Session->Scenario == 2 ? 1 : 0) ||
        Next.StateMachines.Num() != (Session->Scenario == 2 ? 1 : 0))
    { return Invalid(TEXT("Authored provider fact count changed")); }
    for (const auto& Visual : Session->Visuals)
    {
        FCkSelectionGym_VisualRow Row;
        FString Why;
        if (!TryReadVisual(Session, Visual, Row, Why))
        { return Invalid(TEXT("Retained physical visual changed")); }
        if (Session->Scenario == 4 &&
            Visual.Kind == ECkSelectionGym_VisualKind::LabelMarker)
        {
            const auto* Node = Next.Nodes.FindByPredicate(
                [&](const FCkSelectionGym_NodeRow& Candidate)
                { return Candidate.Key == Row.NodeKey; });
            if (Node == nullptr || !Node->HasTransform ||
                !Node->WorldPose.GetLocation().Equals(
                    Row.ActorPose.GetLocation(), 0.01))
            { return Invalid(TEXT("Moving actor and ECS source pose are not settled together")); }
        }
        Next.Visuals.Add(MoveTemp(Row));
    }
    Next.DeclaredNodeCount = Next.Nodes.Num();
    Next.DeclaredVisualCount = Next.Visuals.Num();
    Next.DeclaredTimerCount = Next.Timers.Num();
    Next.DeclaredIntegerCount = Next.Integers.Num();
    Next.DeclaredStateMachineCount = Next.StateMachines.Num();
    if (!ck_selection_gym_wire::TryValidateActiveValue(Next, OutReason))
    {
        const FString Why = OutReason;
        OutSnapshot = {};
        FString CloseReason;
        TryFail(Session, Why, CloseReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    if (!Current(Session))
    { return Invalid(TEXT("Source context changed during sample")); }
    if (!UCk_DebugOverlay_SourceAdmission_UE::TryPublish(
        Session->World.Get(), Session->WorldRoot,
        Session->AdmissionGeneration, Census, OutReason))
    {
        const FString Why = OutReason;
        OutSnapshot = {};
        FString CloseReason;
        TryFail(Session, Why, CloseReason);
        return ECkSelectionGym_SourceOutcome::Invalid;
    }
    ++Session->NextSequence;
    Session->EverComplete = true;
    OutSnapshot = MoveTemp(Next);
    return ECkSelectionGym_SourceOutcome::Complete;
}

bool UCkSelectionGym_SourceSession_UE::TryMakeLifecycleEnvelope(
    UCkSelectionGym_SourceSession* Session,
    ECkSelectionGym_WireLifecycle Lifecycle,
    FCkSelectionGym_WireSnapshot& OutSnapshot, FString& OutReason)
{
    OutSnapshot = {};
    OutReason.Reset();
    if (!IsValid(Session) || !IsInGameThread() ||
        Session->NextSequence <= 0 || Session->NextSequence == MAX_int64 ||
        Session->SourceGeneration <= 0 || Session->ScenarioGeneration <= 0 ||
        Session->Scenario < 0 || Session->Scenario > 4 ||
        !FMath::IsFinite(Session->Origin.X) ||
        !FMath::IsFinite(Session->Origin.Y) ||
        !FMath::IsFinite(Session->Origin.Z))
    { return Fail(OutReason, TEXT("Lifecycle source identity unavailable")); }
    if (Lifecycle == ECkSelectionGym_WireLifecycle::Retiring)
    {
        if (!Current(Session) || !Session->Retiring || Session->Failed)
        { return Fail(OutReason, TEXT("Exact retiring source unavailable")); }
    }
    else if (Lifecycle == ECkSelectionGym_WireLifecycle::Failed)
    {
        auto* World = Session->World.Get();
        if (!Session->Failed || Session->TerminalReason.IsEmpty() ||
            !IsValid(World) || World->bIsTearingDown ||
            World->IsBeingCleanedUp() ||
            !ExactRoot(World, Session->WorldRoot))
        { return Fail(OutReason, TEXT("Exact failed source unavailable")); }
    }
    else
    { return Fail(OutReason, TEXT("Only Retiring or Failed envelope is supported")); }

    FCkSelectionGym_WireSnapshot Next;
    Next.SchemaVersion = 1;
    Next.SourceGeneration = Session->SourceGeneration;
    Next.ScenarioGeneration = Session->ScenarioGeneration;
    Next.Sequence = Session->NextSequence++;
    Next.Lifecycle = Lifecycle;
    Next.FailureReason = Lifecycle == ECkSelectionGym_WireLifecycle::Failed ?
        Session->TerminalReason : FString{};
    Next.Scenario = Session->Scenario;
    Next.SourceOrigin = Session->Origin;
    Next.DeclaredNodeCount = 0;
    Next.DeclaredVisualCount = 0;
    Next.DeclaredTimerCount = 0;
    Next.DeclaredIntegerCount = 0;
    Next.DeclaredStateMachineCount = 0;
    OutSnapshot = MoveTemp(Next);
    return true;
}

bool UCkSelectionGym_SourceSession_UE::TryFail(
    UCkSelectionGym_SourceSession* Session,
    const FString& Reason, FString& OutReason)
{
    OutReason.Reset();
    if (!IsValid(Session))
    { return Fail(OutReason, TEXT("Source session unavailable")); }
    if (Session->TerminalReason.IsEmpty())
    {
        Session->TerminalReason = Reason.IsEmpty() ?
            TEXT("Selection authority source failed") : Reason;
    }
    Session->Failed = true;
    Session->Retiring = true;
    // Close provider discovery before physical visibility/collision setters.
    // An expired GS need not prevent closing an otherwise exact live world
    // scope; actor mutation below still requires exact retained owner identity.
    auto* World = Session->World.Get();
    auto WorldSafe = [&]() -> bool
    {
        return IsInGameThread() && IsValid(World) &&
            !World->HasAnyFlags(RF_ClassDefaultObject | RF_BeginDestroyed | RF_FinishDestroyed) &&
            World->IsGameWorld() && !World->bIsTearingDown &&
            !World->IsBeingCleanedUp() && ExactRoot(World, Session->WorldRoot);
    };
    FString CloseReason;
    const bool Closed = WorldSafe() &&
        UCk_DebugOverlay_SourceAdmission_UE::TryClose(
            World, Session->WorldRoot,
            Session->AdmissionGeneration, CloseReason);
    bool AllOwned = true;
    for (const auto& Visual : Session->Visuals)
    {
        auto* Actor = Visual.Actor.Get();
        if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) { continue; }
        auto ExactActor = [&]() -> bool
        {
            if (!WorldSafe() || !IsValid(Actor) ||
                Actor->IsActorBeingDestroyed() || Actor->GetWorld() != World ||
                Actor->GetClass() !=
                    (Visual.Kind == ECkSelectionGym_VisualKind::OcclusionWall ?
                        Session->WallClass.Get() : Session->MarkerClass.Get()))
            { return false; }
            auto* Owner = Actor->GetOwner();
            if (Owner == nullptr) { return false; }
            // A pending-kill captured GS can have Get()==nullptr while its
            // weak object index/serial still identifies the exact owner.
            const TWeakObjectPtr<AActor> OwnerIdentity(Owner);
            return OwnerIdentity.HasSameIndexAndSerialNumber(Session->GameState);
        };
        if (!ExactActor()) { AllOwned = false; continue; }
        Actor->SetActorHiddenInGame(true);
        if (!ExactActor()) { AllOwned = false; continue; }
        Actor->SetActorEnableCollision(false);
    }
    if (!Closed || !AllOwned)
    {
        OutReason = FString::Printf(
            TEXT("%s; terminal source closure incomplete: %s"),
            *Reason, *CloseReason);
        return false;
    }
    return true;
}

bool UCkSelectionGym_SourceSession_UE::TryStartRetirement(
    UCkSelectionGym_SourceSession* Session, FString& OutReason)
{
    OutReason.Reset();
    auto Reject = [&](const TCHAR* Why)
    {
        OutReason = Why;
        FString Ignored;
        TryFail(Session, OutReason, Ignored);
        return false;
    };
    if (!Current(Session) || !Session->Staged || !Session->EverComplete ||
        Session->Failed || Session->Retiring || Session->Replacing ||
        !ExactOwnedVisualCensus(Session))
    { return Reject(TEXT("Exact active source unavailable for retirement")); }
    TArray<FCk_Handle> ExtraRoots;
    for (int32 I = 1; I < Session->BareRoots.Num(); ++I)
    { ExtraRoots.Add(Session->BareRoots[I]); }
    TArray<FCk_Handle> Frozen;
    if (!ck_selection_gym_census::TryCensusAllocatedBank(
        Session->World.Get(), Session->BareRoots[0], ExtraRoots,
        Frozen, OutReason))
    {
        const FString Why = OutReason;
        FString Ignored;
        TryFail(Session, Why, Ignored);
        return false;
    }
    TSet<FCk_Handle> FrozenSet;
    for (const auto& H : Frozen) { FrozenSet.Add(H); }
    for (const auto& Authored : Session->AuthoredNodes)
    {
        if (!FrozenSet.Contains(Authored))
        { return Reject(TEXT("Authored source receipt lost before retirement")); }
    }
    if (!UCk_DebugOverlay_SourceAdmission_UE::TryClose(
        Session->World.Get(), Session->WorldRoot,
        Session->AdmissionGeneration, OutReason))
    {
        const FString Why = OutReason;
        FString Ignored;
        TryFail(Session, Why, Ignored);
        return false;
    }
    Session->FrozenRetirement = MoveTemp(Frozen);
    Session->Retiring = true;
    auto AbortClosed = [&](const TCHAR* Why)
    {
        if (Session->TerminalReason.IsEmpty())
        { Session->TerminalReason = Why; }
        Session->Failed = true;
        return Fail(OutReason, Why);
    };
    if (!ProveFrozenRetirement(Session))
    { return AbortClosed(TEXT("Frozen source retirement census invalid")); }
    for (const auto& Visual : Session->Visuals)
    {
        if (!ProveFrozenRetirement(Session) || !ValidVisual(Session, Visual))
        { return AbortClosed(TEXT("Visual changed while closing source")); }
        Visual.Actor->SetActorHiddenInGame(true);
        if (!ProveFrozenRetirement(Session))
        { return AbortClosed(TEXT("Visual callback changed frozen source")); }
        Visual.Actor->SetActorEnableCollision(false);
        if (!ProveFrozenRetirement(Session))
        { return AbortClosed(TEXT("Collision callback changed frozen source")); }
    }
    return true;
}

ECkSelectionGym_SourceOutcome
UCkSelectionGym_SourceSession_UE::TryAdvanceRetirement(
    UCkSelectionGym_SourceSession* Session, FString& OutReason)
{
    OutReason.Reset();
    auto Invalid = [&](const TCHAR* Why)
    {
        OutReason = Why;
        FString Ignored;
        TryFail(Session, OutReason, Ignored);
        return ECkSelectionGym_SourceOutcome::Invalid;
    };
    if (!Current(Session) || !Session->Retiring || Session->RespawnMode ||
        Session->Failed ||
        !ProveFrozenRetirement(Session))
    { return Invalid(TEXT("Frozen source retirement provenance lost")); }

    // Retire one proven actor per poll, then reprove the frozen ECS forest.
    for (const auto& Visual : Session->Visuals)
    {
        auto* Actor = Visual.Actor.Get();
        if (!IsValid(Actor)) { continue; }
        if (Actor->IsActorBeingDestroyed())
        { return ECkSelectionGym_SourceOutcome::Pending; }
        if (!ValidVisual(Session, Visual) || !Actor->IsHidden() ||
            Actor->GetActorEnableCollision())
        { return Invalid(TEXT("Retained visual changed before retirement")); }
        if (!Actor->Destroy())
        { return Invalid(TEXT("Exact source visual destruction was rejected")); }
        if (!ProveFrozenRetirement(Session))
        { return Invalid(TEXT("Visual retirement callback changed frozen source")); }
        return ECkSelectionGym_SourceOutcome::Pending;
    }
    // Actor objects must have left the live world before any ECS mutation.
    for (const auto& Visual : Session->Visuals)
    {
        if (IsValid(Visual.Actor.Get()))
        { return ECkSelectionGym_SourceOutcome::Pending; }
    }
    for (const auto& H : Session->FrozenRetirement)
    {
        if (!Allocated(H)) { continue; }
        if (!Live(H)) { continue; } // wait for physical deallocation
        TArray<FCk_Handle> Children;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryGet_AllocatedDirectLifetimeDependents(H, Session->WorldRoot, Children))
        { return Invalid(TEXT("Frozen leaf children unreadable")); }
        if (!Children.IsEmpty()) { continue; }
        auto Mutable = H;
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Mutable);
        if (!ProveFrozenRetirement(Session))
        { return Invalid(TEXT("Leaf retirement callback changed frozen source")); }
        return ECkSelectionGym_SourceOutcome::Pending;
    }
    for (const auto& H : Session->FrozenRetirement)
    {
        if (Allocated(H))
        { return ECkSelectionGym_SourceOutcome::Pending; }
    }
    return ECkSelectionGym_SourceOutcome::Complete;
}

bool UCkSelectionGym_SourceSession_UE::TryStartRespawnRetirement(
    UCkSelectionGym_SourceSession* Session, const FCk_Handle& OldNode,
    int64 OldVisualKey, FString& OutReason)
{
    OutReason.Reset();
    auto Reject = [&](const TCHAR* Why)
    {
        OutReason = Why;
        FString Ignored;
        TryFail(Session, OutReason, Ignored);
        return false;
    };
    if (!Current(Session) || Session->Scenario != 4 || !Session->Staged ||
        !Session->EverComplete || Session->Failed || Session->Retiring ||
        Session->Replacing || !Live(OldNode) ||
        !Direct(OldNode, Session->BareRoots[0]) ||
        !Session->AuthoredNodes.Contains(OldNode) ||
        !ExactOwnedVisualCensus(Session))
    { return Reject(TEXT("Exact active respawn candidate unavailable")); }
    int32 OldVisualIndex = INDEX_NONE;
    for (int32 I = 0; I < Session->Visuals.Num(); ++I)
    {
        if (Session->Visuals[I].VisualKey == OldVisualKey &&
            Session->Visuals[I].Node == OldNode &&
            Session->Visuals[I].Kind == ECkSelectionGym_VisualKind::LabelMarker)
        { OldVisualIndex = I; break; }
    }
    if (OldVisualIndex == INDEX_NONE)
    { return Reject(TEXT("Respawn marker receipt does not match old node")); }
    TArray<FCk_Handle> Children;
    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
        TryGet_AllocatedDirectLifetimeDependents(
            OldNode, Session->WorldRoot, Children) || !Children.IsEmpty())
    { return Reject(TEXT("Respawn candidate is not an exact leaf")); }
    TArray<FCk_Handle> Frozen;
    if (!ck_selection_gym_census::TryCensusAllocatedBank(
        Session->World.Get(), Session->BareRoots[0], {}, Frozen, OutReason))
    {
        const FString Why = OutReason;
        FString Ignored;
        TryFail(Session, Why, Ignored);
        return false;
    }
    if (!UCk_DebugOverlay_SourceAdmission_UE::TryClose(
        Session->World.Get(), Session->WorldRoot,
        Session->AdmissionGeneration, OutReason))
    {
        const FString Why = OutReason;
        FString Ignored;
        TryFail(Session, Why, Ignored);
        return false;
    }
    auto& OldVisual = Session->Visuals[OldVisualIndex];
    Session->FrozenRetirement = MoveTemp(Frozen);
    Session->OldRespawnNode = OldNode;
    Session->OldRespawnVisualKey = OldVisualKey;
    Session->Retiring = true;
    Session->RespawnMode = true;
    auto AbortClosed = [&](const TCHAR* Why)
    {
        if (Session->TerminalReason.IsEmpty())
        { Session->TerminalReason = Why; }
        Session->Failed = true;
        return Fail(OutReason, Why);
    };
    if (!ProveFrozenRetirement(Session))
    { return AbortClosed(TEXT("Frozen respawn retirement census invalid")); }
    if (!ValidVisual(Session, OldVisual))
    { return AbortClosed(TEXT("Respawn visual changed before retirement")); }
    OldVisual.Actor->SetActorHiddenInGame(true);
    if (!ProveFrozenRetirement(Session))
    { return AbortClosed(TEXT("Respawn visibility callback changed frozen source")); }
    OldVisual.Actor->SetActorEnableCollision(false);
    if (!ProveFrozenRetirement(Session))
    { return AbortClosed(TEXT("Respawn collision callback changed frozen source")); }
    return true;
}

ECkSelectionGym_SourceOutcome
UCkSelectionGym_SourceSession_UE::TryAdvanceRespawnRetirement(
    UCkSelectionGym_SourceSession* Session, FString& OutReason)
{
    OutReason.Reset();
    auto Invalid = [&](const TCHAR* Why)
    {
        OutReason = Why;
        FString Ignored;
        TryFail(Session, OutReason, Ignored);
        return ECkSelectionGym_SourceOutcome::Invalid;
    };
    if (!Current(Session) || !Session->Retiring || !Session->RespawnMode ||
        Session->Failed || !ProveFrozenRetirement(Session))
    { return Invalid(TEXT("Frozen respawn provenance lost")); }
    FCkSelectionGym_SourceVisualReceipt* OldVisual = nullptr;
    for (auto& Visual : Session->Visuals)
    {
        if (Visual.VisualKey == Session->OldRespawnVisualKey)
        { OldVisual = &Visual; break; }
    }
    if (OldVisual == nullptr || OldVisual->Node != Session->OldRespawnNode)
    { return Invalid(TEXT("Frozen respawn marker receipt changed")); }
    auto* Actor = OldVisual->Actor.Get();
    if (IsValid(Actor))
    {
        if (Actor->IsActorBeingDestroyed())
        { return ECkSelectionGym_SourceOutcome::Pending; }
        if (!ValidVisual(Session, *OldVisual) || !Actor->IsHidden() ||
            Actor->GetActorEnableCollision())
        { return Invalid(TEXT("Old respawn visual changed before destruction")); }
        if (!Actor->Destroy())
        { return Invalid(TEXT("Old respawn visual destruction rejected")); }
        if (!ProveFrozenRetirement(Session))
        { return Invalid(TEXT("Respawn visual callback changed frozen union")); }
        return ECkSelectionGym_SourceOutcome::Pending;
    }
    if (Allocated(Session->OldRespawnNode))
    {
        if (!Live(Session->OldRespawnNode))
        { return ECkSelectionGym_SourceOutcome::Pending; }
        TArray<FCk_Handle> Children;
        if (!Direct(Session->OldRespawnNode, Session->BareRoots[0]) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::
                TryGet_AllocatedDirectLifetimeDependents(
                    Session->OldRespawnNode, Session->WorldRoot, Children) ||
            !Children.IsEmpty())
        { return Invalid(TEXT("Old respawn candidate is no longer an exact leaf")); }
        auto Mutable = Session->OldRespawnNode;
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Mutable);
        if (!ProveFrozenRetirement(Session))
        { return Invalid(TEXT("Respawn leaf callback changed frozen union")); }
        return ECkSelectionGym_SourceOutcome::Pending;
    }
    // Old actor and entity are physically retired. No replacement is created
    // by this observer; caller gets one new creator attempt and must install
    // its first-return receipts before any further source sample.
    Session->FrozenRetirement.Reset();
    Session->Retiring = false;
    Session->RespawnMode = false;
    Session->Replacing = true;
    return ECkSelectionGym_SourceOutcome::Complete;
}

bool UCkSelectionGym_SourceSession_UE::TryInstallRespawnReplacement(
    UCkSelectionGym_SourceSession* Session, const FCk_Handle& NewNode,
    const FCkSelectionGym_SourceVisualReceipt& NewVisual,
    FString& OutReason)
{
    OutReason.Reset();
    auto Reject = [&](const TCHAR* Why)
    {
        OutReason = Why;
        // The rejected replacement actor is not an accepted source receipt.
        // Its creator retains it and owns any exact-owner cleanup.
        FString Ignored;
        TryFail(Session, OutReason, Ignored);
        return false;
    };
    if (!Current(Session) || Session->Failed || !Session->Replacing ||
        Session->Retiring || Session->Scenario != 4 ||
        Allocated(Session->OldRespawnNode) ||
        !Direct(NewNode, Session->BareRoots[0]) ||
        NewNode == Session->OldRespawnNode ||
        NewVisual.Node != NewNode ||
        NewVisual.Kind != ECkSelectionGym_VisualKind::LabelMarker ||
        NewVisual.VisualKey <= Session->OldRespawnVisualKey ||
        !ValidVisual(Session, NewVisual) ||
        !NewVisual.Actor->IsHidden() ||
        NewVisual.Actor->GetActorEnableCollision())
    { return Reject(TEXT("New respawn first-return receipt invalid")); }
    int32 OldIndex = INDEX_NONE;
    for (int32 I = 0; I < Session->Visuals.Num(); ++I)
    {
        if (Session->Visuals[I].VisualKey == Session->OldRespawnVisualKey)
        { OldIndex = I; }
        else if (Session->Visuals[I].VisualKey == NewVisual.VisualKey ||
            Session->Visuals[I].Actor == NewVisual.Actor)
        { return Reject(TEXT("Respawn visual identity reused")); }
    }
    if (OldIndex == INDEX_NONE ||
        !Session->AuthoredNodes.Contains(Session->OldRespawnNode) ||
        Session->AuthoredNodes.Contains(NewNode))
    { return Reject(TEXT("Old respawn authored receipt missing")); }
    const int32 NodeIndex = Session->AuthoredNodes.Find(Session->OldRespawnNode);
    Session->AuthoredNodes[NodeIndex] = NewNode;
    Session->Visuals[OldIndex] = NewVisual;
    Session->Replacing = false;
    // The caller immediately enables the exact staged visual and calls
    // TrySample; its full census, actor census, value read and TryPublish
    // either publish whole next-Q or close this session terminally.
    return true;
}

#else

// CkTests' overlay dependency is intentionally absent in Shipping. Keep the
// reflected API total without linking the editor/debugger source gate.
namespace
{
    auto Unavailable(FString& Why) -> bool
    { Why = TEXT("Selection gym source unavailable in Shipping"); return false; }
}

bool UCkSelectionGym_SourceSession_UE::TryBegin(UWorld*, AGameStateBase*,
    const FCk_Handle&, int64, int64, int64, int32, FVector,
    const TArray<FCk_Handle>&, TSubclassOf<AActor>, TSubclassOf<AActor>,
    TSubclassOf<UCk_SmState_EntityScript>,
    UCkSelectionGym_SourceSession*& Out, FString& Why)
{ Out = nullptr; return Unavailable(Why); }

bool UCkSelectionGym_SourceSession_UE::TryRetainVisual(
    UCkSelectionGym_SourceSession*, const FCkSelectionGym_SourceVisualReceipt&,
    FString& Why)
{ return Unavailable(Why); }

bool UCkSelectionGym_SourceSession_UE::TryStage(
    UCkSelectionGym_SourceSession*, const TArray<FCk_Handle>&, FString& Why)
{ return Unavailable(Why); }

ECkSelectionGym_SourceOutcome
UCkSelectionGym_SourceSession_UE::TryCheckStageReadiness(
    UCkSelectionGym_SourceSession*, const TArray<FCk_Handle>&, FString& Why)
{ Unavailable(Why); return ECkSelectionGym_SourceOutcome::Invalid; }

bool UCkSelectionGym_SourceSession_UE::TryReadVisual(
    UCkSelectionGym_SourceSession*, const FCkSelectionGym_SourceVisualReceipt&,
    FCkSelectionGym_VisualRow& Out, FString& Why)
{ Out = {}; return Unavailable(Why); }

ECkSelectionGym_SourceOutcome UCkSelectionGym_SourceSession_UE::TrySample(
    UCkSelectionGym_SourceSession*, FCkSelectionGym_WireSnapshot& Out,
    FString& Why)
{
    Out = {};
    Unavailable(Why);
    return ECkSelectionGym_SourceOutcome::Invalid;
}

bool UCkSelectionGym_SourceSession_UE::TryMakeLifecycleEnvelope(
    UCkSelectionGym_SourceSession*, ECkSelectionGym_WireLifecycle,
    FCkSelectionGym_WireSnapshot& Out, FString& Why)
{ Out = {}; return Unavailable(Why); }

bool UCkSelectionGym_SourceSession_UE::TryFail(
    UCkSelectionGym_SourceSession*, const FString&, FString& Why)
{ return Unavailable(Why); }

bool UCkSelectionGym_SourceSession_UE::TryStartRetirement(
    UCkSelectionGym_SourceSession*, FString& Why)
{ return Unavailable(Why); }

ECkSelectionGym_SourceOutcome
UCkSelectionGym_SourceSession_UE::TryAdvanceRetirement(
    UCkSelectionGym_SourceSession*, FString& Why)
{ Unavailable(Why); return ECkSelectionGym_SourceOutcome::Invalid; }

bool UCkSelectionGym_SourceSession_UE::TryStartRespawnRetirement(
    UCkSelectionGym_SourceSession*, const FCk_Handle&, int64, FString& Why)
{ return Unavailable(Why); }

ECkSelectionGym_SourceOutcome
UCkSelectionGym_SourceSession_UE::TryAdvanceRespawnRetirement(
    UCkSelectionGym_SourceSession*, FString& Why)
{ Unavailable(Why); return ECkSelectionGym_SourceOutcome::Invalid; }

bool UCkSelectionGym_SourceSession_UE::TryInstallRespawnReplacement(
    UCkSelectionGym_SourceSession*, const FCk_Handle&,
    const FCkSelectionGym_SourceVisualReceipt&, FString& Why)
{ return Unavailable(Why); }

#endif
