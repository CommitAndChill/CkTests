#include "CkHexDenseAcceptance.h"
#include "CkActorRelay/CkActorRelay_Actor.h"
#include "CkActorRelay/CkActorRelay_GroupSubsystem.h"
#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"
#include "CkEcs/Snapshot/CkSaveKey_Fragment.h"
#include "Misc/AutomationTest.h"

#include "CkGrid/HexGridSystem/Geometry/CkHexGrid_Geometry_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Fragment.h"
#include "CkGrid/HexGridSystem/Persistence/CkHexGridPersistence_Fragment_Data.h"
#include "CkSnapshot/Inspection/CkSnapshot_Inspection.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProperties.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

namespace
{
    using FGrid = UCk_Utils_HexGrid_UE;
    using FOcc = UCk_Utils_HexGridOccupancy_UE;
    constexpr int32 BoardCount = 2;
    constexpr int32 CellsPerBoard = 3169;
    constexpr int32 Batch = 32;

    FGuid GenericRelayKey()
    {
        return FGuid::NewDeterministicGuid(TEXT("ActorRelay.Generic"));
    }

    bool GatherGenericRelayIds(UWorld* World, TSet<uint32>& OutIds)
    {
        OutIds.Reset();
        int32 GroupActors = 0;
        for (TActorIterator<ACk_ActorRelay_UE> It(World); It; ++It)
        {
            const auto* Group = It->Get_GroupSubsystem().Get();
            if (!Group) { return false; }
            if (Group->Get_GroupTag().GetTagName() != FName(TEXT("ActorRelay.Generic"))) { continue; }
            ++GroupActors;
            const auto Entity = UCk_Utils_EntityBridge_UE::TryGet_Entity(*It);
            if (!ck::IsValid(Entity) || !Entity.Has<FFragment_SaveKey>()) { return false; }
            const auto& Key = Entity.Get<FFragment_SaveKey>();
            if (!Key.Get_IsSharedRendezvousGroup() || Key.Get_Key() != GenericRelayKey()) { return false; }
            OutIds.Add(static_cast<uint32>(Entity.Get_Entity().Get_ID()));
        }
        return GroupActors == 2 && OutIds.Num() == 2 && !OutIds.Contains(0) && !OutIds.Contains(MAX_uint32);
    }

    bool MatchesCapturedRelayCollision(const FCk_SnapshotInspection_Document& Doc, const TSet<uint32>& LiveIds)
    {
        if (!Doc.Get_TableParseSucceeded() || Doc.Get_ErrorCount() != 1 || LiveIds.Num() != 2 ||
            LiveIds.Contains(0) || LiveIds.Contains(MAX_uint32)) { return false; }
        const auto* Error = Doc.Get_Diagnostics().FindByPredicate([](const auto& Item)
        { return Item.Get_Severity() == ECk_SnapshotInspection_Severity::Error; });
        if (!Error || Error->Get_Code() != ECk_SnapshotInspection_DiagnosticCode::DuplicateSaveKey ||
            !Error->Get_RelatedPayloadIndices().IsEmpty()) { return false; }
        const auto& Related = Error->Get_RelatedSavedIds();
        if (Related.Num() != 2 || Related[0] == Related[1] ||
            !LiveIds.Contains(Related[0]) || !LiveIds.Contains(Related[1])) { return false; }
        TSet<uint32> Found;
        for (const auto& Summary : Doc.Get_Entities())
        {
            const auto& Entry = Summary.Get_Entry();
            if (Entry.Get_SaveKey() != GenericRelayKey()) { continue; }
            const auto Id = Entry.Get_SavedId();
            if (Entry.Get_Provenance() != ECk_Snapshot_V3_Provenance::EngineOwned ||
                !LiveIds.Contains(Id) || Found.Contains(Id)) { return false; }
            Found.Add(Id);
        }
        if (Found.Num() != 2) { return false; }
        for (const auto& Item : Doc.Get_Diagnostics())
        {
            if (Item.Get_Severity() != ECk_SnapshotInspection_Severity::Warning) { continue; }
            // Dense actors deliberately repeat a spawn recipe. Their persisted payloads and GUIDs are
            // checked individually. All other warnings remain disqualifying.
            if (Item.Get_Code() != ECk_SnapshotInspection_DiagnosticCode::DuplicateRecipeIdentity ||
                Item.Get_RelatedSavedIds().Num() < 2) { return false; }
            for (const uint32 Id : Item.Get_RelatedSavedIds())
            {
                const auto Index = Doc.TryGet_EntityIndexForSavedId(Id);
                if (!Doc.Get_Entities().IsValidIndex(Index)) { return false; }
                const auto& Row = Doc.Get_Entities()[Index].Get_Entry();
                if (Row.Get_Provenance() != ECk_Snapshot_V3_Provenance::RuntimeSpawned ||
                    !(Row.Get_ActorClassPath() == ACk_AutoTest_NetSubject_DenseHexBoard_UE::StaticClass()->GetPathName() ||
                      Row.Get_ActorClassPath() == ACk_AutoTest_NetSubject_DenseHexSecondBoard_UE::StaticClass()->GetPathName() ||
                      Row.Get_ActorClassPath() == ACk_AutoTest_NetSubject_MixedGridObject_UE::StaticClass()->GetPathName()))
                { return false; }
            }
        }
        return !Doc.Get_Payloads().ContainsByPredicate([&Found](const auto& Payload)
        { return Found.Contains(Payload.Get_Entry().Get_OwnerSavedId()); });
    }

