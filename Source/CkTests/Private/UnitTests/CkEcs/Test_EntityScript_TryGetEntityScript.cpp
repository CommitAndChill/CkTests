#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CkTryGetEntityScript_TestTypes.h"

#include "CkCore/Ensure/CkEnsure_Utils.h"
#include "CkCore/Macros/CkMacros.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/World/CkEcsWorld.h"

#include "UnitTests/CkGcWeakRef_TestBase.h"

#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_tryget_entityscript
{
    constexpr auto kTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_TryGetEntityScript_InvalidHandle,
    "Ck.EntityScript.TryGetEntityScript.InvalidHandleReturnsNull",
    ck_test_tryget_entityscript::kTestFlags)

bool FCkTest_TryGetEntityScript_InvalidHandle::RunTest(const FString& Parameters)
{
    const auto EnsureCountBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();

    TestNull(TEXT("an invalid handle has no script"), UCk_Utils_EntityScript_UE::TryGet_EntityScript(FCk_Handle{}));
    TestEqual(TEXT("an invalid handle is not an error"), UCk_Utils_Ensure_UE::Get_EnsureCount(), EnsureCountBefore);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_TryGetEntityScript_NoEntityScript,
    "Ck.EntityScript.TryGetEntityScript.EntityWithoutScriptReturnsNull",
    ck_test_tryget_entityscript::kTestFlags)

bool FCkTest_TryGetEntityScript_NoEntityScript::RunTest(const FString& Parameters)
{
    auto EcsWorld = ck::FEcsWorld{};
    const auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(EcsWorld.Get_Registry());
    const auto EnsureCountBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();

    TestNull(TEXT("an entity without an EntityScript has no script"), UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity));
    TestEqual(TEXT("an entity without an EntityScript is not an error"), UCk_Utils_Ensure_UE::Get_EnsureCount(), EnsureCountBefore);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_TryGetEntityScript_LiveScript,
    "Ck.EntityScript.TryGetEntityScript.LiveScriptIsReturned",
    ck_test_tryget_entityscript::kTestFlags)

bool FCkTest_TryGetEntityScript_LiveScript::RunTest(const FString& Parameters)
{
    auto EcsWorld = ck::FEcsWorld{};
    auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(EcsWorld.Get_Registry());

    const auto Script = TStrongObjectPtr{NewObject<UCk_TryGetEntityScriptTest_Instanced_UE>(GetTransientPackage())};
    Entity.Add<ck::FFragment_EntityScript>(Script.Get());

    TestEqual(TEXT("the entity's own script is returned"),
        UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity), static_cast<UCk_EntityScript_UE*>(Script.Get()));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_TryGetEntityScript_EndedPlay,
    "Ck.EntityScript.TryGetEntityScript.EndedPlayReturnsNull",
    ck_test_tryget_entityscript::kTestFlags)

bool FCkTest_TryGetEntityScript_EndedPlay::RunTest(const FString& Parameters)
{
    auto EcsWorld = ck::FEcsWorld{};
    auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(EcsWorld.Get_Registry());

    // The script is still alive, as it is when the pool has already re-issued it to another entity.
    const auto Script = TStrongObjectPtr{NewObject<UCk_TryGetEntityScriptTest_Instanced_UE>(GetTransientPackage())};
    Entity.Add<ck::FFragment_EntityScript>(Script.Get());
    Entity.Add<ck::FTag_EntityScript_HasEndedPlay>();

    const auto EnsureCountBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();

    TestNull(TEXT("an entity that ended play hands out no script"), UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity));
    TestEqual(TEXT("an entity that ended play is not an error"), UCk_Utils_Ensure_UE::Get_EnsureCount(), EnsureCountBefore);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(
    FCkTest_TryGetEntityScript_ScriptGoneBeforeEndPlay,
    FCkTest_GcWeakRefBase,
    "Ck.EntityScript.TryGetEntityScript.ScriptGoneBeforeEndPlayEnsures",
    ck_test_gc_weak_ref::kTestFlags)

bool FCkTest_TryGetEntityScript_ScriptGoneBeforeEndPlay::RunTest(const FString& Parameters)
{
    auto EcsWorld = ck::FEcsWorld{};
    auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(EcsWorld.Get_Registry());

    auto* Script = NewObject<UCk_TryGetEntityScriptTest_Instanced_UE>(GetTransientPackage());
    const auto WeakScript = TWeakObjectPtr<UCk_EntityScript_UE>{Script};
    Entity.Add<ck::FFragment_EntityScript>(Script);

    Script = nullptr;
    CollectGarbage(RF_NoFlags, true);

    if (NOT TestFalse(TEXT("a script held only by the fragment is collected"), WeakScript.IsValid()))
    { return false; }

    const auto EnsureCountBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();

    TestNull(TEXT("a collected script is not returned"), UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity));
    TestEqual(TEXT("losing the script before end play ensures exactly once"),
        UCk_Utils_Ensure_UE::Get_EnsureCount(), EnsureCountBefore + 1);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_TryGetEntityScript_NotInstanced,
    "Ck.EntityScript.TryGetEntityScript.NotInstancedIsRefused",
    ck_test_tryget_entityscript::kTestFlags)

bool FCkTest_TryGetEntityScript_NotInstanced::RunTest(const FString& Parameters)
{
    auto EcsWorld = ck::FEcsWorld{};
    auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(EcsWorld.Get_Registry());

    auto* SharedScript = GetMutableDefault<UCk_TryGetEntityScriptTest_NotInstanced_UE>();
    Entity.Add<ck::FFragment_EntityScript>(SharedScript);

    AddExpectedError(TEXT("a NotInstanced script is the archetype shared by every entity of its class"),
        EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/0);

    const auto EnsureCountBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();

    TestNull(TEXT("a NotInstanced script is never handed out"), UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity));
    TestEqual(TEXT("the refusal ensured exactly once"), UCk_Utils_Ensure_UE::Get_EnsureCount(), EnsureCountBefore + 1);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
