// Language=angelscript

// Voice-chat gym PC: composes a loopback VoiceTalker on the player pawn and drives
// push-to-talk off the V key (poll + edge-detect - the gym framework carries no input
// bindings of its own). Each transmit edge flashes on screen: the machine-visible half of
// the roger-beep recipe (the module docs, "Consumer recipes") - a consumer binds a cue to
// exactly these edges. Requires [Voice] bEnabled=true in the host project's DefaultEngine.ini
// or the capture source never opens (the standing P2 prerequisite).
class UCk_VoiceChatGym_StartAttempt : UObject
{
    ACk_VoiceChatGym_PlayerController Owner;
    int32 Generation = 0;

    UFUNCTION()
    void OnCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        if (ck::IsValid(Owner))
        { Owner.OnStartCompletedForAttempt(this, Generation, InRequestOwner, InResult); }
    }
}

class ACk_VoiceChatGym_PlayerController : ACk_Gym_Base_PlayerController
{
    private FCk_Handle_VoiceTalker _GymTalker;
    private APawn _TalkerPawn;
    private APawn _PendingPawn;
    private bool _StartupRequested = false;
    private bool _StartupTimedOut = false;
    private float _StartupWaitSeconds = 0.0f;
    private bool _CaptureFailed = false;
    private bool _PttWasDown = false;
    private int32 _StartGeneration = 0;
    private UCk_VoiceChatGym_StartAttempt _CurrentStartAttempt;
    private const float StartupTimeoutSeconds = 10.0f;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();

        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(n"Gym.VoiceChat.MicLoopback");
        Station.Title = FText::FromString("VOICE CHAT — MIC LOOPBACK + TRANSMIT EDGES");
        auto Description = TArray<FText>();
        Description.Add(FText::FromString("HOLD V to talk. Your microphone loops back through the full capture -> VAD -> encode -> jitter -> decode -> synth pipeline: you hear yourself."));
        Description.Add(FText::FromString("Each transmit edge flashes on screen - bind a cue to those signals for a roger beep (recipe: Source/the CkVoiceChat docs, Consumer recipes)."));
        Description.Add(FText::FromString("Prerequisite: [Voice] bEnabled=true in DefaultEngine.ini and a working microphone. A capture failure is shown below; successful TX edges alone do not prove audible microphone playback."));
        Description.Add(FText::FromString("Spatialization / per-channel attenuation / HybridRadio need a second machine or PIE client - steps live in the campaign's Gate_4.md [EDITOR-VERIFY] block."));
        Station.Description = Description;
        Stations.Add(Station);

