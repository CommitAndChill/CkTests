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
#include "CkAttribute/ByteAttribute/CkByteAttribute_Utils.h"
#include "CkTimer/CkTimer_Utils.h"

#include <Engine/GameInstance.h>
#include <Engine/World.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/PlayerController.h>
#include <GameplayTagContainer.h>
#include <HAL/PlatformTime.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>

namespace ck_attribute_byte_network_test
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_ByteAttributeGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_ByteAttributeGym_GameState");
    constexpr auto ControllerPath = TEXT("/Script/Angelscript.Ck_ByteAttributeGym_PlayerController");
    constexpr auto ProjectionPath = TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_ByteProjection");
    constexpr const TCHAR* AnchorNames[6] = {
        TEXT("Gym.Attribute.ByteClamping"), TEXT("Gym.Attribute.ByteModifiers"),
        TEXT("Gym.Attribute.ByteMinMaxCurrent"), TEXT("Gym.Attribute.ByteMultiple"),
        TEXT("Gym.Attribute.ByteValues"), TEXT("Gym.Attribute.ByteSignals")};
    constexpr const TCHAR* OriginalPaths[6] = {
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_ByteClamping"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_ByteModifiers"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_ByteMinMaxCurrent"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_ByteMultiple"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_ByteValues"),
        TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_ByteSignals")};
    constexpr const TCHAR* OriginalTags[6] = {
        TEXT("TAG_AttributeGym_ByteClamping"), TEXT("TAG_AttributeGym_ByteModifiers"),
        TEXT("TAG_AttributeGym_ByteMinMaxCurrent"), TEXT("TAG_AttributeGym_ByteMultiple"),
        TEXT("TAG_AttributeGym_ByteValues"), TEXT("TAG_AttributeGym_ByteSignals")};
    constexpr const TCHAR* AttributeNames[6][5] = {
        {TEXT("ByteAttribute.Armor"), TEXT("ByteAttribute.Stamina"), TEXT("ByteAttribute.Health"), TEXT("ByteAttribute.Shield"), nullptr},
        {TEXT("ByteAttribute.Damage"), TEXT("ByteAttribute.Defense"), nullptr, nullptr, nullptr},
        {TEXT("ByteAttribute.PowerLevel"), nullptr, nullptr, nullptr, nullptr},
        {TEXT("RPG.Strength"), TEXT("RPG.Agility"), TEXT("RPG.Intelligence"), TEXT("Combat.Attack"), TEXT("Combat.Defense")},
        {TEXT("Test.ValueRetrieval"), nullptr, nullptr, nullptr, nullptr},
        {TEXT("Test.SignalBinding"), nullptr, nullptr, nullptr, nullptr}};

    struct FStation
    {
        int32 Step = 0, Total = 0, Batches = 0, Epoch = 0, Changes = 0, Clamps = 0;
        int32 BoundarySerial = 0;
        TArray<int32> ClampSerials;
        uint8 LastPreClamp = 0, LastClamped = 0, LastOverflow = 0;
        int32 Modifiers = 0, DelegateMask = 0;
        bool Auto = false, Combat = false;
        TArray<FName> Names;
        TArray<uint8> Base, Final;
        TArray<uint8> PreClamp, Overflow;
        TArray<uint8> ModifierDeltas;
        FString Title, Body;
    };
    struct FSample { FStation Stations[6]; int32 Total = 0; };
    struct FFeatures { int32 Attributes = 0, Modifiers = 0, Timers = 0; TArray<FCk_Handle> Handles; };
    struct FHistory { int32 Generation = 0, Sequence = 0; TSharedPtr<FStructOnScope> Copy; };
    struct FFixture
    {
        UClass *Mode = nullptr, *State = nullptr, *Controller = nullptr;
        FStructProperty *Snapshot = nullptr, *Attribute = nullptr, *Component = nullptr;
        FStructProperty *ControlCommit = nullptr, *CommitSnapshot = nullptr;
        FIntProperty *CommitSerial = nullptr, *CommitAction = nullptr, *CommitPreset = nullptr;
        FArrayProperty *Stations = nullptr, *Attributes = nullptr, *ServerChildren = nullptr;
        FArrayProperty *ActiveModifierDeltas = nullptr;
        FArrayProperty *ClampSerials = nullptr;
        FIntProperty *Generation = nullptr, *Sequence = nullptr, *StationIndex = nullptr;
        FIntProperty *AutoStep = nullptr, *TotalAttributes = nullptr, *CombatBatchCount = nullptr;
        FIntProperty *BatchEpoch = nullptr, *ValueChangeCount = nullptr, *ClampedCount = nullptr;
        FIntProperty *ActiveModifierCount = nullptr, *DelegateBoundMask = nullptr;
        FIntProperty *BoundarySerial = nullptr;
        FIntProperty *ServerChildCount = nullptr, *LocalGeneration = nullptr;
        FIntProperty *LastSerial = nullptr, *AttemptSerial = nullptr, *Result = nullptr;
        FIntProperty *Ring[4] = {nullptr, nullptr, nullptr, nullptr};
        FIntProperty *BoundAttempt[2] = {nullptr, nullptr};
        FBoolProperty *Ready = nullptr, *Failed = nullptr, *AutoRunning = nullptr;
        FBoolProperty *CombatPresent = nullptr, *LocalReady = nullptr, *LocalFailed = nullptr;
        FBoolProperty *Present = nullptr;
        FByteProperty *Base = nullptr, *Bonus = nullptr, *Final = nullptr;
        FByteProperty *PreClampFinal = nullptr, *AttributeOverflow = nullptr;
        FByteProperty *LastPreClamp = nullptr, *LastClamped = nullptr, *LastOverflow = nullptr;
        FNameProperty *Name = nullptr;
        FStrProperty *FailureReason = nullptr, *Title = nullptr, *Body = nullptr;
        FStrProperty *LocalFailureReason = nullptr;
        FStructProperty *Min = nullptr, *Max = nullptr, *Current = nullptr;
        UFunction *Rpc = nullptr;
        FIntProperty *RpcSerial = nullptr, *RpcAction = nullptr, *RpcPreset = nullptr;
        ECkGym_StartupMode OriginalMode = ECkGym_StartupMode::Cycler;
        FString OriginalLast;
        TArray<FString> OriginalRecents;
        int32 InitialGeneration = 0, BeforeServerSequence = 0, BeforeSequence[2] = {0, 0};
        int32 BeforeAccepted[2] = {0, 0}, DepartureSequence = 0, DepartureStep = 0;
        int32 FailureSequence = 0, HistoryGeneration = 0;
        int32 SurvivorBeforeSequence = 0, ServerBeforeSequence = 0;
        FStation BeforeSurvivorClamping;
        double NextDiagnostic = 0.0, PauseSince = 0.0;
        FSample Before;
        TSharedPtr<FStructOnScope> BeforeSnapshot;
        bool TransactionViolation = false;
        bool HistoryViolation = false;
        FAutomationTestBase* Test = nullptr;
        TArray<FHistory> History;
        FCk_Handle ServerRoots[6], CombatPair[2];
        FCk_Handle BeforeGBuff, BeforeJWeapon, BeforeJBuff, BeforeJEnchant;
        FCk_Handle AuthoredGBuff;
        int32 AuthoredGModifierCount = 0, AuthoredGServerSequence = 0;
        int32 AuthoredGClientSequence[2] = {0, 0};
        uint8 AuthoredGExpectedPreClamp = 0, AuthoredGExpectedFinal = 0;
        int32 CombatEpoch = INDEX_NONE;
        TArray<FCk_Handle> Retired, BeforeFailureServer, BeforeFailureHost, BeforeFailureSurvivor;
        TWeakObjectPtr<AGameStateBase> ServerState;
        TWeakObjectPtr<UGameInstance> SurvivorInstance, DepartingInstance;
        TWeakObjectPtr<APlayerController> DepartingServerPC;
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

    auto IsLive(const FCk_Handle& H) -> bool
    {
        return ck::IsValid(H) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::Teardown) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::Destroyed);
    }
    auto State(UWorld* W, const FFixture& F) -> AGameStateBase*
    {
        auto* S = IsValid(W) ? W->GetGameState() : nullptr;
        return IsValid(S) && S->IsA(F.State) ? S : nullptr;
    }
    auto Data(AGameStateBase* S, const FFixture& F) -> const void*
    { return IsValid(S) ? F.Snapshot->ContainerPtrToValuePtr<void>(S) : nullptr; }
    auto Gen(AGameStateBase* S, const FFixture& F) -> int32
    { const auto* D = Data(S, F); return D ? F.Generation->GetPropertyValue_InContainer(D) : INDEX_NONE; }
    auto Seq(AGameStateBase* S, const FFixture& F) -> int32
    { const auto* D = Data(S, F); return D ? F.Sequence->GetPropertyValue_InContainer(D) : INDEX_NONE; }
    auto Ready(AGameStateBase* S, const FFixture& F, int32 Generation) -> bool
    {
        const auto* D = Data(S, F);
        return D && Gen(S, F) == Generation && Seq(S, F) > 0 &&
            F.Ready->GetPropertyValue_InContainer(D) && !F.Failed->GetPropertyValue_InContainer(D);
    }
    auto Failed(AGameStateBase* S, const FFixture& F, int32 Generation) -> bool
    {
        const auto* D = Data(S, F);
        return D && Gen(S, F) == Generation && !F.Ready->GetPropertyValue_InContainer(D) &&
            F.Failed->GetPropertyValue_InContainer(D);
    }
    auto ComponentFinal(const void* Attribute, const FStructProperty* Property, const FFixture& F, uint8& Out) -> bool
    {
        const auto* C = Property->ContainerPtrToValuePtr<void>(Attribute);
        if (!F.Present->GetPropertyValue_InContainer(C)) { Out = 0; return true; }
        Out = F.Final->GetPropertyValue_InContainer(C);
        return true;
    }
    auto Read(const void* D, const FFixture& F, FSample& Out) -> bool
    {
        if (!D) { return false; }
        FScriptArrayHelper Samples(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(D));
        if (Samples.Num() != 6) { return false; }
        Out = FSample{};
        for (int32 I = 0; I < 6; ++I)
        {
            const void* S = Samples.GetRawPtr(I);
            auto& V = Out.Stations[I];
            if (F.StationIndex->GetPropertyValue_InContainer(S) != I) { return false; }
            V.Auto = F.AutoRunning->GetPropertyValue_InContainer(S);
            V.Step = F.AutoStep->GetPropertyValue_InContainer(S);
            V.Total = F.TotalAttributes->GetPropertyValue_InContainer(S);
            V.Batches = F.CombatBatchCount->GetPropertyValue_InContainer(S);
            V.Epoch = F.BatchEpoch->GetPropertyValue_InContainer(S);
            V.Combat = F.CombatPresent->GetPropertyValue_InContainer(S);
            V.Changes = F.ValueChangeCount->GetPropertyValue_InContainer(S);
            V.Clamps = F.ClampedCount->GetPropertyValue_InContainer(S);
            V.BoundarySerial = F.BoundarySerial->GetPropertyValue_InContainer(S);
            V.LastPreClamp = F.LastPreClamp->GetPropertyValue_InContainer(S);
            V.LastClamped = F.LastClamped->GetPropertyValue_InContainer(S);
            V.LastOverflow = F.LastOverflow->GetPropertyValue_InContainer(S);
            V.Modifiers = F.ActiveModifierCount->GetPropertyValue_InContainer(S);
            V.DelegateMask = F.DelegateBoundMask->GetPropertyValue_InContainer(S);
            V.Title = F.Title->GetPropertyValue_InContainer(S);
            V.Body = F.Body->GetPropertyValue_InContainer(S);
            FScriptArrayHelper Items(F.Attributes, F.Attributes->ContainerPtrToValuePtr<void>(S));
            FScriptArrayHelper Deltas(F.ActiveModifierDeltas,
                F.ActiveModifierDeltas->ContainerPtrToValuePtr<void>(S));
            FScriptArrayHelper ClampEvents(F.ClampSerials,
                F.ClampSerials->ContainerPtrToValuePtr<void>(S));
            if (ClampEvents.Num() != (I == 0 ? 4 : 0)) { return false; }
            for (int32 J = 0; J < ClampEvents.Num(); ++J)
            {
                const int32 Serial = *reinterpret_cast<const int32*>(ClampEvents.GetRawPtr(J));
                if (Serial < 0) { return false; }
                V.ClampSerials.Add(Serial);
            }
            const int32 Expected = I == 0 ? 4 : I == 1 ? 2 : I == 3 ? 3 + 2 * V.Batches : 1;
            if (V.Step < 0 || V.Total < 0 || V.Batches < 0 || V.Batches > 1 ||
                V.Epoch < 0 || V.Changes < 0 || V.Clamps < 0 || V.Modifiers < 0 ||
                V.BoundarySerial < 0 || (I != 0 && V.BoundarySerial != 0) ||
                V.Title.IsEmpty() || V.Body.IsEmpty() || Items.Num() != Expected ||
                Deltas.Num() != V.Modifiers ||
                (I == 3 && (V.Total != Expected || V.Combat != (V.Batches == 1)))) { return false; }
            if (I == 3 &&
                (!V.Body.Contains(*FString::Printf(TEXT("Total Attributes: %d"), Expected)) ||
                 !V.Body.Contains(TEXT("RPG ATTRIBUTES (3)")) ||
                 V.Body.Contains(TEXT("COMBAT ATTRIBUTES (2)")) != (V.Batches == 1)))
            { return false; }
            if (I == 1 && !V.Body.Contains(*FString::Printf(
                TEXT("Active Modifiers: %d"), V.Modifiers))) { return false; }
            if (I == 4 && !V.Body.Contains(*FString::Printf(
                TEXT("MODIFIERS (%d)"), V.Modifiers))) { return false; }
            for (int32 J = 0; J < Deltas.Num(); ++J)
            { V.ModifierDeltas.Add(*static_cast<const uint8*>(Deltas.GetRawPtr(J))); }
            for (int32 J = 0; J < Items.Num(); ++J)
            {
                const void* A = Items.GetRawPtr(J);
                const auto N = F.Name->GetPropertyValue_InContainer(A);
                if (!AttributeNames[I][J] || N != FName(AttributeNames[I][J])) { return false; }
                uint8 Value = 0;
                if (!ComponentFinal(A, F.Current, F, Value)) { return false; }
                V.Names.Add(N); V.Final.Add(Value);
                const auto* Current = F.Current->ContainerPtrToValuePtr<void>(A);
                if (!F.Present->GetPropertyValue_InContainer(Current)) { return false; }
                V.Base.Add(F.Base->GetPropertyValue_InContainer(Current));
                V.PreClamp.Add(F.PreClampFinal->GetPropertyValue_InContainer(A));
                V.Overflow.Add(F.AttributeOverflow->GetPropertyValue_InContainer(A));
            }
            Out.Total += Items.Num();
        }
        return Out.Total == 12 + 2 * Out.Stations[3].Batches;
    }
    auto Read(AGameStateBase* S, const FFixture& F, FSample& Out) -> bool
    { return Read(Data(S, F), F, Out); }
    auto Collect(const FCk_Handle& H, FFeatures& Out) -> void
    {
        if (!IsLive(H)) { return; }
        if (UCk_Utils_ByteAttribute_UE::Has(H)) { ++Out.Attributes; Out.Handles.AddUnique(H); }
        if (UCk_Utils_ByteAttributeModifier_UE::Has(H)) { ++Out.Modifiers; Out.Handles.AddUnique(H); }
        if (UCk_Utils_Timer_UE::Has(H)) { ++Out.Timers; Out.Handles.AddUnique(H); }
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(H))
        { Collect(Child, Out); }
    }
    auto Children(UWorld* W, int32 I, TArray<FCk_Handle>& Originals,
                  TArray<FCk_Handle>& Projections, bool AllowMissingAnchor = false) -> bool
    {
        Originals.Reset(); Projections.Reset();
        const auto Root = IsValid(W) ? UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(W) : FCk_Handle{};
        if (!IsLive(Root)) { return false; }
        const auto Anchors = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, FName(AnchorNames[I]));
        if (Anchors.Num() != 1 || !IsLive(Anchors[0]))
        { return AllowMissingAnchor && Anchors.IsEmpty(); }
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Anchors[0]))
        {
            if (!IsLive(Child) || !UCk_Utils_EntityScript_UE::Has(Child)) { continue; }
            const auto Script = UCk_Utils_EntityScript_UE::CastChecked(Child);
            const auto* Class = UCk_Utils_EntityScript_UE::Get_ScriptClass(Script).Get();
            if (!IsValid(Class)) { return false; }
            if (Class->GetPathName() == OriginalPaths[I]) { Originals.Add(Child); }
            else if (Class->GetPathName() == ProjectionPath) { Projections.Add(Child); }
            else { return false; }
        }
        return true;
    }
    auto RecordMatches(const FCk_Handle& Root, int32 StationIndex,
                       int32 Expected, TArray<FCk_Handle>* Handles) -> bool
    {
        FCk_Handle Owner = Root;
        TArray<FCk_Handle> Recorded;
        UCk_Utils_ByteAttribute_UE::ForEach(Owner,
            TFunction<void(FCk_Handle_ByteAttribute)>([&](FCk_Handle_ByteAttribute A)
            { Recorded.AddUnique(FCk_Handle{A}); }));
        if (Recorded.Num() != Expected) { return false; }
        for (int32 J = 0; J < Expected; ++J)
        {
            const auto Tag = FGameplayTag::RequestGameplayTag(FName(AttributeNames[StationIndex][J]), false);
            const FCk_Handle Found{UCk_Utils_ByteAttribute_UE::TryGet(Root, Tag)};
            if (!Tag.IsValid() || !IsLive(Found) || !Recorded.Contains(Found)) { return false; }
            if (Handles) { Handles->AddUnique(Found); }
        }
        for (int32 J = Expected; J < 5; ++J)
        {
            if (!AttributeNames[StationIndex][J]) { continue; }
            const auto Tag = FGameplayTag::RequestGameplayTag(FName(AttributeNames[StationIndex][J]), false);
            if (Tag.IsValid() && ck::IsValid(FCk_Handle{UCk_Utils_ByteAttribute_UE::TryGet(Root, Tag)}))
            { return false; }
        }
        FFeatures Actual; Collect(Root, Actual);
        return Actual.Attributes == Expected;
    }
    auto Modifier(const FCk_Handle& Root, const TCHAR* AttributeName,
                  const TCHAR* ModifierName) -> FCk_Handle_ByteAttributeModifier
    {
        const auto AttributeTag = FGameplayTag::RequestGameplayTag(FName(AttributeName), false);
        const auto ModifierTag = FGameplayTag::RequestGameplayTag(FName(ModifierName), false);
        if (!IsLive(Root) || !AttributeTag.IsValid() || !ModifierTag.IsValid()) { return {}; }
        const auto Attribute = UCk_Utils_ByteAttribute_UE::TryGet(Root, AttributeTag);
        return ck::IsValid(Attribute) ?
            UCk_Utils_ByteAttributeModifier_UE::TryGet(Attribute, ModifierTag) :
            FCk_Handle_ByteAttributeModifier{};
    }
    constexpr uint8 BoundaryRaw[4] = {255, 30, 150, 255};
    constexpr uint8 BoundaryTarget[4] = {200, 50, 100, 150};
    constexpr uint8 BoundaryMin[4] = {0, 50, 0, 0};
    constexpr uint8 BoundaryMax[4] = {200, 255, 100, 150};

    auto BoundaryAttribute(const FCk_Handle& Root, int32 Index) -> FCk_Handle_ByteAttribute
    {
        if (!IsLive(Root) || Index < 0 || Index >= 4) { return {}; }
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(AttributeNames[0][Index]), false);
        if (!Tag.IsValid()) { return {}; }
        const auto Attribute = UCk_Utils_ByteAttribute_UE::TryGet(Root, Tag);
        if (!IsLive(FCk_Handle{Attribute})) { return {}; }
        for (const auto Part : {ECk_MinMaxCurrent::Min, ECk_MinMaxCurrent::Max,
            ECk_MinMaxCurrent::Current})
        { if (!UCk_Utils_ByteAttribute_UE::Has_Component(Attribute, Part)) { return {}; } }
        if (UCk_Utils_ByteAttribute_UE::Get_BaseValue(Attribute, ECk_MinMaxCurrent::Min) != BoundaryMin[Index] ||
            UCk_Utils_ByteAttribute_UE::Get_FinalValue(Attribute, ECk_MinMaxCurrent::Min) != BoundaryMin[Index] ||
            UCk_Utils_ByteAttribute_UE::Get_BaseValue(Attribute, ECk_MinMaxCurrent::Max) != BoundaryMax[Index] ||
            UCk_Utils_ByteAttribute_UE::Get_FinalValue(Attribute, ECk_MinMaxCurrent::Max) != BoundaryMax[Index] ||
            UCk_Utils_ByteAttribute_UE::Get_BonusValue(Attribute) != 0)
        { return {}; }
        return Attribute;
    }
    auto BoundaryBefore(const FCk_Handle& Root, const FSample& Sample) -> bool
    {
        for (const auto& V : Sample.Stations) { if (V.Auto) { return false; } }
        const auto& C = Sample.Stations[0];
        if (C.Base.Num() != 4 || C.Final.Num() != 4 || C.ClampSerials.Num() != 4 ||
            !RecordMatches(Root, 0, 4, nullptr)) { return false; }
        for (int32 I = 0; I < 4; ++I)
        {
            const auto Attribute = BoundaryAttribute(Root, I);
            if (!IsLive(FCk_Handle{Attribute}) ||
                UCk_Utils_ByteAttribute_UE::Get_BaseValue(Attribute) != C.Base[I] ||
                UCk_Utils_ByteAttribute_UE::Get_FinalValue(Attribute) != C.Final[I])
            { return false; }
        }
        return true;
    }
    auto LiveBoundaryValues(const FCk_Handle& Root) -> bool
    {
        for (int32 I = 0; I < 4; ++I)
        {
            const auto Attribute = BoundaryAttribute(Root, I);
            if (!IsLive(FCk_Handle{Attribute}) ||
                UCk_Utils_ByteAttribute_UE::Get_BaseValue(Attribute) != BoundaryTarget[I] ||
                UCk_Utils_ByteAttribute_UE::Get_FinalValue(Attribute) != BoundaryTarget[I] ||
                UCk_Utils_ByteAttribute_UE::Get_PreClampFinalValue(Attribute) != BoundaryRaw[I] ||
                UCk_Utils_ByteAttribute_UE::Get_ClampOverflow(Attribute) !=
                    static_cast<uint8>(BoundaryRaw[I] - BoundaryTarget[I]))
            { return false; }
        }
        return true;
    }
    auto BoundaryAfter(const FStation& C, const FStation& Before) -> bool
    {
        if (C.Auto || C.Base.Num() != 4 || C.Final.Num() != 4 ||
            C.PreClamp.Num() != 4 || C.Overflow.Num() != 4 ||
            C.ClampSerials.Num() != 4 || Before.ClampSerials.Num() != 4 ||
            Before.Base.Num() != 4 || Before.Final.Num() != 4 ||
            C.BoundarySerial != Before.BoundarySerial + 1) { return false; }
        int32 ExpectedEvents = 0;
        bool PayloadMatchesEvent = false;
        for (int32 J = 0; J < 4; ++J)
        {
            // The processor emits a clamped signal when post-clamp Base or Final
            // changes. R resumes auto, so derive this from the later paused state.
            const bool Expected = Before.Base[J] != BoundaryTarget[J] ||
                Before.Final[J] != BoundaryTarget[J];
            ExpectedEvents += Expected ? 1 : 0;
            if (C.Base[J] != BoundaryTarget[J] || C.Final[J] != BoundaryTarget[J] ||
                C.PreClamp[J] != BoundaryRaw[J] ||
                C.Overflow[J] != static_cast<uint8>(BoundaryRaw[J] - BoundaryTarget[J]) ||
                C.ClampSerials[J] != Before.ClampSerials[J] + (Expected ? 1 : 0))
            { return false; }
            if (Expected && C.LastPreClamp == BoundaryRaw[J] &&
                C.LastClamped == BoundaryTarget[J] &&
                C.LastOverflow == static_cast<uint8>(BoundaryRaw[J] - BoundaryTarget[J]))
            { PayloadMatchesEvent = true; }
        }
        return C.Clamps == Before.Clamps + ExpectedEvents && (ExpectedEvents > 0 ?
            PayloadMatchesEvent : C.LastPreClamp == Before.LastPreClamp &&
                C.LastClamped == Before.LastClamped && C.LastOverflow == Before.LastOverflow);
    }
    auto Branches(UWorld* W, const FFixture& F, bool Server, bool Dedicated,
                  const FSample& Sample, TArray<FCk_Handle>* Simulation = nullptr,
                  TArray<FCk_Handle>* Local = nullptr) -> bool
    {
        if (!IsValid(W)) { return false; }
        int32 AttributeCount = 0, TimerCount = 0, ModifierCount = 0;
        for (int32 I = 0; I < 6; ++I)
        {
            TArray<FCk_Handle> O, P;
            if (!Children(W, I, O, P) || O.Num() != (Server ? 1 : 0) ||
                P.Num() != (Server && Dedicated ? 0 : 1)) { return false; }
            if (Server)
            {
                FFeatures A; Collect(O[0], A);
                const int32 Expected = I == 0 ? 4 : I == 1 ? 2 : I == 3 ?
                    3 + 2 * Sample.Stations[3].Batches : 1;
                if (A.Attributes != Expected || A.Timers != (I == 0 ? 2 : 1)) { return false; }
                if (!RecordMatches(O[0], I, Expected, nullptr)) { return false; }
                AttributeCount += A.Attributes; TimerCount += A.Timers; ModifierCount += A.Modifiers;
                if (Simulation) { Simulation->AddUnique(O[0]); for (const auto& H : A.Handles) { Simulation->AddUnique(H); } }
                auto* S = State(W, F);
                if (!IsValid(S)) { return false; }
                FScriptArrayHelper Slots(F.ServerChildren, F.ServerChildren->ContainerPtrToValuePtr<void>(S));
                if (Slots.Num() != 6 || *reinterpret_cast<const FCk_Handle*>(Slots.GetRawPtr(I)) != O[0]) { return false; }
            }
            for (const auto& Root : P)
            {
                FFeatures A; Collect(Root, A);
                if (A.Attributes != 0 || A.Modifiers != 0 || A.Timers != 1) { return false; }
                if (Local) { Local->AddUnique(Root); for (const auto& H : A.Handles) { Local->AddUnique(H); } }
            }
        }
        return !Server || (AttributeCount == Sample.Total && TimerCount == 7 &&
            ModifierCount >= Sample.Stations[1].Modifiers + Sample.Stations[4].Modifiers &&
            F.ServerChildCount->GetPropertyValue_InContainer(State(W, F)) == 6);
    }
    auto NoBranches(UWorld* W) -> bool
    {
        const auto Root = IsValid(W) ? UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(W) : FCk_Handle{};
        if (!IsLive(Root)) { return false; }
        for (int32 I = 0; I < 6; ++I)
        {
            for (const auto& H : UCk_Utils_EntityTag_UE::ForEach_Entity(Root, FName(OriginalTags[I])))
            { if (IsLive(H)) { return false; } }
            TArray<FCk_Handle> O, P;
            if (!Children(W, I, O, P, true) || !O.IsEmpty() || !P.IsEmpty()) { return false; }
            const auto Anchors = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, FName(AnchorNames[I]));
            if (Anchors.Num() == 1)
            {
                int32 StationTicks = 0;
                FFeatures Unexpected;
                for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Anchors[0]))
                {
                    if (IsLive(Child) && UCk_Utils_Timer_UE::Has(Child) &&
                        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Anchors[0])
                    { ++StationTicks; continue; }
                    Collect(Child, Unexpected);
                }
                if (StationTicks != 1 || Unexpected.Attributes || Unexpected.Modifiers || Unexpected.Timers)
                { return false; }
            }
        }
        for (const auto& H : UCk_Utils_EntityTag_UE::ForEach_Entity(Root,
            FName(TEXT("TAG_AttributeGym_ByteProjection"))))
        { if (IsLive(H)) { return false; } }
        return true;
    }
    auto Catalog(UWorld* Server, UWorld* Client) -> bool
    {
        auto* SG = IsValid(Server) ? Server->GetGameInstance() : nullptr;
        auto* CG = IsValid(Client) ? Client->GetGameInstance() : nullptr;
        const auto* S = IsValid(SG) ? SG->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
        const auto* C = IsValid(CG) ? CG->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
        if (!IsValid(S) || !IsValid(C)) { return false; }
        const int32 Index = S->Find_GymIndexByName(TEXT("Attribute Byte"));
        return Index != INDEX_NONE && S->Get_CurrentGymIndex() == Index &&
            C->Get_CurrentGymIndex() == Index && S->Get_Entries().Num() == C->Get_Entries().Num();
    }
    auto Remember(FFixture& F, AGameStateBase* S) -> void
    {
        if (F.HistoryGeneration != Gen(S, F))
        { F.History.Reset(); F.HistoryGeneration = Gen(S, F); }
        if (!Ready(S, F, F.HistoryGeneration)) { return; }
        const int32 Sequence = Seq(S, F);
        for (const auto& Item : F.History)
        {
            if (Item.Generation != F.HistoryGeneration || Item.Sequence != Sequence)
            { continue; }
            if ((!Item.Copy.IsValid() ||
                !F.Snapshot->Identical(Data(S, F), Item.Copy->GetStructMemory(), 0)) &&
                !F.HistoryViolation)
            {
                F.HistoryViolation = true;
                F.TransactionViolation = true;
                if (F.Test)
                { F.Test->AddError(TEXT("Byte snapshot changed without a new Sequence")); }
            }
            return;
        }
        if (F.History.Num() == 96) { F.History.RemoveAt(0); }
        auto& Item = F.History.AddDefaulted_GetRef();
        Item.Generation = F.HistoryGeneration;
        Item.Sequence = Sequence;
        Item.Copy = MakeShared<FStructOnScope>(F.Snapshot->Struct.Get());
        F.Snapshot->Struct->CopyScriptStruct(Item.Copy->GetStructMemory(), Data(S, F));
    }
    auto MatchesHistory(AGameStateBase* S, const FFixture& F, int32 After) -> bool
    {
        if (F.HistoryViolation || !IsValid(S) || Seq(S, F) <= After) { return false; }
        for (const auto& Item : F.History)
        {
            if (Item.Generation == Gen(S, F) && Item.Sequence == Seq(S, F) &&
                Item.Copy.IsValid() &&
                F.Snapshot->Identical(Data(S, F), Item.Copy->GetStructMemory(), 0))
            { return true; }
        }
        return false;
    }
    auto WorldReady(UWorld* W, FFixture& F, int32 G, bool Server, bool Dedicated) -> bool
    {
        auto* S = State(W, F);
        FSample Sample;
        if (!Ready(S, F, G) || !Read(S, F, Sample) || !Branches(W, F, Server, Dedicated, Sample)) { return false; }
        if (!Server && (F.LocalGeneration->GetPropertyValue_InContainer(S) != G ||
            !F.LocalReady->GetPropertyValue_InContainer(S) ||
            F.LocalFailed->GetPropertyValue_InContainer(S))) { return false; }
        return true;
    }
    auto AllReady(FFixture& F, int32 G, bool Dedicated, bool SurvivorOnly = false) -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        auto* C0 = SurvivorOnly ? nullptr : ck::auto_test::net::Get_ClientWorld(0);
        auto* C1 = SurvivorOnly ?
            (F.SurvivorInstance.IsValid() ? F.SurvivorInstance->GetWorld() : nullptr) :
            ck::auto_test::net::Get_ClientWorld(1);
        if ((!SurvivorOnly && ck::auto_test::net::Get_AllPIEWorlds().Num() != 3) ||
            !WorldReady(Server, F, G, true, Dedicated) ||
            (!SurvivorOnly && (!WorldReady(C0, F, G, false, Dedicated) || !Catalog(Server, C0))) ||
            !WorldReady(C1, F, G, false, Dedicated) || !Catalog(Server, C1)) { return false; }
        Remember(F, State(Server, F));
        return (SurvivorOnly || MatchesHistory(State(C0, F), F, 0)) &&
            MatchesHistory(State(C1, F), F, 0);
    }
    auto CaptureBefore(FFixture& F) -> bool
    {
        auto* S = State(ck::auto_test::net::Get_ServerWorld(), F);
        if (!Read(S, F, F.Before) || Seq(S, F) <= 0) { return false; }
        F.BeforeServerSequence = Seq(S, F);
        F.BeforeSnapshot = MakeShared<FStructOnScope>(F.Snapshot->Struct.Get());
        F.Snapshot->Struct->CopyScriptStruct(F.BeforeSnapshot->GetStructMemory(), Data(S, F));
        F.TransactionViolation = false;
        for (int32 I = 0; I < 2; ++I)
        {
            auto* W = ck::auto_test::net::Get_ClientWorld(I);
            auto* C = State(W, F);
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            if (!IsValid(PC) || !PC->IsA(F.Controller) || !IsValid(C)) { return false; }
            F.BeforeSequence[I] = Seq(C, F);
            F.BeforeAccepted[I] = F.LastSerial->GetPropertyValue_InContainer(PC);
        }
        return true;
    }
    auto Invoke(APlayerController* PC, const FFixture& F, int32 Serial, int32 Action, int32 Preset) -> bool
    {
        if (!IsValid(PC) || !PC->IsA(F.Controller)) { return false; }
        FStructOnScope Params(F.Rpc);
        auto* P = Params.GetStructMemory();
        F.RpcSerial->SetPropertyValue_InContainer(P, Serial);
        F.RpcAction->SetPropertyValue_InContainer(P, Action);
        F.RpcPreset->SetPropertyValue_InContainer(P, Preset);
        PC->ProcessEvent(F.Rpc, P);
        return true;
    }
    auto Result(UWorld* W, const FFixture& F, int32 Attempt, int32 Accepted, int32 Code) -> bool
    {
        auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
        return IsValid(PC) && PC->IsA(F.Controller) &&
            F.AttemptSerial->GetPropertyValue_InContainer(PC) == Attempt &&
            F.LastSerial->GetPropertyValue_InContainer(PC) == Accepted &&
            F.Result->GetPropertyValue_InContainer(PC) == Code;
    }
    auto IsAuthoredReset(const FSample& S) -> bool
    {
        for (const auto& Station : S.Stations)
        { if (!Station.Auto || Station.Step != 0) { return false; } }
        return S.Total == 12 && S.Stations[3].Batches == 0 &&
            !S.Stations[3].Combat && S.Stations[3].Total == 3 &&
            S.Stations[0].Final.Num() == 4 &&
            S.Stations[0].Final[0] == 100 && S.Stations[0].Final[1] == 150 &&
            S.Stations[0].Final[2] == 75 && S.Stations[0].Final[3] == 150 &&
            S.Stations[1].Final.Num() == 2 &&
            S.Stations[1].ModifierDeltas == TArray<uint8>{25} &&
            S.Stations[1].Final[0] == 75 && S.Stations[1].Final[1] == 30 &&
            S.Stations[2].Final.Num() == 1 && S.Stations[2].Final[0] == 100 &&
            S.Stations[3].Final.Num() == 3 && S.Stations[3].Final[0] == 85 &&
            S.Stations[3].Final[1] == 120 && S.Stations[3].Final[2] == 95 &&
            S.Stations[4].Final.Num() == 1 && S.Stations[4].Final[0] == 75 &&
            S.Stations[4].Modifiers == 0 &&
            S.Stations[5].Final.Num() == 1 && S.Stations[5].Final[0] == 100 &&
            S.Stations[5].DelegateMask == 0;
    }
    auto ResetCommitMatches(AGameStateBase* Authority, AGameStateBase* Client,
        const FFixture& F, int32 Serial, int32 BeforeSequence) -> bool
    {
        if (!IsValid(Authority) || !IsValid(Client)) { return false; }
        const auto* A = F.ControlCommit->ContainerPtrToValuePtr<void>(Authority);
        const auto* C = F.ControlCommit->ContainerPtrToValuePtr<void>(Client);
        const auto* D = F.CommitSnapshot->ContainerPtrToValuePtr<void>(A);
        FSample Sample;
        return F.CommitSerial->GetPropertyValue_InContainer(A) == Serial &&
            F.CommitAction->GetPropertyValue_InContainer(A) == 18 &&
            F.CommitPreset->GetPropertyValue_InContainer(A) == -1 &&
            F.Generation->GetPropertyValue_InContainer(D) == F.InitialGeneration &&
            F.Sequence->GetPropertyValue_InContainer(D) > BeforeSequence &&
            F.Ready->GetPropertyValue_InContainer(D) && !F.Failed->GetPropertyValue_InContainer(D) &&
            F.ControlCommit->Identical(A, C, 0) && Read(D, F, Sample) && IsAuthoredReset(Sample);
    }
    auto Describe(UWorld* W, const FFixture& F) -> FString
    {
        auto* S = State(W, F);
        const auto* D = Data(S, F);
        FSample Sample;
        return FString::Printf(TEXT("Byte world=%s gen=%d seq=%d ready=%d failed=%d stations=%d total=%d combat=%d local=%d/%d/%d"),
            *GetNameSafe(W), Gen(S, F), Seq(S, F),
            D ? F.Ready->GetPropertyValue_InContainer(D) : 0,
            D ? F.Failed->GetPropertyValue_InContainer(D) : 0,
            Read(S, F, Sample), Sample.Total, Sample.Stations[3].Batches,
            IsValid(S) ? F.LocalGeneration->GetPropertyValue_InContainer(S) : -1,
            IsValid(S) ? F.LocalReady->GetPropertyValue_InContainer(S) : 0,
            IsValid(S) ? F.LocalFailed->GetPropertyValue_InContainer(S) : 0);
    }
    auto DescribeParts(AGameStateBase* S, const FFixture& F, int32 Station) -> FString
    {
        const auto* D = Data(S, F);
        if (!D || (Station != 0 && Station != 2 && Station != 5))
        { return TEXT("sample=unavailable"); }
        FScriptArrayHelper Rows(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(D));
        if (Rows.Num() != 6) { return TEXT("sample=bad-station-count"); }
        const void* Row = Rows.GetRawPtr(Station);
        FScriptArrayHelper Attributes(F.Attributes, F.Attributes->ContainerPtrToValuePtr<void>(Row));
        if (Attributes.Num() < 1) { return TEXT("sample=no-attribute"); }
        const void* Attribute = Attributes.GetRawPtr(0);
        const FStructProperty* Parts[3] = {F.Min, F.Max, F.Current};
        FString Text;
        for (int32 I = 0; I < 3; ++I)
        {
            const void* Part = Parts[I]->ContainerPtrToValuePtr<void>(Attribute);
            const bool Present = F.Present->GetPropertyValue_InContainer(Part);
            Text += FString::Printf(TEXT(" %s=%s"),
                I == 0 ? TEXT("Min") : I == 1 ? TEXT("Max") : TEXT("Current"),
                Present ? *FString::Printf(TEXT("%u/%u"),
                    F.Base->GetPropertyValue_InContainer(Part),
                    F.Final->GetPropertyValue_InContainer(Part)) : TEXT("absent"));
        }
        return Text;
    }
    auto DescribeLiveParts(const FCk_Handle& Root, int32 Station) -> FString
    {
        if (!IsLive(Root) || (Station != 2 && Station != 5))
        { return TEXT("live=unavailable"); }
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(AttributeNames[Station][0]), false);
        const auto Attribute = Tag.IsValid() ?
            UCk_Utils_ByteAttribute_UE::TryGet(Root, Tag) : FCk_Handle_ByteAttribute{};
        if (!IsLive(FCk_Handle{Attribute})) { return TEXT("live=no-attribute"); }
        const ECk_MinMaxCurrent Parts[3] = {
            ECk_MinMaxCurrent::Min, ECk_MinMaxCurrent::Max, ECk_MinMaxCurrent::Current};
        FString Text;
        for (int32 I = 0; I < 3; ++I)
        {
            Text += FString::Printf(TEXT(" %s=%s"),
                I == 0 ? TEXT("Min") : I == 1 ? TEXT("Max") : TEXT("Current"),
                UCk_Utils_ByteAttribute_UE::Has_Component(Attribute, Parts[I]) ?
                    *FString::Printf(TEXT("%u/%u"),
                        UCk_Utils_ByteAttribute_UE::Get_BaseValue(Attribute, Parts[I]),
                        UCk_Utils_ByteAttribute_UE::Get_FinalValue(Attribute, Parts[I])) :
                    TEXT("absent"));
        }
        return Text;
    }
    auto DescribeControlWait(const FFixture& F, bool Dedicated, int32 Owner,
        int32 Serial, int32 Action, int32 Preset) -> FString
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        auto* S = State(Server, F);
        FSample ServerSample;
        const bool ServerRead = Read(S, F, ServerSample);
        FString Failing;
        const auto Mark = [&Failing](bool Pass, const TCHAR* Name)
        { if (!Pass) { Failing += FString::Printf(TEXT(" %s"), Name); } };
        Mark(Ready(S, F, F.InitialGeneration), TEXT("server-ready"));
        Mark(ServerRead, TEXT("server-read"));
        Mark(Seq(S, F) > F.BeforeServerSequence, TEXT("server-seq-new"));
        Mark(!F.TransactionViolation, TEXT("transaction-coherent"));
        Mark(!F.HistoryViolation, TEXT("history-coherent"));
        Mark(ServerRead && Branches(Server, F, true, Dedicated, ServerSample),
            TEXT("server-branches"));
        if (Action == 0 && ServerRead)
        {
            for (const auto& Station : ServerSample.Stations)
            { Mark(!Station.Auto, TEXT("server-auto-paused")); }
        }
        FString Text = FString::Printf(TEXT("Byte wait action=%d preset=%d owner=%d serial=%d %s"),
            Action, Preset, Owner, Serial, Dedicated ? TEXT("ClientServer") : TEXT("ListenServer"));
        for (int32 I = 0; I < 2; ++I)
        {
            auto* AuthorityPC = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
            const bool PCValid = IsValid(AuthorityPC) && AuthorityPC->IsA(F.Controller);
            Mark(PCValid, I == 0 ? TEXT("authority0-PC") : TEXT("authority1-PC"));
            Text += FString::Printf(TEXT(" authority%d{ack=%d/%d/%d rings="), I,
                PCValid ? F.AttemptSerial->GetPropertyValue_InContainer(AuthorityPC) : -1,
                PCValid ? F.LastSerial->GetPropertyValue_InContainer(AuthorityPC) : -1,
                PCValid ? F.Result->GetPropertyValue_InContainer(AuthorityPC) : -1);
            for (int32 J = 0; J < 4; ++J)
            { Text += FString::Printf(TEXT("%s%d"), J ? TEXT(",") : TEXT(""),
                PCValid ? F.Ring[J]->GetPropertyValue_InContainer(AuthorityPC) : -99); }
            Text += TEXT("}");
        }
        Text += FString::Printf(TEXT(" server-seq=%d before=%d roots="), Seq(S, F), F.BeforeServerSequence);
        for (int32 I = 0; I < 6; ++I)
        { Text += IsLive(F.ServerRoots[I]) ? TEXT("1") : TEXT("0"); }
        if (ServerRead)
        {
            Text += FString::Printf(TEXT(" server-auto=%d%d%d%d%d%d clamps=%d boundary=%d"),
                ServerSample.Stations[0].Auto, ServerSample.Stations[1].Auto,
                ServerSample.Stations[2].Auto, ServerSample.Stations[3].Auto,
                ServerSample.Stations[4].Auto, ServerSample.Stations[5].Auto,
                ServerSample.Stations[0].Clamps, ServerSample.Stations[0].BoundarySerial);
        }
        for (int32 I = 0; I < 2; ++I)
        {
            auto* W = ck::auto_test::net::Get_ClientWorld(I);
            auto* C = State(W, F);
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            FSample ClientSample;
            const bool ClientRead = Read(C, F, ClientSample);
            const bool PCValid = IsValid(PC) && PC->IsA(F.Controller);
            Text += FString::Printf(TEXT(" client%d{seq=%d before=%d ready=%d read=%d history=%d ack=%d/%d/%d rings="),
                I, Seq(C, F), F.BeforeSequence[I],
                Ready(C, F, F.InitialGeneration), ClientRead,
                MatchesHistory(C, F, F.BeforeSequence[I]),
                PCValid ? F.AttemptSerial->GetPropertyValue_InContainer(PC) : -1,
                PCValid ? F.LastSerial->GetPropertyValue_InContainer(PC) : -1,
                PCValid ? F.Result->GetPropertyValue_InContainer(PC) : -1);
            for (int32 J = 0; J < 4; ++J)
            { Text += FString::Printf(TEXT("%s%d"), J ? TEXT(",") : TEXT(""),
                PCValid ? F.Ring[J]->GetPropertyValue_InContainer(PC) : -99); }
            Text += FString::Printf(TEXT(" auto=%d%d%d%d%d%d clamps=%d}"),
                ClientRead ? ClientSample.Stations[0].Auto : -1,
                ClientRead ? ClientSample.Stations[1].Auto : -1,
                ClientRead ? ClientSample.Stations[2].Auto : -1,
                ClientRead ? ClientSample.Stations[3].Auto : -1,
                ClientRead ? ClientSample.Stations[4].Auto : -1,
                ClientRead ? ClientSample.Stations[5].Auto : -1,
                ClientRead ? ClientSample.Stations[0].Clamps : -1);
            Mark(PCValid, I == 0 ? TEXT("client0-PC") : TEXT("client1-PC"));
            Mark(ClientRead, I == 0 ? TEXT("client0-read") : TEXT("client1-read"));
            Mark(Ready(C, F, F.InitialGeneration),
                I == 0 ? TEXT("client0-ready") : TEXT("client1-ready"));
            Mark(ClientRead && Branches(W, F, false, Dedicated, ClientSample),
                I == 0 ? TEXT("client0-branches") : TEXT("client1-branches"));
            Mark(MatchesHistory(C, F, F.BeforeSequence[I]),
                I == 0 ? TEXT("client0-history") : TEXT("client1-history"));
            Mark(Catalog(Server, W), I == 0 ? TEXT("client0-catalog") : TEXT("client1-catalog"));
            if (I == Owner)
            { Mark(Result(W, F, Serial, Serial, 1), TEXT("owner-accepted")); }
            if (Action == 0 && ClientRead)
            {
                for (const auto& Station : ClientSample.Stations)
                { Mark(!Station.Auto, I == 0 ? TEXT("client0-auto-paused") : TEXT("client1-auto-paused")); }
            }
        }
        Text += FString::Printf(TEXT(" fail=[%s]"), *Failing);
        return Text;
    }
}
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkAttributeByteGym_Network,
    "Ck.Gym.Network.AttributeByte.SharedSix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkAttributeByteGym_Network::RunTest(const FString& Parameters)
{
    using namespace ck_attribute_byte_network_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Attribute Byte network topology")); return false; }
    const auto F = MakeShared<FFixture>();
    F->Test = this;
    F->Mode = FSoftClassPath{ModePath}.TryLoadClass<AGameModeBase>();
    F->State = FSoftClassPath{StatePath}.TryLoadClass<AGameStateBase>();
    F->Controller = FSoftClassPath{ControllerPath}.TryLoadClass<APlayerController>();
    bool Valid = true;
    Valid &= TestNotNull(TEXT("Byte mode"), F->Mode);
    Valid &= TestNotNull(TEXT("Byte state"), F->State);
    Valid &= TestNotNull(TEXT("Byte controller"), F->Controller);
    for (int32 I = 0; I < 6; ++I)
    { Valid &= TestNotNull(TEXT("original Byte script"), FSoftClassPath{OriginalPaths[I]}.TryLoadClass<UObject>()); }
    Valid &= TestNotNull(TEXT("Byte projection"), FSoftClassPath{ProjectionPath}.TryLoadClass<UObject>());
    if (!Valid) { return false; }
    F->Snapshot = FindFProperty<FStructProperty>(F->State, TEXT("Snapshot"));
    F->ControlCommit = FindFProperty<FStructProperty>(F->State, TEXT("LastControlCommit"));
    if (!TestNotNull(TEXT("Byte correlated control commit"), F->ControlCommit)) { return false; }
    const auto* Commit = F->ControlCommit->Struct.Get();
    F->CommitSnapshot = FindFProperty<FStructProperty>(Commit, TEXT("Snapshot"));
    F->CommitSerial = FindFProperty<FIntProperty>(Commit, TEXT("Serial"));
    F->CommitAction = FindFProperty<FIntProperty>(Commit, TEXT("Action"));
    F->CommitPreset = FindFProperty<FIntProperty>(Commit, TEXT("Preset"));
    if (!TestNotNull(TEXT("Byte Snapshot"), F->Snapshot)) { return false; }
    auto* Snap = F->Snapshot->Struct.Get();
    F->Generation = FindFProperty<FIntProperty>(Snap, TEXT("Generation"));
    F->Sequence = FindFProperty<FIntProperty>(Snap, TEXT("Sequence"));
    F->Ready = FindFProperty<FBoolProperty>(Snap, TEXT("Ready"));
    F->Failed = FindFProperty<FBoolProperty>(Snap, TEXT("Failed"));
    F->FailureReason = FindFProperty<FStrProperty>(Snap, TEXT("FailureReason"));
    F->Stations = FindFProperty<FArrayProperty>(Snap, TEXT("Stations"));
    Valid &= TestNotNull(TEXT("Snapshot.Stations"), F->Stations);
    if (!Valid) { return false; }
    auto* StationInner = CastField<FStructProperty>(F->Stations->Inner);
    Valid &= TestNotNull(TEXT("typed Byte station"), StationInner);
    if (!Valid) { return false; }
    auto* Station = StationInner->Struct.Get();
    F->StationIndex = FindFProperty<FIntProperty>(Station, TEXT("StationIndex"));
    F->AutoRunning = FindFProperty<FBoolProperty>(Station, TEXT("AutoRunning"));
    F->AutoStep = FindFProperty<FIntProperty>(Station, TEXT("AutoStep"));
    F->Attributes = FindFProperty<FArrayProperty>(Station, TEXT("Attributes"));
    F->ValueChangeCount = FindFProperty<FIntProperty>(Station, TEXT("ValueChangeCount"));
    F->ClampedCount = FindFProperty<FIntProperty>(Station, TEXT("ClampedCount"));
    F->LastPreClamp = FindFProperty<FByteProperty>(Station, TEXT("LastPreClampValue"));
    F->LastClamped = FindFProperty<FByteProperty>(Station, TEXT("LastClampedValue"));
    F->LastOverflow = FindFProperty<FByteProperty>(Station, TEXT("LastOverflow"));
    F->ClampSerials = FindFProperty<FArrayProperty>(Station, TEXT("ClampSerials"));
    F->BoundarySerial = FindFProperty<FIntProperty>(Station, TEXT("BoundarySerial"));
    F->ActiveModifierCount = FindFProperty<FIntProperty>(Station, TEXT("ActiveModifierCount"));
    F->ActiveModifierDeltas = FindFProperty<FArrayProperty>(Station, TEXT("ActiveModifierDeltas"));
    F->DelegateBoundMask = FindFProperty<FIntProperty>(Station, TEXT("DelegateBoundMask"));
    F->TotalAttributes = FindFProperty<FIntProperty>(Station, TEXT("TotalAttributes"));
    F->CombatBatchCount = FindFProperty<FIntProperty>(Station, TEXT("CombatBatchCount"));
    F->BatchEpoch = FindFProperty<FIntProperty>(Station, TEXT("BatchEpoch"));
    F->CombatPresent = FindFProperty<FBoolProperty>(Station, TEXT("CombatPresent"));
    F->Title = FindFProperty<FStrProperty>(Station, TEXT("Title"));
    F->Body = FindFProperty<FStrProperty>(Station, TEXT("Body"));
    Valid &= TestNotNull(TEXT("Station.Attributes"), F->Attributes);
    if (!Valid) { return false; }
    F->Attribute = CastField<FStructProperty>(F->Attributes->Inner);
    Valid &= TestNotNull(TEXT("typed Byte attribute"), F->Attribute);
    if (!Valid) { return false; }
    auto* A = F->Attribute->Struct.Get();
    F->PreClampFinal = FindFProperty<FByteProperty>(A, TEXT("PreClampFinal"));
    F->AttributeOverflow = FindFProperty<FByteProperty>(A, TEXT("ClampOverflow"));
    F->Name = FindFProperty<FNameProperty>(A, TEXT("Name"));
    F->Min = FindFProperty<FStructProperty>(A, TEXT("Min"));
    F->Max = FindFProperty<FStructProperty>(A, TEXT("Max"));
    F->Current = FindFProperty<FStructProperty>(A, TEXT("Current"));
    Valid &= TestNotNull(TEXT("Attribute.Current"), F->Current);
    if (!Valid) { return false; }
    auto* C = F->Current->Struct.Get();
    F->Present = FindFProperty<FBoolProperty>(C, TEXT("Present"));
    F->Base = FindFProperty<FByteProperty>(C, TEXT("Base"));
    F->Bonus = FindFProperty<FByteProperty>(C, TEXT("Bonus"));
    F->Final = FindFProperty<FByteProperty>(C, TEXT("Final"));
    F->ServerChildren = FindFProperty<FArrayProperty>(F->State, TEXT("ServerChildren"));
    F->ServerChildCount = FindFProperty<FIntProperty>(F->State, TEXT("ServerChildCount"));
    F->LocalGeneration = FindFProperty<FIntProperty>(F->State, TEXT("LocalProjectionGeneration"));
    F->LocalReady = FindFProperty<FBoolProperty>(F->State, TEXT("LocalProjectionReady"));
    F->LocalFailed = FindFProperty<FBoolProperty>(F->State, TEXT("LocalProjectionFailed"));
    F->LocalFailureReason = FindFProperty<FStrProperty>(F->State, TEXT("LocalProjectionFailureReason"));
    F->LastSerial = FindFProperty<FIntProperty>(F->Controller, TEXT("LastControlSerial"));
    F->AttemptSerial = FindFProperty<FIntProperty>(F->Controller, TEXT("LastControlAttemptSerial"));
    F->Result = FindFProperty<FIntProperty>(F->Controller, TEXT("LastControlResult"));
    for (int32 I = 0; I < 4; ++I)
    {
        const TCHAR* Names[4] = {TEXT("MinPresetIndex"), TEXT("MaxPresetIndex"),
            TEXT("CurrentPresetIndex"), TEXT("SignalPresetIndex")};
        F->Ring[I] = FindFProperty<FIntProperty>(F->Controller, Names[I]);
        Valid &= TestNotNull(TEXT("per-owner ring"), F->Ring[I]);
    }
    F->Rpc = F->Controller->FindFunctionByName(TEXT("Server_RequestByteControl"));
    Valid &= TestNotNull(TEXT("Byte RPC"), F->Rpc);
    if (!Valid) { return false; }
    F->RpcSerial = FindFProperty<FIntProperty>(F->Rpc, TEXT("InSerial"));
    F->RpcAction = FindFProperty<FIntProperty>(F->Rpc, TEXT("InAction"));
    F->RpcPreset = FindFProperty<FIntProperty>(F->Rpc, TEXT("InPresetIndex"));
    FStructProperty* ChildHandle = F->ServerChildren
        ? CastField<FStructProperty>(F->ServerChildren->Inner) : nullptr;
    Valid &= TestTrue(TEXT("exact replicated Byte snapshot and RPC schema"),
        F->Snapshot->HasAnyPropertyFlags(CPF_Net) &&
        F->ControlCommit->HasAnyPropertyFlags(CPF_Net) &&
        F->CommitSnapshot && F->CommitSnapshot->Struct == Snap &&
        F->CommitSerial && F->CommitAction && F->CommitPreset &&
        Snap->GetName() == TEXT("CkAttributeGym_ByteSnapshot") &&
        Station->GetName() == TEXT("CkAttributeGym_ByteStationSample") &&
        A->GetName() == TEXT("CkAttributeGym_ByteAttributeSample") &&
        C->GetName() == TEXT("CkAttributeGym_ByteComponentSample") &&
        F->Generation && F->Sequence && F->Ready && F->Failed && F->FailureReason &&
        F->StationIndex && F->AutoRunning && F->AutoStep && F->ValueChangeCount &&
        F->ClampedCount && F->ActiveModifierCount && F->DelegateBoundMask &&
        F->ClampSerials && CastField<FIntProperty>(F->ClampSerials->Inner) &&
        F->BoundarySerial &&
        F->LastPreClamp && F->LastClamped && F->LastOverflow &&
        F->ActiveModifierDeltas && CastField<FByteProperty>(F->ActiveModifierDeltas->Inner) &&
        F->TotalAttributes && F->CombatBatchCount && F->BatchEpoch && F->CombatPresent &&
        F->Title && F->Body && F->Name && F->Min && F->Max &&
        F->Min->Struct == C && F->Max->Struct == C && F->Current->Struct == C &&
        F->Present && F->Base && F->Bonus && F->Final &&
        FindFProperty<FIntProperty>(Station, TEXT("MinChangeCount")) &&
        FindFProperty<FIntProperty>(Station, TEXT("MaxChangeCount")) &&
        FindFProperty<FIntProperty>(Station, TEXT("CurrentChangeCount")) &&
        FindFProperty<FIntProperty>(Station, TEXT("Delegate1Count")) &&
        FindFProperty<FIntProperty>(Station, TEXT("Delegate2Count")) &&
        FindFProperty<FIntProperty>(Station, TEXT("Delegate3Count")) &&
        F->PreClampFinal && F->AttributeOverflow &&
        ChildHandle && ChildHandle->Struct == FCk_Handle::StaticStruct() &&
        F->ServerChildCount && F->LocalGeneration && F->LocalReady && F->LocalFailed &&
        F->LocalFailureReason && F->LastSerial && F->AttemptSerial && F->Result &&
        F->LastSerial->HasAnyPropertyFlags(CPF_Net) &&
        F->AttemptSerial->HasAnyPropertyFlags(CPF_Net) &&
        F->Result->HasAnyPropertyFlags(CPF_Net) &&
        F->RpcSerial && F->RpcAction && F->RpcPreset &&
        F->RpcSerial->HasAnyPropertyFlags(CPF_Parm) &&
        F->RpcAction->HasAnyPropertyFlags(CPF_Parm) &&
        F->RpcPreset->HasAnyPropertyFlags(CPF_Parm) &&
        F->Rpc->NumParms == 3 && F->Rpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer));
    if (!Valid) { return false; }

    F->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, F->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([this, F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            const int32 G = Gen(S, *F);
            const bool Good = G > 0 && AllReady(*F, G, Dedicated) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1));
            if (!Good && FPlatformTime::Seconds() >= F->NextDiagnostic)
            {
                F->NextDiagnostic = FPlatformTime::Seconds() + 5.0;
                for (auto* W : {Server, ck::auto_test::net::Get_ClientWorld(0),
                    ck::auto_test::net::Get_ClientWorld(1)}) { AddInfo(Describe(W, *F)); }
            }
            return Good;
        }), 60.0, TEXT("shared six station Byte demo ready on authority and both clients")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F, Dedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            FSample Sample;
            F->InitialGeneration = Gen(S, *F); F->ServerState = S;
            auto* Survivor = ck::auto_test::net::Get_ClientWorld(1);
            F->SurvivorInstance = IsValid(Survivor) ? Survivor->GetGameInstance() : nullptr;
            if (!Read(S, *F, Sample)) { return TestTrue(TEXT("typed Byte sample"), false); }
            TArray<FCk_Handle> Existing;
            if (!Branches(Server, *F, true, Dedicated, Sample, &Existing))
            { return TestTrue(TEXT("exact server Byte branches"), false); }
            FScriptArrayHelper Slots(F->ServerChildren, F->ServerChildren->ContainerPtrToValuePtr<void>(S));
            for (int32 I = 0; I < 6; ++I)
            { F->ServerRoots[I] = *reinterpret_cast<const FCk_Handle*>(Slots.GetRawPtr(I)); }
            auto* GameMode = IsValid(Server) ? Server->GetAuthGameMode() : nullptr;
            return TestTrue(TEXT("world-owned original Byte sextet and two coherent pure projections"),
                IsValid(GameMode) && GameMode->IsA(F->Mode) &&
                Server->GetNetMode() == (Dedicated ? NM_DedicatedServer : NM_ListenServer) &&
                F->InitialGeneration > 0 && F->SurvivorInstance.IsValid() &&
                AllReady(*F, F->InitialGeneration, Dedicated) &&
                Sample.Total == 12 + 2 * Sample.Stations[3].Batches);
        }), TEXT("capture shared Byte identities and typed sample")));
    auto QueueAction = [this, F, Dedicated](int32 Client, int32 Serial, int32 Action,
        int32 Preset, const TCHAR* Label, bool RetryRejectedBusy = false)
    {
        auto NextBusyRetry = MakeShared<double>(0.0);
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Client,
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, F, Client, Serial, Action, Preset](UWorld* W)
                {
                    if (!TestTrue(TEXT("capture complete Byte state before owning control"),
                        CaptureBefore(*F))) { return; }
                     F->NextDiagnostic = FPlatformTime::Seconds() + 5.0;
                    F->Retired.Reset();
                    if (Action == 13)
                    {
                        F->BeforeGBuff = FCk_Handle{Modifier(F->ServerRoots[1],
                            TEXT("ByteAttribute.Damage"), TEXT("Modifier.Buff"))};
                    }
                    if (Action == 11)
                    {
                        F->BeforeJWeapon = FCk_Handle{Modifier(F->ServerRoots[4],
                            TEXT("Test.ValueRetrieval"), TEXT("Modifier.Weapon"))};
                        F->BeforeJBuff = FCk_Handle{Modifier(F->ServerRoots[4],
                            TEXT("Test.ValueRetrieval"), TEXT("Modifier.Buff"))};
                        F->BeforeJEnchant = FCk_Handle{Modifier(F->ServerRoots[4],
                            TEXT("Test.ValueRetrieval"), TEXT("Modifier.Enchant"))};
                    }
                    if (Action == 12 || Action == 14)
                    {
                        const auto Capture = [F](FCk_Handle Root, const TCHAR* AttributeName,
                            const TCHAR* ModifierName)
                        {
                            const FCk_Handle H{Modifier(Root, AttributeName, ModifierName)};
                            if (IsLive(H)) { F->Retired.AddUnique(H); }
                        };
                        if (Action == 12)
                        {
                            for (const auto* Name : {TEXT("Modifier.Weapon"),
                                TEXT("Modifier.Buff"), TEXT("Modifier.Enchant")})
                            { Capture(F->ServerRoots[4], TEXT("Test.ValueRetrieval"), Name); }
                        }
                        else
                        {
                            for (const auto* Name : {TEXT("Modifier.Weapon"),
                                TEXT("Modifier.Buff")})
                            { Capture(F->ServerRoots[1], TEXT("ByteAttribute.Damage"), Name); }
                            for (const auto* Name : {TEXT("Modifier.Armor"),
                                TEXT("Modifier.Shield"), TEXT("Modifier.Enchantment")})
                            { Capture(F->ServerRoots[1], TEXT("ByteAttribute.Defense"), Name); }
                        }
                    }
                    if (Action == 17)
                    {
                        if (!TestTrue(TEXT("B starts from coherent actual paused clamping values and bounds"),
                            BoundaryBefore(F->ServerRoots[0], F->Before)))
                        { return; }
                    }
                    if (Action == 16 || Action == 18)
                    {
                        const int32 Count = F->Before.Stations[3].Batches ? 5 : 3;
                        if (!TestTrue(TEXT("capture actual Multiple record handles before retirement"),
                            RecordMatches(F->ServerRoots[3], 3, Count, &F->Retired))) { return; }
                        if (Action == 16)
                        { F->Retired.RemoveAt(0, 3); }
                    }
                    TestTrue(TEXT("real remote owner invokes Byte RPC"),
                        Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr,
                            *F, Serial, Action, Preset));
                })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda(
                [this, F, Dedicated, Client, Serial, Action, Preset,
                    RetryRejectedBusy, NextBusyRetry]() -> bool
                {
                    auto* Server = ck::auto_test::net::Get_ServerWorld();
                    auto* S = State(Server, *F);
                    auto* OwnerWorld = ck::auto_test::net::Get_ClientWorld(Client);
                    auto* AuthorityOwner = ck::auto_test::net::TryGet_RemoteClientPlayerController(
                        Server, Client);
                    if (RetryRejectedBusy && !F->TransactionViolation &&
                        FPlatformTime::Seconds() >= *NextBusyRetry &&
                        Result(OwnerWorld, *F, Serial, F->BeforeAccepted[Client], 3) &&
                        IsValid(AuthorityOwner) && AuthorityOwner->IsA(F->Controller) &&
                        F->AttemptSerial->GetPropertyValue_InContainer(AuthorityOwner) == Serial &&
                        F->LastSerial->GetPropertyValue_InContainer(AuthorityOwner) == F->BeforeAccepted[Client] &&
                        F->Result->GetPropertyValue_InContainer(AuthorityOwner) == 3 &&
                        AllReady(*F, F->InitialGeneration, Dedicated))
                    {
                        *NextBusyRetry = FPlatformTime::Seconds() + 0.2;
                        if (!TestTrue(TEXT("recapture complete Byte baseline after rejected busy request"),
                            CaptureBefore(*F))) { return false; }
                        auto* OwnerPC = IsValid(OwnerWorld) ? OwnerWorld->GetFirstPlayerController() : nullptr;
                        if (!TestTrue(TEXT("retry only explicitly rejected unaccepted Byte request"),
                            Invoke(OwnerPC, *F, Serial, Action, Preset))) { return false; }
                        return false;
                    }
                    if ((Action == 11 || Action == 12 || Action == 13 ||
                        Action == 14 || Action == 15 || Action == 16 || Action == 18) &&
                        Ready(S, *F, F->InitialGeneration) && F->BeforeSnapshot.IsValid() &&
                        Seq(S, *F) == F->BeforeServerSequence &&
                        !F->Snapshot->Identical(Data(S, *F),
                            F->BeforeSnapshot->GetStructMemory(), 0))
                    { F->TransactionViolation = true; }
                    if ((Action == 11 || Action == 12 || Action == 13 ||
                        Action == 14 || Action == 15 || Action == 16 || Action == 18) &&
                        IsValid(AuthorityOwner) && AuthorityOwner->IsA(F->Controller) &&
                        F->AttemptSerial->GetPropertyValue_InContainer(AuthorityOwner) == Serial &&
                        F->Result->GetPropertyValue_InContainer(AuthorityOwner) == 1 &&
                        Seq(S, *F) == F->BeforeServerSequence)
                    { F->TransactionViolation = true; }
                    if (FPlatformTime::Seconds() >= F->NextDiagnostic)
                    {
                        F->NextDiagnostic = FPlatformTime::Seconds() + 5.0;
                        AddInfo(DescribeControlWait(*F, Dedicated, Client, Serial, Action, Preset));
                        for (auto* W : {Server, ck::auto_test::net::Get_ClientWorld(0),
                            ck::auto_test::net::Get_ClientWorld(1)})
                        { AddInfo(Describe(W, *F) + TEXT(" clamping:") +
                            DescribeParts(State(W, *F), *F, 0) + TEXT(" power:") +
                            DescribeParts(State(W, *F), *F, 2)); }
                        AddInfo(TEXT("Byte live power:") + DescribeLiveParts(F->ServerRoots[2], 2));
                    }
                    if (!Ready(S, *F, F->InitialGeneration)) { return false; }
                    Remember(*F, S);
                    if (!Result(OwnerWorld, *F, Serial, Serial, 1) ||
                        Seq(S, *F) <= F->BeforeServerSequence ||
                        F->TransactionViolation) { return false; }
                    FSample ServerSample;
                    if (!Read(S, *F, ServerSample)) { return false; }
                    for (int32 I = 0; I < 6; ++I)
                    {
                        if (!IsLive(F->ServerRoots[I])) { return false; }
                        TArray<FCk_Handle> O, P;
                        if (!Children(Server, I, O, P) || O.Num() != 1 ||
                            O[0] != F->ServerRoots[I]) { return false; }
                    }
                    const auto Check = [F, Action, Preset](const FSample& X) -> bool
                    {
                        if (Action == 0)
                        {
                            for (const auto& V : X.Stations)
                            { if (V.Auto) { return false; } }
                            return true;
                        }
                        if (Action == 15)
                        {
                            if (X.Total != 14 || X.Stations[3].Batches != 1) { return false; }
                            return F->Before.Stations[3].Batches == 0 ?
                                X.Stations[3].Epoch > F->Before.Stations[3].Epoch :
                                X.Stations[3].Epoch == F->Before.Stations[3].Epoch;
                        }
                        if (Action == 16)
                        { return X.Total == 12 && X.Stations[3].Batches == 0; }
                        if (Action == 17)
                        {
                            return BoundaryAfter(X.Stations[0], F->Before.Stations[0]);
                        }
                        if (Action == 11 || Action == 13)
                        {
                            const int32 I = Action == 11 ? 4 : 1;
                            const bool Repeated = Action == 11 ?
                                IsLive(F->BeforeJWeapon) && IsLive(F->BeforeJBuff) :
                                IsLive(F->BeforeGBuff);
                            return Repeated ?
                                X.Stations[I].Modifiers == F->Before.Stations[I].Modifiers &&
                                X.Stations[I].ModifierDeltas ==
                                    F->Before.Stations[I].ModifierDeltas &&
                                X.Stations[I].Final == F->Before.Stations[I].Final :
                                X.Stations[I].Modifiers ==
                                    F->Before.Stations[I].Modifiers +
                                        (Action == 11 ? 2 : 1);
                        }
                        if (Action == 12 || Action == 14)
                        {
                            const int32 I = Action == 12 ? 4 : 1;
                            return X.Stations[I].Modifiers == 0 &&
                                X.Stations[I].ModifierDeltas.IsEmpty();
                        }
                        if (Action >= 7 && Action <= 10)
                        {
                            const int32 I = Action == 10 ? 5 : 2;
                            return Preset >= 0 && Preset <= 3 &&
                                X.Stations[I].Final.Num() == 1 &&
                                X.Stations[I].Final[0] == 100;
                        }
                        if (Action == 18)
                        {
                            if (!IsAuthoredReset(X) ||
                                X.Stations[0].BoundarySerial <
                                    F->Before.Stations[0].BoundarySerial ||
                                X.Stations[0].ClampSerials.Num() != 4 ||
                                F->Before.Stations[0].ClampSerials.Num() != 4)
                            { return false; }
                            for (int32 J = 0; J < 4; ++J)
                            {
                                if (X.Stations[0].ClampSerials[J] <
                                    F->Before.Stations[0].ClampSerials[J])
                                { return false; }
                            }
                            return true;
                        }
                        return false;
                    };
                    if (Action == 18)
                    {
                        const auto* Commit = F->ControlCommit->ContainerPtrToValuePtr<void>(S);
                        FSample Candidate;
                        if (!Read(F->CommitSnapshot->ContainerPtrToValuePtr<void>(Commit), *F, Candidate) ||
                            !Check(Candidate)) { return false; }
                        for (int32 I = 0; I < 2; ++I)
                        {
                            auto* CS = State(ck::auto_test::net::Get_ClientWorld(I), *F);
                            if (!ResetCommitMatches(S, CS, *F, Serial, F->BeforeServerSequence))
                            { return false; }
                        }
                    }
                    else
                    {
                        if (!Check(ServerSample) ||
                            !AllReady(*F, F->InitialGeneration, Dedicated)) { return false; }
                        for (int32 I = 0; I < 2; ++I)
                        {
                            auto* CS = State(ck::auto_test::net::Get_ClientWorld(I), *F);
                            FSample ClientSample;
                            if (!MatchesHistory(CS, *F, F->BeforeSequence[I]) ||
                                !Read(CS, *F, ClientSample) || !Check(ClientSample)) { return false; }
                        }
                    }
                    if (Action == 11 || Action == 13)
                    {
                        const auto* AttributeName = Action == 11 ?
                            TEXT("Test.ValueRetrieval") : TEXT("ByteAttribute.Damage");
                        const auto Root = F->ServerRoots[Action == 11 ? 4 : 1];
                        const auto Buff = Modifier(Root, AttributeName, TEXT("Modifier.Buff"));
                        if (!ck::IsValid(Buff) ||
                            UCk_Utils_ByteAttributeModifier_UE::Get_Delta(Buff) !=
                                (Action == 11 ? 15 : 20))
                        { return false; }
                        if (Action == 13 && IsLive(F->BeforeGBuff) &&
                            FCk_Handle{Buff} != F->BeforeGBuff) { return false; }
                        if (Action == 11)
                        {
                            const auto Weapon = Modifier(Root, AttributeName,
                                TEXT("Modifier.Weapon"));
                            const auto Enchant = Modifier(Root, AttributeName,
                                TEXT("Modifier.Enchant"));
                            if (!ck::IsValid(Weapon) ||
                                UCk_Utils_ByteAttributeModifier_UE::Get_Delta(Weapon) != 20 ||
                                (IsLive(F->BeforeJWeapon) &&
                                    FCk_Handle{Weapon} != F->BeforeJWeapon) ||
                                (IsLive(F->BeforeJBuff) &&
                                    FCk_Handle{Buff} != F->BeforeJBuff) ||
                                (IsLive(F->BeforeJEnchant) &&
                                    FCk_Handle{Enchant} != F->BeforeJEnchant))
                            { return false; }
                        }
                    }
                    if (Action == 17 && !LiveBoundaryValues(F->ServerRoots[0]))
                    { return false; }
                    if (Action == 15)
                    {
                        TArray<FCk_Handle> Pair;
                        if (!RecordMatches(F->ServerRoots[3], 3, 5, &Pair) || Pair.Num() != 5)
                        { return false; }
                        if (F->Before.Stations[3].Batches == 1)
                        {
                            if (Pair[3] != F->CombatPair[0] || Pair[4] != F->CombatPair[1] ||
                                ServerSample.Stations[3].Epoch != F->CombatEpoch) { return false; }
                        }
                        else
                        {
                            if (Pair[3] == F->CombatPair[0] || Pair[4] == F->CombatPair[1])
                            { return false; }
                            F->CombatPair[0] = Pair[3]; F->CombatPair[1] = Pair[4];
                            F->CombatEpoch = ServerSample.Stations[3].Epoch;
                        }
                    }
                    if (Action == 12 || Action == 14)
                    {
                        for (const auto& H : F->Retired)
                        { if (!ck::Is_NOT_Valid(H)) { return false; } }
                    }
                    if (Action == 16 || Action == 18)
                    {
                        if (F->Retired.Num() != (Action == 16 ?
                            2 * F->Before.Stations[3].Batches :
                            3 + 2 * F->Before.Stations[3].Batches)) { return false; }
                        for (const auto& H : F->Retired)
                        { if (!ck::Is_NOT_Valid(H)) { return false; } }
                        if (!RecordMatches(F->ServerRoots[3], 3, 3, nullptr)) { return false; }
                    }
                    if (Action >= 7 && Action <= 10)
                    {
                        auto* PC = IsValid(OwnerWorld) ? OwnerWorld->GetFirstPlayerController() : nullptr;
                        if (!IsValid(PC) ||
                            F->Ring[Action - 7]->GetPropertyValue_InContainer(PC) != Preset)
                        { return false; }
                    }
                    return true;
                }), 25.0, Label));
    };

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            FSample Sample;
            if (!AllReady(*F, F->InitialGeneration, Dedicated) ||
                !Read(S, *F, Sample) || !Sample.Stations[1].Auto ||
                Sample.Stations[3].Step < 2 ||
                Sample.Stations[1].Step < 6 ||
                Sample.Stations[1].Step % 8 != 6 ||
                Sample.Stations[1].Modifiers != 4 ||
                !Sample.Stations[1].ModifierDeltas.Contains(35))
            { return false; }
            const auto DamageTag = FGameplayTag::RequestGameplayTag(
                FName(TEXT("ByteAttribute.Damage")), false);
            const auto Damage = UCk_Utils_ByteAttribute_UE::TryGet(
                F->ServerRoots[1], DamageTag);
            if (!ck::IsValid(Damage) ||
                Sample.Stations[1].PreClamp[0] !=
                    UCk_Utils_ByteAttribute_UE::Get_PreClampFinalValue(Damage) ||
                Sample.Stations[1].Final[0] !=
                    UCk_Utils_ByteAttribute_UE::Get_FinalValue(Damage))
            { return false; }
            const auto Buff = Modifier(F->ServerRoots[1],
                TEXT("ByteAttribute.Damage"), TEXT("Modifier.Buff"));
            if (!IsLive(FCk_Handle{Buff}) ||
                UCk_Utils_ByteAttributeModifier_UE::Get_Delta(Buff) != 35)
            { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                auto* CS = State(ck::auto_test::net::Get_ClientWorld(I), *F);
                FSample ClientSample;
                if (!Read(CS, *F, ClientSample) ||
                    !ClientSample.Stations[1].Auto ||
                    ClientSample.Stations[1].Step < 6 ||
                    ClientSample.Stations[1].Step % 8 != 6 ||
                    ClientSample.Stations[1].Modifiers != 4 ||
                    ClientSample.Stations[1].PreClamp[0] !=
                        Sample.Stations[1].PreClamp[0] ||
                    ClientSample.Stations[1].Final[0] !=
                        Sample.Stations[1].Final[0] ||
                    !ClientSample.Stations[1].ModifierDeltas.Contains(35))
                { return false; }
                F->AuthoredGClientSequence[I] = Seq(CS, *F);
            }
            F->AuthoredGBuff = FCk_Handle{Buff};
            F->AuthoredGModifierCount = Sample.Stations[1].Modifiers;
            F->AuthoredGServerSequence = Seq(S, *F);
            F->AuthoredGExpectedPreClamp = static_cast<uint8>(
                int32(Sample.Stations[1].PreClamp[0]) - 15);
            const uint8 Min = UCk_Utils_ByteAttribute_UE::Get_FinalValue(
                Damage, ECk_MinMaxCurrent::Min);
            const uint8 Max = UCk_Utils_ByteAttribute_UE::Get_FinalValue(
                Damage, ECk_MinMaxCurrent::Max);
            const uint8 Raw = F->AuthoredGExpectedPreClamp;
            F->AuthoredGExpectedFinal = Raw < Min ? Min : Raw > Max ? Max : Raw;
            return true;
        }), 70.0, TEXT("authored Modifiers auto step publishes one +35 Buff to server and clients")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            TestTrue(TEXT("remote owner invokes G against authored +35 Buff"),
                IsLive(F->AuthoredGBuff) && Invoke(IsValid(W) ?
                    W->GetFirstPlayerController() : nullptr, *F, 199, 13, -1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
            FSample Sample;
            if (!AllReady(*F, F->InitialGeneration, Dedicated) ||
                !Result(ck::auto_test::net::Get_ClientWorld(1), *F, 199, 199, 1) ||
                Seq(S, *F) <= F->AuthoredGServerSequence ||
                !Read(S, *F, Sample) || Sample.Stations[1].Auto ||
                Sample.Stations[1].Modifiers != F->AuthoredGModifierCount ||
                Sample.Stations[1].PreClamp[0] != F->AuthoredGExpectedPreClamp ||
                Sample.Stations[1].Final[0] != F->AuthoredGExpectedFinal ||
                !Sample.Stations[1].ModifierDeltas.Contains(20) ||
                Sample.Stations[1].ModifierDeltas.Contains(35))
            { return false; }
            const auto Buff = Modifier(F->ServerRoots[1],
                TEXT("ByteAttribute.Damage"), TEXT("Modifier.Buff"));
            if (!IsLive(F->AuthoredGBuff) || FCk_Handle{Buff} != F->AuthoredGBuff ||
                UCk_Utils_ByteAttributeModifier_UE::Get_Delta(Buff) != 20)
            { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                auto* CS = State(ck::auto_test::net::Get_ClientWorld(I), *F);
                FSample ClientSample;
                if (Seq(CS, *F) <= F->AuthoredGClientSequence[I] ||
                    !Read(CS, *F, ClientSample) || ClientSample.Stations[1].Auto ||
                    ClientSample.Stations[1].Modifiers != F->AuthoredGModifierCount ||
                    ClientSample.Stations[1].PreClamp[0] != F->AuthoredGExpectedPreClamp ||
                    ClientSample.Stations[1].Final[0] != F->AuthoredGExpectedFinal ||
                    !ClientSample.Stations[1].ModifierDeltas.Contains(20) ||
                    ClientSample.Stations[1].ModifierDeltas.Contains(35))
                { return false; }
            }
            return true;
        }), 25.0, TEXT("owning-client G normalizes authored Buff in place before accepted commit")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            TestTrue(TEXT("remote owner restarts Modifiers auto after G"),
                Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr,
                    *F, 200, 2, -1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
            FSample Sample;
            if (!Result(ck::auto_test::net::Get_ClientWorld(1), *F, 200, 200, 1) ||
                !AllReady(*F, F->InitialGeneration, Dedicated) || !Read(S, *F, Sample))
            { return false; }
            for (const auto& Station : Sample.Stations)
            { if (!Station.Auto) { return false; } }
            return true;
        }), 25.0, TEXT("all authored Byte auto cycles running before global pause")));
    QueueAction(0, 101, 0, -1, TEXT("remote owner pauses all six Byte auto cycles"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            FSample V;
            if (!AllReady(*F, F->InitialGeneration, Dedicated) || !Read(S, *F, V))
            { return false; }
            if (F->PauseSince == 0.0)
            { F->PauseSince = Server->GetTimeSeconds(); F->Before = V; }
            for (int32 I = 0; I < 6; ++I)
            {
                const auto& A = V.Stations[I]; const auto& B = F->Before.Stations[I];
                if (A.Auto || A.Step != B.Step || A.Final != B.Final ||
                    A.Changes != B.Changes || A.Clamps != B.Clamps ||
                    A.BoundarySerial != B.BoundarySerial ||
                    A.ClampSerials != B.ClampSerials)
                { F->PauseSince = 0.0; return false; }
            }
            return Server->GetTimeSeconds() - F->PauseSince >= 3.6;
        }), 12.0, TEXT("all six auto steps stay paused across Multiple's authored 3.5 second cycle")));
    // A late-starting server can already own the unique combat pair. Normalize it
    // through the same real owning-client control before exact manual counts.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            FSample V;
            if (!TestTrue(TEXT("read paused Multiple membership"),
                Read(State(ck::auto_test::net::Get_ServerWorld(), *F), *F, V))) { return; }
            if (V.Stations[3].Batches == 0) { return; }
            if (!TestTrue(TEXT("capture normalization pre-state"), CaptureBefore(*F)))
            { return; }
            F->Retired.Reset();
            if (!TestTrue(TEXT("capture existing combat pair"),
                RecordMatches(F->ServerRoots[3], 3, 5, &F->Retired) && F->Retired.Num() == 5))
            { return; }
            F->Retired.RemoveAt(0, 3);
            TestTrue(TEXT("real remote normalization U"),
                Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr, *F, 201, 16, -1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            FSample V;
            if (!AllReady(*F, F->InitialGeneration, Dedicated) || !Read(S, *F, V) ||
                V.Total != 12 || V.Stations[3].Batches != 0 ||
                !RecordMatches(F->ServerRoots[3], 3, 3, nullptr)) { return false; }
            if (F->Retired.IsEmpty()) { return true; }
            if (!Result(ck::auto_test::net::Get_ClientWorld(1), *F, 201, 201, 1))
            { return false; }
            for (const auto& H : F->Retired)
            { if (!ck::Is_NOT_Valid(H)) { return false; } }
            return true;
        }), 25.0, TEXT("normalized twelve-attribute authority baseline with no combat pair")));
    QueueAction(1, 202, 18, -1, TEXT("R restores deterministic clamping defaults before B"));
    QueueAction(1, 203, 0, -1, TEXT("remote owner pauses autos after default restoration"));
    QueueAction(1, 204, 17, -1, TEXT("remote boundary control advances actual clamp counter"));
    QueueAction(0, 102, 12, -1, TEXT("K clears Values revocable modifiers before first J"));
    QueueAction(1, 205, 14, -1, TEXT("N clears Modifiers revocable modifiers before first G"));
    QueueAction(0, 103, 11, -1, TEXT("J adds keyed Values Weapon and Buff"));
    QueueAction(1, 206, 13, -1, TEXT("G adds keyed Modifiers Buff"));
    QueueAction(0, 104, 11, -1, TEXT("repeated J preserves keyed pair and membership"));
    QueueAction(1, 207, 13, -1, TEXT("repeated G preserves keyed Buff and membership"));
    QueueAction(0, 105, 9, 0, TEXT("remote zero advances its Current preset ring"));
    QueueAction(1, 208, 9, 0, TEXT("remote one independently advances its Current preset ring"));
    QueueAction(0, 106, 15, -1, TEXT("M adds exactly one recorded combat pair"));
    QueueAction(0, 107, 15, -1, TEXT("repeated M admits idempotently with identical combat handles"));
    QueueAction(1, 209, 16, -1, TEXT("U retires both combat attributes and restores twelve"));
    QueueAction(0, 108, 15, -1, TEXT("M after U creates a fresh recorded combat pair"));
    QueueAction(1, 210, 18, -1, TEXT("R retires old RPG and combat and publishes complete authored reset"));
    QueueAction(0, 109, 0, -1,
        TEXT("pause actual Byte auto cycles before same-owner R race"), true);
    QueueAction(0, 110, 16, -1,
        TEXT("retire actual combat pair before same-owner R race"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            if (!TestTrue(TEXT("capture coherent Byte state before same-owner R race"),
                CaptureBefore(*F) && F->Before.Total == 12 &&
                F->Before.Stations[3].Batches == 0)) { return; }
            F->Retired.Reset();
            if (!TestTrue(TEXT("capture old RPG record handles for concurrent R"),
                RecordMatches(F->ServerRoots[3], 3, 3, &F->Retired))) { return; }
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owning PC sends R then a fresh M while R is pending"),
                Invoke(PC, *F, 120, 18, -1) &&
                Invoke(PC, *F, 121, 15, -1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            if (!Ready(S, *F, F->InitialGeneration)) { return false; }
            auto* AuthorityPC =
                ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0);
            if (Seq(S, *F) == F->BeforeServerSequence && F->BeforeSnapshot.IsValid())
            {
                if (!F->Snapshot->Identical(Data(S, *F),
                        F->BeforeSnapshot->GetStructMemory(), 0) ||
                    (IsValid(AuthorityPC) && AuthorityPC->IsA(F->Controller) &&
                     F->LastSerial->GetPropertyValue_InContainer(AuthorityPC) == 120))
                { F->TransactionViolation = true; }
            }
            if (F->TransactionViolation ||
                !Result(ck::auto_test::net::Get_ClientWorld(0), *F, 121, 120, 3) ||
                Seq(S, *F) <= F->BeforeServerSequence ||
                !AllReady(*F, F->InitialGeneration, Dedicated))
            { return false; }
            FSample ServerSample;
            if (!Read(S, *F, ServerSample) || ServerSample.Total != 12 ||
                ServerSample.Stations[3].Batches != 0 ||
                !RecordMatches(F->ServerRoots[3], 3, 3, nullptr)) { return false; }
            for (int32 I = 0; I < 6; ++I)
            {
                TArray<FCk_Handle> O, P;
                if (!Children(Server, I, O, P) || O.Num() != 1 ||
                    O[0] != F->ServerRoots[I]) { return false; }
            }
            for (int32 I = 0; I < 2; ++I)
            {
                auto* Client = State(ck::auto_test::net::Get_ClientWorld(I), *F);
                FSample ClientSample;
                if (!MatchesHistory(Client, *F, F->BeforeSequence[I]) ||
                    !Read(Client, *F, ClientSample) || ClientSample.Total != 12 ||
                    ClientSample.Stations[3].Batches != 0)
                { return false; }
            }
            for (const auto& H : F->Retired)
            { if (!ck::Is_NOT_Valid(H)) { return false; } }
            return true;
        }), 25.0, TEXT("same owning PC busy rejection preserves first R commit and one reset")));
    QueueAction(0, 122, 0, -1, TEXT("remote owner pauses six autos before invalid controls"));
    auto QueueRejected = [this, F, Dedicated](int32 Serial, int32 Action, int32 Preset,
                                              const TCHAR* Label)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, F, Serial, Action, Preset](UWorld* W)
            {
                if (!TestTrue(TEXT("capture pre-rejection typed Byte state"), CaptureBefore(*F)))
                { return; }
                TestTrue(TEXT("invalid request used real owning remote RPC"),
                    Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr,
                        *F, Serial, Action, Preset));
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated, Serial]() -> bool
            {
                auto* Server = ck::auto_test::net::Get_ServerWorld();
                auto* S = State(Server, *F);
                if (!Result(ck::auto_test::net::Get_ClientWorld(0), *F,
                        Serial, F->BeforeAccepted[0], 4) ||
                    Gen(S, *F) != F->InitialGeneration ||
                    !AllReady(*F, F->InitialGeneration, Dedicated)) { return false; }
                for (int32 I = 0; I < 6; ++I)
                {
                    TArray<FCk_Handle> O, P;
                    if (!Children(Server, I, O, P) || O.Num() != 1 ||
                        O[0] != F->ServerRoots[I]) { return false; }
                }
                for (int32 I = -1; I < 2; ++I)
                {
                    auto* W = I < 0 ? Server : ck::auto_test::net::Get_ClientWorld(I);
                    FSample Actual;
                    if (!Read(State(W, *F), *F, Actual) ||
                        Actual.Total != F->Before.Total) { return false; }
                    for (int32 J = 0; J < 6; ++J)
                    {
                        const auto& A = Actual.Stations[J]; const auto& B = F->Before.Stations[J];
                        if (A.Auto != B.Auto || A.Step != B.Step ||
                            A.Final != B.Final || A.Modifiers != B.Modifiers ||
                            A.Batches != B.Batches || A.Epoch != B.Epoch ||
                            A.Changes != B.Changes || A.Clamps != B.Clamps ||
                            A.BoundarySerial != B.BoundarySerial ||
                            A.ClampSerials != B.ClampSerials)
                        { return false; }
                    }
                }
                return true;
            }), 15.0, Label));
    };
    QueueRejected(0, 15, -1, TEXT("zero serial rejected without mutation or watermark"));
    QueueRejected(123, 99, -1, TEXT("unsupported Byte action rejected"));
    QueueRejected(122, 15, -1, TEXT("stale Byte serial rejected"));
    QueueRejected(124, 9, 99, TEXT("out-of-range per-owner preset rejected"));
    QueueRejected(125, 15, 0, TEXT("nonring action with a preset rejected"));
    // T resumes the authored station timers before losing one remote owner.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            if (!TestTrue(TEXT("capture state before resumed auto"), CaptureBefore(*F))) { return; }
            TestTrue(TEXT("survivor resumes authored Byte autos"),
                Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr,
                    *F, 211, 0, -1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
            FSample V;
            if (!Result(ck::auto_test::net::Get_ClientWorld(1), *F, 211, 211, 1) ||
                !AllReady(*F, F->InitialGeneration, Dedicated) || !Read(S, *F, V))
            { return false; }
            for (const auto& Station : V.Stations)
            { if (!Station.Auto) { return false; } }
            return true;
        }), 20.0, TEXT("survivor resumes all six authority Byte cycles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* RemotePC = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0);
            auto* LocalPC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            if (!TestTrue(TEXT("real departing Byte remote owner"),
                IsValid(RemotePC) && IsValid(LocalPC) && IsValid(W->GetGameInstance())))
            { return; }
            F->DepartingServerPC = RemotePC;
            F->DepartingInstance = W->GetGameInstance();
            F->DepartureSequence = Seq(F->ServerState.Get(), *F);
            FSample V;
            if (!TestTrue(TEXT("capture active Byte step before departure"),
                Read(F->ServerState.Get(), *F, V))) { return; }
            F->DepartureStep = V.Stations[3].Step;
            LocalPC->ClientTravel(TEXT("/Engine/Maps/Entry"), TRAVEL_Absolute);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            auto* Departed = F->DepartingInstance.IsValid() ?
                F->DepartingInstance->GetWorld() : nullptr;
            FSample A, B;
            if (S != F->ServerState.Get() || F->DepartingServerPC.IsValid() ||
                !IsValid(Departed) || !Departed->GetMapName().Contains(TEXT("Entry")) ||
                !AllReady(*F, F->InitialGeneration, Dedicated, true) ||
                Seq(S, *F) <= F->DepartureSequence ||
                Seq(State(Survivor, *F), *F) <= F->DepartureSequence ||
                !Read(S, *F, A) || !Read(State(Survivor, *F), *F, B) ||
                A.Stations[3].Step == F->DepartureStep ||
                B.Stations[3].Step == F->DepartureStep) { return false; }
            for (int32 I = 0; I < 6; ++I)
            {
                TArray<FCk_Handle> O, P;
                if (!Children(Server, I, O, P) || O.Num() != 1 ||
                    O[0] != F->ServerRoots[I]) { return false; }
            }
            return true;
        }), 45.0, TEXT("actual owner departure preserves six world roots and advancing survivor")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* Server)
        {
            // Client indices compact after departure. Retain the actual player's
            // game instance and invoke the RPC on its surviving client controller.
            auto* W = F->SurvivorInstance.IsValid() ? F->SurvivorInstance->GetWorld() : nullptr;
            if (!TestTrue(TEXT("retained surviving remote client and original authority world"),
                IsValid(W) && W->GetNetMode() == NM_Client &&
                IsValid(W->GetFirstPlayerController()) &&
                W->GetFirstPlayerController()->IsLocalController() &&
                W->GetGameInstance() == F->SurvivorInstance.Get() &&
                IsValid(F->ServerState.Get()) &&
                State(Server, *F) == F->ServerState.Get())) { return; }
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
            FSample Sample;
            if (!TestTrue(TEXT("capture survivor-only pre-R state"),
                W == Survivor && Read(S, *F, Sample) &&
                IsValid(W->GetFirstPlayerController()))) { return; }
            F->ServerBeforeSequence = Seq(S, *F);
            F->SurvivorBeforeSequence = Seq(State(W, *F), *F);
            F->Retired.Reset();
            if (!TestTrue(TEXT("capture pre-R RPG and optional combat handles"),
                RecordMatches(F->ServerRoots[3], 3,
                    3 + 2 * Sample.Stations[3].Batches, &F->Retired))) { return; }
            TestTrue(TEXT("survivor resets clamping before later B"),
                Invoke(W->GetFirstPlayerController(), *F, 212, 18, -1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            auto* CS = State(Survivor, *F);
            FSample Sample;
            if (!Result(Survivor, *F, 212, 212, 1) ||
                !AllReady(*F, F->InitialGeneration, Dedicated, true) ||
                Seq(S, *F) <= F->ServerBeforeSequence ||
                !ResetCommitMatches(S, CS, *F, 212, F->ServerBeforeSequence) ||
                !Read(S, *F, Sample) ||
                !RecordMatches(F->ServerRoots[3], 3, 3, nullptr)) { return false; }
            for (const auto& H : F->Retired)
            { if (!ck::Is_NOT_Valid(H)) { return false; } }
            return true;
        }), 25.0, TEXT("surviving owner R restores actual authored values and retires old Multiple handles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* Server)
        {
            // Client indices compact after departure. Retain the actual player's
            // game instance and invoke the RPC on its surviving client controller.
            auto* W = F->SurvivorInstance.IsValid() ? F->SurvivorInstance->GetWorld() : nullptr;
            if (!TestTrue(TEXT("retained surviving remote client and original authority world"),
                IsValid(W) && W->GetNetMode() == NM_Client &&
                IsValid(W->GetFirstPlayerController()) &&
                W->GetFirstPlayerController()->IsLocalController() &&
                W->GetGameInstance() == F->SurvivorInstance.Get() &&
                IsValid(F->ServerState.Get()) &&
                State(Server, *F) == F->ServerState.Get())) { return; }
            F->ServerBeforeSequence = Seq(F->ServerState.Get(), *F);
            TestTrue(TEXT("survivor pauses autos before deterministic B"),
                Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr, *F, 213, 0, -1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
            FSample Sample;
            if (!Result(Survivor, *F, 213, 213, 1) ||
                !AllReady(*F, F->InitialGeneration, Dedicated, true) ||
                Seq(S, *F) <= F->ServerBeforeSequence || !Read(S, *F, Sample))
            { return false; }
            for (const auto& V : Sample.Stations)
            { if (V.Auto) { return false; } }
            return BoundaryBefore(F->ServerRoots[0], Sample);
        }), 20.0, TEXT("post-departure real remote pause settles actual clamping values")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* Server)
        {
            // Client indices compact after departure. Retain the actual player's
            // game instance and invoke the RPC on its surviving client controller.
            auto* W = F->SurvivorInstance.IsValid() ? F->SurvivorInstance->GetWorld() : nullptr;
            if (!TestTrue(TEXT("retained surviving remote client and original authority world"),
                IsValid(W) && W->GetNetMode() == NM_Client &&
                IsValid(W->GetFirstPlayerController()) &&
                W->GetFirstPlayerController()->IsLocalController() &&
                W->GetGameInstance() == F->SurvivorInstance.Get() &&
                IsValid(F->ServerState.Get()) &&
                State(Server, *F) == F->ServerState.Get())) { return; }
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
            FSample Sample;
            if (!TestTrue(TEXT("surviving Byte owner and complete server state"),
                W == Survivor && Read(S, *F, Sample) &&
                IsValid(W->GetFirstPlayerController()))) { return; }
            if (!TestTrue(TEXT("post-departure B captures coherent actual paused values and bounds"),
                BoundaryBefore(F->ServerRoots[0], Sample)))
            { return; }
            F->ServerBeforeSequence = Seq(S, *F);
            F->SurvivorBeforeSequence = Seq(State(W, *F), *F);
            F->BeforeSurvivorClamping = Sample.Stations[0];
            TestTrue(TEXT("surviving remote owner invokes live Byte B control"),
                Invoke(W->GetFirstPlayerController(), *F, 214, 17, -1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            auto* CS = State(Survivor, *F);
            FSample A, B;
            if (!Result(Survivor, *F, 214, 214, 1) ||
                !AllReady(*F, F->InitialGeneration, Dedicated, true) ||
                Seq(S, *F) <= F->ServerBeforeSequence ||
                !MatchesHistory(CS, *F, F->SurvivorBeforeSequence) ||
                !Read(S, *F, A) || !Read(CS, *F, B) ||
                !BoundaryAfter(A.Stations[0], F->BeforeSurvivorClamping) ||
                !BoundaryAfter(B.Stations[0], F->BeforeSurvivorClamping) ||
                !LiveBoundaryValues(F->ServerRoots[0]))
            { return false; }
            for (int32 I = 0; I < 6; ++I)
            {
                TArray<FCk_Handle> O, P;
                if (!Children(Server, I, O, P) || O.Num() != 1 ||
                    O[0] != F->ServerRoots[I]) { return false; }
            }
            return true;
        }), 25.0, TEXT("surviving real remote owner still controls shared Byte authority")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F, Dedicated](UWorld* Server)
        {
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            FSample Sample;
            if (!TestTrue(TEXT("capture complete Byte branches before dependency loss"),
                Read(State(Server, *F), *F, Sample) &&
                Branches(Server, *F, true, Dedicated, Sample,
                    &F->BeforeFailureServer, &F->BeforeFailureHost) &&
                Branches(Survivor, *F, false, Dedicated, Sample,
                    nullptr, &F->BeforeFailureSurvivor))) { return; }
            for (int32 I = 0; I < 6; ++I)
            { F->BeforeFailureServer.AddUnique(F->ServerRoots[I]); }
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F->ServerRoots[3]);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            auto* Survivor = F->SurvivorInstance.IsValid() ?
                F->SurvivorInstance->GetWorld() : nullptr;
            auto* CS = State(Survivor, *F);
            if (!Failed(S, *F, F->InitialGeneration) ||
                !Failed(CS, *F, F->InitialGeneration) ||
                !F->LocalFailed->GetPropertyValue_InContainer(CS) ||
                F->LocalReady->GetPropertyValue_InContainer(CS) ||
                F->ServerChildCount->GetPropertyValue_InContainer(S) != 0 ||
                !NoBranches(Server) || !NoBranches(Survivor)) { return false; }
            FScriptArrayHelper Slots(F->ServerChildren,
                F->ServerChildren->ContainerPtrToValuePtr<void>(S));
            if (Slots.Num() != 6) { return false; }
            for (int32 I = 0; I < 6; ++I)
            {
                if (*reinterpret_cast<const FCk_Handle*>(Slots.GetRawPtr(I)) != FCk_Handle{})
                { return false; }
            }
            if (F->BeforeFailureServer.IsEmpty() ||
                F->BeforeFailureSurvivor.IsEmpty() ||
                (Dedicated ? !F->BeforeFailureHost.IsEmpty() :
                    F->BeforeFailureHost.IsEmpty()))
            { return false; }
            for (const auto* List : {&F->BeforeFailureServer,
                &F->BeforeFailureHost, &F->BeforeFailureSurvivor})
            {
                for (const auto& H : *List)
                { if (!ck::Is_NOT_Valid(H)) { return false; } }
            }
            return true;
        }), 30.0, TEXT("required Byte child loss retires all six authority and local branches")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            F->FailureSequence = Seq(F->ServerState.Get(), *F);
            return TestTrue(TEXT("capture terminal Byte failure sequence"),
                F->FailureSequence > 0);
        }), TEXT("capture terminal Byte failure sequence")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
            return TestTrue(TEXT("Byte failure remains terminal with no sibling progress"),
                S == F->ServerState.Get() && Failed(S, *F, F->InitialGeneration) &&
                Seq(S, *F) == F->FailureSequence &&
                F->ServerChildCount->GetPropertyValue_InContainer(S) == 0 &&
                NoBranches(ck::auto_test::net::Get_ServerWorld()));
        }), TEXT("terminal Byte failure remains inert")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            F->RestoreSettings();
            return TestTrue(TEXT("Attribute Byte PIE worlds drained"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
        }), TEXT("restore original startup preferences"), true));
    return true;
}

