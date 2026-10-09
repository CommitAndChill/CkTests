// Language=angelscript

//============================================================================
// CK CAMERA - GYM PAWN
//============================================================================
//
// A flyable pawn (ADefaultPawn movement) carrying the GameplayCamera director, plus a visible subject so the
// driven camera has something to frame. On ready it:
//   - admits one owner-local camera on a fresh SceneNode child following the
//     replicated pawn transform, using this pawn's UCk_CameraComponent,
//   - waits for the authority GameState's complete shared floor/pillar/beacon bank, and
//   - pushes the first camera mode.
//
// The pawn carries a visible body sphere + a forward "nose" cube (ADefaultPawn has no in-game mesh) so you can
// see what the camera frames and which way the pawn faces. These are visual-only (no collision) and the camera's
// collision whiskers ignore the whole pawn actor anyway (_TraceIgnoreActor = owning actor).
//
// The PlayerController drives orbit (look axis -> Request_SetOrientationIntention) and mode cycling, which
// call Request_Look / Request_CycleMode here.
//============================================================================

// The demo zoom trim uses a distinct (negative) priority so cycling modes (OneOnly at the default priority 0) does
// not evict it, and so the active mode - not the look-at-less trim - stays the dominant layer for auto-reorient.
const int32 k_TrimPriority = -10;

class ACk_CameraGym_Pawn : ACk_Gym_Base_Pawn
{
    // We drive the view via the GameplayCamera director, so turn off ADefaultPawn's built-in WASD + mouse-look
    // those move along / pitch the hidden control rotation (the cause of "W/S goes up/down" and the double-acting
    // mouse). Instead the mouse only orbits the camera, and movement is driven from routed key edges in Tick (enabled in Request_OnPawnReady)
    // and applied on the horizontal plane relative to the camera's view yaw.
    default bAddDefaultMovementBindings = false;

    // The director does NOT create this. _OutputComponent is the single essential constructor parameter of
    // FCk_Camera_Spec and is get-only, so the component has to exist on the actor before Add
    // is called - Add ensures on it and returns an invalid handle otherwise.
    UPROPERTY(DefaultComponent)
    UCk_CameraComponent CameraComponent;

    // Visible subject - ADefaultPawn shows nothing in-game, so the third-person / top-down / lock-on cameras
    // would frame empty space. Body sphere + forward nose cube make position and facing readable.
    UPROPERTY(DefaultComponent)
    UStaticMeshComponent BodyMesh;

    UPROPERTY(DefaultComponent)
    UStaticMeshComponent NoseMesh;

    private FCk_Handle _PawnEntity;
    private FCkCameraGym_LocalAdmission _LocalAdmission;
    private bool _CameraAttempted = false;
    private bool _CameraFailed = false;
    private bool _Ending = false;
    private float32 _StartupElapsed = 0.0f;
    private FCk_Handle_Camera _Camera;
    private FCk_Handle_Transform      _LockOnTarget;

    private TArray<TSubclassOf<UCk_CameraLayer_EntityScript>> _Modes;
    private int32                     _ModeIndex = 0;

    // Demo Trim toggle (FOV zoom) - layers over the active mode without being evicted on mode cycle.
    private bool                      _ZoomActive = false;

    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Sphere = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Sphere.Sphere"));
        auto Cube   = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Cube.Cube"));
        auto Mat    = Cast<UMaterialInterface>(LoadObject(this, "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

        if (Sphere != nullptr) { BodyMesh.SetStaticMesh(Sphere); }
        if (Mat    != nullptr) { BodyMesh.SetMaterial(0, Mat); }
        BodyMesh.SetRelativeScale3D(FVector(0.7f, 0.7f, 0.7f));
        BodyMesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);

        if (Cube != nullptr) { NoseMesh.SetStaticMesh(Cube); }
        if (Mat  != nullptr) { NoseMesh.SetMaterial(0, Mat); }
        NoseMesh.SetRelativeLocation(FVector(45.0f, 0.0f, 0.0f)); // pokes out +X (pawn forward)
        NoseMesh.SetRelativeScale3D(FVector(0.35f, 0.35f, 0.35f));
        NoseMesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    void OnEntityConstructed(FCk_Handle_EntityScript InEntityScriptHandle) override
    {
        if (_Ending) { return; }
        auto Returned = FCk_Handle(InEntityScriptHandle);
        if (ck::Is_NOT_Valid(Returned) ||
            utils_owning_actor::TryGet_ActorEntityHandle(this) != Returned)
        { Request_CloseCamera("Pawn completion has no exact actor entity"); return; }
        _PawnEntity = Returned;
        Request_OnPawnReady();
    }

    void Request_OnPawnReady() override
    {
        // A client receives its actor entity later. Tick reads that durable link;
        // it never creates a second camera on a remote pawn or dedicated server.
        SetActorTickEnabled(true);
    }

    private ACk_CameraGym_GameState Get_ExactState()
    {
        auto World = GetWorld();
        if (_Ending || IsActorBeingDestroyed() || ck::Is_NOT_Valid(World)) { return nullptr; }
        auto State = Cast<ACk_CameraGym_GameState>(World.GetGameState());
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != World ||
            State.GetClass() != ACk_CameraGym_GameState || State.IsActorBeingDestroyed())
        { return nullptr; }
        return State;
    }

