#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkGym_Switchboard_Subsystem.h"
#include "CkSlateLayout/CkFlexText.h"
#include "CkSlateLayout/CkUiHudControls.h"
#include "CkSlateLayout/CkUiWidgetRegistry.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/UI/CkCombatHudProof.h"
#include "UI/CkCombatHudProofModel.h"
#include "UI/CkCombatHudReplicaArt.h"
#include "UI/CkUiReplicaPresentation.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Layout/Children.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

#include <limits>

namespace ck_combat_hud_proof_test
{
    struct FThemeCase
    {
        ECk_CombatHudProof_Theme Theme;
        const TCHAR* Name;
        const TCHAR* GymName;
        const TCHAR* GameModeName;
        const TCHAR* PlayerName;
    };

    constexpr FThemeCase Cases[] = {
        {ECk_CombatHudProof_Theme::Futuristic, TEXT("Futuristic"), TEXT("UI HUD: Mass Effect 3"),
            TEXT("Ck_CombatHud_Futuristic_GameMode"), TEXT("Ck_CombatHud_Futuristic_PlayerController")},
        {ECk_CombatHudProof_Theme::Tactical, TEXT("Tactical"), TEXT("UI HUD: Battlefield 4"),
            TEXT("Ck_CombatHud_Tactical_GameMode"), TEXT("Ck_CombatHud_Tactical_PlayerController")},
        {ECk_CombatHudProof_Theme::Fantasy, TEXT("Fantasy"), TEXT("UI HUD: Skyrim"),
            TEXT("Ck_CombatHud_Fantasy_GameMode"), TEXT("Ck_CombatHud_Fantasy_PlayerController")}
    };

    struct FState
    {
        ~FState()
        {
            RestoreDebug();
            RestoreWindow();
            RestoreSettings();
        }

        auto HideDebug(UWorld* InWorld) -> bool
        {
            UGameInstance* Instance = IsValid(InWorld) ? InWorld->GetGameInstance() : nullptr;
            auto* Registry = IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
            IConsoleVariable* Watermark = IConsoleManager::Get().FindConsoleVariable(TEXT("ck.UI.WatermarkDisplayPolicy"));
            IConsoleVariable* Input = IConsoleManager::Get().FindConsoleVariable(TEXT("ck.InputOverlay"));
            if (!IsValid(Registry) || Watermark == nullptr || Input == nullptr) { return false; }
            if (!bDebugCaptured)
            {
                DebugRegistry = Registry;
                bOriginalSuppression = Registry->Get_SuppressHUDDuringStartup();
                for (const TCHAR* Name : {TEXT("ck.UI.WatermarkDisplayPolicy"), TEXT("ck.InputOverlay")})
                {
                    IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
                    DebugValues.Add(Name, CVar->GetString());
                    DebugFlags.Add(Name, static_cast<EConsoleVariableFlags>(CVar->GetFlags()));
                }
                bDebugCaptured = true;
            }
            bDebugRestored = false;
            for (IConsoleVariable* CVar : {Watermark, Input})
            { CVar->Set(0, static_cast<EConsoleVariableFlags>(CVar->GetFlags() & ECVF_SetByMask)); }
            Registry->Set_SuppressHUDDuringStartup(true);
            const bool Hidden = Watermark->GetInt() == 0 && Input->GetInt() == 0
                && Registry->Get_SuppressHUDDuringStartup();
            if (!Hidden) { RestoreDebug(); }
            return Hidden;
        }

        auto RestoreDebug() -> bool
        {
            if (!bDebugCaptured || bDebugRestored) { return true; }
            bool Restored = true;
            for (const auto& [Name, Value] : DebugValues)
            {
                IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
                if (CVar == nullptr) { Restored = false; continue; }
                CVar->Set(*Value, static_cast<EConsoleVariableFlags>(CVar->GetFlags() & ECVF_SetByMask));
                CVar->SetFlags(DebugFlags.FindRef(Name));
                Restored &= CVar->GetString() == Value
                    && static_cast<EConsoleVariableFlags>(CVar->GetFlags()) == DebugFlags.FindRef(Name);
            }
            if (DebugRegistry.IsValid())
            {
                DebugRegistry->Set_SuppressHUDDuringStartup(bOriginalSuppression);
                Restored &= DebugRegistry->Get_SuppressHUDDuringStartup() == bOriginalSuppression;
            }
            bDebugRestored = Restored;
            return Restored;
        }

        void RestoreWindow()
        {
            if (!bWindowCaptured) { return; }
            if (const TSharedPtr<SWindow> Window = WindowToRestore.Pin())
            { Window->Resize(OriginalWindowSize); }
            bWindowCaptured = false;
        }

        void RestoreSettings()
        {
            if (!bSettingsCaptured || bSettingsRestored) { return; }
            UCkGym_StartupSettings* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            if (!IsValid(Settings)) { return; }
            Settings->StartupMode = StartupMode;
            Settings->ControlPanelMode = PanelMode;
            Settings->DefaultGymName = DefaultGymName;
            Settings->LastGymName = LastGymName;
            Settings->RecentGymNames = RecentGymNames;
            Settings->SaveConfig();
            bSettingsRestored = Settings->StartupMode == StartupMode
                && Settings->ControlPanelMode == PanelMode
                && Settings->DefaultGymName == DefaultGymName
                && Settings->LastGymName == LastGymName
                && Settings->RecentGymNames == RecentGymNames;
        }

        ECkGym_StartupMode StartupMode = ECkGym_StartupMode::Cycler;
        ECkGym_ControlPanel_Mode PanelMode = ECkGym_ControlPanel_Mode::Full;
        FString DefaultGymName;
        FString LastGymName;
        TArray<FString> RecentGymNames;
        bool bSettingsCaptured = false;
        bool bSettingsRestored = false;
        TWeakPtr<SWindow> WindowToRestore;
        FVector2D OriginalWindowSize = FVector2D::ZeroVector;
        bool bWindowCaptured = false;
        TWeakObjectPtr<UCk_CombatHudProof_UE> PreviousHud;
        TSharedPtr<FCkUiView> PreviousView;
        double ContextReadyAt = 0.0;
        TMap<FString, FString> DebugValues;
        TMap<FString, EConsoleVariableFlags> DebugFlags;
        TWeakObjectPtr<UCkGym_Registry_Subsystem> DebugRegistry;
        bool bOriginalSuppression = false;
        bool bDebugCaptured = false;
        bool bDebugRestored = false;
        FAutomationTestBase* Test = nullptr;
    };

    auto GetPlayer(UWorld* InWorld) -> APlayerController*
    { return IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr; }

    auto GetRegistry(UWorld* InWorld) -> UCkGym_Registry_Subsystem*
    {
        UGameInstance* Instance = IsValid(InWorld) ? InWorld->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    auto GetSwitchboard(APlayerController* InPlayer) -> UCkGym_Switchboard_Subsystem*
    {
        ULocalPlayer* Local = IsValid(InPlayer) ? InPlayer->GetLocalPlayer() : nullptr;
        return IsValid(Local) ? Local->GetSubsystem<UCkGym_Switchboard_Subsystem>() : nullptr;
    }

    auto IsGymReady(UWorld* InWorld, const FThemeCase& InCase) -> bool
    {
        const AGameModeBase* Mode = IsValid(InWorld) ? InWorld->GetAuthGameMode() : nullptr;
        const APlayerController* Player = GetPlayer(InWorld);
        return IsValid(Mode) && Mode->GetClass()->GetName().Contains(InCase.GameModeName)
            && IsValid(Player) && Player->GetClass()->GetName().Contains(InCase.PlayerName)
            && IsValid(Player->GetLocalPlayer()) && IsValid(GetSwitchboard(GetPlayer(InWorld)));
    }

    auto GetHud(UWorld* InWorld) -> UCk_CombatHudProof_UE*
    {
        APlayerController* Player = GetPlayer(InWorld);
        if (!IsValid(Player)) { return nullptr; }
        UFunction* Function = Player->FindFunction(FName{TEXT("Get_ActiveHud")});
        const FObjectProperty* Return = Function != nullptr
            ? CastField<FObjectProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr) { return nullptr; }
        TArray<uint8> Params;
        Params.SetNumZeroed(Function->ParmsSize);
        Function->InitializeStruct(Params.GetData());
        Player->ProcessEvent(Function, Params.GetData());
        auto* Hud = Cast<UCk_CombatHudProof_UE>(Return->GetObjectPropertyValue_InContainer(Params.GetData()));
        Function->DestroyStruct(Params.GetData());
        return IsValid(Hud) && Hud->IsActive() ? Hud : nullptr;
    }

