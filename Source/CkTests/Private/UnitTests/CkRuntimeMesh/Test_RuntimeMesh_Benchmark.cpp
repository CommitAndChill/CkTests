#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkRuntimeMesh/CkRuntimeMesh_Processor.h"
#include "CkRuntimeMesh/CkRuntimeMesh_Utils.h"
#include "CkRuntimeMesh/Internal/CkRuntimeMesh_Slice.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkCore/Time/CkTime.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkTest_RuntimeMeshListener.h"

#include "Dom/JsonObject.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_runtime_mesh_benchmark
{
    namespace Geometry = ck::runtimemesh::geometry;
    namespace Slice = ck::runtimemesh::slice;

    inline constexpr auto BenchmarkTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    inline constexpr auto BenchmarkName = TEXT("Ck.RuntimeMesh.Benchmark.BoundedWorkloads");

    inline constexpr int32 WarmupIterations = 2;
    inline constexpr int32 MeasuredIterations = 20;

    inline constexpr double BoxExtentCm = 10.0;
    inline constexpr int32 UVLayerCount = 1;
    inline constexpr int32 CapMaterialID = 0;
    inline constexpr int32 FaceCount = 6;

    inline constexpr auto InformEngineOfWorld = false;
    inline constexpr TCHAR CpuFixturePath[] =
        TEXT("/CkTests/CkRuntimeMesh/Cooked/SM_Import_CPU.SM_Import_CPU");

    inline constexpr int32 VertexEdgeListIntsPerVertex = 11;
    inline constexpr int32 NormalFloats = 3;
    inline constexpr int32 ColorFloats = 4;
    inline constexpr int32 UVFloats = 2;

    struct FBoxFace
    {
        int32 Origin[3];
        int32 AxisU[3];
        int32 AxisV[3];
        float Normal[3];
    };

    inline constexpr FBoxFace BoxFaces[FaceCount] = {
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}},
        {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}, {0, -1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}},
        {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}}};

    struct FWorkload
    {
        const TCHAR* Name;
        int32 Subdivisions;
    };

    inline constexpr FWorkload Workloads[] = {
        {TEXT("Box12"), 1},
        {TEXT("Box192"), 4},
        {TEXT("Box768"), 8},
        {TEXT("Box3072"), 16}};

    struct FSampleSummary
    {
        double Min = 0.0;
        double Median = 0.0;
        double Max = 0.0;
    };

    enum class ESummaryUnit : uint8
    {
        Milliseconds,
        SignedBytes,
        Count
    };

    struct FOperationSample
    {
        double Milliseconds = 0.0;
        double UsedPhysicalDeltaBytes = 0.0;
    };

    struct FOperationSeries
    {
        TArray<double> Milliseconds;
        TArray<double> UsedPhysicalDeltaBytes;
    };

    struct FMeshShape
    {
        int32 Vertices = 0;
        int32 Triangles = 0;
        int32 CapTriangles = 0;
        double VolumeCm3 = 0.0;
        int64 OwnedBytesEstimate = 0;
    };

    struct FWorkloadReport
    {
        FString Name;
        int32 Subdivisions = 0;
        FMeshShape Source;
        FMeshShape Positive;
        FMeshShape Negative;
        FOperationSeries Admit;
        FOperationSeries Cut;
    };

    struct FQueueReport
    {
        int32 SourceTriangles = 0;
        int32 Rounds = 0;
        TArray<double> DrainMilliseconds;
        TArray<double> DrainsPerRound;
        TArray<double> EnqueueToEmptyMilliseconds;
    };

    struct FTriangleCorners
    {
        int32 TriangleID = 0;
        int32 Face = 0;
        UE::Geometry::FIndex3i Corners;
    };

    // --------------------------------------------------------------------------------------------------------------------

    auto
        Summarize(
            TArray<double> InSamples)
        -> FSampleSummary
    {
        InSamples.Sort();
        const auto Count = InSamples.Num();
        const auto Middle = Count / 2;
        const auto Median = Count % 2 == 0
            ? (InSamples[Middle - 1] + InSamples[Middle]) * 0.5
            : InSamples[Middle];
        return FSampleSummary{InSamples[0], Median, InSamples.Last()};
    }

    auto
        ExpectedVertexCount(
            int32 InSubdivisions)
        -> int32
    {
        return 6 * InSubdivisions * InSubdivisions + 2;
    }

    auto
        ExpectedTriangleCount(
            int32 InSubdivisions)
        -> int32
    {
        return 12 * InSubdivisions * InSubdivisions;
    }

    auto
        EstimateOverlayBytes(
            int32 InElementCount,
            int32 InElementFloats,
            int32 InTriangleCount)
        -> int64
    {
        const auto BytesPerElement = static_cast<int64>(InElementFloats * sizeof(float) + sizeof(int32) + sizeof(uint16));
        const auto BytesPerTriangle = static_cast<int64>(3 * sizeof(int32));
        return InElementCount * BytesPerElement + InTriangleCount * BytesPerTriangle;
    }

    auto
        EstimateOwnedMeshBytes(
            const UE::Geometry::FDynamicMesh3& InMesh)
        -> int64
    {
        const auto BytesPerVertex = static_cast<int64>(
            sizeof(FVector3d) + sizeof(uint16) + VertexEdgeListIntsPerVertex * sizeof(int32));
        const auto BytesPerTriangle = static_cast<int64>(
            sizeof(UE::Geometry::FIndex3i) + sizeof(uint16) + sizeof(UE::Geometry::FIndex3i) + sizeof(int32));
        const auto BytesPerEdge = static_cast<int64>(2 * sizeof(UE::Geometry::FIndex2i) + sizeof(uint16));

        auto Bytes = InMesh.VertexCount() * BytesPerVertex
            + InMesh.TriangleCount() * BytesPerTriangle
            + InMesh.EdgeCount() * BytesPerEdge;

        if (NOT InMesh.HasAttributes())
        { return Bytes; }

        const auto* Attributes = InMesh.Attributes();
        const auto TriangleCount = InMesh.TriangleCount();

        if (Attributes->PrimaryNormals() != nullptr)
        { Bytes += EstimateOverlayBytes(Attributes->PrimaryNormals()->ElementCount(), NormalFloats, TriangleCount); }

        if (Attributes->PrimaryColors() != nullptr)
        { Bytes += EstimateOverlayBytes(Attributes->PrimaryColors()->ElementCount(), ColorFloats, TriangleCount); }

        for (auto Layer = 0; Layer < Attributes->NumUVLayers(); ++Layer)
        { Bytes += EstimateOverlayBytes(Attributes->GetUVLayer(Layer)->ElementCount(), UVFloats, TriangleCount); }

        return Bytes;
    }

    auto
        MakeShape(
            const Geometry::FValidatedGeometry& InGeometry,
            int32 InCapTriangles)
        -> FMeshShape
    {
        const auto& Mesh = InGeometry.Get_Mesh();
        return FMeshShape{Mesh.VertexCount(), Mesh.TriangleCount(), InCapTriangles,
            InGeometry.Get_VolumeCm3(), EstimateOwnedMeshBytes(Mesh)};
    }

    // --------------------------------------------------------------------------------------------------------------------

    auto
        MakeTessellatedBox(
            int32 InSubdivisions)
        -> UE::Geometry::FDynamicMesh3
    {
        const auto GridSide = InSubdivisions + 1;
        const auto CellCm = BoxExtentCm / static_cast<double>(InSubdivisions);
        auto Mesh = UE::Geometry::FDynamicMesh3{};
        auto VertexByGridKey = TMap<int32, int32>{};
        auto Triangles = TArray<FTriangleCorners>{};
        Triangles.Reserve(ExpectedTriangleCount(InSubdivisions));

        const auto FindOrAddVertex = [&](const FBoxFace& InFace, int32 InU, int32 InV) -> int32
        {
            int32 Grid[3];
            for (auto Axis = 0; Axis < 3; ++Axis)
            { Grid[Axis] = InFace.Origin[Axis] * InSubdivisions + InFace.AxisU[Axis] * InU + InFace.AxisV[Axis] * InV; }

            const auto Key = Grid[0] + GridSide * (Grid[1] + GridSide * Grid[2]);
            if (const auto* Existing = VertexByGridKey.Find(Key))
            { return *Existing; }

            const auto VertexID = Mesh.AppendVertex(FVector3d{Grid[0] * CellCm, Grid[1] * CellCm, Grid[2] * CellCm});
            VertexByGridKey.Add(Key, VertexID);
            return VertexID;
        };

        for (auto FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
        {
            const auto& Face = BoxFaces[FaceIndex];
            for (auto V = 0; V < InSubdivisions; ++V)
            {
                for (auto U = 0; U < InSubdivisions; ++U)
                {
                    const auto P00 = FindOrAddVertex(Face, U, V);
                    const auto P10 = FindOrAddVertex(Face, U + 1, V);
                    const auto P11 = FindOrAddVertex(Face, U + 1, V + 1);
                    const auto P01 = FindOrAddVertex(Face, U, V + 1);
                    const auto L00 = V * GridSide + U;
                    const auto L10 = V * GridSide + U + 1;
                    const auto L11 = (V + 1) * GridSide + U + 1;
                    const auto L01 = (V + 1) * GridSide + U;

                    const auto First = Mesh.AppendTriangle(P00, P10, P11);
                    if (First >= 0)
                    { Triangles.Add(FTriangleCorners{First, FaceIndex, UE::Geometry::FIndex3i{L00, L10, L11}}); }

                    const auto Second = Mesh.AppendTriangle(P00, P11, P01);
                    if (Second >= 0)
                    { Triangles.Add(FTriangleCorners{Second, FaceIndex, UE::Geometry::FIndex3i{L00, L11, L01}}); }
                }
            }
        }

        Mesh.EnableAttributes();
        auto* Attributes = Mesh.Attributes();
        Attributes->SetNumUVLayers(UVLayerCount);
        Attributes->EnablePrimaryColors();
        Attributes->EnableMaterialID();
        auto* Normals = Attributes->PrimaryNormals();
        auto* Colors = Attributes->PrimaryColors();
        auto* UVs = Attributes->GetUVLayer(0);
        auto* MaterialIDs = Attributes->GetMaterialID();

        const auto ElementsPerFace = GridSide * GridSide;
        auto NormalIDs = TArray<int32>{};
        auto ColorIDs = TArray<int32>{};
        auto UVIDs = TArray<int32>{};
        NormalIDs.Reserve(FaceCount * ElementsPerFace);
        ColorIDs.Reserve(FaceCount * ElementsPerFace);
        UVIDs.Reserve(FaceCount * ElementsPerFace);

        for (auto FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
        {
            const auto& Face = BoxFaces[FaceIndex];
            const auto Normal = FVector3f{Face.Normal[0], Face.Normal[1], Face.Normal[2]};
            const auto Red = 0.2f + 0.1f * static_cast<float>(FaceIndex);
            for (auto V = 0; V < GridSide; ++V)
            {
                for (auto U = 0; U < GridSide; ++U)
                {
                    const auto UNormalized = static_cast<float>(U) / static_cast<float>(InSubdivisions);
                    const auto VNormalized = static_cast<float>(V) / static_cast<float>(InSubdivisions);
                    NormalIDs.Add(Normals->AppendElement(Normal));
                    ColorIDs.Add(Colors->AppendElement(FVector4f{Red, UNormalized, VNormalized, 1.0f}));
                    UVIDs.Add(UVs->AppendElement(FVector2f{UNormalized, VNormalized}));
                }
            }
        }

        for (const auto& Triangle : Triangles)
        {
            const auto Base = Triangle.Face * ElementsPerFace;
            const auto& Corners = Triangle.Corners;
            Normals->SetTriangle(Triangle.TriangleID, UE::Geometry::FIndex3i{
                NormalIDs[Base + Corners.A], NormalIDs[Base + Corners.B], NormalIDs[Base + Corners.C]});
            Colors->SetTriangle(Triangle.TriangleID, UE::Geometry::FIndex3i{
                ColorIDs[Base + Corners.A], ColorIDs[Base + Corners.B], ColorIDs[Base + Corners.C]});
            UVs->SetTriangle(Triangle.TriangleID, UE::Geometry::FIndex3i{
                UVIDs[Base + Corners.A], UVIDs[Base + Corners.B], UVIDs[Base + Corners.C]});
            MaterialIDs->SetValue(Triangle.TriangleID, Triangle.Face < FaceCount / 2 ? 0 : 1);
        }

        return Mesh;
    }

    auto
        MakeCentreCut()
        -> Slice::FCutOptions
    {
        const auto HalfExtentCm = BoxExtentCm * 0.5;
        auto Plane = Slice::FPlaneFrameLocal{};
        Plane.Set_PositionCm(FVector3d{HalfExtentCm, HalfExtentCm, HalfExtentCm});
        Plane.Set_Normal(FVector3d{1.0, 0.31, 0.17});
        Plane.Set_Tangent(FVector3d{0.0, 1.0, 0.0});
        auto Cap = Slice::FCapOptions{};
        Cap.Set_MaterialID(CapMaterialID);
        auto Options = Slice::FCutOptions{};
        Options.Set_Plane(Plane);
        Options.Set_Cap(Cap);
        return Options;
    }

    auto
        AllowedVolumeError(
            double InSourceVolumeCm3)
        -> double
    {
        const auto Limits = Slice::FCutLimits{};
        return FMath::Max(Limits.Get_AbsoluteVolumeToleranceCm3(),
            Limits.Get_RelativeVolumeTolerance() * InSourceVolumeCm3);
    }

    auto
        IsConservedHalving(
            double InSourceVolumeCm3,
            double InPositiveVolumeCm3,
            double InNegativeVolumeCm3)
        -> bool
    {
        const auto AllowedError = AllowedVolumeError(InSourceVolumeCm3);
        const auto HalfVolumeCm3 = InSourceVolumeCm3 * 0.5;
        return FMath::Abs(InPositiveVolumeCm3 + InNegativeVolumeCm3 - InSourceVolumeCm3) <= AllowedError
            && FMath::Abs(InPositiveVolumeCm3 - HalfVolumeCm3) <= AllowedError
            && FMath::Abs(InNegativeVolumeCm3 - HalfVolumeCm3) <= AllowedError;
    }

    template <typename T_Operation>
    auto
        Measure(
            const FString& InScopeName,
            T_Operation&& InOperation)
        -> FOperationSample
    {
        const auto UsedBefore = FPlatformMemory::GetStats().UsedPhysical;
        auto Milliseconds = 0.0;
        {
            TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*InScopeName);
            const auto Start = FPlatformTime::Seconds();
            InOperation();
            Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
        }
        const auto UsedAfter = FPlatformMemory::GetStats().UsedPhysical;
        return FOperationSample{Milliseconds,
            static_cast<double>(static_cast<int64>(UsedAfter) - static_cast<int64>(UsedBefore))};
    }

    auto
        Record(
            FOperationSeries& InSeries,
            const FOperationSample& InSample,
            int32 InIteration)
        -> void
    {
        if (InIteration < WarmupIterations)
        { return; }

        InSeries.Milliseconds.Add(InSample.Milliseconds);
        InSeries.UsedPhysicalDeltaBytes.Add(InSample.UsedPhysicalDeltaBytes);
    }

    // --------------------------------------------------------------------------------------------------------------------

    auto
        RunWorkload(
            FAutomationTestBase& InTest,
            const FWorkload& InWorkload)
        -> TOptional<FWorkloadReport>
    {
        const auto Prototype = MakeTessellatedBox(InWorkload.Subdivisions);
        const auto ExpectedVertices = ExpectedVertexCount(InWorkload.Subdivisions);
        const auto ExpectedTriangles = ExpectedTriangleCount(InWorkload.Subdivisions);
        if (Prototype.VertexCount() != ExpectedVertices || Prototype.TriangleCount() != ExpectedTriangles)
        {
            InTest.AddError(FString::Printf(TEXT("%s builder produced %d vertices / %d triangles, expected %d / %d"),
                InWorkload.Name, Prototype.VertexCount(), Prototype.TriangleCount(), ExpectedVertices, ExpectedTriangles));
            return {};
        }

        const auto AdmitScope = FString::Printf(TEXT("CkRuntimeMesh.Benchmark.%s.Admit"), InWorkload.Name);
        const auto CutScope = FString::Printf(TEXT("CkRuntimeMesh.Benchmark.%s.Cut"), InWorkload.Name);
        const auto ImportOptions = Geometry::FImportOptions{};
        const auto CutOptions = MakeCentreCut();
        const auto AnalyticVolumeCm3 = BoxExtentCm * BoxExtentCm * BoxExtentCm;

        auto Report = FWorkloadReport{};
        Report.Name = InWorkload.Name;
        Report.Subdivisions = InWorkload.Subdivisions;

        auto Admitted = TOptional<Geometry::FImportResult>{};
        for (auto Iteration = 0; Iteration < WarmupIterations + MeasuredIterations; ++Iteration)
        {
            auto Copy = UE::Geometry::FDynamicMesh3{Prototype};
            Admitted.Reset();
            const auto Sample = Measure(AdmitScope, [&]() -> void
            {
                Admitted.Emplace(Geometry::Admit(MoveTemp(Copy), ImportOptions));
            });

            const auto& Result = Admitted.GetValue();
            if (NOT Result.Get_IsReady())
            {
                InTest.AddError(FString::Printf(TEXT("%s Admit was not Ready on iteration %d: failure %d"),
                    InWorkload.Name, Iteration, static_cast<int32>(Result.Get_Failure())));
                return {};
            }

            if (FMath::Abs(Result.Get_Geometry()->Get_VolumeCm3() - AnalyticVolumeCm3) > AllowedVolumeError(AnalyticVolumeCm3))
            {
                InTest.AddError(FString::Printf(TEXT("%s admitted volume %.6f cm3 differs from the analytic %.6f cm3"),
                    InWorkload.Name, Result.Get_Geometry()->Get_VolumeCm3(), AnalyticVolumeCm3));
                return {};
            }

            Record(Report.Admit, Sample, Iteration);
        }

        const auto Source = Admitted.GetValue().Get_Geometry();
        Admitted.Reset();
        Report.Source = MakeShape(*Source, 0);

        auto CutResult = TOptional<Slice::FCutResult>{};
        for (auto Iteration = 0; Iteration < WarmupIterations + MeasuredIterations; ++Iteration)
        {
            CutResult.Reset();
            const auto Sample = Measure(CutScope, [&]() -> void
            {
                CutResult.Emplace(Slice::Cut(*Source, CutOptions));
            });

            const auto& Result = CutResult.GetValue();
            if (NOT Result.Get_IsSuccess())
            {
                InTest.AddError(FString::Printf(TEXT("%s centre Cut did not succeed on iteration %d: outcome %d"),
                    InWorkload.Name, Iteration, static_cast<int32>(Result.Get_Outcome())));
                return {};
            }

            const auto PositiveVolumeCm3 = Result.Get_Positive()->Get_VolumeCm3();
            const auto NegativeVolumeCm3 = Result.Get_Negative()->Get_VolumeCm3();
            if (NOT IsConservedHalving(Source->Get_VolumeCm3(), PositiveVolumeCm3, NegativeVolumeCm3))
            {
                InTest.AddError(FString::Printf(
                    TEXT("%s centre Cut broke volume conservation on iteration %d: source %.6f, positive %.6f, negative %.6f cm3"),
                    InWorkload.Name, Iteration, Source->Get_VolumeCm3(), PositiveVolumeCm3, NegativeVolumeCm3));
                return {};
            }

            Record(Report.Cut, Sample, Iteration);
        }

        const auto& LastCut = CutResult.GetValue();
        Report.Positive = MakeShape(*LastCut.Get_Positive(), LastCut.Get_PositiveCapTriangles());
        Report.Negative = MakeShape(*LastCut.Get_Negative(), LastCut.Get_NegativeCapTriangles());
        return Report;
    }

    // --------------------------------------------------------------------------------------------------------------------

    struct FQueueFixture
    {
        UWorld* World = nullptr;
        TStrongObjectPtr<UStaticMesh> Asset;
        FCk_Handle WorldEntity;
        FCk_Handle Owner;
        FCk_Handle ResultOwner;
        FCk_Handle_RuntimeMesh Source;

        auto Initialize() -> bool
        {
            World = UWorld::CreateWorld(EWorldType::Game, InformEngineOfWorld,
                TEXT("RuntimeMeshBenchmarkFixture"));
            if (World == nullptr)
            { return false; }

            World->AddToRoot();
            WorldEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(World);
            Asset.Reset(LoadObject<UStaticMesh>(nullptr, CpuFixturePath));
            if (NOT Asset.IsValid() || ck::Is_NOT_Valid(WorldEntity))
            { return false; }

            Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldEntity);
            ResultOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldEntity);
            auto Spec = FCk_RuntimeMesh_Spec{};
            Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>{Asset.Get()});
            Source = UCk_Utils_RuntimeMesh_UE::Add(Owner, Spec);
            if (ck::Is_NOT_Valid(Source))
            { return false; }

            ck::FProcessor_RuntimeMesh_Setup{WorldEntity.Get_RegistryView()}.ForEachEntity(
                FCk_Time{}, Source, Source.Get<ck::FFragment_RuntimeMesh>(),
                Source.Get<ck::FFragment_RuntimeMesh_PendingImport>());
            return UCk_Utils_RuntimeMesh_UE::Get_SetupState(Source) == ECk_RuntimeMesh_SetupState::Ready;
        }

        ~FQueueFixture()
        {
            if (World != nullptr)
            {
                ck::runtimemesh::CancelWorld(World);
                World->RemoveFromRoot();
                World->DestroyWorld(InformEngineOfWorld);
            }
        }

        auto Drain() -> void
        {
            ck::FProcessor_RuntimeMesh_Drain{WorldEntity.Get_RegistryView()}.DoTick(FCk_Time{1.0f / 60.0f});
        }

        auto PendingCount() -> int32
        {
            if (NOT WorldEntity.Has<ck::FFragment_RuntimeMesh_Queue>())
            { return 0; }

            return WorldEntity.Get<ck::FFragment_RuntimeMesh_Queue>().Get_Pending().Num();
        }
    };

    auto
        MakeQueueRequest(
            const FQueueFixture& InFixture)
        -> FCk_Request_RuntimeMesh_Slice
    {
        const auto HalfExtentCm = BoxExtentCm * 0.5;
        auto Request = FCk_Request_RuntimeMesh_Slice{};
        Request.Set_OperationID(FGuid::NewGuid());
        Request.Set_ResultOwner(InFixture.ResultOwner);
        auto Plane = FCk_RuntimeMesh_PlaneLocal{};
        Plane.Set_PositionCm(FVector{HalfExtentCm, 0.0, 0.0});
        Plane.Set_Normal(FVector::ForwardVector);
        Plane.Set_Tangent(FVector::RightVector);
        Request.Set_Plane(Plane);
        return Request;
    }

    auto
        RunQueue(
            FAutomationTestBase& InTest)
        -> TOptional<FQueueReport>
    {
        auto Fixture = FQueueFixture{};
        if (NOT Fixture.Initialize())
        {
            InTest.AddError(FString::Printf(TEXT("Queue fixture %s did not become Ready"), CpuFixturePath));
            return {};
        }

        const auto SourceMetrics = UCk_Utils_RuntimeMesh_UE::Get_Metrics(Fixture.Source);
        const auto MaxDrainsPerRound = ck::runtimemesh::QueueCapacity;

        auto Report = FQueueReport{};
        Report.SourceTriangles = SourceMetrics.Get_TriangleCount();
        Report.Rounds = MeasuredIterations;

        for (auto Round = 0; Round < WarmupIterations + MeasuredIterations; ++Round)
        {
            auto Listener = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
                NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
            auto Receiver = FCk_Delegate_RuntimeMesh_OnSliceResolved{};
            Receiver.BindDynamic(Listener.Get(), &UCk_Test_RuntimeMeshListener_UE::OnSliceResolved);
            auto Completion = FCk_Delegate_Request_OnCompleted{};
            Completion.BindDynamic(Listener.Get(), &UCk_Test_RuntimeMeshListener_UE::OnRequestCompleted);

            for (auto Index = 0; Index < ck::runtimemesh::QueueCapacity; ++Index)
            {
                const auto Request = MakeQueueRequest(Fixture);
                UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Request, Receiver, Completion);
            }

            if (Listener->Results.Num() != 0 || Fixture.PendingCount() != ck::runtimemesh::QueueCapacity)
            {
                InTest.AddError(FString::Printf(TEXT("Queue round %d: %d synchronous results, %d pending, expected 0 and %d"),
                    Round, Listener->Results.Num(), Fixture.PendingCount(), ck::runtimemesh::QueueCapacity));
                return {};
            }

            auto Drains = 0;
            auto RoundMilliseconds = 0.0;
            auto RoundDrainMilliseconds = TArray<double>{};
            while (Listener->Results.Num() < ck::runtimemesh::QueueCapacity && Drains < MaxDrainsPerRound)
            {
                auto Milliseconds = 0.0;
                {
                    TRACE_CPUPROFILER_EVENT_SCOPE_STR("CkRuntimeMesh.Benchmark.Queue.Drain");
                    const auto Start = FPlatformTime::Seconds();
                    Fixture.Drain();
                    Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
                }
                RoundDrainMilliseconds.Add(Milliseconds);
                RoundMilliseconds += Milliseconds;
                ++Drains;
            }

            if (Listener->Results.Num() != ck::runtimemesh::QueueCapacity
                || Listener->Completions.Num() != ck::runtimemesh::QueueCapacity
                || Fixture.PendingCount() != 0)
            {
                InTest.AddError(FString::Printf(
                    TEXT("Queue round %d did not empty within %d drains: %d results, %d completions, %d pending"),
                    Round, MaxDrainsPerRound, Listener->Results.Num(), Listener->Completions.Num(),
                    Fixture.PendingCount()));
                return {};
            }

            for (const auto& Result : Listener->Results)
            {
                const auto PositiveVolumeCm3 = Result.Get_PositiveMetrics().Get_VolumeCm3();
                const auto NegativeVolumeCm3 = Result.Get_NegativeMetrics().Get_VolumeCm3();
                if (Result.Get_Outcome() != ECk_RuntimeMesh_SliceOutcome::Succeeded
                    || NOT IsConservedHalving(SourceMetrics.Get_VolumeCm3(), PositiveVolumeCm3, NegativeVolumeCm3))
                {
                    InTest.AddError(FString::Printf(
                        TEXT("Queue round %d slice outcome %d: source %.6f, positive %.6f, negative %.6f cm3"),
                        Round, static_cast<int32>(Result.Get_Outcome()), SourceMetrics.Get_VolumeCm3(),
                        PositiveVolumeCm3, NegativeVolumeCm3));
                    return {};
                }
            }

            if (Round < WarmupIterations)
            { continue; }

            Report.DrainMilliseconds.Append(RoundDrainMilliseconds);
            Report.DrainsPerRound.Add(static_cast<double>(Drains));
            Report.EnqueueToEmptyMilliseconds.Add(RoundMilliseconds);
        }

        return Report;
    }

    // --------------------------------------------------------------------------------------------------------------------

    auto
        Emit(
            FAutomationTestBase& InTest,
            const FString& InLine)
        -> void
    {
        InTest.AddInfo(InLine);
        UE_LOG(LogTemp, Display, TEXT("[%s] %s"), BenchmarkName, *InLine);
    }

    auto
        FormatValue(
            double InValue,
            ESummaryUnit InUnit)
        -> FString
    {
        switch (InUnit)
        {
            case ESummaryUnit::Milliseconds:
            {
                return FString::Printf(TEXT("%.4f"), InValue);
            }
            case ESummaryUnit::SignedBytes:
            {
                return FString::Printf(TEXT("%+.0f"), InValue);
            }
            case ESummaryUnit::Count:
            default:
            {
                return FString::Printf(TEXT("%.0f"), InValue);
            }
        }
    }

    auto
        EmitSummary(
            FAutomationTestBase& InTest,
            const FString& InLabel,
            const TArray<double>& InSamples,
            ESummaryUnit InUnit)
        -> void
    {
        const auto Summary = Summarize(InSamples);
        Emit(InTest, FString::Printf(TEXT("%s: min %s median %s max %s"), *InLabel,
            *FormatValue(Summary.Min, InUnit), *FormatValue(Summary.Median, InUnit), *FormatValue(Summary.Max, InUnit)));
    }

    auto
        EmitWorkload(
            FAutomationTestBase& InTest,
            const FWorkloadReport& InReport)
        -> void
    {
        const auto ImportOptions = Geometry::FImportOptions{};
        const auto& Name = InReport.Name;
        Emit(InTest, FString::Printf(TEXT("%s source: %d vertices, %d triangles (ceilings %d / %d), %.3f cm3"),
            *Name, InReport.Source.Vertices, InReport.Source.Triangles, ImportOptions.Get_MaximumVertices(),
            ImportOptions.Get_MaximumTriangles(), InReport.Source.VolumeCm3));
        EmitSummary(InTest, Name + TEXT(" Admit ms"), InReport.Admit.Milliseconds, ESummaryUnit::Milliseconds);
        EmitSummary(InTest, Name + TEXT(" Cut ms"), InReport.Cut.Milliseconds, ESummaryUnit::Milliseconds);
        EmitSummary(InTest, Name + TEXT(" Admit UsedPhysical delta bytes (process-wide sample, not a peak)"),
            InReport.Admit.UsedPhysicalDeltaBytes, ESummaryUnit::SignedBytes);
        EmitSummary(InTest, Name + TEXT(" Cut UsedPhysical delta bytes (process-wide sample, not a peak)"),
            InReport.Cut.UsedPhysicalDeltaBytes, ESummaryUnit::SignedBytes);
        Emit(InTest, FString::Printf(TEXT("%s owned mesh bytes (estimate): source %lld, cut positive %lld + negative %lld"),
            *Name, InReport.Source.OwnedBytesEstimate, InReport.Positive.OwnedBytesEstimate,
            InReport.Negative.OwnedBytesEstimate));
        Emit(InTest, FString::Printf(TEXT("%s cut positive: %d vertices, %d triangles, %d cap triangles, %.3f cm3"),
            *Name, InReport.Positive.Vertices, InReport.Positive.Triangles, InReport.Positive.CapTriangles,
            InReport.Positive.VolumeCm3));
        Emit(InTest, FString::Printf(TEXT("%s cut negative: %d vertices, %d triangles, %d cap triangles, %.3f cm3"),
            *Name, InReport.Negative.Vertices, InReport.Negative.Triangles, InReport.Negative.CapTriangles,
            InReport.Negative.VolumeCm3));
    }

    auto
        EmitQueue(
            FAutomationTestBase& InTest,
            const FQueueReport& InReport)
        -> void
    {
        Emit(InTest, FString::Printf(TEXT("Queue source %s: %d triangles; %d requests per round, budget %d per drain, %d rounds"),
            CpuFixturePath, InReport.SourceTriangles, ck::runtimemesh::QueueCapacity,
            ck::runtimemesh::DrainBudgetPerTick, InReport.Rounds));
        EmitSummary(InTest, TEXT("Queue drains until empty"), InReport.DrainsPerRound, ESummaryUnit::Count);
        EmitSummary(InTest, TEXT("Queue single drain ms"), InReport.DrainMilliseconds, ESummaryUnit::Milliseconds);
        Emit(InTest, FString::Printf(TEXT("Queue worst single drain ms: %.4f"),
            Summarize(InReport.DrainMilliseconds).Max));
        EmitSummary(InTest, TEXT("Queue enqueue-to-empty ms (last request's drain latency)"),
            InReport.EnqueueToEmptyMilliseconds, ESummaryUnit::Milliseconds);
    }

    // --------------------------------------------------------------------------------------------------------------------

    auto
        ToJson(
            const TArray<double>& InSamples)
        -> TSharedPtr<FJsonObject>
    {
        const auto Summary = Summarize(InSamples);
        auto Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("min"), Summary.Min);
        Object->SetNumberField(TEXT("median"), Summary.Median);
        Object->SetNumberField(TEXT("max"), Summary.Max);
        auto Samples = TArray<TSharedPtr<FJsonValue>>{};
        for (const auto Sample : InSamples)
        { Samples.Add(MakeShared<FJsonValueNumber>(Sample)); }
        Object->SetArrayField(TEXT("samples"), Samples);
        return Object;
    }

    auto
        ToJson(
            const FMeshShape& InShape)
        -> TSharedPtr<FJsonObject>
    {
        auto Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("vertices"), InShape.Vertices);
        Object->SetNumberField(TEXT("triangles"), InShape.Triangles);
        Object->SetNumberField(TEXT("capTriangles"), InShape.CapTriangles);
        Object->SetNumberField(TEXT("volumeCm3"), InShape.VolumeCm3);
        Object->SetNumberField(TEXT("ownedBytesEstimate"), static_cast<double>(InShape.OwnedBytesEstimate));
        return Object;
    }

    auto
        ToJson(
            const FWorkloadReport& InReport)
        -> TSharedPtr<FJsonObject>
    {
        auto Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("name"), InReport.Name);
        Object->SetNumberField(TEXT("subdivisionsPerEdge"), InReport.Subdivisions);
        Object->SetObjectField(TEXT("source"), ToJson(InReport.Source));
        Object->SetObjectField(TEXT("cutPositive"), ToJson(InReport.Positive));
        Object->SetObjectField(TEXT("cutNegative"), ToJson(InReport.Negative));
        Object->SetObjectField(TEXT("admitMs"), ToJson(InReport.Admit.Milliseconds));
        Object->SetObjectField(TEXT("cutMs"), ToJson(InReport.Cut.Milliseconds));
        Object->SetObjectField(TEXT("admitUsedPhysicalDeltaBytes"), ToJson(InReport.Admit.UsedPhysicalDeltaBytes));
        Object->SetObjectField(TEXT("cutUsedPhysicalDeltaBytes"), ToJson(InReport.Cut.UsedPhysicalDeltaBytes));
        return Object;
    }

    auto
        ToJson(
            const FQueueReport& InReport)
        -> TSharedPtr<FJsonObject>
    {
        auto Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("source"), CpuFixturePath);
        Object->SetNumberField(TEXT("sourceTriangles"), InReport.SourceTriangles);
        Object->SetNumberField(TEXT("requestsPerRound"), ck::runtimemesh::QueueCapacity);
        Object->SetNumberField(TEXT("rounds"), InReport.Rounds);
        Object->SetObjectField(TEXT("drainsUntilEmpty"), ToJson(InReport.DrainsPerRound));
        Object->SetObjectField(TEXT("singleDrainMs"), ToJson(InReport.DrainMilliseconds));
        Object->SetNumberField(TEXT("worstSingleDrainMs"), Summarize(InReport.DrainMilliseconds).Max);
        Object->SetObjectField(TEXT("enqueueToEmptyMs"), ToJson(InReport.EnqueueToEmptyMilliseconds));
        return Object;
    }

    auto
        BuildReport(
            const FString& InRunID,
            const TArray<FWorkloadReport>& InWorkloads,
            const TOptional<FQueueReport>& InQueue)
        -> TSharedPtr<FJsonObject>
    {
        const auto ImportOptions = Geometry::FImportOptions{};
        const auto CutLimits = Slice::FCutLimits{};

        auto Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("test"), BenchmarkName);
        Root->SetStringField(TEXT("runId"), InRunID);
        Root->SetStringField(TEXT("utc"), FDateTime::UtcNow().ToIso8601());

        auto Machine = MakeShared<FJsonObject>();
        Machine->SetStringField(TEXT("cpuBrand"), FPlatformMisc::GetCPUBrand());
        Machine->SetNumberField(TEXT("secondsPerCycle"), FPlatformTime::GetSecondsPerCycle());
        Root->SetObjectField(TEXT("machine"), Machine);

        auto Method = MakeShared<FJsonObject>();
        Method->SetNumberField(TEXT("warmupIterations"), WarmupIterations);
        Method->SetNumberField(TEXT("measuredIterations"), MeasuredIterations);
        Method->SetStringField(TEXT("clock"), TEXT("FPlatformTime::Seconds wall time around the native call only"));
        Method->SetStringField(TEXT("workloads"),
            TEXT("closed axis-aligned 10 cm boxes, each face an NxN grid of unit-square cells split into two triangles, shared vertices, per-face normal/colour/UV elements, material IDs 0/1"));
        Method->SetStringField(TEXT("cutPlane"),
            TEXT("through the box centre, normal (1, 0.31, 0.17), tangent +Y, cap material 0; tilted so it crosses cells instead of running along grid edges"));
        Method->SetStringField(TEXT("usedPhysicalDelta"),
            TEXT("FPlatformMemory::GetStats().UsedPhysical after minus before each operation, result still alive; process-wide working-set sample, not a peak and not exclusive to the operation"));
        Method->SetStringField(TEXT("ownedBytesEstimate"),
            TEXT("estimate: per vertex FVector3d + uint16 refcount + 11-int edge-list block; per triangle 2 FIndex3i + uint16 + int32 material; per edge 2 FIndex2i + uint16; per overlay element floats + int32 parent + uint16 refcount, plus 3 int32 per triangle; excludes container slack and headers"));
        Method->SetStringField(TEXT("trace"),
            TEXT("each timed operation runs inside TRACE_CPUPROFILER_EVENT_SCOPE CkRuntimeMesh.Benchmark.<Workload>.<Op>; queue drains inside CkRuntimeMesh.Benchmark.Queue.Drain; run with -trace=cpu,memory for allocation attribution"));
        Root->SetObjectField(TEXT("method"), Method);

        auto Ceilings = MakeShared<FJsonObject>();
        Ceilings->SetNumberField(TEXT("importMaximumVertices"), ImportOptions.Get_MaximumVertices());
        Ceilings->SetNumberField(TEXT("importMaximumTriangles"), ImportOptions.Get_MaximumTriangles());
        Ceilings->SetNumberField(TEXT("cutMaximumVertices"), CutLimits.Get_MaximumVertices());
        Ceilings->SetNumberField(TEXT("cutMaximumTriangles"), CutLimits.Get_MaximumTriangles());
        Ceilings->SetNumberField(TEXT("queueCapacity"), ck::runtimemesh::QueueCapacity);
        Ceilings->SetNumberField(TEXT("drainBudgetPerTick"), ck::runtimemesh::DrainBudgetPerTick);
        Root->SetObjectField(TEXT("ceilings"), Ceilings);

        auto WorkloadValues = TArray<TSharedPtr<FJsonValue>>{};
        for (const auto& Workload : InWorkloads)
        { WorkloadValues.Add(MakeShared<FJsonValueObject>(ToJson(Workload))); }

        Root->SetArrayField(TEXT("workloads"), WorkloadValues);

        if (InQueue.IsSet())
        { Root->SetObjectField(TEXT("queue"), ToJson(InQueue.GetValue())); }

        return Root;
    }

    auto
        WriteReport(
            const FString& InRunID,
            const TSharedPtr<FJsonObject>& InRoot)
        -> TOptional<FString>
    {
        auto Serialized = FString{};
        const auto Writer = TJsonWriterFactory<>::Create(&Serialized);
        if (NOT FJsonSerializer::Serialize(InRoot, Writer))
        { return {}; }

        const auto Directory = FPaths::ProjectSavedDir() / TEXT("Automation/RuntimeMesh/Benchmark");
        const auto Path = FPaths::ConvertRelativePathToFull(Directory / (InRunID + TEXT(".json")));
        if (NOT FFileHelper::SaveStringToFile(Serialized, *Path))
        { return {}; }

        return Path;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_Benchmark_BoundedWorkloads,
    "Ck.RuntimeMesh.Benchmark.BoundedWorkloads",
    ck_test_runtime_mesh_benchmark::BenchmarkTestFlags)

