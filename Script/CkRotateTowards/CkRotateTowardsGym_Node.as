// Language=angelscript

class UCk_EntityScript_RotateTowardsGym_WorldRoot : UCk_EntityScript_WithActor_UE
{
    default _Replication = ECk_Replication::Replicates;
}

USTRUCT()
struct FCkRotateTowardsGym_OwnerParams
{
    UPROPERTY() int32 Generation = 0;
}

class UCk_EntityScript_RotateTowardsGym_Owner : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::Replicates;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        if (Generation <= 0)
        { utils_entity_lifetime::Request_DestroyEntity(InHandle); }
        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}

USTRUCT()
struct FCkRotateTowardsGym_CubeParams
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Slot = -1;
    UPROPERTY() int32 Station = -1;
    UPROPERTY() FTransform InitialTransform;
}

class UCk_EntityScript_RotateTowardsGym_Cube : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::Replicates;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    UPROPERTY(ExposeOnSpawn) int32 Slot = -1;
    UPROPERTY(ExposeOnSpawn) int32 Station = -1;
    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform;
    private FCk_Handle_UnrealComponent _Component;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        if (Generation <= 0 || Slot < 0 || Slot >= 11 ||
            Station != (Slot < 3 ? 0 : Slot < 6 ? 1 : 2) ||
            !InitialTransform.IsValid() || InitialTransform.ContainsNaN())
        { utils_entity_lifetime::Request_DestroyEntity(InHandle); return ECk_EntityScript_ConstructionFlow::Finished; }
        auto Transform = utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
        if (ck::Is_NOT_Valid(Transform))
        { utils_entity_lifetime::Request_DestroyEntity(InHandle); return ECk_EntityScript_ConstructionFlow::Finished; }
        utils_entity_tag::Add(InHandle, FName(f"RotateTowardsGym_G{Generation}_Cube{Slot}"));
        _Component = utils_unreal_component::Add(InHandle,
            utils_unreal_component::Make_Params(UStaticMeshComponent,
                ECk_UnrealComponent_TickPolicy::DoNotTick, n"RotateTowardsGym_Cube"));
        if (ck::Is_NOT_Valid(_Component))
        { utils_entity_lifetime::Request_DestroyEntity(InHandle); return ECk_EntityScript_ConstructionFlow::Finished; }
        auto Bound = utils_unreal_component::BindTo_OnAdded(_Component,
            FCk_Delegate_UnrealComponent_OnAdded(this, n"OnCubeAdded"));
        if (ck::Is_NOT_Valid(Bound) || Bound != _Component)
        { utils_entity_lifetime::Request_DestroyEntity(InHandle); }
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION()
    private void OnCubeAdded(FCk_Handle_UnrealComponent InComponent)
    {
        if (InComponent != _Component || ck::Is_NOT_Valid(InComponent) ||
            !utils_unreal_component::Has(FCk_Handle(InComponent))) { return; }
        auto Mesh = Cast<UStaticMeshComponent>(utils_unreal_component::Get_Component(InComponent));
        auto Asset = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Cube.Cube"));
        if (ck::Is_NOT_Valid(Mesh) || ck::Is_NOT_Valid(Asset)) { return; }
        Mesh.SetMobility(EComponentMobility::Movable);
        Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh.SetStaticMesh(Asset);
    }

    FCk_Handle_UnrealComponent Get_Component() const { return _Component; }
    bool Get_IsReady() const
    {
        if (ck::Is_NOT_Valid(_Component) || !utils_unreal_component::Has(FCk_Handle(_Component))) { return false; }
        auto Mesh = Cast<UStaticMeshComponent>(utils_unreal_component::Get_Component(_Component));
        return ck::IsValid(Mesh) && ck::IsValid(Mesh.StaticMesh);
    }
}
