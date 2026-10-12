#include "CkCrowdGym_NetworkRuntime.h"
#include "CkCrowdRemoteBodyProjector.h"
#include "CkGym_Registry.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"

#include <Engine/GameInstance.h>
#include <Engine/LocalPlayer.h>
#include <Engine/World.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <Net/UnrealNetwork.h>
#include <EngineUtils.h>
#include <UObject/UnrealType.h>

namespace ck_crowd_gym_transport
{
    auto IsSerial(int64 Value) -> bool
    { return Value > 0 && Value < TNumericLimits<int64>::Max(); }

    auto IsDefaultId(const FCkCrowdRemoteAgentId& Id) -> bool
    { return Id.WorldGeneration == 0 && Id.AgentSerial == 0; }

    auto IsLocalContext(const UWorld& World, const APlayerController& PC,
        const ULocalPlayer& LocalPlayer) -> bool
    {
        const auto* GI = World.GetGameInstance();
        return !World.HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
            World.IsGameWorld() && PC.GetWorld() == &World && PC.IsLocalController() &&
            PC.GetLocalPlayer() == &LocalPlayer && IsValid(GI) &&
            GI->GetWorld() == &World && GI->GetLocalPlayers().Contains(&LocalPlayer) &&
            LocalPlayer.GetWorld() == &World &&
            LocalPlayer.GetPlayerController(&World) == &PC;
    }

    auto FindSession(const APlayerController& PC) -> UCkCrowdGym_Session_UE*
    { return PC.FindComponentByClass<UCkCrowdGym_Session_UE>(); }

    auto SameRows(const TArray<FCkCrowdRemoteAgentRow>& A,
        const TArray<FCkCrowdRemoteAgentRow>& B) -> bool
    {
        if (A.Num() != B.Num()) { return false; }
        const auto* Struct = FCkCrowdRemoteAgentRow::StaticStruct();
        for (int32 i = 0; i < A.Num(); ++i)
        { if (!Struct->CompareScriptStruct(&A[i], &B[i], PPF_None)) { return false; } }
        return true;
    }
}

ACkCrowdGym_GameState_UE::ACkCrowdGym_GameState_UE()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SharedSimulation = CreateDefaultSubobject<UCkCrowdGym_SharedSimulation>(TEXT("SharedSimulation"));
}

auto ACkCrowdGym_GameState_UE::HasExactAuthorityContext() const -> bool
{
    auto* World = GetWorld();
    auto* GI = IsValid(World) ? World->GetGameInstance() : nullptr;
    auto* GM = IsValid(World) ? World->GetAuthGameMode() : nullptr;
    auto* Registry = IsValid(GI) ? GI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    if (!HasAuthority() || bTerminal || !Get_HasPublishedCatalog() ||
        !IsValid(World) || !World->IsGameWorld() ||
        World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        !IsValid(GI) || GI->GetWorld() != World ||
        World->GetGameState() != this || !IsValid(GM) || !IsValid(Registry) ||
        !PCClass || !PawnClass) { return false; }
    const int32 Index = Registry->Get_CurrentGymIndex();
    const auto& Entries = Registry->Get_Entries();
    return HasFrozenCatalog() && HasFrozenStation() &&
        Index == CatalogGymIndex && Entries.IsValidIndex(Index) &&
        Entries[Index].DisplayName == TEXT("Crowd Separation") &&
        Entries[Index].GameModeClass == CatalogModeClass &&
        CatalogModeClass.Get() == GM->GetClass() &&
        GM->PlayerControllerClass == PCClass && GM->DefaultPawnClass == PawnClass;
}

auto ACkCrowdGym_GameState_UE::TryInitializeShared(const FCk_Handle& Station,
    const FTransform& FootprintCenter,
    TSubclassOf<APlayerController> ExpectedPCClass, TSubclassOf<APawn> ExpectedPawnClass) -> bool
{
    if (bInitialized || bTerminal || !HasAuthority() || !IsValid(SharedSimulation) ||
        !ExpectedPCClass || !ExpectedPawnClass || FootprintCenter.ContainsNaN())
    { return false; }
    PCClass = ExpectedPCClass;
    PawnClass = ExpectedPawnClass;
    const auto* World = GetWorld();
    const auto* GI = IsValid(World) ? World->GetGameInstance() : nullptr;
    const auto* Registry = IsValid(GI) ? GI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    const auto* GM = IsValid(World) ? World->GetAuthGameMode() : nullptr;
    CatalogGymIndex = IsValid(Registry) ? Registry->Get_CurrentGymIndex() : INDEX_NONE;
    CatalogModeClass = IsValid(GM) ? GM->GetClass() : nullptr;
    FrozenCatalog = IsValid(Registry) ? Registry->Get_Entries() : TArray<FCkGym_Entry>{};
    FrozenStation = Station;
    FrozenStationOwner = ck::IsValid(Station) &&
        UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Station) ?
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Station) : FCk_Handle{};
    FrozenStationTransform = FTransform(FRotator(0.0, 180.0, 0.0), FVector(500.0, 0.0, 0.0));
    FrozenFootprint = FootprintCenter;
    if (!HasExactAuthorityContext() || !SharedSimulation->TryInitialize(FootprintCenter))
    {
        // A reentrant native terminal callback owns its retained identities. Only a pure
        // preallocation rejection may discard this local initialization staging.
        if (!bTerminal)
        { PCClass = nullptr; PawnClass = nullptr; CatalogModeClass = nullptr;
          CatalogGymIndex = INDEX_NONE; FrozenCatalog.Reset(); FrozenStation = {};
          FrozenStationOwner = {}; FrozenFootprint = {}; }
        return false;
    }
    if (bTerminal) { return false; }
    bInitialized = true;
    // SharedSimulation is asynchronous. Tick publishes the first complete empty roster.
    return true;
}

auto ACkCrowdGym_GameState_UE::Get_HasReadyServerSimulation() const -> bool
{
    return bInitialized && HasExactAuthorityContext() &&
        IsValid(SharedSimulation) && SharedSimulation->Get_IsReady() &&
        !SharedSimulation->Get_HasFailed();
}

auto ACkCrowdGym_GameState_UE::Get_HasCompleteSnapshot() const -> bool
{ return FCkCrowdGymWireValidation::IsValidReadySnapshot(AcceptedSnapshot) && !bTerminal; }

auto ACkCrowdGym_GameState_UE::TryRegisterProvider() -> bool
{
    if (ProviderToken.IsValid()) { return true; }
    auto* World = GetWorld();
    return IsValid(World) && World->IsGameWorld() &&
        FCkCrowdRemoteDebugRegistry::TryRegister(*World, *this, ProviderToken);
}

auto ACkCrowdGym_GameState_UE::TryBindLocalController(APlayerController* LocalPC) -> bool
{
    auto* World = GetWorld();
    auto* LP = IsValid(LocalPC) ? LocalPC->GetLocalPlayer() : nullptr;
    if (bTerminal || !IsValid(World) || !IsValid(LP) || !IsValid(LocalPC) ||
        !PCClass ||
        !ck_crowd_gym_transport::IsLocalContext(*World, *LocalPC, *LP) ||
        !LocalPC->IsA(PCClass) || !IsValid(ck_crowd_gym_transport::FindSession(*LocalPC)))
    { return false; }
    if (LocalController.IsValid() && LocalController.Get() != LocalPC)
    {
        if (IsValid(Projector) && !Projector->IsRetired())
        { return TryRegisterProvider(); }
        LocalController.Reset();
    }
    if (World->GetNetMode() == NM_Client &&
        (!IsValid(Projector) || Projector->IsRetired()))
    {
        Projector = NewObject<UCkCrowdRemoteBodyProjector>(this);
        ProjectorGeneration = 0; ProjectorSequence = 0; ProjectedSnapshot = {};
    }
    if (World->GetNetMode() == NM_Client && !LocalController.IsValid() &&
        (!IsValid(Projector) || !Projector->TryBind(World, LocalPC)))
    { return false; }
    if (!TryRegisterProvider()) { return false; }
    LocalController = LocalPC;
    return true;
}

auto ACkCrowdGym_GameState_UE::HasServerRequestContext(
    const UCkCrowdGym_Session_UE& Session) const -> bool
{
    const auto* PC = Cast<APlayerController>(Session.GetOwner());
    const APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
    return HasServerTransportContext(Session) &&
        IsValid(Pawn) && Pawn->GetWorld() == GetWorld() && Pawn->IsA(PawnClass) &&
        Pawn->GetController() == PC;
}

auto ACkCrowdGym_GameState_UE::HasServerTransportContext(
    const UCkCrowdGym_Session_UE& Session) const -> bool
{
    const auto* PC = Cast<APlayerController>(Session.GetOwner());
    return HasExactAuthorityContext() && IsValid(PC) && PC->GetWorld() == GetWorld() &&
        Session.GetWorld() == GetWorld() && PC->HasAuthority() && PC->IsA(PCClass) &&
        ck_crowd_gym_transport::FindSession(*PC) == &Session;
}

auto ACkCrowdGym_GameState_UE::HasFrozenCatalog() const -> bool
{
    const auto* World = GetWorld();
    const auto* GI = IsValid(World) ? World->GetGameInstance() : nullptr;
    const auto* Registry = IsValid(GI) ? GI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    if (!Get_HasPublishedCatalog() || !IsValid(Registry) ||
        Registry->Get_CurrentGymIndex() != CatalogGymIndex) { return false; }
    const auto& Entries = Registry->Get_Entries();
    if (Entries.Num() != FrozenCatalog.Num() || !Entries.IsValidIndex(CatalogGymIndex))
    { return false; }
    const auto* Struct = FCkGym_Entry::StaticStruct();
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        if (!Struct->CompareScriptStruct(&Entries[Index], &FrozenCatalog[Index], PPF_None))
        { return false; }
    }
    return true;
}

