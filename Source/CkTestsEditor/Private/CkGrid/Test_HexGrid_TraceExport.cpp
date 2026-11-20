#include "Misc/AutomationTest.h"
#include "CkInsightsAnalyzer/Core/CkTraceSession.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/ModuleService.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "TraceServices/Model/Threads.h"
#include "Common/ProviderLock.h"
#include "TraceServices/Model/AllocationsProvider.h"
#include "TraceServices/Model/Callstack.h"
#include "ProfilingDebugging/MemoryTrace.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "Serialization/Archive.h"

namespace ck_test_hex_trace
{
    constexpr int32 NumRuns = 6;
    constexpr int32 Phases = 4;
    constexpr int32 NumBatches = 30;
    constexpr uint64 MaxExportBytes = 512ull * 1024 * 1024;
    const TCHAR* const Names[Phases] = {TEXT("build begin"), TEXT("filled"), TEXT("teardown begin"), TEXT("teardown complete")};

    struct FCsv
    {
        TUniquePtr<FArchive> File;
        uint64 Bytes = 0;
        bool Open(const FString& Directory, const TCHAR* Name, const TCHAR* Header)
        {
            File.Reset(IFileManager::Get().CreateFileWriter(*FPaths::Combine(Directory,Name),FILEWRITE_NoReplaceExisting));
            return File && Write(Header);
        }
        bool Write(const FString& Text)
        {
            FTCHARToUTF8 Utf8(*Text);
            Bytes += Utf8.Length();
            if (Bytes > MaxExportBytes) { return false; }
            File->Serialize(const_cast<void*>(static_cast<const void*>(Utf8.Get())),Utf8.Length());
            return !File->IsError();
        }
        bool Close() { return File && File->Close() && !File->IsError(); }
    };

    struct FMarks { double At[NumRuns][Phases]{}; };

    bool ExportMarks(FCk_TraceSession& Trace, const FString& Directory, FMarks& Marks)
    {
        FCsv Output;
        if (!Output.Open(Directory,TEXT("bookmarks.csv"),TEXT("Time,Category,Message\n"))) { return false; }
        int32 Next = 0;
        bool Valid = true;
        auto Read = Trace.CreateReadScope();
        const auto& Session = *Trace.GetSession();
        const auto* Provider=Session.ReadProvider<TraceServices::IBookmarkProvider>(TraceServices::GetBookmarkProviderName());
        if (!Provider) { return false; }
        Provider->EnumerateBookmarks(0.,Session.GetDurationSeconds(),
            [&](const TraceServices::FBookmark& Mark)
            {
                if (!Mark.Text || !FCString::Strstr(Mark.Text,TEXT("HexScale "))) { return; }
                if (Next>=NumRuns*Phases) { Valid=false; return; }
                const int32 Run=Next/Phases, Phase=Next%Phases;
                const auto Expected=FString::Printf(TEXT("HexScale %s repeat %d"),Names[Phase],Run);
                if (Expected!=Mark.Text || !FMath::IsFinite(Mark.Time) || Mark.Time<=0. ||
                    (Next && Mark.Time<=Marks.At[(Next-1)/Phases][(Next-1)%Phases]))
                { Valid=false; return; }
                Marks.At[Run][Phase]=Mark.Time;
                Valid &= Output.Write(FString::Printf(TEXT("%.12fs,LogBookmark,%s\n"),Mark.Time,*Expected));
                ++Next;
            });
        return Output.Close() && Valid && Next==NumRuns*Phases;
    }

