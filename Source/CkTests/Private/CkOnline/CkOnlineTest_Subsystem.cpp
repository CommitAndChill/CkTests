#include "CkOnline/CkOnlineTest_Subsystem.h"

#include "CkOnline/CkOnline_Host.h"
#include "CkOnline/CkOnline_Utils.h"
#include "CkOnline/Subsystem/CkOnline_Subsystem.h"

#include <Engine/GameInstance.h>
#include <Engine/World.h>

class FCkOnlineTest_FakeAdapter final : public ck::IOnlineServiceAdapter
{
public:
    auto Execute(FGuid InOperationId, FCompletion InCompletion) -> void override
    {
        ++Executions;
        auto Callback = MakeShared<FCompletion>(MoveTemp(InCompletion));
        Callbacks.Add(InOperationId, Callback);
        CallbackOrder.Add(InOperationId);
        if (CallbackOrder.Num() > 128)
        {
            Callbacks.Remove(CallbackOrder[0]);
            CallbackOrder.RemoveAt(0);
        }
        if (InOperationId == ImmediateOperation)
        {
            (*Callback)(ECk_OnlineService_Result::Succeeded);
        }
    }

    auto Cancel(FGuid InOperationId) -> void override
    {
        ++Cancels;
    }

    auto Shutdown() -> void override
    {
        // Keep callbacks: the gym deliberately fires one after stop/restart to test stale delivery.
        ++Shutdowns;
    }

    auto Release(FGuid InOperationId) -> bool
    {
        const auto* Callback = Callbacks.Find(InOperationId);
        if (Callback == nullptr || !Callback->IsValid())
        { return false; }
        (**Callback)(ECk_OnlineService_Result::Succeeded);
        return true;
    }

    FGuid ImmediateOperation;
    int32 Executions = 0;
    int32 Cancels = 0;
    int32 Shutdowns = 0;

private:
    TMap<FGuid, TSharedPtr<FCompletion>> Callbacks;
    TArray<FGuid> CallbackOrder;
};

auto UCkOnlineTest_Subsystem::Deinitialize() -> void
{
    Request_Stop();
    _Adapter.Reset();
    Super::Deinitialize();
}

bool UCkOnlineTest_Subsystem::Request_Start()
{
    auto* GameInstance = GetGameInstance();
    if (GameInstance == nullptr)
    { return false; }

    auto* ServiceSubsystem = GameInstance->GetSubsystem<UCk_Online_Subsystem_UE>();
    if (ServiceSubsystem == nullptr)
    { return false; }

    if (!_Adapter.IsValid())
    { _Adapter = MakeShared<FCkOnlineTest_FakeAdapter>(); }

    TSharedRef<ck::IOnlineServiceAdapter> Adapter = _Adapter.ToSharedRef();
    if (!ServiceSubsystem->TryStart(Adapter))
    { return false; }

    auto Service = ServiceSubsystem->Get_Service();
    FCk_Delegate_OnlineService_Completed Delegate;
    Delegate.BindDynamic(this, &UCkOnlineTest_Subsystem::OnServiceCompleted);
    UCk_Utils_OnlineService_UE::BindTo_OnCompleted(Service, Delegate,
        ECk_Signal_BindingPolicy::FireIfPayloadInFlightThisFrame,
        ECk_Signal_PostFireBehavior::DoNothing);
    return true;
}

void UCkOnlineTest_Subsystem::Request_Stop()
{
    _StopOnNextCompletion = false;
    _StopOnNextGenericCompletion = false;
    auto* GameInstance = GetGameInstance();
    if (GameInstance == nullptr)
    { return; }
    auto* ServiceSubsystem = GameInstance->GetSubsystem<UCk_Online_Subsystem_UE>();
    if (ServiceSubsystem != nullptr)
    { ServiceSubsystem->Request_Stop(); }
}

