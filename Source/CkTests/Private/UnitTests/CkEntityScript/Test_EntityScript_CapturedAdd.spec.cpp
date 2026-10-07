#include "Test_EntityScript_CapturedAddProbe.h"

int32 UCk_Test_EntityScript_CapturedAddProbe_UE::ConstructCalls = 0;
int32 UCk_Test_EntityScript_CapturedAddProbe_UE::LastConstructMarker = 0;
int32 UCk_Test_EntityScript_CapturedAddProbe_UE::LastPayloadMarker = 0;

auto UCk_Test_EntityScript_CapturedAddProbe_UE::Get_EffectiveReplication() const
    -> ECk_Replication
{ return ECk_Replication::DoesNotReplicate; }

auto UCk_Test_EntityScript_CapturedAddProbe_UE::Construct(
    FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    ++ConstructCalls;
    LastConstructMarker = Marker;
    const auto* Payload = InSpawnParams.GetPtr<FCk_Test_EntityScript_CapturedAddParams>();
    LastPayloadMarker = Payload != nullptr ? Payload->Marker : -1;
    return ECk_EntityScript_ConstructionFlow::Finished;
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "../CkTest_CompletionListener.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment_Data.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/Registry/CkRegistry.h"
#include <Engine/World.h>
#include <StructUtils/InstancedStruct.h>
#include <UObject/StrongObjectPtr.h>

namespace ck_entity_script_capture_test
{
    struct FRun
    {
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> Listener;
        FCk_Handle WorldRoot;
        FCk_Handle Owner;
        FCk_Handle Target;
        FCk_Handle Carrier;
        FCk_EntityScript_AddCapture Capture;
        int32 PostConstructCalls = 0;
    };

    auto Completion(UCk_Test_CompletionListener_UE* Listener)
        -> FCk_Delegate_Request_OnCompleted
    {
        auto Delegate = FCk_Delegate_Request_OnCompleted{};
        Delegate.BindDynamic(Listener, &UCk_Test_CompletionListener_UE::OnRequestCompleted);
        return Delegate;
    }

    auto Population(const FCk_Handle& WorldRoot) -> int32
    {
        int32 Count = 0;
        WorldRoot.Get_RegistryView().View<ck::FTag_EntityJustCreated>().ForEach(
            [&Count](FCk_Entity) { ++Count; });
        return Count;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkEntityScript_CapturedAdd,
    "Ck.EntityScript.Admission.CapturedAdd.Production",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityScript_CapturedAdd::RunTest(const FString& Parameters)
{
    using namespace ck_entity_script_capture_test;
    constexpr int32 ExpectedMarker = 173;
    const auto Run = MakeShared<FRun>();
    Run->Listener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{
        NewObject<UCk_Test_CompletionListener_UE>(GetTransientPackage())};
    if (!TestNotNull(TEXT("captured Add completion listener"), Run->Listener.Get()))
    { return false; }
    auto* Class = UCk_Test_EntityScript_CapturedAddProbe_UE::StaticClass();
    auto* CDO = Class->GetDefaultObject<UCk_Test_EntityScript_CapturedAddProbe_UE>();
    if (!TestNotNull(TEXT("materialized nonreplicated probe CDO"), CDO) ||
        !TestTrue(TEXT("probe is nonreplicated"),
            CDO->Get_EffectiveReplication() == ECk_Replication::DoesNotReplicate))
    { return false; }
    auto Payload = FCk_Test_EntityScript_CapturedAddParams{};
    Payload.Marker = ExpectedMarker;
    const auto Params = FInstancedStruct::Make<FCk_Test_EntityScript_CapturedAddParams>(Payload);
    if (!TestTrue(TEXT("typed retained spawn payload"), Params.IsValid()))
    { return false; }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, Run, Class, CDO, Params](UWorld* World)
            {
                const auto Fail = [this](const TCHAR* Reason)
                { AddError(Reason); ck::auto_test::net::FailSession(Reason); };
                if (!IsValid(World))
                { Fail(TEXT("captured Add requires a real server PIE world")); return; }
                UCk_Test_EntityScript_CapturedAddProbe_UE::ConstructCalls = 0;
                UCk_Test_EntityScript_CapturedAddProbe_UE::LastConstructMarker = 0;
                UCk_Test_EntityScript_CapturedAddProbe_UE::LastPayloadMarker = 0;
                Run->WorldRoot =
                    UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
                Run->Owner =
                    UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
                if (!ck::IsValid(Run->WorldRoot) || !ck::IsValid(Run->Owner) ||
                    !Run->Owner.Has<ck::FFragment_LifetimeOwner>() ||
                    Run->Owner.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Run->WorldRoot)
                { Fail(TEXT("captured Add requires a real direct transient-world owner")); return; }
                Run->Target = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run->Owner);
                if (!ck::IsValid(Run->Target) ||
                    !Run->Target.Has<ck::FFragment_LifetimeOwner>() ||
                    Run->Target.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Run->Owner ||
                    UCk_Utils_EntityScript_UE::Has(Run->Target) ||
                    !UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Run->Target).IsEmpty())
                { Fail(TEXT("captured Add requires a fresh caller-owned target")); return; }
                // This caller owns the policy decision: the fresh child copied
                // its owner's net features, while this probe is nonreplicated.
                if (Run->Target.Has<ck::FFragment_Net_Params>())
                { Run->Target.Remove<ck::FFragment_Net_Params>(); }
                if (Run->Target.Has<ck::FTag_HasAuthority>())
                { Run->Target.Remove<ck::FTag_HasAuthority>(); }
                if (Run->Target.Has<ck::FTag_NetMode_IsHost>())
                { Run->Target.Remove<ck::FTag_NetMode_IsHost>(); }
                if (Run->Target.Has<ck::FTag_NetMode_IsClient>())
                { Run->Target.Remove<ck::FTag_NetMode_IsClient>(); }
                const auto BeforePopulation = Population(Run->WorldRoot);
                const auto Delegate = Completion(Run->Listener.Get());
                auto DefaultTarget = FCk_Handle{};
                const auto BadTarget = UCk_Utils_EntityScript_UE::TryAddCaptured(
                    DefaultTarget, Class, Params, nullptr, Delegate);
                const auto BadClass = UCk_Utils_EntityScript_UE::TryAddCaptured(
                    Run->Target, TSubclassOf<UCk_EntityScript_UE>{}, Params,
                    nullptr, Delegate);
                if (ck::IsValid(BadTarget.Pending) || ck::IsValid(BadTarget.RequestCarrier) ||
                    BadTarget.bAllocationAttempted ||
                    ck::IsValid(BadClass.Pending) || ck::IsValid(BadClass.RequestCarrier) ||
                    BadClass.bAllocationAttempted ||
                    Population(Run->WorldRoot) != BeforePopulation ||
                    !UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Run->Target).IsEmpty() ||
                    UCk_Utils_EntityScript_UE::Has(Run->Target) ||
                    Run->Listener->_TimesRequestCompleted != 0 ||
                    UCk_Test_EntityScript_CapturedAddProbe_UE::ConstructCalls != 0)
                { Fail(TEXT("default captured Add inputs must reject without allocation, callback or Construct")); return; }

                const auto Capture = UCk_Utils_EntityScript_UE::TryAddCaptured(
                    Run->Target, Class, Params,
                    [Run](FCk_Handle) { ++Run->PostConstructCalls; }, Delegate);
                Run->Capture = Capture;
                Run->Carrier = Capture.RequestCarrier;
                if (!Capture.bAllocationAttempted || !ck::IsValid(Capture.Pending) ||
                    Capture.Pending.Get_EntityUnderConstruction() != Run->Target ||
                    !ck::IsValid(Run->Carrier) ||
                    !Run->Carrier.Has<ck::FFragment_LifetimeOwner>() ||
                    Run->Carrier.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Run->Target ||
                    !Run->Target.Has<ck::FFragment_LifetimeDependents>() ||
                    Run->Target.Get<ck::FFragment_LifetimeDependents>().Get_Entities().Num() != 1 ||
                    Run->Target.Get<ck::FFragment_LifetimeDependents>().Get_Entities()[0] != Run->Carrier ||
                    !Run->Carrier.Has<ck::FFragment_EntityScript_RequestSpawnEntity>())
                { Fail(TEXT("captured Add did not return the exact queued raw carrier")); return; }
                const auto& Request =
                    Run->Carrier.Get<ck::FFragment_EntityScript_RequestSpawnEntity>();
                const auto* CapturedPayload =
                    Request.Get_SpawnParams().GetPtr<FCk_Test_EntityScript_CapturedAddParams>();
                if (Request.Get_NewEntity() != Run->Target ||
                    Request.Get_Owner() != Run->Owner ||
                    Request.Get_ContextOwner() != Run->Owner ||
                    Request.Get_EntityScriptClassArchetype().Get() != CDO ||
                    Request.Get_DestroyTargetOnFailure() ||
                    CapturedPayload == nullptr || CapturedPayload->Marker != ExpectedMarker ||
                    Run->Listener->_TimesRequestCompleted != 0 ||
                    Run->PostConstructCalls != 0 ||
                    UCk_Test_EntityScript_CapturedAddProbe_UE::ConstructCalls != 0)
                { Fail(TEXT("captured Add request payload or queued-only callback boundary changed")); }
            })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            return IsValid(Run->Listener.Get()) &&
                Run->Listener->_TimesRequestCompleted == 1 &&
                Run->Listener->_LastRequestResult == ECk_Request_OperationResult::Succeeded &&
                UCk_Test_EntityScript_CapturedAddProbe_UE::ConstructCalls == 1 &&
                !ck::IsValid(Run->Carrier, ck::IsValid_Policy_IncludePendingKill{});
        }), 15.0, TEXT("real captured Add request drains with one completion and carrier retirement")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            const bool Clean = ck::IsValid(Run->Target) &&
                ck::IsValid(Run->Target, ck::IsValid_Policy_IncludePendingKill{}) &&
                UCk_Utils_EntityScript_UE::Has(Run->Target) &&
                !ck::IsValid(Run->Carrier, ck::IsValid_Policy_IncludePendingKill{}) &&
                Run->Capture.RequestCarrier == Run->Carrier &&
                UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Run->Target).IsEmpty() &&
                Run->Listener->_TimesRequestCompleted == 1 &&
                Run->Listener->_LastRequestResult == ECk_Request_OperationResult::Succeeded &&
                Run->PostConstructCalls == 1 &&
                UCk_Test_EntityScript_CapturedAddProbe_UE::ConstructCalls == 1 &&
                UCk_Test_EntityScript_CapturedAddProbe_UE::LastConstructMarker == 173 &&
                UCk_Test_EntityScript_CapturedAddProbe_UE::LastPayloadMarker == 173;
            return TestTrue(TEXT("retained caller target constructs once, retains exact payload and owns no carrier after drain"), Clean);
        }), TEXT("captured Add processor-drain proof")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
