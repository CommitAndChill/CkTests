#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkSlateLayout/CkUiHudControls.h"
#include "CkSlateLayout/CkUiRadialSelector.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkWidgets/Authoring/CkAuthoredSession.h"
#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "RHIGlobals.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Types/SlateAttributeMetaData.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace ck_dark_fantasy_proof
{
    auto ImageSize(const TSharedPtr<SImage>& InImage) -> FVector2D
    {
        if (!InImage.IsValid()) { return FVector2D::ZeroVector; }
        FSlateAttributeMetaData::UpdateAllAttributes(*InImage,
            FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidation);
        InImage->Invalidate(EInvalidateWidgetReason::Layout);
        InImage->SlatePrepass(1.0f);
        return InImage->GetDesiredSize();
    }

    template <typename TWidget>
    auto Find(const TSharedRef<SWidget>& InRoot, const FString& InType) -> TSharedPtr<TWidget>
    {
        if (InRoot->GetTypeAsString() == InType) { return StaticCastSharedRef<TWidget>(InRoot); }
        const auto* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const auto Found = Find<TWidget>(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InType);
            if (Found.IsValid()) { return Found; }
        }
        return {};
    }

    auto Image(UTexture2D* InTexture, const FString& InName = TEXT("portrait")) -> FCk_UI_AuthoredImage
    {
        FCk_UI_AuthoredImage Result{InName};
        Result.Set_Texture(InTexture);
        return Result;
    }

    auto Config(UTexture2D* InTexture) -> FCk_UI_AuthoredSession_Config
    {
        FCk_UI_AuthoredSession_Config Result;
        Result.Set_Images({Image(InTexture)});
        Result.Set_Number({FCk_UI_AuthoredNumber{TEXT("health"), 0.75f}});
        Result.Set_RegisterHudControls(true);
        Result.Set_RegisterRadialControls(true);
        return Result;
    }

    auto Markup() -> FString
    {
        return TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\">"
            "<hud-graphic id=\"portrait\" image-bind=\"portrait\"/>"
            "<hud-meter id=\"health-meter\" value-bind=\"health\"/>"
            "</column></region></ui>");
    }

    auto Registry(UWorld* InWorld) -> UCkGym_Registry_Subsystem*
    {
        auto* Instance = IsValid(InWorld) ? InWorld->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    template <typename TProperty, typename TValue>
    auto Getter(UObject* InObject, FName InName, TValue InDefault) -> TValue
    {
        auto* Function = IsValid(InObject) ? InObject->FindFunction(InName) : nullptr;
        const auto* Return = Function != nullptr ? CastField<TProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 1) { return InDefault; }
        TArray<uint8> Params; Params.SetNumZeroed(Function->ParmsSize);
        Function->InitializeStruct(Params.GetData());
        InObject->ProcessEvent(Function, Params.GetData());
        const TValue Result = Return->GetPropertyValue_InContainer(Params.GetData());
        Function->DestroyStruct(Params.GetData());
        return Result;
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

    auto Hud(UWorld* InWorld) -> UUserWidget*
    {
        auto* Player = IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr;
        return Cast<UUserWidget>(ObjectGetter(Player, TEXT("Get_Hud")));
    }

    auto Session(UWorld* InWorld) -> UCk_UI_AuthoredSession_UE*
    { return Cast<UCk_UI_AuthoredSession_UE>(ObjectGetter(Hud(InWorld), TEXT("Get_Session"))); }

    auto Pump() -> void
    { auto& Slate = FSlateApplication::Get(); Slate.PumpMessages(); Slate.Tick(); Slate.Tick(); }

    struct FPieState
    {
        FPieState()
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            StartupMode = Settings->StartupMode; PanelMode = Settings->ControlPanelMode;
            DefaultGym = Settings->DefaultGymName; LastGym = Settings->LastGymName; RecentGyms = Settings->RecentGymNames;
            Settings->StartupMode = ECkGym_StartupMode::Cycler;
            Settings->ControlPanelMode = ECkGym_ControlPanel_Mode::Hidden;
        }
        ~FPieState() { Restore(); }
        auto Restore() -> void
        {
            if (Restored) { return; }
            if (const auto Window = OriginalWindow.Pin(); Window.IsValid()) { Window->Resize(OriginalWindowSize); }
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = StartupMode; Settings->ControlPanelMode = PanelMode;
            Settings->DefaultGymName = DefaultGym; Settings->LastGymName = LastGym; Settings->RecentGymNames = RecentGyms;
            Settings->SaveConfig();
            Restored = true;
        }
        ECkGym_StartupMode StartupMode;
        ECkGym_ControlPanel_Mode PanelMode;
        FString DefaultGym, LastGym;
        TArray<FString> RecentGyms;
        TWeakPtr<SWindow> OriginalWindow;
        FVector2D OriginalWindowSize = FVector2D::ZeroVector;
        TWeakObjectPtr<UCk_UI_AuthoredSession_UE> Owner;
        TSharedPtr<FCkUiView> View;
        TSharedPtr<SCkUiRadialSelector> Wheel;
        bool Restored = false;
    };

    auto Capture(UWorld* InWorld, FPieState& InState, const FString& InName, FIntPoint InDesired) -> bool
    {
        auto* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const auto Target = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!Target.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Target.ToSharedRef());
        if (!Window.IsValid()) { return false; }
        if (!InState.OriginalWindow.IsValid())
        { InState.OriginalWindow = Window; InState.OriginalWindowSize = Window->GetClientSizeInScreen(); }
        TArray<FColor> Pixels; FIntVector Size = FIntVector::ZeroValue;
        for (int32 Attempt = 0; Attempt < 5; ++Attempt)
        {
            Pump(); Pixels.Reset();
            if (!Slate.TakeScreenshot(Target.ToSharedRef(), Pixels, Size) || Size.X <= 0 || Size.Y <= 0
                || Pixels.Num() < Size.X * Size.Y)
            {
                UE_LOG(LogTemp, Warning, TEXT("Dark fantasy capture %s failed: requested %dx%d, actual %dx%d, pixels %d"),
                    *InName, InDesired.X, InDesired.Y, Size.X, Size.Y, Pixels.Num());
                return false;
            }
            if (Size.X == InDesired.X && Size.Y == InDesired.Y) { break; }
            Window->Resize(Window->GetClientSizeInScreen() + FVector2D(InDesired.X - Size.X, InDesired.Y - Size.Y));
        }
        if (Size.X != InDesired.X || Size.Y != InDesired.Y)
        {
            UE_LOG(LogTemp, Warning, TEXT("Dark fantasy capture %s could not resize: requested %dx%d, actual %dx%d, client %.0fx%.0f"),
                *InName, InDesired.X, InDesired.Y, Size.X, Size.Y,
                Window->GetClientSizeInScreen().X, Window->GetClientSizeInScreen().Y);
            return false;
        }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/DarkFantasyHudProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, InName + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkDarkFantasy_ImageAdmission,
    "Ck.UiAuthoring.DarkFantasy.ReflectedImageAdmissionAndRetention",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkDarkFantasy_ImageAdmission::RunTest(const FString&) -> bool
{
    using namespace ck_dark_fantasy_proof;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Authored image proof requires Slate.")); return false; }
    // CK emits these expected admission diagnostics through two categories when debugging is enabled.
    // Match suppression also supports configurations that compile diagnostic ensures out.
    for (const TCHAR* Diagnostic : {TEXT("Authored image requires a unique identifier"),
        TEXT("Authored image requires a valid texture"), TEXT("Authored image replacement requires a valid texture"),
        TEXT("Authored image replacement requires a declared image"),
        TEXT("Authored image requires positive texture dimensions"),
        TEXT("Authored image replacement requires positive texture dimensions")})
    { AddExpectedError(Diagnostic, EAutomationExpectedErrorFlags::Contains, -1); }
    TStrongObjectPtr<UTexture2D> Texture{UTexture2D::CreateTransient(8, 8)};
    TStrongObjectPtr<UTexture2D> Replacement{UTexture2D::CreateTransient(16, 4)};
    TStrongObjectPtr<UTexture2D> DefaultTexture{NewObject<UTexture2D>()};
    if (!TestTrue(TEXT("Real transient image resources exist"), IsValid(Texture.Get()) && IsValid(Replacement.Get()))) { return false; }
    TArray<FString> Errors;
    const auto Reject = [this, &Errors, &Texture](const FCk_UI_AuthoredSession_Config& Candidate)
    {
        TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Owner{NewObject<UCk_UI_AuthoredSession_UE>()};
        ON_SCOPE_EXIT { Owner->Request_Release(); };
        TestFalse(TEXT("Invalid complete declaration rejects"), Owner->TryInitialize(Candidate, Errors));
        float Value = -1.0f;
        TestTrue(TEXT("Rejected admission publishes no partial view or scalar state"), !Errors.IsEmpty()
            && !Owner->Get_IsUsable() && !Owner->Get_View().IsValid() && Owner->Get_Revision() == 0
            && !Owner->TryGetNumber(TEXT("health"), Value) && !Owner->TrySetImage(TEXT("portrait"), Texture.Get()));
        TestTrue(TEXT("Rejected admission permits a complete valid retry"), Owner->TryInitialize(Config(Texture.Get()), Errors)
            && Owner->TryReload(Markup(), TEXT(""), Errors));
    };
    Reject(Config(nullptr));
    Reject(Config(DefaultTexture.Get()));
    auto Duplicate = Config(Texture.Get());
    Duplicate.Set_Images({Image(Texture.Get()), Image(Replacement.Get())});
    Reject(Duplicate);
    auto CrossType = Config(Texture.Get());
    CrossType.Set_Text({FCk_UI_AuthoredText{TEXT("portrait"), FText::FromString(TEXT("collision"))}});
    Reject(CrossType);
    auto NumberCollision = Config(Texture.Get());
    NumberCollision.Set_Images({Image(Texture.Get(), TEXT("health"))});
    Reject(NumberCollision);

    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Owner{NewObject<UCk_UI_AuthoredSession_UE>()};
    ON_SCOPE_EXIT { Owner->Request_Release(); };
    if (!TestTrue(TEXT("Reflected image and HUD controls admit through production session"),
        Owner->TryInitialize(Config(Texture.Get()), Errors) && Owner->TryReload(Markup(), TEXT(""), Errors))) { return false; }
    const auto View = Owner->Get_View();
    if (!TestTrue(TEXT("Accepted native view exists"), View.IsValid())) { return false; }
    const auto Root = View->GetRegion(TEXT("main"));
    const auto Graphic = Find<SImage>(Root, TEXT("SImage"));
    const auto Meter = Find<SCkUiHudMeter>(Root, TEXT("SCkUiHudMeter"));
    if (!TestTrue(TEXT("Authored HUD realizes native graphic and meter"), Graphic.IsValid() && Meter.IsValid())) { return false; }
    TestEqual(TEXT("Native graphic reads admitted texture dimensions"), ImageSize(Graphic), FVector2D(8, 8));
    Meter->AdvancePresentation(0.0f);
    TestEqual(TEXT("Native meter reads reflected scalar"), Meter->GetTargetFraction(), 0.75f);
    TestFalse(TEXT("Null image update rejects"), Owner->TrySetImage(TEXT("portrait"), nullptr));
    TestFalse(TEXT("Default zero-size image update rejects"), Owner->TrySetImage(TEXT("portrait"), DefaultTexture.Get()));
    TestFalse(TEXT("Unknown image update rejects"), Owner->TrySetImage(TEXT("missing"), Replacement.Get()));
    TestEqual(TEXT("Rejected updates preserve native graphic dimensions"), ImageSize(Graphic), FVector2D(8, 8));
    TestTrue(TEXT("Valid image update accepted"), Owner->TrySetImage(TEXT("portrait"), Replacement.Get()));
    TestTrue(TEXT("Accepted update retains native graphic and reads replacement dimensions"),
        Find<SImage>(Root, TEXT("SImage")) == Graphic && ImageSize(Graphic) == FVector2D(16, 4));
    const auto Revision = Owner->Get_Revision();
    TestFalse(TEXT("Missing image binding rejects reload"), Owner->TryReload(Markup().Replace(
        TEXT("image-bind=\"portrait\""), TEXT("image-bind=\"missing\"")), TEXT(""), Errors));
    TestTrue(TEXT("Failed binding preserves owner revision and current native graphic"), !Errors.IsEmpty()
        && Owner->Get_Revision() == Revision && Owner->Get_View() == View
        && Find<SImage>(Root, TEXT("SImage")) == Graphic && ImageSize(Graphic) == FVector2D(16, 4));
    TestTrue(TEXT("Compatible reload commits"), Owner->TryReload(Markup(), TEXT(""), Errors));
    TestTrue(TEXT("Compatible reload retains HUD native identities"), Find<SImage>(Root, TEXT("SImage")) == Graphic
        && Find<SCkUiHudMeter>(Root, TEXT("SCkUiHudMeter")) == Meter);
    Owner->Request_Release();
    TestTrue(TEXT("Release retires native image reads and meter"), View->IsOwnerReleased()
        && ImageSize(Graphic) == FVector2D::ZeroVector && Meter->IsFrozen() && !Owner->TrySetImage(TEXT("portrait"), Texture.Get()));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkDarkFantasy_ImageGc,
    "Ck.UiAuthoring.DarkFantasy.ReflectedImageOwnerGc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkDarkFantasy_ImageGc::RunTest(const FString&) -> bool
{
    using namespace ck_dark_fantasy_proof;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Image lifetime proof requires Slate.")); return false; }
    TSharedPtr<FCkUiView> HeldView;
    TSharedPtr<SImage> HeldGraphic;
    TWeakObjectPtr<UCk_UI_AuthoredSession_UE> WeakOwner;
    TWeakObjectPtr<UTexture2D> WeakTexture;
    TArray<FString> Errors;
    {
        TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Owner{NewObject<UCk_UI_AuthoredSession_UE>()};
        auto* Texture = UTexture2D::CreateTransient(8, 8);
        WeakTexture = Texture;
        if (!TestTrue(TEXT("Image lifetime fixture admits"), IsValid(Texture)
            && Owner->TryInitialize(Config(Texture), Errors) && Owner->TryReload(Markup(), TEXT(""), Errors))) { return false; }
        HeldView = Owner->Get_View();
        if (!TestTrue(TEXT("Lifetime fixture exposes view"), HeldView.IsValid())) { return false; }
        HeldGraphic = Find<SImage>(HeldView->GetRegion(TEXT("main")), TEXT("SImage"));
        if (!TestTrue(TEXT("Lifetime fixture exposes real graphic"), HeldGraphic.IsValid())) { return false; }
        FlushRenderingCommands();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        TestTrue(TEXT("Reflected session ownership retains texture through GC"), WeakTexture.IsValid()
            && ImageSize(HeldGraphic) == FVector2D(8, 8));
        WeakOwner = Owner.Get();
    }
    FlushRenderingCommands();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestFalse(TEXT("Held native view does not retain reflected image owner"), WeakOwner.IsValid());
    TestTrue(TEXT("Collected owner retires native image reads"), HeldView->IsOwnerReleased() && ImageSize(HeldGraphic) == FVector2D::ZeroVector);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkDarkFantasy_Pie,
    "Ck.UiAuthoring.DarkFantasy.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkDarkFantasy_Pie::RunTest(const FString&) -> bool
{
    using namespace ck_dark_fantasy_proof;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Fantasy gym proof requires Slate.")); return false; }
    const auto State = MakeShared<FPieState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* R = Registry(ck::auto_test::net::Get_ServerWorld());
            return IsValid(R) && R->Find_GymIndexByName(TEXT("UI: Dark Fantasy HUD")) != INDEX_NONE;
        }), 15.0, TEXT("Dark fantasy production gym registers")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
        {
            auto* R = Registry(World);
            if (!TestNotNull(TEXT("Gym registry exists"), R)) { return; }
            const int32 Index = R->Find_GymIndexByName(TEXT("UI: Dark Fantasy HUD"));
            if (!TestTrue(TEXT("Registered production gym entry exists"), R->Get_Entries().IsValidIndex(Index))) { return; }
            const auto& Entry = R->Get_Entries()[Index];
            TestEqual(TEXT("Production HUD gym category"), Entry.Category, FString(TEXT("CkUI")));
            TestTrue(TEXT("Production gym selects authored script game mode"), IsValid(Entry.GameModeClass.Get())
                && Entry.GameModeClass.Get()->GetName().Contains(TEXT("Ck_DarkFantasyHud_GameMode")));
            R->Request_TravelToGym(Index);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            auto* Owner = Session(World);
            return IsValid(Hud(World)) && ((IsValid(Owner) && Owner->Get_IsUsable() && Owner->Get_Revision() > 0)
                || !Getter<FStrProperty, FString>(Hud(World), TEXT("Get_StartupError"), {}).IsEmpty());
        }), 30.0, TEXT("Production authored HUD mounts or reports startup failure")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Widget = Hud(World); auto* Owner = Session(World);
            const FString StartupError = Getter<FStrProperty, FString>(Widget, TEXT("Get_StartupError"), {});
            if (!StartupError.IsEmpty()) { AddError(FString::Printf(TEXT("Fantasy HUD startup: %s"), *StartupError)); return; }
            if (!TestNotNull(TEXT("Script HUD widget exists"), Widget)
                || !TestNotNull(TEXT("Reflected HUD session exists"), Owner)) { return; }
            State->Owner = Owner; State->View = Owner->Get_View();
            if (!TestTrue(TEXT("Production accepted native view exists"), State->View.IsValid())) { return; }
            const auto Root = State->View->GetRegion(TEXT("main"));
            State->Wheel = Find<SCkUiRadialSelector>(Root, TEXT("SCkUiRadialSelector"));
            const auto Graphic = Find<SImage>(Root, TEXT("SImage"));
            const auto Meter = Find<SCkUiHudMeter>(Root, TEXT("SCkUiHudMeter"));
            if (!TestTrue(TEXT("Production script realizes native wheel, image, and meter"), State->Wheel.IsValid()
                && Graphic.IsValid() && Meter.IsValid() && ImageSize(Graphic).X > 0.0 && ImageSize(Graphic).Y > 0.0)) { return; }
            TestEqual(TEXT("Production native wheel carries authored id"), State->Wheel->GetTag(), FName(TEXT("interaction-wheel")));
            TArray<FCk_UI_AuthoredRecord> Seed;
            if (!TestTrue(TEXT("Production script seeds eight reflected interaction records"),
                Owner->TryGetRecords(TEXT("Interactions"), Seed) && Seed.Num() == 8)) { return; }
            if (!GUsingNullRHI)
            {
                // Leave room for the PIE window chrome on a 2560x1440 desktop.
                for (const auto Size : {FIntPoint{1280, 720}, FIntPoint{1920, 1080}, FIntPoint{2400, 1350}})
                { TestTrue(TEXT("Production open-wheel native screenshot"), Capture(World, *State,
                    FString::Printf(TEXT("open-%dx%d"), Size.X, Size.Y), Size)); }
            }

            const auto Key = [&State](FKey InKey)
            {
                const FKeyEvent Event(InKey, FModifierKeysState{}, 0, false, 0, 0);
                return State->Wheel->OnKeyDown(State->Wheel->GetCachedGeometry(), Event).IsEventHandled();
            };
            const int32 Before = Getter<FIntProperty, int32>(Widget, TEXT("Get_ActivationCount"), INDEX_NONE);
            TestTrue(TEXT("Native radial keyboard selection handled"), Key(EKeys::Right));
            TestEqual(TEXT("Native key proposal dispatches through reflected OnItemAction to script"),
                Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}), FString(TEXT("thanks")));
            TestTrue(TEXT("Native radial keyboard activation handled"), Key(EKeys::Enter));
            TestEqual(TEXT("Reflected activation applies exactly once"),
                Getter<FIntProperty, int32>(Widget, TEXT("Get_ActivationCount"), INDEX_NONE), Before + 1);

            const auto RejectActivation = [this, Owner, Widget, &Seed, &Key, Before](bool InEnabled, float InCooldown)
            {
                auto Changed = Seed;
                auto* Record = Changed.FindByPredicate([](const auto& Entry) { return Entry.Get_Key() == TEXT("thanks"); });
                if (!TestNotNull(TEXT("Selected interaction record exists"), Record)) { return; }
                auto Fields = Record->Get_Fields();
                for (auto& Field : Fields)
                {
                    if (Field.Get_Name() == TEXT("enabled")) { Field.Set_Bool(InEnabled); }
                    if (Field.Get_Name() == TEXT("cooldown")) { Field.Set_Number(InCooldown); }
                }
                Record->Set_Fields(Fields);
                TArray<FString> Errors;
                if (!TestTrue(TEXT("Blocked production interaction update accepts atomically"),
                    Owner->TrySetRecords(TEXT("Interactions"), Changed, Errors))) { return; }
                Key(EKeys::Enter);
                TestEqual(TEXT("Native unavailable interaction rejects activation"),
                    Getter<FIntProperty, int32>(Widget, TEXT("Get_ActivationCount"), INDEX_NONE), Before + 1);
                TestFalse(TEXT("Script public activation guard also rejects unavailable interaction"),
                    Getter<FBoolProperty, bool>(Widget, TEXT("Request_Activate"), true));
            };
            RejectActivation(false, 0.0f);
            RejectActivation(true, 0.5f);
            Owner->OnItemAction.Broadcast(TEXT("ActivateInteraction"), TEXT("missing"));
            TestEqual(TEXT("Malformed stable-key proposal has no activation effect"),
                Getter<FIntProperty, int32>(Widget, TEXT("Get_ActivationCount"), INDEX_NONE), Before + 1);
            TArray<FString> Errors;
            TestTrue(TEXT("Valid seed restores interaction model"), Owner->TrySetRecords(TEXT("Interactions"), Seed, Errors));
            TestTrue(TEXT("Script toggles production wheel closed"), Getter<FBoolProperty, bool>(Widget, TEXT("Request_ToggleWheel"), false));
            TestFalse(TEXT("Closed wheel state published"), Getter<FBoolProperty, bool>(Widget, TEXT("Get_WheelOpen"), true));
            TestFalse(TEXT("Hidden wheel rejects public activation"), Getter<FBoolProperty, bool>(Widget, TEXT("Request_Activate"), true));
            if (!GUsingNullRHI)
            { TestTrue(TEXT("Production closed-wheel native screenshot"), Capture(World, *State, TEXT("closed-1920x1080"), {1920, 1080})); }
            const auto Revision = Owner->Get_Revision();
            TestTrue(TEXT("Production invalid-source operation rejects"), Getter<FBoolProperty, bool>(Widget, TEXT("Request_InvalidSource"), false));
            TestTrue(TEXT("Rejected production source preserves usable view and revision"), Owner->Get_IsUsable()
                && Owner->Get_View() == State->View && Owner->Get_Revision() == Revision
                && !Getter<FStrProperty, FString>(Widget, TEXT("Get_SourceError"), {}).IsEmpty());

            const FString Directory = FPaths::Combine(FPaths::ProjectDir(),
                TEXT("Plugins/CkTests/Resources/UiAuthoring/DarkFantasyHudProof"));
            FString SourceMarkup, SourceStyles;
            if (!TestTrue(TEXT("Production radial source fixtures load"),
                FFileHelper::LoadFileToString(SourceMarkup, *FPaths::Combine(Directory, TEXT("DarkFantasyHudProof.ui.html")))
                && FFileHelper::LoadFileToString(SourceStyles, *FPaths::Combine(Directory, TEXT("DarkFantasyHudProof.ui.css")))
                && SourceStyles.Contains(TEXT("-ck-radial-sector-mode: 1")))) { return; }
            if (!TestTrue(TEXT("Unmodified production radial source commits before negative style test"),
                Owner->TryReload(SourceMarkup, SourceStyles, Errors))) { return; }
            const auto StyleRevision = Owner->Get_Revision();
            const auto* RootChildren = Root->GetChildren();
            if (!TestTrue(TEXT("Accepted production source has mounted root"), RootChildren != nullptr && RootChildren->Num() == 1)) { return; }
            const auto AcceptedChild = RootChildren->GetChildAt(0);
            TArray<FCk_UI_AuthoredRecord> AcceptedRecords;
            int64 AcceptedCollectionRevision = 0;
            FText AcceptedSelection, AcceptedStatus;
            float AcceptedHealth = 0.0f, AcceptedMana = 0.0f;
            bool AcceptedWheelOpen = true;
            if (!TestTrue(TEXT("Accepted production model snapshots before rejected sector style"),
                Owner->TryGetRecords(TEXT("Interactions"), AcceptedRecords)
                && Owner->TryGetCollectionRevision(TEXT("Interactions"), AcceptedCollectionRevision)
                && Owner->TryGetText(TEXT("Selection"), AcceptedSelection)
                && Owner->TryGetText(TEXT("ActionStatus"), AcceptedStatus)
                && Owner->TryGetNumber(TEXT("HealthFraction"), AcceptedHealth)
                && Owner->TryGetNumber(TEXT("ManaFraction"), AcceptedMana)
                && Owner->TryGetBool(TEXT("WheelOpen"), AcceptedWheelOpen))) { return; }
            const FString InvalidStyles = SourceStyles.Replace(TEXT("-ck-radial-sector-mode: 1"), TEXT("-ck-radial-sector-mode: 2"));
            TestFalse(TEXT("Out-of-domain numeric sector mode rejects production radial source"),
                Owner->TryReload(SourceMarkup, InvalidStyles, Errors));
            TestTrue(TEXT("Production radial factory diagnoses the rejected sector value"), Errors.ContainsByPredicate([](const FString& Error)
                { return Error.Contains(TEXT("Radial selector sector mode must be 0 or 1.")); }));
            TArray<FCk_UI_AuthoredRecord> AfterRecords;
            int64 AfterCollectionRevision = 0;
            FText AfterSelection, AfterStatus;
            float AfterHealth = -1.0f, AfterMana = -1.0f;
            bool AfterWheelOpen = true;
            TestTrue(TEXT("Rejected numeric sector mode preserves view revision and mounted native identities"),
                Owner->Get_IsUsable() && Owner->Get_Revision() == StyleRevision && Owner->Get_View() == State->View
                && Root->GetChildren() != nullptr && Root->GetChildren()->Num() == 1
                && Root->GetChildren()->GetChildAt(0) == AcceptedChild
                && Find<SCkUiRadialSelector>(Root, TEXT("SCkUiRadialSelector")) == State->Wheel);
            TestTrue(TEXT("Rejected numeric sector mode preserves all scalar model values"),
                Owner->TryGetText(TEXT("Selection"), AfterSelection) && AfterSelection.EqualTo(AcceptedSelection)
                && Owner->TryGetText(TEXT("ActionStatus"), AfterStatus) && AfterStatus.EqualTo(AcceptedStatus)
                && Owner->TryGetNumber(TEXT("HealthFraction"), AfterHealth) && AfterHealth == AcceptedHealth
                && Owner->TryGetNumber(TEXT("ManaFraction"), AfterMana) && AfterMana == AcceptedMana
                && Owner->TryGetBool(TEXT("WheelOpen"), AfterWheelOpen) && AfterWheelOpen == AcceptedWheelOpen);
            bool SameRecords = Owner->TryGetRecords(TEXT("Interactions"), AfterRecords)
                && AfterRecords.Num() == AcceptedRecords.Num()
                && Owner->TryGetCollectionRevision(TEXT("Interactions"), AfterCollectionRevision)
                && AfterCollectionRevision == AcceptedCollectionRevision;
            for (int32 Index = 0; SameRecords && Index < AcceptedRecords.Num(); ++Index)
            {
                const auto& BeforeRecord = AcceptedRecords[Index]; const auto& AfterRecord = AfterRecords[Index];
                SameRecords = BeforeRecord.Get_Key() == AfterRecord.Get_Key()
                    && BeforeRecord.Get_Fields().Num() == AfterRecord.Get_Fields().Num();
                for (int32 FieldIndex = 0; SameRecords && FieldIndex < BeforeRecord.Get_Fields().Num(); ++FieldIndex)
                {
                    const auto& BeforeField = BeforeRecord.Get_Fields()[FieldIndex];
                    const auto& AfterField = AfterRecord.Get_Fields()[FieldIndex];
                    SameRecords = BeforeField.Get_Name() == AfterField.Get_Name() && BeforeField.Get_Kind() == AfterField.Get_Kind();
                    if (!SameRecords) { break; }
                    switch (BeforeField.Get_Kind())
                    {
                        case ECk_UI_AuthoredFieldKind::Text: SameRecords = BeforeField.Get_Text().EqualTo(AfterField.Get_Text()); break;
                        case ECk_UI_AuthoredFieldKind::Number: SameRecords = BeforeField.Get_Number() == AfterField.Get_Number(); break;
                        case ECk_UI_AuthoredFieldKind::Bool: SameRecords = BeforeField.Get_Bool() == AfterField.Get_Bool(); break;
                        default: SameRecords = false; break;
                    }
                }
            }
            TestTrue(TEXT("Rejected numeric sector mode preserves interaction records and collection revision"), SameRecords);
            Errors.Reset();
            const FString InvalidFontStyles = SourceStyles.Replace(TEXT("-ck-radial-label-font-size: 17px"),
                TEXT("-ck-radial-label-font-size: 0px"));
            TestFalse(TEXT("Out-of-domain radial label font size rejects source"),
                Owner->TryReload(SourceMarkup, InvalidFontStyles, Errors));
            TestTrue(TEXT("Rejected radial font size has a specific diagnostic"), Errors.ContainsByPredicate([](const FString& Error)
                { return Error.Contains(TEXT("Radial selector font size '-ck-radial-label-font-size'")); }));
            TestTrue(TEXT("Rejected radial font size preserves the published view and revision"),
                Owner->Get_IsUsable() && Owner->Get_Revision() == StyleRevision && Owner->Get_View() == State->View
                && Root->GetChildren() != nullptr && Root->GetChildren()->Num() == 1
                && Root->GetChildren()->GetChildAt(0) == AcceptedChild);
            CollectGarbage(RF_NoFlags);
            TestTrue(TEXT("Script reflected ownership survives collection"), State->Owner.IsValid()
                && State->Owner.Get() == Session(World) && State->Owner->Get_IsUsable());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([State]() -> bool
        {
            const bool Retired = State->View.IsValid() && State->View->IsOwnerReleased()
                && State->Wheel.IsValid() && State->Wheel->IsFrozen();
            CollectGarbage(RF_NoFlags);
            const bool Collected = !State->Owner.IsValid();
            State->Restore(); return Retired && Collected;
        }), TEXT("PIE teardown retires native wheel and permits reflected session GC")));
    return true;
}

#endif
