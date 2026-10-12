#include "Test_EntityScript_PendingRendezvous_RetireProbe.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkTests/Net/CkAutoTest_NetSubject_DependentSubtree.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"

ACk_Test_PendingRendezvous_MinimalSubject_UE::
    ACk_Test_PendingRendezvous_MinimalSubject_UE()
{
    _EntityScriptClass = UCk_EntityScript_WithActor_UE::StaticClass();
}

void UCk_Test_PendingRendezvous_RetireProbe_UE::OnConstructed(
    FCk_Handle_EntityScript InConstructed)
{
    ++CallbackCount;
    Constructed = InConstructed;
    if (CallbackCount != 1 || NOT ck::IsValid(ExpectedOwner) ||
        NOT ck::IsValid(Pending) || NOT ck::IsValid(Constructed) ||
        Constructed == Pending ||
        (ck::IsValid(ExpectedConstructed) && Constructed != ExpectedConstructed) ||
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Pending) != ExpectedOwner ||
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Constructed) != ExpectedOwner ||
        NOT Constructed.Has<ck::FFragment_EntityScript>())
    { return; }

    auto* Script = Constructed.Get<ck::FFragment_EntityScript>().Get_Script().Get();
    if (NOT ck::IsValid(Script) ||
        Script->GetClass() != UCk_AutoTest_NetSubject_DependentChild_EntityScript_UE::StaticClass())
    { return; }

    bExactAtCallback = true;
    bRequestedBoth = true;
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Pending);
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Constructed);
}
