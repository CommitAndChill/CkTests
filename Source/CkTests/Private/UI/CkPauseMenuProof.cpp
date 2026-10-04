#include "CkTests/UI/CkPauseMenuProof.h"
#include "UI/CkInventoryMenuProofModel.h"

#include "CkSlateLayout/SCkUiSurface.h"
#include "CkSlateLayout/CkUiCheckbox.h"
#include "CkSlateLayout/CkUiCollection.h"
#include "CkSlateLayout/CkUiSelect.h"
#include "CkSlateLayout/CkUiSlider.h"
#include "CkSlateLayout/CkUiActionCard.h"
#include "CkSlateLayout/SCkUiRepeat.h"
#include "CkUI/Layout/CkUI_LayoutConfigAsset.h"
#include "CkUI/Layout/CkUI_Layout_Subsystem.h"
#include "CkUI/Layout/CkUI_LayerStack.h"
#include "CkUI/Layout/CkUI_PrimaryGameLayout.h"
#include "CkWidgets/Authoring/CkAuthoredSurface_Widget.h"

#include <Blueprint/WidgetTree.h>
#include <Components/Border.h>
#include <Components/Image.h>
#include <Components/Overlay.h>
#include <Components/OverlaySlot.h>
#include <Components/SafeZone.h>
#include <Components/ScaleBox.h>
#include <Components/SizeBox.h>
#include <Engine/LocalPlayer.h>
#include <Engine/Texture2D.h>
#include <Engine/World.h>
#include <Framework/Application/IInputProcessor.h>
#include <Framework/Application/SlateApplication.h>
#include <Framework/Application/SlateUser.h>
#include <GameFramework/PlayerController.h>
#include <HAL/IConsoleManager.h>
#include <ImageUtils.h>
#include <Interfaces/IPluginManager.h>
#include <Misc/Paths.h>
#include <Misc/FileHelper.h>
#include <Misc/ScopeExit.h>
#include <NativeGameplayTags.h>
#include <Styling/CoreStyle.h>
#include <Types/SlateAttributeMetaData.h>
#include <Widgets/Input/SButton.h>
#include <Widgets/Layout/SScrollBox.h>
#include <Layout/Children.h>
#include <Layout/WidgetPath.h>
#include <Widgets/SNullWidget.h>

#define LOCTEXT_NAMESPACE "CkTestsPauseMenu"

namespace ck_pause_menu_proof
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(LayerTag, "CkTests.UI.Layer.PauseMenuProof");

    const FName Actions[] = {TEXT("ResumeRequested"), TEXT("SettingsRequested"), TEXT("ReturnToTitleRequested")};
    const FString ButtonIds[] = {TEXT("resume-button"), TEXT("settings-button"), TEXT("return-button")};
    const FString EnabledBindings[] = {TEXT("ResumeEnabled"), TEXT("SettingsEnabled"), TEXT("ReturnEnabled")};
    const FString ButtonLabelBindings[] = {TEXT("PauseResume"), TEXT("PauseSettings"), TEXT("PauseReturnToTitle")};
    const FName SettingsActions[] = {TEXT("CancelSettingsRequested"), TEXT("ApplySettingsRequested")};
    const FString SettingsButtonIds[] = {TEXT("settings-cancel"), TEXT("settings-apply")};
    const FString SettingsEnabledBindings[] = {TEXT("SettingsCancelEnabled"), TEXT("SettingsDirty")};
    const FString SettingsButtonLabels[] = {TEXT("SettingsCancel"), TEXT("SettingsApply")};
    const FName SettingsControlIds[] = {TEXT("settings-volume"), TEXT("settings-subtitles"),
        TEXT("settings-display-mode"), TEXT("settings-cancel"), TEXT("settings-apply")};

    auto ValidScreen(ECk_UiAuthoringProof_Screen InScreen) -> bool
    {
        return InScreen == ECk_UiAuthoringProof_Screen::Pause
            || InScreen == ECk_UiAuthoringProof_Screen::Settings
            || InScreen == ECk_UiAuthoringProof_Screen::Inventory;
    }

    auto ValidDisplayMode(const FString& InMode) -> bool
    { return InMode == TEXT("Windowed") || InMode == TEXT("Borderless") || InMode == TEXT("Fullscreen"); }

    auto ResourcePrefix(ECk_UiAuthoringProof_Screen InScreen) -> FString
    {
        if (InScreen == ECk_UiAuthoringProof_Screen::Inventory)
        { return TEXT("Resources/UiAuthoring/InventoryMenuProof/InventoryMenuProof.ui"); }
        return InScreen == ECk_UiAuthoringProof_Screen::Settings
            ? TEXT("Resources/UiAuthoring/SettingsMenuProof/SettingsMenuProof.ui")
            : TEXT("Resources/UiAuthoring/PauseMenuProof/PauseMenuProof.ui");
    }

    auto FindWidget(const TSharedRef<SWidget>& InRoot, FName InId) -> TSharedPtr<SWidget>
    {
        if (InRoot->GetTag() == InId && InRoot->SupportsKeyboardFocus())
        { return InRoot; }
        const auto* Children = InRoot->GetChildren();
        for (auto Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const auto Found = FindWidget(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InId);
            if (Found.IsValid())
            { return Found; }
        }
        return nullptr;
    }

    auto FillSlot(UOverlaySlot* InSlot) -> void
    {
        InSlot->SetHorizontalAlignment(HAlign_Fill);
        InSlot->SetVerticalAlignment(VAlign_Fill);
    }

    auto FindNode(const FCkUiNode& InNode, const FString& InId) -> const FCkUiNode*
    {
        if (InNode.Id == InId)
        { return &InNode; }
        for (const auto& Child : InNode.Children)
        {
            if (const auto* Found = FindNode(Child, InId))
            { return Found; }
        }
        return nullptr;
    }

    FAutoConsoleCommandWithWorld OpenCommand{
        TEXT("ck.UI.PauseMenuProof"), TEXT("Open the source-authored pause menu for the first local player."),
        FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* InWorld)
        {
            if (IsValid(InWorld))
            { UCk_PauseMenuProof_UE::Open(InWorld->GetFirstPlayerController()); }
        })};
}

// --------------------------------------------------------------------------------------------------------------------

/** Absorbs releases belonging to gestures that began while the menu owned input. */
class FCkPauseMenuInputReleaseGuard final : public IInputProcessor, public TSharedFromThis<FCkPauseMenuInputReleaseGuard>
{
public:
    FCkPauseMenuInputReleaseGuard(int32 InUserIndex, UWorld* InWorld)
        : _UserIndex(InUserIndex), _World(InWorld)
    {}

    auto BeginDrain() -> void
    { _Draining = true; }

    auto Tick(const float InDeltaTime, FSlateApplication& InSlate, TSharedRef<ICursor> InCursor) -> void override
    {
        if (_Draining && (_HeldKeys.IsEmpty() || NOT _World.IsValid() || NOT InSlate.IsActive()))
        { InSlate.UnregisterInputPreProcessor(AsShared()); }
    }

    auto HandleKeyDownEvent(FSlateApplication& InSlate, const FKeyEvent& InEvent) -> bool override
    {
        if (InEvent.GetUserIndex() != _UserIndex)
        { return false; }
        if (_Draining)
        {
            if (InEvent.IsRepeat())
            { return _HeldKeys.Contains(InEvent.GetKey()); }
            _HeldKeys.Remove(InEvent.GetKey());
        }
        else
        { _HeldKeys.Add(InEvent.GetKey()); }
        return false;
    }

    auto HandleKeyUpEvent(FSlateApplication& InSlate, const FKeyEvent& InEvent) -> bool override
    { return DoRelease(InEvent.GetUserIndex(), InEvent.GetKey()); }

    auto HandleMouseButtonDownEvent(FSlateApplication& InSlate, const FPointerEvent& InEvent) -> bool override
    {
        if (InEvent.GetUserIndex() == _UserIndex)
        {
            if (_Draining)
            { _HeldKeys.Remove(InEvent.GetEffectingButton()); }
            else
            { _HeldKeys.Add(InEvent.GetEffectingButton()); }
        }
        return false;
    }

