// Language=angelscript

// Run on /CkTests/TestGyms/TestGyms_CkTests_Level with
// ?game=/Script/Angelscript.Ck_PixelSpriteGym_GameMode and
// -asgauntlet=Ck_GauntletAsTest_PixelSpriteGym. Screenshots require a real RHI.
class UCk_GauntletAsTest_PixelSpriteGym : UCk_GauntletAsTest_Base
{
    default _TimeoutSeconds = 55.0f;

    private ACk_PixelSpriteGym_PlayerController _Gym;
    private int32 _Phase = 0;
    private int32 _InitialFrame = -1;
    private int32 _PausedFrame = -1;
    private int32 _SteppedFromFrame = -1;
    private float _Elapsed = 0.0f;
    private float _PhaseElapsed = 0.0f;
    private bool _Ended = false;
    private bool _RequestedGymTravel = false;
    private FString _PreviousLastGym;

    UFUNCTION(BlueprintOverride)
    void OnAsInit()
    {
        _PreviousLastGym = UCk_Utils_GymStartup_UE::Get_LastGymName();
        Controller.Request_MarkHeartbeat("[PixelSpriteGym] Waiting for the Pixel Sprite gym controller");
    }

    UFUNCTION(BlueprintOverride)
    void OnAsTick(float InDeltaSeconds)
    {
        if (_Ended) { return; }
        _Elapsed += InDeltaSeconds;
        _PhaseElapsed += InDeltaSeconds;

        if (_Phase == 0)
        {
            _Gym = Cast<ACk_PixelSpriteGym_PlayerController>(Controller.Get_FirstPlayerController());
            if (_Gym == nullptr && !_RequestedGymTravel)
            {
                // The saved startup preference may redirect after the explicit map URL is loaded.
                // Travel once through the production switcher after that one-shot is consumed.
                _RequestedGymTravel = true;
                CkGym_Cycler::Request_TravelToGymByName("Pixel Sprite");
                Controller.Request_MarkHeartbeat("[PixelSpriteGym] Selecting registered gym after startup preference");
                return;
            }
            if (_Gym != nullptr && _Gym.Get_Ready())
            {
                auto Player = _Gym.Get_Player();
                if (Player == nullptr) { DoFail("ready gym has no player"); return; }
                _InitialFrame = Player.Get_CurrentFrame();
                Controller.Request_MarkHeartbeat("[PixelSpriteGym] All three renderers ready; waiting for one-clock animation advance");
                AdvancePhase(1);
                return;
            }
            if (_Elapsed >= 30.0f)
            { DoFail("gym did not become ready within 30 seconds; check the map and ?game override"); }
            return;
        }

        auto Player = _Gym.Get_Player();
        if (Player == nullptr || !_Gym.Get_Ready())
        { DoFail("gym lost its ready player"); return; }

        if (_Phase == 1)
        {
            if (Player.Get_CurrentFrame() != _InitialFrame)
            {
                _Gym.Request_ControlActivated(4); // P: pause the sole clock.
                _PausedFrame = Player.Get_CurrentFrame();
                Controller.Request_MarkHeartbeat("[PixelSpriteGym] One player clock advanced; waiting for settled pixels");
                AdvancePhase(2);
            }
            else if (_PhaseElapsed >= 5.0f)
            { DoFail("the player clock did not advance from its initial frame"); }
            return;
        }

        if (_Phase == 2)
        {
            if (_PhaseElapsed < 0.25f) { return; }
            if (Player.Get_CurrentFrame() != _PausedFrame)
            { DoFail("pause did not hold the current frame"); return; }
            if (Get_FrameIsSettled())
            {
                _SteppedFromFrame = Player.Get_CurrentFrame();
                _Gym.Request_ControlActivated(5); // N: one explicit frame step.
                Controller.Request_MarkHeartbeat("[PixelSpriteGym] Paused pixels and ISM data agree; checking one-frame step");
                AdvancePhase(3);
            }
            else if (_PhaseElapsed >= 3.0f)
            { DoFail("paused pixels, visible count, or ISM custom data did not settle"); }
            return;
        }

        if (_Phase == 3)
        {
            if (_PhaseElapsed < 0.25f) { return; }
            if (Player.Get_CurrentFrame() == _SteppedFromFrame)
            { DoFail("manual step did not select a different source frame"); return; }
            if (Get_FrameIsSettled())
            {
                _Gym.Request_ControlActivated(6); // R: return to the first authored frame.
                Controller.Request_MarkHeartbeat("[PixelSpriteGym] Manual step and ISM colours agree; checking restart");
                AdvancePhase(4);
            }
            else if (_PhaseElapsed >= 3.0f)
            { DoFail("stepped pixels, visible count, or ISM custom data did not settle"); }
            return;
        }

        if (_Phase == 4)
        {
            if (_PhaseElapsed < 0.25f) { return; }
            if (Player.Get_CurrentFrame() != 0)
            { DoFail("restart did not select authored frame zero"); return; }
            if (Get_FrameIsSettled())
            {
                _Gym.Request_ControlActivated(1);
                Controller.Request_MarkHeartbeat("[PixelSpriteGym] Restart settled; framing 1/3 aliased shader");
                AdvancePhase(5);
            }
            else if (_PhaseElapsed >= 3.0f)
            { DoFail("restarted pixels, visible count, or ISM custom data did not settle"); }
            return;
        }

        if (_Phase == 5 && _PhaseElapsed >= 1.5f)
        {
            Controller.Request_MarkHeartbeat("[PixelSpriteGym] Capture 1/3: aliased shader");
            Controller.Request_ExecConsoleCommand("Shot SHOWUI");
            AdvancePhase(6);
        }
        else if (_Phase == 6 && _PhaseElapsed >= 0.5f)
        {
            _Gym.Request_ControlActivated(2);
            Controller.Request_MarkHeartbeat("[PixelSpriteGym] Framing 2/3 edge AA shader");
            AdvancePhase(7);
        }
        else if (_Phase == 7 && _PhaseElapsed >= 1.5f)
        {
            Controller.Request_MarkHeartbeat("[PixelSpriteGym] Capture 2/3: edge AA shader");
            Controller.Request_ExecConsoleCommand("Shot SHOWUI");
            AdvancePhase(8);
        }
        else if (_Phase == 8 && _PhaseElapsed >= 0.5f)
        {
            _Gym.Request_ControlActivated(3);
            Controller.Request_MarkHeartbeat("[PixelSpriteGym] Framing 3/3 CkIsm pixel boxes");
            AdvancePhase(9);
        }
        else if (_Phase == 9 && _PhaseElapsed >= 1.5f)
        {
            Controller.Request_MarkHeartbeat("[PixelSpriteGym] Capture 3/3: CkIsm pixel boxes");
            Controller.Request_ExecConsoleCommand("Shot SHOWUI");
            AdvancePhase(10);
        }
        else if (_Phase == 10 && _PhaseElapsed >= 1.0f)
        {
            _Ended = true;
            UCk_Utils_GymStartup_UE::Request_Set_LastGymName(_PreviousLastGym);
            Controller.Request_MarkHeartbeat("[PixelSpriteGym] Runtime checks passed; inspect the three ordered screenshots for rendered acceptance");
            Controller.Request_EndTest(0);
        }
    }

    private bool Get_FrameIsSettled()
    {
        auto Pixels = TArray<FColor>();
        FIntPoint Canvas;
        if (!_Gym.Get_Player().TryGet_CurrentPixels(Pixels, Canvas) ||
            Canvas.X != 32 || Canvas.Y != 32 || Pixels.Num() != 32 * 32)
        { return false; }

        auto AlphaCount = 0;
        for (auto Pixel : Pixels)
        {
            if (Pixel.A > 0) { AlphaCount++; }
        }
        return AlphaCount == _Gym.Get_VisiblePixelCount() &&
            AlphaCount == _Gym.Get_SettledPixelCount();
    }

    private void AdvancePhase(int32 InPhase)
    {
        _Phase = InPhase;
        _PhaseElapsed = 0.0f;
    }

    private void DoFail(FString InReason)
    {
        _Ended = true;
        UCk_Utils_GymStartup_UE::Request_Set_LastGymName(_PreviousLastGym);
        Controller.Request_MarkHeartbeat(f"[PixelSpriteGym] FAILED: {InReason}");
        Controller.Request_EndTest(1);
    }
}
