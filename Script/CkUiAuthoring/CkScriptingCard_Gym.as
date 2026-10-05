// Language=angelscript

class UCk_ScriptingCard_Widget : UUserWidget
{
    UPROPERTY()
    private UCk_UI_AuthoredSession_UE _Session;

    UPROPERTY()
    private UCk_UI_AuthoredSurface_UE _Surface;

    private FString _SourceError;
    private int32 _ActionCount = 0;
    private int32 _ShotCount = 0;
    private int32 _NumberEditCount = 0;
    private int32 _BoolEditCount = 0;

    UFUNCTION()
    bool TryInitializeCard()
    {
        if (ck::IsValid(_Session)) { return false; }
        _Session = Cast<UCk_UI_AuthoredSession_UE>(NewObject(this, UCk_UI_AuthoredSession_UE));
        _Surface = Cast<UCk_UI_AuthoredSurface_UE>(ConstructWidget(UCk_UI_AuthoredSurface_UE, n"AuthoredCard"));
        auto Scale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"CardScale"));
        auto Frame = Cast<USizeBox>(ConstructWidget(USizeBox, n"CardFrame"));
        if (!ck::IsValid(_Session) || !ck::IsValid(_Surface) || !ck::IsValid(Scale) || !ck::IsValid(Frame))
        { _SourceError = "Widget construction failed"; Request_Shutdown(); return false; }

        auto Texts = TArray<FCk_UI_AuthoredText>();
        Texts.Add(FCk_UI_AuthoredText("ShotCount", FText::FromString("0")));
        Texts.Add(FCk_UI_AuthoredText("ChargeLabel", FText::FromString("65%")));
        Texts.Add(FCk_UI_AuthoredText("Status", FText::FromString("READY")));
        auto Numbers = TArray<FCk_UI_AuthoredNumber>();
        Numbers.Add(FCk_UI_AuthoredNumber("Charge", 65.0f));
        auto Bools = TArray<FCk_UI_AuthoredBool>();
        Bools.Add(FCk_UI_AuthoredBool("Safety", true));
        auto Actions = TArray<FString>();
        Actions.Add("Fire");
        const auto FontDirectory = FPaths::CombinePaths(FPaths::EngineContentDir(), "Slate/Fonts");
        auto CardFont = FCk_UI_AuthoredFontFamily("card-ui", FPaths::CombinePaths(FPaths::ProjectDir(),
            "Plugins/CkTests/Resources/UiAuthoring/SkyrimJournalProof/RobotoCondensed.ttf"));
        CardFont.Set_BoldFile(FPaths::CombinePaths(FontDirectory, "Roboto-Bold.ttf"));
        CardFont.Set_FallbackRegularFile(FPaths::CombinePaths(FontDirectory, "DroidSansFallback.ttf"));
        CardFont.Set_FallbackBoldFile(FPaths::CombinePaths(FontDirectory, "DroidSansFallback.ttf"));
        auto Fonts = TArray<FCk_UI_AuthoredFontFamily>();
        Fonts.Add(CardFont);
        auto Config = FCk_UI_AuthoredSession_Config();
        auto Regions = TArray<FString>();
        Regions.Add("main");
        Config.Set_Regions(Regions);
        Config.Set_Text(Texts);
        Config.Set_Number(Numbers);
        Config.Set_Bool(Bools);
        Config.Set_Actions(Actions);
        Config.Set_FontFamilies(Fonts);
        auto Errors = TArray<FString>();
        if (!_Session.TryInitialize(Config, Errors))
        { DoSetErrors(Errors); Request_Shutdown(); return false; }
        _Session.OnAction.AddUFunction(this, n"DoOnAction");
        _Session.OnNumberChanged.AddUFunction(this, n"DoOnNumberChanged");
        _Session.OnBoolChanged.AddUFunction(this, n"DoOnBoolChanged");
        if (!Request_Reload() || !_Session.TryMount(_Surface, "main", Errors))
        { if (!Errors.IsEmpty()) { DoSetErrors(Errors); } Request_Shutdown(); return false; }

        Frame.SetWidthOverride(1920.0f);
        Frame.SetHeightOverride(1080.0f);
        Frame.AddChild(_Surface);
        Scale.SetStretch(EStretch::ScaleToFit);
        Scale.AddChild(Frame);
        SetRootWidget(Scale);
        return true;
    }

    UFUNCTION()
    bool Request_Reload()
    {
        if (!ck::IsValid(_Session) || !_Session.Get_IsUsable()) { return false; }
        const auto Directory = FPaths::CombinePaths(FPaths::ProjectDir(),
            "Plugins/CkTests/Resources/UiAuthoring/ScriptingCardProof");
        auto Errors = TArray<FString>();
        const bool Accepted = _Session.TryReloadFiles(
            FPaths::CombinePaths(Directory, "ScriptingCardProof.ui.html"),
            FPaths::CombinePaths(Directory, "ScriptingCardProof.ui.css"), Errors);
        DoSetErrors(Errors);
        return Accepted;
    }

    UFUNCTION()
    bool TryRejectSource()
    {
        if (!ck::IsValid(_Session) || !_Session.Get_IsUsable()) { return false; }
        auto Errors = TArray<FString>();
        const bool Accepted = _Session.TryReload("<ui version=\"1\"><region name=\"main\"><text id=\"bad\" bind=\"Undeclared\"/></region></ui>", "", Errors);
        DoSetErrors(Errors);
        return !Accepted && !Errors.IsEmpty();
    }

    UFUNCTION()
    void Request_Reset()
    {
        if (!ck::IsValid(_Session) || !_Session.Get_IsUsable()) { return; }
        _ActionCount = 0; _ShotCount = 0; _NumberEditCount = 0; _BoolEditCount = 0;
        _Session.TrySetText("ShotCount", FText::FromString("0"));
        _Session.TrySetText("ChargeLabel", FText::FromString("65%"));
        _Session.TrySetText("Status", FText::FromString("READY"));
        _Session.TrySetNumber("Charge", 65.0f);
        _Session.TrySetBool("Safety", true);
    }

    UFUNCTION()
    void Request_Shutdown()
    {
        if (ck::IsValid(_Session)) { _Session.Request_Release(); }
        if (ck::IsValid(_Surface)) { _Surface.ReleaseView(); }
        RemoveFromParent();
    }

    UFUNCTION(BlueprintOverride)
    void Destruct()
    { if (ck::IsValid(_Session)) { _Session.Request_Release(); } }

    UFUNCTION(BlueprintPure)
    UCk_UI_AuthoredSession_UE Get_Session() const { return _Session; }

    UFUNCTION(BlueprintPure)
    int32 Get_ActionCount() const { return _ActionCount; }

    UFUNCTION(BlueprintPure)
    int32 Get_NumberEditCount() const { return _NumberEditCount; }

    UFUNCTION(BlueprintPure)
    int32 Get_BoolEditCount() const { return _BoolEditCount; }

    UFUNCTION(BlueprintPure)
    FString Get_SourceError() const { return _SourceError; }

    private void DoSetErrors(const TArray<FString>& InErrors)
    {
        _SourceError = "";
        for (const auto Error : InErrors)
        { _SourceError = _SourceError.IsEmpty() ? Error : f"{_SourceError}; {Error}"; }
    }

    UFUNCTION()
    private void DoOnAction(const FString& InName)
    {
        if (InName != "Fire" || !ck::IsValid(_Session) || !_Session.Get_IsUsable()) { return; }
        bool Safety = true;
        if (!_Session.TryGetBool("Safety", Safety)) { return; }
        ++_ActionCount;
        if (Safety) { _Session.TrySetText("Status", FText::FromString("SAFETY LOCK")); return; }
        ++_ShotCount;
        _Session.TrySetText("ShotCount", FText::FromString(f"{_ShotCount}"));
        _Session.TrySetText("Status", FText::FromString("SHOT FIRED"));
    }

    UFUNCTION()
    private void DoOnNumberChanged(const FString& InName, float32 InProposedValue)
    {
        if (InName != "Charge" || !ck::IsValid(_Session)) { return; }
        const float32 Accepted = float32(Math::Clamp(InProposedValue, 0.0f, 100.0f));
        if (!_Session.TrySetNumber(InName, Accepted)) { return; }
        ++_NumberEditCount;
        _Session.TrySetText("ChargeLabel", FText::FromString(f"{Math::RoundToInt(Accepted)}%"));
    }

    UFUNCTION()
    private void DoOnBoolChanged(const FString& InName, bool InProposedValue)
    {
        if (InName != "Safety" || !ck::IsValid(_Session) || !_Session.TrySetBool(InName, InProposedValue)) { return; }
        ++_BoolEditCount;
        _Session.TrySetText("Status", FText::FromString(InProposedValue ? "SAFETY LOCK" : "ARMED"));
    }
}

