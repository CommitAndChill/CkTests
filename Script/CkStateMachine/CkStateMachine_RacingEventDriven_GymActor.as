// ============================================================================
// SM RACING EVENT-DRIVEN TRANSITIONS - GYM ACTOR
// ============================================================================
//
// Driver actor for CkStateMachine_TestStates_RacingEventDriven.as.
//
// Spawns a small entity hosting an SM with two event-driven transitions
// racing on Idle:
//
//     Idle -+-> DestA   (SlowDelaySeconds, declared FIRST)
//           `-> DestB   (FastDelaySeconds, declared SECOND, should win)
//
// The visual station and the headless autotest both use this same actor
// the autotest spawns it with default fast timings, while the gym station
// spawns it with watchable timings.
//
// PASS = Counter_DestB == 1, Counter_DestA == 0 (fast/second-declared wins).
// FAIL = Counter_DestA == 1, Counter_DestB == 0 (state evaluator Break'd on
//        ToDestA's Undetermined and never inspected ToDestB).

// Composes the actor's state machine, on the server only.
class UCk_SmTest_RacingEventDriven_GymActor_EntityScript : UCk_EntityScript_WithActor_UE
{
    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        if (System::IsServer() == false)
        { return ECk_EntityScript_ConstructionFlow::Finished; }

        auto Actor = Cast<ACk_SmTest_RacingEventDriven_GymActor>(DoGet_Actor());
        InHandle.Set_DebugName(n"RacingSmEntity");
        Actor.SmEntity = InHandle;
        Actor.SmHandle = UCk_Utils_StateMachine_UE::Add(InHandle, FCk_StateMachine_Spec(UCk_SmTest_Racing_State_Idle));

        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}

UCLASS(Blueprintable)
class ACk_SmTest_RacingEventDriven_GymActor : AActor
{
    default bReplicates = true;
    default bReplicateUsingRegisteredSubObjectList = true;
    default bAlwaysRelevant = true;

    // ========================================================================
    // COMPONENTS
    // ========================================================================

    UPROPERTY(DefaultComponent)
    UStaticMeshComponent Mesh;
    default Mesh.StaticMesh = Cast<UStaticMesh>(
        utils_i_o::LoadAssetByName("Cube1", ECk_AssetSearchScope::Engine,
        ECk_AssetSearchStrategy::ExactOnly)._Asset);
    default Mesh.CollisionEnabled = ECollisionEnabled::NoCollision;

    UPROPERTY(DefaultComponent)
    UTextRenderComponent ResultText;
    default ResultText.RelativeLocation = FVector(0.0f, 0.0f, 120.0f);
    default ResultText.SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    default ResultText.WorldSize = 40.0f;
    default ResultText.TextRenderColor = FColor::White;

    UPROPERTY(DefaultComponent)
    UTextRenderComponent DetailText;
    default DetailText.RelativeLocation = FVector(0.0f, 0.0f, 60.0f);
    default DetailText.SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    default DetailText.WorldSize = 14.0f;
    default DetailText.TextRenderColor = FColor::White;

    UPROPERTY(DefaultComponent)
    UCk_EntityBridge_ActorComponent_UE EntityBridge;

    // ========================================================================
    // CONFIGURATION
    // ========================================================================

    // Slow path delay - added FIRST. With the bug, only this path's timer
    // ever starts ticking; it Pass'es at this delay and DestA wins.
    UPROPERTY(ExposeOnSpawn)
    float32 SlowDelaySeconds = 0.5f;

    // Fast path delay - added SECOND. After the framework fix this timer
    // starts alongside the slow path, fires first, and DestB wins.
    UPROPERTY(ExposeOnSpawn)
    float32 FastDelaySeconds = 0.1f;

    // Settle window before the actor reports its verdict to the station
    // display. Must be > SlowDelaySeconds so we observe the bug-path outcome.
    UPROPERTY(ExposeOnSpawn)
    float32 SettleSeconds = 0.8f;

