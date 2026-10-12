#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Test_MessagingBindingProbe.h"
#include "../../CkSnapshot/Test_Snapshot_DynamicFragment_Fixtures.h"
#include "CkCore/Ensure/CkEnsure.h"
#include "CkCore/Validation/CkUntracedStructSafety.h"
#include "CkMessaging/CkMessaging_Fragment.h"
#include "NativeGameplayTags.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkMessaging/CkMessaging_Utils.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_messaging_binding_test
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MessagingBindingCensus, "Message.BindingCensusTest");
    auto NativeMessage(FCk_Handle, FGameplayTag, FInstancedStruct) -> void {}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_MessagingBindingCensus,
    "Ck.Messaging.BindingCensus.PureExactCallbacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkTest_MessagingBindingCensus::RunTest(const FString&)
{
    auto EcsWorld = ck::FEcsWorld{};
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(EcsWorld.Get_Registry());
    const auto Message = FGameplayTag{ck_messaging_binding_test::TAG_MessagingBindingCensus};
    auto Probe = TStrongObjectPtr<UCk_Test_MessagingBindingProbe>{NewObject<UCk_Test_MessagingBindingProbe>()};
    auto Callback = FCk_Delegate_Messaging_OnBroadcast{};
    Callback.BindDynamic(Probe.Get(), &UCk_Test_MessagingBindingProbe::OnMessage);
    auto Other = FCk_Delegate_Messaging_OnBroadcast{};
    Other.BindDynamic(Probe.Get(), &UCk_Test_MessagingBindingProbe::OnOther);
    constexpr auto Keep = ECk_Signal_PostFireBehavior::DoNothing;
    constexpr auto Once = ECk_Signal_PostFireBehavior::Unbind;
    constexpr auto Ignore = ECk_Signal_BindingPolicy::IgnorePayloadInFlight;
    const auto Total = [&](ECk_Signal_PostFireBehavior Policy)
    { return UCk_Utils_Messaging_UE::Get_NumBoundTo_OnBroadcast(Owner, Message, Policy); };
    const auto Exact = [&](const FCk_Delegate_Messaging_OnBroadcast& Delegate, ECk_Signal_PostFireBehavior Policy)
    { return UCk_Utils_Messaging_UE::Get_NumUnconditionalBindingsTo_OnBroadcast(Owner, Message, Delegate, Policy); };

    TestEqual(TEXT("missing endpoint has no listeners"), Total(Keep), 0);
    TestEqual(TEXT("missing endpoint has no exact callback"), Exact(Callback, Keep), 0);
    TestFalse(TEXT("pure census never creates endpoint"),
        ck::IsValid(UCk_Utils_Messaging_UE::TryGet_MessengerEntity(Owner, Message)));
    TestEqual(TEXT("invalid owner rejected without callback"),
        UCk_Utils_Messaging_UE::Get_NumUnconditionalBindingsTo_OnBroadcast({}, Message, Callback, Keep), 0);
    TestEqual(TEXT("invalid tag rejected"),
        UCk_Utils_Messaging_UE::Get_NumBoundTo_OnBroadcast(Owner, {}, Keep), 0);
    TestEqual(TEXT("invalid enum rejected"), Total(static_cast<ECk_Signal_PostFireBehavior>(255)), 0);
    TestEqual(TEXT("invalid delegate rejected"), Exact({}, Keep), 0);

    UCk_Utils_Messaging_UE::BindTo_OnBroadcast(Owner, Message, Callback, Ignore, Keep);
    const auto Endpoint = UCk_Utils_Messaging_UE::TryGet_MessengerEntity(Owner, Message);
    TestTrue(TEXT("production bind created endpoint"), ck::IsValid(Endpoint));
    TestEqual(TEXT("one live listener"), Total(Keep), 1);
    TestEqual(TEXT("exact object and function match"), Exact(Callback, Keep), 1);
    TestEqual(TEXT("different function is not equivalent"), Exact(Other, Keep), 0);
    TestEqual(TEXT("wrong postfire policy is not equivalent"), Exact(Callback, Once), 0);
    TestEqual(TEXT("queries never execute callbacks"), Probe->Received, 0);
    // One deliberate duplicate bind is emitted through both CkEnsure and CkEnsures.
    AddExpectedError(TEXT("The Delegate Signal already has the Delegate"),
        EAutomationExpectedErrorFlags::Contains, 2);
    UCk_Utils_Messaging_UE::BindTo_OnBroadcast(Owner, Message, Callback, Ignore, Keep);
    TestEqual(TEXT("production duplicate bind remains one callback"), Total(Keep), 1);
    UCk_Utils_Messaging_UE::BindTo_OnBroadcast(Owner, Message, Other, Ignore, Keep);
    TestEqual(TEXT("unexpected extra callback changes total"), Total(Keep), 2);
    UCk_Utils_Messaging_UE::UnbindFrom_OnBroadcast(Owner, Message, Other);
    UCk_Utils_Messaging_UE::UnbindFrom_OnBroadcast(Owner, Message, Callback);
    TestTrue(TEXT("unbind leaves existing endpoint"),
        ck::IsValid(UCk_Utils_Messaging_UE::TryGet_MessengerEntity(Owner, Message)));
    TestEqual(TEXT("endpoint existence does not prove callback"), Total(Keep), 0);
    TestEqual(TEXT("removed callback is absent"), Exact(Callback, Keep), 0);

    ck::UUtils_Signal_Messaging::Bind(Endpoint, Callback, Ignore,
        [](FCk_Handle, FGameplayTag, FInstancedStruct) { return false; });
    TestEqual(TEXT("conditional callback contributes to total"), Total(Keep), 1);
    TestEqual(TEXT("conditional replacement does not prove unconditional callback"), Exact(Callback, Keep), 0);
    UCk_Utils_Messaging_UE::UnbindFrom_OnBroadcast(Owner, Message, Callback);

    UCk_Utils_Messaging_UE::BindTo_OnBroadcast(Owner, Message, Callback, Ignore, Keep);
    auto NativeConnection = ck::UUtils_Signal_Messaging::Bind<&ck_messaging_binding_test::NativeMessage>(
        Endpoint, Ignore, Keep);
    TestEqual(TEXT("direct native listener is visible as an extra callback"), Total(Keep), 2);
    TestEqual(TEXT("native extra does not replace exact callback"), Exact(Callback, Keep), 1);
    NativeConnection.release();
    UCk_Utils_Messaging_UE::UnbindFrom_OnBroadcast(Owner, Message, Callback);

    UCk_Utils_Messaging_UE::BindTo_OnBroadcast(Owner, Message, Callback, Ignore, Once);
    TestEqual(TEXT("one-shot policy has one exact callback"), Exact(Callback, Once), 1);
    UCk_Utils_Messaging_UE::Broadcast(Owner, Message, {});
    TestEqual(TEXT("real production broadcast executes one-shot"), Probe->Received, 1);
    TestEqual(TEXT("one-shot is absent after real broadcast"), Total(Once), 0);
    TestEqual(TEXT("one-shot exact callback is absent after real broadcast"), Exact(Callback, Once), 0);

    UCk_Utils_Messaging_UE::BindTo_OnBroadcast(Owner, Message, Callback, Ignore, Keep);
    TestEqual(TEXT("rebind restores exact callback"), Exact(Callback, Keep), 1);
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Owner);
    TestEqual(TEXT("pending destruction rejects owner"), Total(Keep), 0);
    TestEqual(TEXT("pending destruction rejects exact callback"), Exact(Callback, Keep), 0);
    auto StaleOwner = FCk_Handle{};
    {
        auto RetiredWorld = ck::FEcsWorld{};
        StaleOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(RetiredWorld.Get_Registry());
        UCk_Utils_Messaging_UE::BindTo_OnBroadcast(StaleOwner, Message, Callback, Ignore, Keep);
    }
    TestEqual(TEXT("retired registry rejects pure lookup"),
        UCk_Utils_Messaging_UE::Get_NumBoundTo_OnBroadcast(StaleOwner, Message, Keep), 0);
    TestEqual(TEXT("retired registry rejects exact callback"),
        UCk_Utils_Messaging_UE::Get_NumUnconditionalBindingsTo_OnBroadcast(StaleOwner, Message, Callback, Keep), 0);
    TestEqual(TEXT("negative and pure checks caused no extra callback"), Probe->Received, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_MessagingBroadcastPayloadSafety,
    "Ck.Messaging.Broadcast.PayloadSafetyRejectsBeforeMutation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkTest_MessagingBroadcastPayloadSafety::RunTest(const FString&)
{
    auto EcsWorld = ck::FEcsWorld{};
    auto FreshOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(EcsWorld.Get_Registry());
    auto BoundOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(EcsWorld.Get_Registry());
    const auto Message = FGameplayTag{ck_messaging_binding_test::TAG_MessagingBindingCensus};
    auto Probe = TStrongObjectPtr<UCk_Test_MessagingBindingProbe>{NewObject<UCk_Test_MessagingBindingProbe>()};
    if (!TestTrue(TEXT("real owners and message available"), ck::IsValid(FreshOwner) &&
        ck::IsValid(BoundOwner) && Message.IsValid() && IsValid(Probe.Get()))) { return false; }
    auto Callback = FCk_Delegate_Messaging_OnBroadcast{};
    Callback.BindDynamic(Probe.Get(), &UCk_Test_MessagingBindingProbe::OnMessage);
    auto Other = FCk_Delegate_Messaging_OnBroadcast{};
    Other.BindDynamic(Probe.Get(), &UCk_Test_MessagingBindingProbe::OnOther);
    if (!TestFalse(TEXT("existing reflected Snapshot object fixture requires GC tracing"),
        ck::Analyze_UntracedStructSafety(FCk_Test_HydrationPayloadWithObject::StaticStruct()).IsGcIndependent()) ||
        !TestTrue(TEXT("existing reflected scalar/string fixture is safe"),
        ck::Analyze_UntracedStructSafety(FCk_Test_DynFrag_PureData::StaticStruct()).IsGcIndependent())) { return false; }
    FInstancedStruct Unsafe; Unsafe.InitializeAs(FCk_Test_HydrationPayloadWithObject::StaticStruct());
    FInstancedStruct Safe; Safe.InitializeAs(FCk_Test_DynFrag_PureData::StaticStruct());
    if (!TestTrue(TEXT("actual typed fixtures initialized"), Unsafe.IsValid() && Safe.IsValid())) { return false; }
    Safe.GetMutable<FCk_Test_DynFrag_PureData>().Count = 37;
    Safe.GetMutable<FCk_Test_DynFrag_PureData>().Label = TEXT("retained safe production payload");
#if !CK_DISABLE_ENSURE_CHECKS && !CK_DISABLE_ENSURE_DEBUGGING
    // Two deliberate public rejections, each reported by CkEnsure and CkEnsures.
    AddExpectedError(TEXT("rejected unsafe InstancedStruct payload"), EAutomationExpectedErrorFlags::Contains, 4);
#endif
    TestFalse(TEXT("fresh owner initially has no messenger record"), FreshOwner.Has<ck::FFragment_RecordOfMessengers>());
    UCk_Utils_Messaging_UE::Broadcast(FreshOwner, Message, Unsafe);
    TestFalse(TEXT("unsafe Broadcast never creates messenger record"), FreshOwner.Has<ck::FFragment_RecordOfMessengers>());
    TestFalse(TEXT("unsafe Broadcast never creates messenger entity"),
        ck::IsValid(UCk_Utils_Messaging_UE::TryGet_MessengerEntity(FreshOwner, Message)));
    TestEqual(TEXT("unsafe fresh dispatch executes no callback"), Probe->Received, 0);

    UCk_Utils_Messaging_UE::BindTo_OnBroadcast(BoundOwner, Message, Callback,
        ECk_Signal_BindingPolicy::IgnorePayloadInFlight, ECk_Signal_PostFireBehavior::DoNothing);
    const auto Endpoint = UCk_Utils_Messaging_UE::TryGet_MessengerEntity(BoundOwner, Message);
    if (!TestTrue(TEXT("real binding creates live endpoint"), ck::IsValid(Endpoint))) { return false; }
    UCk_Utils_Messaging_UE::Broadcast(BoundOwner, Message, Safe);
    TestEqual(TEXT("safe typed public Broadcast invokes original listener"), Probe->Received, 1);
    UCk_Utils_Messaging_UE::Broadcast(BoundOwner, Message, Unsafe);
    TestEqual(TEXT("unsafe public Broadcast executes no downstream listener"), Probe->Received, 1);
    TestTrue(TEXT("rejected broadcast preserves exact messenger"),
        UCk_Utils_Messaging_UE::TryGet_MessengerEntity(BoundOwner, Message) == Endpoint);
    TestEqual(TEXT("rejected broadcast preserves exact binding count"),
        UCk_Utils_Messaging_UE::Get_NumUnconditionalBindingsTo_OnBroadcast(BoundOwner, Message, Callback,
            ECk_Signal_PostFireBehavior::DoNothing), 1);
    bool SawRetainedSafe = false;
    // Real public signal binding observes retained dispatch; no signal storage/private mutation.
    ck::UUtils_Signal_Messaging::Bind(Endpoint, Other,
        ECk_Signal_BindingPolicy::FireIfPayloadInFlight,
        [&](FCk_Handle Owner, FGameplayTag Tag, FInstancedStruct Payload)
        {
            const auto* Value = Payload.GetPtr<FCk_Test_DynFrag_PureData>();
            SawRetainedSafe = Owner == BoundOwner && Tag == Message && Value && Value->Count == 37 &&
                Value->Label == TEXT("retained safe production payload");
            return false;
        });
    TestTrue(TEXT("rejection preserves complete safe retained production payload"), SawRetainedSafe);
    UCk_Utils_Messaging_UE::UnbindFrom_OnBroadcast(BoundOwner, Message, Other);
    UCk_Utils_Messaging_UE::Broadcast(BoundOwner, Message, {});
    TestEqual(TEXT("default empty payload still dispatches through public wrapper"), Probe->Received, 2);
    UCk_Utils_Messaging_UE::Broadcast(FreshOwner, Message, {});
    TestTrue(TEXT("default payload still creates a real messenger when missing"),
        ck::IsValid(UCk_Utils_Messaging_UE::TryGet_MessengerEntity(FreshOwner, Message)));
    return true;
}

#endif