        return Stations;
    }

    void Request_StartGym() override
    {
        // Base gym startup and restart may run before possession, and on the
        // server copy of a remote controller. Capture belongs only to this
        // machine's local player.
        if (!IsLocalController())
        { return; }

        _StartupRequested = true;
        InvalidateStartAttempt();
        _StartupTimedOut = false;
        _StartupWaitSeconds = 0.0f;
        _CaptureFailed = false;
        if (HasOwnedTalker())
        {
            utils_voice_talker::Request_StopTransmit(_GymTalker);
            _PttWasDown = IsInputKeyDown(EKeys::V);
            return;
        }
        ReconcileTalker(0.0f);
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!IsLocalController())
        {
            ReleaseTalker();
            return;
        }

        ReconcileTalker(DeltaSeconds);
        if (!HasOwnedTalker())
        { return; }

        auto PttIsDown = IsInputKeyDown(EKeys::V);

        if (PttIsDown == _PttWasDown)
        { return; }

        _PttWasDown = PttIsDown;

        if (PttIsDown)
        { Request_StartLocalTransmit(); }
        else
        {
            InvalidateStartAttempt();
            utils_voice_talker::Request_StopTransmit(_GymTalker);
            _CaptureFailed = false;
        }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason EndPlayReason)
    {
        ReleaseTalker();
        _StartupRequested = false;
        Super::EndPlay(EndPlayReason);
    }

    private bool HasOwnedTalker()
    {
        if (!IsLocalController() || ck::Is_NOT_Valid(_TalkerPawn) ||
            ck::Is_NOT_Valid(_GymTalker) || GetControlledPawn() != _TalkerPawn ||
            _TalkerPawn.GetController() != this)
        { return false; }

        auto PawnEntity = ck::ToEntity(_TalkerPawn);
        return ck::IsValid(PawnEntity) && FCk_Handle(_GymTalker) == PawnEntity;
    }

    private void ReconcileTalker(float DeltaSeconds)
    {
        auto Pawn = GetControlledPawn();
        if (ck::IsValid(_GymTalker) && !HasOwnedTalker())
        { ReleaseTalker(); }
        if (!_StartupRequested)
        { return; }

        if (Pawn != _PendingPawn)
        {
            _PendingPawn = Pawn;
            _StartupWaitSeconds = 0.0f;
            _StartupTimedOut = false;
        }
        if (HasOwnedTalker() || _StartupTimedOut)
        { return; }

        // Possession and the pawn's WithActor bridge settle independently.
        auto Possessed = ck::IsValid(Pawn) && Pawn.GetController() == this;
        auto PawnEntity = Possessed ? ck::ToEntity(Pawn) : FCk_Handle();
        if (!Possessed || ck::Is_NOT_Valid(PawnEntity))
        {
            _StartupWaitSeconds += DeltaSeconds;
            if (_StartupWaitSeconds >= StartupTimeoutSeconds)
            {
                _StartupTimedOut = true;
                ck::Warning("VoiceChat Gym - local pawn possession/bridge did not become ready within 10 seconds; restart the gym to retry");
            }
            return;
        }

        // Existing feature can be left by an earlier local controller instance;
        // reuse it rather than composing duplicate fragments on this pawn.
        auto MutablePawnEntity = PawnEntity;
        if (utils_voice_talker::Has(PawnEntity))
        { _GymTalker = PawnEntity.As_VoiceTalker(); }
        else
        {
            auto Params = FCk_VoiceTalker_Spec();
            Params.Set_Loopback(ECk_EnableDisable::Enable);
            _GymTalker = utils_voice_talker::Add(MutablePawnEntity, Params);
        }
        if (ck::Is_NOT_Valid(_GymTalker))
        {
            _StartupTimedOut = true;
            ck::Warning("VoiceChat Gym - local pawn talker composition failed; restart the gym to retry");
            return;
        }

        _TalkerPawn = Pawn;
        _PttWasDown = IsInputKeyDown(EKeys::V);
        // A former local owner may have queued a start. Stop it before this
        // controller may submit a new start on the same pawn entity.
        utils_voice_talker::Request_StopTransmit(_GymTalker);
        utils_voice_talker::BindTo_OnTransmitStarted(_GymTalker,
            ECk_Signal_BindingPolicy::IgnorePayloadInFlight,
            ECk_Signal_PostFireBehavior::DoNothing,
            FCk_Delegate_VoiceTalker(this, n"OnTransmitStarted"));
        utils_voice_talker::BindTo_OnTransmitStopped(_GymTalker,
            ECk_Signal_BindingPolicy::IgnorePayloadInFlight,
            ECk_Signal_PostFireBehavior::DoNothing,
            FCk_Delegate_VoiceTalker(this, n"OnTransmitStopped"));
        ck::Trace("VoiceChat Gym - local loopback talker ready on possessed pawn; HOLD V to talk");
    }

    private void ReleaseTalker()
    {
        InvalidateStartAttempt();
        if (ck::IsValid(_GymTalker))
        {
            // An old controller must not stop a new owner's transmission. The
            // new owner queues its own Stop before accepting a new Start.
            if (ck::IsValid(_TalkerPawn) &&
                (!ck::IsValid(_TalkerPawn.GetController()) || _TalkerPawn.GetController() == this))
            { utils_voice_talker::Request_StopTransmit(_GymTalker); }
            utils_voice_talker::UnbindFrom_OnTransmitStarted(_GymTalker,
                FCk_Delegate_VoiceTalker(this, n"OnTransmitStarted"));
            utils_voice_talker::UnbindFrom_OnTransmitStopped(_GymTalker,
                FCk_Delegate_VoiceTalker(this, n"OnTransmitStopped"));
        }
        _GymTalker = FCk_Handle_VoiceTalker();
        _TalkerPawn = nullptr;
        _PttWasDown = false;
        _CaptureFailed = false;
    }

    private void Request_StartLocalTransmit()
    {
        if (!HasOwnedTalker() || utils_voice_talker::Get_IsTransmitting(_GymTalker))
        { return; }
        InvalidateStartAttempt();
        _CurrentStartAttempt = Cast<UCk_VoiceChatGym_StartAttempt>(
            NewObject(this, UCk_VoiceChatGym_StartAttempt));
        if (!ck::IsValid(_CurrentStartAttempt))
        { _CaptureFailed = true; return; }
        _CurrentStartAttempt.Owner = this;
        _CurrentStartAttempt.Generation = _StartGeneration;
        utils_voice_talker::Request_StartTransmit(_GymTalker,
            FCk_Request_VoiceTalker_StartTransmit(),
            FCk_Delegate_Request_OnCompleted(_CurrentStartAttempt, n"OnCompleted"));
    }

    private void InvalidateStartAttempt()
    {
        _StartGeneration += 1;
        _CurrentStartAttempt = nullptr;
    }

    void OnStartCompletedForAttempt(UCk_VoiceChatGym_StartAttempt InAttempt, int32 InGeneration,
        FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        if (InAttempt != _CurrentStartAttempt || InGeneration != _StartGeneration ||
            !HasOwnedTalker() || InRequestOwner != FCk_Handle(_GymTalker))
        { return; }
        _CurrentStartAttempt = nullptr;
        if (InResult == ECk_Request_OperationResult::Succeeded)
        { _CaptureFailed = false; return; }

        _CaptureFailed = true;
        ck::Warning(f"VoiceChat Gym - microphone capture did not start ({InResult}); check [Voice] bEnabled and capture device");
    }

    UFUNCTION()
    private void OnTransmitStarted(FCk_Handle_VoiceTalker InTalker)
    {
        if (!HasOwnedTalker() || InTalker != _GymTalker)
        { return; }
        PrintToScreen("TX START - a roger-beep cue binds HERE", 1.5, FLinearColor::Green);
    }

    UFUNCTION()
    private void OnTransmitStopped(FCk_Handle_VoiceTalker InTalker)
    {
        if (!HasOwnedTalker() || InTalker != _GymTalker)
        { return; }
        PrintToScreen("TX END - squelch-close beep binds HERE", 1.5, FLinearColor::Yellow);
    }

    //--------------------------------------------------------------------------------------------------------------------------
    // CONTROL PANEL (Script/Common/CkGym_ControlPanel.as)
    //
    // A row is a press, not a hold, so this cannot BE the push-to-talk - it is a LATCH: one press opens
    // the transmit and the next closes it. V stays the real hold-to-talk and is left untouched, which is
    // also why the row reads the live transmit tag rather than a mirror: releasing V closes a latch this
    // panel opened, and a mirrored bool would keep claiming ON after that.
    //--------------------------------------------------------------------------------------------------------------------------

    FString Get_ControlPanelTitle() override
    {
        return "VOICE CHAT";
    }

    UFUNCTION()
    bool Get_IsCaptureFailed()
    { return _CaptureFailed; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();

        auto Ready = HasOwnedTalker();
        auto IsTransmitting = Ready && utils_voice_talker::Get_IsTransmitting(_GymTalker);

        Rows.Add(CkGym_Control::Toggle(EKeys::T, "T", "TX latch", IsTransmitting, false, Ready));
        Rows.Add(CkGym_Control::Status("Push-to-talk", "HOLD V - the latch above is the press-only stand-in"));
        if (Get_IsCaptureFailed())
        { Rows.Add(CkGym_Control::Status("Capture", "Unavailable - check [Voice] bEnabled and microphone")); }
        else if (!Ready)
        { Rows.Add(CkGym_Control::Status("Capture", "Waiting for local pawn possession and entity bridge")); }
        else
        { Rows.Add(CkGym_Control::Status("Capture", "Ready to request; actual microphone opens on first TX")); }

        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (InRowIndex != 0)
        { return; }

        Request_LatchTransmit();
    }

    // Shared by the control panel and authored PIE verification. It uses the
    // same ownership, request, and completion path as the player's T row.
    UFUNCTION()
    void Request_LatchTransmit()
    {
        if (!HasOwnedTalker())
        { return; }

        if (utils_voice_talker::Get_IsTransmitting(_GymTalker))
        {
            InvalidateStartAttempt();
            utils_voice_talker::Request_StopTransmit(_GymTalker);
        }
        else
        {
            _CaptureFailed = false;
            Request_StartLocalTransmit();
        }
    }
}
