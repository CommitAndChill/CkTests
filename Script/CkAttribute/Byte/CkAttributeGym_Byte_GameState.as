//============================================================================
// BYTE ATTRIBUTE GYM - WORLD STATE AND LOCAL PROJECTION
//============================================================================

USTRUCT()
struct FCkAttributeGym_ByteComponentSample
{
    UPROPERTY() bool Present = false;
    UPROPERTY() uint8 Base = 0;
    UPROPERTY() uint8 Bonus = 0;
    UPROPERTY() uint8 Final = 0;
}

USTRUCT()
struct FCkAttributeGym_ByteAttributeSample
{
    UPROPERTY() FName Name;
    UPROPERTY() FCkAttributeGym_ByteComponentSample Min;
    UPROPERTY() FCkAttributeGym_ByteComponentSample Max;
    UPROPERTY() FCkAttributeGym_ByteComponentSample Current;
    UPROPERTY() uint8 PreClampFinal = 0;
    UPROPERTY() uint8 ClampOverflow = 0;
}

USTRUCT()
struct FCkAttributeGym_ByteStationSample
{
    UPROPERTY() int32 StationIndex = -1;
    UPROPERTY() bool AutoRunning = false;
    UPROPERTY() int32 AutoStep = 0;
    UPROPERTY() TArray<FCkAttributeGym_ByteAttributeSample> Attributes;
    UPROPERTY() int32 ValueChangeCount = 0;
    UPROPERTY() int32 ClampedCount = 0;
    UPROPERTY() int32 MinChangeCount = 0;
    UPROPERTY() int32 MaxChangeCount = 0;
    UPROPERTY() int32 CurrentChangeCount = 0;
    UPROPERTY() int32 Delegate1Count = 0;
    UPROPERTY() int32 Delegate2Count = 0;
    UPROPERTY() int32 Delegate3Count = 0;
    UPROPERTY() int32 DelegateBoundMask = 0;
    UPROPERTY() uint8 LastPreClampValue = 0;
    UPROPERTY() uint8 LastClampedValue = 0;
    UPROPERTY() uint8 LastOverflow = 0;
    UPROPERTY() int32 ActiveModifierCount = 0;
    UPROPERTY() TArray<uint8> ActiveModifierDeltas;
    UPROPERTY() int32 TotalAttributes = 0;
    UPROPERTY() int32 CombatBatchCount = 0;
    UPROPERTY() int32 BatchEpoch = 0;
    UPROPERTY() bool CombatPresent = false;
    UPROPERTY() TArray<int32> ClampSerials;
    UPROPERTY() int32 BoundarySerial = 0;
    UPROPERTY() FString Title;
    UPROPERTY() FString Body;
}

USTRUCT()
struct FCkAttributeGym_ByteSnapshot
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() TArray<FCkAttributeGym_ByteStationSample> Stations;
}

USTRUCT()
struct FCkAttributeGym_ByteControlCommit
{
    UPROPERTY() int32 Serial = 0;
    UPROPERTY() int32 Action = -1;
    UPROPERTY() int32 Preset = -1;
    UPROPERTY() FCkAttributeGym_ByteSnapshot Snapshot;
}

USTRUCT()
struct FByteStationSpawnParams
{
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 StationIndex = -1;
}

USTRUCT()
struct FByteProjectionSpawnParams
{
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 StationIndex = -1;
}

namespace CkAttributeByte
{
    bool IsValidSample(FCkAttributeGym_ByteStationSample InSample)
    {
        auto Index = InSample.StationIndex;
        if (Index < 0 || Index >= 6 || InSample.AutoStep < 0 ||
            InSample.ValueChangeCount < 0 || InSample.ClampedCount < 0 ||
            InSample.MinChangeCount < 0 || InSample.MaxChangeCount < 0 ||
            InSample.CurrentChangeCount < 0 || InSample.Delegate1Count < 0 ||
            InSample.Delegate2Count < 0 || InSample.Delegate3Count < 0 ||
            InSample.DelegateBoundMask < 0 || InSample.DelegateBoundMask > 7 ||
            InSample.ActiveModifierCount < 0 || InSample.BatchEpoch < 0 ||
            InSample.BoundarySerial < 0 ||
            ((Index == 0 && InSample.ClampSerials.Num() != 4) ||
             (Index != 0 && (InSample.ClampSerials.Num() != 0 ||
                             InSample.BoundarySerial != 0))) ||
            InSample.ActiveModifierCount != InSample.ActiveModifierDeltas.Num())
        { return false; }
        for (auto Serial : InSample.ClampSerials)
        { if (Serial < 0) { return false; } }
        auto Count = InSample.Attributes.Num();
        if ((Index == 0 && Count != 4) || (Index == 1 && Count != 2) ||
            (Index == 2 && Count != 1) ||
            (Index == 3 && Count != 3 && Count != 5) ||
            (Index >= 4 && Count != 1)) { return false; }
        if (Index == 3 && (InSample.CombatBatchCount < 0 ||
            InSample.CombatBatchCount > 1 ||
            InSample.TotalAttributes != Count ||
            InSample.CombatPresent != (Count == 5) ||
            InSample.CombatBatchCount != (Count == 5 ? 1 : 0)))
        { return false; }
        auto Names = TArray<FName>();
        if (Index == 0)
        { Names.Add(n"ByteAttribute.Armor"); Names.Add(n"ByteAttribute.Stamina");
          Names.Add(n"ByteAttribute.Health"); Names.Add(n"ByteAttribute.Shield"); }
        if (Index == 1)
        { Names.Add(n"ByteAttribute.Damage"); Names.Add(n"ByteAttribute.Defense"); }
        if (Index == 2) { Names.Add(n"ByteAttribute.PowerLevel"); }
        if (Index == 3)
        { Names.Add(n"RPG.Strength"); Names.Add(n"RPG.Agility");
          Names.Add(n"RPG.Intelligence");
          if (Count == 5) { Names.Add(n"Combat.Attack"); Names.Add(n"Combat.Defense"); } }
        if (Index == 4) { Names.Add(n"Test.ValueRetrieval"); }
        if (Index == 5) { Names.Add(n"Test.SignalBinding"); }
        if (Names.Num() != Count) { return false; }
        for (int32 i = 0; i < Count; i++)
        {
            if (InSample.Attributes[i].Name != Names[i] ||
                !InSample.Attributes[i].Min.Present ||
                !InSample.Attributes[i].Max.Present ||
                !InSample.Attributes[i].Current.Present) { return false; }
        }
        return true;
    }

    bool IsValidSnapshot(FCkAttributeGym_ByteSnapshot InSnapshot)
    {
        if (InSnapshot.Generation <= 0 || InSnapshot.Sequence <= 0 ||
            !InSnapshot.Ready || InSnapshot.Failed || InSnapshot.Stations.Num() != 6)
        { return false; }
        for (int32 i = 0; i < 6; i++)
        { if (InSnapshot.Stations[i].StationIndex != i ||
              !IsValidSample(InSnapshot.Stations[i])) { return false; } }
        return true;
    }

