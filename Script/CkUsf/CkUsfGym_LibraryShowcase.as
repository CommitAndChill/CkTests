// A disabled point component supports directional-highlight inspection without changing the studio lights.
class ACk_UsfGym_LibraryShowcase : ACk_UsfGym_StudioShowcase
{
    UPROPERTY(DefaultComponent, Attach = Mesh)
    UPointLightComponent DirectionalInspection;

    default DirectionalInspection.Intensity = 0.0;
    default DirectionalInspection.Mobility = EComponentMobility::Movable;
}
