#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Persistence/CkPersistenceHydration_Processor.h"
#include "CkEcs/Persistence/CkPersistenceInitialization.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkEntityTag/CkEntityTag_Fragment.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Snapshot_InitializationEpochDelivery,
    "Ck.Snapshot.Initialization.EpochDelivery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCk_Snapshot_InitializationEpochDelivery::RunTest(const FString&)
{
    using namespace ck::persistence;
    using EOutcome = ck::persistence_apply::EApplyOutcome;
    using FRequests = ck::FFragment_EntityTag_Requests;
    auto World = ck::FEcsWorld{};
    auto Target = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(World.Get_Registry());
    const auto* Type = FCk_SaveData_EntityTags::StaticStruct();
    auto Payload = FCk_SaveData_EntityTags{};
    Payload.Set_TagNames({FName{TEXT("Saved")}}).Set_Counts({1});
    const auto Data = FInstancedStruct::Make(Payload);
    auto Since = 0.0;
    constexpr uint64 Epoch = 99;
    TestTrue(TEXT("Baseline expected"), TryDeclare(Target, Type, Epoch, EInitializationDisposition::BaselineExpected));
    TestTrue(TEXT("Stale queue entry was rejected"), ck::persistence_apply::ApplyQueued(Target, Data, Epoch - 1, Since) ==
        EOutcome::DroppedRejected);
    TestFalse(TEXT("Stale payload never invoked EntityTags restore"), Target.Has<FRequests>());
    TestTrue(TEXT("Current ticket remains open"), Query(Target, Type).Get_Disposition() == EInitializationDisposition::BaselineExpected);
    TestTrue(TEXT("Matching delivery invoked production handler"), ck::persistence_apply::ApplyQueued(Target, Data, Epoch, Since) ==
        EOutcome::Applied);
    TestTrue(TEXT("One restore operation was enqueued"), Target.Has<FRequests>() && Target.Get<FRequests>().Get_Requests().Num() == 1);
    TestTrue(TEXT("Handler accepted its baseline"), Query(Target, Type).Get_Disposition() == EInitializationDisposition::BaselineCommitted);
    TestTrue(TEXT("Coherent release succeeds"), TryRelease(Target, Type, Epoch));
    TestTrue(TEXT("Post-release delivery cannot apply"), ck::persistence_apply::ApplyQueued(Target, Data, Epoch, Since) ==
        EOutcome::DroppedRejected);
    TestTrue(TEXT("Post-release input left queue unchanged"), Target.Has<FRequests>() && Target.Get<FRequests>().Get_Requests().Num() == 1);
    return !HasAnyErrors();
}

#endif