    bool IsSettledReset(FCkAttributeGym_ByteSnapshot InSnapshot)
    {
        if (!IsValidSnapshot(InSnapshot)) { return false; }
        for (auto Station : InSnapshot.Stations)
        { if (!Station.AutoRunning || Station.AutoStep != 0) { return false; } }
        auto C = InSnapshot.Stations[0];
        auto M = InSnapshot.Stations[1];
        auto P = InSnapshot.Stations[2];
        auto R = InSnapshot.Stations[3];
        auto V = InSnapshot.Stations[4];
        auto S = InSnapshot.Stations[5];
        return C.Attributes[0].Current.Final == 100 &&
            C.Attributes[1].Current.Final == 150 &&
            C.Attributes[2].Current.Final == 75 &&
            C.Attributes[3].Current.Final == 150 &&
            C.Attributes[3].PreClampFinal == 200 &&
            M.Attributes[0].Current.Final == 75 && M.Attributes[1].Current.Final == 30 &&
            M.ActiveModifierCount == 1 && M.ActiveModifierDeltas[0] == 25 &&
            P.Attributes[0].Min.Final == 10 && P.Attributes[0].Max.Final == 200 &&
            P.Attributes[0].Current.Final == 100 &&
            R.Attributes.Num() == 3 && R.CombatBatchCount == 0 && !R.CombatPresent &&
            R.Attributes[0].Current.Final == 85 && R.Attributes[1].Current.Final == 120 &&
            R.Attributes[2].Current.Final == 95 &&
            V.ActiveModifierCount == 0 && V.Attributes[0].Min.Final == 5 &&
            V.Attributes[0].Max.Final == 180 && V.Attributes[0].Current.Final == 75 &&
            S.DelegateBoundMask == 0 && S.Attributes[0].Current.Final == 100;
    }
}

