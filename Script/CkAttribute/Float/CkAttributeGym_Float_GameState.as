// FLOAT ATTRIBUTE GYM - SHARED SIMULATION AND LOCAL PRESENTATION

USTRUCT()
struct FCkAttributeGym_FloatComponentSample
{
    UPROPERTY() bool Present = false;
    UPROPERTY() float32 Base = 0.0f;
    UPROPERTY() float32 Bonus = 0.0f;
    UPROPERTY() float32 Final = 0.0f;
}
USTRUCT()
struct FCkAttributeGym_FloatAttributeSample
{
    UPROPERTY() FName Name;
    UPROPERTY() FCkAttributeGym_FloatComponentSample Min;
    UPROPERTY() FCkAttributeGym_FloatComponentSample Max;
    UPROPERTY() FCkAttributeGym_FloatComponentSample Current;
    UPROPERTY() float32 PreClampFinal = 0.0f;
    UPROPERTY() float32 ClampOverflow = 0.0f;
}
USTRUCT()
struct FCkAttributeGym_FloatStationSample
{
    UPROPERTY() int32 StationIndex = -1;
    UPROPERTY() FName StationTag;
    UPROPERTY() FName ChildTag;
    UPROPERTY() bool AutoRunning = false;
    UPROPERTY() bool AutoEnabled = true;
    UPROPERTY() int32 AutoStep = 0;
    UPROPERTY() TArray<FCkAttributeGym_FloatAttributeSample> Attributes;
    UPROPERTY() int32 ValueChangeCount = 0;
    UPROPERTY() int32 ModifierCount = 0;
    UPROPERTY() int32 MinChangeCount = 0;
    UPROPERTY() int32 MaxChangeCount = 0;
    UPROPERTY() int32 CurrentChangeCount = 0;
    UPROPERTY() int32 ClampedCount = 0;
    UPROPERTY() float32 LastPreClampValue = 0.0f;
    UPROPERTY() float32 LastClampedValue = 0.0f;
    UPROPERTY() float32 LastOverflow = 0.0f;
    UPROPERTY() int32 Delegate1Count = 0;
    UPROPERTY() int32 Delegate2Count = 0;
    UPROPERTY() int32 Delegate3Count = 0;
    UPROPERTY() bool Delegate1Bound = false;
    UPROPERTY() bool Delegate2Bound = false;
    UPROPERTY() bool Delegate3Bound = false;
    UPROPERTY() int32 MinClampCount = 0;
    UPROPERTY() int32 MaxClampCount = 0;
    UPROPERTY() float32 LastPreviousFinal = 0.0f;
    UPROPERTY() float32 LastNewFinal = 0.0f;
    UPROPERTY() bool EnergyRefillRunning = false;
    UPROPERTY() bool ManaRefillRunning = false;
    UPROPERTY() bool DesiredEnergyRefillRunning = true;
    UPROPERTY() bool DesiredManaRefillRunning = true;
    UPROPERTY() float32 EnergyFillRate = 0.0f;
    UPROPERTY() float32 ManaFillRate = 0.0f;
    UPROPERTY() int32 TotalAttributes = 0;
    UPROPERTY() int32 CombatAttributes = 0;
    UPROPERTY() FString Title;
    UPROPERTY() FString Body;
    UPROPERTY() TArray<FCkAttributeGym_FloatClampEvent> ClampEvents;
    UPROPERTY() int32 BoundaryEventSerial = 0;
}
USTRUCT()
struct FCkAttributeGym_FloatSnapshot
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() TArray<FCkAttributeGym_FloatStationSample> Stations;
}
USTRUCT()
struct FCkAttributeGym_FloatControlCommit
{
    UPROPERTY() int32 Serial = 0;
    UPROPERTY() int32 Action = -1;
    UPROPERTY() int32 Preset = -1;
    UPROPERTY() FCkAttributeGym_FloatSnapshot Snapshot;
}
USTRUCT()
struct FFloatStationSpawnParams
{
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() bool WaitForWorldReady = true;
}
USTRUCT()
struct FFloatProjectionSpawnParams
{
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 StationIndex = -1;
}