auto ACkCrowdGym_GameState_UE::HasFrozenStation() const -> bool
{
    if (!FrozenFootprint.IsValid() || !ck::IsValid(FrozenStation) ||
        !ck::IsValid(FrozenStationOwner)) { return false; }
    if (UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
        FrozenStation, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
        FrozenStationOwner, ECk_EntityLifetime_DestructionPhase::BeginDestroy)) { return false; }
    auto* World = GetWorld();
    if (!IsValid(World)) { return false; }
    const auto Transient = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!ck::IsValid(Transient) ||
        FrozenStation.Get_RegistryView().Get_RegistryHandle() !=
            Transient.Get_RegistryView().Get_RegistryHandle() ||
        !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(FrozenStation) ||
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(FrozenStation) != FrozenStationOwner ||
        !UCk_Utils_Transform_UE::Has(FrozenStation)) { return false; }
    const auto Transform = UCk_Utils_Transform_UE::Cast(FrozenStation);
    return ck::IsValid(Transform) &&
        UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Transform).Equals(FrozenStationTransform);
}

auto ACkCrowdGym_GameState_UE::HasExactSession(const UCkCrowdGym_Session_UE& Session) const -> bool
{ return Get_HasReadyServerSimulation() && HasServerRequestContext(Session); }

auto ACkCrowdGym_GameState_UE::HasLocalRequestContext(
    const UCkCrowdGym_Session_UE& Session) const -> bool
{
    const auto* PC = Cast<APlayerController>(Session.GetOwner());
    const APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
    return HasLocalTransportContext(Session) && IsValid(Pawn) &&
        Pawn->GetWorld() == GetWorld() && Pawn->IsA(PawnClass) && Pawn->GetController() == PC;
}

auto ACkCrowdGym_GameState_UE::HasLocalTransportContext(
    const UCkCrowdGym_Session_UE& Session) const -> bool
{
    const auto* World = GetWorld();
    const auto* GI = IsValid(World) ? World->GetGameInstance() : nullptr;
    const auto* Registry = IsValid(GI) ? GI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    const auto* PC = Cast<APlayerController>(Session.GetOwner());
    const auto* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
    if (bTerminal || !Get_HasPublishedCatalog() || !IsValid(World) ||
        World->GetGameState() != this || !IsValid(Registry) || !IsValid(PC) ||
        !IsValid(LP) || !PCClass || !PawnClass || !CatalogModeClass ||
        !PC->IsA(PCClass) || Session.GetWorld() != World || !FrozenFootprint.IsValid() ||
        ck_crowd_gym_transport::FindSession(*PC) != &Session ||
        !ck_crowd_gym_transport::IsLocalContext(*World, *PC, *LP)) { return false; }
    const int32 Index = Registry->Get_CurrentGymIndex();
    const auto& Entries = Registry->Get_Entries();
    return HasFrozenCatalog() && Index == CatalogGymIndex && Entries.IsValidIndex(Index) &&
        Entries[Index].DisplayName == TEXT("Crowd Separation") &&
        Entries[Index].GameModeClass == CatalogModeClass;
}

auto ACkCrowdGym_GameState_UE::TryPublishEpoch() -> bool
{
    FCkCrowdGym_RowsSnapshot Candidate;
    TMap<TWeakObjectPtr<UCkCrowdGym_Session_UE>, FCkCrowdRemoteAgentDetail> Details;
    TArray<TWeakObjectPtr<UCkCrowdGym_Session_UE>> Withdrawals;
    if (!TryBuildEpoch(Candidate, Details, Withdrawals)) { return false; }
    PublishPreparedEpoch(MoveTemp(Candidate), MoveTemp(Details), Withdrawals);
    return true;
}

auto ACkCrowdGym_GameState_UE::TryBuildEpoch(FCkCrowdGym_RowsSnapshot& OutSnapshot,
    TMap<TWeakObjectPtr<UCkCrowdGym_Session_UE>, FCkCrowdRemoteAgentDetail>& OutDetails,
    TArray<TWeakObjectPtr<UCkCrowdGym_Session_UE>>& OutWithdrawals,
    UCkCrowdGym_Session_UE* OverrideSession, const FCkCrowdRemoteAgentId* OverrideId,
    int64 OverrideSerial) -> bool
{
    OutSnapshot = {}; OutDetails.Reset(); OutWithdrawals.Reset();
    if (!Get_HasReadyServerSimulation() || SharedSimulation->Get_IsBusy() ||
        !ck_crowd_gym_transport::IsSerial(NextSampleSequence)) { return false; }
    const int64 Generation = SharedSimulation->Get_WorldGeneration();
    const int64 Sequence = NextSampleSequence++;
    TArray<FCkCrowdRemoteAgentRow> Baseline;
    FCkCrowdRemoteAgentDetail Unused;
    if (!SharedSimulation->TrySampleRosterAtSequence(Sequence, {}, nullptr, 0, Baseline, Unused))
    { return false; }
    struct FOwnerDetail
    {
        TWeakObjectPtr<UCkCrowdGym_Session_UE> Session;
        FCkCrowdRemoteAgentDetail Detail;
    };
    TArray<FOwnerDetail> Details;
    TArray<TWeakObjectPtr<UCkCrowdGym_Session_UE>> Withdrawals;
    for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
    {
        auto* Session = ck_crowd_gym_transport::FindSession(**It);
        if (!IsValid(Session) || !HasExactSession(*Session)) { continue; }
        const auto Id = Session == OverrideSession && OverrideId != nullptr ?
            *OverrideId : Session->Get_SelectedId();
        const int64 SelectionSerial = Session == OverrideSession ?
            OverrideSerial : Session->Get_SelectionSerial();
        if (ck_crowd_gym_transport::IsDefaultId(Id)) { continue; }
        TArray<FCkCrowdRemoteAgentRow> OwnerRows;
        FCkCrowdRemoteAgentDetail Detail;
        if (!SharedSimulation->TrySampleRosterAtSequence(Sequence, {}, &Id,
                SelectionSerial, OwnerRows, Detail) ||
            !ck_crowd_gym_transport::SameRows(Baseline, OwnerRows))
        {
            // Clear removes an owner's selection; no detail is published for an absent row.
            bool bFound = false;
            for (const auto& Row : Baseline) { if (Row.Id == Id) { bFound = true; break; } }
            if (bFound) { return false; }
            Withdrawals.Add(Session);
            continue;
        }
        if (!FCkCrowdRemoteDebugValidation::IsValid(Detail) ||
            !(Detail.Id == Id) || Detail.SelectionRequestSerial != SelectionSerial ||
            Detail.SampleSequence != Sequence)
        { return false; }
        FOwnerDetail Entry;
        Entry.Session = Session;
        Entry.Detail = MoveTemp(Detail);
        Details.Add(MoveTemp(Entry));
    }
    FCkCrowdGym_RowsSnapshot Candidate;
    Candidate.WorldGeneration = Generation;
    Candidate.SampleSequence = Sequence;
    Candidate.bReady = true;
    Candidate.Rows = MoveTemp(Baseline);
    if (!FCkCrowdGymWireValidation::IsValidReadySnapshot(Candidate)) { return false; }
    TMap<TWeakObjectPtr<UCkCrowdGym_Session_UE>, FCkCrowdRemoteAgentDetail> CandidateDetails;
    for (auto& Entry : Details) { CandidateDetails.Add(Entry.Session, MoveTemp(Entry.Detail)); }
    for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
    {
        auto* Session = ck_crowd_gym_transport::FindSession(**It);
        if (!IsValid(Session) || !HasServerRequestContext(*Session)) { continue; }
        const auto& Id = Session == OverrideSession && OverrideId != nullptr ?
            *OverrideId : Session->Get_SelectedId();
        if (!Withdrawals.Contains(TWeakObjectPtr<UCkCrowdGym_Session_UE>(Session)) &&
            !ck_crowd_gym_transport::IsDefaultId(Id) &&
            !CandidateDetails.Contains(TWeakObjectPtr<UCkCrowdGym_Session_UE>(Session)))
        { return false; }
    }
    if (!TryRegisterProvider()) { return false; }
    OutSnapshot = MoveTemp(Candidate);
    OutDetails = MoveTemp(CandidateDetails);
    OutWithdrawals = MoveTemp(Withdrawals);
    return true;
}

void ACkCrowdGym_GameState_UE::PublishPreparedEpoch(FCkCrowdGym_RowsSnapshot&& Candidate,
    TMap<TWeakObjectPtr<UCkCrowdGym_Session_UE>, FCkCrowdRemoteAgentDetail>&& CandidateDetails,
    const TArray<TWeakObjectPtr<UCkCrowdGym_Session_UE>>& Withdrawals)
{
    // Every fallible prerequisite was checked in TryBuildEpoch; no yield/mutation interleaves.
    SampledDetails = MoveTemp(CandidateDetails);
    for (const auto& Withdrawn : Withdrawals)
    { if (Withdrawn.IsValid()) { Withdrawn->WithdrawSelection(); } }
    Snapshot = Candidate;
    AcceptedSnapshot = MoveTemp(Candidate);
    for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
    {
        auto* Session = ck_crowd_gym_transport::FindSession(**It);
        if (!IsValid(Session) || !HasServerRequestContext(*Session)) { continue; }
        const auto& Id = Session->Get_SelectedId();
        const auto* Detail = ck_crowd_gym_transport::IsDefaultId(Id) ? nullptr :
            SampledDetails.Find(TWeakObjectPtr<UCkCrowdGym_Session_UE>(Session));
        Session->Client_ReceiveView(AcceptedSnapshot, Id, Session->Get_SelectionSerial(),
            Detail != nullptr, Detail != nullptr ? *Detail : FCkCrowdRemoteAgentDetail{},
            Session->Get_TransportSessionGeneration());
    }
    NextSampleAtSeconds = GetWorld()->GetTimeSeconds() + 0.1;
    FirstSampleFailureSeconds = 0.0;
    ForceNetUpdate();
}

