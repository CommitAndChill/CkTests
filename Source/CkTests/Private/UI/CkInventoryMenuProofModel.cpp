#include "CkInventoryMenuProofModel.h"
#include "CkGameplayInventoryMenuAdapter.h"

#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Styling/SlateBrush.h"

namespace ck_inventory_menu_proof
{
    /** Records/widgets may keep a brush after the model is released. */
    struct FInventoryBrush final : FSlateBrush
    {
        TSharedPtr<TStrongObjectPtr<UTexture2D>> TextureRoot;
    };

    enum class ECategory : uint8 { Equipment, Supplies, Materials };
    enum class EPrimaryStat : uint8 { None, Damage, Defense };
    enum class EResource : uint8 { None, Health, Focus, Stamina };
    enum class ERarity : uint8 { None, Common, Rare };
    constexpr float RowEdges[] = {0.0f, 394.0f / 1086.0f, 741.0f / 1086.0f, 1.0f};

    struct FDefinition
    {
        const TCHAR* Key;
        const TCHAR* Name;
        ECategory Category;
        int32 InitialQuantity;
        const TCHAR* Meta;
        const TCHAR* Description;
        EPrimaryStat PrimaryStat;
        int32 PrimaryValue;
        float Weight;
        int32 RestoreAmount;
        EResource Resource;
        ERarity Rarity;
    };

    constexpr FDefinition Definitions[] = {
        {TEXT("iron-longsword"), TEXT("Iron Longsword"), ECategory::Equipment, 1, TEXT("Melee weapon"),
            TEXT("A dependable blade with a balanced grip."), EPrimaryStat::Damage, 28, 3.5f, 0, EResource::None, ERarity::None},
        {TEXT("oak-bow"), TEXT("Oak Bow"), ECategory::Equipment, 1, TEXT("Ranged weapon"),
            TEXT("A sturdy hunting bow carved from seasoned oak."), EPrimaryStat::Damage, 19, 2.0f, 0, EResource::None, ERarity::None},
        {TEXT("steel-shield"), TEXT("Steel Shield"), ECategory::Equipment, 1, TEXT("Shield"),
            TEXT("A reinforced shield for the road ahead."), EPrimaryStat::Defense, 24, 5.0f, 0, EResource::None, ERarity::None},
        {TEXT("trail-hood"), TEXT("Trail Hood"), ECategory::Equipment, 1, TEXT("Head armor"),
            TEXT("A light hood that keeps the weather off."), EPrimaryStat::Defense, 7, 0.8f, 0, EResource::None, ERarity::None},
        {TEXT("leather-boots"), TEXT("Leather Boots"), ECategory::Equipment, 1, TEXT("Foot armor"),
            TEXT("Soft leather boots made for long journeys."), EPrimaryStat::Defense, 9, 1.2f, 0, EResource::None, ERarity::None},
        {TEXT("scout-vest"), TEXT("Scout Vest"), ECategory::Equipment, 1, TEXT("Body armor"),
            TEXT("Flexible protection for a quick moving scout."), EPrimaryStat::Defense, 16, 2.5f, 0, EResource::None, ERarity::None},
        {TEXT("health-tonic"), TEXT("Health Tonic"), ECategory::Supplies, 3, TEXT("Restorative"),
            TEXT("A bright tonic that restores vitality."), EPrimaryStat::None, 0, 0.0f, 40, EResource::Health, ERarity::None},
        {TEXT("focus-draught"), TEXT("Focus Draught"), ECategory::Supplies, 2, TEXT("Restorative"),
            TEXT("A calming draught that clears the mind."), EPrimaryStat::None, 0, 0.0f, 30, EResource::Focus, ERarity::None},
        {TEXT("travel-rations"), TEXT("Travel Rations"), ECategory::Supplies, 5, TEXT("Food"),
            TEXT("Packed provisions for a day on the trail."), EPrimaryStat::None, 0, 0.0f, 15, EResource::Stamina, ERarity::None},
        {TEXT("bandages"), TEXT("Bandages"), ECategory::Supplies, 4, TEXT("Medical"),
            TEXT("Clean wraps for tending minor wounds."), EPrimaryStat::None, 0, 0.0f, 12, EResource::Health, ERarity::None},
        {TEXT("iron-ore"), TEXT("Iron Ore"), ECategory::Materials, 8, TEXT("Crafting material"),
            TEXT("Raw ore ready for the forge."), EPrimaryStat::None, 0, 0.0f, 0, EResource::None, ERarity::Common},
        {TEXT("moonstone"), TEXT("Moonstone"), ECategory::Materials, 2, TEXT("Crafting material"),
            TEXT("A pale mineral with a faint inner glow."), EPrimaryStat::None, 0, 0.0f, 0, EResource::None, ERarity::Rare},
    };

    struct FStatRows
    {
        FString LabelOne;
        FString ValueOne;
        FString LabelTwo;
        FString ValueTwo;
    };

    auto StatRows(const FDefinition& InDefinition) -> FStatRows
    {
        if (InDefinition.Category == ECategory::Equipment)
        {
            return {InDefinition.PrimaryStat == EPrimaryStat::Damage ? TEXT("Damage") : TEXT("Defense"),
                FString::FromInt(InDefinition.PrimaryValue), TEXT("Weight"),
                FString::Printf(TEXT("%.1f"), InDefinition.Weight)};
        }
        if (InDefinition.Category == ECategory::Supplies)
        {
            const auto* Resource = InDefinition.Resource == EResource::Health ? TEXT("health")
                : InDefinition.Resource == EResource::Focus ? TEXT("focus") : TEXT("stamina");
            return {TEXT("Restores"), FString::FromInt(InDefinition.RestoreAmount), TEXT("Resource"), Resource};
        }
        return {TEXT("Type"), TEXT("Material"), TEXT("Rarity"),
            InDefinition.Rarity == ERarity::Common ? TEXT("Common") : TEXT("Rare")};
    }

