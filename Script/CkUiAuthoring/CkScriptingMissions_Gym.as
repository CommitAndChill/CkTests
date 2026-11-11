// Language=angelscript

struct FCk_ScriptedMission
{
    FString Key;
    FString Title;
    FString Route;
    FString Brief;
    int32 Priority = 1;
    float32 Progress = 0.0f;
    bool Done = false;
    FLinearColor Accent = FLinearColor(0.22f, 0.82f, 0.94f, 1.0f);
}

class UCk_ScriptingMissions_Widget : UUserWidget
{
    UPROPERTY()
    private UCk_UI_AuthoredSession_UE _Session;
    UPROPERTY()
    private UCk_UI_AuthoredSurface_UE _Surface;
    UPROPERTY()
    private UTexture2D _Backdrop;
    private TArray<FCk_ScriptedMission> _Missions;
    private FCk_ScriptedMission _Removed;
    private bool _HasRemoved = false;
    private FString _SelectedKey = "relay";
    private FString _StartupError;
    private FString _SourceError;
    private int32 _SelectionCount = 0;
    private int32 _CompleteCount = 0;

    UFUNCTION()
    bool TryInitializeBoard()
    {
        if (ck::IsValid(_Session)) { return false; }
        _Session = Cast<UCk_UI_AuthoredSession_UE>(NewObject(this, UCk_UI_AuthoredSession_UE));
        _Surface = Cast<UCk_UI_AuthoredSurface_UE>(ConstructWidget(UCk_UI_AuthoredSurface_UE, n"MissionSurface"));
        auto Scale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"MissionScale"));
        auto Frame = Cast<USizeBox>(ConstructWidget(USizeBox, n"MissionFrame"));
        auto Root = Cast<UOverlay>(ConstructWidget(UOverlay, n"MissionOverlay"));
        auto BackdropScale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"MissionBackdropScale"));
        auto Image = Cast<UImage>(ConstructWidget(UImage, n"MissionBackdrop"));
        _Backdrop = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(FPaths::ProjectDir(),
            "Plugins/CkTests/Resources/UiAuthoring/ScriptedCollectionProof/mission-board-backdrop.png"));
        if (!ck::IsValid(_Session) || !ck::IsValid(_Surface) || !ck::IsValid(Scale) || !ck::IsValid(Frame)
            || !ck::IsValid(Root) || !ck::IsValid(BackdropScale) || !ck::IsValid(Image) || !ck::IsValid(_Backdrop))
        { _StartupError = "Widget construction failed"; Request_Shutdown(); return false; }
        _Missions = DoSeeds();
        auto Collection = FCk_UI_AuthoredCollection("Missions");
        auto Schema = TArray<FCk_UI_AuthoredFieldSchema>();
        Schema.Add(FCk_UI_AuthoredFieldSchema("TitleText", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("StatusText", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("RouteText", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("DoneBool", ECk_UI_AuthoredFieldKind::Bool));
        Schema.Add(FCk_UI_AuthoredFieldSchema("AccentColor", ECk_UI_AuthoredFieldKind::Color));
        Schema.Add(FCk_UI_AuthoredFieldSchema("PriorityInteger", ECk_UI_AuthoredFieldKind::Integer));
        Schema.Add(FCk_UI_AuthoredFieldSchema("ProgressNumber", ECk_UI_AuthoredFieldKind::Number));
        Collection.Set_Schema(Schema);
        Collection.Set_Records(DoRecords(_Missions));
        auto Collections = TArray<FCk_UI_AuthoredCollection>(); Collections.Add(Collection);
        auto Texts = TArray<FCk_UI_AuthoredText>();
        auto TextNames = TArray<FString>();
        TextNames.Add("MissionCount"); TextNames.Add("BoardStatus"); TextNames.Add("Selection");
        TextNames.Add("DetailTitle"); TextNames.Add("DetailRoute"); TextNames.Add("DetailBrief");
        TextNames.Add("DetailProgress"); TextNames.Add("DetailStatus");
        for (const auto TextName : TextNames)
        { Texts.Add(FCk_UI_AuthoredText(TextName, FText::FromString(""))); }
        auto Actions = TArray<FString>(); Actions.Add("CompleteMission");
        auto ItemActions = TArray<FString>(); ItemActions.Add("SelectMission");
        auto Regions = TArray<FString>(); Regions.Add("main");
        const auto FontDirectory = FPaths::CombinePaths(FPaths::EngineContentDir(), "Slate/Fonts");
        auto Font = FCk_UI_AuthoredFontFamily("mission-ui", FPaths::CombinePaths(FPaths::ProjectDir(),
            "Plugins/CkTests/Resources/UiAuthoring/SkyrimJournalProof/RobotoCondensed.ttf"));
        Font.Set_BoldFile(FPaths::CombinePaths(FontDirectory, "Roboto-Bold.ttf"));
        Font.Set_FallbackRegularFile(FPaths::CombinePaths(FontDirectory, "DroidSansFallback.ttf"));
        Font.Set_FallbackBoldFile(FPaths::CombinePaths(FontDirectory, "DroidSansFallback.ttf"));
        auto Fonts = TArray<FCk_UI_AuthoredFontFamily>(); Fonts.Add(Font);
        auto Config = FCk_UI_AuthoredSession_Config();
        Config.Set_Regions(Regions); Config.Set_Text(Texts); Config.Set_Actions(Actions);
        Config.Set_Collections(Collections); Config.Set_ItemActions(ItemActions); Config.Set_FontFamilies(Fonts);
        auto Errors = TArray<FString>();
        if (!_Session.TryInitialize(Config, Errors))
        { DoErrors(Errors); _StartupError = _SourceError; Request_Shutdown(); return false; }
        _Session.OnAction.AddUFunction(this, n"DoOnAction");
        _Session.OnItemAction.AddUFunction(this, n"DoOnItemAction");
        DoRefreshDetails();
        if (!Request_Reload() || !_Session.TryMount(_Surface, "main", Errors))
        { if (!Errors.IsEmpty()) { DoErrors(Errors); } _StartupError = _SourceError; Request_Shutdown(); return false; }
        Frame.SetWidthOverride(1920.0f); Frame.SetHeightOverride(1080.0f); Frame.AddChild(_Surface);
        Scale.SetStretch(EStretch::ScaleToFit); Scale.AddChild(Frame);
        BackdropScale.SetStretch(EStretch::ScaleToFill);
        BackdropScale.SetClipping(EWidgetClipping::ClipToBounds);
        BackdropScale.SetVisibility(ESlateVisibility::HitTestInvisible);
        Image.SetBrushFromTexture(_Backdrop, true); BackdropScale.AddChild(Image);
        auto BackdropSlot = Root.AddChildToOverlay(BackdropScale);
        auto ContentSlot = Root.AddChildToOverlay(Scale);
        const bool BackdropReady = DoFillSlot(BackdropSlot);
        const bool ContentReady = DoFillSlot(ContentSlot);
        if (!BackdropReady || !ContentReady)
        { _StartupError = "Required mission overlay composition failed"; Request_Shutdown(); return false; }
        SetRootWidget(Root);
        return true;
    }

    private bool DoFillSlot(UOverlaySlot InSlot)
    {
        if (!ck::IsValid(InSlot)) { return false; }
        InSlot.SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
        InSlot.SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
        return true;
    }

    private TArray<FCk_ScriptedMission> DoSeeds() const
    {
        auto Result = TArray<FCk_ScriptedMission>();
        FCk_ScriptedMission Relay;
        Relay.Key = "relay"; Relay.Title = "Restore the relay"; Relay.Route = "VEGA ARRAY / SECTOR 07";
        Relay.Brief = "Reconnect the outer relay and restore contact with the expedition. The signal is fading.";
        Relay.Priority = 1; Relay.Progress = 35.0f; Relay.Accent = FLinearColor(1.0f, 0.68f, 0.25f, 1.0f); Result.Add(Relay);
        FCk_ScriptedMission Salvage;
        Salvage.Key = "salvage"; Salvage.Title = "Recover the black box"; Salvage.Route = "KEPLER WRECK / SECTOR 12";
        Salvage.Brief = "Recover the flight recorder from the silent freighter. Preserve the crew's final transmission.";
        Salvage.Priority = 2; Salvage.Progress = 10.0f; Result.Add(Salvage);
        FCk_ScriptedMission Distress;
        Distress.Key = "distress"; Distress.Title = "Answer the distress call"; Distress.Route = "ORION OUTPOST / SECTOR 03";
        Distress.Brief = "A remote outpost has broadcast an emergency beacon. Reach the station and establish a safe corridor.";
        Distress.Priority = 1; Distress.Progress = 65.0f; Result.Add(Distress);
        FCk_ScriptedMission Survey;
        Survey.Key = "survey"; Survey.Title = "Survey the anomaly"; Survey.Route = "NEREID RIFT / SECTOR 19";
        Survey.Brief = "Map the unexplained energy signatures beyond the rift. Return a complete survey to mission control.";
        Survey.Priority = 3; Survey.Progress = 0.0f; Result.Add(Survey);
        return Result;
    }

    private TArray<FCk_UI_AuthoredRecord> DoRecords(const TArray<FCk_ScriptedMission>& InMissions) const
    {
        auto Records = TArray<FCk_UI_AuthoredRecord>();
        for (const auto Mission : InMissions)
        {
            auto Fields = TArray<FCk_UI_AuthoredFieldValue>();
            auto Title = FCk_UI_AuthoredFieldValue("TitleText", ECk_UI_AuthoredFieldKind::Text);
            Title.Set_Text(FText::FromString(Mission.Title)); Fields.Add(Title);
            auto Status = FCk_UI_AuthoredFieldValue("StatusText", ECk_UI_AuthoredFieldKind::Text);
            Status.Set_Text(FText::FromString(Mission.Done ? "OBJECTIVE SECURED" : "AWAITING OPERATOR")); Fields.Add(Status);
            auto Route = FCk_UI_AuthoredFieldValue("RouteText", ECk_UI_AuthoredFieldKind::Text);
            Route.Set_Text(FText::FromString(Mission.Route)); Fields.Add(Route);
            auto Done = FCk_UI_AuthoredFieldValue("DoneBool", ECk_UI_AuthoredFieldKind::Bool);
            Done.Set_Bool(Mission.Done); Fields.Add(Done);
            auto Accent = FCk_UI_AuthoredFieldValue("AccentColor", ECk_UI_AuthoredFieldKind::Color);
            Accent.Set_Color(Mission.Accent); Fields.Add(Accent);
            auto PriorityField = FCk_UI_AuthoredFieldValue("PriorityInteger", ECk_UI_AuthoredFieldKind::Integer);
            PriorityField.Set_Integer(Mission.Priority); Fields.Add(PriorityField);
            auto Progress = FCk_UI_AuthoredFieldValue("ProgressNumber", ECk_UI_AuthoredFieldKind::Number);
            Progress.Set_Number(Mission.Progress); Fields.Add(Progress);
            auto Record = FCk_UI_AuthoredRecord(Mission.Key); Record.Set_Fields(Fields); Records.Add(Record);
        }
        return Records;
    }

    private bool DoReplace(const TArray<FCk_ScriptedMission>& InMissions)
    {
        if (!ck::IsValid(_Session) || !_Session.Get_IsUsable()) { return false; }
        auto Errors = TArray<FString>();
        if (!_Session.TrySetRecords("Missions", DoRecords(InMissions), Errors)) { DoErrors(Errors); return false; }
        _Missions = InMissions;
        bool Found = false;
        for (const auto Mission : _Missions) { if (Mission.Key == _SelectedKey) { Found = true; } }
        if (!Found) { _SelectedKey = ""; }
        DoRefreshDetails(); return true;
    }

    private void DoRefreshDetails()
    {
        if (!ck::IsValid(_Session) || !_Session.Get_IsUsable()) { return; }
        _Session.TrySetText("MissionCount", FText::FromString(f"{_Missions.Num()} OPERATIONS"));
        _Session.TrySetText("BoardStatus", FText::FromString(_Missions.IsEmpty() ? "ALL CHANNELS CLEAR" : "LIVE MISSION FEED"));
        _Session.TrySetText("Selection", FText::FromString(_SelectedKey.IsEmpty() ? "NO MISSION SELECTED" : "SELECTED OBJECTIVE"));
        _Session.TrySetText("DetailTitle", FText::FromString("Awaiting a mission"));
        _Session.TrySetText("DetailRoute", FText::FromString("TERMINUS / OPERATIONS COMMAND"));
        _Session.TrySetText("DetailBrief", FText::FromString("Select an operation from the mission feed to review its briefing and objective."));
        _Session.TrySetText("DetailProgress", FText::FromString("--"));
        _Session.TrySetText("DetailStatus", FText::FromString("STANDBY"));
        for (const auto Mission : _Missions)
        {
            if (Mission.Key != _SelectedKey) { continue; }
            _Session.TrySetText("DetailTitle", FText::FromString(Mission.Title));
            _Session.TrySetText("DetailRoute", FText::FromString(Mission.Route));
            _Session.TrySetText("DetailBrief", FText::FromString(Mission.Brief));
            _Session.TrySetText("DetailProgress", FText::FromString(f"{Math::RoundToInt(Mission.Progress)}% / PRIORITY {Mission.Priority}"));
            _Session.TrySetText("DetailStatus", FText::FromString(Mission.Done ? "MISSION COMPLETE" : "IN PROGRESS"));
        }
    }

    UFUNCTION()
    private void DoOnItemAction(const FString& InAction, const FString& InKey)
    {
        if (InAction != "SelectMission" || !ck::IsValid(_Session) || !_Session.Get_IsUsable()) { return; }
        for (const auto Mission : _Missions)
        { if (Mission.Key == InKey) { _SelectedKey = InKey; ++_SelectionCount; DoRefreshDetails(); return; } }
    }

    UFUNCTION()
    private void DoOnAction(const FString& InAction)
    {
        if (InAction != "CompleteMission" || _SelectedKey.IsEmpty()) { return; }
        auto Candidate = _Missions;
        for (int32 Index = 0; Index < Candidate.Num(); ++Index)
        {
            if (Candidate[Index].Key != _SelectedKey || Candidate[Index].Done) { continue; }
            Candidate[Index].Done = true; Candidate[Index].Progress = 100.0f;
            Candidate[Index].Accent = FLinearColor(0.39f, 0.88f, 0.69f, 1.0f);
            if (DoReplace(Candidate)) { ++_CompleteCount; } return;
        }
    }

    UFUNCTION()
    bool Request_Reverse()
    {
        auto Candidate = TArray<FCk_ScriptedMission>();
        for (int32 Index = _Missions.Num() - 1; Index >= 0; --Index) { Candidate.Add(_Missions[Index]); }
        return DoReplace(Candidate);
    }

    UFUNCTION()
    bool Request_RemoveSelected()
    {
        if (_SelectedKey.IsEmpty()) { return false; }
        auto Candidate = TArray<FCk_ScriptedMission>();
        FCk_ScriptedMission Removed; bool Found = false;
        for (const auto Mission : _Missions)
        { if (Mission.Key == _SelectedKey) { Removed = Mission; Found = true; } else { Candidate.Add(Mission); } }
        if (!Found || !DoReplace(Candidate)) { return false; }
        _Removed = Removed; _HasRemoved = true; return true;
    }

    UFUNCTION()
    bool Request_Reinsert()
    {
        if (!_HasRemoved) { return false; }
        auto Candidate = _Missions; Candidate.Add(_Removed);
        if (!DoReplace(Candidate)) { return false; }
        _HasRemoved = false; return true;
    }

    UFUNCTION()
    bool Request_Reset()
    {
        if (!DoReplace(DoSeeds())) { return false; }
        _SelectedKey = "relay"; _HasRemoved = false; _SelectionCount = 0; _CompleteCount = 0;
        DoRefreshDetails(); return true;
    }

    UFUNCTION()
    bool Request_Empty() { return DoReplace(TArray<FCk_ScriptedMission>()); }

    UFUNCTION()
    bool Request_Reload()
    {
        if (!ck::IsValid(_Session) || !_Session.Get_IsUsable()) { return false; }
        const auto Directory = FPaths::CombinePaths(FPaths::ProjectDir(), "Plugins/CkTests/Resources/UiAuthoring/ScriptedCollectionProof");
        auto Errors = TArray<FString>();
        const bool Accepted = _Session.TryReloadFiles(FPaths::CombinePaths(Directory, "ScriptedCollectionProof.ui.html"),
            FPaths::CombinePaths(Directory, "ScriptedCollectionProof.ui.css"), Errors);
        DoErrors(Errors); return Accepted;
    }

    UFUNCTION()
    bool TryRejectData()
    {
        if (!ck::IsValid(_Session) || !_Session.Get_IsUsable()) { return false; }
        auto Candidate = DoSeeds();
        Candidate[0].Title = "THIS MUST NEVER BE PUBLISHED";
        auto Bad = DoRecords(Candidate); Bad.Add(FCk_UI_AuthoredRecord("missing-required-fields"));
        auto Errors = TArray<FString>();
        const bool Accepted = _Session.TrySetRecords("Missions", Bad, Errors);
        DoErrors(Errors); return !Accepted && !Errors.IsEmpty();
    }

    UFUNCTION()
    bool TryRejectSource()
    {
        if (!ck::IsValid(_Session) || !_Session.Get_IsUsable()) { return false; }
        auto Errors = TArray<FString>();
        const bool Accepted = _Session.TryReload("<ui version=\"1\"><region name=\"main\"><repeat id=\"bad\" bind=\"Missions\"><text id=\"bad-field\" bind-field=\"Undeclared\"/></repeat></region></ui>", "", Errors);
        DoErrors(Errors); return !Accepted && !Errors.IsEmpty();
    }

    private void DoErrors(const TArray<FString>& InErrors)
    {
        _SourceError = "";
        for (const auto Error : InErrors) { _SourceError = _SourceError.IsEmpty() ? Error : f"{_SourceError}; {Error}"; }
    }

    UFUNCTION()
    void Request_Shutdown()
    {
        if (ck::IsValid(_Session)) { _Session.Request_Release(); }
        if (ck::IsValid(_Surface)) { _Surface.ReleaseView(); }
        RemoveFromParent();
    }
    UFUNCTION(BlueprintOverride)
    void Destruct() { if (ck::IsValid(_Session)) { _Session.Request_Release(); } }
    UFUNCTION(BlueprintPure)
    UCk_UI_AuthoredSession_UE Get_Session() const { return _Session; }
    UFUNCTION(BlueprintPure)
    FString Get_SelectedKey() const { return _SelectedKey; }
    UFUNCTION(BlueprintPure)
    FString Get_StartupError() const { return _StartupError; }
    UFUNCTION(BlueprintPure)
    FString Get_SourceError() const { return _SourceError; }
    UFUNCTION(BlueprintPure)
    int32 Get_SelectionCount() const { return _SelectionCount; }
    UFUNCTION(BlueprintPure)
    int32 Get_CompleteCount() const { return _CompleteCount; }
}

class ACk_ScriptingMissions_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY()
    private UCk_ScriptingMissions_Widget _Board;
    private bool _PreviousCursor = false;
    private bool _CursorCaptured = false;
    void Request_StartGym() override
    {
        if (ck::IsValid(_Board)) { _Board.Request_Shutdown(); }
        _Board = Cast<UCk_ScriptingMissions_Widget>(WidgetBlueprint::CreateWidget(UCk_ScriptingMissions_Widget, this));
        if (ck::IsValid(_Board) && _Board.TryInitializeBoard())
        {
            if (!_CursorCaptured) { _PreviousCursor = bShowMouseCursor; _CursorCaptured = true; }
            bShowMouseCursor = true; _Board.AddToViewport();
        }
    }
    UFUNCTION(BlueprintPure)
    UCk_ScriptingMissions_Widget Get_Board() const { return _Board; }
    FString Get_ControlPanelTitle() override { return "TERMINUS / SCRIPTED MISSION BOARD"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        const bool Ready = ck::IsValid(_Board) && ck::IsValid(_Board.Get_Session()) && _Board.Get_Session().Get_IsUsable();
        Rows.Add(CkGym_Control::Header("ANGELSCRIPT / KEYED COLLECTION"));
        Rows.Add(CkGym_Control::Action(EKeys::One, "1", "Reverse mission order", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Two, "2", "Remove selected mission", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Three, "3", "Reinsert removed key", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Four, "4", "Reset missions", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Five, "5", "Reload HTML / CSS", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Six, "6", "Reject invalid data and source", Ready));
        Rows.Add(CkGym_Control::Status("Controls", "Mouse: select mission / complete"));
        if (ck::IsValid(_Board))
        {
            Rows.Add(CkGym_Control::Status("Selected key", _Board.Get_SelectedKey()));
            const auto Error = _Board.Get_SourceError();
            Rows.Add(CkGym_Control::Status("Source", Error.IsEmpty() ? "accepted" : Error, !Error.IsEmpty()));
        }
        Rows.Add(CkGym_Control::Status("Gyms", "Tab: selector  H: panel")); return Rows;
    }
    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!ck::IsValid(_Board)) { return; }
        if (InRowIndex == 1) { _Board.Request_Reverse(); }
        else if (InRowIndex == 2) { _Board.Request_RemoveSelected(); }
        else if (InRowIndex == 3) { _Board.Request_Reinsert(); }
        else if (InRowIndex == 4) { _Board.Request_Reset(); }
        else if (InRowIndex == 5) { _Board.Request_Reload(); }
        else if (InRowIndex == 6) { _Board.TryRejectData(); _Board.TryRejectSource(); }
    }
    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (ck::IsValid(_Board)) { _Board.Request_Shutdown(); } _Board = nullptr;
        if (_CursorCaptured) { bShowMouseCursor = _PreviousCursor; _CursorCaptured = false; }
    }
}

class ACk_ScriptingMissions_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_ScriptingMissions_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACkGym_ControlPanelHUD;
}