auto ACkCrowdGym_GameState_UE::TrySubmitControl(UCkCrowdGym_Session_UE& Session,
    int64 Serial, int32 Row) -> bool
{
    if (!HasExactSession(Session) || PendingBatchSerial != 0 ||
        !Session.CanAdmit(Serial, ECkCrowdGym_RequestKind::Control) ||
        !Get_HasCompleteSnapshot() || Row < 1 || Row > 5) { return false; }
    int64 Batch = 0;
    const bool bAccepted = Row == 5 ? SharedSimulation->BeginClear(Batch) :
        SharedSimulation->BeginAppendScenario(Row - 1, Batch);
    if (!bAccepted || !ck_crowd_gym_transport::IsSerial(Batch))
    {
        if (SharedSimulation->Get_HasFailed()) { FailAndRetire(); }
        return false;
    }
    Session.MarkServerPending(Serial, ECkCrowdGym_RequestKind::Control);
    PendingOwner = &Session;
    auto* PC = Cast<APlayerController>(Session.GetOwner());
    PendingController = PC;
    PendingPawn = PC->GetPawn();
    PendingRequestSerial = Serial;
    PendingSessionGeneration = Session.Get_TransportSessionGeneration();
    PendingBatchSerial = Batch;
    PendingRow = Row;
    return true;
}

auto ACkCrowdGym_GameState_UE::TrySubmitSelection(UCkCrowdGym_Session_UE& Session,
    int64 Serial, const FCkCrowdRemoteAgentId& Id, bool bClear) -> bool
{
    if (!HasExactSession(Session) || PendingBatchSerial != 0 ||
        !Session.CanAdmit(Serial, ECkCrowdGym_RequestKind::Selection) ||
        !Get_HasCompleteSnapshot() ||
        (bClear ? !ck_crowd_gym_transport::IsDefaultId(Id) :
            !Id.IsValid() || Id.WorldGeneration != AcceptedSnapshot.WorldGeneration))
    { return false; }
    if (!bClear)
    {
        bool bFound = false;
        for (const auto& Row : AcceptedSnapshot.Rows) { if (Row.Id == Id) { bFound = true; break; } }
        if (!bFound) { return false; }
    }
    // Stage rows, every owner's detail, and the entire reliable commit before changing selection.
    // All preparation and installation runs in one synchronous game-thread stack.
    const auto Selected = bClear ? FCkCrowdRemoteAgentId{} : Id;
    FCkCrowdGym_RowsSnapshot Candidate;
    TMap<TWeakObjectPtr<UCkCrowdGym_Session_UE>, FCkCrowdRemoteAgentDetail> Details;
    TArray<TWeakObjectPtr<UCkCrowdGym_Session_UE>> Withdrawals;
    if (!TryBuildEpoch(Candidate, Details, Withdrawals, &Session, &Selected, Serial))
    { return false; }
    const FCkCrowdRemoteAgentDetail* DetailPtr = nullptr;
    if (!bClear)
    {
        DetailPtr = Details.Find(TWeakObjectPtr<UCkCrowdGym_Session_UE>(&Session));
        if (DetailPtr == nullptr)
        { return false; }
    }
    FCkCrowdGym_OwnerCommit Commit;
    if (!Session.TryPrepareAccepted(Serial, ECkCrowdGym_RequestKind::Selection,
        Candidate, Selected, Serial, DetailPtr, Commit)) { return false; }
    if (!Session.TryRetainPreparedAccepted(Commit))
    { FailAndRetire(); return false; }
    Session.SetSelection(Selected, Serial);
    PublishPreparedEpoch(MoveTemp(Candidate), MoveTemp(Details), Withdrawals);
    Session.SendRetainedCommit();
    return true;
}

auto ACkCrowdGym_GameState_UE::IsForWorld(const UWorld& World) const -> bool
{ return !bTerminal && GetWorld() == &World && World.IsGameWorld(); }

auto ACkCrowdGym_GameState_UE::HasLocalContext(const UWorld& World,
    const APlayerController& PC, const ULocalPlayer& LocalPlayer) const -> bool
{
    const auto* Session = ck_crowd_gym_transport::FindSession(PC);
    return IsForWorld(World) && PCClass && PC.IsA(PCClass) &&
        ck_crowd_gym_transport::IsLocalContext(World, PC, LocalPlayer) &&
        IsValid(Session) && HasLocalRequestContext(*Session);
}

auto ACkCrowdGym_GameState_UE::CopyRows(const UWorld& World, const APlayerController& PC,
    const ULocalPlayer& LocalPlayer, TArray<FCkCrowdRemoteAgentRow>& Out) const -> bool
{
    Out.Reset();
    if (!HasLocalContext(World, PC, LocalPlayer)) { return false; }
    FCkCrowdGym_OwnerCommit Commit;
    const auto* Session = ck_crowd_gym_transport::FindSession(PC);
    const bool bFrozenCommit = IsValid(Session) && Session->TryCopyRetainedCommit(Commit);
    FCkCrowdGym_RowsSnapshot OwnerViewRows;
    FCkCrowdRemoteAgentId OwnerViewId;
    int64 OwnerViewSerial = 0;
    FCkCrowdRemoteAgentDetail OwnerViewDetail;
    bool bOwnerViewDetail = false;
    const bool bOwnerView = IsValid(Session) && Session->TryCopyLatestView(
        OwnerViewRows, OwnerViewId, OwnerViewSerial, OwnerViewDetail, bOwnerViewDetail);
    const auto& Source = bFrozenCommit ? Commit.Snapshot :
        (bOwnerView ? OwnerViewRows :
         (World.GetNetMode() == NM_Client ? ProjectedSnapshot : AcceptedSnapshot));
    if (!FCkCrowdGymWireValidation::IsValidReadySnapshot(Source)) { return false; }
    if (World.GetNetMode() == NM_Client &&
        (!IsValid(Projector) || Projector->HasFailed() ||
         (!bFrozenCommit && !bOwnerView &&
          !Projector->IsReadyFor(Source.WorldGeneration, Source.SampleSequence))))
    { return false; }
    Out = Source.Rows;
    return true;
}

auto ACkCrowdGym_GameState_UE::CopySelectedDetail(const UWorld& World,
    const APlayerController& PC, const ULocalPlayer& LocalPlayer,
    const FCkCrowdRemoteAgentId& Id, int64 SelectionSerial,
    FCkCrowdRemoteAgentDetail& Out) const -> bool
{
    Out = {};
    if (!HasLocalContext(World, PC, LocalPlayer) || !Id.IsValid() ||
        !ck_crowd_gym_transport::IsSerial(SelectionSerial)) { return false; }
    FCkCrowdGym_OwnerCommit Commit;
    const auto* Session = ck_crowd_gym_transport::FindSession(PC);
    if (!IsValid(Session)) { return false; }
    if (Session->TryCopyRetainedCommit(Commit))
    {
        if (Commit.Outcome != ECkCrowdGym_RequestOutcome::Accepted ||
            Commit.SelectionRequestSerial != SelectionSerial ||
            !(Commit.SelectedId == Id) || !Commit.bHasDetail) { return false; }
        Out = Commit.Detail; return true;
    }
    FCkCrowdGym_RowsSnapshot ViewRows;
    FCkCrowdRemoteAgentId ViewId;
    int64 ViewSerial = 0;
    FCkCrowdRemoteAgentDetail ViewDetail;
    bool bHasDetail = false;
    if (!Session->TryCopyLatestView(ViewRows, ViewId, ViewSerial, ViewDetail, bHasDetail) ||
        !bHasDetail || ViewSerial != SelectionSerial || !(ViewId == Id)) { return false; }
    Out = ViewDetail;
    return true;
}

auto ACkCrowdGym_GameState_UE::RequestSelectionFromOwningLocalController(
    const UWorld& World, APlayerController& PC, const ULocalPlayer& LocalPlayer,
    const FCkCrowdRemoteAgentId& Id, int64 Serial, bool bClear) -> bool
{
    auto* Session = ck_crowd_gym_transport::FindSession(PC);
    return HasLocalContext(World, PC, LocalPlayer) && IsValid(Session) &&
        Session->TryQueueSelection(Id, Serial, bClear);
}


auto ACkCrowdGym_GameState_UE::CopySelectionEnvelope(const UWorld& World,
    const APlayerController& PC, const ULocalPlayer& LocalPlayer,
    FCkCrowdRemoteSelectionEnvelope& Out) const -> bool
{
    Out = {};
    const auto* Session = ck_crowd_gym_transport::FindSession(PC);
    if (!IsForWorld(World) || !ck_crowd_gym_transport::IsLocalContext(World, PC, LocalPlayer) ||
        !IsValid(Session) || !HasLocalTransportContext(*Session)) { return false; }
    return Session->TryCopySelectionEnvelope(Out);
}

auto ACkCrowdGym_GameState_UE::AcknowledgeSelectionFromOwningLocalController(const UWorld& World,
    APlayerController& PC, const ULocalPlayer& LocalPlayer,
    const FCkCrowdRemoteSelectionReceipt& Receipt) -> bool
{
    auto* Session = ck_crowd_gym_transport::FindSession(PC);
    return IsForWorld(World) && ck_crowd_gym_transport::IsLocalContext(World, PC, LocalPlayer) &&
        IsValid(Session) && HasLocalTransportContext(*Session) && Session->TryAcknowledgeSelection(Receipt);
}

auto ACkCrowdGym_GameState_UE::RequestNextSelectionFromOwningLocalController(const UWorld& World,
    APlayerController& PC, const ULocalPlayer& LocalPlayer, const FCkCrowdRemoteAgentId& Id,
    bool bClear, int64& OutIssuedSerial) -> bool
{
    OutIssuedSerial = 0;
    auto* Session = ck_crowd_gym_transport::FindSession(PC);
    return HasLocalContext(World, PC, LocalPlayer) && IsValid(Session) &&
        Session->TryQueueNextSelection(Id, bClear, OutIssuedSerial);
}

auto ACkCrowdGym_GameState_UE::SubmitFromOwningLocalController(const UWorld&,
    APlayerController&, const ULocalPlayer&, const FCkCrowdRemoteCommandRequest&) -> bool
{ return false; }

auto ACkCrowdGym_GameState_UE::CopyCommandResult(const UWorld&, const APlayerController&,
    const ULocalPlayer&, int64, FCkCrowdRemoteCommandResult& Out) const -> bool
{ Out = {}; return false; }

