// Language=angelscript

struct FCk_TacticalItem
{
    FString Key;
    FString Name;
    FString Kind;
    FString Description;
    FString Weight;
    FString Condition;
    FString Caliber;
    FString Container;
    int32 X = 0;
    int32 Y = 0;
    int32 W = 1;
    int32 H = 1;
    int32 Icon = 0;
    int32 Quantity = 1;
    bool Rotated = false;
}

struct FCk_TacticalCrop
{
    float32 Left = 0.0f;
    float32 Top = 0.0f;
    float32 Right = 1.0f;
    float32 Bottom = 1.0f;
}

class UCk_TacticalInventory_Widget : UCk_ActivatableWidget_UE
{
    default bIsFocusable = true;
    default bAutoActivate = false;
    UPROPERTY() private UCk_UI_AuthoredSession_UE _Session;
    UPROPERTY() private UCk_UI_AuthoredSurface_UE _Surface;
    UPROPERTY() private UTexture2D _Backdrop;
    UPROPERTY() private UTexture2D _Atlas;
    UPROPERTY() private UTexture2D _Operator;
    private TArray<FCk_TacticalItem> _Items;
    private FString _SelectedKey = "m4a1";
    private FString _Status = "Select an item or drag it between containers.";
    private FString _StartupError;
    private FString _SourceError;
    private bool _CaseOpen = true;
    private bool _Open = true;
    private bool _Retired = false;
    private int32 _MutationCount = 0;

