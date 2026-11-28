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
    private ACk_UsfGym_PostProcessShowcase _Outline;
    private TArray<ACk_UsfGym_Showcase> _StudioShowcases;
    private int32 _StudioIndex = -1;
    private ACk_UsfGym_GlassPaneInspection _GlassPaneInspection;

    UPROPERTY(NotEditable, BlueprintReadOnly)
    private ACk_UsfGym_InspectionLight _InspectionLight;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();

        Stations.Add(Make_Station(n"Gym.Rendering.UsfStudioCeramic", "IVORY GLAZE",
            "Ceramic with a soft, uneven glaze.", "Neutral studio lighting; live StudioCeramic.ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfStudioCopper", "SATIN COPPER",
            "Warm metal with fine machining marks.", "Filtered scratches; live StudioCopper.ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfStudioLacquer", "BURGUNDY LACQUER",
            "Metallic pigment beneath a clear coat.", "Layered highlights; live StudioLacquer.ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfStudioRubber", "GRAPHITE RUBBER",
            "A dark dielectric with a fine grain.", "Soft reflections; live StudioRubber.ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfStudioWax", "PALE GREEN WAX",
            "A stylized subsurface material.", "Existing subsurface path; live StudioWax.ush."));

        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibrarySilk", "CREPE SATIN SILK",
            "Fine woven texture and directional highlights.", "Fiber direction and finish remain live in .ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryLeather", "PEBBLED LEATHER",
            "Clean leather with irregular grain and pores.", "Texture-backed color, normal and roughness."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryTile", "GLAZED CERAMIC TILE",
            "Reflective green glaze and contrasting matte grout.", "Independent glaze and grout responses."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryOak", "FINISHED OAK",
            "Long grain and pores under a restrained finish.", "Texture-backed oak with a live coat response."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryAluminum", "BRUSHED ALUMINUM",
            "Clean reflective metal with directional brushing.", "Brushing and anisotropy remain live in .ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryPlastic", "MATTE MOLDED PLASTIC",
            "Clean polymer with a fine manufactured texture.", "Dielectric highlights and restrained surface relief."));

        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryCanvas", "FINE COTTON / POPLIN",
            "Dry crossed cotton yarns from a poplin source.", "Texture-backed weave and live surface controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibrarySuede", "SUEDE",
            "Fine leather nap with soft grazing fuzz.", "Native Cloth response; live nap amount and tint."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryTerracotta", "TERRACOTTA FLOOR TILE",
            "Warm fired clay with fine pores.", "Texture-backed ceramic with live pore strength."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryMarble", "POLISHED MARBLE",
            "Branching mineral veins beneath a smooth polish.", "Live veins, surface detail and polish."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryRubber", "HARD RUBBER",
            "Dense dark rubber with broad soft reflections.", "Fine texture-backed molding detail."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryGlass", "CLEAR GLASS",
            "Thin-surface transmission and subtle surface detail.", "Texture-backed normal and roughness; live .ush."));

        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryVelvet", "BURGUNDY VELVET",
            "Dense pile with soft grazing fuzz.", "Texture sampling and surface properties remain live in .ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryWalnut", "FINISHED WALNUT",
            "Clean walnut veneer beneath a restrained finish.", "Texture sampling and surface properties remain live in .ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryGranite", "POLISHED GRANITE",
            "Fine mineral grains and polished stone.", "Texture sampling and surface properties remain live in .ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryBrass", "SATIN BRASS",
            "Warm brass reflectance over fine linear machining.", "Texture sampling and surface properties remain live in .ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryFrostedGlass", "FROSTED GLASS",
            "Matte finish with opaque-background screen-space blur.", "Texture sampling and surface properties remain live in .ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryCarbonFiber", "CARBON FIBER",
            "Real crossed-tow texture beneath a resin coat.", "Texture sampling and surface properties remain live in .ush."));

        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryKnit", "KNITTED WOOL",
            "Interlocking yarn loops and soft fiber fuzz.", "Texture-backed inputs and live .ush properties."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryPatentLeather", "PATENT LEATHER",
            "Smooth leather beneath a glossy patent finish.", "Texture-backed inputs and live .ush properties."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryPorcelain", "PORCELAIN-FINISH TILE",
            "Dense white ceramic glaze and matte grout.", "Texture-backed inputs and live .ush properties."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryTerrazzo", "TERRAZZO",
            "Distinct aggregate chips in a polished binder.", "Texture-backed inputs and live .ush properties."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryTranslucentPlastic", "TRANSLUCENT PLASTIC",
            "Colored polymer body and thin transmission.", "Texture-backed inputs and live .ush properties."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryCork", "CORK",
            "Cellular cork with dry pores and soft highlights.", "Texture-backed inputs and live .ush properties."));

        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryDenim", "DENIM TWILL",
            "Diagonal cotton twill with soft fiber highlights.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryConcrete", "SMOOTH CONCRETE",
            "Clean cast surface with fine mineral pores.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryBrick", "CLEAN BRICK",
            "Fired red brick with source-aligned pale mortar.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryStainlessSteel", "SATIN STAINLESS FINISH",
            "Neutral conductor reflectance over a clean metal microfinish.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryPlywood", "PLYWOOD EDGE",
            "Alternating real veneer layers under a light optional seal.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryCardboard", "CARDBOARD",
            "Unprinted paper fibers and subtle corrugated relief.", "Texture-backed surface and live .ush controls."));

        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryLinen", "LINEN",
            "Irregular flax weave with dry fiber highlights.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibrarySmoothLeather", "SMOOTH LEATHER",
            "Fine leather grain with broad dielectric reflections.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryMosaic", "GLAZED MOSAIC",
            "Small glazed tiles with source-aligned matte grout.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryPine", "COATED PINE",
            "Light pine grain beneath a restrained finish.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryLimestone", "LIMESTONE",
            "Fine carbonate stone with subtle pores and mineral variation.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryGalvanizedSteel", "GALVANIZED STEEL",
            "Distinct zinc spangles and variable metal roughness.", "Texture-backed surface and live .ush controls."));

        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryNylon", "TECHNICAL SYNTHETIC WEAVE",
            "Fine polyester source weave with a restrained synthetic sheen.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibrarySlate", "SLATE",
            "Layered mineral clefts and stone seams.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryCopper", "CLEAN COPPER",
            "Warm conductor reflections and fine finish variation.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryGlossyPlastic", "GLOSSY MOLDED PLASTIC",
            "Smooth colored polymer with shallow surface detail.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryWax", "CANDLE WAX",
            "Melted wax texture with an authored subsurface response.", "Texture-backed surface and live .ush controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfLibraryFiberglass", "GLASS-FIBER COMPOSITE",
            "Photographed glass weave beneath an authored resin finish.", "Photo-derived normal and roughness; live .ush."));

        Stations.Add(Make_Station(n"Gym.Rendering.UsfIndustrialFactory", "FACTORY TEAL",
            "Clean industrial paint over steel.", "Intact paint and restrained machining detail."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfIndustrialWorkshop", "WORKSHOP OCHRE",
            "Worn paint exposes primer and steel.", "Scratches, chipped paint and early oxidation."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfIndustrialCoastal", "COASTAL BLUE",
            "Damp steel with spreading corrosion.", "Rust, coating damage and patchy wetness."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfIndustrialAbandoned", "ABANDONED OLIVE",
            "Heavy oxidation beneath settled dust.", "Four aging presets share IndustrialPaint.ush."));

        Stations.Add(Make_Station(n"Gym.Rendering.UsfGroundPom", "RIVER PEBBLES / POM",
            "Dense scanned pebbles, varied sizes and mineral colors.", "Apparent overlap; flat silhouette, depth and shadows."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfGroundDisplacement", "RIVER PEBBLES / DISPLACEMENT",
            "The same height source moves the dense grid vertices.", "Actual geometry relief; GPU displacement leaves collision unchanged."));

        Stations.Add(Make_Station(n"Gym.Rendering.UsfDioramaGrassPom", "GRASS DIORAMA / POM",
            "Arched grass, small flowers, leaves and scattered gravel.", "POM relief; flat silhouette and shadows."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfDioramaGrassDisplacement", "GRASS DIORAMA / DISPLACEMENT",
            "The same grass heightfield moves the dense grid.", "Actual vertex relief; collision remains unchanged."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfDioramaDirtPom", "DIRT DIORAMA / POM",
            "Curled leaf banks, dry soil, small flowers and gravel.", "POM relief; flat silhouette and shadows."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfDioramaDirtDisplacement", "DIRT DIORAMA / DISPLACEMENT",
            "The same dirt heightfield moves the dense grid.", "Actual vertex relief; collision remains unchanged."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfDioramaMudPom", "MUD DIORAMA / POM",
            "Wet basins, rough muddy rims and embedded gravel.", "POM relief; flat silhouette and shadows."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfDioramaMudDisplacement", "MUD DIORAMA / DISPLACEMENT",
            "The same mud heightfield moves the dense grid.", "Actual vertex relief; collision remains unchanged."));

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
            "Native Toon on the standard shader ball.", "Change shading_model in Toon.ush to explore other models."));
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

        Stations.Add(Make_Station(n"Gym.Rendering.UsfNativePorcelainSlab", "NATIVE SLAB - PORCELAIN",
            "Texture-backed ceramic and grout through a Substrate Slab.", "BC, normal, roughness and UV sampling remain live in .ush."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfNativeCeramicToon", "NATIVE SUBSTRATE TOON",
            "The same ceramic textures under UE 5.8 native Toon lighting.", "Live .ush surface inputs; native profile owns lighting ramps."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfNativeRubyVelvet", "NATIVE RUBY VELOUR",
            "Textured nap and a native Slab fuzz response.", "Live .ush fuzz amount, tint, normals and roughness."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfNativeVarnishedWalnut", "NATIVE VARNISHED WALNUT",
            "Sourced wood grain beneath a native simplified coat.", "Independent base roughness, coat coverage and coat roughness."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfNativeResinCarbon", "NATIVE RESIN CARBON WEAVE",
            "Textured crossed tows with a native resin-finish coat.", "Live .ush weave scale, contrast and coat controls."));
        Stations.Add(Make_Station(n"Gym.Rendering.UsfNativeCopper", "NATIVE COPPER CONDUCTOR",
            "Clean copper with colored specular reflectance.", "Texture-backed finish; live .ush F0, roughness and normal sampling."));
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
        if (ck::IsValid(_InspectionLight))
        {
            _InspectionLight.Request_Disable();
            _InspectionLight.DestroyActor();
        }
        _InspectionLight = nullptr;
        // Clear any prior showcases (supports restart).
        for (auto Existing : _Showcases)
        {
            if (ck::IsValid(Existing)) { Existing.DestroyActor(); }
        }
        _Showcases.Empty();
        _ShadingComparison = nullptr;
        _Outline = nullptr;
        _StudioShowcases.Empty();

        auto Environment = SpawnActor(ACk_UsfGym_StudioEnvironment);
        if (ck::IsValid(Environment))
        { _Showcases.Add(Environment); }

        Request_SpawnLook(n"Gym.Rendering.UsfStudioCeramic", CkUsf::StudioCeramic);
        Request_SpawnLook(n"Gym.Rendering.UsfStudioCopper", CkUsf::StudioCopper);
        Request_SpawnLook(n"Gym.Rendering.UsfStudioLacquer", CkUsf::StudioLacquer);
        Request_SpawnLook(n"Gym.Rendering.UsfStudioRubber", CkUsf::StudioRubber);
        Request_SpawnLook(n"Gym.Rendering.UsfStudioWax", CkUsf::StudioWax);

        Request_SpawnLook(n"Gym.Rendering.UsfLibrarySilk", CkUsf::LibrarySilk);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryLeather", CkUsf::LibraryLeather);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryTile", CkUsf::LibraryTile);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryOak", CkUsf::LibraryOak);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryAluminum", CkUsf::LibraryAluminum);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryPlastic", CkUsf::LibraryPlastic);

        Request_SpawnLook(n"Gym.Rendering.UsfLibraryCanvas", CkUsf::LibraryCanvas);
        Request_SpawnLook(n"Gym.Rendering.UsfLibrarySuede", CkUsf::LibrarySuede);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryTerracotta", CkUsf::LibraryTerracotta);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryMarble", CkUsf::LibraryMarble);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryRubber", CkUsf::LibraryRubber);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryGlass", CkUsf::LibraryGlass);

        Request_SpawnLook(n"Gym.Rendering.UsfLibraryVelvet", CkUsf::LibraryVelvet);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryWalnut", CkUsf::LibraryWalnut);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryGranite", CkUsf::LibraryGranite);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryBrass", CkUsf::LibraryBrass);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryFrostedGlass", CkUsf::LibraryFrostedGlass);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryCarbonFiber", CkUsf::LibraryCarbonFiber);

        Request_SpawnLook(n"Gym.Rendering.UsfLibraryKnit", CkUsf::LibraryKnit);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryPatentLeather", CkUsf::LibraryPatentLeather);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryPorcelain", CkUsf::LibraryPorcelain);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryTerrazzo", CkUsf::LibraryTerrazzo);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryTranslucentPlastic", CkUsf::LibraryTranslucentPlastic);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryCork", CkUsf::LibraryCork);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryDenim", CkUsf::LibraryDenim);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryConcrete", CkUsf::LibraryConcrete);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryBrick", CkUsf::LibraryBrick);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryStainlessSteel", CkUsf::LibraryStainlessSteel);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryPlywood", CkUsf::LibraryPlywood);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryCardboard", CkUsf::LibraryCardboard);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryLinen", CkUsf::LibraryLinen);
        Request_SpawnLook(n"Gym.Rendering.UsfLibrarySmoothLeather", CkUsf::LibrarySmoothLeather);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryMosaic", CkUsf::LibraryMosaic);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryPine", CkUsf::LibraryPine);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryLimestone", CkUsf::LibraryLimestone);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryGalvanizedSteel", CkUsf::LibraryGalvanizedSteel);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryNylon", CkUsf::LibraryNylon);
        Request_SpawnLook(n"Gym.Rendering.UsfLibrarySlate", CkUsf::LibrarySlate);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryCopper", CkUsf::LibraryCopper);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryGlossyPlastic", CkUsf::LibraryGlossyPlastic);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryWax", CkUsf::LibraryWax);
        Request_SpawnLook(n"Gym.Rendering.UsfLibraryFiberglass", CkUsf::LibraryFiberglass);
        auto NativeSlab = Cast<UCkUsf_LookDefinition>(LoadObject(this,
            "/CkFoundation/CkUsf/Looks/Native/DA_NativePorcelainSlab.DA_NativePorcelainSlab"));
        auto NativeToon = Cast<UCkUsf_LookDefinition>(LoadObject(this,
            "/CkFoundation/CkUsf/Looks/Native/DA_NativeCeramicToon.DA_NativeCeramicToon"));
        if (ck::IsValid(NativeSlab))
        { Request_SpawnLook(n"Gym.Rendering.UsfNativePorcelainSlab", NativeSlab); }
        if (ck::IsValid(NativeToon))
        { Request_SpawnLook(n"Gym.Rendering.UsfNativeCeramicToon", NativeToon); }
        auto NativeVelvet = Cast<UCkUsf_LookDefinition>(LoadObject(this,
            "/CkFoundation/CkUsf/Looks/Native/DA_NativeRubyVelvet.DA_NativeRubyVelvet"));
        auto NativeWalnut = Cast<UCkUsf_LookDefinition>(LoadObject(this,
            "/CkFoundation/CkUsf/Looks/Native/DA_NativeVarnishedWalnut.DA_NativeVarnishedWalnut"));
        auto NativeCarbon = Cast<UCkUsf_LookDefinition>(LoadObject(this,
            "/CkFoundation/CkUsf/Looks/Native/DA_NativeResinCarbon.DA_NativeResinCarbon"));
        auto NativeCopper = Cast<UCkUsf_LookDefinition>(LoadObject(this,
            "/CkFoundation/CkUsf/Looks/Native/DA_NativeCopper.DA_NativeCopper"));
        if (ck::IsValid(NativeVelvet))
        { Request_SpawnLook(n"Gym.Rendering.UsfNativeRubyVelvet", NativeVelvet); }
        if (ck::IsValid(NativeWalnut))
        { Request_SpawnLook(n"Gym.Rendering.UsfNativeVarnishedWalnut", NativeWalnut); }
        if (ck::IsValid(NativeCarbon))
        { Request_SpawnLook(n"Gym.Rendering.UsfNativeResinCarbon", NativeCarbon); }
        if (ck::IsValid(NativeCopper))
        { Request_SpawnLook(n"Gym.Rendering.UsfNativeCopper", NativeCopper); }

        Request_SpawnLook(n"Gym.Rendering.UsfIndustrialFactory", CkUsf::IndustrialPaint, 0);
        Request_SpawnLook(n"Gym.Rendering.UsfIndustrialWorkshop", CkUsf::IndustrialPaint, 1);
        Request_SpawnLook(n"Gym.Rendering.UsfIndustrialCoastal", CkUsf::IndustrialPaint, 2);
        Request_SpawnLook(n"Gym.Rendering.UsfIndustrialAbandoned", CkUsf::IndustrialPaint, 3);
        Request_SpawnGround(n"Gym.Rendering.UsfGroundPom", false, CkUsf::EnvironmentRiverPebbles, n"Usf.Ground.Pom");
        Request_SpawnGround(n"Gym.Rendering.UsfGroundDisplacement", true, CkUsf::EnvironmentRiverPebbles, n"Usf.Ground.Displacement");
        Request_SpawnGround(n"Gym.Rendering.UsfDioramaGrassPom", false, CkUsf::EnvironmentDioramaGrass, n"Usf.Diorama.Grass.Pom");
        Request_SpawnGround(n"Gym.Rendering.UsfDioramaGrassDisplacement", true, CkUsf::EnvironmentDioramaGrass, n"Usf.Diorama.Grass.Displacement");
        Request_SpawnGround(n"Gym.Rendering.UsfDioramaDirtPom", false, CkUsf::EnvironmentDioramaDirt, n"Usf.Diorama.Dirt.Pom");
        Request_SpawnGround(n"Gym.Rendering.UsfDioramaDirtDisplacement", true, CkUsf::EnvironmentDioramaDirt, n"Usf.Diorama.Dirt.Displacement");
        Request_SpawnGround(n"Gym.Rendering.UsfDioramaMudPom", false, CkUsf::EnvironmentDioramaMud, n"Usf.Diorama.Mud.Pom");
        Request_SpawnGround(n"Gym.Rendering.UsfDioramaMudDisplacement", true, CkUsf::EnvironmentDioramaMud, n"Usf.Diorama.Mud.Displacement");

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
        if (_StudioIndex < 0 || _StudioIndex >= _StudioShowcases.Num())
        { _StudioIndex = _StudioShowcases.Num() - 1; }
        _InspectionLight = Cast<ACk_UsfGym_InspectionLight>(SpawnActor(ACk_UsfGym_InspectionLight));
        if (ck::EnsureIfNot(ck::IsValid(_InspectionLight), "Could not create the selected-station inspection light"))
        { return; }
        _Showcases.Add(_InspectionLight);
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
            _Outline = Cast<ACk_UsfGym_PostProcessShowcase>(Actor);
            _Showcases.Add(Actor);
            ck::Trace("[OK] Post-process edge-outline showcase spawned");
        }
    }

    private void Request_SpawnGround(FName InStationTag, bool InDisplacement,
        UCkUsf_LookDefinition InLook, FName InModeTag)
    {
        if (ck::EnsureIfNot(ck::IsValid(InLook) && InModeTag != NAME_None, "The ground look and mode tag are required"))
        { return; }
        auto StationTransform = Get_StationTransform(InStationTag.ToString());
        auto Ground = Cast<ACk_UsfGym_GroundShowcase>(SpawnActor(ACk_UsfGym_GroundShowcase,
            StationTransform.Location + FVector(-200.0, 0.0, 110.0), FRotator::ZeroRotator));
        if (ck::EnsureIfNot(ck::IsValid(Ground), "Could not create the ground-height specimen"))
        { return; }
        Ground.SetActorScale3D(FVector(1.0, 1.0, 1.0));
        Ground.Request_ConfigurePresentation();
        Ground.Request_ConfigureGroundPresentation();
        Ground.Request_SetLook(InLook);
        auto Material = Cast<UMaterialInstanceDynamic>(Ground.Mesh.GetMaterial(Ground.Get_SpecimenSlot()));
        if (ck::EnsureIfNot(ck::IsValid(Material), "Generate the ground-height material before PIE"))
        {
            Ground.DestroyActor();
            return;
        }
        Material.SetScalarParameterValue(n"PomAmount", InDisplacement ? 0.0 : 1.0);
        Material.SetScalarParameterValue(n"DisplacementAmount", InDisplacement ? 1.0 : 0.0);
        Ground.Tags.Add(InModeTag);
        _StudioShowcases.Add(Ground);
        _Showcases.Add(Ground);
    }

    private void Request_SpawnLook(FName InStationTag, UCkUsf_LookDefinition InLook, int32 InPreset = 0)
    {
        auto StationTransform = Get_StationTransform(InStationTag.ToString());
        // Place the sphere toward the player (world -X) and raised, in front of the station.
        auto Location = StationTransform.Location + FVector(-200.0, 0.0, 150.0);

        TSubclassOf<ACk_UsfGym_Showcase> ShowcaseClass = ACk_UsfGym_Showcase;
        const auto Name = InLook.Get_EffectiveLookName();
        if (Name == n"IndustrialPaint")
        { ShowcaseClass = ACk_UsfGym_IndustrialShowcase; }
        else if (Name == n"LibrarySilk" || Name == n"LibraryLeather" || Name == n"LibraryTile"
            || Name == n"LibraryOak" || Name == n"LibraryAluminum" || Name == n"LibraryPlastic"
            || Name == n"LibraryCanvas" || Name == n"LibrarySuede" || Name == n"LibraryTerracotta"
            || Name == n"LibraryMarble" || Name == n"LibraryRubber" || Name == n"LibraryGlass"
            || Name == n"LibraryVelvet" || Name == n"LibraryWalnut" || Name == n"LibraryGranite"
            || Name == n"LibraryBrass" || Name == n"LibraryFrostedGlass" || Name == n"LibraryCarbonFiber"
            || Name == n"LibraryKnit" || Name == n"LibraryPatentLeather" || Name == n"LibraryPorcelain" || Name == n"LibraryTerrazzo" || Name == n"LibraryTranslucentPlastic" || Name == n"LibraryCork"
            || Name == n"LibraryDenim" || Name == n"LibraryConcrete" || Name == n"LibraryBrick" || Name == n"LibraryStainlessSteel" || Name == n"LibraryPlywood" || Name == n"LibraryCardboard"
            || Name == n"LibraryLinen" || Name == n"LibrarySmoothLeather" || Name == n"LibraryMosaic" || Name == n"LibraryPine" || Name == n"LibraryLimestone" || Name == n"LibraryGalvanizedSteel"
            || Name == n"LibraryNylon" || Name == n"LibrarySlate" || Name == n"LibraryCopper"
            || Name == n"LibraryGlossyPlastic" || Name == n"LibraryWax" || Name == n"LibraryFiberglass"
            || Name == n"NativePorcelainSlab" || Name == n"NativeCeramicToon"
            || Name == n"NativeRubyVelvet" || Name == n"NativeVarnishedWalnut"
            || Name == n"NativeResinCarbon" || Name == n"NativeCopper")
        { ShowcaseClass = ACk_UsfGym_LibraryShowcase; }
        else if (Name == n"StudioCeramic" || Name == n"StudioCopper" || Name == n"StudioRubber"
            || Name == n"StudioLacquer" || Name == n"StudioWax")
        { ShowcaseClass = ACk_UsfGym_StudioShowcase; }
        else if (Name == n"Julia" || Name == n"Seascape" || Name == n"Aiekick"
            || Name == n"PainterlyRuins" || Name == n"Starfield")
        { ShowcaseClass = ACk_UsfGym_ImageShowcase; }
        else if (Name == n"Displace")
        { ShowcaseClass = ACk_UsfGym_DisplaceShowcase; }
        else if (InLook._ShadingModel == ECk_Usf_ShadingModel::Toon)
        { ShowcaseClass = ACk_UsfGym_ToonShowcase; }
        auto Showcase = Cast<ACk_UsfGym_Showcase>(SpawnActor(ShowcaseClass, Location, FRotator::ZeroRotator));
        if (Showcase != nullptr)
        {
            Showcase.SetActorScale3D(FVector(2.0, 2.0, 2.0));
            auto Studio = Cast<ACk_UsfGym_StudioShowcase>(Showcase);
            if (ck::IsValid(Studio))
            {
                Studio.Request_ConfigurePresentation();
                _StudioShowcases.Add(Showcase);
            }
            Showcase.Request_SetLook(InLook);
            if (Name == n"LibraryTranslucentPlastic")
            {
                auto BodyFill = UPointLightComponent::Create(Showcase);
                if (ck::EnsureIfNot(ck::IsValid(BodyFill), "Could not create the translucent plastic body fill"))
                {
                    _StudioShowcases.Remove(Showcase);
                    Showcase.DestroyActor();
                    return;
                }
                BodyFill.ComponentTags.Add(n"Usf.TranslucentBodyFill");
                BodyFill.SetMobility(EComponentMobility::Movable);
                BodyFill.SetWorldScale3D(FVector(1.0, 1.0, 1.0));
                BodyFill.SetWorldLocation(Showcase.GetActorLocation() + FVector(-140.0, -100.0, 140.0));
                BodyFill.SetLightingChannels(false, true, false);
                BodyFill.SetIntensityUnits(ELightUnits::Candelas);
                BodyFill.SetIntensity(80.0);
                BodyFill.SetSourceRadius(15.0);
                BodyFill.SetAttenuationRadius(300.0);
                BodyFill.SetCastShadows(false);
                BodyFill.SetLightColor(FLinearColor::White);
            }
            auto Industrial = Cast<ACk_UsfGym_IndustrialShowcase>(Showcase);
            if (ck::IsValid(Industrial))
            { Industrial.Request_ConfigurePreset(InPreset); }
            _Showcases.Add(Showcase);
            auto Comparison = Cast<ACk_UsfGym_ToonShowcase>(Showcase);
            if (ck::IsValid(Comparison))
            {
                _ShadingComparison = Comparison;
                Comparison.Request_InitializeSourceMaterial();
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
        const bool Ready = ck::IsValid(_ShadingComparison);
        const bool StudioReady = _StudioIndex >= 0 && _StudioIndex < _StudioShowcases.Num();
        auto MaterialName = StudioReady ? _StudioShowcases[_StudioIndex].Get_LookName() : "Unavailable";
        if (StudioReady)
        {
            auto Industrial = Cast<ACk_UsfGym_IndustrialShowcase>(_StudioShowcases[_StudioIndex]);
            if (ck::IsValid(Industrial))
            { MaterialName = Industrial.Get_PresetTitle(); }
        }
        Rows.Add(CkGym_Control::Cycle(EKeys::N, "N", "Next studio material", MaterialName, false, StudioReady));
        Rows.Add(CkGym_Control::Action(EKeys::B, "B", "Previous studio material", StudioReady));
        Rows.Add(CkGym_Control::Action(EKeys::T, "T", "Select Toon source", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::F, "F", "Frame selected material", StudioReady));
        Rows.Add(CkGym_Control::Action(EKeys::G, "G", "Reload lighting HLSL (may compile slowly)", Ready));
        Rows.Add(CkGym_Control::Status("Studio", "Mixed materials and height-mapped ground specimens", StudioReady == false));
        Rows.Add(CkGym_Control::Action(EKeys::U, "U", "Use .ush source material", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::L, "L", "Reload material (.ush model and inputs)", Ready));
        Rows.Add(CkGym_Control::Toggle(EKeys::O, "O", "Edge outline",
            ck::IsValid(_Outline) && _Outline.Get_IsEnabled(), false, ck::IsValid(_Outline)));
        const auto InspectionReady = ck::IsValid(_InspectionLight) && ck::IsValid(Get_SelectedInspectionTarget());
        const auto OrbitEnabled = ck::IsValid(_InspectionLight) && _InspectionLight.Get_IsEnabled();
        Rows.Add(CkGym_Control::Toggle(EKeys::I, "I", "Orbit light around selected material", OrbitEnabled, false, InspectionReady));
        Rows.Add(CkGym_Control::Toggle(EKeys::P, "P", "Pause inspection light",
            ck::IsValid(_InspectionLight) && _InspectionLight.Get_IsPaused(), false, OrbitEnabled));
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Reset inspection light phase", OrbitEnabled));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (InRowIndex == 0)
        { Request_SpawnAllLooks(); }
        else if (InRowIndex == 4)
        { Request_FrameComparison(); }
        else if (InRowIndex == 9 && ck::IsValid(_Outline))
        { _Outline.Request_Toggle(); }
        else if (InRowIndex == 1 || InRowIndex == 2)
        {
            if (_StudioShowcases.IsEmpty())
            { return; }
            const auto Step = InRowIndex == 1 ? 1 : _StudioShowcases.Num() - 1;
            _StudioIndex = (_StudioIndex + Step) % _StudioShowcases.Num();
            Request_FrameComparison();
        }
        else if (InRowIndex == 3 || InRowIndex == 7)
        { Ck_UsfGym_UseSourceMaterial(); }
        else if (InRowIndex == 5)
        { System::ExecuteConsoleCommand("Ck_Usf_ReloadShadingModels"); }
        else if (InRowIndex == 8)
        { System::ExecuteConsoleCommand("Ck_Usf_ReloadShaders"); }
        else if (InRowIndex == 10 && ck::IsValid(_InspectionLight))
        { Ck_UsfGym_SetOrbit(_InspectionLight.Get_IsEnabled() == false); }
        else if (InRowIndex == 11 && ck::IsValid(_InspectionLight))
        { Ck_UsfGym_SetOrbitPaused(_InspectionLight.Get_IsPaused() == false); }
        else if (InRowIndex == 12)
        { Ck_UsfGym_ResetOrbit(); }
    }

    UFUNCTION(Exec)
    void Ck_UsfGym_SetOrbit(bool InEnabled)
    {
        if (ck::EnsureIfNot(ck::IsValid(_InspectionLight), "The inspection light has not been created"))
        { return; }
        if (InEnabled == false)
        { _InspectionLight.Request_Disable(); return; }
        _InspectionLight.Try_Enable(Get_SelectedInspectionTarget());
    }

    UFUNCTION(Exec)
    void Ck_UsfGym_SetOrbitPaused(bool InPaused)
    {
        if (ck::EnsureIfNot(ck::IsValid(_InspectionLight), "The inspection light has not been created"))
        { return; }
        _InspectionLight.Request_SetPaused(InPaused);
    }

    UFUNCTION(Exec)
    void Ck_UsfGym_SetOrbitPhase(float InPhaseDegrees)
    {
        if (ck::EnsureIfNot(ck::IsValid(_InspectionLight), "The inspection light has not been created"))
        { return; }
        _InspectionLight.Request_SetPhaseDegrees(InPhaseDegrees);
    }

    UFUNCTION(Exec)
    void Ck_UsfGym_ResetOrbit()
    { Ck_UsfGym_SetOrbitPhase(0.0); }

    private ACk_UsfGym_StudioShowcase Get_SelectedInspectionTarget() const
    {
        if (_StudioIndex < 0 || _StudioIndex >= _StudioShowcases.Num())
        { return nullptr; }
        auto Selected = _StudioShowcases[_StudioIndex];
        if (ck::Is_NOT_Valid(Selected))
        { return nullptr; }
        return Cast<ACk_UsfGym_StudioShowcase>(Selected);
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason EndPlayReason)
    {
        if (ck::IsValid(_InspectionLight))
        {
            _InspectionLight.Request_Disable();
            _InspectionLight.DestroyActor();
        }
        _InspectionLight = nullptr;
    }

    UFUNCTION(Exec)
    void Ck_UsfGym_SelectShadingModel(int32 InIndex)
    {
        if (ck::Is_NOT_Valid(_ShadingComparison))
        { return; }
        if (_ShadingComparison.Get_ModelCount() == 0 && _ShadingComparison.Request_InitializeComparison() == false)
        { return; }
        Request_SelectShadingModel(InIndex);
    }

    UFUNCTION(Exec)
    void Ck_UsfGym_Control(int32 InRowIndex)
    { Request_ControlActivated(InRowIndex); }

    UFUNCTION(Exec)
    void Ck_UsfGym_GlassPane(bool InEnabled, FName InLookName = n"LibraryGlass", bool InFlatSurface = false)
    {
        if (ck::IsValid(_GlassPaneInspection))
        { _GlassPaneInspection.DestroyActor(); }
        if (!InEnabled)
        { return; }
        ACk_UsfGym_Showcase Glass;
        ACk_UsfGym_Showcase Tile;
        for (auto Showcase : _StudioShowcases)
        {
            if (Showcase.Get_LookName() == InLookName.ToString())
            { Glass = Showcase; }
            else if (Showcase.Get_LookName() == "LibraryTile")
            { Tile = Showcase; }
        }
        if (ck::EnsureIfNot(ck::IsValid(Glass) && ck::IsValid(Tile), "Open both glass and tile specimens before pane inspection"))
        { return; }
        auto Minimum = FVector::ZeroVector;
        auto Maximum = FVector::ZeroVector;
        Glass.Mesh.GetLocalBounds(Minimum, Maximum);
        auto Center = Glass.GetActorTransform().TransformPosition((Minimum + Maximum) * 0.5);
        _GlassPaneInspection = Cast<ACk_UsfGym_GlassPaneInspection>(SpawnActor(
            ACk_UsfGym_GlassPaneInspection, Center + FVector(-120.0, 0.0, 0.0), FRotator::ZeroRotator));
        if (ck::EnsureIfNot(ck::IsValid(_GlassPaneInspection), "Could not create the glass pane inspection"))
        { return; }
        auto Configured = false;
        if (InFlatSurface)
        { Configured = _GlassPaneInspection.Try_ConfigureFlat(Glass.Mesh.GetMaterial(Glass.Get_SpecimenSlot())); }
        else
        { Configured = _GlassPaneInspection.Try_Configure(Glass.Mesh.GetMaterial(Glass.Get_SpecimenSlot()),
            Tile.Mesh.GetMaterial(Tile.Get_SpecimenSlot())); }
        if (!Configured)
        {
            _GlassPaneInspection.DestroyActor();
            return;
        }
        _Showcases.Add(_GlassPaneInspection);
    }

    UFUNCTION(Exec)
    void Ck_UsfGym_UseSourceMaterial()
    {
        if (ck::IsValid(_ShadingComparison) && _ShadingComparison.Request_SelectSourceMaterial())
        {
            _ShadingModelIndex = -1;
            for (int32 Index = 0; Index < _StudioShowcases.Num(); ++Index)
            {
                if (_StudioShowcases[Index] == _ShadingComparison)
                { _StudioIndex = Index; break; }
            }
            Request_FrameComparison();
        }
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
        if (ck::IsValid(_InspectionLight) && _InspectionLight.Get_IsEnabled())
        { _InspectionLight.Try_Enable(Get_SelectedInspectionTarget()); }
        if (_StudioIndex < 0 || _StudioIndex >= _StudioShowcases.Num())
        { return; }
        auto Specimen = _StudioShowcases[_StudioIndex];
        if (ck::Is_NOT_Valid(Specimen) || ck::Is_NOT_Valid(Specimen.Mesh))
        { return; }
        auto Pawn = GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn))
        { return; }
        if (ck::Is_NOT_Valid(Specimen.Mesh.GetStaticMesh()))
        { return; }
        auto Minimum = FVector::ZeroVector;
        auto Maximum = FVector::ZeroVector;
        Specimen.Mesh.GetLocalBounds(Minimum, Maximum);
        auto Target = Specimen.GetActorTransform().TransformPosition((Minimum + Maximum) * 0.5);
        auto Extent = (Maximum - Minimum) * Specimen.GetActorScale3D() * 0.5;
        const auto Distance = Math::Max(180.0, Math::Max(Extent.X, Math::Max(Extent.Y, Extent.Z)) * 2.6);
        auto Eye = Target + FVector(-Distance, 0.0, Extent.Z * 0.3);
        if (Cast<ACk_UsfGym_GroundShowcase>(Specimen) != nullptr)
        { Eye = Target + FVector(-65.0, 0.0, 45.0); }
        Pawn.SetActorLocation(Eye);
        SetControlRotation((Target - Eye).Rotation());
    }
}
