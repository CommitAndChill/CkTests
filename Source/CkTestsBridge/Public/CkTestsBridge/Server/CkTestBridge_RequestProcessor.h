#pragma once

#include "CoreMinimal.h"

#include "CkCore/Macros/CkMacros.h"

#include "CkTestsBridge/Server/CkTestBridge_Preconditions.h"
#include "CkTestsBridge/Server/CkTestBridge_RunController.h"

// --------------------------------------------------------------------------------------------------------------------
// The request-file protocol CORE shared by the -CkTestBridgeServe warm server and the interactive-editor bridge.
// Mirrors CkAssetExporter's FCk_AssetExporter_RequestProcessor, but test runs are ASYNCHRONOUS: claiming a request
// kicks a non-blocking RunController and returns; the result is written on a LATER poll once the run completes.
//
// On-disk protocol (all absolute, computed once at construction), under <ProjectSaved>/CkTestBridge/:
//   Requests/<id>.json   — one request dropped by the driver (op = runTests | quit)
//   Results/<id>.json    — the verdict, written BEFORE the request is deleted
//   Progress/<id>.jsonl  — append-only, one JSON object per line, flushed per line
//   server.json          — pid / startedAt / project / protocolVersion / busy / currentRequestId / lastActivityAt
//
// A terminal I/O failure retains the captured response and request ownership until explicit recovery.
// It never re-runs tests or retries publication from an ordinary poll.
// --------------------------------------------------------------------------------------------------------------------

// Protocol version advertised in server.json and enforced against a request's optional "protocolVersion" field.
inline constexpr auto Ck_TestBridge_ProtocolVersion = int32{1};

// --------------------------------------------------------------------------------------------------------------------

struct FCk_TestBridge_ProcessResult
{
    bool QuitRequested = false;
    bool AnyProcessed  = false;
    bool TerminalFault = false;
};

// --------------------------------------------------------------------------------------------------------------------

enum class ECk_TestBridge_StaleRequestPolicy : uint8
{
    // Delete every queued request at startup. The dedicated warm server OWNS the queue at boot.
    WipeStale,

    // Leave queued requests in place. The editor bridge uses this so it never discards work someone queued and can
    // re-claim on a request that triggered the re-claim.
    PreserveExisting
};

// --------------------------------------------------------------------------------------------------------------------

class CKTESTSBRIDGE_API FCk_TestBridge_RequestProcessor
{
public:
    using PreconditionEvaluatorType = TFunction<TOptional<FString>(bool, bool, FCk_TestBridge_Env&)>;

    FCk_TestBridge_RequestProcessor();

    // Alternate protocol root and external-environment providers; request parsing, runner state,
    // and file publication still follow the normal ProcessPending path.
    FCk_TestBridge_RequestProcessor(
        const FString& InProtocolRoot,
        FCk_TestBridge_RunController::ControllerResolverType InResolveController,
        PreconditionEvaluatorType InEvaluatePreconditions);

public:
    // Create the protocol dirs, (per policy) wipe stale requests, wait once for the asset registry, publish
    // server.json. Returns false if directories/ownership publication fail. Re-runnable when idle.
    auto
    Startup(
        ECk_TestBridge_StaleRequestPolicy InStaleRequestPolicy = ECk_TestBridge_StaleRequestPolicy::WipeStale) -> bool;

    // One poll pass. While a run is in flight: pump the RunController, refresh the heartbeat, and on completion
    // write the result + delete the request + un-busy. While idle: claim at most ONE settled request, validate it,
    // and either refuse-and-delete it or start a run (non-blocking). InDeltaTime pumps the in-flight run.
    auto
    ProcessPending(
        float InDeltaTime) -> FCk_TestBridge_ProcessResult;

    // Retry only the captured terminal response/outstanding request deletion. This never reparses
    // the request, re-evaluates the environment, or starts automation.
    auto Request_RecoverPublication() -> FCk_TestBridge_ProcessResult;

    // Remove server.json. Safe whether or not Startup ran. Does NOT abort an in-flight run (the caller owns that).
    auto
    Shutdown() -> void;

public:
    // True when at least one *.json sits in Requests/ — the bridge's re-claim gate after a quit handoff.
    auto
    Has_PendingRequests() const -> bool;

    auto Get_RequestsDir() const -> const FString&      { return _RequestsDir; }
    auto Get_ResultsDir() const -> const FString&       { return _ResultsDir; }
    auto Get_ProgressDir() const -> const FString&      { return _ProgressDir; }
    auto Get_ServerStatusPath() const -> const FString& { return _ServerStatusPath; }

    auto Get_IsBusy() const -> bool { return _Busy; }
    auto Get_HasTerminalFault() const -> bool { return NOT _TerminalFault.IsEmpty(); }
    auto Get_TerminalFault() const -> const FString& { return _TerminalFault; }

private:
    auto
    Do_WriteStatusFile(
        bool InBusy,
        const FString& InCurrentRequestId) -> bool;

    // Parse + validate one request file. Captures a terminal rejection/quit or starts the runner.
    auto
    Do_ClaimRequest(
        const FString& InRequestPath) -> void;

    // Called each poll while _Busy: pump the runner, and on completion finalize (write result, delete request).
    auto
    Do_PumpActiveRun(
        float InDeltaTime) -> void;

    auto
    Do_AppendProgress(
        const TSharedPtr<class FJsonObject>& InEvent) -> void;

    auto Do_CaptureResponse(
        const TSharedPtr<class FJsonObject>& InResponse,
        bool InQuitRequested = false) -> void;

    auto Do_PublishCapturedResponse() -> FCk_TestBridge_ProcessResult;

    auto Do_FaultPublication(const FString& InReason) -> void;

private:
    PreconditionEvaluatorType _EvaluatePreconditions;
    FString _Root;
    FString _RequestsDir;
    FString _ResultsDir;
    FString _ProgressDir;
    FString _ServerStatusPath;
    FString _StartedAtIso;

    bool    _Busy = false;
    // Private and never mutated after capture: this is the one terminal outcome recovery publishes.
    TSharedPtr<class FJsonObject> _CapturedResponse;
    bool _CapturedQuitRequested = false;
    FString _TerminalFault;
    FString _CurrentRequestId;
    FString _CurrentRequestPath;
    FString _CurrentResultPath;
    FString _CurrentProgressPath;

    FCk_TestBridge_Env          _CurrentEnv;
    FCk_TestBridge_RunController _Runner;
};

// --------------------------------------------------------------------------------------------------------------------
