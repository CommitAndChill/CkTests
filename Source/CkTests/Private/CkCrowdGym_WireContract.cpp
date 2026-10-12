#include "CkCrowdGym_WireContract.h"

#include <UObject/UnrealType.h>

namespace ck_crowd_gym_wire
{
    auto IsPositiveSerial(int64 Value) -> bool
    { return Value > 0 && Value < TNumericLimits<int64>::Max(); }

    auto IsDefaultId(const FCkCrowdRemoteAgentId& Id) -> bool
    { return Id.WorldGeneration == 0 && Id.AgentSerial == 0; }

    auto IsDefaultDetail(const FCkCrowdRemoteAgentDetail& Detail) -> bool
    {
        const FCkCrowdRemoteAgentDetail Default;
        return FCkCrowdRemoteAgentDetail::StaticStruct()->CompareScriptStruct(
            &Detail, &Default, PPF_None);
    }

    auto IsDefaultResult(const FCkCrowdRemoteCommandResult& Result) -> bool
    {
        const FCkCrowdRemoteCommandResult Default;
        return FCkCrowdRemoteCommandResult::StaticStruct()->CompareScriptStruct(
            &Result, &Default, PPF_None);
    }

    auto IsValidKind(ECkCrowdGym_RequestKind Kind) -> bool
    {
        switch (Kind)
        {
        case ECkCrowdGym_RequestKind::Control:
        case ECkCrowdGym_RequestKind::Selection:
        case ECkCrowdGym_RequestKind::Command: return true;
        default: return false;
        }
    }

    auto IsValidOutcome(ECkCrowdGym_RequestOutcome Outcome) -> bool
    {
        switch (Outcome)
        {
        case ECkCrowdGym_RequestOutcome::Accepted:
        case ECkCrowdGym_RequestOutcome::Invalid:
        case ECkCrowdGym_RequestOutcome::Stale:
        case ECkCrowdGym_RequestOutcome::Busy:
        case ECkCrowdGym_RequestOutcome::Unauthorized:
        case ECkCrowdGym_RequestOutcome::Gone:
        case ECkCrowdGym_RequestOutcome::Failed: return true;
        default: return false;
        }
    }

    auto MatchesRejectedCommandOutcome(
        ECkCrowdGym_RequestOutcome CommitOutcome,
        ECkCrowdRemoteCommandOutcome ResultOutcome) -> bool
    {
        switch (CommitOutcome)
        {
        case ECkCrowdGym_RequestOutcome::Invalid:
            return ResultOutcome == ECkCrowdRemoteCommandOutcome::Invalid;
        case ECkCrowdGym_RequestOutcome::Stale:
        case ECkCrowdGym_RequestOutcome::Busy:
            return ResultOutcome == ECkCrowdRemoteCommandOutcome::Stale;
        case ECkCrowdGym_RequestOutcome::Unauthorized:
            return ResultOutcome == ECkCrowdRemoteCommandOutcome::Unauthorized;
        case ECkCrowdGym_RequestOutcome::Gone:
            return ResultOutcome == ECkCrowdRemoteCommandOutcome::Gone;
        case ECkCrowdGym_RequestOutcome::Failed:
            return ResultOutcome == ECkCrowdRemoteCommandOutcome::Failed;
        default: return false;
        }
    }

    auto FindRow(const FCkCrowdGym_RowsSnapshot& Snapshot,
        const FCkCrowdRemoteAgentId& Id) -> const FCkCrowdRemoteAgentRow*
    {
        for (const FCkCrowdRemoteAgentRow& Row : Snapshot.Rows)
        { if (Row.Id == Id) { return &Row; } }
        return nullptr;
    }
}