    auto DescribeHudStartup(UWorld* InWorld) -> FString
    {
        APlayerController* Player = GetPlayer(InWorld);
        if (!IsValid(Player)) { return TEXT("no valid player controller"); }
        UFunction* Function = Player->FindFunction(FName{TEXT("Get_ActiveHud")});
        const FProperty* Return = Function != nullptr ? Function->GetReturnProperty() : nullptr;
        const FObjectProperty* RawProperty = FindFProperty<FObjectProperty>(Player->GetClass(), TEXT("_Hud"));
        UObject* RawObject = RawProperty != nullptr
            ? RawProperty->GetObjectPropertyValue_InContainer(Player) : nullptr;
        auto* RawHud = IsValid(RawObject) ? Cast<UCk_CombatHudProof_UE>(RawObject) : nullptr;
        return FString::Printf(TEXT("player=%s getter=%s return=%s raw-property=%s raw-object=%s raw-valid=%s "
            "raw-active=%s source-error='%s'"), *Player->GetClass()->GetName(),
            Function != nullptr ? TEXT("present") : TEXT("missing"),
            Return != nullptr ? *Return->GetClass()->GetName() : TEXT("missing"),
            RawProperty != nullptr ? TEXT("present") : TEXT("missing"),
            IsValid(RawObject) ? *RawObject->GetClass()->GetName() : TEXT("null or invalid"),
            IsValid(RawHud) ? TEXT("true") : TEXT("false"),
            IsValid(RawHud) && RawHud->IsActive() ? TEXT("true") : TEXT("false"),
            IsValid(RawHud) ? *RawHud->Get_SourceError() : TEXT("unavailable"));
    }

    void Tick(FSlateApplication& InSlate)
    { InSlate.PumpMessages(); InSlate.Tick(); InSlate.Tick(); }

