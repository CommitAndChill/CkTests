USTRUCT()
struct FCkPoolingGym_PoolSample
{
    UPROPERTY() int32 Hits = 0;
    UPROPERTY() int32 Misses = 0;
    UPROPERTY() int32 Live = 0;
    UPROPERTY() int32 Free = 0;
    UPROPERTY() int32 InUse = 0;
    UPROPERTY() int32 HighWater = 0;
}

USTRUCT()
struct FCkPoolingGym_Snapshot
{
    UPROPERTY() int32 CatalogVersion = 0;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() TArray<FCk_GymStation_SpawnParams> Stations;
    UPROPERTY() TArray<FString> Titles;
    UPROPERTY() TArray<FString> Bodies;
    UPROPERTY() int32 TotalSpawned = 0;
    UPROPERTY() int32 TotalDestroyed = 0;
    UPROPERTY() int32 HerdSize = 0;
    UPROPERTY() FCkPoolingGym_PoolSample Script;
    UPROPERTY() FCkPoolingGym_PoolSample Ping;
    UPROPERTY() FCkPoolingGym_PoolSample Pong;
    UPROPERTY() FCkPoolingGym_PoolSample Condition;
}

USTRUCT()
struct FCkPoolingGym_ControlCommit
{
    UPROPERTY() int32 DeliverySerial = 0;
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 OriginGeneration = 0;
    UPROPERTY() int32 Action = -1;
    UPROPERTY() int32 BurstIssued = 0;
    UPROPERTY() int32 BurstConstructed = 0;
    UPROPERTY() int32 BurstCompleted = 0;
    UPROPERTY() ACk_ObjectPoolingGym_GameState OriginState;
    UPROPERTY() ACk_ObjectPoolingGym_PlayerController Owner;
    UPROPERTY() ACk_Gym_Base_Pawn OwnerPawn;
    UPROPERTY() FCkPoolingGym_Snapshot Snapshot;
}

// Authority-only receipt of the real restart boundary. The values are sampled
// during the accepted R path, while the shared timer is suspended from churn.
USTRUCT()
struct FCkPoolingGym_RestartCadence
{
    UPROPERTY() bool Complete = false;
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 OriginGeneration = 0;
    UPROPERTY() int32 ResultGeneration = 0;
    UPROPERTY() ACk_ObjectPoolingGym_PlayerController Owner;
    UPROPERTY() float SmBefore = 0.0f;
    UPROPERTY() float SmAfter = 0.0f;
    UPROPERTY() float DisplayBefore = 0.0f;
    UPROPERTY() float DisplayAfter = 0.0f;
}

// Authority-only admission census, captured from the allocated lifetime graph
// before the restart ledger is built or any old entity is destroyed.
USTRUCT()
struct FCkPoolingGym_RestartAdmissionAudit
{
    UPROPERTY() bool Captured = false;
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 OriginGeneration = 0;
    UPROPERTY() ACk_ObjectPoolingGym_PlayerController Owner;
    UPROPERTY() TArray<FCk_Handle> Allocated;
}

// Durable observation of the actual server owner-admission predicate immediately
// after a real commit is retained, before the owning client can acknowledge it.
USTRUCT()
struct FCkPoolingGym_CommitAdmissionAudit
{
    UPROPERTY() bool Observed = false;
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 DeliverySerial = 0;
    UPROPERTY() int32 AcknowledgedAtPublish = 0;
    UPROPERTY() bool CanAcceptAnother = true;
}

namespace CkPoolingShared
{
    const int32 Slots = 2;
    const int32 CatalogVersion = 1;
    const int32 WaveSize = 10;
    const int32 WaveDepth = 10;
    const int32 HerdSize = 20;
    const int32 RespawnSlice = 5;
    const float RespawnPeriod = 1.0f;
    const float DisplayPeriod = 0.25f;

