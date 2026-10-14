#include "Misc/AutomationTest.h"

#include "CkInsightsAnalyzer/Core/CkTraceSession.h"

#include "Common/ProviderLock.h"
#include "TraceServices/Model/AllocationsProvider.h"
#include "TraceServices/Model/Regions.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace ck_test_procedural_action_perf_analysis
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr int32 MaxRegions = 512;
    constexpr double AnalysisTimeoutSeconds = 60.0;
    constexpr double QueryTimeoutSeconds = 60.0;

    struct FRegion
    {
        FString Name;
        double BeginSeconds = 0.0;
        double EndSeconds = 0.0;
    };

    struct FAllocationSummary
    {
        uint64 Count = 0;
        uint64 Bytes = 0;
    };

    auto
        EscapeCsv(
            FString Value)
        -> FString
    {
        Value.ReplaceInline(TEXT("\""), TEXT("\"\""));
        return FString::Printf(TEXT("\"%s\""), *Value);
    }

    auto
        CountGameThreadAllocations(
            const TraceServices::IAllocationsProvider& InProvider,
            const FRegion& InRegion,
            uint32 InGameThreadId,
            double InDeadlineSeconds,
            FAllocationSummary& OutSummary,
            FString& OutError)
        -> bool
    {
        using FProvider = TraceServices::IAllocationsProvider;
        const FProvider::FQueryParams Params{
            FProvider::EQueryRule::AaB,
            InRegion.BeginSeconds,
            InRegion.EndSeconds,
            0.0,
            0.0
        };

        FProvider::FQueryHandle Query = 0;
        {
            TraceServices::FProviderReadScopeLock ProviderReadScope(InProvider);
            Query = InProvider.StartQuery(Params);
        }
        if (Query == 0)
        {
            OutError = FString::Printf(TEXT("Allocation AaB query was rejected for region %s"),
                *InRegion.Name);
            return false;
        }

        bool bComplete = false;
        while (FPlatformTime::Seconds() < InDeadlineSeconds)
        {
            const FProvider::FQueryStatus Status = InProvider.PollQuery(Query);
            if (Status.Status == FProvider::EQueryStatus::Done)
            {
                bComplete = true;
                break;
            }
            if (Status.Status == FProvider::EQueryStatus::Unknown)
            {
                OutError = FString::Printf(TEXT("Allocation query entered unknown state for region %s"),
                    *InRegion.Name);
                break;
            }
            if (Status.Status == FProvider::EQueryStatus::Available)
            {
                TraceServices::FProviderReadScopeLock ProviderReadScope(InProvider);
                for (auto Result = Status.NextResult(); Result.IsValid(); Result = Status.NextResult())
                {
                    for (uint32 Index = 0; Index < Result->Num(); ++Index)
                    {
                        const auto* Allocation = Result->Get(Index);
                        if (Allocation == nullptr
                            || Allocation->GetAllocThreadId() != InGameThreadId
                            || Allocation->IsHeap()
                            || Allocation->IsSwap()
                            || Allocation->GetStartTime() < InRegion.BeginSeconds
                            || Allocation->GetStartTime() >= InRegion.EndSeconds)
                        { continue; }

                        const uint64 Size = Allocation->GetSize();
                        if (OutSummary.Count == MAX_uint64 || OutSummary.Bytes > MAX_uint64 - Size)
                        {
                            OutError = FString::Printf(TEXT("Allocation counters overflowed for region %s"),
                                *InRegion.Name);
                            break;
                        }
                        ++OutSummary.Count;
                        OutSummary.Bytes += Size;
                    }
                    if (NOT OutError.IsEmpty())
                    { break; }
                }
                if (NOT OutError.IsEmpty())
                { break; }
                continue;
            }
            FPlatformProcess::Sleep(0.005f);
        }

        // FAllocationsQuery::Poll reports Done without deleting; Cancel waits and deletes the query.
        InProvider.CancelQuery(Query);
        if (NOT bComplete && OutError.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Allocation query timed out for region %s"),
                *InRegion.Name);
        }
        return bComplete && OutError.IsEmpty();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PerfAnalysis_GameThreadAllocations,
    "Ck.ProceduralAnimation.Action.Perf.TraceGameThreadAllocations",
    ck_test_procedural_action_perf_analysis::TestFlags)