    auto FindIndex(const FString& InKey) -> int32
    {
        for (auto Index = 0; Index < UE_ARRAY_COUNT(Definitions); ++Index)
        { if (InKey == Definitions[Index].Key) { return Index; } }
        return INDEX_NONE;
    }

    auto CategoryName(ECategory InCategory) -> FString
    {
        switch (InCategory)
        {
        case ECategory::Equipment: return TEXT("Equipment");
        case ECategory::Supplies: return TEXT("Supplies");
        case ECategory::Materials: return TEXT("Materials");
        }
        return {};
    }

    auto IsCategory(const FString& InCategory) -> bool
    { return InCategory == TEXT("All") || InCategory == TEXT("Equipment")
        || InCategory == TEXT("Supplies") || InCategory == TEXT("Materials"); }

    auto ValidateState(const FCk_UiAuthoringProof_InventoryState& InState, FString& OutError) -> bool
    {
        const auto& Quantities = InState.Get_ItemQuantities();
        if (Quantities.IsEmpty())
        {
            if (InState.Get_EquippedKey().IsEmpty()) { return true; }
            OutError = TEXT("An equipped item requires a complete inventory snapshot.");
            return false;
        }
        if (Quantities.Num() != UE_ARRAY_COUNT(Definitions))
        {
            OutError = TEXT("Inventory snapshot must contain exactly the twelve fixture keys.");
            return false;
        }
        for (const auto& Definition : Definitions)
        {
            const auto* Quantity = Quantities.Find(Definition.Key);
            if (Quantity == nullptr || *Quantity < 0 || *Quantity > Definition.InitialQuantity)
            {
                OutError = FString::Printf(TEXT("Invalid quantity for %s."), Definition.Key);
                return false;
            }
        }
        const auto& Equipped = InState.Get_EquippedKey();
        if (Equipped.IsEmpty()) { return true; }
        const auto Index = FindIndex(Equipped);
        if (Index == INDEX_NONE || Definitions[Index].Category != ECategory::Equipment
            || Quantities.FindRef(Equipped) == 0)
        {
            OutError = TEXT("Equipped key must name owned equipment.");
            return false;
        }
        return true;
    }

    auto Text(FString InValue) -> FCkUiFieldValue
    { return FCkUiFieldValue{.Kind = ECkUiFieldKind::Text, .Text = FText::FromString(MoveTemp(InValue))}; }

    auto Bool(bool InValue) -> FCkUiFieldValue
    { return FCkUiFieldValue{.Kind = ECkUiFieldKind::Bool, .Bool = InValue}; }

    auto Number(float InValue) -> FCkUiFieldValue
    { return FCkUiFieldValue{.Kind = ECkUiFieldKind::Number, .Number = InValue}; }

    auto Schema() -> TArray<FCkUiFieldSchema>
    {
        return {
            {TEXT("image"), ECkUiFieldKind::Image, true},
            {TEXT("title"), ECkUiFieldKind::Text, true},
            {TEXT("meta"), ECkUiFieldKind::Text, true},
            {TEXT("selected"), ECkUiFieldKind::Bool, true},
            {TEXT("equipped"), ECkUiFieldKind::Bool, true},
            {TEXT("index"), ECkUiFieldKind::Number, true},
            {TEXT("cropLeft"), ECkUiFieldKind::Number, true},
            {TEXT("cropTop"), ECkUiFieldKind::Number, true},
            {TEXT("cropRight"), ECkUiFieldKind::Number, true},
            {TEXT("cropBottom"), ECkUiFieldKind::Number, true},
        };
    }

    auto GameplayCategoryText(const FString& InCategory) -> FText
    {
        if (InCategory == TEXT("Equipment"))
        { return NSLOCTEXT("CkGameplayInventoryMenu", "EquipmentCategory", "Equipment"); }
        if (InCategory == TEXT("Supplies"))
        { return NSLOCTEXT("CkGameplayInventoryMenu", "SuppliesCategory", "Supplies"); }
        if (InCategory == TEXT("Materials"))
        { return NSLOCTEXT("CkGameplayInventoryMenu", "MaterialsCategory", "Materials"); }
        return NSLOCTEXT("CkGameplayInventoryMenu", "GenericCategory", "Item");
    }
}

auto FCkInventoryMenuProofModel::TryValidateState(const FCk_UiAuthoringProof_InventoryState& InState,
    FString& OutError) -> bool
{
    OutError.Reset();
    return ck_inventory_menu_proof::ValidateState(InState, OutError);
}

