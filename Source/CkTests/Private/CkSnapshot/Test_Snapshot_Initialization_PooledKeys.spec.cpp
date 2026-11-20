#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"
#include "CkTests/Snapshot/CkAutoTest_Snapshot_PooledInitialization.h"
#include "CkTests/Snapshot/CkSnapshot_TestHarness_Common.h"

#include "Test_Snapshot_Inspection_Fixtures.h"

#include <Engine/World.h>
#include <Kismet/GameplayStatics.h>
#include <Serialization/MemoryReader.h>

namespace ck_test_snapshot_pooled_initialization
{
    constexpr int32 FirstValue = 17;
    constexpr int32 SecondValue = 29;

    struct FBeforeTravel
    {
        FCk_Handle _Root;
        TArray<FCk_Handle> _Sources;
        TArray<uint32> _SavedRows;
        int32 _LegacyLastValue = -1;
    };

    auto FindRoot(UWorld* InWorld) -> FCk_Handle
    {
        return ck::auto_test::snapshot::ResolveEntityBySpawnRecipe(
            InWorld, UCk_AutoTest_Snapshot_PooledInitialization_UE::StaticClass());
    }

    void PrepareHistoricalPayloads(FAutomationTestBase* InTest, const FBeforeTravel& InBefore,
        FName InSlot, bool InTwoProducers)
    {
        if (!InTest->TestEqual(TEXT("Fixture recorded two source IDs"), InBefore._SavedRows.Num(), 2) ||
            !InTest->TestEqual(TEXT("Fixture retained two original owners"), InBefore._Sources.Num(), 2))
        { return; }

        auto* Save = Cast<UCk_Snapshot_SaveGame>(ck::snapshot::TryLoad_SlotSaveGame_Guarded(InSlot.ToString(), 0));
        if (!InTest->TestNotNull(TEXT("Current capture has a CK envelope"), Save))
        { return; }

        auto Tables = FCk_Snapshot_V3_Tables{};
        auto Reader = FMemoryReader{Save->_SnapshotBytesV3, true};
        FCk_Snapshot_V3_Tables::StaticStruct()->SerializeItem(Reader, &Tables, nullptr);
        if (!InTest->TestTrue(TEXT("Current capture tables decoded completely"), !Reader.IsError() && Reader.AtEnd()) ||
            !InTest->TestEqual(TEXT("Current capture payload census matches its table"),
                Save->_HeaderV3.Get_PayloadCount(), Tables.Get_Payloads().Num()))
        { return; }

        const auto RootId = static_cast<uint32>(InBefore._Root.Get_Entity().Get_ID());
        const auto* First = Tables.Get_Entities().FindByPredicate([&](const auto& Entry)
        { return Entry.Get_SavedId() == InBefore._SavedRows[0]; });
        if (!InTest->TestTrue(TEXT("First member has a saved rendezvous identity"), First && First->Get_SaveKey().IsValid()))
        { return; }

        auto GroupRows = TArray<uint32>{};
        for (const auto& Entry : Tables.Get_Entities())
        {
            if (Entry.Get_SaveKey() != First->Get_SaveKey()) { continue; }
            if (!InTest->TestTrue(TEXT("Group member is engine-owned under the fixture root"),
                Entry.Get_Provenance() == ECk_Snapshot_V3_Provenance::EngineOwned && Entry.Get_LifetimeOwnerSavedId() == RootId))
            { return; }
            GroupRows.Add(Entry.Get_SavedId());
        }
        if (!InTest->TestTrue(TEXT("Current writer retains both exact shared rows in saved-ID order"),
                GroupRows == InBefore._SavedRows) ||
            !InTest->TestFalse(TEXT("Current writer emits no payload on either group member"),
                Tables.Get_Payloads().ContainsByPredicate([&](const auto& Entry)
                { return GroupRows.Contains(Entry.Get_OwnerSavedId()); })))
        { return; }

        const auto Document = ck::snapshot::Inspect_SaveSlot(InSlot);
        InTest->TestTrue(TEXT("Offline inspection parsed the saved channel rows"), Document.Get_TableParseSucceeded());
        InTest->TestEqual(TEXT("Distinct members sharing the rendezvous key inspect without errors"),
            Document.Get_ErrorCount(), 0);
        auto ClaimIds = TArray<uint32>{};
        for (const auto& Claim : Save->_HeaderV3.Get_SharedRendezvousClaims())
        {
            if (Claim.Get_SaveKey() == First->Get_SaveKey())
            { ClaimIds.Add(Claim.Get_SavedId()); }
        }
        ClaimIds.Sort();
        InTest->TestTrue(TEXT("The disk header records exactly the fixture group's two saved IDs"), ClaimIds == GroupRows);

        // Older V3 writers put member payloads on these rows. Recreate that wire shape in the private
        // slot after verifying the current capture, so the production loader still sees the historical inputs.
        const auto TypePath = FCk_SaveData_AutoTest_PooledValue::StaticStruct()->GetPathName();
        const auto FirstSourceId = static_cast<uint32>(InBefore._Sources[0].Get_Entity().Get_ID());
        for (const auto Id : GroupRows)
        {
            const auto IsFirstSource = Id == FirstSourceId;
            if (IsFirstSource && !InTwoProducers) { continue; }
            auto Payload = FCk_SaveData_AutoTest_PooledValue{};
            Payload.Set_Value(IsFirstSource ? FirstValue : SecondValue);
            Tables.Get_Payloads().Add(ck_test_snapshot_inspection::Make_Payload(Id, TypePath,
                ck_test_snapshot_inspection::SerializeBlob_Save(FInstancedStruct::Make(Payload))));
        }
        Save->_HeaderV3.Set_PayloadCount(Tables.Get_Payloads().Num()).Set_SharedRendezvousClaims({});
        Save->_SnapshotBytesV3 = ck_test_snapshot_inspection::Get_TablesBytes(Tables);
        if (!InTest->TestTrue(TEXT("Historical payload fixture written to its isolated slot"),
            UGameplayStatics::SaveGameToSlot(Save, InSlot.ToString(), 0)))
        { return; }
        const auto LegacyDocument = ck::snapshot::Inspect_SaveSlot(InSlot);
        InTest->TestTrue(TEXT("Historical table is readable"), LegacyDocument.Get_TableParseSucceeded());
        InTest->TestTrue(TEXT("Inspection retains the unmarked group's duplicate-key error"),
            LegacyDocument.Get_Diagnostics().ContainsByPredicate([&](const auto& Item)
            {
                return Item.Get_Code() == ECk_SnapshotInspection_DiagnosticCode::DuplicateSaveKey &&
                    Item.Get_RelatedSavedIds().Num() == 2 && Item.Get_RelatedSavedIds().Contains(GroupRows[0]) &&
                    Item.Get_RelatedSavedIds().Contains(GroupRows[1]);
            }));
    }

