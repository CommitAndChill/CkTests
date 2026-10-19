#include "CkUsfGym_ShadingModels.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EditorValidatorSubsystem.h"
#include "HAL/FileManager.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionSphereMask.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_UsfGym_ShadingModelBatch,
    "CkTests.UnitTests.CkUsf.GymShadingModelsCompileAllLegacyVariants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

auto FCkTest_UsfGym_ShadingModelBatch::RunTest(const FString& Parameters) -> bool
{
    auto Errors = TArray<FString>{};
    auto Batch = ck::usf_gym::FBatch{};
    const auto Environment = ck::usf_gym::Get_Environment();
    if (Environment.IsSubstrate)
    {
        // The legacy comparison gym intentionally excludes Substrate, including Blendable GBuffer.
        // Exercise that production boundary on this host instead of claiming legacy shader coverage.
        const auto PackagePath = FPackageName::ObjectPathToPackageName(ck::usf_gym::CatalogPath);
        const auto PreviousPackage = TWeakObjectPtr<UPackage>{FindPackage(nullptr, *PackagePath)};
        const auto PackageExisted = FPackageName::DoesPackageExist(PackagePath);
        TestFalse(TEXT("Legacy gym rejects the active Substrate environment before staging"),
            ck::usf_gym::Build_TransientBatch(Environment, Batch, Errors));
        TestEqual(TEXT("Active host rejection has only the expected environment violation"), Errors.Num(), 1);
        TestTrue(TEXT("Legacy gym reports that Substrate must be disabled"), Errors.Contains(
            TEXT("Shading-model comparison requires desktop deferred rendering with Substrate disabled.")));
        TestTrue(TEXT("Active environment rejection creates no candidate rows or materials"),
            Batch.Models.IsEmpty() && Batch.Materials.IsEmpty());
        TestFalse(TEXT("Active environment rejection allocates no scattering profile"), Batch.Profile.IsValid());
        TestTrue(TEXT("Active environment rejection preserves the catalog package"),
            PreviousPackage.Get() == FindPackage(nullptr, *PackagePath));
        TestEqual(TEXT("Active environment rejection preserves catalog disk occupancy"),
            FPackageName::DoesPackageExist(PackagePath), PackageExisted);
        AddInfo(TEXT("Active Substrate host exercised atomic legacy-gym rejection; legacy shader compilation requires a Substrate-disabled host."));
        return true;
    }
    if (ck::usf_gym::Build_TransientBatch(Environment, Batch, Errors) == false)
    {
        for (const auto& Error : Errors)
        { AddError(Error); }
        return false;
    }
    TestEqual(TEXT("Full legacy roster plus native Toon"), Batch.Models.Num(), 13);
    TestEqual(TEXT("Every catalog row has a strongly owned candidate"), Batch.Materials.Num(), Batch.Models.Num());
    TestTrue(TEXT("Profile has a lifetime owner while staged"), Batch.Profile.IsValid());
    for (auto Index = 0; Index < Batch.Models.Num(); ++Index)
    {
        const auto& Entry = Batch.Models[Index];
        TestTrue(TEXT("Catalog material is the owned candidate"), Entry.Material.Get() == Batch.Materials[Index].Get());
        TestTrue(TEXT("Candidates are transient and unpublished"), Entry.Material->GetOutermost() == GetTransientPackage());
        TestTrue(TEXT("Catalog enum matches actual sole material model"), Batch.Materials[Index]->GetShadingModels().CountShadingModels() == 1
            && Batch.Materials[Index]->GetShadingModels().HasShadingModel(Entry.ShadingModel));
    }
    TestEqual(TEXT("Toon is last for stable gym controls"), Batch.Models.Last().DisplayName, FString{TEXT("CkUsf Toon")});
    TestTrue(TEXT("Model-specific pins and settings validate"), ck::usf_gym::Validate_Models(Batch.Models, Errors));
    TestTrue(TEXT("Every candidate has complete real-RHI shaders without errors"), ck::usf_gym::Validate_ShaderMaps(Batch.Models, Errors));
    for (const auto& Error : Errors)
    { AddError(Error); }

    auto& Water = *Batch.Materials[10];
    Water.BlendMode = BLEND_Translucent;
    auto InvalidErrors = TArray<FString>{};
    TestFalse(TEXT("Water rejects an incompatible blend mode"), ck::usf_gym::Validate_Models(Batch.Models, InvalidErrors));
    Water.BlendMode = BLEND_Opaque;

    auto& Thin = *Batch.Materials[11];
    Thin.TranslucencyLightingMode = TLM_VolumetricNonDirectional;
    InvalidErrors.Reset();
    TestFalse(TEXT("Thin transmission rejects missing per-pixel surface lighting"), ck::usf_gym::Validate_Models(Batch.Models, InvalidErrors));
    Thin.TranslucencyLightingMode = TLM_SurfacePerPixelLighting;

    auto& Eye = *Batch.Materials[9];
    TestNotNull(TEXT("Eye uses a procedural iris mask"), Cast<UMaterialExpressionSphereMask>(Eye.GetEditorOnlyData()->ClearCoat.Expression));
    Eye.SubsurfaceProfile = nullptr;
    InvalidErrors.Reset();
    TestFalse(TEXT("Eye rejects a missing scattering profile"), ck::usf_gym::Validate_Models(Batch.Models, InvalidErrors));
    Eye.SubsurfaceProfile = Batch.Profile.Get();

    auto& ProfileMaterial = *Batch.Materials[5];
    const auto Opacity = TObjectPtr<UMaterialExpressionScalarParameter>{Cast<UMaterialExpressionScalarParameter>(ProfileMaterial.GetEditorOnlyData()->Opacity.Expression)};
    if (TestNotNull(TEXT("Profile opacity is a tunable scalar"), Opacity.Get()))
    {
        Opacity->DefaultValue = 0.0f;
        InvalidErrors.Reset();
        TestFalse(TEXT("Profile rejects zero opacity that falls back to Default Lit"), ck::usf_gym::Validate_Models(Batch.Models, InvalidErrors));
        Opacity->DefaultValue = 1.0f;
    }

    auto Incomplete = Batch.Models;
    Incomplete.Pop();
    InvalidErrors.Reset();
    TestFalse(TEXT("A successful subset cannot become a valid catalog"), ck::usf_gym::Validate_Models(Incomplete, InvalidErrors));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_UsfGym_ShadingModelRejection,
    "CkTests.UnitTests.CkUsf.GymShadingModelsRejectUnsupportedEnvironmentAtomically",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_UsfGym_ShadingModelRejection::RunTest(const FString& Parameters) -> bool
{
    const auto CatalogPackagePath = FPackageName::ObjectPathToPackageName(ck::usf_gym::CatalogPath);
    const auto PreviousPackage = TWeakObjectPtr<UPackage>{FindPackage(nullptr, *CatalogPackagePath)};
    for (auto Case = 0; Case < 7; ++Case)
    {
        auto Environment = ck::usf_gym::FEnvironment{};
        if (Case == 0) { Environment.IsPIE = true; }
        if (Case == 1) { Environment.CanRender = false; }
        if (Case == 2) { Environment.IsMobile = true; }
        if (Case == 3) { Environment.IsForward = true; }
        if (Case == 4) { Environment.IsSubstrate = true; }
        if (Case == 5) { Environment.HasNativeToon = false; }
        if (Case == 6) { Environment.IsPlatformReady = false; }
        auto Errors = TArray<FString>{};
        auto Batch = ck::usf_gym::FBatch{};
        TestFalse(TEXT("Unsupported environment rejects before staging"), ck::usf_gym::Build_TransientBatch(Environment, Batch, Errors));
        TestFalse(TEXT("Rejection has an actionable diagnostic"), Errors.IsEmpty());
        TestTrue(TEXT("Rejection publishes no rows or material candidates"), Batch.Models.IsEmpty() && Batch.Materials.IsEmpty());
        TestFalse(TEXT("Rejection allocates no profile"), Batch.Profile.IsValid());
        TestTrue(TEXT("Rejection leaves the catalog package unchanged"), PreviousPackage.Get() == FindPackage(nullptr, *CatalogPackagePath));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_UsfGym_ShadingModelReflection,
    "CkTests.UnitTests.CkUsf.GymShadingModelCatalogExposesReadOnlyScriptContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_UsfGym_ShadingModelReflection::RunTest(const FString& Parameters) -> bool
{
    const auto CheckProperty = [this](const UStruct& InOwner, FName InName)
    {
        if (TestNotNull(*FString::Printf(TEXT("Reflected property %s"), *InName.ToString()), FindFProperty<FProperty>(&InOwner, InName)) == false)
        { return; }
        const auto& Property = *FindFProperty<FProperty>(&InOwner, InName);
        TestTrue(TEXT("Script-visible property is BlueprintReadOnly"), Property.HasAllPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintReadOnly));
    };
    CheckProperty(*UCk_UsfGym_ShadingModelCatalog::StaticClass(), TEXT("Models"));
    for (const auto Name : {FName{TEXT("DisplayName")}, FName{TEXT("Description")}, FName{TEXT("Material")}, FName{TEXT("ShadingModel")}})
    { CheckProperty(*FCk_UsfGym_ShadingModelEntry::StaticStruct(), Name); }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_UsfGym_ShadingModelPublication,
    "CkTests.UnitTests.CkUsf.GymShadingModelCatalogPublishesAfterMissingLoad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_UsfGym_ShadingModelPublication::RunTest(const FString& Parameters) -> bool
{
    const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const auto MountRoot = FString{TEXT("/CkUsfGymSaveTest_")} + Id + TEXT("/");
    const auto Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CkUsfGymPublication") / Id) + TEXT("/");
    if (TestTrue(TEXT("Create owned Saved fixture directory"), IFileManager::Get().MakeDirectory(*Directory, true)) == false)
    { return false; }
    FPackageName::RegisterMountPoint(MountRoot, Directory);
    ON_SCOPE_EXIT
    { FPackageName::UnRegisterMountPoint(MountRoot, Directory); };
    // These sentinel assets use a temporary Saved mount and are not production validation targets.
    const FScopedDisableValidateOnSave DisableFixtureValidation;
    AddInfo(FString::Printf(TEXT("Publication fixture files retained at %s"), *Directory));

    const auto MakeCatalog = []()
    {
        auto Catalog = TStrongObjectPtr<UCk_UsfGym_ShadingModelCatalog>{NewObject<UCk_UsfGym_ShadingModelCatalog>(GetTransientPackage(), NAME_None, RF_Transient)};
        auto Row = FCk_UsfGym_ShadingModelEntry{};
        Row.DisplayName = TEXT("SavedCatalogSentinel");
        Row.Description = TEXT("Publication serialization fixture");
        Row.Material = UMaterial::GetDefaultMaterial(MD_Surface);
        Catalog->Models.Add(MoveTemp(Row));
        return Catalog;
    };
    const auto Filename = [](const FString& InPackagePath)
    { return FPackageName::LongPackageNameToFilename(InPackagePath, FPackageName::GetAssetPackageExtension()); };

    const auto MissingPackagePath = MountRoot + TEXT("MissingCatalog");
    const auto MissingObjectPath = MissingPackagePath + TEXT(".MissingCatalog");
    TestNull(TEXT("Missing catalog is absent before the lookup"), FindPackage(nullptr, *MissingPackagePath));
    TestNull(TEXT("Quiet lookup of a missing catalog returns no asset"), LoadObject<UObject>(nullptr, *MissingObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet));
    const auto EmptyShell = TStrongObjectPtr<UPackage>{FindPackage(nullptr, *MissingPackagePath)};
    if (TestNotNull(TEXT("Missing LoadObject left its empty package shell"), EmptyShell.Get()) == false)
    { return false; }
    TestFalse(TEXT("Missing lookup wrote no package file"), FPackageName::DoesPackageExist(MissingPackagePath));
    auto Catalog = MakeCatalog();
    if (TestTrue(TEXT("Production publisher reuses the empty shell and saves the first catalog"), ck::usf_gym::Publish_NewAsset(*Catalog, MissingPackagePath)) == false)
    { return false; }
    TestTrue(TEXT("Publisher reused the exact package shell"), Catalog->GetOutermost() == EmptyShell.Get());
    TestEqual(TEXT("Published catalog has its stable object path"), Catalog->GetPathName(), MissingObjectPath);
    TestTrue(TEXT("Saved catalog is public and standalone"), Catalog->HasAllFlags(RF_Public | RF_Standalone));
    TestFalse(TEXT("Saved catalog is no longer transient"), Catalog->HasAnyFlags(RF_Transient));
    TestTrue(TEXT("Actual SavePackage wrote a nonempty catalog file"), IFileManager::Get().FileSize(*Filename(MissingPackagePath)) > 0);
    const auto OriginalHash = FMD5Hash::HashFile(*Filename(MissingPackagePath));
    TestTrue(TEXT("Saved catalog has a readable file hash"), OriginalHash.IsValid());
    const auto PreviousModels = Catalog->Models;

    auto Replacement = MakeCatalog();
    Replacement->Models[0].DisplayName = TEXT("ReplacementMustNotPublish");
    const auto ReplacementOuter = TWeakObjectPtr<UObject>{Replacement->GetOuter()};
    const auto ReplacementFlags = Replacement->GetFlags();
    TestFalse(TEXT("Publisher rejects an existing saved catalog"), ck::usf_gym::Publish_NewAsset(*Replacement, MissingPackagePath));
    TestTrue(TEXT("Disk collision preserves candidate outer and flags"), Replacement->GetOuter() == ReplacementOuter.Get() && Replacement->GetFlags() == ReplacementFlags);
    TestTrue(TEXT("Disk collision preserves the previous catalog file"), OriginalHash == FMD5Hash::HashFile(*Filename(MissingPackagePath)));
    TestEqual(TEXT("Disk collision preserves previous catalog rows"), Catalog->Models.Num(), PreviousModels.Num());
    TestEqual(TEXT("Disk collision preserves previous display name"), Catalog->Models[0].DisplayName, PreviousModels[0].DisplayName);
    TestTrue(TEXT("Disk collision preserves previous material reference"), Catalog->Models[0].Material == PreviousModels[0].Material);

    const auto FreshPath = MountRoot + TEXT("FreshCatalog");
    TestNull(TEXT("Fresh publication starts without any package"), FindPackage(nullptr, *FreshPath));
    auto Fresh = MakeCatalog();
    TestTrue(TEXT("Production publisher also creates a new package"), ck::usf_gym::Publish_NewAsset(*Fresh, FreshPath));
    TestTrue(TEXT("Fresh publication saves its package file"), IFileManager::Get().FileSize(*Filename(FreshPath)) > 0);

    const auto DiskOnlyPath = MountRoot + TEXT("DiskOnly");
    const auto DiskOnlyFilename = Filename(DiskOnlyPath);
    if (TestTrue(TEXT("Create an owned disk-only collision fixture"), IFileManager::Get().Copy(*DiskOnlyFilename, *Filename(MissingPackagePath)) == COPY_OK) == false)
    { return false; }
    const auto DiskOnlyHash = FMD5Hash::HashFile(*DiskOnlyFilename);
    TestNull(TEXT("Disk-only collision has no in-memory package"), FindPackage(nullptr, *DiskOnlyPath));
    TestFalse(TEXT("Publisher rejects disk-only occupancy"), ck::usf_gym::Publish_NewAsset(*Replacement, DiskOnlyPath));
    TestTrue(TEXT("Disk-only collision preserves file bytes"), DiskOnlyHash.IsValid() && DiskOnlyHash == FMD5Hash::HashFile(*DiskOnlyFilename));
    TestTrue(TEXT("Disk-only collision preserves candidate outer and flags"), Replacement->GetOuter() == ReplacementOuter.Get() && Replacement->GetFlags() == ReplacementFlags);

    const auto OccupiedPath = MountRoot + TEXT("Occupied");
    const auto OccupiedPackage = TStrongObjectPtr<UPackage>{CreatePackage(*OccupiedPath)};
    const auto Occupant = TStrongObjectPtr<UCk_UsfGym_ShadingModelCatalog>{NewObject<UCk_UsfGym_ShadingModelCatalog>(OccupiedPackage.Get(), TEXT("ExistingOccupant"), RF_Transient)};
    Occupant->Models = PreviousModels;
    OccupiedPackage->SetDirtyFlag(false);
    TestFalse(TEXT("Publisher rejects even transient occupants in an unsaved package"), ck::usf_gym::Publish_NewAsset(*Replacement, OccupiedPath));
    TestTrue(TEXT("In-memory collision preserves candidate outer and flags"), Replacement->GetOuter() == ReplacementOuter.Get() && Replacement->GetFlags() == ReplacementFlags);
    TestEqual(TEXT("In-memory collision preserves previous catalog rows"), Occupant->Models.Num(), PreviousModels.Num());
    TestEqual(TEXT("In-memory collision preserves previous catalog value"), Occupant->Models[0].DisplayName, PreviousModels[0].DisplayName);
    TestFalse(TEXT("In-memory collision writes no package"), FPackageName::DoesPackageExist(OccupiedPath));

    const auto DirtyPath = MountRoot + TEXT("DirtyEmpty");
    const auto DirtyPackage = TStrongObjectPtr<UPackage>{CreatePackage(*DirtyPath)};
    DirtyPackage->SetDirtyFlag(true);
    TestFalse(TEXT("Publisher rejects a dirty empty package"), ck::usf_gym::Publish_NewAsset(*Replacement, DirtyPath));
    TestTrue(TEXT("Dirty rejection preserves candidate outer and flags"), Replacement->GetOuter() == ReplacementOuter.Get() && Replacement->GetFlags() == ReplacementFlags);
    TestTrue(TEXT("Dirty rejection preserves package dirt"), DirtyPackage->IsDirty());
    TestFalse(TEXT("Dirty rejection writes no package"), FPackageName::DoesPackageExist(DirtyPath));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_UsfGym_ShadingModelMaterialPublicationRoundTrip,
    "CkTests.UnitTests.CkUsf.GymShadingModelMaterialPublicationRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_UsfGym_ShadingModelMaterialPublicationRoundTrip::RunTest(const FString& Parameters) -> bool
{
    const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const auto MountRoot = FString{TEXT("/CkUsfGymMaterialSaveTest_")} + Id + TEXT("/");
    const auto Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CkUsfGymMaterialPublication") / Id) + TEXT("/");
    if (TestTrue(TEXT("Create owned material publication directory"), IFileManager::Get().MakeDirectory(*Directory, true)) == false)
    { return false; }
    FPackageName::RegisterMountPoint(MountRoot, Directory);
    ON_SCOPE_EXIT
    { FPackageName::UnRegisterMountPoint(MountRoot, Directory); };
    const FScopedDisableValidateOnSave DisableFixtureValidation;
    AddInfo(FString::Printf(TEXT("Material publication round-trip fixture retained at %s"), *Directory));
    const auto PackagePath = MountRoot + TEXT("RoundTripMaterial");
    const auto ObjectPath = PackagePath + TEXT(".RoundTripMaterial");
    auto Material = TStrongObjectPtr<UMaterial>{NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient)};
    if (TestNotNull(TEXT("Transient material creates editor-only graph data"), Material->GetEditorOnlyData()) == false)
    { return false; }
    TestTrue(TEXT("Transient material propagates its transient flag to editor-only data"), Material->GetEditorOnlyData()->HasAnyFlags(RF_Transient));
    auto Scalar = TStrongObjectPtr<UMaterialExpressionScalarParameter>{NewObject<UMaterialExpressionScalarParameter>(
        Material.Get(), TEXT("RoundTripScalar"), RF_Transactional)};
    Scalar->Material = Material.Get();
    Scalar->UpdateMaterialExpressionGuid(true, false);
    Scalar->UpdateParameterGuid(true, false);
    Scalar->ParameterName = TEXT("RoundTripTint");
    Scalar->DefaultValue = 0.375f;
    Material->GetExpressionCollection().AddExpression(Scalar.Get());
    Material->AddExpressionParameter(Scalar.Get(), Material->EditorParameters);
    Material->GetEditorOnlyData()->BaseColor.Connect(0, Scalar.Get());
    Material->UpdateCachedExpressionData();
    const auto ScalarGuid = Scalar->MaterialExpressionGuid;
    if (TestTrue(TEXT("Production publisher saves the transient material"), ck::usf_gym::Publish_NewAsset(*Material, PackagePath)) == false)
    { return false; }
    // Stop before a load if the graph would be omitted; loading that broken material can crash PostLoad.
    if (TestFalse(TEXT("Published material editor-only data is serializable"), Material->GetEditorOnlyData()->HasAnyFlags(RF_Transient)) == false)
    { return false; }
    const auto PackageWeak = TWeakObjectPtr<UPackage>{Material->GetOutermost()};
    const auto OriginalMaterial = TWeakObjectPtr<UMaterial>{Material.Get()};
    const auto OriginalScalar = TWeakObjectPtr<UMaterialExpressionScalarParameter>{Scalar.Get()};
    TestFalse(TEXT("Published fixture package is clean before targeted unload"), PackageWeak->IsDirty());
    Scalar.Reset();
    Material.Reset();
    auto UnloadError = FText{};
    if (TestTrue(TEXT("Unload only the owned material fixture package"), UPackageTools::UnloadPackages({PackageWeak.Get()}, UnloadError)) == false)
    { AddError(UnloadError.ToString()); return false; }
    TestFalse(TEXT("Targeted unload releases the original material instance"), OriginalMaterial.IsValid());
    TestFalse(TEXT("Targeted unload releases the original graph expression"), OriginalScalar.IsValid());
    if (TestNull(TEXT("Material is absent from memory before the disk round trip"), FindObject<UMaterial>(nullptr, *ObjectPath)) == false)
    { return false; }
    const auto Loaded = TStrongObjectPtr<UMaterial>{LoadObject<UMaterial>(nullptr, *ObjectPath)};
    if (TestNotNull(TEXT("Published material loads freshly from disk"), Loaded.Get()) == false)
    { return false; }
    if (TestNotNull(TEXT("Fresh material retains its editor-only graph data"), Loaded->GetEditorOnlyData()) == false)
    { return false; }
    TestEqual(TEXT("Fresh material uses its published name"), Loaded->GetFName(), FName{TEXT("RoundTripMaterial")});
    TestEqual(TEXT("Fresh material retains its graph expression"), Loaded->GetExpressions().Num(), 1);
    const auto LoadedScalar = TObjectPtr<UMaterialExpressionScalarParameter>{Cast<UMaterialExpressionScalarParameter>(
        Loaded->GetEditorOnlyData()->BaseColor.Expression)};
    if (TestNotNull(TEXT("BaseColor retains its scalar expression connection"), LoadedScalar.Get()) == false)
    { return false; }
    TestTrue(TEXT("Reloaded scalar belongs to the reloaded material"), LoadedScalar->GetOuter() == Loaded.Get() && LoadedScalar->Material == Loaded.Get());
    TestTrue(TEXT("Reloaded graph collection contains the connected scalar"), Loaded->GetExpressions().Contains(LoadedScalar));
    TestEqual(TEXT("Reloaded scalar preserves its object name"), LoadedScalar->GetFName(), FName{TEXT("RoundTripScalar")});
    TestEqual(TEXT("Reloaded scalar preserves its parameter name"), LoadedScalar->ParameterName, FName{TEXT("RoundTripTint")});
    TestEqual(TEXT("Reloaded scalar preserves its parameter default"), LoadedScalar->DefaultValue, 0.375f);
    TestEqual(TEXT("Reloaded scalar preserves its expression GUID"), LoadedScalar->MaterialExpressionGuid, ScalarGuid);
    TestEqual(TEXT("BaseColor keeps the scalar's primary output index"), Loaded->GetEditorOnlyData()->BaseColor.OutputIndex, 0);
    return true;
}

#endif