auto FCkInventoryMenuProofModel::TryCreate(const FCk_UiAuthoringProof_InventoryState& InState,
    const FString& InAtlasPath, TSharedPtr<FCkInventoryMenuProofModel>& OutModel, FString& OutError) -> bool
{
    OutError.Reset();
    if (NOT TryValidateState(InState, OutError)) { return false; }
    auto* Texture = FImageUtils::ImportFileAsTexture2D(InAtlasPath);
    if (NOT IsValid(Texture) || Texture->GetSizeX() != 1448 || Texture->GetSizeY() != 1086)
    {
        OutError = TEXT("Inventory icon atlas must be the shipped 1448 x 1086 image.");
        return false;
    }
    auto Candidate = MakeShared<FCkInventoryMenuProofModel>();
    Candidate->_AtlasRoot = MakeShared<TStrongObjectPtr<UTexture2D>>(Texture);
    auto AtlasBrush = MakeShared<ck_inventory_menu_proof::FInventoryBrush>();
    AtlasBrush->TextureRoot = Candidate->_AtlasRoot;
    AtlasBrush->DrawAs = ESlateBrushDrawType::Image;
    AtlasBrush->ImageSize = FVector2D{1448.0f, 1086.0f};
    AtlasBrush->SetResourceObject(Texture);
    Candidate->_AtlasBrush = MoveTemp(AtlasBrush);
    for (auto Index = 0; Index < UE_ARRAY_COUNT(ck_inventory_menu_proof::Definitions); ++Index)
    {
        auto Brush = MakeShared<ck_inventory_menu_proof::FInventoryBrush>();
        Brush->TextureRoot = Candidate->_AtlasRoot;
        Brush->DrawAs = ESlateBrushDrawType::Image;
        const auto Row = Index / 4;
        Brush->ImageSize = FVector2D{362.0f, (ck_inventory_menu_proof::RowEdges[Row + 1]
            - ck_inventory_menu_proof::RowEdges[Row]) * 1086.0f};
        Brush->SetResourceObject(Texture);
        const auto Column = Index % 4;
        Brush->SetUVRegion(FBox2f{FVector2f{Column / 4.0f, ck_inventory_menu_proof::RowEdges[Row]},
            FVector2f{(Column + 1) / 4.0f, ck_inventory_menu_proof::RowEdges[Row + 1]}});
        Candidate->_Brushes.Add(MoveTemp(Brush));
    }
    auto Schema = ck_inventory_menu_proof::Schema();
    if (NOT FCkUiCollection::TryCreate(MoveTemp(Schema), Candidate->_Items).Succeeded)
    {
        OutError = TEXT("Could not create the inventory collection.");
        return false;
    }
    auto Initial = InState;
    if (Initial.Get_ItemQuantities().IsEmpty())
    {
        auto Quantities = TMap<FString, int32>{};
        for (const auto& Definition : ck_inventory_menu_proof::Definitions)
        { Quantities.Add(Definition.Key, Definition.InitialQuantity); }
        Initial.Set_ItemQuantities(MoveTemp(Quantities));
    }
    if (NOT Candidate->TryCommit(Initial, TEXT("All"), FText::GetEmpty(), TEXT("iron-longsword")))
    {
        OutError = TEXT("Could not publish the initial inventory.");
        return false;
    }
    OutModel = MoveTemp(Candidate);
    return true;
}

auto FCkInventoryMenuProofModel::TryCreateForInventory(
    const FCk_Handle_Inventory_DataOnly& InInventory, const FString& InAtlasPath,
    TSharedPtr<FCkInventoryMenuProofModel>& OutModel, FString& OutError) -> bool
{
    (void)InAtlasPath; // The fixture atlas contains item-specific paintings, not gameplay item icons.
    OutError.Reset();
    auto Candidate = MakeShared<FCkInventoryMenuProofModel>();
    Candidate->_GameplayAdapter.Reset(NewObject<UCk_GameplayInventoryMenuAdapter_UE>());
    if (NOT Candidate->_GameplayAdapter.IsValid()
        || NOT Candidate->_GameplayAdapter->TryInitialize(InInventory, OutError))
    {
        if (OutError.IsEmpty()) { OutError = TEXT("Could not create the gameplay inventory adapter."); }
        return false;
    }
    if (NOT FCkUiCollection::TryCreate(ck_inventory_menu_proof::Schema(), Candidate->_Items).Succeeded)
    { OutError = TEXT("Could not create the gameplay inventory collection."); return false; }
    if (NOT Candidate->TryCommitGameplay(TEXT("All"), FText::GetEmpty(), FString{}))
    { OutError = TEXT("Could not project the gameplay inventory."); return false; }
    OutModel = MoveTemp(Candidate);
    return true;
}

auto FCkInventoryMenuProofModel::Get_IsGameplaySourceValid() const -> bool
{ return _GameplayAdapter.IsValid() && NOT _GameplayRetired && _GameplayAdapter->Get_IsSourceValid(); }

auto FCkInventoryMenuProofModel::Get_IsGameplayRequestPending() const -> bool
{ return _GameplayAdapter.IsValid() && _GameplayAdapter->Get_IsRequestPending(); }

auto FCkInventoryMenuProofModel::RefreshGameplay() -> bool
{
    if (NOT _GameplayAdapter.IsValid() || _GameplayRetired) { return false; }
    if (NOT _GameplayAdapter->Get_IsSourceValid())
    { _GameplayRetired = true; return false; }
    return TryCommitGameplay(_Category, _SearchText, _SelectedKey);
}

auto FCkInventoryMenuProofModel::CanDispatch() const -> bool
{
    if (_GameplayAdapter.IsValid() && NOT Get_IsGameplaySourceValid()) { return false; }
    return NOT _DispatchGate.IsSet() || _DispatchGate.Get(true);
}

