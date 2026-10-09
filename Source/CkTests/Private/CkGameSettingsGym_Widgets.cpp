#include "CkGameSettingsGym_Widgets.h"

#include "CkGameSettings/Subsystem/CkGameSettings_Utils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace ck_game_settings_gym_widgets
{
    template <typename T>
    T* Add(UWidgetTree* Tree, UPanelWidget* Parent, const FName Name)
    {
        if (!IsValid(Tree) || !IsValid(Parent)) return nullptr;
        auto* Widget = Tree->ConstructWidget<T>(T::StaticClass(), Name);
        if (!IsValid(Widget) || !IsValid(Parent->AddChild(Widget))) return nullptr;
        return Widget;
    }

    UTextBlock* AddText(UWidgetTree* Tree, UPanelWidget* Parent,
        const FName Name, const TCHAR* Text)
    {
        auto* Widget = Add<UTextBlock>(Tree, Parent, Name);
        if (IsValid(Widget)) Widget->SetText(FText::FromString(Text));
        return Widget;
    }
}

void UCk_GameSettingsGym_Button::InitializeNativeClassData()
{
    Super::InitializeNativeClassData();
    if (!IsValid(WidgetTree))
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
    if (!IsValid(WidgetTree)) return;
    LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label"));
    if (IsValid(LabelText)) WidgetTree->RootWidget = LabelText;
}

void UCk_GameSettingsGym_Button::Set_Label(const FText& Label)
{
    if (IsValid(LabelText)) LabelText->SetText(Label);
}

#if WITH_DEV_AUTOMATION_TESTS
void UCk_GameSettingsGym_Button::Test_DispatchNativeClick()
{
    // CommonUI owns FCommonButtonEvent::Broadcast; invoke its real dispatch method.
    if (GetIsEnabled()) NativeOnClicked();
}
#endif

bool UCk_GameSettingsGym_SettingRow::Get_IsLayoutReady() const
{
    if (!LayoutReady || !IsValid(WidgetTree)) return false;
    auto* Root = Cast<UHorizontalBox>(WidgetTree->RootWidget);
    if (!IsValid(Root) || !IsValid(ControlBox) ||
        ControlBox->GetParent() != Root ||
        !IsValid(_DisplayNameText) || _DisplayNameText->GetParent() != Root ||
        !IsValid(Checkbox) || Checkbox->GetParent() != ControlBox ||
        !IsValid(Slider) || Slider->GetParent() != ControlBox ||
        !IsValid(Options) || Options->GetParent() != ControlBox ||
        !IsValid(ValueText) || ValueText->GetParent() != ControlBox ||
        !IsValid(Previous) || !IsValid(Next)) return false;
    auto* StepBox = Cast<UHorizontalBox>(Previous->GetParent());
    return IsValid(StepBox) && StepBox->GetParent() == ControlBox &&
        Next->GetParent() == StepBox &&
        Previous->GetChildrenCount() == 1 && Next->GetChildrenCount() == 1;
}

void UCk_GameSettingsGym_SettingRow::NativeDestruct()
{
    SliderCaptureActive = false;
    HasSliderDraft = false;
    Super::NativeDestruct();
}

