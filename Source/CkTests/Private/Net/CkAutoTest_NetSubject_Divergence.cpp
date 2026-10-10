#include "CkTests/Net/CkAutoTest_NetSubject_Divergence.h"

#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_auto_test_divergence_subject
{
    constexpr const TCHAR* UntimedClass =
        TEXT("/Script/Angelscript.Ck_SmTest_DivergenceFirstBranch_GymActor");
    constexpr const TCHAR* TimedClass =
        TEXT("/Script/Angelscript.Ck_SmTest_DivergenceTimed_GymActor");

    constexpr const TCHAR* SnapshotNames[12] = {
        TEXT("Snap_A_Enter"), TEXT("Snap_A_Idle"),
        TEXT("Snap_A_Branch"), TEXT("Snap_A_Left"),
        TEXT("Snap_A_Right"), TEXT("Snap_A_Finish"),
        TEXT("Snap_B_Enter"), TEXT("Snap_B_Idle"),
        TEXT("Snap_B_Branch"), TEXT("Snap_B_Left"),
        TEXT("Snap_B_Right"), TEXT("Snap_B_Finish")};

    bool LiveWorld(const UWorld* World)
    {
        return IsInGameThread() && IsValid(World) &&
            !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
            World->IsGameWorld() && !World->bIsTearingDown &&
            !World->IsBeingCleanedUp();
    }

    bool LiveActor(const AActor* Actor)
    {
        return IsValid(Actor) && !Actor->IsActorBeingDestroyed() &&
            !Actor->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject |
                RF_BeginDestroyed | RF_FinishDestroyed);
    }

    bool ReadBool(UObject* Target, const FName Name, bool& Out)
    {
        Out = false;
        if (!IsInGameThread() || !IsValid(Target)) { return false; }
        auto* Function = Target->FindFunction(Name);
        const auto* Return = IsValid(Function) ?
            FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue")) : nullptr;
        if (!Return || Function->NumParms != 1 ||
            !Return->HasAnyPropertyFlags(CPF_ReturnParm)) { return false; }
        const int32 Offset = Return->GetOffset_ForUFunction();
        const int32 Size = Return->GetSize();
        if (Offset < 0 || Size <= 0 || Offset > Function->ParmsSize ||
            Size > Function->ParmsSize - Offset) { return false; }
        FStructOnScope Args(Function);
        if (!Args.GetStructMemory()) { return false; }
        Target->ProcessEvent(Function, Args.GetStructMemory());
        Out = Return->GetPropertyValue_InContainer(Args.GetStructMemory());
        return true;
    }

    bool ReadSnapshots(AActor* Actor, FCk_AutoTest_DivergenceFrame& Frame)
    {
        if (!IsInGameThread() || !LiveActor(Actor)) { return false; }
        int32* const Destinations[12] = {
            &Frame.Snap_A_Enter, &Frame.Snap_A_Idle,
            &Frame.Snap_A_Branch, &Frame.Snap_A_Left,
            &Frame.Snap_A_Right, &Frame.Snap_A_Finish,
            &Frame.Snap_B_Enter, &Frame.Snap_B_Idle,
            &Frame.Snap_B_Branch, &Frame.Snap_B_Left,
            &Frame.Snap_B_Right, &Frame.Snap_B_Finish};
        for (int32 Index = 0; Index < 12; ++Index)
        {
            const auto* Property = FindFProperty<FIntProperty>(
                Actor->GetClass(), SnapshotNames[Index]);
            if (!Property) { return false; }
            *Destinations[Index] = Property->GetPropertyValue_InContainer(Actor);
        }
        return true;
    }

    bool IsExactFamilyClass(const UClass* Class, const bool bTimed)
    {
        return IsValid(Class) &&
            !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated |
                CLASS_NewerVersionExists) &&
            Class->GetPathName() == (bTimed ? TimedClass : UntimedClass);
    }
}

bool ACk_AutoTest_NetSubject_Divergence_UE::HasLiveAuthorityWorld() const
{
    if (!IsInGameThread() ||
        !ck_auto_test_divergence_subject::LiveActor(this) ||
        !HasActorBegunPlay() || !HasAuthority()) { return false; }
    auto* World = GetWorld();
    return ck_auto_test_divergence_subject::LiveWorld(World) &&
        (World->IsNetMode(NM_DedicatedServer) ||
            World->IsNetMode(NM_ListenServer));
}

bool ACk_AutoTest_NetSubject_Divergence_UE::HasExactSourceActor() const
{
    if (!HasLiveAuthorityWorld() || !bAdmissionAttempted || bFailed ||
        !ck_auto_test_divergence_subject::LiveActor(SourceActor.Get()))
    { return false; }
    auto* Actor = SourceActor.Get();
    return SourceIdentity.HasSameIndexAndSerialNumber(
            TWeakObjectPtr<AActor>(Actor)) &&
        Actor->GetWorld() == GetWorld() && Actor->GetOwner() == this &&
        Actor->HasAuthority() && Actor->GetIsReplicated() &&
        ck_auto_test_divergence_subject::IsExactFamilyClass(
            Actor->GetClass(), bSourceTimed);
}

