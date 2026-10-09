// Exercises the authored Game Settings gym through registered network travel and local HUD input.
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_StartupSettings.h"
#include "CkGymRegistry_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkGameSettings/CkGameSettings_Common.h"
#include "CkGameSettings/Subsystem/CkGameSettings_Utils.h"
#include "CkGameSettings/UI/CkGameSettingsUI_ScreenWidget.h"
#include "CkGameSettings/UI/CkGameSettingsUI_KeyBindingPageWidget.h"
#include "CkGameSettings/Settings/CkGameSettings_Settings.h"
#include "CkGameSettings/Subsystem/CkGameSettings_Subsystem.h"
#include "CkGameSettingsGym_Widgets.h"
#include "CkTest_GameSettings_TransientStorageProvider.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "UObject/StrongObjectPtr.h"
#include "CkInput/CkInputSource_Utils.h"
#include "CkInput/Subsystem/CkInputSource_Subsystem.h"
#include "CkInput/CkKeyBinding_Utils.h"
#include "CkTest_GameSettings_TransientInputSettings.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputDeveloperSettings.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/SoftObjectPath.h"
#include "UserSettings/EnhancedInputUserSettings.h"

namespace ck_gamesettings_authored_topology
{
constexpr auto Flags = EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter |
    EAutomationTestFlags::NonNullRHI;
constexpr int32 WorldCount = 3;
constexpr auto Entry = TEXT("/Engine/Maps/Entry");
constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");
constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_GameSettingsGym_GameMode");
constexpr auto ControllerPath = TEXT("/Script/Angelscript.Ck_GameSettingsGym_PlayerController");
constexpr auto GymName = TEXT("Game Settings");

struct FRun
{
    bool Dedicated = false;
    bool StartupCaptured = false;
    bool SettingsChanged = false;
    bool Restored = false;
    ECkGym_StartupMode OldStartup = ECkGym_StartupMode::Cycler;
    FString OldLast;
    TArray<FString> OldRecents;
    TSoftClassPtr<UEnhancedInputUserSettings> OldClass;
    FString OldSlot;
    FString DisposableSlot;
    TSoftClassPtr<UCk_GameSettings_StorageProvider_UE> OldStorageClass;
    TStrongObjectPtr<UClass> PinnedOriginalStorageClass;
    bool StorageChanged = false;
    bool ValueCommitted = false;
    bool SliderCommitSent = false;
    UClass* BaseMode = nullptr;
    UClass* GymMode = nullptr;
    UClass* GymPC = nullptr;
    TWeakObjectPtr<APlayerController> Owner[3];
    TWeakObjectPtr<UCk_GameSettingsUI_ScreenWidget> FirstScreen[3];
    TWeakObjectPtr<UCk_GameSettingsUI_ScreenWidget> ReopenedScreen;
    TWeakObjectPtr<UCk_GameSettingsUI_KeyBindingPageWidget> FirstPage;
    bool KInjected = false;
    bool GInjected = false;

    ~FRun() { Restore(); }
    void Restore()
    {
        if (Restored) return;
        Restored = true;
        if (SettingsChanged)
        {
            if (auto* S = GetMutableDefault<UEnhancedInputDeveloperSettings>())
            { S->UserSettingsClass = OldClass; S->InputSettingsSaveSlotName = OldSlot; }
        }
        if (StorageChanged)
        {
            auto* CDO = GetMutableDefault<UCk_GameSettings_ProjectSettings_UE>();
            if (IsValid(CDO) &&
                CDO->Test_TrySetStorageProviderClass(OldStorageClass) &&
                CDO->Get_StorageProviderClass() == OldStorageClass)
                PinnedOriginalStorageClass.Reset();
        }
        if (StartupCaptured)
        {
            if (auto* S = GetMutableDefault<UCkGym_StartupSettings>())
            {
                S->StartupMode = OldStartup;
                S->LastGymName = OldLast;
                S->RecentGymNames = OldRecents;
                S->SaveConfig();
            }
        }
    }
};

UWorld* WorldAt(int32 I)
{ return I == 0 ? ck::auto_test::net::Get_ServerWorld() : ck::auto_test::net::Get_ClientWorld(I-1); }

APlayerController* Local(UWorld* World)
{
    auto* GI = IsValid(World) ? World->GetGameInstance() : nullptr;
    auto* LP = IsValid(GI) ? GI->GetLocalPlayerByIndex(0) : nullptr;
    auto* PC = IsValid(LP) ? LP->GetPlayerController(World) : nullptr;
    return IsValid(PC) && PC->GetWorld() == World && PC->IsLocalController() ? PC : nullptr;
}

template<class T> TArray<T*> Widgets(UWorld* World)
{
    TArray<UUserWidget*> Found;
    TArray<T*> Result;
    if (!IsValid(World)) return Result;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Found, T::StaticClass(), true);
    for (auto* Widget : Found) if (auto* Typed = Cast<T>(Widget))
    { if (IsValid(Typed) && Typed->GetWorld() == World) Result.Add(Typed); }
    return Result;
}

