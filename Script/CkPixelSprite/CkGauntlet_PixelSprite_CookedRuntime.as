// Language=angelscript

class UCk_GauntletAsTest_PixelSpriteCookedRuntime : UCk_GauntletAsTest_Base
{
    default _TimeoutSeconds = 45.0f;

    private UCk_PixelSpritePlayer_UE _Player;
    private UCk_PixelSpritePreview_UE _Preview;
    private bool _Initialized = false;
    private bool _SawPreviewAdvance = false;
    private bool _RequestedScreenshot = false;
    private bool _Ended = false;
    private float _Time = 0.0f;

    UFUNCTION(BlueprintOverride)
    void OnAsInit()
    {
        _Player = Cast<UCk_PixelSpritePlayer_UE>(NewObject(this, UCk_PixelSpritePlayer_UE));
        if (_Player == nullptr || !_Player.Request_LoadAsset(
            FSoftObjectPath("/Game/PixelSpriteExamples/Character.Character")))
        {
            DoFail("Cooked indexed Character asset did not load");
            return;
        }
        if (!_Player.Request_SelectAnimation("idle") || !_Player.Request_SeekMilliseconds(240.0)
            || _Player.Get_CurrentFrame() != 1)
        {
            DoFail("Authored boundary did not select idle frame 1");
            return;
        }
        FIntPoint Origin;
        if (!_Player.TryResolveAnchor("origin", Origin) || Origin.X != 8 || Origin.Y != 15)
        {
            DoFail("Cooked frame anchor did not survive");
            return;
        }
        if (!_Player.Request_SetPlayback(ECk_PixelSprite_PlayerPlayback::Once)
            || !_Player.Request_Restart() || !_Player.Request_AdvanceMilliseconds(600.0)
            || !_Player.Get_IsComplete() || _Player.Get_CurrentFrame() != 2)
        {
            DoFail("One-shot runtime timing failed");
            return;
        }

        _Preview = Cast<UCk_PixelSpritePreview_UE>(WidgetBlueprint::CreateWidget(
            UCk_PixelSpritePreview_UE, Controller.Get_FirstPlayerController()));
        if (_Preview == nullptr || !_Preview.Request_LoadAsset(
            FSoftObjectPath("/Game/PixelSpriteExamples/Character.Character"))
            || !_Preview.Request_SelectAnimation("idle") || !_Preview.Get_IsReady())
        {
            DoFail("Runtime UMG preview did not prepare cooked textures");
            return;
        }
        _Preview.SetDesiredSizeInViewport(FVector2D(256.0, 256.0));
        _Preview.SetPositionInViewport(FVector2D(64.0, 64.0), false);
        _Preview.AddToViewport();
        _Initialized = true;
        Controller.Request_MarkHeartbeat("[PixelSpriteCooked] Indexed asset, timing, anchors and UMG initialized");
    }

    UFUNCTION(BlueprintOverride)
    void OnAsTick(float DeltaTime)
    {
        if (_Ended || !_Initialized)
        {
            return;
        }
        _Time += DeltaTime;
        if (_Preview.Get_CurrentFrame() > 0)
        {
            _SawPreviewAdvance = true;
        }
        if (_Time >= 2.0f && !_RequestedScreenshot)
        {
            if (!_SawPreviewAdvance)
            {
                DoFail("Viewport preview did not advance its owned player"); return;
            }
            Controller.Request_ExecConsoleCommand("Shot SHOWUI");
            _RequestedScreenshot = true;
            Controller.Request_MarkHeartbeat("[PixelSpriteCooked] Requested rendered viewport screenshot");
        }
        if (_Time >= 4.0f && _RequestedScreenshot)
        {
            _Preview.RemoveFromParent();
            _Ended = true;
            Controller.Request_MarkHeartbeat("[PixelSpriteCooked] Runtime assertions passed; inspect screenshot separately");
            Controller.Request_EndTest(0);
        }
    }

    private void DoFail(const FString& InMessage)
    {
        _Ended = true;
        Controller.Request_MarkHeartbeat("[PixelSpriteCooked] FAILED: " + InMessage);
        Controller.Request_EndTest(1);
    }
}
