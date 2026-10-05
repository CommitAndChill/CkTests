#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/UI/CkJournalProof.h"
#include "CkSlateLayout/CkFlexText.h"
#include "CkSlateLayout/CkUiTreeCollection.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkSlateLayout/SCkUiTabs.h"
#include "CkSlateLayout/SCkUiTree.h"
#include "CkUI/Layout/CkUI_Layout_Subsystem.h"
#include "CkUI/Layout/CkUI_LayerStack.h"
#include "UI/CkJournalProofModel.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameplayTagContainer.h"
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
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <initializer_list>

namespace ck_journal_pie
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
                    IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
                    DebugValues.Add(Name, Variable->GetString());
                    DebugFlags.Add(Name, static_cast<EConsoleVariableFlags>(Variable->GetFlags()));
                }
                bDebugCaptured = true;
            }
            for (IConsoleVariable* Variable : {Watermark, Input})
            { Variable->Set(0, static_cast<EConsoleVariableFlags>(Variable->GetFlags() & ECVF_SetByMask)); }
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
                IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name);
                if (Variable == nullptr) { Restored = false; continue; }
                Variable->Set(*Value, static_cast<EConsoleVariableFlags>(Variable->GetFlags() & ECVF_SetByMask));
                Variable->SetFlags(DebugFlags.FindRef(Name));
                Restored &= Variable->GetString() == Value
                    && static_cast<EConsoleVariableFlags>(Variable->GetFlags()) == DebugFlags.FindRef(Name);
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
        TWeakObjectPtr<UCk_JournalProof_UE> Host;
        TSharedPtr<FCkUiView> HeldView;
        TSharedPtr<SCkUiTree> HeldTree;
        TWeakPtr<SWidget> OriginalFocus;
        bool bOriginalCursor = false;
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

    auto Host(UWorld* World) -> UCk_JournalProof_UE*
    {
        auto* Controller = Player(World);
        if (!IsValid(Controller)) { return nullptr; }
        UFunction* Getter = Controller->FindFunction(TEXT("Get_Journal"));
        const auto* Return = Getter != nullptr ? CastField<FObjectProperty>(Getter->GetReturnProperty()) : nullptr;
        if (Return == nullptr) { return nullptr; }
        TArray<uint8> Params;
        Params.SetNumZeroed(Getter->ParmsSize);
        Getter->InitializeStruct(Params.GetData());
        Controller->ProcessEvent(Getter, Params.GetData());
        auto* Result = Cast<UCk_JournalProof_UE>(Return->GetObjectPropertyValue_InContainer(Params.GetData()));
        Getter->DestroyStruct(Params.GetData());
        return IsValid(Result) ? Result : nullptr;
    }

    auto FindHeader(const TSharedRef<SWidget>& Root, const FString& Label) -> TSharedPtr<SButton>
    {
        if (Root->GetTypeAsString() == TEXT("SButton"))
        {
            const FChildren* Children = Root->GetChildren();
            for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
            {
                const TSharedRef<SWidget> Child = ConstCastSharedRef<SWidget>(Children->GetChildAt(Index));
                if (Child->GetTypeAsString() == TEXT("STextBlock")
                    && StaticCastSharedRef<STextBlock>(Child)->GetText().ToString() == Label)
                { return StaticCastSharedRef<SButton>(Root); }
            }
        }
        const FChildren* Children = Root->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const auto Found = FindHeader(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), Label); Found.IsValid())
            { return Found; }
        }
        return {};
    }

    auto NativeText(const TSharedRef<SWidget>& Root, FName Tag) -> TSharedPtr<SCkFlexText>
    {
        if (Root->GetTag() == Tag && Root->GetTypeAsString() == TEXT("SCkFlexText"))
        { return StaticCastSharedRef<SCkFlexText>(Root); }
        const FChildren* Children = Root->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        { if (const auto Found = NativeText(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), Tag); Found.IsValid()) { return Found; } }
        return {};
    }

    auto EffectivelyVisible(const TSharedRef<SWidget>& Widget) -> bool
    {
        TSharedPtr<SWidget> Current = Widget;
        while (Current.IsValid())
        {
            if (!Current->GetVisibility().IsVisible()) { return false; }
            Current = Current->GetParentWidget();
        }
        return true;
    }

    auto TextFits(const TSharedRef<SWidget>& Root, FName Tag) -> bool
    {
        const auto Text = NativeText(Root, Tag);
        if (!Text.IsValid() || Text->GetText().IsEmpty() || !EffectivelyVisible(Text.ToSharedRef())) { return false; }
        const FGeometry& Geometry = Text->GetCachedGeometry();
        const FVector2D Space = Geometry.GetLocalSize();
        if (!FMath::IsFinite(Geometry.Scale) || Geometry.Scale <= 0.0f
            || !FMath::IsFinite(Space.X) || Space.X <= 0.0
            || !FMath::IsFinite(Space.Y) || Space.Y <= 0.0) { return false; }
        const FVector2D Needed = Text->Measure(static_cast<float>(Space.X), YGMeasureModeExactly,
            YGUndefined, YGMeasureModeUndefined, Geometry.Scale);
        if (!FMath::IsFinite(Needed.Y) || Needed.Y > Space.Y + 0.5) { return false; }
        const bool RequireNaturalWidth = Tag == FName{TEXT("quest-state")}
            || Tag == FName{TEXT("footer-date")};
        if (!RequireNaturalWidth) { return true; }
        const FVector2D Natural = Text->Measure(YGUndefined, YGMeasureModeUndefined,
            YGUndefined, YGMeasureModeUndefined, Geometry.Scale);
        return FMath::IsFinite(Natural.X) && Natural.X <= Space.X + 0.5;
    }

    void GatherVisibleRowTexts(const TSharedRef<SWidget>& Root, FName Tag,
        TArray<TSharedPtr<SCkFlexText>>& OutTexts)
    {
        if (Root->GetTag() == Tag && Root->GetTypeAsString() == TEXT("SCkFlexText")
            && EffectivelyVisible(Root))
        { OutTexts.Add(StaticCastSharedRef<SCkFlexText>(Root)); }
        const FChildren* Children = Root->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        { GatherVisibleRowTexts(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), Tag, OutTexts); }
    }

    auto AllRealizedRowTextsFit(const TSharedRef<SWidget>& Root, FName Tag, FName RowTag) -> bool
    {
        TArray<TSharedPtr<SCkFlexText>> Texts;
        GatherVisibleRowTexts(Root, Tag, Texts);
        if (Texts.IsEmpty()) { return false; }
        for (const auto& Text : Texts)
        {
            if (!Text.IsValid() || Text->GetText().IsEmpty()) { return false; }
            const FGeometry& Geometry = Text->GetCachedGeometry();
            const FVector2D Size = Geometry.GetLocalSize();
            if (!FMath::IsFinite(Geometry.Scale) || Geometry.Scale <= 0.0f
                || !FMath::IsFinite(Size.X) || Size.X <= 0.0
                || !FMath::IsFinite(Size.Y) || Size.Y <= 0.0) { return false; }
            const FVector2D Needed = Text->Measure(static_cast<float>(Size.X), YGMeasureModeExactly,
                YGUndefined, YGMeasureModeUndefined, Geometry.Scale);
            if (!FMath::IsFinite(Needed.Y) || Needed.Y > Size.Y + 0.5) { return false; }
            TSharedPtr<SWidget> Row = Text->GetParentWidget();
            while (Row.IsValid() && Row->GetTag() != RowTag) { Row = Row->GetParentWidget(); }
            if (!Row.IsValid()) { return false; }
            const FGeometry& RowGeometry = Row->GetCachedGeometry();
            const FVector2D Start = RowGeometry.AbsoluteToLocal(Geometry.GetAbsolutePosition());
            const FVector2D End = RowGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(Size));
            if (Start.X < -1.0 || Start.Y < -1.0 || End.X > RowGeometry.GetLocalSize().X + 1.0
                || End.Y > RowGeometry.GetLocalSize().Y + 1.0) { return false; }
        }
        return true;
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

    auto Click(const TSharedRef<SButton>& Button, int32 User) -> bool
    {
        auto& Slate = FSlateApplication::Get();
        FWidgetPath Path;
        if (!Slate.GeneratePathToWidgetUnchecked(Button, Path) || !Path.GetWindow()->GetNativeWindow().IsValid()) { return false; }
        const FGeometry Geometry = Button->GetCachedGeometry();
        const FVector2D Position = Geometry.GetAbsolutePosition() + Geometry.GetAbsoluteSize() * 0.5f;
        const FPointerEvent Down{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Position, Position, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const FPointerEvent Up{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Position, Position, TSet<FKey>{}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        Slate.SetCursorPos(Position);
        Slate.ProcessMouseMoveEvent(Down, true);
        return Slate.ProcessMouseButtonDownEvent(Path.GetWindow()->GetNativeWindow(), Down)
            && Slate.ProcessMouseButtonUpEvent(Up);
    }

    auto Capture(UWorld* World, FState& State, const FString& Name, FIntPoint Desired,
        std::initializer_list<const TCHAR*> TextTags = {}) -> bool
    {
        const auto Target = Viewport(World);
        if (!Target.IsValid() || State.Test == nullptr) { return false; }
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
        bool Fits = true;
        auto* Journal = Host(World);
        const auto View = IsValid(Journal) ? Journal->Get_AuthoredView() : nullptr;
        if (View.IsValid())
        {
            const auto Root = View->GetRegion(TEXT("main"));
            for (const TCHAR* Tag : TextTags)
            {
                const bool ThisFits = TextFits(Root, FName{Tag});
                State.Test->TestTrue(*FString::Printf(TEXT("%s native %s text fits"), *Name, Tag), ThisFits);
                Fits &= ThisFits;
            }
            const auto Model = Journal->Get_Model();
            if (Journal->IsJournalOpen() && Model.IsValid()
                && Model->Get_TabKey() == FName{TEXT("quests")})
            {
                struct FRowTags { FName Text; FName Row; };
                const FRowTags Rows[] = {
                    {FName{TEXT("quest-row-name")}, FName{TEXT("quest-row")}},
                    {FName{TEXT("objective-row-name")}, FName{TEXT("objective-row")}}};
                for (const FRowTags& Row : Rows)
                {
                    const bool RowsFit = AllRealizedRowTextsFit(Root, Row.Text, Row.Row);
                    State.Test->TestTrue(*FString::Printf(TEXT("%s native %s realized rows fit"),
                        *Name, *Row.Text.ToString()), RowsFit);
                    Fits &= RowsFit;
                }
            }
        }
        else if (TextTags.size() > 0) { return false; }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/SkyrimJournalProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, Name + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8)) && Fits;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_JournalProof_PIE, "Ck.UiAuthoring.Journal.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FCk_JournalProof_PIE::RunTest(const FString&)
{
    using namespace ck_journal_pie;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Journal requires Slate.")); return false; }
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
            return IsValid(R) && R->Find_GymIndexByName(TEXT("UI: Skyrim Journal")) != INDEX_NONE; }),
        15.0, TEXT("Skyrim Journal gym registers")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
        {
            auto* R = Registry(World);
            if (!TestNotNull(TEXT("gym registry"), R)) { return; }
            const int32 Index = R->Find_GymIndexByName(TEXT("UI: Skyrim Journal"));
            if (!TestTrue(TEXT("Skyrim Journal gym entry exists"), R->Get_Entries().IsValidIndex(Index))) { return; }
            const auto& Entry = R->Get_Entries()[Index];
            TestEqual(TEXT("journal is a CkUI gym"), Entry.Category, FString{TEXT("CkUI")});
            if (TestTrue(TEXT("entry uses authored journal GameMode"), IsValid(Entry.GameModeClass.Get())
                && Entry.GameModeClass.Get()->GetName().Contains(TEXT("Ck_SkyrimJournal_GameMode"))))
            { R->Request_TravelToGym(Index); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            auto* Mode = IsValid(World) ? World->GetAuthGameMode() : nullptr;
            auto* Controller = Player(World);
            return IsValid(Mode) && Mode->GetClass()->GetName().Contains(TEXT("Ck_SkyrimJournal_GameMode"))
                && IsValid(Controller) && Controller->GetClass()->GetName().Contains(TEXT("Ck_SkyrimJournal_PlayerController"))
                && IsValid(Host(World));
        }), 30.0, TEXT("registry travel creates production Journal host")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Journal = Host(World);
            auto* Controller = Player(World);
            if (!TestNotNull(TEXT("script controller owns journal host"), Journal)
                || !TestNotNull(TEXT("local journal controller"), Controller)) { return; }
            State->Host = Journal;
            State->bOriginalCursor = Controller->bShowMouseCursor;
            if (!TestTrue(TEXT("capture temporarily hides debug overlays"), State->HideDebug(World))) { return; }
            TestFalse(TEXT("gym starts unpaused"), World->IsPaused());
            if (!TestTrue(TEXT("viewport accepts real input"), FocusViewport(World, Journal->Get_SlateUserIndex()))) { return; }
            State->OriginalFocus = FSlateApplication::Get().GetUserFocusedWidget(Journal->Get_SlateUserIndex());
            TestTrue(TEXT("real J down opens through input processor"), Key(EKeys::J, Journal->Get_SlateUserIndex(), true));
            Pump();
            TestTrue(*FString::Printf(TEXT("journal open pauses world (source='%s', summary='%s', view=%s)"),
                *Journal->Get_SourceError(), *Journal->Get_Summary(),
                Journal->Get_AuthoredView().IsValid() ? TEXT("present") : TEXT("absent")),
                Journal->IsJournalOpen() && World->IsPaused());
            Key(EKeys::J, Journal->Get_SlateUserIndex(), false);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Journal = Host(ck::auto_test::net::Get_ServerWorld());
            const auto View = IsValid(Journal) ? Journal->Get_AuthoredView() : nullptr;
            return IsValid(Journal) && Journal->IsJournalOpen() && Journal->GetRenderOpacity() >= 0.99f
                && View.IsValid() && View->GetTree(TEXT("quest-list")).IsValid()
                && View->GetTabs(TEXT("journal-tabs")).IsValid();
        }), 5.0, TEXT("journal fade completes with retained trees and tabs")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Journal = Host(World);
            auto* Controller = Player(World);
            if (!TestNotNull(TEXT("journal after fade"), Journal)
                || !TestNotNull(TEXT("journal controller after fade"), Controller)) { return; }
            const auto View = Journal->Get_AuthoredView();
            const auto Model = Journal->Get_Model();
            if (!TestTrue(TEXT("journal model and authored view exist"), Journal->IsJournalOpen()
                && View.IsValid() && Model.IsValid())) { return; }
            const auto QuestTree = View->GetTree(TEXT("quest-list"));
            const auto ObjectiveTree = View->GetTree(TEXT("objective-list"));
            const auto Tabs = View->GetTabs(TEXT("journal-tabs"));
            if (!TestTrue(TEXT("native quest/objective trees and tabs exist"), QuestTree.IsValid()
                && ObjectiveTree.IsValid() && Tabs.IsValid() && QuestTree->GetTree().IsValid()
                && ObjectiveTree->GetTree().IsValid())) { return; }
            State->HeldView = View;
            State->HeldTree = QuestTree;
            const TSharedRef<SWidget> Root = View->GetRegion(TEXT("main"));
            const int32 User = Journal->Get_SlateUserIndex();
            auto& Slate = FSlateApplication::Get();
            TestTrue(TEXT("journal directs owning-user focus into authored view"),
                Slate.GetUserFocusedWidget(User).IsValid() && Slate.HasUserFocusedDescendants(Root, User));
            struct FViewportCase { const TCHAR* Name; FIntPoint Size; };
            const FViewportCase Viewports[] = {
                {TEXT("initial-1280x720"), FIntPoint{1280, 720}},
                {TEXT("initial-1920x1080"), FIntPoint{1920, 1080}},
                {TEXT("initial-2560x1080"), FIntPoint{2560, 1080}}};
            for (const FViewportCase& Case : Viewports)
            { TestTrue(*FString::Printf(TEXT("native %s capture"), Case.Name), Capture(World, *State,
                Case.Name, Case.Size, {TEXT("quest-title"), TEXT("quest-summary"), TEXT("quest-state"), TEXT("footer-date")})); }
            TestTrue(TEXT("all realized quest and objective row names fit their allocated native rows"),
                AllRealizedRowTextsFit(Root, FName{TEXT("quest-row-name")}, FName{TEXT("quest-row")})
                && AllRealizedRowTextsFit(Root, FName{TEXT("objective-row-name")}, FName{TEXT("objective-row")}));

            Slate.SetUserFocus(User, QuestTree->GetTree().ToSharedRef(), EFocusCause::Navigation);
            if (!TestTrue(TEXT("native quest tree accepts focus"), Slate.GetUserFocusedWidget(User) == QuestTree->GetTree())) { return; }
            const FName Initial = Model->Get_SelectedKey();
            TestTrue(TEXT("real Down traverses quest rows"), Key(EKeys::Down, User, true));
            Key(EKeys::Down, User, false);
            Pump();
            const FName Selected = Model->Get_SelectedKey();
            TestTrue(TEXT("native Down changes the keyed quest selection"), Selected != Initial && !Selected.IsNone()
                && Model->FindQuest(Selected) != nullptr);
            if (const auto* Quest = Model->FindQuest(Selected); Quest != nullptr)
            {
                const bool Before = Quest->Tracked;
                TestTrue(TEXT("real Enter is handled by quest tree"), Key(EKeys::Enter, User, true));
                Key(EKeys::Enter, User, false);
                Pump();
                const auto* After = Model->FindQuest(Selected);
                TestTrue(TEXT("Enter toggles selected quest tracking"), After != nullptr && After->Tracked != Before);
                TestTrue(TEXT("tracked quest native capture"), Capture(World, *State,
                    TEXT("tracked-1920x1080"), {1920, 1080}, {TEXT("quest-title"), TEXT("quest-state")}));
            }

            TestTrue(TEXT("Miscellaneous is a selectable keyed group"), QuestTree->TrySelectKey(FString{TEXT("miscellaneous")}, true));
            Pump();
            TestEqual(TEXT("Miscellaneous drives model selection"), Model->Get_SelectedKey(), FName{TEXT("miscellaneous")});
            const auto Summary = NativeText(Root, FName{TEXT("quest-summary")});
            TestTrue(TEXT("Miscellaneous collapses the inapplicable quest summary"), Summary.IsValid()
                && !EffectivelyVisible(Summary.ToSharedRef()));
            TestTrue(TEXT("Miscellaneous native capture"), Capture(World, *State,
                TEXT("miscellaneous-1920x1080"), {1920, 1080}, {TEXT("quest-title")}));
            const auto ObjectiveSelection = ObjectiveTree->GetSelectedKey();
            const auto* OwnerBefore = Model->Get_Quests().FindByPredicate([](const FCkJournalQuest& Quest)
            { return Quest.Miscellaneous; });
            if (!TestTrue(TEXT("Miscellaneous exposes first fixture errand and selected objective"),
                ObjectiveSelection.IsSet() && OwnerBefore != nullptr && OwnerBefore->Objectives.Num() == 1)) { return; }
            const FName OwnerKey = OwnerBefore->Key;
            const bool TrackedBefore = OwnerBefore->Tracked;
            Slate.SetUserFocus(User, ObjectiveTree->GetTree().ToSharedRef(), EFocusCause::Navigation);
            TestTrue(TEXT("real Enter is handled by objective tree"), Key(EKeys::Enter, User, true));
            Key(EKeys::Enter, User, false);
            Pump();
            const auto* OwnerAfter = Model->FindQuest(OwnerKey);
            TestTrue(TEXT("objective Enter toggles owning miscellaneous quest"), OwnerAfter != nullptr
                && OwnerAfter->Tracked != TrackedBefore && Model->Get_SelectedKey() == FName{TEXT("miscellaneous")});
            const uint64 BeforeMap = Model->Get_Revision();
            TestTrue(TEXT("real M reports selected miscellaneous map target"), Key(EKeys::M, User, true));
            Key(EKeys::M, User, false);
            Pump();
            TestTrue(TEXT("map target is fixture feedback without gameplay or pause mutation"),
                OwnerAfter != nullptr && Journal->Get_Summary().Contains(TEXT("Target:"))
                && Journal->Get_Summary().Contains(OwnerAfter->Title)
                && Model->Get_Revision() == BeforeMap && World->IsPaused()
                && Controller->bShowMouseCursor);

            const auto Stats = FindHeader(Root, TEXT("GENERAL STATS"));
            const auto System = FindHeader(Root, TEXT("SYSTEM"));
            const auto Quests = FindHeader(Root, TEXT("QUESTS"));
            if (!TestTrue(TEXT("three authored native tab headers exist"), Stats.IsValid()
                && System.IsValid() && Quests.IsValid())) { return; }
            TestTrue(TEXT("real E advances to Stats"), Key(EKeys::E, User, true));
            Key(EKeys::E, User, false);
            Pump();
            TestEqual(TEXT("E selects Stats without changing quest selection"), Model->Get_TabKey(), FName{TEXT("stats")});
            TestTrue(TEXT("real E advances to System"), Key(EKeys::E, User, true));
            Key(EKeys::E, User, false);
            Pump();
            TestEqual(TEXT("E selects System"), Model->Get_TabKey(), FName{TEXT("system")});
            TestTrue(TEXT("real Q returns to Stats"), Key(EKeys::Q, User, true));
            Key(EKeys::Q, User, false);
            Pump();
            TestEqual(TEXT("Q selects Stats"), Model->Get_TabKey(), FName{TEXT("stats")});
            TestTrue(TEXT("real Q returns to Quests"), Key(EKeys::Q, User, true));
            Key(EKeys::Q, User, false);
            Pump();
            TestTrue(TEXT("Q restores Quests and prior Miscellaneous selection"),
                Model->Get_TabKey() == FName{TEXT("quests")}
                && Model->Get_SelectedKey() == FName{TEXT("miscellaneous")});
            TestTrue(TEXT("real Stats header click"), Click(Stats.ToSharedRef(), User));
            Pump();
            TestEqual(TEXT("Stats tab becomes authoritative"), Model->Get_TabKey(), FName{TEXT("stats")});
            TestTrue(TEXT("Stats native capture"), Capture(World, *State,
                TEXT("stats-1920x1080"), {1920, 1080}, {TEXT("stats-summary"), TEXT("footer-date")}));
            TestTrue(TEXT("real System header click"), Click(System.ToSharedRef(), User));
            Pump();
            TestEqual(TEXT("System tab becomes authoritative"), Model->Get_TabKey(), FName{TEXT("system")});
            TestTrue(TEXT("System native capture"), Capture(World, *State,
                TEXT("system-1920x1080"), {1920, 1080}, {TEXT("system-summary"), TEXT("footer-date")}));
            TestTrue(TEXT("real Quests header click"), Click(Quests.ToSharedRef(), User));
            Pump();
            TestTrue(TEXT("tab return retains miscellaneous selection and native controls"),
                Model->Get_TabKey() == FName{TEXT("quests")}
                && Model->Get_SelectedKey() == FName{TEXT("miscellaneous")}
                && View->GetTree(TEXT("quest-list")) == QuestTree && View->GetTabs(TEXT("journal-tabs")) == Tabs);

            TestTrue(TEXT("fixture objective advances via host command"), Journal->Request_AdvanceSelected());
            TestTrue(TEXT("completed objective native capture"), Capture(World, *State,
                TEXT("completed-1920x1080"), {1920, 1080}, {TEXT("quest-title"), TEXT("quest-state")}));
            TestTrue(TEXT("long-summary quest selected"), QuestTree->TrySelectKey(FString{TEXT("bleak-falls-barrow")}, true));
            Pump();
            const uint64 BeforeNamedMap = Model->Get_Revision();
            TestTrue(TEXT("real M reports named quest map target"), Key(EKeys::M, User, true));
            Key(EKeys::M, User, false);
            Pump();
            TestTrue(TEXT("named map target changes only fixture feedback"),
                Journal->Get_Summary().Contains(TEXT("Bleak Falls Barrow"))
                && Model->Get_Revision() == BeforeNamedMap && World->IsPaused());
            TestTrue(TEXT("long summary native capture"), Capture(World, *State,
                TEXT("long-summary-1920x1080"), {1920, 1080}, {TEXT("quest-title"), TEXT("quest-summary")}));

            if (!TestTrue(TEXT("stress fixture publishes through host"), Journal->Request_ToggleStress())) { return; }
            Pump();
            TestEqual(TEXT("stress fixture has 130 quests"), Model->Get_Quests().Num(), 130);
            TestTrue(TEXT("stress tree has all keys but virtualizes native rows"), QuestTree->GetVisibleNodeCount() == 128
                && QuestTree->GetLiveRowCount() < 128);
            const auto NativeTree = QuestTree->GetTree();
            const auto StressNode = Model->FindQuest(FName{TEXT("fixture-quest-120")});
            const auto Collection = View->GetTree(TEXT("quest-list"));
            if (!TestTrue(TEXT("last stress quest and native tree exist"), StressNode != nullptr
                && NativeTree.IsValid() && Collection.IsValid())) { return; }
            TestTrue(TEXT("last stress key selects"), QuestTree->TrySelectKey(FString{TEXT("fixture-quest-120")}, true));
            const auto SelectedItems = NativeTree->GetSelectedItems();
            if (!TestTrue(TEXT("stress selection reaches a native tree item"), SelectedItems.Num() == 1)) { return; }
            NativeTree->RequestScrollIntoView(SelectedItems[0], static_cast<uint32>(User));
            for (int32 Frame = 0; Frame < 4; ++Frame) { Pump(); }
            const float Offset = NativeTree->GetScrollOffset();
            TestTrue(TEXT("native tree physically scrolls in stress fixture"), Offset > 0.0f);
            TestTrue(TEXT("stress scrolled native capture"), Capture(World, *State,
                TEXT("stress-scrolled-1920x1080"), {1920, 1080}, {TEXT("quest-title"), TEXT("quest-summary")}));
            TestTrue(TEXT("all scrolled realized quest row names fit their allocated native rows"),
                AllRealizedRowTextsFit(Root, FName{TEXT("quest-row-name")}, FName{TEXT("quest-row")}));

            const int64 Revision = View->GetRevision();
            const uint64 ModelRevision = Model->Get_Revision();
            const FName ModelSelection = Model->Get_SelectedKey();
            const TSharedRef<SWidget> AcceptedRoot = View->GetRegion(TEXT("main"));
            auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
            FString Markup, Style;
            if (!TestTrue(TEXT("journal source installed"), Plugin.IsValid()
                && FFileHelper::LoadFileToString(Markup, *FPaths::Combine(Plugin->GetBaseDir(),
                    TEXT("Resources/UiAuthoring/SkyrimJournalProof/SkyrimJournalProof.ui.html")))
                && FFileHelper::LoadFileToString(Style, *FPaths::Combine(Plugin->GetBaseDir(),
                    TEXT("Resources/UiAuthoring/SkyrimJournalProof/SkyrimJournalProof.ui.css"))))) { return; }
            TestFalse(TEXT("missing required tree rejects source"), Journal->Request_ApplySource(
                Markup.Replace(TEXT("id=\"quest-list\""), TEXT("id=\"missing-quest-list\"")), Style));
            TestTrue(TEXT("rejected source preserves model, view, tree, tabs, root, scroll and revision"),
                Journal->Get_AuthoredView() == View && View->GetRevision() == Revision
                && Model->Get_Revision() == ModelRevision && Model->Get_SelectedKey() == ModelSelection
                && View->GetTree(TEXT("quest-list")) == QuestTree && View->GetTabs(TEXT("journal-tabs")) == Tabs
                && View->GetRegion(TEXT("main")) == AcceptedRoot && NativeTree->GetScrollOffset() >= Offset - 1.0f
                && !Journal->Get_SourceError().IsEmpty());
            TestTrue(TEXT("compatible journal CSS reload accepts"), Journal->Request_ApplySource(Markup,
                Style.Replace(TEXT("font-size: 30px"), TEXT("font-size: 31px"))));
            Pump();
            TestTrue(TEXT("compatible reload retains tree, tabs, root, selection and scroll"),
                View->GetRevision() > Revision && View->GetTree(TEXT("quest-list")) == QuestTree
                && View->GetTabs(TEXT("journal-tabs")) == Tabs && View->GetRegion(TEXT("main")) == AcceptedRoot
                && Model->Get_SelectedKey() == ModelSelection && NativeTree->GetScrollOffset() >= Offset - 1.0f);
            TestTrue(TEXT("installed journal source restores"), Journal->Request_ApplySource(Markup, Style));

            State->HeldTree = QuestTree;
            TestTrue(TEXT("Escape closes owned journal"), Key(EKeys::Escape, User, true));
            Key(EKeys::Escape, User, false);
            Pump();
            TestTrue(TEXT("Escape restores pause, cursor, focus and releases authored owner"),
                !Journal->IsJournalOpen() && !World->IsPaused()
                && Controller->bShowMouseCursor == State->bOriginalCursor
                && State->OriginalFocus.IsValid() && Slate.GetUserFocusedWidget(User) == State->OriginalFocus.Pin()
                && View->IsOwnerReleased());
            TestTrue(TEXT("closed journal native capture"), Capture(World, *State,
                TEXT("closed-1920x1080"), {1920, 1080}));
            const uint64 BeforeHeld = Model->Get_Revision();
            State->HeldTree->TrySelectKey(FString{TEXT("before-the-storm")}, true);
            TestEqual(TEXT("held released tree cannot mutate model"), Model->Get_Revision(), BeforeHeld);
            if (!TestTrue(TEXT("viewport refocuses before real J reopen"), FocusViewport(World, User))) { return; }
            TestTrue(TEXT("real J reopens journal"), Key(EKeys::J, User, true));
            Key(EKeys::J, User, false);
            Pump();
            TestTrue(TEXT("reopened journal pauses world"), Journal->IsJournalOpen() && World->IsPaused());
            if (Journal->IsJournalOpen())
            {
                TestTrue(TEXT("real J closes journal"), Key(EKeys::J, User, true));
                Key(EKeys::J, User, false);
            }
            TestTrue(TEXT("real J releases journal-owned pause"),
                !Journal->IsJournalOpen() && !World->IsPaused());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            auto* Controller = Player(World);
            auto* LocalPlayer = IsValid(Controller) ? Controller->GetLocalPlayer() : nullptr;
            auto* Layout = IsValid(LocalPlayer)
                ? LocalPlayer->GetSubsystem<UCk_UI_Layout_Subsystem_UE>() : nullptr;
            auto* Layer = IsValid(Layout) ? Layout->Get_Layer(
                FGameplayTag::RequestGameplayTag(TEXT("CkTests.UI.Layer.PauseMenuProof"))) : nullptr;
            return IsValid(Layer) && Layer->GetActiveWidget() == nullptr;
        }), 5.0, TEXT("journal layer completes close transition before reopening")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Journal = Host(World);
            auto* Controller = Player(World);
            if (!TestNotNull(TEXT("journal after close transition"), Journal)
                || !TestNotNull(TEXT("controller after close transition"), Controller)) { return; }
            if (!TestTrue(TEXT("external pause is established before journal reopens"),
                Controller->SetPause(true) && World->IsPaused())) { return; }
            TestTrue(TEXT("externally paused journal opens"), Journal->Request_Open());
            Journal->Request_Close();
            TestTrue(TEXT("closing preserves external pause"), World->IsPaused());
            Controller->SetPause(false);
            Journal->Request_Shutdown();
            TestFalse(TEXT("shutdown rejects opening"), Journal->Request_Open());
            TestFalse(TEXT("shutdown rejects fixture commands"), Journal->Request_AdvanceSelected()
                || Journal->Request_ToggleStress() || Journal->Request_Reset());
            TestTrue(TEXT("debug overlay values and flags restore before PIE teardown"), State->RestoreDebug());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([State]() -> bool
        {
            State->Restore();
            return !State->HeldView.IsValid() || State->HeldView->IsOwnerReleased();
        }), TEXT("journal releases retained view and restores capture settings")));
    return true;
}

#endif
