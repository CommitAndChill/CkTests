UCLASS(Blueprintable)
class ACk_TweenTest_GymActor : AActor
{
    default bReplicates = true;
    default bReplicateUsingRegisteredSubObjectList = true;
    default bAlwaysRelevant = true;
    default bReplicateMovement = true;

    UPROPERTY(DefaultComponent)
    UStaticMeshComponent Mesh;
    default Mesh.StaticMesh = Cast<UStaticMesh>(utils_i_o::LoadAssetByName("Cube1", ECk_AssetSearchScope::Engine, ECk_AssetSearchStrategy::ExactOnly)._Asset);
    default Mesh.CollisionEnabled = ECollisionEnabled::NoCollision;

    UPROPERTY(DefaultComponent)
    UTextRenderComponent TextRenderer;
    default TextRenderer.RelativeLocation = FVector(0.0f, 0.0f, 75.0f);
    default TextRenderer.SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    default TextRenderer.WorldSize = 40.0f;
    default TextRenderer.TextRenderColor = FColor::Orange;

    UPROPERTY(DefaultComponent)
    UCk_EntityBridge_ActorComponent_UE EntityBridge;

    UPROPERTY(ExposeOnSpawn, ReplicatedUsing=OnTextUpdated)
    ECk_TweenEasing TweenEasingMethod = ECk_TweenEasing::Linear;

    UPROPERTY(ExposeOnSpawn)
    float32 TweenDuration = 1.0f;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();
        TextRenderer.SetText(ck::Text(f"{TweenEasingMethod}"));

        EntityBridge.Request_SpawnLinkedEntity(UCk_TweenTest_GymActor_EntityScript);
    }

    UFUNCTION()
    void OnTextUpdated()
    {
        TextRenderer.SetText(ck::Text(f"{TweenEasingMethod}"));
    }

    UPROPERTY()
    FVector StartLocation = FVector::ZeroVector;

    UPROPERTY()
    FVector EndLocation = FVector::ZeroVector;

    UPROPERTY()
    FCk_Handle_Tween TweenHandle;
};

// Composes the actor's entity on the server: a yoyo location tween 200 units up from where the actor stands.
class UCk_TweenTest_GymActor_EntityScript : UCk_EntityScript_WithActor_UE
{
    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto Actor = Cast<ACk_TweenTest_GymActor>(DoGet_Actor());
        if (!Actor.HasAuthority())
        { return ECk_EntityScript_ConstructionFlow::Finished; }

        if (System::IsServer() == false)
        { return ECk_EntityScript_ConstructionFlow::Finished; }

        InHandle.Set_DebugName(n"TweenCube");
        Actor.StartLocation = Actor.GetActorLocation();
        Actor.EndLocation = Actor.StartLocation + FVector(0.0f, 0.0f, 200.0f);
        Actor.TweenHandle = utils_tween::Create_TweenEntityLocation(InHandle.As_Transform(), Actor.EndLocation, Actor.TweenDuration,
            Actor.TweenEasingMethod, ECk_TweenLoopType::Yoyo, -1, 0.0f);

        return ECk_EntityScript_ConstructionFlow::Finished;
    }
};
