#pragma once

#include "CoreMinimal.h"

#include "GameFramework/Pawn.h"
#include "Templates/SubclassOf.h"

#include "CkEcs/EntityBridge/CkEntityBridge_Fragment_Data.h"

#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Fragment_Data.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Fragment_Data.h"

#include "CkAutoTest_NetSubject_VoiceChatPawn.generated.h"

class UCk_EntityScript_WithActor_UE;

// --------------------------------------------------------------------------------------------------------------------
//
// Pawn subject for the open-mic net tests. A pawn (not ACk_AutoTest_NetSubject_VoiceChat_UE's plain actor) because the
// open mic opens only on the talker this machine's player controls, which CkVoiceChat reads through
// UCk_Utils_Net_UE::Get_IsEntityLocallyControlled_ByPlayer: a possessed, player-controlled APawn bridged to the talker's
// entity. A test possesses it on the server with a remote client's PlayerController (Get_RemoteClientPlayerController),
// so the client's copy reports locally controlled and the host's does not.
//
// Its entity script composes, on every machine, a VoiceChannel child (tag "VoiceChat.Channel.AutoTest.Pawn") and a
// VoiceTalker on the bridged entity whose Spec transmit mode is _SpecTransmitMode. `_TestChannel` / `_TestTalker` are
// transient, local-only stashes; each machine's entity script fills its own.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_VoiceChatPawn_UE : public APawn
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_VoiceChatPawn_UE();

protected:
    virtual auto BeginPlay() -> void override;

public:
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Ck|AutoTest")
    FCk_Handle_VoiceChannel _TestChannel;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Ck|AutoTest")
    FCk_Handle_VoiceTalker _TestTalker;

    // A class default, so every machine's entity script composes the talker with the same Spec.
    UPROPERTY()
    ECk_VoiceChat_TransmitMode _SpecTransmitMode = ECk_VoiceChat_TransmitMode::PushToTalk;

protected:
    UPROPERTY()
    TSubclassOf<UCk_EntityScript_WithActor_UE> _EntityScriptClass;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCk_EntityBridge_ActorComponent_UE> _EntityBridge;
};

// --------------------------------------------------------------------------------------------------------------------

// The same pawn whose talker's Spec is OpenMic: every copy is armed at Add, before anyone possesses it.
UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_VoiceChatOpenMicPawn_UE : public ACk_AutoTest_NetSubject_VoiceChatPawn_UE
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_VoiceChatOpenMicPawn_UE();
};

// --------------------------------------------------------------------------------------------------------------------
