#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/UI/CkPowerWheelProof.h"
#include "CkSlateLayout/CkUiRadialSelector.h"
#include "CkSlateLayout/CkFlexText.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "UI/CkPowerWheelProofModel.h"

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

namespace ck_power_wheel_pie
{
    struct FState
    {
        ~FState() { Restore(); }
        auto HideDebug(UWorld* World) -> bool
        {
            auto* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
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
            if (!bDebugCaptured) { return true; }
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
            if (Restored) { bDebugCaptured = false; }
            return Restored;
        }
        void Restore()
        {
            RestoreDebug();
            if (const auto Window = WindowToRestore.Pin(); Window.IsValid())
            { Window->Resize(OriginalWindowSize); }
            WindowToRestore.Reset();
            if (bSettingsCaptured)
            {
                auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
                Settings->StartupMode = StartupMode;
                Settings->ControlPanelMode = PanelMode;
                Settings->DefaultGymName = DefaultGymName;
                Settings->LastGymName = LastGymName;
                Settings->RecentGymNames = RecentGymNames;
                Settings->SaveConfig();
                bSettingsCaptured = false;
            }
        }
        ECkGym_StartupMode StartupMode = ECkGym_StartupMode::Cycler;
        ECkGym_ControlPanel_Mode PanelMode = ECkGym_ControlPanel_Mode::Full;
        FString DefaultGymName;
        FString LastGymName;
        TArray<FString> RecentGymNames;
        bool bSettingsCaptured = false;
        TWeakPtr<SWindow> WindowToRestore;
        FVector2D OriginalWindowSize = FVector2D::ZeroVector;
        TWeakObjectPtr<UCk_PowerWheelProof_UE> Host;
        TSharedPtr<FCkUiView> HeldView;
        TSharedPtr<SWidget> HeldNative;
        int32 DispatchBeforeCancel = 0;
        bool bOriginalCursor = false;
        TWeakPtr<SWidget> OriginalFocus;
        FAutomationTestBase* Test = nullptr;
        TMap<FString, FString> DebugValues;
        TMap<FString, EConsoleVariableFlags> DebugFlags;
        TWeakObjectPtr<UCkGym_Registry_Subsystem> DebugRegistry;
        bool bOriginalSuppression = false;
        bool bDebugCaptured = false;
    };

    auto Player(UWorld* World) -> APlayerController*
    { return IsValid(World) ? World->GetFirstPlayerController() : nullptr; }

    auto Registry(UWorld* World) -> UCkGym_Registry_Subsystem*
    {
        auto* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    auto Host(UWorld* World) -> UCk_PowerWheelProof_UE*
    {
        auto* Controller = Player(World);
        if (!IsValid(Controller)) { return nullptr; }
        UFunction* Getter = Controller->FindFunction(TEXT("Get_PowerWheel"));
        const auto* Return = Getter != nullptr ? CastField<FObjectProperty>(Getter->GetReturnProperty()) : nullptr;
        if (Return == nullptr) { return nullptr; }
        TArray<uint8> Params;
        Params.SetNumZeroed(Getter->ParmsSize);
        Getter->InitializeStruct(Params.GetData());
        Controller->ProcessEvent(Getter, Params.GetData());
        auto* Result = Cast<UCk_PowerWheelProof_UE>(Return->GetObjectPropertyValue_InContainer(Params.GetData()));
        Getter->DestroyStruct(Params.GetData());
        return IsValid(Result) ? Result : nullptr;
    }

    auto Find(const TSharedRef<SWidget>& Root, FName Tag) -> TSharedPtr<SWidget>
    {
        if (Root->GetTag() == Tag && Root->GetTypeAsString() == TEXT("SCkUiRadialSelector")) { return Root; }
        auto* Children = Root->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        { if (auto Found = Find(Children->GetChildAt(Index), Tag)) { return Found; } }
        return {};
    }

    auto FindTagged(const TSharedRef<SWidget>& Root, FName Tag) -> TSharedPtr<SWidget>
    {
        if (Root->GetTag() == Tag) { return Root; }
        const FChildren* Children = Root->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        { if (const auto Found = FindTagged(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), Tag); Found.IsValid()) { return Found; } }
        return nullptr;
    }

