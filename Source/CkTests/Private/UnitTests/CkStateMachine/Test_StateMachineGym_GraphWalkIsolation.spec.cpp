#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEntityScriptGym_Runtime.h"
#include "CkSelectionGym_BankCensus.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/ContextOwner/CkContextOwner_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkDynamic/CkDynamic_Utils.h"
#include "CkStateMachine/Debug/CkStateMachine_Debug_GraphWalk_Fragment.h"
#include "CkStateMachine/Debug/CkStateMachine_Debug_Utils.h"
#include "CkStateMachine/State/EntityScripts/CkSmState_EntityScript.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"
#include "CkStateMachine/Task/EntityScripts/CkSmTask_EntityScript.h"
#include "CkStateMachine/Task/CkSmTask_Fragment.h"
#include "CkStateMachine/Task/EntityScripts/CkSmTask_SubStateMachine.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment.h"

#include "Components/TextRenderComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_sm_graphwalk_isolation_test
{
    constexpr auto ActorPath =
        TEXT("/Script/Angelscript.Ck_SmTest_GraphWalkRegression_GymActor");
    constexpr int32 CounterCount = 10;
    constexpr const TCHAR* CounterNames[CounterCount] = {
        TEXT("Counter_TopA"), TEXT("Counter_TopB"), TEXT("Counter_TopC"),
        TEXT("Counter_TopD"), TEXT("Counter_TopStopE"), TEXT("Counter_SubA"),
        TEXT("Counter_SubB"), TEXT("Counter_SubC"), TEXT("Counter_SubD"),
        TEXT("Counter_SubStopE")};
    constexpr int32 ExpectedCounters[CounterCount] =
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0};

    struct FPair
    {
        TWeakObjectPtr<AActor> Actor;
        TArray<FCk_Handle> Roots;
        TArray<FCk_Handle> Forest;
    };

    struct FRun
    {
        bool Dedicated = false;
        bool Spawned = false;
        bool AClosed = false;
        bool BClosed = false;
        bool CaptureOverridden = false;
        bool PreviousCaptureVisible = false;
        bool CaptureStarted = false;
        UClass* Class = nullptr;
        UScriptStruct* StationDisplayType = nullptr;
        TUniquePtr<ck::FEcsWorld> IsolatedStationWorld;
        FCk_Handle BareStation;
        FPair Pending;
        FPair A;
        FPair B;
        FString PendingText;
        FCk_Handle InvalidRoot;
        FCk_Handle_StateMachine InvalidSm;
        TArray<FCk_Handle> InvalidForest;
        UClass* InvalidStateClass = nullptr;
        UClass* InvalidTaskClass = nullptr;
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

    bool Roots(AActor* Actor, TArray<FCk_Handle>& Out)
    {
        Out.Reset();
        FCall C(Actor, TEXT("Get_SourceRoots"));
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

    bool ReadText(AActor* Actor, FString& Out)
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

    bool ReadCounters(AActor* Actor, TArray<int32>& Out)
    {
        Out.Reset();
        if (!IsValid(Actor)) { return false; }
        for (const auto* Name : CounterNames)
        {
            auto* P = Field<FIntProperty>(Actor->GetClass(), Name);
            if (!P) { Out.Reset(); return false; }
            Out.Add(P->GetPropertyValue_InContainer(Actor));
        }
        return Out.Num() == CounterCount;
    }

    bool ReadHandle(AActor* Actor, const TCHAR* Name, FCk_Handle& Out)
    {
        Out = {};
        if (!IsValid(Actor)) { return false; }
        auto* P = Field<FStructProperty>(Actor->GetClass(), Name);
        if (!P || P->Struct != FCk_Handle::StaticStruct()) { return false; }
        Out = *P->ContainerPtrToValuePtr<FCk_Handle>(Actor);
        return true;
    }

    bool ReadSm(AActor* Actor, const TCHAR* Name, FCk_Handle_StateMachine& Out)
    {
        Out = {};
        if (!IsValid(Actor)) { return false; }
        auto* P = Field<FStructProperty>(Actor->GetClass(), Name);
        if (!P || P->Struct != FCk_Handle_StateMachine::StaticStruct())
        { return false; }
        Out = *P->ContainerPtrToValuePtr<FCk_Handle_StateMachine>(Actor);
        return true;
    }

    bool NoSm(AActor* Actor)
    {
        FCk_Handle_StateMachine Top, Sub;
        return ReadSm(Actor, TEXT("TopSmHandle"), Top) &&
            ReadSm(Actor, TEXT("SubSmParentHandle"), Sub) &&
            !ck::IsValid(Top) && !ck::IsValid(Sub);
    }

    bool SetDelays(AActor* Actor, float First, float Second)
    {
        if (!IsValid(Actor) || Actor->HasActorBegunPlay() ||
            !FMath::IsFinite(First) || !FMath::IsFinite(Second) ||
            First <= 0 || Second <= First) { return false; }
        auto* F = Field<FFloatProperty>(Actor->GetClass(), TEXT("FirstVerifyDelay"));
        auto* S = Field<FFloatProperty>(Actor->GetClass(), TEXT("SecondVerifyDelay"));
        if (!F || !S) { return false; }
        F->SetPropertyValue_InContainer(Actor, First);
        S->SetPropertyValue_InContainer(Actor, Second);
        return true;
    }

    bool SetStation(AActor* Actor, const FCk_Handle& Station)
    {
        if (!IsValid(Actor) || Actor->HasActorBegunPlay()) { return false; }
        auto* P = Field<FStructProperty>(Actor->GetClass(), TEXT("StationHandle"));
        if (!P || P->Struct != FCk_Handle::StaticStruct()) { return false; }
        *P->ContainerPtrToValuePtr<FCk_Handle>(Actor) = Station;
        return true;
    }

    bool BareStationUntouched(const FRun& R)
    {
        return R.IsolatedStationWorld.IsValid() &&
            IsValid(R.StationDisplayType) &&
            Allocated(R.BareStation) &&
            !UCk_Utils_ContextOwner_UE::Has(R.BareStation) &&
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(R.BareStation) &&
            !UCk_Utils_DynamicFragment_UE::Has_Fragment(
                R.BareStation, R.StationDisplayType);
    }

    bool ReadPair(AActor* Actor, UWorld* World, TArray<FCk_Handle>& Out)
    {
        Out.Reset();
        if (!IsValid(Actor) || !IsValid(World) ||
            Actor->GetWorld() != World || !Roots(Actor, Out) ||
            Out.Num() != 2 || Out[0] == Out[1])
        { Out.Reset(); return false; }
        const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!Allocated(WorldRoot) || !ck::IsValid(WorldRoot))
        { Out.Reset(); return false; }
        for (const auto& H : Out)
        {
            if (!Allocated(H) || !ck::IsValid(H) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H) != WorldRoot ||
                UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(H) != World)
            { Out.Reset(); return false; }
        }
        return true;
    }

    bool LivePair(const FPair& Pair, UWorld* World)
    {
        auto* Actor = Pair.Actor.Get();
        TArray<FCk_Handle> Current;
        FCk_Handle Top, Sub;
        FCk_Handle_StateMachine TopSm, SubSm;
        int32 Constructed = INDEX_NONE;
        return Pair.Roots.Num() == 2 &&
            ReadPair(Actor, World, Current) && Current == Pair.Roots &&
            Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) &&
            Constructed == 2 &&
            ReadHandle(Actor, TEXT("TopEntity"), Top) && Top == Current[0] &&
            ReadHandle(Actor, TEXT("SubEntity"), Sub) && Sub == Current[1] &&
            UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Top) == Actor &&
            UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Sub) == Actor &&
            ReadSm(Actor, TEXT("TopSmHandle"), TopSm) &&
            ReadSm(Actor, TEXT("SubSmParentHandle"), SubSm) &&
            FCk_Handle(TopSm) == Top && FCk_Handle(SubSm) == Sub &&
            UCk_Utils_StateMachine_UE::Has(Top) &&
            UCk_Utils_StateMachine_UE::Has(Sub) &&
            UCk_Utils_StateMachine_UE::Get_RunStatus(TopSm) ==
                ECk_SmRunStatus::Running &&
            UCk_Utils_StateMachine_UE::Get_RunStatus(SubSm) ==
                ECk_SmRunStatus::Running;
    }

    bool PairSm(const FPair& Pair, AActor* Actor,
        FCk_Handle_StateMachine& OutTop, FCk_Handle_StateMachine& OutSub)
    {
        OutTop = {};
        OutSub = {};
        return Pair.Roots.Num() == 2 &&
            ReadSm(Actor, TEXT("TopSmHandle"), OutTop) &&
            ReadSm(Actor, TEXT("SubSmParentHandle"), OutSub) &&
            FCk_Handle(OutTop) == Pair.Roots[0] &&
            FCk_Handle(OutSub) == Pair.Roots[1] &&
            Allocated(Pair.Roots[0]) && ck::IsValid(Pair.Roots[0]) &&
            Allocated(Pair.Roots[1]) && ck::IsValid(Pair.Roots[1]) &&
            UCk_Utils_StateMachine_UE::Has(Pair.Roots[0]) &&
            UCk_Utils_StateMachine_UE::Has(Pair.Roots[1]);
    }