namespace ck_attribute_byte_preset_test
{
    using namespace ck_attribute_byte_network_test;
    constexpr uint8 Presets[4][4] = {
        {10, 20, 120, 220}, {200, 180, 80, 5},
        {100, 150, 255, 0}, {150, 0, 255, 200}};
    // Bound controls recompute from the already-clamped live Current, not its historical
    // PreClamp value. A reset may advance one authored auto step before the pause settles.
    uint8 ExpectedCurrentAfterPreset(int32 Action, int32 Preset,
                                     uint8 BeforeMin, uint8 BeforeMax,
                                     uint8 BeforeCurrent)
    {
        const uint8 Min = Action == 7 ? Presets[0][Preset] : BeforeMin;
        const uint8 Max = Action == 8 ? Presets[1][Preset] : BeforeMax;
        const uint8 Input = Action == 9 ? Presets[2][Preset] : BeforeCurrent;
        return FMath::Min(FMath::Max(Input, Min), Max);
    }
    struct FComponent { bool Present = false; uint8 Base = 0, Final = 0; };
    struct FComponents { FComponent Values[3]; };
    struct FRun
    {
        int32 BeforeRing[2][4] = {};
        int32 BeforeBoundAttempt[2][2] = {};
        int32 LastIssued[2] = {0, 0};
        int32 ControlSerial = 0, Attempt = 0;
        double NextRetrySeconds = 0.0, NextDiagnostic = 0.0;
        bool ControlRejected = false;
        FComponents BeforeLive[2];
        TSharedPtr<FStructOnScope> Before;
        TSharedPtr<FStructOnScope> BeforeCommit;
    };
    auto FreshSerial(FRun& Run, int32 Owner, int32 Seed) -> int32
    {
        Run.LastIssued[Owner] = FMath::Max(Run.LastIssued[Owner] + 1, Seed);
        return Run.LastIssued[Owner];
    }

