// Language=angelscript

namespace CkAttributeBasic
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
    {
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(n"Gym.Attribute.BasicAttributes");
        Station.AutoSize = true;
        Station.Title = FText::FromString("BASIC ATTRIBUTES - AUTO CLAMPING");
        Station.Description.Add(FText::FromString("Tests float, byte, and vector attributes with auto-clamping."));
        Station.Description.Add(FText::FromString("Auto-cycles through 6 phases every 2s."));
        Station.Description.Add(FText::FromString("All knobs are on the control panel; free-range velocity via console Ck_GymAttribute_SetVelocity X Y Z"));
        auto Transform = FTransform::Identity;
        Transform.SetLocation(FVector(500.0, 0.0, 0.0));
        Transform.SetRotation(FRotator(0.0, 180.0, 0.0).Quaternion());
        Station.Transform = Transform;
        Stations.Add(Station);
        return Stations;
    }

    bool IsLiveEntity(FCk_Handle InHandle)
    {
        return ck::IsValid(InHandle) && !utils_entity_lifetime::Get_IsPendingDestroy(
            InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    bool IsFiniteVelocity(FVector InValue)
    {
        return Math::Abs(InValue.X) < 1.0e150 &&
            Math::Abs(InValue.Y) < 1.0e150 &&
            Math::Abs(InValue.Z) < 1.0e150;
    }

    bool IsValidSample(FCkAttributeGym_BasicSample InSample)
    {
        return InSample.Health >= 0.0 && InSample.Health <= 100.0 &&
            InSample.Armor >= 0 && InSample.Armor <= 255 &&
            IsFiniteVelocity(InSample.Velocity) &&
            InSample.HealthChangeCount >= 0 && InSample.ArmorChangeCount >= 0 &&
            InSample.VelocityChangeCount >= 0 && InSample.ClampedCount >= 0 &&
            InSample.AutoStep >= 0;
    }

    FCk_Handle FindStation()
    {
        auto World = ck::TransientEntity();
        if (!IsLiveEntity(World)) { return FCk_Handle(); }
        auto Matches = utils_entity_tag::ForEach_Entity(World, n"Gym.Attribute.BasicAttributes");
        if (Matches.Num() != 1 || !IsLiveEntity(Matches[0]) ||
            !Matches[0].Has_Fragment(FCkGym_Station_Anchors))
        { return FCk_Handle(); }
        return Matches[0];
    }

    ACk_AttributeGym_GameState Get_State()
    {
        TArray<ACk_AttributeGym_GameState> States;
        GetAllActorsOfClass(ACk_AttributeGym_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0])) { return nullptr; }
        return States[0];
    }

    FCkGym_AutoConfig MakeAutoConfig()
    {
        auto Config = FCkGym_AutoConfig();
        Config.TotalSteps = 6;
        Config.Description = "Tests float, byte, and vector attributes with auto-clamping.";
        Config.GlobalAutoCommand = "panel [T] Auto-cycle phases";
        Config.PerStationAutoCommand = "panel [T] Auto-cycle phases";
        Config.Steps.Add(FCkGym_AutoStep("SetHealth 50, SetArmor 150, SetVelocity mid", 0, 0));
        Config.Steps.Add(FCkGym_AutoStep("SetHealth 95, SetArmor 245, SetVelocity high", 1, 1));
        Config.Steps.Add(FCkGym_AutoStep("TestBoundaries (push past max)", 2, 2));
        Config.Steps.Add(FCkGym_AutoStep("SetHealth 10, SetArmor 20, SetVelocity low", 3, 3));
        Config.Steps.Add(FCkGym_AutoStep("SetHealth -10, SetArmor 0 (push past min)", 4, 4));
        Config.Steps.Add(FCkGym_AutoStep("ResetBasicValues", 5, 5));
        Config.ManualCommands.Add("panel [1] Health preset - [2] Armor preset - [3] Velocity preset");
        Config.ManualCommands.Add("panel [B] Test boundaries - [R] Reset values");
        Config.ManualCommands.Add("console Ck_GymAttribute_SetVelocity [x] [y] [z] (free-range)");
        return Config;
    }
}

//============================================================================
// BASIC ATTRIBUTES ENTITY SCRIPT
//============================================================================

USTRUCT()
struct FBasicAttributesSpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;

    UPROPERTY()
    FString StationName = "";

    UPROPERTY()
    int32 Generation = 0;

    FBasicAttributesSpawnParams(FTransform InTransform, FString InStationName, int32 InGeneration)
    {
        InitialTransform = InTransform;
        StationName = InStationName;
        Generation = InGeneration;
    }
}

