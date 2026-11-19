#pragma once

#include "CoreMinimal.h"

#include "CkTests/Net/CkAutoTest_NetSubject_StateMachineEntityScript.h"
#include "CkTests/Net/CkAutoTest_NetSubject_StateMachineOwningClientPawn.h"

#include "CkAutoTest_NetSubject_StateMachineSiblingSubSm.generated.h"

// --------------------------------------------------------------------------------------------------------------------
//
// Subjects for the sibling sub-SM address tests (Ck.StateMachine.Net.SubSmSiblings.*). Each root state hosts more
// than one sub-SM task; the AngelScript topologies live in
// Script/CkStateMachine/CkAutoTest_StateMachine_Net_SubSmSiblings.as and every transition is gated on the byte
// "input" attribute ByteAttribute.Gyms.Intent.R reaching a threshold.
//
// The owning-client variants reuse the SubSmGated entity script (OwningClientAuthoritative + WithHistory, AutoStart
// Disabled, byte input DoesNotReplicate so only the owning client's copy moves) and only swap the root state.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N1EntityScript_UE
    : public UCk_AutoTest_NetSubject_StateMachineOwningClientSubSmGatedEntityScript_UE
{
    GENERATED_BODY()

protected:
    virtual auto Get_InitialStateClass() const -> TSubclassOf<UCk_SmState_EntityScript> override;
};

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N3EntityScript_UE
    : public UCk_AutoTest_NetSubject_StateMachineOwningClientSubSmGatedEntityScript_UE
{
    GENERATED_BODY()

protected:
    virtual auto Get_InitialStateClass() const -> TSubclassOf<UCk_SmState_EntityScript> override;
};

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N4EntityScript_UE
    : public UCk_AutoTest_NetSubject_StateMachineOwningClientSubSmGatedEntityScript_UE
{
    GENERATED_BODY()

protected:
    virtual auto Get_InitialStateClass() const -> TSubclassOf<UCk_SmState_EntityScript> override;
};

// --------------------------------------------------------------------------------------------------------------------
//
// ServerAuthoritative + WithHistory, AutoStart on Setup, byte input REPLICATED: the server raises the input, the
// root's own transition replicates server -> client, and every machine's sub-SMs (DoesNotReplicate, self-authoritative
// under a server-authoritative root) derive from the replicated input.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N2EntityScript_UE
    : public UCk_AutoTest_NetSubject_StateMachineEntityScript_UE
{
    GENERATED_BODY()

public:
    virtual auto Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;

protected:
    virtual auto Get_InitialStateClass() const -> TSubclassOf<UCk_SmState_EntityScript> override;
    virtual auto DoStashStateMachine(AActor* InOwningActor, const FCk_Handle_StateMachine& InSM) -> void override;
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N1_Pawn : public ACk_AutoTest_NetSubject_StateMachineOwningClient_Pawn
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N1_Pawn();
};

UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N2_Pawn : public ACk_AutoTest_NetSubject_StateMachineOwningClient_Pawn
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N2_Pawn();
};

UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N3_Pawn : public ACk_AutoTest_NetSubject_StateMachineOwningClient_Pawn
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N3_Pawn();
};

UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N4_Pawn : public ACk_AutoTest_NetSubject_StateMachineOwningClient_Pawn
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N4_Pawn();
};

// --------------------------------------------------------------------------------------------------------------------