    FString HashManifest(const FString& Manifest)
    {
        const FTCHARToUTF8 Bytes(*Manifest);
        return FSHA1::HashBuffer(Bytes.Get(), Bytes.Length()).ToString();
    }
}

bool UCk_HexDenseAcceptance_UE::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_DEVELOPMENT
    return Super::ShouldCreateSubsystem(Outer) && FPlatformProperties::RequiresCookedData()
        && FParse::Param(FCommandLine::Get(), TEXT("CkHexDenseAcceptance"));
#else
    return false;
#endif
}

void UCk_HexDenseAcceptance_UE::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Server = IsRunningDedicatedServer();
    ObserveRelayCollision = FParse::Param(FCommandLine::Get(), TEXT("CkHexObserveRelayCollision"));
    if (ObserveRelayCollision && !Server)
    {
        // Standalone GameInstance subsystem initialization precedes StartGameInstance/Browse on the staged Game target.
        const auto* Context = GetGameInstance()->GetWorldContext();
        if (!Context || Context->PendingNetGame || (Context->World() && Context->World()->GetNetDriver()))
        { Finish(false, TEXT("observation did not initialize before network travel")); return; }
        BeforeClientBrowse = FPlatformTime::Seconds();
    }
    FString Run, Base;
    FGuid RunId;
    int32 Density = 0;
    const auto* Command = FCommandLine::Get();
    if (!FParse::Value(Command,TEXT("CkHexDensity="),Density) || (Density != 50 && Density != 90) ||
        !FParse::Value(Command,TEXT("CkHexRun="),Run) || !FGuid::ParseExact(Run,EGuidFormats::Digits,RunId) || !RunId.IsValid() ||
        !FParse::Value(Command,TEXT("CkHexEvidence="),Base) || FPaths::IsRelative(Base))
    { Finish(false,TEXT("missing density, run identity or absolute evidence path")); return; }
    FString ControlName;
    if (FParse::Value(Command, TEXT("CkHexRestoreControl="), ControlName))
    {
        if (!Server || !ObserveRelayCollision)
        { Finish(false, TEXT("restore control requires dedicated authority and inspection observation")); return; }
        if (ControlName == TEXT("EmptyBoards")) { RestoreControl = ERestoreControl::EmptyBoards; }
        else if (ControlName == TEXT("UnplacedActors")) { RestoreControl = ERestoreControl::UnplacedActors; }
        else { Finish(false, TEXT("unknown authority restore control")); return; }
    }
    Filled = CellsPerBoard * Density / 100;
    Directory = FPaths::Combine(Base,RunId.ToString(EGuidFormats::Digits));
    Slot = FName(*(TEXT("HexDense_")+Run));
    if (Server && (IFileManager::Get().DirectoryExists(*Directory) || !IFileManager::Get().MakeDirectory(*Directory,true)))
    { Finish(false,TEXT("evidence directory reused or unavailable")); return; }
    FCk_HexDomain Domain;
    Domain.Set_RingRadius(32);
    ECk_HexGeometryFailure Failure{};
    if (!UCk_Utils_HexGeometry_UE::TryResolve_Domain(Domain,Coordinates,Failure) || Coordinates.Num()!=CellsPerBoard)
    { Finish(false,TEXT("R32 geometry did not produce 3169 addresses")); return; }
    for (int32 I=0; I<Coordinates.Num(); ++I) { Indices.Add(Coordinates[I],I); }
    Advance(EStep::Boot);
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::Tick));
}

void UCk_HexDenseAcceptance_UE::Deinitialize()
{
    FTSTicker::RemoveTicker(Ticker);
    Super::Deinitialize();
}

void UCk_HexDenseAcceptance_UE::Advance(EStep Next)
{
    Step = Next;
    Deadline = FPlatformTime::Seconds() + 900.;
    UE_LOG(LogTemp,Display,TEXT("HexDense [%s] phase=%d"),Server ? TEXT("server") : TEXT("client"),int32(Next));
}

bool UCk_HexDenseAcceptance_UE::Finish(bool Success, const TCHAR* Reason)
{
    UE_LOG(LogTemp,Display,TEXT("HexDense [%s] %s phase=%d cursor=%d %s"),Server ? TEXT("server") : TEXT("client"),
        Success ? (RestoreControl != ERestoreControl::None ? TEXT("CONTROL_VERIFIED") :
            (ObserveRelayCollision ? TEXT("OBSERVED_INSPECTION_REJECTED") : TEXT("PASS"))) : TEXT("FAIL"),int32(Step),Cursor,Reason);
    Step = EStep::Done;
    if (GLog) { GLog->Flush(); }
    FPlatformMisc::RequestExitWithStatus(false,Success ? 0 : 1);
    return false;
}

