// Language=angelscript

struct FCk_WorldMapMarker
{
    FString Key;
    FString Label;
    FString Kind;
    FString District;
    FString Distance;
    FString Status;
    FString Description;
    float32 X = 0.0f;
    float32 Y = 0.0f;
    bool Enabled = true;
}

class UCk_WorldMap_Widget : UCk_ActivatableWidget_UE
{
    default bIsFocusable = true;
    default bAutoActivate = false;
    UPROPERTY() private UCk_UI_AuthoredSession_UE _Session;
    UPROPERTY() private UCk_UI_AuthoredSurface_UE _Surface;
    UPROPERTY() private UTexture2D _MapTexture;
    private TArray<FCk_WorldMapMarker> _Markers;
    private FString _SelectedKey = "quest-meridian";
    private FString _Filter = "All";
    private FString _Status = "SELECT A SIGNAL TO INSPECT";
    private FString _StartupError;
    private FString _SourceError;
    private bool _RouteVisible = false;
    private bool _Open = true;
    private bool _Retired = false;
    private int32 _SelectionCount = 0;

    private FString DoDirectory() const
    { return FPaths::CombinePaths(FPaths::ProjectDir(), "Plugins/CkTests/Resources/UiAuthoring/WorldMapProof"); }
    private FString DoFontDirectory() const
    { return FPaths::CombinePaths(FPaths::ProjectDir(), "Plugins/CkTests/Resources/UiAuthoring/ReplicaPresentation/Fonts"); }
    private bool DoReady() const { return !_Retired && ck::IsValid(_Session) && _Session.Get_IsUsable(); }
    private FCk_WorldMapMarker DoMarker(const FString& InKey, const FString& InLabel, const FString& InKind,
        const FString& InDistrict, const FString& InDistance, const FString& InStatus,
        const FString& InDescription, float32 InX, float32 InY, bool InEnabled = true) const
    {
        FCk_WorldMapMarker Marker; Marker.Key = InKey; Marker.Label = InLabel; Marker.Kind = InKind;
        Marker.District = InDistrict; Marker.Distance = InDistance; Marker.Status = InStatus;
        Marker.Description = InDescription; Marker.X = InX; Marker.Y = InY; Marker.Enabled = InEnabled;
        return Marker;
    }
    private TArray<FCk_WorldMapMarker> DoSeeds() const
    {
        auto Markers = TArray<FCk_WorldMapMarker>();
        Markers.Add(DoMarker("player", "YOUR POSITION", "player", "LOWER CONCOURSE", "0 M", "LIVE POSITION", "Field transponder linked to the Vesper grid.", 0.30f, 0.55f));
        Markers.Add(DoMarker("quest-meridian", "THE MERIDIAN ARCHIVE", "quest", "MERIDIAN", "1.2 KM", "TRACKED OBJECTIVE", "Recover the survey ledger before the archive power fails.", 0.52f, 0.42f));
        Markers.Add(DoMarker("quest-quiet", "A QUIET FREQUENCY", "quest", "NORTH QUAY", "2.8 KM", "AVAILABLE", "Trace the unregistered transmission along the old waterfront.", 0.22f, 0.24f));
        Markers.Add(DoMarker("quest-eclipse", "ECLIPSE PROTOCOL", "quest", "CROWN DISTRICT", "3.4 KM", "LOCKED", "The relay requires an access cipher before entry.", 0.76f, 0.22f, false));
        Markers.Add(DoMarker("quest-orbit", "THE ORBITAL SPINDLE", "quest", "SKYLINE EAST", "4.6 KM", "AVAILABLE", "Reach the uplink balcony and identify the missing carrier.", 0.93f, 0.12f));
        Markers.Add(DoMarker("service-transit", "CENTRAL TRANSIT", "fast-travel", "MERIDIAN", "0.7 KM", "FAST TRAVEL", "Maglev connections to every open district.", 0.43f, 0.49f));
        Markers.Add(DoMarker("service-clinic", "HALCYON CLINIC", "vendor", "GARDEN WARD", "1.9 KM", "MEDICAL", "Supplies, treatment and field recovery.", 0.37f, 0.76f));
        Markers.Add(DoMarker("service-market", "GLASS MARKET", "vendor", "NORTH QUAY", "2.2 KM", "TRADE", "Independent vendors under the north aqueduct.", 0.18f, 0.37f));
        Markers.Add(DoMarker("service-workshop", "FOUNDRY WORKSHOP", "vendor", "IRON YARD", "3.1 KM", "CRAFTING", "Equipment calibration and precision repairs.", 0.72f, 0.69f));
        Markers.Add(DoMarker("service-relay", "SOUTH RELAY", "fast-travel", "SOUTH SPAN", "3.8 KM", "OFFLINE", "Network service is suspended until power is restored.", 0.83f, 0.87f, false));
        Markers.Add(DoMarker("hazard-flood", "FLOODED UNDERPASS", "hazard", "LOWER CONCOURSE", "0.9 KM", "HIGH RISK", "An unstable crossing; use the elevated route.", 0.48f, 0.66f));
        Markers.Add(DoMarker("hazard-static", "STATIC FIELD", "hazard", "CROWN DISTRICT", "2.7 KM", "HIGH RISK", "Heavy interference reduces sensor range.", 0.69f, 0.34f));
        return Markers;
    }
    private int32 DoFind(const TArray<FCk_WorldMapMarker>& InMarkers, const FString& InKey) const
    { for (int32 Index = 0; Index < InMarkers.Num(); ++Index) { if (InMarkers[Index].Key == InKey) { return Index; } } return -1; }
    private bool DoVisible(const FCk_WorldMapMarker& InMarker, const FString& InFilter) const
    { return InFilter == "All" || (InFilter == "Quests" && InMarker.Kind == "quest") || (InFilter == "Services" && (InMarker.Kind == "vendor" || InMarker.Kind == "fast-travel")); }
    private bool DoValid(const TArray<FCk_WorldMapMarker>& InMarkers) const
    {
        for (int32 Index = 0; Index < InMarkers.Num(); ++Index)
        {
            const auto Marker = InMarkers[Index];
            if (Marker.Key.IsEmpty() || Marker.Label.IsEmpty()
                || (Marker.Kind != "quest" && Marker.Kind != "vendor" && Marker.Kind != "fast-travel"
                    && Marker.Kind != "player" && Marker.Kind != "hazard" && Marker.Kind != "generic")
                || Marker.X < 0.0f || Marker.X > 1.0f || Marker.Y < 0.0f || Marker.Y > 1.0f) { return false; }
            for (int32 Other = 0; Other < Index; ++Other)
            { if (InMarkers[Other].Key == Marker.Key) { return false; } }
        }
        return true;
    }
    private FString DoEligibleSelection(const TArray<FCk_WorldMapMarker>& InMarkers,
        const FString& InPreferred, const FString& InFilter) const
    {
        const int32 Preferred = DoFind(InMarkers, InPreferred);
        if (Preferred >= 0 && InMarkers[Preferred].Enabled && DoVisible(InMarkers[Preferred], InFilter)) { return InPreferred; }
        for (const auto Marker : InMarkers)
        { if (Marker.Enabled && Marker.Kind != "player" && DoVisible(Marker, InFilter)) { return Marker.Key; } }
        return "";
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
        Schema.Add(FCk_UI_AuthoredFieldSchema("label", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("kind", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("x", ECk_UI_AuthoredFieldKind::Number));
        Schema.Add(FCk_UI_AuthoredFieldSchema("y", ECk_UI_AuthoredFieldKind::Number));
        Schema.Add(FCk_UI_AuthoredFieldSchema("enabled", ECk_UI_AuthoredFieldKind::Bool));
        Schema.Add(FCk_UI_AuthoredFieldSchema("visible", ECk_UI_AuthoredFieldKind::Bool));
        return Schema;
    }
    private TArray<FCk_UI_AuthoredRecord> DoRecords(const TArray<FCk_WorldMapMarker>& InMarkers,
        const FString& InFilter) const
    {
        auto Records = TArray<FCk_UI_AuthoredRecord>();
        for (const auto Marker : InMarkers)
        {
            auto Fields = TArray<FCk_UI_AuthoredFieldValue>();
            Fields.Add(DoTextField("label", Marker.Label)); Fields.Add(DoTextField("kind", Marker.Kind));
            Fields.Add(DoNumberField("x", Marker.X)); Fields.Add(DoNumberField("y", Marker.Y));
            Fields.Add(DoBoolField("enabled", Marker.Enabled)); Fields.Add(DoBoolField("visible", DoVisible(Marker, InFilter)));
            auto Record = FCk_UI_AuthoredRecord(Marker.Key); Record.Set_Fields(Fields); Records.Add(Record);
        }
        return Records;
    }
    private void DoErrors(const TArray<FString>& InErrors)
    { _SourceError = ""; for (const auto Error : InErrors) { _SourceError = _SourceError.IsEmpty() ? Error : f"{_SourceError}; {Error}"; } }
    private void DoRetire(const FString& InError) { _SourceError = InError; Request_Shutdown(); }
    private bool DoText(const FString& InName, const FString& InValue)
    { return _Session.TrySetText(InName, FText::FromString(InValue)); }
    private FString DoKindLabel(const FString& InKind) const
    {
        if (InKind == "quest") { return "OBJECTIVE"; }
        if (InKind == "fast-travel") { return "TRANSIT"; }
        if (InKind == "vendor") { return "SERVICE"; }
        if (InKind == "player") { return "YOU ARE HERE"; }
        if (InKind == "hazard") { return "HAZARD"; }
        return "SIGNAL";
    }
    private FString DoFilterLabel(const FString& InFilter) const
    {
        if (InFilter == "Quests") { return "QUESTS"; }
        if (InFilter == "Services") { return "SERVICES"; }
        return "ALL";
    }
    private bool DoScalars(const TArray<FCk_WorldMapMarker>& InMarkers, const FString& InSelected,
        const FString& InFilter, bool InRoute, const FString& InStatus)
    {
        const int32 Index = DoFind(InMarkers, InSelected);
        const bool HasSelection = Index >= 0;
        FCk_WorldMapMarker Marker;
        if (HasSelection) { Marker = InMarkers[Index]; }
        return DoText("SelectedKey", InSelected)
            && DoText("SelectedTitle", HasSelection ? Marker.Label : "NO SIGNAL SELECTED")
            && DoText("SelectedKind", HasSelection ? DoKindLabel(Marker.Kind) : "NO ACTIVE TARGET")
            && DoText("SelectedDistrict", HasSelection ? Marker.District : "—")
            && DoText("SelectedDistance", HasSelection ? Marker.Distance : "—")
            && DoText("SelectedStatus", HasSelection ? Marker.Status : "STANDBY")
            && DoText("SelectedDescription", HasSelection ? Marker.Description : "Select an available marker to inspect its location and route.")
            && DoText("Status", InStatus)
            && DoText("FilterLabel", DoFilterLabel(InFilter))
            && _Session.TrySetBool("HasSelection", HasSelection)
            && _Session.TrySetBool("CanRoute", HasSelection && Marker.Enabled && Marker.Kind != "player" && Marker.Kind != "hazard")
            && _Session.TrySetBool("RouteVisible", HasSelection && InRoute)
            && _Session.TrySetBool("RouteHidden", !HasSelection || !InRoute)
            && _Session.TrySetBool("FilterAll", InFilter == "All")
            && _Session.TrySetBool("FilterQuests", InFilter == "Quests")
            && _Session.TrySetBool("FilterServices", InFilter == "Services")
            && _Session.TrySetBool("FilterAllInactive", InFilter != "All")
            && _Session.TrySetBool("FilterQuestsInactive", InFilter != "Quests")
            && _Session.TrySetBool("FilterServicesInactive", InFilter != "Services");
    }
    private bool DoPublish(const TArray<FCk_WorldMapMarker>& InMarkers, const FString& InPreferred,
        const FString& InFilter, bool InRoute, const FString& InStatus)
    {
        if (!DoReady() || !DoValid(InMarkers) || (InFilter != "All" && InFilter != "Quests" && InFilter != "Services")) { return false; }
        const FString Selection = DoEligibleSelection(InMarkers, InPreferred, InFilter);
        // A changed or hidden destination cannot retain a route to the old marker.
        const bool Route = InRoute && Selection == _SelectedKey;
        if (!DoScalars(InMarkers, Selection, InFilter, Route, InStatus))
        { DoRetire("Map scalar publication failed"); return false; }
        auto Errors = TArray<FString>();
        if (!_Session.TrySetRecords("Markers", DoRecords(InMarkers, InFilter), Errors))
        { DoErrors(Errors); DoRetire("Map marker publication failed"); return false; }
        _Markers = InMarkers; _SelectedKey = Selection; _Filter = InFilter; _RouteVisible = Route; _Status = InStatus;
        return true;
    }
    private bool DoFill(UOverlaySlot InSlot)
    { if (!ck::IsValid(InSlot)) { return false; } InSlot.SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill); InSlot.SetVerticalAlignment(EVerticalAlignment::VAlign_Fill); return true; }
    UFUNCTION()
    bool TryInitializeScreen()
    {
        if (ck::IsValid(_Session)) { return false; }
        _Session = Cast<UCk_UI_AuthoredSession_UE>(NewObject(this, UCk_UI_AuthoredSession_UE));
        _Surface = Cast<UCk_UI_AuthoredSurface_UE>(ConstructWidget(UCk_UI_AuthoredSurface_UE, n"WorldMapSurface"));
        auto Scale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"WorldMapScale"));
        auto Frame = Cast<USizeBox>(ConstructWidget(USizeBox, n"WorldMapFrame"));
        auto Root = Cast<UOverlay>(ConstructWidget(UOverlay, n"WorldMapRoot"));
        _MapTexture = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(DoDirectory(), "vesper-map.png"));
        if (!ck::IsValid(_Session) || !ck::IsValid(_Surface) || !ck::IsValid(Scale)
            || !ck::IsValid(Frame) || !ck::IsValid(Root) || !ck::IsValid(_MapTexture))
        { _StartupError = "Map screen or image construction failed"; Request_Shutdown(); return false; }
        _Markers = DoSeeds();
        if (!DoValid(_Markers)) { _StartupError = "Map fixture invalid"; Request_Shutdown(); return false; }
        auto Collection = FCk_UI_AuthoredCollection("Markers");
        Collection.Set_Schema(DoSchema()); Collection.Set_Records(DoRecords(_Markers, _Filter));
        auto Collections = TArray<FCk_UI_AuthoredCollection>(); Collections.Add(Collection);
        auto Texts = TArray<FCk_UI_AuthoredText>();
        auto TextNames = TArray<FString>();
        TextNames.Add("SelectedKey"); TextNames.Add("SelectedTitle"); TextNames.Add("SelectedKind");
        TextNames.Add("SelectedDistrict"); TextNames.Add("SelectedDistance"); TextNames.Add("SelectedStatus");
        TextNames.Add("SelectedDescription"); TextNames.Add("Status"); TextNames.Add("FilterLabel");
        for (const auto TextName : TextNames) { Texts.Add(FCk_UI_AuthoredText(TextName, FText::FromString(""))); }
        auto Bools = TArray<FCk_UI_AuthoredBool>();
        auto BoolNames = TArray<FString>();
        BoolNames.Add("HasSelection"); BoolNames.Add("CanRoute"); BoolNames.Add("RouteVisible"); BoolNames.Add("RouteHidden");
        BoolNames.Add("FilterAll"); BoolNames.Add("FilterQuests"); BoolNames.Add("FilterServices");
        BoolNames.Add("FilterAllInactive"); BoolNames.Add("FilterQuestsInactive"); BoolNames.Add("FilterServicesInactive");
        for (const auto BoolName : BoolNames) { Bools.Add(FCk_UI_AuthoredBool(BoolName, false)); }
        auto Images = TArray<FCk_UI_AuthoredImage>();
        auto MapImage = FCk_UI_AuthoredImage("MapImage"); MapImage.Set_Texture(_MapTexture); Images.Add(MapImage);
        auto Actions = TArray<FString>();
        Actions.Add("FilterAllAction"); Actions.Add("FilterQuestsAction"); Actions.Add("FilterServicesAction");
        Actions.Add("ZoomIn"); Actions.Add("ZoomOut"); Actions.Add("ResetCamera"); Actions.Add("CenterTarget");
        Actions.Add("ToggleRoute"); Actions.Add("Close"); Actions.Add("Reload");
        auto ItemActions = TArray<FString>(); ItemActions.Add("SelectMarker");
        auto Regions = TArray<FString>(); Regions.Add("main");
        const FString FontDir = DoFontDirectory();
        const FString Fallback = FPaths::CombinePaths(FPaths::EngineContentDir(), "Slate/Fonts/DroidSansFallback.ttf");
        auto Ui = FCk_UI_AuthoredFontFamily("map-ui", FPaths::CombinePaths(FontDir, "Rajdhani-Regular.ttf"));
        Ui.Set_BoldFile(FPaths::CombinePaths(FontDir, "Rajdhani-SemiBold.ttf"));
        Ui.Set_FallbackRegularFile(Fallback); Ui.Set_FallbackBoldFile(Fallback);
        auto Display = FCk_UI_AuthoredFontFamily("map-display", FPaths::CombinePaths(FontDir, "Rajdhani-SemiBold.ttf"));
        Display.Set_BoldFile(FPaths::CombinePaths(FontDir, "Rajdhani-Bold.ttf"));
        Display.Set_FallbackRegularFile(Fallback); Display.Set_FallbackBoldFile(Fallback);
        auto Fonts = TArray<FCk_UI_AuthoredFontFamily>(); Fonts.Add(Ui); Fonts.Add(Display);
        auto Tokens = TMap<FString, FString>();
        Tokens.Add("--ink-main", "#E9EFF0"); Tokens.Add("--ink-dim", "#93A7B0");
        Tokens.Add("--ink-amber", "#F2BA68"); Tokens.Add("--ink-cyan", "#7AD4D7");
        auto Config = FCk_UI_AuthoredSession_Config();
        Config.Set_InteractionOwner(this); Config.Set_Regions(Regions); Config.Set_Text(Texts);
        Config.Set_Bool(Bools); Config.Set_Collections(Collections); Config.Set_Images(Images);
        Config.Set_Actions(Actions); Config.Set_ItemActions(ItemActions);
        Config.Set_FontFamilies(Fonts); Config.Set_StyleTokens(Tokens);
        Config.Set_RegisterHudControls(true); Config.Set_RegisterMapControls(true);
        auto Errors = TArray<FString>();
        if (!_Session.TryInitialize(Config, Errors))
        { DoErrors(Errors); _StartupError = _SourceError; Request_Shutdown(); return false; }
        if (!DoScalars(_Markers, _SelectedKey, _Filter, _RouteVisible, _Status))
        { _StartupError = "Initial map bindings failed"; Request_Shutdown(); return false; }
        _Session.OnAction.AddUFunction(this, n"DoOnAction");
        _Session.OnItemAction.AddUFunction(this, n"DoOnItemAction");
        if (!Request_Reload() || !_Session.TryMount(_Surface, "main", Errors))
        { if (!Errors.IsEmpty()) { DoErrors(Errors); } _StartupError = _SourceError; Request_Shutdown(); return false; }
        Frame.SetWidthOverride(1920.0f); Frame.SetHeightOverride(1080.0f); Frame.AddChild(_Surface);
        Scale.SetStretch(EStretch::ScaleToFit); Scale.AddChild(Frame);
        if (!DoFill(Root.AddChildToOverlay(Scale)))
        { _StartupError = "Map composition failed"; Request_Shutdown(); return false; }
        SetRootWidget(Root); return true;
    }
    UFUNCTION()
    bool Request_Reload()
    {
        if (!DoReady()) { return false; }
        FString Markup; FString Stylesheet;
        if (!FFileHelper::LoadFileToString(Markup, FPaths::CombinePaths(DoDirectory(), "WorldMapProof.ui.html"))
            || !FFileHelper::LoadFileToString(Stylesheet, FPaths::CombinePaths(DoDirectory(), "WorldMapProof.ui.css")))
        { _SourceError = "Map source files could not load"; return false; }
        auto Errors = TArray<FString>(); const bool Accepted = _Session.TryReload(Markup, Stylesheet, Errors);
        DoErrors(Errors); return Accepted;
    }
    UFUNCTION()
    bool Request_Select(FString InKey)
    {
        if (!DoReady()) { return false; }
        const int32 Index = DoFind(_Markers, InKey);
        if (Index < 0 || !_Markers[Index].Enabled || !DoVisible(_Markers[Index], _Filter)) { return false; }
        if (InKey == _SelectedKey) { return true; }
        if (!DoPublish(_Markers, InKey, _Filter, false, f"SIGNAL ACQUIRED / {_Markers[Index].District}")) { return false; }
        ++_SelectionCount; return true;
    }
    UFUNCTION()
    bool Request_SetFilter(FString InFilter)
    {
        if (!DoReady() || (InFilter != "All" && InFilter != "Quests" && InFilter != "Services")) { return false; }
        if (InFilter == _Filter) { return true; }
        return DoPublish(_Markers, _SelectedKey, InFilter, _RouteVisible, f"FILTER / {DoFilterLabel(InFilter)}");
    }
    UFUNCTION()
    bool Request_ResetFixture()
    {
        if (!DoReady() || !DoPublish(DoSeeds(), "quest-meridian", "All", false, "VESPER SIGNAL GRID ONLINE")) { return false; }
        _Session.TrySetMapCamera("map-view", FVector2D(0.5f, 0.5f), 1.0f);
        return true;
    }
    UFUNCTION()
    bool Request_EmptyFixture()
    {
        if (!DoReady()) { return false; }
        auto Empty = TArray<FCk_WorldMapMarker>();
        return DoPublish(Empty, "", "All", false, "NO SIGNALS / GRID STANDBY");
    }
    UFUNCTION()
    bool Request_RemoveSelected()
    {
        if (!DoReady()) { return false; }
        const int32 Index = DoFind(_Markers, _SelectedKey);
        if (Index < 0) { return false; }
        auto Candidate = _Markers; Candidate.RemoveAt(Index);
        return DoPublish(Candidate, "", _Filter, false, "SIGNAL REMOVED / SELECTING NEXT AVAILABLE");
    }
    UFUNCTION()
    bool Request_ReinsertAndReorder()
    {
        if (!DoReady()) { return false; }
        auto Candidate = DoSeeds();
        auto Reordered = TArray<FCk_WorldMapMarker>();
        for (int32 Index = Candidate.Num() - 1; Index >= 0; --Index) { Reordered.Add(Candidate[Index]); }
        return DoPublish(Reordered, _SelectedKey, _Filter, false, "SIGNAL GRID RESYNCED / ORDER REVERSED");
    }
    UFUNCTION()
    bool Request_TryInvalidMarkers()
    {
        if (!DoReady()) { return false; }
        auto Invalid = DoRecords(_Markers, _Filter);
        if (Invalid.IsEmpty()) { return false; }
        auto Duplicate = Invalid[0];
        Invalid.Add(Duplicate);
        auto Errors = TArray<FString>();
        const bool Accepted = _Session.TrySetRecords("Markers", Invalid, Errors);
        if (!Accepted) { DoErrors(Errors); return false; }
        DoRetire("Invalid marker collection was unexpectedly accepted"); return true;
    }
    UFUNCTION()
    bool Request_LongLabel()
    {
        if (!DoReady()) { return false; }
        auto Candidate = _Markers;
        const int32 Index = DoFind(Candidate, "quest-meridian");
        if (Index < 0) { return false; }
        Candidate[Index].Label = "THE MERIDIAN ARCHIVE: RECOVERY OF THE LAST SURVEY LEDGER";
        return DoPublish(Candidate, Candidate[Index].Key, "All", false, "LONG TITLE / TYPE MEASUREMENT");
    }
    UFUNCTION()
    bool Request_ToggleRoute()
    {
        if (!DoReady()) { return false; }
        const int32 Index = DoFind(_Markers, _SelectedKey);
        if (Index < 0 || !_Markers[Index].Enabled || _Markers[Index].Kind == "player" || _Markers[Index].Kind == "hazard") { return false; }
        const bool NextRoute = !_RouteVisible;
        if (!DoPublish(_Markers, _SelectedKey, _Filter, NextRoute,
            NextRoute ? "ROUTE COMPUTED / FOLLOW AMBER VECTOR" : "ROUTE CLEARED")) { return false; }
        return true;
    }
    UFUNCTION()
    bool Request_CenterTarget()
    {
        if (!DoReady()) { return false; }
        const int32 Index = DoFind(_Markers, _SelectedKey);
        return Index >= 0 && _Session.TryRevealMapMarker("map-view", _SelectedKey);
    }
    UFUNCTION()
    bool Request_Zoom(float32 InFactor)
    { return DoReady() && _Session.TryZoomMap("map-view", InFactor); }
    UFUNCTION()
    bool Request_ResetCamera()
    { return DoReady() && _Session.TrySetMapCamera("map-view", FVector2D(0.5f, 0.5f), 1.0f); }
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
    UFUNCTION()
    private void DoOnItemAction(const FString& InName, const FString& InKey)
    { if (InName == "SelectMarker") { Request_Select(InKey); } }
    UFUNCTION()
    private void DoOnAction(const FString& InName)
    {
        if (InName == "FilterAllAction") { Request_SetFilter("All"); }
        else if (InName == "FilterQuestsAction") { Request_SetFilter("Quests"); }
        else if (InName == "FilterServicesAction") { Request_SetFilter("Services"); }
        else if (InName == "ZoomIn") { Request_Zoom(1.25f); }
        else if (InName == "ZoomOut") { Request_Zoom(0.8f); }
        else if (InName == "ResetCamera") { Request_ResetCamera(); }
        else if (InName == "CenterTarget") { Request_CenterTarget(); }
        else if (InName == "ToggleRoute") { Request_ToggleRoute(); }
        else if (InName == "Reload") { Request_Reload(); }
        else if (InName == "Close") { Request_Show(false); }
    }
    UFUNCTION(BlueprintOverride)
    void OnActivated()
    { if (DoReady() && _Open) { if (!_Session.TryBeginInput()) { DoRetire("Map input admission failed"); } else { _Session.TryFocusControl("map-view"); } } }
    UFUNCTION(BlueprintOverride)
    void OnDeactivated()
    { if (ck::IsValid(_Session)) { _Session.Request_EndInput(); } }
    UFUNCTION(BlueprintOverride)
    FEventReply OnPreviewKeyDown(FGeometry InGeometry, FKeyEvent InEvent)
    {
        if (!DoReady() || !_Open || !_Session.Get_IsInputActive()
            || int32(InEvent.GetUserIndex()) != _Session.Get_SlateUserIndex()) { return FEventReply::Unhandled(); }
        if (InEvent.GetKey() == EKeys::Escape)
        { if (!InEvent.IsRepeat()) { Request_Show(false); } return FEventReply::Handled(); }
        return FEventReply::Unhandled();
    }
    UFUNCTION(BlueprintOverride) void Destruct() { Request_Shutdown(); }
    UFUNCTION(BlueprintPure) UCk_UI_AuthoredSession_UE Get_Session() const { return _Session; }
    UFUNCTION(BlueprintPure) FString Get_StartupError() const { return _StartupError; }
    UFUNCTION(BlueprintPure) FString Get_SourceError() const { return _SourceError; }
    UFUNCTION(BlueprintPure) FString Get_SelectedKey() const { return _SelectedKey; }
    UFUNCTION(BlueprintPure) bool Get_IsOpen() const { return _Open && DoReady(); }
    UFUNCTION(BlueprintPure) int32 Get_SelectionCount() const { return _SelectionCount; }
}

