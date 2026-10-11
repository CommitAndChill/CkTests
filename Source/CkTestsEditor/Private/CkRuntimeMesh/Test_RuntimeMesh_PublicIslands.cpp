#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Time/CkTime.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkRuntimeMesh/CkRuntimeMesh_Processor.h"
#include "CkRuntimeMesh/CkRuntimeMesh_Utils.h"
#include "CkTest_RuntimeMeshRenderListener.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MeshDescription.h"
#include "Misc/ScopeExit.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_runtime_mesh_public_islands
{
    constexpr auto InformEngineOfWorld = false;
    constexpr TCHAR PackagePath[] = TEXT("/CkTests/CkRuntimeMesh/Transient/SM_UPrism_Islands");

    // A U prism 10 cm deep whose right arm (3 cm) is wider than its left (2 cm): 600 cm3. A cut at y = 5 leaves the two
    // arms (150 and 100 cm3) on the positive side and the 350 cm3 base on the negative side.
    const FVector2f Profile[8] = {
        {0, 0}, {10, 0}, {10, 10}, {7, 10},
        {7, 2}, {2, 2}, {2, 10}, {0, 10}};
    constexpr int32 CapTriangles[6][3] = {
        {1, 2, 3}, {1, 3, 4}, {0, 1, 4}, {0, 4, 5}, {0, 5, 6}, {0, 6, 7}};
    constexpr float DepthCm = 10.0f;

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
        auto BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
        auto Colors = Attributes.GetVertexInstanceColors();
        auto UVs = Attributes.GetVertexInstanceUVs();
        auto SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
        const auto Group = Description.CreatePolygonGroup();
        SlotNames[Group] = TEXT("UPrism");

        FVertexID Bottom[8];
        FVertexID Top[8];
        for (int32 Index = 0; Index < 8; ++Index)
        {
            Bottom[Index] = Description.CreateVertex();
            Positions[Bottom[Index]] = FVector3f{Profile[Index].X, Profile[Index].Y, 0.0f};
            Top[Index] = Description.CreateVertex();
            Positions[Top[Index]] = FVector3f{Profile[Index].X, Profile[Index].Y, DepthCm};
        }

        // Front faces wind clockwise seen from outside (the cooked cube fixture's convention), so the outward normal is
        // (C - A) x (B - A).
        const auto AddTriangle = [&](FVertexID InA, FVertexID InB, FVertexID InC)
        {
            const FVertexID Corners[3] = {InA, InB, InC};
            const auto A = Positions[InA];
            const auto B = Positions[InB];
            const auto C = Positions[InC];
            const auto Normal = FVector3f::CrossProduct(C - A, B - A).GetSafeNormal();
            const auto Tangent = (B - A).GetSafeNormal();
            auto Instances = TArray<FVertexInstanceID>{};
            for (const auto Corner : Corners)
            {
                const auto Instance = Description.CreateVertexInstance(Corner);
                Normals[Instance] = Normal;
                Tangents[Instance] = Tangent;
                BinormalSigns[Instance] = 1.0f;
                Colors[Instance] = FVector4f{1.0f, 1.0f, 1.0f, 1.0f};
                UVs.Set(Instance, 0, FVector2f{Positions[Corner].X, Positions[Corner].Y} / 10.0f);
                Instances.Add(Instance);
            }
            Description.CreatePolygon(Group, Instances);
        };

        for (const auto& Triangle : CapTriangles)
        {
            AddTriangle(Bottom[Triangle[0]], Bottom[Triangle[1]], Bottom[Triangle[2]]);
            AddTriangle(Top[Triangle[0]], Top[Triangle[2]], Top[Triangle[1]]);
        }
        for (int32 Index = 0; Index < 8; ++Index)
        {
            const auto Next = (Index + 1) % 8;
            AddTriangle(Bottom[Index], Top[Next], Bottom[Next]);
            AddTriangle(Bottom[Index], Top[Index], Top[Next]);
        }
        return Description;
    }

    // In memory only: the package is never saved, and the mesh is marked garbage when the test ends.
    auto BuildMesh() -> UStaticMesh*
    {
        auto* Package = CreatePackage(PackagePath);
        if (Package == nullptr)
        { return nullptr; }
        Package->SetFlags(RF_Transient);
        const auto Name = MakeUniqueObjectName(Package, UStaticMesh::StaticClass(), TEXT("SM_UPrism_Islands"));
        auto* Mesh = NewObject<UStaticMesh>(Package, Name, RF_Public | RF_Transient);
        if (Mesh == nullptr)
        { return nullptr; }
        Mesh->bAllowCPUAccess = true;
        Mesh->NeverStream = true;
        Mesh->GetNaniteSettings().bEnabled = false;
        Mesh->SetStaticMaterials({FStaticMaterial{nullptr, TEXT("UPrism")}});
        Mesh->SetNumSourceModels(1);
        auto& BuildSettings = Mesh->GetSourceModel(0).BuildSettings;
        BuildSettings.bGenerateLightmapUVs = false;
        BuildSettings.bRecomputeNormals = false;
        BuildSettings.bRecomputeTangents = false;

        auto Description = MakeDescription();
        auto Params = UStaticMesh::FBuildMeshDescriptionsParams{};
        Params.bCommitMeshDescription = true;
        Params.bFastBuild = false;
        Params.bAllowCpuAccess = true;
        Params.bBuildSimpleCollision = false;
        if (NOT Mesh->BuildFromMeshDescriptions({&Description}, Params))
        { return nullptr; }
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
        return Mesh;
    }

    auto MakeRequest(const FCk_Handle& InResultOwner, ECk_RuntimeMesh_IslandPolicy InPolicy)
        -> FCk_Request_RuntimeMesh_Slice
    {
        auto Request = FCk_Request_RuntimeMesh_Slice{};
        Request.Set_OperationID(FGuid::NewGuid());
        Request.Set_ResultOwner(InResultOwner);
        auto Plane = FCk_RuntimeMesh_PlaneLocal{};
        Plane.Set_PositionCm(FVector{0, 5, 0});
        Plane.Set_Normal(FVector::RightVector);
        Plane.Set_Tangent(FVector::ForwardVector);
        Request.Set_Plane(Plane);
        Request.Set_IslandPolicy(InPolicy);
        return Request;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RuntimeMesh_PublicSplitIslands,
    "Ck.RuntimeMesh.Public.SplitIslandsAdoptsEveryPiece",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkTest_RuntimeMesh_PublicSplitIslands::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_public_islands;
    auto Mesh = TStrongObjectPtr<UStaticMesh>{BuildMesh()};
    if (NOT TestTrue(TEXT("in-memory U prism builds with CPU-readable render data"), Mesh.IsValid()))
    { return false; }

    auto* World = UWorld::CreateWorld(EWorldType::Game, InformEngineOfWorld, TEXT("RuntimeMeshIslandsFixture"));
    if (NOT TestNotNull(TEXT("isolated world"), World))
    { return false; }
    World->AddToRoot();
    ON_SCOPE_EXIT
    {
        ck::runtimemesh::CancelWorld(World);
        World->RemoveFromRoot();
        World->DestroyWorld(InformEngineOfWorld);
        Mesh->MarkAsGarbage();
    };

    auto WorldEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(World);
    if (NOT TestTrue(TEXT("world has a transient entity"), ck::IsValid(WorldEntity)))
    { return false; }
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldEntity);
    auto ResultOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldEntity);
    auto Spec = FCk_RuntimeMesh_Spec{};
    Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>{Mesh.Get()});
    auto Source = UCk_Utils_RuntimeMesh_UE::Add(Owner, Spec);
    if (NOT TestTrue(TEXT("source composes"), ck::IsValid(Source)))
    { return false; }
    ck::FProcessor_RuntimeMesh_Setup{WorldEntity.Get_RegistryView()}.ForEachEntity(
        FCk_Time{}, Source, Source.Get<ck::FFragment_RuntimeMesh>(),
        Source.Get<ck::FFragment_RuntimeMesh_PendingImport>());
    if (NOT TestEqual(TEXT("U prism imports Ready"), UCk_Utils_RuntimeMesh_UE::Get_SetupState(Source),
        ECk_RuntimeMesh_SetupState::Ready))
    {
        AddInfo(FString::Printf(TEXT("Setup failure %d"),
            static_cast<int32>(UCk_Utils_RuntimeMesh_UE::Get_SetupFailure(Source))));
        return false;
    }
    const auto SourceVolume = UCk_Utils_RuntimeMesh_UE::Get_Metrics(Source).Get_VolumeCm3();
    TestTrue(TEXT("U prism is 600 cm3"), FMath::IsNearlyEqual(SourceVolume, 600.0, 0.01));

    const auto Drain = [&]()
    {
        ck::FProcessor_RuntimeMesh_Drain{WorldEntity.Get_RegistryView()}.DoTick(FCk_Time{1.0f / 60.0f});
    };
    const auto Slice = [&](ECk_RuntimeMesh_IslandPolicy InPolicy) -> TOptional<FCk_RuntimeMesh_SliceResult>
    {
        auto Listener = TStrongObjectPtr<UCk_Test_RuntimeMeshRenderListener_UE>{
            NewObject<UCk_Test_RuntimeMeshRenderListener_UE>(GetTransientPackage())};
        auto Receiver = FCk_Delegate_RuntimeMesh_OnSliceResolved{};
        Receiver.BindDynamic(Listener.Get(), &UCk_Test_RuntimeMeshRenderListener_UE::OnSliceResolved);
        UCk_Utils_RuntimeMesh_UE::Request_Slice(Source, MakeRequest(ResultOwner, InPolicy), Receiver, {});
        Drain();
        if (Listener->Calls != 1)
        { return {}; }
        return Listener->Result;
    };

    const auto Rejected = Slice(ECk_RuntimeMesh_IslandPolicy::RejectMultiple);
    if (NOT TestTrue(TEXT("RejectMultiple resolves once"), Rejected.IsSet()))
    { return false; }
    TestEqual(TEXT("RejectMultiple rejects the disconnected side"), Rejected->Get_Outcome(),
        ECk_RuntimeMesh_SliceOutcome::RejectedTopology);
    TestTrue(TEXT("a rejected slice publishes no piece"), ck::Is_NOT_Valid(Rejected->Get_Positive())
        && Rejected->Get_PositivePieces().IsEmpty() && Rejected->Get_NegativePieces().IsEmpty());

    const auto Split = Slice(ECk_RuntimeMesh_IslandPolicy::SplitIslands);
    if (NOT TestTrue(TEXT("SplitIslands resolves once"), Split.IsSet())
        || NOT TestEqual(TEXT("SplitIslands succeeds"), Split->Get_Outcome(), ECk_RuntimeMesh_SliceOutcome::Succeeded))
    { return false; }
    const auto& PositivePieces = Split->Get_PositivePieces();
    const auto& NegativePieces = Split->Get_NegativePieces();
    if (NOT TestEqual(TEXT("two positive pieces"), PositivePieces.Num(), 2)
        || NOT TestEqual(TEXT("one negative piece"), NegativePieces.Num(), 1))
    { return false; }

    TestTrue(TEXT("Positive is the largest positive piece"), Split->Get_Positive() == PositivePieces[0].Get_Mesh());
    TestTrue(TEXT("Negative is the negative piece"), Split->Get_Negative() == NegativePieces[0].Get_Mesh());
    TestTrue(TEXT("largest piece first: 150 cm3"),
        FMath::IsNearlyEqual(PositivePieces[0].Get_Metrics().Get_VolumeCm3(), 150.0, 0.01));
    TestTrue(TEXT("second piece: 100 cm3"),
        FMath::IsNearlyEqual(PositivePieces[1].Get_Metrics().Get_VolumeCm3(), 100.0, 0.01));
    TestTrue(TEXT("negative piece: 350 cm3"),
        FMath::IsNearlyEqual(NegativePieces[0].Get_Metrics().Get_VolumeCm3(), 350.0, 0.01));
    TestTrue(TEXT("Positive metrics are the largest piece's"), FMath::IsNearlyEqual(
        Split->Get_PositiveMetrics().Get_VolumeCm3(), PositivePieces[0].Get_Metrics().Get_VolumeCm3()));

    auto TotalVolume = 0.0;
    auto Pieces = PositivePieces;
    Pieces.Append(NegativePieces);
    for (const auto& Piece : Pieces)
    {
        TestTrue(TEXT("every piece is its own Ready entity"), ck::IsValid(Piece.Get_Mesh())
            && UCk_Utils_RuntimeMesh_UE::Get_SetupState(Piece.Get_Mesh()) == ECk_RuntimeMesh_SetupState::Ready);
        TestTrue(TEXT("every piece has cap triangles"), Piece.Get_CapTriangles() > 0);
        TestTrue(TEXT("a piece's metrics match its entity"), FMath::IsNearlyEqual(
            UCk_Utils_RuntimeMesh_UE::Get_Metrics(Piece.Get_Mesh()).Get_VolumeCm3(), Piece.Get_Metrics().Get_VolumeCm3()));
        TotalVolume += Piece.Get_Metrics().Get_VolumeCm3();
    }
    TestTrue(TEXT("pieces conserve the source volume"), FMath::IsNearlyEqual(TotalVolume, SourceVolume, 0.01));
    TestTrue(TEXT("the three pieces are distinct entities"), NOT (PositivePieces[0].Get_Mesh() == PositivePieces[1].Get_Mesh())
        && NOT (PositivePieces[0].Get_Mesh() == NegativePieces[0].Get_Mesh())
        && NOT (PositivePieces[1].Get_Mesh() == NegativePieces[0].Get_Mesh()));
    TestTrue(TEXT("the source is unchanged"), FMath::IsNearlyEqual(
        UCk_Utils_RuntimeMesh_UE::Get_Metrics(Source).Get_VolumeCm3(), SourceVolume));
    return true;
}

#endif
