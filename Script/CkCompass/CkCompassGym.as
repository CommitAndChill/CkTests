// Language=angelscript
// One authority GameState owns the shared source; each owned local pawn
// carries its own Compass and each local controller draws its own overlay.
USTRUCT()
struct FCkCompassGym_ControlAck
{
    UPROPERTY() int32 Serial = 0;
    UPROPERTY() int64 SourceGeneration = 0;
    UPROPERTY() int32 Action = 0;
    // Admission only: 1 accepted, 2 rejected. Source completion is Snapshot.
    UPROPERTY() int32 Result = 0;
}

class ACk_CompassGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default GameStateClass = ACk_CompassGym_GameState;
    default PlayerControllerClass = ACk_CompassGym_PlayerController;
    default DefaultPawnClass = ACk_CompassGym_Pawn;
}

class ACk_CompassGym_Pawn : ACk_Gym_Base_Pawn
{
    default bUseControllerRotationYaw = true;
    private FCk_Handle _PawnEntity;
    private FCk_Handle_Compass _Compass;
    private ACk_CompassGym_PlayerController _CompassOwner;
    private bool _CompassAttempted = false;
    private bool _CompassFailed = false;

    private bool HasLocalActorContext(ACk_CompassGym_PlayerController Owner)
    {
        auto World = GetWorld();
        return ck::IsValid(World) && World.GetNetMode() != ENetMode::NM_DedicatedServer &&
            ck::IsValid(Owner) && Owner.IsLocalController() &&
            Owner.GetWorld() == World && GetController() == Owner &&
            Owner.GetControlledPawn() == this && !IsActorBeingDestroyed();
    }

    private bool HasLocalContext(ACk_CompassGym_PlayerController Owner)
    {
        if (!HasLocalActorContext(Owner)) { return false; }
        auto CurrentEntity = utils_owning_actor::TryGet_ActorEntityHandle(this);
        return ck::IsValid(CurrentEntity) &&
            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(CurrentEntity) &&
            utils_transform::Has(CurrentEntity) &&
            utils_entity_lifetime::Get_WorldForEntity(CurrentEntity) == GetWorld() &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                CurrentEntity, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            (!_CompassAttempted ||
                (_CompassOwner == Owner && _PawnEntity == CurrentEntity));
    }

    FCk_Handle_Compass TryGetLocalCompass(ACk_CompassGym_PlayerController Owner)
    {
        if (_CompassFailed || !HasLocalContext(Owner) || !_CompassAttempted ||
            ck::Is_NOT_Valid(_Compass) || FCk_Handle(_Compass) != _PawnEntity ||
            !utils_compass::Has(_PawnEntity)) { return FCk_Handle_Compass(); }
        return _Compass;
    }

    void Request_EnsureLocalCompass(ACk_CompassGym_PlayerController Owner)
    {
        if (_CompassAttempted && _CompassOwner != Owner) { _CompassFailed = true; return; }
        if (_CompassFailed) { return; }
        if (!HasLocalContext(Owner))
        {
            if (_CompassAttempted) { _CompassFailed = true; }
            return;
        }
        if (_CompassAttempted)
        {
            if (ck::Is_NOT_Valid(TryGetLocalCompass(Owner))) { _CompassFailed = true; }
            return;
        }
        _PawnEntity = utils_owning_actor::TryGet_ActorEntityHandle(this);
        _CompassOwner = Owner;
        _CompassAttempted = true;
        if (!HasLocalContext(Owner) || utils_compass::Has(_PawnEntity))
        { _CompassFailed = true; return; }
        auto Spec = FCk_Compass_Spec(180.0);
        Spec.Set_HeadingSource(ECk_Compass_HeadingSource::Auto);
        Spec.Set_MaxEntries(16);
        _Compass = utils_compass::Add(_PawnEntity, Spec);
        _CompassFailed = ck::Is_NOT_Valid(TryGetLocalCompass(Owner));
    }