class ACk_ByteAttributeGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkAttributeGym_ByteSnapshot Snapshot;
    // Retain the actual committed sample after automatic cycles resume.
    UPROPERTY(Replicated) FCkAttributeGym_ByteControlCommit LastControlCommit;
    UPROPERTY() TArray<FCk_Handle> ServerChildren;
    UPROPERTY() int32 ServerChildCount = 0;
    UPROPERTY() int32 LocalProjectionGeneration = 0;
    UPROPERTY() bool LocalProjectionReady = false;
    UPROPERTY() bool LocalProjectionFailed = false;
    UPROPERTY() FString LocalProjectionFailureReason;

    private FCk_Handle _WorldStationOwner;
    private FCk_Handle _AdmissionOwner;
    private int32 _AdmissionTicket = 0;
    private bool _AdmissionOwned = false;
    private bool _Starting = false;
    private bool _StationsReady = false;
    private bool _ServerMonitorArmed = false;
    private int32 _NextGeneration = 0;
    private float _StartSeconds = 0.0;
    private float _LastPublishSeconds = -1.0;
    private TArray<FCk_Handle> _Stations;
    private TArray<FCk_Handle> _NewStations;
    private TArray<FCk_Handle_PendingEntityScript> _PendingStations;
    private TArray<FCk_Handle_PendingEntityScript> _PendingChildren;

    private bool _TransactionPending = false;
    private int32 _TransactionEpoch = 0;
    private int32 _TransactionAction = -1;
    private int32 _TransactionSerial = 0;
    private int32 _TransactionGeneration = 0;
    private int32 _TransactionPreset = -1;
    private float _TransactionSeconds = 0.0;
    private bool _RetirementCompleted = false;
    private ACk_ByteAttributeGym_PlayerController _TransactionOwner;
    private TArray<FCk_Handle_ByteAttribute> _RetiringAttributes;

    private TArray<FCk_Handle> _LocalChildren;
    private TArray<FCk_Handle_PendingEntityScript> _PendingLocal;
    private bool _LocalStarting = false;
    private bool _LocalMonitorArmed = false;
    private float _LocalWaitSeconds = 0.0;
    private float _LocalStartSeconds = -1.0;

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || !Get_HasPublishedCatalog() || _Starting ||
            Snapshot.Ready || Snapshot.Failed) { return; }
        _Starting = true;
        _StartSeconds = float(System::GetGameTimeInSeconds());
        _NextGeneration++;
        Snapshot = FCkAttributeGym_ByteSnapshot();
        LastControlCommit = FCkAttributeGym_ByteControlCommit();
        Snapshot.Generation = _NextGeneration;
        _Stations.Empty(); _NewStations.Empty(); _PendingStations.Empty();
        _PendingChildren.Empty(); ServerChildren.Empty();
        for (int32 i = 0; i < 6; i++)
        {
            _Stations.Add(FCk_Handle()); _NewStations.Add(FCk_Handle());
            _PendingStations.Add(FCk_Handle_PendingEntityScript());
            _PendingChildren.Add(FCk_Handle_PendingEntityScript());
            ServerChildren.Add(FCk_Handle());
        }
        ServerChildCount = 0;
        ForceNetUpdate();
        System::SetTimer(this, n"OnStartupDeadline", 20.0, false);
        if (!_ServerMonitorArmed)
        { _ServerMonitorArmed = true; System::SetTimer(this, n"OnServerMonitor", 0.1, true); }
        Request_EnsureWorldStations();
    }

    private void Request_Fail(FString InReason)
    {
        if (Snapshot.Failed) { return; }
        _Starting = false;
        _TransactionPending = false;
        Snapshot.Ready = false;
        Snapshot.Failed = true;
        Snapshot.FailureReason = InReason;
        Snapshot.Sequence++;
        Request_CloseWorldOnFailure();
        ForceNetUpdate();
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        { ck::Error("[AttributeByte] " + InReason); return; }
        if (ck::IsValid(_TransactionOwner) &&
            _TransactionOwner.GetWorld() == GetWorld() &&
            _TransactionGeneration == Snapshot.Generation)
        { _TransactionOwner.Request_CompleteByteControl(_TransactionSerial,
            _TransactionAction, _TransactionPreset, _TransactionGeneration, false); }
        _TransactionOwner = nullptr;
        Request_RetireServerChildren();
        for (int32 i = 0; i < _PendingStations.Num(); i++)
        {
            if (utils_pending_entity_script::Get_IsValid(_PendingStations[i]))
            { utils_pending_entity_script::Request_DestroyEntity(_PendingStations[i]); }
            _PendingStations[i] = FCk_Handle_PendingEntityScript();
        }
        Request_ReleaseAdmission();
        if (CkAttributeByte::IsLiveEntity(_WorldStationOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); }
        _WorldStationOwner = FCk_Handle();
        ck::Error("[AttributeByte] " + InReason);
    }

    private void Request_CloseWorldOnFailure()
    {
        if (_AdmissionTicket == 0) { return; }
        auto World = ck::TransientEntity();
        if (!CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner))
        { CkGym_Admission::TryCloseWorldUnproven(World); }
    }

    private void Request_RetireServerChildren()
    {
        if (_AdmissionTicket != 0) { return; }
        ServerChildCount = 0;
        for (int32 i = 0; i < _PendingChildren.Num(); i++)
        {
            if (utils_pending_entity_script::Get_IsValid(_PendingChildren[i]))
            { utils_pending_entity_script::Request_DestroyEntity(_PendingChildren[i]); }
            else if (i < ServerChildren.Num() && CkAttributeByte::IsLiveEntity(ServerChildren[i]))
            { utils_entity_lifetime::Request_DestroyEntity(ServerChildren[i]); }
            _PendingChildren[i] = FCk_Handle_PendingEntityScript();
            if (i < ServerChildren.Num()) { ServerChildren[i] = FCk_Handle(); }
        }
    }

    private bool Request_ReleaseAdmission(bool InPublishedComplete = false)
    {
        if (_AdmissionTicket == 0 && CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        { return false; }
        auto World = ck::TransientEntity();
        if (_AdmissionTicket != 0)
        {
            if (InPublishedComplete)
            {
                if (!CkGym_Admission::TryReleasePublished(World, _AdmissionTicket, _AdmissionOwner))
                { Request_CloseWorldOnFailure(); return false; }
                _AdmissionTicket = 0;
            }
            else
            { Request_CloseWorldOnFailure(); return false; }
        }
        _AdmissionOwned = false;
        if (CkAttributeByte::IsLiveEntity(_AdmissionOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_AdmissionOwner); }
        _AdmissionOwner = FCk_Handle();
        return true;
    }

    private void Request_EnsureWorldStations()
    {
        if (!_Starting || _StationsReady || _AdmissionOwned) { return; }
        auto World = ck::TransientEntity();
        if (!CkAttributeByte::IsLiveEntity(World) ||
            !utils_entity_lifetime::Get_CanCreateEntity(World))
        { Request_Fail("World owner unavailable"); return; }
        if (CkGym_Admission::Get_IsClosedUnproven(World))
        { Request_Fail("Station admission permanently closed"); return; }
        if (CkGym_Admission::Get_IsBusy(World))
        { System::SetTimer(this, n"OnAdmissionWait", 0.1, false); return; }
        _AdmissionTicket = CkGym_Admission::TryReserve(World);
        if (_AdmissionTicket == 0)
        { Request_Fail("Station admission reservation rejected"); return; }
        _AdmissionOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!CkAttributeByte::IsLiveEntity(_AdmissionOwner))
        { Request_Fail("Admission allocation failed"); return; }
        if (!CkGym_Admission::TryCommitOwner(World, _AdmissionTicket, _AdmissionOwner))
        { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
          Request_Fail("Station admission owner commit rejected"); return; }
        _AdmissionOwned = true;
        auto Specs = CkAttributeByte::Get_Stations();
        if (Specs.Num() != 6) { Request_Fail("Station declaration invalid"); return; }
        for (int32 i = 0; i < 6; i++)
        {
            if (Specs[i].Tags.Num() != 1 || Specs[i].Tags[0] != CkAttributeByte::Get_StationTag(i))
            { Request_Fail("Station tag declaration invalid"); return; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, Specs[i].Tags[0]);
            if (Matches.Num() > 1 || (Matches.Num() == 1 &&
                (!CkAttributeByte::IsLiveEntity(Matches[0]) ||
                 !Matches[0].Has_Fragment(FCkGym_Station_Anchors))))
            { Request_Fail("Station identity ambiguous"); return; }
            if (Matches.Num() == 1) { _Stations[i] = Matches[0]; }
        }
        bool Missing = false;
        for (int32 i = 0; i < 6; i++)
        { if (!CkAttributeByte::IsLiveEntity(_Stations[i])) { Missing = true; } }
        if (Missing)
        {
            _WorldStationOwner = utils_entity_lifetime::Request_CreateEntity(World);
            if (!CkAttributeByte::IsLiveEntity(_WorldStationOwner))
            { Request_Fail("World station group allocation failed"); return; }
            for (int32 i = 0; i < 6; i++)
            {
                if (CkAttributeByte::IsLiveEntity(_Stations[i])) { continue; }
                _PendingStations[i] = CkGym_Common::Request_SpawnNewStationForOwner(
                    Specs[i], _WorldStationOwner,
                    FCk_Delegate_Request_OnCompleted(this, n"OnStationSpawnCompleted"));
                if (!utils_pending_entity_script::Get_IsValid(_PendingStations[i]))
                { Request_Fail("Station spawn rejected"); return; }
                utils_pending_entity_script::Promise_OnConstructed(_PendingStations[i],
                    FCk_Delegate_EntityScript_Constructed(this, n"OnStationConstructed"));
            }
        }
        System::SetTimer(this, n"OnStationSettle", 0.05, false);
    }

    UFUNCTION() private void OnAdmissionWait()
    {
        if (!_Starting || _AdmissionOwned || _StationsReady) { return; }
        if (float(System::GetGameTimeInSeconds()) - _StartSeconds >= 20.0)
        { Request_Fail("Station admission timed out"); return; }
        Request_EnsureWorldStations();
    }

    UFUNCTION() private void OnStationSpawnCompleted(FCk_Handle InOwner,
        ECk_Request_OperationResult InResult)
    {
        if (!_Starting) { return; }
        for (int32 i = 0; i < _PendingStations.Num(); i++)
        {
            if (utils_pending_entity_script::Get_IsValid(_PendingStations[i]) &&
                InOwner == _PendingStations[i].Get_EntityUnderConstruction())
            {
                if (InResult != ECk_Request_OperationResult::Succeeded)
                { Request_Fail("Station spawn failed"); }
                return;
            }
        }
    }

    UFUNCTION() private void OnStationConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || ck::Is_NOT_Valid(InHandle)) { return; }
        auto Child = FCk_Handle(InHandle);
        for (int32 i = 0; i < _PendingStations.Num(); i++)
        {
            if (!utils_pending_entity_script::Get_IsValid(_PendingStations[i]) ||
                Child != _PendingStations[i].Get_EntityUnderConstruction()) { continue; }
            if (!CkAttributeByte::IsLiveEntity(Child) ||
                utils_entity_lifetime::Get_LifetimeOwner(Child) != _WorldStationOwner)
            { Request_Fail("Station completion owner invalid"); return; }
            _NewStations[i] = Child;
            _Stations[i] = Child;
            return;
        }
    }

    UFUNCTION() private void OnStationSettle()
    {
        if (!_Starting || !_AdmissionOwned || _StationsReady) { return; }
        auto World = ck::TransientEntity();
        if (!CkAttributeByte::IsLiveEntity(World))
        { Request_Fail("World lost during station settle"); return; }
        for (int32 i = 0; i < 6; i++)
        {
            if (!CkAttributeByte::IsLiveEntity(_Stations[i]))
            { System::SetTimer(this, n"OnStationSettle", 0.05, false); return; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, CkAttributeByte::Get_StationTag(i));
            if (Matches.Num() == 0)
            { System::SetTimer(this, n"OnStationSettle", 0.05, false); return; }
            if (Matches.Num() != 1 || Matches[0] != _Stations[i] ||
                !_Stations[i].Has_Fragment(FCkGym_Station_Anchors))
            { Request_Fail("Station tag or anchors invalid"); return; }
        }
        _StationsReady = true;
        if (!Request_ReleaseAdmission(true)) { Request_Fail("Station admission publication rejected"); return; }
        Request_SpawnServerChildren();
    }

    private void Request_SpawnServerChildren()
    {
        if (!_Starting || !_StationsReady) { return; }
        auto World = ck::TransientEntity();
        for (int32 i = 0; i < 6; i++)
        {
            if (!CkAttributeByte::IsLiveEntity(_Stations[i]) ||
                !_Stations[i].Has_Fragment(FCkGym_Station_Anchors) ||
                utils_entity_tag::ForEach_Entity(World, CkAttributeByte::Get_ChildTag(i)).Num() != 0)
            { Request_Fail("Simulation child preflight failed"); return; }
        }
        for (int32 i = 0; i < 6; i++)
        {
            auto Params = FByteStationSpawnParams();
            Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
                _Stations[i].Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
            Params.Generation = Snapshot.Generation;
            Params.StationIndex = i;
            auto Owner = _Stations[i];
            if (i == 0) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_ByteClamping, FInstancedStruct::Make(Params)); }
            if (i == 1) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_ByteModifiers, FInstancedStruct::Make(Params)); }
            if (i == 2) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_ByteMinMaxCurrent, FInstancedStruct::Make(Params)); }
            if (i == 3) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_ByteMultiple, FInstancedStruct::Make(Params)); }
            if (i == 4) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_ByteValues, FInstancedStruct::Make(Params)); }
            if (i == 5) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_ByteSignals, FInstancedStruct::Make(Params)); }
            if (!utils_pending_entity_script::Get_IsValid(_PendingChildren[i]))
            { Request_Fail("Simulation child spawn rejected"); return; }
            utils_pending_entity_script::Promise_OnConstructed(_PendingChildren[i],
                FCk_Delegate_EntityScript_Constructed(this, n"OnServerChildConstructed"));
        }
    }

    UFUNCTION() private void OnServerChildConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || ck::Is_NOT_Valid(InHandle)) { return; }
        auto Child = FCk_Handle(InHandle);
        for (int32 i = 0; i < _PendingChildren.Num(); i++)
        {
            if (utils_pending_entity_script::Get_IsValid(_PendingChildren[i]) &&
                Child == _PendingChildren[i].Get_EntityUnderConstruction())
            { ServerChildren[i] = Child; return; }
        }
    }

    UFUNCTION() private void OnStartupDeadline()
    {
        if (!_Starting || Snapshot.Ready || Snapshot.Failed) { return; }
        auto Elapsed = float(System::GetGameTimeInSeconds()) - _StartSeconds;
        if (Elapsed < 20.0)
        { System::SetTimer(this, n"OnStartupDeadline", 20.0 - Elapsed, false); return; }
        Request_Fail("World sextet readiness timed out");
    }

    bool Get_IsExpectedServerChild(FCk_Handle InChild, int32 InGeneration, int32 InIndex)
    {
        return HasAuthority() && _Starting && !Snapshot.Failed &&
            InGeneration == Snapshot.Generation && InIndex >= 0 && InIndex < 6 &&
            InIndex < _PendingChildren.Num() &&
            utils_pending_entity_script::Get_IsValid(_PendingChildren[InIndex]) &&
            InChild == _PendingChildren[InIndex].Get_EntityUnderConstruction();
    }

    void Request_FailExpectedServerChild(FCk_Handle InChild, int32 InGeneration,
        int32 InIndex, FString InReason)
    {
        if (Get_IsExpectedServerChild(InChild, InGeneration, InIndex))
        { Request_Fail(InReason); }
    }

    private bool Get_IsValidServerChildren()
    {
        if (ServerChildren.Num() != 6) { return false; }
        auto World = ck::TransientEntity();
        if (!CkAttributeByte::IsLiveEntity(World)) { return false; }
        for (int32 i = 0; i < 6; i++)
        {
            auto Station = CkAttributeByte::FindStation(i);
            auto Child = ServerChildren[i];
            if (!CkAttributeByte::IsLiveEntity(Station) || Station != _Stations[i] ||
                !CkAttributeByte::IsLiveEntity(Child) ||
                utils_entity_lifetime::Get_LifetimeOwner(Child) != Station)
            { return false; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, CkAttributeByte::Get_ChildTag(i));
            if (Matches.Num() != 1 || Matches[0] != Child) { return false; }
            if (!Get_IsValidServerScript(i)) { return false; }
        }
        return true;
    }

    private bool Get_IsValidServerScript(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= ServerChildren.Num() ||
            !CkAttributeByte::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (ck::Is_NOT_Valid(Script) || ck::ToEntity(Script) != ServerChildren[InIndex])
        { return false; }
        if (InIndex == 0) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteClamping>(Script);
            return ck::IsValid(Typed) && Typed.Generation == Snapshot.Generation &&
                Typed.StationIndex == InIndex && Typed.Get_HasValidAuthorityComposition(); }
        if (InIndex == 1) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteModifiers>(Script);
            return ck::IsValid(Typed) && Typed.Generation == Snapshot.Generation &&
                Typed.StationIndex == InIndex && Typed.Get_HasValidAuthorityComposition(); }
        if (InIndex == 2) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteMinMaxCurrent>(Script);
            return ck::IsValid(Typed) && Typed.Generation == Snapshot.Generation &&
                Typed.StationIndex == InIndex && Typed.Get_HasValidAuthorityComposition(); }
        if (InIndex == 3) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteMultiple>(Script);
            return ck::IsValid(Typed) && Typed.Generation == Snapshot.Generation &&
                Typed.StationIndex == InIndex && Typed.Get_HasValidAuthorityComposition(); }
        if (InIndex == 4) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteValues>(Script);
            return ck::IsValid(Typed) && Typed.Generation == Snapshot.Generation &&
                Typed.StationIndex == InIndex && Typed.Get_HasValidAuthorityComposition(); }
        if (InIndex == 5) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteSignals>(Script);
            return ck::IsValid(Typed) && Typed.Generation == Snapshot.Generation &&
                Typed.StationIndex == InIndex && Typed.Get_HasValidAuthorityComposition(); }
        return false;
    }

    bool Get_HasReadyServerChildren()
    {
        if (!HasAuthority() || _TransactionPending || ServerChildCount != 6 ||
            !CkAttributeByte::IsValidSnapshot(Snapshot)) { return false; }
        if (!Get_IsValidServerChildren())
        { Request_Fail("Authority sextet dependency lost"); return false; }
        return true;
    }

    bool Get_IsCurrentServerChild(FCk_Handle InChild, int32 InGeneration, int32 InIndex)
    {
        if (!HasAuthority() || _TransactionPending || !Snapshot.Ready || Snapshot.Failed ||
            ServerChildCount != 6 || InGeneration != Snapshot.Generation ||
            InIndex < 0 || InIndex >= 6 || InIndex >= ServerChildren.Num() ||
            InChild != ServerChildren[InIndex]) { return false; }
        if (!Get_IsValidServerChildren())
        { Request_Fail("Authority sextet dependency lost"); return false; }
        return true;
    }

    bool Get_CanMutateServerChild(FCk_Handle InChild, int32 InGeneration, int32 InIndex)
    { return Get_IsCurrentServerChild(InChild, InGeneration, InIndex); }

    bool Get_CanApplyTransactionControl(FCk_Handle InChild, int32 InGeneration,
        int32 InIndex)
    {
        if (!HasAuthority() || !_TransactionPending || Snapshot.Failed ||
            InGeneration != Snapshot.Generation ||
            InGeneration != _TransactionGeneration ||
            InIndex < 0 || InIndex >= 6 ||
            InIndex >= ServerChildren.Num() || InIndex >= _Stations.Num() ||
            InChild != ServerChildren[InIndex] ||
            !CkAttributeByte::IsLiveEntity(InChild) ||
            !((_TransactionAction >= 11 && _TransactionAction <= 12 && InIndex == 4) ||
              (_TransactionAction >= 13 && _TransactionAction <= 14 && InIndex == 1) ||
              ((_TransactionAction == 15 || _TransactionAction == 16) && InIndex == 3) ||
              _TransactionAction == 18)) { return false; }
        auto Station = CkAttributeByte::FindStation(InIndex);
        return CkAttributeByte::IsLiveEntity(Station) && Station == _Stations[InIndex] &&
            utils_entity_lifetime::Get_LifetimeOwner(InChild) == Station;
    }

    void Request_FailCurrentServerChild(FCk_Handle InChild, int32 InGeneration,
        int32 InIndex, FString InReason)
    {
        if (HasAuthority() && !Snapshot.Failed && Snapshot.Ready &&
            InGeneration == Snapshot.Generation && InIndex >= 0 && InIndex < 6 &&
            InIndex < ServerChildren.Num() && InChild == ServerChildren[InIndex])
        { Request_Fail(InReason); }
    }

    bool Get_HasCompleteSnapshot() { return CkAttributeByte::IsValidSnapshot(Snapshot); }

    bool Get_AutoRunning(int32 InIndex)
    {
        return Get_HasCompleteSnapshot() && InIndex >= 0 && InIndex < 6 &&
            Snapshot.Stations[InIndex].AutoRunning;
    }

    private bool Request_TryBuildSnapshot(FCkAttributeGym_ByteSnapshot& Out)
    {
        if (!Get_IsValidServerChildren()) { return false; }
        auto Candidate = FCkAttributeGym_ByteSnapshot();
        Candidate.Generation = Snapshot.Generation;
        Candidate.Sequence = Snapshot.Sequence + 1;
        Candidate.Ready = true;
        for (int32 i = 0; i < 6; i++)
        {
            auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[i]);
            auto Sample = FCkAttributeGym_ByteStationSample();
            bool Built = false;
            if (i == 0) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteClamping>(Script);
                Built = ck::IsValid(Typed) && Typed.Request_TryBuildSample(Sample); }
            if (i == 1) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteModifiers>(Script);
                Built = ck::IsValid(Typed) && Typed.Request_TryBuildSample(Sample); }
            if (i == 2) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteMinMaxCurrent>(Script);
                Built = ck::IsValid(Typed) && Typed.Request_TryBuildSample(Sample); }
            if (i == 3) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteMultiple>(Script);
                Built = ck::IsValid(Typed) && Typed.Request_TryBuildSample(Sample); }
            if (i == 4) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteValues>(Script);
                Built = ck::IsValid(Typed) && Typed.Request_TryBuildSample(Sample); }
            if (i == 5) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteSignals>(Script);
                Built = ck::IsValid(Typed) && Typed.Request_TryBuildSample(Sample); }
            if (!Built || Sample.StationIndex != i || !CkAttributeByte::IsValidSample(Sample))
            { return false; }
            Candidate.Stations.Add(Sample);
        }
        if (!CkAttributeByte::IsValidSnapshot(Candidate)) { return false; }
        Out = Candidate;
        return true;
    }

    UFUNCTION() private void OnServerMonitor()
    {
        if (!HasAuthority() || Snapshot.Failed) { return; }
        if (_Starting)
        {
            bool Complete = true;
            for (int32 i = 0; i < 6; i++)
            { if (i >= ServerChildren.Num() || !CkAttributeByte::IsLiveEntity(ServerChildren[i]))
              { Complete = false; } }
            if (!Complete) { return; }
            auto Candidate = FCkAttributeGym_ByteSnapshot();
            if (!Request_TryBuildSnapshot(Candidate)) { return; }
            Snapshot = Candidate;
            ServerChildCount = 6;
            _Starting = false;
            _LastPublishSeconds = float(System::GetGameTimeInSeconds());
            ForceNetUpdate();
            return;
        }
        if (!Snapshot.Ready || ServerChildCount != 6) { return; }
        if (_TransactionPending) { Request_CheckTransaction(); return; }
        if (!Get_IsValidServerChildren())
        { Request_Fail("Authority sextet dependency lost"); return; }
        auto Now = float(System::GetGameTimeInSeconds());
        if (Now - _LastPublishSeconds < 0.1) { return; }
        auto Candidate = FCkAttributeGym_ByteSnapshot();
        if (!Request_TryBuildSnapshot(Candidate))
        { Request_Fail("Authority sextet sample malformed"); return; }
        Snapshot = Candidate;
        _LastPublishSeconds = Now;
    }

    private bool Request_SetTransactionPaused(bool InPaused)
    {
        if (ServerChildren.Num() != 6) { return false; }
        for (int32 i = 0; i < 6; i++)
        {
            auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[i]);
            if (i == 0) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteClamping>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetTransactionPaused(InPaused)) { return false; } }
            if (i == 1) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteModifiers>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetTransactionPaused(InPaused)) { return false; } }
            if (i == 2) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteMinMaxCurrent>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetTransactionPaused(InPaused)) { return false; } }
            if (i == 3) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteMultiple>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetTransactionPaused(InPaused)) { return false; } }
            if (i == 4) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteValues>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetTransactionPaused(InPaused)) { return false; } }
            if (i == 5) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteSignals>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetTransactionPaused(InPaused)) { return false; } }
        }
        return true;
    }

    private bool Request_ApplyActionToChild(int32 InIndex, int32 InAction, int32 InPreset)
    {
        if (InIndex < 0 || InIndex >= ServerChildren.Num() ||
            !CkAttributeByte::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteClamping>(Script);
            return ck::IsValid(Typed) && Typed.Request_ApplyControl(InAction, InPreset); }
        if (InIndex == 1) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteModifiers>(Script);
            return ck::IsValid(Typed) && Typed.Request_ApplyControl(InAction, InPreset); }
        if (InIndex == 2) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteMinMaxCurrent>(Script);
            return ck::IsValid(Typed) && Typed.Request_ApplyControl(InAction, InPreset); }
        if (InIndex == 3) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteMultiple>(Script);
            return ck::IsValid(Typed) && Typed.Request_ApplyControl(InAction, InPreset); }
        if (InIndex == 4) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteValues>(Script);
            return ck::IsValid(Typed) && Typed.Request_ApplyControl(InAction, InPreset); }
        if (InIndex == 5) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteSignals>(Script);
            return ck::IsValid(Typed) && Typed.Request_ApplyControl(InAction, InPreset); }
        return false;
    }

    private bool Request_SetAllAuto(bool InRunning)
    {
        for (int32 i = 0; i < 6; i++)
        {
            auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[i]);
            if (i == 0) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteClamping>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetAutoRunning(InRunning)) { return false; } }
            if (i == 1) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteModifiers>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetAutoRunning(InRunning)) { return false; } }
            if (i == 2) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteMinMaxCurrent>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetAutoRunning(InRunning)) { return false; } }
            if (i == 3) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteMultiple>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetAutoRunning(InRunning)) { return false; } }
            if (i == 4) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteValues>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetAutoRunning(InRunning)) { return false; } }
            if (i == 5) { auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteSignals>(Script);
                if (ck::Is_NOT_Valid(Typed) || !Typed.Request_SetAutoRunning(InRunning)) { return false; } }
        }
        return true;
    }

    bool Get_IsCrossedBoundPreset(int32 InAction, int32 InPreset)
    {
        if ((InAction != 7 && InAction != 8) || InPreset < 0 || InPreset >= 4 ||
            !Get_HasReadyServerChildren() || !Get_IsValidServerScript(2))
        { return false; }
        auto Typed = Cast<UCk_EntityScript_AttributeGym_ByteMinMaxCurrent>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[2]));
        return ck::IsValid(Typed) && Typed.Get_IsCrossedBoundPreset(InAction, InPreset);
    }

    bool Request_ApplyControl(ACk_ByteAttributeGym_PlayerController InOwner,
        int32 InSerial, int32 InAction, int32 InPreset)
    {
        if (!HasAuthority() || !Get_HasReadyServerChildren() || _TransactionPending ||
            ck::Is_NOT_Valid(InOwner) || InOwner.GetWorld() != GetWorld() ||
            InSerial <= 0) { return false; }
        if ((InAction == 7 || InAction == 8) &&
            Get_IsCrossedBoundPreset(InAction, InPreset)) { return false; }
        if ((InAction >= 11 && InAction <= 16) || InAction == 18)
        {
            if (!Request_SetTransactionPaused(true))
            { Request_Fail("Transaction pause failed"); return false; }
            _TransactionPending = true;
            _TransactionEpoch++;
            _TransactionAction = InAction;
            _TransactionPreset = InPreset;
            _TransactionSerial = InSerial;
            _TransactionGeneration = Snapshot.Generation;
            _TransactionOwner = InOwner;
            _TransactionSeconds = float(System::GetGameTimeInSeconds());
            _RetiringAttributes.Empty();
            _RetirementCompleted = false;
            if (InAction >= 11 && InAction <= 14)
            {
                int32 TargetIndex = InAction <= 12 ? 4 : 1;
                if (!Request_ApplyActionToChild(TargetIndex, InAction, InPreset))
                { Request_Fail("Modifier control admission failed"); return false; }
                Request_CheckTransaction();
                return !Snapshot.Failed;
            }
            auto Multiple = Cast<UCk_EntityScript_AttributeGym_ByteMultiple>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[3]));
            if (ck::Is_NOT_Valid(Multiple))
            { Request_Fail("Multiple station missing during transaction"); return false; }
            if ((InAction == 15 || InAction == 16) &&
                !Multiple.Request_SetAutoRunning(false))
            { Request_Fail("Manual combat auto stop failed"); return false; }
            if (InAction == 15)
            {
                if (!Multiple.Request_ApplyControl(15, -1))
                { Request_Fail("Combat batch admission failed"); return false; }
            }
            else if (!Multiple.Request_BeginCombatRetirement(InAction == 18,
                _RetiringAttributes))
            { Request_Fail("Combat batch retirement rejected"); return false; }
            Request_CheckTransaction();
            return true;
        }
        bool Applied = false;
        if (InAction == 0)
        {
            auto Current = FCkAttributeGym_ByteSnapshot();
            if (!Request_TryBuildSnapshot(Current))
            { Request_Fail("Global auto control sample invalid"); return false; }
            bool AllRunning = true;
            for (int32 i = 0; i < 6; i++)
            { AllRunning = AllRunning && Current.Stations[i].AutoRunning; }
            Applied = Request_SetAllAuto(!AllRunning);
        }
        else
        {
            int32 Index = -1;
            if (InAction == 1) { Index = 0; }
            if (InAction == 2 || InAction == 13 || InAction == 14) { Index = 1; }
            if (InAction == 3 || InAction == 11 || InAction == 12) { Index = 4; }
            if (InAction == 4 || (InAction >= 7 && InAction <= 9)) { Index = 2; }
            if (InAction == 5) { Index = 3; }
            if (InAction == 6 || InAction == 10) { Index = 5; }
            if (InAction == 17) { Index = 0; }
            Applied = Index >= 0 && Request_ApplyActionToChild(Index, InAction, InPreset);
        }
        if (!Applied || Snapshot.Failed)
        { Request_Fail("Authority control application failed"); return false; }
        InOwner.Request_CompleteByteControl(InSerial, InAction, InPreset,
            Snapshot.Generation, true);
        return true;
    }

    bool Request_AutoCombatStep(FCk_Handle InChild, int32 InGeneration,
        int32 InStationIndex, bool InAdd)
    {
        if (InStationIndex != 3 || !Get_IsCurrentServerChild(InChild,
            InGeneration, InStationIndex) || _TransactionPending) { return false; }
        if (!Request_SetTransactionPaused(true))
        { Request_Fail("Auto combat transaction pause failed"); return false; }
        _TransactionPending = true;
        _TransactionEpoch++;
        _TransactionAction = InAdd ? 15 : 16;
        _TransactionSerial = 0;
        _TransactionGeneration = Snapshot.Generation;
        _TransactionPreset = -1;
        _TransactionOwner = nullptr;
        _TransactionSeconds = float(System::GetGameTimeInSeconds());
        _RetiringAttributes.Empty();
        _RetirementCompleted = false;
        auto Multiple = Cast<UCk_EntityScript_AttributeGym_ByteMultiple>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[3]));
        if (ck::Is_NOT_Valid(Multiple))
        { Request_Fail("Auto combat station lost"); return false; }
        if (InAdd)
        {
            if (!Multiple.Request_ApplyControl(15, -1))
            { Request_Fail("Auto combat admission failed"); return false; }
        }
        else if (!Multiple.Request_BeginCombatRetirement(false, _RetiringAttributes))
        { Request_Fail("Auto combat retirement failed"); return false; }
        Request_CheckTransaction();
        return !Snapshot.Failed;
    }

    private void Request_CheckTransaction()
    {
        if (!_TransactionPending || Snapshot.Failed) { return; }
        if (_TransactionGeneration != Snapshot.Generation)
        { Request_Fail("Attribute transaction generation changed"); return; }
        if (ServerChildren.Num() != 6)
        { Request_Fail("Authority sextet lost during transaction"); return; }
        auto Now = float(System::GetGameTimeInSeconds());
        if (Now - _TransactionSeconds >= 20.0)
        { Request_Fail("Attribute transaction timed out"); return; }
        for (int32 i = 0; i < 6; i++)
        {
            bool IsMutableTransactionChild =
                ((_TransactionAction >= 11 && _TransactionAction <= 12) && i == 4) ||
                ((_TransactionAction >= 13 && _TransactionAction <= 14) && i == 1) ||
                ((_TransactionAction >= 15 && _TransactionAction <= 16) && i == 3) ||
                (_TransactionAction == 18 && (i == 1 || i == 3 || i == 4));
            if (!CkAttributeByte::IsLiveEntity(CkAttributeByte::FindStation(i)) ||
                !CkAttributeByte::IsLiveEntity(ServerChildren[i]) ||
                (!IsMutableTransactionChild && !Get_IsValidServerScript(i)))
            { Request_Fail("Sibling dependency lost during transaction"); return; }
        }
        for (auto Old : _RetiringAttributes)
        { if (ck::IsValid(Old)) { return; } }
        if (!_RetirementCompleted && (_TransactionAction == 16 || _TransactionAction == 18))
        {
            auto Multiple = Cast<UCk_EntityScript_AttributeGym_ByteMultiple>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[3]));
            if (ck::Is_NOT_Valid(Multiple))
            { Request_Fail("Combat retirement completion owner lost"); return; }
            if (!Multiple.Request_CompleteCombatRetirement(_TransactionAction == 18))
            { return; } // Feature registration can settle after old handles retire.
            if (Snapshot.Failed) { return; }
            if (_TransactionAction == 18)
            {
                for (int32 i = 0; i < 6; i++)
                {
                    if (i != 3 && !Request_ApplyActionToChild(i, 18, -1))
                    { Request_Fail("Attribute reset failed"); return; }
                }
            }
            _RetiringAttributes.Empty();
            _RetirementCompleted = true;
        }
        if ((_TransactionAction >= 11 && _TransactionAction <= 14) ||
            _TransactionAction == 18)
        {
            if (_TransactionAction == 11 || _TransactionAction == 12 ||
                _TransactionAction == 18)
            {
                auto Values = Cast<UCk_EntityScript_AttributeGym_ByteValues>(
                    UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[4]));
                if (ck::Is_NOT_Valid(Values))
                { Request_Fail("Values modifier owner lost"); return; }
                if (!Values.Request_HasSettledModifierControl()) { return; }
            }
            if (_TransactionAction == 13 || _TransactionAction == 14 ||
                _TransactionAction == 18)
            {
                auto Modifiers = Cast<UCk_EntityScript_AttributeGym_ByteModifiers>(
                    UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[1]));
                if (ck::Is_NOT_Valid(Modifiers))
                { Request_Fail("Modifiers owner lost"); return; }
                if (!Modifiers.Request_HasSettledModifierControl()) { return; }
            }
        }
        if (Now - _LastPublishSeconds < 0.1) { return; }
        auto Candidate = FCkAttributeGym_ByteSnapshot();
        if (!Request_TryBuildSnapshot(Candidate)) { return; }
        // Override completion admits work; attribute processors settle its values later.
        if (_TransactionAction == 18 && !CkAttributeByte::IsSettledReset(Candidate)) { return; }
        if (!Request_SetTransactionPaused(false))
        { Request_Fail("Attribute transaction resume failed"); return; }
        _TransactionPending = false;
        Snapshot = Candidate;
        if (_TransactionSerial > 0)
        {
            auto Commit = FCkAttributeGym_ByteControlCommit();
            Commit.Serial = _TransactionSerial;
            Commit.Action = _TransactionAction;
            Commit.Preset = _TransactionPreset;
            Commit.Snapshot = Candidate;
            LastControlCommit = Commit;
        }
        _LastPublishSeconds = Now;
        ForceNetUpdate();
        if (ck::IsValid(_TransactionOwner) && _TransactionOwner.GetWorld() == GetWorld())
        { _TransactionOwner.Request_CompleteByteControl(_TransactionSerial,
            _TransactionAction, _TransactionPreset, _TransactionGeneration, true); }
        _TransactionOwner = nullptr;
        _TransactionAction = -1;
        _TransactionSerial = 0;
    }

    void Request_EnsureLocalProjection()
    {
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) || World.GetNetMode() == ENetMode::NM_DedicatedServer)
        { return; }
        if (!_LocalMonitorArmed)
        { _LocalMonitorArmed = true; _LocalWaitSeconds = float(System::GetGameTimeInSeconds());
          System::SetTimer(this, n"OnLocalMonitor", 0.1, true); }
        Request_CheckLocalProjection();
    }

    bool Get_IsExpectedLocalChild(FCk_Handle InChild, int32 InGeneration, int32 InIndex)
    {
        return _LocalStarting && !LocalProjectionFailed && Snapshot.Ready &&
            LocalProjectionGeneration == InGeneration && Snapshot.Generation == InGeneration &&
            InIndex >= 0 && InIndex < _PendingLocal.Num() &&
            utils_pending_entity_script::Get_IsValid(_PendingLocal[InIndex]) &&
            InChild == _PendingLocal[InIndex].Get_EntityUnderConstruction();
    }

    void Request_FailExpectedLocalChild(FCk_Handle InChild, int32 InGeneration,
        int32 InIndex, FString InReason)
    {
        if (Get_IsExpectedLocalChild(InChild, InGeneration, InIndex))
        { Request_FailLocal(InReason); }
    }

    bool Get_HasAdmittedLocalProjection(int32 InGeneration)
    {
        if (!LocalProjectionReady || LocalProjectionFailed ||
            LocalProjectionGeneration != InGeneration || Snapshot.Generation != InGeneration ||
            !Get_HasCompleteSnapshot()) { return false; }
        if (!Get_IsValidLocalProjection())
        { Request_FailLocal("Projection sextet dependency lost"); return false; }
        return true;
    }

    private bool Get_IsValidLocalProjection()
    {
        if (_LocalChildren.Num() != 6) { return false; }
        auto World = ck::TransientEntity();
        if (!CkAttributeByte::IsLiveEntity(World)) { return false; }
        for (int32 i = 0; i < 6; i++)
        {
            auto Station = CkAttributeByte::FindStation(i);
            auto Child = _LocalChildren[i];
            if (!CkAttributeByte::IsLiveEntity(Station) ||
                !CkAttributeByte::IsLiveEntity(Child) ||
                utils_entity_lifetime::Get_LifetimeOwner(Child) != Station)
            { return false; }
            auto Matches = utils_entity_tag::ForEach_Entity(World,
                n"TAG_AttributeGym_ByteProjection");
            if (Matches.Num() != 6) { return false; }
            int32 Count = 0;
            for (auto Match : Matches) { if (Match == Child) { Count++; } }
            if (Count != 1) { return false; }
            auto Script = Cast<UCk_EntityScript_AttributeGym_ByteProjection>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(Child));
            if (ck::Is_NOT_Valid(Script) || ck::ToEntity(Script) != Child ||
                Script.Generation != LocalProjectionGeneration ||
                Script.StationIndex != i || !Script.Get_HasValidLocalComposition())
            { return false; }
        }
        return true;
    }

    UFUNCTION() private void OnLocalMonitor() { Request_CheckLocalProjection(); }

    private void Request_CheckLocalProjection()
    {
        if (!_LocalMonitorArmed) { return; }
        if (Snapshot.Generation != LocalProjectionGeneration)
        {
            Request_RetireLocal();
            LocalProjectionGeneration = Snapshot.Generation;
            LocalProjectionFailed = false;
            LocalProjectionFailureReason = "";
            _LocalWaitSeconds = float(System::GetGameTimeInSeconds());
            _LocalStartSeconds = -1.0;
        }
        if (LocalProjectionFailed) { return; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World)) { Request_FailLocal("Projection world lost"); return; }
        if (World.GetNetMode() == ENetMode::NM_Client)
        {
            for (int32 i = 0; i < 6; i++)
            { if (utils_entity_tag::ForEach_Entity(ck::TransientEntity(),
                  CkAttributeByte::Get_ChildTag(i)).Num() != 0)
              { Request_FailLocal("Client contains authority child"); return; } }
        }
        if (Snapshot.Failed) { Request_FailLocal("Authority demo failed"); return; }
        if (Snapshot.Ready && !Get_HasCompleteSnapshot())
        { Request_FailLocal("Authority snapshot malformed"); return; }
        if (LocalProjectionReady)
        {
            if (!Snapshot.Ready || !Get_IsValidLocalProjection())
            { Request_FailLocal("Admitted projection lost readiness"); }
            return;
        }
        auto Now = float(System::GetGameTimeInSeconds());
        if (!Snapshot.Ready)
        {
            if (Now - _LocalWaitSeconds >= 25.0)
            { Request_FailLocal("Authority readiness timed out"); }
            return;
        }
        if (_LocalStartSeconds < 0.0) { _LocalStartSeconds = Now; }
        if (Now - _LocalStartSeconds >= 20.0)
        { Request_FailLocal("Local projection readiness timed out"); return; }
        if (_LocalStarting)
        {
            bool AllConstructed = _LocalChildren.Num() == 6;
            for (int32 i = 0; i < _LocalChildren.Num(); i++)
            { AllConstructed = AllConstructed && CkAttributeByte::IsLiveEntity(_LocalChildren[i]); }
            if (AllConstructed && Get_IsValidLocalProjection())
            { _LocalStarting = false; LocalProjectionReady = true; }
            return;
        }
        for (int32 i = 0; i < 6; i++)
        { if (!CkAttributeByte::IsLiveEntity(CkAttributeByte::FindStation(i))) { return; } }
        if (utils_entity_tag::ForEach_Entity(ck::TransientEntity(),
            n"TAG_AttributeGym_ByteProjection").Num() != 0)
        { Request_FailLocal("Projection identity already exists"); return; }
        _LocalChildren.Empty(); _PendingLocal.Empty();
        for (int32 i = 0; i < 6; i++)
        { _LocalChildren.Add(FCk_Handle()); _PendingLocal.Add(FCk_Handle_PendingEntityScript()); }
        _LocalStarting = true;
        for (int32 i = 0; i < 6; i++)
        {
            auto Station = CkAttributeByte::FindStation(i);
            auto Params = FByteProjectionSpawnParams();
            Params.Generation = LocalProjectionGeneration;
            Params.StationIndex = i;
            Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
                Station.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
            auto Owner = Station;
            _PendingLocal[i] = utils_entity_script::Request_SpawnEntity(Owner,
                UCk_EntityScript_AttributeGym_ByteProjection, FInstancedStruct::Make(Params));
            if (!utils_pending_entity_script::Get_IsValid(_PendingLocal[i]))
            { Request_FailLocal("Projection spawn rejected"); return; }
            utils_pending_entity_script::Promise_OnConstructed(_PendingLocal[i],
                FCk_Delegate_EntityScript_Constructed(this, n"OnLocalConstructed"));
        }
    }

    UFUNCTION() private void OnLocalConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_LocalStarting || ck::Is_NOT_Valid(InHandle)) { return; }
        auto Child = FCk_Handle(InHandle);
        for (int32 i = 0; i < _PendingLocal.Num(); i++)
        {
            if (utils_pending_entity_script::Get_IsValid(_PendingLocal[i]) &&
                Child == _PendingLocal[i].Get_EntityUnderConstruction())
            { _LocalChildren[i] = Child; return; }
        }
    }

    void Request_FailCurrentLocalChild(FCk_Handle InChild, int32 InGeneration,
        int32 InIndex, FString InReason)
    {
        if (LocalProjectionReady && !LocalProjectionFailed &&
            InGeneration == LocalProjectionGeneration && InIndex >= 0 && InIndex < 6 &&
            InIndex < _LocalChildren.Num() && InChild == _LocalChildren[InIndex])
        { Request_FailLocal(InReason); }
    }

    private void Request_FailLocal(FString InReason)
    {
        Request_RetireLocal();
        LocalProjectionFailed = true;
        LocalProjectionFailureReason = InReason;
        ck::Error("[AttributeByte] " + InReason);
    }

    private void Request_RetireLocal()
    {
        LocalProjectionReady = false;
        _LocalStarting = false;
        for (int32 i = 0; i < _PendingLocal.Num(); i++)
        {
            if (utils_pending_entity_script::Get_IsValid(_PendingLocal[i]))
            { utils_pending_entity_script::Request_DestroyEntity(_PendingLocal[i]); }
            else if (i < _LocalChildren.Num() && CkAttributeByte::IsLiveEntity(_LocalChildren[i]))
            { utils_entity_lifetime::Request_DestroyEntity(_LocalChildren[i]); }
            _PendingLocal[i] = FCk_Handle_PendingEntityScript();
            if (i < _LocalChildren.Num()) { _LocalChildren[i] = FCk_Handle(); }
        }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (!HasAuthority() || _AdmissionTicket == 0) { return; }
        _Starting = false;
        Snapshot.Ready = false;
        Snapshot.Failed = true;
        Request_CloseWorldOnFailure();
    }
}

