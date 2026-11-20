#include "CkHexGrid_ScaleProbe.h"

#if UE_BUILD_DEVELOPMENT && !WITH_EDITORONLY_DATA

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkGrid/2dGridSystem/Grid/Ck2dGridSystem_Utils.h"
#include "CkGrid/2dGridSystem/Object/Ck2dGridObject_Utils.h"
#include "CkGrid/2dGridSystem/Occupancy/Ck2dGridOccupancy_Utils.h"
#include "CkGrid/2dGridSystem/Placement/Ck2dGridPlacement_Utils.h"
#include "CkGrid/HexGridSystem/Geometry/CkHexGrid_Geometry_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"
#include "../UnitTests/CkGrid/CkHexPlacementTestListener.h"
#include "Containers/Ticker.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace ck::tests::hex_scale
{
    using Hex = UCk_Utils_HexGrid_UE;
    using HexObject = UCk_Utils_HexGridObject_UE;
    using HexPlace = UCk_Utils_HexGridPlacement_UE;
    using HexOcc = UCk_Utils_HexGridOccupancy_UE;
    using Square = UCk_Utils_2dGridSystem_UE;
    using SquarePlace = UCk_Utils_2dGridPlacement_UE;
    using SquareOcc = UCk_Utils_2dGridOccupancy_UE;
    using Life = UCk_Utils_EntityLifetime_UE;
    constexpr int32 MeasuredRuns = 5;
    constexpr int32 BatchSize = 64;
    constexpr int32 FrameSamples = 30;

    enum class EPhase { World, Build, Fill, Validate, Query, Idle, SendMixed, ReceiveMixed, BeginProbe, ProbePlace, SendMove, ReceiveMove, Destroy, Retire };
    struct FBoard
    {
        FCk_Handle_HexGrid HexGrid;
        FCk_Handle_2dGridSystem SquareGrid;
        FCk_Handle_HexGridObject HexProbe;
        FCk_Handle_2dGridObject SquareProbe;
        FCk_Handle CellWitness;
    };

    struct FRun
    {
        FString Topology, Orientation, Directory;
        int32 Radius = 0, Density = 0, Boards = 0;
        int32 CellCount = 0, Filled = 0, Width = 0;
        int32 Repeat = 0, Cursor = 0, Frames = 0, Moves = 0;
        double Deadline = 0, Started = 0, MoveStart = 0, PreviousTick = 0;
        int64 BaseRss = 0, SampledPeak = 0;
        EPhase Phase = EPhase::World;
        FCk_HexGrid_Spec Spec;
        TArray<FCk_HexCoord> Addresses;
        TArray<FBoard> Grids;
        TArray<FCk_Handle> TeardownWitnesses;
        TMap<FString,TArray<double>> Metrics;
        TWeakObjectPtr<UWorld> World;
        FCk_Handle Owner;
        TStrongObjectPtr<UCk_HexPlacementTestListener> Listener;
        FCk_Delegate_HexGridPlacement_OnResult OnMove;
        FCk_HexGridPlacement_Result MoveResult;
        FGuid PlacementId;
        bool ReceivedMove = false;
        bool Pipeline = false;
        int32 ProbeIndex() const { return Filled + (Pipeline ? 2 : 0); }
        double CallbackAt = 0;
        int32 MixedBatch = 0;
        struct FMixedMove
        {
            FGuid Id;
            FCk_HexCoord Target;
            bool Completed = false;
        };
        TArray<FMixedMove> MixedMoves;
        bool MixedFailed = false;
        uint64 SubmissionFrame = 0, FirstCommitFrame = 0, LastCommitFrame = 0;
        double MixedStart = 0, LastCompletion = 0;

        bool IsSquare() const { return Topology == TEXT("Square"); }
        static double Now() { return FPlatformTime::Seconds(); }
        static int64 Rss() { return static_cast<int64>(FPlatformMemory::GetStats().UsedPhysical); }
        FIntPoint Position(int32 Index) const { return {Index % Width, Index / Width}; }

        void Add(const TCHAR* Metric, double Value)
        {
            if (Repeat > 0) { Metrics.FindOrAdd(Metric).Add(Value); }
        }
        void Elapsed(const TCHAR* Metric, double Start) { Add(Metric, (Now() - Start) * 1000.0); }
        void Advance(EPhase Next)
        {
            Phase = Next;
            Deadline = Now() + 300.0;
        }

        bool Configure()
        {
            const auto* Cmd = FCommandLine::Get();
            if (!FParse::Value(Cmd,TEXT("CkHexScaleTopology="),Topology) ||
                !FParse::Value(Cmd,TEXT("CkHexScaleOrientation="),Orientation) ||
                !FParse::Value(Cmd,TEXT("CkHexScaleRadius="),Radius) ||
                !FParse::Value(Cmd,TEXT("CkHexScaleDensity="),Density) ||
                !FParse::Value(Cmd,TEXT("CkHexScaleBoards="),Boards)) { return false; }
            if ((!IsSquare() && Topology != TEXT("Disk") && Topology != TEXT("Sparse")) ||
                (Orientation != TEXT("Flat") && Orientation != TEXT("Pointy")) ||
                (Radius != 8 && Radius != 32 && Radius != 64) ||
                (Density != 0 && Density != 50 && Density != 90) || (Boards != 1 && Boards != 2)) { return false; }
            FCk_HexDomain Domain;
            Domain.Set_RingRadius(Radius);
            ECk_HexGeometryFailure Error{};
            if (!UCk_Utils_HexGeometry_UE::TryResolve_Domain(Domain, Addresses, Error)) { return false; }
            CellCount = Addresses.Num();
            if (CellCount != 1 + 3 * Radius * (Radius + 1)) { return false; }
            if (Topology == TEXT("Sparse"))
            {
                // Same membership count; separated addresses stress explicit sparse lookup.
                for (auto& Address : Addresses) { Address = FCk_HexCoord{Address.Get_Q()*2, Address.Get_R()*2}; }
                Domain.Set_Kind(ECk_HexDomainKind::Explicit).Set_RingRadius(0).Set_Coordinates(Addresses);
            }
            FCk_HexLayout Layout;
            Layout.Set_OuterRadius(100.0).Set_Orientation(Orientation == TEXT("Flat") ?
                ECk_HexOrientation::Flat : ECk_HexOrientation::Pointy);
            Spec.Set_Domain(Domain).Set_Layout(Layout);
            Width = FMath::FloorToInt(FMath::Sqrt(static_cast<double>(CellCount)));
            while (CellCount % Width) { --Width; }
            Filled = CellCount * Density / 100;
            Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling/HexScale"),
                FString::Printf(TEXT("%s-%s-r%d-fill%d-b%d-%s"), *Topology,*Orientation,Radius,Density,Boards,
                    *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
            return IFileManager::Get().MakeDirectory(*Directory,true);
        }

        bool Build()
        {
            // Keep witness capacity stable across repeats and outside the measured board allocation.
            TeardownWitnesses.Reset(1 + Boards * (CellCount + 2 * Filled + 5));
            BaseRss = Rss();
            SampledPeak = BaseRss;
            Started = Now();
            Owner = Life::Request_CreateEntity_TransientOwner(World.Get());
            if (!ck::IsValid(Owner)) { return false; }
            UCk_Utils_Net_UE::Add(Owner,FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,ECk_Net_NetModeType::ClientAndHost,
                ECk_Net_EntityNetRole::Authority});
            for (int32 Index = 0; Index < Boards; ++Index)
            {
                auto& Board = Grids.AddDefaulted_GetRef();
                FTransform Transform{FRotator{0,23,0},FVector{2000.0*Index,100.,12.}};
                if (IsSquare())
                {
                    FCk_2dGridSystem_Spec Params{{Width,CellCount/Width},{100.,100.}};
                    Params.Set_DefaultCellState(ECk_EnableDisable::Enable);
                    Board.SquareGrid = Square::Create(Owner,Transform,Params);
                    if (!ck::IsValid(Board.SquareGrid)) { return false; }
                    Board.CellWitness = Square::Get_CellAt(Board.SquareGrid,Position(0));
                    if (Square::Get_AllCells(Board.SquareGrid,ECk_2dGridSystem_CellFilter::NoFilter).Num() != CellCount) { return false; }
                }
                else
                {
                    ECk_HexBoardFailure Error{};
                    FCk_Handle_HexCell Cell;
                    if (!Hex::TryCreate(Owner,Transform,Spec,Board.HexGrid,Error) ||
                        !Hex::TryGet_CellAt(Board.HexGrid,Addresses[0],Cell,Error) ||
                        Hex::Get_Cells(Board.HexGrid).Num() != CellCount) { return false; }
                    Board.CellWitness = Cell;
                }
                if (!ck::IsValid(Board.CellWitness)) { return false; }
            }
            Elapsed(TEXT("create_and_verify_ms"),Started);
            Add(TEXT("created_process_rss_delta_bytes_per_cell"),double(Rss()-BaseRss)/(CellCount*Boards));
            Cursor = 0;
            Advance(EPhase::Fill);
            return true;
        }

        bool QueueOne(FBoard& Board, int32 Index, bool Probe)
        {
            if (IsSquare())
            {
                auto Entity = Life::Request_CreateEntity(Owner);
                if (!ck::IsValid(Entity)) { return false; }
                auto Object = UCk_Utils_2dGridObject_UE::Add(Entity,FCk_2dGridObject_Spec{FIntPoint{1,1}});
                if (!ck::IsValid(Object)) { return false; }
                TeardownWitnesses.Add(Object);
                if (Probe) { Board.SquareProbe = Object; return true; }
                return ck::IsValid(SquarePlace::Request_Place(Board.SquareGrid,Entity,Position(Index),ECk_CardinalRotation::None,{}));
            }
            FCk_HexGridObject_Spec Shape;
            Shape.Set_FootprintOffsets({FCk_HexCoord{}});
            FCk_Handle_HexGridObject Object;
            ECk_HexGridObject_Failure Error{};
            if (!HexObject::TryCreate(Owner,Shape,Object,Error)) { return false; }
            TeardownWitnesses.Add(Object);
            if (Probe) { Board.HexProbe = Object; return true; }
            FCk_HexGridPlacement_Spec PlaceSpec;
            PlaceSpec.Set_Object(Object).Set_Anchor(Addresses[Index]);
            HexPlace::Request_Place(Board.HexGrid,FCk_Request_HexGridPlacement_Place{PlaceSpec},{});
            return true;
        }

        bool ValidateFill()
        {
            for (const auto& Board : Grids)
            {
                if (IsSquare())
                {
                    if (SquareOcc::Get_Placements(Board.SquareGrid).Num() != Filled) { return false; }
                    for (int32 Index = 0; Index < Filled; ++Index)
                    { if (!SquareOcc::Get_IsOccupied(Board.SquareGrid,Position(Index))) { return false; } }
                }
                else
                {
                    const auto All = HexOcc::Get_Placements(Board.HexGrid);
                    if (!All.Get_Succeeded() || All.Get_Placements().Num() != Filled) { return false; }
                    TSet<FCk_HexCoord> Actual;
                    for (const auto& Placement : All.Get_Placements())
                    {
                        if (Placement.Get_Cells().Num() != 1) { return false; }
                        Actual.Add(Placement.Get_Cells()[0]);
                    }
                    for (int32 Index = 0; Index < Filled; ++Index)
                    { if (!Actual.Contains(Addresses[Index])) { return false; } }
                }
            }
            return true;
        }

        bool Queries()
        {
            const auto QueryCount = 1024;
            FRandomStream Random(7341);
            TArray<int32> Indexes;
            for (int32 I = 0; I < QueryCount; ++I) { Indexes.Add(Random.RandRange(0,CellCount-1)); }
            for (auto& Board : Grids)
            {
                if (!QueueOne(Board,0,true)) { return false; }
                TArray<double, TInlineAllocator<1024>> Durations;
                for (const auto Index : Indexes)
                {
                    const auto Start = Now();
                    if (IsSquare())
                    {
                        if (!ck::IsValid(Square::Get_CellAt(Board.SquareGrid,Position(Index)))) { return false; }
                    }
                    else
                    {
                        FCk_Handle_HexCell Cell;
                        ECk_HexBoardFailure Error{};
                        if (!Hex::TryGet_CellAt(Board.HexGrid,Addresses[Index],Cell,Error)) { return false; }
                    }
                    Durations.Add((Now()-Start)*1.e6);
                }
                for (const double Duration : Durations) { Add(TEXT("lookup_operation_us"), Duration); }
                // Exact, unoccupied coordinates for accepted previews and occupied ones for rejection.
                FCk_HexGridPlacement_Spec Candidate;
                Candidate.Set_Object(Board.HexProbe);
                Durations.Reset();
                for (int32 I = 0; I < 128; ++I)
                {
                    const auto Index = Indexes[I];
                    const bool Expected = Index >= Filled;
                    bool Accepted = false;
                    const auto Start = Now();
                    if (IsSquare())
                    {
                        Accepted = SquarePlace::Get_CanPlace(Board.SquareGrid,Board.SquareProbe,Position(Index),
                            ECk_CardinalRotation::None,ECk_GridConnectivity::Ignore).Get_CanPlace();
                    }
                    else
                    {
                        Candidate.Set_Anchor(Addresses[Index]);
                        Accepted = HexPlace::Get_CanPlace(Board.HexGrid,Candidate).Get_Succeeded();
                    }
                    Durations.Add((Now()-Start)*1.e6);
                    if (Accepted != Expected) { return false; }
                }
                for (const double Duration : Durations) { Add(TEXT("preview_operation_us"), Duration); }
            }
            Frames = 0;
            Advance(EPhase::Idle);
            return true;
        }

        bool SendMixed()
        {
            MixedMoves.Reset();
            MixedFailed = false;
            FirstCommitFrame = MAX_uint64;
            LastCommitFrame = 0;
            const bool Outbound = MixedBatch % 2 == 0;
            for (int32 I = 0; I < 4; ++I)
            {
                const auto Home = I % 2;
                const auto Away = Filled + Home;
                const auto From = Addresses[Outbound ? Home : Away];
                const auto View = HexOcc::TryGet_PlacementAt(Grids[I/2].HexGrid,From);
                if (!View.Get_Succeeded()) { return false; }
                MixedMoves.Add(FMixedMove{View.Get_PlacementId(),Addresses[Outbound ? Away : Home]});
            }
            const auto Start = Now();
            MixedStart = Start;
            SubmissionFrame = GFrameCounter;
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(CkHexScale_MixedPreviewsAndSubmission);
                for (int32 I = 0; I < 16; ++I)
                {
                    const auto& Board = Grids[I%2];
                    FCk_HexGridPlacement_Spec SpecToPreview;
                    SpecToPreview.Set_Object(Board.HexProbe).Set_Anchor(Addresses[Filled+4+I/2]);
                    if (!HexPlace::Get_CanPlace(Board.HexGrid,SpecToPreview).Get_Succeeded()) { return false; }
                }
                Advance(EPhase::ReceiveMixed);
                for (int32 I = 0; I < MixedMoves.Num(); ++I)
                {
                    const auto& Move = MixedMoves[I];
                    FCk_Request_HexGridPlacement_Move Request{Move.Id,Move.Target,ECk_HexRotation::Turn0};
                    Request.Set_OnResult(OnMove);
                    HexPlace::Request_Move(Grids[I/2].HexGrid,Request,{});
                }
            }
            Add(TEXT("mixed_preview_and_enqueue_ms"),(Now()-Start)*1000.);
            return true;
        }

        void OnMixedResult(const FCk_HexGridPlacement_Result& Result)
        {
            auto* Move = MixedMoves.FindByPredicate([&Result](const FMixedMove& Entry)
            { return Entry.Id == Result.Get_PlacementId(); });
            if (!Result.Get_Succeeded() || !Move || Move->Completed || Result.Get_Placement().Get_Anchor() != Move->Target)
            { MixedFailed = true; return; }
            Move->Completed = true;
            FirstCommitFrame = FMath::Min(FirstCommitFrame,uint64(GFrameCounter));
            LastCommitFrame = FMath::Max(LastCommitFrame,uint64(GFrameCounter));
            LastCompletion = Now();
        }

        bool Destroy()
        {
            TSet<FCk_Handle> PrivateCells;
            PrivateCells.Reserve(CellCount * Boards);
            auto CaptureCells = [&PrivateCells](const auto& Cells, const FCk_Handle& Grid)
            {
                for (const FCk_Handle& Cell : Cells)
                {
                    if (!ck::IsValid(Cell) || PrivateCells.Contains(Cell) ||
                        Life::Get_LifetimeOwner(Cell) != Grid)
                    {
                        UE_LOG(LogTemp,Error,TEXT("HexScale teardown found an invalid, duplicate or misowned cell"));
                        return false;
                    }
                    PrivateCells.Add(Cell);
                }
                return true;
            };
            TeardownWitnesses.Add(Owner);
            for (const auto& Board : Grids)
            {
                if (IsSquare())
                {
                    TeardownWitnesses.Add(Board.SquareGrid);
                    const auto Cells = Square::Get_AllCells(Board.SquareGrid,ECk_2dGridSystem_CellFilter::NoFilter);
                    const auto Rows = SquareOcc::Get_Placements(Board.SquareGrid);
                    if (Cells.Num() != CellCount || Rows.Num() != Filled) { return false; }
                    if (!CaptureCells(Cells,Board.SquareGrid)) { return false; }
                    for (const auto& Row : Rows) { TeardownWitnesses.Add(Row); }
                }
                else
                {
                    TeardownWitnesses.Add(Board.HexGrid);
                    const auto Cells = Hex::Get_Cells(Board.HexGrid);
                    const auto Rows = HexOcc::Get_Placements(Board.HexGrid);
                    // The move probe placed one additional object on the first board.
                    const int32 ExpectedRows = Filled + (Board.HexGrid == Grids[0].HexGrid ? 1 : 0);
                    if (Cells.Num() != CellCount || !Rows.Get_Succeeded() || Rows.Get_Placements().Num() != ExpectedRows) { return false; }
                    if (!CaptureCells(Cells,Board.HexGrid)) { return false; }
                    for (const auto& Row : Rows.Get_Placements()) { TeardownWitnesses.Add(Row.Get_Handle()); }
                }
            }
            // Only same-registry children appear in the reverse lifetime list. Both grid
            // types own a separate ECS world for cells, whose forward links were checked above.
            {
                TSet<FCk_Handle> Members;
                TArray<FCk_Handle> Pending{Owner};
                Members.Add(Owner);
                for (int32 Index = 0; Index < Pending.Num(); ++Index)
                {
                    const auto Children = Life::Get_LifetimeDependents(Pending[Index]);
                    for (const auto& Child : Children)
                    {
                        if (Members.Contains(Child)) { return false; }
                        Members.Add(Child);
                        Pending.Add(Child);
                    }
                }
                if (TeardownWitnesses.ContainsByPredicate([&Members](const FCk_Handle& Handle)
                    { return !ck::IsValid(Handle) || !Members.Contains(Handle); })) { return false; }
                TeardownWitnesses = MoveTemp(Pending);
                for (const FCk_Handle& Cell : PrivateCells) { TeardownWitnesses.Add(Cell); }
            }
            Started = Now();
            auto ToDestroy = Owner;
            Life::Request_DestroyEntity(ToDestroy);
            Advance(EPhase::Retire);
            return true;
        }

        bool AllReleased() const
        {
            return !TeardownWitnesses.ContainsByPredicate([](const FCk_Handle& Handle)
            { return ck::IsValid(Handle,ck::IsValid_Policy_IncludePendingKill{}); });
        }

        bool WriteResults() const
        {
            FString Samples = TEXT("metric,sample,value\n");
            FString Summary = TEXT("metric,n,median,p95,min,max\n");
            TArray<FString> Names;
            Metrics.GetKeys(Names);
            Names.Sort();
            for (const auto& Name : Names)
            {
                auto Values = Metrics.FindChecked(Name);
                for (int32 I = 0; I < Values.Num(); ++I)
                { Samples += FString::Printf(TEXT("%s,%d,%.9f\n"),*Name,I,Values[I]); }
                Values.Sort();
                const auto N = Values.Num();
                const auto Median = (Values[(N-1)/2] + Values[N/2]) * 0.5;
                Summary += FString::Printf(TEXT("%s,%d,%.9f,%.9f,%.9f,%.9f\n"),*Name,N,Median,
                    Values[FMath::CeilToInt(0.95*N)-1],Values[0],Values.Last());
            }
            const auto Metadata = FString::Printf(TEXT("Development packaged, standalone authority\n")
                TEXT("topology=%s orientation=%s radius=%d cells_per_board=%d occupied_per_board=%d ")
                TEXT("board_count=%d square_width=%d square_height=%d seed=7341 warmups=1 measured=%d\n")
                TEXT("rss=process working set sampled once per tick; includes allocator caches, harness storage and engine activity; transient intra-tick peaks can be missed\n")
                TEXT("retirement checks all materialized cells, objects and placements and the complete pre-destroy lifetime tree including pending-kill entities\n")
                TEXT("idle_frame measures whole-process tick interval; processor attribution requires CPU trace\n")
                TEXT("lookup and preview samples time individual calls including timing/validation overhead; p95 uses nearest rank\n")
                TEXT("mixed latency runs from first preview through the last terminal callback across both boards\n")
                TEXT("mixed_workload=%s\n")
                TEXT("accepted/rejected moves use single-cell probe after density measurements; square has no atomic move API\n")
                TEXT("save, restore, connection baseline bytes and latency require a durable network fixture\n"),
                *Topology,*Orientation,Radius,CellCount,Filled,Boards,Width,CellCount/Width,MeasuredRuns,
                Pipeline ? TEXT("pipeline31_v1") : TEXT("serial30_v1"));
            return FFileHelper::SaveStringToFile(Samples,*FPaths::Combine(Directory,TEXT("samples.csv"))) &&
                FFileHelper::SaveStringToFile(Summary,*FPaths::Combine(Directory,TEXT("summary.csv"))) &&
                FFileHelper::SaveStringToFile(Metadata,*FPaths::Combine(Directory,TEXT("scenario.txt")));
        }
    };

    static TUniquePtr<FRun> Run;
    static FTSTicker::FDelegateHandle Ticker;
    static FDelegateHandle PreExit;

    static bool Finish(bool Succeeded, const TCHAR* Message)
    {
        if (Succeeded) { UE_LOG(LogTemp,Display,TEXT("HexScale PASS %s"),Message); }
        else { UE_LOG(LogTemp,Error,TEXT("HexScale FAIL %s"),Message); }
        // Finish is a terminal return from the ticker (or runs before registration).
        // Its false return removes the current ticker without removing it inside itself.
        Ticker.Reset();
        Shutdown();
        FPlatformMisc::RequestExitWithStatus(false,Succeeded ? 0 : 2,TEXT("CkHexScale"));
        return false;
    }

    static bool Tick(float)
    {
        auto& S = *Run;
        const auto Now = FRun::Now();
        const auto TickIntervalMs = (Now-S.PreviousTick)*1000.0;
        S.PreviousTick = Now;
        if (Now > S.Deadline)
        {
            const auto Details = FString::Printf(TEXT("timeout phase=%d sample=%d cursor=%d"),
                int32(S.Phase),S.Repeat,S.Cursor);
            return Finish(false,*Details);
        }
        if (S.Phase != EPhase::World && (!S.World.IsValid() || S.World->IsBeingCleanedUp()))
        { return Finish(false,TEXT("benchmark world retired before completion")); }
        if (S.Phase != EPhase::World && S.Phase != EPhase::Build)
        { S.SampledPeak = FMath::Max(S.SampledPeak,FRun::Rss()); }

        switch (S.Phase)
        {
        case EPhase::World:
            if (!GEngine) { break; }
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                auto* World = Context.World();
                if (Context.WorldType != EWorldType::Game || !IsValid(World) ||
                    !World->HasBegunPlay() || World->IsBeingCleanedUp()) { continue; }
                if (World->GetNetMode() != NM_Standalone)
                { return Finish(false,TEXT("scale probe requires a standalone packaged game")); }
                S.World = World;
                S.Listener.Reset(NewObject<UCk_HexPlacementTestListener>());
                S.Listener->Result = [&S](const FCk_HexGridPlacement_Result& Result)
                {
                    if (S.Phase == EPhase::ReceiveMixed) { S.OnMixedResult(Result); return; }
                    S.CallbackAt = FRun::Now();
                    S.MoveResult = Result;
                    S.ReceivedMove = true;
                };
                S.OnMove.BindDynamic(S.Listener.Get(),&UCk_HexPlacementTestListener::OnResult);
                S.Advance(EPhase::Build);
                break;
            }
            break;
        case EPhase::Build:
            TRACE_BOOKMARK(TEXT("HexScale build begin repeat %d"), S.Repeat);
            if (!S.Build()) { return Finish(false,TEXT("board factory or materialized cell count failed")); }
            break;
        case EPhase::Fill:
        {
            const int32 End = FMath::Min(S.Cursor+BatchSize,S.Boards*S.Filled);
            for (; S.Cursor < End; ++S.Cursor)
            {
                if (!S.QueueOne(S.Grids[S.Cursor / S.Filled],S.Cursor % S.Filled,false))
                { return Finish(false,TEXT("occupant creation or placement enqueue failed")); }
            }
            if (S.Cursor == S.Boards*S.Filled) { S.Advance(EPhase::Validate); }
            else { S.Advance(EPhase::Fill); }
            break;
        }
        case EPhase::Validate:
            if (S.ValidateFill())
            {
                TRACE_BOOKMARK(TEXT("HexScale filled repeat %d"), S.Repeat);
                S.Add(TEXT("populated_process_rss_delta_bytes_per_cell"),
                    double(FRun::Rss()-S.BaseRss)/(S.CellCount*S.Boards));
                S.Advance(EPhase::Query);
            }
            break;
        case EPhase::Query:
            if (!S.Queries()) { return Finish(false,TEXT("lookup/preview answer differed from seeded occupancy")); }
            break;
        case EPhase::Idle:
            if (S.Frames > 0) { S.Add(TEXT("idle_frame_interval_ms"),TickIntervalMs); }
            if (++S.Frames <= FrameSamples) { break; }
            if (S.IsSquare()) { S.Advance(EPhase::Destroy); break; }
            S.MixedBatch = 0;
            S.Advance(S.Radius == 32 && S.Boards == 2 && S.Density > 0 ? EPhase::SendMixed : EPhase::BeginProbe);
            break;
        case EPhase::SendMixed:
            if (!S.SendMixed()) { return Finish(false,TEXT("mixed preview or initial placement lookup failed")); }
            break;
        case EPhase::ReceiveMixed:
            if (S.MixedFailed) { return Finish(false,TEXT("mixed commit callback rejected, repeated or misaddressed")); }
            if (S.MixedMoves.ContainsByPredicate([](const FRun::FMixedMove& Move){ return !Move.Completed; })) { break; }
            for (int32 I = 0; I < S.MixedMoves.Num(); ++I)
            {
                const auto& Move = S.MixedMoves[I];
                const auto At = HexOcc::TryGet_PlacementAt(S.Grids[I/2].HexGrid,Move.Target);
                if (!At.Get_Succeeded() || At.Get_PlacementId() != Move.Id)
                { return Finish(false,TEXT("mixed callbacks disagreed with authoritative occupancy")); }
            }
            S.Add(TEXT("mixed_four_commit_frame_span"),double(S.LastCommitFrame-S.FirstCommitFrame));
            S.Add(TEXT("mixed_submission_to_last_commit_frames"),double(S.LastCommitFrame-S.SubmissionFrame));
            S.Add(TEXT("mixed_preview_to_last_commit_ms"),(S.LastCompletion-S.MixedStart)*1000.0);
            if (!S.Pipeline)
            {
                S.Advance(++S.MixedBatch < FrameSamples ? EPhase::SendMixed : EPhase::BeginProbe);
                break;
            }
            S.Add(TEXT("mixed_submission_frame"),double(S.SubmissionFrame));
            S.Add(TEXT("mixed_first_commit_frame"),double(S.FirstCommitFrame));
            S.Add(TEXT("mixed_last_commit_frame"),double(S.LastCommitFrame));
            if (++S.MixedBatch <= FrameSamples)
            {
                const uint64 PriorSubmission = S.SubmissionFrame;
                const uint64 PriorFirst = S.FirstCommitFrame;
                const uint64 PriorLast = S.LastCommitFrame;
                if (!S.SendMixed()) { return Finish(false,TEXT("mixed pipeline submission failed")); }
                const bool Overlapped = S.SubmissionFrame == PriorSubmission + 1 &&
                    PriorFirst == S.SubmissionFrame && PriorLast == S.SubmissionFrame;
                S.Add(TEXT("mixed_interior_frame_matches"),Overlapped ? 1.0 : 0.0);
            }
            else { S.Advance(EPhase::BeginProbe); }
            break;
        case EPhase::BeginProbe:
            {
                FCk_HexGridPlacement_Spec Candidate;
                Candidate.Set_Object(S.Grids[0].HexProbe).Set_Anchor(S.Addresses[S.ProbeIndex()]);
                FCk_Request_HexGridPlacement_Place Request{Candidate};
                Request.Set_OnResult(S.OnMove);
                S.ReceivedMove = false;
                HexPlace::Request_Place(S.Grids[0].HexGrid,Request,{});
                S.Advance(EPhase::ProbePlace);
            }
            break;
        case EPhase::ProbePlace:
            if (!S.ReceivedMove) { break; }
            if (!S.MoveResult.Get_Succeeded() || !S.MoveResult.Get_PlacementId().IsValid())
            { return Finish(false,TEXT("measurement probe could not acquire a placement")); }
            S.PlacementId = S.MoveResult.Get_PlacementId();
            S.Moves = 0;
            S.Advance(EPhase::SendMove);
            break;
        case EPhase::SendMove:
        {
            const bool Reject = S.Moves % 2 == 1;
            const auto Target = Reject ? FCk_HexCoord{10000,10000} :
                S.Addresses[S.ProbeIndex()+1-(S.Moves/2)%2];
            FCk_Request_HexGridPlacement_Move Request{S.PlacementId,Target,ECk_HexRotation::Turn0};
            Request.Set_OnResult(S.OnMove);
            S.ReceivedMove = false;
            S.MoveStart = FRun::Now();
            HexPlace::Request_Move(S.Grids[0].HexGrid,Request,{});
            S.Advance(EPhase::ReceiveMove);
            break;
        }
        case EPhase::ReceiveMove:
        {
            S.Add(TEXT("mutation_frame_interval_ms"),TickIntervalMs);
            if (!S.ReceivedMove) { break; }
            const bool Reject = S.Moves % 2 == 1;
            if (S.MoveResult.Get_Succeeded() == Reject ||
                (Reject && S.MoveResult.Get_Failure() != ECk_HexGridFailure::AnchorOutsideDomain))
            { return Finish(false,TEXT("move result differed from expected verdict")); }
            const auto Expected = S.Addresses[S.ProbeIndex()+1-(S.Moves/2)%2];
            auto View = HexOcc::TryGet_PlacementAt(S.Grids[0].HexGrid,Expected);
            if (!View.Get_Succeeded() || View.Get_PlacementId() != S.PlacementId)
            { return Finish(false,TEXT("move terminal result disagrees with authoritative occupancy")); }
            S.Add(Reject ? TEXT("rejected_move_callback_ms") : TEXT("accepted_move_callback_ms"),
                (S.CallbackAt-S.MoveStart)*1000.0);
            if (++S.Moves < 20) { S.Advance(EPhase::SendMove); }
            else { S.Advance(EPhase::Destroy); }
            break;
        }
        case EPhase::Destroy:
            TRACE_BOOKMARK(TEXT("HexScale teardown begin repeat %d"), S.Repeat);
            if (!S.Destroy()) { return Finish(false,TEXT("missing or retired teardown witness before owner destruction")); }
            break;
        case EPhase::Retire:
            if (!S.AllReleased()) { break; }
            TRACE_BOOKMARK(TEXT("HexScale teardown complete repeat %d"), S.Repeat);
            S.Elapsed(TEXT("teardown_complete_ms"),S.Started);
            S.Grids.Reset();
            S.Add(TEXT("sampled_peak_process_rss_delta_bytes"),double(S.SampledPeak-S.BaseRss));
            S.Add(TEXT("post_teardown_process_rss_delta_bytes"),double(FRun::Rss()-S.BaseRss));
            UE_LOG(LogTemp,Display,TEXT("HexScale completed repeat %d/%d"),S.Repeat,MeasuredRuns);
            if (++S.Repeat <= MeasuredRuns) { S.Advance(EPhase::Build); break; }
            if (!S.WriteResults()) { return Finish(false,TEXT("benchmark evidence could not be written")); }
            return Finish(true,*S.Directory);
        }
        return true;
    }

    void Startup()
    {
        if (!FParse::Param(FCommandLine::Get(),TEXT("CkHexScale"))) { return; }
        Run = MakeUnique<FRun>();
        Run->Pipeline = FParse::Param(FCommandLine::Get(),TEXT("CkHexScalePipeline"));
        PreExit = FCoreDelegates::OnEnginePreExit.AddStatic(&Shutdown);
        if (!Run->Configure()) { Finish(false,TEXT("invalid scale options or output directory")); return; }
        Run->Advance(EPhase::World);
        Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
    }

    void Shutdown()
    {
        if (PreExit.IsValid()) { FCoreDelegates::OnEnginePreExit.Remove(PreExit); PreExit.Reset(); }
        if (Ticker.IsValid()) { FTSTicker::RemoveTicker(Ticker); Ticker.Reset(); }
        if (!Run) { return; }
        UE_LOG(LogTemp,Display,TEXT("HexScale disposing fixture"));
        if (Run->Listener.IsValid()) { Run->Listener->Result = nullptr; }
        Run->OnMove.Unbind();
        Run->Listener.Reset();
        Run.Reset();
        UE_LOG(LogTemp,Display,TEXT("HexScale fixture disposed"));
    }
}
#else
void ck::tests::hex_scale::Startup() {}
void ck::tests::hex_scale::Shutdown() {}
#endif