auto
    FCkTest_ProceduralAction_PerfAnalysis_GameThreadAllocations::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_perf_analysis;

    const FString TracePath = FPlatformMisc::GetEnvironmentVariable(
        TEXT("CK_PROCEDURAL_ACTION_TRACE_FILE"));
    if (TracePath.IsEmpty())
    {
        AddInfo(TEXT("Skipped: CK_PROCEDURAL_ACTION_TRACE_FILE is unset; no performance evidence was produced."));
        return true;
    }
    if (NOT IFileManager::Get().FileExists(*TracePath))
    {
        AddError(FString::Printf(TEXT("Trace file does not exist: %s"), *TracePath));
        return false;
    }

    FCk_TraceSession Trace;
    const auto Fail = [this, &Trace](const FString& InError) -> bool
    {
        AddError(InError);
        Trace.RequestStop();
        Trace.Close();
        return false;
    };
    if (NOT Trace.PrepareAnalysisService() || NOT Trace.StartAnalysis(TracePath))
    { return Fail(TEXT("TraceServices could not start analysis of the requested trace.")); }

    const double AnalysisDeadline = FPlatformTime::Seconds() + AnalysisTimeoutSeconds;
    while (NOT Trace.IsAnalysisComplete() && FPlatformTime::Seconds() < AnalysisDeadline)
    { FPlatformProcess::Sleep(0.005f); }
    if (NOT Trace.IsAnalysisComplete())
    { return Fail(TEXT("TraceServices analysis timed out.")); }
    if (NOT Trace.IsOpen())
    { return Fail(TEXT("TraceServices analysis finished without an open session.")); }

    const uint32 GameThreadId = Trace.GetGameThreadId();
    if (GameThreadId == static_cast<uint32>(INDEX_NONE))
    { return Fail(TEXT("Trace has no identifiable GameThread.")); }
    bool bNamedGameThread = false;
    for (const TraceServices::FThreadInfo& Thread : Trace.GetThreadInfos())
    {
        if (Thread.Id == GameThreadId && Thread.Name != nullptr
            && FCString::Strcmp(Thread.Name, TEXT("GameThread")) == 0)
        { bNamedGameThread = true; break; }
    }
    if (NOT bNamedGameThread)
    {
        return Fail(TEXT("Trace GameThread ID was inferred from CPU events, not a thread named GameThread; allocation evidence is ambiguous."));
    }

    TArray<FRegion> Regions;
    const TraceServices::IAllocationsProvider* Allocations = nullptr;
    FString ReadError;
    {
        auto SessionReadScope = Trace.CreateReadScope();
        const auto* Session = Trace.GetSession();
        if (Session == nullptr)
        { ReadError = TEXT("TraceServices session is missing."); }
        else
        {
            const auto* RegionProvider = Session->ReadProvider<TraceServices::IRegionProvider>(
                TraceServices::GetRegionProviderName());
            Allocations = TraceServices::ReadAllocationsProvider(*Session);
            if (RegionProvider == nullptr)
            { ReadError = TEXT("Trace has no Regions provider."); }
            else if (Allocations == nullptr)
            { ReadError = TEXT("Trace has no Allocations provider."); }
            else
            {
                {
                    TraceServices::FProviderReadScopeLock RegionReadScope(*RegionProvider);
                    RegionProvider->GetDefaultTimeline().EnumerateRegions(
                        0.0, Trace.GetDurationSeconds(),
                        [&Regions, &ReadError](const TraceServices::FTimeRegion& InRegion) -> bool
                        {
                            if (InRegion.Timer == nullptr || InRegion.Timer->Name == nullptr
                                || NOT FStringView(InRegion.Timer->Name).StartsWith(TEXT("CkActionPERF_")))
                            { return true; }
                            if (NOT FMath::IsFinite(InRegion.BeginTime)
                                || NOT FMath::IsFinite(InRegion.EndTime)
                                || InRegion.EndTime <= InRegion.BeginTime)
                            {
                                ReadError = TEXT("A CkActionPERF_ region is not closed with a finite interval.");
                                return false;
                            }
                            if (Regions.Num() >= MaxRegions)
                            {
                                ReadError = TEXT("Trace exceeds the 512-region analyzer budget.");
                                return false;
                            }
                            Regions.Add({FString(InRegion.Timer->Name),
                                InRegion.BeginTime, InRegion.EndTime});
                            return true;
                        });
                }
                if (ReadError.IsEmpty())
                {
                    TraceServices::FProviderReadScopeLock AllocationReadScope(*Allocations);
                    if (NOT Allocations->IsInitialized() || NOT Allocations->HasAllocationEvents())
                    { ReadError = TEXT("Trace has no initialized allocation event stream."); }
                }
            }
        }
    }
    if (NOT ReadError.IsEmpty())
    { return Fail(ReadError); }
    if (Regions.IsEmpty())
    { return Fail(TEXT("Trace has no closed CkActionPERF_ regions.")); }

    Regions.Sort([](const FRegion& Left, const FRegion& Right)
    {
        if (Left.BeginSeconds != Right.BeginSeconds)
        { return Left.BeginSeconds < Right.BeginSeconds; }
        return Left.Name < Right.Name;
    });

    FString Csv = TEXT("trace,region,begin_s,end_s,duration_s,game_thread_id,allocation_count,allocation_bytes,scope\n");
    const double QueryDeadline = FPlatformTime::Seconds() + QueryTimeoutSeconds;
    for (const FRegion& Region : Regions)
    {
        FAllocationSummary Summary;
        FString QueryError;
        if (NOT CountGameThreadAllocations(*Allocations, Region, GameThreadId,
            QueryDeadline, Summary, QueryError))
        { return Fail(QueryError); }

        Csv += FString::Printf(TEXT("%s,%s,%.9f,%.9f,%.9f,%u,%llu,%llu,whole_GameThread_nonheap_nonswap\n"),
            *EscapeCsv(FPaths::GetCleanFilename(TracePath)),
            *EscapeCsv(Region.Name),
            Region.BeginSeconds,
            Region.EndSeconds,
            Region.EndSeconds - Region.BeginSeconds,
            GameThreadId,
            static_cast<unsigned long long>(Summary.Count),
            static_cast<unsigned long long>(Summary.Bytes));
    }

    const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(),
        TEXT("Profiling"), TEXT("ProceduralActions"));
    if (NOT IFileManager::Get().MakeDirectory(*OutputDirectory, true))
    { return Fail(FString::Printf(TEXT("Cannot create profiling output directory: %s"), *OutputDirectory)); }
    const FString OutputPath = FPaths::Combine(OutputDirectory,
        FPaths::GetBaseFilename(TracePath) + TEXT("-GameThreadAllocations.csv"));
    if (NOT FFileHelper::SaveStringToFile(Csv, *OutputPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { return Fail(FString::Printf(TEXT("Cannot save allocation CSV: %s"), *OutputPath)); }

    Trace.Close();
    AddInfo(FString::Printf(TEXT("Copied whole-GameThread allocation events for %d regions to %s"),
        Regions.Num(), *OutputPath));
    return true;
}
