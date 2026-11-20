#include "CkHexPackagedAcceptance.h"

#include "CkGrid/2dGridSystem/Grid/Ck2dGridSystem_Utils.h"
#include "CkGrid/2dGridSystem/Occupancy/Ck2dGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"
#include "CkTests/Net/CkAutoTest_NetSubject_MixedGrid.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProperties.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
    using FBoard = ACk_AutoTest_NetSubject_MixedGrid_UE;
    using FPiece = ACk_AutoTest_NetSubject_MixedGridObject_UE;
    using FGrid = UCk_Utils_HexGrid_UE;
    using FOccupancy = UCk_Utils_HexGridOccupancy_UE;
    using FSquare = UCk_Utils_2dGridOccupancy_UE;
    const FCk_HexCoord Anchor{1, 0};
    const FCk_HexCoord BlockedMoveDestination{-1, 0};
    const FIntPoint SquareAnchor{2, 3};
    constexpr auto Rotation = ECk_HexRotation::Turn2;

    template<typename T>
    auto GetSubjects(UWorld* World) -> TArray<T*>
    {
        TArray<T*> Subjects;
        for (TActorIterator<T> It(World); It; ++It) { Subjects.Add(*It); }
        return Subjects;
    }

    bool CanPick(const FCk_Handle_HexGrid& Board)
    {
        FVector Center;
        FCk_Handle_HexCell Expected, Picked;
        ECk_HexGeometryFailure Spatial;
        ECk_HexBoardFailure Failure;
        return FGrid::TryGet_CellWorldCenter(Board, Anchor, Center, Failure)
            && FGrid::TryGet_CellAt(Board, Anchor, Expected, Failure)
            && FGrid::TryGet_CellFromWorldPoint(Board, Center, Picked, Spatial, Failure)
            && ck::IsValid(Expected) && Expected == Picked;
    }

    bool IsBlocked(const FCk_Handle_HexGrid& Board)
    {
        FCk_Handle_HexCell Cell;
        ECk_HexBoardFailure Failure;
        bool Disabled = false;
        return FGrid::TryGet_CellAt(Board, BlockedMoveDestination, Cell, Failure)
            && UCk_Utils_HexCell_UE::TryGet_IsDisabled(Cell, Disabled) && Disabled;
    }

    FString ReadIdentity(FBoard* Board, FPiece* Piece)
    {
        const auto Grid = Board->Get_HexGrid();
        const auto Square = Board->Get_SquareGrid();
        const auto Object = Piece->Get_HexObject();
        if (!ck::IsValid(Grid) || !FGrid::Get_IsReady(Grid) || !ck::IsValid(Square) || !ck::IsValid(Object)
            || !CanPick(Grid)) { return {}; }
        const auto SquarePlacement = FSquare::Get_PlacementAt(Square, SquareAnchor);
        if (!ck::IsValid(SquarePlacement) || FSquare::Get_OccupantAt(Square, SquareAnchor) != Object
            || FSquare::Get_PlacementForOccupant(Object) != SquarePlacement || FSquare::Get_Placements(Square).Num() != 1)
        { return {}; }
        const auto At = FOccupancy::TryGet_PlacementAt(Grid, Anchor);
        const auto ByObject = FOccupancy::TryGet_PlacementForOccupant(Object);
        const auto All = FOccupancy::Get_Placements(Grid);
        const auto& Row = At.Get_Placement();
        if (!At.Get_Succeeded() || !ByObject.Get_Succeeded() || !All.Get_Succeeded() || All.Get_Placements().Num() != 1
            || !At.Get_PlacementId().IsValid() || At.Get_PlacementId() != ByObject.Get_PlacementId()
            || Row.Get_OccupantState() != ECk_HexGridOccupantState::Bound || Row.Get_Occupant() != Object
            || !Row.Get_OccupantBindingId().IsValid() || Row.Get_Anchor() != Anchor || Row.Get_Rotation() != Rotation
            || Row.Get_Cells() != TArray<FCk_HexCoord>{Anchor}) { return {}; }
        return At.Get_PlacementId().ToString() + TEXT(" ") + Row.Get_OccupantBindingId().ToString();
    }

    bool CaptureHandles(FBoard* Board, FPiece* Piece, TArray<FCk_Handle>& OutHandles)
    {
        const auto Hex = Board->Get_HexGrid();
        const auto Square = Board->Get_SquareGrid();
        const auto HexCells = FGrid::Get_Cells(Hex);
        const auto SquareCells = UCk_Utils_2dGridSystem_UE::Get_AllCells(Square, ECk_2dGridSystem_CellFilter::NoFilter);
        const auto At = FOccupancy::TryGet_PlacementAt(Hex, Anchor);
        if (!At.Get_Succeeded() || HexCells.IsEmpty() || SquareCells.IsEmpty()) { return false; }
        OutHandles = {Hex, Square, Piece->Get_HexObject(), FSquare::Get_PlacementAt(Square, SquareAnchor)};
        const auto Placement = At.Get_Placement().Get_Handle();
        if (Board->HasAuthority() || ck::IsValid(Placement)) { OutHandles.Add(Placement); }
        for (const auto& Cell : HexCells) { OutHandles.Add(Cell); }
        for (const auto& Cell : SquareCells) { OutHandles.Add(Cell); }
        return !OutHandles.ContainsByPredicate([](const FCk_Handle& Handle){ return !ck::IsValid(Handle); });
    }

    bool AnyEntityStillAllocated(const TArray<FCk_Handle>& Handles)
    {
        return Handles.ContainsByPredicate([](const FCk_Handle& Handle)
        { return ck::IsValid(Handle, ck::IsValid_Policy_IncludePendingKill{}); });
    }
}

