#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGameSettings/UI/CkGameSettingsUI_KeyBindingPageWidget.h"
#include "CkGameSettings/UI/CkGameSettingsUI_ScreenWidget.h"
#include "CkInput/CkKeyBinding_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTest_GameSettings_TransientInputSettings.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputDeveloperSettings.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateUser.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UserSettings/EnhancedInputUserSettings.h"

namespace ck_test_gamesettings_exact_capture
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext |
        EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI;
    const FName MappingName(TEXT("CkTest.ExactOwner.Capture"));

    // Registered after the production page processor for each capture. Returning false leaves
    // downstream Slate routing untouched. An event seen here was passed over by that page;
    // an assigned event captured by the page never reaches this observer.
    struct FPassThroughProbe final : IInputProcessor
    {
        int32 Keys = 0;
        int32 MouseButtons = 0;
        int32 Wheels = 0;
        int32 Analogs = 0;
        void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}
        bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent&) override
        { ++Keys; return false; }
        bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent&) override
        { ++MouseButtons; return false; }
        bool HandleMouseWheelOrGestureEvent(FSlateApplication&, const FPointerEvent&,
            const FPointerEvent*) override
        { ++Wheels; return false; }
        bool HandleAnalogInputEvent(FSlateApplication&, const FAnalogInputEvent&) override
        { ++Analogs; return false; }
        void ResetCounts() { Keys = MouseButtons = Wheels = Analogs = 0; }
    };

    struct FRun final
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<UGameInstance> Instance;
        TWeakObjectPtr<ULocalPlayer> First;
        TWeakObjectPtr<ULocalPlayer> Second;
        TWeakObjectPtr<APlayerController> FirstController;
        TWeakObjectPtr<APlayerController> SecondController;
        TStrongObjectPtr<UInputMappingContext> Context;
        TStrongObjectPtr<UInputAction> Action;
        TStrongObjectPtr<UCk_GameSettingsUI_KeyBindingPageWidget> Page;
        TStrongObjectPtr<UCk_GameSettingsUI_ScreenWidget> Screen;
        TSharedPtr<FPassThroughProbe> Probe;
        TSoftClassPtr<UEnhancedInputUserSettings> OriginalSettingsClass;
        FString OriginalSaveSlot;
        FString DisposableSaveSlot;
        IConsoleVariable* SsgiQuality = nullptr;
        int32 OriginalSsgiQuality = 0;
        EConsoleVariableFlags OriginalSsgiFlags = ECVF_Default;
        FName SsgiOverrideTag;
        bool SsgiOverrideApplied = false;
        bool DeveloperSettingsChanged = false;
        bool SecondCreated = false;
        bool RegisteredFirst = false;
        bool RegisteredSecond = false;
        bool SetupReady = false;
        bool ScenarioComplete = false;
        bool Restored = false;

        ~FRun() { RestoreSsgi(); RestoreDeveloperSettings(); }

        auto RestoreSsgi() -> void
        {
            if (!SsgiOverrideApplied || SsgiQuality == nullptr) { return; }
            SsgiQuality->Unset(ECVF_SetByPluginHighPriority, SsgiOverrideTag);
            SsgiOverrideApplied = false;
        }

        auto IsSsgiRestored() const -> bool
        {
            const auto* Current = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SSGI.Quality"));
            return SsgiQuality != nullptr && Current == SsgiQuality &&
                Current->GetInt() == OriginalSsgiQuality &&
                Current->GetFlags() == OriginalSsgiFlags;
        }

        auto RestoreDeveloperSettings() -> void
        {
            if (!DeveloperSettingsChanged) { return; }
            if (auto* Developer = GetMutableDefault<UEnhancedInputDeveloperSettings>())
            {
                Developer->UserSettingsClass = OriginalSettingsClass;
                Developer->InputSettingsSaveSlotName = OriginalSaveSlot;
            }
            DeveloperSettingsChanged = false;
        }

        auto Restore() -> void
        {
            if (Restored) { return; }
            Restored = true;
            if (Probe.IsValid() && FSlateApplication::IsInitialized())
            { FSlateApplication::Get().UnregisterInputPreProcessor(Probe); }
            if (Page.IsValid()) { Page->RemoveFromParent(); }
            if (Screen.IsValid()) { Screen->RemoveFromParent(); }
            if (Context.IsValid())
            {
                if (RegisteredFirst && FirstController.IsValid())
                {
                    if (auto* Settings = UCk_Utils_KeyBinding_UE::Get_InputUserSettings(FirstController.Get()))
                    { Settings->UnregisterInputMappingContext(Context.Get()); }
                }
                if (RegisteredSecond && SecondController.IsValid())
                {
                    if (auto* Settings = UCk_Utils_KeyBinding_UE::Get_InputUserSettings(SecondController.Get()))
                    { Settings->UnregisterInputMappingContext(Context.Get()); }
                }
            }
            if (SecondCreated && Instance.IsValid() && Second.IsValid())
            { Instance->RemoveLocalPlayer(Second.Get()); }
            Page.Reset();
            Screen.Reset();
            Context.Reset();
            Action.Reset();
        }
    };

    auto GetClientWorld() -> UWorld*
    { return ck::auto_test::net::Get_ClientWorld(0); }

    auto GetVisiblePath(UUserWidget* Widget) -> bool
    {
        if (!IsValid(Widget) || !FSlateApplication::IsInitialized()) { return false; }
        const TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
        FWidgetPath Path;
        return SlateWidget.IsValid() &&
            FSlateApplication::Get().FindPathToWidget(
                SlateWidget.ToSharedRef(), Path, EVisibility::Visible) && Path.IsValid();
    }

    // Reflect the real rows created by Request_RebuildRows. This never writes capture state or
    // calls the page's private begin method; the production row delegate initiates capture.
    auto FindRealRow(UCk_GameSettingsUI_KeyBindingPageWidget* Page)
        -> UCk_GameSettingsUI_KeyBindingRowWidget*
    {
        if (!IsValid(Page)) { return nullptr; }
        const FArrayProperty* Rows = FindFProperty<FArrayProperty>(Page->GetClass(), TEXT("_Rows"));
        const FObjectProperty* Objects = Rows ? CastField<FObjectProperty>(Rows->Inner) : nullptr;
        if (!Rows || !Objects) { return nullptr; }
        FScriptArrayHelper Values(Rows, Rows->ContainerPtrToValuePtr<void>(Page));
        for (int32 Index = 0; Index < Values.Num(); ++Index)
        {
            auto* Row = Cast<UCk_GameSettingsUI_KeyBindingRowWidget>(
                Objects->GetObjectPropertyValue(Values.GetRawPtr(Index)));
            if (IsValid(Row) && Row->Get_MappingName() == MappingName) { return Row; }
        }
        return nullptr;
    }

    auto PumpSlate() -> void
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        Slate.PumpMessages();
        Slate.Tick();
        Slate.Tick();
    }

    auto ArmRealRow(UCk_GameSettingsUI_KeyBindingPageWidget* Page, FRun& Run) -> bool
    {
        auto* Row = FindRealRow(Page);
        if (!IsValid(Row) || !FSlateApplication::IsInitialized()) { return false; }
        if (!Run.Probe.IsValid()) { Run.Probe = MakeShared<FPassThroughProbe>(); }
        else { FSlateApplication::Get().UnregisterInputPreProcessor(Run.Probe); }
        Run.Probe->ResetCounts();
        auto* Button = Row->WidgetTree->FindWidget<UCk_GameSettingsGym_Button>(
            TEXT("RebindButton"));
        if (!IsValid(Button) || !GetVisiblePath(Page)) return false;
        Button->Test_DispatchNativeClick();
        return FSlateApplication::Get().RegisterInputPreProcessor(Run.Probe);
    }

    // Temporarily assign the already-existing keyboard device to B. Restoring the exact prior
    // platform user in the destructor keeps the global mapper unchanged even on a failed check.
    struct FScopedAssignedInput final
    {
        IPlatformInputDeviceMapper& Mapper;
        FInputDeviceId Device;
        FPlatformUserId Original;
        bool Changed = false;

        FScopedAssignedInput(FInputDeviceId InDevice, FPlatformUserId InExpectedOriginal,
            FPlatformUserId InTemporary)
            : Mapper(IPlatformInputDeviceMapper::Get()), Device(InDevice), Original(InExpectedOriginal)
        {
            if (Device.IsValid() && Mapper.GetUserForInputDevice(Device) == Original &&
                Original != InTemporary)
            { Changed = Mapper.Internal_ChangeInputDeviceUserMapping(Device, InTemporary, Original); }
        }

        ~FScopedAssignedInput()
        {
            if (Changed)
            {
                const FPlatformUserId Current = Mapper.GetUserForInputDevice(Device);
                if (Current != Original)
                { Mapper.Internal_ChangeInputDeviceUserMapping(Device, Original, Current); }
            }
        }
    };

    class FRestore final : public IAutomationLatentCommand
    {
    public:
        explicit FRestore(const TSharedRef<FRun>& InRun) : Run(InRun) {}
        bool Update() override { Run->Restore(); return true; }
    private:
        TSharedRef<FRun> Run;
    };

    class FRestoreDeveloperSettings final : public IAutomationLatentCommand
    {
    public:
        FRestoreDeveloperSettings(FAutomationTestBase* InTest, const TSharedRef<FRun>& InRun)
            : Test(InTest), Run(InRun) {}

        bool Update() override
        {
            Run->RestoreDeveloperSettings();
            Run->RestoreSsgi();
            if (Test)
            {
                const auto* Developer = GetDefault<UEnhancedInputDeveloperSettings>();
                Test->TestTrue(TEXT("fixture restores SSGI quality and all CVar flags"),
                    Run->IsSsgiRestored());
                Test->TestTrue(TEXT("fixture restores Enhanced Input settings class and slot"),
                    Developer && Developer->UserSettingsClass == Run->OriginalSettingsClass &&
                    Developer->InputSettingsSaveSlotName == Run->OriginalSaveSlot);
                Test->TestFalse(TEXT("disposable player 0 slot has no saved file"),
                    UGameplayStatics::DoesSaveGameExist(Run->DisposableSaveSlot, 0));
                Test->TestFalse(TEXT("disposable player 1 slot has no saved file"),
                    UGameplayStatics::DoesSaveGameExist(Run->DisposableSaveSlot, 1));
            }
            return true;
        }
    private:
        FAutomationTestBase* Test = nullptr;
        TSharedRef<FRun> Run;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkGameSettings_KeyCaptureExactLocalOwner,
    "Ck.GameSettings.KeyCapture.ExactLocalOwner.TwoLocalPlayers",
    ck_test_gamesettings_exact_capture::Flags)