    auto HandleMouseButtonUpEvent(FSlateApplication& InSlate, const FPointerEvent& InEvent) -> bool override
    { return DoRelease(InEvent.GetUserIndex(), InEvent.GetEffectingButton()); }

private:
    auto DoRelease(int32 InUserIndex, FKey InKey) -> bool
    {
        if (InUserIndex != _UserIndex)
        { return false; }
        const auto WasHeld = _HeldKeys.Remove(InKey) > 0;
        return _Draining && WasHeld;
    }

    int32 _UserIndex;
    TWeakObjectPtr<UWorld> _World;
    TSet<FKey> _HeldKeys;
    bool _Draining = false;
};

// --------------------------------------------------------------------------------------------------------------------

UCk_PauseMenuProof_UE::UCk_PauseMenuProof_UE(const FObjectInitializer& InObjectInitializer)
    : Super(InObjectInitializer)
{
    SetIsFocusable(true);
    _KeepSlateAliveWhenPooled = false;
    _Labels = {
        {TEXT("PauseTitle"), LOCTEXT("Title", "PAUSED")},
        {TEXT("PauseSubtitle"), LOCTEXT("Subtitle", "The game is paused.")},
        {TEXT("PauseResume"), LOCTEXT("Resume", "Resume")},
        {TEXT("PauseSettings"), LOCTEXT("Settings", "Settings")},
        {TEXT("PauseReturnToTitle"), LOCTEXT("ReturnToTitle", "Return to title")},
        {TEXT("InputResumeKey"), LOCTEXT("ResumeKey", "Esc")},
        {TEXT("InputResumeHint"), LOCTEXT("ResumeHint", "Resume")},
        {TEXT("SettingsTitle"), LOCTEXT("SettingsTitle", "SETTINGS")},
        {TEXT("SettingsSubtitle"), LOCTEXT("SettingsSubtitle", "Make it feel right.")},
        {TEXT("VolumeLabel"), LOCTEXT("VolumeLabel", "Master volume")},
        {TEXT("SubtitlesLabel"), LOCTEXT("SubtitlesLabel", "Subtitles")},
        {TEXT("SubtitlesHelper"), LOCTEXT("SubtitlesHelper", "Show dialogue captions.")},
        {TEXT("DisplayModeLabel"), LOCTEXT("DisplayModeLabel", "Display mode")},
        {TEXT("SettingsCancel"), LOCTEXT("SettingsCancel", "Cancel")},
        {TEXT("SettingsApply"), LOCTEXT("SettingsApply", "Apply")},
        {TEXT("InputSettingsKey"), LOCTEXT("SettingsKey", "Esc")},
        {TEXT("InputSettingsHint"), LOCTEXT("SettingsHint", "Cancel")}
    };
    for (const auto Action : ck_pause_menu_proof::Actions)
    { _ActionEnabled.Add(Action, true); }
}

auto
    UCk_PauseMenuProof_UE::Open(APlayerController* InPlayerController)
    -> UCk_PauseMenuProof_UE*
{
    return OpenWithSettings(InPlayerController, ECk_UiAuthoringProof_Screen::Pause,
        FCk_UiAuthoringProof_Settings{});
}

auto
    UCk_PauseMenuProof_UE::OpenWithSettings(APlayerController* InPlayerController,
        ECk_UiAuthoringProof_Screen InScreen, const FCk_UiAuthoringProof_Settings& InSettings)
    -> UCk_PauseMenuProof_UE*
{
    return OpenWithProofState(InPlayerController, InScreen, InSettings, FCk_UiAuthoringProof_InventoryState{});
}

auto
    UCk_PauseMenuProof_UE::OpenWithProofState(APlayerController* InPlayerController,
        ECk_UiAuthoringProof_Screen InScreen, const FCk_UiAuthoringProof_Settings& InSettings,
        const FCk_UiAuthoringProof_InventoryState& InInventory)
    -> UCk_PauseMenuProof_UE*
{
    const auto ValidSettings = ck_pause_menu_proof::ValidScreen(InScreen)
        && FMath::IsFinite(InSettings.Get_Volume()) && InSettings.Get_Volume() >= 0.0f
        && InSettings.Get_Volume() <= 100.0f && ck_pause_menu_proof::ValidDisplayMode(InSettings.Get_DisplayMode());
    CK_ENSURE_IF_NOT(ValidSettings, TEXT("UI authoring proof requires a supported screen and valid settings")) {}
    if (NOT ValidSettings)
    { return nullptr; }
    auto InventoryError = FString{};
    const auto ValidInventory = FCkInventoryMenuProofModel::TryValidateState(InInventory, InventoryError);
    CK_ENSURE_IF_NOT(ValidInventory, TEXT("UI authoring proof requires a valid inventory snapshot: {}"), InventoryError) {}
    if (NOT ValidInventory)
    { return nullptr; }
    const auto ValidController = IsValid(InPlayerController) && InPlayerController->IsLocalController();
    CK_ENSURE_IF_NOT(ValidController, TEXT("Pause menu requires a valid local PlayerController")) {}
    if (NOT ValidController)
    { return nullptr; }

    auto* LocalPlayer = InPlayerController->GetLocalPlayer();
    auto* World = InPlayerController->GetWorld();
    const auto ValidContext = IsValid(LocalPlayer) && IsValid(World) && FSlateApplication::IsInitialized();
    CK_ENSURE_IF_NOT(ValidContext, TEXT("Pause menu requires a local player, world, and Slate application")) {}
    if (NOT ValidContext)
    { return nullptr; }

    auto* Subsystem = LocalPlayer->GetSubsystem<UCk_UI_Layout_Subsystem_UE>();
    const auto HasSubsystem = IsValid(Subsystem);
    CK_ENSURE_IF_NOT(HasSubsystem, TEXT("Pause menu requires the CkUI layout subsystem")) {}
    if (NOT HasSubsystem)
    { return nullptr; }

    const auto HadLayout = Subsystem->Has_Layout();
    const auto CompatibleLayout = NOT HadLayout || Subsystem->HasLayer(ck_pause_menu_proof::LayerTag);
    CK_ENSURE_IF_NOT(CompatibleLayout, TEXT("Pause proof cannot replace an unrelated UI layout")) {}
    if (NOT CompatibleLayout)
    { return nullptr; }
    if (HadLayout)
    {
        if (auto* Active = Subsystem->Get_Layer(ck_pause_menu_proof::LayerTag)->GetActiveWidget())
        {
            auto* Existing = Cast<UCk_PauseMenuProof_UE>(Active);
            return IsValid(Existing) && Existing->Request_ShowScreen(InScreen) ? Existing : nullptr;
        }
    }

    auto* Config = LoadObject<UCk_UI_LayoutConfigAsset_UE>(nullptr,
        TEXT("/Script/AngelscriptAssets.CkTests_PauseMenuProof_Layout"));
    const auto ValidConfig = IsValid(Config) && Config->Get_LayerConfig(ck_pause_menu_proof::LayerTag).IsSet();
    CK_ENSURE_IF_NOT(ValidConfig, TEXT("Source-authored pause layout configuration is unavailable")) {}
    if (NOT ValidConfig)
    { return nullptr; }

    auto* Menu = CreateWidget<UCk_PauseMenuProof_UE>(InPlayerController);
    if (IsValid(Menu))
    {
        Menu->_AppliedSettings = InSettings;
        Menu->_DraftSettings = InSettings;
        Menu->_Screen = InScreen;
        Menu->_InventoryInitialState = InInventory;
    }
    const auto Prepared = IsValid(Menu) && Menu->DoPrepare();
    CK_ENSURE_IF_NOT(Prepared, TEXT("Pause menu source or background could not be prepared")) {}
    if (NOT Prepared)
    { return nullptr; }

    const auto PreparedSlateRoot = Menu->TakeWidget();
    const auto SurfaceReady = IsValid(Menu->_Surface) && Menu->_Surface->HasConfiguredView()
        && Menu->_Surface->GetCachedWidget().IsValid();
    CK_ENSURE_IF_NOT(SurfaceReady, TEXT("Pause menu requires a mounted authored surface before acquiring pause")) {}
    if (NOT SurfaceReady)
    {
        Menu->DoRelease();
        return nullptr;
    }

    if (NOT HadLayout)
    { Subsystem->CreateLayout(Config); }
    const auto LayoutReady = Subsystem->HasLayer(ck_pause_menu_proof::LayerTag);
    CK_ENSURE_IF_NOT(LayoutReady, TEXT("Pause menu layer was not created")) {}
    if (NOT LayoutReady)
    {
        Menu->DoRelease();
        if (NOT HadLayout)
        { Subsystem->DestroyLayout(); }
        return nullptr;
    }

    if (NOT World->IsPaused())
    {
        constexpr auto Pause = true;
        Menu->_OwnsPause = InPlayerController->SetPause(Pause);
        const auto PauseAcquired = Menu->_OwnsPause;
        CK_ENSURE_IF_NOT(PauseAcquired, TEXT("Pause menu could not acquire the game's pause state")) {}
        if (NOT PauseAcquired)
        {
            Menu->DoRelease();
            if (NOT HadLayout)
            { Subsystem->DestroyLayout(); }
            return nullptr;
        }
    }

    auto* Pushed = Subsystem->PushWidgetToLayer_Instance(ck_pause_menu_proof::LayerTag, Menu);
    const auto* AcceptedLayer = Subsystem->Get_Layer(ck_pause_menu_proof::LayerTag);
    const auto Accepted = Pushed == Menu
        && IsValid(AcceptedLayer) && AcceptedLayer->GetActiveWidget() == Menu
        && IsValid(Menu->_Surface) && Menu->_Surface->HasConfiguredView();
    CK_ENSURE_IF_NOT(Accepted, TEXT("Pause menu was rejected by the UI layer")) {}
    if (NOT Accepted)
    {
        Menu->DoRelease();
        Subsystem->RemoveWidget(Menu);
        if (NOT HadLayout)
        { Subsystem->DestroyLayout(); }
        return nullptr;
    }
    Menu->ActivateWidget();
    return Menu;
}