#if CK_BUILD_SM_GRAPH_WALK
    bool HasFiveRealStates(const TMap<TSubclassOf<UCk_SmState_EntityScript>,
        ck::FCk_SmDebug_StateDefinition>& Definitions, const TCHAR* Prefix)
    {
        if (Definitions.Num() != 5) { return false; }
        for (const TCHAR* Suffix : {TEXT("A"), TEXT("B"), TEXT("C"),
            TEXT("D"), TEXT("E")})
        {
            const FString Path = FString::Printf(
                TEXT("/Script/Angelscript.Ck_SmTest_GraphWalk_%s_State_%s"),
                Prefix, Suffix);
            auto* Class = FSoftClassPath(Path).
                TryLoadClass<UCk_SmState_EntityScript>();
            if (!IsValid(Class)) { return false; }
            const auto* Def = Definitions.Find(Class);
            if (!Def || Def->Tasks.IsEmpty()) { return false; }
            bool HasActualTask = false;
            for (const auto& Task : Def->Tasks)
            { HasActualTask |= IsValid(Task.ScriptClass.Get()); }
            if (!HasActualTask) { return false; }
            if (FCString::Strcmp(Suffix, TEXT("E")) != 0)
            {
                if (Def->Transitions.Num() != 1 ||
                    Def->Transitions[0].Conditions.Num() != 1 ||
                    Def->Transitions[0].Conditions[0].Mode !=
                        ECk_SmConditionMode::Polled)
                { return false; }
            }
        }
        return true;
    }

    bool GraphComplete(const FPair& Pair)
    {
        FCk_Handle_StateMachine Top, Sub;
        if (!PairSm(Pair, Pair.Actor.Get(), Top, Sub) ||
            !UCk_Utils_StateMachineDebug_UE::Get_IsDebuggerCaptureActive(Top) ||
            !UCk_Utils_StateMachineDebug_UE::Get_IsDebuggerCaptureActive(Sub) ||
            !Top.Has<ck::FFragment_Sm_Debug_GraphDefinition>() ||
            !Sub.Has<ck::FFragment_Sm_Debug_GraphDefinition>())
        { return false; }
        const auto& TopGraph =
            Top.Get<ck::FFragment_Sm_Debug_GraphDefinition>();
        const auto& SubGraph =
            Sub.Get<ck::FFragment_Sm_Debug_GraphDefinition>();
        if (!TopGraph.Get_IsComplete() || !SubGraph.Get_IsComplete() ||
            !HasFiveRealStates(TopGraph.Get_StateDefinitions(), TEXT("Top")) ||
            SubGraph.Get_StateDefinitions().Num() != 1 ||
            SubGraph.Get_SubSmDefinitions().Num() != 1)
        { return false; }
        auto* Wrapper = FSoftClassPath(
            TEXT("/Script/Angelscript.Ck_SmTest_GraphWalk_SubSmWrapper_State")).
            TryLoadClass<UCk_SmState_EntityScript>();
        if (!IsValid(Wrapper)) { return false; }
        const auto* Outer = SubGraph.Get_StateDefinitions().Find(Wrapper);
        const auto* Nested = SubGraph.Get_SubSmDefinitions().Find(Wrapper);
        if (!Outer || !Nested || Outer->Tasks.IsEmpty() ||
            !HasFiveRealStates(Nested->StateDefinitions, TEXT("Sub")))
        { return false; }
        for (const auto& Task : Outer->Tasks)
        { if (Task.HasSubStateMachine &&
            IsValid(Task.SubSmInitialStateClass.Get())) { return true; } }
        return false;
    }
