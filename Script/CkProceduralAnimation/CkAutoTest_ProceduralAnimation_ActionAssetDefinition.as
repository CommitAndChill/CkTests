// Language=angelscript

// A clip declared as a script asset literal compiles and copies into a builder draft like an authored asset.
class UCk_AutoTest_ProceduralAnimation_ActionAssetDefinition : UCk_AutoTest_Base
{
    default _AutoStageOriginField = false;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Clip = ck_procedural_action_fixture_assets::Ck_ProceduralAction_AssetRouteFixture;
        if (Clip == nullptr)
        {
            FinishFailure("The asset literal must exist");
            return;
        }
        const auto Validation = utils_procedural_action::Validate_Data(Clip);
        Assert_True(Validation.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid
            && Validation.Get_Advisory() == false,
            "The script-declared clip validates through the public surface");
        Assert_True(Clip.Get_BodyChannel().Get_Enabled() && Clip.Get_Slots().Num() == 1 && Clip.Get_Phases().Num() == 1,
            "The asset initializer filled the body channel, slots and phases");

        auto Draft = FCk_ProceduralAction_ClipDraft();
        const auto DraftReport = utils_procedural_action_builder::Make_Draft_FromData(Clip, Draft);
        Assert_True(DraftReport.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid
            && Draft.Get_Phases().Num() == 1 && Draft.Get_Slots().Num() == 1,
            "The script-declared clip copies into a builder draft");
        FinishSuccess();
    }
}