auto
    UCk_PauseMenuProof_UE::DoPrepare()
    -> bool
{
    const auto* Player = GetOwningLocalPlayer();
    const TSharedPtr<const FSlateUser> User = IsValid(Player) ? Player->GetSlateUser() : nullptr;
    if (NOT User.IsValid())
    { return false; }
    _SlateUserIndex = User->GetUserIndex();
    _PreviousFocus = FSlateApplication::Get().GetUserFocusedWidget(_SlateUserIndex);

    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
    if (NOT Plugin.IsValid())
    {
        _SourceError = TEXT("The CkTests plugin resource directory is unavailable.");
        return false;
    }
    _Backdrop = FImageUtils::ImportFileAsTexture2D(FPaths::Combine(Plugin->GetBaseDir(),
        TEXT("Resources/UiAuthoring/PauseMenuProof/pause-menu-backdrop.png")));
    if (NOT IsValid(_Backdrop))
    { return false; }

    auto Registry = FCkUiWidgetRegistry{};
    const auto Slider = FCkUiSlider::Register(Registry);
    if (NOT Slider.Succeeded)
    { _SourceError = FString::Join(Slider.Errors, TEXT("\n")); return false; }
    const auto Checkbox = FCkUiCheckbox::Register(Registry);
    if (NOT Checkbox.Succeeded)
    { _SourceError = FString::Join(Checkbox.Errors, TEXT("\n")); return false; }
    const auto Select = FCkUiSelect::Register(Registry);
    if (NOT Select.Succeeded)
    { _SourceError = FString::Join(Select.Errors, TEXT("\n")); return false; }
    const auto ActionCard = FCkUiActionCard::Register(Registry);
    if (NOT ActionCard.Succeeded)
    { _SourceError = FString::Join(ActionCard.Errors, TEXT("\n")); return false; }
    _ControlRegistry = Registry.CreateSnapshot();
    const auto InventoryReady = FCkInventoryMenuProofModel::TryCreate(_InventoryInitialState,
        FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/UiAuthoring/InventoryMenuProof/inventory-item-atlas.png")),
        _InventoryModel, _SourceError);
    if (NOT InventoryReady)
    { return false; }
    const auto Collection = FCkUiCollection::TryCreate({{TEXT("label"), ECkUiFieldKind::Text}}, _DisplayModes);
    if (NOT Collection.Succeeded)
    { _SourceError = FString::Join(Collection.Errors, TEXT("\n")); return false; }
    auto Options = TArray<FCkUiRecordData>{};
    const FText ModeLabels[] = {LOCTEXT("Windowed", "Windowed"), LOCTEXT("Borderless", "Borderless"),
        LOCTEXT("Fullscreen", "Fullscreen")};
    const FString ModeKeys[] = {TEXT("Windowed"), TEXT("Borderless"), TEXT("Fullscreen")};
    for (auto Index = 0; Index < UE_ARRAY_COUNT(ModeKeys); ++Index)
    {
        auto Record = FCkUiRecordData{};
        Record.Key = ModeKeys[Index];
        auto Label = FCkUiFieldValue{};
        Label.Kind = ECkUiFieldKind::Text;
        Label.Text = ModeLabels[Index];
        Record.Fields.Add(TEXT("label"), MoveTemp(Label));
        Options.Add(MoveTemp(Record));
    }
    const auto Published = _DisplayModes->TrySetRecords(MoveTemp(Options));
    if (NOT Published.Succeeded)
    { _SourceError = FString::Join(Published.Errors, TEXT("\n")); return false; }

    _View = DoCreateView(_Screen);
    const auto Loaded = _View.IsValid() && DoReloadFiles();
    if (NOT Loaded)
    {
        CK_ENSURE_IF_NOT(Loaded, TEXT("UI authoring layout rejected: {}"), _SourceError) {}
        DoRelease();
        return false;
    }
    return true;
}

