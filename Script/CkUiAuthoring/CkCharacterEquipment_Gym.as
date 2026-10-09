// Language=angelscript

struct FCk_CharacterItem
{
    FString Key;
    FString Name;
    FString Kind;
    FString Rarity;
    FString Stats;
    FString Description;
    int32 DamageMin = 0;
    int32 DamageMax = 0;
    int32 Armor = 0;
    int32 Strength = 0;
    int32 Dexterity = 0;
    int32 Life = 0;
    int32 Critical = 0;
    int32 Power = 0;
    int32 Icon = -1;
    int32 Quantity = 1;
    FString Slot;
}

class UCk_CharacterEquipment_Widget : UCk_ActivatableWidget_UE
{
    default bIsFocusable = true;
    default bAutoActivate = false;
    UPROPERTY() private UCk_UI_AuthoredSession_UE _Session;
    UPROPERTY() private UCk_UI_AuthoredSurface_UE _Surface;
    UPROPERTY() private UTexture2D _Backdrop;
    UPROPERTY() private UTexture2D _Portrait;
    UPROPERTY() private UTexture2D _Atlas;
    UPROPERTY() private UTexture2D _Ornament;
    UPROPERTY() private UTexture2D _Alcove;
    UPROPERTY() private UTexture2D _SlotFrame;
    UPROPERTY() private UTexture2D _StatGlyphAtlas;
    UPROPERTY() private UTexture2D _PanelFrame;
    UPROPERTY() private UTexture2D _StatStrip;
    UPROPERTY() private UTexture2D _TitleStrip;
    private TArray<FCk_CharacterItem> _Items;
    private FString _SelectedKey = "veilcutter";
    private FString _Filter = "All";
    private FString _NavigationRegion = "Inventory";
    private FString _Status = "Veilcutter selected for the Weapon slot.";
    private FString _RingTarget = "Ring1";
    private FString _StartupError;
    private FString _SourceError;
    private bool _Compare = true;
    private bool _Open = true;
    private bool _Retired = false;
    private int32 _MutationCount = 0;

