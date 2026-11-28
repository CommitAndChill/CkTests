// Owned height-mapped planar specimens; the accepted shader-ball mesh is unchanged.
class ACk_UsfGym_GroundShowcase : ACk_UsfGym_StudioShowcase
{
    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        auto Ground = Cast<UStaticMesh>(LoadObject(this,
            "/CkTests/CkUsf/Showcase/SM_UsfWoodlandGrid.SM_UsfWoodlandGrid"));
        if (ck::EnsureIfNot(ck::IsValid(Ground), "Prepare the owned ground grid before opening the gym"))
        { return; }
        Mesh.SetStaticMesh(Ground);
        Mesh.SetLightingChannels(false, true, false);
    }
    // Reuse the same three studio Rects and intensities, aimed at the small horizontal patch.
    void Request_ConfigureGroundPresentation()
    {
        const auto Origin = GetActorLocation();
        StudioKey.SetWorldLocation(Origin + FVector(-65.0, -45.0, 95.0));
        StudioFill.SetWorldLocation(Origin + FVector(-45.0, 70.0, 75.0));
        StudioRim.SetWorldLocation(Origin + FVector(65.0, 10.0, 85.0));
        StudioKey.SetWorldRotation((Origin - StudioKey.GetWorldLocation()).Rotation());
        StudioFill.SetWorldRotation((Origin - StudioFill.GetWorldLocation()).Rotation());
        StudioRim.SetWorldRotation((Origin - StudioRim.GetWorldLocation()).Rotation());
    }
}