auto
    UCk_PauseMenuProof_UE::DoCreateView(ECk_UiAuthoringProof_Screen InScreen)
    -> TSharedPtr<FCkUiView>
{
    if (NOT _ControlRegistry.IsValid() || NOT _DisplayModes.IsValid() || NOT _InventoryModel.IsValid())
    { return nullptr; }
    auto Data = FCkUiView::FDataBindings{};
    auto Actions = FCkUiView::FActions{};
    const auto WeakOwner = TWeakObjectPtr<UCk_PauseMenuProof_UE>{this};
    Data.SlateUserIndex = _SlateUserIndex;
    Data.RestoreButtonFocusOnReload = true;
    Data.CanDispatchEvents = TAttribute<bool>::CreateLambda([WeakOwner]()
    { return WeakOwner.IsValid() && WeakOwner->IsActivated() && NOT WeakOwner->_SwitchingScreen; });
    for (const auto& [Binding, Text] : _Labels)
    {
        Data.Text.Add(Binding.ToString(), TAttribute<FText>::CreateLambda([WeakOwner, Binding]()
        { return WeakOwner.IsValid() ? WeakOwner->_Labels.FindRef(Binding) : FText::GetEmpty(); }));
    }
    if (InScreen == ECk_UiAuthoringProof_Screen::Pause)
    {
        for (auto Index = 0; Index < UE_ARRAY_COUNT(ck_pause_menu_proof::Actions); ++Index)
        {
            const auto Action = ck_pause_menu_proof::Actions[Index];
            Data.Visibility.Add(ck_pause_menu_proof::EnabledBindings[Index], TAttribute<bool>::CreateLambda([WeakOwner, Action]()
            { return WeakOwner.IsValid() && WeakOwner->_ActionEnabled.FindRef(Action); }));
            Actions.Add(Action.ToString(), FSimpleDelegate::CreateWeakLambda(this, [this, Action]()
            { DoAction(Action); }));
        }
    }
    else if (InScreen == ECk_UiAuthoringProof_Screen::Inventory)
    {
        Data.CanDispatchEvents = TAttribute<bool>::CreateLambda([WeakOwner]()
        {
            return WeakOwner.IsValid() && WeakOwner->IsActivated() && NOT WeakOwner->_SwitchingScreen
                && WeakOwner->_Screen == ECk_UiAuthoringProof_Screen::Inventory;
        });
        _InventoryModel->PopulateBindings(Data, Actions, FSimpleDelegate::CreateWeakLambda(this,
            [this]() { Request_Close(); }));
    }
    else
    {
        Data.Number.Add(TEXT("SettingsVolume"), TAttribute<float>::CreateLambda([WeakOwner]()
        { return WeakOwner.IsValid() ? WeakOwner->_DraftSettings.Get_Volume() : 0.0f; }));
        Data.Visibility.Add(TEXT("SettingsSubtitles"), TAttribute<bool>::CreateLambda([WeakOwner]()
        { return WeakOwner.IsValid() && WeakOwner->_DraftSettings.Get_Subtitles(); }));
        Data.String.Add(TEXT("SettingsDisplayMode"), TAttribute<FString>::CreateLambda([WeakOwner]()
        { return WeakOwner.IsValid() ? WeakOwner->_DraftSettings.Get_DisplayMode() : FString{}; }));
        Data.Collections.Add(TEXT("DisplayModes"), _DisplayModes);
        Data.Visibility.Add(TEXT("SettingsDirty"), TAttribute<bool>::CreateLambda([WeakOwner]()
        { return WeakOwner.IsValid() && WeakOwner->Get_IsSettingsDirty(); }));
        Data.Visibility.Add(TEXT("SettingsCancelEnabled"), true);
        Data.Text.Add(TEXT("VolumeValue"), TAttribute<FText>::CreateLambda([WeakOwner]()
        {
            if (NOT WeakOwner.IsValid())
            { return FText::GetEmpty(); }
            const auto Value = WeakOwner->_VolumePreview.Get(WeakOwner->_DraftSettings.Get_Volume());
            return FText::Format(LOCTEXT("VolumeValue", "{0}%"), FText::AsNumber(FMath::RoundToInt(Value)));
        }));
        Data.Text.Add(TEXT("DirtyStatus"), TAttribute<FText>::CreateLambda([WeakOwner]()
        { return WeakOwner.IsValid() && WeakOwner->Get_IsSettingsDirty()
            ? LOCTEXT("SettingsDirty", "Unsaved changes") : LOCTEXT("SettingsClean", "No changes"); }));
        Data.NumberChanged.Add(TEXT("PreviewVolume"), FCkUiOnNumberChanged::CreateWeakLambda(this,
            [this](float InValue)
            {
                if (_Screen == ECk_UiAuthoringProof_Screen::Settings && IsActivated()
                    && FMath::IsFinite(InValue) && InValue >= 0.0f && InValue <= 100.0f)
                { _VolumePreview = InValue; }
            }));
        Data.NumberInteraction.Add(TEXT("EditVolume"), FCkUiOnNumberInteraction::CreateWeakLambda(this,
            [this](const FCkUiNumberInteraction& InInteraction)
            {
                if (_Screen != ECk_UiAuthoringProof_Screen::Settings || NOT IsActivated())
                { return; }
                if (InInteraction.Phase == ECkUiInteractionPhase::Commit && FMath::IsFinite(InInteraction.Value)
                    && InInteraction.Value >= 0.0f && InInteraction.Value <= 100.0f)
                { _DraftSettings.Set_Volume(InInteraction.Value); }
                if (InInteraction.Phase != ECkUiInteractionPhase::Begin)
                { _VolumePreview.Reset(); }
            }));
        Data.BoolChanged.Add(TEXT("EditSubtitles"), FCkUiOnBoolChanged::CreateWeakLambda(this,
            [this](bool InValue)
            {
                if (_Screen == ECk_UiAuthoringProof_Screen::Settings && IsActivated())
                { _DraftSettings.Set_Subtitles(InValue); }
            }));
        Data.StringChanged.Add(TEXT("EditDisplayMode"), FCkUiOnStringChanged::CreateWeakLambda(this,
            [this](const FString& InValue)
            {
                if (_Screen == ECk_UiAuthoringProof_Screen::Settings && IsActivated()
                    && ck_pause_menu_proof::ValidDisplayMode(InValue))
                { _DraftSettings.Set_DisplayMode(InValue); }
            }));
        for (const auto Action : ck_pause_menu_proof::SettingsActions)
        {
            Actions.Add(Action.ToString(), FSimpleDelegate::CreateWeakLambda(this, [this, Action]()
            { DoAction(Action); }));
        }
    }
    auto View = FCkUiView::Create({}, MoveTemp(Actions), {},
        FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 18), MoveTemp(Data), _ControlRegistry);
    View->GetRegion(TEXT("main"));
    return View;
}

auto
    UCk_PauseMenuProof_UE::Request_ApplySource(const FString& InMarkup, const FString& InStylesheet)
    -> bool
{
    if (_SwitchingScreen || NOT _View.IsValid() || _View->IsOwnerReleased())
    {
        _SourceError = TEXT("No live authored view is configured.");
        return false;
    }
    return DoValidateSource(_View, _Screen, InMarkup, InStylesheet, _SourceError);
}

