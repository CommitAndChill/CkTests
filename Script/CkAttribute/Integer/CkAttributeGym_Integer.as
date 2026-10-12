//============================================================================
// INTEGER ATTRIBUTE GYM - PLAYER CONTROLLER & GAME MODE
//============================================================================

namespace CkAttributeInteger
{
	TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
	{
		auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();

		// Basic Integer Attributes Station
		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.Attribute.IntegerBasic");
			Station.Title = FText::FromString("INTEGER BASIC ATTRIBUTES");
			Station.AutoSize = true;
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Tests integer attributes: Health (0-100), Armor (0-50), Experience (0+)."));
			Description.Add(FText::FromString("Auto-cycles through 6 phases every 2s."));
			Description.Add(FText::FromString("Panel: [1] auto - [5] Health - [6] Armor - [7] Experience"));
			Station.Description = Description;
			Stations.Add(Station);
		}

		// Min/Max/Current Station
		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.Attribute.IntegerMinMaxCurrent");
			Station.Title = FText::FromString("INTEGER MIN/MAX/CURRENT");
			Station.AutoSize = true;
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Displays all three attribute components independently."));
			Description.Add(FText::FromString("Shows how Min, Max, and Current values interact and update."));
			Description.Add(FText::FromString("Panel: [2] auto - [8] Min - [9] Max - [0] Current"));
			Station.Description = Description;
			Stations.Add(Station);
		}

		// Modifiers Station
		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.Attribute.IntegerModifiers");
			Station.Title = FText::FromString("INTEGER MODIFIERS");
			Station.AutoSize = true;
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Tests attribute modifier system with add/remove operations."));
			Description.Add(FText::FromString("Demonstrates weapon and buff modifier stacking."));
			Description.Add(FText::FromString("Panel: [3] auto - [I]/[O] add bonus - [J]/[K] remove - [M] clear all"));
			Station.Description = Description;
			Stations.Add(Station);
		}

		// Clamping & Signals Station
		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.Attribute.IntegerClamping");
			Station.Title = FText::FromString("INTEGER CLAMPING & SIGNALS");
			Station.AutoSize = true;
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Tests automatic value clamping and signal callbacks."));
			Description.Add(FText::FromString("Monitors OnMinClamped, OnMaxClamped events."));
			Description.Add(FText::FromString("Panel: [4] auto - [U] Resource preset - [B] test boundaries - [R] reset"));
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
        if (InIndex == 0) { return n"TAG_IntegerGym_Basic"; }
        if (InIndex == 1) { return n"TAG_IntegerGym_MinMaxCurrent"; }
        if (InIndex == 2) { return n"TAG_IntegerGym_Modifiers"; }
        if (InIndex == 3) { return n"TAG_IntegerGym_Clamping"; }
        return NAME_None;
    }
    bool IsLiveEntity(FCk_Handle InEntity)
    { return ck::IsValid(InEntity) && !utils_entity_lifetime::Get_IsPendingDestroy(
        InEntity, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
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
        return InAction == 1 || InAction == 3 || InAction == 4 ||
            InAction == 5 || InAction == 6 || InAction == 8 ||
            InAction == 9 || InAction == 10 || InAction == 12 ||
            InAction == 13 || InAction == 14 || InAction == 16 ||
            InAction == 17 || InAction == 18 || InAction == 19 ||
            InAction == 20 || InAction == 22 || InAction == 23 ||
            InAction == 24 || InAction == 26;
    }
    int32 PresetCount(int32 InAction)
    {
        if (InAction == 8 || InAction == 9 || InAction == 22) { return 5; }
        if (InAction == 10 || InAction == 12 || InAction == 13 ||
            InAction == 14 || InAction == 16 || InAction == 17) { return 4; }
        return 0;
    }
    int32 Get_NextPreset(int32 InAction, int32 H, int32 A, int32 X,
        int32 Min, int32 Max, int32 Cur, int32 Weapon, int32 Buff, int32 Resource)
    {
        if (InAction == 8) { return (H + 1) % 5; }
        if (InAction == 9) { return (A + 1) % 5; }
        if (InAction == 10) { return (X + 1) % 4; }
        if (InAction == 12) { return (Min + 1) % 4; }
        if (InAction == 13) { return (Max + 1) % 4; }
        if (InAction == 14) { return (Cur + 1) % 4; }
        if (InAction == 16) { return (Weapon + 1) % 4; }
        if (InAction == 17) { return (Buff + 1) % 4; }
        if (InAction == 22) { return (Resource + 1) % 5; }
        return -1;
    }
    int32 Get_PresetValue(int32 InAction, int32 InPreset)
    {
        if (InPreset < 0 || InPreset >= PresetCount(InAction)) { return 0; }
        auto Values = TArray<int32>();
        if (InAction == 8) { Values.Add(100); Values.Add(50); Values.Add(10); Values.Add(-10); Values.Add(150); }
        if (InAction == 9) { Values.Add(50); Values.Add(25); Values.Add(5); Values.Add(-5); Values.Add(80); }
        if (InAction == 10) { Values.Add(0); Values.Add(500); Values.Add(2000); Values.Add(-100); }
        if (InAction == 12) { Values.Add(10); Values.Add(20); Values.Add(60); Values.Add(120); }
        if (InAction == 13) { Values.Add(100); Values.Add(80); Values.Add(40); Values.Add(5); }
        if (InAction == 14) { Values.Add(50); Values.Add(75); Values.Add(150); Values.Add(-20); }
        if (InAction == 16) { Values.Add(25); Values.Add(50); Values.Add(-25); Values.Add(-200); }
        if (InAction == 17) { Values.Add(10); Values.Add(40); Values.Add(-10); Values.Add(-150); }
        if (InAction == 22) { Values.Add(50); Values.Add(0); Values.Add(100); Values.Add(150); Values.Add(-50); }
        if (Values.Num() == PresetCount(InAction)) { return Values[InPreset]; }
        return 0;
    }

    ACk_IntegerAttributeGym_GameState Get_State()
    {
        TArray<ACk_IntegerAttributeGym_GameState> States;
        GetAllActorsOfClass(ACk_IntegerAttributeGym_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0])) { return nullptr; }
        return States[0];
    }
}

