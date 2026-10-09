// Language=angelscript
// Fixed six-role station recipes. The local caller suffixes each tag with its
// controller name and owns the resulting station hierarchy; this catalog only
// supplies the unsuffixed authored identity and world-space geometry.
namespace CkCueGymCatalog
{
    const int32 Version = 1;
    const int32 RoleCount = 6;

    FString Get_Tag(int32 Slot)
    {
        if (Slot == 0) { return "Gym.Cue.Lifetime"; }
        if (Slot == 1) { return "Gym.Cue.Concurrency"; }
        if (Slot == 2) { return "Gym.Cue.OwnerValidation"; }
        if (Slot == 3) { return "Gym.Cue.Restart"; }
        if (Slot == 4) { return "Gym.Cue.Transient"; }
        if (Slot == 5) { return "Gym.Cue.OwnerDestruction"; }
        return "";
    }

    FString Get_Title(int32 Slot)
    {
        if (Slot == 0) { return "CUE LIFETIME"; }
        if (Slot == 1) { return "CUE CONCURRENCY"; }
        if (Slot == 2) { return "CUE OWNER VALIDATION"; }
        if (Slot == 3) { return "CUE RESTART"; }
        if (Slot == 4) { return "CUE TRANSIENT"; }
        if (Slot == 5) { return "CUE OWNER DESTRUCTION"; }
        return "";
    }

    bool TryGet_Station(int32 Slot, FCkGym_Station_SpawnParams_Payload&out Out)
    {
        if (Slot < 0 || Slot >= RoleCount) { return false; }
        FCkGym_Station_SpawnParams_Payload Recipe;
        Recipe.Tags.Add(FName(Get_Tag(Slot)));
        Recipe.Title = FText::FromString(Get_Title(Slot));
        if (Slot == 0)
        {
            Recipe.Description.Add(FText::FromString("Tests AfterOneFrame, Persistent, Timed, and Custom lifetime behaviors."));
            Recipe.Description.Add(FText::FromString("Each lifetime type controls when/how a cue entity is destroyed."));
        }
        else if (Slot == 1)
        {
            Recipe.Description.Add(FText::FromString("Compares AllowMultiple vs RestartExisting concurrency policies."));
            Recipe.Description.Add(FText::FromString("Left side stacks instances, right side restarts the existing one."));
        }
        else if (Slot == 2)
        {
            Recipe.Description.Add(FText::FromString("Tests SkipIfInvalid vs RequireValid with valid and invalid owners."));
            Recipe.Description.Add(FText::FromString("Both policies reject invalid owners; SkipIfInvalid is quiet, RequireValid reports the failure."));
        }
        else if (Slot == 3)
        {
            Recipe.Description.Add(FText::FromString("Tests the Restart() method via RestartExisting concurrency policy."));
            Recipe.Description.Add(FText::FromString("Re-fires every 3s - watch sphere color shift with each restart."));
        }
        else if (Slot == 4)
        {
            Recipe.Description.Add(FText::FromString("Tests cues executed without a persistent owner entity."));
            Recipe.Description.Add(FText::FromString("The Cue relay supplies an internal transient owner for delivery."));
        }
        else
        {
            Recipe.Description.Add(FText::FromString("Tests automatic cue cleanup when owning entity is destroyed."));
            Recipe.Description.Add(FText::FromString("Owner sphere + 3 cue spheres vanish together on owner destruction."));
        }

        // Explicit fixed grid: PanelCenter is at origin plus Z250 with these
        // exact dimensions. Local station anchors must still be read back.
        Recipe.Transform = FTransform::Identity;
        Recipe.Transform.SetLocation(FVector(500.0, -2000.0 + 800.0 * Slot, 0.0));
        Recipe.Transform.SetRotation(FRotator(0.0, 180.0, 0.0).Quaternion());
        Recipe.Width = 6.0f;
        Recipe.Depth = 5.0f;
        Recipe.Height = 5.0f;
        Recipe.AutoSize = false;
        Out = Recipe;
        return true;
    }

    bool TryGet_Origin(int32 Slot, FVector&out Out)
    {
        if (Slot < 0 || Slot >= RoleCount) { return false; }
        FCkGym_Station_SpawnParams_Payload Recipe;
        if (!TryGet_Station(Slot, Recipe)) { return false; }
        Out = Recipe.Transform.GetLocation() + FVector(0.0, 0.0, 250.0);
        return true;
    }
}