void UCk_GameSettingsGym_SettingRow::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if (!IsValid(WidgetTree)) return;
    auto* Root = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("SettingRoot"));
    if (!IsValid(Root)) return;
    WidgetTree->RootWidget = Root;
    _DisplayNameText = ck_game_settings_gym_widgets::AddText(
        WidgetTree, Root, TEXT("SettingName"), TEXT("Setting"));
    ControlBox = ck_game_settings_gym_widgets::Add<UVerticalBox>(
        WidgetTree, Root, TEXT("ControlBox"));
    if (!IsValid(ControlBox)) return;
    Checkbox = ck_game_settings_gym_widgets::Add<UCheckBox>(
        WidgetTree, ControlBox, TEXT("Toggle"));
    Slider = ck_game_settings_gym_widgets::Add<USlider>(
        WidgetTree, ControlBox, TEXT("Slider"));
    Options = ck_game_settings_gym_widgets::Add<UComboBoxString>(
        WidgetTree, ControlBox, TEXT("Options"));
    auto* StepBox = ck_game_settings_gym_widgets::Add<UHorizontalBox>(
        WidgetTree, ControlBox, TEXT("Stepper"));
    Previous = ck_game_settings_gym_widgets::Add<UButton>(
        WidgetTree, StepBox, TEXT("Previous"));
    Next = ck_game_settings_gym_widgets::Add<UButton>(
        WidgetTree, StepBox, TEXT("Next"));
    ck_game_settings_gym_widgets::AddText(
        WidgetTree, Previous, TEXT("PreviousLabel"), TEXT("-"));
    ck_game_settings_gym_widgets::AddText(
        WidgetTree, Next, TEXT("NextLabel"), TEXT("+"));
    ValueText = ck_game_settings_gym_widgets::AddText(
        WidgetTree, ControlBox, TEXT("Value"), TEXT(""));
    LayoutReady = true;
    LayoutReady = Get_IsLayoutReady();
    if (!LayoutReady) { SetIsEnabled(false); return; }
    Checkbox->OnCheckStateChanged.AddDynamic(this, &UCk_GameSettingsGym_SettingRow::OnChecked);
    Slider->OnValueChanged.AddDynamic(this, &UCk_GameSettingsGym_SettingRow::OnSlider);
    Slider->OnMouseCaptureBegin.AddDynamic(this, &UCk_GameSettingsGym_SettingRow::OnSliderCaptureBegin);
    Slider->OnMouseCaptureEnd.AddDynamic(this, &UCk_GameSettingsGym_SettingRow::OnSliderCaptureEnd);
    Slider->OnControllerCaptureBegin.AddDynamic(this, &UCk_GameSettingsGym_SettingRow::OnSliderCaptureBegin);
    Slider->OnControllerCaptureEnd.AddDynamic(this, &UCk_GameSettingsGym_SettingRow::OnSliderCaptureEnd);
    Options->OnSelectionChanged.AddDynamic(this, &UCk_GameSettingsGym_SettingRow::OnSelected);
    Previous->OnClicked.AddDynamic(this, &UCk_GameSettingsGym_SettingRow::OnPrevious);
    Next->OnClicked.AddDynamic(this, &UCk_GameSettingsGym_SettingRow::OnNext);
}

bool UCk_GameSettingsGym_SettingRow::TryGetDefinition(
    FCk_GameSettings_SettingDefinition& Out) const
{
    const auto* WorldContext = Get_WorldContext();
    const auto* Screen = Cast<UCk_GameSettingsGym_Screen>(WorldContext);
    const auto* Owner = GetOwningPlayer();
    const auto* World = GetWorld();
    if (!Get_IsLayoutReady() || Get_SettingKey().IsNone() ||
        !IsValid(Screen) || !Screen->Get_IsLayoutReady() ||
        !Screen->IsActivated() || !Screen->IsInViewport() ||
        !IsValid(Owner) || Owner->IsActorBeingDestroyed() ||
        !Owner->IsLocalController() ||
        !IsValid(World) || World->bIsTearingDown ||
        World->IsBeingCleanedUp() ||
        Screen->GetWorld() != World || Owner->GetWorld() != World ||
        Screen->GetOwningPlayer() != Owner ||
        Screen->GetOwningLocalPlayer() != Owner->GetLocalPlayer() ||
        GetOwningLocalPlayer() != Owner->GetLocalPlayer() ||
        GetParent() != Screen->WidgetTree->RootWidget ||
        GetVisibility() != ESlateVisibility::Visible)
        return false;
    return UCk_Utils_GameSettings_UE::Get_SettingDefinition(
        WorldContext, Get_SettingKey(), Out);
}

