#include "CkTests/Net/CkAutoTest_NetSubject_VoiceAutoJoin.h"

#include "CkTests/Net/CkAutoTest_NetSubject_VoiceAutoJoinEntityScript.h"

// --------------------------------------------------------------------------------------------------------------------

UE_DEFINE_GAMEPLAY_TAG(TAG_VoiceChat_Channel_AutoTest_AutoJoin, TEXT("VoiceChat.Channel.AutoTest.AutoJoin"));

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE::
    ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE()
{
    _EntityScriptClass = UCk_AutoTest_NetSubject_VoiceAutoJoinChannelEntityScript_UE::StaticClass();
}

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE::
    ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE()
{
    _EntityScriptClass = UCk_AutoTest_NetSubject_VoiceAutoJoinTalkerEntityScript_UE::StaticClass();
}

// --------------------------------------------------------------------------------------------------------------------
