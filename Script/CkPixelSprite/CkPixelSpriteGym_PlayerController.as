// Language=angelscript

// Three in-world views of the same authored frame: point-sampled shader, edge-smoothed shader,
// and one movable ISM cube per source pixel. The player is the only animation clock.
class ACk_PixelSpriteGym_PlayerController : ACk_Gym_Base_PlayerController
{
    private const int32 CanvasSide = 32;
    private const float PixelSize = 10.0f;

    private UCk_PixelSpritePlayer_UE _Player;
    private UCk_PixelSpriteMaterialPreview_UE _MaterialPreview;
    private UStaticMeshComponent _MaterialFactory;
    private UMaterialInstanceDynamic _NearestMID;
    private UMaterialInstanceDynamic _EdgeAAMID;
    private FCk_Handle _VisualOwner;
    private TArray<FCk_Handle_IsmProxy> _Cubes;
    private TArray<bool> _CubeVisible;
    private TArray<FVector> _StationCenters;
    private int32 _LastFrame = -1;
    private int32 _VisiblePixelCount = 0;
    private bool _Ready = false;
    private bool _Paused = false;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        Stations.Add(Make_Station(n"Gym.PixelSprite.Nearest", "SPRITE: ALIASED SHADER",
            "Raw indexed texels with point sampling.", "Inspect hard pixel edges and the planted feet."));
        Stations.Add(Make_Station(n"Gym.PixelSprite.EdgeAA", "SPRITE: EDGE AA SHADER",
            "The same texture and animation clock, with edge smoothing.",
            "Interior colours stay sharp; only silhouette coverage softens."));
        Stations.Add(Make_Station(n"Gym.PixelSprite.Cubes", "SPRITE: ISM PIXEL BOXES",
            "One instanced cube per source cell, coloured from the current indexed frame.",
            "The origin anchor keeps the feet fixed as the eight frames advance."));

