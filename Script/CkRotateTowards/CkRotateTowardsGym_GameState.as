UCLASS()
class ACk_RotateTowardsGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency=15.0;
    UPROPERTY(Replicated) FCkRotateTowardsGym_Snapshot Snapshot;
    UPROPERTY() TArray<FCk_Handle> ServerStations;
    private TArray<FCkRotateTowardsGym_Fixture> ServerFixtures;
    UPROPERTY() private FCkRotateTowardsGym_StationCohort Cohort;
    UPROPERTY() FCkRotateTowardsGym_MotionAdmission MotionAdmission;
    UPROPERTY() int32 MotionGeneration=0;
    private FCk_Handle SimulationOwner;
    UPROPERTY() FCk_Handle WorldBridge;
    private FCk_Handle_PendingEntityScript PendingBridge;
    private FCk_Handle_PendingEntityScript PendingOwner;
    private bool BridgeRequested = false;
    private bool OwnerRequested = false;

    private bool EnsureBridge()
    {
        if (!BridgeRequested)
        {
            BridgeRequested = true;
            PendingBridge = utils_entity_script_with_actor::Request_SpawnEntityScript_OnActor(
                this, UCk_EntityScript_RotateTowardsGym_WorldRoot);
            if (!utils_pending_entity_script::Get_IsValid(PendingBridge))
            { Request_Fail("RotateTowards world bridge admission rejected"); return false; }
            WorldBridge = PendingBridge.Get_EntityUnderConstruction();
        }
        if (!CkRotateTowardsShared::IsLive(WorldBridge))
        { Request_Fail("RotateTowards world bridge lost"); return false; }
        if (!utils_entity_script::Has(WorldBridge)) { return false; }
        auto Script = Cast<UCk_EntityScript_RotateTowardsGym_WorldRoot>(utils_entity_script::TryGet_EntityScript(WorldBridge));
        return ck::IsValid(Script) && utils_entity_replication_driver::Has(WorldBridge) &&
            utils_entity_replication_driver::Get_IsReplicationComplete(WorldBridge);
    }

    private bool EnsureSimulationOwner()
    {
        if (!OwnerRequested)
        {
            OwnerRequested = true;
            FCkRotateTowardsGym_OwnerParams Params; Params.Generation = MotionGeneration;
            PendingOwner = utils_entity_script::Request_SpawnEntity(WorldBridge,
                UCk_EntityScript_RotateTowardsGym_Owner, FInstancedStruct::Make(Params));
            if (!utils_pending_entity_script::Get_IsValid(PendingOwner))
            { Request_Fail("RotateTowards generation owner admission rejected"); return false; }
            SimulationOwner = PendingOwner.Get_EntityUnderConstruction();
            MotionAdmission.Owner = SimulationOwner;
            FCk_Handle OwnerCarrier;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryCapture_StationSpawnCarrierWithParent(
                SimulationOwner, WorldBridge, UCk_EntityScript_RotateTowardsGym_Owner,
                ck::TransientEntity(), ck::TransientEntity(), OwnerCarrier))
            { Request_Fail("RotateTowards owner carrier capture rejected"); return false; }
            MotionAdmission.OwnerSpawnCarrier = OwnerCarrier;
            if (!CaptureCreatorReturns()) { Request_Fail("RotateTowards owner capture rejected"); return false; }
        }
        if (!CkRotateTowardsShared::IsLive(SimulationOwner))
        { Request_Fail("RotateTowards generation owner lost"); return false; }
        if (!utils_entity_script::Has(SimulationOwner)) { return false; }
        auto Script = Cast<UCk_EntityScript_RotateTowardsGym_Owner>(utils_entity_script::TryGet_EntityScript(SimulationOwner));
        if (ck::Is_NOT_Valid(Script) || Script.Generation != MotionGeneration)
        { Request_Fail("RotateTowards generation owner identity rejected"); return false; }
        if (!utils_entity_replication_driver::Has(SimulationOwner) ||
            !utils_entity_replication_driver::Get_IsReplicationComplete(SimulationOwner)) { return false; }
        bool CarrierAllocated = false;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrierWithParent(
            MotionAdmission.OwnerSpawnCarrier, SimulationOwner, WorldBridge,
            ck::TransientEntity(), ck::TransientEntity(), CarrierAllocated))
        { Request_Fail("RotateTowards owner carrier identity rejected"); return false; }
        if (CarrierAllocated) { return false; }
        if (!OwnerClaimed)
        {
            OwnerClaimed = UCk_RotateTowardsGym_Runtime_UE::TryClaim_NewOwner(this, SimulationOwner, MotionGeneration);
            if (!OwnerClaimed) { Request_Fail("RotateTowards generation owner claim rejected"); }
        }
        return OwnerClaimed;
    }
    private FCkRotateTowardsGym_CreatorLedger CreatorLedger;
    private TArray<int32> PauseAdded;
    private bool Creating=false, OwnerClaimed=false, FixturesStarted=false, TweenStarted=false, StaticReceiptsObserved=false;
    private FString DeferredFailure;
    private FString LastSampleRejection;
    private float MonitorElapsed=0;
    private TArray<FCk_Handle> Known, Parents, Retiring, Required, RequiredParents;
    private bool Ending=false, Armed=false, Starting=false, RetiringOld=false, Unproven=false, StationsRetired=false;
    private float StartedAt=0;
    private int32 NextGeneration=0, CatalogIndex=-1;
    private FCkGym_Entry CatalogEntry;
    private ACk_RotateTowardsGym_PlayerController Requester;
    private ACk_Gym_Base_Pawn RequestPawn;
    private int32 RequestSerial=0, RequestGeneration=0;
    private FCkRotateTowardsGym_ControlCommit Prepared;

    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_CapturedSimulationLedger() const { return Known; }
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_RetiringSimulationLedger() const { return Retiring; }
    UFUNCTION(BlueprintPure) bool Get_IsRetirementUnproven() const { return Unproven || Cohort.Get_IsRetirementUnproven(); }
    bool Get_HasWorldIdentity()
    {
        auto W=GetWorld();
        if (Ending || !HasAuthority() || ck::Is_NOT_Valid(W) || W.GetGameState()!=this ||
            GetClass()!=ACk_RotateTowardsGym_GameState || !Get_HasPublishedCatalog()) { return false; }
        auto Mode=Gameplay::GetGameMode(); auto Registry=UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        int32 Index=UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld()!=W || Mode.GetClass()!=ACk_RotateTowardsGym_GameMode ||
            Index<0 || Index>=Registry.Num()) { return false; }
        auto Entry=Registry[Index];
        if (Entry.DisplayName!="Rotate Towards" || Entry.GameModeClass!=ACk_RotateTowardsGym_GameMode) { return false; }
        return CatalogIndex<0 || (Index==CatalogIndex && Entry.DisplayName==CatalogEntry.DisplayName &&
            Entry.GameModeClass==CatalogEntry.GameModeClass && Entry.LevelName==CatalogEntry.LevelName &&
            Entry.Category==CatalogEntry.Category && Entry.HintCode==CatalogEntry.HintCode);
    }
    bool Get_HasCapturedOwnerContext(ACk_RotateTowardsGym_PlayerController ControllerOwner, ACk_Gym_Base_Pawn Pawn)
    {
        return Get_HasWorldIdentity() && ck::IsValid(ControllerOwner) && ControllerOwner.GetClass()==ACk_RotateTowardsGym_PlayerController &&
            ControllerOwner.HasAuthority() && ControllerOwner.GetWorld()==GetWorld() && ck::IsValid(Pawn) && Pawn.GetWorld()==GetWorld() &&
            Pawn.GetController()==ControllerOwner && ControllerOwner.GetControlledPawn()==Pawn;
    }
    bool Get_IsValidOwner(ACk_RotateTowardsGym_PlayerController ControllerOwner, ACk_Gym_Base_Pawn Pawn)
    { return Get_HasCapturedOwnerContext(ControllerOwner,Pawn) && !ControllerOwner.OwnerSessionFailed; }
    UFUNCTION(BlueprintPure) bool Get_IsCompleteSnapshot(FCkRotateTowardsGym_Snapshot S)
    { FString Failure; return TryValidate_CompleteSnapshot(S,Failure); }
    private bool TryValidate_CompleteSnapshot(FCkRotateTowardsGym_Snapshot S, FString&out Failure)
    {
        Failure="";
        if (!S.Ready || S.Failed || !S.FailureReason.IsEmpty() || S.CatalogVersion!=CkRotateTowardsShared::CatalogVersion ||
            S.Generation<=0 || S.Sequence<=0 || S.Stations.Num()!=3 || S.PassiveStations.Num()!=3 ||
            S.StationGeometry.Num()!=3 || S.Origins.Num()!=3 || S.Visuals.Num()!=11 ||
            S.Turrets.Num()!=7 || S.StationTitles.Num()!=3 || S.StationBodies.Num()!=3 || S.StationInstructions.Num()!=3) { Failure="snapshot-header"; return false; }
        for (int32 i=0;i<3;++i)
        {
            auto PassiveCopy=S.StationGeometry[i]; PassiveCopy.AutoSize=false;
            FString GeometryFailure;
            if (!CkRotateTowardsShared::TryValidate_SourceAndPassiveGeometry(
                    i,S.PassiveStations[i],S.StationGeometry[i],PassiveCopy,GeometryFailure) ||
                !CkRotateTowardsShared::Get_IsExactWireParams(S.Stations[i],i) || S.Origins[i].ContainsNaN() ||
                S.Origins[i] != S.Stations[i].InitialTransform.GetTranslation()+FVector(1300,0,150) ||
                S.StationTitles[i].IsEmpty() || S.StationBodies[i].IsEmpty() ||
                S.StationTitles[i]!=S.StationGeometry[i].Pieces[6].Text ||
                S.StationBodies[i]!=S.StationGeometry[i].Pieces[7].Text ||
                S.StationInstructions[i]!=(S.StationGeometry[i].Pieces[8].Visible && !S.StationGeometry[i].Pieces[8].HiddenInGame
                    ? S.StationGeometry[i].Pieces[8].Text : "")) { Failure=f"station={i} geometry={GeometryFailure} metadataParams={CkRotateTowardsShared::Get_IsExactWireParams(S.Stations[i],i)} titleEmpty={S.StationTitles[i].IsEmpty()} bodyEmpty={S.StationBodies[i].IsEmpty()}"; return false; }
        }
        for (int32 i=0;i<11;++i)
        {
            auto R=S.Visuals[i]; int32 Station=i<3?0:i<6?1:2;
            if (R.Slot!=i || R.Station!=Station) { Failure=f"visual={i} slot={R.Slot} station={R.Station}"; return false; }
        }
        for (int32 i=0;i<7;++i)
        {
            auto R=S.Turrets[i]; int32 Station=i<2?0:i==2?1:2;
            if (R.Slot!=i || R.Station!=Station ||
                R.Desired.ContainsNaN() || R.Remaining.ContainsNaN() ||
                R.Mode!=(i==1?int32(ECk_RotateTowards_Mode::Instant):int32(ECk_RotateTowards_Mode::RateLimited)) ||
                R.TurnRate!=(i<2?60:90) || R.PitchRate!=(i<2?60:90) || R.RollRate!=(i<2?60:90) || R.ReachedTolerance!=1 || R.Clamped!=(i==2) ||
                R.YawLocked!=(i==4) || R.PitchLocked!=(i==5) || R.Enabled!=(i!=6)) { Failure=f"turret={i} slot={R.Slot} station={R.Station} mode={R.Mode} rates={R.TurnRate}/{R.PitchRate}/{R.RollRate} tolerance={R.ReachedTolerance} clamped={R.Clamped} yawLocked={R.YawLocked} pitchLocked={R.PitchLocked} enabled={R.Enabled}"; return false; }
        }
        return true;
    }
    // Creator identities are retained before any census. Census can reject, never adopt.
    private bool CaptureCreatorReturns()
    {
        if (SimulationOwner==FCk_Handle()) { return Known.IsEmpty(); }
        bool ComponentsCaptured = true;
        for (int32 i = 0; i < ServerFixtures.Num(); ++i)
        { if (!ServerFixtures[i].Capture_CubeComponents()) { ComponentsCaptured = false; } }
        bool Captured=CreatorLedger.TryCapture_Fixtures(ServerFixtures,MotionAdmission,
            SimulationOwner,MotionGeneration,WorldBridge);
        if (CreatorLedger.RoleValues.Num()!=42 || CreatorLedger.ExpectedParents.Num()!=42)
        { return false; }
        for (int32 i=0;i<42;++i)
        {
            auto H=CreatorLedger.RoleValues[i]; if (H==FCk_Handle()) { continue; }
            auto P=CreatorLedger.ExpectedParents[i];
            int32 Existing=Known.FindIndex(H);
            if (Existing<0) { Known.Add(H); Parents.Add(P); }
            else if (Existing>=Parents.Num() || Parents[Existing]!=P) { Captured=false; }
        }
        return ComponentsCaptured && Captured && !CreatorLedger.Broken;
    }
    bool Get_CanStageMotion(int32 Generation)
    {
        return Starting && !Ending && !Snapshot.Failed && DeferredFailure.IsEmpty() &&
            !RetiringOld && !Unproven && Generation==MotionGeneration &&
            MotionAdmission.Generation==Generation && !MotionAdmission.Closed &&
            !MotionAdmission.Activated && Get_HasWorldIdentity();
    }
    private bool SafeLedger()
    {
        if (Unproven || Known.Num()!=Parents.Num()) { return false; }
        if (CreatorLedger.Broken ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(Known,Retiring,ck::TransientEntity()))
        { return false; }
        for (int32 i=0;i<Known.Num();++i)
        {
            auto H=Known[i];
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { continue; }
            // The native proof above checks represented lifetime for every allocated
            // Retiring entry. Its only ownerless exception is the world root, which
            // is never a fixture creator return and must not reach this getter.
            if (H==ck::TransientEntity()) { return false; }
            if (utils_entity_lifetime::Get_LifetimeOwner(H,ECk_PendingKill_Policy::IncludePendingKill)!=Parents[i])
            { return false; }
        }
        return true;
    }
    private bool RetireLeaves()
    {
        for (;;)
        {
            bool Live=false,Progress=false;
            for (auto H:Retiring)
            {
                if (!CkRotateTowardsShared::IsLive(H)) { continue; } Live=true;
                if (!SafeLedger()) { return false; }
                TArray<FCk_Handle> Children;
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H,ck::TransientEntity(),Children)) { return false; }
                bool Leaf=true;
                for (auto C:Children) { if (!Retiring.Contains(C)) { return false; } if (CkRotateTowardsShared::IsLive(C)) { Leaf=false; } }
                for (auto C:utils_entity_lifetime::Get_LifetimeDependents(H))
                { if (!Known.Contains(C) || !Retiring.Contains(C)) { return false; } if (CkRotateTowardsShared::IsLive(C)) { Leaf=false; } }
                if (!Leaf) { continue; }
                utils_entity_lifetime::Request_DestroyEntity(H);
                if (CkRotateTowardsShared::IsLive(H) || !SafeLedger()) { return false; } Progress=true;
            }
            if (!Live) { return SafeLedger(); } if (!Progress) { return false; }
        }
        return false;
    }
    private bool QuiesceCapturedKernels()
    {
        if (SimulationOwner==FCk_Handle()) { return Known.IsEmpty(); }
        FCkRotateTowardsGym_QuiesceResult Result;
        return OwnerClaimed && UCk_RotateTowardsGym_Runtime_UE::TryQuiesce_KnownProducers(this,Result) &&
            Result.AllKnownStopped;
    }
    private void Retire()
    {
        StationsRetired=false;
        // Close the already captured station callbacks even when motion proof fails.
        // Closing is separate from permission to destroy either frozen graph.
        bool PresentationClosed=Cohort.Request_CloseKnownPresentation();
        bool Captured=CaptureCreatorReturns();
        bool Stopped=QuiesceCapturedKernels();
        if (!PresentationClosed || !Captured || !Stopped) { Unproven=true; }
        for (auto H:Known) { if (!Retiring.Contains(H)) { Retiring.Add(H); } }
        if (!SafeLedger()) { MarkRetirementUnproven("RotateTowards captured retirement read-set unsafe"); return; }
        Cohort.Request_Retire();
        if (!RetireLeaves() || Cohort.Get_IsRetirementUnproven())
        { MarkRetirementUnproven("RotateTowards retirement callback changed frozen closure"); }
    }
    private void MarkRetirementUnproven(FString Reason)
    {
        Unproven=true; Snapshot.Ready=false; Snapshot.Failed=true; Snapshot.FailureReason=Reason;
        Starting=false; Prepared=FCkRotateTowardsGym_ControlCommit();
        if (ck::IsValid(Requester) && RequestSerial>0) { Requester.Request_RejectRotateTowardsControl(RequestSerial,3); }
        QuiesceCapturedKernels(); ForceNetUpdate();
    }
    private bool Retired()
    {
        if (Unproven) { return false; }
        if (!StationsRetired)
        { if (!Cohort.PollRetirement()) { return false; } StationsRetired=true; }
        for (auto H:Retiring) { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return false; } }
        Known.Empty(); Parents.Empty(); Retiring.Empty(); Required.Empty(); RequiredParents.Empty();
        ServerFixtures.Empty(); ServerStations.Empty(); SimulationOwner=FCk_Handle();
        CreatorLedger=FCkRotateTowardsGym_CreatorLedger(); MotionAdmission=FCkRotateTowardsGym_MotionAdmission();
        PauseAdded.Empty(); PendingOwner=FCk_Handle_PendingEntityScript(); OwnerRequested=false; OwnerClaimed=false; FixturesStarted=false; TweenStarted=false; StaticReceiptsObserved=false; return true;
    }
    private bool ExactSourceIdentity()
    {
        if (!Get_HasWorldIdentity() || Cohort.Get_Phase()!=2 || !Cohort.Get_HasExactReadyCohort() || Cohort.Get_HasTerminalFailure() ||
            ServerStations.Num()!=3 || ServerFixtures.Num()!=3 || CreatorLedger.Broken) { return false; }
        for (int32 i=0;i<3;++i)
        { if (!CkRotateTowardsShared::Get_IsExactStationRecipe(ServerStations[i],i) ||
              !ServerStations[i].Has_Fragment(FCkGym_Station_TitleAndDescription)) { return false; } }
        return true;
    }
    private bool ExactLive()
    { return ExactSourceIdentity() && UCk_RotateTowardsGym_Runtime_UE::TryValidate_ActiveCohort(this); }
    // Failure-only, read-only split of ExactLive; never changes publication or admission.
    private void LogExactLiveRejection()
    {
        bool WorldOk=Get_HasWorldIdentity();
        int32 CohortPhase=Cohort.Get_Phase();
        bool CohortExact=CohortPhase==2 && Cohort.Get_HasExactReadyCohort();
        bool CohortTerminal=Cohort.Get_HasTerminalFailure();
        bool NativeActive=UCk_RotateTowardsGym_Runtime_UE::TryValidate_ActiveCohort(this);
        auto NativeDetail=UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortDiagnostic(this);
        bool Recipe0=false,Recipe1=false,Recipe2=false;
        bool Title0=false,Title1=false,Title2=false;
        if (ServerStations.Num()==3)
        {
            if (CkRotateTowardsShared::IsLive(ServerStations[0]))
            { Recipe0=CkRotateTowardsShared::Get_IsExactStationRecipe(ServerStations[0],0);
              Title0=ServerStations[0].Has_Fragment(FCkGym_Station_TitleAndDescription); }
            if (CkRotateTowardsShared::IsLive(ServerStations[1]))
            { Recipe1=CkRotateTowardsShared::Get_IsExactStationRecipe(ServerStations[1],1);
              Title1=ServerStations[1].Has_Fragment(FCkGym_Station_TitleAndDescription); }
            if (CkRotateTowardsShared::IsLive(ServerStations[2]))
            { Recipe2=CkRotateTowardsShared::Get_IsExactStationRecipe(ServerStations[2],2);
              Title2=ServerStations[2].Has_Fragment(FCkGym_Station_TitleAndDescription); }
        }
        System::PrintString(f"[RotateTowardsExactLive] world={WorldOk} cohortPhase={CohortPhase} cohortExact={CohortExact} cohortTerminal={CohortTerminal} stationCount={ServerStations.Num()} fixtureCount={ServerFixtures.Num()} creatorBroken={CreatorLedger.Broken} nativeActive={NativeActive} nativeDetail={NativeDetail} sampleRejection={LastSampleRejection} recipe={Recipe0}/{Recipe1}/{Recipe2} title={Title0}/{Title1}/{Title2}",
            false,true,FLinearColor::White,0.0f,NAME_None);
    }
    UFUNCTION(BlueprintPure) bool Get_IsReady() { return Snapshot.Ready && !Snapshot.Failed && !Starting && !RetiringOld && ExactLive(); }
    void Request_EnsureSharedDemo()
    {
        if (!Get_HasWorldIdentity() || Armed || Snapshot.Failed) { return; }
        CatalogIndex=UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex(); CatalogEntry=UCk_Utils_GymRegistry_UE::Get_GymRegistry()[CatalogIndex];
        Armed=true; // Actor Tick alone advances authority construction and publication.
    }
    private void StartGeneration()
    {
        if (Unproven || NextGeneration>=2147483645) { Request_Fail("RotateTowards generation/retirement exhausted"); return; }
        ++NextGeneration; MotionGeneration=NextGeneration; Starting=true; StaticReceiptsObserved=false;
        MotionAdmission=FCkRotateTowardsGym_MotionAdmission(); MotionAdmission.Generation=MotionGeneration;
        MotionAdmission.Roots.SetNum(3); MotionAdmission.Motion.SetNum(5); MotionAdmission.Turrets.SetNum(7);
        MotionAdmission.Tweens.SetNum(4); MotionAdmission.CubeNodes.SetNum(11);
        MotionAdmission.CubeComponents.SetNum(11); MotionAdmission.ComponentAdded.SetNum(11); MotionAdmission.CubeSpawnCarriers.SetNum(11);
        PauseAdded.SetNum(4); ServerFixtures.SetNum(3);
        if (!CreatorLedger.Initialize()) { Request_Fail("RotateTowards creator ledger admission rejected"); return; }
        StartedAt=float(System::GetGameTimeInSeconds());
        TArray<FCk_GymStation_SpawnParams> Recipes; for (int32 i=0;i<3;++i) { Recipes.Add(CkRotateTowardsShared::Get_Params(i)); }
        Creating=true;
        auto Begin=Cohort.TryBegin(ck::TransientEntity(),Recipes,
            FCk_Delegate_Request_OnCompleted(this,n"OnStationCompleted"),
            FCk_Delegate_EntityScript_Constructed(this,n"OnStationConstructed"));
        Creating=false;
        if (!DeferredFailure.IsEmpty()) { Request_Fail(DeferredFailure); return; }
        if (Begin!=ECkGym_SharedStationBegin::Started)
        { Request_Fail("RotateTowards station admission rejected"); }
    }
    UFUNCTION() private void OnStationCompleted(FCk_Handle H,ECk_Request_OperationResult Result)
    {
        if (Ending || !Starting || Snapshot.Failed) { return; }
        if (!Cohort.TryRecord_Completed(H,H,Result)) { Request_Fail("RotateTowards station completion rejected"); return; }
        TArray<FCk_Handle> Geometry,GeometryParents;
        if (!CkRotateTowardsShared::TryGet_RequiredGeometry(H,Geometry,GeometryParents) || Geometry.Num()!=GeometryParents.Num())
        { Request_Fail("RotateTowards station geometry unavailable"); return; }
        for (int32 i=0;i<Geometry.Num();++i)
        { if (!Cohort.TryRequireOwned(Geometry[i],GeometryParents[i])) { Request_Fail("RotateTowards station geometry rejected"); return; } }
    }
    UFUNCTION() private void OnStationConstructed(FCk_Handle_EntityScript H)
    { if (!Ending && Starting && !Snapshot.Failed && !Cohort.TryRecord_Constructed(FCk_Handle(H),H)) { Request_Fail("RotateTowards station constructed rejected"); } }
    private bool CreateFixtures()
    {
        if (FixturesStarted || ServerFixtures.Num()!=3 || !CkRotateTowardsShared::IsLive(ck::TransientEntity()))
        { return false; }
        FixturesStarted=true;
        if (!OwnerClaimed || !CaptureCreatorReturns()) { return false; }
        OwnerClaimed=true; ServerStations=Cohort.Stations;
        if (ServerStations.Num()!=3) { return false; }
        for (int32 i=0;i<3;++i)
        {
            auto H=ServerStations[i];
            if (!CkRotateTowardsShared::IsLive(H) || !H.Has_Fragment(FCkGym_Station_Anchors)) { return false; }
            auto A=H.Get_Fragment(FCkGym_Station_Anchors);
            Creating=true;
            bool Created=ServerFixtures[i].Create_Static(SimulationOwner,this,
                A.FootprintCenter+FVector(1300,0,150),i,MotionGeneration);
            Creating=false;
            bool Captured=CaptureCreatorReturns();
            if (!Created || !Captured || !DeferredFailure.IsEmpty()) { return false; }
        }
        return true;
    }
    UFUNCTION() private void OnRotateTowardsGymCubeAdded(FCk_Handle_UnrealComponent H)
    {
        if (Ending || Snapshot.Failed || !Get_CanStageMotion(MotionGeneration)) { return; }
        if (ServerFixtures.Num()!=3 || MotionAdmission.ComponentAdded.Num()!=11 ||
            !CkRotateTowardsShared::IsLive(H) || !utils_unreal_component::Has(H))
        { Request_Fail("RotateTowards component callback schema rejected"); return; }
        int32 Index=-1;
        for (int32 s=0;s<3;++s)
        {
            int32 Offset=s==0?0:s==1?3:6;
            for (int32 j=0;j<ServerFixtures[s].CubeComponents.Num();++j)
            {
                if (ServerFixtures[s].CubeComponents[j]!=H) { continue; }
                if (Index>=0 || j>=(s==2?5:3) ||
                    ServerFixtures[s].Generation!=MotionGeneration || ServerFixtures[s].Owner!=SimulationOwner)
                { Request_Fail("RotateTowards aliased component callback rejected"); return; }
                Index=Offset+j;
            }
        }
        if (Index<0 || Index>=11 || MotionAdmission.ComponentAdded[Index]!=0)
        { Request_Fail("RotateTowards component callback identity rejected"); return; }
        auto Mesh=Cast<UStaticMeshComponent>(utils_unreal_component::Get_Component(H));
        auto Asset=Cast<UStaticMesh>(LoadObject(this,"/Engine/BasicShapes/Cube.Cube"));
        if (ck::Is_NOT_Valid(Mesh) || Mesh.GetWorld()!=GetWorld() || Mesh.GetClass()!=UStaticMeshComponent ||
            ck::Is_NOT_Valid(Asset)) { Request_Fail("RotateTowards cube component unavailable"); return; }
        Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh.SetStaticMesh(Asset);
        if (Mesh.GetStaticMesh()!=Asset || Mesh.GetCollisionEnabled()!=ECollisionEnabled::NoCollision)
        { Request_Fail("RotateTowards cube application rejected"); return; }
        MotionAdmission.ComponentAdded[Index]=1;
    }
    UFUNCTION() private void OnRotateTowardsGymTweenPaused(FCk_Handle H,ECk_Request_OperationResult Result)
    {
        if (Ending || Snapshot.Failed || !Get_CanStageMotion(MotionGeneration)) { return; }
        int32 Index=-1;
        for (int32 s=0;s<ServerFixtures.Num();++s)
        {
            int32 Offset=s==0?0:s==1?1:2;
            for (int32 j=0;j<ServerFixtures[s].Tweens.Num();++j)
            {
                if (FCk_Handle(ServerFixtures[s].Tweens[j])!=H) { continue; }
                if (Index>=0 || j>=(s==2?2:1) || ServerFixtures[s].Generation!=MotionGeneration)
                { Request_Fail("RotateTowards aliased Pause receipt rejected"); return; }
                Index=Offset+j;
            }
        }
        if (PauseAdded.Num()!=4 || Index<0 || Index>=4 || PauseAdded[Index]!=0 ||
            Result!=ECk_Request_OperationResult::Succeeded || !CkRotateTowardsShared::IsLive(H))
        { Request_Fail("RotateTowards Pause receipt rejected"); return; }
        PauseAdded[Index]=1;
    }
    private bool AdvanceMotionAdmission()
    {
        bool CubesReady = true;
        for (int32 s = 0; s < 3; ++s)
        {
            if (!ServerFixtures[s].Poll_Cubes(this)) { CubesReady = false; }
        }
        if (!CaptureCreatorReturns()) { Request_Fail("RotateTowards creator identity changed"); return false; }
        for (int32 s = 0; s < 3; ++s)
        { if (ServerFixtures[s].StagingFailed) { Request_Fail("RotateTowards cube construction rejected"); return false; } }
        if (!CubesReady) { return false; }
        for (int32 i = 0; i < 11; ++i)
        {
            if (MotionAdmission.ComponentAdded[i] < 0 || MotionAdmission.ComponentAdded[i] > 1)
            { Request_Fail("RotateTowards component receipt cardinality rejected"); return false; }
            if (MotionAdmission.ComponentAdded[i] == 0) { MotionAdmission.ComponentAdded[i] = 1; }
        }
        // Commit the complete static construction phase before composing producers.
        // A subsequent authority step creates the paused Tweens from these receipts.
        if (!StaticReceiptsObserved)
        {
            StaticReceiptsObserved = true;
            return false;
        }
        if (!TweenStarted)
        {
            for (int32 Count:MotionAdmission.ComponentAdded)
            {
                if (Count<0 || Count>1) { Request_Fail("RotateTowards component receipt cardinality rejected"); return false; }
                if (Count!=1) { return false; }
            }
            TweenStarted=true;
            for (int32 s=0;s<3;++s)
            {
                Creating=true;
                bool Created=ServerFixtures[s].Create_PausedTweens(this,ServerFixtures[s].CubeComponents);
                Creating=false;
                bool Captured=CaptureCreatorReturns();
                if (!Created || !Captured || !DeferredFailure.IsEmpty())
                { Request_Fail("RotateTowards paused producer creation rejected"); return false; }
            }
            return false; // Genuine normal request processing supplies the four Pause receipts.
        }
        if (PauseAdded.Num()!=4) { Request_Fail("RotateTowards Pause receipt schema rejected"); return false; }
        for (int32 Count:PauseAdded)
        {
            if (Count<0 || Count>1) { Request_Fail("RotateTowards Pause receipt cardinality rejected"); return false; }
            if (Count!=1) { return false; }
        }
        if (!UCk_RotateTowardsGym_Runtime_UE::Get_IsSourceConstructionDrained(this)) { return false; }
        if (!UCk_RotateTowardsGym_Runtime_UE::TryValidate_Cohort(this,MotionAdmission))
        { Request_Fail("RotateTowards dormant admission rejected"); return false; }
        if (!UCk_RotateTowardsGym_Runtime_UE::TryActivate_Cohort(this))
        { Request_Fail("RotateTowards activation rejected"); return false; }
        return true;
    }
    private bool BuildSample(FCkRotateTowardsGym_Snapshot& Out)
    {
        LastSampleRejection="";
        Out=FCkRotateTowardsGym_Snapshot(); if (!ExactLive() || Snapshot.Sequence>=2147483646) { LastSampleRejection="identity-or-sequence"; return false; }
        FCkRotateTowardsGym_Snapshot S; S.CatalogVersion=CkRotateTowardsShared::CatalogVersion; S.Generation=NextGeneration; S.Sequence=Snapshot.Sequence+1; S.Ready=true;
        int32 VisualSlot=0,TurretSlot=0;
        for (int32 i=0;i<3;++i)
        {
            auto F=ServerFixtures[i];
            S.Stations.Add(CkRotateTowardsShared::Get_Params(i)); S.Origins.Add(F.Origin);
            FCk_GymStation_SpawnParams Passive; FCkGym_StationVisibleGeometry Geometry;
            FString CaptureFailure;
            if (!CkRotateTowardsShared::TryCapture_StationPresentation(ServerStations[i],i,Passive,Geometry,CaptureFailure))
            { LastSampleRejection=f"station={i} capture={CaptureFailure}"; return false; }
            S.PassiveStations.Add(Passive); S.StationGeometry.Add(Geometry);
            // Caption values come from the same validated actual text components
            // as the geometry sample; the optional director fragment can be empty.
            S.StationTitles.Add(Geometry.Pieces[6].Text);
            S.StationBodies.Add(Geometry.Pieces[7].Text);
            // Hidden optional floor text has no displayed instruction caption.
            S.StationInstructions.Add(Geometry.Pieces[8].Visible && !Geometry.Pieces[8].HiddenInGame
                ? Geometry.Pieces[8].Text : "");
            for (auto H:F.CubeComponents)
            {
                auto Mesh=Cast<UStaticMeshComponent>(utils_unreal_component::Get_Component(H));
                if (ck::Is_NOT_Valid(Mesh) || Mesh.GetWorld()!=GetWorld()) { LastSampleRejection=f"station={i} mesh-world"; return false; }
                FCkRotateTowardsGym_VisualRow R; R.Slot=VisualSlot; ++VisualSlot; R.Station=i; S.Visuals.Add(R);
            }
            for (auto H:F.Turrets)
            {
                auto T=utils_rotate_towards::Get_Tunables(H);
                FCkRotateTowardsGym_TurretRow R; R.Slot=TurretSlot; ++TurretSlot; R.Station=i;
                R.Desired=utils_rotate_towards::Get_DesiredRotation(H); R.Remaining=utils_rotate_towards::Get_RemainingRotation(H);
                R.Enabled=utils_rotate_towards::Get_IsEnabled(H); R.AtTarget=utils_rotate_towards::Get_IsAtTarget(H);
                R.Clamped=utils_rotate_towards::Get_HasRangeClamp(H); R.Mode=int32(T.Get_Mode());
                R.YawLocked=T.Get_Yaw().Get_Mode()==ECk_RotateTowards_AxisMode::Locked; R.PitchLocked=T.Get_Pitch().Get_Mode()==ECk_RotateTowards_AxisMode::Locked;
                if (T.Get_Roll().Get_Mode()!=ECk_RotateTowards_AxisMode::Free) { LastSampleRejection=f"station={i} turret-roll"; return false; }
                R.TurnRate=T.Get_Yaw().Get_TurnRateDegPerSec(); R.PitchRate=T.Get_Pitch().Get_TurnRateDegPerSec();
                R.RollRate=T.Get_Roll().Get_TurnRateDegPerSec(); R.ReachedTolerance=T.Get_ReachedToleranceDeg(); S.Turrets.Add(R);
            }
        }
        FString CompleteFailure;
        if (!TryValidate_CompleteSnapshot(S,CompleteFailure)) { LastSampleRejection=f"complete-snapshot {CompleteFailure}"; return false; } Out=S; return true;
    }
    bool Request_Rebuild(ACk_RotateTowardsGym_PlayerController ControllerOwner,ACk_Gym_Base_Pawn Pawn,int32 Serial,int32 Generation)
    {
        if (!Get_IsReady() || !Get_IsValidOwner(ControllerOwner,Pawn) || RequestSerial!=0 || Serial<=0 || Generation!=Snapshot.Generation || NextGeneration>=2147483645 || Snapshot.Sequence>=2147483646 ||
            !ControllerOwner.Get_CanAcceptOwnerCommit() || !ControllerOwner.Get_HasReservedRotateTowardsControl(Serial,Generation,Pawn)) { return false; }
        Requester=ControllerOwner; RequestPawn=Pawn; RequestSerial=Serial; RequestGeneration=Generation;
        Snapshot.Ready=false; ForceNetUpdate(); Retire(); RetiringOld=true; StartedAt=float(System::GetGameTimeInSeconds()); return true;
    }
    bool Request_TryGetPreparedRotateTowardsCommit(ACk_RotateTowardsGym_PlayerController ControllerOwner,int32 Serial,FCkRotateTowardsGym_ControlCommit& Out)
    {
        Out=FCkRotateTowardsGym_ControlCommit();
        if (!Starting || !Get_IsValidOwner(ControllerOwner,RequestPawn) || ControllerOwner!=Requester || Serial!=RequestSerial ||
            !ControllerOwner.Get_HasReservedRotateTowardsControl(Serial,RequestGeneration,RequestPawn) || Prepared.RequestSerial!=Serial ||
            Prepared.Owner!=ControllerOwner || Prepared.OwnerPawn!=RequestPawn || Prepared.OriginState!=this || Prepared.OriginGeneration!=RequestGeneration ||
            Prepared.DeliverySerial!=ControllerOwner.Get_NextDeliverySerial() || Prepared.Snapshot.Generation!=RequestGeneration+1 ||
            !Get_IsCompleteSnapshot(Prepared.Snapshot)) { return false; } Out=Prepared; return true;
    }
    UFUNCTION(BlueprintOverride) void Tick(float DeltaSeconds)
    {
        if (!HasAuthority() || Ending || Snapshot.Failed) { return; }
        if (!Armed) { Request_EnsureSharedDemo(); }
        if (!Armed) { return; }
        MonitorElapsed+=DeltaSeconds;
        if (MonitorElapsed<0.1f) { return; }
        MonitorElapsed=0; AdvanceAuthority();
    }
    // The production Tick and dormant network fixture invoke this same bounded step.
    UFUNCTION(BlueprintCallable) void AdvanceAuthority()
    {
        if (Ending || Snapshot.Failed || !HasAuthority()) { return; }
        if (!Armed) { Request_EnsureSharedDemo(); }
        if (!Armed) { return; }
        if (!Get_HasWorldIdentity()) { Request_Fail("RotateTowards world/catalog changed"); return; }
        if (!EnsureBridge()) { return; }
        if (!Starting && !RetiringOld && Snapshot.Generation==0) { StartGeneration(); return; }
        if (RequestSerial>0 && !Get_IsValidOwner(Requester,RequestPawn))
        {
            if (ck::IsValid(Requester)) { Requester.Request_DetachRotateTowardsControl(this,RequestPawn,RequestSerial,RequestGeneration); }
            Requester=nullptr; RequestPawn=nullptr; RequestSerial=0; RequestGeneration=0; Prepared=FCkRotateTowardsGym_ControlCommit();
        }
        if ((Starting || RetiringOld) && float(System::GetGameTimeInSeconds())-StartedAt>20)
        { if (Starting && MotionAdmission.Activated) { LogExactLiveRejection(); } Request_Fail("RotateTowards construction/retirement timeout"); return; }
        if (RetiringOld) { if (!Retired()) { return; } RetiringOld=false; StartGeneration(); return; }
        auto Poll=Cohort.PollConstruction(); if (Poll==ECkGym_SharedStationPoll::Failed) { Request_Fail("RotateTowards required station lost"); return; }
        if (Poll!=ECkGym_SharedStationPoll::Ready) { return; }
        if (!FixturesStarted && !EnsureSimulationOwner()) { return; }
        if (!FixturesStarted) { if (!CreateFixtures()) { Request_Fail("RotateTowards whole fixture composition rejected"); } return; }
        if (!MotionAdmission.Activated) { if (!AdvanceMotionAdmission()) { return; } }
        if (!ExactSourceIdentity())
        { LogExactLiveRejection(); Request_Fail(Starting ? "RotateTowards activated source rejected" : "RotateTowards required kernel/visual lost"); return; }
        auto ActiveState=UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortState(this);
        if (ActiveState==ECkRotateTowardsGym_ActiveCohortState::Rejected)
        { LogExactLiveRejection(); Request_Fail(Starting ? "RotateTowards activated source rejected" : "RotateTowards required kernel/visual lost"); return; }
        // A real queued scene update delays sampling; no incomplete sample is published.
        if (ActiveState==ECkRotateTowardsGym_ActiveCohortState::Pending) { return; }
        FCkRotateTowardsGym_Snapshot Candidate;
        if (!BuildSample(Candidate)) { if (!Starting) { Request_Fail("RotateTowards complete source sample rejected"); } return; }
        if (Starting && RequestSerial>0)
        {
            if (!Get_IsValidOwner(Requester,RequestPawn) || !Requester.Get_CanAcceptOwnerCommit()) { Request_Fail("RotateTowards owner slot changed"); return; }
            Prepared=FCkRotateTowardsGym_ControlCommit(); Prepared.Owner=Requester; Prepared.OwnerPawn=RequestPawn; Prepared.OriginState=this;
            Prepared.OriginGeneration=RequestGeneration; Prepared.RequestSerial=RequestSerial; Prepared.DeliverySerial=Requester.Get_NextDeliverySerial(); Prepared.Snapshot=Candidate;
            if (Prepared.DeliverySerial<=0 || !Requester.Request_TryRecordRotateTowardsCommit(this,RequestSerial)) { Request_Fail("RotateTowards retained commit rejected"); return; }
        }
        Starting=false; Snapshot=Candidate; ForceNetUpdate(); Prepared=FCkRotateTowardsGym_ControlCommit(); Requester=nullptr; RequestPawn=nullptr; RequestSerial=0; RequestGeneration=0;
    }
    void Request_Fail(FString Reason)
    {
        if (Creating)
        { if (DeferredFailure.IsEmpty()) { DeferredFailure=Reason; } return; }
        if (Snapshot.Failed) { return; }
        FString FailureReason=DeferredFailure.IsEmpty()?Reason:DeferredFailure;
        ck::Error(f"[RotateTowardsShared] {FailureReason}");
        Snapshot.Ready=false; Snapshot.Failed=true; Snapshot.FailureReason=FailureReason;
        if (ck::IsValid(Requester) && RequestSerial>0) { Requester.Request_RejectRotateTowardsControl(RequestSerial,3); }
        Starting=false; Prepared=FCkRotateTowardsGym_ControlCommit(); Retire(); ForceNetUpdate();
    }
    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason Reason)
    {
        Ending=true; Snapshot.Ready=false; Snapshot.Failed=true; Retire();
    }
}
