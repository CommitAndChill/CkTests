// --------------------------------------------------------------------------------------------------------------------
// Showcase actor for the CkUsf gym: a standard specimen whose material is a runtime MID created
// from a USF-authored look. Proves the full path:
//   text .ush + AS LookDefinition -> generated master -> MID -> rendered on a mesh.
//
// The generated master must exist on disk first (run "Generate Look Materials" from the
// CkUsf editor subsystem). If it doesn't, MID creation returns null and we log a warning.
// --------------------------------------------------------------------------------------------------------------------

class ACk_UsfGym_Showcase : AActor
{
    private FString _LookName;

    FString Get_LookName() const
    { return _LookName; }

    int32 Get_SpecimenSlot() const
    { return Mesh.GetNumMaterials() > 1 ? Mesh.GetMaterialIndex(n"Specimen") : 0; }

    UPROPERTY(DefaultComponent, RootComponent)
    UStaticMeshComponent Mesh;

    default Mesh.Mobility = EComponentMobility::Movable;

    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Specimen = Cast<UStaticMesh>(LoadObject(this,
            "/CkTests/CkUsf/Showcase/SM_UsdShaderBall.SM_UsdShaderBall"));
        if (ck::EnsureIfNot(ck::IsValid(Specimen), "Prepare the standard USF shader-ball asset before PIE"))
        { return; }
        Mesh.SetStaticMesh(Specimen);
    }

    // Called by the gym PlayerController right after spawn to pick which look to render.
    void Request_SetLook(UCkUsf_LookDefinition InLook)
    {
        auto MID = UCk_Utils_Usf_UE::Create_MID_ForLook(InLook, this);
        if (MID != nullptr)
        {
            if (Mesh.GetNumMaterials() > 1)
            {
                const auto CoreSlot = Mesh.GetMaterialIndex(n"CoreBase");
                if (ck::EnsureIfNot(Get_SpecimenSlot() >= 0 && CoreSlot >= 0,
                    "The shader ball requires named Specimen and CoreBase slots"))
                { return; }
                auto Stand = utils_usf::Create_MID_ForLook(CkUsf::StudioRubber, this);
                if (ck::EnsureIfNot(ck::IsValid(Stand), "Generate the studio neutral material before PIE"))
                { return; }
                Mesh.SetMaterial(CoreSlot, Stand);
            }
            Mesh.SetMaterial(Get_SpecimenSlot(), MID);
            auto LookName = InLook.Get_EffectiveLookName();
            _LookName = f"{LookName}";
            ck::Trace(f"[OK] CkUsf gym: applied look [{LookName}]");
        }
        else
        {
            ck::Warning("[FAIL] CkUsf gym: MID creation failed - run 'Generate Look Materials' first");
        }
    }
}

class ACk_UsfGym_ToonShowcase : ACk_UsfGym_StudioShowcase
{
    default Mesh.bDisallowNanite = true;

    private UCk_UsfGym_ShadingModelCatalog _Catalog;
    private TArray<UMaterialInstanceDynamic> _ModelMIDs;
    private int32 _SelectedModel = -1;
    private UMaterialInstanceDynamic _SourceMID;
    private bool _UseSource = false;

    UPROPERTY(DefaultComponent, Attach = Mesh)
    UStaticMeshComponent MaterialFactory;
    default MaterialFactory.SetVisibility(false);

    bool Request_InitializeSourceMaterial()
    {
        _SourceMID = Cast<UMaterialInstanceDynamic>(Mesh.GetMaterial(Get_SpecimenSlot()));
        return Request_SelectSourceMaterial();
    }

