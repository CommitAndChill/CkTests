#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../CkUnitTest_Common.h"
#include "CkRuntimeMesh/Internal/CkRuntimeMesh_Geometry.h"

#include "Components.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Intersection/IntrTriangle3Triangle3.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

namespace ck_test_runtime_mesh_import
{
    // UE's signed-volume query uses Cross(V2-V0, V1-V0). Every triangle is outward in
    // that convention; 36 render vertices split face and triangle attribute seams.
    constexpr int32 Faces[12][3] = {
        {0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
        {0, 5, 1}, {0, 4, 5}, {3, 6, 7}, {3, 2, 6},
        {0, 7, 4}, {0, 3, 7}, {1, 6, 2}, {1, 5, 6}};

    const FVector3f Corners[8] = {
        {0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0},
        {0, 0, 10}, {10, 0, 10}, {10, 10, 10}, {0, 10, 10}};

    struct FFixture
    {
        TStrongObjectPtr<UStaticMesh> Asset;
        TArray<FStaticMeshBuildVertex> Vertices;
        TArray<uint32> Indices;
    };

    auto
    MakeCube(int32 InUVLayers = 2, bool InCpuAccess = true, bool InColors = true) -> FFixture
    {
        auto Fixture = FFixture{};
        Fixture.Asset = TStrongObjectPtr<UStaticMesh>{NewObject<UStaticMesh>(GetTransientPackage())};
        Fixture.Asset->bAllowCPUAccess = InCpuAccess;
        Fixture.Asset->GetStaticMaterials().SetNum(2);

        for (int32 Triangle = 0; Triangle < 12; ++Triangle)
        {
            const auto& Face = Faces[Triangle];
            const auto Normal = FVector3f::CrossProduct(
                Corners[Face[2]] - Corners[Face[0]], Corners[Face[1]] - Corners[Face[0]]).GetSafeNormal();
            const auto TangentX = (Corners[Face[1]] - Corners[Face[0]]).GetSafeNormal();
            const auto TangentY = FVector3f::CrossProduct(Normal, TangentX).GetSafeNormal();

            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                auto Vertex = FStaticMeshBuildVertex{};
                Vertex.Position = Corners[Face[Corner]];
                Vertex.TangentX = TangentX;
                Vertex.TangentY = TangentY;
                Vertex.TangentZ = Normal;
                Vertex.Color = FColor{static_cast<uint8>(30 + 20 * Face[Corner]), 80, 160, 255};
                for (int32 Layer = 0; Layer < InUVLayers; ++Layer)
                {
                    Vertex.UVs[Layer] = FVector2f{
                        static_cast<float>(Triangle) / 16.0f + static_cast<float>(Corner) / 64.0f,
                        static_cast<float>(Layer) / 4.0f + static_cast<float>(Corner) / 8.0f};
                }
                Fixture.Indices.Add(Fixture.Vertices.Add(Vertex));
            }
        }

        auto RenderData = MakeUnique<FStaticMeshRenderData>();
        RenderData->AllocateLODResources(1);
        auto& LOD = RenderData->LODResources[0];
        LOD.VertexBuffers.PositionVertexBuffer.Init(Fixture.Vertices, InCpuAccess);
        LOD.VertexBuffers.StaticMeshVertexBuffer.Init(Fixture.Vertices.Num(), InUVLayers, InCpuAccess);
        for (int32 VertexIndex = 0; VertexIndex < Fixture.Vertices.Num(); ++VertexIndex)
        {
            const auto& Vertex = Fixture.Vertices[VertexIndex];
            LOD.VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(
                VertexIndex, Vertex.TangentX, Vertex.TangentY, Vertex.TangentZ);
            for (int32 Layer = 0; Layer < InUVLayers; ++Layer)
            {
                LOD.VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(VertexIndex, Layer, Vertex.UVs[Layer]);
            }
        }
        if (InColors)
        { LOD.VertexBuffers.ColorVertexBuffer.Init(Fixture.Vertices, InCpuAccess); }
        LOD.bHasColorVertexData = InColors;
        LOD.IndexBuffer.TrySetAllowCPUAccess(InCpuAccess);
        LOD.IndexBuffer.SetIndices(Fixture.Indices, EIndexBufferStride::Force16Bit);

        for (int32 SectionIndex = 0; SectionIndex < 2; ++SectionIndex)
        {
            auto& Section = LOD.Sections.AddDefaulted_GetRef();
            Section.MaterialIndex = SectionIndex;
            Section.FirstIndex = SectionIndex * 18;
            Section.NumTriangles = 6;
            Section.MinVertexIndex = SectionIndex * 18;
            Section.MaxVertexIndex = SectionIndex * 18 + 17;
        }

        Fixture.Asset->SetRenderData(MoveTemp(RenderData));
        return Fixture;
    }

