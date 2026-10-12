// Language=angelscript
// Test-only bridge to the real per-world Common admission slot.
UCLASS()
class UCk_UnrealComponentGym_AdmissionProbe : UObject
{
    UFUNCTION()
    bool ClosedAndReserveRejected(FCk_Handle InWorld)
    {
        auto World = ck::TransientEntity();
        return ck::IsValid(World) && World == InWorld &&
            CkGym_Admission::Get_IsClosedUnproven(World) &&
            CkGym_Admission::TryReserve(World) == 0;
    }
}
