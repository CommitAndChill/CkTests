// Language=angelscript

//============================================================================
// CK CAMERA - GAMEPLAYCAMERA GYM
//============================================================================
//
// Visual gym for the GameplayCamera stack. A flyable pawn carries the camera director; the player orbits with
// the mouse and cycles modes (third-person / top-down / lock-on) with E/Q, watching blended transitions,
// collision push-in, and auto-reorient lock-on on screen.
//
// First CkTests gym to combine a controllable pawn + Enhanced Input + a driven camera. Input is plain Enhanced
// Input (IMC over AS-asset IAs + BindAction) - no input-profile-object wrapper.
//
// Register: CkTests_GymRegistry.as -> "Camera". Runs in the shared TestGyms_CkTests_Level.
//============================================================================

class ACk_CameraGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default GameStateClass = ACk_CameraGym_GameState;
    default PlayerControllerClass = ACk_CameraGym_PlayerController;
    default DefaultPawnClass      = ACk_CameraGym_Pawn;
}

// --------------------------------------------------------------------------------------------------------------------

class ACk_CameraGym_PlayerController : ACk_Gym_Base_PlayerController
{
    private bool _Started = false;
    private bool _Ending = false;
    private bool _Failed = false;
    private bool _InputAttempted = false;
    private bool _InputReady = false;
    private bool _SourceCaptured = false;
    private bool _OwnerCaptured = false;
    private float32 _StartupElapsed = 0.0f;
    private UWorld _BoundWorld;
    private ULocalPlayer _BoundPlayer;
    private ACk_CameraGym_Pawn _BoundPawn;
    private FCk_Handle_InputSource _Source;
    private FCk_Handle _Scope;
    private FCk_Handle_InputLayer _Layer;

    private bool Get_HasExactLocalController()
    {
        auto World = GetWorld();
        auto Player = GetLocalPlayer();
        return !_Ending && !_Failed && !IsActorBeingDestroyed() &&
            IsLocalController() && ck::IsValid(World) &&
            World.GetNetMode() != ENetMode::NM_DedicatedServer &&
            ck::IsValid(Player) && UCk_CameraGym_LocalAdmission_UE::Get_HasExactLocalPlayerWorld(Player, World) &&
            (!_OwnerCaptured || (_BoundWorld == World && _BoundPlayer == Player));
    }

    // This preflight can become true before the pawn constructs its scope.
    bool Get_IsCameraControllerAdmitted()
    {
        auto World = GetWorld();
        auto Player = GetLocalPlayer();
        return _Started && !_Failed && !_Ending && !IsActorBeingDestroyed() &&
            IsLocalController() && ck::IsValid(World) &&
            World.GetNetMode() != ENetMode::NM_DedicatedServer &&
            ck::IsValid(Player) && UCk_CameraGym_LocalAdmission_UE::Get_HasExactLocalPlayerWorld(Player, World) &&
            _BoundWorld == World && _BoundPlayer == Player &&
            Get_HasExactInputSource();
    }

    UFUNCTION(BlueprintPure)
    bool Get_IsCameraInputReady()
    {
        if (!Get_HasLiveInput()) { return false; }
        auto Pawn = Get_ExactPawn();
        return ck::IsValid(Pawn) && Pawn == _BoundPawn &&
            Pawn.Get_IsLocalCameraReady();
    }

    private bool Get_HasExactInputSource()
    {
        if (!_SourceCaptured || ck::Is_NOT_Valid(_Source) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                _Source, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
        { return false; }
        auto Subsystem = UCk_InputSource_Subsystem::Get(this);
        return ck::IsValid(Subsystem) && Subsystem.Get_InputSource() == _Source;
    }

    private ACk_CameraGym_Pawn Get_ExactPawn()
    {
        if (!Get_HasExactLocalController()) { return nullptr; }
        auto Pawn = Cast<ACk_CameraGym_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.IsActorBeingDestroyed() ||
            Pawn.GetClass() != ACk_CameraGym_Pawn || Pawn.GetWorld() != GetWorld() ||
            Pawn.GetController() != this ||
            (_BoundPawn != nullptr && _BoundPawn != Pawn))
        { return nullptr; }
        return Pawn;
    }

    private void ReleaseHeldKeys()
    {
        auto Pawn = _BoundPawn;
        if (ck::Is_NOT_Valid(Pawn) || Pawn.IsActorBeingDestroyed() ||
            Pawn.GetClass() != ACk_CameraGym_Pawn || Pawn.GetWorld() != _BoundWorld)
        { return; }
        Pawn.Request_ResetMovementKeys();
    }

