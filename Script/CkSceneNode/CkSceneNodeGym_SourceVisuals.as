// Source-world visuals only. The native ledger sets the private role claim
// before BeginPlay. These actors do not create ECS entities or run timers.
namespace CkSceneNodeGymSourceVisuals
{
    FString Get_ParentLabel(int32 Role)
    {
        if (Role == 1) { return "ParentChild"; }
        if (Role == 3) { return "OffsetUpdates"; }
        if (Role == 5) { return "MultipleChildren"; }
        if (Role == 10) { return "Hierarchy"; }
        if (Role == 13) { return "PropagateOnly"; }
        if (Role == 16) { return "AnchorOffset"; }
        return "";
    }

    FString Get_ChildLabel(int32 Role)
    {
        if (Role == 2 || Role == 11) { return "Child"; }
        if (Role == 4) { return "Offset"; }
        if (Role >= 6 && Role <= 9) { return f"Child {Role - 5}"; }
        if (Role == 12) { return "Grandchild"; }
        if (Role == 14) { return "Planet"; }
        if (Role == 15) { return "Moon"; }
        if (Role == 17) { return "Anchored"; }
        return "";
    }
}

UCLASS(Blueprintable)
class ACk_SceneNodeGym_SourceParent : ACk_SceneNodeGym_SourceActor_UE
{
    default bReplicates = true;
    default bReplicateMovement = false;

    UPROPERTY(DefaultComponent, RootComponent)
    UStaticMeshComponent Mesh;
    default Mesh.StaticMesh = Cast<UStaticMesh>(utils_i_o::LoadAssetByName(
        "/Engine/BasicShapes/Cube.Cube", ECk_AssetSearchScope::Engine, ECk_AssetSearchStrategy::ExactOnly)._Asset);
    default Mesh.CollisionEnabled = ECollisionEnabled::NoCollision;
    default Mesh.Mobility = EComponentMobility::Movable;
    default Mesh.SetVisibility(false);

    UPROPERTY(DefaultComponent)
    UTextRenderComponent TextRenderer;
    default TextRenderer.RelativeLocation = FVector(0.0f, 0.0f, 75.0f);
    default TextRenderer.SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    default TextRenderer.WorldSize = 40.0f;
    default TextRenderer.TextRenderColor = FColor::Orange;
    default TextRenderer.SetVisibility(false);

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto Label = CkSceneNodeGymSourceVisuals::Get_ParentLabel(Get_ActorRole());
        if (Get_ActorGeneration() <= 0 ||
            Label.IsEmpty()) { return; }
        if (ck::Is_NOT_Valid(TextRenderer)) { return; }
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld) || TextRenderer.GetOwner() != this ||
            TextRenderer.GetWorld() != CurrentWorld) { return; }
        TextRenderer.SetText(ck::Text(Label));
    }
}

UCLASS(Blueprintable)
class ACk_SceneNodeGym_SourceChild : ACk_SceneNodeGym_SourceActor_UE
{
    default bReplicates = true;
    default bReplicateMovement = false;

    UPROPERTY(DefaultComponent, RootComponent)
    UStaticMeshComponent Mesh;
    default Mesh.StaticMesh = Cast<UStaticMesh>(utils_i_o::LoadAssetByName(
        "/Engine/BasicShapes/Cube.Cube", ECk_AssetSearchScope::Engine, ECk_AssetSearchStrategy::ExactOnly)._Asset);
    default Mesh.CollisionEnabled = ECollisionEnabled::NoCollision;
    default Mesh.Mobility = EComponentMobility::Movable;
    default Mesh.SetVisibility(false);

    UPROPERTY(DefaultComponent)
    UTextRenderComponent TextRenderer;
    default TextRenderer.RelativeLocation = FVector(0.0f, 0.0f, 75.0f);
    default TextRenderer.SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    default TextRenderer.WorldSize = 30.0f;
    default TextRenderer.TextRenderColor = FColor::Cyan;
    default TextRenderer.SetVisibility(false);

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto Label = CkSceneNodeGymSourceVisuals::Get_ChildLabel(Get_ActorRole());
        if (Get_ActorGeneration() <= 0 ||
            Label.IsEmpty()) { return; }
        if (ck::Is_NOT_Valid(TextRenderer)) { return; }
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld) || TextRenderer.GetOwner() != this ||
            TextRenderer.GetWorld() != CurrentWorld) { return; }
        TextRenderer.SetText(ck::Text(Label));
    }
}
