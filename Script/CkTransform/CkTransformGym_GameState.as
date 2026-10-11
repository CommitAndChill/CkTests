UCLASS()
class ACk_TransformGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkTransformGym_Snapshot Snapshot;
    UPROPERTY() TArray<FCk_Handle> ServerStations;
    UPROPERTY() TArray<ACk_TransformGym_Cube> ServerCubes;
    private FCkGym_SharedStationCohort Cohort;
    private TArray<FCk_Handle> Bridges;
    private TArray<FCk_Handle> Ledger;
    private TArray<FCk_Handle> Parents;
    private TArray<FCk_Handle> Retiring;
    private TArray<TWeakObjectPtr<ACk_TransformGym_Cube>> OldActors;
    private TArray<FCk_Handle_PendingEntityScript> Displays;
    private TArray<int32> DisplayCompleted;
    private TArray<int32> DisplayConstructed;
    private bool Ending = false, Armed = false, Starting = false, RetiringOld = false, Unproven = false;
    private float StartedAt = 0;
    private int32 NextGeneration = 0, CatalogIndex = -1;
    private FCkGym_Entry CatalogEntry;
    private ACk_TransformGym_PlayerController Requester;
    private ACk_Gym_Base_Pawn RequestPawn;
    private int32 RequestSerial = 0, RequestGeneration = 0;
    private FCkTransformGym_ControlCommit Prepared;

    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_CapturedBridgeLedger() const
    { return Ledger; }
    UFUNCTION(BlueprintPure) bool Get_IsRetirementUnproven() const
    { return Unproven || Cohort.Get_IsRetirementUnproven(); }

    bool Get_HasWorldIdentity()
    {
        auto CurrentWorld = GetWorld();
        if (Ending || !HasAuthority() || ck::Is_NOT_Valid(CurrentWorld) || CurrentWorld.GetGameState() != this ||
            GetClass() != ACk_TransformGym_GameState || !Get_HasPublishedCatalog()) { return false; }
        auto Mode = Gameplay::GetGameMode();
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        int32 Current = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != CurrentWorld || Mode.GetClass() != ACk_TransformGym_GameMode ||
            Current < 0 || Current >= Registry.Num()) { return false; }
        auto Entry = Registry[Current];
        if (Entry.DisplayName != "Transform" || Entry.GameModeClass != ACk_TransformGym_GameMode) { return false; }
        return CatalogIndex < 0 || (Current == CatalogIndex && Entry.DisplayName == CatalogEntry.DisplayName &&
            Entry.GameModeClass == CatalogEntry.GameModeClass && Entry.LevelName == CatalogEntry.LevelName &&
            Entry.Category == CatalogEntry.Category && Entry.HintCode == CatalogEntry.HintCode);
    }
    bool Get_HasCapturedOwnerContext(ACk_TransformGym_PlayerController InOwner, ACk_Gym_Base_Pawn Pawn)
    {
        return Get_HasWorldIdentity() && ck::IsValid(InOwner) && InOwner.GetClass() == ACk_TransformGym_PlayerController &&
            InOwner.HasAuthority() && InOwner.GetWorld() == GetWorld() && ck::IsValid(Pawn) && Pawn.GetWorld() == GetWorld() &&
            Pawn.GetController() == InOwner && InOwner.GetControlledPawn() == Pawn;
    }
    bool Get_IsValidOwner(ACk_TransformGym_PlayerController InOwner, ACk_Gym_Base_Pawn Pawn)
    { return Get_HasCapturedOwnerContext(InOwner, Pawn) && !InOwner.OwnerSessionFailed; }
    UFUNCTION(BlueprintPure)
    bool Get_IsCompleteSnapshot(FCkTransformGym_Snapshot S)
    {
        // Retained data remains installable after its old actors have retired; visual resolution is separate.
        if (!S.Ready || S.Failed || !S.FailureReason.IsEmpty() || S.CatalogVersion != CkTransformShared::CatalogVersion || S.Generation <= 0 ||
            S.Sequence <= 0 || S.Stations.Num() != 6 || S.Rows.Num() != 6 || S.StationTitles.Num() != 6 ||
            S.StationBodies.Num() != 6 || S.StationInstructions.Num() != 6) { return false; }
        for (int32 i = 0; i < 6; ++i)
        {
            auto R = S.Rows[i];
            if (!CkTransformShared::Get_IsExactWireParams(S.Stations[i], i) || R.Slot != i || R.Generation != S.Generation ||
                R.Behavior != CkTransformShared::Get_Behavior(i) ||
                !Math::IsFinite(R.Elapsed) || R.Elapsed < 0 || R.Corner < 0 || R.Corner > 3 || R.RotationStep < 0 || R.RotationStep > 3 ||
                R.CornerDistance != 100 || R.ScaleSpeed != 1.5 || R.MinScale != 0.5 || R.MaxScale != 2 ||
                R.CircleRadius != 100 || R.CircleSpeed != 1 || R.RotationSpeed != (i == 5 ? 20 : 45) || R.VectorLength != 150 ||
                S.StationTitles[i].IsEmpty() || S.StationBodies[i].IsEmpty() || S.StationInstructions[i].IsEmpty()) { return false; }
        }
        return true;
    }
    private bool CaptureRoot(FCk_Handle Root, FCk_Handle ExpectedParent, bool Runnable, bool RequireLiveRoot = true)
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Root)) { return !Runnable || !RequireLiveRoot; }
        if (Runnable && RequireLiveRoot && (!CkTransformShared::IsLive(Root) || !utils_entity_lifetime::Has_LifetimeOwner(Root) ||
            utils_entity_lifetime::Get_LifetimeOwner(Root) != ExpectedParent)) { return false; }
        TArray<FCk_Handle> Queue; Queue.Add(Root); TArray<FCk_Handle> Seen;
        for (int32 i = 0; i < Queue.Num(); ++i)
        {
            auto H = Queue[i]; if (Seen.Contains(H)) { continue; } Seen.Add(H);
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { continue; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H, ck::TransientEntity(), Children)) { return false; }
            auto Parent = utils_entity_lifetime::Get_LifetimeOwner(H, ECk_PendingKill_Policy::IncludePendingKill);
            if (!Ledger.Contains(H)) { Ledger.Add(H); Parents.Add(Parent); }
            int32 KnownIndex = Ledger.FindIndex(H);
            if (KnownIndex < 0 || KnownIndex >= Parents.Num()) { return false; }
            // Escaped originally trusted entities retire individually without adopting a foreign new subtree.
            if (Parent != Parents[KnownIndex])
            { if (Runnable) { return false; } continue; }
            for (auto Child : Children) { Queue.Add(Child); }
        }
        return true;
    }
    private bool CaptureAll(bool Runnable)
    {
        if (Ledger.Num() != Parents.Num()) { return false; }
        for (auto Root : Bridges)
        {
            if (!CaptureRoot(Root, ck::TransientEntity(), Runnable)) { return false; }
        }
        // Revisit all historical identities, including additions made during this pass.
        for (int32 i = 0; i < Ledger.Num(); ++i)
        {
            if (Ledger.Num() != Parents.Num() || i >= Parents.Num()) { return false; }
            if (!CaptureRoot(Ledger[i], Parents[i], Runnable, false)) { return false; }
        }
        return true;
    }
    private bool Get_HasSafeCapturedCascade()
    {
        if (Ledger.Num() != Parents.Num()) { return false; }
        for (auto H : Retiring)
        {
            if (!Ledger.Contains(H)) { return false; }
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { continue; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H, ck::TransientEntity(), Children)) { return false; }
            for (auto Child : Children) { if (!Retiring.Contains(Child)) { return false; } }
            // Native destruction walks this filtered read-set too; it can differ from current-owner census.
            if (CkTransformShared::IsLive(H))
            { for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(H))
              { if (!Ledger.Contains(Child) || !Retiring.Contains(Child)) { return false; } } }
        }
        // Actor EndPlay destroys the CURRENT component link, not merely the recorded bridge.
        for (auto Bridge : Bridges)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Bridge) && !Retiring.Contains(Bridge)) { return false; } }
        for (int32 i = 0; i < ServerCubes.Num(); ++i)
        {
            auto Cube = ServerCubes[i];
            if (!ck::IsValid(Cube)) { continue; }
            auto Actual = utils_owning_actor::TryGet_ActorEntityHandle(Cube);
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Actual)) { continue; }
            if (i >= Bridges.Num() || Actual != Bridges[i] || !Retiring.Contains(Actual)) { return false; }
        }
        return true;
    }
    private bool ExactLive()
    {
        if (!Get_HasWorldIdentity() || Cohort.Get_Phase() != 2 || Cohort.Get_HasTerminalFailure() || ServerStations.Num() != 6 ||
            ServerCubes.Num() != 6 || Bridges.Num() != 6 || Displays.Num() != 6 || DisplayCompleted.Num() != 6 || DisplayConstructed.Num() != 6) { return false; }
        TArray<ACk_TransformGym_Cube> All; GetAllActorsOfClass(ACk_TransformGym_Cube, All);
        if (All.Num() != 6) { return false; }
        for (int32 i = 0; i < 6; ++i)
        {
            auto H = ServerStations[i]; auto Cube = ServerCubes[i]; auto D = Displays[i].Get_EntityUnderConstruction();
            if (!CkTransformShared::IsLive(H) || !CkTransformShared::Get_IsExactStationRecipe(H, i) ||
                !H.Has_Fragment(FCkGym_Station_TitleAndDescription) || !H.Has_Fragment(FCkGym_Station_Anchors) ||
                ck::Is_NOT_Valid(Cube) || Cube.GetWorld() != GetWorld() || Cube.GetClass() != ACk_TransformGym_Cube ||
                !Cube.HasAuthority() || !All.Contains(Cube) || Cube.SharedSlot != i || Cube.SharedGeneration != NextGeneration ||
                Cube.Behavior != CkTransformShared::Get_Behavior(i) || Cube.Get_SharedBridge() != Bridges[i] || Cube.EcsEntity != Bridges[i] ||
                !CkTransformShared::IsLive(Bridges[i]) || !CkTransformShared::IsLive(D) || !utils_entity_lifetime::Has_LifetimeOwner(D) ||
                utils_entity_lifetime::Get_LifetimeOwner(D) != H || !utils_entity_script::Has(D) ||
                DisplayCompleted[i] != 1 || DisplayConstructed[i] != 1) { return false; }
            auto Script = utils_entity_script::TryGet_EntityScript(D);
            if (ck::Is_NOT_Valid(Script) || Script.GetClass() != UCk_EntityScript_TransformGym_Display) { return false; }
            auto Display = Cast<UCk_EntityScript_TransformGym_Display>(Script);
            if (Display.LinkedCube.Get() != Cube || utils_net::Get_EntityNetMode(Bridges[i]) == ECk_Net_NetModeType::Client) { return false; }
            auto Tagged = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), CkTransformShared::Get_StationTag(i));
            if (Tagged.Num() != 1 || Tagged[0] != H) { return false; }
        }
        return true;
    }
    UFUNCTION(BlueprintPure) bool Get_IsReady()
    { return Snapshot.Ready && !Snapshot.Failed && !Starting && !RetiringOld && ExactLive(); }
    void Request_EnsureSharedDemo()
    {
        if (!Get_HasWorldIdentity() || Armed || Snapshot.Failed) { return; }
        CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[CatalogIndex];
        Armed = true; System::SetTimer(this, n"OnMonitor", 0.1, true); StartGeneration();
    }
    private void StartGeneration()
    {
        if (NextGeneration >= 2147483646 || Unproven) { Request_Fail("Transform generation/retirement exhausted"); return; }
        ++NextGeneration; Starting = true; StartedAt = float(System::GetGameTimeInSeconds());
        Cohort = FCkGym_SharedStationCohort();
        TArray<FCk_GymStation_SpawnParams> Recipes;
        for (int32 i = 0; i < 6; ++i) { Recipes.Add(CkTransformShared::Get_Params(i)); }
        if (Cohort.TryBegin(ck::TransientEntity(), Recipes, FCk_Delegate_Request_OnCompleted(this,n"OnStationCompleted"),
            FCk_Delegate_EntityScript_Constructed(this,n"OnStationConstructed")) != ECkGym_SharedStationBegin::Started)
        { Request_Fail("Transform station admission rejected"); return; }
    }
    UFUNCTION() private void OnStationCompleted(FCk_Handle H, ECk_Request_OperationResult Result)
    {
        if (Ending || !Starting || Snapshot.Failed) { return; }
        if (!Cohort.TryRecord_Completed(H,H,Result)) { Request_Fail("Transform station completion rejected"); return; }
        TArray<FCk_Handle> Geometry, GeometryParents;
        if (!CkTransformShared::TryGet_RequiredGeometry(H,Geometry,GeometryParents))
        { Request_Fail("Transform station geometry closure rejected"); return; }
        for (int32 i=0; i<Geometry.Num(); ++i)
        { if (!Cohort.TryRequireOwned(Geometry[i],GeometryParents[i]))
          { Request_Fail("Transform geometry enrollment rejected"); return; } }

    }
    UFUNCTION() private void OnStationConstructed(FCk_Handle_EntityScript H)
    { if (!Ending && Starting && !Snapshot.Failed && !Cohort.TryRecord_Constructed(FCk_Handle(H),H)) { Request_Fail("Transform station constructed rejected"); } }
    UFUNCTION() private void OnDisplayCompleted(FCk_Handle H, ECk_Request_OperationResult Result)
    {
        if (Ending || Snapshot.Failed || !Starting) { return; }
        if (!CkTransformShared::IsLive(H)) { Request_Fail("Transform invalid display completion"); return; }
        if (ServerStations.Num()!=6 || Displays.Num()!=6 || DisplayCompleted.Num()!=6 || DisplayConstructed.Num()!=6) { Request_Fail("Transform display arrays invalid"); return; }
        for (int32 i=0;i<6;++i)
        {
            if (Displays[i].Get_EntityUnderConstruction()!=H) { continue; }
            if (DisplayCompleted[i]!=0 || Result!=ECk_Request_OperationResult::Succeeded) { Request_Fail("Transform display completion invalid"); return; }
            DisplayCompleted[i]=1; return;
        }
        Request_Fail("Transform foreign display completion");
    }
    UFUNCTION() private void OnDisplayConstructed(FCk_Handle_EntityScript Script)
    {
        if (Ending || Snapshot.Failed || !Starting) { return; }
        if (ServerStations.Num()!=6 || Displays.Num()!=6 || DisplayCompleted.Num()!=6 || DisplayConstructed.Num()!=6) { Request_Fail("Transform display arrays invalid"); return; }
        auto H=FCk_Handle(Script);
        if (!CkTransformShared::IsLive(H)) { Request_Fail("Transform invalid constructed display"); return; }
        for (int32 i=0;i<6;++i)
        {
            if (Displays[i].Get_EntityUnderConstruction()!=H) { continue; }
            if (DisplayConstructed[i]!=0 || !Cohort.TryRequireOwned(H,ServerStations[i])) { Request_Fail("Transform display identity invalid"); return; }
            DisplayConstructed[i]=1; return;
        }
        Request_Fail("Transform foreign constructed display");
    }
    private bool SpawnActors()
    {
        if (ServerCubes.Num()!=0 || Bridges.Num()!=0 || Cohort.Stations.Num()!=6) { return false; }
        ServerStations=Cohort.Stations;
        for (int32 i=0;i<6;++i) { Displays.Add(FCk_Handle_PendingEntityScript()); DisplayCompleted.Add(0); DisplayConstructed.Add(0); }
        for (int32 i=0;i<6;++i)
        {
            auto H=ServerStations[i];
            if (!CkTransformShared::IsLive(H) || !H.Has_Fragment(FCkGym_Station_Anchors)) { return false; }
            auto Anchors=H.Get_Fragment(FCkGym_Station_Anchors);
            auto Cube=Cast<ACk_TransformGym_Cube>(SpawnActor(ACk_TransformGym_Cube,Anchors.PanelCenter,FRotator(0,180,0),NAME_None,true));
            if (ck::Is_NOT_Valid(Cube)) { return false; }
            ServerCubes.Add(Cube); Cube.SharedSlot=i; Cube.SharedGeneration=NextGeneration; Cube.Behavior=CkTransformShared::Get_Behavior(i);
            FinishSpawningActor(Cube);
            auto Bridge=Cube.Get_SharedPendingBridge(); Bridges.Add(Bridge);
            if (!CaptureRoot(Bridge,ck::TransientEntity(),true)) { return false; }
            FTransformGym_DisplaySpawnParams Params; Params.InitialTransform=FTransform(FRotator(0,180,0),Anchors.PanelCenter); Params.LinkedCube=Cube;
            auto Pending=utils_entity_script::Request_SpawnEntity(H,UCk_EntityScript_TransformGym_Display,FInstancedStruct::Make(Params),
                FCk_Delegate_Request_OnCompleted(this,n"OnDisplayCompleted"));
            if (!utils_pending_entity_script::Get_IsValid(Pending)) { return false; }
            Displays[i]=Pending;
            utils_pending_entity_script::Promise_OnConstructed(Pending,FCk_Delegate_EntityScript_Constructed(this,n"OnDisplayConstructed"));
        }
        return true;
    }
    private bool BuildSample(FCkTransformGym_Snapshot& Out)
    {
        Out=FCkTransformGym_Snapshot();
        if (!ExactLive() || Snapshot.Sequence>=2147483646) { return false; }
        FCkTransformGym_Snapshot Candidate; Candidate.CatalogVersion=CkTransformShared::CatalogVersion;
        Candidate.Generation=NextGeneration; Candidate.Sequence=Snapshot.Sequence+1; Candidate.Ready=true;
        for (int32 i=0;i<6;++i)
        {
            auto Cube=ServerCubes[i]; auto Text=ServerStations[i].Get_Fragment(FCkGym_Station_TitleAndDescription);
            FCkTransformGym_Row R; R.Slot=i; R.Generation=NextGeneration; R.Behavior=Cube.Behavior; R.Cube=Cube;
            R.Corner=Cube.CurrentCorner; R.RotationStep=Cube.CurrentRotationStep;
            R.Elapsed=Cube.ElapsedTime; R.CornerDistance=Cube.CornerOffsetDistance; R.ScaleSpeed=Cube.ScaleSpeed;
            R.MinScale=Cube.MinScale; R.MaxScale=Cube.MaxScale; R.CircleRadius=Cube.CircleRadius; R.CircleSpeed=Cube.CircleSpeed;
            R.RotationSpeed=Cube.RotationSpeed; R.VectorLength=Cube.VectorDrawLength;
            Candidate.Rows.Add(R); Candidate.Stations.Add(CkTransformShared::Get_Params(i));
            Candidate.StationTitles.Add(Text.Title.ToString()); Candidate.StationBodies.Add(Text.Description.ToString()); Candidate.StationInstructions.Add(Text.Instructions.ToString());
        }
        if (!Get_IsCompleteSnapshot(Candidate)) { return false; }
        Out=Candidate; return true;
    }
    bool Request_Rebuild(ACk_TransformGym_PlayerController InOwner, ACk_Gym_Base_Pawn Pawn, int32 Serial, int32 Generation)
    {
        if (!Get_IsReady() || !Get_IsValidOwner(InOwner,Pawn) || RequestSerial!=0 || Serial<=0 || Generation!=Snapshot.Generation ||
            !InOwner.Get_CanAcceptOwnerCommit() || !InOwner.Get_HasReservedTransformControl(Serial,Generation,Pawn)) { return false; }
        Requester=InOwner; RequestPawn=Pawn; RequestSerial=Serial; RequestGeneration=Generation;
        Snapshot.Ready=false; ForceNetUpdate(); Retire();
        if (Unproven || Snapshot.Failed) { return false; }
        RetiringOld=true; StartedAt=float(System::GetGameTimeInSeconds()); return true;
    }
    bool Request_TryGetPreparedTransformCommit(ACk_TransformGym_PlayerController InOwner, int32 Serial, FCkTransformGym_ControlCommit& Out)
    {
        Out=FCkTransformGym_ControlCommit();
        if (!Starting || !Get_IsValidOwner(InOwner,RequestPawn) || InOwner!=Requester || Serial!=RequestSerial ||
            !InOwner.Get_HasReservedTransformControl(Serial,RequestGeneration,RequestPawn) || Prepared.RequestSerial!=Serial ||
            Prepared.Owner!=InOwner || Prepared.OwnerPawn!=RequestPawn || Prepared.OriginState!=this || Prepared.OriginGeneration!=RequestGeneration ||
            Prepared.DeliverySerial!=InOwner.Get_NextDeliverySerial() || Prepared.Snapshot.Generation!=RequestGeneration+1 ||
            !Get_IsCompleteSnapshot(Prepared.Snapshot)) { return false; }
        Out=Prepared; return true;
    }
    private bool Get_IsRetirementBlocked()
    {
        return Unproven || Cohort.Get_HasTerminalFailure() ||
            Cohort.Get_IsRetirementUnproven() ||
            CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity());
    }
    private void Retire()
    {
        // A closed world or opaque station cohort grants no bridge/actor destruction.
        if (Get_IsRetirementBlocked())
        { Unproven=true; Request_Fail("Transform retirement already blocked"); return; }
        // No destroy starts until both independent station and external bridge ledgers have been captured.
        if (!CaptureAll(false)) { Unproven=true; }
        for (auto H:Ledger) { if (!Retiring.Contains(H)) { Retiring.Add(H); } }
        for (auto Cube:ServerCubes) { if (ck::IsValid(Cube)) { OldActors.Add(Cube); } }
        // No actor/component or entity cascade may start from an incomplete/foreign closure.
        if (Unproven || !Get_HasSafeCapturedCascade())
        {
            Unproven=true;
            Request_Fail("Transform retirement closure is unproven or contains foreign descendants");
            return;
        }
        // Common owns station retirement; its independent callback/cascade policy is not changed here.
        Cohort.Request_Retire();
        // Common may discover an unknown member during its own synchronous
        // callbacks and close admission. Its terminal state is not a grant
        // to continue destroying the separately captured bridge cascade.
        if (Get_IsRetirementBlocked())
        { Unproven=true; Request_Fail("Transform station retirement became unproven"); return; }
        if (!Request_RetireCapturedLeaves()) { Unproven=true; Request_Fail("Transform retirement callback changed captured closure"); return; }
        if (Get_IsRetirementBlocked())
        { Unproven=true; Request_Fail("Transform bridge retirement became unproven"); return; }
        for (auto Cube:ServerCubes)
        {
            if (Get_IsRetirementBlocked())
            { Unproven=true; Request_Fail("Transform actor retirement became unproven"); return; }
            if (!ck::IsValid(Cube)) { continue; }
            // Revalidate after EACH callback. EndPlay must not begin any fresh live bridge cascade.
            auto Actual = utils_owning_actor::TryGet_ActorEntityHandle(Cube);
            if (!Get_HasSafeCapturedCascade() ||
                (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Actual) && CkTransformShared::IsLive(Actual)))
            { Unproven=true; Request_Fail("Transform actor retirement bridge is not safely pending"); return; }
            Cube.DestroyActor();
        }
    }
    private bool Request_RetireCapturedLeaves()
    {
        // Destroy leaf-to-root. Native destruction recursively walks children before synchronous callbacks.
        // Already pending children make that recursion a no-op; callbacks cannot authorize new members.
        for (;;)
        {
            bool HasLive = false; bool Progress = false;
            for (auto H : Retiring)
            {
                if (Get_IsRetirementBlocked()) { return false; }
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) || !CkTransformShared::IsLive(H)) { continue; }
                HasLive = true;
                if (!Get_HasSafeCapturedCascade()) { return false; }
                TArray<FCk_Handle> Children;
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H, ck::TransientEntity(), Children)) { return false; }
                bool ChildrenPending = true;
                for (auto Child : Children)
                {
                    if (!Retiring.Contains(Child)) { return false; }
                    if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child) && CkTransformShared::IsLive(Child)) { ChildrenPending = false; }
                }
                for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(H))
                {
                    if (!Ledger.Contains(Child) || !Retiring.Contains(Child)) { return false; }
                    if (CkTransformShared::IsLive(Child)) { ChildrenPending = false; }
                }
                if (!ChildrenPending) { continue; }
                if (Get_IsRetirementBlocked()) { return false; }
                utils_entity_lifetime::Request_DestroyEntity(H);
                if (Get_IsRetirementBlocked() || CkTransformShared::IsLive(H) ||
                    !Get_HasSafeCapturedCascade()) { return false; }
                Progress = true;
            }
            if (!HasLive) { return Get_HasSafeCapturedCascade(); }
            if (!Progress) { return false; }
        }
        return false; // AngelScript requires a syntactic return after the open-ended loop.
    }
    private bool IsRetired()
    {
        if (Unproven || Cohort.Get_IsRetirementUnproven() || !Cohort.PollRetirement()) { return false; }
        for (auto H:Retiring) { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return false; } }
        for (auto Weak:OldActors) { if (ck::IsValid(Weak.Get())) { return false; } }
        TArray<ACk_TransformGym_Cube> All; GetAllActorsOfClass(ACk_TransformGym_Cube,All);
        if (All.Num()!=0) { return false; }
        ServerStations.Empty(); ServerCubes.Empty(); Bridges.Empty(); Displays.Empty(); DisplayCompleted.Empty(); DisplayConstructed.Empty();
        Ledger.Empty(); Parents.Empty(); Retiring.Empty(); OldActors.Empty(); return true;
    }
    UFUNCTION() private void OnMonitor()
    {
        if (Ending || Snapshot.Failed) { return; }
        if (!Get_HasWorldIdentity()) { Request_Fail("Transform frozen world/catalog changed"); return; }
        if (RequestSerial>0 && !Get_IsValidOwner(Requester,RequestPawn))
        {
            if (ck::IsValid(Requester)) { Requester.Request_DetachTransformControl(this,RequestPawn,RequestSerial,RequestGeneration); }
            Requester=nullptr; RequestPawn=nullptr; RequestSerial=0; RequestGeneration=0; Prepared=FCkTransformGym_ControlCommit();
        }
        if (Starting || RetiringOld)
        { if (float(System::GetGameTimeInSeconds())-StartedAt>20) { Request_Fail("Transform construction/retirement timeout"); return; } }
        if (RetiringOld)
        {
            if (!IsRetired()) { return; }
            RetiringOld=false; StartGeneration(); return;
        }
        // A mature retained station can be pending destruction. Detect it
        // through the nonmutating live proof before Common's PollConstruction
        // would terminally latch and bypass this caller's frozen retire.
        if (!Starting && !ExactLive())
        { Request_Fail("Transform required actor/bridge/display lost"); return; }
        auto Poll=Cohort.PollConstruction();
        if (Poll==ECkGym_SharedStationPoll::Failed) { Request_Fail("Transform station/required geometry lost"); return; }
        if (Poll!=ECkGym_SharedStationPoll::Ready) { return; }
        if (ServerCubes.Num()==0)
        { if (!SpawnActors()) { Request_Fail("Transform whole actor/display allocation failed"); } return; }
        if (!CaptureAll(true)) { Request_Fail("Transform external bridge closure changed"); return; }
        if (Starting && !ExactLive()) { return; }
        if (!Starting && !ExactLive()) { Request_Fail("Transform required actor/bridge/display lost"); return; }
        FCkTransformGym_Snapshot Candidate;
        if (!BuildSample(Candidate))
        { if (!Starting) { Request_Fail("Transform complete source sample invalid"); } return; }
        if (Starting && RequestSerial>0)
        {
            if (!Get_IsValidOwner(Requester,RequestPawn) || !Requester.Get_CanAcceptOwnerCommit()) { Request_Fail("Transform owner slot changed"); return; }
            Prepared=FCkTransformGym_ControlCommit(); Prepared.Owner=Requester; Prepared.OwnerPawn=RequestPawn; Prepared.OriginState=this;
            Prepared.OriginGeneration=RequestGeneration; Prepared.RequestSerial=RequestSerial; Prepared.DeliverySerial=Requester.Get_NextDeliverySerial(); Prepared.Snapshot=Candidate;
            if (Prepared.DeliverySerial<=0) { Request_Fail("Transform retained whole commit rejected"); return; }
        }
        // Keep Starting true while recording: this snapshot is the actual
        // source, but Get_IsReady remains false until the owner audit and
        // accepted delivery are committed together.
        Snapshot=Candidate;
        if (RequestSerial>0 && !Requester.Request_TryRecordTransformCommit(this,RequestSerial))
        { Request_Fail("Transform retained whole commit/source publication rejected"); return; }
        Starting=false; ForceNetUpdate();
        Prepared=FCkTransformGym_ControlCommit(); Requester=nullptr; RequestPawn=nullptr; RequestSerial=0; RequestGeneration=0;
    }
    void Request_Fail(FString Reason)
    {
        if (Snapshot.Failed) { return; }
        Snapshot.Ready=false; Snapshot.Failed=true; Snapshot.FailureReason=Reason;
        if (ck::IsValid(Requester) && RequestSerial>0) { Requester.Request_RejectTransformControl(RequestSerial,3); }
        Starting=false; Prepared=FCkTransformGym_ControlCommit(); Retire(); ForceNetUpdate();
    }
    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason Reason)
    { if (Ending) { return; } Ending=true; Snapshot.Ready=false; Snapshot.Failed=true; Retire(); }
}
