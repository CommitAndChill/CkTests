//============================================================================
// BYTE ATTRIBUTE GYM - WORLD DECLARATIONS AND CONTROLLER
//============================================================================

namespace CkAttributeByte
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();

        // Byte Clamping Station
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.ByteClamping");
            Station.Title = FText::FromString("BYTE CLAMPING");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Demonstrates automatic value clamping within min/max boundaries."));
            Description.Add(FText::FromString("Watch attributes cycle and clamp when exceeding their limits."));
            Description.Add(FText::FromString("Armor: 0-200 | Stamina: 50-255 | Health: 0-100"));
            Description.Add(FText::FromString("Panel: [1] auto - [B] test boundaries - [R] reset all"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Byte Modifiers Station
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.ByteModifiers");
            Station.Title = FText::FromString("BYTE MODIFIERS");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Tests attribute modifier system with add/multiply operations."));
            Description.Add(FText::FromString("See how modifiers stack and affect final attribute values."));
            Description.Add(FText::FromString("Panel: [2] auto - [G] add modifier - [N] clear modifiers"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Byte MinMaxCurrent Station
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.ByteMinMaxCurrent");
            Station.Title = FText::FromString("BYTE MIN/MAX/CURRENT");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Displays all three attribute values: Min, Max, and Current."));
            Description.Add(FText::FromString("Watch how they update independently and interact."));
            Description.Add(FText::FromString("Panel: [4] auto - [7] Min - [8] Max - [9] Current"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Byte Multiple Station
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.ByteMultiple");
            Station.Title = FText::FromString("BYTE MULTIPLE ATTRIBUTES");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Entity with multiple byte attributes working simultaneously."));
            Description.Add(FText::FromString("Tests attribute independence and concurrent updates."));
            Description.Add(FText::FromString("Panel: [5] auto - [M] add batch - [U] clear batch"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Byte Values Station
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.ByteValues");
            Station.Title = FText::FromString("BYTE VALUES");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Basic attribute value operations and display."));
            Description.Add(FText::FromString("Tests get/set operations and value updates."));
            Description.Add(FText::FromString("Panel: [3] auto - [J] add modifier - [K] clear modifiers"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        // Byte Signals Station
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Attribute.ByteSignals");
            Station.Title = FText::FromString("BYTE SIGNALS");
            Station.AutoSize = true;
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Tests attribute signal system and callbacks."));
            Description.Add(FText::FromString("Monitors OnValueChanged, OnMinClamped, OnMaxClamped events."));
            Description.Add(FText::FromString("Panel: [6] auto - [0] signal value preset"));
            Station.Description = Description;
            Stations.Add(Station);
        }

        return Stations;
    }

    bool IsLiveEntity(FCk_Handle InHandle)
    {
        return ck::IsValid(InHandle) && !utils_entity_lifetime::Get_IsPendingDestroy(
            InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
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
        if (InIndex == 0) { return n"TAG_AttributeGym_ByteClamping"; }
        if (InIndex == 1) { return n"TAG_AttributeGym_ByteModifiers"; }
        if (InIndex == 2) { return n"TAG_AttributeGym_ByteMinMaxCurrent"; }
        if (InIndex == 3) { return n"TAG_AttributeGym_ByteMultiple"; }
        if (InIndex == 4) { return n"TAG_AttributeGym_ByteValues"; }
        if (InIndex == 5) { return n"TAG_AttributeGym_ByteSignals"; }
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

    ACk_ByteAttributeGym_GameState Get_State()
    {
        TArray<ACk_ByteAttributeGym_GameState> States;
        GetAllActorsOfClass(ACk_ByteAttributeGym_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0])) { return nullptr; }
        return States[0];
    }
}

class ACk_ByteAttributeGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    UPROPERTY(Replicated) int32 MinPresetIndex = -1;
    UPROPERTY(Replicated) int32 MaxPresetIndex = -1;
    UPROPERTY(Replicated) int32 MinAttemptPresetIndex = -1;
    UPROPERTY(Replicated) int32 MaxAttemptPresetIndex = -1;
    UPROPERTY(Replicated) int32 CurrentPresetIndex = -1;
    UPROPERTY(Replicated) int32 SignalPresetIndex = -1;
    private int32 _RequestedSerial = 0;
    private int32 _PendingSerial = 0;
    private float _PendingSeconds = 0.0;
    private int32 _ServerPendingSerial = 0;
    private int32 _ServerPendingGeneration = 0;
    private int32 _ServerPendingAction = -1;
    private int32 _ServerPendingPreset = -1;
    private int32 _AttemptEpoch = 0;
    private int32 _ServerPendingAttemptEpoch = 0;
    private int32 _StateWaitAttempts = 0;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        if (HasAuthority()) { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
        return CkAttributeByte::Get_Stations();
    }

    void Request_StartGym() override
    {
        auto State = CkAttributeByte::Get_State();
        if (ck::Is_NOT_Valid(State))
        {
            if (IsLocalController() && _StateWaitAttempts++ < 200)
            { System::SetTimer(this, n"OnStateWait", 0.1, false); }
            return;
        }
        if (HasAuthority() && State.Get_HasPublishedCatalog())
        { State.Request_EnsureSharedDemo(); }
        if (IsLocalController()) { State.Request_EnsureLocalProjection(); }
    }

    UFUNCTION() private void OnStateWait() { Request_StartGym(); }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (IsLocalController()) { Request_ReconcileControlAck(); }
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        Request_ReconcileControlAck();
        auto Rows = TArray<FCkGym_ControlRow>();
        auto State = CkAttributeByte::Get_State();
        bool _AutoClamping = ck::IsValid(State) && State.Get_AutoRunning(0);
        bool _AutoModifiers = ck::IsValid(State) && State.Get_AutoRunning(1);
        bool _AutoMinMaxCurrent = ck::IsValid(State) && State.Get_AutoRunning(2);
        bool _AutoMultiple = ck::IsValid(State) && State.Get_AutoRunning(3);
        bool _AutoValues = ck::IsValid(State) && State.Get_AutoRunning(4);
        bool _AutoSignals = ck::IsValid(State) && State.Get_AutoRunning(5);
        bool _AutoAllEnabled = _AutoClamping && _AutoModifiers && _AutoMinMaxCurrent &&
            _AutoMultiple && _AutoValues && _AutoSignals;
        Rows.Add(CkGym_Control::Header("BYTE ATTRIBUTE GYM"));
        Rows.Add(CkGym_Control::Toggle(EKeys::T, "T", "Auto-cycle all stations", _AutoAllEnabled));

        Rows.Add(CkGym_Control::Header("AUTO PER STATION"));
        Rows.Add(CkGym_Control::Numbered(0, "Clamping", _AutoClamping));
        Rows.Add(CkGym_Control::Numbered(1, "Modifiers", _AutoModifiers));
        Rows.Add(CkGym_Control::Numbered(2, "Values", _AutoValues));
        Rows.Add(CkGym_Control::Numbered(3, "Min/Max/Current", _AutoMinMaxCurrent));
        Rows.Add(CkGym_Control::Numbered(4, "Multiple", _AutoMultiple));
        Rows.Add(CkGym_Control::Numbered(5, "Signals", _AutoSignals));

        Rows.Add(CkGym_Control::Header("MIN/MAX/CURRENT STATION"));
        Rows.Add(CkGym_Control::Cycle(EKeys::Seven, "7", "Min preset",     DoGet_PresetLabel(MinPresetIndex,     "10 / 20 / 120 / 220")));
        Rows.Add(CkGym_Control::Cycle(EKeys::Eight, "8", "Max preset",     DoGet_PresetLabel(MaxPresetIndex,     "200 / 180 / 80 / 5")));
        Rows.Add(CkGym_Control::Cycle(EKeys::Nine,  "9", "Current preset", DoGet_PresetLabel(CurrentPresetIndex, "100 / 150 / 255 / 0")));

        // The Signals attribute spans 0-255, which IS the whole uint8 domain, so no argument can
        // land outside the band - this ring walks the band ends instead of provoking a clamp.
        Rows.Add(CkGym_Control::Header("SIGNALS STATION"));
        Rows.Add(CkGym_Control::Cycle(EKeys::Zero, "0", "Signal value preset", DoGet_PresetLabel(SignalPresetIndex, "150 / 0 / 255 / 200")));

        Rows.Add(CkGym_Control::Header("VALUES STATION"));
        Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Add modifier"));
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Clear modifiers"));

        Rows.Add(CkGym_Control::Header("MODIFIERS STATION"));
        Rows.Add(CkGym_Control::Action(EKeys::G, "G", "Add modifier"));
        Rows.Add(CkGym_Control::Action(EKeys::N, "N", "Clear modifiers"));

        Rows.Add(CkGym_Control::Header("MULTIPLE STATION"));
        Rows.Add(CkGym_Control::Action(EKeys::M, "M", "Add batch"));
        Rows.Add(CkGym_Control::Action(EKeys::U, "U", "Clear batch"));

        Rows.Add(CkGym_Control::Header("CLAMPING STATION"));
        Rows.Add(CkGym_Control::Action(EKeys::B, "B", "Test boundaries"));

        Rows.Add(CkGym_Control::Header("GLOBAL"));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Reset all stations"));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        Request_ReconcileControlAck();
        if (!IsLocalController() || _PendingSerial != 0) { return; }
        auto State = CkAttributeByte::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_HasCompleteSnapshot()) { return; }
        int32 Action = -1;
        if (InRowIndex == 1) { Action = 0; }
        else if (InRowIndex >= 3 && InRowIndex <= 8) { Action = InRowIndex - 2; }
        else if (InRowIndex >= 10 && InRowIndex <= 12) { Action = InRowIndex - 3; }
        else if (InRowIndex == 14) { Action = 10; }
        else if (InRowIndex == 16) { Action = 11; }
        else if (InRowIndex == 17) { Action = 12; }
        else if (InRowIndex == 19) { Action = 13; }
        else if (InRowIndex == 20) { Action = 14; }
        else if (InRowIndex == 22) { Action = 15; }
        else if (InRowIndex == 23) { Action = 16; }
        else if (InRowIndex == 25) { Action = 17; }
        else if (InRowIndex == 27) { Action = 18; }
        if (Action < 0) { return; }
        int32 Preset = -1;
        if (Action == 7) { Preset = (MinAttemptPresetIndex + 1) % 4; }
        if (Action == 8) { Preset = (MaxAttemptPresetIndex + 1) % 4; }
        if (Action == 9) { Preset = (CurrentPresetIndex + 1) % 4; }
        if (Action == 10) { Preset = (SignalPresetIndex + 1) % 4; }
        _RequestedSerial = Math::Max(_RequestedSerial, LastControlSerial) + 1;
        _PendingSerial = _RequestedSerial;
        _PendingSeconds = float(System::GetGameTimeInSeconds());
        System::SetTimer(this, n"OnControlDeadline", 25.0, false);
        Server_RequestByteControl(_PendingSerial, Action, Preset);
    }

    UFUNCTION() private void OnControlDeadline()
    {
        if (_PendingSerial == 0) { return; }
        Request_ReconcileControlAck();
        if (_PendingSerial == 0) { return; }
        if (float(System::GetGameTimeInSeconds()) - _PendingSeconds >= 25.0)
        { ck::Error("[AttributeByte] Control acknowledgement timed out"); _PendingSerial = 0; }
    }

    private void Request_ReconcileControlAck()
    {
        if (_PendingSerial > 0 &&
            (LastControlSerial >= _PendingSerial ||
             (LastControlAttemptSerial == _PendingSerial && LastControlResult != 0)))
        { _PendingSerial = 0; }
    }

    private FString DoGet_PresetLabel(int32 InIndex, FString InRing)
    { return InIndex < 0 ? f"({InRing})" : f"step {InIndex + 1}"; }

    void Request_CompleteByteControl(int32 InSerial, int32 InAction,
        int32 InPresetIndex, int32 InGeneration, bool InSuccess)
    {
        if (!HasAuthority() || InSerial <= 0 || InSerial <= LastControlSerial ||
            _ServerPendingSerial != InSerial || _ServerPendingGeneration != InGeneration ||
            _ServerPendingAction != InAction || _ServerPendingPreset != InPresetIndex)
        { return; }
        bool IsLatestAttempt = _AttemptEpoch == _ServerPendingAttemptEpoch;
        _ServerPendingSerial = 0;
        _ServerPendingGeneration = 0;
        _ServerPendingAction = -1;
        _ServerPendingPreset = -1;
        _ServerPendingAttemptEpoch = 0;
        if (!InSuccess)
        { if (IsLatestAttempt) { LastControlResult = 3; } return; }
        if (InAction == 7) { MinPresetIndex = InPresetIndex; MinAttemptPresetIndex = InPresetIndex; }
        if (InAction == 8) { MaxPresetIndex = InPresetIndex; MaxAttemptPresetIndex = InPresetIndex; }
        if (InAction == 9) { CurrentPresetIndex = InPresetIndex; }
        if (InAction == 10) { SignalPresetIndex = InPresetIndex; }
        if (InAction == 18)
        { MinPresetIndex = -1; MaxPresetIndex = -1; MinAttemptPresetIndex = -1;
          MaxAttemptPresetIndex = -1; CurrentPresetIndex = -1; SignalPresetIndex = -1; }
        LastControlSerial = InSerial;
        if (IsLatestAttempt) { LastControlResult = 1; }
    }

    UFUNCTION(Server)
    void Server_RequestByteControl(int32 InSerial, int32 InAction, int32 InPresetIndex)
    {
        if (!HasAuthority()) { return; }
        _AttemptEpoch++;
        LastControlAttemptSerial = InSerial;
        LastControlResult = 0;
        if (InSerial <= 0 || InSerial <= LastControlSerial || InAction < 0 || InAction > 18 ||
            ((InAction >= 7 && InAction <= 10) && (InPresetIndex < 0 || InPresetIndex >= 4)) ||
            ((InAction < 7 || InAction > 10) && InPresetIndex != -1) ||
            (InAction == 7 && InPresetIndex != (MinAttemptPresetIndex + 1) % 4) ||
            (InAction == 8 && InPresetIndex != (MaxAttemptPresetIndex + 1) % 4) ||
            (InAction == 9 && InPresetIndex != (CurrentPresetIndex + 1) % 4) ||
            (InAction == 10 && InPresetIndex != (SignalPresetIndex + 1) % 4))
        { LastControlResult = 4; return; }
        if (_ServerPendingSerial > 0)
        { LastControlResult = 3; return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this ||
            Pawn.GetWorld() != GetWorld())
        { LastControlResult = 2; return; }
        auto State = CkAttributeByte::Get_State();
        TArray<ACk_ByteAttributeGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_ByteAttributeGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto CurrentGym = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != GetWorld() ||
            Modes.Num() != 1 || ck::Is_NOT_Valid(Modes[0]) ||
            Modes[0].GetWorld() != GetWorld() || !State.Get_HasPublishedCatalog() ||
            CurrentGym < 0 || CurrentGym >= Registry.Num() ||
            Registry[CurrentGym].DisplayName != "Attribute Byte" ||
            !State.Get_HasReadyServerChildren())
        { LastControlResult = 3; return; }
        if (State.Get_IsCrossedBoundPreset(InAction, InPresetIndex))
        {
            if (InAction == 7) { MinAttemptPresetIndex = InPresetIndex; }
            if (InAction == 8) { MaxAttemptPresetIndex = InPresetIndex; }
            LastControlResult = 4;
            return;
        }
        _ServerPendingSerial = InSerial;
        _ServerPendingGeneration = State.Snapshot.Generation;
        _ServerPendingAction = InAction;
        _ServerPendingPreset = InPresetIndex;
        _ServerPendingAttemptEpoch = _AttemptEpoch;
        if (!State.Request_ApplyControl(this, InSerial, InAction, InPresetIndex))
        {
            if (_ServerPendingSerial == InSerial)
            { _ServerPendingSerial = 0; _ServerPendingGeneration = 0;
              _ServerPendingAction = -1; _ServerPendingPreset = -1;
              _ServerPendingAttemptEpoch = 0; }
            if (LastControlAttemptSerial == InSerial) { LastControlResult = 3; }
            return;
        }
    }
}

class ACk_ByteAttributeGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_ByteAttributeGym_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default GameStateClass = ACk_ByteAttributeGym_GameState;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Super::BeginPlay();
        auto State = CkAttributeByte::Get_State();
        if (ck::IsValid(State) && State.Get_HasPublishedCatalog())
        { State.Request_EnsureSharedDemo(); }
    }
}