bool UCk_HexPackagedAcceptance_UE::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_DEVELOPMENT
    return Super::ShouldCreateSubsystem(Outer) && FPlatformProperties::RequiresCookedData()
        && FParse::Param(FCommandLine::Get(), TEXT("CkHexPackagedAcceptance"));
#else
    return false;
#endif
}

void UCk_HexPackagedAcceptance_UE::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Server = IsRunningDedicatedServer();
    FString Run, Base;
    FGuid Id;
    if (!FParse::Value(FCommandLine::Get(), TEXT("CkHexRun="), Run) || !FGuid::ParseExact(Run, EGuidFormats::Digits, Id)
        || !Id.IsValid() || !FParse::Value(FCommandLine::Get(), TEXT("CkHexEvidence="), Base) || FPaths::IsRelative(Base))
    { Finish(false, TEXT("provide a fresh 32-digit run GUID and absolute shared evidence directory")); return; }
    Directory = FPaths::Combine(Base, Id.ToString(EGuidFormats::Digits));
    Slot = FName(*(TEXT("HexPackaged_") + Run));
    auto& Files = IFileManager::Get();
    if (Server && (Files.DirectoryExists(*Directory) || !Files.MakeDirectory(*Directory, true)))
    { Finish(false, TEXT("run directory reused or unavailable")); return; }
    Advance(EStep::Boot, TEXT("started cooked Development driver"));
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.1f);
}

void UCk_HexPackagedAcceptance_UE::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    Super::Deinitialize();
}