void ACkCrowdGym_GameState_UE::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bTerminal)
    { if (IsValid(Projector)) { Projector->TickProjection(); } return; }
    auto* World = GetWorld();
    if (!IsValid(World)) { FailAndRetire(); return; }
    if (World->GetNetMode() == NM_Client)
    {
        if (!LocalController.IsValid() && IsValid(Projector))
        { Projector->RequestRetire(); Projector->TickProjection(); return; }
        if (IsValid(Projector))
        {
            Projector->TickProjection();
            if (Projector->HasFailed()) { FailAndRetire(); return; }
            if (Get_HasCompleteSnapshot() &&
                (AcceptedSnapshot.WorldGeneration != ProjectorGeneration ||
                 AcceptedSnapshot.SampleSequence > ProjectorSequence) &&
                (ProjectorSequence == 0 ||
                 Projector->IsReadyFor(ProjectorGeneration, ProjectorSequence)))
            {
                if (!Projector->TryApply(AcceptedSnapshot.WorldGeneration,
                    AcceptedSnapshot.SampleSequence, AcceptedSnapshot.Rows))
                { if (Projector->HasFailed()) { FailAndRetire(); } return; }
                ProjectorGeneration = AcceptedSnapshot.WorldGeneration;
                ProjectorSequence = AcceptedSnapshot.SampleSequence;
                ProjectedSnapshot = AcceptedSnapshot;
            }
        }
        return;
    }
    if (!bInitialized) { return; }
    // Initial root creation may be asynchronous; loss of an admitted authority tuple is terminal.
    if (!HasExactAuthorityContext() || !IsValid(SharedSimulation) ||
        SharedSimulation->Get_HasFailed())
    { FailAndRetire(); return; }
    if (PendingBatchSerial == 0 && !SharedSimulation->Get_IsReady())
    {
        // SharedSimulation owns its bounded initial-root deadline and explicit failure state.
        // After readiness was established, a missing complete cohort with no director batch is loss.
        if (bHasHadReadySimulation) { FailAndRetire(); }
        return;
    }
    if (SharedSimulation->Get_IsReady()) { bHasHadReadySimulation = true; }
    if (PendingBatchSerial != 0)
    {
        auto* PendingSession = PendingOwner.Get();
        if (IsValid(PendingSession) && HasServerTransportContext(*PendingSession) &&
            PendingSession->Get_TransportSessionGeneration() == PendingSessionGeneration &&
            PendingSession->IsServerPending(PendingRequestSerial, ECkCrowdGym_RequestKind::Control) &&
            (!HasServerRequestContext(*PendingSession) || !PendingController.IsValid() ||
             !PendingPawn.IsValid() || PendingSession->GetOwner() != PendingController.Get() ||
             PendingController->GetPawn() != PendingPawn.Get()))
        { PendingSession->WithdrawSelection();
          PendingSession->RequestTerminalGone(PendingRequestSerial, ECkCrowdGym_RequestKind::Control); }
        if (SharedSimulation->Get_IsBusy()) { return; }
        if (SharedSimulation->Get_LastCompletedBatchSerial() != PendingBatchSerial)
        { FailAndRetire(); return; }
        FCkCrowdGym_RowsSnapshot Candidate;
        TMap<TWeakObjectPtr<UCkCrowdGym_Session_UE>, FCkCrowdRemoteAgentDetail> Details;
        TArray<TWeakObjectPtr<UCkCrowdGym_Session_UE>> Withdrawals;
        if (!TryBuildEpoch(Candidate, Details, Withdrawals)) { FailAndRetire(); return; }
        bool bPreparedOwnerCommit = false;
        if (IsValid(PendingSession) && HasExactSession(*PendingSession) &&
            PendingSession->Get_TransportSessionGeneration() == PendingSessionGeneration &&
            PendingController.IsValid() && PendingPawn.IsValid() &&
            PendingSession->GetOwner() == PendingController.Get() &&
            PendingController->GetPawn() == PendingPawn.Get() &&
            PendingSession->IsServerPending(PendingRequestSerial, ECkCrowdGym_RequestKind::Control))
        {
            const auto Selected = Withdrawals.Contains(
                TWeakObjectPtr<UCkCrowdGym_Session_UE>(PendingSession)) ?
                FCkCrowdRemoteAgentId{} : PendingSession->Get_SelectedId();
            const auto* Detail = ck_crowd_gym_transport::IsDefaultId(Selected) ? nullptr :
                Details.Find(TWeakObjectPtr<UCkCrowdGym_Session_UE>(PendingSession));
            FCkCrowdGym_OwnerCommit Commit;
            if (!PendingSession->TryPrepareAccepted(PendingRequestSerial,
                ECkCrowdGym_RequestKind::Control, Candidate,
                Selected, PendingSession->Get_SelectionSerial(), Detail, Commit) ||
                !PendingSession->TryRetainPreparedAccepted(Commit))
            { FailAndRetire(); return; }
            bPreparedOwnerCommit = true;
        }
        else if (IsValid(PendingSession) && HasServerTransportContext(*PendingSession) &&
            PendingSession->Get_TransportSessionGeneration() == PendingSessionGeneration &&
            PendingSession->IsServerPending(PendingRequestSerial, ECkCrowdGym_RequestKind::Control))
        {
            // A replacement pawn cannot inherit the old pawn's accepted operation.
            PendingSession->RequestTerminalGone(PendingRequestSerial, ECkCrowdGym_RequestKind::Control);
        }
        PublishPreparedEpoch(MoveTemp(Candidate), MoveTemp(Details), Withdrawals);
        if (bPreparedOwnerCommit) { PendingSession->SendRetainedCommit(); }
        if (IsValid(PendingSession)) { PendingSession->ClearServerPending(); }
        PendingOwner.Reset(); PendingController.Reset(); PendingPawn.Reset();
        PendingBatchSerial = 0; PendingRequestSerial = 0; PendingRow = 0;
        PendingSessionGeneration = 0;
    }
    else if (!SharedSimulation->Get_IsBusy() &&
        (!Get_HasCompleteSnapshot() || World->GetTimeSeconds() >= NextSampleAtSeconds))
    {
        if (!TryPublishEpoch())
        {
            if (FirstSampleFailureSeconds == 0.0)
            { FirstSampleFailureSeconds = World->GetTimeSeconds(); }
            else if (World->GetTimeSeconds() - FirstSampleFailureSeconds >= 20.0)
            { FailAndRetire(); }
        }
    }
}

void ACkCrowdGym_GameState_UE::OnRep_Snapshot()
{
    if (bTerminal || !FCkCrowdGymWireValidation::IsValidReadySnapshot(Snapshot)) { return; }
    if (Get_HasCompleteSnapshot() &&
        (Snapshot.WorldGeneration != AcceptedSnapshot.WorldGeneration ||
         Snapshot.SampleSequence <= AcceptedSnapshot.SampleSequence)) { return; }
    AcceptedSnapshot = Snapshot;
}

void ACkCrowdGym_GameState_UE::OnRep_WorldTerminal()
{
    auto* World = GetWorld();
    if (!WorldTerminal.bRetired || WorldTerminal.WorldGeneration < 0 ||
        WorldTerminal.WorldGeneration >= TNumericLimits<int64>::Max() ||
        !IsValid(World) || World->GetGameState() != this) { return; }
    if (AcceptedSnapshot.WorldGeneration > 0 &&
        WorldTerminal.WorldGeneration != AcceptedSnapshot.WorldGeneration) { return; }
    FailAndRetire();
}

void ACkCrowdGym_GameState_UE::FailAndRetire()
{
    if (bTerminal) { return; }
    if (HasAuthority())
    {
        WorldTerminal.WorldGeneration = IsValid(SharedSimulation) ?
            SharedSimulation->Get_WorldGeneration() : AcceptedSnapshot.WorldGeneration;
        WorldTerminal.bRetired = true;
    }
    bTerminal = true;
    auto* World = GetWorld();
    if (HasAuthority() && IsValid(World) && World->IsGameWorld() &&
        !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        !World->bIsTearingDown && World->GetGameState() == this)
    {
        const bool bClosed = OnAuthorityTerminalFailure(true);
        if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            IsActorBeingDestroyed() || GetWorld() != World) { return; }
        bAuthorityAdmissionClosed = bClosed;
    }
    // The authored callback closes Common admission and asks this same native owner to
    // stop known motion. It may also end the actor; do not read an invalidated instance.
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        IsActorBeingDestroyed()) { return; }
    World = GetWorld();
    if (!IsValid(World) || World->bIsTearingDown || World->GetGameState() != this)
    { return; }
    if (HasAuthority() && !bAuthorityAdmissionClosed) { return; }
    if (HasAuthority())
    {
        ForceNetUpdate();
        if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            IsActorBeingDestroyed() || GetWorld() != World) { return; }
    }
    if (bAuthorityAdmissionClosed && HasAuthority() && IsValid(SharedSimulation) &&
        SharedSimulation->GetOwner() == this && SharedSimulation->GetWorld() == World)
    { SharedSimulation->Request_CloseUnproven(); }
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        IsActorBeingDestroyed() || GetWorld() != World) { return; }
    FinishTerminalPresentation();
    // Failed source identities remain retained for actual world teardown.
}

void ACkCrowdGym_GameState_UE::FinishTerminalPresentation()
{
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        IsActorBeingDestroyed()) { return; }
    auto* World = GetWorld();
    auto HasContext = [&]() -> bool
    {
        return IsValid(this) && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
            !IsActorBeingDestroyed() && IsValid(World) &&
            !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
            !World->bIsTearingDown && GetWorld() == World &&
            World->GetGameState() == this;
    };
    if (!HasContext()) { return; }
    if (IsValid(Projector)) { Projector->RequestRetire(); }
    if (!HasContext()) { return; }
    if (ProviderToken.IsValid())
    { FCkCrowdRemoteDebugRegistry::TryUnregister(*World, *this, ProviderToken); }
    if (!HasContext()) { return; }
    ProviderToken = {};
    for (TActorIterator<APlayerController> It(World); It; ++It)
    {
        auto* Session = ck_crowd_gym_transport::FindSession(**It);
        if (IsValid(Session)) { Session->RetireForWorld(*this, WorldTerminal.WorldGeneration); }
        if (!HasContext()) { return; }
    }
    AcceptedSnapshot = {};
    ProjectedSnapshot = {};
    Snapshot = {};
    PendingOwner.Reset();
    PendingController.Reset(); PendingPawn.Reset();
    PendingBatchSerial = 0; PendingRequestSerial = 0; PendingSessionGeneration = 0; PendingRow = 0;
    SampledDetails.Reset();
}

