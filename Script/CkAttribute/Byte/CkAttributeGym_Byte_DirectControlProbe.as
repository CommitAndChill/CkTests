// Language=angelscript
// Test-only reflected bridge to the real plain-AngelScript GameState control entry.
UCLASS()
class UCk_AttributeByte_DirectControlProbe : UObject
{
    UFUNCTION()
    int32 Invoke(ACk_ByteAttributeGym_GameState InState,
        ACk_ByteAttributeGym_PlayerController InOwner,
        int32 InSerial, int32 InAction, int32 InPreset)
    {
        if (ck::Is_NOT_Valid(InState) || ck::Is_NOT_Valid(InOwner) ||
            !InState.HasAuthority() || InState.GetWorld() != InOwner.GetWorld() ||
            !InState.Get_HasReadyServerChildren()) { return -1; }
        return InState.Request_ApplyControl(InOwner, InSerial, InAction, InPreset) ? 1 : 0;
    }
}
