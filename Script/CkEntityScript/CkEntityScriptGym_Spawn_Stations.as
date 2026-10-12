// Language=angelscript

namespace CkEntityScriptGym_Spawn
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
    {
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        auto Plain = FCkGym_Station_SpawnParams_Payload();
        Plain.Tags.Add(n"Gym.EntityScript.Spawn");
        Plain.Title = FText::FromString("ENTITY SCRIPT SPAWN");
        Plain.Description.Add(FText::FromString("Verifies that ExposeOnSpawn properties are correctly injected"));
        Plain.Description.Add(FText::FromString("during entity script spawning."));
        Plain.Description.Add(FText::FromString("panel [1] Re-run spawn test"));
        Plain.Description.Add(FText::FromString(""));
        Plain.Description.Add(FText::FromString("Waiting for entity to spawn..."));
        Stations.Add(Plain);

        auto Replicated = FCkGym_Station_SpawnParams_Payload();
        Replicated.Tags.Add(n"Gym.EntityScript.SpawnReplicated");
        Replicated.Title = FText::FromString("ENTITY SCRIPT SPAWN (REPLICATED)");
        Replicated.Description.Add(FText::FromString("Verifies that ExposeOnSpawn properties survive replication."));
        Replicated.Description.Add(FText::FromString("Entity script uses ECk_Replication::Replicates."));
        Replicated.Description.Add(FText::FromString("panel [2] Re-run replicated spawn test"));
        Replicated.Description.Add(FText::FromString(""));
        Replicated.Description.Add(FText::FromString("Waiting for entity to spawn..."));
        Stations.Add(Replicated);

        for (int32 i = 0; i < Stations.Num(); i++)
        {
            auto Transform = FTransform::Identity;
            Transform.SetLocation(FVector(500.0, i == 0 ? -400.0 : 400.0, 0.0));
            Transform.SetRotation(FRotator(0.0, 180.0, 0.0).Quaternion());
            Stations[i].Transform = Transform;
        }
        return Stations;
    }

    bool IsLive(FCk_Handle InHandle)
    {
        return ck::IsValid(InHandle) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    bool IsDirectChild(FCk_Handle InChild, FCk_Handle InOwner)
    {
        return IsLive(InChild) && IsLive(InOwner) &&
            utils_entity_lifetime::Has_LifetimeOwner(InChild) &&
            utils_entity_lifetime::Get_LifetimeOwner(InChild) == InOwner;
    }

    FCk_Handle FindStation(FName InTag)
    {
        auto Matches = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), InTag);
        if (Matches.Num() != 1 || !IsLive(Matches[0]) ||
            !Matches[0].Has_Fragment(FCkGym_Station_Anchors))
        { return FCk_Handle(); }
        return Matches[0];
    }

    ACk_EntityScriptGym_Spawn_GameState Get_State()
    {
        TArray<ACk_EntityScriptGym_Spawn_GameState> States;
        GetAllActorsOfClass(ACk_EntityScriptGym_Spawn_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0])) { return nullptr; }
        return States[0];
    }

    FTransform GetPanelTransform(FCk_Handle InStation)
    {
        auto Anchors = InStation.Get_Fragment(FCkGym_Station_Anchors);
        return FTransform(FRotator(0.0, 180.0, 0.0), Anchors.PanelCenter);
    }
}

// This is an actual actor-linked replicated ECS root. Its original replicated
// child is constructed by the entity replication driver, never by a client RPC.
class UCk_EntityScript_EntityScriptGym_WorldRoot : UCk_EntityScript_WithActor_UE
{
    default _Replication = ECk_Replication::Replicates;
}

USTRUCT()
struct FCkEntityScriptGym_PresentationParams
{
    UPROPERTY() int32 StationIndex = -1;
    UPROPERTY() int32 Generation = 0;
}

// Local panel ownership only. It has no demo EntityScript or replication driver.
class UCk_EntityScript_EntityScriptGym_Presentation : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;
    UPROPERTY(ExposeOnSpawn) int32 StationIndex = -1;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto State = CkEntityScriptGym_Spawn::Get_State();
        auto Tag = StationIndex == 0 ? n"Gym.EntityScript.Spawn" :
            n"Gym.EntityScript.SpawnReplicated";
        if (StationIndex < 0 || StationIndex > 1 || Generation <= 0 ||
            ck::Is_NOT_Valid(State) || State.Snapshot.Generation != Generation ||
            !CkEntityScriptGym_Spawn::IsDirectChild(InHandle,
                CkEntityScriptGym_Spawn::FindStation(Tag)))
        {
            ck::Error("[EntityScriptGym] Rejected malformed presentation child");
            utils_entity_lifetime::Request_DestroyEntity(InHandle);
        }
        else
        {
            utils_entity_tag::Add(InHandle, StationIndex == 0 ?
                n"TAG_EntityScriptGym_PresentationPlain" :
                n"TAG_EntityScriptGym_PresentationReplicated");
        }
        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}