auto FCkInventoryMenuProofModel::TryCommitGameplay(const FString& InCategory,
    const FText& InSearch, const FString& InPreferredSelection) -> bool
{
    if (NOT _GameplayAdapter.IsValid() || _GameplayRetired || NOT _Items.IsValid()
        || NOT ck_inventory_menu_proof::IsCategory(InCategory))
    { return false; }
    auto Rows = TArray<FCkGameplayInventoryMenuRow>{};
    auto Error = FString{};
    if (NOT _GameplayAdapter->TryRead(Rows, Error))
    {
        if (NOT _GameplayAdapter->Get_IsSourceValid()) { _GameplayRetired = true; }
        return false;
    }
    const auto Needle = InSearch.ToString().TrimStartAndEnd();
    auto Records = TArray<FCkUiRecordData>{};
    auto Keys = TArray<FString>{};
    Records.Reserve(Rows.Num());
    for (const auto& Row : Rows)
    {
        if (InCategory != TEXT("All") && InCategory != Row.Category) { continue; }
        if (NOT Needle.IsEmpty() && NOT Row.Name.ToString().Contains(Needle, ESearchCase::IgnoreCase))
        { continue; }
        auto Record = FCkUiRecordData{};
        Record.Key = Row.Key;
        Record.Fields.Add(TEXT("image"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Image, .Image = Row.Icon});
        Record.Fields.Add(TEXT("title"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text, .Text = Row.Name});
        Record.Fields.Add(TEXT("meta"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text,
            .Text = FText::Format(NSLOCTEXT("CkGameplayInventoryMenu", "QuantityMeta", "x{0}"),
                FText::AsNumber(Row.Quantity))});
        Record.Fields.Add(TEXT("selected"), ck_inventory_menu_proof::Bool(false));
        Record.Fields.Add(TEXT("equipped"), ck_inventory_menu_proof::Bool(false));
        Record.Fields.Add(TEXT("index"), ck_inventory_menu_proof::Number(0.0f));
        Record.Fields.Add(TEXT("cropLeft"), ck_inventory_menu_proof::Number(0.0f));
        Record.Fields.Add(TEXT("cropTop"), ck_inventory_menu_proof::Number(0.0f));
        Record.Fields.Add(TEXT("cropRight"), ck_inventory_menu_proof::Number(1.0f));
        Record.Fields.Add(TEXT("cropBottom"), ck_inventory_menu_proof::Number(1.0f));
        Records.Add(MoveTemp(Record));
        Keys.Add(Row.Key);
    }
    const auto Selection = Keys.Contains(InPreferredSelection) ? InPreferredSelection
        : (Keys.IsEmpty() ? FString{} : Keys[0]);
    bool SameRows = Rows.Num() == _GameplayRows.Num();
    for (int32 Index = 0; SameRows && Index < Rows.Num(); ++Index)
    {
        const auto& A = Rows[Index];
        const auto& B = _GameplayRows[Index];
        SameRows = A.Key == B.Key && A.Quantity == B.Quantity && A.Category == B.Category
            && A.Name.EqualTo(B.Name) && A.Description.EqualTo(B.Description)
            && A.Icon.Get() == B.Icon.Get();
    }
    if (SameRows && _Category == InCategory && _SearchText.EqualTo(InSearch)
        && _SelectedKey == Selection && _VisibleKeys == Keys)
    { return true; }
    for (auto& Record : Records)
    { Record.Fields.FindChecked(TEXT("selected")).Bool = Record.Key == Selection; }
    if (NOT _Items->TrySetRecords(MoveTemp(Records)).Succeeded) { return false; }
    _GameplayRows = MoveTemp(Rows);
    _Category = InCategory;
    _SearchText = InSearch;
    _VisibleKeys = MoveTemp(Keys);
    _SelectedKey = Selection;
    return true;
}

auto FCkInventoryMenuProofModel::GameplaySelectedIndex() const -> int32
{
    if (NOT _VisibleKeys.Contains(_SelectedKey)) { return INDEX_NONE; }
    return _GameplayRows.IndexOfByPredicate([this](const auto& InRow)
    { return InRow.Key == _SelectedKey; });
}

auto FCkInventoryMenuProofModel::TryProject(const FCk_UiAuthoringProof_InventoryState& InState,
    const FString& InCategory, const FText& InSearch, const FString& InPreferredSelection,
    TArray<FCkUiRecordData>& OutRecords, TArray<FString>& OutKeys, FString& OutSelection) const -> bool
{
    auto Error = FString{};
    if (NOT ck_inventory_menu_proof::ValidateState(InState, Error) || NOT ck_inventory_menu_proof::IsCategory(InCategory)
        || NOT _Items.IsValid() || _Brushes.Num() != UE_ARRAY_COUNT(ck_inventory_menu_proof::Definitions))
    { return false; }
    const auto Needle = InSearch.ToString().TrimStartAndEnd();
    for (auto Index = 0; Index < UE_ARRAY_COUNT(ck_inventory_menu_proof::Definitions); ++Index)
    {
        const auto& Definition = ck_inventory_menu_proof::Definitions[Index];
        const auto Key = FString{Definition.Key};
        const auto Quantity = InState.Get_ItemQuantities().FindRef(Key);
        if (Quantity <= 0 || (InCategory != TEXT("All")
            && InCategory != ck_inventory_menu_proof::CategoryName(Definition.Category))
            || (NOT Needle.IsEmpty() && NOT FString{Definition.Name}.Contains(Needle, ESearchCase::IgnoreCase)))
        { continue; }
        auto Record = FCkUiRecordData{};
        Record.Key = Key;
        Record.Fields.Add(TEXT("image"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Image, .Image = _Brushes[Index]});
        Record.Fields.Add(TEXT("title"), ck_inventory_menu_proof::Text(Definition.Name));
        Record.Fields.Add(TEXT("meta"), ck_inventory_menu_proof::Text(FString::Printf(TEXT("x%d  %s"), Quantity, Definition.Meta)));
        Record.Fields.Add(TEXT("selected"), ck_inventory_menu_proof::Bool(Key == InPreferredSelection));
        Record.Fields.Add(TEXT("equipped"), ck_inventory_menu_proof::Bool(Key == InState.Get_EquippedKey()));
        const auto Column = Index % 4;
        const auto Row = Index / 4;
        Record.Fields.Add(TEXT("index"), ck_inventory_menu_proof::Number(0.0f));
        Record.Fields.Add(TEXT("cropLeft"), ck_inventory_menu_proof::Number(Column / 4.0f));
        Record.Fields.Add(TEXT("cropTop"), ck_inventory_menu_proof::Number(ck_inventory_menu_proof::RowEdges[Row]));
        Record.Fields.Add(TEXT("cropRight"), ck_inventory_menu_proof::Number((Column + 1) / 4.0f));
        Record.Fields.Add(TEXT("cropBottom"), ck_inventory_menu_proof::Number(ck_inventory_menu_proof::RowEdges[Row + 1]));
        OutRecords.Add(MoveTemp(Record));
        OutKeys.Add(Key);
    }
    OutSelection = OutKeys.Contains(InPreferredSelection) ? InPreferredSelection
        : (OutKeys.IsEmpty() ? FString{} : OutKeys[0]);
    for (auto& Record : OutRecords)
    { Record.Fields.FindChecked(TEXT("selected")).Bool = Record.Key == OutSelection; }
    return true;
}

