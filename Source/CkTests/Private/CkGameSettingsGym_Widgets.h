#pragma once

#include "CkGameSettings/UI/CkGameSettingsUI_ScreenWidget.h"
#include "CkGameSettings/UI/CkGameSettingsUI_KeyBindingPageWidget.h"
#include "CkGameSettings/UI/CkGameSettingsUI_RowWidgets.h"
#include "CommonButtonBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkGameSettingsGym_Widgets.generated.h"

class UButton;
class UCheckBox;
class UComboBoxString;
class USlider;
class UTextBlock;
class UVerticalBox;

/** Minimal concrete button: CommonButtonBase's own Initialize wraps this text root in its real input button. */
UCLASS()
class CKTESTS_API UCk_GameSettingsGym_Button final : public UCommonButtonBase
{
    GENERATED_BODY()
public:
    void Set_Label(const FText& Label);
#if WITH_DEV_AUTOMATION_TESTS
    void Test_DispatchNativeClick();
#endif
protected:
    void InitializeNativeClassData() override;
private:
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LabelText;
};

/** One native row class for the gym's bool, ranged numeric, options and rangeless stepper settings. */
UCLASS()
class CKTESTS_API UCk_GameSettingsGym_SettingRow final : public UCk_GameSettingsUI_RowWidgetBase
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure) bool Get_IsLayoutReady() const;
protected:
    void NativeOnInitialized() override;
    void NativeDestruct() override;
    void DoRefreshControl(const FCk_GameSettings_SettingDefinition& Definition,
        const FString& Value) override;
private:
    bool TryGetDefinition(FCk_GameSettings_SettingDefinition& Out) const;
    UFUNCTION() void OnChecked(bool Checked);
    UFUNCTION() void OnSlider(float Value);
    UFUNCTION() void OnSliderCaptureBegin();
    UFUNCTION() void OnSliderCaptureEnd();
    UFUNCTION() void OnSelected(FString Label, ESelectInfo::Type Selection);
    UFUNCTION() void OnPrevious();
    UFUNCTION() void OnNext();
    void Step(int32 Direction);
    void CommitSlider(float Value);

    UPROPERTY(Transient) TObjectPtr<UCheckBox> Checkbox;
    UPROPERTY(Transient) TObjectPtr<USlider> Slider;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> Options;
    UPROPERTY(Transient) TObjectPtr<UButton> Previous;
    UPROPERTY(Transient) TObjectPtr<UButton> Next;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ValueText;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> ControlBox;
    bool LayoutReady = false;
    bool SliderCaptureActive = false;
    bool HasSliderDraft = false;
    float SliderDraft = 0.f;
};

UCLASS()
class CKTESTS_API UCk_GameSettingsGym_Screen final : public UCk_GameSettingsUI_ScreenWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure) bool Get_IsLayoutReady() const;
    UFUNCTION(BlueprintPure) bool Get_HasCompleteDemoRows() const;
protected:
    void NativeOnInitialized() override;
private:
    bool LayoutReady = false;
};

UCLASS()
class CKTESTS_API UCk_GameSettingsGym_KeyRow final : public UCk_GameSettingsUI_KeyBindingRowWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure) bool Get_IsLayoutReady() const;
protected:
    void NativeOnInitialized() override;
};

UCLASS()
class CKTESTS_API UCk_GameSettingsGym_KeyPage : public UCk_GameSettingsUI_KeyBindingPageWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure) bool Get_IsLayoutReady() const;
    UFUNCTION(BlueprintPure) bool Get_HasCompleteMountedRows() const;
protected:
    void NativeOnInitialized() override;
    void NativeConstruct() override;
private:
    bool LayoutReady = false;
};

UCLASS()
class CKTESTS_API UCk_GameSettingsGym_Widgets final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category = "Ck|GameSettingsGym")
    static TSoftClassPtr<UCk_GameSettingsUI_RowWidgetBase> Get_SettingRowClass();
};
