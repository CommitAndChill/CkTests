// Language=angelscript
// The shell contains art only; the authored surface owns every HUD label and control.
class UCk_DarkFantasyHud_Widget : UUserWidget
{
    UPROPERTY() private UCk_UI_AuthoredSession_UE _Session;
    UPROPERTY() private UCk_UI_AuthoredSurface_UE _Surface;
    UPROPERTY() private UTexture2D _Backdrop;
    UPROPERTY() private UTexture2D _Minimap;
    UPROPERTY() private UTexture2D _FrameTexture;
    UPROPERTY() private UTexture2D _OrbBezel;
    UPROPERTY() private UTexture2D _OrbHealth;
    UPROPERTY() private UTexture2D _OrbEnergy;
    UPROPERTY() private UTexture2D _WheelVignette;
    UPROPERTY() private UTexture2D _AbilityCut;
    UPROPERTY() private UTexture2D _AbilityFlurry;
    UPROPERTY() private UTexture2D _AbilityShadow;
    UPROPERTY() private UTexture2D _AbilityDash;
    UPROPERTY() private UTexture2D _AbilityStrike;
    UPROPERTY() private UTexture2D _AbilityPierce;
    UPROPERTY() private UTexture2D _WheelTaunt;
    UPROPERTY() private UTexture2D _WheelThanks;
    UPROPERTY() private UTexture2D _WheelBye;
    UPROPERTY() private UTexture2D _WheelCheer;
    UPROPERTY() private UTexture2D _WheelFollow;
    UPROPERTY() private UTexture2D _WheelHelp;
    UPROPERTY() private UTexture2D _WheelSorry;
    UPROPERTY() private UTexture2D _WheelEmpty;
    private float32 _Health = 0.82f;
    private float32 _Mana = 0.68f;
    private float32 _SkillCooldown = 0.62f;
    private float32 _ShadowRemaining = 6.2f;
    private float32 _FlurryRemaining = 1.92f;
    private float32 _StrikeRemaining = 9.36f;
    private bool _WheelOpen = true;
    private FString _SelectedKey = "greet";
    private FString _LastAction = "Choose an interaction";
    private FString _StartupError;
    private FString _SourceError;
    private int32 _ActivationCount = 0;
    private int32 _SelectionCount = 0;