bool UCkOnlineTest_Subsystem::Request_ResetRecord()
{
    if (Get_Diagnostics().Get_State() != ECk_OnlineService_State::Stopped)
    { return false; }

    _Adapter.Reset();
    _HeldOperation.Invalidate();
    _TimedOperation.Invalidate();
    _PinnedGeneration.Invalidate();
    _VisitsAtPin = 0;
    _ObservedCompletions = 0;
    _ObservedSuccesses = 0;
    _ObservedCancellations = 0;
    _ObservedTimeouts = 0;
    _TravelVisits = 0;
    _StopsFromCompletion = 0;
    _StopsFromGenericCompletion = 0;
    _LateReleaseAttempts = 0;
    _ObservedAtLateRelease = 0;
    _LateReleasesAtDomainStop = 0;
    _LateReleasesAtGenericStop = 0;
    _StopOnNextCompletion = false;
    _StopOnNextGenericCompletion = false;
    return true;
}

auto UCkOnlineTest_Subsystem::DoSubmit(bool InImmediate, FCk_Time InTimeout,
    FGuid& OutOperationId, bool InStopFromGeneric) -> bool
{
    auto Service = Get_Service();
    if (!ck::IsValid(Service) || !_Adapter.IsValid())
    { return false; }

    OutOperationId = FGuid::NewGuid();
    if (InImmediate)
    { _Adapter->ImmediateOperation = OutOperationId; }

    auto Request = FCk_Request_OnlineService_Execute(OutOperationId);
    Request.Set_Timeout(InTimeout);
    auto Delegate = FCk_Delegate_Request_OnCompleted{};
    if (InStopFromGeneric)
    { Delegate.BindDynamic(this, &UCkOnlineTest_Subsystem::OnRequestCompleted); }
    UCk_Utils_OnlineService_UE::Request_Execute(Service, Request, Delegate);
    return true;
}

bool UCkOnlineTest_Subsystem::Request_SubmitImmediate()
{
    FGuid OperationId;
    return DoSubmit(true, FCk_Time{5.0}, OperationId);
}

bool UCkOnlineTest_Subsystem::Request_SubmitImmediateStopFromGeneric()
{
    _StopOnNextGenericCompletion = true;
    FGuid OperationId;
    if (DoSubmit(true, FCk_Time{5.0}, OperationId, true))
    { return true; }
    _StopOnNextGenericCompletion = false;
    return false;
}

bool UCkOnlineTest_Subsystem::Request_MarkOperationImmediate(FGuid InOperationId)
{
    if (!_Adapter.IsValid() || !InOperationId.IsValid())
    { return false; }
    _Adapter->ImmediateOperation = InOperationId;
    return true;
}

bool UCkOnlineTest_Subsystem::Request_SubmitHeld()
{
    return DoSubmit(false, FCk_Time{30.0}, _HeldOperation);
}

bool UCkOnlineTest_Subsystem::Request_SubmitShortTimeout()
{
    return DoSubmit(false, FCk_Time{0.2}, _TimedOperation);
}

bool UCkOnlineTest_Subsystem::Request_ReleaseHeld()
{
    if (!_Adapter.IsValid() || !_HeldOperation.IsValid())
    { return false; }
    const bool Released = _Adapter->Release(_HeldOperation);
    if (Released && Get_Diagnostics().Get_State() == ECk_OnlineService_State::Stopped)
    {
        ++_LateReleaseAttempts;
        _ObservedAtLateRelease = _ObservedCompletions;
        _LateReleaseFrame = GFrameCounter;
    }
    return Released;
}

bool UCkOnlineTest_Subsystem::Request_RepeatHeldCallback()
{
    return Request_ReleaseHeld();
}

bool UCkOnlineTest_Subsystem::Request_ReleaseTimedAfterTimeout()
{
    return _Adapter.IsValid() && _TimedOperation.IsValid() && _Adapter->Release(_TimedOperation);
}

bool UCkOnlineTest_Subsystem::Request_CancelHeld()
{
    auto Service = Get_Service();
    if (!ck::IsValid(Service) || !_HeldOperation.IsValid())
    { return false; }
    UCk_Utils_OnlineService_UE::Request_Cancel(Service,
        FCk_Request_OnlineService_Cancel(_HeldOperation), FCk_Delegate_Request_OnCompleted());
    return true;
}

void UCkOnlineTest_Subsystem::Request_SetStopOnNextCompletion(bool InEnabled)
{
    _StopOnNextCompletion = InEnabled;
}

FCk_Handle_OnlineService UCkOnlineTest_Subsystem::Get_Service() const
{
    auto* GameInstance = GetGameInstance();
    if (GameInstance == nullptr)
    { return {}; }
    auto* ServiceSubsystem = GameInstance->GetSubsystem<UCk_Online_Subsystem_UE>();
    return ServiceSubsystem != nullptr ? ServiceSubsystem->Get_Service() : FCk_Handle_OnlineService{};
}

