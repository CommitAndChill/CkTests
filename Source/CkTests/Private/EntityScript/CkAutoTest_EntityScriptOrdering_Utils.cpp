#include "CkTests/EntityScript/CkAutoTest_EntityScriptOrdering_Utils.h"

#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_Utils_AutoTest_EntityScriptOrdering_UE::
    Get_HasOrderingSequence(
        const FCk_Handle& InScriptEntity)
    -> bool
{
    return ck::IsValid(InScriptEntity) && InScriptEntity.Has<ck::FFragment_EntityScript_OrderingSequence>();
}

auto
    UCk_Utils_AutoTest_EntityScriptOrdering_UE::
    TryGet_Ordering(
        const FCk_Handle& InScriptEntity,
        FGameplayTag& OutGroup,
        int64& OutSequence)
    -> bool
{
    if (NOT Get_HasOrderingSequence(InScriptEntity))
    { return false; }

    const auto& Ordering = InScriptEntity.Get<ck::FFragment_EntityScript_OrderingSequence>();
    OutGroup = Ordering.Get_Group();
    OutSequence = Ordering.Get_Sequence();
    return true;
}

// --------------------------------------------------------------------------------------------------------------------