auto FCkInventoryMenuProofModel::TryCommit(const FCk_UiAuthoringProof_InventoryState& InState,
    const FString& InCategory, const FText& InSearch, const FString& InPreferredSelection) -> bool
{
    auto Records = TArray<FCkUiRecordData>{};
    auto Keys = TArray<FString>{};
    auto Selection = FString{};
    if (NOT TryProject(InState, InCategory, InSearch, InPreferredSelection, Records, Keys, Selection))
    { return false; }
    const auto OldState = _State;
    const auto OldCategory = _Category;
    const auto OldSearch = _SearchText;
    const auto OldKeys = _VisibleKeys;
    const auto OldSelection = _SelectedKey;
    _State = InState;
    _Category = InCategory;
    _SearchText = InSearch;
    _VisibleKeys = MoveTemp(Keys);
    _SelectedKey = MoveTemp(Selection);
    if (NOT _Items->TrySetRecords(MoveTemp(Records)).Succeeded)
    {
        _State = OldState;
        _Category = OldCategory;
        _SearchText = OldSearch;
        _VisibleKeys = OldKeys;
        _SelectedKey = OldSelection;
        return false;
    }
    return true;
}

auto FCkInventoryMenuProofModel::Request_Select(const FString& InKey) -> bool
{
    if (NOT CanDispatch() || InKey == _SelectedKey || NOT _VisibleKeys.Contains(InKey)) { return false; }
    if (_GameplayAdapter.IsValid())
    {
        if (NOT TryCommitGameplay(_Category, _SearchText, InKey)) { return false; }
        ++_ActionCounts.FindOrAdd(TEXT("SelectInventoryItem"));
        return true;
    }
    if (NOT TryCommit(_State, _Category, _SearchText, InKey)) { return false; }
    ++_ActionCounts.FindOrAdd(TEXT("SelectInventoryItem"));
    return true;
}

auto FCkInventoryMenuProofModel::Request_Filter(const FString& InCategory) -> bool
{
    if (NOT CanDispatch() || NOT ck_inventory_menu_proof::IsCategory(InCategory) || InCategory == _Category)
    { return false; }
    if (_GameplayAdapter.IsValid())
    {
        if (NOT TryCommitGameplay(InCategory, _SearchText, _SelectedKey)) { return false; }
        ++_ActionCounts.FindOrAdd(FName{*FString::Printf(TEXT("InventoryFilter%s"), *InCategory)});
        return true;
    }
    if (NOT TryCommit(_State, InCategory, _SearchText, _SelectedKey)) { return false; }
    ++_ActionCounts.FindOrAdd(FName{*FString::Printf(TEXT("InventoryFilter%s"), *InCategory)});
    return true;
}

auto FCkInventoryMenuProofModel::Request_Search(FText InSearch) -> bool
{
    if (NOT CanDispatch() || InSearch.EqualTo(_SearchText)) { return false; }
    if (_GameplayAdapter.IsValid())
    {
        if (NOT TryCommitGameplay(_Category, InSearch, _SelectedKey)) { return false; }
        ++_ActionCounts.FindOrAdd(TEXT("InventorySearch"));
        return true;
    }
    if (NOT TryCommit(_State, _Category, InSearch, _SelectedKey)) { return false; }
    ++_ActionCounts.FindOrAdd(TEXT("InventorySearch"));
    return true;
}

auto FCkInventoryMenuProofModel::Request_PrimaryAction() -> bool
{
    if (NOT CanDispatch() || NOT HasPrimaryAction()) { return false; }
    if (_GameplayAdapter.IsValid())
    {
        auto Error = FString{};
        if (NOT _GameplayAdapter->TryDiscard(_SelectedKey, Error)) { return false; }
        ++_ActionCounts.FindOrAdd(TEXT("InventoryPrimaryAction"));
        return true;
    }
    const auto Index = SelectedIndex();
    if (Index == INDEX_NONE) { return false; }
    auto Candidate = _State;
    const auto Key = _SelectedKey;
    const auto& Definition = ck_inventory_menu_proof::Definitions[Index];
    if (Definition.Category == ck_inventory_menu_proof::ECategory::Equipment)
    { Candidate.Set_EquippedKey(_State.Get_EquippedKey() == Key ? FString{} : Key); }
    else if (Definition.Category == ck_inventory_menu_proof::ECategory::Supplies)
    {
        auto Quantities = Candidate.Get_ItemQuantities();
        auto* Quantity = Quantities.Find(Key);
        if (Quantity == nullptr || *Quantity <= 0) { return false; }
        --*Quantity;
        Candidate.Set_ItemQuantities(MoveTemp(Quantities));
    }
    else { return false; }
    if (NOT TryCommit(Candidate, _Category, _SearchText, Key)) { return false; }
    ++_ActionCounts.FindOrAdd(TEXT("InventoryPrimaryAction"));
    return true;
}

