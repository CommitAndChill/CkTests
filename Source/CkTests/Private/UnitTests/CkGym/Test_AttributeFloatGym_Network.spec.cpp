#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_NetworkRuntime.h"
#include "CkGymRegistry_Utils.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkAttribute/FloatAttribute/CkFloatAttribute_Utils.h"
#include "CkTimer/CkTimer_Utils.h"

#include <Engine/GameInstance.h>
#include <Engine/World.h>
#include <limits>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/PlayerController.h>
#include <GameplayTagContainer.h>
#include <HAL/PlatformMath.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>
#include <UObject/UObjectGlobals.h>

namespace ck_attribute_float_network_test
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_FloatAttributeGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_FloatAttributeGym_GameState");
    constexpr auto ControllerPath = TEXT("/Script/Angelscript.Ck_FloatAttributeGym_PlayerController");
    constexpr auto ProjectionPath = TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_FloatProjection");
    constexpr const TCHAR* AnchorNames[8] = {
        TEXT("Gym.Attribute.FloatValues"), TEXT("Gym.Attribute.FloatClamping"),
        TEXT("Gym.Attribute.FloatModifiers"), TEXT("Gym.Attribute.FloatMinMaxCurrent"),
        TEXT("Gym.Attribute.FloatSignals"), TEXT("Gym.Attribute.FloatRefill"),
        TEXT("Gym.Attribute.FloatIncrementDecrement"), TEXT("Gym.Attribute.FloatMultiple")};
    constexpr const TCHAR* ChildPaths[8] = {
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_FloatValues"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_FloatClamping"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_FloatModifiers"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_FloatMinMaxCurrent"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_FloatSignals"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_FloatRefill"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_FloatIncrementDecrement"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_FloatMultiple")};
    constexpr int32 RingActions[10] = {12, 13, 14, 15, 16, 18, 19, 20, 21, 24};
    constexpr const TCHAR* RingNames[10] = {
        TEXT("ValuesActionIndex"), TEXT("ModifiersActionIndex"),
        TEXT("RefillActionIndex"), TEXT("IncDecActionIndex"),
        TEXT("MultipleActionIndex"), TEXT("MinPresetIndex"),
        TEXT("MaxPresetIndex"), TEXT("CurrentPresetIndex"),
        TEXT("SignalPresetIndex"), TEXT("ResetActionIndex")};
    constexpr int32 InitialAttributes[8] = {1, 4, 2, 1, 1, 4, 1, 3};
    constexpr const TCHAR* ChildTags[8] = {
        TEXT("TAG_AttributeGym_FloatValues"), TEXT("TAG_AttributeGym_FloatClamping"),
        TEXT("TAG_AttributeGym_FloatModifiers"), TEXT("TAG_AttributeGym_FloatMinMaxCurrent"),
        TEXT("TAG_AttributeGym_FloatSignals"), TEXT("TAG_AttributeGym_FloatRefill"),
        TEXT("TAG_AttributeGym_FloatIncrementDecrement"),
        TEXT("TAG_AttributeGym_FloatMultiple")};
    constexpr const TCHAR* AttributeNames[8][5] = {
        {TEXT("FloatAttribute.Test")},
        {TEXT("FloatAttribute.Armor"), TEXT("FloatAttribute.Stamina"),
         TEXT("FloatAttribute.Health"), TEXT("FloatAttribute.Defense")},
        {TEXT("FloatAttribute.Damage"), TEXT("FloatAttribute.Defense")},
        {TEXT("FloatAttribute.PowerLevel")}, {TEXT("FloatAttribute.Signal")},
        {TEXT("FloatAttribute.Energy"), TEXT("FloatAttribute.Mana"),
         TEXT("FloatAttribute.Energy.Refill"), TEXT("FloatAttribute.Mana.Refill")},
        {TEXT("FloatAttribute.Counter")},
        {TEXT("RPG.Strength"), TEXT("RPG.Agility"), TEXT("RPG.Intelligence"),
         TEXT("Combat.Attack"), TEXT("Combat.Defense")}};

    struct FFixture
    {
        UClass *Mode = nullptr, *StateClass = nullptr, *Controller = nullptr;
        FStructProperty *Snapshot = nullptr, *Commit = nullptr, *CommitSnapshot = nullptr;
        FStructProperty *Station = nullptr, *Attribute = nullptr, *Component = nullptr;
        FArrayProperty *Stations = nullptr, *Attributes = nullptr, *ServerChildren = nullptr;
        FIntProperty *Generation = nullptr, *Sequence = nullptr, *ServerChildCount = nullptr;
        FIntProperty *CommitSerial = nullptr, *CommitAction = nullptr, *CommitPreset = nullptr;
        FIntProperty *Accepted = nullptr, *Attempt = nullptr, *Result = nullptr;
        FIntProperty *RpcSerial = nullptr, *RpcAction = nullptr, *RpcPreset = nullptr;
        FIntProperty *SignalRing = nullptr, *MinRing = nullptr;
        FIntProperty* Rings[10] = {};
        FIntProperty *LocalGeneration = nullptr;
        FBoolProperty *Ready = nullptr, *Failed = nullptr, *LocalReady = nullptr, *LocalFailed = nullptr;
        UFunction* Rpc = nullptr;
        ECkGym_StartupMode OriginalMode = ECkGym_StartupMode::Cycler;
        FString OriginalLast;
        TArray<FString> OriginalRecents;
        FCk_Handle ServerRoots[8];
        bool RootsCaptured = false;
        int32 InitialGeneration = 0, BeforeSequence = 0;
        int32 NaturalIncrementFloorSequence = 0;
        int32 BeforeAccepted[2] = {0, 0}, BeforeAttempt[2] = {0, 0};
        int32 BeforeResult[2] = {0, 0}, BeforeSignalRing[2] = {-1, -1};
        int32 BeforeMinRing[2] = {-1, -1}, BeforeCommitSerial = 0;
        int32 BeforeRings[2][10] = {};
        FCk_Handle BeforeCombat[2];
        bool HadCombatBefore = false;
        FCk_Handle_FloatAttributeModifier BeforeKeys[3];
        bool BeforeKeyPresent[3] = {};
        TSharedPtr<FStructOnScope> BeforeSnapshot;
        TMap<int32, TSharedPtr<FStructOnScope>> History;
        TArray<int32> HistoryOrder;
        bool TransactionViolation = false;
        void CaptureSettings()
        {
            const auto* S = GetDefault<UCkGym_StartupSettings>();
            OriginalMode = S->StartupMode;
            OriginalLast = S->LastGymName;
            OriginalRecents = S->RecentGymNames;
        }
        void RestoreSettings() const
        {
            auto* S = GetMutableDefault<UCkGym_StartupSettings>();
            S->StartupMode = OriginalMode;
            S->LastGymName = OriginalLast;
            S->RecentGymNames = OriginalRecents;
            S->SaveConfig();
        }
    };

    template<typename T> auto Prop(const UStruct* S, const TCHAR* N) -> T*
    { return IsValid(S) ? FindFProperty<T>(S, N) : nullptr; }
    auto Live(const FCk_Handle& H) -> bool
    { return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
        H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    auto WorldRoot(UWorld* W) -> FCk_Handle
    { return IsValid(W) ? UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(W) : FCk_Handle{}; }
    auto State(UWorld* W, const FFixture& F) -> AGameStateBase*
    {
        auto* S = IsValid(W) ? W->GetGameState() : nullptr;
        return IsValid(S) && S->IsA(F.StateClass) ? S : nullptr;
    }
    auto Data(AGameStateBase* S, const FFixture& F) -> const void*
    { return IsValid(S) ? F.Snapshot->ContainerPtrToValuePtr<void>(S) : nullptr; }
    auto Int(const UStruct* S, const void* D, const TCHAR* N) -> int32
    { return Prop<FIntProperty>(S, N)->GetPropertyValue_InContainer(D); }
    auto Flag(const UStruct* S, const void* D, const TCHAR* N) -> bool
    { return Prop<FBoolProperty>(S, N)->GetPropertyValue_InContainer(D); }
    auto Float(const UStruct* S, const void* D, const TCHAR* N) -> double
    {
        const auto* P = Prop<FNumericProperty>(S, N);
        return P->GetFloatingPointPropertyValue(P->ContainerPtrToValuePtr<void>(D));
    }
    auto Gen(AGameStateBase* S, const FFixture& F) -> int32
    { const auto* D = Data(S, F); return D ? F.Generation->GetPropertyValue_InContainer(D) : INDEX_NONE; }
    auto Seq(AGameStateBase* S, const FFixture& F) -> int32
    { const auto* D = Data(S, F); return D ? F.Sequence->GetPropertyValue_InContainer(D) : INDEX_NONE; }
    auto Ready(AGameStateBase* S, const FFixture& F, int32 G) -> bool
    {
        const auto* D = Data(S, F);
        return D && Gen(S, F) == G && Seq(S, F) > 0 &&
            F.Ready->GetPropertyValue_InContainer(D) && !F.Failed->GetPropertyValue_InContainer(D);
    }
    auto ChildClass(const FCk_Handle& H) -> FString
    {
        if (!Live(H) || !UCk_Utils_EntityScript_UE::Has(H)) { return {}; }
        const auto Script = UCk_Utils_EntityScript_UE::CastChecked(H);
        const auto* Class = UCk_Utils_EntityScript_UE::Get_ScriptClass(Script).Get();
        return IsValid(Class) ? Class->GetPathName() : FString{};
    }
    auto Collect(const FCk_Handle& H, int32& Attributes, int32& Timers) -> void
    {
        if (!Live(H)) { return; }
        Attributes += UCk_Utils_FloatAttribute_UE::Has(H) ? 1 : 0;
        Timers += UCk_Utils_Timer_UE::Has(H) ? 1 : 0;
        for (const auto& C : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(H))
        { Collect(C, Attributes, Timers); }
    }
    auto Branches(UWorld* W, const FFixture& F, bool Authority, bool Dedicated,
                  TArray<FCk_Handle>* Originals = nullptr) -> bool
    {
        const auto Root = WorldRoot(W);
        if (!Live(Root)) { return false; }
        int32 Attributes = 0, Timers = 0;
        for (int32 I = 0; I < 8; ++I)
        {
            const auto Anchors = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, FName(AnchorNames[I]));
            if (Anchors.Num() != 1 || !Live(Anchors[0])) { return false; }
            int32 Actual = 0, Projected = 0;
            for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Anchors[0]))
            {
                if (!Live(Child) || !UCk_Utils_EntityScript_UE::Has(Child)) { continue; }
                const auto Path = ChildClass(Child);
                if (Path == ChildPaths[I])
                {
                    Actual++;
                    if (Originals) { Originals->Add(Child); }
                    int32 A = 0, T = 0; Collect(Child, A, T);
                    if ((I == 7 ? (A != 3 && A != 5) : A != InitialAttributes[I]) ||
                        T < 2) { return false; }
                    Attributes += A; Timers += T;
                }
                else if (Path == ProjectionPath)
                {
                    Projected++;
                    int32 A = 0, T = 0; Collect(Child, A, T);
                    if (A != 0 || T != 1) { return false; }
                }
                else { return false; }
            }
            if (Actual != (Authority ? 1 : 0) ||
                Projected != (Authority && Dedicated ? 0 : 1)) { return false; }
        }
        return !Authority || ((Attributes == 17 || Attributes == 19) && Timers >= 16);
    }
    auto Typed(const void* D, const FFixture& F) -> bool
    {
        if (!D || !F.Ready->GetPropertyValue_InContainer(D) ||
            F.Failed->GetPropertyValue_InContainer(D) ||
            F.Generation->GetPropertyValue_InContainer(D) <= 0 ||
            F.Sequence->GetPropertyValue_InContainer(D) <= 0) { return false; }
        FScriptArrayHelper Rows(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(D));
        if (Rows.Num() != 8) { return false; }
        const auto* Station = F.Station->Struct.Get();
        const auto* Attribute = F.Attribute->Struct.Get();
        const auto* Component = F.Component->Struct.Get();
        for (int32 I = 0; I < 8; ++I)
        {
            const void* R = Rows.GetRawPtr(I);
            if (Int(Station, R, TEXT("StationIndex")) != I ||
                Prop<FNameProperty>(Station, TEXT("StationTag"))->GetPropertyValue_InContainer(R) !=
                    FName(AnchorNames[I]) ||
                Prop<FNameProperty>(Station, TEXT("ChildTag"))->GetPropertyValue_InContainer(R) !=
                    FName(ChildTags[I]) ||
                Int(Station, R, TEXT("AutoStep")) < 0 ||
                !FMath::IsFinite(Float(Station, R, TEXT("LastOverflow"))) ||
                !FMath::IsFinite(Float(Station, R, TEXT("EnergyFillRate")))) { return false; }
            FScriptArrayHelper Attrs(F.Attributes, F.Attributes->ContainerPtrToValuePtr<void>(R));
            const int32 Expected = I == 7 ? Int(Station, R, TEXT("TotalAttributes")) : InitialAttributes[I];
            if (Attrs.Num() != Expected ||
                (I == 7 && ((Expected != 3 && Expected != 5) ||
                    Int(Station, R, TEXT("CombatAttributes")) != Expected - 3)))
            { return false; }
            for (int32 J = 0; J < Attrs.Num(); ++J)
            {
                const void* A = Attrs.GetRawPtr(J);
                if (Prop<FNameProperty>(Attribute, TEXT("Name"))->GetPropertyValue_InContainer(A) !=
                    FName(AttributeNames[I][J])) { return false; }
                for (const auto* Key : {TEXT("Min"), TEXT("Max"), TEXT("Current")})
                {
                    const auto* P = Prop<FStructProperty>(Attribute, Key);
                    const void* C = P->ContainerPtrToValuePtr<void>(A);
                    if (!Flag(Component, C, TEXT("Present"))) { continue; }
                    for (const auto* V : {TEXT("Base"), TEXT("Bonus"), TEXT("Final")})
                    { if (!FMath::IsFinite(Float(Component, C, V))) { return false; } }
                }
                if (!FMath::IsFinite(Float(Attribute, A, TEXT("PreClampFinal"))) ||
                    !FMath::IsFinite(Float(Attribute, A, TEXT("ClampOverflow")))) { return false; }
            }
        }
        return true;
    }
    auto Remember(FFixture& F, AGameStateBase* S) -> bool
    {
        const void* D = Data(S, F);
        if (!Typed(D, F)) { return false; }
        const int32 Sequence = Seq(S, F);
        if (const auto* Found = F.History.Find(Sequence))
        {
            if (!F.Snapshot->Identical(D, (*Found)->GetStructMemory(), 0))
            { F.TransactionViolation = true; return false; }
            return true;
        }
        auto Copy = MakeShared<FStructOnScope>(F.Snapshot->Struct.Get());
        F.Snapshot->Struct->CopyScriptStruct(Copy->GetStructMemory(), D);
        F.History.Add(Sequence, Copy);
        F.HistoryOrder.Add(Sequence);
        if (F.HistoryOrder.Num() > 256)
        { F.History.Remove(F.HistoryOrder[0]); F.HistoryOrder.RemoveAt(0); }
        return true;
    }
    auto Observed(AGameStateBase* S, const FFixture& F) -> bool
    {
        const void* D = Data(S, F);
        if (!Typed(D, F) || Gen(S, F) != F.InitialGeneration) { return false; }
        const auto* Copy = F.History.Find(Seq(S, F));
        return Copy && F.Snapshot->Identical(D, (*Copy)->GetStructMemory(), 0);
    }
    auto RefillConfigurationUnchanged(const void* Before, const void* After,
                                      const FFixture& F) -> bool
    {
        const auto* Station = F.Station->Struct.Get();
        const auto* Attribute = F.Attribute->Struct.Get();
        const auto* Component = F.Component->Struct.Get();
        for (const auto* Key : {TEXT("StationIndex"), TEXT("AutoStep"),
                                TEXT("TotalAttributes"), TEXT("CombatAttributes")})
        { if (Int(Station, Before, Key) != Int(Station, After, Key)) { return false; } }
        for (const auto* Key : {TEXT("StationTag"), TEXT("ChildTag")})
        {
            auto* P = Prop<FNameProperty>(Station, Key);
            if (P->GetPropertyValue_InContainer(Before) !=
                P->GetPropertyValue_InContainer(After)) { return false; }
        }
        for (const auto* Key : {TEXT("AutoEnabled"), TEXT("AutoRunning"),
                                TEXT("EnergyRefillRunning"), TEXT("ManaRefillRunning"),
                                TEXT("DesiredEnergyRefillRunning"), TEXT("DesiredManaRefillRunning")})
        { if (Flag(Station, Before, Key) != Flag(Station, After, Key)) { return false; } }
        for (const auto* Key : {TEXT("EnergyFillRate"), TEXT("ManaFillRate")})
        { if (Float(Station, Before, Key) != Float(Station, After, Key)) { return false; } }
        FScriptArrayHelper Old(F.Attributes, F.Attributes->ContainerPtrToValuePtr<void>(Before));
        FScriptArrayHelper Now(F.Attributes, F.Attributes->ContainerPtrToValuePtr<void>(After));
        if (Old.Num() != 4 || Now.Num() != 4) { return false; }
        for (int32 I = 0; I < 4; ++I)
        {
            const void* A = Old.GetRawPtr(I);
            const void* B = Now.GetRawPtr(I);
            auto* Name = Prop<FNameProperty>(Attribute, TEXT("Name"));
            if (Name->GetPropertyValue_InContainer(A) !=
                Name->GetPropertyValue_InContainer(B)) { return false; }
            for (const auto* Key : {TEXT("Min"), TEXT("Max")})
            {
                auto* P = Prop<FStructProperty>(Attribute, Key);
                if (!P->Identical(P->ContainerPtrToValuePtr<void>(A),
                                  P->ContainerPtrToValuePtr<void>(B), 0)) { return false; }
            }
            auto* Current = Prop<FStructProperty>(Attribute, TEXT("Current"));
            const void* OldCurrent = Current->ContainerPtrToValuePtr<void>(A);
            const void* NewCurrent = Current->ContainerPtrToValuePtr<void>(B);
            if (Flag(Component, OldCurrent, TEXT("Present")) !=
                    Flag(Component, NewCurrent, TEXT("Present")) ||
                Float(Component, OldCurrent, TEXT("Bonus")) !=
                    Float(Component, NewCurrent, TEXT("Bonus"))) { return false; }
        }
        // Refill Current and presentation/counters may advance while AutoTimer is paused.
        return true;
    }
    auto RejectedStationsUnchanged(AGameStateBase* S, const FFixture& F) -> bool
    {
        if (!IsValid(S) || !F.BeforeSnapshot.IsValid() || !Typed(Data(S, F), F))
        { return false; }
        FScriptArrayHelper Before(F.Stations,
            F.Stations->ContainerPtrToValuePtr<void>(F.BeforeSnapshot->GetStructMemory()));
        FScriptArrayHelper After(F.Stations,
            F.Stations->ContainerPtrToValuePtr<void>(Data(S, F)));
        if (Before.Num() != 8 || After.Num() != 8) { return false; }
        for (int32 I = 0; I < 8; ++I)
        {
            if (I == 5)
            {
                if (!RefillConfigurationUnchanged(Before.GetRawPtr(I), After.GetRawPtr(I), F))
                { return false; }
            }
            else if (!F.Station->Identical(Before.GetRawPtr(I), After.GetRawPtr(I), 0))
            { return false; }
        }
        return true;
    }
    auto Bind(FFixture& F, FAutomationTestBase& Test) -> bool
    {
        F.Mode = FSoftClassPath{ModePath}.TryLoadClass<AGameModeBase>();
        F.StateClass = FSoftClassPath{StatePath}.TryLoadClass<AGameStateBase>();
        F.Controller = FSoftClassPath{ControllerPath}.TryLoadClass<APlayerController>();
        bool Good = Test.TestNotNull(TEXT("Float mode"), F.Mode) &&
            Test.TestNotNull(TEXT("Float state"), F.StateClass) &&
            Test.TestNotNull(TEXT("Float controller"), F.Controller) &&
            Test.TestNotNull(TEXT("Float projection"),
                FSoftClassPath{ProjectionPath}.TryLoadClass<UObject>());
        for (const auto* Path : ChildPaths)
        { Good &= Test.TestNotNull(TEXT("Float original"), FSoftClassPath{Path}.TryLoadClass<UObject>()); }
        if (!Good) { return false; }
        F.Snapshot = Prop<FStructProperty>(F.StateClass, TEXT("Snapshot"));
        F.Commit = Prop<FStructProperty>(F.StateClass, TEXT("LastControlCommit"));
        F.ServerChildren = Prop<FArrayProperty>(F.StateClass, TEXT("ServerChildren"));
        F.ServerChildCount = Prop<FIntProperty>(F.StateClass, TEXT("ServerChildCount"));
        F.LocalGeneration = Prop<FIntProperty>(F.StateClass, TEXT("LocalProjectionGeneration"));
        F.LocalReady = Prop<FBoolProperty>(F.StateClass, TEXT("LocalProjectionReady"));
        F.LocalFailed = Prop<FBoolProperty>(F.StateClass, TEXT("LocalProjectionFailed"));
        Good &= Test.TestNotNull(TEXT("Float snapshot"), F.Snapshot);
        Good &= Test.TestNotNull(TEXT("Float commit"), F.Commit);
        Good &= Test.TestNotNull(TEXT("Float children"), F.ServerChildren);
        if (!Good) { return false; }
        const auto* Snap = F.Snapshot->Struct.Get();
        const auto* Commit = F.Commit->Struct.Get();
        F.Generation = Prop<FIntProperty>(Snap, TEXT("Generation"));
        F.Sequence = Prop<FIntProperty>(Snap, TEXT("Sequence"));
        F.Ready = Prop<FBoolProperty>(Snap, TEXT("Ready"));
        F.Failed = Prop<FBoolProperty>(Snap, TEXT("Failed"));
        F.Stations = Prop<FArrayProperty>(Snap, TEXT("Stations"));
        F.CommitSerial = Prop<FIntProperty>(Commit, TEXT("Serial"));
        F.CommitAction = Prop<FIntProperty>(Commit, TEXT("Action"));
        F.CommitPreset = Prop<FIntProperty>(Commit, TEXT("Preset"));
        F.CommitSnapshot = Prop<FStructProperty>(Commit, TEXT("Snapshot"));
        Good &= Test.TestNotNull(TEXT("Float station array"), F.Stations);
        if (!Good) { return false; }
        F.Station = CastField<FStructProperty>(F.Stations->Inner);
        Good &= Test.TestNotNull(TEXT("Float station sample"), F.Station);
        if (!Good) { return false; }
        F.Attributes = Prop<FArrayProperty>(F.Station->Struct, TEXT("Attributes"));
        Good &= Test.TestNotNull(TEXT("Float attribute array"), F.Attributes);
        if (!Good) { return false; }
        F.Attribute = CastField<FStructProperty>(F.Attributes->Inner);
        Good &= Test.TestNotNull(TEXT("Float attribute sample"), F.Attribute);
        if (!Good) { return false; }
        F.Component = Prop<FStructProperty>(F.Attribute->Struct, TEXT("Current"));
        Good &= Test.TestNotNull(TEXT("Float component sample"), F.Component);
        if (!Good) { return false; }
        for (const auto* Key : {TEXT("Generation"), TEXT("Sequence")})
        { Good &= Test.TestNotNull(Key, Prop<FIntProperty>(Snap, Key)); }
        for (const auto* Key : {TEXT("Ready"), TEXT("Failed")})
        { Good &= Test.TestNotNull(Key, Prop<FBoolProperty>(Snap, Key)); }
        for (const auto* Key : {TEXT("StationIndex"), TEXT("AutoStep"),
                                TEXT("TotalAttributes"), TEXT("CombatAttributes")})
        { Good &= Test.TestNotNull(Key, Prop<FIntProperty>(F.Station->Struct, Key)); }
        for (const auto* Key : {TEXT("StationTag"), TEXT("ChildTag")})
        { Good &= Test.TestNotNull(Key, Prop<FNameProperty>(F.Station->Struct, Key)); }
        for (const auto* Key : {TEXT("AutoRunning"), TEXT("AutoEnabled"),
                                TEXT("EnergyRefillRunning"), TEXT("ManaRefillRunning"),
                                TEXT("DesiredEnergyRefillRunning"),
                                TEXT("DesiredManaRefillRunning")})
        { Good &= Test.TestNotNull(Key, Prop<FBoolProperty>(F.Station->Struct, Key)); }
        for (const auto* Key : {TEXT("LastOverflow"), TEXT("EnergyFillRate"),
                                TEXT("ManaFillRate")})
        { const auto* P = Prop<FNumericProperty>(F.Station->Struct, Key);
          Good &= Test.TestTrue(Key, P && P->IsFloatingPoint()); }
        for (const auto* Key : {TEXT("PreClampFinal"), TEXT("ClampOverflow")})
        { const auto* P = Prop<FNumericProperty>(F.Attribute->Struct, Key);
          Good &= Test.TestTrue(Key, P && P->IsFloatingPoint()); }
        for (const auto* Key : {TEXT("Min"), TEXT("Max"), TEXT("Current")})
        { const auto* P = Prop<FStructProperty>(F.Attribute->Struct, Key);
          Good &= Test.TestTrue(Key, P && P->Struct == F.Component->Struct); }
        Good &= Test.TestNotNull(TEXT("attribute name"),
            Prop<FNameProperty>(F.Attribute->Struct, TEXT("Name")));
        Good &= Test.TestNotNull(TEXT("component present"),
            Prop<FBoolProperty>(F.Component->Struct, TEXT("Present")));
        for (const auto* Key : {TEXT("Base"), TEXT("Bonus"), TEXT("Final")})
        { const auto* P = Prop<FNumericProperty>(F.Component->Struct, Key);
          Good &= Test.TestTrue(Key, P && P->IsFloatingPoint()); }
        F.Accepted = Prop<FIntProperty>(F.Controller, TEXT("LastControlSerial"));
        F.Attempt = Prop<FIntProperty>(F.Controller, TEXT("LastControlAttemptSerial"));
        F.Result = Prop<FIntProperty>(F.Controller, TEXT("LastControlResult"));
        F.SignalRing = Prop<FIntProperty>(F.Controller, TEXT("SignalPresetIndex"));
        F.MinRing = Prop<FIntProperty>(F.Controller, TEXT("MinPresetIndex"));
        for (int32 I = 0; I < 10; ++I)
        {
            F.Rings[I] = Prop<FIntProperty>(F.Controller, RingNames[I]);
            Good &= Test.TestNotNull(TEXT("Float owner preset ring"), F.Rings[I]);
        }
        F.Rpc = F.Controller->FindFunctionByName(TEXT("Server_RequestFloatControl"));
        Good &= Test.TestNotNull(TEXT("Float RPC"), F.Rpc);
        if (!Good) { return false; }
        F.RpcSerial = Prop<FIntProperty>(F.Rpc, TEXT("InSerial"));
        F.RpcAction = Prop<FIntProperty>(F.Rpc, TEXT("InAction"));
        F.RpcPreset = Prop<FIntProperty>(F.Rpc, TEXT("InPreset"));
        const auto* Handles = CastField<FStructProperty>(F.ServerChildren->Inner);
        Good &= F.Generation && F.Sequence && F.Ready && F.Failed &&
            F.CommitSerial && F.CommitAction && F.CommitPreset && F.CommitSnapshot &&
            F.ServerChildCount && F.LocalGeneration && F.LocalReady && F.LocalFailed &&
            F.Accepted && F.Attempt && F.Result && F.SignalRing && F.MinRing &&
            F.RpcSerial && F.RpcAction && F.RpcPreset;
        if (!Good) { return false; }
        return Test.TestTrue(TEXT("exact Float reflected transport schema"), Good &&
            F.Snapshot->HasAnyPropertyFlags(CPF_Net) &&
            F.Commit->HasAnyPropertyFlags(CPF_Net) &&
            Snap->GetName() == TEXT("CkAttributeGym_FloatSnapshot") &&
            F.Station->Struct->GetName() == TEXT("CkAttributeGym_FloatStationSample") &&
            F.Attribute->Struct->GetName() == TEXT("CkAttributeGym_FloatAttributeSample") &&
            F.Component->Struct->GetName() == TEXT("CkAttributeGym_FloatComponentSample") &&
            F.Generation && F.Sequence && F.Ready && F.Failed &&
            F.CommitSerial && F.CommitAction && F.CommitPreset && F.CommitSnapshot &&
            F.CommitSnapshot->Struct == Snap && F.ServerChildCount &&
            F.LocalGeneration && F.LocalReady && F.LocalFailed &&
            Handles && Handles->Struct == FCk_Handle::StaticStruct() &&
            F.Accepted && F.Attempt && F.Result && F.SignalRing && F.MinRing &&
            F.Accepted->HasAnyPropertyFlags(CPF_Net) &&
            F.Attempt->HasAnyPropertyFlags(CPF_Net) &&
            F.Result->HasAnyPropertyFlags(CPF_Net) &&
            F.RpcSerial && F.RpcAction && F.RpcPreset && F.Rpc->NumParms == 3 &&
            F.Rpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer));
    }
    auto Invoke(APlayerController* PC, const FFixture& F,
                int32 Serial, int32 Action, int32 Preset) -> bool
    {
        if (!IsValid(PC) || !PC->IsA(F.Controller)) { return false; }
        FStructOnScope P(F.Rpc);
        F.RpcSerial->SetPropertyValue_InContainer(P.GetStructMemory(), Serial);
        F.RpcAction->SetPropertyValue_InContainer(P.GetStructMemory(), Action);
        F.RpcPreset->SetPropertyValue_InContainer(P.GetStructMemory(), Preset);
        PC->ProcessEvent(F.Rpc, P.GetStructMemory());
        return true;
    }
    auto Capture(FFixture& F) -> bool
    {
        auto* S = State(ck::auto_test::net::Get_ServerWorld(), F);
        if (!IsValid(S) || !Typed(Data(S, F), F)) { return false; }
        F.BeforeSequence = Seq(S, F);
        F.BeforeCommitSerial = F.CommitSerial->GetPropertyValue_InContainer(
            F.Commit->ContainerPtrToValuePtr<void>(S));
        F.BeforeSnapshot = MakeShared<FStructOnScope>(F.Snapshot->Struct.Get());
        F.Snapshot->Struct->CopyScriptStruct(F.BeforeSnapshot->GetStructMemory(), Data(S, F));
        for (int32 I = 0; I < 2; ++I)
        {
            auto* W = ck::auto_test::net::Get_ClientWorld(I);
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            if (!IsValid(PC) || !PC->IsA(F.Controller)) { return false; }
            F.BeforeAccepted[I] = F.Accepted->GetPropertyValue_InContainer(PC);
            F.BeforeAttempt[I] = F.Attempt->GetPropertyValue_InContainer(PC);
            F.BeforeResult[I] = F.Result->GetPropertyValue_InContainer(PC);
            F.BeforeSignalRing[I] = F.SignalRing->GetPropertyValue_InContainer(PC);
            F.BeforeMinRing[I] = F.MinRing->GetPropertyValue_InContainer(PC);
            for (int32 R = 0; R < 10; ++R)
            { F.BeforeRings[I][R] = F.Rings[R]->GetPropertyValue_InContainer(PC); }
        }
        return true;
    }
    auto CommitMatches(AGameStateBase* S, const FFixture& F,
                       int32 Serial, int32 Action, int32 Preset) -> bool
    {
        auto* Authority = State(ck::auto_test::net::Get_ServerWorld(), F);
        if (!Ready(S, F, F.InitialGeneration) ||
            !Ready(Authority, F, F.InitialGeneration)) { return false; }
        const auto* Commit = F.Commit->ContainerPtrToValuePtr<void>(S);
        const auto* ServerCommit = F.Commit->ContainerPtrToValuePtr<void>(Authority);
        const auto* Snap = F.CommitSnapshot->ContainerPtrToValuePtr<void>(Commit);
        const auto* ServerSnap = F.CommitSnapshot->ContainerPtrToValuePtr<void>(ServerCommit);
        const int32 CommitSequence = F.Sequence->GetPropertyValue_InContainer(Snap);
        return F.CommitSerial->GetPropertyValue_InContainer(Commit) == Serial &&
            F.CommitAction->GetPropertyValue_InContainer(Commit) == Action &&
            F.CommitPreset->GetPropertyValue_InContainer(Commit) == Preset &&
            CommitSequence > F.BeforeSequence && Seq(S, F) >= CommitSequence &&
            F.CommitSerial->GetPropertyValue_InContainer(ServerCommit) == Serial &&
            F.Snapshot->Identical(Snap, ServerSnap, 0) && Typed(Snap, F) &&
            (Seq(S, F) != CommitSequence || F.Snapshot->Identical(Data(S, F), Snap, 0));
    }
    auto ControlValueMatches(AGameStateBase* S, const FFixture& F,
                             int32 Action) -> bool
    {
        if (Action == 1)
        {
            if (!IsValid(S)) { return false; }
            const auto* Commit = F.Commit->ContainerPtrToValuePtr<void>(S);
            const auto* Snap = F.CommitSnapshot->ContainerPtrToValuePtr<void>(Commit);
            FScriptArrayHelper Rows(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(Snap));
            if (Rows.Num() != 8) { return false; }
            for (int32 I = 0; I < 8; ++I)
            {
                const void* Row = Rows.GetRawPtr(I);
                if (Flag(F.Station->Struct, Row, TEXT("AutoEnabled")) ||
                    Flag(F.Station->Struct, Row, TEXT("AutoRunning"))) { return false; }
            }
            return true;
        }
        if (Action != 18 && Action != 21) { return true; }
        if (!IsValid(S)) { return false; }
        const auto* Commit = F.Commit->ContainerPtrToValuePtr<void>(S);
        const auto* Snap = F.CommitSnapshot->ContainerPtrToValuePtr<void>(Commit);
        FScriptArrayHelper Stations(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(Snap));
        const int32 Index = Action == 18 ? 3 : 4;
        if (Stations.Num() != 8 || !Live(F.ServerRoots[Index])) { return false; }
        const void* Row = Stations.GetRawPtr(Index);
        FScriptArrayHelper Attrs(F.Attributes, F.Attributes->ContainerPtrToValuePtr<void>(Row));
        if (Attrs.Num() != 1) { return false; }
        const void* Attr = Attrs.GetRawPtr(0);
        const auto* Part = Prop<FStructProperty>(F.Attribute->Struct,
            Action == 18 ? TEXT("Min") : TEXT("Current"));
        const void* PartData = Part->ContainerPtrToValuePtr<void>(Attr);
        const double Expected = Action == 18 ? 10.0 : 150.0;
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(Action == 18 ?
            TEXT("FloatAttribute.PowerLevel") : TEXT("FloatAttribute.Signal")), false);
        if (!Tag.IsValid() || !Flag(F.Component->Struct, PartData, TEXT("Present")))
        { return false; }
        const auto Handle = UCk_Utils_FloatAttribute_UE::TryGet(F.ServerRoots[Index], Tag);
        const auto Component = Action == 18 ? ECk_MinMaxCurrent::Min :
            ECk_MinMaxCurrent::Current;
        return Live(FCk_Handle{Handle}) &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(FCk_Handle{Handle}) ==
                F.ServerRoots[Index] &&
            UCk_Utils_FloatAttribute_UE::Has_Component(Handle, Component) &&
            FMath::Abs(Float(F.Component->Struct, PartData, TEXT("Base")) - Expected) < 1.0e-3 &&
            FMath::Abs(Float(F.Component->Struct, PartData, TEXT("Final")) - Expected) < 1.0e-3 &&
            FMath::Abs(double(UCk_Utils_FloatAttribute_UE::Get_BaseValue(Handle, Component)) -
                Expected) < 1.0e-3 &&
            FMath::Abs(double(UCk_Utils_FloatAttribute_UE::Get_FinalValue(Handle, Component)) -
                Expected) < 1.0e-3;
    }
    auto DirectAttribute(const FFixture& F, int32 Row, const TCHAR* Name)
        -> FCk_Handle_FloatAttribute
    {
        if (Row < 0 || Row >= 8 || !Live(F.ServerRoots[Row])) { return {}; }
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(Name), false);
        if (!Tag.IsValid()) { return {}; }
        const auto Handle = UCk_Utils_FloatAttribute_UE::TryGet(F.ServerRoots[Row], Tag);
        return Live(FCk_Handle{Handle}) &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(FCk_Handle{Handle}) ==
                F.ServerRoots[Row] &&
            UCk_Utils_FloatAttribute_UE::Has_Component(Handle, ECk_MinMaxCurrent::Current) ?
                Handle : FCk_Handle_FloatAttribute{};
    }
    auto DirectModifier(FCk_Handle_FloatAttribute Attribute, const TCHAR* Name)
        -> FCk_Handle_FloatAttributeModifier
    {
        if (!Live(FCk_Handle{Attribute}) ||
            !UCk_Utils_FloatAttribute_UE::Has_Component(
                Attribute, ECk_MinMaxCurrent::Current)) { return {}; }
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(Name), false);
        if (!Tag.IsValid()) { return {}; }
        const auto Modifier = UCk_Utils_FloatAttributeModifier_UE::TryGet(
            Attribute, Tag, ECk_MinMaxCurrent::Current);
        return Live(FCk_Handle{Modifier}) &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(FCk_Handle{Modifier}) ==
                FCk_Handle{Attribute} &&
            UCk_Utils_FloatAttributeModifier_UE::Has_Component(
                FCk_Handle{Modifier}, ECk_MinMaxCurrent::Current) ?
                Modifier : FCk_Handle_FloatAttributeModifier{};
    }
    auto SampleAttribute(const void* SnapshotData, const FFixture& F,
                         int32 Row, int32 AttributeIndex) -> const void*
    {
        FScriptArrayHelper Rows(F.Stations,
            F.Stations->ContainerPtrToValuePtr<void>(SnapshotData));
        if (Rows.Num() != 8) { return nullptr; }
        FScriptArrayHelper Attributes(F.Attributes,
            F.Attributes->ContainerPtrToValuePtr<void>(Rows.GetRawPtr(Row)));
        return AttributeIndex >= 0 && AttributeIndex < Attributes.Num() ?
            Attributes.GetRawPtr(AttributeIndex) : nullptr;
    }
    auto SampleBonus(const void* SnapshotData, const FFixture& F,
                     int32 Row, int32 AttributeIndex) -> double
    {
        const void* Attribute = SampleAttribute(SnapshotData, F, Row, AttributeIndex);
        if (!Attribute) { return std::numeric_limits<double>::quiet_NaN(); }
        const auto* Current = Prop<FStructProperty>(F.Attribute->Struct, TEXT("Current"));
        return Float(F.Component->Struct, Current->ContainerPtrToValuePtr<void>(Attribute),
            TEXT("Bonus"));
    }
    auto DirectAutoTimerStates(const FFixture& F, bool ExpectRunning) -> bool
    {
        for (int32 I = 0; I < 8; ++I)
        {
            if (!Live(F.ServerRoots[I])) { return false; }
            int32 Running = 0, Paused = 0, Total = 0;
            TArray<FCk_Handle> Seen;
            for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(
                F.ServerRoots[I]))
            {
                if (!Live(Child) || !UCk_Utils_Timer_UE::Has(Child)) { continue; }
                if (Seen.Contains(Child)) { return false; }
                Seen.Add(Child);
                const auto Timer = ck::StaticCast<FCk_Handle_Timer>(Child);
                if (UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) !=
                    F.ServerRoots[I]) { return false; }
                const auto State = UCk_Utils_Timer_UE::Get_CurrentState(Timer);
                Running += State == ECk_Timer_State::Running ? 1 : 0;
                Paused += State == ECk_Timer_State::Paused ? 1 : 0;
                Total++;
            }
            // Clamping owns Display + Update + Auto; every other station owns
            // Display + Auto. The direct-child census rejects foreign timers.
            const int32 ExpectedTotal = I == 1 ? 3 : 2;
            if (Total != ExpectedTotal ||
                Running != (ExpectRunning ? ExpectedTotal : 1) ||
                Paused != (ExpectRunning ? 0 : ExpectedTotal - 1)) { return false; }
        }
        return true;
    }
    auto ControlSemantics(AGameStateBase* S, const FFixture& F,
                          int32 Action, int32 Preset) -> bool
    {
        if (!IsValid(S) || !F.BeforeSnapshot.IsValid()) { return false; }
        const void* Commit = F.Commit->ContainerPtrToValuePtr<void>(S);
        const void* Published = F.CommitSnapshot->ContainerPtrToValuePtr<void>(Commit);
        if (!Typed(Published, F)) { return false; }
        FScriptArrayHelper Before(F.Stations,
            F.Stations->ContainerPtrToValuePtr<void>(F.BeforeSnapshot->GetStructMemory()));
        FScriptArrayHelper After(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(Published));
        if (Before.Num() != 8 || After.Num() != 8) { return false; }
        const auto* Station = F.Station->Struct.Get();
        int32 Target = INDEX_NONE;
        if (Action >= 3 && Action <= 10) { Target = Action - 3; }
        else if (Action == 12) { Target = 0; }
        else if (Action == 13) { Target = 2; }
        else if (Action == 14) { Target = 5; }
        else if (Action == 15) { Target = 6; }
        else if (Action == 16) { Target = 7; }
        else if (Action >= 18 && Action <= 20) { Target = 3; }
        else if (Action == 21) { Target = 4; }
        else if (Action == 23) { Target = 1; }
        bool AllEnabled = true;
        for (int32 I = 0; I < 8; ++I)
        { AllEnabled &= Flag(Station, Before.GetRawPtr(I), TEXT("AutoEnabled")); }
        for (int32 I = 0; I < 8; ++I)
        {
            const bool Was = Flag(Station, Before.GetRawPtr(I), TEXT("AutoEnabled"));
            bool Expected = Was;
            if (Action == 1) { Expected = !AllEnabled; }
            else if (Action >= 3 && Action <= 10 && I == Target) { Expected = !Was; }
            else if (Target == I && Action != 24) { Expected = false; }
            if (Flag(Station, After.GetRawPtr(I), TEXT("AutoEnabled")) != Expected)
            { return false; }
        }
        if (Action == 1 && AllEnabled && !DirectAutoTimerStates(F, false))
        { return false; }
        if (Action == 12)
        {
            const auto Attribute = DirectAttribute(F, 0, TEXT("FloatAttribute.Test"));
            if (!Live(FCk_Handle{Attribute})) { return false; }
            const auto Weapon = DirectModifier(Attribute, TEXT("Modifier.Weapon"));
            const auto Buff = DirectModifier(Attribute, TEXT("Modifier.Buff"));
            const bool HadPair = F.BeforeKeyPresent[0] && F.BeforeKeyPresent[1];
            const double BeforeBonus = SampleBonus(F.BeforeSnapshot->GetStructMemory(), F, 0, 0);
            const double AfterBonus = SampleBonus(Published, F, 0, 0);
            const double Expected = Preset == 0 ? BeforeBonus + (HadPair ? 0.0 : 35.75) : 0.0;
            if (Preset == 0)
            {
                if (!Live(FCk_Handle{Weapon}) || !Live(FCk_Handle{Buff}) ||
                    (HadPair && (Weapon != F.BeforeKeys[0] || Buff != F.BeforeKeys[1])) ||
                    FMath::Abs(UCk_Utils_FloatAttributeModifier_UE::Get_Delta(Weapon) - 20.5) > 1.0e-3 ||
                    FMath::Abs(UCk_Utils_FloatAttributeModifier_UE::Get_Delta(Buff) - 15.25) > 1.0e-3 ||
                    Int(Station, After.GetRawPtr(0), TEXT("ModifierCount")) < 2) { return false; }
            }
            else if (Live(FCk_Handle{Weapon}) || Live(FCk_Handle{Buff}) ||
                Live(FCk_Handle{F.BeforeKeys[0]}) || Live(FCk_Handle{F.BeforeKeys[1]}) ||
                Int(Station, After.GetRawPtr(0), TEXT("ModifierCount")) != 0) { return false; }
            if (!FMath::IsFinite(BeforeBonus) || !FMath::IsFinite(AfterBonus) ||
                FMath::Abs(AfterBonus - Expected) > 1.0e-3 ||
                FMath::Abs(UCk_Utils_FloatAttribute_UE::Get_BonusValue(Attribute) - AfterBonus) > 1.0e-3)
            { return false; }
        }
        if (Action == 13)
        {
            const auto Damage = DirectAttribute(F, 2, TEXT("FloatAttribute.Damage"));
            const auto Defense = DirectAttribute(F, 2, TEXT("FloatAttribute.Defense"));
            if (!Live(FCk_Handle{Damage}) || !Live(FCk_Handle{Defense})) { return false; }
            const auto Weapon = DirectModifier(Damage, TEXT("Modifier.Weapon"));
            const bool HadWeapon = F.BeforeKeyPresent[2];
            const double BeforeBonus = SampleBonus(F.BeforeSnapshot->GetStructMemory(), F, 2, 0);
            const double AfterBonus = SampleBonus(Published, F, 2, 0);
            const double Expected = Preset == 0 ? BeforeBonus + (HadWeapon ? 0.0 : 25.5) :
                (Preset == 1 ? BeforeBonus - (HadWeapon ? 25.5 : 0.0) : 0.0);
            if (Preset == 0)
            {
                if (!Live(FCk_Handle{Weapon}) ||
                    (HadWeapon && Weapon != F.BeforeKeys[2]) ||
                    FMath::Abs(UCk_Utils_FloatAttributeModifier_UE::Get_Delta(Weapon) - 25.5) > 1.0e-3)
                { return false; }
            }
            else if (Live(FCk_Handle{Weapon}) || Live(FCk_Handle{F.BeforeKeys[2]}))
            { return false; }
            if (Preset == 2 &&
                (Int(Station, After.GetRawPtr(2), TEXT("ModifierCount")) != 0 ||
                 FMath::Abs(SampleBonus(Published, F, 2, 1)) > 1.0e-3 ||
                 FMath::Abs(UCk_Utils_FloatAttribute_UE::Get_BonusValue(Defense)) > 1.0e-3))
            { return false; }
            if (!FMath::IsFinite(BeforeBonus) || !FMath::IsFinite(AfterBonus) ||
                FMath::Abs(AfterBonus - Expected) > 1.0e-3 ||
                FMath::Abs(UCk_Utils_FloatAttribute_UE::Get_BonusValue(Damage) - AfterBonus) > 1.0e-3)
            { return false; }
        }
        if (Action == 15)
        {
            const int32 BeforeCount = Int(Station, Before.GetRawPtr(6), TEXT("ModifierCount"));
            const int32 AfterCount = Int(Station, After.GetRawPtr(6), TEXT("ModifierCount"));
            const double BeforeBonus = SampleBonus(F.BeforeSnapshot->GetStructMemory(), F, 6, 0);
            const double AfterBonus = SampleBonus(Published, F, 6, 0);
            const auto Counter = DirectAttribute(F, 6, TEXT("FloatAttribute.Counter"));
            if (!Live(FCk_Handle{Counter}) ||
                AfterCount != (Preset == 2 ? 0 : BeforeCount + 1) ||
                !FMath::IsFinite(BeforeBonus) || !FMath::IsFinite(AfterBonus) ||
                FMath::Abs(AfterBonus - (Preset == 2 ? 0.0 :
                    BeforeBonus + (Preset == 0 ? 1.0 : -1.0))) > 1.0e-3 ||
                FMath::Abs(UCk_Utils_FloatAttribute_UE::Get_BonusValue(Counter) -
                    AfterBonus) > 1.0e-3) { return false; }
        }
        if (Action == 16 &&
            (Int(Station, After.GetRawPtr(7), TEXT("TotalAttributes")) != (Preset == 0 ? 5 : 3) ||
             Int(Station, After.GetRawPtr(7), TEXT("CombatAttributes")) != (Preset == 0 ? 2 : 0)))
        { return false; }
        if ((Action == 16 || (Action == 24 && Preset == 1)) && F.HadCombatBefore)
        {
            if (Action == 16 && Preset == 0)
            {
                for (int32 I = 0; I < 2; ++I)
                {
                    const auto Tag = FGameplayTag::RequestGameplayTag(
                        FName(I == 0 ? TEXT("Combat.Attack") : TEXT("Combat.Defense")), false);
                    if (!Tag.IsValid() || !Live(F.BeforeCombat[I]) ||
                        FCk_Handle{UCk_Utils_FloatAttribute_UE::TryGet(F.ServerRoots[7], Tag)} !=
                            F.BeforeCombat[I]) { return false; }
                }
            }
            else if (Live(F.BeforeCombat[0]) || Live(F.BeforeCombat[1])) { return false; }
        }
        if (Action == 14 && Preset == 0 &&
            Flag(Station, Before.GetRawPtr(5), TEXT("DesiredEnergyRefillRunning")) ==
            Flag(Station, After.GetRawPtr(5), TEXT("DesiredEnergyRefillRunning")))
        { return false; }
        if (Action == 14 && Preset > 0)
        {
            const int32 AttributeIndex = Preset == 1 ? 0 : 1;
            const double Raw = Preset == 1 ? 10.0 : 5.0;
            const auto Attribute = DirectAttribute(F, 5,
                Preset == 1 ? TEXT("FloatAttribute.Energy") : TEXT("FloatAttribute.Mana"));
            const void* Sample = SampleAttribute(Published, F, 5, AttributeIndex);
            if (!Sample || !Live(FCk_Handle{Attribute}) ||
                !UCk_Utils_FloatAttribute_UE::Has_Component(Attribute, ECk_MinMaxCurrent::Max) ||
                FMath::Abs(Float(F.Attribute->Struct, Sample, TEXT("PreClampFinal")) - Raw) > 1.0e-3)
            { return false; }
            const auto* Current = Prop<FStructProperty>(F.Attribute->Struct, TEXT("Current"));
            const void* CurrentData = Current->ContainerPtrToValuePtr<void>(Sample);
            const double Committed = Float(F.Component->Struct, CurrentData, TEXT("Final"));
            const double LiveFinal = UCk_Utils_FloatAttribute_UE::Get_FinalValue(
                Attribute, ECk_MinMaxCurrent::Current);
            const double LiveMax = UCk_Utils_FloatAttribute_UE::Get_FinalValue(
                Attribute, ECk_MinMaxCurrent::Max);
            const bool LiveWithinRefillPolicy = Preset == 1 ?
                LiveFinal >= Raw - 1.0e-3 :
                LiveFinal >= -1.0e-3 && LiveFinal <= Raw + 1.0e-3;
            if (FMath::Abs(Committed - Raw) > 1.0e-3 ||
                !FMath::IsFinite(LiveFinal) || !FMath::IsFinite(LiveMax) ||
                !LiveWithinRefillPolicy || LiveFinal > LiveMax + 1.0e-3 ||
                (Preset == 2 &&
                    (!Flag(Station, After.GetRawPtr(5), TEXT("DesiredManaRefillRunning")) ||
                     !Flag(Station, After.GetRawPtr(5), TEXT("ManaRefillRunning")))))
            { return false; }
        }
        if (Action >= 18 && Action <= 21)
        {
            constexpr double Presets[4][4] = {
                {10.0, 25.5, 60.0, 220.0}, {200.0, 175.75, 90.0, 5.0},
                {100.0, 142.3, 250.0, -40.0}, {150.0, 0.0, 300.0, -50.0}};
            if (Preset < 0 || Preset >= 4) { return false; }
            const int32 Row = Action == 21 ? 4 : 3;
            const void* StationData = After.GetRawPtr(Row);
            FScriptArrayHelper Attributes(F.Attributes,
                F.Attributes->ContainerPtrToValuePtr<void>(StationData));
            if (Attributes.Num() != 1 || !Live(F.ServerRoots[Row])) { return false; }
            const void* AttributeData = Attributes.GetRawPtr(0);
            const auto Tag = FGameplayTag::RequestGameplayTag(
                FName(Action == 21 ? TEXT("FloatAttribute.Signal") :
                    TEXT("FloatAttribute.PowerLevel")), false);
            if (!Tag.IsValid()) { return false; }
            const auto Handle = UCk_Utils_FloatAttribute_UE::TryGet(F.ServerRoots[Row], Tag);
            const auto Part = Action == 18 ? ECk_MinMaxCurrent::Min :
                (Action == 19 ? ECk_MinMaxCurrent::Max : ECk_MinMaxCurrent::Current);
            if (!Live(FCk_Handle{Handle}) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(FCk_Handle{Handle}) !=
                    F.ServerRoots[Row] ||
                !UCk_Utils_FloatAttribute_UE::Has_Component(Handle, Part)) { return false; }
            const double Raw = Presets[Action - 18][Preset];
            const auto* PartProperty = Prop<FStructProperty>(F.Attribute->Struct,
                Action == 18 ? TEXT("Min") : (Action == 19 ? TEXT("Max") : TEXT("Current")));
            const void* PartData = PartProperty->ContainerPtrToValuePtr<void>(AttributeData);
            if (!Flag(F.Component->Struct, PartData, TEXT("Present"))) { return false; }
            const double Observed = Action < 20 ? Float(F.Component->Struct, PartData, TEXT("Final")) :
                Float(F.Attribute->Struct, AttributeData, TEXT("PreClampFinal"));
            const double LiveValue = Action < 20 ?
                UCk_Utils_FloatAttribute_UE::Get_FinalValue(Handle, Part) :
                UCk_Utils_FloatAttribute_UE::Get_PreClampFinalValue(Handle);
            if (FMath::Abs(Observed - Raw) > 1.0e-3 ||
                FMath::Abs(LiveValue - Raw) > 1.0e-3) { return false; }
        }
        if (Action == 23)
        {
            const void* Old = Before.GetRawPtr(1);
            const void* New = After.GetRawPtr(1);
            if (Int(Station, New, TEXT("BoundaryEventSerial")) !=
                Int(Station, Old, TEXT("BoundaryEventSerial")) + 1) { return false; }
            auto* Events = Prop<FArrayProperty>(Station, TEXT("ClampEvents"));
            auto* Event = CastField<FStructProperty>(Events ? Events->Inner : nullptr);
            if (!Event) { return false; }
            FScriptArrayHelper OldEvents(Events, Events->ContainerPtrToValuePtr<void>(Old));
            FScriptArrayHelper NewEvents(Events, Events->ContainerPtrToValuePtr<void>(New));
            if (OldEvents.Num() != 4 || NewEvents.Num() != 4) { return false; }
            FScriptArrayHelper OldAttrs(F.Attributes,
                F.Attributes->ContainerPtrToValuePtr<void>(Old));
            FScriptArrayHelper NewAttrs(F.Attributes,
                F.Attributes->ContainerPtrToValuePtr<void>(New));
            if (OldAttrs.Num() != 4 || NewAttrs.Num() != 4) { return false; }
            for (int32 I = 0; I < 4; ++I)
            {
                const void* E = NewEvents.GetRawPtr(I);
                const void* OldAttr = OldAttrs.GetRawPtr(I);
                const void* NewAttr = NewAttrs.GetRawPtr(I);
                const auto* Attribute = F.Attribute->Struct.Get();
                const auto* Current = Prop<FStructProperty>(Attribute, TEXT("Current"));
                const void* OldCurrent = Current->ContainerPtrToValuePtr<void>(OldAttr);
                const void* NewCurrent = Current->ContainerPtrToValuePtr<void>(NewAttr);
                bool MustFire = false;
                for (const auto* BoundName : {TEXT("Min"), TEXT("Max")})
                {
                    const auto* Bound = Prop<FStructProperty>(Attribute, BoundName);
                    const void* OldBound = Bound->ContainerPtrToValuePtr<void>(OldAttr);
                    const void* NewBound = Bound->ContainerPtrToValuePtr<void>(NewAttr);
                    const bool Changed =
                        Float(F.Component->Struct, OldCurrent, TEXT("Base")) !=
                            Float(F.Component->Struct, NewCurrent, TEXT("Base")) ||
                        Float(F.Component->Struct, OldCurrent, TEXT("Final")) !=
                            Float(F.Component->Struct, NewCurrent, TEXT("Final")) ||
                        Float(F.Component->Struct, OldBound, TEXT("Base")) !=
                            Float(F.Component->Struct, NewBound, TEXT("Base")) ||
                        Float(F.Component->Struct, OldBound, TEXT("Final")) !=
                            Float(F.Component->Struct, NewBound, TEXT("Final"));
                    MustFire |= Changed &&
                        Float(F.Component->Struct, NewCurrent, TEXT("Final")) ==
                            Float(F.Component->Struct, NewBound, TEXT("Final"));
                }
                if (Int(Event->Struct, E, TEXT("AttributeIndex")) != I ||
                    Int(Event->Struct, E, TEXT("Serial")) <
                        Int(Event->Struct, OldEvents.GetRawPtr(I), TEXT("Serial")) ||
                    (MustFire && Int(Event->Struct, E, TEXT("Serial")) <=
                        Int(Event->Struct, OldEvents.GetRawPtr(I), TEXT("Serial"))) ||
                    !FMath::IsFinite(Float(Event->Struct, E, TEXT("PreClampFinal"))) ||
                    !FMath::IsFinite(Float(Event->Struct, E, TEXT("Final"))) ||
                    !FMath::IsFinite(Float(Event->Struct, E, TEXT("Overflow"))) ||
                    FMath::Abs(Float(Event->Struct, E, TEXT("PreClampFinal")) -
                        Float(Event->Struct, E, TEXT("Final")) -
                        Float(Event->Struct, E, TEXT("Overflow"))) > 1.0e-3) { return false; }
            }
        }
        if (Action == 24)
        {
            for (int32 I = 0; I < 8; ++I)
            {
                if (Preset == 0 && I != 1)
                {
                    // Auto timers are paused for the transaction; independent Refill may evolve.
                    if (I != 5 && !F.Station->Identical(Before.GetRawPtr(I),
                        After.GetRawPtr(I), 0)) { return false; }
                }
                else if (Int(Station, After.GetRawPtr(I), TEXT("AutoStep")) != 0)
                { return false; }
            }
            if (Preset == 1)
            {
                FScriptArrayHelper Counter(F.Attributes,
                    F.Attributes->ContainerPtrToValuePtr<void>(After.GetRawPtr(6)));
                if (Counter.Num() != 1) { return false; }
                const auto* Current = Prop<FStructProperty>(F.Attribute->Struct, TEXT("Current"));
                if (FMath::Abs(Float(F.Component->Struct,
                    Current->ContainerPtrToValuePtr<void>(Counter.GetRawPtr(0)),
                    TEXT("Base")) - 25.0) > 1.0e-3) { return false; }
            }
        }
        return true;
    }
    auto PermanentIncrementObserved(FFixture& F) -> bool
    {
        auto* Server = State(ck::auto_test::net::Get_ServerWorld(), F);
        if (!Ready(Server, F, F.InitialGeneration) || !Remember(F, Server) ||
            !Live(F.ServerRoots[6])) { return false; }
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("FloatAttribute.Counter")), false);
        if (!Tag.IsValid()) { return false; }
        const auto Handle = UCk_Utils_FloatAttribute_UE::TryGet(F.ServerRoots[6], Tag);
        if (!Live(FCk_Handle{Handle}) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(FCk_Handle{Handle}) !=
                F.ServerRoots[6] ||
            !UCk_Utils_FloatAttribute_UE::Has_Component(Handle, ECk_MinMaxCurrent::Current) ||
            UCk_Utils_FloatAttribute_UE::Get_BaseValue(Handle, ECk_MinMaxCurrent::Current) <=
                25.0f) { return false; }
        FScriptArrayHelper Rows(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(Data(Server, F)));
        FScriptArrayHelper Attrs(F.Attributes,
            F.Attributes->ContainerPtrToValuePtr<void>(Rows.GetRawPtr(6)));
        const auto* Current = Prop<FStructProperty>(F.Attribute->Struct, TEXT("Current"));
        const double Base = Float(F.Component->Struct,
            Current->ContainerPtrToValuePtr<void>(Attrs.GetRawPtr(0)), TEXT("Base"));
        const bool bPublishedIncrement =
            Int(F.Station->Struct, Rows.GetRawPtr(6), TEXT("AutoStep")) >= 1 &&
            Base > 25.0 && FMath::Abs(Base -
                UCk_Utils_FloatAttribute_UE::Get_BaseValue(
                    Handle, ECk_MinMaxCurrent::Current)) < 1.0e-3;
        if (F.NaturalIncrementFloorSequence == 0)
        {
            if (!bPublishedIncrement) { return false; }
            F.NaturalIncrementFloorSequence = Seq(Server, F);
        }
        auto* C0 = State(ck::auto_test::net::Get_ClientWorld(0), F);
        auto* C1 = State(ck::auto_test::net::Get_ClientWorld(1), F);
        if (!IsValid(C0) || !IsValid(C1) ||
            Seq(C0, F) < F.NaturalIncrementFloorSequence ||
            Seq(C1, F) < F.NaturalIncrementFloorSequence ||
            !Observed(C0, F) || !Observed(C1, F))
        { return false; }
        auto ClientBase = [&F](AGameStateBase* Client) -> double
        {
            FScriptArrayHelper CRows(F.Stations,
                F.Stations->ContainerPtrToValuePtr<void>(Data(Client, F)));
            FScriptArrayHelper CAttrs(F.Attributes,
                F.Attributes->ContainerPtrToValuePtr<void>(CRows.GetRawPtr(6)));
            const auto* CCurrent = Prop<FStructProperty>(F.Attribute->Struct, TEXT("Current"));
            return Float(F.Component->Struct,
                CCurrent->ContainerPtrToValuePtr<void>(CAttrs.GetRawPtr(0)), TEXT("Base"));
        };
        return ClientBase(C0) > 25.0 && ClientBase(C1) > 25.0;
    }
    auto WorldReady(UWorld* W, const FFixture& F,
                    int32 G, bool Authority, bool Dedicated) -> bool
    {
        auto* S = State(W, F);
        if (!Ready(S, F, G) || !Typed(Data(S, F), F) ||
            !Branches(W, F, Authority, Dedicated)) { return false; }
        if (Authority && Dedicated)
        { return !F.LocalReady->GetPropertyValue_InContainer(S); }
        return F.LocalGeneration->GetPropertyValue_InContainer(S) == G &&
            F.LocalReady->GetPropertyValue_InContainer(S) &&
            !F.LocalFailed->GetPropertyValue_InContainer(S);
    }
    auto CatalogComplete(UWorld* W, const FFixture& F) -> bool
    {
        auto* ServerWorld = ck::auto_test::net::Get_ServerWorld();
        auto* ServerState = IsValid(ServerWorld) ?
            ServerWorld->GetGameState<ACkGym_GameState_UE>() : nullptr;
        auto* LocalState = IsValid(W) ? W->GetGameState<ACkGym_GameState_UE>() : nullptr;
        auto* ServerInstance = IsValid(ServerWorld) ? ServerWorld->GetGameInstance() : nullptr;
        auto* LocalInstance = IsValid(W) ? W->GetGameInstance() : nullptr;
        auto* ServerRegistry = IsValid(ServerInstance) ?
            ServerInstance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
        auto* LocalRegistry = IsValid(LocalInstance) ?
            LocalInstance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
        if (!IsValid(ServerState) || !ServerState->Get_HasPublishedCatalog() ||
            !IsValid(LocalState) || !LocalState->Get_HasPublishedCatalog() ||
            !IsValid(ServerRegistry) || !IsValid(LocalRegistry)) { return false; }
        const auto& Expected = ServerRegistry->Get_Entries();
        const auto& Entries = LocalRegistry->Get_Entries();
        const int32 FloatIndex = ServerRegistry->Find_GymIndexByName(TEXT("Attribute Float"));
        if (Expected.IsEmpty() || FloatIndex == INDEX_NONE ||
            !Expected.IsValidIndex(FloatIndex) || Entries.Num() != Expected.Num() ||
            ServerRegistry->Get_CurrentGymIndex() != FloatIndex ||
            LocalRegistry->Get_CurrentGymIndex() != FloatIndex ||
            Expected[FloatIndex].DisplayName != TEXT("Attribute Float") ||
            Expected[FloatIndex].GameModeClass.Get() != F.Mode)
        { return false; }
        int32 FloatEntries = 0;
        for (int32 I = 0; I < Expected.Num(); ++I)
        {
            const auto& Authoritative = Expected[I];
            const auto& Replicated = Entries[I];
            FloatEntries += Authoritative.DisplayName == TEXT("Attribute Float") ? 1 : 0;
            if (Authoritative.DisplayName != Replicated.DisplayName ||
                Authoritative.GameModeClass.Get() != Replicated.GameModeClass.Get() ||
                Authoritative.LevelName != Replicated.LevelName ||
                Authoritative.Category != Replicated.Category ||
                Authoritative.HintCode != Replicated.HintCode)
            { return false; }
        }
        return FloatEntries == 1;
    }
    auto AllReady(const FFixture& F, bool Dedicated) -> bool
    {
        auto* S = State(ck::auto_test::net::Get_ServerWorld(), F);
        if (!(F.InitialGeneration > 0 &&
            ck::auto_test::net::Get_AllPIEWorlds().Num() == 3 &&
            CatalogComplete(ck::auto_test::net::Get_ServerWorld(), F) &&
            CatalogComplete(ck::auto_test::net::Get_ClientWorld(0), F) &&
            CatalogComplete(ck::auto_test::net::Get_ClientWorld(1), F) &&
            WorldReady(ck::auto_test::net::Get_ServerWorld(), F,
                F.InitialGeneration, true, Dedicated) &&
            WorldReady(ck::auto_test::net::Get_ClientWorld(0), F,
                F.InitialGeneration, false, Dedicated) &&
            WorldReady(ck::auto_test::net::Get_ClientWorld(1), F,
                F.InitialGeneration, false, Dedicated) &&
            F.ServerChildCount->GetPropertyValue_InContainer(S) == 8))
        { return false; }
        FScriptArrayHelper Handles(F.ServerChildren,
            F.ServerChildren->ContainerPtrToValuePtr<void>(S));
        if (Handles.Num() != 8) { return false; }
        TArray<FCk_Handle> Current;
        if (!Branches(ck::auto_test::net::Get_ServerWorld(), F,
            true, Dedicated, &Current) || Current.Num() != 8) { return false; }
        for (int32 I = 0; I < 8; ++I)
        {
            const auto& Reflected = *reinterpret_cast<const FCk_Handle*>(Handles.GetRawPtr(I));
            if (!Live(Reflected) || Reflected != Current[I] ||
                (F.RootsCaptured && Current[I] != F.ServerRoots[I])) { return false; }
        }
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkAttributeFloatGym_Network,
    "Ck.Gym.Network.AttributeFloat.SharedEight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
        EAutomationTestFlags::EngineFilter)