void UCk_GameSettingsGym_SettingRow::DoRefreshControl(
    const FCk_GameSettings_SettingDefinition& Definition, const FString& Value)
{
    if (!Get_IsLayoutReady()) return;
    const bool Bool = Definition.Get_ValueType() == ECk_GameSettings_ValueType::Bool;
    const bool Choices = Definition.Get_Options().Num() > 0;
    const bool Numeric = Definition.Get_ValueType() == ECk_GameSettings_ValueType::Int32 ||
        Definition.Get_ValueType() == ECk_GameSettings_ValueType::Float;
    const bool Ranged = Numeric && !Definition.Get_MinValue().IsEmpty() &&
        !Definition.Get_MaxValue().IsEmpty();
    Checkbox->SetVisibility(Bool ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    Slider->SetVisibility(Ranged && !Choices ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    Options->SetVisibility(Choices ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    auto* StepBox = Previous->GetParent();
    if (IsValid(StepBox))
        StepBox->SetVisibility(Numeric && !Ranged && !Choices ?
            ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    ValueText->SetText(FText::FromString(Value));
    if (Bool) Checkbox->SetIsChecked(Value.Equals(TEXT("true"), ESearchCase::IgnoreCase));
    if (Ranged && !Choices)
    {
        float Min = 0.f, Max = 0.f, Current = 0.f;
        if (LexTryParseString(Min, *Definition.Get_MinValue()) &&
            LexTryParseString(Max, *Definition.Get_MaxValue()) &&
            LexTryParseString(Current, *Value) &&
            FMath::IsFinite(Min) && FMath::IsFinite(Max) &&
            FMath::IsFinite(Current) && Min < Max)
        {
            Slider->SetMinValue(Min);
            Slider->SetMaxValue(Max);
            Slider->SetValue(FMath::Clamp(Current, Min, Max));
        }
        else { Slider->SetIsEnabled(false); }
    }
    if (Choices)
    {
        Options->ClearOptions();
        for (const auto& Option : Definition.Get_Options())
            Options->AddOption(Option.Get_Label().ToString());
        const int32 Index = Get_CurrentOptionIndex(Definition, Value);
        if (Definition.Get_Options().IsValidIndex(Index))
            Options->SetSelectedIndex(Index);
        else Options->ClearSelection();
    }
}

void UCk_GameSettingsGym_SettingRow::OnChecked(bool Checked)
{
    if (_RefreshingControl) return;
    FCk_GameSettings_SettingDefinition Definition;
    if (TryGetDefinition(Definition) &&
        Definition.Get_ValueType() == ECk_GameSettings_ValueType::Bool &&
        Checkbox->GetVisibility() == ESlateVisibility::Visible)
        DoCommitOptionValue(Definition, Checked ? TEXT("true") : TEXT("false"));
}

void UCk_GameSettingsGym_SettingRow::OnSlider(float Value)
{
    if (_RefreshingControl || !FMath::IsFinite(Value)) return;
    FCk_GameSettings_SettingDefinition Definition;
    if (!TryGetDefinition(Definition) ||
        Slider->GetVisibility() != ESlateVisibility::Visible ||
        (Definition.Get_ValueType() != ECk_GameSettings_ValueType::Int32 &&
         Definition.Get_ValueType() != ECk_GameSettings_ValueType::Float)) return;
    if (IsValid(ValueText)) ValueText->SetText(FText::FromString(LexToString(Value)));
    if (SliderCaptureActive)
    { SliderDraft = Value; HasSliderDraft = true; return; }
    CommitSlider(Value);
}

void UCk_GameSettingsGym_SettingRow::OnSliderCaptureBegin()
{
    FCk_GameSettings_SettingDefinition Definition;
    if (!TryGetDefinition(Definition) ||
        Slider->GetVisibility() != ESlateVisibility::Visible ||
        (Definition.Get_ValueType() != ECk_GameSettings_ValueType::Int32 &&
         Definition.Get_ValueType() != ECk_GameSettings_ValueType::Float)) return;
    SliderCaptureActive = true;
    HasSliderDraft = false;
}

void UCk_GameSettingsGym_SettingRow::OnSliderCaptureEnd()
{
    if (!SliderCaptureActive) return;
    SliderCaptureActive = false;
    FCk_GameSettings_SettingDefinition Definition;
    if (!TryGetDefinition(Definition)) { HasSliderDraft = false; return; }
    if (!HasSliderDraft && !IsValid(Slider)) return;
    const float Value = HasSliderDraft ? SliderDraft : Slider->GetValue();
    HasSliderDraft = false;
    if (FMath::IsFinite(Value)) CommitSlider(Value);
}

void UCk_GameSettingsGym_SettingRow::CommitSlider(float Value)
{
    if (_RefreshingControl || !Get_IsLayoutReady() || !FMath::IsFinite(Value)) return;
    FCk_GameSettings_SettingDefinition Definition;
    if (!TryGetDefinition(Definition) || Definition.Get_Options().Num() > 0) return;
    float Min = 0.f, Max = 0.f;
    if (!LexTryParseString(Min, *Definition.Get_MinValue()) ||
        !LexTryParseString(Max, *Definition.Get_MaxValue()) ||
        !FMath::IsFinite(Min) || !FMath::IsFinite(Max) || Min >= Max)
        return;
    const float Bounded = FMath::Clamp(Value, Min, Max);
    if (Definition.Get_ValueType() == ECk_GameSettings_ValueType::Int32)
    {
        if (Bounded < static_cast<float>(MIN_int32) ||
            Bounded >= static_cast<float>(MAX_int32)) return;
        DoCommitOptionValue(Definition, LexToString(FMath::RoundToInt32(Bounded)));
    }
    else if (Definition.Get_ValueType() == ECk_GameSettings_ValueType::Float)
    {
        const float Step = Definition.Get_OptionalStepSize() > 0.f ?
            Definition.Get_OptionalStepSize() : (Max - Min) / 100.f;
        if (!FMath::IsFinite(Step) || Step <= 0.f) return;
        const float GridPosition = (Bounded - Min) / Step;
        if (!FMath::IsFinite(GridPosition)) return;
        const float Snapped = FMath::Clamp(
            Min + FMath::RoundToFloat(GridPosition) * Step, Min, Max);
        if (FMath::IsFinite(Snapped))
            DoCommitOptionValue(Definition, LexToString(Snapped));
    }
}

void UCk_GameSettingsGym_SettingRow::OnSelected(
    FString Label, ESelectInfo::Type Selection)
{
    if (_RefreshingControl || Selection == ESelectInfo::Direct) return;
    FCk_GameSettings_SettingDefinition Definition;
    if (!TryGetDefinition(Definition) ||
        Options->GetVisibility() != ESlateVisibility::Visible) return;
    const int32 Index = Options->GetSelectedIndex();
    if (Definition.Get_Options().IsValidIndex(Index))
        DoCommitOptionValue(Definition, Definition.Get_Options()[Index].Get_Value());
}

void UCk_GameSettingsGym_SettingRow::OnPrevious() { Step(-1); }
void UCk_GameSettingsGym_SettingRow::OnNext() { Step(1); }

void UCk_GameSettingsGym_SettingRow::Step(int32 Direction)
{
    FCk_GameSettings_SettingDefinition Definition;
    if (!TryGetDefinition(Definition) ||
        Definition.Get_ValueType() != ECk_GameSettings_ValueType::Int32 ||
        Definition.Get_Options().Num() != 0 ||
        !Definition.Get_MinValue().IsEmpty() ||
        !Definition.Get_MaxValue().IsEmpty() ||
        Previous->GetParent()->GetVisibility() != ESlateVisibility::Visible) return;
    int32 Current = 0;
    if (!LexTryParseString(Current, *Get_CurrentValueString(Definition))) return;
    if ((Direction > 0 && Current == MAX_int32) ||
        (Direction < 0 && Current == MIN_int32)) return;
    DoCommitOptionValue(Definition, LexToString(Current + Direction));
}

void UCk_GameSettingsGym_Screen::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if (!IsValid(WidgetTree)) return;
    auto* Rows = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("SettingsRows"));
    if (!IsValid(Rows)) return;
    WidgetTree->RootWidget = Rows;
    _RowContainer = Rows;
    LayoutReady = true;
}

bool UCk_GameSettingsGym_Screen::Get_IsLayoutReady() const
{
    return LayoutReady && IsValid(WidgetTree) &&
        WidgetTree->RootWidget == _RowContainer && IsValid(_RowContainer);
}

bool UCk_GameSettingsGym_Screen::Get_HasCompleteDemoRows() const
{
    if (!Get_IsLayoutReady() || Get_GeneratedRowCount() != 5) return false;
    const auto* Panel = Cast<UPanelWidget>(WidgetTree->RootWidget);
    if (!IsValid(Panel) || Panel->GetChildrenCount() != 5) return false;
    static const FName Keys[] = {TEXT("gymdemo.toggle"), TEXT("gymdemo.volume"),
        TEXT("gymdemo.count"), TEXT("gymdemo.quality"), TEXT("gymdemo.lives")};
    for (int32 I = 0; I < 5; ++I)
    {
        const auto* Row = Cast<UCk_GameSettingsGym_SettingRow>(Panel->GetChildAt(I));
        if (!IsValid(Row) || !Row->Get_IsLayoutReady() ||
            Row->Get_SettingKey() != Keys[I] ||
            Row->GetOwningPlayer() != GetOwningPlayer() ||
            !Get_HasRowForKey(Keys[I]) ||
            Get_RowClassForKey(Keys[I]) != UCk_GameSettingsGym_SettingRow::StaticClass())
            return false;
    }
    return true;
}

void UCk_GameSettingsGym_KeyRow::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if (!IsValid(WidgetTree)) return;
    auto* Root = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("KeyRow"));
    if (!IsValid(Root)) return;
    WidgetTree->RootWidget = Root;
    _NameText = ck_game_settings_gym_widgets::AddText(
        WidgetTree, Root, TEXT("MappingName"), TEXT("Binding"));
    _KeyButton = ck_game_settings_gym_widgets::Add<UCk_GameSettingsGym_Button>(
        WidgetTree, Root, TEXT("RebindButton"));
    _KeyText = ck_game_settings_gym_widgets::AddText(
        WidgetTree, Root, TEXT("CurrentKey"), TEXT("Unbound"));
    _ResetButton = ck_game_settings_gym_widgets::Add<UCk_GameSettingsGym_Button>(
        WidgetTree, Root, TEXT("ResetButton"));
    if (auto* Button = Cast<UCk_GameSettingsGym_Button>(_KeyButton.Get()))
        Button->Set_Label(FText::FromString(TEXT("Change")));
    if (auto* Button = Cast<UCk_GameSettingsGym_Button>(_ResetButton.Get()))
        Button->Set_Label(FText::FromString(TEXT("Reset")));
    if (!Get_IsLayoutReady()) SetIsEnabled(false);
}