#else
    bool GraphComplete(const FPair&) { return false; }
#endif

    bool PendingInert(const FRun& R)
    {
        auto* Actor = R.Pending.Actor.Get();
        TArray<FCk_Handle> Current;
        TArray<int32> Counters;
        int32 Constructed = INDEX_NONE;
        bool Closed = false;
        FCk_Handle Top, Sub;
        FString Text;
        if (!IsValid(Actor) || !Roots(Actor, Current) ||
            Current != R.Pending.Roots || Current.Num() != 2 ||
            !Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) ||
            Constructed != 0 ||
            !Bool(Actor, TEXT("Get_SourceClosed"), Closed) || !Closed ||
            !ReadHandle(Actor, TEXT("TopEntity"), Top) ||
            !ReadHandle(Actor, TEXT("SubEntity"), Sub) ||
            Top != FCk_Handle{} || Sub != FCk_Handle{} || !NoSm(Actor) ||
            !ReadCounters(Actor, Counters) || !ReadText(Actor, Text) ||
            Text != R.PendingText)
        { return false; }
        for (const auto& H : Current) { if (Allocated(H)) { return false; } }
        for (const int32 Counter : Counters)
        { if (Counter != 0) { return false; } }
        return true;
    }

    bool CapturePairForest(UWorld* World, FPair& Pair)
    {
        Pair.Forest.Reset();
        if (Pair.Roots.Num() != 2) { return false; }
        FString Why;
        for (const auto& Root : Pair.Roots)
        {
            TArray<FCk_Handle> One;
            if (!ck_selection_gym_census::TryCensusAllocatedBank(
                World, Root, {}, One, Why) || !One.Contains(Root))
            { Pair.Forest.Reset(); return false; }
            for (const auto& H : One)
            {
                if (Pair.Forest.Contains(H))
                { Pair.Forest.Reset(); return false; }
                Pair.Forest.Add(H);
            }
        }
        return !Pair.Forest.IsEmpty();
    }

    bool ForestGone(const FPair& Pair)
    {
        if (Pair.Forest.IsEmpty()) { return false; }
        for (const auto& H : Pair.Forest)
        { if (Allocated(H)) { return false; } }
        return true;
    }