class UCk_EntityScript_AttributeGym_ByteProjection : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;
    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    UPROPERTY(ExposeOnSpawn) int32 StationIndex = -1;
    private FCk_Handle_Timer _DisplayTimer;
    private bool _Constructed = false;
    private TArray<int32> _LastClampSerials;
    private int32 _LastBoundarySerial = 0;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        _Constructed = false;
        _DisplayTimer = FCk_Handle_Timer();
        _LastClampSerials.Empty();
        for (int32 i = 0; i < 4; i++) { _LastClampSerials.Add(0); }
        _LastBoundarySerial = 0;
        auto State = CkAttributeByte::Get_State();
        auto World = ck::IsValid(State) ? State.GetWorld() : nullptr;
        if (!CkAttributeByte::IsLiveEntity(InHandle) || ck::Is_NOT_Valid(World) ||
            World.GetNetMode() == ENetMode::NM_DedicatedServer ||
            StationIndex < 0 || StationIndex >= 6 ||
            utils_entity_lifetime::Get_LifetimeOwner(InHandle) !=
                CkAttributeByte::FindStation(StationIndex) ||
            ck::Is_NOT_Valid(State) ||
            !State.Get_IsExpectedLocalChild(InHandle, Generation, StationIndex))
        {
            if (ck::IsValid(State)) { State.Request_FailExpectedLocalChild(
                InHandle, Generation, StationIndex, "Projection role or owner invalid"); }
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_ByteProjection");
        auto TimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
        TimerParams.Set_StartingState(ECk_Timer_State::Running)
            .Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        _DisplayTimer = utils_timer::Add(InHandle, TimerParams);
        if (!CkAttributeByte::IsLiveEntity(FCk_Handle(_DisplayTimer)))
        {
            State.Request_FailExpectedLocalChild(InHandle, Generation, StationIndex,
                "Projection display timer allocation failed");
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        _DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));
        _Constructed = true;
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    bool Get_HasValidLocalComposition()
    {
        auto Self = ck::ToEntity(this);
        return _Constructed && CkAttributeByte::IsLiveEntity(Self) &&
            utils_transform::Has(Self) &&
            CkAttributeByte::IsLiveEntity(FCk_Handle(_DisplayTimer)) &&
            utils_timer::DoCast(FCk_Handle(_DisplayTimer)).IsSet() &&
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_DisplayTimer)) == Self &&
            _DisplayTimer.Get_CurrentState() == ECk_Timer_State::Running;
    }

    UFUNCTION() private void DisplayTick(FCk_Handle_Timer InHandle,
        FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto State = CkAttributeByte::Get_State();
        if (InHandle != _DisplayTimer || ck::Is_NOT_Valid(State) ||
            !State.Get_HasAdmittedLocalProjection(Generation)) { return; }
        if (!Get_HasValidLocalComposition())
        { State.Request_FailCurrentLocalChild(ck::ToEntity(this), Generation,
            StationIndex, "Projection display dependency lost"); return; }
        auto Sample = State.Snapshot.Stations[StationIndex];
        if (!CkAttributeByte::IsValidSample(Sample))
        { State.Request_FailCurrentLocalChild(ck::ToEntity(this), Generation,
            StationIndex, "Projection sample malformed"); return; }
        if (StationIndex == 0)
        {
            if (_LastClampSerials.Num() != 4 ||
                Sample.BoundarySerial < _LastBoundarySerial)
            { State.Request_FailCurrentLocalChild(ck::ToEntity(this), Generation,
                StationIndex, "Projection event sequence malformed"); return; }
            for (int32 i = 0; i < 4; i++)
            {
                if (Sample.ClampSerials[i] < _LastClampSerials[i])
                { State.Request_FailCurrentLocalChild(ck::ToEntity(this), Generation,
                    StationIndex, "Projection clamp sequence regressed"); return; }
            }
            if (Sample.ClampSerials[0] > _LastClampSerials[0])
            { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this),
                FVector(-50.0f, 0.0f, 150.0f), FLinearColor(0.0f, 0.0f, 1.0f, 1.0f)); }
            if (Sample.ClampSerials[1] > _LastClampSerials[1])
            { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this),
                FVector(0.0f, 0.0f, 150.0f), FLinearColor(1.0f, 1.0f, 0.0f, 1.0f)); }
            if (Sample.ClampSerials[2] > _LastClampSerials[2])
            { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this),
                FVector(50.0f, 0.0f, 150.0f), FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)); }
            if (Sample.ClampSerials[3] > _LastClampSerials[3])
            { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this),
                FVector(100.0f, 0.0f, 150.0f), FLinearColor(0.0f, 1.0f, 0.0f, 1.0f)); }
            if (Sample.BoundarySerial > _LastBoundarySerial)
            { CkGym_Common::Draw_DebugSphere(ck::ToEntity(this),
                FVector(0.0f, 0.0f, 300.0f), FLinearColor(1.0f, 1.0f, 0.0f, 1.0f),
                25.0f, 3.0f, 2.0f); }
            for (int32 i = 0; i < 4; i++)
            { _LastClampSerials[i] = Sample.ClampSerials[i]; }
            _LastBoundarySerial = Sample.BoundarySerial;
        }
        auto TitleText = Sample.Title + " (" +
            CkGym_Common::Get_NetworkRoleTitle(ck::ToEntity(this)) + ")";
        CkGym_Common::Update_StationDisplay(ck::ToEntity(this), TitleText, Sample.Body, "");
    }
}