    private FString DoDirectory() const
    { return FPaths::CombinePaths(FPaths::ProjectDir(), "Plugins/CkTests/Resources/UiAuthoring/TacticalInventoryProof"); }
    private FString DoFontDirectory() const
    { return FPaths::CombinePaths(FPaths::ProjectDir(), "Plugins/CkTests/Resources/UiAuthoring/DarkFantasyHudProof/Fonts"); }
    private bool DoReady() const { return !_Retired && ck::IsValid(_Session) && _Session.Get_IsUsable(); }
    private int32 DoFind(const TArray<FCk_TacticalItem>& InItems, const FString& InKey) const
    { for (int32 Index = 0; Index < InItems.Num(); ++Index) { if (InItems[Index].Key == InKey) { return Index; } } return -1; }
    private int32 DoColumns(const FString& InContainer) const
    { if (InContainer == "Stash") { return 10; } if (InContainer == "Pack") { return 6; } if (InContainer == "Case") { return 4; } return 0; }
    private int32 DoRows(const FString& InContainer) const
    { if (InContainer == "Stash") { return 14; } if (InContainer == "Pack") { return 6; } if (InContainer == "Case") { return 4; } return 0; }
    private int32 DoWidth(const FCk_TacticalItem& InItem) const { return InItem.Rotated ? InItem.H : InItem.W; }
    private int32 DoHeight(const FCk_TacticalItem& InItem) const { return InItem.Rotated ? InItem.W : InItem.H; }
    private FCk_TacticalCrop DoCropValues(float32 InLeft, float32 InTop, float32 InRight, float32 InBottom) const
    {
        FCk_TacticalCrop Crop; Crop.Left = InLeft; Crop.Top = InTop;
        Crop.Right = InRight; Crop.Bottom = InBottom; return Crop;
    }
    private FCk_TacticalCrop DoCrop(int32 InIcon) const
    {
        if (InIcon == 0) { return DoCropValues(0.20f, 0.43f, 0.83f, 0.80f); }
        if (InIcon == 1) { return DoCropValues(0.19f, 0.45f, 0.91f, 0.71f); }
        if (InIcon == 2) { return DoCropValues(0.10f, 0.03f, 0.98f, 0.98f); }
        if (InIcon == 3) { return DoCropValues(0.06f, 0.05f, 0.98f, 0.99f); }
        if (InIcon == 4) { return DoCropValues(0.06f, 0.15f, 0.97f, 0.99f); }
        if (InIcon == 5) { return DoCropValues(0.05f, 0.20f, 0.97f, 0.97f); }
        if (InIcon == 6) { return DoCropValues(0.02f, 0.13f, 0.98f, 0.94f); }
        if (InIcon == 7) { return DoCropValues(0.20f, 0.09f, 0.91f, 0.99f); }
        if (InIcon == 8) { return DoCropValues(0.28f, 0.05f, 0.76f, 0.99f); }
        if (InIcon == 9) { return DoCropValues(0.17f, 0.04f, 0.87f, 0.93f); }
        if (InIcon == 10) { return DoCropValues(0.12f, 0.13f, 0.98f, 0.98f); }
        if (InIcon == 11) { return DoCropValues(0.10f, 0.10f, 0.98f, 0.90f); }
        if (InIcon == 12) { return DoCropValues(0.12f, 0.01f, 0.94f, 0.86f); }
        if (InIcon == 13) { return DoCropValues(0.47f, 0.06f, 0.84f, 0.91f); }
        if (InIcon == 14) { return DoCropValues(0.04f, 0.05f, 0.98f, 0.89f); }
        if (InIcon == 15) { return DoCropValues(0.03f, 0.06f, 0.98f, 0.94f); }
        if (InIcon == 16) { return DoCropValues(0.10f, 0.01f, 0.96f, 0.96f); }
        if (InIcon == 17) { return DoCropValues(0.04f, 0.06f, 0.99f, 0.99f); }
        if (InIcon == 18) { return DoCropValues(0.23f, 0.01f, 0.94f, 0.97f); }
        if (InIcon == 19) { return DoCropValues(0.08f, 0.25f, 0.97f, 0.66f); }
        FCk_TacticalCrop Crop; return Crop;
    }
    private FCk_TacticalItem DoItem(const FString& InKey, const FString& InName, const FString& InKind,
        const FString& InDescription, const FString& InWeight, const FString& InCondition,
        const FString& InCaliber, const FString& InContainer, int32 InX, int32 InY,
        int32 InW, int32 InH, int32 InIcon, int32 InQuantity = 1) const
    {
        FCk_TacticalItem Item; Item.Key = InKey; Item.Name = InName; Item.Kind = InKind;
        Item.Description = InDescription; Item.Weight = InWeight; Item.Condition = InCondition;
        Item.Caliber = InCaliber; Item.Container = InContainer; Item.X = InX; Item.Y = InY;
        Item.W = InW; Item.H = InH; Item.Icon = InIcon; Item.Quantity = InQuantity; return Item;
    }
    private TArray<FCk_TacticalItem> DoSeeds() const
    {
        auto Items = TArray<FCk_TacticalItem>();
        Items.Add(DoItem("m4a1", "M4A1 CARBINE", "RIFLE", "Short receiver, fitted handguard and reliable 5.56 platform.", "3.1 KG", "86 / 100", "5.56 X 45", "Stash", 0, 0, 4, 2, 0));
        Items.Add(DoItem("sv98", "SV-98 MARKSMAN", "RIFLE", "Long range rifle with a worn laminate stock.", "5.7 KG", "71 / 100", "7.62 X 54R", "Stash", 4, 0, 5, 2, 1));
        Items.Add(DoItem("armor", "FORT ARMOR", "ARMOR", "Heavy plate carrier, repaired after a hard extraction.", "8.8 KG", "42 / 65", "--", "Stash", 0, 6, 3, 3, 2));
        Items.Add(DoItem("helmet", "FAST MT HELMET", "HELMET", "High cut shell with modular side rails.", "1.4 KG", "49 / 52", "--", "Stash", 3, 2, 2, 2, 3));
        Items.Add(DoItem("ifak", "IFAK FIELD KIT", "MEDICAL", "Compact trauma kit for field treatment.", "0.5 KG", "SEALED", "--", "Stash", 5, 2, 2, 2, 4));
        Items.Add(DoItem("ammo556", "M855A1", "AMMUNITION", "Penetrator rounds packed for quick loading.", "0.4 KG", "NEW", "5.56 X 45", "Stash", 7, 2, 1, 1, 5, 48));
        Items.Add(DoItem("docs", "DOCUMENT CASE", "CONTAINER", "Small secured case with four by four internal cells.", "0.7 KG", "LOCKED", "--", "Stash", 8, 2, 2, 2, 6));
        Items.Add(DoItem("mag556", "STANAG MAG", "MAGAZINE", "Thirty round steel magazine.", "0.3 KG", "30 / 30", "5.56 X 45", "Stash", 3, 4, 1, 2, 7));
        Items.Add(DoItem("water", "BOTTLED WATER", "SUPPLY", "Clean water for a long raid.", "0.6 KG", "FULL", "--", "Stash", 4, 4, 1, 2, 8));
        Items.Add(DoItem("grenade", "F-1 GRENADE", "EXPLOSIVE", "Fragmentation grenade, safety pin intact.", "0.6 KG", "READY", "--", "Stash", 5, 4, 1, 1, 9));
        Items.Add(DoItem("pistol", "GLOCK 17", "PISTOL", "Sidearm with a compact sight.", "0.9 KG", "92 / 100", "9 X 19", "Pack", 0, 0, 2, 2, 10));
        Items.Add(DoItem("rig", "BANK ROBBER RIG", "RIG", "Light chest rig for spare magazines.", "1.2 KG", "54 / 60", "--", "Pack", 2, 0, 3, 3, 12));
        Items.Add(DoItem("injector", "MORPHINE", "MEDICAL", "Single dose pain relief injector.", "0.1 KG", "SEALED", "--", "Pack", 0, 2, 1, 1, 11));
        Items.Add(DoItem("mag9", "G17 MAG", "MAGAZINE", "Seventeen round polymer magazine.", "0.2 KG", "17 / 17", "9 X 19", "Pack", 0, 3, 1, 2, 13));
        Items.Add(DoItem("ration", "FIELD RATION", "SUPPLY", "Dense meal bar for extended patrols.", "0.4 KG", "NEW", "--", "Pack", 4, 3, 2, 1, 14));
        Items.Add(DoItem("keycard", "LAB ACCESS CARD", "KEY", "Scuffed card with a still active stripe.", "0.1 KG", "ACTIVE", "--", "Case", 0, 0, 1, 1, 15));
        Items.Add(DoItem("looseammo", "BP ROUNDS", "AMMUNITION", "Loose armor piercing rounds.", "0.3 KG", "NEW", "7.62 X 39", "Case", 1, 0, 1, 1, 16, 32));
        Items.Add(DoItem("intel", "INTELLIGENCE", "VALUABLE", "Recovered field documents.", "0.2 KG", "PRISTINE", "--", "Case", 2, 0, 2, 2, 17));
        Items.Add(DoItem("m4a1-reserve", "M4A1 RESERVE", "RIFLE", "Clean spare carbine held for the next deployment.", "3.0 KG", "93 / 100", "5.56 X 45", "Stash", 0, 11, 4, 2, 0));
        Items.Add(DoItem("sv98-reserve", "SV-98 RESERVE", "RIFLE", "Second long rifle with a freshly serviced action.", "5.6 KG", "88 / 100", "7.62 X 54R", "Stash", 5, 12, 5, 2, 1));
        Items.Add(DoItem("armor-reserve", "FORT RESERVE", "ARMOR", "Stored carrier with intact replacement plates.", "9.0 KG", "63 / 65", "--", "Stash", 7, 9, 3, 3, 2));
        Items.Add(DoItem("ifak-reserve", "IFAK RESERVE", "MEDICAL", "Sealed trauma kit for a backup loadout.", "0.5 KG", "SEALED", "--", "Stash", 3, 6, 2, 2, 4));
        Items.Add(DoItem("mag556-reserve", "STANAG RESERVE", "MAGAZINE", "Loaded thirty round steel magazine.", "0.3 KG", "30 / 30", "5.56 X 45", "Stash", 6, 4, 1, 2, 7));
        Items.Add(DoItem("ammo556-reserve", "M855A1 RESERVE", "AMMUNITION", "Extra penetrator rounds for the carbine.", "0.5 KG", "NEW", "5.56 X 45", "Stash", 7, 4, 1, 1, 5, 60));
        Items.Add(DoItem("grenade-reserve", "F-1 RESERVE", "EXPLOSIVE", "Spare fragmentation grenade, pin secured.", "0.6 KG", "READY", "--", "Stash", 8, 4, 1, 1, 9));
        Items.Add(DoItem("grenade-second", "F-1 SECOND", "EXPLOSIVE", "Additional fragmentation grenade.", "0.6 KG", "READY", "--", "Stash", 9, 4, 1, 1, 9));
        Items.Add(DoItem("water-reserve", "WATER RESERVE", "SUPPLY", "Fresh bottle held for a long raid.", "0.6 KG", "FULL", "--", "Stash", 6, 6, 1, 2, 8));
        Items.Add(DoItem("headset-reserve", "COMTAC HEADSET", "HEADSET", "Electronic hearing protection with working comms.", "0.4 KG", "92 / 100", "--", "Stash", 7, 6, 2, 2, 18));
        Items.Add(DoItem("ammo-second", "BP RESERVE", "AMMUNITION", "Packed armor piercing rounds for the next raid.", "0.4 KG", "NEW", "7.62 X 39", "Stash", 9, 6, 1, 1, 16, 40));
        Items.Add(DoItem("injector-reserve", "MORPHINE RESERVE", "MEDICAL", "Sealed spare pain relief injector.", "0.1 KG", "SEALED", "--", "Stash", 9, 7, 1, 1, 11));
        return Items;
    }
    private bool DoFits(const TArray<FCk_TacticalItem>& InItems, int32 InIndex) const
    {
        const auto Item = InItems[InIndex];
        const int32 Columns = DoColumns(Item.Container); const int32 Rows = DoRows(Item.Container);
        const int32 W = DoWidth(Item); const int32 H = DoHeight(Item);
        if (Columns <= 0 || Rows <= 0 || W <= 0 || H <= 0 || Item.X < 0 || Item.Y < 0
            || Item.X > Columns - W || Item.Y > Rows - H) { return false; }
        if (Item.Key == "docs" && Item.Container == "Case") { return false; }
        for (int32 Index = 0; Index < InItems.Num(); ++Index)
        {
            if (Index == InIndex || InItems[Index].Container != Item.Container) { continue; }
            const auto Other = InItems[Index];
            if (Item.X < Other.X + DoWidth(Other) && Other.X < Item.X + W
                && Item.Y < Other.Y + DoHeight(Other) && Other.Y < Item.Y + H) { return false; }
        }
        return true;
    }
    private bool DoFirstFit(const TArray<FCk_TacticalItem>& InItems, int32 InIndex,
        const FString& InDestination, int32& OutX, int32& OutY) const
    {
        if (InIndex < 0 || InIndex >= InItems.Num() || DoColumns(InDestination) <= 0
            || InItems[InIndex].Container == "Case" || InDestination == "Case") { return false; }
        auto Candidate = InItems;
        Candidate[InIndex].Container = InDestination;
        for (int32 Y = 0; Y < DoRows(InDestination); ++Y)
        {
            for (int32 X = 0; X < DoColumns(InDestination); ++X)
            {
                Candidate[InIndex].X = X; Candidate[InIndex].Y = Y;
                if (DoFits(Candidate, InIndex)) { OutX = X; OutY = Y; return true; }
            }
        }
        return false;
    }
    private FCk_UI_AuthoredFieldValue DoTextField(const FString& InName, const FString& InValue) const
    { auto Field = FCk_UI_AuthoredFieldValue(InName, ECk_UI_AuthoredFieldKind::Text); Field.Set_Text(FText::FromString(InValue)); return Field; }
    private FCk_UI_AuthoredFieldValue DoNumberField(const FString& InName, float32 InValue) const
    { auto Field = FCk_UI_AuthoredFieldValue(InName, ECk_UI_AuthoredFieldKind::Number); Field.Set_Number(InValue); return Field; }
    private FCk_UI_AuthoredFieldValue DoBoolField(const FString& InName, bool InValue) const
    { auto Field = FCk_UI_AuthoredFieldValue(InName, ECk_UI_AuthoredFieldKind::Bool); Field.Set_Bool(InValue); return Field; }
    private TArray<FCk_UI_AuthoredFieldSchema> DoSchema() const
    {
        auto Schema = TArray<FCk_UI_AuthoredFieldSchema>();
        Schema.Add(FCk_UI_AuthoredFieldSchema("name", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("kind", ECk_UI_AuthoredFieldKind::Text));
        auto NumberNames = TArray<FString>();
        NumberNames.Add("x"); NumberNames.Add("y"); NumberNames.Add("w");
        NumberNames.Add("h"); NumberNames.Add("icon"); NumberNames.Add("quantity");
        NumberNames.Add("icon-left"); NumberNames.Add("icon-top");
        NumberNames.Add("icon-right"); NumberNames.Add("icon-bottom");
        for (const auto FieldName : NumberNames)
        { Schema.Add(FCk_UI_AuthoredFieldSchema(FieldName, ECk_UI_AuthoredFieldKind::Number)); }
        auto BoolNames = TArray<FString>(); BoolNames.Add("rotated"); BoolNames.Add("selected"); BoolNames.Add("enabled");
        for (const auto FieldName : BoolNames)
        { Schema.Add(FCk_UI_AuthoredFieldSchema(FieldName, ECk_UI_AuthoredFieldKind::Bool)); }
        return Schema;
    }
    private TArray<FCk_UI_AuthoredRecord> DoRecords(const TArray<FCk_TacticalItem>& InItems,
        const FString& InContainer, const FString& InSelected) const
    {
        auto Records = TArray<FCk_UI_AuthoredRecord>();
        for (const auto Item : InItems)
        {
            if (Item.Container != InContainer) { continue; }
            auto Fields = TArray<FCk_UI_AuthoredFieldValue>();
            Fields.Add(DoTextField("name", Item.Name)); Fields.Add(DoTextField("kind", Item.Kind));
            Fields.Add(DoNumberField("x", float32(Item.X))); Fields.Add(DoNumberField("y", float32(Item.Y)));
            Fields.Add(DoNumberField("w", float32(Item.W))); Fields.Add(DoNumberField("h", float32(Item.H)));
            Fields.Add(DoNumberField("icon", float32(Item.Icon)));
            Fields.Add(DoNumberField("quantity", float32(Item.Quantity)));
            const auto Crop = DoCrop(Item.Icon);
            Fields.Add(DoNumberField("icon-left", Crop.Left));
            Fields.Add(DoNumberField("icon-top", Crop.Top));
            Fields.Add(DoNumberField("icon-right", Crop.Right));
            Fields.Add(DoNumberField("icon-bottom", Crop.Bottom));
            Fields.Add(DoBoolField("rotated", Item.Rotated));
            Fields.Add(DoBoolField("selected", Item.Key == InSelected));
            Fields.Add(DoBoolField("enabled", true));
            auto Record = FCk_UI_AuthoredRecord(Item.Key); Record.Set_Fields(Fields); Records.Add(Record);
        }
        return Records;
    }
    private void DoErrors(const TArray<FString>& InErrors)
    { _SourceError = ""; for (const auto Error : InErrors) { _SourceError = _SourceError.IsEmpty() ? Error : f"{_SourceError}; {Error}"; } }
    private void DoRetire(const FString& InError)
    { _SourceError = InError; Request_Shutdown(); }
    private bool DoText(const FString& InName, const FString& InValue)
    { return _Session.TrySetText(InName, FText::FromString(InValue)); }
    private int32 DoCount(const TArray<FCk_TacticalItem>& InItems, const FString& InContainer) const
    { int32 Count = 0; for (const auto Item : InItems) { if (Item.Container == InContainer) { ++Count; } } return Count; }
    private bool DoScalars(const TArray<FCk_TacticalItem>& InItems, const FString& InSelected,
        bool InCaseOpen, const FString& InStatus)
    {
        const int32 Index = DoFind(InItems, InSelected);
        if (Index < 0) { return false; }
        const auto Item = InItems[Index];
        const auto Crop = DoCrop(Item.Icon);
        const int32 StashCount = DoCount(InItems, "Stash");
        const int32 PackCount = DoCount(InItems, "Pack");
        const int32 CaseCount = DoCount(InItems, "Case");
        auto RotatedItems = InItems;
        RotatedItems[Index].Rotated = !RotatedItems[Index].Rotated;
        const bool CanRotate = Item.W != Item.H && (InCaseOpen || Item.Container != "Case")
            && DoFits(RotatedItems, Index);
        const FString TransferDestination = Item.Container == "Stash" ? "Pack" : "Stash";
        int32 TransferX = -1; int32 TransferY = -1;
        const bool CanQuickTransfer = (Item.Container == "Stash" || Item.Container == "Pack")
            && DoFirstFit(InItems, Index, TransferDestination, TransferX, TransferY);
        return DoText("SelectedItemKey", InSelected)
            && DoText("SelectedName", Item.Name) && DoText("SelectedKind", Item.Kind)
            && DoText("SelectedDescription", Item.Description) && DoText("SelectedWeight", Item.Weight)
            && DoText("SelectedCondition", Item.Condition) && DoText("SelectedCaliber", Item.Caliber)
            && DoText("SelectedFootprint", f"{DoWidth(Item)} X {DoHeight(Item)} CELLS")
            && DoText("SelectedContainer", Item.Container)
            && DoText("SelectedQuantity", f"{Item.Quantity}") && DoText("Status", InStatus)
            && DoText("StashCount", f"{StashCount}")
            && DoText("PackCount", f"{PackCount}")
            && DoText("CaseCount", f"{CaseCount}")
            && _Session.TrySetNumber("SelectedIcon", float32(Item.Icon))
            && _Session.TrySetNumber("SelectedIconLeft", Crop.Left)
            && _Session.TrySetNumber("SelectedIconTop", Crop.Top)
            && _Session.TrySetNumber("SelectedIconRight", Crop.Right)
            && _Session.TrySetNumber("SelectedIconBottom", Crop.Bottom)
            && _Session.TrySetBool("CaseOpen", InCaseOpen)
            && _Session.TrySetBool("HasSelection", true)
            && _Session.TrySetBool("CanRotate", CanRotate)
            && _Session.TrySetBool("CanOpenCase", Item.Key == "docs" && !InCaseOpen)
            && _Session.TrySetBool("CanCloseCase", InCaseOpen)
            && _Session.TrySetBool("CanQuickTransfer", CanQuickTransfer);
    }
    private bool DoPublish(const TArray<FCk_TacticalItem>& InItems, const FString& InSelected,
        bool InCaseOpen, const FString& InStatus)
    {
        if (!DoReady() || DoFind(InItems, InSelected) < 0) { return false; }
        auto Errors = TArray<FString>();
        if (!_Session.TrySetRecords("Stash", DoRecords(InItems, "Stash", InSelected), Errors)
            || !_Session.TrySetRecords("Pack", DoRecords(InItems, "Pack", InSelected), Errors)
            || !_Session.TrySetRecords("Case", DoRecords(InItems, "Case", InSelected), Errors))
        { DoErrors(Errors); DoRetire("Inventory collection publication failed"); return false; }
        if (!DoScalars(InItems, InSelected, InCaseOpen, InStatus))
        { DoRetire("Inventory scalar publication failed"); return false; }
        _Items = InItems; _SelectedKey = InSelected; _CaseOpen = InCaseOpen; _Status = InStatus;
        return true;
    }
    private bool DoFill(UOverlaySlot InSlot)
    { if (!ck::IsValid(InSlot)) { return false; } InSlot.SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill); InSlot.SetVerticalAlignment(EVerticalAlignment::VAlign_Fill); return true; }
    UFUNCTION()
    bool TryInitializeScreen()
    {
        if (ck::IsValid(_Session)) { return false; }
        _Session = Cast<UCk_UI_AuthoredSession_UE>(NewObject(this, UCk_UI_AuthoredSession_UE));
        _Surface = Cast<UCk_UI_AuthoredSurface_UE>(ConstructWidget(UCk_UI_AuthoredSurface_UE, n"TacticalInventorySurface"));
        auto Scale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"TacticalInventoryScale"));
        auto Frame = Cast<USizeBox>(ConstructWidget(USizeBox, n"TacticalInventoryFrame"));
        auto Root = Cast<UOverlay>(ConstructWidget(UOverlay, n"TacticalInventoryRoot"));
        auto BackdropScale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"TacticalInventoryBackdropScale"));
        auto BackdropImage = Cast<UImage>(ConstructWidget(UImage, n"TacticalInventoryBackdrop"));
        _Backdrop = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "backdrop.png"));
        _Atlas = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "item-atlas.png"));
        _Operator = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "operator.png"));
        if (!ck::IsValid(_Session) || !ck::IsValid(_Surface) || !ck::IsValid(Scale) || !ck::IsValid(Frame)
            || !ck::IsValid(Root) || !ck::IsValid(BackdropScale) || !ck::IsValid(BackdropImage)
            || !ck::IsValid(_Backdrop) || !ck::IsValid(_Atlas) || !ck::IsValid(_Operator))
        { _StartupError = "Inventory screen or resource construction failed"; Request_Shutdown(); return false; }
        _Items = DoSeeds();
        for (int32 Index = 0; Index < _Items.Num(); ++Index)
        { if (!DoFits(_Items, Index)) { _StartupError = "Inventory seed placement invalid"; Request_Shutdown(); return false; } }
        auto Collections = TArray<FCk_UI_AuthoredCollection>();
        auto Containers = TArray<FString>(); Containers.Add("Stash"); Containers.Add("Pack"); Containers.Add("Case");
        for (const auto Container : Containers)
        { auto Collection = FCk_UI_AuthoredCollection(Container); Collection.Set_Schema(DoSchema()); Collection.Set_Records(DoRecords(_Items, Container, _SelectedKey)); Collections.Add(Collection); }
        auto Texts = TArray<FCk_UI_AuthoredText>();
        auto TextNames = TArray<FString>();
        TextNames.Add("SelectedItemKey"); TextNames.Add("SelectedName"); TextNames.Add("SelectedKind");
        TextNames.Add("SelectedDescription"); TextNames.Add("SelectedWeight"); TextNames.Add("SelectedCondition");
        TextNames.Add("SelectedCaliber"); TextNames.Add("SelectedFootprint"); TextNames.Add("SelectedContainer");
        TextNames.Add("SelectedQuantity"); TextNames.Add("Status"); TextNames.Add("StashCount");
        TextNames.Add("PackCount"); TextNames.Add("CaseCount");
        for (const auto FieldName : TextNames)
        { Texts.Add(FCk_UI_AuthoredText(FieldName, FText::FromString(""))); }
        auto Numbers = TArray<FCk_UI_AuthoredNumber>(); Numbers.Add(FCk_UI_AuthoredNumber("SelectedIcon", 0.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("SelectedIconLeft", 0.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("SelectedIconTop", 0.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("SelectedIconRight", 1.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("SelectedIconBottom", 1.0f));
        for (int32 Icon = 0; Icon < 20; ++Icon)
        {
            const auto Crop = DoCrop(Icon);
            Numbers.Add(FCk_UI_AuthoredNumber(f"GearIcon{Icon}", float32(Icon)));
            Numbers.Add(FCk_UI_AuthoredNumber(f"GearIcon{Icon}Left", Crop.Left));
            Numbers.Add(FCk_UI_AuthoredNumber(f"GearIcon{Icon}Top", Crop.Top));
            Numbers.Add(FCk_UI_AuthoredNumber(f"GearIcon{Icon}Right", Crop.Right));
            Numbers.Add(FCk_UI_AuthoredNumber(f"GearIcon{Icon}Bottom", Crop.Bottom));
        }
        auto Bools = TArray<FCk_UI_AuthoredBool>();
        auto BoolNames = TArray<FString>(); BoolNames.Add("CaseOpen"); BoolNames.Add("HasSelection");
        BoolNames.Add("CanRotate"); BoolNames.Add("CanOpenCase"); BoolNames.Add("CanCloseCase");
        BoolNames.Add("CanQuickTransfer");
        for (const auto FieldName : BoolNames)
        { Bools.Add(FCk_UI_AuthoredBool(FieldName, false)); }
        auto Images = TArray<FCk_UI_AuthoredImage>();
        auto Atlas = FCk_UI_AuthoredImage("InventoryAtlas"); Atlas.Set_Texture(_Atlas); Images.Add(Atlas);
        auto Operator = FCk_UI_AuthoredImage("OperatorPortrait"); Operator.Set_Texture(_Operator); Images.Add(Operator);
        auto Actions = TArray<FString>();
        Actions.Add("RotateSelected"); Actions.Add("OpenCase"); Actions.Add("CloseCase");
        Actions.Add("QuickTransfer"); Actions.Add("ResetFixture"); Actions.Add("Close"); Actions.Add("Reload");
        auto ItemActions = TArray<FString>();
        ItemActions.Add("SelectItem"); ItemActions.Add("OnStashMove");
        ItemActions.Add("OnPackMove"); ItemActions.Add("OnCaseMove");
        auto Regions = TArray<FString>(); Regions.Add("main");
        const auto FontDir = DoFontDirectory();
        const auto Fallback = FPaths::CombinePaths(FPaths::EngineContentDir(), "Slate/Fonts/DroidSansFallback.ttf");
        auto Ui = FCk_UI_AuthoredFontFamily("tactical-ui", FPaths::CombinePaths(FontDir, "SourceSans3-Regular.ttf"));
        Ui.Set_BoldFile(FPaths::CombinePaths(FontDir, "SourceSans3-Bold.ttf"));
        Ui.Set_FallbackRegularFile(Fallback); Ui.Set_FallbackBoldFile(Fallback);
        auto Fonts = TArray<FCk_UI_AuthoredFontFamily>(); Fonts.Add(Ui);
        auto Tokens = TMap<FString, FString>();
        Tokens.Add("--ink-main", "#D9E2DF"); Tokens.Add("--ink-dim", "#8FA5A2");
        Tokens.Add("--ink-accent", "#E6B45D"); Tokens.Add("--ink-warning", "#D6865A");
        auto Config = FCk_UI_AuthoredSession_Config();
        Config.Set_InteractionOwner(this); Config.Set_Regions(Regions); Config.Set_Text(Texts);
        Config.Set_Number(Numbers); Config.Set_Bool(Bools); Config.Set_Collections(Collections);
        Config.Set_Images(Images); Config.Set_Actions(Actions); Config.Set_ItemActions(ItemActions);
        Config.Set_FontFamilies(Fonts); Config.Set_StyleTokens(Tokens);
        Config.Set_RegisterHudControls(true); Config.Set_RegisterItemControls(true);
        auto Errors = TArray<FString>();
        if (!_Session.TryInitialize(Config, Errors))
        { DoErrors(Errors); _StartupError = _SourceError; Request_Shutdown(); return false; }
        if (!DoScalars(_Items, _SelectedKey, _CaseOpen, _Status))
        { _StartupError = "Initial inventory bindings failed"; Request_Shutdown(); return false; }
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
        { _StartupError = "Inventory screen composition failed"; Request_Shutdown(); return false; }
        SetRootWidget(Root); return true;
    }
    UFUNCTION()
    bool Request_Reload()
    {
        if (!DoReady()) { return false; }
        FString Markup; FString Stylesheet;
        if (!FFileHelper::LoadFileToString(Markup, FPaths::CombinePaths(DoDirectory(), "TacticalInventoryProof.ui.html"))
            || !FFileHelper::LoadFileToString(Stylesheet, FPaths::CombinePaths(DoDirectory(), "TacticalInventoryProof.ui.css")))
        { _SourceError = "Inventory source files could not load"; return false; }
        auto Errors = TArray<FString>(); const bool Accepted = _Session.TryReload(Markup, Stylesheet, Errors);
        DoErrors(Errors); return Accepted;
    }
    UFUNCTION()
    bool Request_Select(FString InKey)
    {
        if (!DoReady()) { return false; }
        const int32 Index = DoFind(_Items, InKey);
        if (Index < 0 || (_Items[Index].Container == "Case" && !_CaseOpen)) { return false; }
        return DoPublish(_Items, InKey, _CaseOpen, f"{_Items[Index].Name} selected.");
    }
    UFUNCTION()
    bool Request_TryMove(FString InKey, FString InContainer, int32 InX, int32 InY, bool InRotated)
    {
        if (!DoReady() || DoColumns(InContainer) <= 0) { return false; }
        const int32 Index = DoFind(_Items, InKey); if (Index < 0) { return false; }
        if (_Items[Index].Container == "Case" && !_CaseOpen) { return false; }
        if (InContainer == "Case" && (!_CaseOpen || InKey == "docs")) { return false; }
        auto Candidate = _Items; Candidate[Index].Container = InContainer;
        Candidate[Index].X = InX; Candidate[Index].Y = InY; Candidate[Index].Rotated = InRotated;
        if (!DoFits(Candidate, Index)) { return false; }
        if (Candidate[Index].Container == _Items[Index].Container && Candidate[Index].X == _Items[Index].X
            && Candidate[Index].Y == _Items[Index].Y && Candidate[Index].Rotated == _Items[Index].Rotated) { return true; }
        if (!DoPublish(Candidate, InKey, _CaseOpen, f"Moved {Candidate[Index].Name} to {InContainer}.")) { return false; }
        ++_MutationCount; return true;
    }
    UFUNCTION()
    bool Request_RotateSelected()
    {
        if (!DoReady()) { return false; }
        const int32 Index = DoFind(_Items, _SelectedKey); if (Index < 0 || _Items[Index].W == _Items[Index].H) { return false; }
        return Request_TryMove(_SelectedKey, _Items[Index].Container, _Items[Index].X, _Items[Index].Y, !_Items[Index].Rotated);
    }
    UFUNCTION()
    bool Request_OpenCase()
    {
        if (!DoReady() || _CaseOpen || _SelectedKey != "docs") { return false; }
        if (!DoPublish(_Items, _SelectedKey, true, "Document case opened.")) { return false; }
        ++_MutationCount; return true;
    }
    UFUNCTION()
    bool Request_CloseCase()
    {
        if (!DoReady() || !_CaseOpen) { return false; }
        const int32 Selected = DoFind(_Items, _SelectedKey);
        const FString NextSelection = Selected >= 0 && _Items[Selected].Container == "Case" ? "docs" : _SelectedKey;
        if (!DoPublish(_Items, NextSelection, false, "Document case closed.")) { return false; }
        ++_MutationCount; return true;
    }
    UFUNCTION()
    bool Request_QuickTransfer()
    {
        if (!DoReady()) { return false; }
        const int32 Index = DoFind(_Items, _SelectedKey); if (Index < 0) { return false; }
        const FString Destination = _Items[Index].Container == "Stash" ? "Pack" : "Stash";
        int32 X = -1; int32 Y = -1;
        if (!DoFirstFit(_Items, Index, Destination, X, Y)) { return false; }
        return Request_TryMove(_SelectedKey, Destination, X, Y, _Items[Index].Rotated);
    }
    UFUNCTION()
    bool Request_ResetFixture()
    {
        if (!DoPublish(DoSeeds(), "m4a1", true, "Inventory restored to starting layout.")) { return false; }
        ++_MutationCount; return true;
    }
    private bool DoParseCoord(const FString& InPart, int32& OutValue) const
    {
        for (int32 Value = 0; Value <= 13; ++Value)
        { if (InPart == f"{Value}") { OutValue = Value; return true; } }
        return false;
    }
    private bool DoMoveProposal(const FString& InDestination, const FString& InProposal)
    {
        auto Parts = TArray<FString>(); InProposal.ParseIntoArray(Parts, "|", false);
        if (Parts.Num() != 4 || Parts[0].IsEmpty() || (Parts[3] != "0" && Parts[3] != "1")) { return false; }
        int32 X = -1; int32 Y = -1;
        if (!DoParseCoord(Parts[1], X) || !DoParseCoord(Parts[2], Y)) { return false; }
        return Request_TryMove(Parts[0], InDestination, X, Y, Parts[3] == "1");
    }
    UFUNCTION()
    private void DoOnItemAction(const FString& InName, const FString& InKey)
    {
        if (InName == "SelectItem") { Request_Select(InKey); }
        else if (InName == "OnStashMove") { DoMoveProposal("Stash", InKey); }
        else if (InName == "OnPackMove") { DoMoveProposal("Pack", InKey); }
        else if (InName == "OnCaseMove") { DoMoveProposal("Case", InKey); }
    }
    UFUNCTION()
    private void DoOnAction(const FString& InName)
    {
        if (InName == "RotateSelected") { Request_RotateSelected(); }
        else if (InName == "OpenCase") { Request_OpenCase(); }
        else if (InName == "CloseCase") { Request_CloseCase(); }
        else if (InName == "QuickTransfer") { Request_QuickTransfer(); }
        else if (InName == "ResetFixture") { Request_ResetFixture(); }
        else if (InName == "Reload") { Request_Reload(); }
        else if (InName == "Close") { Request_Show(false); }
    }
    UFUNCTION()
    bool Request_Show(bool InShow)
    {
        if (!DoReady()) { return false; }
        if (_Open == InShow) { return true; }
        _Open = InShow;
        if (InShow) { SetVisibility(ESlateVisibility::Visible); ActivateWidget(); }
        else { _Session.Request_EndInput(false); DeactivateWidget(); SetVisibility(ESlateVisibility::Collapsed); }
        return _Open == InShow && DoReady();
    }
    UFUNCTION()
    void Request_Shutdown()
    {
        if (_Retired) { return; } _Open = false; _Retired = true;
        if (ck::IsValid(_Session)) { _Session.Request_EndInput(false); _Session.Request_Release(); }
        if (ck::IsValid(_Surface)) { _Surface.ReleaseView(); } RemoveFromParent();
    }
    UFUNCTION(BlueprintOverride)
    void OnActivated()
    { if (DoReady() && _Open && !_Session.TryBeginInput()) { DoRetire("Inventory screen input admission failed"); } }
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
        if (Key == EKeys::O)
        { if (!InEvent.IsRepeat()) { Request_OpenCase(); } return FEventReply::Handled(); }
        if (Key == EKeys::E)
        { if (!InEvent.IsRepeat()) { Request_QuickTransfer(); } return FEventReply::Handled(); }
        return FEventReply::Unhandled();
    }
    UFUNCTION(BlueprintOverride)
    FEventReply OnKeyDown(FGeometry InGeometry, FKeyEvent InEvent)
    {
        if (!DoReady() || !_Open || !_Session.Get_IsInputActive()
            || int32(InEvent.GetUserIndex()) != _Session.Get_SlateUserIndex()) { return FEventReply::Unhandled(); }
        if (InEvent.GetKey() != EKeys::R) { return FEventReply::Unhandled(); }
        if (!InEvent.IsRepeat()) { Request_RotateSelected(); }
        return FEventReply::Handled();
    }
    UFUNCTION(BlueprintOverride) void Destruct() { Request_Shutdown(); }
    UFUNCTION(BlueprintPure) UCk_UI_AuthoredSession_UE Get_Session() const { return _Session; }
    UFUNCTION(BlueprintPure) FString Get_SelectedKey() const { return _SelectedKey; }
    UFUNCTION(BlueprintPure) FString Get_StartupError() const { return _StartupError; }
    UFUNCTION(BlueprintPure) FString Get_SourceError() const { return _SourceError; }
    UFUNCTION(BlueprintPure) bool Get_IsOpen() const { return _Open && DoReady(); }
    UFUNCTION(BlueprintPure) int32 Get_MutationCount() const { return _MutationCount; }
    UFUNCTION(BlueprintPure) FString Get_ItemContainer(FString InKey) const
    { const int32 Index = DoFind(_Items, InKey); return Index >= 0 ? _Items[Index].Container : ""; }
    UFUNCTION(BlueprintPure) int32 Get_ItemX(FString InKey) const
    { const int32 Index = DoFind(_Items, InKey); return Index >= 0 ? _Items[Index].X : -1; }
    UFUNCTION(BlueprintPure) int32 Get_ItemY(FString InKey) const
    { const int32 Index = DoFind(_Items, InKey); return Index >= 0 ? _Items[Index].Y : -1; }
    UFUNCTION(BlueprintPure) bool Get_ItemRotated(FString InKey) const
    { const int32 Index = DoFind(_Items, InKey); return Index >= 0 && _Items[Index].Rotated; }
}

class ACk_TacticalInventory_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY() private UCk_TacticalInventory_Widget _Screen;
    private bool _PreviousCursor = false;
    private bool _CursorCaptured = false;
    void Request_StartGym() override
    {
        if (ck::IsValid(_Screen)) { _Screen.Request_Shutdown(); }
        _Screen = Cast<UCk_TacticalInventory_Widget>(WidgetBlueprint::CreateWidget(UCk_TacticalInventory_Widget, this));
        if (ck::IsValid(_Screen) && _Screen.TryInitializeScreen())
        {
            if (!_CursorCaptured) { _PreviousCursor = bShowMouseCursor; _CursorCaptured = true; }
            bShowMouseCursor = true; _Screen.AddToViewport(); _Screen.ActivateWidget();
        }
    }
    UFUNCTION(BlueprintPure) UCk_TacticalInventory_Widget Get_Screen() const { return _Screen; }
    UFUNCTION(BlueprintPure) UCk_TacticalInventory_Widget Get_Hud() const { return _Screen; }
    FString Get_ControlPanelTitle() override { return "TACTICAL INVENTORY"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        const bool Ready = ck::IsValid(_Screen) && ck::IsValid(_Screen.Get_Session()) && _Screen.Get_Session().Get_IsUsable();
        Rows.Add(CkGym_Control::Header("TACTICAL INVENTORY FIXTURE"));
        Rows.Add(CkGym_Control::Action(EKeys::One, "1", "Open or close inventory", true));
        Rows.Add(CkGym_Control::Action(EKeys::Two, "2", "Rotate selected item", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Three, "3", "Quick transfer selected", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Four, "4", "Open document case", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Five, "5", "Reset fixture", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Six, "6", "Reload HTML / CSS", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Seven, "7", "Reject invalid overlap", Ready));
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
        if (InRowIndex == 2) { _Screen.Request_RotateSelected(); }
        else if (InRowIndex == 3) { _Screen.Request_QuickTransfer(); }
        else if (InRowIndex == 4) { _Screen.Request_Select("docs"); _Screen.Request_OpenCase(); }
        else if (InRowIndex == 5) { _Screen.Request_ResetFixture(); }
        else if (InRowIndex == 6) { _Screen.Request_Reload(); }
        else if (InRowIndex == 7) { _Screen.Request_TryMove("m4a1", "Stash", 4, 0, false); }
    }
    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (ck::IsValid(_Screen)) { _Screen.Request_Shutdown(); } _Screen = nullptr;
        if (_CursorCaptured) { bShowMouseCursor = _PreviousCursor; _CursorCaptured = false; }
    }
}

class ACk_TacticalInventory_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_TacticalInventory_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACkGym_ControlPanelHUD;
}
