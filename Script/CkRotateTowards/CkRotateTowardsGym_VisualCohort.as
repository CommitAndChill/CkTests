// A controller observes the shared production replicas; it never owns or poses them.
USTRUCT()
struct FCkRotateTowardsGym_VisualCohort
{
    UPROPERTY() TArray<FCk_Handle_Transform> Roots;
    UPROPERTY() TArray<FCk_Handle_UnrealComponent> Components;
    private UWorld World;
    private int32 Generation = 0;
    private int32 InstalledSequence = 0;
    private int32 Phase = 0;
    int32 Get_Phase() const { return Phase; }
    bool Get_IsRetirementUnproven() const { return false; }
    FString Get_LastInstallFailure() const { return "production replica cohort unavailable"; }
    bool TryBegin(UWorld InWorld, FCk_Handle InPCEntity, FCkRotateTowardsGym_Snapshot S,
        FCk_Delegate_Request_OnCompleted OnPushDisabled,
        FCk_Delegate_UnrealComponent_OnAdded OnAdded)
    {
        if (Phase != 0 || ck::Is_NOT_Valid(InWorld) || !S.Ready || S.Failed ||
            S.Generation <= 0 || S.Visuals.Num() != 11 || !CkRotateTowardsShared::IsLive(InPCEntity)) { return false; }
        World = InWorld; Generation = S.Generation; Phase = 1;
        Roots.SetNum(11); Components.SetNum(11); return true;
    }
    bool TryRecord_PushDisabled(FCk_Handle H, ECk_Request_OperationResult Result) { return false; }
    bool TryRecord_Added(FCk_Handle_UnrealComponent H) { return false; }
    bool Get_IsReady()
    {
        if ((Phase != 1 && Phase != 2) || ck::Is_NOT_Valid(World)) { return false; }
        for (int32 i = 0; i < 11; ++i)
        {
            auto H = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(World, Generation, i);
            auto Mesh = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCubeMesh(World, Generation, i);
            if (ck::Is_NOT_Valid(H) || ck::Is_NOT_Valid(Mesh)) { return false; }
            auto Script = Cast<UCk_EntityScript_RotateTowardsGym_Cube>(utils_entity_script::TryGet_EntityScript(H));
            if (ck::Is_NOT_Valid(Script) || !Script.Get_IsReady()) { return false; }
            if (Roots[i] != FCk_Handle_Transform() && FCk_Handle(Roots[i]) != H) { return false; }
            Roots[i] = H.As_Transform(); Components[i] = Script.Get_Component();
        }
        return true;
    }
    bool TryInstall(FCkRotateTowardsGym_Snapshot S)
    {
        if (!S.Ready || S.Failed || S.Generation != Generation || S.Sequence <= InstalledSequence ||
            S.Visuals.Num() != 11 || !Get_IsReady()) { return false; }
        InstalledSequence = S.Sequence; Phase = 2; return true;
    }
    bool Get_HasExactInstalledVisuals(FCkRotateTowardsGym_Snapshot S)
    { return S.Generation == Generation && S.Sequence == InstalledSequence && Get_IsReady(); }
    void Request_Retire()
    {
        // Lifetime belongs to the replicated source, including on source loss.
        Roots.Empty(); Components.Empty(); World = nullptr;
        Generation = 0; InstalledSequence = 0; Phase = 0;
    }
    bool PollRetirement() const { return Phase == 0; }
}
