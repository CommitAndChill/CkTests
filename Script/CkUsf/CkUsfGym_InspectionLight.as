// One selected-station inspection light. The ordinary studio rig is restored on every exit.
class ACk_UsfGym_InspectionLight : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent SceneRoot;

    UPROPERTY(DefaultComponent, Attach = SceneRoot)
    UPointLightComponent MovingLight;

    default MovingLight.Mobility = EComponentMobility::Movable;
    default MovingLight.Intensity = 0.0;

    UPROPERTY(NotEditable, BlueprintReadOnly)
    ACk_UsfGym_StudioShowcase Target;

    UPROPERTY(NotEditable, BlueprintReadOnly)
    private URectLightComponent _SavedKey;

    UPROPERTY(NotEditable, BlueprintReadOnly)
    private URectLightComponent _SavedFill;

    UPROPERTY(NotEditable, BlueprintReadOnly)
    private URectLightComponent _SavedRim;

    private float _KeyIntensity = 0.0;
    private float _FillIntensity = 0.0;
    private float _RimIntensity = 0.0;

    UPROPERTY(NotEditable, BlueprintReadOnly)
    float PhaseDegrees = 0.0;

    UPROPERTY(NotEditable, BlueprintReadOnly)
    float OrbitRadiusCm = 210.0;

    UPROPERTY(NotEditable, BlueprintReadOnly)
    float OrbitHeightCm = 90.0;

    UPROPERTY(NotEditable, BlueprintReadOnly)
    bool Enabled = false;

    UPROPERTY(NotEditable, BlueprintReadOnly)
    bool Paused = false;

    UPROPERTY(NotEditable, BlueprintReadOnly)
    float AngularSpeedDegrees = 20.0;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Tags.Add(n"Usf.InspectionOrbit");
        MovingLight.SetLightingChannels(false, true, false);
        MovingLight.SetLightColor(FLinearColor::White);
        MovingLight.SetIntensityUnits(ELightUnits::Candelas);
        MovingLight.SetCastShadows(true);
        MovingLight.SetVisibility(false);
        SetActorTickEnabled(false);
    }

    UFUNCTION()
    bool Try_Enable(ACk_UsfGym_StudioShowcase InTarget)
    {
        if (Enabled && InTarget == Target && ck::IsValid(Target))
        { return true; }
        Request_Disable();
        if (ck::EnsureIfNot(ck::IsValid(InTarget), "Select a studio specimen before enabling its inspection light"))
        { return false; }
        if (ck::EnsureIfNot(ck::IsValid(InTarget.Mesh) && ck::IsValid(MovingLight), "The inspection mesh or light is unavailable"))
        { return false; }
        if (ck::EnsureIfNot(ck::IsValid(InTarget.Mesh.GetStaticMesh()), "The selected inspection mesh has no geometry"))
        { return false; }
        if (ck::EnsureIfNot(ck::IsValid(InTarget.StudioKey) && ck::IsValid(InTarget.StudioFill)
            && ck::IsValid(InTarget.StudioRim), "The selected studio rig is incomplete"))
        { return false; }
        auto Minimum = FVector::ZeroVector;
        auto Maximum = FVector::ZeroVector;
        InTarget.Mesh.GetLocalBounds(Minimum, Maximum);
        auto Center = InTarget.GetActorTransform().TransformPosition((Minimum + Maximum) * 0.5);
        if (ck::EnsureIfNot(Center.ContainsNaN() == false && Math::IsFinite(InTarget.StudioKey.Intensity)
            && Math::IsFinite(InTarget.StudioFill.Intensity) && Math::IsFinite(InTarget.StudioRim.Intensity),
            "The selected inspection bounds or rig intensities are invalid"))
        { return false; }

        Target = InTarget;
        _SavedKey = InTarget.StudioKey;
        _SavedFill = InTarget.StudioFill;
        _SavedRim = InTarget.StudioRim;
        _KeyIntensity = _SavedKey.Intensity;
        _FillIntensity = _SavedFill.Intensity;
        _RimIntensity = _SavedRim.Intensity;
        const auto Ground = Cast<ACk_UsfGym_GroundShowcase>(InTarget);
        OrbitRadiusCm = ck::IsValid(Ground) ? 100.0 : 210.0;
        OrbitHeightCm = ck::IsValid(Ground) ? 70.0 : 90.0;
        MovingLight.SetSourceRadius(ck::IsValid(Ground) ? 6.0 : 12.0);
        MovingLight.SetAttenuationRadius(ck::IsValid(Ground) ? 200.0 : 260.0);
        _SavedKey.SetIntensity(0.0);
        _SavedFill.SetIntensity(_FillIntensity * 0.15);
        _SavedRim.SetIntensity(0.0);
        Enabled = true;
        if (Try_UpdateTransform() == false)
        {
            Request_Disable();
            return false;
        }
        MovingLight.SetIntensity(ck::IsValid(Ground) ? 40.0 : 120.0);
        MovingLight.SetVisibility(true);
        SetActorTickEnabled(true);
        return true;
    }

    UFUNCTION()
    void Request_Disable()
    {
        SetActorTickEnabled(false);
        if (ck::IsValid(MovingLight))
        {
            MovingLight.SetIntensity(0.0);
            MovingLight.SetVisibility(false);
        }
        if (Enabled)
        {
            if (ck::IsValid(_SavedKey)) { _SavedKey.SetIntensity(_KeyIntensity); }
            if (ck::IsValid(_SavedFill)) { _SavedFill.SetIntensity(_FillIntensity); }
            if (ck::IsValid(_SavedRim)) { _SavedRim.SetIntensity(_RimIntensity); }
        }
        Enabled = false;
        Target = nullptr;
        _SavedKey = nullptr;
        _SavedFill = nullptr;
        _SavedRim = nullptr;
    }

    UFUNCTION()
    void Request_SetPaused(bool InPaused)
    { Paused = InPaused; }

    UFUNCTION()
    void Request_SetPhaseDegrees(float InPhaseDegrees)
    {
        if (ck::EnsureIfNot(Math::IsFinite(InPhaseDegrees), "Inspection phase must be finite"))
        { return; }
        PhaseDegrees = Math::Fmod(Math::Fmod(InPhaseDegrees, 360.0) + 360.0, 360.0);
        if (Enabled && Try_UpdateTransform() == false)
        { Request_Disable(); }
    }

    UFUNCTION()
    bool Get_IsEnabled() const
    { return Enabled; }

    UFUNCTION()
    bool Get_IsPaused() const
    { return Paused; }

    UFUNCTION()
    float Get_PhaseDegrees() const
    { return PhaseDegrees; }

    UFUNCTION()
    ACk_UsfGym_StudioShowcase Get_Target() const
    { return Target; }

    UFUNCTION()
    FVector Get_OriginalRigIntensities() const
    { return FVector(_KeyIntensity, _FillIntensity, _RimIntensity); }

    UFUNCTION(BlueprintOverride)
    void Tick(float InDeltaSeconds)
    {
        if (ck::Is_NOT_Valid(Target) || ck::Is_NOT_Valid(_SavedKey) || ck::Is_NOT_Valid(_SavedFill)
            || ck::Is_NOT_Valid(_SavedRim) || ck::Is_NOT_Valid(MovingLight))
        {
            Request_Disable();
            return;
        }
        if (Paused == false)
        { PhaseDegrees = Math::Fmod(PhaseDegrees + InDeltaSeconds * AngularSpeedDegrees, 360.0); }
        if (Try_UpdateTransform() == false)
        { Request_Disable(); }
    }

    private bool Try_UpdateTransform()
    {
        if (ck::Is_NOT_Valid(Target) || ck::Is_NOT_Valid(Target.Mesh) || ck::Is_NOT_Valid(MovingLight))
        { return false; }
        if (ck::Is_NOT_Valid(Target.Mesh.GetStaticMesh()))
        { return false; }
        auto Minimum = FVector::ZeroVector;
        auto Maximum = FVector::ZeroVector;
        Target.Mesh.GetLocalBounds(Minimum, Maximum);
        auto Center = Target.GetActorTransform().TransformPosition((Minimum + Maximum) * 0.5);
        const auto Radians = Math::DegreesToRadians(PhaseDegrees);
        const auto Position = Center + FVector(-Math::Cos(Radians) * OrbitRadiusCm, Math::Sin(Radians) * OrbitRadiusCm, OrbitHeightCm);
        if (Center.ContainsNaN() || Position.ContainsNaN())
        { return false; }
        MovingLight.SetWorldLocation(Position);
        MovingLight.SetWorldRotation((Center - Position).Rotation());
        return true;
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason EndPlayReason)
    { Request_Disable(); }
}
