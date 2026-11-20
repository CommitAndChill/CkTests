#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/HexGrid/CkHexPersistenceReadinessProbe.h"
#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"
#include "CkTests/Snapshot/CkSnapshot_TestHarness_Common.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/Snapshot/CkSnapshot_Context.h"
#include "CkEcs/Snapshot/CkSnapshot_HandleWalk.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Utils.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Fragment.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGrid_Projection.h"
#include "CkGrid/HexGridSystem/Persistence/CkHexGridPersistence_Fragment_Data.h"
#include "CkSnapshot/SaveGame/CkSnapshot_SaveGame.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"
#include <Engine/World.h>
#include <Kismet/GameplayStatics.h>
#include <Serialization/MemoryReader.h>
#include <Serialization/MemoryWriter.h>
#include <Serialization/ObjectAndNameAsStringProxyArchive.h>

namespace ck_test_hex_readiness
{
    enum class ECase { Empty, Missing, Rejected };
    const FGuid Implicated{9,0,0,1};

    auto Board(UWorld* InWorld) -> FCk_Handle_HexGrid
    {
        return UCk_Utils_HexGrid_UE::Cast(ck::auto_test::snapshot::ResolveEntityBySpawnRecipe(
            InWorld, UCk_HexPersistenceReadinessProbe::StaticClass()));
    }

    // The subject is first captured normally. Modify only its saved board payload, retain its real
    // reconstruction recipe, then hand the complete saved slot back to the production loader.
    auto RewriteCapturedBoard(FAutomationTestBase& Test, FName Slot, ECase Case) -> bool
    {
        auto* Save = Cast<UCk_Snapshot_SaveGame>(ck::snapshot::TryLoad_SlotSaveGame_Guarded(Slot.ToString(), 0));
        if (!Test.TestNotNull(TEXT("Load actual captured save envelope"), Save)) { return false; }
        FCk_Snapshot_V3_Tables Tables;
        FMemoryReader Reader(Save->_SnapshotBytesV3, true);
        FCk_Snapshot_V3_Tables::StaticStruct()->SerializeItem(Reader, &Tables, nullptr);
        if (!Test.TestFalse(TEXT("Captured tables decode"), Reader.IsError())) { return false; }
        auto& Rows = Tables.Get_Payloads();
        const auto Path = FCk_RepData_HexGridState::StaticStruct()->GetPathName();
        const auto Index = Rows.IndexOfByPredicate([&](const auto& Row) { return Row.Get_TypePath() == Path; });
        if (!Test.TestTrue(TEXT("Real capture contains board-state baseline"), Rows.IsValidIndex(Index))) { return false; }
        if (Case == ECase::Missing)
        {
            Rows.RemoveAt(Index);
        }
        else
        {
            FInstancedStruct Data;
            FMemoryReader PayloadReader(Rows[Index].Get_PayloadBytes(), true);
            FObjectAndNameAsStringProxyArchive Input(PayloadReader, true);
            Input.ArIsSaveGame = false;
            Input.SetIsPersistent(true);
            Data.Serialize(Input);
            auto* State = Data.GetMutablePtr<FCk_RepData_HexGridState>();
            if (!Test.TestTrue(TEXT("Captured board payload has the actual reflected type"), !Input.IsError() && State))
            { return false; }
            State->Set_Placements({}).Set_Blockers({}).Set_CellRules({});
            if (Case == ECase::Rejected)
            {
                FCk_HexGridPlacement_PersistedEntry Invalid;
                Invalid.Set_PlacementId(Implicated).Set_FootprintOffsets({FCk_HexCoord{}}).Set_Cells({FCk_HexCoord{}})
                    .Set_Rotation(static_cast<ECk_HexRotation>(255));
                State->Set_Placements({Invalid});
            }
            TArray<uint8> Bytes;
            FMemoryWriter PayloadWriter(Bytes, true);
            FObjectAndNameAsStringProxyArchive Output(PayloadWriter, true);
            Output.ArIsSaveGame = false;
            Output.SetIsPersistent(true);
            ck::FSnapshotContext Context;
            Data.Serialize(Output);
            ck::snapshot::RemapHandles(Data.GetScriptStruct(), Data.GetMutableMemory(), Output, Context);
            if (!Test.TestFalse(TEXT("Modified tagged board payload serializes"), Output.IsError())) { return false; }
            Rows[Index].Set_PayloadBytes(Bytes);
        }
        Save->_SnapshotBytesV3.Reset();
        FMemoryWriter Writer(Save->_SnapshotBytesV3, true);
        FCk_Snapshot_V3_Tables::StaticStruct()->SerializeItem(Writer, &Tables, nullptr);
        Save->_HeaderV3.Set_PayloadCount(Rows.Num());
        return Test.TestTrue(TEXT("Write amended captured slot through save-game envelope"), !Writer.IsError() &&
            UGameplayStatics::SaveGameToSlot(Save, Slot.ToString(), 0));
    }

