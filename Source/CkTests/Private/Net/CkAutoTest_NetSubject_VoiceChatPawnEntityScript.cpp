#include "CkTests/Net/CkAutoTest_NetSubject_VoiceChatPawnEntityScript.h"

#include "CkTests/Net/CkAutoTest_NetSubject_VoiceChatPawn.h"

#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"

#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Utils.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

#include <NativeGameplayTags.h>

// --------------------------------------------------------------------------------------------------------------------

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VoiceChat_Channel_AutoTest_Pawn, TEXT("VoiceChat.Channel.AutoTest.Pawn"));

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_AutoTest_NetSubject_VoiceChatPawnEntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    // Super::Construct sets up the WithActor bridge (Transform + Label) before anything is composed on the entity.
    const auto Flow = Super::Construct(InHandle, InSpawnParams);

    auto* Subject = Cast<ACk_AutoTest_NetSubject_VoiceChatPawn_UE>(UCk_Utils_EntityBridge_UE::Get_Actor(InHandle));

    CK_ENSURE_IF_NOT(ck::IsValid(Subject),
        TEXT("The voice-chat pawn entity script is bridged to an actor that is not an ACk_AutoTest_NetSubject_VoiceChatPawn_UE "
             "on Entity [{}]"), InHandle)
    { return Flow; }

    auto TalkerSpec = FCk_VoiceTalker_Spec{};
    TalkerSpec.Set_TransmitMode(Subject->_SpecTransmitMode);

    Subject->_TestChannel = UCk_Utils_VoiceChannel_UE::Add(InHandle, FCk_VoiceChannel_Spec{TAG_VoiceChat_Channel_AutoTest_Pawn});
    Subject->_TestTalker = UCk_Utils_VoiceTalker_UE::Add(InHandle, TalkerSpec);

    return Flow;
}

// --------------------------------------------------------------------------------------------------------------------