#if CK_BUILD_SM_GRAPH_WALK
    bool InvalidTempReady(const FRun& R, UWorld* World)
    {
        if (!IsValid(World) || !Allocated(R.InvalidRoot) ||
            !ck::IsValid(R.InvalidRoot) ||
            FCk_Handle(R.InvalidSm) != R.InvalidRoot ||
            !UCk_Utils_StateMachine_UE::Has(R.InvalidRoot) ||
            !R.InvalidRoot.Has<ck::FFragment_Sm_Debug_GraphWalk_Progress>() ||
            R.InvalidRoot.Has<ck::FFragment_Sm_Debug_GraphDefinition>() ||
            !IsValid(R.InvalidTaskClass))
        { return false; }
        for (const auto& State : UCk_Utils_EntityLifetime_UE::
            Get_LifetimeDependents(R.InvalidRoot))
        {
            if (!Allocated(State) || !ck::IsValid(State) ||
                !State.Has<ck::FTag_Sm_Debug_GraphWalkEntity>() ||
                !State.Has<ck::FFragment_EntityScript>()) { continue; }
            for (const auto& Task : UCk_Utils_EntityLifetime_UE::
                Get_LifetimeDependents(State))
            {
                if (!Allocated(Task) || !ck::IsValid(Task) ||
                    !Task.Has<ck::FFragment_SmTask_Params>() ||
                    !Task.Has<ck::FFragment_EntityScript>()) { continue; }
                if (Task.Get<ck::FFragment_SmTask_Params>().
                    Get_ScriptClass() == R.InvalidTaskClass)
                { return true; }
            }
        }
        return false;
    }

    bool InvalidGraphRejected(const FRun& R)
    {
        return Allocated(R.InvalidRoot) && ck::IsValid(R.InvalidRoot) &&
            FCk_Handle(R.InvalidSm) == R.InvalidRoot &&
            UCk_Utils_StateMachine_UE::Has(R.InvalidRoot) &&
            UCk_Utils_StateMachine_UE::Get_RunStatus(R.InvalidSm) ==
                ECk_SmRunStatus::Stopped &&
            !R.InvalidRoot.Has<ck::FFragment_Sm_Debug_GraphDefinition>();
    }
#else
    bool InvalidTempReady(const FRun&, UWorld*) { return false; }
    bool InvalidGraphRejected(const FRun&) { return false; }
