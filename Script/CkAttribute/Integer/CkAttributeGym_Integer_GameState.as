//============================================================================
// INTEGER ATTRIBUTE GYM - WORLD STATE AND LOCAL PROJECTION
//============================================================================

USTRUCT()
struct FCkAttributeGym_IntegerComponentSample
{
    UPROPERTY() bool Present = false;
    UPROPERTY() int32 Base = 0;
    UPROPERTY() int32 Bonus = 0;
    UPROPERTY() int32 Final = 0;
}
USTRUCT()
struct FCkAttributeGym_IntegerAttributeSample
{
    UPROPERTY() FName Name;
    UPROPERTY() FCkAttributeGym_IntegerComponentSample Min;
    UPROPERTY() FCkAttributeGym_IntegerComponentSample Max;
    UPROPERTY() FCkAttributeGym_IntegerComponentSample Current;
    UPROPERTY() int32 PreClampFinal = 0;
    UPROPERTY() int32 ClampOverflow = 0;
}
USTRUCT()
struct FCkAttributeGym_IntegerStationSample
{
    UPROPERTY() int32 StationIndex = -1;
    UPROPERTY() FName StationTag;
    UPROPERTY() FName ChildTag;
    UPROPERTY() bool AutoRunning = false;
    UPROPERTY() int32 AutoStep = 0;
    UPROPERTY() TArray<FCkAttributeGym_IntegerAttributeSample> Attributes;
    UPROPERTY() int32 ValueChangeCount = 0;
    UPROPERTY() int32 HealthChangeCount = 0;
    UPROPERTY() int32 ArmorChangeCount = 0;
    UPROPERTY() int32 ExperienceChangeCount = 0;
    UPROPERTY() int32 MinChangeCount = 0;
    UPROPERTY() int32 MaxChangeCount = 0;
    UPROPERTY() int32 CurrentChangeCount = 0;
    UPROPERTY() bool WeaponPresent = false;
    UPROPERTY() bool BuffPresent = false;
    UPROPERTY() int32 WeaponDelta = 0;
    UPROPERTY() int32 BuffDelta = 0;
    UPROPERTY() int32 ModifierPhase = -1;
    UPROPERTY() int32 ModifierRunStatus = -1;
    UPROPERTY() int32 ModifierCount = 0;
    UPROPERTY() bool IsIncreasing = false;
    UPROPERTY() int32 CurrentTestValue = 0;
    UPROPERTY() int32 LastInputValue = 0;
    UPROPERTY() int32 MinClampCount = 0;
    UPROPERTY() int32 MaxClampCount = 0;
    UPROPERTY() int32 LastPreClampValue = 0;
    UPROPERTY() int32 LastClampedValue = 0;
    UPROPERTY() int32 LastOverflow = 0;
    UPROPERTY() int32 ClampEventSerial = 0;
    UPROPERTY() int32 BoundaryMinEventSerial = 0;
    UPROPERTY() int32 BoundaryMaxEventSerial = 0;
    UPROPERTY() int32 BoundaryMinPre = 0;
    UPROPERTY() int32 BoundaryMinFinal = 0;
    UPROPERTY() int32 BoundaryMinOverflow = 0;
    UPROPERTY() int32 BoundaryMaxPre = 0;
    UPROPERTY() int32 BoundaryMaxFinal = 0;
    UPROPERTY() int32 BoundaryMaxOverflow = 0;
    UPROPERTY() FString Title;
    UPROPERTY() FString Body;
}
USTRUCT()
struct FCkAttributeGym_IntegerSnapshot
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() TArray<FCkAttributeGym_IntegerStationSample> Stations;
}
USTRUCT()
struct FCkAttributeGym_IntegerControlCommit
{
    UPROPERTY() int32 Serial = 0;
    UPROPERTY() int32 Action = -1;
    UPROPERTY() int32 Preset = -1;
    UPROPERTY() FCkAttributeGym_IntegerSnapshot Snapshot;
}
USTRUCT()
struct FIntegerStationSpawnParams
{
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() bool WaitForWorldReady = true;
}
USTRUCT()
struct FIntegerProjectionSpawnParams
{
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 StationIndex = -1;
}

namespace CkAttributeInteger
{
    bool IsValidAttribute(FCkAttributeGym_IntegerAttributeSample InSample,
        FName InName, bool InHasMin, bool InHasMax)
    {
        if (InSample.Name != InName || InSample.Min.Present != InHasMin ||
            InSample.Max.Present != InHasMax || !InSample.Current.Present ||
            int64(InSample.PreClampFinal) - int64(InSample.Current.Final) !=
                int64(InSample.ClampOverflow))
        { return false; }
        if (InHasMin && InSample.Min.Final != InSample.Min.Base + InSample.Min.Bonus) { return false; }
        if (InHasMax && InSample.Max.Final != InSample.Max.Base + InSample.Max.Bonus) { return false; }
        return true;
    }
    bool IsValidSample(FCkAttributeGym_IntegerStationSample InSample)
    {
        auto Index = InSample.StationIndex;
        if (Index < 0 || Index >= 4 || InSample.StationTag != Get_StationTag(Index) ||
            InSample.ChildTag != Get_ChildTag(Index) || InSample.AutoStep < 0 ||
            InSample.ValueChangeCount < 0 || InSample.HealthChangeCount < 0 ||
            InSample.ArmorChangeCount < 0 || InSample.ExperienceChangeCount < 0 ||
            InSample.MinChangeCount < 0 || InSample.MaxChangeCount < 0 ||
            InSample.CurrentChangeCount < 0 || InSample.MinClampCount < 0 ||
            InSample.MaxClampCount < 0 || InSample.ClampEventSerial < 0 ||
            InSample.BoundaryMinEventSerial < 0 || InSample.BoundaryMaxEventSerial < 0)
        { return false; }
        if (Index == 0)
        {
            if (InSample.Attributes.Num() != 3) { return false; }
            auto H = InSample.Attributes[0]; auto A = InSample.Attributes[1];
            auto X = InSample.Attributes[2];
            return IsValidAttribute(H, n"IntegerAttribute.Health", true, true) &&
                IsValidAttribute(A, n"IntegerAttribute.Armor", true, true) &&
                IsValidAttribute(X, n"IntegerAttribute.Experience", true, false) &&
                H.Min.Final == 0 && H.Max.Final == 100 &&
                A.Min.Final == 0 && A.Max.Final == 50 && X.Min.Final == 0 &&
                H.Current.Final == Math::Clamp(H.PreClampFinal, 0, 100) &&
                A.Current.Final == Math::Clamp(A.PreClampFinal, 0, 50) &&
                X.Current.Final == Math::Max(X.PreClampFinal, 0);
        }
        if (Index == 1)
        {
            if (InSample.Attributes.Num() != 1) { return false; }
            auto P = InSample.Attributes[0];
            return IsValidAttribute(P, n"IntegerAttribute.PowerLevel", true, true) &&
                P.Current.Final == Math::Min(Math::Max(P.PreClampFinal, P.Min.Final), P.Max.Final);
        }
        if (Index == 2)
        {
            return InSample.Attributes.Num() == 1 &&
                (InSample.ModifierRunStatus == 1 || InSample.ModifierRunStatus == 2) &&
                InSample.AutoRunning == (InSample.ModifierRunStatus == 1) &&
                InSample.ModifierPhase >= 0 &&
                InSample.ModifierPhase <= 3 && InSample.ModifierCount >= 0 &&
                InSample.ModifierCount <= 2 &&
                InSample.ModifierCount == (InSample.WeaponPresent ? 1 : 0) + (InSample.BuffPresent ? 1 : 0) &&
                (!InSample.WeaponPresent ||
                    (InSample.WeaponDelta == 25 || InSample.WeaponDelta == 50 ||
                     InSample.WeaponDelta == -25 || InSample.WeaponDelta == -200)) &&
                (!InSample.BuffPresent ||
                    (InSample.BuffDelta == 10 || InSample.BuffDelta == 40 ||
                     InSample.BuffDelta == -10 || InSample.BuffDelta == -150)) &&
                IsValidAttribute(InSample.Attributes[0], n"IntegerAttribute.Damage", true, false) &&
                InSample.Attributes[0].Min.Final == 0 &&
                InSample.Attributes[0].Current.Base == 100 &&
                InSample.Attributes[0].PreClampFinal ==
                    100 + InSample.WeaponDelta + InSample.BuffDelta &&
                InSample.Attributes[0].Current.Final ==
                    Math::Max(InSample.Attributes[0].PreClampFinal, 0) &&
                InSample.Attributes[0].Current.Bonus ==
                    InSample.Attributes[0].Current.Final -
                        InSample.Attributes[0].Current.Base;
        }
        return InSample.Attributes.Num() == 1 &&
            InSample.BoundaryMinEventSerial <= InSample.ClampEventSerial &&
            InSample.BoundaryMaxEventSerial <= InSample.ClampEventSerial &&
            IsValidAttribute(InSample.Attributes[0], n"IntegerAttribute.Resource", true, true) &&
            InSample.Attributes[0].Min.Final == 0 &&
            InSample.Attributes[0].Max.Final == 100 &&
            InSample.Attributes[0].Current.Final ==
                Math::Clamp(InSample.Attributes[0].PreClampFinal, 0, 100);
    }
    bool IsValidSnapshot(FCkAttributeGym_IntegerSnapshot InSnapshot)
    {
        if (InSnapshot.Generation <= 0 || InSnapshot.Sequence <= 0 ||
            !InSnapshot.Ready || InSnapshot.Failed || InSnapshot.Stations.Num() != 4)
        { return false; }
        for (int32 i = 0; i < 4; i++)
        { if (InSnapshot.Stations[i].StationIndex != i ||
              !IsValidSample(InSnapshot.Stations[i])) { return false; } }
        return true;
    }
}


