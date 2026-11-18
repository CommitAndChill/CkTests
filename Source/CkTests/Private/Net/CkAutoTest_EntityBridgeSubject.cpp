#include "CkTests/Net/CkAutoTest_EntityBridgeSubject.h"

#include "CkCore/Ensure/CkEnsure_Tracker.h"

#include "CkEcs/EntityBridge/CkEntityBridge_Fragment.h"
#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

#include <StructUtils/InstancedStruct.h>
#include <tuple>

// --------------------------------------------------------------------------------------------------------------------

namespace ck::auto_test::entity_bridge
{
    auto
        Get_LinkedEntityCount(
            UWorld* InWorld,
            const TWeakObjectPtr<const AActor>& InActor)
        -> int32
    {
        if (ck::Is_NOT_Valid(InWorld, ck::IsValid_Policy_NullptrOnly{}))
        { return 0; }

        auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*InWorld);

        if (ck::Is_NOT_Valid(TransientEntity))
        { return 0; }

        auto Count = 0;

        TransientEntity.View<ck::FFragment_EntityBridge>().ForEach(
            [&](FCk_Entity, const ck::FFragment_EntityBridge& InFragment) -> void
            {
                if (InFragment.Get_Actor().HasSameIndexAndSerialNumber(InActor))
                { ++Count; }
            });

        return Count;
    }

    auto
        Get_ScriptEntityCount(
            UWorld* InWorld,
            const UClass* InScriptClass)
        -> int32
    {
        if (ck::Is_NOT_Valid(InWorld, ck::IsValid_Policy_NullptrOnly{}))
        { return 0; }

        auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*InWorld);

        if (ck::Is_NOT_Valid(TransientEntity))
        { return 0; }

        auto Count = 0;

        TransientEntity.View<ck::FFragment_EntityScript>().ForEach(
            [&](FCk_Entity, const ck::FFragment_EntityScript& InFragment) -> void
            {
                const auto& Script = InFragment.Get_Script();

                if (ck::Is_NOT_Valid(Script))
                { return; }

                if (Script->GetClass() == InScriptClass)
                { ++Count; }
            });

        return Count;
    }

    auto
        Get_EnsureCount_FromFiles(
            const FString& InFileNameFragment)
        -> int64
    {
        auto Count = int64{0};

        for (const auto& Occurrence : ck::ensure::Get_EnsureOccurrenceTracker().GetSnapshot())
        {
            if (Occurrence.Signature.File.ToString().Contains(InFileNameFragment))
            { Count += static_cast<int64>(Occurrence.Count); }
        }

        return Count;
    }
}

// --------------------------------------------------------------------------------------------------------------------

UCk_AutoTest_EntityBridge_EntityScript_UE::
    UCk_AutoTest_EntityBridge_EntityScript_UE()
{
    _InstancingPolicy = ECk_EntityScript_InstancingPolicy::InstancedPerEntity;
}

auto
    UCk_AutoTest_EntityBridge_EntityScript_UE::
    BeginPlay()
    -> void
{
    Super::BeginPlay();

    auto* Subject = Cast<ACk_AutoTest_EntityBridgeSubject>(UCk_Utils_EntityBridge_UE::TryGet_Actor(DoGet_ScriptEntity()));

    // A script with no subject records nothing; the test then fails on the missing "ScriptBeginPlay".
    if (ck::Is_NOT_Valid(Subject))
    { return; }

    Subject->Request_RecordEvent(FName{ck::auto_test::entity_bridge::EventName_ScriptBeginPlay});
}

// --------------------------------------------------------------------------------------------------------------------

UCk_AutoTest_EntityBridge_LocalEntityScript_UE::
    UCk_AutoTest_EntityBridge_LocalEntityScript_UE()
{
    _Replication = ECk_Replication::DoesNotReplicate;
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_AutoTest_EntityBridge_NeverFinishesEntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    std::ignore = Super::Construct(InHandle, InSpawnParams);

    return ECk_EntityScript_ConstructionFlow::Continue;
}

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_EntityBridgeSubject::
    ACk_AutoTest_EntityBridgeSubject()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    bReplicateUsingRegisteredSubObjectList = true;
    PrimaryActorTick.bCanEverTick = false;

    // A movable root lets the script's Construct give the entity a Transform, as on every other test subject.
    auto* Root = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
    Root->SetMobility(EComponentMobility::Movable);
    RootComponent = Root;

    _EntityBridge = CreateDefaultSubobject<UCk_EntityBridge_ActorComponent_UE>(TEXT("EntityBridge"));
    _EntityScriptClass = UCk_AutoTest_EntityBridge_EntityScript_UE::StaticClass();
}

auto
    ACk_AutoTest_EntityBridgeSubject::
    Find(
        UWorld* InWorld,
        TSubclassOf<ACk_AutoTest_EntityBridgeSubject> InClass)
    -> ACk_AutoTest_EntityBridgeSubject*
{
    if (ck::Is_NOT_Valid(InWorld, ck::IsValid_Policy_NullptrOnly{}))
    { return nullptr; }

    for (auto It = TActorIterator<ACk_AutoTest_EntityBridgeSubject>(InWorld); It; ++It)
    {
        if (It->GetClass() == InClass.Get())
        { return *It; }
    }

    return nullptr;
}

