#include "CkTests/Net/CkAutoTest_NetSubject_StateMachineSiblingSubSm.h"

#include "CkAttribute/ByteAttribute/CkByteAttribute_Utils.h"
#include "CkAttribute/ByteAttribute/CkByteAttribute_Fragment_Data.h"
#include "CkStateMachine/State/EntityScripts/CkSmState_EntityScript.h"

#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_sm_sibling_subject
{
    // AS state classes are not visible to C++ at compile time; resolved by path (the leading 'U' is stripped from the
    // AS class name). Null when the AS topology failed to compile, which StateMachine Add then ensures on.
    auto
    DoLoadAsState(
        const TCHAR* InPath) -> TSubclassOf<UCk_SmState_EntityScript>
    {
        return FSoftClassPath{InPath}.TryLoadClass<UCk_SmState_EntityScript>();
    }
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N1EntityScript_UE::
    Get_InitialStateClass() const
    -> TSubclassOf<UCk_SmState_EntityScript>
{
    return ck_sm_sibling_subject::DoLoadAsState(TEXT("/Script/Angelscript.Ck_SmNetSibTest_N1_Hold"));
}

auto
    UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N3EntityScript_UE::
    Get_InitialStateClass() const
    -> TSubclassOf<UCk_SmState_EntityScript>
{
    return ck_sm_sibling_subject::DoLoadAsState(TEXT("/Script/Angelscript.Ck_SmNetSibTest_N3_Hold"));
}

auto
    UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N4EntityScript_UE::
    Get_InitialStateClass() const
    -> TSubclassOf<UCk_SmState_EntityScript>
{
    return ck_sm_sibling_subject::DoLoadAsState(TEXT("/Script/Angelscript.Ck_SmNetSibTest_N4_Hold"));
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N2EntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    // Super adds the SM (AutoStart OnSetup: it starts on a later pump, after the input below exists).
    const auto Flow = Super::Construct(InHandle, InSpawnParams);

    // Replicates: the server raises it and every machine's sub-SMs derive from the same value.
    const auto InputTag = FGameplayTag::RequestGameplayTag(FName{TEXT("ByteAttribute.Gyms.Intent.R")});
    auto InputParams = FCk_ByteAttribute_Spec{InputTag, static_cast<uint8>(0)};
    UCk_Utils_ByteAttribute_UE::Add(InHandle, InputParams, ECk_Replication::Replicates);

    return Flow;
}

auto
    UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N2EntityScript_UE::
    Get_InitialStateClass() const
    -> TSubclassOf<UCk_SmState_EntityScript>
{
    return ck_sm_sibling_subject::DoLoadAsState(TEXT("/Script/Angelscript.Ck_SmNetSibTest_N2_Wait"));
}

auto
    UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N2EntityScript_UE::
    DoStashStateMachine(
        AActor* InOwningActor,
        const FCk_Handle_StateMachine& InSM)
    -> void
{
    if (auto* PawnSubject = Cast<ACk_AutoTest_NetSubject_StateMachineOwningClient_Pawn>(InOwningActor))
    { PawnSubject->_TestStateMachine = InSM; }
}

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N1_Pawn::
    ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N1_Pawn()
{
    _EntityScriptClass = UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N1EntityScript_UE::StaticClass();
}

ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N2_Pawn::
    ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N2_Pawn()
{
    _EntityScriptClass = UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N2EntityScript_UE::StaticClass();
}

ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N3_Pawn::
    ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N3_Pawn()
{
    _EntityScriptClass = UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N3EntityScript_UE::StaticClass();
}

ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N4_Pawn::
    ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N4_Pawn()
{
    _EntityScriptClass = UCk_AutoTest_NetSubject_StateMachineSiblingSubSm_N4EntityScript_UE::StaticClass();
}

// --------------------------------------------------------------------------------------------------------------------