    UPROPERTY(ExposeOnSpawn)
    FCk_Handle StationHandle;

    // ========================================================================
    // COUNTERS - bumped by the SM tasks via the SmRacing_Registry namespace.
    // ========================================================================

    UPROPERTY() int32 Counter_DestA = 0;
    UPROPERTY() int32 Counter_DestB = 0;

    UPROPERTY() FCk_Handle SmEntity;
    UPROPERTY() FCk_Handle_StateMachine SmHandle;

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();
        ResultText.SetText(ck::Text("SM Racing Event-Driven"));
        ResultText.SetTextRenderColor(FColor::White);
        DetailText.SetText(ck::Text("Race in progress..."));

        if (HasAuthority() == false)
        { return; }

        // Spawn the actor's entity; its script adds the SM. The
        // states' conditions read SlowDelaySeconds/FastDelaySeconds from
        // this actor when they DoEnterCondition.
        EntityBridge.Request_SpawnLinkedEntity(UCk_SmTest_RacingEventDriven_GymActor_EntityScript);

        System::SetTimer(this, n"OnSettled", SettleSeconds, false);
    }

    UFUNCTION()
    private void OnSettled()
    {
        UpdateDisplay();
    }

    // ========================================================================
    // VERDICT
    // ========================================================================

    private bool Race_OK() const
    {
        return Counter_DestB == 1 && Counter_DestA == 0;
    }

    private void UpdateDisplay()
    {
        auto OK = Race_OK();
        auto StatusLabel = OK ? "PASS" : "FAIL";
        auto StatusColor = OK ? FColor::Green : FColor::Red;
        auto TraceColor  = OK ? FLinearColor::Green : FLinearColor::Red;
        auto TraceDuration = OK ? 5.0f : 10.0f;

        auto ReportLine = f"DestA={Counter_DestA} DestB={Counter_DestB}";

        ResultText.SetText(ck::Text(StatusLabel));
        ResultText.SetTextRenderColor(StatusColor);
        DetailText.SetText(ck::Text(ReportLine));
        DetailText.SetTextRenderColor(OK ? FColor::White : FColor::Red);

        ck::Trace(f"[SmRacing] {StatusLabel}: {ReportLine}",
            n"SmRacing", TraceDuration, TraceColor);

        if (ck::IsValid(StationHandle))
        {
            auto VerdictLabel = OK ? "PASS - fix is live" : "FAIL - bug reproduced (slow first-declared transition won)";

            auto Title = f"RACING EVENT-DRIVEN TRANSITIONS - {StatusLabel}";

            auto Setup =
                FString("State with two outgoing event-driven transitions:\n")
                + f"  Idle -+-> DestA  (SlowDelay={SlowDelaySeconds}s, declared FIRST)\n"
                + f"        `-> DestB  (FastDelay={FastDelaySeconds}s, declared SECOND)\n"
                + "\n"
                + "Both transitions are pure timer-event-driven conditions.\n"
                + "DestB's timer fires first and SHOULD win - but the state\n"
                + "evaluator Break's on the first Undetermined transition,\n"
                + "so DestA's slow timer is the only one that ever starts\n"
                + "ticking. DestA wins by default at its slower delay.\n"
                + "\n"
                + "PASS = DestB entered exactly once.\n"
                + "FAIL = DestA entered (the slower, first-declared sibling\n"
                + "       won because the evaluator never inspected DestB).\n"
                + "\n";

            auto Counts =
                f"Counters after {SettleSeconds}s settle:\n"
                + f"  DestA={Counter_DestA} DestB={Counter_DestB}\n"
                + f"  Verdict: {VerdictLabel}\n";

            auto Description = Setup + Counts;

            auto& Fragment = StationHandle.AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
            Fragment.Title = FText::FromString(Title);
            Fragment.Description = FText::FromString(Description);
            Fragment.Instructions = FText::FromString("panel [8] Racing: event-driven");
        }
    }
};

// ============================================================================
