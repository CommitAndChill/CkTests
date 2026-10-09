// Language=angelscript

// The human visual surface for the CkGameSettings widget accelerant: registers a demo collection
// (one setting per control type, split across two categories) and pushes the code-built
// settings screen. The keybinding page opens from the control panel's [K] row.
class ACk_GameSettingsGym_PlayerController : ACk_Gym_Base_PlayerController
{
    private UCk_GameSettingsUI_ScreenWidget _SettingsScreen;
    private UCk_GameSettingsUI_KeyBindingPageWidget _KeyBindingPage;
    private UWorld _BoundWorld;
    private ACkGym_GameState_UE _BoundState;
    private int32 _BoundGymIndex = -1;
    private UWorld _PendingWorld;
    private ACkGym_GameState_UE _PendingState;
    private bool _PendingStateCaptured = false;
    private double _BootstrapStartedSeconds = 0.0;
    private bool _StartRequested = false;
    private bool _Attempted = false;
    private bool _Failed = false;
    private bool _Ending = false;

    private bool HasCurrentLocalContext()
    {
        auto World = GetWorld();
        if (_Ending || IsActorBeingDestroyed() ||
            GetClass() != ACk_GameSettingsGym_PlayerController ||
            !IsLocalController() || ck::Is_NOT_Valid(World) ||
            World.GetNetMode() == ENetMode::NM_DedicatedServer)
        { return false; }
        auto State = Cast<ACkGym_GameState_UE>(World.GetGameState());
        if (ck::Is_NOT_Valid(State) || State.IsActorBeingDestroyed() ||
            State.GetClass() != ACkGym_GameState_UE ||
            State.GetWorld() != World || !State.Get_HasPublishedCatalog())
        { return false; }
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Registry.Num() ||
            Registry[Index].DisplayName != "Game Settings" ||
            Registry[Index].GameModeClass != ACk_GameSettingsGym_GameMode)
        { return false; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_GameSettingsGym_GameMode)
            { return false; }
        }
        return _BoundWorld == nullptr ||
            (_BoundWorld == World && _BoundState == State && _BoundGymIndex == Index);
    }

    private bool HasBoundLocalContext()
    { return _Attempted && !_Failed && _BoundWorld != nullptr && HasCurrentLocalContext(); }

    private void RemoveOwnedWidgets()
    {
        if (ck::IsValid(_KeyBindingPage)) { _KeyBindingPage.RemoveFromParent(); }
        if (ck::IsValid(_SettingsScreen)) { _SettingsScreen.RemoveFromParent(); }
        _KeyBindingPage = nullptr;
        _SettingsScreen = nullptr;
    }

    private void CloseLocalFailure(FString Reason)
    {
        if (_Failed) { return; }
        _Failed = true;
        RemoveOwnedWidgets();
        ck::Error(f"GameSettingsGym: local presentation closed: {Reason}");
    }

    // The base calls Request_StartGym when station admission finishes. On a
    // client that can precede the first replicated gym catalog. Waiting here
    // performs no registration or widget allocation, and never changes world.
    private void TryFinishInitialBootstrap()
    {
        if (!_StartRequested || _Attempted || _Failed || _Ending) { return; }
        auto World = GetWorld();
        if (GetClass() != ACk_GameSettingsGym_PlayerController ||
            !IsLocalController() || IsActorBeingDestroyed() ||
            ck::Is_NOT_Valid(World) || World != _PendingWorld ||
            World.GetNetMode() == ENetMode::NM_DedicatedServer)
        { CloseLocalFailure("initial local controller or world changed"); return; }
        const double Now = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        if (Now < _BootstrapStartedSeconds || Now - _BootstrapStartedSeconds > 20.0)
        { CloseLocalFailure("initial gym catalog did not arrive before deadline"); return; }
        auto Mode = Gameplay::GetGameMode();
        if (HasAuthority() &&
            (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
             Mode.GetClass() != ACk_GameSettingsGym_GameMode))
        { CloseLocalFailure("initial authoritative gym mode changed"); return; }

        auto CurrentState = World.GetGameState();
        if (ck::Is_NOT_Valid(CurrentState))
        {
            if (_PendingStateCaptured)
            { CloseLocalFailure("initial game state was lost"); }
            return;
        }
        auto State = Cast<ACkGym_GameState_UE>(CurrentState);
        if (ck::Is_NOT_Valid(State) || State.IsActorBeingDestroyed() ||
            State.GetClass() != ACkGym_GameState_UE ||
            State.GetWorld() != World ||
            (_PendingStateCaptured && _PendingState != State))
        { CloseLocalFailure("initial game state changed or is foreign"); return; }
        if (!_PendingStateCaptured)
        { _PendingState = State; _PendingStateCaptured = true; }
        if (!State.Get_HasPublishedCatalog()) { return; }

        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Registry.Num() == 0) { return; }
        if (Index >= Registry.Num() ||
            Registry[Index].DisplayName != "Game Settings" ||
            Registry[Index].GameModeClass != ACk_GameSettingsGym_GameMode)
        { CloseLocalFailure("published gym catalog selected another mode"); return; }

        _BoundWorld = World;
        _BoundState = State;
        _BoundGymIndex = Index;
        _Attempted = true; // first allocation/registration admission is one-shot
        if (!HasBoundLocalContext() || !DoRegisterDemoCollection() ||
            !DoOpenSettingsScreen())
        { CloseLocalFailure("demo settings or screen incomplete"); return; }
        ck::Trace("GameSettingsGym: functional code-built settings screen pushed; panel [K] toggles the keybinding page");
    }

    void Request_StartGym() override
    {
        if (_Ending || _StartRequested || _Attempted || _Failed || !IsLocalController()) { return; }
        _StartRequested = true;
        _PendingWorld = GetWorld();
        _BootstrapStartedSeconds = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        TryFinishInitialBootstrap();
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (_StartRequested && !_Attempted && !_Failed && !_Ending)
        { TryFinishInitialBootstrap(); }
        if (_Attempted && !_Failed && !_Ending && !HasBoundLocalContext())
        { CloseLocalFailure("local owner or gym changed"); }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        RemoveOwnedWidgets();
        Super::EndPlay(Reason);
    }

    private bool DoRegisterDemoCollection()
    {
        auto GameplayCategory = utils_gameplay_tag::ResolveGameplayTag(n"GymGameSettings.Gameplay");
        auto DisplayCategory = utils_gameplay_tag::ResolveGameplayTag(n"GymGameSettings.Display");
        if (!GameplayCategory.IsValid() || !DisplayCategory.IsValid()) { return false; }

        FGameplayTagContainer GameplayTags;
        GameplayTags.AddTag(GameplayCategory);

        FGameplayTagContainer DisplayTags;
        DisplayTags.AddTag(DisplayCategory);

        auto ToggleDefinition = FCk_GameSettings_SettingDefinition(n"gymdemo.toggle", ECk_GameSettings_ValueType::Bool, "true");
        ToggleDefinition.Set_DisplayName(FText::FromString("Demo Toggle"));
        ToggleDefinition.Set_Description(FText::FromString("A Bool setting - checkbox row"));
        ToggleDefinition.Set_CategoryTags(GameplayTags);
        ToggleDefinition.Set_OptionalRowClassOverride(UCk_GameSettingsGym_Widgets::Get_SettingRowClass());

        auto VolumeDefinition = FCk_GameSettings_SettingDefinition(n"gymdemo.volume", ECk_GameSettings_ValueType::Float, "0.75");
        VolumeDefinition.Set_DisplayName(FText::FromString("Demo Volume"));
        VolumeDefinition.Set_Description(FText::FromString("A ranged Float setting - slider row with live readout"));
        VolumeDefinition.Set_MinValue("0");
        VolumeDefinition.Set_MaxValue("1");
        VolumeDefinition.Set_CategoryTags(GameplayTags);
        VolumeDefinition.Set_OptionalRowClassOverride(UCk_GameSettingsGym_Widgets::Get_SettingRowClass());

        auto CountDefinition = FCk_GameSettings_SettingDefinition(n"gymdemo.count", ECk_GameSettings_ValueType::Int32, "3");
        CountDefinition.Set_DisplayName(FText::FromString("Demo Count"));
        CountDefinition.Set_Description(FText::FromString("A ranged Int32 setting - stepped slider row"));
        CountDefinition.Set_MinValue("0");
        CountDefinition.Set_MaxValue("10");
        CountDefinition.Set_CategoryTags(GameplayTags);
        CountDefinition.Set_OptionalRowClassOverride(UCk_GameSettingsGym_Widgets::Get_SettingRowClass());

        auto QualityDefinition = FCk_GameSettings_SettingDefinition(n"gymdemo.quality", ECk_GameSettings_ValueType::String, "medium");
        QualityDefinition.Set_DisplayName(FText::FromString("Demo Quality"));
        QualityDefinition.Set_Description(FText::FromString("An options setting - prev/next select row"));
        TArray<FCk_GameSettings_SettingOption> QualityOptions;
        QualityOptions.Add(FCk_GameSettings_SettingOption(FText::FromString("Low"), "low"));
        QualityOptions.Add(FCk_GameSettings_SettingOption(FText::FromString("Medium"), "medium"));
        QualityOptions.Add(FCk_GameSettings_SettingOption(FText::FromString("High"), "high"));
        QualityDefinition.Set_Options(QualityOptions);
        QualityDefinition.Set_CategoryTags(DisplayTags);
        QualityDefinition.Set_OptionalRowClassOverride(UCk_GameSettingsGym_Widgets::Get_SettingRowClass());

        auto LivesDefinition = FCk_GameSettings_SettingDefinition(n"gymdemo.lives", ECk_GameSettings_ValueType::Int32, "3");
        LivesDefinition.Set_DisplayName(FText::FromString("Demo Lives"));
        LivesDefinition.Set_Description(FText::FromString("A rangeless Int32 setting - stepper select row"));
        LivesDefinition.Set_CategoryTags(DisplayTags);
        LivesDefinition.Set_OptionalRowClassOverride(UCk_GameSettingsGym_Widgets::Get_SettingRowClass());

        TArray<FCk_GameSettings_SettingDefinition> Definitions;
        Definitions.Add(ToggleDefinition);
        Definitions.Add(VolumeDefinition);
        Definitions.Add(CountDefinition);
        Definitions.Add(QualityDefinition);
        Definitions.Add(LivesDefinition);

        int32 AlreadyRegistered = 0;
        for (auto Definition : Definitions)
        {
            if (utils_game_settings::Get_IsSettingRegistered(Definition.Get_Key()))
            { AlreadyRegistered++; }
        }
        // Another local player may share this GameInstance registry. A partial
        // previous batch cannot be adopted or filled one setting at a time.
        if (AlreadyRegistered != 0 && AlreadyRegistered != Definitions.Num())
        { return false; }
        if (AlreadyRegistered == 0 &&
            !utils_game_settings::Request_RegisterSettings(Definitions))
        { return false; }
        for (int32 I = 0; I < Definitions.Num(); ++I)
        {
            FCk_GameSettings_SettingDefinition Actual;
            auto Expected = Definitions[I];
            if (!utils_game_settings::Get_SettingDefinition(Expected.Get_Key(), Actual) ||
                Actual.Get_Key() != Expected.Get_Key() ||
                Actual.Get_ValueType() != Expected.Get_ValueType() ||
                !Actual.Get_DisplayName().EqualTo(Expected.Get_DisplayName()) ||
                !Actual.Get_Description().EqualTo(Expected.Get_Description()) ||
                Actual.Get_DefaultValue() != Expected.Get_DefaultValue() ||
                Actual.Get_MinValue() != Expected.Get_MinValue() ||
                Actual.Get_MaxValue() != Expected.Get_MaxValue() ||
                Actual.Get_Scope() != Expected.Get_Scope() ||
                Actual.Get_PersistencePolicy() != Expected.Get_PersistencePolicy() ||
                Actual.Get_ApplyBindingType() != Expected.Get_ApplyBindingType() ||
                Actual.Get_OptionalRowClassOverride() != Expected.Get_OptionalRowClassOverride() ||
                Actual.Get_CategoryTags().Num() != 1 ||
                !Actual.Get_CategoryTags().HasTagExact(
                    I < 3 ? GameplayCategory : DisplayCategory))
            { return false; }
            auto ActualOptions = Actual.Get_Options();
            auto ExpectedOptions = Expected.Get_Options();
            if (ActualOptions.Num() != ExpectedOptions.Num()) { return false; }
            for (int32 J = 0; J < ActualOptions.Num(); ++J)
            {
                if (ActualOptions[J].Get_Value() != ExpectedOptions[J].Get_Value() ||
                    !ActualOptions[J].Get_Label().EqualTo(ExpectedOptions[J].Get_Label()))
                { return false; }
            }
        }
        return HasBoundLocalContext();
    }

    private bool DoOpenSettingsScreen()
    {
        if (!HasBoundLocalContext()) { return false; }
        if (ck::IsValid(_SettingsScreen)) { _SettingsScreen.RemoveFromParent(); }
        _SettingsScreen = nullptr;
        _SettingsScreen = Cast<UCk_GameSettingsUI_ScreenWidget>(
            WidgetBlueprint::CreateWidget(UCk_GameSettingsGym_Screen, this));

        if (ck::Is_NOT_Valid(_SettingsScreen)) { return false; }

        auto CodeScreen = Cast<UCk_GameSettingsGym_Screen>(_SettingsScreen);
        if (ck::Is_NOT_Valid(CodeScreen) || !CodeScreen.Get_IsLayoutReady()) { return false; }
        if (!_SettingsScreen.AddToPlayerScreen(0)) { return false; }
        _SettingsScreen.ActivateWidget();
        return CodeScreen.Get_HasCompleteDemoRows() && HasBoundLocalContext();
    }

    //--------------------------------------------------------------------------------------------------------------------------
    // CONTROL PANEL (Script/Common/CkGym_ControlPanel.as)
    //
    // Closing the settings screen leaves an empty gym with no way back into it that is visible from
    // inside the gym. These two rows are that way back.
    //--------------------------------------------------------------------------------------------------------------------------

    FString Get_ControlPanelTitle() override
    {
        return "GAME SETTINGS";
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        const bool Ready = HasBoundLocalContext();
        Rows.Add(CkGym_Control::Action(EKeys::G, "G", "Reopen the settings screen", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Open the key-binding page", Ready));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!_Attempted || _Failed || _Ending) { return; }
        if (!HasBoundLocalContext())
        { CloseLocalFailure("control owner or gym changed"); return; }
        if (InRowIndex == 0) { DoReopenSettingsScreen(); }
        else if (InRowIndex == 1) { DoToggleKeyBindingPage(); }
    }

    private void DoReopenSettingsScreen()
    {
        if (!DoOpenSettingsScreen())
        { CloseLocalFailure("settings screen reopen failed"); }
    }

    private void DoToggleKeyBindingPage()
    {
        if (ck::IsValid(_KeyBindingPage) && _KeyBindingPage.IsInViewport())
        {
            _KeyBindingPage.RemoveFromParent();
            _KeyBindingPage = nullptr;
            return;
        }

        if (ck::IsValid(_KeyBindingPage)) { _KeyBindingPage.RemoveFromParent(); }
        _KeyBindingPage = nullptr;

        _KeyBindingPage = Cast<UCk_GameSettingsUI_KeyBindingPageWidget>(
            WidgetBlueprint::CreateWidget(UCk_GameSettingsGym_KeyPage, this));

        if (ck::Is_NOT_Valid(_KeyBindingPage))
        { CloseLocalFailure("key-binding page creation failed"); return; }

        auto CodePage = Cast<UCk_GameSettingsGym_KeyPage>(_KeyBindingPage);
        if (ck::Is_NOT_Valid(CodePage) || !CodePage.Get_IsLayoutReady())
        { CloseLocalFailure("key-binding page layout incomplete"); return; }

        if (!_KeyBindingPage.AddToPlayerScreen(10))
        { CloseLocalFailure("key-binding page placement failed"); return; }
        if (!CodePage.Get_HasCompleteMountedRows())
        { CloseLocalFailure("key-binding rows incomplete"); }
    }
}
