#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkSlateLayout/CkFlexText.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkSlateLayout/SCkUiRepeat.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkWidgets/Authoring/CkAuthoredSession.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
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
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UObject/GarbageCollection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace ck_scripted_collection_pie
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
        TSharedPtr<SButton> RetiredButton;
        TSharedPtr<SWidget> RelayItem;
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

    auto Board(UWorld* InWorld) -> UUserWidget*
    {
        auto* Player = IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr;
        return Cast<UUserWidget>(ObjectGetter(Player, TEXT("Get_Board")));
    }

    auto Session(UWorld* InWorld) -> UCk_UI_AuthoredSession_UE*
    { return Cast<UCk_UI_AuthoredSession_UE>(ObjectGetter(Board(InWorld), TEXT("Get_Session"))); }

    auto Tagged(const TSharedRef<SWidget>& InRoot, FName InTag, bool& OutVisible,
        bool InAncestorsVisible = true) -> TSharedPtr<SWidget>
    {
        // Authored style wrappers can own visibility while the tagged text stays visible.
        const bool Visible = InAncestorsVisible && InRoot->GetVisibility().IsVisible();
        if (InRoot->GetTag() == InTag) { OutVisible = Visible; return InRoot; }
        const auto* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const auto Found = Tagged(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag, OutVisible, Visible);
            if (Found.IsValid()) { return Found; }
        }
        return {};
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

    auto FindButton(const TSharedRef<SWidget>& InRoot, FName InTag) -> TSharedPtr<SButton>
    {
        if (InRoot->GetTag() == InTag && (InRoot->GetTypeAsString() == TEXT("SButton")
            || InRoot->GetTypeAsString() == TEXT("SCkUiStyledButton")))
        { return StaticCastSharedRef<SButton>(InRoot); }
        const auto* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const auto Found = FindButton(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag);
            if (Found.IsValid()) { return Found; }
        }
        return {};
    }

    auto ItemButton(const TSharedPtr<SCkUiRepeat>& InRepeat, const FString& InKey) -> TSharedPtr<SButton>
    {
        const auto Item = InRepeat.IsValid() ? InRepeat->GetItemWidget(InKey) : nullptr;
        return Item.IsValid() ? FindButton(Item.ToSharedRef(), TEXT("mission-select")) : nullptr;
    }

    auto FirstText(const TSharedRef<SWidget>& InRoot) -> TSharedPtr<SCkFlexText>
    {
        if (InRoot->GetTypeAsString() == TEXT("SCkFlexText")) { return StaticCastSharedRef<SCkFlexText>(InRoot); }
        const auto* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const auto Found = FirstText(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)));
            if (Found.IsValid()) { return Found; }
        }
        return {};
    }

    auto Field(const FCk_UI_AuthoredRecord& InRecord, const FString& InName) -> const FCk_UI_AuthoredFieldValue*
    { return InRecord.Get_Fields().FindByPredicate([&InName](const auto& Value) { return Value.Get_Name() == InName; }); }

    auto CheckSeed(UCk_UI_AuthoredSession_UE* InOwner, FAutomationTestBase& InTest) -> bool
    {
        TArray<FCk_UI_AuthoredRecord> Records;
        if (!InTest.TestTrue(TEXT("script seeded four reflected collection records"),
            IsValid(InOwner) && InOwner->TryGetRecords(TEXT("Missions"), Records) && Records.Num() == 4)) { return false; }
        const auto& Relay = Records[0];
        const auto* Title = Field(Relay, TEXT("TitleText"));
        const auto* Done = Field(Relay, TEXT("DoneBool"));
        const auto* Priority = Field(Relay, TEXT("PriorityInteger"));
        const auto* Progress = Field(Relay, TEXT("ProgressNumber"));
        const auto* Accent = Field(Relay, TEXT("AccentColor"));
        return InTest.TestTrue(TEXT("all five reflected field kinds round trip exact script values"),
            Relay.Get_Key() == TEXT("relay") && Title != nullptr && Done != nullptr && Priority != nullptr
            && Progress != nullptr && Accent != nullptr
            && Title->Get_Kind() == ECk_UI_AuthoredFieldKind::Text && Title->Get_Text().ToString() == TEXT("Restore the relay")
            && Done->Get_Kind() == ECk_UI_AuthoredFieldKind::Bool && !Done->Get_Bool()
            && Priority->Get_Kind() == ECk_UI_AuthoredFieldKind::Integer && Priority->Get_Integer() == 1
            && Progress->Get_Kind() == ECk_UI_AuthoredFieldKind::Number && Progress->Get_Number() == 35.0f
            && Accent->Get_Kind() == ECk_UI_AuthoredFieldKind::Color && Accent->Get_Color().Equals(FLinearColor{1.0f, 0.68f, 0.25f, 1.0f}));
    }

    auto SameRecords(const TArray<FCk_UI_AuthoredRecord>& InLeft, const TArray<FCk_UI_AuthoredRecord>& InRight) -> bool
    {
        if (InLeft.Num() != InRight.Num()) { return false; }
        for (int32 Index = 0; Index < InLeft.Num(); ++Index)
        {
            const auto& Left = InLeft[Index]; const auto& Right = InRight[Index];
            if (Left.Get_Key() != Right.Get_Key() || Left.Get_Fields().Num() != Right.Get_Fields().Num()) { return false; }
            for (const auto& Value : Left.Get_Fields())
            {
                const auto* Other = Field(Right, Value.Get_Name());
                if (Other == nullptr || Value.Get_Kind() != Other->Get_Kind()) { return false; }
                switch (Value.Get_Kind())
                {
                    case ECk_UI_AuthoredFieldKind::Text: if (Value.Get_Text().ToString() != Other->Get_Text().ToString()) { return false; } break;
                    case ECk_UI_AuthoredFieldKind::Number: if (Value.Get_Number() != Other->Get_Number()) { return false; } break;
                    case ECk_UI_AuthoredFieldKind::Bool: if (Value.Get_Bool() != Other->Get_Bool()) { return false; } break;
                    case ECk_UI_AuthoredFieldKind::Integer: if (Value.Get_Integer() != Other->Get_Integer()) { return false; } break;
                    case ECk_UI_AuthoredFieldKind::Color: if (!Value.Get_Color().Equals(Other->Get_Color())) { return false; } break;
                    default: return false;
                }
            }
        }
        return true;
    }

    auto CheckBounds(const TSharedRef<SWidget>& InRoot, FAutomationTestBase& InTest,
        const FString& InCapture, const FSlateRect& InViewport) -> bool
    {
        if (!InRoot->GetVisibility().IsVisible()) { return true; }
        bool Good = true;
        if (InRoot->GetTypeAsString() == TEXT("SCkFlexText"))
        {
            const auto Text = StaticCastSharedRef<SCkFlexText>(InRoot);
            const auto Geometry = Text->GetCachedGeometry();
            const auto Space = Geometry.GetLocalSize();
            const bool WrappedBrief = Text->GetTag() == TEXT("detail-brief");
            const auto Needed = Text->Measure(WrappedBrief ? Space.X : YGUndefined,
                WrappedBrief ? YGMeasureModeAtMost : YGMeasureModeUndefined,
                YGUndefined, YGMeasureModeUndefined, Geometry.Scale);
            const auto Top = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
            const auto Bottom = Geometry.LocalToAbsolute(Space);
            Good = FMath::IsFinite(Geometry.Scale) && Geometry.Scale > 0.0f && Space.X > 0.0 && Space.Y > 0.0
                && FMath::IsFinite(Needed.X) && FMath::IsFinite(Needed.Y) && Needed.X <= Space.X + 0.5 && Needed.Y <= Space.Y + 0.5
                && Top.X >= InViewport.Left - 0.5 && Top.Y >= InViewport.Top - 0.5
                && Bottom.X <= InViewport.Right + 0.5 && Bottom.Y <= InViewport.Bottom + 0.5;
            InTest.TestTrue(*FString::Printf(TEXT("%s: %s text fits allocated content and viewport (%.1fx%.1f / %.1fx%.1f)"),
                *InCapture, *Text->GetTag().ToString(), Needed.X, Needed.Y, Space.X, Space.Y), Good);
        }
        const auto* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        { Good &= CheckBounds(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTest, InCapture, InViewport); }
        return Good;
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
        TArray<FColor> Pixels; FIntVector Size = FIntVector::ZeroValue;
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
        const auto Geometry = Target->GetCachedGeometry();
        const auto Top = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
        const auto Bottom = Geometry.LocalToAbsolute(Geometry.GetLocalSize());
        const bool Fits = CheckBounds(View->GetRegion(TEXT("main")), InTest, InName, FSlateRect(Top.X, Top.Y, Bottom.X, Bottom.Y));
        const auto Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/ScriptedCollectionProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, InName + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8)) && Fits;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_ScriptedCollectionProof_PIE, "Ck.UiAuthoring.ScriptedCollection.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCk_ScriptedCollectionProof_PIE::RunTest(const FString&) -> bool
{
    using namespace ck_scripted_collection_pie;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Scripted mission board requires Slate.")); return false; }
    const auto State = MakeShared<FState>();
    auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
    State->StartupMode = Settings->StartupMode; State->PanelMode = Settings->ControlPanelMode;
    State->DefaultGymName = Settings->DefaultGymName; State->LastGymName = Settings->LastGymName;
    State->RecentGymNames = Settings->RecentGymNames; State->SettingsCaptured = true;
    Settings->StartupMode = ECkGym_StartupMode::Cycler; Settings->ControlPanelMode = ECkGym_ControlPanel_Mode::Hidden;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { auto* R = Registry(ck::auto_test::net::Get_ServerWorld());
            return IsValid(R) && R->Find_GymIndexByName(TEXT("UI: Scripted Mission Board")) != INDEX_NONE; }),
        15.0, TEXT("scripted mission gym registers")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
        {
            auto* R = Registry(World);
            if (!TestNotNull(TEXT("gym registry"), R)) { return; }
            const int32 Index = R->Find_GymIndexByName(TEXT("UI: Scripted Mission Board"));
            if (!TestTrue(TEXT("mission gym entry exists"), R->Get_Entries().IsValidIndex(Index))) { return; }
            const auto& Entry = R->Get_Entries()[Index];
            TestEqual(TEXT("mission gym category"), Entry.Category, FString{TEXT("CkUI")});
            TestTrue(TEXT("mission gym uses script GameMode"), IsValid(Entry.GameModeClass.Get())
                && Entry.GameModeClass.Get()->GetName().Contains(TEXT("Ck_ScriptingMissions_GameMode")));
            R->Request_TravelToGym(Index);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { auto* World = ck::auto_test::net::Get_ServerWorld(); auto* Mode = IsValid(World) ? World->GetAuthGameMode() : nullptr;
            auto* Owner = Session(World);
            return IsValid(Mode) && Mode->GetClass()->GetName().Contains(TEXT("Ck_ScriptingMissions_GameMode"))
                && ((IsValid(Owner) && Owner->Get_IsUsable() && Owner->Get_Revision() > 0)
                    || !StringGetter(Board(World), TEXT("Get_StartupError")).IsEmpty()); }),
        30.0, TEXT("production script mounts mission view")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Widget = Board(World); auto* Owner = Session(World); auto* R = Registry(World);
            const FString StartupError = StringGetter(Widget, TEXT("Get_StartupError"));
            if (!StartupError.IsEmpty()) { AddError(FString::Printf(TEXT("Mission board startup: %s"), *StartupError)); return; }
            if (!TestNotNull(TEXT("script UUserWidget"), Widget) || !TestNotNull(TEXT("script session"), Owner)
                || !TestNotNull(TEXT("travel registry"), R)) { return; }
            State->Session = Owner; State->HeldView = Owner->Get_View();
            if (!TestTrue(TEXT("accepted production view"), State->HeldView.IsValid())) { return; }
            const auto Repeat = State->HeldView->GetRepeat(TEXT("missions"));
            if (!TestTrue(TEXT("native repeat presents four mission rows"), Repeat.IsValid() && Repeat->GetItemCount() == 4)) { return; }
            CheckSeed(Owner, *this);
            State->RelayItem = Repeat->GetItemWidget(TEXT("relay"));
            State->HeldButton = ItemButton(Repeat, TEXT("relay"));
            if (!TestTrue(TEXT("native stable-key select button"), State->RelayItem.IsValid() && State->HeldButton.IsValid())) { return; }
            const auto RelayLabel = FirstText(State->HeldButton.ToSharedRef());
            bool DoneVisible = true;
            const auto DoneLabel = Tagged(State->RelayItem.ToSharedRef(), TEXT("mission-done"), DoneVisible);
            TestTrue(TEXT("native field-bound title and accent match script data"), RelayLabel.IsValid()
                && RelayLabel->GetText().ToString() == TEXT("Restore the relay")
                && RelayLabel->GetColorAndOpacity().GetSpecifiedColor().Equals(FLinearColor{1.0f, 0.68f, 0.25f, 1.0f}));
            TestTrue(TEXT("native Bool field hides completed label through its authored ancestry initially"), DoneLabel.IsValid() && !DoneVisible);
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
            { TestTrue(TEXT("initial mission native capture and bounds"), Capture(World, *State, *this,
                FString::Printf(TEXT("initial-%dx%d"), Size.X, Size.Y), Size)); }
            const auto Salvage = ItemButton(Repeat, TEXT("salvage"));
            if (!TestTrue(TEXT("native salvage button exists"), Salvage.IsValid())) { return; }
            TestTrue(TEXT("pointer selects stable mission key"), Click(Salvage.ToSharedRef())
                && StringGetter(Widget, TEXT("Get_SelectedKey")) == TEXT("salvage") && IntGetter(Widget, TEXT("Get_SelectionCount")) == 1);
            TestTrue(TEXT("selected mission capture"), Capture(World, *State, *this, TEXT("selected-1920x1080"), {1920, 1080}));
            TestTrue(TEXT("script pure order replacement accepted"), BoolCall(Widget, TEXT("Request_Reverse")));
            Pump();
            TArray<FCk_UI_AuthoredRecord> Ordered;
            TestTrue(TEXT("pure order retains native item identity"), Owner->TryGetRecords(TEXT("Missions"), Ordered)
                && Ordered.Num() == 4 && Ordered[0].Get_Key() == TEXT("survey")
                && Repeat->GetItemWidget(TEXT("relay")) == State->RelayItem);
            const auto* Children = Repeat->GetChildren();
            const auto* Items = Children != nullptr && Children->Num() == 1
                ? ConstCastSharedRef<SWidget>(Children->GetChildAt(0))->GetChildren() : nullptr;
            TestTrue(TEXT("native child order follows replacement order"), Items != nullptr && Items->Num() == 4
                && Items->GetChildAt(0) == Repeat->GetItemWidget(TEXT("survey")));
            State->HeldButton = ItemButton(Repeat, TEXT("relay"));
            if (!TestTrue(TEXT("current native relay control exists after reorder"), State->HeldButton.IsValid())) { return; }
            TestTrue(TEXT("current native button selects stable key after pure reorder"), Click(State->HeldButton.ToSharedRef())
                && StringGetter(Widget, TEXT("Get_SelectedKey")) == TEXT("relay"));
            State->RetiredButton = State->HeldButton;
            const int32 Before = IntGetter(Widget, TEXT("Get_SelectionCount"));
            TestTrue(TEXT("script removes selected key"), BoolCall(Widget, TEXT("Request_RemoveSelected")));
            State->RetiredButton->SimulateClick();
            TestEqual(TEXT("removed held callback immediately rejects before presenter tick"), IntGetter(Widget, TEXT("Get_SelectionCount")), Before);
            Pump();
            TestTrue(TEXT("removed row gone"), !Repeat->GetItemWidget(TEXT("relay")).IsValid()
                && StringGetter(Widget, TEXT("Get_SelectedKey")).IsEmpty());
            TestTrue(TEXT("script reintroduces same key"), BoolCall(Widget, TEXT("Request_Reinsert")));
            State->RetiredButton->SimulateClick();
            TestEqual(TEXT("same-key admission never revives removed callback"), IntGetter(Widget, TEXT("Get_SelectionCount")), Before);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(3));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Widget = Board(World); auto* Owner = Session(World);
            if (!TestNotNull(TEXT("board remains owned"), Widget) || !TestNotNull(TEXT("session remains owned"), Owner)
                || !TestTrue(TEXT("retained view valid"), State->HeldView.IsValid())) { return; }
            const auto Repeat = State->HeldView->GetRepeat(TEXT("missions"));
            const auto Relay = ItemButton(Repeat, TEXT("relay"));
            if (!TestTrue(TEXT("reinsert creates fresh native control"), Relay.IsValid() && Relay != State->RetiredButton)) { return; }
            TestTrue(TEXT("fresh pointer selects reinserted mission"), Click(Relay.ToSharedRef()));
            const auto Complete = State->HeldView->GetButton(TEXT("mission-complete"));
            if (!TestTrue(TEXT("root complete action exists"), Complete.IsValid())) { return; }
            TestTrue(TEXT("root action executes script completion policy"), Click(Complete.ToSharedRef())
                && IntGetter(Widget, TEXT("Get_CompleteCount")) == 1);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(3));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Widget = Board(World); auto* Owner = Session(World);
            if (!TestNotNull(TEXT("board for final model checks"), Widget) || !TestNotNull(TEXT("session for final model checks"), Owner)
                || !TestTrue(TEXT("view for final model checks"), State->HeldView.IsValid())) { return; }
            const auto Repeat = State->HeldView->GetRepeat(TEXT("missions"));
            TArray<FCk_UI_AuthoredRecord> Records;
            if (!TestTrue(TEXT("accepted mission records readable"), Owner->TryGetRecords(TEXT("Missions"), Records))) { return; }
            const auto* Relay = Records.FindByPredicate([](const auto& Record) { return Record.Get_Key() == TEXT("relay"); });
            const auto* Done = Relay != nullptr ? Field(*Relay, TEXT("DoneBool")) : nullptr;
            const auto* Progress = Relay != nullptr ? Field(*Relay, TEXT("ProgressNumber")) : nullptr;
            FText Text;
            TestTrue(TEXT("completion publishes typed collection and scalar detail"), Done != nullptr && Done->Get_Bool()
                && Progress != nullptr && Progress->Get_Number() == 100.0f
                && Owner->TryGetText(TEXT("DetailStatus"), Text) && Text.ToString() == TEXT("MISSION COMPLETE"));
            const auto CompletedItem = Repeat.IsValid() ? Repeat->GetItemWidget(TEXT("relay")) : nullptr;
            bool CompletedVisible = false;
            const auto CompletedLabel = CompletedItem.IsValid() ? Tagged(CompletedItem.ToSharedRef(), TEXT("mission-done"), CompletedVisible) : nullptr;
            TestTrue(TEXT("native Bool field reveals completed label through its authored ancestry after presenter update"), CompletedLabel.IsValid() && CompletedVisible);
            TestTrue(TEXT("completed mission capture and bounds"), Capture(World, *State, *this, TEXT("complete-1920x1080"), {1920, 1080}));
            int64 CollectionRevision = 0;
            if (!TestTrue(TEXT("collection revision readable"), Owner->TryGetCollectionRevision(TEXT("Missions"), CollectionRevision))) { return; }
            const int64 ViewRevision = Owner->Get_Revision();
            const auto Root = State->HeldView->GetRegion(TEXT("main"));
            const auto* RootChildren = Root->GetChildren();
            if (!TestTrue(TEXT("production region has concrete child"), RootChildren != nullptr && RootChildren->Num() > 0)) { return; }
            const auto Child = RootChildren->GetChildAt(0);
            TestTrue(TEXT("script rejects malformed full collection and source"), BoolCall(Widget, TEXT("TryRejectData"))
                && BoolCall(Widget, TEXT("TryRejectSource")));
            int64 AfterRevision = 0;
            TArray<FCk_UI_AuthoredRecord> After;
            TestTrue(TEXT("rejected requests preserve accepted view, collection revision and model"), Owner->Get_Revision() == ViewRevision
                && Owner->TryGetCollectionRevision(TEXT("Missions"), AfterRevision) && AfterRevision == CollectionRevision
                && Owner->TryGetRecords(TEXT("Missions"), After) && SameRecords(Records, After)
                && Root->GetChildren() != nullptr && Root->GetChildren()->Num() > 0 && Root->GetChildren()->GetChildAt(0) == Child
                && Owner->TryGetText(TEXT("DetailStatus"), Text) && Text.ToString() == TEXT("MISSION COMPLETE"));
            TestTrue(TEXT("valid authored reload retains model and collection revision"), BoolCall(Widget, TEXT("Request_Reload"))
                && Owner->Get_Revision() > ViewRevision && Owner->TryGetCollectionRevision(TEXT("Missions"), AfterRevision)
                && AfterRevision == CollectionRevision && Owner->TryGetRecords(TEXT("Missions"), After) && SameRecords(Records, After)
                && Owner->TryGetText(TEXT("DetailStatus"), Text) && Text.ToString() == TEXT("MISSION COMPLETE"));
            CollectGarbage(RF_NoFlags);
            Widget = Board(World); Owner = Session(World);
            if (!TestTrue(TEXT("script property ownership survives GC"), IsValid(Widget) && IsValid(Owner)
                && State->Session.Get() == Owner && Owner->Get_IsUsable())) { return; }
            const auto CurrentRepeat = State->HeldView->GetRepeat(TEXT("missions"));
            State->HeldButton = ItemButton(CurrentRepeat, TEXT("relay"));
            if (!TestTrue(TEXT("current held callback exists before empty replacement"), State->HeldButton.IsValid())) { return; }
            TestTrue(TEXT("script accepts empty collection"), BoolCall(Widget, TEXT("Request_Empty")));
            const int32 Before = IntGetter(Widget, TEXT("Get_SelectionCount"));
            State->HeldButton->SimulateClick();
            TestEqual(TEXT("empty collection immediately invalidates old item callbacks"), IntGetter(Widget, TEXT("Get_SelectionCount")), Before);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(3));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Owner = Session(World);
            if (!TestTrue(TEXT("empty session remains usable"), IsValid(Owner) && Owner->Get_IsUsable() && State->HeldView.IsValid())) { return; }
            const auto Repeat = State->HeldView->GetRepeat(TEXT("missions"));
            TestTrue(TEXT("native empty list realizes zero items"), Repeat.IsValid() && Repeat->GetItemCount() == 0);
            TestTrue(TEXT("empty native capture and bounds"), Capture(World, *State, *this, TEXT("empty-1920x1080"), {1920, 1080}));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([State]() -> bool
        {
            const bool Retired = State->HeldView.IsValid() && State->HeldView->IsOwnerReleased();
            if (State->HeldButton.IsValid()) { State->HeldButton->SimulateClick(); }
            if (State->RetiredButton.IsValid()) { State->RetiredButton->SimulateClick(); }
            CollectGarbage(RF_NoFlags);
            const bool Collected = !State->Session.IsValid();
            State->Restore(); return Retired && Collected;
        }), TEXT("teardown retires native callbacks and permits script session collection")));
    return true;
}

#endif
