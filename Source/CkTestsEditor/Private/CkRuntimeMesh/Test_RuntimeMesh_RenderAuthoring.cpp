#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace ck_test_runtime_mesh_render_authoring
{
    constexpr TCHAR PackageRoot[] = TEXT("/CkTests/CkRuntimeMesh/Render/");
    constexpr TCHAR MarkerKey[] = TEXT("CkRuntimeMeshRenderFixture");
    constexpr TCHAR MarkerValue[] = TEXT("RenderFixture-v1");
    constexpr TCHAR MeshName[] = TEXT("SM_Checker_CPU");
    constexpr TCHAR MaterialNames[3][32] = {
        TEXT("M_Checker_Blue"), TEXT("M_Checker_Red"), TEXT("M_Checker_Cap")};
    constexpr int32 Faces[12][3] = {
        {0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
        {0, 5, 1}, {0, 4, 5}, {3, 6, 7}, {3, 2, 6},
        {0, 7, 4}, {0, 3, 7}, {1, 6, 2}, {1, 5, 6}};
    const FVector3f Corners[8] = {
        {0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0},
        {0, 0, 10}, {10, 0, 10}, {10, 10, 10}, {0, 10, 10}};
    constexpr TCHAR CheckerColors[3][2][40] = {
        {TEXT("float3(0.015, 0.05, 0.42)"), TEXT("float3(0.08, 0.28, 0.95)")},
        {TEXT("float3(0.40, 0.018, 0.025)"), TEXT("float3(0.96, 0.09, 0.10)")},
        {TEXT("float3(0.015, 0.36, 0.045)"), TEXT("float3(0.08, 0.92, 0.12)")}};

    auto CheckerCode(int32 InIndex) -> FString
    {
        return FString::Printf(
            TEXT("float c = fmod(floor(UV.x * 4.0) + floor(UV.y * 4.0), 2.0); return lerp(%s, %s, c);"),
            CheckerColors[InIndex][0], CheckerColors[InIndex][1]);
    }

    auto PackagePath(const TCHAR* InName) -> FString
    { return FString::Printf(TEXT("%s%s"), PackageRoot, InName); }

    auto SaveAsset(UObject& InAsset) -> bool
    {
        auto* Package = InAsset.GetOutermost();
        const auto Filename = FPackageName::LongPackageNameToFilename(
            Package->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        auto Args = FSavePackageArgs{};
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        Args.Error = GLog;
        return UPackage::SavePackage(Package, &InAsset, *Filename, Args);
    }

    auto IsMarked(const UObject& InAsset) -> bool
    {
        auto* Package = InAsset.GetOutermost();
        return Package != nullptr && Package->GetMetaData().GetValue(&InAsset, MarkerKey).Equals(MarkerValue);
    }

    auto IsMaterialOurs(const UMaterial& InMaterial, int32 InIndex) -> bool
    {
        if (NOT IsMarked(InMaterial) || InMaterial.MaterialDomain != MD_Surface
            || InMaterial.BlendMode != BLEND_Opaque
            || NOT InMaterial.GetShadingModels().HasShadingModel(MSM_DefaultLit)
            || InMaterial.TwoSided
            || NOT InMaterial.GetName().Equals(MaterialNames[InIndex]))
        { return false; }
        const auto* Custom = static_cast<const UMaterialExpressionCustom*>(nullptr);
        const auto* Roughness = static_cast<const UMaterialExpressionConstant*>(nullptr);
        const auto* UV = static_cast<const UMaterialExpressionTextureCoordinate*>(nullptr);
        for (const auto& Expression : InMaterial.GetExpressionCollection().Expressions)
        {
            if (const auto* Found = Cast<UMaterialExpressionCustom>(Expression.Get()))
            { Custom = Found; }

            if (const auto* Found = Cast<UMaterialExpressionConstant>(Expression.Get()))
            { Roughness = Found; }

            if (const auto* Found = Cast<UMaterialExpressionTextureCoordinate>(Expression.Get()))
            { UV = Found; }
        }
        return Custom != nullptr && Roughness != nullptr && UV != nullptr
            && Custom->Code == CheckerCode(InIndex)
            && Custom->Inputs.Num() == 1 && Custom->Inputs[0].InputName == TEXT("UV")
            && Custom->Inputs[0].Input.Expression == UV
            && FMath::IsNearlyEqual(Roughness->R, 0.7f)
            && InMaterial.GetEditorOnlyData() != nullptr
            && InMaterial.GetEditorOnlyData()->BaseColor.Expression == Custom
            && InMaterial.GetEditorOnlyData()->Roughness.Expression == Roughness;
    }

    auto IsMeshOurs(const UStaticMesh& InMesh) -> bool
    {
        const auto* RenderData = InMesh.GetRenderData();
        if (NOT IsMarked(InMesh) || NOT InMesh.bAllowCPUAccess || NOT InMesh.NeverStream
            || InMesh.GetNaniteSettings().bEnabled || InMesh.GetStaticMaterials().Num() != 2
            || RenderData == nullptr || RenderData->LODResources.Num() != 1
            || RenderData->LODResources[0].GetNumTriangles() != 12
            || RenderData->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() != 1)
        { return false; }
        for (auto Index = 0; Index < 2; ++Index)
        {
            const auto* Material = InMesh.GetStaticMaterials()[Index].MaterialInterface.Get();
            if (Material == nullptr || Material->GetPathName() != FString::Printf(
                TEXT("%s.%s"), *PackagePath(MaterialNames[Index]), MaterialNames[Index]))
            { return false; }
        }
        return true;
    }

    auto MakeDescription() -> FMeshDescription
    {
        auto Description = FMeshDescription{};
        auto Attributes = FStaticMeshAttributes{Description};
        Attributes.Register();
        Attributes.GetVertexInstanceUVs().SetNumChannels(1);
        Description.SetNumUVChannels(1);
        auto Positions = Attributes.GetVertexPositions();
        auto Normals = Attributes.GetVertexInstanceNormals();
        auto Tangents = Attributes.GetVertexInstanceTangents();
        auto Signs = Attributes.GetVertexInstanceBinormalSigns();
        auto Colors = Attributes.GetVertexInstanceColors();
        auto UVs = Attributes.GetVertexInstanceUVs();
        auto SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
        const FPolygonGroupID Groups[2] = {Description.CreatePolygonGroup(), Description.CreatePolygonGroup()};
        SlotNames[Groups[0]] = TEXT("CheckerBlue");
        SlotNames[Groups[1]] = TEXT("CheckerRed");
        FVertexID Vertices[8];
        for (int32 Index = 0; Index < 8; ++Index)
        {
            Vertices[Index] = Description.CreateVertex();
            Positions[Vertices[Index]] = Corners[Index];
        }
        for (int32 Triangle = 0; Triangle < 12; ++Triangle)
        {
            const auto& Face = Faces[Triangle];
            const int32 Axis = Triangle / 2;
            const auto Normal = FVector3f::CrossProduct(
                Corners[Face[2]] - Corners[Face[0]], Corners[Face[1]] - Corners[Face[0]]).GetSafeNormal();
            const auto Tangent = (Corners[Face[1]] - Corners[Face[0]]).GetSafeNormal();
            auto Instances = TArray<FVertexInstanceID>{};
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const auto Position = Corners[Face[Corner]];
                const auto Instance = Description.CreateVertexInstance(Vertices[Face[Corner]]);
                Normals[Instance] = Normal;
                Tangents[Instance] = Tangent;
                Signs[Instance] = 1.0f;
                Colors[Instance] = FVector4f{1, 1, 1, 1};
                const auto UV = Axis < 2 ? FVector2f{Position.X, Position.Y}
                    : Axis < 4 ? FVector2f{Position.X, Position.Z}
                    : FVector2f{Position.Y, Position.Z};
                UVs.Set(Instance, 0, UV / 10.0f);
                Instances.Add(Instance);
            }
            Description.CreatePolygon(Groups[Axis % 2], Instances);
        }
        return Description;
    }

    auto WriteMaterial(int32 InIndex) -> UMaterial*
    {
        auto* Package = CreatePackage(*PackagePath(MaterialNames[InIndex]));
        auto* Material = Package != nullptr
            ? NewObject<UMaterial>(Package, MaterialNames[InIndex], RF_Public | RF_Standalone) : nullptr;
        if (ck::Is_NOT_Valid(Material))
        { return nullptr; }

        Material->MaterialDomain = MD_Surface;
        Material->BlendMode = BLEND_Opaque;
        Material->SetShadingModel(MSM_DefaultLit);
        Material->TwoSided = false;
        auto* Checker = Cast<UMaterialExpressionCustom>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionCustom::StaticClass(), -300, 0));
        auto* TexCoord = UMaterialEditingLibrary::CreateMaterialExpression(
            Material, UMaterialExpressionTextureCoordinate::StaticClass(), -600, 0);
        auto* Roughness = Cast<UMaterialExpressionConstant>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -300, 240));
        if (ck::Is_NOT_Valid(Checker) || ck::Is_NOT_Valid(TexCoord) || ck::Is_NOT_Valid(Roughness))
        { return nullptr; }

        Checker->OutputType = CMOT_Float3;

        // UMaterialExpressionCustom's constructor adds one unnamed input; the fixture contract
        // (IsMaterialOurs) is exactly one input named UV.
        auto UVInput = FCustomInput{};
        UVInput.InputName = TEXT("UV");
        Checker->Inputs.Reset();
        Checker->Inputs.Add(UVInput);
        Checker->Code = CheckerCode(InIndex);
        Roughness->R = 0.7f;
        if (NOT UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord, TEXT(""), Checker, TEXT("UV"))
            || NOT UMaterialEditingLibrary::ConnectMaterialProperty(Checker, TEXT(""), MP_BaseColor)
            || NOT UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness))
        { return nullptr; }
        UMaterialEditingLibrary::RecompileMaterial(Material);
        Package->GetMetaData().SetValue(Material, MarkerKey, MarkerValue);
        Material->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(Material);
        return SaveAsset(*Material) ? Material : nullptr;
    }

    auto WriteMesh(UMaterial* InBlue, UMaterial* InRed) -> bool
    {
        auto* Package = CreatePackage(*PackagePath(MeshName));
        auto* Mesh = Package != nullptr
            ? NewObject<UStaticMesh>(Package, MeshName, RF_Public | RF_Standalone) : nullptr;
        if (ck::Is_NOT_Valid(Mesh) || ck::Is_NOT_Valid(InBlue) || ck::Is_NOT_Valid(InRed))
        { return false; }

        Mesh->bAllowCPUAccess = true;
        Mesh->NeverStream = true;
        Mesh->GetNaniteSettings().bEnabled = false;
        Mesh->SetStaticMaterials({FStaticMaterial{InBlue, TEXT("CheckerBlue")},
            FStaticMaterial{InRed, TEXT("CheckerRed")}});
        Mesh->SetNumSourceModels(1);
        auto& Settings = Mesh->GetSourceModel(0).BuildSettings;
        Settings.bGenerateLightmapUVs = false;
        Settings.bRecomputeNormals = false;
        Settings.bRecomputeTangents = false;
        auto Description = MakeDescription();
        auto Params = UStaticMesh::FBuildMeshDescriptionsParams{};
        Params.bCommitMeshDescription = true;
        Params.bFastBuild = false;
        Params.bAllowCpuAccess = true;
        Params.bBuildSimpleCollision = false;
        if (NOT Mesh->BuildFromMeshDescriptions({&Description}, Params))
        { return false; }

        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
        Package->GetMetaData().SetValue(Mesh, MarkerKey, MarkerValue);
        if (NOT IsMeshOurs(*Mesh))
        { return false; }

        Mesh->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(Mesh);
        return SaveAsset(*Mesh);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RuntimeMesh_AuthorRenderFixtures,
    "RuntimeMeshCooked.AuthorRenderFixtures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkTest_RuntimeMesh_AuthorRenderFixtures::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_render_authoring;
    UMaterial* Materials[3] = {};
    bool NeedsMaterial[3] = {};
    for (auto Index = 0; Index < 3; ++Index)
    {
        const auto Path = PackagePath(MaterialNames[Index]);
        if (FPackageName::DoesPackageExist(Path))
        {
            Materials[Index] = LoadObject<UMaterial>(nullptr,
                *FString::Printf(TEXT("%s.%s"), *Path, MaterialNames[Index]));
            if (NOT TestTrue(TEXT("existing material is owned and valid"),
                Materials[Index] != nullptr && IsMaterialOurs(*Materials[Index], Index)))
            { return false; }
        }
        else
        { NeedsMaterial[Index] = true; }
    }
    const auto MeshPath = PackagePath(MeshName);
    const auto NeedsMesh = NOT FPackageName::DoesPackageExist(MeshPath);
    if (NOT NeedsMesh)
    {
        auto* Existing = LoadObject<UStaticMesh>(nullptr,
            *FString::Printf(TEXT("%s.%s"), *MeshPath, MeshName));
        if (NOT TestTrue(TEXT("existing mesh is owned and valid"),
            Existing != nullptr && IsMeshOurs(*Existing)))
        { return false; }
    }
    for (auto Index = 0; Index < 3; ++Index)
    {
        if (NeedsMaterial[Index])
        {
            Materials[Index] = WriteMaterial(Index);
            if (NOT TestNotNull(TEXT("checker material saved"), Materials[Index]))
            { return false; }
        }
    }
    if (NeedsMesh && NOT TestTrue(TEXT("checker mesh saved"), WriteMesh(Materials[0], Materials[1])))
    { return false; }
    return true;
}

#endif