    auto Native(UCk_PowerWheelProof_UE* Wheel) -> TSharedPtr<SCkUiRadialSelector>
    {
        const auto View = IsValid(Wheel) ? Wheel->Get_AuthoredView() : nullptr;
        if (!View.IsValid()) { return {}; }
        const auto Found = Find(View->GetRegion(TEXT("main")), TEXT("power-wheel"));
        return Found.IsValid() && Found->GetTypeAsString() == TEXT("SCkUiRadialSelector")
            ? StaticCastSharedPtr<SCkUiRadialSelector>(Found) : nullptr;
    }

    auto NativeText(const TSharedRef<SWidget>& Root, FName Tag) -> TSharedPtr<SCkFlexText>
    {
        if (Root->GetTag() == Tag && Root->GetTypeAsString() == TEXT("SCkFlexText"))
        { return StaticCastSharedRef<SCkFlexText>(Root); }
        auto* Children = Root->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        { if (auto Found = NativeText(Children->GetChildAt(Index), Tag)) { return Found; } }
        return {};
    }

    auto TextFits(UCk_PowerWheelProof_UE* Wheel, FName Tag) -> bool
    {
        const auto View = IsValid(Wheel) ? Wheel->Get_AuthoredView() : nullptr;
        const auto Text = View.IsValid() ? NativeText(View->GetRegion(TEXT("main")), Tag) : nullptr;
        if (!Text.IsValid() || Text->GetText().IsEmpty()) { return false; }
        const FGeometry& Geometry = Text->GetCachedGeometry();
        const FVector2D Space = Geometry.GetLocalSize();
        if (!FMath::IsFinite(Geometry.Scale) || Geometry.Scale <= 0.0f
            || !FMath::IsFinite(Space.X) || Space.X <= 0.0
            || !FMath::IsFinite(Space.Y) || Space.Y <= 0.0) { return false; }
        const FVector2D Needed = Text->Measure(static_cast<float>(Space.X), YGMeasureModeExactly,
            YGUndefined, YGMeasureModeUndefined, Geometry.Scale);
        return FMath::IsFinite(Needed.Y) && Needed.Y <= Space.Y + 0.5;
    }

    void Pump()
    { auto& Slate = FSlateApplication::Get(); Slate.PumpMessages(); Slate.Tick(); Slate.Tick(); }

    auto Viewport(UWorld* World) -> TSharedPtr<SViewport>
    {
        auto* Client = IsValid(World) ? World->GetGameViewport() : nullptr;
        return IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
    }