class ACk_IntegerAttributeGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkAttributeGym_IntegerSnapshot Snapshot;
    UPROPERTY(Replicated) FCkAttributeGym_IntegerControlCommit LastControlCommit;
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
    private bool _StartIssued = false;
    private bool _StartReleased = false;
    private int32 _NextStartupDiagnosticSlot = 5;

    private bool _TransactionPending = false;
    private int32 _TransactionPhase = 0;
    private int32 _TransactionAction = -1;
    private int32 _TransactionSerial = 0;
    private int32 _TransactionGeneration = 0;
    private int32 _TransactionPreset = -1;
    private float _TransactionSeconds = 0.0;
    private TArray<bool> _ResumeAuto;
    private ACk_IntegerAttributeGym_PlayerController _TransactionOwner;

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
        Snapshot = FCkAttributeGym_IntegerSnapshot();
        LastControlCommit = FCkAttributeGym_IntegerControlCommit();
        Snapshot.Generation = _NextGeneration;
        _Stations.Empty(); _NewStations.Empty(); _PendingStations.Empty();
        _PendingChildren.Empty(); ServerChildren.Empty();
        _StationsReady = false; _StartIssued = false; _StartReleased = false;
        _NextStartupDiagnosticSlot = 5;
        for (int32 i = 0; i < 4; i++)
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
        _TransactionPhase = 0;
        Snapshot.Ready = false;
        Snapshot.Failed = true;
        Snapshot.FailureReason = InReason;
        Snapshot.Sequence++;
        Request_CloseWorldOnFailure();
        ForceNetUpdate();
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        { ck::Error("[AttributeInteger] " + InReason); return; }
        if (ck::IsValid(_TransactionOwner) &&
            _TransactionOwner.GetWorld() == GetWorld() &&
            _TransactionGeneration == Snapshot.Generation)
        { _TransactionOwner.Request_CompleteIntegerControl(_TransactionSerial,
            _TransactionAction, _TransactionPreset, _TransactionGeneration, false); }
        _TransactionOwner = nullptr;
        _ResumeAuto.Empty();
        Request_RetireServerChildren();
        for (int32 i = 0; i < _PendingStations.Num(); i++)
        {
            if (utils_pending_entity_script::Get_IsValid(_PendingStations[i]))
            { utils_pending_entity_script::Request_DestroyEntity(_PendingStations[i]); }
            _PendingStations[i] = FCk_Handle_PendingEntityScript();
        }
        Request_ReleaseAdmission();
        if (CkAttributeInteger::IsLiveEntity(_WorldStationOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); }
        _WorldStationOwner = FCk_Handle();
        ck::Error("[AttributeInteger] " + InReason);
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
            else if (i < ServerChildren.Num() && CkAttributeInteger::IsLiveEntity(ServerChildren[i]))
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
        if (CkAttributeInteger::IsLiveEntity(_AdmissionOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_AdmissionOwner); }
        _AdmissionOwner = FCk_Handle();
        return true;
    }

    private void Request_EnsureWorldStations()
    {
        if (!_Starting || _StationsReady || _AdmissionOwned) { return; }
        auto World = ck::TransientEntity();
        if (!CkAttributeInteger::IsLiveEntity(World) ||
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
        if (!CkAttributeInteger::IsLiveEntity(_AdmissionOwner))
        { Request_Fail("Admission allocation failed"); return; }
        if (!CkGym_Admission::TryCommitOwner(World, _AdmissionTicket, _AdmissionOwner))
        { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
          Request_Fail("Station admission owner commit rejected"); return; }
        _AdmissionOwned = true;
        auto Specs = CkAttributeInteger::Get_Stations();
        if (Specs.Num() != 4) { Request_Fail("Station declaration invalid"); return; }
        for (int32 i = 0; i < 4; i++)
        {
            if (Specs[i].Tags.Num() != 1 || Specs[i].Tags[0] != CkAttributeInteger::Get_StationTag(i))
            { Request_Fail("Station tag declaration invalid"); return; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, Specs[i].Tags[0]);
            if (Matches.Num() > 1 || (Matches.Num() == 1 &&
                (!CkAttributeInteger::IsLiveEntity(Matches[0]) ||
                 !Matches[0].Has_Fragment(FCkGym_Station_Anchors))))
            { Request_Fail("Station identity ambiguous"); return; }
            if (Matches.Num() == 1) { _Stations[i] = Matches[0]; }
        }
        bool Missing = false;
        for (int32 i = 0; i < 4; i++)
        { if (!CkAttributeInteger::IsLiveEntity(_Stations[i])) { Missing = true; } }
        if (Missing)
        {
            _WorldStationOwner = utils_entity_lifetime::Request_CreateEntity(World);
            if (!CkAttributeInteger::IsLiveEntity(_WorldStationOwner))
            { Request_Fail("World station group allocation failed"); return; }
            for (int32 i = 0; i < 4; i++)
            {
                if (CkAttributeInteger::IsLiveEntity(_Stations[i])) { continue; }
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
            if (!CkAttributeInteger::IsLiveEntity(Child) ||
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
        if (!CkAttributeInteger::IsLiveEntity(World))
        { Request_Fail("World lost during station settle"); return; }
        for (int32 i = 0; i < 4; i++)
        {
            if (!CkAttributeInteger::IsLiveEntity(_Stations[i]))
            { System::SetTimer(this, n"OnStationSettle", 0.05, false); return; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, CkAttributeInteger::Get_StationTag(i));
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
        for (int32 i = 0; i < 4; i++)
        {
            if (!CkAttributeInteger::IsLiveEntity(_Stations[i]) ||
                !_Stations[i].Has_Fragment(FCkGym_Station_Anchors) ||
                utils_entity_tag::ForEach_Entity(World, CkAttributeInteger::Get_ChildTag(i)).Num() != 0)
            { Request_Fail("Simulation child preflight failed"); return; }
        }
        for (int32 i = 0; i < 4; i++)
        {
            auto Params = FIntegerStationSpawnParams();
            Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
                _Stations[i].Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
            Params.WaitForWorldReady = true;
            auto Owner = _Stations[i];
            if (i == 0) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_IntegerGym_Basic, FInstancedStruct::Make(Params)); }
            if (i == 1) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_IntegerGym_MinMaxCurrent, FInstancedStruct::Make(Params)); }
            if (i == 2) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_IntegerGym_Modifiers, FInstancedStruct::Make(Params)); }
            if (i == 3) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_IntegerGym_Clamping, FInstancedStruct::Make(Params)); }
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

    // Read-only startup census. Each getter is reached only after its owner, script and
    // feature checks; the snapshot builder is probed only after all strict roots are valid.
    private void Request_LogStartupDiagnostics(int32 InSlot)
    {
        if (!HasAuthority() || !_Starting || Snapshot.Failed) { return; }
        auto World = ck::TransientEntity();
        const bool WorldLive = CkAttributeInteger::IsLiveEntity(World);
        FString First = WorldLive ? "none" : "world-live";
        if (First == "none" && !_StationsReady) { First = "stations-ready"; }
        FString Detail = "";
        bool AllStrict = true;
        bool AllStarted = true;
        FString FirstUnsettled = "none";
        for (int32 i = 0; i < 4; i++)
        {
            const bool StationLive = i < _Stations.Num() &&
                CkAttributeInteger::IsLiveEntity(_Stations[i]);
            const bool ChildLive = i < ServerChildren.Num() &&
                CkAttributeInteger::IsLiveEntity(ServerChildren[i]);
            auto Child = ChildLive ? ServerChildren[i] : FCk_Handle();
            const bool OwnerMatches = StationLive && ChildLive &&
                utils_entity_lifetime::Get_LifetimeOwner(Child) == _Stations[i];
            int32 DirectChildren = -1;
            int32 LiveDirectChildren = -1;
            if (ChildLive)
            {
                auto Dependents = utils_entity_lifetime::Get_LifetimeDependents(Child);
                DirectChildren = Dependents.Num();
                LiveDirectChildren = 0;
                for (auto Dependent : Dependents)
                { if (CkAttributeInteger::IsLiveEntity(Dependent)) { LiveDirectChildren++; } }
            }
            UCk_EntityScript_UE Script = nullptr;
            if (ChildLive)
            { Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Child); }
            const bool ScriptBound = ck::IsValid(Script) && ck::ToEntity(Script) == Child;
            bool Composed = false;
            bool StartSettled = false;
            bool StateSettled = false;
            FString SmDetail = "";
            if (ScriptBound && i == 0)
            {
                auto Typed = Cast<UCk_EntityScript_IntegerGym_Basic>(Script);
                if (ck::IsValid(Typed))
                {
                    Composed = Typed.Get_HasValidAuthorityComposition();
                    if (Composed)
                    { StartSettled = Typed.Get_HasSettledAuthorityStart();
                      StateSettled = Typed.Get_HasSettledAuthorityState(); }
                }
            }
            if (ScriptBound && i == 1)
            {
                auto Typed = Cast<UCk_EntityScript_IntegerGym_MinMaxCurrent>(Script);
                if (ck::IsValid(Typed))
                {
                    Composed = Typed.Get_HasValidAuthorityComposition();
                    if (Composed)
                    { StartSettled = Typed.Get_HasSettledAuthorityStart();
                      StateSettled = Typed.Get_HasSettledAuthorityState(); }
                }
            }
            if (ScriptBound && i == 2)
            {
                auto Typed = Cast<UCk_EntityScript_IntegerGym_Modifiers>(Script);
                if (ck::IsValid(Typed))
                {
                    Composed = Typed.Get_HasValidAuthorityComposition();
                    const bool SmHas = Composed && utils_state_machine::Has(Child);
                    bool DefaultsSettled = false;
                    bool CurrentStateLive = false;
                    FString Run = "unavailable";
                    FString Phase = "unavailable";
                    if (SmHas)
                    {
                        auto Status = Typed.StepMachine.Get_RunStatus();
                        Run = Status == ECk_SmRunStatus::Running ? "running" :
                            (Status == ECk_SmRunStatus::Paused ? "paused" : "stopped");
                        auto CurrentClass = Typed.StepMachine.Get_CurrentStateClass();
                        if (CurrentClass == UCk_IntegerGym_Step_AddDefaults) { Phase = "defaults"; }
                        else if (CurrentClass == UCk_IntegerGym_Step_AddWeapon) { Phase = "weapon"; }
                        else if (CurrentClass == UCk_IntegerGym_Step_AddBuff) { Phase = "buff"; }
                        else if (CurrentClass == UCk_IntegerGym_Step_ClearAll) { Phase = "clear"; }
                        else { Phase = "none-or-unknown"; }
                        CurrentStateLive = CkAttributeInteger::IsLiveEntity(
                            FCk_Handle(Typed.StepMachine.Get_CurrentStateHandle()));
                        DefaultsSettled = Typed.Get_HasSettledInitialDefaults();
                        StateSettled = Typed.Get_HasSettledAuthorityState();
                        StartSettled = DefaultsSettled && StateSettled;
                    }
                    SmDetail = f" smHas={SmHas} run={Run} phase={Phase} currentLive={CurrentStateLive} defaultsEntrySettled={DefaultsSettled} stepFailed={Typed.Get_HasStepFailure()}";
                }
            }
            if (ScriptBound && i == 3)
            {
                auto Typed = Cast<UCk_EntityScript_IntegerGym_Clamping>(Script);
                if (ck::IsValid(Typed))
                {
                    Composed = Typed.Get_HasValidAuthorityComposition();
                    if (Composed)
                    { StartSettled = Typed.Get_HasSettledAuthorityStart();
                      StateSettled = Typed.Get_HasSettledAuthorityState(); }
                }
            }
            const bool Strict = OwnerMatches && ScriptBound &&
                Get_IsValidServerScript(i);
            AllStrict = AllStrict && Strict;
            AllStarted = AllStarted && StartSettled;
            if (FirstUnsettled == "none" && !StartSettled)
            { FirstUnsettled = f"child[{i}]-start-settled"; }
            if (First == "none")
            {
                if (!StationLive) { First = f"station[{i}]-live"; }
                else if (!ChildLive) { First = f"child[{i}]-live"; }
                else if (!OwnerMatches) { First = f"child[{i}]-owner"; }
                else if (!ScriptBound) { First = f"child[{i}]-script"; }
                else if (!Composed) { First = f"child[{i}]-composition"; }
                else if (!Strict) { First = f"child[{i}]-strict-census"; }
            }
            Detail += f" [{i}:station={StationLive} child={ChildLive} owner={OwnerMatches} script={ScriptBound} direct={LiveDirectChildren}/{DirectChildren} composition={Composed} strict={Strict} start={StartSettled} state={StateSettled}{SmDetail}]";
        }
        const bool QuartetValid = AllStrict && Get_IsValidServerChildren();
        bool SampleBuilds = false;
        if (QuartetValid && _StartIssued && _StartReleased)
        {
            auto Candidate = FCkAttributeGym_IntegerSnapshot();
            SampleBuilds = Request_TryBuildSnapshot(Candidate);
        }
        if (First == "none" && !QuartetValid) { First = "quartet-catalog-or-owner"; }
        if (First == "none" && !_StartIssued) { First = "start-issued"; }
        if (First == "none" && !AllStarted) { First = FirstUnsettled; }
        if (First == "none" && !_StartReleased) { First = "start-released"; }
        if (First == "none" && !SampleBuilds) { First = "snapshot-build"; }
        const float Elapsed = float(System::GetGameTimeInSeconds()) - _StartSeconds;
        ck::Trace(f"[AttributeIntegerStartup] slot={InSlot}s elapsed={Elapsed} first={First} stationsReady={_StationsReady} startIssued={_StartIssued} startReleased={_StartReleased} strictAll={AllStrict} quartetValid={QuartetValid} startAll={AllStarted} sampleBuilds={SampleBuilds}{Detail}");
    }

    UFUNCTION() private void OnStartupDeadline()
    {
        if (!_Starting || Snapshot.Ready || Snapshot.Failed) { return; }
        auto Elapsed = float(System::GetGameTimeInSeconds()) - _StartSeconds;
        if (Elapsed < 20.0)
        { System::SetTimer(this, n"OnStartupDeadline", 20.0 - Elapsed, false); return; }
        Request_LogStartupDiagnostics(20);
        Request_Fail("World quartet readiness timed out");
    }

    bool Get_IsExpectedServerChild(FCk_Handle InChild, int32 InGeneration, int32 InIndex)
    {
        return HasAuthority() && _Starting && !Snapshot.Failed &&
            InGeneration == Snapshot.Generation && InIndex >= 0 && InIndex < 4 &&
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

    private bool Request_AppendExactMessenger(FCk_Handle InOwner, UScriptStruct InMessageTemplate,
        TArray<FCk_Handle>& InOutExpected)
    {
        if (!CkAttributeInteger::IsLiveEntity(InOwner) || !ck::IsValid(InMessageTemplate)) { return false; }
        auto Messenger = utils_messaging::TryGet_MessengerEntity(InOwner, InMessageTemplate);
        if (!CkAttributeInteger::IsLiveEntity(Messenger) ||
            utils_entity_lifetime::Get_LifetimeOwner(Messenger) != InOwner ||
            InOutExpected.Contains(Messenger)) { return false; }
        InOutExpected.Add(Messenger);
        return true;
    }
    private bool Get_HasExactDirectChildren(FCk_Handle InOwner, TArray<FCk_Handle> InExpected)
    {
        if (!CkAttributeInteger::IsLiveEntity(InOwner)) { return false; }
        auto Actual = utils_entity_lifetime::Get_LifetimeDependents(InOwner);
        int32 LiveCount = 0;
        for (auto Child : Actual)
        {
            if (!CkAttributeInteger::IsLiveEntity(Child)) { return false; }
            LiveCount++;
            if (!InExpected.Contains(Child)) { return false; }
        }
        if (LiveCount != InExpected.Num()) { return false; }
        for (int32 i = 0; i < InExpected.Num(); i++)
        {
            auto Expected = InExpected[i];
            if (!CkAttributeInteger::IsLiveEntity(Expected) ||
                utils_entity_lifetime::Get_LifetimeOwner(Expected) != InOwner) { return false; }
            for (int32 j = 0; j < i; j++)
            { if (InExpected[j] == Expected) { return false; } }
        }
        return true;
    }
    private bool Get_IsValidServerScript(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= ServerChildren.Num() ||
            !CkAttributeInteger::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Child = ServerChildren[InIndex];
        auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Child);
        if (ck::Is_NOT_Valid(Script) || ck::ToEntity(Script) != Child) { return false; }
        auto Expected = TArray<FCk_Handle>();
        if (InIndex == 0)
        {
            auto Typed = Cast<UCk_EntityScript_IntegerGym_Basic>(Script);
            if (ck::Is_NOT_Valid(Typed) || !Typed.WaitForWorldReady) { return false; }
            Expected.Add(FCk_Handle(Typed.HealthAttribute));
            Expected.Add(FCk_Handle(Typed.ArmorAttribute));
            Expected.Add(FCk_Handle(Typed.ExperienceAttribute));
            Expected.Add(FCk_Handle(Typed.AutoTimer));
            Expected.Add(FCk_Handle(Typed.DisplayTimer));
            if (!Request_AppendExactMessenger(Child, FCk_Message_Gym_AutoSet, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_IntegerGym_SetHealth, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_IntegerGym_SetArmor, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_IntegerGym_SetExperience, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_AttributeGym_ResetAttributes, Expected))
            { return false; }
            if (!Get_HasExactDirectChildren(Child, Expected)) { return false; }
            return Typed.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 1)
        {
            auto Typed = Cast<UCk_EntityScript_IntegerGym_MinMaxCurrent>(Script);
            if (ck::Is_NOT_Valid(Typed) || !Typed.WaitForWorldReady) { return false; }
            Expected.Add(FCk_Handle(Typed.PowerLevelAttribute));
            Expected.Add(FCk_Handle(Typed.AutoTimer));
            Expected.Add(FCk_Handle(Typed.DisplayTimer));
            if (!Request_AppendExactMessenger(Child, FCk_Message_Gym_AutoSet, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_AttributeGym_ResetAttributes, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_IntegerGym_SetValue, Expected))
            { return false; }
            if (!Get_HasExactDirectChildren(Child, Expected)) { return false; }
            return Typed.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 2)
        {
            auto Typed = Cast<UCk_EntityScript_IntegerGym_Modifiers>(Script);
            if (ck::Is_NOT_Valid(Typed) || !Typed.WaitForWorldReady ||
                FCk_Handle(Typed.StepMachine) != Child ||
                !Typed.Get_HasValidAuthorityComposition()) { return false; }
            Expected.Add(FCk_Handle(Typed.DamageAttribute));
            Expected.Add(FCk_Handle(Typed.DisplayTimer));
            if (!Request_AppendExactMessenger(Child, FCk_Message_IntegerGym_AddModifier, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_IntegerGym_RemoveModifier, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_IntegerGym_ClearModifiers, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_AttributeGym_ResetAttributes, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_Gym_AutoSet, Expected))
            { return false; }
            auto Children = utils_entity_lifetime::Get_LifetimeDependents(Child);
            int32 Required = 0; int32 StateCount = 0;
            FCk_Handle StateChild;
            for (auto Dependent : Children)
            {
                if (!CkAttributeInteger::IsLiveEntity(Dependent))
                {
                    if (!Typed.Get_IsExpectedRetiringState(Dependent)) { return false; }
                    continue;
                }
                if (Expected.Contains(Dependent)) { Required++; continue; }
                auto StateScript = Cast<UCk_Gym_StepState>(
                    UCk_Utils_EntityScript_UE::TryGet_EntityScript(Dependent));
                if (ck::Is_NOT_Valid(StateScript) ||
                    utils_entity_lifetime::Get_LifetimeOwner(Dependent) != Child)
                { return false; }
                StateCount++; StateChild = Dependent;
            }
            if (Required != Expected.Num()) { return false; }
            for (int32 i = 0; i < Expected.Num(); i++)
            {
                if (!CkAttributeInteger::IsLiveEntity(Expected[i]) ||
                    utils_entity_lifetime::Get_LifetimeOwner(Expected[i]) != Child) { return false; }
                for (int32 j = 0; j < i; j++)
                { if (Expected[j] == Expected[i]) { return false; } }
            }
            auto Current = Typed.StepMachine.Get_CurrentStateHandle();
            if (ck::IsValid(Current))
            { if (StateCount != 1 || StateChild != FCk_Handle(Current)) { return false; } }
            else if (StateCount != 0 ||
                Typed.StepMachine.Get_RunStatus() != ECk_SmRunStatus::Stopped)
            { return false; }
            return true;
        }
        if (InIndex == 3)
        {
            auto Typed = Cast<UCk_EntityScript_IntegerGym_Clamping>(Script);
            if (ck::Is_NOT_Valid(Typed) || !Typed.WaitForWorldReady) { return false; }
            Expected.Add(FCk_Handle(Typed.ResourceAttribute));
            Expected.Add(FCk_Handle(Typed.AutoTimer));
            Expected.Add(FCk_Handle(Typed.UpdateTimer));
            Expected.Add(FCk_Handle(Typed.DisplayTimer));
            if (!Request_AppendExactMessenger(Child, FCk_Message_Gym_AutoSet, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_AttributeGym_ResetAttributes, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_AttributeGym_TestBoundaries, Expected) ||
                !Request_AppendExactMessenger(Child, FCk_Message_IntegerGym_SetResource, Expected))
            { return false; }
            if (!Get_HasExactDirectChildren(Child, Expected)) { return false; }
            return Typed.Get_HasValidAuthorityComposition();
        }
        return false;
    }
    private bool Get_IsValidServerChildren()
    {
        if (ServerChildren.Num() != 4 || _Stations.Num() != 4) { return false; }
        auto World = ck::TransientEntity();
        if (!CkAttributeInteger::IsLiveEntity(World)) { return false; }
        for (int32 i = 0; i < 4; i++)
        {
            auto Station = CkAttributeInteger::FindStation(i);
            auto Child = ServerChildren[i];
            if (!CkAttributeInteger::IsLiveEntity(Station) || Station != _Stations[i] ||
                !CkAttributeInteger::IsLiveEntity(Child) ||
                utils_entity_lifetime::Get_LifetimeOwner(Child) != Station)
            { return false; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, CkAttributeInteger::Get_ChildTag(i));
            if (Matches.Num() != 1 || Matches[0] != Child || !Get_IsValidServerScript(i))
            { return false; }
        }
        return true;
    }
    bool Get_HasReadyServerChildren()
    {
        if (!HasAuthority() || _TransactionPending || ServerChildCount != 4 ||
            !CkAttributeInteger::IsValidSnapshot(Snapshot)) { return false; }
        return Get_IsValidServerChildren();
    }
    bool Get_HasCompleteSnapshot()
    { return CkAttributeInteger::IsValidSnapshot(Snapshot); }
    bool Get_AutoRunning(int32 InIndex)
    {
        return Get_HasCompleteSnapshot() && InIndex >= 0 && InIndex < 4 &&
            Snapshot.Stations[InIndex].AutoRunning;
    }
    private FCkAttributeGym_IntegerAttributeSample MakeAttribute(
        FCk_Handle_IntegerAttribute InHandle, FName InName)
    {
        auto Out = FCkAttributeGym_IntegerAttributeSample();
        Out.Name = InName;
        if (utils_integer_attribute::Has_Component(InHandle, ECk_MinMaxCurrent::Min))
        {
            Out.Min.Present = true;
            Out.Min.Base = utils_integer_attribute::Get_BaseValue(InHandle, ECk_MinMaxCurrent::Min);
            Out.Min.Bonus = utils_integer_attribute::Get_BonusValue(InHandle, ECk_MinMaxCurrent::Min);
            Out.Min.Final = utils_integer_attribute::Get_FinalValue(InHandle, ECk_MinMaxCurrent::Min);
        }
        if (utils_integer_attribute::Has_Component(InHandle, ECk_MinMaxCurrent::Max))
        {
            Out.Max.Present = true;
            Out.Max.Base = utils_integer_attribute::Get_BaseValue(InHandle, ECk_MinMaxCurrent::Max);
            Out.Max.Bonus = utils_integer_attribute::Get_BonusValue(InHandle, ECk_MinMaxCurrent::Max);
            Out.Max.Final = utils_integer_attribute::Get_FinalValue(InHandle, ECk_MinMaxCurrent::Max);
        }
        Out.Current.Present = true;
        Out.Current.Base = utils_integer_attribute::Get_BaseValue(InHandle);
        Out.Current.Bonus = utils_integer_attribute::Get_BonusValue(InHandle);
        Out.Current.Final = utils_integer_attribute::Get_FinalValue(InHandle);
        Out.PreClampFinal = utils_integer_attribute::Get_PreClampFinalValue(InHandle);
        Out.ClampOverflow = utils_integer_attribute::Get_ClampOverflow(InHandle);
        return Out;
    }
    private bool Request_TryBuildSnapshot(FCkAttributeGym_IntegerSnapshot& Out)
    {
        if (!Get_IsValidServerChildren()) { return false; }
        auto Candidate = FCkAttributeGym_IntegerSnapshot();
        Candidate.Generation = Snapshot.Generation;
        Candidate.Sequence = Snapshot.Sequence + 1;
        Candidate.Ready = true;
        for (int32 i = 0; i < 4; i++)
        {
            auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[i]);
            auto Sample = FCkAttributeGym_IntegerStationSample();
            Sample.StationIndex = i;
            Sample.StationTag = CkAttributeInteger::Get_StationTag(i);
            Sample.ChildTag = CkAttributeInteger::Get_ChildTag(i);
            if (i == 0)
            {
                auto Basic = Cast<UCk_EntityScript_IntegerGym_Basic>(Script);
                if (ck::Is_NOT_Valid(Basic) || !Basic.Get_HasSettledAuthorityState()) { return false; }
                Sample.AutoRunning = Basic.AutoRunning;
                Sample.AutoStep = Basic.AutoStep;
                Sample.HealthChangeCount = Basic.HealthChangeCount;
                Sample.ArmorChangeCount = Basic.ArmorChangeCount;
                Sample.ExperienceChangeCount = Basic.ExperienceChangeCount;
                Sample.Attributes.Add(MakeAttribute(Basic.HealthAttribute, n"IntegerAttribute.Health"));
                Sample.Attributes.Add(MakeAttribute(Basic.ArmorAttribute, n"IntegerAttribute.Armor"));
                Sample.Attributes.Add(MakeAttribute(Basic.ExperienceAttribute, n"IntegerAttribute.Experience"));
                Sample.Title = Basic.LastDisplayTitle; Sample.Body = Basic.LastDisplayBody;
            }
            if (i == 1)
            {
                auto MinMax = Cast<UCk_EntityScript_IntegerGym_MinMaxCurrent>(Script);
                if (ck::Is_NOT_Valid(MinMax) || !MinMax.Get_HasSettledAuthorityState()) { return false; }
                Sample.AutoRunning = MinMax.AutoRunning;
                Sample.AutoStep = MinMax.AutoStep;
                Sample.MinChangeCount = MinMax.MinChangeCount;
                Sample.MaxChangeCount = MinMax.MaxChangeCount;
                Sample.CurrentChangeCount = MinMax.CurrentChangeCount;
                Sample.Attributes.Add(MakeAttribute(MinMax.PowerLevelAttribute, n"IntegerAttribute.PowerLevel"));
                Sample.Title = MinMax.LastDisplayTitle; Sample.Body = MinMax.LastDisplayBody;
            }
            if (i == 2)
            {
                auto Mods = Cast<UCk_EntityScript_IntegerGym_Modifiers>(Script);
                if (ck::Is_NOT_Valid(Mods) || !Mods.Get_HasSettledAuthorityState()) { return false; }
                auto Status = Mods.StepMachine.Get_RunStatus();
                Sample.AutoRunning = Status == ECk_SmRunStatus::Running;
                Sample.ModifierRunStatus = Status == ECk_SmRunStatus::Running ? 1 :
                    (Status == ECk_SmRunStatus::Paused ? 2 : 0);
                auto Phase = Mods.StepMachine.Get_CurrentStateClass();
                if (Phase == UCk_IntegerGym_Step_AddDefaults) { Sample.ModifierPhase = 0; }
                else if (Phase == UCk_IntegerGym_Step_AddWeapon) { Sample.ModifierPhase = 1; }
                else if (Phase == UCk_IntegerGym_Step_AddBuff) { Sample.ModifierPhase = 2; }
                else if (Phase == UCk_IntegerGym_Step_ClearAll) { Sample.ModifierPhase = 3; }
                else { return false; }
                Sample.ValueChangeCount = Mods.ValueChangeCount;
                auto Weapon = utils_integer_attribute_modifier::TryGet(Mods.DamageAttribute,
                    utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"), ECk_MinMaxCurrent::Current);
                auto Buff = utils_integer_attribute_modifier::TryGet(Mods.DamageAttribute,
                    utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"), ECk_MinMaxCurrent::Current);
                // A retiring record can still be registry-valid. Keep the old
                // coherent snapshot until the complete keyed set has settled.
                if (!integer_gym_modifiers::HasExactModifiers(
                    Mods.DamageAttribute, Weapon, Buff)) { return false; }
                auto All = utils_integer_attribute_modifier::ForEach(Mods.DamageAttribute,
                    FInstancedStruct(), FCk_Lambda_InHandle());
                Sample.WeaponPresent = ck::IsValid(Weapon);
                Sample.BuffPresent = ck::IsValid(Buff);
                Sample.ModifierCount = All.Num();
                if (Sample.WeaponPresent)
                { Sample.WeaponDelta = utils_integer_attribute_modifier::Get_Delta(Weapon); }
                if (Sample.BuffPresent)
                { Sample.BuffDelta = utils_integer_attribute_modifier::Get_Delta(Buff); }
                Sample.Attributes.Add(MakeAttribute(Mods.DamageAttribute, n"IntegerAttribute.Damage"));
                Sample.Title = Mods.LastDisplayTitle; Sample.Body = Mods.LastDisplayBody;
            }
            if (i == 3)
            {
                auto Clamp = Cast<UCk_EntityScript_IntegerGym_Clamping>(Script);
                if (ck::Is_NOT_Valid(Clamp) || !Clamp.Get_HasSettledAuthorityState()) { return false; }
                Sample.AutoRunning = Clamp.AutoRunning;
                Sample.AutoStep = Clamp.AutoStep;
                Sample.IsIncreasing = Clamp.IsIncreasing;
                Sample.CurrentTestValue = Clamp.CurrentTestValue;
                Sample.LastInputValue = Clamp.LastInputValue;
                Sample.MinClampCount = Clamp.MinClampCount;
                Sample.MaxClampCount = Clamp.MaxClampCount;
                Sample.LastPreClampValue = Clamp.LastPreClampValue;
                Sample.LastClampedValue = Clamp.LastClampedValue;
                Sample.LastOverflow = Clamp.LastOverflow;
                Sample.ClampEventSerial = Clamp.ClampEventSerial;
                Sample.BoundaryMinEventSerial = Clamp.BoundaryMinEventSerial;
                Sample.BoundaryMaxEventSerial = Clamp.BoundaryMaxEventSerial;
                Sample.BoundaryMinPre = Clamp.BoundaryMinPre;
                Sample.BoundaryMinFinal = Clamp.BoundaryMinFinal;
                Sample.BoundaryMinOverflow = Clamp.BoundaryMinOverflow;
                Sample.BoundaryMaxPre = Clamp.BoundaryMaxPre;
                Sample.BoundaryMaxFinal = Clamp.BoundaryMaxFinal;
                Sample.BoundaryMaxOverflow = Clamp.BoundaryMaxOverflow;
                Sample.Attributes.Add(MakeAttribute(Clamp.ResourceAttribute, n"IntegerAttribute.Resource"));
                Sample.Title = Clamp.LastDisplayTitle; Sample.Body = Clamp.LastDisplayBody;
            }
            if (!CkAttributeInteger::IsValidSample(Sample)) { return false; }
            Candidate.Stations.Add(Sample);
        }
        if (!CkAttributeInteger::IsValidSnapshot(Candidate)) { return false; }
        Out = Candidate;
        return true;
    }
    UFUNCTION() private void OnServerMonitor()
    {
        if (!HasAuthority() || Snapshot.Failed) { return; }
        if (_Starting)
        {
            const float Elapsed = float(System::GetGameTimeInSeconds()) - _StartSeconds;
            if (_NextStartupDiagnosticSlot < 20 &&
                Elapsed >= float(_NextStartupDiagnosticSlot))
            {
                Request_LogStartupDiagnostics(_NextStartupDiagnosticSlot);
                _NextStartupDiagnosticSlot += 5;
            }
            if (!Get_IsValidServerChildren()) { return; }
            auto Basic = Cast<UCk_EntityScript_IntegerGym_Basic>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[0]));
            auto MinMax = Cast<UCk_EntityScript_IntegerGym_MinMaxCurrent>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[1]));
            auto Mods = Cast<UCk_EntityScript_IntegerGym_Modifiers>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[2]));
            auto Clamp = Cast<UCk_EntityScript_IntegerGym_Clamping>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[3]));
            if (ck::Is_NOT_Valid(Basic) || ck::Is_NOT_Valid(MinMax) ||
                ck::Is_NOT_Valid(Mods) || ck::Is_NOT_Valid(Clamp))
            { Request_Fail("Authority station script lost during startup"); return; }
            if (Basic.Get_HasAuthorityFailure() || MinMax.Get_HasAuthorityFailure() ||
                Mods.Get_HasStepFailure() || Clamp.Get_HasAuthorityFailure())
            { Request_Fail("Authority station startup failed"); return; }
            if (!_StartIssued)
            {
                _StartIssued = true;
                if (!Basic.Request_StartAuthorityDemo() ||
                    !MinMax.Request_StartAuthorityDemo() ||
                    !Mods.Request_StartAuthorityDemo() ||
                    !Clamp.Request_StartAuthorityDemo())
                { Request_Fail("Authority startup admission rejected"); }
                return;
            }
            if (!Basic.Get_HasSettledAuthorityStart() ||
                !MinMax.Get_HasSettledAuthorityStart() ||
                !Mods.Get_HasSettledInitialDefaults() ||
                !Mods.Get_HasSettledAuthorityState() ||
                !Clamp.Get_HasSettledAuthorityStart()) { return; }
            if (!_StartReleased)
            {
                // Every station is already settled before any gate is opened.
                if (!Basic.Request_ReleaseAuthorityStart() ||
                    !MinMax.Request_ReleaseAuthorityStart() ||
                    !Mods.Request_ReleaseAuthorityStart() ||
                    !Clamp.Request_ReleaseAuthorityStart())
                { Request_Fail("Authority startup release rejected"); return; }
                _StartReleased = true;
            }
            auto Initial = FCkAttributeGym_IntegerSnapshot();
            if (!Request_TryBuildSnapshot(Initial)) { return; }
            Snapshot = Initial; ServerChildCount = 4; _Starting = false;
            _LastPublishSeconds = float(System::GetGameTimeInSeconds());
            ForceNetUpdate();
            return;
        }
        if (!Snapshot.Ready || ServerChildCount != 4) { return; }
        if (_TransactionPending) { Request_CheckTransaction(); return; }
        auto Now = float(System::GetGameTimeInSeconds());
        if (!Get_IsValidServerChildren())
        {
            if (Now - _LastPublishSeconds >= 20.0)
            { Request_Fail("Authority quartet dependency failed to settle"); }
            return;
        }
        if (Now - _LastPublishSeconds < 0.1) { return; }
        auto Candidate = FCkAttributeGym_IntegerSnapshot();
        if (!Request_TryBuildSnapshot(Candidate))
        {
            // An authored auto transition may temporarily have no complete sample.
            // Preserve the previous coherent snapshot while it settles.
            if (Now - _LastPublishSeconds >= 20.0)
            { Request_Fail("Authority quartet sample failed to settle"); }
            return;
        }
        Snapshot = Candidate; _LastPublishSeconds = Now; ForceNetUpdate();
    }
    private int32 Get_TargetStation(int32 InAction)
    {
        if (InAction == 3 || InAction == 8 || InAction == 9 || InAction == 10) { return 0; }
        if (InAction == 4 || InAction == 12 || InAction == 13 || InAction == 14) { return 1; }
        if (InAction == 5 || (InAction >= 16 && InAction <= 20)) { return 2; }
        if (InAction == 6 || InAction == 22 || InAction == 23 || InAction == 24) { return 3; }
        return -1;
    }
    bool Request_ApplyControl(ACk_IntegerAttributeGym_PlayerController InOwner,
        int32 InSerial, int32 InAction, int32 InPreset)
    {
        if (!HasAuthority() || !Get_HasReadyServerChildren() || _TransactionPending ||
            ck::Is_NOT_Valid(InOwner) || InOwner.GetWorld() != GetWorld() ||
            InSerial <= 0 || !CkAttributeInteger::IsAction(InAction) ||
            (CkAttributeInteger::PresetCount(InAction) == 0 && InPreset != -1) ||
            (CkAttributeInteger::PresetCount(InAction) > 0 &&
                (InPreset < 0 || InPreset >= CkAttributeInteger::PresetCount(InAction))))
        { return false; }
        _ResumeAuto.Empty();
        for (int32 i = 0; i < 4; i++) { _ResumeAuto.Add(Snapshot.Stations[i].AutoRunning); }
        // The request owns a serialized world transaction until all four finish.
        _TransactionPending = true; _TransactionPhase = 0;
        _TransactionAction = InAction; _TransactionPreset = InPreset;
        _TransactionSerial = InSerial; _TransactionGeneration = Snapshot.Generation;
        _TransactionOwner = InOwner;
        _TransactionSeconds = float(System::GetGameTimeInSeconds());
        Request_CheckTransaction();
        return !Snapshot.Failed;
    }
    private bool Get_HasAnyStationFailure()
    {
        if (ServerChildren.Num() != 4) { return true; }
        // State-machine transitions can create and retire a state child while
        // the stable station dependencies remain live.
        {
            for (int32 i = 0; i < 4; i++)
            {
                auto Station = CkAttributeInteger::FindStation(i);
                if (!CkAttributeInteger::IsLiveEntity(Station) ||
                    !CkAttributeInteger::IsLiveEntity(ServerChildren[i]) ||
                    utils_entity_lifetime::Get_LifetimeOwner(ServerChildren[i]) != Station)
                { return true; }
                if (i != 2 && !Get_IsValidServerScript(i)) { return true; }
            }
        }
        auto Basic = Cast<UCk_EntityScript_IntegerGym_Basic>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[0]));
        auto MinMax = Cast<UCk_EntityScript_IntegerGym_MinMaxCurrent>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[1]));
        auto Mods = Cast<UCk_EntityScript_IntegerGym_Modifiers>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[2]));
        auto Clamp = Cast<UCk_EntityScript_IntegerGym_Clamping>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[3]));
        if (ck::IsValid(Mods) &&
            (!CkAttributeInteger::IsLiveEntity(FCk_Handle(Mods.DamageAttribute)) ||
             !CkAttributeInteger::IsLiveEntity(FCk_Handle(Mods.DisplayTimer)) ||
             FCk_Handle(Mods.StepMachine) != ServerChildren[2] ||
             !Mods.Get_HasValidAuthorityComposition()))
        { return true; }
        return ck::Is_NOT_Valid(Basic) || ck::Is_NOT_Valid(MinMax) ||
            ck::Is_NOT_Valid(Mods) || ck::Is_NOT_Valid(Clamp) ||
            Basic.Get_HasAuthorityFailure() || MinMax.Get_HasAuthorityFailure() ||
            Mods.Get_HasStepFailure() || Clamp.Get_HasAuthorityFailure();
    }
    private void Request_CheckTransaction()
    {
        if (!_TransactionPending || Snapshot.Failed) { return; }
        // The admitted command belongs to this generation, not to the
        // continued lifetime of its issuing controller. A departing owner
        // loses its acknowledgement; the world still settles atomically.
        if (_TransactionGeneration != Snapshot.Generation || Get_HasAnyStationFailure())
        { Request_Fail("Integer transaction generation or station lost"); return; }
        if (float(System::GetGameTimeInSeconds()) - _TransactionSeconds >= 20.0)
        { Request_Fail("Integer transaction timed out"); return; }
        auto Basic = Cast<UCk_EntityScript_IntegerGym_Basic>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[0]));
        auto MinMax = Cast<UCk_EntityScript_IntegerGym_MinMaxCurrent>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[1]));
        auto Mods = Cast<UCk_EntityScript_IntegerGym_Modifiers>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[2]));
        auto Clamp = Cast<UCk_EntityScript_IntegerGym_Clamping>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[3]));
        if (_TransactionPhase == 0)
        {
            // Closing each gate precedes every asynchronous pause. No action is applied
            // until all four report stable, paused authority state.
            bool A = Basic.Request_TryPrepareAuthorityTransaction();
            bool B = MinMax.Request_TryPrepareAuthorityTransaction();
            bool C = Mods.Request_TryPrepareAuthorityTransaction();
            bool D = Clamp.Request_TryPrepareAuthorityTransaction();
            if (!A || !B || !C || !D || !Get_IsValidServerChildren() ||
                !Basic.Get_IsAuthorityTransactionPrepared() ||
                !MinMax.Get_IsAuthorityTransactionPrepared() ||
                !Mods.Get_IsAuthorityTransactionPrepared() ||
                !Clamp.Get_IsAuthorityTransactionPrepared()) { return; }
            _TransactionPhase = 1;
        }
        if (_TransactionPhase == 1)
        {
            int32 Action = _TransactionAction;
            int32 Target = Get_TargetStation(Action);
            int32 Value = CkAttributeInteger::Get_PresetValue(Action, _TransactionPreset);
            if (Action == 1)
            {
                bool All = true;
                for (auto Running : _ResumeAuto) { All = All && Running; }
                for (int32 i = 0; i < 4; i++) { _ResumeAuto[i] = !All; }
            }
            else if (Action >= 3 && Action <= 6)
            { if (Target < 0) { Request_Fail("Auto target invalid"); return; }
              _ResumeAuto[Target] = !_ResumeAuto[Target]; }
            else if (Action == 26)
            {
                if (!Basic.Request_BeginAuthorityReset() ||
                    !MinMax.Request_BeginAuthorityReset() ||
                    !Mods.Request_BeginAuthorityReset() ||
                    !Clamp.Request_BeginAuthorityReset())
                { Request_Fail("Integer reset admission rejected"); return; }
                for (int32 i = 0; i < 4; i++) { _ResumeAuto[i] = true; }
            }
            else if (Action == 24)
            {
                if (!Clamp.Request_BeginAuthorityReset())
                { Request_Fail("Clamping reset admission rejected"); return; }
                _ResumeAuto[3] = true;
            }
            else if (Action == 8 || Action == 9 || Action == 10)
            {
                if (!Basic.Request_ApplyAuthorityControl(Action, Value))
                { Request_Fail("Basic control rejected"); return; }
                _ResumeAuto[0] = false;
            }
            else if (Action == 12 || Action == 13 || Action == 14)
            {
                if (!MinMax.Request_ApplyAuthorityControl(Action, Value))
                { Request_Fail("MinMax control rejected"); return; }
                _ResumeAuto[1] = false;
            }
            else if (Action >= 16 && Action <= 20)
            {
                int32 ModifierAction = Action - 16;
                if (!Mods.Request_ApplyAuthorityModifierControl(ModifierAction, Value))
                { Request_Fail("Modifier control rejected"); return; }
                _ResumeAuto[2] = false;
            }
            else if (Action == 22 || Action == 23)
            {
                if (!Clamp.Request_ApplyAuthorityControl(Action, Value))
                { Request_Fail("Clamping control rejected"); return; }
                _ResumeAuto[3] = false;
            }
            else if (Action != 1 && !(Action >= 3 && Action <= 6))
            { Request_Fail("Integer action invalid"); return; }
            _TransactionPhase = 2;
        }
        if (_TransactionPhase == 2)
        {
            int32 Action = _TransactionAction;
            if (Action == 26 &&
                (!Basic.Get_HasSettledAuthorityReset() ||
                 !MinMax.Get_HasSettledAuthorityReset() ||
                 !Mods.Get_HasSettledAuthorityReset() ||
                 !Clamp.Get_HasSettledAuthorityReset())) { return; }
            if (Action == 24 && !Clamp.Get_HasSettledAuthorityReset()) { return; }
            if ((Action == 8 || Action == 9 || Action == 10) &&
                !Basic.Get_HasSettledAuthorityControl()) { return; }
            if ((Action == 12 || Action == 13 || Action == 14) &&
                !MinMax.Get_HasSettledAuthorityControl()) { return; }
            if (Action >= 16 && Action <= 20 &&
                !Mods.Get_HasSettledAuthorityModifierControl()) { return; }
            if ((Action == 22 || Action == 23) &&
                !Clamp.Get_HasSettledAuthorityControl()) { return; }
            _TransactionPhase = 3;
        }
        if (_TransactionPhase == 3)
        {
            // The Modifiers completed flag is consumed here, before a future
            // prepare call can clear it.
            if (!Basic.Request_BeginFinishAuthorityTransaction(_ResumeAuto[0]) ||
                !MinMax.Request_BeginFinishAuthorityTransaction(_ResumeAuto[1]) ||
                !Mods.Request_BeginFinishAuthorityTransaction(_ResumeAuto[2]) ||
                !Clamp.Request_BeginFinishAuthorityTransaction(_ResumeAuto[3]))
            { Request_Fail("Integer finish admission rejected"); return; }
            _TransactionPhase = 4; return;
        }
        if (_TransactionPhase != 4 ||
            !Basic.Get_HasFinishedAuthorityTransaction() ||
            !MinMax.Get_HasFinishedAuthorityTransaction() ||
            !Mods.Get_HasFinishedAuthorityTransaction() ||
            !Clamp.Get_HasFinishedAuthorityTransaction()) { return; }
        auto Candidate = FCkAttributeGym_IntegerSnapshot();
        if (!Request_TryBuildSnapshot(Candidate)) { return; }
        // Every station is still gated here. Validate all release prerequisites
        // before opening any station, then publish in this same call stack.
        if (!Basic.Get_CanReleaseAuthorityTransaction() ||
            !MinMax.Get_CanReleaseAuthorityTransaction() ||
            !Mods.Get_CanReleaseAuthorityTransaction() ||
            !Clamp.Get_CanReleaseAuthorityTransaction())
        { Request_Fail("Integer finish release preflight rejected"); return; }
        if (!Basic.Request_ReleaseAuthorityTransaction() ||
            !MinMax.Request_ReleaseAuthorityTransaction() ||
            !Mods.Request_ReleaseAuthorityTransaction() ||
            !Clamp.Request_ReleaseAuthorityTransaction())
        { Request_Fail("Integer finish release rejected"); return; }
        if (!Request_TryBuildSnapshot(Candidate))
        { Request_Fail("Integer finish snapshot changed during release"); return; }
        Snapshot = Candidate;
        auto Commit = FCkAttributeGym_IntegerControlCommit();
        Commit.Serial = _TransactionSerial; Commit.Action = _TransactionAction;
        Commit.Preset = _TransactionPreset; Commit.Snapshot = Candidate;
        LastControlCommit = Commit;
        _LastPublishSeconds = float(System::GetGameTimeInSeconds());
        ForceNetUpdate();
        _TransactionPending = false; _TransactionPhase = 0;
        if (ck::IsValid(_TransactionOwner) && _TransactionOwner.GetWorld() == GetWorld())
        { _TransactionOwner.Request_CompleteIntegerControl(_TransactionSerial,
            _TransactionAction, _TransactionPreset, _TransactionGeneration, true); }
        _TransactionOwner = nullptr; _ResumeAuto.Empty();
        _TransactionAction = -1; _TransactionSerial = 0;
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
        { Request_FailLocal("Projection quartet dependency lost"); return false; }
        return true;
    }

    private bool Get_IsValidLocalProjection()
    {
        if (_LocalChildren.Num() != 4) { return false; }
        auto World = ck::TransientEntity();
        if (!CkAttributeInteger::IsLiveEntity(World)) { return false; }
        for (int32 i = 0; i < 4; i++)
        {
            auto Station = CkAttributeInteger::FindStation(i);
            auto Child = _LocalChildren[i];
            if (!CkAttributeInteger::IsLiveEntity(Station) ||
                !CkAttributeInteger::IsLiveEntity(Child) ||
                utils_entity_lifetime::Get_LifetimeOwner(Child) != Station)
            { return false; }
            auto Matches = utils_entity_tag::ForEach_Entity(World,
                n"TAG_AttributeGym_IntegerProjection");
            if (Matches.Num() != 4) { return false; }
            int32 Count = 0;
            for (auto Match : Matches) { if (Match == Child) { Count++; } }
            if (Count != 1) { return false; }
            auto Script = Cast<UCk_EntityScript_AttributeGym_IntegerProjection>(
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
            for (int32 i = 0; i < 4; i++)
            { if (utils_entity_tag::ForEach_Entity(ck::TransientEntity(),
                  CkAttributeInteger::Get_ChildTag(i)).Num() != 0)
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
            bool AllConstructed = _LocalChildren.Num() == 4;
            for (int32 i = 0; i < _LocalChildren.Num(); i++)
            { AllConstructed = AllConstructed && CkAttributeInteger::IsLiveEntity(_LocalChildren[i]); }
            if (AllConstructed && Get_IsValidLocalProjection())
            { _LocalStarting = false; LocalProjectionReady = true; }
            return;
        }
        for (int32 i = 0; i < 4; i++)
        { if (!CkAttributeInteger::IsLiveEntity(CkAttributeInteger::FindStation(i))) { return; } }
        if (utils_entity_tag::ForEach_Entity(ck::TransientEntity(),
            n"TAG_AttributeGym_IntegerProjection").Num() != 0)
        { Request_FailLocal("Projection identity already exists"); return; }
        _LocalChildren.Empty(); _PendingLocal.Empty();
        for (int32 i = 0; i < 4; i++)
        { _LocalChildren.Add(FCk_Handle()); _PendingLocal.Add(FCk_Handle_PendingEntityScript()); }
        _LocalStarting = true;
        for (int32 i = 0; i < 4; i++)
        {
            auto Station = CkAttributeInteger::FindStation(i);
            auto Params = FIntegerProjectionSpawnParams();
            Params.Generation = LocalProjectionGeneration;
            Params.StationIndex = i;
            Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
                Station.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
            auto Owner = Station;
            _PendingLocal[i] = utils_entity_script::Request_SpawnEntity(Owner,
                UCk_EntityScript_AttributeGym_IntegerProjection, FInstancedStruct::Make(Params));
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
            InGeneration == LocalProjectionGeneration && InIndex >= 0 && InIndex < 4 &&
            InIndex < _LocalChildren.Num() && InChild == _LocalChildren[InIndex])
        { Request_FailLocal(InReason); }
    }

    private void Request_FailLocal(FString InReason)
    {
        Request_RetireLocal();
        LocalProjectionFailed = true;
        LocalProjectionFailureReason = InReason;
        ck::Error("[AttributeInteger] " + InReason);
    }

    private void Request_RetireLocal()
    {
        LocalProjectionReady = false;
        _LocalStarting = false;
        for (int32 i = 0; i < _PendingLocal.Num(); i++)
        {
            if (utils_pending_entity_script::Get_IsValid(_PendingLocal[i]))
            { utils_pending_entity_script::Request_DestroyEntity(_PendingLocal[i]); }
            else if (i < _LocalChildren.Num() && CkAttributeInteger::IsLiveEntity(_LocalChildren[i]))
            { utils_entity_lifetime::Request_DestroyEntity(_LocalChildren[i]); }
            _PendingLocal[i] = FCk_Handle_PendingEntityScript();
            if (i < _LocalChildren.Num()) { _LocalChildren[i] = FCk_Handle(); }
        }
    }
    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (HasAuthority() && _AdmissionTicket != 0)
        {
            _Starting = false;
            _TransactionPending = false;
            Snapshot.Ready = false;
            Snapshot.Failed = true;
            Request_CloseWorldOnFailure();
            return;
        }
        _Starting = false; _TransactionPending = false; Snapshot.Ready = false;
        Request_RetireLocal();
        if (HasAuthority())
        {
            Request_RetireServerChildren();
            for (int32 i = 0; i < _PendingStations.Num(); i++)
            {
                if (utils_pending_entity_script::Get_IsValid(_PendingStations[i]))
                { utils_pending_entity_script::Request_DestroyEntity(_PendingStations[i]); }
                _PendingStations[i] = FCk_Handle_PendingEntityScript();
            }
            Request_ReleaseAdmission();
            if (CkAttributeInteger::IsLiveEntity(_WorldStationOwner))
            { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); }
            _WorldStationOwner = FCk_Handle();
        }
    }
}

