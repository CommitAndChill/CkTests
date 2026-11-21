#pragma once

#include "CoreMinimal.h"

#include "CkEcs/EntityConstructionScript/CkEntity_ConstructionScript.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/Tag/CkTag.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"

#include "CkTests/Net/CkAutoTest_NetSubject.h"

#include "CkAutoTest_NetSubject_LifetimeTransfer.generated.h"

// --------------------------------------------------------------------------------------------------------------------
//
// Subject for the Ck.EntityLifetime.Net.* lifetime-transfer rows (CkEntityLifetime_Net_TransferLifetimeOwner.spec.cpp).
//
// Topology, built on the server inside each owner's own construction:
//
//   Root (actor-bridged)
//   |- OwnerA  - Child            (C: built inside A's construction, so a construction dependent of A)
//   |          - DefinitionChild  (D: definition-built through Request_BuildAndReplicate inside A's construction)
//   |- OwnerB  - Holder           (H: plain entity WITHOUT a replication driver, built on every machine)
//   '- GrandOwner - MidOwner - ChildOfMid   (G - W - C2: C2 built inside W's construction)
//
// Replicated children are spawned on the host only, so a client holds no pending entities. Every replicated
// member below Root carries FTag_LifetimeTransfer_Member plus its role tag, so each world finds its own copies by tag.
//
// --------------------------------------------------------------------------------------------------------------------

namespace ck::auto_test::lifetime_transfer
{
    CK_DEFINE_ECS_TAG(FTag_LifetimeTransfer_Member);

    CK_DEFINE_ECS_TAG(FTag_LifetimeTransfer_Root);
    CK_DEFINE_ECS_TAG(FTag_LifetimeTransfer_OwnerA);
    CK_DEFINE_ECS_TAG(FTag_LifetimeTransfer_OwnerB);
    CK_DEFINE_ECS_TAG(FTag_LifetimeTransfer_Child);
    CK_DEFINE_ECS_TAG(FTag_LifetimeTransfer_DefinitionChild);
    CK_DEFINE_ECS_TAG(FTag_LifetimeTransfer_GrandOwner);
    CK_DEFINE_ECS_TAG(FTag_LifetimeTransfer_MidOwner);
    CK_DEFINE_ECS_TAG(FTag_LifetimeTransfer_ChildOfMid);
    CK_DEFINE_ECS_TAG(FTag_LifetimeTransfer_Holder);

    // Bumped by every Construct of the Child script on this machine's entity.
    struct CKTESTS_API FFragment_LifetimeTransfer_ConstructCount
    {
        CK_GENERATED_BODY(FFragment_LifetimeTransfer_ConstructCount);

    public:
        int32 _Count = 0;
    };

    inline constexpr int32 NumReplicatedMembers = 7;
}

// --------------------------------------------------------------------------------------------------------------------

UCLASS()
class CKTESTS_API UCk_AutoTest_LifetimeTransfer_DefinitionChild_ConstructionScript_UE : public UCk_Entity_ConstructionScript_PDA
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(UCk_AutoTest_LifetimeTransfer_DefinitionChild_ConstructionScript_UE);

protected:
    virtual auto
    DoConstruct_Implementation(
        FCk_Handle& InHandle) const
        -> void override;
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_LifetimeTransfer_Child_EntityScript_UE : public UCk_EntityScript_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_LifetimeTransfer_ChildOfMid_EntityScript_UE : public UCk_EntityScript_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_LifetimeTransfer_OwnerA_EntityScript_UE : public UCk_EntityScript_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_LifetimeTransfer_OwnerB_EntityScript_UE : public UCk_EntityScript_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_LifetimeTransfer_MidOwner_EntityScript_UE : public UCk_EntityScript_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_LifetimeTransfer_GrandOwner_EntityScript_UE : public UCk_EntityScript_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_LifetimeTransfer_Root_EntityScript_UE : public UCk_EntityScript_WithActor_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API ACk_AutoTest_NetSubject_LifetimeTransfer_UE : public ACk_AutoTest_NetSubject
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_LifetimeTransfer_UE();
};

// --------------------------------------------------------------------------------------------------------------------