auto
    UCk_PauseMenuProof_UE::DoValidateSource(const TSharedPtr<FCkUiView>& InView,
        ECk_UiAuthoringProof_Screen InScreen, const FString& InMarkup, const FString& InStyle,
        FString& OutError) const
    -> bool
{
    auto Document = FCkUiDocument{};
    const auto Parsed = FCkUiDocumentParser::TryParse(InMarkup, InStyle, {}, Document,
        TEXT("UiAuthoringProof"), _ControlRegistry);
    if (NOT Parsed.Succeeded)
    {
        OutError = FString::Join(Parsed.Errors, TEXT("\n"));
        return false;
    }
    const auto* Main = Document.Regions.Find(TEXT("main"));
    if (Main == nullptr)
    {
        OutError = TEXT("The UI authoring screen requires region 'main'.");
        return false;
    }
    if (InScreen == ECk_UiAuthoringProof_Screen::Inventory)
    {
        const TMap<FString, FString> RequiredActions = {
            {TEXT("inventory-filter-all"), TEXT("InventoryFilterAll")},
            {TEXT("inventory-filter-equipment"), TEXT("InventoryFilterEquipment")},
            {TEXT("inventory-filter-supplies"), TEXT("InventoryFilterSupplies")},
            {TEXT("inventory-filter-materials"), TEXT("InventoryFilterMaterials")},
            {TEXT("inventory-primary-action"), TEXT("InventoryPrimaryAction")},
            {TEXT("inventory-close"), TEXT("InventoryClose")}
        };
        for (const auto& [Id, Action] : RequiredActions)
        {
            const auto* Node = ck_pause_menu_proof::FindNode(*Main, Id);
            if (Node == nullptr || Node->Kind != ECkUiNodeKind::Button || Node->Action != Action)
            {
                OutError = FString::Printf(TEXT("Inventory requires button '%s' with action '%s'."), *Id, *Action);
                return false;
            }
        }
        const auto* Primary = ck_pause_menu_proof::FindNode(*Main, TEXT("inventory-primary-action"));
        const auto* Search = ck_pause_menu_proof::FindNode(*Main, TEXT("inventory-search"));
        const auto* Items = ck_pause_menu_proof::FindNode(*Main, TEXT("inventory-items"));
        const auto* Card = ck_pause_menu_proof::FindNode(*Main, TEXT("inventory-card"));
        const auto* Scroll = ck_pause_menu_proof::FindNode(*Main, TEXT("inventory-scroll"));
        const auto* Selected = Card != nullptr ? Card->FieldBindings.Find(TEXT("selected")) : nullptr;
        const auto Matches = Primary->Binding == TEXT("InventoryPrimaryLabel")
            && Primary->ButtonEnabledBinding == TEXT("InventoryPrimaryEnabled")
            && Search != nullptr && Search->Kind == ECkUiNodeKind::Search && Search->Binding == TEXT("InventorySearch")
            && Items != nullptr && Items->Kind == ECkUiNodeKind::Repeat && Items->Binding == TEXT("InventoryItems")
            && Items->RepeatDirection == Orient_Horizontal
            && Card != nullptr && Card->Kind == ECkUiNodeKind::Custom && Card->CustomTag == TEXT("action-card")
            && Card->ItemAction == TEXT("SelectInventoryItem") && Selected != nullptr && *Selected == TEXT("selected")
            && Scroll != nullptr && Scroll->Kind == ECkUiNodeKind::Scroll && Scroll->ScrollDirection == Orient_Vertical;
        if (NOT Matches)
        {
            OutError = TEXT("Inventory requires its search, repeated action cards, scroll, and bound primary action.");
            return false;
        }
        const auto Loaded = InView->TryReload(InMarkup, InStyle, TEXT("UiAuthoringProof"));
        OutError = FString::Join(Loaded.Errors, TEXT("\n"));
        return Loaded.Succeeded;
    }
    const auto Settings = InScreen == ECk_UiAuthoringProof_Screen::Settings;
    const auto* ButtonIds = Settings ? ck_pause_menu_proof::SettingsButtonIds : ck_pause_menu_proof::ButtonIds;
    const auto* Actions = Settings ? ck_pause_menu_proof::SettingsActions : ck_pause_menu_proof::Actions;
    const auto* Labels = Settings ? ck_pause_menu_proof::SettingsButtonLabels : ck_pause_menu_proof::ButtonLabelBindings;
    const auto* Enabled = Settings ? ck_pause_menu_proof::SettingsEnabledBindings : ck_pause_menu_proof::EnabledBindings;
    const auto ButtonCount = Settings ? UE_ARRAY_COUNT(ck_pause_menu_proof::SettingsButtonIds)
        : UE_ARRAY_COUNT(ck_pause_menu_proof::ButtonIds);
    for (auto Index = 0; Index < ButtonCount; ++Index)
    {
        const auto* Button = ck_pause_menu_proof::FindNode(*Main, ButtonIds[Index]);
        const auto MatchesContract = Button != nullptr && Button->Kind == ECkUiNodeKind::Button
            && Button->Action == Actions[Index].ToString()
            && Button->Binding == Labels[Index] && Button->ButtonEnabledBinding == Enabled[Index];
        if (NOT MatchesContract)
        {
            OutError = FString::Printf(TEXT("The main region requires button '%s' with action '%s' and its label/enabled bindings."),
                *ButtonIds[Index], *Actions[Index].ToString());
            return false;
        }
    }
    const TMap<FString, FString> RequiredText = Settings ? TMap<FString, FString>{
        {TEXT("settings-title"), TEXT("SettingsTitle")}, {TEXT("settings-subtitle"), TEXT("SettingsSubtitle")},
        {TEXT("settings-volume-label"), TEXT("VolumeLabel")},
        {TEXT("settings-subtitles-label"), TEXT("SubtitlesLabel")},
        {TEXT("settings-subtitles-helper"), TEXT("SubtitlesHelper")},
        {TEXT("settings-display-label"), TEXT("DisplayModeLabel")},
        {TEXT("settings-volume-value"), TEXT("VolumeValue")}, {TEXT("settings-dirty-status"), TEXT("DirtyStatus")},
        {TEXT("settings-key"), TEXT("InputSettingsKey")}, {TEXT("settings-hint-label"), TEXT("InputSettingsHint")}
    } : TMap<FString, FString>{
        {TEXT("pause-title"), TEXT("PauseTitle")}, {TEXT("pause-subtitle"), TEXT("PauseSubtitle")},
        {TEXT("resume-key"), TEXT("InputResumeKey")}, {TEXT("resume-hint-label"), TEXT("InputResumeHint")}
    };
    for (const auto& [Id, Binding] : RequiredText)
    {
        const auto* Text = ck_pause_menu_proof::FindNode(*Main, Id);
        if (Text == nullptr || Text->Kind != ECkUiNodeKind::Text || Text->Binding != Binding)
        {
            OutError = FString::Printf(TEXT("The main region requires text '%s' with binding '%s'."), *Id, *Binding);
            return false;
        }
    }
    if (Settings)
    {
        const auto Matches = [](const FCkUiNode* InNode, const FString& InTag,
            const TMap<FString, FString>& InBindings) -> bool
        {
            if (InNode == nullptr || InNode->Kind != ECkUiNodeKind::Custom || InNode->CustomTag != InTag)
            { return false; }
            for (const auto& [Property, Binding] : InBindings)
            {
                const auto* Value = InNode->CustomProperties.Find(Property);
                if (Value == nullptr || Value->Name != Binding)
                { return false; }
            }
            return true;
        };
        const auto* Slider = ck_pause_menu_proof::FindNode(*Main, TEXT("settings-volume"));
        auto ValidSlider = Matches(Slider, TEXT("slider"), {{TEXT("value"), TEXT("SettingsVolume")},
            {TEXT("interaction"), TEXT("EditVolume")}, {TEXT("changed"), TEXT("PreviewVolume")}});
        if (ValidSlider)
        {
            const auto* Minimum = Slider->CustomProperties.Find(TEXT("min"));
            const auto* Maximum = Slider->CustomProperties.Find(TEXT("max"));
            const auto* Step = Slider->CustomProperties.Find(TEXT("step"));
            ValidSlider = Minimum != nullptr && Maximum != nullptr && Step != nullptr
                && Minimum->Number == 0.0f && Maximum->Number == 100.0f && Step->Number == 1.0f;
        }
        const auto ValidCheckbox = Matches(ck_pause_menu_proof::FindNode(*Main, TEXT("settings-subtitles")),
            TEXT("checkbox"), {{TEXT("value"), TEXT("SettingsSubtitles")}, {TEXT("changed"), TEXT("EditSubtitles")}});
        const auto ValidSelect = Matches(ck_pause_menu_proof::FindNode(*Main, TEXT("settings-display-mode")),
            TEXT("select"), {{TEXT("value"), TEXT("SettingsDisplayMode")}, {TEXT("options"), TEXT("DisplayModes")},
                {TEXT("changed"), TEXT("EditDisplayMode")}});
        if (NOT ValidSlider || NOT ValidCheckbox || NOT ValidSelect)
        {
            OutError = TEXT("Settings requires its volume, subtitles and display-mode controls with the model bindings and events.");
            return false;
        }
    }
    const auto Loaded = InView->TryReload(InMarkup, InStyle, TEXT("UiAuthoringProof"));
    OutError = FString::Join(Loaded.Errors, TEXT("\n"));
    return Loaded.Succeeded;
}

auto
    UCk_PauseMenuProof_UE::Get_SourceError() const
    -> FString
{ return _SourceError; }