    UFUNCTION()
    bool TryInitializeHud()
    {
        if (ck::IsValid(_Session)) { return false; }
        _Session = Cast<UCk_UI_AuthoredSession_UE>(NewObject(this, UCk_UI_AuthoredSession_UE));
        _Surface = Cast<UCk_UI_AuthoredSurface_UE>(ConstructWidget(UCk_UI_AuthoredSurface_UE, n"FantasySurface"));
        auto Scale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"FantasyScale"));
        auto Frame = Cast<USizeBox>(ConstructWidget(USizeBox, n"FantasyFrame"));
        auto Root = Cast<UOverlay>(ConstructWidget(UOverlay, n"FantasyOverlay"));
        auto BackdropScale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"FantasyBackdropScale"));
        auto Image = Cast<UImage>(ConstructWidget(UImage, n"FantasyBackdrop"));
        const auto Directory = DoResourceDirectory();
        _Backdrop = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "dungeon-backdrop.png"));
        _Minimap = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "sewer-minimap.png"));
        _FrameTexture = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "ornamental-hud-frame.png"));
        _OrbBezel = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "orb-bezel.png"));
        _OrbHealth = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "orb-health.png"));
        _OrbEnergy = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "orb-energy.png"));
        _WheelVignette = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "wheel-vignette.png"));
        _AbilityCut = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "ability-cut.png"));
        _AbilityFlurry = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "ability-flurry.png"));
        _AbilityShadow = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "ability-shadow.png"));
        _AbilityDash = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "ability-dash.png"));
        _AbilityStrike = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "ability-strike.png"));
        _AbilityPierce = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "ability-pierce.png"));
        _WheelTaunt = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "wheel-taunt.png"));
        _WheelThanks = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "wheel-thanks.png"));
        _WheelBye = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "wheel-bye.png"));
        _WheelCheer = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "wheel-cheer.png"));
        _WheelFollow = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "wheel-follow.png"));
        _WheelHelp = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "wheel-help.png"));
        _WheelSorry = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "wheel-sorry.png"));
        _WheelEmpty = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "wheel-empty.png"));
        if (!ck::IsValid(_Session) || !ck::IsValid(_Surface) || !ck::IsValid(Scale)
            || !ck::IsValid(Frame) || !ck::IsValid(Root) || !ck::IsValid(BackdropScale)
            || !ck::IsValid(Image) || !ck::IsValid(_Backdrop) || !ck::IsValid(_Minimap) || !ck::IsValid(_FrameTexture)
            || !ck::IsValid(_OrbBezel) || !ck::IsValid(_OrbHealth) || !ck::IsValid(_OrbEnergy)
            || !ck::IsValid(_WheelVignette) || !ck::IsValid(_AbilityCut) || !ck::IsValid(_AbilityFlurry)
            || !ck::IsValid(_AbilityShadow) || !ck::IsValid(_AbilityDash) || !ck::IsValid(_AbilityStrike)
            || !ck::IsValid(_AbilityPierce) || !ck::IsValid(_WheelTaunt) || !ck::IsValid(_WheelThanks)
            || !ck::IsValid(_WheelBye) || !ck::IsValid(_WheelCheer) || !ck::IsValid(_WheelFollow)
            || !ck::IsValid(_WheelHelp) || !ck::IsValid(_WheelSorry) || !ck::IsValid(_WheelEmpty))
        { _StartupError = "Required HUD widget or art construction failed"; Request_Shutdown(); return false; }

        auto Schema = TArray<FCk_UI_AuthoredFieldSchema>();
        Schema.Add(FCk_UI_AuthoredFieldSchema("actor", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("glyph", ECk_UI_AuthoredFieldKind::Text));
        Schema.Add(FCk_UI_AuthoredFieldSchema("angle", ECk_UI_AuthoredFieldKind::Number));
        Schema.Add(FCk_UI_AuthoredFieldSchema("cooldown", ECk_UI_AuthoredFieldKind::Number));
        Schema.Add(FCk_UI_AuthoredFieldSchema("enabled", ECk_UI_AuthoredFieldKind::Bool));
        Schema.Add(FCk_UI_AuthoredFieldSchema("queued", ECk_UI_AuthoredFieldKind::Bool));
        Schema.Add(FCk_UI_AuthoredFieldSchema("image", ECk_UI_AuthoredFieldKind::Image));
        auto Interactions = FCk_UI_AuthoredCollection("Interactions");
        Interactions.Set_Schema(Schema); Interactions.Set_Records(DoRecords());
        auto Collections = TArray<FCk_UI_AuthoredCollection>(); Collections.Add(Interactions);
        auto Texts = TArray<FCk_UI_AuthoredText>();
        Texts.Add(FCk_UI_AuthoredText("Selection", FText::FromString(_SelectedKey)));
        Texts.Add(FCk_UI_AuthoredText("SelectionLabel", FText::FromString("Empty")));
        Texts.Add(FCk_UI_AuthoredText("HealthText", FText::FromString("820 / 1000")));
        Texts.Add(FCk_UI_AuthoredText("ManaText", FText::FromString("340 / 500")));
        Texts.Add(FCk_UI_AuthoredText("ActionStatus", FText::FromString(_LastAction)));
        auto Numbers = TArray<FCk_UI_AuthoredNumber>();
        Numbers.Add(FCk_UI_AuthoredNumber("HealthFraction", _Health));
        Numbers.Add(FCk_UI_AuthoredNumber("ManaFraction", _Mana));
        Numbers.Add(FCk_UI_AuthoredNumber("SkillCooldown", _SkillCooldown));
        Numbers.Add(FCk_UI_AuthoredNumber("CooldownReady", 0.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("CooldownFlurry", _FlurryRemaining / 8.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("CooldownStrike", _StrikeRemaining / 12.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("RemainingReady", 0.0f));
        Numbers.Add(FCk_UI_AuthoredNumber("RemainingShadow", _ShadowRemaining));
        Numbers.Add(FCk_UI_AuthoredNumber("RemainingFlurry", _FlurryRemaining));
        Numbers.Add(FCk_UI_AuthoredNumber("RemainingStrike", _StrikeRemaining));
        auto Bools = TArray<FCk_UI_AuthoredBool>(); Bools.Add(FCk_UI_AuthoredBool("WheelOpen", _WheelOpen));
        auto Images = TArray<FCk_UI_AuthoredImage>();
        auto Map = FCk_UI_AuthoredImage("Minimap"); Map.Set_Texture(_Minimap); Images.Add(Map);
        auto Ornament = FCk_UI_AuthoredImage("HudFrame"); Ornament.Set_Texture(_FrameTexture); Images.Add(Ornament);
        auto Bezel = FCk_UI_AuthoredImage("OrbBezel"); Bezel.Set_Texture(_OrbBezel); Images.Add(Bezel);
        auto HealthArt = FCk_UI_AuthoredImage("OrbHealth"); HealthArt.Set_Texture(_OrbHealth); Images.Add(HealthArt);
        auto EnergyArt = FCk_UI_AuthoredImage("OrbEnergy"); EnergyArt.Set_Texture(_OrbEnergy); Images.Add(EnergyArt);
        auto Vignette = FCk_UI_AuthoredImage("WheelVignette"); Vignette.Set_Texture(_WheelVignette); Images.Add(Vignette);
        auto Cut = FCk_UI_AuthoredImage("AbilityCut"); Cut.Set_Texture(_AbilityCut); Images.Add(Cut);
        auto Flurry = FCk_UI_AuthoredImage("AbilityFlurry"); Flurry.Set_Texture(_AbilityFlurry); Images.Add(Flurry);
        auto Shadow = FCk_UI_AuthoredImage("AbilityShadow"); Shadow.Set_Texture(_AbilityShadow); Images.Add(Shadow);
        auto Dash = FCk_UI_AuthoredImage("AbilityDash"); Dash.Set_Texture(_AbilityDash); Images.Add(Dash);
        auto Strike = FCk_UI_AuthoredImage("AbilityStrike"); Strike.Set_Texture(_AbilityStrike); Images.Add(Strike);
        auto Pierce = FCk_UI_AuthoredImage("AbilityPierce"); Pierce.Set_Texture(_AbilityPierce); Images.Add(Pierce);
        auto Actions = TArray<FString>(); Actions.Add("CloseWheel"); Actions.Add("ActivateSelection");
        Actions.Add("OpenWheel"); Actions.Add("ResetFixture"); Actions.Add("AdvanceCooldown");
        auto ItemActions = TArray<FString>(); ItemActions.Add("SelectInteraction"); ItemActions.Add("ActivateInteraction");
        auto Regions = TArray<FString>(); Regions.Add("main");
        const auto FontDirectory = FPaths::CombinePaths(Directory, "Fonts");
        const auto FallbackFile = FPaths::CombinePaths(FPaths::EngineContentDir(), "Slate/Fonts/DroidSansFallback.ttf");
        auto DisplayFont = FCk_UI_AuthoredFontFamily("fantasy-display",
            FPaths::CombinePaths(FontDirectory, "MarcellusSC-Regular.ttf"));
        DisplayFont.Set_FallbackRegularFile(FallbackFile);
        auto UiFont = FCk_UI_AuthoredFontFamily("fantasy-ui",
            FPaths::CombinePaths(FontDirectory, "SourceSans3-Regular.ttf"));
        UiFont.Set_BoldFile(FPaths::CombinePaths(FontDirectory, "SourceSans3-Bold.ttf"));
        UiFont.Set_FallbackRegularFile(FallbackFile);
        UiFont.Set_FallbackBoldFile(FallbackFile);
        auto Fonts = TArray<FCk_UI_AuthoredFontFamily>(); Fonts.Add(DisplayFont); Fonts.Add(UiFont);
        auto Config = FCk_UI_AuthoredSession_Config();
        Config.Set_Regions(Regions); Config.Set_Text(Texts); Config.Set_Number(Numbers); Config.Set_Bool(Bools);
        Config.Set_Collections(Collections); Config.Set_Images(Images); Config.Set_Actions(Actions);
        Config.Set_ItemActions(ItemActions); Config.Set_FontFamilies(Fonts);
        Config.Set_RegisterHudControls(true); Config.Set_RegisterRadialControls(true);
        auto Errors = TArray<FString>();
        if (!_Session.TryInitialize(Config, Errors))
        { DoErrors(Errors); _StartupError = _SourceError; Request_Shutdown(); return false; }
        _Session.OnAction.AddUFunction(this, n"DoOnAction");
        _Session.OnItemAction.AddUFunction(this, n"DoOnItemAction");
        if (!Request_Reload() || !_Session.TryMount(_Surface, "main", Errors))
        { if (!Errors.IsEmpty()) { DoErrors(Errors); } _StartupError = _SourceError; Request_Shutdown(); return false; }
        Frame.SetWidthOverride(1920.0f); Frame.SetHeightOverride(1080.0f); Frame.AddChild(_Surface);
        Scale.SetStretch(EStretch::ScaleToFit); Scale.AddChild(Frame);
        BackdropScale.SetStretch(EStretch::ScaleToFill); BackdropScale.SetClipping(EWidgetClipping::ClipToBounds);
        BackdropScale.SetVisibility(ESlateVisibility::HitTestInvisible);
        Image.SetBrushFromTexture(_Backdrop, true); BackdropScale.AddChild(Image);
        auto BackdropSlot = Root.AddChildToOverlay(BackdropScale);
        auto ContentSlot = Root.AddChildToOverlay(Scale);
        const bool ArtReady = DoFillSlot(BackdropSlot);
        const bool ContentReady = DoFillSlot(ContentSlot);
        if (!ArtReady || !ContentReady)
        { _StartupError = "Required HUD overlay composition failed"; Request_Shutdown(); return false; }
        SetRootWidget(Root); return true;
    }

    private FString DoResourceDirectory() const
    { return FPaths::CombinePaths(FPaths::ProjectDir(), "Plugins/CkTests/Resources/UiAuthoring/DarkFantasyHudProof"); }
    private bool DoFillSlot(UOverlaySlot InSlot)
    {
        if (!ck::IsValid(InSlot)) { return false; }
        InSlot.SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
        InSlot.SetVerticalAlignment(EVerticalAlignment::VAlign_Fill); return true;
    }
    private bool DoReady() const { return ck::IsValid(_Session) && _Session.Get_IsUsable(); }
    private TArray<FString> DoKeys() const
    {
        auto Keys = TArray<FString>();
        Keys.Add("greet"); Keys.Add("thanks"); Keys.Add("follow"); Keys.Add("wait");
        Keys.Add("help"); Keys.Add("trade"); Keys.Add("inspect"); Keys.Add("leave"); return Keys;
    }
    private FString DoLabel(const FString& InKey) const
    {
        if (InKey == "greet") { return "Empty"; } if (InKey == "thanks") { return "Thanks"; }
        if (InKey == "follow") { return "Bye"; } if (InKey == "wait") { return "Cheer"; }
        if (InKey == "help") { return "Follow"; } if (InKey == "trade") { return "Help"; }
        if (InKey == "inspect") { return "Sorry"; } if (InKey == "leave") { return "Taunt"; } return "";
    }
    private UTexture2D DoWheelArt(int32 InIndex) const
    {
        if (InIndex == 0) { return _WheelEmpty; } if (InIndex == 1) { return _WheelThanks; }
        if (InIndex == 2) { return _WheelBye; } if (InIndex == 3) { return _WheelCheer; }
        if (InIndex == 4) { return _WheelFollow; } if (InIndex == 5) { return _WheelHelp; }
        if (InIndex == 6) { return _WheelSorry; } return _WheelTaunt;
    }
    private TArray<FCk_UI_AuthoredRecord> DoRecords() const
    {
        auto Records = TArray<FCk_UI_AuthoredRecord>(); const auto Keys = DoKeys();
        for (int32 Index = 0; Index < Keys.Num(); ++Index)
        {
            auto Fields = TArray<FCk_UI_AuthoredFieldValue>();
            auto Actor = FCk_UI_AuthoredFieldValue("actor", ECk_UI_AuthoredFieldKind::Text);
            Actor.Set_Text(FText::FromString(DoLabel(Keys[Index]))); Fields.Add(Actor);
            auto Glyph = FCk_UI_AuthoredFieldValue("glyph", ECk_UI_AuthoredFieldKind::Text);
            Glyph.Set_Text(FText::FromString(f"{Index + 1}")); Fields.Add(Glyph);
            auto Angle = FCk_UI_AuthoredFieldValue("angle", ECk_UI_AuthoredFieldKind::Number);
            Angle.Set_Number(float32(Index * 45 - 90)); Fields.Add(Angle);
            auto Cooldown = FCk_UI_AuthoredFieldValue("cooldown", ECk_UI_AuthoredFieldKind::Number);
            Cooldown.Set_Number(0.0f); Fields.Add(Cooldown);
            auto Enabled = FCk_UI_AuthoredFieldValue("enabled", ECk_UI_AuthoredFieldKind::Bool);
            Enabled.Set_Bool(true); Fields.Add(Enabled);
            auto Queued = FCk_UI_AuthoredFieldValue("queued", ECk_UI_AuthoredFieldKind::Bool);
            Queued.Set_Bool(false); Fields.Add(Queued);
            auto Art = FCk_UI_AuthoredFieldValue("image", ECk_UI_AuthoredFieldKind::Image);
            Art.Set_Texture(DoWheelArt(Index)); Fields.Add(Art);
            auto Record = FCk_UI_AuthoredRecord(Keys[Index]); Record.Set_Fields(Fields); Records.Add(Record);
        }
        return Records;
    }
    UFUNCTION()
    bool Request_ShowWheel(bool InShow = true)
    {
        if (!DoReady() || !_Session.TrySetBool("WheelOpen", InShow)) { return false; }
        _WheelOpen = InShow; return true;
    }
    UFUNCTION()
    bool Request_ToggleWheel() { return Request_ShowWheel(!_WheelOpen); }
    UFUNCTION()
    bool Request_Damage(float32 InAmount = 0.15f)
    { if (!Math::IsFinite(InAmount) || InAmount < 0.0f) { return false; } return DoSetHealth(_Health - InAmount); }
    UFUNCTION()
    bool Request_Heal(float32 InAmount = 0.15f)
    { if (!Math::IsFinite(InAmount) || InAmount < 0.0f) { return false; } return DoSetHealth(_Health + InAmount); }
    private bool DoSetHealth(float32 InValue)
    {
        if (!DoReady()) { return false; }
        const auto Candidate = Math::Clamp(InValue, 0.0f, 1.0f);
        if (!_Session.TrySetNumber("HealthFraction", Candidate)) { return false; }
        _Health = Candidate; _Session.TrySetText("HealthText", FText::FromString(f"{Math::RoundToInt(_Health * 1000)} / 1000")); return true;
    }
    UFUNCTION()
    bool Request_ChangeMana(float32 InDelta = -0.10f)
    {
        if (!DoReady() || !Math::IsFinite(InDelta)) { return false; }
        const auto Candidate = Math::Clamp(_Mana + InDelta, 0.0f, 1.0f);
        if (!_Session.TrySetNumber("ManaFraction", Candidate)) { return false; }
        _Mana = Candidate; _Session.TrySetText("ManaText", FText::FromString(f"{Math::RoundToInt(_Mana * 500)} / 500")); return true;
    }
    UFUNCTION()
    bool Request_AdvanceCooldown()
    {
        if (!DoReady()) { return false; }
        return DoPublishCooldowns(10.0f, _FlurryRemaining, _StrikeRemaining);
    }
    private bool DoPublishCooldowns(float32 InShadow, float32 InFlurry, float32 InStrike)
    {
        if (!DoReady() || !Math::IsFinite(InShadow) || !Math::IsFinite(InFlurry)
            || !Math::IsFinite(InStrike) || InShadow < 0.0f || InShadow > 10.0f
            || InFlurry < 0.0f || InFlurry > 8.0f || InStrike < 0.0f || InStrike > 12.0f)
        { return false; }
        if (!_Session.TrySetNumber("RemainingShadow", InShadow)
            || !_Session.TrySetNumber("RemainingFlurry", InFlurry)
            || !_Session.TrySetNumber("RemainingStrike", InStrike)
            || !_Session.TrySetNumber("SkillCooldown", InShadow / 10.0f)
            || !_Session.TrySetNumber("CooldownFlurry", InFlurry / 8.0f)
            || !_Session.TrySetNumber("CooldownStrike", InStrike / 12.0f))
        {
            // Retire the whole surface if a required binding disappears; never run a partial timer update.
            _SourceError = "Required cooldown binding update failed";
            Request_Shutdown();
            return false;
        }
        _ShadowRemaining = InShadow;
        _FlurryRemaining = InFlurry;
        _StrikeRemaining = InStrike;
        _SkillCooldown = InShadow / 10.0f;
        return true;
    }
    UFUNCTION()
    bool Request_Select(const FString& InKey)
    {
        if (!DoReady() || !_WheelOpen || DoLabel(InKey).IsEmpty()) { return false; }
        if (!_Session.TrySetText("Selection", FText::FromString(InKey))) { return false; }
        _SelectedKey = InKey; ++_SelectionCount;
        _Session.TrySetText("SelectionLabel", FText::FromString(DoLabel(InKey))); return true;
    }
    UFUNCTION()
    bool Request_Activate()
    {
        if (!DoReady() || !_WheelOpen || DoLabel(_SelectedKey).IsEmpty()) { return false; }
        // Re-read live admission state; rejected/disabled records cannot activate through script either.
        auto Records = TArray<FCk_UI_AuthoredRecord>();
        if (!_Session.TryGetRecords("Interactions", Records)) { return false; }
        bool Allowed = false;
        for (const auto Record : Records)
        {
            if (Record.Get_Key() != _SelectedKey) { continue; }
            bool Enabled = false; bool Ready = false;
            for (const auto Field : Record.Get_Fields())
            {
                if (Field.Get_Name() == "enabled") { Enabled = Field.Get_Bool(); }
                else if (Field.Get_Name() == "cooldown") { Ready = Field.Get_Number() <= 0.0f; }
            }
            Allowed = Enabled && Ready;
        }
        if (!Allowed) { return false; }
        const auto Candidate = f"You: {DoLabel(_SelectedKey)}";
        if (!_Session.TrySetText("ActionStatus", FText::FromString(Candidate))) { return false; }
        _LastAction = Candidate; ++_ActivationCount; return true;
    }
    UFUNCTION()
    bool Request_Reset()
    {
        if (!DoReady()) { return false; }
        auto Errors = TArray<FString>();
        if (!_Session.TrySetRecords("Interactions", DoRecords(), Errors)) { DoErrors(Errors); return false; }
        DoSetHealth(0.82f); Request_ChangeMana(0.68f - _Mana); Request_ShowWheel(true); Request_Select("greet");
        if (!DoPublishCooldowns(6.2f, 1.92f, 9.36f)) { return false; }
        _LastAction = "Choose an interaction"; _Session.TrySetText("ActionStatus", FText::FromString(_LastAction));
        _ActivationCount = 0; _SelectionCount = 0; return true;
    }
    UFUNCTION()
    bool Request_Reload()
    {
        if (!DoReady()) { return false; } auto Errors = TArray<FString>(); const auto Directory = DoResourceDirectory();
        const bool Accepted = _Session.TryReloadFiles(FPaths::CombinePaths(Directory, "DarkFantasyHudProof.ui.html"),
            FPaths::CombinePaths(Directory, "DarkFantasyHudProof.ui.css"), Errors);
        DoErrors(Errors); return Accepted;
    }
    UFUNCTION()
    bool Request_InvalidSource()
    {
        if (!DoReady()) { return false; } auto Errors = TArray<FString>();
        const bool Accepted = _Session.TryReload("<ui version=\"1\"><region name=\"main\"><hud-meter id=\"invalid\" value-bind=\"MissingHealth\"/></region></ui>", "", Errors);
        DoErrors(Errors); return !Accepted && !Errors.IsEmpty();
    }
    UFUNCTION()
    private void DoOnAction(const FString& InAction)
    {
        if (InAction == "CloseWheel") { Request_ShowWheel(false); }
        else if (InAction == "OpenWheel") { Request_ShowWheel(true); }
        else if (InAction == "ActivateSelection") { Request_Activate(); }
        else if (InAction == "ResetFixture") { Request_Reset(); }
        else if (InAction == "AdvanceCooldown") { Request_AdvanceCooldown(); }
    }
    UFUNCTION()
    private void DoOnItemAction(const FString& InAction, const FString& InKey)
    {
        if (InAction == "SelectInteraction") { Request_Select(InKey); }
        else if (InAction == "ActivateInteraction") { if (Request_Select(InKey)) { Request_Activate(); } }
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
        if (ck::IsValid(_Surface)) { _Surface.ReleaseView(); } RemoveFromParent();
    }
    UFUNCTION(BlueprintOverride)
    void Tick(FGeometry InGeometry, float InDeltaTime)
    {
        if (!DoReady() || !Math::IsFinite(InDeltaTime) || InDeltaTime <= 0.0f)
        { return; }
        const float32 Delta = float32(Math::Clamp(InDeltaTime, 0.0, 12.0));
        const float32 Shadow = Math::Max(0.0f, _ShadowRemaining - Delta);
        const float32 Flurry = Math::Max(0.0f, _FlurryRemaining - Delta);
        const float32 Strike = Math::Max(0.0f, _StrikeRemaining - Delta);
        if (Shadow == _ShadowRemaining && Flurry == _FlurryRemaining && Strike == _StrikeRemaining)
        { return; }
        DoPublishCooldowns(Shadow, Flurry, Strike);
    }
    UFUNCTION(BlueprintOverride)
    void Destruct() { if (ck::IsValid(_Session)) { _Session.Request_Release(); } }
    UFUNCTION(BlueprintPure) UCk_UI_AuthoredSession_UE Get_Session() const { return _Session; }
    UFUNCTION(BlueprintPure) float32 Get_HealthFraction() const { return _Health; }
    UFUNCTION(BlueprintPure) float32 Get_ManaFraction() const { return _Mana; }
    UFUNCTION(BlueprintPure) bool Get_IsWheelOpen() const { return _WheelOpen; }
    UFUNCTION(BlueprintPure) bool Get_WheelOpen() const { return _WheelOpen; }
    UFUNCTION(BlueprintPure) FString Get_SelectedKey() const { return _SelectedKey; }
    UFUNCTION(BlueprintPure) FString Get_LastAction() const { return _LastAction; }
    UFUNCTION(BlueprintPure) FString Get_SourceError() const { return _SourceError; }
    UFUNCTION(BlueprintPure) FString Get_StartupError() const { return _StartupError; }
    UFUNCTION(BlueprintPure) int32 Get_ActivationCount() const { return _ActivationCount; }
    UFUNCTION(BlueprintPure) int32 Get_SelectionCount() const { return _SelectionCount; }
}

class ACk_DarkFantasyHud_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY() private UCk_DarkFantasyHud_Widget _Hud;
    private bool _PreviousCursor = false;
    private bool _CursorCaptured = false;
    void Request_StartGym() override
    {
        if (ck::IsValid(_Hud)) { _Hud.Request_Shutdown(); }
        _Hud = Cast<UCk_DarkFantasyHud_Widget>(WidgetBlueprint::CreateWidget(UCk_DarkFantasyHud_Widget, this));
        if (ck::IsValid(_Hud) && _Hud.TryInitializeHud())
        {
            if (!_CursorCaptured) { _PreviousCursor = bShowMouseCursor; _CursorCaptured = true; }
            bShowMouseCursor = true; _Hud.AddToViewport();
        }
    }
    UFUNCTION(BlueprintPure) UCk_DarkFantasyHud_Widget Get_Hud() const { return _Hud; }
    FString Get_ControlPanelTitle() override { return "DARK FANTASY / SCRIPTED HUD"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>(); const bool Ready = ck::IsValid(_Hud) && ck::IsValid(_Hud.Get_Session()) && _Hud.Get_Session().Get_IsUsable();
        Rows.Add(CkGym_Control::Header("AUTHORED HUD / LIVE STATE"));
        Rows.Add(CkGym_Control::Action(EKeys::One, "1", "Toggle interaction wheel", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Two, "2", "Take damage", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Three, "3", "Heal", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Four, "4", "Spend mana", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Five, "5", "Select next interaction", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Six, "6", "Activate selection", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Seven, "7", "Reset fixture", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Eight, "8", "Reload HTML / CSS", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Nine, "9", "Reject invalid source", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Zero, "0", "Restart shadow cooldown (10s)", Ready));
        Rows.Add(CkGym_Control::Status("Wheel", "Mouse hover/select; click/Enter activate"));
        if (ck::IsValid(_Hud)) { const auto Error = _Hud.Get_SourceError(); Rows.Add(CkGym_Control::Status("Source", Error.IsEmpty() ? "accepted" : Error, !Error.IsEmpty())); }
        Rows.Add(CkGym_Control::Status("Gyms", "Tab: selector  H: panel")); return Rows;
    }
    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!ck::IsValid(_Hud)) { return; }
        if (InRowIndex == 1) { _Hud.Request_ShowWheel(!_Hud.Get_IsWheelOpen()); }
        else if (InRowIndex == 2) { _Hud.Request_Damage(); }
        else if (InRowIndex == 3) { _Hud.Request_Heal(); }
        else if (InRowIndex == 4) { _Hud.Request_ChangeMana(); }
        else if (InRowIndex == 5)
        {
            auto Keys = TArray<FString>(); Keys.Add("greet"); Keys.Add("thanks"); Keys.Add("follow"); Keys.Add("wait");
            Keys.Add("help"); Keys.Add("trade"); Keys.Add("inspect"); Keys.Add("leave");
            int32 Next = 0; for (int32 Index = 0; Index < Keys.Num(); ++Index) { if (Keys[Index] == _Hud.Get_SelectedKey()) { Next = (Index + 1) % Keys.Num(); } }
            _Hud.Request_Select(Keys[Next]);
        }
        else if (InRowIndex == 6) { _Hud.Request_Activate(); }
        else if (InRowIndex == 7) { _Hud.Request_Reset(); }
        else if (InRowIndex == 8) { _Hud.Request_Reload(); }
        else if (InRowIndex == 9) { _Hud.Request_InvalidSource(); }
        else if (InRowIndex == 10) { _Hud.Request_AdvanceCooldown(); }
    }
    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (ck::IsValid(_Hud)) { _Hud.Request_Shutdown(); } _Hud = nullptr;
        if (_CursorCaptured) { bShowMouseCursor = _PreviousCursor; _CursorCaptured = false; }
    }
}
class ACk_DarkFantasyHud_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_DarkFantasyHud_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACkGym_ControlPanelHUD;
}