#endif

    bool InvalidForestGone(const FRun& R)
    {
        if (R.InvalidForest.IsEmpty()) { return false; }
        for (const auto& H : R.InvalidForest)
        { if (Allocated(H)) { return false; } }
        return true;
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

    bool PassiveClients(const FRun& R)
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        const uint64 A = IrisId(Server, R.A.Actor.Get());
        const uint64 B = IrisId(Server, R.B.Actor.Get());
        const uint64 Pending = IrisId(Server, R.Pending.Actor.Get());
        if (!A || !B || A == B) { return false; }
        for (int32 I = 0; I != 2; ++I)
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(I);
            if (!IsValid(Client) || Client->GetNetMode() != NM_Client)
            { return false; }
            TSet<uint64> Seen;
            for (TActorIterator<AActor> It(Client); It; ++It)
            {
                auto* Actor = *It;
                if (!IsValid(Actor) || Actor->GetClass() != R.Class) { continue; }
                const uint64 Id = IrisId(Client, Actor);
                TArray<FCk_Handle> LocalRoots;
                int32 Constructed = INDEX_NONE;
                if (!Id || Seen.Contains(Id) || Actor->HasAuthority() ||
                    !Roots(Actor, LocalRoots) || !LocalRoots.IsEmpty() ||
                    !Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) ||
                    Constructed != 0 ||
                    (Id != A && Id != B && (!Pending || Id != Pending)))
                { return false; }
                Seen.Add(Id);
            }
            if (!Seen.Contains(A) || !Seen.Contains(B) ||
                Seen.Num() != (Seen.Contains(Pending) ? 3 : 2))
            { return false; }
        }
        return true;
    }

    int32 CountActors(UWorld* World, UClass* Class)
    {
        if (!IsValid(World) || !IsValid(Class)) { return INDEX_NONE; }
        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        { if (IsValid(*It) && (*It)->GetClass() == Class) { ++Count; } }
        return Count;
    }

    struct FCleanup : IAutomationLatentCommand
    {
        TSharedPtr<FRun> Run;
        explicit FCleanup(TSharedPtr<FRun> InRun) : Run(MoveTemp(InRun)) {}
        bool Update() override
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!Run.IsValid()) { return true; }
            if (IsValid(World))
            {
                if (Allocated(Run->InvalidRoot) &&
                    ck::IsValid(Run->InvalidRoot) &&
                    UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(
                        Run->InvalidRoot) &&
                    UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(
                        Run->InvalidRoot) == World)
                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(
                    Run->InvalidRoot); }
                for (const FPair* Pair : {&Run->A, &Run->B, &Run->Pending})
                {
                    auto* Actor = Pair->Actor.Get();
                    if (!IsValid(Actor) || Actor->GetWorld() != World ||
                        Actor->GetClass() != Run->Class) { continue; }
                    TArray<FCk_Handle> Current;
                    if (Roots(Actor, Current) && Current == Pair->Roots &&
                        Current.Num() == 2)
                    {
                        Close(Actor);
                        if (IsValid(Actor) && Actor->GetWorld() == World)
                        { Actor->Destroy(); }
                    }
                }
            }
            if (Run->CaptureOverridden)
            {
                UCk_Utils_StateMachineDebug_UE::Set_IsDebuggerCaptureVisible(
                    Run->PreviousCaptureVisible);
                Run->CaptureOverridden = false;
            }
            return true;
        }
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkStateMachineGym_GraphWalkIsolation,
    "Ck.StateMachine.Gym.GraphWalkIsolation.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineGym_GraphWalkIsolation::RunTest(const FString& Parameters)
{
    using namespace ck_sm_graphwalk_isolation_test;
    const auto Run = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Run->Dedicated))
    { AddError(TEXT("unsupported GraphWalk isolation topology")); return false; }
    Run->Class = FSoftClassPath(ActorPath).TryLoadClass<AActor>();
    Run->InvalidStateClass = FSoftClassPath(TEXT(
        "/Script/Angelscript.Ck_SmTest_GraphWalk_InvalidWrapper_State")).
        TryLoadClass<UCk_SmState_EntityScript>();
    Run->InvalidTaskClass = FSoftClassPath(TEXT(
        "/Script/Angelscript.Ck_SmTest_GraphWalk_InvalidSubSmTask")).
        TryLoadClass<UCk_SmTask_EntityScript>();
    Run->StationDisplayType = FindObject<UScriptStruct>(nullptr,
        TEXT("/Script/Angelscript.CkGym_Station_TitleAndDescription"));
#if !CK_BUILD_SM_GRAPH_WALK
    AddError(TEXT("GraphWalk isolation requires CK_BUILD_SM_GRAPH_WALK=1"));
    return false;
