#pragma once

#include "CkUICore/UserWidget/CkActivatableWidget.h"
#include "CkTests/UI/CkInventoryMenuProof.h"
#include "CkInventory/Inventory/DataOnly/CkInventory_DataOnly_Fragment_Data.h"
#include "CkCore/Time/CkTime.h"
#include "Containers/Ticker.h"

#include "CkPauseMenuProof.generated.h"

class FCkUiView;
class FCkUiCollection;
class FCkUiWidgetRegistrySnapshot;
class FCkPauseMenuInputReleaseGuard;
class FCkInventoryMenuProofModel;
class SButton;
class UCk_UI_AuthoredSurface_UE;
class UTexture2D;
class UCk_UI_LayerMembership_UE;
class UCk_UI_Layout_Subsystem_UE;
class UCk_UI_PrimaryGameLayout_UE;
class UCk_UI_LayerStack_UE;
class ULocalPlayer;

// --------------------------------------------------------------------------------------------------------------------

UENUM(BlueprintType)
enum class ECk_UiAuthoringProof_Screen : uint8
{
    Pause,
    Settings,
    Inventory
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCk_UiAuthoringProof_Settings
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(FCk_UiAuthoringProof_Settings);

private:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    float _Volume = 75.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    bool _Subtitles = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    FString _DisplayMode = TEXT("Windowed");

public:
    CK_PROPERTY(_Volume);
    CK_PROPERTY(_Subtitles);
    CK_PROPERTY(_DisplayMode);

    auto operator==(const FCk_UiAuthoringProof_Settings& InOther) const -> bool
    {
        return FMath::IsNearlyEqual(_Volume, InOther._Volume)
            && _Subtitles == InOther._Subtitles && _DisplayMode == InOther._DisplayMode;
    }
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API UCk_PauseMenuProof_UE : public UCk_ActivatableWidget_UE
{
    GENERATED_BODY()

public:
    UCk_PauseMenuProof_UE(const FObjectInitializer& InObjectInitializer);

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Pause Proof")
    static UCk_PauseMenuProof_UE*
    Open(APlayerController* InPlayerController);

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Pause Proof")
    static UCk_PauseMenuProof_UE*
    OpenWithSettings(APlayerController* InPlayerController, ECk_UiAuthoringProof_Screen InScreen,
        const FCk_UiAuthoringProof_Settings& InSettings);

    /** Snapshots seed a new host. Reusing an active host preserves its accepted state and switches only the screen. */
    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Authoring Proof")
    static UCk_PauseMenuProof_UE*
    OpenWithProofState(APlayerController* InPlayerController, ECk_UiAuthoringProof_Screen InScreen,
        const FCk_UiAuthoringProof_Settings& InSettings, const FCk_UiAuthoringProof_InventoryState& InInventory);

    /** Standalone gameplay inventory: input is captured, but ECS keeps running. Never seeds fixture state. */
    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Authoring Proof")
    static UCk_PauseMenuProof_UE*
    OpenWithGameplayInventory(APlayerController* InPlayerController, const FCk_Handle_Inventory_DataOnly& InInventory);

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Authoring Proof")
    bool Get_UsesGameplayInventory() const { return _UsesGameplayInventory; }

    /** Prepare without changing stack/input/pause. Commit or close before the next CoreTicker pump, or preparation expires. */
    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Authoring Proof")
    static UCk_PauseMenuProof_UE*
    TryPrepareSettingsChild(APlayerController* InController, UCommonActivatableWidget* InParent,
        const FCk_UiAuthoringProof_Settings& InSettings, TArray<FString>& OutErrors);

    /** One-shot commit over the exact admitted parent. A failed fresh commit retires only this candidate. */
    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Authoring Proof")
    bool
    TryPushPreparedSettingsChild(TArray<FString>& OutErrors);

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Authoring Proof")
    FCk_UiAuthoringProof_InventoryState
    Get_InventoryState() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Authoring Proof")
    FString
    Get_InventorySummary() const;

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Pause Proof")
    bool
    Request_ShowScreen(ECk_UiAuthoringProof_Screen InScreen, bool InReturnToPause = false);

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Pause Proof")
    ECk_UiAuthoringProof_Screen
    Get_Screen() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Pause Proof")
    FCk_UiAuthoringProof_Settings
    Get_AppliedSettings() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Pause Proof")
    FCk_UiAuthoringProof_Settings
    Get_DraftSettings() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Pause Proof")
    bool
    Get_IsSettingsDirty() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Pause Proof")
    FString
    Get_SettingsSummary() const;

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Pause Proof")
    void
    Request_Close();

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Pause Proof")
    bool
    Request_SetActionEnabled(FName InAction, bool InEnabled);

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Pause Proof")
    bool
    Request_SetLabel(FName InBinding, FText InText);

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Pause Proof")
    bool
    Request_ApplySource(const FString& InMarkup, const FString& InStylesheet);

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Pause Proof")
    FString
    Get_SourceError() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Pause Proof")
    int32
    Get_ActionCount(FName InAction) const;

    auto Get_AuthoredView() const -> TSharedPtr<FCkUiView>;
    auto Get_SlateUserIndex() const -> int32;
    auto Get_ControlWidget(FName InId) const -> TSharedPtr<SWidget>;
    auto Get_InventoryModel() const -> TSharedPtr<FCkInventoryMenuProofModel>;

protected:
    auto RebuildWidget() -> TSharedRef<SWidget> override;
    auto NativeOnActivated() -> void override;
    auto NativeOnDeactivated() -> void override;
    auto NativeDestruct() -> void override;
    auto NativeGetDesiredFocusTarget() const -> UWidget* override;
    auto NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InEvent) -> FReply override;
    auto NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InEvent) -> FReply override;
    auto NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InEvent) -> FReply override;
    auto NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InEvent) -> FReply override;
    auto NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InEvent) -> FReply override;
    auto NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InEvent) -> FReply override;