    bool Get_HasCompassFailure() const { return _CompassFailed; }
    UFUNCTION(BlueprintPure)
    FCk_Handle_Compass Get_Compass() { return TryGetLocalCompass(Cast<ACk_CompassGym_PlayerController>(GetController())); }
}

class ACk_CompassGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) FCkCompassGym_ControlAck LastControlAck;
    private bool _ControllerAdmitted = false;
    private bool _Ending = false;
    private bool _QuestFilterActive = false;
    private float _ReadoutAccum = 0.0;
    private int32 _RequestedSerial = 0;
    private bool _LocalFailed = false;
    private bool _StationsBound = false;
    private TArray<FCk_Handle> _Stations;
    private FCk_Handle _StationCohort;
    private ACk_CompassGym_Pawn _LocalPawn;

    FCk_Handle Get_StationLifetimeOwner() override
    { return IsLocalController() ? ck::ToEntity(this) : FCk_Handle(); }
    FString Get_StationTag(FString Tag) override
    { return f"{Tag}_{GetName()}"; }

    private ACk_CompassGym_GameState Get_ExactState()
    {
        auto World = GetWorld();
        if (_Ending || IsActorBeingDestroyed() || ck::Is_NOT_Valid(World))
        { return nullptr; }
        auto State = Cast<ACk_CompassGym_GameState>(World.GetGameState());
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != World ||
            State.GetClass() != ACk_CompassGym_GameState ||
            State.IsActorBeingDestroyed() || !State.Get_HasPublishedCatalog())
        { return nullptr; }
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Registry.Num() ||
            Registry[Index].DisplayName != "Compass" ||
            Registry[Index].GameModeClass != ACk_CompassGym_GameMode)
        { return nullptr; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_CompassGym_GameMode) { return nullptr; }
        }
        return State;
    }

    private bool Get_HasExactPawn(APawn Pawn)
    {
        return ck::IsValid(Pawn) && Pawn.GetWorld() == GetWorld() &&
            !Pawn.IsActorBeingDestroyed() && Pawn.GetClass() == ACk_CompassGym_Pawn &&
            GetControlledPawn() == Pawn && Pawn.GetController() == this;
    }

    private bool Get_HasLocalPresentation()
    {
        if (!_ControllerAdmitted || !IsLocalController() || _Ending || _LocalFailed ||
            !_StationsBound || !CheckStations(false) ||
            !Get_HasExactPawn(GetControlledPawn())) { return false; }
        auto State = Get_ExactState();
        return ck::IsValid(State) && State.Get_IsPresentationReady();
    }

    private bool IsDirect(FCk_Handle Child, FCk_Handle Owner)
    {
        return ck::IsValid(Child) && ck::IsValid(Owner) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(Child, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            utils_entity_lifetime::Has_LifetimeOwner(Child) &&
            utils_entity_lifetime::Get_LifetimeOwner(Child) == Owner;
    }

    private bool CheckStations(bool Bind)
    {
        auto Parent = ck::ToEntity(this);
        auto WorldRoot = ck::TransientEntity();
        if (!IsLocalController() || ck::Is_NOT_Valid(Parent) || ck::Is_NOT_Valid(WorldRoot)) { return false; }
        TArray<FCk_Handle> Found;
        FCk_Handle Cohort;
        for (int32 I = 0; I < 2; ++I)
        {
            FString Tag = I == 0 ? "Gym.Compass.Main" : "Gym.Compass.Stress";
            auto Matches = utils_entity_tag::ForEach_Entity(WorldRoot, FName(Get_StationTag(Tag)));
            if (Matches.Num() != 1 || ck::Is_NOT_Valid(Matches[0]) ||
                !Matches[0].Has_Fragment(FCkGym_Station_Anchors) ||
                !utils_entity_lifetime::Has_LifetimeOwner(Matches[0])) { return false; }
            auto Station = Matches[0];
            auto Owner = utils_entity_lifetime::Get_LifetimeOwner(Station);
            if (!IsDirect(Station, Owner) || !IsDirect(Owner, Parent) ||
                (I > 0 && Owner != Cohort)) { return false; }
            FVector Anchor = I == 0 ? FVector(500.0,-400.0,0.0) : FVector(500.0,400.0,0.0);
            if ((Station.Get_Fragment(FCkGym_Station_Anchors).FootprintCenter - Anchor).SizeSquared() > 1.0) { return false; }
            auto Script = Cast<UCk_EntityScript_GymStation>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(Station));
            if (ck::Is_NOT_Valid(Script) ||
                UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(Script) != Station) { return false; }
            if (_StationsBound && (_Stations.Num() != 2 || _Stations[I] != Station || _StationCohort != Owner)) { return false; }
            Found.Add(Station); Cohort = Owner;
        }
        if (Bind && !_StationsBound) { _Stations = Found; _StationCohort = Cohort; _StationsBound = true; }
        return _StationsBound;
    }

    private void Request_CloseLocalFailure(FString Reason)
    {
        if (_LocalFailed) { return; }
        _LocalFailed = true;
        auto WorldRoot = ck::TransientEntity();
        if (ck::IsValid(WorldRoot) && utils_entity_lifetime::Get_WorldForEntity(WorldRoot) == GetWorld())
        {
            CkGym_Admission::TryCloseWorldUnproven(WorldRoot);
            utils_compass::TryCloseWorldProjection(WorldRoot);
        }
        auto State = Get_ExactState();
        if (HasAuthority() && ck::IsValid(State)) { State.Request_Fail(Reason); }
        ck::Error(f"Compass local presentation closed: {Reason}");
    }

    void Request_StartGym() override
    {
        _ControllerAdmitted = true;
        auto State = Get_ExactState();
        if (HasAuthority() && ck::IsValid(State)) { State.Request_EnsureSharedDemo(); }
    }

    private FCk_Handle_Compass DoGet_Compass()
    {
        if (!Get_HasLocalPresentation()) { return FCk_Handle_Compass(); }
        auto Pawn = Cast<ACk_CompassGym_Pawn>(GetControlledPawn());
        return ck::IsValid(Pawn) ? Pawn.TryGetLocalCompass(this) : FCk_Handle_Compass();
    }

    UFUNCTION(BlueprintOverride) void Tick(float DeltaSeconds)
    {
        if (!_ControllerAdmitted || _Ending) { return; }
        auto State = Get_ExactState();
        if (HasAuthority() && ck::IsValid(State)) { State.Request_EnsureSharedDemo(); }
        if (!IsLocalController() || _LocalFailed) { return; }
        if (!CheckStations(!_StationsBound)) { Request_CloseLocalFailure("Station cohort changed"); return; }
        if (!Get_HasLocalPresentation()) { return; }
        auto Pawn = Cast<ACk_CompassGym_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn)) { return; }
        if (_LocalPawn != Pawn)
        {
            _LocalPawn = Pawn;
            _QuestFilterActive = false;
            _ReadoutAccum = 0.0;
        }
        Pawn.Request_EnsureLocalCompass(this);
        if (Pawn.Get_HasCompassFailure()) { Request_CloseLocalFailure("Local Compass failed"); return; }
        auto Compass = DoGet_Compass();
        if (ck::Is_NOT_Valid(Compass)) { return; }
        DoDrawCompassOverlay(Compass);
        _ReadoutAccum += DeltaSeconds;
        if (_ReadoutAccum < 0.5) { return; }
        _ReadoutAccum = 0.0;
        ck::Trace(f"Compass [{GetName()}]: heading {utils_compass::Get_Heading(Compass)} | {utils_compass::Get_Entries(Compass).Num()} entries");
    }

    UFUNCTION(Server) void Server_RequestCompassControl(int32 Serial, int64 Generation, int32 Action)
    {
        if (Action < 1 || Action > 3 || Serial <= 0 ||
            LastControlAck.Serial >= 2147483647 || Serial != LastControlAck.Serial + 1 ||
            !_ControllerAdmitted || !HasAuthority() || _Ending) { return; }
        FCkCompassGym_ControlAck Ack;
        Ack.Serial = Serial; Ack.SourceGeneration = Generation;
        Ack.Action = Action; Ack.Result = 2;
        auto State = Get_ExactState();
        auto Pawn = GetControlledPawn();
        if (ck::IsValid(State) && Get_HasExactPawn(Pawn) &&
            Generation > 0 && Generation == State.Snapshot.SourceGeneration &&
            State.Snapshot.Lifecycle == ECkCompassGym_WireLifecycle::Active)
        {
            bool Accepted = false;
            if (Action == 1) { Accepted = State.TryRequestPing(this, Pawn); }
            else if (Action == 2) { Accepted = State.TryRequestStressAdmission(this, Pawn); }
            else if (Action == 3) { Accepted = State.TryRequestStressWithdrawal(this, Pawn); }
            if (Accepted) { Ack.Result = 1; }
        }
        LastControlAck = Ack;
        ForceNetUpdate();
    }

    private void Request_SharedControl(int32 Action)
    {
        if (!Get_HasLocalPresentation()) { return; }
        auto State = Get_ExactState();
        auto Serial = Math::Max(_RequestedSerial, LastControlAck.Serial);
        if (Serial >= 2147483647 || ck::Is_NOT_Valid(State)) { return; }
        _RequestedSerial = Serial + 1;
        Server_RequestCompassControl(_RequestedSerial, State.Snapshot.SourceGeneration, Action);
    }

    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        Super::EndPlay(Reason);
    }

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        if (!IsLocalController()) { return Stations; }

        auto Main = FCkGym_Station_SpawnParams_Payload();
        Main.Tags.Add(FName(Get_StationTag("Gym.Compass.Main")));
        Main.Transform = FTransform(FRotator(0.0, 180.0, 0.0), FVector(500.0, -400.0, 0.0));
        Main.Title = FText::FromString("COMPASS");
        auto MainDescription = TArray<FText>();
        MainDescription.Add(FText::FromString("POI ring around the station.\nTurn the mouse to sweep the compass heading."));
        MainDescription.Add(FText::FromString("Panel: [J] ping ahead\n[T] Quest-only filter\n[P] readout to the log"));
        Main.Description = MainDescription;
        Main.AutoSize = true;
        Stations.Add(Main);

        auto Stress = FCkGym_Station_SpawnParams_Payload();
        Stress.Tags.Add(FName(Get_StationTag("Gym.Compass.Stress")));
        Stress.Transform = FTransform(FRotator(0.0, 180.0, 0.0), FVector(500.0, 400.0, 0.0));
        Stress.Title = FText::FromString("COMPASS STRESS");
        auto StressDescription = TArray<FText>();
        StressDescription.Add(FText::FromString("500 shared POIs on demand. Watch the projector cost."));
        StressDescription.Add(FText::FromString("Panel: [M] spawn 500\n[U] clear"));
        Stress.Description = StressDescription;
        Stress.AutoSize = true;
        Stations.Add(Stress);

        return Stations;
    }

    private void DoDrawCompassOverlay(FCk_Handle_Compass InCompass)
    {
        if (ControlledPawn == nullptr)
        { return; }

        auto Base = ControlledPawn.GetActorLocation() + FVector(0.0, 0.0, 30.0);
        auto Heading = utils_compass::Get_Heading(InCompass);
        auto Rad = Heading * Math::PI / 180.0;
        auto Dir = FVector(Math::Cos(Rad), Math::Sin(Rad), 0.0);

        UCk_Utils_DebugDraw_UE::DrawDebugLine(Base, Base + Dir * 500.0,
            FLinearColor(0.2, 1.0, 0.4), 0.05, 6.0);
        UCk_Utils_DebugDraw_UE::DrawDebugLine(Base, Base + FVector(300.0, 0.0, 0.0),
            FLinearColor(1.0, 1.0, 1.0, 0.6), 0.05, 2.0);
        UCk_Utils_DebugDraw_UE::DrawDebugLine(Base, Base + FVector(0.0, 300.0, 0.0),
            FLinearColor(0.6, 0.6, 0.6, 0.5), 0.05, 2.0);
    }

    private void DoToggleQuestFilter()
    {
        auto Compass = DoGet_Compass();
        if (ck::Is_NOT_Valid(Compass))
        { return; }

        if (_QuestFilterActive)
        {
            Compass.Request_SetCategoryFilter(FCk_Request_Compass_SetCategoryFilter(FGameplayTagQuery()));
            _QuestFilterActive = false;
            ck::Trace("CompassGym: category filter CLEARED (all POIs)");
            return;
        }

        auto QuestOnly = FGameplayTagContainer();
        QuestOnly.AddTag(utils_gameplay_tag::ResolveGameplayTag(n"Poi.Category.Quest"));
        Compass.Request_SetCategoryFilter(FCk_Request_Compass_SetCategoryFilter(
            FGameplayTagQuery::MakeQuery_MatchAnyTags(QuestOnly)));
        _QuestFilterActive = true;
        ck::Trace("CompassGym: category filter = Quest only");
    }

    private void DoReadout()
    {
        auto Compass = DoGet_Compass();
        if (ck::Is_NOT_Valid(Compass))
        { return; }

        auto Heading = utils_compass::Get_Heading(Compass);
        ck::Trace(f"Compass readout: heading {Heading}");

        auto Entries = utils_compass::Get_Entries(Compass);
        for (auto Entry : Entries)
        {
            ck::Trace(f"  bearing {Entry.Get_BearingDegrees()} | offset {Entry.Get_NormalizedOffset()} | dist {Entry.Get_Distance()} | prio {Entry.Get_Priority()}");
        }
    }

    FString Get_ControlPanelTitle() override { return "COMPASS"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        auto Compass = DoGet_Compass();
        const bool Ready = ck::IsValid(Compass);
        auto Heading = Ready ? utils_compass::Get_Heading(Compass) : 0.0f;
        auto Entries = Ready ? utils_compass::Get_Entries(Compass).Num() : 0;
        int32 StressCount = 0;
        auto State = Get_ExactState();
        if (ck::IsValid(State))
        {
            for (auto Row : State.Snapshot.Rows)
            { if (Row.Kind == ECkCompassGym_SourceKind::Stress) { StressCount++; } }
        }
        Rows.Add(CkGym_Control::Header("COMPASS"));
        Rows.Add(CkGym_Control::Status("Heading", f"{Heading} - {Entries} entries"));
        Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Shared ping ahead (5s TTL)", Ready));
        Rows.Add(CkGym_Control::Toggle(EKeys::T, "T", "My Quest-only category filter", _QuestFilterActive, false, Ready));
        Rows.Add(CkGym_Control::Action(EKeys::P, "P", "My readout to the log", Ready));
        Rows.Add(CkGym_Control::Header("SHARED STRESS"));
        Rows.Add(CkGym_Control::Status("Stress POIs", f"{StressCount}"));
        Rows.Add(CkGym_Control::Action(EKeys::M, "M", "Spawn 500 shared POIs", Ready && StressCount == 0));
        Rows.Add(CkGym_Control::Action(EKeys::U, "U", "Clear the shared field", Ready && StressCount == 500));
        return Rows;
    }

    UFUNCTION()
    void Request_ControlActivated(int32 RowIndex) override
    {
        if (!Get_HasLocalPresentation()) { return; }
        if (RowIndex == 2) { Request_SharedControl(1); }
        else if (RowIndex == 3) { DoToggleQuestFilter(); }
        else if (RowIndex == 4) { DoReadout(); }
        else if (RowIndex == 7) { Request_SharedControl(2); }
        else if (RowIndex == 8) { Request_SharedControl(3); }
    }
}
