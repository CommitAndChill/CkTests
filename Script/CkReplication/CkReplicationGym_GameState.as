// The scenario-A simulation belongs to the world, never to a player controller.
class ACk_ReplicationGym_GameState : ACkGym_GameState_UE
{
    UPROPERTY(Replicated)
    ACk_ReplicationGym_ReplicatedActor SharedActor;

    UPROPERTY(Replicated)
    bool SharedReady = false;

    UPROPERTY(Replicated)
    bool SharedFailed = false;

    UPROPERTY(Replicated)
    int32 ActorPresetIndex = -1;

    private int32 _ReadinessAttempts = 0;
    private bool _ReadinessTimerArmed = false;

    void Request_EnsureSharedActor()
    {
        if (!HasAuthority() || SharedFailed || !Get_HasPublishedCatalog())
        { return; }
        if (ck::IsValid(SharedActor))
        {
            if (!SharedReady)
            { Request_CheckReadiness(); }
            return;
        }

        Request_SpawnSharedActor();
    }

    bool Request_ResetSharedActor()
    {
        if (!HasAuthority() || !Get_HasPublishedCatalog())
        { return false; }

        // Withdraw the published ready lease before either destroying or replacing the actor.
        SharedReady = false;
        SharedFailed = false;
        ActorPresetIndex = -1;
        _ReadinessAttempts = 0;
        if (ck::IsValid(SharedActor))
        { SharedActor.DestroyActor(); }
        SharedActor = nullptr;
        return Request_SpawnSharedActor();
    }

    private bool Request_SpawnSharedActor()
    {
        if (!HasAuthority() || SharedFailed || ck::IsValid(SharedActor))
        { return false; }
        SharedReady = false;
        _ReadinessAttempts = 0;
        auto Anchor = CkReplicationGym::Get_ActorPanelCenter();
        SharedActor = Cast<ACk_ReplicationGym_ReplicatedActor>(SpawnActor(
            ACk_ReplicationGym_ReplicatedActor, Anchor, FRotator(0.0f, 180.0f, 0.0f)));
        if (ck::Is_NOT_Valid(SharedActor))
        {
            SharedFailed = true;
            ck::Error("[ReplicationGym] Shared actor spawn failed");
            return false;
        }
        if (!_ReadinessTimerArmed)
        { Request_CheckReadiness(); }
        return true;
    }

    UFUNCTION()
    private void Request_CheckReadiness()
    {
        if (!HasAuthority() || SharedFailed || SharedReady)
        { return; }
        if (ck::Is_NOT_Valid(SharedActor))
        {
            SharedFailed = true;
            ck::Error("[ReplicationGym] Shared actor disappeared before ECS readiness");
            return;
        }

        auto ActorTag = utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.ReplicationGym.ActorValue");
        int32 Matches = 0;
        for (auto E : utils_entity_tag::ForEach_Entity(ck::TransientEntity(), n"TAG_ReplicationGym_ReplicatedActor"))
        {
            if (utils_owning_actor::TryGet_EntityOwningActor(E) == SharedActor &&
                utils_integer_attribute::TryGet(E, ActorTag).IsValid())
            { Matches++; }
        }
        if (Matches == 1)
        {
            SharedReady = true;
            return;
        }
        _ReadinessAttempts++;
        if (_ReadinessAttempts >= 100)
        {
            SharedFailed = true;
            ck::Error(f"[ReplicationGym] Shared actor ECS readiness timed out; matches={Matches}");
            return;
        }
        if (!_ReadinessTimerArmed)
        {
            _ReadinessTimerArmed = true;
            System::SetTimer(this, n"OnReadinessTimer", 0.1f, false);
        }
    }

    UFUNCTION()
    private void OnReadinessTimer()
    {
        _ReadinessTimerArmed = false;
        Request_CheckReadiness();
    }

    bool Request_SetActorPreset(int32 InIndex)
    {
        if (!HasAuthority() || !SharedReady || SharedFailed ||
            ck::Is_NOT_Valid(SharedActor) || InIndex < 0 || InIndex >= CkReplicationGym::ActorPresetCount)
        { return false; }

        auto Tag = utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.ReplicationGym.ActorValue");
        FCk_Handle Target;
        int32 Matches = 0;
        for (auto E : utils_entity_tag::ForEach_Entity(ck::TransientEntity(), n"TAG_ReplicationGym_ReplicatedActor"))
        {
            if (utils_owning_actor::TryGet_EntityOwningActor(E) == SharedActor &&
                utils_integer_attribute::TryGet(E, Tag).IsValid())
            { Target = E; Matches++; }
        }
        if (Matches != 1)
        {
            SharedReady = false;
            SharedFailed = true;
            ck::Error(f"[ReplicationGym] Shared actor lost its unique ECS entity; matches={Matches}");
            return false;
        }
        utils_messaging::Broadcast(Target, FCk_Message_ReplicationGym_SetAttribute(
            CkReplicationGym::Get_ActorPresetValue(InIndex)));
        ActorPresetIndex = InIndex;
        return true;
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason EndPlayReason)
    {
        SharedReady = false;
        if (HasAuthority() && ck::IsValid(SharedActor))
        { SharedActor.DestroyActor(); }
        SharedActor = nullptr;
    }
}