    bool Request_InitializeComparison()
    {
        if (ck::Is_NOT_Valid(_SourceMID) && Request_InitializeSourceMaterial() == false)
        { return false; }
        auto Catalog = Cast<UCk_UsfGym_ShadingModelCatalog>(LoadObject(this,
            "/CkTests/CkUsf/GeneratedShadingModels/DA_UsfGym_ShadingModels_v2.DA_UsfGym_ShadingModels_v2"));
        if (ck::EnsureIfNot(ck::IsValid(Catalog),
            "Generate comparison materials outside PIE with Ck_GymUsf_GenerateShadingModels"))
        { return false; }
        auto ExpectedNames = TArray<FString>();
        ExpectedNames.Add("Unlit");
        ExpectedNames.Add("Default Lit");
        ExpectedNames.Add("Subsurface");
        ExpectedNames.Add("Preintegrated Skin");
        ExpectedNames.Add("Clear Coat");
        ExpectedNames.Add("Subsurface Profile");
        ExpectedNames.Add("Two Sided Foliage");
        ExpectedNames.Add("Hair");
        ExpectedNames.Add("Cloth");
        ExpectedNames.Add("Eye");
        ExpectedNames.Add("Single Layer Water");
        ExpectedNames.Add("Thin Translucent");
        ExpectedNames.Add("CkUsf Toon");
        if (ck::EnsureIfNot(Catalog.Models.Num() == ExpectedNames.Num(),
            "Incomplete shading comparison catalog; regenerate it outside PIE"))
        { return false; }

        for (int32 Index = 0; Index < ExpectedNames.Num(); ++Index)
        {
            if (ck::EnsureIfNot(Catalog.Models[Index].DisplayName == ExpectedNames[Index],
                "Shading comparison catalog order changed; regenerate it outside PIE"))
            { return false; }
            if (ck::EnsureIfNot(ck::IsValid(Catalog.Models[Index].Material), "Comparison material is missing"))
            { return false; }
        }

        auto Candidates = TArray<UMaterialInstanceDynamic>();
        for (int32 Index = 0; Index < Catalog.Models.Num(); ++Index)
        {
            auto Candidate = MaterialFactory.CreateDynamicMaterialInstance(Index, Catalog.Models[Index].Material);
            if (ck::EnsureIfNot(ck::IsValid(Candidate), "Comparison MID creation failed"))
            { return false; }
            Candidates.Add(Candidate);
        }
        _Catalog = Catalog;
        _ModelMIDs = Candidates;
        return true;
    }

    int32 Get_ModelCount() const
    { return _ModelMIDs.Num(); }

    int32 Get_SelectedModel() const
    { return _SelectedModel; }

    FString Get_ModelName() const
    {
        if (_UseSource)
        { return "Toon.ush (source material)"; }
        if (_SelectedModel < 0)
        { return "Comparison unavailable"; }
        return _Catalog.Models[_SelectedModel].DisplayName;
    }

    FString Get_ModelDescription() const
    {
        if (_UseSource)
        { return "Edit shading_model in Toon.ush; save or press L to reload this material."; }
        if (_SelectedModel < 0)
        { return "Stop PIE, run Ck_GymUsf_GenerateShadingModels, then play again."; }
        return _Catalog.Models[_SelectedModel].Description;
    }

    bool Request_SelectModel(int32 InIndex)
    {
        if (ck::EnsureIfNot(InIndex >= 0 && InIndex < _ModelMIDs.Num(), "Invalid comparison model index"))
        { return false; }
        Mesh.SetMaterial(Get_SpecimenSlot(), _ModelMIDs[InIndex]);
        _UseSource = false;
        _SelectedModel = InIndex;
        ck::Trace(f"USF_SHADING_MODEL_SELECTED: {Get_ModelName()}");
        return true;
    }

    bool Request_SelectSourceMaterial()
    {
        if (ck::EnsureIfNot(ck::IsValid(_SourceMID), "USF source material is unavailable"))
        { return false; }
        Mesh.SetMaterial(Get_SpecimenSlot(), _SourceMID);
        _UseSource = true;
        ck::Trace("USF_SHADING_MODEL_SOURCE_SELECTED: Toon.ush");
        return true;
    }
}
