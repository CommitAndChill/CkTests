#include "CkTests/Net/CkAutoTest_NetSubject_VoiceAutoJoinEntityScript.h"

#include "CkTests/Net/CkAutoTest_NetSubject_VoiceAutoJoin.h"

#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"

#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Utils.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_AutoTest_NetSubject_VoiceAutoJoinChannelEntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    // Super::Construct sets up the WithActor bridge first - the control-plane container needs an OwningActor in
    // the chain to bind its replication driver to.
    const auto Flow = Super::Construct(InHandle, InSpawnParams);

    auto Spec = FCk_VoiceChannel_Spec{TAG_VoiceChat_Channel_AutoTest_AutoJoin};
    Spec.Set_SpatializationPolicy(ECk_VoiceChat_SpatializationPolicy::Global2D);
    Spec.Set_AutoJoinTalkers(ECk_EnableDisable::Enable);

    const auto Channel = UCk_Utils_VoiceChannel_UE::Add(InHandle, Spec);

    if (auto* Subject = Cast<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE>(
            UCk_Utils_EntityBridge_UE::Get_Actor(InHandle)))
    { Subject->_TestChannel = Channel; }

    return Flow;
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_AutoTest_NetSubject_VoiceAutoJoinTalkerEntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);

    auto Spec = FCk_VoiceTalker_Spec{};
    Spec.Set_AutoJoinChannels(FGameplayTagContainer{TAG_VoiceChat_Channel_AutoTest_AutoJoin});

    const auto Talker = UCk_Utils_VoiceTalker_UE::Add(InHandle, Spec);

    if (auto* Subject = Cast<ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE>(
            UCk_Utils_EntityBridge_UE::Get_Actor(InHandle)))
    { Subject->_TestTalker = Talker; }

    return Flow;
}

// --------------------------------------------------------------------------------------------------------------------
