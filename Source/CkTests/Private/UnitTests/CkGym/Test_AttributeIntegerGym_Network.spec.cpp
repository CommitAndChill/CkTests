#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_NetworkRuntime.h"
#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkLabel/CkLabel_Utils.h"
#include "CkMessaging/CkMessaging_Utils.h"
#include "CkMessaging/CkMessaging_Fragment.h"
#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Utils.h"
#include "CkTimer/CkTimer_Utils.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"

#include <Engine/GameInstance.h>
#include <Engine/World.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/PlayerController.h>
#include <GameplayTagContainer.h>
#include <HAL/PlatformTime.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <StructUtils/InstancedStruct.h>
#include <UObject/UnrealType.h>
#include <UObject/UObjectGlobals.h>

namespace ck_attribute_integer_network_test
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_IntegerAttributeGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_IntegerAttributeGym_GameState");
    constexpr auto ControllerPath = TEXT("/Script/Angelscript.Ck_IntegerAttributeGym_PlayerController");
    constexpr auto ProjectionPath = TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_IntegerProjection");
    constexpr auto ComponentProbePath = TEXT("/Script/Angelscript.Ck_IntegerModifierComponent_ASProbe");
    constexpr const TCHAR* AnchorNames[4] = {
        TEXT("Gym.Attribute.IntegerBasic"), TEXT("Gym.Attribute.IntegerMinMaxCurrent"),
        TEXT("Gym.Attribute.IntegerModifiers"), TEXT("Gym.Attribute.IntegerClamping")};
    constexpr const TCHAR* ChildTags[4] = {
        TEXT("TAG_IntegerGym_Basic"), TEXT("TAG_IntegerGym_MinMaxCurrent"),
        TEXT("TAG_IntegerGym_Modifiers"), TEXT("TAG_IntegerGym_Clamping")};
    constexpr const TCHAR* ChildPaths[4] = {
        TEXT("/Script/Angelscript.Ck_EntityScript_IntegerGym_Basic"),
        TEXT("/Script/Angelscript.Ck_EntityScript_IntegerGym_MinMaxCurrent"),
        TEXT("/Script/Angelscript.Ck_EntityScript_IntegerGym_Modifiers"),
        TEXT("/Script/Angelscript.Ck_EntityScript_IntegerGym_Clamping")};
    constexpr const TCHAR* AttributeNames[4][3] = {
        {TEXT("IntegerAttribute.Health"), TEXT("IntegerAttribute.Armor"), TEXT("IntegerAttribute.Experience")},
        {TEXT("IntegerAttribute.PowerLevel"), nullptr, nullptr},
        {TEXT("IntegerAttribute.Damage"), nullptr, nullptr},
        {TEXT("IntegerAttribute.Resource"), nullptr, nullptr}};
    constexpr const TCHAR* RingNames[9] = {
        TEXT("HealthPresetIndex"), TEXT("ArmorPresetIndex"), TEXT("ExperiencePresetIndex"),
        TEXT("MinPresetIndex"), TEXT("MaxPresetIndex"), TEXT("CurrentPresetIndex"),
        TEXT("WeaponBonusPresetIndex"), TEXT("BuffBonusPresetIndex"), TEXT("ResourcePresetIndex")};
    constexpr int32 RingActions[9] = {8, 9, 10, 12, 13, 14, 16, 17, 22};
    constexpr int32 Presets[9][5] = {
        {100, 50, 10, -10, 150}, {50, 25, 5, -5, 80},
        {0, 500, 2000, -100, 0}, {10, 20, 60, 120, 0},
        {100, 80, 40, 5, 0}, {50, 75, 150, -20, 0},
        {25, 50, -25, -200, 0}, {10, 40, -10, -150, 0},
        {50, 0, 100, 150, -50}};
    constexpr int32 PresetCounts[9] = {5, 5, 4, 4, 4, 4, 4, 4, 5};

    struct FComponent { bool Present = false; int32 Base = 0, Bonus = 0, Final = 0; };
    struct FAttribute
    {
        FName Name;
        FComponent Min, Max, Current;
        int32 Pre = 0, Overflow = 0;
    };
    struct FStation
    {
        FName StationTag, ChildTag;
        int32 Index = -1, Step = 0, Phase = -1, RunStatus = -1;
        int32 ValueChanges = 0, HealthChanges = 0, ArmorChanges = 0;
        int32 ExperienceChanges = 0, MinChanges = 0, MaxChanges = 0;
        int32 CurrentChanges = 0, CurrentTestValue = 0, LastInputValue = 0;
        int32 LastPre = 0, LastClamped = 0, LastOverflow = 0;
        bool Increasing = false;
        int32 Modifiers = 0, WeaponDelta = 0, BuffDelta = 0;
        int32 MinEvents = 0, MaxEvents = 0, ClampSerial = 0;
        int32 BoundaryMinSerial = 0, BoundaryMaxSerial = 0;
        int32 BoundaryMinPre = 0, BoundaryMinFinal = 0, BoundaryMinOverflow = 0;
        int32 BoundaryMaxPre = 0, BoundaryMaxFinal = 0, BoundaryMaxOverflow = 0;
        bool Auto = false, Weapon = false, Buff = false;
        TArray<FAttribute> Attributes;
        FString Title, Body;
    };
    struct FSample { int32 Generation = 0, Sequence = 0; FStation Stations[4]; };
    struct FFeatures
    {
        int32 Attributes = 0, Timers = 0, Modifiers = 0, StateMachines = 0;
        TArray<FCk_Handle> Handles;
    };
    struct FFixture
    {
        UClass *Mode = nullptr, *State = nullptr, *Controller = nullptr;
        FStructProperty *Snapshot = nullptr, *Commit = nullptr, *CommitSnapshot = nullptr;
        FStructProperty *Station = nullptr, *Attribute = nullptr, *Component = nullptr;
        FArrayProperty *Stations = nullptr, *Attributes = nullptr, *ServerChildren = nullptr;
        FIntProperty *Generation = nullptr, *Sequence = nullptr, *ServerChildCount = nullptr;
        FIntProperty *CommitSerial = nullptr, *CommitAction = nullptr, *CommitPreset = nullptr;
        FIntProperty *Accepted = nullptr, *Attempt = nullptr, *Result = nullptr;
        FIntProperty *Ring[9] = {};
        FIntProperty *RpcSerial = nullptr, *RpcAction = nullptr, *RpcPreset = nullptr;
        FBoolProperty *Ready = nullptr, *Failed = nullptr;
        FBoolProperty *LocalReady = nullptr, *LocalFailed = nullptr;
        FIntProperty *LocalGeneration = nullptr;
        UFunction* Rpc = nullptr;
        ECkGym_StartupMode OriginalMode = ECkGym_StartupMode::Cycler;
        FString OriginalLast;
        TArray<FString> OriginalRecents;
        int32 InitialGeneration = 0, BeforeSequence = 0, BeforeAccepted[2] = {0, 0};
        int32 BeforeAttempt[2] = {0, 0}, BeforeResult[2] = {0, 0};
        int32 BeforeRings[2][9] = {};
        int32 LastSequence = 0, TerminalSequence = 0, BeforeMinEvents = 0, BeforeMaxEvents = 0;
        FCk_Handle Roots[4], FirstWeapon, FirstBuff;
        TArray<FCk_Handle> Retired;
        FCk_Handle ProbeOwner, ProbeAttribute, ProbeModifiers[3], ProbeStale;
        FCk_Handle ProbeMessenger, ProbeForeignMessenger;
        bool ProbeCreated = false;
        TSharedPtr<FStructOnScope> BeforeSnapshot;
        TWeakObjectPtr<UGameInstance> SurvivorInstance;
        TWeakObjectPtr<APlayerController> DepartingServerPC;
        TWeakObjectPtr<AGameStateBase> ServerState;
        bool TransactionViolation = false;
        FSample Before;
        FAutomationTestBase* Test = nullptr;
        void CaptureSettings()
        {
            const auto* Settings = GetDefault<UCkGym_StartupSettings>();
            OriginalMode = Settings->StartupMode;
            OriginalLast = Settings->LastGymName;
            OriginalRecents = Settings->RecentGymNames;
        }
        void RestoreSettings() const
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = OriginalMode;
            Settings->LastGymName = OriginalLast;
            Settings->RecentGymNames = OriginalRecents;
            Settings->SaveConfig();
        }
    };

    template<typename T> auto Prop(const UStruct* S, const TCHAR* Name) -> T*
    { return IsValid(S) ? FindFProperty<T>(S, Name) : nullptr; }
    auto Live(const FCk_Handle& H) -> bool
    {
        return ck::IsValid(H) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }
    auto Root(UWorld* W) -> FCk_Handle
    { return IsValid(W) ? UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(W) : FCk_Handle{}; }
    auto State(UWorld* W, const FFixture& F) -> AGameStateBase*
    {
        auto* S = IsValid(W) ? W->GetGameState() : nullptr;
        return IsValid(S) && S->IsA(F.State) ? S : nullptr;
    }
    auto Int(const UStruct* S, const void* D, const TCHAR* Name) -> int32
    { return Prop<FIntProperty>(S, Name)->GetPropertyValue_InContainer(D); }
    auto Flag(const UStruct* S, const void* D, const TCHAR* Name) -> bool
    { return Prop<FBoolProperty>(S, Name)->GetPropertyValue_InContainer(D); }
    auto Text(const UStruct* S, const void* D, const TCHAR* Name) -> FString
    { return Prop<FStrProperty>(S, Name)->GetPropertyValue_InContainer(D); }
    auto Name(const UStruct* S, const void* D, const TCHAR* Key) -> FName
    { return Prop<FNameProperty>(S, Key)->GetPropertyValue_InContainer(D); }
    auto Data(AGameStateBase* S, const FFixture& F) -> const void*
    { return IsValid(S) ? F.Snapshot->ContainerPtrToValuePtr<void>(S) : nullptr; }
    auto Gen(AGameStateBase* S, const FFixture& F) -> int32
    { const auto* D = Data(S, F); return D ? F.Generation->GetPropertyValue_InContainer(D) : INDEX_NONE; }
    auto Seq(AGameStateBase* S, const FFixture& F) -> int32
    { const auto* D = Data(S, F); return D ? F.Sequence->GetPropertyValue_InContainer(D) : INDEX_NONE; }
    auto Ready(AGameStateBase* S, const FFixture& F, int32 G) -> bool
    { const auto* D = Data(S, F); return D && Gen(S, F) == G && Seq(S, F) > 0 &&
        F.Ready->GetPropertyValue_InContainer(D) && !F.Failed->GetPropertyValue_InContainer(D); }
    auto Failed(AGameStateBase* S, const FFixture& F, int32 G) -> bool
    { const auto* D = Data(S, F); return D && Gen(S, F) == G &&
        F.Failed->GetPropertyValue_InContainer(D) && !F.Ready->GetPropertyValue_InContainer(D); }

    auto ReadComponent(const FFixture& F, const void* A, const TCHAR* Key, FComponent& Out) -> bool
    {
        const auto* D = Prop<FStructProperty>(F.Attribute->Struct, Key)->ContainerPtrToValuePtr<void>(A);
        const auto* C = F.Component->Struct.Get();
        Out.Present = Flag(C, D, TEXT("Present"));
        Out.Base = Int(C, D, TEXT("Base")); Out.Bonus = Int(C, D, TEXT("Bonus"));
        Out.Final = Int(C, D, TEXT("Final"));
        return true;
    }
    auto ReadData(const void* D, const FFixture& F, FSample& Out) -> bool
    {
        if (!D || !F.Ready->GetPropertyValue_InContainer(D) ||
            F.Failed->GetPropertyValue_InContainer(D)) { return false; }
        FScriptArrayHelper Slots(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(D));
        if (Slots.Num() != 4) { return false; }
        Out = FSample{};
        Out.Generation = F.Generation->GetPropertyValue_InContainer(D);
        Out.Sequence = F.Sequence->GetPropertyValue_InContainer(D);
        for (int32 I = 0; I < 4; ++I)
        {
            const void* V = Slots.GetRawPtr(I);
            auto& T = Out.Stations[I];
            T.Index = Int(F.Station->Struct, V, TEXT("StationIndex"));
            T.StationTag = Name(F.Station->Struct, V, TEXT("StationTag"));
            T.ChildTag = Name(F.Station->Struct, V, TEXT("ChildTag"));
            T.Step = Int(F.Station->Struct, V, TEXT("AutoStep"));
            T.Auto = Flag(F.Station->Struct, V, TEXT("AutoRunning"));
            T.ValueChanges = Int(F.Station->Struct, V, TEXT("ValueChangeCount"));
            T.HealthChanges = Int(F.Station->Struct, V, TEXT("HealthChangeCount"));
            T.ArmorChanges = Int(F.Station->Struct, V, TEXT("ArmorChangeCount"));
            T.ExperienceChanges = Int(F.Station->Struct, V, TEXT("ExperienceChangeCount"));
            T.MinChanges = Int(F.Station->Struct, V, TEXT("MinChangeCount"));
            T.MaxChanges = Int(F.Station->Struct, V, TEXT("MaxChangeCount"));
            T.CurrentChanges = Int(F.Station->Struct, V, TEXT("CurrentChangeCount"));
            T.Increasing = Flag(F.Station->Struct, V, TEXT("IsIncreasing"));
            T.CurrentTestValue = Int(F.Station->Struct, V, TEXT("CurrentTestValue"));
            T.LastInputValue = Int(F.Station->Struct, V, TEXT("LastInputValue"));
            T.LastPre = Int(F.Station->Struct, V, TEXT("LastPreClampValue"));
            T.LastClamped = Int(F.Station->Struct, V, TEXT("LastClampedValue"));
            T.LastOverflow = Int(F.Station->Struct, V, TEXT("LastOverflow"));
            T.Phase = Int(F.Station->Struct, V, TEXT("ModifierPhase"));
            T.RunStatus = Int(F.Station->Struct, V, TEXT("ModifierRunStatus"));
            T.Modifiers = Int(F.Station->Struct, V, TEXT("ModifierCount"));
            T.Weapon = Flag(F.Station->Struct, V, TEXT("WeaponPresent"));
            T.Buff = Flag(F.Station->Struct, V, TEXT("BuffPresent"));
            T.WeaponDelta = Int(F.Station->Struct, V, TEXT("WeaponDelta"));
            T.BuffDelta = Int(F.Station->Struct, V, TEXT("BuffDelta"));
            T.MinEvents = Int(F.Station->Struct, V, TEXT("MinClampCount"));
            T.MaxEvents = Int(F.Station->Struct, V, TEXT("MaxClampCount"));
            T.ClampSerial = Int(F.Station->Struct, V, TEXT("ClampEventSerial"));
            T.BoundaryMinSerial = Int(F.Station->Struct, V, TEXT("BoundaryMinEventSerial"));
            T.BoundaryMaxSerial = Int(F.Station->Struct, V, TEXT("BoundaryMaxEventSerial"));
            T.BoundaryMinPre = Int(F.Station->Struct, V, TEXT("BoundaryMinPre"));
            T.BoundaryMinFinal = Int(F.Station->Struct, V, TEXT("BoundaryMinFinal"));
            T.BoundaryMinOverflow = Int(F.Station->Struct, V, TEXT("BoundaryMinOverflow"));
            T.BoundaryMaxPre = Int(F.Station->Struct, V, TEXT("BoundaryMaxPre"));
            T.BoundaryMaxFinal = Int(F.Station->Struct, V, TEXT("BoundaryMaxFinal"));
            T.BoundaryMaxOverflow = Int(F.Station->Struct, V, TEXT("BoundaryMaxOverflow"));
            T.Title = Text(F.Station->Struct, V, TEXT("Title"));
            T.Body = Text(F.Station->Struct, V, TEXT("Body"));
            if (T.Index != I || T.StationTag != FName(AnchorNames[I]) ||
                T.ChildTag != FName(ChildTags[I]) ||
                T.Title.IsEmpty() || T.Body.IsEmpty() || T.Step < 0) { return false; }
            FScriptArrayHelper Attributes(F.Attributes, F.Attributes->ContainerPtrToValuePtr<void>(V));
            if (Attributes.Num() != (I == 0 ? 3 : 1)) { return false; }
            for (int32 J = 0; J < Attributes.Num(); ++J)
            {
                const void* A = Attributes.GetRawPtr(J);
                FAttribute X;
                X.Name = Name(F.Attribute->Struct, A, TEXT("Name"));
                X.Pre = Int(F.Attribute->Struct, A, TEXT("PreClampFinal"));
                X.Overflow = Int(F.Attribute->Struct, A, TEXT("ClampOverflow"));
                ReadComponent(F, A, TEXT("Min"), X.Min);
                ReadComponent(F, A, TEXT("Max"), X.Max);
                ReadComponent(F, A, TEXT("Current"), X.Current);
                if (X.Name != FName(AttributeNames[I][J]) || !X.Current.Present ||
                    !X.Min.Present || X.Max.Present != (I == 0 ? J != 2 : I != 2) ||
                    int64(X.Pre) - int64(X.Current.Final) != int64(X.Overflow)) { return false; }
                T.Attributes.Add(X);
            }
        }
        return Out.Generation > 0 && Out.Sequence > 0;
    }
    auto Read(AGameStateBase* S, const FFixture& F, FSample& Out) -> bool
    { return ReadData(Data(S, F), F, Out); }

    auto Collect(const FCk_Handle& H, FFeatures& Out) -> void
    {
        if (!Live(H)) { return; }
        if (UCk_Utils_IntegerAttribute_UE::Has(H)) { Out.Attributes++; Out.Handles.AddUnique(H); }
        if (UCk_Utils_IntegerAttributeModifier_UE::Has(H)) { Out.Modifiers++; Out.Handles.AddUnique(H); }
        if (UCk_Utils_Timer_UE::Has(H)) { Out.Timers++; Out.Handles.AddUnique(H); }
        if (UCk_Utils_StateMachine_UE::Has(H)) { Out.StateMachines++; }
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(H))
        { Collect(Child, Out); }
    }
    auto ChildClass(const FCk_Handle& H) -> FString
    {
        if (!Live(H) || !UCk_Utils_EntityScript_UE::Has(H)) { return {}; }
        const auto Script = UCk_Utils_EntityScript_UE::CastChecked(H);
        const auto* Class = UCk_Utils_EntityScript_UE::Get_ScriptClass(Script).Get();
        return IsValid(Class) ? Class->GetPathName() : FString{};
    }
    auto Branches(UWorld* W, const FFixture& F, bool Authority, bool Dedicated,
                  TArray<FCk_Handle>* Originals = nullptr,
                  TArray<FCk_Handle>* Projections = nullptr) -> bool
    {
        const auto World = Root(W);
        if (!Live(World)) { return false; }
        int32 AttributeCount = 0, TimerCount = 0, StateMachineCount = 0;
        for (int32 I = 0; I < 4; ++I)
        {
            auto Anchors = UCk_Utils_EntityTag_UE::ForEach_Entity(World, FName(AnchorNames[I]));
            if (Anchors.Num() != 1 || !Live(Anchors[0])) { return false; }
            int32 Actual = 0, Local = 0;
            for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Anchors[0]))
            {
                if (!Live(Child) || !UCk_Utils_EntityScript_UE::Has(Child)) { continue; }
                const auto Path = ChildClass(Child);
                if (Path == ChildPaths[I])
                {
                    Actual++;
                    if (Originals) { Originals->Add(Child); }
                    FFeatures Features; Collect(Child, Features);
                    if (Features.Attributes != (I == 0 ? 3 : 1) ||
                        Features.Timers != (I == 3 ? 3 : I == 2 ? 1 : 2) ||
                        Features.StateMachines != (I == 2 ? 1 : 0)) { return false; }
                    AttributeCount += Features.Attributes; TimerCount += Features.Timers;
                    StateMachineCount += Features.StateMachines;
                }
                else if (Path == ProjectionPath)
                {
                    Local++;
                    if (Projections) { Projections->Add(Child); }
                    FFeatures Features; Collect(Child, Features);
                    if (Features.Attributes != 0 || Features.Modifiers != 0 ||
                        Features.Timers != 1 || Features.StateMachines != 0) { return false; }
                }
                else { return false; }
            }
            if (Actual != (Authority ? 1 : 0) ||
                Local != (Authority && Dedicated ? 0 : 1)) { return false; }
        }
        return !Authority || (AttributeCount == 6 && TimerCount == 8 &&
            StateMachineCount == 1);
    }
    auto NoBranches(UWorld* W) -> bool
    {
        const auto World = Root(W);
        if (!Live(World)) { return false; }
        for (int32 I = 0; I < 4; ++I)
        {
            for (const auto& H : UCk_Utils_EntityTag_UE::ForEach_Entity(World, FName(ChildTags[I])))
            { if (Live(H)) { return false; } }
            const auto Anchors = UCk_Utils_EntityTag_UE::ForEach_Entity(World, FName(AnchorNames[I]));
            if (Anchors.Num() > 1) { return false; }
            if (Anchors.Num() == 1)
            {
                for (const auto& H : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Anchors[0]))
                { if (Live(H) && (ChildClass(H) == ChildPaths[I] ||
                    ChildClass(H) == ProjectionPath)) { return false; } }
            }
        }
        for (const auto& H : UCk_Utils_EntityTag_UE::ForEach_Entity(World,
            FName(TEXT("TAG_AttributeGym_IntegerProjection"))))
        { if (Live(H)) { return false; } }
        return true;
    }

    auto RecordedAttribute(const FCk_Handle& Owner, const TCHAR* Label)
        -> FCk_Handle_IntegerAttribute
    {
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(Label), false);
        return Live(Owner) && Tag.IsValid() ?
            UCk_Utils_IntegerAttribute_UE::TryGet(Owner, Tag) : FCk_Handle_IntegerAttribute{};
    }
    auto Modifier(const FCk_Handle& RootHandle, const TCHAR* Label)
        -> FCk_Handle_IntegerAttributeModifier
    {
        const auto Damage = RecordedAttribute(RootHandle, TEXT("IntegerAttribute.Damage"));
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(Label), false);
        return Live(FCk_Handle{Damage}) && Tag.IsValid() ?
            UCk_Utils_IntegerAttributeModifier_UE::TryGet(Damage, Tag) :
            FCk_Handle_IntegerAttributeModifier{};
    }
    auto LiveMatches(const FCk_Handle& RootHandle, int32 StationIndex, const FStation& S) -> bool
    {
        if (!Live(RootHandle) || S.Attributes.Num() != (StationIndex == 0 ? 3 : 1)) { return false; }
        for (int32 I = 0; I < S.Attributes.Num(); ++I)
        {
            const auto H = RecordedAttribute(RootHandle, AttributeNames[StationIndex][I]);
            const auto Raw = FCk_Handle{H};
            const auto& A = S.Attributes[I];
            if (!Live(Raw) || !UCk_Utils_IntegerAttribute_UE::Has(Raw) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Raw) != RootHandle) { return false; }
            for (int32 C = 0; C < 3; ++C)
            {
                const auto Type = C == 0 ? ECk_MinMaxCurrent::Min :
                    C == 1 ? ECk_MinMaxCurrent::Max : ECk_MinMaxCurrent::Current;
                const auto& Expected = C == 0 ? A.Min : C == 1 ? A.Max : A.Current;
                if (UCk_Utils_IntegerAttribute_UE::Has_Component(H, Type) != Expected.Present)
                { return false; }
                if (Expected.Present &&
                    (UCk_Utils_IntegerAttribute_UE::Get_BaseValue(H, Type) != Expected.Base ||
                     UCk_Utils_IntegerAttribute_UE::Get_BonusValue(H, Type) != Expected.Bonus ||
                     UCk_Utils_IntegerAttribute_UE::Get_FinalValue(H, Type) != Expected.Final))
                { return false; }
            }
            if (UCk_Utils_IntegerAttribute_UE::Get_PreClampFinalValue(H) != A.Pre ||
                UCk_Utils_IntegerAttribute_UE::Get_ClampOverflow(H) != A.Overflow) { return false; }
        }
        if (StationIndex == 2)
        {
            const auto Damage = RecordedAttribute(RootHandle, TEXT("IntegerAttribute.Damage"));
            const auto Weapon = Modifier(RootHandle, TEXT("Modifier.Weapon"));
            const auto Buff = Modifier(RootHandle, TEXT("Modifier.Buff"));
            if (Live(FCk_Handle{Weapon}) != S.Weapon || Live(FCk_Handle{Buff}) != S.Buff ||
                S.Modifiers != int32(S.Weapon) + int32(S.Buff)) { return false; }
            if ((S.Weapon && (!UCk_Utils_IntegerAttributeModifier_UE::Has(FCk_Handle{Weapon}) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(FCk_Handle{Weapon}) != FCk_Handle{Damage})) ||
                (S.Buff && (!UCk_Utils_IntegerAttributeModifier_UE::Has(FCk_Handle{Buff}) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(FCk_Handle{Buff}) != FCk_Handle{Damage})))
            { return false; }
            if (S.Weapon && UCk_Utils_IntegerAttributeModifier_UE::Get_Delta(Weapon) != S.WeaponDelta)
            { return false; }
            if (S.Buff && UCk_Utils_IntegerAttributeModifier_UE::Get_Delta(Buff) != S.BuffDelta)
            { return false; }
        }
        return true;
    }

    auto CreateComponentProbe(UWorld* W, FFixture& F) -> bool
    {
        if (!IsValid(W) || W != ck::auto_test::net::Get_ServerWorld()) { return false; }
        const auto DamageTag = FGameplayTag::RequestGameplayTag(
            FName(TEXT("IntegerAttribute.Damage")), false);
        const TCHAR* Labels[4] = {TEXT("Modifier.Weapon"), TEXT("Modifier.Buff"),
            TEXT("Modifier.Armor"), TEXT("Modifier.Shield")};
        FGameplayTag Tags[4];
        for (int32 I = 0; I < 4; ++I)
        {
            Tags[I] = FGameplayTag::RequestGameplayTag(FName(Labels[I]), false);
            if (!Tags[I].IsValid()) { return false; }
        }
        if (!DamageTag.IsValid()) { return false; }
        F.ProbeOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(W);
        if (!Live(F.ProbeOwner)) { return false; }
        auto Spec = FCk_IntegerAttribute_Spec{DamageTag, 50};
        Spec.Set_MinMax(ECk_MinMax::MinMax);
        Spec.Set_MinValue(0); Spec.Set_MaxValue(100);
        auto Attribute = UCk_Utils_IntegerAttribute_UE::Add(
            F.ProbeOwner, Spec, ECk_Replication::DoesNotReplicate);
        F.ProbeAttribute = FCk_Handle{Attribute};
        if (!Live(F.ProbeAttribute) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(F.ProbeAttribute) != F.ProbeOwner ||
            !UCk_Utils_IntegerAttribute_UE::Has(F.ProbeAttribute) ||
            !UCk_Utils_IntegerAttribute_UE::Has_Component(Attribute, ECk_MinMaxCurrent::Current) ||
            !UCk_Utils_IntegerAttribute_UE::Has_Component(Attribute, ECk_MinMaxCurrent::Min) ||
            !UCk_Utils_IntegerAttribute_UE::Has_Component(Attribute, ECk_MinMaxCurrent::Max))
        { return false; }
        const ECk_MinMaxCurrent Components[4] = {ECk_MinMaxCurrent::Current,
            ECk_MinMaxCurrent::Min, ECk_MinMaxCurrent::Max, ECk_MinMaxCurrent::Current};
        for (int32 I = 0; I < 4; ++I)
        {
            const auto ModifierSpec = FCk_IntegerAttributeModifier_Spec{0, Components[I]};
            const auto Modifier = UCk_Utils_IntegerAttributeModifier_UE::Add_Revocable(
                Attribute, Tags[I], ECk_AttributeModifier_Operation::Add, ModifierSpec);
            const auto Handle = FCk_Handle{Modifier};
            if (I < 3) { F.ProbeModifiers[I] = Handle; }
            else { F.ProbeStale = Handle; }
            if (!Live(Handle) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Handle) != F.ProbeAttribute ||
                !UCk_Utils_IntegerAttributeModifier_UE::Has(Handle)) { return false; }
        }
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F.ProbeStale);
        return true;
    }

    auto CheckMessengerLookup(FFixture& F) -> bool
    {
        if (!F.Test || !Live(F.ProbeOwner) || !Live(F.ProbeAttribute) ||
            ck::IsValid(F.ProbeStale)) { return false; }
        const auto Tag = FGameplayTag::RequestGameplayTag(
            FName(TEXT("Modifier.Weapon")), false);
        const auto Missing = FGameplayTag::RequestGameplayTag(
            FName(TEXT("Modifier.Buff")), false);
        if (!Tag.IsValid() || !Missing.IsValid()) { return false; }
        const auto Lookup = [](const FCk_Handle& Owner, FGameplayTag Name)
        { return UCk_Utils_Messaging_UE::TryGet_MessengerEntity(Owner, Name); };
        const int32 Before = UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(F.ProbeOwner).Num();
        bool Good = true;
        Good &= F.Test->TestFalse(TEXT("default messenger owner rejected"),
            ck::IsValid(Lookup(FCk_Handle{}, Tag)));
        Good &= F.Test->TestFalse(TEXT("retired messenger owner rejected"),
            ck::IsValid(Lookup(F.ProbeStale, Tag)));
        Good &= F.Test->TestFalse(TEXT("owner without messenger record rejected"),
            ck::IsValid(Lookup(F.ProbeOwner, Tag)));
        Good &= F.Test->TestFalse(TEXT("missing messenger label rejected"),
            ck::IsValid(Lookup(F.ProbeOwner, Missing)));
        Good &= F.Test->TestEqual(TEXT("rejected messenger reads create no child"),
            UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(F.ProbeOwner).Num(), Before);
        if (!Good) { return false; }

        // Broadcast uses the production messenger-record creation path.
        UCk_Utils_Messaging_UE::Broadcast(F.ProbeOwner, Tag, FInstancedStruct{});
        F.ProbeMessenger = Lookup(F.ProbeOwner, Tag);
        Good &= F.Test->TestTrue(TEXT("production messenger lookup is an owned direct child"),
            Live(F.ProbeMessenger) &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(F.ProbeMessenger) == F.ProbeOwner &&
            UCk_Utils_GameplayLabel_UE::Has(F.ProbeMessenger) &&
            UCk_Utils_GameplayLabel_UE::Get_Label(F.ProbeMessenger) == Tag);
        if (!Good) { return false; }
        const int32 BoundCount = UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(F.ProbeOwner).Num();
        Good &= F.Test->TestTrue(TEXT("repeated production lookup preserves messenger identity"),
            Lookup(F.ProbeOwner, Tag) == F.ProbeMessenger);
        Good &= F.Test->TestFalse(TEXT("different unbound messenger label rejected"),
            ck::IsValid(Lookup(F.ProbeOwner, Missing)));
        Good &= F.Test->TestEqual(TEXT("bound messenger reads create no child"),
            UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(F.ProbeOwner).Num(), BoundCount);

        F.ProbeForeignMessenger = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(F.ProbeOwner);
        if (!Live(F.ProbeForeignMessenger)) { return false; }
        UCk_Utils_GameplayLabel_UE::Add(F.ProbeForeignMessenger, Tag);
        Good &= F.Test->TestTrue(TEXT("same-label foreign direct child cannot replace recorded messenger"),
            Live(F.ProbeForeignMessenger) && F.ProbeForeignMessenger != F.ProbeMessenger &&
            Lookup(F.ProbeOwner, Tag) == F.ProbeMessenger);

        F.ProbeMessenger.Remove<ck::FFragment_Signal_Messaging>();
        Good &= F.Test->TestFalse(TEXT("recorded partial messenger without signal rejected"),
            ck::IsValid(Lookup(F.ProbeOwner, Tag)));
        Good &= F.Test->TestFalse(TEXT("partial record cannot adopt same-label foreign child"),
            Lookup(F.ProbeOwner, Tag) == F.ProbeForeignMessenger);
        Good &= F.Test->TestEqual(TEXT("partial messenger reads create no child"),
            UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(F.ProbeOwner).Num(), BoundCount + 1);
        F.ProbeMessenger.AddOrGet<ck::FFragment_Signal_Messaging>();
        Good &= F.Test->TestTrue(TEXT("restored production messenger remains the exact record entry"),
            Lookup(F.ProbeOwner, Tag) == F.ProbeMessenger);
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F.ProbeMessenger);
        Good &= F.Test->TestFalse(TEXT("recorded messenger BeginDestroy tombstone rejected"),
            ck::IsValid(Lookup(F.ProbeOwner, Tag)));
        Good &= F.Test->TestFalse(TEXT("same-label foreign child is never adopted"),
            Lookup(F.ProbeOwner, Tag) == F.ProbeForeignMessenger);
        return Good;
    }

    auto CheckComponentPredicate(FFixture& F) -> bool
    {
        if (!F.Test || !Live(F.ProbeOwner) || !Live(F.ProbeAttribute) ||
            ck::IsValid(F.ProbeStale)) { return false; }
        auto* NativeClass = UCk_Utils_IntegerAttributeModifier_UE::StaticClass();
        auto* NativeFunction = NativeClass->FindFunctionByName(TEXT("Has_Component"));
        auto* NativeHandle = NativeFunction ?
            Prop<FStructProperty>(NativeFunction, TEXT("InModifierEntity")) : nullptr;
        auto* NativeEnum = NativeFunction ?
            Prop<FEnumProperty>(NativeFunction, TEXT("InComponent")) : nullptr;
        auto* NativeByte = NativeFunction ?
            Prop<FByteProperty>(NativeFunction, TEXT("InComponent")) : nullptr;
        auto* NativeResult = NativeFunction ?
            Prop<FBoolProperty>(NativeFunction, TEXT("ReturnValue")) : nullptr;
        if (!NativeFunction || !NativeHandle ||
            NativeHandle->Struct != FCk_Handle::StaticStruct() ||
            ((!NativeEnum || !NativeEnum->GetUnderlyingProperty()) && !NativeByte) ||
            !NativeResult ||
            NativeFunction->NumParms != 3) { return false; }
        auto* ProbeClass = FSoftClassPath{ComponentProbePath}.TryLoadClass<UObject>();
        auto* ProbeFunction = IsValid(ProbeClass) ?
            ProbeClass->FindFunctionByName(TEXT("Probe")) : nullptr;
        auto* ProbeResult = ProbeFunction ?
            Prop<FBoolProperty>(ProbeFunction, TEXT("ReturnValue")) : nullptr;
        auto* Probe = IsValid(ProbeClass) ?
            NewObject<UObject>(GetTransientPackage(), ProbeClass) : nullptr;
        if (!IsValid(Probe) || !ProbeFunction || !ProbeResult ||
            ProbeFunction->NumParms != 5) { return false; }
        const TCHAR* ProbeNames[4] = {TEXT("Current"), TEXT("Min"),
            TEXT("Max"), TEXT("Stale")};
        const FCk_Handle Handles[5] = {F.ProbeModifiers[0], F.ProbeModifiers[1],
            F.ProbeModifiers[2], FCk_Handle{}, F.ProbeStale};
        for (int32 I = 0; I < 4; ++I)
        {
            auto* P = Prop<FStructProperty>(ProbeFunction, ProbeNames[I]);
            if (!P || P->Struct != FCk_Handle::StaticStruct()) { return false; }
        }
        FStructOnScope ProbeParams(ProbeFunction);
        for (int32 I = 0; I < 4; ++I)
        {
            auto* P = Prop<FStructProperty>(ProbeFunction, ProbeNames[I]);
            *P->ContainerPtrToValuePtr<FCk_Handle>(ProbeParams.GetStructMemory()) =
                I < 3 ? Handles[I] : Handles[4];
        }
        Probe->ProcessEvent(ProbeFunction, ProbeParams.GetStructMemory());
        bool Good = F.Test && F.Test->TestTrue(TEXT("AngelScript Integer modifier component predicate"),
            ProbeResult->GetPropertyValue_InContainer(ProbeParams.GetStructMemory()));
        const ECk_MinMaxCurrent Components[4] = {ECk_MinMaxCurrent::Current,
            ECk_MinMaxCurrent::Min, ECk_MinMaxCurrent::Max,
            static_cast<ECk_MinMaxCurrent>(255)};
        for (int32 I = 0; I < 5; ++I)
        {
            for (int32 J = 0; J < 4; ++J)
            {
                const bool Expected = I < 3 && I == J;
                const bool Native = UCk_Utils_IntegerAttributeModifier_UE::Has_Component(
                    Handles[I], Components[J]);
                FStructOnScope Params(NativeFunction);
                *NativeHandle->ContainerPtrToValuePtr<FCk_Handle>(Params.GetStructMemory()) = Handles[I];
                if (NativeEnum)
                {
                    NativeEnum->GetUnderlyingProperty()->SetIntPropertyValue(
                        NativeEnum->ContainerPtrToValuePtr<void>(Params.GetStructMemory()),
                        static_cast<uint64>(Components[J]));
                }
                else
                { NativeByte->SetPropertyValue_InContainer(Params.GetStructMemory(),
                    static_cast<uint8>(Components[J])); }
                NativeClass->GetDefaultObject()->ProcessEvent(NativeFunction, Params.GetStructMemory());
                const bool Reflected = NativeResult->GetPropertyValue_InContainer(Params.GetStructMemory());
                const FString NativeLabel = FString::Printf(
                    TEXT("native Integer modifier component handle %d component %d"), I, J);
                const FString ReflectedLabel = FString::Printf(
                    TEXT("reflected Integer modifier component handle %d component %d"), I, J);
                Good &= F.Test->TestEqual(NativeLabel, Native, Expected);
                Good &= F.Test->TestEqual(ReflectedLabel, Reflected, Expected);
            }
        }
        return Good;
    }

    auto Bind(FFixture& F, FAutomationTestBase& Test) -> bool
    {
        F.Mode = FSoftClassPath{ModePath}.TryLoadClass<AGameModeBase>();
        F.State = FSoftClassPath{StatePath}.TryLoadClass<AGameStateBase>();
        F.Controller = FSoftClassPath{ControllerPath}.TryLoadClass<APlayerController>();
        bool Good = Test.TestNotNull(TEXT("Integer gym mode"), F.Mode) &&
            Test.TestNotNull(TEXT("Integer gym state"), F.State) &&
            Test.TestNotNull(TEXT("Integer gym controller"), F.Controller);
        for (const auto* Path : ChildPaths)
        { Good &= Test.TestNotNull(TEXT("authored Integer station"),
            FSoftClassPath{Path}.TryLoadClass<UObject>()); }
        Good &= Test.TestNotNull(TEXT("pure Integer projection"),
            FSoftClassPath{ProjectionPath}.TryLoadClass<UObject>());
        if (!Good) { return false; }
        F.Snapshot = Prop<FStructProperty>(F.State, TEXT("Snapshot"));
        F.Commit = Prop<FStructProperty>(F.State, TEXT("LastControlCommit"));
        F.ServerChildren = Prop<FArrayProperty>(F.State, TEXT("ServerChildren"));
        F.ServerChildCount = Prop<FIntProperty>(F.State, TEXT("ServerChildCount"));
        F.LocalGeneration = Prop<FIntProperty>(F.State, TEXT("LocalProjectionGeneration"));
        F.LocalReady = Prop<FBoolProperty>(F.State, TEXT("LocalProjectionReady"));
        F.LocalFailed = Prop<FBoolProperty>(F.State, TEXT("LocalProjectionFailed"));
        Good &= Test.TestNotNull(TEXT("typed Integer snapshot"), F.Snapshot);
        Good &= Test.TestNotNull(TEXT("correlated Integer commit"), F.Commit);
        Good &= Test.TestNotNull(TEXT("server child handles"), F.ServerChildren);
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
        Good &= Test.TestNotNull(TEXT("Integer station array"), F.Stations);
        if (!Good) { return false; }
        F.Station = CastField<FStructProperty>(F.Stations->Inner);
        Good &= Test.TestNotNull(TEXT("Integer station schema"), F.Station);
        if (!Good) { return false; }
        F.Attributes = Prop<FArrayProperty>(F.Station->Struct, TEXT("Attributes"));
        Good &= Test.TestNotNull(TEXT("Integer attribute array"), F.Attributes);
        if (!Good) { return false; }
        F.Attribute = CastField<FStructProperty>(F.Attributes->Inner);
        Good &= Test.TestNotNull(TEXT("Integer attribute schema"), F.Attribute);
        if (!Good) { return false; }
        F.Component = Prop<FStructProperty>(F.Attribute->Struct, TEXT("Current"));
        Good &= Test.TestNotNull(TEXT("Integer component schema"), F.Component);
        if (!Good) { return false; }
        const auto* Station = F.Station->Struct.Get();
        const auto* Attr = F.Attribute->Struct.Get();
        const auto* Component = F.Component->Struct.Get();
        for (const auto* Key : {TEXT("StationIndex"), TEXT("AutoStep"),
            TEXT("ValueChangeCount"), TEXT("HealthChangeCount"),
            TEXT("ArmorChangeCount"), TEXT("ExperienceChangeCount"),
            TEXT("MinChangeCount"), TEXT("MaxChangeCount"),
            TEXT("CurrentChangeCount"), TEXT("CurrentTestValue"),
            TEXT("LastInputValue"), TEXT("LastPreClampValue"),
            TEXT("LastClampedValue"), TEXT("LastOverflow"),
            TEXT("ModifierPhase"), TEXT("ModifierRunStatus"), TEXT("ModifierCount"),
            TEXT("WeaponDelta"), TEXT("BuffDelta"), TEXT("MinClampCount"),
            TEXT("MaxClampCount"), TEXT("ClampEventSerial"),
            TEXT("BoundaryMinEventSerial"), TEXT("BoundaryMaxEventSerial"),
            TEXT("BoundaryMinPre"), TEXT("BoundaryMinFinal"), TEXT("BoundaryMinOverflow"),
            TEXT("BoundaryMaxPre"), TEXT("BoundaryMaxFinal"), TEXT("BoundaryMaxOverflow")})
        { Good &= Test.TestNotNull(Key, Prop<FIntProperty>(Station, Key)); }
        for (const auto* Key : {TEXT("StationTag"), TEXT("ChildTag")})
        { Good &= Test.TestNotNull(Key, Prop<FNameProperty>(Station, Key)); }
        for (const auto* Key : {TEXT("AutoRunning"), TEXT("WeaponPresent"),
            TEXT("BuffPresent"), TEXT("IsIncreasing")})
        { Good &= Test.TestNotNull(Key, Prop<FBoolProperty>(Station, Key)); }
        for (const auto* Key : {TEXT("Title"), TEXT("Body")})
        { Good &= Test.TestNotNull(Key, Prop<FStrProperty>(Station, Key)); }
        for (const auto* Key : {TEXT("PreClampFinal"), TEXT("ClampOverflow")})
        { Good &= Test.TestNotNull(Key, Prop<FIntProperty>(Attr, Key)); }
        Good &= Test.TestNotNull(TEXT("attribute name"), Prop<FNameProperty>(Attr, TEXT("Name")));
        for (const auto* Key : {TEXT("Min"), TEXT("Max"), TEXT("Current")})
        {
            const auto* P = Prop<FStructProperty>(Attr, Key);
            Good &= Test.TestNotNull(Key, P) && P->Struct == Component;
        }
        for (const auto* Key : {TEXT("Base"), TEXT("Bonus"), TEXT("Final")})
        { Good &= Test.TestNotNull(Key, Prop<FIntProperty>(Component, Key)); }
        Good &= Test.TestNotNull(TEXT("component presence"),
            Prop<FBoolProperty>(Component, TEXT("Present")));
        F.Accepted = Prop<FIntProperty>(F.Controller, TEXT("LastControlSerial"));
        F.Attempt = Prop<FIntProperty>(F.Controller, TEXT("LastControlAttemptSerial"));
        F.Result = Prop<FIntProperty>(F.Controller, TEXT("LastControlResult"));
        for (int32 I = 0; I < 9; ++I)
        {
            F.Ring[I] = Prop<FIntProperty>(F.Controller, RingNames[I]);
            Good &= Test.TestNotNull(TEXT("owner Integer preset ring"), F.Ring[I]);
        }
        F.Rpc = F.Controller->FindFunctionByName(TEXT("Server_RequestIntegerControl"));
        Good &= Test.TestNotNull(TEXT("owning Integer server RPC"), F.Rpc);
        if (!Good) { return false; }
        F.RpcSerial = Prop<FIntProperty>(F.Rpc, TEXT("InSerial"));
        F.RpcAction = Prop<FIntProperty>(F.Rpc, TEXT("InAction"));
        F.RpcPreset = Prop<FIntProperty>(F.Rpc, TEXT("InPreset"));
        const auto* Handles = CastField<FStructProperty>(F.ServerChildren->Inner);
        return Test.TestTrue(TEXT("exact Integer reflected transport and record schema"),
            F.Snapshot->HasAnyPropertyFlags(CPF_Net) &&
            F.Commit->HasAnyPropertyFlags(CPF_Net) &&
            Snap->GetName() == TEXT("CkAttributeGym_IntegerSnapshot") &&
            Station->GetName() == TEXT("CkAttributeGym_IntegerStationSample") &&
            Attr->GetName() == TEXT("CkAttributeGym_IntegerAttributeSample") &&
            Component->GetName() == TEXT("CkAttributeGym_IntegerComponentSample") &&
            F.Generation && F.Sequence && F.Ready && F.Failed &&
            F.CommitSerial && F.CommitAction && F.CommitPreset && F.CommitSnapshot &&
            F.CommitSnapshot->Struct == Snap && F.ServerChildCount &&
            F.LocalGeneration && F.LocalReady && F.LocalFailed &&
            Handles && Handles->Struct == FCk_Handle::StaticStruct() &&
            F.Accepted && F.Attempt && F.Result &&
            F.Accepted->HasAnyPropertyFlags(CPF_Net) &&
            F.Attempt->HasAnyPropertyFlags(CPF_Net) &&
            F.Result->HasAnyPropertyFlags(CPF_Net) &&
            F.RpcSerial && F.RpcAction && F.RpcPreset &&
            F.RpcSerial->HasAnyPropertyFlags(CPF_Parm) &&
            F.RpcAction->HasAnyPropertyFlags(CPF_Parm) &&
            F.RpcPreset->HasAnyPropertyFlags(CPF_Parm) && F.Rpc->NumParms == 3 &&
            F.Rpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer) && Good);
    }

    auto Invoke(APlayerController* PC, const FFixture& F, int32 Serial,
                int32 Action, int32 Preset) -> bool
    {
        if (!IsValid(PC) || !PC->IsA(F.Controller)) { return false; }
        FStructOnScope P(F.Rpc);
        F.RpcSerial->SetPropertyValue_InContainer(P.GetStructMemory(), Serial);
        F.RpcAction->SetPropertyValue_InContainer(P.GetStructMemory(), Action);
        F.RpcPreset->SetPropertyValue_InContainer(P.GetStructMemory(), Preset);
        PC->ProcessEvent(F.Rpc, P.GetStructMemory());
        return true;
    }
    auto Result(UWorld* W, const FFixture& F, int32 Attempt, int32 Accepted,
                int32 Code) -> bool
    {
        auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
        return IsValid(PC) && PC->IsA(F.Controller) &&
            F.Attempt->GetPropertyValue_InContainer(PC) == Attempt &&
            F.Accepted->GetPropertyValue_InContainer(PC) == Accepted &&
            F.Result->GetPropertyValue_InContainer(PC) == Code;
    }
    auto Capture(FFixture& F) -> bool
    {
        auto* S = State(ck::auto_test::net::Get_ServerWorld(), F);
        if (!Read(S, F, F.Before)) { return false; }
        F.BeforeSequence = F.Before.Sequence;
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
            for (int32 J = 0; J < 9; ++J)
            { F.BeforeRings[I][J] = F.Ring[J]->GetPropertyValue_InContainer(PC); }
        }
        return true;
    }
    auto CommitMatches(AGameStateBase* S, const FFixture& F,
                       int32 Serial, int32 Action, int32 Preset) -> bool
    {
        if (!Ready(S, F, F.InitialGeneration)) { return false; }
        auto* Authority = State(ck::auto_test::net::Get_ServerWorld(), F);
        if (!Ready(Authority, F, F.InitialGeneration)) { return false; }
        const auto* Commit = F.Commit->ContainerPtrToValuePtr<void>(S);
        const auto* Snap = F.CommitSnapshot->ContainerPtrToValuePtr<void>(Commit);
        const auto* ServerCommit = F.Commit->ContainerPtrToValuePtr<void>(Authority);
        const auto* ServerSnap = F.CommitSnapshot->ContainerPtrToValuePtr<void>(ServerCommit);
        const int32 CommitSequence = F.Sequence->GetPropertyValue_InContainer(Snap);
        return F.CommitSerial->GetPropertyValue_InContainer(Commit) == Serial &&
            F.CommitAction->GetPropertyValue_InContainer(Commit) == Action &&
            F.CommitPreset->GetPropertyValue_InContainer(Commit) == Preset &&
            CommitSequence > F.BeforeSequence && Seq(S, F) >= CommitSequence &&
            F.CommitSerial->GetPropertyValue_InContainer(ServerCommit) == Serial &&
            F.Snapshot->Identical(Snap, ServerSnap, 0) &&
            (Seq(S, F) != CommitSequence || F.Snapshot->Identical(Data(S, F), Snap, 0));
    }
    auto CommitSample(AGameStateBase* S, const FFixture& F, FSample& Out) -> bool
    {
        if (!IsValid(S)) { return false; }
        const auto* Commit = F.Commit->ContainerPtrToValuePtr<void>(S);
        return ReadData(F.CommitSnapshot->ContainerPtrToValuePtr<void>(Commit), F, Out);
    }
    auto WorldReady(UWorld* W, const FFixture& F, int32 G,
                    bool Authority, bool Dedicated) -> bool
    {
        auto* S = State(W, F);
        FSample Sample;
        if (!Ready(S, F, G) || !Read(S, F, Sample) ||
            !Branches(W, F, Authority, Dedicated)) { return false; }
        if (!Authority && (F.LocalGeneration->GetPropertyValue_InContainer(S) != G ||
            !F.LocalReady->GetPropertyValue_InContainer(S) ||
            F.LocalFailed->GetPropertyValue_InContainer(S))) { return false; }
        return true;
    }
    auto AllReady(const FFixture& F, int32 G, bool Dedicated,
                  UWorld* Survivor = nullptr) -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        auto* C0 = Survivor ? nullptr : ck::auto_test::net::Get_ClientWorld(0);
        auto* C1 = Survivor ? Survivor : ck::auto_test::net::Get_ClientWorld(1);
        if ((!Survivor && ck::auto_test::net::Get_AllPIEWorlds().Num() != 3) ||
            !WorldReady(Server, F, G, true, Dedicated) ||
            (!Survivor && !WorldReady(C0, F, G, false, Dedicated)) ||
            !WorldReady(C1, F, G, false, Dedicated)) { return false; }
        FSample A, B;
        if (!Read(State(Server, F), F, A) || !Read(State(C1, F), F, B) ||
            B.Generation != A.Generation || B.Sequence > A.Sequence) { return false; }
        return Survivor || (Read(State(C0, F), F, B) && B.Generation == A.Generation &&
            B.Sequence <= A.Sequence);
    }
    auto RingForAction(int32 Action) -> int32
    {
        for (int32 I = 0; I < 9; ++I)
        { if (RingActions[I] == Action) { return I; } }
        return INDEX_NONE;
    }
    auto RingsMatch(APlayerController* PC, const FFixture& F,
                    int32 Owner, int32 Action, int32 Preset) -> bool
    {
        if (!IsValid(PC) || !PC->IsA(F.Controller)) { return false; }
        const int32 Ring = RingForAction(Action);
        for (int32 I = 0; I < 9; ++I)
        {
            const int32 Expected = Action == 26 && Owner >= 0 ? -1 :
                I == Ring && Owner >= 0 ? Preset : F.BeforeRings[Owner < 0 ? 0 : Owner][I];
            if (F.Ring[I]->GetPropertyValue_InContainer(PC) != Expected) { return false; }
        }
        return true;
    }
    auto ControlSemantics(const FFixture& F, int32 Action, int32 Preset,
                          const FSample& After) -> bool
    {
        if (After.Generation != F.InitialGeneration ||
            After.Sequence <= F.BeforeSequence) { return false; }
        const auto& B = F.Before;
        if (Action == 1)
        {
            bool All = true;
            for (const auto& Station : B.Stations) { All &= Station.Auto; }
            for (const auto& Station : After.Stations)
            { if (Station.Auto != !All) { return false; } }
        }
        if (Action >= 3 && Action <= 6 &&
            After.Stations[Action - 3].Auto == B.Stations[Action - 3].Auto)
        { return false; }
        const int32 Ring = RingForAction(Action);
        if (Ring != INDEX_NONE)
        {
            const int32 Value = Presets[Ring][Preset];
            if (Action == 8 || Action == 9 || Action == 10)
            {
                const auto& A = After.Stations[0].Attributes[Action - 8];
                const int32 Clamped = Action == 10 ? FMath::Max(0, Value) :
                    FMath::Clamp(Value, 0, Action == 8 ? 100 : 50);
                if (After.Stations[0].Auto || A.Current.Base != Clamped ||
                    A.Pre != Value || A.Current.Final != Clamped) { return false; }
            }
            if (Action == 12 || Action == 13 || Action == 14)
            {
                const auto& A = After.Stations[1].Attributes[0];
                if (After.Stations[1].Auto ||
                    (Action == 12 && A.Min.Base != Value) ||
                    (Action == 13 && A.Max.Base != Value) ||
                    (Action == 14 && (A.Current.Base !=
                        FMath::Clamp(Value, A.Min.Final, A.Max.Final) || A.Pre != Value)) ||
                    A.Current.Final != FMath::Min(FMath::Max(A.Pre, A.Min.Final), A.Max.Final))
                { return false; }
            }
            if (Action == 16 || Action == 17)
            {
                const auto& M = After.Stations[2];
                if (M.Auto || (Action == 16 && (!M.Weapon || M.WeaponDelta != Value)) ||
                    (Action == 17 && (!M.Buff || M.BuffDelta != Value)) ||
                    M.Attributes[0].Current.Base != 100 ||
                    M.Attributes[0].Pre != 100 + M.WeaponDelta + M.BuffDelta ||
                    M.Attributes[0].Current.Final != FMath::Max(0, M.Attributes[0].Pre) ||
                    M.Attributes[0].Current.Bonus != M.Attributes[0].Current.Final - 100 ||
                    M.Attributes[0].Overflow !=
                        M.Attributes[0].Pre - M.Attributes[0].Current.Final)
                { return false; }
            }
            if (Action == 22)
            {
                const auto& A = After.Stations[3].Attributes[0];
                if (After.Stations[3].Auto || A.Current.Base !=
                    FMath::Clamp(Value, 0, 100) || A.Pre != Value ||
                    A.Current.Final != FMath::Clamp(Value, 0, 100)) { return false; }
            }
        }
        if (Action >= 18 && Action <= 20)
        {
            const auto& M = After.Stations[2];
            const auto& Prior = B.Stations[2];
            if (M.Auto ||
                (Action == 18 && (!Prior.Weapon || M.Weapon ||
                    M.Buff != Prior.Buff || M.BuffDelta != Prior.BuffDelta)) ||
                (Action == 19 && (!Prior.Buff || M.Buff ||
                    M.Weapon != Prior.Weapon || M.WeaponDelta != Prior.WeaponDelta)) ||
                (Action == 20 && (!Prior.Weapon || !Prior.Buff ||
                    M.Weapon || M.Buff)) ||
                M.Modifiers != int32(M.Weapon) + int32(M.Buff) ||
                M.Attributes[0].Current.Base != 100 ||
                M.Attributes[0].Pre != 100 + M.WeaponDelta + M.BuffDelta ||
                M.Attributes[0].Current.Final != FMath::Max(0, M.Attributes[0].Pre) ||
                M.Attributes[0].Current.Bonus != M.Attributes[0].Current.Final - 100 ||
                M.Attributes[0].Overflow !=
                    M.Attributes[0].Pre - M.Attributes[0].Current.Final)
            { return false; }
        }
        if (Action == 23)
        {
            const auto& C = After.Stations[3];
            if (C.Auto || C.MinEvents <= B.Stations[3].MinEvents ||
                C.MaxEvents <= B.Stations[3].MaxEvents ||
                C.BoundaryMinSerial <= B.Stations[3].ClampSerial ||
                C.BoundaryMaxSerial <= C.BoundaryMinSerial ||
                C.BoundaryMinPre != -50 || C.BoundaryMinFinal != 0 ||
                C.BoundaryMinOverflow != -50 || C.BoundaryMaxPre != 150 ||
                C.BoundaryMaxFinal != 100 || C.BoundaryMaxOverflow != 50 ||
                C.Attributes[0].Current.Final != 100) { return false; }
        }
        if (Action == 26)
        {
            const auto& H = After.Stations[0].Attributes;
            const auto& M = After.Stations[2];
            if (H[0].Current.Final != 100 || H[1].Current.Final != 25 ||
                H[2].Current.Final != 0 ||
                After.Stations[1].Attributes[0].Current.Final != 50 ||
                After.Stations[3].Attributes[0].Current.Final != 50 ||
                M.Phase != 0 || M.RunStatus != 1 || !M.Weapon || !M.Buff ||
                M.WeaponDelta != 25 || M.BuffDelta != 10) { return false; }
            for (const auto& Station : After.Stations)
            { if (!Station.Auto) { return false; } }
        }
        return true;
    }
    auto EquivalentPayload(const FSample& A, const FSample& B) -> bool
    {
        if (A.Generation != B.Generation) { return false; }
        for (int32 I = 0; I < 4; ++I)
        {
            const auto& X = A.Stations[I]; const auto& Y = B.Stations[I];
            if (X.Index != Y.Index || X.StationTag != Y.StationTag ||
                X.ChildTag != Y.ChildTag || X.Step != Y.Step || X.Auto != Y.Auto ||
                X.ValueChanges != Y.ValueChanges ||
                X.HealthChanges != Y.HealthChanges || X.ArmorChanges != Y.ArmorChanges ||
                X.ExperienceChanges != Y.ExperienceChanges ||
                X.MinChanges != Y.MinChanges || X.MaxChanges != Y.MaxChanges ||
                X.CurrentChanges != Y.CurrentChanges ||
                X.CurrentTestValue != Y.CurrentTestValue ||
                X.LastInputValue != Y.LastInputValue ||
                X.LastPre != Y.LastPre || X.LastClamped != Y.LastClamped ||
                X.LastOverflow != Y.LastOverflow || X.Increasing != Y.Increasing ||
                X.Phase != Y.Phase || X.RunStatus != Y.RunStatus ||
                X.Modifiers != Y.Modifiers || X.Weapon != Y.Weapon || X.Buff != Y.Buff ||
                X.WeaponDelta != Y.WeaponDelta || X.BuffDelta != Y.BuffDelta ||
                X.MinEvents != Y.MinEvents || X.MaxEvents != Y.MaxEvents ||
                X.ClampSerial != Y.ClampSerial ||
                X.BoundaryMinSerial != Y.BoundaryMinSerial ||
                X.BoundaryMaxSerial != Y.BoundaryMaxSerial ||
                X.BoundaryMinPre != Y.BoundaryMinPre ||
                X.BoundaryMinFinal != Y.BoundaryMinFinal ||
                X.BoundaryMinOverflow != Y.BoundaryMinOverflow ||
                X.BoundaryMaxPre != Y.BoundaryMaxPre ||
                X.BoundaryMaxFinal != Y.BoundaryMaxFinal ||
                X.BoundaryMaxOverflow != Y.BoundaryMaxOverflow ||
                X.Title != Y.Title || X.Body != Y.Body ||
                X.Attributes.Num() != Y.Attributes.Num()) { return false; }
            for (int32 J = 0; J < X.Attributes.Num(); ++J)
            {
                const auto& P = X.Attributes[J]; const auto& Q = Y.Attributes[J];
                if (P.Name != Q.Name || P.Pre != Q.Pre || P.Overflow != Q.Overflow ||
                    P.Min.Present != Q.Min.Present || P.Min.Base != Q.Min.Base ||
                    P.Min.Bonus != Q.Min.Bonus || P.Min.Final != Q.Min.Final ||
                    P.Max.Present != Q.Max.Present || P.Max.Base != Q.Max.Base ||
                    P.Max.Bonus != Q.Max.Bonus || P.Max.Final != Q.Max.Final ||
                    P.Current.Present != Q.Current.Present ||
                    P.Current.Base != Q.Current.Base ||
                    P.Current.Bonus != Q.Current.Bonus ||
                    P.Current.Final != Q.Current.Final) { return false; }
            }
        }
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkAttributeIntegerGym_Network,
    "Ck.Gym.Network.AttributeInteger.SharedFour",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
        EAutomationTestFlags::EngineFilter)