class ACk_IntegerAttributeGym_PlayerController : ACk_Gym_Base_PlayerController
{
	// Preset-ring positions for the panel rows; each press applies the NEXT value, chosen to
	// exercise the auto-clamping (over-max, below-min) each station demonstrates.
	UPROPERTY(Replicated) int32 HealthPresetIndex = -1;
	UPROPERTY(Replicated) int32 ArmorPresetIndex = -1;
	UPROPERTY(Replicated) int32 ExperiencePresetIndex = -1;
	UPROPERTY(Replicated) int32 MinPresetIndex = -1;
	UPROPERTY(Replicated) int32 MaxPresetIndex = -1;
	UPROPERTY(Replicated) int32 CurrentPresetIndex = -1;
	UPROPERTY(Replicated) int32 ResourcePresetIndex = -1;
	UPROPERTY(Replicated) int32 WeaponBonusPresetIndex = -1;
	UPROPERTY(Replicated) int32 BuffBonusPresetIndex = -1;

    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
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
        return CkAttributeInteger::Get_Stations();
    }

    void Request_StartGym() override
    {
        auto State = CkAttributeInteger::Get_State();
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
    UFUNCTION(BlueprintOverride) void Tick(float DeltaSeconds)
    { if (IsLocalController()) { Request_ReconcileControlAck(); } }


	//------------------------------------------------------------------------
	// Control panel
	//------------------------------------------------------------------------

	TArray<FCkGym_ControlRow> Get_ControlRows() override
	{
		Request_ReconcileControlAck();
        auto State = CkAttributeInteger::Get_State();
        bool _AutoBasic = ck::IsValid(State) && State.Get_AutoRunning(0);
        bool _AutoMinMaxCurrent = ck::IsValid(State) && State.Get_AutoRunning(1);
        bool _AutoModifiers = ck::IsValid(State) && State.Get_AutoRunning(2);
        bool _AutoClamping = ck::IsValid(State) && State.Get_AutoRunning(3);
        bool _AutoAllEnabled = _AutoBasic && _AutoMinMaxCurrent && _AutoModifiers && _AutoClamping;
        auto Rows = TArray<FCkGym_ControlRow>();
		Rows.Add(CkGym_Control::Header("INTEGER ATTRIBUTE GYM"));
		Rows.Add(CkGym_Control::Toggle(EKeys::T, "T", "Auto-cycle all stations", _AutoAllEnabled));

		Rows.Add(CkGym_Control::Header("AUTO PER STATION"));
		Rows.Add(CkGym_Control::Numbered(0, "Basic", _AutoBasic));
		Rows.Add(CkGym_Control::Numbered(1, "Min/Max/Current", _AutoMinMaxCurrent));
		Rows.Add(CkGym_Control::Numbered(2, "Modifiers", _AutoModifiers));
		Rows.Add(CkGym_Control::Numbered(3, "Clamping", _AutoClamping));

		Rows.Add(CkGym_Control::Header("BASIC STATION"));
		Rows.Add(CkGym_Control::Cycle(EKeys::Five,  "5", "Health preset",     DoGet_PresetLabel(HealthPresetIndex,     "100 / 50 / 10 / -10 / 150")));
		Rows.Add(CkGym_Control::Cycle(EKeys::Six,   "6", "Armor preset",      DoGet_PresetLabel(ArmorPresetIndex,      "50 / 25 / 5 / -5 / 80")));
		Rows.Add(CkGym_Control::Cycle(EKeys::Seven, "7", "Experience preset", DoGet_PresetLabel(ExperiencePresetIndex, "0 / 500 / 2000 / -100")));

		Rows.Add(CkGym_Control::Header("MIN/MAX/CURRENT STATION"));
		Rows.Add(CkGym_Control::Cycle(EKeys::Eight, "8", "Min preset",     DoGet_PresetLabel(MinPresetIndex,     "10 / 20 / 60 / 120")));
		Rows.Add(CkGym_Control::Cycle(EKeys::Nine,  "9", "Max preset",     DoGet_PresetLabel(MaxPresetIndex,     "100 / 80 / 40 / 5")));
		Rows.Add(CkGym_Control::Cycle(EKeys::Zero,  "0", "Current preset", DoGet_PresetLabel(CurrentPresetIndex, "50 / 75 / 150 / -20")));

		Rows.Add(CkGym_Control::Header("MODIFIERS STATION"));
		Rows.Add(CkGym_Control::Cycle(EKeys::I, "I", "Weapon bonus", DoGet_PresetLabel(WeaponBonusPresetIndex, "+25 / +50 / -25 / -200")));
		Rows.Add(CkGym_Control::Cycle(EKeys::O, "O", "Buff bonus",   DoGet_PresetLabel(BuffBonusPresetIndex,   "+10 / +40 / -10 / -150")));
		Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Remove weapon bonus"));
		Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Remove buff bonus"));
		Rows.Add(CkGym_Control::Action(EKeys::M, "M", "Clear all modifiers"));

		Rows.Add(CkGym_Control::Header("CLAMPING STATION"));
		Rows.Add(CkGym_Control::Cycle(EKeys::U, "U", "Resource preset", DoGet_PresetLabel(ResourcePresetIndex, "50 / 0 / 100 / 150 / -50")));
		Rows.Add(CkGym_Control::Action(EKeys::B, "B", "Test boundaries"));
		Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Reset clamping station"));

		Rows.Add(CkGym_Control::Header("GLOBAL"));
		Rows.Add(CkGym_Control::Action(EKeys::N, "N", "Reset all stations"));
		return Rows;
	}

    void Request_ControlActivated(int32 InRowIndex) override
    {
        Request_ReconcileControlAck();
        if (!IsLocalController() || _PendingSerial != 0) { return; }
        auto State = CkAttributeInteger::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_HasCompleteSnapshot() ||
            !CkAttributeInteger::IsAction(InRowIndex)) { return; }
        int32 Preset = CkAttributeInteger::Get_NextPreset(InRowIndex,
            HealthPresetIndex, ArmorPresetIndex, ExperiencePresetIndex,
            MinPresetIndex, MaxPresetIndex, CurrentPresetIndex,
            WeaponBonusPresetIndex, BuffBonusPresetIndex, ResourcePresetIndex);
        _RequestedSerial = Math::Max(_RequestedSerial, LastControlSerial) + 1;
        _PendingSerial = _RequestedSerial;
        _PendingSeconds = float(System::GetGameTimeInSeconds());
        System::SetTimer(this, n"OnControlDeadline", 25.0, false);
        Server_RequestIntegerControl(_PendingSerial, InRowIndex, Preset);
    }
    UFUNCTION() private void OnControlDeadline()
    {
        if (_PendingSerial == 0) { return; }
        Request_ReconcileControlAck();
        if (_PendingSerial == 0) { return; }
        if (float(System::GetGameTimeInSeconds()) - _PendingSeconds >= 25.0)
        { ck::Error("[AttributeInteger] Control acknowledgement timed out"); _PendingSerial = 0; }
    }
    private void Request_ReconcileControlAck()
    {
        if (_PendingSerial > 0 && (LastControlSerial >= _PendingSerial ||
            (LastControlAttemptSerial == _PendingSerial && LastControlResult != 0)))
        { _PendingSerial = 0; }
    }
    void Request_CompleteIntegerControl(int32 InSerial, int32 InAction,
        int32 InPreset, int32 InGeneration, bool InSuccess)
    {
        if (!HasAuthority() || InSerial <= 0 || InSerial <= LastControlSerial ||
            _ServerPendingSerial != InSerial || _ServerPendingGeneration != InGeneration ||
            _ServerPendingAction != InAction || _ServerPendingPreset != InPreset) { return; }
        bool Latest = _AttemptEpoch == _ServerPendingAttemptEpoch;
        _ServerPendingSerial = 0; _ServerPendingGeneration = 0;
        _ServerPendingAction = -1; _ServerPendingPreset = -1; _ServerPendingAttemptEpoch = 0;
        if (!InSuccess) { if (Latest) { LastControlResult = 3; } return; }
        if (InAction == 8) { HealthPresetIndex = InPreset; }
        if (InAction == 9) { ArmorPresetIndex = InPreset; }
        if (InAction == 10) { ExperiencePresetIndex = InPreset; }
        if (InAction == 12) { MinPresetIndex = InPreset; }
        if (InAction == 13) { MaxPresetIndex = InPreset; }
        if (InAction == 14) { CurrentPresetIndex = InPreset; }
        if (InAction == 16) { WeaponBonusPresetIndex = InPreset; }
        if (InAction == 17) { BuffBonusPresetIndex = InPreset; }
        if (InAction == 22) { ResourcePresetIndex = InPreset; }
        if (InAction == 26)
        { HealthPresetIndex = -1; ArmorPresetIndex = -1; ExperiencePresetIndex = -1;
          MinPresetIndex = -1; MaxPresetIndex = -1; CurrentPresetIndex = -1;
          WeaponBonusPresetIndex = -1; BuffBonusPresetIndex = -1; ResourcePresetIndex = -1; }
        LastControlSerial = InSerial;
        if (Latest) { LastControlResult = 1; }
    }
    UFUNCTION(Server) void Server_RequestIntegerControl(int32 InSerial, int32 InAction, int32 InPreset)
    {
        if (!HasAuthority()) { return; }
        _AttemptEpoch++; LastControlAttemptSerial = InSerial; LastControlResult = 0;
        if (InSerial <= 0 || InSerial <= LastControlSerial ||
            !CkAttributeInteger::IsAction(InAction) ||
            InPreset != CkAttributeInteger::Get_NextPreset(InAction,
                HealthPresetIndex, ArmorPresetIndex, ExperiencePresetIndex,
                MinPresetIndex, MaxPresetIndex, CurrentPresetIndex,
                WeaponBonusPresetIndex, BuffBonusPresetIndex, ResourcePresetIndex))
        { LastControlResult = 4; return; }
        if (_ServerPendingSerial > 0) { LastControlResult = 3; return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this ||
            Pawn.GetWorld() != GetWorld()) { LastControlResult = 2; return; }
        auto State = CkAttributeInteger::Get_State();
        TArray<ACk_IntegerAttributeGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_IntegerAttributeGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Gym = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != GetWorld() ||
            Modes.Num() != 1 || ck::Is_NOT_Valid(Modes[0]) || Modes[0].GetWorld() != GetWorld() ||
            !State.Get_HasPublishedCatalog() || Gym < 0 || Gym >= Registry.Num() ||
            Registry[Gym].DisplayName != "Attribute Integer" || !State.Get_HasReadyServerChildren())
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
	{
		return InIndex < 0 ? f"({InRing})" : f"step {InIndex + 1}";
	}

}

//============================================================================
// GAME MODE
//============================================================================

class ACk_IntegerAttributeGym_GameMode : ACkTests_Gym_Base_GameMode
{
	default PlayerControllerClass = ACk_IntegerAttributeGym_PlayerController;
	default DefaultPawnClass = ACk_Gym_Base_Pawn;
	default GameStateClass = ACk_IntegerAttributeGym_GameState;
	UFUNCTION(BlueprintOverride) void BeginPlay()
	{
		Super::BeginPlay();
		auto State = CkAttributeInteger::Get_State();
		if (ck::IsValid(State) && State.Get_HasPublishedCatalog())
		{ State.Request_EnsureSharedDemo(); }
	}
}