auto
    UCk_PauseMenuProof_UE::Request_ShowScreen(ECk_UiAuthoringProof_Screen InScreen, bool InReturnToPause)
    -> bool
{
    if (_SwitchingScreen || NOT ck_pause_menu_proof::ValidScreen(InScreen) || NOT IsActivated()
        || NOT _View.IsValid() || _View->IsOwnerReleased() || NOT IsValid(_Surface))
    { return false; }
    if (_Screen == InScreen)
    { return true; }
    _SwitchingScreen = true;
    ON_SCOPE_EXIT { _SwitchingScreen = false; };
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
    if (NOT Plugin.IsValid())
    { return false; }
    const auto ResourceRoot = FPaths::Combine(Plugin->GetBaseDir(), ck_pause_menu_proof::ResourcePrefix(InScreen));
    auto Markup = FString{};
    auto Style = FString{};
    if (NOT FFileHelper::LoadFileToString(Markup, *(ResourceRoot + TEXT(".html")))
        || NOT FFileHelper::LoadFileToString(Style, *(ResourceRoot + TEXT(".css"))))
    {
        _SourceError = TEXT("Could not read both requested screen source files.");
        return false;
    }
    const auto Candidate = DoCreateView(InScreen);
    if (NOT Candidate.IsValid() || NOT DoValidateSource(Candidate, InScreen, Markup, Style, _SourceError))
    {
        if (Candidate.IsValid())
        { Candidate->ReleaseOwnerInteractions(); }
        return false;
    }
    if (NOT _Surface->TryConfigure(Candidate, TEXT("main")))
    {
        Candidate->ReleaseOwnerInteractions();
        _SourceError = TEXT("Could not mount the requested screen.");
        if (NOT _Surface->HasConfiguredView() || NOT _View.IsValid() || _View->IsOwnerReleased())
        { Request_Close(); }
        return false;
    }
    if (NOT IsActivated())
    {
        _Surface->ReleaseView();
        Candidate->ReleaseOwnerInteractions();
        return false;
    }
    _View = Candidate;
    _Screen = InScreen;
    _SettingsReturnToPause = InScreen == ECk_UiAuthoringProof_Screen::Settings && InReturnToPause;
    _DraftSettings = _AppliedSettings;
    _VolumePreview.Reset();
    _HasSourceAttempt = true;
    _LastAttemptMarkup = MoveTemp(Markup);
    _LastAttemptStyle = MoveTemp(Style);
    _SourceError.Reset();
    _NeedsInitialFocus = NOT DoFocus(0);
    return true;
}

auto
    UCk_PauseMenuProof_UE::Get_Screen() const
    -> ECk_UiAuthoringProof_Screen
{ return _Screen; }

auto
    UCk_PauseMenuProof_UE::Get_AppliedSettings() const
    -> FCk_UiAuthoringProof_Settings
{ return _AppliedSettings; }

auto
    UCk_PauseMenuProof_UE::Get_DraftSettings() const
    -> FCk_UiAuthoringProof_Settings
{ return _DraftSettings; }

auto
    UCk_PauseMenuProof_UE::Get_IsSettingsDirty() const
    -> bool
{ return NOT (_DraftSettings == _AppliedSettings); }

auto
    UCk_PauseMenuProof_UE::Get_SettingsSummary() const
    -> FString
{
    return FString::Printf(TEXT("Volume %d%%  Subtitles %s  %s"), FMath::RoundToInt(_AppliedSettings.Get_Volume()),
        _AppliedSettings.Get_Subtitles() ? TEXT("on") : TEXT("off"), *_AppliedSettings.Get_DisplayMode());
}

auto
    UCk_PauseMenuProof_UE::Get_InventoryState() const
    -> FCk_UiAuthoringProof_InventoryState
{ return _InventoryModel.IsValid() ? _InventoryModel->Get_State() : _InventoryInitialState; }

auto
    UCk_PauseMenuProof_UE::Get_InventorySummary() const
    -> FString
{ return _InventoryModel.IsValid() ? _InventoryModel->Get_Summary() : FString{}; }

auto
    UCk_PauseMenuProof_UE::Get_InventoryModel() const
    -> TSharedPtr<FCkInventoryMenuProofModel>
{ return _InventoryModel; }

auto
    UCk_PauseMenuProof_UE::DoReloadFiles()
    -> bool
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
    if (NOT Plugin.IsValid())
    {
        _SourceError = TEXT("The CkTests plugin resource directory is unavailable.");
        _HasSourceAttempt = false;
        return false;
    }
    const auto ResourceRoot = FPaths::Combine(Plugin->GetBaseDir(),
        ck_pause_menu_proof::ResourcePrefix(_Screen));
    auto Markup = FString{};
    auto Style = FString{};
    if (NOT FFileHelper::LoadFileToString(Markup, *(ResourceRoot + TEXT(".html")))
        || NOT FFileHelper::LoadFileToString(Style, *(ResourceRoot + TEXT(".css"))))
    {
        _SourceError = TEXT("Could not read both UI authoring source files.");
        _HasSourceAttempt = false;
        return false;
    }
    if (_HasSourceAttempt && Markup == _LastAttemptMarkup && Style == _LastAttemptStyle)
    { return _SourceError.IsEmpty(); }
    _HasSourceAttempt = true;
    _LastAttemptMarkup = Markup;
    _LastAttemptStyle = Style;
    return Request_ApplySource(Markup, Style);
}

auto
    UCk_PauseMenuProof_UE::RebuildWidget()
    -> TSharedRef<SWidget>
{
    if (NOT _View.IsValid())
    { return SNullWidget::NullWidget; }
    if (NOT IsValid(WidgetTree))
    { WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree")); }

    if (NOT IsValid(WidgetTree->RootWidget))
    {
        auto* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
        WidgetTree->RootWidget = Root;
        auto* BackdropScale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("BackdropScale"));
        BackdropScale->SetStretch(EStretch::ScaleToFill);
        BackdropScale->SetClipping(EWidgetClipping::ClipToBounds);
        BackdropScale->SetVisibility(ESlateVisibility::HitTestInvisible);
        ck_pause_menu_proof::FillSlot(Root->AddChildToOverlay(BackdropScale));
        auto* Image = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PreviewBackdrop"));
        constexpr auto MatchTextureSize = true;
        Image->SetBrushFromTexture(_Backdrop, MatchTextureSize);
        BackdropScale->AddChild(Image);

        auto* Dimmer = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dimmer"));
        Dimmer->SetBrushColor(FLinearColor{0.002f, 0.003f, 0.005f, 0.28f});
        Dimmer->SetVisibility(ESlateVisibility::HitTestInvisible);
        ck_pause_menu_proof::FillSlot(Root->AddChildToOverlay(Dimmer));

        auto* SafeZone = WidgetTree->ConstructWidget<USafeZone>(USafeZone::StaticClass(), TEXT("SafeContent"));
        ck_pause_menu_proof::FillSlot(Root->AddChildToOverlay(SafeZone));
        auto* Scale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("ReferenceScale"));
        Scale->SetStretch(EStretch::ScaleToFit);
        SafeZone->AddChild(Scale);
        auto* Reference = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ReferenceViewport"));
        Reference->SetWidthOverride(1920.0f);
        Reference->SetHeightOverride(1080.0f);
        Scale->AddChild(Reference);
        _Surface = WidgetTree->ConstructWidget<UCk_UI_AuthoredSurface_UE>(UCk_UI_AuthoredSurface_UE::StaticClass(), TEXT("AuthoredLayoutHost"));
        Reference->AddChild(_Surface);
    }
    const auto Configured = IsValid(_Surface) && _Surface->TryConfigure(_View, TEXT("main"));
    CK_ENSURE_IF_NOT(Configured, TEXT("Pause menu could not mount its accepted authored view")) {}
    if (NOT Configured)
    { return SNullWidget::NullWidget; }
    return Super::RebuildWidget();
}

auto
    UCk_PauseMenuProof_UE::NativeOnActivated()
    -> void
{
    Super::NativeOnActivated();
    _NeedsInitialFocus = NOT DoFocus(0);
    if (NOT _InputReleaseGuard.IsValid())
    {
        _InputReleaseGuard = MakeShared<FCkPauseMenuInputReleaseGuard>(_SlateUserIndex, GetWorld());
        FSlateApplication::Get().RegisterInputPreProcessor(_InputReleaseGuard, 0);
    }
    if (NOT _PollHandle.IsValid())
    { _PollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::DoPoll)); }
}

auto
    UCk_PauseMenuProof_UE::DoPoll(float InDeltaTime)
    -> bool
{
    if (NOT IsActivated() || NOT _View.IsValid())
    { return true; }
    if (_NeedsInitialFocus)
    { _NeedsInitialFocus = NOT DoFocus(0); }
    _PollElapsed = FCk_Time{_PollElapsed.Get_Seconds() + InDeltaTime};
    constexpr auto PollInterval = FCk_Time{0.5};
    if (_PollElapsed.Get_Seconds() >= PollInterval.Get_Seconds())
    {
        _PollElapsed = FCk_Time{0.0};
        DoReloadFiles();
    }
    return true;
}

