#include "CkTests/Net/CkAutoTest_NetSubject_VoicePlaybackEntityScript.h"

#include "CkTests/Net/CkAutoTest_NetSubject_VoicePlayback.h"

#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"

#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Utils.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

#include <NativeGameplayTags.h>
#include <Sound/SoundClass.h>

// --------------------------------------------------------------------------------------------------------------------

// Test-only child of the C++-registered "VoiceChat.Channel" category root.
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VoiceChat_Channel_AutoTest_Playback, TEXT("VoiceChat.Channel.AutoTest.Playback"));

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_AutoTest_NetSubject_VoicePlaybackEntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    // Super::Construct sets up the WithActor bridge (Transform + Label) before anything is composed on the entity.
    const auto Flow = Super::Construct(InHandle, InSpawnParams);

    auto* Subject = Cast<ACk_AutoTest_NetSubject_VoicePlayback_UE>(UCk_Utils_EntityBridge_UE::Get_Actor(InHandle));

    CK_ENSURE_IF_NOT(ck::IsValid(Subject),
        TEXT("The voice-playback entity script is bridged to an actor that is not an ACk_AutoTest_NetSubject_VoicePlayback_UE "
             "on Entity [{}]"), InHandle)
    { return Flow; }

    auto ChannelSpec = FCk_VoiceChannel_Spec{TAG_VoiceChat_Channel_AutoTest_Playback};
    ChannelSpec
        .Set_SpatializationPolicy(ECk_VoiceChat_SpatializationPolicy::Global2D)
        .Set_SoundClass(TSoftObjectPtr<USoundClass>{ACk_AutoTest_NetSubject_VoicePlayback_UE::Get_ChannelSoundClassPath()});

    auto TalkerSpec = FCk_VoiceTalker_Spec{};
    TalkerSpec
        .Set_PlaybackAttachComponent(TWeakObjectPtr<USceneComponent>{Subject->_VoiceSocket.Get()})
        .Set_PlaybackAttachSocketName(UCk_AutoTest_VoiceSocketComponent_UE::Get_SocketName());

    Subject->_TestChannel = UCk_Utils_VoiceChannel_UE::Add(InHandle, ChannelSpec);
    Subject->_TestTalker = UCk_Utils_VoiceTalker_UE::Add(InHandle, TalkerSpec);

    return Flow;
}

// --------------------------------------------------------------------------------------------------------------------
