// --------------------------------------------------------------------------------------------------------------------
// CkUsf gym PlayerController ("USF Materials"): one station per USF-authored look, each
// with a sphere rendering that look via a runtime MID. Showcases what text-only USF
// material authoring can do - from a simple hologram to per-pixel fractal math.
// --------------------------------------------------------------------------------------------------------------------

class ACk_UsfGym_PlayerController : ACk_Gym_Base_PlayerController
{
    private TArray<AActor> _Showcases;
    private ACk_UsfGym_ToonShowcase _ShadingComparison;
    private int32 _ShadingModelIndex = -1;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();

        Stations.Add(Make_Station(n"Gym.Rendering.UsfHologram", "HOLOGRAM",
            "Emissive scanlines from sin(UV, Time).", "Lightest look - a few sines."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfPlasma", "PLASMA",
            "Animated sum-of-sines colour field.", "Two-colour blend over a sine plasma."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfVoronoi", "VORONOI",
            "Animated cellular distance field.", "3x3 neighbour search, glowing seams."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfJulia", "JULIA FRACTAL",
            "Per-pixel escape-time fractal.", "96 iterations, orbiting constant."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfFbmWarp", "FBM DOMAIN WARP",
            "Domain-warped fractal noise.", "fbm(fbm(fbm)) - 5 octaves each."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfSeascape", "SEASCAPE (TDM)",
            "Raymarched procedural ocean.", "32-step heightfield trace, ported from GLSL."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfAiekick", "DISPLACEMENT (AIEKICK)",
            "Raymarched glass sphere.", "Cubemap reflect/refract + noise displacement."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfTruchet", "TRUCHET CIRCUIT",
            "Animated Truchet arc tiling.", "Per-cell arc flip; glowing connected curves."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfStarfield", "STARFIELD",
            "Parallax twinkling starfield.", "5 jittered-grid depth layers."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfCaustics", "CAUSTICS",
            "Underwater light caustics.", "Domain-distorted sine ridges."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfRimGlow", "RIM GLOW (FRESNEL)",
            "Fresnel rim from normal vs view dir.", "Proves the new world-space surface inputs."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfDissolve", "DISSOLVE (MASKED)",
            "Animated cutout via OpacityMask.", "Masked blend + two-sided; glowing burn edge."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLitMetal", "LIT METAL (PBR)",
            "Scene-lit metal/dielectric checker.", "DefaultLit + Metallic/Specular/Roughness."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfToon", "TOON (NATIVE LIGHTING)",
            "Switch native shading models on the same sphere.", "N / B: next / previous. T: Toon. F: frame sphere."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfDisplace", "DISPLACE (WPO)",
            "Vertex-shader world-position offset.", "Sine wave along normal; coarse on a stock sphere."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfGlass", "GLASS (REFRACTION)",
            "Lit translucent glass with IOR bend.", "Refraction pin + RM_IndexOfRefraction."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfSkin", "SKIN (SUBSURFACE)",
            "Subsurface scatter shading model.", "SubsurfaceColor + opacity-driven scatter."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfCarPaint", "CAR PAINT (CLEARCOAT)",
            "Metallic base under a clear lacquer coat.", "ClearCoat + ClearCoatRoughness custom data."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfPainterlyRuins", "PAINTERLY RUINS",
            "Concept-art landscape: arch ruin on a lake.", "SDF fills + fbm brush edges; mirrored reflection."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfPerInstance", "PER-INSTANCE HUE (ISM)",
            "One material, N instances, distinct colours.", "Per-instance custom data slot 0 -> hue."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfFeedback", "RENDER-TO-TEXTURE",
            "Multi-pass feedback buffer (ping-pong RT).", "BufferA reads itself; Image colorizes."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfPostProcess", "POST-PROCESS (EDGE OUTLINE)",
            "Screen-space depth+normal outline.", "Reads SceneDepth/WorldNormal; unbound PP volume."));