    bool IsLive(FCk_Handle H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) && ck::IsValid(H) &&
        !utils_entity_lifetime::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }

    FName Get_Tag(int32 Slot)
    {
        if (Slot == 0) { return n"Gym.ObjectPooling.ScriptChurn"; }
        if (Slot == 1) { return n"Gym.ObjectPooling.SmChurn"; }
        return n"";
    }

    FString Get_Title(int32 Slot)
    {
        if (Slot == 0) { return "OBJECT POOLING - SCRIPT CHURN STRESS"; }
        if (Slot == 1) { return "OBJECT POOLING - STATEMACHINE CHURN STRESS"; }
        return "";
    }

    FCk_GymStation_SpawnParams Get_Params(int32 Slot)
    {
        FCk_GymStation_SpawnParams P;
        if (Slot < 0 || Slot >= Slots) { return P; }
        P.InitialTransform = FTransform(FRotator(0, 180, 0), FVector(500, Slot == 0 ? -400 : 400, 0));
        P.StationTags.Add(Get_Tag(Slot));
        P.TitleText = FText::FromString(Get_Title(Slot));
        if (Slot == 0)
        {
            P.DescriptionText.Add(FText::FromString("Sustained spawn/destroy waves of poolable EntityScripts."));
            P.DescriptionText.Add(FText::FromString("Healthy pooling: LIVE plateaus, HITS climbs, MISSES freezes."));
        }
        else
        {
            P.DescriptionText.Add(FText::FromString("Herd of Ping<->Pong StateMachines, slices respawned every second."));
            P.DescriptionText.Add(FText::FromString("SM state + condition scripts recycle continuously under load."));
        }
        P.DescriptionText.Add(FText::FromString("Warming up..."));
        return P;
    }

    bool HasExactRecipe(FCk_GymStation_SpawnParams Actual, int32 Slot)
    {
        if (Slot < 0 || Slot >= Slots) { return false; }
        auto Expected = Get_Params(Slot);
        if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                Expected.InitialTransform, Actual.InitialTransform) ||
            Actual.Width != Expected.Width || Actual.Depth != Expected.Depth || Actual.Height != Expected.Height ||
            Actual.WallThickness != Expected.WallThickness || Actual.FloorThickness != Expected.FloorThickness ||
            Actual.TrimDepth != Expected.TrimDepth || Actual.AgentSpawnOffset != Expected.AgentSpawnOffset ||
            Actual.BodyColour != Expected.BodyColour || Actual.TrimColour != Expected.TrimColour ||
            Actual.TitleText.ToString() != Expected.TitleText.ToString() || Actual.TitleScale != Expected.TitleScale ||
            Actual.TitleColour != Expected.TitleColour || Actual.DescriptionScale != Expected.DescriptionScale ||
            Actual.DescriptionColour != Expected.DescriptionColour || Actual.TextAlignment != Expected.TextAlignment ||
            Actual.FloorDescriptionScale != Expected.FloorDescriptionScale ||
            Actual.FloorDescriptionColour != Expected.FloorDescriptionColour ||
            Actual.FloorTextAlignment != Expected.FloorTextAlignment ||
            Actual.FloorTextPlacement != Expected.FloorTextPlacement ||
            Actual.ShowSpotlight != Expected.ShowSpotlight || Actual.ShowAnchors != Expected.ShowAnchors ||
            Actual.ShowDebugOverlays != Expected.ShowDebugOverlays || Actual.AutoSize != Expected.AutoSize ||
            Actual.StationTags.Num() != Expected.StationTags.Num() ||
            Actual.DescriptionText.Num() != Expected.DescriptionText.Num() ||
            Actual.FloorDescriptionText.Num() != Expected.FloorDescriptionText.Num()) { return false; }
        for (int32 i=0;i<Actual.StationTags.Num();++i)
        { if (Actual.StationTags[i] != Expected.StationTags[i]) { return false; } }
        for (int32 i=0;i<Actual.DescriptionText.Num();++i)
        { if (Actual.DescriptionText[i].ToString() != Expected.DescriptionText[i].ToString()) { return false; } }
        for (int32 i=0;i<Actual.FloorDescriptionText.Num();++i)
        { if (Actual.FloorDescriptionText[i].ToString() != Expected.FloorDescriptionText[i].ToString()) { return false; } }
        return true;
    }

    bool HasExactStationScript(UCk_EntityScript_GymStation Script, int32 Slot)
    {
        if (ck::Is_NOT_Valid(Script) || Slot < 0 || Slot >= Slots) { return false; }
        FCk_GymStation_SpawnParams Actual;
        Actual.InitialTransform=Script.InitialTransform;
        Actual.Width=Script.Width; Actual.Depth=Script.Depth; Actual.Height=Script.Height;
        Actual.WallThickness=Script.WallThickness; Actual.FloorThickness=Script.FloorThickness;
        Actual.TrimDepth=Script.TrimDepth; Actual.AgentSpawnOffset=Script.AgentSpawnOffset;
        Actual.BodyColour=Script.BodyColour; Actual.TrimColour=Script.TrimColour;
        Actual.TitleText=Script.TitleText; Actual.TitleScale=Script.TitleScale; Actual.TitleColour=Script.TitleColour;
        Actual.DescriptionText=Script.DescriptionText; Actual.DescriptionScale=Script.DescriptionScale;
        Actual.DescriptionColour=Script.DescriptionColour; Actual.TextAlignment=Script.TextAlignment;
        Actual.FloorDescriptionText=Script.FloorDescriptionText; Actual.FloorDescriptionScale=Script.FloorDescriptionScale;
        Actual.FloorDescriptionColour=Script.FloorDescriptionColour;
        Actual.FloorTextAlignment=Script.FloorTextAlignment; Actual.FloorTextPlacement=Script.FloorTextPlacement;
        Actual.ShowSpotlight=Script.ShowSpotlight; Actual.ShowAnchors=Script.ShowAnchors;
        Actual.ShowDebugOverlays=Script.ShowDebugOverlays; Actual.AutoSize=Script.AutoSize;
        Actual.StationTags=Script.StationTags;
        return HasExactRecipe(Actual,Slot);
    }

    ACk_ObjectPoolingGym_GameState Get_State(UWorld World)
    {
        if (ck::Is_NOT_Valid(World)) { return nullptr; }
        auto State = Cast<ACk_ObjectPoolingGym_GameState>(World.GetGameState());
        return ck::IsValid(State) && State.GetWorld() == World &&
            State.GetClass() == ACk_ObjectPoolingGym_GameState ? State : nullptr;
    }

    FCkPoolingGym_PoolSample Sample(FCk_ObjectPooling_PoolStats Stats)
    {
        FCkPoolingGym_PoolSample S;
        S.Hits = Stats.Get_NumHits(); S.Misses = Stats.Get_NumMisses();
        S.Live = Stats.Get_NumLiveInstances(); S.Free = Stats.Get_NumFree();
        S.InUse = Stats.Get_NumInUse(); S.HighWater = Stats.Get_HighWaterMark();
        return S;
    }

    bool ValidSample(FCkPoolingGym_PoolSample S)
    { return S.Hits >= 0 && S.Misses >= 0 && S.Live >= 0 && S.Free >= 0 &&
        S.InUse >= 0 && S.HighWater >= S.InUse && S.Live == S.Free + S.InUse; }

    FString Format(FCkPoolingGym_PoolSample S)
    { return f"hits {S.Hits}  misses {S.Misses}  live {S.Live}  free {S.Free}  in-use {S.InUse}  high-water {S.HighWater}"; }
}