    auto FocusViewport(UWorld* World, int32 User) -> bool
    {
        const auto Target = Viewport(World);
        if (!Target.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Target.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
        Window->BringToFront(true);
        Slate.SetUserFocus(User, Target.ToSharedRef(), EFocusCause::SetDirectly);
        Pump();
        return Slate.IsActive() && Window->IsActive() && Slate.GetUserFocusedWidget(User).Get() == Target.Get();
    }

    auto Key(const FKey& Which, int32 User, bool Down) -> bool
    {
        auto& Slate = FSlateApplication::Get();
        return Down ? Slate.ProcessKeyDownEvent(FKeyEvent{Which, FModifierKeysState{}, static_cast<uint32>(User), false, 0, 0})
            : Slate.ProcessKeyUpEvent(FKeyEvent{Which, FModifierKeysState{}, static_cast<uint32>(User), false, 0, 0});
    }

    auto Hover(UCk_PowerWheelProof_UE* Wheel, FName KeyName) -> bool
    {
        const auto Widget = Native(Wheel);
        if (!Widget.IsValid() || !Wheel->Get_Model().IsValid()) { return false; }
        const auto* Power = Wheel->Get_Model()->Get_Powers().FindByPredicate(
            [KeyName](const FCkPowerWheelPower& Item) { return Item.Key == KeyName; });
        if (Power == nullptr) { return false; }
        const FGeometry Geometry = Widget->GetCachedGeometry();
        const FVector2D Size = Geometry.GetLocalSize();
        if (Size.X <= 0.0 || Size.Y <= 0.0) { return false; }
        const double Radius = 0.39 * FMath::Min(Size.X, Size.Y);
        const double Angle = FMath::DegreesToRadians(static_cast<double>(Power->Angle));
        const FVector2D Local{Size.X * 0.5 + Radius * FMath::Cos(Angle),
            Size.Y * 0.5 + Radius * FMath::Sin(Angle)};
        auto& Slate = FSlateApplication::Get();
        const FVector2D Position = Geometry.LocalToAbsolute(Local);
        Slate.SetCursorPos(Position);
        const FPointerEvent Event{static_cast<uint32>(Wheel->Get_SlateUserIndex()), FSlateApplication::CursorPointerIndex,
            Position, Position, TSet<FKey>{}, EKeys::Invalid, 0.0f, FModifierKeysState{}};
        Slate.ProcessMouseMoveEvent(Event, true);
        Pump();
        return Wheel->Get_Model()->Get_SelectedKey() == KeyName;
    }

    auto Capture(UWorld* World, FState& State, const FString& Name, FIntPoint Desired) -> bool
    {
        const auto Target = Viewport(World);
        if (!Target.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Target.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
        if (!State.WindowToRestore.IsValid())
        { State.WindowToRestore = Window; State.OriginalWindowSize = Window->GetClientSizeInScreen(); }
        TArray<FColor> Pixels;
        FIntVector Size = FIntVector::ZeroValue;
        for (int32 Attempt = 0; Attempt < 5; ++Attempt)
        {
            Pump();
            Pixels.Reset(); Size = FIntVector::ZeroValue;
            if (!Slate.TakeScreenshot(Target.ToSharedRef(), Pixels, Size)
                || Size.X <= 0 || Size.Y <= 0 || Pixels.Num() < Size.X * Size.Y) { return false; }
            if (Size.X == Desired.X && Size.Y == Desired.Y) { break; }
            Window->Resize(Window->GetClientSizeInScreen() + FVector2D(
                static_cast<double>(Desired.X - Size.X), static_cast<double>(Desired.Y - Size.Y)));
        }
        if (Size.X != Desired.X || Size.Y != Desired.Y) { return false; }
        auto* Wheel = Host(World);
        if (!IsValid(Wheel) || State.Test == nullptr) { return false; }
        bool Fits = true;
        if (Wheel->IsWheelOpen())
        {
            for (const TCHAR* Id : {TEXT("power-name"), TEXT("power-description"), TEXT("power-state"),
                TEXT("queue-garrus"), TEXT("queue-liara"), TEXT("queue-shepard")})
            {
                const bool ThisFits = TextFits(Wheel, FName{Id});
                State.Test->TestTrue(*FString::Printf(TEXT("%s native %s text fits"), *Name, Id), ThisFits);
                Fits &= ThisFits;
            }
        }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/PowerWheelProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, Name + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8)) && Fits;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_PowerWheelProof_PIE, "Ck.UiAuthoring.PowerWheel.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FCk_PowerWheelProof_PIE::RunTest(const FString&)
{
    using namespace ck_power_wheel_pie;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Power wheel requires Slate.")); return false; }
    const TSharedRef<FState> State = MakeShared<FState>();
    State->Test = this;
    auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
    if (!TestNotNull(TEXT("gym startup settings"), Settings)) { return false; }
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
        { auto* R = Registry(ck::auto_test::net::Get_ServerWorld());
            return IsValid(R) && R->Find_GymIndexByName(TEXT("UI: Mass Effect 3 Power Wheel")) != INDEX_NONE; }),
        15.0, TEXT("Power Wheel gym registers")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
        {
            auto* R = Registry(World);
            if (!TestNotNull(TEXT("gym registry"), R)) { return; }
            const int32 Index = R->Find_GymIndexByName(TEXT("UI: Mass Effect 3 Power Wheel"));
            if (!TestTrue(TEXT("Power Wheel gym entry exists"), R->Get_Entries().IsValidIndex(Index))) { return; }
            const auto& Entry = R->Get_Entries()[Index];
            TestEqual(TEXT("Power Wheel is a CkUI gym"), Entry.Category, FString{TEXT("CkUI")});
            if (TestTrue(TEXT("entry uses authored wheel GameMode"), IsValid(Entry.GameModeClass.Get())
                && Entry.GameModeClass.Get()->GetName().Contains(TEXT("Ck_PowerWheel_GameMode"))))
            { R->Request_TravelToGym(Index); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            auto* Mode = IsValid(World) ? World->GetAuthGameMode() : nullptr;
            auto* Controller = Player(World);
            return IsValid(Mode) && Mode->GetClass()->GetName().Contains(TEXT("Ck_PowerWheel_GameMode"))
                && IsValid(Controller) && Controller->GetClass()->GetName().Contains(TEXT("Ck_PowerWheel_PlayerController"))
                && IsValid(Host(World));
        }), 30.0, TEXT("registry travel creates production Power Wheel host")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Wheel = Host(World);
            auto* Controller = Player(World);
            if (!TestNotNull(TEXT("script controller owns wheel host"), Wheel)
                || !TestNotNull(TEXT("local controller"), Controller)) { return; }
            State->Host = Wheel;
            State->bOriginalCursor = Controller->bShowMouseCursor;
            if (!TestTrue(TEXT("capture temporarily hides debug overlays"), State->HideDebug(World))) { return; }
            TestFalse(TEXT("gym starts unpaused"), World->IsPaused());
            if (!TestTrue(TEXT("viewport accepts real input"), FocusViewport(World, Wheel->Get_SlateUserIndex()))) { return; }
            State->OriginalFocus = FSlateApplication::Get().GetUserFocusedWidget(Wheel->Get_SlateUserIndex());
            TestTrue(TEXT("real Shift down opens through input processor"), Key(EKeys::LeftShift, Wheel->Get_SlateUserIndex(), true));
            Pump();
            const auto* Client = World->GetGameViewport();
            const bool NativeViewportFocused = IsValid(Client) && Client->Viewport != nullptr
                && Client->Viewport->HasFocus();
            TestTrue(*FString::Printf(TEXT("wheel open pauses world (source='%s', summary='%s', "
                "native-viewport-focus=%s, view=%s)"), *Wheel->Get_SourceError(), *Wheel->Get_Summary(),
                NativeViewportFocused ? TEXT("true") : TEXT("false"),
                Wheel->Get_AuthoredView().IsValid() ? TEXT("present") : TEXT("absent")),
                Wheel->IsWheelOpen() && World->IsPaused());
            TestTrue(TEXT("authored wheel native root appears"), Native(Wheel).IsValid());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Wheel = Host(ck::auto_test::net::Get_ServerWorld());
            return IsValid(Wheel) && Wheel->IsWheelOpen() && Wheel->GetRenderOpacity() >= 0.99f
                && Native(Wheel).IsValid() && Native(Wheel)->GetCachedGeometry().GetLocalSize().X > 300.0;
        }), 5.0, TEXT("wheel fade completes with native geometry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Wheel = Host(World);
            auto* Controller = Player(World);
            if (!TestNotNull(TEXT("wheel remains active after fade"), Wheel)
                || !TestNotNull(TEXT("wheel controller remains valid"), Controller)) { return; }
            if (!TestTrue(TEXT("wheel still open after fade"), Wheel->IsWheelOpen())
                || !TestTrue(TEXT("wheel model remains valid"), Wheel->Get_Model().IsValid())
                || !TestTrue(TEXT("wheel view remains valid"), Wheel->Get_AuthoredView().IsValid())
                || !TestTrue(TEXT("wheel native root remains valid"), Native(Wheel).IsValid())) { return; }
            State->HeldView = Wheel->Get_AuthoredView();
            State->HeldNative = Native(Wheel);
            TestTrue(TEXT("native wheel owns local-user focus after CommonUI activation"),
                FSlateApplication::Get().GetUserFocusedWidget(Wheel->Get_SlateUserIndex()) == State->HeldNative);
            const TSharedRef<SWidget> RegionRoot = State->HeldView->GetRegion(TEXT("main"));
            const auto Ui = NativeText(RegionRoot, FName{TEXT("power-description")});
            const auto Display = NativeText(RegionRoot, FName{TEXT("power-name")});
            const auto DefaultFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12).GetCompositeFont();
            TestTrue(TEXT("Power wheel body and power name use two admitted named fonts"),
                Ui.IsValid() && Display.IsValid()
                    && Ui->GetFont().GetCompositeFont() != DefaultFont
                    && Display->GetFont().GetCompositeFont() != DefaultFont
                    && Ui->GetFont().GetCompositeFont() != Display->GetFont().GetCompositeFont());
            bool HasAllHints = true;
            for (const TCHAR* Id : {TEXT("input-label-1"), TEXT("input-label-2"), TEXT("input-label-3"),
                TEXT("center-click-key"), TEXT("center-enter-key")})
            {
                const auto Hint = FindTagged(RegionRoot, FName{Id});
                HasAllHints &= Hint.IsValid() && !Hint->SupportsKeyboardFocus();
            }
            TestTrue(TEXT("Power wheel mounts five passive shared native key hints"), HasAllHints);
            TestTrue(TEXT("Power wheel mounts its bound full-frame backdrop"),
                FindTagged(RegionRoot, FName{TEXT("replica-backdrop")}).IsValid());
            struct FViewportCase { const TCHAR* Name; FIntPoint Size; };
            const FViewportCase Viewports[] = {
                {TEXT("initial-1280x720"), FIntPoint{1280, 720}},
                {TEXT("initial-1920x1080"), FIntPoint{1920, 1080}},
                {TEXT("initial-2560x1080"), FIntPoint{2560, 1080}}};
            for (const FViewportCase& Case : Viewports)
            { TestTrue(*FString::Printf(TEXT("native %s capture"), Case.Name),
                Capture(World, *State, Case.Name, Case.Size)); }
            const auto Backdrop = FindTagged(RegionRoot, FName{TEXT("replica-backdrop")});
            TestTrue(TEXT("power wheel backdrop occupies its full authored frame"),
                Backdrop.IsValid()
                    && Backdrop->GetCachedGeometry().GetLocalSize().X >= 1919.0
                    && Backdrop->GetCachedGeometry().GetLocalSize().Y >= 1079.0);
            for (const TCHAR* Power : {TEXT("shepard-warp"), TEXT("garrus-overload"), TEXT("liara-warp")})
            {
                TestTrue(*FString::Printf(TEXT("real hover selects %s"), Power), Hover(Wheel, FName{Power}));
                TestTrue(*FString::Printf(TEXT("Enter queues %s"), Power), Key(EKeys::Enter, Wheel->Get_SlateUserIndex(), true));
                Key(EKeys::Enter, Wheel->Get_SlateUserIndex(), false);
            }
            TestTrue(TEXT("three actors queued independently"), Wheel->Get_Model()->Get_QueuedKey(0) == FName{TEXT("shepard-warp")}
                && Wheel->Get_Model()->Get_QueuedKey(1) == FName{TEXT("garrus-overload")}
                && Wheel->Get_Model()->Get_QueuedKey(2) == FName{TEXT("liara-warp")});
            TestTrue(TEXT("queued-three native capture"), Capture(World, *State, TEXT("queued-three-1920x1080"), {1920, 1080}));
            const auto Before = Wheel->Get_AuthoredView();
            const auto Root = Native(Wheel);
            if (!TestTrue(TEXT("accepted view and native root survive input"), Before.IsValid() && Root.IsValid())) { return; }
            const int64 Revision = Before->GetRevision();
            auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
            FString Markup, Style;
            if (TestTrue(TEXT("wheel source installed"), Plugin.IsValid()
                && FFileHelper::LoadFileToString(Markup, *FPaths::Combine(Plugin->GetBaseDir(),
                    TEXT("Resources/UiAuthoring/PowerWheelProof/PowerWheelProof.ui.html")))
                && FFileHelper::LoadFileToString(Style, *FPaths::Combine(Plugin->GetBaseDir(),
                    TEXT("Resources/UiAuthoring/PowerWheelProof/PowerWheelProof.ui.css")))))
            {
                TestFalse(TEXT("missing required wheel source rejects"), Wheel->Request_ApplySource(
                    Markup.Replace(TEXT("id=\"power-wheel\""), TEXT("id=\"other-wheel\"")), Style));
                TestTrue(TEXT("rejection preserves queued model/native/revision"), Wheel->Get_AuthoredView() == Before
                    && Before->GetRevision() == Revision && Native(Wheel) == Root
                    && Wheel->Get_Model()->Get_QueuedKey(2) == FName{TEXT("liara-warp")}
                    && !Wheel->Get_SourceError().IsEmpty());
                TestTrue(TEXT("compatible source reload accepts"), Wheel->Request_ApplySource(Markup,
                    Style.Replace(TEXT("font-size: 18"), TEXT("font-size: 19"))));
                TestTrue(TEXT("reload retains native wheel and queued state"), Native(Wheel) == Root
                    && Before->GetRevision() > Revision && Wheel->Get_Model()->Get_QueuedKey(2) == FName{TEXT("liara-warp")});
                TestTrue(TEXT("original installed wheel style restores"), Wheel->Request_ApplySource(Markup, Style));
                TestTrue(TEXT("restoring source retains wheel and queue"), Native(Wheel) == Root
                    && Wheel->Get_Model()->Get_QueuedKey(2) == FName{TEXT("liara-warp")});
            }
            TestTrue(TEXT("real Shift release deploys"), Key(EKeys::LeftShift, Wheel->Get_SlateUserIndex(), false));
            TestTrue(TEXT("deploy closes and resumes"), !Wheel->IsWheelOpen() && !World->IsPaused());
            TestEqual(TEXT("exactly three commands dispatched"), Wheel->Get_Model()->Get_DispatchCount(), 3);
            TestEqual(TEXT("cursor restored"), Controller->bShowMouseCursor, State->bOriginalCursor);
            TestTrue(TEXT("prior viewport focus restored"), State->OriginalFocus.IsValid()
                && FSlateApplication::Get().GetUserFocusedWidget(Wheel->Get_SlateUserIndex()) == State->OriginalFocus.Pin());
            TestTrue(TEXT("held view released"), State->HeldView.IsValid() && State->HeldView->IsOwnerReleased());
            TestTrue(TEXT("held native wheel frozen"), Native(Wheel) == nullptr
                && State->HeldNative.IsValid()
                && StaticCastSharedPtr<SCkUiRadialSelector>(State->HeldNative)->IsFrozen());
            TestTrue(TEXT("closed deployment capture"), Capture(World, *State, TEXT("deployed-1920x1080"), {1920, 1080}));
            TestTrue(TEXT("reopen freezes cooldown"), Wheel->Request_Open());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { auto* Wheel = Host(ck::auto_test::net::Get_ServerWorld());
            return IsValid(Wheel) && Wheel->IsWheelOpen() && Wheel->GetRenderOpacity() >= 0.99f; }),
        5.0, TEXT("cooldown wheel fade completes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Wheel = Host(World);
            if (!TestNotNull(TEXT("cooldown wheel host"), Wheel)) { return; }
            if (!TestTrue(TEXT("cooldown wheel model and view exist"), Wheel->IsWheelOpen()
                && Wheel->Get_Model().IsValid() && Wheel->Get_AuthoredView().IsValid()
                && Native(Wheel).IsValid())) { return; }
            TestTrue(TEXT("cooldown native capture"), Capture(World, *State, TEXT("cooldown-1920x1080"), {1920, 1080}));
            Wheel->Request_Cancel();
            TestTrue(TEXT("closed fixture cooldown advances"), Wheel->Get_Model()->Advance(10.0f));
            TestTrue(TEXT("uncooled wheel reopens"), Wheel->Request_Open());
            Wheel->Request_ToggleTarget();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { auto* Wheel = Host(ck::auto_test::net::Get_ServerWorld());
            return IsValid(Wheel) && Wheel->IsWheelOpen() && Wheel->GetRenderOpacity() >= 0.99f; }),
        5.0, TEXT("shielded wheel fade completes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Wheel = Host(World);
            auto* Controller = Player(World);
            if (!TestNotNull(TEXT("shielded wheel host"), Wheel)
                || !TestNotNull(TEXT("shielded wheel controller"), Controller)) { return; }
            if (!TestTrue(TEXT("shielded wheel model and view exist"), Wheel->IsWheelOpen()
                && Wheel->Get_Model().IsValid() && Wheel->Get_AuthoredView().IsValid()
                && Native(Wheel).IsValid())) { return; }
            TestTrue(TEXT("real hover selects shield-blocked power"), Hover(Wheel, TEXT("shepard-throw")));
            TestEqual(TEXT("shield-sensitive power explicitly blocked"), Wheel->Get_Model()->Get_BlockReason(TEXT("shepard-throw")),
                FString{TEXT("Target is shielded")});
            TestTrue(TEXT("shielded target native capture"), Capture(World, *State, TEXT("shielded-1920x1080"), {1920, 1080}));
            State->DispatchBeforeCancel = Wheel->Get_Model()->Get_DispatchCount();
            Wheel->Request_Cancel();
            TestTrue(TEXT("cancel resumes without command"), !Wheel->IsWheelOpen() && !World->IsPaused()
                && Wheel->Get_Model()->Get_DispatchCount() == State->DispatchBeforeCancel);
            TestTrue(TEXT("cancel native capture"), Capture(World, *State, TEXT("canceled-1920x1080"), {1920, 1080}));
            Controller->SetPause(true);
            TestTrue(TEXT("externally paused wheel opens"), Wheel->Request_Open());
            Wheel->Request_Cancel();
            TestTrue(TEXT("cancel preserves external pause"), World->IsPaused()
                && Wheel->Get_Model()->Get_DispatchCount() == State->DispatchBeforeCancel);
            Controller->SetPause(false);
            Wheel->Request_Shutdown();
            TestFalse(TEXT("shutdown prevents opening"), Wheel->Request_Open());
            TestTrue(TEXT("held native remains inert"), State->HeldNative.IsValid()
                && StaticCastSharedPtr<SCkUiRadialSelector>(State->HeldNative)->IsFrozen());
            TestTrue(TEXT("debug overlay values and flags restore before PIE teardown"), State->RestoreDebug());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([State]() -> bool
        {
            State->Restore();
            return !State->HeldView.IsValid() || State->HeldView->IsOwnerReleased();
        }), TEXT("wheel releases retained view and restores capture settings")));
    return true;
}

#endif
