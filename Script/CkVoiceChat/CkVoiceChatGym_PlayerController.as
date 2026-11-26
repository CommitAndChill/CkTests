// Language=angelscript

// Voice-chat gym PC: composes a loopback VoiceTalker on the player pawn and drives
// push-to-talk off the V key (poll + edge-detect - the gym framework carries no input
// bindings of its own). Each transmit edge flashes on screen: the machine-visible half of
// the roger-beep recipe (the module docs, "Consumer recipes") - a consumer binds a cue to
// exactly these edges. Requires [Voice] bEnabled=true in the host project's DefaultEngine.ini
// or the capture source never opens (the standing P2 prerequisite).
//
// It is also where the voice gate is tuned by ear: M switches PushToTalk / OpenMic, Z / X move the
// gate threshold, P runs the gate in push-to-talk too, and the panel shows the live threshold.
//
// OpenMic opens ITSELF here: the talker is composed on the possessed pawn's own entity (GetControlledPawn ->
// ck::ToEntity, the pawn's Entity Bridge), which is exactly what the module's open mic waits for (a possessed,
// player-controlled pawn bridged to the talker's entity), so the gym never issues the Start for it. V is ignored in
// OpenMic: its release would issue a StopTransmit, and an explicit Stop stands until the talker is re-armed.
class ACk_VoiceChatGym_PlayerController : ACk_Gym_Base_PlayerController
{
    private FCk_Handle_VoiceTalker _GymTalker;
    private bool _PttWasDown = false;

    // The level readout on the control panel. The module measures RMS on every captured frame whether or not its
    // voice gate is open, so the peak shows what the gate was asked to pass during this transmit.
    private float32 _PeakAmplitude = 0.0f;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();

        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(n"Gym.VoiceChat.MicLoopback");
        Station.Title = FText::FromString("VOICE CHAT — MIC LOOPBACK + TRANSMIT EDGES");
        auto Description = TArray<FText>();
        Description.Add(FText::FromString("HOLD V to talk (push-to-talk). Your microphone loops back through the full capture -> VAD -> encode -> jitter -> decode -> synth pipeline: you hear yourself."));
        Description.Add(FText::FromString("Tune the voice gate by ear: M switches push-to-talk / open mic (open mic opens by itself, V does nothing), Z lowers and X raises the gate threshold, P runs the gate while V is held too. Read the threshold off the panel."));
        Description.Add(FText::FromString("T latches the transmit open/closed, G raises the input gain. Each transmit edge flashes on screen - bind a cue to those signals for a roger beep (recipe: Source/the CkVoiceChat docs, Consumer recipes)."));
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

        // The edge is tracked in every mode so a mode switch never leaves a stale one, but only push-to-talk acts on it.
        if (utils_voice_talker::Get_TransmitMode(_GymTalker) != ECk_VoiceChat_TransmitMode::PushToTalk)
        { return; }

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
    //
    // Every value below is read back from the talker each frame (mode, gain, threshold, switch); none is mirrored.
    //--------------------------------------------------------------------------------------------------------------------------

    FString Get_ControlPanelTitle() override
    {
        return "VOICE CHAT";
    }

    // The keyed rows come first, in this order; Request_ControlActivated acts on these indices.
    private const int32 Row_TxLatch = 0;
    private const int32 Row_Gain = 1;
    private const int32 Row_Mode = 2;
    private const int32 Row_ThresholdDown = 3;
    private const int32 Row_ThresholdUp = 4;
    private const int32 Row_GateInPushToTalk = 5;

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();

        const bool HasTalker = ck::IsValid(_GymTalker);
        const bool IsTransmitting = HasTalker && utils_voice_talker::Get_IsTransmitting(_GymTalker);
        const bool IsGateOpen = HasTalker && utils_voice_talker::Get_IsSpeaking(_GymTalker);
        const float32 MicLevel = IsTransmitting ? utils_voice_talker::Get_CurrentAmplitude(_GymTalker) : 0.0f;
        const float32 Gain = HasTalker ? utils_voice_talker::Get_InputGain(_GymTalker) : 0.0f;
        const float32 Threshold = HasTalker ? utils_voice_talker::Get_VadThreshold(_GymTalker) : 0.0f;
        const bool IsOpenMic = HasTalker && utils_voice_talker::Get_TransmitMode(_GymTalker) == ECk_VoiceChat_TransmitMode::OpenMic;
        const bool GateInPtt = HasTalker && utils_voice_talker::Get_GateInPushToTalk(_GymTalker) == ECk_EnableDisable::Enable;
        const bool GateApplies = Get_DoesGateApply();
        const bool LevelNeverReachedGate = IsTransmitting && GateApplies && _PeakAmplitude < Threshold;