bool FCkGameSettings_KeyCaptureExactLocalOwner::RunTest(const FString& Parameters)
{
    using namespace ck_test_gamesettings_exact_capture;

    bool DedicatedServer = false;
    if ((Parameters != TEXT("ClientServer") && Parameters != TEXT("ListenServer")) ||
        !ck::auto_test::net::TryGet_DedicatedServer(Parameters, DedicatedServer))
    {
        AddError(TEXT("Exact-owner key capture fixture received an unsupported topology"));
        return false;
    }

    const TSharedRef<FRun> Run = MakeShared<FRun>();
    auto* Developer = GetMutableDefault<UEnhancedInputDeveloperSettings>();
    if (!IsValid(Developer) || !UCk_Utils_KeyBinding_UE::Get_AreUserSettingsEnabled())
    {
        AddError(TEXT("Enhanced Input User Settings must be enabled for exact-owner capture"));
        return false;
    }
    Run->OriginalSettingsClass = Developer->UserSettingsClass;
    Run->OriginalSaveSlot = Developer->InputSettingsSaveSlotName;
    Run->DisposableSaveSlot = FString::Printf(TEXT("CkTestExactOwner_%s"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if (UGameplayStatics::DoesSaveGameExist(Run->DisposableSaveSlot, 0) ||
        UGameplayStatics::DoesSaveGameExist(Run->DisposableSaveSlot, 1))
    {
        AddError(TEXT("Fresh disposable Enhanced Input save slot unexpectedly already exists"));
        return false;
    }
    Run->SsgiQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SSGI.Quality"));
    if (Run->SsgiQuality == nullptr)
    {
        AddError(TEXT("r.SSGI.Quality is unavailable for the two-view real-RHI fixture"));
        return false;
    }
    Run->OriginalSsgiQuality = Run->SsgiQuality->GetInt();
    Run->OriginalSsgiFlags = Run->SsgiQuality->GetFlags();
    const auto OriginalPriority = static_cast<EConsoleVariableFlags>(
        Run->OriginalSsgiFlags & ECVF_SetByMask);
    if (OriginalPriority > ECVF_SetByPluginHighPriority)
    {
        AddError(TEXT("r.SSGI.Quality has a stronger user override; cannot safely scope-disable it"));
        return false;
    }
    Run->SsgiOverrideTag = FName(*FString::Printf(TEXT("CkTestExactOwnerSSGI_%s"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    Run->SsgiOverrideApplied = true;
    Run->SsgiQuality->Set(0, ECVF_SetByPluginHighPriority, Run->SsgiOverrideTag);
    if (IConsoleManager::Get().FindConsoleVariable(TEXT("r.SSGI.Quality")) != Run->SsgiQuality ||
        Run->SsgiQuality->GetInt() != 0 ||
        (Run->SsgiQuality->GetFlags() & ECVF_SetByMask) != ECVF_SetByPluginHighPriority)
    {
        Run->RestoreSsgi();
        AddError(TEXT("could not tag-disable SSGI before PIE"));
        return false;
    }
    Developer->UserSettingsClass = UCkTest_GameSettings_TransientInputSettings::StaticClass();
    Developer->InputSettingsSaveSlotName = Run->DisposableSaveSlot;
    Run->DeveloperSettingsChanged = true;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 45.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([DedicatedServer]()
        {
            const UWorld* Server = ck::auto_test::net::Get_ServerWorld();
            const UWorld* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            const UWorld* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            return IsValid(Server) && IsValid(Client0) && IsValid(Client1) &&
                Server != Client0 && Server != Client1 && Client0 != Client1 &&
                Server->GetNetMode() == (DedicatedServer ? NM_DedicatedServer : NM_ListenServer) &&
                Client0->GetNetMode() == NM_Client && Client1->GetNetMode() == NM_Client &&
                ck::auto_test::net::Get_AllPIEWorlds().Num() == 3 &&
                ck::auto_test::net::Get_NumClientWorlds() == 2;
        }), TEXT("exact requested server topology and two distinct client PIE worlds exist")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]()
        {
            UWorld* World = GetClientWorld();
            UGameInstance* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
            ULocalPlayer* First = IsValid(Instance) ? Instance->GetLocalPlayerByIndex(0) : nullptr;
            return IsValid(First) && IsValid(First->GetPlayerController(World)) &&
                UCk_Utils_KeyBinding_UE::Get_AreUserSettingsEnabled() &&
                IsValid(UCk_Utils_KeyBinding_UE::Get_InputUserSettings(
                    First->GetPlayerController(World))) && FSlateApplication::IsInitialized();
        }), 45.0, TEXT("PIE client has a real local controller, input profile, and Slate")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!IsValid(World)) { return; }
            UGameInstance* Instance = World->GetGameInstance();
            ULocalPlayer* First = IsValid(Instance) ? Instance->GetLocalPlayerByIndex(0) : nullptr;
            if (!IsValid(First)) { return; }
            if (IConsoleManager::Get().FindConsoleVariable(TEXT("r.SSGI.Quality")) != Run->SsgiQuality ||
                Run->SsgiQuality == nullptr || Run->SsgiQuality->GetInt() != 0 ||
                (Run->SsgiQuality->GetFlags() & ECVF_SetByMask) != ECVF_SetByPluginHighPriority)
            {
                AddError(TEXT("SSGI disable did not survive PIE startup before the second view"));
                return;
            }
            Run->World = World;
            Run->Instance = Instance;
            Run->First = First;
            Run->FirstController = First->GetPlayerController(World);

            FString Error;
            ULocalPlayer* Second = Instance->CreateLocalPlayer(1, Error, true);
            if (!IsValid(Second) || Second == First) { return; }
            Run->Second = Second;
            Run->SecondCreated = true;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        {
            if (!Run->World.IsValid() || !Run->Second.IsValid()) { return false; }
            APlayerController* SecondController = Run->Second->GetPlayerController(Run->World.Get());
            return IsValid(SecondController) && Run->Second->GetSlateUser().IsValid() &&
                IsValid(UCk_Utils_KeyBinding_UE::Get_InputUserSettings(SecondController));
        }), 30.0, TEXT("second actual LocalPlayer has controller, Slate user and independent profile")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!IsValid(World) || World != Run->World.Get() || !Run->First.IsValid() ||
                !Run->Second.IsValid() || !Run->FirstController.IsValid()) { return; }
            Run->SecondController = Run->Second->GetPlayerController(World);
            if (!Run->SecondController.IsValid()) { return; }
            if (Run->First->GetLocalPlayerIndex() != 0 ||
                Run->Second->GetLocalPlayerIndex() != 1) { return; }
            auto* FirstSettings = UCk_Utils_KeyBinding_UE::Get_InputUserSettings(Run->FirstController.Get());
            auto* SecondSettings = UCk_Utils_KeyBinding_UE::Get_InputUserSettings(Run->SecondController.Get());
            if (!IsValid(FirstSettings) || !IsValid(SecondSettings) || FirstSettings == SecondSettings)
            { return; }
            if (!FirstSettings->IsA<UCkTest_GameSettings_TransientInputSettings>() ||
                !SecondSettings->IsA<UCkTest_GameSettings_TransientInputSettings>())
            { return; }

            Run->Action.Reset(NewObject<UInputAction>(Run->Instance.Get()));
            Run->Context.Reset(NewObject<UInputMappingContext>(Run->Instance.Get()));
            if (!Run->Action.IsValid() || !Run->Context.IsValid()) { return; }
            Run->Context->MapKey(Run->Action.Get(), EKeys::F1);
            if (!UCk_Utils_KeyBinding_UE::MakeMappingPlayerMappable(
                Run->Context.Get(), Run->Action.Get(), EKeys::F1, MappingName,
                FText::FromString(TEXT("Exact owner capture")), FText::GetEmpty(),
                FGameplayTagContainer{})) { return; }
            Run->RegisteredFirst = FirstSettings->RegisterInputMappingContext(Run->Context.Get());
            Run->RegisteredSecond = SecondSettings->RegisterInputMappingContext(Run->Context.Get());
            if (!Run->RegisteredFirst || !Run->RegisteredSecond) { return; }

            Run->Page.Reset(CreateWidget<UCkTest_GameSettings_ForwardedKeyBindingPage>(
                Run->FirstController.Get(), UCkTest_GameSettings_ForwardedKeyBindingPage::StaticClass()));
            if (!Run->Page.IsValid() || !Run->Page->AddToPlayerScreen()) { return; }
            PumpSlate();
            if (!GetVisiblePath(Run->Page.Get()) ||
                Run->Page->GetOwningPlayer() != Run->FirstController.Get() ||
                Run->Page->Get_GeneratedRowCount() < 1 || !FindRealRow(Run->Page.Get())) { return; }
            Run->SetupReady = true;
            TestEqual(TEXT("both player profiles start with the authored default key"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(
                    Run->SecondController.Get(), MappingName, EPlayerMappableKeySlot::First), EKeys::F1);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() { return Run->SetupReady; }),
        TEXT("two real local profiles and a visible production key page are ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            if (!Run->SetupReady || !Run->Page.IsValid()) { return; }
            FSlateApplication& Slate = FSlateApplication::Get();
            const auto UserA = Run->First->GetSlateUser();
            const auto UserB = Run->Second->GetSlateUser();
            if (!UserA.IsValid() || !UserB.IsValid() ||
                UserA->GetUserIndex() == UserB->GetUserIndex()) { return; }

            const auto DeviceA = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
            const auto PlatformA = Run->First->GetPlatformUserId();
            const auto PlatformB = Run->Second->GetPlatformUserId();
            if (!DeviceA.IsValid() || !PlatformA.IsValid() || !PlatformB.IsValid() ||
                PlatformA == PlatformB ||
                IPlatformInputDeviceMapper::Get().GetUserForInputDevice(DeviceA) != PlatformA)
            { return; }

            // A foreign Slate user and an unmapped device must leave capture pending. The real
            // remap only occurs when A's assigned keyboard event reaches the registered processor.
            if (!ArmRealRow(Run->Page.Get(), *Run)) { return; }
            {
                FScopedAssignedInput AssignedToB(DeviceA, PlatformA, PlatformB);
                if (!AssignedToB.Changed) { return; }
                const FKeyEvent Foreign(EKeys::F2, FModifierKeysState{}, DeviceA,
                    false, 0, 0, UserB->GetUserIndex());
                Slate.ProcessKeyDownEvent(Foreign);
            }
            const FKeyEvent Unknown(EKeys::F3, FModifierKeysState{},
                FInputDeviceId::CreateFromInternalId(65534), false, 0, 0,
                UserA->GetUserIndex());
            Slate.ProcessKeyDownEvent(Unknown);
            TestEqual(TEXT("foreign and unknown keys pass the real page processor"),
                Run->Probe->Keys, 2);
            TestEqual(TEXT("A unchanged after foreign and unknown input"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(
                    Run->FirstController.Get(), MappingName, EPlayerMappableKeySlot::First), EKeys::F1);
            TestEqual(TEXT("B unchanged after foreign and unknown input"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(
                    Run->SecondController.Get(), MappingName, EPlayerMappableKeySlot::First), EKeys::F1);

            const FKeyEvent Assigned(EKeys::F4, FModifierKeysState{}, DeviceA,
                false, 0, 0, UserA->GetUserIndex());
            TestTrue(TEXT("A's assigned key is consumed by active production capture"),
                Slate.ProcessKeyDownEvent(Assigned));
            TestEqual(TEXT("A's captured key does not pass through to the later probe"),
                Run->Probe->Keys, 2);
            TestEqual(TEXT("only A's profile is remapped"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(
                    Run->FirstController.Get(), MappingName, EPlayerMappableKeySlot::First), EKeys::F4);
            TestEqual(TEXT("B's profile is unchanged"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(
                    Run->SecondController.Get(), MappingName, EPlayerMappableKeySlot::First), EKeys::F1);

            auto ResetA = [&]() -> bool
            {
                FGameplayTagContainer Why;
                return UCk_Utils_KeyBinding_UE::RemapKey(Run->FirstController.Get(),
                    MappingName, EPlayerMappableKeySlot::First, EKeys::F1, Why) &&
                    UCk_Utils_KeyBinding_UE::Get_KeyForMapping(Run->FirstController.Get(),
                        MappingName, EPlayerMappableKeySlot::First) == EKeys::F1;
            };
            auto Unchanged = [&]() -> bool
            {
                return UCk_Utils_KeyBinding_UE::Get_KeyForMapping(Run->FirstController.Get(),
                           MappingName, EPlayerMappableKeySlot::First) == EKeys::F1 &&
                    UCk_Utils_KeyBinding_UE::Get_KeyForMapping(Run->SecondController.Get(),
                           MappingName, EPlayerMappableKeySlot::First) == EKeys::F1;
            };

            const auto PageSlate = Run->Page->GetCachedWidget();
            const auto Window = PageSlate.IsValid() ? Slate.FindWidgetWindow(PageSlate.ToSharedRef()) : nullptr;
            if (!Window.IsValid() || !Window->GetNativeWindow().IsValid() || !ResetA() ||
                !ArmRealRow(Run->Page.Get(), *Run)) { return; }
            const FVector2D At = Run->Page->GetCachedGeometry().LocalToAbsolute(FVector2D(1.0, 1.0));
            const TSet<FKey> MouseDown{EKeys::LeftMouseButton};
            const TSet<FKey> NoButtons;
            {
                FScopedAssignedInput AssignedToB(DeviceA, PlatformA, PlatformB);
                if (!AssignedToB.Changed) { return; }
                const FPointerEvent ForeignMouse(DeviceA, FSlateApplication::CursorPointerIndex,
                    At, At, MouseDown, EKeys::LeftMouseButton, 0.0f,
                    FModifierKeysState{}, UserB->GetUserIndex());
                Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), ForeignMouse);
                const FPointerEvent ForeignMouseUp(DeviceA, FSlateApplication::CursorPointerIndex,
                    At, At, NoButtons, EKeys::LeftMouseButton, 0.0f,
                    FModifierKeysState{}, UserB->GetUserIndex());
                Slate.ProcessMouseButtonUpEvent(ForeignMouseUp);
            }
            TestTrue(TEXT("foreign assigned mouse button leaves A's capture pending and both profiles unchanged"),
                Unchanged());
            const auto UnknownDevice = FInputDeviceId::CreateFromInternalId(65534);
            const FPointerEvent UnknownMouse(UnknownDevice, FSlateApplication::CursorPointerIndex,
                At, At, MouseDown, EKeys::LeftMouseButton, 0.0f,
                FModifierKeysState{}, UserA->GetUserIndex());
            Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), UnknownMouse);
            const FPointerEvent UnknownMouseUp(UnknownDevice, FSlateApplication::CursorPointerIndex,
                At, At, NoButtons, EKeys::LeftMouseButton, 0.0f,
                FModifierKeysState{}, UserA->GetUserIndex());
            Slate.ProcessMouseButtonUpEvent(UnknownMouseUp);
            TestTrue(TEXT("unknown mouse device leaves both profiles unchanged"), Unchanged());
            TestEqual(TEXT("foreign and unknown mouse buttons pass the real page processor"),
                Run->Probe->MouseButtons, 2);
            const FPointerEvent AssignedMouse(DeviceA, FSlateApplication::CursorPointerIndex,
                At, At, MouseDown, EKeys::LeftMouseButton, 0.0f,
                FModifierKeysState{}, UserA->GetUserIndex());
            TestTrue(TEXT("A's assigned mouse button is captured"),
                Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), AssignedMouse));
            TestEqual(TEXT("A's captured mouse button does not pass to the probe"),
                Run->Probe->MouseButtons, 2);
            const FPointerEvent AssignedMouseUp(DeviceA, FSlateApplication::CursorPointerIndex,
                At, At, NoButtons, EKeys::LeftMouseButton, 0.0f,
                FModifierKeysState{}, UserA->GetUserIndex());
            Slate.ProcessMouseButtonUpEvent(AssignedMouseUp);
            TestEqual(TEXT("mouse rebind affects A alone"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(Run->FirstController.Get(),
                    MappingName, EPlayerMappableKeySlot::First), EKeys::LeftMouseButton);

            if (!ResetA() || !ArmRealRow(Run->Page.Get(), *Run)) { return; }
            {
                FScopedAssignedInput AssignedToB(DeviceA, PlatformA, PlatformB);
                if (!AssignedToB.Changed) { return; }
                const FPointerEvent ForeignWheel(DeviceA, FSlateApplication::CursorPointerIndex,
                    At, At, NoButtons, EKeys::Invalid, 1.0f,
                    FModifierKeysState{}, UserB->GetUserIndex());
                Slate.ProcessMouseWheelOrGestureEvent(ForeignWheel, nullptr);
            }
            TestTrue(TEXT("foreign assigned wheel leaves A's capture pending and both profiles unchanged"),
                Unchanged());
            const FPointerEvent UnknownWheel(UnknownDevice, FSlateApplication::CursorPointerIndex,
                At, At, NoButtons, EKeys::Invalid, 1.0f,
                FModifierKeysState{}, UserA->GetUserIndex());
            Slate.ProcessMouseWheelOrGestureEvent(UnknownWheel, nullptr);
            TestTrue(TEXT("unknown wheel device leaves both profiles unchanged"), Unchanged());
            TestEqual(TEXT("foreign and unknown wheels pass the real page processor"),
                Run->Probe->Wheels, 2);
            const FPointerEvent AssignedWheel(DeviceA, FSlateApplication::CursorPointerIndex,
                At, At, NoButtons, EKeys::Invalid, 1.0f,
                FModifierKeysState{}, UserA->GetUserIndex());
            const FPointerEvent CrossUserGesture(DeviceA, FSlateApplication::CursorPointerIndex,
                At, At, NoButtons, EKeys::Invalid, 1.0f,
                FModifierKeysState{}, UserB->GetUserIndex());
            Slate.ProcessMouseWheelOrGestureEvent(AssignedWheel, &CrossUserGesture);
            TestTrue(TEXT("A wheel paired with a B Slate-user gesture cannot remap A"), Unchanged());
            TestEqual(TEXT("mixed-owner wheel and gesture pass through the production capture processor"),
                Run->Probe->Wheels, 3);
            TestTrue(TEXT("A's assigned wheel is captured"),
                Slate.ProcessMouseWheelOrGestureEvent(AssignedWheel, nullptr));
            TestEqual(TEXT("A's captured wheel does not pass to the probe"),
                Run->Probe->Wheels, 3);
            TestEqual(TEXT("wheel rebind affects A alone"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(Run->FirstController.Get(),
                    MappingName, EPlayerMappableKeySlot::First), EKeys::MouseScrollUp);

            if (!ResetA() || !ArmRealRow(Run->Page.Get(), *Run)) { return; }
            {
                FScopedAssignedInput AssignedToB(DeviceA, PlatformA, PlatformB);
                if (!AssignedToB.Changed) { return; }
                const FAnalogInputEvent ForeignAnalog(EKeys::Gamepad_LeftX,
                    FModifierKeysState{}, DeviceA, false, 0, 0, 0.9f,
                    UserB->GetUserIndex());
                Slate.ProcessAnalogInputEvent(ForeignAnalog);
            }
            TestTrue(TEXT("foreign assigned analog leaves A's capture pending and both profiles unchanged"),
                Unchanged());
            const FAnalogInputEvent UnknownAnalog(EKeys::Gamepad_LeftX,
                FModifierKeysState{}, UnknownDevice, false, 0, 0, 0.9f,
                UserA->GetUserIndex());
            Slate.ProcessAnalogInputEvent(UnknownAnalog);
            TestTrue(TEXT("unknown analog device leaves both profiles unchanged"), Unchanged());
            TestEqual(TEXT("foreign and unknown analog events pass the real page processor"),
                Run->Probe->Analogs, 2);
            const FAnalogInputEvent AssignedAnalog(EKeys::Gamepad_LeftX,
                FModifierKeysState{}, DeviceA, false, 0, 0, 0.9f,
                UserA->GetUserIndex());
            TestTrue(TEXT("A's assigned analog is captured"),
                Slate.ProcessAnalogInputEvent(AssignedAnalog));
            TestEqual(TEXT("A's captured analog does not pass to the probe"),
                Run->Probe->Analogs, 2);
            TestEqual(TEXT("analog rebind affects A alone"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(Run->FirstController.Get(),
                    MappingName, EPlayerMappableKeySlot::First), EKeys::Gamepad_LeftX);
            TestEqual(TEXT("B remains unchanged after mouse, wheel, and analog"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(Run->SecondController.Get(),
                    MappingName, EPlayerMappableKeySlot::First), EKeys::F1);
            TestTrue(TEXT("synthetic foreign-event assignment restores the original keyboard owner"),
                IPlatformInputDeviceMapper::Get().GetUserForInputDevice(DeviceA) == PlatformA);

            // Forward only protected production entry points; the page's real generated-row
            // delegate still arms capture. This isolates direct widget delivery from Slate's
            // preprocessor without changing any private capture state.
            auto* ForwardPage = Cast<UCkTest_GameSettings_ForwardedKeyBindingPage>(Run->Page.Get());
            if (!IsValid(ForwardPage) || !ResetA() || !ArmRealRow(ForwardPage, *Run)) { return; }
            {
                FScopedAssignedInput AssignedToB(DeviceA, PlatformA, PlatformB);
                if (!AssignedToB.Changed) { return; }
                const FKeyEvent ForeignDirect(EKeys::F6, FModifierKeysState{}, DeviceA,
                    false, 0, 0, UserB->GetUserIndex());
                TestFalse(TEXT("direct native key handler rejects B's assigned key"),
                    ForwardPage->ForwardNativeKey(ForwardPage->GetCachedGeometry(),
                        ForeignDirect).IsEventHandled());
            }
            TestTrue(TEXT("foreign direct key leaves both profiles unchanged"), Unchanged());
            const FKeyEvent AssignedDirect(EKeys::F6, FModifierKeysState{}, DeviceA,
                false, 0, 0, UserA->GetUserIndex());
            TestTrue(TEXT("direct native key handler accepts A's assigned key"),
                ForwardPage->ForwardNativeKey(ForwardPage->GetCachedGeometry(),
                    AssignedDirect).IsEventHandled());
            TestEqual(TEXT("direct key remaps A alone"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(Run->FirstController.Get(),
                    MappingName, EPlayerMappableKeySlot::First), EKeys::F6);

            if (!ResetA() || !ArmRealRow(ForwardPage, *Run)) { return; }
            {
                FScopedAssignedInput AssignedToB(DeviceA, PlatformA, PlatformB);
                if (!AssignedToB.Changed) { return; }
                const FAnalogInputEvent ForeignDirectAnalog(EKeys::Gamepad_RightX,
                    FModifierKeysState{}, DeviceA, false, 0, 0, 0.9f,
                    UserB->GetUserIndex());
                TestFalse(TEXT("direct native analog handler rejects B's assigned axis"),
                    ForwardPage->ForwardNativeAnalog(ForwardPage->GetCachedGeometry(),
                        ForeignDirectAnalog).IsEventHandled());
            }
            TestTrue(TEXT("foreign direct analog leaves both profiles unchanged"), Unchanged());
            const FAnalogInputEvent AssignedDirectAnalog(EKeys::Gamepad_RightX,
                FModifierKeysState{}, DeviceA, false, 0, 0, 0.9f,
                UserA->GetUserIndex());
            TestTrue(TEXT("direct native analog handler accepts A's assigned axis"),
                ForwardPage->ForwardNativeAnalog(ForwardPage->GetCachedGeometry(),
                    AssignedDirectAnalog).IsEventHandled());
            TestEqual(TEXT("direct analog remaps A alone"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(Run->FirstController.Get(),
                    MappingName, EPlayerMappableKeySlot::First), EKeys::Gamepad_RightX);
            TestEqual(TEXT("B stays unchanged through both direct native handlers"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(Run->SecondController.Get(),
                    MappingName, EPlayerMappableKeySlot::First), EKeys::F1);

            // Move from a directly mounted page to a page inside the real settings-screen
            // widget class. The page still uses its own generated production row/delegate.
            if (!ResetA()) { return; }
            Run->Page->RemoveFromParent();
            Run->Page.Reset();
            Run->Screen.Reset(CreateWidget<UCk_GameSettingsUI_ScreenWidget>(
                Run->FirstController.Get(), UCk_GameSettingsUI_ScreenWidget::StaticClass()));
            if (!Run->Screen.IsValid() || !Run->Screen->WidgetTree) { return; }
            UCanvasPanel* Panel = Run->Screen->WidgetTree->ConstructWidget<UCanvasPanel>();
            if (!IsValid(Panel)) { return; }
            Run->Screen->WidgetTree->RootWidget = Panel;
            UCanvasPanel* ScreenRowsPanel = Run->Screen->WidgetTree->ConstructWidget<UCanvasPanel>();
            if (!IsValid(ScreenRowsPanel) || !IsValid(Panel->AddChild(ScreenRowsPanel))) { return; }
            FObjectProperty* ScreenRows = FindFProperty<FObjectProperty>(
                Run->Screen->GetClass(), TEXT("_RowContainer"));
            if (!ScreenRows) { return; }
            ScreenRows->SetObjectPropertyValue(
                ScreenRows->ContainerPtrToValuePtr<void>(Run->Screen.Get()), ScreenRowsPanel);
            Run->Page.Reset(CreateWidget<UCkTest_GameSettings_ForwardedKeyBindingPage>(
                Run->FirstController.Get(), UCkTest_GameSettings_ForwardedKeyBindingPage::StaticClass()));
            if (!Run->Page.IsValid() || !IsValid(Panel->AddChild(Run->Page.Get())) ||
                !Run->Screen->AddToPlayerScreen()) { return; }
            Run->Screen->ActivateWidget();
            PumpSlate();
            TestTrue(TEXT("nested page has no direct viewport slot"), !Run->Page->IsInViewport());
            TestTrue(TEXT("nested page has a visible exact-owner Slate path"),
                GetVisiblePath(Run->Page.Get()) && GetVisiblePath(Run->Screen.Get()));
            if (!GetVisiblePath(Run->Page.Get()) || !ArmRealRow(Run->Page.Get(), *Run)) { return; }
            const FKeyEvent NestedAssigned(EKeys::F5, FModifierKeysState{}, DeviceA,
                false, 0, 0, UserA->GetUserIndex());
            TestTrue(TEXT("nested real settings page captures A's assigned key"),
                Slate.ProcessKeyDownEvent(NestedAssigned));
            TestEqual(TEXT("nested page consumes before the later probe"), Run->Probe->Keys, 0);
            TestEqual(TEXT("nested capture updates A alone"),
                UCk_Utils_KeyBinding_UE::Get_KeyForMapping(Run->FirstController.Get(),
                    MappingName, EPlayerMappableKeySlot::First), EKeys::F5);
            if (!ResetA() || !ArmRealRow(Run->Page.Get(), *Run)) { return; }
            Run->Screen->SetVisibility(ESlateVisibility::Collapsed);
            PumpSlate();
            Slate.ProcessKeyDownEvent(NestedAssigned);
            TestTrue(TEXT("collapsed settings root cannot remap through an armed page"), Unchanged());
            TestEqual(TEXT("collapsed page passes the assigned key"), Run->Probe->Keys, 1);
            Run->Screen->SetVisibility(ESlateVisibility::Visible);
            PumpSlate();
            Run->Screen->RemoveFromParent();
            Slate.ProcessKeyDownEvent(NestedAssigned);
            TestTrue(TEXT("detached settings root cannot remap through an armed page"), Unchanged());
            TestEqual(TEXT("detached page passes the assigned key"), Run->Probe->Keys, 2);

            // A remains the page's owner, but its mounted viewport root now belongs to B.
            // The visible hierarchy exists; ownership alone must reject the event.
            Run->Page->RemoveFromParent();
            TStrongObjectPtr<UCkTest_GameSettings_ViewportRoot> WrongOwnerRoot(
                CreateWidget<UCkTest_GameSettings_ViewportRoot>(
                    Run->SecondController.Get(), UCkTest_GameSettings_ViewportRoot::StaticClass()));
            if (!WrongOwnerRoot.IsValid() || !WrongOwnerRoot->WidgetTree) { return; }
            UCanvasPanel* WrongPanel = WrongOwnerRoot->WidgetTree->ConstructWidget<UCanvasPanel>();
            if (!IsValid(WrongPanel)) { return; }
            WrongOwnerRoot->WidgetTree->RootWidget = WrongPanel;
            if (!IsValid(WrongPanel->AddChild(Run->Page.Get())) ||
                !WrongOwnerRoot->AddToPlayerScreen()) { return; }
            PumpSlate();
            TestTrue(TEXT("foreign-owned viewport root is mounted and visible"),
                GetVisiblePath(WrongOwnerRoot.Get()) && GetVisiblePath(Run->Page.Get()));
            if (!ArmRealRow(Run->Page.Get(), *Run)) { return; }
            Slate.ProcessKeyDownEvent(NestedAssigned);
            TestTrue(TEXT("foreign-owned visible root cannot remap A"), Unchanged());
            TestEqual(TEXT("foreign-owned page passes the assigned key"), Run->Probe->Keys, 1);
            WrongOwnerRoot->RemoveFromParent();

            // Removing the page invokes its real NativeDestruct, which unregisters the
            // preprocessor. A subsequent assigned event must not change the profile.
            Run->Page->RemoveFromParent();
            Slate.ProcessKeyDownEvent(NestedAssigned);
            TestTrue(TEXT("page destruction releases capture without a late remap"), Unchanged());
            Run->ScenarioComplete = true;
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() { return Run->ScenarioComplete; }),
        TEXT("all assigned, foreign, visibility, ownership and teardown observations completed")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]()
        {
            auto* Settings = Run->FirstController.IsValid()
                ? Cast<UCkTest_GameSettings_TransientInputSettings>(
                    UCk_Utils_KeyBinding_UE::Get_InputUserSettings(Run->FirstController.Get()))
                : nullptr;
            return IsValid(Settings) && Settings->Get_AsynchronousSaveAttempts() > 0;
        }), TEXT("the real page invoked the disposable profile's no-disk save path")));

    ADD_LATENT_AUTOMATION_COMMAND(FRestore(Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FRestoreDeveloperSettings(this, Run));
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
