#if WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Macros/CkMacros.h"
#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"
#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "AssetCompilingManager.h"
#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShaderCore.h"
#include "ShaderCompiler.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_usf_profile_reload
{
    auto Drain() -> void
    {
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (GShaderCompilingManager != nullptr) { GShaderCompilingManager->FinishAllCompilation(); }
        FlushRenderingCommands();
    }

    auto FindCustom(UMaterial& InMaterial) -> TWeakObjectPtr<UMaterialExpressionCustom>
    {
        for (const auto& Expression : InMaterial.GetExpressions())
        {
            const auto Custom = TWeakObjectPtr<UMaterialExpressionCustom>{Cast<UMaterialExpressionCustom>(Expression)};
            if (Custom.IsValid() && Custom->Code.StartsWith(TEXT("FCkUsf_SurfaceInput In = CkUsf_DefaultInput();"))) { return Custom; }
        }
        return {};
    }

    struct FFixture
    {
        FScopedDisableValidateOnSave DisableValidation;
        FString Root, Directory, VirtualRoot, ActiveSource, OtherSource, OtherPath;
        TStrongObjectPtr<UCkUsf_LookDefinition> ActiveDefinition, OtherDefinition;
        TStrongObjectPtr<UMaterial> Active, Other;
        TStrongObjectPtr<UMaterialInstanceDynamic> Mid;
        TWeakObjectPtr<UMaterialExpressionCustom> OtherCustom;
        TArray<FCk_Usf_ParamDesc> OtherParameters;
        TArray<FString> OtherIncludes;
        TArray<FCustomOutput> OtherOutputs;
        FString OtherOwner;
        ECk_Usf_SurfaceGraph OtherGraph = ECk_Usf_SurfaceGraph::Legacy;
        FString OtherIncludePath;
        FName OtherFunction;
        bool OtherDirectional = false;
        bool OtherFuzz = false;
        int32 AutoReload = 0;
        EConsoleVariableFlags AutoPriority = ECVF_SetByConstructor;
        bool RestoreAutoReload = false;
        bool OtherSnapshotReady = false;

        FFixture()
        {
            const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Root = TEXT("/CkUsfProfileReload_") + Id + TEXT("/");
            Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CkUsfProfileReload") / Id);
            VirtualRoot = TEXT("/CkUsf/ProfileReload_") + Id;
            IFileManager::Get().MakeDirectory(*(Directory / TEXT("Packages")), true);
            FPackageName::RegisterMountPoint(Root, Directory / TEXT("Packages/"));
            AddShaderSourceDirectoryMapping(VirtualRoot, Directory);
            ActiveDefinition.Reset(NewObject<UCkUsf_LookDefinition>());
            ActiveDefinition->_LookName = TEXT("ProfileReloadActive");
            ActiveDefinition->_GeneratedPackageRoot = Root.LeftChop(1);
            ActiveDefinition->_Domain = ECk_Usf_Domain::SurfaceLit;
            ActiveDefinition->_UshIncludePath = VirtualRoot / TEXT("Active.ush");
            ActiveDefinition->_UshFunctionName = TEXT("CkUsf_ProfileReloadActive");
            auto Gain = FCk_Usf_ParamDesc{};
            Gain._Name = TEXT("Gain"); Gain._DefaultScalar = 0.4f;
            ActiveDefinition->_Parameters = {Gain};
        }

        auto Setup(FAutomationTestBase& Test) -> bool
        {
            using namespace ck::usf_editor;
            AutoReload = IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->GetInt();
            AutoPriority = static_cast<EConsoleVariableFlags>(IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->GetFlags() & ECVF_SetByMask);
            RestoreAutoReload = true;
            IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->Set(0, AutoPriority);
            const auto Support = Get_SubstrateShadingModelSupport();
            ActiveDefinition->_SurfaceGraph = Support.BlendableGBuffer ? ECk_Usf_SurfaceGraph::NativeSlab : ECk_Usf_SurfaceGraph::AdaptiveMix;
            ActiveSource = Support.BlendableGBuffer
                ? TEXT("#pragma once\n#include \"/CkUsf/Common.ush\"\nFCkUsf_SlabOutput CkUsf_ProfileReloadActive(FCkUsf_SurfaceInput In, float Gain) { FCkUsf_SlabOutput O=CkUsf_DefaultSlab(); O.DiffuseAlbedo=Gain.xxx; return O; }\n")
                : TEXT("#pragma once\n#include \"/CkUsf/Common.ush\"\nFCkUsf_AdaptiveMixOutput CkUsf_ProfileReloadActive(FCkUsf_SurfaceInput In, float Gain) { FCkUsf_AdaptiveMixOutput O=CkUsf_DefaultAdaptiveMix(); O.ADiffuseAlbedo=Gain.xxx; return O; }\n");
            if (NOT Test.TestTrue(TEXT("Write owned active-profile source"), FFileHelper::SaveStringToFile(ActiveSource, *(Directory / TEXT("Active.ush")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) { return false; }
            Active.Reset(Generate_LookMaterial(ActiveDefinition.Get()));
            if (NOT Test.TestTrue(TEXT("Generate active-profile master"), Active.IsValid())) { return false; }
            Mid.Reset(UMaterialInstanceDynamic::Create(Active.Get(), GetTransientPackage()));
            if (NOT Test.TestTrue(TEXT("Active MID exists"), Mid.IsValid())) { return false; }
            Mid->SetScalarParameterValue(TEXT("Gain"), 0.75f);

            const auto PersistentPath = Support.BlendableGBuffer
                ? TEXT("/CkFoundation/CkUsf/GeneratedLooks/M_CkUsf_Look_AdaptiveCandyPaint.M_CkUsf_Look_AdaptiveCandyPaint")
                : TEXT("/CkFoundation/CkUsf/GeneratedLooks/M_CkUsf_Look_NativeRubyVelvet.M_CkUsf_Look_NativeRubyVelvet");
            const auto Persistent = TStrongObjectPtr<UMaterial>{LoadObject<UMaterial>(nullptr, PersistentPath)};
            if (NOT Test.TestTrue(TEXT("Prepared persistent opposite-profile showcase exists (read-only backing)"), Persistent.IsValid())) { return false; }
            const auto Owner = Persistent->GetOutermost()->GetMetaData().GetValue(Persistent.Get(), TEXT("CkUsf.SourceDefinition.v1"));
            const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{LoadObject<UCkUsf_LookDefinition>(nullptr, *Owner)};
            if (NOT Test.TestTrue(TEXT("Persistent opposite-profile owner exists"), Definition.IsValid())) { return false; }
            OtherDefinition.Reset(DuplicateObject<UCkUsf_LookDefinition>(Definition.Get(), GetTransientPackage()));
            const auto Package = TStrongObjectPtr<UPackage>{CreatePackage(*(Root + TEXT("Other")))};
            // Duplication preserves expression GUIDs and does not compile or save a material.
            Other.Reset(DuplicateObject<UMaterial>(Persistent.Get(), Package.Get(), TEXT("M_Other")));
            if (NOT Test.TestTrue(TEXT("Owned opposite-profile clone exists"), Other.IsValid() && OtherDefinition.IsValid())) { return false; }
            OtherPath = Other->GetPathName();
            OtherCustom = FindCustom(*Other);
            if (NOT Test.TestTrue(TEXT("Owned opposite-profile Custom exists"), OtherCustom.IsValid())) { return false; }
            const auto OriginalInclude = OtherDefinition->_UshIncludePath;
            if (NOT Test.TestTrue(TEXT("Read opposite source without editing shipped bytes"), FFileHelper::LoadFileToString(OtherSource, *GetShaderSourceFilePath(OriginalInclude)))) { return false; }
            if (NOT Test.TestTrue(TEXT("Copy opposite source into owned Saved fixture"), FFileHelper::SaveStringToFile(OtherSource, *(Directory / TEXT("Other.ush")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) { return false; }
            OtherDefinition->_UshIncludePath = VirtualRoot / TEXT("Other.ush");
            for (auto& Include : OtherCustom->IncludeFilePaths)
            { if (Include == OriginalInclude) { Include = OtherDefinition->_UshIncludePath; } }
            auto Signature = Persistent->GetOutermost()->GetMetaData().GetValue(Persistent.Get(), TEXT("CkUsf.NativeSurfaceGraph.v1"));
            Signature = Signature.Replace(*Persistent->GetPathName(), *OtherPath)
                .Replace(*(TEXT("Include=") + OriginalInclude), *(TEXT("Include=") + OtherDefinition->_UshIncludePath));
            auto& Metadata = Other->GetOutermost()->GetMetaData();
            Metadata.SetValue(Other.Get(), TEXT("CkUsf.NativeSurfaceGraph.v1"), *Signature);
            Metadata.SetValue(Other.Get(), TEXT("CkUsf.SourceDefinition.v1"), *OtherDefinition->GetPathName());
            Metadata.SetValue(Other.Get(), TEXT("CkUsf.SourceSelection.v1"), TEXT("1"));
            OtherParameters = OtherDefinition->_Parameters;
            if (NOT Test.TestTrue(TEXT("Opposite fixture has parameters and at least two outputs"), NOT OtherParameters.IsEmpty() && OtherCustom->AdditionalOutputs.Num() >= 2)) { return false; }
            OtherIncludes = OtherCustom->IncludeFilePaths;
            OtherOutputs = OtherCustom->AdditionalOutputs;
            OtherOwner = OtherDefinition->GetPathName();
            OtherGraph = OtherDefinition->_SurfaceGraph;
            OtherIncludePath = OtherDefinition->_UshIncludePath;
            OtherFunction = OtherDefinition->_UshFunctionName;
            OtherDirectional = OtherDefinition->_DirectionalShading;
            OtherFuzz = OtherDefinition->_SlabFuzz;
            OtherSnapshotReady = true;
            auto Prepared = FPreparedLookShadingModelReload{};
            auto Reason = FString{};
            auto Errors = TArray<FString>{};
            const auto Admission = Prepare_LookShadingModelReloadForBatch(*Other, Prepared, Reason, Errors);
            for (const auto& Error : Errors) { Test.AddError(Error); }
            if (NOT Test.TestTrue(TEXT("Owned opposite clone fully validates before exclusion"), Admission == ELookShadingModelReloadAdmission::OtherRendererProfile && NOT Prepared.Custom.IsValid() && NOT Reason.IsEmpty())) { return false; }
            Test.TestTrue(TEXT("An all-excluded preparation contains no runnable work"), Prepared.Code.IsEmpty() && NOT Prepared.Custom.IsValid() && Errors.IsEmpty());
            Drain();
            return true;
        }

        auto RestoreOther() -> void
        {
            if (NOT OtherSnapshotReady || NOT Other.IsValid() || NOT OtherDefinition.IsValid() || NOT OtherCustom.IsValid()) { return; }
            OtherDefinition->_Parameters = OtherParameters;
            OtherDefinition->_SurfaceGraph = OtherGraph;
            OtherDefinition->_UshIncludePath = OtherIncludePath;
            OtherDefinition->_UshFunctionName = OtherFunction;
            OtherDefinition->_DirectionalShading = OtherDirectional;
            OtherDefinition->_SlabFuzz = OtherFuzz;
            OtherCustom->IncludeFilePaths = OtherIncludes;
            OtherCustom->AdditionalOutputs = OtherOutputs;
            Other->GetOutermost()->GetMetaData().SetValue(Other.Get(), TEXT("CkUsf.SourceDefinition.v1"), *OtherOwner);
            const auto Vertical = TObjectPtr<UMaterialExpressionSubstrateVerticalLayering>{Cast<UMaterialExpressionSubstrateVerticalLayering>(Other->GetEditorOnlyData()->FrontMaterial.Expression)};
            if (Vertical != nullptr) { Vertical->bUseParameterBlending = false; }
        }

        ~FFixture()
        {
            RestoreOther();
            Drain();
            for (const auto& Material : TArray<TObjectPtr<UMaterial>>{Active.Get(), Other.Get()})
            {
                if (Material == nullptr) { continue; }
                for (const auto& Expression : Material->GetExpressions())
                {
                    const auto Custom = TObjectPtr<UMaterialExpressionCustom>{Cast<UMaterialExpressionCustom>(Expression)};
                    if (Custom != nullptr) { Custom->IncludeFilePaths.Reset(); }
                }
                Material->UpdateCachedExpressionData();
                Material->ClearFlags(RF_Public | RF_Standalone);
                Material->MarkAsGarbage();
            }
            if (RestoreAutoReload)
            { IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->Set(AutoReload, AutoPriority); }
            FPackageName::UnRegisterMountPoint(Root, Directory / TEXT("Packages/"));
        }
    };

    class FSequence final : public IAutomationLatentCommand, public FOutputDevice
    {
    public:
        FSequence(FAutomationTestBase& InTest, TSharedRef<FFixture> InFixture) : Test(InTest), Fixture(InFixture)
        { GLog->AddOutputDevice(this); }
        ~FSequence() override { GLog->RemoveOutputDevice(this); }

        auto Serialize(const TCHAR* Message, ELogVerbosity::Type, const FName&) -> void override
        {
            const auto Text = FString{Message};
            if (Text.Contains(TEXT("CkUsf reload profile exclusion:")) && Text.Contains(Fixture->OtherPath)) { Excluded.Store(true); }
            if (Text.Contains(TEXT("CkUsf reload complete:"))) { Completion = Text; Completed.Store(true); }
            if (Text.Contains(TEXT("CkUsf reload batch rejected:"))) { Rejected.Store(true); }
        }

        auto Update() -> bool override
        {
            using namespace ck::usf_editor;
            if (NOT Started)
            {
                Fixture->RestoreOther();
                if (Stage == 1) { Fixture->OtherDefinition->_Parameters.Add(Fixture->OtherParameters[0]); }
                if (Stage == 2)
                {
                    const auto Vertical = TObjectPtr<UMaterialExpressionSubstrateVerticalLayering>{Cast<UMaterialExpressionSubstrateVerticalLayering>(Fixture->Other->GetEditorOnlyData()->FrontMaterial.Expression)};
                    if (Vertical != nullptr) { Vertical->bUseParameterBlending = true; }
                    else { FindCustom(*Fixture->Other)->AdditionalOutputs.Swap(0, 1); }
                }
                if (Stage == 3) { Fixture->Other->GetOutermost()->GetMetaData().SetValue(Fixture->Other.Get(), TEXT("CkUsf.SourceDefinition.v1"), *(Fixture->Root + TEXT("MissingOwner.MissingOwner"))); }
                if (Stage == 4)
                {
                    Fixture->OtherDefinition->_SurfaceGraph = Fixture->ActiveDefinition->_SurfaceGraph;
                    Fixture->OtherDefinition->_UshIncludePath = Fixture->ActiveDefinition->_UshIncludePath;
                    Fixture->OtherDefinition->_UshFunctionName = Fixture->ActiveDefinition->_UshFunctionName;
                    Fixture->OtherDefinition->_Parameters = Fixture->ActiveDefinition->_Parameters;
                    Fixture->OtherDefinition->_DirectionalShading = false;
                    Fixture->OtherDefinition->_SlabFuzz = false;
                }
                if (Stage == 5) { Fixture->OtherCustom->IncludeFilePaths.Add(Fixture->VirtualRoot / TEXT("Missing.ush")); }
                BeforeActive = Fixture->Active->StateId; BeforeOther = Fixture->Other->StateId;
                OtherCode = Fixture->OtherCustom->Code;
                Completed.Store(false); Rejected.Store(false); Excluded.Store(false);
                Deadline = FPlatformTime::Seconds() + 90.0;
                if (NOT Test.TestTrue(TEXT("Dispatch actual production material reload"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Ck_Usf_ReloadShaders"), *GLog, GEditor->GetEditorWorldContext().World()))) { return true; }
                Started = true;
                return false;
            }
            const auto Acknowledged = Stage == 0 ? Completed.Load() : Rejected.Load();
            if (NOT Acknowledged)
            {
                if (FPlatformTime::Seconds() < Deadline) { return false; }
                Test.AddError(TEXT("Production profile-aware reload failed to acknowledge within 90 seconds.")); return true;
            }
            Test.TestEqual(TEXT("Excluded/invalid opposite master retains StateId"), Fixture->Other->StateId, BeforeOther);
            Test.TestEqual(TEXT("Excluded/invalid opposite Custom code is unchanged"), Fixture->OtherCustom->Code, OtherCode);
            Test.TestTrue(TEXT("Active MID retains parent and scalar override"), Fixture->Mid->Parent == Fixture->Active.Get() && FMath::IsNearlyEqual(Fixture->Mid->K2_GetScalarParameterValue(TEXT("Gain")), 0.75f));
            if (Stage == 0)
            {
                Test.TestTrue(TEXT("Compatible master reloads despite loaded valid opposite graph"), Fixture->Active->StateId != BeforeActive);
                Test.TestTrue(TEXT("Completion explicitly reports zero failures"), Completion.Contains(TEXT("[0] failed")));
                Test.TestTrue(TEXT("Named opposite-profile exclusion is acknowledged"), Excluded.Load());
            }
            else
            {
                Test.TestEqual(TEXT("One invalid opposite candidate rejects compatible master atomically"), Fixture->Active->StateId, BeforeActive);
                Test.TestFalse(TEXT("Rejected batch does not report successful completion"), Completed.Load());
            }
            Fixture->RestoreOther();
            ++Stage; Started = false;
            return Stage == 6;
        }
    private:
        FAutomationTestBase& Test;
        TSharedRef<FFixture> Fixture;
        TAtomic<bool> Completed{false}, Rejected{false}, Excluded{false};
        FString Completion, OtherCode;
        FGuid BeforeActive, BeforeOther;
        double Deadline = 0.0;
        int32 Stage = 0;
        bool Started = false;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ProfileAwareReload, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.ProfileAwareReload.ProductionBatchAdmissionAndAtomicRejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_ProfileAwareReload::RunTest(const FString&) -> bool
{
    if (NOT FApp::CanEverRender() || GEditor == nullptr) { AddError(TEXT("Profile-aware reload requires a real editor RHI.")); return false; }
    const auto Fixture = MakeShared<ck_test_usf_profile_reload::FFixture>();
    if (NOT Fixture->Setup(*this)) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(ck_test_usf_profile_reload::FSequence(*this, Fixture));
    return true;
}

#endif
