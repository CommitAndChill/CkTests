// Language=angelscript

USTRUCT()
struct FCkMessagingGym_StationSample
{
    UPROPERTY() int32 StationIndex = -1;
    UPROPERTY() FName StationTag = n"";
    UPROPERTY() FName ChildTag = n"";
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() FString Title;
    UPROPERTY() FString Body;
    UPROPERTY() FString Instructions;
    UPROPERTY() int32 PingCount = 0;
    UPROPERTY() int32 PongCount = 0;
    UPROPERTY() int32 AlertCount = 0;
    UPROPERTY() int32 LastSequence = -1;
    UPROPERTY() FString LastSender = "N/A";
    UPROPERTY() int32 ListenerA_Count = 0;
    UPROPERTY() int32 ListenerB_Count = 0;
    UPROPERTY() int32 ListenerC_Count = 0;
    UPROPERTY() bool IsListening = false;
    UPROPERTY() bool IsBound = false;
    UPROPERTY() int32 BindCount = 0;
    UPROPERTY() int32 UnbindCount = 0;
    UPROPERTY() FString LastPingSender = "N/A";
    UPROPERTY() FString LastPongResponder = "N/A";
    UPROPERTY() FString LastAlertText = "N/A";
    UPROPERTY() int32 LastAlertPriority = 0;
}

USTRUCT()
struct FCkMessagingGym_Snapshot
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() int32 MessageSequence = 0;
    UPROPERTY() TArray<FCkMessagingGym_StationSample> Stations;
}

USTRUCT()
struct FCkMessagingGym_ControlCommit
{
    UPROPERTY() int32 DeliverySerial = 0;
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 Row = -1;
    UPROPERTY() int32 AlertPriority = 0;
    UPROPERTY() FCkMessagingGym_Snapshot Snapshot;
}

USTRUCT()
struct FCkMessagingGym_QueuedControl
{
    UPROPERTY() ACk_MessagingGym_PlayerController Owner;
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 Row = -1;
    UPROPERTY() int32 Priority = 0;
    UPROPERTY() int32 Generation = 0;
}

