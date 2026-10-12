// Language=angelscript

// Called by the native two-client probe after a world-owned provider is registered.
UCLASS()
class UCk_CrowdRemoteDebug_ASProbe : UObject
{
    UFUNCTION()
    bool ProbeValid(APlayerController InPC, FCkCrowdRemoteCommandRequest InRequest)
    {
        TArray<FCkCrowdRemoteAgentRow> Rows;
        if (!utils_crowd_remote_debug::TryCopyRows(InPC, Rows) || Rows.Num() != 80)
        { return false; }
        if (Rows[0].Id.WorldGeneration != InRequest.Id.WorldGeneration ||
            Rows[0].Id.AgentSerial != InRequest.Id.AgentSerial ||
            !Rows[0].bHasDebugOverride || Rows[1].bHasDebugOverride ||
            Rows[0].SampleSequence != 7 || Rows[0].ConfigurationRevision != 2)
        { return false; }

        if (!utils_crowd_remote_debug::TryRequestSelection(
            InPC, InRequest.Id, 41, false))
        { return false; }
        FCkCrowdRemoteAgentDetail Detail;
        if (!utils_crowd_remote_debug::TryCopySelectedDetail(
            InPC, InRequest.Id, 41, Detail))
        { return false; }
        if (Detail.SelectionRequestSerial != 41 ||
            Detail.Id.WorldGeneration != InRequest.Id.WorldGeneration ||
            Detail.Id.AgentSerial != InRequest.Id.AgentSerial ||
            Detail.Sample.Id.AgentSerial != InRequest.Id.AgentSerial ||
            !Detail.Sample.bHasDebugOverride ||
            Detail.Neighbors.Num() != 80 || Detail.PlannedPath.Num() != 80)
        { return false; }

        if (!utils_crowd_remote_debug::TrySubmit(InPC, InRequest))
        { return false; }
        FCkCrowdRemoteCommandResult Result;
        if (!utils_crowd_remote_debug::TryCopyCommandResult(
            InPC, InRequest.ClientSerial, Result))
        { return false; }
        return Result.Id.WorldGeneration == InRequest.Id.WorldGeneration &&
            Result.Id.AgentSerial == InRequest.Id.AgentSerial &&
            Result.ClientSerial == InRequest.ClientSerial &&
            Result.Outcome == ECkCrowdRemoteCommandOutcome::Accepted &&
            Result.AppliedConfigurationRevision == 3 &&
            Result.ResultSampleSequence == 8;
    }

    UFUNCTION()
    bool ProbeInvalidPC(APlayerController InPC, FCkCrowdRemoteCommandRequest InRequest)
    {
        TArray<FCkCrowdRemoteAgentRow> Rows;
        FCkCrowdRemoteAgentDetail Detail;
        FCkCrowdRemoteCommandResult Result;
        auto RowsAccepted = utils_crowd_remote_debug::TryCopyRows(InPC, Rows);
        auto SelectionAccepted = utils_crowd_remote_debug::TryRequestSelection(
            InPC, InRequest.Id, 41, false);
        auto DetailAccepted = utils_crowd_remote_debug::TryCopySelectedDetail(
            InPC, InRequest.Id, 41, Detail);
        auto CommandAccepted = utils_crowd_remote_debug::TrySubmit(InPC, InRequest);
        auto ResultAccepted = utils_crowd_remote_debug::TryCopyCommandResult(
            InPC, InRequest.ClientSerial, Result);
        return !RowsAccepted && Rows.Num() == 0 &&
            !SelectionAccepted && !DetailAccepted && Detail.Id.AgentSerial == 0 &&
            !CommandAccepted && !ResultAccepted && Result.Id.AgentSerial == 0;
    }
}