    auto PressKey(const FKey& InKey) -> bool
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        const FKeyEvent Event{InKey, FModifierKeysState{}, 0, false, 0, 0};
        const bool Down = Slate.ProcessKeyDownEvent(Event);
        const bool Up = Slate.ProcessKeyUpEvent(Event);
        Tick(Slate);
        return Down && Up;
    }

    auto FocusViewport(UWorld* InWorld) -> bool
    {
        UGameViewportClient* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const TSharedPtr<SViewport> Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!Viewport.IsValid()) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Viewport.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
        Window->BringToFront(true);
        Slate.SetUserFocus(0, Viewport.ToSharedRef(), EFocusCause::SetDirectly);
        return Slate.IsActive() && Window->IsActive()
            && Slate.GetUserFocusedWidget(0).Get() == Viewport.Get();
    }

    auto FindTagged(const TSharedRef<SWidget>& InRoot, const FName InTag) -> TSharedPtr<SWidget>
    {
        if (InRoot->GetTag() == InTag) { return InRoot; }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<SWidget> Found = FindTagged(
                ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag); Found.IsValid())
            { return Found; }
        }
        return nullptr;
    }

    auto FindNativeArt(const TSharedRef<SWidget>& InRoot, const FName InTag) -> TSharedPtr<SCkCombatHudReplicaArt>
    {
        if (InRoot->GetTag() == InTag && InRoot->GetTypeAsString() == TEXT("SCkCombatHudReplicaArt"))
        { return StaticCastSharedRef<SCkCombatHudReplicaArt>(InRoot); }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<SCkCombatHudReplicaArt> Found = FindNativeArt(
                ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag); Found.IsValid())
            { return Found; }
        }
        return nullptr;
    }

    auto HasControl(UCk_CombatHudProof_UE* InHud, const TCHAR* InId) -> bool
    {
        const TSharedPtr<FCkUiView> View = IsValid(InHud) ? InHud->Get_AuthoredView() : nullptr;
        return View.IsValid() && FindTagged(View->GetRegion(TEXT("main")), FName{InId}).IsValid();
    }

    auto FindNativeText(const TSharedRef<SWidget>& InRoot, FName InTag) -> TSharedPtr<SCkFlexText>
    {
        if (InRoot->GetTag() == InTag && InRoot->GetTypeAsString() == TEXT("SCkFlexText"))
        { return StaticCastSharedRef<SCkFlexText>(InRoot); }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const auto Found = FindNativeText(
                ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag); Found.IsValid())
            { return Found; }
        }
        return nullptr;
    }

    struct FTextFit
    {
        FVector2D Required;
        FVector2D Allocated;
        int32 FontSize = 0;
        FName Tag;

        auto Fits() const -> bool
        { return Required.X <= Allocated.X + 0.5 && Required.Y <= Allocated.Y + 0.5; }

        auto Describe() const -> FString
        {
            return FString::Printf(TEXT("%s font=%d required=%.2fx%.2f allocated=%.2fx%.2f"),
                *Tag.ToString(), FontSize, Required.X, Required.Y, Allocated.X, Allocated.Y);
        }
    };

    auto MeasureHudText(UCk_CombatHudProof_UE* InHud, FName InTag) -> TOptional<FTextFit>
    {
        const auto View = IsValid(InHud) ? InHud->Get_AuthoredView() : nullptr;
        if (!View.IsValid()) { return {}; }
        const auto Text = FindNativeText(View->GetRegion(TEXT("main")), InTag);
        if (!Text.IsValid() || Text->GetText().IsEmpty()) { return {}; }
        const FGeometry& Geometry = Text->GetCachedGeometry();
        const FVector2D Allocated = Geometry.GetLocalSize();
        if (!FMath::IsFinite(Geometry.Scale) || Geometry.Scale <= 0.0f
            || !FMath::IsFinite(Allocated.X) || Allocated.X <= 0.0
            || !FMath::IsFinite(Allocated.Y) || Allocated.Y <= 0.0) { return {}; }
        // Unconstrained measurement uses the same font/layout as native paint, without
        // Yoga's exact-height clamp masking the oversized line that caused the crop.
        const FVector2D Required = Text->Measure(YGUndefined, YGMeasureModeUndefined,
            YGUndefined, YGMeasureModeUndefined, Geometry.Scale);
        const int32 FontSize = Text->GetFont().Size;
        if (FontSize <= 0 || !FMath::IsFinite(Required.X) || Required.X <= 0.0
            || !FMath::IsFinite(Required.Y) || Required.Y <= 0.0) { return {}; }
        return FTextFit{Required, Allocated, FontSize, InTag};
    }

    auto FindDisplayed(const TSharedRef<SWidget>& InRoot, FName InTag, bool InAncestorsVisible) -> TOptional<bool>
    {
        const bool Visible = InAncestorsVisible && InRoot->GetVisibility().IsVisible();
        if (InRoot->GetTag() == InTag) { return Visible; }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const auto Found = FindDisplayed(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag, Visible);
            if (Found.IsSet()) { return Found; }
        }
        return {};
    }

    auto ContextDisplayed(UCk_CombatHudProof_UE* InHud, ECk_CombatHudProof_Theme InTheme, int32 InStep) -> bool
    {
        const auto View = IsValid(InHud) ? InHud->Get_AuthoredView() : nullptr;
        if (!View.IsValid()) { return false; }
        const auto Matches = [&](const TCHAR* Id, bool Expected)
        {
            const auto Visible = FindDisplayed(View->GetRegion(TEXT("main")), FName{Id}, true);
            return Visible.IsSet() && Visible.GetValue() == Expected;
        };
        if (InTheme == ECk_CombatHudProof_Theme::Futuristic)
        { return Matches(TEXT("objective-prompt"), InStep == 1); }
        if (InTheme == ECk_CombatHudProof_Theme::Tactical)
        { return Matches(TEXT("capture-status"), InStep == 1) && Matches(TEXT("kill-feed-text"), InStep == 2); }
        return Matches(TEXT("sneak-eye"), InStep == 1) && Matches(TEXT("interaction-row"), InStep == 2)
            && Matches(TEXT("target-group"), false) && Matches(TEXT("arrow-readout"), false);
    }

    auto SourceRoot(const FThemeCase& InCase) -> FString
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
        return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(),
            TEXT("Resources/UiAuthoring/CombatHudProof"), InCase.Name) : FString{};
    }

    auto Capture(UWorld* InWorld, FState& InState, const FString& InName, FIntPoint InSize) -> bool
    {
        UGameViewportClient* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const TSharedPtr<SViewport> Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!Viewport.IsValid()) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Viewport.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
        if (!InState.bWindowCaptured)
        {
            InState.WindowToRestore = Window;
            InState.OriginalWindowSize = Window->GetClientSizeInScreen();
            InState.bWindowCaptured = true;
        }
        TArray<FColor> Pixels;
        FIntVector Size = FIntVector::ZeroValue;
        for (int32 Attempt = 0; Attempt < 5; ++Attempt)
        {
            Tick(Slate);
            Pixels.Reset();
            Size = FIntVector::ZeroValue;
            if (!Slate.TakeScreenshot(Viewport.ToSharedRef(), Pixels, Size)
                || Size.X <= 0 || Size.Y <= 0 || Pixels.Num() < Size.X * Size.Y)
            { return false; }
            if (Size.X == InSize.X && Size.Y == InSize.Y) { break; }
            Window->Resize(Window->GetClientSizeInScreen() + FVector2D{
                static_cast<double>(InSize.X - Size.X), static_cast<double>(InSize.Y - Size.Y)});
        }
        if (Size.X != InSize.X || Size.Y != InSize.Y) { return false; }
        UCk_CombatHudProof_UE* Hud = GetHud(InWorld);
        if (!IsValid(Hud) || InState.Test == nullptr) { return false; }
        bool TextFits = true;
        if (Hud->Get_Theme() == ECk_CombatHudProof_Theme::Futuristic)
        {
            const auto Fit = MeasureHudText(Hud, FName{TEXT("ammo-value")});
            TextFits = Fit.IsSet() && Fit->Fits();
            InState.Test->TestTrue(*FString::Printf(TEXT("%s native ammo text fits: %s"), *InName,
                Fit.IsSet() ? *Fit->Describe() : TEXT("missing or invalid native text geometry")), TextFits);
        }
        else if (Hud->Get_Theme() == ECk_CombatHudProof_Theme::Tactical)
        {
            const auto Root = Hud->Get_AuthoredView()->GetRegion(TEXT("main"));
            const auto Friendly = FindDisplayed(Root, FName{TEXT("objective-icon")}, true);
            const auto Hostile = FindDisplayed(Root, FName{TEXT("objective-icon-hostile")}, true);
            FCkUiView::FDataBindings Ownership;
            Hud->Get_Model()->PopulateBindings(Ownership);
            const bool OwnerMatches = Friendly.IsSet() && Hostile.IsSet()
                && Friendly.GetValue() == Ownership.Visibility.FindRef(TEXT("ObjectiveFriendly")).Get(false)
                && Hostile.GetValue() == Ownership.Visibility.FindRef(TEXT("ObjectiveHostile")).Get(false)
                && Friendly.GetValue() != Hostile.GetValue();
            InState.Test->TestTrue(TEXT("mounted Battlefield objective matches exclusive typed ownership"), OwnerMatches);
            TextFits &= OwnerMatches;
            for (const TCHAR* Id : {TEXT("friendly-side"), TEXT("tickets-friendly"), TEXT("ticket-divider"),
                TEXT("enemy-side"), TEXT("tickets-enemy"), TEXT("round-timer"), TEXT("ammo-value"),
                TEXT("reserve-value"), TEXT("fire-mode"), TEXT("health-value"), TEXT("grenade-count"),
                TEXT("squad-one"), TEXT("squad-two"), TEXT("squad-three"), TEXT("squad-four"),
                TEXT("objective-distance"), TEXT("capture-status"), TEXT("kill-feed-text"), TEXT("reload-status")})
            {
                const auto Displayed = FindDisplayed(Hud->Get_AuthoredView()->GetRegion(TEXT("main")), FName{Id}, true);
                if (Displayed.IsSet() && !Displayed.GetValue()) { continue; }
                const auto Fit = MeasureHudText(Hud, FName{Id});
                const bool Fits = Fit.IsSet() && Fit->Fits();
                InState.Test->TestTrue(*FString::Printf(TEXT("%s native %s text fits: %s"), *InName, Id,
                    Fit.IsSet() ? *Fit->Describe() : TEXT("missing or invalid native text geometry")), Fits);
                TextFits &= Fits;
            }
        }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/CombatHudProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const bool Saved = FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, InName + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
        return Saved && TextFits;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_CombatHudProof_Model,
    "Ck.UiAuthoring.CombatHudProof.Model",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
        | EAutomationTestFlags::NonNullRHI)

