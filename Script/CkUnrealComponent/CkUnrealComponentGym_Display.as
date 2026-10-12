// Language=angelscript
// Kept as a compatibility type for older authored references. The multiplayer
// controller updates each admitted local Common station directly from the
// validated replicated value wire. No display entity is spawned by that path.
USTRUCT()
struct FUnrealComponentGym_DisplaySpawnParams
{
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() TWeakObjectPtr<ACk_UnrealComponentGym_Driver> LinkedDriver;
}

class UCk_EntityScript_UnrealComponentGym_Display : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) TWeakObjectPtr<ACk_UnrealComponentGym_Driver> LinkedDriver;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        // Historical identity only. No timer, driver reads, or mutable fallback.
        utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        utils_entity_tag::Add(InHandle, n"TAG_UnrealComponentGym_Display");
        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}