bool Definitions(UWorld* World)
{
    const TArray<FName> Expected = {TEXT("gymdemo.toggle"), TEXT("gymdemo.volume"),
        TEXT("gymdemo.count"), TEXT("gymdemo.quality"), TEXT("gymdemo.lives")};
    if (!IsValid(World)) return false;
    const auto Keys = UCk_Utils_GameSettings_UE::Get_AllSettingKeys(World);
    if (Keys.Num() != Expected.Num()) return false;
    for (int32 I=0; I<Expected.Num(); ++I)
    {
        FCk_GameSettings_SettingDefinition D;
        if (Keys[I] != Expected[I] ||
            !UCk_Utils_GameSettings_UE::Get_SettingDefinition(World, Expected[I], D) ||
            D.Get_Key() != Expected[I]) return false;
        const ECk_GameSettings_ValueType Type = I == 0 ? ECk_GameSettings_ValueType::Bool :
            I == 1 ? ECk_GameSettings_ValueType::Float :
            I == 3 ? ECk_GameSettings_ValueType::String : ECk_GameSettings_ValueType::Int32;
        const FString Default = I == 0 ? TEXT("true") : I == 1 ? TEXT("0.75") :
            I == 2 || I == 4 ? TEXT("3") : TEXT("medium");
        if (D.Get_ValueType() != Type || D.Get_DefaultValue() != Default ||
            D.Get_CategoryTags().Num() != 1) return false;
        if ((I == 1 && (D.Get_MinValue() != TEXT("0") || D.Get_MaxValue() != TEXT("1"))) ||
            (I == 2 && (D.Get_MinValue() != TEXT("0") || D.Get_MaxValue() != TEXT("10"))) ||
            (I == 3 && D.Get_Options().Num() != 3)) return false;
    }
    return true;
}