class ACk_MessagingGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkMessagingGym_Snapshot Snapshot;
    UPROPERTY() TArray<FCk_Handle> ServerChildren;
    UPROPERTY() int32 ServerChildCount = 0;

    private FCk_Handle _WorldStationOwner;
    private FCk_Handle _AdmissionOwner;
    private int32 _AdmissionTicket = 0;
    private bool _AdmissionOwned = false;
    private bool _Ending = false;
    private bool _Starting = false;
    private bool _StationPhase = false;
    private bool _ChildPhase = false;
    private bool _Mutating = false;
    private int32 _NextGeneration = 0;
    private float _StartSeconds = 0.0;
    private int64 _LastActionFrame = -1;
    UPROPERTY() TArray<FCkMessagingGym_QueuedControl> _QueuedControls;
    UPROPERTY() private FCkMessagingGym_QueuedControl _ExecutingControl;
    UPROPERTY() private FCkMessagingGym_ControlCommit _PreparedOwnerCommit;
    private TArray<FCk_Handle> _Stations;
    private TArray<FCk_Handle_PendingEntityScript> _PendingStations;
    private TArray<FCk_Handle_PendingEntityScript> _PendingChildren;
    private TArray<int32> _StationCompletions;
    private TArray<int32> _ChildCompletions;

    void Request_EnsureSharedDemo()
    {
        if (_Ending || !Get_HasMessagingWorldIdentity() ||
            _Starting || Snapshot.Ready || Snapshot.Failed) { return; }
        if (_NextGeneration >= 2147483646) { Request_Fail("Messaging generation exhausted"); return; }
        _NextGeneration++;
        Snapshot = FCkMessagingGym_Snapshot();
        Snapshot.Generation = _NextGeneration;
        _Starting = true;
        _StartSeconds = float(System::GetGameTimeInSeconds());
        System::SetTimer(this, n"OnMonitor", 0.1, true);
        Request_StartStations();
    }

    private bool Request_ReleaseAdmission(bool InPublishedComplete = false)
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
        _AdmissionOwned = false;
        if (CkMessagingGym::IsLive(_AdmissionOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_AdmissionOwner); }
        _AdmissionOwner = FCk_Handle();
        return true;
    }

    private void Request_StartStations()
    {
        auto World = ck::TransientEntity();
        auto Declared = CkMessagingGym::Get_Stations();
        if (!CkMessagingGym::IsLive(World) ||
            !utils_entity_lifetime::Get_CanCreateEntity(World) || Declared.Num() != 5)
        { Request_Fail("Messaging world or catalog unavailable"); return; }
        if (CkGym_Admission::Get_IsClosedUnproven(World))
        { Request_Fail("Station admission permanently closed"); return; }
        if (CkGym_Admission::Get_IsBusy(World))
        { System::SetTimer(this, n"OnStationAdmissionRetry", 0.1, false); return; }
        _AdmissionTicket = CkGym_Admission::TryReserve(World);
        if (_AdmissionTicket == 0)
        { Request_Fail("Station admission reservation rejected"); return; }
        _AdmissionOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!CkMessagingGym::IsLive(_AdmissionOwner))
        { Request_Fail("Messaging station admission rejected"); return; }
        if (!CkGym_Admission::TryCommitOwner(World, _AdmissionTicket, _AdmissionOwner))
        { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
          Request_Fail("Station admission owner commit rejected"); return; }
        _AdmissionOwned = true;
        for (int32 i = 0; i < 5; i++)
        {
            if (Declared[i].Tags.Num() != 1 ||
                Declared[i].Tags[0] != CkMessagingGym::Get_StationTag(i) ||
                utils_entity_tag::ForEach_Entity(World, Declared[i].Tags[0]).Num() != 0)
            { Request_Fail("Foreign or duplicate Messaging station"); return; }
        }
        _WorldStationOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!CkMessagingGym::IsLive(_WorldStationOwner))
        { Request_Fail("Messaging world station owner rejected"); return; }
        _StationPhase = true;
        for (int32 i = 0; i < 5; i++)
        {
            auto Pending = CkGym_Common::Request_SpawnNewStationForOwner(
                Declared[i], _WorldStationOwner,
                FCk_Delegate_Request_OnCompleted(this, n"OnSpawnCompleted"));
            if (!utils_pending_entity_script::Get_IsValid(Pending))
            { Request_Fail("Messaging station spawn rejected"); return; }
            _PendingStations.Add(Pending);
            _StationCompletions.Add(0);
        }
    }

    UFUNCTION() private void OnStationAdmissionRetry()
    { if (_Starting && !_Ending && !_StationPhase) { Request_StartStations(); } }

    UFUNCTION() private void OnSpawnCompleted(FCk_Handle InOwner,
        ECk_Request_OperationResult InResult)
    {
        if (_Ending || !_Starting) { return; }
        for (int32 i = 0; i < _PendingStations.Num(); i++)
        {
            if (_PendingStations[i].Get_EntityUnderConstruction() != InOwner) { continue; }
            if (InResult != ECk_Request_OperationResult::Succeeded)
            { Request_Fail("Messaging station completion failed"); return; }
            _StationCompletions[i] = 1;
            return;
        }
        for (int32 i = 0; i < _PendingChildren.Num(); i++)
        {
            if (_PendingChildren[i].Get_EntityUnderConstruction() != InOwner) { continue; }
            if (InResult != ECk_Request_OperationResult::Succeeded)
            { Request_Fail("Messaging child completion failed"); return; }
            _ChildCompletions[i] = 1;
            return;
        }
    }

    private FCk_Handle Get_Station(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 5) { return FCk_Handle(); }
        auto Matches = utils_entity_tag::ForEach_Entity(
            ck::TransientEntity(), CkMessagingGym::Get_StationTag(InIndex));
        if (Matches.Num() != 1 || !CkMessagingGym::IsDirectChild(Matches[0], _WorldStationOwner) ||
            !Matches[0].Has_Fragment(FCkGym_Station_Anchors))
        { return FCk_Handle(); }
        return Matches[0];
    }

    private void Request_StartChildren()
    {
        if (_ChildPhase || _Stations.Num() != 5) { Request_Fail("Messaging station cohort incomplete"); return; }
        _ChildPhase = true;
        for (int32 i = 0; i < 5; i++)
        {
            if (!CkMessagingGym::IsLive(_Stations[i]) ||
                utils_entity_tag::ForEach_Entity(_Stations[i], CkMessagingGym::Get_ChildTag(i)).Num() != 0)
            { Request_Fail("Messaging child owner ambiguous"); return; }
            auto Params = FMessagingGymSpawnParams(
                FTransform(FRotator(0.0, 180.0, 0.0),
                    _Stations[i].Get_Fragment(FCkGym_Station_Anchors).PanelCenter));
            Params.SharedAuthority = true;
            FCk_Handle_PendingEntityScript Pending;
            if (i == 0) { Pending = utils_entity_script::Request_SpawnEntity(_Stations[i],
                UCk_EntityScript_MessagingGym_Basic, FInstancedStruct::Make(Params),
                FCk_Delegate_Request_OnCompleted(this, n"OnSpawnCompleted")); }
            if (i == 1) { Pending = utils_entity_script::Request_SpawnEntity(_Stations[i],
                UCk_EntityScript_MessagingGym_MultiListener, FInstancedStruct::Make(Params),
                FCk_Delegate_Request_OnCompleted(this, n"OnSpawnCompleted")); }
            if (i == 2) { Pending = utils_entity_script::Request_SpawnEntity(_Stations[i],
                UCk_EntityScript_MessagingGym_OneShot, FInstancedStruct::Make(Params),
                FCk_Delegate_Request_OnCompleted(this, n"OnSpawnCompleted")); }
            if (i == 3) { Pending = utils_entity_script::Request_SpawnEntity(_Stations[i],
                UCk_EntityScript_MessagingGym_DynamicBind, FInstancedStruct::Make(Params),
                FCk_Delegate_Request_OnCompleted(this, n"OnSpawnCompleted")); }
            if (i == 4) { Pending = utils_entity_script::Request_SpawnEntity(_Stations[i],
                UCk_EntityScript_MessagingGym_MultiType, FInstancedStruct::Make(Params),
                FCk_Delegate_Request_OnCompleted(this, n"OnSpawnCompleted")); }
            if (!utils_pending_entity_script::Get_IsValid(Pending))
            { Request_Fail("Messaging listener spawn rejected"); return; }
            _PendingChildren.Add(Pending);
            _ChildCompletions.Add(0);
            ServerChildren.Add(Pending.Get_EntityUnderConstruction());
        }
    }

    private bool Request_AppendEndpoint(FCk_Handle InChild, UScriptStruct InMessage,
        TArray<FCk_Handle>& InOutExpected)
    {
        if (!CkMessagingGym::IsLive(InChild) || !ck::IsValid(InMessage)) { return false; }
        auto Endpoint = utils_messaging::TryGet_MessengerEntity(InChild, InMessage);
        if (!CkMessagingGym::IsDirectChild(Endpoint, InChild) ||
            InOutExpected.Contains(Endpoint)) { return false; }
        InOutExpected.Add(Endpoint);
        return true;
    }

    private bool Get_HasTotalBindings(FCk_Handle InChild, UScriptStruct InMessage,
        int32 InDoNothing, int32 InUnbind)
    {
        return utils_messaging::Get_NumBoundTo_OnBroadcast(InChild, InMessage,
                ECk_Signal_PostFireBehavior::DoNothing) == InDoNothing &&
            utils_messaging::Get_NumBoundTo_OnBroadcast(InChild, InMessage,
                ECk_Signal_PostFireBehavior::Unbind) == InUnbind;
    }

    private bool Get_HasUnconditionalBinding(FCk_Handle InChild, UScriptStruct InMessage,
        FCk_Delegate_Messaging_OnBroadcast InDelegate,
        ECk_Signal_PostFireBehavior InBehavior, int32 InExpected)
    {
        return utils_messaging::Get_NumUnconditionalBindingsTo_OnBroadcast(
            InChild, InMessage, InDelegate, InBehavior) == InExpected;
    }

    private bool Get_HasExactBindings(int32 InIndex, FCk_Handle InChild,
        UCk_EntityScript_UE InScript)
    {
        if (ck::Is_NOT_Valid(InScript) || !Get_HasTotalBindings(InChild,
            FCk_Message_MessagingGym_Reset, 1, 0) ||
            !Get_HasUnconditionalBinding(InChild,
                FCk_Message_MessagingGym_Reset,
                FCk_Delegate_Messaging_OnBroadcast(InScript, n"OnReset"),
                ECk_Signal_PostFireBehavior::DoNothing, 1)) { return false; }
        if (InIndex == 0)
        {
            return Get_HasTotalBindings(InChild, FCk_Message_MessagingGym_Ping, 1, 0) &&
                Get_HasUnconditionalBinding(InChild, FCk_Message_MessagingGym_Ping,
                    FCk_Delegate_Messaging_OnBroadcast(InScript, n"OnPing"),
                    ECk_Signal_PostFireBehavior::DoNothing, 1);
        }
        if (InIndex == 1)
        {
            return Get_HasTotalBindings(InChild, FCk_Message_MessagingGym_Ping, 3, 0) &&
                Get_HasUnconditionalBinding(InChild, FCk_Message_MessagingGym_Ping,
                    FCk_Delegate_Messaging_OnBroadcast(InScript, n"OnPing_ListenerA"),
                    ECk_Signal_PostFireBehavior::DoNothing, 1) &&
                Get_HasUnconditionalBinding(InChild, FCk_Message_MessagingGym_Ping,
                    FCk_Delegate_Messaging_OnBroadcast(InScript, n"OnPing_ListenerB"),
                    ECk_Signal_PostFireBehavior::DoNothing, 1) &&
                Get_HasUnconditionalBinding(InChild, FCk_Message_MessagingGym_Ping,
                    FCk_Delegate_Messaging_OnBroadcast(InScript, n"OnPing_ListenerC"),
                    ECk_Signal_PostFireBehavior::DoNothing, 1);
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_MessagingGym_OneShot>(InScript);
            if (ck::Is_NOT_Valid(S)) { return false; }
            int32 Expected = S.IsListening ? 1 : 0;
            return Get_HasTotalBindings(InChild, FCk_Message_MessagingGym_Ping, 0, Expected) &&
                Get_HasUnconditionalBinding(InChild, FCk_Message_MessagingGym_Ping,
                    FCk_Delegate_Messaging_OnBroadcast(S, n"OnPing"),
                    ECk_Signal_PostFireBehavior::Unbind, Expected);
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_MessagingGym_DynamicBind>(InScript);
            if (ck::Is_NOT_Valid(S)) { return false; }
            int32 Expected = S.IsBound ? 1 : 0;
            return Get_HasTotalBindings(InChild, FCk_Message_MessagingGym_Ping, Expected, 0) &&
                Get_HasUnconditionalBinding(InChild, FCk_Message_MessagingGym_Ping,
                    FCk_Delegate_Messaging_OnBroadcast(S, n"OnPing"),
                    ECk_Signal_PostFireBehavior::DoNothing, Expected) &&
                Get_HasTotalBindings(InChild, FCk_Message_MessagingGym_ToggleBind, 1, 0) &&
                Get_HasUnconditionalBinding(InChild, FCk_Message_MessagingGym_ToggleBind,
                    FCk_Delegate_Messaging_OnBroadcast(S, n"OnToggleBind"),
                    ECk_Signal_PostFireBehavior::DoNothing, 1);
        }
        if (InIndex == 4)
        {
            return Get_HasTotalBindings(InChild, FCk_Message_MessagingGym_Ping, 1, 0) &&
                Get_HasTotalBindings(InChild, FCk_Message_MessagingGym_Pong, 1, 0) &&
                Get_HasTotalBindings(InChild, FCk_Message_MessagingGym_Alert, 1, 0) &&
                Get_HasUnconditionalBinding(InChild, FCk_Message_MessagingGym_Ping,
                    FCk_Delegate_Messaging_OnBroadcast(InScript, n"OnPing"),
                    ECk_Signal_PostFireBehavior::DoNothing, 1) &&
                Get_HasUnconditionalBinding(InChild, FCk_Message_MessagingGym_Pong,
                    FCk_Delegate_Messaging_OnBroadcast(InScript, n"OnPong"),
                    ECk_Signal_PostFireBehavior::DoNothing, 1) &&
                Get_HasUnconditionalBinding(InChild, FCk_Message_MessagingGym_Alert,
                    FCk_Delegate_Messaging_OnBroadcast(InScript, n"OnAlert"),
                    ECk_Signal_PostFireBehavior::DoNothing, 1);
        }
        return false;
    }

    private bool Get_HasExactChild(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= 5 || ServerChildren.Num() != 5 ||
            _Stations.Num() != 5 ||
            !CkMessagingGym::IsDirectChild(ServerChildren[InIndex], _Stations[InIndex]))
        { return false; }
        auto Child = ServerChildren[InIndex];
        auto Matches = utils_entity_tag::ForEach_Entity(_Stations[InIndex],
            CkMessagingGym::Get_ChildTag(InIndex));
        if (Matches.Num() != 1 || Matches[0] != Child) { return false; }
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Child);
        if (ck::Is_NOT_Valid(Base)) { return false; }
        if (InIndex == 0)
        { auto S = Cast<UCk_EntityScript_MessagingGym_Basic>(Base);
          if (ck::Is_NOT_Valid(S) || !S.SharedAuthority ||
              Base.GetClass() != UCk_EntityScript_MessagingGym_Basic) { return false; } }
        if (InIndex == 1)
        { auto S = Cast<UCk_EntityScript_MessagingGym_MultiListener>(Base);
          if (ck::Is_NOT_Valid(S) || !S.SharedAuthority ||
              Base.GetClass() != UCk_EntityScript_MessagingGym_MultiListener) { return false; } }
        if (InIndex == 2)
        { auto S = Cast<UCk_EntityScript_MessagingGym_OneShot>(Base);
          if (ck::Is_NOT_Valid(S) || !S.SharedAuthority ||
              Base.GetClass() != UCk_EntityScript_MessagingGym_OneShot) { return false; } }
        if (InIndex == 3)
        { auto S = Cast<UCk_EntityScript_MessagingGym_DynamicBind>(Base);
          if (ck::Is_NOT_Valid(S) || !S.SharedAuthority ||
              Base.GetClass() != UCk_EntityScript_MessagingGym_DynamicBind) { return false; } }
        if (InIndex == 4)
        { auto S = Cast<UCk_EntityScript_MessagingGym_MultiType>(Base);
          if (ck::Is_NOT_Valid(S) || !S.SharedAuthority ||
              Base.GetClass() != UCk_EntityScript_MessagingGym_MultiType) { return false; } }
        TArray<FCk_Handle> Endpoints;
        if (!Request_AppendEndpoint(Child, FCk_Message_MessagingGym_Reset, Endpoints))
        { return false; }
        if (InIndex != 3 && !Request_AppendEndpoint(
            Child, FCk_Message_MessagingGym_Ping, Endpoints)) { return false; }
        if (InIndex == 3 &&
            (!Request_AppendEndpoint(Child, FCk_Message_MessagingGym_Ping, Endpoints) ||
             !Request_AppendEndpoint(Child, FCk_Message_MessagingGym_ToggleBind, Endpoints)))
        { return false; }
        if (InIndex == 4 &&
            (!Request_AppendEndpoint(Child, FCk_Message_MessagingGym_Pong, Endpoints) ||
             !Request_AppendEndpoint(Child, FCk_Message_MessagingGym_Alert, Endpoints)))
        { return false; }
        int32 LiveDependents = 0;
        for (auto Dependent : utils_entity_lifetime::Get_LifetimeDependents(Child))
        {
            if (!CkMessagingGym::IsLive(Dependent)) { continue; }
            if (!Endpoints.Contains(Dependent) ||
                !CkMessagingGym::IsDirectChild(Dependent, Child)) { return false; }
            LiveDependents++;
        }
        return LiveDependents == Endpoints.Num() &&
            Get_HasExactBindings(InIndex, Child, Base);
    }

    bool Get_HasReadyServerCohort()
    {
        if (_Ending || !Get_HasMessagingWorldIdentity() || _Starting || !Snapshot.Ready || Snapshot.Failed ||
            ServerChildCount != 5 || _Stations.Num() != 5 || ServerChildren.Num() != 5)
        { return false; }
        for (int32 i = 0; i < 5; i++)
        {
            if (Get_Station(i) != _Stations[i] || !Get_HasExactChild(i))
            { return false; }
        }
        return true;
    }

    private bool Get_HasValidControlOwner(ACk_MessagingGym_PlayerController InOwner)
    {
        if (_Ending || !HasAuthority() || ck::Is_NOT_Valid(InOwner) ||
            InOwner.GetWorld() != GetWorld() || !InOwner.HasAuthority()) { return false; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) || World.GetGameState() != this) { return false; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(InOwner.GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != InOwner ||
            Pawn.GetWorld() != GetWorld()) { return false; }
        return Get_HasMessagingWorldIdentity();
    }

    private bool Get_HasMessagingWorldIdentity()
    {
        if (_Ending || !HasAuthority() || !Get_HasPublishedCatalog()) { return false; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) || World.GetGameState() != this) { return false; }
        TArray<ACk_MessagingGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_MessagingGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Current = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        return Modes.Num() == 1 && ck::IsValid(Modes[0]) &&
            Modes[0].GetWorld() == World &&
            Gameplay::GetGameMode() == Modes[0] &&
            Modes[0].GetClass() == ACk_MessagingGym_GameMode &&
            Current >= 0 && Current < Registry.Num() &&
            Registry[Current].DisplayName == "Messaging" &&
            Registry[Current].GameModeClass == ACk_MessagingGym_GameMode;
    }

    int32 Request_QueueControl(ACk_MessagingGym_PlayerController InOwner,
        int32 InSerial, int32 InRow, int32 InPriority)
    {
        if (InSerial <= 0 || (InRow != 1 && InRow != 2 && InRow != 3 &&
            InRow != 4 && InRow != 5 && InRow != 6 && InRow != 7 &&
            InRow != 9 && InRow != 10) ||
            (InRow != 6 && InPriority != 0) ||
            (InRow == 6 && InPriority != 1 && InPriority != 5 && InPriority != 9))
        { return 4; }
        if (!Get_HasValidControlOwner(InOwner) ||
            !InOwner.Get_IsSubmittingMessagingControl(InSerial) ||
            !InOwner.Get_HasReservedMessagingControl(InSerial, InRow, InPriority, Snapshot.Generation) ||
            InOwner.LastControlAttemptSerial != InSerial ||
            InSerial <= InOwner.LastControlSerial)
        { return 2; }
        if (_Mutating || !Get_HasReadyServerCohort() ||
            !InOwner.Get_CanAcceptOwnerCommit())
        { return 3; }
        for (auto Existing : _QueuedControls)
        { if (Existing.Owner == InOwner) { return 3; } }
        auto Queued = FCkMessagingGym_QueuedControl();
        Queued.Owner = InOwner;
        Queued.RequestSerial = InSerial;
        Queued.Row = InRow;
        Queued.Priority = InPriority;
        Queued.Generation = Snapshot.Generation;
        _QueuedControls.Add(Queued);
        return 1;
    }

    private bool Get_HasCompleteUnpublishedCohort()
    {
        if (_Ending || !_Starting || _Stations.Num() != 5 || ServerChildren.Num() != 5)
        { return false; }
        for (int32 i = 0; i < 5; i++)
        { if (Get_Station(i) != _Stations[i] || !Get_HasExactChild(i)) { return false; } }
        return true;
    }

    private bool Get_FiniteTransform(FTransform InTransform)
    {
        auto Position = InTransform.GetLocation();
        auto Scale = InTransform.GetScale3D();
        auto Rotation = InTransform.GetRotation();
        auto NormSquared = Rotation.X*Rotation.X + Rotation.Y*Rotation.Y +
            Rotation.Z*Rotation.Z + Rotation.W*Rotation.W;
        return Math::Abs(Position.X) < 100000000.0 &&
            Math::Abs(Position.Y) < 100000000.0 &&
            Math::Abs(Position.Z) < 100000000.0 &&
            Math::Abs(Scale.X) < 1000000.0 &&
            Math::Abs(Scale.Y) < 1000000.0 &&
            Math::Abs(Scale.Z) < 1000000.0 &&
            Math::Abs(Rotation.X) <= 1.01 &&
            Math::Abs(Rotation.Y) <= 1.01 &&
            Math::Abs(Rotation.Z) <= 1.01 &&
            Math::Abs(Rotation.W) <= 1.01 &&
            Math::Abs(NormSquared - 1.0) < 0.01;
    }

    bool Get_IsCompleteSnapshot(FCkMessagingGym_Snapshot InSnapshot)
    {
        if (!InSnapshot.Ready || InSnapshot.Failed || InSnapshot.Generation <= 0 ||
            InSnapshot.Generation >= 2147483647 || InSnapshot.Sequence <= 0 ||
            InSnapshot.Sequence >= 2147483647 || InSnapshot.MessageSequence < 0 ||
            InSnapshot.Stations.Num() != 5) { return false; }
        for (int32 i = 0; i < 5; i++)
        {
            auto S = InSnapshot.Stations[i];
            if (S.StationIndex != i || S.StationTag != CkMessagingGym::Get_StationTag(i) ||
                S.ChildTag != CkMessagingGym::Get_ChildTag(i) ||
                !Get_FiniteTransform(S.InitialTransform) ||
                S.Title.IsEmpty() || S.Body.IsEmpty() || S.Instructions.IsEmpty() ||
                S.PingCount < 0 || S.PongCount < 0 || S.AlertCount < 0 ||
                S.ListenerA_Count < 0 || S.ListenerB_Count < 0 || S.ListenerC_Count < 0 ||
                S.BindCount < 0 || S.UnbindCount < 0 || S.LastSequence < -1 ||
                S.LastAlertPriority < 0 || S.LastAlertPriority > 9)
            { return false; }
            if (!Get_HasAuthoredText(S)) { return false; }
        }
        return true;
    }

    private bool Get_HasAuthoredText(FCkMessagingGym_StationSample S)
    {
        if (S.StationIndex == 0)
        {
            auto Body = f"Demonstrates basic message broadcast and listener binding.\nTests payload extraction with sender and sequence data.\n\n===== Message Stats =====\nPings Received: {S.PingCount}\nLast Sender: {S.LastSender}\nLast Sequence: {S.LastSequence}\n";
            return S.Title == "BASIC BROADCAST & LISTEN" && S.Body == Body &&
                S.Instructions == "panel [1] Ping\npanel [R] Reset every station";
        }
        if (S.StationIndex == 1)
        {
            auto Total = S.ListenerA_Count + S.ListenerB_Count + S.ListenerC_Count;
            auto Body = f"Three delegates bound to the same message type on one entity.\nA single broadcast fires all three listeners simultaneously.\n\n===== Listener Counts =====\nListener A: {S.ListenerA_Count}\nListener B: {S.ListenerB_Count}\nListener C: {S.ListenerC_Count}\nTotal: {Total}\n";
            return S.Title == "MULTI-LISTENER FAN-OUT" && S.Body == Body &&
                S.Instructions == "panel [2] Fan-out ping\npanel [R] Reset every station";
        }
        if (S.StationIndex == 2)
        {
            auto Status = S.IsListening ? "LISTENING" : "NOT LISTENING";
            auto Body = f"Listener auto-unbinds after first fire using PostFireBehavior::Unbind.\nSubsequent broadcasts are ignored until reset re-arms the listener.\n\n===== One-Shot Status =====\nStatus: {Status}\nPings Received: {S.PingCount}\n";
            return S.Title == "ONE-SHOT LISTENER" && S.Body == Body &&
                S.Instructions == "panel [3] One-shot\npanel [R] Reset every station";
        }
        if (S.StationIndex == 3)
        {
            auto Status = S.IsBound ? "BOUND" : "UNBOUND";
            auto Body = f"Runtime bind and unbind of message listeners via console commands.\nTests UnbindFrom_OnBroadcast and re-binding at runtime.\n\n===== Bind Status =====\nPing Listener: {Status}\nPings Received: {S.PingCount}\n\n===== Bind History =====\nBind Count: {S.BindCount}\nUnbind Count: {S.UnbindCount}\n";
            return S.Title == "DYNAMIC BIND / UNBIND" && S.Body == Body &&
                S.Instructions == "panel [B] Flip the dynamic bind\npanel [4] Ping to dynamic\npanel [R] Reset every station";
        }
        if (S.StationIndex == 4)
        {
            auto Body = f"Single entity listening to three different message types.\nPing, Pong, and Alert messages tracked independently.\n\n===== Ping =====\nCount: {S.PingCount}  Sender: {S.LastPingSender}\n\n===== Pong =====\nCount: {S.PongCount}  Responder: {S.LastPongResponder}\n\n===== Alert =====\nCount: {S.AlertCount}  Last: {S.LastAlertText} (P{S.LastAlertPriority})\n";
            return S.Title == "MULTIPLE MESSAGE TYPES" && S.Body == Body &&
                S.Instructions == "panel [7] All types at once\npanel [5] Pong\npanel [6] Alert (1 / 5 / 9)\npanel [R] Reset every station";
        }
        return false;
    }

    private bool Request_TryBuildSample(int32 InIndex, FCkMessagingGym_StationSample& Out)
    {
        Out = FCkMessagingGym_StationSample();
        if (InIndex < 0 || InIndex >= 5 || !Get_HasExactChild(InIndex)) { return false; }
        Out.StationIndex = InIndex;
        Out.StationTag = CkMessagingGym::Get_StationTag(InIndex);
        Out.ChildTag = CkMessagingGym::Get_ChildTag(InIndex);
        auto Base = UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChildren[InIndex]);
        if (InIndex == 0)
        {
            auto S = Cast<UCk_EntityScript_MessagingGym_Basic>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            Out.InitialTransform = S.InitialTransform;
            Out.PingCount = S.PingCount;
            Out.LastSender = S.LastSender;
            Out.LastSequence = S.LastSequence;
            Out.Title = "BASIC BROADCAST & LISTEN";
            Out.Body = f"Demonstrates basic message broadcast and listener binding.\nTests payload extraction with sender and sequence data.\n\n===== Message Stats =====\nPings Received: {Out.PingCount}\nLast Sender: {Out.LastSender}\nLast Sequence: {Out.LastSequence}\n";
            Out.Instructions = "panel [1] Ping\npanel [R] Reset every station";
        }
        if (InIndex == 1)
        {
            auto S = Cast<UCk_EntityScript_MessagingGym_MultiListener>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            Out.InitialTransform = S.InitialTransform;
            Out.ListenerA_Count = S.ListenerA_Count;
            Out.ListenerB_Count = S.ListenerB_Count;
            Out.ListenerC_Count = S.ListenerC_Count;
            auto Total = Out.ListenerA_Count + Out.ListenerB_Count + Out.ListenerC_Count;
            Out.Title = "MULTI-LISTENER FAN-OUT";
            Out.Body = f"Three delegates bound to the same message type on one entity.\nA single broadcast fires all three listeners simultaneously.\n\n===== Listener Counts =====\nListener A: {Out.ListenerA_Count}\nListener B: {Out.ListenerB_Count}\nListener C: {Out.ListenerC_Count}\nTotal: {Total}\n";
            Out.Instructions = "panel [2] Fan-out ping\npanel [R] Reset every station";
        }
        if (InIndex == 2)
        {
            auto S = Cast<UCk_EntityScript_MessagingGym_OneShot>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            Out.InitialTransform = S.InitialTransform;
            Out.PingCount = S.PingCount;
            Out.IsListening = S.IsListening;
            auto Status = Out.IsListening ? "LISTENING" : "NOT LISTENING";
            Out.Title = "ONE-SHOT LISTENER";
            Out.Body = f"Listener auto-unbinds after first fire using PostFireBehavior::Unbind.\nSubsequent broadcasts are ignored until reset re-arms the listener.\n\n===== One-Shot Status =====\nStatus: {Status}\nPings Received: {Out.PingCount}\n";
            Out.Instructions = "panel [3] One-shot\npanel [R] Reset every station";
        }
        if (InIndex == 3)
        {
            auto S = Cast<UCk_EntityScript_MessagingGym_DynamicBind>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            Out.InitialTransform = S.InitialTransform;
            Out.PingCount = S.PingCount;
            Out.IsBound = S.IsBound;
            Out.BindCount = S.BindCount;
            Out.UnbindCount = S.UnbindCount;
            auto Status = Out.IsBound ? "BOUND" : "UNBOUND";
            Out.Title = "DYNAMIC BIND / UNBIND";
            Out.Body = f"Runtime bind and unbind of message listeners via console commands.\nTests UnbindFrom_OnBroadcast and re-binding at runtime.\n\n===== Bind Status =====\nPing Listener: {Status}\nPings Received: {Out.PingCount}\n\n===== Bind History =====\nBind Count: {Out.BindCount}\nUnbind Count: {Out.UnbindCount}\n";
            Out.Instructions = "panel [B] Flip the dynamic bind\npanel [4] Ping to dynamic\npanel [R] Reset every station";
        }
        if (InIndex == 4)
        {
            auto S = Cast<UCk_EntityScript_MessagingGym_MultiType>(Base);
            if (ck::Is_NOT_Valid(S)) { return false; }
            Out.InitialTransform = S.InitialTransform;
            Out.PingCount = S.PingCount;
            Out.PongCount = S.PongCount;
            Out.AlertCount = S.AlertCount;
            Out.LastPingSender = S.LastPingSender;
            Out.LastPongResponder = S.LastPongResponder;
            Out.LastAlertText = S.LastAlertText;
            Out.LastAlertPriority = S.LastAlertPriority;
            Out.Title = "MULTIPLE MESSAGE TYPES";
            Out.Body = f"Single entity listening to three different message types.\nPing, Pong, and Alert messages tracked independently.\n\n===== Ping =====\nCount: {Out.PingCount}  Sender: {Out.LastPingSender}\n\n===== Pong =====\nCount: {Out.PongCount}  Responder: {Out.LastPongResponder}\n\n===== Alert =====\nCount: {Out.AlertCount}  Last: {Out.LastAlertText} (P{Out.LastAlertPriority})\n";
            Out.Instructions = "panel [7] All types at once\npanel [5] Pong\npanel [6] Alert (1 / 5 / 9)\npanel [R] Reset every station";
        }
        return Get_FiniteTransform(Out.InitialTransform) &&
            !Out.Title.IsEmpty() && !Out.Body.IsEmpty() && !Out.Instructions.IsEmpty();
    }

    private bool Request_TryBuildSnapshot(FCkMessagingGym_Snapshot& Out,
        int32 InMessageSequence = -1)
    {
        Out = FCkMessagingGym_Snapshot();
        if (!Get_HasMessagingWorldIdentity() || ServerChildren.Num() != 5 || Snapshot.Generation <= 0 ||
            Snapshot.Sequence >= 2147483646 || Snapshot.MessageSequence < 0)
        { return false; }
        auto Candidate = FCkMessagingGym_Snapshot();
        Candidate.Generation = Snapshot.Generation;
        Candidate.Sequence = Snapshot.Sequence + 1;
        Candidate.MessageSequence = InMessageSequence < 0 ? Snapshot.MessageSequence : InMessageSequence;
        Candidate.Ready = true;
        for (int32 i = 0; i < 5; i++)
        {
            auto Sample = FCkMessagingGym_StationSample();
            if (!Request_TryBuildSample(i, Sample)) { return false; }
            Candidate.Stations.Add(Sample);
        }
        if (!Get_IsCompleteSnapshot(Candidate)) { return false; }
        Out = Candidate;
        return true;
    }

    UFUNCTION() private void OnMonitor()
    {
        if (_Ending || Snapshot.Failed) { return; }
        if (_Starting)
        {
            if (float(System::GetGameTimeInSeconds()) - _StartSeconds > 45.0)
            { Request_Fail("Messaging cohort admission timed out"); return; }
            if (!_StationPhase) { return; }
            if (!_ChildPhase)
            {
                if (_PendingStations.Num() != 5 || _StationCompletions.Num() != 5)
                { return; }
                TArray<FCk_Handle> Found;
                for (int32 i = 0; i < 5; i++)
                {
                    if (_StationCompletions[i] != 1) { return; }
                    auto Station = Get_Station(i);
                    if (!CkMessagingGym::IsLive(Station)) { return; }
                    if (Station != _PendingStations[i].Get_EntityUnderConstruction() ||
                        Found.Contains(Station))
                    { Request_Fail("Messaging station admission identity changed"); return; }
                    Found.Add(Station);
                }
                _Stations = Found;
                if (!Request_ReleaseAdmission(true)) { Request_Fail("Station admission publication rejected"); return; }
                Request_StartChildren();
                return;
            }
            if (_ChildCompletions.Num() != 5) { return; }
            for (int32 i = 0; i < 5; i++)
            { if (_ChildCompletions[i] != 1) { return; } }
            if (!Get_HasCompleteUnpublishedCohort()) { return; }
            auto Candidate = FCkMessagingGym_Snapshot();
            if (!Request_TryBuildSnapshot(Candidate))
            { Request_Fail("Messaging initial sample malformed"); return; }
            Snapshot = Candidate;
            ServerChildCount = 5;
            _Starting = false;
            ForceNetUpdate();
            return;
        }
        if (Snapshot.Ready && !Get_HasReadyServerCohort())
        { Request_Fail("Messaging shared listener prerequisite lost"); return; }
        if (Snapshot.Ready) { Request_ProcessQueuedControl(); }
    }

    private void Request_ProcessQueuedControl()
    {
        if (_Ending || _Mutating || _QueuedControls.Num() == 0 ||
            _LastActionFrame == utils_time::Get_FrameCounter()) { return; }
        auto Queued = _QueuedControls[0];
        _QueuedControls.RemoveAt(0);
        auto InOwner = Queued.Owner;
        if (!Get_HasValidControlOwner(InOwner))
        {
            if (ck::IsValid(InOwner))
            { InOwner.Request_RejectQueuedMessagingControl(Queued.RequestSerial, 2); }
            return;
        }
        if (Queued.Generation != Snapshot.Generation ||
            Queued.RequestSerial <= InOwner.LastControlSerial ||
            !InOwner.Get_HasReservedMessagingControl(Queued.RequestSerial,
                Queued.Row, Queued.Priority, Queued.Generation) ||
            !InOwner.Get_CanAcceptOwnerCommit())
        { InOwner.Request_RejectQueuedMessagingControl(Queued.RequestSerial, 3); return; }
        if (!Get_HasReadyServerCohort())
        {
            if (Snapshot.Ready) { Request_Fail("Messaging recipient lost before control"); }
            InOwner.Request_RejectQueuedMessagingControl(Queued.RequestSerial, 3);
            return;
        }
        if (Snapshot.MessageSequence >= 2147483646 || Snapshot.Sequence >= 2147483646)
        {
            InOwner.Request_RejectQueuedMessagingControl(Queued.RequestSerial, 3);
            Request_Fail("Messaging sequence exhausted"); return;
        }
        // Signal payloads are retained for their broadcast frame. Never rebind
        // a listener in the same frame as another owner's broadcast.
        _LastActionFrame = utils_time::Get_FrameCounter();
        _ExecutingControl = Queued;
        bool Applied = Request_ApplyQueuedControl(InOwner, Queued);
        _ExecutingControl = FCkMessagingGym_QueuedControl();
        _PreparedOwnerCommit = FCkMessagingGym_ControlCommit();
        if (!Applied)
        { InOwner.Request_RejectQueuedMessagingControl(Queued.RequestSerial, 4); }
    }

    bool Request_TryGetPreparedOwnerCommit(ACk_MessagingGym_PlayerController InOwner,
        int32 InSerial, FCkMessagingGym_ControlCommit& Out)
    {
        Out = FCkMessagingGym_ControlCommit();
        if (!_Mutating || !Get_HasValidControlOwner(InOwner) ||
            _ExecutingControl.Owner != InOwner || _ExecutingControl.RequestSerial != InSerial ||
            InSerial <= 0 || InSerial <= InOwner.LastControlSerial ||
            !InOwner.Get_CanAcceptOwnerCommit() ||
            !InOwner.Get_HasReservedMessagingControl(InSerial, _ExecutingControl.Row,
                _ExecutingControl.Priority, _ExecutingControl.Generation)) { return false; }
        auto Commit = _PreparedOwnerCommit;
        if (Commit.RequestSerial != InSerial || Commit.Row != _ExecutingControl.Row ||
            Commit.AlertPriority != _ExecutingControl.Priority ||
            Commit.DeliverySerial != InOwner.Get_NextDeliverySerial() ||
            Commit.Snapshot.Generation != _ExecutingControl.Generation ||
            Commit.Snapshot.Generation != Snapshot.Generation ||
            Commit.Snapshot.Sequence != Snapshot.Sequence + 1 ||
            !Get_IsCompleteSnapshot(Commit.Snapshot)) { return false; }
        Out = Commit;
        return true;
    }

    private bool Request_ApplyQueuedControl(ACk_MessagingGym_PlayerController InOwner,
        FCkMessagingGym_QueuedControl Queued)
    {
        int32 InSerial = Queued.RequestSerial;
        int32 InRow = Queued.Row;
        int32 InPriority = Queued.Priority;
        // Broadcast creates missing endpoints. Check the complete five-child endpoint
        // census immediately before the first irreversible synchronous callback.
        _Mutating = true;
        auto Basic = ServerChildren[0];
        auto FanOut = ServerChildren[1];
        auto OneShot = ServerChildren[2];
        auto Dynamic = ServerChildren[3];
        auto MultiType = ServerChildren[4];
        auto Number = Snapshot.MessageSequence;
        if (InRow == 1) { utils_messaging::Broadcast(Basic,
            FCk_Message_MessagingGym_Ping("Console", Number)); }
        if (InRow == 2) { utils_messaging::Broadcast(FanOut,
            FCk_Message_MessagingGym_Ping("FanOut", Number)); }
        if (InRow == 3) { utils_messaging::Broadcast(OneShot,
            FCk_Message_MessagingGym_Ping("OneShot", Number)); }
        if (InRow == 4) { utils_messaging::Broadcast(Dynamic,
            FCk_Message_MessagingGym_Ping("Dynamic", Number)); }
        if (InRow == 5) { utils_messaging::Broadcast(MultiType,
            FCk_Message_MessagingGym_Pong("Console", Number)); }
        if (InRow == 6) { utils_messaging::Broadcast(MultiType,
            FCk_Message_MessagingGym_Alert("Warning!", InPriority)); }
        if (InRow == 7)
        {
            utils_messaging::Broadcast(MultiType, FCk_Message_MessagingGym_Ping("AllTypes", Number));
            utils_messaging::Broadcast(MultiType, FCk_Message_MessagingGym_Pong("AllTypes", Number));
            utils_messaging::Broadcast(MultiType, FCk_Message_MessagingGym_Alert("Batch Alert", 5));
        }
        if (InRow == 9) { utils_messaging::Broadcast(Dynamic,
            FCk_Message_MessagingGym_ToggleBind()); }
        if (InRow == 10)
        {
            for (int32 i = 0; i < 5; i++)
            { auto Child = ServerChildren[i]; utils_messaging::Broadcast(Child,
                FCk_Message_MessagingGym_Reset()); }
        }
        int32 NextMessageSequence = Snapshot.MessageSequence;
        if (InRow == 10) { NextMessageSequence = 0; }
        else if (InRow == 1 || InRow == 2 || InRow == 3 || InRow == 4 ||
                 InRow == 5 || InRow == 7) { NextMessageSequence++; }
        if (!Get_HasReadyServerCohort())
        { _Mutating = false; Request_Fail("Messaging listener changed during broadcast"); return false; }
        auto Candidate = FCkMessagingGym_Snapshot();
        if (!Request_TryBuildSnapshot(Candidate, NextMessageSequence))
        { _Mutating = false; Request_Fail("Messaging post-broadcast sample malformed"); return false; }
        auto Commit = FCkMessagingGym_ControlCommit();
        Commit.DeliverySerial = InOwner.Get_NextDeliverySerial();
        if (Commit.DeliverySerial <= 0)
        { _Mutating = false; Request_Fail("Messaging owner delivery serial exhausted"); return false; }
        Commit.RequestSerial = InSerial;
        Commit.Row = InRow;
        Commit.AlertPriority = InPriority;
        Commit.Snapshot = Candidate;
        _PreparedOwnerCommit = Commit;
        if (!InOwner.Request_TryRecordCommit(this, InSerial))
        { _Mutating = false; Request_Fail("Messaging owner commit rejected"); return false; }
        Snapshot = Candidate;
        _Mutating = false;
        ForceNetUpdate();
        return true;
    }

    private void Request_RetireOwned()
    {
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        { Request_Fail("Partial station admission cannot retire in this world"); return; }
        for (auto Pending : _PendingChildren)
        { if (utils_pending_entity_script::Get_IsValid(Pending))
          { utils_pending_entity_script::Request_DestroyEntity(Pending); } }
        for (auto Child : ServerChildren)
        { if (CkMessagingGym::IsLive(Child))
          { utils_entity_lifetime::Request_DestroyEntity(Child); } }
        for (auto Pending : _PendingStations)
        { if (utils_pending_entity_script::Get_IsValid(Pending))
          { utils_pending_entity_script::Request_DestroyEntity(Pending); } }
        Request_ReleaseAdmission();
        if (CkMessagingGym::IsLive(_WorldStationOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); }
        _WorldStationOwner = FCk_Handle();
        ServerChildren.Empty();
        ServerChildCount = 0;
        _Stations.Empty();
        _PendingChildren.Empty();
        _PendingStations.Empty();
        _ChildCompletions.Empty();
        _StationCompletions.Empty();
        _QueuedControls.Empty();
        _ExecutingControl = FCkMessagingGym_QueuedControl();
        _PreparedOwnerCommit = FCkMessagingGym_ControlCommit();
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

    private void Request_Fail(FString InReason)
    {
        if (Request_HoldFailedAdmission() ||
            CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        {
            _Starting = false; _Mutating = false; Snapshot.Ready = false;
            Snapshot.Failed = true; Snapshot.FailureReason = InReason;
            ForceNetUpdate();
            return;
        }
        if (_Ending || Snapshot.Failed) { return; }
        _Starting = false;
        _Mutating = false;
        Snapshot.Ready = false;
        Snapshot.Failed = true;
        Snapshot.FailureReason = InReason;
        for (auto Queued : _QueuedControls)
        {
            if (ck::IsValid(Queued.Owner))
            { Queued.Owner.Request_RejectQueuedMessagingControl(Queued.RequestSerial, 3); }
        }
        Request_RetireOwned();
        ForceNetUpdate();
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        {
            _Ending = true; _Starting = false; Snapshot.Ready = false;
            Request_HoldFailedAdmission();
            return;
        }
        _Ending = true;
        _Starting = false;
        Request_RetireOwned();
    }
}
