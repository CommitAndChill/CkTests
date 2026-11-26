#pragma once

#include "CkTests/Net/CkAutoTest_NetSubject.h"

#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Fragment_Data.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Fragment_Data.h"

#include <NativeGameplayTags.h>

#include "CkAutoTest_NetSubject_VoiceAutoJoin.generated.h"

// --------------------------------------------------------------------------------------------------------------------

// The channel name both auto-join subjects agree on: the channel subject opts in to auto-join under it, the talker
// subject lists it in its AutoJoinChannels.
CKTESTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_VoiceChat_Channel_AutoTest_AutoJoin);

// --------------------------------------------------------------------------------------------------------------------

// A subject that composes ONLY a VoiceChannel (Global2D, _AutoJoinTalkers enabled, named
// "VoiceChat.Channel.AutoTest.AutoJoin") on its bridged entity, on every world. Spawned separately from the talker
// subject below so a spec can choose which of the two exists first. `_TestChannel` is transient and local-only:
// each world's entity script fills its own actor.
UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE : public ACk_AutoTest_NetSubject
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE();

public:
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Ck|AutoTest")
    FCk_Handle_VoiceChannel _TestChannel;
};

// --------------------------------------------------------------------------------------------------------------------

// A subject that composes ONLY a VoiceTalker whose AutoJoinChannels lists "VoiceChat.Channel.AutoTest.AutoJoin",
// on every world. It never requests a join itself; any membership it gains comes from the auto-join reconcile.
UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE : public ACk_AutoTest_NetSubject
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE();

public:
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Ck|AutoTest")
    FCk_Handle_VoiceTalker _TestTalker;
};

// --------------------------------------------------------------------------------------------------------------------