bool Context(const FRun& F)
{
    auto* Server = WorldAt(0);
    if (!IsValid(Server) || Server->GetNetMode() !=
        (F.Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
        ck::auto_test::net::Get_AllPIEWorlds().Num() != WorldCount ||
        ck::auto_test::net::Get_NumClientWorlds() != 2) return false;
    for (int32 I=0; I<WorldCount; ++I)
    {
        UWorld* W = WorldAt(I);
        if (!IsValid(W) || !IsValid(W->GetGameInstance())) return false;
        auto* Registry = W->GetGameInstance()->GetSubsystem<UCkGym_Registry_Subsystem>();
        const int32 Index = IsValid(Registry) ? Registry->Find_GymIndexByName(GymName) : INDEX_NONE;
        if (Index == INDEX_NONE || Registry->Get_CurrentGymIndex() != Index) return false;
        if (I != 0 && W->GetNetMode() != NM_Client) return false;
        if (I == 0 && !IsValid(W->GetAuthGameMode())) return false;
        if (I == 0 && W->GetAuthGameMode()->GetClass() != F.GymMode) return false;
        if (I > 0 || !F.Dedicated)
        {
            auto* PC = Local(W);
            if (!IsValid(PC) || PC->GetClass() != F.GymPC) return false;
        }
    }
    return WorldAt(0) != WorldAt(1) && WorldAt(1) != WorldAt(2) && WorldAt(0) != WorldAt(2);
}

bool OwnedScreens(FRun& F, bool Capture)
{
    for (int32 I=0; I<WorldCount; ++I)
    {
        UWorld* W = WorldAt(I);
        auto Screens = Widgets<UCk_GameSettingsUI_ScreenWidget>(W);
        auto Pages = Widgets<UCk_GameSettingsUI_KeyBindingPageWidget>(W);
        if (I == 0 && F.Dedicated)
        { if (!Screens.IsEmpty() || !Pages.IsEmpty() || Local(W)) return false; continue; }
        APlayerController* PC = Local(W);
        auto* Profile = IsValid(PC) ? UCk_Utils_KeyBinding_UE::Get_InputUserSettings(PC) : nullptr;
        auto* GI = IsValid(W) ? W->GetGameInstance() : nullptr;
        auto* Settings = IsValid(GI) ?
            GI->GetSubsystem<UCk_GameSettings_Subsystem_UE>() : nullptr;
        auto* Storage = IsValid(Settings) ? Settings->Get_StorageProvider() : nullptr;
        if (!IsValid(PC) || !IsValid(Profile) ||
            !Profile->IsA<UCkTest_GameSettings_TransientInputSettings>() ||
            !IsValid(Storage) ||
            Storage->GetClass() != UCkTest_GameSettings_TransientStorageProvider::StaticClass() ||
            Screens.Num() != 1 || !Pages.IsEmpty() ||
            Screens[0]->GetOwningPlayer() != PC ||
            Screens[0]->GetOwningLocalPlayer() != PC->GetLocalPlayer() ||
            Screens[0]->GetClass() != UCk_GameSettingsGym_Screen::StaticClass() ||
            !Cast<UCk_GameSettingsGym_Screen>(Screens[0])->Get_IsLayoutReady() ||
            Screens[0]->Get_GeneratedRowCount() != 5 ||
            !Screens[0]->IsInViewport() ||
            !Definitions(W)) return false;
        auto* RowPanel = Cast<UPanelWidget>(Screens[0]->WidgetTree->RootWidget);
        if (!IsValid(RowPanel) || RowPanel->GetChildrenCount() != 5) return false;
        for (int32 K = 0; K < 5; ++K)
        {
            const FName Key = K == 0 ? TEXT("gymdemo.toggle") :
                K == 1 ? TEXT("gymdemo.volume") : K == 2 ? TEXT("gymdemo.count") :
                K == 3 ? TEXT("gymdemo.quality") : TEXT("gymdemo.lives");
            if (!Screens[0]->Get_HasRowForKey(Key) ||
                Screens[0]->Get_RowClassForKey(Key) !=
                    UCk_GameSettingsGym_SettingRow::StaticClass()) return false;
            auto* Row = Cast<UCk_GameSettingsGym_SettingRow>(RowPanel->GetChildAt(K));
            if (!IsValid(Row) || !Row->Get_IsLayoutReady()) return false;
            auto* Check = Row->WidgetTree->FindWidget<UCheckBox>(TEXT("Toggle"));
            auto* Slider = Row->WidgetTree->FindWidget<USlider>(TEXT("Slider"));
            auto* Choices = Row->WidgetTree->FindWidget<UComboBoxString>(TEXT("Options"));
            auto* Stepper = Row->WidgetTree->FindWidget<UHorizontalBox>(TEXT("Stepper"));
            if (!IsValid(Check) || !IsValid(Slider) ||
                !IsValid(Choices) || !IsValid(Stepper) ||
                (Check->GetVisibility() == ESlateVisibility::Visible) != (K == 0) ||
                (Slider->GetVisibility() == ESlateVisibility::Visible) != (K == 1 || K == 2) ||
                (Choices->GetVisibility() == ESlateVisibility::Visible) != (K == 3) ||
                (Stepper->GetVisibility() == ESlateVisibility::Visible) != (K == 4)) return false;
        }
        if (Capture) { F.Owner[I] = PC; F.FirstScreen[I] = Screens[0]; }
    }
    return true;
}

bool Inject(APlayerController* PC, FKey Key)
{
    if (!IsValid(PC) || !PC->IsLocalController()) return false;
    auto* LP = PC->GetLocalPlayer();
    auto* Sub = IsValid(LP) ? LP->GetSubsystem<UCk_InputSource_Subsystem>() : nullptr;
    FCk_Handle_InputSource Source = IsValid(Sub) ? Sub->Get_InputSource() : FCk_Handle_InputSource{};
    if (!ck::IsValid(Source)) return false;
    const FCk_InputSource_RawEvent Event(ECk_InputSource_DeviceClass::Keyboard,
        Key, ECk_InputSource_EventType::Pressed);
    return ck::IsValid(UCk_Utils_InputSource_UE::Request_InjectRawEvent(
        Source, FCk_Request_InputSource_InjectRawEvent(Event), {}));
}

struct FRestore final : IAutomationLatentCommand
{
    TSharedRef<FRun> Run;
    FRestore(const TSharedRef<FRun>& InRun) : Run(InRun) {}
    bool Update() override { Run->Restore(); return true; }
};
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkGameSettingsGym_OwnedTopology_Network,
    "Ck.GameSettings.Gym.OwnedTopology.Network", ck_gamesettings_authored_topology::Flags)

