// Station Spawner Actor - Place in level to spawn AudioGym stations
// This gives you manual control over station placement

class ACkAudioGym_Advanced_StationSpawner : AActor
{
    // Root component to make the actor movable
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent RootComponent;

    // TextRender component to display station name
    UPROPERTY(DefaultComponent, Attach = RootComponent)
    UTextRenderComponent TextRenderComponent;

    UPROPERTY(DefaultComponent)
    UCk_EntityBridge_ActorComponent_UE EntityBridge;

    // Expose on spawn - choose which station to spawn
    UPROPERTY(EditAnywhere, Category = "AudioGym Station")
    TSubclassOf<UCk_EntityScript_UE> StationEntityScriptClass;

    // Optional: Override the station's transform
    UPROPERTY(EditAnywhere, Category = "AudioGym Station")
    bool bOverrideStationTransform = false;

    UPROPERTY(EditAnywhere, Category = "AudioGym Station", meta = (EditCondition = "bOverrideStationTransform"))
    FTransform StationTransform;

    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        auto _CkPerfScope = ck::ScopedStat();
        SetupTextRender();
    }

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();
        EntityBridge.Request_SpawnLinkedEntity(UCkAudioGym_Advanced_StationSpawner_EntityScript);
    }

        void SetupTextRender()
    {
        if (TextRenderComponent == nullptr)
        {
            return;
        }

        // Get station name from class
        FString StationName = GetStationDisplayName();

        // Configure TextRender properties
        TextRenderComponent.SetText(FText::FromString(StationName));
        TextRenderComponent.HorizontalAlignment = EHorizTextAligment::EHTA_Center;
        TextRenderComponent.VerticalAlignment = EVerticalTextAligment::EVRTA_TextCenter;

        // Position text above the spawner
        TextRenderComponent.SetRelativeLocation(FVector(0, 0, 250));

        // Scale text appropriately
        TextRenderComponent.SetWorldSize(100.0f);

        // Set text color
        TextRenderComponent.SetTextRenderColor(FColor::White);

        // Make it face the camera (rotate to face forward)
        TextRenderComponent.SetRelativeRotation(FRotator(0, 180, 0));
    }

        FString GetStationDisplayName()
    {
        if (StationEntityScriptClass == nullptr)
        {
            return "NO STATION SELECTED";
        }

        // Get the class name as string and clean it up
        auto _FullName = f"{StationEntityScriptClass}";

        _FullName = FullName.Replace("CkAudioGym_Advanced_", "");
        _FullName = FullName.Replace("(UASClass)", "");
        _FullName = FullName.Replace("{ ", "");
        _FullName = FullName.Replace(" }", "");

        return _FullName;
    }
}

// Composes the spawner's entity: the chosen station, spawned as a child of it.
class UCkAudioGym_Advanced_StationSpawner_EntityScript : UCk_EntityScript_WithActor_UE
{
    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto Actor = Cast<ACkAudioGym_Advanced_StationSpawner>(DoGet_Actor());
        InHandle.Set_DebugName(n"StationSpawner");

        if (Actor.StationEntityScriptClass == nullptr)
        {
            Print("[FAIL] No station class selected in StationSpawner", 5.0f);
            return ECk_EntityScript_ConstructionFlow::Finished;
        }

        // Use actor's transform or override
        auto SpawnTransform = Actor.bOverrideStationTransform ? Actor.StationTransform : Actor.GetActorTransform();

        // Create spawn params
        auto SpawnParams = FCkAudioGym_Advanced_Station_SpawnParams();
        SpawnParams.Transform = SpawnTransform;

        // Spawn the station entity script
        auto StationEntity = utils_entity_script::Request_SpawnEntity(InHandle,
            Actor.StationEntityScriptClass, SpawnParams);

        if (ck::IsValid(StationEntity))
        {
            Print("[OK] Station spawned successfully", 3.0f);
            Print("* Position: Transform applied", 2.0f);
        }
        else
        {
            Print("[FAIL] Failed to spawn station", 3.0f);
        }

        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}
