#pragma once

#include "CkTests/UI/CkInventoryMenuProof.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkSlateLayout/CkUiCollection.h"

#include "UObject/StrongObjectPtr.h"

class UTexture2D;

/** In-memory proof inventory. All mutations validate and publish a complete projection. */
class FCkInventoryMenuProofModel final : public TSharedFromThis<FCkInventoryMenuProofModel>
{
public:
    FCkInventoryMenuProofModel() = default;

    /** Validates a persisted snapshot without loading resources or mutating a model. */
    static auto TryValidateState(const FCk_UiAuthoringProof_InventoryState& InState,
        FString& OutError) -> bool;

    static auto TryCreate(const FCk_UiAuthoringProof_InventoryState& InState, const FString& InAtlasPath,
        TSharedPtr<FCkInventoryMenuProofModel>& OutModel, FString& OutError) -> bool;

    auto PopulateBindings(FCkUiView::FDataBindings& InOutData, FCkUiView::FActions& InOutActions,
        FSimpleDelegate InClose) -> void;

    auto Get_State() const -> FCk_UiAuthoringProof_InventoryState { return _State; }
    auto Get_Items() const -> TSharedPtr<FCkUiCollection> { return _Items; }
    auto Get_VisibleKeys() const -> TArray<FString> { return _VisibleKeys; }
    auto Get_SelectedKey() const -> FString { return _SelectedKey; }
    auto Get_Category() const -> FString { return _Category; }
    auto Get_SearchText() const -> FText { return _SearchText; }
    auto Get_ActionCount(FName InAction) const -> int32 { return _ActionCounts.FindRef(InAction); }
    auto Get_Summary() const -> FString;

    auto Request_Select(const FString& InKey) -> bool;
    auto Request_Filter(const FString& InCategory) -> bool;
    auto Request_Search(FText InSearch) -> bool;
    auto Request_PrimaryAction() -> bool;

private:
    auto CanDispatch() const -> bool;
    auto TryProject(const FCk_UiAuthoringProof_InventoryState& InState, const FString& InCategory,
        const FText& InSearch, const FString& InPreferredSelection, TArray<FCkUiRecordData>& OutRecords,
        TArray<FString>& OutKeys, FString& OutSelection) const -> bool;
    auto TryCommit(const FCk_UiAuthoringProof_InventoryState& InState, const FString& InCategory,
        const FText& InSearch, const FString& InPreferredSelection) -> bool;
    auto SelectedIndex() const -> int32;
    auto SelectedBrush() const -> const FSlateBrush*;
    auto SelectedName() const -> FText;
    auto SelectedCategory() const -> FText;
    auto SelectedDescription() const -> FText;
    auto SelectedStats() const -> FText;
    auto SelectedOwned() const -> FText;
    auto PrimaryLabel() const -> FText;
    auto HasPrimaryAction() const -> bool;

    FCk_UiAuthoringProof_InventoryState _State;
    TSharedPtr<TStrongObjectPtr<UTexture2D>> _AtlasRoot;
    TArray<TSharedPtr<FSlateBrush>> _Brushes;
    TSharedPtr<FCkUiCollection> _Items;
    TArray<FString> _VisibleKeys;
    FString _SelectedKey;
    FString _Category = TEXT("All");
    FText _SearchText;
    TMap<FName, int32> _ActionCounts;
    TAttribute<bool> _DispatchGate;
};
