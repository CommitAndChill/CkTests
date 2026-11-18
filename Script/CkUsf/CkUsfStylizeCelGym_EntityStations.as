// Language=angelscript

//============================================================================
// CK USF CEL SHADE GYM - ENTITY-PATTERN subjects.
//
// Demonstrates "give this ENTITY a cel pattern" through the public API instead
// of tagging a mesh's Custom Stencil by hand:
//   UCk_Utils_Usf_CelPattern_UE::Request_SetCelPattern(Handle, Pattern, Scope)
//   UCk_Utils_Usf_CelPattern_UE::Request_ClearCelPattern(Handle)
//
// The actor path is the one CkUsf itself implements: each subject spawns a
// UCk_EntityScript_WithActor_UE subclass bound to itself, so the entity carries an
// owning actor and FProcessor_Usf_CelPatternActor_Sync writes the stencil onto
// its primitives. ISM / ISKM proxies are not covered - the same distribution
// entity outlines have, one sync processor per renderer module.
//
// Judge these against the STENCIL row in the judge scene: the hand-tagged cubes
// and these entity-driven ones must be indistinguishable, because they end up
// writing the same values through different front doors.
//============================================================================

class ACk_UsfGym_StylizeCelEntitySubject : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent Root;

    UPROPERTY(DefaultComponent, Attach = Root)
    UStaticMeshComponent Mesh;

    UPROPERTY(DefaultComponent)
    UCk_EntityBridge_ActorComponent_UE EntityBridge;

    // Which pattern this subject asks for. The default is the first entry, so a subject dropped in a
    // level without configuration still exercises the path rather than doing nothing.
    UPROPERTY()
    ECk_Usf_CelPattern Pattern = ECk_Usf_CelPattern::Bayer;

    private FCk_Handle _Entity;
    bool _PatternApplied = false; // first set by the entity script's initial pattern

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
                // Same albedo as the hand-tagged stencil row, so the two rows are directly comparable.
                Mid.SetVectorParameterValue(n"Color", FLinearColor(0.45f, 0.45f, 0.48f, 1.0f));
            }
        }

        // The bridge derives the entity's replication from the Actor, so this purely cosmetic local actor
        // gets a local entity (no EntityReplicationDriver).
        if (HasAuthority())
        {
            EntityBridge.Promise_OnEntityReady(FCk_Delegate_EntityBridge_OnEntityReady(this, n"OnEntityReady"));
            EntityBridge.Request_SpawnLinkedEntity(UCk_UsfGym_StylizeCelEntitySubject_EntityScript);
        }
    }

    // The initial pattern is composed by UCk_UsfGym_StylizeCelEntitySubject_EntityScript; toggles go through the
    // Request_ functions below.
    UFUNCTION()
    void OnEntityReady(AActor InActor, FCk_Handle InEntity)
    {
        _Entity = InEntity;
    }

    void Request_ApplyPattern()
    {
        if (ck::Is_NOT_Valid(_Entity))
        { return; }

        UCk_Utils_Usf_CelPattern_UE::Request_SetCelPattern(
            _Entity, Pattern, ECk_Usf_OutlineScope::EntityOnly);
        _PatternApplied = true;
    }

    void Request_ClearPattern()
    {
        if (ck::Is_NOT_Valid(_Entity))
        { return; }

        UCk_Utils_Usf_CelPattern_UE::Request_ClearCelPattern(_Entity);
        _PatternApplied = false;
    }

    // Toggling is the actual verification: a pattern that never comes OFF proves only that something was
    // written once, not that the remove processor restores the mesh.
    void Request_TogglePattern()
    {
        if (_PatternApplied)
        { Request_ClearPattern(); }
        else
        { Request_ApplyPattern(); }
    }
}

// Composes the subject's entity: the subject's Pattern, applied once at construction.
class UCk_UsfGym_StylizeCelEntitySubject_EntityScript : UCk_EntityScript_WithActor_UE
{
    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto Actor = Cast<ACk_UsfGym_StylizeCelEntitySubject>(DoGet_Actor());
        if (!Actor.HasAuthority())
        { return ECk_EntityScript_ConstructionFlow::Finished; }

        InHandle.Set_DebugName(n"UsfCelPatternSubject");

        UCk_Utils_Usf_CelPattern_UE::Request_SetCelPattern(
            InHandle, Actor.Pattern, ECk_Usf_OutlineScope::EntityOnly);
        Actor._PatternApplied = true;

        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}
