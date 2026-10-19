// --------------------------------------------------------------------------------------------------------------------
// Showcase for a PostProcess look: an unbound UPostProcessComponent applies a CkUsf PostProcess-domain
// look (EdgeOutline) to the entire view. Proves the post-process + scene-texture capability end-to-end.
// --------------------------------------------------------------------------------------------------------------------

class ACk_UsfGym_PostProcessShowcase : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent SceneRoot;

    UPROPERTY(DefaultComponent)
    UPostProcessComponent PostProcess;

    // Unbound = affects the whole view regardless of volume bounds.
    default PostProcess.bUnbound = true;
    default PostProcess.BlendWeight = 0.0;

    private bool _Enabled = false;

    bool Get_IsEnabled() const
    { return _Enabled; }

    void Request_Toggle()
    {
        _Enabled = _Enabled == false;
        PostProcess.BlendWeight = _Enabled ? 1.0 : 0.0;
    }

    UPROPERTY()
    UCkUsf_LookDefinition Look = CkUsf::EdgeOutline;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto MID = UCk_Utils_Usf_UE::Apply_PostProcess_ToComponent(PostProcess, Look);
        if (MID != nullptr)
        {
            ck::Trace("[OK] CkUsf gym: edge-outline ready; press O to enable it");
        }
        else
        {
            ck::Warning("[FAIL] CkUsf gym: PostProcess MID failed - run 'Generate Look Materials' first");
        }
    }
}