bool UCk_HexDenseAcceptance_UE::Write(const TCHAR* Name, const FString& Text) const
{
    const FString Path = FPaths::Combine(Directory, ObserveRelayCollision ? FString(TEXT("observation-")) + Name : FString(Name));
    const FString Temp = Path + TEXT(".tmp");
    return FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        && IFileManager::Get().Move(*Path,*Temp,false);
}

FString UCk_HexDenseAcceptance_UE::Read(const TCHAR* Name) const
{
    FString Value;
    const auto Path = FPaths::Combine(Directory, ObserveRelayCollision ? FString(TEXT("observation-")) + Name : FString(Name));
    if (IFileManager::Get().FileExists(*Path)) { FFileHelper::LoadFileToString(Value,*Path); }
    return Value;
}

bool UCk_HexDenseAcceptance_UE::WitnessesReleased() const
{
    return !LifetimeWitnesses.ContainsByPredicate([](const FCk_Handle& Handle)
    { return ck::IsValid(Handle,ck::IsValid_Policy_IncludePendingKill{}); });
}

bool UCk_HexDenseAcceptance_UE::Verify(UWorld* World, FString& OutManifest)
{
    if (RestoreControl != ERestoreControl::None) { return VerifyControl(World, OutManifest); }
    TArray<FCk_Handle> Captured;
    TSet<FCk_Handle> Objects;
    for (TActorIterator<FPiece> It(World); It; ++It)
    {
        const auto Object = It->Get_HexObject();
        if (!ck::IsValid(Object)) { return false; }
        Objects.Add(Object);
        Captured.Add(Object);
    }
    if (Objects.Num()!=BoardCount*Filled) { return false; }
    TSet<FGuid> Placements, Bindings;
    TArray<FString> Lines;
    uint32 BoardMask = 0;
    for (TActorIterator<FBoard> It(World); It; ++It)
    {
        const auto Grid = It->Get_HexGrid();
        if (!ck::IsValid(Grid) || !FGrid::Get_IsReady(Grid) ||
            (!Server && It->Get_ConstructReadiness().Get_State()!=ECk_HexGridReadiness::AwaitingReplication))
        { return false; }
        const auto All = FOcc::Get_Placements(Grid);
        const auto Cells = FGrid::Get_Cells(Grid);
        if (!All.Get_Succeeded() || All.Get_Placements().Num()!=Filled || Cells.Num()!=CellsPerBoard)
        { return false; }
        Captured.Add(Grid);
        for (const auto& Cell : Cells)
        {
            if (!ck::IsValid(Cell)) { return false; }
            Captured.Add(Cell);
        }
        const int32 BoardIndex = It->IsA<ACk_AutoTest_NetSubject_DenseHexSecondBoard_UE>() ? 1 : 0;
        const uint32 Mask = 1u << BoardIndex;
        if (BoardMask & Mask) { return false; }
        BoardMask |= Mask;
        TSet<int32> Seen;
        for (const auto& Row : All.Get_Placements())
        {
            const auto* I = Indices.Find(Row.Get_Anchor());
            if (!I || (BoardIndex==0 ? *I>=Filled : *I<CellsPerBoard-Filled) || Seen.Contains(*I) ||
                Row.Get_Rotation()!=ECk_HexRotation::Turn0 || Row.Get_Cells().Num()!=1 || Row.Get_Cells()[0]!=Row.Get_Anchor() ||
                Row.Get_OccupantState()!=ECk_HexGridOccupantState::Bound || !Objects.Remove(Row.Get_Occupant()) ||
                !Row.Get_PlacementId().IsValid() || !Row.Get_OccupantBindingId().IsValid() ||
                ck::hex_grid::GetAcceptedObjectBindingId(Row.Get_Occupant())!=Row.Get_OccupantBindingId() ||
                Placements.Contains(Row.Get_PlacementId()) || Bindings.Contains(Row.Get_OccupantBindingId()))
            { return false; }
            if (Step==EStep::Capture)
            {
                const int32 Original = BoardIndex==0 ? *I : Filled+CellsPerBoard-1-*I;
                if (!Pieces.IsValidIndex(Original) || !Pieces[Original].IsValid() ||
                    Pieces[Original]->Get_HexObject()!=Row.Get_Occupant()) { return false; }
            }
            if (!ck::IsValid(Row.Get_Handle())) { return false; }
            Captured.Add(Row.Get_Handle());
            const auto At = FOcc::TryGet_PlacementAt(Grid,Row.Get_Anchor());
            const auto ByObject = FOcc::TryGet_PlacementForOccupant(UCk_Utils_HexGridObject_UE::Cast(Row.Get_Occupant()));
            if (!At.Get_Succeeded() || !ByObject.Get_Succeeded() ||
                At.Get_PlacementId()!=Row.Get_PlacementId() || ByObject.Get_PlacementId()!=Row.Get_PlacementId())
            { return false; }
            Seen.Add(*I);
            Placements.Add(Row.Get_PlacementId());
            Bindings.Add(Row.Get_OccupantBindingId());
            Lines.Add(FString::Printf(TEXT("%d,%d,%d,%s,%s"),BoardIndex,Row.Get_Anchor().Get_Q(),Row.Get_Anchor().Get_R(),
                *Row.Get_PlacementId().ToString(),*Row.Get_OccupantBindingId().ToString()));
        }
    }
    if (!Objects.IsEmpty() || BoardMask!=3 || Lines.Num()!=BoardCount*Filled) { return false; }
    Lines.Sort();
    OutManifest = TEXT("board,q,r,placement_id,binding_id\n") + FString::Join(Lines,TEXT("\n")) + TEXT("\n");
    LifetimeWitnesses = MoveTemp(Captured);
    return true;
}

