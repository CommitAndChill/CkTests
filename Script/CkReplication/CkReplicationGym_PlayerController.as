//============================================================================
// REPLICATION GYM - PLAYER CONTROLLER
//============================================================================

class ACk_ReplicationGym_PlayerController : ACk_Gym_Base_PlayerController
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        if (!IsLocalController())
        { return Stations; }

        Stations.Add(CkReplicationGym::Get_ActorStation());

        // Scenario B - replicated pawn + second WithActor entity script.
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(n"Gym.Replication.PawnTransient");
            Station.Title = FText::FromString("REPLICATION — PAWN TRANSIENT-ENTITY CHILD");
            Station.AutoSize = true;
            Station.Transform = FTransform(FRotator(0.0f, 180.0f, 0.0f),
                FVector(500.0f, 400.0f, 0.0f), FVector(1.0f, 1.0f, 1.0f));
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Scenario B: the replicated pawn attaches a SECOND WithActor entity"));
            Description.Add(FText::FromString("script with ck::TransientEntity() as lifetime, and that script adds"));
            Description.Add(FText::FromString("a replicated Integer Attribute in DoConstructWithActor."));
            Description.Add(FText::FromString("Mirrors the BB_PlayerCharacter regression after re-ordering."));
            Station.Description = Description;
            Stations.Add(Station);
        }

        return Stations;
    }

    void Request_StartGym() override
    {
        if (IsLocalController())
        { ck::Trace("[ReplicationGym] Local station reconciliation complete"); }
    }

    void Request_RestartGym() override
    {
        if (IsLocalController())
        { Server_RequestReplicationAction(CkReplicationGym::Action_ResetActor, -1); }
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!IsLocalController())
        { return; }
        Request_ProjectLocalStations();
    }

    private void Request_ProjectLocalStations()
    {
        auto State = Get_ReplicationState();
        auto ActorStations = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), n"Gym.Replication.ReplicatedActor");
        auto ActorStation = ActorStations.Num() == 1 ? ActorStations[0] : FCk_Handle();
        if (ck::IsValid(ActorStation))
        {
            FString Body = "Server-owned actor, replicated ECS integer attribute\n";
            Body += ck::IsValid(State) && State.SharedReady ? "Shared actor READY\n" : "Shared actor WAITING/FAILED\n";
            if (ck::IsValid(State) && ck::IsValid(State.SharedActor))
            {
                auto Tag = utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.ReplicationGym.ActorValue");
                int32 Matches = 0;
                int32 Value = 0;
                for (auto E : utils_entity_tag::ForEach_Entity(ck::TransientEntity(), n"TAG_ReplicationGym_ReplicatedActor"))
                {
                    if (utils_owning_actor::TryGet_EntityOwningActor(E) != State.SharedActor)
                    { continue; }
                    auto Attribute = utils_integer_attribute::TryGet(E, Tag);
                    if (Attribute.IsValid())
                    {
                        Matches++;
                        Value = Attribute.Get_FinalValue(ECk_MinMaxCurrent::Current);
                    }
                }
                Body += Matches == 1 ? f"Local replica value: {Value}\n" : f"Local replica pending; matches={Matches}\n";
            }
            Body += "[1] Cycle shared preset  [G] Respawn shared actor  [J] Dump";
            auto& Fragment = ActorStation.AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
            Fragment.Title = FText::FromString("REPLICATION - SHARED ACTOR");
            Fragment.Description = FText::FromString(Body);
        }

        auto PawnStations = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), n"Gym.Replication.PawnTransient");
        auto PawnStation = PawnStations.Num() == 1 ? PawnStations[0] : FCk_Handle();
        auto Pawn = Cast<ACk_ReplicationGym_Pawn>(GetControlledPawn());
        if (ck::IsValid(PawnStation))
        {
            FString Body = "Owned pawn, second replicated WithActor entity\n";
            int32 Matches = 0;
            int32 Value = 0;
            if (ck::IsValid(Pawn) && Pawn.GetController() == this)
            {
                auto Tag = utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.ReplicationGym.PawnValue");
                for (auto E : utils_entity_tag::ForEach_Entity(ck::TransientEntity(), n"TAG_ReplicationGym_PawnExtra"))
                {
                    if (utils_owning_actor::TryGet_EntityOwningActor(E) != Pawn)
                    { continue; }
                    auto Attribute = utils_integer_attribute::TryGet(E, Tag);
                    if (Attribute.IsValid())
                    {
                        Matches++;
                        Value = Attribute.Get_FinalValue(ECk_MinMaxCurrent::Current);
                    }
                }
            }
            Body += Matches == 1 ? f"Own replica value: {Value}\n" : f"Own replica pending; matches={Matches}\n";
            Body += "[2] Cycle owned pawn preset  [J] Dump";
            auto& Fragment = PawnStation.AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
            Fragment.Title = FText::FromString("REPLICATION - OWNED PAWN");
            Fragment.Description = FText::FromString(Body);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------------
    // CONTROL PANEL (Script/Common/CkGym_ControlPanel.as)
    //
    // Both value setters are preset RINGS rather than single-value actions: each attribute is Min-clamped
    // at 0 with no maximum (see the two entity scripts), so a ring that steps through the placard value,
    // zero, a NEGATIVE value and a large one exercises the clamp the replicated attribute has to survive
    // - and the clamped result is what has to arrive on the other side.
    //--------------------------------------------------------------------------------------------------------------------------

    // -1 means nothing has been sent yet, so the row shows the ring instead of a value.
    UPROPERTY(Replicated)
    int32 PawnPresetIndex = -1;

    UPROPERTY(Replicated)
    int32 LastActionResult = CkReplicationGym::Result_None;

    UPROPERTY(Replicated)
    int32 LastActionSerial = 0;

    FString Get_ControlPanelTitle() override
    {
        return "REPLICATION";
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();

        Rows.Add(CkGym_Control::Cycle(EKeys::One, "1", "Actor value", DoGet_ActorPresetLabel()));
        Rows.Add(CkGym_Control::Cycle(EKeys::Two, "2", "Pawn value",  DoGet_PawnPresetLabel()));
        Rows.Add(CkGym_Control::Action(EKeys::G, "G", "Respawn shared actor"));
        Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Dump replication state"));
        Rows.Add(CkGym_Control::Status("Last action", CkReplicationGym::Get_ActionResultLabel(LastActionResult),
            LastActionResult > CkReplicationGym::Result_Accepted));

        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!IsLocalController() || InRowIndex < 0 || InRowIndex > 3)
        { return; }
        Server_RequestReplicationAction(InRowIndex, -1);
    }

    // Both the panel and the console reach this same owning-controller transport.
    UFUNCTION(Server)
    void Server_RequestReplicationAction(int32 InAction, int32 InPresetIndex)
    {
        if (!HasAuthority())
        { return; }
        LastActionSerial++;
        if (InAction < CkReplicationGym::Action_CycleActor ||
            InAction > CkReplicationGym::Action_SetActorPreset)
        {
            LastActionResult = CkReplicationGym::Result_InvalidAction;
            return;
        }
        if (InAction != CkReplicationGym::Action_SetActorPreset && InPresetIndex != -1)
        {
            LastActionResult = CkReplicationGym::Result_InvalidPreset;
            return;
        }

        auto State = Get_ReplicationState();
        TArray<ACk_ReplicationGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_ReplicationGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto CurrentGym = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(State) || !State.Get_HasPublishedCatalog() ||
            Modes.Num() != 1 ||
            CurrentGym < 0 || CurrentGym >= Registry.Num() ||
            Registry[CurrentGym].DisplayName != "Replication")
        {
            LastActionResult = CkReplicationGym::Result_InvalidGym;
            return;
        }

        auto Pawn = Cast<ACk_ReplicationGym_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this)
        {
            LastActionResult = CkReplicationGym::Result_InvalidOwner;
            return;
        }

        if (InAction == CkReplicationGym::Action_ResetActor)
        {
            LastActionResult = State.Request_ResetSharedActor() ?
                CkReplicationGym::Result_Accepted : CkReplicationGym::Result_TargetFailed;
            return;
        }
        if (!State.SharedReady || State.SharedFailed)
        {
            LastActionResult = CkReplicationGym::Result_NotReady;
            return;
        }

        if (InAction == CkReplicationGym::Action_CycleActor)
        {
            LastActionResult = State.Request_SetActorPreset(
                (State.ActorPresetIndex + 1) % CkReplicationGym::ActorPresetCount) ?
                CkReplicationGym::Result_Accepted : CkReplicationGym::Result_TargetFailed;
        }
        else if (InAction == CkReplicationGym::Action_SetActorPreset)
        {
            if (InPresetIndex < 0 || InPresetIndex >= CkReplicationGym::ActorPresetCount)
            {
                LastActionResult = CkReplicationGym::Result_InvalidPreset;
                return;
            }
            LastActionResult = State.Request_SetActorPreset(InPresetIndex) ?
                CkReplicationGym::Result_Accepted : CkReplicationGym::Result_TargetFailed;
        }
        else if (InAction == CkReplicationGym::Action_CyclePawn)
        {
            LastActionResult = Request_SetOwnedPawnPreset(Pawn,
                (PawnPresetIndex + 1) % CkReplicationGym::PawnPresetCount) ?
                CkReplicationGym::Result_Accepted : CkReplicationGym::Result_TargetFailed;
        }
        else if (InAction == CkReplicationGym::Action_Dump)
        {
            Request_DumpReplicationState();
            LastActionResult = CkReplicationGym::Result_Accepted;
        }
    }

    private ACk_ReplicationGym_GameState Get_ReplicationState() const
    {
        TArray<ACk_ReplicationGym_GameState> States;
        GetAllActorsOfClass(ACk_ReplicationGym_GameState, States);
        if (States.Num() != 1)
        { return nullptr; }
        return States[0];
    }

    private bool Request_SetOwnedPawnPreset(ACk_ReplicationGym_Pawn InPawn, int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= CkReplicationGym::PawnPresetCount ||
            ck::Is_NOT_Valid(InPawn) || InPawn.GetController() != this)
        { return false; }
        auto Tag = utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.ReplicationGym.PawnValue");
        FCk_Handle Target;
        int32 Matches = 0;
        for (auto E : utils_entity_tag::ForEach_Entity(ck::TransientEntity(), n"TAG_ReplicationGym_PawnExtra"))
        {
            if (utils_owning_actor::TryGet_EntityOwningActor(E) == InPawn &&
                utils_integer_attribute::TryGet(E, Tag).IsValid())
            { Target = E; Matches++; }
        }
        if (Matches != 1)
        {
            ck::Error(f"[ReplicationGym] Owned pawn ECS entity not uniquely ready; matches={Matches}");
            return false;
        }
        utils_messaging::Broadcast(Target, FCk_Message_ReplicationGym_SetAttribute(
            CkReplicationGym::Get_PawnPresetValue(InIndex)));
        PawnPresetIndex = InIndex;
        return true;
    }

    private FString DoGet_ActorPresetLabel() const
    {
        auto State = Get_ReplicationState();
        if (ck::Is_NOT_Valid(State) || State.SharedFailed)
        { return "FAILED"; }
        if (!State.SharedReady)
        { return "waiting for shared actor"; }
        if (State.ActorPresetIndex < 0 || State.ActorPresetIndex >= CkReplicationGym::ActorPresetCount)
        { return "(100 / 0 / -50 / 9999)"; }
        return f"server preset {CkReplicationGym::Get_ActorPresetValue(State.ActorPresetIndex)}";
    }

    private FString DoGet_PawnPresetLabel() const
    {
        if (PawnPresetIndex < 0 || PawnPresetIndex >= CkReplicationGym::PawnPresetCount)
        { return "(50 / 0 / -25 / 500)"; }
        return f"own pawn preset {CkReplicationGym::Get_PawnPresetValue(PawnPresetIndex)}";
    }

    private void Request_DumpReplicationState()
    {
        auto State = Get_ReplicationState();
        auto Pawn = Cast<ACk_ReplicationGym_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(State) || ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this)
        { return; }
        auto ActorTag = utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.ReplicationGym.ActorValue");
        auto PawnTag  = utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.ReplicationGym.PawnValue");
        ck::Trace("=== ReplicationGym - DumpRep ===");
        for (auto E : utils_entity_tag::ForEach_Entity(ck::TransientEntity(), n"TAG_ReplicationGym_ReplicatedActor"))
        {
            if (utils_owning_actor::TryGet_EntityOwningActor(E) != State.SharedActor)
            { continue; }
            auto A = utils_integer_attribute::TryGet(E, ActorTag);
            ck::Trace(f"  ReplicatedActor: present={A.IsValid()} value={(A.IsValid() ? A.Get_FinalValue(ECk_MinMaxCurrent::Current) : 0)}");
        }
        for (auto E : utils_entity_tag::ForEach_Entity(ck::TransientEntity(), n"TAG_ReplicationGym_PawnExtra"))
        {
            if (utils_owning_actor::TryGet_EntityOwningActor(E) != Pawn)
            { continue; }
            auto A = utils_integer_attribute::TryGet(E, PawnTag);
            ck::Trace(f"  PawnExtra:       present={A.IsValid()} value={(A.IsValid() ? A.Get_FinalValue(ECk_MinMaxCurrent::Current) : 0)}");
        }
        ck::Trace("================================");
    }
}