void ACkCrowdGym_GameState_UE::Request_CloseFailedShared()
{
    if (!bTerminal) { FailAndRetire(); return; }
    auto* World = GetWorld();
    if (bAuthorityAdmissionClosed && HasAuthority() && IsValid(World) &&
        !World->bIsTearingDown && World->GetGameState() == this &&
        IsValid(SharedSimulation) && SharedSimulation->GetOwner() == this &&
        SharedSimulation->GetWorld() == World)
    { SharedSimulation->Request_CloseUnproven(); }
}

void ACkCrowdGym_GameState_UE::Request_RetireShared()
{
    if (bTerminal) { return; }
    auto* World = GetWorld();
    if (!HasAuthority() || !IsValid(World) || World->bIsTearingDown ||
        World->GetGameState() != this || !IsValid(SharedSimulation) ||
        SharedSimulation->GetOwner() != this ||
        SharedSimulation->GetWorld() != World ||
        !SharedSimulation->Get_IsReady())
    { FailAndRetire(); return; }
    WorldTerminal.WorldGeneration = SharedSimulation->Get_WorldGeneration();
    WorldTerminal.bRetired = true;
    bTerminal = true;
    const bool bClosed = OnAuthorityTerminalFailure(false);
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        IsActorBeingDestroyed() || GetWorld() != World) { return; }
    bAuthorityAdmissionClosed = bClosed;
    if (!bAuthorityAdmissionClosed) { return; }
    ForceNetUpdate();
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        IsActorBeingDestroyed() || GetWorld() != World) { return; }
    if (IsValid(SharedSimulation) && SharedSimulation->GetOwner() == this &&
        SharedSimulation->GetWorld() == World)
    { SharedSimulation->Request_RetireHealthyRoot(); }
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        IsActorBeingDestroyed() || GetWorld() != World) { return; }
    FinishTerminalPresentation();
}

void ACkCrowdGym_GameState_UE::EndPlay(const EEndPlayReason::Type Reason)
{ FailAndRetire(); Super::EndPlay(Reason); }

void ACkCrowdGym_GameState_UE::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACkCrowdGym_GameState_UE, Snapshot);
    DOREPLIFETIME(ACkCrowdGym_GameState_UE, WorldTerminal);
    DOREPLIFETIME(ACkCrowdGym_GameState_UE, PCClass);
    DOREPLIFETIME(ACkCrowdGym_GameState_UE, PawnClass);
    DOREPLIFETIME(ACkCrowdGym_GameState_UE, CatalogModeClass);
    DOREPLIFETIME(ACkCrowdGym_GameState_UE, CatalogGymIndex);
    DOREPLIFETIME(ACkCrowdGym_GameState_UE, FrozenCatalog);
    DOREPLIFETIME(ACkCrowdGym_GameState_UE, FrozenFootprint);
}

UCkCrowdGym_Session_UE::UCkCrowdGym_Session_UE()
{ SetIsReplicatedByDefault(true); }

auto UCkCrowdGym_Session_UE::GetCrowdState() const -> ACkCrowdGym_GameState_UE*
{
    auto* World = GetWorld();
    return IsValid(World) ? Cast<ACkCrowdGym_GameState_UE>(World->GetGameState()) : nullptr;
}

auto UCkCrowdGym_Session_UE::HasExactLocalOwner() const -> bool
{
    const auto* PC = Cast<APlayerController>(GetOwner());
    const auto* World = GetWorld();
    const auto* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
    return IsValid(World) && IsValid(PC) && IsValid(LP) &&
        ck_crowd_gym_transport::IsLocalContext(*World, *PC, *LP) &&
        IsValid(GetCrowdState()) && GetCrowdState()->HasLocalRequestContext(*this);
}

auto UCkCrowdGym_Session_UE::HasExactServerOwner() const -> bool
{
    const auto* PC = Cast<APlayerController>(GetOwner());
    const auto* State = GetCrowdState();
    return IsValid(PC) && PC->HasAuthority() && IsValid(State) &&
        State->HasServerRequestContext(*this);
}

auto UCkCrowdGym_Session_UE::HasLocalTransportOwner() const -> bool
{
    const auto* State = GetCrowdState();
    return IsValid(State) && State->HasLocalTransportContext(*this);
}

auto UCkCrowdGym_Session_UE::HasServerTransportOwner() const -> bool
{
    const auto* State = GetCrowdState();
    return IsValid(State) && State->HasServerTransportContext(*this);
}

auto UCkCrowdGym_Session_UE::TryQueueControl(int32 Row) -> bool
{
    auto* State = GetCrowdState();
    if (!HasExactLocalOwner() || !IsValid(State) || !State->Get_HasCompleteSnapshot() ||
        Row < 1 || Row > 5 || LocalPendingSerial != 0 || bLocalRetained || bSessionTeardownPending ||
        !ck_crowd_gym_transport::IsSerial(NextControlSerial)) { return false; }
    LocalPendingSerial = NextControlSerial++;
    LocalPendingGeneration = State->Get_RowsSnapshot().WorldGeneration;
    LocalPendingKind = ECkCrowdGym_RequestKind::Control;
    LocalPendingPawn = Cast<APlayerController>(GetOwner())->GetPawn();
    Server_RequestControl(LocalPendingSerial, Row, LocalPendingGeneration, TransportSessionGeneration);
    return true;
}

auto UCkCrowdGym_Session_UE::TryQueueSelection(const FCkCrowdRemoteAgentId& Id,
    int64 Serial, bool bClear) -> bool
{
    auto* State = GetCrowdState();
    if (!IsInGameThread() || !HasExactLocalOwner() || !IsValid(State) || !State->Get_HasCompleteSnapshot() ||
        !ck_crowd_gym_transport::IsSerial(Serial) ||
        Serial <= LocalSelectionAttemptHigh ||
        (bClear ? !ck_crowd_gym_transport::IsDefaultId(Id) :
            !Id.IsValid() || Id.WorldGeneration != State->Get_RowsSnapshot().WorldGeneration) ||
        LocalPendingSerial != 0 || bLocalRetained || bSessionTeardownPending) { return false; }
    LocalPendingSerial = Serial;
    LocalPendingGeneration = State->Get_RowsSnapshot().WorldGeneration;
    LocalPendingKind = ECkCrowdGym_RequestKind::Selection;
    LocalSelectionAttemptHigh = Serial;
    LocalPendingPawn = Cast<APlayerController>(GetOwner())->GetPawn();
    Server_RequestSelection(Serial, Id, bClear, LocalPendingGeneration, TransportSessionGeneration);
    return true;
}


auto UCkCrowdGym_Session_UE::TryQueueNextSelection(const FCkCrowdRemoteAgentId& Id,
    bool bClear, int64& OutIssuedSerial) -> bool
{
    OutIssuedSerial = 0;
    if (!IsInGameThread() || LocalSelectionAttemptHigh < 0 || SelectionAccepted < 0) { return false; }
    const int64 High = FMath::Max(LocalSelectionAttemptHigh, SelectionAccepted);
    if (High >= TNumericLimits<int64>::Max() - 1) { return false; }
    const int64 Candidate = High + 1;
    if (!TryQueueSelection(Id, Candidate, bClear)) { return false; }
    OutIssuedSerial = Candidate; // publish only after real owner admission succeeds
    return true;
}

auto UCkCrowdGym_Session_UE::TryCopySelectionEnvelope(FCkCrowdRemoteSelectionEnvelope& Out) const -> bool
{
    Out = {};
    const auto* State = GetCrowdState();
    if (!IsInGameThread() || !HasLocalTransportOwner() || !IsValid(State) ||
        !State->Get_HasCompleteSnapshot() || bSessionTeardownPending) { return false; }
    FCkCrowdRemoteSelectionEnvelope Candidate;
    Candidate.SessionGeneration = TransportSessionGeneration;
    if (bLocalRetained)
    {
        // Never hide an unavailable Accepted package or another kind behind a newer view.
        FCkCrowdGym_OwnerCommit Commit;
        if (LocalRetained.Kind != ECkCrowdGym_RequestKind::Selection || !TryCopyRetainedCommit(Commit))
        { return false; }
        Candidate.bRetainedDelivery = true;
        Candidate.WorldGeneration = Commit.Snapshot.WorldGeneration;
        Candidate.SampleSequence = Commit.Snapshot.SampleSequence;
        Candidate.Rows = Commit.Snapshot.Rows;
        Candidate.SelectedId = Commit.SelectedId;
        Candidate.SelectionRequestSerial = Commit.SelectionRequestSerial;
        Candidate.bHasDetail = Commit.bHasDetail;
        Candidate.Detail = Commit.Detail;
        Candidate.Receipt.WorldGeneration = Candidate.WorldGeneration;
        Candidate.Receipt.SessionGeneration = TransportSessionGeneration;
        Candidate.Receipt.DeliverySerial = Commit.DeliverySerial;
        Candidate.Receipt.RequestSerial = Commit.RequestSerial;
        switch (Commit.Outcome)
        {
        case ECkCrowdGym_RequestOutcome::Accepted: Candidate.Outcome = ECkCrowdRemoteSelectionOutcome::Accepted; break;
        case ECkCrowdGym_RequestOutcome::Invalid: Candidate.Outcome = ECkCrowdRemoteSelectionOutcome::Invalid; break;
        case ECkCrowdGym_RequestOutcome::Stale: Candidate.Outcome = ECkCrowdRemoteSelectionOutcome::Stale; break;
        case ECkCrowdGym_RequestOutcome::Busy: Candidate.Outcome = ECkCrowdRemoteSelectionOutcome::Busy; break;
        case ECkCrowdGym_RequestOutcome::Unauthorized: Candidate.Outcome = ECkCrowdRemoteSelectionOutcome::Unauthorized; break;
        case ECkCrowdGym_RequestOutcome::Gone: Candidate.Outcome = ECkCrowdRemoteSelectionOutcome::Gone; break;
        case ECkCrowdGym_RequestOutcome::Failed: Candidate.Outcome = ECkCrowdRemoteSelectionOutcome::Failed; break;
        default: return false;
        }
    }
    else
    {
        FCkCrowdGym_RowsSnapshot Rows;
        if (!TryCopyLatestView(Rows, Candidate.SelectedId, Candidate.SelectionRequestSerial,
            Candidate.Detail, Candidate.bHasDetail)) { return false; }
        Candidate.WorldGeneration = Rows.WorldGeneration;
        Candidate.SampleSequence = Rows.SampleSequence;
        Candidate.Rows = MoveTemp(Rows.Rows);
    }
    if (Candidate.WorldGeneration != State->Get_RowsSnapshot().WorldGeneration ||
        !FCkCrowdRemoteDebugValidation::IsValid(Candidate)) { return false; }
    Out = MoveTemp(Candidate);
    return true;
}

