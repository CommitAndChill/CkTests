// Language=angelscript

namespace ck_ui_authoring
{
    asset CkTests_PauseMenuProof_Layout of UCk_UI_LayoutConfigAsset_UE
    {
        FCk_UI_LayerConfig PauseLayer;
        PauseLayer._LayerTag = utils_gameplay_tag::ResolveGameplayTag(
            n"CkTests.UI.Layer.PauseMenuProof", "Pause menu layout");
        PauseLayer._LayerWidgetClass = UCk_UI_LayerStack_UE;
        PauseLayer.Set_InputMode(ECk_UI_InputMode::UIOnly);
        PauseLayer.Set_TransitionDuration(FCk_Time(0.0));
        _LayerConfigs.Add(PauseLayer);
    }
}
