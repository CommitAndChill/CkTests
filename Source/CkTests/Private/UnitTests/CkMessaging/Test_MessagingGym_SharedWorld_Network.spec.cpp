#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_NetworkRuntime.h"
#include "CkGymRegistry_Utils.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkCore/Time/CkTime_Utils.h"
#include "CkDynamic/CkDynamic_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkMessaging/CkMessaging_Utils.h"
#include "CkMessaging/CkMessaging_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"

#include <Engine/World.h>
#include <EngineUtils.h>
#include <GameFramework/Pawn.h>
#include <Kismet/GameplayStatics.h>
#include <TimerManager.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/HUD.h>
#include <GameFramework/PlayerController.h>
#include <HAL/PlatformTime.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <StructUtils/InstancedStruct.h>
#include <UObject/UnrealType.h>

namespace ck_messaging_shared_world_test
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr const TCHAR* StationTags[5] = {TEXT("Gym.Messaging.BasicBroadcast"),
        TEXT("Gym.Messaging.MultiListener"), TEXT("Gym.Messaging.OneShot"),
        TEXT("Gym.Messaging.DynamicBind"), TEXT("Gym.Messaging.MultiType")};
    constexpr const TCHAR* ChildTags[5] = {TEXT("TAG_MessagingGym_Basic"),
        TEXT("TAG_MessagingGym_MultiListener"), TEXT("TAG_MessagingGym_OneShot"),
        TEXT("TAG_MessagingGym_DynamicBind"), TEXT("TAG_MessagingGym_MultiType")};
    constexpr const TCHAR* ChildNames[5] = {TEXT("Basic"), TEXT("MultiListener"),
        TEXT("OneShot"), TEXT("DynamicBind"), TEXT("MultiType")};
    constexpr const TCHAR* Titles[5] = {TEXT("BASIC BROADCAST & LISTEN"), TEXT("MULTI-LISTENER FAN-OUT"),
        TEXT("ONE-SHOT LISTENER"), TEXT("DYNAMIC BIND / UNBIND"), TEXT("MULTIPLE MESSAGE TYPES")};
    constexpr const TCHAR* MessageNames[5] = {TEXT("Ping"), TEXT("Pong"), TEXT("Alert"),
        TEXT("Reset"), TEXT("ToggleBind")};

    template<typename T> auto Prop(const UStruct* S, const TCHAR* N) -> T*
    { return IsValid(S) ? FindFProperty<T>(S, N) : nullptr; }
    auto Int(const UStruct* S, const void* V, const TCHAR* N) -> int32
    { const auto* P = Prop<FIntProperty>(S, N); return P && V ? P->GetPropertyValue_InContainer(V) : INDEX_NONE; }
    auto Bool(const UStruct* S, const void* V, const TCHAR* N) -> bool
    { const auto* P = Prop<FBoolProperty>(S, N); return P && V && P->GetPropertyValue_InContainer(V); }
    auto Str(const UStruct* S, const void* V, const TCHAR* N) -> FString
    { const auto* P = Prop<FStrProperty>(S, N); return P && V ? P->GetPropertyValue_InContainer(V) : FString{}; }
    auto Live(const FCk_Handle& H) -> bool
    { return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
        H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    auto Direct(const FCk_Handle& H, const FCk_Handle& Owner) -> bool
    { return Live(H) && Live(Owner) && UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H) &&
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H) == Owner; }
    auto Tagged(UWorld* W, const TCHAR* Tag) -> TArray<FCk_Handle>
    {
        const auto Root = IsValid(W) ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W) : FCk_Handle{};
        return Live(Root) ? UCk_Utils_EntityTag_UE::ForEach_Entity(Root, FName(Tag)) : TArray<FCk_Handle>{};
    }

    struct FExpected
    {
        int32 Basic = 0, LastSequence = -1, Fanout = 0, OneShot = 0, Dynamic = 0;
        int32 Bind = 1, Unbind = 0, Ping = 0, Pong = 0, Alert = 0, Priority = 0;
        int32 MessageSequence = 0;
        bool Listening = true, Bound = true;
        FString Sender = TEXT("N/A"), PingSender = TEXT("N/A"), PongSender = TEXT("N/A"), AlertText = TEXT("N/A");
    };
    struct FFixture
    {
        UClass *Mode = nullptr, *StateClass = nullptr, *PCClass = nullptr, *Scripts[5]{};
        UScriptStruct *Display = nullptr, *Anchors = nullptr;
        FStructProperty *Snapshot = nullptr, *Commit = nullptr, *CommitSnapshot = nullptr, *Sample = nullptr;
        FArrayProperty *Stations = nullptr, *Children = nullptr, *Queue = nullptr;
        UFunction* Rpc = nullptr;
        FIntProperty *RpcSerial = nullptr, *RpcRow = nullptr, *RpcPriority = nullptr;
        FGameplayTag Messages[5];
        FCk_Handle ServerChildren[5];
        int32 Generation = 0, NextSerial[2]{100, 200}, Accepted[2]{}, BeforeSequence = 0;
        TSharedPtr<FStructOnScope> Before;
        FExpected Expected;
        FString InitialReadiness[3];
        ECkGym_StartupMode OldStartup = ECkGym_StartupMode::Cycler;
        FString OldLast;
        TArray<FString> OldRecents;
        void CaptureSettings()
        {
            const auto* S = GetDefault<UCkGym_StartupSettings>();
            OldStartup = S->StartupMode; OldLast = S->LastGymName; OldRecents = S->RecentGymNames;
        }
        void RestoreSettings() const
        {
            auto* S = GetMutableDefault<UCkGym_StartupSettings>();
            S->StartupMode = OldStartup; S->LastGymName = OldLast; S->RecentGymNames = OldRecents; S->SaveConfig();
        }
    };
    auto State(UWorld* W, const FFixture& F) -> AGameStateBase*
    {
        auto* S = IsValid(W) ? W->GetGameState() : nullptr;
        return IsValid(S) && S->GetClass() == F.StateClass ? S : nullptr;
    }
    auto Data(UWorld* W, const FFixture& F) -> const void*
    { auto* S = State(W, F); return S ? F.Snapshot->ContainerPtrToValuePtr<void>(S) : nullptr; }
    auto PC(UWorld* W, const FFixture& F) -> APlayerController*
    {
        auto* P = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
        return IsValid(P) && P->GetClass() == F.PCClass ? P : nullptr;
    }
    auto ServerPC(int32 I, const FFixture& F) -> APlayerController*
    {
        auto* P = ck::auto_test::net::TryGet_RemoteClientPlayerController(ck::auto_test::net::Get_ServerWorld(), I);
        return IsValid(P) && P->GetClass() == F.PCClass ? P : nullptr;
    }
    auto Commit(APlayerController* P, const FFixture& F) -> const void*
    { return IsValid(P) ? F.Commit->ContainerPtrToValuePtr<void>(P) : nullptr; }
    auto CommitData(APlayerController* P, const FFixture& F) -> const void*
    { const auto* C = Commit(P, F); return C ? F.CommitSnapshot->ContainerPtrToValuePtr<void>(C) : nullptr; }
    auto Sample(const void* D, const FFixture& F, int32 Index) -> const void*
    {
        if (!D || Index < 0 || Index >= 5) { return nullptr; }
        FScriptArrayHelper A(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(D));
        return A.Num() == 5 ? A.GetRawPtr(Index) : nullptr;
    }
    auto Ready(UWorld* W, const FFixture& F) -> bool
    {
        const auto* D = Data(W, F); const auto* T = F.Snapshot->Struct.Get();
        return D && Bool(T, D, TEXT("Ready")) && !Bool(T, D, TEXT("Failed")) &&
            Int(T, D, TEXT("Generation")) > 0 && Int(T, D, TEXT("Sequence")) > 0 && Sample(D, F, 4);
    }
    auto Matches(const void* D, const FFixture& F, const FExpected& E) -> bool
    {
        const auto* T = F.Sample->Struct.Get(); const auto* S = F.Snapshot->Struct.Get();
        if (!D || !Bool(S, D, TEXT("Ready")) || Bool(S, D, TEXT("Failed")) ||
            Int(S, D, TEXT("MessageSequence")) != E.MessageSequence) { return false; }
        const auto* A = Sample(D, F, 0); const auto* B = Sample(D, F, 1);
        const auto* C = Sample(D, F, 2); const auto* X = Sample(D, F, 3); const auto* Y = Sample(D, F, 4);
        return A && B && C && X && Y &&
            Int(T, A, TEXT("PingCount")) == E.Basic && Int(T, A, TEXT("LastSequence")) == E.LastSequence &&
            Str(T, A, TEXT("LastSender")) == E.Sender &&
            Int(T, B, TEXT("ListenerA_Count")) == E.Fanout && Int(T, B, TEXT("ListenerB_Count")) == E.Fanout &&
            Int(T, B, TEXT("ListenerC_Count")) == E.Fanout &&
            Int(T, C, TEXT("PingCount")) == E.OneShot && Bool(T, C, TEXT("IsListening")) == E.Listening &&
            Int(T, X, TEXT("PingCount")) == E.Dynamic && Bool(T, X, TEXT("IsBound")) == E.Bound &&
            Int(T, X, TEXT("BindCount")) == E.Bind && Int(T, X, TEXT("UnbindCount")) == E.Unbind &&
            Int(T, Y, TEXT("PingCount")) == E.Ping && Int(T, Y, TEXT("PongCount")) == E.Pong &&
            Int(T, Y, TEXT("AlertCount")) == E.Alert && Str(T, Y, TEXT("LastPingSender")) == E.PingSender &&
            Str(T, Y, TEXT("LastPongResponder")) == E.PongSender &&
            Str(T, Y, TEXT("LastAlertText")) == E.AlertText && Int(T, Y, TEXT("LastAlertPriority")) == E.Priority;
    }
    auto Delegate(UObject* Script, const TCHAR* Name) -> FCk_Delegate_Messaging_OnBroadcast
    {
        FCk_Delegate_Messaging_OnBroadcast D;
        if (IsValid(Script) && Script->FindFunction(FName(Name))) { D.BindUFunction(Script, FName(Name)); }
        return D;
    }
    auto Binding(const FCk_Handle& H, UObject* Script, FGameplayTag Message,
        const TCHAR* Callback, ECk_Signal_PostFireBehavior Behavior, int32 Count) -> bool
    {
        return IsValid(Script) && Script->FindFunction(FName(Callback)) &&
            UCk_Utils_Messaging_UE::Get_NumUnconditionalBindingsTo_OnBroadcast(
            H, Message, Delegate(Script, Callback), Behavior) == Count;
    }
    auto Census(UWorld* W, const FFixture& F, bool Authority) -> bool
    {
        const auto* D = Data(W, F); const auto* T = F.Sample->Struct.Get();
        if (!IsValid(W) || !Ready(W, F)) { return false; }
        auto* S = State(W, F);
        if (!S) { return false; }
        FScriptArrayHelper Owned(F.Children, F.Children->ContainerPtrToValuePtr<void>(S));
        if (Authority && (Owned.Num() != 5 || Int(F.StateClass, S, TEXT("ServerChildCount")) != 5)) { return false; }
        FCk_Handle StationOwner;
        for (int32 I = 0; I < 5; ++I)
        {
            const auto Stations = Tagged(W, StationTags[I]); const auto Scripts = Tagged(W, ChildTags[I]);
            if (Stations.Num() != 1 || !Live(Stations[0]) ||
                !UCk_Utils_DynamicFragment_UE::Has_Fragment(Stations[0], F.Anchors)) { return false; }
            const auto* Value = Sample(D, F, I);
            const auto* Anchor = UCk_Utils_DynamicFragment_UE::TryGet_Fragment_TypeUnsafe(Stations[0], F.Anchors);
            const auto* Center = Anchor && Anchor->GetScriptStruct() == F.Anchors ?
                Prop<FStructProperty>(F.Anchors, TEXT("PanelCenter"))->ContainerPtrToValuePtr<FVector>(Anchor->GetMemory()) : nullptr;
            const auto* Xf = Value ? Prop<FStructProperty>(T, TEXT("InitialTransform"))->ContainerPtrToValuePtr<FTransform>(Value) : nullptr;
            if (!Value || !Center || !Xf || Center->ContainsNaN() || Xf->ContainsNaN() ||
                Int(T, Value, TEXT("StationIndex")) != I ||
                Prop<FNameProperty>(T, TEXT("StationTag"))->GetPropertyValue_InContainer(Value) != FName(StationTags[I]) ||
                Prop<FNameProperty>(T, TEXT("ChildTag"))->GetPropertyValue_InContainer(Value) != FName(ChildTags[I]) ||
                Str(T, Value, TEXT("Title")) != Titles[I] ||
                !Xf->GetLocation().Equals(*Center, 0.01) ||
                !Xf->GetRotation().Equals(FRotator(0, 180, 0).Quaternion(), 0.001)) { return false; }
            if (!Authority) { if (!Scripts.IsEmpty()) { return false; } continue; }
            if (!UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Stations[0])) { return false; }
            const auto Owner = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Stations[0]);
            const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
            if (!Direct(Owner, Root) || (I > 0 && Owner != StationOwner)) { return false; }
            StationOwner = Owner;
            if (Scripts.Num() != 1 || !Direct(Scripts[0], Stations[0])) { return false; }
            const auto H = Scripts[0]; auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
            if (*reinterpret_cast<const FCk_Handle*>(Owned.GetRawPtr(I)) != H) { return false; }
            if (!IsValid(Script) || Script->GetClass() != F.Scripts[I]) { return false; }
            TArray<FCk_Handle> Endpoints;
            const int32 Types[5] = {0, 1, 2, 3, 4};
            for (const int32 M : Types)
            {
                const bool Expected = M == 3 || M == 0 || (I == 3 && M == 4) || (I == 4 && (M == 1 || M == 2));
                const auto Endpoint = UCk_Utils_Messaging_UE::TryGet_MessengerEntity(H, F.Messages[M]);
                if (!Expected) { if (Live(Endpoint)) { return false; } continue; }
                if (!Direct(Endpoint, H) || Endpoints.Contains(Endpoint)) { return false; }
                Endpoints.Add(Endpoint);
                int32 Persistent = M == 0 ? (I == 1 ? 3 : I == 2 ? 0 : I == 3 ?
                    (Bool(T, Sample(D, F, I), TEXT("IsBound")) ? 1 : 0) : 1) : 1;
                int32 Once = I == 2 && M == 0 && Bool(T, Sample(D, F, I), TEXT("IsListening")) ? 1 : 0;
                if (UCk_Utils_Messaging_UE::Get_NumBoundTo_OnBroadcast(H, F.Messages[M],
                        ECk_Signal_PostFireBehavior::DoNothing) != Persistent ||
                    UCk_Utils_Messaging_UE::Get_NumBoundTo_OnBroadcast(H, F.Messages[M],
                        ECk_Signal_PostFireBehavior::Unbind) != Once) { return false; }
                if (I == 1 && M == 0)
                {
                    if (!Binding(H, Script, F.Messages[M], TEXT("OnPing_ListenerA"), ECk_Signal_PostFireBehavior::DoNothing, 1) ||
                        !Binding(H, Script, F.Messages[M], TEXT("OnPing_ListenerB"), ECk_Signal_PostFireBehavior::DoNothing, 1) ||
                        !Binding(H, Script, F.Messages[M], TEXT("OnPing_ListenerC"), ECk_Signal_PostFireBehavior::DoNothing, 1)) { return false; }
                }
                else
                {
                    const TCHAR* Callback = M == 0 ? TEXT("OnPing") : M == 1 ? TEXT("OnPong") :
                        M == 2 ? TEXT("OnAlert") : M == 3 ? TEXT("OnReset") : TEXT("OnToggleBind");
                    if (!Binding(H, Script, F.Messages[M], Callback,
                        I == 2 && M == 0 ? ECk_Signal_PostFireBehavior::Unbind : ECk_Signal_PostFireBehavior::DoNothing,
                        Persistent + Once)) { return false; }
                }
            }
            int32 LiveCount = 0;
            for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(H))
            {
                if (!Live(Child)) { continue; }
                if (!Endpoints.Contains(Child) || !Direct(Child, H) || UCk_Utils_Timer_UE::Has(Child)) { return false; }
                ++LiveCount;
            }
            if (LiveCount != Endpoints.Num()) { return false; }
        }
        return true;
    }
    auto Panels(UWorld* W, const FFixture& F) -> bool
    {
        auto* P = PC(W, F);
        if (!IsValid(P) || !P->IsLocalController() || !IsValid(P->GetHUD()) || P->GetHUD()->GetOwner() != P) { return false; }
        for (int32 I = 0; I < 5; ++I)
        {
            const auto Stations = Tagged(W, StationTags[I]); const auto* S = Sample(Data(W, F), F, I);
            if (Stations.Num() != 1 || !S || !Live(Stations[0]) ||
                !UCk_Utils_DynamicFragment_UE::Has_Fragment(Stations[0], F.Display)) { return false; }
            const auto* Frag = UCk_Utils_DynamicFragment_UE::TryGet_Fragment_TypeUnsafe(Stations[0], F.Display);
            if (!Frag || Frag->GetScriptStruct() != F.Display) { return false; }
            const auto* D = Frag->GetMemory(); const auto* T = F.Sample->Struct.Get();
            const auto* Title = Prop<FTextProperty>(F.Display, TEXT("Title"));
            const auto* Body = Prop<FTextProperty>(F.Display, TEXT("Description"));
            const auto* Instructions = Prop<FTextProperty>(F.Display, TEXT("Instructions"));
            if (Title->GetPropertyValue_InContainer(D).ToString() != Str(T, S, TEXT("Title")) ||
                Body->GetPropertyValue_InContainer(D).ToString() != Str(T, S, TEXT("Body")) ||
                Instructions->GetPropertyValue_InContainer(D).ToString() != Str(T, S, TEXT("Instructions"))) { return false; }
        }
        return true;
    }
    auto EquivalentReplicatedSnapshot(const void* Actual, const void* Expected, const FFixture& F) -> bool
    {
        if (!Actual || !Expected || !F.Snapshot || !F.Sample || !F.Stations ||
            !IsValid(F.Snapshot->Struct) || !IsValid(F.Sample->Struct) || F.Stations->Inner != F.Sample)
        { return false; }
        const auto* Transform = Prop<FStructProperty>(F.Sample->Struct, TEXT("InitialTransform"));
        if (!Transform || Transform->Struct != TBaseStructure<FTransform>::Get()) { return false; }
        for (TFieldIterator<FProperty> It(F.Snapshot->Struct.Get()); It; ++It)
        { if (*It != F.Stations && !It->Identical_InContainer(Actual, Expected)) { return false; } }
        FScriptArrayHelper A(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(Actual));
        FScriptArrayHelper B(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(Expected));
        if (A.Num() != 5 || B.Num() != 5) { return false; }
        for (int32 I = 0; I < 5; ++I)
        {
            const auto* AV = A.GetRawPtr(I); const auto* BV = B.GetRawPtr(I);
            for (TFieldIterator<FProperty> It(F.Sample->Struct.Get()); It; ++It)
            { if (*It != Transform && !It->Identical_InContainer(AV, BV)) { return false; } }
            const auto& AX = *Transform->ContainerPtrToValuePtr<FTransform>(AV);
            const auto& BX = *Transform->ContainerPtrToValuePtr<FTransform>(BV);
            const auto AQ = AX.GetRotation(); const auto BQ = BX.GetRotation();
            // Iris reconstructs unit-quaternion W. The authored 180-degree rotation
            // arrives as (Z=1,W=0) instead of (Z=1-epsilon,W=epsilon).
            if (AX.ContainsNaN() || BX.ContainsNaN() || !AQ.IsNormalized() || !BQ.IsNormalized() ||
                AX.GetLocation() != BX.GetLocation() || AX.GetScale3D() != BX.GetScale3D() ||
                !AQ.Equals(BQ, 1.e-12)) { return false; }
        }
        return true;
    }
    auto EquivalentReplicatedCommit(const void* Actual, const void* Expected, const FFixture& F) -> bool
    {
        if (!Actual || !Expected || !F.Commit || !F.CommitSnapshot || !F.Snapshot ||
            !IsValid(F.Commit->Struct) || F.CommitSnapshot->Struct != F.Snapshot->Struct) { return false; }
        for (TFieldIterator<FProperty> It(F.Commit->Struct.Get()); It; ++It)
        { if (*It != F.CommitSnapshot && !It->Identical_InContainer(Actual, Expected)) { return false; } }
        return EquivalentReplicatedSnapshot(F.CommitSnapshot->ContainerPtrToValuePtr<void>(Actual),
            F.CommitSnapshot->ContainerPtrToValuePtr<void>(Expected), F);
    }

    auto Converged(const FFixture& F, bool Dedicated) -> bool
    {
        auto* W = ck::auto_test::net::Get_ServerWorld(); const auto* D = Data(W, F);
        if (!Matches(D, F, F.Expected) || !Census(W, F, true) || (!Dedicated && !Panels(W, F))) { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            auto* C = ck::auto_test::net::Get_ClientWorld(I); const auto* CD = Data(C, F);
            if (!Ready(C, F) || !EquivalentReplicatedSnapshot(CD, D, F) || !Census(C, F, false) || !Panels(C, F)) { return false; }
        }
        return true;
    }
    auto SnapshotDifferenceTuple(const void* Actual, const void* Expected, const FFixture& F) -> FString
    {
        if (!Actual || !Expected || !F.Snapshot || !F.Sample || !F.Stations)
        { return TEXT("comparisonInputs=missing"); }
        FString Out;
        const auto Describe = [&Out](const FProperty* P, const void* A, const void* B, const FString& Prefix)
        {
            if (!P || !A || !B || P->Identical_InContainer(A, B)) { return; }
            FString ActualText, ExpectedText;
            P->ExportTextItem_Direct(ActualText, P->ContainerPtrToValuePtr<void>(A), nullptr, nullptr, PPF_None);
            P->ExportTextItem_Direct(ExpectedText, P->ContainerPtrToValuePtr<void>(B), nullptr, nullptr, PPF_None);
            ActualText.ReplaceInline(TEXT("\n"), TEXT(" ")); ExpectedText.ReplaceInline(TEXT("\n"), TEXT(" "));
            Out += FString::Printf(TEXT(" %s%s={actual:%s expected:%s}"), *Prefix, *P->GetName(),
                *ActualText.Left(256), *ExpectedText.Left(256));
            const auto* Struct = CastField<FStructProperty>(P);
            if (Struct && Struct->Struct == TBaseStructure<FTransform>::Get())
            {
                const auto& AX = *P->ContainerPtrToValuePtr<FTransform>(A);
                const auto& BX = *P->ContainerPtrToValuePtr<FTransform>(B);
                const auto AQ = AX.GetRotation(); const auto BQ = BX.GetRotation();
                Out += FString::Printf(TEXT(" transformDelta={location:%.17g scale:%.17g "
                    "actualQuaternion:%.17g,%.17g,%.17g,%.17g expectedQuaternion:%.17g,%.17g,%.17g,%.17g}"),
                    FVector::Dist(AX.GetLocation(), BX.GetLocation()), FVector::Dist(AX.GetScale3D(), BX.GetScale3D()),
                    AQ.X, AQ.Y, AQ.Z, AQ.W, BQ.X, BQ.Y, BQ.Z, BQ.W);
            }
        };
        for (TFieldIterator<FProperty> It(F.Snapshot->Struct.Get()); It; ++It)
        { if (*It != F.Stations) { Describe(*It, Actual, Expected, TEXT("snapshot.")); } }
        FScriptArrayHelper A(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(Actual));
        FScriptArrayHelper B(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(Expected));
        if (A.Num() != B.Num()) { Out += FString::Printf(TEXT(" samplesCount={actual:%d expected:%d}"), A.Num(), B.Num()); }
        for (int32 I = 0; I < FMath::Min(5, FMath::Min(A.Num(), B.Num())); ++I)
        {
            const auto Prefix = FString::Printf(TEXT("sample%d."), I);
            for (TFieldIterator<FProperty> It(F.Sample->Struct.Get()); It; ++It)
            { Describe(*It, A.GetRawPtr(I), B.GetRawPtr(I), Prefix); }
        }
        return Out.IsEmpty() ? TEXT("noReflectedFieldDifference") : Out;
    }

    auto InitialReadinessTuple(UWorld* W, const FFixture& F, bool Authority, bool Dedicated) -> FString
    {
        if (!IsValid(W)) { return TEXT("world=missing"); }
        auto* S = State(W, F); const auto* D = Data(W, F);
        if (!S || !D) { return TEXT("exactStateOrSnapshot=missing"); }
        const auto* T = F.Snapshot->Struct.Get(); const auto* ST = F.Sample->Struct.Get();
        FString Reason = Str(T, D, TEXT("FailureReason"));
        Reason.ReplaceInline(TEXT("\n"), TEXT(" ")); Reason.ReplaceInline(TEXT("\r"), TEXT(" "));
        const auto* ServerData = Data(ck::auto_test::net::Get_ServerWorld(), F);
        FString Out = FString::Printf(TEXT("mode=%d snapshot={gen:%d seq:%d message:%d ready:%d failed:%d reason:%s matchesInitial:%d sameServer:%d} "),
            static_cast<int32>(W->GetNetMode()), Int(T, D, TEXT("Generation")), Int(T, D, TEXT("Sequence")),
            Int(T, D, TEXT("MessageSequence")), Bool(T, D, TEXT("Ready")), Bool(T, D, TEXT("Failed")), *Reason,
            Matches(D, F, F.Expected), ServerData && F.Snapshot->Identical(D, ServerData, 0));
        const auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry(S);
        const auto Current = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex(S);
        auto* NativeState = Cast<ACkGym_GameState_UE>(S);
        auto* AuthMode = W->GetAuthGameMode(); auto* GameplayMode = UGameplayStatics::GetGameMode(S);
        int32 MessagingModes = 0;
        for (TActorIterator<AGameModeBase> It(W); It; ++It)
        { if (IsValid(*It) && It->IsA(F.Mode)) { ++MessagingModes; } }
        auto* CurrentMode = Registry.IsValidIndex(Current) ? Registry[Current].GameModeClass.Get() : nullptr;
        Out += FString::Printf(TEXT("identity={authority:%d catalog:%d gsExact:%d gsActual:%d messagingModes:%d authMode:%s authExact:%d gameplayAuth:%d registryCount:%d current:%d currentDisplay:%s currentMode:%s currentModeExact:%d startup:%d} "),
            S->HasAuthority(), IsValid(NativeState) && NativeState->Get_HasPublishedCatalog(), S->GetClass() == F.StateClass,
            W->GetGameState() == S, MessagingModes, IsValid(AuthMode) ? *AuthMode->GetClass()->GetPathName() : TEXT("missing"),
            IsValid(AuthMode) && AuthMode->GetClass() == F.Mode, GameplayMode == AuthMode,
            Registry.Num(), Current, Registry.IsValidIndex(Current) ? *Registry[Current].DisplayName : TEXT("missing"),
            IsValid(CurrentMode) ? *CurrentMode->GetPathName() : TEXT("missing"), CurrentMode == F.Mode,
            static_cast<int32>(GetDefault<UCkGym_StartupSettings>()->StartupMode));
        if (Authority)
        {
            for (int32 I = 0; I < 2; ++I)
            {
                auto* Remote = ServerPC(I, F); APawn* Pawn = IsValid(Remote) ? Remote->GetPawn() : nullptr;
                Out += FString::Printf(TEXT("serverOwner%d={pc:%d exact:%d world:%d authority:%d pawn:%d pawnWorld:%d reciprocal:%d} "),
                    I, IsValid(Remote), IsValid(Remote) && Remote->GetClass() == F.PCClass,
                    IsValid(Remote) && Remote->GetWorld() == W, IsValid(Remote) && Remote->HasAuthority(), IsValid(Pawn),
                    IsValid(Pawn) && Pawn->GetWorld() == W, IsValid(Pawn) && Pawn->GetController() == Remote);
            }
        }
        auto* P = PC(W, F); auto* H = IsValid(P) ? P->GetHUD() : nullptr;
        bool PresentationTimer = false;
        if (IsValid(P) && P->FindFunction(TEXT("OnPresentationTick")))
        {
            FTimerDynamicDelegate Callback; Callback.BindUFunction(P, TEXT("OnPresentationTick"));
            PresentationTimer = W->GetTimerManager().TimerExists(W->GetTimerManager().K2_FindDynamicTimerHandle(Callback));
        }
        Out += FString::Printf(TEXT("local={pc:%d local:%d hud:%d hudOwner:%d presentationTimer:%d shownGenProperty:%d shownGen:%d shownSeq:%d armedProperty:%d armed:%d} "),
            IsValid(P), IsValid(P) && P->IsLocalController(), IsValid(H), IsValid(H) && H->GetOwner() == P,
            PresentationTimer, Prop<FIntProperty>(F.PCClass, TEXT("_ShownGeneration")) != nullptr,
            Int(F.PCClass, P, TEXT("_ShownGeneration")), Int(F.PCClass, P, TEXT("_ShownSequence")),
            Prop<FBoolProperty>(F.PCClass, TEXT("_PresentationArmed")) != nullptr, Bool(F.PCClass, P, TEXT("_PresentationArmed")));
        FScriptArrayHelper Owned(F.Children, F.Children->ContainerPtrToValuePtr<void>(S));
        FScriptArrayHelper Samples(F.Stations, F.Stations->ContainerPtrToValuePtr<void>(D));
        Out += FString::Printf(TEXT("cohort={count:%d owned:%d samples:%d clientOriginalsExpected:%d} "),
            Int(F.StateClass, S, TEXT("ServerChildCount")), Owned.Num(), Samples.Num(), Authority ? 5 : 0);
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
        FCk_Handle StationOwner;
        for (int32 I = 0; I < 5; ++I)
        {
            const auto Stations = Tagged(W, StationTags[I]); const auto Originals = Tagged(W, ChildTags[I]);
            const auto Station = Stations.Num() == 1 && Live(Stations[0]) ? Stations[0] : FCk_Handle{};
            const bool StationLive = Live(Station);
            const bool HasAnchors = StationLive && UCk_Utils_DynamicFragment_UE::Has_Fragment(Station, F.Anchors);
            const bool HasDisplay = StationLive && UCk_Utils_DynamicFragment_UE::Has_Fragment(Station, F.Display);
            const auto* Anchor = HasAnchors ? UCk_Utils_DynamicFragment_UE::TryGet_Fragment_TypeUnsafe(Station, F.Anchors) : nullptr;
            const auto* Display = HasDisplay ? UCk_Utils_DynamicFragment_UE::TryGet_Fragment_TypeUnsafe(Station, F.Display) : nullptr;
            const auto* CenterProp = Prop<FStructProperty>(F.Anchors, TEXT("PanelCenter"));
            const auto* Center = Anchor && Anchor->GetScriptStruct() == F.Anchors && Anchor->GetMemory() && CenterProp ?
                CenterProp->ContainerPtrToValuePtr<FVector>(Anchor->GetMemory()) : nullptr;
            const auto* Value = Sample(D, F, I);
            const auto* TransformProp = Prop<FStructProperty>(ST, TEXT("InitialTransform"));
            const auto* Transform = Value && TransformProp ? TransformProp->ContainerPtrToValuePtr<FTransform>(Value) : nullptr;
            FCk_Handle Owner;
            if (StationLive && UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Station))
            { Owner = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Station); }
            if (I == 0) { StationOwner = Owner; }
            const auto Child = Originals.Num() == 1 && Live(Originals[0]) ? Originals[0] : FCk_Handle{};
            const bool ChildLive = Live(Child);
            auto* Script = ChildLive && UCk_Utils_EntityScript_UE::Has(Child) ? UCk_Utils_EntityScript_UE::TryGet_EntityScript(Child) : nullptr;
            const bool OwnedMatches = Owned.Num() == 5 && *reinterpret_cast<const FCk_Handle*>(Owned.GetRawPtr(I)) == Child;
            Out += FString::Printf(TEXT("station%d={tags:%d live:%d anchors:%d display:%d ownerLive:%d ownerDirectRoot:%d sameOwner:%d originalTags:%d originalLive:%d originalDirect:%d originalExact:%d ownedMatches:%d originalTimer:%d sample:%d index:%d tag:%s childTag:%s title:%s center:%s transform:%s geometry:%d"),
                I, Stations.Num(), StationLive, HasAnchors, HasDisplay, Live(Owner), Direct(Owner, Root), Owner == StationOwner,
                Originals.Num(), ChildLive, Direct(Child, Station), IsValid(Script) && Script->GetClass() == F.Scripts[I],
                OwnedMatches, ChildLive && UCk_Utils_Timer_UE::Has(Child), Value != nullptr,
                Int(ST, Value, TEXT("StationIndex")), Value ? *Prop<FNameProperty>(ST, TEXT("StationTag"))->GetPropertyValue_InContainer(Value).ToString() : TEXT("missing"),
                Value ? *Prop<FNameProperty>(ST, TEXT("ChildTag"))->GetPropertyValue_InContainer(Value).ToString() : TEXT("missing"),
                *Str(ST, Value, TEXT("Title")), Center ? *Center->ToString() : TEXT("missing"),
                Transform ? *Transform->ToString() : TEXT("missing"), Center && Transform && !Center->ContainsNaN() && !Transform->ContainsNaN() &&
                    Transform->GetLocation().Equals(*Center, 0.01) && Transform->GetRotation().Equals(FRotator(0, 180, 0).Quaternion(), 0.001));
            if (Value)
            {
                for (const auto* N : {TEXT("PingCount"), TEXT("PongCount"), TEXT("AlertCount"), TEXT("LastSequence"),
                    TEXT("ListenerA_Count"), TEXT("ListenerB_Count"), TEXT("ListenerC_Count"), TEXT("BindCount"), TEXT("UnbindCount"), TEXT("LastAlertPriority")})
                { Out += FString::Printf(TEXT(" %s:%d"), N, Int(ST, Value, N)); }
                Out += FString::Printf(TEXT(" listening:%d bound:%d sender:%s pingSender:%s pongSender:%s alertText:%s"),
                    Bool(ST, Value, TEXT("IsListening")), Bool(ST, Value, TEXT("IsBound")),
                    *Str(ST, Value, TEXT("LastSender")), *Str(ST, Value, TEXT("LastPingSender")),
                    *Str(ST, Value, TEXT("LastPongResponder")), *Str(ST, Value, TEXT("LastAlertText")));
            }
            if (Display && Display->GetScriptStruct() == F.Display && Display->GetMemory())
            {
                const TCHAR* TextFields[] = {TEXT("Title"), TEXT("Description"), TEXT("Instructions")};
                const TCHAR* SampleFields[] = {TEXT("Title"), TEXT("Body"), TEXT("Instructions")};
                for (int32 J = 0; J < 3; ++J)
                {
                    const auto* TextProp = Prop<FTextProperty>(F.Display, TextFields[J]);
                    if (!TextProp) { Out += TEXT(" textSchema=missing"); continue; }
                    auto Actual = TextProp->GetPropertyValue_InContainer(Display->GetMemory()).ToString();
                    auto Expected = Str(ST, Value, SampleFields[J]); const bool Equal = Value && Actual == Expected;
                    Out += FString::Printf(TEXT(" %sMatch:%d"), TextFields[J], Equal);
                    if (!Equal)
                    {
                        Actual.ReplaceInline(TEXT("\n"), TEXT("|")); Expected.ReplaceInline(TEXT("\n"), TEXT("|"));
                        Out += FString::Printf(TEXT(" actual:%s expected:%s"), *Actual, *Expected);
                    }
                }
            }
            Out += TEXT("}");
            if (!ChildLive) { continue; }
            TArray<FCk_Handle> Endpoints;
            for (int32 M = 0; M < 5; ++M)
            {
                const bool Expected = M == 3 || M == 0 || (I == 3 && M == 4) || (I == 4 && (M == 1 || M == 2));
                const auto Endpoint = UCk_Utils_Messaging_UE::TryGet_MessengerEntity(Child, F.Messages[M]);
                const bool DuplicateEndpoint = Live(Endpoint) && Endpoints.Contains(Endpoint);
                if (Live(Endpoint)) { Endpoints.Add(Endpoint); }
                const int32 ExpectedPersistent = !Expected ? 0 : M == 0 ?
                    (I == 1 ? 3 : I == 2 ? 0 : I == 3 ? (Bool(ST, Value, TEXT("IsBound")) ? 1 : 0) : 1) : 1;
                const int32 ExpectedOnce = I == 2 && M == 0 && Bool(ST, Value, TEXT("IsListening")) ? 1 : 0;
                Out += FString::Printf(TEXT(" channel%d.%d={expected:%d handle:%s duplicate:%d live:%d direct:%d timer:%d expectedPersistent:%d expectedOnce:%d persistent:%d once:%d"), I, M,
                    Expected, Live(Endpoint) ? *Endpoint.ToString() : TEXT("not-live"), DuplicateEndpoint,
                    Live(Endpoint), Direct(Endpoint, Child), Live(Endpoint) && UCk_Utils_Timer_UE::Has(Endpoint),
                    ExpectedPersistent, ExpectedOnce,
                    UCk_Utils_Messaging_UE::Get_NumBoundTo_OnBroadcast(Child, F.Messages[M], ECk_Signal_PostFireBehavior::DoNothing),
                    UCk_Utils_Messaging_UE::Get_NumBoundTo_OnBroadcast(Child, F.Messages[M], ECk_Signal_PostFireBehavior::Unbind));
                TArray<FName> Callbacks;
                if (I == 1 && M == 0)
                { Callbacks = {TEXT("OnPing_ListenerA"), TEXT("OnPing_ListenerB"), TEXT("OnPing_ListenerC")}; }
                else { Callbacks.Add(M == 0 ? TEXT("OnPing") : M == 1 ? TEXT("OnPong") : M == 2 ? TEXT("OnAlert") : M == 3 ? TEXT("OnReset") : TEXT("OnToggleBind")); }
                for (const auto Callback : Callbacks)
                {
                    const bool Callable = IsValid(Script) && Script->FindFunction(Callback);
                    auto DelegateValue = FCk_Delegate_Messaging_OnBroadcast();
                    if (Callable) { DelegateValue.BindUFunction(Script, Callback); }
                    const auto Behavior = I == 2 && M == 0 ? ECk_Signal_PostFireBehavior::Unbind : ECk_Signal_PostFireBehavior::DoNothing;
                    Out += FString::Printf(TEXT(" %sFunction:%d exact:%d"), *Callback.ToString(), Callable,
                        Callable ? UCk_Utils_Messaging_UE::Get_NumUnconditionalBindingsTo_OnBroadcast(Child, F.Messages[M], DelegateValue, Behavior) : INDEX_NONE);
                }
                Out += TEXT("}");
            }
            int32 Dependents = 0, Timers = 0, Unexpected = 0;
            for (const auto& Dependent : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Child))
            {
                if (!Live(Dependent)) { continue; }
                ++Dependents; if (UCk_Utils_Timer_UE::Has(Dependent)) { ++Timers; }
                if (!Endpoints.Contains(Dependent) || !Direct(Dependent, Child)) { ++Unexpected; }
            }
            Out += FString::Printf(TEXT(" dependencies%d={live:%d timers:%d unexpected:%d} "), I, Dependents, Timers, Unexpected);
        }
        Out += FString::Printf(TEXT(" summary={snapshotReady:%d panelRequired:%d panels:%d}"),
            Ready(W, F), !Dedicated, Dedicated || Panels(W, F));
        if (ServerData && !F.Snapshot->Identical(D, ServerData, 0))
        { Out += TEXT(" diffFromServer={") + SnapshotDifferenceTuple(D, ServerData, F) + TEXT("}"); }
        return Out;
    }

    auto Invoke(APlayerController* P, const FFixture& F, int32 Serial, int32 Row, int32 Priority = 0) -> bool
    {
        if (!IsValid(P) || P->GetClass() != F.PCClass) { return false; }
        FStructOnScope Params(F.Rpc); auto* M = Params.GetStructMemory();
        F.RpcSerial->SetPropertyValue_InContainer(M, Serial); F.RpcRow->SetPropertyValue_InContainer(M, Row);
        F.RpcPriority->SetPropertyValue_InContainer(M, Priority); P->ProcessEvent(F.Rpc, M); return true;
    }
    auto Capture(FFixture& F, UWorld* W) -> bool
    {
        if (!State(W, F) || !Ready(W, F)) { return false; }
        const auto* D = Data(W, F);
        if (!D) { return false; }
        for (int32 I = 0; I < 5; ++I) { if (!Sample(D, F, I)) { return false; } }
        auto Candidate = MakeShared<FStructOnScope>(F.Snapshot->Struct.Get());
        F.Snapshot->Struct->CopyScriptStruct(Candidate->GetStructMemory(), D);
        F.Before = Candidate; F.BeforeSequence = Int(F.Snapshot->Struct, D, TEXT("Sequence"));
        return true;
    }
    auto OwnerSettled(int32 Owner, int32 Serial, const FFixture& F) -> bool
    {
        auto* P = PC(ck::auto_test::net::Get_ClientWorld(Owner), F); auto* SP = ServerPC(Owner, F);
        const auto* C = Commit(P, F); const auto* SC = Commit(SP, F); const auto* T = F.Commit->Struct.Get();
        return P && SP && C && SC && Int(F.PCClass, P, TEXT("LastControlSerial")) == Serial &&
            Int(F.PCClass, SP, TEXT("LastControlSerial")) == Serial &&
            Int(T, C, TEXT("RequestSerial")) == Serial && EquivalentReplicatedCommit(C, SC, F) &&
            Int(F.PCClass, SP, TEXT("LastAcknowledgedDeliverySerial")) == Int(T, SC, TEXT("DeliverySerial"));
    }
    auto ApplyExpected(FExpected& E, int32 Row, int32 Priority) -> void
    {
        if (Row == 1) { E.Basic++; E.Sender = TEXT("Console"); E.LastSequence = E.MessageSequence; }
        if (Row == 2) { E.Fanout++; }
        if (Row == 3 && E.Listening) { E.OneShot++; E.Listening = false; }
        if (Row == 4 && E.Bound) { E.Dynamic++; }
        if (Row == 5) { E.Pong++; E.PongSender = TEXT("Console"); }
        if (Row == 6) { E.Alert++; E.AlertText = TEXT("Warning!"); E.Priority = Priority; }
        if (Row == 7) { E.Ping++; E.Pong++; E.Alert++; E.PingSender = TEXT("AllTypes");
            E.PongSender = TEXT("AllTypes"); E.AlertText = TEXT("Batch Alert"); E.Priority = 5; }
        if (Row == 9) { E.Bound = !E.Bound; if (E.Bound) { E.Bind++; } else { E.Unbind++; } }
        if (Row == 10) { E = FExpected{}; }
        else if ((Row >= 1 && Row <= 5) || Row == 7) { E.MessageSequence++; }
    }

    // Both hooks surround the production TimerManager tick (LevelTick.cpp). This
    // observes actual world ticks even when automation commands skip a frame.
    class FObservePair final : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        TSharedRef<FFixture> F;
        int32 Serial[2], Rows[2], BusySerial = 0, Before = 0, SeenSequence = 0;
        int64 LastCommitFrame = -1;
        int64 FirstCommitFrame = -1;
        int32 FirstOwner = INDEX_NONE, QueuedFirstOwner = INDEX_NONE;
        double Started = 0;
        bool SawQueuePair = false, Done = false;
        FDelegateHandle PreHook, PostHook;
        void Stop()
        {
            FWorldDelegates::OnWorldPreActorTick.Remove(PreHook);
            FWorldDelegates::OnWorldPostActorTick.Remove(PostHook);
            PreHook.Reset(); PostHook.Reset();
        }
        void Fail(const TCHAR* Message)
        { Test->AddError(Message); ck::auto_test::net::FailSession(Message); Done = true; }
        void Observe(UWorld* W)
        {
            if (Done || ck::auto_test::net::Get_HasSessionFailed() || W != ck::auto_test::net::Get_ServerWorld()) { return; }
            auto* S = State(W, *F); const auto* D = Data(W, *F); const auto* T = F->Snapshot->Struct.Get();
            if (!S || !D || !Ready(W, *F)) { Fail(TEXT("Messaging pair lost production state")); return; }
            FScriptArrayHelper Queue(F->Queue, F->Queue->ContainerPtrToValuePtr<void>(S));
            const auto* QueueInner = CastField<FStructProperty>(F->Queue->Inner);
            if (!QueueInner || !QueueInner->Struct) { Fail(TEXT("Messaging queue schema unavailable")); return; }
            const auto* QueueType = QueueInner->Struct.Get();
            bool Queued[2]{};
            TArray<int32> ObservedOrder;
            for (int32 I = 0; I < Queue.Num(); ++I)
            {
                const auto* Entry = Queue.GetRawPtr(I);
                const auto SerialInQueue = Int(QueueType, Entry, TEXT("RequestSerial"));
                const int32 OwnerIndex = SerialInQueue == Serial[0] ? 0 : SerialInQueue == Serial[1] ? 1 : INDEX_NONE;
                if (OwnerIndex >= 0)
                {
                    auto* ActualOwner = Prop<FObjectPropertyBase>(QueueType, TEXT("Owner"))->GetObjectPropertyValue_InContainer(Entry);
                    if (!IsValid(ActualOwner) || ActualOwner != ServerPC(OwnerIndex, *F) ||
                        Int(QueueType, Entry, TEXT("Row")) != Rows[OwnerIndex])
                    { Fail(TEXT("Messaging queued owner/action correlation malformed")); return; }
                    ObservedOrder.Add(OwnerIndex);
                }
                Queued[0] |= SerialInQueue == Serial[0]; Queued[1] |= SerialInQueue == Serial[1];
            }
            if (Queued[0] && Queued[1])
            {
                if (ObservedOrder.Num() != 2 || ObservedOrder[0] == ObservedOrder[1] ||
                    (QueuedFirstOwner >= 0 && QueuedFirstOwner != ObservedOrder[0]))
                { Fail(TEXT("Messaging observed FIFO queue reordered or duplicated")); return; }
                QueuedFirstOwner = ObservedOrder[0];
            }
            SawQueuePair |= Queued[0] && Queued[1];
            const int32 Sequence = Int(T, D, TEXT("Sequence"));
            if (Sequence == SeenSequence) { return; }
            const int64 Frame = UCk_Utils_Time_UE::Get_FrameCounter();
            if (Sequence != SeenSequence + 1 || Frame == LastCommitFrame)
            { Fail(TEXT("Messaging production published multiple actions in one actual engine frame")); return; }
            APlayerController* Owners[2] = {ServerPC(0, *F), ServerPC(1, *F)};
            if (!Owners[0] || !Owners[1]) { Fail(TEXT("Messaging pair owner unavailable")); return; }
            const bool Accepted[2] = {Int(F->PCClass, Owners[0], TEXT("LastControlSerial")) == Serial[0],
                Int(F->PCClass, Owners[1], TEXT("LastControlSerial")) == Serial[1]};
            if (Sequence == Before + 1)
            {
                if (Accepted[0] == Accepted[1]) { Fail(TEXT("Messaging first commit cannot be correlated")); return; }
                FirstOwner = Accepted[0] ? 0 : 1;
                if (!SawQueuePair || FirstOwner != QueuedFirstOwner)
                { Fail(TEXT("Messaging execution differs from observed FIFO admission order")); return; }
                FirstCommitFrame = Frame;
                if (Int(T, CommitData(Owners[FirstOwner], *F), TEXT("Sequence")) != Sequence)
                { Fail(TEXT("Messaging first owner snapshot correlation lost")); return; }
                auto Intermediate = F->Expected; ApplyExpected(Intermediate, Rows[FirstOwner], 0);
                if (!Matches(D, *F, Intermediate) || !Census(W, *F, true))
                { Fail(TEXT("Messaging first real callback/binding result differs from observed admission order")); return; }
            }
            else
            {
                if (FirstOwner < 0 || !Accepted[0] || !Accepted[1] || Sequence != Before + 2)
                { Fail(TEXT("Messaging second commit cannot be correlated")); return; }
                const int32 SecondOwner = 1 - FirstOwner;
                if (!Test->TestTrue(TEXT("both real owners queued before production execution"), SawQueuePair) ||
                    !Test->TestTrue(TEXT("earlier per-owner complete commit survives newer global snapshot"),
                        Int(T, CommitData(Owners[FirstOwner], *F), TEXT("Sequence")) == Before + 1 &&
                        Int(T, CommitData(Owners[SecondOwner], *F), TEXT("Sequence")) == Before + 2))
                { Fail(TEXT("Messaging concurrent admission/retention evidence incomplete")); return; }
                if (BusySerial > 0 && !Test->TestTrue(TEXT("newer busy attempt preserves original queued reservation"),
                    Int(F->PCClass, Owners[0], TEXT("LastControlAttemptSerial")) == BusySerial &&
                    Int(F->PCClass, Owners[0], TEXT("LastControlResult")) == 3 && Accepted[0]))
                { Fail(TEXT("Messaging busy request invalidated accepted queue slot")); return; }
                ApplyExpected(F->Expected, Rows[FirstOwner], 0); ApplyExpected(F->Expected, Rows[SecondOwner], 0);
                if (!Matches(D, *F, F->Expected) || !Census(W, *F, true))
                { Fail(TEXT("Messaging second real callback/binding result differs from observed admission order")); return; }
                F->Accepted[0] = Serial[0]; F->Accepted[1] = Serial[1]; Done = true;
                Test->AddInfo(FString::Printf(TEXT("Messaging actual FIFO: owner%d row%d frame%lld; owner%d row%d frame%lld"),
                    FirstOwner, Rows[FirstOwner], FirstCommitFrame, SecondOwner, Rows[SecondOwner], Frame));
            }
            SeenSequence = Sequence; LastCommitFrame = Frame;
        }
    public:
        FObservePair(FAutomationTestBase* InTest, TSharedRef<FFixture> InF,
            int32 A, int32 B, int32 RowA, int32 RowB, int32 InBusySerial = 0)
            : Test(InTest), F(InF), Serial{A, B}, Rows{RowA, RowB}, BusySerial(InBusySerial) {}
        ~FObservePair() override { Stop(); }
        bool Update() override
        {
            if (Done || ck::auto_test::net::Get_HasSessionFailed()) { Stop(); return true; }
            if (Started == 0)
            {
                Started = FPlatformTime::Seconds();
                const auto* D = Data(ck::auto_test::net::Get_ServerWorld(), *F);
                if (!D || !Ready(ck::auto_test::net::Get_ServerWorld(), *F))
                { Fail(TEXT("Messaging pair starts without complete live snapshot")); return true; }
                Before = Int(F->Snapshot->Struct, D, TEXT("Sequence")); SeenSequence = Before;
                PreHook = FWorldDelegates::OnWorldPreActorTick.AddLambda([this](UWorld* W, ELevelTick, float) { Observe(W); });
                PostHook = FWorldDelegates::OnWorldPostActorTick.AddLambda([this](UWorld* W, ELevelTick, float) { Observe(W); });
                for (int32 I = 0; I < 2; ++I)
                {
                    auto* P = PC(ck::auto_test::net::Get_ClientWorld(I), *F);
                    if (!Test->TestTrue(TEXT("concurrent controls enter both real owning RPCs"),
                        Invoke(P, *F, Serial[I], Rows[I])))
                    { Fail(TEXT("Messaging pair owning RPC unavailable")); Stop(); return true; }
                    if (I == 0 && BusySerial > 0)
                    {
                        if (!Test->TestTrue(TEXT("newer same-owner busy request traverses real RPC"), Invoke(P, *F, BusySerial, 1)))
                        { Fail(TEXT("Messaging pair busy RPC unavailable")); Stop(); return true; }
                    }
                }
            }
            if (FPlatformTime::Seconds() - Started > 20)
            {
                Fail(TEXT("Messaging concurrent production queue/commit observation timed out")); Stop(); return true;
            }
            return false;
        }
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkMessagingGym_SharedWorld_Network,
    "Ck.Gym.Network.Messaging.SharedWorld",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkMessagingGym_SharedWorld_Network::RunTest(const FString& Parameters)
{
    using namespace ck_messaging_shared_world_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Messaging topology")); return false; }
    const auto F = MakeShared<FFixture>();
    F->Mode = FSoftClassPath{TEXT("/Script/Angelscript.Ck_MessagingGym_GameMode")}.TryLoadClass<AGameModeBase>();
    F->StateClass = FSoftClassPath{TEXT("/Script/Angelscript.Ck_MessagingGym_GameState")}.TryLoadClass<AGameStateBase>();
    F->PCClass = FSoftClassPath{TEXT("/Script/Angelscript.Ck_MessagingGym_PlayerController")}.TryLoadClass<APlayerController>();
    F->Display = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Angelscript.CkGym_Station_TitleAndDescription"));
    F->Anchors = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Angelscript.CkGym_Station_Anchors"));
    bool Schema = TestNotNull(TEXT("real Messaging mode"), F->Mode) &&
        TestNotNull(TEXT("real Messaging state"), F->StateClass) && TestNotNull(TEXT("real Messaging PC"), F->PCClass) &&
        TestNotNull(TEXT("real display fragment"), F->Display) && TestNotNull(TEXT("real station anchors"), F->Anchors);
    for (int32 I = 0; I < 5; ++I)
    {
        F->Scripts[I] = FSoftClassPath{FString::Printf(TEXT("/Script/Angelscript.Ck_EntityScript_MessagingGym_%s"),
            ChildNames[I])}.TryLoadClass<UCk_EntityScript_UE>();
        F->Messages[I] = FGameplayTag::RequestGameplayTag(FName(*FString::Printf(
            TEXT("Message.Ck_Message_MessagingGym_%s"), MessageNames[I])), false);
        Schema &= TestNotNull(TEXT("original Messaging script"), F->Scripts[I]);
        Schema &= TestTrue(TEXT("registered original message type tag"), F->Messages[I].IsValid());
    }
    if (!Schema) { return false; }
    if (!TestTrue(TEXT("Messaging derives from native catalog owner"),
        F->StateClass->IsChildOf(ACkGym_GameState_UE::StaticClass()))) { return false; }
    F->Snapshot = Prop<FStructProperty>(F->StateClass, TEXT("Snapshot"));
    F->Commit = Prop<FStructProperty>(F->PCClass, TEXT("LastControlCommit"));
    F->Children = Prop<FArrayProperty>(F->StateClass, TEXT("ServerChildren"));
    F->Queue = Prop<FArrayProperty>(F->StateClass, TEXT("_QueuedControls"));
    F->Rpc = F->PCClass->FindFunctionByName(TEXT("Server_RequestMessagingControl"));
    if (!TestTrue(TEXT("required reflected Messaging production wire members"),
        F->Snapshot && F->Commit && F->Children && F->Queue && F->Rpc)) { return false; }
    F->Stations = Prop<FArrayProperty>(F->Snapshot->Struct, TEXT("Stations"));
    F->Sample = F->Stations ? CastField<FStructProperty>(F->Stations->Inner) : nullptr;
    F->CommitSnapshot = Prop<FStructProperty>(F->Commit->Struct, TEXT("Snapshot"));
    F->RpcSerial = Prop<FIntProperty>(F->Rpc, TEXT("InSerial"));
    F->RpcRow = Prop<FIntProperty>(F->Rpc, TEXT("InRow"));
    F->RpcPriority = Prop<FIntProperty>(F->Rpc, TEXT("InPriority"));
    const auto* OwnedType = CastField<FStructProperty>(F->Children->Inner);
    const auto* QueueType = CastField<FStructProperty>(F->Queue->Inner);
    Schema = OwnedType && OwnedType->Struct == FCk_Handle::StaticStruct() && QueueType &&
        QueueType->Struct->GetFName() == FName(TEXT("CkMessagingGym_QueuedControl")) &&
        Prop<FObjectPropertyBase>(QueueType->Struct, TEXT("Owner")) &&
        Prop<FIntProperty>(QueueType->Struct, TEXT("RequestSerial")) && Prop<FIntProperty>(QueueType->Struct, TEXT("Row")) &&
        F->Stations && F->Sample && F->CommitSnapshot &&
        F->CommitSnapshot->Struct == F->Snapshot->Struct &&
        F->Commit->Struct->GetFName() == FName(TEXT("CkMessagingGym_ControlCommit")) &&
        F->Snapshot->Struct->GetFName() == FName(TEXT("CkMessagingGym_Snapshot")) &&
        F->Sample->Struct->GetFName() == FName(TEXT("CkMessagingGym_StationSample")) &&
        F->Snapshot->HasAnyPropertyFlags(CPF_Net) && F->Commit->HasAnyPropertyFlags(CPF_Net) &&
        F->RpcSerial && F->RpcRow && F->RpcPriority && F->Rpc->NumParms == 3 &&
        F->Rpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer);
    const TCHAR* SnapInts[] = {TEXT("Generation"), TEXT("Sequence"), TEXT("MessageSequence")};
    for (const auto* N : SnapInts) { Schema &= Prop<FIntProperty>(F->Snapshot->Struct, N) != nullptr; }
    Schema &= Prop<FBoolProperty>(F->Snapshot->Struct, TEXT("Ready")) && Prop<FBoolProperty>(F->Snapshot->Struct, TEXT("Failed"));
    const TCHAR* OwnerInts[] = {TEXT("LastControlSerial"), TEXT("LastControlAttemptSerial"),
        TEXT("LastControlResult"), TEXT("LastAcknowledgedDeliverySerial")};
    for (const auto* N : OwnerInts) { auto* P = Prop<FIntProperty>(F->PCClass, N); Schema &= P && P->HasAnyPropertyFlags(CPF_Net); }
    const TCHAR* CommitInts[] = {TEXT("DeliverySerial"), TEXT("RequestSerial"), TEXT("Row"), TEXT("AlertPriority")};
    for (const auto* N : CommitInts) { Schema &= Prop<FIntProperty>(F->Commit->Struct, N) != nullptr; }
    if (!Schema) { AddError(TEXT("Messaging snapshot/RPC schema mismatch")); return false; }
    const TCHAR* SampleInts[] = {TEXT("StationIndex"), TEXT("PingCount"), TEXT("PongCount"), TEXT("AlertCount"),
        TEXT("LastSequence"), TEXT("ListenerA_Count"), TEXT("ListenerB_Count"), TEXT("ListenerC_Count"),
        TEXT("BindCount"), TEXT("UnbindCount"), TEXT("LastAlertPriority")};
    const TCHAR* SampleStrings[] = {TEXT("Title"), TEXT("Body"), TEXT("Instructions"), TEXT("LastSender"),
        TEXT("LastPingSender"), TEXT("LastPongResponder"), TEXT("LastAlertText")};
    for (const auto* N : SampleInts) { Schema &= Prop<FIntProperty>(F->Sample->Struct, N) != nullptr; }
    for (const auto* N : SampleStrings) { Schema &= Prop<FStrProperty>(F->Sample->Struct, N) != nullptr; }
    Schema &= Prop<FBoolProperty>(F->Sample->Struct, TEXT("IsListening")) &&
        Prop<FBoolProperty>(F->Sample->Struct, TEXT("IsBound"));
    const auto* Transform = Prop<FStructProperty>(F->Sample->Struct, TEXT("InitialTransform"));
    const auto* Center = Prop<FStructProperty>(F->Anchors, TEXT("PanelCenter"));
    Schema &= Prop<FNameProperty>(F->Sample->Struct, TEXT("StationTag")) &&
        Prop<FNameProperty>(F->Sample->Struct, TEXT("ChildTag")) && Transform &&
        Transform->Struct == TBaseStructure<FTransform>::Get() && Center && Center->Struct == TBaseStructure<FVector>::Get();
    for (const auto* N : {TEXT("Title"), TEXT("Description"), TEXT("Instructions")})
    { Schema &= Prop<FTextProperty>(F->Display, N) != nullptr; }
    if (!TestTrue(TEXT("exact typed Messaging samples and local text schema"), Schema)) { return false; }

    F->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, F->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            UWorld* Worlds[3] = {ck::auto_test::net::Get_ServerWorld(),
                ck::auto_test::net::Get_ClientWorld(0), ck::auto_test::net::Get_ClientWorld(1)};
            for (int32 I = 0; I < 3; ++I)
            {
                const auto Tuple = InitialReadinessTuple(Worlds[I], *F, I == 0, I == 0 && Dedicated);
                if (Tuple != F->InitialReadiness[I])
                {
                    F->InitialReadiness[I] = Tuple;
                    UE_LOG(LogTemp, Display, TEXT("[MessagingGymInitial] worldSlot=%d %s"), I, *Tuple);
                }
            }
            return Converged(*F, Dedicated) && ServerPC(0, *F) && ServerPC(1, *F);
        }),
        45, TEXT("one authoritative Messaging cohort and every local five-panel projection ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F, Dedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* W = ck::auto_test::net::Get_ServerWorld(); auto* S = State(W, *F);
            const auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry(S);
            const auto Current = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex(S);
            const auto* D = Data(W, *F);
            if (!TestTrue(TEXT("actual mode/topology and published Messaging catalog"),
                W && S && W->GetAuthGameMode() && W->GetAuthGameMode()->GetClass() == F->Mode &&
                W->GetNetMode() == (Dedicated ? NM_DedicatedServer : NM_ListenServer) &&
                Cast<ACkGym_GameState_UE>(S)->Get_HasPublishedCatalog() &&
                Registry.IsValidIndex(Current) && Registry[Current].DisplayName == TEXT("Messaging") &&
                Registry[Current].GameModeClass.Get() == F->Mode &&
                Int(F->StateClass, S, TEXT("ServerChildCount")) == 5 && Converged(*F, Dedicated)))
            { ck::auto_test::net::FailSession(TEXT("Messaging initial production admission rejected")); return false; }
            // Derive malformed complete copies from the real admitted cohort. The
            // rotation allowance must not hide metadata, counters, text or geometry changes.
            FStructOnScope Changed(F->Snapshot->Struct.Get());
            auto* ChangedData = Changed.GetStructMemory();
            const auto Restore = [&]() { F->Snapshot->Struct->CopyScriptStruct(ChangedData, D); };
            const auto Rejected = [&](const TCHAR* Label) -> bool
            { return TestFalse(Label, EquivalentReplicatedSnapshot(ChangedData, D, *F)); };
            Restore();
            Prop<FIntProperty>(F->Snapshot->Struct, TEXT("Sequence"))->SetPropertyValue_InContainer(
                ChangedData, Int(F->Snapshot->Struct, D, TEXT("Sequence")) + 1);
            bool ComparisonSafe = Rejected(TEXT("wire comparison rejects a different whole snapshot epoch"));
            Restore();
            auto* ChangedSample = const_cast<void*>(Sample(ChangedData, *F, 0));
            Prop<FIntProperty>(F->Sample->Struct, TEXT("PingCount"))->SetPropertyValue_InContainer(ChangedSample, 1);
            ComparisonSafe &= Rejected(TEXT("wire comparison rejects a changed actual listener count"));
            Restore(); ChangedSample = const_cast<void*>(Sample(ChangedData, *F, 0));
            Prop<FStrProperty>(F->Sample->Struct, TEXT("Body"))->SetPropertyValue_InContainer(ChangedSample, TEXT("changed"));
            ComparisonSafe &= Rejected(TEXT("wire comparison rejects changed copied station text"));
            Restore(); ChangedSample = const_cast<void*>(Sample(ChangedData, *F, 0));
            auto* ChangedTransform = Prop<FStructProperty>(F->Sample->Struct, TEXT("InitialTransform"))
                ->ContainerPtrToValuePtr<FTransform>(ChangedSample);
            ChangedTransform->AddToTranslation(FVector(0.001, 0, 0));
            ComparisonSafe &= Rejected(TEXT("wire comparison requires exact location"));
            Restore(); ChangedSample = const_cast<void*>(Sample(ChangedData, *F, 0));
            ChangedTransform = Prop<FStructProperty>(F->Sample->Struct, TEXT("InitialTransform"))
                ->ContainerPtrToValuePtr<FTransform>(ChangedSample);
            ChangedTransform->SetScale3D(FVector(1.001, 1, 1));
            ComparisonSafe &= Rejected(TEXT("wire comparison requires exact scale"));
            Restore(); ChangedSample = const_cast<void*>(Sample(ChangedData, *F, 0));
            ChangedTransform = Prop<FStructProperty>(F->Sample->Struct, TEXT("InitialTransform"))
                ->ContainerPtrToValuePtr<FTransform>(ChangedSample);
            ChangedTransform->SetRotation(FRotator(0, 180.001, 0).Quaternion());
            ComparisonSafe &= Rejected(TEXT("wire comparison rejects a changed rotation beyond roundoff"));
            Restore(); ChangedSample = const_cast<void*>(Sample(ChangedData, *F, 0));
            ChangedTransform = Prop<FStructProperty>(F->Sample->Struct, TEXT("InitialTransform"))
                ->ContainerPtrToValuePtr<FTransform>(ChangedSample);
            ChangedTransform->SetRotation(FQuat(0, 0, 0, 0));
            ComparisonSafe &= Rejected(TEXT("wire comparison rejects a non-unit rotation"));
            Restore();
            FScriptArrayHelper ChangedRows(F->Stations, F->Stations->ContainerPtrToValuePtr<void>(ChangedData));
            ChangedRows.RemoveValues(4, 1);
            ComparisonSafe &= Rejected(TEXT("wire comparison rejects a partial station cohort"));
            if (!ComparisonSafe) { ck::auto_test::net::FailSession(TEXT("Messaging wire comparison lost whole coherence")); return false; }
            F->Generation = Int(F->Snapshot->Struct, D, TEXT("Generation"));
            for (int32 I = 0; I < 5; ++I) { F->ServerChildren[I] = Tagged(W, ChildTags[I])[0]; }
            return true;
        }), TEXT("capture the five original production listeners")));

    auto QueueAction = [this, F, Dedicated](int32 Owner, int32 Row, int32 Priority, const TCHAR* Label)
    {
        const int32 Serial = F->NextSerial[Owner]++;
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Owner,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, F, Owner, Serial, Row, Priority](UWorld* W)
            {
                if (!TestTrue(TEXT("real owning Messaging RPC submitted"), Invoke(PC(W, *F), *F, Serial, Row, Priority)))
                { ck::auto_test::net::FailSession(TEXT("Messaging action owner unavailable")); return; }
                ApplyExpected(F->Expected, Row, Priority); F->Accepted[Owner] = Serial;
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([F, Owner, Serial, Dedicated]() -> bool
            { return OwnerSettled(Owner, Serial, *F) && Converged(*F, Dedicated); }), 20, Label));
    };
    // Exercise the ordinary AS commit boundary through a test-only reflected bridge.
    // All calls occur outside director execution against real PIE owners/states.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            auto Fail = [this](const TCHAR* Reason)
            { AddError(Reason); ck::auto_test::net::FailSession(Reason); };
            auto* Current = State(W, *F);
            auto* Foreign = State(ck::auto_test::net::Get_ClientWorld(0), *F);
            auto* Owner = ServerPC(0, *F);
            if (!IsValid(Current) || !IsValid(Foreign) || Foreign->GetWorld() == W ||
                !IsValid(Owner) || Owner->GetWorld() != W || !Capture(*F, W))
            { Fail(TEXT("Messaging commit probe real contexts unavailable")); return; }
            FScriptArrayHelper QueueBefore(F->Queue, F->Queue->ContainerPtrToValuePtr<void>(Current));
            if (QueueBefore.Num() != 0)
            { Fail(TEXT("Messaging commit probe requires idle production director")); return; }
            auto* ProbeClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_MessagingGym_CommitAdmissionProbe")).TryLoadClass<UObject>();
            auto* Method = IsValid(ProbeClass) ? ProbeClass->FindFunctionByName(TEXT("TryRecordCommit")) : nullptr;
            auto* InPC = Prop<FObjectPropertyBase>(Method, TEXT("InPC"));
            auto* InState = Prop<FObjectPropertyBase>(Method, TEXT("InState"));
            auto* InSerial = Prop<FIntProperty>(Method, TEXT("InSerial"));
            auto* Result = Prop<FBoolProperty>(Method, TEXT("ReturnValue"));
            auto* Admission = Prop<FBoolProperty>(ProbeClass, TEXT("AdmissionUnchanged"));
            if (!Method || Method->NumParms != 4 || !InPC || !InState || !InSerial || !Result || !Admission ||
                InPC->PropertyClass != F->PCClass || InState->PropertyClass != F->StateClass ||
                !InPC->HasAnyPropertyFlags(CPF_Parm) || !InState->HasAnyPropertyFlags(CPF_Parm) ||
                !InSerial->HasAnyPropertyFlags(CPF_Parm) || !Result->HasAnyPropertyFlags(CPF_ReturnParm))
            { Fail(TEXT("Messaging commit probe exact reflected schema unavailable")); return; }
            auto* Probe = NewObject<UObject>(W, ProbeClass);
            if (!IsValid(Probe)) { Fail(TEXT("Messaging commit probe allocation failed")); return; }
            const TCHAR* OwnerFields[] = {TEXT("LastControlSerial"), TEXT("LastControlAttemptSerial"),
                TEXT("LastControlResult"), TEXT("LastAcknowledgedDeliverySerial")};
            TSharedPtr<FStructOnScope> OwnerCommits[2];
            int32 OwnerValues[2][4]{};
            for (int32 I = 0; I < 2; ++I)
            {
                auto* P = ServerPC(I, *F); const auto* C = Commit(P, *F);
                if (!IsValid(P) || !C) { Fail(TEXT("Messaging commit probe owner capture failed")); return; }
                OwnerCommits[I] = MakeShared<FStructOnScope>(F->Commit->Struct.Get());
                F->Commit->Struct->CopyScriptStruct(OwnerCommits[I]->GetStructMemory(), C);
                for (int32 J = 0; J < 4; ++J) { OwnerValues[I][J] = Int(F->PCClass, P, OwnerFields[J]); }
            }
            AGameStateBase* CallerStates[] = {nullptr, nullptr, Current, Current, Foreign};
            const int32 Serials[] = {0, 123456, 0, 123456, 123456};
            for (int32 Case = 0; Case < UE_ARRAY_COUNT(Serials); ++Case)
            {
                FStructOnScope Parameters(Method); auto* Memory = Parameters.GetStructMemory();
                InPC->SetObjectPropertyValue_InContainer(Memory, Owner);
                InState->SetObjectPropertyValue_InContainer(Memory, CallerStates[Case]);
                InSerial->SetPropertyValue_InContainer(Memory, Serials[Case]);
                Probe->ProcessEvent(Method, Memory);
                bool Unchanged = !Result->GetPropertyValue_InContainer(Memory) &&
                    Admission->GetPropertyValue_InContainer(Probe);
                const auto* D = Data(W, *F);
                Unchanged = Unchanged && D && F->Before.IsValid() &&
                    F->Snapshot->Identical(D, F->Before->GetStructMemory(), 0);
                FScriptArrayHelper QueueAfter(F->Queue, F->Queue->ContainerPtrToValuePtr<void>(Current));
                Unchanged = Unchanged && QueueAfter.Num() == 0 && Census(W, *F, true);
                for (int32 I = 0; I < 2; ++I)
                {
                    auto* P = ServerPC(I, *F); const auto* C = Commit(P, *F);
                    Unchanged = Unchanged && IsValid(P) && C && F->Commit->Identical(C, OwnerCommits[I]->GetStructMemory(), 0);
                    for (int32 J = 0; J < 4; ++J)
                    { Unchanged = Unchanged && Int(F->PCClass, P, OwnerFields[J]) == OwnerValues[I][J]; }
                }
                if (!TestTrue(FString::Printf(TEXT("direct commit case %d rejects without owner/world/admission mutation"), Case), Unchanged))
                { Fail(TEXT("Messaging ordinary commit boundary mutated rejected input")); return; }
            }
            // World owns the stateless probe; existing unconditional EndPIE drains it.
        })));

    // A real native Broadcast of Ping followed immediately by Reset in one engine
    // frame reproduces retained-payload replay without bypassing any station callback.
    const auto ResetFrame = MakeShared<int64>(-1);
    const auto ProbeOneShot = [this, F](UWorld* W, bool Fresh) -> bool
    {
        auto Fail = [this](const TCHAR* Reason) -> bool
        { AddError(Reason); ck::auto_test::net::FailSession(Reason); return false; };
        if (!IsValid(W) || W != ck::auto_test::net::Get_ServerWorld() ||
            !Ready(W, *F) || !Census(W, *F, true))
        { return Fail(TEXT("OneShot native replay probe requires the complete real authority cohort")); }
        const auto Children = Tagged(W, ChildTags[2]); const auto Stations = Tagged(W, StationTags[2]);
        auto Child = F->ServerChildren[2];
        if (Children.Num() != 1 || Stations.Num() != 1 || Children[0] != Child || !Direct(Child, Stations[0]))
        { return Fail(TEXT("OneShot native replay probe lost the original direct child")); }
        auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Child);
        auto* PingType = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Angelscript.Ck_Message_MessagingGym_Ping"));
        auto* ResetType = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Angelscript.Ck_Message_MessagingGym_Reset"));
        auto* Count = IsValid(Script) ? Prop<FIntProperty>(Script->GetClass(), TEXT("PingCount")) : nullptr;
        auto* Listening = IsValid(Script) ? Prop<FBoolProperty>(Script->GetClass(), TEXT("IsListening")) : nullptr;
        auto* Sender = Prop<FStrProperty>(PingType, TEXT("Sender"));
        auto* Sequence = Prop<FIntProperty>(PingType, TEXT("SequenceNumber"));
        if (!IsValid(Script))
        { return Fail(TEXT("OneShot schema: actual original script UObject is unavailable")); }
        if (Script->GetClass() != F->Scripts[2])
        { return Fail(TEXT("OneShot schema: actual original script class differs")); }
        if (!IsValid(PingType))
        { return Fail(TEXT("OneShot schema: generated Ping USTRUCT /Script/Angelscript.Ck_Message_MessagingGym_Ping is unavailable")); }
        if (!IsValid(ResetType))
        { return Fail(TEXT("OneShot schema: generated Reset USTRUCT /Script/Angelscript.Ck_Message_MessagingGym_Reset is unavailable")); }
        if (PingType->GetPathName() != TEXT("/Script/Angelscript.Ck_Message_MessagingGym_Ping") ||
            ResetType->GetPathName() != TEXT("/Script/Angelscript.Ck_Message_MessagingGym_Reset"))
        { return Fail(TEXT("OneShot schema: generated message USTRUCT exact identity differs")); }
        if (!Count)
        { return Fail(TEXT("OneShot schema: original PingCount reflected int32 property is unavailable")); }
        if (!Listening)
        { return Fail(TEXT("OneShot schema: original IsListening reflected bool property is unavailable")); }
        if (!Count->HasAllPropertyFlags(CPF_Transient | CPF_BlueprintReadOnly) ||
            !Listening->HasAllPropertyFlags(CPF_Transient | CPF_BlueprintReadOnly) ||
            Count->HasAnyPropertyFlags(CPF_Net) || Listening->HasAnyPropertyFlags(CPF_Net))
        { return Fail(TEXT("OneShot schema: observation properties must be transient read-only and nonreplicated")); }
        if (!Sender)
        { return Fail(TEXT("OneShot schema: original Ping Sender reflected FString property is unavailable")); }
        if (!Sequence)
        { return Fail(TEXT("OneShot schema: original Ping SequenceNumber reflected int32 property is unavailable")); }
        if (!F->Messages[0].IsValid() ||
            F->Messages[0].GetTagName() != FName(*FString::Printf(TEXT("Message.%s"), *PingType->GetName())))
        { return Fail(TEXT("OneShot schema: original Ping message tag/type correlation differs")); }
        if (!F->Messages[3].IsValid() ||
            F->Messages[3].GetTagName() != FName(*FString::Printf(TEXT("Message.%s"), *ResetType->GetName())))
        { return Fail(TEXT("OneShot schema: original Reset message tag/type correlation differs")); }
        FInstancedStruct Ping; Ping.InitializeAs(PingType);
        FInstancedStruct Reset; Reset.InitializeAs(ResetType);
        if (!Ping.IsValid() || Ping.GetScriptStruct() != PingType || !Reset.IsValid() || Reset.GetScriptStruct() != ResetType)
        { return Fail(TEXT("OneShot native replay probe typed payload construction failed")); }
        Sender->SetPropertyValue_InContainer(Ping.GetMutableMemory(), TEXT("Native same-frame regression"));
        Sequence->SetPropertyValue_InContainer(Ping.GetMutableMemory(), Fresh ? 2 : 1);
        const auto Check = [&](int32 Expected, bool Armed) -> bool
        {
            if (!Live(Child) || !IsValid(Script) || Script->GetClass() != F->Scripts[2] ||
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(Child) != Script)
            { return false; }
            return Count->GetPropertyValue_InContainer(Script) == Expected &&
                Listening->GetPropertyValue_InContainer(Script) == Armed &&
                UCk_Utils_Messaging_UE::Get_NumBoundTo_OnBroadcast(Child, F->Messages[0], ECk_Signal_PostFireBehavior::DoNothing) == 0 &&
                UCk_Utils_Messaging_UE::Get_NumBoundTo_OnBroadcast(Child, F->Messages[0], ECk_Signal_PostFireBehavior::Unbind) == (Armed ? 1 : 0) &&
                Binding(Child, Script, F->Messages[0], TEXT("OnPing"), ECk_Signal_PostFireBehavior::Unbind, Armed ? 1 : 0) &&
                Binding(Child, Script, F->Messages[3], TEXT("OnReset"), ECk_Signal_PostFireBehavior::DoNothing, 1);
        };
        if (!Check(0, true))
        { return Fail(TEXT("OneShot native replay probe must start with the original armed listener")); }
        if (!Fresh)
        {
            UCk_Utils_Messaging_UE::Broadcast(Child, F->Messages[3], FInstancedStruct{});
            if (!TestTrue(TEXT("default Reset payload rejects through real dispatch without state/binding mutation"),
                Check(0, true) && Census(W, *F, true)))
            { return Fail(TEXT("OneShot default Reset payload mutated production state")); }
            UCk_Utils_Messaging_UE::Broadcast(Child, F->Messages[3], Ping);
            if (!TestTrue(TEXT("wrong typed Reset payload rejects through real dispatch without state/binding mutation"),
                Check(0, true) && Census(W, *F, true)))
            { return Fail(TEXT("OneShot wrong typed Reset payload mutated production state")); }
        }
        const auto Frame = UCk_Utils_Time_UE::Get_FrameCounter();
        UCk_Utils_Messaging_UE::Broadcast(Child, F->Messages[0], Ping);
        if (!TestTrue(TEXT("real OneShot Ping consumes exactly one callback/binding"), Check(1, false)))
        { return Fail(TEXT("OneShot native Ping did not consume the original listener")); }
        if (Fresh)
        {
            UCk_Utils_Messaging_UE::Broadcast(Child, F->Messages[0], Ping);
            if (!TestTrue(TEXT("second fresh-frame OneShot broadcast cannot invoke consumed callback"), Check(1, false)))
            { return Fail(TEXT("OneShot consumed callback fired more than once")); }
        }
        UCk_Utils_Messaging_UE::Broadcast(Child, F->Messages[3], Reset);
        if (!TestTrue(TEXT("same-frame Reset ignores cached Ping and rearms exact listener/counter/flag"),
            UCk_Utils_Time_UE::Get_FrameCounter() == Frame && Check(0, true) && Census(W, *F, true)))
        { return Fail(TEXT("OneShot Reset replayed retained Ping or lost the whole authority cohort")); }
        // An already-armed Reset is idempotent, also while the consumed Ping remains cached.
        UCk_Utils_Messaging_UE::Broadcast(Child, F->Messages[3], Reset);
        if (!TestTrue(TEXT("same-frame repeated armed Reset keeps exactly one listener"), Check(0, true) && Census(W, *F, true)))
        { return Fail(TEXT("OneShot armed Reset duplicated or lost its binding")); }
        return true;
    };
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([ProbeOneShot, ResetFrame](UWorld* W)
        { if (ProbeOneShot(W, false)) { *ResetFrame = UCk_Utils_Time_UE::Get_FrameCounter(); } })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([ResetFrame]() -> bool
        { return *ResetFrame >= 0 && UCk_Utils_Time_UE::Get_FrameCounter() > *ResetFrame; }),
        10, TEXT("a later actual engine frame before the next fresh OneShot Ping")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([ProbeOneShot](UWorld* W) { ProbeOneShot(W, true); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool { return Converged(*F, Dedicated); }),
        20, TEXT("native same-frame OneShot regression preserves the complete shared baseline")));

    QueueAction(0, 1, 0, TEXT("Basic real OnPing count, sender and sequence"));
    QueueAction(1, 2, 0, TEXT("three real fan-out callbacks"));
    QueueAction(0, 3, 0, TEXT("one-shot fires and real native binding goes to zero"));
    QueueAction(1, 3, 0, TEXT("second one-shot ping ignored"));
    QueueAction(0, 4, 0, TEXT("bound dynamic real OnPing"));
    QueueAction(1, 5, 0, TEXT("MultiType real OnPong"));
    QueueAction(0, 6, 1, TEXT("owner zero Alert ring first preset"));
    QueueAction(0, 6, 5, TEXT("owner zero Alert ring second preset"));
    QueueAction(1, 6, 1, TEXT("owner one's independent Alert ring starts at one"));
    QueueAction(1, 7, 0, TEXT("AllTypes three true callbacks with one message sequence increment"));
    QueueAction(0, 10, 0, TEXT("all five real Reset callbacks rearm one-shot and dynamic"));
    QueueAction(0, 6, 9, TEXT("Reset retains owner zero's Alert ring"));
    QueueAction(1, 6, 5, TEXT("Reset retains owner one's independent Alert ring"));
    QueueAction(0, 9, 0, TEXT("dynamic real unbind"));
    QueueAction(1, 4, 0, TEXT("unbound dynamic ping ignored"));
    QueueAction(0, 9, 0, TEXT("dynamic real rebind without retained old-frame replay"));
    QueueAction(1, 4, 0, TEXT("rebound dynamic fires once"));
    QueueAction(0, 10, 0, TEXT("reset before concurrent production FIFO regression"));

    const int32 PairA = F->NextSerial[0]++, PairB = F->NextSerial[1]++, PairBusy = F->NextSerial[0]++;
    AddCommand(new FObservePair(this, F, PairA, PairB, 3, 10, PairBusy));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated, PairA, PairB]() -> bool
        { return OwnerSettled(0, PairA, *F) && OwnerSettled(1, PairB, *F) && Converged(*F, Dedicated); }),
        20, TEXT("concurrent Ping/Reset matches actual admission order and exact binding state")));
    QueueAction(0, 10, 0, TEXT("real Reset leaves count zero and one binding after concurrent case"));
    QueueAction(0, 3, 0, TEXT("next-frame rearmed one-shot fires once"));
    QueueAction(1, 3, 0, TEXT("next one-shot broadcast ignored until reset"));
    QueueAction(0, 9, 0, TEXT("unbind dynamic before queued Ping/rebind regression"));
    const int32 DynamicA = F->NextSerial[0]++, DynamicB = F->NextSerial[1]++;
    AddCommand(new FObservePair(this, F, DynamicA, DynamicB, 4, 9));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated, DynamicA, DynamicB]() -> bool
        { return OwnerSettled(0, DynamicA, *F) && OwnerSettled(1, DynamicB, *F) && Converged(*F, Dedicated); }),
        20, TEXT("concurrent unbound Ping/rebind matches actual admission order without same-frame replay")));
    QueueAction(0, 4, 0, TEXT("fresh rebound dynamic broadcast fires exactly once"));

    // Malformed/stale controls traverse the real owner RPC and compare the entire
    // production snapshot and accepted watermarks after network drainage.
    const int32 BadSerial = F->NextSerial[1]++, BadPrioritySerial = F->NextSerial[1]++;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            if (!TestTrue(TEXT("complete live snapshot before rejected RPCs"), Capture(*F, W)))
            { ck::auto_test::net::FailSession(TEXT("Messaging invalid-control capture unavailable")); return; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F, BadSerial, BadPrioritySerial](UWorld* W)
        {
            auto* P = PC(W, *F);
            if (!TestTrue(TEXT("live owner for invalid control probes"), IsValid(P)))
            { ck::auto_test::net::FailSession(TEXT("Messaging invalid probe owner unavailable")); return; }
            TestTrue(TEXT("unsupported row owning RPC"), Invoke(P, *F, BadSerial, 99));
            TestTrue(TEXT("unsupported Alert priority owning RPC"), Invoke(P, *F, BadPrioritySerial, 6, 42));
            TestTrue(TEXT("duplicate accepted serial owning RPC"), Invoke(P, *F, F->Accepted[1], 10));
            TestTrue(TEXT("default serial owning RPC"), Invoke(P, *F, 0, 1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, BadPrioritySerial]() -> bool
        {
            auto* P = PC(ck::auto_test::net::Get_ClientWorld(1), *F);
            return P && Int(F->PCClass, P, TEXT("LastControlAttemptSerial")) == BadPrioritySerial &&
                Int(F->PCClass, P, TEXT("LastControlResult")) == 4;
        }), 10, TEXT("invalid control returns correlated rejection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(15));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F, Dedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            const auto* D = Data(ck::auto_test::net::Get_ServerWorld(), *F);
            const bool Unchanged = TestTrue(TEXT("invalid/default/duplicate controls cause zero callback/sequence/accepted mutation"),
                D && F->Before && F->Snapshot->Identical(D, F->Before->GetStructMemory(), 0) &&
                OwnerSettled(0, F->Accepted[0], *F) && OwnerSettled(1, F->Accepted[1], *F) && Converged(*F, Dedicated));
            if (!Unchanged) { ck::auto_test::net::FailSession(TEXT("Messaging rejected control mutated production state")); }
            return Unchanged;
        }), TEXT("whole-snapshot invariant after rejected RPCs")));
    QueueAction(0, 1, 0, TEXT("next valid action resamples all original callbacks after rejected controls"));

    // Remove the actual authored delegate from a production child, then submit
    // Reset through its owner RPC. No fake stage, replacement fragment or ensure.
    const int32 TerminalSerial = F->NextSerial[0]++;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* W)
        {
            auto Child = F->ServerChildren[0];
            if (!TestTrue(TEXT("live Basic before malformed-composition probe"), Live(Child)))
            { ck::auto_test::net::FailSession(TEXT("Messaging Basic child unavailable")); return; }
            auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Child);
            if (!TestTrue(TEXT("original Basic script before malformed-composition probe"),
                IsValid(Script) && Script->GetClass() == F->Scripts[0]))
            { ck::auto_test::net::FailSession(TEXT("Messaging Basic script unavailable")); return; }
            if (!TestTrue(TEXT("complete live snapshot before production unbind"), Capture(*F, W)))
            { ck::auto_test::net::FailSession(TEXT("Messaging terminal capture unavailable")); return; }
            if (!TestTrue(TEXT("original Basic delegate is present before production unbind"),
                Binding(Child, Script, F->Messages[0], TEXT("OnPing"), ECk_Signal_PostFireBehavior::DoNothing, 1)))
            { ck::auto_test::net::FailSession(TEXT("Messaging original Basic callback unavailable")); return; }
            const auto Callback = Delegate(Script, TEXT("OnPing"));
            if (!TestTrue(TEXT("valid Basic unbind delegate"), Callback.IsBound()))
            { ck::auto_test::net::FailSession(TEXT("Messaging Basic unbind delegate unavailable")); return; }
            UCk_Utils_Messaging_UE::UnbindFrom_OnBroadcast(Child, F->Messages[0], Callback);
            if (!TestEqual(TEXT("real unbind removes exact unconditional callback"),
                UCk_Utils_Messaging_UE::Get_NumBoundTo_OnBroadcast(Child, F->Messages[0], ECk_Signal_PostFireBehavior::DoNothing), 0))
            { ck::auto_test::net::FailSession(TEXT("Messaging real callback unbind failed")); return; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F, TerminalSerial](UWorld* W)
        {
            if (!TestTrue(TEXT("Reset after lost real callback traverses owner RPC"), Invoke(PC(W, *F), *F, TerminalSerial, 10)))
            { ck::auto_test::net::FailSession(TEXT("Messaging terminal owner unavailable")); return; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
        {
            const auto* T = F->Snapshot->Struct.Get();
            for (auto* W : ck::auto_test::net::Get_AllPIEWorlds())
            { const auto* D = Data(W, *F); if (!D || !Bool(T, D, TEXT("Failed")) || Bool(T, D, TEXT("Ready"))) { return false; } }
            return true;
        }), 15, TEXT("malformed real listener composition terminal on every world")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* W = ck::auto_test::net::Get_ServerWorld(); const auto* D = Data(W, *F);
            if (!TestTrue(TEXT("terminal server state and captured snapshot remain available"),
                State(W, *F) && D && F->Before.IsValid()))
            { ck::auto_test::net::FailSession(TEXT("Messaging terminal state unavailable")); return false; }
            const auto* Before = F->Before->GetStructMemory(); const auto* T = F->Snapshot->Struct.Get();
            bool Unchanged = Int(T, D, TEXT("Sequence")) == Int(T, Before, TEXT("Sequence")) &&
                Int(T, D, TEXT("MessageSequence")) == Int(T, Before, TEXT("MessageSequence"));
            for (int32 I = 0; I < 5; ++I)
            {
                const auto* CurrentSample = Sample(D, *F, I); const auto* PreviousSample = Sample(Before, *F, I);
                if (!TestTrue(TEXT("terminal and captured complete station samples"), CurrentSample && PreviousSample))
                { ck::auto_test::net::FailSession(TEXT("Messaging terminal sample unavailable")); return false; }
                Unchanged &= F->Sample->Identical(CurrentSample, PreviousSample, 0);
            }
            for (int32 I = 0; I < 2; ++I)
            { auto* P = ServerPC(I, *F); Unchanged &= P && Int(F->PCClass, P, TEXT("LastControlSerial")) == F->Accepted[I]; }
            return TestTrue(TEXT("lost callback rejects atomic Reset with no downstream sample or accepted mutation"), Unchanged);
        }), TEXT("terminal admission never publishes partial reset")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        { F->RestoreSettings(); return TestTrue(TEXT("Messaging PIE worlds drained"),
            ck::auto_test::net::Get_AllPIEWorlds().IsEmpty()); }), TEXT("always restore startup settings after EndPIE"), true));
    return true;
}

#endif