auto UCkCrowdGym_Session_UE::TryAcknowledgeSelection(const FCkCrowdRemoteSelectionReceipt& Receipt) -> bool
{
    if (!IsInGameThread() || !FCkCrowdRemoteDebugValidation::IsValid(Receipt) ||
        Receipt.SessionGeneration != TransportSessionGeneration) { return false; }
    FCkCrowdGym_OwnerCommit Commit;
    // Pure copy enforces admitted pawn for Accepted, transport identity for Gone.
    if (!TryCopyRetainedCommit(Commit) || Commit.Kind != ECkCrowdGym_RequestKind::Selection ||
        Commit.Snapshot.WorldGeneration != Receipt.WorldGeneration ||
        Commit.DeliverySerial != Receipt.DeliverySerial || Commit.RequestSerial != Receipt.RequestSerial)
    { return false; }
    const auto* State = GetCrowdState();
    if (!IsValid(State) || State->Get_RowsSnapshot().WorldGeneration != Receipt.WorldGeneration) { return false; }
    return TryAcknowledgeCommit(Receipt.DeliverySerial);
}

auto UCkCrowdGym_Session_UE::CanAdmit(int64 Serial, ECkCrowdGym_RequestKind Kind) const -> bool
{
    if (!ck_crowd_gym_transport::IsSerial(Serial) || bServerRetained || ServerPendingSerial != 0)
    { return false; }
    if (Kind == ECkCrowdGym_RequestKind::Control)
    { return Serial == ControlAttemptHigh && Serial > ControlAccepted; }
    if (Kind == ECkCrowdGym_RequestKind::Selection)
    { return Serial == SelectionAttemptHigh && Serial > SelectionAccepted; }
    return false;
}

void UCkCrowdGym_Session_UE::AdmitAttempt(int64 Serial, ECkCrowdGym_RequestKind Kind)
{
    if (Kind == ECkCrowdGym_RequestKind::Control) { ControlAttemptHigh = Serial; }
    if (Kind == ECkCrowdGym_RequestKind::Selection) { SelectionAttemptHigh = Serial; }
}

auto UCkCrowdGym_Session_UE::HasFreshAttempt(int64 Serial,
    ECkCrowdGym_RequestKind Kind) const -> bool
{
    if (!ck_crowd_gym_transport::IsSerial(Serial)) { return false; }
    return Kind == ECkCrowdGym_RequestKind::Control ?
        Serial > ControlAttemptHigh && Serial > ControlAccepted :
        Kind == ECkCrowdGym_RequestKind::Selection &&
        Serial > SelectionAttemptHigh && Serial > SelectionAccepted;
}

void UCkCrowdGym_Session_UE::MarkServerPending(int64 Serial, ECkCrowdGym_RequestKind Kind)
{
    ServerPendingSerial = Serial; ServerPendingKind = Kind;
    auto* PC = Cast<APlayerController>(GetOwner());
    ServerPendingController = PC;
    ServerPendingPawn = IsValid(PC) ? PC->GetPawn() : nullptr;
}

auto UCkCrowdGym_Session_UE::IsServerPending(int64 Serial,
    ECkCrowdGym_RequestKind Kind) const -> bool
{ return ServerPendingSerial == Serial && ServerPendingKind == Kind; }

void UCkCrowdGym_Session_UE::ClearServerPending()
{ ServerPendingSerial = 0; ServerPendingController.Reset(); ServerPendingPawn.Reset(); }

void UCkCrowdGym_Session_UE::WithdrawSelection()
{ ServerSelectedId = {}; }

void UCkCrowdGym_Session_UE::SetSelection(const FCkCrowdRemoteAgentId& Id, int64 Serial)
{ ServerSelectedId = Id; ServerSelectionSerial = Serial; }

auto UCkCrowdGym_Session_UE::DeliverAccepted(int64 Serial,
    ECkCrowdGym_RequestKind Kind, int32 Row,
    const FCkCrowdGym_RowsSnapshot& InSnapshot,
    const FCkCrowdRemoteAgentId& SelectedId,
    int64 SelectionSerial, const FCkCrowdRemoteAgentDetail* Detail) -> bool
{
    FCkCrowdGym_OwnerCommit Candidate;
    if (!TryPrepareAccepted(Serial, Kind, InSnapshot, SelectedId, SelectionSerial, Detail,
        Candidate) || !TryRetainPreparedAccepted(Candidate)) { return false; }
    SendRetainedCommit();
    return true;
}

auto UCkCrowdGym_Session_UE::TryPrepareAccepted(int64 Serial,
    ECkCrowdGym_RequestKind Kind, const FCkCrowdGym_RowsSnapshot& InSnapshot,
    const FCkCrowdRemoteAgentId& SelectedId, int64 SelectionSerial,
    const FCkCrowdRemoteAgentDetail* Detail, FCkCrowdGym_OwnerCommit& Out) const -> bool
{
    Out = {};
    const auto* State = GetCrowdState();
    if (!HasExactServerOwner() || bServerRetained ||
        !IsValid(State) || !IsValid(State->SharedSimulation) ||
        InSnapshot.WorldGeneration != State->SharedSimulation->Get_WorldGeneration() ||
        !ck_crowd_gym_transport::IsSerial(NextDeliverySerial) ||
        !FCkCrowdGymWireValidation::IsValidReadySnapshot(InSnapshot)) { return false; }
    FCkCrowdGym_OwnerCommit Candidate;
    Candidate.DeliverySerial = NextDeliverySerial;
    Candidate.RequestSerial = Serial;
    Candidate.Kind = Kind;
    Candidate.Outcome = ECkCrowdGym_RequestOutcome::Accepted;
    Candidate.Snapshot = InSnapshot;
    Candidate.SelectedId = SelectedId;
    Candidate.SelectionRequestSerial = SelectionSerial;
    Candidate.bHasDetail = Detail != nullptr;
    if (Detail) { Candidate.Detail = *Detail; }
    if (!FCkCrowdGymWireValidation::IsValidCommit(Candidate)) { return false; }
    if (Kind == ECkCrowdGym_RequestKind::Control ?
        !IsServerPending(Serial, Kind) : !CanAdmit(Serial, Kind)) { return false; }
    const auto* PC = Cast<APlayerController>(GetOwner());
    if (Kind == ECkCrowdGym_RequestKind::Control &&
        (ServerPendingController.Get() != PC || !ServerPendingPawn.IsValid() ||
         PC->GetPawn() != ServerPendingPawn.Get())) { return false; }
    Out = MoveTemp(Candidate);
    return true;
}

auto UCkCrowdGym_Session_UE::TryRetainPreparedAccepted(
    const FCkCrowdGym_OwnerCommit& Candidate) -> bool
{
    // Revalidate before the first externally observable mutation. Nothing fallible follows retain.
    const auto* State = GetCrowdState();
    if (!HasExactServerOwner() || bServerRetained ||
        !IsValid(State) || !IsValid(State->SharedSimulation) ||
        Candidate.Snapshot.WorldGeneration != State->SharedSimulation->Get_WorldGeneration() ||
        Candidate.DeliverySerial != NextDeliverySerial ||
        Candidate.Outcome != ECkCrowdGym_RequestOutcome::Accepted ||
        !FCkCrowdGymWireValidation::IsValidCommit(Candidate) ||
        (Candidate.Kind == ECkCrowdGym_RequestKind::Control ?
            !IsServerPending(Candidate.RequestSerial, Candidate.Kind) :
            !CanAdmit(Candidate.RequestSerial, Candidate.Kind))) { return false; }
    const auto* CurrentPC = Cast<APlayerController>(GetOwner());
    if (Candidate.Kind == ECkCrowdGym_RequestKind::Control &&
        (ServerPendingController.Get() != CurrentPC || !ServerPendingPawn.IsValid() ||
         CurrentPC->GetPawn() != ServerPendingPawn.Get())) { return false; }
    ServerRetained = Candidate;
    auto* PC = Cast<APlayerController>(GetOwner());
    ServerRetainedController = PC;
    ServerRetainedPawn = PC->GetPawn();
    bServerRetained = true;
    ++NextDeliverySerial;
    if (Candidate.Kind == ECkCrowdGym_RequestKind::Control)
    { ControlAccepted = Candidate.RequestSerial; }
    if (Candidate.Kind == ECkCrowdGym_RequestKind::Selection)
    { SelectionAccepted = Candidate.RequestSerial; }
    ClearServerPending();
    return true;
}

void UCkCrowdGym_Session_UE::SendRetainedCommit()
{
    if (bServerRetained) { Client_ReceiveCommit(ServerRetained, TransportSessionGeneration); }
}