class UCk_EntityScript_AttributeGym_BasicAttributes : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn)
    FTransform InitialTransform = FTransform::Identity;

    UPROPERTY(ExposeOnSpawn)
    FString StationName = "BasicAttributes";

    UPROPERTY(ExposeOnSpawn)
    int32 Generation = 0;

    // Attribute handles
    FCk_Handle_FloatAttribute HealthAttribute;
    FCk_Handle_ByteAttribute ArmorAttribute;
    FCk_Handle_VectorAttribute VelocityAttribute;

    // Auto mode
    FCk_Handle_Timer AutoTimer;
    bool AuthorityConstructed = false;
    bool AutoRunning = true;
    int32 AutoStep = 0;
    FCkGym_AutoConfig AutoConfig;

    // Signal counters
    int32 HealthChangeCount = 0;
    int32 ArmorChangeCount = 0;
    int32 VelocityChangeCount = 0;
    int32 ClampedCount = 0;

    //------------------------------------------------------------------------
    // Construction & Initialization
    //------------------------------------------------------------------------

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        AuthorityConstructed = false;
        HealthAttribute = FCk_Handle_FloatAttribute();
        ArmorAttribute = FCk_Handle_ByteAttribute();
        VelocityAttribute = FCk_Handle_VectorAttribute();
        AutoTimer = FCk_Handle_Timer();
        AutoRunning = true;
        AutoStep = 0;
        HealthChangeCount = 0;
        ArmorChangeCount = 0;
        VelocityChangeCount = 0;
        ClampedCount = 0;
        AutoConfig = FCkGym_AutoConfig();
        auto State = CkAttributeBasic::Get_State();
        if (!CkAttributeBasic::IsLiveEntity(InHandle))
        {
            if (ck::IsValid(State))
            { State.Request_FailExpectedServerChild(InHandle, Generation, "Simulation handle invalid"); }
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        auto Mode = utils_net::Get_EntityNetMode(InHandle);
        if ((Mode != ECk_Net_NetModeType::Host &&
             Mode != ECk_Net_NetModeType::ClientAndHost) ||
            !CkAttributeBasic::IsLiveEntity(InHandle) ||
            utils_entity_lifetime::Get_LifetimeOwner(InHandle) != CkAttributeBasic::FindStation() ||
            ck::Is_NOT_Valid(State) || !State.Get_IsExpectedServerChild(InHandle, Generation))
        {
            if (ck::IsValid(State))
            { State.Request_FailExpectedServerChild(InHandle, Generation, "Simulation role or owner invalid"); }
            ck::Error("[AttributeBasic] Rejected non-authority or stale simulation construction");
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        InHandle.Set_DebugName(n"BasicAttributes");
        utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_BasicAttributes");
        auto TimerParams = FCk_Timer_Spec(FCk_Time(2.0f));
        TimerParams.Set_StartingState(ECk_Timer_State::Running)
            .Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        AutoTimer = utils_timer::Add(InHandle, TimerParams);
        if (!CkAttributeBasic::IsLiveEntity(FCk_Handle(AutoTimer)))
        {
            State.Request_FailExpectedServerChild(InHandle, Generation, "Auto timer allocation failed");
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
        AutoConfig = CkAttributeBasic::MakeAutoConfig();
        AuthorityConstructed = true;
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto State = CkAttributeBasic::Get_State();
        if (!AuthorityConstructed || ck::Is_NOT_Valid(State) ||
            !State.Get_IsExpectedServerChild(InHandle, Generation))
        {
            if (ck::IsValid(State))
            { State.Request_FailExpectedServerChild(InHandle, Generation, "Simulation BeginPlay rejected"); }
            return;
        }
        Request_SetupAttributes(InHandle);
        if (!CkAttributeBasic::IsLiveEntity(FCk_Handle(HealthAttribute)) ||
            !CkAttributeBasic::IsLiveEntity(FCk_Handle(ArmorAttribute)) ||
            !CkAttributeBasic::IsLiveEntity(FCk_Handle(VelocityAttribute)))
        {
            State.Request_FailExpectedServerChild(InHandle, Generation, "Attribute allocation failed");
            return;
        }
        Request_BindSignals();
    }

    void Request_SetupAttributes(FCk_Handle InHandle)
    {
        // Float Attribute (Health: 0-100)
        auto HealthParams = FCk_FloatAttribute_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"Attribute.Health"),
            100.0f
        );
        HealthParams.Set_MinMax(ECk_MinMax::MinMax);
        HealthParams.Set_MinValue(0.0f);
        HealthParams.Set_MaxValue(100.0f);
        HealthAttribute = utils_float_attribute::Add(InHandle, HealthParams, ECk_Replication::DoesNotReplicate);

        // Byte Attribute (Armor: 0-255)
        auto ArmorParams = FCk_ByteAttribute_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"Attribute.Armor"),
            150
        );
        ArmorParams.Set_MinMax(ECk_MinMax::MinMax);
        ArmorParams.Set_MinValue(0);
        ArmorParams.Set_MaxValue(255);
        ArmorAttribute = utils_byte_attribute::Add(InHandle, ArmorParams, ECk_Replication::DoesNotReplicate);

        // Vector Attribute (Velocity: unclamped)
        auto VelocityParams = FCk_VectorAttribute_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"Attribute.Velocity"),
            FVector(100.0f, 50.0f, 0.0f)
        );
        VelocityAttribute = utils_vector_attribute::Add(InHandle, VelocityParams, ECk_Replication::DoesNotReplicate);
    }

    void Request_BindSignals()
    {
        // Health signals
        utils_float_attribute::BindTo_OnValueChanged(HealthAttribute, ECk_MinMaxCurrent::Current,
            FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnHealthValueChanged"));
        utils_float_attribute::BindTo_OnMinClamped(HealthAttribute,
            FCk_Delegate_FloatAttribute_OnClamped(this, n"OnHealthClamped"));
        utils_float_attribute::BindTo_OnMaxClamped(HealthAttribute,
            FCk_Delegate_FloatAttribute_OnClamped(this, n"OnHealthClamped"));

        // Armor signals
        utils_byte_attribute::BindTo_OnValueChanged(ArmorAttribute, ECk_MinMaxCurrent::Current,
            FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnArmorValueChanged"));
        utils_byte_attribute::BindTo_OnMinClamped(ArmorAttribute,
            FCk_Delegate_ByteAttribute_OnClamped(this, n"OnArmorClamped"));
        utils_byte_attribute::BindTo_OnMaxClamped(ArmorAttribute,
            FCk_Delegate_ByteAttribute_OnClamped(this, n"OnArmorClamped"));

        // Velocity signals
        utils_vector_attribute::BindTo_OnValueChanged(VelocityAttribute, ECk_MinMaxCurrent::Current,
            FCk_Delegate_VectorAttribute_OnValueChanged(this, n"OnVelocityValueChanged"));
    }

    bool Get_HasValidAuthorityComposition()
    {
        auto Self = ck::ToEntity(this);
        if (!AuthorityConstructed || !CkAttributeBasic::IsLiveEntity(Self) ||
            !utils_transform::Has(Self) ||
            !CkAttributeBasic::IsLiveEntity(FCk_Handle(AutoTimer)) ||
            !utils_timer::DoCast(FCk_Handle(AutoTimer)).IsSet() ||
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(AutoTimer)) != Self ||
            !CkAttributeBasic::IsLiveEntity(FCk_Handle(HealthAttribute)) ||
            !CkAttributeBasic::IsLiveEntity(FCk_Handle(ArmorAttribute)) ||
            !CkAttributeBasic::IsLiveEntity(FCk_Handle(VelocityAttribute)))
        { return false; }
        if (utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(HealthAttribute)) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(ArmorAttribute)) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(VelocityAttribute)) != Self)
        { return false; }
        auto Dependents = utils_entity_lifetime::Get_LifetimeDependents(Self);
        if (Dependents.Num() != 4 ||
            !Dependents.Contains(FCk_Handle(HealthAttribute)) ||
            !Dependents.Contains(FCk_Handle(ArmorAttribute)) ||
            !Dependents.Contains(FCk_Handle(VelocityAttribute)) ||
            !Dependents.Contains(FCk_Handle(AutoTimer)))
        { return false; }
        return utils_float_attribute::Has_Component(HealthAttribute, ECk_MinMaxCurrent::Current) &&
            utils_float_attribute::Has_Component(HealthAttribute, ECk_MinMaxCurrent::Min) &&
            utils_float_attribute::Has_Component(HealthAttribute, ECk_MinMaxCurrent::Max) &&
            utils_byte_attribute::Has_Component(ArmorAttribute, ECk_MinMaxCurrent::Current) &&
            utils_byte_attribute::Has_Component(ArmorAttribute, ECk_MinMaxCurrent::Min) &&
            utils_byte_attribute::Has_Component(ArmorAttribute, ECk_MinMaxCurrent::Max) &&
            utils_vector_attribute::Has_Component(VelocityAttribute, ECk_MinMaxCurrent::Current) &&
            (AutoTimer.Get_CurrentState() == ECk_Timer_State::Running ||
             AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused);
    }

    bool Request_TryBuildSample(FCkAttributeGym_BasicSample& OutSample)
    {
        if (!Get_HasValidAuthorityComposition()) { return false; }
        auto Sample = FCkAttributeGym_BasicSample();
        Sample.Health = utils_float_attribute::Get_FinalValue(HealthAttribute);
        Sample.Armor = int32(utils_byte_attribute::Get_FinalValue(ArmorAttribute));
        Sample.Velocity = utils_vector_attribute::Get_FinalValue(VelocityAttribute);
        Sample.HealthChangeCount = HealthChangeCount;
        Sample.ArmorChangeCount = ArmorChangeCount;
        Sample.VelocityChangeCount = VelocityChangeCount;
        Sample.ClampedCount = ClampedCount;
        Sample.AutoStep = AutoStep;
        Sample.AutoRunning = AutoRunning;
        if (!CkAttributeBasic::IsValidSample(Sample)) { return false; }
        OutSample = Sample;
        return true;
    }

    bool Request_ApplyControl(int32 InAction, int32 InPresetIndex,
        FVector InVelocity, bool InAutoRunning)
    {
        if (InAction < 0 || InAction > 6 ||
            (InAction == 1 && (InPresetIndex < 0 || InPresetIndex >= 5)) ||
            (InAction == 2 && (InPresetIndex < 0 || InPresetIndex >= 4)) ||
            (InAction == 3 && (InPresetIndex < 0 || InPresetIndex >= 4)) ||
            (InAction == 6 && !CkAttributeBasic::IsFiniteVelocity(InVelocity)))
        { return false; }
        auto State = CkAttributeBasic::Get_State();
        if (ck::Is_NOT_Valid(State) ||
            !State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation)) { return false; }
        if (InAction == 0)
        {
            AutoRunning = InAutoRunning;
            if (AutoRunning) { utils_timer::Request_Resume(AutoTimer); }
            else { utils_timer::Request_Pause(AutoTimer); }
            return true;
        }
        if (InAction == 5)
        {
            AutoStep = 0;
            HealthChangeCount = 0;
            ArmorChangeCount = 0;
            VelocityChangeCount = 0;
            ClampedCount = 0;
            Request_ResetToDefaults();
            AutoRunning = true;
            utils_timer::Request_Resume(AutoTimer);
            return true;
        }
        gym_auto::StopAuto(AutoTimer, AutoRunning);
        if (InAction == 1)
        {
            auto Values = TArray<float32>();
            Values.Add(100.0f); Values.Add(50.0f); Values.Add(0.0f);
            Values.Add(-25.0f); Values.Add(150.0f);
            utils_float_attribute::Request_Override(HealthAttribute, Values[InPresetIndex], ECk_MinMaxCurrent::Current);
        }
        else if (InAction == 2)
        {
            auto Values = TArray<uint8>();
            Values.Add(uint8(0)); Values.Add(uint8(64));
            Values.Add(uint8(128)); Values.Add(uint8(255));
            utils_byte_attribute::Request_Override(ArmorAttribute, Values[InPresetIndex], ECk_MinMaxCurrent::Current);
        }
        else if (InAction == 3)
        {
            auto Values = TArray<FVector>();
            Values.Add(FVector::ZeroVector);
            Values.Add(FVector(100.0, 0.0, 0.0));
            Values.Add(FVector(0.0, 500.0, 0.0));
            Values.Add(FVector(9999.0, 9999.0, 9999.0));
            utils_vector_attribute::Request_Override(VelocityAttribute, Values[InPresetIndex], ECk_MinMaxCurrent::Current);
        }
        else if (InAction == 4) { Request_TestBoundariesMax(); }
        else if (InAction == 6)
        { utils_vector_attribute::Request_Override(VelocityAttribute, InVelocity, ECk_MinMaxCurrent::Current); }
        else { return false; }
        return true;
    }

    //------------------------------------------------------------------------
    // Signal Callbacks
    //------------------------------------------------------------------------

    UFUNCTION()
    void OnHealthValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload)
    {
        auto State = CkAttributeBasic::Get_State();
        if (InAttributeOwnerEntity != ck::ToEntity(this) || ck::Is_NOT_Valid(State) ||
            !State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation)) { return; }
        HealthChangeCount++;
    }

    UFUNCTION()
    void OnHealthClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnClamped InPayload)
    {
        auto State = CkAttributeBasic::Get_State();
        if (InAttributeOwnerEntity != ck::ToEntity(this) || ck::Is_NOT_Valid(State) ||
            !State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation)) { return; }
        ClampedCount++;
    }

    UFUNCTION()
    void OnArmorValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload)
    {
        auto State = CkAttributeBasic::Get_State();
        if (InAttributeOwnerEntity != ck::ToEntity(this) || ck::Is_NOT_Valid(State) ||
            !State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation)) { return; }
        ArmorChangeCount++;
    }

    UFUNCTION()
    void OnArmorClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnClamped InPayload)
    {
        auto State = CkAttributeBasic::Get_State();
        if (InAttributeOwnerEntity != ck::ToEntity(this) || ck::Is_NOT_Valid(State) ||
            !State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation)) { return; }
        ClampedCount++;
    }

    UFUNCTION()
    void OnVelocityValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_VectorAttribute_OnValueChanged InPayload)
    {
        auto State = CkAttributeBasic::Get_State();
        if (InAttributeOwnerEntity != ck::ToEntity(this) || ck::Is_NOT_Valid(State) ||
            !State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation)) { return; }
        VelocityChangeCount++;
    }

    //------------------------------------------------------------------------
    // Auto Mode
    //------------------------------------------------------------------------

    UFUNCTION()
    private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto State = CkAttributeBasic::Get_State();
        if (InHandle != AutoTimer || ck::Is_NOT_Valid(State) ||
            !State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation)) { return; }
        auto Step = AutoStep % AutoConfig.TotalSteps;
        AutoStep++;

        switch (Step)
        {
            case 0: // SetHealth 50, SetArmor 150, SetVelocity mid
                Request_SetAllValues(50.0f, 150, FVector(100.0f, 50.0f, 0.0f));
                break;

            case 1: // SetHealth 95, SetArmor 245, SetVelocity high
                Request_SetAllValues(95.0f, 245, FVector(200.0f, 100.0f, 50.0f));
                break;

            case 2: // TestBoundaries - push past max
                Request_TestBoundariesMax();
                break;

            case 3: // SetHealth 10, SetArmor 20, SetVelocity low
                Request_SetAllValues(10.0f, 20, FVector(50.0f, 25.0f, -25.0f));
                break;

            case 4: // Push past min - SetHealth -10 (clamps to 0), SetArmor 0
                Request_SetAllValues(-10.0f, 0, FVector(-50.0f, -25.0f, -50.0f));
                break;

            case 5: // Reset to defaults
                Request_ResetToDefaults();
                break;
        }
    }

    // Shared operations used by both auto steps and manual message handlers
    void Request_SetAllValues(float32 InHealth, uint8 InArmor, FVector InVelocity)
    {
        utils_float_attribute::Request_Override(HealthAttribute, InHealth, ECk_MinMaxCurrent::Current);
        utils_byte_attribute::Request_Override(ArmorAttribute, InArmor, ECk_MinMaxCurrent::Current);
        utils_vector_attribute::Request_Override(VelocityAttribute, InVelocity, ECk_MinMaxCurrent::Current);
    }

    void Request_TestBoundariesMax()
    {
        utils_float_attribute::Request_Override(HealthAttribute, 120.0f, ECk_MinMaxCurrent::Current);
        utils_byte_attribute::Request_Override(ArmorAttribute, 255, ECk_MinMaxCurrent::Current);
        utils_vector_attribute::Request_Override(VelocityAttribute, FVector(500.0f, 250.0f, 100.0f), ECk_MinMaxCurrent::Current);
    }

    void Request_ResetToDefaults()
    {
        if (ck::IsValid(HealthAttribute))
        {
            utils_float_attribute::Request_Override(HealthAttribute, 100.0f, ECk_MinMaxCurrent::Current);
        }
        if (ck::IsValid(ArmorAttribute))
        {
            utils_byte_attribute::Request_Override(ArmorAttribute, 150, ECk_MinMaxCurrent::Current);
        }
        if (ck::IsValid(VelocityAttribute))
        {
            utils_vector_attribute::Request_Override(VelocityAttribute, FVector(100.0f, 50.0f, 0.0f), ECk_MinMaxCurrent::Current);
        }
    }

}

