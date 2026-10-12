#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "../CkTest_CompletionListener.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor_Data.h"
#include "CkEcs/Handle/CkHandle_Utils.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "EngineUtils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_SpawnRecipe.h"
#include "CkEntityScriptGym_Runtime.h"
#include "HAL/PlatformTime.h"
#include <UObject/UnrealType.h>

#include <Engine/World.h>
#include <GameFramework/Actor.h>
#include <StructUtils/InstancedStruct.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StrongObjectPtr.h>
#include <UObject/UObjectGlobals.h>

namespace ck_entity_script_admission_test
{
    constexpr auto NonReplicatedPath =
        TEXT("/Script/Angelscript.Ck_EntityScript_EntityScriptGym_Spawn");
    constexpr auto ReplicatedPath =
        TEXT("/Script/Angelscript.Ck_EntityScript_EntityScriptGym_SpawnReplicated");
    constexpr auto ParamsPath =
        TEXT("/Script/Angelscript.EntityScriptGym_SpawnParams");

    struct FRun
    {
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> Listener;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> RecoveryListener;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> ExistingListener;
        TWeakObjectPtr<AActor> ExistingActor;
        FCk_Handle ExistingTarget;
        int32 ExistingPostCalls = 0;
        FCk_Handle RecoveryTarget;
        int32 PostConstructCalls = 0;
        int32 Rejections = 0;
    };

    auto Population(const FCk_Registry& Registry) -> int32
    {
        int32 Count = 0;
        Registry.View<ck::FTag_EntityJustCreated>().ForEach(
            [&Count](FCk_Entity) { ++Count; });
        return Count;
    }