// The travel control keeps both cooked radius-32 boards. The actor control also keeps
// the same mixed actor classes and object payloads, removing only their placements.
bool UCk_HexDenseAcceptance_UE::VerifyControl(UWorld* World, FString& OutManifest)
{
    TArray<FCk_Handle> Witnesses;
    TSet<FGuid> ObjectIds;
    TArray<FString> Lines;
    int32 BoardMask = 0;
    for (TActorIterator<FBoard> It(World); It; ++It)
    {
        const auto Board = It->Get_HexGrid();
        if (!ck::IsValid(Board) || !FGrid::Get_IsReady(Board)) { return false; }
        const auto ExpectedReadiness = Step == EStep::Capture ? ECk_HexGridReadiness::Ready : ECk_HexGridReadiness::AwaitingRestore;
        if (It->Get_ConstructReadiness().Get_State() != ExpectedReadiness) { return false; }
        const auto Placements = FOcc::Get_Placements(Board);
        const auto Cells = FGrid::Get_Cells(Board);
        if (!Placements.Get_Succeeded() || !Placements.Get_Placements().IsEmpty() || Cells.Num() != CellsPerBoard)
        { return false; }
        const int32 BoardIndex = It->IsA<ACk_AutoTest_NetSubject_DenseHexSecondBoard_UE>() ? 1 : 0;
        const int32 Mask = 1 << BoardIndex;
        if (BoardMask & Mask) { return false; }
        BoardMask |= Mask;
        Witnesses.Add(Board);
        for (const auto& Cell : Cells)
        {
            if (!ck::IsValid(Cell)) { return false; }
            Witnesses.Add(Cell);
        }
        Lines.Add(FString::Printf(TEXT("board,%d"), BoardIndex));
    }
    if (BoardMask != 3) { return false; }
    for (TActorIterator<FPiece> It(World); It; ++It)
    {
        const auto Object = It->Get_HexObject();
        if (!ck::IsValid(Object)) { return false; }
        const FGuid Binding = ck::hex_grid::GetAcceptedObjectBindingId(Object);
        if (!Binding.IsValid() || ObjectIds.Contains(Binding)) { return false; }
        ObjectIds.Add(Binding);
        Witnesses.Add(Object);
        Lines.Add(TEXT("object,") + Binding.ToString());
    }
    const int32 ExpectedObjects = RestoreControl == ERestoreControl::EmptyBoards ? 0 : BoardCount * Filled;
    if (ObjectIds.Num() != ExpectedObjects) { return false; }
    Lines.Sort();
    OutManifest = TEXT("kind,identity\n") + FString::Join(Lines, TEXT("\n")) + TEXT("\n");
    LifetimeWitnesses = MoveTemp(Witnesses);
    return true;
}

