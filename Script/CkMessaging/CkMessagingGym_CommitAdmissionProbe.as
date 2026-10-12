// Language=angelscript

// Test-only bridge to the ordinary production method; no prepared state is supplied.
UCLASS()
class UCk_MessagingGym_CommitAdmissionProbe : UObject
{
    UPROPERTY() bool AdmissionUnchanged = false;

    private int32 CountReservations(ACk_MessagingGym_PlayerController InPC,
        int32 InSerial, int32 InGeneration) const
    {
        int32 Count = 0;
        for (int32 Row = 1; Row <= 10; ++Row)
        {
            if (Row == 8) { continue; }
            if (Row == 6)
            {
                if (InPC.Get_HasReservedMessagingControl(InSerial, Row, 1, InGeneration)) { ++Count; }
                if (InPC.Get_HasReservedMessagingControl(InSerial, Row, 5, InGeneration)) { ++Count; }
                if (InPC.Get_HasReservedMessagingControl(InSerial, Row, 9, InGeneration)) { ++Count; }
            }
            else if (InPC.Get_HasReservedMessagingControl(InSerial, Row, 0, InGeneration)) { ++Count; }
        }
        return Count;
    }

    UFUNCTION()
    bool TryRecordCommit(ACk_MessagingGym_PlayerController InPC,
        ACk_MessagingGym_GameState InState, int32 InSerial)
    {
        AdmissionUnchanged = false;
        if (ck::Is_NOT_Valid(InPC)) { return false; }
        auto World = InPC.GetWorld();
        if (ck::Is_NOT_Valid(World)) { return false; }
        auto CurrentState = Cast<ACk_MessagingGym_GameState>(World.GetGameState());
        if (ck::Is_NOT_Valid(CurrentState)) { return false; }
        int32 Generation = CurrentState.Snapshot.Generation;
        int32 NextDelivery = InPC.Get_NextDeliverySerial();
        bool CanAccept = InPC.Get_CanAcceptOwnerCommit();
        bool Submitting = InPC.Get_IsSubmittingMessagingControl(InSerial);
        int32 Reservations = CountReservations(InPC, InSerial, Generation);
        bool Accepted = InPC.Request_TryRecordCommit(InState, InSerial);
        AdmissionUnchanged = !Submitting && Reservations == 0 &&
            InPC.Get_NextDeliverySerial() == NextDelivery &&
            InPC.Get_CanAcceptOwnerCommit() == CanAccept &&
            InPC.Get_IsSubmittingMessagingControl(InSerial) == Submitting &&
            CountReservations(InPC, InSerial, Generation) == Reservations;
        return Accepted;
    }
}