    private ACk_CameraGym_PlayerController Get_ExactLocalOwner()
    {
        auto Owner = Cast<ACk_CameraGym_PlayerController>(GetController());
        auto World = GetWorld();
        if (_Ending || IsActorBeingDestroyed() || ck::Is_NOT_Valid(World) ||
            World.GetNetMode() == ENetMode::NM_DedicatedServer ||
            ck::Is_NOT_Valid(Owner) || Owner.IsActorBeingDestroyed() ||
            Owner.GetClass() != ACk_CameraGym_PlayerController ||
            !Owner.IsLocalController() || Owner.GetWorld() != World ||
            Owner.GetControlledPawn() != this || ck::Is_NOT_Valid(Owner.GetLocalPlayer()))
        { return nullptr; }
        return Owner;
    }

    private void Request_CloseCamera(FString Reason)
    {
        if (_CameraFailed) { return; }
        _CameraFailed = true;
        Request_ResetMovementKeys();
        UCk_CameraGym_LocalAdmission_UE::TryClose_LocalCamera(_LocalAdmission);
        _Camera = FCk_Handle_Camera();
        _LockOnTarget = FCk_Handle_Transform();
        ck::Error("Camera gym local setup closed: " + Reason);
    }

    private void Request_EnsureCamera(float32 DeltaSeconds)
    {
        if (_Ending || _CameraFailed) { return; }
        auto State = Get_ExactState();
        auto CurrentEntity = utils_owning_actor::TryGet_ActorEntityHandle(this);
        if (HasAuthority() && ck::IsValid(State) && ck::IsValid(CurrentEntity))
        { State.TryEnsureReferenceGeometry(this); }
        auto Owner = Get_ExactLocalOwner();
        if (ck::Is_NOT_Valid(Owner))
        {
            if (_CameraAttempted) { Request_CloseCamera("Local possession was lost"); }
            return;
        }
        if (!Math::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
        { Request_CloseCamera("Startup delta is invalid"); return; }
        _StartupElapsed += DeltaSeconds;
        if (!Owner.Get_IsCameraControllerAdmitted())
        {
            if (_StartupElapsed >= 20.0f)
            { Request_CloseCamera("Local controller admission timed out"); }
            return;
        }
        if (ck::Is_NOT_Valid(State))
        {
            if (_CameraAttempted || _StartupElapsed >= 20.0f)
            { Request_CloseCamera("Exact shared state is unavailable"); }
            return;
        }
        auto Readiness = State.Get_ReferenceReadiness();
        if (Readiness == ECkCameraGym_SourceReadiness::Failed)
        { Request_CloseCamera("Shared reference bank failed"); return; }
        if (Readiness != ECkCameraGym_SourceReadiness::Ready || ck::Is_NOT_Valid(CurrentEntity))
        {
            if (_CameraAttempted || _StartupElapsed >= 20.0f)
            { Request_CloseCamera("Shared bank or pawn entity did not converge"); }
            return;
        }
        if (_CameraAttempted)
        {
            if (_PawnEntity != CurrentEntity ||
                !UCk_CameraGym_LocalAdmission_UE::Get_IsExactLocalAdmission(_LocalAdmission))
            { Request_CloseCamera("Local camera admission changed"); }
            return;
        }
        _CameraAttempted = true;
        _PawnEntity = CurrentEntity;
        if (!UCk_CameraGym_LocalAdmission_UE::TryCreate_LocalCamera(this, Owner,
            Owner.GetLocalPlayer(), GetWorld(), _PawnEntity, State.SourceActors[9],
            CameraComponent, _LocalAdmission))
        { Request_CloseCamera("Local camera composition was rejected"); return; }
        _Camera = UCk_CameraGym_LocalAdmission_UE::Get_Director(_LocalAdmission);
        _LockOnTarget = UCk_CameraGym_LocalAdmission_UE::Get_Target(_LocalAdmission);
        _Modes.Add(UCk_CameraLayer_ThirdPerson);
        _Modes.Add(UCk_CameraLayer_TopDown);
        _Modes.Add(UCk_CameraLayer_LockOn);
        _ModeIndex = 0;
        Request_ApplyMode();
    }

    UFUNCTION()
    bool Get_IsLocalCameraReady()
    {
        if (_CameraFailed || !_CameraAttempted || ck::Is_NOT_Valid(Get_ExactLocalOwner()))
        { return false; }
        auto State = Get_ExactState();
        return ck::IsValid(State) &&
            State.Get_ReferenceReadiness() == ECkCameraGym_SourceReadiness::Ready &&
            UCk_CameraGym_LocalAdmission_UE::Get_IsExactLocalAdmission(_LocalAdmission);
    }

    UFUNCTION(BlueprintPure)
    FCk_Handle Get_LocalCameraScope()
    { return UCk_CameraGym_LocalAdmission_UE::Get_Scope(_LocalAdmission); }
    void Request_CloseOwnerInputFailure(FString Reason) { Request_CloseCamera(Reason); }
    private FVector2D _PendingLook = FVector2D::ZeroVector;
    private bool _ForwardHeld = false;
    private bool _BackHeld = false;
    private bool _RightHeld = false;
    private bool _LeftHeld = false;
    private bool _UpHeld = false;
    private bool _DownHeld = false;
    void Request_ResetMovementKeys()
    {
        _PendingLook = FVector2D::ZeroVector;
        _ForwardHeld = false; _BackHeld = false; _RightHeld = false;
        _LeftHeld = false; _UpHeld = false; _DownHeld = false;
    }
    void Request_MovementKey(FKey Key, bool Pressed)
    {
        // Release is always safe for this pawn's held state, including focus or
        // possession loss. A new press requires its current local camera.
        if (Pressed && !Get_IsLocalCameraReady()) { return; }
        if (Key == EKeys::W) { _ForwardHeld = Pressed; }
        else if (Key == EKeys::S) { _BackHeld = Pressed; }
        else if (Key == EKeys::D) { _RightHeld = Pressed; }
        else if (Key == EKeys::A) { _LeftHeld = Pressed; }
        else if (Key == EKeys::SpaceBar) { _UpHeld = Pressed; }
        else if (Key == EKeys::LeftControl) { _DownHeld = Pressed; }
    }
    bool Get_HasCameraFailure() const { return _CameraFailed; }
    UFUNCTION(BlueprintPure)
    int32 Get_ModeIndex() const { return _ModeIndex; }
    bool Get_IsZoomActive() const { return _ZoomActive; }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        Request_ResetMovementKeys();
        UCk_CameraGym_LocalAdmission_UE::TryClose_LocalCamera(_LocalAdmission);
        _Camera = FCk_Handle_Camera();
        _LockOnTarget = FCk_Handle_Transform();
        Super::EndPlay(Reason);
    }

