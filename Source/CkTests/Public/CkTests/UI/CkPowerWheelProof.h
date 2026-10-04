#pragma once

#include "CkUICore/UserWidget/CkActivatableWidget.h"
#include "Containers/Ticker.h"
#include "CkPowerWheelProof.generated.h"

class FCkUiView;
class FCkUiCollection;
class FCkUiWidgetRegistrySnapshot;
class FCkPowerWheelProofModel;
class FCkPowerWheelInput;
class UCk_UI_AuthoredSurface_UE;
class UTexture2D;
struct FSlateBrush;

// --------------------------------------------------------------------------------------------------------------------

/** Persistent gym fixture; presentation and pause are acquired only while the wheel is open. */
UCLASS(BlueprintType)
class CKTESTS_API UCk_PowerWheelProof_UE : public UCk_ActivatableWidget_UE
{
    GENERATED_BODY()

public:
    UCk_PowerWheelProof_UE(const FObjectInitializer& InInitializer);

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Power Wheel")
    static UCk_PowerWheelProof_UE*
    Create(APlayerController* InController);

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Power Wheel")
    bool
    Request_Open();

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Power Wheel")
    bool
    Request_Deploy();

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Power Wheel")
    void
    Request_Cancel();

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Power Wheel")
    void
    Request_Shutdown();

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Power Wheel")
    bool
    Request_ToggleTarget();

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Power Wheel")
    bool
    Request_ResetFixture();

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Power Wheel")
    bool
    IsWheelOpen() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Power Wheel")
    FString
    Get_Summary() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Power Wheel")
    FString
    Get_SourceError() const;

    auto Request_ApplySource(const FString& InMarkup, const FString& InStyle) -> bool;
    auto Get_AuthoredView() const -> TSharedPtr<FCkUiView> { return _View; }
    auto Get_Model() const -> TSharedPtr<FCkPowerWheelProofModel> { return _Model; }
    auto Get_SlateUserIndex() const -> int32 { return _UserIndex; }

protected:
    auto RebuildWidget() -> TSharedRef<SWidget> override;
    auto NativeOnDeactivated() -> void override;
    auto NativeDestruct() -> void override;
    auto BeginDestroy() -> void override;
    auto NativeGetDesiredFocusTarget() const -> UWidget* override;
    auto NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InEvent) -> FReply override;

private:
    friend class FCkPowerWheelInput;
    auto DoOpen(FKey InHoldKey) -> bool;
    auto DoPrepare() -> bool;
    auto DoSync() -> bool;
    auto DoReload() -> bool;
    auto DoTick(float InDeltaTime) -> bool;
    auto DoClosePresentation() -> void;
    auto DoText(const FString& InName) const -> FText;
    auto DoSelect(const FString& InKey, bool InQueue) -> void;
    auto DoFocus() -> bool;
    auto CanOpenFromInput() const -> bool;

    UPROPERTY(Transient)
    TObjectPtr<UCk_UI_AuthoredSurface_UE> _Surface;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> _PortraitTexture;

    TSharedPtr<FCkPowerWheelProofModel> _Model;
    TSharedPtr<FCkUiCollection> _Powers;
    TSharedPtr<FCkUiView> _View;
    TSharedPtr<const FCkUiWidgetRegistrySnapshot> _Registry;
    TSharedPtr<FCkPowerWheelInput> _Input;
    TArray<TSharedPtr<FSlateBrush>> _Portraits;
    TWeakPtr<SWidget> _PreviousFocus;
    FTSTicker::FDelegateHandle _TickHandle;
    FKey _HoldKey;
    int32 _UserIndex = INDEX_NONE;
    bool _Alive = false;
    bool _Open = false;
    bool _OwnsPause = false;
    bool _HadCursor = false;
    bool _NeedsFocus = false;
    float _PollRemaining = 0.0f;
    float _OpenElapsed = 0.0f;
    FString _LastMarkup;
    FString _LastStyle;
    FString _SourceError;
    FString _Status = TEXT("Hold Shift to open the power wheel.");
};
