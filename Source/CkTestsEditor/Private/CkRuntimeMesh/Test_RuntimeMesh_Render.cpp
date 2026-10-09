#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Algorithms/CkAlgorithms.h"
#include "CkCore/ObjectPooling/CkObjectPooling_Subsystem.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsEditor_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkRuntimeMesh/CkRuntimeMesh_Utils.h"
#include "CkRuntimeMesh/Display/CkRuntimeMeshDisplay_Fragment.h"
#include "CkRuntimeMesh/Display/CkRuntimeMeshDisplay_Utils.h"
#include "CkTest_RuntimeMeshRenderListener.h"

#include "AssetCompilingManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/GarbageCollection.h"
#include "UObject/MetaData.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_runtime_mesh_render
{
    constexpr int32 Size = 512;
    constexpr TCHAR MeshPath[] = TEXT("/CkTests/CkRuntimeMesh/Render/SM_Checker_CPU.SM_Checker_CPU");
    constexpr TCHAR MaterialPaths[3][128] = {
        TEXT("/CkTests/CkRuntimeMesh/Render/M_Checker_Blue.M_Checker_Blue"),
        TEXT("/CkTests/CkRuntimeMesh/Render/M_Checker_Red.M_Checker_Red"),
        TEXT("/CkTests/CkRuntimeMesh/Render/M_Checker_Cap.M_Checker_Cap")};
    constexpr TCHAR MarkerKey[] = TEXT("CkRuntimeMeshRenderFixture");
    constexpr TCHAR MarkerValue[] = TEXT("RenderFixture-v1");

    auto Tick(UWorld& InWorld, int32 InCount = 1) -> bool
    {
        auto* Ecs = InWorld.GetSubsystem<UCk_EditorEcsWorld_Subsystem_UE>();
        if (ck::Is_NOT_Valid(Ecs))
        { return false; }

        for (auto Index = 0; Index < InCount; ++Index)
        { Ecs->Tick(1.0f / 60.0f); }

        return true;
    }

    auto TickUntilSettled(UWorld& InWorld, const FCk_Handle_RuntimeMeshDisplay& InDisplay) -> ECk_RuntimeMesh_SetupState
    {
        constexpr auto MaxSetupTicks = 8;
        for (auto Index = 0; Index < MaxSetupTicks; ++Index)
        {
            FlushAsyncLoading();
            Tick(InWorld);

            const auto State = UCk_Utils_RuntimeMeshDisplay_UE::Get_SetupState(InDisplay);
            if (State != ECk_RuntimeMesh_SetupState::Pending)
            { return State; }
        }

        return ECk_RuntimeMesh_SetupState::Pending;
    }

    auto IsMarked(const UObject& InAsset) -> bool
    {
        auto* Package = InAsset.GetOutermost();
        return Package != nullptr && Package->GetMetaData().GetValue(&InAsset, MarkerKey).Equals(MarkerValue);
    }

    struct FColorRegion
    {
        int32 Count = 0;
        int32 MinX = Size;
        int32 MinY = Size;
        int32 MaxX = -1;
        int32 MaxY = -1;
        uint8 MinChannel = 255;
        uint8 MaxChannel = 0;

        auto Add(int32 InX, int32 InY, uint8 InChannel) -> void
        {
            ++Count;
            MinX = FMath::Min(MinX, InX);
            MinY = FMath::Min(MinY, InY);
            MaxX = FMath::Max(MaxX, InX);
            MaxY = FMath::Max(MaxY, InY);
            MinChannel = FMath::Min(MinChannel, InChannel);
            MaxChannel = FMath::Max(MaxChannel, InChannel);
        }

        auto IsVisibleChecker() const -> bool
        { return Count > 200 && MaxX - MinX > 12 && MaxY - MinY > 12 && MaxChannel - MinChannel > 35; }
    };

    auto SavePng(const TArray<FColor>& InPixels, const FString& InName) -> FString
    {
        const auto Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"),
            TEXT("RuntimeMesh"), TEXT("Render"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const auto Path = FPaths::Combine(Directory, InName + TEXT(".png"));

        // The capture copy pass writes alpha 0, which image viewers show as a blank (white) frame.
        auto Opaque = InPixels;
        ck::algo::ForEach(Opaque, [](FColor& InPixel)
        {
            InPixel.A = 255;
        });
        auto Compressed = TArray64<uint8>{};
        FImageUtils::PNGCompressImageArray(Size, Size,
            TArrayView64<const FColor>{Opaque.GetData(), Opaque.Num()}, Compressed);
        auto Bytes = TArray<uint8>{};
        Bytes.Append(Compressed.GetData(), Compressed.Num());
        return FFileHelper::SaveArrayToFile(Bytes, *Path) && IFileManager::Get().FileSize(*Path) > 0
            ? Path : FString{};
    }

    auto ReadCapture(USceneCaptureComponent2D& InCapture, UTextureRenderTarget2D& InTarget) -> TArray<FColor>
    {
        InCapture.CaptureScene();
        FlushRenderingCommands();
        auto Pixels = TArray<FColor>{};
        auto* Resource = InTarget.GameThread_GetRenderTargetResource();
        if (ck::Is_NOT_Valid(Resource, ck::IsValid_Policy_NullptrOnly{}) || NOT Resource->ReadPixels(Pixels))
        { Pixels.Reset(); }

        return Pixels;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RuntimeMesh_RenderChecker,
    "Ck.RuntimeMesh.Render.CheckerSliceDisplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FCkTest_RuntimeMesh_RenderChecker::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_render;
    if (NOT FApp::CanEverRender())
    {
        AddError(TEXT("Requires a real RHI; run via UnrealToolbox without NullRHI."));
        return false;
    }
    auto* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
    if (NOT TestTrue(TEXT("owned render fixture exists"), Mesh != nullptr && IsMarked(*Mesh)))
    { return false; }
    auto Materials = TArray<TSoftObjectPtr<UMaterialInterface>>{};
    for (auto Index = 0; Index < 3; ++Index)
    {
        auto* Material = LoadObject<UMaterial>(nullptr, MaterialPaths[Index]);
        if (NOT TestTrue(TEXT("owned checker material exists"), Material != nullptr && IsMarked(*Material)))
        { return false; }
        Materials.Add(TSoftObjectPtr<UMaterialInterface>{Material});
    }
    const auto FixtureFilename = FPackageName::LongPackageNameToFilename(
        Mesh->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    if (NOT TestTrue(TEXT("source fixture exists on disk"), IFileManager::Get().FileExists(*FixtureFilename)))
    { return false; }
    AddInfo(FString::Printf(TEXT("Render source: %s; mtime=%s; owner=%s"), *FixtureFilename,
        *IFileManager::Get().GetTimeStamp(*FixtureFilename).ToString(), MarkerValue));

    auto* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("RuntimeMeshRender"));
    if (NOT TestNotNull(TEXT("isolated render world"), World))
    { return false; }

    World->AddToRoot();
    ON_SCOPE_EXIT
    {
        World->RemoveFromRoot();
        World->DestroyWorld(false);
        FlushRenderingCommands();
    };
    auto* Pool = World->GetSubsystem<UCk_ObjectPooling_Subsystem_UE>();
    if (NOT TestNotNull(TEXT("component pool"), Pool))
    { return false; }

    const auto PinnedBefore = Pool->Get_NumPinnedUnique();
    auto SourceOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
    auto ResultOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
    auto DisplayOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
    auto DisplayTransform = UCk_Utils_Transform_UE::Add(DisplayOwner, FTransform::Identity, ECk_Replication::DoesNotReplicate);
    auto SourceSpec = FCk_RuntimeMesh_Spec{};
    SourceSpec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>{Mesh});
    auto Source = UCk_Utils_RuntimeMesh_UE::Add(SourceOwner, SourceSpec);
    Tick(*World);
    FlushAsyncLoading();
    Tick(*World, 3);
    if (NOT TestEqual(TEXT("public source import Ready"),
        UCk_Utils_RuntimeMesh_UE::Get_SetupState(Source), ECk_RuntimeMesh_SetupState::Ready))
    { return false; }

    auto Listener = TStrongObjectPtr<UCk_Test_RuntimeMeshRenderListener_UE>{
        NewObject<UCk_Test_RuntimeMeshRenderListener_UE>(GetTransientPackage())};
    auto Request = FCk_Request_RuntimeMesh_Slice{};
    Request.Set_OperationID(FGuid::NewGuid());
    Request.Set_ResultOwner(ResultOwner);
    auto Plane = FCk_RuntimeMesh_PlaneLocal{};
    Plane.Set_PositionCm(FVector{0, 0, 5});
    Plane.Set_Normal(FVector::UpVector);
    Plane.Set_Tangent(FVector::ForwardVector);
    Request.Set_Plane(Plane);
    auto Cap = FCk_RuntimeMesh_Cap{};
    Cap.Set_MaterialID(2);
    Cap.Set_CmPerUVUnit(10.0);
    Request.Set_Cap(Cap);
    auto Receiver = FCk_Delegate_RuntimeMesh_OnSliceResolved{};
    Receiver.BindDynamic(Listener.Get(), &UCk_Test_RuntimeMeshRenderListener_UE::OnSliceResolved);
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Source, Request, Receiver, {});
    Tick(*World, 2);
    if (NOT TestEqual(TEXT("one slice terminal"), Listener->Calls, 1)
        || NOT TestEqual(TEXT("slice succeeds"), Listener->Result.Get_Outcome(),
            ECk_RuntimeMesh_SliceOutcome::Succeeded))
    { return false; }
    const auto Negative = Listener->Result.Get_Negative();
    if (NOT TestEqual(TEXT("negative half Ready"), UCk_Utils_RuntimeMesh_UE::Get_SetupState(Negative),
        ECk_RuntimeMesh_SetupState::Ready)
        || NOT TestTrue(TEXT("cap triangles emitted"), Listener->Result.Get_NegativeCapTriangles() > 0))
    { return false; }

    auto Visuals = FCk_RuntimeMeshDisplay_Visuals{};
    Visuals.Set_Materials(Materials);
    Visuals.Set_CastShadow(ECk_EnableDisable::Enable);
    auto DisplaySpec = FCk_RuntimeMeshDisplay_Spec{};
    DisplaySpec.Set_Geometry(Negative);
    DisplaySpec.Set_Visuals(Visuals);
    const auto Display = UCk_Utils_RuntimeMeshDisplay_UE::Add(DisplayTransform, DisplaySpec);
    if (NOT TestTrue(TEXT("display accepted"), ck::IsValid(Display)))
    { return false; }

    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(SourceOwner);
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(ResultOwner);
    CollectGarbage(RF_NoFlags);
    TickUntilSettled(*World, Display);
    Tick(*World, 3);
    if (NOT TestFalse(TEXT("result geometry owner gone"), ck::IsValid(Negative))
        || NOT TestEqual(TEXT("display Ready after geometry owner destruction"),
        UCk_Utils_RuntimeMeshDisplay_UE::Get_SetupState(Display), ECk_RuntimeMesh_SetupState::Ready))
    { return false; }
    const auto Component = Display.Get<ck::FFragment_RuntimeMeshDisplay>().Get_Component();
    if (NOT TestTrue(TEXT("rendered dynamic mesh exists"), Component.IsValid())
        || NOT TestTrue(TEXT("dynamic mesh casts shadow"), Component->CastShadow)
        || NOT TestEqual(TEXT("three material slots"), Component->GetNumMaterials(), 3))
    { return false; }

    auto* LightActor = World->SpawnActor<AActor>(AActor::StaticClass());
    auto* Light = LightActor != nullptr ? NewObject<UDirectionalLightComponent>(LightActor) : nullptr;
    if (NOT TestNotNull(TEXT("directional light"), Light))
    { return false; }

    LightActor->SetRootComponent(Light);
    Light->SetIntensity(6.0f);
    Light->SetWorldRotation(FRotator{-50, -40, 0});
    Light->RegisterComponentWithWorld(World);

    auto* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
    Target->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    Target->ClearColor = FLinearColor::Black;
    Target->InitAutoFormat(Size, Size);
    Target->UpdateResourceImmediate(true);
    auto* CaptureActor = World->SpawnActor<AActor>(AActor::StaticClass());
    auto* Capture = CaptureActor != nullptr ? NewObject<USceneCaptureComponent2D>(CaptureActor) : nullptr;
    if (NOT TestNotNull(TEXT("scene capture"), Capture)
        || NOT TestNotNull(TEXT("render target resource"), Target->GameThread_GetRenderTargetResource()))
    { return false; }
    CaptureActor->SetRootComponent(Capture);
    Capture->TextureTarget = Target;
    Capture->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
    Capture->ProjectionType = ECameraProjectionMode::Orthographic;
    Capture->OrthoWidth = 22.0f;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->SetWorldLocation(FVector{35, -45, 35});
    Capture->SetWorldRotation((FVector{5, 5, 2.5} - Capture->GetComponentLocation()).Rotation());
    Capture->RegisterComponentWithWorld(World);
    Capture->ShowFlags.SetAntiAliasing(false);
    Capture->ShowFlags.SetBloom(false);
    Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetTonemapper(false);

    // The editor loads materials without compiling their shaders and compiles on demand; until the jobs are
    // submitted and finished, every pass falls back to the default material.
    UMaterialInterface::SubmitRemainingJobsForWorld(World);
    FAssetCompilingManager::Get().FinishAllCompilation();
    FlushRenderingCommands();
    for (const auto& Material : Materials)
    {
        const auto* Resolved = Material.Get();
        if (NOT TestTrue(TEXT("checker material shaders compiled before capture"),
            Resolved != nullptr && Resolved->IsComplete()))
        { return false; }
    }

    const auto BaseColor = ReadCapture(*Capture, *Target);
    if (NOT TestEqual(TEXT("base-color readback"), BaseColor.Num(), Size * Size))
    { return false; }
    const auto RunId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const auto BaseColorPath = SavePng(BaseColor, FString::Printf(TEXT("%s_BaseColor"), *RunId));
    if (NOT TestFalse(TEXT("fresh base-color PNG saved"), BaseColorPath.IsEmpty()))
    { return false; }

    AddInfo(FString::Printf(TEXT("Base-color capture: %s"), *BaseColorPath));
    FColorRegion Regions[3];
    for (auto Y = 0; Y < Size; ++Y)
    {
        for (auto X = 0; X < Size; ++X)
        {
            const auto& Pixel = BaseColor[Y * Size + X];
            if (Pixel.B > 30 && Pixel.B > Pixel.R * 1.5f && Pixel.B > Pixel.G * 1.3f)
            { Regions[0].Add(X, Y, Pixel.B); }
            else if (Pixel.R > 30 && Pixel.R > Pixel.G * 1.5f && Pixel.R > Pixel.B * 1.5f)
            { Regions[1].Add(X, Y, Pixel.R); }
            else if (Pixel.G > 30 && Pixel.G > Pixel.R * 1.5f && Pixel.G > Pixel.B * 1.5f)
            { Regions[2].Add(X, Y, Pixel.G); }
        }
    }

    for (auto Index = 0; Index < 3; ++Index)
    {
        AddInfo(FString::Printf(TEXT("material %d region: %d px, [%d,%d]-[%d,%d], channel %d..%d"),
            Index, Regions[Index].Count, Regions[Index].MinX, Regions[Index].MinY,
            Regions[Index].MaxX, Regions[Index].MaxY, Regions[Index].MinChannel, Regions[Index].MaxChannel));
        if (NOT TestTrue(TEXT("expected checker material occupies a distinct visible region"),
            Regions[Index].IsVisibleChecker()))
        { return false; }
    }
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->PostProcessBlendWeight = 1.0f;
    Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    const auto FinalColor = ReadCapture(*Capture, *Target);
    if (NOT TestEqual(TEXT("lit readback"), FinalColor.Num(), Size * Size))
    { return false; }

    const auto FinalColorPath = SavePng(FinalColor, FString::Printf(TEXT("%s_FinalColor"), *RunId));
    if (NOT TestFalse(TEXT("fresh lit PNG saved"), FinalColorPath.IsEmpty()))
    { return false; }

    AddInfo(FString::Printf(TEXT("Lit capture: %s"), *FinalColorPath));
    const auto NonBlack = ck::algo::CountIf(FinalColor, [](const FColor& InPixel)
    {
        return InPixel.R > 10 || InPixel.G > 10 || InPixel.B > 10;
    });
    if (NOT TestTrue(TEXT("lit render is nonblank"), NonBlack > 1000))
    { return false; }

    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(DisplayOwner);
    Tick(*World, 3);
    CollectGarbage(RF_NoFlags);
    TestEqual(TEXT("display pool pin released"), Pool->Get_NumPinnedUnique(), PinnedBefore);
    TestFalse(TEXT("component released"), Component.IsValid());
    return true;
}

#endif
