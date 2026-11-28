class ACk_UsfGym_GlassPaneInspection : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent SceneRoot;

    UPROPERTY(DefaultComponent, Attach = SceneRoot)
    UStaticMeshComponent Background;

    UPROPERTY(DefaultComponent, Attach = SceneRoot)
    UStaticMeshComponent GlassPane;

    default Background.Mobility = EComponentMobility::Movable;
    default GlassPane.Mobility = EComponentMobility::Movable;

    bool Try_Configure(UMaterialInterface InGlass, UMaterialInterface InBackground)
    {
        auto Plane = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Plane.Plane"));
        if (ck::EnsureIfNot(ck::IsValid(Plane) && ck::IsValid(InGlass) && ck::IsValid(InBackground),
            "Glass inspection requires the reference plane and both materials"))
        { return false; }
        Background.SetStaticMesh(Plane);
        Background.SetMaterial(0, InBackground);
        Background.SetRelativeRotation(FRotator(90.0, 0.0, 0.0));
        Background.SetRelativeScale3D(FVector(1.6, 1.4, 1.0));
        Background.SetLightingChannels(false, true, false);
        Background.SetCastShadow(false);
        GlassPane.SetStaticMesh(Plane);
        GlassPane.SetMaterial(0, InGlass);
        GlassPane.SetRelativeLocation(FVector(-35.0, -35.0, 0.0));
        GlassPane.SetRelativeRotation(FRotator(90.0, 0.0, 0.0));
        GlassPane.SetRelativeScale3D(FVector(1.6, 0.7, 1.0));
        GlassPane.SetLightingChannels(false, true, false);
        GlassPane.SetCastShadow(false);
        return true;
    }
    bool Try_ConfigureFlat(UMaterialInterface InMaterial)
    {
        if (!Try_Configure(InMaterial, InMaterial))
        { return false; }
        Background.SetVisibility(false);
        GlassPane.SetRelativeLocation(FVector::ZeroVector);
        GlassPane.SetRelativeScale3D(FVector(1.6, 1.4, 1.0));
        return true;
    }
}
