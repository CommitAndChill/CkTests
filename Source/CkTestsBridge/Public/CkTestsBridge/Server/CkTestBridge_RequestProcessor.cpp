#include "CkTestBridge_RequestProcessor.h"

#include "CkTestsBridge_Log.h"

#include "CkCore/Ensure/CkEnsure.h"
#include "CkCore/Format/CkFormat.h"
#include "CkCore/Macros/CkMacros.h"

#include "AssetRegistry/AssetRegistryModule.h"

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <HAL/FileManager.h>
#include <HAL/PlatformProcess.h>
#include <Misc/CommandLine.h>
#include <Misc/DateTime.h>
#include <Misc/FileHelper.h>
#include <Misc/Parse.h>
#include <Misc/Paths.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include <Serialization/JsonWriter.h>
#include <UObject/UObjectGlobals.h>

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_bridge_requestprocessor
{
    // Requests older than this (by file mtime) are refused rather than run — a live test bridge must never launch a
    // PIE takeover on a request the driver has long forgotten.
    constexpr auto StaleRequestSeconds = double{5.0 * 60.0};

    static auto
    Write_Json(
        const TSharedPtr<FJsonObject>& InObject,
        const FString& InPath)
        -> bool
    {
        auto JsonString = FString{};
        const auto Writer = TJsonWriterFactory<>::Create(&JsonString);
        if (NOT FJsonSerializer::Serialize(InObject.ToSharedRef(), Writer))
        { return false; }
        return FFileHelper::SaveStringToFile(JsonString, *InPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    static auto
    TryRead_StringArray(
        const TSharedPtr<FJsonObject>& InObject,
        const FString& InField,
        TArray<FString>& OutArray)
        -> bool
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (NOT InObject->TryGetArrayField(InField, Values) || Values == nullptr)
        { return true; }

        auto Parsed = TArray<FString>{};
        for (const auto& Value : *Values)
        {
            if (NOT Value.IsValid() || Value->Type != EJson::String)
            { return false; }
            Parsed.Add(Value->AsString());
        }
        OutArray.Append(MoveTemp(Parsed));
        return true;
    }

    static auto
    Build_EnvJson(
        const FCk_TestBridge_Env& InEnv)
        -> TSharedPtr<FJsonObject>
    {
        auto Env = MakeShared<FJsonObject>();
        Env->SetBoolField(TEXT("live"), InEnv.Live);
        Env->SetNumberField(TEXT("editorPid"), static_cast<double>(InEnv.EditorPid));
        Env->SetBoolField(TEXT("asPendingFullReload"), InEnv.AsPendingFullReload);
        Env->SetBoolField(TEXT("asCompileErrors"), InEnv.AsCompileErrors);

        auto Dirty = TArray<TSharedPtr<FJsonValue>>{};
        for (const auto& Name : InEnv.DirtyPackages)
        { Dirty.Add(MakeShared<FJsonValueString>(Name)); }
        Env->SetArrayField(TEXT("dirtyPackages"), Dirty);

        return Env;
    }

    static auto
    Make_RefusedResult(
        const FString& InReason,
        const FCk_TestBridge_Env& InEnv)
        -> TSharedPtr<FJsonObject>
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("ok"), false);
        Result->SetBoolField(TEXT("refused"), true);
        Result->SetStringField(TEXT("refusalReason"), InReason);
        Result->SetObjectField(TEXT("env"), Build_EnvJson(InEnv));
        Result->SetArrayField(TEXT("perTest"), TArray<TSharedPtr<FJsonValue>>{});
        Result->SetArrayField(TEXT("notFound"), TArray<TSharedPtr<FJsonValue>>{});
        return Result;
    }

    static auto
    Make_ErrorResult(
        const FString& InMessage)
        -> TSharedPtr<FJsonObject>
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("ok"), false);
        Result->SetBoolField(TEXT("refused"), false);
        Result->SetStringField(TEXT("error"), InMessage);
        return Result;
    }

    static auto
    Delete_FilesIn(
        const FString& InDir,
        const TCHAR* InWildcard)
        -> void
    {
        auto& FileManager = IFileManager::Get();
        auto FoundFiles = TArray<FString>{};
        constexpr auto FindFiles = true;
        constexpr auto FindDirectories = false;
        FileManager.FindFiles(FoundFiles, *FPaths::Combine(InDir, InWildcard), FindFiles, FindDirectories);

        for (const auto& FileName : FoundFiles)
        {
            constexpr auto RequireExists = false;
            FileManager.Delete(*FPaths::Combine(InDir, FileName), RequireExists);
        }
    }
}

