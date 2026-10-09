// Language=angelscript
// Shared authority source context for all six producer scripts.
class UCk_CueGym_SourceDemo : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;
    // Retained EnTT spawn parameters cannot contain a strong UObject reference.
    UPROPERTY(ExposeOnSpawn) TWeakObjectPtr<ACk_CueGym_GameState> SourceStateRef;
    UPROPERTY() ACk_CueGym_GameState SourceState;
    UPROPERTY(ExposeOnSpawn) int32 SourceRole = -1;
    UPROPERTY(ExposeOnSpawn) int32 SourceGeneration = 0;
    UPROPERTY(ExposeOnSpawn) int32 RoleEpoch = 0;
    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform = FTransform::Identity;

    bool Get_CanRunSource()
    {
        auto State = SourceStateRef.Get();
        if (ck::Is_NOT_Valid(State) || (SourceState != nullptr && SourceState != State) ||
            !State.Get_IsSourceContext(this, SourceRole, SourceGeneration,
                RoleEpoch, ck::ToEntity(this))) { return false; }
        // Bind only after the exact target/role/epoch/world predicate succeeds.
        SourceState = State;
        return true;
    }

    void SubmitDisplay(FString Title, FString Body, FString Instructions)
    {
        if (!Get_CanRunSource()) { return; }
        SourceState.SubmitDisplay(this, SourceRole, SourceGeneration,
            RoleEpoch, Title, Body, Instructions);
    }
}

USTRUCT()
struct FCkCueGym_SourceSpawnParams
{
    UPROPERTY() TWeakObjectPtr<ACk_CueGym_GameState> SourceStateRef;
    UPROPERTY() int32 SourceRole = -1;
    UPROPERTY() int32 SourceGeneration = 0;
    UPROPERTY() int32 RoleEpoch = 0;
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
}