bool FCkGameSettingsGym_OwnedTopology_Network::RunTest(const FString& Parameters)
{
    using namespace ck_gamesettings_authored_topology;
    bool Dedicated = false;
    if ((Parameters != TEXT("ClientServer") && Parameters != TEXT("ListenServer")) ||
        !ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported authored Game Settings topology")); return false; }
    const TSharedRef<FRun> Run = MakeShared<FRun>();
    Run->Dedicated = Dedicated;
    Run->BaseMode = FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    Run->GymMode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    Run->GymPC = FSoftClassPath(ControllerPath).TryLoadClass<APlayerController>();
    auto* InputCDO = GetMutableDefault<UEnhancedInputDeveloperSettings>();
    auto* Startup = GetMutableDefault<UCkGym_StartupSettings>();
    auto* StorageCDO = GetMutableDefault<UCk_GameSettings_ProjectSettings_UE>();
    if (!Run->BaseMode || !Run->GymMode || !Run->GymPC || !IsValid(InputCDO) ||
        !IsValid(Startup) || !IsValid(StorageCDO) ||
        !IsValid(StorageCDO->Get_StorageProviderClass().Get()) ||
        !UCk_Utils_KeyBinding_UE::Get_AreUserSettingsEnabled())
    { AddError(TEXT("authored classes and disposable settings prerequisites missing")); return false; }
    Run->OldStartup = Startup->StartupMode;
    Run->OldLast = Startup->LastGymName;
    Run->OldRecents = Startup->RecentGymNames;
    Run->StartupCaptured = true;
    Run->OldStorageClass = StorageCDO->Get_StorageProviderClass();
    Run->PinnedOriginalStorageClass.Reset(Run->OldStorageClass.Get());
    if (!Run->PinnedOriginalStorageClass.IsValid())
    { AddError(TEXT("original GameSettings provider class could not be pinned")); return false; }
    if (!StorageCDO->Test_TrySetStorageProviderClass(
        TSoftClassPtr<UCk_GameSettings_StorageProvider_UE>{
            UCkTest_GameSettings_TransientStorageProvider::StaticClass()}))
    { AddError(TEXT("disposable GameSettings provider selection rejected")); return false; }
    Run->StorageChanged = true;
    if (StorageCDO->Get_StorageProviderClass().Get() !=
        UCkTest_GameSettings_TransientStorageProvider::StaticClass())
    { AddError(TEXT("disposable GameSettings provider selection failed")); return false; }
    Run->OldClass = InputCDO->UserSettingsClass;
    Run->OldSlot = InputCDO->InputSettingsSaveSlotName;
    Run->DisposableSlot = FString::Printf(TEXT("CkTestGameSettingsGym_%s"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if (UGameplayStatics::DoesSaveGameExist(Run->DisposableSlot,0) ||
        UGameplayStatics::DoesSaveGameExist(Run->DisposableSlot,1))
    { AddError(TEXT("disposable profile slot already exists")); return false; }
    InputCDO->UserSettingsClass = UCkTest_GameSettings_TransientInputSettings::StaticClass();
    InputCDO->InputSettingsSaveSlotName = Run->DisposableSlot;
    Run->SettingsChanged = true;
    Startup->StartupMode = ECkGym_StartupMode::Cycler;

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(WorldCount,Entry,Parameters,Run->BaseMode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(WorldCount,60));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]()
        { auto* W=WorldAt(0); auto* GI=IsValid(W)?W->GetGameInstance():nullptr;
          auto* R=IsValid(GI)?GI->GetSubsystem<UCkGym_Registry_Subsystem>():nullptr;
          return IsValid(R)&&R->Find_GymIndexByName(GymName)!=INDEX_NONE; }),
        30,TEXT("real Game Settings gym is registered")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W)
        { auto* PC=Local(W); TestTrue(TEXT("owning client requests registered Game Settings travel"),
          IsValid(PC)&&UCk_Utils_GymRegistry_UE::TryRequest_TravelToGymFromPlayer(PC,GymName)); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() { return Context(*Run); }),
        60,TEXT("Game Settings mode and exact local controller converge in CS/LS worlds")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() { return OwnedScreens(*Run,false); }),
        30,TEXT("each local owner has exactly one own screen and five definitions; dedicated server has none")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() { return OwnedScreens(*Run,true); }),
        TEXT("capture distinct screens belonging to distinct local owners")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this,Run](UWorld* W)
        {
            if (!IsValid(W) || W!=WorldAt(1) || !Context(*Run))
            { AddError(TEXT("toggle owner world changed")); return; }
            auto Screens=Widgets<UCk_GameSettingsGym_Screen>(W);
            auto* Panel=Screens.Num()==1 ?
                Cast<UPanelWidget>(Screens[0]->WidgetTree->RootWidget) : nullptr;
            auto* Row=IsValid(Panel) ?
                Cast<UCk_GameSettingsGym_SettingRow>(Panel->GetChildAt(0)) : nullptr;
            auto* Box=IsValid(Row) ?
                Row->WidgetTree->FindWidget<UCheckBox>(TEXT("Toggle")) : nullptr;
            if (!IsValid(Box) ||
                !UCk_Utils_GameSettings_UE::Get_SettingValue_Bool(
                    W,TEXT("gymdemo.toggle"),false))
            { AddError(TEXT("mounted authored toggle is unavailable")); return; }
            Box->OnCheckStateChanged.Broadcast(false);
            Run->ValueCommitted=true;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        {
            if (!Run->ValueCommitted || !Context(*Run) ||
                !IsValid(WorldAt(1)) || !IsValid(WorldAt(2)) ||
                (!Run->Dedicated && !IsValid(WorldAt(0))) ||
                UCk_Utils_GameSettings_UE::Get_SettingValue_Bool(
                    WorldAt(1),TEXT("gymdemo.toggle"),true)) return false;
            if (!UCk_Utils_GameSettings_UE::Get_SettingValue_Bool(
                WorldAt(2),TEXT("gymdemo.toggle"),false)) return false;
            return Run->Dedicated ||
                UCk_Utils_GameSettings_UE::Get_SettingValue_Bool(
                    WorldAt(0),TEXT("gymdemo.toggle"),false);
        }),
        5,TEXT("mounted toggle delegate commits only the owning GameInstance value")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this,Run](UWorld* W)
        {
            if (!IsValid(W) || W!=WorldAt(1) || !Context(*Run))
            { AddError(TEXT("slider owner world changed")); return; }
            auto Screens=Widgets<UCk_GameSettingsGym_Screen>(W);
            auto* Panel=Screens.Num()==1 ?
                Cast<UPanelWidget>(Screens[0]->WidgetTree->RootWidget) : nullptr;
            auto* Row=IsValid(Panel) ?
                Cast<UCk_GameSettingsGym_SettingRow>(Panel->GetChildAt(1)) : nullptr;
            auto* Slider=IsValid(Row) ?
                Row->WidgetTree->FindWidget<USlider>(TEXT("Slider")) : nullptr;
            if (!IsValid(Slider) || !FMath::IsNearlyEqual(
                UCk_Utils_GameSettings_UE::Get_SettingValue_Float(
                    W,TEXT("gymdemo.volume"),-1.f),0.75f))
            { AddError(TEXT("mounted authored slider is unavailable")); return; }
            Slider->OnMouseCaptureBegin.Broadcast();
            Slider->OnValueChanged.Broadcast(0.25f);
            TestTrue(TEXT("slider drag previews without committing"),FMath::IsNearlyEqual(
                UCk_Utils_GameSettings_UE::Get_SettingValue_Float(
                    W,TEXT("gymdemo.volume"),-1.f),0.75f));
            Slider->OnMouseCaptureEnd.Broadcast();
            Run->SliderCommitSent=true;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        {
            return Run->SliderCommitSent && Context(*Run) &&
                IsValid(WorldAt(1)) && IsValid(WorldAt(2)) &&
                (Run->Dedicated || IsValid(WorldAt(0))) && FMath::IsNearlyEqual(
                UCk_Utils_GameSettings_UE::Get_SettingValue_Float(
                    WorldAt(1),TEXT("gymdemo.volume"),-1.f),0.25f) &&
                FMath::IsNearlyEqual(UCk_Utils_GameSettings_UE::Get_SettingValue_Float(
                    WorldAt(2),TEXT("gymdemo.volume"),-1.f),0.75f) &&
                (Run->Dedicated || FMath::IsNearlyEqual(
                    UCk_Utils_GameSettings_UE::Get_SettingValue_Float(
                        WorldAt(0),TEXT("gymdemo.volume"),-1.f),0.75f));
        }),
        5,TEXT("slider capture-end commits only owner value after preview")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this,Run](UWorld* W)
        { Run->KInjected=Inject(Local(W),EKeys::K); TestTrue(TEXT("K enters real local HUD input source"),Run->KInjected); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { auto* W=WorldAt(1); auto P=Widgets<UCk_GameSettingsUI_KeyBindingPageWidget>(W);
          if (!Run->KInjected||!Run->Owner[1].IsValid()||P.Num()!=1||
              P[0]->GetOwningPlayer()!=Run->Owner[1].Get()||
              P[0]->GetOwningLocalPlayer()!=Run->Owner[1]->GetLocalPlayer()||
              P[0]->GetClass()!=UCk_GameSettingsGym_KeyPage::StaticClass()||
              !Cast<UCk_GameSettingsGym_KeyPage>(P[0])->Get_IsLayoutReady()||
              !Cast<UCk_GameSettingsGym_KeyPage>(P[0])->Get_HasCompleteMountedRows()||
              !P[0]->IsInViewport()) return false;
          auto* KeyRows = P[0]->WidgetTree->FindWidget<UVerticalBox>(TEXT("KeyRows"));
          if (!IsValid(KeyRows) ||
              KeyRows->GetChildrenCount()!=P[0]->Get_GeneratedRowCount()) return false;
          for (int32 K=0; K<KeyRows->GetChildrenCount(); ++K)
              if (!IsValid(Cast<UCk_GameSettingsGym_KeyRow>(KeyRows->GetChildAt(K))))
                  return false;
          Run->FirstPage=P[0];
          return Widgets<UCk_GameSettingsUI_KeyBindingPageWidget>(WorldAt(2)).IsEmpty() &&
              (Run->Dedicated || Widgets<UCk_GameSettingsUI_KeyBindingPageWidget>(WorldAt(0)).IsEmpty()); }),
        10,TEXT("real K capture toggles only client 0 owned page")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this,Run](UWorld* W)
        { auto Screens=Widgets<UCk_GameSettingsUI_ScreenWidget>(W);
          if (Screens.Num()!=1) { AddError(TEXT("client 0 screen missing before G")); return; }
          Screens[0]->RemoveFromParent();
          Run->GInjected=Inject(Local(W),EKeys::G);
          TestTrue(TEXT("G enters real local HUD input source"),Run->GInjected); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { auto Screens=Widgets<UCk_GameSettingsUI_ScreenWidget>(WorldAt(1));
          if (!(Run->GInjected && Run->Owner[1].IsValid() && Screens.Num()==1 &&
              Screens[0]->IsInViewport() &&
              Screens[0]->GetOwningPlayer()==Run->Owner[1].Get() &&
              Screens[0]->GetOwningLocalPlayer()==Run->Owner[1]->GetLocalPlayer() &&
              Screens[0]!=Run->FirstScreen[1].Get() &&
              Run->FirstScreen[2].IsValid() && Run->FirstScreen[2]->IsInViewport()))
          { return false; }
          Run->ReopenedScreen=Screens[0];
          return true; }),
        10,TEXT("real G capture reopens only the requesting local player's screen")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W)
        { auto* PC=Local(W);
          TestTrue(TEXT("second owning client requests travel away"),IsValid(PC)&&
            UCk_Utils_GymRegistry_UE::TryRequest_TravelToGymFromPlayer(PC,TEXT("Attribute Basic"))); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]()
        { auto* W=WorldAt(0); auto* GI=IsValid(W)?W->GetGameInstance():nullptr;
          auto* R=IsValid(GI)?GI->GetSubsystem<UCkGym_Registry_Subsystem>():nullptr;
          return IsValid(R)&&R->Get_CurrentGymIndex()==R->Find_GymIndexByName(TEXT("Attribute Basic")); }),
        60,TEXT("registered client travel away lands in Attribute Basic")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { for (int32 I=0;I<WorldCount;++I)
          { if (Run->FirstScreen[I].IsValid() && Run->FirstScreen[I]->IsInViewport()) return false;
            if (!Widgets<UCk_GameSettingsUI_ScreenWidget>(WorldAt(I)).IsEmpty() ||
                !Widgets<UCk_GameSettingsUI_KeyBindingPageWidget>(WorldAt(I)).IsEmpty()) return false; }
          return (!Run->FirstPage.IsValid() || !Run->FirstPage->IsInViewport()) &&
              (!Run->ReopenedScreen.IsValid() || !Run->ReopenedScreen->IsInViewport()); }),
        20,TEXT("travel releases all authored Game Settings owner widgets")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]()
        { for (const auto& Old : Run->FirstScreen)
          { if (Old.IsValid() && Old->IsInViewport()) return false; }
          return (!Run->FirstPage.IsValid() || !Run->FirstPage->IsInViewport()) &&
              (!Run->ReopenedScreen.IsValid() || !Run->ReopenedScreen->IsInViewport()); }),
        TEXT("EndPIE leaves neither initial nor reopened owned widgets mounted")));
    ADD_LATENT_AUTOMATION_COMMAND(FRestore(Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]()
        { auto* D=GetDefault<UEnhancedInputDeveloperSettings>();
          auto* StorageCDO=GetDefault<UCk_GameSettings_ProjectSettings_UE>();
          return IsValid(D)&&IsValid(StorageCDO)&&
              StorageCDO->Get_StorageProviderClass()==Run->OldStorageClass&&
              !Run->PinnedOriginalStorageClass.IsValid()&&
              D->UserSettingsClass==Run->OldClass&&
              D->InputSettingsSaveSlotName==Run->OldSlot&&
              !UGameplayStatics::DoesSaveGameExist(Run->DisposableSlot,0)&&
              !UGameplayStatics::DoesSaveGameExist(Run->DisposableSlot,1); }),
        TEXT("disposable settings leave existing user profiles and CDO settings untouched")));
    return true;
}
#endif
