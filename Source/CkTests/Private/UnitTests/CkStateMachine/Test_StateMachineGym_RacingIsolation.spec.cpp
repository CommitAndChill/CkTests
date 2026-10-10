#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkSelectionGym_BankCensus.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkStateMachineGym_Runtime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/ContextOwner/CkContextOwner_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"

#include "Components/TextRenderComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_sm_racing_isolation_test
{
    constexpr auto RacingPath = TEXT("/Script/Angelscript.Ck_SmTest_RacingEventDriven_GymActor");

    struct FRun
    {
        bool Dedicated = false;
        UClass* Class = nullptr;
        TWeakObjectPtr<AActor> Pending;
        TWeakObjectPtr<AActor> FastB;
        TWeakObjectPtr<AActor> FastA;
        FCk_Handle PendingRoot;
        FCk_Handle FastBRoot;
        FCk_Handle FastARoot;
        TArray<FCk_Handle> FastBForest;
        TArray<FCk_Handle> FastAForest;
        FString PendingText;
        bool Spawned = false;
        bool Closed = false;
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
        auto* Return = Field<FIntProperty>(C.Function, TEXT("ReturnValue"));
        if (!Return || C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = Return->GetPropertyValue_InContainer(C.Args->GetStructMemory());
        return true;
    }

    bool Bool(UObject* Object, const TCHAR* Name, bool& Out)
    {
        Out = false;
        FCall C(Object, Name);
        auto* Return = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        if (!Return || C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = Return->GetPropertyValue_InContainer(C.Args->GetStructMemory());
        return true;
    }

    bool Roots(UObject* Object, TArray<FCk_Handle>& Out)
    {
        Out.Reset();
        FCall C(Object, TEXT("Get_SourceRoots"));
        auto* Return = Field<FArrayProperty>(C.Function, TEXT("ReturnValue"));
        auto* Inner = Return ? CastField<FStructProperty>(Return->Inner) : nullptr;
        if (!Inner || Inner->Struct != FCk_Handle::StaticStruct() ||
            C.Function->NumParms != 1 || !C.Send()) { return false; }
        FScriptArrayHelper Array(Return,
            Return->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory()));
        for (int32 I = 0; I < Array.Num(); ++I)
        { Out.Add(*reinterpret_cast<FCk_Handle*>(Array.GetRawPtr(I))); }
        return true;
    }

    bool Close(AActor* Actor)
    {
        FCall C(Actor, TEXT("Request_CloseSource"));
        return C.Function && C.Function->NumParms == 0 && C.Send();
    }

    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    bool ReadCounters(AActor* Actor, int32& A, int32& B)
    {
        A = B = INDEX_NONE;
        if (!IsValid(Actor)) { return false; }
        auto* PA = Field<FIntProperty>(Actor->GetClass(), TEXT("Counter_DestA"));
        auto* PB = Field<FIntProperty>(Actor->GetClass(), TEXT("Counter_DestB"));
        if (!PA || !PB) { return false; }
        A = PA->GetPropertyValue_InContainer(Actor);
        B = PB->GetPropertyValue_InContainer(Actor);
        return true;
    }

    bool ReadResult(AActor* Actor, FString& Out)
    {
        Out.Reset();
        if (!IsValid(Actor)) { return false; }
        auto* Property = Field<FObjectProperty>(Actor->GetClass(), TEXT("ResultText"));
        auto* Text = Property ? Cast<UTextRenderComponent>(
            Property->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
        if (!IsValid(Text) || Text->GetOwner() != Actor) { return false; }
        Out = Text->Text.ToString();
        return true;
    }

    bool HasNoSmHandle(AActor* Actor)
    {
        if (!IsValid(Actor)) { return false; }
        auto* Property = Field<FStructProperty>(Actor->GetClass(), TEXT("SmHandle"));
        if (!Property || Property->Struct != FCk_Handle_StateMachine::StaticStruct())
        { return false; }
        const auto& Handle = *Property->ContainerPtrToValuePtr<FCk_Handle_StateMachine>(Actor);
        return !ck::IsValid(Handle);
    }

    bool SetTimings(AActor* Actor, float Slow, float Fast, float Settle)
    {
        if (!IsValid(Actor) || Actor->HasActorBegunPlay() ||
            Slow <= 0 || Fast <= 0 || Settle <= FMath::Max(Slow, Fast))
        { return false; }
        auto* S = Field<FFloatProperty>(Actor->GetClass(), TEXT("SlowDelaySeconds"));
        auto* F = Field<FFloatProperty>(Actor->GetClass(), TEXT("FastDelaySeconds"));
        auto* T = Field<FFloatProperty>(Actor->GetClass(), TEXT("SettleSeconds"));
        if (!S || !F || !T) { return false; }
        S->SetPropertyValue_InContainer(Actor, Slow);
        F->SetPropertyValue_InContainer(Actor, Fast);
        T->SetPropertyValue_InContainer(Actor, Settle);
        return true;
    }

    bool OneRoot(AActor* Actor, UWorld* World, FCk_Handle& Out)
    {
        Out = {};
        TArray<FCk_Handle> Found;
        if (!IsValid(Actor) || !IsValid(World) || Actor->GetWorld() != World ||
            !Roots(Actor, Found) || Found.Num() != 1 || !Allocated(Found[0]))
        { return false; }
        const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!Allocated(WorldRoot) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Found[0]) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Found[0]) != WorldRoot ||
            UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(Found[0]) != World)
        { return false; }
        Out = Found[0];
        return true;
    }

    bool PendingStillInert(const FRun& R)
    {
        auto* Actor = R.Pending.Get();
        int32 A, B, Count;
        bool Closed;
        FString Text;
        TArray<FCk_Handle> RootsNow;
        return IsValid(Actor) && Roots(Actor, RootsNow) &&
            RootsNow.Num() == 1 && RootsNow[0] == R.PendingRoot &&
            !Allocated(R.PendingRoot) &&
            Bool(Actor, TEXT("Get_SourceClosed"), Closed) && Closed &&
            Int(Actor, TEXT("Get_SourceConstructedCount"), Count) && Count == 0 &&
            HasNoSmHandle(Actor) &&
            ReadCounters(Actor, A, B) && A == 0 && B == 0 &&
            ReadResult(Actor, Text) && Text == R.PendingText;
    }

    bool LiveVerdict(AActor* Actor, UWorld* World, const FCk_Handle& Expected,
        int32 ExpectedA, int32 ExpectedB, const TCHAR* ExpectedText)
    {
        FCk_Handle Root;
        int32 A, B, Count;
        bool Closed;
        FString Text;
        return OneRoot(Actor, World, Root) && Root == Expected &&
            UCk_Utils_StateMachine_UE::Has(Root) &&
            UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor) == Root &&
            Bool(Actor, TEXT("Get_SourceClosed"), Closed) && !Closed &&
            Int(Actor, TEXT("Get_SourceConstructedCount"), Count) && Count == 1 &&
            ReadCounters(Actor, A, B) && A == ExpectedA && B == ExpectedB &&
            ReadResult(Actor, Text) && Text == ExpectedText;
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

    bool TwoPassiveClientReplicas(const FRun& R)
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        const uint64 BId = IrisId(Server, R.FastB.Get());
        const uint64 AId = IrisId(Server, R.FastA.Get());
        const uint64 PendingId = IrisId(Server, R.Pending.Get());
        if (!BId || !AId || BId == AId) { return false; }
        for (int32 I = 0; I != 2; ++I)
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(I);
            if (!IsValid(Client) || Client->GetNetMode() != NM_Client)
            { return false; }
            int32 LiveCount = 0;
            int32 PendingCount = 0;
            TSet<uint64> Ids;
            for (TActorIterator<AActor> It(Client); It; ++It)
            {
                auto* Actor = *It;
                if (!IsValid(Actor) || Actor->GetClass() != R.Class) { continue; }
                const uint64 Id = IrisId(Client, Actor);
                TArray<FCk_Handle> LocalRoots;
                int32 Constructed = INDEX_NONE;
                if (!Id || Ids.Contains(Id) || Actor->HasAuthority() ||
                    !Roots(Actor, LocalRoots) ||
                    !LocalRoots.IsEmpty() ||
                    !Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) ||
                    Constructed != 0)
                { return false; }
                Ids.Add(Id);
                if (Id == AId || Id == BId) { ++LiveCount; }
                else if (PendingId && Id == PendingId) { ++PendingCount; }
                else { return false; }
            }
            if (LiveCount != 2 || PendingCount > 1) { return false; }
        }
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

    FCk_Handle FindActualChildWithRootContext(const FRun& R)
    {
        for (const auto& H : R.FastBForest)
        {
            if (H != R.FastBRoot && Allocated(H) && ck::IsValid(H) &&
                UCk_Utils_ContextOwner_UE::Has(H) &&
                UCk_Utils_ContextOwner_UE::Get_ContextOwner(H) == R.FastBRoot)
            { return H; }
        }
        return {};
    }

    bool ForeignContextOutsideActualAncestryRejects(FRun& R,
        FCk_Handle Child)
    {
        if (!Allocated(Child) || !ck::IsValid(Child)) { return false; }
        FCk_Handle OriginalContext;
        if (!UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                Child, OriginalContext) || OriginalContext != R.FastBRoot)
        { return false; }
        int32 BeforeBA, BeforeBB, BeforeAA, BeforeAB;
        FString BeforeBText, BeforeAText;
        if (!ReadCounters(R.FastB.Get(), BeforeBA, BeforeBB) ||
            !ReadCounters(R.FastA.Get(), BeforeAA, BeforeAB) ||
            !ReadResult(R.FastB.Get(), BeforeBText) ||
            !ReadResult(R.FastA.Get(), BeforeAText))
        { return false; }

        // Real public mutation, not a fabricated fragment state. The selected
        // child's lifetime still descends from B while A is a sibling root.
        UCk_Utils_ContextOwner_UE::Request_Override(Child, R.FastARoot, {});
        const bool ForeignInstalled = Allocated(Child) && ck::IsValid(Child) &&
            UCk_Utils_ContextOwner_UE::Has(Child) &&
            UCk_Utils_ContextOwner_UE::Get_ContextOwner(Child) == R.FastARoot;
        FCk_Handle RejectedOutput = R.FastBRoot;
        const bool Rejected = ForeignInstalled &&
            !UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                Child, RejectedOutput) && RejectedOutput == FCk_Handle{} &&
            UCk_Utils_ContextOwner_UE::Get_ContextOwner(Child) == R.FastARoot;

        // Always restore the real source context before any retirement request.
        if (!Allocated(Child) || !ck::IsValid(Child)) { return false; }
        UCk_Utils_ContextOwner_UE::Request_Override(Child, R.FastBRoot, {});
        FCk_Handle RestoredOutput = R.FastARoot;
        int32 AfterBA, AfterBB, AfterAA, AfterAB;
        FString AfterBText, AfterAText;
        return Rejected && UCk_Utils_ContextOwner_UE::Has(Child) &&
            UCk_Utils_ContextOwner_UE::Get_ContextOwner(Child) == R.FastBRoot &&
            UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                Child, RestoredOutput) && RestoredOutput == R.FastBRoot &&
            ReadCounters(R.FastB.Get(), AfterBA, AfterBB) &&
            ReadCounters(R.FastA.Get(), AfterAA, AfterAB) &&
            ReadResult(R.FastB.Get(), AfterBText) &&
            ReadResult(R.FastA.Get(), AfterAText) &&
            BeforeBA == AfterBA && BeforeBB == AfterBB &&
            BeforeAA == AfterAA && BeforeAB == AfterAB &&
            BeforeBText == AfterBText && BeforeAText == AfterAText;
    }

    int32 RacingActorCount(UWorld* World, UClass* Class)
    {
        if (!IsValid(World) || !IsValid(Class)) { return INDEX_NONE; }
        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        { if (IsValid(*It) && (*It)->GetClass() == Class) { ++Count; } }
        return Count;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkStateMachineGym_RacingIsolation,
    "Ck.StateMachine.Gym.RacingIsolation.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineGym_RacingIsolation::RunTest(const FString& Parameters)
{
    using namespace ck_sm_racing_isolation_test;
    const auto Run = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Run->Dedicated))
    { AddError(TEXT("unsupported Racing isolation topology")); return false; }
    Run->Class = FSoftClassPath(RacingPath).TryLoadClass<AActor>();
    if (!TestTrue(TEXT("real Racing actor and source API resolve"),
        Run->Class && Run->Class->FindFunctionByName(TEXT("Request_CloseSource")) &&
        Run->Class->FindFunctionByName(TEXT("Get_SourceRoots"))))
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
        }), 60.0, TEXT("Racing authority and two client worlds ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (!IsValid(World)) { AddError(TEXT("Racing authority world unavailable")); return; }
            const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
                TryGet_TransientEntityForWorld(*World);
            FCk_Handle Out = WorldRoot;
            const bool DefaultRejected =
                !UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext({}, Out) &&
                Out == FCk_Handle{};
            ck::FEcsWorld Isolated;
            auto Bare = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                Isolated.Get_Registry());
            const bool BareBefore = Allocated(Bare) &&
                !UCk_Utils_ContextOwner_UE::Has(Bare);
            Out = WorldRoot;
            const bool BareRejected = BareBefore &&
                !UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(Bare, Out) &&
                Out == FCk_Handle{} && Allocated(Bare) &&
                !UCk_Utils_ContextOwner_UE::Has(Bare);
            TestTrue(TEXT("default and real bare live no-context source callbacks reject with cleared output and no mutation"),
                DefaultRejected && BareRejected);
            auto Spawn = [Run, World](const FVector& Location, float Slow,
                float Fast, float Settle, TWeakObjectPtr<AActor>& OutActor,
                FCk_Handle& OutRoot) {
                auto* Actor = World->SpawnActorDeferred<AActor>(Run->Class,
                    FTransform(Location));
                if (!IsValid(Actor) || !SetTimings(Actor, Slow, Fast, Settle))
                { if (IsValid(Actor)) { Actor->Destroy(); } return false; }
                OutActor = Actor;
                Actor->FinishSpawning(FTransform(Location));
                return OneRoot(Actor, World, OutRoot);
            };
            // First return is retained, then closed before deferred ECS construction.
            auto* Pending = World->SpawnActorDeferred<AActor>(Run->Class,
                FTransform(FVector(-250, 0, 0)));
            if (!TestTrue(TEXT("pending Racing actor allocated"), IsValid(Pending)))
            { return; }
            Run->Pending = Pending;
            Pending->FinishSpawning(FTransform(FVector(-250, 0, 0)));
            const bool FirstReturn = OneRoot(Pending, World, Run->PendingRoot) &&
                ReadResult(Pending, Run->PendingText);
            if (FirstReturn && !UCk_Utils_ContextOwner_UE::Has(Run->PendingRoot))
            {
                Out = WorldRoot;
                TestTrue(TEXT("real pending target without context rejects and clears output"),
                    !UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                        Run->PendingRoot, Out) && Out == FCk_Handle{} &&
                    Allocated(Run->PendingRoot) &&
                    !UCk_Utils_ContextOwner_UE::Has(Run->PendingRoot));
            }
            const bool PendingReceipt = FirstReturn && Close(Pending);
            TestTrue(TEXT("pending Racing source closes before construction"), PendingReceipt);
            if (!PendingReceipt) { Close(Pending); return; }

            const bool BReady = Spawn(FVector(0, 0, 0), 0.5f, 0.1f, 0.8f,
                Run->FastB, Run->FastBRoot);
            const bool AReady = Spawn(FVector(250, 0, 0), 0.1f, 0.5f, 0.8f,
                Run->FastA, Run->FastARoot);
            Run->Spawned = BReady && AReady &&
                Run->FastBRoot != Run->FastARoot &&
                Run->PendingRoot != Run->FastBRoot &&
                Run->PendingRoot != Run->FastARoot;
            TestTrue(TEXT("two simultaneously admitted Racing actors retain distinct source roots"),
                Run->Spawned);
        })));

    // Wait for both real actors to settle, without baking the expected winner
    // into the wait predicate. A wrong A/B result fails directly below.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            int32 BCount = INDEX_NONE, ACount = INDEX_NONE;
            FString BText, AText;
            if (!Run->Spawned) { return true; } // already failed visibly at spawn
            return IsValid(World) &&
                IsValid(Run->FastB.Get()) && IsValid(Run->FastA.Get()) &&
                Int(Run->FastB.Get(), TEXT("Get_SourceConstructedCount"), BCount) &&
                Int(Run->FastA.Get(), TEXT("Get_SourceConstructedCount"), ACount) &&
                BCount == 1 && ACount == 1 &&
                ReadResult(Run->FastB.Get(), BText) &&
                ReadResult(Run->FastA.Get(), AText) &&
                BText != TEXT("SM Racing Event-Driven") &&
                AText != TEXT("SM Racing Event-Driven");
        }), 15.0, TEXT("both real Racing actors construct and publish a verdict")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return !Run->Spawned || TwoPassiveClientReplicas(*Run);
        }), 15.0, TEXT("two exact passive Racing actor replicas reach each client")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            int32 BA, BB, AA, AB, PA, PB;
            FString BText, AText, PText;
            const bool BValues = ReadCounters(Run->FastB.Get(), BA, BB) &&
                ReadResult(Run->FastB.Get(), BText);
            const bool AValues = ReadCounters(Run->FastA.Get(), AA, AB) &&
                ReadResult(Run->FastA.Get(), AText);
            const bool PValues = ReadCounters(Run->Pending.Get(), PA, PB) &&
                ReadResult(Run->Pending.Get(), PText);
            TestTrue(TEXT("B-winner actor exposes real counters and verdict"), BValues);
            TestTrue(TEXT("reversed A-winner actor exposes real counters and verdict"), AValues);
            TestTrue(TEXT("closed pending actor exposes unchanged counters and text"), PValues);
            if (BValues)
            {
                TestEqual(TEXT("B-winner DestA count"), BA, 0);
                TestEqual(TEXT("B-winner DestB count"), BB, 1);
                TestEqual(TEXT("B-winner verdict"), BText, FString(TEXT("PASS")));
            }
            if (AValues)
            {
                TestEqual(TEXT("reversed A-winner DestA count"), AA, 1);
                TestEqual(TEXT("reversed A-winner DestB count"), AB, 0);
                TestEqual(TEXT("reversed A-winner verdict"), AText, FString(TEXT("FAIL")));
            }
            if (PValues)
            {
                TestEqual(TEXT("closed pending DestA count"), PA, 0);
                TestEqual(TEXT("closed pending DestB count"), PB, 0);
                TestEqual(TEXT("closed pending text"), PText, Run->PendingText);
            }
            FCk_Handle StaleOut = Run->FastBRoot;
            const bool StaleRejected = !Allocated(Run->PendingRoot) &&
                !UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                    Run->PendingRoot, StaleOut) && StaleOut == FCk_Handle{};
            TestTrue(TEXT("physically retired pending receipt rejects source context and clears output"),
                StaleRejected);
            const bool Forests = CaptureForest(World, Run->FastBRoot,
                Run->FastBForest) && CaptureForest(World, Run->FastARoot,
                Run->FastAForest);
            const auto ContextChild = Forests
                ? FindActualChildWithRootContext(*Run) : FCk_Handle{};
            TestTrue(TEXT("actual settled B source has a live SM child with B root context"),
                ck::IsValid(ContextChild));
            if (ck::IsValid(ContextChild))
            {
                TestTrue(TEXT("real SM child rejects same-world context outside its lifetime ancestry, restores exact source and leaves both actors unchanged"),
                    ForeignContextOutsideActualAncestryRejects(*Run, ContextChild));
            }
            const bool Ready = Run->Spawned &&
                Forests &&
                LiveVerdict(Run->FastB.Get(), World, Run->FastBRoot,
                    0, 1, TEXT("PASS")) &&
                LiveVerdict(Run->FastA.Get(), World, Run->FastARoot,
                    1, 0, TEXT("FAIL")) && PendingStillInert(*Run) &&
                TwoPassiveClientReplicas(*Run);
            TestTrue(TEXT("both real Racing outcomes, isolated receipts and client replicas remain settled"),
                Ready);
            const bool BClosed = IsValid(Run->FastB.Get()) && Close(Run->FastB.Get());
            // An accepted WithActor close may destroy its own actor immediately.
            const bool AUnaffected = LiveVerdict(Run->FastA.Get(), World,
                Run->FastARoot, 1, 0, TEXT("FAIL"));
            const bool AClosed = IsValid(Run->FastA.Get()) && Close(Run->FastA.Get());
            Run->Closed = BClosed && AClosed;
            TestTrue(TEXT("closing B cannot mutate A before both accepted sources close"),
                BClosed && AUnaffected && AClosed);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            if (!Run->Closed || Run->FastBForest.IsEmpty() ||
                Run->FastAForest.IsEmpty()) { return false; }
            for (const auto& H : Run->FastBForest)
            { if (Allocated(H)) { return false; } }
            for (const auto& H : Run->FastAForest)
            { if (Allocated(H)) { return false; } }
            return RacingActorCount(ck::auto_test::net::Get_ServerWorld(),
                Run->Class) == 1 && IsValid(Run->Pending.Get()) &&
                !Allocated(Run->PendingRoot);
        }), 15.0, TEXT("both accepted forests and actors physically retire while pending actor remains live")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            TestTrue(TEXT("pending actor remained unchanged throughout both races"),
                PendingStillInert(*Run));
            auto* Pending = Run->Pending.Get();
            TestTrue(TEXT("pending actor explicitly destroys after both accepted sources retire"),
                IsValid(Pending) && Pending->Destroy());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return !Allocated(Run->PendingRoot) &&
                !Allocated(Run->FastBRoot) && !Allocated(Run->FastARoot) &&
                RacingActorCount(ck::auto_test::net::Get_ServerWorld(),
                    Run->Class) == 0 &&
                RacingActorCount(ck::auto_test::net::Get_ClientWorld(0),
                    Run->Class) == 0 &&
                RacingActorCount(ck::auto_test::net::Get_ClientWorld(1),
                    Run->Class) == 0;
        }), 15.0, TEXT("all three Racing actors and retained source roots gone")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