    bool ExportCpu(FCk_TraceSession& Trace, const FString& Directory, const FMarks& Marks)
    {
        uint32 GameThread=0;
        int32 Count=0;
        auto Read=Trace.CreateReadScope();
        const auto& Session=*Trace.GetSession();
        const auto* Threads=Session.ReadProvider<TraceServices::IThreadProvider>(TraceServices::GetThreadProviderName());
        if (!Threads) { return false; }
        Threads->EnumerateThreads([&](const TraceServices::FThreadInfo& Thread)
        {
            if (Thread.Name && FCString::Strcmp(Thread.Name,TEXT("GameThread"))==0) { GameThread=Thread.Id; ++Count; }
        });
        if (Count!=1) { return false; }
        FCsv Out;
        if (!Out.Open(Directory,TEXT("events.csv"),TEXT("ThreadId,ThreadName,TimerId,TimerName,StartTime,EndTime\n"))) { return false; }
        const auto* Frames=Session.ReadProvider<TraceServices::IFrameProvider>(TraceServices::GetFrameProviderName());
        const auto* Timing=Trace.GetTimingProvider();
        uint32 Timeline=0;
        if (!Frames || !Timing || !Timing->GetCpuThreadTimelineIndex(GameThread,Timeline)) { return false; }
        TMap<uint32,FString> Selected;
        const FString Submit=TEXT("CkHexScale_MixedPreviewsAndSubmission");
        const FString Rules=TEXT("CkHexGrid_Rules_DoTick");
        const auto& Reader=Timing->GetTimerReader();
        for (uint32 I=0; I<Reader.GetTimerCount(); ++I)
        {
            const auto* Timer=Reader.GetTimer(I);
            if (Timer && Timer->Name && (Submit==Timer->Name || Rules==Timer->Name))
            { Selected.Add(Timer->Id,Timer->Name); }
        }
        bool Valid=true;
        int32 RuleCount=0, FrameCount=0, Submissions[NumRuns]{};
        const double Begin=Marks.At[0][0], End=Marks.At[NumRuns-1][Phases-1];
        Frames->EnumerateFrames(ETraceFrameType::TraceFrameType_Game,Begin,End,[&](const TraceServices::FFrame& F)
        {
            if (!Valid) { return; }
            Valid=FMath::IsFinite(F.EndTime) && F.EndTime>F.StartTime &&
                Out.Write(FString::Printf(TEXT("0,Game Frames,%llu,Game Frame %llu,%.15f,%.15f\n"),
                    F.Index,F.Index,F.StartTime,F.EndTime));
            ++FrameCount;
        });
        if (!Valid || FrameCount==0) { return false; }
        const bool HasTimeline=Timing->ReadTimeline(Timeline,[&](const TraceServices::ITimingProfilerProvider::Timeline& Events)
        {
            Events.EnumerateEvents(Begin,End,[&](double A,double B,uint32,const TraceServices::FTimingProfilerEvent& Event)
            {
                const auto* Name=Selected.Find(Event.TimerIndex);
                if (!Name) { return TraceServices::EEventEnumerate::Continue; }
                if (*Name==Submit)
                {
                    int32 Run=0;
                    while (Run<NumRuns && !(Marks.At[Run][1]<A && B<Marks.At[Run][2])) { ++Run; }
                    if (Run==NumRuns) { Valid=false; return TraceServices::EEventEnumerate::Stop; }
                    ++Submissions[Run];
                }
                else { ++RuleCount; }
                Valid=FMath::IsFinite(B) && B>A && Out.Write(FString::Printf(TEXT("%u,GameThread,%u,%s,%.15f,%.15f\n"),
                    GameThread,Event.TimerIndex,**Name,A,B));
                return Valid ? TraceServices::EEventEnumerate::Continue : TraceServices::EEventEnumerate::Stop;
            });
        });
        const int32 PerRepeat=Submissions[0];
        Valid &= PerRepeat==NumBatches || PerRepeat==NumBatches+1;
        for (int32 N : Submissions) { Valid &= N==PerRepeat; }
        return Out.Close() && HasTimeline && Valid && RuleCount>0;
    }
    FString Quote(const TCHAR* Raw)
    {
        FString Text=Raw ? Raw : TEXT("");
        Text.ReplaceInline(TEXT("\""),TEXT("\"\""));
        return TEXT("\"")+Text+TEXT("\"");
    }

    struct FMemoryExport
    {
        FCsv Rows,Stacks;
        TSet<uint32> SeenStacks;
        TSet<uint32> IncompleteStacks;
        TSet<uint32> HexSourceStacks;
        uint64 BornRows=0, BaselineRows=0, UncertainRows=0, WithHexSource=0;

