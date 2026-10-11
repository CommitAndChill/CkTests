// Language=angelscript

//============================================================================
// CK EQS GYM - STATION ENTITY SCRIPT
//============================================================================
//
// One station class drives every EQS scenario via a Scenario enum. Each
// scenario builds a different FCk_Eqs_QueryParams (generator + test set + run
// mode) and re-issues the query on a 5-second cadence. Some scenarios spawn
// extra entities in DoConstruct (target rings, probe markers, LOS blockers).
//
// Display panel shows: scenario name, candidate count, best location, status,
// run #, and the time-to-next-restart.
//============================================================================

// AS-internal enum - UENUM() macro is not available in AngelScript; plain `enum`
// is the right form (no `class` keyword, values accessed as Name::Value per
// CkGoapEmpire_Station's ECk_GoapEmpire_Phase precedent).
enum ECkEqsGym_Scenario
{
    SimpleGrid_Distance,    // 5x5 SimpleGrid, Distance test, SingleBest
    Donut_Dot,              // 3-ring Donut, Dot test, SingleBest
    Cone_Generator,         // Cone generator, no tests, AllMatchingSorted
    Immediate_Sync,         // Same as SimpleGrid_Distance but via Request_RunQuery_Immediate
    RandomBest5Pct,         // SimpleGrid + Distance, RunMode RandomBest5Pct

    // v1.1 generators / tests.
    NavProjection,          // SimpleGrid + _ProjectOntoNav=true, snaps to floor's navmesh
    OnCircle,               // 8 points evenly spaced on a 300uu ring, Distance scoring
    VolumeCheck,            // SimpleGrid filtered by AABB centred on station
    Random,                 // SimpleGrid + Distance + Random tiebreaker, AllMatchingSorted

    // Existing test types that didn't have a v1.0 station.
    Trace,                  // SimpleGrid + LineTrace LOS test against a spawned wall
    EntitiesWithTag,        // Generator returns 5 spawned target entities, Distance scoring
    Overlap                 // SimpleGrid + Overlap test against 3 spawned probe markers
}

USTRUCT()
struct FCkEqsGym_StationSpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;

    UPROPERTY()
    FString StationTitle;

    UPROPERTY()
    FString StationDescription;

    UPROPERTY()
    ECkEqsGym_Scenario Scenario = ECkEqsGym_Scenario::SimpleGrid_Distance;

    UPROPERTY()
    float RestartIntervalSeconds = 5.0f;
}

// ====================================================================================================================