        return Stations;
    }

    private FCkGym_Station_SpawnParams_Payload Make_Station(FName InTag, FString InTitle, FString InLine1, FString InLine2)
    {
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(InTag);
        Station.Title = FText::FromString(InTitle);
        auto Description = TArray<FText>();
        Description.Add(FText::FromString(InLine1));
        Description.Add(FText::FromString(InLine2));
        Station.Description = Description;
        Station.AutoSize = true;
        return Station;
    }

    void Request_StartGym() override
    {
        Request_SpawnAllLooks();
        ck::Trace("* USF Materials Gym - all looks started");
    }

    void Request_SpawnAllLooks()
    {
        // Clear any prior showcases (supports restart).
        for (auto Existing : _Showcases)
        {
            if (ck::IsValid(Existing)) { Existing.DestroyActor(); }
        }
        _Showcases.Empty();
        _ShadingComparison = nullptr;

        Request_SpawnLook(n"Gym.Rendering.UsfHologram", CkUsf::Hologram);
        Request_SpawnLook(n"Gym.Rendering.UsfPlasma",   CkUsf::Plasma);
        Request_SpawnLook(n"Gym.Rendering.UsfVoronoi",  CkUsf::Voronoi);
        Request_SpawnLook(n"Gym.Rendering.UsfJulia",    CkUsf::Julia);
        Request_SpawnLook(n"Gym.Rendering.UsfFbmWarp",  CkUsf::FbmWarp);
        Request_SpawnLook(n"Gym.Rendering.UsfSeascape", CkUsf::Seascape);
        Request_SpawnLook(n"Gym.Rendering.UsfAiekick",  CkUsf::Aiekick);
        Request_SpawnLook(n"Gym.Rendering.UsfTruchet",   CkUsf::Truchet);
        Request_SpawnLook(n"Gym.Rendering.UsfStarfield", CkUsf::Starfield);
        Request_SpawnLook(n"Gym.Rendering.UsfCaustics",  CkUsf::Caustics);
        Request_SpawnLook(n"Gym.Rendering.UsfRimGlow",   CkUsf::RimGlow);
        Request_SpawnLook(n"Gym.Rendering.UsfDissolve",  CkUsf::Dissolve);
        Request_SpawnLook(n"Gym.Rendering.UsfLitMetal",  CkUsf::LitMetal);
        Request_SpawnLook(n"Gym.Rendering.UsfToon",      CkUsf::Toon);
        Request_SpawnLook(n"Gym.Rendering.UsfDisplace",  CkUsf::Displace);
        Request_SpawnLook(n"Gym.Rendering.UsfGlass",     CkUsf::Glass);
        Request_SpawnLook(n"Gym.Rendering.UsfSkin",      CkUsf::Skin);
        Request_SpawnLook(n"Gym.Rendering.UsfCarPaint",  CkUsf::CarPaint);
        Request_SpawnLook(n"Gym.Rendering.UsfPainterlyRuins", CkUsf::PainterlyRuins);
        Request_SpawnPerInstance(n"Gym.Rendering.UsfPerInstance", CkUsf::PerInstanceHue);
        Request_SpawnMultiPass(n"Gym.Rendering.UsfFeedback");
        Request_SpawnPostProcess(n"Gym.Rendering.UsfPostProcess");
    }

    private void Request_SpawnMultiPass(FName InStationTag)
    {
        auto StationTransform = Get_StationTransform(InStationTag.ToString());
        auto Location = StationTransform.Location + FVector(-200.0, 0.0, 150.0);
        auto Actor = SpawnActor(ACk_UsfGym_MultiPassShowcase, Location, FRotator::ZeroRotator);
        if (Actor != nullptr)
        {
            Actor.SetActorScale3D(FVector(2.0, 2.0, 2.0));
            _Showcases.Add(Actor);
            ck::Trace("[OK] Render-to-texture feedback showcase spawned");
        }
    }

    private void Request_SpawnPerInstance(FName InStationTag, UCkUsf_LookDefinition InLook)
    {
        auto StationTransform = Get_StationTransform(InStationTag.ToString());
        auto Location = StationTransform.Location + FVector(-200.0, 0.0, 150.0);
        auto Actor = Cast<ACk_UsfGym_PerInstanceShowcase>(
            SpawnActor(ACk_UsfGym_PerInstanceShowcase, Location, FRotator::ZeroRotator));
        if (Actor != nullptr)
        {
            const int InstanceCount = 8;
            Actor.Request_SetLook(InLook, InstanceCount);
            _Showcases.Add(Actor);
        }
        else
        {
            ck::Error("[FAIL] Failed to spawn USF per-instance showcase actor");
        }
    }

    private void Request_SpawnPostProcess(FName InStationTag)
    {
        auto StationTransform = Get_StationTransform(InStationTag.ToString());
        auto Actor = SpawnActor(ACk_UsfGym_PostProcessShowcase, StationTransform.Location, FRotator::ZeroRotator);
        if (Actor != nullptr)
        {
            _Showcases.Add(Actor);
            ck::Trace("[OK] Post-process edge-outline showcase spawned");
        }
    }

    private void Request_SpawnLook(FName InStationTag, UCkUsf_LookDefinition InLook)
    {
        auto StationTransform = Get_StationTransform(InStationTag.ToString());
        // Place the sphere toward the player (world -X) and raised, in front of the station.
        auto Location = StationTransform.Location + FVector(-200.0, 0.0, 150.0);

        TSubclassOf<ACk_UsfGym_Showcase> ShowcaseClass = ACk_UsfGym_Showcase;
        if (InLook._ShadingModel == ECk_Usf_ShadingModel::Toon)
        { ShowcaseClass = ACk_UsfGym_ToonShowcase; }
        auto Showcase = Cast<ACk_UsfGym_Showcase>(SpawnActor(ShowcaseClass, Location, FRotator::ZeroRotator));
        if (Showcase != nullptr)
        {
            Showcase.SetActorScale3D(FVector(2.0, 2.0, 2.0));
            Showcase.Request_SetLook(InLook);
            _Showcases.Add(Showcase);
            auto Comparison = Cast<ACk_UsfGym_ToonShowcase>(Showcase);
            if (ck::IsValid(Comparison))
            {
                _ShadingComparison = Comparison;
                if (Comparison.Request_InitializeComparison())
                {
                    // The catalog lists Toon last; preserve the selected model across respawns.
                    if (_ShadingModelIndex < 0)
                    { Comparison.Request_SelectSourceMaterial(); }
                    else
                    { Comparison.Request_SelectModel(_ShadingModelIndex); }
                }
            }
        }
        else
        {
            ck::Error("[FAIL] Failed to spawn USF showcase actor");
        }
    }

    //--------------------------------------------------------------------------------------------------------------------------
    // CONTROL PANEL (Script/Common/CkGym_ControlPanel.as)
    //
    // The shared panel owns rendering and input; the comparison swaps compiled MIDs only.
    //--------------------------------------------------------------------------------------------------------------------------

    FString Get_ControlPanelTitle() override
    {
        return "USF MATERIALS";
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Respawn every look"));
        const bool Ready = ck::IsValid(_ShadingComparison) && _ShadingComparison.Get_ModelCount() > 0;
        const auto ModelName = Ready ? _ShadingComparison.Get_ModelName() : "Unavailable";
        Rows.Add(CkGym_Control::Cycle(EKeys::N, "N", "Next shading model", ModelName, false, Ready));
        Rows.Add(CkGym_Control::Action(EKeys::B, "B", "Previous shading model", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::T, "T", "Select CkUsf Toon", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::F, "F", "Frame comparison sphere", ck::IsValid(_ShadingComparison)));
        Rows.Add(CkGym_Control::Action(EKeys::G, "G", "Reload lighting HLSL (may compile slowly)", Ready));
        Rows.Add(CkGym_Control::Status("Material setup", ck::IsValid(_ShadingComparison)
            ? _ShadingComparison.Get_ModelDescription() : "Comparison sphere not spawned", Ready == false));
        Rows.Add(CkGym_Control::Action(EKeys::U, "U", "Use .ush source material", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::L, "L", "Reload material (.ush model and inputs)", Ready));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (InRowIndex == 0)
        { Request_SpawnAllLooks(); }
        else if (InRowIndex == 4)
        { Request_FrameComparison(); }
        else if (ck::IsValid(_ShadingComparison) && _ShadingComparison.Get_ModelCount() > 0)
        {
            const int32 Count = _ShadingComparison.Get_ModelCount();
            if (InRowIndex == 1)
            { Request_SelectShadingModel((_ShadingModelIndex + 1) % Count); }
            else if (InRowIndex == 2)
            { Request_SelectShadingModel((_ShadingModelIndex + Count - 1) % Count); }
            else if (InRowIndex == 3)
            { Request_SelectShadingModel(Count - 1); }
            else if (InRowIndex == 5)
            { System::ExecuteConsoleCommand("Ck_Usf_ReloadShadingModels"); }
            else if (InRowIndex == 7)
            { Ck_UsfGym_UseSourceMaterial(); }
            else if (InRowIndex == 8)
            { System::ExecuteConsoleCommand("Ck_Usf_ReloadShaders"); }
        }
    }

    UFUNCTION(Exec)
    void Ck_UsfGym_SelectShadingModel(int32 InIndex)
    { Request_SelectShadingModel(InIndex); }

    UFUNCTION(Exec)
    void Ck_UsfGym_Control(int32 InRowIndex)
    { Request_ControlActivated(InRowIndex); }

    UFUNCTION(Exec)
    void Ck_UsfGym_UseSourceMaterial()
    {
        if (ck::IsValid(_ShadingComparison) && _ShadingComparison.Request_SelectSourceMaterial())
        { _ShadingModelIndex = -1; }
    }

    private void Request_SelectShadingModel(int32 InIndex)
    {
        if (ck::EnsureIfNot(ck::IsValid(_ShadingComparison), "Comparison sphere is unavailable"))
        { return; }
        if (_ShadingComparison.Request_SelectModel(InIndex))
        { _ShadingModelIndex = InIndex; }
    }

    void Request_FrameComparison()
    {
        auto Pawn = GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn) || ck::Is_NOT_Valid(_ShadingComparison))
        { return; }
        auto Target = _ShadingComparison.GetActorLocation();
        auto Eye = Target + FVector(-280.0, 0.0, 30.0);
        Pawn.SetActorLocation(Eye);
        SetControlRotation((Target - Eye).Rotation());
    }
}
