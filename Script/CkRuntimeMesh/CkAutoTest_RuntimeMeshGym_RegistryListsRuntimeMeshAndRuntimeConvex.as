// Language=angelscript

// The two runtime-mesh gyms are reachable from the switchboard: after the CkTests registration pass, each display
// name resolves to an entry grouped under its owning module. Checked by name and category only, so this test does not
// depend on either gym's classes.
class UCk_AutoTest_RuntimeMeshGym_RegistryListsRuntimeMeshAndRuntimeConvex : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;
    default _AutoStageOriginField = false;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        CkTests_Gyms::RegisterAll();

        DoAssert_Registered("Runtime Mesh", "CkRuntimeMesh");
        DoAssert_Registered("Runtime Convex", "CkJolt");

        FinishSuccess();
    }

    private void DoAssert_Registered(FString InDisplayName, FString InCategory)
    {
        const auto Index = CkGym_Cycler::Find_GymIndexByName(InDisplayName);
        Assert_True(Index >= 0, f"CkTests_Gyms::RegisterAll registers the '{InDisplayName}' gym");
        if (Index < 0)
        {
            return;
        }

        const auto Registry = CkGym_Cycler::Get_GymRegistry();
        Assert_True(Registry.IsValidIndex(Index), f"the index found for '{InDisplayName}' is inside the registry");
        if (Registry.IsValidIndex(Index) == false)
        {
            return;
        }
        Assert_Equals_String(Registry[Index].Category, InCategory,
            f"'{InDisplayName}' is grouped under its owning module on the switchboard");
    }
}
