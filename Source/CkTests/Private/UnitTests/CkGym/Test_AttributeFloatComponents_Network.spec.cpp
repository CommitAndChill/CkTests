#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkGym_StartupSettings.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkAttribute/FloatAttribute/CkFloatAttribute_Utils.h"

#include <Engine/World.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/PlayerController.h>
#include <GameplayTagContainer.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>
#include <UObject/UObjectGlobals.h>

namespace ck_attribute_float_component_test
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_IntegerAttributeGym_GameMode");
    constexpr auto ProbePath = TEXT("/Script/Angelscript.Ck_FloatFeatureComponent_ASProbe");

    struct FFixture
    {
        ECkGym_StartupMode OldMode = ECkGym_StartupMode::Cycler;
        FString OldLast;
        TArray<FString> OldRecents;
        FCk_Handle Owner, Attribute, Refill, Plain, Partial, Child;
        FCk_Handle Modifiers[3], Stale;
        bool Created = false;

        void CaptureSettings()
        {
            const auto* Settings = GetDefault<UCkGym_StartupSettings>();
            OldMode = Settings->StartupMode;
            OldLast = Settings->LastGymName;
            OldRecents = Settings->RecentGymNames;
        }
        void RestoreSettings() const
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = OldMode;
            Settings->LastGymName = OldLast;
            Settings->RecentGymNames = OldRecents;
            Settings->SaveConfig();
        }
    };

    auto Live(const FCk_Handle& H) -> bool
    {
        return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
            H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    auto Create(UWorld* World, FFixture& F) -> bool
    {
        if (!IsValid(World) || World != ck::auto_test::net::Get_ServerWorld()) { return false; }
        const auto Name = FGameplayTag::RequestGameplayTag(FName(TEXT("FloatAttribute.Energy")), false);
        const auto RefillName = FGameplayTag::RequestGameplayTag(
            FName(TEXT("FloatAttribute.Energy.Refill")), false);
        const auto PlainName = FGameplayTag::RequestGameplayTag(
            FName(TEXT("FloatAttribute.Mana")), false);
        const auto PartialName = FGameplayTag::RequestGameplayTag(
            FName(TEXT("FloatAttribute.Mana.Refill")), false);
        const TCHAR* ModifierNames[4] = {TEXT("Modifier.Weapon"), TEXT("Modifier.Buff"),
            TEXT("Modifier.Armor"), TEXT("Modifier.Shield")};
        FGameplayTag ModifierTags[4];
        for (int32 I = 0; I < 4; ++I)
        {
            ModifierTags[I] = FGameplayTag::RequestGameplayTag(FName(ModifierNames[I]), false);
            if (!ModifierTags[I].IsValid()) { return false; }
        }
        if (!Name.IsValid() || !RefillName.IsValid() || !PlainName.IsValid() ||
            !PartialName.IsValid()) { return false; }
        F.Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
        if (!Live(F.Owner)) { return false; }
        F.Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(F.Owner);
        if (!Live(F.Child)) { return false; }
        auto RefillSpec = FCk_FloatAttributeRefill_Spec{RefillName, 5.0f};
        RefillSpec.Set_StartingState(ECk_Attribute_RefillState::Paused);
        auto Spec = FCk_FloatAttribute_Spec{Name, 50.0f};
        Spec.Set_MinMax(ECk_MinMax::MinMax);
        Spec.Set_MinValue(0.0f); Spec.Set_MaxValue(100.0f);
        Spec.Set_EnableRefill(true).Set_RefillParams(RefillSpec);
        auto Attribute = UCk_Utils_FloatAttribute_UE::Add(
            F.Owner, Spec, ECk_Replication::DoesNotReplicate);
        F.Attribute = FCk_Handle{Attribute};
        F.Refill = FCk_Handle{UCk_Utils_FloatAttribute_UE::TryGet_RefillAttribute(Attribute)};
        F.Plain = FCk_Handle{UCk_Utils_FloatAttribute_UE::Add(F.Owner,
            FCk_FloatAttribute_Spec{PlainName, 20.0f}, ECk_Replication::DoesNotReplicate)};
        F.Partial = FCk_Handle{UCk_Utils_FloatAttribute_UE::Add(F.Owner,
            FCk_FloatAttribute_Spec{PartialName, 2.0f}, ECk_Replication::DoesNotReplicate)};
        if (!Live(F.Attribute) || !Live(F.Refill) || !Live(F.Plain) || !Live(F.Partial) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(F.Attribute) != F.Owner ||
            !UCk_Utils_FloatAttribute_UE::Has_Component(Attribute, ECk_MinMaxCurrent::Current))
        { return false; }
        F.Partial.Add<ck::FTag_IsRefillAttribute>();
        F.Partial.Try_Remove<ck::FFragment_FloatAttribute>();
        const ECk_MinMaxCurrent Components[4] = {ECk_MinMaxCurrent::Current,
            ECk_MinMaxCurrent::Min, ECk_MinMaxCurrent::Max, ECk_MinMaxCurrent::Current};
        for (int32 I = 0; I < 4; ++I)
        {
            const auto Mod = UCk_Utils_FloatAttributeModifier_UE::Add_Revocable(
                Attribute, ModifierTags[I], ECk_AttributeModifier_Operation::Add,
                FCk_FloatAttributeModifier_Spec{0.0f, Components[I]});
            const auto H = FCk_Handle{Mod};
            if (I < 3) { F.Modifiers[I] = H; } else { F.Stale = H; }
            if (!Live(H) || UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H) != F.Attribute)
            { return false; }
        }
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F.Stale);
        return true;
    }

    auto Schema(UClass* NativeClass, UFunction*& ModifierFn, UFunction*& RefillFn,
        UFunction*& ProbeFn, UClass*& ProbeClass) -> bool
    {
        ModifierFn = NativeClass->FindFunctionByName(TEXT("Has_Component"));
        RefillFn = UCk_Utils_FloatAttributeRefill_UE::StaticClass()->FindFunctionByName(
            TEXT("Has_Refill"));
        ProbeClass = FSoftClassPath{ProbePath}.TryLoadClass<UObject>();
        ProbeFn = IsValid(ProbeClass) ? ProbeClass->FindFunctionByName(TEXT("Probe")) : nullptr;
        const auto* ModHandle = ModifierFn ? FindFProperty<FStructProperty>(ModifierFn,
            TEXT("InModifierEntity")) : nullptr;
        const auto* ModResult = ModifierFn ? FindFProperty<FBoolProperty>(ModifierFn,
            TEXT("ReturnValue")) : nullptr;
        const auto* ModEnum = ModifierFn ? FindFProperty<FEnumProperty>(ModifierFn,
            TEXT("InComponent")) : nullptr;
        const auto* ModByte = ModifierFn ? FindFProperty<FByteProperty>(ModifierFn,
            TEXT("InComponent")) : nullptr;
        const auto* RefillHandle = RefillFn ? FindFProperty<FStructProperty>(RefillFn,
            TEXT("InAttributeRefillEntity")) : nullptr;
        const auto* RefillResult = RefillFn ? FindFProperty<FBoolProperty>(RefillFn,
            TEXT("ReturnValue")) : nullptr;
        const auto* ProbeResult = ProbeFn ? FindFProperty<FBoolProperty>(ProbeFn,
            TEXT("ReturnValue")) : nullptr;
        if (!ModHandle || ModHandle->Struct != FCk_Handle::StaticStruct() || !ModResult ||
            ((!ModEnum || !ModEnum->GetUnderlyingProperty()) && !ModByte) ||
            !ModifierFn->HasAnyFunctionFlags(FUNC_BlueprintPure) ||
            ModifierFn->NumParms != 3 || !RefillHandle ||
            RefillHandle->Struct != FCk_Handle::StaticStruct() || !RefillResult ||
            !RefillFn->HasAnyFunctionFlags(FUNC_BlueprintPure) ||
            RefillFn->NumParms != 2 || !ProbeResult || ProbeFn->NumParms != 8)
        { return false; }
        const TCHAR* Names[7] = {TEXT("Current"), TEXT("Min"), TEXT("Max"),
            TEXT("Stale"), TEXT("Refill"), TEXT("NonRefill"), TEXT("PartialRefill")};
        for (const auto* Name : Names)
        {
            const auto* P = FindFProperty<FStructProperty>(ProbeFn, Name);
            if (!P || P->Struct != FCk_Handle::StaticStruct()) { return false; }
        }
        return true;
    }

    auto OwnerSchema(UClass* ProbeClass, UFunction*& NativeFn, UFunction*& ProbeFn) -> bool
    {
        NativeFn = UCk_Utils_EntityLifetime_UE::StaticClass()->FindFunctionByName(
            TEXT("Has_LifetimeOwner"));
        ProbeFn = IsValid(ProbeClass) ?
            ProbeClass->FindFunctionByName(TEXT("Probe_LifetimeOwner")) : nullptr;
        const auto* Handle = NativeFn ? FindFProperty<FStructProperty>(NativeFn,
            TEXT("InHandle")) : nullptr;
        const auto* Result = NativeFn ? FindFProperty<FBoolProperty>(NativeFn,
            TEXT("ReturnValue")) : nullptr;
        const auto* ProbeResult = ProbeFn ? FindFProperty<FBoolProperty>(ProbeFn,
            TEXT("ReturnValue")) : nullptr;
        if (!Handle || Handle->Struct != FCk_Handle::StaticStruct() || !Result ||
            !NativeFn->HasAnyFunctionFlags(FUNC_BlueprintPure) || NativeFn->NumParms != 2 ||
            !ProbeResult || ProbeFn->NumParms != 6) { return false; }
        const TCHAR* Names[5] = {TEXT("Attribute"), TEXT("Modifier"), TEXT("Child"),
            TEXT("Transient"), TEXT("Stale")};
        for (const auto* Name : Names)
        {
            const auto* P = FindFProperty<FStructProperty>(ProbeFn, Name);
            if (!P || P->Struct != FCk_Handle::StaticStruct()) { return false; }
        }
        return true;
    }

    auto Verify(FAutomationTestBase& Test, const FFixture& F) -> bool
    {
        if (!Live(F.Owner) || !Live(F.Attribute) || !Live(F.Refill) ||
            !Live(F.Partial) || !Live(F.Child) || ck::IsValid(F.Stale)) { return false; }
        auto* ModClass = UCk_Utils_FloatAttributeModifier_UE::StaticClass();
        UFunction *ModFn = nullptr, *RefillFn = nullptr, *ProbeFn = nullptr;
        UClass* ProbeClass = nullptr;
        if (!Test.TestTrue(TEXT("Float component and refill reflection schema"),
            Schema(ModClass, ModFn, RefillFn, ProbeFn, ProbeClass))) { return false; }
        const FCk_Handle Handles[5] = {F.Modifiers[0], F.Modifiers[1], F.Modifiers[2],
            FCk_Handle{}, F.Stale};
        const ECk_MinMaxCurrent Components[4] = {ECk_MinMaxCurrent::Current,
            ECk_MinMaxCurrent::Min, ECk_MinMaxCurrent::Max,
            static_cast<ECk_MinMaxCurrent>(255)};
        auto* MH = FindFProperty<FStructProperty>(ModFn, TEXT("InModifierEntity"));
        auto* ME = FindFProperty<FEnumProperty>(ModFn, TEXT("InComponent"));
        auto* MB = FindFProperty<FByteProperty>(ModFn, TEXT("InComponent"));
        auto* MR = FindFProperty<FBoolProperty>(ModFn, TEXT("ReturnValue"));
        const auto Attribute = UCk_Utils_FloatAttribute_UE::CastChecked(F.Attribute);
        const float BeforeValue = UCk_Utils_FloatAttribute_UE::Get_FinalValue(Attribute);
        const auto Refill = UCk_Utils_FloatAttributeRefill_UE::CastChecked(F.Refill);
        const int32 BeforeOwnerChildren = UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(F.Owner).Num();
        const int32 BeforeAttributeChildren = UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(F.Attribute).Num();
        const auto BeforeState = UCk_Utils_FloatAttributeRefill_UE::Get_RefillState(Refill);
        bool Good = true;
        for (int32 I = 0; I < 5; ++I)
        for (int32 J = 0; J < 4; ++J)
        {
            const bool Expected = I < 3 && I == J;
            const bool Native = UCk_Utils_FloatAttributeModifier_UE::Has_Component(
                Handles[I], Components[J]);
            FStructOnScope Params(ModFn);
            *MH->ContainerPtrToValuePtr<FCk_Handle>(Params.GetStructMemory()) = Handles[I];
            if (ME)
            {
                ME->GetUnderlyingProperty()->SetIntPropertyValue(
                    ME->ContainerPtrToValuePtr<void>(Params.GetStructMemory()),
                    static_cast<uint64>(Components[J]));
            }
            else
            { MB->SetPropertyValue_InContainer(Params.GetStructMemory(),
                static_cast<uint8>(Components[J])); }
            ModClass->GetDefaultObject()->ProcessEvent(ModFn, Params.GetStructMemory());
            const FString Label = FString::Printf(TEXT("Float modifier component %d/%d"), I, J);
            Good &= Test.TestEqual(Label, Native, Expected);
            Good &= Test.TestEqual(Label + TEXT(" reflected"),
                MR->GetPropertyValue_InContainer(Params.GetStructMemory()), Expected);
        }
        auto* RH = FindFProperty<FStructProperty>(RefillFn, TEXT("InAttributeRefillEntity"));
        auto* RR = FindFProperty<FBoolProperty>(RefillFn, TEXT("ReturnValue"));
        const FCk_Handle RefillCases[5] = {F.Refill, F.Plain, F.Partial,
            FCk_Handle{}, F.Stale};
        for (int32 I = 0; I < 5; ++I)
        {
            const bool Expected = I == 0;
            const bool Native = UCk_Utils_FloatAttributeRefill_UE::Has_Refill(RefillCases[I]);
            FStructOnScope Params(RefillFn);
            *RH->ContainerPtrToValuePtr<FCk_Handle>(Params.GetStructMemory()) = RefillCases[I];
            UCk_Utils_FloatAttributeRefill_UE::StaticClass()->GetDefaultObject()->ProcessEvent(
                RefillFn, Params.GetStructMemory());
            const FString Label = FString::Printf(TEXT("Float refill prerequisite %d"), I);
            Good &= Test.TestEqual(Label, Native, Expected);
            Good &= Test.TestEqual(Label + TEXT(" reflected"),
                RR->GetPropertyValue_InContainer(Params.GetStructMemory()), Expected);
        }
        auto* Probe = NewObject<UObject>(GetTransientPackage(), ProbeClass);
        if (!Test.TestNotNull(TEXT("Float AngelScript component probe"), Probe)) { return false; }
        FStructOnScope ProbeParams(ProbeFn);
        const TCHAR* Names[7] = {TEXT("Current"), TEXT("Min"), TEXT("Max"),
            TEXT("Stale"), TEXT("Refill"), TEXT("NonRefill"), TEXT("PartialRefill")};
        const FCk_Handle ProbeHandles[7] = {F.Modifiers[0], F.Modifiers[1],
            F.Modifiers[2], F.Stale, F.Refill, F.Plain, F.Partial};
        for (int32 I = 0; I < 7; ++I)
        {
            auto* P = FindFProperty<FStructProperty>(ProbeFn, Names[I]);
            *P->ContainerPtrToValuePtr<FCk_Handle>(ProbeParams.GetStructMemory()) = ProbeHandles[I];
        }
        Probe->ProcessEvent(ProbeFn, ProbeParams.GetStructMemory());
        auto* PR = FindFProperty<FBoolProperty>(ProbeFn, TEXT("ReturnValue"));
        Good &= Test.TestTrue(TEXT("AngelScript Float modifier/refill predicates"),
            PR->GetPropertyValue_InContainer(ProbeParams.GetStructMemory()));
        auto* World = ck::auto_test::net::Get_ServerWorld();
        const auto Transient = IsValid(World) ?
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) : FCk_Handle{};
        UFunction *OwnerFn = nullptr, *OwnerProbeFn = nullptr;
        if (!Test.TestTrue(TEXT("lifetime owner reflection and AngelScript schema"),
            OwnerSchema(ProbeClass, OwnerFn, OwnerProbeFn)) || !Live(Transient) ||
            !Live(F.Child)) { return false; }
        const FCk_Handle OwnerCases[7] = {F.Owner, F.Attribute, F.Modifiers[0],
            F.Child, Transient, FCk_Handle{}, F.Stale};
        auto* OH = FindFProperty<FStructProperty>(OwnerFn, TEXT("InHandle"));
        auto* OR = FindFProperty<FBoolProperty>(OwnerFn, TEXT("ReturnValue"));
        for (int32 I = 0; I < 7; ++I)
        {
            const bool Expected = I < 4;
            const bool Native = UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(OwnerCases[I]);
            FStructOnScope Params(OwnerFn);
            *OH->ContainerPtrToValuePtr<FCk_Handle>(Params.GetStructMemory()) = OwnerCases[I];
            UCk_Utils_EntityLifetime_UE::StaticClass()->GetDefaultObject()->ProcessEvent(
                OwnerFn, Params.GetStructMemory());
            const FString Label = FString::Printf(TEXT("lifetime owner prerequisite %d"), I);
            Good &= Test.TestEqual(Label, Native, Expected);
            Good &= Test.TestEqual(Label + TEXT(" reflected"),
                OR->GetPropertyValue_InContainer(Params.GetStructMemory()), Expected);
        }
        FStructOnScope OwnerProbeParams(OwnerProbeFn);
        const TCHAR* OwnerNames[5] = {TEXT("Attribute"), TEXT("Modifier"), TEXT("Child"),
            TEXT("Transient"), TEXT("Stale")};
        const FCk_Handle ProbeOwnerHandles[5] = {F.Attribute, F.Modifiers[0], F.Child,
            Transient, F.Stale};
        for (int32 I = 0; I < 5; ++I)
        {
            auto* P = FindFProperty<FStructProperty>(OwnerProbeFn, OwnerNames[I]);
            *P->ContainerPtrToValuePtr<FCk_Handle>(OwnerProbeParams.GetStructMemory()) =
                ProbeOwnerHandles[I];
        }
        Probe->ProcessEvent(OwnerProbeFn, OwnerProbeParams.GetStructMemory());
        auto* OPR = FindFProperty<FBoolProperty>(OwnerProbeFn, TEXT("ReturnValue"));
        Good &= Test.TestTrue(TEXT("AngelScript lifetime owner predicate"),
            OPR->GetPropertyValue_InContainer(OwnerProbeParams.GetStructMemory()));
        Good &= Test.TestEqual(TEXT("lifetime owner probe leaves owner child count unchanged"),
            UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(F.Owner).Num(), BeforeOwnerChildren);
        Good &= Test.TestEqual(TEXT("lifetime owner probe leaves attribute child count unchanged"),
            UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(F.Attribute).Num(),
            BeforeAttributeChildren);
        Good &= Test.TestEqual(TEXT("Float attribute value unchanged by predicate probes"),
            UCk_Utils_FloatAttribute_UE::Get_FinalValue(Attribute), BeforeValue);
        Good &= Test.TestEqual(TEXT("Float refill state unchanged by predicate probes"),
            static_cast<uint8>(UCk_Utils_FloatAttributeRefill_UE::Get_RefillState(Refill)),
            static_cast<uint8>(BeforeState));
        return Good;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkAttributeFloatComponents_Network,
    "Ck.Gym.Network.AttributeFloat.Components",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
        EAutomationTestFlags::EngineFilter)

