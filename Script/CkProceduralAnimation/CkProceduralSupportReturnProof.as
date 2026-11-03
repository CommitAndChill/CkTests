class UCk_ProceduralSupportReturnProof : UObject
{
    UFUNCTION()
    bool CheckNativeEnvelope(FCk_Handle_SurfaceMotion InMotion)
    {
        if (ck::Is_NOT_Valid(InMotion))
        { return false; }
        return utils_surface_motion::Get_ContactSource(InMotion) == ECk_SurfaceMotion_ContactSource::NativeEnvelope;
    }
}
