#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/UI/CkGameplayInventoryProof.h"
#include "CkTests/UI/CkPauseMenuProof.h"
#include "CkInventory/Inventory/CkInventory_Utils.h"
#include "CkInventory/Item/CkItem_Definition.h"
#include "CkInventory/Item/CkItem_Utils.h"
#include "CkInventory/ItemTrait/Stackable/CkItemTrait_Stackable_Utils.h"
#include "UI/CkInventoryMenuProofModel.h"

#include "CommonInputSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SViewport.h"
#include "Layout/WidgetPath.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Types/SlateAttributeMetaData.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_gameplay_inventory_menu_proof_test
{
    constexpr TCHAR GymName[] = TEXT("UI Authoring");
    constexpr TCHAR GymModeName[] = TEXT("Ck_UiAuthoringGym_GameMode");

    struct FState
    {
        ~FState()
        {
            if (Owner.IsValid()) { Owner->Release(); }
            for (const FString& OwnedSlot : OwnedSlots) { UGameplayStatics::DeleteGameInSlot(OwnedSlot, 0); }
            if (!bSettingsCaptured) { return; }
            UCkGym_StartupSettings* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            if (!IsValid(Settings)) { return; }
            Settings->StartupMode = StartupMode;
            Settings->ControlPanelMode = PanelMode;
            Settings->DefaultGymName = DefaultGymName;
            Settings->LastGymName = LastGymName;
            Settings->RecentGymNames = RecentGymNames;
            Settings->SaveConfig();
        }

        TStrongObjectPtr<UCk_GameplayInventoryProof_UE> Owner;
        TWeakObjectPtr<UCk_PauseMenuProof_UE> Menu;
        TSharedPtr<FCkInventoryMenuProofModel> RetainedModel;
        TMap<FString, int32> InitialTotals;
        TMap<FString, int32> SavedTotals;
        FString Slot = FString{TEXT("GameplayInventoryMenuProof_")}
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        TArray<FString> OwnedSlots;
        ECkGym_StartupMode StartupMode = ECkGym_StartupMode::Cycler;
        ECkGym_ControlPanel_Mode PanelMode = ECkGym_ControlPanel_Mode::Full;
        FString DefaultGymName;
        FString LastGymName;
        TArray<FString> RecentGymNames;
        bool bSettingsCaptured = false;
    };

    auto Registry(UWorld* World) -> UCkGym_Registry_Subsystem*
    {
        UGameInstance* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    auto Player(UWorld* World) -> APlayerController*
    { return IsValid(World) ? World->GetFirstPlayerController() : nullptr; }

    auto GymReady(UWorld* World) -> bool
    {
        const AGameModeBase* Mode = IsValid(World) ? World->GetAuthGameMode() : nullptr;
        return IsValid(Mode) && Mode->GetClass()->GetName().Contains(GymModeName)
            && IsValid(Player(World)) && IsValid(Player(World)->GetLocalPlayer());
    }

    auto Totals(const FCk_Handle_Inventory_DataOnly& Inventory) -> TMap<FString, int32>
    {
        TMap<FString, int32> Result;
        if (!ck::IsValid(Inventory)) { return Result; }
        for (const FCk_Handle_Item& Item : UCk_Utils_Inventory_UE::Get_Items(Inventory))
        {
            if (!ck::IsValid(Item)) { continue; }
            const UCk_InventoryItem_Definition* Definition = UCk_Utils_Item_UE::Get_Definition(Item);
            if (!IsValid(Definition)) { continue; }
            const int32 Quantity = UCk_Utils_ItemTrait_Stackable_UE::Get_IsStackable(Item)
                ? UCk_Utils_ItemTrait_Stackable_UE::Get_StackCount(Item) : 1;
            const FString Name = Definition->Get_CoreInfo().Get_Name().ToString();
            const FString Catalog = Name.Contains(TEXT("Potion")) ? TEXT("Potion")
                : Name.Contains(TEXT("Arrow")) ? TEXT("Arrow")
                : Name.Contains(TEXT("Sword")) ? TEXT("Sword") : Name;
            Result.FindOrAdd(Catalog) += Quantity;
        }
        return Result;
    }

    auto KeyForName(const FCk_Handle_Inventory_DataOnly& Inventory, const TCHAR* Name) -> FString
    {
        for (const FCk_Handle_Item& Item : UCk_Utils_Inventory_UE::Get_Items(Inventory))
        {
            const UCk_InventoryItem_Definition* Definition = ck::IsValid(Item)
                ? UCk_Utils_Item_UE::Get_Definition(Item) : nullptr;
            if (IsValid(Definition) && Definition->Get_CoreInfo().Get_Name().ToString().Contains(Name))
            { return Item.ToString(); }
        }
        return {};
    }

    auto PressSyntheticKey(const FKey& Key, int32 User) -> bool
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        const FKeyEvent Event{Key, FModifierKeysState{}, static_cast<uint32>(User), false, 0, 0};
        const bool Down = Slate.ProcessKeyDownEvent(Event);
        const bool Up = Slate.ProcessKeyUpEvent(Event);
        Slate.PumpMessages();
        Slate.Tick();
        Slate.Tick();
        return Down && Up;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_GameplayInventoryMenuProof_PIE,
    "Ck.UiAuthoring.GameplayInventoryMenuProof.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
        | EAutomationTestFlags::NonNullRHI)

bool FCk_GameplayInventoryMenuProof_PIE::RunTest(const FString&)
{
    using namespace ck_gameplay_inventory_menu_proof_test;
    if (!FSlateApplication::IsInitialized())
    { AddError(TEXT("Gameplay Inventory proof requires Slate.")); return false; }

    FString InvalidError;
    TestNull(TEXT("null world context rejects without composing a backend"),
        UCk_GameplayInventoryProof_UE::TryCreate(nullptr, InvalidError));
    TestFalse(TEXT("null-context rejection provides an explicit reason"), InvalidError.IsEmpty());
    TSharedPtr<FCkInventoryMenuProofModel> InvalidModel;
    TestFalse(TEXT("default inventory rejects before reading backend state"),
        FCkInventoryMenuProofModel::TryCreateForInventory(FCk_Handle_Inventory_DataOnly{},
            FString{}, InvalidModel, InvalidError));
    TestTrue(TEXT("invalid input publishes no model and provides a reason"),
        !InvalidModel.IsValid() && !InvalidError.IsEmpty());

    const TSharedRef<FState> State = MakeShared<FState>();
    UCkGym_StartupSettings* Settings = GetMutableDefault<UCkGym_StartupSettings>();
    if (!TestNotNull(TEXT("gym startup settings"), Settings)) { return false; }
    State->StartupMode = Settings->StartupMode;
    State->PanelMode = Settings->ControlPanelMode;
    State->DefaultGymName = Settings->DefaultGymName;
    State->LastGymName = Settings->LastGymName;
    State->RecentGymNames = Settings->RecentGymNames;
    State->bSettingsCaptured = true;
    Settings->StartupMode = ECkGym_StartupMode::Cycler;
    Settings->ControlPanelMode = ECkGym_ControlPanel_Mode::Full;

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1,
        TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UCkGym_Registry_Subsystem* Found = Registry(ck::auto_test::net::Get_ServerWorld());
            return IsValid(Found) && Found->Find_GymIndexByName(GymName) != INDEX_NONE;
        }), 15.0, TEXT("UI Authoring gym registers")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World) -> void
        {
            UCkGym_Registry_Subsystem* Found = Registry(World);
            if (!TestNotNull(TEXT("gym registry"), Found)) { return; }
            const int32 Index = Found->Find_GymIndexByName(GymName);
            if (TestTrue(TEXT("UI Authoring gym entry exists"), Found->Get_Entries().IsValidIndex(Index)))
            { Found->Request_TravelToGym(Index); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return GymReady(ck::auto_test::net::Get_ServerWorld()); }),
        30.0, TEXT("real UI Authoring gym travel completes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World) -> void
        {
            FString Error;
            State->Owner.Reset(UCk_GameplayInventoryProof_UE::TryCreate(World, Error));
            TestTrue(TEXT("gameplay owner starts from actual ECS inventory"),
                State->Owner.IsValid() && Error.IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        { return State->Owner.IsValid() && State->Owner->Get_IsReady(); }),
        20.0, TEXT("deferred gameplay inventory seed commits")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World) -> void
        {
            if (!TestTrue(TEXT("owner has a committed DataOnly inventory"),
                State->Owner.IsValid() && ck::IsValid(State->Owner->Get_Inventory()))) { return; }
            State->InitialTotals = Totals(State->Owner->Get_Inventory());
            TestTrue(TEXT("ECS has committed Potion x6, Arrow x24 and Sword x1"),
                State->InitialTotals.Num() == 3
                    && State->InitialTotals.FindRef(TEXT("Potion")) == 6
                    && State->InitialTotals.FindRef(TEXT("Arrow")) == 24
                    && State->InitialTotals.FindRef(TEXT("Sword")) == 1);
            UCk_PauseMenuProof_UE* Menu = UCk_PauseMenuProof_UE::OpenWithGameplayInventory(
                Player(World), State->Owner->Get_Inventory());
            State->Menu = Menu;
            if (!TestNotNull(TEXT("gameplay inventory host opens"), Menu)) { return; }
            const TSharedPtr<FCkInventoryMenuProofModel> Model = Menu->Get_InventoryModel();
            if (!TestTrue(TEXT("host mounts gameplay model and view"),
                Menu->IsActivated() && Model.IsValid() && Model->Get_IsGameplayMode()
                    && Model->Get_Items().IsValid() && Menu->Get_AuthoredView().IsValid())) { return; }
            TestFalse(TEXT("standalone inventory does not pause gameplay"), World->IsPaused());
            TestEqual(TEXT("visible cards match committed ECS items"),
                Model->Get_VisibleKeys().Num(), UCk_Utils_Inventory_UE::Get_NumItems(State->Owner->Get_Inventory()));
            const TArray<FString> Keys = Model->Get_VisibleKeys();
            if (Keys.Num() >= 2 && Menu->Get_SlateUserIndex() >= 0)
            {
                const auto Card = [Menu](const FString& Key) -> TSharedPtr<SWidget>
                {
                    return Menu->Get_ControlWidget(FName{*FString::Printf(
                        TEXT("inventory-items/%s/inventory-card"), *Key)});
                };
                const TSharedPtr<SWidget> First = Card(Keys[0]);
                const TSharedPtr<SWidget> Second = Card(Keys[1]);
                if (TestTrue(TEXT("native gameplay cards are mounted for synthetic controller routing"),
                    First.IsValid() && Second.IsValid()))
                {
                    FSlateApplication& Slate = FSlateApplication::Get();
                    const int32 User = Menu->Get_SlateUserIndex();
                    Slate.SetUserFocus(User, First.ToSharedRef(), EFocusCause::SetDirectly);
                    TestTrue(TEXT("synthetic D-pad Right routes to next native card"),
                        PressSyntheticKey(EKeys::Gamepad_DPad_Right, User)
                            && Slate.GetUserFocusedWidget(User) == Second);
                    TestTrue(TEXT("synthetic shoulder moves focus through gameplay host"),
                        PressSyntheticKey(EKeys::Gamepad_LeftShoulder, User)
                            && Slate.GetUserFocusedWidget(User) != Second);
                }
            }
            const int64 Revision = Model->Get_Items()->GetRevision();
            TestTrue(TEXT("refresh with unchanged ECS state succeeds"), Model->RefreshGameplay());
            TestEqual(TEXT("unchanged refresh retains collection revision"), Model->Get_Items()->GetRevision(), Revision);
            State->RetainedModel = Model;
            const FString PotionKey = KeyForName(State->Owner->Get_Inventory(), TEXT("Potion"));
            if (!TestTrue(TEXT("seeded Potion has a stable live-world key"), !PotionKey.IsEmpty())) { return; }
            TestTrue(TEXT("selected gameplay item is a live ECS key"),
                (Model->Get_SelectedKey() == PotionKey || Model->Request_Select(PotionKey))
                    && Model->Get_SelectedKey() == PotionKey);
            const TSharedPtr<SWidget> Primary = Menu->Get_ControlWidget(TEXT("inventory-primary-action"));
            if (!TestTrue(TEXT("native discard button is mounted"), Primary.IsValid())) { return; }
            FSlateAttributeMetaData::UpdateExceptVisibilityAttributes(*Primary,
                FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);
            FSlateApplication::Get().SetUserFocus(Menu->Get_SlateUserIndex(), Primary.ToSharedRef(), EFocusCause::SetDirectly);
            FSlateApplication::Get().Tick();
            TestTrue(TEXT("native discard owns enabled focus before controller accept"),
                Primary->IsEnabled() && FSlateApplication::Get().GetUserFocusedWidget(Menu->Get_SlateUserIndex()) == Primary);
            // CommonUI converts virtual Accept into a pointer click. Position the synthetic
            // cursor explicitly; this gate does not prove physical focus-to-cursor synchronization.
            const TSharedPtr<FSlateUser> SlateUser = FSlateApplication::Get().GetUser(Menu->Get_SlateUserIndex());
            if (!TestTrue(TEXT("controller Slate user exists"), SlateUser.IsValid())) { return; }
            const FGeometry Geometry = Primary->GetCachedGeometry();
            TestTrue(TEXT("native discard has a rendered hit target"), Geometry.GetLocalSize().X > 0.0f && Geometry.GetLocalSize().Y > 0.0f);
            SlateUser->SetCursorPosition(Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f));
            const int32 ActionsBeforeAccept = Model->Get_ActionCount(TEXT("InventoryPrimaryAction"));
            FSlateApplication& Slate = FSlateApplication::Get();
            const auto Button = StaticCastSharedPtr<SButton>(Primary);
            // Cursor assignment alone does not route hover. Native SButton requires hover
            // on the mouse-up produced by CommonUI virtual Accept.
            const FVector2D Cursor = SlateUser->GetCursorPosition();
            const TSet<FKey> PressedButtons;
            const FPointerEvent Move{static_cast<uint32>(Menu->Get_SlateUserIndex()),
                FSlateApplication::CursorPointerIndex, Cursor, Cursor - FVector2D{1.0f, 0.0f},
                PressedButtons, EKeys::Invalid, 0.0f, FModifierKeysState{}};
            Slate.ProcessMouseMoveEvent(Move, true);
            TestTrue(TEXT("synthetic pointer movement establishes native discard hover"), Button->IsHovered());
            const auto TraceAccept = [this, World, Menu, &Slate, SlateUser, Button](const TCHAR* Phase) -> void
            {
                const auto Hit = Slate.LocateWindowUnderMouse(SlateUser->GetCursorPosition(),
                    Slate.GetInteractiveTopLevelWindows(), false, Menu->Get_SlateUserIndex());
                const auto* Input = UCommonInputSubsystem::Get(Player(World)->GetLocalPlayer());
                const auto Viewport = World->GetGameViewport()->GetGameViewportWidget();
                AddInfo(FString::Printf(TEXT("AcceptRoute %s: input=%d viewportFocus=%d buttonPressed=%d hovered=%d capture=%d hitButton=%d leaf=%s cursor=%s buttonPosition=%s buttonSize=%s"),
                    Phase, Input ? static_cast<int32>(Input->GetCurrentInputType()) : -1,
                    SlateUser->IsWidgetInFocusPath(Viewport), Button->IsPressed(), Button->IsHovered(),
                    SlateUser->DoesWidgetHaveAnyCapture(Button), Hit.ContainsWidget(Button.Get()),
                    Hit.IsValid() ? *Hit.GetLastWidget()->GetTypeAsString() : TEXT("none"),
                    *SlateUser->GetCursorPosition().ToString(), *Button->GetCachedGeometry().GetAbsolutePosition().ToString(),
                    *Button->GetCachedGeometry().GetLocalSize().ToString()));
            };
            const FKeyEvent Accept{EKeys::Gamepad_FaceButton_Bottom, FModifierKeysState{},
                static_cast<uint32>(Menu->Get_SlateUserIndex()), false, 0, 0};
            AddInfo(FString::Printf(TEXT("Accept navigation action=%d"), static_cast<int32>(Slate.GetNavigationActionFromKey(Accept))));
            TraceAccept(TEXT("before"));
            const bool AcceptDown = Slate.ProcessKeyDownEvent(Accept);
            TraceAccept(TEXT("down"));
            const bool AcceptUp = Slate.ProcessKeyUpEvent(Accept);
            TraceAccept(TEXT("up"));
            TestTrue(TEXT("synthetic gamepad A input is processed"), AcceptDown && AcceptUp);
            TestEqual(TEXT("controller accept dispatches exactly one native action"),
                Model->Get_ActionCount(TEXT("InventoryPrimaryAction")), ActionsBeforeAccept + 1);
            TestTrue(TEXT("request is pending before ECS processing"), Model->Get_IsGameplayRequestPending());
            TestFalse(TEXT("second discard cannot dispatch while pending"), Model->Request_PrimaryAction());
            const FString PendingSlot = FString{TEXT("GIMProofPending_")}
                + FGuid::NewGuid().ToString(EGuidFormats::Digits);
            State->OwnedSlots.Add(PendingSlot);
            FString PendingError;
            TestFalse(TEXT("save refuses an in-flight gameplay request"),
                State->Owner->Request_Save(PendingSlot, PendingError));
            TestTrue(TEXT("pending-save rejection is explicit and writes no slot"),
                !PendingError.IsEmpty() && !UGameplayStatics::DoesSaveGameExist(PendingSlot, 0));
            TestEqual(TEXT("deferred request has not yet mutated committed ECS"),
                Totals(State->Owner->Get_Inventory()).FindRef(TEXT("Potion")),
                State->InitialTotals.FindRef(TEXT("Potion")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return State->Owner.IsValid() && State->RetainedModel.IsValid()
                && !State->RetainedModel->Get_IsGameplayRequestPending()
                && Totals(State->Owner->Get_Inventory()).FindRef(TEXT("Potion"))
                    < State->InitialTotals.FindRef(TEXT("Potion"));
        }), 15.0, TEXT("deferred discard changes committed ECS inventory")));

    // Completion precedes the lifetime cleanup of the discarded item.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World) -> void
        {
            if (!TestTrue(TEXT("owner and model survived deferred gameplay request"),
                State->Owner.IsValid() && State->RetainedModel.IsValid()
                    && ck::IsValid(State->Owner->Get_Inventory()))) { return; }
            const TMap<FString, int32> After = Totals(State->Owner->Get_Inventory());
            TestTrue(TEXT("discard destroys the selected whole stack exactly once"),
                After.FindRef(TEXT("Potion")) == 0
                    && After.FindRef(TEXT("Arrow")) == State->InitialTotals.FindRef(TEXT("Arrow"))
                    && After.FindRef(TEXT("Sword")) == State->InitialTotals.FindRef(TEXT("Sword")));
            TestTrue(TEXT("model refreshes after backend completion"), State->RetainedModel->RefreshGameplay());
            TestEqual(TEXT("authored cards reflect committed ECS count"), State->RetainedModel->Get_VisibleKeys().Num(),
                UCk_Utils_Inventory_UE::Get_NumItems(State->Owner->Get_Inventory()));
            if (UCk_PauseMenuProof_UE* Menu = State->Menu.Get()) { Menu->Request_Close(); }
            TestFalse(TEXT("closed model cannot submit gameplay discard"), State->RetainedModel->Request_PrimaryAction());
            TestEqual(TEXT("closed view leaves ECS unchanged"), Totals(State->Owner->Get_Inventory()).FindRef(TEXT("Arrow")),
                State->InitialTotals.FindRef(TEXT("Arrow")));
            UCk_PauseMenuProof_UE* Reopened = UCk_PauseMenuProof_UE::OpenWithGameplayInventory(
                Player(World), State->Owner->Get_Inventory());
            State->Menu = Reopened;
            TestTrue(TEXT("reopen projects the same committed ECS, without fixture reseed"),
                IsValid(Reopened) && Reopened->Get_InventoryModel().IsValid()
                    && Reopened->Get_InventoryModel()->Get_VisibleKeys().Num()
                        == UCk_Utils_Inventory_UE::Get_NumItems(State->Owner->Get_Inventory())
                    && Totals(State->Owner->Get_Inventory()).OrderIndependentCompareEqual(After));
            State->SavedTotals = After;
            FString Error;
            State->OwnedSlots.Add(State->Slot);
            const bool Saved = State->Owner->Request_Save(State->Slot, Error);
            TestTrue(FString::Printf(TEXT("owner persists current ECS state: %s"), *Error),
                Saved && Error.IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World) -> void
        {
            if (UCk_PauseMenuProof_UE* Menu = State->Menu.Get()) { Menu->Request_Close(); }
            State->Menu.Reset();
            State->RetainedModel.Reset();
            if (State->Owner.IsValid()) { State->Owner->Release(); State->Owner.Reset(); }
            FString Error;
            State->Owner.Reset(UCk_GameplayInventoryProof_UE::TryLoad(World, State->Slot, Error));
            TestTrue(TEXT("owner recreates from persisted slot in the same process"),
                State->Owner.IsValid() && Error.IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        { return State->Owner.IsValid() && State->Owner->Get_IsReady(); }),
        20.0, TEXT("loaded gameplay inventory commits")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World) -> void
        {
            if (!TestTrue(TEXT("loaded owner is ready with a committed ECS source"),
                State->Owner.IsValid() && State->Owner->Get_IsReady()
                    && ck::IsValid(State->Owner->Get_Inventory()))) { return; }
            TestTrue(TEXT("disk load restores the exact committed definition totals"),
                Totals(State->Owner->Get_Inventory()).OrderIndependentCompareEqual(State->SavedTotals));
            UCk_PauseMenuProof_UE* Menu = UCk_PauseMenuProof_UE::OpenWithGameplayInventory(
                Player(World), State->Owner->Get_Inventory());
            State->Menu = Menu;
            TestTrue(TEXT("loaded owner projects the same playable inventory"),
                IsValid(Menu) && Menu->Get_InventoryModel().IsValid()
                    && Menu->Get_InventoryModel()->Get_Items().IsValid()
                    && Menu->Get_InventoryModel()->Get_VisibleKeys().Num()
                        == UCk_Utils_Inventory_UE::Get_NumItems(State->Owner->Get_Inventory()));
            if (!IsValid(Menu) || !Menu->Get_InventoryModel().IsValid()) { return; }
            State->RetainedModel = Menu->Get_InventoryModel();
            const int64 AcceptedRevision = State->RetainedModel->Get_Items()->GetRevision();
            auto* Accepted = Cast<UCk_GameplayInventoryProof_SaveGame_UE>(
                UGameplayStatics::LoadGameFromSlot(State->Slot, 0));
            if (!TestNotNull(TEXT("saved payload can be reopened for rejection fixtures"), Accepted)) { return; }
            const FString WrongVersionSlot = FString{TEXT("GIMProofWrong_")}
                + FGuid::NewGuid().ToString(EGuidFormats::Digits);
            State->OwnedSlots.Add(WrongVersionSlot);
            auto* WrongVersion = NewObject<UCk_GameplayInventoryProof_SaveGame_UE>();
            WrongVersion->Entries = Accepted->Entries;
            WrongVersion->SchemaVersion = 2;
            if (TestTrue(TEXT("wrong-version payload written to distinct slot"),
                UGameplayStatics::SaveGameToSlot(WrongVersion, WrongVersionSlot, 0)))
            {
                FString Error;
                TestNull(TEXT("wrong-version load rejects before ECS recreation"),
                    UCk_GameplayInventoryProof_UE::TryLoad(World, WrongVersionSlot, Error));
                TestFalse(TEXT("wrong-version rejection gives a reason"), Error.IsEmpty());
            }
            const FString MalformedSlot = FString{TEXT("GIMProofBad_")}
                + FGuid::NewGuid().ToString(EGuidFormats::Digits);
            State->OwnedSlots.Add(MalformedSlot);
            auto* Malformed = NewObject<UCk_GameplayInventoryProof_SaveGame_UE>();
            Malformed->Entries = Accepted->Entries;
            if (!Malformed->Entries.IsEmpty()) { Malformed->Entries[0].Quantity = -1; }
            if (TestTrue(TEXT("malformed payload written to distinct slot"),
                UGameplayStatics::SaveGameToSlot(Malformed, MalformedSlot, 0)))
            {
                FString Error;
                TestNull(TEXT("malformed quantity rejects before ECS recreation"),
                    UCk_GameplayInventoryProof_UE::TryLoad(World, MalformedSlot, Error));
                TestFalse(TEXT("malformed rejection gives a reason"), Error.IsEmpty());
            }
            for (int32 InvalidCase = 0; InvalidCase < 3; ++InvalidCase)
            {
                auto* InvalidSave = NewObject<UCk_GameplayInventoryProof_SaveGame_UE>();
                InvalidSave->Entries = Accepted->Entries;
                if (!TestTrue(TEXT("accepted save has complete entries for invalid-input exercises"),
                    InvalidSave->Entries.Num() == 3)) { return; }
                if (InvalidCase == 0) { InvalidSave->Entries[0].CatalogKey = TEXT("unknown-item"); }
                else if (InvalidCase == 1) { InvalidSave->Entries[0].CatalogKey = InvalidSave->Entries[1].CatalogKey; }
                else { InvalidSave->Entries[0].Quantity = MAX_int32; }
                const FString InvalidSlot = FString{TEXT("GIMProofInvalid_")}
                    + FGuid::NewGuid().ToString(EGuidFormats::Digits);
                State->OwnedSlots.Add(InvalidSlot);
                if (!TestTrue(TEXT("invalid catalog payload written to an isolated slot"),
                    UGameplayStatics::SaveGameToSlot(InvalidSave, InvalidSlot, 0))) { continue; }
                FString Error;
                TestNull(TEXT("unknown, duplicate and out-of-range catalog data reject"),
                    UCk_GameplayInventoryProof_UE::TryLoad(World, InvalidSlot, Error));
                TestFalse(TEXT("invalid catalog rejection provides a reason"), Error.IsEmpty());
            }
            TestTrue(TEXT("rejected loads preserve accepted owner, ECS and mounted view"),
                State->Owner->Get_IsReady()
                    && Totals(State->Owner->Get_Inventory()).OrderIndependentCompareEqual(State->SavedTotals)
                    && State->RetainedModel->Get_Items()->GetRevision() == AcceptedRevision
                    && Menu->IsActivated());
            const TArray<FString> LoadedKeys = State->RetainedModel->Get_VisibleKeys();
            const TSharedPtr<SWidget> FirstLoadedCard = !LoadedKeys.IsEmpty()
                ? Menu->Get_ControlWidget(FName{*FString::Printf(
                    TEXT("inventory-items/%s/inventory-card"), *LoadedKeys[0])}) : nullptr;
            if (TestTrue(TEXT("loaded inventory has a focusable native card"),
                FirstLoadedCard.IsValid() && Menu->Get_SlateUserIndex() >= 0))
            {
                const int32 User = Menu->Get_SlateUserIndex();
                FSlateApplication::Get().SetUserFocus(User, FirstLoadedCard.ToSharedRef(), EFocusCause::SetDirectly);
                // Synthetic Slate delivery proves the host route, not a physical controller device.
                PressSyntheticKey(EKeys::Gamepad_FaceButton_Right, User);
                TestFalse(TEXT("synthetic gamepad B closes the non-pausing host"), Menu->IsActivated());
            }
            else { Menu->Request_Close(); }
            State->Menu.Reset();
            State->Owner->Release();
            TestFalse(TEXT("released owner no longer advertises a ready inventory"), State->Owner->Get_IsReady());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*) -> void
        {
            TestTrue(TEXT("released owner invalidates retained gameplay model source"),
                State->RetainedModel.IsValid() && !State->RetainedModel->Get_IsGameplaySourceValid());
            TestFalse(TEXT("retained model cannot refresh a released ECS source"),
                State->RetainedModel.IsValid() && State->RetainedModel->RefreshGameplay());
            TestFalse(TEXT("retained model cannot submit a stale gameplay action"),
                State->RetainedModel.IsValid() && State->RetainedModel->Request_PrimaryAction());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