    auto
    SetIndices(FFixture& InFixture, const TArray<uint32>& InIndices) -> void
    {
        InFixture.Indices = InIndices;
        InFixture.Asset->GetRenderData()->LODResources[0].IndexBuffer.SetIndices(
            InFixture.Indices, EIndexBufferStride::Force16Bit);
    }

    auto
    AppendRawCube(UE::Geometry::FDynamicMesh3& InMesh, const FVector3d& InOffset,
        double InScale = 1.0, bool InInvertWinding = false) -> void
    {
        int32 VertexIDs[8];
        for (int32 Index = 0; Index < 8; ++Index)
        {
            VertexIDs[Index] = InMesh.AppendVertex(FVector3d(Corners[Index]) * InScale + InOffset);
        }
        for (const auto& Face : Faces)
        {
            if (InInvertWinding)
            { InMesh.AppendTriangle(VertexIDs[Face[0]], VertexIDs[Face[2]], VertexIDs[Face[1]]); }
            else
            { InMesh.AppendTriangle(VertexIDs[Face[0]], VertexIDs[Face[1]], VertexIDs[Face[2]]); }
        }
    }

    auto
    AddRequiredAttributes(UE::Geometry::FDynamicMesh3& InMesh) -> void
    {
        InMesh.EnableAttributes();
        auto* Attributes = InMesh.Attributes();
        Attributes->SetNumUVLayers(0);
        Attributes->EnableMaterialID();
        Attributes->EnablePrimaryColors();
        auto* Normals = Attributes->PrimaryNormals();
        auto* Colors = Attributes->PrimaryColors();
        auto* Materials = Attributes->GetMaterialID();
        for (const int32 TriangleID : InMesh.TriangleIndicesItr())
        {
            const auto Normal = FVector3f(InMesh.GetTriNormal(TriangleID));
            const auto N0 = Normals->AppendElement(Normal);
            const auto N1 = Normals->AppendElement(Normal);
            const auto N2 = Normals->AppendElement(Normal);
            Normals->SetTriangle(TriangleID, UE::Geometry::FIndex3i{N0, N1, N2});
            const auto C0 = Colors->AppendElement(FVector4f{1, 1, 1, 1});
            const auto C1 = Colors->AppendElement(FVector4f{1, 1, 1, 1});
            const auto C2 = Colors->AppendElement(FVector4f{1, 1, 1, 1});
            Colors->SetTriangle(TriangleID, UE::Geometry::FIndex3i{C0, C1, C2});
            Materials->SetValue(TriangleID, 0);
        }
    }