private:
    static auto DoOpen(APlayerController* InPlayerController, ECk_UiAuthoringProof_Screen InScreen,
        const FCk_UiAuthoringProof_Settings& InSettings, const FCk_UiAuthoringProof_InventoryState& InInventory,
        const FCk_Handle_Inventory_DataOnly* InGameplayInventory) -> UCk_PauseMenuProof_UE*;
    auto DoPrepare() -> bool;
    auto DoCreateView(ECk_UiAuthoringProof_Screen InScreen) -> TSharedPtr<FCkUiView>;
    auto DoValidateSource(const TSharedPtr<FCkUiView>& InView, ECk_UiAuthoringProof_Screen InScreen,
        const FString& InMarkup, const FString& InStyle, FString& OutError) const -> bool;
    auto DoRelease() -> void;
    auto DoAction(FName InAction) -> void;
    auto DoNavigate(int32 InDirection) -> void;
    auto DoGetFocusIds() const -> TArray<FName>;
    auto DoNavigateInventoryCards(int32 InOffset) -> bool;
    auto DoGetFocusableWidget(int32 InIndex) -> TSharedPtr<SWidget>;
    auto DoFocus(int32 InIndex) -> bool;
    auto DoPoll(float InDeltaTime) -> bool;
    auto DoReloadFiles() -> bool;
    auto DoHasChildContext(bool InRequireActiveParent) const -> bool;
    auto DoCanDispatch() const -> bool;
    auto DoCancelSettingsPreparationExpiry() -> void;

    UFUNCTION()
    void HandleSettingsParentRetired(UCk_UI_LayerMembership_UE* InMembership);

    UPROPERTY(Transient)
    TObjectPtr<UCk_UI_AuthoredSurface_UE> _Surface;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> _Backdrop;

    UPROPERTY(Transient)
    TObjectPtr<UCk_UI_LayerMembership_UE> _SettingsParentMembership;

    TWeakObjectPtr<UCommonActivatableWidget> _SettingsParent;
    TWeakObjectPtr<APlayerController> _SettingsController;
    TWeakObjectPtr<ULocalPlayer> _SettingsPlayer;
    TWeakObjectPtr<UWorld> _SettingsWorld;
    TWeakObjectPtr<UCk_UI_Layout_Subsystem_UE> _SettingsSubsystem;
    TWeakObjectPtr<UCk_UI_PrimaryGameLayout_UE> _SettingsLayout;
    TWeakObjectPtr<UCk_UI_LayerStack_UE> _SettingsLayer;
    TSharedPtr<SWidget> _PreparedSettingsRoot;
    FTSTicker::FDelegateHandle _SettingsPreparationExpiryHandle;
    uint64 _SettingsChildGeneration = 0;
    bool _IsSettingsChild = false;
    bool _SettingsChildPushStarted = false;
    bool _SettingsChildCommitted = false;
    bool _SettingsChildReleased = false;
    bool _SettingsChildClosing = false;

    UPROPERTY(Transient)
    TMap<FName, FText> _Labels;

    TMap<FName, int32> _ActionCounts;
    TMap<FName, bool> _ActionEnabled;
    TSharedPtr<FCkUiView> _View;
    TSharedPtr<FCkUiCollection> _DisplayModes;
    TSharedPtr<FCkInventoryMenuProofModel> _InventoryModel;
    FCk_UiAuthoringProof_InventoryState _InventoryInitialState;

    UPROPERTY(Transient)
    FCk_Handle_Inventory_DataOnly _GameplayInventory;

    bool _UsesGameplayInventory = false;
    TSharedPtr<const FCkUiWidgetRegistrySnapshot> _ControlRegistry;
    FCk_UiAuthoringProof_Settings _AppliedSettings;
    FCk_UiAuthoringProof_Settings _DraftSettings;
    ECk_UiAuthoringProof_Screen _Screen = ECk_UiAuthoringProof_Screen::Pause;
    bool _SettingsReturnToPause = false;
    bool _SwitchingScreen = false;
    TOptional<float> _VolumePreview;
    TSharedPtr<FCkPauseMenuInputReleaseGuard> _InputReleaseGuard;
    TWeakPtr<SWidget> _PreviousFocus;
    FTSTicker::FDelegateHandle _PollHandle;
    FCk_Time _PollElapsed;
    FString _LastAttemptMarkup;
    FString _LastAttemptStyle;
    FString _SourceError;
    bool _HasSourceAttempt = false;
    int32 _SlateUserIndex = INDEX_NONE;
    bool _OwnsPause = false;
    bool _NeedsInitialFocus = false;
};