bool UCk_GameSettingsGym_KeyRow::Get_IsLayoutReady() const
{
    if (!IsValid(WidgetTree)) return false;
    auto* Root = Cast<UHorizontalBox>(WidgetTree->RootWidget);
    return IsValid(Root) && IsValid(_NameText) &&
        _NameText->GetParent() == Root && IsValid(_KeyButton) &&
        _KeyButton->GetParent() == Root && IsValid(_KeyText) &&
        _KeyText->GetParent() == Root && IsValid(_ResetButton) &&
        _ResetButton->GetParent() == Root;
}

void UCk_GameSettingsGym_KeyPage::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if (!IsValid(WidgetTree)) return;
    auto* Root = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("KeyPage"));
    if (!IsValid(Root)) return;
    WidgetTree->RootWidget = Root;
    _StatusText = ck_game_settings_gym_widgets::AddText(
        WidgetTree, Root, TEXT("Status"), TEXT(""));
    _RowContainer = ck_game_settings_gym_widgets::Add<UVerticalBox>(
        WidgetTree, Root, TEXT("KeyRows"));
    _ResetAllButton = ck_game_settings_gym_widgets::Add<UCk_GameSettingsGym_Button>(
        WidgetTree, Root, TEXT("ResetAllButton"));
    if (auto* Button = Cast<UCk_GameSettingsGym_Button>(_ResetAllButton.Get()))
        Button->Set_Label(FText::FromString(TEXT("Reset all")));
    _CaptureOverlay = ck_game_settings_gym_widgets::Add<UVerticalBox>(
        WidgetTree, Root, TEXT("CaptureOverlay"));
    _CapturePromptText = ck_game_settings_gym_widgets::AddText(
        WidgetTree, Cast<UPanelWidget>(_CaptureOverlay.Get()),
        TEXT("CapturePrompt"), TEXT("Press a key"));
    _ConflictOverlay = ck_game_settings_gym_widgets::Add<UVerticalBox>(
        WidgetTree, Root, TEXT("ConflictOverlay"));
    auto* ConflictPanel = Cast<UPanelWidget>(_ConflictOverlay.Get());
    _ConflictText = ck_game_settings_gym_widgets::AddText(
        WidgetTree, ConflictPanel, TEXT("ConflictMessage"), TEXT("Binding conflict"));
    _SwapButton = ck_game_settings_gym_widgets::Add<UCk_GameSettingsGym_Button>(
        WidgetTree, ConflictPanel, TEXT("SwapButton"));
    _OverwriteButton = ck_game_settings_gym_widgets::Add<UCk_GameSettingsGym_Button>(
        WidgetTree, ConflictPanel, TEXT("OverwriteButton"));
    _ConflictCancelButton = ck_game_settings_gym_widgets::Add<UCk_GameSettingsGym_Button>(
        WidgetTree, ConflictPanel, TEXT("CancelButton"));
    if (auto* B = Cast<UCk_GameSettingsGym_Button>(_SwapButton.Get()))
        B->Set_Label(FText::FromString(TEXT("Swap")));
    if (auto* B = Cast<UCk_GameSettingsGym_Button>(_OverwriteButton.Get()))
        B->Set_Label(FText::FromString(TEXT("Overwrite")));
    if (auto* B = Cast<UCk_GameSettingsGym_Button>(_ConflictCancelButton.Get()))
        B->Set_Label(FText::FromString(TEXT("Cancel")));
    _RowWidgetClass = UCk_GameSettingsGym_KeyRow::StaticClass();
    LayoutReady = true;
    LayoutReady = Get_IsLayoutReady();
    if (!LayoutReady) SetIsEnabled(false);
}

