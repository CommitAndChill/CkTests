// Language=angelscript

class ACk_EntityLifecycleGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 10.0;
    UPROPERTY(Replicated) FCkLifecycleGym_Snapshot Snapshot;
    UPROPERTY(Replicated) ACk_LifecycleGym_BridgeProbe BridgeProbe;
    UPROPERTY() TArray<FCk_Handle> ServerChildren;
    UPROPERTY() TArray<int32> BranchGenerations;
    UPROPERTY() FCk_Handle ServerBridgeRoot;
    UPROPERTY() int32 ServerCohortCount = 0;
    private FCk_Handle _StationOwner;
    private ACk_LifecycleGym_BridgeProbe _OwnedProbe;
    private FCk_Handle _AdmissionOwner;
    private int32 _AdmissionTicket = 0;
    private TArray<FCk_Handle> _Stations;
    private TArray<FCk_Handle_PendingEntityScript> _PendingStations;
    private TArray<bool> _StationConstructed;
    private TArray<FCk_Handle_PendingEntityScript> _PendingChildren;
    private FCk_Handle_PendingEntityScript _PendingRoot;
    private TArray<bool> _Constructed;
    private TArray<FTransform> _BranchTransforms;
    private TArray<FCk_Handle> _Retiring;
    private bool _Starting = false;
    private bool _StartupBlockerReported = false;
    private FString _StartupBlockerReason;
    private bool _Ending = false;
    private bool _Armed = false;
    private bool _Spawning = false;
    private bool _RetirementPending = false;
    private int32 _Rerun = -1;
    private float _StartTime = 0.0f;
    private ACk_EntityLifecycleGym_PlayerController _Owner;
    private ACk_Gym_Base_Pawn _Pawn;
    private int32 _Serial = 0;
    private int32 _Row = 0;
    private FCkLifecycleGym_ControlCommit _Prepared;
    private int32 _CatalogIndex = -1;
    private FCkGym_Entry _CatalogEntry;

    bool Get_IsCurrentBranch(int32 Index, int32 Generation)
    {
        return !_Ending && !Snapshot.Failed && HasAuthority() &&
            Index >= 0 && Index < BranchGenerations.Num() && Generation > 0 && BranchGenerations[Index] == Generation;
    }

    private bool HasWorldIdentity()
    {
        auto W = GetWorld();
        if (_Ending || !HasAuthority() || ck::Is_NOT_Valid(W) || W.GetGameState() != this ||
            GetClass() != ACk_EntityLifecycleGym_GameState || !Get_HasPublishedCatalog()) { return false; }
        auto M = Gameplay::GetGameMode();
        TArray<ACk_EntityLifecycleGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_EntityLifecycleGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        int32 Current = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(M) || Modes.Num() != 1 || Modes[0] != M || M.GetWorld() != W || M.GetClass() != ACk_EntityLifecycleGym_GameMode ||
            Current < 0 || Current >= Registry.Num() || Registry[Current].DisplayName != "Entity Lifecycle" ||
            Registry[Current].GameModeClass != ACk_EntityLifecycleGym_GameMode) { return false; }
        if (_CatalogIndex >= 0)
        {
            auto E = Registry[Current];
            return Current == _CatalogIndex && E.DisplayName == _CatalogEntry.DisplayName &&
                E.GameModeClass == _CatalogEntry.GameModeClass && E.LevelName == _CatalogEntry.LevelName &&
                E.Category == _CatalogEntry.Category && E.HintCode == _CatalogEntry.HintCode;
        }
        return true;
    }

    bool Get_IsValidOwner(ACk_EntityLifecycleGym_PlayerController Owner, ACk_Gym_Base_Pawn Pawn)
    {
        return HasWorldIdentity() && ck::IsValid(Owner) && Owner.HasAuthority() &&
            Owner.GetClass() == ACk_EntityLifecycleGym_PlayerController && Owner.GetWorld() == GetWorld() &&
            ck::IsValid(Pawn) && Pawn.GetWorld() == GetWorld() && Pawn.GetController() == Owner && Owner.GetControlledPawn() == Pawn;
    }

    void Request_EnsureSharedDemo()
    {
        if (_Ending || _Starting || Snapshot.Ready || Snapshot.Failed || !HasWorldIdentity()) { return; }
        Snapshot.Generation = 1;
        _CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        _CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[_CatalogIndex];
        for (int32 i = 0; i < 7; ++i)
        {
            BranchGenerations.Add(1); ServerChildren.Add(FCk_Handle());
            _PendingChildren.Add(FCk_Handle_PendingEntityScript()); _Constructed.Add(false);
            _BranchTransforms.Add(FTransform::Identity);
        }
        _StartupBlockerReported = false; _StartupBlockerReason = ""; _Starting = true; _StartTime = float(System::GetGameTimeInSeconds());
        if (!_Armed) { _Armed = true; System::SetTimer(this, n"OnMonitor", 0.1, true); }
        Request_CreateStations();
    }

    private void Request_CreateStations()
    {
        auto World = ck::TransientEntity();
        if (!CkLifecycleGym::IsLive(World) || !utils_entity_lifetime::Get_CanCreateEntity(World))
        { Request_Fail("World station owner unavailable"); return; }
        if (CkGym_Admission::Get_IsClosedUnproven(World))
        { Request_Fail("Station admission permanently closed"); return; }
        if (CkGym_Admission::Get_IsBusy(World)) { return; }
        auto Declarations = CkLifecycleGym::Get_Stations();
        if (Declarations.Num() != 7) { Request_Fail("Lifecycle declarations changed"); return; }
        for (auto D : Declarations)
        {
            if (D.Tags.Num() != 1 || D.Tags[0] == FName() || utils_entity_tag::ForEach_Entity(World, D.Tags[0]).Num() != 0)
            { Request_Fail("Lifecycle world station collision"); return; }
        }
        _AdmissionTicket = CkGym_Admission::TryReserve(World);
        if (_AdmissionTicket == 0)
        { Request_Fail("Station admission reservation rejected"); return; }
        _AdmissionOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!CkLifecycleGym::IsLive(_AdmissionOwner)) { Request_Fail("Admission allocation failed"); return; }
        if (!CkGym_Admission::TryCommitOwner(World, _AdmissionTicket, _AdmissionOwner))
        { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
          Request_Fail("Station admission owner commit rejected"); return; }
        _StationOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!CkLifecycleGym::IsLive(_StationOwner)) { Request_Fail("Station owner allocation failed"); return; }
        for (auto D : Declarations)
        {
            auto Pending = CkGym_Common::Request_SpawnNewStationForOwner(D, _StationOwner,
                FCk_Delegate_Request_OnCompleted(this, n"OnStationComplete"));
            if (!utils_pending_entity_script::Get_IsValid(Pending)) { Request_Fail("Station request rejected"); return; }
            _PendingStations.Add(Pending);
            _StationConstructed.Add(false);
            utils_pending_entity_script::Promise_OnConstructed(Pending, FCk_Delegate_EntityScript_Constructed(this, n"OnStationConstructed"));
        }
    }

    UFUNCTION() private void OnStationConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || Snapshot.Failed) { return; }
        auto H = FCk_Handle(InHandle);
        for (int32 i = 0; i < _PendingStations.Num(); ++i)
        {
            if (_PendingStations[i].Get_EntityUnderConstruction() != H) { continue; }
            if (_StationConstructed[i] || !CkLifecycleGym::IsDirect(H, _StationOwner))
            { Request_Fail("Station callback identity rejected"); return; }
            _StationConstructed[i] = true; return;
        }
    }

    UFUNCTION() private void OnStationComplete(FCk_Handle H, ECk_Request_OperationResult Result)
    {
        if (!_Starting || Snapshot.Failed) { return; }
        for (auto Pending : _PendingStations)
        {
            if (Pending.Get_EntityUnderConstruction() == H && Result != ECk_Request_OperationResult::Succeeded)
            { Request_Fail("Station construction failed"); return; }
        }
    }

    private bool ReleaseAdmission(bool InPublishedComplete = false)
    {
        auto World = ck::TransientEntity();
        if (_AdmissionTicket == 0 && CkGym_Admission::Get_IsClosedUnproven(World))
        { return false; }
        if (_AdmissionTicket != 0)
        {
            if (InPublishedComplete)
            {
                if (!CkGym_Admission::TryReleasePublished(World, _AdmissionTicket, _AdmissionOwner))
                { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
                  CkGym_Admission::TryCloseWorldUnproven(World); return false; }
                _AdmissionTicket = 0;
            }
            else
            {
                CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
                CkGym_Admission::TryCloseWorldUnproven(World);
                return false;
            }
        }
        if (CkLifecycleGym::IsLive(_AdmissionOwner)) { utils_entity_lifetime::Request_DestroyEntity(_AdmissionOwner); }
        _AdmissionOwner = FCk_Handle();
        return true;
    }

    private bool CheckStations()
    {
        auto D = CkLifecycleGym::Get_Stations();
        if (_PendingStations.Num() != 7 || D.Num() != 7) { return StartupBlocked("private bool CheckStations checkpoint1: if (_PendingStations.Num() != 7 || D.Num() != 7)"); }
        TArray<FCk_Handle> Candidate;
        for (int32 i = 0; i < 7; ++i)
        {
            if (i >= _StationConstructed.Num() || !_StationConstructed[i]) { return StartupBlocked(f"private bool CheckStations checkpoint2: branch={i} if (i >= _StationConstructed.Num() || !_StationConstructed[i])"); }
            auto H = CkLifecycleGym::FindStation(D[i].Tags[0]);
            if (!CkLifecycleGym::IsDirect(H, _StationOwner) || H != _PendingStations[i].Get_EntityUnderConstruction() ||
                !utils_entity_script::Has(H)) { return StartupBlocked(f"private bool CheckStations checkpoint3: branch={i} if (!CkLifecycleGym::IsDirect(H, _StationOwner) || H != _PendingStations[i].Get_EntityUnderConstruction() || !utils_entity_script::Has(H))"); }
            auto S = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
            if (ck::Is_NOT_Valid(S) || S.GetClass() != UCk_EntityScript_GymStation) { return StartupBlocked(f"private bool CheckStations checkpoint4: branch={i} if (ck::Is_NOT_Valid(S) || S.GetClass() != UCk_EntityScript_GymStation)"); }
            auto A = H.Get_Fragment(FCkGym_Station_Anchors);
            if (A.FootprintCenter != D[i].Transform.GetLocation()) { return StartupBlocked(f"private bool CheckStations checkpoint5: branch={i} if (A.FootprintCenter != D[i].Transform.GetLocation())"); }
            Candidate.Add(H);
        }
        _Stations = Candidate; if (!ReleaseAdmission(true)) { Request_Fail("Station admission publication rejected"); return false; } return true;
    }

    private void SpawnProbe()
    {
        if (_Stations.Num() != 7 || !CkLifecycleGym::IsLive(_Stations[3])) { Request_Fail("Probe station absent"); return; }
        BridgeProbe = Cast<ACk_LifecycleGym_BridgeProbe>(SpawnActor(ACk_LifecycleGym_BridgeProbe,
            FVector(500.0, 0.0, 0.0), FRotator(0.0, 180.0, 0.0)));
        if (ck::Is_NOT_Valid(BridgeProbe) || BridgeProbe.GetWorld() != GetWorld()) { Request_Fail("Probe actor allocation failed"); return; }
        _OwnedProbe = BridgeProbe;
        BridgeProbe.BranchGeneration = BranchGenerations[3];
        _PendingRoot = utils_entity_script_with_actor::Request_SpawnEntityScript_OnActor(BridgeProbe, UCk_EntityScript_LifecycleGym_BridgeRoot);
        if (!utils_pending_entity_script::Get_IsValid(_PendingRoot)) { Request_Fail("Probe root rejected"); return; }
        utils_pending_entity_script::Promise_OnConstructed(_PendingRoot, FCk_Delegate_EntityScript_Constructed(this, n"OnRootConstructed"));
    }

    UFUNCTION() private void OnRootConstructed(FCk_Handle_EntityScript InRoot)
    {
        if (!_Starting || Snapshot.Failed || FCk_Handle(InRoot) != _PendingRoot.Get_EntityUnderConstruction()) { return; }
        auto H = FCk_Handle(InRoot);
        if (!CkLifecycleGym::IsLive(H) || ck::Is_NOT_Valid(BridgeProbe) || !utils_owning_actor::Has(H) ||
            utils_owning_actor::TryGet_ActorEntityHandle(BridgeProbe) != H ||
            utils_owning_actor::TryGet_EntityOwningActor(H) != BridgeProbe || !utils_entity_replication_driver::Has(H) ||
            !utils_net::Has(H) || utils_net::Get_EntityReplication(H) != ECk_Replication::Replicates)
        { Request_Fail("Actual probe root invalid"); return; }
        auto S = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
        if (ck::Is_NOT_Valid(S) || S.GetClass() != UCk_EntityScript_LifecycleGym_BridgeRoot)
        { Request_Fail("Probe root class mismatch"); return; }
        ServerBridgeRoot = H; SpawnBranch(3);
    }

    private void SpawnBranch(int32 i)
    {
        if (i < 0 || i >= 7 || _BranchTransforms.Num() != 7 || _Stations.Num() != 7 || !CkLifecycleGym::IsLive(_Stations[i]) || !_Stations[i].Has_Fragment(FCkGym_Station_Anchors))
        { Request_Fail("Original station missing"); return; }
        auto Owner = i == 3 ? _PendingRoot.Get_EntityUnderConstruction() : _Stations[i];
        if (!CkLifecycleGym::IsLive(Owner) || !utils_entity_lifetime::Get_CanCreateEntity(Owner)) { Request_Fail("Original owner unavailable"); return; }
        auto A = _Stations[i].Get_Fragment(FCkGym_Station_Anchors);
        if (!CkLifecycleGym::AreAnchorsValid(A, i)) { Request_Fail("Original station anchors invalid"); return; }
        auto Params = FCkLifecycleGym_Params();
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0), A.PanelCenter);
        _BranchTransforms[i] = Params.InitialTransform;
        Params.SharedAuthority = true; Params.BranchIndex = i; Params.BranchGeneration = BranchGenerations[i];
        FCk_Handle_PendingEntityScript Pending;
        if (i == 0) { Pending = utils_entity_script::Request_SpawnEntity(Owner, UCk_EntityScript_EntityLifecycleGym_HandleAndEntity, FInstancedStruct::Make(Params)); }
        if (i == 1) { Pending = utils_entity_script::Request_SpawnEntity(Owner, UCk_EntityScript_EntityLifecycleGym_OwnershipTree, FInstancedStruct::Make(Params)); }
        if (i == 2) { Pending = utils_entity_script::Request_SpawnEntity(Owner, UCk_EntityScript_EntityLifecycleGym_DestroyCallbacks, FInstancedStruct::Make(Params)); }
        if (i == 3) { Pending = utils_entity_script::Request_SpawnEntity(Owner, UCk_EntityScript_EntityLifecycleGym_ActorBridge, FInstancedStruct::Make(Params)); }
        if (i == 4) { Pending = utils_entity_script::Request_SpawnEntity(Owner, UCk_EntityScript_EntityLifecycleGym_TagSystem, FInstancedStruct::Make(Params)); }
        if (i == 5) { Pending = utils_entity_script::Request_SpawnEntity(Owner, UCk_EntityScript_EntityLifecycleGym_DeferredSetup, FInstancedStruct::Make(Params)); }
        if (i == 6) { Pending = utils_entity_script::Request_SpawnEntity(Owner, UCk_EntityScript_EntityLifecycleGym_ScriptSpawnCast, FInstancedStruct::Make(Params)); }

        if (!utils_pending_entity_script::Get_IsValid(Pending)) { Request_Fail("Original spawn request rejected"); return; }
        _PendingChildren[i] = Pending;
        utils_pending_entity_script::Promise_OnConstructed(Pending, FCk_Delegate_EntityScript_Constructed(this, n"OnChildConstructed"));
    }

    UFUNCTION() private void OnChildConstructed(FCk_Handle_EntityScript InChild)
    {
        if (!_Starting || Snapshot.Failed) { return; }
        auto H = FCk_Handle(InChild);
        for (int32 i = 0; i < 7; ++i)
        {
            if (H != _PendingChildren[i].Get_EntityUnderConstruction()) { continue; }
            if (!CkLifecycleGym::IsLive(H) || _Constructed[i]) { Request_Fail("Original callback invalid or duplicated"); return; }
            ServerChildren[i] = H; _Constructed[i] = true; return;
        }
    }

    // Diagnostic only: same rejection, once per startup after twenty game seconds.
    private bool StartupBlocked(FString InReason)
    {
        if (_Starting && !_StartupBlockerReported &&
            float(System::GetGameTimeInSeconds()) - _StartTime > 20.0f)
        {
            _StartupBlockerReported = true;
            _StartupBlockerReason = InReason;
            ck::Trace(f"[LifecycleStartupBlocker] generation={Snapshot.Generation} rerun={_Rerun} reason={InReason}");
        }
        return false;
    }

    private bool SampleBranch(int32 i, FCkLifecycleGym_RowSample& Out)
    {
        Out = FCkLifecycleGym_RowSample();
        if (i < 0 || i >= ServerChildren.Num() || i >= _Stations.Num() || !_Constructed[i]) { return StartupBlocked(f"private bool SampleBranch checkpoint1: branch={i} if (i < 0 || i >= ServerChildren.Num() || i >= _Stations.Num() || !_Constructed[i])"); }
        auto H = ServerChildren[i]; auto Owner = i == 3 ? ServerBridgeRoot : _Stations[i];
        // Preserve the original short-circuit order; name the exact rejected prerequisite.
        if (!CkLifecycleGym::IsDirect(H, Owner))
        { return StartupBlocked(f"SampleBranch structure: branch={i} child is not a live direct dependent"); }
        if (!CkLifecycleGym::IsDirect(_Stations[i], _StationOwner))
        { return StartupBlocked(f"SampleBranch structure: branch={i} station is not a live direct dependent"); }
        if (!_Stations[i].Has_Fragment(FCkGym_Station_Anchors))
        { return StartupBlocked(f"SampleBranch structure: branch={i} station has no anchors"); }
        if (!utils_entity_script::Has(H))
        { return StartupBlocked(f"SampleBranch structure: branch={i} child has no entity script"); }
        if (!utils_net::Has(H))
        { return StartupBlocked(f"SampleBranch structure: branch={i} child has no net params"); }
        if (utils_net::Get_EntityReplication(H) != ECk_Replication::DoesNotReplicate)
        { return StartupBlocked(f"SampleBranch structure: branch={i} child net params are not DoesNotReplicate"); }
        auto StationScript = UCk_Utils_EntityScript_UE::TryGet_EntityScript(_Stations[i]);
        if (ck::Is_NOT_Valid(StationScript) || StationScript.GetClass() != UCk_EntityScript_GymStation) { return StartupBlocked(f"private bool SampleBranch checkpoint3: branch={i} if (ck::Is_NOT_Valid(StationScript) || StationScript.GetClass() != UCk_EntityScript_GymStation)"); }
        auto Anchor = _Stations[i].Get_Fragment(FCkGym_Station_Anchors);
        if (!CkLifecycleGym::AreAnchorsValid(Anchor, i)) { return StartupBlocked(f"private bool SampleBranch checkpoint4: branch={i} if (!CkLifecycleGym::AreAnchorsValid(Anchor, i))"); }
        auto Matches = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), CkLifecycleGym::ChildTag(i));
        if (Matches.Num() != 1 || Matches[0] != H) { return StartupBlocked(f"private bool SampleBranch checkpoint5: branch={i} if (Matches.Num() != 1 || Matches[0] != H)"); }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
        auto Demo = Cast<UCkLifecycleGym_DemoBase>(Base);
        if (ck::Is_NOT_Valid(Demo) || !Demo.SharedAuthority || Demo.BranchIndex != i || Demo.BranchGeneration != BranchGenerations[i] ||
            !Demo.CanRun(H) || Demo.Failed || Demo.Closed) { return StartupBlocked(f"private bool SampleBranch checkpoint6: branch={i} if (ck::Is_NOT_Valid(Demo) || !Demo.SharedAuthority || Demo.BranchIndex != i || Demo.BranchGeneration != BranchGenerations[i] || !Demo.CanRun(H) || Demo.Failed || Demo.Closed)"); }
        if (i == 0)
        {
            if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_HandleAndEntity) { return StartupBlocked(f"private bool SampleBranch checkpoint7: branch={i} if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_HandleAndEntity)"); }
            auto S = Cast<UCk_EntityScript_EntityLifecycleGym_HandleAndEntity>(Base);
            if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D()) { return StartupBlocked(f"private bool SampleBranch checkpoint8: branch={i} if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D())"); }
            S.Request_UpdateDisplay();
        }
        if (i == 1)
        {
            if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_OwnershipTree) { return StartupBlocked(f"private bool SampleBranch checkpoint9: branch={i} if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_OwnershipTree)"); }
            auto S = Cast<UCk_EntityScript_EntityLifecycleGym_OwnershipTree>(Base);
            if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D()) { return StartupBlocked(f"private bool SampleBranch checkpoint10: branch={i} if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D())"); }
            S.Request_UpdateDisplay();
            if (!CkLifecycleGym::IsDirect(S.ChildA, H) || !CkLifecycleGym::IsDirect(S.ChildB, H)) { return StartupBlocked(f"private bool SampleBranch checkpoint11: branch={i} if (!CkLifecycleGym::IsDirect(S.ChildA, H) || !CkLifecycleGym::IsDirect(S.ChildB, H))"); }
        }
        if (i == 2)
        {
            if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_DestroyCallbacks) { return StartupBlocked(f"private bool SampleBranch checkpoint12: branch={i} if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_DestroyCallbacks)"); }
            auto S = Cast<UCk_EntityScript_EntityLifecycleGym_DestroyCallbacks>(Base);
            if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D()) { return StartupBlocked(f"private bool SampleBranch checkpoint13: branch={i} if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D())"); }
            S.Request_UpdateDisplay();
            if (S.CurrentPhase == 0 && (!CkLifecycleGym::IsDirect(S.ChildA, H) || !CkLifecycleGym::IsDirect(S.ChildB, H) || !CkLifecycleGym::IsDirect(S.ChildC, H))) { return StartupBlocked(f"private bool SampleBranch checkpoint14: branch={i} if (S.CurrentPhase == 0 && (!CkLifecycleGym::IsDirect(S.ChildA, H) || !CkLifecycleGym::IsDirect(S.ChildB, H) || !CkLifecycleGym::IsDirect(S.ChildC, H)))"); }
            if (S.CurrentPhase == 1 && (!CkLifecycleGym::IsDirect(S.ChildB, H) || !CkLifecycleGym::IsDirect(S.ChildC, H))) { return StartupBlocked(f"private bool SampleBranch checkpoint15: branch={i} if (S.CurrentPhase == 1 && (!CkLifecycleGym::IsDirect(S.ChildB, H) || !CkLifecycleGym::IsDirect(S.ChildC, H)))"); }
        }
        if (i == 3)
        {
            if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_ActorBridge) { return StartupBlocked(f"private bool SampleBranch checkpoint16: branch={i} if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_ActorBridge)"); }
            auto S = Cast<UCk_EntityScript_EntityLifecycleGym_ActorBridge>(Base);
            if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D()) { return StartupBlocked(f"private bool SampleBranch checkpoint17: branch={i} if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D())"); }
            S.Request_UpdateDisplay();
        }
        if (i == 4)
        {
            if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_TagSystem) { return StartupBlocked(f"private bool SampleBranch checkpoint18: branch={i} if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_TagSystem)"); }
            auto S = Cast<UCk_EntityScript_EntityLifecycleGym_TagSystem>(Base);
            if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D()) { return StartupBlocked(f"private bool SampleBranch checkpoint19: branch={i} if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D())"); }
            S.Request_UpdateDisplay();
            if (!CkLifecycleGym::IsDirect(S.ChildEntity, H)) { return StartupBlocked(f"private bool SampleBranch checkpoint20: branch={i} if (!CkLifecycleGym::IsDirect(S.ChildEntity, H))"); }
        }
        if (i == 5)
        {
            if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_DeferredSetup) { return StartupBlocked(f"private bool SampleBranch checkpoint21: branch={i} if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_DeferredSetup)"); }
            auto S = Cast<UCk_EntityScript_EntityLifecycleGym_DeferredSetup>(Base);
            if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D()) { return StartupBlocked(f"private bool SampleBranch checkpoint22: branch={i} if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D())"); }
            S.Request_UpdateDisplay();
            // CompleteSetup removes the deferred tag synchronously while phase1
            // remains active; validate the real resulting state, not the old tag.
            if (S.CurrentPhase < 2 && !CkLifecycleGym::IsDirect(FCk_Handle(S.DeferredHandle), H)) { return StartupBlocked(f"private bool SampleBranch checkpoint23: branch={i} if (S.CurrentPhase < 2 && !CkLifecycleGym::IsDirect(FCk_Handle(S.DeferredHandle), H))"); }
            if (S.CurrentPhase == 0 && (!utils_deferred_entity::Has(FCk_Handle(S.DeferredHandle)) || !utils_deferred_entity::Get_IsDeferred(S.DeferredHandle))) { return StartupBlocked(f"private bool SampleBranch checkpoint24: branch={i} if (S.CurrentPhase == 0 && (!utils_deferred_entity::Has(FCk_Handle(S.DeferredHandle)) || !utils_deferred_entity::Get_IsDeferred(S.DeferredHandle)))"); }
            if (S.CurrentPhase == 1 && (utils_deferred_entity::Has(FCk_Handle(S.DeferredHandle)) || !S.Pass_CompleteSetup ||
                !S.SetupCompleteCallbackFired || !S.FullyCompleteCallbackFired || S.ActualCallbackCount - S.CycleStartCallbacks != 2)) { return StartupBlocked(f"private bool SampleBranch checkpoint25: branch={i} if (S.CurrentPhase == 1 && (utils_deferred_entity::Has(FCk_Handle(S.DeferredHandle)) || !S.Pass_CompleteSetup || !S.SetupCompleteCallbackFired || !S.FullyCompleteCallbackFired || S.ActualCallbackCount - S.CycleStartCallbacks != 2))"); }
            if (S.CurrentPhase == 2 && (S.LastCompletedCycle != S.CycleCount || S.LastCycleCallbackCount != 2 ||
                !S.Pass_SetupCompleteCallback || !S.Pass_FullyCompleteCallback)) { return StartupBlocked(f"private bool SampleBranch checkpoint26: branch={i} if (S.CurrentPhase == 2 && (S.LastCompletedCycle != S.CycleCount || S.LastCycleCallbackCount != 2 || !S.Pass_SetupCompleteCallback || !S.Pass_FullyCompleteCallback))"); }
        }
        if (i == 6)
        {
            if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_ScriptSpawnCast) { return StartupBlocked(f"private bool SampleBranch checkpoint27: branch={i} if (Base.GetClass() != UCk_EntityScript_EntityLifecycleGym_ScriptSpawnCast)"); }
            auto S = Cast<UCk_EntityScript_EntityLifecycleGym_ScriptSpawnCast>(Base);
            if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D()) { return StartupBlocked(f"private bool SampleBranch checkpoint28: branch={i} if (S.InitialTransform.GetLocation() != _BranchTransforms[i].GetLocation() || S.InitialTransform.GetRotation() != _BranchTransforms[i].GetRotation() || S.InitialTransform.GetScale3D() != _BranchTransforms[i].GetScale3D())"); }
            S.Request_UpdateDisplay();
            if (!S.TestsComplete || !CkLifecycleGym::IsDirect(S.SpawnTarget, H) || !utils_entity_script::Has(S.SpawnTarget)) { return StartupBlocked(f"private bool SampleBranch checkpoint29: branch={i} if (!S.TestsComplete || !CkLifecycleGym::IsDirect(S.SpawnTarget, H) || !utils_entity_script::Has(S.SpawnTarget))"); }
            auto Target = UCk_Utils_EntityScript_UE::TryGet_EntityScript(S.SpawnTarget);
            if (ck::Is_NOT_Valid(Target) || Target.GetClass() != UCk_EntityScript_EntityLifecycleGym_SpawnTarget) { return StartupBlocked(f"private bool SampleBranch checkpoint30: branch={i} if (ck::Is_NOT_Valid(Target) || Target.GetClass() != UCk_EntityScript_EntityLifecycleGym_SpawnTarget)"); }
        }
        if (Demo.Failed || Demo.Closed || Snapshot.Failed) { return StartupBlocked(f"private bool SampleBranch checkpoint31: branch={i} if (Demo.Failed || Demo.Closed || Snapshot.Failed)"); }
        Out = Demo.Sample;
        const bool IsCompleteRow = Out.BranchGeneration == BranchGenerations[i] && !Out.Body.IsEmpty();
        if (!IsCompleteRow) { return StartupBlocked(f"private bool SampleBranch checkpoint32: branch={i} incomplete original row"); }
        return true;
    }

    bool Get_IsCompleteSnapshot(FCkLifecycleGym_Snapshot S)
    {
        if (!S.Ready || S.Failed || S.Generation <= 0 || S.Sequence <= 0 || S.Rows.Num() != 7) { return false; }
        for (auto R : S.Rows)
        {
            if (R.BranchGeneration <= 0 || R.Title.IsEmpty() || R.Body.IsEmpty() || R.Instructions.IsEmpty() ||
                !(R.GoalMilliseconds >= 0.0f && R.GoalMilliseconds < 100000000.0f) ||
                !(R.ElapsedMilliseconds >= 0.0f && R.ElapsedMilliseconds < 100000000.0f)) { return false; }
        }
        return true;
    }

    private bool BuildSnapshot(FCkLifecycleGym_Snapshot& Out)
    {
        Out = FCkLifecycleGym_Snapshot();
        if (!HasWorldIdentity() || ServerChildren.Num() != 7 || BranchGenerations.Num() != 7 || _Constructed.Num() != 7 ||
            _PendingChildren.Num() != 7 || _BranchTransforms.Num() != 7 || _Stations.Num() != 7 || Snapshot.Sequence >= 2147483646 ||
            ck::Is_NOT_Valid(BridgeProbe) || !CkLifecycleGym::IsLive(ServerBridgeRoot)) { return StartupBlocked("private bool BuildSnapshot checkpoint1: if (!HasWorldIdentity() || ServerChildren.Num() != 7 || BranchGenerations.Num() != 7 || _Constructed.Num() != 7 || _PendingChildren.Num() != 7 || _BranchTransforms.Num() != 7 || _Stations.Num() != 7 || Snapshot.Sequence >= 2147483646 || ck::Is_NOT_Valid(BridgeProbe) || !CkLifecycleGym::IsLive(ServerBridgeRoot))"); }
        if (BridgeProbe != _OwnedProbe || ServerBridgeRoot != _PendingRoot.Get_EntityUnderConstruction() ||
            Get_LocalVerifiedProbeRoot(BranchGenerations[3]) != ServerBridgeRoot) { return StartupBlocked("private bool BuildSnapshot checkpoint2: if (BridgeProbe != _OwnedProbe || ServerBridgeRoot != _PendingRoot.Get_EntityUnderConstruction() || Get_LocalVerifiedProbeRoot(BranchGenerations[3]) != ServerBridgeRoot)"); }
        auto Candidate = FCkLifecycleGym_Snapshot();
        Candidate.Generation = Snapshot.Generation; Candidate.Sequence = Snapshot.Sequence + 1; Candidate.Ready = true;
        for (int32 i = 0; i < 7; ++i)
        {
            FCkLifecycleGym_RowSample Row;
            if (!SampleBranch(i, Row)) { return StartupBlocked(f"private bool BuildSnapshot checkpoint3: branch={i} if (!SampleBranch(i, Row))"); }
            Candidate.Rows.Add(Row);
        }
        if (!Get_IsCompleteSnapshot(Candidate)) { return StartupBlocked("private bool BuildSnapshot checkpoint4: if (!Get_IsCompleteSnapshot(Candidate))"); }
        Out = Candidate; return true;
    }

    bool Get_HasReadyServerCohort()
    {
        return !_Starting && HasWorldIdentity() && Get_IsCompleteSnapshot(Snapshot) && ServerCohortCount == 7;
    }

    int32 Request_Rerun(ACk_EntityLifecycleGym_PlayerController Owner, int32 Serial, int32 Row, int32 Generation)
    {
        auto Pawn = ck::IsValid(Owner) ? Cast<ACk_Gym_Base_Pawn>(Owner.GetControlledPawn()) : nullptr;
        if (!Get_IsValidOwner(Owner, Pawn) || Serial <= 0 || Serial <= Owner.LastControlSerial || Row < 1 || Row > 8 || Generation != Snapshot.Generation ||
            !Owner.Get_IsReserved(Serial, Row, Generation)) { return 2; }
        if (_Starting || ck::IsValid(_Owner) || !Get_HasReadyServerCohort()) { return 3; }
        FCkLifecycleGym_Snapshot Preflight;
        if (!BuildSnapshot(Preflight)) { Request_Fail("Lifecycle admission cohort invalid"); return 3; }
        for (int32 i = 0; i < 7; ++i)
        { if ((Row == 8 || i == Row - 1) && BranchGenerations[i] >= 2147483646) { return 3; } }
        _Owner = Owner; _Pawn = Pawn; _Serial = Serial; _Row = Row; _Rerun = Row == 8 ? 7 : Row - 1;
        _StartupBlockerReported = false; _StartupBlockerReason = ""; _Starting = true; _RetirementPending = true; _Spawning = false; _StartTime = float(System::GetGameTimeInSeconds());
        Snapshot.Ready = false;
        for (int32 i = 0; i < 7; ++i)
        {
            if (_Rerun != 7 && i != _Rerun) { continue; }
            BranchGenerations[i]++;
            CloseAndCapture(i); ServerChildren[i] = FCk_Handle(); _Constructed[i] = false;
        }
        if (_Rerun == 3 || _Rerun == 7) { RetireProbe(); }
        ForceNetUpdate(); return 1;
    }

    private void CaptureRetiring(FCk_Handle H)
    {
        if (!CkLifecycleGym::IsLive(H) || _Retiring.Contains(H)) { return; }
        _Retiring.Add(H);
        for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(H)) { CaptureRetiring(Child); }
    }
    private void CloseAndCapture(int32 i)
    {
        if (i < 0 || i >= _PendingChildren.Num()) { return; }
        auto Pending = _PendingChildren[i];
        auto H = Pending.Get_EntityUnderConstruction();
        if (CkLifecycleGym::IsLive(H))
        {
            auto S = Cast<UCkLifecycleGym_DemoBase>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(H));
            if (ck::IsValid(S))
            {
                S.Closed = true;
                for (auto Extra : S.ExtraRetirement)
                {
                    if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Extra) && !_Retiring.Contains(Extra)) { _Retiring.Add(Extra); }
                    if (CkLifecycleGym::IsLive(Extra)) { utils_entity_lifetime::Request_DestroyEntity(Extra); }
                }
            }
            CaptureRetiring(H); utils_entity_lifetime::Request_DestroyEntity(H);
        }
        if (utils_pending_entity_script::Get_IsValid(Pending))
        {
            auto P = Pending.Get_EntityUnderConstruction();
            CaptureRetiring(P);
            if (CkLifecycleGym::IsLive(P)) { utils_entity_lifetime::Request_DestroyEntity(P); }
        }
    }
    private void RetireProbe()
    {
        auto H = _PendingRoot.Get_EntityUnderConstruction();
        CaptureRetiring(H);
        if (CkLifecycleGym::IsLive(H)) { utils_entity_lifetime::Request_DestroyEntity(H); }
        ServerBridgeRoot = FCk_Handle(); _PendingRoot = FCk_Handle_PendingEntityScript();
        if (ck::IsValid(_OwnedProbe)) { _OwnedProbe.DestroyActor(); }
        _OwnedProbe = nullptr;
        BridgeProbe = nullptr;
    }

    bool Request_TryGetPreparedCommit(ACk_EntityLifecycleGym_PlayerController Owner, int32 Serial, FCkLifecycleGym_ControlCommit& Out)
    {
        Out = FCkLifecycleGym_ControlCommit();
        if (!Get_IsValidOwner(Owner, _Pawn) || Owner != _Owner || Serial != _Serial || Serial <= 0 ||
            _Prepared.RequestSerial != Serial || _Prepared.Row != _Row || !Get_IsCompleteSnapshot(_Prepared.Snapshot) ||
            !Owner.Get_IsReserved(Serial, _Row, Snapshot.Generation)) { return false; }
        Out = _Prepared; return true;
    }

    bool Get_IsCapturedOwnerTransaction(ACk_EntityLifecycleGym_PlayerController Owner, ACk_Gym_Base_Pawn Pawn,
        int32 Serial, int32 Row, int32 Generation)
    {
        return !_Ending && !Snapshot.Failed && HasWorldIdentity() && _Starting && Serial > 0 &&
            Owner == _Owner && Pawn == _Pawn && Serial == _Serial && Row == _Row &&
            Row >= 1 && Row <= 8 && Generation > 0 && Generation == Snapshot.Generation;
    }

    UFUNCTION() private void OnMonitor()
    {
        if (_Ending || Snapshot.Failed) { return; }
        if (!HasWorldIdentity()) { Request_Fail("Lifecycle world identity lost"); return; }
        if (_Starting && float(System::GetGameTimeInSeconds()) - _StartTime > 45.0f)
        { Request_Fail("Lifecycle transaction timed out"); return; }
        if (_Starting && _Serial > 0)
        {
            bool ContextLost = !Get_IsValidOwner(_Owner, _Pawn);
            bool ExactOwner = ck::IsValid(_Owner) && _Owner.GetClass() == ACk_EntityLifecycleGym_PlayerController &&
                _Owner.HasAuthority() && _Owner.GetWorld() == GetWorld();
            if (ExactOwner && _Owner.OwnerSessionFailed) { ContextLost = true; }
            if (ContextLost)
            {
                if (ExactOwner && !_Owner.Request_RetireCapturedOwnerContext(this, _Pawn, _Serial, _Row, Snapshot.Generation))
                { Request_Fail("Lifecycle lost owner reservation corrupt"); return; }
                // The admitted world operation remains owned by this GameState. Finish its real
                // retirement/construction barrier, but never deliver acceptance to a lost session.
                _Prepared = FCkLifecycleGym_ControlCommit(); _Owner = nullptr; _Pawn = nullptr; _Serial = 0; _Row = 0;
            }
            else if (!_Owner.Get_IsReserved(_Serial, _Row, Snapshot.Generation))
            { Request_Fail("Lifecycle live owner reservation corrupt"); return; }
        }
        if (_Stations.Num() == 0)
        {
            if (_PendingStations.Num() == 0) { Request_CreateStations(); return; }
            if (!CheckStations()) { return; }
        }
        if (_RetirementPending)
        {
            for (auto H : _Retiring) { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { StartupBlocked("OnMonitor retirement still allocated"); return; } }
            _Retiring.Empty(); _RetirementPending = false;
        }
        if (_Starting && !_Spawning)
        {
            _Spawning = true;
            for (int32 i = 0; i < 7; ++i) { if (i != 3 && (_Rerun < 0 || _Rerun == 7 || _Rerun == i)) { SpawnBranch(i); if (Snapshot.Failed) { return; } } }
            if (_Rerun < 0 || _Rerun == 7 || _Rerun == 3) { SpawnProbe(); }
        }
        if (_Starting)
        {
            for (int32 i = 0; i < _Constructed.Num(); ++i)
            { if (!_Constructed[i]) { StartupBlocked(f"OnMonitor pending original constructed callback branch={i}"); return; } }
        }
        FCkLifecycleGym_Snapshot Candidate;
        if (!BuildSnapshot(Candidate))
        {
            if (_Starting) { return; }
            Request_Fail("Lifecycle required cohort lost"); return;
        }
        if (_Starting && _Serial > 0)
        {
            _Prepared = FCkLifecycleGym_ControlCommit();
            _Prepared.RequestSerial = _Serial; _Prepared.Row = _Row;
            _Prepared.DeliverySerial = _Owner.Get_NextDeliverySerial(); _Prepared.Snapshot = Candidate;
            if (!_Owner.Request_TryRecordCommit(this, _Serial)) { Request_Fail("Lifecycle complete commit rejected"); return; }
        }
        Snapshot = Candidate; ServerCohortCount = 7;
        _Prepared = FCkLifecycleGym_ControlCommit(); _Owner = nullptr; _Pawn = nullptr; _Serial = 0; _Row = 0;
        _Rerun = -1; _Starting = false; ForceNetUpdate();
    }

    FCk_Handle Get_LocalVerifiedProbeRoot(int32 ExpectedBranchGeneration)
    {
        if (ck::Is_NOT_Valid(BridgeProbe) || BridgeProbe.GetWorld() != GetWorld() || BridgeProbe.GetClass() != ACk_LifecycleGym_BridgeProbe)
        { return FCk_Handle(); }
        if (ExpectedBranchGeneration <= 0 || BridgeProbe.BranchGeneration != ExpectedBranchGeneration) { return FCk_Handle(); }
        auto H = utils_owning_actor::TryGet_ActorEntityHandle(BridgeProbe);
        if (!CkLifecycleGym::IsLive(H) || !utils_owning_actor::Has(H) || !utils_entity_replication_driver::Has(H) ||
            utils_owning_actor::TryGet_EntityOwningActor(H) != BridgeProbe || !utils_entity_script::Has(H) ||
            !utils_net::Has(H) || utils_net::Get_EntityReplication(H) != ECk_Replication::Replicates) { return FCk_Handle(); }
        auto S = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
        if (ck::Is_NOT_Valid(S) || S.GetClass() != UCk_EntityScript_LifecycleGym_BridgeRoot) { return FCk_Handle(); }
        return H;
    }

    // A failed partial station admission is retained until the world restarts.
    private bool Request_HoldFailedAdmission()
    {
        if (_AdmissionTicket == 0) { return false; }
        auto World = ck::TransientEntity();
        CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
        CkGym_Admission::TryCloseWorldUnproven(World);
        return true;
    }

    void Request_Fail(FString Reason)
    {
        if (Request_HoldFailedAdmission() ||
            CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        {
            _Starting = false; Snapshot.Ready = false; Snapshot.Failed = true;
            Snapshot.FailureReason = Reason; _Prepared = FCkLifecycleGym_ControlCommit();
            ForceNetUpdate();
            return;
        }
        if (_Ending || Snapshot.Failed) { return; }
        Snapshot.Ready = false; Snapshot.Failed = true; Snapshot.FailureReason = Reason;
        _Starting = false; _Prepared = FCkLifecycleGym_ControlCommit();
        if (ck::IsValid(_Owner)) { _Owner.Request_RejectControl(_Serial, 3); }
        RetireAll(); ForceNetUpdate();
    }
    private void RetireAll()
    {
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        { Request_Fail("Partial station admission cannot retire in this world"); return; }
        for (int32 i = 0; i < _PendingChildren.Num(); ++i) { CloseAndCapture(i); }
        RetireProbe();
        for (auto P : _PendingStations)
        {
            if (!utils_pending_entity_script::Get_IsValid(P)) { continue; }
            auto H = P.Get_EntityUnderConstruction();
            if (CkLifecycleGym::IsLive(H)) { CaptureRetiring(H); utils_entity_lifetime::Request_DestroyEntity(H); }
        }
        if (CkLifecycleGym::IsLive(_StationOwner)) { CaptureRetiring(_StationOwner); utils_entity_lifetime::Request_DestroyEntity(_StationOwner); }
        ReleaseAdmission(); ServerCohortCount = 0;
    }
    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason Reason)
    {
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        {
            _Ending = true; _Starting = false; Snapshot.Ready = false;
            Request_HoldFailedAdmission();
            return;
        }
        _Ending = true;
        if (HasAuthority()) { RetireAll(); }
    }
}
