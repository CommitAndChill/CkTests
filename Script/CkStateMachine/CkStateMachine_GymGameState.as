// One authority bank; all client presentation is copied from ordered values.
class ACk_SmTest_GymGameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;

    UPROPERTY(Replicated) FCkStateMachineGym_WireSnapshot Snapshot;
    UPROPERTY(Replicated) bool SourceFailed = false;
    UPROPERTY(Replicated) private TArray<ACk_SmTest_GymSourceActor> _Sources;
    private TArray<int64> _Generations;
    private UWorld _World;
    private FCk_Handle _WorldRoot;
    private FCkGym_Entry _Entry;
    private int32 _Index = -1;
    private bool _Captured = false;
    private bool _Failed = false;
    private bool _Ending = false;
    private int32 _Phase = 0; // 0 waiting, 1 primaries, 2 starting, 3 active, 4 retiring, 5 replacement
    private double _PendingSince = 0.0;
    private double _PublishElapsed = 0.0;
    private bool _HasConsumed = false;
    private FCkStateMachineGym_WireSnapshot _Consumed;
    private ACk_SmTest_GymPlayerController _Requester;
    private APawn _RequesterPawn;
    private int32 _RequestSerial = 0;
    private int32 _Replacing = -1;
    private int64 _RequestedGeneration = 0;
    private bool _RetirementRequested = false;
    private TArray<FCk_Handle> _Retiring;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    { SetTickGroup(ETickingGroup::TG_PostUpdateWork); }

    UFUNCTION(BlueprintPure)
    AActor Get_SourceActor(int32 Index)
    { return Index >= 0 && Index < _Sources.Num() ? _Sources[Index] : nullptr; }

    UFUNCTION(BlueprintPure)
    bool Get_HasSessionFailure() const { return _Failed || SourceFailed; }

    private bool TryCapture()
    {
        auto World = GetWorld();
        if (_Ending || IsActorBeingDestroyed() || ck::Is_NOT_Valid(World) ||
            World.GetGameState() != this || !Get_HasPublishedCatalog() ||
            (_Captured && World != _World)) { return false; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_SmTest_GymGameMode) { return false; }
        }
        else if (World.GetNetMode() != ENetMode::NM_Client) { return false; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return false; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "State Machine" ||
            Entry.GameModeClass != ACk_SmTest_GymGameMode ||
            (_Captured && (Index != _Index || Entry.DisplayName != _Entry.DisplayName ||
                Entry.GameModeClass != _Entry.GameModeClass || Entry.LevelName != _Entry.LevelName ||
                Entry.Category != _Entry.Category || Entry.HintCode != _Entry.HintCode)))
        { return false; }
        auto Ecs = Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(Ecs)) { return false; }
        auto Root = Ecs.Get_TransientEntity();
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Root) ||
            ck::Is_NOT_Valid(Root) ||
            utils_entity_lifetime::Get_IsPendingDestroy(Root,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            utils_entity_lifetime::Get_WorldForEntity(Root) != World ||
            (_Captured && Root != _WorldRoot)) { return false; }
        bool Closed = true;
        if (!UCkGym_AdmissionRuntime_UE::TryGet_IsClosedUnproven(Root, Closed) || Closed)
        { return false; }
        if (!_Captured)
        { _World = World; _WorldRoot = Root; _Entry = Entry; _Index = Index; _Captured = true; }
        return true;
    }

    void Request_CloseClient(FString Reason)
    {
        if (HasAuthority() || _Failed || _Ending) { return; }
        _Failed = true;
        if (_Captured) { UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot); }
        ck::Trace(f"StateMachine client presentation closed: {Reason}");
    }

    UFUNCTION()
    void Request_Fail(FString Reason)
    {
        if (!HasAuthority()) { Request_CloseClient(Reason); return; }
        if (_Failed || _Ending) { return; }
        _Failed = true; SourceFailed = true; _Phase = 0;
        // Close every callback before any healthy-tree retirement can invoke one.
        // Each source retains its own exact receipts; partial setup is never retried.
        for (auto Source : _Sources)
        {
            if (ck::IsValid(Source) && Source.GetWorld() == _World)
            { Source.Request_BlockSource(); }
        }
        for (auto Source : _Sources)
        {
            if (ck::IsValid(Source) && Source.GetWorld() == _World)
            { Source.Request_CloseSource(); }
        }
        if (_Captured) { UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot); }
        Snapshot.Phase = 4; Snapshot.Replacing = -1;
        if (Snapshot.Sequence < 9223372036854775806) { Snapshot.Sequence++; }
        ForceNetUpdate();
        CompleteRequest(false);
        ck::Trace(f"StateMachine bank closed: {Reason}");
    }

    private ACk_SmTest_GymSourceActor SpawnSource(int32 Index)
    {
        if (!HasAuthority() || !TryCapture() || Index < 0 || Index >= 9) { return nullptr; }
        TSubclassOf<ACk_SmTest_GymSourceActor> Class = ACk_SmTest_GymActor;
        if (Index == 4) { Class = ACk_SmTest_GraphWalkRegression_GymActor; }
        else if (Index == 5) { Class = ACk_SmTest_DivergenceFirstBranch_GymActor; }
        else if (Index == 6) { Class = ACk_SmTest_DivergenceTimed_GymActor; }
        else if (Index == 7) { Class = ACk_SmTest_RacingEventDriven_GymActor; }
        else if (Index == 8) { Class = ACk_SmTest_EventDrivenMultiCondition_GymActor; }
        auto Pose = CkStateMachineGym::Get_SourceTransform(Index);
        auto Source = Cast<ACk_SmTest_GymSourceActor>(SpawnActor(
            Class, Pose.GetLocation(), Pose.Rotator(), NAME_None, true));
        if (ck::Is_NOT_Valid(Source)) { return nullptr; }
        _Sources[Index] = Source; // retain before FinishSpawning can call back
        Source.SetOwner(this);
        Source.DeferSourceStart = true;
        if (Index < 4)
        {
            auto Standard = Cast<ACk_SmTest_GymActor>(Source);
            if (ck::Is_NOT_Valid(Standard)) { Request_Fail("Wrong standard source class"); return nullptr; }
            Standard.CycleDuration = Index == 0 ? 2.0f : (Index == 1 ? 1.5f : 1.0f);
            if (Index == 1)
            { Standard.EnablePauseResume = true; Standard.PauseAfterTransitions = 6; Standard.PauseDuration = 3.0f; }
            else if (Index == 2) { Standard.InitialStateClass = UCk_SmTest_Complex_State_Idle; }
            else if (Index == 3) { Standard.InitialStateClass = UCk_SmTest_Hier_Parent_Spawn; }
        }
        else if (Index == 7)
        {
            auto Racing = Cast<ACk_SmTest_RacingEventDriven_GymActor>(Source);
            if (ck::Is_NOT_Valid(Racing)) { Request_Fail("Wrong racing source class"); return nullptr; }
            Racing.SlowDelaySeconds = 2.0f; Racing.FastDelaySeconds = 0.5f; Racing.SettleSeconds = 2.5f;
        }
        else if (Index == 8)
        {
            auto Multi = Cast<ACk_SmTest_EventDrivenMultiCondition_GymActor>(Source);
            if (ck::Is_NOT_Valid(Multi)) { Request_Fail("Wrong multi-condition source class"); return nullptr; }
            Multi.FastDelaySeconds = 0.5f; Multi.SlowDelaySeconds = 2.0f; Multi.SettleSeconds = 2.5f;
        }
        FinishSpawningActor(Source);
        return Source;
    }

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || _Ending || _Failed || _Phase != 0 || !TryCapture()) { return; }
        if (CkGym_Admission::Get_IsBusy(_WorldRoot)) { return; }
        _Phase = 1;
        _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        for (int32 I = 0; I < 9; ++I) { _Sources.Add(nullptr); _Generations.Add(1); }
        for (int32 I = 0; I < 9; ++I)
        {
            auto Source = SpawnSource(I);
            if (_Failed || ck::Is_NOT_Valid(Source) || Source.Get_SourceClosed())
            { Request_Fail(f"Source {I} preparation rejected"); return; }
        }
    }

    private bool HasSource(int32 Index)
    {
        if (_Sources.Num() != 9 || _Generations.Num() != 9 || Index < 0 || Index >= 9)
        { return false; }
        auto Source = _Sources[Index];
        return ck::IsValid(Source) && !Source.IsActorBeingDestroyed() &&
            Source.GetWorld() == _World && Source.HasAuthority() && Source.GetOwner() == this &&
            !Source.Get_SourceClosed();
    }

    private bool TryReadSource(int32 Index, FCkStateMachineGym_StationFrame&out Frame)
    {
        Frame = FCkStateMachineGym_StationFrame();
        if (!HasSource(Index)) { return false; }
        auto Source = _Sources[Index];
        if (!Source.Get_SourcePrepared() || !Source.Get_SourceStarted()) { return false; }
        auto Roots = Source.Get_SourceRoots();
        if (Roots.IsEmpty() || Roots.Num() != Source.Get_SourceConstructedCount()) { return false; }
        for (auto Root : Roots)
        {
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Root) ||
                ck::Is_NOT_Valid(Root) || !utils_state_machine::Has(Root)) { return false; }
        }
        FString Title; FString Detail; FColor Color;
        if (!Source.TryGet_SourcePresentation(Title, Detail, Color) ||
            Title.Len() > 4096 || Detail.Len() > 16384) { return false; }
        Frame.Index = Index; Frame.Generation = _Generations[Index];
        Frame.Title = Title; Frame.Detail = Detail; Frame.Color = Color;
        return true;
    }

    private bool PublishComplete()
    {
        if (!TryCapture() || !HasRequestOwner() || Snapshot.Sequence >= 9223372036854775806)
        { Request_Fail("Publication lost its world or requester"); return false; }
        FCkStateMachineGym_WireSnapshot Frame;
        Frame.SourceGeneration = 1; Frame.Sequence = Snapshot.Sequence + 1; Frame.Phase = 2;
        for (int32 I = 0; I < 9; ++I)
        {
            FCkStateMachineGym_StationFrame Station;
            if (!TryReadSource(I, Station)) { return false; }
            Frame.Stations.Add(Station);
        }
        Snapshot = Frame;
        for (int32 I = 0; I < 9; ++I) { _Sources[I].Publish_SourcePresentation(Frame.Stations[I]); }
        _PublishElapsed = 0.0;
        ForceNetUpdate();
        return true;
    }

    private bool HasRequestOwner()
    {
        if (_RequestSerial == 0) { return true; }
        return ck::IsValid(_Requester) && !_Requester.IsActorBeingDestroyed() &&
            _Requester.GetWorld() == _World && _Requester.HasAuthority() &&
            _Requester.GetClass() == ACk_SmTest_GymPlayerController &&
            ck::IsValid(_RequesterPawn) && !_RequesterPawn.IsActorBeingDestroyed() &&
            _RequesterPawn.GetWorld() == _World && _Requester.GetControlledPawn() == _RequesterPawn &&
            _RequesterPawn.GetController() == _Requester;
    }

    bool TryStartControl(ACk_SmTest_GymPlayerController Requester, int32 Serial,
        int64 SourceGeneration, int32 StationIndex, int64 Generation)
    {
        if (!HasAuthority() || _Failed || _Ending || _Phase != 3 || !TryCapture() ||
            ck::Is_NOT_Valid(Requester) || Requester.IsActorBeingDestroyed() ||
            Requester.GetWorld() != _World || !Requester.HasAuthority() ||
            Requester.GetClass() != ACk_SmTest_GymPlayerController || Serial <= 0 ||
            _RequestSerial != 0 || SourceGeneration != 1 || StationIndex < 0 || StationIndex >= 9 ||
            _Generations.Num() != 9 || Generation != _Generations[StationIndex] ||
            Generation >= 9223372036854775806 || Snapshot.Sequence >= 9223372036854775806)
        { return false; }
        auto Pawn = Requester.GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn) || Pawn.IsActorBeingDestroyed() ||
            Pawn.GetWorld() != _World || Pawn.GetController() != Requester) { return false; }
        _Requester = Requester; _RequesterPawn = Pawn; _RequestSerial = Serial;
        _Replacing = StationIndex; _RequestedGeneration = Generation;
        _RetirementRequested = false; _Retiring.Empty(); _Phase = 4;
        _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        Snapshot.Phase = 3; Snapshot.Replacing = StationIndex; Snapshot.Sequence++;
        ForceNetUpdate();
        return true;
    }

    private void CompleteRequest(bool Success)
    {
        auto Requester = _Requester;
        int32 Serial = _RequestSerial; int32 Index = _Replacing;
        int64 Requested = _RequestedGeneration;
        _Requester = nullptr; _RequesterPawn = nullptr; _RequestSerial = 0;
        _Replacing = -1; _RequestedGeneration = 0;
        if (ck::IsValid(Requester) && Requester.GetWorld() == _World)
        {
            Requester.Complete_StateMachineControl(Serial, Index, Requested, Success,
                Success ? _Generations[Index] : 0, Success ? Snapshot.Sequence : 0);
        }
    }

    private bool ValidFrame(const FCkStateMachineGym_WireSnapshot&in Frame)
    {
        if (Frame.SourceGeneration != 1 || Frame.Sequence <= 0 ||
            (Frame.Phase != 2 && Frame.Phase != 3 && Frame.Phase != 4)) { return false; }
        if (Frame.Phase == 4) { return Frame.Replacing == -1; }
        if (Frame.Stations.Num() != 9 ||
            (Frame.Phase == 2 && Frame.Replacing != -1) ||
            (Frame.Phase == 3 && (Frame.Replacing < 0 || Frame.Replacing >= 9))) { return false; }
        for (int32 I = 0; I < 9; ++I)
        {
            auto Station = Frame.Stations[I];
            if (Station.Index != I || Station.Generation <= 0 ||
                Station.Title.Len() > 4096 || Station.Detail.Len() > 16384) { return false; }
        }
        return true;
    }

    private bool ConsumeSnapshot()
    {
        if (_Failed || _Ending || !TryCapture()) { return false; }
        if (Snapshot.Sequence == 0) { return false; }
        if (!ValidFrame(Snapshot)) { Request_CloseClient("Malformed source frame"); return false; }
        if (_HasConsumed)
        {
            if (Snapshot.Sequence < _Consumed.Sequence)
            { Request_CloseClient("Source frame moved backward"); return false; }
            if (Snapshot.Sequence == _Consumed.Sequence)
            {
                if (Snapshot.Phase != _Consumed.Phase || Snapshot.Replacing != _Consumed.Replacing ||
                    Snapshot.Stations.Num() != _Consumed.Stations.Num())
                { Request_CloseClient("Source frame identity changed"); return false; }
                for (int32 I = 0; I < Snapshot.Stations.Num(); ++I)
                {
                    auto A = Snapshot.Stations[I]; auto B = _Consumed.Stations[I];
                    if (A.Index != B.Index || A.Generation != B.Generation ||
                        A.Title != B.Title || A.Detail != B.Detail || A.Color != B.Color)
                    { Request_CloseClient("Source values changed without a new sequence"); return false; }
                }
                return Snapshot.Phase == 2;
            }
            if (Snapshot.Phase != 4 && _Consumed.Stations.Num() == 9)
            {
                for (int32 I = 0; I < 9; ++I)
                {
                    if (Snapshot.Stations[I].Generation < _Consumed.Stations[I].Generation)
                    { Request_CloseClient("Station generation moved backward"); return false; }
                }
            }
        }
        _Consumed = Snapshot; _HasConsumed = true;
        if (Snapshot.Phase == 4 || SourceFailed)
        { Request_CloseClient("Authority closed the bank"); return false; }
        return Snapshot.Phase == 2;
    }

    bool TryGetPresentation(FCkStateMachineGym_WireSnapshot&out Frame)
    {
        Frame = FCkStateMachineGym_WireSnapshot();
        if (_Failed || SourceFailed || _Ending || !TryCapture()) { return false; }
        if (HasAuthority())
        {
            if (_Phase != 3 || !ValidFrame(Snapshot) || Snapshot.Phase != 2) { return false; }
            Frame = Snapshot; return true;
        }
        if (!ConsumeSnapshot()) { return false; }
        Frame = _Consumed; return true;
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (_Failed || _Ending) { return; }
        if (!_Captured)
        {
            if (!HasAuthority()) { ConsumeSnapshot(); }
            return;
        }
        if (!TryCapture()) { Request_Fail("Captured world or catalog changed"); return; }
        if (!HasAuthority()) { ConsumeSnapshot(); return; }
        if (!HasRequestOwner()) { Request_Fail("Rerun requester changed"); return; }
        if (_Phase == 0) { return; }
        auto Now = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        if (_Phase != 3 && Now - _PendingSince >= 20.0)
        { Request_Fail("Source admission or physical retirement timed out"); return; }
        for (int32 I = 0; I < 9; ++I)
        {
            if (_Phase == 4 && I == _Replacing && _RetirementRequested) { continue; }
            if (!HasSource(I)) { Request_Fail(f"Source {I} lost its captured actor"); return; }
        }
        if (_Phase == 1)
        {
            for (auto Source : _Sources) { if (!Source.Get_SourcePrepared()) { return; } }
            _Phase = 2;
            for (auto Source : _Sources)
            {
                if (!Source.Request_StartAdmittedSource())
                { Request_Fail("Admitted source failed to start"); return; }
            }
        }
        if (_Phase == 2)
        {
            if (PublishComplete()) { _Phase = 3; }
            return;
        }
        if (_Phase == 3)
        {
            _PublishElapsed += DeltaSeconds;
            if (_PublishElapsed >= 0.1)
            {
                if (PublishComplete()) { _PendingSince = Now; }
                else if (Now - _PendingSince >= 20.0)
                { Request_Fail("Source values stopped converging"); }
            }
            return;
        }
        if (_Phase == 4)
        {
            auto Source = _Sources[_Replacing];
            if (!_RetirementRequested)
            {
                TArray<FCk_Handle> Forest; TArray<FCk_Handle> Parents;
                if (!Source.Get_SourcePrepared() ||
                    !UCk_StateMachineGym_Runtime_UE::TryCollect_DivergenceHealthyForest(
                        Source.Get_SourcePrimary(), Source, Source.Get_SourceRoots(), Forest, Parents))
                { return; }
                _Retiring = Forest;
                _RetirementRequested = true;
                Source.Request_CloseSource();
            }
            for (auto Handle : _Retiring)
            { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle)) { return; } }
            if (ck::IsValid(Source) && !Source.IsActorBeingDestroyed())
            {
                if (Source.GetWorld() != _World || Source.GetOwner() != this)
                { Request_Fail("Retired actor changed owner"); return; }
                Source.DestroyActor();
            }
            _Sources[_Replacing] = nullptr;
            _Generations[_Replacing]++;
            _Phase = 5;
            auto Replacement = SpawnSource(_Replacing);
            if (ck::Is_NOT_Valid(Replacement)) { Request_Fail("Replacement spawn rejected"); }
            return;
        }
        if (_Phase == 5)
        {
            auto Source = _Sources[_Replacing];
            if (!Source.Get_SourcePrepared()) { return; }
            if (!Source.Request_StartAdmittedSource())
            { Request_Fail("Replacement start rejected"); return; }
            if (PublishComplete())
            { _Phase = 3; _Retiring.Empty(); CompleteRequest(true); }
        }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        if (HasAuthority()) { Request_Fail("Gym session ended"); }
        _Ending = true;
    }
}