auto
    UCk_PauseMenuProof_UE::DoRelease()
    -> void
{
    if (_PollHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(_PollHandle);
        _PollHandle.Reset();
    }
    if (_InputReleaseGuard.IsValid())
    {
        _InputReleaseGuard->BeginDrain();
        _InputReleaseGuard.Reset();
    }
    if (IsValid(_Surface))
    { _Surface->ReleaseView(); }
    if (_View.IsValid())
    { _View->ReleaseOwnerInteractions(); }
    _View.Reset();
    _VolumePreview.Reset();
    _NeedsInitialFocus = false;
    if (_OwnsPause)
    {
        _OwnsPause = false;
        if (auto* Controller = GetOwningPlayer(); IsValid(Controller))
        {
            constexpr auto Pause = false;
            Controller->SetPause(Pause);
        }
    }
}

auto
    UCk_PauseMenuProof_UE::NativeOnDeactivated()
    -> void
{
    DoRelease();
    Super::NativeOnDeactivated();
}

auto
    UCk_PauseMenuProof_UE::NativeDestruct()
    -> void
{
    DoRelease();
    Super::NativeDestruct();
}

auto
    UCk_PauseMenuProof_UE::NativeGetDesiredFocusTarget() const
    -> UWidget*
{ return const_cast<UCk_PauseMenuProof_UE*>(this); }

auto
    UCk_PauseMenuProof_UE::NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InEvent)
    -> FReply
{
    if (IsActivated() && _View.IsValid() && InEvent.GetUser() == _SlateUserIndex)
    {
        const auto Count = DoGetFocusIds().Num();
        for (auto Index = 0; Index < Count; ++Index)
        {
            const auto Button = DoGetFocusableWidget(Index);
            if (Button.IsValid())
            {
                _NeedsInitialFocus = false;
                return FReply::Handled().SetUserFocus(Button.ToSharedRef(), EFocusCause::Navigation);
            }
        }
    }
    return Super::NativeOnFocusReceived(InGeometry, InEvent);
}

auto
    UCk_PauseMenuProof_UE::Request_Close()
    -> void
{
    const auto PreviousFocus = _PreviousFocus.Pin();
    DoRelease();
    DeactivateWidget();
    if (auto* Player = GetOwningLocalPlayer(); IsValid(Player))
    {
        if (auto* Subsystem = Player->GetSubsystem<UCk_UI_Layout_Subsystem_UE>(); IsValid(Subsystem))
        { Subsystem->RemoveWidget(this); }
    }
    if (PreviousFocus.IsValid() && FSlateApplication::IsInitialized())
    { FSlateApplication::Get().SetUserFocus(_SlateUserIndex, PreviousFocus, EFocusCause::SetDirectly); }
}

auto
    UCk_PauseMenuProof_UE::DoAction(FName InAction)
    -> void
{
    if (NOT IsActivated() || _SwitchingScreen)
    { return; }
    if (_Screen == ECk_UiAuthoringProof_Screen::Settings)
    {
        if (InAction == ck_pause_menu_proof::SettingsActions[1])
        {
            if (NOT Get_IsSettingsDirty())
            { return; }
            ++_ActionCounts.FindOrAdd(InAction);
            _AppliedSettings = _DraftSettings;
            _VolumePreview.Reset();
        }
        else if (InAction == ck_pause_menu_proof::SettingsActions[0])
        {
            if (_SettingsReturnToPause)
            {
                if (Request_ShowScreen(ECk_UiAuthoringProof_Screen::Pause))
                { ++_ActionCounts.FindOrAdd(InAction); }
            }
            else
            {
                ++_ActionCounts.FindOrAdd(InAction);
                _DraftSettings = _AppliedSettings;
                Request_Close();
            }
        }
        return;
    }
    if (NOT _ActionEnabled.FindRef(InAction))
    { return; }
    if (InAction == ck_pause_menu_proof::Actions[1])
    {
        if (Request_ShowScreen(ECk_UiAuthoringProof_Screen::Settings, true))
        { ++_ActionCounts.FindOrAdd(InAction); }
        return;
    }
    ++_ActionCounts.FindOrAdd(InAction);
    if (InAction == ck_pause_menu_proof::Actions[0])
    { Request_Close(); }
}

auto
    UCk_PauseMenuProof_UE::Request_SetActionEnabled(FName InAction, bool InEnabled)
    -> bool
{
    auto* Enabled = _ActionEnabled.Find(InAction);
    if (Enabled == nullptr)
    { return false; }
    *Enabled = InEnabled;
    if (NOT InEnabled && _View.IsValid())
    {
        const auto Focused = FSlateApplication::Get().GetUserFocusedWidget(_SlateUserIndex);
        for (auto Index = 0; Index < UE_ARRAY_COUNT(ck_pause_menu_proof::Actions); ++Index)
        {
            if (ck_pause_menu_proof::Actions[Index] == InAction && Focused == _View->GetButton(ck_pause_menu_proof::ButtonIds[Index]))
            { DoNavigate(1); break; }
        }
    }
    return true;
}

auto
    UCk_PauseMenuProof_UE::Request_SetLabel(FName InBinding, FText InText)
    -> bool
{
    auto* Text = _Labels.Find(InBinding);
    if (Text == nullptr)
    { return false; }
    *Text = MoveTemp(InText);
    return true;
}

auto
    UCk_PauseMenuProof_UE::Get_ActionCount(FName InAction) const
    -> int32
{ return _ActionCounts.FindRef(InAction); }

auto
    UCk_PauseMenuProof_UE::Get_AuthoredView() const
    -> TSharedPtr<FCkUiView>
{ return _View; }

auto
    UCk_PauseMenuProof_UE::Get_SlateUserIndex() const
    -> int32
{ return _SlateUserIndex; }

auto
    UCk_PauseMenuProof_UE::Get_ControlWidget(FName InId) const
    -> TSharedPtr<SWidget>
{
    if (NOT _View.IsValid() || _View->IsOwnerReleased())
    { return nullptr; }
    const auto Path = InId.ToString();
    if (Path.StartsWith(TEXT("inventory-items/")))
    {
        auto Parts = TArray<FString>{};
        Path.ParseIntoArray(Parts, TEXT("/"), false);
        const auto Repeat = _View->GetRepeat(TEXT("inventory-items"));
        if (Parts.Num() != 3 || NOT Repeat.IsValid())
        { return nullptr; }
        const auto Item = Repeat->GetItemWidget(Parts[1]);
        return Item.IsValid() ? ck_pause_menu_proof::FindWidget(Item.ToSharedRef(), FName{*Parts[2]}) : nullptr;
    }
    if (const auto Button = _View->GetButton(InId.ToString()); Button.IsValid())
    { return Button; }
    return ck_pause_menu_proof::FindWidget(_View->GetRegion(TEXT("main")), InId);
}

auto
    UCk_PauseMenuProof_UE::DoGetFocusIds() const
    -> TArray<FName>
{
    auto Ids = TArray<FName>{};
    if (_Screen == ECk_UiAuthoringProof_Screen::Inventory)
    {
        Ids = {TEXT("inventory-filter-all"), TEXT("inventory-filter-equipment"), TEXT("inventory-filter-supplies"),
            TEXT("inventory-filter-materials"), TEXT("inventory-search")};
        if (_InventoryModel.IsValid())
        {
            for (const auto& Key : _InventoryModel->Get_VisibleKeys())
            { Ids.Add(FName{*FString::Printf(TEXT("inventory-items/%s/inventory-card"), *Key)}); }
        }
        Ids.Add(TEXT("inventory-primary-action"));
        Ids.Add(TEXT("inventory-close"));
        return Ids;
    }
    if (_Screen == ECk_UiAuthoringProof_Screen::Settings)
    {
        for (const auto Id : ck_pause_menu_proof::SettingsControlIds)
        { Ids.Add(Id); }
        return Ids;
    }
    for (const auto& Id : ck_pause_menu_proof::ButtonIds)
    { Ids.Add(FName{*Id}); }
    return Ids;
}

