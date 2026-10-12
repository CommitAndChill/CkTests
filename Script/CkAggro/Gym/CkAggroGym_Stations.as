// Language=angelscript
//
//============================================================================
// CkAggro Gym - station EntityScripts
//
// The server owns the Aggro simulation; every world projects one coherent snapshot.
//
//   1. Chase        - bursts of threat rotate the active target; the guard eases
//                     toward whoever it is aggro'd on. Dummies redden with threat.
//   2. Perception   - a vision cone gates decay: a dummy inside the cone is
//                     perceived (keeps its threat); outside, threat decays fast.
//   3. Stress       - many guards x many dummies; the pipeline holds, a sampled
//                     guard reports its selection.
//============================================================================

USTRUCT()
struct FCkAggroGym_Station_SpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;

    UPROPERTY()
    FString StationTitle;

    UPROPERTY()
    FString StationDescription;

    UPROPERTY()
    int32 Generation = 0;
}

namespace CkAggroGym
{
    ACk_AggroGym_GameState Get_State()
    {
        TArray<ACk_AggroGym_GameState> States;
        GetAllActorsOfClass(ACk_AggroGym_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0])) { return nullptr; }
        return States[0];
    }

    bool IsValidStation(FCk_Handle InChild, FName InTag)
    {
        if (ck::Is_NOT_Valid(InChild)) { return false; }
        auto Parent = utils_entity_lifetime::Get_LifetimeOwner(InChild);
        return ck::IsValid(Parent) && Parent == FindStation(InTag);
    }

    bool IsLiveEntity(FCk_Handle InHandle)
    {
        if (ck::Is_NOT_Valid(InHandle)) { return false; }
        return !utils_entity_lifetime::Get_IsPendingDestroy(
            InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    void DrawChase(FCk_Handle InChild, FString InTitle, FString InDescription, FCkAggroGym_Snapshot InSnapshot)
    {
        if (InSnapshot.ChaseDummies.Num() != 3 || InSnapshot.ChaseThreats.Num() != 3) { return; }
        float MaxThreat = 0.001;
        for (auto Threat : InSnapshot.ChaseThreats) { MaxThreat = Math::Max(MaxThreat, Threat); }
        for (int32 i = 0; i < 3; i++)
        {
            auto Frac = Math::Clamp(InSnapshot.ChaseThreats[i] / MaxThreat, 0.0, 1.0);
            utils_debug_draw::DrawDebugSphere(InSnapshot.ChaseDummies[i], 50.0, 12,
                FLinearColor(0.3 + 0.7 * Frac, 0.25, 0.25), 0.0, 0.2);
        }
        utils_debug_draw::DrawDebugSphere(InSnapshot.ChaseGuard, 40.0, 12, FLinearColor(0.2, 0.6, 1.0), 0.0, 0.2);
        if (InSnapshot.ChaseActiveIndex >= 0 && InSnapshot.ChaseActiveIndex < 3)
        { utils_debug_draw::DrawDebugLine(InSnapshot.ChaseGuard, InSnapshot.ChaseDummies[InSnapshot.ChaseActiveIndex],
            FLinearColor(1.0, 0.9, 0.2), 0.0, 3.0); }
        auto Active = InSnapshot.ChaseActiveIndex >= 0 ? "yes" : "(none)";
        auto Body = f"Tracked          {InSnapshot.ChaseTrackedCount}\n"
            + f"Active target    {Active}\n\n"
            + "Blue  = guard (chases its target)\n"
            + "Red   = attacker (brighter = more threat)\n"
            + "Yellow line = current aggro";
        CkGym_Common::Update_StationDisplay(InChild, InTitle, Body, InDescription);
    }

    void DrawPerception(FCk_Handle InChild, FString InTitle, FString InDescription, FCkAggroGym_Snapshot InSnapshot)
    {
        auto Left = InSnapshot.PerceptionGuard + FVector(Math::Cos(0.6109) * 900.0, Math::Sin(0.6109) * 900.0, 0.0);
        auto Right = InSnapshot.PerceptionGuard + FVector(Math::Cos(-0.6109) * 900.0, Math::Sin(-0.6109) * 900.0, 0.0);
        utils_debug_draw::DrawDebugLine(InSnapshot.PerceptionGuard, Left, FLinearColor(0.5, 0.5, 0.5), 0.0, 1.0);
        utils_debug_draw::DrawDebugLine(InSnapshot.PerceptionGuard, Right, FLinearColor(0.5, 0.5, 0.5), 0.0, 1.0);
        utils_debug_draw::DrawDebugSphere(InSnapshot.PerceptionGuard, 40.0, 12, FLinearColor(0.2, 0.6, 1.0), 0.0, 0.2);
        auto Color = InSnapshot.PerceptionInCone ? FLinearColor(0.2, 1.0, 0.3) : FLinearColor(0.5, 0.35, 0.35);
        utils_debug_draw::DrawDebugSphere(InSnapshot.PerceptionOrbiter, 55.0, 12, Color, 0.0, 0.2);
        auto InSight = InSnapshot.PerceptionInCone ? "PERCEIVED" : "lost";
        auto Body = f"In sight         {InSight}\n"
            + f"Threat           {int32(InSnapshot.PerceptionThreat)}\n\n"
            + "Green orbiter = perceived (holds threat)\n"
            + "Dim orbiter   = unperceived (decays ~5x)";
        CkGym_Common::Update_StationDisplay(InChild, InTitle, Body, InDescription);
    }

    void DrawStress(FCk_Handle InChild, FString InTitle, FString InDescription, FCkAggroGym_Snapshot InSnapshot)
    {
        if (InSnapshot.StressGuards.Num() != 24) { return; }
        for (int32 i = 0; i < 24; i++)
        {
            auto Active = (InSnapshot.StressActiveMask & (1 << i)) != 0;
            auto Color = Active ? FLinearColor(0.2, 0.9, 0.3) : FLinearColor(0.5, 0.5, 0.5);
            utils_debug_draw::DrawDebugSphere(InSnapshot.StressGuards[i], 30.0, 8, Color, 0.0, 0.2);
        }
        auto Body = f"Guards           {InSnapshot.StressGuardCount}\n"
            + f"Targets / guard  {InSnapshot.StressTargetsPerGuard}\n"
            + f"Guards w/ active {InSnapshot.StressActiveCount}\n\n"
            + "Green = guard with an active target\n"
            + "Grey  = idle this frame";
        CkGym_Common::Update_StationDisplay(InChild, InTitle, Body, InDescription);
    }
}

// ====================================================================================================================
// Station 1 - Aggro + Chase
// ====================================================================================================================

class UCk_EntityScript_AggroGym_Chase_Station : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) FString StationTitle;
    UPROPERTY(ExposeOnSpawn) FString StationDescription;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    UPROPERTY() bool ProjectionConstructed = false;
    UPROPERTY() bool AuthorityConstructed = false;

    private FCk_Handle_Aggro _Guard;
    private FCk_Handle_Transform _GuardTransform;
    private TArray<FCk_Handle_Transform> _Dummies;
    private float _FeedTimer = 0.0;
    private int32 _FeedIndex = 0;
    private bool _Authority = false;
    private FCk_Handle_Timer _AuthorityTick;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow
    DoConstruct(FCk_Handle& InHandle)
    {
        auto State = CkAggroGym::Get_State();
        auto Mode = utils_net::Get_EntityNetMode(InHandle);
        if (ck::Is_NOT_Valid(State) || Generation <= 0 ||
            !CkAggroGym::IsValidStation(InHandle, n"Gym.Aggro.Chase") ||
            (Mode != ECk_Net_NetModeType::Host && Mode != ECk_Net_NetModeType::ClientAndHost &&
             Mode != ECk_Net_NetModeType::Client))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        _Authority = Mode != ECk_Net_NetModeType::Client;
        if (_Authority && (!State.HasAuthority() || State.Snapshot.Generation != Generation))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        if (!_Authority)
        {
            utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"OnTick"));
            ProjectionConstructed = true;
            return ECk_EntityScript_ConstructionFlow::Finished;
        }

        auto Center = InitialTransform.Location + FVector(0.0, 0.0, 150.0);

        auto GuardEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        if (ck::Is_NOT_Valid(GuardEntity)) { return ECk_EntityScript_ConstructionFlow::Finished; }
        GuardEntity.Set_DebugName(n"AggroGym.Chase.Guard");
        _GuardTransform = utils_transform::Add(GuardEntity, FTransform(FRotator::ZeroRotator, Center), ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(_GuardTransform)) { return ECk_EntityScript_ConstructionFlow::Finished; }
        _Guard = utils_aggro::Add(GuardEntity, FCk_Aggro_Spec());
        if (ck::Is_NOT_Valid(_Guard)) { return ECk_EntityScript_ConstructionFlow::Finished; }

        for (int32 i = 0; i < 3; i++)
        {
            auto Angle = float(i) * 2.0943951;   // 120 degrees
            auto Loc = Center + FVector(Math::Cos(Angle) * 450.0, Math::Sin(Angle) * 450.0, 0.0);

            auto Dummy = utils_entity_lifetime::Request_CreateEntity(InHandle);
            if (ck::Is_NOT_Valid(Dummy)) { return ECk_EntityScript_ConstructionFlow::Finished; }
            Dummy.Set_DebugName(n"AggroGym.Chase.Dummy");
            auto DummyTransform = utils_transform::Add(Dummy, FTransform(FRotator::ZeroRotator, Loc), ECk_Replication::DoesNotReplicate);
            if (ck::Is_NOT_Valid(DummyTransform)) { return ECk_EntityScript_ConstructionFlow::Finished; }
            _Dummies.Add(DummyTransform);
            if (ck::Is_NOT_Valid(_Guard.CreateTarget(FCk_Handle(DummyTransform))))
            { return ECk_EntityScript_ConstructionFlow::Finished; }
        }

        _AuthorityTick = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"OnTick"));
        if (ck::Is_NOT_Valid(_AuthorityTick)) { return ECk_EntityScript_ConstructionFlow::Finished; }
        AuthorityConstructed = true;
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    bool Get_HasValidAuthorityComposition()
    {
        if (!_Authority || !AuthorityConstructed || _Dummies.Num() != 3 ||
            !CkAggroGym::IsLiveEntity(FCk_Handle(_Guard)) ||
            !CkAggroGym::IsLiveEntity(FCk_Handle(_GuardTransform)) ||
            !CkAggroGym::IsLiveEntity(FCk_Handle(_AuthorityTick)))
        { return false; }
        auto Self = ck::ToEntity(this);
        auto Station = CkAggroGym::FindStation(n"Gym.Aggro.Chase");
        auto Guard = FCk_Handle(_Guard);
        if (!CkAggroGym::IsLiveEntity(Self) || !CkAggroGym::IsLiveEntity(Station) ||
            utils_entity_lifetime::Get_LifetimeOwner(Self) != Station ||
            !utils_timer::DoCast(FCk_Handle(_AuthorityTick)).IsSet() ||
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_AuthorityTick)) != Self ||
            _AuthorityTick.Get_CurrentState() != ECk_Timer_State::Running ||
            !utils_aggro::DoCast(Guard).IsSet() ||
            !utils_transform::Has(Guard) || !utils_transform::Has(FCk_Handle(_GuardTransform)) ||
            utils_entity_lifetime::Get_LifetimeOwner(Guard) != Self ||
            FCk_Handle(_GuardTransform) != Guard || _Guard.Get_NumTrackedTargets() != 3)
        { return false; }
        for (int32 i = 0; i < 3; i++)
        {
            auto Dummy = FCk_Handle(_Dummies[i]);
            if (!CkAggroGym::IsLiveEntity(Dummy) || !utils_transform::Has(Dummy) ||
                utils_entity_lifetime::Get_LifetimeOwner(Dummy) != Self)
            { return false; }
            for (int32 j = 0; j < i; j++)
            { if (Dummy == FCk_Handle(_Dummies[j])) { return false; } }
            auto Target = _Guard.TryGet_Target_ByTrackedEntity(Dummy);
            if (!CkAggroGym::IsLiveEntity(FCk_Handle(Target)) || !utils_aggro_target::DoCast(FCk_Handle(Target)).IsSet() ||
                utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(Target)) != Guard ||
                Target.Get_TrackedEntity() != Dummy)
            { return false; }
        }
        auto Active = _Guard.TryGet_ActiveTrackedEntity();
        if (ck::IsValid(Active))
        {
            if (!CkAggroGym::IsLiveEntity(Active)) { return false; }
            bool IsOwnedDummy = false;
            for (auto Dummy : _Dummies)
            { if (Active == FCk_Handle(Dummy)) { IsOwnedDummy = true; break; } }
            if (!IsOwnedDummy) { return false; }
        }
        return true;
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto State = CkAggroGym::Get_State();
        if (ck::Is_NOT_Valid(State) || State.Snapshot.Generation != Generation || State.Snapshot.Failed ||
            !CkAggroGym::IsValidStation(ck::ToEntity(this), n"Gym.Aggro.Chase")) { return; }
        if (!_Authority)
        {
            if (State.Get_HasAdmittedLocalProjection(Generation))
            { CkAggroGym::DrawChase(ck::ToEntity(this), StationTitle, StationDescription, State.Snapshot); }
            return;
        }
        if (!State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation)) { return; }
        if (!Get_HasValidAuthorityComposition())
        {
            State.Request_FailCurrentServerChild(ck::ToEntity(this), Generation, "Chase composition lost a dependent entity");
            return;
        }
        auto DtSec = float(InDeltaT.Get_Seconds());
        _FeedTimer += DtSec;

        // Every 2.5s, dump a burst of threat on the next dummy - the active target rotates around the ring.
        if (_FeedTimer >= 2.5)
        {
            _FeedTimer = 0.0;
            _FeedIndex = (_FeedIndex + 1) % _Dummies.Num();
            _Guard.Request_AddThreat(FCk_Request_Aggro_AddThreat(FCk_Handle(_Dummies[_FeedIndex]), 25.0));
        }

        auto GuardLoc      = utils_transform::Get_EntityCurrentLocation(_GuardTransform);
        auto ActiveTracked = _Guard.TryGet_ActiveTrackedEntity();

        auto Sample = FCkAggroGym_Snapshot();
        Sample.ChaseGuard = GuardLoc;
        Sample.ChaseActiveIndex = -1;
        Sample.ChaseTrackedCount = _Guard.Get_NumTrackedTargets();
        // Chase: ease the guard toward the active target.
        if (ck::IsValid(ActiveTracked))
        {
            for (int32 i = 0; i < _Dummies.Num(); i++)
            {
                if (FCk_Handle(_Dummies[i]) != ActiveTracked)
                { continue; }
                Sample.ChaseActiveIndex = i;
                auto TargetLoc = utils_transform::Get_EntityCurrentLocation(_Dummies[i]);
                auto NewLoc    = GuardLoc + (TargetLoc - GuardLoc) * 0.03;
                utils_transform::Request_SetLocation(_GuardTransform, FCk_Request_Transform_SetLocation(NewLoc));
                break;
            }
        }
        for (auto DummyTransform : _Dummies)
        {
            Sample.ChaseDummies.Add(utils_transform::Get_EntityCurrentLocation(DummyTransform));
            auto Target = _Guard.TryGet_Target_ByTrackedEntity(FCk_Handle(DummyTransform));
            Sample.ChaseThreats.Add(ck::IsValid(Target) ? Target.Get_Threat() : 0.0);
        }
        State.SubmitChase(ck::ToEntity(this), Generation, Sample);
        if (State.Snapshot.Ready)
        { CkAggroGym::DrawChase(ck::ToEntity(this), StationTitle, StationDescription, State.Snapshot); }
    }
}