bool FCkAttributeFloatGym_Network::RunTest(const FString& Parameters)
{
    using namespace ck_attribute_float_network_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Attribute Float network topology")); return false; }
    const auto F = MakeShared<FFixture>();
    if (!Bind(*F, *this)) { return false; }
    F->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, F->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            const int32 G = Gen(S, *F);
            if (G <= 0 || !WorldReady(Server, *F, G, true, Dedicated) ||
                ck::auto_test::net::Get_AllPIEWorlds().Num() != 3 ||
                !WorldReady(ck::auto_test::net::Get_ClientWorld(0), *F, G, false, Dedicated) ||
                !WorldReady(ck::auto_test::net::Get_ClientWorld(1), *F, G, false, Dedicated) ||
                !IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)) ||
                !IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1)))
            { return false; }
            F->InitialGeneration = G;
            return Remember(*F, S) && !F->TransactionViolation &&
                Observed(State(ck::auto_test::net::Get_ClientWorld(0), *F), *F) &&
                Observed(State(ck::auto_test::net::Get_ClientWorld(1), *F), *F) &&
                AllReady(*F, Dedicated);
        }), 60.0, TEXT("eight authority Float demos and both pure client projection sets")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F, Dedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* W = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(W, *F);
            TArray<FCk_Handle> Roots;
            if (!TestTrue(TEXT("Float initial eight world-owned scripts and typed snapshot"),
                AllReady(*F, Dedicated) && Branches(W, *F, true, Dedicated, &Roots) &&
                Roots.Num() == 8 && W->GetNetMode() ==
                    (Dedicated ? NM_DedicatedServer : NM_ListenServer) &&
                IsValid(W->GetAuthGameMode()) && W->GetAuthGameMode()->IsA(F->Mode)))
            { return false; }
            for (int32 I = 0; I < 8; ++I) { F->ServerRoots[I] = Roots[I]; }
            F->RootsCaptured = true;
            return TestTrue(TEXT("both remote owners start without accepted controls"),
                F->ServerChildCount->GetPropertyValue_InContainer(S) == 8 &&
                Capture(*F) && F->BeforeAccepted[0] == 0 && F->BeforeAccepted[1] == 0);
        }), TEXT("capture Float eight-station authority ownership")));

    auto QueueAccepted = [this, F, Dedicated](int32 Owner, int32 Serial,
                                              int32 Action, int32 Preset,
                                              const TCHAR* Label)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Owner,
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, F, Serial, Action, Preset](UWorld* W)
                {
                    if (!TestTrue(TEXT("capture complete Float pre-control publication"),
                        Capture(*F))) { return; }
                    F->HadCombatBefore = false;
                    for (int32 I = 0; I < 3; ++I)
                    { F->BeforeKeys[I] = {}; F->BeforeKeyPresent[I] = false; }
                    if (Action == 12)
                    {
                        const auto Attribute = DirectAttribute(*F, 0,
                            TEXT("FloatAttribute.Test"));
                        if (!Live(FCk_Handle{Attribute})) { return; }
                        F->BeforeKeys[0] = DirectModifier(Attribute, TEXT("Modifier.Weapon"));
                        F->BeforeKeys[1] = DirectModifier(Attribute, TEXT("Modifier.Buff"));
                        F->BeforeKeyPresent[0] = Live(FCk_Handle{F->BeforeKeys[0]});
                        F->BeforeKeyPresent[1] = Live(FCk_Handle{F->BeforeKeys[1]});
                        if (Live(FCk_Handle{F->BeforeKeys[0]}) !=
                            Live(FCk_Handle{F->BeforeKeys[1]})) { return; }
                    }
                    if (Action == 13)
                    {
                        const auto Attribute = DirectAttribute(*F, 2,
                            TEXT("FloatAttribute.Damage"));
                        if (!Live(FCk_Handle{Attribute})) { return; }
                        F->BeforeKeys[2] = DirectModifier(Attribute, TEXT("Modifier.Weapon"));
                        F->BeforeKeyPresent[2] = Live(FCk_Handle{F->BeforeKeys[2]});
                    }
                    if (Action == 16 || (Action == 24 && Preset == 1))
                    {
                        FScriptArrayHelper Rows(F->Stations,
                            F->Stations->ContainerPtrToValuePtr<void>(
                                F->BeforeSnapshot->GetStructMemory()));
                        if (Rows.Num() != 8) { return; }
                        const int32 Count = Int(F->Station->Struct, Rows.GetRawPtr(7),
                            TEXT("TotalAttributes"));
                        if (Count != 3 && Count != 5) { return; }
                        F->HadCombatBefore = Count == 5;
                        if (F->HadCombatBefore)
                        {
                            for (int32 I = 0; I < 2; ++I)
                            {
                                const auto Tag = FGameplayTag::RequestGameplayTag(
                                    FName(I == 0 ? TEXT("Combat.Attack") :
                                        TEXT("Combat.Defense")), false);
                                if (!Tag.IsValid() || !Live(F->ServerRoots[7])) { return; }
                                F->BeforeCombat[I] = FCk_Handle{
                                    UCk_Utils_FloatAttribute_UE::TryGet(F->ServerRoots[7], Tag)};
                                if (!Live(F->BeforeCombat[I])) { return; }
                            }
                        }
                    }
                    TestTrue(TEXT("invoke actual owning remote Float RPC"),
                        Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr,
                            *F, Serial, Action, Preset));
                })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda(
                [F, Dedicated, Owner, Serial, Action, Preset]() -> bool
                {
                    auto* Server = ck::auto_test::net::Get_ServerWorld();
                    auto* S = State(Server, *F);
                    if (!Ready(S, *F, F->InitialGeneration)) { return false; }
                    if (!Remember(*F, S)) { return false; }
                    if (Seq(S, *F) == F->BeforeSequence && F->BeforeSnapshot.IsValid() &&
                        !F->Snapshot->Identical(Data(S, *F),
                            F->BeforeSnapshot->GetStructMemory(), 0))
                    { F->TransactionViolation = true; }
                    auto* PC = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, Owner);
                    if (!IsValid(PC) || !PC->IsA(F->Controller)) { return false; }
                    if (F->Attempt->GetPropertyValue_InContainer(PC) == Serial &&
                        F->Result->GetPropertyValue_InContainer(PC) == 1 &&
                        !CommitMatches(S, *F, Serial, Action, Preset))
                    { F->TransactionViolation = true; }
                    auto* C0 = ck::auto_test::net::Get_ClientWorld(0);
                    auto* C1 = ck::auto_test::net::Get_ClientWorld(1);
                    auto* Owning = Owner == 0 ? C0 : C1;
                    auto* Other = Owner == 0 ? C1 : C0;
                    auto* OwnerPC = IsValid(Owning) ? Owning->GetFirstPlayerController() : nullptr;
                    auto* OtherPC = IsValid(Other) ? Other->GetFirstPlayerController() : nullptr;
                    if (F->TransactionViolation || !IsValid(OwnerPC) || !IsValid(OtherPC) ||
                        !OwnerPC->IsA(F->Controller) || !OtherPC->IsA(F->Controller) ||
                        F->Attempt->GetPropertyValue_InContainer(OwnerPC) != Serial ||
                        F->Accepted->GetPropertyValue_InContainer(OwnerPC) != Serial ||
                        F->Result->GetPropertyValue_InContainer(OwnerPC) != 1 ||
                        F->Accepted->GetPropertyValue_InContainer(OtherPC) !=
                            F->BeforeAccepted[1 - Owner] ||
                        F->Attempt->GetPropertyValue_InContainer(OtherPC) !=
                            F->BeforeAttempt[1 - Owner] ||
                        F->Result->GetPropertyValue_InContainer(OtherPC) !=
                            F->BeforeResult[1 - Owner] ||
                        !CommitMatches(S, *F, Serial, Action, Preset) ||
                        !CommitMatches(State(C0, *F), *F, Serial, Action, Preset) ||
                        !CommitMatches(State(C1, *F), *F, Serial, Action, Preset) ||
                        !ControlSemantics(S, *F, Action, Preset) ||
                        !AllReady(*F, Dedicated)) { return false; }
                    for (int32 R = 0; R < 10; ++R)
                    {
                        int32 Expected = F->BeforeRings[Owner][R];
                        if (RingActions[R] == Action) { Expected = Preset; }
                        if (Action == 24 && Preset == 1 && R >= 5 && R <= 8)
                        { Expected = -1; }
                        if (F->Rings[R]->GetPropertyValue_InContainer(OwnerPC) != Expected ||
                            F->Rings[R]->GetPropertyValue_InContainer(OtherPC) !=
                                F->BeforeRings[1 - Owner][R]) { return false; }
                    }
                    return true;
                }), 25.0, Label));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
            FCk_NetAutoTest_Assertion::CreateLambda([this, F, Label]() -> bool
            { return TestFalse(Label, F->TransactionViolation); }), Label));
    };
    QueueAccepted(0, 100, 1, -1, TEXT("remote owner pauses all eight Float stations"));
    QueueAccepted(0, 101, 21, 0, TEXT("owner zero commits Float signal current preset zero"));
    QueueAccepted(1, 200, 18, 0, TEXT("owner one independently commits Float min preset zero"));
    auto QueueRejected = [this, F, Dedicated](int32 Owner, int32 Serial,
                                              int32 Action, int32 Preset,
                                              const TCHAR* Label)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Owner,
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, F, Serial, Action, Preset](UWorld* W)
                {
                    if (!TestTrue(TEXT("capture complete Float pre-rejection state"),
                        Capture(*F))) { return; }
                    TestTrue(TEXT("invoke malformed Float request through owning RPC"),
                        Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr,
                            *F, Serial, Action, Preset));
                })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda(
                [F, Dedicated, Owner, Serial]() -> bool
                {
                    auto* Server = ck::auto_test::net::Get_ServerWorld();
                    auto* S = State(Server, *F);
                    auto* Owning = ck::auto_test::net::Get_ClientWorld(Owner);
                    auto* PC = IsValid(Owning) ? Owning->GetFirstPlayerController() : nullptr;
                    auto* ServerPC = ck::auto_test::net::TryGet_RemoteClientPlayerController(
                        Server, Owner);
                    if (!AllReady(*F, Dedicated) || !IsValid(PC) ||
                        !PC->IsA(F->Controller) || !IsValid(ServerPC) ||
                        !ServerPC->IsA(F->Controller)) { return false; }
                    if (F->Attempt->GetPropertyValue_InContainer(ServerPC) == Serial &&
                        F->Result->GetPropertyValue_InContainer(ServerPC) != 4)
                    { F->TransactionViolation = true; }
                    return !F->TransactionViolation &&
                        F->Attempt->GetPropertyValue_InContainer(PC) == Serial &&
                        F->Result->GetPropertyValue_InContainer(PC) == 4 &&
                        F->Accepted->GetPropertyValue_InContainer(PC) ==
                            F->BeforeAccepted[Owner] &&
                        F->SignalRing->GetPropertyValue_InContainer(PC) ==
                            F->BeforeSignalRing[Owner] &&
                        F->MinRing->GetPropertyValue_InContainer(PC) ==
                            F->BeforeMinRing[Owner] &&
                        Gen(S, *F) == F->InitialGeneration &&
                        RejectedStationsUnchanged(S, *F) &&
                        F->CommitSerial->GetPropertyValue_InContainer(
                            F->Commit->ContainerPtrToValuePtr<void>(S)) ==
                                F->BeforeCommitSerial &&
                        [&]() -> bool {
                            for (int32 I = 0; I < 8; ++I)
                            { if (!Live(F->ServerRoots[I])) { return false; } }
                            return true;
                        }();
                }), 15.0, Label));
    };
    // Every authored active row uses a real owning client and a complete committed snapshot.
    // Toggle each auto timer twice, leaving the cohort paused for deterministic manual controls.
    QueueAccepted(0, 102, 1, -1, TEXT("global Float auto resumes all eight"));
    QueueAccepted(0, 103, 1, -1, TEXT("global Float auto pauses all eight"));
    for (int32 I = 0; I < 8; ++I)
    {
        QueueAccepted(0, 104 + 2 * I, 3 + I, -1, TEXT("individual Float auto starts"));
        QueueAccepted(0, 105 + 2 * I, 3 + I, -1, TEXT("individual Float auto stops"));
    }
    QueueAccepted(0, 120, 24, 0, TEXT("Float local R resets Clamping after auto exposure"));
    QueueAccepted(0, 121, 24, 1, TEXT("Float full R establishes settled manual baseline"));
    QueueAccepted(0, 122, 12, 0, TEXT("Float Values add exact modifier pair"));
    QueueAccepted(0, 123, 12, 1, TEXT("Float Values clear modifier pair"));
    QueueAccepted(0, 124, 13, 0, TEXT("Float Modifiers add keyed weapon"));
    QueueAccepted(0, 125, 13, 1, TEXT("Float Modifiers revoke weapon"));
    QueueAccepted(0, 126, 13, 2, TEXT("Float Modifiers clear all"));
    QueueAccepted(0, 127, 14, 0, TEXT("Float Refill toggle desired Energy refill"));
    QueueAccepted(0, 128, 14, 1, TEXT("Float Refill drain Energy"));
    QueueAccepted(0, 129, 14, 2, TEXT("Float Refill drain Mana"));
    QueueAccepted(0, 130, 15, 0, TEXT("Float IncDec increment"));
    QueueAccepted(0, 131, 15, 1, TEXT("Float IncDec decrement"));
    QueueAccepted(0, 132, 15, 2, TEXT("Float IncDec revoke both"));
    QueueAccepted(0, 133, 15, 0, TEXT("Float IncDec repeated increment"));
    QueueAccepted(0, 134, 16, 0, TEXT("Float Multiple admits combat pair"));
    QueueAccepted(1, 201, 16, 0, TEXT("other owner repeats M without replacing combat pair"));
    QueueAccepted(0, 135, 16, 1, TEXT("Float Multiple retires combat pair"));
    QueueAccepted(1, 202, 16, 1, TEXT("other owner repeats U after combat retirement"));
    QueueAccepted(1, 203, 18, 1, TEXT("remote min fractional preset"));
    QueueAccepted(1, 204, 18, 2, TEXT("remote min sixty preset"));
    QueueAccepted(1, 205, 19, 0, TEXT("Float max two hundred preset"));
    QueueAccepted(1, 206, 19, 1, TEXT("Float max fractional preset"));
    QueueAccepted(1, 207, 19, 2, TEXT("Float max ninety preset"));
    QueueAccepted(1, 208, 20, 0, TEXT("Float Current one hundred preset"));
    QueueAccepted(1, 209, 20, 1, TEXT("Float Current fractional preset"));
    QueueAccepted(1, 210, 20, 2, TEXT("Float Current upper clamp preset"));
    QueueAccepted(1, 211, 20, 3, TEXT("Float Current lower clamp preset"));
    QueueAccepted(0, 136, 21, 0, TEXT("Float Signal initial preset after full R"));
    QueueAccepted(0, 137, 21, 1, TEXT("Float Signal zero preset advances owner ring"));
    QueueAccepted(0, 138, 21, 2, TEXT("Float Signal upper clamp preset"));
    QueueAccepted(0, 139, 21, 3, TEXT("Float Signal lower clamp preset"));
    QueueAccepted(0, 140, 23, -1, TEXT("Float B publishes boundary and clamp events"));
    QueueAccepted(0, 141, 24, 0, TEXT("Float R resets only Clamping values"));
    QueueAccepted(0, 142, 24, 1, TEXT("Float R resets all values and preserves auto leases"));
    QueueAccepted(0, 143, 1, -1, TEXT("Float auto resumes after retained full reset"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        { return AllReady(*F, Dedicated) && DirectAutoTimerStates(*F, true) &&
            PermanentIncrementObserved(*F); }),
        18.0, TEXT("natural permanent Float increment reaches both client snapshots")));
    // Natural auto movement is already observed on both clients. Settle the real
    // producer before comparing exact station state across rejected RPCs.
    QueueAccepted(0, 144, 1, -1, TEXT("Float auto pauses after natural increment proof"));
    QueueRejected(0, 0, 21, 1, TEXT("zero Float serial cannot mutate accepted state"));
    QueueRejected(1, 220, 99, -1, TEXT("unsupported Float action cannot mutate accepted state"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            F->RestoreSettings();
            return TestTrue(TEXT("Float fixture drains PIE and restores startup settings"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
        }), TEXT("Float PIE and settings cleanup"), true));
    return true;
}

#endif