auto FCkInventoryMenuProofModel::SelectedIndex() const -> int32
{
    if (_SelectedKey.IsEmpty() || NOT _VisibleKeys.Contains(_SelectedKey)
        || _State.Get_ItemQuantities().FindRef(_SelectedKey) <= 0)
    { return INDEX_NONE; }
    return ck_inventory_menu_proof::FindIndex(_SelectedKey);
}

auto FCkInventoryMenuProofModel::SelectedBrush() const -> const FSlateBrush*
{ if (_GameplayAdapter.IsValid()) { const auto Index = GameplaySelectedIndex();
    return Index == INDEX_NONE ? nullptr : _GameplayRows[Index].Icon.Get(); }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? nullptr : _Brushes[Index].Get(); }

auto FCkInventoryMenuProofModel::SelectedName() const -> FText
{ if (_GameplayAdapter.IsValid()) { const auto Index = GameplaySelectedIndex();
    return Index == INDEX_NONE ? FText::GetEmpty() : _GameplayRows[Index].Name; }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? FText::GetEmpty()
    : FText::FromString(ck_inventory_menu_proof::Definitions[Index].Name); }

auto FCkInventoryMenuProofModel::SelectedCategory() const -> FText
{ if (_GameplayAdapter.IsValid()) { const auto Index = GameplaySelectedIndex();
    return Index == INDEX_NONE ? FText::GetEmpty()
        : ck_inventory_menu_proof::GameplayCategoryText(_GameplayRows[Index].Category); }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? FText::GetEmpty()
    : FText::FromString(ck_inventory_menu_proof::CategoryName(ck_inventory_menu_proof::Definitions[Index].Category)); }

auto FCkInventoryMenuProofModel::SelectedDescription() const -> FText
{ if (_GameplayAdapter.IsValid()) { const auto Index = GameplaySelectedIndex();
    return Index == INDEX_NONE ? FText::GetEmpty() : _GameplayRows[Index].Description; }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? FText::GetEmpty()
    : FText::FromString(ck_inventory_menu_proof::Definitions[Index].Description); }

auto FCkInventoryMenuProofModel::SelectedStats() const -> FText
{
    if (_GameplayAdapter.IsValid()) { return FText::GetEmpty(); }
    const auto Index = SelectedIndex();
    if (Index == INDEX_NONE) { return FText::GetEmpty(); }
    const auto& Definition = ck_inventory_menu_proof::Definitions[Index];
    const auto Rows = ck_inventory_menu_proof::StatRows(Definition);
    if (Definition.Category == ck_inventory_menu_proof::ECategory::Supplies)
    { return FText::FromString(FString::Printf(TEXT("%s %s %s"), *Rows.LabelOne, *Rows.ValueOne, *Rows.ValueTwo)); }
    if (Definition.Category == ck_inventory_menu_proof::ECategory::Materials)
    { return FText::FromString(FString::Printf(TEXT("%s  /  %s"), *Rows.ValueOne, *Rows.ValueTwo)); }
    return FText::FromString(FString::Printf(TEXT("%s %s  /  %s %s"),
        *Rows.LabelOne, *Rows.ValueOne, *Rows.LabelTwo, *Rows.ValueTwo));
}

auto FCkInventoryMenuProofModel::SelectedStatLabelOne() const -> FText
{ if (_GameplayAdapter.IsValid()) { return GameplaySelectedIndex() == INDEX_NONE ? FText::GetEmpty()
    : NSLOCTEXT("CkGameplayInventoryMenu", "QuantityLabel", "Quantity"); }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? FText::GetEmpty()
    : FText::FromString(ck_inventory_menu_proof::StatRows(ck_inventory_menu_proof::Definitions[Index]).LabelOne); }

auto FCkInventoryMenuProofModel::SelectedStatValueOne() const -> FText
{ if (_GameplayAdapter.IsValid()) { const auto Index = GameplaySelectedIndex();
    return Index == INDEX_NONE ? FText::GetEmpty() : FText::AsNumber(_GameplayRows[Index].Quantity); }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? FText::GetEmpty()
    : FText::FromString(ck_inventory_menu_proof::StatRows(ck_inventory_menu_proof::Definitions[Index]).ValueOne); }

auto FCkInventoryMenuProofModel::SelectedStatLabelTwo() const -> FText
{ if (_GameplayAdapter.IsValid()) { return FText::GetEmpty(); }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? FText::GetEmpty()
    : FText::FromString(ck_inventory_menu_proof::StatRows(ck_inventory_menu_proof::Definitions[Index]).LabelTwo); }

auto FCkInventoryMenuProofModel::SelectedStatValueTwo() const -> FText
{ if (_GameplayAdapter.IsValid()) { return FText::GetEmpty(); }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? FText::GetEmpty()
    : FText::FromString(ck_inventory_menu_proof::StatRows(ck_inventory_menu_proof::Definitions[Index]).ValueTwo); }

auto FCkInventoryMenuProofModel::SelectedCropLeft() const -> float
{ if (_GameplayAdapter.IsValid()) { return 0.0f; }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? 0.0f : (Index % 4) / 4.0f; }

