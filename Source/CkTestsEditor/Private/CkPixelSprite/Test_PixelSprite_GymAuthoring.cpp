#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "CkPixelSprite/CkPixelSprite_Asset.h"
#include "CkPixelSprite/CkPixelSprite_MaterialPreview.h"
#include "CkPixelSprite/CkPixelSprite_Player.h"
#include "CkCore/Macros/CkMacros.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "MaterialEditingLibrary.h"
#include "MaterialDomain.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "MeshDescription.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_pixel_sprite_gym_authoring
{
    constexpr TCHAR PackageRoot[] = TEXT("/CkTests/PixelSprite/");
    constexpr TCHAR MarkerKey[] = TEXT("CkPixelSpriteGymFixture");
    constexpr TCHAR MarkerValue[] = TEXT("GymAuthoring-v1");
    constexpr TCHAR ShaderName[] = TEXT("M_PixelSprite_Shader");
    constexpr TCHAR CubeName[] = TEXT("M_PixelSprite_Cubes");
    constexpr TCHAR QuadName[] = TEXT("SM_PixelSprite_Quad");

    // Each sample is at an integer texel's center. Thus even a linear sampler cannot mix
    // authored colors. Only alpha is integrated over the 3x3 cell neighborhood.
    constexpr TCHAR ShaderCode[] = LR"CK(
if (SpriteReady < 0.5) return float4(0.0, 0.0, 0.0, 0.0);
float2 size = max(CanvasSize.xy, float2(1.0, 1.0));
float2 pixel = UV * size;
if (any(pixel < 0.0) || any(pixel >= size)) return float4(0.0, 0.0, 0.0, 0.0);
float2 cell = floor(pixel);
float4 center = Texture2DSampleLevel(SpriteTexture, SpriteTextureSampler, (cell + 0.5) / size, 0);
if (EdgeAA < 0.5) return center;
float2 halfFootprint = min(max(fwidth(pixel) * 0.5, 0.0001), 1.0);
float2 lo = pixel - halfFootprint;
float2 hi = pixel + halfFootprint;
float coveredAlpha = 0.0;
float bestCoveredAlpha = 0.0;
float3 bestRGB = float3(0.0, 0.0, 0.0);
[unroll] for (int y = -1; y <= 1; ++y)
{
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        float2 neighbor = cell + float2(x, y);
        float2 overlap = max(min(hi, neighbor + 1.0) - max(lo, neighbor), 0.0);
        float area = overlap.x * overlap.y;
        if (area <= 0.0 || any(neighbor < 0.0) || any(neighbor >= size)) continue;
        float4 sampleRGBA = Texture2DSampleLevel(SpriteTexture, SpriteTextureSampler,
            (neighbor + 0.5) / size, 0);
        float contribution = area * sampleRGBA.a;
        coveredAlpha += contribution;
        // Strict comparison makes ties deterministic in y-major, then x-major order.
        if (contribution > bestCoveredAlpha)
        {
            bestCoveredAlpha = contribution;
            bestRGB = sampleRGBA.rgb;
        }
    }
}
float alpha = saturate(coveredAlpha / (4.0 * halfFootprint.x * halfFootprint.y));
float3 authoredRGB = center.a > 0.0 ? center.rgb : bestRGB;
return float4(authoredRGB, alpha);
)CK";

    auto PackagePath(const FString& InName) -> FString
    { return FString::Printf(TEXT("%s%s"), PackageRoot, *InName); }

    auto ObjectPath(const FString& InName) -> FString
    { return FString::Printf(TEXT("%s%s.%s"), PackageRoot, *InName, *InName); }

    auto IsMarked(const UObject& InAsset) -> bool
    {
        auto& Package = *InAsset.GetOutermost();
        return Package.GetMetaData().GetValue(&InAsset, MarkerKey).Equals(MarkerValue);
    }

    auto SaveAsset(UObject& InAsset) -> bool
    {
        const auto& Package = *InAsset.GetOutermost();
        const auto Filename = FPackageName::LongPackageNameToFilename(
            Package.GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        auto Args = FSavePackageArgs{};
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        Args.Error = GLog;
        return UPackage::SavePackage(InAsset.GetOutermost(), &InAsset, *Filename, Args);
    }

    template <typename TExpression>
    auto AddExpression(UMaterial& InMaterial, int32 InX, int32 InY) -> TObjectPtr<TExpression>
    {
        return Cast<TExpression>(UMaterialEditingLibrary::CreateMaterialExpression(
            &InMaterial, TExpression::StaticClass(), InX, InY));
    }

    auto AddCustomInput(UMaterialExpressionCustom& InCustom, FName InName) -> void
    {
        auto Input = FCustomInput{};
        Input.InputName = InName;
        InCustom.Inputs.Add(Input);
    }

    auto IsShaderOurs(const UMaterial& InMaterial) -> bool
    {
        if (NOT IsMarked(InMaterial) || InMaterial.MaterialDomain != MD_Surface
            || InMaterial.BlendMode != BLEND_Translucent
            || NOT InMaterial.GetShadingModels().HasShadingModel(MSM_Unlit)
            || NOT InMaterial.TwoSided || NOT InMaterial.bUsedWithInstancedStaticMeshes
            || NOT InMaterial.GetName().Equals(ShaderName))
        { return false; }

        TObjectPtr<UMaterialExpressionCustom> Custom;
        TObjectPtr<UMaterialExpressionTextureObjectParameter> Texture;
        TObjectPtr<UMaterialExpressionVectorParameter> CanvasSize;
        TObjectPtr<UMaterialExpressionScalarParameter> EdgeAA;
        TObjectPtr<UMaterialExpressionScalarParameter> SpriteReady;
        TObjectPtr<UMaterialExpressionTextureCoordinate> UV;
        TObjectPtr<UMaterialExpressionComponentMask> RGB;
        TObjectPtr<UMaterialExpressionComponentMask> Alpha;
        for (const auto& Expression : InMaterial.GetExpressionCollection().Expressions)
        {
            if (auto Found = TObjectPtr<UMaterialExpressionCustom>{Cast<UMaterialExpressionCustom>(Expression.Get())}) { Custom = Found; }
            if (auto Found = TObjectPtr<UMaterialExpressionTextureObjectParameter>{Cast<UMaterialExpressionTextureObjectParameter>(Expression.Get())}) { Texture = Found; }
            if (auto Found = TObjectPtr<UMaterialExpressionVectorParameter>{Cast<UMaterialExpressionVectorParameter>(Expression.Get())}) { CanvasSize = Found; }
            if (auto Found = TObjectPtr<UMaterialExpressionScalarParameter>{Cast<UMaterialExpressionScalarParameter>(Expression.Get())})
            {
                if (Found->ParameterName == TEXT("EdgeAA")) { EdgeAA = Found; }
                if (Found->ParameterName == TEXT("SpriteReady")) { SpriteReady = Found; }
            }
            if (auto Found = TObjectPtr<UMaterialExpressionTextureCoordinate>{Cast<UMaterialExpressionTextureCoordinate>(Expression.Get())}) { UV = Found; }
            if (auto Found = TObjectPtr<UMaterialExpressionComponentMask>{Cast<UMaterialExpressionComponentMask>(Expression.Get())})
            {
                if (Found->R && Found->G && Found->B && NOT Found->A) { RGB = Found; }
                if (NOT Found->R && NOT Found->G && NOT Found->B && Found->A) { Alpha = Found; }
            }
        }
        return Custom && Texture && CanvasSize && EdgeAA && SpriteReady && UV && RGB && Alpha
            && InMaterial.GetEditorOnlyData() != nullptr
            && Custom->OutputType == CMOT_Float4 && Custom->Code == ShaderCode
            && Custom->Inputs.Num() == 5
            && Custom->Inputs[0].InputName == TEXT("SpriteTexture")
            && Custom->Inputs[0].Input.Expression == Texture
            && Custom->Inputs[1].InputName == TEXT("UV")
            && Custom->Inputs[1].Input.Expression == UV
            && Custom->Inputs[2].InputName == TEXT("CanvasSize")
            && Custom->Inputs[2].Input.Expression == CanvasSize
            && Custom->Inputs[3].InputName == TEXT("EdgeAA")
            && Custom->Inputs[3].Input.Expression == EdgeAA
            && Custom->Inputs[4].InputName == TEXT("SpriteReady")
            && Custom->Inputs[4].Input.Expression == SpriteReady
            && Texture->ParameterName == TEXT("SpriteTexture") && Texture->Texture != nullptr
            && CanvasSize->ParameterName == TEXT("CanvasSize")
            && CanvasSize->DefaultValue == FLinearColor(32, 32, 0, 0)
            && EdgeAA->ParameterName == TEXT("EdgeAA") && EdgeAA->DefaultValue == 0.0f
            && SpriteReady->DefaultValue == 0.0f
            && RGB->Input.Expression == Custom && Alpha->Input.Expression == Custom
            && InMaterial.GetEditorOnlyData()->EmissiveColor.Expression == RGB
            && InMaterial.GetEditorOnlyData()->Opacity.Expression == Alpha
            && InMaterial.GetEditorOnlyData()->WorldPositionOffset.Expression == nullptr;
    }

    auto IsCubesOurs(const UMaterial& InMaterial) -> bool
    {
        if (NOT IsMarked(InMaterial) || InMaterial.MaterialDomain != MD_Surface
            || InMaterial.BlendMode != BLEND_Opaque
            || NOT InMaterial.GetShadingModels().HasShadingModel(MSM_DefaultLit)
            || InMaterial.TwoSided || NOT InMaterial.bUsedWithInstancedStaticMeshes
            || NOT InMaterial.GetName().Equals(CubeName))
        { return false; }
        TObjectPtr<UMaterialExpressionPerInstanceCustomData> Slots[3];
        TObjectPtr<UMaterialExpressionConstant> Roughness;
        for (const auto& Expression : InMaterial.GetExpressionCollection().Expressions)
        {
            if (auto Found = TObjectPtr<UMaterialExpressionPerInstanceCustomData>{Cast<UMaterialExpressionPerInstanceCustomData>(Expression.Get())})
            {
                if (Found->DataIndex < 3 && NOT Slots[Found->DataIndex])
                { Slots[Found->DataIndex] = Found; }
            }
            if (auto Found = TObjectPtr<UMaterialExpressionConstant>{Cast<UMaterialExpressionConstant>(Expression.Get())}) { Roughness = Found; }
        }
        if (NOT Slots[0] || NOT Slots[1] || NOT Slots[2] || NOT Roughness
            || InMaterial.GetEditorOnlyData() == nullptr)
        { return false; }
        TObjectPtr<UMaterialExpressionVertexInterpolator> VertexColor =
            Cast<UMaterialExpressionVertexInterpolator>(InMaterial.GetEditorOnlyData()->BaseColor.Expression);
        if (NOT VertexColor) { return false; }
        TObjectPtr<UMaterialExpressionAppendVector> RGB =
            Cast<UMaterialExpressionAppendVector>(VertexColor->Input.Expression);
        if (NOT RGB) { return false; }
        TObjectPtr<UMaterialExpressionAppendVector> RG =
            Cast<UMaterialExpressionAppendVector>(RGB->A.Expression);
        return RG && RG->A.Expression == Slots[0] && RG->B.Expression == Slots[1]
            && RGB->B.Expression == Slots[2]
            && Slots[0]->ConstDefaultValue == 1.0f
            && Slots[1]->ConstDefaultValue == 1.0f
            && Slots[2]->ConstDefaultValue == 1.0f
            && FMath::IsNearlyEqual(Roughness->R, 0.8f)
            && InMaterial.GetEditorOnlyData()->Roughness.Expression == Roughness
            && InMaterial.GetEditorOnlyData()->WorldPositionOffset.Expression == nullptr;
    }

    auto IsQuadOurs(const UStaticMesh& InMesh) -> bool
    {
        if (NOT IsMarked(InMesh) || InMesh.GetName() != QuadName || InMesh.GetRenderData() == nullptr
            || InMesh.GetRenderData()->LODResources.Num() != 1
            || InMesh.GetRenderData()->LODResources[0].GetNumTriangles() != 2
            || InMesh.GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() != 1
            || InMesh.GetStaticMaterials().Num() != 1)
        { return false; }
        const auto& Material = InMesh.GetStaticMaterials()[0].MaterialInterface;
        return Material && Material->GetPathName() == ObjectPath(ShaderName);
    }

    auto WriteShader() -> TObjectPtr<UMaterial>
    {
        TObjectPtr<UPackage> Package = CreatePackage(*PackagePath(ShaderName));
        if (NOT Package) { return nullptr; }
        TObjectPtr<UMaterial> Material = NewObject<UMaterial>(Package.Get(), ShaderName, RF_Public | RF_Standalone);
        if (NOT Material) { return nullptr; }
        Material->MaterialDomain = MD_Surface;
        Material->BlendMode = BLEND_Translucent;
        Material->SetShadingModel(MSM_Unlit);
        Material->TwoSided = true;
        Material->bUsedWithInstancedStaticMeshes = true;

        auto Custom = AddExpression<UMaterialExpressionCustom>(*Material, -300, 0);
        auto Texture = AddExpression<UMaterialExpressionTextureObjectParameter>(*Material, -800, -300);
        auto UV = AddExpression<UMaterialExpressionTextureCoordinate>(*Material, -800, -100);
        auto CanvasSize = AddExpression<UMaterialExpressionVectorParameter>(*Material, -800, 100);
        auto EdgeAA = AddExpression<UMaterialExpressionScalarParameter>(*Material, -800, 300);
        auto SpriteReady = AddExpression<UMaterialExpressionScalarParameter>(*Material, -800, 500);
        auto RGB = AddExpression<UMaterialExpressionComponentMask>(*Material, 0, -100);
        auto Alpha = AddExpression<UMaterialExpressionComponentMask>(*Material, 0, 100);
        TObjectPtr<UTexture> White = LoadObject<UTexture>(nullptr,
            TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
        if (NOT Custom || NOT Texture || NOT UV || NOT CanvasSize || NOT EdgeAA || NOT SpriteReady || NOT RGB || NOT Alpha || NOT White)
        { return nullptr; }

        Custom->OutputType = CMOT_Float4;
        Custom->Inputs.Reset();
        AddCustomInput(*Custom, TEXT("SpriteTexture"));
        AddCustomInput(*Custom, TEXT("UV"));
        AddCustomInput(*Custom, TEXT("CanvasSize"));
        AddCustomInput(*Custom, TEXT("EdgeAA"));
        AddCustomInput(*Custom, TEXT("SpriteReady"));
        Custom->Code = ShaderCode;
        Texture->ParameterName = TEXT("SpriteTexture");
        Texture->Texture = White;
        Texture->AutoSetSampleType();
        CanvasSize->ParameterName = TEXT("CanvasSize");
        CanvasSize->DefaultValue = FLinearColor(32, 32, 0, 0);
        EdgeAA->ParameterName = TEXT("EdgeAA");
        EdgeAA->DefaultValue = 0.0f;
        SpriteReady->ParameterName = TEXT("SpriteReady");
        SpriteReady->DefaultValue = 0.0f;
        RGB->R = RGB->G = RGB->B = true;
        RGB->A = false;
        Alpha->R = Alpha->G = Alpha->B = false;
        Alpha->A = true;
        if (NOT UMaterialEditingLibrary::ConnectMaterialExpressions(Texture.Get(), TEXT(""), Custom.Get(), TEXT("SpriteTexture"))
            || NOT UMaterialEditingLibrary::ConnectMaterialExpressions(UV.Get(), TEXT(""), Custom.Get(), TEXT("UV"))
            || NOT UMaterialEditingLibrary::ConnectMaterialExpressions(CanvasSize.Get(), TEXT(""), Custom.Get(), TEXT("CanvasSize"))
            || NOT UMaterialEditingLibrary::ConnectMaterialExpressions(EdgeAA.Get(), TEXT(""), Custom.Get(), TEXT("EdgeAA"))
            || NOT UMaterialEditingLibrary::ConnectMaterialExpressions(SpriteReady.Get(), TEXT(""), Custom.Get(), TEXT("SpriteReady"))
            || NOT UMaterialEditingLibrary::ConnectMaterialExpressions(Custom.Get(), TEXT(""), RGB.Get(), TEXT(""))
            || NOT UMaterialEditingLibrary::ConnectMaterialExpressions(Custom.Get(), TEXT(""), Alpha.Get(), TEXT(""))
            || NOT UMaterialEditingLibrary::ConnectMaterialProperty(RGB.Get(), TEXT(""), MP_EmissiveColor)
            || NOT UMaterialEditingLibrary::ConnectMaterialProperty(Alpha.Get(), TEXT(""), MP_Opacity))
        { return nullptr; }
        UMaterialEditingLibrary::RecompileMaterial(Material.Get());
        Package->GetMetaData().SetValue(Material.Get(), MarkerKey, MarkerValue);
        if (NOT IsShaderOurs(*Material)) { return nullptr; }
        Material->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(Material.Get());
        return SaveAsset(*Material) ? Material : nullptr;
    }

    auto WriteCubes() -> TObjectPtr<UMaterial>
    {
        TObjectPtr<UPackage> Package = CreatePackage(*PackagePath(CubeName));
        if (NOT Package) { return nullptr; }
        TObjectPtr<UMaterial> Material = NewObject<UMaterial>(Package.Get(), CubeName, RF_Public | RF_Standalone);
        if (NOT Material) { return nullptr; }
        Material->MaterialDomain = MD_Surface;
        Material->BlendMode = BLEND_Opaque;
        Material->SetShadingModel(MSM_DefaultLit);
        Material->TwoSided = false;
        Material->bUsedWithInstancedStaticMeshes = true;

        auto Red = AddExpression<UMaterialExpressionPerInstanceCustomData>(*Material, -900, -250);
        auto Green = AddExpression<UMaterialExpressionPerInstanceCustomData>(*Material, -900, -50);
        auto Blue = AddExpression<UMaterialExpressionPerInstanceCustomData>(*Material, -900, 150);
        auto RG = AddExpression<UMaterialExpressionAppendVector>(*Material, -600, -150);
        auto RGB = AddExpression<UMaterialExpressionAppendVector>(*Material, -350, 0);
        auto VertexColor = AddExpression<UMaterialExpressionVertexInterpolator>(*Material, -100, 0);
        auto Roughness = AddExpression<UMaterialExpressionConstant>(*Material, -350, 250);
        if (NOT Red || NOT Green || NOT Blue || NOT RG || NOT RGB || NOT VertexColor || NOT Roughness)
        { return nullptr; }
        Red->DataIndex = 0;
        Green->DataIndex = 1;
        Blue->DataIndex = 2;
        Red->ConstDefaultValue = Green->ConstDefaultValue = Blue->ConstDefaultValue = 1.0f;
        Roughness->R = 0.8f;
        if (NOT UMaterialEditingLibrary::ConnectMaterialExpressions(Red.Get(), TEXT(""), RG.Get(), TEXT("A"))
            || NOT UMaterialEditingLibrary::ConnectMaterialExpressions(Green.Get(), TEXT(""), RG.Get(), TEXT("B"))
            || NOT UMaterialEditingLibrary::ConnectMaterialExpressions(RG.Get(), TEXT(""), RGB.Get(), TEXT("A"))
            || NOT UMaterialEditingLibrary::ConnectMaterialExpressions(Blue.Get(), TEXT(""), RGB.Get(), TEXT("B"))
            || NOT UMaterialEditingLibrary::ConnectMaterialExpressions(RGB.Get(), TEXT(""), VertexColor.Get(), TEXT("VS"))
            || NOT UMaterialEditingLibrary::ConnectMaterialProperty(VertexColor.Get(), TEXT(""), MP_BaseColor)
            || NOT UMaterialEditingLibrary::ConnectMaterialProperty(Roughness.Get(), TEXT(""), MP_Roughness))
        { return nullptr; }
        UMaterialEditingLibrary::RecompileMaterial(Material.Get());
        Package->GetMetaData().SetValue(Material.Get(), MarkerKey, MarkerValue);
        if (NOT IsCubesOurs(*Material)) { return nullptr; }
        Material->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(Material.Get());
        return SaveAsset(*Material) ? Material : nullptr;
    }

    // Upgrade only our known fixture graph. Ordinary lit pixels should not acquire a white
    // specular hotspot that conceals the authored palette; diffuse face shading retains depth.
    auto ConfigureCubeSpecular(UMaterial& InMaterial) -> bool
    {
        if (NOT IsCubesOurs(InMaterial)) { return false; }
        if (InMaterial.GetEditorOnlyData()->Specular.Expression)
        {
            const auto Specular = TObjectPtr<UMaterialExpressionScalarParameter>{
                Cast<UMaterialExpressionScalarParameter>(InMaterial.GetEditorOnlyData()->Specular.Expression)};
            return Specular && Specular->ParameterName == TEXT("PixelSpecular") && Specular->DefaultValue == 0.0f;
        }
        const auto Specular = AddExpression<UMaterialExpressionScalarParameter>(InMaterial, -350, 400);
        if (NOT Specular) { return false; }
        Specular->ParameterName = TEXT("PixelSpecular");
        Specular->DefaultValue = 0.0f;
        if (NOT UMaterialEditingLibrary::ConnectMaterialProperty(Specular.Get(), TEXT(""), MP_Specular))
        { return false; }
        UMaterialEditingLibrary::RecompileMaterial(&InMaterial);
        return SaveAsset(InMaterial);
    }

    auto MakeQuadDescription() -> FMeshDescription
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
        const auto Group = Description.CreatePolygonGroup();
        SlotNames[Group] = TEXT("Sprite");
        const FVector3f Corners[4] = {{0, -50, 50}, {0, 50, 50}, {0, 50, -50}, {0, -50, -50}};
        const FVector2f Coordinates[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const int32 Triangles[2][3] = {{0, 1, 2}, {0, 2, 3}};
        FVertexID Vertices[4];
        for (int32 Index = 0; Index < 4; ++Index)
        {
            Vertices[Index] = Description.CreateVertex();
            Positions[Vertices[Index]] = Corners[Index];
        }
        for (const auto& Triangle : Triangles)
        {
            TArray<FVertexInstanceID> Instances;
            for (const auto Index : Triangle)
            {
                const auto Instance = Description.CreateVertexInstance(Vertices[Index]);
                Normals[Instance] = FVector3f{-1, 0, 0};
                Tangents[Instance] = FVector3f{0, 1, 0};
                Signs[Instance] = 1.0f;
                Colors[Instance] = FVector4f{1, 1, 1, 1};
                UVs.Set(Instance, 0, Coordinates[Index]);
                Instances.Add(Instance);
            }
            Description.CreatePolygon(Group, Instances);
        }
        return Description;
    }

    auto WriteQuad(UMaterial& InShader) -> bool
    {
        TObjectPtr<UPackage> Package = CreatePackage(*PackagePath(QuadName));
        if (NOT Package) { return false; }
        TObjectPtr<UStaticMesh> Mesh = NewObject<UStaticMesh>(Package.Get(), QuadName, RF_Public | RF_Standalone);
        if (NOT Mesh) { return false; }
        Mesh->bAllowCPUAccess = true;
        Mesh->NeverStream = true;
        Mesh->GetNaniteSettings().bEnabled = false;
        Mesh->SetStaticMaterials({FStaticMaterial{&InShader, TEXT("Sprite")}});
        Mesh->SetNumSourceModels(1);
        auto& Settings = Mesh->GetSourceModel(0).BuildSettings;
        Settings.bGenerateLightmapUVs = false;
        Settings.bRecomputeNormals = false;
        Settings.bRecomputeTangents = false;
        auto Description = MakeQuadDescription();
        auto Params = UStaticMesh::FBuildMeshDescriptionsParams{};
        Params.bCommitMeshDescription = true;
        Params.bFastBuild = false;
        Params.bAllowCpuAccess = true;
        Params.bBuildSimpleCollision = false;
        if (NOT Mesh->BuildFromMeshDescriptions({&Description}, Params)) { return false; }
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh.Get()});
        Package->GetMetaData().SetValue(Mesh.Get(), MarkerKey, MarkerValue);
        if (NOT IsQuadOurs(*Mesh)) { return false; }
        Mesh->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(Mesh.Get());
        return SaveAsset(*Mesh);
    }

    auto CheckCompiled(FAutomationTestBase& InTest, UMaterial& InMaterial) -> bool
    {
        if (NOT InTest.TestNotNull(*FString::Printf(TEXT("%s render resource"), *InMaterial.GetName()),
            InMaterial.GetMaterialResource(GMaxRHIShaderPlatform)))
        { return false; }
        auto& Resource = *InMaterial.GetMaterialResource(GMaxRHIShaderPlatform);
        const auto ReportState = [&]()
        {
            InTest.AddInfo(FString::Printf(TEXT("%s platform=%d quality=%d caching=%d map=%d complete=%d"),
                *InMaterial.GetName(), int32(GMaxRHIShaderPlatform), int32(Resource.GetQualityLevel()),
                Resource.IsCachingShaders(),
                Resource.GetGameThreadShaderMap() != nullptr, Resource.IsGameThreadShaderMapComplete()));
        };
        ReportState();
        Resource.FinishCompilation();
        ReportState();
        const auto& Errors = Resource.GetCompileErrors();
        for (const auto& Error : Errors) { InTest.AddError(InMaterial.GetName() + TEXT(": ") + Error); }
        const auto HasMap = InTest.TestTrue(*FString::Printf(TEXT("%s has an applied shader map"), *InMaterial.GetName()),
            Resource.GetGameThreadShaderMap() != nullptr);
        return HasMap && Errors.IsEmpty();
    }

    auto CheckMaterialBinding(FAutomationTestBase& InTest, UMaterial& InShader) -> bool
    {
        auto Asset = TStrongObjectPtr<UCk_PixelSpriteAsset_UE>{NewObject<UCk_PixelSpriteAsset_UE>()};
        auto Player = TStrongObjectPtr<UCk_PixelSpritePlayer_UE>{NewObject<UCk_PixelSpritePlayer_UE>()};
        auto Preview = TStrongObjectPtr<UCk_PixelSpriteMaterialPreview_UE>{
            NewObject<UCk_PixelSpriteMaterialPreview_UE>()};
        auto MID = TStrongObjectPtr<UMaterialInstanceDynamic>{
            UMaterialInstanceDynamic::Create(&InShader, Preview.Get())};
        if (NOT InTest.TestNotNull(TEXT("binding fixture has a material instance"), MID.Get()))
        { return false; }

        auto Manifest = FCk_PixelSprite_Manifest{};
        Manifest._Width = 2;
        Manifest._Height = 2;
        auto Source = FCk_PixelSprite_Source{};
        Source._Path = TEXT("sprite.xpm");
        Source._Bytes = 1;
        Source._Grid._Width = 2;
        Source._Grid._Height = 2;
        Source._Grid._Palette.Add({TEXT("."), FColor::Transparent});
        Source._Grid._Palette.Add({TEXT("R"), FColor::Red});
        Source._Grid._Indices = {0, 1, 1, 0};
        Manifest._Sources.Add(MoveTemp(Source));
        auto Animation = FCk_PixelSprite_Animation{};
        Animation._Name = TEXT("idle");
        auto Frame = FCk_PixelSprite_Frame{};
        Frame._Source = TEXT("sprite.xpm");
        Frame._SourceIndex = 0;
        Frame._DurationMilliseconds = 100;
        Animation._Frames.Add(MoveTemp(Frame));
        Manifest._Animations.Add(MoveTemp(Animation));
        auto Diagnostic = FCk_PixelSprite_Diagnostic{};
        if (NOT InTest.TestTrue(TEXT("authored source selects for shader binding"),
            Asset->Request_ReplaceManifest(Manifest, Diagnostic)
            && Player->TrySelectAsset(Asset.Get(), Diagnostic)))
        {
            InTest.AddError(Diagnostic.Get_Text());
            return false;
        }
        Preview->Player = Player.Get();
        Preview->Materials = {MID.Get()};
        InTest.TestTrue(TEXT("unbound presenter requires refresh"), Preview->Get_RequiresRefresh());
        if (NOT InTest.TestTrue(TEXT("real shader MID accepts raw sprite texture"), Preview->Request_Refresh()))
        { return false; }
        if (NOT InTest.TestEqual(TEXT("SpriteReady enables shader after binding"),
            MID->K2_GetScalarParameterValue(TEXT("SpriteReady")), 1.0f)
            || NOT InTest.TestEqual(TEXT("CanvasSize binds the source dimensions"),
                MID->K2_GetVectorParameterValue(TEXT("CanvasSize")), FLinearColor(2, 2, 0, 0))
            || NOT InTest.TestTrue(TEXT("SpriteTexture binds the current authored texture"),
                MID->K2_GetTextureParameterValue(TEXT("SpriteTexture")) == Player->Get_CurrentTexture().Get()))
        { return false; }
        InTest.TestFalse(TEXT("bound presenter is current"), Preview->Get_RequiresRefresh());
        Preview->Request_Clear();
        InTest.TestTrue(TEXT("cleared presenter requires refresh"), Preview->Get_RequiresRefresh());
        return InTest.TestEqual(TEXT("clear disables the real shader MID"),
            MID->K2_GetScalarParameterValue(TEXT("SpriteReady")), 0.0f);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_PixelSprite_GymAuthorAssets,
    "Ck.PixelSprite.Gym.AuthorAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

bool FCkTest_PixelSprite_GymAuthorAssets::RunTest(const FString&)
{
    using namespace ck_test_pixel_sprite_gym_authoring;
    if (NOT TestTrue(TEXT("asset authoring requires a render-capable editor"), FApp::CanEverRender()))
    { return false; }

    // Validate every pre-existing target before creating any missing target.
    TObjectPtr<UMaterial> Shader;
    TObjectPtr<UMaterial> Cubes;
    bool NeedsShader = NOT FPackageName::DoesPackageExist(PackagePath(ShaderName));
    bool NeedsCubes = NOT FPackageName::DoesPackageExist(PackagePath(CubeName));
    bool NeedsQuad = NOT FPackageName::DoesPackageExist(PackagePath(QuadName));
    if (NOT TestTrue(TEXT("missing targets have no unsaved object collision"),
        (NOT NeedsShader || FindObject<UObject>(nullptr, *ObjectPath(ShaderName)) == nullptr)
        && (NOT NeedsCubes || FindObject<UObject>(nullptr, *ObjectPath(CubeName)) == nullptr)
        && (NOT NeedsQuad || FindObject<UObject>(nullptr, *ObjectPath(QuadName)) == nullptr)))
    { return false; }
    if (NOT NeedsShader)
    {
        Shader = LoadObject<UMaterial>(nullptr, *ObjectPath(ShaderName));
        if (NOT TestTrue(TEXT("existing sprite shader is owned and valid"), Shader && IsShaderOurs(*Shader)))
        { return false; }
    }
    if (NOT NeedsCubes)
    {
        Cubes = LoadObject<UMaterial>(nullptr, *ObjectPath(CubeName));
        if (NOT TestTrue(TEXT("existing cube material is owned and valid"), Cubes && IsCubesOurs(*Cubes)))
        { return false; }
    }
    if (NOT NeedsQuad)
    {
        TObjectPtr<UStaticMesh> Quad = LoadObject<UStaticMesh>(nullptr, *ObjectPath(QuadName));
        if (NOT TestTrue(TEXT("existing sprite quad is owned and valid"), Quad && IsQuadOurs(*Quad)))
        { return false; }
    }

    if (NeedsShader)
    {
        Shader = WriteShader();
        if (NOT TestNotNull(TEXT("sprite shader saved"), Shader.Get())) { return false; }
    }
    if (NeedsCubes)
    {
        Cubes = WriteCubes();
        if (NOT TestNotNull(TEXT("cube material saved"), Cubes.Get())) { return false; }
    }
    if (NeedsQuad && NOT TestTrue(TEXT("sprite quad saved"), WriteQuad(*Shader)))
    { return false; }

    if (NOT TestTrue(TEXT("owned cube material preserves diffuse pixel colors without glare"), ConfigureCubeSpecular(*Cubes)))
    { return false; }
    const auto ShaderReady = CheckCompiled(*this, *Shader);
    const auto CubesReady = CheckCompiled(*this, *Cubes);
    const auto BindingReady = CheckMaterialBinding(*this, *Shader);
    return ShaderReady && CubesReady && BindingReady;
}

#endif