bool UCk_HexDenseAcceptance_UE::RecordSave(double RestoreMs) const
{
    TArray<uint8> Bytes;
    if (!UGameplayStatics::LoadDataFromSlot(Bytes,Slot.ToString(),0)) { return false; }
    const auto Doc = ck::snapshot::Inspect_SaveBytes(Bytes,Slot.ToString());
    UE_LOG(LogTemp, Display, TEXT("HexDense save inspection: slot=%s sha256=%s parsed=%d errors=%d warnings=%d shared_claims=%d"),
        *Slot.ToString(), *Doc.Get_SourceHashHex(), Doc.Get_TableParseSucceeded(), Doc.Get_ErrorCount(),
        Doc.Get_WarningCount(), Doc.Get_Header().Get_SharedRendezvousClaims().Num());
    if (!Doc.Get_TableParseSucceeded() || Doc.Get_ErrorCount()!=0)
    {
        FString Summary = FString::Printf(TEXT("table_parsed=%d errors=%d\n"),
            Doc.Get_TableParseSucceeded(), Doc.Get_ErrorCount());
        for (const auto& Diagnostic : Doc.Get_Diagnostics())
        {
            if (Diagnostic.Get_Severity() == ECk_SnapshotInspection_Severity::Error)
            {
                Summary += FString::Printf(TEXT("%s %s\n"), ck::snapshot::Get_DiagnosticCodeText(Diagnostic.Get_Code()),
                    *Diagnostic.Get_Message());
            }
        }
        if (ObserveRelayCollision)
        {
            Summary += FString::Printf(TEXT("warnings=%d\n"), Doc.Get_WarningCount());
            for (const auto& Item : Doc.Get_Diagnostics())
            {
                if (Item.Get_Severity() == ECk_SnapshotInspection_Severity::Warning)
                { Summary += FString::Printf(TEXT("%s %s\n"), ck::snapshot::Get_DiagnosticCodeText(Item.Get_Code()), *Item.Get_Message()); }
            }
        }
        const bool SavedRejection = Write(TEXT("inspection-rejected.txt"), Summary);
        TSet<uint32> RestoredRelayIds;
        if (!ObserveRelayCollision || !SavedRejection || !MatchesCapturedRelayCollision(Doc, CapturedRelayIds) ||
            !GatherGenericRelayIds(GetGameInstance()->GetWorld(), RestoredRelayIds)) { return false; }
        if (!Write(TEXT("inspected-slot.csv"), FString::Printf(
            TEXT("slot,bytes,sha256\n%s,%d,%s\n"), *Slot.ToString(), Bytes.Num(), *Doc.Get_SourceHashHex())))
        { return false; }
        FString Witnesses = TEXT("stage,group,key,live_id\n");
        for (const uint32 Id : CapturedRelayIds)
        { Witnesses += FString::Printf(TEXT("capture,ActorRelay.Generic,%s,%u\n"), *GenericRelayKey().ToString(), Id); }
        for (const uint32 Id : RestoredRelayIds)
        { Witnesses += FString::Printf(TEXT("restored,ActorRelay.Generic,%s,%u\n"), *GenericRelayKey().ToString(), Id); }
        if (!Write(TEXT("relay-witnesses.csv"), Witnesses)) { return false; }
    }
    else if (ObserveRelayCollision && RestoreControl == ERestoreControl::None)
    {
        return false;
    }
    else if (RestoreControl != ERestoreControl::None && !Write(TEXT("inspected-slot.csv"), FString::Printf(
        TEXT("slot,bytes,sha256\n%s,%d,%s\n"), *Slot.ToString(), Bytes.Num(), *Doc.Get_SourceHashHex())))
    { return false; }
    const auto BoardType = FCk_RepData_HexGridState::StaticStruct()->GetPathName();
    const auto ObjectType = FCk_RepData_HexGridObjectIdentity::StaticStruct()->GetPathName();
    int32 BoardRows=0,ObjectRows=0;
    int64 BoardBytes=0,ObjectBytes=0;
    TSet<uint32> Owners;
    FString PayloadRows = TEXT("owner_saved_id,type_path,payload_bytes\n");
    for (const auto& Payload : Doc.Get_Payloads())
    {
        const auto& Entry=Payload.Get_Entry();
        if (Entry.Get_TypePath()!=BoardType && Entry.Get_TypePath()!=ObjectType)
        {
            if (ObserveRelayCollision) { return false; }
            continue;
        }
        if (Payload.Get_ByteCount()<=0 || Owners.Contains(Entry.Get_OwnerSavedId())) { return false; }
        Owners.Add(Entry.Get_OwnerSavedId());
        if (ObserveRelayCollision)
        {
            PayloadRows += FString::Printf(TEXT("%u,%s,%d\n"), Entry.Get_OwnerSavedId(),
                *Entry.Get_TypePath(), Payload.Get_ByteCount());
        }
        if (Entry.Get_TypePath()==BoardType) { ++BoardRows; BoardBytes+=Payload.Get_ByteCount(); }
        else { ++ObjectRows; ObjectBytes+=Payload.Get_ByteCount(); }
    }
    const int32 ExpectedObjects = RestoreControl == ERestoreControl::EmptyBoards ? 0 : BoardCount * Filled;
    if (BoardRows != BoardCount || ObjectRows != ExpectedObjects || BoardBytes <= 0 ||
        (ExpectedObjects > 0 && ObjectBytes <= 0)) { return false; }
    if (ObserveRelayCollision && !Write(TEXT("payload-rows.csv"), PayloadRows)) { return false; }
    return Write(TEXT("server-metrics.csv"), FString::Printf(
        TEXT("cells_per_board,placements_per_board,boards,save_request_ms,load_to_verified_manifest_ms,main_slot_bytes,board_payload_rows,board_payload_bytes,object_identity_rows,object_identity_bytes\n")
        TEXT("%d,%d,%d,%.6f,%.6f,%d,%d,%lld,%d,%lld\n"),
        CellsPerBoard,RestoreControl == ERestoreControl::None ? Filled : 0,BoardCount,SaveMs,RestoreMs,Bytes.Num(),BoardRows,BoardBytes,ObjectRows,ObjectBytes));
}

