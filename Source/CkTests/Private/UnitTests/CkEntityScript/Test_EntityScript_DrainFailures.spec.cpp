#include "Test_EntityScript_DrainFailureProbe.h"
int32 UCk_Test_EntityScript_DrainFailureProbe_UE::ConstructCalls = 0;
auto UCk_Test_EntityScript_DrainFailureProbe_UE::Get_EffectiveReplication() const
    -> ECk_Replication { return ECk_Replication::DoesNotReplicate; }
auto UCk_Test_EntityScript_DrainFailureProbe_UE::Construct(
    FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    ++ConstructCalls;
    return ECk_EntityScript_ConstructionFlow::Finished;
}
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "../CkTest_CompletionListener.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_SpawnRecipe.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/Registry/CkRegistry.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include <Engine/World.h>
#include <Settings/LevelEditorPlaySettings.h>
#include <UObject/StrongObjectPtr.h>


namespace ck_entity_script_drain_failure_test
{
    struct FSettings
    {
        int32 Clients = 0;
        EPlayNetMode Mode = EPlayNetMode::PIE_Standalone;
        bool OneProcess = false;
        bool SeparateServer = false;
        FSettings()
        {
            const auto* S = GetDefault<ULevelEditorPlaySettings>();
            S->GetPlayNumberOfClients(Clients);
            S->GetPlayNetMode(Mode);
            S->GetRunUnderOneProcess(OneProcess);
            SeparateServer = S->bLaunchSeparateServer;
        }
        auto Matches() const -> bool
        {
            const auto S = FSettings{};
            return Clients == S.Clients && Mode == S.Mode &&
                OneProcess == S.OneProcess && SeparateServer == S.SeparateServer;
        }
    };
    struct FRun
    {
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> SpawnListener;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> AddListener;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> WorldListener;
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> RecoveryListener;
        FCk_Handle SpawnTarget, SpawnRequest, AddTarget, AddRequest;
        FCk_Handle WorldOwner, WorldTarget, WorldRequest, RecoveryTarget;
        FCk_Net_ConnectionSettings AddSettings;
        int32 AddPostCalls = 0;
    };
    auto Completion(UCk_Test_CompletionListener_UE* L) -> FCk_Delegate_Request_OnCompleted
    {
        auto D = FCk_Delegate_Request_OnCompleted{};
        D.BindDynamic(L, &UCk_Test_CompletionListener_UE::OnRequestCompleted);
        return D;
    }
    auto RequestFor(const FCk_Handle& Target) -> FCk_Handle
    {
        if (ck::Is_NOT_Valid(Target)) { return {}; }
        for (const auto& D : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Target))
        {
            if (ck::IsValid(D) && D.Has<ck::FFragment_EntityScript_RequestSpawnEntity>() &&
                D.Get<ck::FFragment_EntityScript_RequestSpawnEntity>().Get_NewEntity() == Target)
            { return D; }
        }
        return {};
    }
    auto CallerTargetClean(const FRun& R) -> bool
    {
        if (ck::Is_NOT_Valid(R.AddTarget) || UCk_Utils_EntityScript_UE::Has(R.AddTarget) ||
            R.AddTarget.Has<ck::FFragment_SpawnRecipe>() ||
            UCk_Utils_EntityReplicationDriver_UE::Has(R.AddTarget) ||
            !R.AddTarget.Has<ck::FFragment_Net_Params>() ||
            ck::IsValid(RequestFor(R.AddTarget))) { return false; }
        const auto& S = R.AddTarget.Get<ck::FFragment_Net_Params>().Get_ConnectionSettings();
        return S.Get_Replication() == R.AddSettings.Get_Replication() &&
            S.Get_NetMode() == R.AddSettings.Get_NetMode() &&
            S.Get_NetRole() == R.AddSettings.Get_NetRole();
    }
}
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkEntityScript_QueuedArchetypeLoss,
    "Ck.EntityScript.Admission.QueuedArchetypeLoss",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkEntityScript_QueuedArchetypeLoss::RunTest(const FString& Parameters)
{
    using namespace ck_entity_script_drain_failure_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("Unsupported topology")); return false; }
    const auto Before = FSettings{};
    const auto R = MakeShared<FRun>();
    R->SpawnListener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{NewObject<UCk_Test_CompletionListener_UE>(GetTransientPackage())};
    R->AddListener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{NewObject<UCk_Test_CompletionListener_UE>(GetTransientPackage())};
    R->RecoveryListener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{NewObject<UCk_Test_CompletionListener_UE>(GetTransientPackage())};
    if (!TestNotNull(TEXT("spawn listener"), R->SpawnListener.Get()) ||
        !TestNotNull(TEXT("Add listener"), R->AddListener.Get()) ||
        !TestNotNull(TEXT("recovery listener"), R->RecoveryListener.Get())) { return false; }
    AddExpectedErrorPlain(TEXT("EntityScriptArchetype ["), EAutomationExpectedErrorFlags::Contains, 2);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, R](UWorld* World)
        {
            if (!TestTrue(TEXT("server world"), IsValid(World))) { return; }
            UCk_Test_EntityScript_DrainFailureProbe_UE::ConstructCalls = 0;
            auto GoodOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
            if (!TestTrue(TEXT("real world owner"), ck::IsValid(GoodOwner))) { return; }
            const auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(GoodOwner);
            if (!TestTrue(TEXT("transient net settings"), ck::IsValid(Root) &&
                Root.Has<ck::FFragment_Net_Params>())) { return; }
            const auto& S = Root.Get<ck::FFragment_Net_Params>().Get_ConnectionSettings();
            R->AddSettings = FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
                S.Get_NetMode(), S.Get_NetRole()};
            auto* TempSpawn = NewObject<UCk_Test_EntityScript_DrainFailureProbe_UE>(GetTransientPackage());
            const auto PendingSpawn = UCk_Utils_EntityScript_UE::Request_SpawnEntity_Archetype(
                GoodOwner, TempSpawn, {}, Completion(R->SpawnListener.Get()));
            R->SpawnTarget = PendingSpawn.Get_EntityUnderConstruction();
            R->SpawnRequest = RequestFor(R->SpawnTarget);
            TestTrue(TEXT("temporary spawn archetype admitted"), ck::IsValid(PendingSpawn) &&
                ck::IsValid(R->SpawnRequest) && R->SpawnListener->_TimesRequestCompleted == 0);
            TempSpawn->MarkAsGarbage();
            R->AddTarget = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(GoodOwner);
            if (!TestTrue(TEXT("caller target"), ck::IsValid(R->AddTarget))) { return; }
            UCk_Utils_Net_UE::Add(R->AddTarget, R->AddSettings);
            auto* TempAdd = NewObject<UCk_Test_EntityScript_DrainFailureProbe_UE>(GetTransientPackage());
            const auto PendingAdd = UCk_Utils_EntityScript_UE::Add(
                R->AddTarget, TWeakObjectPtr<UCk_EntityScript_UE>{TempAdd}, {},
                [R](FCk_Handle) { ++R->AddPostCalls; }, Completion(R->AddListener.Get()));
            R->AddRequest = RequestFor(R->AddTarget);
            TestTrue(TEXT("temporary Add archetype admitted"), ck::IsValid(PendingAdd) &&
                ck::IsValid(R->AddRequest) && R->AddListener->_TimesRequestCompleted == 0);
            TempAdd->MarkAsGarbage();
            TestEqual(TEXT("no Construct before scheduler drain"),
                UCk_Test_EntityScript_DrainFailureProbe_UE::ConstructCalls, 0);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([R]() -> bool
        {
            return R->SpawnListener->_TimesRequestCompleted >= 1 &&
                R->AddListener->_TimesRequestCompleted >= 1 &&
                ck::Is_NOT_Valid(R->SpawnRequest) && ck::Is_NOT_Valid(R->AddRequest);
        }), 15.0, TEXT("queued archetype failures complete and requests drain")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, R]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            TestTrue(TEXT("each admitted failure completes exactly once"),
                R->SpawnListener->_TimesRequestCompleted == 1 &&
                R->AddListener->_TimesRequestCompleted == 1 &&
                R->SpawnListener->_LastRequestResult == ECk_Request_OperationResult::Failed &&
                R->AddListener->_LastRequestResult == ECk_Request_OperationResult::Failed);
            TestEqual(TEXT("no failed request called Construct"),
                UCk_Test_EntityScript_DrainFailureProbe_UE::ConstructCalls, 0);
            TestEqual(TEXT("no failed Add post-construction"), R->AddPostCalls, 0);
            TestTrue(TEXT("spawn-owned targets and requests fully retired"),
                ck::Is_NOT_Valid(R->SpawnTarget) && ck::Is_NOT_Valid(R->SpawnRequest));
            TestTrue(TEXT("caller target unchanged and script-free"),
                CallerTargetClean(*R) && ck::Is_NOT_Valid(R->AddRequest));
            return true;
        }), TEXT("queued rejection is atomic")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, R](UWorld* World)
        {
            if (!TestTrue(TEXT("server still live"), IsValid(World))) { return; }
            auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
            const auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                Owner, UCk_Test_EntityScript_DrainFailureProbe_UE::StaticClass(), {},
                Completion(R->RecoveryListener.Get()));
            R->RecoveryTarget = Pending.Get_EntityUnderConstruction();
            TestTrue(TEXT("unaffected owner queues valid follow-up"),
                ck::IsValid(Pending) && ck::IsValid(R->RecoveryTarget));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([R]() -> bool
        {
            return R->RecoveryListener->_TimesRequestCompleted == 1 &&
                R->RecoveryListener->_LastRequestResult == ECk_Request_OperationResult::Succeeded &&
                ck::IsValid(R->RecoveryTarget) &&
                UCk_Utils_EntityScript_UE::Has(R->RecoveryTarget);
        }), 15.0, TEXT("valid follow-up constructs")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, R]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            TestEqual(TEXT("only valid follow-up called Construct"),
                UCk_Test_EntityScript_DrainFailureProbe_UE::ConstructCalls, 1);
            TestTrue(TEXT("bad completions remain exact once"),
                R->SpawnListener->_TimesRequestCompleted == 1 &&
                R->AddListener->_TimesRequestCompleted == 1 &&
                R->AddPostCalls == 0 && CallerTargetClean(*R));
            return true;
        }), TEXT("no late failed-request mutation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Before]() -> bool
        {
            TestTrue(TEXT("PIE ended"), ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
            TestTrue(TEXT("play settings restored"), Before.Matches());
            return true;
        }), TEXT("queued-failure cleanup")));
    return true;
}
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkEntityScript_QueuedOwnerWorldLoss,
    "Ck.EntityScript.Admission.QueuedOwnerWorldLoss",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkEntityScript_QueuedOwnerWorldLoss::RunTest(const FString& Parameters)
{
    using namespace ck_entity_script_drain_failure_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("Unsupported topology")); return false; }
    const auto Before = FSettings{};
    const auto R = MakeShared<FRun>();
    R->WorldListener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{
        NewObject<UCk_Test_CompletionListener_UE>(GetTransientPackage())};
    R->RecoveryListener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{
        NewObject<UCk_Test_CompletionListener_UE>(GetTransientPackage())};
    if (!TestNotNull(TEXT("world-loss listener"), R->WorldListener.Get()) ||
        !TestNotNull(TEXT("recovery listener"), R->RecoveryListener.Get())) { return false; }
    AddExpectedErrorPlain(TEXT("Queued EntityScript owner has no safe live World chain"),
        EAutomationExpectedErrorFlags::Contains, 1);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, R](UWorld* World)
        {
            if (!TestTrue(TEXT("server world"), IsValid(World))) { return; }
            UCk_Test_EntityScript_DrainFailureProbe_UE::ConstructCalls = 0;
            auto Registry = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_RegistryForWorld(*World);
            if (!TestTrue(TEXT("real PIE registry"), ck::IsValid(Registry))) { return; }
            R->WorldOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
            if (!TestTrue(TEXT("dedicated registry owner"), ck::IsValid(R->WorldOwner)))
            { return; }
            R->WorldOwner.Add<ck::FFragment_LifetimeOwner>(R->WorldOwner);
            R->WorldOwner.Add<TWeakObjectPtr<UWorld>>(World);
            const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
            if (!TestTrue(TEXT("real transient net settings"), ck::IsValid(Root) &&
                Root.Has<ck::FFragment_Net_Params>())) { return; }
            const auto& S = Root.Get<ck::FFragment_Net_Params>().Get_ConnectionSettings();
            UCk_Utils_Net_UE::Add(R->WorldOwner, FCk_Net_ConnectionSettings{
                ECk_Replication::DoesNotReplicate, S.Get_NetMode(), S.Get_NetRole()});
            const auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                R->WorldOwner, UCk_Test_EntityScript_DrainFailureProbe_UE::StaticClass(),
                {}, Completion(R->WorldListener.Get()));
            R->WorldTarget = Pending.Get_EntityUnderConstruction();
            R->WorldRequest = RequestFor(R->WorldTarget);
            if (!TestTrue(TEXT("world-rooted request admitted before prerequisite loss"),
                ck::IsValid(Pending) && ck::IsValid(R->WorldRequest) &&
                R->WorldListener->_TimesRequestCompleted == 0)) { return; }
            R->WorldOwner.Remove<TWeakObjectPtr<UWorld>>();
            TestEqual(TEXT("no Construct before drain"),
                UCk_Test_EntityScript_DrainFailureProbe_UE::ConstructCalls, 0);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([R]() -> bool
        {
            return R->WorldListener->_TimesRequestCompleted >= 1 &&
                ck::Is_NOT_Valid(R->WorldRequest);
        }), 15.0, TEXT("world-loss request completes and drains")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, R]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            TestTrue(TEXT("world-loss callback exactly once as Failed"),
                R->WorldListener->_TimesRequestCompleted == 1 &&
                R->WorldListener->_LastRequestResult == ECk_Request_OperationResult::Failed);
            TestTrue(TEXT("spawn-owned world-loss target fully retired"),
                ck::Is_NOT_Valid(R->WorldTarget) && ck::Is_NOT_Valid(R->WorldRequest));
            TestEqual(TEXT("world-loss Construct never called"),
                UCk_Test_EntityScript_DrainFailureProbe_UE::ConstructCalls, 0);
            return true;
        }), TEXT("queued owner World loss is atomic")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, R](UWorld* World)
        {
            if (!TestTrue(TEXT("server still live"), IsValid(World))) { return; }
            auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
            const auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                Owner, UCk_Test_EntityScript_DrainFailureProbe_UE::StaticClass(), {},
                Completion(R->RecoveryListener.Get()));
            R->RecoveryTarget = Pending.Get_EntityUnderConstruction();
            TestTrue(TEXT("unaffected owner queues recovery"),
                ck::IsValid(Pending) && ck::IsValid(R->RecoveryTarget));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([R]() -> bool
        {
            return R->RecoveryListener->_TimesRequestCompleted == 1 &&
                R->RecoveryListener->_LastRequestResult ==
                    ECk_Request_OperationResult::Succeeded &&
                ck::IsValid(R->RecoveryTarget) &&
                UCk_Utils_EntityScript_UE::Has(R->RecoveryTarget);
        }), 15.0, TEXT("valid script recovers after owner World loss")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, R]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            TestEqual(TEXT("only recovery called Construct"),
                UCk_Test_EntityScript_DrainFailureProbe_UE::ConstructCalls, 1);
            TestEqual(TEXT("bad completion never repeated"),
                R->WorldListener->_TimesRequestCompleted, 1);
            return true;
        }), TEXT("world-loss recovery remains clean")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Before]() -> bool
        {
            TestTrue(TEXT("PIE ended"), ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
            TestTrue(TEXT("play settings restored"), Before.Matches());
            return true;
        }), TEXT("world-loss cleanup")));
    return true;
}
#endif