    auto Enqueue(FAutomationTestBase& Test, ECase Case, FName Slot) -> void
    {
        using namespace ck::auto_test::snapshot;
        auto Before = MakeShared<FCk_Handle_HexGrid>();
        FCk_SnapshotRoundTrip_Spec Spec;
        Spec.NumPIEClients = 1;
        Spec.NumCycles = 1;
        Spec.SlotName = Slot;
        Spec.Spawn = FCk_NetAutoTest_ServerAction::CreateLambda([&Test](UWorld* World)
        {
            auto* Asset = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, ck::tests::hex_persistence::BoardAssetPath);
            if (!Test.TestNotNull(TEXT("Saved persistence recipe"), Asset)) { return; }
            auto Owner = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(World);
            UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner, UCk_HexPersistenceReadinessProbe::StaticClass(),
                FInstancedStruct::Make(FCk_HexGrid_SpawnParams{Asset, FTransform::Identity}), {});
        });
        Spec.SubjectReady = FCk_NetAutoTest_Assertion::CreateLambda([]()
        { return UCk_Utils_HexGrid_UE::Get_IsReady(Board(Get_PostTravelServerWorld())); });
        Spec.Mutate = FCk_NetAutoTest_ServerAction::CreateLambda([&Test, Before](UWorld* World)
        {
            auto Grid = Board(World);
            *Before = Grid;
            auto Projection = ck::hex_grid::ReadCommittedProjection(Grid);
            FCk_HexGridPlacement_View Placement;
            Placement.Set_PlacementId(Implicated).Set_FootprintOffsets({FCk_HexCoord{}}).Set_Cells({FCk_HexCoord{}});
            Projection._Placements.Add(Placement);
            Projection._ManualBlocks.Add({1,0});
            Projection._LocalTags.Add(FCk_HexCoord{}, FGameplayTagContainer{});
            Test.TestTrue(TEXT("Mutated projection committed before real capture"),
                ck::hex_grid::TryCommitProjection(Grid, Projection).Get_Succeeded());
        });
        Spec.PostSave = FCk_NetAutoTest_ServerAction::CreateLambda([&Test, Slot, Case](UWorld* World)
        {
            const auto* Snapshot = Get_SnapshotSubsystem(World);
            Test.TestTrue(TEXT("Production capture succeeded without durable losses"), Snapshot &&
                Snapshot->Get_LastSaveReport().Get_Result() == ECk_SnapshotResult::Success &&
                Snapshot->Get_LastSaveReport().Get_Losses().IsEmpty());
            RewriteCapturedBoard(Test, Slot, Case);
        });
        Spec.Assert = FCk_NetAutoTest_Assertion::CreateLambda([&Test, Before, Case]()
        {
            auto* World = Get_PostTravelServerWorld();
            const auto Grid = Board(World);
            auto* Probe = Cast<UCk_HexPersistenceReadinessProbe>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(Grid));
            auto* Snapshot = Get_SnapshotSubsystem(World);
            if (!Test.TestTrue(TEXT("Fresh world physically reconstructed the readiness subject"),
                Probe && Snapshot && ck::IsValid(Grid) && !ck::IsValid(*Before) && Grid != *Before)) { return false; }
            Test.TestTrue(TEXT("Construct consumer saw AwaitingRestore"), Probe->AwaitingAtConstruct);
            Test.TestTrue(TEXT("Request submitted before hydration was rejected NotReady"), Probe->RequestRejectedNotReady);
            Test.TestTrue(TEXT("Fresh procedural child remained usable during unrelated parent restoration"), Probe->FreshChildReady);
            Test.TestFalse(TEXT("No terminal gameplay notification preceded coherent release"), Probe->EarlyTerminalNotification);
            Test.TestEqual(TEXT("Exactly one terminal readiness notification"), Probe->TerminalNotifications, 1);
            Test.TestEqual(TEXT("OnHydrated fired even when baseline was missing or rejected"), Probe->HydratedNotifications, 1);
            const auto Ready = UCk_Utils_HexGrid_UE::Get_Readiness(Grid);
            const auto P = ck::hex_grid::ReadCommittedProjection(Grid);
            const auto Report = Snapshot->Get_LastLoadReport();
            Test.TestTrue(TEXT("Load accounting closes"), Report.Get_IsAccountingClosed());
            if (Case == ECase::Empty)
            {
                Test.TestTrue(TEXT("Present empty baseline reached Ready"), Ready.Get_State() == ECk_HexGridReadiness::Ready);
                Test.TestTrue(TEXT("Empty projection committed before neutral acceptance released gameplay"),
                    Probe->CommittedWhileAwaiting && Probe->EmptyAtCommit);
                Test.TestTrue(TEXT("Empty baseline clears placements, live contributors and mutable overrides"),
                    P._Placements.IsEmpty() && P._Blockers.IsEmpty() && P._ManualBlocks.IsEmpty() && P._LocalTags.IsEmpty());
                Test.TestTrue(TEXT("Empty baseline is a successful applied payload"),
                    Report.Get_Result() == ECk_SnapshotResult::Success && Report.Get_PayloadsRejected() == 0 && Report.Get_PayloadsApplied() > 0);
            }
            else
            {
                Test.TestTrue(TEXT("Absent/rejected baseline never means an accepted empty board"),
                    Ready.Get_State() == ECk_HexGridReadiness::Unavailable && !Probe->CommittedWhileAwaiting);
                Test.TestFalse(TEXT("OnHydrated alone did not report successful readiness"), Probe->HydratedSawReady);
                Test.TestTrue(TEXT("Rejected or missing projection retains reconstructed recipe contributor"),
                    P._Blockers.Num() == 1 && P._Placements.IsEmpty());
                const auto Failure = Case == ECase::Missing ? ECk_HexGridFailure::RestoreBaselineMissing : ECk_HexGridFailure::InvalidRotation;
                Test.TestTrue(TEXT("Readiness retains explicit terminal cause"), Ready.Get_Failure() == Failure);
                if (Case == ECase::Rejected)
                {
                    Test.TestTrue(TEXT("Readiness retains implicated placement ID"), Ready.Get_PlacementId() == Implicated);
                    Test.TestTrue(TEXT("Actual report classifies exactly one rejected board projection"),
                        Report.Get_Result() == ECk_SnapshotResult::Succeeded_WithLoss && Report.Get_PayloadsRejected() == 1);
                    const auto* Loss = Report.Get_PayloadLosses().FindByPredicate([](const auto& Entry)
                    { return Entry.Get_PayloadType().Contains(FCk_RepData_HexGridState::StaticStruct()->GetName()); });
                    Test.TestTrue(TEXT("Load loss names board payload, owner and rejection"),
                        Loss && !Loss->Get_OwnerIdentity().IsEmpty() && Loss->Get_Reason() == TEXT("rejected"));
                }
                else
                {
                    Test.TestEqual(TEXT("Absent payload is not counted as an applied/rejected value"), Report.Get_PayloadsRejected(), 0);
                }
            }
            FCk_Handle_HexCell Center;
            ECk_HexBoardFailure CellFailure{};
            FGameplayTagContainer Tags;
            if (Test.TestTrue(TEXT("Reconstructed center cell remains structurally available"),
                UCk_Utils_HexGrid_UE::TryGet_CellAt(Grid, {}, Center, CellFailure)))
            {
                if (Case == ECase::Empty)
                {
                    Test.TestTrue(TEXT("Ready empty baseline exposes the recipe's restored local tags"),
                        UCk_Utils_HexCell_UE::TryGet_LocalTags(Center, Tags) &&
                        Tags.HasTagExact(ck::tests::hex_persistence::AuthoredLocal));
                }
                else
                {
                    Test.TestFalse(TEXT("Unavailable baseline refuses public mutable-rule reads"),
                        UCk_Utils_HexCell_UE::TryGet_LocalTags(Center, Tags));
                    Test.TestTrue(TEXT("Rejected or absent baseline retains the physical recipe tags"),
                        Center.Has<ck::FFragment_HexCell_Tags>() && Center.Get<ck::FFragment_HexCell_Tags>()
                            .Get_Tags().HasTagExact(ck::tests::hex_persistence::AuthoredLocal));
                }
            }
            return !Test.HasAnyErrors();
        });
        EnqueueRoundTrip(&Test, Spec);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexGrid_ReadinessAndEmpty, "Ck.HexGrid.Persistence.ReadinessAndEmpty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexGrid_ReadinessAndEmpty::RunTest(const FString&)
{
    using namespace ck_test_hex_readiness;
    AddExpectedErrorPlain(FString::Printf(TEXT("Request_Load: LOST payload [%s]"), *FCk_RepData_HexGridState::StaticStruct()->GetName()),
        EAutomationExpectedMessageFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("Request_Load: the load COMPLETED WITH LOSS — [1] payload entries did not apply (rejected [1], no handler [0], timed out [0]"),
        EAutomationExpectedMessageFlags::Contains, 1);
    Enqueue(*this, ECase::Empty, TEXT("CkHexReadyEmpty"));
    Enqueue(*this, ECase::Missing, TEXT("CkHexReadyMissing"));
    Enqueue(*this, ECase::Rejected, TEXT("CkHexReadyRejected"));
    return true;
}

#endif