class UCk_EntityScript_EqsGym_Station : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) FString    StationTitle;
    UPROPERTY(ExposeOnSpawn) FString    StationDescription;
    UPROPERTY(ExposeOnSpawn) ECkEqsGym_Scenario Scenario = ECkEqsGym_Scenario::SimpleGrid_Distance;
    UPROPERTY(ExposeOnSpawn) float      RestartIntervalSeconds = 5.0f;

    // Latest result snapshot (updated on each OnComplete fire / on Immediate return).
    private bool             _HasResult = false;
    private FVector          _BestLocation = FVector::ZeroVector;
    private int32            _CandidateCount = 0;
    private bool             _Failed = false;
    private int32            _RunCount = 0;
    private float            _TimeSinceLast = 0.0f;
    private bool             _IsImmediate = false;
    private bool             _Prepared = false;
    private bool             _Started = false;
    private bool             _Closed = false;
    private bool             _QueryPending = false;
    private FCk_Handle       _Root;
    private FCk_Handle       _WorldRoot;
    private UWorld           _World;
    private FCk_Handle_Timer _Tick;
    private TArray<FCk_Handle> _Helpers;

    // Most-recent query handle (deferred path: filled in OnQueryComplete; immediate path: filled
    // in Capture_Result). Set_AutoDestroy(false) on the deferred request keeps the query entity
    // alive past Cleanup so the user can inspect it in the debugger; we manually destroy it on the
    // next cycle to keep the in-world list at one query per station instead of accumulating.
    private FCk_Handle_EqsQuery _LastQueryHandle;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        _Root = InHandle;
        _World = GetWorld();
        if (ck::Is_NOT_Valid(_World) ||
            Cast<ACk_EqsGym_GameState>(_World.GetGameState()) == nullptr ||
            !_World.GetGameState().HasAuthority() ||
            !InitialTransform.IsValid() || InitialTransform.ContainsNaN() ||
            !Math::IsFinite(RestartIntervalSeconds) || RestartIntervalSeconds <= 0.0f ||
            int32(Scenario) < int32(ECkEqsGym_Scenario::SimpleGrid_Distance) ||
            int32(Scenario) > int32(ECkEqsGym_Scenario::Overlap))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        auto Ecs = Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(Ecs))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        _WorldRoot = Ecs.Get_TransientEntity();
        if (!IsDirectChild(_Root, _WorldRoot))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        InHandle.Set_DebugName(n"EqsStation");
        auto RootTransform = utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(RootTransform) || !IsRootLive())
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }

        // Per-scenario one-shot world setup. Targets / probes live as child entities
        // owned by this station so they're cleaned up when the station entity goes away.
        if (Scenario == ECkEqsGym_Scenario::EntitiesWithTag)
        {
            if (!Spawn_TargetEntities(InHandle))
            { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        }
        else if (Scenario == ECkEqsGym_Scenario::Overlap)
        {
            if (!Spawn_OverlapMarkers(InHandle))
            { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        }
        else if (Scenario == ECkEqsGym_Scenario::Trace)
        {
            if (!Spawn_TraceBlocker(InHandle))
            { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        }
        _Tick = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"OnDisplayTick"));
        if (!IsTimerLive(_Tick) || utils_timer::Request_Pause(_Tick) != _Tick ||
            !Get_StationIsHealthy(false))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        _Prepared = true;
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    { Request_Block(); }

    private bool IsRootLive() const
    {
        return ck::IsValid(_World) && !_Closed &&
            UCk_EqsGym_Runtime_UE::Get_IsLiveInWorld(_Root, _World) &&
            utils_transform::Has(_Root);
    }

    private bool IsDirectChild(FCk_Handle Child, FCk_Handle Parent) const
    {
        return ck::IsValid(_World) &&
            UCk_EqsGym_Runtime_UE::Get_IsLiveInWorld(Child, _World) &&
            UCk_EqsGym_Runtime_UE::Get_IsLiveInWorld(Parent, _World) &&
            utils_entity_lifetime::Has_LifetimeOwner(Child) &&
            utils_entity_lifetime::Get_LifetimeOwner(Child) == Parent;
    }

    private bool IsTimerLive(FCk_Handle_Timer Timer) const
    {
        return ck::IsValid(_World) &&
            UCk_EqsGym_Runtime_UE::Get_IsLiveTimer(FCk_Handle(Timer), _World);
    }

    private bool Get_StationIsHealthy(bool RequireSettledTags = true) const
    {
        if (!IsRootLive() || !IsDirectChild(_Root, _WorldRoot)) { return false; }
        const int32 Expected = Scenario == ECkEqsGym_Scenario::EntitiesWithTag ? 5 :
            (Scenario == ECkEqsGym_Scenario::Overlap ? 3 :
            (Scenario == ECkEqsGym_Scenario::Trace ? 1 : 0));
        if (_Helpers.Num() != Expected) { return false; }
        for (int32 I = 0; I < _Helpers.Num(); I++)
        {
            if (!IsDirectChild(_Helpers[I], _Root) || !utils_transform::Has(_Helpers[I]))
            { return false; }
            if (RequireSettledTags && Scenario == ECkEqsGym_Scenario::EntitiesWithTag &&
                !utils_entity_tag::Has(_Helpers[I], n"Gym.Eqs.Target"))
            { return false; }
            if ((Scenario == ECkEqsGym_Scenario::Overlap || Scenario == ECkEqsGym_Scenario::Trace) &&
                !utils_probe::Has(_Helpers[I]))
            { return false; }
        }
        return true;
    }

    bool Get_IsPrepared() const
    {
        return _Prepared && !_Closed && Get_StationIsHealthy() &&
            IsTimerLive(_Tick) && IsDirectChild(FCk_Handle(_Tick), _Root);
    }

    bool Get_IsStarted() const { return _Started && !_Closed; }
    bool Get_IsClosed() const { return _Closed; }
    FCk_Handle Get_SourceRoot() const { return _Root; }

    bool Request_StartPrepared()
    {
        if (_Started) { return Get_IsPrepared(); }
        if (!Get_IsPrepared()) { Request_Block(); return false; }
        _Started = true;
        if (!Request_StartScenario() || utils_timer::Request_Resume(_Tick) != _Tick)
        { Request_Block(); return false; }
        return true;
    }

    void Request_Block()
    {
        if (_Closed) { return; }
        _Closed = true;
        _Prepared = false;
        _Started = false;
        _QueryPending = false;
        if (IsTimerLive(_Tick)) { utils_timer::Request_Stop(_Tick); }
        if (ck::IsValid(_World) && UCk_EqsGym_Runtime_UE::Get_IsLiveInWorld(_Root, _World))
        { utils_eqs::Request_CancelAllQueries(_Root); }
    }

    bool TryGet_Frame(FCkEqsGym_StationFrame&out Out) const
    {
        if (!Get_IsPrepared() || _RunCount < 0 || _CandidateCount < 0 ||
            !Math::IsFinite(_TimeSinceLast) || !Math::IsFinite(RestartIntervalSeconds) ||
            (_HasResult && (!Math::IsFinite(_BestLocation.X) ||
                !Math::IsFinite(_BestLocation.Y) || !Math::IsFinite(_BestLocation.Z))))
        { return false; }
        FCkEqsGym_StationFrame Frame;
        Frame.Title = StationTitle;
        Frame.Footer = StationDescription;
        Frame.RunCount = int64(_RunCount);
        Frame.CandidateCount = _CandidateCount;
        Frame.HasResult = _HasResult;
        Frame.Failed = _Failed;
        Frame.IsImmediate = _IsImmediate;
        Frame.BestLocation = _BestLocation;
        Frame.SecondsLeft = Math::Max(0.0f, RestartIntervalSeconds - _TimeSinceLast);
        const auto Mode = _IsImmediate ? "IMMEDIATE" : "DEFERRED";
        const auto Status = _Failed ? "FAILED" : (_HasResult ? "READY" : "PENDING");
        const auto SecondsLeft = int(Frame.SecondsLeft) + 1;
        auto Detail = f"Scenario: {Get_ScenarioName()}\n";
        Detail = f"{Detail}Mode: {Mode}    Run #{_RunCount}\n";
        Detail = f"{Detail}Status: {Status}\n";
        Detail = f"{Detail}Candidates: {_CandidateCount}\n";
        if (_HasResult)
        {
            Detail = f"{Detail}Best: ({int(_BestLocation.X)}, {int(_BestLocation.Y)}, {int(_BestLocation.Z)})\n";
        }
        Frame.Detail = f"{Detail}\nRestarting in {SecondsLeft}s...";
        Out = Frame;
        return true;
    }

    // Spawn 5 tagged child entities arranged on a 250uu ring around the station so the
    // EntitiesWithTag generator has something to find. The Distance test then orders them
    // by closeness to the querier (the station entity itself).
    private bool Spawn_TargetEntities(FCk_Handle& InStation)
    {
        const auto Radius = 250.0f;
        const auto NumTargets = 5;
        const auto BaseLocation = InitialTransform.GetLocation();
        const auto TargetTag = utils_gameplay_tag::ResolveGameplayTag(n"Gym.Eqs.Target");
        if (!TargetTag.IsValid()) { return false; }

        for (int32 i = 0; i < NumTargets; i++)
        {
            const auto Yaw = (360.0f / float(NumTargets)) * float(i);
            const auto Direction = FRotator(0.0f, Yaw, 0.0f).Vector();
            const auto TargetLocation = BaseLocation + Direction * Radius;
            const auto TargetTransform = FTransform(FRotator::ZeroRotator, TargetLocation);

            auto Target = utils_entity_lifetime::Request_CreateEntity(InStation);
            if (!IsDirectChild(Target, InStation)) { return false; }
            _Helpers.Add(Target);
            auto TargetName = FName(f"Target_{i}");
            Target.Set_DebugName(TargetName);
            Target.Request_OverrideToSelf();
            auto TargetXform = utils_transform::Add(Target, TargetTransform, ECk_Replication::DoesNotReplicate);
            if (ck::Is_NOT_Valid(TargetXform) || !utils_transform::Has(Target)) { return false; }
            // Tags are queued; Get_IsPrepared waits for all five to settle.
            if (utils_entity_tag::Add_UsingGameplayTag(Target, TargetTag) != Target) { return false; }
        }
        return true;
    }

    // Spawn 1 box probe co-located with the GameState's shared visible wall.
    // The probe is what the CkSpatialQuery line-trace actually intersects (actor collision
    // alone isn't visible to the probe-trace system), so this is what makes the Trace
    // test's filter eliminate candidates whose LOS is blocked.
    //
    // Wall offset/scale must match the shared fixture in CkEqs_GymGameState.as.
    private bool Spawn_TraceBlocker(FCk_Handle& InStation)
    {
        const auto BlockerProbeName = utils_gameplay_tag::ResolveGameplayTag(n"Gym.Eqs.TraceBlocker");
        if (!BlockerProbeName.IsValid()) { return false; }
        const auto BaseLocation = InitialTransform.GetLocation();
        const auto BlockerOffset = FVector(150.0f, 0.0f, 100.0f);   // matches shared wall
        const auto BlockerHalfExtent = FVector(25.0f, 250.0f, 150.0f); // 50 x 500 x 300 cm

        const auto BlockerTransform = FTransform(FRotator::ZeroRotator, BaseLocation + BlockerOffset);

        auto Blocker = utils_entity_lifetime::Request_CreateEntity(InStation);
        if (!IsDirectChild(Blocker, InStation)) { return false; }
        _Helpers.Add(Blocker);
        Blocker.Set_DebugName(n"TraceBlocker");
        Blocker.Request_OverrideToSelf();
        auto BlockerXform = utils_transform::Add(Blocker, BlockerTransform, ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(BlockerXform) || !utils_transform::Has(Blocker)) { return false; }

        auto ProbeParams = FCk_Probe_Spec(BlockerProbeName);
        ProbeParams.Set_MotionType(ECk_MotionType::Static);
        ProbeParams.Set_ResponsePolicy(ECk_ProbeResponse_Policy::Silent);
        auto DebugInfo = FCk_Probe_DebugInfo();
        auto Probe = utils_probe::Add_Box(BlockerXform, BlockerHalfExtent, ProbeParams, DebugInfo);
        if (ck::Is_NOT_Valid(Probe)) { return false; }
        if (FCk_Handle(Probe) != Blocker || !utils_probe::Has(Blocker))
        { return false; }
        return true;
    }

    // Spawn 3 sphere probes carrying the Gym.Eqs.OverlapMarker probe-name. The Overlap test's
    // _OverlapFilter matches that name so candidates near a marker score higher.
    private bool Spawn_OverlapMarkers(FCk_Handle& InStation)
    {
        const auto MarkerProbeName = utils_gameplay_tag::ResolveGameplayTag(n"Gym.Eqs.OverlapMarker");
        if (!MarkerProbeName.IsValid()) { return false; }
        const auto Radius = 60.0f;
        const auto BaseLocation = InitialTransform.GetLocation();
        // Three offsets near the SimpleGrid footprint (which is 5x5 @ 100uu spacing).
        auto Offsets = TArray<FVector>();
        Offsets.Add(FVector(-100.0f,  100.0f, 0.0f));
        Offsets.Add(FVector(   0.0f,    0.0f, 0.0f));
        Offsets.Add(FVector( 100.0f, -100.0f, 0.0f));

        for (int32 i = 0; i < Offsets.Num(); i++)
        {
            const auto MarkerTransform = FTransform(FRotator::ZeroRotator, BaseLocation + Offsets[i]);

            auto Marker = utils_entity_lifetime::Request_CreateEntity(InStation);
            if (!IsDirectChild(Marker, InStation)) { return false; }
            _Helpers.Add(Marker);
            auto MarkerName = FName(f"OverlapMarker_{i}");
            Marker.Set_DebugName(MarkerName);
            Marker.Request_OverrideToSelf();
            auto MarkerXform = utils_transform::Add(Marker, MarkerTransform, ECk_Replication::DoesNotReplicate);
            if (ck::Is_NOT_Valid(MarkerXform) || !utils_transform::Has(Marker)) { return false; }

            auto ProbeParams = FCk_Probe_Spec(MarkerProbeName);
            ProbeParams.Set_MotionType(ECk_MotionType::Static);
            ProbeParams.Set_ResponsePolicy(ECk_ProbeResponse_Policy::Silent);
            auto DebugInfo = FCk_Probe_DebugInfo();
            auto Probe = utils_probe::Add_Sphere(MarkerXform, Radius, ProbeParams, DebugInfo);
            if (ck::Is_NOT_Valid(Probe)) { return false; }
            if (FCk_Handle(Probe) != Marker || !utils_probe::Has(Marker))
            { return false; }
        }
        return true;
    }

    private bool Request_StartScenario()
    {
        if (!_Started || !Get_IsPrepared() || _QueryPending || _RunCount == 2147483647)
        { return false; }
        auto SelfEntity = _Root;
        _RunCount++;
        _TimeSinceLast = 0.0f;
        _IsImmediate = (Scenario == ECkEqsGym_Scenario::Immediate_Sync);
        _HasResult = false;
        _Failed = false;
        _CandidateCount = 0;
        _BestLocation = FVector::ZeroVector;

        // Destroy the previous query before issuing a new one. Without this, deferred queries
        // (with Set_AutoDestroy(false)) and Immediate queries would pile up on the station entity
        // every 5 seconds. Doing it here also fixes the original "queries flicker" UX problem in
        // the debugger - the debugger now sees one stable query per station instead of a brief
        // 1-2 frame flash followed by 5 seconds of nothing.
        if (UCk_EqsGym_Runtime_UE::Get_IsLiveQuery(FCk_Handle(_LastQueryHandle), _World))
        {
            auto OldQuery = FCk_Handle(_LastQueryHandle);
            utils_entity_lifetime::Request_DestroyEntity(OldQuery);
            _LastQueryHandle = utils_eqs::Get_InvalidHandle();
        }

        auto QueryParams = Build_QueryParams(SelfEntity);

        if (_IsImmediate)
        {
            // Synchronous path: returns a typed query handle with results already written.
            // Does NOT broadcast OnComplete (P3-E5) - read accessors directly.
            auto QueryHandle = utils_eqs::Request_RunQuery_Immediate(SelfEntity, QueryParams);
            if (!UCk_EqsGym_Runtime_UE::Get_IsQueryOwnedBy(FCk_Handle(QueryHandle), _Root, _World) ||
                !utils_eqs::Get_IsComplete(QueryHandle))
            { return false; }
            _LastQueryHandle = QueryHandle;
            Capture_Result(QueryHandle);
            if (_Closed) { return false; }
        }
        else
        {
            // Deferred path: bind OnComplete on the request, fire and forget. AutoDestroy=false
            // keeps the query entity alive past Cleanup so the user can inspect it in the
            // debugger - we own the destroy on the next cycle (above).
            auto Request = FCk_Request_Eqs_RunQuery(QueryParams);
            Request.Set_OnComplete(FCk_Delegate_EqsQuery_OnComplete(this, n"OnQueryComplete"));
            Request.Set_AutoDestroy(false);
            _QueryPending = true;
            if (utils_eqs::Request_RunQuery(SelfEntity, Request) != SelfEntity)
            { _QueryPending = false; return false; }
        }
        return true;
    }

    private FCk_Eqs_QueryParams Build_QueryParams(const FCk_Handle& InQuerier)
    {
        auto Tests = TArray<FCk_Eqs_TestParams>();

        if (Scenario == ECkEqsGym_Scenario::SimpleGrid_Distance ||
            Scenario == ECkEqsGym_Scenario::Immediate_Sync ||
            Scenario == ECkEqsGym_Scenario::RandomBest5Pct)
        {
            auto Generator = FCk_Eqs_GeneratorParams();
            Generator.Set_GeneratorType(ECk_Eqs_GeneratorType::SimpleGrid);
            Generator.Set_SpaceBetween(100.0f);
            Generator.Set_GridHalfSize(200.0f);  // 5x5 = 25 candidates

            // Distance test - closer to querier scores higher (InverseLinear).
            auto Distance = FCk_Eqs_TestParams();
            Distance.Set_TestType(ECk_Eqs_TestType::Distance);
            Distance.Set_Purpose(ECk_Eqs_TestPurpose::Score);
            Distance.Set_DistanceMode(ECk_Eqs_DistanceMode::Distance3D);
            auto Scoring = FCk_Eqs_ScoringConfig();
            Scoring.Set_ScoringEquation(ECk_Eqs_ScoringEquation::InverseLinear);
            Distance.Set_ScoringConfig(Scoring);
            Tests.Add(Distance);

            auto Params = FCk_Eqs_QueryParams(InQuerier, Generator, Tests);
            Params.Set_RunMode(Scenario == ECkEqsGym_Scenario::RandomBest5Pct
                ? ECk_Eqs_RunMode::RandomBest5Pct
                : ECk_Eqs_RunMode::SingleBest);
            return Params;
        }

        if (Scenario == ECkEqsGym_Scenario::Donut_Dot)
        {
            auto Generator = FCk_Eqs_GeneratorParams();
            Generator.Set_GeneratorType(ECk_Eqs_GeneratorType::Donut);
            Generator.Set_InnerRadius(200.0f);
            Generator.Set_OuterRadius(500.0f);
            Generator.Set_NumRings(3);
            Generator.Set_PointsPerRing(8);
            Generator.Set_ArcAngle(360.0f);

            // Dot test - forward of the querier scores highest.
            auto Dot = FCk_Eqs_TestParams();
            Dot.Set_TestType(ECk_Eqs_TestType::Dot);
            Dot.Set_Purpose(ECk_Eqs_TestPurpose::Score);
            Dot.Set_DotMode(ECk_Eqs_DotMode::NormalizedDot);
            auto Scoring = FCk_Eqs_ScoringConfig();
            Scoring.Set_ScoringEquation(ECk_Eqs_ScoringEquation::Linear);
            Dot.Set_ScoringConfig(Scoring);
            Tests.Add(Dot);

            auto Params = FCk_Eqs_QueryParams(InQuerier, Generator, Tests);
            Params.Set_RunMode(ECk_Eqs_RunMode::SingleBest);
            return Params;
        }

        if (Scenario == ECkEqsGym_Scenario::Cone_Generator)
        {
            // Cone_Generator: no tests, AllMatchingSorted to surface every candidate.
            auto Generator = FCk_Eqs_GeneratorParams();
            Generator.Set_GeneratorType(ECk_Eqs_GeneratorType::Cone);
            Generator.Set_ConeDegrees(90.0f);
            Generator.Set_ConeRange(500.0f);
            Generator.Set_ConeAngleStep(15.0f);
            Generator.Set_ConePointSpacing(100.0f);

            // A neutral Score-only Distance test satisfies the non-empty-Tests requirement
            // without changing which candidates survive.
            auto Distance = FCk_Eqs_TestParams();
            Distance.Set_Purpose(ECk_Eqs_TestPurpose::Score);
            Tests.Add(Distance);

            auto Params = FCk_Eqs_QueryParams(InQuerier, Generator, Tests);
            Params.Set_RunMode(ECk_Eqs_RunMode::AllMatchingSorted);
            return Params;
        }

        if (Scenario == ECkEqsGym_Scenario::NavProjection)
        {
            // SimpleGrid + project candidates onto the floor's navmesh. Without the
            // floor's bake, every candidate would be dropped; with it, all candidates
            // snap down to navmesh height and survive.
            auto Generator = FCk_Eqs_GeneratorParams();
            Generator.Set_GeneratorType(ECk_Eqs_GeneratorType::SimpleGrid);
            Generator.Set_SpaceBetween(100.0f);
            Generator.Set_GridHalfSize(200.0f);
            Generator.Set_ProjectOntoNav(true);
            // PanelCenter is 250uu above the floor; 300uu reaches its baked surface.
            Generator.Set_NavProjectionSearchHalfExtentUu(300.0f);

            auto Distance = FCk_Eqs_TestParams();
            Distance.Set_Purpose(ECk_Eqs_TestPurpose::Score);
            Tests.Add(Distance);

            auto Params = FCk_Eqs_QueryParams(InQuerier, Generator, Tests);
            Params.Set_RunMode(ECk_Eqs_RunMode::AllMatching);
            return Params;
        }

        if (Scenario == ECkEqsGym_Scenario::OnCircle)
        {
            // 8 evenly-spaced points on a 300uu ring. Distance from the querier scores
            // the picks (all at radius 300, so scoring is degenerate but visualises the
            // generator nicely).
            auto Generator = FCk_Eqs_GeneratorParams();
            Generator.Set_GeneratorType(ECk_Eqs_GeneratorType::OnCircle);
            Generator.Set_CircleRadius(300.0f);
            Generator.Set_NumPointsOnCircle(8);
            Generator.Set_CircleArcAngle(360.0f);

            auto Distance = FCk_Eqs_TestParams();
            Distance.Set_TestType(ECk_Eqs_TestType::Distance);
            Distance.Set_Purpose(ECk_Eqs_TestPurpose::Score);
            Tests.Add(Distance);

            auto Params = FCk_Eqs_QueryParams(InQuerier, Generator, Tests);
            Params.Set_RunMode(ECk_Eqs_RunMode::AllMatchingSorted);
            return Params;
        }

        if (Scenario == ECkEqsGym_Scenario::VolumeCheck)
        {
            // SimpleGrid (25 candidates) filtered by an AABB at the station origin.
            // HalfExtent = (150, 150, 50) keeps the inner 3x3 = 9 candidates.
            auto Generator = FCk_Eqs_GeneratorParams();
            Generator.Set_GeneratorType(ECk_Eqs_GeneratorType::SimpleGrid);
            Generator.Set_SpaceBetween(100.0f);
            Generator.Set_GridHalfSize(200.0f);

            auto Volume = FCk_Eqs_TestParams();
            Volume.Set_TestType(ECk_Eqs_TestType::VolumeCheck);
            Volume.Set_Purpose(ECk_Eqs_TestPurpose::Filter);
            Volume.Set_VolumeCheck_WorldCenter(InitialTransform.GetLocation());
            Volume.Set_VolumeCheck_HalfExtent(FVector(150.0f, 150.0f, 50.0f));
            Volume.Set_VolumeCheck_Inverted(false);
            // Raise FilterMin so raw=0 (outside box) candidates fail the Minimum filter.
            auto FilterCfg = FCk_Eqs_FilterConfig();
            FilterCfg.Set_FilterMin(0.5f);
            Volume.Set_FilterConfig(FilterCfg);
            Tests.Add(Volume);

            auto Params = FCk_Eqs_QueryParams(InQuerier, Generator, Tests);
            Params.Set_RunMode(ECk_Eqs_RunMode::AllMatching);
            return Params;
        }

        if (Scenario == ECkEqsGym_Scenario::Random)
        {
            // SimpleGrid + Distance score + Random tiebreaker. Distance dominates; Random
            // multiplies a small per-candidate factor in so equal-distance candidates aren't
            // ordered deterministically.
            auto Generator = FCk_Eqs_GeneratorParams();
            Generator.Set_GeneratorType(ECk_Eqs_GeneratorType::SimpleGrid);
            Generator.Set_SpaceBetween(100.0f);
            Generator.Set_GridHalfSize(200.0f);

            auto Distance = FCk_Eqs_TestParams();
            Distance.Set_TestType(ECk_Eqs_TestType::Distance);
            Distance.Set_Purpose(ECk_Eqs_TestPurpose::Score);
            auto DistanceScoring = FCk_Eqs_ScoringConfig();
            DistanceScoring.Set_ScoringEquation(ECk_Eqs_ScoringEquation::InverseLinear);
            Distance.Set_ScoringConfig(DistanceScoring);
            Tests.Add(Distance);

            auto Random = FCk_Eqs_TestParams();
            Random.Set_TestType(ECk_Eqs_TestType::Random);
            Random.Set_Purpose(ECk_Eqs_TestPurpose::Score);
            Random.Set_Weight(0.1f);  // small enough to be a tiebreaker, not a primary
            Tests.Add(Random);

            auto Params = FCk_Eqs_QueryParams(InQuerier, Generator, Tests);
            Params.Set_RunMode(ECk_Eqs_RunMode::AllMatchingSorted);
            return Params;
        }

        if (Scenario == ECkEqsGym_Scenario::Trace)
        {
            // SimpleGrid + LineTrace LOS test. The GameState spawns a shared obstacle wall
            // alongside this station; candidates whose line-of-sight from the querier is blocked
            // by the wall fail the trace and are dropped.
            auto Generator = FCk_Eqs_GeneratorParams();
            Generator.Set_GeneratorType(ECk_Eqs_GeneratorType::SimpleGrid);
            Generator.Set_SpaceBetween(100.0f);
            Generator.Set_GridHalfSize(200.0f);

            auto Trace = FCk_Eqs_TestParams();
            Trace.Set_TestType(ECk_Eqs_TestType::Trace);
            Trace.Set_Purpose(ECk_Eqs_TestPurpose::Filter);
            Trace.Set_TraceMode(ECk_Eqs_TraceMode::LineTrace);
            // The trace must hit only the blocker probe (TraceFilter matches its probe-name).
            auto TraceFilter = FGameplayTagContainer();
            TraceFilter.AddTag(utils_gameplay_tag::ResolveGameplayTag(n"Gym.Eqs.TraceBlocker"));
            Trace.Set_TraceFilter(TraceFilter);
            // FilterMin=0.5 so raw=0 (LOS blocked) candidates fail the Minimum filter.
            auto FilterCfg = FCk_Eqs_FilterConfig();
            FilterCfg.Set_FilterMin(0.5f);
            Trace.Set_FilterConfig(FilterCfg);
            Tests.Add(Trace);

            auto Params = FCk_Eqs_QueryParams(InQuerier, Generator, Tests);
            Params.Set_RunMode(ECk_Eqs_RunMode::AllMatching);
            return Params;
        }

        if (Scenario == ECkEqsGym_Scenario::EntitiesWithTag)
        {
            // Generator returns the 5 target entities the station spawned in DoConstruct.
            // Distance test orders them by proximity to the querier.
            auto Generator = FCk_Eqs_GeneratorParams();
            Generator.Set_GeneratorType(ECk_Eqs_GeneratorType::EntitiesWithTag);
            Generator.Set_RequiredEntityTag(utils_gameplay_tag::ResolveGameplayTag(n"Gym.Eqs.Target"));

            auto Distance = FCk_Eqs_TestParams();
            Distance.Set_TestType(ECk_Eqs_TestType::Distance);
            Distance.Set_Purpose(ECk_Eqs_TestPurpose::Score);
            auto DistanceScoring = FCk_Eqs_ScoringConfig();
            DistanceScoring.Set_ScoringEquation(ECk_Eqs_ScoringEquation::InverseLinear);
            Distance.Set_ScoringConfig(DistanceScoring);
            Tests.Add(Distance);

            auto Params = FCk_Eqs_QueryParams(InQuerier, Generator, Tests);
            Params.Set_RunMode(ECk_Eqs_RunMode::SingleBest);
            return Params;
        }

        // Overlap: SimpleGrid + Overlap test against the 3 markers DoConstruct spawned.
        // Score-only purpose: candidates near markers get a higher overlap count and rank
        // higher in AllMatchingSorted.
        auto Generator = FCk_Eqs_GeneratorParams();
        Generator.Set_GeneratorType(ECk_Eqs_GeneratorType::SimpleGrid);
        Generator.Set_SpaceBetween(100.0f);
        Generator.Set_GridHalfSize(200.0f);

        auto Overlap = FCk_Eqs_TestParams();
        Overlap.Set_TestType(ECk_Eqs_TestType::Overlap);
        Overlap.Set_Purpose(ECk_Eqs_TestPurpose::Score);
        Overlap.Set_OverlapRadius(80.0f);
        auto OverlapFilter = FGameplayTagContainer();
        OverlapFilter.AddTag(utils_gameplay_tag::ResolveGameplayTag(n"Gym.Eqs.OverlapMarker"));
        Overlap.Set_OverlapFilter(OverlapFilter);
        Tests.Add(Overlap);

        auto Params = FCk_Eqs_QueryParams(InQuerier, Generator, Tests);
        Params.Set_RunMode(ECk_Eqs_RunMode::AllMatchingSorted);
        return Params;
    }

    UFUNCTION()
    private void OnQueryComplete(FCk_Handle_EqsQuery InQueryHandle, FCk_Eqs_QueryResults InResults)
    {
        if (!_Started || _Closed || !_QueryPending || !Get_IsPrepared()) { return; }
        if (!UCk_EqsGym_Runtime_UE::Get_IsQueryOwnedBy(
            FCk_Handle(InQueryHandle), _Root, _World))
        { return; }
        if (!utils_eqs::Get_IsComplete(InQueryHandle))
        { Request_Block(); return; }
        _QueryPending = false;
        _LastQueryHandle = InQueryHandle;
        _HasResult = InResults.Get_HasResults();
        _CandidateCount = InResults.Get_Candidates().Num();
        _BestLocation = InResults.Get_BestLocation();
        _Failed = utils_eqs::Get_IsFailed(InQueryHandle);
        if (_Failed || !_HasResult || _CandidateCount <= 0 ||
            !Math::IsFinite(_BestLocation.X) || !Math::IsFinite(_BestLocation.Y) ||
            !Math::IsFinite(_BestLocation.Z))
        { Request_Block(); }
    }

    private void Capture_Result(FCk_Handle_EqsQuery InQueryHandle)
    {
        // Immediate path: results are already written; read via accessors.
        _HasResult = utils_eqs::Get_HasResults(InQueryHandle);
        auto Candidates = utils_eqs::Get_AllCandidates(InQueryHandle);
        _CandidateCount = Candidates.Num();
        _BestLocation = utils_eqs::Get_BestLocation(InQueryHandle);
        _Failed = utils_eqs::Get_IsFailed(InQueryHandle);
        if (_Failed || !_HasResult || _CandidateCount <= 0 ||
            !Math::IsFinite(_BestLocation.X) || !Math::IsFinite(_BestLocation.Y) ||
            !Math::IsFinite(_BestLocation.Z))
        { Request_Block(); }
    }

    UFUNCTION()
    private void OnDisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (!_Started || _Closed || InHandle != _Tick || !Get_IsPrepared() ||
            !IsTimerLive(InHandle))
        { return; }
        const float DeltaSeconds = float(InDeltaT.Get_Seconds());
        if (!Math::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
        { Request_Block(); return; }
        _TimeSinceLast += DeltaSeconds;
        if (!Math::IsFinite(_TimeSinceLast)) { Request_Block(); return; }
        if (_QueryPending && _TimeSinceLast > 15.0f)
        { Request_Block(); return; }
        if (_TimeSinceLast >= RestartIntervalSeconds && !_QueryPending &&
            !Request_StartScenario())
        { Request_Block(); }
    }

    private FString Get_ScenarioName() const
    {
        if (Scenario == ECkEqsGym_Scenario::SimpleGrid_Distance) { return "SimpleGrid + Distance"; }
        if (Scenario == ECkEqsGym_Scenario::Donut_Dot)           { return "Donut + Dot"; }
        if (Scenario == ECkEqsGym_Scenario::Cone_Generator)      { return "Cone (no tests)"; }
        if (Scenario == ECkEqsGym_Scenario::Immediate_Sync)      { return "Immediate Sync"; }
        if (Scenario == ECkEqsGym_Scenario::RandomBest5Pct)      { return "RandomBest5Pct"; }
        if (Scenario == ECkEqsGym_Scenario::NavProjection)       { return "NavProjection"; }
        if (Scenario == ECkEqsGym_Scenario::OnCircle)            { return "OnCircle"; }
        if (Scenario == ECkEqsGym_Scenario::VolumeCheck)         { return "VolumeCheck"; }
        if (Scenario == ECkEqsGym_Scenario::Random)              { return "Random Tiebreaker"; }
        if (Scenario == ECkEqsGym_Scenario::Trace)               { return "Trace (LOS)"; }
        if (Scenario == ECkEqsGym_Scenario::EntitiesWithTag)     { return "EntitiesWithTag"; }
        if (Scenario == ECkEqsGym_Scenario::Overlap)             { return "Overlap"; }
        return "UNKNOWN";
    }
}