        Rows.Add(CkGym_Control::Toggle(EKeys::T, "T", "TX latch", IsTransmitting, false, HasTalker));
        Rows.Add(CkGym_Control::Cycle(EKeys::G, "G", "Input gain (lifts the level the voice gate sees)", f"x{Gain :.1}", false, HasTalker));
        Rows.Add(CkGym_Control::Cycle(EKeys::M, "M", "Transmit mode",
            IsOpenMic ? "OpenMic - opens by itself, V does nothing" : "PushToTalk - hold V to talk", false, HasTalker));
        Rows.Add(CkGym_Control::Action(EKeys::Z, "Z", "Gate threshold down", HasTalker));
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", "Gate threshold up", HasTalker));
        Rows.Add(CkGym_Control::ToggleNamed(EKeys::P, "P", "Gate while push-to-talk", GateInPtt,
            "ON - the gate applies while V is held", "OFF - holding V bypasses the gate", false, HasTalker));

        Rows.Add(CkGym_Control::Status("Push-to-talk", IsOpenMic
            ? "V does nothing in OpenMic - the open mic opened the transmit itself (T closes / reopens it)"
            : "HOLD V - the latch above is the press-only stand-in"));
        Rows.Add(CkGym_Control::Status("Gate threshold (RMS)", f"{Threshold :.3}"));
        Rows.Add(CkGym_Control::Status("Gate applies now", GateApplies
            ? (IsOpenMic ? "YES - OpenMic always gates" : "YES - push-to-talk with the gate ON (P)")
            : "NO - push-to-talk bypasses it: every frame is encoded while V is held"));
        Rows.Add(CkGym_Control::Status("Mic level (RMS, after gain)",
            f"{MicLevel :.3} now, {_PeakAmplitude :.3} peak this transmit; " +
                (GateApplies ? f"the gate opens at {Threshold :.3}" : f"the gate is bypassed ({Threshold :.3} is not applied)"),
            LevelNeverReachedGate));
        Rows.Add(CkGym_Control::Status("Voice gate", Get_GateStateText(IsTransmitting, IsGateOpen, GateApplies), LevelNeverReachedGate));
        Rows.Add(CkGym_Control::Status("Verdict", Get_Verdict(IsTransmitting, IsGateOpen, MicLevel),
            IsTransmitting && (_PeakAmplitude <= 0.0f || LevelNeverReachedGate)));

