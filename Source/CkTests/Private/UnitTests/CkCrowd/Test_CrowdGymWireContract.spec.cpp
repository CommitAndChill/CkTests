#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCrowdGym_WireContract.h"

#include <Misc/AutomationTest.h>
#include <limits>

namespace ck_crowd_gym_wire_test
{
    auto MakeRow(int64 Generation, int64 Sequence, int64 Serial)
        -> FCkCrowdRemoteAgentRow
    {
        FCkCrowdRemoteAgentRow Row;
        Row.Id.WorldGeneration = Generation;
        Row.Id.AgentSerial = Serial;
        Row.SampleSequence = Sequence;
        Row.ConfigurationRevision = Serial + 2;
        Row.DebugName = FString::Printf(TEXT("Agent%lld"), Serial);
        Row.DisplayId = FString::Printf(TEXT("%lld:%lld"), Generation, Serial);
        Row.Position = FVector{double(Serial), 20.0, 30.0};
        Row.WorldTransform = FTransform{Row.Position};
        Row.BaseColor = FLinearColor{0.25f, 0.5f, 0.75f, 1.0f};
        return Row;
    }

    auto MakeReady() -> FCkCrowdGym_RowsSnapshot
    {
        FCkCrowdGym_RowsSnapshot Snapshot;
        Snapshot.bReady = true;
        Snapshot.WorldGeneration = 31;
        Snapshot.SampleSequence = 42;
        Snapshot.Rows.Add(MakeRow(31, 42, 1));
        Snapshot.Rows.Add(MakeRow(31, 42, 2));
        return Snapshot;
    }

    auto MakeSelected() -> FCkCrowdGym_OwnerCommit
    {
        FCkCrowdGym_OwnerCommit Commit;
        Commit.DeliverySerial = 11;
        Commit.RequestSerial = 12;
        Commit.Kind = ECkCrowdGym_RequestKind::Selection;
        Commit.Outcome = ECkCrowdGym_RequestOutcome::Accepted;
        Commit.Snapshot = MakeReady();
        Commit.SelectionRequestSerial = 12;
        Commit.SelectedId = Commit.Snapshot.Rows[0].Id;
        Commit.bHasDetail = true;
        Commit.Detail.Id = Commit.SelectedId;
        Commit.Detail.SelectionRequestSerial = 12;
        Commit.Detail.SampleSequence = 42;
        Commit.Detail.ConfigurationRevision = Commit.Snapshot.Rows[0].ConfigurationRevision;
        Commit.Detail.Sample = Commit.Snapshot.Rows[0];
        return Commit;
    }

    auto MakeAcceptedCommand() -> FCkCrowdGym_OwnerCommit
    {
        FCkCrowdGym_OwnerCommit Commit = MakeSelected();
        Commit.DeliverySerial = 13;
        Commit.RequestSerial = 14;
        Commit.Kind = ECkCrowdGym_RequestKind::Command;
        Commit.bHasCommandResult = true;
        // A command may target a different roster agent from the retained selection.
        Commit.CommandResult.Id = Commit.Snapshot.Rows[1].Id;
        Commit.CommandResult.ClientSerial = 14;
        Commit.CommandResult.AppliedConfigurationRevision =
            Commit.Snapshot.Rows[1].ConfigurationRevision;
        Commit.CommandResult.ResultSampleSequence = Commit.Snapshot.SampleSequence;
        Commit.CommandResult.Outcome = ECkCrowdRemoteCommandOutcome::Accepted;
        return Commit;
    }

    auto Reject(FAutomationTestBase& Test, const TCHAR* Label,
        const FCkCrowdGym_OwnerCommit& Commit) -> void
    { Test.TestFalse(Label, FCkCrowdGymWireValidation::IsValidCommit(Commit)); }