class ACk_WorldMap_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY() private UCk_WorldMap_Widget _Screen;
    private bool _PreviousCursor = false;
    private bool _CursorCaptured = false;
    void Request_StartGym() override
    {
        if (ck::IsValid(_Screen)) { _Screen.Request_Shutdown(); }
        _Screen = Cast<UCk_WorldMap_Widget>(WidgetBlueprint::CreateWidget(UCk_WorldMap_Widget, this));
        if (ck::IsValid(_Screen) && _Screen.TryInitializeScreen())
        {
            if (!_CursorCaptured) { _PreviousCursor = bShowMouseCursor; _CursorCaptured = true; }
            bShowMouseCursor = true; _Screen.AddToViewport(); _Screen.ActivateWidget();
        }
    }
    UFUNCTION(BlueprintPure) UCk_WorldMap_Widget Get_Screen() const { return _Screen; }
    UFUNCTION(BlueprintPure) UCk_WorldMap_Widget Get_Hud() const { return _Screen; }
    FString Get_ControlPanelTitle() override { return "VESPER WORLD MAP"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        const bool Ready = ck::IsValid(_Screen) && ck::IsValid(_Screen.Get_Session()) && _Screen.Get_Session().Get_IsUsable();
        Rows.Add(CkGym_Control::Header("WORLD MAP / AUTHORING PROOF"));
        Rows.Add(CkGym_Control::Action(EKeys::One, "1", "Open or close map", true));
        Rows.Add(CkGym_Control::Action(EKeys::Two, "2", "Quest signals", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Three, "3", "Services", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Four, "4", "Reset fixture", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Five, "5", "Reload HTML / CSS", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Six, "6", "Reject invalid markers", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Seven, "7", "Long selected title", Ready));
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
        if (InRowIndex == 2) { _Screen.Request_SetFilter("Quests"); }
        else if (InRowIndex == 3) { _Screen.Request_SetFilter("Services"); }
        else if (InRowIndex == 4) { _Screen.Request_ResetFixture(); }
        else if (InRowIndex == 5) { _Screen.Request_Reload(); }
        else if (InRowIndex == 6) { _Screen.Request_TryInvalidMarkers(); }
        else if (InRowIndex == 7) { _Screen.Request_LongLabel(); }
    }
    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (ck::IsValid(_Screen)) { _Screen.Request_Shutdown(); } _Screen = nullptr;
        if (_CursorCaptured) { bShowMouseCursor = _PreviousCursor; _CursorCaptured = false; }
    }
}

class ACk_WorldMap_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_WorldMap_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACkGym_ControlPanelHUD;
}
