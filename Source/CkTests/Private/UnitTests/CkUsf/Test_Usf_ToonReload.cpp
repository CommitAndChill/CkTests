#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"
#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "AssetCompilingManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SubsurfaceProfile.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "RenderingThread.h"
#include "ShaderCore.h"
#include "ShaderCompiler.h"
#include "Templates/Atomic.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_usf_toon_reload
{
    constexpr auto TargetSize = 64;

    struct FFixture final : FOutputDevice
    {
        TStrongObjectPtr<UWorld> World;
        TStrongObjectPtr<UMaterial> Material;
        TStrongObjectPtr<UMaterialInstanceDynamic> Mid;
        TStrongObjectPtr<UCkUsf_LookDefinition> Definition;
        FString LookFile;
        FString PackageRoot;
        FString PackageDirectory;
        TArray<uint8> LookBytes;
        bool LookModified = false;
        bool RejectionFixture = false;
        int32 PreviousAutoReload = 0;
        EConsoleVariableFlags AutoPriority = ECVF_SetByConstructor;
        bool RestoreAutoReload = false;
        TStrongObjectPtr<UTextureRenderTarget2D> Target;
        TObjectPtr<USceneCaptureComponent2D> Capture;
        TObjectPtr<UDirectionalLightComponent> Light;
        FString ShaderFile;
        TArray<uint8> OriginalBytes;
        TAtomic<bool> Completed{false};
        TAtomic<bool> ReloadRejected{false};
        bool Observing = false;
        bool Modified = false;
        bool RecoveryNeeded = false;
        bool RestoreDevelopmentMode = false;
        int32 PreviousDevelopmentMode = 0;
        EConsoleVariableFlags DevelopmentPriority = ECVF_SetByConstructor;
        double Baseline = 0.0;
        double ReloadStarted = 0.0;
        uint32 EditorPid = FPlatformProcess::GetCurrentProcessId();

        auto Serialize(const TCHAR* InMessage, ELogVerbosity::Type InVerbosity, const FName& InCategory)
            -> void override
        {
            if (FString{InMessage}.Contains(TEXT("CkUsf reload rejected before material changes: Unsupported shading model for this engine")))
            { ReloadRejected.Store(true); }
            if (FString{InMessage}.Contains(TEXT("CkUsf shading-model reload complete")))
            { Completed.Store(true); }
        }

        auto DispatchReload() -> bool
        {
            Completed.Store(false);
            ReloadStarted = FPlatformTime::Seconds();
            return IConsoleManager::Get().ProcessUserConsoleInput(
                TEXT("Ck_Usf_ReloadShadingModels"), *GLog, World.Get());
        }

        auto RestoreSource() -> bool
        {
            if (Modified == false)
            { return true; }
            if (FFileHelper::SaveArrayToFile(OriginalBytes, *ShaderFile) == false)
            { return false; }
            auto Restored = TArray<uint8>{};
            if (FFileHelper::LoadFileToArray(Restored, *ShaderFile) == false || Restored != OriginalBytes)
            { return false; }
            Modified = false;
            return true;
        }

        auto RestoreLook() -> bool
        {
            if (NOT LookModified)
            { return true; }
            if (NOT FFileHelper::SaveArrayToFile(LookBytes, *LookFile))
            { return false; }
            auto Actual = TArray<uint8>{};
            if (NOT FFileHelper::LoadFileToArray(Actual, *LookFile) || Actual != LookBytes)
            { return false; }
            LookModified = false;
            return true;
        }

        ~FFixture()
        {
            if (Observing)
            { GLog->RemoveOutputDevice(this); }
            if (NOT RestoreLook())
            { UE_LOG(LogTemp, Error, TEXT("Toon rejection fixture could not restore owned source")); }
            if (RejectionFixture)
            {
                if (Definition.IsValid())
                { Definition->_ShadingModel = ECk_Usf_ShadingModel::DefaultLit; }
                FAssetCompilingManager::Get().FinishAllCompilation();
                if (GShaderCompilingManager != nullptr)
                { GShaderCompilingManager->FinishAllCompilation(); }
                FlushRenderingCommands();
                if (Material.IsValid())
                {
                    for (const auto& Expression : Material->GetExpressions())
                    {
                        if (auto* Custom = Cast<UMaterialExpressionCustom>(Expression))
                        { Custom->IncludeFilePaths.Reset(); }
                    }
                    Material->UpdateCachedExpressionData();
                    Material->ClearFlags(RF_Public | RF_Standalone);
                    Material->MarkAsGarbage();
                }
                if (Definition.IsValid())
                {
                    const auto File = FPackageName::LongPackageNameToFilename(
                        Definition->Get_GeneratedMasterPackagePath(), FPackageName::GetAssetPackageExtension());
                    if (IFileManager::Get().FileExists(*File) && NOT IFileManager::Get().Delete(*File))
                    { UE_LOG(LogTemp, Error, TEXT("Toon rejection fixture could not remove owned master: %s"), *File); }
                }
                if (NOT PackageRoot.IsEmpty())
                { FPackageName::UnRegisterMountPoint(PackageRoot, PackageDirectory); }
            }
            if (RestoreAutoReload)
            {
                auto& Auto = *IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"));
                if ((Auto.GetFlags() & ECVF_SetByMask) == ECVF_SetByConsole && AutoPriority != ECVF_SetByConsole)
                { Auto.Unset(ECVF_SetByConsole); }
                else
                { Auto.Set(PreviousAutoReload, AutoPriority); }
                if (Auto.GetInt() != PreviousAutoReload || (Auto.GetFlags() & ECVF_SetByMask) != AutoPriority)
                { UE_LOG(LogTemp, Error, TEXT("Toon rejection fixture failed to restore auto reload value/priority")); }
            }
            // Failure cleanup is synchronous so it finishes before retry-mode restoration.
            // The normal pixel sequence exclusively exercises the public production command.
            if (Modified)
            {
                if (RestoreSource() == false)
                { UE_LOG(LogTemp, Error, TEXT("Toon reload fixture could not restore shader: %s"), *ShaderFile); }
            }
            if (RecoveryNeeded && Modified == false && GEngine != nullptr)
            {
                GEngine->Exec(nullptr, TEXT("RecompileShaders Changed"));
                if (GShaderCompilingManager != nullptr)
                { GShaderCompilingManager->FinishAllCompilation(); }
                FlushRenderingCommands();
            }
            if (World.IsValid())
            {
                World->DestroyWorld(false);
                FlushRenderingCommands();
            }
            if (RestoreDevelopmentMode && IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderDevelopmentMode")))
            {
                auto& DevelopmentMode = *IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderDevelopmentMode"));
                const auto CurrentPriority = static_cast<EConsoleVariableFlags>(DevelopmentMode.GetFlags() & ECVF_SetByMask);
                if (CurrentPriority == ECVF_SetByConsole && DevelopmentPriority != ECVF_SetByConsole)
                { DevelopmentMode.Unset(ECVF_SetByConsole); }
                else if (DevelopmentMode.GetInt() != PreviousDevelopmentMode)
                { DevelopmentMode.Set(PreviousDevelopmentMode, DevelopmentPriority); }
                if (DevelopmentMode.GetInt() != PreviousDevelopmentMode
                    || (DevelopmentMode.GetFlags() & ECVF_SetByMask) != DevelopmentPriority)
                { UE_LOG(LogTemp, Error, TEXT("Toon fixture failed to restore shader development mode value/priority")); }
            }
        }

        auto CaptureMean(FAutomationTestBase& InTest, const TCHAR* InStage) -> double
        {
            // Use Unreal's draw-readiness helper to finish on-demand shaders without recaching
            // the parent material between MID updates. DrawMaterialToRenderTarget uses it too.
            Material->EnsureIsComplete();
            FAssetCompilingManager::Get().FinishAllCompilation();
            if (GShaderCompilingManager != nullptr)
            { GShaderCompilingManager->FinishAllCompilation(); }
            FlushRenderingCommands();
            if (InTest.TestNotNull(TEXT("Native Toon material has a rendering resource"),
                Material->GetMaterialResource(GMaxRHIShaderPlatform)) == false)
            { return -1.0; }
            const auto& Resource = *Material->GetMaterialResource(GMaxRHIShaderPlatform);
            for (const auto& Error : Resource.GetCompileErrors())
            { InTest.AddError(Error); }
            if (InTest.TestTrue(TEXT("Native Toon shader compilation finished"), Resource.IsCompilationFinished()) == false ||
                InTest.TestTrue(TEXT("Native Toon game-thread shader map is complete"), Resource.IsGameThreadShaderMapComplete()) == false ||
                InTest.TestTrue(TEXT("Native Toon resource has no compile errors"), Resource.GetCompileErrors().IsEmpty()) == false ||
                InTest.TestNotNull(TEXT("Native Toon has an applied game-thread shader map"), Resource.GetGameThreadShaderMap()) == false)
            { return -1.0; }
            auto RenderMapComplete = false;
            ENQUEUE_RENDER_COMMAND(CkUsfToonShaderMapReady)([&Resource, &RenderMapComplete](FRHICommandListImmediate&)
            {
                RenderMapComplete = Resource.IsRenderingThreadShaderMapComplete();
            });
            FlushRenderingCommands();
            if (InTest.TestTrue(TEXT("Native Toon render-thread shader map is complete"), RenderMapComplete) == false)
            { return -1.0; }
            Capture->CaptureScene();
            FlushRenderingCommands();
            auto Pixels = TArray<FLinearColor>{};
            if (InTest.TestTrue(TEXT("Toon floating-point readback succeeds"),
                Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels)) == false ||
                InTest.TestEqual(TEXT("Toon capture has expected pixel count"), Pixels.Num(), TargetSize * TargetSize) == false)
            { return -1.0; }
            auto Sum = 0.0;
            // The centre lies safely inside the front cube face, away from silhouette filtering.
            for (auto Y = 24; Y < 40; ++Y)
            for (auto X = 24; X < 40; ++X)
            {
                const auto& Pixel = Pixels[Y * TargetSize + X];
                if (FMath::IsFinite(Pixel.R) == false || FMath::IsFinite(Pixel.G) == false || FMath::IsFinite(Pixel.B) == false)
                {
                    InTest.AddError(TEXT("Toon readback contains non-finite RGB"));
                    return -1.0;
                }
                Sum += (Pixel.R + Pixel.G + Pixel.B) / 3.0;
            }
            const auto Mean = Sum / 256.0;
            const auto Evidence = FString::Printf(TEXT("Toon %s: PID=%u MID=%s meanRGB=%.6f reloadSeconds=%.3f"),
                InStage, EditorPid, *Mid->GetPathName(), Mean,
                ReloadStarted == 0.0 ? 0.0 : FPlatformTime::Seconds() - ReloadStarted);
            InTest.AddInfo(Evidence);
            UE_LOG(LogTemp, Display, TEXT("%s"), *Evidence);
            auto Image = TArray<FColor>{};
            Image.Reserve(Pixels.Num());
            for (const auto& Pixel : Pixels)
            { Image.Add(Pixel.ToFColor(true)); }
            const auto Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/ToonReload"),
                FString::Printf(TEXT("P%u"), EditorPid));
            IFileManager::Get().MakeDirectory(*Directory, true);
            const auto Path = FPaths::Combine(Directory, FString(InStage) + TEXT(".png"));
            auto Png = TArray64<uint8>{};
            FImageUtils::PNGCompressImageArray(TargetSize, TargetSize,
                TArrayView64<const FColor>{Image.GetData(), Image.Num()}, Png);
            InTest.TestTrue(TEXT("Toon inspection image saved"), FFileHelper::SaveArrayToFile(Png, *Path));
            InTest.AddInfo(FString::Printf(TEXT("Toon inspection PNG: %s"), *Path));
            return Mean;
        }
    };


    auto PrepareUnsupportedReload(FAutomationTestBase& InTest, const TSharedRef<FFixture>& InFixture,
        bool InSourceSelected) -> bool
    {
        auto* Auto = IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"));
        auto* Development = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderDevelopmentMode"));
        if (NOT InTest.TestNotNull(TEXT("Production auto-reload CVar exists"), Auto)
            || NOT InTest.TestNotNull(TEXT("Shader development-mode CVar exists"), Development))
        { return false; }
        InFixture->PreviousAutoReload = Auto->GetInt();
        InFixture->AutoPriority = static_cast<EConsoleVariableFlags>(Auto->GetFlags() & ECVF_SetByMask);
        InFixture->RestoreAutoReload = true;
        Auto->Set(0, ECVF_SetByConsole);
        if (NOT InTest.TestEqual(TEXT("Owned source edits cannot trigger an automatic reload"), Auto->GetInt(), 0))
        { return false; }
        InFixture->PreviousDevelopmentMode = Development->GetInt();
        InFixture->DevelopmentPriority = static_cast<EConsoleVariableFlags>(Development->GetFlags() & ECVF_SetByMask);
        InFixture->RestoreDevelopmentMode = true;
        InFixture->RejectionFixture = true;
        const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const auto Directory = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectSavedDir() / TEXT("Automation/ToonRejectedReload") / Id);
        const auto VirtualRoot = TEXT("/CkUsf/RejectedReload_") + Id;
        InFixture->LookFile = Directory / TEXT("Look.ush");
        InFixture->PackageRoot = TEXT("/CkUsfRejectedReload_") + Id + TEXT("/");
        InFixture->PackageDirectory = Directory / TEXT("Packages/");
        if (NOT InTest.TestTrue(TEXT("Create owned reload fixture directory"),
            IFileManager::Get().MakeDirectory(*InFixture->PackageDirectory, true)))
        { return false; }
        AddShaderSourceDirectoryMapping(VirtualRoot, Directory);
        FPackageName::RegisterMountPoint(InFixture->PackageRoot, InFixture->PackageDirectory);
        const auto Source = FString{TEXT("#pragma once\n#include \"/CkUsf/Common.ush\"\n")
            TEXT("FCkUsf_SurfaceOutput CkUsf_Look_RejectedReload(FCkUsf_SurfaceInput In, float Tint) { ")
            TEXT("FCkUsf_SurfaceOutput O = CkUsf_DefaultSurface(); O.BaseColor = Tint; return O; }\n")};
        if (NOT InTest.TestTrue(TEXT("Write owned compatible source"),
                FFileHelper::SaveStringToFile(Source, *InFixture->LookFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            || NOT InTest.TestTrue(TEXT("Capture exact original owned source"),
                FFileHelper::LoadFileToArray(InFixture->LookBytes, *InFixture->LookFile)))
        { return false; }
        InFixture->Definition.Reset(NewObject<UCkUsf_LookDefinition>());
        InFixture->Definition->_LookName = FName{*(TEXT("RejectedReload_") + Id)};
        InFixture->Definition->_GeneratedPackageRoot = InFixture->PackageRoot.LeftChop(1);
        InFixture->Definition->_UshIncludePath = VirtualRoot / TEXT("Look.ush");
        InFixture->Definition->_UshFunctionName = TEXT("CkUsf_Look_RejectedReload");
        InFixture->Definition->_ShadingModel = ECk_Usf_ShadingModel::DefaultLit;
        auto Tint = FCk_Usf_ParamDesc{};
        Tint._Name = TEXT("Tint");
        Tint._DefaultScalar = 0.4f;
        InFixture->Definition->_Parameters.Add(Tint);
        InFixture->Material.Reset(ck::usf_editor::Generate_LookMaterial(InFixture->Definition.Get()));
        if (NOT InTest.TestNotNull(TEXT("Production generator seeds compatible reload master"), InFixture->Material.Get()))
        { return false; }
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (GShaderCompilingManager != nullptr)
        { GShaderCompilingManager->FinishAllCompilation(); }
        FlushRenderingCommands();
        InFixture->Mid.Reset(UMaterialInstanceDynamic::Create(InFixture->Material.Get(), GetTransientPackage()));
        if (NOT InTest.TestNotNull(TEXT("Existing MID exists before requesting unsupported Toon"), InFixture->Mid.Get()))
        { return false; }
        InFixture->Mid->SetScalarParameterValue(TEXT("Tint"), 0.75f);
        if (InSourceSelected)
        {
            InFixture->LookModified = true;
            if (NOT InTest.TestTrue(TEXT("Request Toon by changing only the owned source declaration"),
                FFileHelper::SaveStringToFile(TEXT("// @ckusf shading_model Toon\n") + Source,
                    *InFixture->LookFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
            { return false; }
        }
        else
        { InFixture->Definition->_ShadingModel = ECk_Usf_ShadingModel::Toon; }
        return true;
    }

    class FRejectedReloadSequence final : public IAutomationLatentCommand
    {
    public:
        FRejectedReloadSequence(FAutomationTestBase& InTest, TSharedRef<FFixture> InFixture)
            : Test(InTest), Fixture(MoveTemp(InFixture)),
              MaterialIdentity(Fixture->Material->GetUniqueID()), MidIdentity(Fixture->Mid->GetUniqueID()),
              StateId(Fixture->Material->StateId), Blend(Fixture->Material->BlendMode),
              Lighting(Fixture->Material->TranslucencyLightingMode), TwoSided(Fixture->Material->TwoSided),
              TangentSpace(Fixture->Material->bTangentSpaceNormal), Profile(Fixture->Material->SubsurfaceProfile)
        {
            Expressions = Fixture->Material->GetExpressionCollection().Expressions;
            for (const auto& Expression : Expressions)
            {
                if (auto* Custom = Cast<UMaterialExpressionCustom>(Expression))
                { Customs.Add(Custom); Codes.Add(Custom->Code); Outputs.Add(Custom->AdditionalOutputs); }
            }
            for (const auto Property : Properties)
            { Inputs.Add(*Fixture->Material->GetExpressionInputForProperty(Property)); }
            PackageFile = FPackageName::LongPackageNameToFilename(
                Fixture->Definition->Get_GeneratedMasterPackagePath(), FPackageName::GetAssetPackageExtension());
            Test.TestTrue(TEXT("Capture owned master bytes before rejection"), FFileHelper::LoadFileToArray(PackageBytes, *PackageFile));
        }

        auto Update() -> bool override
        {
            if (NOT Dispatched)
            {
                auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
                auto Errors = TArray<FString>{};
                Test.TestFalse(TEXT("Actual reload preflight rejects the owned unsupported Toon transition"),
                    ck::usf_editor::Prepare_LookShadingModelReload(*Fixture->Material, Prepared, Errors));
                Test.TestTrue(TEXT("Owned reload preflight explicitly reports missing native engine support"),
                    Errors.Contains(TEXT("Unsupported shading model for this engine")));
                Test.TestTrue(TEXT("Rejected preparation cannot apply a Custom node or new HLSL"),
                    NOT Prepared.Custom.IsValid() && Prepared.Code.IsEmpty());
                CheckUnchanged();
                Test.AddExpectedError(TEXT("CkUsf reload rejected before material changes: Unsupported shading model for this engine"),
                    EAutomationExpectedErrorFlags::Contains, 1);
                GLog->AddOutputDevice(&Fixture.Get());
                Fixture->Observing = true;
                Dispatched = true;
                if (NOT Test.TestTrue(TEXT("Dispatch actual production reload command"), Fixture->DispatchReload()))
                { return true; }
                return false;
            }
            if (NOT Fixture->ReloadRejected.Load())
            {
                if (FPlatformTime::Seconds() - Fixture->ReloadStarted < 120.0)
                { return false; }
                Test.AddError(TEXT("Production reload did not report explicit unsupported Toon rejection"));
                return true;
            }
            Test.TestTrue(TEXT("Production shading-model command completes its global reload before material rejection"),
                Fixture->Completed.Load());
            CheckUnchanged();
            Test.TestTrue(TEXT("Exact owned source restored after rejected reload"), Fixture->RestoreLook());
            Fixture->Definition->_ShadingModel = ECk_Usf_ShadingModel::DefaultLit;
            return true;
        }

    private:
        auto CheckUnchanged() -> void
        {
            auto& Material = *Fixture->Material;
            Test.TestTrue(TEXT("Rejected reload preserves existing material and MID identities"),
                Material.GetUniqueID() == MaterialIdentity && Fixture->Mid->GetUniqueID() == MidIdentity);
            Test.TestTrue(TEXT("Rejected reload preserves material state and shading settings"),
                Material.StateId == StateId && Material.GetShadingModels().HasOnlyShadingModel(MSM_DefaultLit)
                    && Material.BlendMode == Blend && Material.TranslucencyLightingMode == Lighting
                    && Material.TwoSided == TwoSided && Material.bTangentSpaceNormal == TangentSpace
                    && Material.SubsurfaceProfile == Profile.Get());
            Test.TestTrue(TEXT("Rejected reload preserves the existing MID parent and scalar override"),
                Fixture->Mid->Parent == Fixture->Material.Get()
                    && FMath::IsNearlyEqual(Fixture->Mid->K2_GetScalarParameterValue(TEXT("Tint")), 0.75f));
            Test.TestTrue(TEXT("Rejected reload preserves expression identities"),
                Material.GetExpressionCollection().Expressions == Expressions);
            for (auto Index = 0; Index < Customs.Num(); ++Index)
            {
                Test.TestEqual(TEXT("Rejected reload preserves Custom HLSL"), Customs[Index]->Code, Codes[Index]);
                const auto& Before = Outputs[Index];
                const auto& After = Customs[Index]->AdditionalOutputs;
                Test.TestEqual(TEXT("Rejected reload preserves Custom output count"), After.Num(), Before.Num());
                for (auto OutputIndex = 0; OutputIndex < FMath::Min(Before.Num(), After.Num()); ++OutputIndex)
                {
                    Test.TestTrue(TEXT("Rejected reload preserves Custom output names and types"),
                        Before[OutputIndex].OutputName == After[OutputIndex].OutputName
                            && Before[OutputIndex].OutputType == After[OutputIndex].OutputType);
                }
            }
            for (auto Index = 0; Index < Properties.Num(); ++Index)
            {
                const auto& Before = Inputs[Index];
                const auto& After = *Fixture->Material->GetExpressionInputForProperty(Properties[Index]);
                Test.TestTrue(TEXT("Rejected reload preserves every material pin connection"),
                    Before.Expression == After.Expression && Before.OutputIndex == After.OutputIndex
                        && Before.Mask == After.Mask && Before.MaskR == After.MaskR && Before.MaskG == After.MaskG
                        && Before.MaskB == After.MaskB && Before.MaskA == After.MaskA);
            }
            auto Actual = TArray<uint8>{};
            Test.TestTrue(TEXT("Rejected reload writes no changes to the existing master package"),
                FFileHelper::LoadFileToArray(Actual, *PackageFile) && Actual == PackageBytes);
        }

        FAutomationTestBase& Test;
        TSharedRef<FFixture> Fixture;
        uint32 MaterialIdentity;
        uint32 MidIdentity;
        FGuid StateId;
        TEnumAsByte<EBlendMode> Blend;
        TEnumAsByte<ETranslucencyLightingMode> Lighting;
        bool TwoSided;
        bool TangentSpace;
        TWeakObjectPtr<USubsurfaceProfile> Profile;
        TArray<TObjectPtr<UMaterialExpression>> Expressions;
        TArray<TObjectPtr<UMaterialExpressionCustom>> Customs;
        TArray<FString> Codes;
        TArray<TArray<FCustomOutput>> Outputs;
        const TArray<EMaterialProperty> Properties{MP_BaseColor, MP_Normal, MP_Metallic, MP_Specular, MP_Roughness,
            MP_AmbientOcclusion, MP_EmissiveColor, MP_Opacity, MP_OpacityMask, MP_Refraction,
            MP_SubsurfaceColor, MP_CustomData0, MP_CustomData1};
        TArray<FExpressionInput> Inputs;
        FString PackageFile;
        TArray<uint8> PackageBytes;
        bool Dispatched = false;
    };

    class FReloadSequence final : public IAutomationLatentCommand
    {
    public:
        FReloadSequence(FAutomationTestBase& InTest, TSharedRef<FFixture> InFixture)
            : Test(InTest), Fixture(MoveTemp(InFixture)) {}

        auto Update() -> bool override
        {
            if (Fixture->Completed.Load() == false)
            {
                if (FPlatformTime::Seconds() - Fixture->ReloadStarted > 600.0)
                {
                    Test.AddError(TEXT("Timed out waiting for production shading-model reload completion"));
                    return true;
                }
                return false;
            }
            Test.TestEqual(TEXT("Reload remains in the same editor process"),
                FPlatformProcess::GetCurrentProcessId(), Fixture->EditorPid);
            Test.TestTrue(TEXT("Existing MID retains the same material parent"), Fixture->Mid->Parent == Fixture->Material.Get());
            Test.TestTrue(TEXT("Reload enables development shader retries"),
                IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderDevelopmentMode"))->GetInt() != 0);
            if (Restoring == false)
            {
                const auto Dark = Fixture->CaptureMean(Test, TEXT("Changed"));
                Test.TestTrue(TEXT("Editing deferred Toon lighting makes existing MID dark"),
                    Dark >= 0.0 && Dark < Fixture->Baseline * 0.1);
                if (Test.TestTrue(TEXT("Exact original lighting shader bytes restored"), Fixture->RestoreSource()) == false)
                { return true; }
                Restoring = true;
                if (Test.TestTrue(TEXT("Production recovery reload dispatched"), Fixture->DispatchReload()) == false)
                { return true; }
                return false;
            }
            const auto Recovered = Fixture->CaptureMean(Test, TEXT("Recovered"));
            Test.TestTrue(TEXT("Restored lighting shader recovers original pixels on same MID"),
                Recovered > 0.02 && FMath::Abs(Recovered - Fixture->Baseline) <= Fixture->Baseline * 0.05);
            Fixture->RecoveryNeeded = false;
            return true;
        }

    private:
        FAutomationTestBase& Test;
        TSharedRef<FFixture> Fixture;
        bool Restoring = false;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ToonReload,
    "CkTests.UnitTests.CkUsf.ToonLightingReloadUpdatesExistingMID",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

auto FCkTest_Usf_ToonReload::RunTest(const FString& Parameters) -> bool
{
#if defined(UE_CK_USF_SHADING_MODEL_VERSION) && UE_CK_USF_SHADING_MODEL_VERSION >= 1
    using namespace ck_test_usf_toon_reload;
    if (TestTrue(TEXT("Toon lighting proof requires a real RHI"), FApp::CanEverRender()) == false)
    { return false; }
    if (TestNotNull(TEXT("Production shading-model command is registered"),
        IConsoleManager::Get().FindConsoleObject(TEXT("Ck_Usf_ReloadShadingModels"))) == false)
    { return false; }
    const auto& Mappings = AllShaderSourceDirectoryMappings();
    if (TestTrue(TEXT("Stable shading-model mapping exists"), Mappings.Contains(TEXT("/CkUsfShadingModel"))) == false)
    { return false; }
    const auto Fixture = MakeShared<FFixture>();
    Fixture->ShaderFile = FPaths::Combine(Mappings.FindChecked(TEXT("/CkUsfShadingModel")), TEXT("CkUsf_Toon.ush"));
    if (TestTrue(TEXT("Production lighting shader readable"),
        FFileHelper::LoadFileToArray(Fixture->OriginalBytes, *Fixture->ShaderFile)) == false)
    { return false; }
    auto Source = FString{};
    if (TestTrue(TEXT("Production lighting shader text readable"), FFileHelper::LoadFileToString(Source, *Fixture->ShaderFile)) == false)
    { return false; }
    const auto Marker = FString{TEXT("#define CK_USF_TOON_ITERATION_GAIN 1.0")};
    if (TestEqual(TEXT("Exactly one approved Toon gain marker exists"),
        Source.ReplaceInline(*Marker, TEXT("#define CK_USF_TOON_ITERATION_GAIN 0.0"), ESearchCase::CaseSensitive), 1) == false)
    { return false; }
    if (TestNotNull(TEXT("Development retry CVar exists"),
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderDevelopmentMode"))) == false)
    { return false; }
    Fixture->PreviousDevelopmentMode = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderDevelopmentMode"))->GetInt();
    Fixture->DevelopmentPriority = static_cast<EConsoleVariableFlags>(
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderDevelopmentMode"))->GetFlags() & ECVF_SetByMask);
    Fixture->RestoreDevelopmentMode = true;
    Fixture->World.Reset(UWorld::CreateWorld(EWorldType::Game, false));
    if (TestNotNull(TEXT("Isolated rendering world exists"), Fixture->World.Get()) == false)
    { return false; }
    Fixture->Material.Reset(NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient));
    Fixture->Material->SetShadingModel(MSM_CkUsf);
    Fixture->Material->bFullyRough = true;
    const auto White = TObjectPtr<UMaterialExpressionConstant3Vector>{NewObject<UMaterialExpressionConstant3Vector>(Fixture->Material.Get())};
    White->Constant = FLinearColor::White;
    const auto Zero = TObjectPtr<UMaterialExpressionConstant>{NewObject<UMaterialExpressionConstant>(Fixture->Material.Get())};
    Zero->R = 0.0f;
    const auto Threshold = TObjectPtr<UMaterialExpressionScalarParameter>{NewObject<UMaterialExpressionScalarParameter>(Fixture->Material.Get())};
    Threshold->ParameterName = TEXT("ToonProbeThreshold");
    Threshold->DefaultValue = 0.5f;
    const auto Softness = TObjectPtr<UMaterialExpressionConstant>{NewObject<UMaterialExpressionConstant>(Fixture->Material.Get())};
    Softness->R = 0.05f;
    Fixture->Material->GetExpressionCollection().AddExpression(Threshold.Get());
    Fixture->Material->GetExpressionCollection().AddExpression(Softness.Get());
    Fixture->Material->GetEditorOnlyData()->ClearCoat.Connect(0, Threshold.Get());
    Fixture->Material->GetEditorOnlyData()->ClearCoatRoughness.Connect(0, Softness.Get());
    Fixture->Material->GetExpressionCollection().AddExpression(White.Get());
    Fixture->Material->GetExpressionCollection().AddExpression(Zero.Get());
    Fixture->Material->GetEditorOnlyData()->BaseColor.Connect(0, White.Get());
    Fixture->Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Zero.Get());
    Fixture->Material->GetEditorOnlyData()->Specular.Connect(0, Zero.Get());
    Fixture->Material->PostEditChange();
    FAssetCompilingManager::Get().FinishAllCompilation();
    Fixture->Mid.Reset(UMaterialInstanceDynamic::Create(Fixture->Material.Get(), GetTransientPackage()));
    Fixture->Mid->SetScalarParameterValue(TEXT("ToonProbeThreshold"), 0.5f);
    const auto MeshActor = TObjectPtr<AActor>{Fixture->World->SpawnActor<AActor>()};
    const auto Mesh = TObjectPtr<UStaticMeshComponent>{NewObject<UStaticMeshComponent>(MeshActor.Get())};
    MeshActor->SetRootComponent(Mesh.Get());
    const auto Cube = TObjectPtr<UStaticMesh>{LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))};
    if (TestNotNull(TEXT("Engine cube exists"), Cube.Get()) == false)
    { return false; }
    Mesh->SetStaticMesh(Cube.Get());
    Mesh->bDisallowNanite = true;
    Mesh->SetMaterial(0, Fixture->Mid.Get());
    Mesh->RegisterComponent();
    const auto LightActor = TObjectPtr<AActor>{Fixture->World->SpawnActor<AActor>()};
    Fixture->Light = NewObject<UDirectionalLightComponent>(LightActor.Get());
    LightActor->SetRootComponent(Fixture->Light.Get());
    Fixture->Light->SetMobility(EComponentMobility::Movable);
    Fixture->Light->SetIntensity(3.0f);
    Fixture->Light->SetWorldRotation(FRotator::ZeroRotator);
    Fixture->Light->SetCastShadows(false);
    Fixture->Light->RegisterComponent();
    Fixture->Target.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
    Fixture->Target->RenderTargetFormat = RTF_RGBA16f;
    Fixture->Target->ClearColor = FLinearColor::Black;
    Fixture->Target->InitAutoFormat(TargetSize, TargetSize);
    Fixture->Target->UpdateResourceImmediate(true);
    const auto CaptureActor = TObjectPtr<AActor>{Fixture->World->SpawnActor<AActor>()};
    Fixture->Capture = NewObject<USceneCaptureComponent2D>(CaptureActor.Get());
    CaptureActor->SetRootComponent(Fixture->Capture.Get());
    Fixture->Capture->TextureTarget = Fixture->Target.Get();
    Fixture->Capture->CaptureSource = SCS_SceneColorHDR;
    Fixture->Capture->ProjectionType = ECameraProjectionMode::Orthographic;
    Fixture->Capture->OrthoWidth = 160.0f;
    Fixture->Capture->bCaptureEveryFrame = false;
    Fixture->Capture->bCaptureOnMovement = false;
    Fixture->Capture->SetWorldLocation(FVector(-300.0, 0.0, 0.0));
    Fixture->Capture->SetWorldRotation(FRotator::ZeroRotator);
    Fixture->Capture->RegisterComponent();
    // Registration restores archetype flags, so deterministic overrides belong here.
    Fixture->Capture->ShowFlags.SetAntiAliasing(false);
    Fixture->Capture->ShowFlags.SetEyeAdaptation(false);
    Fixture->Capture->ShowFlags.SetTonemapper(false);
    Fixture->Capture->ShowFlags.SetBloom(false);
    Fixture->Capture->ShowFlags.SetGlobalIllumination(false);
    Fixture->Capture->ShowFlags.SetLumenGlobalIllumination(false);
    Fixture->Capture->ShowFlags.SetLumenReflections(false);
    Fixture->Capture->ShowFlags.SetIndirectLightingCache(false);
    Fixture->Capture->ShowFlags.SetReflectionEnvironment(false);
    Fixture->Capture->ShowFlags.SetSkyLighting(false);
    Fixture->Baseline = Fixture->CaptureMean(*this, TEXT("Baseline"));
    if (TestTrue(TEXT("Native deferred Toon lighting produces bright baseline"), Fixture->Baseline > 0.02) == false)
    { return false; }
    // At NoL=1, a threshold of one places the front face at smoothstep's midpoint.
    // The resulting half-bright band proves the MID parameter crosses the actual GBuffer
    // CustomData transport and controls CkUsf_ToonBxDF, independently of the reload gain.
    Fixture->Mid->SetScalarParameterValue(TEXT("ToonProbeThreshold"), 1.0f);
    const auto HalfBand = Fixture->CaptureMean(*this, TEXT("HalfBand"));
    Fixture->Mid->SetScalarParameterValue(TEXT("ToonProbeThreshold"), 0.5f);
    const auto FullBandRestored = Fixture->CaptureMean(*this, TEXT("FullBandRestored"));
    if (TestTrue(TEXT("MID threshold produces the expected half-bright native Toon band"),
        HalfBand >= Fixture->Baseline * 0.4 && HalfBand <= Fixture->Baseline * 0.6) == false ||
        TestTrue(TEXT("Restored MID threshold recovers original native Toon band"),
            FMath::Abs(FullBandRestored - Fixture->Baseline) <= Fixture->Baseline * 0.05) == false)
    { return false; }
    Fixture->Light->SetVisibility(false);
    const auto NoLight = Fixture->CaptureMean(*this, TEXT("NoLight"));
    Fixture->Light->SetVisibility(true);
    if (TestTrue(TEXT("Baseline requires actual direct lighting, not emissive or ambient"),
        NoLight >= 0.0 && NoLight < Fixture->Baseline * 0.1) == false)
    { return false; }
    GLog->AddOutputDevice(&Fixture.Get());
    Fixture->Observing = true;
    // Arm restoration before writing: a failed write may still have partially changed the file.
    Fixture->Modified = true;
    Fixture->RecoveryNeeded = true;
    if (TestTrue(TEXT("Only deferred lighting gain changed on disk"), FFileHelper::SaveStringToFile(
        Source, *Fixture->ShaderFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) == false)
    { return false; }
    if (TestTrue(TEXT("Production shading-model reload dispatched"), Fixture->DispatchReload()) == false)
    { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FReloadSequence(*this, Fixture));
    return true;
#else
    using namespace ck_test_usf_toon_reload;
    const auto Fixture = MakeShared<FFixture>();
    constexpr auto SourceSelectedToon = false;
    if (NOT PrepareUnsupportedReload(*this, Fixture, SourceSelectedToon))
    { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FRejectedReloadSequence(*this, Fixture));
    return true;
#endif
}

#endif // WITH_EDITOR