        bool ExportStack(uint32 Id,const TraceServices::ICallstacksProvider& Provider)
        {
            if (SeenStacks.Contains(Id)) { return true; }
            if (SeenStacks.Num()>=131072) { return false; }
            SeenStacks.Add(Id);
            const auto* Stack=Provider.GetCallstack(Id);
            if (!Stack || !Stack->Num())
            {
                IncompleteStacks.Add(Id);
                return Stacks.Write(FString::Printf(TEXT("%u,-1,0,Missing,,,,0\n"),Id));
            }
            for (uint32 I=0; I<Stack->Num(); ++I)
            {
                const auto* Frame=Stack->Frame(static_cast<uint8>(I));
                const auto* S=Frame ? Frame->Symbol : nullptr;
                const auto Status=S ? S->GetResult() : TraceServices::ESymbolQueryResult::NotFound;
                const bool Resolved=Status==TraceServices::ESymbolQueryResult::OK && S->Name && *S->Name;
                FString File=Resolved && S->File ? S->File : TEXT("");
                if (!Resolved || File.IsEmpty()) { IncompleteStacks.Add(Id); }
                File.ReplaceInline(TEXT("\\"),TEXT("/"));
                if (File.Contains(TEXT("/CkGrid/")) && File.Contains(TEXT("/HexGridSystem/")))
                { HexSourceStacks.Add(Id); }
                if (!Stacks.Write(FString::Printf(TEXT("%u,%u,%llu,%s,%s,%s,%s,%u\n"),Id,I,Frame ? Frame->Addr : 0,
                    TraceServices::QueryResultToString(Status),*Quote(Resolved ? S->Module : nullptr),
                    *Quote(Resolved ? S->Name : nullptr),*Quote(*File),Resolved ? uint32(S->Line) : 0u)))
                { return false; }
            }
            return true;
        }

        bool ExportQuery(const TraceServices::IAllocationsProvider& P,const TraceServices::ICallstacksProvider& C,
            bool Baseline,double Begin,double End)
        {
            using Provider=TraceServices::IAllocationsProvider;
            const Provider::FQueryParams Params{Baseline ? Provider::EQueryRule::aAf : Provider::EQueryRule::AaB,Begin,End,0.,0.};
            Provider::FQueryHandle Query=0;
            {
                TraceServices::FProviderReadScopeLock Lock(P);
                Query=P.StartQuery(Params);
            }
            if (!Query) { return false; }
            const double Deadline=FPlatformTime::Seconds()+900.;
            bool Done=false,Valid=true;
            while (!Done && Valid && FPlatformTime::Seconds()<Deadline)
            {
                const auto Status=P.PollQuery(Query);
                if (Status.Status==Provider::EQueryStatus::Done) { Done=true; break; }
                if (Status.Status==Provider::EQueryStatus::Unknown) { break; }
                if (Status.Status!=Provider::EQueryStatus::Available) { FPlatformProcess::Sleep(.005f); continue; }
                TraceServices::FProviderReadScopeLock Lock(P);
                for (auto Page=Status.NextResult(); Page.IsValid() && Valid; Page=Status.NextResult())
                {
                    for (uint32 I=0; I<Page->Num(); ++I)
                    {
                        const auto* A=Page->Get(I);
                        if (!A) { Valid=false; break; }
                        if (A->IsHeap() || A->IsSwap() || A->GetRootHeap()!=EMemoryTraceRootHeap::SystemMemory) { continue; }
                        const double Start=A->GetStartTime(), Free=A->GetEndTime();
                        if (!FMath::IsFinite(Start) || FMath::IsNaN(Free) || Free<Start) { Valid=false; break; }
                        if (Baseline ? (Start>=Begin || Free<=Begin) : (Start<Begin || Start>=End)) { continue; }
                        const uint32 Id=A->GetAllocCallstackId();
                        if (!ExportStack(Id,C)) { Valid=false; break; }
                        if (HexSourceStacks.Contains(Id)) { ++WithHexSource; }
                        else if (IncompleteStacks.Contains(Id)) { ++UncertainRows; }
                        if (Baseline) { ++BaselineRows; } else { ++BornRows; }
                        // The free time is only needed inside the measurement window. Blank means live at its end.
                        const FString FreeTime=Free<End ? FString::Printf(TEXT("%.15f"),Free) : FString{};
                        if (!Rows.Write(FString::Printf(TEXT("%s,%u,%u,%llu,%llu,%.15f,%s,%u,%u,%u,%s\n"),
                            Baseline ? TEXT("baseline") : TEXT("born"),A->GetStartEventIndex(),A->GetEndEventIndex(),
                            A->GetAddress(),A->GetSize(),Start,*FreeTime,A->GetAllocThreadId(),Id,A->GetTag(),
                            *Quote(P.GetTagFullPath(A->GetTag()))))) { Valid=false; break; }
                    }
                    if (FPlatformTime::Seconds()>=Deadline) { Valid=false; }
                }
            }
            P.CancelQuery(Query);
            return Done && Valid;
        }
    };

