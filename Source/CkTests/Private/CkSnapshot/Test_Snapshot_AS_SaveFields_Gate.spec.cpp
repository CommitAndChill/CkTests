// CkSnapshot 4B.3 AS SaveGame-field GATE — round-trips an AngelScript EntityScript's UPROPERTY(SaveGame)
// fields across a real v3 rebuild+hydrate load, and proves a non-SaveGame field is dropped (resets to default).
// Surfaces in Session Frontend: Ck.Snapshot.AS.SaveGameFields_RoundTrip,
// Ck.Snapshot.AS.NonSaveGameField_Drops, Ck.Snapshot.AS.SaveFields_MPServerTravel.
//
// Fixture: Plugins/CkTests/Script/CkSnapshot/CkSnapshot_AsSaveFields_EntityScript.as
// Framework handler under test: CkEcs FCk_SaveData_EntityScriptFields (Save-transport, reflect-walks CPF_SaveGame).

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "CkSnapshot_SaveCompletionProbe.h"

#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#include <InstancedStruct.h>

#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkGenericEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"

#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"

#include "CkTests/Net/CkAutoTest_NetSubject.h"
#include "CkTests/Net/CkAutoTest_NetSubject_M2aProbe.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_snapshot_as_savefields_gate
{
    // Fixture UPROPERTY names (see the .as fixture) + the values the gate mutates them to pre-save.
    constexpr auto FieldName_SavedInt    = TEXT("SavedInt");
    constexpr auto FieldName_SavedString = TEXT("SavedString");
    constexpr auto FieldName_SavedArray  = TEXT("SavedArray");
    constexpr auto FieldName_PlainInt    = TEXT("PlainInt");

    constexpr auto Mutated_Int    = 1234;
    const auto     Mutated_String = FString{TEXT("hydrated-value")};
    const auto     Mutated_Array  = TArray<int32>{7, 8, 9};
    constexpr auto Mutated_Plain  = 99;

    // ----------------------------------------------------------------------------------------------------------------
    // Reflection helpers — set/read the AS-defined UPROPERTYs by name.

    auto Has_SaveGameFlag(const UClass* InClass, const TCHAR* InName) -> bool
    {
        const auto* Prop = InClass->FindPropertyByName(FName{InName});
        return Prop != nullptr && Prop->HasAnyPropertyFlags(CPF_SaveGame);
    }

    auto Set_Int(UObject* InObj, const TCHAR* InName, int32 InValue) -> bool
    {
        auto* Prop = CastField<FIntProperty>(InObj->GetClass()->FindPropertyByName(FName{InName}));
        if (Prop == nullptr) { return false; }
        Prop->SetPropertyValue_InContainer(InObj, InValue);
        return true;
    }

    auto Get_Int(const UObject* InObj, const TCHAR* InName, int32& OutValue) -> bool
    {
        const auto* Prop = CastField<FIntProperty>(InObj->GetClass()->FindPropertyByName(FName{InName}));
        if (Prop == nullptr) { return false; }
        OutValue = Prop->GetPropertyValue_InContainer(InObj);
        return true;
    }

    auto Set_Str(UObject* InObj, const TCHAR* InName, const FString& InValue) -> bool
    {
        auto* Prop = CastField<FStrProperty>(InObj->GetClass()->FindPropertyByName(FName{InName}));
        if (Prop == nullptr) { return false; }
        Prop->SetPropertyValue_InContainer(InObj, InValue);
        return true;
    }

    auto Get_Str(const UObject* InObj, const TCHAR* InName, FString& OutValue) -> bool
    {
        const auto* Prop = CastField<FStrProperty>(InObj->GetClass()->FindPropertyByName(FName{InName}));
        if (Prop == nullptr) { return false; }
        OutValue = Prop->GetPropertyValue_InContainer(InObj);
        return true;
    }

    auto Set_IntArray(UObject* InObj, const TCHAR* InName, const TArray<int32>& InValues) -> bool
    {
        auto* ArrProp = CastField<FArrayProperty>(InObj->GetClass()->FindPropertyByName(FName{InName}));
        if (ArrProp == nullptr) { return false; }
        auto* Inner = CastField<FIntProperty>(ArrProp->Inner);
        if (Inner == nullptr) { return false; }
        auto* ArrPtr = ArrProp->ContainerPtrToValuePtr<void>(InObj);
        auto Helper = FScriptArrayHelper{ArrProp, ArrPtr};
        Helper.EmptyValues();
        for (const auto Value : InValues)
        {
            const auto Idx = Helper.AddValue();
            Inner->SetPropertyValue(Helper.GetRawPtr(Idx), Value);
        }
        return true;
    }

    auto Get_IntArray(UObject* InObj, const TCHAR* InName, TArray<int32>& OutValues) -> bool
    {
        auto* ArrProp = CastField<FArrayProperty>(InObj->GetClass()->FindPropertyByName(FName{InName}));
        if (ArrProp == nullptr) { return false; }
        auto* Inner = CastField<FIntProperty>(ArrProp->Inner);
        if (Inner == nullptr) { return false; }
        auto* ArrPtr = ArrProp->ContainerPtrToValuePtr<void>(InObj);
        auto Helper = FScriptArrayHelper{ArrProp, ArrPtr};
        OutValues.Reset();
        for (auto i = 0; i < Helper.Num(); ++i)
        { OutValues.Add(Inner->GetPropertyValue(Helper.GetRawPtr(i))); }
        return true;
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Resolution helpers.

    // Resolve the AS fixture UClass by name (mirrors the Gauntlet AS-class matcher: iterate UClasses, filter by
    // the EntityScript base, match the declared name bare or U-prefixed). Fails distinguishably from a name mismatch.
    auto Resolve_FixtureClass() -> UClass*
    {
        for (TObjectIterator<UClass> It; It; ++It)
        {
            auto* Cls = *It;
            if (Cls == nullptr || NOT Cls->IsChildOf(UCk_GenericEntityScript_UE::StaticClass()))
            { continue; }

            const auto Name = Cls->GetName();
            if (Name == TEXT("CkSnapshot_AsSaveFields_EntityScript")
                || Name == TEXT("UCkSnapshot_AsSaveFields_EntityScript"))
            { return Cls; }
        }
        return nullptr;
    }

    auto CurrentPIEWorld() -> UWorld*
    {
        for (auto* W : ck::auto_test::net::Get_AllPIEWorlds())
        { if (W != nullptr && W->HasBegunPlay()) { return W; } }
        return nullptr;
    }

    auto Subsystem(UWorld* InWorld) -> UCk_Snapshot_Subsystem_UE*
    {
        if (InWorld == nullptr || InWorld->GetGameInstance() == nullptr) { return nullptr; }
        return InWorld->GetGameInstance()->GetSubsystem<UCk_Snapshot_Subsystem_UE>();
    }

    auto ProbeEntity(UWorld* InWorld) -> FCk_Handle
    {
        auto* Subject = ACk_AutoTest_NetSubject::Find(InWorld);
        if (Subject == nullptr) { return {}; }
        return UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Subject);
    }

    // Walk the probe's lifetime dependents for the RuntimeSpawned fixture child; return its live script instance.
    // Get_LifetimeDependents is lazily pruned, so stale refs are filtered by validity + fragment presence.
    auto Resolve_FixtureScript(UWorld* InWorld, const UClass* InFixtureClass) -> UObject*
    {
        auto Owner = ProbeEntity(InWorld);
        if (ck::Is_NOT_Valid(Owner)) { return nullptr; }

        for (auto Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Owner))
        {
            if (ck::Is_NOT_Valid(Child) || NOT Child.Has<ck::FFragment_EntityScript>())
            { continue; }

            auto* Script = Child.Get<ck::FFragment_EntityScript>().Get_Script().Get();
            if (Script != nullptr && Script->GetClass() == InFixtureClass)
            { return Script; }
        }
        return nullptr;
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Shared save/load staging. FinalAssert runs after the load completes with the RELOADED fixture script (or null).

    auto Enqueue_SaveLoadCycle(
        FAutomationTestBase* InTest,
        const FName InSlotName,
        TFunction<void(FAutomationTestBase&, UObject* /*ReloadedScript*/)> InFinalAssert) -> void
    {
        FAutomationTestBase::bSuppressLogErrors   = true;
        FAutomationTestBase::bSuppressLogWarnings = true;

        constexpr auto NumPIEClients       = 1;
        constexpr auto ExpectedTotalWorlds = 1;
        constexpr auto ReadyTimeoutSeconds = 30.0f;
        constexpr auto FramesPerSettle     = 30;
        constexpr auto FramesForLoad       = 240; // teardown + OpenLevel + respawn + hydrate + quiescence span frames
        const auto     MapPath             = FString{TEXT("/Engine/Maps/Entry")};

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(NumPIEClients, MapPath));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(ExpectedTotalWorlds, ReadyTimeoutSeconds));

        // Stage 1 — spawn the bridged, snapshot-respawnable M2a probe (a persisted lifetime owner for the fixture).
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([InTest](UWorld* InServer) -> void
            {
                auto SpawnInfo = FActorSpawnParameters{};
                SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                auto* Subject = InServer->SpawnActor<ACk_AutoTest_NetSubject_M2aProbe>(
                    ACk_AutoTest_NetSubject_M2aProbe::StaticClass(), FTransform::Identity, SpawnInfo);
                if (Subject == nullptr)
                { InTest->AddError(TEXT("Stage 1: probe subject spawn returned null")); }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesPerSettle));

        // Stage 2 — spawn the AS fixture as a RuntimeSpawned child under the probe entity (persisted owner => the v3
        // loader respawns it and hydrates its SaveGame fields).
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([InTest](UWorld* InServer) -> void
            {
                auto* FixtureClass = Resolve_FixtureClass();
                if (FixtureClass == nullptr)
                {
                    InTest->AddError(TEXT("Stage 2: AS fixture class not found — "
                        "the .as failed to compile OR its UClass name does not match "
                        "'CkSnapshot_AsSaveFields_EntityScript'"));
                    return;
                }

                auto Owner = ProbeEntity(InServer);
                if (ck::Is_NOT_Valid(Owner))
                { InTest->AddError(TEXT("Stage 2: could not resolve probe entity")); return; }

                const auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner, FixtureClass, FInstancedStruct{}, {});
                if (NOT UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending))
                { InTest->AddError(TEXT("Stage 2: Request_SpawnEntity returned an invalid pending handle")); }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesPerSettle));

        // Stage 3 — precondition-assert the SaveGame flags took (separates "AS specifier inert" from "framework didn't
        // restore"), mutate all fields, then Save.
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([InTest, InSlotName](UWorld* InServer) -> void
            {
                auto* FixtureClass = Resolve_FixtureClass();
                if (FixtureClass == nullptr)
                { InTest->AddError(TEXT("Stage 3: AS fixture class not found")); return; }

                auto* Script = Resolve_FixtureScript(InServer, FixtureClass);
                if (Script == nullptr)
                { InTest->AddError(TEXT("Stage 3: fixture script child not found under probe pre-save")); return; }

                InTest->TestTrue(TEXT("SavedInt is CPF_SaveGame"),    Has_SaveGameFlag(FixtureClass, FieldName_SavedInt));
                InTest->TestTrue(TEXT("SavedString is CPF_SaveGame"), Has_SaveGameFlag(FixtureClass, FieldName_SavedString));
                InTest->TestTrue(TEXT("SavedArray is CPF_SaveGame"),  Has_SaveGameFlag(FixtureClass, FieldName_SavedArray));
                InTest->TestFalse(TEXT("PlainInt is NOT CPF_SaveGame"), Has_SaveGameFlag(FixtureClass, FieldName_PlainInt));

                Set_Int(Script, FieldName_SavedInt, Mutated_Int);
                Set_Str(Script, FieldName_SavedString, Mutated_String);
                Set_IntArray(Script, FieldName_SavedArray, Mutated_Array);
                Set_Int(Script, FieldName_PlainInt, Mutated_Plain);

                auto* Sub = Subsystem(InServer);
                if (Sub == nullptr) { InTest->AddError(TEXT("Stage 3: no snapshot subsystem")); return; }
                Sub->Request_Save(InSlotName, FCk_Delegate_OnSaveComplete{});
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesPerSettle));

        // Stage 4 — fire the async load.
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([InTest, InSlotName](UWorld* InServer) -> void
            {
                auto* Sub = Subsystem(InServer);
                if (Sub == nullptr) { InTest->AddError(TEXT("Stage 4: no snapshot subsystem")); return; }
                Sub->Request_Load(InSlotName, FCk_Delegate_OnLoadComplete{});
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesForLoad));

        // Stage 5 — assert load complete, re-resolve the RELOADED fixture, run the test-specific assertion.
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([InTest, InFinalAssert]() -> bool
            {
                auto* Server = CurrentPIEWorld();
                auto* Sub = Subsystem(Server);
                if (Sub == nullptr) { InTest->AddError(TEXT("Stage 5: no snapshot subsystem")); return false; }

                InTest->TestFalse(TEXT("load flag cleared after completion"), Sub->Get_IsLoadInProgress());

                auto* FixtureClass = Resolve_FixtureClass();
                auto* Reloaded = FixtureClass != nullptr ? Resolve_FixtureScript(Server, FixtureClass) : nullptr;
                InFinalAssert(*InTest, Reloaded);
                return true;
            }),
            TEXT("AS SaveGame gate: load complete + fixture reloaded + fields asserted")));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
            {
                FAutomationTestBase::bSuppressLogErrors   = false;
                FAutomationTestBase::bSuppressLogWarnings = false;
                return true;
            }),
            TEXT("AS SaveGame gate: restore log suppression statics")));
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkSnapshot_AS_SaveGameFields_RoundTrip,
    "Ck.Snapshot.AS.SaveGameFields_RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkSnapshot_AS_SaveGameFields_RoundTrip::RunTest(const FString& Parameters)
{
    using namespace ck_snapshot_as_savefields_gate;

    Enqueue_SaveLoadCycle(this, FName{TEXT("CkSnapshot_AsSaveFields_RoundTrip_Slot")},
        [](FAutomationTestBase& InTest, UObject* InReloaded) -> void
        {
            if (InReloaded == nullptr)
            { InTest.AddError(TEXT("RoundTrip: reloaded fixture script not found post-load")); return; }

            auto ReloadedInt = 0;
            auto ReloadedStr = FString{};
            auto ReloadedArr = TArray<int32>{};
            Get_Int(InReloaded, FieldName_SavedInt, ReloadedInt);
            Get_Str(InReloaded, FieldName_SavedString, ReloadedStr);
            Get_IntArray(InReloaded, FieldName_SavedArray, ReloadedArr);

            InTest.TestEqual(TEXT("SavedInt round-tripped"), ReloadedInt, Mutated_Int);
            InTest.TestEqual(TEXT("SavedString round-tripped"), ReloadedStr, Mutated_String);
            InTest.TestEqual(TEXT("SavedArray element count round-tripped"), ReloadedArr.Num(), Mutated_Array.Num());
            InTest.TestTrue(TEXT("SavedArray contents round-tripped"), ReloadedArr == Mutated_Array);
        });

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkSnapshot_AS_NonSaveGameField_Drops,
    "Ck.Snapshot.AS.NonSaveGameField_Drops",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkSnapshot_AS_NonSaveGameField_Drops::RunTest(const FString& Parameters)
{
    using namespace ck_snapshot_as_savefields_gate;

    Enqueue_SaveLoadCycle(this, FName{TEXT("CkSnapshot_AsSaveFields_Drops_Slot")},
        [](FAutomationTestBase& InTest, UObject* InReloaded) -> void
        {
            if (InReloaded == nullptr)
            { InTest.AddError(TEXT("Drops: reloaded fixture script not found post-load")); return; }

            auto ReloadedPlain = -1;
            auto ReloadedInt   = 0;
            Get_Int(InReloaded, FieldName_PlainInt, ReloadedPlain);
            Get_Int(InReloaded, FieldName_SavedInt, ReloadedInt);

            // The non-SaveGame field must NOT survive — it resets to the class default (0), not the mutated 99.
            InTest.TestEqual(TEXT("PlainInt dropped to class default (not the mutated value)"), ReloadedPlain, 0);
            // ...and a SaveGame field DID survive, proving selective drop rather than a wholesale-empty payload.
            InTest.TestEqual(TEXT("SavedInt survived (payload is real, drop is selective)"), ReloadedInt, Mutated_Int);
        });

    return true;
}


// Real connected-client load: the existing OpenLevel rows above retain their single-world contract.
namespace ck_snapshot_as_savefields_mp
{
    constexpr auto MapPath = TEXT("/Engine/Maps/Entry");
    const auto Slot = FName{TEXT("CkSnapshot_AsSaveFields_MP_Slot")};

    struct FTravelState
    {
        TWeakObjectPtr<UWorld> PreServer;
        TWeakObjectPtr<UWorld> PreClient;
        TStrongObjectPtr<UCkSnapshot_SaveCompletionProbe> SaveProbe;
        FString SavedSeamless;
        EConsoleVariableFlags SavedPriority = ECVF_Default;
        bool HasSeamless = false;

        ~FTravelState()
        { RestoreSeamless(); }

        void RestoreSeamless()
        {
            if (NOT HasSeamless) { return; }
            if (auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel")))
            {
                const auto CurrentPriority = static_cast<EConsoleVariableFlags>(CVar->GetFlags() & ECVF_SetByMask);
                CVar->Set(*SavedSeamless, CurrentPriority);
                CVar->SetFlags(static_cast<EConsoleVariableFlags>(
                    (CVar->GetFlags() & ~ECVF_SetByMask) | SavedPriority));
            }
            HasSeamless = false;
        }

        auto EnableSeamless() -> bool
        {
            auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel"));
            if (CVar == nullptr) { return false; }
            SavedSeamless = CVar->GetString();
            SavedPriority = static_cast<EConsoleVariableFlags>(CVar->GetFlags() & ECVF_SetByMask);
            HasSeamless = true;
            CVar->Set(1, ECVF_SetByCode);
            return CVar->GetInt() == 1;
        }
    };

    auto MapName(UWorld* InWorld) -> FString
    {
        return InWorld != nullptr ? InWorld->RemovePIEPrefix(InWorld->GetOutermost()->GetName()) : FString{};
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkSnapshot_AS_SaveFields_MPServerTravel,
    "Ck.Snapshot.AS.SaveFields_MPServerTravel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkSnapshot_AS_SaveFields_MPServerTravel::RunTest(const FString& Parameters)
{
    using namespace ck_snapshot_as_savefields_gate;
    using namespace ck_snapshot_as_savefields_mp;

    auto Dedicated = false;
    if (NOT ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("AS SaveFields MP: unsupported topology")); return false; }
    const auto ExpectedMode = Dedicated ? NM_DedicatedServer : NM_ListenServer;
    auto State = MakeShared<FTravelState>();
    State->SaveProbe.Reset(NewObject<UCkSnapshot_SaveCompletionProbe>(GetTransientPackage()));
    if (NOT State->SaveProbe.IsValid())
    { AddError(TEXT("AS SaveFields MP: cannot allocate save completion recorder")); return false; }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, FString{MapPath}, Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            if (NOT State->EnableSeamless())
            { AddError(TEXT("AS SaveFields MP: cannot enable PIE seamless travel")); return; }
            auto SpawnInfo = FActorSpawnParameters{};
            SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            if (InServer->SpawnActor<ACk_AutoTest_NetSubject_M2aProbe>(
                    ACk_AutoTest_NetSubject_M2aProbe::StaticClass(), FTransform::Identity, SpawnInfo) == nullptr)
            { AddError(TEXT("AS SaveFields MP: probe spawn failed")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            auto* FixtureClass = Resolve_FixtureClass();
            auto Owner = ProbeEntity(InServer);
            if (FixtureClass == nullptr || ck::Is_NOT_Valid(Owner))
            { AddError(TEXT("AS SaveFields MP: fixture class or probe entity missing")); return; }
            const auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                Owner, FixtureClass, FInstancedStruct{}, {});
            if (NOT UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending))
            { AddError(TEXT("AS SaveFields MP: fixture spawn rejected")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            auto* FixtureClass = Resolve_FixtureClass();
            auto* Script = FixtureClass != nullptr ? Resolve_FixtureScript(InServer, FixtureClass) : nullptr;
            auto* Sub = Subsystem(InServer);
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            if (Script == nullptr || Sub == nullptr || Client == nullptr)
            { AddError(TEXT("AS SaveFields MP: pre-save fixture, subsystem, or client missing")); return; }
            TestTrue(TEXT("SavedInt is CPF_SaveGame"), Has_SaveGameFlag(FixtureClass, FieldName_SavedInt));
            TestTrue(TEXT("SavedString is CPF_SaveGame"), Has_SaveGameFlag(FixtureClass, FieldName_SavedString));
            TestTrue(TEXT("SavedArray is CPF_SaveGame"), Has_SaveGameFlag(FixtureClass, FieldName_SavedArray));
            TestFalse(TEXT("PlainInt is not CPF_SaveGame"), Has_SaveGameFlag(FixtureClass, FieldName_PlainInt));
            if (NOT Set_Int(Script, FieldName_SavedInt, Mutated_Int) ||
                NOT Set_Str(Script, FieldName_SavedString, Mutated_String) ||
                NOT Set_IntArray(Script, FieldName_SavedArray, Mutated_Array) ||
                NOT Set_Int(Script, FieldName_PlainInt, Mutated_Plain))
            { AddError(TEXT("AS SaveFields MP: failed to mutate reflected fixture fields")); return; }
            State->PreServer = InServer;
            State->PreClient = Client;
            State->SaveProbe->Clear();
            auto Completion = FCk_Delegate_OnSaveComplete{};
            Completion.BindDynamic(State->SaveProbe.Get(), &UCkSnapshot_SaveCompletionProbe::OnSaveComplete);
            Sub->Request_Save(Slot, Completion);
        })));
    for (auto Cycle = 1; Cycle <= 2; ++Cycle)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
            { return State->SaveProbe.IsValid() && State->SaveProbe->HasFreshSuccess(); }),
            30.0, FString::Printf(TEXT("AS SaveFields MP cycle %d: fresh save callback reports Success exactly once"), Cycle)));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
            FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
                TestEqual(TEXT("AS SaveFields MP: save callback fires exactly once"), State->SaveProbe->CallbackCount, 1);
                TestTrue(TEXT("AS SaveFields MP: disk-write result is Success"),
                    State->SaveProbe->Result == ECk_SnapshotResult::Success);
                return State->SaveProbe->HasFreshSuccess();
            }), TEXT("AS SaveFields MP: only a newly successful disk save may be loaded")));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
            {
                if (NOT State->SaveProbe.IsValid() || NOT State->SaveProbe->HasFreshSuccess() ||
                    State->PreServer.Get() != InServer)
                { AddError(TEXT("AS SaveFields MP: fresh successful save required before load")); return; }
                auto* Sub = Subsystem(InServer);
                if (Sub == nullptr) { AddError(TEXT("AS SaveFields MP: pre-travel subsystem missing")); return; }
                Sub->Request_Load(Slot, FCk_Delegate_OnLoadComplete{});
                TestTrue(TEXT("AS SaveFields MP: Request_Load is asynchronous"), Sub->Get_IsLoadInProgress());
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
            FCk_NetAutoTest_Assertion::CreateLambda([State]() -> bool
            {
                auto* Server = ck::auto_test::net::Get_ServerWorld();
                auto* Client = ck::auto_test::net::Get_ClientWorld(0);
                if (Server == nullptr || Client == nullptr || Server == State->PreServer.Get() ||
                    Client == State->PreClient.Get() || NOT Server->HasBegunPlay() || NOT Client->HasBegunPlay() ||
                    MapName(Server) != MapPath || MapName(Client) != MapPath) { return false; }
                auto* Sub = Subsystem(Server);
                auto* FixtureClass = Resolve_FixtureClass();
                return Sub != nullptr && NOT Sub->Get_IsLoadInProgress() && FixtureClass != nullptr &&
                    Resolve_FixtureScript(Server, FixtureClass) != nullptr;
            }), 60.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
            FCk_NetAutoTest_Assertion::CreateLambda([this, ExpectedMode, State, Cycle]() -> bool
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
                auto* Server = ck::auto_test::net::Get_ServerWorld();
                auto* Client = ck::auto_test::net::Get_ClientWorld(0);
                if (Server == nullptr || Client == nullptr)
                { AddError(TEXT("AS SaveFields MP: post-travel server or client missing")); return false; }
                TestEqual(TEXT("AS SaveFields MP: exactly two post-travel worlds"),
                    ck::auto_test::net::Get_AllPIEWorlds().Num(), 2);
                TestTrue(TEXT("AS SaveFields MP: server world changed"), Server != State->PreServer.Get());
                TestTrue(TEXT("AS SaveFields MP: client reconnected"), Client != State->PreClient.Get());
                TestTrue(TEXT("AS SaveFields MP: server topology retained"), Server->GetNetMode() == ExpectedMode);
                TestTrue(TEXT("AS SaveFields MP: client mode retained"), Client->GetNetMode() == NM_Client);
                TestTrue(TEXT("AS SaveFields MP: destination server map"), MapName(Server) == MapPath);
                TestTrue(TEXT("AS SaveFields MP: destination client map"), MapName(Client) == MapPath);
                auto* FixtureClass = Resolve_FixtureClass();
                auto* Script = FixtureClass != nullptr ? Resolve_FixtureScript(Server, FixtureClass) : nullptr;
                if (Script == nullptr) { AddError(TEXT("AS SaveFields MP: reloaded script missing")); return false; }
                auto SavedInt = 0;
                auto PlainInt = -1;
                auto SavedString = FString{};
                auto SavedArray = TArray<int32>{};
                if (NOT Get_Int(Script, FieldName_SavedInt, SavedInt) ||
                    NOT Get_Int(Script, FieldName_PlainInt, PlainInt) ||
                    NOT Get_Str(Script, FieldName_SavedString, SavedString) ||
                    NOT Get_IntArray(Script, FieldName_SavedArray, SavedArray))
                { AddError(TEXT("AS SaveFields MP: reloaded reflected fields missing")); return false; }
                const auto Label = FString::Printf(TEXT("AS SaveFields MP cycle %d: "), Cycle);
                TestEqual(*(Label + TEXT("saved int round-trips")), SavedInt, Mutated_Int);
                TestEqual(*(Label + TEXT("saved string round-trips")), SavedString, Mutated_String);
                TestTrue(*(Label + TEXT("saved array round-trips")), SavedArray == Mutated_Array);
                TestEqual(*(Label + TEXT("non-SaveGame field resets to default")), PlainInt, 0);
                return true;
            }), TEXT("AS SaveFields MP: ServerTravel preserves SaveGame fields and drops PlainInt")));
        if (Cycle == 1)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
                {
                    auto* Sub = Subsystem(InServer);
                    auto* Client = ck::auto_test::net::Get_ClientWorld(0);
                    auto* FixtureClass = Resolve_FixtureClass();
                    auto* Script = FixtureClass != nullptr ? Resolve_FixtureScript(InServer, FixtureClass) : nullptr;
                    if (Sub == nullptr || Client == nullptr || Script == nullptr)
                    { AddError(TEXT("AS SaveFields MP: cycle-two save prerequisites missing")); return; }
                    if (NOT Set_Int(Script, FieldName_PlainInt, Mutated_Plain))
                    { AddError(TEXT("AS SaveFields MP: cannot re-mutate non-SaveGame field before second save")); return; }
                    State->PreServer = InServer;
                    State->PreClient = Client;
                    State->SaveProbe->Clear();
                    auto Completion = FCk_Delegate_OnSaveComplete{};
                    Completion.BindDynamic(State->SaveProbe.Get(), &UCkSnapshot_SaveCompletionProbe::OnSaveComplete);
                    Sub->Request_Save(Slot, Completion);
                })));
        }
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([State]() -> bool
        { State->SaveProbe.Reset(); State->RestoreSeamless(); return true; }),
        TEXT("AS SaveFields MP: release save callback recorder")));
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
