#pragma once

#include "CkTests/Net/CkAutoTest_NetSubject_VoiceChat.h"

#include <Components/SceneComponent.h>

#include "CkAutoTest_NetSubject_VoicePlayback.generated.h"

// --------------------------------------------------------------------------------------------------------------------
//
// A scene component with one named socket at a fixed offset, so a spec can attach to a socket without an engine mesh
// asset. Stands in for a skeletal mesh's head bone.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS()
class CKTESTS_API UCk_AutoTest_VoiceSocketComponent_UE : public USceneComponent
{
    GENERATED_BODY()

public:
    static auto Get_SocketName() -> FName { return TEXT("Mouth"); }
    static auto Get_SocketOffset() -> FVector { return FVector{0.0, 0.0, 10.0}; }

public:
    auto
    GetSocketTransform(
        FName InSocketName,
        ERelativeTransformSpace InTransformSpace = RTS_World) const -> FTransform override;

    auto
    DoesSocketExist(
        FName InSocketName) const -> bool override;

    auto
    HasAnySockets() const -> bool override;

    auto
    QuerySupportedSockets(
        TArray<FComponentSocketDescription>& OutSockets) const -> void override;
};

// --------------------------------------------------------------------------------------------------------------------
//
// The VoiceChat subject with an authored playback config, for the specs that read what a receiving machine's synth
// was given. Its entity script (UCk_AutoTest_NetSubject_VoicePlaybackEntityScript_UE) composes, on every machine:
//   - a Global2D VoiceChannel child (tag "VoiceChat.Channel.AutoTest.Playback") whose Spec authors a SoundClass
//     (/Engine/EngineSounds/Voice), and
//   - a VoiceTalker whose Spec attaches playback to `_VoiceSocket` (this actor's own copy on each machine) at
//     UCk_AutoTest_VoiceSocketComponent_UE::Get_SocketName().
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_VoicePlayback_UE : public ACk_AutoTest_NetSubject_VoiceChat_UE
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_VoicePlayback_UE();

public:
    static auto Get_ChannelSoundClassPath() -> FSoftObjectPath { return FSoftObjectPath{TEXT("/Engine/EngineSounds/Voice.Voice")}; }

public:
    UPROPERTY(VisibleAnywhere, Category = "Ck|AutoTest")
    TObjectPtr<UCk_AutoTest_VoiceSocketComponent_UE> _VoiceSocket;
};

// --------------------------------------------------------------------------------------------------------------------
