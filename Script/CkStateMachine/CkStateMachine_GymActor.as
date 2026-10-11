// ============================================================================
// STATE MACHINE GYM ACTOR
// ============================================================================

// Spawnable actor that demonstrates CkStateMachine functionality.
// Creates a state machine from the initial state only. The graph is built
// dynamically by state scripts (Idle -> Patrol -> Alert -> Idle).
// Visual feedback via TextRenderComponent.
//
// When EnablePauseResume is true, the SM pauses after PauseAfterTransitions
// transitions and resumes after PauseDuration seconds.

UCLASS(Blueprintable)
class ACk_SmTest_GymActor : ACk_SmTest_GymSourceActor
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
    UTextRenderComponent StateText;
    default StateText.RelativeLocation = FVector(0.0f, 0.0f, 100.0f);
    default StateText.SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    default StateText.WorldSize = 50.0f;
    default StateText.TextRenderColor = FColor::White;

    UPROPERTY(DefaultComponent)
    UTextRenderComponent InfoText;
    default InfoText.RelativeLocation = FVector(0.0f, 0.0f, 150.0f);
    default InfoText.SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    default InfoText.WorldSize = 30.0f;
    default InfoText.TextRenderColor = FColor::Orange;

    // ========================================================================
    // CONFIGURATION
    // ========================================================================

    UPROPERTY(ExposeOnSpawn)
    float32 CycleDuration = 2.0f;

    UPROPERTY(ExposeOnSpawn)
    bool EnablePauseResume = false;

    UPROPERTY(ExposeOnSpawn)
    int32 PauseAfterTransitions = 6;

    UPROPERTY(ExposeOnSpawn)
    float32 PauseDuration = 3.0f;

    UPROPERTY(ExposeOnSpawn)
    TSubclassOf<UCk_SmState_EntityScript> InitialStateClass;

    // ========================================================================
    // STATE
    // ========================================================================

    UPROPERTY()
    FCk_Handle_StateMachine SmHandle;

    UPROPERTY()
    int32 TransitionCount = 0;

    UPROPERTY()
    bool IsPaused = false;

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto ModeLabel = EnablePauseResume ? "Pause/Resume + State Graph" : "State Graph Auto";
        InfoText.SetText(ck::Text(f"SM: {ModeLabel}"));

        if (HasAuthority()) { Request_PrepareSource(); }
        else { Request_RefreshSourcePresentation(); }
    }

    bool Validate_SourceConfiguration() override
    {
        return Math::IsFinite(CycleDuration) && CycleDuration > 0.0f &&
            (!EnablePauseResume ||
             (PauseAfterTransitions > 0 && Math::IsFinite(PauseDuration) &&
              PauseDuration > 0.0f));
    }

    void DoStart_Source() override
    { Request_PassSource(n"OnEntityConstructed"); }

    bool TryGet_SourcePresentation(FString&out OutTitle, FString&out OutDetail,
        FColor&out OutColor) override
    {
        OutTitle = "";
        OutDetail = "";
        OutColor = FColor::White;
        if (!Get_HasCurrentSourceContext() || !Get_SourceStarted())
        { return false; }
        OutTitle = StateText.Text.ToString();
        OutDetail = InfoText.Text.ToString();
        OutColor = StateText.TextRenderColor;
        return true;
    }

    void Apply_SourcePresentation(const FCkStateMachineGym_StationFrame&in Frame) override
    {
        StateText.SetText(FText::FromString(Frame.Title));
        StateText.SetTextRenderColor(Frame.Color);
        InfoText.SetText(FText::FromString(Frame.Detail));
        InfoText.SetTextRenderColor(Frame.Color);
    }

    // ========================================================================
    // ECS SETUP
    // ========================================================================

    UFUNCTION()
    private void OnEntityConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        if (!TryAccept_PassSource(InEntityScriptHandle)) { return; }

        auto OwnerHandle = FCk_Handle(InEntityScriptHandle);
        auto Roots = Get_SourceRoots();
        if (!Get_HasCurrentSourceContext() || Roots.Num() != 1 ||
            Roots[0] != OwnerHandle || Get_SourceConstructedCount() != 1)
        { Request_CloseSource(); return; }
        OwnerHandle.Set_DebugName(n"StateMachine");
        if (!Get_HasCurrentSourceContext()) { return; }
        auto StateClass = InitialStateClass.IsValid()
            ? InitialStateClass
            : TSubclassOf<UCk_SmState_EntityScript>(UCk_SmTest_State_Idle);
        auto Spec = FCk_StateMachine_Spec(StateClass);
        Spec.Set_AutoStart(ECk_SmAutoStart::Disabled);
        SmHandle = UCk_Utils_StateMachine_UE::Add(
            OwnerHandle, Spec);
        if (!Get_HasCurrentSourceContext() || ck::Is_NOT_Valid(SmHandle) ||
            FCk_Handle(SmHandle) != OwnerHandle ||
            !utils_state_machine::Has(OwnerHandle))
        { Request_CloseSource(); return; }
        BindSignals();
        if (!Get_HasCurrentSourceContext()) { return; }
        SmHandle.Request_Start();
    }

    // ========================================================================
    // SIGNAL BINDING
    // ========================================================================

    private void BindSignals()
    {
        FCk_Delegate_Sm_OnStateChanged OnStateChangedDelegate;
        OnStateChangedDelegate.BindUFunction(this, n"OnSmStateChanged");
        SmHandle.BindTo_OnStateChanged(OnStateChangedDelegate);

        FCk_Delegate_Sm_OnStarted OnStartedDelegate;
        OnStartedDelegate.BindUFunction(this, n"OnSmStarted");
        SmHandle.BindTo_OnStarted(OnStartedDelegate);

        FCk_Delegate_Sm_OnStopped OnStoppedDelegate;
        OnStoppedDelegate.BindUFunction(this, n"OnSmStopped");
        SmHandle.BindTo_OnStopped(OnStoppedDelegate);
    }

    // ========================================================================
    // SIGNAL HANDLERS
    // ========================================================================

    UFUNCTION()
    private void OnSmStateChanged(
        FCk_Handle_StateMachine InHandle,
        FCk_Sm_Payload_OnStateChanged InPayload)
    {
        if (Get_SourceClosed()) { return; }
        if (!Get_HasCurrentSourceContext() ||
            ck::Is_NOT_Valid(SmHandle) || ck::Is_NOT_Valid(InHandle) ||
            InHandle != SmHandle ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(FCk_Handle(SmHandle)) ||
            !utils_state_machine::Has(FCk_Handle(SmHandle)))
        { Request_CloseSource(); return; }
        TransitionCount++;

        auto NewStateClass = InPayload.Get_NewStateClass();
        auto StateName = GetFriendlyStateName(NewStateClass);

        StateText.SetText(ck::Text(StateName));
        StateText.SetTextRenderColor(GetStateColor(NewStateClass));
        InfoText.SetText(ck::Text(f"Transitions: {TransitionCount}"));

        if (EnablePauseResume && TransitionCount >= PauseAfterTransitions && !IsPaused)
        {
            DoPause();
        }
    }

    UFUNCTION()
    private void OnSmStarted(
        FCk_Handle_StateMachine InHandle,
        FCk_Sm_Payload_OnStarted InPayload)
    {
        if (Get_SourceClosed()) { return; }
        if (!Get_HasCurrentSourceContext() ||
            ck::Is_NOT_Valid(SmHandle) || ck::Is_NOT_Valid(InHandle) ||
            InHandle != SmHandle ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(FCk_Handle(SmHandle)) ||
            !utils_state_machine::Has(FCk_Handle(SmHandle)))
        { Request_CloseSource(); return; }
        StateText.SetText(ck::Text("Idle"));
        StateText.SetTextRenderColor(FColor::Blue);
    }

    UFUNCTION()
    private void OnSmStopped(
        FCk_Handle_StateMachine InHandle,
        FCk_Sm_Payload_OnStopped InPayload)
    {
        if (Get_SourceClosed()) { return; }
        if (!Get_HasCurrentSourceContext() ||
            ck::Is_NOT_Valid(SmHandle) || ck::Is_NOT_Valid(InHandle) ||
            InHandle != SmHandle ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(FCk_Handle(SmHandle)) ||
            !utils_state_machine::Has(FCk_Handle(SmHandle)))
        { Request_CloseSource(); return; }
        StateText.SetText(ck::Text("STOPPED"));
        StateText.SetTextRenderColor(FColor(128, 128, 128));
    }

    // ========================================================================
    // PAUSE / RESUME
    // ========================================================================

    private void DoPause()
    {
        if (Get_SourceClosed()) { return; }
        if (!Get_HasCurrentSourceContext() || ck::Is_NOT_Valid(SmHandle) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(FCk_Handle(SmHandle)) ||
            !utils_state_machine::Has(FCk_Handle(SmHandle)))
        { Request_CloseSource(); return; }
        SmHandle.Request_Pause();
        if (!Get_HasCurrentSourceContext()) { return; }
        IsPaused = true;

        StateText.SetText(ck::Text("PAUSED"));
        StateText.SetTextRenderColor(FColor(255, 165, 0));
        InfoText.SetText(ck::Text(f"Resuming in {PauseDuration}s..."));

        System::SetTimer(this, n"DoResume", PauseDuration, false);
    }

    UFUNCTION()
    private void DoResume()
    {
        if (Get_SourceClosed()) { return; }
        if (!Get_HasCurrentSourceContext() || ck::Is_NOT_Valid(SmHandle) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(FCk_Handle(SmHandle)) ||
            !utils_state_machine::Has(FCk_Handle(SmHandle)))
        { Request_CloseSource(); return; }
        SmHandle.Request_Resume();
        if (!Get_HasCurrentSourceContext()) { return; }
        IsPaused = false;
        TransitionCount = 0;

        InfoText.SetText(ck::Text("Resumed! Count reset."));
    }

    // ========================================================================
    // HELPERS
    // ========================================================================

    private FString GetFriendlyStateName(TSubclassOf<UCk_SmState_EntityScript> InStateClass) const
    {
        // Simple states
        if (InStateClass == UCk_SmTest_State_Idle)   { return "Idle"; }
        if (InStateClass == UCk_SmTest_State_Patrol) { return "Patrol"; }
        if (InStateClass == UCk_SmTest_State_Alert)  { return "Alert"; }

        // Complex states
        if (InStateClass == UCk_SmTest_Complex_State_Idle)   { return "Idle"; }
        if (InStateClass == UCk_SmTest_Complex_State_Patrol) { return "Patrol"; }
        if (InStateClass == UCk_SmTest_Complex_State_Chase)  { return "Chase"; }
        if (InStateClass == UCk_SmTest_Complex_State_Attack) { return "Attack"; }
        if (InStateClass == UCk_SmTest_Complex_State_Search) { return "Search"; }
        if (InStateClass == UCk_SmTest_Complex_State_Flee)   { return "Flee"; }

        // Hierarchical states (parent)
        if (InStateClass == UCk_SmTest_Hier_Parent_Spawn)    { return "Spawn"; }
        if (InStateClass == UCk_SmTest_Hier_Parent_Approach) { return "Approach"; }
        if (InStateClass == UCk_SmTest_Hier_Parent_Engage)   { return "Engage [Combat SM]"; }
        if (InStateClass == UCk_SmTest_Hier_Parent_Retreat)  { return "Retreat"; }
        if (InStateClass == UCk_SmTest_Hier_Parent_Heal)     { return "Heal [Heal SM]"; }
        if (InStateClass == UCk_SmTest_Hier_Parent_Flee)     { return "Flee"; }

        return "Unknown";
    }

    private FColor GetStateColor(TSubclassOf<UCk_SmState_EntityScript> InStateClass) const
    {
        // Simple states
        if (InStateClass == UCk_SmTest_State_Idle)   { return FColor::Blue; }
        if (InStateClass == UCk_SmTest_State_Patrol) { return FColor::Green; }
        if (InStateClass == UCk_SmTest_State_Alert)  { return FColor::Red; }

        // Complex states
        if (InStateClass == UCk_SmTest_Complex_State_Idle)   { return FColor::Blue; }
        if (InStateClass == UCk_SmTest_Complex_State_Patrol) { return FColor::Green; }
        if (InStateClass == UCk_SmTest_Complex_State_Chase)  { return FColor(255, 128, 0); }
        if (InStateClass == UCk_SmTest_Complex_State_Attack) { return FColor::Red; }
        if (InStateClass == UCk_SmTest_Complex_State_Search) { return FColor(128, 0, 255); }
        if (InStateClass == UCk_SmTest_Complex_State_Flee)   { return FColor(255, 255, 0); }

        // Hierarchical states (parent)
        if (InStateClass == UCk_SmTest_Hier_Parent_Spawn)    { return FColor(128, 128, 128); }
        if (InStateClass == UCk_SmTest_Hier_Parent_Approach) { return FColor(0, 128, 255); }
        if (InStateClass == UCk_SmTest_Hier_Parent_Engage)   { return FColor::Red; }
        if (InStateClass == UCk_SmTest_Hier_Parent_Retreat)  { return FColor(255, 255, 0); }
        if (InStateClass == UCk_SmTest_Hier_Parent_Heal)     { return FColor(77, 230, 77); }
        if (InStateClass == UCk_SmTest_Hier_Parent_Flee)     { return FColor(255, 102, 0); }

        return FColor::White;
    }
};

// ============================================================================