    auto
    IsFailureWithoutPayload(
        FAutomationTestBase& InTest,
        const TCHAR* InLabel,
        const ck::runtimemesh::geometry::FImportResult& InResult,
        ck::runtimemesh::geometry::EImportFailure InExpected) -> bool
    {
        const auto FailureMatches = InTest.TestEqual(InLabel,
            static_cast<int32>(InResult.Get_Failure()), static_cast<int32>(InExpected));
        const auto NoPayload = InTest.TestFalse(TEXT("rejected import publishes no geometry"), InResult.Get_Geometry().IsValid());
        return FailureMatches && NoPayload;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Import_SeamedCubePreservesGeometryAndAttributes,
    "Ck.RuntimeMesh.Import.SeamedCubePreservesGeometryAndAttributes",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Import_SeamedCubePreservesGeometryAndAttributes::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_import;
    namespace Geometry = ck::runtimemesh::geometry;

    auto Fixture = MakeCube();
    TestEqual(TEXT("render fixture has two UV layers"), Fixture.Asset->GetRenderData()
        ->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords(), 2u);
    const auto Result = Geometry::Import(*Fixture.Asset, {});
    if (TestTrue(TEXT("render LOD cube imports"), Result.Get_IsReady()) == false)
    { return false; }

    const auto& Payload = *Result.Get_Geometry();
    const auto& Mesh = Payload.Get_Mesh();
    TestEqual(TEXT("all 12 exterior triangles imported"), Mesh.TriangleCount(), 12);
    TestEqual(TEXT("36 split render vertices weld to 8 geometric vertices"), Mesh.VertexCount(), 8);
    TestTrue(TEXT("10 cm cube volume is 1000 cm3"), FMath::IsNearlyEqual(Payload.Get_VolumeCm3(), 1000.0, 0.01));
    TestTrue(TEXT("centroid retains original local origin"), Payload.Get_CentroidCm().Equals(FVector3d{5, 5, 5}, 0.001));
    TestTrue(TEXT("lower bound retains origin"), Payload.Get_BoundsCm().Min.Equals(FVector3d::ZeroVector, 0.001));
    TestTrue(TEXT("upper bound is 10 cm"), Payload.Get_BoundsCm().Max.Equals(FVector3d{10, 10, 10}, 0.001));

    const auto* Attributes = Mesh.Attributes();
    if (TestNotNull(TEXT("import has corner attributes"), Attributes) == false)
    { return false; }
    TestEqual(TEXT("both UV layers retained"), Attributes->NumUVLayers(), 2);
    TestNotNull(TEXT("hard normals retained"), Attributes->PrimaryNormals());
    TestNotNull(TEXT("vertex colors retained"), Attributes->PrimaryColors());
    if (TestNotNull(TEXT("material IDs retained"), Attributes->GetMaterialID()) == false)
    { return false; }

    if (Attributes->PrimaryNormals() == nullptr || Attributes->PrimaryColors() == nullptr
        || Attributes->GetUVLayer(0) == nullptr || Attributes->GetUVLayer(1) == nullptr)
    { return false; }

    for (int32 Triangle = 0; Triangle < 12; ++Triangle)
    {
        const auto MeshTriangle = Mesh.GetTriangle(Triangle);
        const auto NormalTriangle = Attributes->PrimaryNormals()->GetTriangle(Triangle);
        const auto ColorTriangle = Attributes->PrimaryColors()->GetTriangle(Triangle);
        const auto UV0Triangle = Attributes->GetUVLayer(0)->GetTriangle(Triangle);
        const auto UV1Triangle = Attributes->GetUVLayer(1)->GetTriangle(Triangle);
        const auto& Face = Faces[Triangle];
        const auto ExpectedNormal = FVector3f::CrossProduct(
            Corners[Face[2]] - Corners[Face[0]], Corners[Face[1]] - Corners[Face[0]]).GetSafeNormal();

        TestEqual(TEXT("every triangle retains its material section"),
            Attributes->GetMaterialID()->GetValue(Triangle), Triangle < 6 ? 0 : 1);
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            const auto SourceIndex = Triangle * 3 + Corner;
            const auto& Source = Fixture.Vertices[SourceIndex];
            TestTrue(TEXT("geometric corner position is unchanged by seam welding"),
                Mesh.GetVertex(MeshTriangle[Corner]).Equals(FVector3d(Source.Position), 0.001));
            TestTrue(TEXT("hard normal survives at the triangle corner"),
                Attributes->PrimaryNormals()->GetElement(NormalTriangle[Corner]).Equals(ExpectedNormal, 0.03f));
            TestTrue(TEXT("UV0 seam value survives at the triangle corner"),
                Attributes->GetUVLayer(0)->GetElement(UV0Triangle[Corner]).Equals(Source.UVs[0], 0.003f));
            TestTrue(TEXT("UV1 seam value survives at the triangle corner"),
                Attributes->GetUVLayer(1)->GetElement(UV1Triangle[Corner]).Equals(Source.UVs[1], 0.003f));
            const auto ActualColor = Attributes->PrimaryColors()->GetElement(ColorTriangle[Corner]);
            const auto ExpectedColor = Source.Color.ReinterpretAsLinear();
            TestTrue(TEXT("vertex color survives at the triangle corner"),
                FMath::IsNearlyEqual(ActualColor.X, ExpectedColor.R, 0.002f)
                && FMath::IsNearlyEqual(ActualColor.Y, ExpectedColor.G, 0.002f)
                && FMath::IsNearlyEqual(ActualColor.Z, ExpectedColor.B, 0.002f)
                && FMath::IsNearlyEqual(ActualColor.W, ExpectedColor.A, 0.002f));
            TestTrue(TEXT("render LOD position was not mutated by import"),
                Fixture.Asset->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer
                    .VertexPosition(SourceIndex).Equals(Source.Position, 0.001f));
            TestTrue(TEXT("render LOD normal was not mutated by import"),
                Fixture.Asset->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer
                    .VertexTangentZ(SourceIndex).Equals(Source.TangentZ, 0.03f));
            TestTrue(TEXT("render LOD UV0 was not mutated by import"),
                Fixture.Asset->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer
                    .GetVertexUV(SourceIndex, 0).Equals(Source.UVs[0], 0.003f));
            TestEqual(TEXT("render LOD color was not mutated by import"),
                Fixture.Asset->GetRenderData()->LODResources[0].VertexBuffers.ColorVertexBuffer
                    .VertexColor(SourceIndex), Source.Color);
            TestEqual(TEXT("render LOD index was not mutated by import"),
                Fixture.Asset->GetRenderData()->LODResources[0].IndexBuffer.GetIndex(SourceIndex),
                Fixture.Indices[SourceIndex]);
        }
    }

