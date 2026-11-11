// Language=angelscript

class UCk_ScriptedPause_Widget : UCk_ActivatableWidget_UE
{
    default bIsFocusable = true;
    default bAutoActivate = false;

    UPROPERTY()
    private UCk_UI_AuthoredSession_UE _Session;
    UPROPERTY()
    private UCk_UI_AuthoredSurface_UE _Surface;
    UPROPERTY()
    private UCk_UI_LayerMembership_UE _Membership;
    UPROPERTY()
    private UCk_PauseMenuProof_UE _SettingsChild;
    UPROPERTY()
    private UTexture2D _Backdrop;
    UPROPERTY()
    private FCk_UiAuthoringProof_Settings _AppliedSettings;

    private TWeakObjectPtr<APlayerController> _Controller;
    private TWeakObjectPtr<UWorld> _World;
    private TWeakObjectPtr<UCk_UI_Layout_Subsystem_UE> _Subsystem;
    private TWeakObjectPtr<UCk_UI_PrimaryGameLayout_UE> _Layout;
    private TWeakObjectPtr<UCk_UI_LayerStack_UE> _Layer;
    private TMap<FName, int32> _ActionCounts;
    private FString _SourceError;
    private bool _Opening = false;
    private bool _Committed = false;
    private bool _Terminal = false;
    private bool _OwnsPause = false;
    private bool _ChangingChild = false;
    private bool _CreatedLayout = false;
    private float _PollElapsed = 0.0;
    private FString _LastMarkup;
    private FString _LastStylesheet;

    UFUNCTION()
    bool TryOpenWithSettings(FCk_UiAuthoringProof_Settings InSettings)
    {
        if (_Opening || _Committed || _Terminal) { return false; }
        if (!Math::IsFinite(InSettings.Get_Volume()) || InSettings.Get_Volume() < 0.0
            || InSettings.Get_Volume() > 100.0) { return false; }
        const auto Mode = InSettings.Get_DisplayMode();
        if (Mode != "Windowed" && Mode != "Borderless" && Mode != "Fullscreen") { return false; }
        _AppliedSettings = InSettings;
        return TryOpen();
    }

    UFUNCTION()
    bool TryOpen()
    {
        if (_Opening || _Committed || _Terminal) { return false; }
        _Opening = true;
        auto Controller = GetOwningPlayer();
        if (!ck::IsValid(Controller) || !Controller.IsLocalController())
        { return DoRejectEntry("Pause requires a local owning controller"); }
        auto OwningWorld = Controller.GetWorld();
        if (!ck::IsValid(OwningWorld) || GetWorld() != OwningWorld)
        { return DoRejectEntry("Pause requires a consistent live world"); }
        auto Subsystem = UCk_UI_Layout_Subsystem_UE::Get(Controller);
        if (!ck::IsValid(Subsystem)) { return DoRejectEntry("Pause layout subsystem is unavailable"); }
        const auto Tag = DoLayerTag();
        if (!Tag.IsValid()) { return DoRejectEntry("Pause layer tag is unavailable"); }
        if (Subsystem.Has_Layout())
        {
            auto ExistingLayer = Subsystem.Get_Layer(Tag);
            if (!ck::IsValid(ExistingLayer) || ExistingLayer.HasWidgets())
            { return DoRejectEntry("Pause cannot replace another admitted screen or layout"); }
        }
        _Controller = Controller;
        _World = OwningWorld;
        _Subsystem = Subsystem;
        if (!DoPrepare()) { return DoRejectEntry(_SourceError); }
        if (!Subsystem.Has_Layout())
        {
            Subsystem.CreateLayout(ck_ui_authoring::CkTests_PauseMenuProof_Layout);
            _CreatedLayout = true;
        }
        auto Layout = Subsystem.Get_Layout();
        auto Layer = Subsystem.Get_Layer(Tag);
        if (!ck::IsValid(Layout) || !ck::IsValid(Layer) || Layer.HasWidgets())
        { return DoRejectEntry("Pause layout changed during preparation"); }
        _Layout = Layout;
        _Layer = Layer;
        // Entry callbacks stay inert until the realized stack admission is verified.
        const auto Pushed = Subsystem.PushWidgetToLayer_Instance(Tag, this);
        if (_Terminal || Pushed != this || Subsystem.Get_Layout() != Layout
            || Subsystem.Get_Layer(Tag) != Layer || Layer.GetActiveWidget() != this || !IsActivated())
        { return DoRejectEntry("Pause was rejected by its exact layer"); }
        _Membership = Cast<UCk_UI_LayerMembership_UE>(NewObject(this, UCk_UI_LayerMembership_UE));
        if (!ck::IsValid(_Membership)) { return DoRejectEntry("Pause membership construction failed"); }
        auto Errors = TArray<FString>();
        if (!_Membership.TryObserve(this, Tag, Errors))
        { DoSetErrors(Errors); return DoRejectEntry(_SourceError); }
        _Membership.OnRetired.AddUFunction(this, n"DoOnRetired");
        if (!Gameplay::IsGamePaused())
        {
            _OwnsPause = Gameplay::SetGamePaused(true);
            if (!_OwnsPause) { return DoRejectEntry("Pause could not acquire game pause"); }
        }
        _Committed = true;
        _Opening = false;
        if (!DoBeginInput() || !Get_IsCurrent() || !IsActivated())
        { return DoRejectEntry("Pause input or focus changed during entry"); }
        return true;
    }

