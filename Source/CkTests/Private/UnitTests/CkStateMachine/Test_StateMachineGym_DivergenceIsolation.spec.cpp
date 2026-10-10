#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEntityScriptGym_Runtime.h"
#include "CkStateMachineGym_Runtime.h"
#include "CkSelectionGym_BankCensus.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/ContextOwner/CkContextOwner_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkDynamic/CkDynamic_Utils.h"
#include "CkStateMachine/State/EntityScripts/CkSmState_EntityScript.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"
#include "CkStateMachine/Task/EntityScripts/CkSmTask_EntityScript.h"

#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_sm_divergence_isolation_test
{
    constexpr const TCHAR* UntimedPath =
        TEXT("/Script/Angelscript.Ck_SmTest_DivergenceFirstBranch_GymActor");
    constexpr const TCHAR* TimedPath =
        TEXT("/Script/Angelscript.Ck_SmTest_DivergenceTimed_GymActor");
    constexpr const TCHAR* SnapshotNames[12] = {
        TEXT("Snap_A_Enter"), TEXT("Snap_A_Idle"),
        TEXT("Snap_A_Branch"), TEXT("Snap_A_Left"),
        TEXT("Snap_A_Right"), TEXT("Snap_A_Finish"),
        TEXT("Snap_B_Enter"), TEXT("Snap_B_Idle"),
        TEXT("Snap_B_Branch"), TEXT("Snap_B_Left"),
        TEXT("Snap_B_Right"), TEXT("Snap_B_Finish")};
    constexpr int32 Expected[12] = {1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1};

    struct FActorReceipt
    {
        TWeakObjectPtr<AActor> Actor;
        FCk_Handle Primary;
        TArray<FCk_Handle> Roots; // A/B only
        TArray<FCk_Handle> Forest;
        TArray<int32> FinalSnapshots;
    };

    struct FRun
    {
        bool Dedicated = false;
        bool Timed = false;
        bool Spawned = false;
        bool OverlapSeen = false;
        bool FastClosed = false;
        bool SlowClosed = false;
        UClass* Class = nullptr;
        UScriptStruct* StationType = nullptr;
        TUniquePtr<ck::FEcsWorld> IsolatedWorld;
        FCk_Handle BareStation;
        FActorReceipt Pending, Fast, Slow;
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

    bool Bool(UObject* Object, const TCHAR* Name, bool& Out)
    {
        Out = false;
        FCall C(Object, Name);
        auto* P = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        if (!P || C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = P->GetPropertyValue_InContainer(C.Args->GetStructMemory());
        return true;
    }

    bool Roots(AActor* Actor, TArray<FCk_Handle>& Out)
    {
        Out.Reset();
        FCall C(Actor, TEXT("Get_SourceRoots"));
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

    bool PrimaryGetter(AActor* Actor, const TCHAR* Name, FCk_Handle& Out)
    {
        Out = {};
        FCall C(Actor, Name);
        auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!P || P->Struct != FCk_Handle::StaticStruct() ||
            C.Function->NumParms != 1 ||
            !P->HasAnyPropertyFlags(CPF_ReturnParm) ||
            P->GetSize() != sizeof(FCk_Handle) ||
            P->GetOffset_ForUFunction() < 0 ||
            P->GetOffset_ForUFunction() > C.Function->ParmsSize ||
            P->GetSize() > C.Function->ParmsSize - P->GetOffset_ForUFunction() ||
            !C.Send()) { return false; }
        const auto* Value = P->ContainerPtrToValuePtr<FCk_Handle>(
            C.Args->GetStructMemory());
        if (!Value) { return false; }
        Out = *Value;
        return true;
    }

    bool Close(AActor* Actor)
    {
        FCall C(Actor, TEXT("Request_CloseSource"));
        return C.Function && C.Function->NumParms == 0 && C.Send();
    }

    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

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

    bool Snapshots(AActor* Actor, TArray<int32>& Out)
    {
        Out.Reset();
        if (!IsValid(Actor)) { return false; }
        for (const TCHAR* Name : SnapshotNames)
        {
            auto* P = Field<FIntProperty>(Actor->GetClass(), Name);
            if (!P) { Out.Reset(); return false; }
            Out.Add(P->GetPropertyValue_InContainer(Actor));
        }
        return Out.Num() == 12;
    }

    bool CountersZero(AActor* Actor)
    {
        if (!IsValid(Actor)) { return false; }
        for (const TCHAR* Name : {TEXT("Counter_Enter"),
            TEXT("Counter_Idle"), TEXT("Counter_Branch"),
            TEXT("Counter_Left"), TEXT("Counter_Right"),
            TEXT("Counter_Finish")})
        {
            auto* P = Field<FIntProperty>(Actor->GetClass(), Name);
            if (!P || P->GetPropertyValue_InContainer(Actor) != 0)
            { return false; }
        }
        return true;
    }

    bool Settle(AActor* Actor, float Seconds)
    {
        if (!IsValid(Actor) || Actor->HasActorBegunPlay() ||
            !FMath::IsFinite(Seconds) || Seconds <= 0) { return false; }
        auto* P = Field<FFloatProperty>(Actor->GetClass(),
            TEXT("PerPassSettleSeconds"));
        if (!P) { return false; }
        P->SetPropertyValue_InContainer(Actor, Seconds);
        return true;
    }

    bool SetStation(AActor* Actor, const FCk_Handle& H)
    {
        if (!IsValid(Actor) || Actor->HasActorBegunPlay()) { return false; }
        auto* P = Field<FStructProperty>(Actor->GetClass(), TEXT("StationHandle"));
        if (!P || P->Struct != FCk_Handle::StaticStruct()) { return false; }
        *P->ContainerPtrToValuePtr<FCk_Handle>(Actor) = H;
        return true;
    }

    bool HasConfig(AActor* Actor, bool LeftFirst, int64 Choice)
    {
        if (!IsValid(Actor)) { return false; }
        const auto* Order = Field<FBoolProperty>(Actor->GetClass(),
            TEXT("AddOrderLeftFirst"));
        const auto* Enum = Field<FEnumProperty>(Actor->GetClass(),
            TEXT("PaymentChoice"));
        const auto* Byte = Field<FByteProperty>(Actor->GetClass(),
            TEXT("PaymentChoice"));
        int64 ActualChoice = MIN_int64;
        if (Enum && Enum->GetUnderlyingProperty())
        { ActualChoice = Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(
            Enum->ContainerPtrToValuePtr<void>(Actor)); }
        else if (Byte)
        { ActualChoice = Byte->GetPropertyValue_InContainer(Actor); }
        return Order &&
            Order->GetPropertyValue_InContainer(Actor) == LeftFirst &&
            ActualChoice == Choice;
    }

    bool BareUntouched(const FRun& R)
    {
        return R.IsolatedWorld.IsValid() && IsValid(R.StationType) &&
            Allocated(R.BareStation) &&
            !UCk_Utils_ContextOwner_UE::Has(R.BareStation) &&
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(R.BareStation) &&
            !UCk_Utils_DynamicFragment_UE::Has_Fragment(
                R.BareStation, R.StationType);
    }

    bool LiveRoots(AActor* Actor, UWorld* World,
        int32 Count, const FCk_Handle& ExpectedPrimary,
        TArray<FCk_Handle>& Out)
    {
        Out.Reset();
        FCk_Handle Primary, Target;
        if (!IsValid(Actor) || !IsValid(World) ||
            Actor->GetWorld() != World || !Roots(Actor, Out) ||
            Out.Num() != Count ||
            !PrimaryGetter(Actor, TEXT("Get_SourcePrimaryTarget"), Target) ||
            !PrimaryGetter(Actor, TEXT("Get_SourcePrimary"), Primary) ||
            Primary != Target || Primary != ExpectedPrimary ||
            !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePrimary(
                Primary, Actor))
        { Out.Reset(); return false; }
        int32 Constructed = INDEX_NONE;
        if (!Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) ||
            Constructed != Count) { Out.Reset(); return false; }
        for (int32 I = 0; I < Count; ++I)
        {
            const auto H = Out[I];
            FCk_Handle Routed;
            if (H == Primary || (I > 0 && H == Out[0]) ||
                !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePass(
                    H, Primary) ||
                !UCk_StateMachineGym_Runtime_UE::TryGet_DirectPassChild(
                    H, Primary, Routed) || Routed != H ||
                !UCk_Utils_StateMachine_UE::Has(H))
            { Out.Reset(); return false; }
        }
        FCk_Handle PassA, PassB;
        FCk_Handle_StateMachine ASm, BSm;
        if (!ReadHandle(Actor, TEXT("PassAEntity"), PassA) ||
            PassA != Out[0] ||
            !ReadSm(Actor, TEXT("PassASmHandle"), ASm) ||
            FCk_Handle(ASm) != PassA ||
            (Count == 2 &&
                (!ReadHandle(Actor, TEXT("PassBEntity"), PassB) ||
                 PassB != Out[1] ||
                 !ReadSm(Actor, TEXT("PassBSmHandle"), BSm) ||
                 FCk_Handle(BSm) != PassB)))
        { Out.Reset(); return false; }
        return true;
    }

    bool PendingInert(const FRun& R)
    {
        auto* Actor = R.Pending.Actor.Get();
        TArray<FCk_Handle> Current;
        FCk_Handle A, B, Target, Accepted;
        FCk_Handle_StateMachine ASm, BSm;
        int32 Constructed = INDEX_NONE;
        bool Closed = false, Finished = true;
        if (!IsValid(Actor) || !Roots(Actor, Current) ||
            !Current.IsEmpty() || R.Pending.Roots.Num() != 0 ||
            !PrimaryGetter(Actor, TEXT("Get_SourcePrimaryTarget"), Target) ||
            Target != R.Pending.Primary ||
            !PrimaryGetter(Actor, TEXT("Get_SourcePrimary"), Accepted) ||
            Accepted != FCk_Handle{} ||
            !Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) ||
            Constructed != 0 ||
            !Bool(Actor, TEXT("Get_SourceClosed"), Closed) || !Closed ||
            !Bool(Actor, TEXT("Get_SourceRunFinished"), Finished) || Finished ||
            !ReadHandle(Actor, TEXT("PassAEntity"), A) || A != FCk_Handle{} ||
            !ReadHandle(Actor, TEXT("PassBEntity"), B) || B != FCk_Handle{} ||
            !ReadSm(Actor, TEXT("PassASmHandle"), ASm) || ck::IsValid(ASm) ||
            !ReadSm(Actor, TEXT("PassBSmHandle"), BSm) || ck::IsValid(BSm) ||
            !CountersZero(Actor) || Allocated(Target))
        { return false; }
        return true;
    }

    bool CaptureForest(UWorld* World, FActorReceipt& Pair)
    {
        if (!IsValid(World) || Pair.Roots.Num() != 2 ||
            !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePrimary(
                Pair.Primary, Pair.Actor.Get()))
        { return false; }
        const FCk_Handle WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!ck::IsValid(WorldRoot) || !Allocated(WorldRoot))
        { return false; }
        TArray<FCk_Handle> Candidate, Parents, Census;
        FString Why;
        if (!UCk_StateMachineGym_Runtime_UE::TryCollect_DivergenceHealthyForest(
                Pair.Primary, Pair.Actor.Get(), Pair.Roots,
                Candidate, Parents) ||
            Candidate.Num() < 3 || Candidate.Last() != Pair.Primary ||
            Parents.Num() != Candidate.Num() ||
            !ck_selection_gym_census::TryCensusAllocatedBank(
                World, Pair.Primary, {}, Census, Why) ||
            Census.Num() != Candidate.Num())
        { return false; }
        TSet<FCk_Handle> Seen;
        for (int32 I = 0; I < Candidate.Num(); ++I)
        {
            const auto& H = Candidate[I];
            if (!ck::IsValid(H) || !Allocated(H) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H) != Parents[I] ||
                Seen.Contains(H) || !Census.Contains(H))
            { return false; }
            if (H == Pair.Primary)
            {
                if (I != Candidate.Num() - 1 || Parents[I] != WorldRoot)
                { return false; }
            }
            else if (Candidate.Find(Parents[I]) <= I)
            { return false; }
            Seen.Add(H);
        }
        if (!Seen.Contains(Pair.Roots[0]) || !Seen.Contains(Pair.Roots[1]))
        { return false; }
        Pair.Forest = MoveTemp(Candidate);
        return true;
    }

    bool ForestGone(const FActorReceipt& Pair)
    {
        if (Pair.Forest.IsEmpty()) { return false; }
        for (const auto& H : Pair.Forest)
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
        const uint64 Fast = IrisId(Server, R.Fast.Actor.Get());
        const uint64 Slow = IrisId(Server, R.Slow.Actor.Get());
        const uint64 Pending = IrisId(Server, R.Pending.Actor.Get());
        if (!Fast || !Slow || Fast == Slow) { return false; }
        for (int32 I = 0; I != 2; ++I)
        {
            auto* World = ck::auto_test::net::Get_ClientWorld(I);
            if (!IsValid(World) || World->GetNetMode() != NM_Client)
            { return false; }
            TSet<uint64> Seen;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                auto* Actor = *It;
                if (!IsValid(Actor) || Actor->GetClass() != R.Class)
                { continue; }
                const uint64 Id = IrisId(World, Actor);
                TArray<FCk_Handle> Local;
                FCk_Handle LocalPrimary, LocalTarget;
                int32 Constructed = INDEX_NONE;
                bool Finished = true;
                if (!Id || Seen.Contains(Id) || Actor->HasAuthority() ||
                    !Roots(Actor, Local) || !Local.IsEmpty() ||
                    !PrimaryGetter(Actor, TEXT("Get_SourcePrimary"), LocalPrimary) ||
                    !PrimaryGetter(Actor, TEXT("Get_SourcePrimaryTarget"), LocalTarget) ||
                    LocalPrimary != FCk_Handle{} || LocalTarget != FCk_Handle{} ||
                    !Int(Actor, TEXT("Get_SourceConstructedCount"),
                        Constructed) || Constructed != 0 ||
                    !Bool(Actor, TEXT("Get_SourceRunFinished"), Finished) ||
                    Finished ||
                    (Id != Fast && Id != Slow && (!Pending || Id != Pending)))
                { return false; }
                Seen.Add(Id);
            }
            if (!Seen.Contains(Fast) || !Seen.Contains(Slow) ||
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

    struct FCleanup final : IAutomationLatentCommand
    {
        TSharedPtr<FRun> Run;
        explicit FCleanup(TSharedPtr<FRun> In) : Run(MoveTemp(In)) {}
        bool Update() override
        {
            if (!Run.IsValid()) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return true; }
            for (const FActorReceipt* Receipt :
                {&Run->Fast, &Run->Slow, &Run->Pending})
            {
                auto* Actor = Receipt->Actor.Get();
                if (!IsValid(Actor) || Actor->GetWorld() != World ||
                    Actor->GetClass() != Run->Class) { continue; }
                Close(Actor);
                if (IsValid(Actor) && Actor->GetWorld() == World)
                { Actor->Destroy(); }
            }
            return true;
        }
    };

    bool RunFamily(FAutomationTestBase& Test,
        const FString& Parameters, bool Timed)
    {
        const auto R = MakeShared<FRun>();
        R->Timed = Timed;
        if (!ck::auto_test::net::TryGet_DedicatedServer(
                Parameters, R->Dedicated))
        { Test.AddError(TEXT("unsupported Divergence isolation topology")); return false; }
        R->Class = FSoftClassPath(Timed ? TimedPath : UntimedPath).
            TryLoadClass<AActor>();
        R->StationType = FindObject<UScriptStruct>(nullptr,
            TEXT("/Script/Angelscript.CkGym_Station_TitleAndDescription"));
        if (!Test.TestTrue(TEXT("real Divergence source class and API resolve"),
            R->Class && R->StationType &&
            R->Class->FindFunctionByName(TEXT("Get_SourceRoots")) &&
            R->Class->FindFunctionByName(TEXT("Get_SourcePrimary")) &&
            R->Class->FindFunctionByName(TEXT("Get_SourcePrimaryTarget")) &&
            R->Class->FindFunctionByName(TEXT("Get_SourceRunFinished")) &&
            R->Class->FindFunctionByName(TEXT("Request_CloseSource"))))
        { return false; }

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
            3, TEXT("/Engine/Maps/Entry"), Parameters));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                auto* Server = ck::auto_test::net::Get_ServerWorld();
                if (!IsValid(Server) || Server->GetNetMode() !=
                    (R->Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
                    !Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                        TryGet_TransientEntityForWorld(*Server)))
                { return false; }
                for (int32 I = 0; I != 2; ++I)
                {
                    auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                    if (!IsValid(Client) || Client->GetNetMode() != NM_Client)
                    { return false; }
                }
                return true;
            }), 60.0, TEXT("Divergence server and two clients ready")));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&Test, R](UWorld* World) {
                if (!IsValid(World))
                { Test.AddError(TEXT("Divergence source world missing")); return; }
                auto* Pending = World->SpawnActorDeferred<AActor>(R->Class,
                    FTransform(FVector(-300, 0, 0)));
                if (!Test.TestTrue(TEXT("real pending Divergence actor spawned"),
                    IsValid(Pending))) { return; }
                R->Pending.Actor = Pending;
                Pending->FinishSpawning(FTransform(FVector(-300, 0, 0)));
                FCk_Handle Accepted;
                const bool PendingReceipt = Roots(Pending, R->Pending.Roots) &&
                    R->Pending.Roots.IsEmpty() &&
                    PrimaryGetter(Pending, TEXT("Get_SourcePrimaryTarget"),
                        R->Pending.Primary) &&
                    Allocated(R->Pending.Primary) &&
                    PrimaryGetter(Pending, TEXT("Get_SourcePrimary"), Accepted) &&
                    Accepted == FCk_Handle{} &&
                    Close(Pending);
                Test.TestTrue(TEXT("third source closes its retained pending root before construction"),
                    PendingReceipt);
                if (!PendingReceipt) { Close(Pending); return; }

                R->IsolatedWorld = MakeUnique<ck::FEcsWorld>();
                R->BareStation = UCk_Utils_EntityLifetime_UE::
                    Request_CreateEntity(R->IsolatedWorld->Get_Registry());
                if (!Test.TestTrue(TEXT("bare foreign station has no context or display"),
                    BareUntouched(*R))) { return; }

                auto Spawn = [R, World](const FVector& At, float Seconds,
                    bool Bare, FActorReceipt& Out) {
                    auto* Actor = World->SpawnActorDeferred<AActor>(R->Class,
                        FTransform(At));
                    if (!IsValid(Actor) || !Settle(Actor, Seconds) ||
                        (Bare && !SetStation(Actor, R->BareStation)))
                    { if (IsValid(Actor)) { Actor->Destroy(); } return false; }
                    Out.Actor = Actor;
                    Actor->FinishSpawning(FTransform(At));
                    FCk_Handle Accepted;
                    return Roots(Actor, Out.Roots) && Out.Roots.IsEmpty() &&
                        PrimaryGetter(Actor, TEXT("Get_SourcePrimaryTarget"),
                            Out.Primary) &&
                        Allocated(Out.Primary) &&
                        PrimaryGetter(Actor, TEXT("Get_SourcePrimary"), Accepted) &&
                        (Accepted == FCk_Handle{} || Accepted == Out.Primary);
                };
                const bool Fast = Spawn(FVector(0, 0, 0), 1.5f,
                    false, R->Fast);
                const bool Slow = Spawn(FVector(300, 0, 0), 4.0f,
                    true, R->Slow);
                R->Spawned = Fast && Slow &&
                    R->Fast.Primary != R->Slow.Primary &&
                    R->Fast.Primary != R->Pending.Primary &&
                    R->Slow.Primary != R->Pending.Primary;
                Test.TestTrue(TEXT("two real staggered runs have distinct first-return roots"),
                    R->Spawned);
            })));

        // Fast B and Slow A pass windows overlap; this does not claim both
        // opposing conditions execute simultaneously. Use source receipts.
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                if (!R->Spawned) { return true; }
                auto* World = ck::auto_test::net::Get_ServerWorld();
                TArray<FCk_Handle> Fast, Slow;
                bool FastDone = true, SlowDone = true;
                const bool Overlap = LiveRoots(R->Fast.Actor.Get(), World,
                        2, R->Fast.Primary, Fast) &&
                    LiveRoots(R->Slow.Actor.Get(), World, 1,
                        R->Slow.Primary, Slow) &&
                    Fast[0] != R->Fast.Primary &&
                    Slow[0] != R->Slow.Primary &&
                    R->Fast.Primary != R->Slow.Primary &&
                    Fast[1] != Slow[0] &&
                    HasConfig(R->Fast.Actor.Get(), false, 1) &&
                    HasConfig(R->Slow.Actor.Get(), true, 0) &&
                    Bool(R->Fast.Actor.Get(), TEXT("Get_SourceRunFinished"),
                        FastDone) &&
                    !FastDone &&
                    Bool(R->Slow.Actor.Get(), TEXT("Get_SourceRunFinished"),
                        SlowDone) && !SlowDone;
                if (Overlap)
                { R->Fast.Roots = Fast; R->Slow.Roots = Slow;
                  R->OverlapSeen = true; }
                return Overlap;
            }), 15.0, TEXT("real Fast B and Slow A pass windows overlap")));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&Test, R](UWorld* World) {
                TArray<FCk_Handle> Fast, Slow;
                const bool Actual = R->OverlapSeen &&
                    LiveRoots(R->Fast.Actor.Get(), World, 2,
                        R->Fast.Primary, Fast) &&
                    LiveRoots(R->Slow.Actor.Get(), World, 1,
                        R->Slow.Primary, Slow) &&
                    Fast == R->Fast.Roots && Slow == R->Slow.Roots &&
                    BareUntouched(*R) && PendingInert(*R);
                Test.TestTrue(TEXT("opposed configurations retain separate sources during overlapping pass windows"),
                    Actual);
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                if (!R->Spawned) { return true; }
                bool Fast = false, Slow = false;
                return Bool(R->Fast.Actor.Get(),
                        TEXT("Get_SourceRunFinished"), Fast) && Fast &&
                    Bool(R->Slow.Actor.Get(),
                        TEXT("Get_SourceRunFinished"), Slow) && Slow;
            }), 20.0, TEXT("both actual B snapshots committed")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                return !R->Spawned || PassiveClients(*R);
            }), 15.0, TEXT("two exact Iris source replicas reach each client")));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                if (!R->Spawned) { return true; }
                auto* World = ck::auto_test::net::Get_ServerWorld();
                TArray<FCk_Handle> Fast, Slow;
                if (!LiveRoots(R->Fast.Actor.Get(), World, 2,
                        R->Fast.Primary, Fast) ||
                    !LiveRoots(R->Slow.Actor.Get(), World, 2,
                        R->Slow.Primary, Slow) ||
                    Fast != R->Fast.Roots || R->Slow.Roots.Num() != 1 ||
                    Slow[0] != R->Slow.Roots[0] ||
                    Slow[1] == R->Fast.Primary ||
                    Slow[1] == R->Pending.Primary ||
                    Fast.Contains(Slow[1]))
                { return false; }
                FActorReceipt FastCandidate = R->Fast;
                FActorReceipt SlowCandidate = R->Slow;
                SlowCandidate.Roots = Slow;
                if (!CaptureForest(World, FastCandidate) ||
                    !CaptureForest(World, SlowCandidate))
                { return false; }
                R->Fast.Forest = MoveTemp(FastCandidate.Forest);
                R->Slow.Roots = MoveTemp(SlowCandidate.Roots);
                R->Slow.Forest = MoveTemp(SlowCandidate.Forest);
                return true;
            }), 15.0, TEXT("both completed SM forests physically settle")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&Test, R](UWorld* World) {
                TArray<FCk_Handle> Fast, Slow;
                const bool Live = R->OverlapSeen &&
                    LiveRoots(R->Fast.Actor.Get(), World, 2,
                        R->Fast.Primary, Fast) &&
                    LiveRoots(R->Slow.Actor.Get(), World, 2,
                        R->Slow.Primary, Slow) &&
                    Fast == R->Fast.Roots &&
                    Slow[0] == R->Slow.Roots[0] &&
                    Slow[1] != Fast[0] && Slow[1] != Fast[1] &&
                    Slow[1] != R->Pending.Primary &&
                    PendingInert(*R) && BareUntouched(*R) &&
                    PassiveClients(*R);
                Test.TestTrue(TEXT("both actors retain their own A/B roots and passive replicas"),
                    Live);
                if (!Live) { return; }
                R->Slow.Roots = Slow;
                const bool Read = Snapshots(R->Fast.Actor.Get(),
                        R->Fast.FinalSnapshots) &&
                    Snapshots(R->Slow.Actor.Get(), R->Slow.FinalSnapshots);
                Test.TestTrue(TEXT("all twelve A/B snapshot fields readable on both real actors"),
                    Read);
                if (Read)
                {
                    for (int32 I = 0; I != 12; ++I)
                    {
                        Test.TestEqual(FString::Printf(TEXT("Fast %s"),
                                SnapshotNames[I]),
                            R->Fast.FinalSnapshots[I], Expected[I]);
                        Test.TestEqual(FString::Printf(TEXT("Slow %s"),
                                SnapshotNames[I]),
                            R->Slow.FinalSnapshots[I], Expected[I]);
                    }
                }
                const auto PriorFastForest = R->Fast.Forest;
                const auto PriorSlowForest = R->Slow.Forest;
                TArray<FCk_Handle> Rejected{R->Fast.Primary};
                TArray<FCk_Handle> RejectedParents{R->Fast.Primary};
                const bool DefaultRejected =
                    !UCk_StateMachineGym_Runtime_UE::TryCollect_DivergenceHealthyForest(
                        {}, R->Fast.Actor.Get(), R->Fast.Roots,
                        Rejected, RejectedParents) &&
                    Rejected.IsEmpty() && RejectedParents.IsEmpty();
                Rejected.Add(R->Fast.Primary);
                RejectedParents.Add(R->Fast.Primary);
                const bool ForeignRejected =
                    !UCk_StateMachineGym_Runtime_UE::TryCollect_DivergenceHealthyForest(
                        R->Slow.Primary, R->Fast.Actor.Get(), R->Fast.Roots,
                        Rejected, RejectedParents) &&
                    Rejected.IsEmpty() && RejectedParents.IsEmpty();
                Rejected.Add(R->Fast.Primary);
                const bool AliasRejected =
                    !UCk_StateMachineGym_Runtime_UE::TryCollect_DivergenceHealthyForest(
                        R->Fast.Primary, R->Fast.Actor.Get(), R->Fast.Roots,
                        Rejected, Rejected) && Rejected.IsEmpty();
                Test.TestTrue(TEXT("invalid and aliased healthy-forest queries reject without publication"),
                    DefaultRejected && ForeignRejected && AliasRejected &&
                    R->Fast.Forest == PriorFastForest &&
                    R->Slow.Forest == PriorSlowForest);
                FActorReceipt FastCandidate = R->Fast;
                FActorReceipt SlowCandidate = R->Slow;
                const bool Forests = CaptureForest(World, FastCandidate) &&
                    CaptureForest(World, SlowCandidate);
                Test.TestTrue(TEXT("two complete primary-owned source forests captured before closure"),
                    Forests);
                if (Forests)
                {
                    R->Fast.Forest = MoveTemp(FastCandidate.Forest);
                    R->Slow.Forest = MoveTemp(SlowCandidate.Forest);
                }
                R->FastClosed = Forests && Close(R->Fast.Actor.Get());
                Test.TestTrue(TEXT("Fast source closes with A and B retained together"),
                    R->FastClosed);
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                return !R->FastClosed || (ForestGone(R->Fast) &&
                    !IsValid(R->Fast.Actor.Get()));
            }), 15.0, TEXT("Fast A/B forest and owning actor physically retire")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&Test, R](UWorld* World) {
                TArray<FCk_Handle> Slow;
                TArray<int32> Values;
                bool Finished = false;
                const bool Unchanged = R->FastClosed &&
                    LiveRoots(R->Slow.Actor.Get(), World, 2,
                        R->Slow.Primary, Slow) &&
                    Slow == R->Slow.Roots &&
                    Bool(R->Slow.Actor.Get(),
                        TEXT("Get_SourceRunFinished"), Finished) && Finished &&
                    Snapshots(R->Slow.Actor.Get(), Values) &&
                    Values == R->Slow.FinalSnapshots &&
                    PendingInert(*R) && BareUntouched(*R);
                Test.TestTrue(TEXT("Slow B identity and all twelve counts survive Fast retirement"),
                    Unchanged);
                auto* const SlowActor = R->Slow.Actor.Get();
                R->SlowClosed = Unchanged && IsValid(SlowActor) &&
                    SlowActor->Destroy();
                Test.TestTrue(TEXT("Slow actor EndPlay retires its healthy source after peer retirement"),
                    R->SlowClosed);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                return !R->SlowClosed || (ForestGone(R->Fast) &&
                    ForestGone(R->Slow) &&
                    CountActors(ck::auto_test::net::Get_ServerWorld(),
                        R->Class) == 1 && IsValid(R->Pending.Actor.Get()));
            }), 15.0, TEXT("Slow A/B forest retires; pending actor remains inert")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&Test, R](UWorld*) {
                Test.TestTrue(TEXT("closed pending actor had no late source callbacks"),
                    PendingInert(*R));
                auto* Actor = R->Pending.Actor.Get();
                Test.TestTrue(TEXT("retained pending actor explicitly destroys"),
                    IsValid(Actor) && Actor->Destroy());
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                if (!ForestGone(R->Fast) || !ForestGone(R->Slow) ||
                    !R->Pending.Roots.IsEmpty() ||
                    Allocated(R->Pending.Primary)) { return false; }
                for (UWorld* World : {ck::auto_test::net::Get_ServerWorld(),
                    ck::auto_test::net::Get_ClientWorld(0),
                    ck::auto_test::net::Get_ClientWorld(1)})
                { if (CountActors(World, R->Class) != 0) { return false; } }
                return true;
            }), 15.0, TEXT("all three actors and every retained forest physically gone")));
        ADD_LATENT_AUTOMATION_COMMAND(FCleanup(R));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkStateMachineGym_DivergenceUntimedIsolation,
    "Ck.StateMachine.Gym.DivergenceUntimedIsolation.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineGym_DivergenceUntimedIsolation::RunTest(
    const FString& Parameters)
{ return ck_sm_divergence_isolation_test::RunFamily(*this, Parameters, false); }

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkStateMachineGym_DivergenceTimedIsolation,
    "Ck.StateMachine.Gym.DivergenceTimedIsolation.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineGym_DivergenceTimedIsolation::RunTest(
    const FString& Parameters)
{ return ck_sm_divergence_isolation_test::RunFamily(*this, Parameters, true); }

#endif
