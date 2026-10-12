// Language=angelscript

//============================================================================
// TWO-PLAYER NET GYM - CADENCE DIRECTOR
//============================================================================
// Server-only. Spawned once by the GameMode on authority (the GameMode is
// server-only, so no extra authority gate is needed). Holds the single shared
// clock as a local Beat counter advanced by a BeatSeconds heartbeat timer, and
// orchestrates the interleaved auto-cadence across both pawns:
//
//   Beat % 10 == 5 -> slot-0 pawn advances state
//   Beat % 10 == 0 -> slot-1 pawn advances state
//   Beat % 10 == 2 -> slot-0 pawn takes damage
//   Beat % 10 == 7 -> slot-1 pawn takes damage
//
// Why a server-only orchestrator rather than each pawn binding to a replicated
// beat: a transient entity has no owning actor, so its attributes don't
// replicate - there is no client-visible shared clock to bind to. Instead the
// single server clock drives everything and respects each authority:
//   - State is owning-client-authoritative, so the director triggers the owning
//     client via a Client RPC on the pawn (Client_AdvanceState), which issues
//     Request_Transition locally.
//   - Damage is server-authoritative, so the director applies it directly by
//     broadcasting the Damage message on the pawn entity (the pawn entity-script's
//     server-side OnDamage handler runs Request_Override).
//
// Found from anywhere via the CkNetGym::DirectorTag EntityTag; pawns are located
// via CkNetGym::PlayerPawnTag (added in the pawn entity-script's Construct). The
// server assigns replicated numeric slots to possessed player pawns. This also
// works on a dedicated server, where neither pawn is locally controlled.
//============================================================================

class UCk_NetGym_TwoPlayer_Director : UCk_GenericEntityScript_UE
{
    // Server-only orchestrator - never replicated (it has no owning actor anyway).
    default _Replication = ECk_Replication::DoesNotReplicate;