    auto Completion(UCk_Test_CompletionListener_UE* Listener)
        -> FCk_Delegate_Request_OnCompleted
    {
        auto D = FCk_Delegate_Request_OnCompleted{};
        D.BindDynamic(Listener, &UCk_Test_CompletionListener_UE::OnRequestCompleted);
        return D;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkEntityScript_AdmissionGuards,
    "Ck.EntityScript.Admission.MalformedOwnersAndRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityScript_AdmissionGuards::RunTest(const FString& Parameters)
{
    using namespace ck_entity_script_admission_test;
    const auto Class = FSoftClassPath{NonReplicatedPath}.TryLoadClass<UCk_EntityScript_UE>();
    const auto RepClass = FSoftClassPath{ReplicatedPath}.TryLoadClass<UCk_EntityScript_UE>();
    auto* ParamsStruct = FindObject<UScriptStruct>(nullptr, ParamsPath);
    if (!TestNotNull(TEXT("unmodified authored nonreplicated script class"), Class) ||
        !TestNotNull(TEXT("unmodified authored replicated script class"), RepClass) ||
        !TestNotNull(TEXT("authored reflected spawn payload"), ParamsStruct))
    { return false; }
    const auto* Archetype = Class->GetDefaultObject<UCk_EntityScript_UE>();
    if (!TestNotNull(TEXT("authored class archetype"), Archetype) ||
        !TestTrue(TEXT("class CDO replication intent is retained"),
            RepClass->GetDefaultObject<UCk_EntityScript_UE>()->Get_EffectiveReplication()
                == ECk_Replication::Replicates))
    { return false; }
    const auto* WithActorDefault =
        UCk_EntityScript_WithActor_UE::StaticClass()->GetDefaultObject<UCk_EntityScript_WithActor_UE>();
    if (!TestNotNull(TEXT("native WithActor CDO"), WithActorDefault) ||
        !TestTrue(TEXT("unmodified WithActor CDO declares replication"),
            WithActorDefault->Get_EffectiveReplication() == ECk_Replication::Replicates))
    { return false; }
    FInstancedStruct Params;
    Params.InitializeAs(ParamsStruct);
    if (!TestTrue(TEXT("real typed spawn payload exists"), Params.IsValid()))
    { return false; }

    AddExpectedError(TEXT("EntityScript spawn owner has no safe live World chain"),
        EAutomationExpectedErrorFlags::Contains, 5);
    AddExpectedError(TEXT("EntityScript target has no safe live World chain"),
        EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("Replicated EntityScript owner has no net-mode source"),
        EAutomationExpectedErrorFlags::Contains, 3);
    AddExpectedError(TEXT("Replicated EntityScript target has no usable net mode"),
        EAutomationExpectedErrorFlags::Contains, 2);

    const auto Run = MakeShared<FRun>();
    Run->Listener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{
        NewObject<UCk_Test_CompletionListener_UE>(GetTransientPackage())};
    Run->RecoveryListener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{
        NewObject<UCk_Test_CompletionListener_UE>(GetTransientPackage())};
    Run->ExistingListener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{
        NewObject<UCk_Test_CompletionListener_UE>(GetTransientPackage())};
    if (!TestNotNull(TEXT("existing-target completion observer"), Run->ExistingListener.Get()))
    { return false; }
    if (!TestNotNull(TEXT("rejection completion observer"), Run->Listener.Get()) ||
        !TestNotNull(TEXT("recovery completion observer"), Run->RecoveryListener.Get()))
    { return false; }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, Run, Class, RepClass, Archetype, WithActorDefault, Params](UWorld* World)
            {
                if (!TestTrue(TEXT("real PIE server world exists"), IsValid(World)))
                { return; }
                ck::FEcsWorld Isolated;
                auto& Registry = Isolated.Get_Registry();
                auto RegistryOnly = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
                auto SelfOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
                auto CycleA = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
                auto CycleB = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
                auto WorldWithoutNet = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
                auto ForeignRoot = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
                auto ForeignChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
                auto UnknownNet = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
                auto InvalidNet = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
                if (!TestTrue(TEXT("isolated live malformed owners allocated"),
                    ck::IsValid(RegistryOnly) && ck::IsValid(SelfOwner) &&
                    ck::IsValid(CycleA) && ck::IsValid(CycleB) &&
                    ck::IsValid(WorldWithoutNet) && ck::IsValid(ForeignRoot) &&
                    ck::IsValid(ForeignChild) && ck::IsValid(UnknownNet) &&
                    ck::IsValid(InvalidNet))) { return; }
                auto* ClientWorld = ck::auto_test::net::Get_ClientWorld(0);
                if (!TestTrue(TEXT("foreign lower-owner world is distinct and live"),
                    IsValid(ClientWorld) && ClientWorld != World)) { return; }
                SelfOwner.Add<ck::FFragment_LifetimeOwner>(SelfOwner);
                CycleA.Add<ck::FFragment_LifetimeOwner>(CycleB);
                CycleB.Add<ck::FFragment_LifetimeOwner>(CycleA);
                CycleA.Add<TWeakObjectPtr<UWorld>>(World);
                UCk_Utils_Net_UE::Add(CycleA, FCk_Net_ConnectionSettings{
                    ECk_Replication::DoesNotReplicate,
                    ECk_Net_NetModeType::ClientAndHost,
                    ECk_Net_EntityNetRole::Authority});
                WorldWithoutNet.Add<ck::FFragment_LifetimeOwner>(WorldWithoutNet);
                WorldWithoutNet.Add<TWeakObjectPtr<UWorld>>(World);
                ForeignRoot.Add<ck::FFragment_LifetimeOwner>(ForeignRoot);
                ForeignRoot.Add<TWeakObjectPtr<UWorld>>(ClientWorld);
                ForeignChild.Add<ck::FFragment_LifetimeOwner>(ForeignRoot);
                ForeignChild.Add<TWeakObjectPtr<UWorld>>(World);
                UCk_Utils_Net_UE::Add(ForeignChild, FCk_Net_ConnectionSettings{
                    ECk_Replication::DoesNotReplicate,
                    ECk_Net_NetModeType::ClientAndHost,
                    ECk_Net_EntityNetRole::Authority});
                for (auto* Owner : {&UnknownNet, &InvalidNet})
                {
                    Owner->Add<ck::FFragment_LifetimeOwner>(*Owner);
                    Owner->Add<TWeakObjectPtr<UWorld>>(World);
                }
                UCk_Utils_Net_UE::Add(UnknownNet, FCk_Net_ConnectionSettings{
                    ECk_Replication::Replicates, ECk_Net_NetModeType::Unknown,
                    ECk_Net_EntityNetRole::Authority});
                UCk_Utils_Net_UE::Add(InvalidNet, FCk_Net_ConnectionSettings{
                    ECk_Replication::Replicates, static_cast<ECk_Net_NetModeType>(255),
                    ECk_Net_EntityNetRole::Authority});
                const int32 Baseline = Population(Registry);
                const auto D = Completion(Run->Listener.Get());

                const auto CheckRejected = [this, Run, &Registry, Baseline](
                    const TCHAR* Label, FCk_Handle_PendingEntityScript Pending)
                {
                    ++Run->Rejections;
                    TestFalse(Label, ck::IsValid(Pending));
                    TestEqual(TEXT("one synchronous completion per rejected attempt"),
                        Run->Listener->_TimesRequestCompleted, Run->Rejections);
                    TestTrue(TEXT("invalid admission returns Failed_NotEnqueued"),
                        Run->Listener->_LastRequestResult ==
                            ECk_Request_OperationResult::Failed_NotEnqueued);
                    TestEqual(TEXT("rejected request adds no registry population"),
                        Population(Registry), Baseline);
                };

                CheckRejected(TEXT("registry-only owner rejected before world lookup"),
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                        RegistryOnly, Class, Params, D));
                CheckRejected(TEXT("LoadRendezvous wrapper safely rejects registry-only owner"),
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity_LoadRendezvous(
                        RegistryOnly, Class, Params, D));
                CheckRejected(TEXT("self-owned no-World owner rejected without recursion"),
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity_Archetype(
                        SelfOwner, const_cast<UCk_EntityScript_UE*>(Archetype), Params, D));
                CheckRejected(TEXT("same-registry cycle rejected despite local World and Net_Params"),
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                        CycleA, Class, Params, D));
                CheckRejected(TEXT("lower-owner foreign World rejected despite local context"),
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                        ForeignChild, Class, Params, D));
                CheckRejected(TEXT("replicated script rejects world owner without net source"),
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                        WorldWithoutNet, RepClass, Params, D));
                CheckRejected(TEXT("replicated script rejects Unknown net mode"),
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                        UnknownNet, RepClass, Params, D));
                CheckRejected(TEXT("replicated script rejects invalid net mode enum"),
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                        InvalidNet, RepClass, Params, D));

                auto Target = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(CycleA);
                UCk_Utils_Net_UE::Add(Target, FCk_Net_ConnectionSettings{
                    ECk_Replication::DoesNotReplicate,
                    ECk_Net_NetModeType::ClientAndHost,
                    ECk_Net_EntityNetRole::Authority});
                const auto TargetName = Target.Get_DebugName();
                const int32 TargetPopulation = Population(Registry);
                const auto BeforeDependents =
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Target);
                const auto PendingAdd = UCk_Utils_EntityScript_UE::Add(
                    Target, TWeakObjectPtr<UCk_EntityScript_UE>{
                        const_cast<UCk_EntityScript_UE*>(Archetype)},
                    Params, [Run](FCk_Handle) { ++Run->PostConstructCalls; }, D);
                ++Run->Rejections;
                TestFalse(TEXT("direct Add rejects malformed owner despite target Net_Params"),
                    ck::IsValid(PendingAdd));
                TestEqual(TEXT("direct Add completes once"), Run->Listener->_TimesRequestCompleted,
                    Run->Rejections);
                TestTrue(TEXT("direct Add reports not enqueued"),
                    Run->Listener->_LastRequestResult ==
                        ECk_Request_OperationResult::Failed_NotEnqueued);
                TestTrue(TEXT("direct Add leaves caller-owned target live and script-free"),
                    ck::IsValid(Target) && !UCk_Utils_EntityScript_UE::Has(Target) &&
                    Target.Has<ck::FFragment_Net_Params>() &&
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Target) == CycleA &&
                    !Target.Has<ck::FFragment_EntityScript_RequestSpawnEntity>() &&
                    Target.Get_DebugName() == TargetName);
                TestEqual(TEXT("direct Add leaves request dependents unchanged"),
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Target).Num(),
                    BeforeDependents.Num());
                TestEqual(TEXT("direct Add leaves registry population unchanged"),
                    Population(Registry), TargetPopulation);
                TestEqual(TEXT("direct Add invokes no post-construction continuation"),
                    Run->PostConstructCalls, 0);

                const auto CheckDirectBadNet = [this, Run, &Registry, &WorldWithoutNet,
                    &Params, &D, RepClass](ECk_Net_NetModeType Mode, const TCHAR* Label)
                {
                    auto RawTarget = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
                    RawTarget.Add<ck::FFragment_LifetimeOwner>(WorldWithoutNet);
                    UCk_Utils_Net_UE::Add(RawTarget, FCk_Net_ConnectionSettings{
                        ECk_Replication::Replicates, Mode,
                        ECk_Net_EntityNetRole::Authority});
                    const auto Name = RawTarget.Get_DebugName();
                    const int32 Before = Population(Registry);
                    const auto Dependents =
                        UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(RawTarget).Num();
                    const auto Pending = UCk_Utils_EntityScript_UE::Add(
                        RawTarget, TWeakObjectPtr<UCk_EntityScript_UE>{
                            RepClass->GetDefaultObject<UCk_EntityScript_UE>()}, Params,
                        [Run](FCk_Handle) { ++Run->PostConstructCalls; }, D);
                    ++Run->Rejections;
                    TestFalse(Label, ck::IsValid(Pending));
                    TestEqual(TEXT("invalid own net mode completes exactly once"),
                        Run->Listener->_TimesRequestCompleted, Run->Rejections);
                    TestTrue(TEXT("invalid own net mode reports not-enqueued"),
                        Run->Listener->_LastRequestResult ==
                            ECk_Request_OperationResult::Failed_NotEnqueued);
                    TestTrue(TEXT("invalid own net mode preserves caller target"),
                        ck::IsValid(RawTarget) && RawTarget.Has<ck::FFragment_Net_Params>() &&
                        !UCk_Utils_EntityScript_UE::Has(RawTarget) &&
                        RawTarget.Get_DebugName() == Name &&
                        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(RawTarget) == WorldWithoutNet);
                    TestEqual(TEXT("invalid own net mode adds no request dependent"),
                        UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(RawTarget).Num(),
                        Dependents);
                    TestEqual(TEXT("invalid own net mode adds no registry population"),
                        Population(Registry), Before);
                    TestEqual(TEXT("invalid own net mode invokes no post-construction"),
                        Run->PostConstructCalls, 0);
                };
                CheckDirectBadNet(ECk_Net_NetModeType::Unknown,
                    TEXT("direct Add rejects replicated target with Unknown own net mode"));
                CheckDirectBadNet(static_cast<ECk_Net_NetModeType>(255),
                    TEXT("direct Add rejects replicated target with invalid own net mode"));

                auto GoodOwner =
                    UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
                if (!TestTrue(TEXT("real world owner remains admissible after rejections"),
                    ck::IsValid(GoodOwner) &&
                    UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(GoodOwner)))
                { return; }
                auto* Actor = World->SpawnActor<AActor>(AActor::StaticClass());
                if (!TestTrue(TEXT("real nonreplicated PIE actor spawned"),
                    IsValid(Actor) && !Actor->GetIsReplicated()))
                { return; }
                Run->ExistingActor = Actor;
                Run->ExistingTarget = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(GoodOwner);
                if (!TestTrue(TEXT("existing target allocated in real world"),
                    ck::IsValid(Run->ExistingTarget))) { return; }
                const auto Transient =
                    UCk_Utils_EntityLifetime_UE::Get_TransientEntity(GoodOwner);
                if (!TestTrue(TEXT("real PIE transient has net settings"),
                    ck::IsValid(Transient) && Transient.Has<ck::FFragment_Net_Params>()))
                { return; }
                const auto& OwnerSettings =
                    Transient.Get<ck::FFragment_Net_Params>().Get_ConnectionSettings();
                const auto SeededSettings = FCk_Net_ConnectionSettings{
                    ECk_Replication::DoesNotReplicate, OwnerSettings.Get_NetMode(),
                    OwnerSettings.Get_NetRole()};
                UCk_Utils_Net_UE::Add(Run->ExistingTarget, SeededSettings);
                const auto WithActorParams =
                    FInstancedStruct::Make<FCk_EntityScript_WithActor_SpawnParams>(Actor);
                const auto ExistingPending = UCk_Utils_EntityScript_UE::Add(
                    Run->ExistingTarget,
                    TWeakObjectPtr<UCk_EntityScript_UE>{
                        const_cast<UCk_EntityScript_WithActor_UE*>(WithActorDefault)},
                    WithActorParams,
                    [Run](FCk_Handle) { ++Run->ExistingPostCalls; },
                    Completion(Run->ExistingListener.Get()));
                TestTrue(TEXT("direct Add admits preseeded nonreplicated WithActor target"),
                    ck::IsValid(ExistingPending));
                const auto& AfterAdmission = Run->ExistingTarget.Get<ck::FFragment_Net_Params>()
                    .Get_ConnectionSettings();
                TestTrue(TEXT("direct Add preserves caller-seeded Net_Params"),
                    AfterAdmission.Get_Replication() == SeededSettings.Get_Replication() &&
                    AfterAdmission.Get_NetMode() == SeededSettings.Get_NetMode() &&
                    AfterAdmission.Get_NetRole() == SeededSettings.Get_NetRole());
                const auto RecoveryPending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                    GoodOwner, Class, Params, Completion(Run->RecoveryListener.Get()));
                Run->RecoveryTarget = RecoveryPending.Get_EntityUnderConstruction();
                TestTrue(TEXT("valid follow-up creates a real pending script request"),
                    ck::IsValid(Run->RecoveryTarget));
            })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            return Run->RecoveryListener->_TimesRequestCompleted == 1 &&
                Run->RecoveryListener->_LastRequestResult ==
                    ECk_Request_OperationResult::Succeeded &&
                ck::IsValid(Run->RecoveryTarget) &&
                UCk_Utils_EntityScript_UE::Has(Run->RecoveryTarget) &&
                Run->ExistingListener->_TimesRequestCompleted == 1 &&
                Run->ExistingListener->_LastRequestResult ==
                    ECk_Request_OperationResult::Succeeded &&
                ck::IsValid(Run->ExistingTarget) &&
                UCk_Utils_EntityScript_UE::Has(Run->ExistingTarget) &&
                Run->ExistingPostCalls == 1;
        }), 15.0, TEXT("valid script admission still constructs after malformed requests")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            return TestTrue(TEXT("rejected callbacks remain exact once after scheduler drain"),
                Run->Rejections == 11 && Run->Listener->_TimesRequestCompleted == 11 &&
                Run->Listener->_LastRequestResult ==
                    ECk_Request_OperationResult::Failed_NotEnqueued &&
                Run->PostConstructCalls == 0 &&
                Run->ExistingPostCalls == 1 &&
                Run->ExistingListener->_TimesRequestCompleted == 1 &&
                IsValid(Run->ExistingActor.Get()) &&
                !Run->ExistingActor->GetIsReplicated() &&
                ck::IsValid(Run->ExistingTarget) &&
                Run->ExistingTarget.Has<ck::FFragment_Net_Params>() &&
                Run->ExistingTarget.Get<ck::FFragment_Net_Params>()
                    .Get_ConnectionSettings().Get_Replication() ==
                    ECk_Replication::DoesNotReplicate);
        }), TEXT("no late callback or post-construction from rejected admissions")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}


