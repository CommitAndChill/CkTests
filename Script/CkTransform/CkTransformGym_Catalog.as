USTRUCT()
struct FCkTransformGym_Row
{
    UPROPERTY() int32 Slot = -1;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() ECk_TransformGym_Behavior Behavior = ECk_TransformGym_Behavior::ECk_MAX;
    UPROPERTY() ACk_TransformGym_Cube Cube;
    UPROPERTY() int32 Corner = 0;
    UPROPERTY() int32 RotationStep = 0;
    UPROPERTY() float Elapsed = 0;
    UPROPERTY() float CornerDistance = 100;
    UPROPERTY() float ScaleSpeed = 1.5;
    UPROPERTY() float MinScale = 0.5;
    UPROPERTY() float MaxScale = 2;
    UPROPERTY() float CircleRadius = 100;
    UPROPERTY() float CircleSpeed = 1;
    UPROPERTY() float RotationSpeed = 45;
    UPROPERTY() float VectorLength = 150;
}
USTRUCT()
struct FCkTransformGym_Snapshot
{
    UPROPERTY() int32 CatalogVersion = 0;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() TArray<FCk_GymStation_SpawnParams> Stations;
    UPROPERTY() TArray<FCkTransformGym_Row> Rows;
    UPROPERTY() TArray<FString> StationTitles;
    UPROPERTY() TArray<FString> StationBodies;
    UPROPERTY() TArray<FString> StationInstructions;
}
USTRUCT()
struct FCkTransformGym_ControlCommit
{
    UPROPERTY() int32 DeliverySerial = 0;
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 OriginGeneration = 0;
    UPROPERTY() ACk_TransformGym_GameState OriginState;
    UPROPERTY() ACk_TransformGym_PlayerController Owner;
    UPROPERTY() ACk_Gym_Base_Pawn OwnerPawn;
    UPROPERTY() FCkTransformGym_Snapshot Snapshot;
}
namespace CkTransformShared
{
    const int32 SlotCount = 6;
    const int32 CatalogVersion = 1;
    bool IsLive(FCk_Handle H)
    { return ck::IsValid(H) && !utils_entity_lifetime::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    ACk_TransformGym_GameState Get_State(UWorld CurrentWorld)
    {
        if (ck::Is_NOT_Valid(CurrentWorld)) { return nullptr; }
        auto State = Cast<ACk_TransformGym_GameState>(CurrentWorld.GetGameState());
        return ck::IsValid(State) && State.GetWorld() == CurrentWorld && State.GetClass() == ACk_TransformGym_GameState ? State : nullptr;
    }
    FName Get_StationTag(int32 Slot)
    {
        if (Slot == 0) { return n"Gym.Transform.SetLocation"; }
        if (Slot == 1) { return n"Gym.Transform.SetRotation"; }
        if (Slot == 2) { return n"Gym.Transform.SetScale"; }
        if (Slot == 3) { return n"Gym.Transform.AddLocationOffset"; }
        if (Slot == 4) { return n"Gym.Transform.AddRotationOffset"; }
        if (Slot == 5) { return n"Gym.Transform.DirectionalVectors"; }
        return n"";
    }
    ECk_TransformGym_Behavior Get_Behavior(int32 Slot)
    {
        if (Slot == 0) { return ECk_TransformGym_Behavior::SetLocation; }
        if (Slot == 1) { return ECk_TransformGym_Behavior::SetRotation; }
        if (Slot == 2) { return ECk_TransformGym_Behavior::SetScale; }
        if (Slot == 3) { return ECk_TransformGym_Behavior::AddLocationOffset; }
        if (Slot == 4) { return ECk_TransformGym_Behavior::AddRotationOffset; }
        if (Slot == 5) { return ECk_TransformGym_Behavior::DirectionalVectors; }
        return ECk_TransformGym_Behavior::ECk_MAX;
    }
    FCk_GymStation_SpawnParams Get_Params(int32 Slot)
    {
        FCk_GymStation_SpawnParams P;
        if (Slot < 0 || Slot >= SlotCount) { return P; }
        P.InitialTransform = FTransform(FRotator(0,180,0), FVector(500,-2000+800*Slot,0));
        P.StationTags.Add(Get_StationTag(Slot));
        if (Slot == 0)
        {
            P.TitleText = FText::FromString("SET LOCATION");
            P.DescriptionText.Add(FText::FromString("Teleports entity between square corner positions."));
            P.DescriptionText.Add(FText::FromString("Uses utils_transform::Request_SetLocation() with World space."));
        }
        if (Slot == 1)
        {
            P.TitleText = FText::FromString("SET ROTATION");
            P.DescriptionText.Add(FText::FromString("Rotates entity to absolute yaw values (0, 90, 180, 270)."));
            P.DescriptionText.Add(FText::FromString("Uses utils_transform::Request_SetRotation() with World space."));
        }
        if (Slot == 2)
        {
            P.TitleText = FText::FromString("SET SCALE");
            P.DescriptionText.Add(FText::FromString("Pulses entity scale between 0.5 and 2.0."));
            P.DescriptionText.Add(FText::FromString("Uses utils_transform::Request_SetScale() with World space."));
        }
        if (Slot == 3)
        {
            P.TitleText = FText::FromString("ADD LOCATION OFFSET");
            P.DescriptionText.Add(FText::FromString("Entity traces a circular path using relative offsets."));
            P.DescriptionText.Add(FText::FromString("Uses utils_transform::Request_AddLocationOffset() each frame."));
        }
        if (Slot == 4)
        {
            P.TitleText = FText::FromString("ADD ROTATION OFFSET");
            P.DescriptionText.Add(FText::FromString("Entity spins continuously with incremental yaw offsets."));
            P.DescriptionText.Add(FText::FromString("Uses utils_transform::Request_AddRotationOffset() each frame."));
        }
        if (Slot == 5)
        {
            P.TitleText = FText::FromString("DIRECTIONAL VECTORS");
            P.DescriptionText.Add(FText::FromString("Draws Forward (red), Right (green), Up (blue) vectors."));
            P.DescriptionText.Add(FText::FromString("Entity rotates slowly to show vector orientation changes."));
        }
        return P;
    }
    // Validate the entire frozen recipe before construction or presentation.
    bool Get_HasFiniteRecipe(FCk_GymStation_SpawnParams InParams, int32 InSlot)
    {
        if (InSlot < 0 || InSlot >= SlotCount || !InParams.InitialTransform.IsValid() ||
            InParams.InitialTransform.ContainsNaN() ||
            !Math::IsFinite(InParams.Width) || !Math::IsFinite(InParams.Depth) ||
            !Math::IsFinite(InParams.Height) || !Math::IsFinite(InParams.WallThickness) ||
            !Math::IsFinite(InParams.FloorThickness) || !Math::IsFinite(InParams.TrimDepth) ||
            !Math::IsFinite(InParams.AgentSpawnOffset) || !Math::IsFinite(InParams.TitleScale) ||
            !Math::IsFinite(InParams.DescriptionScale) || !Math::IsFinite(InParams.FloorDescriptionScale) ||
            !Math::IsFinite(InParams.BodyColour.R) || !Math::IsFinite(InParams.BodyColour.G) ||
            !Math::IsFinite(InParams.BodyColour.B) || !Math::IsFinite(InParams.BodyColour.A) ||
            !Math::IsFinite(InParams.TrimColour.R) || !Math::IsFinite(InParams.TrimColour.G) ||
            !Math::IsFinite(InParams.TrimColour.B) || !Math::IsFinite(InParams.TrimColour.A))
        { return false; }
        return true;
    }

    // Locally generated authority recipes must retain the exact authored transform.
    bool Get_IsExactAuthoredParams(FCk_GymStation_SpawnParams InParams, int32 InSlot)
    {
        if (!Get_HasFiniteRecipe(InParams, InSlot)) { return false; }
        auto Expected = Get_Params(InSlot);
        if (!InParams.InitialTransform.Equals(Expected.InitialTransform, 0.0)) { return false; }
        auto ActualValue = FInstancedStruct::Make(InParams);
        auto ExpectedValue = FInstancedStruct::Make(Expected);
        return ActualValue.IsValid() && ExpectedValue.IsValid() && ActualValue == ExpectedValue;
    }

    // Accept only the authored transform or its exact Iris serialized representation.
    bool Get_IsExactWireParams(FCk_GymStation_SpawnParams InParams, int32 InSlot)
    {
        if (!Get_HasFiniteRecipe(InParams, InSlot)) { return false; }
        auto Expected = Get_Params(InSlot);
        if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
            Expected.InitialTransform, InParams.InitialTransform)) { return false; }
        // Only the already-verified wire-equivalent transform is normalized in this local copy.
        auto Normalized = InParams;
        Normalized.InitialTransform = Expected.InitialTransform;
        auto ActualValue = FInstancedStruct::Make(Normalized);
        auto ExpectedValue = FInstancedStruct::Make(Expected);
        return ActualValue.IsValid() && ExpectedValue.IsValid() && ActualValue == ExpectedValue;
    }

    bool Get_IsExactStationScript(FCk_Handle InStation)
    {
        if (!IsLive(InStation) || !utils_entity_script::Has(InStation)) { return false; }
        auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(InStation);
        return ck::IsValid(Script) && Script.GetClass() == UCk_EntityScript_GymStation;
    }
    bool Get_IsExactStationRecipe(FCk_Handle H, int32 Slot)
    {
        if (Slot < 0 || Slot >= SlotCount || !Get_IsExactStationScript(H) ||
            !H.Has_Fragment(FCkGym_Station_Anchors) || !utils_transform::Has(H)) { return false; }
        auto S = Cast<UCk_EntityScript_GymStation>(utils_entity_script::TryGet_EntityScript(H));
        if (ck::Is_NOT_Valid(S)) { return false; }
        FCk_GymStation_SpawnParams Actual;
        Actual.InitialTransform = S.InitialTransform;
        Actual.Width = S.Width;
        Actual.Depth = S.Depth;
        Actual.Height = S.Height;
        Actual.WallThickness = S.WallThickness;
        Actual.FloorThickness = S.FloorThickness;
        Actual.TrimDepth = S.TrimDepth;
        Actual.AgentSpawnOffset = S.AgentSpawnOffset;
        Actual.BodyColour = S.BodyColour;
        Actual.TrimColour = S.TrimColour;
        Actual.TitleText = S.TitleText;
        Actual.TitleScale = S.TitleScale;
        Actual.TitleColour = S.TitleColour;
        Actual.DescriptionText = S.DescriptionText;
        Actual.DescriptionScale = S.DescriptionScale;
        Actual.DescriptionColour = S.DescriptionColour;
        Actual.TextAlignment = S.TextAlignment;
        Actual.FloorDescriptionText = S.FloorDescriptionText;
        Actual.FloorDescriptionScale = S.FloorDescriptionScale;
        Actual.FloorDescriptionColour = S.FloorDescriptionColour;
        Actual.FloorTextAlignment = S.FloorTextAlignment;
        Actual.FloorTextPlacement = S.FloorTextPlacement;
        Actual.ShowSpotlight = S.ShowSpotlight;
        Actual.ShowAnchors = S.ShowAnchors;
        Actual.ShowDebugOverlays = S.ShowDebugOverlays;
        Actual.AutoSize = S.AutoSize;
        Actual.StationTags = S.StationTags;
        if (!Get_IsExactWireParams(Actual, Slot) ||
            !utils_transform::Get_EntityCurrentTransform(H).Equals(Actual.InitialTransform, 0.0)) { return false; }
        auto A = H.Get_Fragment(FCkGym_Station_Anchors);
        auto T = Actual.InitialTransform;
        double W = Actual.Width * 100.0, D = Actual.Depth * 100.0, Height = Actual.Height * 100.0;
        return A.FootprintCenter.Equals(T.TransformPosition(FVector(0,0,0)),0.0) &&
            A.AgentSpawnFront.Equals(T.TransformPosition(FVector(D*0.5+Actual.AgentSpawnOffset,0,0)),0.0) &&
            A.AgentSpawnBack.Equals(T.TransformPosition(FVector(-D*0.5-Actual.AgentSpawnOffset,0,0)),0.0) &&
            A.AgentSpawnLeft.Equals(T.TransformPosition(FVector(0,W*0.5+Actual.AgentSpawnOffset,0)),0.0) &&
            A.AgentSpawnRight.Equals(T.TransformPosition(FVector(0,-W*0.5-Actual.AgentSpawnOffset,0)),0.0) &&
            A.PanelTopFront.Equals(T.TransformPosition(FVector(D*0.5,0,Height)),0.0) &&
            A.PanelCenter.Equals(T.TransformPosition(FVector(0,0,Height*0.5)),0.0);
    }
    bool TryGet_RequiredGeometry(FCk_Handle Station, TArray<FCk_Handle>& Handles, TArray<FCk_Handle>& Parents)
    {
        Handles.Empty(); Parents.Empty();
        if (!IsLive(Station)) { return false; }
        TArray<FCk_Handle> Queue; Queue.Add(Station); TArray<FCk_Handle> Seen;
        int32 SceneNodes=0, Components=0;
        for (int32 i=0; i<Queue.Num(); ++i)
        {
            auto H=Queue[i]; if (Seen.Contains(H)) { continue; } Seen.Add(H);
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H,ck::TransientEntity(),Children))
            { Handles.Empty(); Parents.Empty(); return false; }
            for (auto Child:Children)
            {
                if (!IsLive(Child)) { continue; }
                if (utils_scene_node::Has(Child) || utils_unreal_component::Has(Child))
                {
                    if (!utils_entity_lifetime::Has_LifetimeOwner(Child) || utils_entity_lifetime::Get_LifetimeOwner(Child)!=H)
                    { Handles.Empty(); Parents.Empty(); return false; }
                    Handles.Add(Child); Parents.Add(H);
                    if (utils_scene_node::Has(Child)) { ++SceneNodes; }
                    if (utils_unreal_component::Has(Child)) { ++Components; }
                    Queue.Add(Child);
                }
            }
        }
        if (SceneNodes==0 || Components==0) { Handles.Empty(); Parents.Empty(); return false; }
        return true;
    }

}