    bool ExportMemory(FCk_TraceSession& Trace,const FString& Directory,const FMarks& Marks,int32 Repeat,FString& Error)
    {
        const TraceServices::IAllocationsProvider* P=nullptr;
        const TraceServices::ICallstacksProvider* C=nullptr;
        {
            auto Read=Trace.CreateReadScope();
            P=TraceServices::ReadAllocationsProvider(*Trace.GetSession());
            C=TraceServices::ReadCallstacksProvider(*Trace.GetSession());
        }
        if (!P || !C) { Error=TEXT("The trace lacks allocation/callstack providers."); return false; }
        {
            TraceServices::FProviderReadScopeLock Lock(*P);
            if (!P->IsInitialized() || !P->HasAllocationEvents())
            { Error=TEXT("The memory trace has no allocation events."); return false; }
        }
        FMemoryExport Export;
        if (!Export.Rows.Open(Directory,TEXT("allocations.csv"),TEXT("kind,alloc_event,free_event,address,size,alloc_seconds,free_seconds_if_before_window_end,thread,stack,tag_id,tag_path\n")) ||
            !Export.Stacks.Open(Directory,TEXT("stacks.csv"),TEXT("stack,depth,address,status,module,symbol,file,line\n")))
        { Error=TEXT("Could not open fresh memory evidence files."); return false; }
        for (const bool Baseline : {true,false})
        {
            if (!Export.ExportQuery(*P,*C,Baseline,Marks.At[Repeat][0],Marks.At[Repeat][3]))
            { Error=TEXT("Allocation query failed, timed out or exceeded the bounded output capacity."); return false; }
        }
        if (!Export.Rows.Close() || !Export.Stacks.Close())
        { Error=TEXT("Could not finalize allocation exports."); return false; }
        const auto Summary=FString::Printf(TEXT("repeat,baseline_rows,interval_born_rows,distinct_stacks,rows_with_hex_source,rows_unknown_due_to_missing_frames\n%d,%llu,%llu,%d,%llu,%llu\n"),
            Repeat,Export.BaselineRows,Export.BornRows,Export.SeenStacks.Num(),Export.WithHexSource,Export.UncertainRows);
        if (!FFileHelper::SaveStringToFile(Summary,*FPaths::Combine(Directory,TEXT("memory-coverage.csv"))))
        { Error=TEXT("Could not save stack coverage."); return false; }
        if (!Export.BornRows || !Export.WithHexSource || Export.UncertainRows)
        { Error=TEXT("Incomplete allocation provenance. Inspect memory-coverage.csv. No attributed budget verdict."); return false; }
        return true;
    }