namespace CkAttributeFloat
{
    bool IsFinite(float32 V) { return V == V && Math::Abs(V) < 1.0e20f; }
    bool Near(float32 A, float32 B) { return IsFinite(A) && IsFinite(B) && Math::Abs(A-B) <= 0.001f; }
    bool IsValidComponent(FCkAttributeGym_FloatComponentSample S)
    {
        return IsFinite(S.Base) && IsFinite(S.Bonus) && IsFinite(S.Final) &&
            (S.Present || (S.Base == 0.0f && S.Bonus == 0.0f && S.Final == 0.0f));
    }
    bool IsValidAttribute(FCkAttributeGym_FloatAttributeSample S, FName Name, bool Bounds = true)
    {
        if (S.Name != Name || !S.Current.Present || S.Min.Present != Bounds || S.Max.Present != Bounds ||
            !IsValidComponent(S.Min) || !IsValidComponent(S.Max) || !IsValidComponent(S.Current) ||
            !Near(S.PreClampFinal-S.Current.Final, S.ClampOverflow)) { return false; }
        if (Bounds && (!Near(S.Min.Base+S.Min.Bonus,S.Min.Final) ||
            !Near(S.Max.Base+S.Max.Bonus,S.Max.Final) || S.Min.Final > S.Max.Final)) { return false; }
        return Near(S.Current.Final, Bounds ? Math::Clamp(S.PreClampFinal,S.Min.Final,S.Max.Final) : S.PreClampFinal);
    }
    bool IsValidSample(FCkAttributeGym_FloatStationSample S)
    {
        if (S.StationIndex < 0 || S.StationIndex >= 8 || S.StationTag != Get_StationTag(S.StationIndex) ||
            S.ChildTag != Get_ChildTag(S.StationIndex) || S.AutoStep < 0 || S.ValueChangeCount < 0 ||
            S.ModifierCount < 0 || S.MinChangeCount < 0 || S.MaxChangeCount < 0 || S.CurrentChangeCount < 0 ||
            S.ClampedCount < 0 || S.Delegate1Count < 0 || S.Delegate2Count < 0 || S.Delegate3Count < 0 ||
            S.MinClampCount < 0 || S.MaxClampCount < 0 || !IsFinite(S.LastPreClampValue) ||
            !IsFinite(S.LastClampedValue) || !IsFinite(S.LastOverflow) || !IsFinite(S.LastPreviousFinal) ||
            !IsFinite(S.LastNewFinal) || !IsFinite(S.EnergyFillRate) || !IsFinite(S.ManaFillRate)) { return false; }
        if (S.StationIndex == 0)
        {
            if (S.Attributes.Num() != 1) { return false; }
            if (!IsValidAttribute(S.Attributes[0],n"FloatAttribute.Test")) { return false; }
        }
        if (S.StationIndex == 1)
        {
            if (S.Attributes.Num() != 4) { return false; }
            if (!IsValidAttribute(S.Attributes[0],n"FloatAttribute.Armor")) { return false; }
            if (!IsValidAttribute(S.Attributes[1],n"FloatAttribute.Stamina")) { return false; }
            if (!IsValidAttribute(S.Attributes[2],n"FloatAttribute.Health")) { return false; }
            if (!IsValidAttribute(S.Attributes[3],n"FloatAttribute.Defense")) { return false; }
        }
        if (S.StationIndex == 2)
        {
            if (S.Attributes.Num() != 2) { return false; }
            if (!IsValidAttribute(S.Attributes[0],n"FloatAttribute.Damage")) { return false; }
            if (!IsValidAttribute(S.Attributes[1],n"FloatAttribute.Defense")) { return false; }
        }
        if (S.StationIndex == 3)
        {
            if (S.Attributes.Num() != 1) { return false; }
            if (!IsValidAttribute(S.Attributes[0],n"FloatAttribute.PowerLevel")) { return false; }
        }
        if (S.StationIndex == 4)
        {
            if (S.Attributes.Num() != 1) { return false; }
            if (!IsValidAttribute(S.Attributes[0],n"FloatAttribute.Signal")) { return false; }
        }
        if (S.StationIndex == 5)
        {
            if (S.Attributes.Num() != 4) { return false; }
            if (!IsValidAttribute(S.Attributes[0],n"FloatAttribute.Energy")) { return false; }
            if (!IsValidAttribute(S.Attributes[1],n"FloatAttribute.Mana")) { return false; }
            if (!IsValidAttribute(S.Attributes[2],n"FloatAttribute.Energy.Refill",false) ||
                !IsValidAttribute(S.Attributes[3],n"FloatAttribute.Mana.Refill",false) ||
                !Near(S.EnergyFillRate,5.0f) || !Near(S.ManaFillRate,3.0f)) { return false; }
        }
        if (S.StationIndex == 6)
        {
            if (S.Attributes.Num() != 1) { return false; }
            if (!IsValidAttribute(S.Attributes[0],n"FloatAttribute.Counter")) { return false; }
        }
        if (S.StationIndex == 7)
        {
            if ((S.CombatAttributes != 0 && S.CombatAttributes != 2) || S.TotalAttributes != 3+S.CombatAttributes ||
                S.Attributes.Num() != S.TotalAttributes) { return false; }
            if (!IsValidAttribute(S.Attributes[0],n"RPG.Strength") || !IsValidAttribute(S.Attributes[1],n"RPG.Agility") ||
                !IsValidAttribute(S.Attributes[2],n"RPG.Intelligence")) { return false; }
            if (S.CombatAttributes == 2 && (!IsValidAttribute(S.Attributes[3],n"Combat.Attack") ||
                !IsValidAttribute(S.Attributes[4],n"Combat.Defense"))) { return false; }
        }
        if (S.StationIndex == 1)
        {
            if (S.ClampEvents.Num() != 4 || S.BoundaryEventSerial < 0) { return false; }
            for (int32 i = 0; i < 4; i++)
            {
                auto E = S.ClampEvents[i];
                if (E.AttributeIndex != i || E.Serial < 0 || !IsFinite(E.PreClampFinal) ||
                    !IsFinite(E.Final) || !IsFinite(E.Overflow) ||
                    !Near(E.PreClampFinal-E.Final,E.Overflow)) { return false; }
                if (E.Serial == 0 && (E.PreClampFinal != 0.0f || E.Final != 0.0f || E.Overflow != 0.0f)) { return false; }
            }
        }
        else if (S.ClampEvents.Num() != 0 || S.BoundaryEventSerial != 0) { return false; }
        return true;
    }
    bool IsValidSnapshot(FCkAttributeGym_FloatSnapshot S)
    {
        if (!S.Ready || S.Failed || S.Generation <= 0 || S.Sequence <= 0 || S.Stations.Num() != 8) { return false; }
        for (int32 i=0;i<8;i++) { if (S.Stations[i].StationIndex != i || !IsValidSample(S.Stations[i])) { return false; } }
        return true;
    }
}
class ACk_FloatAttributeGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkAttributeGym_FloatSnapshot Snapshot;
    UPROPERTY(Replicated) FCkAttributeGym_FloatControlCommit LastControlCommit;
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
    private bool _WaitingForCatalog = false;
    private float _CatalogWaitSeconds = 0.0;
    private int32 _NextGeneration = 0;
    private float _StartSeconds = 0.0;
    private float _LastPublishSeconds = -1.0;
    private TArray<FCk_Handle> _Stations;
    private TArray<FCk_Handle> _NewStations;
    private TArray<FCk_Handle_PendingEntityScript> _PendingStations;
    private TArray<FCk_Handle_PendingEntityScript> _PendingChildren;
    private bool _StartIssued = false;
    private bool _StartReleased = false;
    private int32 _StartupDiagnosticSlot = 0;
    private int32 _FirstSnapshotFailureSlot = -1;
    private int32 _FirstSnapshotFailureAttribute = -1;
    private FString _FirstSnapshotFailureStage = "not-attempted";

    private bool _TransactionPending = false;
    private int32 _TransactionPhase = 0;
    private int32 _PendingAutoAction = -1;
    private int32 _TransactionAction = -1;
    private int32 _TransactionSerial = 0;
    private int32 _TransactionGeneration = 0;
    private int32 _TransactionPreset = -1;
    private float _TransactionSeconds = 0.0;
    private TArray<bool> _ResumeAuto;
    private ACk_FloatAttributeGym_PlayerController _TransactionOwner;

    private TArray<FCk_Handle> _LocalChildren;
    private TArray<FCk_Handle_PendingEntityScript> _PendingLocal;
    private bool _LocalStarting = false;
    private bool _LocalMonitorArmed = false;
    private float _LocalWaitSeconds = 0.0;
    private float _LocalStartSeconds = -1.0;

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || _Starting || Snapshot.Ready || Snapshot.Failed) { return; }
        if (!Get_HasPublishedCatalog())
        {
            if (!_WaitingForCatalog)
            {
                _WaitingForCatalog = true;
                _CatalogWaitSeconds = float(System::GetGameTimeInSeconds());
                System::SetTimer(this,n"OnCatalogAdmission",0.1,false);
            }
            return;
        }
        _WaitingForCatalog = false;
        if (_NextGeneration == 2147483647) { Request_Fail("Float generation exhausted"); return; }
        _Starting = true;
        _StartSeconds = float(System::GetGameTimeInSeconds());
        _NextGeneration++;
        Snapshot = FCkAttributeGym_FloatSnapshot();
        LastControlCommit = FCkAttributeGym_FloatControlCommit();
        Snapshot.Generation = _NextGeneration;
        _Stations.Empty(); _NewStations.Empty(); _PendingStations.Empty();
        _PendingChildren.Empty(); ServerChildren.Empty();
        _StationsReady = false; _StartIssued = false; _StartReleased = false;
        _StartupDiagnosticSlot = 0;
        for (int32 i = 0; i < 8; i++)
        {
            _Stations.Add(FCk_Handle()); _NewStations.Add(FCk_Handle());
            _PendingStations.Add(FCk_Handle_PendingEntityScript());
            _PendingChildren.Add(FCk_Handle_PendingEntityScript());
            ServerChildren.Add(FCk_Handle());
        }
        ServerChildCount = 0;
        ForceNetUpdate();
        System::SetTimer(this, n"OnStartupDeadline", 20.0, false);
        System::SetTimer(this, n"OnStartupDiagnostic", 5.0, false);
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
        { ck::Error("[AttributeFloat] " + InReason); return; }
        if (ck::IsValid(_TransactionOwner) &&
            _TransactionOwner.GetWorld() == GetWorld() &&
            _TransactionGeneration == Snapshot.Generation)
        { _TransactionOwner.Request_CompleteFloatControl(_TransactionSerial,
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
        if (CkAttributeFloat::IsLiveEntity(_WorldStationOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); }
        _WorldStationOwner = FCk_Handle();
        ck::Error("[AttributeFloat] " + InReason);
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
            else if (i < ServerChildren.Num() && CkAttributeFloat::IsLiveEntity(ServerChildren[i]))
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
        if (CkAttributeFloat::IsLiveEntity(_AdmissionOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_AdmissionOwner); }
        _AdmissionOwner = FCk_Handle();
        return true;
    }

    private void Request_EnsureWorldStations()
    {
        if (!_Starting || _StationsReady || _AdmissionOwned) { return; }
        auto World = ck::TransientEntity();
        if (!CkAttributeFloat::IsLiveEntity(World) ||
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
        if (!CkAttributeFloat::IsLiveEntity(_AdmissionOwner))
        { Request_Fail("Admission allocation failed"); return; }
        if (!CkGym_Admission::TryCommitOwner(World, _AdmissionTicket, _AdmissionOwner))
        { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
          Request_Fail("Station admission owner commit rejected"); return; }
        _AdmissionOwned = true;
        auto Specs = CkAttributeFloat::Get_Stations();
        if (Specs.Num() != 8) { Request_Fail("Station declaration invalid"); return; }
        for (int32 i = 0; i < 8; i++)
        {
            if (Specs[i].Tags.Num() != 1 || Specs[i].Tags[0] != CkAttributeFloat::Get_StationTag(i))
            { Request_Fail("Station tag declaration invalid"); return; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, Specs[i].Tags[0]);
            if (Matches.Num() > 1 || (Matches.Num() == 1 &&
                (!CkAttributeFloat::IsLiveEntity(Matches[0]) ||
                 !Matches[0].Has_Fragment(FCkGym_Station_Anchors))))
            { Request_Fail("Station identity ambiguous"); return; }
            if (Matches.Num() == 1) { _Stations[i] = Matches[0]; }
        }
        bool Missing = false;
        for (int32 i = 0; i < 8; i++)
        { if (!CkAttributeFloat::IsLiveEntity(_Stations[i])) { Missing = true; } }
        if (Missing)
        {
            _WorldStationOwner = utils_entity_lifetime::Request_CreateEntity(World);
            if (!CkAttributeFloat::IsLiveEntity(_WorldStationOwner))
            { Request_Fail("World station group allocation failed"); return; }
            for (int32 i = 0; i < 8; i++)
            {
                if (CkAttributeFloat::IsLiveEntity(_Stations[i])) { continue; }
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
            if (!CkAttributeFloat::IsLiveEntity(Child) ||
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
        if (!CkAttributeFloat::IsLiveEntity(World))
        { Request_Fail("World lost during station settle"); return; }
        for (int32 i = 0; i < 8; i++)
        {
            if (!CkAttributeFloat::IsLiveEntity(_Stations[i]))
            { System::SetTimer(this, n"OnStationSettle", 0.05, false); return; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, CkAttributeFloat::Get_StationTag(i));
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
        for (int32 i = 0; i < 8; i++)
        {
            if (!CkAttributeFloat::IsLiveEntity(_Stations[i]) ||
                !_Stations[i].Has_Fragment(FCkGym_Station_Anchors) ||
                utils_entity_tag::ForEach_Entity(World, CkAttributeFloat::Get_ChildTag(i)).Num() != 0)
            { Request_Fail("Simulation child preflight failed"); return; }
        }
        for (int32 i = 0; i < 8; i++)
        {
            auto Params = FFloatStationSpawnParams();
            Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
                _Stations[i].Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
            Params.WaitForWorldReady = true;
            auto Owner = _Stations[i];
            if (i == 0) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_FloatValues, FInstancedStruct::Make(Params)); }
            if (i == 1) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_FloatClamping, FInstancedStruct::Make(Params)); }
            if (i == 2) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_FloatModifiers, FInstancedStruct::Make(Params)); }
            if (i == 3) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_FloatMinMaxCurrent, FInstancedStruct::Make(Params)); }
            if (i == 4) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_FloatSignals, FInstancedStruct::Make(Params)); }
            if (i == 5) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_FloatRefill, FInstancedStruct::Make(Params)); }
            if (i == 6) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_FloatIncrementDecrement, FInstancedStruct::Make(Params)); }
            if (i == 7) { _PendingChildren[i] = utils_entity_script::Request_SpawnEntity(
                Owner, UCk_EntityScript_AttributeGym_FloatMultiple, FInstancedStruct::Make(Params)); }
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

    // Temporary log-only startup census. It never starts stations or builds a snapshot.
    UFUNCTION() private void OnStartupDiagnostic()
    {
        if (!HasAuthority() || !_Starting || Snapshot.Ready || Snapshot.Failed) { return; }
        _StartupDiagnosticSlot++;
        Request_LogStartupDiagnostic(_StartupDiagnosticSlot * 5);
        if (_StartupDiagnosticSlot < 3)
        { System::SetTimer(this, n"OnStartupDiagnostic", 5.0, false); }
    }

    private void Request_LogStartupDiagnostic(int32 InSecond)
    {
        if (!HasAuthority() || !_Starting || Snapshot.Ready || Snapshot.Failed) { return; }
        auto Elapsed = float(System::GetGameTimeInSeconds()) - _StartSeconds;
        FString FirstBlocked = "none";
        if (!_StationsReady) { FirstBlocked = "station-admission"; }
        else if (!_StartIssued) { FirstBlocked = "composition-before-start"; }
        else if (!_StartReleased) { FirstBlocked = "start-settlement"; }
        else { FirstBlocked = "coherent-sample"; }
        System::PrintString(
            f"[AttributeFloatStartup] t={InSecond} elapsed={Elapsed} gen={Snapshot.Generation} " +
            f"stationReady={_StationsReady} startIssued={_StartIssued} startReleased={_StartReleased} " +
            f"serverCount={ServerChildCount} stations={_Stations.Num()} children={ServerChildren.Num()} " +
            f"pending={_PendingChildren.Num()} firstGate={FirstBlocked} " +
            f"snapshotSlot={_FirstSnapshotFailureSlot} snapshotAttribute={_FirstSnapshotFailureAttribute} " +
            f"snapshotStage={_FirstSnapshotFailureStage}",
            false, true, FLinearColor::White, 0.0f, NAME_None);
        for (int32 i = 0; i < 8; i++)
        {
            bool StationLive = i < _Stations.Num() && CkAttributeFloat::IsLiveEntity(_Stations[i]);
            bool ChildLive = i < ServerChildren.Num() && CkAttributeFloat::IsLiveEntity(ServerChildren[i]);
            bool Pending = i < _PendingChildren.Num() &&
                utils_pending_entity_script::Get_IsValid(_PendingChildren[i]);
            bool OwnerMatch = false;
            bool StationTagged = false;
            bool ChildTagged = false;
            bool ScriptMatch = false;
            int32 DirectLiveCount = -1;
            bool Composed = false;
            bool Dependencies = false;
            bool Failed = false;
            bool Started = false;
            bool SampleSettled = false;
            FString StationId = StationLive ? _Stations[i].ToString() : "none";
            FString ChildId = ChildLive ? ServerChildren[i].ToString() : "none";
            if (StationLive)
            { StationTagged = CkAttributeFloat::FindStation(i) == _Stations[i]; }
            if (ChildLive)
            {
                auto Child = ServerChildren[i];
                DirectLiveCount = 0;
                auto Direct = utils_entity_lifetime::Get_LifetimeDependents(Child);
                for (int32 j = 0; j < Direct.Num(); j++)
                { if (CkAttributeFloat::IsLiveEntity(Direct[j])) { DirectLiveCount++; } }
                if (StationLive)
                {
                    auto Listed = utils_entity_lifetime::Get_LifetimeDependents(_Stations[i]);
                    for (int32 k = 0; k < Listed.Num(); k++)
                    { if (Listed[k] == Child) { OwnerMatch = true; break; } }
                    // Membership is a safe read even when a partially created child lacks its owner fragment.
                    auto World = ck::TransientEntity();
                    if (CkAttributeFloat::IsLiveEntity(World))
                    {
                        auto Matches = utils_entity_tag::ForEach_Entity(World,
                            CkAttributeFloat::Get_ChildTag(i));
                        ChildTagged = Matches.Num() == 1 && Matches[0] == Child;
                    }
                    if (OwnerMatch && StationTagged && ChildTagged)
                    {
                        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Child);
                        if (i == 0) { ScriptMatch = !ck::Is_NOT_Valid(Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base)); }
                        if (i == 1) { ScriptMatch = !ck::Is_NOT_Valid(Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base)); }
                        if (i == 2) { ScriptMatch = !ck::Is_NOT_Valid(Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base)); }
                        if (i == 3) { ScriptMatch = !ck::Is_NOT_Valid(Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base)); }
                        if (i == 4) { ScriptMatch = !ck::Is_NOT_Valid(Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base)); }
                        if (i == 5) { ScriptMatch = !ck::Is_NOT_Valid(Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base)); }
                        if (i == 6) { ScriptMatch = !ck::Is_NOT_Valid(Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base)); }
                        if (i == 7) { ScriptMatch = !ck::Is_NOT_Valid(Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base)); }
                        if (ScriptMatch)
                        {
                            Failed = StationFailure(i);
                            Composed = StationComposition(i);
                            Dependencies = StationDependencies(i);
                            if (Composed)
                            {
                                if (_StartIssued) { Started = StationStarted(i); }
                                SampleSettled = StationSampleSettled(i);
                            }
                        }
                    }
                }
            }
            FString Gate = "root";
            if (StationLive && ChildLive && OwnerMatch && StationTagged &&
                ChildTagged && ScriptMatch)
            {
                Gate = Failed ? "failure" : !Composed ? "composition" :
                    !Dependencies ? "dependencies" :
                    (_StartIssued && !Started) ? "start" :
                    !SampleSettled ? "sample" : "ready";
            }
            System::PrintString(
                f"[AttributeFloatStartup] slot={i} station={StationId} child={ChildId} " +
                f"stationLive={StationLive} childLive={ChildLive} pending={Pending} " +
                f"ownerMatch={OwnerMatch} stationTagged={StationTagged} childTagged={ChildTagged} classMatch={ScriptMatch} " +
                f"directLive={DirectLiveCount} composed={Composed} dependencies={Dependencies} " +
                f"failed={Failed} started={Started} sample={SampleSettled} gate={Gate}",
                false, true, FLinearColor::White, 0.0f, NAME_None);
        }
    }

    UFUNCTION() private void OnStartupDeadline()
    {
        if (!_Starting || Snapshot.Ready || Snapshot.Failed) { return; }
        auto Elapsed = float(System::GetGameTimeInSeconds()) - _StartSeconds;
        if (Elapsed < 20.0)
        { System::SetTimer(this, n"OnStartupDeadline", 20.0 - Elapsed, false); return; }
        Request_LogStartupDiagnostic(20);
        Request_Fail("World octet readiness timed out");
    }

    bool Get_IsExpectedServerChild(FCk_Handle InChild, int32 InGeneration, int32 InIndex)
    {
        return HasAuthority() && _Starting && !Snapshot.Failed &&
            InGeneration == Snapshot.Generation && InIndex >= 0 && InIndex < 8 &&
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

    UFUNCTION() private void OnCatalogAdmission()
    {
        if (!_WaitingForCatalog || !HasAuthority() || Snapshot.Failed || Snapshot.Ready) { return; }
        if (Get_HasPublishedCatalog()) { Request_EnsureSharedDemo(); return; }
        if (float(System::GetGameTimeInSeconds())-_CatalogWaitSeconds >= 20.0)
        { _WaitingForCatalog = false; Request_Fail("Float catalog admission timed out"); return; }
        System::SetTimer(this,n"OnCatalogAdmission",0.1,false);
    }
    private bool StationComposition(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasValidAuthorityComposition();
        }
        return false;
    }
    private bool StationDependencies(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasLiveAuthorityDependencies();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasLiveAuthorityDependencies();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasLiveAuthorityDependencies();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasLiveAuthorityDependencies();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasLiveAuthorityDependencies();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasLiveAuthorityDependencies();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasLiveAuthorityDependencies();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasLiveAuthorityDependencies();
        }
        return false;
    }
    private bool StationFailure(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return true; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return true; }
            return S.Get_HasAuthorityFailure();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return true; }
            return S.Get_HasAuthorityFailure();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return true; }
            return S.Get_HasAuthorityFailure();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return true; }
            return S.Get_HasAuthorityFailure();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return true; }
            return S.Get_HasAuthorityFailure();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return true; }
            return S.Get_HasAuthorityFailure();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return true; }
            return S.Get_HasAuthorityFailure();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return true; }
            return S.Get_HasAuthorityFailure();
        }
        return true;
    }
    private bool StationSampleSettled(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthoritySample();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthoritySample();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthoritySample();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthoritySample();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthoritySample();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthoritySample();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthoritySample();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthoritySample();
        }
        return false;
    }
    private bool StationStart(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_StartAuthorityDemo();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_StartAuthorityDemo();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_StartAuthorityDemo();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_StartAuthorityDemo();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_StartAuthorityDemo();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_StartAuthorityDemo();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_StartAuthorityDemo();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_StartAuthorityDemo();
        }
        return false;
    }
    private bool StationStarted(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityStart();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityStart();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityStart();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityStart();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityStart();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityStart();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityStart();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityStart();
        }
        return false;
    }
    private bool StationReleaseStart(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityStart();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityStart();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityStart();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityStart();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityStart();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityStart();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityStart();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityStart();
        }
        return false;
    }
    private bool StationPrepare(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_TryPrepareAuthorityTransaction();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_TryPrepareAuthorityTransaction();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_TryPrepareAuthorityTransaction();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_TryPrepareAuthorityTransaction();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_TryPrepareAuthorityTransaction();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_TryPrepareAuthorityTransaction();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_TryPrepareAuthorityTransaction();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_TryPrepareAuthorityTransaction();
        }
        return false;
    }
    private bool StationPrepared(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_IsAuthorityTransactionPrepared();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_IsAuthorityTransactionPrepared();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_IsAuthorityTransactionPrepared();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_IsAuthorityTransactionPrepared();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_IsAuthorityTransactionPrepared();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_IsAuthorityTransactionPrepared();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_IsAuthorityTransactionPrepared();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_IsAuthorityTransactionPrepared();
        }
        return false;
    }
    private bool StationControl(int32 InIndex, int32 Action, float32 Value)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ApplyAuthorityControl(Action,Value);
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ApplyAuthorityControl(Action,Value);
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ApplyAuthorityControl(Action,Value);
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ApplyAuthorityControl(Action,Value);
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ApplyAuthorityControl(Action,Value);
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ApplyAuthorityControl(Action,Value);
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ApplyAuthorityControl(Action,Value);
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ApplyAuthorityControl(Action,Value);
        }
        return false;
    }
    private bool StationControlSettled(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityControl();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityControl();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityControl();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityControl();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityControl();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityControl();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityControl();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityControl();
        }
        return false;
    }
    private bool StationReset(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginAuthorityReset();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginAuthorityReset();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginAuthorityReset();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginAuthorityReset();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginAuthorityReset();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginAuthorityReset();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginAuthorityReset();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginAuthorityReset();
        }
        return false;
    }
    private bool StationResetSettled(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityReset();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityReset();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityReset();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityReset();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityReset();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityReset();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityReset();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasSettledAuthorityReset();
        }
        return false;
    }
    private bool StationFinish(int32 InIndex, bool Resume)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginFinishAuthorityTransaction(Resume);
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginFinishAuthorityTransaction(Resume);
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginFinishAuthorityTransaction(Resume);
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginFinishAuthorityTransaction(Resume);
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginFinishAuthorityTransaction(Resume);
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginFinishAuthorityTransaction(Resume);
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginFinishAuthorityTransaction(Resume);
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_BeginFinishAuthorityTransaction(Resume);
        }
        return false;
    }
    private bool StationFinished(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasFinishedAuthorityTransaction();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasFinishedAuthorityTransaction();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasFinishedAuthorityTransaction();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasFinishedAuthorityTransaction();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasFinishedAuthorityTransaction();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasFinishedAuthorityTransaction();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasFinishedAuthorityTransaction();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_HasFinishedAuthorityTransaction();
        }
        return false;
    }
    private bool StationCanRelease(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_CanReleaseAuthorityTransaction();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_CanReleaseAuthorityTransaction();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_CanReleaseAuthorityTransaction();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_CanReleaseAuthorityTransaction();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_CanReleaseAuthorityTransaction();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_CanReleaseAuthorityTransaction();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_CanReleaseAuthorityTransaction();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Get_CanReleaseAuthorityTransaction();
        }
        return false;
    }
    private bool StationRelease(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 8 || ServerChildren.Num() != 8 ||
            !CkAttributeFloat::IsLiveEntity(ServerChildren[InIndex])) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityTransaction();
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityTransaction();
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityTransaction();
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityTransaction();
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityTransaction();
        }
        if (InIndex == 5)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityTransaction();
        }
        if (InIndex == 6)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityTransaction();
        }
        if (InIndex == 7)
        {
            auto S = Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            return S.Request_ReleaseAuthorityTransaction();
        }
        return false;
    }
    private bool Get_IsValidServerRoots()
    {
        if (ServerChildren.Num()!=8 || _Stations.Num()!=8) { return false; }
        auto World = ck::TransientEntity();
        if (!CkAttributeFloat::IsLiveEntity(World)) { return false; }
        for (int32 i=0;i<8;i++)
        {
            if (!CkAttributeFloat::IsLiveEntity(ServerChildren[i]) ||
                CkAttributeFloat::FindStation(i)!=_Stations[i] ||
                utils_entity_lifetime::Get_LifetimeOwner(ServerChildren[i])!=_Stations[i]) { return false; }
            auto Matches=utils_entity_tag::ForEach_Entity(World,CkAttributeFloat::Get_ChildTag(i));
            if (Matches.Num()!=1 || Matches[0]!=ServerChildren[i]) { return false; }
        }
        return true;
    }
    private bool Get_IsValidServerChildren()
    {
        if (!Get_IsValidServerRoots()) { return false; }
        for (int32 i=0;i<8;i++) { if (!StationComposition(i)) { return false; } }
        return true;
    }
    bool Get_HasReadyServerChildren()
    {
        if (!Snapshot.Ready || Snapshot.Failed || ServerChildCount!=8) { return false; }
        if (Get_HasAnyStationFailure()) { Request_Fail("Float ready dependencies lost"); return false; }
        return true;
    }
    bool Get_HasCompleteSnapshot() { return CkAttributeFloat::IsValidSnapshot(Snapshot); }
    bool Get_AutoRunning(int32 InIndex) { return Get_HasCompleteSnapshot() && InIndex>=0 && InIndex<8 && Snapshot.Stations[InIndex].AutoEnabled; }
    private bool Get_HasAnyStationFailure()
    {
        if (!Get_IsValidServerRoots()) { return true; }
        for (int32 i=0;i<8;i++)
        { if (StationFailure(i) || (Snapshot.Ready && !StationDependencies(i))) { return true; } }
        return false;
    }
    private bool TryAttribute(FCk_Handle_FloatAttribute A, FName Name, FCkAttributeGym_FloatAttributeSample& Out)
    {
        Out=FCkAttributeGym_FloatAttributeSample();
        _FirstSnapshotFailureStage = "attribute-live";
        if (!CkAttributeFloat::IsLiveEntity(FCk_Handle(A))) { return false; }
        _FirstSnapshotFailureStage = "attribute-feature";
        if (!utils_float_attribute::DoCast(FCk_Handle(A)).IsSet()) { return false; }
        _FirstSnapshotFailureStage = "attribute-current";
        if (!utils_float_attribute::Has_Component(A,ECk_MinMaxCurrent::Current)) { return false; }
        Out.Name=Name;
        if (utils_float_attribute::Has_Component(A,ECk_MinMaxCurrent::Min))
        {
            Out.Min.Present=true;
            Out.Min.Base=utils_float_attribute::Get_BaseValue(A,ECk_MinMaxCurrent::Min);
            Out.Min.Bonus=utils_float_attribute::Get_BonusValue(A,ECk_MinMaxCurrent::Min);
            Out.Min.Final=utils_float_attribute::Get_FinalValue(A,ECk_MinMaxCurrent::Min);
        }
        if (utils_float_attribute::Has_Component(A,ECk_MinMaxCurrent::Max))
        {
            Out.Max.Present=true;
            Out.Max.Base=utils_float_attribute::Get_BaseValue(A,ECk_MinMaxCurrent::Max);
            Out.Max.Bonus=utils_float_attribute::Get_BonusValue(A,ECk_MinMaxCurrent::Max);
            Out.Max.Final=utils_float_attribute::Get_FinalValue(A,ECk_MinMaxCurrent::Max);
        }
        if (utils_float_attribute::Has_Component(A,ECk_MinMaxCurrent::Current))
        {
            Out.Current.Present=true;
            Out.Current.Base=utils_float_attribute::Get_BaseValue(A,ECk_MinMaxCurrent::Current);
            Out.Current.Bonus=utils_float_attribute::Get_BonusValue(A,ECk_MinMaxCurrent::Current);
            Out.Current.Final=utils_float_attribute::Get_FinalValue(A,ECk_MinMaxCurrent::Current);
        }
        Out.PreClampFinal=utils_float_attribute::Get_PreClampFinalValue(A);
        Out.ClampOverflow=utils_float_attribute::Get_ClampOverflow(A);
        _FirstSnapshotFailureStage = "attribute-validator";
        return CkAttributeFloat::IsValidAttribute(Out,Name,Out.Min.Present);
    }
    private bool Request_TryBuildSnapshot(FCkAttributeGym_FloatSnapshot& Out)
    {
        Out=FCkAttributeGym_FloatSnapshot();
        _FirstSnapshotFailureSlot = -1;
        _FirstSnapshotFailureAttribute = -1;
        _FirstSnapshotFailureStage = "children";
        if (!Get_IsValidServerChildren()) { return false; }
        _FirstSnapshotFailureStage = "generation";
        if (Snapshot.Generation<=0) { return false; }
        _FirstSnapshotFailureStage = "sequence-overflow";
        if (Snapshot.Sequence==2147483647) { return false; }
        auto Candidate=FCkAttributeGym_FloatSnapshot();
        Candidate.Generation=Snapshot.Generation; Candidate.Sequence=Snapshot.Sequence+1; Candidate.Ready=true;
        for (int32 i=0;i<8;i++)
        {
            _FirstSnapshotFailureSlot = i;
            _FirstSnapshotFailureAttribute = -1;
            _FirstSnapshotFailureStage = "sample";
            if (!StationSampleSettled(i)) { return false; }
            auto Sample=FCkAttributeGym_FloatStationSample();
            Sample.StationIndex=i; Sample.StationTag=CkAttributeFloat::Get_StationTag(i); Sample.ChildTag=CkAttributeFloat::Get_ChildTag(i);
            auto Base=UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[i]);
            if (i==0)
            {
                auto S=Cast<UCk_EntityScript_AttributeGym_FloatValues>(Base);
                _FirstSnapshotFailureStage = "script";
                if (ck::Is_NOT_Valid(S)) { return false; }
                _FirstSnapshotFailureStage = "presentation";
                if (!S.Request_TryBuildPresentation(Sample.Title,Sample.Body)) { return false; }
                Sample.AutoEnabled=S.AutoRunning; Sample.AutoStep=S.AutoStep;
                Sample.AutoRunning=S.AutoTimer.Get_CurrentState()==ECk_Timer_State::Running;
                Sample.ValueChangeCount=S.ValueChangeCount;
                Sample.ModifierCount=S.ActiveModifiers.Num();
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=0; if (!TryAttribute(S.TestAttribute,n"FloatAttribute.Test",A)) { return false; } Sample.Attributes.Add(A); }
            }
            if (i==1)
            {
                auto S=Cast<UCk_EntityScript_AttributeGym_FloatClamping>(Base);
                _FirstSnapshotFailureStage = "script";
                if (ck::Is_NOT_Valid(S)) { return false; }
                _FirstSnapshotFailureStage = "presentation";
                if (!S.Request_TryBuildPresentation(Sample.Title,Sample.Body)) { return false; }
                Sample.AutoEnabled=S.AutoRunning; Sample.AutoStep=S.AutoStep;
                Sample.AutoRunning=S.AutoTimer.Get_CurrentState()==ECk_Timer_State::Running;
                Sample.ValueChangeCount=S.ValueChangeCount;
                Sample.ClampedCount=S.ClampedCount;
                Sample.LastPreClampValue=S.LastPreClampValue;
                Sample.LastClampedValue=S.LastClampedValue;
                Sample.LastOverflow=S.LastOverflow;
                Sample.ClampEvents=S.ClampEvents;
                Sample.BoundaryEventSerial=S.BoundaryEventSerial;
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=0; if (!TryAttribute(S.ArmorAttribute,n"FloatAttribute.Armor",A)) { return false; } Sample.Attributes.Add(A); }
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=1; if (!TryAttribute(S.StaminaAttribute,n"FloatAttribute.Stamina",A)) { return false; } Sample.Attributes.Add(A); }
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=2; if (!TryAttribute(S.HealthAttribute,n"FloatAttribute.Health",A)) { return false; } Sample.Attributes.Add(A); }
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=3; if (!TryAttribute(S.ShieldAttribute,n"FloatAttribute.Defense",A)) { return false; } Sample.Attributes.Add(A); }
            }
            if (i==2)
            {
                auto S=Cast<UCk_EntityScript_AttributeGym_FloatModifiers>(Base);
                _FirstSnapshotFailureStage = "script";
                if (ck::Is_NOT_Valid(S)) { return false; }
                _FirstSnapshotFailureStage = "presentation";
                if (!S.Request_TryBuildPresentation(Sample.Title,Sample.Body)) { return false; }
                Sample.AutoEnabled=S.AutoRunning; Sample.AutoStep=S.AutoStep;
                Sample.AutoRunning=S.AutoTimer.Get_CurrentState()==ECk_Timer_State::Running;
                Sample.ValueChangeCount=S.ValueChangeCount;
                Sample.ModifierCount=S.ActiveModifiers.Num();
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=0; if (!TryAttribute(S.DamageAttribute,n"FloatAttribute.Damage",A)) { return false; } Sample.Attributes.Add(A); }
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=1; if (!TryAttribute(S.DefenseAttribute,n"FloatAttribute.Defense",A)) { return false; } Sample.Attributes.Add(A); }
            }
            if (i==3)
            {
                auto S=Cast<UCk_EntityScript_AttributeGym_FloatMinMaxCurrent>(Base);
                _FirstSnapshotFailureStage = "script";
                if (ck::Is_NOT_Valid(S)) { return false; }
                _FirstSnapshotFailureStage = "presentation";
                if (!S.Request_TryBuildPresentation(Sample.Title,Sample.Body)) { return false; }
                Sample.AutoEnabled=S.AutoRunning; Sample.AutoStep=S.AutoStep;
                Sample.AutoRunning=S.AutoTimer.Get_CurrentState()==ECk_Timer_State::Running;
                Sample.MinChangeCount=S.MinChangeCount;
                Sample.MaxChangeCount=S.MaxChangeCount;
                Sample.CurrentChangeCount=S.CurrentChangeCount;
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=0; if (!TryAttribute(S.PowerLevelAttribute,n"FloatAttribute.PowerLevel",A)) { return false; } Sample.Attributes.Add(A); }
            }
            if (i==4)
            {
                auto S=Cast<UCk_EntityScript_AttributeGym_FloatSignals>(Base);
                _FirstSnapshotFailureStage = "script";
                if (ck::Is_NOT_Valid(S)) { return false; }
                _FirstSnapshotFailureStage = "presentation";
                if (!S.Request_TryBuildPresentation(Sample.Title,Sample.Body)) { return false; }
                Sample.AutoEnabled=S.AutoRunning; Sample.AutoStep=S.AutoStep;
                Sample.AutoRunning=S.AutoTimer.Get_CurrentState()==ECk_Timer_State::Running;
                Sample.Delegate1Count=S.Delegate1Count;
                Sample.Delegate2Count=S.Delegate2Count;
                Sample.Delegate3Count=S.Delegate3Count;
                Sample.Delegate1Bound=S.Delegate1Bound;
                Sample.Delegate2Bound=S.Delegate2Bound;
                Sample.Delegate3Bound=S.Delegate3Bound;
                Sample.MinClampCount=S.MinClampCount;
                Sample.MaxClampCount=S.MaxClampCount;
                Sample.LastPreviousFinal=S.LastPreviousFinal;
                Sample.LastNewFinal=S.LastNewFinal;
                Sample.LastPreClampValue=S.LastPreClampValue;
                Sample.LastClampedValue=S.LastClampedValue;
                Sample.LastOverflow=S.LastOverflow;
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=0; if (!TryAttribute(S.TestAttribute,n"FloatAttribute.Signal",A)) { return false; } Sample.Attributes.Add(A); }
            }
            if (i==5)
            {
                auto S=Cast<UCk_EntityScript_AttributeGym_FloatRefill>(Base);
                _FirstSnapshotFailureStage = "script";
                if (ck::Is_NOT_Valid(S)) { return false; }
                _FirstSnapshotFailureStage = "presentation";
                if (!S.Request_TryBuildPresentation(Sample.Title,Sample.Body)) { return false; }
                Sample.AutoEnabled=S.AutoRunning; Sample.AutoStep=S.AutoStep;
                Sample.AutoRunning=S.AutoTimer.Get_CurrentState()==ECk_Timer_State::Running;
                Sample.ValueChangeCount=S.ValueChangeCount;
                Sample.DesiredEnergyRefillRunning=S.DesiredEnergyRefillRunning;
                Sample.DesiredManaRefillRunning=S.DesiredManaRefillRunning;
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=0; if (!TryAttribute(S.EnergyAttribute,n"FloatAttribute.Energy",A)) { return false; } Sample.Attributes.Add(A); }
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=1; if (!TryAttribute(S.ManaAttribute,n"FloatAttribute.Mana",A)) { return false; } Sample.Attributes.Add(A); }
                auto E=utils_float_attribute::TryGet_RefillAttribute(S.EnergyAttribute);
                auto M=utils_float_attribute::TryGet_RefillAttribute(S.ManaAttribute);
                _FirstSnapshotFailureAttribute = -1;
                _FirstSnapshotFailureStage = "energy-refill-live";
                if (!CkAttributeFloat::IsLiveEntity(FCk_Handle(E))) { return false; }
                _FirstSnapshotFailureStage = "mana-refill-live";
                if (!CkAttributeFloat::IsLiveEntity(FCk_Handle(M))) { return false; }
                _FirstSnapshotFailureStage = "energy-refill-feature";
                if (!utils_float_attribute_refill::Has_Refill(FCk_Handle(E))) { return false; }
                _FirstSnapshotFailureStage = "mana-refill-feature";
                if (!utils_float_attribute_refill::Has_Refill(FCk_Handle(M))) { return false; }
                Sample.EnergyRefillRunning=utils_float_attribute_refill::Get_RefillState(E)==ECk_Attribute_RefillState::Running;
                Sample.ManaRefillRunning=utils_float_attribute_refill::Get_RefillState(M)==ECk_Attribute_RefillState::Running;
                Sample.EnergyFillRate=utils_float_attribute_refill::Get_FillRate(E); Sample.ManaFillRate=utils_float_attribute_refill::Get_FillRate(M);
                auto EF=utils_float_attribute::DoCast(FCk_Handle(E)); auto MF=utils_float_attribute::DoCast(FCk_Handle(M));
                _FirstSnapshotFailureStage = "energy-refill-cast";
                if (!EF.IsSet()) { return false; }
                _FirstSnapshotFailureStage = "mana-refill-cast";
                if (!MF.IsSet()) { return false; }
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=2; if (!TryAttribute(EF.GetValue(),n"FloatAttribute.Energy.Refill",A)) { return false; } Sample.Attributes.Add(A); }
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=3; if (!TryAttribute(MF.GetValue(),n"FloatAttribute.Mana.Refill",A)) { return false; } Sample.Attributes.Add(A); }
            }
            if (i==6)
            {
                auto S=Cast<UCk_EntityScript_AttributeGym_FloatIncrementDecrement>(Base);
                _FirstSnapshotFailureStage = "script";
                if (ck::Is_NOT_Valid(S)) { return false; }
                _FirstSnapshotFailureStage = "presentation";
                if (!S.Request_TryBuildPresentation(Sample.Title,Sample.Body)) { return false; }
                Sample.AutoEnabled=S.AutoRunning; Sample.AutoStep=S.AutoStep;
                Sample.AutoRunning=S.AutoTimer.Get_CurrentState()==ECk_Timer_State::Running;
                Sample.ValueChangeCount=S.ValueChangeCount;
                Sample.ModifierCount=S.RevocableModifiers.Num();
                { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=0; if (!TryAttribute(S.CounterAttribute,n"FloatAttribute.Counter",A)) { return false; } Sample.Attributes.Add(A); }
            }
            if (i==7)
            {
                auto S=Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(Base);
                _FirstSnapshotFailureStage = "script";
                if (ck::Is_NOT_Valid(S)) { return false; }
                _FirstSnapshotFailureStage = "presentation";
                if (!S.Request_TryBuildPresentation(Sample.Title,Sample.Body)) { return false; }
                Sample.AutoEnabled=S.AutoRunning; Sample.AutoStep=S.AutoStep;
                Sample.AutoRunning=S.AutoTimer.Get_CurrentState()==ECk_Timer_State::Running;
                Sample.ValueChangeCount=S.ValueChangeCount;
                Sample.TotalAttributes=S.TotalAttributes;
                Sample.CombatAttributes=S.CombatAttributes.Num();
                _FirstSnapshotFailureAttribute = -1;
                _FirstSnapshotFailureStage = "multiple-all-layout";
                if (S.AllAttributes.Num()!=3+Sample.CombatAttributes) { return false; }
                _FirstSnapshotFailureStage = "multiple-rpg-layout";
                if (S.RPGAttributes.Num()!=3) { return false; }
                TArray<FName> Names; Names.Add(n"RPG.Strength"); Names.Add(n"RPG.Agility"); Names.Add(n"RPG.Intelligence");
                if (Sample.CombatAttributes==2) { Names.Add(n"Combat.Attack"); Names.Add(n"Combat.Defense"); }
                _FirstSnapshotFailureStage = "multiple-names";
                if (Names.Num()!=S.AllAttributes.Num()) { return false; }
                for (int32 j=0;j<Names.Num();j++) { auto A=FCkAttributeGym_FloatAttributeSample(); _FirstSnapshotFailureAttribute=j; if (!TryAttribute(S.AllAttributes[j],Names[j],A)) { return false; } Sample.Attributes.Add(A); }
            }
            _FirstSnapshotFailureAttribute = -1;
            _FirstSnapshotFailureStage = "station-validator";
            if (!CkAttributeFloat::IsValidSample(Sample)) { return false; }
            Candidate.Stations.Add(Sample);
        }
        _FirstSnapshotFailureSlot = -1;
        _FirstSnapshotFailureAttribute = -1;
        _FirstSnapshotFailureStage = "snapshot-validator";
        if (!CkAttributeFloat::IsValidSnapshot(Candidate)) { return false; }
        _FirstSnapshotFailureStage = "none";
        Out=Candidate; return true;
    }
    UFUNCTION() private void OnServerMonitor()
    {
        if (!HasAuthority() || Snapshot.Failed) { return; }
        auto Now=float(System::GetGameTimeInSeconds());
        if (_Starting)
        {
            if (!Get_IsValidServerRoots()) { return; }
            if (Get_HasAnyStationFailure()) { Request_Fail("Float startup child failed"); return; }
            if (!_StartIssued)
            {
                if (!Get_IsValidServerChildren()) { return; }
                _StartIssued=true;
                for (int32 i=0;i<8;i++) { if (!StationStart(i)) { Request_Fail("Float startup admission rejected"); return; } }
                return;
            }
            if (!_StartReleased)
            {
                for (int32 i=0;i<8;i++) { if (!StationStarted(i)) { return; } }
                for (int32 i=0;i<8;i++) { if (!StationReleaseStart(i)) { Request_Fail("Float startup release rejected"); return; } }
                _StartReleased=true;
            }
            auto Initial=FCkAttributeGym_FloatSnapshot();
            if (!Request_TryBuildSnapshot(Initial)) { return; }
            Snapshot=Initial; ServerChildCount=8; _Starting=false; _LastPublishSeconds=Now; ForceNetUpdate(); return;
        }
        if (!Snapshot.Ready || ServerChildCount!=8) { return; }
        if (Get_HasAnyStationFailure())
        {
            int32 FaultSlot = -1;
            bool StationFailed = false;
            bool DependenciesLost = false;
            if (Get_IsValidServerRoots())
            {
                for (int32 i = 0; i < 8; i++)
                {
                    StationFailed = StationFailure(i);
                    DependenciesLost = !StationFailed && !StationDependencies(i);
                    if (StationFailed || DependenciesLost) { FaultSlot = i; break; }
                }
            }
            System::PrintString(
                f"[AttributeFloatChild] slot={FaultSlot} stationFailed={StationFailed} dependenciesLost={DependenciesLost}",
                false, true, FLinearColor::White, 0.0f, NAME_None);
            Request_Fail("Float authority child lost"); return;
        }
        if (_TransactionPending) { Request_CheckTransaction(); return; }
        if (!Get_IsValidServerChildren())
        { if (Now-_LastPublishSeconds>=20.0) { Request_Fail("Float composition failed to settle"); } return; }
        auto Multiple=Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[7]));
        if (ck::Is_NOT_Valid(Multiple)) { Request_Fail("Multiple script unavailable"); return; }
        auto Pending=Multiple.Get_PendingAuthorityAutoAction();
        if (Pending>=0)
        { _PendingAutoAction=Pending; Request_BeginTransaction(nullptr,0,-100,Pending); Request_CheckTransaction(); return; }
        if (Now-_LastPublishSeconds<0.1) { return; }
        auto Candidate=FCkAttributeGym_FloatSnapshot();
        if (!Request_TryBuildSnapshot(Candidate))
        { if (Now-_LastPublishSeconds>=20.0) { Request_Fail("Float sample failed to settle"); } return; }
        Snapshot=Candidate; _LastPublishSeconds=Now; ForceNetUpdate();
    }
    private int32 Get_TargetStation(int32 Action)
    {
        if (Action>=3 && Action<=10) { return Action-3; }
        if (Action==12) { return 0; } if (Action==13) { return 2; }
        if (Action==14) { return 5; } if (Action==15) { return 6; }
        if (Action==16 || Action==-100) { return 7; }
        if (Action>=18 && Action<=20) { return 3; } if (Action==21) { return 4; }
        if (Action==23) { return 1; } return -1;
    }
    private float32 Get_ControlValue(int32 Action,int32 Preset)
    {
        TArray<float32> Values;
        if (Action==18) { Values.Add(10.0f); Values.Add(25.5f); Values.Add(60.0f); Values.Add(220.0f); }
        if (Action==19) { Values.Add(200.0f); Values.Add(175.75f); Values.Add(90.0f); Values.Add(5.0f); }
        if (Action==20) { Values.Add(100.0f); Values.Add(142.3f); Values.Add(250.0f); Values.Add(-40.0f); }
        if (Action==21) { Values.Add(150.0f); Values.Add(0.0f); Values.Add(300.0f); Values.Add(-50.0f); }
        return Preset>=0 && Preset<Values.Num() ? Values[Preset] : 0.0f;
    }
    bool Request_ApplyControl(ACk_FloatAttributeGym_PlayerController Owner,int32 Serial,int32 Action,int32 Preset)
    {
        if (!HasAuthority() || !Get_HasReadyServerChildren() || _TransactionPending || ck::Is_NOT_Valid(Owner) ||
            Owner.GetWorld()!=GetWorld() || Serial<=0 || !CkAttributeFloat::IsAction(Action) ||
            (CkAttributeFloat::PresetCount(Action)==0 ? Preset!=-1 : Preset<0 || Preset>=CkAttributeFloat::PresetCount(Action))) { return false; }
        Request_BeginTransaction(Owner,Serial,Action,Preset); Request_CheckTransaction(); return !Snapshot.Failed;
    }
    private void Request_BeginTransaction(ACk_FloatAttributeGym_PlayerController Owner,int32 Serial,int32 Action,int32 Preset)
    {
        _ResumeAuto.Empty(); for (int32 i=0;i<8;i++) { _ResumeAuto.Add(Snapshot.Stations[i].AutoEnabled); }
        _TransactionPending=true; _TransactionPhase=0; _TransactionAction=Action; _TransactionPreset=Preset;
        _TransactionSerial=Serial; _TransactionGeneration=Snapshot.Generation; _TransactionOwner=Owner;
        _TransactionSeconds=float(System::GetGameTimeInSeconds());
    }
    private bool IsResetTarget(int32 i)
    { return _TransactionAction==24 && (_TransactionPreset==1 || i==1); }
    private void Request_CheckTransaction()
    {
        if (!_TransactionPending || Snapshot.Failed) { return; }
        if (_TransactionGeneration!=Snapshot.Generation || Get_HasAnyStationFailure())
        { Request_Fail("Float transaction generation or dependency lost"); return; }
        if (float(System::GetGameTimeInSeconds())-_TransactionSeconds>=20.0)
        {
            int32 FirstWaiting = -1;
            FString Gate = "unknown";
            FString Rows = "";
            if (_TransactionPhase==0) { Gate = "prepare"; }
            else if (_TransactionPhase==2)
            { Gate = _TransactionAction==24 ? "reset" : "control"; }
            else if (_TransactionPhase==3) { Gate = "finish"; }
            for (int32 i=0;i<8;i++)
            {
                bool Relevant = _TransactionPhase==0 || _TransactionPhase==3 ||
                    (_TransactionPhase==2 && _TransactionAction==24 && IsResetTarget(i)) ||
                    (_TransactionPhase==2 && _TransactionAction!=24 &&
                        i==Get_TargetStation(_TransactionAction));
                if (!Relevant) { continue; }
                bool Settled = _TransactionPhase==0 ? StationPrepared(i) :
                    (_TransactionPhase==2 ? (_TransactionAction==24 ?
                        StationResetSettled(i) : StationControlSettled(i)) :
                        (_TransactionPhase==3 ? StationFinished(i) : false));
                Rows += f"{i}:{Settled} ";
                if (!Settled && FirstWaiting<0) { FirstWaiting=i; }
            }
            bool SampleSettled = FirstWaiting>=0 && StationSampleSettled(FirstWaiting);
            bool Dependencies = FirstWaiting>=0 && StationDependencies(FirstWaiting);
            System::PrintString(
                f"[AttributeFloatTxnTimeout] action={_TransactionAction} preset={_TransactionPreset} serial={_TransactionSerial} phase={_TransactionPhase} gate={Gate} first={FirstWaiting} rows={Rows} sample={SampleSettled} dependencies={Dependencies}",
                false, true, FLinearColor::White, 0.0f, NAME_None);
            Request_Fail("Float transaction timed out"); return;
        }
        auto Target=Get_TargetStation(_TransactionAction);
        if (_TransactionPhase==0)
        {
            bool All=true; for (int32 i=0;i<8;i++) { bool Ready=StationPrepare(i); All=All && Ready; }
            if (!All) { return; }
            for (int32 i=0;i<8;i++) { if (!StationPrepared(i)) { return; } }
            _TransactionPhase=1;
        }
        if (_TransactionPhase==1)
        {
            auto Action=_TransactionAction;
            if (Action==1) { bool All=true; for (auto Enabled:_ResumeAuto) { All=All && Enabled; } for(int32 i=0;i<8;i++) { _ResumeAuto[i]=!All; } }
            else if (Action>=3 && Action<=10) { _ResumeAuto[Target]=!_ResumeAuto[Target]; }
            else if (Action==24)
            { for(int32 i=0;i<8;i++) { if(IsResetTarget(i)) { if(!StationReset(i)) { Request_Fail("Float reset rejected"); return; } } } }
            else
            {
                if (Target<0) { Request_Fail("Float action target invalid"); return; }
                int32 Local=_TransactionPreset; float32 Value=Get_ControlValue(Action,_TransactionPreset);
                if (Action>=18 && Action<=20) { Local=Action-18; }
                if (Action==21 || Action==23) { Local=0; }
                if (!StationControl(Target,Local,Value)) { Request_Fail("Float station control rejected"); return; }
                if (Action!=-100) { _ResumeAuto[Target]=false; }
            }
            _TransactionPhase=2;
        }
        if (_TransactionPhase==2)
        {
            if (_TransactionAction==24)
            { for(int32 i=0;i<8;i++) { if(IsResetTarget(i) && !StationResetSettled(i)) { return; } } }
            else if (Target>=0 && !(_TransactionAction>=3 && _TransactionAction<=10) && !StationControlSettled(Target)) { return; }
            for(int32 i=0;i<8;i++) { if(!StationFinish(i,_ResumeAuto[i])) { Request_Fail("Float finish rejected"); return; } }
            _TransactionPhase=3; return;
        }
        if (_TransactionPhase!=3) { return; }
        for(int32 i=0;i<8;i++) { if(!StationFinished(i)) { return; } }
        for(int32 i=0;i<8;i++) { if(!StationCanRelease(i)) { Request_Fail("Float collective release preflight failed"); return; } }
        for(int32 i=0;i<8;i++) { if(!StationRelease(i)) { Request_Fail("Float collective release failed"); return; } }
        if (_TransactionAction==-100)
        {
            auto Multiple=Cast<UCk_EntityScript_AttributeGym_FloatMultiple>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[7]));
            if(ck::Is_NOT_Valid(Multiple) || !Multiple.Request_ConsumeAuthorityAutoAction()) { Request_Fail("Multiple auto action commit failed"); return; }
            _PendingAutoAction=-1;
        }
        auto Candidate=FCkAttributeGym_FloatSnapshot();
        if(!Request_TryBuildSnapshot(Candidate))
        {
            System::PrintString(
                f"[AttributeFloatRelease] action={_TransactionAction} preset={_TransactionPreset} phase={_TransactionPhase} " +
                f"slot={_FirstSnapshotFailureSlot} attribute={_FirstSnapshotFailureAttribute} stage={_FirstSnapshotFailureStage}",
                false, true, FLinearColor::White, 0.0f, NAME_None);
            Request_Fail("Float release snapshot unavailable"); return;
        }
        Snapshot=Candidate; _LastPublishSeconds=float(System::GetGameTimeInSeconds());
        if(_TransactionAction!=-100)
        { auto Commit=FCkAttributeGym_FloatControlCommit(); Commit.Serial=_TransactionSerial; Commit.Action=_TransactionAction; Commit.Preset=_TransactionPreset; Commit.Snapshot=Candidate; LastControlCommit=Commit; }
        _TransactionPending=false; _TransactionPhase=0; ForceNetUpdate();
        if(ck::IsValid(_TransactionOwner) && _TransactionOwner.GetWorld()==GetWorld())
        { _TransactionOwner.Request_CompleteFloatControl(_TransactionSerial,_TransactionAction,_TransactionPreset,_TransactionGeneration,true); }
        _TransactionOwner=nullptr; _ResumeAuto.Empty(); _TransactionAction=-1; _TransactionSerial=0;
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
        { Request_FailLocal("Projection octet dependency lost"); return false; }
        return true;
    }

    private bool Get_IsValidLocalProjection()
    {
        if (_LocalChildren.Num() != 8) { return false; }
        auto World = ck::TransientEntity();
        if (!CkAttributeFloat::IsLiveEntity(World)) { return false; }
        for (int32 i = 0; i < 8; i++)
        {
            auto Station = CkAttributeFloat::FindStation(i);
            auto Child = _LocalChildren[i];
            if (!CkAttributeFloat::IsLiveEntity(Station) ||
                !CkAttributeFloat::IsLiveEntity(Child) ||
                utils_entity_lifetime::Get_LifetimeOwner(Child) != Station)
            { return false; }
            auto Matches = utils_entity_tag::ForEach_Entity(World,
                n"TAG_AttributeGym_FloatProjection");
            if (Matches.Num() != 8) { return false; }
            int32 Count = 0;
            for (auto Match : Matches) { if (Match == Child) { Count++; } }
            if (Count != 1) { return false; }
            auto Script = Cast<UCk_EntityScript_AttributeGym_FloatProjection>(
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
        auto TransientWorld = ck::TransientEntity();
        if (!CkAttributeFloat::IsLiveEntity(TransientWorld))
        {
            if (LocalProjectionReady || _LocalStarting ||
                float(System::GetGameTimeInSeconds())-_LocalWaitSeconds >= 25.0)
            { Request_FailLocal("Projection transient world unavailable"); }
            return;
        }
        if (World.GetNetMode() == ENetMode::NM_Client)
        {
            for (int32 i = 0; i < 8; i++)
            { if (utils_entity_tag::ForEach_Entity(TransientWorld,
                  CkAttributeFloat::Get_ChildTag(i)).Num() != 0)
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
            bool AllConstructed = _LocalChildren.Num() == 8;
            for (int32 i = 0; i < _LocalChildren.Num(); i++)
            { AllConstructed = AllConstructed && CkAttributeFloat::IsLiveEntity(_LocalChildren[i]); }
            if (AllConstructed && Get_IsValidLocalProjection())
            { _LocalStarting = false; LocalProjectionReady = true; }
            return;
        }
        for (int32 i = 0; i < 8; i++)
        { if (!CkAttributeFloat::IsLiveEntity(CkAttributeFloat::FindStation(i))) { return; } }
        if (utils_entity_tag::ForEach_Entity(TransientWorld,
            n"TAG_AttributeGym_FloatProjection").Num() != 0)
        { Request_FailLocal("Projection identity already exists"); return; }
        _LocalChildren.Empty(); _PendingLocal.Empty();
        for (int32 i = 0; i < 8; i++)
        { _LocalChildren.Add(FCk_Handle()); _PendingLocal.Add(FCk_Handle_PendingEntityScript()); }
        _LocalStarting = true;
        for (int32 i = 0; i < 8; i++)
        {
            auto Station = CkAttributeFloat::FindStation(i);
            auto Params = FFloatProjectionSpawnParams();
            Params.Generation = LocalProjectionGeneration;
            Params.StationIndex = i;
            Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
                Station.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
            auto Owner = Station;
            _PendingLocal[i] = utils_entity_script::Request_SpawnEntity(Owner,
                UCk_EntityScript_AttributeGym_FloatProjection, FInstancedStruct::Make(Params));
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
            InGeneration == LocalProjectionGeneration && InIndex >= 0 && InIndex < 8 &&
            InIndex < _LocalChildren.Num() && InChild == _LocalChildren[InIndex])
        { Request_FailLocal(InReason); }
    }

    private void Request_FailLocal(FString InReason)
    {
        Request_RetireLocal();
        LocalProjectionFailed = true;
        LocalProjectionFailureReason = InReason;
        ck::Error("[AttributeFloat] " + InReason);
    }

    private void Request_RetireLocal()
    {
        LocalProjectionReady = false;
        _LocalStarting = false;
        for (int32 i = 0; i < _PendingLocal.Num(); i++)
        {
            if (utils_pending_entity_script::Get_IsValid(_PendingLocal[i]))
            { utils_pending_entity_script::Request_DestroyEntity(_PendingLocal[i]); }
            else if (i < _LocalChildren.Num() && CkAttributeFloat::IsLiveEntity(_LocalChildren[i]))
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
            _WaitingForCatalog = false;
            _Starting = false;
            _TransactionPending = false;
            Snapshot.Ready = false;
            Snapshot.Failed = true;
            Request_CloseWorldOnFailure();
            return;
        }
        _WaitingForCatalog = false; _Starting = false; _TransactionPending = false; Snapshot.Ready = false;
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
            if (CkAttributeFloat::IsLiveEntity(_WorldStationOwner))
            { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); }
            _WorldStationOwner = FCk_Handle();
        }
    }
}
class UCk_EntityScript_AttributeGym_FloatProjection : UCk_GenericEntityScript_UE
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
        _LastClampSerials.Empty(); for (int32 i=0;i<4;i++) { _LastClampSerials.Add(0); }
        _LastBoundarySerial = 0;
        auto State = CkAttributeFloat::Get_State();
        auto World = ck::IsValid(State) ? State.GetWorld() : nullptr;
        if (!CkAttributeFloat::IsLiveEntity(InHandle) || ck::Is_NOT_Valid(World) ||
            World.GetNetMode() == ENetMode::NM_DedicatedServer ||
            StationIndex < 0 || StationIndex >= 8 ||
            utils_entity_lifetime::Get_LifetimeOwner(InHandle) !=
                CkAttributeFloat::FindStation(StationIndex) ||
            ck::Is_NOT_Valid(State) ||
            !State.Get_IsExpectedLocalChild(InHandle, Generation, StationIndex))
        {
            if (ck::IsValid(State)) { State.Request_FailExpectedLocalChild(
                InHandle, Generation, StationIndex, "Projection role or owner invalid"); }
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_FloatProjection");
        auto TimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
        TimerParams.Set_StartingState(ECk_Timer_State::Running)
            .Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        _DisplayTimer = utils_timer::Add(InHandle, TimerParams);
        if (!CkAttributeFloat::IsLiveEntity(FCk_Handle(_DisplayTimer)))
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
        return _Constructed && CkAttributeFloat::IsLiveEntity(Self) &&
            Children.Num() == 1 && Children[0] == FCk_Handle(_DisplayTimer) &&
            utils_transform::Has(Self) &&
            CkAttributeFloat::IsLiveEntity(FCk_Handle(_DisplayTimer)) &&
            utils_timer::DoCast(FCk_Handle(_DisplayTimer)).IsSet() &&
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_DisplayTimer)) == Self &&
            _DisplayTimer.Get_CurrentState() == ECk_Timer_State::Running;
    }

    UFUNCTION() private void DisplayTick(FCk_Handle_Timer InHandle,
        FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto State = CkAttributeFloat::Get_State();
        if (InHandle != _DisplayTimer || ck::Is_NOT_Valid(State) ||
            !State.Get_HasAdmittedLocalProjection(Generation)) { return; }
        if (!Get_HasValidLocalComposition())
        { State.Request_FailCurrentLocalChild(ck::ToEntity(this), Generation,
            StationIndex, "Projection display dependency lost"); return; }
        auto Sample = State.Snapshot.Stations[StationIndex];
        if (!CkAttributeFloat::IsValidSample(Sample))
        { State.Request_FailCurrentLocalChild(ck::ToEntity(this), Generation,
            StationIndex, "Projection sample malformed"); return; }
        if (StationIndex == 1)
        {
            if (_LastClampSerials.Num()!=4 || Sample.ClampEvents.Num()!=4 ||
                Sample.BoundaryEventSerial < _LastBoundarySerial)
            { State.Request_FailCurrentLocalChild(ck::ToEntity(this),Generation,StationIndex,
                "Float projection event sequence invalid"); return; }
            for (int32 i=0;i<4;i++)
            {
                if (Sample.ClampEvents[i].Serial < _LastClampSerials[i])
                { State.Request_FailCurrentLocalChild(ck::ToEntity(this),Generation,StationIndex,
                    "Float projection clamp sequence regressed"); return; }
            }
            for (int32 i=0;i<4;i++)
            {
                auto Event=Sample.ClampEvents[i];
                if (Event.Serial > _LastClampSerials[i])
                {
                    auto Color=FLinearColor(0.0f,0.0f,1.0f,1.0f);
                    if (i==1) { Color=FLinearColor(1.0f,1.0f,0.0f,1.0f); }
                    if (i==2) { Color=FLinearColor(1.0f,0.0f,0.0f,1.0f); }
                    if (i==3) { Color=FLinearColor(0.0f,1.0f,0.0f,1.0f); }
                    CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this),FVector(-50.0f+50.0f*i,0.0f,150.0f),Color);
                }
                _LastClampSerials[i]=Event.Serial;
            }
            if (Sample.BoundaryEventSerial > _LastBoundarySerial)
            { CkGym_Common::Draw_DebugSphere(ck::ToEntity(this),FVector(0.0f,0.0f,300.0f),
                FLinearColor(1.0f,1.0f,0.0f,1.0f),25.0f,3.0f,2.0f); }
            _LastBoundarySerial=Sample.BoundaryEventSerial;
        }
        FString BaseTitle="FLOAT VALUES";
        if (StationIndex==1) { BaseTitle="FLOAT CLAMPING"; }
        if (StationIndex==2) { BaseTitle="FLOAT MODIFIERS"; }
        if (StationIndex==3) { BaseTitle="FLOAT MIN/MAX/CURRENT"; }
        if (StationIndex==4) { BaseTitle="FLOAT SIGNALS"; }
        if (StationIndex==5) { BaseTitle="FLOAT REFILL"; }
        if (StationIndex==6) { BaseTitle="FLOAT INC/DEC"; }
        if (StationIndex==7) { BaseTitle="FLOAT MULTIPLE"; }
        auto TitleText = BaseTitle + " (" +
            CkGym_Common::Get_NetworkRoleTitle(ck::ToEntity(this)) + ")";
        CkGym_Common::Update_StationDisplay(ck::ToEntity(this), TitleText, Sample.Body, "");
    }
}