bool FCkAttributeIntegerGym_Network::RunTest(const FString& Parameters)
{
    using namespace ck_attribute_integer_network_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Attribute Integer network topology")); return false; }
    const auto F = MakeShared<FFixture>();
    F->Test = this;
    if (!Bind(*F, *this)) { return false; }
    F->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, F->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
            const int32 G = Gen(S, *F);
            return G > 0 && AllReady(*F, G, Dedicated) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(
                    ck::auto_test::net::Get_ServerWorld(), 0)) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(
                    ck::auto_test::net::Get_ServerWorld(), 1));
        }), 60.0, TEXT("four real Integer authority stations and two local projections ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            F->ProbeCreated = CreateComponentProbe(W, *F);
            TestTrue(TEXT("create independent server Integer modifier component hierarchy"),
                F->ProbeCreated);
            if (!F->ProbeCreated && Live(F->ProbeOwner))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F->ProbeOwner); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
        {
            return !F->ProbeCreated || (Live(F->ProbeOwner) && Live(F->ProbeAttribute) &&
                Live(F->ProbeModifiers[0]) && Live(F->ProbeModifiers[1]) &&
                Live(F->ProbeModifiers[2]) &&
                !ck::IsValid(F->ProbeStale));
        }), 15.0, TEXT("disposable Integer modifier fully retired before predicate probes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld*)
        {
            if (F->ProbeCreated)
            {
                TestTrue(TEXT("native, reflected and AngelScript Integer component predicates"),
                    CheckComponentPredicate(*F));
                TestTrue(TEXT("pure production messenger record lookup and retirement"),
                    CheckMessengerLookup(*F));
            }
            if (Live(F->ProbeOwner))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F->ProbeOwner); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            if (ck::IsValid(F->ProbeOwner) || ck::IsValid(F->ProbeAttribute) ||
                ck::IsValid(F->ProbeStale) || ck::IsValid(F->ProbeMessenger) ||
                ck::IsValid(F->ProbeForeignMessenger)) { return false; }
            for (const auto& H : F->ProbeModifiers)
            { if (ck::IsValid(H)) { return false; } }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            return AllReady(*F, Gen(S, *F), Dedicated);
        }), 20.0, TEXT("disposable Integer hierarchy retired before gym assertions")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F, Dedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            FSample Sample;
            TArray<FCk_Handle> Originals;
            if (!TestTrue(TEXT("capture typed Integer authority quartet"),
                Read(S, *F, Sample) && Branches(Server, *F, true, Dedicated, &Originals) &&
                Originals.Num() == 4 &&
                Server->GetNetMode() == (Dedicated ? NM_DedicatedServer : NM_ListenServer) &&
                IsValid(Server->GetAuthGameMode()) &&
                Server->GetAuthGameMode()->IsA(F->Mode))) { return false; }
            F->InitialGeneration = Sample.Generation;
            F->ServerState = S;
            auto* Survivor = ck::auto_test::net::Get_ClientWorld(1);
            F->SurvivorInstance = IsValid(Survivor) ? Survivor->GetGameInstance() : nullptr;
            if (!TestTrue(TEXT("stable Integer owner and replicated typed schema"),
                F->SurvivorInstance.IsValid() &&
                F->ServerChildCount->GetPropertyValue_InContainer(S) == 4 &&
                Sample.Stations[0].Attributes[2].Min.Present &&
                !Sample.Stations[0].Attributes[2].Max.Present &&
                Sample.Stations[2].Attributes[0].Min.Present &&
                !Sample.Stations[2].Attributes[0].Max.Present &&
                AllReady(*F, Sample.Generation, Dedicated))) { return false; }
            for (int32 I = 0; I < 4; ++I) { F->Roots[I] = Originals[I]; }
            return true;
        }), TEXT("capture Integer world-owned quartet and Min-only attributes")));

    auto QueueAccepted = [this, F, Dedicated](int32 Owner, int32 Serial,
                                              int32 Action, int32 Preset,
                                              const TCHAR* Label)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Owner,
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, F, Serial, Action, Preset](UWorld* W)
                {
                    if (!TestTrue(TEXT("capture complete Integer snapshot before control"),
                        Capture(*F))) { return; }
                    TestTrue(TEXT("real owning remote Integer RPC"),
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
                    // A running transaction may publish only with a new sequence.
                    if (Seq(S, *F) == F->BeforeSequence && F->BeforeSnapshot.IsValid() &&
                        !F->Snapshot->Identical(Data(S, *F),
                            F->BeforeSnapshot->GetStructMemory(), 0))
                    { F->TransactionViolation = true; }
                    auto* ServerOwner = ck::auto_test::net::TryGet_RemoteClientPlayerController(
                        Server, Owner);
                    if (IsValid(ServerOwner) && ServerOwner->IsA(F->Controller) &&
                        F->Attempt->GetPropertyValue_InContainer(ServerOwner) == Serial &&
                        F->Result->GetPropertyValue_InContainer(ServerOwner) == 1 &&
                        !CommitMatches(S, *F, Serial, Action, Preset))
                    { F->TransactionViolation = true; }
                    auto* C0 = ck::auto_test::net::Get_ClientWorld(0);
                    auto* C1 = ck::auto_test::net::Get_ClientWorld(1);
                    if (F->TransactionViolation ||
                        !Result(Owner == 0 ? C0 : C1, *F, Serial, Serial, 1) ||
                        !CommitMatches(S, *F, Serial, Action, Preset) ||
                        !CommitMatches(State(C0, *F), *F, Serial, Action, Preset) ||
                        !CommitMatches(State(C1, *F), *F, Serial, Action, Preset) ||
                        !AllReady(*F, F->InitialGeneration, Dedicated)) { return false; }
                    FSample Committed;
                    if (!CommitSample(S, *F, Committed) ||
                        !ControlSemantics(*F, Action, Preset, Committed)) { return false; }
                    auto* OwnerPC = (Owner == 0 ? C0 : C1)->GetFirstPlayerController();
                    auto* OtherPC = (Owner == 0 ? C1 : C0)->GetFirstPlayerController();
                    if (!RingsMatch(OwnerPC, *F, Owner, Action, Preset) ||
                        !RingsMatch(OtherPC, *F, 1 - Owner, -1, -1) ||
                        !Result(Owner == 0 ? C1 : C0, *F,
                            F->BeforeAttempt[1 - Owner], F->BeforeAccepted[1 - Owner],
                            F->BeforeResult[1 - Owner]))
                    { return false; }
                    // Only a paused target is stable enough for an exact live read.
                    const int32 Target = Action >= 8 && Action <= 10 ? 0 :
                        Action >= 12 && Action <= 14 ? 1 :
                        Action >= 16 && Action <= 20 ? 2 :
                        Action == 22 || Action == 23 ? 3 : -1;
                    if (Action == 1 && !Committed.Stations[0].Auto &&
                        !Committed.Stations[1].Auto && !Committed.Stations[2].Auto &&
                        !Committed.Stations[3].Auto)
                    {
                        for (int32 I = 0; I < 4; ++I)
                        { if (!LiveMatches(F->Roots[I], I, Committed.Stations[I])) { return false; } }
                    }
                    if (Target >= 0 && !Committed.Stations[Target].Auto &&
                        !LiveMatches(F->Roots[Target], Target, Committed.Stations[Target]))
                    { return false; }
                    return true;
                }), 30.0, Label));
    };

    // First stop every authored auto producer; each subsequent command is a
    // real serialised owner RPC and has a retained, typed correlated commit.
    QueueAccepted(0, 100, 1, -1, TEXT("Integer all-auto pause commits coherently"));
    QueueAccepted(0, 101, 8, 0, TEXT("Integer Health base and owner ring"));
    QueueAccepted(1, 200, 9, 0, TEXT("Integer Armor and independent owner ring"));
    QueueAccepted(0, 102, 10, 0, TEXT("Integer Experience remains Min-only"));
    QueueAccepted(1, 201, 12, 0, TEXT("Integer Power Min control"));
    QueueAccepted(0, 103, 13, 0, TEXT("Integer Power Max control"));
    QueueAccepted(1, 202, 14, 0, TEXT("Integer Power Current control"));
    QueueAccepted(0, 104, 16, 0, TEXT("Integer signed keyed Weapon +25"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld*)
        {
            F->FirstWeapon = FCk_Handle{Modifier(F->Roots[2], TEXT("Modifier.Weapon"))};
            TestTrue(TEXT("capture live keyed Integer Weapon identity"), Live(F->FirstWeapon));
        })));
    QueueAccepted(0, 105, 16, 1, TEXT("Integer Weapon Override preserves identity +50"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            return TestTrue(TEXT("Integer Weapon same keyed live handle after Override"),
                Live(F->FirstWeapon) &&
                FCk_Handle{Modifier(F->Roots[2], TEXT("Modifier.Weapon"))} == F->FirstWeapon);
        }), TEXT("keyed Integer Weapon identity")));
    QueueAccepted(1, 203, 17, 0, TEXT("Integer signed keyed Buff +10"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld*)
        {
            F->FirstBuff = FCk_Handle{Modifier(F->Roots[2], TEXT("Modifier.Buff"))};
            TestTrue(TEXT("capture live keyed Integer Buff identity"), Live(F->FirstBuff));
        })));
    QueueAccepted(1, 204, 17, 1, TEXT("Integer Buff Override preserves identity +40"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            return TestTrue(TEXT("Integer Buff same keyed live handle after Override"),
                Live(F->FirstBuff) &&
                FCk_Handle{Modifier(F->Roots[2], TEXT("Modifier.Buff"))} == F->FirstBuff);
        }), TEXT("keyed Integer Buff identity")));
    QueueAccepted(0, 106, 16, 2, TEXT("Integer negative Weapon modifier -25"));
    QueueAccepted(0, 107, 16, 3, TEXT("Integer negative Weapon modifier -200 and clamp"));
    QueueAccepted(1, 205, 9, 1, TEXT("Integer Armor preset 25"));
    QueueAccepted(1, 206, 9, 2, TEXT("Integer Armor preset 5"));
    QueueAccepted(1, 207, 9, 3, TEXT("Integer Armor negative -5 clamps"));
    QueueAccepted(1, 208, 9, 4, TEXT("Integer Armor over-max 80 clamps"));
    QueueAccepted(0, 108, 8, 1, TEXT("Integer Health preset 50"));
    QueueAccepted(0, 109, 8, 2, TEXT("Integer Health preset 10"));
    QueueAccepted(0, 110, 8, 3, TEXT("Integer Health negative -10 clamps"));
    QueueAccepted(0, 111, 8, 4, TEXT("Integer Health over-max 150 clamps"));
    QueueAccepted(0, 112, 10, 1, TEXT("Integer Experience preset 500, Min-only"));
    QueueAccepted(0, 113, 10, 2, TEXT("Integer Experience preset 2000, no Max"));
    QueueAccepted(0, 114, 10, 3, TEXT("Integer Experience negative -100 clamps at Min"));
    QueueAccepted(1, 209, 14, 1, TEXT("Integer Power Current preset 75"));
    QueueAccepted(1, 210, 14, 2, TEXT("Integer Power Current over-max 150 clamps"));
    QueueAccepted(1, 211, 14, 3, TEXT("Integer Power Current negative -20 clamps"));
    QueueAccepted(1, 212, 12, 1, TEXT("Integer Power Min preset 20"));
    QueueAccepted(0, 115, 13, 1, TEXT("Integer Power Max preset 80"));
    QueueAccepted(0, 116, 13, 2, TEXT("Integer Power Max preset 40 above Min20"));
    QueueAccepted(1, 213, 13, 0, TEXT("other owner Max ring begins at 100"));
    QueueAccepted(1, 214, 12, 2, TEXT("Integer Power Min preset 60 below Max100"));
    QueueAccepted(1, 215, 17, 2, TEXT("Integer negative Buff -10"));
    QueueAccepted(1, 216, 17, 3, TEXT("Integer negative Buff -150 and clamp"));
    QueueAccepted(1, 217, 22, 0, TEXT("Integer Resource interior 50"));
    QueueAccepted(1, 218, 22, 1, TEXT("Integer Resource zero boundary"));
    QueueAccepted(1, 219, 22, 2, TEXT("Integer Resource upper boundary 100"));
    QueueAccepted(1, 220, 22, 3, TEXT("Integer Resource over-max 150 clamps"));
    QueueAccepted(1, 221, 22, 4, TEXT("Integer Resource negative -50 clamps"));
    QueueAccepted(1, 222, 22, 0, TEXT("Integer Resource ring wraps to interior 50"));
    QueueAccepted(1, 223, 23, -1, TEXT("Integer B emits ordered -50 then +150 real clamps"));
    QueueAccepted(0, 117, 18, -1, TEXT("Integer Weapon removal retains Buff"));
    QueueAccepted(1, 224, 19, -1, TEXT("Integer Buff removal retires last modifier"));
    QueueAccepted(0, 118, 16, 0, TEXT("Integer Weapon ring wraps, re-add +25"));
    QueueAccepted(1, 225, 17, 0, TEXT("Integer Buff ring wraps, re-add +10"));
    QueueAccepted(0, 119, 20, -1, TEXT("Integer clear retires both keyed modifiers"));
    QueueAccepted(0, 120, 26, -1, TEXT("Integer four-station reset re-enters defaults and resumes"));

    // Min120 and Max5 cross every available opposing bound; the two owner
    // rings above exercise Min60 and Max40 without crossing the live bounds.
    QueueAccepted(1, 226, 1, -1, TEXT("Integer pause before invalid RPC checks"));
    auto QueueInvalid = [this, F, Dedicated](int32 Serial, int32 Action,
                                             int32 Preset, const TCHAR* Label)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, F, Serial, Action, Preset](UWorld* W)
                {
                    if (!TestTrue(TEXT("capture Integer before rejected RPC"), Capture(*F)))
                    { return; }
                    TestTrue(TEXT("send malformed Integer request through owning RPC"),
                        Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr,
                            *F, Serial, Action, Preset));
                })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda(
                [F, Dedicated, Serial]() -> bool
                {
                    auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
                    auto* C0 = ck::auto_test::net::Get_ClientWorld(0);
                    FSample After;
                    if (!Result(C0, *F, Serial, F->BeforeAccepted[0], 4) ||
                        !AllReady(*F, F->InitialGeneration, Dedicated) ||
                        !Read(S, *F, After) || !EquivalentPayload(After, F->Before))
                    { return false; }
                    auto* Other = ck::auto_test::net::Get_ClientWorld(1)->GetFirstPlayerController();
                    return RingsMatch(C0->GetFirstPlayerController(), *F, 0, -1, -1) &&
                        RingsMatch(Other, *F, 1, -1, -1);
                }), 15.0, Label));
    };
    QueueInvalid(0, 18, -1, TEXT("zero Integer serial rejected without mutation"));
    QueueInvalid(120, 26, -1, TEXT("replayed accepted Integer serial rejected without mutation"));
    QueueInvalid(121, 99, -1, TEXT("unsupported Integer action rejected"));
    QueueInvalid(122, 12, 2, TEXT("wrong owner Min preset rejected"));
    QueueInvalid(123, 18, 0, TEXT("preset on nonring Integer action rejected"));

    QueueAccepted(1, 227, 1, -1, TEXT("survivor resumes four authored Integer demos"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            F->DepartingServerPC = ck::auto_test::net::TryGet_RemoteClientPlayerController(
                ck::auto_test::net::Get_ServerWorld(), 0);
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            if (!TestTrue(TEXT("departing Integer owner has real client world and controller"),
                IsValid(W) && IsValid(PC) && IsValid(W->GetGameInstance()) &&
                F->DepartingServerPC.IsValid())) { return; }
            PC->ClientTravel(TEXT("/Engine/Maps/Entry"), TRAVEL_Absolute);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            FSample A;
            return IsValid(Survivor) &&
                !F->DepartingServerPC.IsValid() &&
                AllReady(*F, F->InitialGeneration, Dedicated, Survivor) &&
                Read(S, *F, A) && A.Sequence > F->BeforeSequence &&
                A.Stations[0].Auto && A.Stations[1].Auto &&
                A.Stations[2].Auto && A.Stations[3].Auto;
        }), 30.0, TEXT("departed owner leaves shared four-station Integer demo running")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
            F->BeforeSequence = Seq(S, *F);
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            auto* SurvivorPC = IsValid(Survivor) ? Survivor->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("surviving original client owns live Integer RPC"),
                IsValid(SurvivorPC) && SurvivorPC->GetWorld() == Survivor &&
                Invoke(SurvivorPC, *F, 228, 3, -1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
            return IsValid(Survivor) && Result(Survivor, *F, 228, 228, 1) &&
                CommitMatches(S, *F, 228, 3, -1) &&
                CommitMatches(State(Survivor, *F), *F, 228, 3, -1) &&
                AllReady(*F, F->InitialGeneration, Dedicated, Survivor);
        }), 30.0, TEXT("survivor independently controls shared Integer authority")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* Server)
        {
            auto* S = State(Server, *F);
            TArray<FCk_Handle> Roots;
            if (!TestTrue(TEXT("capture all owned Integer resources before child loss"),
                IsValid(S) && Branches(Server, *F, true, Server->GetNetMode() == NM_DedicatedServer,
                    &Roots) && Roots.Num() == 4)) { return; }
            F->Retired = Roots;
            for (const auto& H : Roots)
            { FFeatures Features; Collect(H, Features); F->Retired.Append(Features.Handles); }
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            TArray<FCk_Handle> Projections;
            if (!TestTrue(TEXT("capture live survivor Integer projection descendants"),
                Branches(Survivor, *F, false, Server->GetNetMode() == NM_DedicatedServer,
                    nullptr, &Projections) && Projections.Num() == 4)) { return; }
            F->Retired.Append(Projections);
            for (const auto& H : Projections)
            { FFeatures Features; Collect(H, Features); F->Retired.Append(Features.Handles); }
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F->Roots[1]);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            auto* S = State(Server, *F);
            auto* C = State(Survivor, *F);
            if (!Failed(S, *F, F->InitialGeneration) ||
                !Failed(C, *F, F->InitialGeneration) ||
                !F->LocalFailed->GetPropertyValue_InContainer(C) ||
                F->LocalReady->GetPropertyValue_InContainer(C) ||
                F->ServerChildCount->GetPropertyValue_InContainer(S) != 0 ||
                !NoBranches(Server) || !NoBranches(Survivor)) { return false; }
            for (const auto& H : F->Retired)
            { if (Live(H)) { return false; } }
            return true;
        }), 30.0, TEXT("Integer child loss retires all owned simulation and projection branches")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            F->TerminalSequence = Seq(F->ServerState.Get(), *F);
            return TestTrue(TEXT("capture terminal Integer failure sequence"),
                F->TerminalSequence > F->BeforeSequence);
        }), TEXT("capture terminal Integer sequence")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
            return TestTrue(TEXT("Integer child loss is terminal, no surviving authority"),
                S == F->ServerState.Get() && Failed(S, *F, F->InitialGeneration) &&
                Seq(S, *F) == F->TerminalSequence &&
                F->ServerChildCount->GetPropertyValue_InContainer(S) == 0 &&
                NoBranches(ck::auto_test::net::Get_ServerWorld()));
        }), TEXT("terminal Integer generation remains inert")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            F->RestoreSettings();
            return TestTrue(TEXT("Integer PIE worlds drained"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
        }), TEXT("restore Integer gym startup settings"), true));
    return true;
}

#endif