    // Diagnostic observation only: interval-born system allocations exclude all baseline allocations.
    // Neither allocation callsites nor this query establish ownership by a grid. Provider analysis
    // errors (including fabricated heap records) are not exposed/cleared by this exporter.
    bool ExportMemoryInterval(FCk_TraceSession& Trace,const FString& Directory,const FMarks& Marks,
        int32 Repeat,FString& Error)
    {
        using Provider=TraceServices::IAllocationsProvider;
        struct FEvent { uint32 Index; int64 Delta; };
        // Bound temporary memory, not on-disk stack dumps. A limit hit produces a diagnostic row,
        // never a truncated measurement represented as a complete interval.
        constexpr int32 MaxEvents=4*1024*1024;
        TArray<FEvent> Events;
        uint64 Rows=0,Skipped=0,Outside=0,Invalid=0,FilledBytes=0,BeforeTeardownBytes=0,Peak=0;
        uint64 QueryPages=0,OrderErrors=0;
        bool QueryComplete=false;
        bool MetricsComplete=false;
        const double Begin=Marks.At[Repeat][0],Filled=Marks.At[Repeat][1];
        const double BeforeTeardown=Marks.At[Repeat][2],End=Marks.At[Repeat][3];
        const Provider* P=nullptr;
        {
            auto Read=Trace.CreateReadScope();
            P=TraceServices::ReadAllocationsProvider(*Trace.GetSession());
        }
        auto QueryInterval=[&]() -> bool
        {
            if (!P) { Error=TEXT("allocation_provider_missing"); return false; }
            Provider::FQueryHandle Query=0;
            {
                TraceServices::FProviderReadScopeLock Lock(*P);
                if (!P->IsInitialized() || !P->HasAllocationEvents())
                { Error=TEXT("allocation_events_missing"); return false; }
                Query=P->StartQuery({Provider::EQueryRule::AaB,Begin,End,0.,0.});
            }
            if (!Query) { Error=TEXT("query_start_failed"); return false; }
            ON_SCOPE_EXIT { P->CancelQuery(Query); };
            const double Deadline=FPlatformTime::Seconds()+900.;
            while (FPlatformTime::Seconds()<Deadline)
            {
                const auto Status=P->PollQuery(Query);
                if (Status.Status==Provider::EQueryStatus::Done)
                { QueryComplete=true; break; }
                if (Status.Status==Provider::EQueryStatus::Unknown)
                { Error=TEXT("query_status_unknown"); return false; }
                if (Status.Status!=Provider::EQueryStatus::Available)
                { FPlatformProcess::Sleep(.005f); continue; }
                TraceServices::FProviderReadScopeLock Lock(*P);
                for (auto Page=Status.NextResult(); Page.IsValid(); Page=Status.NextResult())
                {
                    ++QueryPages;
                    for (uint32 I=0; I<Page->Num(); ++I)
                    {
                        const auto* A=Page->Get(I);
                        if (!A) { ++Invalid; continue; }
                        if (A->IsHeap() || A->IsSwap() || A->GetRootHeap()!=EMemoryTraceRootHeap::SystemMemory)
                        { ++Skipped; continue; }
                        const double Start=A->GetStartTime(),Free=A->GetEndTime();
                        if (!FMath::IsFinite(Start) || FMath::IsNaN(Free) || Free<Start)
                        { ++Invalid; continue; }
                        if (Start<Begin || Start>=End) { ++Outside; continue; }
                        const uint64 Size=A->GetSize();
                        if (Size>uint64(MAX_int64)) { ++Invalid; continue; }
                        const bool FreeInWindow=Free<End;
                        if (FreeInWindow && A->GetEndEventIndex()<=A->GetStartEventIndex())
                        { ++Invalid; continue; }
                        if (Events.Num()>MaxEvents-(FreeInWindow ? 2 : 1))
                        { Error=TEXT("interval_event_capacity_4194304_exceeded"); return false; }
                        if ((Start<=Filled && Free>Filled && FilledBytes>MAX_uint64-Size) ||
                            (Start<=BeforeTeardown && Free>BeforeTeardown && BeforeTeardownBytes>MAX_uint64-Size))
                        { Error=TEXT("checkpoint_byte_sum_overflow"); return false; }
                        ++Rows;
                        if (Start<=Filled && Free>Filled) { FilledBytes+=Size; }
                        if (Start<=BeforeTeardown && Free>BeforeTeardown) { BeforeTeardownBytes+=Size; }
                        Events.Add(FEvent{A->GetStartEventIndex(),static_cast<int64>(Size)});
                        if (FreeInWindow) { Events.Add(FEvent{A->GetEndEventIndex(),-static_cast<int64>(Size)}); }
                    }
                    if (FPlatformTime::Seconds()>=Deadline)
                    { Error=TEXT("interval_query_timeout_900s"); return false; }
                }
            }
            if (!QueryComplete) { Error=TEXT("interval_query_timeout_900s"); return false; }
            if (Invalid) { Error=TEXT("invalid_allocation_rows"); return false; }
            // Equal timestamps do not define order; use the provider's allocation event indices.
            // Duplicate indices are ambiguous, so refuse a peak instead of choosing a tie order.
            Events.Sort([](const FEvent& A,const FEvent& B) { return A.Index<B.Index; });
            uint64 Live=0;
            for (int32 I=0; I<Events.Num(); ++I)
            {
                const auto& Event=Events[I];
                if (I && Event.Index==Events[I-1].Index)
                { ++OrderErrors; continue; }
                if (Event.Delta>=0)
                {
                    if (Live>MAX_uint64-static_cast<uint64>(Event.Delta))
                    { Error=TEXT("peak_byte_sum_overflow"); return false; }
                    Live+=static_cast<uint64>(Event.Delta);
                    Peak=FMath::Max(Peak,Live);
                }
                else
                {
                    const uint64 Freed=static_cast<uint64>(-Event.Delta);
                    if (Freed>Live) { ++OrderErrors; continue; }
                    Live-=Freed;
                }
            }
            if (OrderErrors) { Error=TEXT("ambiguous_or_unbalanced_allocation_event_order"); return false; }
            if (!Rows) { Error=TEXT("no_interval_born_system_allocations"); return false; }
            return true;
        };
        MetricsComplete=QueryInterval();
        const FString Reason=MetricsComplete
            ? TEXT("observation_only_baseline_ownership_and_provider_lifetime_errors_unqualified") : Error;
        // Blank byte columns on failure: counters describe partial diagnostic work, not valid totals.
        const FString ByteColumns=MetricsComplete
            ? FString::Printf(TEXT("%llu,%llu,%llu"),FilledBytes,BeforeTeardownBytes,Peak) : TEXT(",,");
        const FString Summary=FString::Printf(
            TEXT("repeat,scope,begin_seconds,filled_seconds,preteardown_seconds,end_seconds,query_complete,metrics_complete,query_pages,interval_born_rows,excluded_non_system_heap_swap_rows,outside_interval_rows,invalid_rows,event_order_errors,events,event_capacity,live_at_filled_bytes,live_at_preteardown_bytes,interval_peak_bytes,baseline_included,grid_ownership_qualified,provider_lifetime_diagnostics_checked,provider_error_count,budget_verdict,reason\n")
            TEXT("%d,interval_born_system_allocations,%.15f,%.15f,%.15f,%.15f,%d,%d,%llu,%llu,%llu,%llu,%llu,%llu,%d,%d,%s,0,0,0,unknown,unqualifiable,%s\n"),
            Repeat,Begin,Filled,BeforeTeardown,End,QueryComplete,MetricsComplete,QueryPages,Rows,Skipped,Outside,
            Invalid,OrderErrors,Events.Num(),MaxEvents,*ByteColumns,*Quote(*Reason));
        if (!FFileHelper::SaveStringToFile(Summary,*FPaths::Combine(Directory,TEXT("memory-interval.csv"))))
        { Error=TEXT("could_not_write_interval_diagnostic_csv"); return false; }
        return MetricsComplete;
    }

