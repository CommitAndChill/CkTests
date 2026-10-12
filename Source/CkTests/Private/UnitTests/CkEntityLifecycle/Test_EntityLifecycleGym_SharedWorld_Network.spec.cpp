#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_NetworkRuntime.h"
#include "CkGym_StartupSettings.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkDynamic/CkDynamic_Utils.h"
#include "CkEcs/DeferredEntity/CkDeferredEntity_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkTimer/CkTimer_Utils.h"

#include <Engine/World.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/HUD.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <HAL/PlatformTime.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>

namespace ck_lifecycle_shared_test
{
    constexpr const TCHAR* Names[7] = {TEXT("HandleAndEntity"), TEXT("OwnershipTree"), TEXT("DestroyCallbacks"),
        TEXT("ActorBridge"), TEXT("TagSystem"), TEXT("DeferredSetup"), TEXT("ScriptSpawnCast")};
    template<typename T> auto Prop(const UStruct* S, const TCHAR* N) -> T*
    { return IsValid(S) ? FindFProperty<T>(S, N) : nullptr; }
    auto Int(const UStruct* S, const void* V, const TCHAR* N) -> int32
    { const auto* P = Prop<FIntProperty>(S, N); return P && V ? P->GetPropertyValue_InContainer(V) : INDEX_NONE; }
    auto Bool(const UStruct* S, const void* V, const TCHAR* N) -> bool
    { const auto* P = Prop<FBoolProperty>(S, N); return P && V && P->GetPropertyValue_InContainer(V); }
    auto String(const UStruct* S, const void* V, const TCHAR* N) -> FString
    { const auto* P = Prop<FStrProperty>(S, N); return P && V ? P->GetPropertyValue_InContainer(V) : FString{}; }
    auto Handle(const UStruct* S, const void* V, const TCHAR* N) -> FCk_Handle
    { const auto* P = Prop<FStructProperty>(S, N); return P && V && P->Struct->IsChildOf(FCk_Handle::StaticStruct()) ?
        *P->ContainerPtrToValuePtr<FCk_Handle>(V) : FCk_Handle{}; }
    auto Live(const FCk_Handle& H) -> bool
    { return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    auto Allocated(const FCk_Handle& H) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
    auto Direct(const FCk_Handle& H, const FCk_Handle& Parent) -> bool
    { return Live(H) && Live(Parent) && UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H) &&
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H) == Parent; }
    auto Fail(FAutomationTestBase* Test, const FString& Reason) -> void
    { Test->AddError(Reason); ck::auto_test::net::FailSession(Reason); }
    auto Tagged(UWorld* W, const FString& Tag) -> TArray<FCk_Handle>
    {
        const auto Root = IsValid(W) ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W) : FCk_Handle{};
        return Live(Root) ? UCk_Utils_EntityTag_UE::ForEach_Entity(Root, FName(*Tag)) : TArray<FCk_Handle>{};
    }
    auto ReadDynamic(const FCk_Handle& H, UScriptStruct* Type) -> const void*
    {
        if (!Live(H) || !IsValid(Type) || !UCk_Utils_DynamicFragment_UE::Has_Fragment(H, Type)) { return nullptr; }
        const auto* V = UCk_Utils_DynamicFragment_UE::TryGet_Fragment_TypeUnsafe(H, Type);
        return V && V->GetScriptStruct() == Type ? V->GetMemory() : nullptr;
    }

    struct FFixture
    {
        UClass *Mode = nullptr, *StateClass = nullptr, *PCClass = nullptr, *ProbeClass = nullptr, *RootClass = nullptr;
        UClass *Scripts[7]{}, *TargetClass = nullptr, *StationClass = nullptr;
        FStructProperty *Snapshot = nullptr, *Commit = nullptr, *CommitSnapshot = nullptr;
        FArrayProperty* Rows = nullptr;
        FStructProperty* Row = nullptr;
        UScriptStruct *Display = nullptr, *Anchors = nullptr;
        UFunction *Control = nullptr, *ACK = nullptr;
        int32 NextSerial[2]{100, 200};
        ECkGym_StartupMode OldStartup = ECkGym_StartupMode::Cycler;
        FString OldLast; TArray<FString> OldRecents;
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
    { auto* S = State(W, F); return S && F.Snapshot ? F.Snapshot->ContainerPtrToValuePtr<void>(S) : nullptr; }
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
    auto Row(const void* D, const FFixture& F, int32 I) -> const void*
    {
        if (!D || !F.Rows || I < 0 || I >= 7) { return nullptr; }
        FScriptArrayHelper A(F.Rows, F.Rows->ContainerPtrToValuePtr<void>(D));
        return A.Num() == 7 ? A.GetRawPtr(I) : nullptr;
    }
    auto Complete(const void* D, const FFixture& F) -> bool
    {
        if (!D || !Bool(F.Snapshot->Struct, D, TEXT("Ready")) || Bool(F.Snapshot->Struct, D, TEXT("Failed")) ||
            Int(F.Snapshot->Struct, D, TEXT("Generation")) <= 0 || Int(F.Snapshot->Struct, D, TEXT("Sequence")) <= 0) { return false; }
        for (int32 I = 0; I < 7; ++I)
        {
            const auto* R = Row(D, F, I);
            if (!R || Int(F.Row->Struct, R, TEXT("BranchGeneration")) <= 0 || String(F.Row->Struct, R, TEXT("Title")).IsEmpty() ||
                String(F.Row->Struct, R, TEXT("Body")).IsEmpty() || String(F.Row->Struct, R, TEXT("Instructions")).IsEmpty()) { return false; }
        }
        return true;
    }
    auto Original(UWorld* W, const FFixture& F, int32 I) -> FCk_Handle
    {
        if (I < 0 || I >= 7) { return {}; }
        const auto A = Tagged(W, FString(TEXT("TAG_LifecycleGym_")) + Names[I]);
        if (A.Num() != 1 || !Live(A[0]) || !UCk_Utils_EntityScript_UE::Has(A[0])) { return {}; }
        auto* S = UCk_Utils_EntityScript_UE::TryGet_EntityScript(A[0]);
        return IsValid(S) && S->GetClass() == F.Scripts[I] ? A[0] : FCk_Handle{};
    }
    auto ProbeRoot(UWorld* W, const FFixture& F) -> FCk_Handle
    {
        auto* S = State(W, F); const auto* D = Data(W, F); const auto* R = Row(D, F, 3);
        const auto* P = Prop<FObjectPropertyBase>(F.StateClass, TEXT("BridgeProbe"));
        auto* A = P && S ? Cast<AActor>(P->GetObjectPropertyValue_InContainer(S)) : nullptr;
        if (!IsValid(A) || A->GetWorld() != W || A->GetClass() != F.ProbeClass || !A->GetIsReplicated() || !R ||
            Int(F.ProbeClass, A, TEXT("BranchGeneration")) != Int(F.Row->Struct, R, TEXT("BranchGeneration"))) { return {}; }
        const auto H = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(A);
        if (!Live(H) || !UCk_Utils_OwningActor_UE::Has(H) || UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(H) != A ||
            !UCk_Utils_EntityScript_UE::Has(H) || !UCk_Utils_Net_UE::Has(H) ||
            UCk_Utils_Net_UE::Get_EntityReplication(H) != ECk_Replication::Replicates || !UCk_Utils_EntityReplicationDriver_UE::Has(H)) { return {}; }
        auto* Driver = H.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
        auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
        return IsValid(Driver) && Driver->GetWorld() == W && Driver->Get_AssociatedEntity() == H &&
            IsValid(Script) && Script->GetClass() == F.RootClass ? H : FCk_Handle{};
    }
    auto Census(UWorld* W, const FFixture& F, bool Authority) -> bool
    {
        auto* S = State(W, F); const auto* D = Data(W, F);
        if (!IsValid(W) || !S || !Complete(D, F) || !Live(ProbeRoot(W, F))) { return false; }
        if (Authority && Int(F.StateClass, S, TEXT("ServerCohortCount")) != 7) { return false; }
        FCk_Handle StationOwner;
        for (int32 I = 0; I < 7; ++I)
        {
            const auto A = Tagged(W, FString(TEXT("Gym.EntityLifecycle.")) + Names[I]);
            const auto* Anchor = A.Num() == 1 ? ReadDynamic(A[0], F.Anchors) : nullptr;
            const auto* FP = Prop<FStructProperty>(F.Anchors, TEXT("FootprintCenter"));
            auto* StationScript = Anchor && UCk_Utils_EntityScript_UE::Has(A[0]) ? UCk_Utils_EntityScript_UE::TryGet_EntityScript(A[0]) : nullptr;
            if (!Anchor || !FP || !IsValid(StationScript) || StationScript->GetClass() != F.StationClass ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(A[0]) || UCk_Utils_OwningActor_UE::Has(A[0]) ||
                *FP->ContainerPtrToValuePtr<FVector>(Anchor) != FVector(500, -2400 + I * 800, 0)) { return false; }
            const auto Owner = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(A[0]);
            if (I == 0) { StationOwner = Owner; }
            if (!Direct(A[0], StationOwner)) { return false; }
            const auto Matches = Tagged(W, FString(TEXT("TAG_LifecycleGym_")) + Names[I]);
            if (!Authority) { if (!Matches.IsEmpty()) { return false; } continue; }
            const auto H = Original(W, F, I);
            auto* Script = Live(H) ? UCk_Utils_EntityScript_UE::TryGet_EntityScript(H) : nullptr;
            if (!IsValid(Script) || !Direct(H, I == 3 ? ProbeRoot(W, F) : A[0]) ||
                !UCk_Utils_Net_UE::Has(H) || UCk_Utils_Net_UE::Get_EntityReplication(H) != ECk_Replication::DoesNotReplicate ||
                !Bool(F.Scripts[I], Script, TEXT("SharedAuthority")) || Bool(F.Scripts[I], Script, TEXT("Closed")) ||
                Bool(F.Scripts[I], Script, TEXT("Failed"))) { return false; }
            int32 Timers = 0;
            for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(H))
            { if (Live(Child) && UCk_Utils_Timer_UE::Has(Child)) { ++Timers; } }
            if (Timers != (I == 2 || I == 4 || I == 5 ? 1 : 0)) { return false; }
        }
        return Authority || Tagged(W, TEXT("TAG_LifecycleGym_SpawnTarget")).IsEmpty();
    }
    auto Panels(UWorld* W, const FFixture& F) -> bool
    {
        auto* P = PC(W, F); const auto* D = Data(W, F);
        if (!P || !P->IsLocalController() || !IsValid(P->GetHUD()) || P->GetHUD()->GetOwner() != P || !Complete(D, F) ||
            !Bool(F.PCClass, P, TEXT("LocalPresentationReady")) || Bool(F.PCClass, P, TEXT("LocalPresentationFailed")) ||
            Int(F.PCClass, P, TEXT("LocalPresentationSequence")) != Int(F.Snapshot->Struct, D, TEXT("Sequence"))) { return false; }
        const TCHAR* Field[] = {TEXT("Title"), TEXT("Description"), TEXT("Instructions")};
        const TCHAR* SampleField[] = {TEXT("Title"), TEXT("Body"), TEXT("Instructions")};
        for (int32 I = 0; I < 7; ++I)
        {
            const auto A = Tagged(W, FString(TEXT("Gym.EntityLifecycle.")) + Names[I]);
            const auto* Panel = A.Num() == 1 ? ReadDynamic(A[0], F.Display) : nullptr; const auto* R = Row(D, F, I);
            if (!Panel || !R) { return false; }
            for (int32 J = 0; J < 3; ++J)
            {
                const auto* Text = Prop<FTextProperty>(F.Display, Field[J]);
                if (!Text || Text->GetPropertyValue_InContainer(Panel).ToString() != String(F.Row->Struct, R, SampleField[J])) { return false; }
            }
        }
        return true;
    }
    auto ReadyAll(const FFixture& F, bool Dedicated) -> bool
    {
        auto* W = ck::auto_test::net::Get_ServerWorld();
        if (!Census(W, F, true) || (!Dedicated && !Panels(W, F))) { return false; }
        for (int32 I = 0; I < 2; ++I)
        { auto* C = ck::auto_test::net::Get_ClientWorld(I); if (!Census(C, F, false) || !Panels(C, F)) { return false; } }
        return true;
    }
    auto PreflightObservation(const FFixture& F) -> FString
    {
        FString Result;
        for (int32 I = -1; I < 2; ++I)
        {
            auto* W = I < 0 ? ck::auto_test::net::Get_ServerWorld() : ck::auto_test::net::Get_ClientWorld(I);
            const auto* D = Data(W, F); auto* P = PC(W, F);
            const UScriptStruct* Type = F.Snapshot ? F.Snapshot->Struct.Get() : nullptr;
            Result += FString::Printf(TEXT(" [world=%d live=%d census=%d panels=%d snapshotReady=%d snapshotFailed=%d reason='%s' generation=%d sequence=%d pcReady=%d pcFailed=%d pcReason='%s' pcSequence=%d]"),
                I, IsValid(W), Census(W, F, I < 0), Panels(W, F),
                Bool(Type, D, TEXT("Ready")), Bool(Type, D, TEXT("Failed")), *String(Type, D, TEXT("FailureReason")),
                Int(Type, D, TEXT("Generation")), Int(Type, D, TEXT("Sequence")),
                Bool(F.PCClass, P, TEXT("LocalPresentationReady")), Bool(F.PCClass, P, TEXT("LocalPresentationFailed")),
                *String(F.PCClass, P, TEXT("LocalPresentationFailureReason")), Int(F.PCClass, P, TEXT("LocalPresentationSequence")));
        }
        return Result;
    }
    auto Invoke(UObject* Target, UFunction* Method, const TArray<TPair<FName, int32>>& Values) -> bool
    {
        if (!IsValid(Target) || !IsValid(Method)) { return false; }
        FStructOnScope Params(Method); auto* Memory = Params.GetStructMemory();
        for (const auto& V : Values)
        { auto* P = Prop<FIntProperty>(Method, *V.Key.ToString()); if (!P) { return false; } P->SetPropertyValue_InContainer(Memory, V.Value); }
        Target->ProcessEvent(Method, Memory); return true;
    }
    auto Commit(APlayerController* P, const FFixture& F) -> const void*
    { return IsValid(P) ? F.Commit->ContainerPtrToValuePtr<void>(P) : nullptr; }
    auto Settled(int32 Owner, int32 Serial, const FFixture& F) -> bool
    {
        auto* P = PC(ck::auto_test::net::Get_ClientWorld(Owner), F); auto* SP = ServerPC(Owner, F);
        const auto* C = Commit(P, F); const auto* SC = Commit(SP, F);
        return C && SC && Int(F.PCClass, P, TEXT("LastControlSerial")) == Serial &&
            Int(F.PCClass, SP, TEXT("LastControlSerial")) == Serial && Int(F.Commit->Struct, SC, TEXT("RequestSerial")) == Serial &&
            Complete(F.CommitSnapshot->ContainerPtrToValuePtr<void>(SC), F) && F.Commit->Identical(C, SC, 0) &&
            Int(F.PCClass, SP, TEXT("LastAcknowledgedDeliverySerial")) == Int(F.Commit->Struct, SC, TEXT("DeliverySerial"));
    }
    auto Collect(const FCk_Handle& H, TArray<FCk_Handle>& Out) -> void
    {
        if (!Live(H) || Out.Contains(H)) { return; }
        Out.Add(H); for (const auto& C : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(H)) { Collect(C, Out); }
    }

    auto Script(const FCk_Handle& H) -> UObject*
    { return Live(H) && UCk_Utils_EntityScript_UE::Has(H) ? UCk_Utils_EntityScript_UE::TryGet_EntityScript(H) : nullptr; }
    auto Transform(UObject* S, FTransform& Out) -> bool
    {
        const auto* P = IsValid(S) ? Prop<FStructProperty>(S->GetClass(), TEXT("InitialTransform")) : nullptr;
        if (!P || P->Struct != TBaseStructure<FTransform>::Get()) { return false; }
        Out = *P->ContainerPtrToValuePtr<FTransform>(S); return true;
    }
    auto CaptureExtra(UObject* S, TArray<FCk_Handle>& Out) -> bool
    {
        const auto* P = IsValid(S) ? Prop<FArrayProperty>(S->GetClass(), TEXT("ExtraRetirement")) : nullptr;
        const auto* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
        if (!Inner || Inner->Struct != FCk_Handle::StaticStruct()) { return false; }
        FScriptArrayHelper A(P, P->ContainerPtrToValuePtr<void>(S));
        for (int32 I = 0; I < A.Num(); ++I)
        { const auto H = *reinterpret_cast<const FCk_Handle*>(A.GetRawPtr(I)); if (Allocated(H)) { Out.AddUnique(H); } }
        return true;
    }
    // The observers read the real server both before and after actor/TimerManager processing.
    // They never call lifecycle callbacks, construct replacement entities, or mutate script fields.
    class FObserveCycles final : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test; TSharedRef<FFixture> F;
        FDelegateHandle Pre, Post; double Start = 0; bool Started = false;
        bool TagAdded = false, TagRemoved = false, DeferredPending = false, DeferredComplete = false;
        bool DeferredCycle = false, DestroyCycle = false, Spawned = false;
        FCk_Handle Initial[7]; FTransform Recipes[7];
        void Stop() { FWorldDelegates::OnWorldPreActorTick.Remove(Pre); FWorldDelegates::OnWorldPostActorTick.Remove(Post); }
        void Observe(UWorld* W)
        {
            if (W != ck::auto_test::net::Get_ServerWorld() || ck::auto_test::net::Get_HasSessionFailed()) { return; }
            for (int32 I = 0; I < 7; ++I)
            {
                const auto H = Original(W, *F, I); auto* S = Script(H); FTransform T;
                if (H != Initial[I] || !IsValid(S) || !Transform(S, T) || !T.Equals(Recipes[I], 0))
                { Fail(Test, TEXT("real original identity/construction recipe changed during phase observation")); return; }
                if (I == 2 || I == 4 || I == 5)
                {
                    const auto Timer = Handle(F->Scripts[I], S, TEXT("PhaseTimer"));
                    if (!Live(Timer) || !UCk_Utils_Timer_UE::Has(Timer))
                    { Fail(Test, TEXT("authored phase timer lost during cycle observation")); return; }
                    const auto Typed = UCk_Utils_Timer_UE::CastChecked(Timer);
                    const auto Goal = UCk_Utils_Timer_UE::Get_CurrentTimerValue(Typed).Get_GoalValue().Get_Milliseconds();
                    if (Goal != (I == 2 ? 1500.0 : I == 4 ? 1000.0 : 2000.0) ||
                        UCk_Utils_Timer_UE::Get_Behavior(Typed) != ECk_Timer_Behavior::ResetOnDone)
                    { Fail(Test, TEXT("authored phase duration/reset behavior changed")); return; }
                }
                if (I == 4)
                {
                    const auto C = Handle(F->Scripts[I], S, TEXT("ChildEntity"));
                    if (!Direct(C, H)) { Fail(Test, TEXT("tag demo actual direct child lost")); return; }
                    const bool Has = UCk_Utils_EntityTag_UE::Has(C, FName(TEXT("TAG_LifecycleGym_TestAlpha")));
                    const auto Phase = Int(F->Scripts[I], S, TEXT("CurrentPhase"));
                    TagAdded |= Phase == 1 && Has && Bool(F->Scripts[I], S, TEXT("Pass_AddTag")) &&
                        Bool(F->Scripts[I], S, TEXT("Pass_TryGetTag")) && Int(F->Scripts[I], S, TEXT("ForEachCount_BeforeRemove")) == 1;
                    TagRemoved |= Phase == 3 && !Has && Bool(F->Scripts[I], S, TEXT("Pass_ForEachEmpty")) &&
                        Int(F->Scripts[I], S, TEXT("ForEachCount_AfterRemove")) == 0;
                }
                if (I == 5)
                {
                    const auto C = Handle(F->Scripts[I], S, TEXT("DeferredHandle"));
                    const auto Phase = Int(F->Scripts[I], S, TEXT("CurrentPhase"));
                    if (Phase == 0 && Direct(C, H) && UCk_Utils_DeferredEntity_UE::Has(C))
                    {
                        const auto Typed = UCk_Utils_DeferredEntity_UE::CastChecked(C);
                        DeferredPending |= UCk_Utils_DeferredEntity_UE::Get_PendingCount(Typed) == 1 &&
                            Int(F->Scripts[I], S, TEXT("PendingCount")) == 1 && Bool(F->Scripts[I], S, TEXT("Pass_PendingCount"));
                    }
                    DeferredComplete |= Phase == 1 && Direct(C, H) && !UCk_Utils_DeferredEntity_UE::Has(C) &&
                        Bool(F->Scripts[I], S, TEXT("Pass_CompleteSetup")) && Bool(F->Scripts[I], S, TEXT("SetupCompleteCallbackFired")) &&
                        Bool(F->Scripts[I], S, TEXT("FullyCompleteCallbackFired")) &&
                        Int(F->Scripts[I], S, TEXT("ActualCallbackCount")) - Int(F->Scripts[I], S, TEXT("CycleStartCallbacks")) == 2;
                    DeferredCycle |= Phase == 2 && Int(F->Scripts[I], S, TEXT("LastCompletedCycle")) >= 0 &&
                        Int(F->Scripts[I], S, TEXT("LastCycleCallbackCount")) == 2 && Bool(F->Scripts[I], S, TEXT("Pass_SetupCompleteCallback")) &&
                        Bool(F->Scripts[I], S, TEXT("Pass_FullyCompleteCallback"));
                }
                if (I == 2)
                {
                    DestroyCycle |= Int(F->Scripts[I], S, TEXT("CurrentPhase")) == 3 &&
                        Int(F->Scripts[I], S, TEXT("LastCompletedCycle")) >= 0 && Int(F->Scripts[I], S, TEXT("LastCycleCallbackCount")) == 1 &&
                        Int(F->Scripts[I], S, TEXT("DestroyCallbackCount")) == Int(F->Scripts[I], S, TEXT("CycleCount")) + 1 &&
                        !Allocated(Handle(F->Scripts[I], S, TEXT("ChildA"))) && !Allocated(Handle(F->Scripts[I], S, TEXT("ChildB"))) &&
                        !Allocated(Handle(F->Scripts[I], S, TEXT("ChildC")));
                }
                if (I == 6)
                {
                    const auto C = Handle(F->Scripts[I], S, TEXT("SpawnTarget")); auto* Target = Script(C);
                    Spawned |= Direct(C, H) && IsValid(Target) && Target->GetClass() == F->TargetClass &&
                        Bool(F->Scripts[I], S, TEXT("TestsComplete")) && Bool(F->Scripts[I], S, TEXT("Pass_DoCastChecked")) &&
                        Int(F->Scripts[I], S, TEXT("ActualCallbackCount")) == 1;
                }
            }
        }
    public:
        FObserveCycles(FAutomationTestBase* T, TSharedRef<FFixture> InF) : Test(T), F(InF) {}
        ~FObserveCycles() override { Stop(); }
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { Stop(); return true; }
            auto* W = ck::auto_test::net::Get_ServerWorld();
            if (!Started)
            {
                for (int32 I = 0; I < 7; ++I)
                {
                    Initial[I] = Original(W, *F, I);
                    if (!Live(Initial[I]) || !Transform(Script(Initial[I]), Recipes[I]))
                    { Fail(Test, TEXT("cycle observer requires seven real originals and construction recipes")); return true; }
                }
                Started = true; Start = FPlatformTime::Seconds();
                Pre = FWorldDelegates::OnWorldPreActorTick.AddLambda([this](UWorld* V, ELevelTick, float) { Observe(V); });
                Post = FWorldDelegates::OnWorldPostActorTick.AddLambda([this](UWorld* V, ELevelTick, float) { Observe(V); });
            }
            if (TagAdded && TagRemoved && DeferredPending && DeferredComplete && DeferredCycle && DestroyCycle && Spawned)
            { Test->AddInfo(TEXT("Observed real settled tag add/remove, deferred count1/two callbacks, destroy callback cycle, and constructed SpawnTarget")); Stop(); return true; }
            if (FPlatformTime::Seconds() - Start > 20)
            { Fail(Test, FString::Printf(TEXT("cycle evidence timeout tag=%d/%d deferred=%d/%d/%d destroy=%d spawn=%d"),
                TagAdded, TagRemoved, DeferredPending, DeferredComplete, DeferredCycle, DestroyCycle, Spawned)); Stop(); return true; }
            return false;
        }
    };

    class FObserveRerun final : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test; TSharedRef<FFixture> F; int32 Owner, RequestedRow, Serial;
        bool Dedicated, Terminal, Started = false, SawPending = false, Accepted = false, Repossessed = false, StaleAckSent = false, BarrierSent = false;
        double Start = -1; FDelegateHandle Pre, Post;
        FCk_Handle Before[7]; int32 Generations[7]{}; TArray<FCk_Handle> Old;
        TSharedPtr<FStructOnScope> Retained; TWeakObjectPtr<APawn> OriginalPawn;
        int32 BeforeAck = 0, Delivery = 0, BarrierSerial = 0, ExpectedGeneration = 0, ExpectedSequence = 0;
        void Stop() { FWorldDelegates::OnWorldPreActorTick.Remove(Pre); FWorldDelegates::OnWorldPostActorTick.Remove(Post); }
        bool Selected(int32 I) const { return RequestedRow == 8 || RequestedRow == I + 1; }
        void Observe(UWorld* W)
        {
            if (W != ck::auto_test::net::Get_ServerWorld() || ck::auto_test::net::Get_HasSessionFailed() || Accepted) { return; }
            for (const auto& H : Old) { SawPending |= !Live(H) && Allocated(H); }
            auto* SP = ServerPC(Owner, *F);
            if (!SP) { Fail(Test, TEXT("rerun owning server PC disappeared")); return; }
            if (Int(F->PCClass, SP, TEXT("LastControlSerial")) != Serial) { return; }
            if (Int(F->PCClass, SP, TEXT("LastControlAttemptSerial")) != Serial + 1 || Int(F->PCClass, SP, TEXT("LastControlResult")) != 3)
            { Fail(Test, TEXT("newer same-owner busy attempt did not preserve original queued acceptance")); return; }
            if (!SawPending) { Fail(Test, TEXT("rerun accepted without observed actual allocated pending-kill interval")); return; }
            for (const auto& H : Old)
            { if (Allocated(H)) { Fail(Test, TEXT("rerun accepted while captured old entity is still allocated")); return; } }
            const auto* D = Data(W, *F); const auto* C = Commit(SP, *F);
            if (!Complete(D, *F) || !C || !Complete(F->CommitSnapshot->ContainerPtrToValuePtr<void>(C), *F) ||
                Int(F->Commit->Struct, C, TEXT("Row")) != RequestedRow || Int(F->Commit->Struct, C, TEXT("RequestSerial")) != Serial)
            { Fail(Test, TEXT("rerun accepted without complete correlated retained package")); return; }
            for (int32 I = 0; I < 7; ++I)
            {
                const auto H = Original(W, *F, I); const auto* R = Row(D, *F, I);
                if (!Live(H) || !R || (Selected(I) ? H == Before[I] : H != Before[I]) ||
                    Int(F->Row->Struct, R, TEXT("BranchGeneration")) != Generations[I] + (Selected(I) ? 1 : 0))
                { Fail(Test, TEXT("rerun replaced wrong branch or published wrong branch generation")); return; }
            }
            Accepted = true;
            if (Terminal)
            {
                BeforeAck = Int(F->PCClass, SP, TEXT("LastAcknowledgedDeliverySerial"));
                Delivery = Int(F->Commit->Struct, C, TEXT("DeliverySerial")); OriginalPawn = SP->GetPawn();
                const auto* CommittedSnapshot = F->CommitSnapshot->ContainerPtrToValuePtr<void>(C);
                ExpectedGeneration = Int(F->Snapshot->Struct, CommittedSnapshot, TEXT("Generation"));
                ExpectedSequence = Int(F->Snapshot->Struct, CommittedSnapshot, TEXT("Sequence"));
                if (BeforeAck >= Delivery || !OriginalPawn.IsValid())
                { Fail(Test, TEXT("terminal ACK regression did not capture real unacknowledged commit and pawn")); return; }
                Retained = MakeShared<FStructOnScope>(F->Commit->Struct);
                F->Commit->Struct->CopyScriptStruct(Retained->GetStructMemory(), C);
                SP->UnPossess();
            }
        }
    public:
        FObserveRerun(FAutomationTestBase* T, TSharedRef<FFixture> InF, int32 O, int32 R, int32 S, bool D, bool Term = false)
            : Test(T), F(InF), Owner(O), RequestedRow(R), Serial(S), Dedicated(D), Terminal(Term) {}
        ~FObserveRerun() override { Stop(); }
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { Stop(); return true; }
            if (Start < 0) { Start = FPlatformTime::Seconds(); }
            auto* W = ck::auto_test::net::Get_ServerWorld(); auto* SP = ServerPC(Owner, *F);
            if (!Started)
            {
                if (!SP || !ReadyAll(*F, Dedicated))
                {
                    if (FPlatformTime::Seconds() - Start > 20)
                    { Fail(Test, FString(TEXT("rerun preflight requires actual three-world complete cohort")) + PreflightObservation(*F)); return true; }
                    return false;
                }
                const auto* D = Data(W, *F);
                for (int32 I = 0; I < 7; ++I)
                {
                    Before[I] = Original(W, *F, I); const auto* R = Row(D, *F, I);
                    if (!Live(Before[I]) || !R) { Fail(Test, TEXT("rerun initial originals unavailable")); return true; }
                    Generations[I] = Int(F->Row->Struct, R, TEXT("BranchGeneration"));
                    if (Selected(I))
                    {
                        Collect(Before[I], Old);
                        if (!CaptureExtra(Script(Before[I]), Old)) { Fail(Test, TEXT("rerun extra retirement schema unavailable")); return true; }
                        if (I == 3) { Collect(ProbeRoot(W, *F), Old); }
                    }
                }
                Start = FPlatformTime::Seconds(); Started = true;
                Pre = FWorldDelegates::OnWorldPreActorTick.AddLambda([this](UWorld* V, ELevelTick, float) { Observe(V); });
                Post = FWorldDelegates::OnWorldPostActorTick.AddLambda([this](UWorld* V, ELevelTick, float) { Observe(V); });
                if (!Invoke(PC(ck::auto_test::net::Get_ClientWorld(Owner), *F), F->Control,
                    {{TEXT("Serial"), Serial}, {TEXT("Row"), RequestedRow}, {TEXT("Generation"), Int(F->Snapshot->Struct, D, TEXT("Generation"))}}))
                { Fail(Test, TEXT("real owning Lifecycle control RPC unavailable")); Stop(); return true; }
                if (!Invoke(PC(ck::auto_test::net::Get_ClientWorld(Owner), *F), F->Control,
                    {{TEXT("Serial"), Serial + 1}, {TEXT("Row"), RequestedRow}, {TEXT("Generation"), Int(F->Snapshot->Struct, D, TEXT("Generation"))}}))
                { Fail(Test, TEXT("real same-owner busy RPC unavailable")); Stop(); return true; }
            }
            if (Terminal && Accepted)
            {
                if (!SP || !OriginalPawn.IsValid() || !Retained.IsValid())
                { Fail(Test, TEXT("terminal owner/pawn/retained commit lost")); Stop(); return true; }
                if (!Repossessed && Bool(F->PCClass, SP, TEXT("OwnerSessionFailed")))
                { SP->Possess(OriginalPawn.Get()); Repossessed = true; }
                auto* CP = PC(ck::auto_test::net::Get_ClientWorld(Owner), *F);
                if (Repossessed && !StaleAckSent && CP && IsValid(CP->GetPawn()) && Bool(F->PCClass, CP, TEXT("OwnerSessionFailed")))
                {
                    if (!Invoke(CP, F->ACK, {{TEXT("Delivery"), Delivery}, {TEXT("Serial"), Serial}}))
                    { Fail(Test, TEXT("real owning stale ACK RPC unavailable")); Stop(); return true; }
                    StaleAckSent = true; BarrierSerial = Serial + 2;
                }
                if (StaleAckSent && !BarrierSent && IsValid(CP) && IsValid(CP->GetPawn()) && Bool(F->PCClass, CP, TEXT("OwnerSessionFailed")))
                {
                    const auto* D = Data(ck::auto_test::net::Get_ClientWorld(Owner), *F);
                    const auto* ClientCommit = Commit(CP, *F);
                    const auto* ClientCommittedSnapshot = ClientCommit ? F->CommitSnapshot->ContainerPtrToValuePtr<void>(ClientCommit) : nullptr;
                    if (Complete(D, *F) && Complete(ClientCommittedSnapshot, *F) &&
                        F->Commit->Identical(ClientCommit, Retained->GetStructMemory(), 0) &&
                        Int(F->Snapshot->Struct, ClientCommittedSnapshot, TEXT("Generation")) == ExpectedGeneration &&
                        Int(F->Snapshot->Struct, ClientCommittedSnapshot, TEXT("Sequence")) == ExpectedSequence &&
                        Int(F->Snapshot->Struct, D, TEXT("Generation")) == ExpectedGeneration &&
                        Int(F->Snapshot->Struct, D, TEXT("Sequence")) >= ExpectedSequence)
                    {
                        if (!Invoke(CP, F->Control, {{TEXT("Serial"), BarrierSerial}, {TEXT("Row"), 1},
                            {TEXT("Generation"), ExpectedGeneration}}))
                        { Fail(Test, TEXT("terminal reliable control barrier unavailable")); Stop(); return true; }
                        BarrierSent = true;
                    }
                }
                if (BarrierSent && Int(F->PCClass, SP, TEXT("LastControlAttemptSerial")) == BarrierSerial)
                {
                    const bool Kept = Bool(F->PCClass, SP, TEXT("OwnerSessionFailed")) &&
                        Int(F->PCClass, SP, TEXT("LastControlResult")) == 2 && Int(F->PCClass, SP, TEXT("LastControlSerial")) == Serial &&
                        Int(F->PCClass, SP, TEXT("LastAcknowledgedDeliverySerial")) == BeforeAck &&
                        F->Commit->Identical(Commit(SP, *F), Retained->GetStructMemory(), 0);
                    if (!Kept) { Fail(Test, TEXT("terminal re-possession/stale ACK changed retained package or accepted/ACK watermark")); }
                    Stop(); return true;
                }
            }
            if (!Terminal && Accepted && Settled(Owner, Serial, *F) && ReadyAll(*F, Dedicated))
            { Test->AddInfo(FString::Printf(TEXT("Owner%d row%d real retirement/replacement/whole retained commit and consumer ACK settled"), Owner, RequestedRow)); Stop(); return true; }
            if (FPlatformTime::Seconds() - Start > 60)
            {
                if (Terminal && Accepted && !BarrierSent)
                {
                    auto* CP = PC(ck::auto_test::net::Get_ClientWorld(Owner), *F);
                    const auto* D = Data(ck::auto_test::net::Get_ClientWorld(Owner), *F);
                    const auto* ClientCommit = Commit(CP, *F);
                    const auto* ClientCommittedSnapshot = ClientCommit ? F->CommitSnapshot->ContainerPtrToValuePtr<void>(ClientCommit) : nullptr;
                    Fail(Test, FString::Printf(TEXT("terminal committed snapshot/barrier timed out: staleAck=%d repossessed=%d clientPC=%d clientPawn=%d ownerFailed=%d ready=%d failed=%d generation=%d sequence=%d committedGeneration=%d committedSequence=%d commitMatches=%d expectedGeneration=%d expectedSequence=%d serverAttempt=%d"),
                        StaleAckSent, Repossessed, IsValid(CP), IsValid(CP) && IsValid(CP->GetPawn()),
                        Bool(F->PCClass, CP, TEXT("OwnerSessionFailed")), Bool(F->Snapshot->Struct, D, TEXT("Ready")),
                        Bool(F->Snapshot->Struct, D, TEXT("Failed")), Int(F->Snapshot->Struct, D, TEXT("Generation")),
                        Int(F->Snapshot->Struct, D, TEXT("Sequence")),
                        Int(F->Snapshot->Struct, ClientCommittedSnapshot, TEXT("Generation")),
                        Int(F->Snapshot->Struct, ClientCommittedSnapshot, TEXT("Sequence")),
                        ClientCommit && F->Commit->Identical(ClientCommit, Retained->GetStructMemory(), 0),
                        ExpectedGeneration, ExpectedSequence, Int(F->PCClass, SP, TEXT("LastControlAttemptSerial"))));
                }
                else { Fail(Test, TEXT("real Lifecycle rerun/retained ACK observation timed out")); }
                Stop(); return true;
            }
            return false;
        }
    };

    class FObserveRejected final : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test; TSharedRef<FFixture> F;
        int32 Serial, RequestedRow, RequestedGeneration, ExpectedResult, AttemptBefore = 0, AcceptedBefore = 0, AckBefore = 0;
        bool Dedicated, Started = false; double Start = -1;
        FCk_Handle Originals[7]; int32 Generations[7]{}; TSharedPtr<FStructOnScope> Retained;
    public:
        FObserveRejected(FAutomationTestBase* T, TSharedRef<FFixture> InF, int32 S, int32 R, int32 G, int32 Expected, bool D)
            : Test(T), F(InF), Serial(S), RequestedRow(R), RequestedGeneration(G), ExpectedResult(Expected), Dedicated(D) {}
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            if (Start < 0) { Start = FPlatformTime::Seconds(); }
            auto* W = ck::auto_test::net::Get_ServerWorld(); auto* SP = ServerPC(1, *F); auto* CP = PC(ck::auto_test::net::Get_ClientWorld(1), *F);
            if (!Started)
            {
                if (!SP || !CP || !ReadyAll(*F, Dedicated))
                { if (FPlatformTime::Seconds() - Start > 20) { Fail(Test, FString(TEXT("rejection preflight unavailable")) + PreflightObservation(*F)); return true; } return false; }
                AcceptedBefore = Int(F->PCClass, SP, TEXT("LastControlSerial")); AckBefore = Int(F->PCClass, SP, TEXT("LastAcknowledgedDeliverySerial"));
                AttemptBefore = Int(F->PCClass, SP, TEXT("LastControlAttemptSerial"));
                Retained = MakeShared<FStructOnScope>(F->Commit->Struct);
                F->Commit->Struct->CopyScriptStruct(Retained->GetStructMemory(), Commit(SP, *F));
                const auto* D = Data(W, *F);
                for (int32 I = 0; I < 7; ++I)
                { Originals[I] = Original(W, *F, I); Generations[I] = Int(F->Row->Struct, Row(D, *F, I), TEXT("BranchGeneration")); }
                const auto G = RequestedGeneration < 0 ? Int(F->Snapshot->Struct, D, TEXT("Generation")) : RequestedGeneration;
                if (!Invoke(CP, F->Control, {{TEXT("Serial"), Serial}, {TEXT("Row"), RequestedRow}, {TEXT("Generation"), G}}))
                { Fail(Test, TEXT("malformed payload owning RPC unavailable")); return true; }
                // A following fresh malformed request is a reliable ordering barrier for default/stale serial rejection.
                if (Serial <= AttemptBefore && !Invoke(CP, F->Control, {{TEXT("Serial"), AttemptBefore + 1}, {TEXT("Row"), 0}, {TEXT("Generation"), G}}))
                { Fail(Test, TEXT("malformed input reliable barrier unavailable")); return true; }
                Started = true; Start = FPlatformTime::Seconds();
            }
            if (!SP) { Fail(Test, TEXT("rejection owning server PC lost")); return true; }
            const auto ExpectedAttempt = Serial > AttemptBefore ? Serial : AttemptBefore + 1;
            if (Int(F->PCClass, SP, TEXT("LastControlAttemptSerial")) == ExpectedAttempt)
            {
                bool Unchanged = Int(F->PCClass, SP, TEXT("LastControlResult")) == ExpectedResult &&
                    Int(F->PCClass, SP, TEXT("LastControlSerial")) == AcceptedBefore && Int(F->PCClass, SP, TEXT("LastAcknowledgedDeliverySerial")) == AckBefore &&
                    F->Commit->Identical(Commit(SP, *F), Retained->GetStructMemory(), 0);
                const auto* D = Data(W, *F);
                if (!Complete(D, *F)) { Fail(Test, TEXT("rejected input damaged complete world cohort")); return true; }
                for (int32 I = 0; I < 7; ++I)
                { Unchanged &= Original(W, *F, I) == Originals[I] && Int(F->Row->Struct, Row(D, *F, I), TEXT("BranchGeneration")) == Generations[I]; }
                if (!Unchanged) { Fail(Test, TEXT("rejected input changed actual original identities/generations, retained commit, or accepted/ACK watermarks")); }
                return true;
            }
            if (FPlatformTime::Seconds() - Start > 15) { Fail(Test, TEXT("malformed owning RPC receipt timed out")); return true; }
            return false;
        }
    };

    class FObservePendingOwnerLoss final : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test; TSharedRef<FFixture> F; int32 Serial;
        bool Dedicated, Started = false, Lost = false; double Start = -1; FDelegateHandle Pre, Post;
        FCk_Handle Before[7]; int32 Generations[7]{}; TArray<FCk_Handle> Old;
        int32 AcceptedBefore = 0, AckBefore = 0; TSharedPtr<FStructOnScope> Retained;
        void Stop() { FWorldDelegates::OnWorldPreActorTick.Remove(Pre); FWorldDelegates::OnWorldPostActorTick.Remove(Post); }
        void Observe(UWorld* W)
        {
            if (W != ck::auto_test::net::Get_ServerWorld() || ck::auto_test::net::Get_HasSessionFailed() || Lost) { return; }
            const auto* D = Data(W, *F); auto* P = ServerPC(0, *F);
            if (!D || !P) { Fail(Test, TEXT("pending owner observer lost actual world/PC")); return; }
            if (!Bool(F->Snapshot->Struct, D, TEXT("Ready")) && !Live(Before[0]) && Allocated(Before[0]) &&
                Int(F->PCClass, P, TEXT("LastControlAttemptSerial")) == Serial)
            {
                if (Int(F->PCClass, P, TEXT("LastControlSerial")) != AcceptedBefore || !IsValid(P->GetPawn()))
                { Fail(Test, TEXT("pending owner loss requires destructive admission before acceptance and actual pawn")); return; }
                Lost = true; P->UnPossess();
            }
        }
    public:
        FObservePendingOwnerLoss(FAutomationTestBase* T, TSharedRef<FFixture> InF, int32 S, bool D)
            : Test(T), F(InF), Serial(S), Dedicated(D) {}
        ~FObservePendingOwnerLoss() override { Stop(); }
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { Stop(); return true; }
            if (Start < 0) { Start = FPlatformTime::Seconds(); }
            auto* W = ck::auto_test::net::Get_ServerWorld(); auto* P = ServerPC(0, *F);
            if (!Started)
            {
                if (!P || !ReadyAll(*F, Dedicated))
                { if (FPlatformTime::Seconds() - Start > 20) { Fail(Test, FString(TEXT("pending owner preflight unavailable")) + PreflightObservation(*F)); return true; } return false; }
                const auto* D = Data(W, *F);
                AcceptedBefore = Int(F->PCClass, P, TEXT("LastControlSerial")); AckBefore = Int(F->PCClass, P, TEXT("LastAcknowledgedDeliverySerial"));
                Retained = MakeShared<FStructOnScope>(F->Commit->Struct); F->Commit->Struct->CopyScriptStruct(Retained->GetStructMemory(), Commit(P, *F));
                for (int32 I = 0; I < 7; ++I)
                {
                    Before[I] = Original(W, *F, I); Generations[I] = Int(F->Row->Struct, Row(D, *F, I), TEXT("BranchGeneration"));
                    Collect(Before[I], Old);
                    if (!Live(Before[I]) || !CaptureExtra(Script(Before[I]), Old))
                    { Fail(Test, TEXT("pending loss actual originals/retirement evidence unavailable")); return true; }
                }
                Collect(ProbeRoot(W, *F), Old);
                Pre = FWorldDelegates::OnWorldPreActorTick.AddLambda([this](UWorld* V, ELevelTick, float) { Observe(V); });
                Post = FWorldDelegates::OnWorldPostActorTick.AddLambda([this](UWorld* V, ELevelTick, float) { Observe(V); });
                Started = true; Start = FPlatformTime::Seconds();
                if (!Invoke(PC(ck::auto_test::net::Get_ClientWorld(0), *F), F->Control,
                    {{TEXT("Serial"), Serial}, {TEXT("Row"), 8}, {TEXT("Generation"), Int(F->Snapshot->Struct, D, TEXT("Generation"))}}))
                { Fail(Test, TEXT("pending owner real owning full-rerun RPC unavailable")); Stop(); return true; }
            }
            if (!P) { Fail(Test, TEXT("pending loss actual PC lost")); Stop(); return true; }
            const auto* D = Data(W, *F);
            if (D && Bool(F->Snapshot->Struct, D, TEXT("Failed")))
            { Fail(Test, TEXT("pending owner loss terminalized shared world")); Stop(); return true; }
            if (Lost && Bool(F->PCClass, P, TEXT("OwnerSessionFailed")) && ReadyAll(*F, Dedicated))
            {
                bool Kept = Int(F->PCClass, P, TEXT("LastControlResult")) == 2 && Int(F->PCClass, P, TEXT("LastControlSerial")) == AcceptedBefore &&
                    Int(F->PCClass, P, TEXT("LastAcknowledgedDeliverySerial")) == AckBefore && F->Commit->Identical(Commit(P, *F), Retained->GetStructMemory(), 0);
                for (const auto& H : Old) { Kept &= !Allocated(H); }
                for (int32 I = 0; I < 7; ++I)
                { Kept &= Live(Original(W, *F, I)) && Original(W, *F, I) != Before[I] &&
                    Int(F->Row->Struct, Row(D, *F, I), TEXT("BranchGeneration")) == Generations[I] + 1; }
                if (!Kept) { Fail(Test, TEXT("pending owner loss altered prior acceptance/package/ACK or skipped complete world retirement/replacement")); }
                else { Test->AddInfo(TEXT("Lost pending requester remains terminal with unchanged held acceptance while both real clients regain complete shared view")); }
                Stop(); return true;
            }
            if (FPlatformTime::Seconds() - Start > 60) { Fail(Test, TEXT("pending owner loss recovery evidence timed out")); Stop(); return true; }
            return false;
        }
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkEntityLifecycleGym_SharedWorld_Network,
    "Ck.Gym.Network.EntityLifecycle.SharedWorld",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityLifecycleGym_SharedWorld_Network::RunTest(const FString& Parameters)
{
    using namespace ck_lifecycle_shared_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Lifecycle topology")); return false; }
    const auto F = MakeShared<FFixture>();
    F->Mode = FSoftClassPath{TEXT("/Script/Angelscript.Ck_EntityLifecycleGym_GameMode")}.TryLoadClass<AGameModeBase>();
    F->StateClass = FSoftClassPath{TEXT("/Script/Angelscript.Ck_EntityLifecycleGym_GameState")}.TryLoadClass<AGameStateBase>();
    F->PCClass = FSoftClassPath{TEXT("/Script/Angelscript.Ck_EntityLifecycleGym_PlayerController")}.TryLoadClass<APlayerController>();
    F->ProbeClass = FSoftClassPath{TEXT("/Script/Angelscript.Ck_LifecycleGym_BridgeProbe")}.TryLoadClass<AActor>();
    F->RootClass = FSoftClassPath{TEXT("/Script/Angelscript.Ck_EntityScript_LifecycleGym_BridgeRoot")}.TryLoadClass<UCk_EntityScript_UE>();
    F->TargetClass = FSoftClassPath{TEXT("/Script/Angelscript.Ck_EntityScript_EntityLifecycleGym_SpawnTarget")}.TryLoadClass<UCk_EntityScript_UE>();
    F->StationClass = FSoftClassPath{TEXT("/Script/Angelscript.Ck_EntityScript_GymStation")}.TryLoadClass<UCk_EntityScript_UE>();
    F->Display = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Angelscript.CkGym_Station_TitleAndDescription"));
    F->Anchors = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Angelscript.CkGym_Station_Anchors"));
    bool Schema = IsValid(F->Mode) && IsValid(F->StateClass) && IsValid(F->PCClass) && IsValid(F->ProbeClass) &&
        IsValid(F->RootClass) && IsValid(F->TargetClass) && IsValid(F->StationClass) && IsValid(F->Display) && IsValid(F->Anchors);
    for (int32 I = 0; I < 7; ++I)
    {
        F->Scripts[I] = FSoftClassPath{FString::Printf(TEXT("/Script/Angelscript.Ck_EntityScript_EntityLifecycleGym_%s"), Names[I])}.TryLoadClass<UCk_EntityScript_UE>();
        Schema &= IsValid(F->Scripts[I]);
    }
    if (!TestTrue(TEXT("actual authored Lifecycle classes and dynamic fragment types"), Schema)) { return false; }
    F->Snapshot = Prop<FStructProperty>(F->StateClass, TEXT("Snapshot"));
    F->Commit = Prop<FStructProperty>(F->PCClass, TEXT("LastControlCommit"));
    F->Control = F->PCClass->FindFunctionByName(TEXT("Server_RequestLifecycleControl"));
    F->ACK = F->PCClass->FindFunctionByName(TEXT("Server_AcknowledgeCommit"));
    if (!TestTrue(TEXT("actual reflected wire structs and owning RPCs"), F->Snapshot && F->Commit && F->Control && F->ACK)) { return false; }
    F->Rows = Prop<FArrayProperty>(F->Snapshot->Struct, TEXT("Rows"));
    F->Row = F->Rows ? CastField<FStructProperty>(F->Rows->Inner) : nullptr;
    F->CommitSnapshot = Prop<FStructProperty>(F->Commit->Struct, TEXT("Snapshot"));
    Schema = F->Row && F->CommitSnapshot && F->CommitSnapshot->Struct == F->Snapshot->Struct &&
        F->Snapshot->Struct->GetFName() == TEXT("CkLifecycleGym_Snapshot") &&
        F->Commit->Struct->GetFName() == TEXT("CkLifecycleGym_ControlCommit") &&
        F->Row->Struct->GetFName() == TEXT("CkLifecycleGym_RowSample") &&
        F->Snapshot->HasAnyPropertyFlags(CPF_Net) && F->Commit->HasAnyPropertyFlags(CPF_Net) &&
        F->Control->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable) && F->Control->NumParms == 3 &&
        F->ACK->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable) && F->ACK->NumParms == 2;
    if (!TestTrue(TEXT("exact replicated complete package schema and reliable owner transport"), Schema)) { return false; }
    for (const auto* N : {TEXT("Generation"), TEXT("Sequence")}) { Schema &= Prop<FIntProperty>(F->Snapshot->Struct, N) != nullptr; }
    for (const auto* N : {TEXT("Ready"), TEXT("Failed")}) { Schema &= Prop<FBoolProperty>(F->Snapshot->Struct, N) != nullptr; }
    for (const auto* N : {TEXT("RequestSerial"), TEXT("DeliverySerial"), TEXT("Row")}) { Schema &= Prop<FIntProperty>(F->Commit->Struct, N) != nullptr; }
    for (const auto* N : {TEXT("BranchGeneration"), TEXT("Phase"), TEXT("Cycle"), TEXT("ActualCallbackCount"), TEXT("LastCompletedCycle"), TEXT("LastCycleCallbackCount")})
    { Schema &= Prop<FIntProperty>(F->Row->Struct, N) != nullptr; }
    for (const auto* N : {TEXT("Title"), TEXT("Body"), TEXT("Instructions")}) { Schema &= Prop<FStrProperty>(F->Row->Struct, N) != nullptr; }
    for (const auto* N : {TEXT("LastControlAttemptSerial"), TEXT("LastControlSerial"), TEXT("LastControlResult"), TEXT("LastAcknowledgedDeliverySerial"), TEXT("LocalPresentationSequence")})
    { Schema &= Prop<FIntProperty>(F->PCClass, N) != nullptr; }
    for (const auto* N : {TEXT("OwnerSessionFailed"), TEXT("LocalPresentationReady"), TEXT("LocalPresentationFailed")})
    { Schema &= Prop<FBoolProperty>(F->PCClass, N) != nullptr; }
    for (const auto* N : {TEXT("Serial"), TEXT("Row"), TEXT("Generation")}) { Schema &= Prop<FIntProperty>(F->Control, N) != nullptr; }
    for (const auto* N : {TEXT("Delivery"), TEXT("Serial")}) { Schema &= Prop<FIntProperty>(F->ACK, N) != nullptr; }
    for (const auto* N : {TEXT("Title"), TEXT("Description"), TEXT("Instructions")}) { Schema &= Prop<FTextProperty>(F->Display, N) != nullptr; }
    const auto* Footprint = Prop<FStructProperty>(F->Anchors, TEXT("FootprintCenter"));
    Schema &= Footprint && Footprint->Struct == TBaseStructure<FVector>::Get();
    Schema &= Prop<FObjectPropertyBase>(F->StateClass, TEXT("BridgeProbe")) &&
        Prop<FIntProperty>(F->StateClass, TEXT("ServerCohortCount")) && Prop<FIntProperty>(F->ProbeClass, TEXT("BranchGeneration"));
    for (int32 I = 0; I < 7; ++I)
    {
        const auto* Recipe = Prop<FStructProperty>(F->Scripts[I], TEXT("InitialTransform"));
        Schema &= Recipe && Recipe->Struct == TBaseStructure<FTransform>::Get();
        for (const auto* N : {TEXT("SharedAuthority"), TEXT("Closed"), TEXT("Failed")}) { Schema &= Prop<FBoolProperty>(F->Scripts[I], N) != nullptr; }
        const auto* Extra = Prop<FArrayProperty>(F->Scripts[I], TEXT("ExtraRetirement"));
        const auto* H = Extra ? CastField<FStructProperty>(Extra->Inner) : nullptr;
        Schema &= H && H->Struct == FCk_Handle::StaticStruct();
        for (const auto* N : {TEXT("ActualCallbackCount"), TEXT("LastCompletedCycle"), TEXT("LastCycleCallbackCount")})
        { Schema &= Prop<FIntProperty>(F->Scripts[I], N) != nullptr; }
        if (I == 2 || I == 4 || I == 5)
        {
            const auto* Timer = Prop<FStructProperty>(F->Scripts[I], TEXT("PhaseTimer"));
            Schema &= Timer && Timer->Struct->IsChildOf(FCk_Handle::StaticStruct());
            for (const auto* N : {TEXT("CurrentPhase"), TEXT("CycleCount")}) { Schema &= Prop<FIntProperty>(F->Scripts[I], N) != nullptr; }
        }
    }
    for (const auto* N : {TEXT("ChildA"), TEXT("ChildB"), TEXT("ChildC")}) { Schema &= Prop<FStructProperty>(F->Scripts[2], N) != nullptr; }
    Schema &= Prop<FIntProperty>(F->Scripts[2], TEXT("DestroyCallbackCount")) != nullptr;
    for (const auto* N : {TEXT("Pass_AddTag"), TEXT("Pass_TryGetTag"), TEXT("Pass_ForEachEmpty")}) { Schema &= Prop<FBoolProperty>(F->Scripts[4], N) != nullptr; }
    for (const auto* N : {TEXT("ForEachCount_BeforeRemove"), TEXT("ForEachCount_AfterRemove")}) { Schema &= Prop<FIntProperty>(F->Scripts[4], N) != nullptr; }
    Schema &= Prop<FStructProperty>(F->Scripts[4], TEXT("ChildEntity")) != nullptr;
    for (const auto* N : {TEXT("Pass_PendingCount"), TEXT("Pass_CompleteSetup"), TEXT("SetupCompleteCallbackFired"), TEXT("FullyCompleteCallbackFired"), TEXT("Pass_SetupCompleteCallback"), TEXT("Pass_FullyCompleteCallback")})
    { Schema &= Prop<FBoolProperty>(F->Scripts[5], N) != nullptr; }
    for (const auto* N : {TEXT("PendingCount"), TEXT("CycleStartCallbacks")}) { Schema &= Prop<FIntProperty>(F->Scripts[5], N) != nullptr; }
    Schema &= Prop<FStructProperty>(F->Scripts[5], TEXT("DeferredHandle")) != nullptr;
    Schema &= Prop<FStructProperty>(F->Scripts[6], TEXT("SpawnTarget")) && Prop<FBoolProperty>(F->Scripts[6], TEXT("TestsComplete")) &&
        Prop<FBoolProperty>(F->Scripts[6], TEXT("Pass_DoCastChecked"));
    if (!TestTrue(TEXT("all observed property types validated before PIE"), Schema)) { return false; }
    F->CaptureSettings();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3,
        TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level"), Parameters, F->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60));
    const auto ReadinessStart = MakeShared<double>(-1.0);
    const auto ReadinessLogged = MakeShared<bool>(false);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([this, F, Dedicated, ReadinessStart, ReadinessLogged]()
        {
            if (*ReadinessStart < 0) { *ReadinessStart = FPlatformTime::Seconds(); }
            const bool Ready = ReadyAll(*F, Dedicated);
            if (!Ready && !*ReadinessLogged && FPlatformTime::Seconds() - *ReadinessStart > 20.0)
            {
                *ReadinessLogged = true;
                for (auto* W : ck::auto_test::net::Get_AllPIEWorlds())
                {
                    auto* S = State(W, *F);
                    const auto* D = Data(W, *F);
                    auto* P = PC(W, *F);
                    AddInfo(FString::Printf(TEXT("Lifecycle readiness world=%s mode=%d state=%s ready=%d failed=%d reason=%s sequence=%d cohort=%d probeRootLive=%d census=%d panels=%d localReady=%d localFailed=%d localReason=%s"),
                        *GetNameSafe(W), IsValid(W) ? int32(W->GetNetMode()) : -1, *GetNameSafe(S),
                        Bool(F->Snapshot->Struct, D, TEXT("Ready")), Bool(F->Snapshot->Struct, D, TEXT("Failed")),
                        *String(F->Snapshot->Struct, D, TEXT("FailureReason")), Int(F->Snapshot->Struct, D, TEXT("Sequence")),
                        Int(F->StateClass, S, TEXT("ServerCohortCount")), Live(ProbeRoot(W, *F)),
                        Census(W, *F, W == ck::auto_test::net::Get_ServerWorld()), Panels(W, *F),
                        Bool(F->PCClass, P, TEXT("LocalPresentationReady")), Bool(F->PCClass, P, TEXT("LocalPresentationFailed")),
                        *String(F->PCClass, P, TEXT("LocalPresentationFailureReason"))));
                    if (W != ck::auto_test::net::Get_ServerWorld()) { continue; }
                    for (int32 I = 0; I < 7; ++I)
                    {
                        const auto H = Original(W, *F, I);
                        auto* Actual = Script(H);
                        AddInfo(FString::Printf(TEXT("Lifecycle readiness original=%s live=%d script=%s closed=%d failed=%d callbacks=%d phase=%d cycle=%d"),
                            Names[I], Live(H), *GetNameSafe(Actual), Bool(F->Scripts[I], Actual, TEXT("Closed")),
                            Bool(F->Scripts[I], Actual, TEXT("Failed")), Int(F->Scripts[I], Actual, TEXT("ActualCallbackCount")),
                            Int(F->Scripts[I], Actual, TEXT("CurrentPhase")), Int(F->Scripts[I], Actual, TEXT("CycleCount"))));
                    }
                }
            }
            return Ready;
        }), 45, TEXT("seven actual authority originals and two real remote actor/root replicas with actor-free panels")));
    AddCommand(new FObserveCycles(this, F));
    for (int32 Owner = 0; Owner < 2; ++Owner)
    {
        for (int32 R = 1; R <= 8; ++R)
        { const auto Serial = F->NextSerial[Owner]; F->NextSerial[Owner] += 2;
          AddCommand(new FObserveRerun(this, F, Owner, R, Serial, Dedicated)); }
    }
    AddCommand(new FObserveRejected(this, F, F->NextSerial[1]++, 0, -1, 4, Dedicated));
    AddCommand(new FObserveRejected(this, F, F->NextSerial[1]++, 9, -1, 4, Dedicated));
    AddCommand(new FObserveRejected(this, F, F->NextSerial[1]++, 1, 0, 4, Dedicated));
    AddCommand(new FObserveRejected(this, F, F->NextSerial[1]++, 1, 2147483646, 2, Dedicated));
    AddCommand(new FObserveRejected(this, F, 0, 8, -1, 4, Dedicated));
    AddCommand(new FObserveRejected(this, F, 1, 8, -1, 4, Dedicated));
    // Each default/stale request above also sends one fresh reliable rejection barrier.
    // Reserve both consumed attempt serials before scheduling this owner's next valid action.
    F->NextSerial[1] += 2;
    AddCommand(new FObservePendingOwnerLoss(this, F, F->NextSerial[0]++, Dedicated));
    const auto SurvivorSerial = F->NextSerial[1]; F->NextSerial[1] += 2;
    AddCommand(new FObserveRerun(this, F, 1, 1, SurvivorSerial, Dedicated));
    AddCommand(new FObserveRerun(this, F, 1, 1, F->NextSerial[1]++, Dedicated, true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]()
        { F->RestoreSettings(); return TestTrue(TEXT("Lifecycle PIE worlds drained"), ck::auto_test::net::Get_AllPIEWorlds().IsEmpty()); }),
        TEXT("always restore startup settings after EndPIE"), true));
    return true;
}

#endif