    auto RejectSnapshot(FAutomationTestBase& Test, const TCHAR* Label,
        const FCkCrowdGym_RowsSnapshot& Snapshot) -> void
    { Test.TestFalse(Label, FCkCrowdGymWireValidation::IsValidReadySnapshot(Snapshot)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkCrowdGymWireContractTest,
    "Ck.Crowd.GymWireContract.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkCrowdGymWireContractTest::RunTest(const FString& Parameters) -> bool
{
    using namespace ck_crowd_gym_wire_test;
    const auto Ready = MakeReady();
    TestTrue(TEXT("complete two-agent snapshot"),
        FCkCrowdGymWireValidation::IsValidReadySnapshot(Ready));
    auto Empty = Ready;
    Empty.Rows.Reset();
    TestTrue(TEXT("ready empty roster is valid"),
        FCkCrowdGymWireValidation::IsValidReadySnapshot(Empty));

    auto Bad = Ready;
    Bad.bReady = false;
    RejectSnapshot(*this, TEXT("unready snapshot"), Bad);
    Bad = Ready; Bad.WorldGeneration = 0;
    RejectSnapshot(*this, TEXT("zero generation"), Bad);
    Bad = Ready; Bad.WorldGeneration = TNumericLimits<int64>::Max();
    RejectSnapshot(*this, TEXT("MAX generation"), Bad);
    Bad = Ready; Bad.SampleSequence = 0;
    RejectSnapshot(*this, TEXT("zero sequence"), Bad);
    Bad = Ready; Bad.SampleSequence = TNumericLimits<int64>::Max();
    RejectSnapshot(*this, TEXT("MAX sequence"), Bad);
    Bad = Ready; Bad.Rows[1].Id = {};
    RejectSnapshot(*this, TEXT("default row ID"), Bad);
    Bad = Ready; ++Bad.Rows[1].Id.WorldGeneration;
    RejectSnapshot(*this, TEXT("mixed generation"), Bad);
    Bad = Ready; ++Bad.Rows[1].SampleSequence;
    RejectSnapshot(*this, TEXT("mixed sequence"), Bad);
    Bad = Ready; Bad.Rows[1].Id = Bad.Rows[0].Id;
    RejectSnapshot(*this, TEXT("duplicate roster ID"), Bad);
    Bad = Ready; Bad.Rows[0].Position.X = std::numeric_limits<double>::quiet_NaN();
    RejectSnapshot(*this, TEXT("nonfinite row"), Bad);
    Bad = Ready; Bad.Rows[0].WorldTransform.SetRotation(FQuat{2.0, 0.0, 0.0, 0.0});
    RejectSnapshot(*this, TEXT("non-normalized finite quaternion"), Bad);

    auto Commit = MakeSelected();
    TestTrue(TEXT("accepted selection with exact frozen detail"),
        FCkCrowdGymWireValidation::IsValidCommit(Commit));
    Reject(*this, TEXT("default commit is never publishable"), FCkCrowdGym_OwnerCommit{});
    auto Mutated = Commit;
    Mutated.DeliverySerial = 0;
    Reject(*this, TEXT("zero delivery serial"), Mutated);
    Mutated = Commit; Mutated.DeliverySerial = TNumericLimits<int64>::Max();
    Reject(*this, TEXT("MAX delivery serial"), Mutated);
    Mutated = Commit; Mutated.RequestSerial = 0;
    Reject(*this, TEXT("zero request serial"), Mutated);
    Mutated = Commit; Mutated.RequestSerial = TNumericLimits<int64>::Max();
    Reject(*this, TEXT("MAX request serial"), Mutated);
    Mutated = Commit; Mutated.Kind = static_cast<ECkCrowdGym_RequestKind>(255);
    Reject(*this, TEXT("invalid request kind"), Mutated);
    Mutated = Commit; Mutated.Outcome = static_cast<ECkCrowdGym_RequestOutcome>(255);
    Reject(*this, TEXT("invalid request outcome"), Mutated);
    Mutated = Commit; Mutated.SelectionRequestSerial = 13;
    Reject(*this, TEXT("accepted selection serial differs from request"), Mutated);
    Mutated = Commit; Mutated.SelectedId.AgentSerial = 0;
    Reject(*this, TEXT("partially default selection ID"), Mutated);
    Mutated = Commit; Mutated.SelectedId.AgentSerial = 999;
    Reject(*this, TEXT("selected ID absent from frozen roster"), Mutated);
    Mutated = Commit; Mutated.bHasDetail = false;
    Reject(*this, TEXT("selected ID cannot omit detail"), Mutated);
    Mutated = Commit; Mutated.Detail.Id.AgentSerial++;
    Reject(*this, TEXT("detail ID mismatches selection"), Mutated);
    Mutated = Commit; Mutated.Detail.SelectionRequestSerial++;
    Reject(*this, TEXT("detail request serial mismatch"), Mutated);
    Mutated = Commit; Mutated.Detail.SampleSequence++;
    Mutated.Detail.Sample.SampleSequence++;
    Reject(*this, TEXT("detail sample epoch mismatch"), Mutated);
    Mutated = Commit; Mutated.Detail.ConfigurationRevision++;
    Mutated.Detail.Sample.ConfigurationRevision++;
    Reject(*this, TEXT("detail revision mismatch"), Mutated);
    Mutated = Commit; Mutated.Detail.Sample.DebugName = TEXT("detached different sample");
    Reject(*this, TEXT("detail sample differs from frozen row"), Mutated);
    Mutated = Commit;
    Mutated.Kind = ECkCrowdGym_RequestKind::Control;
    Mutated.SelectionRequestSerial = TNumericLimits<int64>::Max();
    Mutated.Detail.SelectionRequestSerial = Mutated.SelectionRequestSerial;
    TestTrue(TEXT("retained selection serial may be any positive int64"),
        FCkCrowdGymWireValidation::IsValidCommit(Mutated));

    auto Cleared = Commit;
    Cleared.DeliverySerial = 15;
    Cleared.RequestSerial = 16;
    Cleared.SelectionRequestSerial = 16;
    Cleared.SelectedId = {};
    Cleared.bHasDetail = false;
    Cleared.Detail = {};
    TestTrue(TEXT("accepted exact selection clear"),
        FCkCrowdGymWireValidation::IsValidCommit(Cleared));
    Cleared.SelectionRequestSerial = 0;
    Reject(*this, TEXT("accepted clear still correlates request serial"), Cleared);
    Cleared = Commit; Cleared.SelectedId = {}; Cleared.bHasDetail = false;
    Reject(*this, TEXT("clear cannot hide nondefault detail"), Cleared);
    Cleared = Commit; Cleared.SelectedId = {}; Cleared.bHasDetail = false;
    Cleared.Detail = {}; Cleared.SelectionRequestSerial = 0;
    Cleared.Kind = ECkCrowdGym_RequestKind::Control;
    TestTrue(TEXT("control carries exact default selection and no detail"),
        FCkCrowdGymWireValidation::IsValidCommit(Cleared));
    Cleared.Snapshot = Empty;
    TestTrue(TEXT("control carries ready empty roster"),
        FCkCrowdGymWireValidation::IsValidCommit(Cleared));
    Cleared.Detail.Sample.DebugName = TEXT("hidden");
    Reject(*this, TEXT("empty roster rejects hidden detail"), Cleared);

    auto Command = MakeAcceptedCommand();
    TestTrue(TEXT("accepted command correlates other roster agent and epoch"),
        FCkCrowdGymWireValidation::IsValidCommit(Command));
    Mutated = Command; Mutated.bHasCommandResult = false;
    Reject(*this, TEXT("accepted command requires result"), Mutated);
    Mutated = Command; ++Mutated.CommandResult.ClientSerial;
    Reject(*this, TEXT("command result request serial mismatch"), Mutated);
    Mutated = Command; ++Mutated.CommandResult.ResultSampleSequence;
    Reject(*this, TEXT("command result mixed sample epoch"), Mutated);
    Mutated = Command; ++Mutated.CommandResult.AppliedConfigurationRevision;
    Reject(*this, TEXT("command result revision differs from roster"), Mutated);
    Mutated = Command; Mutated.CommandResult.Id.AgentSerial = 999;
    Reject(*this, TEXT("accepted command agent absent from roster"), Mutated);
    Mutated = Command; Mutated.CommandResult.Outcome = ECkCrowdRemoteCommandOutcome::Stale;
    Reject(*this, TEXT("accepted command cannot carry rejected result"), Mutated);
    Mutated = Command; Mutated.Kind = ECkCrowdGym_RequestKind::Control;
    Reject(*this, TEXT("noncommand cannot hide command result"), Mutated);
    Mutated = Commit; Mutated.CommandResult.Diagnostic = TEXT("hidden");
    Reject(*this, TEXT("noncommand requires default result even when flag false"), Mutated);

    auto Rejected = Command;
    Rejected.Outcome = ECkCrowdGym_RequestOutcome::Busy;
    Rejected.CommandResult.Outcome = ECkCrowdRemoteCommandOutcome::Stale;
    Rejected.CommandResult.ResultSampleSequence = 0;
    Rejected.CommandResult.AppliedConfigurationRevision = 0;
    TestTrue(TEXT("busy command maps to neutral stale"),
        FCkCrowdGymWireValidation::IsValidCommit(Rejected));
    Mutated = Rejected; Mutated.CommandResult.Outcome = ECkCrowdRemoteCommandOutcome::Accepted;
    Reject(*this, TEXT("rejected command cannot claim neutral accepted"), Mutated);
    Mutated = Rejected; Mutated.CommandResult.Outcome = ECkCrowdRemoteCommandOutcome::Failed;
    Reject(*this, TEXT("rejected command outcome mapping exact"), Mutated);
    Mutated = Rejected; Mutated.bHasCommandResult = false; Mutated.CommandResult = {};
    TestTrue(TEXT("rejected malformed command can omit result"),
        FCkCrowdGymWireValidation::IsValidCommit(Mutated));
    Mutated.CommandResult.Diagnostic = TEXT("hidden");
    Reject(*this, TEXT("omitted result must be exactly default"), Mutated);

    struct FRejectedMapping
    {
        ECkCrowdGym_RequestOutcome CommitOutcome;
        ECkCrowdRemoteCommandOutcome NeutralOutcome;
    };
    const FRejectedMapping RejectedMappings[] = {
        {ECkCrowdGym_RequestOutcome::Invalid, ECkCrowdRemoteCommandOutcome::Invalid},
        {ECkCrowdGym_RequestOutcome::Stale, ECkCrowdRemoteCommandOutcome::Stale},
        {ECkCrowdGym_RequestOutcome::Busy, ECkCrowdRemoteCommandOutcome::Stale},
        {ECkCrowdGym_RequestOutcome::Unauthorized, ECkCrowdRemoteCommandOutcome::Unauthorized},
        {ECkCrowdGym_RequestOutcome::Gone, ECkCrowdRemoteCommandOutcome::Gone},
        {ECkCrowdGym_RequestOutcome::Failed, ECkCrowdRemoteCommandOutcome::Failed},
    };
    for (const auto& Mapping : RejectedMappings)
    {
        Rejected = Command;
        Rejected.Outcome = Mapping.CommitOutcome;
        Rejected.CommandResult.Outcome = Mapping.NeutralOutcome;
        Rejected.CommandResult.ResultSampleSequence = 0;
        Rejected.CommandResult.AppliedConfigurationRevision = 0;
        TestTrue(TEXT("nonaccepted command outcome maps exactly"),
            FCkCrowdGymWireValidation::IsValidCommit(Rejected));
        Rejected.CommandResult.Outcome = ECkCrowdRemoteCommandOutcome::Accepted;
        TestFalse(TEXT("mapped rejection cannot carry Accepted"),
            FCkCrowdGymWireValidation::IsValidCommit(Rejected));
    }

    // Detached neutral-envelope validation: synthetic wire values only, never private session state.
    FCkCrowdRemoteSelectionEnvelope E;
    E.WorldGeneration = Commit.Snapshot.WorldGeneration; E.SessionGeneration = 3;
    E.SampleSequence = Commit.Snapshot.SampleSequence; E.Rows = Commit.Snapshot.Rows;
    E.SelectedId = Commit.SelectedId; E.SelectionRequestSerial = Commit.SelectionRequestSerial;
    E.bHasDetail = Commit.bHasDetail; E.Detail = Commit.Detail; E.bRetainedDelivery = true;
    E.Receipt.WorldGeneration = E.WorldGeneration; E.Receipt.SessionGeneration = E.SessionGeneration;
    E.Receipt.DeliverySerial = Commit.DeliverySerial; E.Receipt.RequestSerial = Commit.RequestSerial;
    TestTrue(TEXT("complete neutral retained selection"), FCkCrowdRemoteDebugValidation::IsValid(E));
    TestFalse(TEXT("default neutral envelope rejects"), FCkCrowdRemoteDebugValidation::IsValid(FCkCrowdRemoteSelectionEnvelope{}));
    TestFalse(TEXT("default receipt rejects"), FCkCrowdRemoteDebugValidation::IsValid(FCkCrowdRemoteSelectionReceipt{}));
    auto RejectEnvelope = [this](const TCHAR* Name, const FCkCrowdRemoteSelectionEnvelope& Value)
    { TestFalse(Name, FCkCrowdRemoteDebugValidation::IsValid(Value)); };
    auto BadEnvelope = E; BadEnvelope.SessionGeneration = 0;
    RejectEnvelope(TEXT("default neutral session"), BadEnvelope);
    BadEnvelope = E; BadEnvelope.WorldGeneration = TNumericLimits<int64>::Max();
    RejectEnvelope(TEXT("overflow sentinel neutral generation"), BadEnvelope);
    BadEnvelope = E; BadEnvelope.Outcome = static_cast<ECkCrowdRemoteSelectionOutcome>(255);
    RejectEnvelope(TEXT("unknown neutral outcome"), BadEnvelope);
    BadEnvelope = E; BadEnvelope.Rows[1].Id = BadEnvelope.Rows[0].Id;
    RejectEnvelope(TEXT("neutral duplicate complete-row ID"), BadEnvelope);
    BadEnvelope = E; ++BadEnvelope.Rows[1].SampleSequence;
    RejectEnvelope(TEXT("neutral mixed sample epoch"), BadEnvelope);
    BadEnvelope = E; BadEnvelope.bHasDetail = false;
    RejectEnvelope(TEXT("neutral selected ID requires whole detail"), BadEnvelope);
    BadEnvelope = E; BadEnvelope.Detail.Sample.DebugName = TEXT("detached detail");
    RejectEnvelope(TEXT("neutral detail must match full selected row"), BadEnvelope);
    BadEnvelope = E; ++BadEnvelope.Receipt.SessionGeneration;
    RejectEnvelope(TEXT("neutral receipt session must match envelope"), BadEnvelope);
    BadEnvelope = E; ++BadEnvelope.Receipt.RequestSerial;
    RejectEnvelope(TEXT("Accepted neutral receipt must match selection serial"), BadEnvelope);
    BadEnvelope = E; BadEnvelope.Receipt.DeliverySerial = TNumericLimits<int64>::Max();
    RejectEnvelope(TEXT("overflow sentinel neutral delivery"), BadEnvelope);
    auto Continuous = E; Continuous.bRetainedDelivery = false; Continuous.Receipt = {};
    TestTrue(TEXT("complete neutral continuous selection has no receipt"), FCkCrowdRemoteDebugValidation::IsValid(Continuous));
    Continuous.Receipt = E.Receipt;
    RejectEnvelope(TEXT("continuous view cannot hide a receipt"), Continuous);
    for (const auto Outcome : {ECkCrowdRemoteSelectionOutcome::Invalid, ECkCrowdRemoteSelectionOutcome::Stale,
        ECkCrowdRemoteSelectionOutcome::Busy, ECkCrowdRemoteSelectionOutcome::Unauthorized,
        ECkCrowdRemoteSelectionOutcome::Gone, ECkCrowdRemoteSelectionOutcome::Failed})
    {
        auto RejectedEnvelope = E; RejectedEnvelope.Outcome = Outcome;
        RejectEnvelope(TEXT("neutral rejection cannot retain Accepted selection/detail"), RejectedEnvelope);
        RejectedEnvelope.SelectedId = {}; RejectedEnvelope.SelectionRequestSerial = 0;
        RejectedEnvelope.bHasDetail = false; RejectedEnvelope.Detail = {};
        TestTrue(TEXT("complete neutral rejection preserves exact receipt and full rows"), FCkCrowdRemoteDebugValidation::IsValid(RejectedEnvelope));
        RejectedEnvelope.Detail.Sample.DebugName = TEXT("hidden");
        RejectEnvelope(TEXT("neutral rejection cannot hide unused detail"), RejectedEnvelope);
    }

    return true;
}

#endif