namespace ck_spawn_authored_replication_test
{
    struct FPolicyRun
    {
        TWeakObjectPtr<AActor> Actor;
        FCk_Handle ActorRoot, DnrOwner;
        TArray<FCk_Handle> Targets;
        TArray<TWeakObjectPtr<AActor>> ReplicatedActors;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> RootListener;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> Listener;
        FCk_Net_ConnectionSettings WorldSettings, ExplicitOwnerSettings;
        UClass* ProbeClass = nullptr;
        FIntProperty* ProbeId = nullptr;
    };
    auto HasDriver(const FCk_Handle& H) -> bool
    {
        if (!ck::IsValid(H) || !H.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>()) { return false; }
        const auto Driver = H.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>();
        return IsValid(Driver.Get()) && Driver->Get_AssociatedEntity() == H;
    }
    auto Matches(const FCk_Handle& H, ECk_Replication Policy,
        const FCk_Net_ConnectionSettings& Source) -> bool
    {
        if (!ck::IsValid(H) || !H.Has<ck::FFragment_Net_Params>()) { return false; }
        const auto& S = H.Get<ck::FFragment_Net_Params>().Get_ConnectionSettings();
        const bool Authority = Source.Get_NetRole() == ECk_Net_EntityNetRole::Authority;
        const bool Host = Source.Get_NetMode() == ECk_Net_NetModeType::Host || Source.Get_NetMode() == ECk_Net_NetModeType::ClientAndHost;
        const bool Client = Source.Get_NetMode() == ECk_Net_NetModeType::Client || Source.Get_NetMode() == ECk_Net_NetModeType::ClientAndHost;
        return S.Get_Replication() == Policy && S.Get_NetMode() == Source.Get_NetMode() &&
            S.Get_NetRole() == Source.Get_NetRole() && H.Has<ck::FTag_HasAuthority>() == Authority &&
            H.Has<ck::FTag_NetMode_IsHost>() == Host && H.Has<ck::FTag_NetMode_IsClient>() == Client;
    }
    auto ProbeIdentity(const AActor* Actor, UClass* Class, const FIntProperty* Property) -> int32
    {
        return IsValid(Actor) && IsValid(Class) && Actor->IsA(Class) && Property
            ? Property->GetPropertyValue_InContainer(Actor) : INDEX_NONE;
    }
    auto DnrCount(UWorld* W, UClass* Class) -> int32
    {
        if (!IsValid(W) || !IsValid(Class)) { return INDEX_NONE; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
        if (!ck::IsValid(Root)) { return INDEX_NONE; }
        int32 Count = 0;
        Root.Get_RegistryView().View<ck::FFragment_EntityScript>().ForEach(
            [&Count, Class](FCk_Entity, const ck::FFragment_EntityScript& F)
            {
                const auto Script = F.Get_Script();
                if (Script.IsValid() && Script->GetClass() == Class) { ++Count; }
            });
        return Count;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkEntityScript_FreshAuthoredPolicy,
    "Ck.EntityScript.Admission.FreshAuthoredReplicationPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkEntityScript_FreshAuthoredPolicy::RunTest(const FString& Parameters)
{
    using namespace ck_entity_script_admission_test;
    using namespace ck_spawn_authored_replication_test;
    const auto Class = FSoftClassPath{NonReplicatedPath}.TryLoadClass<UCk_EntityScript_UE>();
    const auto RepClass = UCk_EntityScript_WithActor_UE::StaticClass();
    const auto ProbeClass = FSoftClassPath{TEXT("/Script/Angelscript.Ck_EntityScript_DriverOrderProbe")}.TryLoadClass<AActor>();
    auto* ProbeId = IsValid(ProbeClass) ? FindFProperty<FIntProperty>(ProbeClass, TEXT("ProbeId")) : nullptr;
    auto* Struct = FindObject<UScriptStruct>(nullptr, ParamsPath);
    if (!TestTrue(TEXT("actual authored opposite policies, actor probe, and payload"),
        IsValid(Class) && IsValid(RepClass) && IsValid(ProbeClass) && ProbeId &&
        ProbeId->HasAnyPropertyFlags(CPF_Net) && IsValid(Struct))) { return false; }
    auto* DnrCDO = Class->GetDefaultObject<UCk_EntityScript_UE>();
    auto* RepCDO = RepClass->GetDefaultObject<UCk_EntityScript_UE>();
    if (!TestTrue(TEXT("actual unmodified archetype replication policies"), IsValid(DnrCDO) && IsValid(RepCDO) &&
        DnrCDO->Get_EffectiveReplication() == ECk_Replication::DoesNotReplicate &&
        RepCDO->Get_EffectiveReplication() == ECk_Replication::Replicates)) { return false; }
    FInstancedStruct Params; Params.InitializeAs(Struct);
    if (!TestTrue(TEXT("actual safe typed params"), Params.IsValid())) { return false; }
    const auto Run = MakeShared<FPolicyRun>();
    Run->ProbeClass = ProbeClass; Run->ProbeId = ProbeId;
    Run->RootListener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{NewObject<UCk_Test_CompletionListener_UE>()};
    Run->Listener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{NewObject<UCk_Test_CompletionListener_UE>()};
    if (!TestTrue(TEXT("actual completion observers"), IsValid(Run->RootListener.Get()) && IsValid(Run->Listener.Get()))) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Run](UWorld* W)
        {
            auto Fail = [this](const TCHAR* M) { AddError(M); ck::auto_test::net::FailSession(M); };
            if (!IsValid(W)) { Fail(TEXT("policy server world missing")); return; }
            auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(W);
            const auto Transient = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
            if (!ck::IsValid(Owner) || !ck::IsValid(Transient) || !Transient.Has<ck::FFragment_Net_Params>())
            { Fail(TEXT("policy actual world owner/net settings missing")); return; }
            Run->WorldSettings = Transient.Get<ck::FFragment_Net_Params>().Get_ConnectionSettings();
            const auto ExpectedMode = W->GetNetMode() == NM_ListenServer ?
                ECk_Net_NetModeType::ClientAndHost : ECk_Net_NetModeType::Host;
            if ((W->GetNetMode() != NM_ListenServer && W->GetNetMode() != NM_DedicatedServer) ||
                Run->WorldSettings.Get_NetMode() != ExpectedMode ||
                Run->WorldSettings.Get_NetRole() != ECk_Net_EntityNetRole::Authority)
            { Fail(TEXT("policy actual authority world settings inconsistent")); return; }
            FActorSpawnParameters Spawn;
            auto* Actor = W->SpawnActor<AActor>(Run->ProbeClass, FTransform::Identity, Spawn);
            if (!IsValid(Actor)) { Fail(TEXT("policy actual actor allocation failed")); return; }
            Run->ProbeId->SetPropertyValue_InContainer(Actor, 1);
            Actor->ForceNetUpdate(); Run->Actor = Actor;
            const auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner,
                UCk_EntityScript_WithActor_UE::StaticClass(),
                FInstancedStruct::Make<FCk_EntityScript_WithActor_SpawnParams>(Actor), Completion(Run->RootListener.Get()));
            Run->ActorRoot = Pending.Get_EntityUnderConstruction();
            if (!ck::IsValid(Run->ActorRoot)) { Fail(TEXT("policy real actor-linked root admission failed")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([Run]()
    { return IsValid(Run->Actor.Get()) && Run->Actor->GetIsReplicated() &&
        Run->RootListener->_TimesRequestCompleted == 1 && Run->RootListener->_LastRequestResult == ECk_Request_OperationResult::Succeeded &&
        Matches(Run->ActorRoot, ECk_Replication::Replicates, Run->WorldSettings) &&
        UCk_Utils_EntityScript_UE::Has(Run->ActorRoot) && HasDriver(Run->ActorRoot); }),
        15.0, TEXT("actual authored actor-linked replicated root constructs with real driver")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Run, Class, RepClass, DnrCDO, RepCDO, Params](UWorld* W)
        {
            auto Fail = [this](const TCHAR* M) { AddError(M); ck::auto_test::net::FailSession(M); };
            if (!IsValid(W) || !Matches(Run->ActorRoot, ECk_Replication::Replicates, Run->WorldSettings)) { Fail(TEXT("policy actual root lost")); return; }
            // Keep this owner actor-free: WithActor acquires its own driver during Construct,
            // before self-owned metadata is published, rather than an ancestor actor's driver.
            Run->DnrOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(W);
            if (!ck::IsValid(Run->DnrOwner)) { Fail(TEXT("policy real child owner allocation failed")); return; }
            // Public explicit owner configuration, coherently differing from the actual world.
            auto& H = Run->DnrOwner;
            if (H.Has<ck::FFragment_Net_Params>()) { H.Remove<ck::FFragment_Net_Params>(); }
            if (H.Has<ck::FTag_HasAuthority>()) { H.Remove<ck::FTag_HasAuthority>(); }
            if (H.Has<ck::FTag_NetMode_IsHost>()) { H.Remove<ck::FTag_NetMode_IsHost>(); }
            if (H.Has<ck::FTag_NetMode_IsClient>()) { H.Remove<ck::FTag_NetMode_IsClient>(); }
            Run->ExplicitOwnerSettings = FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
                Run->WorldSettings.Get_NetMode() == ECk_Net_NetModeType::Host ? ECk_Net_NetModeType::ClientAndHost : ECk_Net_NetModeType::Host,
                ECk_Net_EntityNetRole::Proxy};
            UCk_Utils_Net_UE::Add(H, Run->ExplicitOwnerSettings);
            if (!Matches(H, ECk_Replication::DoesNotReplicate, Run->ExplicitOwnerSettings)) { Fail(TEXT("policy explicit owner config invalid")); return; }
            for (int32 I = 0; I < 4; ++I)
            {
                auto Owner = I % 2 == 0 ? Run->ActorRoot : Run->DnrOwner;
                auto ActualParams = Params;
                if (I % 2 != 0)
                {
                    FActorSpawnParameters Spawn;
                    auto* Actor = W->SpawnActor<AActor>(Run->ProbeClass, FTransform::Identity, Spawn);
                    if (!IsValid(Actor)) { Fail(TEXT("policy actual child actor allocation failed")); return; }
                    Run->ProbeId->SetPropertyValue_InContainer(Actor, I == 1 ? 2 : 3);
                    Actor->ForceNetUpdate();
                    Run->ReplicatedActors.Add(Actor);
                    ActualParams = FInstancedStruct::Make<FCk_EntityScript_WithActor_SpawnParams>(Actor);
                }
                const auto Pending = I < 2 ? UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner,
                    I == 0 ? Class : RepClass, ActualParams, Completion(Run->Listener.Get())) :
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity_Archetype(Owner,
                    I == 2 ? DnrCDO : RepCDO, ActualParams, Completion(Run->Listener.Get()));
                const auto Target = Pending.Get_EntityUnderConstruction(); Run->Targets.Add(Target);
                if (!Matches(Target, I % 2 == 0 ? ECk_Replication::DoesNotReplicate : ECk_Replication::Replicates, Run->WorldSettings) || HasDriver(Target) ||
                    !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Target) ||
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Target) != Owner)
                { Fail(TEXT("fresh target authored policy/world mode/role/tags wrong before construction")); return; }
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([Run, Class, RepClass]()
    {
        if (Run->Targets.Num() != 4 || Run->Listener->_TimesRequestCompleted != 4 ||
            Run->Listener->_LastRequestResult != ECk_Request_OperationResult::Succeeded) { return false; }
        for (int32 I = 0; I < 4; ++I)
        {
            const auto H = Run->Targets[I]; const bool Rep = I % 2 != 0;
            if (!Matches(H, Rep ? ECk_Replication::Replicates : ECk_Replication::DoesNotReplicate, Run->WorldSettings) ||
                !UCk_Utils_EntityScript_UE::Has(H) || HasDriver(H) != Rep ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H) != (Rep ? Run->DnrOwner : Run->ActorRoot) ||
                !IsValid(UCk_Utils_EntityScript_UE::TryGet_EntityScript(H))) { return false; }
            const auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
            if (Script->GetClass() != (Rep ? RepClass : Class)) { return false; }
        }
        if (!Matches(Run->ActorRoot, ECk_Replication::Replicates, Run->WorldSettings) ||
            !Matches(Run->DnrOwner, ECk_Replication::DoesNotReplicate, Run->ExplicitOwnerSettings)) { return false; }
        return true;
    }), 15.0, TEXT("all four authored class/archetype policies survive authority construction")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([Run, Class, RepClass]()
    {
        auto* Client = ck::auto_test::net::Get_ClientWorld(0);
        if (!IsValid(Client) || Run->ReplicatedActors.Num() != 2) { return false; }
        const auto ClientRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*Client);
        if (!ck::IsValid(ClientRoot)) { return false; }
        bool ProxyPolicies = true;
        TArray<bool> ReplicaFound{false, false};
        const auto Reg = ClientRoot.Get_RegistryView();
        Reg.View<ck::FFragment_EntityScript>().ForEach([&](FCk_Entity E, const ck::FFragment_EntityScript& F)
        {
            const auto Script = F.Get_Script();
            if (!Script.IsValid() || Script->GetClass() != RepClass) { return; }
            const auto* WithActor = Cast<UCk_EntityScript_WithActor_UE>(Script.Get());
            if (!IsValid(WithActor) || !IsValid(WithActor->Get_OwningActor())) { ProxyPolicies = false; return; }
            auto* Actor = WithActor->Get_OwningActor().Get();
            int32 Which = INDEX_NONE;
            for (int32 J = 0; J < Run->ReplicatedActors.Num(); ++J)
            {
                const auto ServerActor = Run->ReplicatedActors[J];
                if (!ServerActor.IsValid() ||
                    ProbeIdentity(ServerActor.Get(), Run->ProbeClass, Run->ProbeId) != (J == 0 ? 2 : 3))
                { ProxyPolicies = false; return; }
                if (ProbeIdentity(Actor, Run->ProbeClass, Run->ProbeId) == (J == 0 ? 2 : 3)) { Which = J; }
            }
            if (Which == INDEX_NONE) { return; }
            if (ReplicaFound[Which] || Actor->GetWorld() != Client || Actor->HasAuthority() || !Actor->GetIsReplicated())
            { ProxyPolicies = false; return; }
            ReplicaFound[Which] = true;
            const auto H = FCk_Handle{E, Reg.Get_RegistryHandle()};
            if (!Matches(H, ECk_Replication::Replicates, FCk_Net_ConnectionSettings{
                ECk_Replication::Replicates, ECk_Net_NetModeType::Client, ECk_Net_EntityNetRole::Proxy}) || !HasDriver(H))
            { ProxyPolicies = false; }
        });
        // Absence is meaningful only after both real actor-linked script replicas are ready.
        if (!ProxyPolicies || !ReplicaFound[0] || !ReplicaFound[1]) { return false; }
        if (DnrCount(Client, Class) != 0) { return false; }
        bool ActorArrived = false;
        for (TActorIterator<AActor> It(Client); It; ++It)
        { if (ProbeIdentity(*It, Run->ProbeClass, Run->ProbeId) == 1 &&
              It->GetIsReplicated() && !It->HasAuthority()) { ActorArrived = true; } }
        return ActorArrived;
    }), 15.0, TEXT("real probe actor and two actual Proxy replicas arrive with zero DNR originals")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