    Fixture.Asset->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer.VertexPosition(0) =
        FVector3f{1000, 1000, 1000};
    TestTrue(TEXT("imported geometry owns a copy of render positions"),
        Mesh.GetVertex(0).GetAbsMax() <= 10.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Validation_RenderAvailability,
    "Ck.RuntimeMesh.Validation.RenderAvailability",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Validation_RenderAvailability::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_import;
    namespace Geometry = ck::runtimemesh::geometry;

    auto Empty = TStrongObjectPtr<UStaticMesh>{NewObject<UStaticMesh>(GetTransientPackage())};
    IsFailureWithoutPayload(*this, TEXT("missing render data"), Geometry::Import(*Empty, {}),
        Geometry::EImportFailure::MissingRenderData);

    auto Fixture = MakeCube();
    auto Options = Geometry::FImportOptions{};
    Options.Set_LODIndex(1);
    IsFailureWithoutPayload(*this, TEXT("unavailable explicit LOD is rejected"),
        Geometry::Import(*Fixture.Asset, Options), Geometry::EImportFailure::InvalidLOD);

    auto NoCpu = MakeCube(2, false);
    IsFailureWithoutPayload(*this, TEXT("non-CPU render buffers are rejected"),
        Geometry::Import(*NoCpu.Asset, {}), Geometry::EImportFailure::CpuDataUnavailable);

    auto AssetFlagOff = MakeCube();
    AssetFlagOff.Asset->bAllowCPUAccess = false;
    IsFailureWithoutPayload(*this, TEXT("asset CPU-access policy is required even when buffers are resident"),
        Geometry::Import(*AssetFlagOff.Asset, {}), Geometry::EImportFailure::CpuDataUnavailable);

    auto StreamedOut = MakeCube();
    StreamedOut.Asset->GetRenderData()->CurrentFirstLODIdx = 1;
    IsFailureWithoutPayload(*this, TEXT("explicit LOD below current streamed first LOD is unavailable"),
        Geometry::Import(*StreamedOut.Asset, {}), Geometry::EImportFailure::CpuDataUnavailable);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Import_MissingColorsAndUVsUseContractDefaults,
    "Ck.RuntimeMesh.Import.MissingColorsAndUVsUseContractDefaults",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Import_MissingColorsAndUVsUseContractDefaults::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_import;
    namespace Geometry = ck::runtimemesh::geometry;

    auto Fixture = MakeCube(0, true, false);
    TestEqual(TEXT("render fixture has zero UV layers"), Fixture.Asset->GetRenderData()
        ->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords(), 0u);
    const auto Result = Geometry::Import(*Fixture.Asset, {});
    if (TestTrue(TEXT("closed cube with no authored UV or colors imports"), Result.Get_IsReady()) == false)
    { return false; }

    const auto* Attributes = Result.Get_Geometry()->Get_Mesh().Attributes();
    if (TestNotNull(TEXT("attribute set exists"), Attributes) == false)
    { return false; }
    TestEqual(TEXT("missing UVs do not fabricate an authored layer"), Attributes->NumUVLayers(), 0);
    if (TestNotNull(TEXT("missing colors have opaque-white overlay"), Attributes->PrimaryColors()) == false)
    { return false; }
    for (int32 Triangle = 0; Triangle < 12; ++Triangle)
    {
        const auto ColorTriangle = Attributes->PrimaryColors()->GetTriangle(Triangle);
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            const auto Color = Attributes->PrimaryColors()->GetElement(ColorTriangle[Corner]);
            TestTrue(TEXT("missing color is opaque white on every corner"),
                Color.Equals(FVector4f{1, 1, 1, 1}, 0.001f));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Validation_OptionsAndLimits,
    "Ck.RuntimeMesh.Validation.OptionsAndLimits",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Validation_OptionsAndLimits::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_import;
    namespace Geometry = ck::runtimemesh::geometry;

    auto Fixture = MakeCube();
    auto Options = Geometry::FImportOptions{};
    Options.Set_SeamToleranceCm(-1.0);
    IsFailureWithoutPayload(*this, TEXT("negative seam tolerance"), Geometry::Import(*Fixture.Asset, Options),
        Geometry::EImportFailure::InvalidOptions);
    Options = {};
    Options.Set_IntersectionToleranceCm(0.0);
    IsFailureWithoutPayload(*this, TEXT("zero intersection tolerance"), Geometry::Import(*Fixture.Asset, Options),
        Geometry::EImportFailure::InvalidOptions);
    Options = {};
    Options.Set_MaximumVertices(2049);
    IsFailureWithoutPayload(*this, TEXT("caller ceiling cannot exceed module ceiling"),
        Geometry::Import(*Fixture.Asset, Options), Geometry::EImportFailure::InvalidOptions);
    Options = {};
    Options.Set_MaximumTriangles(11);
    IsFailureWithoutPayload(*this, TEXT("triangle ceiling"), Geometry::Import(*Fixture.Asset, Options),
        Geometry::EImportFailure::LimitExceeded);
    Options = {};
    Options.Set_MaximumVertices(7);
    IsFailureWithoutPayload(*this, TEXT("raw render vertex ceiling"), Geometry::Import(*Fixture.Asset, Options),
        Geometry::EImportFailure::LimitExceeded);

    auto NativeCube = UE::Geometry::FDynamicMesh3{};
    AppendRawCube(NativeCube, FVector3d::ZeroVector);
    AddRequiredAttributes(NativeCube);
    TestEqual(TEXT("native fixture has exactly eight vertices"), NativeCube.VertexCount(), 8);
    IsFailureWithoutPayload(*this, TEXT("native welded vertex ceiling"),
        Geometry::Admit(MoveTemp(NativeCube), Options), Geometry::EImportFailure::LimitExceeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Validation_RenderBufferContracts,
    "Ck.RuntimeMesh.Validation.RenderBufferContracts",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Validation_RenderBufferContracts::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_import;
    namespace Geometry = ck::runtimemesh::geometry;

    auto UVOverflow = MakeCube(5);
    TestEqual(TEXT("overflow fixture has five UV layers"), UVOverflow.Asset->GetRenderData()
        ->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords(), 5u);
    IsFailureWithoutPayload(*this, TEXT("five UV layers rejected"), Geometry::Import(*UVOverflow.Asset, {}),
        Geometry::EImportFailure::InvalidAttribute);

    auto NonFinitePosition = MakeCube();
    NonFinitePosition.Asset->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer
        .VertexPosition(0).X = std::numeric_limits<float>::quiet_NaN();
    IsFailureWithoutPayload(*this, TEXT("nonfinite position rejected"),
        Geometry::Import(*NonFinitePosition.Asset, {}), Geometry::EImportFailure::InvalidAttribute);

    auto NonFiniteUV = MakeCube();
    NonFiniteUV.Asset->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer
        .SetVertexUV(0, 0, FVector2f{std::numeric_limits<float>::quiet_NaN(), 0});
    IsFailureWithoutPayload(*this, TEXT("nonfinite corner UV rejected"),
        Geometry::Import(*NonFiniteUV.Asset, {}), Geometry::EImportFailure::InvalidAttribute);

    auto ZeroNormal = MakeCube();
    auto& VertexBuffer = ZeroNormal.Asset->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer;
    VertexBuffer.SetVertexTangents(0, FVector3f{1, 0, 0}, FVector3f{0, 1, 0}, FVector3f::ZeroVector);
    IsFailureWithoutPayload(*this, TEXT("zero-length authored normal rejected"),
        Geometry::Import(*ZeroNormal.Asset, {}), Geometry::EImportFailure::InvalidAttribute);

    auto BadIndex = MakeCube();
    BadIndex.Indices[0] = 999;
    SetIndices(BadIndex, BadIndex.Indices);
    IsFailureWithoutPayload(*this, TEXT("index outside position buffer"), Geometry::Import(*BadIndex.Asset, {}),
        Geometry::EImportFailure::InvalidIndexOrSection);

    auto BadSection = MakeCube();
    BadSection.Asset->GetRenderData()->LODResources[0].Sections[1].FirstIndex = 17;
    IsFailureWithoutPayload(*this, TEXT("overlapping section range"), Geometry::Import(*BadSection.Asset, {}),
        Geometry::EImportFailure::InvalidIndexOrSection);

    auto BadMaterial = MakeCube();
    BadMaterial.Asset->GetRenderData()->LODResources[0].Sections[1].MaterialIndex = 2;
    IsFailureWithoutPayload(*this, TEXT("section material outside slots"), Geometry::Import(*BadMaterial.Asset, {}),
        Geometry::EImportFailure::InvalidMaterial);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Validation_OpenAndDuplicateTriangles,
    "Ck.RuntimeMesh.Validation.OpenAndDuplicateTriangles",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Validation_OpenAndDuplicateTriangles::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_import;
    namespace Geometry = ck::runtimemesh::geometry;

    auto Open = MakeCube();
    Open.Asset->GetRenderData()->LODResources[0].Sections[1].NumTriangles = 5;
    Open.Indices.SetNum(33);
    SetIndices(Open, Open.Indices);
    IsFailureWithoutPayload(*this, TEXT("missing last face triangle leaves boundary"),
        Geometry::Import(*Open.Asset, {}), Geometry::EImportFailure::InvalidTopology);

    auto Duplicate = MakeCube();
    for (int32 Corner = 0; Corner < 3; ++Corner)
    { Duplicate.Indices[33 + Corner] = Duplicate.Indices[30 + Corner]; }
    SetIndices(Duplicate, Duplicate.Indices);
    IsFailureWithoutPayload(*this, TEXT("duplicate triangle invalidates closed manifold"),
        Geometry::Import(*Duplicate.Asset, {}), Geometry::EImportFailure::InvalidTopology);

    auto Ambiguous = MakeCube();
    auto& AmbiguousPositions = Ambiguous.Asset->GetRenderData()->LODResources[0]
        .VertexBuffers.PositionVertexBuffer;
    AmbiguousPositions.VertexPosition(33) = Corners[0];
    AmbiguousPositions.VertexPosition(34) = Corners[1];
    AmbiguousPositions.VertexPosition(35) = Corners[4];
    IsFailureWithoutPayload(*this, TEXT("three split boundary edges at the same positions are ambiguous"),
        Geometry::Import(*Ambiguous.Asset, {}), Geometry::EImportFailure::AmbiguousSeam);

    auto Nonmanifold = MakeCube();
    Nonmanifold.Indices[3] = 0;
    Nonmanifold.Indices[4] = 1;
    Nonmanifold.Indices[6] = 0;
    Nonmanifold.Indices[7] = 1;
    SetIndices(Nonmanifold, Nonmanifold.Indices);
    IsFailureWithoutPayload(*this, TEXT("three render triangles sharing a raw edge are nonmanifold"),
        Geometry::Import(*Nonmanifold.Asset, {}), Geometry::EImportFailure::InvalidTopology);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Validation_DisconnectedAndInvertedSolids,
    "Ck.RuntimeMesh.Validation.DisconnectedAndInvertedSolids",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Validation_DisconnectedAndInvertedSolids::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_import;
    namespace Geometry = ck::runtimemesh::geometry;

    auto Disconnected = UE::Geometry::FDynamicMesh3{};
    AppendRawCube(Disconnected, FVector3d::ZeroVector);
    AppendRawCube(Disconnected, FVector3d{30, 0, 0});
    AddRequiredAttributes(Disconnected);
    IsFailureWithoutPayload(*this, TEXT("two disjoint closed shells are not one solid"),
        Geometry::Admit(MoveTemp(Disconnected), {}), Geometry::EImportFailure::InvalidTopology);

    auto Cavity = UE::Geometry::FDynamicMesh3{};
    AppendRawCube(Cavity, FVector3d::ZeroVector);
    AppendRawCube(Cavity, FVector3d{2.5, 2.5, 2.5}, 0.5, true);
    AddRequiredAttributes(Cavity);
    IsFailureWithoutPayload(*this, TEXT("closed shell enclosing an inner cavity shell is rejected"),
        Geometry::Admit(MoveTemp(Cavity), {}), Geometry::EImportFailure::InvalidTopology);

    auto Inverted = UE::Geometry::FDynamicMesh3{};
    AppendRawCube(Inverted, FVector3d::ZeroVector, 1.0, true);
    AddRequiredAttributes(Inverted);
    IsFailureWithoutPayload(*this, TEXT("closed shell with negative signed volume rejected"),
        Geometry::Admit(MoveTemp(Inverted), {}), Geometry::EImportFailure::NonPositiveVolume);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Validation_AdjacentCoplanarFold,
    "Ck.RuntimeMesh.Validation.AdjacentCoplanarFold",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Validation_AdjacentCoplanarFold::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_import;
    namespace Geometry = ck::runtimemesh::geometry;

    auto Folded = UE::Geometry::FDynamicMesh3{};
    AppendRawCube(Folded, FVector3d::ZeroVector);
    // The top's two triangles share edge 4-6. Moving vertex 7 inside triangle
    // 4-6-5 folds the second triangle over its coplanar neighbour.
    Folded.SetVertex(7, FVector3d{7, 2, 10});
    AddRequiredAttributes(Folded);
    TestTrue(TEXT("folded shell remains edge-closed"), Folded.IsClosed());
    TestFalse(TEXT("fold is not a bowtie shortcut"), Folded.IsBowtieVertex(7));

    FVector3d A0, A1, A2, B0, B1, B2;
    Folded.GetTriVertices(2, A0, A1, A2);
    Folded.GetTriVertices(3, B0, B1, B2);
    auto Intersection = UE::Geometry::FIntrTriangle3Triangle3d{
        UE::Geometry::FTriangle3d{A0, A1, A2}, UE::Geometry::FTriangle3d{B0, B1, B2}};
    Intersection.SetReportCoplanarIntersection(true);
    if (TestTrue(TEXT("top triangle pair intersects in the coplanar narrowphase"), Intersection.Find()) == false)
    { return false; }
    bool HasPointOffSharedDiagonal = false;
    for (int32 PointIndex = 0; PointIndex < Intersection.Quantity; ++PointIndex)
    {
        HasPointOffSharedDiagonal |= FMath::Abs(Intersection.Points[PointIndex].X
            - Intersection.Points[PointIndex].Y) > 0.01;
    }
    if (TestTrue(TEXT("intersection has area beyond the shared diagonal"), HasPointOffSharedDiagonal) == false)
    { return false; }
    IsFailureWithoutPayload(*this, TEXT("adjacent coplanar overlap is self-intersection"),
        Geometry::Admit(MoveTemp(Folded), {}), Geometry::EImportFailure::SelfIntersection);

    auto RotatedFold = UE::Geometry::FDynamicMesh3{};
    AppendRawCube(RotatedFold, FVector3d::ZeroVector);
    RotatedFold.SetVertex(7, FVector3d{7, 2, 10});
    const auto Rotation = FQuat4d{FVector3d{1, 2, 3}.GetSafeNormal(), 0.37};
    for (const int32 VertexID : RotatedFold.VertexIndicesItr())
    {
        RotatedFold.SetVertex(VertexID,
            Rotation.RotateVector(RotatedFold.GetVertex(VertexID)) + FVector3d{30, -12, 7});
    }
    AddRequiredAttributes(RotatedFold);
    TestTrue(TEXT("rotated folded shell remains edge-closed"), RotatedFold.IsClosed());
    IsFailureWithoutPayload(*this, TEXT("rotated adjacent overlap is self-intersection"),
        Geometry::Admit(MoveTemp(RotatedFold), {}), Geometry::EImportFailure::SelfIntersection);

    auto RotatedCube = UE::Geometry::FDynamicMesh3{};
    AppendRawCube(RotatedCube, FVector3d::ZeroVector);
    for (const int32 VertexID : RotatedCube.VertexIndicesItr())
    {
        RotatedCube.SetVertex(VertexID,
            Rotation.RotateVector(RotatedCube.GetVertex(VertexID)) + FVector3d{30, -12, 7});
    }
    AddRequiredAttributes(RotatedCube);
    const auto ValidRotated = Geometry::Admit(MoveTemp(RotatedCube), {});
    TestTrue(TEXT("same rotation admits a valid closed cube"), ValidRotated.Get_IsReady());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Validation_ClosedBowtieVertex,
    "Ck.RuntimeMesh.Validation.ClosedBowtieVertex",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Validation_ClosedBowtieVertex::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_import;
    namespace Geometry = ck::runtimemesh::geometry;

    auto Pinched = UE::Geometry::FDynamicMesh3{};
    int32 VertexIDs[8];
    for (int32 Index = 0; Index < 8; ++Index)
    { VertexIDs[Index] = Pinched.AppendVertex(FVector3d(Corners[Index])); }
    const int32 PinchID = Pinched.AppendVertex(FVector3d{5, 5, 0});

    // The bottom and top each have a four-triangle fan around PinchID. Their fans
    // share one vertex but no edge; the side faces connect the overall shell.
    Pinched.AppendTriangle(VertexIDs[0], VertexIDs[1], PinchID);
    Pinched.AppendTriangle(VertexIDs[1], VertexIDs[2], PinchID);
    Pinched.AppendTriangle(VertexIDs[2], VertexIDs[3], PinchID);
    Pinched.AppendTriangle(VertexIDs[3], VertexIDs[0], PinchID);
    Pinched.AppendTriangle(VertexIDs[4], PinchID, VertexIDs[5]);
    Pinched.AppendTriangle(VertexIDs[5], PinchID, VertexIDs[6]);
    Pinched.AppendTriangle(VertexIDs[6], PinchID, VertexIDs[7]);
    Pinched.AppendTriangle(VertexIDs[7], PinchID, VertexIDs[4]);
    for (int32 Triangle = 4; Triangle < 12; ++Triangle)
    {
        const auto& Face = Faces[Triangle];
        Pinched.AppendTriangle(VertexIDs[Face[0]], VertexIDs[Face[1]], VertexIDs[Face[2]]);
    }
    AddRequiredAttributes(Pinched);

    TestEqual(TEXT("all face triangles are present"), Pinched.TriangleCount(), 16);
    TestTrue(TEXT("pinched shell is edge-closed"), Pinched.IsClosed());
    if (TestTrue(TEXT("the centre has two disconnected triangle fans"), Pinched.IsBowtieVertex(PinchID)) == false)
    { return false; }
    IsFailureWithoutPayload(*this, TEXT("closed bowtie is invalid topology"),
        Geometry::Admit(MoveTemp(Pinched), {}), Geometry::EImportFailure::InvalidTopology);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Validation_NativeAttributeContract,
    "Ck.RuntimeMesh.Validation.NativeAttributeContract",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Validation_NativeAttributeContract::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_import;
    namespace Geometry = ck::runtimemesh::geometry;

    auto Bare = UE::Geometry::FDynamicMesh3{};
    AppendRawCube(Bare, FVector3d::ZeroVector);
    IsFailureWithoutPayload(*this, TEXT("native admission requires complete material and corner attributes"),
        Geometry::Admit(MoveTemp(Bare), {}), Geometry::EImportFailure::InvalidAttribute);

    auto TooManyUVs = UE::Geometry::FDynamicMesh3{};
    AppendRawCube(TooManyUVs, FVector3d::ZeroVector);
    AddRequiredAttributes(TooManyUVs);
    auto* Attributes = TooManyUVs.Attributes();
    Attributes->SetNumUVLayers(5);
    for (int32 Layer = 0; Layer < 5; ++Layer)
    {
        auto* UV = Attributes->GetUVLayer(Layer);
        for (const int32 TriangleID : TooManyUVs.TriangleIndicesItr())
        {
            const auto U0 = UV->AppendElement(FVector2f{0, 0});
            const auto U1 = UV->AppendElement(FVector2f{1, 0});
            const auto U2 = UV->AppendElement(FVector2f{0, 1});
            UV->SetTriangle(TriangleID, UE::Geometry::FIndex3i{U0, U1, U2});
        }
    }
    IsFailureWithoutPayload(*this, TEXT("five complete native UV layers exceed admission contract"),
        Geometry::Admit(MoveTemp(TooManyUVs), {}), Geometry::EImportFailure::InvalidAttribute);

    auto OrphanVertex = UE::Geometry::FDynamicMesh3{};
    AppendRawCube(OrphanVertex, FVector3d::ZeroVector);
    AddRequiredAttributes(OrphanVertex);
    const int32 UnusedID = OrphanVertex.AppendVertex(FVector3d{1000, 0, 0});
    TestEqual(TEXT("unused vertex extends beyond valid cube"), OrphanVertex.GetVertex(UnusedID).X, 1000.0);
    IsFailureWithoutPayload(*this, TEXT("unused far vertex cannot enter immutable geometry"),
        Geometry::Admit(MoveTemp(OrphanVertex), {}), Geometry::EImportFailure::InvalidTopology);
    return true;
}

#endif
