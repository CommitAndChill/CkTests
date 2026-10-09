// Real Base_PlayerController startup fixture. The second authored station fails
// its own constructor's finite-recipe guard after the two spawn requests exist.
class ACk_GymAdmissionFailureProbe_PC : ACk_Gym_Base_PlayerController
{
    private bool _EnteredOwnerCreation = false;
    private int32 _OwnerCreationCalls = 0;
    private int32 _RequiredStationReads = 0;

    UFUNCTION(BlueprintPure)
    bool Get_EnteredOwnerCreation() const { return _EnteredOwnerCreation; }

    UFUNCTION(BlueprintPure)
    int32 Get_OwnerCreationCalls() const { return _OwnerCreationCalls; }

    UFUNCTION(BlueprintPure)
    int32 Get_RequiredStationReads() const { return _RequiredStationReads; }

    FCk_Handle Get_StationLifetimeOwner() override
    {
        _EnteredOwnerCreation = true;
        ++_OwnerCreationCalls;
        return ck::TransientEntity();
    }

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        ++_RequiredStationReads;
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        if (!HasAuthority()) { return Stations; }

        FCkGym_Station_SpawnParams_Payload Good;
        Good.Tags.Add(n"Test.BasePC.Admission.Good");
        Good.Title = FText::FromString("Admission good station");
        Stations.Add(Good);

        FCkGym_Station_SpawnParams_Payload Broken;
        Broken.Tags.Add(n"Test.BasePC.Admission.Broken");
        Broken.Title = FText::FromString("Admission broken station");
        Broken.Width = -1.0f;
        Stations.Add(Broken);
        return Stations;
    }
}

class ACk_GymAdmissionFailureProbe_Mode : AGameModeBase
{
    // Connected controllers are ordinary BasePCs and create no stations. The
    // test spawns two unconnected probe PCs so their EndPlay does not close a
    // real client connection.
    default PlayerControllerClass = ACk_Gym_Base_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default GameStateClass = ACkGym_GameState_UE;

    UFUNCTION(BlueprintPure)
    FCk_Handle Get_AdmissionOwner() const
    {
        auto World = ck::TransientEntity();
        if (ck::Is_NOT_Valid(World) || !World.Has_Fragment(FCkGym_StationAdmission))
        { return FCk_Handle(); }
        return World.Get_Fragment(FCkGym_StationAdmission).ActiveOwner;
    }

    // One real world reservation, then a malformed attempt to claim the world
    // as its own station owner. Compare every raw slot field around the rejected
    // commit; the outstanding reservation remains intact until PIE teardown.
    UFUNCTION(BlueprintCallable)
    bool Request_RejectWorldOwner(FCk_Handle InWorld)
    {
        if (ck::Is_NOT_Valid(InWorld) || InWorld != ck::TransientEntity() ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                InWorld, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            CkGym_Admission::Get_IsBusy(InWorld))
        { return false; }
        int32 Ticket = CkGym_Admission::TryReserve(InWorld);
        if (Ticket <= 0 || !InWorld.Has_Fragment(FCkGym_StationAdmission))
        { return false; }
        FCkGym_StationAdmission Before = InWorld.Get_Fragment(FCkGym_StationAdmission);
        bool Accepted = CkGym_Admission::TryCommitOwner(InWorld, Ticket, InWorld);
        FCkGym_StationAdmission After = InWorld.Get_Fragment(FCkGym_StationAdmission);
        return !Accepted && Before.ActiveOwner == FCk_Handle() &&
            Before.ReservationTicket == Ticket && Before.ActiveTicket == 0 &&
            Before.NextTicket == Ticket && !Before.ClosedUnproven &&
            After.ActiveOwner == Before.ActiveOwner &&
            After.ReservationTicket == Before.ReservationTicket &&
            After.ActiveTicket == Before.ActiveTicket &&
            After.NextTicket == Before.NextTicket &&
            After.ClosedUnproven == Before.ClosedUnproven;
    }

    // Uses the ordinary station spawn path after the first controller has
    // ended. Its pending target is returned before any construction callback.
    UFUNCTION(BlueprintCallable)
    FCk_Handle Request_SpawnLateStation(FCk_Handle InOwner)
    {
        if (ck::Is_NOT_Valid(InOwner) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                InOwner, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !utils_entity_lifetime::Get_CanCreateEntity(InOwner))
        { return FCk_Handle(); }
        FCkGym_Station_SpawnParams_Payload Late;
        Late.Tags.Add(n"Test.BasePC.Admission.Late");
        Late.Title = FText::FromString("Admission late station");
        auto Pending = CkGym_Common::Request_SpawnNewStationForOwner(
            Late, InOwner, FCk_Delegate_Request_OnCompleted());
        if (!utils_pending_entity_script::Get_IsValid(Pending)) { return FCk_Handle(); }
        return Pending.Get_EntityUnderConstruction();
    }

    UFUNCTION(BlueprintPure)
    bool Get_IsLateStationRejected(FCk_Handle InStation) const
    {
        if (ck::Is_NOT_Valid(InStation) ||
            !utils_entity_lifetime::Has_LifetimeOwner(InStation) ||
            utils_entity_lifetime::Get_LifetimeOwner(InStation) != Get_AdmissionOwner())
        { return false; }
        auto Script = Cast<UCk_EntityScript_GymStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(InStation));
        if (ck::Is_NOT_Valid(Script) ||
            UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(Script) != InStation)
        { return false; }
        FCkGymStation_BaselineCreatorLedger Ledger;
        if (!Script.TryGet_BaselineCreatorLedger(Ledger) ||
            Ledger.Station != InStation || !Ledger.Started || !Ledger.Broken ||
            !Ledger.PresentationClosed || Ledger.Handles.Num() != 28)
        { return false; }
        for (auto H : Ledger.Handles)
        { if (H != FCk_Handle()) { return false; } }
        return true;
    }
}