bool UCk_GameSettingsGym_KeyPage::Get_IsLayoutReady() const
{
    if (!LayoutReady || !IsValid(WidgetTree) ||
        _RowWidgetClass != UCk_GameSettingsGym_KeyRow::StaticClass()) return false;
    auto* Root = Cast<UVerticalBox>(WidgetTree->RootWidget);
    auto* Capture = Cast<UVerticalBox>(_CaptureOverlay.Get());
    auto* Conflict = Cast<UVerticalBox>(_ConflictOverlay.Get());
    return IsValid(Root) && IsValid(_RowContainer) &&
        _RowContainer->GetParent() == Root &&
        IsValid(_StatusText) && _StatusText->GetParent() == Root &&
        IsValid(_ResetAllButton) && _ResetAllButton->GetParent() == Root &&
        IsValid(Capture) && Capture->GetParent() == Root &&
        IsValid(_CapturePromptText) && _CapturePromptText->GetParent() == Capture &&
        IsValid(Conflict) && Conflict->GetParent() == Root &&
        IsValid(_ConflictText) && _ConflictText->GetParent() == Conflict &&
        IsValid(_SwapButton) && _SwapButton->GetParent() == Conflict &&
        IsValid(_OverwriteButton) && _OverwriteButton->GetParent() == Conflict &&
        IsValid(_ConflictCancelButton) && _ConflictCancelButton->GetParent() == Conflict;
}