#endif
    if (!TestTrue(TEXT("real GraphWalk actor exposes retained source API"),
        Run->Class && Run->StationDisplayType &&
        IsValid(Run->InvalidStateClass) &&
        IsValid(Run->InvalidTaskClass) &&
        Field<FStructProperty>(Run->Class, TEXT("StationHandle")) &&
        Run->Class->FindFunctionByName(TEXT("Get_SourceRoots")) &&
        Run->Class->FindFunctionByName(TEXT("Request_CloseSource")) &&
        Run->Class->FindFunctionByName(TEXT("Get_SourceConstructedCount"))))
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
        }), 60.0, TEXT("GraphWalk server and both client worlds ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (!IsValid(World)) { AddError(TEXT("GraphWalk server missing")); return; }
            auto* Pending = World->SpawnActorDeferred<AActor>(Run->Class,
                FTransform(FVector(-300, 0, 0)));
            if (!TestTrue(TEXT("pending GraphWalk actor spawned"), IsValid(Pending)))
            { return; }
            Run->Pending.Actor = Pending;
            Pending->FinishSpawning(FTransform(FVector(-300, 0, 0)));
            const bool PendingReceipt =
                ReadPair(Pending, World, Run->Pending.Roots) &&
                ReadText(Pending, Run->PendingText) && Close(Pending);
            TestTrue(TEXT("two pending GraphWalk sources close before construction"),
                PendingReceipt);
            if (!PendingReceipt) { Close(Pending); return; }

            Run->PreviousCaptureVisible =
                UCk_Utils_StateMachineDebug_UE::Get_IsDebuggerCaptureVisible();
            Run->CaptureOverridden = true;
            UCk_Utils_StateMachineDebug_UE::Set_IsDebuggerCaptureVisible(true);
            Run->IsolatedStationWorld = MakeUnique<ck::FEcsWorld>();
            Run->BareStation = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                Run->IsolatedStationWorld->Get_Registry());
            const bool BareReady = BareStationUntouched(*Run);
            TestTrue(TEXT("real isolated bare station is allocated and has no context, lifetime or display"),
                BareReady);
            if (!BareReady) { return; }

            auto Spawn = [Run, World](const FVector& At, float First,
                float Second, FPair& Out, bool UseBareStation) {
                auto* Actor = World->SpawnActorDeferred<AActor>(Run->Class,
                    FTransform(At));
                if (!IsValid(Actor) || !SetDelays(Actor, First, Second) ||
                    (UseBareStation &&
                        !SetStation(Actor, Run->BareStation)))
                { if (IsValid(Actor)) { Actor->Destroy(); } return false; }
                Out.Actor = Actor;
                Actor->FinishSpawning(FTransform(At));
                return ReadPair(Actor, World, Out.Roots);
            };
            const bool A = Spawn(FVector(0, 0, 0), 2.0f, 4.0f,
                Run->A, false);
            const bool B = Spawn(FVector(300, 0, 0), 2.5f, 4.5f,
                Run->B, true);
            Run->Spawned = A && B &&
                Run->A.Roots[0] != Run->B.Roots[0] &&
                Run->A.Roots[0] != Run->B.Roots[1] &&
                Run->A.Roots[1] != Run->B.Roots[0] &&
                Run->A.Roots[1] != Run->B.Roots[1];
            TestTrue(TEXT("two simultaneous GraphWalk actors have four distinct retained roots"),
                Run->Spawned);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            if (!Run->Spawned) { return true; }
            int32 A = INDEX_NONE, B = INDEX_NONE;
            return Int(Run->A.Actor.Get(), TEXT("Get_SourceConstructedCount"), A) &&
                Int(Run->B.Actor.Get(), TEXT("Get_SourceConstructedCount"), B) &&
                A == 2 && B == 2;
        }), 15.0, TEXT("all four real GraphWalk machines constructed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            FCk_Handle_StateMachine ATop, ASub, BTop, BSub;
            TArray<FCk_Handle> ARoots, BRoots;
            int32 ACount = INDEX_NONE, BCount = INDEX_NONE;
            const bool Four =
                ReadPair(Run->A.Actor.Get(), World, ARoots) &&
                ReadPair(Run->B.Actor.Get(), World, BRoots) &&
                ARoots == Run->A.Roots && BRoots == Run->B.Roots &&
                Int(Run->A.Actor.Get(), TEXT("Get_SourceConstructedCount"), ACount) &&
                Int(Run->B.Actor.Get(), TEXT("Get_SourceConstructedCount"), BCount) &&
                ACount == 2 && BCount == 2 &&
                PairSm(Run->A, Run->A.Actor.Get(), ATop, ASub) &&
                PairSm(Run->B, Run->B.Actor.Get(), BTop, BSub);
            TestTrue(TEXT("four exact live Top/Sub machines ready for capture"),
                Four);
            if (!Four) { return; }
            UCk_Utils_StateMachineDebug_UE::BeginDebuggerCapture(ATop);
            UCk_Utils_StateMachineDebug_UE::BeginDebuggerCapture(ASub);
            UCk_Utils_StateMachineDebug_UE::BeginDebuggerCapture(BTop);
            UCk_Utils_StateMachineDebug_UE::BeginDebuggerCapture(BSub);
            Run->CaptureStarted =
                UCk_Utils_StateMachineDebug_UE::Get_IsDebuggerCaptureActive(ATop) &&
                UCk_Utils_StateMachineDebug_UE::Get_IsDebuggerCaptureActive(ASub) &&
                UCk_Utils_StateMachineDebug_UE::Get_IsDebuggerCaptureActive(BTop) &&
                UCk_Utils_StateMachineDebug_UE::Get_IsDebuggerCaptureActive(BSub);
            TestTrue(TEXT("public debugger capture active on all four machines"),
                Run->CaptureStarted);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return !Run->Spawned || !Run->CaptureStarted ||
                (GraphComplete(Run->A) && GraphComplete(Run->B));
        }), 15.0, TEXT("four real graph definitions complete with reachable states and tasks")));

    // The invalid nested task is authored through AddTask, not injected into
    // a graph fragment. AutoStart Disabled prevents its runtime EnterTask.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            auto* SubTask = IsValid(Run->InvalidTaskClass) ?
                Cast<UCk_SmTask_SubStateMachine>(
                    Run->InvalidTaskClass->GetDefaultObject(false)) : nullptr;
            const bool InvalidDefault = IsValid(SubTask) &&
                !IsValid(SubTask->Get_InitialStateClass().Get());
            TestTrue(TEXT("authored invalid nested task has no initial state"),
                InvalidDefault);
            if (!InvalidDefault || !IsValid(World)) { return; }
            auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
                TryGet_TransientEntityForWorld(*World);
            if (!Allocated(WorldRoot) || !ck::IsValid(WorldRoot) ||
                !UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(WorldRoot))
            { AddError(TEXT("invalid GraphWalk world root unavailable")); return; }
            Run->InvalidRoot = UCk_Utils_EntityLifetime_UE::
                Request_CreateEntity(WorldRoot);
            if (!Allocated(Run->InvalidRoot) || !ck::IsValid(Run->InvalidRoot) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(
                    Run->InvalidRoot) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(
                    Run->InvalidRoot) != WorldRoot)
            { AddError(TEXT("invalid GraphWalk root creation failed")); return; }
            auto Params = FCk_StateMachine_Spec(Run->InvalidStateClass);
            Params.Set_AutoStart(ECk_SmAutoStart::Disabled);
            Run->InvalidSm = UCk_Utils_StateMachine_UE::Add(
                Run->InvalidRoot, Params);
            const bool Added = FCk_Handle(Run->InvalidSm) == Run->InvalidRoot &&
                UCk_Utils_StateMachine_UE::Has(Run->InvalidRoot);
            TestTrue(TEXT("real disabled malformed GraphWalk machine added"), Added);
            if (Added)
            { UCk_Utils_StateMachineDebug_UE::BeginDebuggerCapture(
                Run->InvalidSm); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return InvalidTempReady(*Run,
                ck::auto_test::net::Get_ServerWorld());
        }), 15.0, TEXT("real invalid GraphWalk temp state and task script attached")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(3));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            const bool Ready = InvalidTempReady(*Run, World);
            TestTrue(TEXT("invalid task remained in actual graph-walk progress after ticks"),
                Ready);
            if (!Ready) { return; }
            FString Why;
            const bool Forest = ck_selection_gym_census::TryCensusAllocatedBank(
                World, Run->InvalidRoot, {}, Run->InvalidForest, Why) &&
                Run->InvalidForest.Contains(Run->InvalidRoot);
            TestTrue(TEXT("invalid graph-walk owned forest captured"), Forest);
            if (!Forest) { return; }
            bool NestedSm = false;
            for (const auto& H : Run->InvalidForest)
            { NestedSm |= H != Run->InvalidRoot && Allocated(H) &&
                ck::IsValid(H) && H.Has<ck::FFragment_Sm_Params>(); }
            TestTrue(TEXT("invalid graph never publishes complete or starts nested SM"),
                InvalidGraphRejected(*Run) && !NestedSm);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(
                Run->InvalidRoot);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return InvalidForestGone(*Run);
        }), 15.0, TEXT("invalid graph-walk owned temp forest physically retired")));

    // Wait for both actual second-pass publications, not expected counter values.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            if (!Run->Spawned) { return true; } // spawn failure was asserted
            FString A, B;
            return ReadText(Run->A.Actor.Get(), A) &&
                ReadText(Run->B.Actor.Get(), B) &&
                A.Contains(TEXT("Pass 2/2")) && B.Contains(TEXT("Pass 2/2"));
        }), 15.0, TEXT("both GraphWalk actors publish actual second verify")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return !Run->Spawned || PassiveClients(*Run);
        }), 15.0, TEXT("both exact GraphWalk Iris replicas reach each client")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            TArray<int32> A, B;
            FString AText, BText;
            const bool AValues = ReadCounters(Run->A.Actor.Get(), A) &&
                ReadText(Run->A.Actor.Get(), AText);
            const bool BValues = ReadCounters(Run->B.Actor.Get(), B) &&
                ReadText(Run->B.Actor.Get(), BText);
            TestTrue(TEXT("actor A has all ten actual counter fields"), AValues);
            TestTrue(TEXT("actor B has all ten actual counter fields"), BValues);
            if (AValues && BValues)
            {
                for (int32 I = 0; I != CounterCount; ++I)
                {
                    TestEqual(FString::Printf(TEXT("A %s"), CounterNames[I]),
                        A[I], ExpectedCounters[I]);
                    TestEqual(FString::Printf(TEXT("B %s"), CounterNames[I]),
                        B[I], ExpectedCounters[I]);
                }
                TestEqual(TEXT("actor A actual second verdict"), AText,
                    FString(TEXT("PASS (Pass 2/2)")));
                TestEqual(TEXT("actor B actual second verdict"), BText,
                    FString(TEXT("PASS (Pass 2/2)")));
            }
            TestTrue(TEXT("both actual Top/Sub state machines remain running"),
                LivePair(Run->A, World) && LivePair(Run->B, World));
            TestTrue(TEXT("both Top and nested Sub graphs remain complete under active capture"),
                Run->CaptureStarted &&
                GraphComplete(Run->A) && GraphComplete(Run->B));
            TestTrue(TEXT("closed third actor remains inert through both runs"),
                PendingInert(*Run));
            TestTrue(TEXT("B's invalid bare station remained unmodified after both real verify passes"),
                BareStationUntouched(*Run));
            TestTrue(TEXT("both server actors have separate passive client replicas"),
                PassiveClients(*Run));
            const bool Forests = CapturePairForest(World, Run->A) &&
                CapturePairForest(World, Run->B);
            TestTrue(TEXT("all four accepted source forests captured before closure"),
                Forests);
            Run->AClosed = Forests && Close(Run->A.Actor.Get());
            TestTrue(TEXT("actor A source closes"), Run->AClosed);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return !Run->AClosed || (ForestGone(Run->A) &&
                !IsValid(Run->A.Actor.Get()));
        }), 15.0, TEXT("A forest and actor physically retire while B stays live")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            TestTrue(TEXT("B remains the same live Top/Sub source after A closes"),
                LivePair(Run->B, World));
            TArray<int32> B;
            FString Text;
            const bool Values = ReadCounters(Run->B.Actor.Get(), B) &&
                ReadText(Run->B.Actor.Get(), Text);
            TestTrue(TEXT("B counters and text readable after A retirement"), Values);
            if (Values)
            {
                for (int32 I = 0; I != CounterCount; ++I)
                { TestEqual(FString::Printf(TEXT("B after A %s"), CounterNames[I]),
                    B[I], ExpectedCounters[I]); }
                TestEqual(TEXT("B still published final PASS"), Text,
                    FString(TEXT("PASS (Pass 2/2)")));
            }
            TestTrue(TEXT("closed pending source still inert after A retirement"),
                PendingInert(*Run));
            TestTrue(TEXT("B's bare station remains allocated and unmodified after A retirement"),
                BareStationUntouched(*Run));
            Run->BClosed = IsValid(Run->B.Actor.Get()) && Close(Run->B.Actor.Get());
            TestTrue(TEXT("actor B source closes"), Run->BClosed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return !Run->BClosed || (ForestGone(Run->A) &&
                ForestGone(Run->B) && CountActors(
                    ck::auto_test::net::Get_ServerWorld(), Run->Class) == 1 &&
                IsValid(Run->Pending.Actor.Get()));
        }), 15.0, TEXT("both accepted forests and actors physically retire")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            TestTrue(TEXT("pending actor stayed inert after both source retirements"),
                PendingInert(*Run));
            auto* Pending = Run->Pending.Actor.Get();
            TestTrue(TEXT("pending actor explicitly destroyed"),
                IsValid(Pending) && Pending->Destroy());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            if (CountActors(ck::auto_test::net::Get_ServerWorld(),
                    Run->Class) != 0 ||
                CountActors(ck::auto_test::net::Get_ClientWorld(0),
                    Run->Class) != 0 ||
                CountActors(ck::auto_test::net::Get_ClientWorld(1),
                    Run->Class) != 0 || !ForestGone(Run->A) ||
                !ForestGone(Run->B)) { return false; }
            for (const auto& H : Run->Pending.Roots)
            { if (Allocated(H)) { return false; } }
            return Run->Pending.Roots.Num() == 2;
        }), 15.0, TEXT("all GraphWalk actors, replicas and source receipts gone")));
    // Runs even after a failed wait skips ordinary server actions.
    ADD_LATENT_AUTOMATION_COMMAND(FCleanup(Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
