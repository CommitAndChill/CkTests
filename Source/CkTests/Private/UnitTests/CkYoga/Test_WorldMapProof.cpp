#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkSlateLayout/CkFlexText.h"
#include "CkSlateLayout/CkUiWorldMap.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkWidgets/Authoring/CkAuthoredSession.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Layout/SlateRect.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Styling/CoreStyle.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWindow.h"
#include <limits>

namespace ck_world_map_proof
{
    template <typename TWidget>
    auto Find(const TSharedRef<SWidget>& Root, const FString& Type, FName Tag) -> TSharedPtr<TWidget>
    {
        if (Root->GetTypeAsString() == Type && Root->GetTag() == Tag) { return StaticCastSharedRef<TWidget>(Root); }
        const auto* Children = Root->GetChildren();
        for (int32 I = 0; Children != nullptr && I < Children->Num(); ++I)
        { if (const auto Match = Find<TWidget>(ConstCastSharedRef<SWidget>(Children->GetChildAt(I)), Type, Tag); Match.IsValid()) { return Match; } }
        return {};
    }
    auto Tagged(const TSharedRef<SWidget>& Root, FName Tag) -> TSharedPtr<SWidget>
    {
        if (Root->GetTag() == Tag) { return Root; }
        const auto* Children = Root->GetChildren();
        for (int32 I = 0; Children != nullptr && I < Children->Num(); ++I)
        { if (const auto Match = Tagged(ConstCastSharedRef<SWidget>(Children->GetChildAt(I)), Tag); Match.IsValid()) { return Match; } }
        return {};
    }
    auto TextFits(const TSharedRef<SWidget>& Root, FName Tag, FName ParentTag, double Inset) -> bool
    {
        const auto Text = Find<SCkFlexText>(Root, TEXT("SCkFlexText"), Tag);
        const auto Parent = Tagged(Root, ParentTag);
        if (!Text.IsValid() || !Parent.IsValid() || Text->GetText().IsEmpty()) { return false; }
        const FGeometry Geometry = Text->GetCachedGeometry();
        const FVector2D Space = Geometry.GetLocalSize();
        if (!FMath::IsFinite(Geometry.Scale) || Geometry.Scale <= 0 || Space.X <= 0 || Space.Y <= 0) { return false; }
        const FVector2D Needed = Text->Measure(static_cast<float>(Space.X), YGMeasureModeExactly,
            YGUndefined, YGMeasureModeUndefined, Geometry.Scale);
        const FSlateRect A = Text->GetCachedGeometry().GetRenderBoundingRect();
        const FSlateRect B = Parent->GetCachedGeometry().GetRenderBoundingRect();
        const double Padding = Inset * Geometry.Scale;
        return FMath::IsFinite(Needed.Y) && Needed.Y <= Space.Y + 0.5
            && A.Left >= B.Left + Padding - 1 && A.Right <= B.Right - Padding + 1
            && A.Top >= B.Top + Padding - 1 && A.Bottom <= B.Bottom - Padding + 1;
    }
    auto GetterObject(UObject* Object, FName Name) -> UObject*
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        auto* Return = Function != nullptr ? CastField<FObjectProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 1) { return nullptr; }
        FStructOnScope Args{Function}; Object->ProcessEvent(Function, Args.GetStructMemory());
        return Return->GetObjectPropertyValue_InContainer(Args.GetStructMemory());
    }
    template <typename TProperty, typename TValue>
    auto Getter(UObject* Object, FName Name, TValue Default) -> TValue
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        auto* Return = Function != nullptr ? CastField<TProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 1) { return Default; }
        FStructOnScope Args{Function}; Object->ProcessEvent(Function, Args.GetStructMemory());
        return Return->GetPropertyValue_InContainer(Args.GetStructMemory());
    }
    auto Call(UObject* Object, FName Name, const TArray<FString>& Strings = {}, const TArray<bool>& Bools = {}) -> bool
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        auto* Return = Function != nullptr ? CastField<FBoolProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != Strings.Num() + Bools.Num() + 1) { return false; }
        FStructOnScope Args{Function}; int32 SI = 0, BI = 0;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm)) { continue; }
            if (auto* String = CastField<FStrProperty>(Property))
            { if (!Strings.IsValidIndex(SI)) { return false; } String->SetPropertyValue_InContainer(Args.GetStructMemory(), Strings[SI++]); }
            else if (auto* Flag = CastField<FBoolProperty>(Property))
            { if (!Bools.IsValidIndex(BI)) { return false; } Flag->SetPropertyValue_InContainer(Args.GetStructMemory(), Bools[BI++]); }
            else { return false; }
        }
        if (SI != Strings.Num() || BI != Bools.Num()) { return false; }
        Object->ProcessEvent(Function, Args.GetStructMemory());
        return Return->GetPropertyValue_InContainer(Args.GetStructMemory());
    }
    auto Screen(UWorld* World) -> UUserWidget*
    {
        auto* Player = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
        return Cast<UUserWidget>(GetterObject(Player, TEXT("Get_Screen")));
    }
    auto Session(UWorld* World) -> UCk_UI_AuthoredSession_UE*
    { return Cast<UCk_UI_AuthoredSession_UE>(GetterObject(Screen(World), TEXT("Get_Session"))); }
    auto Registry(UWorld* World) -> UCkGym_Registry_Subsystem*
    {
        auto* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }
    auto Map(UWorld* World) -> TSharedPtr<SCkUiWorldMap>
    {
        auto* Owner = Session(World); const auto View = IsValid(Owner) ? Owner->Get_View() : nullptr;
        return View.IsValid() ? Find<SCkUiWorldMap>(View->GetRegion(TEXT("main")),
            TEXT("SCkUiWorldMap"), TEXT("map-view")) : nullptr;
    }
    auto Selected(UObject* Widget) -> FString
    { return Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}); }
    auto SelectionCount(UObject* Widget) -> int32
    { return Getter<FIntProperty, int32>(Widget, TEXT("Get_SelectionCount"), -1); }
    auto Near(FVector2D A, FVector2D B, double Epsilon = 0.01) -> bool
    { return (A - B).Size() < Epsilon; }
    auto Buttons(bool Held) -> const TSet<FKey>&
    {
        static const TSet<FKey> None;
        static const TSet<FKey> Left{EKeys::LeftMouseButton};
        return Held ? Left : None;
    }
    struct FState
    {
        FState()
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            StartupMode = Settings->StartupMode; PanelMode = Settings->ControlPanelMode;
            DefaultGym = Settings->DefaultGymName; LastGym = Settings->LastGymName;
            Recent = Settings->RecentGymNames;
            Settings->StartupMode = ECkGym_StartupMode::Cycler;
            Settings->ControlPanelMode = ECkGym_ControlPanel_Mode::Hidden;
        }
        ~FState() { Restore(); }
        void Restore()
        {
            if (Restored) { return; }
            if (const auto Window = OriginalWindow.Pin(); Window.IsValid()) { Window->Resize(OriginalSize); }
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = StartupMode; Settings->ControlPanelMode = PanelMode;
            Settings->DefaultGymName = DefaultGym; Settings->LastGymName = LastGym;
            Settings->RecentGymNames = Recent; Settings->SaveConfig(); Restored = true;
        }
        ECkGym_StartupMode StartupMode;
        ECkGym_ControlPanel_Mode PanelMode;
        FString DefaultGym, LastGym;
        TArray<FString> Recent;
        TWeakPtr<SWindow> OriginalWindow;
        FVector2D OriginalSize = FVector2D::ZeroVector;
        bool Restored = false;
    };
    auto Capture(UWorld* World, FState& State, const FString& Name, FIntPoint Desired,
        TArray<FColor>* OutPixels = nullptr) -> bool
    {
        auto* Client = IsValid(World) ? World->GetGameViewport() : nullptr;
        const auto Target = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!Target.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get(); const auto Window = Slate.FindWidgetWindow(Target.ToSharedRef());
        if (!Window.IsValid()) { return false; }
        if (!State.OriginalWindow.IsValid())
        { State.OriginalWindow = Window; State.OriginalSize = Window->GetClientSizeInScreen(); }
        TArray<FColor> Pixels; FIntVector Size = FIntVector::ZeroValue;
        for (int32 Attempt = 0; Attempt < 5; ++Attempt)
        {
            Slate.PumpMessages(); Slate.Tick(); Slate.Tick(); Pixels.Reset();
            if (!Slate.TakeScreenshot(Target.ToSharedRef(), Pixels, Size) || Size.X <= 0 || Size.Y <= 0
                || Pixels.Num() < Size.X * Size.Y) { return false; }
            if (Size.X == Desired.X && Size.Y == Desired.Y) { break; }
            Window->Resize(Window->GetClientSizeInScreen() + FVector2D(Desired.X - Size.X, Desired.Y - Size.Y));
        }
        if (Size.X != Desired.X || Size.Y != Desired.Y) { return false; }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/WorldMapProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const bool Saved = FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, Name + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
        if (Saved && OutPixels != nullptr) { *OutPixels = MoveTemp(Pixels); }
        return Saved;
    }
    auto Click(const TSharedPtr<SCkUiWorldMap>& MapWidget, const FString& Key, int32 User) -> bool
    {
        if (!MapWidget.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(MapWidget.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
        Window->BringToFront(true); Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        const auto Local = MapWidget->GetMarkerLocalPosition(Key);
        if (!Local.IsSet()) { return false; }
        const FVector2D P = MapWidget->GetCachedGeometry().LocalToAbsolute(Local.GetValue());
        if (!MapWidget->GetCachedGeometry().IsUnderLocation(P)) { return false; }
        const TArray<TSharedRef<SWindow>> Windows{Window.ToSharedRef()};
        if (!Slate.LocateWindowUnderMouse(P, Windows, false, User).ContainsWidget(MapWidget.Get())) { return false; }
        Slate.SetCursorPos(P);
        Slate.ProcessMouseMoveEvent(FPointerEvent(static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            P, P, Buttons(false), EKeys::Invalid, 0, FModifierKeysState{}), true);
        const FPointerEvent Down{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            P, P, Buttons(true), EKeys::LeftMouseButton, 0, FModifierKeysState{}};
        const FPointerEvent Up{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            P, P, Buttons(false), EKeys::LeftMouseButton, 0, FModifierKeysState{}};
        const bool Pressed = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        const bool Released = Slate.ProcessMouseButtonUpEvent(Up);
        Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        return Pressed && Released;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkWorldMap_PIE,
    "Ck.UiAuthoring.WorldMap.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkWorldMap_PIE::RunTest(const FString&) -> bool
{
    using namespace ck_world_map_proof;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("World map PIE proof requires Slate.")); return false; }
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* R = Registry(ck::auto_test::net::Get_ServerWorld());
            return IsValid(R) && R->Find_GymIndexByName(TEXT("UI: World Map")) != INDEX_NONE;
        }), 15.0, TEXT("World map production gym registers")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
        {
            auto* R = Registry(World);
            if (!TestNotNull(TEXT("Gym registry exists"), R)) { return; }
            const int32 Index = R->Find_GymIndexByName(TEXT("UI: World Map"));
            if (!TestTrue(TEXT("World map gym entry exists"), R->Get_Entries().IsValidIndex(Index))) { return; }
            const auto& Entry = R->Get_Entries()[Index];
            TestEqual(TEXT("World map gym category"), Entry.Category, FString(TEXT("CkUI")));
            TestTrue(TEXT("World map gym uses its script game mode"), IsValid(Entry.GameModeClass.Get())
                && Entry.GameModeClass.Get()->GetName().Contains(TEXT("Ck_WorldMap_GameMode")));
            R->Request_TravelToGym(Index);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            auto* Widget = Screen(World); auto* Owner = Session(World);
            return IsValid(Widget) && ((IsValid(Owner) && Owner->Get_IsUsable() && Owner->Get_Revision() > 0)
                || !Getter<FStrProperty, FString>(Widget, TEXT("Get_StartupError"), {}).IsEmpty());
        }), 30.0, TEXT("World map screen mounts or reports startup error")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Widget = Screen(World); auto* Owner = Session(World);
            const FString Startup = Getter<FStrProperty, FString>(Widget, TEXT("Get_StartupError"), {});
            if (!Startup.IsEmpty()) { AddError(FString::Printf(TEXT("World map startup: %s"), *Startup)); return; }
            if (!TestNotNull(TEXT("Production world map widget exists"), Widget)
                || !TestNotNull(TEXT("Production authored session exists"), Owner)) { return; }
            const auto View = Owner->Get_View();
            if (!TestTrue(TEXT("World map authored view exists"), View.IsValid())) { return; }
            const auto MapWidget = Map(World);
            if (!TestTrue(TEXT("Production authored map control mounts"), MapWidget.IsValid())) { return; }
            const auto Root = View->GetRegion(TEXT("main"));
            const auto Heading = Find<SCkFlexText>(Root, TEXT("SCkFlexText"), TEXT("map-title"));
            const auto Body = Find<SCkFlexText>(Root, TEXT("SCkFlexText"), TEXT("selected-description"));
            const auto DefaultFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12).GetCompositeFont();
            TestTrue(TEXT("Display and body text use two registered font families"),
                Heading.IsValid() && Body.IsValid()
                && Heading->GetFont().GetCompositeFont() != DefaultFont
                && Body->GetFont().GetCompositeFont() != DefaultFont
                && Heading->GetFont().GetCompositeFont() != Body->GetFont().GetCompositeFont());
            TestTrue(TEXT("All seeded markers reach the native collection"), MapWidget->GetMarkerCount() == 12
                && MapWidget->GetMarkerLocalPosition(TEXT("quest-meridian")).IsSet());
            TestEqual(TEXT("Initial selection"), Selected(Widget), FString(TEXT("quest-meridian")));
            TestTrue(TEXT("Gym starts with usable input"), Owner->Get_IsInputActive());
            if (!GUsingNullRHI)
            {
                for (const auto Size : {FIntPoint{1280, 720}, FIntPoint{1920, 1080}, FIntPoint{2560, 1080}})
                {
                    TestTrue(*FString::Printf(TEXT("World map capture %dx%d"), Size.X, Size.Y),
                        Capture(World, *State, FString::Printf(TEXT("world-map-clean-%dx%d"), Size.X, Size.Y), Size));
                    const auto Shell = Tagged(Root, TEXT("screen-shell"));
                    const auto Frame = Tagged(Root, TEXT("map-frame"));
                    TestTrue(TEXT("Map and surrounding frame have positive viewport geometry"),
                        Shell.IsValid() && Frame.IsValid() && MapWidget->GetCachedGeometry().GetLocalSize().X > 0
                        && MapWidget->GetCachedGeometry().GetLocalSize().Y > 0);
                    TestTrue(*FString::Printf(TEXT("Mission text measures inside padded card at %dx%d"), Size.X, Size.Y),
                        TextFits(Root, TEXT("selected-title"), TEXT("mission-card"), 16)
                        && TextFits(Root, TEXT("selected-description"), TEXT("mission-card"), 16));
                }
            }
            TestTrue(TEXT("Routed click selects the service from the production screen"),
                Click(MapWidget, TEXT("service-clinic"), Owner->Get_SlateUserIndex())
                && Selected(Widget) == TEXT("service-clinic") && SelectionCount(Widget) == 1);
            TestFalse(TEXT("Disabled marker cannot be selected"), Call(Widget, TEXT("Request_Select"), {TEXT("service-relay")}));
            TestFalse(TEXT("Missing marker cannot be selected"), Call(Widget, TEXT("Request_Select"), {TEXT("missing")}));
            TestTrue(TEXT("Invalid selection preserves model"), Selected(Widget) == TEXT("service-clinic")
                && SelectionCount(Widget) == 1);
            TestTrue(TEXT("Service filter retains selected service"),
                Call(Widget, TEXT("Request_SetFilter"), {TEXT("Services")})
                && Selected(Widget) == TEXT("service-clinic")
                && !MapWidget->GetMarkerLocalPosition(TEXT("quest-meridian")).IsSet());
            if (!GUsingNullRHI)
            { TestTrue(TEXT("Filtered native capture"), Capture(World, *State,
                TEXT("world-map-filter-services"), {1920, 1080})); }
            TestTrue(TEXT("Quest filter reconciles selection to an eligible quest"),
                Call(Widget, TEXT("Request_SetFilter"), {TEXT("Quests")})
                && Selected(Widget).StartsWith(TEXT("quest-"))
                && !MapWidget->GetMarkerLocalPosition(TEXT("service-clinic")).IsSet());
            TestFalse(TEXT("Filtered service cannot be selected"),
                Call(Widget, TEXT("Request_Select"), {TEXT("service-clinic")}));
            TestTrue(TEXT("Reset restores original model and camera"),
                Call(Widget, TEXT("Request_ResetFixture"))
                && Selected(Widget) == TEXT("quest-meridian")
                && MapWidget->GetZoom() == 1.0f);
            TestTrue(TEXT("Map camera session command zooms live control"),
                Owner->TrySetMapCamera(TEXT("map-view"), FVector2D(0.5, 0.5), 2)
                && MapWidget->GetZoom() == 2);
            FVector2D Camera; float Zoom = 0;
            TestTrue(TEXT("Camera getter returns the live retained control"),
                Owner->TryGetMapCamera(TEXT("map-view"), Camera, Zoom) && Zoom == 2);
            TestTrue(TEXT("Invalid session camera request leaves center and zoom intact"),
                !Owner->TrySetMapCamera(TEXT("map-view"),
                    FVector2D(std::numeric_limits<float>::quiet_NaN(), 0.5), 3)
                && !Owner->TrySetMapCamera(TEXT("map-view"), FVector2D(0.5, 0.5), 0.5f)
                && Near(MapWidget->GetCameraCenter(), Camera) && MapWidget->GetZoom() == Zoom);
            TestFalse(TEXT("Disabled service cannot be revealed"),
                Owner->TryRevealMapMarker(TEXT("map-view"), TEXT("service-relay")));
            TestTrue(TEXT("Reveal command centers an eligible marker"),
                Owner->TryRevealMapMarker(TEXT("map-view"), TEXT("quest-quiet"))
                && MapWidget->GetCameraCenter().X <= 0.35
                && MapWidget->GetCameraCenter().Y <= 0.38
                && MapWidget->GetMarkerLocalPosition(TEXT("quest-quiet")).IsSet());
            auto& Slate = FSlateApplication::Get();
            const int32 User = Owner->Get_SlateUserIndex();
            Slate.SetUserFocus(User, MapWidget.ToSharedRef(), EFocusCause::SetDirectly);
            const FVector2D BeforeKeyPan = MapWidget->GetCameraCenter();
            const FKeyEvent PanKey(EKeys::D, FModifierKeysState{}, static_cast<uint32>(User), false, 0, 0);
            Slate.ProcessKeyDownEvent(PanKey); Slate.ProcessKeyUpEvent(PanKey);
            TestTrue(TEXT("Routed keyboard pan moves the live map camera"),
                MapWidget->GetCameraCenter().X > BeforeKeyPan.X + 0.01);
            if (!GUsingNullRHI)
            { TestTrue(TEXT("Zoomed and panned native capture"), Capture(World, *State,
                TEXT("world-map-zoomed-panned"), {1920, 1080})); }
            TestTrue(TEXT("Route command publishes a selected route"),
                Call(Widget, TEXT("Request_ToggleRoute")));
            if (!GUsingNullRHI)
            { TestTrue(TEXT("Route native capture"), Capture(World, *State,
                TEXT("world-map-route"), {1920, 1080})); }
            TestTrue(TEXT("Long title publishes through the real model"), Call(Widget, TEXT("Request_LongLabel")));
            Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
            TestTrue(TEXT("Long mission title remains within measured card height and insets"),
                TextFits(Root, TEXT("selected-title"), TEXT("mission-card"), 16));
            if (!GUsingNullRHI)
            { TestTrue(TEXT("Long label native capture"), Capture(World, *State,
                TEXT("world-map-long-label"), {1920, 1080})); }
            const int64 BeforeReload = Owner->Get_Revision();
            const FVector2D BeforeCenter = MapWidget->GetCameraCenter(); const float BeforeZoom = MapWidget->GetZoom();
            TestTrue(TEXT("Compatible source reload retains control and camera"),
                Call(Widget, TEXT("Request_Reload")) && Owner->Get_Revision() > BeforeReload
                && Map(World) == MapWidget && Near(MapWidget->GetCameraCenter(), BeforeCenter)
                && MapWidget->GetZoom() == BeforeZoom);
            TestTrue(TEXT("Removing selected marker reconciles model selection"),
                Call(Widget, TEXT("Request_RemoveSelected"))
                && MapWidget->GetMarkerCount() == 11
                && Selected(Widget) != TEXT("quest-meridian"));
            TestTrue(TEXT("Reinsertion and reordering restore all stable markers"),
                Call(Widget, TEXT("Request_ReinsertAndReorder"))
                && MapWidget->GetMarkerCount() == 12
                && MapWidget->GetMarkerLocalPosition(TEXT("quest-meridian")).IsSet());
            TestTrue(TEXT("Empty fixture clears all native markers and selection"),
                Call(Widget, TEXT("Request_EmptyFixture"))
                && MapWidget->GetMarkerCount() == 0 && Selected(Widget).IsEmpty());
            if (!GUsingNullRHI)
            { TestTrue(TEXT("Empty native map capture"), Capture(World, *State,
                TEXT("world-map-empty"), {1920, 1080})); }
            TestTrue(TEXT("Fixture reset recovers from empty state"),
                Call(Widget, TEXT("Request_ResetFixture")) && MapWidget->GetMarkerCount() == 12);
            const int32 BeforeInvalid = SelectionCount(Widget);
            TestFalse(TEXT("Duplicate marker fixture rejects before publishing"),
                Call(Widget, TEXT("Request_TryInvalidMarkers")));
            TestTrue(TEXT("Rejected duplicate leaves complete live model"), MapWidget->GetMarkerCount() == 12
                && SelectionCount(Widget) == BeforeInvalid);
            TestTrue(TEXT("Hiding ends map input"),
                Call(Widget, TEXT("Request_Show"), {}, {false}) && !Owner->Get_IsInputActive());
            TestFalse(TEXT("Hidden map rejects camera changes"), Owner->TryZoomMap(TEXT("map-view"), 1.2f));
            TestTrue(TEXT("Showing restores map input"),
                Call(Widget, TEXT("Request_Show"), {}, {true}) && Owner->Get_IsInputActive());
            Owner->Request_Release();
            TestTrue(TEXT("Released retained control cannot mutate camera"),
                View->IsOwnerReleased() && !MapWidget->TryZoomBy(1.2f) && MapWidget->GetOwnedCapture().IsEmpty());
            if (!GUsingNullRHI)
            { TestTrue(TEXT("Capture after owner teardown"), Capture(World, *State,
                TEXT("world-map-capture-teardown"), {1920, 1080})); }
            State->Restore();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
