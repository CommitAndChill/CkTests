// Language=angelscript

//============================================================================
// CK AUTOMATION TEST - NETWORKED INVENTORY: SOURCE DESTROYED IN THE SAME FRAME AS THE TRANSFER
//============================================================================
//
// The tightest ordering the harness can produce for the transferred lifetime on the client. As the relay-hosted row,
// but the server destroys the host from the transfer's own completion callback, so the target's
// container update and the host's destroy leave the server in the same frame. If the client receives
// both together, the destroy cascades into the item before the inventory diff can move its lifetime.
//
// Surface: Ck.Inventory.Net.AS_DataOnly_TransferredItemOutlivesSameFrameSourceDestroy
//============================================================================

class UCk_AutoTest_Net_DataOnly_TransferredItemOutlivesSameFrameSourceDestroy : UCk_AutoTest_Net_DataOnly_TransferredItemOutlivesRelayHostedSource
{
    protected bool Get_DestroysHostOnTransferComplete() const override
    {
        return true;
    }
}