    auto Enqueue(FAutomationTestBase* InTest, bool InTracked, bool InTwoProducers, FName InSlot) -> bool
    {
        using namespace ck::auto_test::snapshot;
        using namespace ck::persistence;
        using FRoot = ck::FFragment_AutoTest_PooledRoot;
        using FValue = ck::FFragment_AutoTest_PooledValue;
        const auto Before = MakeShared<FBeforeTravel>();
        if (InTracked && InTwoProducers)
        {
            InTest->AddExpectedErrorPlain(FString::Printf(TEXT("Request_Load: LOST payload [%s]"),
                *FCk_SaveData_AutoTest_PooledValue::StaticStruct()->GetName()), EAutomationExpectedMessageFlags::Contains, 2);
            InTest->AddExpectedErrorPlain(
                TEXT("Request_Load: the load COMPLETED WITH LOSS — [2] payload entries did not apply (rejected [2], no handler [0], timed out [0]"),
                EAutomationExpectedMessageFlags::Contains, 1);
        }

        auto Spec = FCk_SnapshotRoundTrip_Spec{};
        Spec.NumPIEClients = 1;
        Spec.NumCycles = 1;
        Spec.SlotName = InSlot;
        Spec.Spawn = FCk_NetAutoTest_ServerAction::CreateLambda([InTracked, InTwoProducers](UWorld* InServer)
        {
            auto Owner = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InServer);
            auto Params = FCk_AutoTest_PooledInitializationParams{};
            Params.TrackInitialization = InTracked;
            Params.TwoProducers = InTwoProducers;
            UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner,
                UCk_AutoTest_Snapshot_PooledInitialization_UE::StaticClass(), FInstancedStruct::Make(Params), {});
        });
        Spec.SubjectReady = FCk_NetAutoTest_Assertion::CreateLambda([]()
        {
            const auto Root = FindRoot(Get_PostTravelServerWorld());
            if (ck::Is_NOT_Valid(Root) || NOT Root.Has<FRoot>())
            { return false; }
            const auto& Children = Root.Get<FRoot>()._ConstructedChildren;
            return Children.Num() == 2 && ck::IsValid(Children[0]) && ck::IsValid(Children[1]);
        });
        Spec.Mutate = FCk_NetAutoTest_ServerAction::CreateLambda([InTest, Before](UWorld* InServer)
        {
            Before->_Root = FindRoot(InServer);
            if (NOT InTest->TestTrue(TEXT("Original root was constructed"),
                ck::IsValid(Before->_Root) && Before->_Root.Has<FRoot>()))
            { return; }
            Before->_Sources = Before->_Root.Get<FRoot>()._ConstructedChildren;
            if (NOT InTest->TestEqual(TEXT("Two distinct original pooled owners"), Before->_Sources.Num(), 2))
            { return; }
            InTest->TestTrue(TEXT("Source handles are distinct"), Before->_Sources[0] != Before->_Sources[1]);
            for (auto Index = 0; Index < Before->_Sources.Num(); ++Index)
            {
                auto Source = Before->_Sources[Index];
                Source.Get<FValue>()._Value = Index == 0 ? FirstValue : SecondValue;
                Before->_SavedRows.Add(static_cast<uint32>(Source.Get_Entity().Get_ID()));
            }
            // Capture orders these equal-depth rows by saved ID, and legacy hydration drains in reverse.
            Before->_LegacyLastValue = Before->_SavedRows[0] < Before->_SavedRows[1] ? FirstValue : SecondValue;
            Before->_SavedRows.Sort();
        });
        Spec.PostSave = FCk_NetAutoTest_ServerAction::CreateLambda([InTest, Before, InSlot, InTwoProducers](UWorld* InServer)
        {
            const auto* Snapshot = Get_SnapshotSubsystem(InServer);
            if (NOT InTest->TestNotNull(TEXT("Capture subsystem exists"), Snapshot))
            { return; }
            const auto Report = Snapshot->Get_LastSaveReport();
            if (!InTest->TestTrue(TEXT("Source capture succeeded"), Report.Get_Result() == ECk_SnapshotResult::Success) ||
                !InTest->TestTrue(TEXT("Both source references captured without loss"), Report.Get_Losses().IsEmpty()))
            { return; }
            PrepareHistoricalPayloads(InTest, *Before, InSlot, InTwoProducers);
        });
        Spec.Assert = FCk_NetAutoTest_Assertion::CreateLambda([InTest, Before, InTracked, InTwoProducers]()
        {
            auto* World = Get_PostTravelServerWorld();
            const auto Root = FindRoot(World);
            if (NOT InTest->TestTrue(TEXT("Root physically reacquired from destination spawn recipe"),
                ck::IsValid(Root) && Root.Has<FRoot>()))
            { return false; }
            InTest->TestFalse(TEXT("Pre-travel root is invalid"), ck::IsValid(Before->_Root));
            for (const auto& Source : Before->_Sources)
            { InTest->TestFalse(TEXT("Pre-travel pooled owner is invalid"), ck::IsValid(Source)); }

            const auto& Observation = Root.Get<FRoot>();
            if (NOT InTest->TestEqual(TEXT("Destination reconstructs one representative"),
                Observation._ConstructedChildren.Num(), 1))
            { return false; }
            const auto Target = Observation._ConstructedChildren[0];
            if (NOT InTest->TestTrue(TEXT("Fresh representative exists"), ck::IsValid(Target) && Target.Has<FValue>()))
            { return false; }
            if (NOT InTest->TestEqual(TEXT("Both saved references survived hydration"),
                Observation._RestoredReferences.Num(), 2))
            { return false; }
            InTest->TestTrue(TEXT("First saved owner remapped to representative"), Observation._RestoredReferences[0] == Target);
            InTest->TestTrue(TEXT("Second saved owner remapped to same representative"), Observation._RestoredReferences[1] == Target);

            const auto* Snapshot = Get_SnapshotSubsystem(World);
            if (NOT InTest->TestNotNull(TEXT("Destination snapshot subsystem exists"), Snapshot))
            { return false; }
            const auto Report = Snapshot->Get_LastLoadReport();
            const auto& Value = Target.Get<FValue>();
            const auto* PayloadType = FCk_SaveData_AutoTest_PooledValue::StaticStruct();
            const auto Status = Query(Target, PayloadType);
            const auto Conflict = InTracked && InTwoProducers;

            if (Conflict)
            {
                InTest->TestTrue(TEXT("Rejected pooled baseline reports completed load with loss"),
                    Report.Get_Result() == ECk_SnapshotResult::Succeeded_WithLoss);
                InTest->TestEqual(TEXT("Both producing payload occurrences rejected"), Report.Get_PayloadsRejected(), 2);
                InTest->TestEqual(TEXT("No conflicting payload entered its handler"), Value._AppliedValues.Num(), 0);
                InTest->TestEqual(TEXT("Original projection was not partially replaced"), Value._Value, -1);
                InTest->TestTrue(TEXT("Conflicting baseline terminates unavailable at coherent release"),
                    Status.Get_Disposition() == EInitializationDisposition::Unavailable && Status.Get_IsReleased()
                    && Status.Get_Failure() == EInitializationFailure::Rejected);
                const auto& Conflicts = Report.Get_InitializationConflicts();
                if (InTest->TestEqual(TEXT("One exact entity/type conflict is named"), Conflicts.Num(), 1))
                {
                    InTest->TestEqual(TEXT("Conflict names payload schema"), Conflicts[0].Get_PayloadType(), PayloadType->GetPathName());
                    InTest->TestTrue(TEXT("Conflict names both exact saved source rows"), Conflicts[0].Get_SavedRows() == Before->_SavedRows);
                    InTest->TestFalse(TEXT("Conflict names the live owner identity"), Conflicts[0].Get_OwnerIdentity().IsEmpty());
                }
            }
            else
            {
                InTest->TestTrue(TEXT("Unambiguous or legacy load succeeds"), Report.Get_Result() == ECk_SnapshotResult::Success);
                InTest->TestEqual(TEXT("No payload rejection"), Report.Get_PayloadsRejected(), 0);
                InTest->TestTrue(TEXT("No initialization conflicts"), Report.Get_InitializationConflicts().IsEmpty());
                if (InTracked)
                {
                    InTest->TestEqual(TEXT("Only producing row applies"), Value._AppliedValues.Num(), 1);
                    InTest->TestEqual(TEXT("Unique later producer restores its saved value"), Value._Value, SecondValue);
                    InTest->TestTrue(TEXT("Unique producer baseline is released"),
                        Status.Get_Disposition() == EInitializationDisposition::Released && Status.Get_IsReleased());
                }
                else
                {
                    InTest->TestTrue(TEXT("Legacy type remains unenrolled"), Status.Get_Disposition() == EInitializationDisposition::Undeclared);
                    if (InTest->TestEqual(TEXT("Legacy receives both payloads"), Value._AppliedValues.Num(), 2))
                    {
                        InTest->TestTrue(TEXT("Legacy receives both distinct saved values"),
                            Value._AppliedValues.Contains(FirstValue) && Value._AppliedValues.Contains(SecondValue));
                        InTest->TestEqual(TEXT("Legacy preserves reverse-table application order"),
                            Value._AppliedValues.Last(), Before->_LegacyLastValue);
                    }
                    InTest->TestEqual(TEXT("Legacy final value retains historical ordering"), Value._Value, Before->_LegacyLastValue);
                }
            }
            return NOT InTest->HasAnyErrors();
        });
        EnqueueRoundTrip(InTest, Spec);
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Snapshot_Initialization_PooledUniqueProducer,
    "Ck.Snapshot.Initialization.PooledKeys.UniqueProducer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCk_Snapshot_Initialization_PooledUniqueProducer::RunTest(const FString&)
{
    return ck_test_snapshot_pooled_initialization::Enqueue(this, true, false, TEXT("CkTest_PooledUniqueProducer"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Snapshot_Initialization_PooledRejectMultiple,
    "Ck.Snapshot.Initialization.PooledKeys.RejectMultipleProducers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCk_Snapshot_Initialization_PooledRejectMultiple::RunTest(const FString&)
{
    return ck_test_snapshot_pooled_initialization::Enqueue(this, true, true, TEXT("CkTest_PooledRejectMultiple"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Snapshot_Initialization_PooledLegacyMultiple,
    "Ck.Snapshot.Initialization.PooledKeys.LegacyMultipleProducers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCk_Snapshot_Initialization_PooledLegacyMultiple::RunTest(const FString&)
{
    return ck_test_snapshot_pooled_initialization::Enqueue(this, false, true, TEXT("CkTest_PooledLegacyMultiple"));
}

#endif