    private int _Beat = 0;
    private ACk_NetGym_TwoPlayer_Pawn _Slot0Pawn;
    private ACk_NetGym_TwoPlayer_Pawn _Slot1Pawn;
    private bool _RosterWasReady = false;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        utils_entity_tag::Add(InHandle, CkNetGym::DirectorTag);
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto BeatTimerParams = FCk_Timer_Spec(FCk_Time(CkNetGym::BeatSeconds));
        BeatTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        auto BeatTimer = utils_timer::Add(InHandle, BeatTimerParams);
        BeatTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnBeat"));
    }

    UFUNCTION()
    private void OnBeat(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto SelfEntity = ck::ToEntity(this);
        auto TaggedEntities = utils_entity_tag::ForEach_Entity(SelfEntity, CkNetGym::PlayerPawnTag);
        TArray<ACk_NetGym_TwoPlayer_Pawn> Participants;
        TArray<FCk_Handle> ParticipantEntities;

        // Count controllers independently of tagged entities so a joining player
        // cannot be hidden during the gap before its pawn entity is constructed.
        auto Controllers = utils_actor::Get_AllActorsOfClass(APlayerController);
        int ControllerCount = 0;
        for (auto ControllerActor : Controllers)
        {
            if (ck::IsValid(ControllerActor))
            { ControllerCount += 1; }
        }

        for (auto PawnEntity : TaggedEntities)
        {
            if (ck::Is_NOT_Valid(PawnEntity))
            { continue; }

            auto Pawn = Cast<ACk_NetGym_TwoPlayer_Pawn>(
                utils_owning_actor::TryGet_EntityOwningActor(PawnEntity));
            if (!ck::IsValid(Pawn))
            { continue; }

            // A tagged entity may outlive possession. Only player-controlled pawns
            // participate, and duplicate tags must not manufacture a second player.
            auto PlayerController = Cast<APlayerController>(Pawn.GetController());
            if (!ck::IsValid(PlayerController))
            {
                Pawn.PlayerSlot = CkNetGym::PlayerSlot_Unassigned;
                Pawn.RosterReady = false;
                continue;
            }

            if (Participants.Contains(Pawn))
            { continue; }

            Participants.Add(Pawn);
            ParticipantEntities.Add(PawnEntity);
        }

        if (ck::IsValid(_Slot0Pawn) && !Participants.Contains(_Slot0Pawn))
        {
            _Slot0Pawn.PlayerSlot = CkNetGym::PlayerSlot_Unassigned;
            _Slot0Pawn.RosterReady = false;
            _Slot0Pawn = nullptr;
        }
        if (ck::IsValid(_Slot1Pawn) && !Participants.Contains(_Slot1Pawn))
        {
            _Slot1Pawn.PlayerSlot = CkNetGym::PlayerSlot_Unassigned;
            _Slot1Pawn.RosterReady = false;
            _Slot1Pawn = nullptr;
        }
        if (!ck::IsValid(_Slot0Pawn))
        { _Slot0Pawn = nullptr; }
        if (!ck::IsValid(_Slot1Pawn))
        { _Slot1Pawn = nullptr; }

        // A third participant is not silently ignored. Keep established slots
        // reserved, but make every pawn wait until the roster is exactly two.
        if (ControllerCount != CkNetGym::RequiredPlayerCount ||
            Participants.Num() != CkNetGym::RequiredPlayerCount)
        {
            for (auto Pawn : Participants)
            {
                Pawn.RosterReady = false;
                if (Pawn != _Slot0Pawn && Pawn != _Slot1Pawn)
                { Pawn.PlayerSlot = CkNetGym::PlayerSlot_Unassigned; }
            }
            _RosterWasReady = false;
            _Beat = 0;
            return;
        }

        for (auto Pawn : Participants)
        {
            if (Pawn == _Slot0Pawn || Pawn == _Slot1Pawn)
            { continue; }

            if (!ck::IsValid(_Slot0Pawn))
            { _Slot0Pawn = Pawn; }
            else if (!ck::IsValid(_Slot1Pawn))
            { _Slot1Pawn = Pawn; }
        }

        if (!ck::IsValid(_Slot0Pawn) || !ck::IsValid(_Slot1Pawn) || _Slot0Pawn == _Slot1Pawn ||
            !Participants.Contains(_Slot0Pawn) || !Participants.Contains(_Slot1Pawn))
        {
            for (auto Pawn : Participants)
            { Pawn.RosterReady = false; }
            _RosterWasReady = false;
            _Beat = 0;
            return;
        }

        _Slot0Pawn.PlayerSlot = CkNetGym::PlayerSlot_0;
        _Slot1Pawn.PlayerSlot = CkNetGym::PlayerSlot_1;
        _Slot0Pawn.RosterReady = true;
        _Slot1Pawn.RosterReady = true;
        if (!_RosterWasReady)
        { _Beat = 0; }
        _RosterWasReady = true;

        _Beat += 1;
        auto Phase = _Beat % CkNetGym::BeatPeriod;
        for (int Index = 0; Index < Participants.Num(); ++Index)
        {
            auto Pawn = Participants[Index];
            auto PawnEntity = ParticipantEntities[Index];
            int StatePhase = -1;
            int DamagePhase = -1;
            if (Pawn == _Slot0Pawn && Pawn.PlayerSlot == CkNetGym::PlayerSlot_0)
            {
                StatePhase = CkNetGym::BeatPhase_State_Slot0;
                DamagePhase = CkNetGym::BeatPhase_Damage_Slot0;
            }
            else if (Pawn == _Slot1Pawn && Pawn.PlayerSlot == CkNetGym::PlayerSlot_1)
            {
                StatePhase = CkNetGym::BeatPhase_State_Slot1;
                DamagePhase = CkNetGym::BeatPhase_Damage_Slot1;
            }
            else
            { return; }

            if (Phase == StatePhase)
            { Pawn.Client_AdvanceState(); }
            if (Phase == DamagePhase)
            {
                utils_messaging::Broadcast(PawnEntity, FCk_Message_NetGym_Damage(CkNetGym::DefaultDamage));
            }
        }
    }
}