    private void CloseInput(FString Reason)
    {
        if (_Failed) { return; }
        _Failed = true;
        _InputReady = false;
        ReleaseHeldKeys();
        if (ck::IsValid(_Layer) && ck::IsValid(_Scope) &&
            utils_entity_lifetime::Has_LifetimeOwner(_Layer) &&
            utils_entity_lifetime::Get_LifetimeOwner(_Layer) == _Scope)
        {
            FCk_Handle LayerEntity(_Layer);
            utils_entity_lifetime::Request_DestroyEntity(LayerEntity);
        }
        auto Pawn = _BoundPawn;
        if (ck::IsValid(Pawn) && !Pawn.IsActorBeingDestroyed() &&
            Pawn.GetClass() == ACk_CameraGym_Pawn && Pawn.GetWorld() == _BoundWorld)
        { Pawn.Request_CloseOwnerInputFailure(Reason); }
        ck::Error("Camera gym owner input closed: " + Reason);
    }

    private void AddKeyCapture(FKey Key, ECk_InputLayer_CaptureBehavior Behavior)
    {
        utils_input_layer::Request_AddCapture(_Layer, FCk_Request_InputLayer_AddCapture(
            utils_input_layer::Make_KeyCapture(Key, Behavior)));
    }

    private bool Get_HasAllCaptures()
    {
        if (ck::Is_NOT_Valid(_Layer) ||
            utils_input_layer::Get_NumCaptures(_Layer) != 10) { return false; }
        TArray<FKey> Keys;
        Keys.Add(EKeys::W); Keys.Add(EKeys::S);
        Keys.Add(EKeys::A); Keys.Add(EKeys::D);
        Keys.Add(EKeys::SpaceBar); Keys.Add(EKeys::LeftControl);
        Keys.Add(EKeys::E); Keys.Add(EKeys::Q);
        Keys.Add(EKeys::MouseX); Keys.Add(EKeys::MouseY);
        auto Captures = utils_input_layer::Get_Captures(_Layer);
        for (auto Key : Keys)
        {
            bool Found = false;
            auto ExpectedBehavior = (Key == EKeys::MouseX || Key == EKeys::MouseY)
                ? ECk_InputLayer_CaptureBehavior::PassThrough
                : ECk_InputLayer_CaptureBehavior::Consume;
            for (auto Capture : Captures)
            {
                if (Capture.Get_MatchMode() == ECk_InputLayer_CaptureMatch::Key &&
                    Capture.Get_Key() == Key && Capture.Get_Behavior() == ExpectedBehavior)
                { Found = true; break; }
            }
            if (!Found) { return false; }
        }
        return true;
    }

    private bool Get_HasLiveInput()
    {
        if (!_InputReady || !_InputAttempted || !Get_IsCameraControllerAdmitted() ||
            ck::Is_NOT_Valid(_Layer) || ck::Is_NOT_Valid(_Scope) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                _Layer, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                _Scope, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !utils_entity_lifetime::Has_LifetimeOwner(_Layer) ||
            utils_entity_lifetime::Get_LifetimeOwner(_Layer) != _Scope ||
            utils_input_layer::Get_InputSource(_Layer) != _Source ||
            utils_input_layer::Get_Priority(_Layer) != CkGym_InputStack::Priority_Pawn ||
            utils_input_layer::TryGet_LayerWithPriority(
                _Source, CkGym_InputStack::Priority_Pawn) != _Layer ||
            !Get_HasAllCaptures())
        { return false; }
        return true;
    }