bool UCk_GameSettingsGym_KeyPage::Get_HasCompleteMountedRows() const
{
    if (!Get_IsLayoutReady() ||
        _RowContainer->GetChildrenCount() != Get_GeneratedRowCount()) return false;
    for (int32 I = 0; I < _RowContainer->GetChildrenCount(); ++I)
    {
        const auto* Row = Cast<UCk_GameSettingsGym_KeyRow>(_RowContainer->GetChildAt(I));
        if (!IsValid(Row) || !Row->Get_IsLayoutReady() ||
            Row->GetOwningPlayer() != GetOwningPlayer()) return false;
    }
    return true;
}

void UCk_GameSettingsGym_KeyPage::NativeConstruct()
{
    Super::NativeConstruct();
    if (Get_IsLayoutReady() && Get_GeneratedRowCount() == 0 && IsValid(_StatusText))
        _StatusText->SetText(FText::FromString(
            TEXT("No remappable controls are registered for this player.")));
}

TSoftClassPtr<UCk_GameSettingsUI_RowWidgetBase>
UCk_GameSettingsGym_Widgets::Get_SettingRowClass()
{
    return TSoftClassPtr<UCk_GameSettingsUI_RowWidgetBase>(
        UCk_GameSettingsGym_SettingRow::StaticClass());
}
