#include "CoreMinimal.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "../CkTest_CompletionListener.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "Engine/World.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace ck_station_carrier_parent_test
{
    auto Allocated(const FCk_Handle& H) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    auto Live(const FCk_Handle& H) -> bool
    {
        return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
            H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    struct FRun
    {
        TStrongObjectPtr<UCk_Test_CompletionListener_UE> Listener;
        FCk_Handle World;
        FCk_Handle Outer;
        FCk_Handle Owner;
        FCk_Handle DirectOwner;
        FCk_Handle NestedTarget;
        FCk_Handle DirectTarget;
        FCk_Handle NestedCarrier;
        FCk_Handle DirectCarrier;
        bool CleanupRejected = false;

        bool RequestCleanup()
        {
            if (!Live(World))
            { return Gone(); }
            TArray<FCk_Handle> Frozen;
            for (const auto& H : {Outer, Owner, NestedTarget, NestedCarrier,
                                  DirectOwner, DirectTarget, DirectCarrier})
            { if (Allocated(H)) { Frozen.Add(H); } }
            if (!Frozen.IsEmpty() &&
                !UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
                    Frozen, Frozen, World))
            { CleanupRejected = true; return false; }
            if (Allocated(Outer))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Outer); }
            if (Allocated(DirectOwner))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(DirectOwner); }
            return true;
        }
        bool Gone() const
        {
            return !Allocated(Outer) && !Allocated(Owner) && !Allocated(DirectOwner) &&
                !Allocated(NestedTarget) && !Allocated(DirectTarget) &&
                !Allocated(NestedCarrier) && !Allocated(DirectCarrier);
        }
    };

    auto ExactOneCarrier(const FCk_Handle& Target, const FCk_Handle& World,
                         const FCk_Handle& Carrier) -> bool
    {
        if (!Live(Target) || !Target.Has<ck::FFragment_LifetimeDependents>())
        { return false; }
        const auto& Raw = Target.Get<ck::FFragment_LifetimeDependents>().Get_Entities();
        TArray<FCk_Handle> Current;
        return Raw.Num() == 1 && Raw[0] == Carrier &&
            UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                Target, World, Current) && Current.Num() == 1 && Current[0] == Carrier &&
            Live(Carrier) && Carrier.Has<ck::FFragment_EntityScript_RequestSpawnEntity>();
    }

    struct FRequestWitness
    {
        FCk_Handle Target;
        FCk_Handle Owner;
        FCk_Handle ContextOwner;
        TWeakObjectPtr<UCk_EntityScript_UE> Archetype;
        FInstancedStruct Params;
    };

    auto ReadRequest(const FCk_Handle& Carrier, FRequestWitness& Out) -> bool
    {
        Out = {};
        if (!Live(Carrier) ||
            !Carrier.Has<ck::FFragment_EntityScript_RequestSpawnEntity>())
        { return false; }
        const auto& Request = Carrier.Get<ck::FFragment_EntityScript_RequestSpawnEntity>();
        Out.Target = Request.Get_NewEntity();
        Out.Owner = Request.Get_Owner();
        Out.ContextOwner = Request.Get_ContextOwner();
        Out.Archetype = Request.Get_EntityScriptClassArchetype();
        Out.Params = Request.Get_SpawnParams();
        return Out.Params.IsValid();
    }

    auto SameRequest(const FCk_Handle& Carrier, const FRequestWitness& Before) -> bool
    {
        FRequestWitness Now;
        return ReadRequest(Carrier, Now) && Now.Target == Before.Target &&
            Now.Owner == Before.Owner && Now.ContextOwner == Before.ContextOwner &&
            Now.Archetype == Before.Archetype && Now.Params == Before.Params;
    }

    auto SnapshotRaw(const TArray<FCk_Handle>& Roots,
                     TArray<TArray<FCk_Handle>>& Out) -> bool
    {
        Out.Reset();
        for (const auto& H : Roots)
        {
            if (!Live(H) || !H.Has<ck::FFragment_LifetimeDependents>())
            { Out.Reset(); return false; }
            Out.Add(H.Get<ck::FFragment_LifetimeDependents>().Get_Entities());
        }
        return Out.Num() == Roots.Num();
    }

    auto RejectCapture(const FCk_Handle& Target, const FCk_Handle& Owner,
                       TSubclassOf<UCk_EntityScript_UE> Class,
                       const FCk_Handle& World, const FCk_Handle& OwnerParent,
                       const FCk_Handle& ExistingCarrier) -> bool
    {
        FCk_Handle Out = ExistingCarrier;
        return !UCk_EntityScriptGym_Spawn_Runtime_UE::TryCapture_StationSpawnCarrierWithParent(
            Target, Owner, Class, World, OwnerParent, Out) && Out == FCk_Handle{};
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkStationCarrier_ExplicitParent,
    "Ck.RotateTowardsGym.Network.Admission.CarrierParent.Production",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkStationCarrier_ExplicitParent::RunTest(const FString& Parameters)
{
    using namespace ck_station_carrier_parent_test;
    const auto Class = FSoftClassPath{
        TEXT("/Script/Angelscript.Ck_EntityScript_GymStation")}.TryLoadClass<UCk_EntityScript_UE>();
    auto* ParamsType = FindObject<UScriptStruct>(nullptr,
        TEXT("/Script/Angelscript.Ck_GymStation_SpawnParams"));
    if (!TestTrue(TEXT("actual authored station class and typed spawn payload"),
        IsValid(Class) && IsValid(ParamsType))) { return false; }
    FInstancedStruct Params;
    Params.InitializeAs(ParamsType);
    if (!TestTrue(TEXT("station payload is a real typed value"), Params.IsValid()))
    { return false; }
    const auto Run = MakeShared<FRun>();
    Run->Listener = TStrongObjectPtr<UCk_Test_CompletionListener_UE>{
        NewObject<UCk_Test_CompletionListener_UE>(GetTransientPackage())};
    if (!TestNotNull(TEXT("real completion observer"), Run->Listener.Get()))
    { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Class, Params](UWorld* W)
    {
        auto Fail = [this](const TCHAR* Reason)
        { AddError(Reason); ck::auto_test::net::FailSession(Reason); };
        if (!IsValid(W) || !W->GetAuthGameMode())
        { Fail(TEXT("actual authority PIE world unavailable")); return; }
        Run->World = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
        if (!Live(Run->World))
        { Fail(TEXT("actual world transient root unavailable")); return; }
        Run->Outer = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run->World);
        if (!Live(Run->Outer))
        { Fail(TEXT("real nested parent not created")); Run->RequestCleanup(); return; }
        Run->Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run->Outer);
        Run->DirectOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run->World);
        if (!Live(Run->Owner) || !Live(Run->DirectOwner))
        { Fail(TEXT("real nested and direct owners not created")); Run->RequestCleanup(); return; }
        auto D = FCk_Delegate_Request_OnCompleted{};
        D.BindDynamic(Run->Listener.Get(), &UCk_Test_CompletionListener_UE::OnRequestCompleted);
        const auto NestedPending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
            Run->Owner, Class, Params, D);
        const auto DirectPending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
            Run->DirectOwner, Class, Params, D);
        if (ck::IsValid(NestedPending))
        { Run->NestedTarget = NestedPending.Get_EntityUnderConstruction(); }
        if (ck::IsValid(DirectPending))
        { Run->DirectTarget = DirectPending.Get_EntityUnderConstruction(); }
        bool Good = ck::IsValid(NestedPending) && ck::IsValid(DirectPending) &&
            Live(Run->NestedTarget) && Live(Run->DirectTarget) &&
            Run->Listener->_TimesRequestCompleted == 0;
        FCk_Handle NestedCarrier, DirectCarrier;
        if (Good)
        {
            Good = UCk_EntityScriptGym_Spawn_Runtime_UE::TryCapture_StationSpawnCarrierWithParent(
                Run->NestedTarget, Run->Owner, Class, Run->World, Run->Outer, NestedCarrier) &&
                UCk_EntityScriptGym_Spawn_Runtime_UE::TryCapture_StationSpawnCarrier(
                    Run->DirectTarget, Run->DirectOwner, Class, Run->World, DirectCarrier);
        }
        Run->NestedCarrier = NestedCarrier;
        Run->DirectCarrier = DirectCarrier;
        if (Good)
        {
            Good = NestedCarrier != DirectCarrier &&
                ExactOneCarrier(Run->NestedTarget, Run->World, NestedCarrier) &&
                ExactOneCarrier(Run->DirectTarget, Run->World, DirectCarrier);
        }
        const TArray<FCk_Handle> RawRoots{Run->Outer, Run->Owner,
            Run->NestedTarget, Run->DirectOwner, Run->DirectTarget};
        TArray<TArray<FCk_Handle>> BeforeRaw;
        FRequestWitness BeforeNested, BeforeDirect;
        if (Good)
        {
            Good = SnapshotRaw(RawRoots, BeforeRaw) &&
                ReadRequest(NestedCarrier, BeforeNested) &&
                ReadRequest(DirectCarrier, BeforeDirect) &&
                BeforeNested.Target == Run->NestedTarget && BeforeNested.Owner == Run->Owner &&
                BeforeNested.ContextOwner == Run->Owner &&
                BeforeDirect.Target == Run->DirectTarget && BeforeDirect.Owner == Run->DirectOwner &&
                BeforeDirect.ContextOwner == Run->DirectOwner;
        }
        if (Good)
        {
            // The parent input and carrier output may alias; the pure helper
            // must copy the parent before clearing its output.
            FCk_Handle ParentAndOutput = Run->Outer;
            Good = UCk_EntityScriptGym_Spawn_Runtime_UE::TryCapture_StationSpawnCarrierWithParent(
                Run->NestedTarget, Run->Owner, Class, Run->World,
                ParentAndOutput, ParentAndOutput) && ParentAndOutput == NestedCarrier;
        }
        if (Good)
        {
            FCk_Handle Rejected = NestedCarrier;
            bool AllocatedOutput = true;
            Good = !UCk_EntityScriptGym_Spawn_Runtime_UE::TryCapture_StationSpawnCarrier(
                    Run->NestedTarget, Run->Owner, Class, Run->World, Rejected) &&
                Rejected == FCk_Handle{} &&
                RejectCapture(Run->NestedTarget, Run->Owner, Class, Run->World,
                    Run->World, NestedCarrier) &&
                RejectCapture(Run->NestedTarget, Run->Owner, Class, Run->World,
                    FCk_Handle{}, NestedCarrier) &&
                RejectCapture(Run->NestedTarget, Run->Owner, Class, Run->World,
                    Run->Owner, NestedCarrier) &&
                UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrierWithParent(
                    NestedCarrier, Run->NestedTarget, Run->Owner, Run->World, Run->Outer,
                    AllocatedOutput) && AllocatedOutput;
            AllocatedOutput = true;
            Good = Good && !UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrier(
                    NestedCarrier, Run->NestedTarget, Run->Owner, Run->World, AllocatedOutput) &&
                !AllocatedOutput;
            AllocatedOutput = true;
            Good = Good && !UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrierWithParent(
                    NestedCarrier, Run->NestedTarget, Run->Owner, Run->World, Run->World,
                    AllocatedOutput) && !AllocatedOutput;
            AllocatedOutput = true;
            Good = Good && UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrier(
                    DirectCarrier, Run->DirectTarget, Run->DirectOwner, Run->World,
                    AllocatedOutput) && AllocatedOutput;
        }
        if (Good)
        {
            auto* ForeignWorld = ck::auto_test::net::Get_ClientWorld(0);
            const auto ForeignRoot = IsValid(ForeignWorld)
                ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*ForeignWorld)
                : FCk_Handle{};
            bool AllocatedOutput = true;
            Good = Live(ForeignRoot) && ForeignRoot != Run->World &&
                RejectCapture(Run->NestedTarget, Run->Owner, Class, Run->World,
                    ForeignRoot, NestedCarrier) &&
                !UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrierWithParent(
                    NestedCarrier, Run->NestedTarget, Run->Owner, Run->World, ForeignRoot,
                    AllocatedOutput) && !AllocatedOutput;
        }
        Good = Good && Run->Listener->_TimesRequestCompleted == 0 &&
            ExactOneCarrier(Run->NestedTarget, Run->World, NestedCarrier) &&
            ExactOneCarrier(Run->DirectTarget, Run->World, DirectCarrier) &&
            SameRequest(NestedCarrier, BeforeNested) &&
            SameRequest(DirectCarrier, BeforeDirect);
        if (Good)
        {
            TArray<TArray<FCk_Handle>> AfterRaw;
            Good = SnapshotRaw(RawRoots, AfterRaw) && AfterRaw == BeforeRaw;
        }
        if (!TestTrue(TEXT("real nested and direct station carriers: exact parent, strict wrapper, malformed rejection and pure readback"), Good))
        { Fail(TEXT("station carrier parent regression rejected")); }
        if (!Run->RequestCleanup())
        { Fail(TEXT("station carrier cleanup closure is unproven; left intact for PIE disposal")); }
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { return Run->CleanupRejected || Run->Gone(); }),
        15.0, TEXT("both real pending station request trees fully retire")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
#endif