    auto Equal(const FComponents& A, const FComponents& B) -> bool
    {
        for (int32 I = 0; I < 3; ++I)
        {
            if (A.Values[I].Present != B.Values[I].Present ||
                A.Values[I].Base != B.Values[I].Base ||
                A.Values[I].Final != B.Values[I].Final) { return false; }
        }
        return true;
    }
    auto ReadComponents(AGameStateBase* S, const FFixture& F, int32 Station,
                        FComponents& Out) -> bool
    {
        const auto* D = Data(S, F);
        if (!D || (Station != 2 && Station != 5)) { return false; }
        FScriptArrayHelper Samples(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(D));
        if (Samples.Num() != 6) { return false; }
        const void* Row = Samples.GetRawPtr(Station);
        FScriptArrayHelper Items(F.Attributes, F.Attributes->ContainerPtrToValuePtr<void>(Row));
        if (Items.Num() != 1 || F.StationIndex->GetPropertyValue_InContainer(Row) != Station ||
            F.Name->GetPropertyValue_InContainer(Items.GetRawPtr(0)) !=
                FName(AttributeNames[Station][0])) { return false; }
        const void* A = Items.GetRawPtr(0);
        const FStructProperty* Properties[3] = {F.Min, F.Max, F.Current};
        Out = FComponents{};
        for (int32 I = 0; I < 3; ++I)
        {
            const void* C = Properties[I]->ContainerPtrToValuePtr<void>(A);
            Out.Values[I].Present = F.Present->GetPropertyValue_InContainer(C);
            if (Out.Values[I].Present)
            {
                Out.Values[I].Base = F.Base->GetPropertyValue_InContainer(C);
                Out.Values[I].Final = F.Final->GetPropertyValue_InContainer(C);
            }
        }
        return true;
    }
    auto LiveComponents(const FCk_Handle& Root, int32 Station, FComponents& Out) -> bool
    {
        if (!IsLive(Root) || (Station != 2 && Station != 5)) { return false; }
        const auto Tag = FGameplayTag::RequestGameplayTag(
            FName(AttributeNames[Station][0]), false);
        const auto A = Tag.IsValid() ?
            UCk_Utils_ByteAttribute_UE::TryGet(Root, Tag) : FCk_Handle_ByteAttribute{};
        if (!IsLive(FCk_Handle{A})) { return false; }
        const ECk_MinMaxCurrent Parts[3] = {
            ECk_MinMaxCurrent::Min, ECk_MinMaxCurrent::Max, ECk_MinMaxCurrent::Current};
        Out = FComponents{};
        for (int32 I = 0; I < 3; ++I)
        {
            Out.Values[I].Present = UCk_Utils_ByteAttribute_UE::Has_Component(A, Parts[I]);
            if (Out.Values[I].Present)
            {
                Out.Values[I].Base = UCk_Utils_ByteAttribute_UE::Get_BaseValue(A, Parts[I]);
                Out.Values[I].Final = UCk_Utils_ByteAttribute_UE::Get_FinalValue(A, Parts[I]);
            }
        }
        return true;
    }
    auto RootsRetained(UWorld* W, const FFixture& F) -> bool
    {
        auto* S = State(W, F);
        if (!Ready(S, F, F.InitialGeneration) ||
            F.ServerChildCount->GetPropertyValue_InContainer(S) != 6) { return false; }
        FScriptArrayHelper Slots(F.ServerChildren, F.ServerChildren->ContainerPtrToValuePtr<void>(S));
        if (Slots.Num() != 6) { return false; }
        for (int32 I = 0; I < 6; ++I)
        {
            if (!IsLive(F.ServerRoots[I]) ||
                *reinterpret_cast<const FCk_Handle*>(Slots.GetRawPtr(I)) != F.ServerRoots[I])
            { return false; }
        }
        return true;
    }
    auto Capture(FFixture& F, FRun& Run) -> bool
    {
        if (!CaptureBefore(F)) { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            auto* W = ck::auto_test::net::Get_ClientWorld(I);
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            if (!IsValid(PC) || !PC->IsA(F.Controller)) { return false; }
            for (int32 J = 0; J < 4; ++J)
            { Run.BeforeRing[I][J] = F.Ring[J]->GetPropertyValue_InContainer(PC); }
            for (int32 J = 0; J < 2; ++J)
            { Run.BeforeBoundAttempt[I][J] = F.BoundAttempt[J]->GetPropertyValue_InContainer(PC); }
        }
        if (!LiveComponents(F.ServerRoots[2], 2, Run.BeforeLive[0]) ||
            !LiveComponents(F.ServerRoots[5], 5, Run.BeforeLive[1])) { return false; }
        Run.Before = MakeShared<FStructOnScope>(F.Snapshot->Struct.Get());
        F.Snapshot->Struct->CopyScriptStruct(Run.Before->GetStructMemory(),
            Data(State(ck::auto_test::net::Get_ServerWorld(), F), F));
        Run.BeforeCommit = MakeShared<FStructOnScope>(F.ControlCommit->Struct.Get());
        F.ControlCommit->Struct->CopyScriptStruct(Run.BeforeCommit->GetStructMemory(),
            F.ControlCommit->ContainerPtrToValuePtr<void>(
                State(ck::auto_test::net::Get_ServerWorld(), F)));
        return true;
    }
    auto Rings(const FFixture& F, const FRun& Run, int32 Owner, int32 Ring,
               int32 NewIndex) -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        for (int32 I = 0; I < 2; ++I)
        {
            auto* AuthorityPC = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
            auto* W = ck::auto_test::net::Get_ClientWorld(I);
            auto* ClientPC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            if (!IsValid(AuthorityPC) || !AuthorityPC->IsA(F.Controller) ||
                !IsValid(ClientPC) || !ClientPC->IsA(F.Controller)) { return false; }
            if (I != Owner &&
                (F.LastSerial->GetPropertyValue_InContainer(AuthorityPC) != F.BeforeAccepted[I] ||
                 F.LastSerial->GetPropertyValue_InContainer(ClientPC) != F.BeforeAccepted[I]))
            { return false; }
            for (int32 J = 0; J < 4; ++J)
            {
                const int32 Expected = I == Owner && Ring == -2 ? -1 :
                    I == Owner && J == Ring ? NewIndex : Run.BeforeRing[I][J];
                if (F.Ring[J]->GetPropertyValue_InContainer(AuthorityPC) != Expected ||
                    F.Ring[J]->GetPropertyValue_InContainer(ClientPC) != Expected)
                { return false; }
            }
        }
        return true;
    }
    auto BoundAttempts(const FFixture& F, const FRun& Run, int32 Owner,
                       int32 ChangedBound, int32 NewIndex) -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        for (int32 I = 0; I < 2; ++I)
        {
            auto* AuthorityPC = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
            auto* W = ck::auto_test::net::Get_ClientWorld(I);
            auto* ClientPC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            if (!IsValid(AuthorityPC) || !AuthorityPC->IsA(F.Controller) ||
                !IsValid(ClientPC) || !ClientPC->IsA(F.Controller)) { return false; }
            for (int32 J = 0; J < 2; ++J)
            {
                const int32 Expected = I == Owner && ChangedBound == -2 ? -1 :
                    I == Owner && J == ChangedBound ? NewIndex : Run.BeforeBoundAttempt[I][J];
                if (F.BoundAttempt[J]->GetPropertyValue_InContainer(AuthorityPC) != Expected ||
                    F.BoundAttempt[J]->GetPropertyValue_InContainer(ClientPC) != Expected)
                { return false; }
            }
        }
        return true;
    }
    auto StationsEqual(AGameStateBase* S, const FFixture& F,
                       const FStructOnScope& Baseline) -> bool
    {
        const auto* D = Data(S, F);
        return D && F.Stations->Identical(
            F.Stations->ContainerPtrToValuePtr<void>(D),
            F.Stations->ContainerPtrToValuePtr<void>(Baseline.GetStructMemory()), 0);
    }
}
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkAttributeByteGym_PresetRings,
    "Ck.Gym.Network.AttributeByte.PresetRings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkAttributeByteGym_PresetRings::RunTest(const FString& Parameters)
{
    using namespace ck_attribute_byte_network_test;
    using namespace ck_attribute_byte_preset_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Attribute Byte network topology")); return false; }
    const auto F = MakeShared<FFixture>();
    const auto Run = MakeShared<FRun>();
    F->Test = this;
    F->Mode = FSoftClassPath{ModePath}.TryLoadClass<AGameModeBase>();
    F->State = FSoftClassPath{StatePath}.TryLoadClass<AGameStateBase>();
    F->Controller = FSoftClassPath{ControllerPath}.TryLoadClass<APlayerController>();
    bool Valid = true;
    Valid &= TestNotNull(TEXT("Byte mode"), F->Mode);
    Valid &= TestNotNull(TEXT("Byte state"), F->State);
    Valid &= TestNotNull(TEXT("Byte controller"), F->Controller);
    for (int32 I = 0; I < 6; ++I)
    { Valid &= TestNotNull(TEXT("original Byte script"), FSoftClassPath{OriginalPaths[I]}.TryLoadClass<UObject>()); }
    Valid &= TestNotNull(TEXT("Byte projection"), FSoftClassPath{ProjectionPath}.TryLoadClass<UObject>());
    if (!Valid) { return false; }
    F->Snapshot = FindFProperty<FStructProperty>(F->State, TEXT("Snapshot"));
    F->ControlCommit = FindFProperty<FStructProperty>(F->State, TEXT("LastControlCommit"));
    if (!TestNotNull(TEXT("Byte correlated control commit"), F->ControlCommit)) { return false; }
    const auto* Commit = F->ControlCommit->Struct.Get();
    F->CommitSnapshot = FindFProperty<FStructProperty>(Commit, TEXT("Snapshot"));
    F->CommitSerial = FindFProperty<FIntProperty>(Commit, TEXT("Serial"));
    F->CommitAction = FindFProperty<FIntProperty>(Commit, TEXT("Action"));
    F->CommitPreset = FindFProperty<FIntProperty>(Commit, TEXT("Preset"));
    if (!TestNotNull(TEXT("Byte Snapshot"), F->Snapshot)) { return false; }
    auto* Snap = F->Snapshot->Struct.Get();
    F->Generation = FindFProperty<FIntProperty>(Snap, TEXT("Generation"));
    F->Sequence = FindFProperty<FIntProperty>(Snap, TEXT("Sequence"));
    F->Ready = FindFProperty<FBoolProperty>(Snap, TEXT("Ready"));
    F->Failed = FindFProperty<FBoolProperty>(Snap, TEXT("Failed"));
    F->FailureReason = FindFProperty<FStrProperty>(Snap, TEXT("FailureReason"));
    F->Stations = FindFProperty<FArrayProperty>(Snap, TEXT("Stations"));
    Valid &= TestNotNull(TEXT("Snapshot.Stations"), F->Stations);
    if (!Valid) { return false; }
    auto* StationInner = CastField<FStructProperty>(F->Stations->Inner);
    Valid &= TestNotNull(TEXT("typed Byte station"), StationInner);
    if (!Valid) { return false; }
    auto* Station = StationInner->Struct.Get();
    F->StationIndex = FindFProperty<FIntProperty>(Station, TEXT("StationIndex"));
    F->AutoRunning = FindFProperty<FBoolProperty>(Station, TEXT("AutoRunning"));
    F->AutoStep = FindFProperty<FIntProperty>(Station, TEXT("AutoStep"));
    F->Attributes = FindFProperty<FArrayProperty>(Station, TEXT("Attributes"));
    F->ValueChangeCount = FindFProperty<FIntProperty>(Station, TEXT("ValueChangeCount"));
    F->ClampedCount = FindFProperty<FIntProperty>(Station, TEXT("ClampedCount"));
    F->LastPreClamp = FindFProperty<FByteProperty>(Station, TEXT("LastPreClampValue"));
    F->LastClamped = FindFProperty<FByteProperty>(Station, TEXT("LastClampedValue"));
    F->LastOverflow = FindFProperty<FByteProperty>(Station, TEXT("LastOverflow"));
    F->ClampSerials = FindFProperty<FArrayProperty>(Station, TEXT("ClampSerials"));
    F->BoundarySerial = FindFProperty<FIntProperty>(Station, TEXT("BoundarySerial"));
    F->ActiveModifierCount = FindFProperty<FIntProperty>(Station, TEXT("ActiveModifierCount"));
    F->ActiveModifierDeltas = FindFProperty<FArrayProperty>(Station, TEXT("ActiveModifierDeltas"));
    F->DelegateBoundMask = FindFProperty<FIntProperty>(Station, TEXT("DelegateBoundMask"));
    F->TotalAttributes = FindFProperty<FIntProperty>(Station, TEXT("TotalAttributes"));
    F->CombatBatchCount = FindFProperty<FIntProperty>(Station, TEXT("CombatBatchCount"));
    F->BatchEpoch = FindFProperty<FIntProperty>(Station, TEXT("BatchEpoch"));
    F->CombatPresent = FindFProperty<FBoolProperty>(Station, TEXT("CombatPresent"));
    F->Title = FindFProperty<FStrProperty>(Station, TEXT("Title"));
    F->Body = FindFProperty<FStrProperty>(Station, TEXT("Body"));
    Valid &= TestNotNull(TEXT("Station.Attributes"), F->Attributes);
    if (!Valid) { return false; }
    F->Attribute = CastField<FStructProperty>(F->Attributes->Inner);
    Valid &= TestNotNull(TEXT("typed Byte attribute"), F->Attribute);
    if (!Valid) { return false; }
    auto* A = F->Attribute->Struct.Get();
    F->PreClampFinal = FindFProperty<FByteProperty>(A, TEXT("PreClampFinal"));
    F->AttributeOverflow = FindFProperty<FByteProperty>(A, TEXT("ClampOverflow"));
    F->Name = FindFProperty<FNameProperty>(A, TEXT("Name"));
    F->Min = FindFProperty<FStructProperty>(A, TEXT("Min"));
    F->Max = FindFProperty<FStructProperty>(A, TEXT("Max"));
    F->Current = FindFProperty<FStructProperty>(A, TEXT("Current"));
    Valid &= TestNotNull(TEXT("Attribute.Current"), F->Current);
    if (!Valid) { return false; }
    auto* C = F->Current->Struct.Get();
    F->Present = FindFProperty<FBoolProperty>(C, TEXT("Present"));
    F->Base = FindFProperty<FByteProperty>(C, TEXT("Base"));
    F->Bonus = FindFProperty<FByteProperty>(C, TEXT("Bonus"));
    F->Final = FindFProperty<FByteProperty>(C, TEXT("Final"));
    F->ServerChildren = FindFProperty<FArrayProperty>(F->State, TEXT("ServerChildren"));
    F->ServerChildCount = FindFProperty<FIntProperty>(F->State, TEXT("ServerChildCount"));
    F->LocalGeneration = FindFProperty<FIntProperty>(F->State, TEXT("LocalProjectionGeneration"));
    F->LocalReady = FindFProperty<FBoolProperty>(F->State, TEXT("LocalProjectionReady"));
    F->LocalFailed = FindFProperty<FBoolProperty>(F->State, TEXT("LocalProjectionFailed"));
    F->LocalFailureReason = FindFProperty<FStrProperty>(F->State, TEXT("LocalProjectionFailureReason"));
    F->LastSerial = FindFProperty<FIntProperty>(F->Controller, TEXT("LastControlSerial"));
    F->AttemptSerial = FindFProperty<FIntProperty>(F->Controller, TEXT("LastControlAttemptSerial"));
    F->Result = FindFProperty<FIntProperty>(F->Controller, TEXT("LastControlResult"));
    for (int32 I = 0; I < 4; ++I)
    {
        const TCHAR* Names[4] = {TEXT("MinPresetIndex"), TEXT("MaxPresetIndex"),
            TEXT("CurrentPresetIndex"), TEXT("SignalPresetIndex")};
        F->Ring[I] = FindFProperty<FIntProperty>(F->Controller, Names[I]);
        Valid &= TestNotNull(TEXT("per-owner ring"), F->Ring[I]);
    }
    F->BoundAttempt[0] = FindFProperty<FIntProperty>(F->Controller, TEXT("MinAttemptPresetIndex"));
    F->BoundAttempt[1] = FindFProperty<FIntProperty>(F->Controller, TEXT("MaxAttemptPresetIndex"));
    Valid &= TestNotNull(TEXT("per-owner Min attempt cursor"), F->BoundAttempt[0]);
    Valid &= TestNotNull(TEXT("per-owner Max attempt cursor"), F->BoundAttempt[1]);
    F->Rpc = F->Controller->FindFunctionByName(TEXT("Server_RequestByteControl"));
    Valid &= TestNotNull(TEXT("Byte RPC"), F->Rpc);
    if (!Valid) { return false; }
    F->RpcSerial = FindFProperty<FIntProperty>(F->Rpc, TEXT("InSerial"));
    F->RpcAction = FindFProperty<FIntProperty>(F->Rpc, TEXT("InAction"));
    F->RpcPreset = FindFProperty<FIntProperty>(F->Rpc, TEXT("InPresetIndex"));
    FStructProperty* ChildHandle = F->ServerChildren
        ? CastField<FStructProperty>(F->ServerChildren->Inner) : nullptr;
    Valid &= TestTrue(TEXT("exact replicated Byte snapshot and RPC schema"),
        F->Snapshot->HasAnyPropertyFlags(CPF_Net) &&
        F->ControlCommit->HasAnyPropertyFlags(CPF_Net) &&
        F->CommitSnapshot && F->CommitSnapshot->Struct == Snap &&
        F->CommitSerial && F->CommitAction && F->CommitPreset &&
        Snap->GetName() == TEXT("CkAttributeGym_ByteSnapshot") &&
        Station->GetName() == TEXT("CkAttributeGym_ByteStationSample") &&
        A->GetName() == TEXT("CkAttributeGym_ByteAttributeSample") &&
        C->GetName() == TEXT("CkAttributeGym_ByteComponentSample") &&
        F->Generation && F->Sequence && F->Ready && F->Failed && F->FailureReason &&
        F->StationIndex && F->AutoRunning && F->AutoStep && F->ValueChangeCount &&
        F->ClampedCount && F->ActiveModifierCount && F->DelegateBoundMask &&
        F->ClampSerials && CastField<FIntProperty>(F->ClampSerials->Inner) &&
        F->BoundarySerial &&
        F->LastPreClamp && F->LastClamped && F->LastOverflow &&
        F->ActiveModifierDeltas && CastField<FByteProperty>(F->ActiveModifierDeltas->Inner) &&
        F->TotalAttributes && F->CombatBatchCount && F->BatchEpoch && F->CombatPresent &&
        F->Title && F->Body && F->Name && F->Min && F->Max &&
        F->Min->Struct == C && F->Max->Struct == C && F->Current->Struct == C &&
        F->Present && F->Base && F->Bonus && F->Final &&
        FindFProperty<FIntProperty>(Station, TEXT("MinChangeCount")) &&
        FindFProperty<FIntProperty>(Station, TEXT("MaxChangeCount")) &&
        FindFProperty<FIntProperty>(Station, TEXT("CurrentChangeCount")) &&
        FindFProperty<FIntProperty>(Station, TEXT("Delegate1Count")) &&
        FindFProperty<FIntProperty>(Station, TEXT("Delegate2Count")) &&
        FindFProperty<FIntProperty>(Station, TEXT("Delegate3Count")) &&
        F->PreClampFinal && F->AttributeOverflow &&
        ChildHandle && ChildHandle->Struct == FCk_Handle::StaticStruct() &&
        F->ServerChildCount && F->LocalGeneration && F->LocalReady && F->LocalFailed &&
        F->LocalFailureReason && F->LastSerial && F->AttemptSerial && F->Result &&
        F->LastSerial->HasAnyPropertyFlags(CPF_Net) &&
        F->AttemptSerial->HasAnyPropertyFlags(CPF_Net) &&
        F->Result->HasAnyPropertyFlags(CPF_Net) &&
        F->RpcSerial && F->RpcAction && F->RpcPreset &&
        F->RpcSerial->HasAnyPropertyFlags(CPF_Parm) &&
        F->RpcAction->HasAnyPropertyFlags(CPF_Parm) &&
        F->RpcPreset->HasAnyPropertyFlags(CPF_Parm) &&
        F->Rpc->NumParms == 3 && F->Rpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer));
    if (!Valid) { return false; }
    for (const auto* Ring : F->Ring)
    {
        if (!TestTrue(TEXT("replicated Byte owner ring"), Ring->HasAnyPropertyFlags(CPF_Net)))
        { return false; }
    }
    for (const auto* Cursor : F->BoundAttempt)
    {
        if (!TestTrue(TEXT("replicated Byte bound attempt cursor"),
            Cursor->HasAnyPropertyFlags(CPF_Net))) { return false; }
    }
    F->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, F->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([this, F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            const int32 G = Gen(S, *F);
            const bool Good = G > 0 && AllReady(*F, G, Dedicated) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1));
            if (!Good && FPlatformTime::Seconds() >= F->NextDiagnostic)
            {
                F->NextDiagnostic = FPlatformTime::Seconds() + 5.0;
                for (auto* W : {Server, ck::auto_test::net::Get_ClientWorld(0),
                    ck::auto_test::net::Get_ClientWorld(1)}) { AddInfo(Describe(W, *F)); }
            }
            return Good;
        }), 60.0, TEXT("Byte preset authority and both client projections ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F, Dedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            FSample Sample;
            F->InitialGeneration = Gen(S, *F);
            if (!Read(S, *F, Sample) || !Branches(Server, *F, true, Dedicated, Sample))
            { return TestTrue(TEXT("exact authority Byte sextet"), false); }
            FScriptArrayHelper Slots(F->ServerChildren,
                F->ServerChildren->ContainerPtrToValuePtr<void>(S));
            if (Slots.Num() != 6) { return TestTrue(TEXT("six public Byte root handles"), false); }
            for (int32 I = 0; I < 6; ++I)
            { F->ServerRoots[I] = *reinterpret_cast<const FCk_Handle*>(Slots.GetRawPtr(I)); }
            return TestTrue(TEXT("retained authority roots and two complete projections"),
                RootsRetained(Server, *F) && AllReady(*F, F->InitialGeneration, Dedicated));
        }), TEXT("capture world-owned Byte preset roots")));

    const auto Queue = [this, F, Run, Dedicated](int32 Owner, int32 Serial,
        int32 Action, int32 Preset, const TCHAR* Label)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Owner,
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, F, Run, Owner, Serial, Action, Preset](UWorld* W)
                {
                    if (Run->ControlRejected) { return; }
                    if (!TestTrue(TEXT("capture Byte preset baseline"), Capture(*F, *Run)))
                    { Run->ControlRejected = true; return; }
                    Run->ControlSerial = FreshSerial(*Run, Owner, Serial);
                    Run->NextRetrySeconds = FPlatformTime::Seconds() + 0.2;
                    Run->NextDiagnostic = FPlatformTime::Seconds() + 5.0;
                    if (!TestTrue(TEXT("actual remote owning PC sends Byte control"),
                        Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr,
                            *F, Run->ControlSerial, Action, Preset)))
                    { Run->ControlRejected = true; }
                })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([F, Run, Dedicated, Owner,
                Action, Preset]() -> bool
            {
                if (ck::auto_test::net::Get_HasSessionFailed() || Run->ControlRejected)
                { return true; } // FailSession or AddError has already failed this test.
                auto* Server = ck::auto_test::net::Get_ServerWorld();
                auto* S = State(Server, *F);
                auto* OwnerWorld = ck::auto_test::net::Get_ClientWorld(Owner);
                auto* PC = IsValid(OwnerWorld) ? OwnerWorld->GetFirstPlayerController() : nullptr;
                const int32 Serial = Run->ControlSerial;
                if (FPlatformTime::Seconds() >= Run->NextDiagnostic)
                {
                    Run->NextDiagnostic = FPlatformTime::Seconds() + 5.0;
                    FString Failing;
                    const auto Mark = [&Failing](bool Pass, const TCHAR* Name)
                    { if (!Pass) { Failing += FString::Printf(TEXT(" %s"), Name); } };
                    Mark(RootsRetained(Server, *F), TEXT("server-roots-retained"));
                    Mark(Result(OwnerWorld, *F, Serial, Serial, 1), TEXT("owner-accepted"));
                    Mark(Seq(S, *F) > F->BeforeServerSequence, TEXT("server-seq-new"));
                    for (int32 I = 0; I < 2; ++I)
                    {
                        auto* W = ck::auto_test::net::Get_ClientWorld(I);
                        Mark(WorldReady(W, *F, F->InitialGeneration, false, Dedicated),
                            I == 0 ? TEXT("client0-projection-ready") :
                                TEXT("client1-projection-ready"));
                        Mark(Catalog(Server, W),
                            I == 0 ? TEXT("client0-catalog") : TEXT("client1-catalog"));
                    }
                    Mark(!F->HistoryViolation, TEXT("history-coherent"));
                    if (Action >= 7 && Action <= 10 && Preset >= 0 && Preset < 4)
                    {
                        const int32 Station = Action == 10 ? 5 : 2;
                        const int32 Part = Action == 7 ? 0 : Action == 8 ? 1 : 2;
                        const auto& Before = Run->BeforeLive[Station == 2 ? 0 : 1];
                        const uint8 ExpectedCurrent = Action == 10 ? Presets[3][Preset] :
                            ExpectedCurrentAfterPreset(Action, Preset,
                                Before.Values[0].Final, Before.Values[1].Final,
                                Before.Values[2].Final);
                        const uint8 Selected = Part == 2 ?
                            ExpectedCurrent : Presets[Action - 7][Preset];
                        FComponents Actual, Published;
                        const bool Live = LiveComponents(F->ServerRoots[Station], Station, Actual);
                        const bool Sample = ReadComponents(S, *F, Station, Published);
                        Mark(Live, TEXT("authority-live-components"));
                        Mark(Sample, TEXT("server-sample-components"));
                        Mark(Live && Sample && Equal(Actual, Published),
                            TEXT("authority-equals-server-sample"));
                        if (Live)
                        {
                            Mark(Actual.Values[0].Present && Actual.Values[1].Present &&
                                Actual.Values[2].Present, TEXT("all-components-present"));
                            Mark(Actual.Values[Part].Base == Selected &&
                                Actual.Values[Part].Final == Selected,
                                TEXT("selected-base-final"));
                            Mark(Actual.Values[2].Base == ExpectedCurrent &&
                                Actual.Values[2].Final == ExpectedCurrent,
                                TEXT("current-base-final"));
                            Mark(Action == 7 || (Actual.Values[0].Base == Before.Values[0].Base &&
                                Actual.Values[0].Final == Before.Values[0].Final),
                                TEXT("unchanged-min"));
                            Mark(Action == 8 || (Actual.Values[1].Base == Before.Values[1].Base &&
                                Actual.Values[1].Final == Before.Values[1].Final),
                                TEXT("unchanged-max"));
                        }
                        Mark(Rings(*F, *Run, Owner, Action - 7, Preset),
                            TEXT("owner-rings-only"));
                        for (int32 I = 0; I < 2; ++I)
                        {
                            auto* C = State(ck::auto_test::net::Get_ClientWorld(I), *F);
                            FComponents Client;
                            Mark(MatchesHistory(C, *F, F->BeforeSequence[I]),
                                I == 0 ? TEXT("client0-history") : TEXT("client1-history"));
                            Mark(ReadComponents(C, *F, Station, Client) && Live &&
                                Equal(Client, Actual),
                                I == 0 ? TEXT("client0-typed-values") : TEXT("client1-typed-values"));
                        }
                        F->Test->AddInfo(FString::Printf(TEXT("Byte preset expected selected=%u current=%u before min=%u/%u max=%u/%u current=%u/%u fail=[%s]"),
                            Selected, ExpectedCurrent,
                            Before.Values[0].Base, Before.Values[0].Final,
                            Before.Values[1].Base, Before.Values[1].Final,
                            Before.Values[2].Base, Before.Values[2].Final, *Failing));
                        F->Test->AddInfo(TEXT("Byte preset live:") +
                            DescribeLiveParts(F->ServerRoots[Station], Station));
                    }
                    else if (Action == 18)
                    {
                        for (int32 I = 0; I < 2; ++I)
                        { Mark(ResetCommitMatches(S,
                            State(ck::auto_test::net::Get_ClientWorld(I), *F), *F,
                            Serial, F->BeforeServerSequence),
                            I == 0 ? TEXT("client0-reset-commit") : TEXT("client1-reset-commit")); }
                        Mark(Rings(*F, *Run, Owner, -2, -1), TEXT("reset-rings"));
                    }
                    else if (Action == 0)
                    {
                        FSample Published;
                        const bool ReadServer = Read(S, *F, Published);
                        Mark(ReadServer, TEXT("server-sample-read"));
                        if (ReadServer)
                        { for (const auto& Station : Published.Stations)
                            { Mark(!Station.Auto, TEXT("server-auto-paused")); } }
                        Mark(Rings(*F, *Run, Owner, -1, -1), TEXT("pause-rings"));
                        for (int32 I = 0; I < 2; ++I)
                        {
                            auto* C = State(ck::auto_test::net::Get_ClientWorld(I), *F);
                            FSample Client;
                            Mark(MatchesHistory(C, *F, F->BeforeSequence[I]),
                                I == 0 ? TEXT("client0-history") : TEXT("client1-history"));
                            const bool ReadClient = Read(C, *F, Client);
                            Mark(ReadClient, I == 0 ? TEXT("client0-read") : TEXT("client1-read"));
                            if (ReadClient)
                            { for (const auto& Station : Client.Stations)
                                { Mark(!Station.Auto, I == 0 ?
                                    TEXT("client0-auto-paused") : TEXT("client1-auto-paused")); } }
                        }
                    }
                    F->Test->AddInfo(DescribeControlWait(*F, Dedicated, Owner,
                        Serial, Action, Preset) + TEXT(" preset-fail=[") + Failing + TEXT("]"));
                    for (auto* W : {Server, ck::auto_test::net::Get_ClientWorld(0),
                        ck::auto_test::net::Get_ClientWorld(1)})
                    { F->Test->AddInfo(Describe(W, *F) + TEXT(" power:") +
                        DescribeParts(State(W, *F), *F, 2) + TEXT(" signals:") +
                        DescribeParts(State(W, *F), *F, 5) + TEXT(" clamping:") +
                        DescribeParts(State(W, *F), *F, 0)); }
                }
                if (IsValid(PC) && PC->IsA(F->Controller) &&
                    F->AttemptSerial->GetPropertyValue_InContainer(PC) == Serial)
                {
                    const int32 Code = F->Result->GetPropertyValue_InContainer(PC);
                    if (Code != 0 && Code != 1 && Code != 3)
                    {
                        F->Test->AddError(FString::Printf(
                            TEXT("Byte preset control action %d serial %d rejected with result %d"),
                            Action, Serial, Code));
                        Run->ControlRejected = true;
                        return true;
                    }
                    if (Code == 3 &&
                        !Result(OwnerWorld, *F, Serial, F->BeforeAccepted[Owner], 3))
                    {
                        F->Test->AddError(TEXT("Byte preset busy result changed accepted watermark"));
                        Run->ControlRejected = true;
                        return true;
                    }
                    if (Code == 3)
                    {
                        if (FPlatformTime::Seconds() < Run->NextRetrySeconds ||
                            !RootsRetained(Server, *F) ||
                            !AllReady(*F, F->InitialGeneration, Dedicated))
                        { return false; }
                        if (!Capture(*F, *Run))
                        {
                            F->Test->AddError(TEXT("Byte preset busy retry could not capture complete state"));
                            Run->ControlRejected = true;
                            return true;
                        }
                        Run->ControlSerial = FreshSerial(*Run, Owner, Serial + 1);
                        Run->NextRetrySeconds = FPlatformTime::Seconds() + 0.2;
                        if (!Invoke(PC, *F, Run->ControlSerial, Action, Preset))
                        {
                            F->Test->AddError(TEXT("Byte preset busy retry lost owning client PC"));
                            Run->ControlRejected = true;
                            return true;
                        }
                        return false;
                    }
                }
                if (!RootsRetained(Server, *F) ||
                    !Result(OwnerWorld, *F, Serial, Serial, 1) ||
                    Seq(S, *F) <= F->BeforeServerSequence ||
                    !AllReady(*F, F->InitialGeneration, Dedicated) ||
                    F->HistoryViolation) { return false; }
                if (Action == 18)
                {
                    for (int32 I = 0; I < 2; ++I)
                    {
                        if (!ResetCommitMatches(S,
                            State(ck::auto_test::net::Get_ClientWorld(I), *F),
                            *F, Serial, F->BeforeServerSequence)) { return false; }
                    }
                    return Rings(*F, *Run, Owner, -2, -1) &&
                        BoundAttempts(*F, *Run, Owner, -2, -1);
                }
                if (Action == 0)
                {
                    FSample A;
                    if (!Read(S, *F, A) || !Rings(*F, *Run, Owner, -1, -1) ||
                        !BoundAttempts(*F, *Run, Owner, -1, -1))
                    { return false; }
                    for (const auto& X : A.Stations) { if (X.Auto) { return false; } }
                    for (int32 I = 0; I < 2; ++I)
                    {
                        auto* C = State(ck::auto_test::net::Get_ClientWorld(I), *F);
                        FSample B;
                        if (!MatchesHistory(C, *F, F->BeforeSequence[I]) ||
                            !Read(C, *F, B)) { return false; }
                        for (const auto& X : B.Stations) { if (X.Auto) { return false; } }
                    }
                    return true;
                }
                const int32 Station = Action == 10 ? 5 : 2;
                FComponents Actual, Published;
                const int32 Part = Action == 7 ? 0 : Action == 8 ? 1 : 2;
                const auto& Before = Run->BeforeLive[Station == 2 ? 0 : 1];
                const uint8 ExpectedCurrent = Action == 10 ? Presets[3][Preset] :
                    ExpectedCurrentAfterPreset(Action, Preset,
                        Before.Values[0].Final, Before.Values[1].Final,
                        Before.Values[2].Final);
                const uint8 Selected = Part == 2 ?
                    ExpectedCurrent : Presets[Action - 7][Preset];
                if (!LiveComponents(F->ServerRoots[Station], Station, Actual) ||
                    !ReadComponents(S, *F, Station, Published) ||
                    !Equal(Actual, Published) ||
                    !Actual.Values[0].Present || !Actual.Values[1].Present ||
                    !Actual.Values[2].Present ||
                    Actual.Values[Part].Base != Selected ||
                    Actual.Values[Part].Final != Selected ||
                    Actual.Values[2].Base != ExpectedCurrent ||
                    Actual.Values[2].Final != ExpectedCurrent ||
                    (Action != 7 && (Actual.Values[0].Base != Before.Values[0].Base ||
                                     Actual.Values[0].Final != Before.Values[0].Final)) ||
                    (Action != 8 && (Actual.Values[1].Base != Before.Values[1].Base ||
                                     Actual.Values[1].Final != Before.Values[1].Final)) ||
                    !Rings(*F, *Run, Owner, Action - 7, Preset) ||
                    !BoundAttempts(*F, *Run, Owner,
                        Action == 7 || Action == 8 ? Action - 7 : -1, Preset))
                { return false; }
                for (int32 I = 0; I < 2; ++I)
                {
                    auto* C = State(ck::auto_test::net::Get_ClientWorld(I), *F);
                    FComponents Client;
                    if (!MatchesHistory(C, *F, F->BeforeSequence[I]) ||
                        !ReadComponents(C, *F, Station, Client) ||
                        !Equal(Client, Actual)) { return false; }
                }
                return true;
            }), 25.0, Label));
    };
    const auto QueueCrossed = [this, F, Run, Dedicated](int32 Owner, int32 Serial,
        int32 Action, const TCHAR* Label)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, F, Run, Owner, Serial, Action](UWorld* W)
                {
                    if (Run->ControlRejected) { return; }
                    auto* S = State(W, *F);
                    auto* PC = ck::auto_test::net::TryGet_RemoteClientPlayerController(W, Owner);
                    if (!TestTrue(TEXT("direct crossed Byte control has real authority context"),
                        IsValid(S) && IsValid(PC) && PC->IsA(F->Controller) &&
                        PC->GetWorld() == W && Capture(*F, *Run)))
                    { Run->ControlRejected = true; return; }
                    auto* ProbeClass = FSoftClassPath(
                        TEXT("/Script/Angelscript.Ck_AttributeByte_DirectControlProbe"))
                        .TryLoadClass<UObject>();
                    auto* Method = IsValid(ProbeClass) ?
                        ProbeClass->FindFunctionByName(TEXT("Invoke")) : nullptr;
                    auto* StateArg = Method ? FindFProperty<FObjectPropertyBase>(Method, TEXT("InState")) : nullptr;
                    auto* OwnerArg = Method ? FindFProperty<FObjectPropertyBase>(Method, TEXT("InOwner")) : nullptr;
                    auto* SerialArg = Method ? FindFProperty<FIntProperty>(Method, TEXT("InSerial")) : nullptr;
                    auto* ActionArg = Method ? FindFProperty<FIntProperty>(Method, TEXT("InAction")) : nullptr;
                    auto* PresetArg = Method ? FindFProperty<FIntProperty>(Method, TEXT("InPreset")) : nullptr;
                    auto* Return = Method ? FindFProperty<FIntProperty>(Method, TEXT("ReturnValue")) : nullptr;
                    if (!TestTrue(TEXT("direct crossed Byte probe exact reflected schema"),
                        Method && Method->NumParms == 6 && StateArg && OwnerArg &&
                        SerialArg && ActionArg && PresetArg && Return &&
                        StateArg->PropertyClass == F->State &&
                        OwnerArg->PropertyClass == F->Controller &&
                        Return->HasAnyPropertyFlags(CPF_ReturnParm)))
                    { Run->ControlRejected = true; return; }
                    auto* Probe = NewObject<UObject>(W, ProbeClass);
                    if (!TestTrue(TEXT("direct crossed Byte probe allocated"), IsValid(Probe)))
                    { Run->ControlRejected = true; return; }
                    FStructOnScope Params(Method);
                    auto* P = Params.GetStructMemory();
                    StateArg->SetObjectPropertyValue_InContainer(P, S);
                    OwnerArg->SetObjectPropertyValue_InContainer(P, PC);
                    SerialArg->SetPropertyValue_InContainer(P, 100000 + Serial);
                    ActionArg->SetPropertyValue_InContainer(P, Action);
                    PresetArg->SetPropertyValue_InContainer(P, 3);
                    const int32 BeforeAttempt = F->AttemptSerial->GetPropertyValue_InContainer(PC);
                    const int32 BeforeAccepted = F->LastSerial->GetPropertyValue_InContainer(PC);
                    const int32 BeforeResult = F->Result->GetPropertyValue_InContainer(PC);
                    Probe->ProcessEvent(Method, P);
                    FComponents MinMax, Signals;
                    const bool Preserved = Return->GetPropertyValue_InContainer(P) == 0 &&
                        !Failed(S, *F, F->InitialGeneration) &&
                        Run->Before.IsValid() && Run->BeforeCommit.IsValid() &&
                        F->Snapshot->Identical(Data(S, *F), Run->Before->GetStructMemory(), 0) &&
                        F->ControlCommit->Identical(
                            F->ControlCommit->ContainerPtrToValuePtr<void>(S),
                            Run->BeforeCommit->GetStructMemory(), 0) &&
                        LiveComponents(F->ServerRoots[2], 2, MinMax) &&
                        LiveComponents(F->ServerRoots[5], 5, Signals) &&
                        Equal(MinMax, Run->BeforeLive[0]) && Equal(Signals, Run->BeforeLive[1]) &&
                        F->AttemptSerial->GetPropertyValue_InContainer(PC) == BeforeAttempt &&
                        F->LastSerial->GetPropertyValue_InContainer(PC) == BeforeAccepted &&
                        F->Result->GetPropertyValue_InContainer(PC) == BeforeResult;
                    if (!TestTrue(TEXT("direct GameState crossed Byte bound rejects without state mutation"),
                        Preserved)) { Run->ControlRejected = true; }
                })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Owner,
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, F, Run, Owner, Serial, Action](UWorld* W)
                {
                    if (Run->ControlRejected) { return; }
                    if (!TestTrue(TEXT("capture crossed Byte bound baseline"), Capture(*F, *Run)))
                    { Run->ControlRejected = true; return; }
                    Run->Attempt = FreshSerial(*Run, Owner, Serial);
                    if (!TestTrue(TEXT("owner sends crossed Byte bound candidate"),
                        Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr,
                            *F, Run->Attempt, Action, 3)))
                    { Run->ControlRejected = true; }
                })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([F, Run, Dedicated, Owner, Action]() -> bool
            {
                if (ck::auto_test::net::Get_HasSessionFailed() || Run->ControlRejected)
                { return true; }
                auto* Server = ck::auto_test::net::Get_ServerWorld();
                auto* S = State(Server, *F);
                auto* OwnerWorld = ck::auto_test::net::Get_ClientWorld(Owner);
                auto* PC = IsValid(OwnerWorld) ? OwnerWorld->GetFirstPlayerController() : nullptr;
                if (IsValid(PC) && PC->IsA(F->Controller) &&
                    F->AttemptSerial->GetPropertyValue_InContainer(PC) == Run->Attempt)
                {
                    const int32 Code = F->Result->GetPropertyValue_InContainer(PC);
                    if (Code > 0 && Code != 4)
                    {
                        F->Test->AddError(TEXT("crossed Byte bound was admitted"));
                        Run->ControlRejected = true;
                        return true;
                    }
                }
                FComponents LiveMinMax, LiveSignals;
                if (!RootsRetained(Server, *F) ||
                    !Result(OwnerWorld, *F, Run->Attempt, F->BeforeAccepted[Owner], 4) ||
                    !Rings(*F, *Run, Owner, -1, -1) ||
                    !BoundAttempts(*F, *Run, Owner, Action - 7, 3) ||
                    !AllReady(*F, F->InitialGeneration, Dedicated) ||
                    Seq(S, *F) <= F->BeforeServerSequence ||
                    !Run->Before.IsValid() || !Run->BeforeCommit.IsValid() ||
                    !StationsEqual(S, *F, *Run->Before) ||
                    !F->ControlCommit->Identical(
                        F->ControlCommit->ContainerPtrToValuePtr<void>(S),
                        Run->BeforeCommit->GetStructMemory(), 0) ||
                    !LiveComponents(F->ServerRoots[2], 2, LiveMinMax) ||
                    !LiveComponents(F->ServerRoots[5], 5, LiveSignals) ||
                    !Equal(LiveMinMax, Run->BeforeLive[0]) ||
                    !Equal(LiveSignals, Run->BeforeLive[1])) { return false; }
                for (int32 I = 0; I < 2; ++I)
                {
                    auto* C = State(ck::auto_test::net::Get_ClientWorld(I), *F);
                    if (!MatchesHistory(C, *F, F->BeforeSequence[I]) ||
                        !StationsEqual(C, *F, *Run->Before) ||
                        !F->ControlCommit->Identical(
                            F->ControlCommit->ContainerPtrToValuePtr<void>(C),
                            Run->BeforeCommit->GetStructMemory(), 0))
                    { return false; }
                }
                return true;
            }), 25.0, Label));
    };
    for (int32 Ring = 0; Ring < 4; ++Ring)
    {
        const int32 BaseSerial = 1000 + Ring * 100;
        Queue(0, BaseSerial, 18, -1, TEXT("retained Byte reset and both-client commit"));
        Queue(1, BaseSerial + 1, 0, -1, TEXT("pause Byte station timers"));
        for (int32 Index = 0; Index < 4; ++Index)
        {
            if (Ring < 2 && Index == 3) { continue; }
            for (int32 Owner = 0; Owner < 2; ++Owner)
            {
                Queue(Owner, BaseSerial + 10 + Index * 2 + Owner,
                    Ring + 7, Index, TEXT("authored Byte preset from each remote owner"));
            }
        }
        if (Ring < 2)
        {
            for (int32 Owner = 0; Owner < 2; ++Owner)
            {
                QueueCrossed(Owner, BaseSerial + 16 + Owner, Ring + 7,
                    TEXT("crossed Byte bound rejects without changing accepted range or commit"));
                Queue(Owner, BaseSerial + 20 + Owner, Ring + 7, 0,
                    TEXT("next valid Byte bound key cycles beyond rejected candidate"));
            }
        }
    }
    for (int32 Owner = 0; Owner < 2; ++Owner)
    {
        for (int32 Kind = 0; Kind < 3; ++Kind)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Owner,
                FCk_NetAutoTest_ServerAction::CreateLambda(
                    [this, F, Run, Owner, Kind](UWorld* W)
                    {
                        if (Run->ControlRejected) { return; }
                        if (!TestTrue(TEXT("capture rejected Byte preset"), Capture(*F, *Run)))
                        { Run->ControlRejected = true; return; }
                        Run->Attempt = Kind == 2 ? F->BeforeAccepted[Owner] :
                            FreshSerial(*Run, Owner, 5000 + Owner * 100 + Kind);
                        const int32 BadPreset = Kind == 0 ? 2 : Kind == 1 ? 4 : 0;
                        if (!TestTrue(TEXT("actual owner sends wrong-next, out-of-range, or stale preset"),
                            Invoke(IsValid(W) ? W->GetFirstPlayerController() : nullptr,
                                *F, Run->Attempt, 7, BadPreset)))
                        { Run->ControlRejected = true; }
                    })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
                FCk_NetAutoTest_Condition::CreateLambda([F, Run, Dedicated, Owner]() -> bool
                {
                    if (ck::auto_test::net::Get_HasSessionFailed() || Run->ControlRejected)
                    { return true; } // FailSession or AddError has already failed this test.
                    auto* Server = ck::auto_test::net::Get_ServerWorld();
                    auto* S = State(Server, *F);
                    auto* OwnerWorld = ck::auto_test::net::Get_ClientWorld(Owner);
                    auto* PC = IsValid(OwnerWorld) ? OwnerWorld->GetFirstPlayerController() : nullptr;
                    if (IsValid(PC) && PC->IsA(F->Controller) &&
                        F->AttemptSerial->GetPropertyValue_InContainer(PC) == Run->Attempt)
                    {
                        const int32 Code = F->Result->GetPropertyValue_InContainer(PC);
                        if (Code > 0 && Code != 4)
                        {
                            F->Test->AddError(FString::Printf(
                                TEXT("invalid Byte preset serial %d admitted/result %d"),
                                Run->Attempt, Code));
                            Run->ControlRejected = true;
                            return true;
                        }
                    }
                    FComponents LiveMinMax, LiveSignals;
                    if (!RootsRetained(Server, *F) ||
                        !Result(ck::auto_test::net::Get_ClientWorld(Owner), *F,
                            Run->Attempt, F->BeforeAccepted[Owner], 4) ||
                        !Rings(*F, *Run, Owner, -1, -1) ||
                         !BoundAttempts(*F, *Run, Owner, -1, -1) ||
                        !AllReady(*F, F->InitialGeneration, Dedicated) ||
                        Seq(S, *F) <= F->BeforeServerSequence ||
                        !Run->Before.IsValid() || !StationsEqual(S, *F, *Run->Before) ||
                        !LiveComponents(F->ServerRoots[2], 2, LiveMinMax) ||
                        !LiveComponents(F->ServerRoots[5], 5, LiveSignals) ||
                        !Equal(LiveMinMax, Run->BeforeLive[0]) ||
                        !Equal(LiveSignals, Run->BeforeLive[1])) { return false; }
                    for (int32 I = 0; I < 2; ++I)
                    {
                        auto* C = State(ck::auto_test::net::Get_ClientWorld(I), *F);
                        if (!MatchesHistory(C, *F, F->BeforeSequence[I]) ||
                            !StationsEqual(C, *F, *Run->Before)) { return false; }
                    }
                    return true;
                }), 25.0, TEXT("rejected Byte preset leaves all typed station state and rings unchanged")));
        }
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            F->RestoreSettings();
            return TestTrue(TEXT("Byte preset PIE worlds drained"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
        }), TEXT("restore Byte preset startup settings"), true));
    return true;
}

#endif