bool FCkTest_RuntimeMesh_Benchmark_BoundedWorkloads::RunTest(const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_benchmark;

    auto Reports = TArray<FWorkloadReport>{};
    auto AllWorkloadsProduced = true;
    for (const auto& Workload : Workloads)
    {
        auto Report = RunWorkload(*this, Workload);
        if (NOT Report.IsSet())
        {
            AllWorkloadsProduced = false;
            continue;
        }

        EmitWorkload(*this, Report.GetValue());
        Reports.Add(MoveTemp(Report.GetValue()));
    }

    const auto Queue = RunQueue(*this);
    if (Queue.IsSet())
    { EmitQueue(*this, Queue.GetValue()); }

    const auto RunID = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    const auto Path = WriteReport(RunID, BuildReport(RunID, Reports, Queue));
    if (Path.IsSet())
    {
        AddInfo(FString::Printf(TEXT("Benchmark JSON: %s"), *Path.GetValue()));
        UE_LOG(LogTemp, Display, TEXT("[%s] Benchmark JSON written to %s"), BenchmarkName, *Path.GetValue());
    }
    else
    {
        AddWarning(FString::Printf(TEXT("Benchmark JSON for run %s could not be written"), *RunID));
        UE_LOG(LogTemp, Warning, TEXT("[%s] Benchmark JSON for run %s could not be written"), BenchmarkName, *RunID);
    }

    return AllWorkloadsProduced && Queue.IsSet();
}

// --------------------------------------------------------------------------------------------------------------------

#endif