auto
    UCk_PauseMenuProof_UE::DoGetFocusableWidget(int32 InIndex)
    -> TSharedPtr<SWidget>
{
    const auto Ids = DoGetFocusIds();
    const auto Count = Ids.Num();
    if (NOT IsActivated() || NOT FSlateApplication::IsInitialized() || NOT _View.IsValid()
        || InIndex < 0 || InIndex >= Count)
    { return nullptr; }
    if (_Screen == ECk_UiAuthoringProof_Screen::Pause && NOT _ActionEnabled.FindRef(ck_pause_menu_proof::Actions[InIndex]))
    { return nullptr; }
    const auto View = _View;
    const auto Revision = View->GetRevision();
    const auto Id = Ids[InIndex];
    const auto Button = Get_ControlWidget(Id);
    if (NOT Button.IsValid())
    { return nullptr; }
    // Activation and model updates can precede Slate's next attribute pass.
    if (Button->HasRegisteredSlateAttribute() && Button->IsAttributesUpdatesEnabled())
    {
        FSlateAttributeMetaData::UpdateExceptVisibilityAttributes(*Button,
            FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);
    }
    if (NOT IsActivated() || NOT FSlateApplication::IsInitialized() || _View != View
        || View->IsOwnerReleased() || View->GetRevision() != Revision
        || Get_ControlWidget(Id) != Button || NOT Button->IsEnabled() || NOT Button->SupportsKeyboardFocus())
    { return nullptr; }
    return Button;
}

auto
    UCk_PauseMenuProof_UE::DoFocus(int32 InIndex)
    -> bool
{
    const auto Button = DoGetFocusableWidget(InIndex);
    if (NOT Button.IsValid())
    { return false; }
    auto& Slate = FSlateApplication::Get();
    Slate.SetUserFocus(_SlateUserIndex, Button, EFocusCause::Navigation);
    const auto FocusIds = DoGetFocusIds();
    if (_Screen == ECk_UiAuthoringProof_Screen::Inventory && _View.IsValid()
        && FocusIds.IsValidIndex(InIndex) && FocusIds[InIndex].ToString().StartsWith(TEXT("inventory-items/")))
    {
        if (const auto Scroll = _View->GetScroll(TEXT("inventory-scroll")); Scroll.IsValid())
        { Scroll->ScrollDescendantIntoView(Button, false, EDescendantScrollDestination::IntoView); }
    }
    return IsActivated() && (Slate.GetUserFocusedWidget(_SlateUserIndex) == Button
        || Slate.HasUserFocusedDescendants(Button.ToSharedRef(), _SlateUserIndex));
}

auto
    UCk_PauseMenuProof_UE::DoNavigate(int32 InDirection)
    -> void
{
    if (NOT _View.IsValid())
    { return; }
    const auto Focused = FSlateApplication::Get().GetUserFocusedWidget(_SlateUserIndex);
    auto Current = InDirection > 0 ? -1 : 0;
    const auto Ids = DoGetFocusIds();
    const auto Count = Ids.Num();
    for (auto Index = 0; Index < Count; ++Index)
    {
        const auto Id = Ids[Index];
        const auto Control = Get_ControlWidget(Id);
        const auto IsFocused = Control.IsValid() && (Focused == Control
            || FSlateApplication::Get().HasUserFocusedDescendants(Control.ToSharedRef(), _SlateUserIndex));
        if (IsFocused)
        { Current = Index; break; }
    }
    for (auto Offset = 1; Offset <= Count; ++Offset)
    {
        if (DoFocus((Current + InDirection * Offset + Count * Count) % Count))
        { return; }
    }
    if (IsValid(_Surface))
    { FSlateApplication::Get().SetUserFocus(_SlateUserIndex, TakeWidget(), EFocusCause::Navigation); }
}

auto
    UCk_PauseMenuProof_UE::DoNavigateInventoryCards(int32 InOffset)
    -> bool
{
    if (_Screen != ECk_UiAuthoringProof_Screen::Inventory || NOT _InventoryModel.IsValid()
        || NOT FSlateApplication::IsInitialized())
    { return false; }
    const auto Keys = _InventoryModel->Get_VisibleKeys();
    if (Keys.IsEmpty())
    { return false; }
    const auto Focused = FSlateApplication::Get().GetUserFocusedWidget(_SlateUserIndex);
    const auto Ids = DoGetFocusIds();
    for (auto Index = 0; Index < Keys.Num(); ++Index)
    {
        const auto Id = FName{*FString::Printf(TEXT("inventory-items/%s/inventory-card"), *Keys[Index])};
        const auto Card = Get_ControlWidget(Id);
        if (Card.IsValid() && (Focused == Card
            || FSlateApplication::Get().HasUserFocusedDescendants(Card.ToSharedRef(), _SlateUserIndex)))
        {
            const auto Next = (Index + InOffset % Keys.Num() + Keys.Num()) % Keys.Num();
            const auto NextId = FName{*FString::Printf(TEXT("inventory-items/%s/inventory-card"), *Keys[Next])};
            return DoFocus(Ids.IndexOfByKey(NextId));
        }
    }
    return false;
}

auto
    UCk_PauseMenuProof_UE::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InEvent)
    -> FReply
{
    if (NOT IsActivated() || InEvent.GetUserIndex() != _SlateUserIndex)
    { return FReply::Unhandled(); }
    const auto Key = InEvent.GetKey();
    if (_Screen == ECk_UiAuthoringProof_Screen::Inventory)
    {
        const auto Offset = Key == EKeys::Right ? 1 : Key == EKeys::Left ? -1
            : Key == EKeys::Down ? 4 : Key == EKeys::Up ? -4 : 0;
        if (Offset != 0 && DoNavigateInventoryCards(Offset))
        { return FReply::Handled(); }
    }
    if (Key == EKeys::Escape && _Screen == ECk_UiAuthoringProof_Screen::Pause)
    {
        if (NOT InEvent.IsRepeat())
        { DoAction(ck_pause_menu_proof::Actions[0]); }
        return FReply::Handled();
    }
    if (Key == EKeys::Tab || (_Screen == ECk_UiAuthoringProof_Screen::Pause && (Key == EKeys::Down || Key == EKeys::Up)))
    {
        DoNavigate(Key == EKeys::Up || (Key == EKeys::Tab && InEvent.IsShiftDown()) ? -1 : 1);
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(InGeometry, InEvent);
}

auto
    UCk_PauseMenuProof_UE::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InEvent)
    -> FReply
{
    if (NOT IsActivated() || InEvent.GetUserIndex() != _SlateUserIndex)
    { return FReply::Unhandled(); }
    if (_Screen == ECk_UiAuthoringProof_Screen::Settings)
    {
        const auto Key = InEvent.GetKey();
        if (Key == EKeys::Escape && NOT InEvent.IsRepeat())
        { DoAction(ck_pause_menu_proof::SettingsActions[0]); }
        else if (Key == EKeys::Up || Key == EKeys::Down)
        { DoNavigate(Key == EKeys::Up ? -1 : 1); }
    }
    else if (_Screen == ECk_UiAuthoringProof_Screen::Inventory && InEvent.GetKey() == EKeys::Escape
        && NOT InEvent.IsRepeat())
    { Request_Close(); }
    return FReply::Handled();
}

auto
    UCk_PauseMenuProof_UE::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InEvent)
    -> FReply
{ return IsActivated() && InEvent.GetUserIndex() == _SlateUserIndex ? FReply::Handled() : FReply::Unhandled(); }

auto
    UCk_PauseMenuProof_UE::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InEvent)
    -> FReply
{ return IsActivated() && InEvent.GetUserIndex() == _SlateUserIndex ? FReply::Handled() : FReply::Unhandled(); }

auto
    UCk_PauseMenuProof_UE::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InEvent)
    -> FReply
{ return IsActivated() && InEvent.GetUserIndex() == _SlateUserIndex ? FReply::Handled() : FReply::Unhandled(); }

#undef LOCTEXT_NAMESPACE
