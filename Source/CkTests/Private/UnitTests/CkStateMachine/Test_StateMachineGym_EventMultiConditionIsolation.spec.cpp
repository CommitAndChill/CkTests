#include "Misc/AutomationTest.h"
#include "CkStateMachineGym_Runtime.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkSelectionGym_BankCensus.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkStateMachine/Condition/CkSmCondition_Utils.h"
#include "CkStateMachine/State/EntityScripts/CkSmState_EntityScript.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"

#include "Components/TextRenderComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_sm_event_multi_isolation_test
{
    constexpr auto ActorPath = TEXT("/Script/Angelscript.Ck_SmTest_EventDrivenMultiCondition_GymActor");
    constexpr auto FinishPath = TEXT("/Script/Angelscript.Ck_SmTest_EventDrivenMultiCondition_State_Finish");

    struct FTimerFacts
    {
        bool Captured = false;
        TArray<float> Goals;
        bool AllPauseOnDone = false;
        bool AllCountUp = false;
        bool AllOwnedByConditions = false;
    };

    struct FRun
    {
        bool Dedicated = false;
        UClass* ActorClass = nullptr;
        UClass* FinishClass = nullptr;
        TWeakObjectPtr<AActor> Pending;
        TWeakObjectPtr<AActor> A;
        TWeakObjectPtr<AActor> B;
        FCk_Handle PendingRoot;
        FCk_Handle ARoot;
        FCk_Handle BRoot;
        TArray<FCk_Handle> AForest;
        TArray<FCk_Handle> BForest;
        FString PendingText;
        FTimerFacts ATimers;
        FTimerFacts BTimers;
        FDelegateHandle Pre;
        FDelegateHandle Post;
        bool Spawned = false;
        bool Closed = false;
        void Stop()
        {
            if (Pre.IsValid()) { FWorldDelegates::OnWorldPreActorTick.Remove(Pre); Pre.Reset(); }
            if (Post.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(Post); Post.Reset(); }
        }
        ~FRun() { Stop(); }
    };

    template<class T> T* Field(const UStruct* Type, const TCHAR* Name)
    { return Type ? FindFProperty<T>(Type, Name) : nullptr; }

    struct FCall
    {
        UObject* Target = nullptr;
        UFunction* Function = nullptr;
        TUniquePtr<FStructOnScope> Args;
        FCall(UObject* Object, const TCHAR* Name)
        {
            if (!IsValid(Object)) { return; }
            Function = Object->FindFunction(Name);
            if (!IsValid(Function)) { Function = nullptr; return; }
            Target = Object;
            Args = MakeUnique<FStructOnScope>(Function);
        }
        bool Send() const
        { return IsValid(Target) && IsValid(Function) && Args &&
            Args->GetStructMemory() &&
            (Target->ProcessEvent(Function, Args->GetStructMemory()), true); }
    };

    bool Int(UObject* Object, const TCHAR* Name, int32& Out)
    {
        Out = INDEX_NONE;
        FCall C(Object, Name);
        auto* P = Field<FIntProperty>(C.Function, TEXT("ReturnValue"));
        if (!P || C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = P->GetPropertyValue_InContainer(C.Args->GetStructMemory());
        return true;
    }

    bool Roots(UObject* Object, TArray<FCk_Handle>& Out)
    {
        Out.Reset();
        FCall C(Object, TEXT("Get_SourceRoots"));
        auto* P = Field<FArrayProperty>(C.Function, TEXT("ReturnValue"));
        auto* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
        if (!Inner || Inner->Struct != FCk_Handle::StaticStruct() ||
            C.Function->NumParms != 1 || !C.Send()) { return false; }
        FScriptArrayHelper Array(P,
            P->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory()));
        for (int32 I = 0; I < Array.Num(); ++I)
        { Out.Add(*reinterpret_cast<FCk_Handle*>(Array.GetRawPtr(I))); }
        return true;
    }

    bool Primary(AActor* Actor, const TCHAR* Name, FCk_Handle& Out)
    {
        Out = {};
        FCall C(Actor, Name);
        auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!P || P->Struct != FCk_Handle::StaticStruct() ||
            C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = *P->ContainerPtrToValuePtr<FCk_Handle>(
            C.Args->GetStructMemory());
        return true;
    }

    bool Close(AActor* Actor)
    {
        FCall C(Actor, TEXT("Request_CloseSource"));
        return C.Function && C.Function->NumParms == 0 && C.Send();
    }

    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    bool ReadCounter(AActor* Actor, int32& Out)
    {
        Out = INDEX_NONE;
        if (!IsValid(Actor)) { return false; }
        auto* P = Field<FIntProperty>(Actor->GetClass(), TEXT("Counter_Finish"));
        if (!P) { return false; }
        Out = P->GetPropertyValue_InContainer(Actor);
        return true;
    }

    bool ReadResult(AActor* Actor, FString& Out)
    {
        Out.Reset();
        if (!IsValid(Actor)) { return false; }
        auto* P = Field<FObjectProperty>(Actor->GetClass(), TEXT("ResultText"));
        auto* Text = P ? Cast<UTextRenderComponent>(
            P->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
        if (!IsValid(Text) || Text->GetOwner() != Actor) { return false; }
        Out = Text->Text.ToString();
        return true;
    }

    bool ReadSm(AActor* Actor, FCk_Handle_StateMachine& Out)
    {
        Out = {};
        if (!IsValid(Actor)) { return false; }
        auto* P = Field<FStructProperty>(Actor->GetClass(), TEXT("SmHandle"));
        if (!P || P->Struct != FCk_Handle_StateMachine::StaticStruct())
        { return false; }
        Out = *P->ContainerPtrToValuePtr<FCk_Handle_StateMachine>(Actor);
        return true;
    }

    bool SetTimings(AActor* Actor, float Fast, float Slow, float Settle)
    {
        if (!IsValid(Actor) || Actor->HasActorBegunPlay() ||
            Fast <= 0 || Slow <= Fast || Settle <= Slow) { return false; }
        auto* F = Field<FFloatProperty>(Actor->GetClass(), TEXT("FastDelaySeconds"));
        auto* S = Field<FFloatProperty>(Actor->GetClass(), TEXT("SlowDelaySeconds"));
        auto* T = Field<FFloatProperty>(Actor->GetClass(), TEXT("SettleSeconds"));
        if (!F || !S || !T) { return false; }
        F->SetPropertyValue_InContainer(Actor, Fast);
        S->SetPropertyValue_InContainer(Actor, Slow);
        T->SetPropertyValue_InContainer(Actor, Settle);
        return true;
    }

    bool OneRoot(AActor* Actor, UWorld* World, FCk_Handle& Out)
    {
        Out = {};
        if (!IsValid(Actor) || !IsValid(World) || Actor->GetWorld() != World ||
            !Primary(Actor, TEXT("Get_SourcePrimaryTarget"), Out) ||
            !Allocated(Out) || !ck::IsValid(Out))
        { return false; }
        const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!Allocated(WorldRoot) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Out) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Out) != WorldRoot ||
            UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(Out) != World)
        { return false; }
        return true;
    }

    bool CaptureForest(UWorld* World, const FCk_Handle& Root,
        TArray<FCk_Handle>& Out)
    {
        Out.Reset();
        FString Why;
        return ck_selection_gym_census::TryCensusAllocatedBank(
            World, Root, {}, Out, Why) && Out.Contains(Root);
    }

    void ObserveTimerFacts(UWorld* World, const FCk_Handle& Root,
        FTimerFacts& Out)
    {
        if (!IsInGameThread() || Out.Captured || !IsValid(World) ||
            Root == FCk_Handle{}) { return; }
        if (ck::registry_table::Get_IsInParallelRegion(
            Root.Get_RegistryView().Get_RegistryHandle()) ||
            !Allocated(Root) || !ck::IsValid(Root)) { return; }
        const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!Allocated(WorldRoot) || !ck::IsValid(WorldRoot) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Root) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Root) != WorldRoot ||
            UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(Root) != World)
        { return; }
        TArray<FCk_Handle> Forest;
        if (!CaptureForest(World, Root, Forest)) { return; }
        TArray<float> Goals;
        bool PauseOnDone = true;
        bool CountUp = true;
        bool Owned = true;
        for (const auto& H : Forest)
        {
            if (!Allocated(H) || !ck::IsValid(H) ||
                !H.Has<ck::FFragment_Timer>()) { continue; }
            if (!H.Has<ck::FFragment_Timer_Params>() ||
                H.Has<ck::FTag_Timer_NeedsSetup>() ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H))
            { return; }
            const auto Parent = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H);
            Owned &= Parent != Root && Forest.Contains(Parent) &&
                Allocated(Parent) && ck::IsValid(Parent) &&
                UCk_Utils_SmCondition_UE::Has(Parent);
            PauseOnDone &= H.Get<ck::FFragment_Timer_Params>().Get_Behavior() ==
                ECk_Timer_Behavior::PauseOnDone;
            CountUp &= !H.Has<ck::FTag_Timer_Countdown>();
            const float Goal = H.Get<ck::FFragment_Timer>().Get_Chrono().
                Get_GoalValue().Get_Seconds();
            if (!FMath::IsFinite(Goal) || Goal <= 0) { return; }
            Goals.Add(Goal);
        }
        if (Goals.Num() < 2) { return; }
        Goals.Sort();
        Out.Goals = MoveTemp(Goals);
        Out.AllPauseOnDone = PauseOnDone;
        Out.AllCountUp = CountUp;
        Out.AllOwnedByConditions = Owned;
        Out.Captured = true;
    }

    void Observe(UWorld* World, FRun& R)
    {
        if (!IsInGameThread() || !IsValid(World) ||
            World != ck::auto_test::net::Get_ServerWorld())
        { return; }
        ObserveTimerFacts(World, R.ARoot, R.ATimers);
        ObserveTimerFacts(World, R.BRoot, R.BTimers);
    }

    bool GoalsAre(const FTimerFacts& Facts, float Fast, float Slow)
    {
        return Facts.Captured && Facts.Goals.Num() == 2 &&
            Facts.AllPauseOnDone && Facts.AllCountUp &&
            Facts.AllOwnedByConditions &&
            FMath::IsNearlyEqual(Facts.Goals[0], Fast, 0.0001f) &&
            FMath::IsNearlyEqual(Facts.Goals[1], Slow, 0.0001f);
    }

    bool LiveFinish(AActor* Actor, UWorld* World, const FCk_Handle& Expected,
        UClass* FinishClass)
    {
        FCk_Handle Root;
        FCk_Handle_StateMachine Sm;
        int32 Count, Constructed;
        FString Text;
        TArray<FCk_Handle> Passes;
        FCk_Handle Accepted;
        return OneRoot(Actor, World, Root) && Root == Expected &&
            Primary(Actor, TEXT("Get_SourcePrimary"), Accepted) &&
            Accepted == Root &&
            UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePrimary(
                Root, Actor) &&
            Roots(Actor, Passes) && Passes.Num() == 1 &&
            UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePass(
                Passes[0], Root) &&
            UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor) == Root &&
            Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) &&
            Constructed == 1 && ReadSm(Actor, Sm) &&
            FCk_Handle(Sm) == Passes[0] && Allocated(FCk_Handle(Sm)) &&
            ck::IsValid(FCk_Handle(Sm)) &&
            UCk_Utils_StateMachine_UE::Has(FCk_Handle(Sm)) &&
            UCk_Utils_StateMachine_UE::Get_CurrentStateClass(Sm).Get() == FinishClass &&
            ReadCounter(Actor, Count) && Count == 1 &&
            ReadResult(Actor, Text) && Text == TEXT("PASS");
    }

    bool PendingInert(const FRun& R)
    {
        auto* Actor = R.Pending.Get();
        TArray<FCk_Handle> Found;
        FCk_Handle_StateMachine Sm;
        int32 Count, Constructed;
        FString Text;
        FCk_Handle Target, Accepted;
        return IsValid(Actor) && Roots(Actor, Found) && Found.IsEmpty() &&
            Primary(Actor, TEXT("Get_SourcePrimaryTarget"), Target) &&
            Target == R.PendingRoot &&
            Primary(Actor, TEXT("Get_SourcePrimary"), Accepted) &&
            Accepted == FCk_Handle{} && !Allocated(R.PendingRoot) &&
            Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) &&
            Constructed == 0 && ReadSm(Actor, Sm) && !ck::IsValid(Sm) &&
            ReadCounter(Actor, Count) && Count == 0 &&
            ReadResult(Actor, Text) && Text == R.PendingText;
    }

    uint64 IrisId(UWorld* World, AActor* Actor)
    {
        if (!IsValid(World) || !IsValid(Actor) || Actor->GetWorld() != World)
        { return 0; }
        auto* Driver = World->GetNetDriver();
        if (!IsValid(Driver) || Driver->GetWorld() != World) { return 0; }
        auto* System = Driver->GetReplicationSystem();
        auto* Bridge = IsValid(System) ? System->GetReplicationBridge() : nullptr;
        if (!IsValid(Bridge)) { return 0; }
        const auto Handle = Bridge->GetReplicatedRefHandle(Actor);
        return Handle.IsValid() ? Handle.GetId() : 0;
    }

    bool PassiveReplicas(const FRun& R)
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        const uint64 AId = IrisId(Server, R.A.Get());
        const uint64 BId = IrisId(Server, R.B.Get());
        const uint64 PendingId = IrisId(Server, R.Pending.Get());
        if (!AId || !BId || AId == BId) { return false; }
        for (int32 I = 0; I != 2; ++I)
        {
            auto* World = ck::auto_test::net::Get_ClientWorld(I);
            if (!IsValid(World) || World->GetNetMode() != NM_Client)
            { return false; }
            TSet<uint64> Seen;
            int32 LiveCount = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                auto* Actor = *It;
                if (!IsValid(Actor) || Actor->GetClass() != R.ActorClass) { continue; }
                const uint64 Id = IrisId(World, Actor);
                TArray<FCk_Handle> ClientRoots;
                int32 Constructed;
                if (!Id || Seen.Contains(Id) || Actor->HasAuthority() ||
                    !Roots(Actor, ClientRoots) || !ClientRoots.IsEmpty() ||
                    !Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) ||
                    Constructed != 0) { return false; }
                Seen.Add(Id);
                if (Id == AId || Id == BId) { ++LiveCount; }
                else if (!PendingId || Id != PendingId) { return false; }
            }
            if (LiveCount != 2) { return false; }
        }
        return true;
    }

    int32 ActorCount(UWorld* World, UClass* Class)
    {
        if (!IsValid(World) || !IsValid(Class)) { return INDEX_NONE; }
        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        { if (IsValid(*It) && (*It)->GetClass() == Class) { ++Count; } }
        return Count;
    }

    // Ordinary automation latent command: cleanup still executes when the
    // network harness has failed the session and skips RunOnServer actions.
    class FStopTimerObservation final : public IAutomationLatentCommand
    {
    public:
        explicit FStopTimerObservation(TSharedPtr<FRun> InRun)
            : Run(MoveTemp(InRun)) {}
        bool Update() override { Run->Stop(); return true; }
    private:
        TSharedPtr<FRun> Run;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkStateMachineGym_EventMultiConditionIsolation,
    "Ck.StateMachine.Gym.EventMultiConditionIsolation.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineGym_EventMultiConditionIsolation::RunTest(const FString& Parameters)
{
    using namespace ck_sm_event_multi_isolation_test;
    const auto Run = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Run->Dedicated))
    { AddError(TEXT("unsupported EventMultiCondition isolation topology")); return false; }
    Run->ActorClass = FSoftClassPath(ActorPath).TryLoadClass<AActor>();
    Run->FinishClass = FSoftClassPath(FinishPath).TryLoadClass<UObject>();
    if (!TestTrue(TEXT("real EventMultiCondition actor and Finish classes resolve"),
        Run->ActorClass && Run->FinishClass &&
        Run->ActorClass->FindFunctionByName(TEXT("Request_CloseSource")) &&
        Run->ActorClass->FindFunctionByName(TEXT("Get_SourceRoots")) &&
        Run->ActorClass->FindFunctionByName(TEXT("Get_SourcePrimary")) &&
        Run->ActorClass->FindFunctionByName(TEXT("Get_SourcePrimaryTarget"))))
    { return false; }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || Server->GetNetMode() !=
                (Run->Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
                !Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Server))) { return false; }
            for (int32 I = 0; I != 2; ++I)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                if (!IsValid(Client) || Client->GetNetMode() != NM_Client)
                { return false; }
            }
            return true;
        }), 60.0, TEXT("EventMultiCondition authority and two client worlds ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (!IsValid(World)) { AddError(TEXT("source world unavailable")); return; }
            const TWeakPtr<FRun> Weak(Run);
            Run->Pre = FWorldDelegates::OnWorldPreActorTick.AddLambda(
                [Weak](UWorld* W, ELevelTick, float) {
                    if (const auto Pinned = Weak.Pin()) { Observe(W, *Pinned); }
                });
            Run->Post = FWorldDelegates::OnWorldPostActorTick.AddLambda(
                [Weak](UWorld* W, ELevelTick, float) {
                    if (const auto Pinned = Weak.Pin()) { Observe(W, *Pinned); }
                });
            auto* Pending = World->SpawnActorDeferred<AActor>(Run->ActorClass,
                FTransform(FVector(-250, 0, 0)));
            if (!TestTrue(TEXT("pending EventMultiCondition actor allocated"),
                IsValid(Pending))) { return; }
            Run->Pending = Pending;
            Pending->FinishSpawning(FTransform(FVector(-250, 0, 0)));
            const bool FirstReturn = OneRoot(Pending, World, Run->PendingRoot) &&
                ReadResult(Pending, Run->PendingText);
            const bool PendingClosed = FirstReturn && Close(Pending);
            TestTrue(TEXT("third pending source closes before ECS construction"),
                PendingClosed);
            if (!PendingClosed) { Close(Pending); return; }

            auto Spawn = [Run, World](FVector Location, float Fast,
                float Slow, float Settle, TWeakObjectPtr<AActor>& OutActor,
                FCk_Handle& OutRoot) {
                auto* Actor = World->SpawnActorDeferred<AActor>(Run->ActorClass,
                    FTransform(Location));
                if (!IsValid(Actor) || !SetTimings(Actor, Fast, Slow, Settle))
                { if (IsValid(Actor)) { Actor->Destroy(); } return false; }
                OutActor = Actor;
                Actor->FinishSpawning(FTransform(Location));
                return OneRoot(Actor, World, OutRoot);
            };
            const bool A = Spawn(FVector(0, 0, 0), 0.1f, 0.4f, 0.6f,
                Run->A, Run->ARoot);
            const bool B = Spawn(FVector(250, 0, 0), 0.8f, 1.2f, 1.4f,
                Run->B, Run->BRoot);
            Run->Spawned = A && B && Run->ARoot != Run->BRoot &&
                Run->ARoot != Run->PendingRoot &&
                Run->BRoot != Run->PendingRoot;
            TestTrue(TEXT("two simultaneous real sources retain distinct receipts and authored delays"),
                Run->Spawned);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return !Run->Spawned || (Run->ATimers.Captured && Run->BTimers.Captured);
        }), 15.0, TEXT("both real owner forests expose two configured ECS timer receipts")));
    ADD_LATENT_AUTOMATION_COMMAND(FStopTimerObservation(Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            TestTrue(TEXT("A owns configured fast/slow timers 0.1/0.4, PauseOnDone, count-up"),
                GoalsAre(Run->ATimers, 0.1f, 0.4f));
            TestTrue(TEXT("B owns configured fast/slow timers 0.8/1.2, PauseOnDone, count-up"),
                GoalsAre(Run->BTimers, 0.8f, 1.2f));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            if (!Run->Spawned) { return true; }
            int32 AC = INDEX_NONE, BC = INDEX_NONE;
            FString AT, BT;
            return Int(Run->A.Get(), TEXT("Get_SourceConstructedCount"), AC) &&
                Int(Run->B.Get(), TEXT("Get_SourceConstructedCount"), BC) &&
                AC == 1 && BC == 1 &&
                ReadResult(Run->A.Get(), AT) &&
                ReadResult(Run->B.Get(), BT) &&
                AT != TEXT("SM Event-Driven Multi-Condition") &&
                BT != TEXT("SM Event-Driven Multi-Condition");
        }), 15.0, TEXT("both real multi-condition actors publish a verdict")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return !Run->Spawned || PassiveReplicas(*Run);
        }), 15.0, TEXT("both exact passive source actor replicas reach both clients")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            int32 ACount, BCount, PendingCount;
            FString AText, BText, PendingText;
            const bool AValues = ReadCounter(Run->A.Get(), ACount) &&
                ReadResult(Run->A.Get(), AText);
            const bool BValues = ReadCounter(Run->B.Get(), BCount) &&
                ReadResult(Run->B.Get(), BText);
            const bool PValues = ReadCounter(Run->Pending.Get(), PendingCount) &&
                ReadResult(Run->Pending.Get(), PendingText);
            TestTrue(TEXT("A exposes real result"), AValues);
            TestTrue(TEXT("B exposes real result"), BValues);
            TestTrue(TEXT("closed pending actor exposes real result"), PValues);
            if (AValues)
            {
                TestEqual(TEXT("A Finish task count"), ACount, 1);
                TestEqual(TEXT("A verdict"), AText, FString(TEXT("PASS")));
            }
            if (BValues)
            {
                TestEqual(TEXT("B Finish task count"), BCount, 1);
                TestEqual(TEXT("B verdict"), BText, FString(TEXT("PASS")));
            }
            if (PValues)
            {
                TestEqual(TEXT("closed pending Finish task count"), PendingCount, 0);
                TestEqual(TEXT("closed pending text"), PendingText, Run->PendingText);
            }
            const bool Forests = CaptureForest(World, Run->ARoot, Run->AForest) &&
                CaptureForest(World, Run->BRoot, Run->BForest);
            const bool BothReady = Forests &&
                GoalsAre(Run->ATimers, 0.1f, 0.4f) &&
                GoalsAre(Run->BTimers, 0.8f, 1.2f) &&
                LiveFinish(Run->A.Get(), World, Run->ARoot, Run->FinishClass) &&
                LiveFinish(Run->B.Get(), World, Run->BRoot, Run->FinishClass) &&
                PendingInert(*Run) && PassiveReplicas(*Run);
            TestTrue(TEXT("both distinct actual timer conditions preserve Fast Pass through transition Reset and finish once"),
                BothReady);

            const bool AClosed = IsValid(Run->A.Get()) && Close(Run->A.Get());
            const bool BUnaffected = LiveFinish(Run->B.Get(), World,
                Run->BRoot, Run->FinishClass);
            const bool BClosed = IsValid(Run->B.Get()) && Close(Run->B.Get());
            Run->Closed = AClosed && BClosed;
            TestTrue(TEXT("closing A leaves B settled and both accepted sources close"),
                AClosed && BUnaffected && BClosed);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            if (!Run->Closed || Run->AForest.IsEmpty() || Run->BForest.IsEmpty())
            { return false; }
            for (const auto& H : Run->AForest) { if (Allocated(H)) { return false; } }
            for (const auto& H : Run->BForest) { if (Allocated(H)) { return false; } }
            return !Allocated(Run->PendingRoot) &&
                IsValid(Run->Pending.Get()) &&
                ActorCount(ck::auto_test::net::Get_ServerWorld(),
                    Run->ActorClass) == 1;
        }), 15.0, TEXT("accepted multi-condition forests and actors retire while pending actor remains")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            TestTrue(TEXT("closed pending source had no cross-run counter or text mutation"),
                PendingInert(*Run));
            auto* Pending = Run->Pending.Get();
            TestTrue(TEXT("retained pending actor explicitly destroys"),
                IsValid(Pending) && Pending->Destroy());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return !Allocated(Run->PendingRoot) && !Allocated(Run->ARoot) &&
                !Allocated(Run->BRoot) &&
                ActorCount(ck::auto_test::net::Get_ServerWorld(),
                    Run->ActorClass) == 0 &&
                ActorCount(ck::auto_test::net::Get_ClientWorld(0),
                    Run->ActorClass) == 0 &&
                ActorCount(ck::auto_test::net::Get_ClientWorld(1),
                    Run->ActorClass) == 0;
        }), 15.0, TEXT("all three actors and both client replica cohorts physically gone")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
