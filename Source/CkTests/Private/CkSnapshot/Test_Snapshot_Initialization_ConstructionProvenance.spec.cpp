#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Fragment_Data.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkSnapshot/CkSnapshot_Utils.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"
#include "CkTests/Snapshot/CkAutoTest_Snapshot_InitializationProbe.h"
#include "CkTests/Snapshot/CkSnapshot_TestHarness_Common.h"
#include "Engine/World.h"

#include <NativeGameplayTags.h>

namespace ck_autotest_initialization_gate
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Mutated, "Ck.AutoTest.Initialization.Mutated");

    using FObservation = ck::FFragment_AutoTest_InitializationObservation;

    auto GetRoot(UWorld* InWorld) -> FCk_Handle
    {
        return ck::auto_test::snapshot::ResolveEntityBySpawnRecipe(
            InWorld, UCk_AutoTest_Snapshot_InitializationRoot_UE::StaticClass());
    }

    auto GetChild(const FCk_Handle& InOwner, int32 InIndex) -> FCk_Handle
    {
        if (ck::Is_NOT_Valid(InOwner) || NOT InOwner.Has<FObservation>())
        { return {}; }
        const auto& Children = InOwner.Get<FObservation>().Children;
        return Children.IsValidIndex(InIndex) ? Children[InIndex] : FCk_Handle{};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCk_Snapshot_Initialization_ConstructionProvenance,
    "Ck.Snapshot.Initialization.ConstructionProvenance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCk_Snapshot_Initialization_ConstructionProvenance::RunTest(const FString& /*InParameters*/)
{
    using namespace ck_autotest_initialization_gate;
    using namespace ck::auto_test::snapshot;
    using namespace ck::persistence;
    using EDisposition = EInitializationDisposition;

    auto Spec = FCk_SnapshotRoundTrip_Spec{};
    Spec.NumPIEClients = 1;
    // This scenario intentionally adds new children on reload. Re-saving would include them in the next manifest.
    Spec.NumCycles = 1;
    Spec.SlotName = FName{TEXT("CkTest_Initialization_ConstructionProvenance")};
    Spec.Spawn = FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* InServer)
    {
        auto Owner = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InServer);
        UCk_Utils_EntityScript_UE::Request_SpawnEntity(
            Owner, UCk_AutoTest_Snapshot_InitializationRoot_UE::StaticClass(), FInstancedStruct{}, {});
    });
    Spec.SubjectReady = FCk_NetAutoTest_Assertion::CreateLambda([]()
    {
        const auto Root = GetRoot(Get_PostTravelServerWorld());
        const auto Saved = GetChild(GetChild(Root, 0), 0);
        return ck::IsValid(Saved) && Root.Get<FObservation>().ConstructedCallbackRan;
    });
    Spec.Mutate = FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer)
    {
        auto Root = GetRoot(InServer);
        auto Parent = GetChild(Root, 0);
        auto Saved = GetChild(Parent, 0);
        if (NOT TestTrue(TEXT("Saved child is the only original child"),
            ck::IsValid(Saved) && Parent.Get<FObservation>().Children.Num() == 1))
        { return; }
        TestTrue(TEXT("Original root constructs fresh outside a load"),
            Root.Get<FObservation>().DuringConstruct.Get_Disposition() == EDisposition::Fresh);
        UCk_Utils_EntityTag_UE::Add_UsingGameplayTag(Root, Mutated);
        TestTrue(TEXT("Staged owner has its own EngineOwned rendezvous key"), UCk_Utils_Snapshot_UE::Get_HasSaveKey(Parent));
        UCk_Utils_EntityTag_UE::Add_UsingGameplayTag(Parent, Mutated);
        UCk_Utils_EntityTag_UE::Add_UsingGameplayTag(Saved, Mutated);
    });
    Spec.PostSave = FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer)
    {
        const auto* Subsystem = Get_SnapshotSubsystem(InServer);
        if (NOT TestNotNull(TEXT("Capture subsystem"), Subsystem))
        { return; }
        const auto Report = Subsystem->Get_LastSaveReport();
        TestTrue(TEXT("Capture succeeded"), Report.Get_Result() == ECk_SnapshotResult::Success);
        TestTrue(TEXT("Capture retained all durable values"), Report.Get_Losses().IsEmpty());
    });
    Spec.Assert = FCk_NetAutoTest_Assertion::CreateLambda([this]()
    {
        auto* World = Get_PostTravelServerWorld();
        auto Root = GetRoot(World);
        const auto Parent = GetChild(Root, 0);
        auto Saved = GetChild(Parent, 0);
        auto New = GetChild(Parent, 1);
        auto ExplicitFresh = GetChild(Parent, 2);
        if (NOT TestTrue(TEXT("Reconstructed parent has saved, new and explicitly fresh children"),
            ck::IsValid(Saved) && ck::IsValid(New) && ck::IsValid(ExplicitFresh) && Parent.Get<FObservation>().Children.Num() == 3))
        { return false; }
        const auto* Subsystem = Get_SnapshotSubsystem(World);
        if (NOT TestNotNull(TEXT("Destination snapshot subsystem"), Subsystem))
        { return false; }
        TestTrue(TEXT("Restore completed successfully"), Subsystem->Get_LastLoadReport().Get_Result() == ECk_SnapshotResult::Success);
        const auto Epoch = Root.Get<FObservation>().DuringConstruct.Get_Epoch();
        TestTrue(TEXT("Restore has a nonzero epoch"), Epoch != 0);
        const auto* Payload = FCk_SaveData_EntityTags::StaticStruct();
        const auto Check = [this, Epoch, Payload](const TCHAR* InName, const FCk_Handle& InEntity,
            EDisposition InConstruct, EDisposition InConstructed, EDisposition InFinal)
        {
            if (NOT TestTrue(FString::Printf(TEXT("%s observations exist"), InName), InEntity.Has<FObservation>()))
            { return; }
            const auto& Observation = InEntity.Get<FObservation>();
            TestTrue(FString::Printf(TEXT("%s direct Construct disposition"), InName),
                Observation.DuringConstruct.Get_Disposition() == InConstruct);
            TestTrue(FString::Printf(TEXT("%s Constructed callback ran"), InName), Observation.ConstructedCallbackRan);
            TestTrue(FString::Printf(TEXT("%s Constructed disposition"), InName),
                Observation.OnConstructed.Get_Disposition() == InConstructed);
            const auto Status = Query(InEntity, Payload);
            TestTrue(FString::Printf(TEXT("%s final disposition"), InName), Status.Get_Disposition() == InFinal);
            TestTrue(FString::Printf(TEXT("%s same load epoch throughout"), InName),
                Observation.DuringConstruct.Get_Epoch() == Epoch && Observation.OnConstructed.Get_Epoch() == Epoch
                && Status.Get_Epoch() == Epoch);
        };
        Check(TEXT("Runtime row"), Root, EDisposition::BaselineExpected, EDisposition::BaselineExpected, EDisposition::Released);
        Check(TEXT("Saved child"), Saved, EDisposition::IdentityUnresolved, EDisposition::IdentityUnresolved, EDisposition::Released);
        Check(TEXT("Unmapped child"), New, EDisposition::IdentityUnresolved, EDisposition::IdentityUnresolved, EDisposition::Fresh);
        Check(TEXT("Explicit fresh child"), ExplicitFresh, EDisposition::Fresh, EDisposition::Fresh, EDisposition::Fresh);
        const auto CheckWrites = [this](const TCHAR* InName, const FCk_Handle& InBoard, ECk_HexGridBlocker_Failure InExpected)
        {
            const auto& Observation = InBoard.Get<FObservation>();
            TestTrue(FString::Printf(TEXT("%s board composed"), InName), Observation.ComposedBoard);
            TestTrue(FString::Printf(TEXT("%s Construct blocker request"), InName), Observation.ConstructWrite == InExpected);
            TestTrue(FString::Printf(TEXT("%s OnConstructed blocker request"), InName), Observation.ConstructedWrite == InExpected);
        };
        CheckWrites(TEXT("Mapped child"), Saved, ECk_HexGridBlocker_Failure::Unavailable);
        CheckWrites(TEXT("Awaiting owner"), New, ECk_HexGridBlocker_Failure::Unavailable);
        CheckWrites(TEXT("Explicit fresh child"), ExplicitFresh, ECk_HexGridBlocker_Failure::None);
        TestTrue(TEXT("Late-key owner restored its own mutation"), UCk_Utils_EntityTag_UE::Has_UsingGameplayTag(Parent, Mutated));
        TestTrue(TEXT("Late-key owner consumed its saved tag baseline"), Query(Parent, Payload).Get_Disposition() == EDisposition::Released);
        TestTrue(TEXT("Runtime row recovered saved mutation"), UCk_Utils_EntityTag_UE::Has_UsingGameplayTag(Root, Mutated));
        TestTrue(TEXT("Saved child recovered saved mutation"), UCk_Utils_EntityTag_UE::Has_UsingGameplayTag(Saved, Mutated));
        TestFalse(TEXT("Unmapped sibling did not receive saved mutation"), UCk_Utils_EntityTag_UE::Has_UsingGameplayTag(New, Mutated));
        TestFalse(TEXT("Explicit fresh sibling did not receive saved mutation"), UCk_Utils_EntityTag_UE::Has_UsingGameplayTag(ExplicitFresh, Mutated));
        return NOT HasAnyErrors();
    });
    EnqueueRoundTrip(this, Spec);
    return true;
}

#endif
