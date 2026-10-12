class ACk_TweenTest_GymGameState : ACkGym_GameState_UE
{
    UPROPERTY(Replicated) int32 Generation = 0;
    UPROPERTY(Replicated) bool Ready = false;
    UPROPERTY(Replicated) bool Failed = false;
    UPROPERTY() TArray<ACk_TweenTest_GymActor> TweenActors;

    private bool _Starting = false;
    private bool _Retiring = false;
    private bool _LivenessArmed = false;
    private float _Deadline = 0.0f;
    private TArray<ACk_TweenTest_GymActor> _RetiredActors;
    private TArray<FCk_Handle> _RetiredBridges;
    private TArray<FCk_Handle> _RetiredTweens;

    void Request_EnsureTweenSet()
    {
        if (!HasAuthority() || !Get_HasPublishedCatalog() || Ready || Failed || _Starting)
        { return; }
        TArray<ACk_TweenTest_GymActor> Existing;
        GetAllActorsOfClass(ACk_TweenTest_GymActor, Existing);
        if (Existing.Num() != 0)
        { Request_Fail("Unowned Tween actors exist before shared startup"); return; }
        Request_StartGeneration();
    }

    bool Request_ResetTweenSet()
    {
        if (!HasAuthority() || !Get_HasPublishedCatalog() || !Ready || Failed || _Starting ||
            !Get_IsCompleteSet())
        { return false; }
        _Starting = true;
        _Retiring = true;
        Ready = false;
        Generation++;
        ForceNetUpdate();
        _Deadline = float(System::GetGameTimeInSeconds()) + 5.0f;
        _RetiredActors = TweenActors;
        TweenActors.Empty();
        for (auto Actor : _RetiredActors)
        {
            _RetiredBridges.Add(Actor.Get_BridgeForRetirement());
            _RetiredTweens.Add(Actor.Get_TweenForRetirement());
            Actor.Request_RetireBridge();
            Actor.DestroyActor();
        }
        System::SetTimer(this, n"OnRetirementPoll", 0.05f, false);
        return true;
    }

    private void Request_StartGeneration()
    {
        if (!HasAuthority() || _Starting || Failed) { return; }
        _Starting = true;
        Ready = false;
        Generation++;
        ForceNetUpdate();
        Request_SpawnSet();
    }

    UFUNCTION() private void OnRetirementPoll()
    {
        if (!_Starting || !_Retiring || Failed) { return; }
        TArray<ACk_TweenTest_GymActor> Existing;
        GetAllActorsOfClass(ACk_TweenTest_GymActor, Existing);
        bool EcsStillAlive = false;
        for (auto Bridge : _RetiredBridges)
        { if (ck::IsValid(Bridge)) { EcsStillAlive = true; break; } }
        for (auto Tween : _RetiredTweens)
        { if (ck::IsValid(Tween)) { EcsStillAlive = true; break; } }
        if (Existing.Num() != 0 || EcsStillAlive)
        {
            if (float(System::GetGameTimeInSeconds()) >= _Deadline)
            { Request_Fail("Old Tween actors or ECS entities did not retire"); return; }
            System::SetTimer(this, n"OnRetirementPoll", 0.05f, false);
            return;
        }
        _RetiredActors.Empty();
        _RetiredBridges.Empty();
        _RetiredTweens.Empty();
        _Retiring = false;
        Request_SpawnSet();
    }

    private void Request_SpawnSet()
    {
        auto Stations = CkTweenGym::Get_Stations();
        if (Stations.Num() <= 0 || Stations.Num() != int32(ECk_TweenEasing::ECk_MAX))
        { Request_Fail("Invalid Tween easing station layout"); return; }
        _Deadline = float(System::GetGameTimeInSeconds()) + 10.0f;
        for (int32 Index = 0; Index < Stations.Num(); ++Index)
        {
            auto Actor = SpawnActor(ACk_TweenTest_GymActor, CkTweenGym::Get_ActorLocation(Index),
                FRotator(0.0f, 180.0f, 0.0f), NAME_None, true);
            if (ck::Is_NOT_Valid(Actor))
            { Request_Fail("Tween actor spawn rejected"); return; }
            Actor.Generation = Generation;
            Actor.TweenEasingMethod = ECk_TweenEasing(Index);
            TweenActors.Add(Actor);
            FinishSpawningActor(Actor);
            if (ck::Is_NOT_Valid(Actor))
            { Request_Fail("Tween actor construction failed"); return; }
        }
        System::SetTimer(this, n"OnReadinessPoll", 0.05f, false);
    }

    UFUNCTION() private void OnReadinessPoll()
    {
        if (!_Starting || _Retiring || Failed) { return; }
        if (TweenActors.Num() != int32(ECk_TweenEasing::ECk_MAX))
        { Request_Fail("Incomplete Tween actor set"); return; }
        for (int32 Index = 0; Index < TweenActors.Num(); ++Index)
        {
            auto Actor = TweenActors[Index];
            if (ck::Is_NOT_Valid(Actor) || Actor.Generation != Generation ||
                Actor.TweenEasingMethod != ECk_TweenEasing(Index))
            { Request_Fail("Tween actor disappeared or changed identity"); return; }
            if (!Actor.Get_IsPrepared())
            {
                if (float(System::GetGameTimeInSeconds()) >= _Deadline)
                { Request_Fail("Tween actor bridge readiness timed out"); return; }
                System::SetTimer(this, n"OnReadinessPoll", 0.05f, false);
                return;
            }
        }
        if (!Get_IsPreparedSet())
        { Request_Fail("Tween actor set contains an unexpected actor"); return; }
        for (auto Actor : TweenActors)
        {
            if (!Actor.Request_ActivateTween())
            { Request_Fail("Tween actor activation failed"); return; }
        }
        for (auto Actor : TweenActors)
        {
            if (!Actor.Request_PublishTween())
            { Request_Fail("Tween actor publication failed"); return; }
        }
        if (!Get_IsCompleteSet())
        { Request_Fail("Published Tween actor set is incomplete"); return; }
        _Starting = false;
        Ready = true;
        ForceNetUpdate();
        if (!_LivenessArmed)
        {
            _LivenessArmed = true;
            System::SetTimer(this, n"OnLivenessPoll", 0.25f, true);
        }
    }

    private bool Get_IsPreparedSet()
    {
        if (TweenActors.Num() != int32(ECk_TweenEasing::ECk_MAX)) { return false; }
        TArray<ACk_TweenTest_GymActor> Existing;
        GetAllActorsOfClass(ACk_TweenTest_GymActor, Existing);
        if (Existing.Num() != TweenActors.Num()) { return false; }
        for (int32 Index = 0; Index < TweenActors.Num(); ++Index)
        {
            auto Actor = TweenActors[Index];
            if (ck::Is_NOT_Valid(Actor) || !Existing.Contains(Actor) ||
                Actor.Generation != Generation ||
                Actor.TweenEasingMethod != ECk_TweenEasing(Index) || !Actor.Get_IsPrepared())
            { return false; }
        }
        return true;
    }

    private bool Get_IsCompleteSet()
    {
        if (TweenActors.Num() != int32(ECk_TweenEasing::ECk_MAX)) { return false; }
        TArray<ACk_TweenTest_GymActor> Existing;
        GetAllActorsOfClass(ACk_TweenTest_GymActor, Existing);
        if (Existing.Num() != TweenActors.Num()) { return false; }
        for (int32 Index = 0; Index < TweenActors.Num(); ++Index)
        {
            auto Actor = TweenActors[Index];
            if (ck::Is_NOT_Valid(Actor) || !Existing.Contains(Actor) ||
                Actor.Generation != Generation ||
                Actor.TweenEasingMethod != ECk_TweenEasing(Index) ||
                !Actor.Published || !Actor.Get_IsTweenReady())
            { return false; }
        }
        return true;
    }

    UFUNCTION() private void OnLivenessPoll()
    {
        if (HasAuthority() && Ready && !Get_IsCompleteSet())
        { Request_Fail("Published Tween actor set lost a member"); }
    }

    private void Request_Fail(FString InReason)
    {
        _Starting = false;
        _Retiring = false;
        Ready = false;
        Failed = true;
        ForceNetUpdate();
        for (auto Actor : TweenActors)
        {
            if (ck::IsValid(Actor)) { Actor.Request_RetireBridge(); Actor.DestroyActor(); }
        }
        for (auto Actor : _RetiredActors)
        {
            if (ck::IsValid(Actor)) { Actor.Request_RetireBridge(); Actor.DestroyActor(); }
        }
        TweenActors.Empty();
        _RetiredActors.Empty();
        _RetiredBridges.Empty();
        _RetiredTweens.Empty();
        ck::Error("[TweenGym] " + InReason);
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        _Starting = false;
        Ready = false;
        if (!HasAuthority()) { return; }
        for (auto Actor : TweenActors)
        {
            if (ck::IsValid(Actor)) { Actor.Request_RetireBridge(); Actor.DestroyActor(); }
        }
        for (auto Actor : _RetiredActors)
        {
            if (ck::IsValid(Actor)) { Actor.Request_RetireBridge(); Actor.DestroyActor(); }
        }
        TweenActors.Empty();
        _RetiredActors.Empty();
        _RetiredBridges.Empty();
        _RetiredTweens.Empty();
    }
}