bool ACk_AutoTest_NetSubject_Divergence_UE::TryRetain_SourceActor(
    AActor* Actor, const bool bTimed)
{
    if (!HasLiveAuthorityWorld() || bAdmissionAttempted || bFailed ||
        Snapshot.bCompleted) { return false; }
    bAdmissionAttempted = true;
    if (!ck_auto_test_divergence_subject::LiveActor(Actor) ||
        Actor == this || Actor->GetWorld() != GetWorld() ||
        Actor->GetOwner() != this || !Actor->HasAuthority() ||
        !Actor->GetIsReplicated() ||
        !ck_auto_test_divergence_subject::IsExactFamilyClass(
            Actor->GetClass(), bTimed))
    { bFailed = true; return false; }
    SourceActor = Actor;
    SourceIdentity = Actor;
    bSourceTimed = bTimed;
    return true;
}

bool ACk_AutoTest_NetSubject_Divergence_UE::TryPublish_CompletedSource()
{
    if (Snapshot.bCompleted || bFailed) { return false; }
    if (!HasExactSourceActor()) { bFailed = true; return false; }
    auto* Actor = SourceActor.Get();
    if (!Actor->HasActorBegunPlay()) { return false; }

    bool Completed = false;
    if (!ck_auto_test_divergence_subject::ReadBool(
            Actor, TEXT("Get_SourceRunFinished"), Completed))
    { bFailed = true; return false; }
    if (!HasExactSourceActor()) { bFailed = true; return false; }

    bool Closed = true;
    if (!ck_auto_test_divergence_subject::ReadBool(
            Actor, TEXT("Get_SourceClosed"), Closed) ||
        !HasExactSourceActor() || Closed)
    { bFailed = true; return false; }
    if (!Completed) { return false; }

    FCk_AutoTest_DivergenceFrame Next;
    Next.SchemaVersion = 1;
    Next.SourceGeneration = 1;
    Next.Sequence = 1;
    Next.bTimed = bSourceTimed;
    Next.SourceActor = Actor;
    if (!ck_auto_test_divergence_subject::ReadSnapshots(Actor, Next))
    { bFailed = true; return false; }
    Next.SourceNetIdentity = Get_IrisActorIdentity(Actor);
    if (!HasExactSourceActor())
    { bFailed = true; return false; }
    // Registration can settle after the source completes. A zero identity
    // cannot be published, but it is not proof of a malformed source actor.
    if (Next.SourceNetIdentity <= 0) { return false; }

    Next.bCompleted = true;
    Snapshot = Next;
    ForceNetUpdate();
    return true;
}

int64 ACk_AutoTest_NetSubject_Divergence_UE::Get_IrisActorIdentity(AActor* Actor)
{
    using namespace ck_auto_test_divergence_subject;
    if (!IsInGameThread() || !LiveActor(Actor) ||
        !Actor->HasActorBegunPlay()) { return 0; }
    auto* World = Actor->GetWorld();
    if (!LiveWorld(World)) { return 0; }
    if (!World->IsNetMode(NM_DedicatedServer) &&
        !World->IsNetMode(NM_ListenServer) &&
        !World->IsNetMode(NM_Client)) { return 0; }
    auto* Driver = World->GetNetDriver();
    if (!IsValid(Driver) || Driver->GetWorld() != World) { return 0; }
    auto* System = Driver->GetReplicationSystem();
    if (!IsValid(System)) { return 0; }
    auto* Bridge = System->GetReplicationBridge();
    if (!IsValid(Bridge)) { return 0; }
    const auto Handle = Bridge->GetReplicatedRefHandle(Actor);
    if (!Handle.IsValid() || Bridge->GetReplicatedObject(Handle) != Actor)
    { return 0; }
    const uint64 RawId = static_cast<uint64>(Handle.GetId());
    if (RawId == 0 || RawId >= static_cast<uint64>(TNumericLimits<int64>::Max()))
    { return 0; }
    return static_cast<int64>(RawId);
}

void ACk_AutoTest_NetSubject_Divergence_UE::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACk_AutoTest_NetSubject_Divergence_UE, Snapshot);
}

void ACk_AutoTest_NetSubject_Divergence_UE::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    bFailed = true;
    auto* Actor = SourceActor.Get();
    auto* World = GetWorld();
    const TWeakObjectPtr<AActor> Identity = SourceIdentity;
    const bool OwnsLiveSource =
        ck_auto_test_divergence_subject::LiveWorld(World) &&
        ck_auto_test_divergence_subject::LiveActor(Actor) &&
        Identity.HasSameIndexAndSerialNumber(TWeakObjectPtr<AActor>(Actor)) &&
        Actor->GetWorld() == World && Actor->GetOwner() == this &&
        ck_auto_test_divergence_subject::IsExactFamilyClass(
            Actor->GetClass(), bSourceTimed);
    if (OwnsLiveSource)
    {
        auto* Close = Actor->FindFunction(TEXT("Request_CloseSource"));
        if (IsValid(Close) && Close->NumParms == 0)
        { Actor->ProcessEvent(Close, nullptr); }
        if (ck_auto_test_divergence_subject::LiveWorld(World) &&
            ck_auto_test_divergence_subject::LiveActor(Actor) &&
            Identity.HasSameIndexAndSerialNumber(TWeakObjectPtr<AActor>(Actor)) &&
            Actor->GetWorld() == World && Actor->GetOwner() == this &&
            ck_auto_test_divergence_subject::IsExactFamilyClass(
                Actor->GetClass(), bSourceTimed))
        { Actor->Destroy(); }
    }
    Super::EndPlay(EndPlayReason);
}