auto FCkInventoryMenuProofModel::SelectedCropTop() const -> float
{ if (_GameplayAdapter.IsValid()) { return 0.0f; }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? 0.0f : ck_inventory_menu_proof::RowEdges[Index / 4]; }

auto FCkInventoryMenuProofModel::SelectedCropRight() const -> float
{ if (_GameplayAdapter.IsValid()) { return 1.0f; }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? 1.0f : ((Index % 4) + 1) / 4.0f; }

auto FCkInventoryMenuProofModel::SelectedCropBottom() const -> float
{ if (_GameplayAdapter.IsValid()) { return 1.0f; }
  const auto Index = SelectedIndex(); return Index == INDEX_NONE ? 1.0f : ck_inventory_menu_proof::RowEdges[Index / 4 + 1]; }

auto FCkInventoryMenuProofModel::SelectedOwned() const -> FText
{
    if (_GameplayAdapter.IsValid()) { const auto Index = GameplaySelectedIndex();
        return Index == INDEX_NONE ? FText::GetEmpty()
            : FText::Format(NSLOCTEXT("CkGameplayInventoryMenu", "OwnedCount", "Owned  {0}"),
                FText::AsNumber(_GameplayRows[Index].Quantity)); }
    if (SelectedIndex() == INDEX_NONE) { return FText::GetEmpty(); }
    return FText::FromString(FString::Printf(TEXT("Owned  %d%s"), _State.Get_ItemQuantities().FindRef(_SelectedKey),
        _State.Get_EquippedKey() == _SelectedKey ? TEXT("  /  Equipped") : TEXT("")));
}

auto FCkInventoryMenuProofModel::PrimaryLabel() const -> FText
{
    if (_GameplayAdapter.IsValid())
    { return NSLOCTEXT("CkGameplayInventoryMenu", "DiscardAction", "Discard"); }
    const auto Index = SelectedIndex();
    if (Index == INDEX_NONE) { return FText::FromString(TEXT("Select an item")); }
    switch (ck_inventory_menu_proof::Definitions[Index].Category)
    {
    case ck_inventory_menu_proof::ECategory::Equipment:
        return FText::FromString(_State.Get_EquippedKey() == _SelectedKey ? TEXT("Unequip") : TEXT("Equip"));
    case ck_inventory_menu_proof::ECategory::Supplies: return FText::FromString(TEXT("Use"));
    case ck_inventory_menu_proof::ECategory::Materials: return FText::FromString(TEXT("Inspect"));
    }
    return FText::GetEmpty();
}

auto FCkInventoryMenuProofModel::HasPrimaryAction() const -> bool
{
    if (_GameplayAdapter.IsValid())
    { return Get_IsGameplaySourceValid() && NOT _GameplayAdapter->Get_IsRequestPending()
        && GameplaySelectedIndex() != INDEX_NONE; }
    const auto Index = SelectedIndex();
    return Index != INDEX_NONE && ck_inventory_menu_proof::Definitions[Index].Category
        != ck_inventory_menu_proof::ECategory::Materials;
}

auto FCkInventoryMenuProofModel::Get_Summary() const -> FString
{
    if (_GameplayAdapter.IsValid())
    { return FString::Printf(TEXT("%d entries  /  %d shown  /  %s"),
        _GameplayRows.Num(), _VisibleKeys.Num(), *_Category); }
    auto OwnedTypes = 0;
    for (const auto& Definition : ck_inventory_menu_proof::Definitions)
    { if (_State.Get_ItemQuantities().FindRef(Definition.Key) > 0) { ++OwnedTypes; } }
    return FString::Printf(TEXT("%d items  /  %d shown  /  %s"), OwnedTypes, _VisibleKeys.Num(), *_Category);
}

auto FCkInventoryMenuProofModel::Get_SummaryText() const -> FText
{
    if (NOT _GameplayAdapter.IsValid()) { return FText::FromString(Get_Summary()); }
    return FText::Format(NSLOCTEXT("CkGameplayInventoryMenu", "Summary", "{0} entries  /  {1} shown"),
        FText::AsNumber(_GameplayRows.Num()), FText::AsNumber(_VisibleKeys.Num()));
}

