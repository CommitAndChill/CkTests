//============================================================================
// REPLICATION GYM - SHARED MESSAGES & CONSTANTS
//============================================================================
// Reproduces the "No container fragment entry found for type
// [Ck_RepData_IntegerAttributes] on Entity" ensure seen when a replicated
// actor (or a replicated pawn) spawns a UCk_EntityScript_WithActor_UE with
// ck::TransientEntity() as the lifetime owner and that script adds replicated
// Integer Attributes in its construct handler.
//============================================================================

USTRUCT()
struct FCk_Message_ReplicationGym_SetAttribute
{
    UPROPERTY()
    int32 Value;

    FCk_Message_ReplicationGym_SetAttribute(int32 InValue = 0)
    {
        Value = InValue;
    }
}

namespace CkReplicationGym
{
    const int32 ActorPresetCount = 4;
    const int32 PawnPresetCount = 4;
    const int32 Action_CycleActor = 0;
    const int32 Action_CyclePawn = 1;
    const int32 Action_ResetActor = 2;
    const int32 Action_Dump = 3;
    const int32 Action_SetActorPreset = 4;

    const int32 Result_None = -1;
    const int32 Result_Accepted = 0;
    const int32 Result_InvalidAction = 1;
    const int32 Result_InvalidPreset = 2;
    const int32 Result_InvalidGym = 3;
    const int32 Result_InvalidOwner = 4;
    const int32 Result_NotReady = 5;
    const int32 Result_TargetFailed = 6;

    FString Get_ActionResultLabel(int32 InResult)
    {
        if (InResult == Result_Accepted) { return "accepted"; }
        if (InResult == Result_InvalidAction) { return "rejected: action"; }
        if (InResult == Result_InvalidPreset) { return "rejected: preset"; }
        if (InResult == Result_InvalidGym) { return "rejected: gym"; }
        if (InResult == Result_InvalidOwner) { return "rejected: owner"; }
        if (InResult == Result_NotReady) { return "rejected: not ready"; }
        if (InResult == Result_TargetFailed) { return "rejected: target"; }
        return "none";
    }

    FCkGym_Station_SpawnParams_Payload Get_ActorStation()
    {
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(n"Gym.Replication.ReplicatedActor");
        Station.Title = FText::FromString("REPLICATION — REPLICATED ACTOR");
        Station.AutoSize = true;
        Station.Transform = FTransform(FRotator(0.0f, 180.0f, 0.0f),
            FVector(500.0f, -400.0f, 0.0f), FVector(1.0f, 1.0f, 1.0f));
        auto Description = TArray<FText>();
        Description.Add(FText::FromString("Scenario A: a replicated AActor (mirror of ABB_Store) spawns a"));
        Description.Add(FText::FromString("UCk_EntityScript_WithActor_UE subclass with _Replication=Replicates"));
        Description.Add(FText::FromString("using ck::TransientEntity() as lifetime. The entity script adds a"));
        Description.Add(FText::FromString("replicated Integer Attribute in DoConstructWithActor."));
        Description.Add(FText::FromString("Regression: no 'No container fragment entry found for type"));
        Description.Add(FText::FromString("      [Ck_RepData_IntegerAttributes]' ensure on server or clients."));
        Station.Description = Description;
        return Station;
    }

    FVector Get_ActorPanelCenter()
    {
        auto Station = Get_ActorStation();
        // Mirrors GymStation's spawn-time AutoSize/Populate_AnchorsFragment calculation.
        const double HeightCm = Math::Max(200.0,
            20.0 + double(Station.Description.Num()) * 12.0 * 1.5 + 120.0);
        return Station.Transform.TransformPosition(FVector(0.0f, 0.0f, HeightCm * 0.5));
    }

    int32 Get_ActorPresetValue(int32 InIndex)
    {
        if (InIndex == 0) { return 100; }
        if (InIndex == 1) { return 0; }
        if (InIndex == 2) { return -50; }
        if (InIndex == 3) { return 9999; }
        return 0;
    }

    int32 Get_PawnPresetValue(int32 InIndex)
    {
        if (InIndex == 0) { return 50; }
        if (InIndex == 1) { return 0; }
        if (InIndex == 2) { return -25; }
        if (InIndex == 3) { return 500; }
        return 0;
    }
}
