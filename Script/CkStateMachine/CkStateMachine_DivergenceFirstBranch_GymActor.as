// ============================================================================
// SM DIVERGENCE-FIRST-BRANCH DUPLICATE-TASK REGRESSION - GYM ACTOR
// ============================================================================
//
// Driver actor for CkStateMachine_TestStates_DivergenceFirstBranch.as.
//
// Test plan:
//   Pass A: AddOrderLeftFirst = true,  PaymentChoice = Left
//           Run one cycle. Expect each task exactly 1x. With the bug,
//           Counter_Left == 2 (Left is the first-added branch AND the
//           chosen branch).
//
//   Pass B: AddOrderLeftFirst = false, PaymentChoice = Right
//           Run one cycle. Expect each task exactly 1x. With the bug,
//           Counter_Right == 2 (Right is the first-added branch AND the
//           chosen branch).
//
// Both passes must be clean for a PASS verdict. Each pass spawns its own
// entity / wrapper SM; cycles are deterministic because the divergence
// transitions are gated by polled checks tied to PaymentChoice.

UCLASS(Blueprintable)
class ACk_SmTest_DivergenceFirstBranch_GymActor : ACk_SmTest_GymSourceActor
{
    default bReplicates = true;
    default bAlwaysRelevant = true;
    // UE 5.7: subobjects (SM replication) register via the registered-subobject list; the
    // legacy path raises an ensure headlessly, which poisons the AutoTest verdict.
    default bReplicateUsingRegisteredSubObjectList = true;

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

    // ========================================================================
    // CONFIGURATION
    // ========================================================================

    // Per-pass settle window. The vacuous chain Enter -> Idle -> Branch is
    // a transition per frame, but the entity-construction async hop plus the
    // per-state SM graph walk under AutoTest load can stretch the cycle to
    // ~600-800ms. 1.5s matches the timed sibling gym
    // (CkStateMachine_DivergenceFirstBranchTimed_GymActor) which has run
    // reliably with this value; under PIE the cube still appears briefly
    // before tear-down.
    UPROPERTY(ExposeOnSpawn)
    float32 PerPassSettleSeconds = 1.5f;

    // Station entity - receives the FCkGym_Station_TitleAndDescription
    // fragment for the demo display panel.
    UPROPERTY(ExposeOnSpawn)
    FCk_Handle StationHandle;

    // ========================================================================
    // STATE READ BY THE SM
    // ========================================================================

    // Display-compatible authored values; callbacks use the private active
    // pass and its accepted source root instead of these mutable properties.
    UPROPERTY()
    bool AddOrderLeftFirst = true;

    // Read by the polled payment-method conditions on the divergence
    // transitions. Mutated between passes to pin which branch fires.
    UPROPERTY()
    ECk_SmTest_DivergenceFirstBranch_PaymentChoice PaymentChoice = ECk_SmTest_DivergenceFirstBranch_PaymentChoice::Left;

    // ========================================================================
    // PER-PASS COUNTERS
    // ========================================================================

    UPROPERTY() int32 Counter_Enter = 0;
    UPROPERTY() int32 Counter_Idle = 0;
    UPROPERTY() int32 Counter_Branch = 0;
    UPROPERTY() int32 Counter_Left = 0;
    UPROPERTY() int32 Counter_Right = 0;
    UPROPERTY() int32 Counter_Finish = 0;

    UFUNCTION()
    void Increment_Counter(FName InLabel)
    {
        if      (InLabel == n"Enter")  { Counter_Enter  += 1; }
        else if (InLabel == n"Idle")   { Counter_Idle   += 1; }
        else if (InLabel == n"Branch") { Counter_Branch += 1; }
        else if (InLabel == n"Left")   { Counter_Left   += 1; }
        else if (InLabel == n"Right")  { Counter_Right  += 1; }
        else if (InLabel == n"Finish") { Counter_Finish += 1; }
    }

    UFUNCTION()
    int32 Get_Counter(FName InLabel) const
    {
        if      (InLabel == n"Enter")  { return Counter_Enter;  }
        else if (InLabel == n"Idle")   { return Counter_Idle;   }
        else if (InLabel == n"Branch") { return Counter_Branch; }
        else if (InLabel == n"Left")   { return Counter_Left;   }
        else if (InLabel == n"Right")  { return Counter_Right;  }
        else if (InLabel == n"Finish") { return Counter_Finish; }
        return 0;
    }

