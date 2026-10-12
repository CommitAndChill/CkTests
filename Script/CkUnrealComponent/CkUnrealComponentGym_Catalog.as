// Language=angelscript
// Shared station recipes and source anchors in the authored role order.
namespace CkUnrealComponentGymCatalog
{
    const int32 RoleCount = 8;

    // Matches the existing ECk_UnrealComponentGym_Type ordinal contract.
    FString Get_Tag(int32 Slot)
    {
        if (Slot == 0) { return "Gym.UnrealComponent.StaticMesh"; }
        if (Slot == 1) { return "Gym.UnrealComponent.PointLight"; }
        if (Slot == 2) { return "Gym.UnrealComponent.SpotLight"; }
        if (Slot == 3) { return "Gym.UnrealComponent.TextRender"; }
        if (Slot == 4) { return "Gym.UnrealComponent.Arrow"; }
        if (Slot == 5) { return "Gym.UnrealComponent.Box"; }
        if (Slot == 6) { return "Gym.UnrealComponent.Sphere"; }
        if (Slot == 7) { return "Gym.UnrealComponent.Capsule"; }
        return "";
    }

    FString Get_Title(int32 Slot)
    {
        if (Slot == 0) { return "STATIC MESH"; }
        if (Slot == 1) { return "POINT LIGHT"; }
        if (Slot == 2) { return "SPOT LIGHT"; }
        if (Slot == 3) { return "TEXT RENDER"; }
        if (Slot == 4) { return "ARROW"; }
        if (Slot == 5) { return "BOX"; }
        if (Slot == 6) { return "SPHERE"; }
        if (Slot == 7) { return "CAPSULE"; }
        return "";
    }

    bool TryGet_Station(int32 Slot, FCkGym_Station_SpawnParams_Payload&out Out)
    {
        Out = FCkGym_Station_SpawnParams_Payload();
        if (Slot < 0 || Slot >= RoleCount) { return false; }
        Out.Tags.Add(FName(Get_Tag(Slot)));
        Out.Title = FText::FromString(Get_Title(Slot));
        if (Slot == 0)
        {
            Out.Description.Add(FText::FromString("Attaches a UStaticMeshComponent (engine cube) to an ECS entity."));
            Out.Description.Add(FText::FromString("Component is registered against the ComponentHost subsystem."));
        }
        else if (Slot == 1)
        {
            Out.Description.Add(FText::FromString("Attaches a UPointLightComponent that orbits around the station."));
            Out.Description.Add(FText::FromString("Demonstrates lit-component lifecycle on the shared component host."));
        }
        else if (Slot == 2)
        {
            Out.Description.Add(FText::FromString("Attaches a USpotLightComponent and steers it via entity rotation."));
            Out.Description.Add(FText::FromString("PushTransform processor drives the component's world transform."));
        }
        else if (Slot == 3)
        {
            Out.Description.Add(FText::FromString("Attaches a UTextRenderComponent that orbits and rotates with the entity."));
            Out.Description.Add(FText::FromString("Demonstrates a non-light scene component reacting to transform updates."));
        }
        else if (Slot == 4)
        {
            Out.Description.Add(FText::FromString("Attaches a UArrowComponent (debug visual) following the entity."));
            Out.Description.Add(FText::FromString("Useful for visualising entity facing without a mesh."));
        }
        else if (Slot == 5)
        {
            Out.Description.Add(FText::FromString("Attaches a UBoxComponent (collision shape) to the entity."));
            Out.Description.Add(FText::FromString("Demonstrates a query-only collider on the shared component host."));
        }
        else if (Slot == 6)
        {
            Out.Description.Add(FText::FromString("Attaches a USphereComponent (collision shape) to the entity."));
            Out.Description.Add(FText::FromString("Sphere radius is set in the OnAdded callback."));
        }
        else
        {
            Out.Description.Add(FText::FromString("Attaches a UCapsuleComponent (collision shape) to the entity."));
            Out.Description.Add(FText::FromString("Capsule dimensions set in OnAdded; visible thanks to SetHiddenInGame(false)."));
        }
        // Explicit version of the former seven-column default grid. Fixed
        // dimensions keep the authored PanelCenter at Origin + Z250.
        Out.Transform = FTransform::Identity;
        Out.Transform.SetLocation(FVector(Slot == 7 ? 1300.0 : 500.0,
            Slot == 7 ? 0.0 : -2400.0 + 800.0 * Slot, 0.0));
        Out.Transform.SetRotation(FRotator(0.0, 180.0, 0.0).Quaternion());
        Out.Width = 6.0f;
        Out.Depth = 5.0f;
        Out.Height = 5.0f;
        Out.AutoSize = false;
        return true;
    }

    bool TryGet_Origin(int32 Slot, FVector&out Out)
    {
        Out = FVector::ZeroVector;
        FCkGym_Station_SpawnParams_Payload Recipe;
        if (!TryGet_Station(Slot, Recipe)) { return false; }
        Out = Recipe.Transform.GetLocation() + FVector(0.0, 0.0, 250.0);
        return true;
    }
}
