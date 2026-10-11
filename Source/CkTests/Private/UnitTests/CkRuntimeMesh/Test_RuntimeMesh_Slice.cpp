#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../CkUnitTest_Common.h"
#include "CkRuntimeMesh/Internal/CkRuntimeMesh_Slice.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Generators/SweepGenerator.h"

namespace ck_test_runtime_mesh_slice
{
    namespace Geometry = ck::runtimemesh::geometry;
    namespace Slice = ck::runtimemesh::slice;

    constexpr int32 Faces[12][3] = {
        {0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
        {0, 5, 1}, {0, 4, 5}, {3, 6, 7}, {3, 2, 6},
        {0, 7, 4}, {0, 3, 7}, {1, 6, 2}, {1, 5, 6}};

    auto
        MakeCube(const FVector3d& InOffset = FVector3d::ZeroVector,
            int32 InUVLayers = 2, double InScale = 1.0) -> Geometry::FImportResult
    {
        const FVector3d Corners[8] = {
            {0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0},
            {0, 0, 10}, {10, 0, 10}, {10, 10, 10}, {0, 10, 10}};
        auto Mesh = UE::Geometry::FDynamicMesh3{};
        for (const auto& Corner : Corners)
        {
            Mesh.AppendVertex(Corner * InScale + InOffset);
        }
        for (const auto& Face : Faces)
        {
            Mesh.AppendTriangle(Face[0], Face[1], Face[2]);
        }
        Mesh.EnableAttributes();
        auto* Attributes = Mesh.Attributes();
        Attributes->SetNumUVLayers(InUVLayers);
        Attributes->EnablePrimaryColors();
        Attributes->EnableMaterialID();
        for (const auto TriangleID : Mesh.TriangleIndicesItr())
        {
            const auto Normal = FVector3f(Mesh.GetTriNormal(TriangleID));
            auto* Normals = Attributes->PrimaryNormals();
            auto* Colors = Attributes->PrimaryColors();
            const auto N0 = Normals->AppendElement(Normal);
            const auto N1 = Normals->AppendElement(Normal);
            const auto N2 = Normals->AppendElement(Normal);
            Normals->SetTriangle(TriangleID, UE::Geometry::FIndex3i{N0, N1, N2});
            const auto Red = 0.1f + static_cast<float>(TriangleID) * 0.02f;
            const auto C0 = Colors->AppendElement(FVector4f{Red, 0.2f, 0.6f, 1});
            const auto C1 = Colors->AppendElement(FVector4f{Red, 0.4f, 0.6f, 1});
            const auto C2 = Colors->AppendElement(FVector4f{Red, 0.8f, 0.6f, 1});
            Colors->SetTriangle(TriangleID, UE::Geometry::FIndex3i{C0, C1, C2});
            for (int32 Layer = 0; Layer < InUVLayers; ++Layer)
            {
                auto* UVs = Attributes->GetUVLayer(Layer);
                const auto U0 = UVs->AppendElement(FVector2f{0, static_cast<float>(Layer)});
                const auto U1 = UVs->AppendElement(FVector2f{1, static_cast<float>(Layer)});
                const auto U2 = UVs->AppendElement(FVector2f{0, 1 + static_cast<float>(Layer)});
                UVs->SetTriangle(TriangleID, UE::Geometry::FIndex3i{U0, U1, U2});
            }
            Attributes->GetMaterialID()->SetValue(TriangleID, TriangleID < 6 ? 0 : 1);
        }
        return Geometry::Admit(MoveTemp(Mesh), {});
    }

    // A per-vertex linear UV1 (a stand-in for a baked per-vertex channel such as a morph offset): interpolation along
    // a cut edge reproduces it exactly, so the cap rim can be checked against the function itself.
    auto
        LinearUV1(const FVector3d& InPosition) -> FVector2f
    {
        return FVector2f{static_cast<float>(0.1 * InPosition.X + 0.5), static_cast<float>(0.2 * InPosition.Z - 1.0)};
    }

    auto
        MakeLinearUV1Cube() -> Geometry::FImportResult
    {
        const FVector3d Corners[8] = {
            {0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0},
            {0, 0, 10}, {10, 0, 10}, {10, 10, 10}, {0, 10, 10}};
        auto Mesh = UE::Geometry::FDynamicMesh3{};
        for (const auto& Corner : Corners)
        {
            Mesh.AppendVertex(Corner);
        }
        for (const auto& Face : Faces)
        {
            Mesh.AppendTriangle(Face[0], Face[1], Face[2]);
        }
        Mesh.EnableAttributes();
        auto* Attributes = Mesh.Attributes();
        Attributes->SetNumUVLayers(2);
        Attributes->EnablePrimaryColors();
        Attributes->EnableMaterialID();
        for (const auto TriangleID : Mesh.TriangleIndicesItr())
        {
            const auto Triangle = Mesh.GetTriangle(TriangleID);
            const auto Normal = FVector3f(Mesh.GetTriNormal(TriangleID));
            auto* Normals = Attributes->PrimaryNormals();
            auto* Colors = Attributes->PrimaryColors();
            const auto N0 = Normals->AppendElement(Normal);
            const auto N1 = Normals->AppendElement(Normal);
            const auto N2 = Normals->AppendElement(Normal);
            Normals->SetTriangle(TriangleID, UE::Geometry::FIndex3i{N0, N1, N2});
            const auto C0 = Colors->AppendElement(FVector4f{1, 1, 1, 1});
            const auto C1 = Colors->AppendElement(FVector4f{1, 1, 1, 1});
            const auto C2 = Colors->AppendElement(FVector4f{1, 1, 1, 1});
            Colors->SetTriangle(TriangleID, UE::Geometry::FIndex3i{C0, C1, C2});
            auto* UV0 = Attributes->GetUVLayer(0);
            const auto U0 = UV0->AppendElement(FVector2f{0, 0});
            const auto U1 = UV0->AppendElement(FVector2f{1, 0});
            const auto U2 = UV0->AppendElement(FVector2f{0, 1});
            UV0->SetTriangle(TriangleID, UE::Geometry::FIndex3i{U0, U1, U2});
            auto* UV1 = Attributes->GetUVLayer(1);
            const auto L0 = UV1->AppendElement(LinearUV1(Mesh.GetVertex(Triangle.A)));
            const auto L1 = UV1->AppendElement(LinearUV1(Mesh.GetVertex(Triangle.B)));
            const auto L2 = UV1->AppendElement(LinearUV1(Mesh.GetVertex(Triangle.C)));
            UV1->SetTriangle(TriangleID, UE::Geometry::FIndex3i{L0, L1, L2});
            Attributes->GetMaterialID()->SetValue(TriangleID, TriangleID < 6 ? 0 : 1);
        }
        return Geometry::Admit(MoveTemp(Mesh), {});
    }

    // The default right arm is as wide as the left; a narrower inner edge widens it so the arms differ in volume.
    auto
        MakeUPrism(double InRightArmInnerX = 8.0) -> Geometry::FImportResult
    {
        auto Generator = UE::Geometry::FGeneralizedCylinderGenerator{};
        const FVector2d Profile[8] = {
            {0, 0}, {10, 0}, {10, 10}, {InRightArmInnerX, 10},
            {InRightArmInnerX, 2}, {2, 2}, {2, 10}, {0, 10}};
        for (const auto& Point : Profile)
        {
            Generator.CrossSection.AppendVertex(Point);
        }
        Generator.Path.Add(FVector3d{0, 0, 0});
        Generator.Path.Add(FVector3d{0, 0, 10});
        Generator.InitialFrame = UE::Geometry::FFrame3d{};
        Generator.bCapped = true;
        Generator.Generate();
        auto Mesh = UE::Geometry::FDynamicMesh3{&Generator};
        Mesh.DiscardAttributes();
        Mesh.EnableAttributes();
        auto* Attributes = Mesh.Attributes();
        Attributes->SetNumUVLayers(0);
        Attributes->EnablePrimaryColors();
        Attributes->EnableMaterialID();
        auto* Normals = Attributes->PrimaryNormals();
        auto* Colors = Attributes->PrimaryColors();
        for (const auto TriangleID : Mesh.TriangleIndicesItr())
        {
            const auto Normal = FVector3f(Mesh.GetTriNormal(TriangleID));
            const auto N0 = Normals->AppendElement(Normal);
            const auto N1 = Normals->AppendElement(Normal);
            const auto N2 = Normals->AppendElement(Normal);
            Normals->SetTriangle(TriangleID, UE::Geometry::FIndex3i{N0, N1, N2});
            const auto C0 = Colors->AppendElement(FVector4f{1, 1, 1, 1});
            const auto C1 = Colors->AppendElement(FVector4f{1, 1, 1, 1});
            const auto C2 = Colors->AppendElement(FVector4f{1, 1, 1, 1});
            Colors->SetTriangle(TriangleID, UE::Geometry::FIndex3i{C0, C1, C2});
            Attributes->GetMaterialID()->SetValue(TriangleID, 0);
        }
        return Geometry::Admit(MoveTemp(Mesh), {});
    }

    auto
        MakeOptions(const FVector3d& InPosition = FVector3d{5, 0, 0}) -> Slice::FCutOptions
    {
        auto Plane = Slice::FPlaneFrameLocal{};
        Plane.Set_PositionCm(InPosition);
        Plane.Set_Normal(FVector3d{1, 0, 0});
        Plane.Set_Tangent(FVector3d{0, 1, 0});
        auto Cap = Slice::FCapOptions{};
        Cap.Set_MaterialID(7);
        Cap.Set_Color(FVector4f{0.8f, 0.1f, 0.3f, 1});
        Cap.Set_CmPerUVUnit(10);
        Cap.Set_UVOffset(FVector2f{0.25f, 0.5f});
        auto Options = Slice::FCutOptions{};
        Options.Set_Plane(Plane);
        Options.Set_Cap(Cap);
        return Options;
    }

    auto
        CheckExteriorInterpolation(FAutomationTestBase& InTest,
            const Geometry::FValidatedGeometry& InSource,
            const Geometry::FValidatedGeometry& InPiece,
            const FVector3d& InPlanePosition,
            const FVector3d& InPlaneNormal) -> bool
    {
        const auto& Source = InSource.Get_Mesh();
        const auto& Piece = InPiece.Get_Mesh();
        const auto* SourceAttributes = Source.Attributes();
        const auto* PieceAttributes = Piece.Attributes();
        int32 CheckedCorners = 0;
        for (const auto PieceTriangleID : Piece.TriangleIndicesItr())
        {
            const auto Material = PieceAttributes->GetMaterialID()->GetValue(PieceTriangleID);
            if (Material == 7)
            { continue; }
            const auto PieceTriangle = Piece.GetTriangle(PieceTriangleID);
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const auto Position = Piece.GetVertex(PieceTriangle[Corner]);
                if (FMath::Abs((Position - InPlanePosition).Dot(InPlaneNormal)) > 0.0001)
                { continue; }
                const auto ActualNormal = PieceAttributes->PrimaryNormals()->GetElement(
                    PieceAttributes->PrimaryNormals()->GetTriangle(PieceTriangleID)[Corner]);
                const auto ActualColor = PieceAttributes->PrimaryColors()->GetElement(
                    PieceAttributes->PrimaryColors()->GetTriangle(PieceTriangleID)[Corner]);
                bool MatchedSourceCorner = false;
                for (const auto SourceTriangleID : Source.TriangleIndicesItr())
                {
                    if (SourceAttributes->GetMaterialID()->GetValue(SourceTriangleID) != Material)
                    { continue; }
                    const auto SourceTriangle = Source.GetTriangle(SourceTriangleID);
                    const auto A = Source.GetVertex(SourceTriangle.A);
                    const auto E0 = Source.GetVertex(SourceTriangle.B) - A;
                    const auto E1 = Source.GetVertex(SourceTriangle.C) - A;
                    const auto Q = Position - A;
                    const auto D00 = E0.Dot(E0);
                    const auto D01 = E0.Dot(E1);
                    const auto D11 = E1.Dot(E1);
                    const auto D20 = Q.Dot(E0);
                    const auto D21 = Q.Dot(E1);
                    const auto Denominator = D00 * D11 - D01 * D01;
                    if (Denominator <= 0)
                    { continue; }
                    const auto V = (D11 * D20 - D01 * D21) / Denominator;
                    const auto W = (D00 * D21 - D01 * D20) / Denominator;
                    const auto U = 1.0 - V - W;
                    if (U < -0.0001 || V < -0.0001 || W < -0.0001
                        || (A + E0 * V + E1 * W - Position).Length() > 0.0001)
                    { continue; }
                    const auto NormalIDs = SourceAttributes->PrimaryNormals()->GetTriangle(SourceTriangleID);
                    const auto ColorIDs = SourceAttributes->PrimaryColors()->GetTriangle(SourceTriangleID);
                    const auto SourceNormal = SourceAttributes->PrimaryNormals()->GetElement(NormalIDs.A);
                    const auto SourceColors = SourceAttributes->PrimaryColors();
                    const auto ColorA = SourceColors->GetElement(ColorIDs.A);
                    const auto ColorB = SourceColors->GetElement(ColorIDs.B);
                    const auto ColorC = SourceColors->GetElement(ColorIDs.C);
                    const auto ExpectedColor = ColorA * static_cast<float>(U)
                        + ColorB * static_cast<float>(V) + ColorC * static_cast<float>(W);
                    if (NOT ActualNormal.Equals(SourceNormal, 0.001f)
                        || NOT ActualColor.Equals(ExpectedColor, 0.001f))
                    { continue; }
                    bool AllUVsMatch = true;
                    for (int32 Layer = 0; Layer < SourceAttributes->NumUVLayers(); ++Layer)
                    {
                        const auto* SourceUVs = SourceAttributes->GetUVLayer(Layer);
                        const auto SourceUVTriangle = SourceUVs->GetTriangle(SourceTriangleID);
                        const auto ExpectedUV = SourceUVs->GetElement(SourceUVTriangle.A) * static_cast<float>(U)
                            + SourceUVs->GetElement(SourceUVTriangle.B) * static_cast<float>(V)
                            + SourceUVs->GetElement(SourceUVTriangle.C) * static_cast<float>(W);
                        const auto* PieceUVs = PieceAttributes->GetUVLayer(Layer);
                        const auto ActualUV = PieceUVs->GetElement(
                            PieceUVs->GetTriangle(PieceTriangleID)[Corner]);
                        AllUVsMatch &= ActualUV.Equals(ExpectedUV, 0.001f);
                    }
                    if (AllUVsMatch)
                    {
                        MatchedSourceCorner = true;
                        break;
                    }
                }
                InTest.TestTrue(TEXT("cut exterior corner interpolates source overlays"),
                    MatchedSourceCorner);
                ++CheckedCorners;
            }
        }
        InTest.TestTrue(TEXT("checked cut-edge exterior corners"), CheckedCorners > 0);
        return CheckedCorners > 0;
    }