    UFUNCTION()
    void Reset_Counters()
    {
        Counter_Enter = 0;
        Counter_Idle = 0;
        Counter_Branch = 0;
        Counter_Left = 0;
        Counter_Right = 0;
        Counter_Finish = 0;
    }

    // ========================================================================
    // PASS SNAPSHOTS - captured at the end of each pass so the second pass'
    // counters don't overwrite the first pass' verdict before the display
    // is updated.
    // ========================================================================

    UPROPERTY() int32 Snap_A_Enter = 0;
    UPROPERTY() int32 Snap_A_Idle = 0;
    UPROPERTY() int32 Snap_A_Branch = 0;
    UPROPERTY() int32 Snap_A_Left = 0;
    UPROPERTY() int32 Snap_A_Right = 0;
    UPROPERTY() int32 Snap_A_Finish = 0;

    UPROPERTY() int32 Snap_B_Enter = 0;
    UPROPERTY() int32 Snap_B_Idle = 0;
    UPROPERTY() int32 Snap_B_Branch = 0;
    UPROPERTY() int32 Snap_B_Left = 0;
    UPROPERTY() int32 Snap_B_Right = 0;
    UPROPERTY() int32 Snap_B_Finish = 0;

    // ========================================================================
    // ENTITIES
    // ========================================================================

    // One spawn per pass - each Add() runs the graph walk + initial-state
    // entry once. The two passes share counter fields but snapshot before
    // resetting so we can compare.
    UPROPERTY() FCk_Handle PassAEntity;
    UPROPERTY() FCk_Handle_StateMachine PassASmHandle;

    UPROPERTY() FCk_Handle PassBEntity;
    UPROPERTY() FCk_Handle_StateMachine PassBSmHandle;

    private int32 _ActivePass = 0; // 1 = A, 2 = B; set before requesting a pass
    private bool _PassAAdded = false;
    private bool _PassBAdded = false;
    private bool _PassATimerArmed = false;
    private bool _PassBTimerArmed = false;
    private bool _SourceRunFinished = false;

    UFUNCTION(BlueprintPure)
    bool Get_SourceRunFinished() const
    { return _SourceRunFinished && !Get_SourceClosed(); }

    bool TryGet_ActiveConfig(FCk_Handle InContext, bool& OutLeftFirst,
        ECk_SmTest_DivergenceFirstBranch_PaymentChoice& OutChoice)
    {
        OutLeftFirst = false;
        OutChoice = ECk_SmTest_DivergenceFirstBranch_PaymentChoice::Left;
        if (Get_SourceClosed() || !Get_HasCurrentSourceContext() ||
            _SourceRunFinished) { return false; }
        auto Roots = Get_SourceRoots();
        if (_ActivePass == 1 && Roots.Num() == 1 &&
            Get_SourceConstructedCount() == 1 &&
            InContext == PassAEntity && Roots[0] == PassAEntity)
        {
            if (_PassAAdded &&
                (ck::Is_NOT_Valid(PassASmHandle) ||
                 FCk_Handle(PassASmHandle) != PassAEntity ||
                 !utils_state_machine::Has(PassAEntity)))
            { Request_CloseSource(); return false; }
            if (ck::IsValid(PassASmHandle) && FCk_Handle(PassASmHandle) != PassAEntity)
            { Request_CloseSource(); return false; }
            OutLeftFirst = true;
            OutChoice = ECk_SmTest_DivergenceFirstBranch_PaymentChoice::Left;
            return true;
        }
        if (_ActivePass == 2 && Roots.Num() == 2 && Roots[0] != Roots[1] &&
            Get_SourceConstructedCount() == 2 &&
            Roots[0] == PassAEntity && InContext == PassBEntity &&
            Roots[1] == PassBEntity)
        {
            if (_PassBAdded &&
                (ck::Is_NOT_Valid(PassBSmHandle) ||
                 FCk_Handle(PassBSmHandle) != PassBEntity ||
                 !utils_state_machine::Has(PassBEntity)))
            { Request_CloseSource(); return false; }
            if (ck::IsValid(PassBSmHandle) && FCk_Handle(PassBSmHandle) != PassBEntity)
            { Request_CloseSource(); return false; }
            OutLeftFirst = false;
            OutChoice = ECk_SmTest_DivergenceFirstBranch_PaymentChoice::Right;
            return true;
        }
        // Late callbacks from the retained A forest are deliberately inert in B.
        if (_ActivePass == 2 && InContext == PassAEntity) { return false; }
        Request_CloseSource();
        return false;
    }