        for (int32 Index = 0; Index < Stations.Num(); Index++)
        {
            auto Transform = FTransform::Identity;
            Transform.SetLocation(FVector(1800.0f, (Index - 1) * 1200.0f, 0.0f));
            Transform.SetRotation(FRotator(0.0f, 180.0f, 0.0f).Quaternion());
            Stations[Index].Transform = Transform;
        }
        return Stations;
    }

    private FCkGym_Station_SpawnParams_Payload Make_Station(FName InTag, FString InTitle,
        FString InDescription, FString InVerdict)
    {
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(InTag);
        Station.Title = FText::FromString(InTitle);
        Station.Description.Add(FText::FromString(InDescription));
        Station.Description.Add(FText::FromString(InVerdict));
        Station.AutoSize = true;
        return Station;
    }

    void Request_StartGym() override
    {
        Request_DestroyVisuals();

        auto Asset = Cast<UCk_PixelSpriteAsset_UE>(LoadObject(this,
            "/Game/PixelSpriteExamples/Sproutling.Sproutling"));
        auto Shader = Cast<UMaterialInterface>(LoadObject(this,
            "/CkTests/PixelSprite/M_PixelSprite_Shader.M_PixelSprite_Shader"));
        auto CubesMaterial = Cast<UMaterialInterface>(LoadObject(this,
            "/CkTests/PixelSprite/M_PixelSprite_Cubes.M_PixelSprite_Cubes"));
        auto Quad = Cast<UStaticMesh>(LoadObject(this,
            "/CkTests/PixelSprite/SM_PixelSprite_Quad.SM_PixelSprite_Quad"));
        auto Cube = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Cube.Cube"));
        if (Asset == nullptr || Shader == nullptr || CubesMaterial == nullptr || Quad == nullptr || Cube == nullptr)
        { Fail_Setup("a required sprite, material, or mesh asset is missing"); return; }

        if (Asset.Get_Width() != CanvasSide || Asset.Get_Height() != CanvasSide)
        { Fail_Setup("Sproutling must be a 32x32 indexed canvas"); return; }

        _Player = Cast<UCk_PixelSpritePlayer_UE>(NewObject(this, UCk_PixelSpritePlayer_UE));
        if (_Player == nullptr || !_Player.Request_LoadAsset(FSoftObjectPath(
            "/Game/PixelSpriteExamples/Sproutling.Sproutling")) || !_Player.Request_SelectAnimation("idle"))
        { Fail_Setup("the Sproutling player could not select idle"); return; }

        FIntPoint Origin;
        if (!_Player.TryResolveAnchor("origin", Origin) || Origin.X != 16 || Origin.Y != 29)
        { Fail_Setup("the current frame lacks the expected 16,29 origin anchor"); return; }

        _StationCenters.Empty();
        auto DisplayOffset = FVector(0.0f, 0.0f, 240.0f);
        _StationCenters.Add(Get_StationAnchorLocation("Gym.PixelSprite.Nearest", ECk_GymStation_Anchor::PanelCenter) + DisplayOffset);
        _StationCenters.Add(Get_StationAnchorLocation("Gym.PixelSprite.EdgeAA", ECk_GymStation_Anchor::PanelCenter) + DisplayOffset);
        _StationCenters.Add(Get_StationAnchorLocation("Gym.PixelSprite.Cubes", ECk_GymStation_Anchor::PanelCenter) + DisplayOffset);
        for (int32 Index = 0; Index < _StationCenters.Num(); Index++)
        {
            if (!ck::IsValid(Get_StationHandle(Get_StationTag(Index))))
            { Fail_Setup("a required gym station is missing"); return; }
        }

        _MaterialFactory = UStaticMeshComponent::Create(this);
        if (_MaterialFactory == nullptr)
        { Fail_Setup("could not create material instances"); return; }
        _NearestMID = _MaterialFactory.CreateDynamicMaterialInstance(0, Shader);
        _EdgeAAMID = _MaterialFactory.CreateDynamicMaterialInstance(1, Shader);
        if (_NearestMID == nullptr || _EdgeAAMID == nullptr)
        { Fail_Setup("could not create both shader material instances"); return; }
        _NearestMID.SetScalarParameterValue(n"EdgeAA", 0.0f);
        _EdgeAAMID.SetScalarParameterValue(n"EdgeAA", 1.0f);

        _MaterialPreview = Cast<UCk_PixelSpriteMaterialPreview_UE>(
            NewObject(this, UCk_PixelSpriteMaterialPreview_UE));
        if (_MaterialPreview == nullptr)
        { Fail_Setup("could not create the shared material presenter"); return; }
        _MaterialPreview.Player = _Player;
        _MaterialPreview.Materials.Add(_NearestMID);
        _MaterialPreview.Materials.Add(_EdgeAAMID);
        if (!_MaterialPreview.Request_Refresh())
        { Fail_Setup("the material presenter rejected the source frame"); return; }

        _VisualOwner = utils_entity_lifetime::Request_CreateEntity(ck::ToEntity(this));
        if (!ck::IsValid(_VisualOwner))
        { Fail_Setup("could not create the visual lifetime owner"); return; }
        auto VisualWorld = utils_entity_lifetime::Get_WorldForEntity(_VisualOwner);
        if (ck::Is_NOT_Valid(VisualWorld))
        { Fail_Setup("the visual lifetime owner has no world"); return; }

        if (!Create_Plane(VisualWorld, Quad, _NearestMID, 0) ||
            !Create_Plane(VisualWorld, Quad, _EdgeAAMID, 1) ||
            !Create_CubeGrid(VisualWorld, Cube, CubesMaterial, Origin))
        { Fail_Setup("could not create all three renderers and pixel proxies"); return; }

        _LastFrame = -1;
        if (!Request_RefreshFrame())
        { Fail_Setup("the initial indexed frame could not be presented"); return; }
        _Ready = true;
        _Paused = false;
        _Player.Request_Play();
        Request_FrameStation(0);
    }

    private FString Get_StationTag(int32 InIndex) const
    {
        if (InIndex == 0) { return "Gym.PixelSprite.Nearest"; }
        if (InIndex == 1) { return "Gym.PixelSprite.EdgeAA"; }
        return "Gym.PixelSprite.Cubes";
    }

    private bool Create_Plane(UWorld InWorld, UStaticMesh InQuad, UMaterialInstanceDynamic InMID, int32 InStation)
    {
        auto Overrides = TArray<FCk_MeshMaterialOverride>();
        Overrides.Add(FCk_MeshMaterialOverride(0, InMID));
        auto Renderer = utils_ism_renderer_transient_factory::GetOrCreate_ForMeshWithMaterials(
            InWorld, InQuad, Overrides, ECk_Mobility::Movable);
        if (ck::Is_NOT_Valid(Renderer)) { return false; }

        FIntPoint Origin;
        if (!_Player.TryResolveAnchor("origin", Origin)) { return false; }
        auto Position = _StationCenters[InStation] + FVector(-100.0f,
            (CanvasSide * 0.5f - Origin.X) * PixelSize,
            (Origin.Y - CanvasSide * 0.5f) * PixelSize);
        auto Entity = utils_entity_lifetime::Request_CreateEntity(_VisualOwner);
        if (!ck::IsValid(Entity)) { return false; }
        auto Transform = utils_transform::Add(Entity,
            FTransform(FRotator::ZeroRotator, Position), ECk_Replication::DoesNotReplicate);
        if (!ck::IsValid(Transform)) { return false; }
        auto Params = FCk_IsmProxy_Spec(Renderer);
        Params.Set_ScaleMultiplier(FVector(1.0f, CanvasSide * PixelSize / 100.0f,
            CanvasSide * PixelSize / 100.0f));
        return ck::IsValid(utils_ism_proxy::Add(Transform, Params));
    }

    private bool Create_CubeGrid(UWorld InWorld, UStaticMesh InCube, UMaterialInterface InMaterial,
        FIntPoint InOrigin)
    {
        auto Overrides = TArray<FCk_MeshMaterialOverride>();
        Overrides.Add(FCk_MeshMaterialOverride(0, InMaterial));
        auto Renderer = utils_ism_renderer_transient_factory::GetOrCreate_ForMeshWithMaterialsAndCustomData(
            InWorld, InCube, Overrides, 3, ECk_Mobility::Movable);
        if (ck::Is_NOT_Valid(Renderer)) { return false; }

        for (int32 Row = 0; Row < CanvasSide; Row++)
        {
            for (int32 Col = 0; Col < CanvasSide; Col++)
            {
                auto Position = _StationCenters[2] + FVector(-100.0f,
                    (Col + 0.5f - InOrigin.X) * PixelSize,
                    (InOrigin.Y - Row - 0.5f) * PixelSize);
                auto Entity = utils_entity_lifetime::Request_CreateEntity(_VisualOwner);
                if (!ck::IsValid(Entity)) { return false; }
                auto Transform = utils_transform::Add(Entity,
                    FTransform(FRotator::ZeroRotator, Position), ECk_Replication::DoesNotReplicate);
                if (!ck::IsValid(Transform)) { return false; }
                auto Params = FCk_IsmProxy_Spec(Renderer);
                Params.Set_StartingState(ECk_EnableDisable::Disable);
                Params.Set_ScaleMultiplier(FVector(PixelSize / 100.0f, PixelSize / 100.0f,
                    PixelSize / 100.0f));
                auto Proxy = utils_ism_proxy::Add(Transform, Params);
                if (!ck::IsValid(Proxy)) { return false; }
                _Cubes.Add(Proxy);
                _CubeVisible.Add(false);
            }
        }
        return _Cubes.Num() == CanvasSide * CanvasSide;
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float InDeltaSeconds)
    {
        if (!_Ready || _Paused) { return; }
        if (!_Player.Request_AdvanceMilliseconds(InDeltaSeconds * 1000.0))
        { Fail_Setup("the sprite player rejected a clock advance"); return; }
        if ((_Player.Get_CurrentFrame() != _LastFrame || _MaterialPreview.Get_RequiresRefresh()) &&
            !Request_RefreshFrame())
        { Fail_Setup("the indexed frame could not be presented"); }
    }

    private bool Request_RefreshFrame()
    {
        auto Pixels = TArray<FColor>();
        FIntPoint Canvas;
        if (!_Player.TryGet_CurrentPixels(Pixels, Canvas) || Canvas.X != CanvasSide ||
            Canvas.Y != CanvasSide || Pixels.Num() != CanvasSide * CanvasSide ||
            _Cubes.Num() != Pixels.Num())
        { return false; }
        if (!_MaterialPreview.Request_Refresh()) { return false; }

        FIntPoint Origin;
        if (!_Player.TryResolveAnchor("origin", Origin) || Origin.X != 16 || Origin.Y != 29)
        { return false; }

        auto Visible = 0;
        for (int32 Index = 0; Index < Pixels.Num(); Index++)
        {
            auto IsVisible = Pixels[Index].A > 0;
            if (IsVisible)
            {
                auto Data = TArray<float32>();
                Data.Add(Srgb_ToLinear(Pixels[Index].R));
                Data.Add(Srgb_ToLinear(Pixels[Index].G));
                Data.Add(Srgb_ToLinear(Pixels[Index].B));
                utils_ism_proxy::Request_SetCustomInstanceData(_Cubes[Index],
                    FCk_Request_IsmProxy_SetCustomInstanceData(Data));
                Visible++;
            }
            if (IsVisible != _CubeVisible[Index])
            {
                utils_ism_proxy::Request_EnableDisable(_Cubes[Index],
                    FCk_Request_IsmProxy_EnableDisable(IsVisible ? ECk_EnableDisable::Enable :
                        ECk_EnableDisable::Disable));
                _CubeVisible[Index] = IsVisible;
            }
        }
        _VisiblePixelCount = Visible;
        _LastFrame = _Player.Get_CurrentFrame();
        return true;
    }

    private float32 Srgb_ToLinear(uint8 InByte) const
    {
        auto Channel = float(InByte) / 255.0;
        if (Channel <= 0.04045) { return float32(Channel / 12.92); }
        return float32(Math::Pow((Channel + 0.055) / 1.055, 2.4));
    }

    private void Request_FrameStation(int32 InIndex)
    {
        if (InIndex < 0 || InIndex >= _StationCenters.Num()) { return; }
        auto Pawn = GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn)) { return; }
        auto Target = _StationCenters[InIndex] + FVector(-100.0f, 0.0f, 130.0f);
        auto Eye = Target + FVector(-500.0f, InIndex == 2 ? 180.0f : 0.0f, 70.0f);
        Pawn.SetActorLocation(Eye);
        SetControlRotation((Target - Eye).Rotation());
    }

    FString Get_ControlPanelTitle() override { return "PIXEL SPRITE"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        Rows.Add(CkGym_Control::Header("FRAMES: one authored animation, three in-world views"));
        Rows.Add(CkGym_Control::Numbered(0, "Aliased shader", false, _Ready));
        Rows.Add(CkGym_Control::Numbered(1, "Edge AA shader", false, _Ready));
        Rows.Add(CkGym_Control::Numbered(2, "ISM pixel boxes", false, _Ready));
        Rows.Add(CkGym_Control::Toggle(EKeys::P, "P", "Pause animation", _Paused, false, _Ready));
        Rows.Add(CkGym_Control::Action(EKeys::N, "N", "Step one frame", _Ready));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Restart animation", _Ready));
        Rows.Add(CkGym_Control::Status("Frame", _Player == nullptr ? "--" : f"{_Player.Get_CurrentFrame() + 1} / 8"));
        Rows.Add(CkGym_Control::Status("Visible boxes", f"{_VisiblePixelCount}"));
        Rows.Add(CkGym_Control::Status("Verdict", _Ready ? "ready" : "setup failed", !_Ready));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!_Ready) { return; }
        if (InRowIndex >= 1 && InRowIndex <= 3) { Request_FrameStation(InRowIndex - 1); }
        else if (InRowIndex == 4)
        {
            _Paused = !_Paused;
            if (_Paused) { _Player.Request_Pause(); }
            else { _Player.Request_Play(); }
        }
        else if (InRowIndex == 5)
        {
            if (!_Paused) { _Paused = true; _Player.Request_Pause(); }
            if (!_Player.Request_StepFrames(1) || !Request_RefreshFrame())
            { Fail_Setup("manual frame stepping failed"); }
        }
        else if (InRowIndex == 6)
        {
            if (!_Player.Request_Restart() || !Request_RefreshFrame())
            { Fail_Setup("animation restart failed"); }
            else if (_Paused) { _Player.Request_Pause(); }
        }
    }

    UFUNCTION(BlueprintPure)
    bool Get_Ready() const { return _Ready; }

    UFUNCTION(BlueprintPure)
    int32 Get_VisiblePixelCount() const { return _VisiblePixelCount; }

    // Read-only settled-state probe for the cooked gym run. The ISM request lane is deferred,
    // so a frame number alone does not prove its per-instance colours have reached the cache.
    UFUNCTION(BlueprintPure)
    int32 Get_SettledPixelCount() const
    {
        if (!_Ready || _Player == nullptr || _Cubes.Num() != CanvasSide * CanvasSide ||
            _CubeVisible.Num() != _Cubes.Num() || _LastFrame != _Player.Get_CurrentFrame())
        { return -1; }

        auto Pixels = TArray<FColor>();
        FIntPoint Canvas;
        if (!_Player.TryGet_CurrentPixels(Pixels, Canvas) || Canvas.X != CanvasSide ||
            Canvas.Y != CanvasSide || Pixels.Num() != _Cubes.Num())
        { return -1; }

        auto Visible = 0;
        for (int32 Index = 0; Index < Pixels.Num(); Index++)
        {
            if (!ck::IsValid(_Cubes[Index])) { return -1; }
            auto IsVisible = Pixels[Index].A > 0;
            if (_CubeVisible[Index] != IsVisible) { return -1; }
            auto Data = utils_ism_proxy::Get_CustomInstanceData(_Cubes[Index]);
            if (Data.Num() != 3) { return -1; }
            if (IsVisible)
            {
                if (!Math::IsNearlyEqual(Data[0], Srgb_ToLinear(Pixels[Index].R), 0.002f) ||
                    !Math::IsNearlyEqual(Data[1], Srgb_ToLinear(Pixels[Index].G), 0.002f) ||
                    !Math::IsNearlyEqual(Data[2], Srgb_ToLinear(Pixels[Index].B), 0.002f))
                { return -1; }
                Visible++;
            }
        }
        return Visible == _VisiblePixelCount ? Visible : -1;
    }

    UFUNCTION(BlueprintPure)
    UCk_PixelSpritePlayer_UE Get_Player() const { return _Player; }

    private void Fail_Setup(FString InReason)
    {
        ck::Error(f"[FAIL] Pixel Sprite gym: {InReason}");
        Request_DestroyVisuals();
    }

    private void Request_DestroyVisuals()
    {
        _Ready = false;
        _Paused = false;
        _LastFrame = -1;
        _VisiblePixelCount = 0;
        _CubeVisible.Empty();
        _Cubes.Empty();
        if (ck::IsValid(_VisualOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_VisualOwner); }
        _VisualOwner = FCk_Handle();
        if (_MaterialPreview != nullptr) { _MaterialPreview.Request_Clear(); }
        _MaterialPreview = nullptr;
        _NearestMID = nullptr;
        _EdgeAAMID = nullptr;
        if (ck::IsValid(_MaterialFactory)) { _MaterialFactory.DestroyComponent(); }
        _MaterialFactory = nullptr;
        _Player = nullptr;
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason EndPlayReason)
    {
        Request_DestroyVisuals();
    }
}
