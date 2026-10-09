class ACk_Gym_Base_PlayerController : ACkGym_PlayerController_UE
{

    // Z offset applied to every station's default grid placement. Override
    // in subclasses whose world floor isn't at Z=0 - the GymStation's alcove
    // floor sits at Z=FloorThickness/2 above the station origin, so without
    // adjustment that floor protrudes above any world floor whose top is at
    // Z=0. Negative values pull the alcove down. See ACk_NavigationGym_PlayerController.
    UPROPERTY()
    float DefaultStationGridZ = 0.0f;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (_StationStartupFailed || _StationStartupPending || _ControllerEntityAdmitted) { return; }
        auto ControllerWorld = GetWorld();
        if (ck::Is_NOT_Valid(ControllerWorld))
        { Request_AbortStationStartup("Controller world is unavailable"); return; }
        _StationStartupPending = true;
        System::SetTimer(this, n"OnStationStartupTimeout", 20.0f, false);
        if (ControllerWorld.GetNetMode() == ENetMode::NM_Client)
        {
            // The replicated association may have completed before this actor's
            // BeginPlay. Wait on its durable link/replication readiness instead
            // of creating a pending spawn after the construction signal expired.
            utils_owning_actor::Promise_OnActorEcsReady(this,
                FCk_Delegate_OwningActor_OnEcsReady(this, n"OnControllerActorEcsReady"),
                ECk_ActorEcsReady_Policy::ValuesReplicated);
            return;
        }
        auto SpawnParams = FCk_EntityScript_WithActor_SpawnParams();
        SpawnParams._OwningActor = this;
        auto LifetimeOwner = ck::TransientEntity();
        if (ck::Is_NOT_Valid(LifetimeOwner) || !utils_entity_lifetime::Get_CanCreateEntity(LifetimeOwner))
        { Request_AbortStationStartup("Controller lifetime owner is unavailable"); return; }
        auto PendingEntity = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
            LifetimeOwner, UCk_EntityScript_WithActor_UE, FInstancedStruct::Make(SpawnParams));
        if (_StationStartupFailed || !utils_pending_entity_script::Get_IsValid(PendingEntity))
        {
            Request_AbortStationStartup("Controller entity spawn was rejected");
            return;
        }
        utils_pending_entity_script::Promise_OnConstructed(
            PendingEntity, FCk_Delegate_EntityScript_Constructed(this, n"OnEntityConstructed"));
    }

    private int32 _PendingStationCount = 0;
    private bool _StationStartupFailed = false;
    private bool _StationStartupPending = false;
    private bool _ControllerEntityAdmitted = false;
    private FCk_Handle _StationAdmissionOwner;
    private int32 _StationAdmissionTicket = 0;
    private TArray<FCk_Handle> _ConstructedStations;
    private TArray<FCk_Handle> _ObservedStationReturns;
    private TArray<FCk_Handle> _ReusedStations;
    private TArray<FCkGym_Station_SpawnParams_Payload> _StartupStations;

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (_StationStartupPending)
        { Request_AbortStationStartup("Controller left during station startup", false); }
    }

    private void Request_AbortStationStartup(FString InReason, bool InReportFailure = true)
    {
        if (_StationStartupFailed)
        { return; }
        _StationStartupFailed = true;
        _StationStartupPending = false;
        _PendingStationCount = 0;
        // Keep the raw owner and creator returns: Request_SpawnEntity can reject
        // after allocation without returning every identity to this controller.
        if (_StationAdmissionTicket > 0)
        {
            auto World = ck::TransientEntity();
            if (!CkGym_Admission::TryCloseUnproven(
                World, _StationAdmissionTicket, _StationAdmissionOwner))
            {
                // A changed ticket cannot authorize releasing or destroying
                // another owner. The valid world still stays terminal.
                CkGym_Admission::TryCloseWorldUnproven(World);
            }
        }
        for (auto Station : _ObservedStationReturns)
        { Request_CloseKnownStationPresentation(Station); }
        auto Outcome = _StationAdmissionTicket > 0
            ? "; station admission blocked until world restart"
            : "; station startup failed before admission";
        if (InReportFailure) { ck::Error("[Gym] " + InReason + Outcome); }
        else { ck::Trace("[Gym] " + InReason + Outcome); }
    }

    UFUNCTION()
    private void OnStationStartupTimeout()
    {
        if (_StationStartupPending)
        { Request_AbortStationStartup("Station construction did not finish before the deadline"); }
    }

    private bool Request_ReleaseStationAdmission()
    {
        if (_StationAdmissionTicket <= 0) { return false; }
        if (CkGym_Admission::TryReleasePublished(
            ck::TransientEntity(), _StationAdmissionTicket, _StationAdmissionOwner))
        { _StationAdmissionTicket = 0; return true; }
        return false;
    }

    // A completed station may arrive after a different creator failed. Close
    // only a known, exact script beneath this controller's retained owner.
    private void Request_CloseKnownStationPresentation(FCk_Handle InStation)
    {
        if (ck::Is_NOT_Valid(InStation) || ck::Is_NOT_Valid(_StationAdmissionOwner) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                InStation, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !utils_entity_lifetime::Has_LifetimeOwner(InStation) ||
            utils_entity_lifetime::Get_LifetimeOwner(InStation) != _StationAdmissionOwner)
        { return; }
        auto Script = Cast<UCk_EntityScript_GymStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(InStation));
        if (ck::Is_NOT_Valid(Script) ||
            UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(Script) != InStation)
        { return; }
        Script.Request_CloseBaselinePresentation();
    }

    UFUNCTION()
    private void OnStationAdmissionAvailable(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (_StationStartupPending && !_StationStartupFailed)
        { Request_EnsureStationsExist(); }
    }

    UFUNCTION()
    private void OnStationSpawnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (InResult != ECk_Request_OperationResult::Succeeded)
        { Request_AbortStationStartup("Station entity spawn failed"); }
    }

    // The PC's own ECS entity (the WithActor entity spawned in BeginPlay). Hosts
    // the deferred-wait timer used by WaitOneFrame.
    private FCk_Handle _SelfEntity;

    UFUNCTION()
    void OnEntityConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        Request_AdmitControllerEntity(FCk_Handle(InEntityScriptHandle));
    }

    UFUNCTION()
    private void OnControllerActorEcsReady(AActor InActor, FCk_Handle InEntity)
    {
        if (_StationStartupFailed || !_StationStartupPending || _ControllerEntityAdmitted) { return; }
        if (InActor != this)
        { Request_AbortStationStartup("Controller readiness belongs to another actor"); return; }
        Request_AdmitControllerEntity(InEntity);
    }

    private void Request_AdmitControllerEntity(FCk_Handle InEntity)
    {
        if (_StationStartupFailed || !_StationStartupPending || _ControllerEntityAdmitted) { return; }
        if (ck::Is_NOT_Valid(InEntity) || utils_entity_lifetime::Get_IsPendingDestroy(
            InEntity, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
        { Request_AbortStationStartup("Controller entity construction failed"); return; }
        auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(InEntity);
        auto WithActor = Cast<UCk_EntityScript_WithActor_UE>(Script);
        if (ck::Is_NOT_Valid(WithActor) || ck::ToEntity(WithActor) != InEntity ||
            utils_owning_actor::TryGet_EntityOwningActor(InEntity) != this)
        { Request_AbortStationStartup("Controller entity completion has no matching actor script"); return; }
        auto ControllerWorld = GetWorld();
        if (ck::Is_NOT_Valid(ControllerWorld) ||
            utils_entity_lifetime::Get_WorldForEntity(InEntity) != ControllerWorld)
        { Request_AbortStationStartup("Controller entity completion belongs to another world"); return; }
        if (ControllerWorld.GetNetMode() == ENetMode::NM_Client &&
            (!InEntity.Has_NetInfo() || InEntity.Get_Replication() != ECk_Replication::Replicates))
        { Request_AbortStationStartup("Client controller entity is not replicated"); return; }
        _SelfEntity = InEntity;
        _ControllerEntityAdmitted = true;
        Request_EnsureStationsExist();
    }

    UFUNCTION()
    void OnStationEntityConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        auto ReturnedStation = FCk_Handle(InEntityScriptHandle);
        if (ReturnedStation == FCk_Handle() || !_ObservedStationReturns.Contains(ReturnedStation))
        {
            if (!_StationStartupFailed)
            { Request_AbortStationStartup("Station callback has no retained creator return"); }
            return;
        }
        if (_StationStartupFailed)
        {
            Request_CloseKnownStationPresentation(ReturnedStation);
            return;
        }
        if (!_StationStartupPending || _PendingStationCount <= 0) { return; }
        if (ck::Is_NOT_Valid(InEntityScriptHandle))
        {
            Request_AbortStationStartup("Station construction failed");
            return;
        }
        auto Station = FCk_Handle(InEntityScriptHandle);
        if (ck::Is_NOT_Valid(_StationAdmissionOwner) ||
            !utils_entity_lifetime::Has_LifetimeOwner(Station) ||
            utils_entity_lifetime::Get_LifetimeOwner(Station) != _StationAdmissionOwner)
        {
            Request_AbortStationStartup("Station completion belongs to a different admission hierarchy");
            return;
        }
        auto StationScript = Cast<UCk_EntityScript_GymStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(Station));
        if (ck::Is_NOT_Valid(StationScript) ||
            UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(StationScript) != Station)
        { Request_AbortStationStartup("Station callback has no exact station script"); return; }
        if (_ConstructedStations.Contains(Station))
        { return; }
        _ConstructedStations.Add(Station);
        _PendingStationCount--;
        if (_PendingStationCount <= 0)
        {
            // Stations tag themselves via utils_entity_tag::Add inside DoConstruct,
            // which is DEFERRED by one processor pass (see the CkEntityTag docs
            // "Timing"). Request_StartGym / Get_StationHandle query that tag store,
            // so starting in this same construction pass finds zero stations. Wait
            // one frame for the deferred Adds to settle before starting the gym.
            WaitOneFrame(n"OnStationsSettled");
        }
    }

    UFUNCTION()
    private void OnStationsSettled(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (_StationStartupFailed)
        { return; }
        if (_PendingStationCount != 0)
        { return; }
        for (auto Station : _StartupStations)
        {
            if (!Request_StationExists(Station.Tags))
            {
                Request_AbortStationStartup("Station tags did not settle as a complete unique station set");
                return;
            }
            auto Matches = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), Station.Tags[0]);
            if (!_ConstructedStations.Contains(Matches[0]) && !_ReusedStations.Contains(Matches[0]))
            {
                Request_AbortStationStartup("Settled station differs from its admitted completion");
                return;
            }
        }
        if (!Request_ReleaseStationAdmission())
        { Request_AbortStationStartup("Complete station publication lost its admission"); return; }
        _StationStartupPending = false;
        _ConstructedStations.Empty();
        _ObservedStationReturns.Empty();
        _ReusedStations.Empty();
        _StartupStations.Empty();
        Request_StartGym();
    }

    // Mirrors CkAutoTest_Base::WaitOneFrame - schedules a one-frame timer on the
    // PC's own entity and invokes InCallbackName (FCk_Delegate_Timer signature)
    // once it fires. Use to observe state mutated through a deferred pathway
    // (e.g. deferred CkEntityTag Adds) on a subsequent frame.
    private void WaitOneFrame(FName InCallbackName)
    {
        auto Params = FCk_Timer_Spec(FCk_Time(0.05));
        Params.Set_StartingState(ECk_Timer_State::Running)
              .Set_Behavior(ECk_Timer_Behavior::StopOnDone);
        auto Timer = utils_timer::Add(_SelfEntity, Params);
        Timer.BindTo_OnDone(FCk_Delegate_Timer(this, InCallbackName));
    }

    // Override in derived classes to define required stations
    // Stations will be spawned in a default grid layout unless transforms are explicitly set
    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations()
    {
        return TArray<FCkGym_Station_SpawnParams_Payload>();
    }

    // Shared stations live for the world. Personal input/device gyms may scope their station
    // hierarchy to this controller and use distinct tags for each local player.
    FCk_Handle Get_StationLifetimeOwner()
    {
        return ck::TransientEntity();
    }

    FString Get_StationTag(FString InTag)
    {
        return InTag;
    }

    //--------------------------------------------------------------------------------------------------------------------------
    // Control panel - the on-screen list of this gym's controls
    //
    // Declared here rather than per-gym so ACkGym_ControlPanelHUD (the default HUDClass) can drive ANY
    // gym without knowing what it is. A gym's console commands are invisible until they appear here.
    // Authoring guide and row builders: Script/Common/CkGym_ControlPanel.as.
    //--------------------------------------------------------------------------------------------------------------------------

    // Rebuilt every frame, so the value column reports live state rather than a snapshot. Returning an
    // empty list - the default - draws no panel at all, which is why this costs nothing to the gyms that
    // have not adopted it.
    TArray<FCkGym_ControlRow> Get_ControlRows()
    {
        return TArray<FCkGym_ControlRow>();
    }

    // InRowIndex indexes the array Get_ControlRows() returned this frame. Header and Status rows hold no
    // key and never arrive here, but they DO occupy an index - build the list in one place and branch on
    // the index there, rather than counting rows by hand in two places that then drift apart.
    void Request_ControlActivated(int32 InRowIndex)
    {
    }

    FString Get_ControlPanelTitle()
    {
        return "GYM CONTROLS";
    }

    void Request_EnsureStationsExist()
    {
        if (_StationStartupFailed || !_StationStartupPending || ck::IsValid(_StationAdmissionOwner))
        { return; }
        if (ck::Is_NOT_Valid(_SelfEntity))
        {
            Request_AbortStationStartup("Controller entity is unavailable for station admission");
            return;
        }
        auto RequiredStations = Get_RequiredStations();
        if (RequiredStations.Num() == 0)
        {
            // No stations to spawn - proceed straight to gym startup.
            _StationStartupPending = false;
            Request_StartGym();
            return;
        }
        TArray<FName> DeclaredTags;
        for (auto Station : RequiredStations)
        {
            if (Station.Tags.Num() == 0)
            {
                Request_AbortStationStartup("A required station has no identity tags");
                return;
            }
            for (auto Tag : Station.Tags)
            {
                if (Tag == FName() || DeclaredTags.Contains(Tag))
                {
                    Request_AbortStationStartup("Required station tags are empty or duplicated");
                    return;
                }
                DeclaredTags.Add(Tag);
            }
        }

        auto WorldOwner = ck::TransientEntity();
        if (ck::Is_NOT_Valid(WorldOwner))
        {
            Request_AbortStationStartup("World station admission owner is unavailable");
            return;
        }
        if (CkGym_Admission::Get_IsClosedUnproven(WorldOwner))
        {
            Request_AbortStationStartup("Prior station construction is unproven", false);
            return;
        }
        if (CkGym_Admission::Get_IsBusy(WorldOwner))
        {
            WaitOneFrame(n"OnStationAdmissionAvailable");
            return;
        }

        // Apply default grid layout if transforms not explicitly set.
        Request_ApplyDefaultGridLayout(RequiredStations);
        _StartupStations = RequiredStations;

        _PendingStationCount = 0;
        _StationAdmissionTicket = CkGym_Admission::TryReserve(WorldOwner);
        if (_StationAdmissionTicket <= 0)
        { Request_AbortStationStartup("Station admission reservation was rejected"); return; }

        auto LifetimeOwner = Get_StationLifetimeOwner();
        if (ck::Is_NOT_Valid(LifetimeOwner) ||
            !utils_entity_lifetime::Get_CanCreateEntity(LifetimeOwner))
        {
            Request_AbortStationStartup("Station lifetime owner is unavailable");
            return;
        }
        _StationAdmissionOwner = utils_entity_lifetime::Request_CreateEntity(LifetimeOwner);
        if (ck::Is_NOT_Valid(_StationAdmissionOwner))
        {
            Request_AbortStationStartup("Station admission hierarchy could not be created");
            return;
        }
        if (!CkGym_Admission::TryCommitOwner(
            WorldOwner, _StationAdmissionTicket, _StationAdmissionOwner))
        { Request_AbortStationStartup("Station admission owner was rejected"); return; }

        for (auto StationParams : RequiredStations)
        {
            if (Request_StationExists(StationParams.Tags))
            {
                auto Matches = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), StationParams.Tags[0]);
                _ReusedStations.Add(Matches[0]);
                continue;
            }

            _PendingStationCount++;
            auto Pending = CkGym_Common::Request_SpawnNewStationForOwner(
                StationParams, _StationAdmissionOwner,
                FCk_Delegate_Request_OnCompleted(this, n"OnStationSpawnCompleted"));
            const bool bPendingValid = utils_pending_entity_script::Get_IsValid(Pending);
            if (bPendingValid)
            {
                auto ReturnedTarget = Pending.Get_EntityUnderConstruction();
                if (ReturnedTarget == FCk_Handle())
                { Request_AbortStationStartup("Pending station has no creator identity"); }
                else if (!_ObservedStationReturns.Contains(ReturnedTarget))
                { _ObservedStationReturns.Add(ReturnedTarget); }
                if (_StationStartupFailed)
                { Request_CloseKnownStationPresentation(ReturnedTarget); }
            }
            if (_StationStartupFailed || !bPendingValid)
            {
                Request_AbortStationStartup("Station construction was rejected");
                return;
            }
            utils_pending_entity_script::Promise_OnConstructed(
                Pending, FCk_Delegate_EntityScript_Constructed(this, n"OnStationEntityConstructed"));
        }

        if (_PendingStationCount == 0)
        {
            // All stations already existed - nothing to await.
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                    _StationAdmissionOwner, WorldOwner, Children) || Children.Num() != 0)
            { Request_AbortStationStartup("Empty admission owner had unproven children"); return; }
            if (!Request_ReleaseStationAdmission())
            { Request_AbortStationStartup("Complete reused stations lost their admission"); return; }
            _StationStartupPending = false;
            utils_entity_lifetime::Request_DestroyEntity(_StationAdmissionOwner);
            _StationAdmissionOwner = FCk_Handle();
            _StartupStations.Empty();
            _ReusedStations.Empty();
            Request_StartGym();
        }
    }

    void Request_ApplyDefaultGridLayout(TArray<FCkGym_Station_SpawnParams_Payload>& InOutStations)
    {
        auto StationSpacing = 800.0f;
        auto RowSpacing = 800.0f;
        auto MaxStationsPerRow = 7;
        auto Rotation180 = FRotator(0.0f, 180.0f, 0.0f);
        auto BaseXOffset = 500.0f;

        for (int32 i = 0; i < InOutStations.Num(); i++)
        {
            // Only apply default layout if transform is identity (not explicitly set)
            if (InOutStations[i].Transform.Equals(FTransform::Identity))
            {
                auto RowIndex = Math::IntegerDivisionTrunc(i, MaxStationsPerRow);
                auto ColIndex = i % MaxStationsPerRow;
                auto StationsInRow = Math::Min(MaxStationsPerRow, InOutStations.Num() - (RowIndex * MaxStationsPerRow));
                auto RowStartOffset = -(StationsInRow - 1) * StationSpacing * 0.5f;

                auto DefaultTransform = FTransform::Identity;
                auto XPos = BaseXOffset + (RowIndex * RowSpacing);
                auto YPos = RowStartOffset + (ColIndex * StationSpacing);
                DefaultTransform.SetLocation(FVector(XPos, YPos, DefaultStationGridZ));
                DefaultTransform.SetRotation(Rotation180.Quaternion());
                InOutStations[i].Transform = DefaultTransform;
            }
        }
    }

    bool Request_StationExists(TArray<FName> InTags)
    {
        if (InTags.Num() == 0)
        {
            return false;
        }

        // Every declared tag must identify the same complete station.
        FCk_Handle Existing;
        for (auto Tag : InTags)
        {
            auto Entities = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), Tag);
            if (Entities.Num() != 1 || ck::Is_NOT_Valid(Entities[0]) ||
                !Entities[0].Has_Fragment(FCkGym_Station_Anchors))
            { return false; }
            if (ck::IsValid(Existing) && Existing != Entities[0])
            { return false; }
            Existing = Entities[0];
        }

        return ck::IsValid(Existing);
    }

    // Override this in derived gym classes to implement gym-specific startup logic
    void Request_StartGym()
    {
        // Base class: open the gym switchboard
        auto Switchboard = UCkGym_Switchboard_Subsystem::Get(this);
        if (ck::IsValid(Switchboard))
        {
            Switchboard.Request_Open();
        }
    }

    // Look up the station entity tagged with InStationTag. Stations are
    // pure ECS entities now (UCk_EntityScript_GymStation) - no underlying
    // AActor - so all station lookups go through utils_entity_tag.
    FCk_Handle Get_StationHandle(FString InStationTag)
    {
        auto Entities = utils_entity_tag::ForEach_Entity(
            ck::TransientEntity(), FName(Get_StationTag(InStationTag)));
        if (Entities.Num() == 0)
        {
            ck::Warning("Cannot get station handle - station entity not found: " + InStationTag);
            return FCk_Handle();
        }
        return Entities[0];
    }

    FTransform Get_StationTransform(FString InStationTag)
    {
        auto Handle = Get_StationHandle(InStationTag);
        if (!ck::IsValid(Handle))
        {
            return FTransform::Identity;
        }

        auto TH = Handle.As_Transform();
        if (!ck::IsValid(TH))
        {
            ck::Warning("Station [" + InStationTag + "] entity has no Transform fragment");
            return FTransform::Identity;
        }

        return utils_transform::Get_EntityCurrentTransform(TH);
    }

    void Set_StationTitleAndDescription(FString InStationTag, FCkGym_Station_TitleAndDescription InTextAndDescription)
    {
        auto Handle = Get_StationHandle(InStationTag);
        if (!ck::IsValid(Handle))
        {
            ck::Warning("Cannot set station text - station entity not found: " + InStationTag);
            return;
        }
        auto& Fragment = Handle.AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
        Fragment = InTextAndDescription;
    }

    // Cross-gym anchor lookup. Reads the FCkGym_Station_Anchors snapshot
    // fragment that the GymStation EntityScript populates in DoConstruct.
    // Returns FVector::ZeroVector if the station entity isn't found.
    //
    // Picking the right anchor:
    //   - PanelCenter      -> DEFAULT for most gyms. Inside the alcove at
    //                        mid-height; debug visuals don't intersect geometry.
    //   - FootprintCenter  -> For nav / physics / spatial / AStar gyms whose
    //                        test elements need to sit on the floor / navmesh.
    //   - AgentSpawn*      -> Perimeter pickup points (respects AgentSpawnOffset).
    //   - PanelTopFront    -> Banner / label attach point.
    FVector Get_StationAnchorLocation(FString InStationTag, ECk_GymStation_Anchor InAnchor)
    {
        auto Handle = Get_StationHandle(InStationTag);
        if (!ck::IsValid(Handle))
        {
            return FVector::ZeroVector;
        }
        auto Anchors = Handle.Get_Fragment(FCkGym_Station_Anchors);
        if (InAnchor == ECk_GymStation_Anchor::FootprintCenter) { return Anchors.FootprintCenter; }
        if (InAnchor == ECk_GymStation_Anchor::AgentSpawnFront) { return Anchors.AgentSpawnFront; }
        if (InAnchor == ECk_GymStation_Anchor::AgentSpawnBack)  { return Anchors.AgentSpawnBack; }
        if (InAnchor == ECk_GymStation_Anchor::AgentSpawnLeft)  { return Anchors.AgentSpawnLeft; }
        if (InAnchor == ECk_GymStation_Anchor::AgentSpawnRight) { return Anchors.AgentSpawnRight; }
        if (InAnchor == ECk_GymStation_Anchor::PanelTopFront)   { return Anchors.PanelTopFront; }
        return Anchors.PanelCenter;
    }

    // Convenience for callers that want a full FTransform at the named anchor
    // (anchor location + station's existing rotation). Replaces the common
    // pattern of Get_StationTransform(tag) when migrating gyms to use a
    // specific anchor - the inventory entity / cube / etc. still gets the
    // station's facing direction, just placed at the chosen anchor.
    FTransform Get_StationAnchorTransform(FString InStationTag, ECk_GymStation_Anchor InAnchor)
    {
        auto StationT = Get_StationTransform(InStationTag);
        auto Location = Get_StationAnchorLocation(InStationTag, InAnchor);
        return FTransform(StationT.Rotator(), Location, FVector(1.0, 1.0, 1.0));
    }

    UFUNCTION(Exec, DisplayName="Gym - Restart")
    void Ck_Gym_Restart()
    {
        if (_StationStartupFailed || _StationStartupPending)
        { return; }
        Request_RestartGym();
    }

    // Initial per-player startup is separate from a requested reset of the shared demo.
    // Existing gyms retain their behavior until they move simulation into a world owner.
    void Request_RestartGym()
    {
        Request_StartGym();
    }

    //--------------------------------------------------------------------------------------------------------------------------
    // Gym Cycling
    //--------------------------------------------------------------------------------------------------------------------------

    UFUNCTION(Exec, DisplayName="Gym - Next")
    void Ck_Gym_Next()
    {
        CkGym_Cycler::TryRequest_NextGymFromPlayer(this);
    }

    UFUNCTION(Exec, DisplayName="Gym - Previous")
    void Ck_Gym_Prev()
    {
        CkGym_Cycler::TryRequest_PrevGymFromPlayer(this);
    }

    UFUNCTION(Exec, DisplayName="Gym - Go To Index")
    void Ck_Gym_GoTo(int32 InIndex)
    {
        CkGym_Cycler::TryRequest_TravelToGymFromPlayer(this, InIndex);
    }

    UFUNCTION(Exec, DisplayName="Gym - List All")
    void Ck_Gym_List()
    {
        CkGym_Cycler::Print_GymList();
    }

}
