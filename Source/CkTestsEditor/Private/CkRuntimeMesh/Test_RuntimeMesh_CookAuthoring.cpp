#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace ck_test_runtime_mesh_cook_authoring
{
    constexpr TCHAR PackageRoot[] = TEXT("/CkTests/CkRuntimeMesh/Cooked/");
    constexpr TCHAR MarkerKey[] = TEXT("CkRuntimeMeshCookFixture");
    constexpr TCHAR MarkerValue[] = TEXT("Gate1B-v1");
    constexpr TCHAR Names[2][32] = {TEXT("SM_Import_CPU"), TEXT("SM_Import_NoCPU")};

    constexpr int32 Faces[12][3] = {
        {0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
        {0, 5, 1}, {0, 4, 5}, {3, 6, 7}, {3, 2, 6},
        {0, 7, 4}, {0, 3, 7}, {1, 6, 2}, {1, 5, 6}};
    const FVector3f Corners[8] = {
        {0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0},
        {0, 0, 10}, {10, 0, 10}, {10, 10, 10}, {0, 10, 10}};

    auto MakeDescription() -> FMeshDescription
    {
        auto Description = FMeshDescription{};
        auto Attributes = FStaticMeshAttributes{Description};
        Attributes.Register();
        Attributes.GetVertexInstanceUVs().SetNumChannels(2);
        Description.SetNumUVChannels(2);

        auto Positions = Attributes.GetVertexPositions();
        auto Normals = Attributes.GetVertexInstanceNormals();
        auto Tangents = Attributes.GetVertexInstanceTangents();
        auto BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
        auto Colors = Attributes.GetVertexInstanceColors();
        auto UVs = Attributes.GetVertexInstanceUVs();
        auto SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
        const FPolygonGroupID Groups[2] = {Description.CreatePolygonGroup(), Description.CreatePolygonGroup()};
        SlotNames[Groups[0]] = TEXT("CookFixture_A");
        SlotNames[Groups[1]] = TEXT("CookFixture_B");

        FVertexID VertexIDs[8];
        for (int32 Index = 0; Index < 8; ++Index)
        {
            VertexIDs[Index] = Description.CreateVertex();
            Positions[VertexIDs[Index]] = Corners[Index];
        }
        for (int32 Triangle = 0; Triangle < 12; ++Triangle)
        {
            const auto& Face = Faces[Triangle];
            const auto Normal = FVector3f::CrossProduct(
                Corners[Face[2]] - Corners[Face[0]], Corners[Face[1]] - Corners[Face[0]]).GetSafeNormal();
            const auto Tangent = (Corners[Face[1]] - Corners[Face[0]]).GetSafeNormal();
            auto Instances = TArray<FVertexInstanceID>{};
            Instances.Reserve(3);
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const int32 CornerID = Face[Corner];
                const auto Instance = Description.CreateVertexInstance(VertexIDs[CornerID]);
                Normals[Instance] = Normal;
                Tangents[Instance] = Tangent;
                BinormalSigns[Instance] = 1.0f;
                Colors[Instance] = FVector4f{static_cast<float>(30 + 20 * CornerID) / 255.0f,
                    80.0f / 255.0f, 160.0f / 255.0f, 1.0f};
                UVs.Set(Instance, 0, FVector2f{static_cast<float>(Triangle) / 16.0f,
                    static_cast<float>(Corner) / 4.0f});
                UVs.Set(Instance, 1, FVector2f{static_cast<float>(Triangle) / 32.0f,
                    static_cast<float>(Corner) / 8.0f});
                Instances.Add(Instance);
            }
            Description.CreatePolygon(Groups[Triangle < 6 ? 0 : 1], Instances);
        }
        return Description;
    }

    auto HasBuiltContract(const UStaticMesh& InMesh, bool InCpuAccess) -> bool
    {
        const auto* RenderData = InMesh.GetRenderData();
        const bool Valid = InMesh.bAllowCPUAccess == InCpuAccess
            && InMesh.GetStaticMaterials().Num() == 2
            && RenderData != nullptr && RenderData->LODResources.Num() == 1
            && RenderData->LODResources[0].GetNumTriangles() == 12
            && RenderData->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() == 2
            && RenderData->LODResources[0].VertexBuffers.ColorVertexBuffer.GetNumVertices() > 0;
        if (NOT Valid)
        {
            const auto* LOD = RenderData != nullptr && RenderData->LODResources.Num() > 0
                ? &RenderData->LODResources[0] : nullptr;
            UE_LOG(LogTemp, Warning,
                TEXT("RuntimeMesh cook fixture [%s] contract: cpu=%d expected=%d materials=%d lods=%d triangles=%d uv=%d colorVertices=%d"),
                *InMesh.GetName(), InMesh.bAllowCPUAccess, InCpuAccess, InMesh.GetStaticMaterials().Num(),
                RenderData != nullptr ? RenderData->LODResources.Num() : -1,
                LOD != nullptr ? LOD->GetNumTriangles() : -1,
                LOD != nullptr ? static_cast<int32>(LOD->VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()) : -1,
                LOD != nullptr ? static_cast<int32>(LOD->VertexBuffers.ColorVertexBuffer.GetNumVertices()) : -1);
        }
        return Valid;
    }

    auto IsOurs(const UStaticMesh& InMesh, bool InCpuAccess) -> bool
    {
        auto* Package = InMesh.GetOutermost();
        return Package != nullptr
            && Package->GetMetaData().GetValue(&InMesh, MarkerKey).Equals(MarkerValue)
            && HasBuiltContract(InMesh, InCpuAccess);
    }

    auto WriteFixture(const TCHAR* InName, bool InCpuAccess) -> bool
    {
        const auto PackagePath = FString::Printf(TEXT("%s%s"), PackageRoot, InName);
        auto* Package = CreatePackage(*PackagePath);
        if (Package == nullptr)
        { return false; }
        auto* Mesh = NewObject<UStaticMesh>(Package, InName, RF_Public | RF_Standalone);
        if (Mesh == nullptr)
        { return false; }
        Mesh->bAllowCPUAccess = InCpuAccess;
        Mesh->NeverStream = true;
        Mesh->GetNaniteSettings().bEnabled = false;
        Mesh->SetStaticMaterials({FStaticMaterial{nullptr, TEXT("CookFixture_A")},
            FStaticMaterial{nullptr, TEXT("CookFixture_B")}});
        Mesh->SetNumSourceModels(1);
        auto& BuildSettings = Mesh->GetSourceModel(0).BuildSettings;
        BuildSettings.bGenerateLightmapUVs = false;
        BuildSettings.bRecomputeNormals = false;
        BuildSettings.bRecomputeTangents = false;

        auto Description = MakeDescription();
        auto Params = UStaticMesh::FBuildMeshDescriptionsParams{};
        Params.bCommitMeshDescription = true;
        Params.bFastBuild = false;
        Params.bAllowCpuAccess = InCpuAccess;
        Params.bBuildSimpleCollision = false;
        if (NOT Mesh->BuildFromMeshDescriptions({&Description}, Params))
        { return false; }
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
        if (NOT HasBuiltContract(*Mesh, InCpuAccess))
        { return false; }
        Package->GetMetaData().SetValue(Mesh, MarkerKey, MarkerValue);
        Mesh->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(Mesh);
        auto SaveArgs = FSavePackageArgs{};
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        SaveArgs.Error = GLog;
        const auto FileName = FPackageName::LongPackageNameToFilename(
            PackagePath, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(FileName), true);
        return UPackage::SavePackage(Package, Mesh, *FileName, SaveArgs);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RuntimeMesh_CookAuthoring_WriteFixtures,
    "RuntimeMeshCooked.AuthorFixtures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkTest_RuntimeMesh_CookAuthoring_WriteFixtures::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_cook_authoring;
    bool NeedsWrite[2] = {};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const auto PackagePath = FString::Printf(TEXT("%s%s"), PackageRoot, Names[Index]);
        if (FPackageName::DoesPackageExist(PackagePath))
        {
            const auto ObjectPath = FString::Printf(TEXT("%s.%s"), *PackagePath, Names[Index]);
            auto* Existing = LoadObject<UStaticMesh>(nullptr, *ObjectPath);
            if (NOT TestTrue(TEXT("existing package is our verified fixture"),
                Existing != nullptr && IsOurs(*Existing, Index == 0)))
            { return false; }
        }
        else
        { NeedsWrite[Index] = true; }
    }
    for (int32 Index = 0; Index < 2; ++Index)
    {
        if (NeedsWrite[Index] && NOT TestTrue(TEXT("fixture package saved"),
            WriteFixture(Names[Index], Index == 0)))
        { return false; }
    }
    return true;
}

#endif
