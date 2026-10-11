// Language=angelscript

class UCk_EntityScript_SwayGym_WorldRoot : UCk_EntityScript_WithActor_UE
{
    default _Replication = ECk_Replication::Replicates;
}

UENUM()
enum ECkSwayGym_NodeRole { Root, Head, Sway, Cube }

USTRUCT()
struct FCkSwayGym_NodeSpawnParams
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Station = -1;
    UPROPERTY() int32 Index = -1;
    UPROPERTY() ECkSwayGym_NodeRole Role = ECkSwayGym_NodeRole::Root;
    UPROPERTY() FTransform InitialTransform;
}

class UCk_EntityScript_SwayGym_Node : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::Replicates;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    UPROPERTY(ExposeOnSpawn) int32 Station = -1;
    UPROPERTY(ExposeOnSpawn) int32 Index = -1;
    UPROPERTY(ExposeOnSpawn) ECkSwayGym_NodeRole Role = ECkSwayGym_NodeRole::Root;
    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform;
    private FCk_Handle_UnrealComponent _Component;
    private bool _Ready = false;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        const int32 RoleValue = int32(Role);
        if (Generation <= 0 || Station < 0 || Station >= 3 || Index < 0 ||
            RoleValue < 0 || RoleValue > 3 || InitialTransform.ContainsNaN() ||
            !InitialTransform.IsValid())
        {
            utils_entity_lifetime::Request_DestroyEntity(InHandle);
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        const int32 MaxIndex = Role == ECkSwayGym_NodeRole::Cube ? (Station == 2 ? 2 : 1) :
            Role == ECkSwayGym_NodeRole::Sway && Station == 2 ? 2 : 0;
        if (Index > MaxIndex)
        {
            utils_entity_lifetime::Request_DestroyEntity(InHandle);
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        const bool Visible = Role == ECkSwayGym_NodeRole::Head || Role == ECkSwayGym_NodeRole::Cube;
        if (Visible || System::IsServer())
        {
            auto Transform = utils_transform::Add(InHandle, InitialTransform,
                Visible ? ECk_Replication::Replicates : ECk_Replication::DoesNotReplicate);
            if (ck::Is_NOT_Valid(Transform))
            {
                utils_entity_lifetime::Request_DestroyEntity(InHandle);
                return ECk_EntityScript_ConstructionFlow::Finished;
            }
        }
        utils_entity_tag::Add(InHandle,
            FName(f"SwayGym_G{Generation}_Role{RoleValue}_Station{Station}_Index{Index}"));
        if (Role == ECkSwayGym_NodeRole::Cube)
        {
            _Component = utils_unreal_component::Add(InHandle,
                utils_unreal_component::Make_Params(UStaticMeshComponent,
                    ECk_UnrealComponent_TickPolicy::DoNotTick, n"SwayGym_Cube"));
            if (ck::Is_NOT_Valid(_Component))
            {
                utils_entity_lifetime::Request_DestroyEntity(InHandle);
                return ECk_EntityScript_ConstructionFlow::Finished;
            }
            auto Bound = utils_unreal_component::BindTo_OnAdded(_Component,
                FCk_Delegate_UnrealComponent_OnAdded(this, n"OnCubeAdded"));
            if (ck::Is_NOT_Valid(Bound) || Bound != _Component)
            {
                utils_entity_lifetime::Request_DestroyEntity(InHandle);
                return ECk_EntityScript_ConstructionFlow::Finished;
            }
        }
        _Ready = true;
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION()
    private void OnCubeAdded(FCk_Handle_UnrealComponent InComponent)
    {
        if (InComponent != _Component || ck::Is_NOT_Valid(InComponent) ||
            !utils_unreal_component::Has(FCk_Handle(InComponent))) { return; }
        auto Mesh = Cast<UStaticMeshComponent>(utils_unreal_component::Get_Component(InComponent));
        if (ck::Is_NOT_Valid(Mesh)) { return; }
        auto Asset = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Cube.Cube"));
        if (ck::Is_NOT_Valid(Asset)) { return; }
        Mesh.SetMobility(EComponentMobility::Movable);
        Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh.SetStaticMesh(Asset);
    }

    bool Get_IsReady() const
    {
        if (!_Ready) { return false; }
        if (Role != ECkSwayGym_NodeRole::Cube) { return true; }
        if (ck::Is_NOT_Valid(_Component) ||
            !utils_unreal_component::Has(FCk_Handle(_Component))) { return false; }
        auto Mesh = Cast<UStaticMeshComponent>(utils_unreal_component::Get_Component(_Component));
        return ck::IsValid(Mesh) && ck::IsValid(Mesh.StaticMesh);
    }
    FCk_Handle_UnrealComponent Get_Component() const { return _Component; }
}