    private bool DoPrepare()
    {
        const auto Directory = DoResourceDirectory();
        _Backdrop = Rendering::ImportFileAsTexture2D(FPaths::CombinePaths(Directory, "pause-menu-backdrop.png"));
        if (!ck::IsValid(_Backdrop)) { _SourceError = "Pause backdrop could not load"; return false; }
        _Session = Cast<UCk_UI_AuthoredSession_UE>(NewObject(this, UCk_UI_AuthoredSession_UE));
        _Surface = Cast<UCk_UI_AuthoredSurface_UE>(ConstructWidget(UCk_UI_AuthoredSurface_UE, n"AuthoredPause"));
        if (!ck::IsValid(_Session) || !ck::IsValid(_Surface))
        { _SourceError = "Pause session or surface construction failed"; return false; }
        auto Config = FCk_UI_AuthoredSession_Config();
        Config.Set_InteractionOwner(nullptr);
        if (Config.Get_InteractionOwner() != nullptr)
        { _SourceError = "Pause config could not clear its interaction owner"; return false; }
        Config.Set_InteractionOwner(this);
        if (Config.Get_InteractionOwner() != this)
        { _SourceError = "Pause config did not retain its exact interaction owner"; return false; }
        auto Texts = TArray<FCk_UI_AuthoredText>();
        Texts.Add(FCk_UI_AuthoredText("PauseTitle", FText::FromString("PAUSED")));
        Texts.Add(FCk_UI_AuthoredText("PauseSubtitle", FText::FromString("The game is paused.")));
        Texts.Add(FCk_UI_AuthoredText("PauseResume", FText::FromString("Resume")));
        Texts.Add(FCk_UI_AuthoredText("PauseSettings", FText::FromString("Settings")));
        Texts.Add(FCk_UI_AuthoredText("PauseReturnToTitle", FText::FromString("Return to title")));
        Texts.Add(FCk_UI_AuthoredText("InputResumeKey", FText::FromString("Esc")));
        Texts.Add(FCk_UI_AuthoredText("InputResumeHint", FText::FromString("Resume")));
        Config.Set_Text(Texts);
        auto Bools = TArray<FCk_UI_AuthoredBool>();
        Bools.Add(FCk_UI_AuthoredBool("ResumeEnabled", true));
        Bools.Add(FCk_UI_AuthoredBool("SettingsEnabled", true));
        Bools.Add(FCk_UI_AuthoredBool("ReturnEnabled", true));
        Config.Set_Bool(Bools);
        auto Actions = TArray<FString>();
        Actions.Add("ResumeRequested"); Actions.Add("SettingsRequested"); Actions.Add("ReturnToTitleRequested");
        Config.Set_Actions(Actions);
        auto Required = TArray<FCk_UI_AuthoredButtonRequirement>();
        Required.Add(FCk_UI_AuthoredButtonRequirement("resume-button", "ResumeRequested", "ResumeEnabled"));
        Required.Add(FCk_UI_AuthoredButtonRequirement("settings-button", "SettingsRequested", "SettingsEnabled"));
        Required.Add(FCk_UI_AuthoredButtonRequirement("return-button", "ReturnToTitleRequested", "ReturnEnabled"));
        Config.Set_RequiredButtons(Required);
        const auto FontDirectory = FPaths::CombinePaths(FPaths::ProjectDir(),
            "Plugins/CkTests/Resources/UiAuthoring/DarkFantasyHudProof/Fonts");
        const auto FallbackFile = FPaths::CombinePaths(FPaths::EngineContentDir(), "Slate/Fonts/DroidSansFallback.ttf");
        auto UiFont = FCk_UI_AuthoredFontFamily("menu-ui",
            FPaths::CombinePaths(FontDirectory, "SourceSans3-Regular.ttf"));
        UiFont.Set_BoldFile(FPaths::CombinePaths(FontDirectory, "SourceSans3-Bold.ttf"));
        UiFont.Set_FallbackRegularFile(FallbackFile);
        UiFont.Set_FallbackBoldFile(FallbackFile);
        auto DisplayFont = FCk_UI_AuthoredFontFamily("menu-display",
            FPaths::CombinePaths(FontDirectory, "SourceSans3-Regular.ttf"));
        DisplayFont.Set_BoldFile(FPaths::CombinePaths(FontDirectory, "SourceSans3-Bold.ttf"));
        DisplayFont.Set_FallbackRegularFile(FallbackFile);
        DisplayFont.Set_FallbackBoldFile(FallbackFile);
        auto Fonts = TArray<FCk_UI_AuthoredFontFamily>(); Fonts.Add(UiFont); Fonts.Add(DisplayFont);
        Config.Set_FontFamilies(Fonts);
        Config.Set_RegisterHudControls(true);
        auto Errors = TArray<FString>();
        if (!_Session.TryInitialize(Config, Errors)) { DoSetErrors(Errors); return false; }
        _Session.OnAction.AddUFunction(this, n"DoOnAction");
        if (!DoLoadSource()) { return false; }

        auto Root = Cast<UOverlay>(ConstructWidget(UOverlay, n"PauseOverlay"));
        auto BackdropScale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"BackdropScale"));
        auto Image = Cast<UImage>(ConstructWidget(UImage, n"PauseBackdrop"));
        auto Dimmer = Cast<UBorder>(ConstructWidget(UBorder, n"PauseDimmer"));
        auto Safe = Cast<USafeZone>(ConstructWidget(USafeZone, n"SafeContent"));
        auto Scale = Cast<UScaleBox>(ConstructWidget(UScaleBox, n"ReferenceScale"));
        auto Frame = Cast<USizeBox>(ConstructWidget(USizeBox, n"ReferenceViewport"));
        if (!ck::IsValid(Root) || !ck::IsValid(BackdropScale) || !ck::IsValid(Image)
            || !ck::IsValid(Dimmer) || !ck::IsValid(Safe) || !ck::IsValid(Scale) || !ck::IsValid(Frame))
        { _SourceError = "Pause UMG composition failed"; return false; }
        BackdropScale.SetStretch(EStretch::ScaleToFill);
        BackdropScale.SetClipping(EWidgetClipping::ClipToBounds);
        BackdropScale.SetVisibility(ESlateVisibility::HitTestInvisible);
        Image.SetBrushFromTexture(_Backdrop, true);
        BackdropScale.AddChild(Image);
        DoFillSlot(Root.AddChildToOverlay(BackdropScale));
        Dimmer.SetBrushColor(FLinearColor(0.002f, 0.003f, 0.005f, 0.28f));
        Dimmer.SetVisibility(ESlateVisibility::HitTestInvisible);
        DoFillSlot(Root.AddChildToOverlay(Dimmer));
        DoFillSlot(Root.AddChildToOverlay(Safe));
        Scale.SetStretch(EStretch::ScaleToFit);
        Safe.AddChild(Scale);
        Frame.SetWidthOverride(1920.0f);
        Frame.SetHeightOverride(1080.0f);
        Scale.AddChild(Frame);
        Frame.AddChild(_Surface);
        SetRootWidget(Root);
        if (!_Session.TryMount(_Surface, "main", Errors)) { DoSetErrors(Errors); return false; }
        return true;
    }

    private void DoFillSlot(UOverlaySlot InSlot)
    {
        if (!ck::IsValid(InSlot)) { return; }
        InSlot.SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
        InSlot.SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
    }

    private FGameplayTag DoLayerTag() const
    { return utils_gameplay_tag::ResolveGameplayTag(n"CkTests.UI.Layer.PauseMenuProof", "Script Pause"); }
    private FString DoResourceDirectory() const
    { return FPaths::CombinePaths(FPaths::ProjectDir(), "Plugins/CkTests/Resources/UiAuthoring/PauseMenuProof"); }
    private bool DoLoadSource()
    {
        FString Markup;
        FString Stylesheet;
        const auto Directory = DoResourceDirectory();
        if (!FFileHelper::LoadFileToString(Markup, FPaths::CombinePaths(Directory, "PauseMenuProof.ui.html"))
            || !FFileHelper::LoadFileToString(Stylesheet, FPaths::CombinePaths(Directory, "PauseMenuProof.ui.css")))
        { _SourceError = "Pause source files could not load"; return false; }
        _LastMarkup = Markup;
        _LastStylesheet = Stylesheet;
        return Request_ApplySource(Markup, Stylesheet);
    }

    UFUNCTION()
    bool Request_ApplySource(const FString& InMarkup, const FString& InStylesheet)
    {
        if (_Terminal || !ck::IsValid(_Session) || !_Session.Get_IsUsable()) { return false; }
        auto Errors = TArray<FString>();
        const bool Accepted = _Session.TryReload(InMarkup, InStylesheet, Errors);
        DoSetErrors(Errors);
        return Accepted;
    }

    UFUNCTION()
    bool Request_SetActionEnabled(FName InAction, bool InEnabled)
    {
        if (_Terminal || !ck::IsValid(_Session)) { return false; }
        const auto Binding = DoEnabledBinding(InAction);
        if (Binding.IsEmpty()) { return false; }
        FString Id;
        if (InAction == n"ResumeRequested") { Id = "resume-button"; }
        else if (InAction == n"SettingsRequested") { Id = "settings-button"; }
        else { Id = "return-button"; }
        const bool WasFocused = _Session.Get_IsButtonFocused(Id);
        const bool Accepted = _Session.TrySetBool(Binding, InEnabled);
        if (Accepted && !InEnabled && WasFocused && Get_IsCurrent() && IsActivated())
        { _Session.TryNavigateButtons(DoButtonIds(), 1); }
        return Accepted;
    }
    private FString DoEnabledBinding(FName InAction) const
    {
        if (InAction == n"ResumeRequested") { return "ResumeEnabled"; }
        if (InAction == n"SettingsRequested") { return "SettingsEnabled"; }
        if (InAction == n"ReturnToTitleRequested") { return "ReturnEnabled"; }
        return "";
    }
    private TArray<FString> DoButtonIds() const
    {
        auto Ids = TArray<FString>();
        Ids.Add("resume-button"); Ids.Add("settings-button"); Ids.Add("return-button");
        return Ids;
    }
    UFUNCTION()
    bool Request_SetLabel(FName InBinding, FText InText)
    { return !_Terminal && ck::IsValid(_Session) && _Session.TrySetText(InBinding.ToString(), InText); }
    UFUNCTION(BlueprintPure)
    UCk_UI_AuthoredSession_UE Get_Session() const { return _Session; }
    UFUNCTION(BlueprintPure)
    UCk_UI_LayerMembership_UE Get_Membership() const { return _Membership; }
    UFUNCTION(BlueprintPure)
    UCk_PauseMenuProof_UE Get_SettingsChild() const { return _SettingsChild; }
    UFUNCTION(BlueprintPure)
    FCk_UiAuthoringProof_Settings Get_AppliedSettings() const
    { return ck::IsValid(_SettingsChild) ? _SettingsChild.Get_AppliedSettings() : _AppliedSettings; }
    UFUNCTION(BlueprintPure)
    FString Get_SourceError() const { return _SourceError; }
    UFUNCTION(BlueprintPure)
    bool Get_IsCurrent() const
    {
        return _Committed && !_Terminal && ck::IsValid(_Membership) && _Membership.Get_IsCurrent()
            && ck::IsValid(_Session) && _Session.Get_IsUsable();
    }
    UFUNCTION(BlueprintPure)
    int32 Get_ActionCount(FName InAction) const
    { int32 Count = 0; _ActionCounts.Find(InAction, Count); return Count; }

    UFUNCTION()
    bool Request_ShowSettings()
    {
        if (_ChangingChild || !Get_IsCurrent() || !IsActivated()) { return false; }
        if (ck::IsValid(_SettingsChild)) { _AppliedSettings = _SettingsChild.Get_AppliedSettings(); }
        auto Errors = TArray<FString>();
        auto Child = UCk_PauseMenuProof_UE::TryPrepareSettingsChild(_Controller.Get(), this, _AppliedSettings, Errors);
        if (!ck::IsValid(Child)) { DoSetErrors(Errors); return false; }
        _SettingsChild = Child;
        _ChangingChild = true;
        _Session.Request_EndInput();
        if (!Get_IsCurrent() || !IsActivated() || _SettingsChild != Child)
        {
            Child.Request_Close();
            _ChangingChild = false;
            return false;
        }
        const bool Pushed = Child.TryPushPreparedSettingsChild(Errors);
        _ChangingChild = false;
        if (!Pushed)
        {
            Child.Request_Close();
            DoSetErrors(Errors);
            if (Get_IsCurrent() && IsActivated()) { DoBeginInput(); }
            return false;
        }
        return Get_IsCurrent() && _SettingsChild == Child && Child.IsActivated();
    }

    private bool DoBeginInput()
    {
        if (!Get_IsCurrent() || !IsActivated() || _ChangingChild) { return false; }
        if (!_Session.TryBeginInput()) { _SourceError = "Pause input admission failed"; return false; }
        return _Session.TryFocusButton("resume-button") && Get_IsCurrent();
    }
    UFUNCTION(BlueprintOverride)
    void OnActivated()
    {
        if (_Opening || _Terminal || !_Committed || _ChangingChild) { return; }
        if (!Get_IsCurrent()) { Request_Close(); return; }
        if (ck::IsValid(_SettingsChild))
        {
            _AppliedSettings = _SettingsChild.Get_AppliedSettings();
            if (!_SettingsChild.IsActivated()) { _SettingsChild = nullptr; }
        }
        if (!DoBeginInput()) { Request_Close(); }
    }
    UFUNCTION(BlueprintOverride)
    void OnDeactivated()
    { if (ck::IsValid(_Session)) { _Session.Request_EndInput(!_Terminal); } }
    UFUNCTION(BlueprintOverride)
    void Destruct()
    { DoRetire(false); }
    UFUNCTION()
    private void DoOnRetired(UCk_UI_LayerMembership_UE InMembership)
    { if (InMembership == _Membership) { DoRetire(false); } }

    UFUNCTION()
    void Request_Close()
    { DoRetire(true); }
    private void DoRetire(bool InRemove)
    {
        if (_Terminal) { return; }
        _Terminal = true;
        _Committed = false;
        _Opening = false;
        auto RemovalMembership = _Membership;
        auto Subsystem = _Subsystem.Get();
        auto Layer = _Layer.Get();
        // A rejected entry may have reached its layer before observing it.
        // Capture removal authority before any callback-capable teardown.
        if (InRemove && !ck::IsValid(RemovalMembership) && ck::IsValid(Subsystem)
            && ck::IsValid(Layer) && Subsystem.Get_Layout() == _Layout.Get()
            && Subsystem.Get_Layer(DoLayerTag()) == Layer && Layer.ContainsWidget(this))
        {
            RemovalMembership = Cast<UCk_UI_LayerMembership_UE>(NewObject(this, UCk_UI_LayerMembership_UE));
            auto RemovalErrors = TArray<FString>();
            if (ck::IsValid(RemovalMembership) && !RemovalMembership.TryObserve(this, DoLayerTag(), RemovalErrors))
            { RemovalMembership.Request_Stop(); }
        }
        // CommonUI owns terminal focus handoff. Drain input before callbacks;
        // restoring our saved focus here would reenter a still-listed parent.
        if (ck::IsValid(_Session)) { _Session.Request_EndInput(false); }
        const bool ReleasePause = _OwnsPause;
        _OwnsPause = false;
        auto Controller = _Controller.Get();
        if (ReleasePause && ck::IsValid(Controller) && Controller.GetWorld() == _World.Get())
        {
            auto WorldContext = FAngelscriptGameThreadScopeWorldContext(Controller);
            Gameplay::SetGamePaused(false);
        }
        if (ck::IsValid(_SettingsChild))
        {
            _AppliedSettings = _SettingsChild.Get_AppliedSettings();
            _SettingsChild.Request_Close();
        }
        if (ck::IsValid(_Session)) { _Session.Request_Release(); }
        if (ck::IsValid(_Surface)) { _Surface.ReleaseView(); }
        if (InRemove && ck::IsValid(Subsystem) && ck::IsValid(Layer)
            && Subsystem.Get_Layout() == _Layout.Get() && ck::IsValid(RemovalMembership)
            && RemovalMembership.Get_IsCurrent())
        { Subsystem.RemoveWidget(this); }
        if (ck::IsValid(RemovalMembership)) { RemovalMembership.Request_Stop(); }
    }
    private bool DoRejectEntry(const FString& InError)
    {
        if (!InError.IsEmpty()) { _SourceError = InError; }
        DoRetire(true);
        auto Subsystem = _Subsystem.Get();
        auto Layer = _Layer.Get();
        if (_CreatedLayout && ck::IsValid(Subsystem) && ck::IsValid(Layer)
            && Subsystem.Get_Layout() == _Layout.Get() && !Layer.HasWidgets())
        { Subsystem.DestroyLayout(); }
        return false;
    }
    private void DoSetErrors(const TArray<FString>& InErrors)
    {
        _SourceError = "";
        for (const auto Error : InErrors)
        { _SourceError = _SourceError.IsEmpty() ? Error : f"{_SourceError}; {Error}"; }
    }
    UFUNCTION()
    private void DoOnAction(const FString& InName)
    {
        if (!Get_IsCurrent() || !IsActivated() || !_Session.Get_IsInputActive() || _ChangingChild) { return; }
        const auto Action = FName(InName);
        bool Enabled = false;
        if (!_Session.TryGetBool(DoEnabledBinding(Action), Enabled) || !Enabled) { return; }
        if (Action == n"SettingsRequested")
        {
            if (!Request_ShowSettings()) { return; }
            DoCountAction(Action);
            return;
        }
        DoCountAction(Action);
        if (Action == n"ResumeRequested") { Request_Close(); }
    }
    private void DoCountAction(FName InAction)
    { int32 Count = 0; _ActionCounts.Find(InAction, Count); _ActionCounts.Add(InAction, Count + 1); }

    UFUNCTION(BlueprintOverride)
    FEventReply OnPreviewKeyDown(FGeometry InGeometry, FKeyEvent InEvent)
    {
        if (!Get_IsCurrent() || !IsActivated() || !_Session.Get_IsInputActive()
            || int32(InEvent.GetUserIndex()) != _Session.Get_SlateUserIndex()) { return FEventReply::Unhandled(); }
        const auto Key = InEvent.GetKey();
        if (Key == EKeys::Escape)
        {
            if (!InEvent.IsRepeat()) { DoOnAction("ResumeRequested"); }
            return FEventReply::Handled();
        }
        int32 Direction = 0;
        if (Key == EKeys::Tab) { Direction = InEvent.IsShiftDown() ? -1 : 1; }
        else if (Key == EKeys::Up || Key == EKeys::Left) { Direction = -1; }
        else if (Key == EKeys::Down || Key == EKeys::Right) { Direction = 1; }
        if (Direction == 0) { return FEventReply::Unhandled(); }
        if (!InEvent.IsRepeat())
        {
            _Session.TryNavigateButtons(DoButtonIds(), Direction);
        }
        return FEventReply::Handled();
    }
    UFUNCTION(BlueprintOverride)
    FEventReply OnFocusReceived(FGeometry InGeometry, FFocusEvent InEvent)
    {
        if (Get_IsCurrent() && IsActivated() && _Session.Get_IsInputActive()
            && int32(InEvent.GetUser()) == _Session.Get_SlateUserIndex())
        { _Session.TryFocusButton("resume-button"); }
        return FEventReply::Handled();
    }
    UFUNCTION(BlueprintOverride)
    void Tick(FGeometry InGeometry, float InDeltaTime)
    {
        if (!Get_IsCurrent()) { return; }
        _PollElapsed += InDeltaTime;
        if (_PollElapsed < 0.5) { return; }
        _PollElapsed = 0.0;
        FString Markup;
        FString Stylesheet;
        const auto Directory = DoResourceDirectory();
        if (!FFileHelper::LoadFileToString(Markup, FPaths::CombinePaths(Directory, "PauseMenuProof.ui.html"))
            || !FFileHelper::LoadFileToString(Stylesheet, FPaths::CombinePaths(Directory, "PauseMenuProof.ui.css")))
        { _SourceError = "Pause source files could not load"; return; }
        if (Markup == _LastMarkup && Stylesheet == _LastStylesheet) { return; }
        _LastMarkup = Markup;
        _LastStylesheet = Stylesheet;
        Request_ApplySource(Markup, Stylesheet);
    }
}
