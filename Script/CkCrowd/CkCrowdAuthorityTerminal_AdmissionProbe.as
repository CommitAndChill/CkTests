// Language=angelscript
// Test-only bridge to the real per-world Common admission slot.
UCLASS()
class UCk_CrowdAuthorityTerminal_AdmissionProbe : UObject
{
    UFUNCTION()
    bool AdmissionOpen(FCk_Handle InWorld)
    {
        auto World = ck::TransientEntity();
        return ck::IsValid(World) && World == InWorld &&
            !CkGym_Admission::Get_IsClosedUnproven(World) &&
            !CkGym_Admission::Get_IsBusy(World) &&
            utils_entity_lifetime::Get_CanCreateEntity(World);
    }

    UFUNCTION()
    bool ClosedAndReserveRejected(FCk_Handle InWorld)
    {
        auto World = ck::TransientEntity();
        return ck::IsValid(World) && World == InWorld &&
            CkGym_Admission::Get_IsClosedUnproven(World) &&
            CkGym_Admission::TryReserve(World) == 0;
    }
}