bool UCk_HexDenseAcceptance_UE::Tick(float)
{
    if (Step==EStep::Done) { return false; }
    const auto Now = FPlatformTime::Seconds();
    if (Now>Deadline) { return Finish(false,TEXT("phase deadline exceeded")); }
    auto* World = GetGameInstance()->GetWorld();
    if (!IsValid(World) || World->bIsTearingDown || !World->HasBegunPlay()) { return true; }
    auto* Driver = World->GetNetDriver();
    if (!Driver || World->GetNetMode()!=(Server ? NM_DedicatedServer : NM_Client)) { return true; }
    if (Server && Step<EStep::Join && !Driver->ClientConnections.IsEmpty())
    { return Finish(false,TEXT("client connected before restore acceptance")); }
    if (!Server && (!Driver->ServerConnection || Driver->ServerConnection->GetConnectionState()!=USOCK_Open)) { return true; }
    auto* Snapshot = GetGameInstance()->GetSubsystem<UCk_Snapshot_Subsystem_UE>();
    switch (Step)
    {
    case EStep::Boot:
        if (!Server)
        {
            ConnectedAt=Now;
            Advance(EStep::Join);
            break;
        }
        if (TActorIterator<FBoard>(World) || TActorIterator<FPiece>(World))
        { return Finish(false,TEXT("dense acceptance requires an empty map")); }
        for (int32 I=0; I<BoardCount; ++I)
        {
            auto* Board=World->SpawnActor<FBoard>(I==0 ? FBoard::StaticClass() : ACk_AutoTest_NetSubject_DenseHexSecondBoard_UE::StaticClass());
            if (!Board) { return Finish(false,TEXT("board actor spawn failed")); }
            Boards.Add(Board);
        }
        Advance(RestoreControl == ERestoreControl::EmptyBoards ? EStep::Capture : EStep::Spawn);
        break;
    case EStep::Spawn:
        for (int32 I=0; I<Batch && Pieces.Num()<BoardCount*Filled; ++I)
        {
            auto* Piece=World->SpawnActor<FPiece>();
            if (!Piece) { return Finish(false,TEXT("durable object actor spawn failed")); }
            Pieces.Add(Piece);
        }
        if (Pieces.Num()==BoardCount*Filled) { Advance(RestoreControl == ERestoreControl::UnplacedActors ? EStep::Capture : EStep::Fill); }
        break;
    case EStep::Fill:
        for (int32 I=0; I<Batch && Cursor<Pieces.Num(); ++I)
        {
            const int32 BoardIndex=Cursor/Filled;
            auto* Board=Boards[BoardIndex].Get();
            auto* Piece=Pieces[Cursor].Get();
            if (!Board || !Piece) { return Finish(false,TEXT("a newly spawned actor retired")); }
            if (!ck::IsValid(Piece->Get_HexObject()) || !ck::IsValid(Board->Get_HexGrid()) || !FGrid::Get_IsReady(Board->Get_HexGrid())) { return true; }
            const int32 Index=BoardIndex==0 ? Cursor%Filled : CellsPerBoard-1-Cursor%Filled;
            if (!Board->Request_PlaceHex(Piece,Coordinates[Index],ECk_HexRotation::Turn0))
            { return Finish(false,TEXT("durable placement submission failed")); }
            ++Cursor;
        }
        if (Cursor==Pieces.Num()) { Advance(EStep::Capture); }
        break;
    case EStep::Capture:
    {
        FString Manifest;
        if (!Verify(World,Manifest)) { break; }
        if (!Snapshot || Snapshot->Get_HasSaveSlot(Slot) || !Write(TEXT("authority-manifest.csv"),Manifest))
        { return Finish(false,TEXT("fresh slot or authority manifest unavailable")); }
        ManifestHash=HashManifest(Manifest);
        if (ObserveRelayCollision && !GatherGenericRelayIds(World, CapturedRelayIds))
        {
            CapturedRelayIds.Reset();
            if (RestoreControl == ERestoreControl::None)
            { return Finish(false,TEXT("could not identify two distinct shared generic relay entities before save")); }
        }
        const auto Start=FPlatformTime::Seconds();
        Snapshot->Request_Save(Slot,{});
        SaveMs=(FPlatformTime::Seconds()-Start)*1000.;
        const auto Report=Snapshot->Get_LastSaveReport();
        if (Report.Get_Result()!=ECk_SnapshotResult::Success || !Report.Get_Losses().IsEmpty() || !Snapshot->Get_HasSaveSlot(Slot))
        { return Finish(false,TEXT("authority save failed or reported losses")); }
        PreviousWorld=World;
        Boards.Reset();
        Pieces.Reset();
        LoadStart=FPlatformTime::Seconds();
        Snapshot->Request_Load(Slot,{});
        if (!Snapshot->Get_IsLoadInProgress()) { return Finish(false,TEXT("snapshot load did not start")); }
        Advance(EStep::Restore);
        break;
    }
    case EStep::Restore:
    {
        if (World==PreviousWorld.Get() || !Snapshot || Snapshot->Get_IsLoadInProgress() || !WitnessesReleased()) { break; }
        const auto Report=Snapshot->Get_LastLoadReport();
        if (Report.Get_Result()!=ECk_SnapshotResult::Success || !Report.Get_IsAccountingClosed() || !Report.Get_PayloadLosses().IsEmpty())
        { return Finish(false,TEXT("authority load accounting failed")); }
        FString Manifest;
        if (!Verify(World,Manifest)) { break; }
        const double RestoreMs=(FPlatformTime::Seconds()-LoadStart)*1000.;
        if (HashManifest(Manifest)!=ManifestHash) { return Finish(false,TEXT("authority row identities changed during restore")); }
        if (RestoreControl != ERestoreControl::None)
        {
            const bool Empty = RestoreControl == ERestoreControl::EmptyBoards;
            const auto ControlResult = FString::Printf(
                TEXT("mode,density,boards,cells_per_board,objects,placements,manifest_sha1,load_to_verified_manifest_ms\n")
                TEXT("%s,%d,%d,%d,%d,0,%s,%.6f\n"), Empty ? TEXT("EmptyBoards") : TEXT("UnplacedActors"),
                FMath::DivideAndRoundUp(Filled * 100, CellsPerBoard), BoardCount, CellsPerBoard,
                Empty ? 0 : BoardCount * Filled, *ManifestHash, RestoreMs);
            if (!RecordSave(RestoreMs) || !Write(TEXT("restored-control-manifest.csv"), Manifest) ||
                !Write(TEXT("restore-control.csv"), ControlResult)) { return Finish(false, TEXT("control evidence failed")); }
            return Finish(true, TEXT("authority restore control verified"));
        }
        const auto Observation = FString::Printf(
            TEXT("run,density,boards,cells_per_board,placements_per_board,manifest_sha1,save_request_ms,load_to_verified_manifest_ms\n")
            TEXT("%s,%d,%d,%d,%d,%s,%.6f,%.6f\n"),
            *FPaths::GetCleanFilename(Directory), FMath::DivideAndRoundUp(Filled * 100, CellsPerBoard),
            BoardCount, CellsPerBoard, Filled, *ManifestHash, SaveMs, RestoreMs);
        if (!Write(TEXT("authority-restore.csv"), Observation))
        { return Finish(false,TEXT("verified authority restore observation could not be recorded")); }
        if (!RecordSave(RestoreMs) || !Write(TEXT("server-ready"),ManifestHash))
        { return Finish(false,TEXT("save inspection, expected typed payload counts, or ready receipt failed")); }
        Advance(EStep::Join);
        break;
    }
    case EStep::Join:
    {
        if (Server)
        {
            if (Driver->ClientConnections.Num()>1) { return Finish(false,TEXT("more than one client")); }
            if (Driver->ClientConnections.IsEmpty()) { break; }
            auto* Conn=Driver->ClientConnections[0].Get();
            if (!Conn || Conn->GetConnectionState()!=USOCK_Open) { break; }
            if (ConnectedAt==0) { ConnectedAt=Now; }
            if (Read(TEXT("client-observed"))!=ManifestHash) { break; }
            const auto ServerNetwork=FString::Printf(TEXT("server_open_observed_to_receipt_observed_ms,connection_out_bytes,connection_netspeed_Bps,last_net_stat_avg_lag_s\n%.6f,%d,%d,%.6f\n"),
                (Now-ConnectedAt)*1000.,Conn->OutTotalBytes,Conn->CurrentNetSpeed,Conn->AvgLag);
            if (!Write(TEXT("server-connection.csv"),ServerNetwork)) { return Finish(false,TEXT("server connection evidence failed")); }
            TArray<AActor*> Retire;
            for (TActorIterator<FPiece> It(World); It; ++It) { Retire.Add(*It); }
            for (TActorIterator<FBoard> It(World); It; ++It) { Retire.Add(*It); }
            for (auto* Actor : Retire)
            { if (!Actor->Destroy()) { return Finish(false,TEXT("authority actor teardown rejected")); } }
        }
        else
        {
            FString Manifest;
            if (!Verify(World,Manifest)) { break; }
            const double VerifiedAt = FPlatformTime::Seconds();
            const double ObservedMs = (VerifiedAt - ConnectedAt) * 1000.;
            if (ObserveRelayCollision && (BeforeClientBrowse <= 0. || BeforeClientBrowse > ConnectedAt ||
                !Write(TEXT("client-upper-bound.csv"), FString::Printf(
                    TEXT("subsystem_init_before_browse_to_verified_manifest_ms,observed_open_to_verified_manifest_ms\n%.6f,%.6f\n"),
                    (VerifiedAt - BeforeClientBrowse) * 1000., ObservedMs))))
            { return Finish(false,TEXT("client monotonic observation interval unavailable")); }
            ManifestHash=HashManifest(Manifest);
            if (Read(TEXT("server-ready"))!=ManifestHash) { return Finish(false,TEXT("late client's full manifest differs")); }
            auto* Conn=Driver->ServerConnection.Get();
            const auto ClientNetwork=FString::Printf(TEXT("observed_open_to_verified_manifest_ms,connection_in_bytes,connection_netspeed_Bps,last_net_stat_avg_lag_s\n%.6f,%d,%d,%.6f\n"),
                ObservedMs,Conn->InTotalBytes,Conn->CurrentNetSpeed,Conn->AvgLag);
            if (!Write(TEXT("client-metrics.csv"),ClientNetwork) || !Write(TEXT("client-manifest.csv"),Manifest) ||
                !Write(TEXT("client-observed"),ManifestHash)) { return Finish(false,TEXT("client evidence failed")); }
        }
        Advance(EStep::Drain);
        break;
    }
    case EStep::Drain:
        if (TActorIterator<FBoard>(World) || TActorIterator<FPiece>(World) || !WitnessesReleased()) { break; }
        if (Server && Read(TEXT("client-teardown"))!=ManifestHash) { break; }
        if (!Server && !Write(TEXT("client-teardown"),ManifestHash)) { return Finish(false,TEXT("teardown receipt failed")); }
        return Finish(true,TEXT("both dense board projections and every occupant identity matched"));
    case EStep::Done:
        return false;
    }
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexDenseRelayObservationPosture,
    "Ck.HexGrid.DenseObservation.RelayCollisionPosture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCk_HexDenseRelayObservationPosture::RunTest(const FString&)
{
    using ECode = ECk_SnapshotInspection_DiagnosticCode;
    using ESeverity = ECk_SnapshotInspection_Severity;
    using EProvenance = ECk_Snapshot_V3_Provenance;
    const auto Row = [](uint32 Id, FGuid Key, EProvenance Provenance)
    {
        FCk_Snapshot_V3_EntityEntry Entry;
        Entry.Set_SavedId(Id).Set_SaveKey(Key).Set_Provenance(Provenance);
        FCk_SnapshotInspection_EntitySummary Summary;
        Summary.Set_Entry(Entry);
        return Summary;
    };
    const auto Diagnostic = [](ECode Code, TArray<uint32> Ids)
    {
        FCk_SnapshotInspection_Diagnostic Result;
        Result.Set_Code(Code).Set_Severity(ESeverity::Error).Set_RelatedSavedIds(Ids);
        return Result;
    };
    const auto Key = GenericRelayKey();
    const auto First = Row(2, Key, EProvenance::EngineOwned);
    const auto Second = Row(4, Key, EProvenance::EngineOwned);
    const TSet<uint32> LiveIds{2,4};
    FCk_SnapshotInspection_Document Valid;
    const auto Expected = Diagnostic(ECode::DuplicateSaveKey,{4,2});
    Valid.Set_TableParseSucceeded(true).Set_Entities({First,Second}).Set_Diagnostics({Expected});
    TestTrue(TEXT("Only the two witnessed shared relay rows explain the error"), MatchesCapturedRelayCollision(Valid, LiveIds));
    TestFalse(TEXT("Missing live relay"), MatchesCapturedRelayCollision(Valid, {2}));
    TestFalse(TEXT("Wrong live relay"), MatchesCapturedRelayCollision(Valid, {2,8}));
    TestFalse(TEXT("Invalid live id"), MatchesCapturedRelayCollision(Valid, {0,2}));
    auto OtherWarning = Diagnostic(ECode::PayloadBytesEmpty,{2});
    OtherWarning.Set_Severity(ESeverity::Warning);
    auto Bad = Valid;
    Bad.Set_Diagnostics({Expected,OtherWarning});
    TestFalse(TEXT("Unrelated warning indicating a damaged payload"), MatchesCapturedRelayCollision(Bad, LiveIds));
    Bad.Set_TableParseSucceeded(false);
    TestFalse(TEXT("Unparsed document"), MatchesCapturedRelayCollision(Bad, LiveIds));
    Bad = Valid;
    Bad.Set_Diagnostics({});
    TestFalse(TEXT("Clean save belongs to the normal route"), MatchesCapturedRelayCollision(Bad, LiveIds));
    Bad.Set_Diagnostics({Expected,Diagnostic(ECode::OwnerCycle,{2})});
    TestFalse(TEXT("Additional corruption"), MatchesCapturedRelayCollision(Bad, LiveIds));
    Bad.Set_Diagnostics({Diagnostic(ECode::DuplicateAdoptKey,{2,4})});
    TestFalse(TEXT("Different error with matching ids"), MatchesCapturedRelayCollision(Bad, LiveIds));
    Bad.Set_Diagnostics({Diagnostic(ECode::DuplicateSaveKey,{2,2})});
    TestFalse(TEXT("Same saved id reported twice"), MatchesCapturedRelayCollision(Bad, LiveIds));
    Bad = Valid;
    Bad.Set_Entities({First,Row(4,Key,EProvenance::RuntimeSpawned)});
    TestFalse(TEXT("Wrong saved provenance"), MatchesCapturedRelayCollision(Bad, LiveIds));
    Bad.Set_Entities({First,Row(4,FGuid{},EProvenance::EngineOwned)});
    TestFalse(TEXT("Wrong saved key"), MatchesCapturedRelayCollision(Bad, LiveIds));
    Bad.Set_Entities({First,Second,Row(8,Key,EProvenance::EngineOwned)});
    TestFalse(TEXT("Third same-key owner"), MatchesCapturedRelayCollision(Bad, LiveIds));
    Bad = Valid;
    FCk_Snapshot_V3_PayloadEntry Entry;
    Entry.Set_OwnerSavedId(2).Set_TypePath(FCk_RepData_HexGridState::StaticStruct()->GetPathName());
    FCk_SnapshotInspection_PayloadSummary Payload;
    Payload.Set_Entry(Entry);
    Bad.Set_Payloads({Payload});
    TestFalse(TEXT("Relay owns hex baseline"), MatchesCapturedRelayCollision(Bad, LiveIds));
    return true;
}
#endif