namespace ck_withactor_driver_order_test
{
    struct FRun
    {
        TWeakObjectPtr<AActor> RootActor;
        TArray<TWeakObjectPtr<AActor>> ChildActors;
        FCk_Handle Root, DnrOwner, Generic, ClassChild, ArchetypeChild;
        FCk_Handle Deferred;
        TArray<FCk_Handle> PolicyTargets, PolicyCaptured, DeferredCaptured;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> PolicyRejected, PolicyCancelled;
        FDelegateHandle PolicyTransformObserver;
        int32 RejectedConstructs = 0, RejectedPostCalls = 0;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> RootDone, OwnerDone, GenericDone, ChildrenDone;
        FCk_Net_ConnectionSettings WorldSettings;
        UClass* ProbeClass = nullptr;
        FIntProperty* ProbeId = nullptr;
    };

    auto CaptureAllocatedClosure(const FCk_Handle& Root, const FCk_Handle& Context,
        TArray<FCk_Handle>& Out) -> bool
    {
        Out.Empty();
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Root)) { return false; }
        Out.Add(Root);
        for (int32 I = 0; I < Out.Num(); ++I)
        {
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                Out[I], Context, Children)) { Out.Empty(); return false; }
            for (const auto& Child : Children)
            { if (!Out.Contains(Child)) { Out.Add(Child); } }
        }
        return true;
    }
    auto SameHandles(const TArray<FCk_Handle>& A, const TArray<FCk_Handle>& B) -> bool
    {
        if (A.Num() != B.Num()) { return false; }
        for (const auto& H : A) { if (!B.Contains(H)) { return false; } }
        return true;
    }
    class FPolicyAlwaysRestore final : public IAutomationLatentCommand
    {
        TSharedPtr<FRun> Run;
    public:
        explicit FPolicyAlwaysRestore(TSharedPtr<FRun> InRun) : Run(MoveTemp(InRun)) {}
        bool Update() override
        {
            if (Run.IsValid() && Run->PolicyTransformObserver.IsValid())
            {
                UCk_Utils_Transform_UE::Get_OnAdded().Remove(Run->PolicyTransformObserver);
                Run->PolicyTransformObserver.Reset();
            }
            return true;
        }
    };
    class FPolicyRetireTargets final : public IAutomationLatentCommand
    {
        TSharedPtr<FRun> Run;
    public:
        explicit FPolicyRetireTargets(TSharedPtr<FRun> InRun) : Run(MoveTemp(InRun)) {}
        bool Update() override
        {
            if (!Run.IsValid()) { return true; }
            for (auto H : Run->PolicyTargets)
            { if (ck::IsValid(H)) { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(H); } }
            if (ck::IsValid(Run->Deferred))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->Deferred); }
            return true;
        }
    };
    class FPolicyAwaitRetired final : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        TSharedPtr<FRun> Run;
        double Started = -1.0;
    public:
        FPolicyAwaitRetired(FAutomationTestBase* InTest, TSharedPtr<FRun> InRun) : Test(InTest), Run(MoveTemp(InRun)) {}
        bool Update() override
        {
            if (!Run.IsValid()) { Test->AddError(TEXT("policy cleanup observation lost")); return true; }
            if (Started < 0.0) { Started = FPlatformTime::Seconds(); }
            bool Gone = !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Run->Deferred);
            for (const auto& H : Run->PolicyTargets)
            { Gone = Gone && !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
            for (const auto& H : Run->PolicyCaptured)
            { Gone = Gone && !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
            if (Gone) { return true; }
            if (FPlatformTime::Seconds() - Started > 15.0)
            { Test->AddError(TEXT("all policy target/request allocations must retire before EndPIE, including failure cleanup")); return true; }
            return false;
        }
    };
    auto HasOwnDriver(const FCk_Handle& H, AActor* ExpectedOuter) -> bool
    {
        if (!ck::IsValid(H) || !H.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>())
        { return false; }
        auto* Driver = H.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
        return IsValid(Driver) && Driver->Get_AssociatedEntity() == H &&
            Driver->GetOuter() == ExpectedOuter;
    }
    auto Identity(const AActor* Actor, UClass* Class, const FIntProperty* Property) -> int32
    {
        return IsValid(Actor) && IsValid(Class) && Actor->IsA(Class) && Property
            ? Property->GetPropertyValue_InContainer(Actor) : INDEX_NONE;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkEntityScript_WithActorDriverOrder,
    "Ck.EntityScript.Admission.WithActorNestedDriverOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkEntityScript_WithActorDriverOrder::RunTest(const FString& Parameters)
{
    using namespace ck_entity_script_admission_test;
    using namespace ck_spawn_authored_replication_test;
    using namespace ck_withactor_driver_order_test;
    const auto DnrClass = FSoftClassPath{NonReplicatedPath}.TryLoadClass<UCk_EntityScript_UE>();
    const auto GenericClass = FSoftClassPath{ReplicatedPath}.TryLoadClass<UCk_EntityScript_UE>();
    const auto WithActorClass = UCk_EntityScript_WithActor_UE::StaticClass();
    const auto ProbeClass = FSoftClassPath{TEXT("/Script/Angelscript.Ck_EntityScript_DriverOrderProbe")}
        .TryLoadClass<AActor>();
    auto* ProbeId = IsValid(ProbeClass) ? FindFProperty<FIntProperty>(ProbeClass, TEXT("ProbeId")) : nullptr;
    auto* ParamsType = FindObject<UScriptStruct>(nullptr, ParamsPath);
    if (!TestTrue(TEXT("real nested classes and typed params"),
        IsValid(DnrClass) && IsValid(GenericClass) && IsValid(WithActorClass) &&
        IsValid(ProbeClass) && ProbeId && ProbeId->HasAnyPropertyFlags(CPF_Net) &&
        IsValid(ParamsType)))
    { return false; }
    auto* WithActorCDO = WithActorClass->GetDefaultObject<UCk_EntityScript_UE>();
    if (!TestTrue(TEXT("native driver timing policies"),
        IsValid(WithActorCDO) && WithActorCDO->Get_RequiresOwningActorBeforeReplicationDriver() &&
        !DnrClass->GetDefaultObject<UCk_EntityScript_UE>()->Get_RequiresOwningActorBeforeReplicationDriver() &&
        !GenericClass->GetDefaultObject<UCk_EntityScript_UE>()->Get_RequiresOwningActorBeforeReplicationDriver() &&
        WithActorCDO->Get_EffectiveReplication() == ECk_Replication::Replicates))
    { return false; }
    FInstancedStruct GenericParams; GenericParams.InitializeAs(ParamsType);
    if (!TestTrue(TEXT("real generic spawn params"), GenericParams.IsValid())) { return false; }
    const auto Run = MakeShared<ck_withactor_driver_order_test::FRun>();
    Run->ProbeClass = ProbeClass; Run->ProbeId = ProbeId;
    Run->RootDone = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{NewObject<UCk_Test_CompletionListener_UE>()};
    Run->OwnerDone = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{NewObject<UCk_Test_CompletionListener_UE>()};
    Run->GenericDone = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{NewObject<UCk_Test_CompletionListener_UE>()};
    Run->ChildrenDone = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{NewObject<UCk_Test_CompletionListener_UE>()};
    if (!TestTrue(TEXT("completion observers"), IsValid(Run->RootDone.Get()) &&
        IsValid(Run->OwnerDone.Get()) && IsValid(Run->GenericDone.Get()) && IsValid(Run->ChildrenDone.Get())))
    { return false; }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Run](UWorld* W)
        {
            auto Fail = [this](const TCHAR* M) { AddError(M); ck::auto_test::net::FailSession(M); };
            if (!IsValid(W)) { Fail(TEXT("nested server world missing")); return; }
            auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(W);
            const auto Transient = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
            if (!ck::IsValid(Owner) || !ck::IsValid(Transient) || !Transient.Has<ck::FFragment_Net_Params>())
            { Fail(TEXT("nested real world net owner missing")); return; }
            Run->WorldSettings = Transient.Get<ck::FFragment_Net_Params>().Get_ConnectionSettings();
            FActorSpawnParameters Spawn;
            auto* Actor = W->SpawnActor<AActor>(Run->ProbeClass, FTransform::Identity, Spawn);
            if (!IsValid(Actor)) { Fail(TEXT("nested root actor allocation failed")); return; }
            Run->ProbeId->SetPropertyValue_InContainer(Actor, 1);
            Actor->ForceNetUpdate(); Run->RootActor = Actor;
            auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner,
                UCk_EntityScript_WithActor_UE::StaticClass(),
                FInstancedStruct::Make<FCk_EntityScript_WithActor_SpawnParams>(Actor),
                Completion(Run->RootDone.Get()));
            Run->Root = Pending.Get_EntityUnderConstruction();
            if (!ck::IsValid(Run->Root)) { Fail(TEXT("nested actor root admission failed")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([Run]()
    { return Run->RootDone->_TimesRequestCompleted == 1 &&
        Run->RootDone->_LastRequestResult == ECk_Request_OperationResult::Succeeded &&
        HasOwnDriver(Run->Root, Run->RootActor.Get()) &&
        Matches(Run->Root, ECk_Replication::Replicates, Run->WorldSettings); }),
        15.0, TEXT("real replicated actor root is ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Run, DnrClass, GenericClass, GenericParams](UWorld* W)
        {
            auto Fail = [this](const TCHAR* M) { AddError(M); ck::auto_test::net::FailSession(M); };
            if (!IsValid(W) || !HasOwnDriver(Run->Root, Run->RootActor.Get()))
            { Fail(TEXT("nested actor root lost before children")); return; }
            auto OwnerPending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(Run->Root,
                DnrClass, GenericParams, Completion(Run->OwnerDone.Get()));
            Run->DnrOwner = OwnerPending.Get_EntityUnderConstruction();
            auto GenericPending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(Run->Root,
                GenericClass, GenericParams, Completion(Run->GenericDone.Get()));
            Run->Generic = GenericPending.Get_EntityUnderConstruction();
            if (!ck::IsValid(Run->DnrOwner) || !ck::IsValid(Run->Generic))
            { Fail(TEXT("nested DNR or ordinary replicated admission failed")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([Run]()
    { return Run->OwnerDone->_TimesRequestCompleted == 1 &&
        Run->OwnerDone->_LastRequestResult == ECk_Request_OperationResult::Succeeded &&
        Run->GenericDone->_TimesRequestCompleted == 1 &&
        Run->GenericDone->_LastRequestResult == ECk_Request_OperationResult::Succeeded &&
        Matches(Run->DnrOwner, ECk_Replication::DoesNotReplicate, Run->WorldSettings) &&
        !HasDriver(Run->DnrOwner) &&
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Run->DnrOwner) == Run->Root &&
        Matches(Run->Generic, ECk_Replication::Replicates, Run->WorldSettings) &&
        HasOwnDriver(Run->Generic, Run->RootActor.Get()) &&
        Run->Generic.Has<ck::FFragment_ContainerRef_Location>() &&
        Run->Generic.Has<ck::FFragment_ContainerRef_Rotation>() &&
        Run->Generic.Has<ck::FFragment_ContainerRef_Scale>(); }),
        15.0, TEXT("DNR intermediate and ordinary generic script construct; generic transform retains pre-Construct driver-dependent replication fragments")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Run, WithActorClass, WithActorCDO](UWorld* W)
        {
            auto Fail = [this](const TCHAR* M) { AddError(M); ck::auto_test::net::FailSession(M); };
            if (!IsValid(W) || !ck::IsValid(Run->DnrOwner))
            { Fail(TEXT("nested DNR owner unavailable")); return; }
            for (int32 I = 0; I < 2; ++I)
            {
                FActorSpawnParameters Spawn;
                auto* Actor = W->SpawnActor<AActor>(Run->ProbeClass, FTransform::Identity, Spawn);
                if (!IsValid(Actor)) { Fail(TEXT("nested child actor allocation failed")); return; }
                Run->ProbeId->SetPropertyValue_InContainer(Actor, I + 2);
                Actor->ForceNetUpdate(); Run->ChildActors.Add(Actor);
                const auto Params = FInstancedStruct::Make<FCk_EntityScript_WithActor_SpawnParams>(Actor);
                auto Pending = I == 0 ? UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                    Run->DnrOwner, WithActorClass, Params, Completion(Run->ChildrenDone.Get())) :
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity_Archetype(
                    Run->DnrOwner, WithActorCDO, Params, Completion(Run->ChildrenDone.Get()));
                auto Target = Pending.Get_EntityUnderConstruction();
                if (I == 0) { Run->ClassChild = Target; } else { Run->ArchetypeChild = Target; }
                if (!Matches(Target, ECk_Replication::Replicates, Run->WorldSettings) ||
                    !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Target) ||
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Target) != Run->DnrOwner || HasDriver(Target))
                { Fail(TEXT("nested WithActor target admission or exact parent wrong")); return; }
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([Run, WithActorClass, DnrClass]()
    {
        if (Run->ChildrenDone->_TimesRequestCompleted != 2 ||
            Run->ChildrenDone->_LastRequestResult != ECk_Request_OperationResult::Succeeded ||
            Run->ChildActors.Num() != 2) { return false; }
        const FCk_Handle Children[] = {Run->ClassChild, Run->ArchetypeChild};
        for (int32 I = 0; I < 2; ++I)
        {
            const auto H = Children[I];
            if (!Matches(H, ECk_Replication::Replicates, Run->WorldSettings) ||
                !HasOwnDriver(H, Run->ChildActors[I].Get()) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H) != Run->DnrOwner ||
                !UCk_Utils_EntityScript_UE::Has(H)) { return false; }
            auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
            if (!IsValid(Script) || Script->GetClass() != WithActorClass) { return false; }
            auto* Driver = H.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
            if (Driver->Get_ReplicationData_EntityScript().Get_OwningEntityDriver().Get() != Driver)
            { return false; }
        }
        auto* Client = ck::auto_test::net::Get_ClientWorld(0);
        if (!IsValid(Client)) { return false; }
        const auto ClientRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*Client);
        if (!ck::IsValid(ClientRoot)) { return false; }
        bool Found[2] = {false, false}; bool ValidReplicas = true;
        const auto Registry = ClientRoot.Get_RegistryView();
        Registry.View<ck::FFragment_EntityScript>().ForEach([&](FCk_Entity E, const ck::FFragment_EntityScript& F)
        {
            const auto Script = F.Get_Script();
            if (!Script.IsValid() || Script->GetClass() != WithActorClass) { return; }
            auto* WithActor = Cast<UCk_EntityScript_WithActor_UE>(Script.Get());
            if (!IsValid(WithActor) || !IsValid(WithActor->Get_OwningActor()))
            { ValidReplicas = false; return; }
            auto* Actor = WithActor->Get_OwningActor().Get();
            for (int32 I = 0; I < 2; ++I)
            {
                if (Identity(Actor, Run->ProbeClass, Run->ProbeId) == I + 2)
                {
                    auto H = FCk_Handle{E, Registry.Get_RegistryHandle()};
                    if (Found[I] || Actor->GetWorld() != Client || Actor->HasAuthority() ||
                        !Actor->GetIsReplicated() || !HasOwnDriver(H, Actor) ||
                        !Matches(H, ECk_Replication::Replicates, FCk_Net_ConnectionSettings{
                            ECk_Replication::Replicates, ECk_Net_NetModeType::Client,
                            ECk_Net_EntityNetRole::Proxy}) ||
                        !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H) ||
                        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H) != ClientRoot)
                    { ValidReplicas = false; }
                    else { Found[I] = true; }
                }
            }
        });
        bool RootArrived = false;
        for (TActorIterator<AActor> It(Client); It; ++It)
        { if (Identity(*It, Run->ProbeClass, Run->ProbeId) == 1 &&
              !It->HasAuthority() && It->GetIsReplicated()) { RootArrived = true; } }
        return ValidReplicas && Found[0] && Found[1] && RootArrived &&
            DnrCount(Client, DnrClass) == 0;
    }), 15.0, TEXT("nested class/archetype WithActor drivers use child actors; two real client proxies and self-owned wire metadata arrive without DNR original")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Run, GenericClass, GenericParams](UWorld* W)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            auto Fail = [this](const TCHAR* M) { AddError(M); ck::auto_test::net::FailSession(M); };
            if (!IsValid(W) || !HasOwnDriver(Run->Root, Run->RootActor.Get()) ||
                !ck::IsValid(Run->DnrOwner) || !IsValid(GenericClass))
            { Fail(TEXT("direct-owner policy production prerequisites lost")); return; }
            auto* Archetype = GenericClass->GetDefaultObject<UCk_EntityScript_UE>();
            if (!IsValid(Archetype) || Archetype->Get_EffectiveReplication() != ECk_Replication::Replicates ||
                Archetype->Get_RequiresOwningActorBeforeReplicationDriver() ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Run->DnrOwner) != Run->Root ||
                !Run->DnrOwner.Has<ck::FFragment_Net_Params>() ||
                Run->DnrOwner.Get<ck::FFragment_Net_Params>().Get_ConnectionSettings().Get_Replication() != ECk_Replication::DoesNotReplicate)
            { Fail(TEXT("direct-owner policy requires actual generic Rep under DNR below replicated grandparent")); return; }
            Run->PolicyRejected = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{NewObject<UCk_Test_CompletionListener_UE>()};
            Run->PolicyCancelled = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{NewObject<UCk_Test_CompletionListener_UE>()};
            if (!IsValid(Run->PolicyRejected.Get()) || !IsValid(Run->PolicyCancelled.Get()))
            { Fail(TEXT("direct-owner policy real completion listeners missing")); return; }
            Run->PolicyTransformObserver = UCk_Utils_Transform_UE::Get_OnAdded().AddLambda(
                [WeakRun=TWeakPtr<ck_withactor_driver_order_test::FRun>{Run}, GenericClass](const FCk_Handle_Transform& Added)
                {
                    const auto ObservedRun = WeakRun.Pin();
                    if (!ObservedRun.IsValid()) { return; }
                    const FCk_Handle H = Added;
                    if (!ck::IsValid(H)) { return; }
                    const auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
                    if (!IsValid(Script) || Script->GetClass() != GenericClass) { return; }
                    if (H == ObservedRun->Deferred || ObservedRun->PolicyTargets.Contains(H) ||
                        (H.Has<ck::FFragment_LifetimeOwner>() &&
                         H.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == ObservedRun->DnrOwner))
                    { ++ObservedRun->RejectedConstructs; }
                });
            const auto Context = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
            TArray<FCk_Handle> Before, After;
            if (!CaptureAllocatedClosure(Context, Context, Before))
            { Fail(TEXT("direct-owner policy initial allocated census rejected")); return; }
            const auto ClassPending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                Run->DnrOwner, GenericClass, GenericParams, Completion(Run->PolicyRejected.Get()));
            const auto ArchetypePending = UCk_Utils_EntityScript_UE::Request_SpawnEntity_Archetype(
                Run->DnrOwner, Archetype, GenericParams, Completion(Run->PolicyRejected.Get()));
            for (const auto Pending : {ClassPending, ArchetypePending})
            {
                TestFalse(TEXT("generic replicated spawn rejects DNR DIRECT owner without ancestor fallback"), ck::IsValid(Pending));
                if (ck::IsValid(Pending))
                {
                    const auto Target = Pending.Get_EntityUnderConstruction(); Run->PolicyTargets.Add(Target);
                    TArray<FCk_Handle> Captured;
                    if (!CaptureAllocatedClosure(Target,Context,Captured))
                    { Fail(TEXT("unexpected admitted policy target closure cannot be captured for cleanup")); return; }
                    for (const auto& H : Captured) { Run->PolicyCaptured.AddUnique(H); }
                }
            }
            if (!CaptureAllocatedClosure(Context, Context, After))
            { Fail(TEXT("direct-owner policy post-rejection census rejected")); return; }
            TestTrue(TEXT("class/archetype rejection allocates no target or request anywhere in actual world closure"), SameHandles(Before, After));
            TestEqual(TEXT("class/archetype rejections complete exactly once each immediately"), Run->PolicyRejected->_TimesRequestCompleted, 2);
            TestTrue(TEXT("class/archetype rejection is NotEnqueued"), Run->PolicyRejected->_LastRequestResult == ECk_Request_OperationResult::Failed_NotEnqueued);
            for (int32 I=0; I<2; ++I)
            {
                auto Owner = I==0 ? Run->Root : Run->DnrOwner;
                auto Target = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(W);
                if (!ck::IsValid(Target)) { Fail(TEXT("caller-owned policy target allocation failed")); return; }
                Run->PolicyTargets.Add(Target);
                if (Target.Has<ck::FFragment_Net_Params>())
                { Fail(TEXT("real fresh transient-owned target unexpectedly has inherited NetParams")); return; }
                const auto Seed = FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
                    Run->WorldSettings.Get_NetMode(),Run->WorldSettings.Get_NetRole()};
                UCk_Utils_Net_UE::Add(Target, Seed);
                UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Target,Owner);
                if (!Matches(Target,ECk_Replication::DoesNotReplicate,Seed) ||
                    !Target.Has<ck::FFragment_LifetimeOwner>() || Target.Get<ck::FFragment_LifetimeOwner>().Get_Entity()!=Owner)
                { Fail(TEXT("public seed and lifetime transfer must preserve actual caller policy")); return; }
                const auto Name = Target.Get_DebugName();
                if (!CaptureAllocatedClosure(Context,Context,Before)) { Fail(TEXT("caller target preflight census rejected")); return; }
                const auto Pending = UCk_Utils_EntityScript_UE::Add(Target,
                    TWeakObjectPtr<UCk_EntityScript_UE>{Archetype}, GenericParams,
                    [Run](FCk_Handle) { ++Run->RejectedPostCalls; },Completion(Run->PolicyRejected.Get()));
                TestFalse(TEXT("unsupported generic Rep on seeded DNR caller target rejects"),ck::IsValid(Pending));
                TestTrue(TEXT("caller target preserves actual seeded policy and debug/lifetime identity"),
                    Matches(Target,ECk_Replication::DoesNotReplicate,Seed) && Target.Get_DebugName()==Name &&
                    Target.Has<ck::FFragment_LifetimeOwner>() && Target.Get<ck::FFragment_LifetimeOwner>().Get_Entity()==Owner &&
                    !UCk_Utils_EntityScript_UE::Has(Target) && !HasDriver(Target) && !Target.Has<ck::FFragment_SpawnRecipe>());
                if (!CaptureAllocatedClosure(Context,Context,After)) { Fail(TEXT("caller target postflight census rejected")); return; }
                TestTrue(TEXT("rejected direct Add creates no request/allocation"),SameHandles(Before,After));
                TArray<FCk_Handle> Captured;
                if (!CaptureAllocatedClosure(Target,Context,Captured))
                { Fail(TEXT("caller-owned policy target closure cannot be captured for cleanup")); return; }
                for (const auto& H : Captured) { Run->PolicyCaptured.AddUnique(H); }
            }
            TestEqual(TEXT("four policy rejections each completed once before drain"),Run->PolicyRejected->_TimesRequestCompleted,4);
            TestTrue(TEXT("all direct policy rejection outcomes are NotEnqueued"),Run->PolicyRejected->_LastRequestResult==ECk_Request_OperationResult::Failed_NotEnqueued);
            auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(Run->Root,GenericClass,GenericParams,
                Completion(Run->PolicyCancelled.Get()));
            Run->Deferred=Pending.Get_EntityUnderConstruction();
            if (!ck::IsValid(Run->Deferred) || !CaptureAllocatedClosure(Run->Deferred,Context,Run->DeferredCaptured))
            { Fail(TEXT("deferred policy regression must admit actual target/request before real ownership transfer")); return; }
            for (const auto& H : Run->DeferredCaptured) { Run->PolicyCaptured.AddUnique(H); }
            TestTrue(TEXT("actual queued target captured before transfer has request child"),Run->DeferredCaptured.Num()>=2);
            UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Run->Deferred,Run->DnrOwner);
            TestTrue(TEXT("real transfer changes DIRECT parent before normal processor drain"),
                Run->Deferred.Has<ck::FFragment_LifetimeOwner>() && Run->Deferred.Get<ck::FFragment_LifetimeOwner>().Get_Entity()==Run->DnrOwner);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,FCk_NetAutoTest_Condition::CreateLambda([Run]()
    {
        if (!IsValid(Run->PolicyCancelled.Get()) || Run->PolicyCancelled->_TimesRequestCompleted!=1) { return false; }
        for (const auto& H : Run->DeferredCaptured)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return false; } }
        return true;
    }),15.0,TEXT("real queued direct-owner transfer cancels and retires all captured target/request allocations")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,FCk_NetAutoTest_Assertion::CreateLambda([this,Run]()
    {
        if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
        bool Clean=IsValid(Run->PolicyRejected.Get()) && IsValid(Run->PolicyCancelled.Get()) &&
            Run->PolicyRejected->_TimesRequestCompleted==4 && Run->PolicyRejected->_LastRequestResult==ECk_Request_OperationResult::Failed_NotEnqueued &&
            Run->PolicyCancelled->_TimesRequestCompleted==1 && Run->PolicyCancelled->_LastRequestResult==ECk_Request_OperationResult::Failed_Cancelled &&
            Run->RejectedConstructs==0 && Run->RejectedPostCalls==0 && ck::IsValid(Run->Root) && ck::IsValid(Run->DnrOwner);
        for (const auto& H : Run->PolicyTargets)
        { Clean=Clean && ck::IsValid(H) && !UCk_Utils_EntityScript_UE::Has(H) && !HasDriver(H) && !H.Has<ck::FFragment_SpawnRecipe>(); }
        for (const auto& H : Run->DeferredCaptured)
        { Clean=Clean && !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
        return TestTrue(TEXT("no generic Construct/driver/recipe/post callback or late outcome from rejected direct-owner policies; owners survive"),Clean);
    }),TEXT("exact policy outcomes and no construction mutation")));
    ADD_LATENT_AUTOMATION_COMMAND(FPolicyAlwaysRestore(Run));
    ADD_LATENT_AUTOMATION_COMMAND(FPolicyRetireTargets(Run));
    ADD_LATENT_AUTOMATION_COMMAND(FPolicyAwaitRetired(this,Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
#endif