    private void EnsureInput(float32 DeltaSeconds)
    {
        if (!_Started || _Ending || _Failed) { return; }
        // Server replicas and dedicated-server controllers never own a local input source.
        if (!_OwnerCaptured && !IsLocalController())
        { return; }
        if (!_OwnerCaptured && ck::IsValid(GetWorld()) &&
            GetWorld().GetNetMode() == ENetMode::NM_DedicatedServer)
        { return; }
        if (!Math::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f ||
            !Math::IsFinite(_StartupElapsed + DeltaSeconds))
        { CloseInput("Invalid local input startup delta"); return; }
        _StartupElapsed += DeltaSeconds;
        if (!Get_HasExactLocalController())
        {
            if (_OwnerCaptured || _StartupElapsed >= 20.0f)
            { CloseInput("Local controller or captured player changed"); }
            return;
        }
        auto Player = GetLocalPlayer();
        auto World = GetWorld();
        if (!_OwnerCaptured)
        { _BoundWorld = World; _BoundPlayer = Player; _OwnerCaptured = true; }
        auto SourceSubsystem = UCk_InputSource_Subsystem::Get(this);
        auto CurrentSource = ck::IsValid(SourceSubsystem)
            ? SourceSubsystem.Get_InputSource() : FCk_Handle_InputSource();
        if (_SourceCaptured && CurrentSource != _Source)
        { CloseInput("Local input source changed"); return; }
        if (!_SourceCaptured && ck::IsValid(CurrentSource))
        { _Source = CurrentSource; _SourceCaptured = true; }

        auto Pawn = Get_ExactPawn();
        if (_BoundPawn != nullptr && Pawn != _BoundPawn)
        { CloseInput("Local pawn changed"); return; }
        if (ck::IsValid(Pawn) && Pawn.Get_HasCameraFailure())
        { CloseInput("Local camera failed"); return; }
        if (!_InputAttempted && ck::IsValid(Pawn) && ck::IsValid(_Source) &&
            Pawn.Get_IsLocalCameraReady())
        {
            auto Scope = Pawn.Get_LocalCameraScope();
            if (ck::Is_NOT_Valid(Scope))
            { CloseInput("Ready camera has no retained scope"); return; }
            auto Occupant = utils_input_layer::TryGet_LayerWithPriority(
                _Source, CkGym_InputStack::Priority_Pawn);
            if (ck::IsValid(Occupant))
            {
                if (_StartupElapsed >= 20.0f)
                { CloseInput("Pawn input slot stayed occupied"); }
                return;
            }
            _InputAttempted = true;
            _BoundPawn = Pawn;
            _Scope = Scope;
            _Layer = utils_input_layer::Create(_Scope,
                FCk_InputLayer_Spec(_Source, CkGym_InputStack::Priority_Pawn));
            if (ck::Is_NOT_Valid(_Layer) ||
                !utils_entity_lifetime::Has_LifetimeOwner(_Layer) ||
                utils_entity_lifetime::Get_LifetimeOwner(_Layer) != _Scope ||
                utils_input_layer::Get_InputSource(_Layer) != _Source)
            { CloseInput("Pawn input layer creation failed"); return; }
            utils_input_layer::BindTo_OnCaptureTriggered(_Layer,
                FCk_Delegate_InputLayer_CaptureTriggered(this, n"OnCaptured"),
                ECk_Signal_BindingPolicy::IgnorePayloadInFlight,
                ECk_Signal_PostFireBehavior::DoNothing);
            AddKeyCapture(EKeys::W, ECk_InputLayer_CaptureBehavior::Consume);
            AddKeyCapture(EKeys::S, ECk_InputLayer_CaptureBehavior::Consume);
            AddKeyCapture(EKeys::A, ECk_InputLayer_CaptureBehavior::Consume);
            AddKeyCapture(EKeys::D, ECk_InputLayer_CaptureBehavior::Consume);
            AddKeyCapture(EKeys::SpaceBar, ECk_InputLayer_CaptureBehavior::Consume);
            AddKeyCapture(EKeys::LeftControl, ECk_InputLayer_CaptureBehavior::Consume);
            AddKeyCapture(EKeys::E, ECk_InputLayer_CaptureBehavior::Consume);
            AddKeyCapture(EKeys::Q, ECk_InputLayer_CaptureBehavior::Consume);
            AddKeyCapture(EKeys::MouseX, ECk_InputLayer_CaptureBehavior::PassThrough);
            AddKeyCapture(EKeys::MouseY, ECk_InputLayer_CaptureBehavior::PassThrough);
        }
        if (_InputAttempted)
        {
            if (ck::Is_NOT_Valid(Pawn) || Pawn != _BoundPawn ||
                !Pawn.Get_IsLocalCameraReady() || ck::Is_NOT_Valid(_Layer) ||
                ck::Is_NOT_Valid(_Scope) || !Get_HasExactInputSource() ||
                utils_entity_lifetime::Get_IsPendingDestroy(
                    _Layer, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                utils_entity_lifetime::Get_IsPendingDestroy(
                    _Scope, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                !utils_entity_lifetime::Has_LifetimeOwner(_Layer) ||
                utils_entity_lifetime::Get_LifetimeOwner(_Layer) != _Scope ||
                utils_input_layer::Get_InputSource(_Layer) != _Source ||
                utils_input_layer::Get_Priority(_Layer) != CkGym_InputStack::Priority_Pawn)
            { CloseInput("Admitted owner input changed"); return; }
            if (!_InputReady && Get_HasAllCaptures()) { _InputReady = true; }
            if (_InputReady && !Get_HasLiveInput())
            { CloseInput("Admitted capture or input identity changed"); return; }
        }
        if (!_InputReady && _StartupElapsed >= 20.0f)
        { CloseInput("Complete local camera input did not converge in 20 seconds"); }
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float32 DeltaSeconds)
    { EnsureInput(DeltaSeconds); }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        ReleaseHeldKeys();
        if (ck::IsValid(_Layer) && ck::IsValid(_Scope) &&
            utils_entity_lifetime::Has_LifetimeOwner(_Layer) &&
            utils_entity_lifetime::Get_LifetimeOwner(_Layer) == _Scope)
        {
            FCk_Handle LayerEntity(_Layer);
            utils_entity_lifetime::Request_DestroyEntity(LayerEntity);
        }
        _Ending = true;
        _InputReady = false;
        Super::EndPlay(Reason);
    }

    void Request_StartGym() override
    {
        if (_Started || _Ending || _Failed) { return; }
        _Started = true;
        if (Get_HasExactLocalController())
        { Print("Camera Gym - Mouse orbit | E/Q modes | J/K modes | B zoom | Tab menu", 12.0f); }
    }

    UFUNCTION()
    private void OnCaptured(FCk_Handle_InputLayer InLayer,
        FCk_InputSource_RawEvent InEvent, FCk_InputLayer_Capture InCapture)
    {
        if (InLayer != _Layer || !Get_HasLiveInput()) { return; }
        auto Key = InEvent.Get_Key();
        auto Type = InEvent.Get_EventType();
        auto ExpectedBehavior = (Key == EKeys::MouseX || Key == EKeys::MouseY)
            ? ECk_InputLayer_CaptureBehavior::PassThrough
            : ECk_InputLayer_CaptureBehavior::Consume;
        if (InCapture.Get_MatchMode() != ECk_InputLayer_CaptureMatch::Key ||
            InCapture.Get_Key() != Key ||
            InCapture.Get_Behavior() != ExpectedBehavior) { return; }
        auto Pawn = Get_ExactPawn();
        if (Type == ECk_InputSource_EventType::Released &&
            ck::IsValid(_BoundPawn) && !_BoundPawn.IsActorBeingDestroyed() &&
            _BoundPawn.GetWorld() == _BoundWorld &&
            (Key == EKeys::W || Key == EKeys::S || Key == EKeys::A ||
             Key == EKeys::D || Key == EKeys::SpaceBar || Key == EKeys::LeftControl))
        {
            _BoundPawn.Request_MovementKey(Key, false);
            return;
        }
        if (ck::Is_NOT_Valid(Pawn) || Pawn != _BoundPawn ||
            !Pawn.Get_IsLocalCameraReady()) { return; }
        if (Type == ECk_InputSource_EventType::AnalogAxis)
        {
            if (Key == EKeys::MouseX)
            { Pawn.Request_Look(FVector2D(InEvent.Get_AnalogValue(), 0.0f)); }
            else if (Key == EKeys::MouseY)
            { Pawn.Request_Look(FVector2D(0.0f, InEvent.Get_AnalogValue())); }
            return;
        }
        if (Type != ECk_InputSource_EventType::Pressed &&
            Type != ECk_InputSource_EventType::Released) { return; }
        const bool Pressed = Type == ECk_InputSource_EventType::Pressed;
        if (Key == EKeys::W || Key == EKeys::S || Key == EKeys::A ||
            Key == EKeys::D || Key == EKeys::SpaceBar || Key == EKeys::LeftControl)
        { Pawn.Request_MovementKey(Key, Pressed); }
        else if (Pressed && Key == EKeys::E) { Pawn.Request_CycleMode(1); }
        else if (Pressed && Key == EKeys::Q) { Pawn.Request_CycleMode(-1); }
    }

    private ACk_CameraGym_Pawn Get_ReadyLocalPawn()
    {
        if (!Get_HasLiveInput()) { return nullptr; }
        auto Pawn = Get_ExactPawn();
        return ck::IsValid(Pawn) && Pawn.Get_IsLocalCameraReady() ? Pawn : nullptr;
    }
    private void DoCycleMode(int32 Direction)
    {
        auto Pawn = Get_ReadyLocalPawn();
        if (ck::IsValid(Pawn)) { Pawn.Request_CycleMode(Direction); }
    }
    private void DoToggleZoom()
    {
        auto Pawn = Get_ReadyLocalPawn();
        if (ck::IsValid(Pawn)) { Pawn.Request_ToggleZoom(); }
    }
    private FString Get_ModeLabel(int32 Index)
    {
        if (Index == 0) { return "Third-Person"; }
        if (Index == 1) { return "Top-Down"; }
        return "Lock-On";
    }
    FString Get_ControlPanelTitle() override { return "GAMEPLAY CAMERA"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        auto Pawn = Get_ReadyLocalPawn();
        bool Ready = ck::IsValid(Pawn);
        Rows.Add(CkGym_Control::Cycle(EKeys::J, "J", "Camera mode (next)",
            Ready ? Get_ModeLabel(Pawn.Get_ModeIndex()) : "Waiting for local camera", false, Ready));
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Camera mode (previous)", Ready));
        Rows.Add(CkGym_Control::Toggle(EKeys::B, "B", "FOV zoom trim",
            Ready && Pawn.Get_IsZoomActive(), false, Ready));
        return Rows;
    }
    void Request_ControlActivated(int32 Row) override
    {
        if (Row == 0) { DoCycleMode(1); }
        else if (Row == 1) { DoCycleMode(-1); }
        else if (Row == 2) { DoToggleZoom(); }
    }
}