auto FCkInventoryMenuProofModel::PopulateBindings(FCkUiView::FDataBindings& InOutData,
    FCkUiView::FActions& InOutActions, FSimpleDelegate InClose) -> void
{
    _DispatchGate = InOutData.CanDispatchEvents;
    const auto Weak = TWeakPtr<FCkInventoryMenuProofModel>{AsShared()};
    InOutData.Collections.Add(TEXT("InventoryItems"), _Items);
    InOutData.Text.Add(TEXT("InventoryTitle"), _GameplayAdapter.IsValid()
        ? NSLOCTEXT("CkGameplayInventoryMenu", "Title", "INVENTORY")
        : FText::FromString(TEXT("INVENTORY")));
    InOutData.Text.Add(TEXT("InventorySubtitle"), _GameplayAdapter.IsValid()
        ? NSLOCTEXT("CkGameplayInventoryMenu", "LiveSubtitle", "Live carried items")
        : FText::FromString(TEXT("Ready for the next run.")));
    InOutData.Text.Add(TEXT("InventoryCount"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); if (NOT Self.IsValid()) { return FText::GetEmpty(); }
      return Self->_GameplayAdapter.IsValid()
          ? FText::Format(NSLOCTEXT("CkGameplayInventoryMenu", "VisibleCount", "{0} items"),
              FText::AsNumber(Self->_VisibleKeys.Num()))
          : FText::FromString(FString::Printf(TEXT("%d items"), Self->_VisibleKeys.Num())); }));
    InOutData.Text.Add(TEXT("InventoryDetailName"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedName() : FText::GetEmpty(); }));
    InOutData.Text.Add(TEXT("InventoryDetailCategory"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedCategory() : FText::GetEmpty(); }));
    InOutData.Text.Add(TEXT("InventoryDetailDescription"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedDescription() : FText::GetEmpty(); }));
    InOutData.Text.Add(TEXT("InventoryDetailStats"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedStats() : FText::GetEmpty(); }));
    InOutData.Text.Add(TEXT("InventoryStatLabelOne"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedStatLabelOne() : FText::GetEmpty(); }));
    InOutData.Text.Add(TEXT("InventoryStatValueOne"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedStatValueOne() : FText::GetEmpty(); }));
    InOutData.Text.Add(TEXT("InventoryStatLabelTwo"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedStatLabelTwo() : FText::GetEmpty(); }));
    InOutData.Text.Add(TEXT("InventoryStatValueTwo"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedStatValueTwo() : FText::GetEmpty(); }));
    InOutData.Text.Add(TEXT("InventoryDetailOwned"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedOwned() : FText::GetEmpty(); }));
    InOutData.Text.Add(TEXT("InventoryPrimaryLabel"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->PrimaryLabel() : FText::GetEmpty(); }));
    InOutData.Text.Add(TEXT("InventoryFeedback"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->Get_SummaryText() : FText::GetEmpty(); }));
    InOutData.Text.Add(TEXT("InventoryEmpty"), _GameplayAdapter.IsValid()
        ? NSLOCTEXT("CkGameplayInventoryMenu", "EmptyState", "No carried items match this view.")
        : FText::FromString(TEXT("No items match your search.")));
    InOutData.Text.Add(TEXT("InventorySearch"), TAttribute<FText>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->_SearchText : FText::GetEmpty(); }));
    InOutData.TextChanged.Add(TEXT("InventorySearch"), FOnTextChanged::CreateLambda([Weak](const FText& InText)
    { if (const auto Self = Weak.Pin(); Self.IsValid()) { Self->Request_Search(InText); } }));
    InOutData.Images.Add(TEXT("InventoryDetailImage"), TAttribute<const FSlateBrush*>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedBrush() : nullptr; }));
    InOutData.Images.Add(TEXT("InventoryAtlas"), TAttribute<const FSlateBrush*>::CreateLambda([Weak]() -> const FSlateBrush*
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->_AtlasBrush.Get() : nullptr; }));
    InOutData.Number.Add(TEXT("InventoryAtlasIndex"), 0.0f);
    InOutData.Number.Add(TEXT("InventoryCropLeft"), TAttribute<float>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedCropLeft() : 0.0f; }));
    InOutData.Number.Add(TEXT("InventoryCropTop"), TAttribute<float>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedCropTop() : 0.0f; }));
    InOutData.Number.Add(TEXT("InventoryCropRight"), TAttribute<float>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedCropRight() : 1.0f; }));
    InOutData.Number.Add(TEXT("InventoryCropBottom"), TAttribute<float>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->SelectedCropBottom() : 1.0f; }));
    InOutData.Visibility.Add(TEXT("InventoryHasSelection"), TAttribute<bool>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() && (Self->_GameplayAdapter.IsValid()
        ? Self->GameplaySelectedIndex() : Self->SelectedIndex()) != INDEX_NONE; }));
    InOutData.Visibility.Add(TEXT("InventoryIsEmpty"), TAttribute<bool>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() && Self->_VisibleKeys.IsEmpty(); }));
    InOutData.Visibility.Add(TEXT("InventoryPrimaryEnabled"), TAttribute<bool>::CreateLambda([Weak]()
    { const auto Self = Weak.Pin(); return Self.IsValid() && Self->HasPrimaryAction(); }));
    for (const auto* Category : {TEXT("All"), TEXT("Equipment"), TEXT("Supplies"), TEXT("Materials")})
    {
        const auto CategoryString = FString{Category};
        InOutData.Visibility.Add(FString::Printf(TEXT("InventoryFilter%sSelected"), Category),
            TAttribute<bool>::CreateLambda([Weak, CategoryString]()
            { const auto Self = Weak.Pin(); return Self.IsValid() && Self->_Category == CategoryString; }));
        InOutActions.Add(FString::Printf(TEXT("InventoryFilter%s"), Category), FSimpleDelegate::CreateLambda([Weak, CategoryString]()
        { if (const auto Self = Weak.Pin(); Self.IsValid()) { Self->Request_Filter(CategoryString); } }));
    }
    InOutData.ItemActions.Add(TEXT("SelectInventoryItem"), FCkUiOnItemAction::CreateLambda([Weak](FString InKey)
    { if (const auto Self = Weak.Pin(); Self.IsValid()) { Self->Request_Select(InKey); } }));
    InOutActions.Add(TEXT("InventoryPrimaryAction"), FSimpleDelegate::CreateLambda([Weak]()
    { if (const auto Self = Weak.Pin(); Self.IsValid()) { Self->Request_PrimaryAction(); } }));
    InOutActions.Add(TEXT("InventoryClose"), FSimpleDelegate::CreateLambda([Weak, InClose]()
    {
        if (const auto Self = Weak.Pin(); Self.IsValid() && Self->CanDispatch() && InClose.IsBound())
        {
            ++Self->_ActionCounts.FindOrAdd(TEXT("InventoryClose"));
            InClose.Execute();
        }
    }));
}
