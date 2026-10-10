#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEntityScriptGym_Runtime.h"
#include "CkStateMachineGym_Runtime.h"
#include "CkStateMachineGym_RetirementProbe.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_sm_divergence_retirement_callback_test
{
    constexpr auto SourcePath =
        TEXT("/Script/Angelscript.Ck_SmTest_DivergenceFirstBranch_GymActor");

    struct FRun
    {
        bool Dedicated = false;
        bool Spawned = false;
        bool Captured = false;
        bool Armed = false;
        bool Closed = false;
        bool Failed = false;
        UClass* SourceClass = nullptr;
        TWeakObjectPtr<AActor> Actor;
        TStrongObjectPtr<UCkStateMachineGym_RetirementProbe> Probe;
        FCk_Handle Primary;
        FCk_Handle Unknown;
        TArray<FCk_Handle> Passes;
        TArray<FCk_Handle> Forest;
        TArray<FCk_Handle> Parents;
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

    bool ValidReturn(const UFunction* Function, const FProperty* Property)
    {
        if (!IsValid(Function) || !Property || Function->NumParms != 1 ||
            !Property->HasAnyPropertyFlags(CPF_ReturnParm))
        { return false; }
        const int32 Offset = Property->GetOffset_ForUFunction();
        const int32 Size = Property->GetSize();
        return Offset >= 0 && Size > 0 && Offset <= Function->ParmsSize &&
            Size <= Function->ParmsSize - Offset;
    }

    bool Bool(AActor* Actor, const TCHAR* Name, bool& Out)
    {
        Out = false;
        FCall C(Actor, Name);
        auto* P = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        if (!ValidReturn(C.Function, P) || !C.Send()) { return false; }
        Out = P->GetPropertyValue_InContainer(C.Args->GetStructMemory());
        return true;
    }

    bool Handle(AActor* Actor, const TCHAR* Name, FCk_Handle& Out)
    {
        Out = {};
        FCall C(Actor, Name);
        auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!ValidReturn(C.Function, P) ||
            P->Struct != FCk_Handle::StaticStruct() ||
            P->GetSize() != sizeof(FCk_Handle) || !C.Send())
        { return false; }
        const auto* Value = P->ContainerPtrToValuePtr<FCk_Handle>(
            C.Args->GetStructMemory());
        if (!Value) { return false; }
        Out = *Value;
        return true;
    }

    bool Roots(AActor* Actor, TArray<FCk_Handle>& Out)
    {
        Out.Reset();
        FCall C(Actor, TEXT("Get_SourceRoots"));
        auto* P = Field<FArrayProperty>(C.Function, TEXT("ReturnValue"));
        auto* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
        if (!ValidReturn(C.Function, P) || !Inner ||
            Inner->Struct != FCk_Handle::StaticStruct() ||
            Inner->GetSize() != sizeof(FCk_Handle) || !C.Send())
        { return false; }
        FScriptArrayHelper Values(P,
            P->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory()));
        TArray<FCk_Handle> Candidate;
        for (int32 I = 0; I < Values.Num(); ++I)
        { Candidate.Add(*reinterpret_cast<FCk_Handle*>(Values.GetRawPtr(I))); }
        Out = MoveTemp(Candidate);
        return true;
    }

    bool Close(AActor* Actor)
    {
        FCall C(Actor, TEXT("Request_CloseSource"));
        return C.Function && C.Function->NumParms == 0 && C.Send();
    }

    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    bool LiveNotBegun(const FCk_Handle& H)
    {
        return Allocated(H) && ck::IsValid(H) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(H,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    bool TryCaptureSettled(UWorld* World, FRun& Run)
    {
        auto* Actor = Run.Actor.Get();
        if (!Run.Spawned || !IsValid(World) || !IsValid(Actor) ||
            Actor->GetWorld() != World || Actor->GetClass() != Run.SourceClass)
        { return false; }
        bool Finished = false, Closed = true;
        FCk_Handle Primary, Target;
        TArray<FCk_Handle> Passes, Forest, Parents;
        if (!Bool(Actor, TEXT("Get_SourceRunFinished"), Finished) || !Finished ||
            !Bool(Actor, TEXT("Get_SourceClosed"), Closed) || Closed ||
            !Handle(Actor, TEXT("Get_SourcePrimaryTarget"), Target) ||
            !Handle(Actor, TEXT("Get_SourcePrimary"), Primary) ||
            Primary != Target || !Roots(Actor, Passes) || Passes.Num() != 2 ||
            !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePrimary(
                Primary, Actor))
        { return false; }
        for (const auto& Pass : Passes)
        { if (!UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePass(
                Pass, Primary)) { return false; } }
        if (!UCk_StateMachineGym_Runtime_UE::TryCollect_DivergenceHealthyForest(
                Primary, Actor, Passes, Forest, Parents) ||
            Forest.Num() < 3 || Forest.Last() != Primary ||
            Parents.Num() != Forest.Num() || !LiveNotBegun(Forest[0]))
        { return false; }
        Run.Primary = Primary;
        Run.Passes = MoveTemp(Passes);
        Run.Forest = MoveTemp(Forest);
        Run.Parents = MoveTemp(Parents);
        Run.Captured = true;
        return true;
    }

    bool UnchangedAfterFault(const FRun& Run)
    {
        auto* Probe = Run.Probe.Get();
        auto* Actor = Run.Actor.Get();
        bool Closed = false;
        if (!Run.Closed || !Run.Captured || Run.Forest.Num() < 3 ||
            !IsValid(Probe) || Probe->Get_Failed() ||
            Probe->Get_CallbackCount() != 1 ||
            Probe->Get_UnknownChild() != Run.Unknown ||
            !IsValid(Actor) || !Bool(Actor, TEXT("Get_SourceClosed"), Closed) ||
            !Closed || !LiveNotBegun(Run.Primary) ||
            !LiveNotBegun(Run.Unknown) ||
            !Run.Unknown.Has<ck::FFragment_LifetimeOwner>() ||
            Run.Unknown.Get<ck::FFragment_LifetimeOwner>().Get_Entity() !=
                Run.Primary)
        { return false; }
        // The first request may have retired its leaf. No later frozen leaf
        // may be initiated after the callback inserted an unknown direct child.
        for (int32 I = 1; I < Run.Forest.Num(); ++I)
        { if (!LiveNotBegun(Run.Forest[I])) { return false; } }
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

    struct FRequestUnknownCleanup final : IAutomationLatentCommand
    {
        TSharedPtr<FRun> Run;
        explicit FRequestUnknownCleanup(TSharedPtr<FRun> In)
            : Run(MoveTemp(In)) {}
        bool Update() override
        {
            if (!Run.IsValid()) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            auto Unknown = Run->Unknown;
            if (!Allocated(Unknown) && IsValid(Run->Probe.Get()))
            { Unknown = Run->Probe->Get_UnknownChild(); }
            if (IsValid(World) && LiveNotBegun(Unknown) &&
                LiveNotBegun(Run->Primary) &&
                Unknown.Has<ck::FFragment_LifetimeOwner>() &&
                Unknown.Get<ck::FFragment_LifetimeOwner>().Get_Entity() ==
                    Run->Primary)
            {
                Run->Unknown = Unknown;
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Unknown);
            }
            return true;
        }
    };

    bool RunCase(FAutomationTestBase& Test, const FString& Parameters)
    {
        const auto R = MakeShared<FRun>();
        if (!ck::auto_test::net::TryGet_DedicatedServer(
                Parameters, R->Dedicated))
        { Test.AddError(TEXT("unsupported Divergence retirement topology")); return false; }
        R->SourceClass = FSoftClassPath(SourcePath).TryLoadClass<AActor>();
        if (!Test.TestTrue(TEXT("real untimed source and reflected closure exist"),
            IsValid(R->SourceClass) &&
            R->SourceClass->FindFunctionByName(TEXT("Get_SourceRoots")) &&
            R->SourceClass->FindFunctionByName(TEXT("Get_SourcePrimary")) &&
            R->SourceClass->FindFunctionByName(TEXT("Request_CloseSource"))))
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
            }), 60.0, TEXT("Divergence retirement three worlds ready")));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&Test, R](UWorld* World) {
                auto* Actor = IsValid(World) ? World->SpawnActorDeferred<AActor>(
                    R->SourceClass, FTransform::Identity) : nullptr;
                R->Spawned = IsValid(Actor);
                Test.TestTrue(TEXT("real untimed source actor spawned"), R->Spawned);
                if (!R->Spawned) { R->Failed = true; return; }
                R->Actor = Actor;
                Actor->FinishSpawning(FTransform::Identity);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                return R->Failed || TryCaptureSettled(
                    ck::auto_test::net::Get_ServerWorld(), *R);
            }), 25.0, TEXT("real source finishes and full primary forest settles")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&Test, R](UWorld* World) {
                Test.TestTrue(TEXT("real source forest captured before callback"),
                    R->Captured);
                if (!R->Captured) { R->Failed = true; return; }
                R->Probe = TStrongObjectPtr<UCkStateMachineGym_RetirementProbe>(
                    NewObject<UCkStateMachineGym_RetirementProbe>(
                        GetTransientPackage()));
                R->Armed = IsValid(R->Probe.Get()) &&
                    R->Probe->Arm(World, R->Actor.Get(), R->Primary,
                        R->Forest[0]);
                Test.TestTrue(TEXT("real first leaf BeginDestroy probe armed"),
                    R->Armed);
                if (!R->Armed) { R->Failed = true; return; }
                R->Closed = Close(R->Actor.Get());
                Test.TestTrue(TEXT("source closure invoked once with real probe"),
                    R->Closed);
                if (!R->Closed) { R->Failed = true; }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                return R->Failed || (R->Armed && IsValid(R->Probe.Get()) &&
                    R->Probe->Get_CallbackCount() == 1);
            }), 15.0, TEXT("real first-leaf BeginDestroy callback creates unknown child")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&Test, R](UWorld*) {
                if (!R->Failed && IsValid(R->Probe.Get()))
                { R->Unknown = R->Probe->Get_UnknownChild(); }
                const bool Before = !R->Failed && UnchangedAfterFault(*R);
                Test.TestTrue(TEXT("unknown direct child and uninitiated primary/later leaves survive failed frozen proof"),
                    Before);
                if (!Before) { R->Failed = true; return; }
                const bool ClosedAgain = Close(R->Actor.Get());
                Test.TestTrue(TEXT("second close is a terminal no-op"),
                    ClosedAgain && UnchangedAfterFault(*R));
                if (!ClosedAgain || !UnchangedAfterFault(*R))
                { R->Failed = true; }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FRequestUnknownCleanup(R));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                return !Allocated(R->Unknown);
            }), 15.0, TEXT("retained unknown child physically retired before actor cleanup")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&Test, R](UWorld* World) {
                auto* Actor = R->Actor.Get();
                const bool Exact = IsValid(World) && IsValid(Actor) &&
                    Actor->GetWorld() == World &&
                    Actor->GetClass() == R->SourceClass &&
                    !Allocated(R->Unknown);
                Test.TestTrue(TEXT("source actor cleanup starts after unknown child is gone"),
                    Exact);
                if (Exact) { Test.TestTrue(TEXT("source actor explicitly destroys"),
                    Actor->Destroy()); }
                else { R->Failed = true; }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([R]() {
                for (const auto& H : R->Forest)
                { if (Allocated(H)) { return false; } }
                for (UWorld* World : {ck::auto_test::net::Get_ServerWorld(),
                    ck::auto_test::net::Get_ClientWorld(0),
                    ck::auto_test::net::Get_ClientWorld(1)})
                { if (CountActors(World, R->SourceClass) != 0) { return false; } }
                return true;
            }), 20.0, TEXT("owned unknown, frozen forest and source actors physically gone")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&Test, R](UWorld*) {
                Test.TestFalse(TEXT("retirement callback proof completed without fixture failure"),
                    R->Failed);
                bool Gone = !Allocated(R->Unknown) && !R->Forest.IsEmpty();
                for (const auto& H : R->Forest) { Gone &= !Allocated(H); }
                Test.TestTrue(TEXT("full retained forest physically absent after explicit cleanup"),
                    Gone);
                R->Probe.Reset();
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkStateMachineGym_DivergenceRetirementCallback,
    "Ck.StateMachine.Gym.DivergenceRetirementCallback.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineGym_DivergenceRetirementCallback::RunTest(
    const FString& Parameters)
{ return ck_sm_divergence_retirement_callback_test::RunCase(*this, Parameters); }

#endif
