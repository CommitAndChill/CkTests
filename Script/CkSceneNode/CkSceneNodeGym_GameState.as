// One authority-world SceneNode simulation and one passive projection per client world.
// Each local player owns a separate overlay; no PC owns the shared source.
UCLASS()
class ACk_SceneNodeGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkSceneNodeGym_Snapshot Snapshot;

    // Strong reflected references: the native records themselves retain raw
    // creation evidence and remain available after a failed admission.
    UPROPERTY() private UCk_SceneNodeGym_ActorLedger_UE _Actors;
    UPROPERTY() private UCk_SceneNodeGym_EcsRoots_UE _Roots;
    UPROPERTY() private UCk_SceneNodeGym_NodeGraph_UE _Graph;
    UPROPERTY() private UCk_SceneNodeGym_Motion_UE _Motion;
    UPROPERTY() private UCk_SceneNodeGym_FrameSampler_UE _Sampler;
    // The completed receipt remains reflected while native successor admission
    // destroys the historical actor-ledger component.
    UPROPERTY() private UCk_SceneNodeGym_Retirement_UE _Retirement;
    private bool _Armed = false;
    private bool _Starting = false;
    private bool _ActivationIssued = false;
    private bool _Failed = false;
    private bool _Ending = false;
    private bool _RetiringShared = false;
    private float _MonitorElapsed = 0.0f;
    private float _StartedAt = 0.0f;
    private float _LastPublishedAt = 0.0f;
    private float _RetireStartedAt = 0.0f;
    private int32 _CatalogIndex = -1;
    private FCkGym_Entry _CatalogEntry;

    private bool Get_HasWorldIdentity()
    {
        if (_Ending || !HasAuthority() || IsActorBeingDestroyed() ||
            !Get_HasPublishedCatalog() ||
            GetClass() != ACk_SceneNodeGym_GameState) { return false; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) || World.GetGameState() != this) { return false; }
        auto Mode = Gameplay::GetGameMode();
        if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
            Mode.GetClass() != ACk_SceneNodeGym_GameMode) { return false; }
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Registry.Num()) { return false; }
        auto Entry = Registry[Index];
        if (Entry.DisplayName != "Scene Node" ||
            Entry.GameModeClass != ACk_SceneNodeGym_GameMode) { return false; }
        return _CatalogIndex < 0 ||
            (_CatalogIndex == Index &&
             _CatalogEntry.DisplayName == Entry.DisplayName &&
             _CatalogEntry.GameModeClass == Entry.GameModeClass &&
             _CatalogEntry.LevelName == Entry.LevelName &&
             _CatalogEntry.Category == Entry.Category &&
             _CatalogEntry.HintCode == Entry.HintCode);
    }

    // Any authority PC may arm the one shared source after catalog admission.
    void Request_EnsureSharedDemo()
    {
        if (_Armed || _Failed || !Get_HasWorldIdentity()) { return; }
        _CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        _CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[_CatalogIndex];
        _Armed = true;
    }

    private bool Get_IsParentRole(int32 Role)
    { return Role == 1 || Role == 3 || Role == 5 || Role == 10 || Role == 13 || Role == 16; }

    private int32 Get_StationForRole(int32 Role)
    {
        if (Role >= 1 && Role <= 2) { return 0; }
        if (Role >= 3 && Role <= 4) { return 1; }
        if (Role >= 5 && Role <= 9) { return 2; }
        if (Role >= 10 && Role <= 12) { return 3; }
        if (Role >= 13 && Role <= 15) { return 4; }
        if (Role >= 16 && Role <= 17) { return 5; }
        return -1;
    }

    private TArray<TSubclassOf<AActor>> Get_SourceVisualClasses()
    {
        auto Classes = TArray<TSubclassOf<AActor>>();
        for (int32 Role = 1; Role <= CkSceneNodeGymShared::VisualCount; ++Role)
        {
            if (Get_IsParentRole(Role)) { Classes.Add(ACk_SceneNodeGym_SourceParent); }
            else { Classes.Add(ACk_SceneNodeGym_SourceChild); }
        }
        return Classes;
    }

    private void Request_CloseKnownProducers()
    {
        if (ck::IsValid(_Sampler))
        { UCk_SceneNodeGym_FrameSamplerRuntime_UE::Request_Close(_Sampler); }
        if (ck::IsValid(_Motion))
        { UCk_SceneNodeGym_MotionRuntime_UE::TryClose(_Motion); }
        if (ck::IsValid(_Actors))
        { UCk_SceneNodeGym_ActorLedgerRuntime_UE::Request_Close(_Actors); }
    }

    private void Request_Fail(FString Reason)
    {
        if (_Failed || _Ending) { return; }
        _Failed = true;
        _Starting = false;
        _RetiringShared = false;
        Request_CloseKnownProducers();
        // No destructive fallback: retained native records name attempted IDs.
        if (Snapshot.Generation <= 0 || Snapshot.Sequence <= 0 ||
            Snapshot.Sequence >= 9223372036854775807) { return; }
        FCkSceneNodeGym_Snapshot Candidate;
        Candidate.Generation = Snapshot.Generation;
        Candidate.Sequence = Snapshot.Sequence + 1;
        Candidate.Lifecycle = ECkSceneNodeGym_Lifecycle::Failed;
        Candidate.FailureReason = Reason.IsEmpty() ? "SceneNode source admission failed" : Reason;
        if (CkSceneNodeGymShared::Get_IsValidPublicationAdvance(Snapshot, Candidate))
        {
            Snapshot = Candidate;
            if (ck::IsValid(GetWorld()) && !IsActorBeingDestroyed()) { ForceNetUpdate(); }
        }
    }

    private void StartActorsAndRoots(int32 Generation)
    {
        if (!Get_HasWorldIdentity() || ck::Is_NOT_Valid(_Actors) ||
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_Generation(_Actors) != Generation ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(_Actors))
        { Request_Fail("SceneNode reserved actor generation rejected"); return; }
        AActor Scope;
        if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
                _Actors, 0, FTransform::Identity, Scope) ||
            !Get_HasWorldIdentity() || !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(_Actors))
        { Request_Fail("SceneNode source scope rejected"); return; }
        for (int32 Role = 1; Role <= CkSceneNodeGymShared::VisualCount; ++Role)
        {
            auto Station = Get_StationForRole(Role);
            FTransform PanelCenter;
            if (Station < 0 || !CkSceneNodeGymCatalog::TryGet_PanelCenter(Station, PanelCenter))
            { Request_Fail("SceneNode station anchor rejected"); return; }
            AActor Visual;
            if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
                    _Actors, Role, PanelCenter, Visual) ||
                !Get_HasWorldIdentity() ||
                !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(_Actors))
            { Request_Fail("SceneNode visual role rejected"); return; }
        }
        if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(_Actors, true))
        { Request_Fail("SceneNode full actor cohort rejected"); return; }
        _Roots = UCk_SceneNodeGym_EcsRootsRuntime_UE::TryBegin(
            _Actors, UCk_SceneNodeGym_WithActorScript_UE);
        if (ck::Is_NOT_Valid(_Roots) ||
            UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsFailed(_Roots) ||
            UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsUnproven(_Roots))
        { Request_Fail("SceneNode six root admission rejected"); }
    }

    private void StartInitialGeneration()
    {
        FCkSceneNodeGym_Snapshot Initializing;
        Initializing.Generation = 1;
        Initializing.Sequence = 1;
        Initializing.Lifecycle = ECkSceneNodeGym_Lifecycle::Initializing;
        if (!Get_HasWorldIdentity() ||
            !CkSceneNodeGymShared::Get_IsValidPublicationAdvance(Snapshot, Initializing))
        { Request_Fail("SceneNode initial source publication rejected"); return; }
        Snapshot = Initializing;
        ForceNetUpdate();
        _Starting = true;
        _StartedAt = float(System::GetGameTimeInSeconds());

        auto Classes = Get_SourceVisualClasses();
        if (Classes.Num() != CkSceneNodeGymShared::VisualCount)
        { Request_Fail("SceneNode visual class catalog rejected"); return; }
        _Actors = UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(this, 1, Classes);
        if (ck::Is_NOT_Valid(_Actors) || !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(_Actors))
        { Request_Fail("SceneNode actor claim rejected"); return; }
        StartActorsAndRoots(1);
    }

    private bool Get_HasExactRebuildOwner(APlayerController Owner, APawn Pawn,
        int32 RequestSerial, int32 ExpectedGeneration)
    {
        if (!Get_HasWorldIdentity() || ck::Is_NOT_Valid(Owner) ||
            Owner.GetClass() != ACk_SceneNodeGym_PlayerController ||
            Owner.IsActorBeingDestroyed() || !Owner.HasAuthority() ||
            Owner.GetWorld() != GetWorld() || ck::Is_NOT_Valid(Pawn) ||
            Pawn.IsActorBeingDestroyed() || Pawn.GetWorld() != GetWorld() ||
            Cast<ACk_Gym_Base_Pawn>(Pawn) == nullptr)
        { return false; }
        auto ExactOwner = Cast<ACk_SceneNodeGym_PlayerController>(Owner);
        return ck::IsValid(ExactOwner) && Pawn.GetController() == Owner &&
            Owner.GetControlledPawn() == Pawn &&
            ExactOwner.Get_HasReservedSceneNodeControl(
                RequestSerial, ExpectedGeneration, Pawn);
    }

    // True admits one shared retirement, not completion of the next generation.
    // The requesting owner never owns the authority simulation.
    UFUNCTION(BlueprintCallable)
    bool Request_Rebuild(APlayerController Owner, APawn Pawn,
        int32 RequestSerial, int32 ExpectedGeneration)
    {
        if (_Failed || _Ending || _Starting || _RetiringShared || !_Armed ||
            RequestSerial <= 0 ||
            !Get_HasExactRebuildOwner(Owner, Pawn, RequestSerial, ExpectedGeneration) ||
            Snapshot.Lifecycle != ECkSceneNodeGym_Lifecycle::Active ||
            !CkSceneNodeGymShared::Get_IsValidSnapshot(Snapshot) ||
            ExpectedGeneration != Snapshot.Generation ||
            Snapshot.Generation <= 0 || Snapshot.Generation >= 2147483647 ||
            // Reserve Retiring, Initializing, first Active, and terminal Failed.
            Snapshot.Sequence <= 0 || Snapshot.Sequence >= 9223372036854775804 ||
            ck::Is_NOT_Valid(_Actors) || ck::Is_NOT_Valid(_Motion) ||
            ck::Is_NOT_Valid(_Sampler) ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(_Actors) ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(_Actors, true) ||
            !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(_Motion))
        { return false; }
        FCkSceneNodeGym_Snapshot Retiring;
        Retiring.Generation = Snapshot.Generation;
        Retiring.Sequence = Snapshot.Sequence + 1;
        Retiring.Lifecycle = ECkSceneNodeGym_Lifecycle::Retiring;
        if (!CkSceneNodeGymShared::Get_IsValidPublicationAdvance(Snapshot, Retiring) ||
            !Get_HasExactRebuildOwner(Owner, Pawn, RequestSerial, ExpectedGeneration))
        { return false; }

        // Publish the whole value-only retirement state before native closes
        // the sampler and producers. No stale poses or text cross this boundary.
        _RetiringShared = true;
        _RetireStartedAt = float(System::GetGameTimeInSeconds());
        Snapshot = Retiring;
        ForceNetUpdate();
        auto Begun = UCk_SceneNodeGym_RetirementRuntime_UE::TryBeginCompleted(_Motion);
        _Retirement = Begun;
        // Native admission can synchronously call back into this source.
        // A changed publication or owner context cannot become an accepted R.
        if (_Failed || _Ending || !_RetiringShared || !Get_HasWorldIdentity() ||
            !CkSceneNodeGymShared::Get_IsExactSnapshotValue(Snapshot, Retiring) ||
            _Retirement != Begun || ck::Is_NOT_Valid(Begun) ||
            UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(_Retirement) ==
                ECk_SceneNodeGym_RetirementState::Unproven)
        { Request_Fail("SceneNode completed retirement admission rejected"); return false; }
        return true;
    }

    private void AdvanceSharedRetirement()
    {
        if (!Get_HasWorldIdentity() || ck::Is_NOT_Valid(_Retirement))
        { Request_Fail("SceneNode retirement context lost"); return; }
        if (float(System::GetGameTimeInSeconds()) - _RetireStartedAt > 25.0f)
        { Request_Fail("SceneNode retirement timed out"); return; }
        auto State = UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(_Retirement);
        if (State == ECk_SceneNodeGym_RetirementState::Unproven)
        { Request_Fail("SceneNode retirement proof rejected"); return; }
        if (State == ECk_SceneNodeGym_RetirementState::Pending) { return; }
        if (State != ECk_SceneNodeGym_RetirementState::Complete ||
            Snapshot.Lifecycle != ECkSceneNodeGym_Lifecycle::Retiring ||
            Snapshot.Generation <= 0 || Snapshot.Generation >= 2147483647 ||
            Snapshot.Sequence >= 9223372036854775806)
        { Request_Fail("SceneNode completed generation changed"); return; }

        FCkSceneNodeGym_Snapshot Initializing;
        Initializing.Generation = Snapshot.Generation + 1;
        Initializing.Sequence = Snapshot.Sequence + 1;
        Initializing.Lifecycle = ECkSceneNodeGym_Lifecycle::Initializing;
        auto Classes = Get_SourceVisualClasses();
        if (Classes.Num() != CkSceneNodeGymShared::VisualCount ||
            !CkSceneNodeGymShared::Get_IsValidPublicationAdvance(Snapshot, Initializing) ||
            !Get_HasWorldIdentity())
        { Request_Fail("SceneNode successor publication rejected"); return; }
        Snapshot = Initializing;
        ForceNetUpdate();
        _Starting = true;
        _StartedAt = float(System::GetGameTimeInSeconds());
        _ActivationIssued = false;
        _LastPublishedAt = 0.0f;
        // No script operation reads the historical ActorLedger after native
        // TryBeginSuccessor destroys that component. The Complete receipt is
        // retained through the call and remains historical afterward.
        _Actors = nullptr;
        auto CompletedReceipt = _Retirement;
        auto Next = UCk_SceneNodeGym_RetirementRuntime_UE::TryBeginSuccessor(
            this, CompletedReceipt, Classes);
        _Actors = Next;
        // The returned ledger is retained before checking callbacks that may
        // have failed or replaced the intended source during native creation.
        if (_Failed || _Ending || !_RetiringShared || !_Starting ||
            !Get_HasWorldIdentity() || _Retirement != CompletedReceipt ||
            !CkSceneNodeGymShared::Get_IsExactSnapshotValue(Snapshot, Initializing) ||
            ck::Is_NOT_Valid(Next) ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Next))
        {
            if (ck::IsValid(Next))
            { UCk_SceneNodeGym_ActorLedgerRuntime_UE::Request_Close(Next); }
            Request_Fail("SceneNode successor claim rejected"); return;
        }
        _Roots = nullptr;
        _Graph = nullptr;
        _Motion = nullptr;
        _Sampler = nullptr;
        _RetiringShared = false;
        StartActorsAndRoots(Initializing.Generation);
    }

    private void Request_PublishActiveFrame()
    {
        if (!Get_HasWorldIdentity() ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(_Actors) ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(_Actors, true) ||
            !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(_Motion))
        { Request_Fail("SceneNode active source changed"); return; }
        FCkSceneNodeGym_Frame Frame;
        FCkSceneNodeGym_SourceDisplayData SourceDisplay;
        if (!UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(_Sampler, Frame) ||
            !UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestSourceDisplayData(
                _Sampler, SourceDisplay))
        {
            if (!_Starting && float(System::GetGameTimeInSeconds()) - _LastPublishedAt > 2.0f)
            { Request_Fail("SceneNode current source frame expired"); }
            return;
        }
        if (Frame.Generation != Snapshot.Generation || Frame.FrameSerial <= 0 ||
            (Snapshot.Lifecycle == ECkSceneNodeGym_Lifecycle::Active &&
             Frame.FrameSerial <= Snapshot.FrameSerial)) { return; }
        TArray<FString> Titles;
        TArray<FString> Bodies;
        TArray<FString> Instructions;
        if (!CkSceneNodeGymShared::TryFormatDisplayRows(
                Frame, SourceDisplay, Titles, Bodies, Instructions))
        { Request_Fail("SceneNode frame display receipt rejected"); return; }
        if (Snapshot.Sequence >= 9223372036854775806)
        { Request_Fail("SceneNode publication sequence exhausted"); return; }
        FCkSceneNodeGym_Snapshot Candidate;
        Candidate.Generation = Snapshot.Generation;
        Candidate.Sequence = Snapshot.Sequence + 1;
        Candidate.Lifecycle = ECkSceneNodeGym_Lifecycle::Active;
        Candidate.FrameSerial = Frame.FrameSerial;

        Candidate.BaseTitles = Titles;
        Candidate.Bodies = Bodies;
        Candidate.Instructions = Instructions;
        if (!CkSceneNodeGymShared::Get_IsValidSnapshot(Candidate) ||
            !CkSceneNodeGymShared::Get_IsValidPublicationAdvance(Snapshot, Candidate) ||
            !Get_HasWorldIdentity())
        { Request_Fail("SceneNode whole frame publication rejected"); return; }
        Snapshot = Candidate;
        for (int32 Role = 1; Role <= CkSceneNodeGymShared::VisualCount; ++Role)
        {
            AActor Visual;
            if (UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(_Actors, Role, Visual))
            { UCk_SceneNodeGym_RemoteVisualRuntime_UE::Request_SetVisualVisible(Visual, true); }
        }
        _Starting = false;
        _LastPublishedAt = float(System::GetGameTimeInSeconds());
        ForceNetUpdate();
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (_Ending) { return; }
        if (!Math::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f) { return; }
        if (!HasAuthority()) { AdvanceClientProjection(); return; }
        if (_Failed) { return; }
        if (!Math::IsFinite(_MonitorElapsed) ||
            _MonitorElapsed < 0.0f || _MonitorElapsed > 0.05f)
        { Request_Fail("SceneNode source monitor clock rejected"); return; }
        if (!_Armed) { Request_EnsureSharedDemo(); }
        if (!_Armed) { return; }
        // A long frame advances one bounded monitor pass without overflowing.
        _MonitorElapsed += DeltaSeconds > 0.05f ? 0.05f : DeltaSeconds;
        if (_MonitorElapsed < 0.05f) { return; }
        _MonitorElapsed = 0.0f;
        AdvanceAuthority();
    }

    UFUNCTION(BlueprintCallable)
    void AdvanceAuthority()
    {
        if (_Ending || _Failed || !HasAuthority()) { return; }
        if (!_Armed) { Request_EnsureSharedDemo(); }
        if (!_Armed) { return; }
        if (!Get_HasWorldIdentity())
        { Request_Fail("SceneNode source world/catalog changed"); return; }
        if (Snapshot.Generation == 0) { StartInitialGeneration(); return; }
        if (_RetiringShared) { AdvanceSharedRetirement(); return; }
        if (_Starting && float(System::GetGameTimeInSeconds()) - _StartedAt > 20.0f)
        { Request_Fail("SceneNode source startup timed out"); return; }
        if (ck::Is_NOT_Valid(_Actors) ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(_Actors))
        { Request_Fail("SceneNode actor cohort lost"); return; }
        if (ck::Is_NOT_Valid(_Roots) ||
            UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsFailed(_Roots) ||
            UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsUnproven(_Roots))
        { Request_Fail("SceneNode root receipt lost"); return; }
        if (!UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(_Roots))
        { return; }
        if (ck::Is_NOT_Valid(_Graph))
        {
            _Graph = UCk_SceneNodeGym_NodeGraphRuntime_UE::TryBegin(_Roots);
            if (ck::Is_NOT_Valid(_Graph) ||
                UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsFailed(_Graph) ||
                UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsUnproven(_Graph))
            { Request_Fail("SceneNode graph admission rejected"); }
            return;
        }
        if (UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsFailed(_Graph) ||
            UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsUnproven(_Graph))
        { Request_Fail("SceneNode graph receipt lost"); return; }
        if (ck::Is_NOT_Valid(_Motion))
        {
            if (!UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(_Graph))
            { return; }
            _Motion = UCk_SceneNodeGym_MotionRuntime_UE::TryBeginDormant(_Graph);
            if (ck::Is_NOT_Valid(_Motion) ||
                UCk_SceneNodeGym_MotionRuntime_UE::Get_IsFailed(_Motion) ||
                UCk_SceneNodeGym_MotionRuntime_UE::Get_IsUnproven(_Motion))
            { Request_Fail("SceneNode dormant timer admission rejected"); }
            return;
        }
        if (UCk_SceneNodeGym_MotionRuntime_UE::Get_IsFailed(_Motion) ||
            UCk_SceneNodeGym_MotionRuntime_UE::Get_IsUnproven(_Motion))
        { Request_Fail("SceneNode motion receipt lost"); return; }
        if (!_ActivationIssued)
        {
            if (!UCk_SceneNodeGym_MotionRuntime_UE::Get_IsDormantReady(_Motion))
            { return; }
            _ActivationIssued = true; // Never retry a partially issued Resume batch.
            if (!UCk_SceneNodeGym_MotionRuntime_UE::TryActivateOnce(_Motion))
            { Request_Fail("SceneNode six producer activation rejected"); }
            return;
        }
        if (!UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(_Motion))
        {
            if (!_Starting) { Request_Fail("SceneNode active producer lost"); }
            return;
        }
        if (ck::Is_NOT_Valid(_Sampler))
        {
            _Sampler = UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryBegin(_Motion);
            if (ck::Is_NOT_Valid(_Sampler))
            { Request_Fail("SceneNode source frame sampler rejected"); }
            return;
        }
        Request_PublishActiveFrame();
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        if (!HasAuthority()) { Request_FailProjection(); return; }
        Request_CloseKnownProducers();
    }

    UPROPERTY() private UCk_SceneNodeGym_RemoteVisuals_UE _RemoteVisuals;
    UPROPERTY() private FCkSceneNodeGym_Snapshot _ReceivedSample;
    UPROPERTY() private FCkSceneNodeGym_Snapshot _InstalledSample;
    private bool _ProjectionFailed = false;
    private bool _Projecting = false;
    private bool _RemoteAttempted = false;
    private bool _RemoteRetiring = false;
    private float _RemoteRetireStartedAt = 0.0f;

    private bool Get_HasClientIdentity()
    {
        if (_Ending || HasAuthority() || IsActorBeingDestroyed() ||
            GetClass() != ACk_SceneNodeGym_GameState) { return false; }
        auto World = GetWorld();
        if (!ck::IsValid(World) || World.GetNetMode() != ENetMode::NM_Client ||
            World.GetGameState() != this || !Get_HasPublishedCatalog()) { return false; }
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        int32 Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Registry.Num()) { return false; }
        auto Entry = Registry[Index];
        if (Entry.DisplayName != "Scene Node" || Entry.GameModeClass != ACk_SceneNodeGym_GameMode)
        { return false; }
        return _CatalogIndex < 0 || (_CatalogIndex == Index &&
            _CatalogEntry.DisplayName == Entry.DisplayName &&
            _CatalogEntry.GameModeClass == Entry.GameModeClass &&
            _CatalogEntry.LevelName == Entry.LevelName && _CatalogEntry.Category == Entry.Category &&
            _CatalogEntry.HintCode == Entry.HintCode);
    }

    private FCkSceneNodeGym_Frame Get_Frame(FCkSceneNodeGym_Snapshot Sample)
    {
        FCkSceneNodeGym_Frame Frame;
        if (!UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryMakeFrame(
            Sample.Generation, Sample.FrameSerial, TArray<FTransform>(), Frame))
        { return Frame; } // The factory cleared the failed output.
        return Frame;
    }

    private TArray<TSubclassOf<AActor>> Get_RemoteClasses()
    {
        auto Classes = TArray<TSubclassOf<AActor>>();
        for (int32 Role = 1; Role <= CkSceneNodeGymShared::VisualCount; ++Role)
        {
            if (Get_IsParentRole(Role)) { Classes.Add(ACk_SceneNodeGym_SourceParent); }
            else { Classes.Add(ACk_SceneNodeGym_SourceChild); }
        }
        return Classes;
    }

    private void Request_FailProjection()
    {
        _ProjectionFailed = true;
        _InstalledSample = FCkSceneNodeGym_Snapshot();
        if (ck::IsValid(_RemoteVisuals) && !_RemoteRetiring)
        {
            _RemoteRetiring = true;
            _RemoteRetireStartedAt = float(System::GetGameTimeInSeconds());
            UCk_SceneNodeGym_RemoteVisualRuntime_UE::Request_Retire(_RemoteVisuals);
        }
    }

    // One client-world projection, shared by every local owner's overlay.
    // The received receipt advances independently of a fully installed frame.
    UFUNCTION(BlueprintCallable)
    void AdvanceClientProjection()
    {
        if (_Ending || _ProjectionFailed || HasAuthority() || _Projecting) { return; }
        if (!Get_HasClientIdentity())
        {
            if (_ReceivedSample.Generation > 0 || _RemoteAttempted)
            { Request_FailProjection(); }
            return;
        }
        if (!CkSceneNodeGymShared::Get_IsValidSnapshot(Snapshot))
        { Request_FailProjection(); return; }
        if (Snapshot.Generation == 0)
        {
            if (_ReceivedSample.Generation > 0) { Request_FailProjection(); }
            return;
        }
        if (_CatalogIndex < 0)
        {
            _CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
            _CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[_CatalogIndex];
        }
        if (Snapshot.Sequence == _ReceivedSample.Sequence)
        {
            if (!CkSceneNodeGymShared::Get_IsExactSnapshotValue(Snapshot, _ReceivedSample))
            { Request_FailProjection(); return; }
        }
        else
        {
            if (!CkSceneNodeGymShared::Get_IsValidReceivedAdvance(_ReceivedSample, Snapshot))
            { Request_FailProjection(); return; }
            _ReceivedSample = Snapshot;
        }
        // Freeze the input before actor construction, transform, or destroy callbacks.
        auto Sample = _ReceivedSample;
        bool Active = Sample.Lifecycle == ECkSceneNodeGym_Lifecycle::Active;
        if (!Active || (ck::IsValid(_RemoteVisuals) &&
            UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_Generation(_RemoteVisuals) != Sample.Generation))
        {
            _InstalledSample = FCkSceneNodeGym_Snapshot();
            if (ck::IsValid(_RemoteVisuals) && !_RemoteRetiring)
            {
                _RemoteRetiring = true;
                _RemoteRetireStartedAt = float(System::GetGameTimeInSeconds());
                _Projecting = true;
                bool Retiring = UCk_SceneNodeGym_RemoteVisualRuntime_UE::Request_Retire(_RemoteVisuals);
                _Projecting = false;
                if (_Ending || !Get_HasClientIdentity() || !Retiring)
                { Request_FailProjection(); return; }
            }
        }
        if (ck::IsValid(_RemoteVisuals) && _RemoteRetiring)
        {
            auto State = UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_State(_RemoteVisuals);
            if (State == ECk_SceneNodeGym_RemoteVisualState::Unproven ||
                float(System::GetGameTimeInSeconds()) - _RemoteRetireStartedAt > 25.0f)
            { Request_FailProjection(); return; }
            if (State != ECk_SceneNodeGym_RemoteVisualState::Complete) { return; }
        }
        if (!Active) { return; }
        auto Frame = Get_Frame(Sample);
        if (!ck::IsValid(_RemoteVisuals))
        {
            if (_RemoteAttempted) { Request_FailProjection(); return; }

            _Projecting = true;
            _RemoteVisuals = UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryBegin(
                this, Sample.Sequence, Frame, Get_RemoteClasses());
            _Projecting = false;
            if (!ck::IsValid(_RemoteVisuals)) { return; }
            _RemoteAttempted = true;
        }
        else if (_RemoteRetiring)
        {
            // Same-generation Active after Retiring is rejected by receipt validation.
            _Projecting = true;
            auto Next = UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryBeginSuccessor(
                this, _RemoteVisuals, Sample.Sequence, Frame, Get_RemoteClasses());
            if (ck::IsValid(Next)) { _RemoteVisuals = Next; }
            _Projecting = false;
            if (!ck::IsValid(Next)) { return; }
            _RemoteRetiring = false;
        }
        else if (_InstalledSample.Sequence != Sample.Sequence)
        {
            _InstalledSample = FCkSceneNodeGym_Snapshot();
            _Projecting = true;
            bool Installed = UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryInstall(
                _RemoteVisuals, Sample.Sequence, Frame);
            _Projecting = false;
            if (!Installed)
            {
                if (UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_State(_RemoteVisuals) ==
                    ECk_SceneNodeGym_RemoteVisualState::Unproven) { Request_FailProjection(); }
                return;
            }
        }
        if (_Ending || _ProjectionFailed || !Get_HasClientIdentity() ||
            !ck::IsValid(_RemoteVisuals) ||
            !UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_IsInstalledConformant(
                _RemoteVisuals, Sample.Sequence, Frame))
        { return; }
        _InstalledSample = Sample;
    }

    UFUNCTION(BlueprintCallable)
    bool TryGet_PresentationSample(FCkSceneNodeGym_Snapshot&out OutSample)
    {
        OutSample = FCkSceneNodeGym_Snapshot();
        if (HasAuthority())
        {
            if (_Failed || _Ending || !Get_HasWorldIdentity() ||
                Snapshot.Lifecycle != ECkSceneNodeGym_Lifecycle::Active ||
                !CkSceneNodeGymShared::Get_IsValidSnapshot(Snapshot) ||
                !ck::IsValid(_Actors) || !ck::IsValid(_Motion) ||
                !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(_Actors) ||
                !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(_Actors, true) ||
                !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(_Motion)) { return false; }
            OutSample = Snapshot;
            return true;
        }
        if (_ProjectionFailed || _Ending || _Projecting || !Get_HasClientIdentity() ||
            _InstalledSample.Lifecycle != ECkSceneNodeGym_Lifecycle::Active ||
            !CkSceneNodeGymShared::Get_IsValidSnapshot(_InstalledSample) ||
            !CkSceneNodeGymShared::Get_IsExactSnapshotValue(Snapshot, _InstalledSample) ||
            !ck::IsValid(_RemoteVisuals) ||
            !UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_IsInstalledConformant(
                _RemoteVisuals, _InstalledSample.Sequence, Get_Frame(_InstalledSample)))
        { return false; }
        OutSample = _InstalledSample;
        return true;
    }

}