        return Rows;
    }

    // Whether the voice gate decides what is encoded right now: always in OpenMic, in PushToTalk only with the switch on.
    private bool Get_DoesGateApply() const
    {
        if (ck::Is_NOT_Valid(_GymTalker))
        { return false; }

        const auto Mode = utils_voice_talker::Get_TransmitMode(_GymTalker);

        if (Mode == ECk_VoiceChat_TransmitMode::OpenMic)
        { return true; }

        return Mode == ECk_VoiceChat_TransmitMode::PushToTalk &&
            utils_voice_talker::Get_GateInPushToTalk(_GymTalker) == ECk_EnableDisable::Enable;
    }

    private FString Get_GateStateText(bool InIsTransmitting, bool InIsGateOpen, bool InGateApplies) const
    {
        if (InIsTransmitting == false)
        { return "not transmitting - nothing is encoded"; }

        if (InGateApplies == false)
        { return "BYPASSED - every frame is encoded"; }

        return InIsGateOpen ? "OPEN - frames are being encoded" : "closed - nothing is encoded";
    }

    // Names the stage that is losing the audio, from what the talker itself reports.
    private FString Get_Verdict(bool InIsTransmitting, bool InIsGateOpen, float32 InMicLevel) const
    {
        if (ck::Is_NOT_Valid(_GymTalker))
        { return "No talker was composed - read the log for 'VoiceChat Gym'"; }

        const bool IsOpenMic = utils_voice_talker::Get_TransmitMode(_GymTalker) == ECk_VoiceChat_TransmitMode::OpenMic;

        if (InIsTransmitting == false)
        {
            if (IsOpenMic)
            { return "Idle - the open mic is closed (T stopped it, or the microphone did not open: read the log). Press T to open it, or M twice to re-arm the open mic"; }

            return "Idle - hold V and speak";
        }

        if (_PeakAmplitude <= 0.0f)
        { return "No audio is arriving from the microphone"; }

        const float32 Threshold = utils_voice_talker::Get_VadThreshold(_GymTalker);

        if (Get_DoesGateApply() == false)
        { return "Gate bypassed (push-to-talk): every frame is encoded, you should hear yourself. Silence now is a fault after the gate (encode, decode or playback)"; }

        if (InIsGateOpen)
        { return "Gate open: you should hear yourself. Silence now is a fault after the gate (encode, decode or playback)"; }

        if (_PeakAmplitude < Threshold)
        { return f"Gate closed: your level never reached the threshold {Threshold :.3} this transmit. Press Z to lower it, or G to raise the gain"; }

        if (InMicLevel < Threshold)
        { return f"Gate closed: the level now is under the threshold {Threshold :.3} - this is the gate cutting the room between words. Speak: it should open"; }

        return "Gate closed although the level is over the threshold now - it opens 20 ms after the level crosses it; if it stays closed, that is a fault";
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (ck::Is_NOT_Valid(_GymTalker))
        { return; }

        if (InRowIndex == Row_TxLatch)
        {
            if (utils_voice_talker::Get_IsTransmitting(_GymTalker))
            { utils_voice_talker::Request_StopTransmit(_GymTalker); }
            else
            { utils_voice_talker::Request_StartTransmit(_GymTalker, FCk_Request_VoiceTalker_StartTransmit()); }

            return;
        }

        if (InRowIndex == Row_Gain)
        {
            // Doubles from the module default up to x9.6, then wraps. Read back from the talker, not mirrored here.
            const float32 Gain = utils_voice_talker::Get_InputGain(_GymTalker);
            utils_voice_talker::Request_SetInputGain(_GymTalker, Gain >= 9.0f ? 1.2f : Gain * 2.0f);
            return;
        }

        if (InRowIndex == Row_Mode)
        {
            // PushToTalk -> OpenMic arms the open mic, which opens the transmit itself on this possessed pawn; leaving
            // OpenMic closes it. A transmit V holds open when switching to OpenMic is kept as it is (the module's rule).
            const bool IsOpenMic = utils_voice_talker::Get_TransmitMode(_GymTalker) == ECk_VoiceChat_TransmitMode::OpenMic;
            utils_voice_talker::Request_SetTransmitMode(_GymTalker,
                IsOpenMic ? ECk_VoiceChat_TransmitMode::PushToTalk : ECk_VoiceChat_TransmitMode::OpenMic);
            return;
        }

        if (InRowIndex == Row_ThresholdDown || InRowIndex == Row_ThresholdUp)
        {
            utils_voice_talker::Request_SetVadThreshold(_GymTalker,
                Get_NextThreshold(utils_voice_talker::Get_VadThreshold(_GymTalker), InRowIndex == Row_ThresholdUp));
            return;
        }

        if (InRowIndex == Row_GateInPushToTalk)
        {
            const bool IsOn = utils_voice_talker::Get_GateInPushToTalk(_GymTalker) == ECk_EnableDisable::Enable;
            utils_voice_talker::Request_SetGateInPushToTalk(_GymTalker, IsOn ? ECk_EnableDisable::Disable : ECk_EnableDisable::Enable);
        }
    }

    // 0.005 steps below 0.050 and 0.010 steps from there, snapped to the 0.005 grid so the panel shows the exact value:
    // from the module default 0.070, 0.005 is 11 presses of Z and 0.200 is 13 presses of X. Bounded to 0..1 as the module is.
    private float32 Get_NextThreshold(float32 InThreshold, bool InUp) const
    {
        const bool IsInFineBand = InUp ? InThreshold < 0.0499f : InThreshold <= 0.0501f;
        const float32 Step = IsInFineBand ? 0.005f : 0.01f;
        const float32 Next = InUp ? InThreshold + Step : InThreshold - Step;

        return Math::Clamp(float32(Math::RoundToInt(Next / 0.005f)) * 0.005f, 0.0f, 1.0f);
    }
}
