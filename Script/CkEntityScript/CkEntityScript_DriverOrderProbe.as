// Network probe only. Entity Scripts are created exclusively by the native test's public spawn calls.
UCLASS()
class ACk_EntityScript_DriverOrderProbe : AActor
{
    default bReplicates = true;
    default bAlwaysRelevant = true;
    default bReplicateUsingRegisteredSubObjectList = true;

    UPROPERTY(Replicated)
    int32 ProbeId = 0;
}