    private bool HasExactPass(int32 Pass)
    {
        if (Get_SourceClosed() || _ActivePass != Pass ||
            !Get_HasCurrentSourceContext()) { return false; }
        auto Roots = Get_SourceRoots();
        if (Roots.Num() != Pass || Get_SourceConstructedCount() != Pass ||
            Roots[0] != PassAEntity ||
            (Pass == 2 && (Roots[0] == Roots[1] || Roots[1] != PassBEntity)))
        { Request_CloseSource(); return false; }
        auto Root = Pass == 1 ? PassAEntity : PassBEntity;
        auto Sm = Pass == 1 ? PassASmHandle : PassBSmHandle;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Root) ||
            ck::Is_NOT_Valid(Root) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                Root, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            ck::Is_NOT_Valid(Sm) || FCk_Handle(Sm) != Root ||
            !utils_state_machine::Has(Root))
        { Request_CloseSource(); return false; }
        return true;
    }

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();
        ResultText.SetText(ck::Text("SM Divergence FirstBranch Regression"));
        ResultText.SetTextRenderColor(FColor::White);
        DetailText.SetText(ck::Text("Pass A starting..."));

        if (HasAuthority() == false)
        { return; }

        Request_PrimarySource(n"OnPrimaryConstructed");
    }

    UFUNCTION()
    private void OnPrimaryConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        if (Get_SourceClosed() || !TryAccept_PrimarySource(InEntityScriptHandle))
        { return; }
        if (Get_SourcePrimary() != FCk_Handle(InEntityScriptHandle))
        { Request_CloseSource(); return; }
        StartPassA();
    }

    UFUNCTION()
    private void StartPassA()
    {
        if (Get_SourceClosed() || !Math::IsFinite(PerPassSettleSeconds) ||
            PerPassSettleSeconds <= 0.0f)
        { Request_CloseSource(); return; }
        _ActivePass = 1;
        AddOrderLeftFirst = true;
        PaymentChoice = ECk_SmTest_DivergenceFirstBranch_PaymentChoice::Left;
        Reset_Counters();
        if (!Request_PassSource(n"OnPassAConstructed")) { return; }

        // Settle timer is started in OnPassAConstructed - after the SM is actually
        // added - so PerPassSettleSeconds measures the SM-chain duration only, not
        // the async entity-spawn overhead (~250ms under PIE load).
    }

    UFUNCTION()
    private void OnPassAConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        if (Get_SourceClosed() || !TryAccept_PassSource(InEntityScriptHandle))
        { return; }
        PassAEntity = FCk_Handle(InEntityScriptHandle);
        auto Roots = Get_SourceRoots();
        if (_ActivePass != 1 || !Get_HasCurrentSourceContext() ||
            Roots.Num() != 1 || Roots[0] != PassAEntity)
        { Request_CloseSource(); return; }
        PassAEntity.Set_DebugName(n"PassA");
        if (Get_SourceClosed() || !Get_HasCurrentSourceContext()) { return; }
        PassASmHandle = UCk_Utils_StateMachine_UE::Add(
            PassAEntity,
            FCk_StateMachine_Spec(UCk_SmTest_Divergence_ParentState));
        _PassAAdded = true;
        if (!HasExactPass(1) || !Math::IsFinite(PerPassSettleSeconds) ||
            PerPassSettleSeconds <= 0.0f)
        { Request_CloseSource(); return; }
        _PassATimerArmed = true;
        System::SetTimer(this, n"VerifyPassA", PerPassSettleSeconds, false);
    }

    UFUNCTION()
    private void VerifyPassA()
    {
        if (!_PassATimerArmed || !HasExactPass(1)) { return; }
        _PassATimerArmed = false;
        Snap_A_Enter  = Counter_Enter;
        Snap_A_Idle   = Counter_Idle;
        Snap_A_Branch = Counter_Branch;
        Snap_A_Left   = Counter_Left;
        Snap_A_Right  = Counter_Right;
        Snap_A_Finish = Counter_Finish;

        StartPassB();
    }

    UFUNCTION()
    private void StartPassB()
    {
        if (Get_SourceClosed() || !HasExactPass(1) ||
            !Math::IsFinite(PerPassSettleSeconds) ||
            PerPassSettleSeconds <= 0.0f)
        { Request_CloseSource(); return; }
        _ActivePass = 2;
        AddOrderLeftFirst = false;
        PaymentChoice = ECk_SmTest_DivergenceFirstBranch_PaymentChoice::Right;
        Reset_Counters();
        if (!Request_PassSource(n"OnPassBConstructed")) { return; }

        // Settle timer started in OnPassBConstructed - see StartPassA comment.
    }

    UFUNCTION()
    private void OnPassBConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        if (Get_SourceClosed() || !TryAccept_PassSource(InEntityScriptHandle))
        { return; }
        PassBEntity = FCk_Handle(InEntityScriptHandle);
        auto Roots = Get_SourceRoots();
        if (_ActivePass != 2 || !Get_HasCurrentSourceContext() ||
            Roots.Num() != 2 || Roots[0] != PassAEntity ||
            Roots[1] != PassBEntity || Roots[0] == Roots[1])
        { Request_CloseSource(); return; }
        PassBEntity.Set_DebugName(n"PassB");
        if (Get_SourceClosed() || !Get_HasCurrentSourceContext()) { return; }
        PassBSmHandle = UCk_Utils_StateMachine_UE::Add(
            PassBEntity,
            FCk_StateMachine_Spec(UCk_SmTest_Divergence_ParentState));
        _PassBAdded = true;
        if (!HasExactPass(2) || !Math::IsFinite(PerPassSettleSeconds) ||
            PerPassSettleSeconds <= 0.0f)
        { Request_CloseSource(); return; }
        _PassBTimerArmed = true;
        System::SetTimer(this, n"VerifyPassB", PerPassSettleSeconds, false);
    }

    UFUNCTION()
    private void VerifyPassB()
    {
        if (!_PassBTimerArmed || !HasExactPass(2)) { return; }
        _PassBTimerArmed = false;
        Snap_B_Enter  = Counter_Enter;
        Snap_B_Idle   = Counter_Idle;
        Snap_B_Branch = Counter_Branch;
        Snap_B_Left   = Counter_Left;
        Snap_B_Right  = Counter_Right;
        Snap_B_Finish = Counter_Finish;

        UpdateDisplay();
        if (HasExactPass(2)) { _SourceRunFinished = true; }
    }

    // ========================================================================
    // VERDICT
    // ========================================================================

    private bool PassA_OK()
    {
        // Pass A: AddLeftFirst + PaymentLeft. Expected path:
        //   Enter -> Idle -> Branch -> Left -> Finish
        // Each task should fire exactly once. Right (the not-chosen branch)
        // should never fire.
        if (Snap_A_Enter  != 1) { return false; }
        if (Snap_A_Idle   != 1) { return false; }
        if (Snap_A_Branch != 1) { return false; }
        if (Snap_A_Left   != 1) { return false; }
        if (Snap_A_Right  != 0) { return false; }
        if (Snap_A_Finish != 1) { return false; }
        return true;
    }

    private bool PassB_OK()
    {
        // Pass B: AddRightFirst + PaymentRight. Expected path:
        //   Enter -> Idle -> Branch -> Right -> Finish
        if (Snap_B_Enter  != 1) { return false; }
        if (Snap_B_Idle   != 1) { return false; }
        if (Snap_B_Branch != 1) { return false; }
        if (Snap_B_Left   != 0) { return false; }
        if (Snap_B_Right  != 1) { return false; }
        if (Snap_B_Finish != 1) { return false; }
        return true;
    }

    private void UpdateDisplay()
    {
        if (!HasExactPass(2)) { return; }
        auto OkA = PassA_OK();
        auto OkB = PassB_OK();
        auto AllOk = OkA && OkB;

        auto StatusLabel = AllOk ? "PASS" : "FAIL";
        auto StatusColor = AllOk ? FColor::Green : FColor::Red;
        auto TraceColor  = AllOk ? FLinearColor::Green : FLinearColor::Red;
        auto TraceDuration = AllOk ? 5.0f : 10.0f;

        auto ReportLine = f"A[E={Snap_A_Enter} I={Snap_A_Idle} B={Snap_A_Branch} L={Snap_A_Left} R={Snap_A_Right} F={Snap_A_Finish}]"
            + f" B[E={Snap_B_Enter} I={Snap_B_Idle} B={Snap_B_Branch} L={Snap_B_Left} R={Snap_B_Right} F={Snap_B_Finish}]";

        ResultText.SetText(ck::Text(StatusLabel));
        ResultText.SetTextRenderColor(StatusColor);
        DetailText.SetText(ck::Text(ReportLine));
        DetailText.SetTextRenderColor(AllOk ? FColor::White : FColor::Red);

        ck::Trace(f"[SmDivergenceFirstBranch] {StatusLabel}: {ReportLine}",
            n"SmDivergenceFirstBranch", TraceDuration, TraceColor);

        FCk_Handle StationContext;
        if (UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                StationHandle, StationContext) &&
            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                StationHandle) && ck::IsValid(StationHandle) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                StationHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            utils_entity_lifetime::Has_LifetimeOwner(StationHandle) &&
            utils_entity_lifetime::Get_WorldForEntity(StationHandle) == GetWorld() &&
            StationHandle.Has_Fragment(FCkGym_Station_TitleAndDescription))
        {
            auto Title = f"DIVERGENCE FIRST-BRANCH - {StatusLabel}";

            auto Setup =
                FString("Sub-SM: Enter -> Idle -> Branch -+-> Left  -> Finish\n")
                + "                            `-> Right -/\n"
                + "\n"
                + "Pass A: AddLeftFirst + PaymentLeft (expect Left=1)\n"
                + "Pass B: AddRightFirst + PaymentRight (expect Right=1)\n"
                + "\n"
                + "PASS = each task fires exactly 1x per pass.\n"
                + "BUG  = first-added branch task fires 2x.\n"
                + "\n";

            auto LabelA = OkA ? "OK" : "FAIL";
            auto LabelB = OkB ? "OK" : "FAIL";

            auto Counts =
                f"Pass A (AddLeftFirst, PaymentLeft):\n"
                + f"  Enter={Snap_A_Enter} Idle={Snap_A_Idle} Branch={Snap_A_Branch}\n"
                + f"  Left={Snap_A_Left} Right={Snap_A_Right} Finish={Snap_A_Finish}\n"
                + f"  -> {LabelA}\n"
                + "\n"
                + f"Pass B (AddRightFirst, PaymentRight):\n"
                + f"  Enter={Snap_B_Enter} Idle={Snap_B_Idle} Branch={Snap_B_Branch}\n"
                + f"  Left={Snap_B_Left} Right={Snap_B_Right} Finish={Snap_B_Finish}\n"
                + f"  -> {LabelB}\n";

            auto Description = Setup + Counts;

            if (AllOk == false)
            {
                auto Why = FString("\nWhy failed:\n");
                if (OkA == false && Snap_A_Left >= 2)
                {
                    Why = Why
                        + "- Pass A: Left task fired 2x (expected 1).\n"
                        + "  Left was the first-added AND chosen branch.\n";
                }
                if (OkB == false && Snap_B_Right >= 2)
                {
                    Why = Why
                        + "- Pass B: Right task fired 2x (expected 1).\n"
                        + "  Right was the first-added AND chosen branch.\n"
                        + "  Doubling tracks add-order, confirming the bug.\n";
                }
                Description = Description + Why;
            }

            if (!HasExactPass(2)) { return; }
            auto& Fragment = StationHandle.Get_Fragment(FCkGym_Station_TitleAndDescription);
            Fragment.Title = FText::FromString(Title);
            Fragment.Description = FText::FromString(Description);
            Fragment.Instructions = FText::FromString("panel [6] Divergence: first branch");
        }
    }
};

// ============================================================================