    // Mouse-look sensitivity scales routed MouseX/MouseY deltas into a roughly-normalized orbit intention.
    // The modifier's OrientationControl speeds (deg/sec) then set the actual orbit rate. Bump this for faster look.
    const float32 _LookSensitivity = 0.05f;

    void Request_Look(FVector2D InLook)
    {
        if (!Get_IsLocalCameraReady() || InLook.ContainsNaN() ||
            !Math::IsFinite(InLook.X) || !Math::IsFinite(InLook.Y))
        { return; }

        // X = yaw intention, Y = pitch intention. User-pref invert would also be applied here in a real game.
        auto Pending = _PendingLook + InLook;
        if (Pending.ContainsNaN() || !Math::IsFinite(Pending.X) || !Math::IsFinite(Pending.Y))
        { return; }
        _PendingLook = Pending;
    }

    // Free-fly movement, driven from routed held state each frame and applied relative to the camera's view yaw so "forward" always means
    // "into the screen" regardless of where the pawn faces. Horizontal only (camera pitch is ignored) - Space/Ctrl
    // handle vertical. ADefaultPawn's default WASD/mouse-look are disabled (see class defaults), so nothing fights this.
    UFUNCTION(BlueprintOverride)
    void Tick(float32 InDeltaSeconds)
    {
        auto _CkPerfScope = ck::ScopedStat();
        Request_EnsureCamera(InDeltaSeconds);
        if (!Get_IsLocalCameraReady())
        { Request_ResetMovementKeys(); return; }

        auto PC = Get_ExactLocalOwner();
        if (ck::Is_NOT_Valid(PC))
        { Request_ResetMovementKeys(); return; }

        // A key held before the menu opened remains owned by this input layer
        // until its physical release. Clear its local latch now: the menu's
        // higher-priority catch-all masks NEW presses but sends no synthetic
        // release merely because it opened. The same rule clears a pending
        // mouse delta, and zeroes the last orientation intention.
        auto LocalPlayer = PC.GetLocalPlayer();
        auto Switchboard = UCkGym_Switchboard_Subsystem::Get(PC);
        const bool HasExactSwitchboard = ck::IsValid(LocalPlayer) &&
            UCk_CameraGym_LocalAdmission_UE::Get_HasExactLocalPlayerWorld(LocalPlayer, GetWorld()) &&
            ck::IsValid(Switchboard) && Switchboard.GetOuter() == LocalPlayer;
        if (!HasExactSwitchboard || Switchboard.Get_IsOpen())
        {
            Request_ResetMovementKeys();
            _Camera.Request_SetOrientationIntention(FVector::ZeroVector);
            return;
        }

        // Source axes arrive separately. Publish their accumulated pair once
        // per frame so a MouseY event cannot erase this frame's MouseX intention.
        _Camera.Request_SetOrientationIntention(FVector(
            _PendingLook.X * _LookSensitivity, _PendingLook.Y * _LookSensitivity, 0.0f));
        _PendingLook = FVector2D::ZeroVector;

        const auto YawRot  = FRotator(0.0f, utils_camera::Get_ViewRotation(_Camera).Yaw, 0.0f);
        const auto Forward = YawRot.GetForwardVector();
        const auto Right   = YawRot.GetRightVector();

        if (_ForwardHeld) { AddMovementInput(Forward,  1.0f); }
        if (_BackHeld) { AddMovementInput(Forward, -1.0f); }
        if (_RightHeld) { AddMovementInput(Right,    1.0f); }
        if (_LeftHeld) { AddMovementInput(Right,   -1.0f); }

        // Vertical free-fly - Space up, Left Ctrl down (E/Q are taken by mode cycling).
        if (_UpHeld)    { AddMovementInput(FVector(0.0f, 0.0f, 1.0f),  1.0f); }
        if (_DownHeld) { AddMovementInput(FVector(0.0f, 0.0f, 1.0f), -1.0f); }
    }

