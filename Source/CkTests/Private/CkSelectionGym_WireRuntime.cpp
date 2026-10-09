#include "CkSelectionGym_WireRuntime.h"

#include "UObject/UnrealType.h"

bool UCkSelectionGym_WireRuntime_UE::Get_IsSameFrame(
    const FCkSelectionGym_WireSnapshot& A,
    const FCkSelectionGym_WireSnapshot& B)
{
    return FCkSelectionGym_WireSnapshot::StaticStruct()->
        CompareScriptStruct(&A, &B, PPF_None);
}

bool UCkSelectionGym_WireRuntime_UE::Get_IsDefaultFrame(
    const FCkSelectionGym_WireSnapshot& Frame)
{
    const FCkSelectionGym_WireSnapshot Default;
    return FCkSelectionGym_WireSnapshot::StaticStruct()->
        CompareScriptStruct(&Frame, &Default, PPF_None);
}