FCk_OnlineService_Diagnostics UCkOnlineTest_Subsystem::Get_Diagnostics() const
{
    auto* GameInstance = GetGameInstance();
    if (GameInstance == nullptr)
    { return {}; }
    auto* ServiceSubsystem = GameInstance->GetSubsystem<UCk_Online_Subsystem_UE>();
    return ServiceSubsystem != nullptr ? ServiceSubsystem->Get_Diagnostics() : FCk_OnlineService_Diagnostics{};
}

FString UCkOnlineTest_Subsystem::Get_GenerationText() const
{
    return Get_Diagnostics().Get_Generation().ToString();
}

FString UCkOnlineTest_Subsystem::Get_StateText() const
{
    switch (Get_Diagnostics().Get_State())
    {
    case ECk_OnlineService_State::Running: return TEXT("Running");
    case ECk_OnlineService_State::Stopping: return TEXT("Stopping");
    default: return TEXT("Stopped");
    }
}

bool UCkOnlineTest_Subsystem::Request_PinGenerationForTravel()
{
    const auto Diagnostics = Get_Diagnostics();
    if (Diagnostics.Get_State() != ECk_OnlineService_State::Running ||
        !Diagnostics.Get_Generation().IsValid())
    { return false; }
    _PinnedGeneration = Diagnostics.Get_Generation();
    _VisitsAtPin = _TravelVisits;
    _TicksAtPin = Diagnostics.Get_TickCount();
    return true;
}

void UCkOnlineTest_Subsystem::Request_RecordGymVisit()
{
    const auto World = TWeakObjectPtr<UWorld>{GetWorld()};
    if (World.IsValid() && World != _LastVisitedWorld)
    {
        _LastVisitedWorld = World;
        ++_TravelVisits;
    }
}

bool UCkOnlineTest_Subsystem::Get_HasTickedSincePin() const
{
    return Get_IsPinnedGenerationCurrent() && Get_Diagnostics().Get_TickCount() > _TicksAtPin;
}

bool UCkOnlineTest_Subsystem::Get_IsPinnedGenerationCurrent() const
{
    return _PinnedGeneration.IsValid() && Get_Diagnostics().Get_Generation() == _PinnedGeneration;
}

int32 UCkOnlineTest_Subsystem::Get_AdapterExecutions() const
{
    return _Adapter.IsValid() ? _Adapter->Executions : 0;
}

int32 UCkOnlineTest_Subsystem::Get_AdapterCancels() const
{
    return _Adapter.IsValid() ? _Adapter->Cancels : 0;
}

int32 UCkOnlineTest_Subsystem::Get_AdapterShutdowns() const
{
    return _Adapter.IsValid() ? _Adapter->Shutdowns : 0;
}

void UCkOnlineTest_Subsystem::OnServiceCompleted(FCk_Handle_OnlineService InService,
    FCk_OnlineService_Completion InCompletion)
{
    ++_ObservedCompletions;
    switch (InCompletion.Get_Result())
    {
    case ECk_OnlineService_Result::Succeeded: ++_ObservedSuccesses; break;
    case ECk_OnlineService_Result::Cancelled: ++_ObservedCancellations; break;
    case ECk_OnlineService_Result::TimedOut: ++_ObservedTimeouts; break;
    default: break;
    }

    if (_StopOnNextCompletion)
    {
        _StopOnNextCompletion = false;
        ++_StopsFromCompletion;
        _LateReleasesAtDomainStop = _LateReleaseAttempts;
        _DomainStopGeneration = Get_Diagnostics().Get_Generation();
        Request_Stop();
    }
}

void UCkOnlineTest_Subsystem::OnRequestCompleted(FCk_Handle InService,
    ECk_Request_OperationResult InResult)
{
    if (!_StopOnNextGenericCompletion || InResult != ECk_Request_OperationResult::Succeeded)
    { return; }
    _StopOnNextGenericCompletion = false;
    ++_StopsFromGenericCompletion;
    _LateReleasesAtGenericStop = _LateReleaseAttempts;
    _GenericStopGeneration = Get_Diagnostics().Get_Generation();
    Request_Stop();
}
