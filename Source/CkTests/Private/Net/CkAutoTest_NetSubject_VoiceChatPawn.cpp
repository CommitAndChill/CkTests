#include "CkTests/Net/CkAutoTest_NetSubject_VoiceChatPawn.h"

#include "CkTests/Net/CkAutoTest_NetSubject_VoiceChatPawnEntityScript.h"

#include "Components/SceneComponent.h"

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_NetSubject_VoiceChatPawn_UE::
    ACk_AutoTest_NetSubject_VoiceChatPawn_UE()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    bReplicateUsingRegisteredSubObjectList = true;
    PrimaryActorTick.bCanEverTick = false;

    // Movable USceneComponent root: UCk_EntityScript_WithActor_UE::Construct adds the Transform fragment only with a
    // root, and the Positional3D channel's proximity routing needs the talker's Transform.
    auto* Root = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
    Root->SetMobility(EComponentMobility::Movable);
    RootComponent = Root;

    _EntityBridge = CreateDefaultSubobject<UCk_EntityBridge_ActorComponent_UE>(TEXT("EntityBridge"));

    _EntityScriptClass = UCk_AutoTest_NetSubject_VoiceChatPawnEntityScript_UE::StaticClass();
}

// --------------------------------------------------------------------------------------------------------------------

auto
    ACk_AutoTest_NetSubject_VoiceChatPawn_UE::
    BeginPlay()
    -> void
{
    Super::BeginPlay();

    if (ck::Is_NOT_Valid(_EntityScriptClass))
    { return; }

    _EntityBridge->Request_SpawnLinkedEntity(_EntityScriptClass);
}

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_NetSubject_VoiceChatOpenMicPawn_UE::
    ACk_AutoTest_NetSubject_VoiceChatOpenMicPawn_UE()
{
    _SpecTransmitMode = ECk_VoiceChat_TransmitMode::OpenMic;
}

// --------------------------------------------------------------------------------------------------------------------
