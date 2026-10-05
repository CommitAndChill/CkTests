#pragma once

#include "CkUICore/UserWidget/CkActivatableWidget.h"
#include "Containers/Ticker.h"
#include "CkJournalProof.generated.h"

class FCkJournalProofModel;
class FCkJournalInput;
class FCkUiTreeCollection;
class FCkUiView;
class FCkUiWidgetRegistrySnapshot;
class UCk_UI_AuthoredSurface_UE;

UCLASS(BlueprintType)
class CKTESTS_API UCk_JournalProof_UE : public UCk_ActivatableWidget_UE
{
    GENERATED_BODY()

public:
    UCk_JournalProof_UE(const FObjectInitializer& InInitializer);

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Journal")
    static UCk_JournalProof_UE*
    Create(APlayerController* InController);

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Journal")
    bool
    Request_Open();

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Journal")
    void
    Request_Close();

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Journal")
    void
    Request_Shutdown();

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Journal")
    bool
    Request_AdvanceSelected();

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Journal")
    bool
    Request_ToggleStress();

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Journal")
    bool
    Request_Reset();

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Journal")
    bool
    IsJournalOpen() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Journal")
    FString
    Get_Summary() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Journal")
    FString
    Get_SourceError() const;

    auto Get_Model() const -> TSharedPtr<FCkJournalProofModel> { return _Model; }
    auto Get_AuthoredView() const -> TSharedPtr<FCkUiView> { return _View; }
    auto Get_SlateUserIndex() const -> int32 { return _UserIndex; }
    auto Request_ApplySource(const FString& InMarkup, const FString& InStyle) -> bool;

protected:
    auto RebuildWidget() -> TSharedRef<SWidget> override;
    auto NativeGetDesiredFocusTarget() const -> UWidget* override;
    auto NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InEvent) -> FReply override;
    auto NativeOnDeactivated() -> void override;
    auto NativeDestruct() -> void override;
    auto BeginDestroy() -> void override;

private:
    friend class FCkJournalInput;

    auto CanOpenFromInput() const -> bool;
    auto CanHandleOpenInput() const -> bool;
    auto DoPrepare() -> bool;
    auto DoSync() -> bool;
    auto DoReload() -> bool;
    auto DoTick(float InDeltaTime) -> bool;
    auto DoClosePresentation() -> void;
    auto DoFocus() -> bool;
    auto DoGetFocusTarget() const -> TSharedPtr<SWidget>;
    auto DoText(const FString& InName) const -> FText;
    auto DoSelectQuest(TOptional<FString> InKey) -> void;
    auto DoSelectObjective(TOptional<FString> InKey) -> void;
    auto DoSelectTab(const FString& InKey, bool InFromShortcut = false) -> void;
    auto DoToggleSelected(bool InObjectiveTree) -> bool;
    auto DoShowMapTarget() -> bool;
    auto DoBindTreeKeys() -> bool;

    UPROPERTY(Transient)
    TObjectPtr<UCk_UI_AuthoredSurface_UE> _Surface;

    TSharedPtr<FCkJournalProofModel> _Model;
    TSharedPtr<FCkUiTreeCollection> _Quests;
    TSharedPtr<FCkUiTreeCollection> _Objectives;
    TSharedPtr<FCkUiView> _View;
    TSharedPtr<const FCkUiWidgetRegistrySnapshot> _Registry;
    TSharedPtr<FCkJournalInput> _Input;
    TMap<FString, TPair<FName, FName>> _ObjectiveKeys;
    TWeakPtr<SWidget> _PreviousFocus;
    FTSTicker::FDelegateHandle _TickHandle;
    int32 _UserIndex = INDEX_NONE;
    bool _Alive = false;
    bool _Open = false;
    bool _OwnsPause = false;
    bool _HadCursor = false;
    bool _NeedsFocus = false;
    bool _Syncing = false;
    bool _Stress = false;
    float _PollRemaining = 0.0f;
    float _OpenElapsed = 0.0f;
    FString _LastMarkup;
    FString _LastStyle;
    FString _SourceError;
    FString _Status = TEXT("Press J to open the journal.");
};
