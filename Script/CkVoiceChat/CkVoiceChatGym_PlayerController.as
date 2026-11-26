// Language=angelscript

// Voice-chat gym PC: composes a loopback VoiceTalker on the player pawn and drives
// push-to-talk off the V key (poll + edge-detect - the gym framework carries no input
// bindings of its own). Each transmit edge flashes on screen: the machine-visible half of
// the roger-beep recipe (the module docs, "Consumer recipes") - a consumer binds a cue to
// exactly these edges. Requires [Voice] bEnabled=true in the host project's DefaultEngine.ini
// or the capture source never opens (the standing P2 prerequisite).
class ACk_VoiceChatGym_PlayerController : ACk_Gym_Base_PlayerController
{
    private FCk_Handle_VoiceTalker _GymTalker;
    private bool _PttWasDown = false;

    // The level readout on the control panel. The module measures RMS on every captured frame whether or not its
    // voice gate is open, so the peak shows what the gate was asked to pass during this transmit.
    private float32 _VadThreshold = 0.0f;
    private float32 _PeakAmplitude = 0.0f;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();

        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(n"Gym.VoiceChat.MicLoopback");
        Station.Title = FText::FromString("VOICE CHAT — MIC LOOPBACK + TRANSMIT EDGES");
        auto Description = TArray<FText>();
        Description.Add(FText::FromString("HOLD V to talk. Your microphone loops back through the full capture -> VAD -> encode -> jitter -> decode -> synth pipeline: you hear yourself."));
        Description.Add(FText::FromString("Each transmit edge flashes on screen - bind a cue to those signals for a roger beep (recipe: Source/the CkVoiceChat docs, Consumer recipes)."));
        Description.Add(FText::FromString("Prerequisite: [Voice] bEnabled=true in DefaultEngine.ini. Silence with the flash working means the capture source never opened - check that line first."));
        Description.Add(FText::FromString("Spatialization / per-channel attenuation / HybridRadio need a second machine or PIE client; this station covers the loopback only."));
        Station.Description = Description;
        Stations.Add(Station);

