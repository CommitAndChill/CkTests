//============================================================================
// FLOAT ATTRIBUTE GYM - WORLD-OWNED CATALOG AND CONTROL IDENTITIES
//============================================================================

namespace CkAttributeFloat
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();

        // Float Values Station (6 steps, ~20 live-data lines)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.FloatValues");
            Station.Title = FText::FromString("FLOAT VALUES");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Basic float attribute value operations and display."));
            Description.Add(FText::FromString("Tests Base/Bonus/Final retrieval, percentage, and magnitude calculations."));
            Description.Add(FText::FromString("Panel: [1] auto - [J] add/clear modifier"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Float Clamping Station (1 step, ~16 live-data lines)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.FloatClamping");
            Station.Title = FText::FromString("FLOAT CLAMPING");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Demonstrates automatic value clamping within min/max boundaries."));
            Description.Add(FText::FromString("Cycles fractional values beyond limits to show float-precision clamping."));
            Description.Add(FText::FromString("Armor: 0-200 | Stamina: 50-255 | Health: 0-100 | Shield: 0-150"));
            Description.Add(FText::FromString("Panel: [2] auto - [B] test boundaries - [R] reset"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Float Modifiers Station (8 steps, ~14 live-data lines)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.FloatModifiers");
            Station.Title = FText::FromString("FLOAT MODIFIERS");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Tests attribute modifier system with add/multiply operations."));
            Description.Add(FText::FromString("See how modifiers stack and affect final attribute values."));
            Description.Add(FText::FromString("Panel: [3] auto - [K] add weapon / revoke weapon / clear all"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Float MinMaxCurrent Station (6 steps, ~16 live-data lines)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.FloatMinMaxCurrent");
            Station.Title = FText::FromString("FLOAT MIN/MAX/CURRENT");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Displays all three attribute components: Min, Max, and Current."));
            Description.Add(FText::FromString("Watch how they update independently and interact."));
            Description.Add(FText::FromString("Panel: [4] auto - [U] Min preset - [I] Max preset - [O] Current preset"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Float Signals Station (6 steps, ~18 live-data lines)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.FloatSignals");
            Station.Title = FText::FromString("FLOAT SIGNALS");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Tests attribute signal system and callbacks."));
            Description.Add(FText::FromString("Monitors OnValueChanged, OnMinClamped, OnMaxClamped events."));
            Description.Add(FText::FromString("Panel: [5] auto - [P] signal value preset"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Float Refill Station (6 steps, ~14 live-data lines)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.FloatRefill");
            Station.Title = FText::FromString("FLOAT REFILL");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Tests the float-exclusive refill/regeneration system."));
            Description.Add(FText::FromString("Drains energy, then watches it auto-refill. Pause/resume controls."));
            Description.Add(FText::FromString("Panel: [6] auto - [G] toggle refill / drain energy / drain mana"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Float Increment/Decrement Station (12 steps, ~12 live-data lines)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.FloatIncrementDecrement");
            Station.Title = FText::FromString("FLOAT INC/DEC");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Tests the float mixin increment/decrement helpers."));
            Description.Add(FText::FromString("Revocable vs non-revocable +1/-1 operations and revocation."));
            Description.Add(FText::FromString("Panel: [7] auto - [N] increment / decrement / revoke all"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Float Multiple Station (6 steps, ~10 live-data lines)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.FloatMultiple");
            Station.Title = FText::FromString("FLOAT MULTIPLE ATTRIBUTES");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Entity with multiple float attributes working simultaneously."));
            Description.Add(FText::FromString("Tests attribute independence, batch creation, and iteration patterns."));
            Description.Add(FText::FromString("Panel: [8] auto - [M] add batch / clear batch"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        return Stations;
    }
    FName Get_StationTag(int32 InIndex)
    {
        auto Stations = Get_Stations();
        if (InIndex < 0 || InIndex >= Stations.Num() || Stations[InIndex].Tags.Num() != 1)
        { return NAME_None; }
        return Stations[InIndex].Tags[0];
    }
    FName Get_ChildTag(int32 InIndex)
    {
        if (InIndex == 0) { return n"TAG_AttributeGym_FloatValues"; }
        if (InIndex == 1) { return n"TAG_AttributeGym_FloatClamping"; }
        if (InIndex == 2) { return n"TAG_AttributeGym_FloatModifiers"; }
        if (InIndex == 3) { return n"TAG_AttributeGym_FloatMinMaxCurrent"; }
        if (InIndex == 4) { return n"TAG_AttributeGym_FloatSignals"; }
        if (InIndex == 5) { return n"TAG_AttributeGym_FloatRefill"; }
        if (InIndex == 6) { return n"TAG_AttributeGym_FloatIncrementDecrement"; }
        if (InIndex == 7) { return n"TAG_AttributeGym_FloatMultiple"; }
        return NAME_None;
    }
    FCk_Handle FindStation(int32 InIndex)
    {
        auto World = ck::TransientEntity();
        if (!IsLiveEntity(World)) { return FCk_Handle(); }
        auto Tag = Get_StationTag(InIndex);
        if (Tag == NAME_None) { return FCk_Handle(); }
        auto Matches = utils_entity_tag::ForEach_Entity(World, Tag);
        if (Matches.Num() != 1 || !IsLiveEntity(Matches[0]) ||
            !Matches[0].Has_Fragment(FCkGym_Station_Anchors))
        { return FCk_Handle(); }
        return Matches[0];
    }
    bool IsAction(int32 InAction)
    {
        return InAction == 1 || (InAction >= 3 && InAction <= 10) ||
            (InAction >= 12 && InAction <= 16) ||
            (InAction >= 18 && InAction <= 21) ||
            InAction == 23 || InAction == 24;
    }
    int32 PresetCount(int32 InAction)
    {
        if (InAction == 12 || InAction == 16 || InAction == 24) { return 2; }
        if (InAction == 13 || InAction == 14 || InAction == 15) { return 3; }
        if (InAction >= 18 && InAction <= 21) { return 4; }
        return 0;
    }
    int32 Get_NextPreset(int32 InAction, int32 Min, int32 Max, int32 Cur,
        int32 Signal, int32 Values, int32 Modifiers, int32 Refill, int32 IncDec,
        int32 Multiple, int32 Reset)
    {
        int32 Count = PresetCount(InAction);
        int32 Cursor = -1;
        if (InAction == 12) { Cursor = Values; }
        if (InAction == 13) { Cursor = Modifiers; }
        if (InAction == 14) { Cursor = Refill; }
        if (InAction == 15) { Cursor = IncDec; }
        if (InAction == 16) { Cursor = Multiple; }
        if (InAction == 18) { Cursor = Min; }
        if (InAction == 19) { Cursor = Max; }
        if (InAction == 20) { Cursor = Cur; }
        if (InAction == 21) { Cursor = Signal; }
        if (InAction == 24) { Cursor = Reset; }
        if (Count > 0 && (Cursor < -1 || Cursor >= Count)) { return -1; }
        if (InAction == 12) { return (Values + 1) % 2; }
        if (InAction == 13) { return (Modifiers + 1) % 3; }
        if (InAction == 14) { return (Refill + 1) % 3; }
        if (InAction == 15) { return (IncDec + 1) % 3; }
        if (InAction == 16) { return (Multiple + 1) % 2; }
        if (InAction == 18) { return (Min + 1) % 4; }
        if (InAction == 19) { return (Max + 1) % 4; }
        if (InAction == 20) { return (Cur + 1) % 4; }
        if (InAction == 21) { return (Signal + 1) % 4; }
        if (InAction == 24) { return (Reset + 1) % 2; }
        return -1;
    }
    ACk_FloatAttributeGym_GameState Get_State()
    {
        TArray<ACk_FloatAttributeGym_GameState> States;
        GetAllActorsOfClass(ACk_FloatAttributeGym_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0])) { return nullptr; }
        return States[0];
    }
}

class ACk_FloatAttributeGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 MinPresetIndex = -1;
    UPROPERTY(Replicated) int32 MaxPresetIndex = -1;
    UPROPERTY(Replicated) int32 CurrentPresetIndex = -1;
    UPROPERTY(Replicated) int32 SignalPresetIndex = -1;
    UPROPERTY(Replicated) int32 ValuesActionIndex = -1;
    UPROPERTY(Replicated) int32 ModifiersActionIndex = -1;
    UPROPERTY(Replicated) int32 RefillActionIndex = -1;
    UPROPERTY(Replicated) int32 IncDecActionIndex = -1;
    UPROPERTY(Replicated) int32 MultipleActionIndex = -1;
    UPROPERTY(Replicated) int32 ResetActionIndex = -1;
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    private int32 _RequestedSerial = 0, _PendingSerial = 0;
    private float _PendingSeconds = 0.0;
    private int32 _ServerPendingSerial = 0, _ServerPendingGeneration = 0;
    private int32 _ServerPendingAction = -1, _ServerPendingPreset = -1;
    private int32 _AttemptEpoch = 0, _ServerPendingAttemptEpoch = 0;
    private int32 _StateWaitAttempts = 0;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        if (HasAuthority()) { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
        return CkAttributeFloat::Get_Stations();
    }
    void Request_StartGym() override
    {
        auto State = CkAttributeFloat::Get_State();
        if (ck::Is_NOT_Valid(State))
        {
            if (IsLocalController() && _StateWaitAttempts++ < 200)
            { System::SetTimer(this, n"OnStateWait", 0.1, false); }
            return;
        }
        if (HasAuthority()) { State.Request_EnsureSharedDemo(); }
        if (IsLocalController()) { State.Request_EnsureLocalProjection(); }
    }
    UFUNCTION() private void OnStateWait() { Request_StartGym(); }
    UFUNCTION(BlueprintOverride) void Tick(float DeltaSeconds)
    { if (IsLocalController()) { Request_ReconcileControlAck(); } }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        Request_ReconcileControlAck();
        auto State = CkAttributeFloat::Get_State();
        bool _AutoValues = ck::IsValid(State) && State.Get_AutoRunning(0);
        bool _AutoClamping = ck::IsValid(State) && State.Get_AutoRunning(1);
        bool _AutoModifiers = ck::IsValid(State) && State.Get_AutoRunning(2);
        bool _AutoMinMaxCurrent = ck::IsValid(State) && State.Get_AutoRunning(3);
        bool _AutoSignals = ck::IsValid(State) && State.Get_AutoRunning(4);
        bool _AutoRefill = ck::IsValid(State) && State.Get_AutoRunning(5);
        bool _AutoIncDec = ck::IsValid(State) && State.Get_AutoRunning(6);
        bool _AutoMultiple = ck::IsValid(State) && State.Get_AutoRunning(7);
        bool _AutoAllEnabled = _AutoValues && _AutoClamping && _AutoModifiers &&
            _AutoMinMaxCurrent && _AutoSignals && _AutoRefill && _AutoIncDec && _AutoMultiple;
        auto Rows = TArray<FCkGym_ControlRow>();
        Rows.Add(CkGym_Control::Header("FLOAT ATTRIBUTE GYM"));
        Rows.Add(CkGym_Control::Toggle(EKeys::T, "T", "Auto-cycle all stations", _AutoAllEnabled));

        Rows.Add(CkGym_Control::Header("AUTO PER STATION"));
        Rows.Add(CkGym_Control::Numbered(0, "Values", _AutoValues));
        Rows.Add(CkGym_Control::Numbered(1, "Clamping", _AutoClamping));
        Rows.Add(CkGym_Control::Numbered(2, "Modifiers", _AutoModifiers));
        Rows.Add(CkGym_Control::Numbered(3, "Min/Max/Current", _AutoMinMaxCurrent));
        Rows.Add(CkGym_Control::Numbered(4, "Signals", _AutoSignals));
        Rows.Add(CkGym_Control::Numbered(5, "Refill", _AutoRefill));
        Rows.Add(CkGym_Control::Numbered(6, "Inc/Dec", _AutoIncDec));
        Rows.Add(CkGym_Control::Numbered(7, "Multiple", _AutoMultiple));

        Rows.Add(CkGym_Control::Header("STATION ACTIONS"));
        Rows.Add(CkGym_Control::Cycle(EKeys::J, "J", "Values modifiers",  DoGet_PresetLabel(ValuesActionIndex,    "add modifier / clear modifiers")));
        Rows.Add(CkGym_Control::Cycle(EKeys::K, "K", "Modifiers station", DoGet_PresetLabel(ModifiersActionIndex, "add weapon / revoke weapon / clear all")));
        Rows.Add(CkGym_Control::Cycle(EKeys::G, "G", "Refill station",    DoGet_PresetLabel(RefillActionIndex,    "toggle refill / drain energy / drain mana")));
        Rows.Add(CkGym_Control::Cycle(EKeys::N, "N", "Inc/Dec station",   DoGet_PresetLabel(IncDecActionIndex,    "increment / decrement / revoke all")));
        Rows.Add(CkGym_Control::Cycle(EKeys::M, "M", "Multiple station",  DoGet_PresetLabel(MultipleActionIndex,  "add batch / clear batch")));

        Rows.Add(CkGym_Control::Header("PRESETS"));
        Rows.Add(CkGym_Control::Cycle(EKeys::U, "U", "Min preset (Min/Max/Current)",     DoGet_PresetLabel(MinPresetIndex,     "10 / 25.5 / 60 / 220")));
        Rows.Add(CkGym_Control::Cycle(EKeys::I, "I", "Max preset (Min/Max/Current)",     DoGet_PresetLabel(MaxPresetIndex,     "200 / 175.75 / 90 / 5")));
        Rows.Add(CkGym_Control::Cycle(EKeys::O, "O", "Current preset (Min/Max/Current)", DoGet_PresetLabel(CurrentPresetIndex, "100 / 142.3 / 250 / -40")));
        Rows.Add(CkGym_Control::Cycle(EKeys::P, "P", "Signal value preset (Signals)",    DoGet_PresetLabel(SignalPresetIndex,  "150 / 0 / 300 / -50")));

        Rows.Add(CkGym_Control::Header("GLOBAL"));
        Rows.Add(CkGym_Control::Action(EKeys::B, "B", "Test boundaries (Clamping)"));
        Rows.Add(CkGym_Control::Cycle(EKeys::R, "R", "Reset", DoGet_PresetLabel(ResetActionIndex, "clamping station / all stations")));
        return Rows;
    }
    void Request_ControlActivated(int32 InRowIndex) override
    {
        Request_ReconcileControlAck();
        if (!IsLocalController() || _PendingSerial != 0 ||
            _RequestedSerial >= 2147483647 || LastControlSerial >= 2147483647)
        { return; }
        auto State = CkAttributeFloat::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_HasCompleteSnapshot() ||
            !CkAttributeFloat::IsAction(InRowIndex)) { return; }
        int32 Preset = CkAttributeFloat::Get_NextPreset(InRowIndex,
            MinPresetIndex, MaxPresetIndex, CurrentPresetIndex, SignalPresetIndex,
            ValuesActionIndex, ModifiersActionIndex, RefillActionIndex, IncDecActionIndex,
            MultipleActionIndex, ResetActionIndex);
        if (CkAttributeFloat::PresetCount(InRowIndex) > 0 && Preset < 0) { return; }
        _RequestedSerial = Math::Max(_RequestedSerial, LastControlSerial) + 1;
        _PendingSerial = _RequestedSerial;
        _PendingSeconds = float(System::GetGameTimeInSeconds());
        System::SetTimer(this, n"OnControlDeadline", 25.0, false);
        Server_RequestFloatControl(_PendingSerial, InRowIndex, Preset);
    }
    UFUNCTION() private void OnControlDeadline()
    {
        if (_PendingSerial == 0) { return; }
        Request_ReconcileControlAck();
        if (_PendingSerial == 0) { return; }
        if (float(System::GetGameTimeInSeconds()) - _PendingSeconds >= 25.0)
        { ck::Error("[AttributeFloat] Control acknowledgement timed out"); _PendingSerial = 0; }
    }
    private void Request_ReconcileControlAck()
    {
        if (_PendingSerial > 0 && (LastControlSerial >= _PendingSerial ||
            (LastControlAttemptSerial == _PendingSerial && LastControlResult != 0)))
        { _PendingSerial = 0; }
    }
    void Request_CompleteFloatControl(int32 InSerial, int32 InAction,
        int32 InPreset, int32 InGeneration, bool InSuccess)
    {
        if (!HasAuthority() || InSerial <= 0 || InSerial <= LastControlSerial ||
            _ServerPendingSerial != InSerial || _ServerPendingGeneration != InGeneration ||
            _ServerPendingAction != InAction || _ServerPendingPreset != InPreset) { return; }
        bool Latest = _AttemptEpoch == _ServerPendingAttemptEpoch;
        _ServerPendingSerial = 0; _ServerPendingGeneration = 0;
        _ServerPendingAction = -1; _ServerPendingPreset = -1; _ServerPendingAttemptEpoch = 0;
        if (!InSuccess) { if (Latest) { LastControlResult = 3; } return; }
        if (InAction == 12) { ValuesActionIndex = InPreset; }
        if (InAction == 13) { ModifiersActionIndex = InPreset; }
        if (InAction == 14) { RefillActionIndex = InPreset; }
        if (InAction == 15) { IncDecActionIndex = InPreset; }
        if (InAction == 16) { MultipleActionIndex = InPreset; }
        if (InAction == 18) { MinPresetIndex = InPreset; }
        if (InAction == 19) { MaxPresetIndex = InPreset; }
        if (InAction == 20) { CurrentPresetIndex = InPreset; }
        if (InAction == 21) { SignalPresetIndex = InPreset; }
        if (InAction == 24)
        {
            ResetActionIndex = InPreset;
            if (InPreset == 1)
            { MinPresetIndex = -1; MaxPresetIndex = -1;
              CurrentPresetIndex = -1; SignalPresetIndex = -1; }
        }
        LastControlSerial = InSerial;
        if (Latest) { LastControlResult = 1; }
    }
    UFUNCTION(Server) void Server_RequestFloatControl(int32 InSerial, int32 InAction, int32 InPreset)
    {
        if (!HasAuthority()) { return; }
        _AttemptEpoch++; LastControlAttemptSerial = InSerial; LastControlResult = 0;
        if (InSerial <= 0 || InSerial <= LastControlSerial ||
            !CkAttributeFloat::IsAction(InAction) ||
            (CkAttributeFloat::PresetCount(InAction) > 0 &&
                (InPreset < 0 || InPreset >= CkAttributeFloat::PresetCount(InAction))) ||
            (CkAttributeFloat::PresetCount(InAction) == 0 && InPreset != -1) ||
            InPreset != CkAttributeFloat::Get_NextPreset(InAction,
                MinPresetIndex, MaxPresetIndex, CurrentPresetIndex, SignalPresetIndex,
                ValuesActionIndex, ModifiersActionIndex, RefillActionIndex, IncDecActionIndex,
                MultipleActionIndex, ResetActionIndex))
        { LastControlResult = 4; return; }
        if (_ServerPendingSerial > 0) { LastControlResult = 3; return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this ||
            Pawn.GetWorld() != GetWorld()) { LastControlResult = 2; return; }
        auto State = CkAttributeFloat::Get_State();
        TArray<ACk_FloatAttributeGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_FloatAttributeGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Gym = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != GetWorld() ||
            Modes.Num() != 1 || ck::Is_NOT_Valid(Modes[0]) || Modes[0].GetWorld() != GetWorld() ||
            !State.Get_HasCompleteSnapshot() || Gym < 0 || Gym >= Registry.Num() ||
            Registry[Gym].DisplayName != "Attribute Float" || !State.Get_HasReadyServerChildren())
        { LastControlResult = 3; return; }
        _ServerPendingSerial = InSerial; _ServerPendingGeneration = State.Snapshot.Generation;
        _ServerPendingAction = InAction; _ServerPendingPreset = InPreset;
        _ServerPendingAttemptEpoch = _AttemptEpoch;
        if (!State.Request_ApplyControl(this, InSerial, InAction, InPreset))
        {
            if (_ServerPendingSerial == InSerial)
            { _ServerPendingSerial = 0; _ServerPendingGeneration = 0;
              _ServerPendingAction = -1; _ServerPendingPreset = -1;
              _ServerPendingAttemptEpoch = 0; }
            if (LastControlAttemptSerial == InSerial) { LastControlResult = 3; }
        }
    }
    private FString DoGet_PresetLabel(int32 InIndex, FString InRing)
    { return InIndex < 0 ? f"({InRing})" : f"step {InIndex + 1}"; }
}

class ACk_FloatAttributeGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_FloatAttributeGym_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default GameStateClass = ACk_FloatAttributeGym_GameState;
    UFUNCTION(BlueprintOverride) void BeginPlay()
    {
        Super::BeginPlay();
        auto State = CkAttributeFloat::Get_State();
        if (ck::IsValid(State)) { State.Request_EnsureSharedDemo(); }
    }
}
