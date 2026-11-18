// Language=angelscript

//============================================================================
// CK USF CROSS HATCH GYM - ENTITY effect-mask subjects.
//
// Demonstrates "put this ENTITY in the stylize effect mask" through the public
// API instead of tagging a mesh's Custom Stencil by hand:
//   UCk_Utils_Usf_StylizeMask_UE::Request_AddToStylizeMask(Handle, Scope)
//   UCk_Utils_Usf_StylizeMask_UE::Request_RemoveFromStylizeMask(Handle)
//
// The actor path is the one CkUsf itself implements: each subject spawns a
// UCk_EntityScript_WithActor_UE bound to itself, so the entity carries an
// owning actor and FProcessor_Usf_StylizeMaskActor_Sync writes the project's
// mask stencil value onto its primitives. ISM / ISKM proxies have their own
// sync processors, one per renderer module.
//
// Judge these against the MASK row in the judge scene: the hand-tagged cubes
// and these entity-driven ones must be indistinguishable, because they end up
// writing the same value through different front doors.
//============================================================================

class ACk_UsfGym_StylizeCrossHatchMaskSubject : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent Root;

    UPROPERTY(DefaultComponent, Attach = Root)
    UStaticMeshComponent Mesh;

    UPROPERTY(DefaultComponent)
    UCk_EntityBridge_ActorComponent_UE EntityBridge;

    private FCk_Handle _Entity;
    private bool _MaskApplied = false;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();

        auto CubeMesh = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Cube.Cube"));
        if (CubeMesh != nullptr)
        { Mesh.SetStaticMesh(CubeMesh); }

        Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh.SetRelativeScale3D(FVector(2.0f, 2.0f, 2.0f));

        auto Material = Cast<UMaterialInterface>(LoadObject(this, "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        if (Material != nullptr)
        {
            auto Mid = Mesh.CreateDynamicMaterialInstance(0, Material);
            if (Mid != nullptr)
            {
                // Same albedo as the hand-tagged mask row, so the two rows are directly comparable.
                Mid.SetVectorParameterValue(n"Color", FLinearColor(0.45f, 0.45f, 0.48f, 1.0f));
            }
        }

        // The bridge derives the entity's replication from the Actor, so this purely cosmetic local actor
        // gets a local entity (no EntityReplicationDriver).
        if (HasAuthority())
        {
            EntityBridge.Promise_OnEntityConstructed(FCk_Delegate_EntityScript_Constructed(this, n"OnEntityConstructed"));
            EntityBridge.Request_SpawnLinkedEntity(UCk_EntityScript_WithActor_UE);
        }
    }

    UFUNCTION()
    void OnEntityConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        _Entity = FCk_Handle(InEntityScriptHandle);
        _Entity.Set_DebugName(n"UsfStylizeMaskSubject");
        Request_ApplyMask();
    }

    void Request_ApplyMask()
    {
        if (ck::Is_NOT_Valid(_Entity))
        { return; }

        UCk_Utils_Usf_StylizeMask_UE::Request_AddToStylizeMask(
            _Entity, ECk_Usf_OutlineScope::EntityOnly);
        _MaskApplied = true;
    }

    void Request_ClearMask()
    {
        if (ck::Is_NOT_Valid(_Entity))
        { return; }

        UCk_Utils_Usf_StylizeMask_UE::Request_RemoveFromStylizeMask(_Entity);
        _MaskApplied = false;
    }

    // Toggling is the actual verification: a mask that never comes OFF proves only that something was
    // written once, not that the remove processor restores the mesh.
    void Request_ToggleMask()
    {
        if (_MaskApplied)
        { Request_ClearMask(); }
        else
        { Request_ApplyMask(); }
    }
}