        return Stations;
    }

    void Request_StartGym() override
    {
        auto GymPawn = GetControlledPawn();

        if (!System::IsValid(GymPawn))
        {
            ck::Trace("VoiceChat Gym - no controlled pawn; talker not composed");
            return;
        }

        auto PawnEntity = ck::ToEntity(GymPawn);

        if (ck::Is_NOT_Valid(PawnEntity))
        {
            ck::Trace("VoiceChat Gym - pawn has no entity yet; talker not composed");
            return;
        }

        auto Params = FCk_VoiceTalker_Spec();
        Params.Set_Loopback(ECk_EnableDisable::Enable);
        _VadThreshold = Params.Get_VadThreshold();
        _GymTalker = utils_voice_talker::Add(PawnEntity, Params);

        utils_voice_talker::BindTo_OnTransmitStarted(_GymTalker,
            ECk_Signal_BindingPolicy::IgnorePayloadInFlight,
            ECk_Signal_PostFireBehavior::DoNothing,
            FCk_Delegate_VoiceTalker(this, n"OnTransmitStarted"));
        utils_voice_talker::BindTo_OnTransmitStopped(_GymTalker,
            ECk_Signal_BindingPolicy::IgnorePayloadInFlight,
            ECk_Signal_PostFireBehavior::DoNothing,
            FCk_Delegate_VoiceTalker(this, n"OnTransmitStopped"));

        ck::Trace("VoiceChat Gym - loopback talker composed on the pawn; HOLD V to talk");
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (ck::Is_NOT_Valid(_GymTalker))
        { return; }

        if (utils_voice_talker::Get_IsTransmitting(_GymTalker))
        { _PeakAmplitude = Math::Max(_PeakAmplitude, utils_voice_talker::Get_CurrentAmplitude(_GymTalker)); }

        auto PttIsDown = IsInputKeyDown(EKeys::V);

        if (PttIsDown == _PttWasDown)
        { return; }

        _PttWasDown = PttIsDown;

        if (PttIsDown)
        { utils_voice_talker::Request_StartTransmit(_GymTalker, FCk_Request_VoiceTalker_StartTransmit()); }
        else
        { utils_voice_talker::Request_StopTransmit(_GymTalker); }
    }

    UFUNCTION()
    private void OnTransmitStarted(FCk_Handle_VoiceTalker InTalker)
    {
        _PeakAmplitude = 0.0f;
        PrintToScreen("TX START - a roger-beep cue binds HERE", 1.5, FLinearColor::Green);
    }

    UFUNCTION()
    private void OnTransmitStopped(FCk_Handle_VoiceTalker InTalker)
    {
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

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();

        const bool HasTalker = ck::IsValid(_GymTalker);
        const bool IsTransmitting = HasTalker && utils_voice_talker::Get_IsTransmitting(_GymTalker);
        const bool IsGateOpen = HasTalker && utils_voice_talker::Get_IsSpeaking(_GymTalker);
        const float32 MicLevel = IsTransmitting ? utils_voice_talker::Get_CurrentAmplitude(_GymTalker) : 0.0f;
        const float32 Gain = HasTalker ? utils_voice_talker::Get_InputGain(_GymTalker) : 0.0f;

        // Rows 0 and 1 are the keyed ones; Request_ControlActivated acts on those two indices.
        Rows.Add(CkGym_Control::Toggle(EKeys::T, "T", "TX latch", IsTransmitting, false, HasTalker));
        Rows.Add(CkGym_Control::Cycle(EKeys::G, "G", "Input gain (lifts the level the voice gate sees)", f"x{Gain :.1}", false, HasTalker));
        Rows.Add(CkGym_Control::Status("Push-to-talk", "HOLD V - the latch above is the press-only stand-in"));
        Rows.Add(CkGym_Control::Status("Mic level (RMS, after gain)",
            f"{MicLevel :.3} now, {_PeakAmplitude :.3} peak this transmit; the gate opens at {_VadThreshold :.3}",
            IsTransmitting && _PeakAmplitude < _VadThreshold));
        Rows.Add(CkGym_Control::Status("Voice gate",
            IsGateOpen ? "OPEN - frames are being encoded" : "closed - nothing is encoded",
            IsTransmitting && IsGateOpen == false));
        Rows.Add(CkGym_Control::Status("Verdict", Get_Verdict(IsTransmitting, IsGateOpen), IsTransmitting && IsGateOpen == false));

        return Rows;
    }

    // Names the stage that is losing the audio, from what the talker itself reports.
    private FString Get_Verdict(bool InIsTransmitting, bool InIsGateOpen) const
    {
        if (ck::Is_NOT_Valid(_GymTalker))
        { return "No talker was composed - read the log for 'VoiceChat Gym'"; }

        if (InIsTransmitting == false)
        { return "Idle - hold V and speak"; }

        if (InIsGateOpen)
        { return "Gate open: you should hear yourself. Silence now is a fault after the gate (encode, decode or playback)"; }

        if (_PeakAmplitude <= 0.0f)
        { return "No audio is arriving from the microphone"; }

        if (_PeakAmplitude < _VadThreshold)
        { return "Gate closed: your level peaks below the threshold. Press G to raise the gain"; }

        return "Gate closed although the level reached the threshold";
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (ck::Is_NOT_Valid(_GymTalker))
        { return; }

        if (InRowIndex == 0)
        {
            if (utils_voice_talker::Get_IsTransmitting(_GymTalker))
            { utils_voice_talker::Request_StopTransmit(_GymTalker); }
            else
            { utils_voice_talker::Request_StartTransmit(_GymTalker, FCk_Request_VoiceTalker_StartTransmit()); }

            return;
        }

        if (InRowIndex == 1)
        {
            // Doubles from the module default up to x9.6, then wraps. Read back from the talker, not mirrored here.
            const float32 Gain = utils_voice_talker::Get_InputGain(_GymTalker);
            utils_voice_talker::Request_SetInputGain(_GymTalker, Gain >= 9.0f ? 1.2f : Gain * 2.0f);
        }
    }
}