// ====================================================================================================================
// Station 2 - Line-of-sight cone gates perception (and therefore decay)
// ====================================================================================================================

class UCk_EntityScript_AggroGym_Perception_Station : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) FString StationTitle;
    UPROPERTY(ExposeOnSpawn) FString StationDescription;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    UPROPERTY() bool ProjectionConstructed = false;
    UPROPERTY() bool AuthorityConstructed = false;

    private FCk_Handle_Aggro _Guard;
    private FCk_Handle_Transform _GuardTransform;
    private FVector _GuardLoc = FVector::ZeroVector;
    private FCk_Handle_Transform _OrbiterTransform;
    private FCk_Handle _Orbiter;
    private bool _WasInCone = false;
    private float _OrbitAngle = 0.0;

    // Guard faces +X; cone half-angle 35 degrees. cos(35deg) ~= 0.819.
    private float _ConeHalfCos = 0.819;
    private bool _Authority = false;
    private FCk_Handle_Timer _AuthorityTick;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow
    DoConstruct(FCk_Handle& InHandle)
    {
        auto State = CkAggroGym::Get_State();
        auto Mode = utils_net::Get_EntityNetMode(InHandle);
        if (ck::Is_NOT_Valid(State) || Generation <= 0 ||
            !CkAggroGym::IsValidStation(InHandle, n"Gym.Aggro.Perception") ||
            (Mode != ECk_Net_NetModeType::Host && Mode != ECk_Net_NetModeType::ClientAndHost &&
             Mode != ECk_Net_NetModeType::Client))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        _Authority = Mode != ECk_Net_NetModeType::Client;
        if (_Authority && (!State.HasAuthority() || State.Snapshot.Generation != Generation))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        if (!_Authority)
        {
            utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"OnTick"));
            ProjectionConstructed = true;
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        _GuardLoc = InitialTransform.Location + FVector(0.0, 0.0, 150.0);

        auto GuardEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        if (ck::Is_NOT_Valid(GuardEntity)) { return ECk_EntityScript_ConstructionFlow::Finished; }
        GuardEntity.Set_DebugName(n"AggroGym.Perception.Guard");
        _GuardTransform = utils_transform::Add(GuardEntity, FTransform(FRotator::ZeroRotator, _GuardLoc), ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(_GuardTransform)) { return ECk_EntityScript_ConstructionFlow::Finished; }

        // Fast decay + heavy unperceived penalty so "out of sight" visibly bleeds threat.
        auto OwnerParams = FCk_Aggro_Spec();
        auto DefaultTargetParams = FCk_AggroTarget_Spec();
        auto ThreatParams = FCk_AggroTarget_ThreatParams();
        ThreatParams.Set_InitialThreat(50.0);
        ThreatParams.Set_ThreatDecayRate(3.0);
        ThreatParams.Set_UnperceivedThreatDecayMultiplier(5.0);
        DefaultTargetParams.Set_ThreatParams(ThreatParams);
        auto ForgetParams = FCk_AggroTarget_ForgetParams();
        ForgetParams.Set_LostSightGraceDuration(FCk_Time(0.4));
        ForgetParams.Set_ForgetDuration(FCk_Time(120.0));   // never forget during the demo
        DefaultTargetParams.Set_ForgetParams(ForgetParams);
        OwnerParams.Set_DefaultTargetParams(DefaultTargetParams);
        _Guard = utils_aggro::Add(GuardEntity, OwnerParams);
        if (ck::Is_NOT_Valid(_Guard)) { return ECk_EntityScript_ConstructionFlow::Finished; }

        _Orbiter = utils_entity_lifetime::Request_CreateEntity(InHandle);
        if (ck::Is_NOT_Valid(_Orbiter)) { return ECk_EntityScript_ConstructionFlow::Finished; }
        _Orbiter.Set_DebugName(n"AggroGym.Perception.Orbiter");
        _OrbiterTransform = utils_transform::Add(_Orbiter, FTransform(FRotator::ZeroRotator, Get_OrbitLocation()), ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(_OrbiterTransform)) { return ECk_EntityScript_ConstructionFlow::Finished; }
        if (ck::Is_NOT_Valid(_Guard.CreateTarget(_Orbiter)))
        { return ECk_EntityScript_ConstructionFlow::Finished; }

        _AuthorityTick = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"OnTick"));
        if (ck::Is_NOT_Valid(_AuthorityTick)) { return ECk_EntityScript_ConstructionFlow::Finished; }
        AuthorityConstructed = true;
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    bool Get_HasValidAuthorityComposition()
    {
        if (!_Authority || !AuthorityConstructed ||
            !CkAggroGym::IsLiveEntity(FCk_Handle(_Guard)) ||
            !CkAggroGym::IsLiveEntity(FCk_Handle(_GuardTransform)) ||
            !CkAggroGym::IsLiveEntity(_Orbiter) ||
            !CkAggroGym::IsLiveEntity(FCk_Handle(_OrbiterTransform)) ||
            !CkAggroGym::IsLiveEntity(FCk_Handle(_AuthorityTick)))
        { return false; }
        auto Self = ck::ToEntity(this);
        auto Station = CkAggroGym::FindStation(n"Gym.Aggro.Perception");
        auto Guard = FCk_Handle(_Guard);
        if (!CkAggroGym::IsLiveEntity(Self) || !CkAggroGym::IsLiveEntity(Station) ||
            utils_entity_lifetime::Get_LifetimeOwner(Self) != Station ||
            !utils_timer::DoCast(FCk_Handle(_AuthorityTick)).IsSet() ||
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_AuthorityTick)) != Self ||
            _AuthorityTick.Get_CurrentState() != ECk_Timer_State::Running ||
            !utils_aggro::DoCast(Guard).IsSet() ||
            !utils_transform::Has(Guard) || !utils_transform::Has(_Orbiter) ||
            FCk_Handle(_GuardTransform) != Guard || FCk_Handle(_OrbiterTransform) != _Orbiter ||
            utils_entity_lifetime::Get_LifetimeOwner(Guard) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(_Orbiter) != Self ||
            _Guard.Get_NumTrackedTargets() != 1)
        { return false; }
        auto Target = _Guard.TryGet_Target_ByTrackedEntity(_Orbiter);
        if (!CkAggroGym::IsLiveEntity(FCk_Handle(Target)) || !utils_aggro_target::DoCast(FCk_Handle(Target)).IsSet() ||
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(Target)) != Guard ||
            Target.Get_TrackedEntity() != _Orbiter)
        { return false; }
        auto Active = _Guard.TryGet_ActiveTrackedEntity();
        return ck::Is_NOT_Valid(Active) ||
            (CkAggroGym::IsLiveEntity(Active) && Active == _Orbiter);
    }

    private FVector Get_OrbitLocation()
    {
        return _GuardLoc + FVector(Math::Cos(_OrbitAngle) * 500.0, Math::Sin(_OrbitAngle) * 500.0, 0.0);
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto State = CkAggroGym::Get_State();
        if (ck::Is_NOT_Valid(State) || State.Snapshot.Generation != Generation || State.Snapshot.Failed ||
            !CkAggroGym::IsValidStation(ck::ToEntity(this), n"Gym.Aggro.Perception")) { return; }
        if (!_Authority)
        {
            if (State.Get_HasAdmittedLocalProjection(Generation))
            { CkAggroGym::DrawPerception(ck::ToEntity(this), StationTitle, StationDescription, State.Snapshot); }
            return;
        }
        if (!State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation)) { return; }
        if (!Get_HasValidAuthorityComposition())
        {
            State.Request_FailCurrentServerChild(ck::ToEntity(this), Generation, "Perception composition lost a dependent entity");
            return;
        }
        _OrbitAngle += 0.9 * float(InDeltaT.Get_Seconds());
        utils_transform::Request_SetLocation(_OrbiterTransform, FCk_Request_Transform_SetLocation(Get_OrbitLocation()));

        auto OrbiterLoc = utils_transform::Get_EntityCurrentLocation(_OrbiterTransform);
        // Guard faces +X, so dot((1,0,0), dir) == dir.X.
        auto ToOrbiter = (OrbiterLoc - _GuardLoc).GetSafeNormal();
        auto InCone    = ToOrbiter.X >= _ConeHalfCos;

        // Mark perception ONLY on cone-entry/exit - FTag_AggroTarget_Perceived is counted.
        if (InCone && !_WasInCone)
        { _Guard.Request_MarkPerceived_ByTrackedEntity(_Orbiter, FCk_Request_AggroTarget_MarkPerceived()); }
        else if (!InCone && _WasInCone)
        {
            auto Target = _Guard.TryGet_Target_ByTrackedEntity(_Orbiter);
            if (ck::IsValid(Target))
            { Target.Request_MarkUnperceived(); }
        }
        _WasInCone = InCone;

        auto  Target = _Guard.TryGet_Target_ByTrackedEntity(_Orbiter);
        float Threat = ck::IsValid(Target) ? Target.Get_Threat() : 0.0f;
        auto Sample = FCkAggroGym_Snapshot();
        Sample.PerceptionGuard = _GuardLoc;
        Sample.PerceptionOrbiter = OrbiterLoc;
        Sample.PerceptionInCone = InCone;
        Sample.PerceptionThreat = Threat;
        State.SubmitPerception(ck::ToEntity(this), Generation, Sample);
        if (State.Snapshot.Ready)
        { CkAggroGym::DrawPerception(ck::ToEntity(this), StationTitle, StationDescription, State.Snapshot); }
    }
}

