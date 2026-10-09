// Language=angelscript
// One authority-owned reference bank. Player cameras and controls remain owner-local.

class ACk_CameraGym_SharedFloor : ACk_Gym_Floor
{
    default bReplicates = false;
    default bReplicateMovement = true;

    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        Super::ConstructionScript();
        SetActorScale3D(FVector(40.0f, 40.0f, 0.5f));
    }
}

class ACk_CameraGym_SharedPillar : ACk_Gym_ObstacleWall
{
    default bReplicates = false;
    default bReplicateMovement = true;

    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        Super::ConstructionScript();
        SetActorScale3D(FVector(1.0f, 1.0f, 4.0f));
    }
}

class ACk_CameraGym_SharedBeacon : ACk_CameraGym_Beacon
{
    default bReplicates = false;
    default bReplicateMovement = true;

    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        Super::ConstructionScript();
        SetActorScale3D(FVector(1.5f, 1.5f, 1.5f));
    }
}

enum ECkCameraGym_SourceReadiness
{
    Pending,
    Ready,
    Failed
}

class ACk_CameraGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0f;

    // The actor references may arrive after Ready. No caller uses a prefix.
    UPROPERTY(Replicated) TArray<AActor> SourceActors;
    UPROPERTY(Replicated) FVector SourceCenter = FVector::ZeroVector;
    UPROPERTY(Replicated) int32 SourceGeneration = 0;
    UPROPERTY(Replicated) bool SourceReady = false;
    UPROPERTY(Replicated) bool SourceFailed = false;

    private bool _Attempted = false;
    private bool _Published = false;
    private bool _Ending = false;
    private bool _ClientAccepted = false;
    private bool _ClientFailed = false;
    private UWorld _SourceWorld;
    private UWorld _ClientWorld;
    private FCkGym_Entry _CatalogEntry;
    private int32 _CatalogIndex = -1;
    private TArray<AActor> _RetainedActors;
    private TArray<AActor> _ClientIdentity;

    private bool Get_HasExactCatalog()
    {
        auto World = GetWorld();
        if (_Ending || IsActorBeingDestroyed() ||
            GetClass() != ACk_CameraGym_GameState || !Get_HasPublishedCatalog() ||
            ck::Is_NOT_Valid(World) || World.GetGameState() != this)
        { return false; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_CameraGym_GameMode)
            { return false; }
        }
        else if (World.GetNetMode() != ENetMode::NM_Client) { return false; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return false; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "Camera" ||
            Entry.GameModeClass != ACk_CameraGym_GameMode) { return false; }
        if (_Attempted || _ClientAccepted)
        {
            if (Index != _CatalogIndex ||
                Entry.DisplayName != _CatalogEntry.DisplayName ||
                Entry.GameModeClass != _CatalogEntry.GameModeClass ||
                Entry.LevelName != _CatalogEntry.LevelName ||
                Entry.Category != _CatalogEntry.Category ||
                Entry.HintCode != _CatalogEntry.HintCode)
            { return false; }
        }
        return true;
    }

    private bool Get_HasAuthorityContext()
    {
        return HasAuthority() && Get_HasExactCatalog() &&
            (!_Attempted || GetWorld() == _SourceWorld);
    }

    private bool Get_IsCurrentActor(AActor Actor, UClass ExpectedClass,
        FVector ExpectedLocation, FVector ExpectedScale, bool RequireReplicated)
    {
        if (ck::Is_NOT_Valid(Actor) || Actor.IsActorBeingDestroyed() ||
            Actor.GetClass() != ExpectedClass || Actor.GetWorld() != GetWorld() ||
            Actor.GetOwner() != this || !UCk_CameraGym_LocalAdmission_UE::Get_IsReplicatingMovement(Actor) ||
            (RequireReplicated &&
                !UCk_CameraGym_LocalAdmission_UE::Get_HasReplicationDelivery(Actor)))
        { return false; }
        auto Pose = Actor.GetActorTransform();
        if (!Pose.IsValid() || Pose.ContainsNaN()) { return false; }
        auto Location = Pose.GetLocation();
        auto Scale = Pose.GetScale3D();
        auto Rotation = Pose.GetRotation();
        return !Location.ContainsNaN() && !Scale.ContainsNaN() &&
            Math::IsFinite(Location.X) && Math::IsFinite(Location.Y) &&
            Math::IsFinite(Location.Z) && Math::IsFinite(Scale.X) &&
            Math::IsFinite(Scale.Y) && Math::IsFinite(Scale.Z) &&
            Math::IsFinite(Rotation.X) && Math::IsFinite(Rotation.Y) &&
            Math::IsFinite(Rotation.Z) && Math::IsFinite(Rotation.W) &&
            Rotation.IsNormalized() &&
            Math::Abs(Rotation.X) <= 0.0001f &&
            Math::Abs(Rotation.Y) <= 0.0001f &&
            Math::Abs(Rotation.Z) <= 0.0001f &&
            Math::Abs(Math::Abs(Rotation.W) - 1.0f) <= 0.0001f &&
            Location.Equals(ExpectedLocation, 0.1f) &&
            Scale.Equals(ExpectedScale, 0.01f);
    }

    private FVector Get_RoleLocation(int32 Index) const
    {
        if (Index == 0) { return SourceCenter; }
        if (Index == 9) { return SourceCenter + FVector(600.0f, 0.0f, 150.0f); }
        TArray<FVector2D> Offsets;
        Offsets.Add(FVector2D( 700.0f,    0.0f));
        Offsets.Add(FVector2D(-700.0f,    0.0f));
        Offsets.Add(FVector2D(   0.0f,  700.0f));
        Offsets.Add(FVector2D(   0.0f, -700.0f));
        Offsets.Add(FVector2D( 500.0f,  500.0f));
        Offsets.Add(FVector2D(-500.0f,  500.0f));
        Offsets.Add(FVector2D( 500.0f, -500.0f));
        Offsets.Add(FVector2D(-500.0f, -500.0f));
        return SourceCenter + FVector(Offsets[Index - 1].X, Offsets[Index - 1].Y, 200.0f);
    }

    private bool Get_IsExactBank(TArray<AActor> Actors, bool RequireReplicated)
    {
        if (Actors.Num() != 10 || SourceCenter.ContainsNaN() ||
            !Math::IsFinite(SourceCenter.X) || !Math::IsFinite(SourceCenter.Y) ||
            !Math::IsFinite(SourceCenter.Z)) { return false; }
        for (int32 Index = 0; Index < 10; ++Index)
        {
            UClass ExpectedClass = ACk_CameraGym_SharedPillar;
            FVector ExpectedScale(1.0f, 1.0f, 4.0f);
            if (Index == 0)
            { ExpectedClass = ACk_CameraGym_SharedFloor; ExpectedScale = FVector(40.0f, 40.0f, 0.5f); }
            else if (Index == 9)
            { ExpectedClass = ACk_CameraGym_SharedBeacon; ExpectedScale = FVector(1.5f, 1.5f, 1.5f); }
            if (!Get_IsCurrentActor(Actors[Index], ExpectedClass,
                Get_RoleLocation(Index), ExpectedScale, RequireReplicated)) { return false; }
            for (int32 Previous = 0; Previous < Index; ++Previous)
            { if (Actors[Previous] == Actors[Index]) { return false; } }
        }
        auto Floor = Cast<ACk_CameraGym_SharedFloor>(Actors[0]);
        auto Beacon = Cast<ACk_CameraGym_SharedBeacon>(Actors[9]);
        if (ck::Is_NOT_Valid(Floor) || ck::Is_NOT_Valid(Beacon))
        { return false; }
        auto FloorMesh = Floor.FloorMesh;
        auto BeaconMesh = Beacon.BeaconMesh;
        if (ck::Is_NOT_Valid(FloorMesh) || ck::Is_NOT_Valid(BeaconMesh))
        { return false; }
        if (ck::Is_NOT_Valid(FloorMesh.GetStaticMesh()) ||
            ck::Is_NOT_Valid(BeaconMesh.GetStaticMesh()) ||
            FloorMesh.GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics ||
            FloorMesh.GetCollisionResponseToChannel(ECollisionChannel::ECC_Camera) !=
                ECollisionResponse::ECR_Block ||
            BeaconMesh.GetCollisionEnabled() != ECollisionEnabled::NoCollision)
        { return false; }
        for (int32 Index = 1; Index <= 8; ++Index)
        {
            auto Pillar = Cast<ACk_CameraGym_SharedPillar>(Actors[Index]);
            if (ck::Is_NOT_Valid(Pillar)) { return false; }
            auto WallMesh = Pillar.WallMesh;
            if (ck::Is_NOT_Valid(WallMesh) ||
                ck::Is_NOT_Valid(WallMesh.GetStaticMesh()) ||
                WallMesh.GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics ||
                WallMesh.GetCollisionResponseToChannel(ECollisionChannel::ECC_Camera) !=
                    ECollisionResponse::ECR_Block)
            { return false; }
        }
        return true;
    }

    private void RetireRetainedActors()
    {
        if (!HasAuthority()) { return; }
        for (int32 Index = 0; Index < _RetainedActors.Num(); ++Index)
        {
            auto Actor = _RetainedActors[Index];
            UClass ExpectedClass = ACk_CameraGym_SharedPillar;
            if (Index == 0) { ExpectedClass = ACk_CameraGym_SharedFloor; }
            else if (Index == 9) { ExpectedClass = ACk_CameraGym_SharedBeacon; }
            if (ck::Is_NOT_Valid(Actor) || Actor.IsActorBeingDestroyed() ||
                Actor.GetWorld() != _SourceWorld || Actor.GetClass() != ExpectedClass)
            { continue; }
            // A newly spawned actor may not have reached SetOwner yet. Once
            // published, only the GameState's still-owned source may be retired.
            auto Owner = Actor.GetOwner();
            if (Owner != this && (_Published || Owner != nullptr)) { continue; }
            Actor.DestroyActor();
        }
    }

    private void FailSource()
    {
        SourceFailed = true;
        SourceReady = false;
        // No retry in this session; never retire a transferred actor.
        RetireRetainedActors();
    }

    // Called once by the first ready authority Camera pawn; it supplies the old pawn-spawn center.
    bool TryEnsureReferenceGeometry(ACk_CameraGym_Pawn RequestingPawn)
    {
        if (_Attempted)
        {
            if (!Get_HasAuthorityContext() || !SourceReady || SourceFailed)
            { if (!SourceFailed) { FailSource(); } return false; }
            return Get_ReferenceReadiness() == ECkCameraGym_SourceReadiness::Ready;
        }
        if (!Get_HasAuthorityContext() || ck::Is_NOT_Valid(RequestingPawn) ||
            RequestingPawn.IsActorBeingDestroyed() || RequestingPawn.GetWorld() != GetWorld() ||
            RequestingPawn.GetClass() != ACk_CameraGym_Pawn)
        { return false; }
        _CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        _CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[_CatalogIndex];
        _Attempted = true;
        _SourceWorld = GetWorld();
        SourceCenter = RequestingPawn.GetActorLocation();
        if (SourceCenter.ContainsNaN() ||
            !Math::IsFinite(SourceCenter.X) || !Math::IsFinite(SourceCenter.Y) ||
            !Math::IsFinite(SourceCenter.Z))
        { FailSource(); return false; }

        for (int32 Index = 0; Index < 10; ++Index)
        {
            UClass RoleClass = ACk_CameraGym_SharedPillar;
            AActor Actor = nullptr;
            if (Index == 0)
            {
                RoleClass = ACk_CameraGym_SharedFloor;
                Actor = SpawnActor(ACk_CameraGym_SharedFloor,
                    Get_RoleLocation(Index), FRotator::ZeroRotator, NAME_None, true);
            }
            else if (Index == 9)
            {
                RoleClass = ACk_CameraGym_SharedBeacon;
                Actor = SpawnActor(ACk_CameraGym_SharedBeacon,
                    Get_RoleLocation(Index), FRotator::ZeroRotator, NAME_None, true);
            }
            else
            {
                Actor = SpawnActor(ACk_CameraGym_SharedPillar,
                    Get_RoleLocation(Index), FRotator::ZeroRotator, NAME_None, true);
            }
            if (ck::Is_NOT_Valid(Actor)) { FailSource(); return false; }
            _RetainedActors.Add(Actor);
            Actor.SetOwner(this);
            FinishSpawningActor(Actor);
            if (!Get_IsCurrentActor(Actor, RoleClass, Get_RoleLocation(Index),
                    Index == 0 ? FVector(40.0f, 40.0f, 0.5f) :
                    Index == 9 ? FVector(1.5f, 1.5f, 1.5f) : FVector(1.0f, 1.0f, 4.0f), false))
            { FailSource(); return false; }
        }
        if (!Get_IsExactBank(_RetainedActors, false)) { FailSource(); return false; }
        for (auto Actor : _RetainedActors) { Actor.SetReplicates(true); }
        if (!Get_HasAuthorityContext() || !Get_IsExactBank(_RetainedActors, true))
        { FailSource(); return false; }
        SourceActors = _RetainedActors;
        SourceGeneration = 1;
        _Published = true;
        SourceReady = true;
        ForceNetUpdate();
        return true;
    }

    ECkCameraGym_SourceReadiness Get_ReferenceReadiness()
    {
        if (SourceFailed || _ClientFailed || _Ending || IsActorBeingDestroyed() ||
            GetClass() != ACk_CameraGym_GameState ||
            ck::Is_NOT_Valid(GetWorld()) || GetWorld().GetGameState() != this)
        { return ECkCameraGym_SourceReadiness::Failed; }
        if (!Get_HasPublishedCatalog())
        {
            if (_ClientAccepted || _Attempted) { _ClientFailed = true; return ECkCameraGym_SourceReadiness::Failed; }
            return ECkCameraGym_SourceReadiness::Pending;
        }
        if (!Get_HasExactCatalog() || (_ClientAccepted && GetWorld() != _ClientWorld))
        { _ClientFailed = true; return ECkCameraGym_SourceReadiness::Failed; }
        const int32 Count = SourceActors.Num();
        if ((SourceGeneration != 0 && SourceGeneration != 1) ||
            (Count != 0 && Count != 10) ||
            (_ClientAccepted && (!SourceReady || SourceGeneration != 1 ||
                Count != 10 || _ClientIdentity.Num() != 10)))
        { _ClientFailed = true; return ECkCameraGym_SourceReadiness::Failed; }
        if (SourceCenter.ContainsNaN() ||
            !Math::IsFinite(SourceCenter.X) || !Math::IsFinite(SourceCenter.Y) ||
            !Math::IsFinite(SourceCenter.Z))
        { _ClientFailed = true; return ECkCameraGym_SourceReadiness::Failed; }
        if (Count == 10)
        {
            bool MissingActor = false;
            for (int32 Index = 0; Index < 10; ++Index)
            {
                auto Actor = SourceActors[Index];
                if (Actor == nullptr)
                {
                    if (_ClientAccepted) { _ClientFailed = true; return ECkCameraGym_SourceReadiness::Failed; }
                    MissingActor = true;
                    continue;
                }
                if (ck::Is_NOT_Valid(Actor))
                { _ClientFailed = true; return ECkCameraGym_SourceReadiness::Failed; }
                UClass ExpectedClass = ACk_CameraGym_SharedPillar;
                if (Index == 0) { ExpectedClass = ACk_CameraGym_SharedFloor; }
                else if (Index == 9) { ExpectedClass = ACk_CameraGym_SharedBeacon; }
                if (Actor.IsActorBeingDestroyed() || Actor.GetWorld() != GetWorld() ||
                    Actor.GetClass() != ExpectedClass ||
                    (Actor.GetOwner() != nullptr && Actor.GetOwner() != this) ||
                    (_ClientAccepted && Actor != _ClientIdentity[Index]))
                { _ClientFailed = true; return ECkCameraGym_SourceReadiness::Failed; }
                for (int32 Previous = 0; Previous < Index; ++Previous)
                {
                    if (SourceActors[Previous] == Actor)
                    { _ClientFailed = true; return ECkCameraGym_SourceReadiness::Failed; }
                }
            }
            if (MissingActor) { return ECkCameraGym_SourceReadiness::Pending; }
        }
        if (!SourceReady || SourceGeneration == 0 || Count == 0)
        { return ECkCameraGym_SourceReadiness::Pending; }
        if (!Get_IsExactBank(SourceActors, true))
        {
            if (_ClientAccepted) { _ClientFailed = true; return ECkCameraGym_SourceReadiness::Failed; }
            return ECkCameraGym_SourceReadiness::Pending;
        }
        if (!HasAuthority() && !_ClientAccepted)
        {
            _ClientWorld = GetWorld();
            _CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
            _CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[_CatalogIndex];
            _ClientIdentity = SourceActors;
            _ClientAccepted = true;
        }
        return ECkCameraGym_SourceReadiness::Ready;
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!HasAuthority() || _Ending || !_Attempted || SourceFailed) { return; }
        if (!Get_HasAuthorityContext() || !SourceReady ||
            !Get_IsExactBank(_RetainedActors, true) ||
            SourceActors.Num() != _RetainedActors.Num())
        { FailSource(); }
        else
        {
            for (int32 Index = 0; Index < _RetainedActors.Num(); ++Index)
            {
                if (SourceActors[Index] != _RetainedActors[Index])
                { FailSource(); return; }
            }
        }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        RetireRetainedActors();
    }
}