    // InPlanarLayers: how many leading UV layers carry the planar cap projection (every layer by default).
    auto
        CheckCapAttributes(FAutomationTestBase& InTest,
            const Geometry::FValidatedGeometry& InPiece,
            const Slice::FCutOptions& InOptions, int32 InExpectedCaps,
            const FVector3f& InExpectedNormal,
            int32 InPlanarLayers = TNumericLimits<int32>::Max()) -> bool
    {
        const auto& Mesh = InPiece.Get_Mesh();
        const auto* Attributes = Mesh.Attributes();
        const auto FrameNormal = InOptions.Get_Plane().Get_Normal().GetSafeNormal();
        const auto FrameTangent = (InOptions.Get_Plane().Get_Tangent()
            - FrameNormal * InOptions.Get_Plane().Get_Tangent().Dot(FrameNormal)).GetSafeNormal();
        const auto FrameBitangent = FrameNormal.Cross(FrameTangent);
        int32 CapCount = 0;
        int32 ExteriorCount = 0;
        for (const auto TriangleID : Mesh.TriangleIndicesItr())
        {
            const auto Material = Attributes->GetMaterialID()->GetValue(TriangleID);
            if (Material != InOptions.Get_Cap().Get_MaterialID())
            {
                ++ExteriorCount;
                InTest.TestTrue(TEXT("exterior material remains 0 or 1"), Material == 0 || Material == 1);
                continue;
            }
            ++CapCount;
            const auto Triangle = Mesh.GetTriangle(TriangleID);
            const auto NormalIDs = Attributes->PrimaryNormals()->GetTriangle(TriangleID);
            const auto ColorIDs = Attributes->PrimaryColors()->GetTriangle(TriangleID);
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const auto Normal = Attributes->PrimaryNormals()->GetElement(NormalIDs[Corner]);
                const auto Color = Attributes->PrimaryColors()->GetElement(ColorIDs[Corner]);
                InTest.TestTrue(TEXT("cap normal points outward"),
                    FVector3f::DotProduct(Normal, InExpectedNormal) > 0.999f);
                InTest.TestTrue(TEXT("cap has requested color"),
                    Color.Equals(InOptions.Get_Cap().Get_Color(), 0.0001f));
                const auto Position = Mesh.GetVertex(Triangle[Corner]);
                const auto Origin = InOptions.Get_Plane().Get_PositionCm();
                const auto Relative = Position - Origin;
                const auto ExpectedUV = FVector2f{
                    static_cast<float>(Relative.Dot(FrameTangent)
                        / InOptions.Get_Cap().Get_CmPerUVUnit()) + InOptions.Get_Cap().Get_UVOffset().X,
                    static_cast<float>(Relative.Dot(FrameBitangent)
                        / InOptions.Get_Cap().Get_CmPerUVUnit()) + InOptions.Get_Cap().Get_UVOffset().Y};
                for (int32 Layer = 0; Layer < FMath::Min(Attributes->NumUVLayers(), InPlanarLayers); ++Layer)
                {
                    const auto UV = Attributes->GetUVLayer(Layer)->GetElement(
                        Attributes->GetUVLayer(Layer)->GetTriangle(TriangleID)[Corner]);
                    InTest.TestTrue(TEXT("cap UV uses shared request frame in every planar layer"),
                        UV.Equals(ExpectedUV, 0.0001f));
                }
            }
        }
        InTest.TestEqual(TEXT("cap triangle count matches result"), CapCount, InExpectedCaps);
        InTest.TestTrue(TEXT("piece keeps exterior triangles"), ExteriorCount > 0);
        return CapCount == InExpectedCaps && ExteriorCount > 0;
    }

    // Every cap corner of InPiece carries InExpected(position) in UV layer 1.
    template <typename T_Expected>
    auto
        CheckCapLayer1(FAutomationTestBase& InTest, const Geometry::FValidatedGeometry& InPiece,
            int32 InCapMaterialID, T_Expected&& InExpected) -> bool
    {
        const auto& Mesh = InPiece.Get_Mesh();
        const auto* Attributes = Mesh.Attributes();
        if (NOT InTest.TestEqual(TEXT("piece keeps two UV layers"), Attributes->NumUVLayers(), 2))
        { return false; }
        const auto* UV1 = Attributes->GetUVLayer(1);
        int32 CheckedCorners = 0;
        auto AllMatch = true;
        for (const auto TriangleID : Mesh.TriangleIndicesItr())
        {
            if (Attributes->GetMaterialID()->GetValue(TriangleID) != InCapMaterialID)
            { continue; }
            const auto Triangle = Mesh.GetTriangle(TriangleID);
            const auto Elements = UV1->GetTriangle(TriangleID);
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const auto Actual = UV1->GetElement(Elements[Corner]);
                const auto Expected = InExpected(Mesh.GetVertex(Triangle[Corner]));
                AllMatch &= Actual.Equals(Expected, 0.0001f);
                ++CheckedCorners;
            }
        }
        InTest.TestTrue(TEXT("every cap corner carries the expected UV1"), AllMatch);
        InTest.TestTrue(TEXT("checked cap UV1 corners"), CheckedCorners > 0);
        return AllMatch && CheckedCorners > 0;
    }

    auto
        MakeIslandOptions() -> Slice::FCutOptions
    {
        auto Options = MakeOptions(FVector3d{0, 5, 0});
        auto Plane = Options.Get_Plane();
        Plane.Set_Normal(FVector3d{0, 1, 0});
        Plane.Set_Tangent(FVector3d{1, 0, 0});
        Options.Set_Plane(Plane);
        return Options;
    }

    auto
        CheckOutcome(FAutomationTestBase& InTest, const Slice::FCutResult& InResult,
            Slice::ECutOutcome InExpected) -> bool
    {
        const auto Matching = InTest.TestEqual(TEXT("typed cut outcome"),
            static_cast<int32>(InResult.Get_Outcome()), static_cast<int32>(InExpected));
        const auto NoPositive = InTest.TestFalse(TEXT("failed cut has no positive payload"),
            InResult.Get_Positive().IsValid());
        const auto NoNegative = InTest.TestFalse(TEXT("failed cut has no negative payload"),
            InResult.Get_Negative().IsValid());
        const auto NoPieces = InTest.TestTrue(TEXT("failed cut has no pieces"),
            InResult.Get_PositivePieces().IsEmpty() && InResult.Get_NegativePieces().IsEmpty());
        return Matching && NoPositive && NoNegative && NoPieces;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Slice_CentredAttributedCube,
    "Ck.RuntimeMesh.Slice.CentredAttributedCube",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Slice_CentredAttributedCube::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_slice;
    const auto Source = MakeCube();
    if (TestTrue(TEXT("cube admitted"), Source.Get_IsReady()) == false)
    { return false; }
    const auto SourceTriangles = Source.Get_Geometry()->Get_Mesh().TriangleCount();
    const auto Options = MakeOptions();
    const auto Result = Slice::Cut(*Source.Get_Geometry(), Options);
    if (TestTrue(TEXT("centred cut succeeds"), Result.Get_IsSuccess()) == false)
    { return false; }
    TestTrue(TEXT("positive 500 cm3"),
        FMath::IsNearlyEqual(Result.Get_Positive()->Get_VolumeCm3(), 500.0, 0.01));
    TestTrue(TEXT("negative 500 cm3"),
        FMath::IsNearlyEqual(Result.Get_Negative()->Get_VolumeCm3(), 500.0, 0.01));
    TestEqual(TEXT("source triangle count unchanged"),
        Source.Get_Geometry()->Get_Mesh().TriangleCount(), SourceTriangles);
    TestTrue(TEXT("source volume unchanged"),
        FMath::IsNearlyEqual(Source.Get_Geometry()->Get_VolumeCm3(), 1000.0, 0.01));
    CheckCapAttributes(*this, *Result.Get_Positive(), Options,
        Result.Get_PositiveCapTriangles(), FVector3f{-1, 0, 0});
    CheckCapAttributes(*this, *Result.Get_Negative(), Options,
        Result.Get_NegativeCapTriangles(), FVector3f{1, 0, 0});
    CheckExteriorInterpolation(*this, *Source.Get_Geometry(), *Result.Get_Positive(),
        Options.Get_Plane().Get_PositionCm(), FVector3d{1, 0, 0});
    CheckExteriorInterpolation(*this, *Source.Get_Geometry(), *Result.Get_Negative(),
        Options.Get_Plane().Get_PositionCm(), FVector3d{1, 0, 0});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Slice_OffsetTranslatedAndRepeated,
    "Ck.RuntimeMesh.Slice.OffsetTranslatedAndRepeated",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Slice_OffsetTranslatedAndRepeated::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_slice;
    const auto Source = MakeCube(FVector3d{100, 200, -50});
    if (TestTrue(TEXT("translated cube admitted"), Source.Get_IsReady()) == false)
    { return false; }
    auto Options = MakeOptions(FVector3d{103, 200, -50});
    const auto First = Slice::Cut(*Source.Get_Geometry(), Options);
    if (TestTrue(TEXT("offset cut succeeds"), First.Get_IsSuccess()) == false)
    { return false; }
    TestTrue(TEXT("positive 700 cm3"),
        FMath::IsNearlyEqual(First.Get_Positive()->Get_VolumeCm3(), 700.0, 0.01));
    TestTrue(TEXT("negative 300 cm3"),
        FMath::IsNearlyEqual(First.Get_Negative()->Get_VolumeCm3(), 300.0, 0.01));
    Options = MakeOptions(FVector3d{106, 200, -50});
    const auto Second = Slice::Cut(*First.Get_Positive(), Options);
    if (TestTrue(TEXT("second-generation cut succeeds"), Second.Get_IsSuccess()) == false)
    { return false; }
    TestTrue(TEXT("second positive 400 cm3"),
        FMath::IsNearlyEqual(Second.Get_Positive()->Get_VolumeCm3(), 400.0, 0.01));
    TestTrue(TEXT("second negative 300 cm3"),
        FMath::IsNearlyEqual(Second.Get_Negative()->Get_VolumeCm3(), 300.0, 0.01));
    const auto FarSource = MakeCube(FVector3d{100000, 0, 0});
    if (TestTrue(TEXT("far local-origin cube admitted"), FarSource.Get_IsReady()) == false)
    { return false; }
    const auto FarCut = Slice::Cut(*FarSource.Get_Geometry(), MakeOptions(FVector3d{100005, 0, 0}));
    if (TestTrue(TEXT("far local-origin plane accepted"), FarCut.Get_IsSuccess()) == false)
    { return false; }
    TestTrue(TEXT("far positive 500 cm3"),
        FMath::IsNearlyEqual(FarCut.Get_Positive()->Get_VolumeCm3(), 500.0, 0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Slice_ObliqueAndZeroUV,
    "Ck.RuntimeMesh.Slice.ObliqueAndZeroUV",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Slice_ObliqueAndZeroUV::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_slice;
    const auto Source = MakeCube(FVector3d::ZeroVector, 0);
    if (TestTrue(TEXT("zero-UV cube admitted"), Source.Get_IsReady()) == false)
    { return false; }
    auto Options = MakeOptions(FVector3d{5, 5, 5});
    auto Plane = Options.Get_Plane();
    Plane.Set_Normal(FVector3d{1, 1, 0});
    Plane.Set_Tangent(FVector3d{-1, 1, 0});
    Options.Set_Plane(Plane);
    const auto Result = Slice::Cut(*Source.Get_Geometry(), Options);
    if (TestTrue(TEXT("oblique cut succeeds"), Result.Get_IsSuccess()) == false)
    { return false; }
    TestTrue(TEXT("oblique positive 500 cm3"),
        FMath::IsNearlyEqual(Result.Get_Positive()->Get_VolumeCm3(), 500.0, 0.01));
    TestTrue(TEXT("oblique negative 500 cm3"),
        FMath::IsNearlyEqual(Result.Get_Negative()->Get_VolumeCm3(), 500.0, 0.01));
    TestEqual(TEXT("cap introduces UV0 positive"),
        Result.Get_Positive()->Get_Mesh().Attributes()->NumUVLayers(), 1);
    TestEqual(TEXT("cap introduces UV0 negative"),
        Result.Get_Negative()->Get_Mesh().Attributes()->NumUVLayers(), 1);
    TestEqual(TEXT("source retains zero UV layers"),
        Source.Get_Geometry()->Get_Mesh().Attributes()->NumUVLayers(), 0);
    const auto Normal = Options.Get_Plane().Get_Normal().GetSafeNormal();
    CheckCapAttributes(*this, *Result.Get_Positive(), Options,
        Result.Get_PositiveCapTriangles(), FVector3f(-Normal));
    CheckCapAttributes(*this, *Result.Get_Negative(), Options,
        Result.Get_NegativeCapTriangles(), FVector3f(Normal));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Slice_RejectsMultipleIslands,
    "Ck.RuntimeMesh.Slice.RejectsMultipleIslands",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Slice_RejectsMultipleIslands::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_slice;
    const auto Source = MakeUPrism();
    if (TestTrue(TEXT("closed concave U prism admitted"), Source.Get_IsReady()) == false)
    { return false; }
    auto Options = MakeOptions(FVector3d{0, 5, 0});
    auto Plane = Options.Get_Plane();
    Plane.Set_Normal(FVector3d{0, 1, 0});
    Plane.Set_Tangent(FVector3d{1, 0, 0});
    Options.Set_Plane(Plane);
    const auto Result = Slice::Cut(*Source.Get_Geometry(), Options);
    return CheckOutcome(*this, Result, Slice::ECutOutcome::RejectedTopology);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Slice_SplitIslandsKeepsEveryPiece,
    "Ck.RuntimeMesh.Slice.SplitIslandsKeepsEveryPiece",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Slice_SplitIslandsKeepsEveryPiece::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_slice;
    const auto Source = MakeUPrism(7.0);
    if (TestTrue(TEXT("asymmetric concave U prism admitted"), Source.Get_IsReady()) == false)
    { return false; }
    TestTrue(TEXT("U prism is 600 cm3"),
        FMath::IsNearlyEqual(Source.Get_Geometry()->Get_VolumeCm3(), 600.0, 0.01));
    auto Options = MakeIslandOptions();
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), Options), Slice::ECutOutcome::RejectedTopology);

    Options.Set_IslandPolicy(Slice::EIslandPolicy::SplitIslands);
    const auto Result = Slice::Cut(*Source.Get_Geometry(), Options);
    if (TestTrue(TEXT("split-islands cut succeeds"), Result.Get_IsSuccess()) == false)
    { return false; }
    const auto& PositivePieces = Result.Get_PositivePieces();
    const auto& NegativePieces = Result.Get_NegativePieces();
    if (TestEqual(TEXT("two positive islands"), PositivePieces.Num(), 2) == false
        || TestEqual(TEXT("one negative island"), NegativePieces.Num(), 1) == false)
    { return false; }
    TestTrue(TEXT("largest island first: the 3 cm arm is 150 cm3"),
        FMath::IsNearlyEqual(PositivePieces[0].Get_Geometry()->Get_VolumeCm3(), 150.0, 0.01));
    TestTrue(TEXT("largest island is the right arm"),
        FMath::IsNearlyEqual(PositivePieces[0].Get_Geometry()->Get_CentroidCm().X, 8.5, 0.01));
    TestTrue(TEXT("second island: the 2 cm arm is 100 cm3"),
        FMath::IsNearlyEqual(PositivePieces[1].Get_Geometry()->Get_VolumeCm3(), 100.0, 0.01));
    TestTrue(TEXT("negative base is 350 cm3"),
        FMath::IsNearlyEqual(NegativePieces[0].Get_Geometry()->Get_VolumeCm3(), 350.0, 0.01));
    TestTrue(TEXT("Positive names the largest positive island"),
        Result.Get_Positive() == PositivePieces[0].Get_Geometry());
    TestTrue(TEXT("Negative names the negative island"),
        Result.Get_Negative() == NegativePieces[0].Get_Geometry());
    TestEqual(TEXT("positive cap count is the largest island's"),
        Result.Get_PositiveCapTriangles(), PositivePieces[0].Get_CapTriangles());
    const auto Total = PositivePieces[0].Get_Geometry()->Get_VolumeCm3()
        + PositivePieces[1].Get_Geometry()->Get_VolumeCm3()
        + NegativePieces[0].Get_Geometry()->Get_VolumeCm3();
    TestTrue(TEXT("islands conserve the source volume"),
        FMath::IsNearlyEqual(Total, Source.Get_Geometry()->Get_VolumeCm3(), 0.01));
    for (const auto& Piece : PositivePieces)
    {
        CheckCapAttributes(*this, *Piece.Get_Geometry(), Options, Piece.Get_CapTriangles(), FVector3f{0, -1, 0});
    }
    CheckCapAttributes(*this, *NegativePieces[0].Get_Geometry(), Options,
        NegativePieces[0].Get_CapTriangles(), FVector3f{0, 1, 0});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Slice_SplitIslandsLimits,
    "Ck.RuntimeMesh.Slice.SplitIslandsLimits",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Slice_SplitIslandsLimits::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_slice;
    const auto Source = MakeUPrism(7.0);
    if (TestTrue(TEXT("asymmetric concave U prism admitted"), Source.Get_IsReady()) == false)
    { return false; }
    auto Options = MakeIslandOptions();
    Options.Set_IslandPolicy(Slice::EIslandPolicy::SplitIslands);
    auto Limits = Options.Get_Limits();
    Limits.Set_MaximumPiecesPerSide(1);
    Options.Set_Limits(Limits);
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), Options), Slice::ECutOutcome::RejectedLimit);

    Limits = MakeIslandOptions().Get_Limits();
    Limits.Set_MaximumPiecesPerSide(0);
    Options.Set_Limits(Limits);
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), Options), Slice::ECutOutcome::InvalidRequest);

    // One island under the volume floor rejects the whole cut: a slice never publishes a subset of the source.
    Limits = MakeIslandOptions().Get_Limits();
    Limits.Set_MinimumOutputVolumeCm3(120.0);
    Options.Set_Limits(Limits);
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), Options), Slice::ECutOutcome::RejectedTooSmall);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Slice_CapUVPolicy,
    "Ck.RuntimeMesh.Slice.CapUVPolicy",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Slice_CapUVPolicy::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_slice;
    const auto Source = MakeLinearUV1Cube();
    if (TestTrue(TEXT("two-layer cube admitted"), Source.Get_IsReady()) == false)
    { return false; }
    constexpr auto PlanarLayers = 1;
    auto Options = MakeOptions();

    auto Cap = Options.Get_Cap();
    Cap.Set_UVPolicy(Slice::ECapUVPolicy::PlanarOnLayer0_ZeroOthers);
    Options.Set_Cap(Cap);
    const auto Zeroed = Slice::Cut(*Source.Get_Geometry(), Options);
    if (TestTrue(TEXT("zero-others cut succeeds"), Zeroed.Get_IsSuccess()) == false)
    { return false; }
    for (const auto& Piece : {Zeroed.Get_Positive(), Zeroed.Get_Negative()})
    {
        CheckCapLayer1(*this, *Piece, Cap.Get_MaterialID(),
            [](const FVector3d&) { return FVector2f::Zero(); });
        CheckExteriorInterpolation(*this, *Source.Get_Geometry(), *Piece,
            Options.Get_Plane().Get_PositionCm(), FVector3d{1, 0, 0});
    }
    CheckCapAttributes(*this, *Zeroed.Get_Positive(), Options,
        Zeroed.Get_PositiveCapTriangles(), FVector3f{-1, 0, 0}, PlanarLayers);
    CheckCapAttributes(*this, *Zeroed.Get_Negative(), Options,
        Zeroed.Get_NegativeCapTriangles(), FVector3f{1, 0, 0}, PlanarLayers);

    Cap.Set_UVPolicy(Slice::ECapUVPolicy::PlanarOnLayer0_KeepInterpolated);
    Options.Set_Cap(Cap);
    const auto Kept = Slice::Cut(*Source.Get_Geometry(), Options);
    if (TestTrue(TEXT("keep-interpolated cut succeeds"), Kept.Get_IsSuccess()) == false)
    { return false; }
    for (const auto& Piece : {Kept.Get_Positive(), Kept.Get_Negative()})
    {
        CheckCapLayer1(*this, *Piece, Cap.Get_MaterialID(),
            [](const FVector3d& InPosition) { return LinearUV1(InPosition); });
    }
    CheckCapAttributes(*this, *Kept.Get_Positive(), Options,
        Kept.Get_PositiveCapTriangles(), FVector3f{-1, 0, 0}, PlanarLayers);
    CheckCapAttributes(*this, *Kept.Get_Negative(), Options,
        Kept.Get_NegativeCapTriangles(), FVector3f{1, 0, 0}, PlanarLayers);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Slice_MissTouchAndInvalid,
    "Ck.RuntimeMesh.Slice.MissTouchAndInvalid",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Slice_MissTouchAndInvalid::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_slice;
    const auto Source = MakeCube();
    if (TestTrue(TEXT("cube admitted"), Source.Get_IsReady()) == false)
    { return false; }
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), MakeOptions(FVector3d{20, 0, 0})),
        Slice::ECutOutcome::NoIntersection);
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), MakeOptions(FVector3d{10, 0, 0})),
        Slice::ECutOutcome::TouchingOnly);
    auto TouchOptions = MakeOptions(FVector3d{10, 10, 0});
    auto TouchPlane = TouchOptions.Get_Plane();
    TouchPlane.Set_Normal(FVector3d{1, 1, 0});
    TouchPlane.Set_Tangent(FVector3d{-1, 1, 0});
    TouchOptions.Set_Plane(TouchPlane);
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), TouchOptions),
        Slice::ECutOutcome::TouchingOnly);
    TouchOptions = MakeOptions(FVector3d{10, 10, 10});
    TouchPlane = TouchOptions.Get_Plane();
    TouchPlane.Set_Normal(FVector3d{1, 1, 1});
    TouchOptions.Set_Plane(TouchPlane);
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), TouchOptions),
        Slice::ECutOutcome::TouchingOnly);
    auto Options = MakeOptions();
    auto Plane = Options.Get_Plane();
    Plane.Set_Tangent(FVector3d{2, 0, 0});
    Options.Set_Plane(Plane);
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), Options),
        Slice::ECutOutcome::InvalidRequest);
    Options = MakeOptions();
    auto Limits = Options.Get_Limits();
    Limits.Set_MaximumTriangles(12);
    Options.Set_Limits(Limits);
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), Options),
        Slice::ECutOutcome::RejectedLimit);
    Options = MakeOptions();
    Limits = Options.Get_Limits();
    Limits.Set_MaximumTriangles(11);
    Options.Set_Limits(Limits);
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), Options),
        Slice::ECutOutcome::RejectedLimit);
    Options = MakeOptions();
    Limits = Options.Get_Limits();
    Limits.Set_MaximumTriangles(3);
    Options.Set_Limits(Limits);
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), Options),
        Slice::ECutOutcome::InvalidRequest);
    Options = MakeOptions(FVector3d{0.001, 0, 0});
    Limits = Options.Get_Limits();
    Limits.Set_MinimumNormalExtentCm(0.01);
    Options.Set_Limits(Limits);
    CheckOutcome(*this, Slice::Cut(*Source.Get_Geometry(), Options),
        Slice::ECutOutcome::RejectedTooSmall);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Slice_BelowDefaultAdmissionVolume,
    "Ck.RuntimeMesh.Slice.BelowDefaultAdmissionVolume",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_Slice_BelowDefaultAdmissionVolume::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_slice;
    const auto Source = MakeCube(FVector3d::ZeroVector, 0, 0.0012);
    if (TestTrue(TEXT("small source clears importer default volume floor"), Source.Get_IsReady()) == false)
    { return false; }
    auto Options = MakeOptions(FVector3d{0.006, 0, 0});
    auto Limits = Options.Get_Limits();
    Limits.Set_MinimumOutputVolumeCm3(0.00000001);
    Limits.Set_AbsoluteVolumeToleranceCm3(0.0000000001);
    Options.Set_Limits(Limits);
    const auto Result = Slice::Cut(*Source.Get_Geometry(), Options);
    if (TestTrue(TEXT("both halves below importer default still admit"), Result.Get_IsSuccess()) == false)
    { return false; }
    TestTrue(TEXT("small positive half volume"),
        FMath::IsNearlyEqual(Result.Get_Positive()->Get_VolumeCm3(), 0.000000864, 0.00000001));
    TestTrue(TEXT("small negative half volume"),
        FMath::IsNearlyEqual(Result.Get_Negative()->Get_VolumeCm3(), 0.000000864, 0.00000001));
    return true;
}

#endif
