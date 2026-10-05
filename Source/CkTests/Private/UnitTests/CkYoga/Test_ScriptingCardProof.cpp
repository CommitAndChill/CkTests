#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkSlateLayout/CkFlexText.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkWidgets/Authoring/CkAuthoredSession.h"

#include "Blueprint/UserWidget.h"
#include "Engine/FontFace.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/CompositeFont.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Layout/WidgetPath.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"
#include "UObject/GarbageCollection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace ck_scripting_card_pie
{
    struct FState
    {
        ~FState() { Restore(); }

        auto Restore() -> void
        {
            for (const auto& [Name, Value] : DebugValues)
            {
                if (auto* Variable = IConsoleManager::Get().FindConsoleVariable(*Name))
                {
                    Variable->Set(*Value, static_cast<EConsoleVariableFlags>(Variable->GetFlags() & ECVF_SetByMask));
                    Variable->SetFlags(DebugFlags.FindRef(Name));
                }
            }
            DebugValues.Reset(); DebugFlags.Reset();
            if (DebugRegistry.IsValid()) { DebugRegistry->Set_SuppressHUDDuringStartup(OriginalSuppression); }
            DebugRegistry.Reset();
            if (const auto Window = WindowToRestore.Pin(); Window.IsValid()) { Window->Resize(OriginalWindowSize); }
            WindowToRestore.Reset();
            if (SettingsCaptured)
            {
                auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
                Settings->StartupMode = StartupMode;
                Settings->ControlPanelMode = PanelMode;
                Settings->DefaultGymName = DefaultGymName;
                Settings->LastGymName = LastGymName;
                Settings->RecentGymNames = RecentGymNames;
                Settings->SaveConfig();
                SettingsCaptured = false;
            }
        }

        ECkGym_StartupMode StartupMode = ECkGym_StartupMode::Cycler;
        ECkGym_ControlPanel_Mode PanelMode = ECkGym_ControlPanel_Mode::Full;
        FString DefaultGymName, LastGymName;
        TArray<FString> RecentGymNames;
        bool SettingsCaptured = false;
        TWeakPtr<SWindow> WindowToRestore;
        FVector2D OriginalWindowSize = FVector2D::ZeroVector;
        TWeakObjectPtr<UCkGym_Registry_Subsystem> DebugRegistry;
        bool OriginalSuppression = false;
        TMap<FString, FString> DebugValues;
        TMap<FString, EConsoleVariableFlags> DebugFlags;
        TWeakObjectPtr<UCk_UI_AuthoredSession_UE> Session;
        TSharedPtr<FCkUiView> HeldView;
        TSharedPtr<SButton> HeldButton;
        const FCompositeFont* NamedComposite = nullptr;
    };

    auto Registry(UWorld* InWorld) -> UCkGym_Registry_Subsystem*
    {
        auto* Instance = IsValid(InWorld) ? InWorld->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    auto ObjectGetter(UObject* InObject, FName InName) -> UObject*
    {
        auto* Function = IsValid(InObject) ? InObject->FindFunction(InName) : nullptr;
        const auto* Return = Function != nullptr ? CastField<FObjectProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 1) { return nullptr; }
        TArray<uint8> Params; Params.SetNumZeroed(Function->ParmsSize);
        Function->InitializeStruct(Params.GetData());
        InObject->ProcessEvent(Function, Params.GetData());
        auto* Result = Return->GetObjectPropertyValue_InContainer(Params.GetData());
        Function->DestroyStruct(Params.GetData());
        return IsValid(Result) ? Result : nullptr;
    }

    auto BoolCall(UObject* InObject, FName InName) -> bool
    {
        auto* Function = IsValid(InObject) ? InObject->FindFunction(InName) : nullptr;
        const auto* Return = Function != nullptr ? CastField<FBoolProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 1) { return false; }
        TArray<uint8> Params; Params.SetNumZeroed(Function->ParmsSize);
        Function->InitializeStruct(Params.GetData());
        InObject->ProcessEvent(Function, Params.GetData());
        const bool Result = Return->GetPropertyValue_InContainer(Params.GetData());
        Function->DestroyStruct(Params.GetData());
        return Result;
    }

    auto IntGetter(UObject* InObject, FName InName) -> int32
    {
        auto* Function = IsValid(InObject) ? InObject->FindFunction(InName) : nullptr;
        const auto* Return = Function != nullptr ? CastField<FIntProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 1) { return INDEX_NONE; }
        TArray<uint8> Params; Params.SetNumZeroed(Function->ParmsSize);
        Function->InitializeStruct(Params.GetData());
        InObject->ProcessEvent(Function, Params.GetData());
        const int32 Result = Return->GetPropertyValue_InContainer(Params.GetData());
        Function->DestroyStruct(Params.GetData());
        return Result;
    }

    auto StringGetter(UObject* InObject, FName InName) -> FString
    {
        auto* Function = IsValid(InObject) ? InObject->FindFunction(InName) : nullptr;
        const auto* Return = Function != nullptr ? CastField<FStrProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 1) { return {}; }
        TArray<uint8> Params; Params.SetNumZeroed(Function->ParmsSize);
        Function->InitializeStruct(Params.GetData());
        InObject->ProcessEvent(Function, Params.GetData());
        const FString Result = Return->GetPropertyValue_InContainer(Params.GetData());
        Function->DestroyStruct(Params.GetData());
        return Result;
    }

    auto VoidCall(UObject* InObject, FName InName) -> bool
    {
        auto* Function = IsValid(InObject) ? InObject->FindFunction(InName) : nullptr;
        if (Function == nullptr || Function->NumParms != 0) { return false; }
        InObject->ProcessEvent(Function, nullptr);
        return true;
    }

    auto Card(UWorld* InWorld) -> UUserWidget*
    {
        auto* Player = IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr;
        return Cast<UUserWidget>(ObjectGetter(Player, TEXT("Get_Card")));
    }

    auto Session(UWorld* InWorld) -> UCk_UI_AuthoredSession_UE*
    { return Cast<UCk_UI_AuthoredSession_UE>(ObjectGetter(Card(InWorld), TEXT("Get_Session"))); }

    auto Tagged(const TSharedRef<SWidget>& InRoot, FName InTag) -> TSharedPtr<SWidget>
    {
        if (InRoot->GetTag() == InTag) { return InRoot; }
        const auto* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const auto Found = Tagged(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag);
            if (Found.IsValid()) { return Found; }
        }
        return {};
    }

    auto CheckCardFonts(const TSharedRef<SWidget>& InRoot, FAutomationTestBase& InTest) -> bool
    {
        const auto TitleWidget = Tagged(InRoot, TEXT("card-title"));
        const auto SubtitleWidget = Tagged(InRoot, TEXT("card-subtitle"));
        const auto KickerWidget = Tagged(InRoot, TEXT("card-kicker"));
        if (!InTest.TestTrue(TEXT("Production card realizes title, subtitle and default role as native text"),
            TitleWidget.IsValid() && SubtitleWidget.IsValid() && KickerWidget.IsValid()
            && TitleWidget->GetTypeAsString() == TEXT("SCkFlexText")
            && SubtitleWidget->GetTypeAsString() == TEXT("SCkFlexText")
            && KickerWidget->GetTypeAsString() == TEXT("SCkFlexText"))) { return false; }
        const FSlateFontInfo Title = StaticCastSharedPtr<SCkFlexText>(TitleWidget)->GetFont();
        const FSlateFontInfo Subtitle = StaticCastSharedPtr<SCkFlexText>(SubtitleWidget)->GetFont();
        const FSlateFontInfo Kicker = StaticCastSharedPtr<SCkFlexText>(KickerWidget)->GetFont();
        const FCompositeFont* Named = Title.GetCompositeFont();
        bool Good = InTest.TestTrue(TEXT("Production title Bold 44 and subtitle Regular 21 share the admitted named composite"),
            Named != nullptr && Subtitle.GetCompositeFont() == Named
            && Title.TypefaceFontName == TEXT("Bold") && Title.Size == 44.0f
            && Subtitle.TypefaceFontName == TEXT("Regular") && Subtitle.Size == 21.0f);
        Good &= InTest.TestTrue(TEXT("Unstyled kicker retains the Core font"),
            Kicker.GetCompositeFont() == FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10).GetCompositeFont()
            && Kicker.GetCompositeFont() != Named);
        if (Named == nullptr || Named->DefaultTypeface.Fonts.Num() != 2 || Named->FallbackTypeface.Typeface.Fonts.Num() != 2)
        { InTest.AddError(TEXT("Production card named composite lacks primary or fallback face.")); return false; }
        const FString Sources[] = {
            FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins/CkTests/Resources/UiAuthoring/SkyrimJournalProof/RobotoCondensed.ttf")),
            FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Bold.ttf")),
            FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/DroidSansFallback.ttf")),
            FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/DroidSansFallback.ttf"))};
        int32 Index = 0;
        for (const FTypeface* Typeface : {&Named->DefaultTypeface, &Named->FallbackTypeface.Typeface})
        {
            for (const FTypefaceEntry& Entry : Typeface->Fonts)
            {
                TArray<uint8> Expected;
                const UFontFace* Asset = Cast<UFontFace>(const_cast<UObject*>(Entry.Font.GetFontFaceAsset()));
                Good &= InTest.TestTrue(TEXT("Production card named face retains exact source bytes as inline asset"),
                    FFileHelper::LoadFileToArray(Expected, *Sources[Index]) && Asset != nullptr
                    && Asset->LoadingPolicy == EFontLoadingPolicy::Inline
                    && Asset->FontFaceData->GetData() == Expected);
                ++Index;
            }
        }
        return Good;
    }

    auto Pump() -> void
    { auto& Slate = FSlateApplication::Get(); Slate.PumpMessages(); Slate.Tick(); Slate.Tick(); }

    auto Click(const TSharedRef<SWidget>& InWidget) -> bool
    {
        auto& Slate = FSlateApplication::Get();
        FWidgetPath Path;
        if (!Slate.GeneratePathToWidgetUnchecked(InWidget, Path) || !Path.GetWindow()->GetNativeWindow().IsValid()) { return false; }
        Path.GetWindow()->BringToFront(true);
        Pump();
        const auto Geometry = InWidget->GetCachedGeometry();
        const auto Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5);
        const FPointerEvent Down{0, FSlateApplication::CursorPointerIndex, Position, Position,
            TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const FPointerEvent Up{0, FSlateApplication::CursorPointerIndex, Position, Position,
            TSet<FKey>{}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        Slate.SetCursorPos(Position);
        Slate.ProcessMouseMoveEvent(Down, true);
        const bool DownHandled = Slate.ProcessMouseButtonDownEvent(Path.GetWindow()->GetNativeWindow(), Down);
        const bool UpHandled = Slate.ProcessMouseButtonUpEvent(Up);
        Pump();
        return DownHandled && UpHandled;
    }

    auto Capture(UWorld* InWorld, FState& InState, FAutomationTestBase& InTest,
        const FString& InName, FIntPoint InDesired) -> bool
    {
        auto* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const auto Target = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!Target.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Target.ToSharedRef());
        if (!Window.IsValid()) { return false; }
        if (!InState.WindowToRestore.IsValid())
        { InState.WindowToRestore = Window; InState.OriginalWindowSize = Window->GetClientSizeInScreen(); }
        TArray<FColor> Pixels;
        FIntVector Size = FIntVector::ZeroValue;
        for (int32 Attempt = 0; Attempt < 5; ++Attempt)
        {
            Pump(); Pixels.Reset(); Size = FIntVector::ZeroValue;
            if (!Slate.TakeScreenshot(Target.ToSharedRef(), Pixels, Size) || Size.X <= 0 || Size.Y <= 0
                || Pixels.Num() < Size.X * Size.Y) { return false; }
            if (Size.X == InDesired.X && Size.Y == InDesired.Y) { break; }
            Window->Resize(Window->GetClientSizeInScreen() + FVector2D(InDesired.X - Size.X, InDesired.Y - Size.Y));
        }
        if (Size.X != InDesired.X || Size.Y != InDesired.Y) { return false; }
        auto* Owner = Session(InWorld);
        const auto View = IsValid(Owner) ? Owner->Get_View() : nullptr;
        if (!View.IsValid()) { return false; }
        bool Fits = true;
        for (const TCHAR* Tag : {TEXT("card-kicker"), TEXT("card-unit"), TEXT("card-title"), TEXT("card-subtitle"),
            TEXT("card-shot-label"), TEXT("card-shot-value"), TEXT("card-state-label"), TEXT("card-status"),
            TEXT("card-charge-label"), TEXT("card-charge-value"), TEXT("card-safety-label"), TEXT("card-safety-hint"), TEXT("card-footer")})
        {
            const auto Widget = Tagged(View->GetRegion(TEXT("main")), FName{Tag});
            bool ThisFits = Widget.IsValid() && Widget->GetTypeAsString() == TEXT("SCkFlexText");
            FString FitDetails = TEXT("text widget unavailable");
            if (ThisFits)
            {
                const auto Text = StaticCastSharedPtr<SCkFlexText>(Widget);
                const auto Geometry = Text->GetCachedGeometry();
                const auto Space = Geometry.GetLocalSize();
                const auto Needed = Text->Measure(YGUndefined, YGMeasureModeUndefined, YGUndefined, YGMeasureModeUndefined, Geometry.Scale);
                FitDetails = FString::Printf(TEXT("natural %.2fx%.2f, allotted %.2fx%.2f, scale %.3f"),
                    Needed.X, Needed.Y, Space.X, Space.Y, Geometry.Scale);
                ThisFits = FMath::IsFinite(Geometry.Scale) && Geometry.Scale > 0.0f
                    && Space.X > 0.0 && Space.Y > 0.0 && FMath::IsFinite(Needed.X) && FMath::IsFinite(Needed.Y)
                    && Needed.X <= Space.X + 0.5 && Needed.Y <= Space.Y + 0.5;
            }
            InTest.TestTrue(*FString::Printf(TEXT("%s: %s natural text fits (%s)"), *InName, Tag, *FitDetails), ThisFits);
            Fits &= ThisFits;
        }
        const auto Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/ScriptingCardProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, InName + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8)) && Fits;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_ScriptingCardProof_PIE, "Ck.UiAuthoring.ScriptingCard.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCk_ScriptingCardProof_PIE::RunTest(const FString&) -> bool
{
    using namespace ck_scripting_card_pie;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Scripted card requires Slate.")); return false; }
    const auto State = MakeShared<FState>();
    auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
    State->StartupMode = Settings->StartupMode; State->PanelMode = Settings->ControlPanelMode;
    State->DefaultGymName = Settings->DefaultGymName; State->LastGymName = Settings->LastGymName;
    State->RecentGymNames = Settings->RecentGymNames; State->SettingsCaptured = true;
    Settings->StartupMode = ECkGym_StartupMode::Cycler;
    Settings->ControlPanelMode = ECkGym_ControlPanel_Mode::Hidden;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { auto* R = Registry(ck::auto_test::net::Get_ServerWorld());
            return IsValid(R) && R->Find_GymIndexByName(TEXT("UI: Scripted UMG Card")) != INDEX_NONE; }),
        15.0, TEXT("scripted UMG gym registers")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
        {
            auto* R = Registry(World);
            if (!TestNotNull(TEXT("gym registry"), R)) { return; }
            const int32 Index = R->Find_GymIndexByName(TEXT("UI: Scripted UMG Card"));
            if (!TestTrue(TEXT("scripted card entry exists"), R->Get_Entries().IsValidIndex(Index))) { return; }
            const auto& Entry = R->Get_Entries()[Index];
            TestEqual(TEXT("scripted card is in CkUI category"), Entry.Category, FString{TEXT("CkUI")});
            TestTrue(TEXT("gym uses script GameMode"), IsValid(Entry.GameModeClass.Get())
                && Entry.GameModeClass.Get()->GetName().Contains(TEXT("Ck_ScriptingCard_GameMode")));
            R->Request_TravelToGym(Index);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { auto* World = ck::auto_test::net::Get_ServerWorld(); auto* Mode = IsValid(World) ? World->GetAuthGameMode() : nullptr;
            auto* Owner = Session(World);
            return IsValid(Mode) && Mode->GetClass()->GetName().Contains(TEXT("Ck_ScriptingCard_GameMode"))
                && ((IsValid(Owner) && Owner->Get_IsUsable() && Owner->Get_Revision() > 0)
                    || !StringGetter(Card(World), TEXT("Get_SourceError")).IsEmpty()); }),
        30.0, TEXT("production script creates and mounts native view")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Widget = Card(World); auto* Owner = Session(World); auto* R = Registry(World);
            const FString StartupError = StringGetter(Widget, TEXT("Get_SourceError"));
            if (!StartupError.IsEmpty())
            { AddError(FString::Printf(TEXT("Production scripted card source admission: %s"), *StartupError)); return; }
            if (!TestNotNull(TEXT("script owns UUserWidget"), Widget)
                || !TestNotNull(TEXT("script owns shared session"), Owner) || !TestNotNull(TEXT("travel registry"), R)) { return; }
            State->Session = Owner; State->HeldView = Owner->Get_View();
            if (!TestTrue(TEXT("accepted view exists"), State->HeldView.IsValid())) { return; }
            CheckCardFonts(State->HeldView->GetRegion(TEXT("main")), *this);
            const auto Title = Tagged(State->HeldView->GetRegion(TEXT("main")), TEXT("card-title"));
            if (Title.IsValid() && Title->GetTypeAsString() == TEXT("SCkFlexText"))
            { State->NamedComposite = StaticCastSharedPtr<SCkFlexText>(Title)->GetFont().GetCompositeFont(); }
            TestTrue(TEXT("Initial production card exposes an admitted named composite"), State->NamedComposite != nullptr);
            State->HeldButton = State->HeldView->GetButton(TEXT("card-fire"));
            if (!TestTrue(TEXT("action button exists"), State->HeldButton.IsValid())) { return; }
            State->DebugRegistry = R; State->OriginalSuppression = R->Get_SuppressHUDDuringStartup();
            R->Set_SuppressHUDDuringStartup(true);
            for (const TCHAR* Name : {TEXT("ck.UI.WatermarkDisplayPolicy"), TEXT("ck.InputOverlay")})
            {
                auto* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
                if (Variable == nullptr) { AddError(TEXT("Capture debug variable missing.")); return; }
                State->DebugValues.Add(Name, Variable->GetString());
                State->DebugFlags.Add(Name, static_cast<EConsoleVariableFlags>(Variable->GetFlags()));
                Variable->Set(0, static_cast<EConsoleVariableFlags>(Variable->GetFlags() & ECVF_SetByMask));
            }
            for (const auto Size : {FIntPoint{1280, 720}, FIntPoint{1920, 1080}, FIntPoint{2560, 1440}})
            { TestTrue(TEXT("initial native capture and text bounds"), Capture(World, *State, *this,
                FString::Printf(TEXT("initial-%dx%d"), Size.X, Size.Y), Size)); }
            TestTrue(TEXT("real click invokes reflected script action"), Click(State->HeldButton.ToSharedRef()));
            FText Text;
            TestTrue(TEXT("script enforces safety"), Owner->TryGetText(TEXT("Status"), Text) && Text.ToString() == TEXT("SAFETY LOCK")
                && IntGetter(Widget, TEXT("Get_ActionCount")) == 1);
            const auto Root = State->HeldView->GetRegion(TEXT("main"));
            const auto Safety = Tagged(Root, TEXT("card-safety"));
            const auto Charge = Tagged(Root, TEXT("card-charge"));
            if (!TestTrue(TEXT("native editable controls realized"), Safety.IsValid() && Charge.IsValid())) { return; }
            TestTrue(TEXT("real checkbox click proposes edit to script"), Click(Safety.ToSharedRef()));
            bool Safe = true;
            TestTrue(TEXT("script accepts boolean edit"), Owner->TryGetBool(TEXT("Safety"), Safe) && !Safe
                && IntGetter(Widget, TEXT("Get_BoolEditCount")) == 1);
            TestTrue(TEXT("real slider pointer interaction proposes edit"), Click(Charge.ToSharedRef()));
            float Number = 0.0f;
            TestTrue(TEXT("script accepts finite number and updates label"), Owner->TryGetNumber(TEXT("Charge"), Number)
                && FMath::IsFinite(Number) && Number != 65.0f && IntGetter(Widget, TEXT("Get_NumberEditCount")) >= 1
                && Owner->TryGetText(TEXT("ChargeLabel"), Text) && Text.ToString() != TEXT("65%"));
            TestTrue(TEXT("armed native capture"), Capture(World, *State, *this, TEXT("armed-1920x1080"), {1920, 1080}));
            TestTrue(TEXT("real fire click runs script gameplay fixture"), Click(State->HeldButton.ToSharedRef()));
            TestTrue(TEXT("script increments text model"), Owner->TryGetText(TEXT("ShotCount"), Text) && Text.ToString() == TEXT("1")
                && IntGetter(Widget, TEXT("Get_ActionCount")) == 2);
            TestTrue(TEXT("fired native capture"), Capture(World, *State, *this, TEXT("fired-1920x1080"), {1920, 1080}));
            const int64 Revision = Owner->Get_Revision();
            TSharedPtr<const SWidget> Child;
            if (Root->GetChildren()->Num() > 0) { Child = Root->GetChildren()->GetChildAt(0); }
            TestTrue(TEXT("script malformed source command proves rejection"), BoolCall(Widget, TEXT("TryRejectSource")));
            TestTrue(TEXT("rejected script source preserves revision and visible child"), Owner->Get_Revision() == Revision
                && Root->GetChildren()->Num() > 0 && Root->GetChildren()->GetChildAt(0) == Child);
            const FString SourceDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins/CkTests/Resources/UiAuthoring/ScriptingCardProof"));
            FString FontMarkup, FontStyles;
            if (!TestTrue(TEXT("Read actual script card sources for named-font rejection"),
                FFileHelper::LoadFileToString(FontMarkup, *FPaths::Combine(SourceDirectory, TEXT("ScriptingCardProof.ui.html")))
                && FFileHelper::LoadFileToString(FontStyles, *FPaths::Combine(SourceDirectory, TEXT("ScriptingCardProof.ui.css"))))) { return; }
            FontStyles.ReplaceInline(TEXT("font-family: card-ui;"), TEXT("font-family: unknown-ui;"));
            TArray<FString> FontErrors;
            TestFalse(TEXT("Actual script card rejects an unknown named font"), Owner->TryReload(FontMarkup, FontStyles, FontErrors));
            TestTrue(TEXT("Rejected card font source preserves accepted root, revision and model"), !FontErrors.IsEmpty()
                && Owner->Get_Revision() == Revision && Root->GetChildren()->Num() > 0 && Root->GetChildren()->GetChildAt(0) == Child
                && Owner->TryGetText(TEXT("ShotCount"), Text) && Text.ToString() == TEXT("1"));
            TestTrue(TEXT("script reloads authored source files"), BoolCall(Widget, TEXT("Request_Reload")));
            TestTrue(TEXT("accepted script reload retains model"), Owner->Get_Revision() > Revision
                && Owner->TryGetText(TEXT("ShotCount"), Text) && Text.ToString() == TEXT("1"));
            CheckCardFonts(State->HeldView->GetRegion(TEXT("main")), *this);
            const auto ReloadedTitle = Tagged(State->HeldView->GetRegion(TEXT("main")), TEXT("card-title"));
            TestTrue(TEXT("Production card reload retains admitted composite identity"), ReloadedTitle.IsValid()
                && ReloadedTitle->GetTypeAsString() == TEXT("SCkFlexText")
                && StaticCastSharedPtr<SCkFlexText>(ReloadedTitle)->GetFont().GetCompositeFont() == State->NamedComposite);
            State->HeldButton = State->HeldView->GetButton(TEXT("card-fire"));
            CollectGarbage(RF_NoFlags);
            TestTrue(TEXT("script property ownership survives GC"), State->Session.IsValid()
                && Session(World) == State->Session.Get() && Owner->Get_IsUsable());
            CheckCardFonts(State->HeldView->GetRegion(TEXT("main")), *this);
            const auto CollectedTitle = Tagged(State->HeldView->GetRegion(TEXT("main")), TEXT("card-title"));
            TestTrue(TEXT("Production card GC retains admitted composite identity"), CollectedTitle.IsValid()
                && CollectedTitle->GetTypeAsString() == TEXT("SCkFlexText")
                && StaticCastSharedPtr<SCkFlexText>(CollectedTitle)->GetFont().GetCompositeFont() == State->NamedComposite);
            Widget = Card(World); Owner = Session(World);
            if (!TestNotNull(TEXT("script widget remains owned after GC"), Widget)
                || !TestNotNull(TEXT("session remains owned after GC"), Owner)) { return; }
            TestTrue(TEXT("script resets bound values"), VoidCall(Widget, TEXT("Request_Reset")));
            TestTrue(TEXT("reset reaches native scalar model"), Owner->TryGetText(TEXT("ShotCount"), Text) && Text.ToString() == TEXT("0"));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([State]() -> bool
        {
            if (!State->HeldView.IsValid()) { State->Restore(); return true; }
            const bool Retired = State->HeldView.IsValid() && State->HeldView->IsOwnerReleased();
            if (State->HeldButton.IsValid()) { State->HeldButton->SimulateClick(); }
            const bool Unusable = !State->Session.IsValid() || !State->Session->Get_IsUsable();
            State->Restore();
            return Retired && Unusable;
        }), TEXT("script EndPlay retires externally retained native widgets")));
    return true;
}

#endif