bool FCk_CombatHudProof_Model::RunTest(const FString&)
{
    using namespace ck_combat_hud_proof_test;
    for (const FThemeCase& Case : Cases)
    {
        TSharedPtr<FCkCombatHudProofModel> Model;
        FString Error;
        if (!TestTrue(TEXT("replica model admits its installed source and required portraits"),
            FCkCombatHudProofModel::TryCreate(Case.Theme,
                SourceRoot(Case), Model, Error)
                && Model.IsValid() && Error.IsEmpty())) { return false; }
        FCkUiWidgetRegistry Registry;
        const auto Registration = FCkUiHudControls::Register(Registry);
        if (!TestTrue(*FString::Printf(TEXT("%s HUD controls register: %s"), Case.Name,
            *FString::Join(Registration.Errors, TEXT(" | "))), Registration.Succeeded)) { return false; }
        const auto ArtRegistration = FCkCombatHudReplicaArt::Register(Registry);
        if (!TestTrue(TEXT("replica artwork registers atomically"), ArtRegistration.Succeeded)) { return false; }
        FString Markup;
        FString Stylesheet;
        const FString Root = SourceRoot(Case);
        if (!TestTrue(*FString::Printf(TEXT("%s installed HUD source pair can be read"), Case.Name),
            FFileHelper::LoadFileToString(Markup, *FPaths::Combine(Root, TEXT("CombatHudProof.ui.html")))
                && FFileHelper::LoadFileToString(Stylesheet,
                    *FPaths::Combine(Root, TEXT("CombatHudProof.ui.css"))))) { return false; }
        auto Bindings = FCkUiView::FDataBindings{};
        Model->PopulateBindings(Bindings);
        TSharedPtr<const FCkUiFontRegistrySnapshot> Fonts;
        if (!TestTrue(TEXT("replica presentation publishes named fonts to the model fixture"),
                ck_ui_replica_presentation::TryLoad(
                    Case.Theme == ECk_CombatHudProof_Theme::Futuristic ? TEXT("MassEffect")
                        : Case.Theme == ECk_CombatHudProof_Theme::Tactical ? TEXT("Battlefield") : TEXT("Skyrim"),
                    Bindings, Fonts, Error) && Fonts.IsValid()))
        { AddError(Error); return false; }
        const TSharedRef<FCkUiView> View = FCkUiView::Create({}, {}, {},
            FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 24), MoveTemp(Bindings), Registry.CreateSnapshot(), Fonts);
        View->GetRegion(TEXT("main"));
        const int64 BeforeRevision = View->GetRevision();
        const auto SourceResult = View->TryReload(Markup, Stylesheet, TEXT("CombatHudProof"));
        if (!TestTrue(*FString::Printf(TEXT("%s installed HUD source reloads: %s"), Case.Name,
            *FString::Join(SourceResult.Errors, TEXT(" | "))), SourceResult.Succeeded)) { return false; }
        TestTrue(*FString::Printf(TEXT("%s main HUD region publishes"), Case.Name),
            View->HasRegion(TEXT("main")) && View->GetRevision() > BeforeRevision
                && FindTagged(View->GetRegion(TEXT("main")), FName{TEXT("aim-reticle")}).IsValid());
        View->ReleaseOwnerInteractions();
        FCkUiView::FDataBindings Presentation;
        Model->PopulateBindings(Presentation);
        const auto Visible = [&](const TCHAR* Name) { return Presentation.Visibility.FindRef(Name).Get(false); };
        if (Case.Theme == ECk_CombatHudProof_Theme::Fantasy)
        {
            TestTrue(TEXT("Skyrim combat initially shows target and arrows"), Visible(TEXT("ShowTarget")) && Visible(TEXT("ShowArrows")));
            Model->Advance(4.0f);
            TestEqual(TEXT("Skyrim full idle health fades"), Presentation.Number.FindRef(TEXT("HealthOpacity")).Get(1.0f), 0.0f);
            TestEqual(TEXT("Skyrim full idle magicka fades"), Presentation.Number.FindRef(TEXT("MagickaOpacity")).Get(1.0f), 0.0f);
            Model->Request_Command(ECk_CombatHudProof_Command::Damage);
            TestEqual(TEXT("Skyrim damage shows only the affected resource"), Presentation.Number.FindRef(TEXT("HealthOpacity")).Get(0.0f), 1.0f);
            TestEqual(TEXT("Skyrim damage does not wake full idle magicka"), Presentation.Number.FindRef(TEXT("MagickaOpacity")).Get(1.0f), 0.0f);
            Model->Request_Command(ECk_CombatHudProof_Command::Context);
            TestTrue(TEXT("Skyrim sneak replaces combat context"), Visible(TEXT("ShowSneak")) && !Visible(TEXT("ShowArrows")) && !Visible(TEXT("ShowTarget")));
            Model->Request_Command(ECk_CombatHudProof_Command::Context);
            TestTrue(TEXT("Skyrim interaction replaces sneak context"), Visible(TEXT("ShowInteraction")) && !Visible(TEXT("ShowSneak")));
        }
        else if (Case.Theme == ECk_CombatHudProof_Theme::Tactical)
        {
            TestTrue(TEXT("Battlefield starts with one hostile objective presentation"),
                Visible(TEXT("ObjectiveHostile")) && !Visible(TEXT("ObjectiveFriendly")));
            const auto Tickets = Presentation.Text.FindRef(TEXT("EnemyTickets")).Get().ToString();
            Model->Request_Command(ECk_CombatHudProof_Command::Ability);
            TestTrue(TEXT("Battlefield flag capture updates live ticket and ownership readouts"),
                Visible(TEXT("ShowCapture")) && Presentation.Text.FindRef(TEXT("EnemyTickets")).Get().ToString() != Tickets
                    && Presentation.Text.FindRef(TEXT("FlagText")).Get().ToString().Contains(TEXT("[C]")));
            TestTrue(TEXT("Battlefield capture switches the whole objective presentation exclusively"),
                Visible(TEXT("ObjectiveFriendly")) && !Visible(TEXT("ObjectiveHostile")));
            Model->Request_Command(ECk_CombatHudProof_Command::Reset);
            TestTrue(TEXT("Battlefield reset restores hostile objective presentation"),
                Visible(TEXT("ObjectiveHostile")) && !Visible(TEXT("ObjectiveFriendly")));
        }
        else
        {
            TestTrue(TEXT("Mass Effect uses two valid squad portrait resources"),
                Presentation.Images.FindRef(TEXT("SquadOnePortrait")).Get(nullptr) != nullptr
                    && Presentation.Images.FindRef(TEXT("SquadTwoPortrait")).Get(nullptr) != nullptr);
            Model->Request_Command(ECk_CombatHudProof_Command::Context);
            TestTrue(TEXT("Mass Effect objective prompt is contextual"), Visible(TEXT("ShowObjective")));
        }
        Model->Request_Command(ECk_CombatHudProof_Command::Reset);
        const auto Initial = Model->Get_State();
        const FString InitialSummary = Model->Get_Summary();
        TestFalse(TEXT("invalid command rejects"),
            Model->Request_Command(static_cast<ECk_CombatHudProof_Command>(255)));
        TestFalse(TEXT("negative time step rejects"), Model->Advance(-1.0f));
        TestFalse(TEXT("NaN time step rejects"),
            Model->Advance(std::numeric_limits<float>::quiet_NaN()));
        TestTrue(TEXT("invalid inputs leave all live state unchanged"),
            Model->Get_Summary() == InitialSummary
                && Model->Get_State().Get_Health() == Initial.Get_Health()
                && Model->Get_State().Get_Shield() == Initial.Get_Shield()
                && Model->Get_State().Get_Ammo() == Initial.Get_Ammo()
                && Model->Get_State().Get_ReloadProgress() == Initial.Get_ReloadProgress());
        TSharedPtr<FCkCombatHudProofModel> Output = Model;
        TestFalse(TEXT("invalid theme rejects before resource admission"),
            FCkCombatHudProofModel::TryCreate(static_cast<ECk_CombatHudProof_Theme>(255),
                FString{}, Output, Error));
        TestTrue(TEXT("invalid theme preserves prior output"), Output == Model && !Error.IsEmpty());
        Error.Reset();
        TestFalse(TEXT("missing source resources rejects without candidate publication"),
            FCkCombatHudProofModel::TryCreate(Case.Theme, FString{}, Output, Error));
        TestTrue(TEXT("missing source resources preserves prior output"), Output == Model && !Error.IsEmpty());

        for (int32 Hit = 0; Hit < 10; ++Hit)
        { TestTrue(TEXT("fixture damage command applies"), Model->Request_Command(ECk_CombatHudProof_Command::Damage)); }
        TestTrue(TEXT("damage clamps health and shield at zero"),
            Model->Get_State().Get_Health() == 0.0f && Model->Get_State().Get_Shield() == 0.0f);
        TestTrue(TEXT("low-health shortcut fixes threshold and status"),
            Model->Request_Command(ECk_CombatHudProof_Command::LowHealth)
                && Model->Get_State().Get_Health() == 20.0f
                && Model->Get_Summary() == TEXT("LOW HEALTH"));
        for (int32 Heal = 0; Heal < 5; ++Heal)
        { TestTrue(TEXT("fixture recovery command applies"), Model->Request_Command(ECk_CombatHudProof_Command::Recover)); }
        TestTrue(TEXT("recovery clamps vitals at full"),
            Model->Get_State().Get_Health() == 100.0f
                && Model->Get_State().Get_Stamina() == 100.0f
                && Model->Get_State().Get_Magicka() == 100.0f);

        TestTrue(TEXT("reset clears prior damage"), Model->Request_Command(ECk_CombatHudProof_Command::Reset));
        TestTrue(TEXT("sprint begins"), Model->Request_Command(ECk_CombatHudProof_Command::Sprint));
        TestTrue(TEXT("five seconds of sprint drains stamina and stops sprint"),
            Model->Advance(5.0f) && Model->Get_State().Get_Stamina() == 0.0f
                && !Model->Get_State().Get_Sprinting());
        TestFalse(TEXT("empty stamina rejects another sprint"),
            Model->Request_Command(ECk_CombatHudProof_Command::Sprint));
        TestTrue(TEXT("idle recovery refills stamina"),
            Model->Advance(5.0f) && Model->Get_State().Get_Stamina() == 100.0f);

        const int32 InitialAmmo = Model->Get_State().Get_Ammo();
        for (int32 Shot = 0; Shot < InitialAmmo; ++Shot)
        { TestTrue(TEXT("fire consumes one round"), Model->Request_Command(ECk_CombatHudProof_Command::Fire)); }
        TestTrue(TEXT("magazine reaches exactly zero"), Model->Get_State().Get_Ammo() == 0);
        TestFalse(TEXT("empty magazine rejects fire"),
            Model->Request_Command(ECk_CombatHudProof_Command::Fire));
        const int32 InitialReserve = Model->Get_State().Get_Reserve();
        TestTrue(TEXT("reload begins from empty magazine"),
            Model->Request_Command(ECk_CombatHudProof_Command::Reload)
                && Model->Get_State().Get_Reloading());
        TestFalse(TEXT("active reload rejects fire"),
            Model->Request_Command(ECk_CombatHudProof_Command::Fire));
        TestTrue(TEXT("reload fraction advances while ammo stays empty"),
            Model->Advance(0.8f) && Model->Get_State().Get_ReloadProgress() > 0.0f
                && Model->Get_State().Get_ReloadProgress() < 1.0f
                && Model->Get_State().Get_Ammo() == 0);
        TestTrue(TEXT("reload fills magazine and spends matching reserve"),
            Model->Advance(1.0f) && !Model->Get_State().Get_Reloading()
                && Model->Get_State().Get_Ammo() > 0
                && Model->Get_State().Get_Reserve() == InitialReserve - Model->Get_State().Get_Ammo());

        TestTrue(TEXT("ability spends magicka and starts cooldown"),
            Model->Request_Command(ECk_CombatHudProof_Command::Ability)
                && Model->Get_State().Get_Magicka() == 75.0f
                && Model->Get_State().Get_CooldownProgress() > 0.0f);
        TestFalse(TEXT("active cooldown rejects repeated ability"),
            Model->Request_Command(ECk_CombatHudProof_Command::Ability));
        TestTrue(TEXT("cooldown expires with finite world time"),
            Model->Advance(3.1f) && Model->Get_State().Get_CooldownProgress() == 0.0f);
        TestTrue(TEXT("expired ability can restart"),
            Model->Request_Command(ECk_CombatHudProof_Command::Ability));
        TestTrue(TEXT("reset cancels active cooldown and restores theme fixture"),
            Model->Request_Command(ECk_CombatHudProof_Command::Reset)
                && Model->Get_State().Get_CooldownProgress() == 0.0f
                && Model->Get_State().Get_Ammo() == InitialAmmo
                && Model->Get_State().Get_Health() == 100.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_CombatHudProof_PIE,
    "Ck.UiAuthoring.CombatHudProof.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
        | EAutomationTestFlags::NonNullRHI)

bool FCk_CombatHudProof_PIE::RunTest(const FString&)
{
    using namespace ck_combat_hud_proof_test;
    if (!FSlateApplication::IsInitialized())
    { AddError(TEXT("Combat HUD proof requires Slate.")); return false; }

    const TSharedRef<FState> State = MakeShared<FState>();
    State->Test = this;
    UCkGym_StartupSettings* Settings = GetMutableDefault<UCkGym_StartupSettings>();
    if (!TestNotNull(TEXT("per-user gym startup settings"), Settings)) { return false; }
    State->StartupMode = Settings->StartupMode;
    State->PanelMode = Settings->ControlPanelMode;
    State->DefaultGymName = Settings->DefaultGymName;
    State->LastGymName = Settings->LastGymName;
    State->RecentGymNames = Settings->RecentGymNames;
    State->bSettingsCaptured = true;
    Settings->StartupMode = ECkGym_StartupMode::Cycler;
    Settings->ControlPanelMode = ECkGym_ControlPanel_Mode::Hidden;

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1,
        TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UCkGym_Registry_Subsystem* Registry = GetRegistry(ck::auto_test::net::Get_ServerWorld());
            if (!IsValid(Registry)) { return false; }
            for (const FThemeCase& Case : Cases)
            { if (Registry->Find_GymIndexByName(Case.GymName) == INDEX_NONE) { return false; } }
            return true;
        }), 15.0, TEXT("all three Combat HUD gyms register")));

    for (const FThemeCase& Case : Cases)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, Case](UWorld* InWorld) -> void
            {
                UCkGym_Registry_Subsystem* Registry = GetRegistry(InWorld);
                if (!TestNotNull(TEXT("combat gym registry"), Registry)) { return; }
                const int32 Index = Registry->Find_GymIndexByName(Case.GymName);
                if (!TestTrue(TEXT("named Combat HUD gym exists"),
                    Registry->Get_Entries().IsValidIndex(Index))) { return; }
                const FCkGym_Entry& Entry = Registry->Get_Entries()[Index];
                TestEqual(TEXT("Combat HUD gym belongs to CkUI"), Entry.Category, FString{TEXT("CkUI")});
                if (TestTrue(TEXT("registry names the authored HUD GameMode"),
                    IsValid(Entry.GameModeClass.Get())
                        && Entry.GameModeClass.Get()->GetName().Contains(Case.GameModeName)))
                { Registry->Request_TravelToGym(Index); }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([Case]() -> bool
            { return IsGymReady(ck::auto_test::net::Get_ServerWorld(), Case); }),
            30.0, TEXT("registry travel loads the actual Combat HUD gym")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([Case]() -> bool
            {
                UCk_CombatHudProof_UE* Hud = GetHud(ck::auto_test::net::Get_ServerWorld());
                return IsValid(Hud) && Hud->Get_Theme() == Case.Theme
                    && Hud->Get_AuthoredView().IsValid() && Hud->Get_Model().IsValid();
            }), 10.0, TEXT("controller mounts its authored Combat HUD")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
            { TestTrue(TEXT("capture temporarily hides debug overlays"), State->HideDebug(InWorld)); })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(2));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State, Case](UWorld* InWorld) -> void
            {
                UCk_CombatHudProof_UE* Hud = GetHud(InWorld);
                if (!TestNotNull(*FString::Printf(TEXT("active Combat HUD host (%s): %s"),
                    Case.Name, *DescribeHudStartup(InWorld)), Hud)) { return; }
                const TSharedPtr<FCkCombatHudProofModel> Model = Hud->Get_Model();
                const TSharedPtr<FCkUiView> View = Hud->Get_AuthoredView();
                if (!TestTrue(TEXT("HUD model and authored view mounted"),
                    Model.IsValid() && View.IsValid())) { return; }
                TestTrue(TEXT("previous animated HUD releases on travel"),
                    !State->PreviousView.IsValid() || State->PreviousView->IsOwnerReleased());
                TestTrue(TEXT("previous HUD no longer active"),
                    !State->PreviousHud.IsValid() || !State->PreviousHud->IsActive());
                if (State->PreviousHud.IsValid())
                {
                    const FString OldSummary = State->PreviousHud->Get_Summary();
                    TestFalse(TEXT("travel-released animated HUD rejects further commands"),
                        State->PreviousHud->Request_Command(ECk_CombatHudProof_Command::Damage));
                    TestEqual(TEXT("released HUD keeps its final fixture state"),
                        State->PreviousHud->Get_Summary(), OldSummary);
                }
                TestTrue(TEXT("passive HUD preserves live unpaused viewport input"),
                    !InWorld->IsPaused() && IsValid(GetSwitchboard(GetPlayer(InWorld)))
                        && !GetSwitchboard(GetPlayer(InWorld))->Get_IsOpen()
                        && FocusViewport(InWorld));
                TestTrue(TEXT("replicas mount common aim and damage presentation"),
                    HasControl(Hud, TEXT("aim-reticle")) && HasControl(Hud, TEXT("damage-overlay")));
                if (Case.Theme == ECk_CombatHudProof_Theme::Futuristic)
                {
                    TestTrue(TEXT("Mass Effect has quickslots, curved vitals and squad portraits"),
                        HasControl(Hud, TEXT("quickslots")) && HasControl(Hud, TEXT("shield-meter"))
                            && HasControl(Hud, TEXT("squad-one-portrait")) && HasControl(Hud, TEXT("squad-two-portrait")));
                    TestFalse(TEXT("Mass Effect has no persistent stamina panel"), HasControl(Hud, TEXT("stamina-meter")));
                    const TSharedRef<SWidget> Root = View->GetRegion(TEXT("main"));
                    const auto Ui = FindNativeText(Root, FName{TEXT("objective-text")});
                    const auto Display = FindNativeText(Root, FName{TEXT("ammo-value")});
                    const auto DefaultFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12).GetCompositeFont();
                    TestTrue(TEXT("Mass Effect body and numeric labels use two admitted named fonts"),
                        Ui.IsValid() && Display.IsValid()
                            && Ui->GetFont().GetCompositeFont() != DefaultFont
                            && Display->GetFont().GetCompositeFont() != DefaultFont
                            && Ui->GetFont().GetCompositeFont() != Display->GetFont().GetCompositeFont());
                    bool HasAllQuickslotHints = true;
                    for (int32 Index = 0; Index < 8; ++Index)
                    {
                        const FName Hint(*FString::Printf(TEXT("quickslot-%d-key"), Index));
                        HasAllQuickslotHints &= FindTagged(Root, Hint).IsValid();
                    }
                    TestTrue(TEXT("Mass Effect mounts all eight shared native quickslot key hints"), HasAllQuickslotHints);
                    TestTrue(TEXT("Mass Effect mounts its bound full-frame backdrop"),
                        FindTagged(Root, FName{TEXT("replica-backdrop")}).IsValid());
                }
                else if (Case.Theme == ECk_CombatHudProof_Theme::Tactical)
                {
                    TestTrue(TEXT("Battlefield has Conquest map, tickets, squad and numeric health"),
                        HasControl(Hud, TEXT("minimap")) && HasControl(Hud, TEXT("tickets-friendly"))
                            && HasControl(Hud, TEXT("squad-list")) && HasControl(Hud, TEXT("health-value")));
                    TestFalse(TEXT("Battlefield has no generic vitals or ability meters"),
                        HasControl(Hud, TEXT("health-meter")) || HasControl(Hud, TEXT("cooldown-meter")));
                    const TSharedRef<SWidget> Root = View->GetRegion(TEXT("main"));
                    const auto Ui = FindNativeText(Root, FName{TEXT("squad-one")});
                    const auto Display = FindNativeText(Root, FName{TEXT("ammo-value")});
                    const auto DefaultFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12).GetCompositeFont();
                    TestTrue(TEXT("Battlefield body and numeric labels use two admitted named fonts"),
                        Ui.IsValid() && Display.IsValid()
                            && Ui->GetFont().GetCompositeFont() != DefaultFont
                            && Display->GetFont().GetCompositeFont() != DefaultFont
                            && Ui->GetFont().GetCompositeFont() != Display->GetFont().GetCompositeFont());
                    TestFalse(TEXT("Battlefield passive HUD leaves the gameplay scenery unobstructed"),
                        FindTagged(Root, FName{TEXT("replica-backdrop")}).IsValid());
                }
                else
                {
                    TestTrue(TEXT("Skyrim has compass, target, independent resources and sneak eye"),
                        HasControl(Hud, TEXT("compass-strip")) && HasControl(Hud, TEXT("target-name"))
                            && HasControl(Hud, TEXT("magicka-meter")) && HasControl(Hud, TEXT("stamina-meter"))
                            && HasControl(Hud, TEXT("sneak-eye")));
                    TestFalse(TEXT("Skyrim has no magazine/reserve weapon card"),
                        HasControl(Hud, TEXT("reserve-value")) || HasControl(Hud, TEXT("weapon-panel")));
                    const TSharedRef<SWidget> Root = View->GetRegion(TEXT("main"));
                    const auto Ui = FindNativeText(Root, FName{TEXT("compass-north")});
                    const auto Display = FindNativeText(Root, FName{TEXT("target-name")});
                    const auto DefaultFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12).GetCompositeFont();
                    TestTrue(TEXT("Skyrim compass and target use two admitted named fonts"),
                        Ui.IsValid() && Display.IsValid()
                            && Ui->GetFont().GetCompositeFont() != DefaultFont
                            && Display->GetFont().GetCompositeFont() != DefaultFont
                            && Ui->GetFont().GetCompositeFont() != Display->GetFont().GetCompositeFont());
                    const auto Keycap = FindTagged(Root, FName{TEXT("interaction-keycap")});
                    TestTrue(TEXT("Skyrim interaction mounts a passive native E key hint"),
                        Keycap.IsValid() && !Keycap->SupportsKeyboardFocus());
                    TestTrue(TEXT("Skyrim mounts its bound full-frame backdrop"),
                        FindTagged(Root, FName{TEXT("replica-backdrop")}).IsValid());
                }
                const auto Accepted = Hud->Get_State();
                const FString BeforeInvalid = Hud->Get_Summary();
                TestFalse(TEXT("invalid command rejects without mutation"),
                    Model->Request_Command(static_cast<ECk_CombatHudProof_Command>(255)));
                TestFalse(TEXT("NaN time step rejects without mutation"),
                    Model->Advance(std::numeric_limits<float>::quiet_NaN()));
                TestTrue(TEXT("invalid inputs preserve mounted fixture state"),
                    Hud->Get_Summary() == BeforeInvalid
                        && Hud->Get_State().Get_Health() == Accepted.Get_Health()
                        && Hud->Get_State().Get_Ammo() == Accepted.Get_Ammo());
                FString Error;
                TSharedPtr<FCkCombatHudProofModel> Output = Model;
                TestFalse(TEXT("invalid theme rejects before atlas load"),
                    FCkCombatHudProofModel::TryCreate(static_cast<ECk_CombatHudProof_Theme>(255),
                        FString{}, Output, Error));
                TestTrue(TEXT("invalid theme retains the output and reports failure"),
                    Output == Model && !Error.IsEmpty());
                Error.Reset();
                TestFalse(TEXT("missing source resources rejects without publishing a candidate"),
                    FCkCombatHudProofModel::TryCreate(Case.Theme, FString{}, Output, Error));
                TestTrue(TEXT("missing source resources preserves the accepted model"),
                    Output == Model && !Error.IsEmpty());
                FString Markup;
                FString Style;
                const FString Root = SourceRoot(Case);
                if (!TestTrue(TEXT("installed theme source pair loads"),
                    FFileHelper::LoadFileToString(Markup, *FPaths::Combine(Root, TEXT("CombatHudProof.ui.html")))
                        && FFileHelper::LoadFileToString(Style,
                            *FPaths::Combine(Root, TEXT("CombatHudProof.ui.css"))))) { return; }
                if (Case.Theme == ECk_CombatHudProof_Theme::Futuristic)
                {
                    const FString OversizedStyle = Style.Replace(
                        TEXT(".ammo { font-family: me-display; height: 68px; font-size: 36;"),
                        TEXT(".ammo { font-family: me-display; height: 40px; font-size: 64;"));
                    if (!TestTrue(TEXT("regression source creates an oversized ammo line"),
                        OversizedStyle != Style)) { return; }
                    if (!TestTrue(TEXT("former ammo typography loads through the live source pipeline"),
                        Hud->Request_ApplySource(Markup, OversizedStyle))) { return; }
                    Tick(FSlateApplication::Get());
                    const auto Oversized = MeasureHudText(Hud, FName{TEXT("ammo-value")});
                    TestTrue(TEXT("former ammo typography has valid measurable native geometry"), Oversized.IsSet());
                    if (Oversized.IsSet())
                    {
                        AddInfo(Oversized->Describe());
                        TestFalse(TEXT("oversized 64-point ammo line fits its 40-pixel slot"), Oversized->Fits());
                    }
                    if (!TestTrue(TEXT("corrected authored typography restores after regression reproduction"),
                        Hud->Request_ApplySource(Markup, Style))) { return; }
                    Tick(FSlateApplication::Get());
                }
                TestTrue(TEXT("initial Combat HUD 1920x1080 capture"),
                    Capture(InWorld, *State, FString{Case.Name} + TEXT("-replica-initial-1920x1080"),
                        FIntPoint{1920, 1080}));
                TestTrue(TEXT("initial Combat HUD 1280x720 capture"),
                    Capture(InWorld, *State, FString{Case.Name} + TEXT("-replica-initial-1280x720"),
                        FIntPoint{1280, 720}));
                TestTrue(TEXT("initial Combat HUD 2560x1080 capture"),
                    Capture(InWorld, *State, FString{Case.Name} + TEXT("-replica-initial-2560x1080"),
                        FIntPoint{2560, 1080}));
                if (Case.Theme != ECk_CombatHudProof_Theme::Tactical)
                {
                    const auto AuthoredView = Hud->Get_AuthoredView();
                    const auto Backdrop = AuthoredView.IsValid() ? FindTagged(AuthoredView->GetRegion(TEXT("main")),
                        FName{TEXT("replica-backdrop")}) : nullptr;
                    TestTrue(TEXT("replica backdrop occupies its full authored frame"),
                        Backdrop.IsValid()
                            && Backdrop->GetCachedGeometry().GetLocalSize().X >= 1919.0
                            && Backdrop->GetCachedGeometry().GetLocalSize().Y >= 1079.0);
                }
                State->RestoreWindow();
                const FString ChangedStyle = Case.Theme == ECk_CombatHudProof_Theme::Fantasy
                    ? Style.Replace(TEXT("font-size: 17px;"), TEXT("font-size: 18px;"))
                    : Style.Replace(TEXT("font-size: 18;"), TEXT("font-size: 19;"));
                if (!TestTrue(TEXT("valid reload candidate changes authored text style"),
                    ChangedStyle != Style)) { return; }
                const TSharedPtr<SCkCombatHudReplicaArt> RetainedArt = FindNativeArt(View->GetRegion(TEXT("main")),
                    FName{TEXT("aim-reticle")});
                if (!TestTrue(TEXT("live HUD exposes its native reticle artwork"), RetainedArt.IsValid())) { return; }
                const int64 InitialRevision = View->GetRevision();
                TestTrue(TEXT("accepted HUD style reload"), Hud->Request_ApplySource(Markup, ChangedStyle));
                TestTrue(TEXT("HUD reload retains model and native artwork identity"),
                    Hud->Get_Model() == Model && Hud->Get_AuthoredView() == View
                        && View->GetRevision() > InitialRevision
                        && FindNativeArt(View->GetRegion(TEXT("main")), FName{TEXT("aim-reticle")}) == RetainedArt);
                const TSharedPtr<SWidget> AcceptedArtPort = FindTagged(View->GetRegion(TEXT("main")),
                    FName{TEXT("aim-reticle")});
                const int64 AcceptedRevision = View->GetRevision();
                if (Case.Theme == ECk_CombatHudProof_Theme::Tactical)
                {
                    const FString MissingHostile = Markup.Replace(TEXT("id=\"objective-icon-hostile\""),
                        TEXT("id=\"removed-hostile\""));
                    const FString DuplicateOwner = Markup.Replace(TEXT("visible=\"ObjectiveHostile\""),
                        TEXT("visible=\"ObjectiveFriendly\""));
                    const FString WrongColor = Markup.Replace(TEXT("piece=\"bf-objective\" variant=\"1\""),
                        TEXT("piece=\"bf-objective\" variant=\"0\""));
                    TestTrue(TEXT("objective rejection fixtures alter the installed source"),
                        MissingHostile != Markup && DuplicateOwner != Markup && WrongColor != Markup);
                    for (const auto& Candidate : {MissingHostile, DuplicateOwner, WrongColor})
                    {
                        TestFalse(TEXT("incomplete or contradictory ownership reload rejects"),
                            Hud->Request_ApplySource(Candidate, Style));
                        TestTrue(TEXT("rejected ownership reload preserves accepted view and revision"),
                            View->GetRevision() == AcceptedRevision && Hud->Get_Model() == Model
                                && FindTagged(View->GetRegion(TEXT("main")), FName{TEXT("aim-reticle")}) == AcceptedArtPort);
                    }
                }
                const FString MissingReticle = Markup.Replace(TEXT("id=\"aim-reticle\""), TEXT("id=\"removed-reticle\""));
                TestFalse(TEXT("valid XML missing a required replica component rejects atomically"),
                    Hud->Request_ApplySource(MissingReticle, Style));
                TestTrue(TEXT("missing component preserves accepted artwork, model and revision"),
                    View->GetRevision() == AcceptedRevision && Hud->Get_Model() == Model
                        && FindNativeArt(View->GetRegion(TEXT("main")), FName{TEXT("aim-reticle")}) == RetainedArt);
                TestFalse(TEXT("malformed HUD source rejects atomically"),
                    Hud->Request_ApplySource(TEXT("<ui version=\"1\"><region name=\"main\"><overlay"), Style));
                TestTrue(TEXT("rejected HUD source preserves accepted artwork and revision"),
                    Hud->Get_AuthoredView() == View && View->GetRevision() == AcceptedRevision
                        && AcceptedArtPort.IsValid()
                        && FindTagged(View->GetRegion(TEXT("main")), FName{TEXT("aim-reticle")}) == AcceptedArtPort
                        && FindNativeArt(View->GetRegion(TEXT("main")), FName{TEXT("aim-reticle")}) == RetainedArt
                        && !Hud->Get_SourceError().IsEmpty());
                TestTrue(TEXT("installed HUD source recovers after rejection"),
                    Hud->Request_ApplySource(Markup, Style));
                PressKey(EKeys::One);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([Case]() -> bool
            {
                UCk_CombatHudProof_UE* Hud = GetHud(ck::auto_test::net::Get_ServerWorld());
                return IsValid(Hud) && Hud->Get_Theme() == Case.Theme
                    && (Hud->Get_State().Get_Health() < 100.0f
                        || Hud->Get_State().Get_Shield() < 100.0f);
            }), 5.0, TEXT("routed damage changes HUD vitals")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State, Case](UWorld* InWorld) -> void
            {
                UCk_CombatHudProof_UE* Hud = GetHud(InWorld);
                if (!TestNotNull(TEXT("damage HUD remains active"), Hud)) { return; }
                TestTrue(TEXT("damage overlay is authored"), HasControl(Hud, TEXT("damage-overlay")));
                PressKey(EKeys::Two);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                UCk_CombatHudProof_UE* Hud = GetHud(ck::auto_test::net::Get_ServerWorld());
                return IsValid(Hud) && Hud->Get_State().Get_Sprinting()
                    && Hud->Get_State().Get_Stamina() < 100.0f;
            }), 5.0, TEXT("world ticking drains sprint stamina")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
            {
                UCk_CombatHudProof_UE* Hud = GetHud(InWorld);
                if (!TestNotNull(TEXT("sprinting HUD remains active"), Hud)) { return; }
                PressKey(EKeys::Three);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([Case]() -> bool
            {
                UCk_CombatHudProof_UE* Hud = GetHud(ck::auto_test::net::Get_ServerWorld());
                const int32 InitialAmmo = Case.Theme == ECk_CombatHudProof_Theme::Futuristic ? 50
                    : Case.Theme == ECk_CombatHudProof_Theme::Tactical ? 31 : 18;
                return IsValid(Hud) && Hud->Get_State().Get_Ammo() == InitialAmmo - 1;
            }), 5.0, TEXT("routed fire decrements ammo exactly once")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State, Case](UWorld* InWorld) -> void
            {
                UCk_CombatHudProof_UE* Hud = GetHud(InWorld);
                if (!TestNotNull(TEXT("firing HUD remains active"), Hud)) { return; }
                PressKey(EKeys::Four);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                UCk_CombatHudProof_UE* Hud = GetHud(ck::auto_test::net::Get_ServerWorld());
                return IsValid(Hud) && Hud->Get_State().Get_Reloading()
                    && Hud->Get_State().Get_ReloadProgress() > 0.0f;
            }), 5.0, TEXT("reload animates from live world ticks")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State, Case](UWorld* InWorld) -> void
            {
                UCk_CombatHudProof_UE* Hud = GetHud(InWorld);
                if (!TestNotNull(TEXT("animating HUD remains active"), Hud)) { return; }
                const int32 AmmoBefore = Hud->Get_State().Get_Ammo();
                TestFalse(TEXT("fire rejects during reload"),
                    Hud->Request_Command(ECk_CombatHudProof_Command::Fire));
                TestEqual(TEXT("rejected fire preserves ammo"), Hud->Get_State().Get_Ammo(), AmmoBefore);
                TestTrue(TEXT("animated reload native capture"),
                    Capture(InWorld, *State, FString{Case.Name} + TEXT("-replica-reloading-1920x1080"),
                        FIntPoint{1920, 1080}));
                PressKey(EKeys::Five);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                UCk_CombatHudProof_UE* Hud = GetHud(ck::auto_test::net::Get_ServerWorld());
                return IsValid(Hud) && Hud->Get_State().Get_CooldownProgress() > 0.0f
                    && Hud->Get_State().Get_Magicka() < 100.0f;
            }), 5.0, TEXT("routed ability begins cooldown and spends magicka")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State, Case](UWorld* InWorld) -> void
            {
                TestTrue(TEXT("game-specific ability native capture"),
                    Capture(InWorld, *State, FString{Case.Name} + TEXT("-replica-ability-1920x1080"), FIntPoint{1920, 1080}));
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                UCk_CombatHudProof_UE* Hud = GetHud(ck::auto_test::net::Get_ServerWorld());
                return IsValid(Hud) && !Hud->Get_State().Get_Reloading()
                    && Hud->Get_State().Get_ReloadProgress() == 0.0f;
            }), 5.0, TEXT("world ticker completes reload")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State, Case](UWorld* InWorld) -> void
            {
                UCk_CombatHudProof_UE* Hud = GetHud(InWorld);
                if (!TestNotNull(TEXT("reloaded HUD remains active"), Hud)) { return; }
                TestTrue(TEXT("reload transfers reserve into magazine"),
                    Hud->Get_State().Get_Ammo() > 0 && Hud->Get_State().Get_Reserve() > 0);
                PressKey(EKeys::Eight);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                UCk_CombatHudProof_UE* Hud = GetHud(ck::auto_test::net::Get_ServerWorld());
                return IsValid(Hud) && Hud->Get_State().Get_Health() == 20.0f
                    && Hud->Get_Summary() == TEXT("LOW HEALTH");
            }), 5.0, TEXT("routed low-health state reaches authored HUD")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State, Case](UWorld* InWorld) -> void
            {
                UCk_CombatHudProof_UE* Hud = GetHud(InWorld);
                if (!TestNotNull(TEXT("low-health HUD remains active"), Hud)) { return; }
                TestTrue(TEXT("low-health native capture"),
                    Capture(InWorld, *State, FString{Case.Name} + TEXT("-replica-low-health-1920x1080"),
                        FIntPoint{1920, 1080}));
                PressKey(EKeys::Six);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                UCk_CombatHudProof_UE* Hud = GetHud(ck::auto_test::net::Get_ServerWorld());
                return IsValid(Hud) && Hud->Get_State().Get_Health() > 20.0f;
            }), 5.0, TEXT("routed recovery updates health")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
            {
                UCk_CombatHudProof_UE* Hud = GetHud(InWorld);
                if (!TestNotNull(TEXT("recovered HUD remains active"), Hud)) { return; }
                PressKey(EKeys::Seven);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([Case]() -> bool
            {
                UCk_CombatHudProof_UE* Hud = GetHud(ck::auto_test::net::Get_ServerWorld());
                const int32 InitialAmmo = Case.Theme == ECk_CombatHudProof_Theme::Futuristic ? 50
                    : Case.Theme == ECk_CombatHudProof_Theme::Tactical ? 31 : 18;
                return IsValid(Hud) && Hud->Get_State().Get_Health() == 100.0f
                    && Hud->Get_State().Get_Ammo() == InitialAmmo
                    && !Hud->Get_State().Get_Reloading();
            }), 5.0, TEXT("routed reset restores the theme fixture")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State, Case](UWorld* InWorld) -> void
            {
                UCk_CombatHudProof_UE* Hud = GetHud(InWorld);
                if (!TestNotNull(TEXT("reset HUD remains active"), Hud)) { return; }
                PressKey(EKeys::Nine);
                State->ContextReadyAt = FPlatformTime::Seconds() + 0.25;
            })));
        for (int32 ContextStep = 1; ContextStep <= 2; ++ContextStep)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
                FCk_NetAutoTest_Condition::CreateLambda([State, Case, ContextStep]() -> bool
                {
                    return FPlatformTime::Seconds() >= State->ContextReadyAt
                        && ContextDisplayed(GetHud(ck::auto_test::net::Get_ServerWorld()), Case.Theme, ContextStep);
                }), 5.0, TEXT("routed context reaches the displayed native widget tree")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([this, State, Case, ContextStep](UWorld* InWorld) -> void
                {
                    UCk_CombatHudProof_UE* Hud = GetHud(InWorld);
                    if (!TestNotNull(TEXT("context HUD remains active"), Hud)) { return; }
                    TestTrue(TEXT("game-specific context visibility is displayed"), ContextDisplayed(Hud, Case.Theme, ContextStep));
                    TestTrue(TEXT("game-specific context native capture"), Capture(InWorld, *State, FString{Case.Name}
                        + (ContextStep == 1 ? TEXT("-replica-context-one-1920x1080") : TEXT("-replica-context-two-1920x1080")), FIntPoint{1920, 1080}));
                    PressKey(EKeys::Nine);
                    State->ContextReadyAt = FPlatformTime::Seconds() + 0.25;
                    if (ContextStep == 2)
                    {
                        PressKey(EKeys::Five);
                        State->PreviousHud = Hud;
                        State->PreviousView = Hud->Get_AuthoredView();
                    }
                })));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                UCk_CombatHudProof_UE* Hud = GetHud(ck::auto_test::net::Get_ServerWorld());
                return IsValid(Hud) && Hud->Get_State().Get_CooldownProgress() > 0.0f;
            }), 5.0, TEXT("HUD is animated before travel or PIE teardown")));
    }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*) -> void
        { TestTrue(TEXT("capture restores debug overlay values and flags before PIE teardown"), State->RestoreDebug()); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            State->RestoreWindow();
            State->RestoreSettings();
            return State->bSettingsRestored && State->bDebugRestored
                && (!State->PreviousView.IsValid() || State->PreviousView->IsOwnerReleased())
                && (!State->PreviousHud.IsValid() || (!State->PreviousHud->IsActive()
                    && !State->PreviousHud->Request_Command(ECk_CombatHudProof_Command::Damage)));
        }), TEXT("Combat HUD teardown releases its view and restores gym startup preferences")));
    return true;
}

#endif