    void Request_CycleMode(int32 InDir)
    {
        if (!Get_IsLocalCameraReady() || _Modes.Num() != 3 ||
            (InDir != -1 && InDir != 1))
        { return; }

        _ModeIndex = (_ModeIndex + InDir + _Modes.Num()) % _Modes.Num();
        Request_ApplyMode();
    }

    // Toggles a FOV-zoom Trim that layers on top of whichever mode is active. Because it lives in its own ordering
    // group, cycling modes does not evict it; because the profile is rebuilt each frame, removing it just restores
    // the mode's FOV next frame.
    void Request_ToggleZoom()
    {
        if (!Get_IsLocalCameraReady())
        { return; }

        if (_ZoomActive)
        {
            auto Request = FCk_Request_Camera_RemoveLayer(UCk_CameraLayer_FovZoom);
            Request.Set_BlendOutTime(FCk_Time(0.2));
            _Camera.Request_RemoveLayer(Request);
            _ZoomActive = false;
            Print("Camera zoom: OFF", 4.0f);
        }
        else
        {
            auto Request = FCk_Request_Camera_AddLayer(UCk_CameraLayer_FovZoom);
            Request.Set_StackingBehavior(ECk_Camera_StackingBehavior::Additive);
            Request.Set_Priority(k_TrimPriority);
            Request.Set_BlendInTime(FCk_Time(0.2));
            _Camera.Request_AddLayer(Request);
            _ZoomActive = true;
            Print("Camera zoom: ON (FOV Trim layered over the active mode)", 4.0f);
        }
    }

    private void Request_ApplyMode()
    {
        if (!Get_IsLocalCameraReady())
        { return; }

        auto ModeClass = _Modes[_ModeIndex];

        // OneOnly (shared default ordering group) -> the new mode blends in and evicts the previous one.
        auto Request = FCk_Request_Camera_AddLayer(ModeClass);
        Request.Set_StackingBehavior(ECk_Camera_StackingBehavior::OneOnly);
        Request.Set_BlendInTime(FCk_Time(0.4));

        if (ModeClass == UCk_CameraLayer_LockOn)
        { Request.Set_CameraTarget(FCk_Camera_Target(_LockOnTarget, ECk_Camera_TargetMode::LookAt)); }

        _Camera.Request_AddLayer(Request);

        Print("Camera mode: " + Get_ModeName(ModeClass) + "   [E/Q to cycle, mouse to orbit]", 4.0f);
    }

    private FString Get_ModeName(TSubclassOf<UCk_CameraLayer_EntityScript> InClass) const
    {
        if (InClass == UCk_CameraLayer_ThirdPerson) { return "Third-Person (orbit + collision)"; }
        if (InClass == UCk_CameraLayer_TopDown)     { return "Top-Down (fixed framing)"; }
        if (InClass == UCk_CameraLayer_LockOn)      { return "Lock-On (auto-reorient to beacon)"; }
        return "Unknown";
    }
}