USTRUCT()
struct FBasicProjectionSpawnParams
{
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() int32 Generation = 0;
}

class UCk_EntityScript_AttributeGym_BasicProjection : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;
    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    private FCk_Handle_Timer _DisplayTick;
    private FCkGym_AutoConfig _AutoConfig;
    private bool _ProjectionConstructed = false;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        _ProjectionConstructed = false;
        _DisplayTick = FCk_Handle_Timer();
        _AutoConfig = FCkGym_AutoConfig();
        auto State = CkAttributeBasic::Get_State();
        if (!CkAttributeBasic::IsLiveEntity(InHandle))
        {
            if (ck::IsValid(State))
            { State.Request_FailExpectedLocalChild(InHandle, Generation, "Projection handle invalid"); }
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        auto Mode = utils_net::Get_EntityNetMode(InHandle);
        if ((Mode != ECk_Net_NetModeType::Client &&
             Mode != ECk_Net_NetModeType::ClientAndHost) ||
            !CkAttributeBasic::IsLiveEntity(InHandle) ||
            utils_entity_lifetime::Get_LifetimeOwner(InHandle) != CkAttributeBasic::FindStation() ||
            ck::Is_NOT_Valid(State) || !State.Get_IsExpectedLocalChild(InHandle, Generation))
        {
            if (ck::IsValid(State))
            { State.Request_FailExpectedLocalChild(InHandle, Generation, "Projection role or owner invalid"); }
            ck::Error("[AttributeBasic] Rejected stale or dedicated-server projection");
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_BasicProjection");
        _DisplayTick = utils_timer::Create_Tick(InHandle,
            FCk_Delegate_Timer(this, n"OnDisplayTick"));
        if (!CkAttributeBasic::IsLiveEntity(FCk_Handle(_DisplayTick)))
        {
            State.Request_FailExpectedLocalChild(InHandle, Generation,
                "Projection display timer allocation failed");
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        _AutoConfig = CkAttributeBasic::MakeAutoConfig();
        _ProjectionConstructed = true;
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    bool Get_HasValidLocalComposition()
    {
        auto Self = ck::ToEntity(this);
        if (!_ProjectionConstructed || !CkAttributeBasic::IsLiveEntity(Self) ||
            !utils_transform::Has(Self) ||
            !CkAttributeBasic::IsLiveEntity(FCk_Handle(_DisplayTick)) ||
            !utils_timer::DoCast(FCk_Handle(_DisplayTick)).IsSet() ||
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_DisplayTick)) != Self ||
            _DisplayTick.Get_CurrentState() != ECk_Timer_State::Running)
        { return false; }
        auto Dependents = utils_entity_lifetime::Get_LifetimeDependents(Self);
        return Dependents.Num() == 1 && Dependents[0] == FCk_Handle(_DisplayTick);
    }

    UFUNCTION()
    private void OnDisplayTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto State = CkAttributeBasic::Get_State();
        if (InTimer != _DisplayTick || ck::Is_NOT_Valid(State) ||
            !State.Get_HasAdmittedLocalProjection(Generation)) { return; }
        if (!Get_HasValidLocalComposition())
        {
            State.Request_FailCurrentLocalChild(ck::ToEntity(this), Generation,
                "Projection display dependency lost");
            return;
        }
        auto Sample = State.Snapshot.Sample;
        if (!CkAttributeBasic::IsValidSample(Sample))
        {
            State.Request_FailCurrentLocalChild(ck::ToEntity(this), Generation,
                "Projection sample invalid");
            return;
        }
        auto TitleText = "BASIC ATTRIBUTES (" +
            CkGym_Common::Get_NetworkRoleTitle(ck::ToEntity(this)) + ")";
        auto DisplayText = gym_auto::FormatHeader(_AutoConfig, Sample.AutoRunning);
        DisplayText = DisplayText + "===== Attribute Values =====\n";
        auto HealthBar = CkGym_Attribute::Create_ProgressBar(Sample.Health, 100.0f, 20);
        DisplayText = f"{DisplayText}FLOAT Health: {Sample.Health}/100\n";
        DisplayText = f"{DisplayText}[{HealthBar}]\n\n";
        auto ArmorBar = CkGym_Attribute::Create_ProgressBar(
            float32(Sample.Armor), 255.0f, 20, ECk_ASCII_ProgressBar_Style::HashTag_Symbol);
        DisplayText = f"{DisplayText}BYTE Armor: {Sample.Armor}/255\n";
        DisplayText = f"{DisplayText}[{ArmorBar}]\n\n";
        DisplayText = f"{DisplayText}VECTOR Velocity: {Sample.Velocity.ToString()}\n";
        DisplayText = f"{DisplayText}Speed: {Sample.Velocity.Size()} (unclamped)\n\n";
        auto TotalChanges = Sample.HealthChangeCount + Sample.ArmorChangeCount +
            Sample.VelocityChangeCount;
        DisplayText = f"{DisplayText}Changes: {TotalChanges} | Clamps: {Sample.ClampedCount}\n";
        DisplayText = DisplayText + gym_auto::FormatAutoAndCommands(
            _AutoConfig, Sample.AutoStep, Sample.AutoRunning);
        CkGym_Common::Update_StationDisplay(ck::ToEntity(this), TitleText, DisplayText, "");
    }
}

