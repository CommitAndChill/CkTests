// Language=angelscript

//============================================================================
// CK AUTOMATION TEST - NETWORKED INVENTORY: A TRANSFERRED ITEM OUTLIVES A SOURCE ON ANOTHER ROOT
//============================================================================
//
// Same as CkAutoTest_Net_DataOnly_TransferredItemOutlivesSourceOwner, except the host is spawned under
// an ActorRelay channel entity, as world items are, instead of under the subject. The host's destroy
// and the target's container update then ride different replicated roots, with no ordering between
// them on the client.
//
// Surface: Ck.Inventory.Net.AS_DataOnly_TransferredItemOutlivesRelayHostedSource
//============================================================================

class UCk_AutoTest_Net_DataOnly_TransferredItemOutlivesRelayHostedSource : UCk_AutoTest_Net_DataOnly_TransferredItemOutlivesSourceOwner
{
    private FCk_Handle_PendingActorRelay _PendingChannel;

    protected void Request_SpawnHost(FCk_Handle InSubject) override
    {
        _PendingChannel = utils_actor_relay::Request_AcquireChannel(GameplayTags::ActorRelay_Generic);
        _PendingChannel.Promise_OnAcquired(FCk_Delegate_ActorRelay_Acquired(this, n"OnChannelAcquired"));
    }

    UFUNCTION()
    private void OnChannelAcquired(FCk_ActorRelay_ChannelResult InResult)
    {
        _PendingChannel = FCk_Handle_PendingActorRelay();

        // The channel is pooled and shared; only the host spawned under it is tracked for cleanup.
        auto Channel = InResult.Get_ChannelEntity();
        if (ck::Is_NOT_Valid(Channel))
        { FinishFailure("server: ActorRelay channel acquired but its entity is invalid"); return; }

        Request_SpawnHostUnder(Channel);
    }
}
