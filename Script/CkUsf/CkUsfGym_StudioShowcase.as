// Material specimens use the USD Working Group shader ball, imported with a
// named Specimen and CoreBase slots. See Resources/CkUsf.
class ACk_UsfGym_StudioShowcase : ACk_UsfGym_Showcase
{
    default Mesh.bDisallowNanite = true;

    UPROPERTY(DefaultComponent, Attach = Mesh)
    URectLightComponent StudioKey;

    UPROPERTY(DefaultComponent, Attach = Mesh)
    URectLightComponent StudioFill;

    UPROPERTY(DefaultComponent, Attach = Mesh)
    URectLightComponent StudioRim;

    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        auto Specimen = Cast<UStaticMesh>(LoadObject(this,
            "/CkTests/CkUsf/Showcase/SM_UsdShaderBall.SM_UsdShaderBall"));
        if (ck::EnsureIfNot(ck::IsValid(Specimen),
            "Import the licensed USD shader ball before opening the studio gallery"))
        { return; }
        Mesh.SetStaticMesh(Specimen);
        Mesh.SetLightingChannels(false, true, false);
    }

    void Request_ConfigurePresentation()
    {
        auto Stand = utils_usf::Create_MID_ForLook(CkUsf::StudioRubber, this);
        if (ck::EnsureIfNot(ck::IsValid(Stand), "Generate the studio look materials before PIE"))
        { return; }
        const auto CoreSlot = Mesh.GetMaterialIndex(n"CoreBase");
        if (ck::EnsureIfNot(CoreSlot >= 0, "The standard shader ball has no CoreBase material slot"))
        { return; }
        Mesh.SetMaterial(CoreSlot, Stand);

        Configure_Light(StudioKey, FVector(-140.0, -100.0, 140.0),
            FRotator(-35.0, 35.0, 0.0), 6000.0, 90.0, 140.0);
        Configure_Light(StudioFill, FVector(-70.0, 130.0, 45.0),
            FRotator(-15.0, -60.0, 0.0), 3600.0, 120.0, 120.0);
        Configure_Light(StudioRim, FVector(110.0, 10.0, 100.0),
            FRotator(-40.0, 180.0, 0.0), 3000.0, 35.0, 140.0);
    }

    private void Configure_Light(URectLightComponent InLight, FVector InLocation,
        FRotator InRotation, float InIntensity, float InWidth, float InHeight)
    {
        InLight.SetMobility(EComponentMobility::Movable);
        InLight.SetWorldLocation(GetActorLocation() + InLocation);
        InLight.SetWorldRotation(InRotation);
        InLight.SetLightingChannels(false, true, false);
        InLight.SetLightColor(FLinearColor::White);
        InLight.SetIntensity(InIntensity);
        InLight.SetSourceWidth(InWidth);
        InLight.SetSourceHeight(InHeight);
        InLight.SetAttenuationRadius(350.0);
        InLight.SetCastShadows(true);
    }
}

class ACk_UsfGym_StudioEnvironment : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent SceneRoot;

    UPROPERTY(DefaultComponent, Attach = SceneRoot)
    UPostProcessComponent Exposure;

    default Exposure.bUnbound = true;
    default Exposure.Priority = 10.0;
    default Exposure.Settings.bOverride_AutoExposureMinBrightness = true;
    default Exposure.Settings.bOverride_AutoExposureMaxBrightness = true;
    default Exposure.Settings.AutoExposureMinBrightness = 3.5;
    default Exposure.Settings.AutoExposureMaxBrightness = 3.5;
    default Exposure.Settings.bOverride_BloomIntensity = true;
    default Exposure.Settings.BloomIntensity = 0.1;
}

// Flat image/raymarch looks should retain a readable composition and UV domain.
class ACk_UsfGym_ImageShowcase : ACk_UsfGym_Showcase
{
    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        auto Display = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Plane.Plane"));
        if (ck::EnsureIfNot(ck::IsValid(Display), "The engine display plane is missing"))
        { return; }
        Mesh.SetStaticMesh(Display);
        Mesh.SetRelativeRotation(FRotator(90.0, 0.0, 0.0));
    }
}

// Keep normal-displacement tests on a continuous sphere without hard-edge splits.
class ACk_UsfGym_DisplaceShowcase : ACk_UsfGym_Showcase
{
    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        auto Sphere = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Sphere.Sphere"));
        if (ck::EnsureIfNot(ck::IsValid(Sphere), "The displacement sphere is missing"))
        { return; }
        Mesh.SetStaticMesh(Sphere);
    }
}