//============================================================================
// PLAYER CONTROLLER
//============================================================================

class ACk_AttributeGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    private int32 _RequestedSerial = 0;
    private int32 _PendingSerial = 0;
    private int32 _PendingAction = -1;
    private int32 _PendingIndex = -1;
    private float _PendingSinceSeconds = 0.0;
    private int32 _HealthPresetIndex = -1;
    private int32 _ArmorPresetIndex = -1;
    private int32 _VelocityPresetIndex = -1;
    private int32 _StateWaitAttempts = 0;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        if (HasAuthority()) { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
        return CkAttributeBasic::Get_Stations();
    }

    void Request_StartGym() override
    {
        auto State = CkAttributeBasic::Get_State();
        if (ck::Is_NOT_Valid(State))
        {
            if (IsLocalController() && _StateWaitAttempts++ < 200)
            { System::SetTimer(this, n"OnStateWait", 0.1, false); }
            return;
        }
        if (HasAuthority() && State.Get_HasPublishedCatalog())
        { State.Request_EnsureSharedDemo(); }
        if (IsLocalController()) { State.Request_EnsureLocalProjection(); }
    }

    UFUNCTION() private void OnStateWait() { Request_StartGym(); }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        Request_ReconcileControlAck();
        auto Rows = TArray<FCkGym_ControlRow>();
        auto State = CkAttributeBasic::Get_State();
        bool Running = ck::IsValid(State) && State.Snapshot.Sample.AutoRunning;
        Rows.Add(CkGym_Control::Header("BASIC ATTRIBUTES"));
        Rows.Add(CkGym_Control::Toggle(EKeys::T, "T", "Auto-cycle phases", Running));
        Rows.Add(CkGym_Control::Cycle(EKeys::One, "1", "Health preset",
            DoGet_PresetLabel(_HealthPresetIndex, "100 / 50 / 0 / -25 / 150")));
        Rows.Add(CkGym_Control::Cycle(EKeys::Two, "2", "Armor preset",
            DoGet_PresetLabel(_ArmorPresetIndex, "0 / 64 / 128 / 255")));
        Rows.Add(CkGym_Control::Cycle(EKeys::Three, "3", "Velocity preset",
            DoGet_PresetLabel(_VelocityPresetIndex, "rings")));
        Rows.Add(CkGym_Control::Action(EKeys::B, "B", "Test boundaries"));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Reset values"));
        Rows.Add(CkGym_Control::Status("Free-range velocity",
            "console Ck_GymAttribute_SetVelocity X Y Z"));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        Request_ReconcileControlAck();
        if (!IsLocalController() || _PendingSerial != 0 || InRowIndex < 1 || InRowIndex > 6)
        { return; }
        auto State = CkAttributeBasic::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_HasCompleteSnapshot()) { return; }
        int32 Action = InRowIndex == 1 ? 0 : (InRowIndex == 2 ? 1 :
            (InRowIndex == 3 ? 2 : (InRowIndex == 4 ? 3 :
            (InRowIndex == 5 ? 4 : 5))));
        int32 Index = -1;
        if (Action == 1) { Index = (_HealthPresetIndex + 1) % 5; }
        if (Action == 2) { Index = (_ArmorPresetIndex + 1) % 4; }
        if (Action == 3) { Index = (_VelocityPresetIndex + 1) % 4; }
        Request_SendControl(Action, Index, FVector::ZeroVector,
            Action == 0 ? !State.Snapshot.Sample.AutoRunning : State.Snapshot.Sample.AutoRunning);
    }

    private FString DoGet_PresetLabel(int32 InIndex, FString InRing)
    { return InIndex < 0 ? f"({InRing})" : f"step {InIndex + 1}"; }

    private void Request_ReconcileControlAck()
    {
        if (_PendingSerial == 0 || LastControlAttemptSerial != _PendingSerial) { return; }
        if (LastControlResult == 1)
        {
            if (_PendingAction == 1) { _HealthPresetIndex = _PendingIndex; }
            if (_PendingAction == 2) { _ArmorPresetIndex = _PendingIndex; }
            if (_PendingAction == 3) { _VelocityPresetIndex = _PendingIndex; }
            if (_PendingAction == 5)
            {
                _HealthPresetIndex = -1;
                _ArmorPresetIndex = -1;
                _VelocityPresetIndex = -1;
            }
        }
        _PendingSerial = 0;
        _PendingAction = -1;
        _PendingIndex = -1;
    }

    UFUNCTION() private void OnControlAckDeadline()
    {
        if (_PendingSerial == 0 ||
            float(System::GetGameTimeInSeconds()) - _PendingSinceSeconds < 5.0)
        { return; }
        ck::Error("[AttributeBasic] Owning control acknowledgement timed out");
        _PendingSerial = 0;
        _PendingAction = -1;
        _PendingIndex = -1;
    }

    private void Request_SendControl(int32 InAction, int32 InIndex,
        FVector InVelocity, bool InAutoRunning)
    {
        if (!IsLocalController() || _PendingSerial != 0) { return; }
        _RequestedSerial = Math::Max(_RequestedSerial, LastControlSerial) + 1;
        _PendingSerial = _RequestedSerial;
        _PendingAction = InAction;
        _PendingIndex = InIndex;
        _PendingSinceSeconds = float(System::GetGameTimeInSeconds());
        System::SetTimer(this, n"OnControlAckDeadline", 5.0, false);
        Server_RequestAttributeBasicControl(_PendingSerial, InAction, InIndex,
            InVelocity, InAutoRunning);
    }

    UFUNCTION(Exec, DisplayName="Attribute Gym - Set Velocity")
    void Ck_GymAttribute_SetVelocity(float32 InX, float32 InY, float32 InZ)
    {
        Request_ReconcileControlAck();
        auto Velocity = FVector(InX, InY, InZ);
        if (!CkAttributeBasic::IsFiniteVelocity(Velocity)) { return; }
        Request_SendControl(6, -1, Velocity, false);
    }

    UFUNCTION(Server)
    void Server_RequestAttributeBasicControl(int32 InSerial, int32 InAction,
        int32 InPresetIndex, FVector InVelocity, bool InAutoRunning)
    {
        if (!HasAuthority()) { return; }
        LastControlAttemptSerial = InSerial;
        LastControlResult = 0;
        if (InSerial <= 0 || InSerial <= LastControlSerial ||
            InAction < 0 || InAction > 6 ||
            (InAction == 1 && (InPresetIndex < 0 || InPresetIndex >= 5)) ||
            (InAction == 2 && (InPresetIndex < 0 || InPresetIndex >= 4)) ||
            (InAction == 3 && (InPresetIndex < 0 || InPresetIndex >= 4)) ||
            (InAction == 6 && !CkAttributeBasic::IsFiniteVelocity(InVelocity)))
        { LastControlResult = 4; return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this ||
            Pawn.GetWorld() != GetWorld())
        { LastControlResult = 2; return; }
        auto State = CkAttributeBasic::Get_State();
        TArray<ACk_AttributeGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_AttributeGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto CurrentGym = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(State) || Modes.Num() != 1 ||
            ck::Is_NOT_Valid(Modes[0]) || Modes[0].GetWorld() != GetWorld() ||
            State.GetWorld() != GetWorld() || !State.Get_HasPublishedCatalog() ||
            CurrentGym < 0 || CurrentGym >= Registry.Num() ||
            Registry[CurrentGym].DisplayName != "Attribute Basic" ||
            !State.Get_HasReadyServerChild())
        { LastControlResult = 3; return; }
        if (!State.Request_ApplyControl(InAction, InPresetIndex, InVelocity, InAutoRunning))
        { LastControlResult = 4; return; }
        LastControlSerial = InSerial;
        LastControlResult = 1;
    }
}

class ACk_AttributeGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_AttributeGym_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default GameStateClass = ACk_AttributeGym_GameState;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Super::BeginPlay();
        TArray<ACk_AttributeGym_GameState> States;
        GetAllActorsOfClass(ACk_AttributeGym_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0]) ||
            !States[0].Get_HasPublishedCatalog())
        { ck::Error("[AttributeBasic] GameState/catalog unavailable"); return; }
        States[0].Request_EnsureSharedDemo();
    }
}
