// Advanced AudioGym Pawn - Clean and simple setup
// Following gym philosophy: simple, readable, organized

class ACkAudioGym_Advanced_Pawn : ADefaultPawn
{
    UPROPERTY(DefaultComponent)
    UCk_EntityBridge_ActorComponent_UE EntityBridge;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();
        EntityBridge.Request_SpawnLinkedEntity(UCkAudioGym_Advanced_Pawn_EntityScript);
    }
}

// Composes the pawn's entity: the player probe, then the level's scattered audio pickups (owned by the pawn's entity).
class UCkAudioGym_Advanced_Pawn_EntityScript : UCk_EntityScript_WithActor_UE
{
    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        InHandle.Set_DebugName(n"Pawn");
        SetupPlayer(InHandle);
        SetupLevel(InHandle);
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    // ============================================================================
    // PLAYER SETUP
    // ============================================================================

    void SetupPlayer(FCk_Handle InEntity)
    {
        // Add player probe feature so player can overlap with stations
        auto ProbeParams = FCk_Probe_Spec(utils_gameplay_tag::ResolveGameplayTag(n"Player.Probe"));
        ProbeParams.Set_MotionType(ECk_MotionType::Kinematic);
        ProbeParams.Set_ResponsePolicy(ECk_ProbeResponse_Policy::Notify);

        // Add transform first (required for probe)
        auto TransformHandle = utils_transform::Add(InEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);

        // Create a larger probe around the player for better pickup detection
        auto PlayerProbeSize = FVector(150, 150, 250); // Larger player probe for better overlap detection
        auto BoxShape = utils_shapes::Make_Box(FCk_ShapeBox_Dimensions(PlayerProbeSize));
        utils_shapes::Add(InEntity, BoxShape);

        auto DebugInfo = FCk_Probe_DebugInfo();
        utils_probe::Add(TransformHandle, ProbeParams, DebugInfo);

        ck::Trace("[OK] Player probe added", NAME_None, 2.0f, utils_linear_color::Get_Green());
    }

    // ============================================================================
    // LEVEL SETUP
    // ============================================================================

    void SetupLevel(FCk_Handle InOwner)
    {
        ck::Trace("* AudioGym Advanced Level Ready - Use StationSpawner actors to place stations", NAME_None, 5.0f, utils_linear_color::Get_Orange());

        // Scatter audio pickups around the level for testing
        ScatterAudioPickups(InOwner);
    }

    // ============================================================================
    // PICKUP SCATTERING
    // ============================================================================

    void ScatterAudioPickups(FCk_Handle InOwner)
    {
        ck::Trace("* Scattering audio pickups throughout the level...", NAME_None, 3.0f, utils_linear_color::Get_Yellow());

        // Scatter pickups across the full 8000x6000 level area
        // Level dimensions: X: -4000 to +4000, Y: -3000 to +3000
        ScatterPickupsAcrossFullLevel(InOwner);

        ck::Trace("[OK] Audio pickups scattered successfully", NAME_None, 2.0f, utils_linear_color::Get_Green());
    }

void ScatterPickupsAcrossFullLevel(FCk_Handle InOwner)
{
    // Test cluster around (1000, 1000, 100) for audio debugging
    // Very close to test point - should be loud
    SpawnInterfacePickup(InOwner, FVector(1000, 1000, 100));  // Exact test position
    SpawnInterfacePickup(InOwner, FVector(1050, 1050, 100));  // 50 units away
    SpawnInterfacePickup(InOwner, FVector(950, 950, 100));    // 50 units away, other direction

    // Medium distance - should be audible with attenuation
    SpawnLevelUpPickup(InOwner, FVector(1200, 1200, 100));    // ~280 units away
    SpawnLevelUpPickup(InOwner, FVector(800, 800, 100));      // ~280 units away
    SpawnLevelUpPickup(InOwner, FVector(1000, 1400, 100));    // 400 units away
    SpawnLevelUpPickup(InOwner, FVector(1000, 600, 100));     // 400 units away

    // Longer distance - testing attenuation falloff
    SpawnNotificationsPickup(InOwner, FVector(1500, 1500, 100)); // ~700 units away
    SpawnNotificationsPickup(InOwner, FVector(500, 500, 100));   // ~700 units away
    SpawnNotificationsPickup(InOwner, FVector(1000, 1800, 100)); // 800 units away
    SpawnNotificationsPickup(InOwner, FVector(1000, 200, 100));  // 800 units away

    // Control test - one near world origin to compare
    SpawnInterfacePickup(InOwner, FVector(100, 100, 100));       // Near origin for comparison
}

    // ============================================================================
    // PICKUP SPAWNING HELPERS
    // ============================================================================

    void SpawnInterfacePickup(FCk_Handle InOwner, FVector Location)
    {
        auto SpawnParams = FCkAudioGym_Advanced_AudioPickup_SpawnParams();
        SpawnParams.Transform = FTransform(FRotator::ZeroRotator, Location);

        auto PickupEntity = utils_entity_script::Request_SpawnEntity(InOwner,
            UCkAudioGym_Advanced_InterfacePickup, SpawnParams);

        if (ck::IsValid(PickupEntity))
        {
            ck::Trace(f"* Interface pickup placed at {Location.X},{Location.Y}", NAME_None, 1.0f, utils_linear_color::Get_Cyan());
        }
    }

    void SpawnLevelUpPickup(FCk_Handle InOwner, FVector Location)
    {
        auto SpawnParams = FCkAudioGym_Advanced_AudioPickup_SpawnParams();
        SpawnParams.Transform = FTransform(FRotator::ZeroRotator, Location);

        auto PickupEntity = utils_entity_script::Request_SpawnEntity(InOwner,
            UCkAudioGym_Advanced_LevelUpPickup, SpawnParams);

        if (ck::IsValid(PickupEntity))
        {
            ck::Trace(f"* LevelUp pickup placed at {Location.X},{Location.Y}", NAME_None, 1.0f, utils_linear_color::Get_Yellow());
        }
    }

    void SpawnNotificationsPickup(FCk_Handle InOwner, FVector Location)
    {
        auto SpawnParams = FCkAudioGym_Advanced_AudioPickup_SpawnParams();
        SpawnParams.Transform = FTransform(FRotator::ZeroRotator, Location);

        auto PickupEntity = utils_entity_script::Request_SpawnEntity(InOwner,
            UCkAudioGym_Advanced_NotificationsPickup, SpawnParams);

        if (ck::IsValid(PickupEntity))
        {
            ck::Trace(f"* Notifications pickup placed at {Location.X},{Location.Y}", NAME_None, 1.0f, utils_linear_color::Get_Magenta());
        }
    }
}