    bool ExportJob(const FString& Input,const FString& Output,const FString& Mode,
        int32 FirstRepeat,int32 LastRepeat,TFunctionRef<void(const FString&)> RecordError)
    {
        auto& Files=IFileManager::Get();
        FCk_TraceSession Trace;
        if (!Trace.PrepareAnalysisService()) { RecordError(TEXT("TraceServices is unavailable.")); return false; }
        {
            // StartAnalysis synchronously installs session analyzers on the game thread.
            // Restore this process-global setting before polling the offline session.
            auto Modules=FModuleManager::GetModuleChecked<ITraceServicesModule>(TEXT("TraceServices")).GetModuleService();
            if (!Modules) { RecordError(TEXT("TraceServices module selection is unavailable.")); return false; }
            const FName MemoryName(TEXT("TraceModule_Memory"));
            TArray<TraceServices::FModuleInfo> Enabled;
            Modules->GetEnabledModules(Enabled);
            const bool HasMemory=Enabled.ContainsByPredicate([&](const TraceServices::FModuleInfo& Info)
            { return Info.Name==MemoryName; });
            if (!HasMemory && (Mode==TEXT("memory") || Mode==TEXT("memory-interval")))
            { RecordError(TEXT("Memory export requires the memory analyzer module.")); return false; }
            const bool DisableMemory=HasMemory && Mode==TEXT("cpu");
            if (DisableMemory) { Modules->SetModuleEnabled(MemoryName,false); }
            ON_SCOPE_EXIT { if (DisableMemory) { Modules->SetModuleEnabled(MemoryName,true); } };
            if (!Trace.StartAnalysis(Input))
            { RecordError(TEXT("TraceServices could not open the archived trace.")); return false; }
        }
        const double Deadline=FPlatformTime::Seconds()+900.;
        while (!Trace.IsAnalysisComplete() && FPlatformTime::Seconds()<Deadline) { FPlatformProcess::Sleep(0.05f); }
        if (!Trace.IsAnalysisComplete() || !Trace.IsOpen())
        { Trace.RequestStop(); Trace.Close(); RecordError(TEXT("Trace analysis timed out or closed.")); return false; }
        FMarks Marks;
        if (!ExportMarks(Trace,Output,Marks)) { RecordError(TEXT("Could not export the expected 24 ordered repeat bookmarks.")); return false; }
        if (Mode==TEXT("cpu") && !ExportCpu(Trace,Output,Marks))
        { RecordError(TEXT("Missing GameThread, frame track, batch scopes or writable output.")); return false; }
        bool Succeeded=true;
        if (Mode==TEXT("memory") || Mode==TEXT("memory-interval"))
        {
            for (int32 Index=FirstRepeat; Index<=LastRepeat; ++Index)
            {
                const FString Directory=FirstRepeat==LastRepeat ? Output :
                    FPaths::Combine(Output,FString::Printf(TEXT("repeat-%d"),Index));
                FString Error;
                if (Directory!=Output && !Files.MakeDirectory(*Directory,true))
                { RecordError(TEXT("Could not create repeat directory.")); return false; }
                const bool Exported=Mode==TEXT("memory-interval")
                    ? ExportMemoryInterval(Trace,Directory,Marks,Index,Error)
                    : ExportMemory(Trace,Directory,Marks,Index,Error);
                if (!Exported)
                {
                    RecordError(FString::Printf(TEXT("repeat %d: %s"),Index,*Error));
                    Succeeded=false;
                    FFileHelper::SaveStringToFile(Error,*FPaths::Combine(Directory,TEXT("failure.txt")));
                }
            }
        }
        if (!Succeeded) { return false; }
        Trace.Close();
        const TCHAR* Receipt=Mode==TEXT("memory-interval") ? TEXT("observation-complete.txt") : TEXT("complete.txt");
        if (!FFileHelper::SaveStringToFile(Input+TEXT("\n")+Mode+TEXT("\n"),*FPaths::Combine(Output,Receipt)))
        { RecordError(TEXT("Could not finalize trace evidence.")); return false; }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexTraceExport, "Ck.HexGrid.Release.TraceExport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexTraceExport::RunTest(const FString&)
{
    using namespace ck_test_hex_trace;
    const auto Input=FPlatformMisc::GetEnvironmentVariable(TEXT("CK_HEX_TRACE_FILE"));
    const auto Output=FPlatformMisc::GetEnvironmentVariable(TEXT("CK_HEX_TRACE_OUTPUT"));
    const auto Mode=FPlatformMisc::GetEnvironmentVariable(TEXT("CK_HEX_TRACE_MODE"));
    const auto Number=FPlatformMisc::GetEnvironmentVariable(TEXT("CK_HEX_TRACE_REPEAT"));
    const auto Manifest=FPlatformMisc::GetEnvironmentVariable(TEXT("CK_HEX_TRACE_MANIFEST"));
    if (Input.IsEmpty() && Output.IsEmpty() && Mode.IsEmpty() && Manifest.IsEmpty() && Number.IsEmpty())
    { AddInfo(TEXT("Offline trace export skipped. No trace was selected.")); return true; }
    auto& Files=IFileManager::Get();
    if (Output.IsEmpty() || FPaths::IsRelative(Output) || Files.DirectoryExists(*Output) || Files.FileExists(*Output))
    { AddError(TEXT("A fresh absolute trace export directory is required.")); return false; }

    struct FJob { FString Path, Kind; int32 First, Last; };
    TArray<FJob> Jobs;
    if (Manifest.IsEmpty())
    {
        int32 Repeat=0;
        if (Mode==TEXT("memory") || Mode==TEXT("memory-interval"))
        {
            if (Number.Len()!=1 || Number[0]<TEXT('1') || Number[0]>TEXT('5'))
            { AddError(TEXT("Memory export requires CK_HEX_TRACE_REPEAT=1..5.")); return false; }
            Repeat=Number[0]-TEXT('0');
        }
        Jobs.Add(FJob{Input,Mode,Repeat,Repeat});
    }
    else
    {
        TArray<FString> Lines;
        if (!Input.IsEmpty() || !Mode.IsEmpty() || !Number.IsEmpty() || FPaths::IsRelative(Manifest) ||
            Files.FileSize(*Manifest)<=0 || Files.FileSize(*Manifest)>32768 ||
            !FFileHelper::LoadFileToStringArray(Lines,*Manifest) || Lines.Num()<1 || Lines.Num()>4)
        { AddError(TEXT("Expected one to four manifest rows and no single-input selectors.")); return false; }
        for (const FString& Line : Lines)
        {
            FString Kind,Path;
            if (!Line.Split(TEXT("\t"),&Kind,&Path) || Path.Contains(TEXT("\t")))
            { AddError(TEXT("Expected one tab-separated mode/path pair per row.")); return false; }
            Jobs.Add(FJob{Path,Kind,1,5});
        }
    }
    TSet<FString> Paths;
    for (auto& Job : Jobs)
    {
        if (Job.Path.IsEmpty() || FPaths::IsRelative(Job.Path) || !Job.Path.EndsWith(TEXT(".utrace")) ||
            Files.FileSize(*Job.Path)<=0 ||
            (Job.Kind!=TEXT("cpu") && Job.Kind!=TEXT("memory") && Job.Kind!=TEXT("memory-interval")))
        { AddError(TEXT("Every job requires an archived .utrace and mode cpu/memory/memory-interval.")); return false; }
        FPaths::NormalizeFilename(Job.Path);
        FPaths::CollapseRelativeDirectories(Job.Path);
        const FString Key=Job.Path.ToLower();
        if (Paths.Contains(Key)) { AddError(TEXT("Duplicate trace in manifest.")); return false; }
        Paths.Add(Key);
    }
    if (!Files.MakeDirectory(*Output,true)) { AddError(TEXT("Could not create export directory.")); return false; }

    bool AllSucceeded=true;
    for (int32 Index=0; Index<Jobs.Num(); ++Index)
    {
        const auto& Job=Jobs[Index];
        const FString Directory=Manifest.IsEmpty() ? Output :
            FPaths::Combine(Output,FString::Printf(TEXT("case-%d-%s"),Index+1,*Job.Kind));
        if (Directory!=Output && !Files.MakeDirectory(*Directory,true))
        { AddError(TEXT("Could not create case directory.")); AllSucceeded=false; continue; }
        FString Errors;
        auto Record=[&](const FString& Message)
        {
            Errors+=Message+TEXT("\n");
            AddError(FString::Printf(TEXT("%s: %s"),*Job.Path,*Message));
        };
        if (!ExportJob(Job.Path,Directory,Job.Kind,Job.First,Job.Last,Record))
        {
            FFileHelper::SaveStringToFile(Job.Path+TEXT("\n")+Errors,*FPaths::Combine(Directory,TEXT("failure.txt")));
            AllSucceeded=false;
        }
    }
    return AllSucceeded;
}