bool FCkAttributeFloatComponents_Network::RunTest(const FString& Parameters)
{
    using namespace ck_attribute_float_component_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Float component network topology")); return false; }
    const auto F = MakeShared<FFixture>();
    auto* Mode = FSoftClassPath{ModePath}.TryLoadClass<AGameModeBase>();
    if (!TestNotNull(TEXT("existing network gym mode"), Mode)) { return false; }
    UFunction *ModFn = nullptr, *RefillFn = nullptr, *ProbeFn = nullptr;
    UClass* ProbeClass = nullptr;
    if (!TestTrue(TEXT("Float predicate schema before PIE"),
        Schema(UCk_Utils_FloatAttributeModifier_UE::StaticClass(),
            ModFn, RefillFn, ProbeFn, ProbeClass))) { return false; }
    UFunction *OwnerFn = nullptr, *OwnerProbeFn = nullptr;
    if (!TestTrue(TEXT("lifetime owner schema before PIE"),
        OwnerSchema(ProbeClass, OwnerFn, OwnerProbeFn))) { return false; }
    F->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || !IsValid(ck::auto_test::net::Get_ClientWorld(0)) ||
                !IsValid(ck::auto_test::net::Get_ClientWorld(1))) { return false; }
            auto* First = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0);
            auto* Second = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1);
            return IsValid(First) && IsValid(Second) && First != Second;
        }), 60.0, TEXT("two correlated remote controllers in three Float probe worlds")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* World)
        {
            F->Created = Create(World, *F);
            TestTrue(TEXT("disposable world-owned Float attribute/refill/modifiers"), F->Created);
            if (!F->Created && Live(F->Owner))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F->Owner); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
        { return !F->Created || (Live(F->Owner) && Live(F->Child) && Live(F->Refill) &&
            Live(F->Modifiers[0]) && Live(F->Modifiers[1]) &&
            Live(F->Modifiers[2]) && ck::Is_NOT_Valid(F->Stale)); }),
        15.0, TEXT("stale Float modifier fully retired")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld*)
        {
            if (F->Created)
            { TestTrue(TEXT("native/reflected/AngelScript Float predicates"), Verify(*this, *F)); }
            if (Live(F->Owner))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F->Owner); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
        {
            if (ck::IsValid(F->Owner) || ck::IsValid(F->Attribute) ||
                ck::IsValid(F->Refill) || ck::IsValid(F->Plain) ||
                ck::IsValid(F->Partial) || ck::IsValid(F->Child) ||
                ck::IsValid(F->Stale)) { return false; }
            for (const auto& H : F->Modifiers)
            { if (ck::IsValid(H)) { return false; } }
            return true;
        }), 20.0, TEXT("all disposable Float features fully retired")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            return TestTrue(TEXT("Float probe features retired with all three PIE worlds intact"),
                ck::Is_NOT_Valid(F->Owner) && ck::Is_NOT_Valid(F->Refill) &&
                ck::auto_test::net::Get_AllPIEWorlds().Num() == 3);
        }), TEXT("Float component probe isolation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            F->RestoreSettings();
            return TestTrue(TEXT("Float probe PIE worlds drained"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
        }), TEXT("restore Float probe startup settings"), true));
    return true;
}

#endif