class ACk_ScriptingCard_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY()
    private UCk_ScriptingCard_Widget _Card;

    private bool _PreviousCursor = false;
    private bool _CursorCaptured = false;

    void Request_StartGym() override
    {
        if (ck::IsValid(_Card)) { _Card.Request_Shutdown(); }
        _Card = Cast<UCk_ScriptingCard_Widget>(WidgetBlueprint::CreateWidget(UCk_ScriptingCard_Widget, this));
        if (ck::IsValid(_Card) && _Card.TryInitializeCard())
        {
            if (!_CursorCaptured) { _PreviousCursor = bShowMouseCursor; _CursorCaptured = true; }
            bShowMouseCursor = true;
            _Card.AddToViewport();
        }
    }

    UFUNCTION(BlueprintPure)
    UCk_ScriptingCard_Widget Get_Card() const { return _Card; }

    FString Get_ControlPanelTitle() override { return "SCRIPTED UMG / WEAPON CONSOLE"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        const bool Ready = ck::IsValid(_Card) && ck::IsValid(_Card.Get_Session()) && _Card.Get_Session().Get_IsUsable();
        Rows.Add(CkGym_Control::Header("HTML / CSS + ANGELSCRIPT"));
        Rows.Add(CkGym_Control::Action(EKeys::One, "1", "Reset console", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Two, "2", "Reload HTML / CSS", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Three, "3", "Prove rejected source preserves UI", Ready));
        Rows.Add(CkGym_Control::Status("Controls", "Mouse: charge, safety, fire"));
        Rows.Add(CkGym_Control::Status("Code", "UMG tree + events owned in AngelScript"));
        if (ck::IsValid(_Card))
        {
            const auto Error = _Card.Get_SourceError();
            Rows.Add(CkGym_Control::Status("Source", Error.IsEmpty() ? "accepted" : Error, !Error.IsEmpty()));
            Rows.Add(CkGym_Control::Status("Events", f"Fire {_Card.Get_ActionCount()} / charge {_Card.Get_NumberEditCount()} / safety {_Card.Get_BoolEditCount()}"));
        }
        Rows.Add(CkGym_Control::Status("Gyms", "Tab: selector  H: panel"));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!ck::IsValid(_Card)) { return; }
        if (InRowIndex == 1) { _Card.Request_Reset(); }
        else if (InRowIndex == 2) { _Card.Request_Reload(); }
        else if (InRowIndex == 3) { _Card.TryRejectSource(); }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (ck::IsValid(_Card)) { _Card.Request_Shutdown(); }
        _Card = nullptr;
        if (_CursorCaptured) { bShowMouseCursor = _PreviousCursor; _CursorCaptured = false; }
    }
}

class ACk_ScriptingCard_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_ScriptingCard_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACkGym_ControlPanelHUD;
}