auto
    ACk_AutoTest_EntityBridgeSubject::
    Request_RecordEvent(
        FName InEvent)
    -> void
{
    _Events.Add(InEvent);
}

auto
    ACk_AutoTest_EntityBridgeSubject::
    BeginPlay()
    -> void
{
    Super::BeginPlay();

    _ParamValue = HasAuthority()
        ? ck::auto_test::entity_bridge::ParamValue_Authority
        : ck::auto_test::entity_bridge::ParamValue_NonAuthority;

    // Bind first, request last.
    DoBind_Promises();
    DoRequest_Entity();
}

auto
    ACk_AutoTest_EntityBridgeSubject::
    DoBind_Promises()
    -> void
{
    auto ConstructedDelegate = FCk_Delegate_EntityScript_Constructed{};
    ConstructedDelegate.BindDynamic(this, &ACk_AutoTest_EntityBridgeSubject::OnEntityConstructed);
    _EntityBridge->Promise_OnEntityConstructed(ConstructedDelegate);

    auto ReadyDelegate = FCk_Delegate_EntityBridge_OnEntityReady{};
    ReadyDelegate.BindDynamic(this, &ACk_AutoTest_EntityBridgeSubject::OnEntityReady);
    _EntityBridge->Promise_OnEntityReady(ReadyDelegate);
}

auto
    ACk_AutoTest_EntityBridgeSubject::
    DoRequest_Entity()
    -> void
{
    _EntityBridge->Request_SpawnLinkedEntity_WithParams(_EntityScriptClass,
        FInstancedStruct::Make(FCk_AutoTest_EntityBridge_SpawnParams{_ParamValue}));
}

void
    ACk_AutoTest_EntityBridgeSubject::
    OnEntityConstructed(
        FCk_Handle_EntityScript InEntity)
{
    _Events.Add(FName{ck::auto_test::entity_bridge::EventName_Constructed});
}

void
    ACk_AutoTest_EntityBridgeSubject::
    OnEntityReady(
        AActor* InActor,
        FCk_Handle InEntity)
{
    _Events.Add(FName{ck::auto_test::entity_bridge::EventName_Ready});

    _WasReplicationCompleteAtReady =
        UCk_Utils_EntityReplicationDriver_UE::Has(InEntity) &&
        UCk_Utils_EntityReplicationDriver_UE::Get_IsReplicationComplete(InEntity);
}

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_EntityBridgeSubject_LocalScript::
    ACk_AutoTest_EntityBridgeSubject_LocalScript()
{
    _EntityScriptClass = UCk_AutoTest_EntityBridge_LocalEntityScript_UE::StaticClass();
}

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_EntityBridgeSubject_NotReplicated::
    ACk_AutoTest_EntityBridgeSubject_NotReplicated()
{
    bReplicates = false;
}

// --------------------------------------------------------------------------------------------------------------------

auto
    ACk_AutoTest_EntityBridgeSubject_NeverRequests::
    DoRequest_Entity()
    -> void
{
}

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_EntityBridgeSubject_NeverFinishes::
    ACk_AutoTest_EntityBridgeSubject_NeverFinishes()
{
    _EntityScriptClass = UCk_AutoTest_EntityBridge_NeverFinishesEntityScript_UE::StaticClass();
}

// --------------------------------------------------------------------------------------------------------------------

auto
    ACk_AutoTest_EntityBridgeSubject_LinkedProbe::
    DoBind_Promises()
    -> void
{
    Super::DoBind_Promises();

    UCk_Utils_EntityBridge_UE::Promise_OnEntityLinked(this,
        [WeakThis = TWeakObjectPtr<ThisType>{this}](AActor* InActor, FCk_Handle InEntity) -> void
        {
            if (ck::Is_NOT_Valid(WeakThis))
            { return; }

            WeakThis->DoRecord_Linked(InActor, InEntity);
        });
}

auto
    ACk_AutoTest_EntityBridgeSubject_LinkedProbe::
    DoRecord_Linked(
        AActor* InActor,
        const FCk_Handle& InEntity)
    -> void
{
    ++_LinkedFireCount;
    _LinkedEntity = InEntity;
    _LinkedActor = InActor;
    _EventCountAtLinked = Get_Events().Num();
    _StateAtLinked = Get_EntityBridge()->Get_State();
}

// --------------------------------------------------------------------------------------------------------------------

void
    UCk_AutoTest_EntityBridge_Capturer_UE::
    OnEntityConstructed(
        FCk_Handle_EntityScript InEntity)
{
    ++_ConstructedCount;
    _ConstructedEntity = InEntity;
}

void
    UCk_AutoTest_EntityBridge_Capturer_UE::
    OnEntityReady(
        AActor* InActor,
        FCk_Handle InEntity)
{
    ++_ReadyCount;
    _ReadyEntity = InEntity;
    _ReadyActor = InActor;
}

void
    UCk_AutoTest_EntityBridge_Capturer_UE::
    OnRequestCompleted(
        FCk_Handle InRequestOwner,
        ECk_Request_OperationResult InResult)
{
    ++_RequestCompletedCount;
    _RequestResult = InResult;
}

// --------------------------------------------------------------------------------------------------------------------
