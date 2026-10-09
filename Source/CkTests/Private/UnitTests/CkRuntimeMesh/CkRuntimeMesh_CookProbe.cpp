#include "CkRuntimeMesh_CookProbe.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CkRuntimeMesh/CkRuntimeMesh_Utils.h"
#include "CkRuntimeMesh/Display/CkRuntimeMeshDisplay_Utils.h"
#include "CkRuntimeMesh/Internal/CkRuntimeMesh_Geometry.h"
#include "CkRuntimeMesh/Internal/CkRuntimeMesh_Slice.h"

#include "CkCore/Format/CkFormat.h"
#include "CkCore/Macros/CkMacros.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"

#include "Containers/Ticker.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/OutputDevice.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "StaticMeshResources.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_runtime_mesh_cook_probe
{
    constexpr TCHAR CpuPath[] = TEXT("/CkTests/CkRuntimeMesh/Cooked/SM_Import_CPU.SM_Import_CPU");
    constexpr TCHAR NoCpuPath[] = TEXT("/CkTests/CkRuntimeMesh/Cooked/SM_Import_NoCPU.SM_Import_NoCPU");
    constexpr TCHAR BootMapPackage[] = TEXT("/Engine/Maps/Entry");
    constexpr TCHAR BootMapArgument[] = TEXT(" -map=/Engine/Maps/Entry?game=/Script/Engine.GameModeBase");
    constexpr auto WorldDeadlineSeconds = 90.0;
    constexpr auto PublicPathDeadlineSeconds = 60.0;
    constexpr auto VolumeToleranceCm3 = 0.01;
    constexpr int32 Faces[12][3] = {
        {0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
        {0, 5, 1}, {0, 4, 5}, {3, 6, 7}, {3, 2, 6},
        {0, 7, 4}, {0, 3, 7}, {1, 6, 2}, {1, 5, 6}};
    const FVector3f Corners[8] = {
        {0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0},
        {0, 0, 10}, {10, 0, 10}, {10, 10, 10}, {0, 10, 10}};

    enum class EPhase : uint8
    {
        AwaitWorld,
        AwaitSource,
        AwaitSlice,
        AwaitComposition
    };

    struct FProbe
    {
        EPhase Phase = EPhase::AwaitWorld;
        double Deadline = 0.0;
        TWeakObjectPtr<UWorld> World;
        FCk_Handle ResultOwner;
        FCk_Handle_RuntimeMesh Source;
        FGuid OperationID;
        FCk_Handle_RuntimeMeshDisplay Display;
        FCk_Handle_JoltBody Body;
        TStrongObjectPtr<UCk_Test_RuntimeMeshCookProbeListener_UE> Listener;
    };

    FDelegateHandle DelegateHandle;
    FTSTicker::FDelegateHandle TickerHandle;
    TUniquePtr<FProbe> Probe;

    auto LogStep(
        const TCHAR* InStep,
        bool InPassed,
        const FString& InDiagnostic) -> bool
    {
        UE_LOG(LogTemp, Display, TEXT("CK_RUNTIME_MESH_COOK_PROBE_STEP %s %s %s"),
            InStep, InPassed ? TEXT("PASS") : TEXT("FAIL"), *InDiagnostic);
        return InPassed;
    }

    auto Finish(
        bool InPassed) -> void
    {
        if (TickerHandle.IsValid())
        {
            FTSTicker::RemoveTicker(TickerHandle);
            TickerHandle.Reset();
        }

        UE_LOG(LogTemp, Display, TEXT("CK_RUNTIME_MESH_COOK_PROBE %s"), InPassed ? TEXT("PASS") : TEXT("FAIL"));
        if (GLog != nullptr)
        { GLog->Flush(); }
        FPlatformMisc::RequestExitWithStatus(true, InPassed ? 0 : 2, TEXT("CkRuntimeMeshCookProbe"));
    }

    auto Fail(
        const TCHAR* InStep,
        const FString& InDiagnostic) -> bool
    {
        LogStep(InStep, false, InDiagnostic);
        Finish(false);
        return false;
    }

    auto KeepWaiting(
        double InNow,
        const TCHAR* InStep,
        const FString& InPending) -> bool
    {
        if (InNow < Probe->Deadline)
        { return true; }

        return Fail(InStep, ck::Format_UE(TEXT("deadline passed while waiting: {}"), InPending));
    }

    auto IsNearVolume(
        double InVolumeCm3,
        double InExpectedCm3) -> bool
    {
        return FMath::IsNearlyEqual(InVolumeCm3, InExpectedCm3, VolumeToleranceCm3);
    }

    auto FindCorner(const FVector3d& InPosition) -> int32
    {
        for (int32 Index = 0; Index < 8; ++Index)
        {
            if (InPosition.Equals(FVector3d(Corners[Index]), 0.01))
            { return Index; }
        }
        return INDEX_NONE;
    }

    auto FindFace(const int32 InCornerIDs[3]) -> int32
    {
        for (int32 FaceIndex = 0; FaceIndex < 12; ++FaceIndex)
        {
            bool Matches = true;
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                Matches &= InCornerIDs[Corner] == Faces[FaceIndex][0]
                    || InCornerIDs[Corner] == Faces[FaceIndex][1]
                    || InCornerIDs[Corner] == Faces[FaceIndex][2];
            }
            if (Matches)
            { return FaceIndex; }
        }
        return INDEX_NONE;
    }

    auto DescribeRenderLOD(const UStaticMesh& InMesh) -> FString
    {
        const auto* RenderData = InMesh.GetRenderData();
        if (RenderData == nullptr)
        { return TEXT("no render data"); }
        if (RenderData->LODResources.Num() != 1)
        { return ck::Format_UE(TEXT("{} LODs"), RenderData->LODResources.Num()); }
        const auto& LOD = RenderData->LODResources[0];
        const auto& Buffers = LOD.VertexBuffers;
        return ck::Format_UE(
            TEXT("cpuFlag={} tris={} sections={} uv={} colors={} positions={} indexBytes={} indexCpu={} posCpu={} attrCpu={} colorCpu={} firstLOD={}"),
            static_cast<bool>(InMesh.bAllowCPUAccess), LOD.GetNumTriangles(), LOD.Sections.Num(),
            Buffers.StaticMeshVertexBuffer.GetNumTexCoords(), Buffers.ColorVertexBuffer.GetNumVertices(),
            Buffers.PositionVertexBuffer.GetNumVertices(), LOD.IndexBuffer.GetIndexDataSize(),
            LOD.IndexBuffer.GetAllowCPUAccess(), Buffers.PositionVertexBuffer.GetAllowCPUAccess(),
            Buffers.StaticMeshVertexBuffer.GetAllowCPUAccess(), Buffers.ColorVertexBuffer.GetAllowCPUAccess(),
            RenderData->CurrentFirstLODIdx);
    }

    // The CPU-readable fixture must still carry the authored cube in its cooked render LOD; the non-CPU
    // fixture only has to load with its flag intact, since stripped buffers are exactly what it proves.
    auto HasExpectedRenderLOD(const UStaticMesh& InMesh) -> bool
    {
        const auto* RenderData = InMesh.GetRenderData();
        if (RenderData == nullptr || RenderData->LODResources.Num() != 1)
        { return false; }
        const auto& LOD = RenderData->LODResources[0];
        return LOD.GetNumTriangles() == 12
            && LOD.Sections.Num() == 2
            && LOD.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() == 2
            && LOD.VertexBuffers.ColorVertexBuffer.GetNumVertices() > 0
            && LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices() >= 8;
    }

    // Empty when every imported channel matches the authored cube; otherwise the first mismatch.
    auto DoGet_ChannelMismatch(const ck::runtimemesh::geometry::FValidatedGeometry& InGeometry) -> FString
    {
        const auto& Mesh = InGeometry.Get_Mesh();
        const auto* Attributes = Mesh.Attributes();
        if (Mesh.VertexCount() != 8 || Mesh.TriangleCount() != 12)
        { return ck::Format_UE(TEXT("topology is {} vertices / {} triangles"), Mesh.VertexCount(), Mesh.TriangleCount()); }
        if (NOT FMath::IsNearlyEqual(InGeometry.Get_VolumeCm3(), 1000.0, 0.01)
            || NOT InGeometry.Get_CentroidCm().Equals(FVector3d{5, 5, 5}, 0.01))
        { return ck::Format_UE(TEXT("volume {} cm3 or centroid is off"), InGeometry.Get_VolumeCm3()); }
        if (Attributes == nullptr || Attributes->NumUVLayers() != 2
            || Attributes->PrimaryNormals() == nullptr || Attributes->PrimaryColors() == nullptr
            || Attributes->GetUVLayer(0) == nullptr || Attributes->GetUVLayer(1) == nullptr
            || Attributes->GetMaterialID() == nullptr)
        { return TEXT("an attribute channel is missing"); }

        int32 MaterialCounts[2] = {};
        bool SeenFaces[12] = {};
        for (const int32 TriangleID : Mesh.TriangleIndicesItr())
        {
            const auto Vertices = Mesh.GetTriangle(TriangleID);
            int32 CornerIDs[3] = {};
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                CornerIDs[Corner] = FindCorner(Mesh.GetVertex(Vertices[Corner]));
                if (CornerIDs[Corner] == INDEX_NONE)
                { return ck::Format_UE(TEXT("triangle {} has a vertex off the cube corners"), TriangleID); }
            }
            const int32 FaceIndex = FindFace(CornerIDs);
            if (FaceIndex == INDEX_NONE || SeenFaces[FaceIndex])
            { return ck::Format_UE(TEXT("triangle {} matches no unseen authored face"), TriangleID); }
            SeenFaces[FaceIndex] = true;
            const int32 Material = Attributes->GetMaterialID()->GetValue(TriangleID);
            if (Material != (FaceIndex < 6 ? 0 : 1))
            { return ck::Format_UE(TEXT("triangle {} has material ID {}"), TriangleID, Material); }
            ++MaterialCounts[Material];
            const auto ExpectedNormal = FVector3f::CrossProduct(
                Corners[Faces[FaceIndex][2]] - Corners[Faces[FaceIndex][0]],
                Corners[Faces[FaceIndex][1]] - Corners[Faces[FaceIndex][0]]).GetSafeNormal();
            const auto NormalIDs = Attributes->PrimaryNormals()->GetTriangle(TriangleID);
            const auto ColorIDs = Attributes->PrimaryColors()->GetTriangle(TriangleID);
            const auto UV0IDs = Attributes->GetUVLayer(0)->GetTriangle(TriangleID);
            const auto UV1IDs = Attributes->GetUVLayer(1)->GetTriangle(TriangleID);
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                int32 AuthoredCorner = INDEX_NONE;
                for (int32 Index = 0; Index < 3; ++Index)
                {
                    if (Faces[FaceIndex][Index] == CornerIDs[Corner])
                    { AuthoredCorner = Index; }
                }
                if (AuthoredCorner == INDEX_NONE)
                { return ck::Format_UE(TEXT("triangle {} lost its authored corner order"), TriangleID); }
                const auto ExpectedUV0 = FVector2f{static_cast<float>(FaceIndex) / 16.0f,
                    static_cast<float>(AuthoredCorner) / 4.0f};
                const auto ExpectedUV1 = FVector2f{static_cast<float>(FaceIndex) / 32.0f,
                    static_cast<float>(AuthoredCorner) / 8.0f};
                if (NOT Attributes->PrimaryNormals()->GetElement(NormalIDs[Corner]).Equals(ExpectedNormal, 0.03f)
                    || NOT Attributes->GetUVLayer(0)->GetElement(UV0IDs[Corner]).Equals(ExpectedUV0, 0.003f)
                    || NOT Attributes->GetUVLayer(1)->GetElement(UV1IDs[Corner]).Equals(ExpectedUV1, 0.003f))
                { return ck::Format_UE(TEXT("triangle {} has a normal or UV off its authored value"), TriangleID); }
                const auto Color = Attributes->PrimaryColors()->GetElement(ColorIDs[Corner]);
                const auto SourceColor = FLinearColor{static_cast<float>(30 + 20 * CornerIDs[Corner]) / 255.0f,
                    80.0f / 255.0f, 160.0f / 255.0f, 1.0f}.ToFColor(true).ReinterpretAsLinear();
                if (NOT Color.Equals(FVector4f{SourceColor.R, SourceColor.G, SourceColor.B, SourceColor.A}, 0.02f))
                { return ck::Format_UE(TEXT("triangle {} has a vertex colour off its authored value"), TriangleID); }
            }
        }
        if (MaterialCounts[0] != 6 || MaterialCounts[1] != 6)
        { return ck::Format_UE(TEXT("material split is {}/{}"), MaterialCounts[0], MaterialCounts[1]); }

        return {};
    }

    auto DoCheck_NativeImport(ck::runtimemesh::geometry::FValidatedGeometryPtr& OutGeometry) -> bool
    {
        auto* Cpu = LoadObject<UStaticMesh>(nullptr, CpuPath);
        auto* NoCpu = LoadObject<UStaticMesh>(nullptr, NoCpuPath);
        if (Cpu == nullptr || NoCpu == nullptr)
        { return LogStep(TEXT("NativeImport"), false, TEXT("a cooked fixture did not load")); }
        if (NOT Cpu->bAllowCPUAccess || NoCpu->bAllowCPUAccess)
        { return LogStep(TEXT("NativeImport"), false, TEXT("the fixtures' CPU-access flags are not as authored")); }
        const auto Layout = ck::Format_UE(TEXT("CPU[{}] NoCPU[{}]"), DescribeRenderLOD(*Cpu), DescribeRenderLOD(*NoCpu));
        if (NOT HasExpectedRenderLOD(*Cpu))
        { return LogStep(TEXT("NativeImport"), false, ck::Format_UE(TEXT("the CPU fixture's cooked render LOD is not the authored cube: {}"), Layout)); }

        const auto Positive = ck::runtimemesh::geometry::Import(*Cpu, {});
        const auto Negative = ck::runtimemesh::geometry::Import(*NoCpu, {});
        if (NOT Positive.Get_IsReady() || NOT Positive.Get_Geometry().IsValid())
        {
            return LogStep(TEXT("NativeImport"), false, ck::Format_UE(
                TEXT("the CPU-readable fixture did not import Ready (failure {}): {}"),
                static_cast<int32>(Positive.Get_Failure()), Layout));
        }
        if (Negative.Get_Failure() != ck::runtimemesh::geometry::EImportFailure::CpuDataUnavailable
            || Negative.Get_Geometry().IsValid())
        {
            return LogStep(TEXT("NativeImport"), false, ck::Format_UE(
                TEXT("the non-CPU fixture did not fail CpuDataUnavailable (failure {}): {}"),
                static_cast<int32>(Negative.Get_Failure()), Layout));
        }

        const auto Mismatch = DoGet_ChannelMismatch(*Positive.Get_Geometry());
        if (NOT Mismatch.IsEmpty())
        { return LogStep(TEXT("NativeImport"), false, Mismatch); }

        OutGeometry = Positive.Get_Geometry();
        return LogStep(TEXT("NativeImport"), true, ck::Format_UE(
            TEXT("CPU fixture Ready with every channel intact; non-CPU fixture failed CpuDataUnavailable: {}"), Layout));
    }

    auto DoCheck_NativeSlice(const ck::runtimemesh::geometry::FValidatedGeometry& InSource) -> bool
    {
        auto Plane = ck::runtimemesh::slice::FPlaneFrameLocal{};
        Plane.Set_PositionCm(FVector3d{5, 0, 0});
        Plane.Set_Normal(FVector3d{1, 0, 0});
        Plane.Set_Tangent(FVector3d{0, 1, 0});
        auto Options = ck::runtimemesh::slice::FCutOptions{};
        Options.Set_Plane(Plane);

        const auto Result = ck::runtimemesh::slice::Cut(InSource, Options);
        if (NOT Result.Get_IsSuccess() || NOT Result.Get_Positive().IsValid() || NOT Result.Get_Negative().IsValid())
        { return LogStep(TEXT("NativeSlice"), false, TEXT("the x=5 cut of the imported fixture did not succeed with two halves")); }

        const auto PositiveVolume = Result.Get_Positive()->Get_VolumeCm3();
        const auto NegativeVolume = Result.Get_Negative()->Get_VolumeCm3();
        const auto Diagnostic = ck::Format_UE(TEXT("halves {:.4f} / {:.4f} cm3"), PositiveVolume, NegativeVolume);
        return LogStep(TEXT("NativeSlice"),
            IsNearVolume(PositiveVolume, 500.0) && IsNearVolume(NegativeVolume, 500.0), Diagnostic);
    }

    auto FindBegunGameWorld() -> UWorld*
    {
        if (GEngine == nullptr)
        { return nullptr; }

        for (const auto& Context : GEngine->GetWorldContexts())
        {
            auto* World = Context.World();
            if (Context.WorldType == EWorldType::Game && World != nullptr
                && World->HasBegunPlay() && NOT World->IsBeingCleanedUp())
            { return World; }
        }

        return nullptr;
    }

    auto DoStart_PublicPath(const FCk_Handle& InWorldEntity) -> bool
    {
        auto SourceEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InWorldEntity);
        Probe->ResultOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InWorldEntity);

        auto Spec = FCk_RuntimeMesh_Spec{};
        Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>{FSoftObjectPath{CpuPath}});
        Probe->Source = UCk_Utils_RuntimeMesh_UE::Add(SourceEntity, Spec);
        if (ck::Is_NOT_Valid(Probe->Source) || ck::Is_NOT_Valid(Probe->ResultOwner))
        { return Fail(TEXT("PublicAdd"), TEXT("Add rejected the soft fixture path or the result owner is invalid")); }

        Probe->Phase = EPhase::AwaitSource;
        return true;
    }

    auto DoTick_AwaitWorld(double InNow) -> bool
    {
        auto* World = FindBegunGameWorld();
        if (World == nullptr)
        { return KeepWaiting(InNow, TEXT("World"), TEXT("no Game world has begun play")); }

        const auto WorldEntity = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (ck::Is_NOT_Valid(WorldEntity))
        {
            return KeepWaiting(InNow, TEXT("World"),
                ck::Format_UE(TEXT("Game world [{}] began play without an ECS transient entity"), World->GetMapName()));
        }

        LogStep(TEXT("World"), true, ck::Format_UE(TEXT("Game world [{}] began play with an ECS world"), World->GetMapName()));
        Probe->World = World;
        Probe->Deadline = InNow + PublicPathDeadlineSeconds;
        return DoStart_PublicPath(WorldEntity);
    }

    auto DoRequest_Slice() -> bool
    {
        auto Plane = FCk_RuntimeMesh_PlaneLocal{};
        Plane.Set_PositionCm(FVector{5, 0, 0});
        Plane.Set_Normal(FVector::ForwardVector);
        Plane.Set_Tangent(FVector::RightVector);
        Probe->OperationID = FGuid::NewGuid();
        auto Request = FCk_Request_RuntimeMesh_Slice{};
        Request.Set_OperationID(Probe->OperationID);
        Request.Set_ResultOwner(Probe->ResultOwner);
        Request.Set_Plane(Plane);

        auto Receiver = FCk_Delegate_RuntimeMesh_OnSliceResolved{};
        Receiver.BindDynamic(Probe->Listener.Get(), &UCk_Test_RuntimeMeshCookProbeListener_UE::OnSliceResolved);
        auto Completion = FCk_Delegate_Request_OnCompleted{};
        Completion.BindDynamic(Probe->Listener.Get(), &UCk_Test_RuntimeMeshCookProbeListener_UE::OnRequestCompleted);
        UCk_Utils_RuntimeMesh_UE::Request_Slice(Probe->Source, Request, Receiver, Completion);

        if (NOT Probe->Listener->SliceResults.IsEmpty())
        {
            return Fail(TEXT("PublicSlice"), ck::Format_UE(TEXT("rejected on submission with outcome [{}]"),
                Probe->Listener->SliceResults[0].Get_Outcome()));
        }

        Probe->Phase = EPhase::AwaitSlice;
        return true;
    }

    auto DoTick_AwaitSource(double InNow) -> bool
    {
        const auto State = UCk_Utils_RuntimeMesh_UE::Get_SetupState(Probe->Source);
        if (State == ECk_RuntimeMesh_SetupState::Pending)
        { return KeepWaiting(InNow, TEXT("PublicAdd"), TEXT("the source is still Pending")); }

        if (State == ECk_RuntimeMesh_SetupState::Failed)
        {
            return Fail(TEXT("PublicAdd"), ck::Format_UE(TEXT("source setup Failed [{}]"),
                UCk_Utils_RuntimeMesh_UE::Get_SetupFailure(Probe->Source)));
        }

        const auto SourceVolume = UCk_Utils_RuntimeMesh_UE::Get_Metrics(Probe->Source).Get_VolumeCm3();
        if (NOT IsNearVolume(SourceVolume, 1000.0))
        { return Fail(TEXT("PublicAdd"), ck::Format_UE(TEXT("source Ready but its volume is {:.4f} cm3"), SourceVolume)); }

        LogStep(TEXT("PublicAdd"), true, ck::Format_UE(TEXT("source Ready through the real processors, {:.4f} cm3"), SourceVolume));
        return DoRequest_Slice();
    }

    // Empty when the slice resolved as specified; otherwise the first deviation.
    auto DoGet_SliceDeviation(const FCk_RuntimeMesh_SliceResult& InResult) -> FString
    {
        const auto& Listener = *Probe->Listener;
        if (Listener.SliceResults.Num() != 1 || Listener.Completions.Num() != 1)
        { return ck::Format_UE(TEXT("{} typed / {} generic terminals instead of one each"), Listener.SliceResults.Num(), Listener.Completions.Num()); }
        if (InResult.Get_OperationID() != Probe->OperationID)
        { return TEXT("the typed result carries another operation ID"); }
        if (InResult.Get_Outcome() != ECk_RuntimeMesh_SliceOutcome::Succeeded)
        { return ck::Format_UE(TEXT("outcome [{}]"), InResult.Get_Outcome()); }
        if (Listener.Completions[0] != ECk_Request_OperationResult::Succeeded)
        { return ck::Format_UE(TEXT("generic completion [{}]"), Listener.Completions[0]); }

        const auto& Positive = InResult.Get_Positive();
        const auto& Negative = InResult.Get_Negative();
        if (ck::Is_NOT_Valid(Positive) || ck::Is_NOT_Valid(Negative))
        { return TEXT("a result handle is invalid"); }
        if (UCk_Utils_RuntimeMesh_UE::Get_SetupState(Positive) != ECk_RuntimeMesh_SetupState::Ready
            || UCk_Utils_RuntimeMesh_UE::Get_SetupState(Negative) != ECk_RuntimeMesh_SetupState::Ready)
        { return TEXT("a result half is not Ready"); }
        if (NOT (UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Positive) == Probe->ResultOwner)
            || NOT (UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Negative) == Probe->ResultOwner))
        { return TEXT("a result half is not owned by the independent result owner"); }

        const auto PositiveVolume = UCk_Utils_RuntimeMesh_UE::Get_Metrics(Positive).Get_VolumeCm3();
        const auto NegativeVolume = UCk_Utils_RuntimeMesh_UE::Get_Metrics(Negative).Get_VolumeCm3();
        if (NOT IsNearVolume(PositiveVolume, 500.0) || NOT IsNearVolume(NegativeVolume, 500.0))
        { return ck::Format_UE(TEXT("Get_Metrics reports {:.4f} / {:.4f} cm3"), PositiveVolume, NegativeVolume); }

        if (UCk_Utils_RuntimeMesh_UE::Copy_LocalVerticesCm(Positive).IsEmpty()
            || UCk_Utils_RuntimeMesh_UE::Copy_LocalVerticesCm(Negative).IsEmpty())
        { return TEXT("Copy_LocalVerticesCm returned no vertices"); }

        return {};
    }

    auto DoStart_Composition(const FCk_RuntimeMesh_SliceResult& InResult) -> bool
    {
        auto PositiveEntity = InResult.Get_Positive().ConvertToHandle();
        auto PositiveTransform = UCk_Utils_Transform_UE::Add(PositiveEntity, FTransform{FVector{0, 0, 100}}, ECk_Replication::DoesNotReplicate);

        const auto DefaultMaterial = TSoftObjectPtr<UMaterialInterface>{UMaterial::GetDefaultMaterial(MD_Surface)};
        auto Visuals = FCk_RuntimeMeshDisplay_Visuals{};
        Visuals.Set_Materials({DefaultMaterial, DefaultMaterial});
        auto DisplaySpec = FCk_RuntimeMeshDisplay_Spec{};
        DisplaySpec.Set_Geometry(InResult.Get_Positive());
        DisplaySpec.Set_Visuals(Visuals);
        Probe->Display = UCk_Utils_RuntimeMeshDisplay_UE::Add(PositiveTransform, DisplaySpec);
        if (ck::Is_NOT_Valid(Probe->Display))
        { return Fail(TEXT("Display"), TEXT("Add rejected the half (see the preceding ensure)")); }

        auto Convex = FCk_JoltBody_RuntimeConvexSpec{};
        Convex.Set_PointsCm(UCk_Utils_RuntimeMesh_UE::Copy_LocalVerticesCm(InResult.Get_Positive()));
        auto BodySpec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::RuntimeConvex};
        BodySpec.Set_RuntimeConvex(Convex);
        BodySpec.Set_MassSource(ECk_JoltBody_MassSource::Explicit);
        BodySpec.Set_MassKg(1.0f);
        Probe->Body = UCk_Utils_JoltBody_UE::Add(PositiveEntity, BodySpec);
        if (ck::Is_NOT_Valid(Probe->Body))
        { return Fail(TEXT("JoltBody"), TEXT("Add rejected the RuntimeConvex spec")); }

        auto OnResolved = FCk_Delegate_JoltBody_OnSetupResolved{};
        OnResolved.BindDynamic(Probe->Listener.Get(), &UCk_Test_RuntimeMeshCookProbeListener_UE::OnJoltSetupResolved);
        if (NOT UCk_Utils_JoltBody_UE::TryPromise_OnSetupResolved(Probe->Body, OnResolved))
        { return Fail(TEXT("JoltBody"), TEXT("TryPromise_OnSetupResolved refused the body")); }

        Probe->Phase = EPhase::AwaitComposition;
        return true;
    }

    auto DoTick_AwaitSlice(double InNow) -> bool
    {
        const auto& Listener = *Probe->Listener;
        if (Listener.SliceResults.IsEmpty() || Listener.Completions.IsEmpty())
        {
            return KeepWaiting(InNow, TEXT("PublicSlice"), ck::Format_UE(TEXT("{} typed / {} generic terminals so far"),
                Listener.SliceResults.Num(), Listener.Completions.Num()));
        }

        const auto Result = Listener.SliceResults[0];
        const auto Deviation = DoGet_SliceDeviation(Result);
        if (NOT Deviation.IsEmpty())
        { return Fail(TEXT("PublicSlice"), Deviation); }

        LogStep(TEXT("PublicSlice"), true, ck::Format_UE(
            TEXT("Succeeded through the real drain; halves {:.4f} / {:.4f} cm3 under an independent owner"),
            UCk_Utils_RuntimeMesh_UE::Get_Metrics(Result.Get_Positive()).Get_VolumeCm3(),
            UCk_Utils_RuntimeMesh_UE::Get_Metrics(Result.Get_Negative()).Get_VolumeCm3()));
        return DoStart_Composition(Result);
    }

    auto DoTick_AwaitComposition(double InNow) -> bool
    {
        const auto& Listener = *Probe->Listener;
        const auto DisplayState = UCk_Utils_RuntimeMeshDisplay_UE::Get_SetupState(Probe->Display);
        if (DisplayState == ECk_RuntimeMesh_SetupState::Pending || Listener.JoltResolutions == 0)
        {
            return KeepWaiting(InNow, TEXT("Composition"), ck::Format_UE(TEXT("display [{}], Jolt resolutions {}"),
                DisplayState, Listener.JoltResolutions));
        }

        const auto DisplayFailure = UCk_Utils_RuntimeMeshDisplay_UE::Get_SetupFailure(Probe->Display);
        const auto DisplayPassed = LogStep(TEXT("Display"),
            DisplayFailure != ECk_RuntimeMeshDisplay_SetupFailure::Cancelled,
            DisplayState == ECk_RuntimeMesh_SetupState::Ready
                ? FString{TEXT("terminal state Ready in the packaged game")}
                : ck::Format_UE(TEXT("terminal state Failed [{}] in the packaged game"), DisplayFailure));

        const auto BodyState = UCk_Utils_JoltBody_UE::Get_SetupState(Probe->Body);
        const auto JoltPassed = LogStep(TEXT("JoltBody"),
            Listener.JoltResolutions == 1
                && Listener.JoltState == ECk_JoltBody_SetupState::Ready
                && Listener.JoltFailure == ECk_JoltBody_SetupFailure::None
                && BodyState == ECk_JoltBody_SetupState::Ready
                && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Probe->Body),
            ck::Format_UE(TEXT("promise [{}] [{}] x{}, getter [{}], diagnostic [{}]"),
                Listener.JoltState, Listener.JoltFailure, Listener.JoltResolutions, BodyState,
                UCk_Utils_JoltBody_UE::Get_SetupDiagnostic(Probe->Body)));

        Finish(DisplayPassed && JoltPassed);
        return false;
    }

    auto Tick(float InDeltaSeconds) -> bool
    {
        if (NOT Probe.IsValid())
        { return false; }

        const auto Now = FPlatformTime::Seconds();
        if (Probe->Phase != EPhase::AwaitWorld)
        {
            const auto* World = Probe->World.Get();
            if (World == nullptr || World->IsBeingCleanedUp())
            { return Fail(TEXT("World"), TEXT("the probe's game world was torn down before the public path resolved")); }
        }

        switch (Probe->Phase)
        {
            case EPhase::AwaitWorld:
            { return DoTick_AwaitWorld(Now); }
            case EPhase::AwaitSource:
            { return DoTick_AwaitSource(Now); }
            case EPhase::AwaitSlice:
            { return DoTick_AwaitSlice(Now); }
            case EPhase::AwaitComposition:
            { return DoTick_AwaitComposition(Now); }
        }

        return Fail(TEXT("Probe"), TEXT("unknown phase"));
    }

    auto DoSelect_BootMap() -> void
    {
        if (NOT FPackageName::DoesPackageExist(FString{BootMapPackage}))
        {
            LogStep(TEXT("BootMap"), true, TEXT("/Engine/Maps/Entry is not cooked; booting the configured default map"));
            return;
        }

        // Appended, so a map the caller named on the command line still wins (UGameInstance::GetMapOverrideName).
        FCommandLine::Append(BootMapArgument);
        LogStep(TEXT("BootMap"), true, TEXT("requested /Engine/Maps/Entry with /Script/Engine.GameModeBase"));
    }

    auto OnPostEngineInit() -> void
    {
        FCoreDelegates::OnPostEngineInit.Remove(DelegateHandle);
        DelegateHandle.Reset();

#if WITH_EDITORONLY_DATA
        LogStep(TEXT("Environment"), false, TEXT("this build carries editor-only data; the probe proves cooked data only"));
        Finish(false);
#else
        auto Geometry = ck::runtimemesh::geometry::FValidatedGeometryPtr{};
        if (NOT DoCheck_NativeImport(Geometry) || NOT DoCheck_NativeSlice(*Geometry))
        {
            Finish(false);
            return;
        }

        DoSelect_BootMap();
        Probe = MakeUnique<FProbe>();
        Probe->Deadline = FPlatformTime::Seconds() + WorldDeadlineSeconds;
        Probe->Listener.Reset(NewObject<UCk_Test_RuntimeMeshCookProbeListener_UE>(GetTransientPackage()));
        TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
#endif
    }
}

auto ck::tests::runtimemesh::StartCookProbeIfRequested() -> void
{
    if (FParse::Param(FCommandLine::Get(), TEXT("CkRuntimeMeshCookProbe")))
    {
        ck_test_runtime_mesh_cook_probe::DelegateHandle =
            FCoreDelegates::OnPostEngineInit.AddStatic(&ck_test_runtime_mesh_cook_probe::OnPostEngineInit);
    }
}

auto ck::tests::runtimemesh::StopCookProbe() -> void
{
    if (ck_test_runtime_mesh_cook_probe::DelegateHandle.IsValid())
    {
        FCoreDelegates::OnPostEngineInit.Remove(ck_test_runtime_mesh_cook_probe::DelegateHandle);
        ck_test_runtime_mesh_cook_probe::DelegateHandle.Reset();
    }

    if (ck_test_runtime_mesh_cook_probe::TickerHandle.IsValid())
    {
        FTSTicker::RemoveTicker(ck_test_runtime_mesh_cook_probe::TickerHandle);
        ck_test_runtime_mesh_cook_probe::TickerHandle.Reset();
    }

    ck_test_runtime_mesh_cook_probe::Probe.Reset();
}

#endif