class UCk_EntityScript_AttributeGym_IntegerProjection : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;
    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    UPROPERTY(ExposeOnSpawn) int32 StationIndex = -1;
    private FCk_Handle_Timer _DisplayTimer;
    private bool _Constructed = false;
    private int32 _LastMinSerial = 0;
    private int32 _LastMaxSerial = 0;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        _Constructed = false;
        _DisplayTimer = FCk_Handle_Timer();
        _LastMinSerial = 0; _LastMaxSerial = 0;
        auto State = CkAttributeInteger::Get_State();
        auto World = ck::IsValid(State) ? State.GetWorld() : nullptr;
        if (!CkAttributeInteger::IsLiveEntity(InHandle) || ck::Is_NOT_Valid(World) ||
            World.GetNetMode() == ENetMode::NM_DedicatedServer ||
            StationIndex < 0 || StationIndex >= 4 ||
            utils_entity_lifetime::Get_LifetimeOwner(InHandle) !=
                CkAttributeInteger::FindStation(StationIndex) ||
            ck::Is_NOT_Valid(State) ||
            !State.Get_IsExpectedLocalChild(InHandle, Generation, StationIndex))
        {
            if (ck::IsValid(State)) { State.Request_FailExpectedLocalChild(
                InHandle, Generation, StationIndex, "Projection role or owner invalid"); }
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_IntegerProjection");
        auto TimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
        TimerParams.Set_StartingState(ECk_Timer_State::Running)
            .Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        _DisplayTimer = utils_timer::Add(InHandle, TimerParams);
        if (!CkAttributeInteger::IsLiveEntity(FCk_Handle(_DisplayTimer)))
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
        auto Children = utils_entity_lifetime::Get_LifetimeDependents(Self);
        return _Constructed && CkAttributeInteger::IsLiveEntity(Self) &&
            Children.Num() == 1 && Children[0] == FCk_Handle(_DisplayTimer) &&
            utils_transform::Has(Self) &&
            CkAttributeInteger::IsLiveEntity(FCk_Handle(_DisplayTimer)) &&
            utils_timer::DoCast(FCk_Handle(_DisplayTimer)).IsSet() &&
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_DisplayTimer)) == Self &&
            _DisplayTimer.Get_CurrentState() == ECk_Timer_State::Running;
    }

    UFUNCTION() private void DisplayTick(FCk_Handle_Timer InHandle,
        FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto State = CkAttributeInteger::Get_State();
        if (InHandle != _DisplayTimer || ck::Is_NOT_Valid(State) ||
            !State.Get_HasAdmittedLocalProjection(Generation)) { return; }
        if (!Get_HasValidLocalComposition())
        { State.Request_FailCurrentLocalChild(ck::ToEntity(this), Generation,
            StationIndex, "Projection display dependency lost"); return; }
        auto Sample = State.Snapshot.Stations[StationIndex];
        if (!CkAttributeInteger::IsValidSample(Sample))
        { State.Request_FailCurrentLocalChild(ck::ToEntity(this), Generation,
            StationIndex, "Projection sample malformed"); return; }
        if (StationIndex == 3)
        {
            if (Sample.BoundaryMinEventSerial < _LastMinSerial ||
                Sample.BoundaryMaxEventSerial < _LastMaxSerial)
            { State.Request_FailCurrentLocalChild(ck::ToEntity(this), Generation,
                StationIndex, "Projection clamp sequence regressed"); return; }
            if (Sample.BoundaryMinEventSerial > _LastMinSerial)
            { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this),
                FVector(-50.0f, 0.0f, 150.0f), FLinearColor(0.0f, 0.0f, 1.0f, 1.0f)); }
            if (Sample.BoundaryMaxEventSerial > _LastMaxSerial)
            { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this),
                FVector(50.0f, 0.0f, 150.0f), FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)); }
            _LastMinSerial = Sample.BoundaryMinEventSerial;
            _LastMaxSerial = Sample.BoundaryMaxEventSerial;
        }
        FString BaseTitle = "INTEGER BASIC";
        if (StationIndex == 1) { BaseTitle = "INTEGER MIN/MAX/CURRENT"; }
        if (StationIndex == 2) { BaseTitle = "INTEGER MODIFIERS"; }
        if (StationIndex == 3) { BaseTitle = "INTEGER CLAMPING"; }
        auto TitleText = BaseTitle + " (" +
            CkGym_Common::Get_NetworkRoleTitle(ck::ToEntity(this)) + ")";
        CkGym_Common::Update_StationDisplay(ck::ToEntity(this), TitleText, Sample.Body, "");
    }
}