// --------------------------------------------------------------------------------------------------------------------

FCk_TestBridge_RequestProcessor::
    FCk_TestBridge_RequestProcessor()
    : FCk_TestBridge_RequestProcessor{
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CkTestBridge")), {}, {}}
{
}

// --------------------------------------------------------------------------------------------------------------------

FCk_TestBridge_RequestProcessor::
    FCk_TestBridge_RequestProcessor(
        const FString& InProtocolRoot,
        FCk_TestBridge_RunController::ControllerResolverType InResolveController,
        PreconditionEvaluatorType InEvaluatePreconditions)
    // Absolute on purpose: these paths are handed to the external driver via server.json — a relative form only
    // resolves from the editor's cwd, not the submitter's.
    : _EvaluatePreconditions{MoveTemp(InEvaluatePreconditions)}
    , _Root{FPaths::ConvertRelativePathToFull(InProtocolRoot)}
    , _RequestsDir{FPaths::Combine(_Root, TEXT("Requests"))}
    , _ResultsDir{FPaths::Combine(_Root, TEXT("Results"))}
    , _ProgressDir{FPaths::Combine(_Root, TEXT("Progress"))}
    , _ServerStatusPath{FPaths::Combine(_Root, TEXT("server.json"))}
    , _Runner{MoveTemp(InResolveController)}
{
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    Startup(
        ECk_TestBridge_StaleRequestPolicy InStaleRequestPolicy)
    -> bool
{
    using namespace ck_test_bridge_requestprocessor;

    auto& FileManager = IFileManager::Get();

    CK_ENSURE_IF_NOT(NOT _Busy && NOT _CapturedResponse.IsValid(),
        TEXT("[Server] cannot restart while an active request or retained terminal response is owned"))
    { return false; }

    constexpr auto MakeTree = true;
    FileManager.MakeDirectory(*_RequestsDir, MakeTree);
    FileManager.MakeDirectory(*_ResultsDir, MakeTree);
    FileManager.MakeDirectory(*_ProgressDir, MakeTree);

    if (NOT FileManager.DirectoryExists(*_RequestsDir) ||
        NOT FileManager.DirectoryExists(*_ResultsDir) ||
        NOT FileManager.DirectoryExists(*_ProgressDir))
    {
        ck::tests_bridge::Error(TEXT("[Server] could not create protocol dirs under [{}]"), _Root);
        return false;
    }

    if (InStaleRequestPolicy == ECk_TestBridge_StaleRequestPolicy::WipeStale)
    { Delete_FilesIn(_RequestsDir, TEXT("*.json")); }

    {
        auto& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
        AssetRegistry.WaitForCompletion();
    }

    _StartedAtIso = FDateTime::UtcNow().ToIso8601();
    return Do_WriteStatusFile(false, {});
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    Do_WriteStatusFile(
        bool InBusy,
        const FString& InCurrentRequestId)
    -> bool
{
    using namespace ck_test_bridge_requestprocessor;

    auto Status = MakeShared<FJsonObject>();
    Status->SetNumberField(TEXT("pid"), static_cast<double>(FPlatformProcess::GetCurrentProcessId()));
    Status->SetStringField(TEXT("startedAt"), _StartedAtIso);
    Status->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Status->SetNumberField(TEXT("protocolVersion"), static_cast<double>(Ck_TestBridge_ProtocolVersion));
    Status->SetStringField(TEXT("requestsDir"), _RequestsDir);
    Status->SetStringField(TEXT("resultsDir"), _ResultsDir);
    Status->SetStringField(TEXT("progressDir"), _ProgressDir);
    // Distinguish an intentionally launched warm server from an opted-in interactive editor.
    Status->SetStringField(TEXT("serverKind"),
        FParse::Param(FCommandLine::Get(), TEXT("CkTestBridgeServe")) ? TEXT("warmServer") : TEXT("liveEditor"));
    Status->SetBoolField(TEXT("busy"), InBusy);
    Status->SetBoolField(TEXT("faulted"), Get_HasTerminalFault());
    if (Get_HasTerminalFault())
    { Status->SetStringField(TEXT("faultReason"), _TerminalFault); }
    if (NOT InCurrentRequestId.IsEmpty())
    { Status->SetStringField(TEXT("currentRequestId"), InCurrentRequestId); }
    // This is request-processing activity, not proof of editor liveness: PIE can starve our ticker.
    Status->SetStringField(TEXT("lastActivityAt"), FDateTime::UtcNow().ToIso8601());
    const auto Written = Write_Json(Status, _ServerStatusPath);
    if (NOT Written)
    {
        ck::tests_bridge::Error(TEXT("[Server] could not write ownership heartbeat [{}]"), _ServerStatusPath);
        return false;
    }
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    ProcessPending(
        float InDeltaTime)
    -> FCk_TestBridge_ProcessResult
{
    using namespace ck_test_bridge_requestprocessor;

    auto Outcome = FCk_TestBridge_ProcessResult{};

    if (Get_HasTerminalFault())
    {
        // Retain ownership, but never retry publication or admission from an ordinary poll.
        Do_WriteStatusFile(true, _CurrentRequestId);
        Outcome.TerminalFault = true;
        return Outcome;
    }

    // A run in flight — pump it, don't claim anything new. One request at a time.
    if (_Busy)
    {
        Do_PumpActiveRun(InDeltaTime);
        if (Get_HasTerminalFault())
        {
            Outcome.TerminalFault = true;
            return Outcome;
        }
        if (_CapturedResponse.IsValid())
        { return Do_PublishCapturedResponse(); }
        return Outcome;
    }

    auto& FileManager = IFileManager::Get();

    auto RequestFiles = TArray<FString>{};
    constexpr auto FindFiles = true;
    constexpr auto FindDirectories = false;
    FileManager.FindFiles(RequestFiles, *FPaths::Combine(_RequestsDir, TEXT("*.json")), FindFiles, FindDirectories);
    RequestFiles.Sort();

    for (const auto& RequestFileName : RequestFiles)
    {
        const auto RequestPath = FPaths::Combine(_RequestsDir, RequestFileName);

        // Skip sub-second-old files (mid-write settle guard) — the next poll picks them up settled.
        const auto FileAge = FDateTime::UtcNow() - FileManager.GetTimeStamp(*RequestPath);
        if (FileAge < FTimespan::FromSeconds(1.0))
        { continue; }

        Do_ClaimRequest(RequestPath);
        Outcome.AnyProcessed = true;
        if (Get_HasTerminalFault())
        {
            Outcome.TerminalFault = true;
            return Outcome;
        }
        if (_CapturedResponse.IsValid())
        { return Do_PublishCapturedResponse(); }

        // Claim at most ONE request per pass: either a run is now in flight (busy) or it was refused/handled.
        break;
    }

    return Outcome;
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    Do_ClaimRequest(
        const FString& InRequestPath)
    -> void
{
    using namespace ck_test_bridge_requestprocessor;

    auto& FileManager = IFileManager::Get();
    const auto RequestId  = FPaths::GetBaseFilename(InRequestPath);
    const auto ResultPath = FPaths::Combine(_ResultsDir, RequestId + TEXT(".json"));

    _CurrentRequestId = RequestId;
    _CurrentRequestPath = InRequestPath;
    _CurrentResultPath = ResultPath;

    auto JsonString = FString{};
    if (NOT FFileHelper::LoadFileToString(JsonString, *InRequestPath))
    {
        Do_CaptureResponse(Make_ErrorResult(TEXT("Could not read request file")));
        return;
    }

    auto Request = TSharedPtr<FJsonObject>{};
    const auto Reader = TJsonReaderFactory<>::Create(JsonString);
    if (NOT FJsonSerializer::Deserialize(Reader, Request) || NOT Request.IsValid())
    {
        Do_CaptureResponse(Make_ErrorResult(TEXT("Malformed request json")));
        return;
    }

    auto Op = FString{};
    Request->TryGetStringField(TEXT("op"), Op);

    if (Op.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("ok"), true);
        Do_CaptureResponse(Result, true);
        ck::tests_bridge::Display(TEXT("[Server] quit requested"));
        return;
    }

    if (NOT Op.Equals(TEXT("runTests"), ESearchCase::IgnoreCase))
    {
        Do_CaptureResponse(Make_ErrorResult(ck::Format_UE(TEXT("Unknown op [{}] (expected runTests | quit)"), Op)));
        return;
    }

    // ---- request-level refusals (do not need the editor env) ----
    auto RequestLevelRefusal = TOptional<FString>{};

    auto ProtocolVersion = int32{Ck_TestBridge_ProtocolVersion};
    if (Request->TryGetNumberField(TEXT("protocolVersion"), ProtocolVersion) && ProtocolVersion != Ck_TestBridge_ProtocolVersion)
    { RequestLevelRefusal = FString{TEXT("protocolVersion")}; }

    if (NOT RequestLevelRefusal.IsSet())
    {
        const auto FileAge = FDateTime::UtcNow() - FileManager.GetTimeStamp(*InRequestPath);
        if (FileAge.GetTotalSeconds() > StaleRequestSeconds)
        { RequestLevelRefusal = FString{TEXT("staleRequest")}; }
    }

    if (NOT RequestLevelRefusal.IsSet())
    {
        auto SubmitterPid = int32{0};
        if (Request->TryGetNumberField(TEXT("submitterPid"), SubmitterPid) &&
            SubmitterPid > 0 &&
            NOT FPlatformProcess::IsApplicationRunning(static_cast<uint32>(SubmitterPid)))
        { RequestLevelRefusal = FString{TEXT("submitterGone")}; }
    }

    // ---- options ----
    auto AllowDirtyWorld    = false;
    auto PerTestStallSeconds = double{300.0};
    auto WallClockCapSeconds = double{3600.0};

    const TSharedPtr<FJsonObject>* Options = nullptr;
    if (Request->TryGetObjectField(TEXT("options"), Options) && Options != nullptr)
    {
        (*Options)->TryGetBoolField(TEXT("allowDirtyWorld"), AllowDirtyWorld);
        (*Options)->TryGetNumberField(TEXT("perTestStallSeconds"), PerTestStallSeconds);
        (*Options)->TryGetNumberField(TEXT("wallClockCapSeconds"), WallClockCapSeconds);
    }

    // ---- precondition env (always fills OutEnv; returns the precondition-level refusal if any) ----
    auto Env = FCk_TestBridge_Env{};
    constexpr auto NotBusyForThisRequest = false;
    const auto PreconditionRefusal = _EvaluatePreconditions
        ? _EvaluatePreconditions(AllowDirtyWorld, NotBusyForThisRequest, Env)
        : FCk_TestBridge_Preconditions::Evaluate(AllowDirtyWorld, NotBusyForThisRequest, Env);

    const auto Refusal = RequestLevelRefusal.IsSet() ? RequestLevelRefusal : PreconditionRefusal;
    if (Refusal.IsSet())
    {
        Do_CaptureResponse(Make_RefusedResult(Refusal.GetValue(), Env));
        ck::tests_bridge::Display(TEXT("[Server] refused request [{}] — {}"), RequestId, Refusal.GetValue());
        return;
    }

    // ---- accept: mark busy and start the run (non-blocking) ----
    auto RunRequest = FCk_TestBridge_RunRequest{};
    if (NOT TryRead_StringArray(Request, TEXT("tests"), RunRequest.Tests))
    {
        Do_CaptureResponse(Make_ErrorResult(TEXT("Malformed tests array: every element must be a string")));
        return;
    }
    RunRequest.PerTestStallSeconds = static_cast<float>(PerTestStallSeconds);
    RunRequest.WallClockCapSeconds = static_cast<float>(WallClockCapSeconds);

    _Busy                = true;
    _CurrentRequestId    = RequestId;
    _CurrentRequestPath  = InRequestPath;
    _CurrentResultPath   = ResultPath;
    _CurrentProgressPath = FPaths::Combine(_ProgressDir, RequestId + TEXT(".jsonl"));
    _CurrentEnv          = Env;

    auto Callbacks = FCk_TestBridge_RunCallbacks{};
    Callbacks.OnAccepted = [this](int32 InTestCount) -> void
    {
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("event"), TEXT("accepted"));
        Event->SetNumberField(TEXT("testCount"), static_cast<double>(InTestCount));
        Do_AppendProgress(Event);
    };
    Callbacks.OnTestStarted = [this](const FString& InPath) -> void
    {
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("event"), TEXT("testStarted"));
        Event->SetStringField(TEXT("path"), InPath);
        Do_AppendProgress(Event);
    };
    Callbacks.OnTestCompleted = [this](const FString& InPath, ECk_TestBridge_TestResult InResult, double InDurationSec) -> void
    {
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("event"), TEXT("testCompleted"));
        Event->SetStringField(TEXT("path"), InPath);
        Event->SetStringField(TEXT("result"), ToString(InResult));
        Event->SetNumberField(TEXT("durationSec"), InDurationSec);
        Do_AppendProgress(Event);
    };

    Do_WriteStatusFile(true, _CurrentRequestId);
    _Runner.Begin(RunRequest, Callbacks);

    ck::tests_bridge::Display(TEXT("[Server] accepted request [{}] — {} test(s)"), RequestId, RunRequest.Tests.Num());
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    Do_PumpActiveRun(
        float InDeltaTime)
    -> void
{
    using namespace ck_test_bridge_requestprocessor;

    _Runner.Tick(InDeltaTime);

    // Record request-processing activity; a PIE test can starve this ticker until it returns.
    Do_WriteStatusFile(true, _CurrentRequestId);

    if (NOT _Runner.Is_Complete())
    { return; }

    {
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("event"), TEXT("runCompleted"));
        Do_AppendProgress(Event);
    }

    const auto& RunResult = _Runner.Get_Result();

    // ok = every requested test that ran passed (Success/Skipped) AND nothing was NotFound.
    auto Ok = RunResult.Error.IsEmpty() && RunResult.NotFound.Num() == 0;
    for (const auto& PerTest : RunResult.PerTest)
    {
        if (PerTest.Result != ECk_TestBridge_TestResult::Success && PerTest.Result != ECk_TestBridge_TestResult::Skipped)
        { Ok = false; }
    }

    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), Ok);
    if (NOT RunResult.Error.IsEmpty())
    { Result->SetStringField(TEXT("error"), RunResult.Error); }
    Result->SetBoolField(TEXT("refused"), false);
    Result->SetField(TEXT("refusalReason"), MakeShared<FJsonValueNull>());
    Result->SetObjectField(TEXT("env"), Build_EnvJson(_CurrentEnv));

    auto PerTestArray = TArray<TSharedPtr<FJsonValue>>{};
    for (const auto& PerTest : RunResult.PerTest)
    {
        auto Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("path"), PerTest.Path);
        Obj->SetStringField(TEXT("result"), ToString(PerTest.Result));
        Obj->SetNumberField(TEXT("durationSec"), PerTest.DurationSec);

        auto Entries = TArray<TSharedPtr<FJsonValue>>{};
        for (const auto& Entry : PerTest.Entries)
        {
            auto EntryObj = MakeShared<FJsonObject>();
            EntryObj->SetStringField(TEXT("type"), Entry.Type);
            EntryObj->SetStringField(TEXT("message"), Entry.Message);
            Entries.Add(MakeShared<FJsonValueObject>(EntryObj));
        }
        Obj->SetArrayField(TEXT("entries"), Entries);

        PerTestArray.Add(MakeShared<FJsonValueObject>(Obj));
    }
    Result->SetArrayField(TEXT("perTest"), PerTestArray);

    auto NotFoundArray = TArray<TSharedPtr<FJsonValue>>{};
    for (const auto& NotFound : RunResult.NotFound)
    { NotFoundArray.Add(MakeShared<FJsonValueString>(NotFound)); }
    Result->SetArrayField(TEXT("notFound"), NotFoundArray);

    Do_CaptureResponse(Result);
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    Do_CaptureResponse(
        const TSharedPtr<FJsonObject>& InResponse,
        bool InQuitRequested)
    -> void
{
    CK_ENSURE_IF_NOT(NOT _CapturedResponse.IsValid(), TEXT("[Server] attempted to replace a captured terminal response"))
    {
        Do_FaultPublication(TEXT("Attempted to replace a captured terminal response"));
        return;
    }

    _CapturedResponse = InResponse;
    _CapturedQuitRequested = InQuitRequested;
    _Busy = true;
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    Do_FaultPublication(
        const FString& InReason)
    -> void
{
    _TerminalFault = InReason;
    // Keep the live PID claim. Releasing it would invite another server to re-execute this request.
    Do_WriteStatusFile(true, _CurrentRequestId);
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    Do_PublishCapturedResponse()
    -> FCk_TestBridge_ProcessResult
{
    auto Outcome = FCk_TestBridge_ProcessResult{};
    Outcome.TerminalFault = true;

    CK_ENSURE_IF_NOT(_CapturedResponse.IsValid(), TEXT("[Server] no captured terminal response to publish"))
    {
        Do_FaultPublication(TEXT("No captured terminal response to publish"));
        return Outcome;
    }

    auto& FileManager = IFileManager::Get();
    // Revalidate the installed bytes on explicit recovery too: the result could have disappeared
    // since a deletion fault. Never consume the request based only on an in-memory installed flag.
    auto Json = FString{};
    const auto Writer = TJsonWriterFactory<>::Create(&Json);
    const auto Serialized = FJsonSerializer::Serialize(_CapturedResponse.ToSharedRef(), Writer);
    CK_ENSURE_IF_NOT(Serialized,
        TEXT("[Server] terminal response serialization failed for [{}]; retained for Ck.TestBridge.RecoverPublication"),
        _CurrentRequestId)
    {
        Do_FaultPublication(TEXT("Terminal response serialization failed"));
        return Outcome;
    }

    if (FileManager.FileExists(*_CurrentResultPath))
    {
        // Never overwrite another outcome. A byte-identical result is already installed and
        // only the outstanding request deletion remains (for example after explicit recovery).
        auto Existing = FString{};
        const auto ReadExisting = FFileHelper::LoadFileToString(Existing, *_CurrentResultPath);
        if (NOT ReadExisting || Existing != Json)
        {
            ck::tests_bridge::Error(TEXT("[Server] terminal result path [{}] contains a conflicting response; retained for Ck.TestBridge.RecoverPublication"),
                _CurrentResultPath);
            Do_FaultPublication(TEXT("Terminal result path contains a conflicting response"));
            return Outcome;
        }
    }
    else
    {
        const auto TempPath = _CurrentResultPath + TEXT(".tmp");
        const auto Saved = FFileHelper::SaveStringToFile(Json, *TempPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if (NOT Saved)
        {
            ck::tests_bridge::Error(TEXT("[Server] could not write terminal response [{}]; retained for Ck.TestBridge.RecoverPublication"), TempPath);
            Do_FaultPublication(TEXT("Could not write terminal response temp file"));
            return Outcome;
        }

        // The selected engine's generic Move deletes the destination for Replace=true and
        // sleeps/retries unless DoNotRetryOrError=true. Use one non-replacing install attempt.
        // Adjacent temp publication avoids exposing a partial JSON document; it is not a
        // crash-durability guarantee and never removes an already-published result.
        constexpr auto ReplaceExisting = false;
        constexpr auto EvenIfReadOnly = false;
        constexpr auto PreserveAttributes = false;
        constexpr auto DoNotRetryOrError = true;
        const auto Installed = FileManager.Move(*_CurrentResultPath, *TempPath, ReplaceExisting,
            EvenIfReadOnly, PreserveAttributes, DoNotRetryOrError);
        if (NOT Installed)
        {
            ck::tests_bridge::Error(TEXT("[Server] could not install terminal response [{}]; retained for Ck.TestBridge.RecoverPublication"),
                _CurrentResultPath);
            Do_FaultPublication(TEXT("Could not install terminal response"));
            return Outcome;
        }
    }

    constexpr auto RequireRequestExists = false;
    constexpr auto DeleteReadOnlyRequest = false;
    constexpr auto QuietDelete = true;
    const auto Deleted = FileManager.Delete(*_CurrentRequestPath, RequireRequestExists,
        DeleteReadOnlyRequest, QuietDelete);
    if (NOT Deleted)
    {
        ck::tests_bridge::Error(TEXT("[Server] could not delete published request [{}]; retained for Ck.TestBridge.RecoverPublication"), _CurrentRequestPath);
        Do_FaultPublication(TEXT("Could not delete published request"));
        return Outcome;
    }

    Outcome.TerminalFault = false;
    Outcome.AnyProcessed = true;
    Outcome.QuitRequested = _CapturedQuitRequested;
    ck::tests_bridge::Display(TEXT("[Server] request [{}] response published and request removed"), _CurrentRequestId);

    _CapturedResponse.Reset();
    _CapturedQuitRequested = false;
    _TerminalFault.Empty();
    _Runner.Reset();
    _Busy = false;
    _CurrentRequestId.Empty();
    _CurrentRequestPath.Empty();
    _CurrentResultPath.Empty();
    _CurrentProgressPath.Empty();
    _CurrentEnv = FCk_TestBridge_Env{};
    Do_WriteStatusFile(false, {});
    return Outcome;
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    Request_RecoverPublication()
    -> FCk_TestBridge_ProcessResult
{
    if (NOT Get_HasTerminalFault())
    { return {}; }

    _TerminalFault.Empty();
    return Do_PublishCapturedResponse();
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    Do_AppendProgress(
        const TSharedPtr<FJsonObject>& InEvent)
    -> void
{
    if (_CurrentProgressPath.IsEmpty())
    { return; }

    auto Line = FString{};
    const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
    FJsonSerializer::Serialize(InEvent.ToSharedRef(), Writer);
    Line.Append(LINE_TERMINATOR);

    // Append + flush per line so an external tailer sees each event immediately.
    constexpr auto FileWriteFlags = FILEWRITE_Append;
    FFileHelper::SaveStringToFile(
        Line, *_CurrentProgressPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FileWriteFlags);
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    Shutdown()
    -> void
{
    constexpr auto RequireExists = false;
    IFileManager::Get().Delete(*_ServerStatusPath, RequireExists);
}

// --------------------------------------------------------------------------------------------------------------------

auto
    FCk_TestBridge_RequestProcessor::
    Has_PendingRequests() const
    -> bool
{
    auto& FileManager = IFileManager::Get();
    auto RequestFiles = TArray<FString>{};
    constexpr auto FindFiles = true;
    constexpr auto FindDirectories = false;
    FileManager.FindFiles(RequestFiles, *FPaths::Combine(_RequestsDir, TEXT("*.json")), FindFiles, FindDirectories);
    return RequestFiles.Num() > 0;
}

// --------------------------------------------------------------------------------------------------------------------
