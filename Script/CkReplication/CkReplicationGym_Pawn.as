//============================================================================
// REPLICATION GYM - PAWN (Scenario B driver)
//============================================================================
// A SECOND replicated WithActor script uses the world transient lifetime owner.
// Authority creates it after its primary bridge; client pawns register only the
// matching pending request, mirroring the historical attribute-container case.
//============================================================================

class ACk_ReplicationGym_Pawn : ACk_Gym_Base_Pawn
{
    UPROPERTY()
    bool ExtraEntityReady = false;

    private bool _ExtraSpawnRequested = false;
    private bool _EndingPlay = false;
    private FCk_Handle_PendingEntityScript _ClientExtraPending;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Super::BeginPlay();
        if (!HasAuthority())
        { Request_ExtraEntity(); }
    }

    void Request_OnPawnReady() override
    {
        if (HasAuthority())
        { Request_ExtraEntity(); }
    }

    // Replicated ECS construction can precede actor BeginPlay. Register the
    // exact client match before the secondary script completes construction.
    bool Request_RegisterExtraReplica(FCk_Handle InReplica)
    {
        if (HasAuthority() || _EndingPlay || ck::Is_NOT_Valid(InReplica) ||
            utils_owning_actor::TryGet_EntityOwningActor(InReplica) != this)
        { return false; }
        auto Script = utils_entity_script::TryGet_EntityScript(InReplica);
        if (ck::Is_NOT_Valid(Script) || Script.GetClass() != UCk_ReplicationGym_PawnExtra_EntityScript)
        { return false; }
        Request_ExtraEntity();
        return _ExtraSpawnRequested;
    }

    private void Request_ExtraEntity()
    {
        if (_EndingPlay || _ExtraSpawnRequested)
        { return; }

        auto SpawnParams = FCk_EntityScript_WithActor_SpawnParams();
        SpawnParams._OwningActor = this;
        auto PendingEntity = utils_entity_script::Request_SpawnEntity(
            ck::TransientEntity(),
            UCk_ReplicationGym_PawnExtra_EntityScript,
            SpawnParams);
        if (!utils_pending_entity_script::Get_IsValid(PendingEntity))
        {
            ck::Error("[ReplicationGym] Pawn extra entity request rejected");
            return;
        }
        _ExtraSpawnRequested = true;
        if (!HasAuthority())
        { _ClientExtraPending = PendingEntity; }
        utils_pending_entity_script::Promise_OnConstructed(
            PendingEntity, FCk_Delegate_EntityScript_Constructed(this, n"OnPawnExtraConstructed"));
    }

    UFUNCTION()
    private void OnPawnExtraConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        if (_EndingPlay)
        { return; }
        auto Entity = FCk_Handle(InEntityScriptHandle);
        if (ck::Is_NOT_Valid(Entity) ||
            utils_owning_actor::TryGet_EntityOwningActor(Entity) != this)
        {
            ck::Error("[ReplicationGym] Pawn extra entity receipt has no matching owning actor");
            return;
        }
        _ClientExtraPending = FCk_Handle_PendingEntityScript();
        ExtraEntityReady = true;
        ck::Trace("[ReplicationGym] Pawn extra entity script constructed");
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason EndPlayReason)
    {
        _EndingPlay = true;
        ExtraEntityReady = false;
        if (!HasAuthority() && utils_pending_entity_script::Get_IsValid(_ClientExtraPending))
        { utils_pending_entity_script::Request_DestroyEntity(_ClientExtraPending); }
        _ClientExtraPending = FCk_Handle_PendingEntityScript();
        Super::EndPlay(EndPlayReason);
    }
}
