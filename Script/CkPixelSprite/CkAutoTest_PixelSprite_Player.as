// Language=angelscript

// Exercises the reflected runtime player on the imported Character example.
// Run Ck.PixelSprite.Examples.ImportNativeAssets before this host-project test.
class UCk_AutoTest_PixelSprite_Player : UCk_AutoTest_Base
{
    private UCk_PixelSpritePlayer_UE _Player;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Player = Cast<UCk_PixelSpritePlayer_UE>(NewObject(this, UCk_PixelSpritePlayer_UE));
        if (_Player == nullptr)
        {
            FinishFailure("Pixel sprite player could not be constructed");
            return;
        }
        if (!_Player.Request_LoadAsset(FSoftObjectPath(
            "/Game/PixelSpriteExamples/Character.Character")))
        {
            FinishFailure("Imported Character example was not available");
            return;
        }

        Assert_True(_Player.Request_SelectAnimation("idle"), "idle animation is selectable");
        Assert_Equals_Int(_Player.Get_CurrentFrame(), 0, "idle starts on frame zero");
        Assert_True(_Player.Request_SeekMilliseconds(240.0), "authored boundary is seekable");
        Assert_Equals_Int(_Player.Get_CurrentFrame(), 1, "240 ms selects idle frame one");
        FIntPoint Origin;
        Assert_True(_Player.TryResolveAnchor("origin", Origin), "frame anchor resolves");
        Assert_True(Origin.X == 8 && Origin.Y == 15, "frame anchor retains authored coordinates");

        Assert_True(_Player.Request_SetSamplingIntervalMilliseconds(1000.0), "sampling interval accepts one second");
        Assert_Equals_Int(_Player.Get_LogicalFrame(), 1, "sampling leaves logical frame unchanged");
        Assert_Equals_Int(_Player.Get_CurrentFrame(), 0, "sampling changes presentation only");
        Assert_True(_Player.Request_SetSamplingIntervalMilliseconds(0.0), "sampling can be disabled");
        Assert_True(_Player.Request_SetSourceFrameDecimation(3), "source-frame decimation is accepted");
        Assert_Equals_Int(_Player.Get_CurrentFrame(), 0, "decimation changes presentation only");
        Assert_True(_Player.Request_StepFrames(1), "explicit step reaches the next source frame");
        Assert_Equals_Int(_Player.Get_CurrentFrame(), 2, "explicit step displays exact requested frame");
        Assert_True(_Player.Request_SetSourceFrameDecimation(1), "decimation can be disabled");

        Assert_True(_Player.Request_SetPlayback(ECk_PixelSprite_PlayerPlayback::Once),
            "one-shot playback is selectable");
        Assert_True(_Player.Request_SetDirection(1), "forward direction is selectable");
        Assert_True(_Player.Request_SetSpeed(2.0), "positive speed is selectable");
        Assert_True(_Player.Request_Restart(), "one-shot can restart");
        Assert_True(_Player.Request_AdvanceMilliseconds(300.0), "speed-scaled advance succeeds");
        Assert_True(_Player.Get_IsComplete(), "600 authored milliseconds complete idle once");
        Assert_Equals_Int(_Player.Get_CurrentFrame(), 2, "one-shot holds last frame");
        Assert_True(_Player.Request_SetDirection(-1), "reverse direction can leave forward end");
        _Player.Request_Play();
        Assert_True(_Player.Request_AdvanceMilliseconds(300.0), "reverse one-shot reaches the start");
        Assert_True(_Player.Get_IsComplete(), "reverse one-shot reports completion");
        Assert_Equals_Int(_Player.Get_CurrentFrame(), 0, "reverse completion displays first frame");
        Assert_True(!_Player.Request_SetSpeed(0.0), "invalid speed rejects");
        Assert_True(!_Player.Request_SetSamplingIntervalMilliseconds(-1.0), "invalid sampling rejects");
        FinishSuccess();
    }
}