auto UCkCrowdGym_Session_UE::DeliverRejected(int64 Serial,
    ECkCrowdGym_RequestKind Kind, ECkCrowdGym_RequestOutcome Outcome) -> bool
{
    auto* State = GetCrowdState();
    if (!HasServerTransportOwner() || !IsValid(State) || !State->Get_HasCompleteSnapshot() ||
        bServerRetained || ServerPendingSerial != 0 ||
        !ck_crowd_gym_transport::IsSerial(Serial) ||
        !ck_crowd_gym_transport::IsSerial(NextDeliverySerial)) { return false; }
    FCkCrowdGym_OwnerCommit Candidate;
    Candidate.DeliverySerial = NextDeliverySerial;
    Candidate.RequestSerial = Serial;
    Candidate.Kind = Kind;
    Candidate.Outcome = Outcome;
    Candidate.Snapshot = State->Get_RowsSnapshot();
    if (!FCkCrowdGymWireValidation::IsValidCommit(Candidate)) { return false; }
    ServerRetained = Candidate;
    ServerRetainedController.Reset(); ServerRetainedPawn.Reset();
    bServerRetained = true;
    ++NextDeliverySerial;
    Client_ReceiveCommit(Candidate, TransportSessionGeneration);
    return true;
}

void UCkCrowdGym_Session_UE::RequestTerminalGone(int64 Serial, ECkCrowdGym_RequestKind Kind)
{
    if (!HasServerTransportOwner() || !IsServerPending(Serial, Kind)) { return; }
    // Existing accepted commits remain retained. Context loss cannot overwrite an unresolved result.
    if (bServerRetained) { return; }
    ClearServerPending();
    if (!DeliverRejected(Serial, Kind, ECkCrowdGym_RequestOutcome::Gone))
    { GetCrowdState()->Request_RetireShared(); }
}

void UCkCrowdGym_Session_UE::Server_RequestControl_Implementation(
    int64 Serial, int32 Row, int64 Generation, int64 SessionGeneration)
{
    auto* State = GetCrowdState();
    if (!HasServerTransportOwner() || !IsValid(State) ||
        SessionGeneration != TransportSessionGeneration) { return; }
    if (!HasFreshAttempt(Serial, ECkCrowdGym_RequestKind::Control))
    { DeliverRejected(Serial, ECkCrowdGym_RequestKind::Control,
        ECkCrowdGym_RequestOutcome::Stale); return; }
    AdmitAttempt(Serial, ECkCrowdGym_RequestKind::Control);
    if (bServerRetained || ServerPendingSerial != 0) { return; }
    if (!HasExactServerOwner())
    { DeliverRejected(Serial, ECkCrowdGym_RequestKind::Control,
        ECkCrowdGym_RequestOutcome::Gone); return; }
    if (Generation != State->Get_RowsSnapshot().WorldGeneration)
    { DeliverRejected(Serial, ECkCrowdGym_RequestKind::Control,
        ECkCrowdGym_RequestOutcome::Stale); return; }
    if (Row < 1 || Row > 5)
    { DeliverRejected(Serial, ECkCrowdGym_RequestKind::Control,
        ECkCrowdGym_RequestOutcome::Invalid); return; }
    if (!State->TrySubmitControl(*this, Serial, Row))
    { DeliverRejected(Serial, ECkCrowdGym_RequestKind::Control,
        ECkCrowdGym_RequestOutcome::Busy); }
}

void UCkCrowdGym_Session_UE::Server_RequestSelection_Implementation(
    int64 Serial, FCkCrowdRemoteAgentId Id, bool bClear, int64 Generation, int64 SessionGeneration)
{
    auto* State = GetCrowdState();
    if (!HasServerTransportOwner() || !IsValid(State) ||
        SessionGeneration != TransportSessionGeneration) { return; }
    if (!HasFreshAttempt(Serial, ECkCrowdGym_RequestKind::Selection))
    { DeliverRejected(Serial, ECkCrowdGym_RequestKind::Selection,
        ECkCrowdGym_RequestOutcome::Stale); return; }
    AdmitAttempt(Serial, ECkCrowdGym_RequestKind::Selection);
    if (bServerRetained || ServerPendingSerial != 0) { return; }
    if (!HasExactServerOwner())
    { DeliverRejected(Serial, ECkCrowdGym_RequestKind::Selection,
        ECkCrowdGym_RequestOutcome::Gone); return; }
    if (Generation != State->Get_RowsSnapshot().WorldGeneration)
    { DeliverRejected(Serial, ECkCrowdGym_RequestKind::Selection,
        ECkCrowdGym_RequestOutcome::Stale); return; }
    if (bClear ? !ck_crowd_gym_transport::IsDefaultId(Id) : !Id.IsValid())
    { DeliverRejected(Serial, ECkCrowdGym_RequestKind::Selection,
        ECkCrowdGym_RequestOutcome::Invalid); return; }
    if (!State->TrySubmitSelection(*this, Serial, Id, bClear))
    { DeliverRejected(Serial, ECkCrowdGym_RequestKind::Selection,
        ECkCrowdGym_RequestOutcome::Busy); }
}

void UCkCrowdGym_Session_UE::Client_ReceiveCommit_Implementation(
    const FCkCrowdGym_OwnerCommit& Commit, int64 SessionGeneration)
{
    auto* State = GetCrowdState();
    if (!HasLocalTransportOwner() || !IsValid(State) || bLocalRetained || bSessionTeardownPending ||
        SessionGeneration != TransportSessionGeneration ||
        !FCkCrowdGymWireValidation::IsValidCommit(Commit) ||
        Commit.DeliverySerial <= LocalDeliveryHigh ||
        Commit.RequestSerial != LocalPendingSerial || Commit.Kind != LocalPendingKind ||
        Commit.Snapshot.WorldGeneration != LocalPendingGeneration)
    { return; }
    LocalRetained = Commit;
    LocalRetainedPawn = LocalPendingPawn;
    LocalPendingPawn.Reset();
    bLocalRetained = true;
    LocalPendingSerial = 0;
    LocalPendingGeneration = 0;
    LocalDeliveryHigh = Commit.DeliverySerial;
    const auto* PC = Cast<APlayerController>(GetOwner());
    const bool bMayInstallAccepted = HasExactLocalOwner() && LocalRetainedPawn.IsValid() &&
        PC->GetPawn() == LocalRetainedPawn.Get();
    if (Commit.Outcome == ECkCrowdGym_RequestOutcome::Accepted && bMayInstallAccepted)
    {
        if (Commit.Kind == ECkCrowdGym_RequestKind::Control)
        { ControlAccepted = Commit.RequestSerial; }
        if (Commit.Kind == ECkCrowdGym_RequestKind::Selection)
        { SelectionAccepted = Commit.RequestSerial; }
    }
    if (Commit.Outcome == ECkCrowdGym_RequestOutcome::Accepted && bMayInstallAccepted)
    {
        LocalSelectionViewHigh = FMath::Max(LocalSelectionViewHigh,
            Commit.SelectionRequestSerial);
        TryInstallView(Commit.Snapshot, Commit.SelectedId,
            Commit.SelectionRequestSerial, Commit.bHasDetail, Commit.Detail);
    }
}

auto UCkCrowdGym_Session_UE::TryInstallView(
    const FCkCrowdGym_RowsSnapshot& InSnapshot,
    const FCkCrowdRemoteAgentId& InId, int64 InSelectionSerial,
    bool bInHasDetail, const FCkCrowdRemoteAgentDetail& InDetail) -> bool
{
    if (!FCkCrowdGymWireValidation::IsValidReadySnapshot(InSnapshot)) { return false; }
    const FCkCrowdRemoteAgentDetail DefaultDetail;
    if (ck_crowd_gym_transport::IsDefaultId(InId))
    {
        if (InSelectionSerial < 0 ||
            (InSelectionSerial != 0 && !ck_crowd_gym_transport::IsSerial(InSelectionSerial)) ||
            bInHasDetail ||
            !FCkCrowdRemoteAgentDetail::StaticStruct()->CompareScriptStruct(
                &InDetail, &DefaultDetail, PPF_None)) { return false; }
    }
    else
    {
        if (!InId.IsValid() || InId.WorldGeneration != InSnapshot.WorldGeneration ||
            !ck_crowd_gym_transport::IsSerial(InSelectionSerial) || !bInHasDetail ||
            !FCkCrowdRemoteDebugValidation::IsValid(InDetail) ||
            !(InDetail.Id == InId) || InDetail.SelectionRequestSerial != InSelectionSerial ||
            InDetail.SampleSequence != InSnapshot.SampleSequence)
        { return false; }
        bool bFound = false;
        for (const auto& Row : InSnapshot.Rows)
        {
            if (!(Row.Id == InId)) { continue; }
            if (!FCkCrowdRemoteAgentRow::StaticStruct()->CompareScriptStruct(
                &Row, &InDetail.Sample, PPF_None) ||
                Row.ConfigurationRevision != InDetail.ConfigurationRevision) { return false; }
            bFound = true; break;
        }
        if (!bFound) { return false; }
    }
    if (InSelectionSerial < LocalSelectionViewHigh ||
        (LocalPendingSerial != 0 && LocalPendingKind == ECkCrowdGym_RequestKind::Selection &&
         InSelectionSerial < LocalPendingSerial)) { return false; }
    if (bHasLatestView &&
        (InSnapshot.WorldGeneration != LatestViewRows.WorldGeneration ||
         InSnapshot.SampleSequence <= LatestViewRows.SampleSequence)) { return false; }
    LatestViewRows = InSnapshot;
    LocalSelectionViewHigh = InSelectionSerial;
    LatestViewId = InId;
    LatestViewSelectionSerial = InSelectionSerial;
    LatestViewDetail = InDetail;
    bLatestViewHasDetail = bInHasDetail;
    bHasLatestView = true;
    return true;
}