    private FString DoDirectory() const
    { return FPaths::CombinePaths(FPaths::ProjectDir(), "Plugins/CkTests/Resources/UiAuthoring/CharacterEquipmentProof"); }
    private FString DoFantasyDirectory() const
    { return FPaths::CombinePaths(FPaths::ProjectDir(), "Plugins/CkTests/Resources/UiAuthoring/DarkFantasyHudProof"); }
    private bool DoReady() const { return !_Retired && ck::IsValid(_Session) && _Session.Get_IsUsable(); }
    private TArray<FString> DoSlots() const
    {
        auto Slots = TArray<FString>();
        Slots.Add("Head"); Slots.Add("Chest"); Slots.Add("Hands"); Slots.Add("Legs"); Slots.Add("Boots");
        Slots.Add("Weapon"); Slots.Add("Amulet"); Slots.Add("Ring1"); Slots.Add("Ring2"); Slots.Add("Offhand");
        return Slots;
    }
    private bool DoValidSlot(const FString& InSlot) const
    { for (const auto EquipmentSlot : DoSlots()) { if (EquipmentSlot == InSlot) { return true; } } return false; }
    private TArray<FString> DoAffixes() const
    { auto Names = TArray<FString>(); Names.Add("Armor"); Names.Add("Strength");
      Names.Add("Dexterity"); Names.Add("Life"); Names.Add("Critical"); return Names; }
    private TArray<FString> DoCardPrefixes() const
    { auto Names = TArray<FString>(); Names.Add("Selected"); Names.Add("Equipped"); return Names; }
    private TArray<FString> DoDeltaStates() const
    { auto Names = TArray<FString>(); Names.Add("Gain"); Names.Add("Loss"); Names.Add("Neutral"); return Names; }
    private int32 DoPlaceholderIcon(const FString& InSlot) const
    {
        if (InSlot == "Head") { return 2; } if (InSlot == "Chest") { return 3; }
        if (InSlot == "Hands") { return 4; } if (InSlot == "Legs") { return 11; }
        if (InSlot == "Boots") { return 5; } if (InSlot == "Weapon") { return 0; }
        if (InSlot == "Amulet") { return 6; } if (InSlot == "Ring1" || InSlot == "Ring2") { return 7; }
        return 1;
    }
    private bool DoAccepts(const FString& InKind, const FString& InSlot) const
    {
        if (InKind == "ring") { return InSlot == "Ring1" || InSlot == "Ring2"; }
        if (InKind == "weapon") { return InSlot == "Weapon"; }
        if (InKind == "offhand") { return InSlot == "Offhand"; }
        return (InKind == "head" && InSlot == "Head") || (InKind == "chest" && InSlot == "Chest")
            || (InKind == "hands" && InSlot == "Hands") || (InKind == "legs" && InSlot == "Legs")
            || (InKind == "boots" && InSlot == "Boots") || (InKind == "amulet" && InSlot == "Amulet");
    }
    private bool DoVisible(const FCk_CharacterItem& InItem, const FString& InFilter) const
    {
        if (!InItem.Slot.IsEmpty()) { return false; }
        return InFilter == "All" || (InFilter == "Supplies" && InItem.Kind == "supply")
            || (InFilter == "Equipment" && InItem.Kind != "supply");
    }
    private int32 DoOwnedCount(const TArray<FCk_CharacterItem>& InItems) const
    { int32 Count = 0; for (const auto Item : InItems) { if (Item.Slot.IsEmpty()) { ++Count; } } return Count; }
    private int32 DoFind(const TArray<FCk_CharacterItem>& InItems, const FString& InKey) const
    { for (int32 Index = 0; Index < InItems.Num(); ++Index) { if (InItems[Index].Key == InKey) { return Index; } } return -1; }
    private int32 DoSlotIndex(const TArray<FCk_CharacterItem>& InItems, const FString& InSlot) const
    { for (int32 Index = 0; Index < InItems.Num(); ++Index) { if (InItems[Index].Slot == InSlot) { return Index; } } return -1; }
    private TArray<FString> DoEquipmentOrder() const
    {
        auto Slots = TArray<FString>();
        Slots.Add("Head"); Slots.Add("Weapon"); Slots.Add("Chest"); Slots.Add("Amulet");
        Slots.Add("Hands"); Slots.Add("Ring1"); Slots.Add("Legs"); Slots.Add("Ring2");
        Slots.Add("Boots"); Slots.Add("Offhand"); return Slots;
    }
    private FString DoFirstEquippedKey() const
    {
        for (const auto EquipmentSlot : DoEquipmentOrder())
        { const int32 Index = DoSlotIndex(_Items, EquipmentSlot); if (Index >= 0) { return _Items[Index].Key; } }
        return "";
    }
    private FString DoSelectVisible(const TArray<FCk_CharacterItem>& InItems, const FString& InFilter, const FString& InPreferred) const
    {
        const int32 Preferred = DoFind(InItems, InPreferred);
        if (Preferred >= 0 && (!InItems[Preferred].Slot.IsEmpty() || DoVisible(InItems[Preferred], InFilter))) { return InPreferred; }
        for (const auto Item : InItems) { if (DoVisible(Item, InFilter)) { return Item.Key; } }
        return "";
    }
    private FString DoStats(const FCk_CharacterItem& InItem) const
    {
        if (InItem.Kind == "supply") { return InItem.Stats; }
        FString Stats = "";
        if (InItem.DamageMax > 0) { Stats = f"{InItem.DamageMin}–{InItem.DamageMax} Damage"; }
        if (InItem.Armor > 0)
        { if (!Stats.IsEmpty()) { Stats += "\n"; } Stats += f"{InItem.Armor} Armor"; }
        if (InItem.Strength > 0)
        { if (!Stats.IsEmpty()) { Stats += "\n"; } Stats += f"+{InItem.Strength} Strength"; }
        if (InItem.Dexterity > 0)
        { if (!Stats.IsEmpty()) { Stats += "\n"; } Stats += f"+{InItem.Dexterity} Dexterity"; }
        if (InItem.Life > 0)
        { if (!Stats.IsEmpty()) { Stats += "\n"; } Stats += f"+{InItem.Life} Life"; }
        if (InItem.Critical > 0)
        { if (!Stats.IsEmpty()) { Stats += "\n"; } Stats += f"+{InItem.Critical}% Critical Chance"; }
        return Stats;
    }
    private FCk_CharacterItem DoItem(const FString& InKey, const FString& InName, const FString& InKind,
        int32 InIcon, const FString& InRarity, const FString& InDescription, int32 InPower,
        int32 InDamageMin, int32 InDamageMax, int32 InArmor, int32 InStrength, int32 InDexterity,
        int32 InLife, int32 InCritical) const
    {
        FCk_CharacterItem Item; Item.Key = InKey; Item.Name = InName; Item.Kind = InKind;
        Item.Icon = InIcon; Item.Rarity = InRarity; Item.Description = InDescription;
        Item.Power = InPower; Item.DamageMin = InDamageMin; Item.DamageMax = InDamageMax;
        Item.Armor = InArmor; Item.Strength = InStrength; Item.Dexterity = InDexterity;
        Item.Life = InLife; Item.Critical = InCritical; Item.Stats = DoStats(Item);
        return Item;
    }
    private TArray<FCk_CharacterItem> DoSeeds() const
    {
        auto Items = TArray<FCk_CharacterItem>();
        Items.Add(DoItem("veilcutter", "Veilcutter", "weapon", 0, "Rare Sword", "A whisper of steel that finds the seam in every shadow.", 421, 78, 112, 0, 14, 2, 0, 9));
        Items.Add(DoItem("duskbow", "Duskwood Longbow", "weapon", 1, "Common Bow", "Seasoned yew with a taut black gutstring.", 312, 62, 88, 0, 0, 8, 0, 0));
        Items.Add(DoItem("hood", "Wanderer's Hood", "head", 2, "Common Helm", "A weathered hood, stitched to turn a glancing blade.", 234, 0, 0, 24, 0, 5, 0, 0));
        Items.Add(DoItem("chest", "Gravewarden Cuirass", "chest", 3, "Rare Chest Armor", "The wardens kept their oath long after the gates fell.", 385, 0, 0, 96, 0, 0, 12, 0));
        Items.Add(DoItem("gloves", "Ironbound Grips", "hands", 4, "Common Gloves", "Steel plates guard the knuckles and fingertips.", 260, 0, 0, 31, 4, 0, 0, 0));
        Items.Add(DoItem("boots", "Ashwalker Boots", "boots", 5, "Common Boots", "Their soles leave almost no trace in ash.", 257, 0, 0, 28, 0, 6, 0, 0));
        Items.Add(DoItem("amulet", "Emberglass Amulet", "amulet", 6, "Rare Amulet", "A sealed ember burns beneath the smoky glass.", 406, 0, 0, 0, 7, 0, 18, 0));
        Items.Add(DoItem("sapphire-ring", "Sapphire Signet", "ring", 7, "Rare Ring", "A blue stone cut with a six pointed star.", 382, 0, 0, 0, 0, 6, 9, 0));
        Items.Add(DoItem("skull-ring", "Bone Covenant", "ring", 8, "Common Ring", "A small vow carved into blackened bone.", 246, 0, 0, 0, 3, 0, 5, 0));
        auto Red = DoItem("red-potion", "Crimson Tonic", "supply", 9, "Common Consumable", "A sharp herbal draught for the wounded.", 0, 0, 0, 0, 0, 0, 0, 0); Red.Stats = "Restores 120 Life"; Red.Quantity = 3; Items.Add(Red);
        auto Blue = DoItem("blue-potion", "Azure Tonic", "supply", 10, "Common Consumable", "A cool tincture for long journeys.", 0, 0, 0, 0, 0, 0, 0, 0); Blue.Stats = "Restores 80 Focus"; Blue.Quantity = 2; Items.Add(Blue);
        Items.Add(DoItem("leggings", "Sentinel Greaves", "legs", 11, "Common Leg Armor", "Chain and leather from an old frontier watch.", 282, 0, 0, 51, 0, 0, 6, 0));
        Items.Add(DoItem("ironblade", "Ironblade", "weapon", 0, "Common Sword", "A spare blade tempered for hard travel.", 265, 42, 64, 0, 5, 0, 0, 0));
        Items.Add(DoItem("thornbow", "Thornbow", "weapon", 1, "Magic Bow", "A hunter's bow bound in dark thorn.", 338, 69, 93, 0, 0, 10, 0, 0));
        Items.Add(DoItem("ember-grips", "Ember Grips", "hands", 4, "Magic Gloves", "Warmth lingers in the seams.", 329, 0, 0, 38, 6, 0, 0, 0));
        Items.Add(DoItem("vigil-band", "Vigil Band", "ring", 7, "Magic Ring", "A quiet promise to return at dawn.", 323, 0, 0, 0, 0, 4, 12, 0));
        auto Sword = DoItem("ashen-edge", "Ashen Edge", "weapon", 0, "Common Sword", "A reliable blade darkened by the forge.", 219, 34, 51, 0, 3, 5, 0, 0);
        Sword.Slot = "Weapon"; Items.Add(Sword);
        auto WornHead = DoItem("worn-hood", "Ashen Hood", "head", 2, "Common Helm", "Linen darkened by a hundred campfires.", 201, 0, 0, 19, 0, 3, 0, 0); WornHead.Slot = "Head"; Items.Add(WornHead);
        auto WornChest = DoItem("worn-chest", "Wayfarer Cuirass", "chest", 3, "Common Chest Armor", "Worn plates still turn a blade.", 241, 0, 0, 71, 0, 0, 7, 0); WornChest.Slot = "Chest"; Items.Add(WornChest);
        auto WornHands = DoItem("worn-hands", "Gravehide Gloves", "hands", 4, "Common Gloves", "Soot pressed deep into the leather.", 183, 0, 0, 22, 2, 0, 0, 0); WornHands.Slot = "Hands"; Items.Add(WornHands);
        auto WornLegs = DoItem("worn-legs", "Tarnished Greaves", "legs", 11, "Common Leg Armor", "Iron links mended by hand.", 204, 0, 0, 39, 0, 0, 4, 0); WornLegs.Slot = "Legs"; Items.Add(WornLegs);
        auto WornBoots = DoItem("worn-boots", "Cinder Boots", "boots", 5, "Common Boots", "Their soles remember the long road.", 193, 0, 0, 18, 0, 3, 0, 0); WornBoots.Slot = "Boots"; Items.Add(WornBoots);
        auto WornAmulet = DoItem("worn-amulet", "Pilgrim Charm", "amulet", 6, "Common Amulet", "A keepsake from the southern gate.", 213, 0, 0, 0, 2, 0, 9, 0); WornAmulet.Slot = "Amulet"; Items.Add(WornAmulet);
        auto WornRing = DoItem("worn-ring", "Copper Oath", "ring", 8, "Common Ring", "A plain ring engraved with a vow.", 176, 0, 0, 0, 2, 0, 4, 0); WornRing.Slot = "Ring1"; Items.Add(WornRing);
        auto WornOffhand = DoItem("worn-offhand", "Ragged Quiver", "offhand", 1, "Common Quiver", "A handful of carefully fletched arrows.", 189, 0, 0, 0, 0, 2, 0, 0); WornOffhand.Slot = "Offhand"; Items.Add(WornOffhand);
        return Items;
    }
    private FCk_UI_AuthoredFieldValue DoTextField(const FString& InName, const FString& InValue) const
    { auto Field = FCk_UI_AuthoredFieldValue(InName, ECk_UI_AuthoredFieldKind::Text); Field.Set_Text(FText::FromString(InValue)); return Field; }
    private FCk_UI_AuthoredFieldValue DoNumberField(const FString& InName, float32 InValue) const
    { auto Field = FCk_UI_AuthoredFieldValue(InName, ECk_UI_AuthoredFieldKind::Number); Field.Set_Number(InValue); return Field; }
    private FCk_UI_AuthoredFieldValue DoBoolField(const FString& InName, bool InValue) const
    { auto Field = FCk_UI_AuthoredFieldValue(InName, ECk_UI_AuthoredFieldKind::Bool); Field.Set_Bool(InValue); return Field; }
    private int32 DoTier(const FCk_CharacterItem& InItem) const
    {
        if (InItem.Rarity.StartsWith("Legendary")) { return 3; }
        if (InItem.Rarity.StartsWith("Rare")) { return 2; }
        if (InItem.Rarity.StartsWith("Magic")) { return 1; }
        return 0;
    }
    private TArray<FCk_UI_AuthoredRecord> DoRecords(const TArray<FCk_CharacterItem>& InItems,
        const FString& InFilter, const FString& InSelected, bool InCompare) const
    {
        auto Records = TArray<FCk_UI_AuthoredRecord>();
        for (const auto Item : InItems)
        {
            if (!DoVisible(Item, InFilter)) { continue; }
            auto Fields = TArray<FCk_UI_AuthoredFieldValue>();
            Fields.Add(DoTextField("key", Item.Key)); Fields.Add(DoTextField("name", Item.Name));
            Fields.Add(DoTextField("kind", Item.Kind)); Fields.Add(DoNumberField("icon", float32(Item.Icon)));
            Fields.Add(DoNumberField("tier", float32(DoTier(Item))));
            Fields.Add(DoNumberField("quantity", float32(Item.Quantity)));
            Fields.Add(DoTextField("quantityLabel", Item.Quantity > 1 ? f"{Item.Quantity}" : ""));
            Fields.Add(DoBoolField("selected", Item.Key == InSelected)); Fields.Add(DoBoolField("enabled", true));
            Fields.Add(DoBoolField("equipped", false)); Fields.Add(DoTextField("rarity", Item.Rarity));
            Fields.Add(DoTextField("stats", Item.Stats)); Fields.Add(DoTextField("description", Item.Description));
            Fields.Add(DoTextField("compare", InCompare ? DoComparison(Item, InItems) : ""));
            const int32 Compared = DoComparisonIndex(Item, InItems);
            Fields.Add(DoBoolField("improvement", Compared >= 0
                && (Item.Armor + Item.Strength + Item.Dexterity + Item.Life + Item.DamageMax)
                > (InItems[Compared].Armor + InItems[Compared].Strength + InItems[Compared].Dexterity
                    + InItems[Compared].Life + InItems[Compared].DamageMax)));
            auto Record = FCk_UI_AuthoredRecord(Item.Key); Record.Set_Fields(Fields); Records.Add(Record);
        }
        // Empty cells are presentation records only. Their stable identities never enter _Items.
        for (int32 Cell = Records.Num(); Cell < 24; ++Cell)
        {
            auto Fields = TArray<FCk_UI_AuthoredFieldValue>();
            Fields.Add(DoTextField("key", "")); Fields.Add(DoTextField("name", ""));
            Fields.Add(DoTextField("kind", "")); Fields.Add(DoNumberField("icon", -1.0f));
            Fields.Add(DoNumberField("tier", 0.0f)); Fields.Add(DoNumberField("quantity", 0.0f));
            Fields.Add(DoTextField("quantityLabel", ""));
            Fields.Add(DoBoolField("selected", false)); Fields.Add(DoBoolField("enabled", false));
            Fields.Add(DoBoolField("equipped", false)); Fields.Add(DoTextField("rarity", ""));
            Fields.Add(DoTextField("stats", "")); Fields.Add(DoTextField("description", ""));
            Fields.Add(DoTextField("compare", "")); Fields.Add(DoBoolField("improvement", false));
            auto Record = FCk_UI_AuthoredRecord(f"empty-{Cell}"); Record.Set_Fields(Fields); Records.Add(Record);
        }
        return Records;
    }
    private FString DoDefaultSlotFor(const FCk_CharacterItem& InItem,
        const TArray<FCk_CharacterItem>& InItems) const
    {
        if (InItem.Kind == "ring") { return _RingTarget; }
        if (InItem.Kind == "weapon") { return "Weapon"; }
        if (InItem.Kind == "offhand") { return "Offhand"; }
        if (InItem.Kind == "head") { return "Head"; } if (InItem.Kind == "chest") { return "Chest"; }
        if (InItem.Kind == "hands") { return "Hands"; } if (InItem.Kind == "legs") { return "Legs"; }
        if (InItem.Kind == "boots") { return "Boots"; } if (InItem.Kind == "amulet") { return "Amulet"; }
        return "";
    }
    private FString DoDefaultSlot(const FCk_CharacterItem& InItem) const
    { return DoDefaultSlotFor(InItem, _Items); }
    private int32 DoComparisonIndex(const FCk_CharacterItem& InItem,
        const TArray<FCk_CharacterItem>& InItems) const
    {
        if (InItem.Kind == "supply") { return -1; }
        const FString EquipmentSlot = DoDefaultSlotFor(InItem, InItems);
        const int32 Equipped = DoSlotIndex(InItems, EquipmentSlot);
        return Equipped >= 0 && InItems[Equipped].Key != InItem.Key ? Equipped : -1;
    }
    private FString DoComparison(const FCk_CharacterItem& InItem, const TArray<FCk_CharacterItem>& InItems) const
    {
        const int32 Equipped = DoComparisonIndex(InItem, InItems);
        if (Equipped < 0) { return ""; }
        return f"EQUIPPED: {InItems[Equipped].Name}\n{InItems[Equipped].Stats}";
    }
    private FString DoPrimaryStat(const FCk_CharacterItem& InItem) const
    {
        if (InItem.DamageMax > 0) { return f"{InItem.DamageMin}–{InItem.DamageMax} DAMAGE"; }
        if (InItem.Armor > 0) { return f"{InItem.Armor} ARMOR"; }
        if (InItem.Kind == "supply") { return InItem.Stats; }
        return "ACCESSORY";
    }
    private FString DoAffix(int32 InValue) const { return InValue == 0 ? "—" : f"+{InValue}"; }
    private FString DoDelta(int32 InValue) const
    { return InValue == 0 ? "—" : (InValue > 0 ? f"+{InValue}" : f"{InValue}"); }
    private bool DoSetDelta(const FString& InName, int32 InValue, bool InVisible)
    {
        FString Delta = DoDelta(InValue);
        if (InValue != 0 && (InName == "SelectedDeltaCritical" || InName == "EquippedDeltaCritical"))
        { Delta += "%"; }
        return DoSetText(InName, Delta)
            && _Session.TrySetBool(f"{InName}Gain", InVisible && InValue > 0)
            && _Session.TrySetBool(f"{InName}Loss", InVisible && InValue < 0)
            && _Session.TrySetBool(f"{InName}Neutral", InVisible && InValue == 0);
    }
    private bool DoSetCard(const FString& InPrefix, const FCk_CharacterItem& InItem, bool InPresent)
    {
        return DoSetText(f"{InPrefix}Name", InPresent ? InItem.Name : "")
            && DoSetText(f"{InPrefix}Rarity", InPresent ? InItem.Rarity : "")
            && DoSetText(f"{InPrefix}Power", InPresent && InItem.Kind != "supply" ? f"ITEM POWER {InItem.Power}" : "")
            && DoSetText(f"{InPrefix}Primary", InPresent ? DoPrimaryStat(InItem) : "")
            && DoSetText(f"{InPrefix}Description", InPresent ? InItem.Description : "")
            && DoSetText(f"{InPrefix}Armor", InPresent ? DoAffix(InItem.Armor) : "—")
            && DoSetText(f"{InPrefix}Strength", InPresent ? DoAffix(InItem.Strength) : "—")
            && DoSetText(f"{InPrefix}Dexterity", InPresent ? DoAffix(InItem.Dexterity) : "—")
            && DoSetText(f"{InPrefix}Life", InPresent ? DoAffix(InItem.Life) : "—")
            && DoSetText(f"{InPrefix}Critical", InPresent && InItem.Critical != 0 ? f"+{InItem.Critical}%" : "—")
            && _Session.TrySetNumber(f"{InPrefix}Icon", InPresent ? float32(InItem.Icon) : -1.0f)
            && _Session.TrySetBool(f"{InPrefix}GearStats", InPresent && InItem.Kind != "supply");
    }
    private bool DoSetText(const FString& InName, const FString& InValue)
    { return _Session.TrySetText(InName, FText::FromString(InValue)); }
    private bool DoPublishScalars(const TArray<FCk_CharacterItem>& InItems, const FString& InSelected,
        const FString& InFilter, bool InCompare, const FString& InStatus)
    {
        if (!DoReady()) { return false; }
        int32 VisibleCount = 0;
        int32 OwnedCount = 0;
        int32 TotalArmor = 100; int32 TotalStrength = 30;
        int32 TotalDexterity = 25; int32 TotalLife = 500;
        for (const auto Item : InItems) { if (DoVisible(Item, InFilter)) { ++VisibleCount; } }
        for (const auto Item : InItems)
        {
            if (Item.Slot.IsEmpty()) { ++OwnedCount; }
            else
            {
                TotalArmor += Item.Armor; TotalStrength += Item.Strength;
                TotalDexterity += Item.Dexterity; TotalLife += Item.Life;
            }
        }
        for (const auto EquipmentSlot : DoSlots())
        {
            const int32 Index = DoSlotIndex(InItems, EquipmentSlot);
            const FString Key = Index >= 0 ? InItems[Index].Key : "";
            const FString Kind = Index >= 0 ? InItems[Index].Kind : "";
            const FString ItemName = Index >= 0 ? InItems[Index].Name : "Empty";
            const FString Stats = Index >= 0 ? InItems[Index].Stats : "";
            const float32 Icon = Index >= 0 ? float32(InItems[Index].Icon) : -1.0f;
            const float32 Tier = Index >= 0 ? float32(DoTier(InItems[Index])) : 0.0f;
            if (!DoSetText(f"{EquipmentSlot}Key", Key) || !DoSetText(f"{EquipmentSlot}Kind", Kind)
                || !DoSetText(f"{EquipmentSlot}Name", ItemName) || !DoSetText(f"{EquipmentSlot}Stats", Stats)
                || !_Session.TrySetNumber(f"{EquipmentSlot}Icon", Icon)
                || !_Session.TrySetNumber(f"{EquipmentSlot}Tier", Tier)
                || !_Session.TrySetBool(f"{EquipmentSlot}Selected", Key == InSelected && !Key.IsEmpty())
                || !_Session.TrySetBool(f"{EquipmentSlot}Empty", Index < 0)) { return false; }
        }
        const int32 Selected = DoFind(InItems, InSelected);
        const bool HasSelection = Selected >= 0;
        const bool CanEquip = HasSelection && InItems[Selected].Slot.IsEmpty()
            && !DoDefaultSlotFor(InItems[Selected], InItems).IsEmpty();
        const bool CanUnequip = HasSelection && !InItems[Selected].Slot.IsEmpty() && OwnedCount < 24;
        const bool IsSupply = HasSelection && InItems[Selected].Kind == "supply";
        const bool CanPrimary = CanEquip || CanUnequip || IsSupply;
        const int32 Equipped = HasSelection ? DoComparisonIndex(InItems[Selected], InItems) : -1;
        const bool ComparisonVisible = InCompare && Equipped >= 0;
        FCk_CharacterItem Empty;
        const FCk_CharacterItem Chosen = HasSelection ? InItems[Selected] : Empty;
        const FCk_CharacterItem Previous = Equipped >= 0 ? InItems[Equipped] : Empty;
        const FString Comparison = HasSelection && InCompare ? DoComparison(Chosen, InItems) : "";
        const FString PrimaryLabel = IsSupply ? "USE" : (CanUnequip ? "UNEQUIP" : (CanEquip ? "EQUIP" : "UNAVAILABLE"));
        const FString SelectionContext = HasSelection && !Chosen.Slot.IsEmpty()
            ? f" · {Chosen.Slot} equipped" : "";
        if (!DoSetCard("Selected", Chosen, HasSelection)
            || !DoSetCard("Equipped", Previous, Equipped >= 0)
            || !DoSetText("SelectedStats", HasSelection ? Chosen.Stats : "")
            || !DoSetText("ComparisonStats", Comparison) || !DoSetText("Status", InStatus)
            || !DoSetText("InventoryCount", f"{OwnedCount} / 24")
            || !DoSetText("FilterContext", f"{InFilter} · {VisibleCount} shown{SelectionContext}")
            || !DoSetText("PrimaryActionLabel", PrimaryLabel)
            || !DoSetText("CompareActionLabel", InCompare ? "HIDE COMPARISON" : "SHOW COMPARISON")
            || !DoSetText("RingTargetLabel", f"TARGET {_RingTarget}")
            || !DoSetText("Armor", f"{TotalArmor}") || !DoSetText("Strength", f"{TotalStrength}")
            || !DoSetText("Dexterity", f"{TotalDexterity}") || !DoSetText("Life", f"{TotalLife} / {TotalLife}")
            || !_Session.TrySetNumber("SelectedIcon", HasSelection ? float32(InItems[Selected].Icon) : -1.0f)
            || !_Session.TrySetNumber("SelectedTier", HasSelection ? float32(DoTier(InItems[Selected])) : 0.0f)
            || !_Session.TrySetBool("HasSelection", HasSelection)
            || !_Session.TrySetBool("CanEquip", CanEquip)
            || !_Session.TrySetBool("CanUnequip", CanUnequip)
            || !_Session.TrySetBool("CanPrimaryAction", CanPrimary)
            || !_Session.TrySetBool("CompareVisible", ComparisonVisible)
            || !_Session.TrySetBool("CompareEnabled", Equipped >= 0)
            || !_Session.TrySetBool("RingTargetVisible", HasSelection && Chosen.Kind == "ring")
            || !_Session.TrySetBool("FilterAllActive", InFilter == "All")
            || !_Session.TrySetBool("FilterEquipmentActive", InFilter == "Equipment")
            || !_Session.TrySetBool("FilterSuppliesActive", InFilter == "Supplies")
            || !DoSetDelta("SelectedDeltaArmor", Chosen.Armor - Previous.Armor, ComparisonVisible)
            || !DoSetDelta("SelectedDeltaStrength", Chosen.Strength - Previous.Strength, ComparisonVisible)
            || !DoSetDelta("SelectedDeltaDexterity", Chosen.Dexterity - Previous.Dexterity, ComparisonVisible)
            || !DoSetDelta("SelectedDeltaLife", Chosen.Life - Previous.Life, ComparisonVisible)
            || !DoSetDelta("SelectedDeltaCritical", Chosen.Critical - Previous.Critical, ComparisonVisible)
            || !DoSetDelta("EquippedDeltaArmor", Previous.Armor - Chosen.Armor, ComparisonVisible)
            || !DoSetDelta("EquippedDeltaStrength", Previous.Strength - Chosen.Strength, ComparisonVisible)
            || !DoSetDelta("EquippedDeltaDexterity", Previous.Dexterity - Chosen.Dexterity, ComparisonVisible)
            || !DoSetDelta("EquippedDeltaLife", Previous.Life - Chosen.Life, ComparisonVisible)
            || !DoSetDelta("EquippedDeltaCritical", Previous.Critical - Chosen.Critical, ComparisonVisible))
        { return false; }
        return true;
    }
    private void DoRetire(const FString& InError)
    {
        _SourceError = InError; _Open = false; _Retired = true;
        if (ck::IsValid(_Session)) { _Session.Request_EndInput(false); _Session.Request_Release(); }
        if (ck::IsValid(_Surface)) { _Surface.ReleaseView(); }
        RemoveFromParent();
    }
    private bool DoPublish(const TArray<FCk_CharacterItem>& InItems, const FString& InFilter,
        const FString& InPreferred, bool InCompare, const FString& InStatus)
    {
        if (!DoReady()) { return false; }
        const FString Selection = DoSelectVisible(InItems, InFilter, InPreferred);
        auto Errors = TArray<FString>();
        if (!_Session.TrySetRecords("Items", DoRecords(InItems, InFilter, Selection, InCompare), Errors))
        { DoErrors(Errors); DoRetire(_SourceError); return false; }
        if (!DoPublishScalars(InItems, Selection, InFilter, InCompare, InStatus))
        { DoRetire("Required character binding failed during publication"); return false; }
        _Items = InItems; _Filter = InFilter; _SelectedKey = Selection; _Compare = InCompare; _Status = InStatus;
        return true;
    }
    private void DoErrors(const TArray<FString>& InErrors)
    { _SourceError = ""; for (const auto Error : InErrors) { _SourceError = _SourceError.IsEmpty() ? Error : f"{_SourceError}; {Error}"; } }
    private bool DoFill(UOverlaySlot InSlot)
    {
        if (!ck::IsValid(InSlot)) { return false; }
        InSlot.SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
        InSlot.SetVerticalAlignment(EVerticalAlignment::VAlign_Fill); return true;
    }
    UFUNCTION()
    bool TryInitializeScreen()
    {
        if (ck::IsValid(_Session)) { return false; }
        _Session = Cast<UCk_UI_AuthoredSession_UE>(NewObject(this, UCk_UI_AuthoredSession_UE));
        _Surface = Cast<UCk_UI_AuthoredSurface_UE>(ConstructWidget(UCk_UI_AuthoredSurface_UE, n"CharacterEquipmentSurface"));
        auto Scale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"CharacterEquipmentScale"));
        auto Frame = Cast<USizeBox>(ConstructWidget(USizeBox, n"CharacterEquipmentFrame"));
        auto Root = Cast<UOverlay>(ConstructWidget(UOverlay, n"CharacterEquipmentRoot"));
        auto BackdropScale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"CharacterEquipmentBackdropScale"));
        auto BackdropImage = Cast<UImage>(ConstructWidget(UImage, n"CharacterEquipmentBackdrop"));
        _Backdrop = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoFantasyDirectory(), "dungeon-backdrop.png"));
        _Portrait = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "character-portrait.png"));
        _Atlas = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "item-atlas.png"));
        _Ornament = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "ornamental-frame.png"));
        _Alcove = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "character-alcove.png"));
        _SlotFrame = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "slot-frame.png"));
        _StatGlyphAtlas = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "stat-glyph-atlas.png"));
        _PanelFrame = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "panel-frame.png"));
        _StatStrip = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "stat-strip.png"));
        _TitleStrip = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "title-strip.png"));
        if (!ck::IsValid(_Session) || !ck::IsValid(_Surface) || !ck::IsValid(Scale) || !ck::IsValid(Frame)
            || !ck::IsValid(Root) || !ck::IsValid(BackdropScale) || !ck::IsValid(BackdropImage)
            || !ck::IsValid(_Backdrop) || !ck::IsValid(_Portrait) || !ck::IsValid(_Atlas) || !ck::IsValid(_Ornament)
            || !ck::IsValid(_Alcove) || !ck::IsValid(_SlotFrame) || !ck::IsValid(_StatGlyphAtlas)
            || !ck::IsValid(_PanelFrame) || !ck::IsValid(_StatStrip) || !ck::IsValid(_TitleStrip))
        { _StartupError = "Character screen widget or art construction failed"; Request_Shutdown(); return false; }
        _Items = DoSeeds();
        auto Schema = TArray<FCk_UI_AuthoredFieldSchema>();
        Schema.Add(FCk_UI_AuthoredFieldSchema("key", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("name", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("kind", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("icon", ECk_UI_AuthoredFieldKind::Number));
        Schema.Add(FCk_UI_AuthoredFieldSchema("tier", ECk_UI_AuthoredFieldKind::Number));
        Schema.Add(FCk_UI_AuthoredFieldSchema("quantity", ECk_UI_AuthoredFieldKind::Number));
        Schema.Add(FCk_UI_AuthoredFieldSchema("quantityLabel", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("selected", ECk_UI_AuthoredFieldKind::Bool));
        Schema.Add(FCk_UI_AuthoredFieldSchema("enabled", ECk_UI_AuthoredFieldKind::Bool));
        Schema.Add(FCk_UI_AuthoredFieldSchema("equipped", ECk_UI_AuthoredFieldKind::Bool));
        Schema.Add(FCk_UI_AuthoredFieldSchema("rarity", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("stats", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("description", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("compare", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("improvement", ECk_UI_AuthoredFieldKind::Bool));
        auto Collection = FCk_UI_AuthoredCollection("Items"); Collection.Set_Schema(Schema);
        Collection.Set_Records(DoRecords(_Items, _Filter, _SelectedKey, _Compare));
        auto Collections = TArray<FCk_UI_AuthoredCollection>(); Collections.Add(Collection);
        auto Texts = TArray<FCk_UI_AuthoredText>();
        for (const auto EquipmentSlot : DoSlots())
        {
            const int32 Index = DoSlotIndex(_Items, EquipmentSlot);
            Texts.Add(FCk_UI_AuthoredText(f"{EquipmentSlot}Key", FText::FromString(Index >= 0 ? _Items[Index].Key : "")));
            Texts.Add(FCk_UI_AuthoredText(f"{EquipmentSlot}Kind", FText::FromString(Index >= 0 ? _Items[Index].Kind : "")));
            Texts.Add(FCk_UI_AuthoredText(f"{EquipmentSlot}Name", FText::FromString(Index >= 0 ? _Items[Index].Name : "Empty")));
            Texts.Add(FCk_UI_AuthoredText(f"{EquipmentSlot}Stats", FText::FromString(Index >= 0 ? _Items[Index].Stats : "")));
        }
        Texts.Add(FCk_UI_AuthoredText("SelectedName", FText::FromString(_Items[0].Name)));
        Texts.Add(FCk_UI_AuthoredText("SelectedRarity", FText::FromString(_Items[0].Rarity)));
        Texts.Add(FCk_UI_AuthoredText("SelectedStats", FText::FromString(_Items[0].Stats)));
        Texts.Add(FCk_UI_AuthoredText("SelectedDescription", FText::FromString(_Items[0].Description)));
        Texts.Add(FCk_UI_AuthoredText("ComparisonStats", FText::FromString(DoComparison(_Items[0], _Items))));
        Texts.Add(FCk_UI_AuthoredText("Status", FText::FromString(_Status)));
        Texts.Add(FCk_UI_AuthoredText("InventoryCount", FText::FromString("12 ITEMS")));
        Texts.Add(FCk_UI_AuthoredText("Armor", FText::FromString("230")));
        Texts.Add(FCk_UI_AuthoredText("Strength", FText::FromString("42")));
        Texts.Add(FCk_UI_AuthoredText("Dexterity", FText::FromString("35")));
        Texts.Add(FCk_UI_AuthoredText("Life", FText::FromString("548 / 548")));
        for (const auto Prefix : DoCardPrefixes())
        {
            if (Prefix == "Equipped")
            {
                Texts.Add(FCk_UI_AuthoredText("EquippedName", FText::FromString("")));
                Texts.Add(FCk_UI_AuthoredText("EquippedRarity", FText::FromString("")));
                Texts.Add(FCk_UI_AuthoredText("EquippedDescription", FText::FromString("")));
            }
            Texts.Add(FCk_UI_AuthoredText(f"{Prefix}Power", FText::FromString("")));
            Texts.Add(FCk_UI_AuthoredText(f"{Prefix}Primary", FText::FromString("")));
            Texts.Add(FCk_UI_AuthoredText(f"{Prefix}Armor", FText::FromString("—")));
            Texts.Add(FCk_UI_AuthoredText(f"{Prefix}Strength", FText::FromString("—")));
            Texts.Add(FCk_UI_AuthoredText(f"{Prefix}Dexterity", FText::FromString("—")));
            Texts.Add(FCk_UI_AuthoredText(f"{Prefix}Life", FText::FromString("—")));
            Texts.Add(FCk_UI_AuthoredText(f"{Prefix}Critical", FText::FromString("—")));
        }
        Texts.Add(FCk_UI_AuthoredText("PrimaryActionLabel", FText::FromString("EQUIP")));
        Texts.Add(FCk_UI_AuthoredText("CompareActionLabel", FText::FromString("HIDE COMPARISON")));
        Texts.Add(FCk_UI_AuthoredText("RingTargetLabel", FText::FromString("TARGET RING1")));
        Texts.Add(FCk_UI_AuthoredText("FilterContext", FText::FromString("ALL · 16 SHOWN")));
        for (const auto Affix : DoAffixes())
        { Texts.Add(FCk_UI_AuthoredText(f"SelectedDelta{Affix}", FText::FromString("—")));
          Texts.Add(FCk_UI_AuthoredText(f"EquippedDelta{Affix}", FText::FromString("—"))); }
        auto Numbers = TArray<FCk_UI_AuthoredNumber>();
        for (const auto EquipmentSlot : DoSlots())
        {
            const int32 Index = DoSlotIndex(_Items, EquipmentSlot);
            Numbers.Add(FCk_UI_AuthoredNumber(f"{EquipmentSlot}Icon", Index >= 0 ? float32(_Items[Index].Icon) : -1.0f));
            Numbers.Add(FCk_UI_AuthoredNumber(f"{EquipmentSlot}Tier", Index >= 0 ? float32(DoTier(_Items[Index])) : 0.0f));
            Numbers.Add(FCk_UI_AuthoredNumber(f"{EquipmentSlot}Placeholder", float32(DoPlaceholderIcon(EquipmentSlot))));
        }
        Numbers.Add(FCk_UI_AuthoredNumber("SelectedIcon", 0.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("SelectedTier", float32(DoTier(_Items[0]))));
        Numbers.Add(FCk_UI_AuthoredNumber("EquippedIcon", -1.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("GlyphStrength", 0.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("GlyphDexterity", 1.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("GlyphArmor", 2.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("GlyphLife", 3.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("GlyphCritical", 4.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("GlyphAffix", 5.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("GlyphGain", 6.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("GlyphLoss", 7.0f));
        auto Bools = TArray<FCk_UI_AuthoredBool>();
        for (const auto EquipmentSlot : DoSlots())
        { Bools.Add(FCk_UI_AuthoredBool(f"{EquipmentSlot}Selected", false));
          Bools.Add(FCk_UI_AuthoredBool(f"{EquipmentSlot}Empty", false)); }
        Bools.Add(FCk_UI_AuthoredBool("HasSelection", true)); Bools.Add(FCk_UI_AuthoredBool("CompareVisible", true));
        Bools.Add(FCk_UI_AuthoredBool("Never", false));
        Bools.Add(FCk_UI_AuthoredBool("SelectedGearStats", true));
        Bools.Add(FCk_UI_AuthoredBool("EquippedGearStats", true));
        Bools.Add(FCk_UI_AuthoredBool("CanEquip", true)); Bools.Add(FCk_UI_AuthoredBool("CanUnequip", false));
        Bools.Add(FCk_UI_AuthoredBool("CanPrimaryAction", true));
        Bools.Add(FCk_UI_AuthoredBool("CompareEnabled", true));
        Bools.Add(FCk_UI_AuthoredBool("RingTargetVisible", false));
        Bools.Add(FCk_UI_AuthoredBool("FilterAllActive", true));
        Bools.Add(FCk_UI_AuthoredBool("FilterEquipmentActive", false));
        Bools.Add(FCk_UI_AuthoredBool("FilterSuppliesActive", false));
        for (const auto Affix : DoAffixes())
        { for (const auto State : DoDeltaStates())
          { Bools.Add(FCk_UI_AuthoredBool(f"SelectedDelta{Affix}{State}", false));
            Bools.Add(FCk_UI_AuthoredBool(f"EquippedDelta{Affix}{State}", false)); } }
        auto Images = TArray<FCk_UI_AuthoredImage>();
        auto Portrait = FCk_UI_AuthoredImage("CharacterPortrait"); Portrait.Set_Texture(_Portrait); Images.Add(Portrait);
        auto Atlas = FCk_UI_AuthoredImage("ItemAtlas"); Atlas.Set_Texture(_Atlas); Images.Add(Atlas);
        auto Ornament = FCk_UI_AuthoredImage("OrnamentalFrame"); Ornament.Set_Texture(_Ornament); Images.Add(Ornament);
        auto Alcove = FCk_UI_AuthoredImage("CharacterAlcove"); Alcove.Set_Texture(_Alcove); Images.Add(Alcove);
        auto SlotFrame = FCk_UI_AuthoredImage("SlotFrame"); SlotFrame.Set_Texture(_SlotFrame); Images.Add(SlotFrame);
        auto StatGlyphAtlas = FCk_UI_AuthoredImage("StatGlyphAtlas"); StatGlyphAtlas.Set_Texture(_StatGlyphAtlas); Images.Add(StatGlyphAtlas);
        auto PanelFrame = FCk_UI_AuthoredImage("PanelFrame"); PanelFrame.Set_Texture(_PanelFrame); Images.Add(PanelFrame);
        auto StatStrip = FCk_UI_AuthoredImage("StatStrip"); StatStrip.Set_Texture(_StatStrip); Images.Add(StatStrip);
        auto TitleStrip = FCk_UI_AuthoredImage("TitleStrip"); TitleStrip.Set_Texture(_TitleStrip); Images.Add(TitleStrip);
        auto Actions = TArray<FString>();
        Actions.Add("FilterAll"); Actions.Add("FilterEquipment"); Actions.Add("FilterSupplies");
        Actions.Add("EquipSelected"); Actions.Add("UnequipSelected"); Actions.Add("ToggleCompare");
        Actions.Add("PrimaryAction"); Actions.Add("CycleRingTarget"); Actions.Add("ToggleRegion");
        Actions.Add("ResetFixture"); Actions.Add("Close"); Actions.Add("Reload");
        auto ItemActions = TArray<FString>(); ItemActions.Add("SelectItem");
        for (const auto EquipmentSlot : DoSlots()) { ItemActions.Add(f"Equip{EquipmentSlot}"); }
        auto Regions = TArray<FString>(); Regions.Add("main");
        const auto FontDir = FPaths::CombinePaths(DoFantasyDirectory(), "Fonts");
        const auto Fallback = FPaths::CombinePaths(FPaths::EngineContentDir(), "Slate/Fonts/DroidSansFallback.ttf");
        auto Display = FCk_UI_AuthoredFontFamily("fantasy-display", FPaths::CombinePaths(FontDir, "MarcellusSC-Regular.ttf"));
        Display.Set_FallbackRegularFile(Fallback);
        auto Ui = FCk_UI_AuthoredFontFamily("fantasy-ui", FPaths::CombinePaths(FontDir, "SourceSans3-Regular.ttf"));
        Ui.Set_BoldFile(FPaths::CombinePaths(FontDir, "SourceSans3-Bold.ttf"));
        Ui.Set_FallbackRegularFile(Fallback); Ui.Set_FallbackBoldFile(Fallback);
        const auto CharacterFontDir = FPaths::CombinePaths(DoDirectory(), "Fonts");
        auto Body = FCk_UI_AuthoredFontFamily("fantasy-body", FPaths::CombinePaths(CharacterFontDir, "CrimsonText-Regular.ttf"));
        Body.Set_FallbackRegularFile(Fallback);
        auto Flavor = FCk_UI_AuthoredFontFamily("fantasy-flavor", FPaths::CombinePaths(CharacterFontDir, "CrimsonText-Italic.ttf"));
        Flavor.Set_FallbackRegularFile(Fallback);
        auto Fonts = TArray<FCk_UI_AuthoredFontFamily>(); Fonts.Add(Display); Fonts.Add(Ui);
        Fonts.Add(Body); Fonts.Add(Flavor);
        auto StyleTokens = TMap<FString, FString>();
        StyleTokens.Add("--space-xxs", "4px"); StyleTokens.Add("--space-xs", "8px");
        StyleTokens.Add("--space-s", "12px"); StyleTokens.Add("--space-m", "16px");
        StyleTokens.Add("--space-l", "24px"); StyleTokens.Add("--space-xl", "32px");
        StyleTokens.Add("--panel-inset", "16px 20px");
        StyleTokens.Add("--identity-inset", "16px 24px 12px 24px");
        StyleTokens.Add("--ink-title", "#E8DFCB"); StyleTokens.Add("--ink-body", "#CBC2AE");
        StyleTokens.Add("--ink-secondary", "#B3A38A"); StyleTokens.Add("--ink-muted", "#948875");
        StyleTokens.Add("--ink-gold", "#C6A165"); StyleTokens.Add("--ink-gain", "#86D57D");
        StyleTokens.Add("--ink-loss", "#EA7467");
        auto Config = FCk_UI_AuthoredSession_Config();
        Config.Set_InteractionOwner(this);
        Config.Set_Regions(Regions); Config.Set_Text(Texts); Config.Set_Number(Numbers); Config.Set_Bool(Bools);
        Config.Set_Collections(Collections); Config.Set_Images(Images); Config.Set_Actions(Actions);
        Config.Set_ItemActions(ItemActions); Config.Set_FontFamilies(Fonts); Config.Set_StyleTokens(StyleTokens);
        Config.Set_RegisterHudControls(true); Config.Set_RegisterItemControls(true);
        auto Errors = TArray<FString>();
        if (!_Session.TryInitialize(Config, Errors))
        { DoErrors(Errors); _StartupError = _SourceError; Request_Shutdown(); return false; }
        if (!DoPublishScalars(_Items, _SelectedKey, _Filter, _Compare, _Status))
        { _StartupError = "Initial character bindings failed"; Request_Shutdown(); return false; }
        _Session.OnAction.AddUFunction(this, n"DoOnAction");
        _Session.OnItemAction.AddUFunction(this, n"DoOnItemAction");
        if (!Request_Reload() || !_Session.TryMount(_Surface, "main", Errors))
        { if (!Errors.IsEmpty()) { DoErrors(Errors); } _StartupError = _SourceError; Request_Shutdown(); return false; }
        Frame.SetWidthOverride(1920.0f); Frame.SetHeightOverride(1080.0f); Frame.AddChild(_Surface);
        Scale.SetStretch(EStretch::ScaleToFit); Scale.AddChild(Frame);
        BackdropScale.SetStretch(EStretch::ScaleToFill); BackdropScale.SetClipping(EWidgetClipping::ClipToBounds);
        BackdropScale.SetVisibility(ESlateVisibility::HitTestInvisible);
        BackdropImage.SetBrushFromTexture(_Backdrop, true); BackdropScale.AddChild(BackdropImage);
        if (!DoFill(Root.AddChildToOverlay(BackdropScale)) || !DoFill(Root.AddChildToOverlay(Scale)))
        { _StartupError = "Character screen composition failed"; Request_Shutdown(); return false; }
        SetRootWidget(Root); return true;
    }
    UFUNCTION()
    bool Request_Reload()
    {
        if (!DoReady()) { return false; }
        FString Markup; FString Stylesheet;
        if (!FFileHelper::LoadFileToString(Markup, FPaths::CombinePaths(DoDirectory(), "CharacterEquipmentProof.ui.html"))
            || !FFileHelper::LoadFileToString(Stylesheet, FPaths::CombinePaths(DoDirectory(), "CharacterEquipmentProof.ui.css")))
        { _SourceError = "Character screen source files could not load"; return false; }
        auto Errors = TArray<FString>(); const bool Accepted = _Session.TryReload(Markup, Stylesheet, Errors);
        DoErrors(Errors); return Accepted;
    }
    UFUNCTION()
    bool Request_TryEquip(FString InItemKey, FString InSlot)
    {
        if (!DoReady() || !DoValidSlot(InSlot)) { return false; }
        const int32 Index = DoFind(_Items, InItemKey);
        if (Index < 0 || _Items[Index].Slot == InSlot || !DoAccepts(_Items[Index].Kind, InSlot)) { return false; }
        const FString FormerSlot = _Items[Index].Slot;
        const int32 Displaced = DoSlotIndex(_Items, InSlot);
        if (Displaced >= 0 && !FormerSlot.IsEmpty()
            && !DoAccepts(_Items[Displaced].Kind, FormerSlot)) { return false; }
        auto Candidate = _Items;
        if (Displaced >= 0) { Candidate[Displaced].Slot = FormerSlot; }
        Candidate[Index].Slot = InSlot;
        if (!DoPublish(Candidate, _Filter, InItemKey, _Compare, f"Equipped {Candidate[Index].Name}.")) { return false; }
        _NavigationRegion = "Equipment"; ++_MutationCount; DoFocusSelection(); return true;
    }
    UFUNCTION()
    bool Request_Unequip(FString InSlot)
    {
        if (!DoReady() || !DoValidSlot(InSlot) || DoOwnedCount(_Items) >= 24) { return false; }
        auto Candidate = _Items; const int32 Index = DoSlotIndex(Candidate, InSlot);
        if (Index < 0) { return false; }
        Candidate[Index].Slot = "";
        if (!DoPublish(Candidate, _Filter, Candidate[Index].Key, _Compare, f"Unequipped {Candidate[Index].Name}.")) { return false; }
        _NavigationRegion = "Inventory"; ++_MutationCount; DoFocusSelection(); return true;
    }
    UFUNCTION()
    bool Request_Select(FString InKey)
    {
        const int32 Index = DoFind(_Items, InKey);
        if (!DoReady() || Index < 0 || (_Items[Index].Slot.IsEmpty() && !DoVisible(_Items[Index], _Filter))) { return false; }
        const FString Context = _Items[Index].Slot.IsEmpty()
            ? f"{_Items[Index].Name} selected from inventory."
            : f"{_Items[Index].Name} selected in {_Items[Index].Slot}.";
        if (!DoPublish(_Items, _Filter, InKey, _Compare, Context)) { return false; }
        _NavigationRegion = _Items[Index].Slot.IsEmpty() ? "Inventory" : "Equipment";
        DoFocusSelection(); return true;
    }
    UFUNCTION()
    bool Request_Filter(FString InFilter)
    {
        if (InFilter != "All" && InFilter != "Equipment" && InFilter != "Supplies") { return false; }
        if (!DoPublish(_Items, InFilter, _SelectedKey, _Compare, f"Showing {InFilter.ToLower()} items.")) { return false; }
        DoFocusSelection(); return true;
    }
    UFUNCTION()
    bool Request_CycleFilter()
    {
        if (_Filter == "All") { return Request_Filter("Equipment"); }
        if (_Filter == "Equipment") { return Request_Filter("Supplies"); }
        return Request_Filter("All");
    }
    UFUNCTION()
    bool Request_ToggleNavigationRegion()
    {
        if (!DoReady() || !_Open || !_Session.Get_IsInputActive()) { return false; }
        if (_NavigationRegion == "Inventory")
        {
            const FString Key = DoFirstEquippedKey();
            return !Key.IsEmpty() && Request_Select(Key);
        }
        const FString Key = DoSelectVisible(_Items, _Filter, "");
        return !Key.IsEmpty() && Request_Select(Key);
    }
    UFUNCTION()
    bool Request_Navigate(int32 InHorizontal, int32 InVertical)
    {
        if (!DoReady() || !_Open || !_Session.Get_IsInputActive()
            || ((InHorizontal == 0) == (InVertical == 0))
            || Math::Abs(InHorizontal) > 1 || Math::Abs(InVertical) > 1) { return false; }
        if (_NavigationRegion == "Equipment")
        {
            const int32 Selected = DoFind(_Items, _SelectedKey);
            if (Selected < 0 || _Items[Selected].Slot.IsEmpty()) { return false; }
            const auto Slots = DoEquipmentOrder();
            int32 Current = -1;
            for (int32 Index = 0; Index < Slots.Num(); ++Index)
            { if (Slots[Index] == _Items[Selected].Slot) { Current = Index; break; } }
            if (Current < 0) { return false; }
            const int32 Row = Math::IntegerDivisionTrunc(Current, 2);
            const int32 Column = Current % 2;
            int32 Best = -1;
            int32 BestDistance = 100;
            for (int32 Position = 0; Position < Slots.Num(); ++Position)
            {
                if (Position == Current || DoSlotIndex(_Items, Slots[Position]) < 0) { continue; }
                const int32 OtherRow = Math::IntegerDivisionTrunc(Position, 2);
                const int32 OtherColumn = Position % 2;
                int32 Distance = 100;
                if (InHorizontal != 0 && OtherColumn == 1 - Column)
                { Distance = Math::Abs(OtherRow - Row); }
                else if (InVertical != 0 && OtherColumn == Column)
                {
                    Distance = InVertical > 0 ? (OtherRow - Row + 5) % 5 : (Row - OtherRow + 5) % 5;
                    if (Distance == 0) { Distance = 100; }
                }
                if (Distance < BestDistance) { Best = Position; BestDistance = Distance; }
            }
            if (Best < 0) { return false; }
            const int32 Item = DoSlotIndex(_Items, Slots[Best]);
            return Item >= 0 && Request_Select(_Items[Item].Key);
        }
        auto Keys = TArray<FString>();
        for (const auto Item : _Items) { if (DoVisible(Item, _Filter)) { Keys.Add(Item.Key); } }
        if (Keys.IsEmpty()) { return false; }
        int32 Index = 0;
        for (int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex)
        { if (Keys[KeyIndex] == _SelectedKey) { Index = KeyIndex; break; } }
        const int32 Candidate = Index + InHorizontal + InVertical * 6;
        if (Candidate < 0 || Candidate >= Keys.Num()) { return false; }
        if (InHorizontal != 0 && Math::IntegerDivisionTrunc(Candidate, 6)
            != Math::IntegerDivisionTrunc(Index, 6)) { return false; }
        return Request_Select(Keys[Candidate]);
    }
    UFUNCTION()
    bool Request_EquipSelected()
    {
        const int32 Index = DoFind(_Items, _SelectedKey);
        if (Index < 0 || !_Items[Index].Slot.IsEmpty()) { return false; }
        return Request_TryEquip(_SelectedKey, DoDefaultSlot(_Items[Index]));
    }
    UFUNCTION()
    bool Request_UnequipSelected()
    {
        const int32 Index = DoFind(_Items, _SelectedKey);
        if (Index < 0 || _Items[Index].Slot.IsEmpty()) { return false; }
        return Request_Unequip(_Items[Index].Slot);
    }
    UFUNCTION()
    bool Request_ToggleCompare()
    { return DoPublish(_Items, _Filter, _SelectedKey, !_Compare,
        _Compare ? "Comparison hidden." : "Comparison shown where equipment is available."); }
    UFUNCTION()
    bool Request_CycleRingTarget()
    {
        if (!DoReady()) { return false; }
        const int32 Selected = DoFind(_Items, _SelectedKey);
        if (Selected < 0 || _Items[Selected].Kind != "ring") { return false; }
        const FString Previous = _RingTarget;
        _RingTarget = _RingTarget == "Ring1" ? "Ring2" : "Ring1";
        if (!DoPublish(_Items, _Filter, _SelectedKey, _Compare, f"Ring destination: {_RingTarget}."))
        { _RingTarget = Previous; return false; }
        return true;
    }
    UFUNCTION()
    bool Request_PrimaryAction()
    {
        const int32 Selected = DoFind(_Items, _SelectedKey);
        if (!DoReady() || Selected < 0) { return false; }
        if (!_Items[Selected].Slot.IsEmpty()) { return Request_UnequipSelected(); }
        if (_Items[Selected].Kind != "supply") { return Request_EquipSelected(); }
        if (_Items[Selected].Quantity <= 0) { return false; }
        auto Candidate = _Items;
        const FString UsedName = Candidate[Selected].Name;
        --Candidate[Selected].Quantity;
        if (Candidate[Selected].Quantity == 0) { Candidate.RemoveAt(Selected); }
        if (!DoPublish(Candidate, _Filter, _SelectedKey, _Compare, f"Used {UsedName} in this equipment fixture.")) { return false; }
        _NavigationRegion = "Inventory"; ++_MutationCount; DoFocusSelection(); return true;
    }
    UFUNCTION() bool Request_ToggleRegion() { return Request_ToggleNavigationRegion(); }
    UFUNCTION()
    bool Request_ResetFixture()
    {
        const FString Previous = _RingTarget;
        _RingTarget = "Ring1";
        if (!DoPublish(DoSeeds(), "All", "veilcutter", true, "Equipment restored to its starting state."))
        { _RingTarget = Previous; return false; }
        _NavigationRegion = "Inventory"; ++_MutationCount; DoFocusSelection(); return true;
    }
    UFUNCTION() bool Request_Reset() { return Request_ResetFixture(); }
    UFUNCTION() bool Request_SetFilter(FString InFilter) { return Request_Filter(InFilter); }
    UFUNCTION()
    bool Request_Show(bool InShow)
    {
        if (!DoReady()) { return false; }
        if (_Open == InShow) { return true; }
        _Open = InShow;
        if (InShow) { SetVisibility(ESlateVisibility::Visible); ActivateWidget(); }
        else
        {
            _Session.Request_EndInput(false);
            DeactivateWidget(); SetVisibility(ESlateVisibility::Collapsed);
        }
        return _Open == InShow && DoReady();
    }
    private void DoFocusSelection()
    {
        if (!DoReady() || !_Open || _SelectedKey.IsEmpty()) { return; }
        const int32 Index = DoFind(_Items, _SelectedKey);
        if (Index < 0) { return; }
        if (_Items[Index].Slot.IsEmpty()) { _Session.TryFocusControl(f"items/{_SelectedKey}/card"); }
        else { _Session.TryFocusControl(f"equip-{_Items[Index].Slot.ToLower()}/card"); }
    }
    UFUNCTION()
    private void DoOnItemAction(const FString& InName, const FString& InKey)
    {
        if (InName == "SelectItem") { Request_Select(InKey); return; }
        for (const auto EquipmentSlot : DoSlots())
        { if (InName == f"Equip{EquipmentSlot}") { Request_TryEquip(InKey, EquipmentSlot); return; } }
    }
    UFUNCTION()
    private void DoOnAction(const FString& InName)
    {
        if (InName == "FilterAll") { Request_Filter("All"); }
        else if (InName == "FilterEquipment") { Request_Filter("Equipment"); }
        else if (InName == "FilterSupplies") { Request_Filter("Supplies"); }
        else if (InName == "EquipSelected") { Request_EquipSelected(); }
        else if (InName == "UnequipSelected") { Request_UnequipSelected(); }
        else if (InName == "PrimaryAction") { Request_PrimaryAction(); }
        else if (InName == "CycleRingTarget") { Request_CycleRingTarget(); }
        else if (InName == "ToggleRegion") { Request_ToggleNavigationRegion(); }
        else if (InName == "ToggleCompare") { Request_ToggleCompare(); }
        else if (InName == "ResetFixture") { Request_ResetFixture(); }
        else if (InName == "Close") { Request_Show(false); }
        else if (InName == "Reload") { Request_Reload(); }
    }
    UFUNCTION()
    void Request_Shutdown()
    { if (_Retired) { return; } _Open = false; _Retired = true;
      if (ck::IsValid(_Session)) { _Session.Request_EndInput(false); _Session.Request_Release(); }
      if (ck::IsValid(_Surface)) { _Surface.ReleaseView(); } RemoveFromParent(); }
    UFUNCTION(BlueprintOverride)
    void OnActivated()
    {
        if (!DoReady() || !_Open) { return; }
        if (!_Session.TryBeginInput()) { DoRetire("Character screen input admission failed"); return; }
        DoFocusSelection();
    }
    UFUNCTION(BlueprintOverride)
    void OnDeactivated()
    { if (ck::IsValid(_Session)) { _Session.Request_EndInput(); } }
    UFUNCTION(BlueprintOverride)
    FEventReply OnPreviewKeyDown(FGeometry InGeometry, FKeyEvent InEvent)
    {
        if (!DoReady() || !_Open || !_Session.Get_IsInputActive()
            || int32(InEvent.GetUserIndex()) != _Session.Get_SlateUserIndex()) { return FEventReply::Unhandled(); }
        const auto Key = InEvent.GetKey();
        if (Key == EKeys::Escape || Key == EKeys::I)
        { if (!InEvent.IsRepeat()) { Request_Show(false); } return FEventReply::Handled(); }
        if (Key == EKeys::E || Key == EKeys::Gamepad_FaceButton_Bottom)
        {
            if (!InEvent.IsRepeat())
            {
                Request_PrimaryAction();
            }
            return FEventReply::Handled();
        }
        if (Key == EKeys::LeftShift || Key == EKeys::RightShift)
        { if (!InEvent.IsRepeat()) { Request_ToggleCompare(); } return FEventReply::Handled(); }
        if (Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder)
        { if (!InEvent.IsRepeat()) { Request_ToggleNavigationRegion(); } return FEventReply::Handled(); }
        if (Key == EKeys::R)
        { if (!InEvent.IsRepeat()) { Request_CycleRingTarget(); } return FEventReply::Handled(); }
        int32 Horizontal = 0; int32 Vertical = 0;
        if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left) { Horizontal = -1; }
        else if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right) { Horizontal = 1; }
        else if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up) { Vertical = -1; }
        else if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down) { Vertical = 1; }
        if (Horizontal == 0 && Vertical == 0) { return FEventReply::Unhandled(); }
        if (!InEvent.IsRepeat()) { Request_Navigate(Horizontal, Vertical); }
        return FEventReply::Handled();
    }
    UFUNCTION(BlueprintOverride) void Destruct() { Request_Shutdown(); }
    UFUNCTION(BlueprintPure) UCk_UI_AuthoredSession_UE Get_Session() const { return _Session; }
    UFUNCTION(BlueprintPure) FString Get_StartupError() const { return _StartupError; }
    UFUNCTION(BlueprintPure) FString Get_SourceError() const { return _SourceError; }
    UFUNCTION(BlueprintPure) FString Get_SelectedKey() const { return _SelectedKey; }
    UFUNCTION(BlueprintPure) bool Get_IsOpen() const { return _Open && DoReady(); }
    UFUNCTION(BlueprintPure) FString Get_EquippedKey(FString InSlot) const
    { const int32 Index = DoSlotIndex(_Items, InSlot); return Index >= 0 ? _Items[Index].Key : ""; }
    UFUNCTION(BlueprintPure) int32 Get_MutationCount() const { return _MutationCount; }
}

class ACk_CharacterEquipment_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY() private UCk_CharacterEquipment_Widget _Screen;
    private bool _PreviousCursor = false;
    private bool _CursorCaptured = false;
    void Request_StartGym() override
    {
        if (ck::IsValid(_Screen)) { _Screen.Request_Shutdown(); }
        _Screen = Cast<UCk_CharacterEquipment_Widget>(WidgetBlueprint::CreateWidget(UCk_CharacterEquipment_Widget, this));
        if (ck::IsValid(_Screen) && _Screen.TryInitializeScreen())
        {
            if (!_CursorCaptured) { _PreviousCursor = bShowMouseCursor; _CursorCaptured = true; }
            bShowMouseCursor = true; _Screen.AddToViewport(); _Screen.ActivateWidget();
        }
    }
    UFUNCTION(BlueprintPure) UCk_CharacterEquipment_Widget Get_Screen() const { return _Screen; }
    UFUNCTION(BlueprintPure) UCk_CharacterEquipment_Widget Get_Hud() const { return _Screen; }
    FString Get_ControlPanelTitle() override { return "CHARACTER & EQUIPMENT"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        const bool Ready = ck::IsValid(_Screen) && ck::IsValid(_Screen.Get_Session()) && _Screen.Get_Session().Get_IsUsable();
        Rows.Add(CkGym_Control::Header("CHARACTER / EQUIPMENT FIXTURE"));
        Rows.Add(CkGym_Control::Action(EKeys::One, "1", "Open or close screen", true));
        Rows.Add(CkGym_Control::Action(EKeys::Two, "2", "Equip selected item", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Three, "3", "Unequip selected item", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Four, "4", "Cycle inventory filter", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Five, "5", "Toggle comparison", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Six, "6", "Reset fixture", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Seven, "7", "Reload HTML / CSS", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Eight, "8", "Reject invalid drop", Ready));
        if (ck::IsValid(_Screen))
        { const auto Error = _Screen.Get_SourceError(); Rows.Add(CkGym_Control::Status("Source", Error.IsEmpty() ? "accepted" : Error, !Error.IsEmpty())); }
        Rows.Add(CkGym_Control::Status("Gyms", "Tab: selector  H: panel")); return Rows;
    }
    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (InRowIndex == 1)
        {
            if (ck::IsValid(_Screen) && ck::IsValid(_Screen.Get_Session()) && _Screen.Get_Session().Get_IsUsable())
            { _Screen.Request_Show(!_Screen.Get_IsOpen()); }
            else { Request_StartGym(); }
            return;
        }
        if (!ck::IsValid(_Screen)) { return; }
        if (InRowIndex == 2) { _Screen.Request_EquipSelected(); }
        else if (InRowIndex == 3) { _Screen.Request_UnequipSelected(); }
        else if (InRowIndex == 4) { _Screen.Request_CycleFilter(); }
        else if (InRowIndex == 5) { _Screen.Request_ToggleCompare(); }
        else if (InRowIndex == 6) { _Screen.Request_ResetFixture(); }
        else if (InRowIndex == 7) { _Screen.Request_Reload(); }
        else if (InRowIndex == 8) { _Screen.Request_TryEquip("red-potion", "Head"); }
    }
    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (ck::IsValid(_Screen)) { _Screen.Request_Shutdown(); } _Screen = nullptr;
        if (_CursorCaptured) { bShowMouseCursor = _PreviousCursor; _CursorCaptured = false; }
    }
}

class ACk_CharacterEquipment_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_CharacterEquipment_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACkGym_ControlPanelHUD;
}