// ====================================================================================================================
// Station 3 - Stress: many guards x many dummies
// ====================================================================================================================

class UCk_EntityScript_AggroGym_Stress_Station : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) FString StationTitle;
    UPROPERTY(ExposeOnSpawn) FString StationDescription;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    UPROPERTY() bool ProjectionConstructed = false;
    UPROPERTY() bool AuthorityConstructed = false;

    private int32 _GuardCount = 24;
    private int32 _DummiesPerGuard = 5;

    private TArray<FCk_Handle_Aggro> _Guards;
    private TArray<FVector> _GuardLocs;
    private TArray<FCk_Handle> _TrackedDummies;
    private TArray<FCk_Handle_AggroTarget> _Targets;
    private float _FeedTimer = 0.0;
    private int32 _Seed = 1337;
    private bool _Authority = false;
    private FCk_Handle_Timer _AuthorityTick;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow
    DoConstruct(FCk_Handle& InHandle)
    {
        auto State = CkAggroGym::Get_State();
        auto Mode = utils_net::Get_EntityNetMode(InHandle);
        if (ck::Is_NOT_Valid(State) || Generation <= 0 ||
            !CkAggroGym::IsValidStation(InHandle, n"Gym.Aggro.Stress") ||
            (Mode != ECk_Net_NetModeType::Host && Mode != ECk_Net_NetModeType::ClientAndHost &&
             Mode != ECk_Net_NetModeType::Client))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        _Authority = Mode != ECk_Net_NetModeType::Client;
        if (_Authority && (!State.HasAuthority() || State.Snapshot.Generation != Generation))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        if (!_Authority)
        {
            utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"OnTick"));
            ProjectionConstructed = true;
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        auto Origin = InitialTransform.Location + FVector(0.0, 0.0, 100.0);

        for (int32 g = 0; g < _GuardCount; g++)
        {
            int32 Col = g % 6;
            int32 Row = Math::IntegerDivisionTrunc(g, 6);
            auto GuardLoc = Origin + FVector(float(Col) * 220.0, float(Row) * 220.0, 0.0);
            auto GuardEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
            if (ck::Is_NOT_Valid(GuardEntity)) { return ECk_EntityScript_ConstructionFlow::Finished; }
            GuardEntity.Set_DebugName(n"AggroGym.Stress.Guard");
            auto GuardTransform = utils_transform::Add(GuardEntity, FTransform(FRotator::ZeroRotator, GuardLoc), ECk_Replication::DoesNotReplicate);
            if (ck::Is_NOT_Valid(GuardTransform)) { return ECk_EntityScript_ConstructionFlow::Finished; }
            auto Guard = utils_aggro::Add(GuardEntity, FCk_Aggro_Spec());
            if (ck::Is_NOT_Valid(Guard)) { return ECk_EntityScript_ConstructionFlow::Finished; }

            for (int32 d = 0; d < _DummiesPerGuard; d++)
            {
                auto Dummy = utils_entity_lifetime::Request_CreateEntity(InHandle);
                if (ck::Is_NOT_Valid(Dummy)) { return ECk_EntityScript_ConstructionFlow::Finished; }
                Dummy.Set_DebugName(n"AggroGym.Stress.Dummy");
                auto DummyTransform = utils_transform::Add(Dummy, FTransform(FRotator::ZeroRotator, GuardLoc + FVector(float(d) * 40.0, 90.0, 0.0)), ECk_Replication::DoesNotReplicate);
                if (ck::Is_NOT_Valid(DummyTransform)) { return ECk_EntityScript_ConstructionFlow::Finished; }
                auto Target = Guard.CreateTarget(Dummy);
                if (ck::Is_NOT_Valid(Target)) { return ECk_EntityScript_ConstructionFlow::Finished; }
                _TrackedDummies.Add(Dummy);
                _Targets.Add(Target);
                Guard.Request_AddThreat(FCk_Request_Aggro_AddThreat(Dummy, float(d + 1) * 3.0));
            }

            _Guards.Add(Guard);
            _GuardLocs.Add(GuardLoc);
        }

        _AuthorityTick = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"OnTick"));
        if (ck::Is_NOT_Valid(_AuthorityTick)) { return ECk_EntityScript_ConstructionFlow::Finished; }
        AuthorityConstructed = true;
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    bool Get_HasValidAuthorityComposition()
    {
        if (!_Authority || !AuthorityConstructed || _GuardCount != 24 || _DummiesPerGuard != 5 ||
            _Guards.Num() != 24 || _GuardLocs.Num() != 24 ||
            _TrackedDummies.Num() != 120 || _Targets.Num() != 120 ||
            !CkAggroGym::IsLiveEntity(FCk_Handle(_AuthorityTick)))
        { return false; }
        auto Self = ck::ToEntity(this);
        auto Station = CkAggroGym::FindStation(n"Gym.Aggro.Stress");
        if (!CkAggroGym::IsLiveEntity(Self) || !CkAggroGym::IsLiveEntity(Station) ||
            utils_entity_lifetime::Get_LifetimeOwner(Self) != Station ||
            !utils_timer::DoCast(FCk_Handle(_AuthorityTick)).IsSet() ||
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_AuthorityTick)) != Self ||
            _AuthorityTick.Get_CurrentState() != ECk_Timer_State::Running)
        { return false; }
        for (int32 g = 0; g < 24; g++)
        {
            if (!CkAggroGym::IsLiveEntity(FCk_Handle(_Guards[g]))) { return false; }
            auto Guard = FCk_Handle(_Guards[g]);
            if (!utils_aggro::DoCast(Guard).IsSet() || !utils_transform::Has(Guard) ||
                utils_entity_lifetime::Get_LifetimeOwner(Guard) != Self ||
                _Guards[g].Get_NumTrackedTargets() != 5)
            { return false; }
            for (int32 prior = 0; prior < g; prior++)
            { if (Guard == FCk_Handle(_Guards[prior])) { return false; } }
            for (int32 d = 0; d < 5; d++)
            {
                auto Index = g * 5 + d;
                auto Dummy = _TrackedDummies[Index];
                auto Target = _Targets[Index];
                if (!CkAggroGym::IsLiveEntity(Dummy) ||
                    !CkAggroGym::IsLiveEntity(FCk_Handle(Target)) ||
                    !utils_transform::Has(Dummy) ||
                    !utils_aggro_target::DoCast(FCk_Handle(Target)).IsSet() ||
                    utils_entity_lifetime::Get_LifetimeOwner(Dummy) != Self ||
                    utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(Target)) != Guard ||
                    Target.Get_TrackedEntity() != Dummy ||
                    _Guards[g].TryGet_Target_ByTrackedEntity(Dummy) != Target)
                { return false; }
                for (int32 priorDummy = g * 5; priorDummy < Index; priorDummy++)
                { if (Dummy == _TrackedDummies[priorDummy]) { return false; } }
            }
            auto Active = _Guards[g].Get_ActiveTarget();
            if (ck::IsValid(Active))
            {
                if (!CkAggroGym::IsLiveEntity(FCk_Handle(Active))) { return false; }
                bool IsOwnedTarget = false;
                for (int32 d = 0; d < 5; d++)
                { if (Active == _Targets[g * 5 + d]) { IsOwnedTarget = true; break; } }
                if (!IsOwnedTarget) { return false; }
            }
        }
        return true;
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto State = CkAggroGym::Get_State();
        if (ck::Is_NOT_Valid(State) || State.Snapshot.Generation != Generation || State.Snapshot.Failed ||
            !CkAggroGym::IsValidStation(ck::ToEntity(this), n"Gym.Aggro.Stress")) { return; }
        if (!_Authority)
        {
            if (State.Get_HasAdmittedLocalProjection(Generation))
            { CkAggroGym::DrawStress(ck::ToEntity(this), StationTitle, StationDescription, State.Snapshot); }
            return;
        }
        if (!State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation)) { return; }
        if (!Get_HasValidAuthorityComposition())
        {
            State.Request_FailCurrentServerChild(ck::ToEntity(this), Generation, "Stress composition lost a dependent entity");
            return;
        }
        _FeedTimer += float(InDeltaT.Get_Seconds());

        // Keep churning: every 0.5s nudge a pseudo-random guard's random dummy so selections keep moving.
        if (_FeedTimer >= 0.5)
        {
            _FeedTimer = 0.0;
            _Seed = (_Seed * 1103515245 + 12345) & 0x7fffffff;
            auto Gi = _Seed % _Guards.Num();
            if (ck::IsValid(_Guards[Gi]))
            {
                auto Active = _Guards[Gi].Get_ActiveTarget();
                if (ck::IsValid(Active))
                { Active.Request_AddThreat(8.0); }
            }
        }

        auto Sample = FCkAggroGym_Snapshot();
        Sample.StressGuardCount = _Guards.Num();
        Sample.StressTargetsPerGuard = _DummiesPerGuard;
        for (int32 g = 0; g < _Guards.Num(); g++)
        {
            Sample.StressGuards.Add(_GuardLocs[g]);
            if (ck::Is_NOT_Valid(_Guards[g])) { continue; }
            if (ck::IsValid(_Guards[g].Get_ActiveTarget()))
            {
                Sample.StressActiveCount++;
                Sample.StressActiveMask |= (1 << g);
            }
        }
        State.SubmitStress(ck::ToEntity(this), Generation, Sample);
        if (State.Snapshot.Ready)
        { CkAggroGym::DrawStress(ck::ToEntity(this), StationTitle, StationDescription, State.Snapshot); }
    }
}
