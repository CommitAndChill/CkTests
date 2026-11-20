#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEntityTag/CkEntityTag_Fragment_Data.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkTests/Snapshot/CkAutoTest_Snapshot_RendezvousProbe.h"
#include "CkTests/Snapshot/CkSnapshot_TestHarness_Common.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace ck_autotest_pending_bridge_rows
{
    using FActor = ACk_AutoTest_Snapshot_BridgeRendezvousActor_UE;
    using FObservation = ck::FFragment_AutoTest_RendezvousObservation;
    using EDisposition = ck::persistence::EInitializationDisposition;
    constexpr int32 NumSaved = 4;

    struct FRun
    {
        TWeakObjectPtr<UWorld> FreshWorld;
        TWeakObjectPtr<FActor> FreshActor;
        uint64 PreviousEpoch = 0;
    };

    auto Mutation(int32 InSlot) -> FName
    {
        return FName{*FString::Printf(TEXT("Rendezvous.SavedSlot%d"), InSlot)};
    }

    auto IsConstructed(const FCk_Handle& InEntity) -> bool
    {
        return ck::IsValid(InEntity) && InEntity.Has<FObservation>() &&
            InEntity.Get<FObservation>().ConstructedCallbackRan;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Snapshot_Initialization_PendingBridgeRows,
    "Ck.Snapshot.Initialization.PendingBridgeRows", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCk_Snapshot_Initialization_PendingBridgeRows::RunTest(const FString&)
{
    using namespace ck_autotest_pending_bridge_rows;
    using namespace ck::auto_test::snapshot;
    const auto Run = MakeShared<FRun>();
    FCk_SnapshotRoundTrip_Spec Spec;
    Spec.NumPIEClients = 1;
    Spec.NumCycles = 2;
    // Exercise the same saved rows in two different registries. The fresh witness added
    // after each load must never become an input to the next one.
    Spec.SaveEveryCycle = false;
    Spec.SlotName = FName{TEXT("CkTest_Initialization_PendingBridgeRows")};
    Spec.Spawn = FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
    {
        for (int32 Slot = 0; Slot < NumSaved; ++Slot)
        {
            auto* Actor = World->SpawnActorDeferred<FActor>(
                FActor::StaticClass(), FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (!TestNotNull(TEXT("Spawned a runtime actor of the common recipe"), Actor)) { return; }
            Actor->RestoreProbeSlot = Slot;
            Actor->FinishSpawning(FTransform::Identity);
        }
    });
    Spec.SubjectReady = FCk_NetAutoTest_Assertion::CreateLambda([]()
    {
        auto* World = Get_PostTravelServerWorld();
        if (!World) { return false; }
        int32 Count = 0;
        for (TActorIterator<FActor> It(World); It; ++It)
        {
            if (!IsConstructed(UCk_Utils_EntityBridge_UE::TryGet_Entity(*It))) { return false; }
            ++Count;
        }
        return Count == NumSaved;
    });
    Spec.Mutate = FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
    {
        for (TActorIterator<FActor> It(World); It; ++It)
        {
            auto Entity = UCk_Utils_EntityBridge_UE::TryGet_Entity(*It);
            if (!TestTrue(TEXT("Original bridged actor is constructed"), IsConstructed(Entity))) { return; }
            TestTrue(TEXT("Source construction is fresh"),
                Entity.Get<FObservation>().DuringConstruction.Get_Disposition() == EDisposition::Fresh);
            UCk_Utils_EntityTag_UE::Add(Entity, Mutation(It->RestoreProbeSlot));
            int32 SavedChildren = 0;
            for (auto Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Entity))
            {
                if (ck::Is_NOT_Valid(Child) || !Child.Has<FObservation>()) { continue; }
                TestTrue(TEXT("Source labeled child constructed fresh"),
                    Child.Get<FObservation>().DuringConstruction.Get_Disposition() == EDisposition::Fresh);
                UCk_Utils_EntityTag_UE::Add(Child, Mutation(It->RestoreProbeSlot));
                ++SavedChildren;
            }
            TestEqual(TEXT("Each source owner has one labeled child to save"), SavedChildren, 1);
        }
    });
    Spec.ReloadSettled = FCk_NetAutoTest_Assertion::CreateLambda([Run]()
    {
        auto* World = Get_PostTravelServerWorld();
        if (!World) { return false; }
        if (World != Run->FreshWorld.Get())
        {
            Run->FreshWorld = World;
            Run->FreshActor = World->SpawnActor<FActor>();
        }
        return Run->FreshActor.IsValid() && IsConstructed(UCk_Utils_EntityBridge_UE::TryGet_Entity(Run->FreshActor.Get()));
    });
    Spec.Assert = FCk_NetAutoTest_Assertion::CreateLambda([this, Run]()
    {
        auto* World = Get_PostTravelServerWorld();
        if (!TestNotNull(TEXT("Destination world exists"), World)) { return false; }
        TSet<int32> Slots;
        uint64 Epoch = 0;
        int32 FreshCount = 0;
        for (TActorIterator<FActor> It(World); It; ++It)
        {
            const auto Entity = UCk_Utils_EntityBridge_UE::TryGet_Entity(*It);
            if (!TestTrue(TEXT("Actor links to its own constructed subject"),
                IsConstructed(Entity) && UCk_Utils_EntityBridge_UE::TryGet_Actor(Entity) == *It)) { return false; }
            const auto& Obs = Entity.Get<FObservation>();
            const auto Slot = It->RestoreProbeSlot;
            if (*It == Run->FreshActor.Get())
            {
                ++FreshCount;
                TestTrue(TEXT("New actor of the same class cannot claim a consumed row"),
                    Slot == INDEX_NONE && Obs.DuringConstruction.Get_Disposition() == EDisposition::Fresh);
            }
            else
            {
                if (!TestTrue(TEXT("Restored actor property identifies one distinct saved subject"),
                    Slot >= 0 && Slot < NumSaved && !Slots.Contains(Slot))) { return false; }
                Slots.Add(Slot);
                const auto Status = ck::persistence::Query(Entity, FCk_SaveData_EntityTags::StaticStruct());
                if (Epoch == 0) { Epoch = Status.Get_Epoch(); }
                TestTrue(TEXT("Linked subject has its loader baseline before Construct and Constructed finish"),
                    Obs.DuringConstruction.Get_Disposition() == EDisposition::BaselineExpected &&
                    Obs.OnConstructed.Get_Disposition() == EDisposition::BaselineExpected);
                TestTrue(TEXT("The saved subject releases in the same nonzero load epoch"),
                    Epoch != 0 && Status.Get_Disposition() == EDisposition::Released && Status.Get_Epoch() == Epoch &&
                    Obs.DuringConstruction.Get_Epoch() == Epoch && Obs.OnConstructed.Get_Epoch() == Epoch);
                int32 SavedChildren = 0;
                int32 NewChildren = 0;
                for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Entity))
                {
                    if (ck::Is_NOT_Valid(Child) || !Child.Has<FObservation>()) { continue; }
                    const auto& Early = Child.Get<FObservation>().DuringConstruction;
                    TestTrue(TEXT("Child queried its owner in the current load epoch"), Early.Get_Epoch() == Epoch);
                    if (UCk_Utils_EntityTag_UE::Has(Child, Mutation(Slot)))
                    {
                        ++SavedChildren;
                        TestTrue(TEXT("This owner's saved label awaited adoption during its owner's Construct"),
                            Early.Get_Disposition() == EDisposition::IdentityUnresolved);
                    }
                    else
                    {
                        ++NewChildren;
                        TestTrue(TEXT("The adjacent owner's label was immediately fresh under this owner"),
                            Early.Get_Disposition() == EDisposition::Fresh);
                    }
                }
                TestTrue(TEXT("Exactly one child retained its source mutation and one child was new"),
                    SavedChildren == 1 && NewChildren == 1);
            }
            for (int32 Other = 0; Other < NumSaved; ++Other)
            {
                TestEqual(TEXT("ECS tag baseline belongs only to its corresponding saved actor field"),
                    UCk_Utils_EntityTag_UE::Has(Entity, Mutation(Other)), Other == Slot);
            }
        }
        TestTrue(TEXT("Exactly the four saved actors and one independent fresh witness exist"),
            Slots.Num() == NumSaved && FreshCount == 1);
        TestTrue(TEXT("A second load uses a distinct epoch"), Epoch != Run->PreviousEpoch);
        Run->PreviousEpoch = Epoch;
        return !HasAnyErrors();
    });
    EnqueueRoundTrip(this, Spec);
    return true;
}

#endif