void UCkCrowdGym_Session_UE::Client_ReceiveView_Implementation(
    const FCkCrowdGym_RowsSnapshot& InSnapshot,
    FCkCrowdRemoteAgentId InSelectedId, int64 InSelectionSerial,
    bool bInHasDetail, const FCkCrowdRemoteAgentDetail& InDetail, int64 SessionGeneration)
{
    auto* State = GetCrowdState();
    if (!HasExactLocalOwner() || !IsValid(State) || bSessionTeardownPending ||
        SessionGeneration != TransportSessionGeneration ||
        InSnapshot.WorldGeneration != State->Get_RowsSnapshot().WorldGeneration) { return; }
    const auto* PC = Cast<APlayerController>(GetOwner());
    if (bLocalRetained && LocalRetained.Outcome == ECkCrowdGym_RequestOutcome::Accepted &&
        (!LocalRetainedPawn.IsValid() || PC->GetPawn() != LocalRetainedPawn.Get())) { return; }
    TryInstallView(InSnapshot, InSelectedId, InSelectionSerial, bInHasDetail, InDetail);
}

auto UCkCrowdGym_Session_UE::TryCopyLatestView(
    FCkCrowdGym_RowsSnapshot& OutRows, FCkCrowdRemoteAgentId& OutId,
    int64& OutSelectionSerial, FCkCrowdRemoteAgentDetail& OutDetail,
    bool& bOutHasDetail) const -> bool
{
    OutRows = {}; OutId = {}; OutSelectionSerial = 0; OutDetail = {};
    bOutHasDetail = false;
    if (!HasExactLocalOwner() || !bHasLatestView || bSessionTeardownPending ||
        !FCkCrowdGymWireValidation::IsValidReadySnapshot(LatestViewRows) ||
        (LocalPendingSerial != 0 && LocalPendingKind == ECkCrowdGym_RequestKind::Selection &&
         LatestViewSelectionSerial < LocalPendingSerial)) { return false; }
    const auto* PC = Cast<APlayerController>(GetOwner());
    if (bLocalRetained && LocalRetained.Outcome == ECkCrowdGym_RequestOutcome::Accepted &&
        (!LocalRetainedPawn.IsValid() || PC->GetPawn() != LocalRetainedPawn.Get())) { return false; }
    OutRows = LatestViewRows;
    OutId = LatestViewId;
    OutSelectionSerial = LatestViewSelectionSerial;
    OutDetail = LatestViewDetail;
    bOutHasDetail = bLatestViewHasDetail;
    return true;
}

auto UCkCrowdGym_Session_UE::TryCopyRetainedCommit(FCkCrowdGym_OwnerCommit& Out) const -> bool
{
    Out = {};
    if (!HasLocalTransportOwner() || !bLocalRetained || bSessionTeardownPending ||
        !FCkCrowdGymWireValidation::IsValidCommit(LocalRetained)) { return false; }
    if (LocalRetained.Outcome == ECkCrowdGym_RequestOutcome::Accepted)
    {
        const auto* PC = Cast<APlayerController>(GetOwner());
        if (!HasExactLocalOwner() || !LocalRetainedPawn.IsValid() ||
            PC->GetPawn() != LocalRetainedPawn.Get()) { return false; }
    }
    Out = LocalRetained;
    return true;
}

auto UCkCrowdGym_Session_UE::TryAcknowledgeCommit(int64 DeliverySerial) -> bool
{
    if (!HasLocalTransportOwner() || !bLocalRetained || bSessionTeardownPending ||
        LocalRetained.DeliverySerial != DeliverySerial ||
        !FCkCrowdGymWireValidation::IsValidCommit(LocalRetained)) { return false; }
    if (LocalRetained.Outcome == ECkCrowdGym_RequestOutcome::Accepted)
    {
        FCkCrowdGym_OwnerCommit InstalledContext;
        if (!TryCopyRetainedCommit(InstalledContext)) { return false; }
    }
    const int64 Generation = LocalRetained.Snapshot.WorldGeneration;
    RecordLocalSend(LocalAcknowledgeSendCount);
    Server_AcknowledgeCommit(Generation, DeliverySerial, TransportSessionGeneration);
    LocalRetained = {};
    bLocalRetained = false;
    LocalRetainedPawn.Reset();
    return true;
}

auto UCkCrowdGym_Session_UE::TryTeardownObsoletePawnSession() -> bool
{
    if (!HasLocalTransportOwner() || bSessionTeardownPending || !bLocalRetained ||
        LocalRetained.Outcome != ECkCrowdGym_RequestOutcome::Accepted) { return false; }
    const auto* PC = Cast<APlayerController>(GetOwner());
    if (LocalRetainedPawn.IsValid() && PC->GetPawn() == LocalRetainedPawn.Get()) { return false; }
    // Explicit terminal session teardown releases a previously accepted retained package only
    // after its admitted pawn context has ended. It never installs/ACKs that package's state.
    bSessionTeardownPending = true;
    LocalTeardownWorldGeneration = LocalRetained.Snapshot.WorldGeneration;
    LocalTeardownSessionGeneration = TransportSessionGeneration;
    RecordLocalSend(LocalTeardownSendCount);
    Server_TeardownSession(LocalTeardownWorldGeneration, LocalTeardownSessionGeneration);
    return true;
}

void UCkCrowdGym_Session_UE::Server_TeardownSession_Implementation(
    int64 Generation, int64 SessionGeneration)
{
    const auto* State = GetCrowdState();
    if (!HasServerTransportOwner() || !IsValid(State) ||
        Generation != State->Get_RowsSnapshot().WorldGeneration ||
        SessionGeneration != TransportSessionGeneration ||
        TransportSessionGeneration >= TNumericLimits<int64>::Max() - 1) { return; }
    if (!bServerRetained || ServerRetained.Outcome != ECkCrowdGym_RequestOutcome::Accepted)
    { return; }
    const auto* PC = Cast<APlayerController>(GetOwner());
    if (ServerRetainedController.Get() == PC && ServerRetainedPawn.IsValid() &&
        HasExactServerOwner() && PC->GetPawn() == ServerRetainedPawn.Get()) { return; }
    ServerRetained = {}; bServerRetained = false;
    ServerRetainedController.Reset(); ServerRetainedPawn.Reset();
    ClearServerPending();
    WithdrawSelection();
    ++TransportSessionGeneration;
    Client_SessionTeardownComplete(Generation, SessionGeneration, TransportSessionGeneration);
}

void UCkCrowdGym_Session_UE::Client_SessionTeardownComplete_Implementation(
    int64 Generation, int64 PreviousSessionGeneration, int64 NewSessionGeneration)
{
    if (!HasLocalTransportOwner() || !bSessionTeardownPending ||
        Generation != LocalTeardownWorldGeneration ||
        PreviousSessionGeneration != LocalTeardownSessionGeneration ||
        !ck_crowd_gym_transport::IsSerial(NewSessionGeneration) ||
        !ck_crowd_gym_transport::IsSerial(PreviousSessionGeneration) ||
        NewSessionGeneration != PreviousSessionGeneration + 1) { return; }
    TransportSessionGeneration = NewSessionGeneration;
    LocalRetained = {}; bLocalRetained = false; LocalRetainedPawn.Reset();
    LocalPendingSerial = 0; LocalPendingGeneration = 0; LocalPendingPawn.Reset();
    LatestViewRows = {}; LatestViewId = {}; LatestViewDetail = {};
    LatestViewSelectionSerial = 0; LocalSelectionViewHigh = 0;
    bHasLatestView = false; bLatestViewHasDetail = false;
    LocalTeardownWorldGeneration = 0; LocalTeardownSessionGeneration = 0;
    bSessionTeardownPending = false;
}

void UCkCrowdGym_Session_UE::Server_AcknowledgeCommit_Implementation(
    int64 Generation, int64 DeliverySerial, int64 SessionGeneration)
{
    if (!HasServerTransportOwner() || !bServerRetained ||
        SessionGeneration != TransportSessionGeneration ||
        ServerRetained.Snapshot.WorldGeneration != Generation ||
        ServerRetained.DeliverySerial != DeliverySerial) { return; }
    if (ServerRetained.Outcome == ECkCrowdGym_RequestOutcome::Accepted)
    {
        const auto* PC = Cast<APlayerController>(GetOwner());
        if (!HasExactServerOwner() || ServerRetainedController.Get() != PC ||
            !ServerRetainedPawn.IsValid() || PC->GetPawn() != ServerRetainedPawn.Get()) { return; }
    }
    ServerRetained = {};
    bServerRetained = false;
    ServerRetainedController.Reset(); ServerRetainedPawn.Reset();
}

void UCkCrowdGym_Session_UE::RetireForWorld(const ACkCrowdGym_GameState_UE& State, int64 Generation)
{
    if (GetWorld() != State.GetWorld() || GetCrowdState() != &State || Generation < 0)
    { return; }
    // The exact GameState's terminal transition dominates every pending/retained owner package.
    ServerRetained = {}; LocalRetained = {};
    ServerRetainedController.Reset(); ServerRetainedPawn.Reset();
    LocalPendingPawn.Reset(); LocalRetainedPawn.Reset();
    ClearServerPending(); LocalPendingSerial = 0; LocalPendingGeneration = 0;
    LocalTeardownWorldGeneration = 0; LocalTeardownSessionGeneration = 0;
    bServerRetained = false; bLocalRetained = false; bSessionTeardownPending = false;
    LatestViewRows = {}; LatestViewDetail = {}; LatestViewId = {};
    LatestViewSelectionSerial = 0; bHasLatestView = false; bLatestViewHasDetail = false;
    ServerSelectedId = {}; ServerSelectionSerial = 0;
    ControlAccepted = 0; SelectionAccepted = 0;
    if (TransportSessionGeneration < TNumericLimits<int64>::Max() - 1)
    { ++TransportSessionGeneration; }
}

void UCkCrowdGym_Session_UE::EndPlay(const EEndPlayReason::Type Reason)
{
    ServerRetained = {}; LocalRetained = {};
    LatestViewRows = {}; LatestViewDetail = {}; LatestViewId = {};
    bHasLatestView = false; bLatestViewHasDetail = false;
    bServerRetained = false; bLocalRetained = false;
    ClearServerPending(); LocalPendingSerial = 0;
    LocalPendingGeneration = 0;
    LocalPendingPawn.Reset(); LocalRetainedPawn.Reset();
    ServerRetainedController.Reset(); ServerRetainedPawn.Reset();
    bSessionTeardownPending = false;
    Super::EndPlay(Reason);
}
