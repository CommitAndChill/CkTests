#include "CkTests/Net/CkAutoTest_NetSubject_VoicePlayback.h"

#include "CkTests/Net/CkAutoTest_NetSubject_VoicePlaybackEntityScript.h"

#include <GameFramework/Actor.h>

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_AutoTest_VoiceSocketComponent_UE::
    GetSocketTransform(
        FName InSocketName,
        ERelativeTransformSpace InTransformSpace) const
    -> FTransform
{
    if (InSocketName != Get_SocketName())
    { return Super::GetSocketTransform(InSocketName, InTransformSpace); }

    const auto SocketRelative = FTransform{Get_SocketOffset()};

    switch (InTransformSpace)
    {
        case RTS_World:
        {
            return SocketRelative * GetComponentTransform();
        }
        case RTS_Actor:
        {
            const auto SocketWorld = SocketRelative * GetComponentTransform();
            const auto* Owner = GetOwner();
            return ck::IsValid(Owner) ? SocketWorld.GetRelativeTransform(Owner->GetTransform()) : SocketWorld;
        }
        default:
        {
            return SocketRelative;
        }
    }
}

auto
    UCk_AutoTest_VoiceSocketComponent_UE::
    DoesSocketExist(
        FName InSocketName) const
    -> bool
{
    return InSocketName == Get_SocketName();
}

auto
    UCk_AutoTest_VoiceSocketComponent_UE::
    HasAnySockets() const
    -> bool
{
    return true;
}

auto
    UCk_AutoTest_VoiceSocketComponent_UE::
    QuerySupportedSockets(
        TArray<FComponentSocketDescription>& OutSockets) const
    -> void
{
    OutSockets.Emplace(Get_SocketName(), EComponentSocketType::Socket);
}

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_NetSubject_VoicePlayback_UE::
    ACk_AutoTest_NetSubject_VoicePlayback_UE()
{
    _VoiceSocket = CreateDefaultSubobject<UCk_AutoTest_VoiceSocketComponent_UE>(TEXT("VoiceSocket"));
    _VoiceSocket->SetMobility(EComponentMobility::Movable);
    _VoiceSocket->SetupAttachment(RootComponent);
    _VoiceSocket->SetRelativeLocation(FVector{0.0, 0.0, 60.0});

    _EntityScriptClass = UCk_AutoTest_NetSubject_VoicePlaybackEntityScript_UE::StaticClass();
}

// --------------------------------------------------------------------------------------------------------------------