auto
    FCkCrowdGymWireValidation::
    IsValidReadySnapshot(const FCkCrowdGym_RowsSnapshot& Snapshot)
    -> bool
{
    if (!Snapshot.bReady ||
        !ck_crowd_gym_wire::IsPositiveSerial(Snapshot.WorldGeneration) ||
        !ck_crowd_gym_wire::IsPositiveSerial(Snapshot.SampleSequence))
    { return false; }

    TSet<FCkCrowdRemoteAgentId> Seen;
    for (const FCkCrowdRemoteAgentRow& Row : Snapshot.Rows)
    {
        if (!FCkCrowdRemoteDebugValidation::IsValid(Row) ||
            Row.Id.WorldGeneration != Snapshot.WorldGeneration ||
            Row.SampleSequence != Snapshot.SampleSequence || Seen.Contains(Row.Id))
        { return false; }
        Seen.Add(Row.Id);
    }
    return true;
}

auto
    FCkCrowdGymWireValidation::
    IsValidCommit(const FCkCrowdGym_OwnerCommit& Commit)
    -> bool
{
    using namespace ck_crowd_gym_wire;
    if (!IsPositiveSerial(Commit.DeliverySerial) ||
        !IsPositiveSerial(Commit.RequestSerial) ||
        !IsValidKind(Commit.Kind) || !IsValidOutcome(Commit.Outcome) ||
        !IsValidReadySnapshot(Commit.Snapshot) ||
        Commit.SelectionRequestSerial < 0)
    { return false; }

    const FCkCrowdRemoteAgentRow* SelectedRow = nullptr;
    if (IsDefaultId(Commit.SelectedId))
    {
        if (Commit.bHasDetail || !IsDefaultDetail(Commit.Detail))
        { return false; }
    }
    else
    {
        if (!Commit.SelectedId.IsValid() ||
            Commit.SelectionRequestSerial <= 0 || !Commit.bHasDetail)
        { return false; }
        SelectedRow = FindRow(Commit.Snapshot, Commit.SelectedId);
        if (SelectedRow == nullptr ||
            !FCkCrowdRemoteDebugValidation::IsValid(Commit.Detail) ||
            !(Commit.Detail.Id == Commit.SelectedId) ||
            Commit.Detail.SelectionRequestSerial != Commit.SelectionRequestSerial ||
            Commit.Detail.SampleSequence != Commit.Snapshot.SampleSequence ||
            Commit.Detail.ConfigurationRevision != SelectedRow->ConfigurationRevision ||
            !FCkCrowdRemoteAgentRow::StaticStruct()->CompareScriptStruct(
                &Commit.Detail.Sample, SelectedRow, PPF_None))
        { return false; }
    }

    if (Commit.Kind == ECkCrowdGym_RequestKind::Selection &&
        Commit.Outcome == ECkCrowdGym_RequestOutcome::Accepted &&
        Commit.SelectionRequestSerial != Commit.RequestSerial)
    { return false; }

    if (Commit.Kind != ECkCrowdGym_RequestKind::Command)
    { return !Commit.bHasCommandResult && IsDefaultResult(Commit.CommandResult); }

    if (!Commit.bHasCommandResult)
    {
        // Malformed command identifiers need not have a neutral result.
        return Commit.Outcome != ECkCrowdGym_RequestOutcome::Accepted &&
            IsDefaultResult(Commit.CommandResult);
    }

    const auto& Result = Commit.CommandResult;
    if (!FCkCrowdRemoteDebugValidation::IsValid(Result) ||
        Result.ClientSerial != Commit.RequestSerial)
    { return false; }
    if (Commit.Outcome != ECkCrowdGym_RequestOutcome::Accepted)
    { return MatchesRejectedCommandOutcome(Commit.Outcome, Result.Outcome); }

    const FCkCrowdRemoteAgentRow* CommandRow = FindRow(Commit.Snapshot, Result.Id);
    return Result.Outcome == ECkCrowdRemoteCommandOutcome::Accepted &&
        CommandRow != nullptr &&
        Result.AppliedConfigurationRevision == CommandRow->ConfigurationRevision &&
        Result.ResultSampleSequence == Commit.Snapshot.SampleSequence;
}