bool UCk_HexPackagedAcceptance_UE::WriteReceipt(const TCHAR* Name, const FString& Value) const
{
    const FString Final = FPaths::Combine(Directory, Name);
    const FString Temp = Final + TEXT(".tmp");
    return FFileHelper::SaveStringToFile(Value, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        && IFileManager::Get().Move(*Final, *Temp, false);
}

FString UCk_HexPackagedAcceptance_UE::ReadReceipt(const TCHAR* Name) const
{
    FString Value;
    const auto File = FPaths::Combine(Directory, Name);
    if (IFileManager::Get().FileExists(*File)) { FFileHelper::LoadFileToString(Value, *File); }
    return Value;
}

void UCk_HexPackagedAcceptance_UE::Advance(EStep Next, const TCHAR* Evidence)
{
    Step = Next;
    Deadline = FPlatformTime::Seconds() + (Next == EStep::LateJoin ? 300.0 : 120.0);
    UE_LOG(LogTemp, Display, TEXT("HexPackaged [%s] %s"), Server ? TEXT("server") : TEXT("client"), Evidence);
}

bool UCk_HexPackagedAcceptance_UE::Finish(bool Success, const TCHAR* Reason)
{
    UE_LOG(LogTemp, Display, TEXT("HexPackaged [%s] %s step=%d %s"), Server ? TEXT("server") : TEXT("client"),
        Success ? TEXT("PASS") : TEXT("FAIL"), static_cast<int32>(Step), Reason);
    Step = EStep::Done;
    if (GLog) { GLog->Flush(); }
    FPlatformMisc::RequestExitWithStatus(false, Success ? 0 : 1);
    return false;
}

void UCk_HexPackagedAcceptance_UE::OnMove(const FCk_HexGridPlacement_Result& InResult)
{
    if (Step != EStep::Move || BlockedCallback || InResult.Get_Succeeded() || InResult.Get_Failure() != ECk_HexGridFailure::Blocked)
    { Finish(false, TEXT("move did not return a single Blocked terminal result")); return; }
    BlockedCallback = true;
}

bool UCk_HexPackagedAcceptance_UE::Tick(float)
{
    if (Step == EStep::Done) { return false; }
    if (FPlatformTime::Seconds() > Deadline) { return Finish(false, TEXT("step timed out")); }
    auto* World = GetGameInstance()->GetWorld();
    if (!IsValid(World) || !World->HasBegunPlay() || World->bIsTearingDown) { return true; }
    auto* Driver = World->GetNetDriver();
    if (!Driver || World->GetNetMode() != (Server ? NM_DedicatedServer : NM_Client)) { return true; }
    if (Server && Step < EStep::LateJoin && Driver->ClientConnections.Num() != 0)
    { return Finish(false, TEXT("client connected before restored authority was ready")); }
    if (!Server && (!Driver->ServerConnection || Driver->ServerConnection->GetConnectionState() != USOCK_Open)) { return true; }
    if (Server && (FApp::CanEverRender() || FModuleManager::Get().IsModuleLoaded(TEXT("CkTestsEditor"))
        || FModuleManager::Get().IsModuleLoaded(TEXT("CkGridEditor")) || FModuleManager::Get().IsModuleLoaded(TEXT("UnrealEd"))))
    { return Finish(false, TEXT("dedicated process has rendering or editor modules active")); }
    auto Boards = GetSubjects<FBoard>(World);
    auto Pieces = GetSubjects<FPiece>(World);
    if (Boards.Num() > 1 || Pieces.Num() > 1) { return Finish(false, TEXT("duplicate mixed-grid actors")); }
    if (Step == EStep::Boot && !Server)
    {
        OpenConnectionObservedAt = FPlatformTime::Seconds();
        Advance(EStep::LateJoin, TEXT("connected late client"));
    }
    else if (Step == EStep::Boot)
    {
        if (!Boards.IsEmpty() || !Pieces.IsEmpty() || !World->SpawnActor<FBoard>() || !World->SpawnActor<FPiece>())
        { return Finish(false, TEXT("failed to spawn unique actors into clean map")); }
        Advance(EStep::Compose, TEXT("spawned board and independent object"));
        return true;
    }
    if (Step == EStep::Teardown)
    {
        if (!Boards.IsEmpty() || !Pieces.IsEmpty() || AnyEntityStillAllocated(PriorHandles))
        { return true; }
        if (!Server && !WriteReceipt(TEXT("client-teardown"), Identity)) { return Finish(false, TEXT("client teardown receipt failed")); }
        if (Server && ReadReceipt(TEXT("client-teardown")) != Identity) { return true; }
        return Finish(true, TEXT("both actors and their local handles drained"));
    }
    if (Boards.IsEmpty() || Pieces.IsEmpty()) { return true; }
    auto* Board = Boards[0];
    auto* Piece = Pieces[0];
    auto Grid = Board->Get_HexGrid();
    if (!ck::IsValid(Grid) || !FGrid::Get_IsReady(Grid) || !ck::IsValid(Piece->Get_HexObject()) || !ck::IsValid(Board->Get_SquareGrid())) { return true; }
    if (Step == EStep::Compose)
    {
        if (!CanPick(Grid) || !Board->Request_PlaceSquare(Piece, SquareAnchor) || !Board->Request_PlaceHex(Piece, Anchor, Rotation))
        { return Finish(false, TEXT("cell pick or public placement submission failed")); }
        Advance(EStep::Place, TEXT("picked cell and submitted square and hex placements"));
        return true;
    }
    if (Step == EStep::Place)
    {
        Identity = ReadIdentity(Board, Piece);
        if (Identity.IsEmpty()) { return true; }
        FCk_Handle_HexCell Cell;
        ECk_HexBoardFailure Failure;
        if (!FGrid::TryGet_CellAt(Grid, BlockedMoveDestination, Cell, Failure)) { return Finish(false, TEXT("missing move destination")); }
        UCk_Utils_HexCell_UE::Request_SetBlocked(Cell, true, {});
        Advance(EStep::Block, TEXT("both accepted placements resolve to the independent object"));
        return true;
    }
    if (Step == EStep::Block)
    {
        if (!IsBlocked(Grid)) { return true; }
        const auto Row = FOccupancy::TryGet_PlacementAt(Grid, Anchor);
        if (!Row.Get_Succeeded()) { return Finish(false, TEXT("source placement disappeared")); }
        FCk_Delegate_HexGridPlacement_OnResult Result;
        Result.BindDynamic(this, &ThisClass::OnMove);
        FCk_Request_HexGridPlacement_Move Move{Row.Get_PlacementId(), BlockedMoveDestination, Rotation};
        Move.Set_OnResult(Result);
        Advance(EStep::Move, TEXT("blocked destination through public cell request"));
        UCk_Utils_HexGridPlacement_UE::Request_Move(Grid, Move, {});
        return true;
    }
    auto* Snapshot = GetGameInstance()->GetSubsystem<UCk_Snapshot_Subsystem_UE>();
    if (Step == EStep::Move)
    {
        if (!BlockedCallback) { return true; }
        if (!Snapshot || Snapshot->Get_HasSaveSlot(Slot) || ReadIdentity(Board, Piece) != Identity ||
            FOccupancy::TryGet_PlacementAt(Grid, BlockedMoveDestination).Get_Succeeded()) { return Finish(false, TEXT("move changed occupancy or slot unavailable")); }
        const auto SaveStart = FPlatformTime::Seconds();
        Snapshot->Request_Save(Slot, {});
        SaveRequestMs = (FPlatformTime::Seconds() - SaveStart) * 1000.0;
        const auto Save = Snapshot->Get_LastSaveReport();
        if (!Snapshot->Get_HasSaveSlot(Slot) || Save.Get_Result() != ECk_SnapshotResult::Success || !Save.Get_Losses().IsEmpty())
        { return Finish(false, TEXT("disk save failed or lost durable data")); }
        PriorWorld = World;
        if (!CaptureHandles(Board, Piece, PriorHandles)) { return Finish(false, TEXT("could not capture live pre-travel entities")); }
        LoadStartedAt = FPlatformTime::Seconds();
        Snapshot->Request_Load(Slot, {});
        if (!Snapshot->Get_IsLoadInProgress()) { return Finish(false, TEXT("snapshot load did not start")); }
        Advance(EStep::Restore, TEXT("rejected move atomically and saved authority to a fresh disk slot"));
        return true;
    }
    if (Step == EStep::Restore)
    {
        if (PriorWorld.Get() == World || !Snapshot || Snapshot->Get_IsLoadInProgress()) { return true; }
        const auto Load = Snapshot->Get_LastLoadReport();
        if (Load.Get_Result() != ECk_SnapshotResult::Success || !Load.Get_IsAccountingClosed() || !Load.Get_PayloadLosses().IsEmpty())
        { return Finish(false, TEXT("load reported incomplete accounting or loss")); }
        if (AnyEntityStillAllocated(PriorHandles)
            || ReadIdentity(Board, Piece) != Identity || !IsBlocked(Grid)) { return true; }
        const auto RestoreObservedMs = (FPlatformTime::Seconds() - LoadStartedAt) * 1000.0;
        TArray<uint8> MainSlot;
        if (!UGameplayStatics::LoadDataFromSlot(MainSlot, Slot.ToString(), 0) || MainSlot.IsEmpty())
        { return Finish(false, TEXT("could not read the completed primary save slot for its byte count")); }
        const auto ServerMetrics = FString::Printf(TEXT("save_request_ms,load_request_to_restored_state_observed_ms,main_slot_bytes\n%.6f,%.6f,%d\n"),
            SaveRequestMs, RestoreObservedMs, MainSlot.Num());
        if (!WriteReceipt(TEXT("server-metrics.csv"), ServerMetrics) || !WriteReceipt(TEXT("server-ready"), Identity))
        { return Finish(false, TEXT("server measurements or ready receipt failed")); }
        Advance(EStep::LateJoin, TEXT("restored identities, cells, blocker and square control on fresh authority world with zero connections"));
        return true;
    }
    if (Step == EStep::LateJoin)
    {
        const auto Observed = ReadIdentity(Board, Piece);
        if (Observed.IsEmpty() || !IsBlocked(Grid)) { return true; }
        if (!Server)
        {
            if (Board->Get_ConstructReadiness().Get_State() != ECk_HexGridReadiness::AwaitingReplication)
            { return Finish(false, TEXT("late client published a board before receiving its replication baseline")); }
            const auto ObservedMs = (FPlatformTime::Seconds() - OpenConnectionObservedAt) * 1000.0;
            if (ReadReceipt(TEXT("server-ready")) != Observed) { return true; }
            Identity = Observed;
            const auto ClientMetrics = FString::Printf(TEXT("open_game_connection_observed_to_validated_baseline_observed_ms\n%.6f\n"), ObservedMs);
            if (!WriteReceipt(TEXT("client-metrics.csv"), ClientMetrics) || !WriteReceipt(TEXT("client-observed"), Observed))
            { return Finish(false, TEXT("client measurements or observation receipt failed")); }
        }
        else
        {
            if (Driver->ClientConnections.Num() != 1 || !Driver->ClientConnections[0]
                || Driver->ClientConnections[0]->GetConnectionState() != USOCK_Open || ReadReceipt(TEXT("client-observed")) != Identity) { return true; }
            if (Observed != Identity) { return Finish(false, TEXT("authority changed during late join")); }
        }
        if (!CaptureHandles(Board, Piece, PriorHandles)) { return Finish(false, TEXT("could not capture live teardown entities")); }
        if (Server && (!Piece->Destroy() || !Board->Destroy())) { return Finish(false, TEXT("authority actor teardown rejected")); }
        Advance(EStep::Teardown, TEXT("late client's local square and hex placements agree with restored authority"));
    }
    return true;
}
