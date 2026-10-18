#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AssetCompilingManager.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/PlatformTime.h"
#include "RenderingThread.h"
#include "ShaderCore.h"
#include "TextureResource.h"
#include "Templates/Atomic.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

// --------------------------------------------------------------------------------------------------------------------

namespace
{
    struct FReloadFixture : FOutputDevice
    {
        TStrongObjectPtr<UMaterial> Material;
        TStrongObjectPtr<UMaterial> Unrelated;
        TStrongObjectPtr<UMaterialExpressionCustom> Custom;
        TStrongObjectPtr<UMaterialInstanceDynamic> Mid;
        TStrongObjectPtr<UTextureRenderTarget2D> Target;
        FString RootFile;
        FString FixtureFile;
        FString Directory;
        FGuid BeforeReload;
        int32 PreviousAutoReload = 0;
        EConsoleVariableFlags AutoReloadPriority = ECVF_SetByConstructor;
        bool RestoreAutoReload = false;
        FString RejectionMessage;
        TAtomic<bool> RejectionObserved{false};
        bool ObservingRejection = false;

        auto
            Serialize(const TCHAR* InMessage, ELogVerbosity::Type InVerbosity, const FName& InCategory)
            -> void override
        {
            // Automation can downgrade an expected warning before later output devices see it.
            if (FString{InMessage}.Contains(RejectionMessage))
            { RejectionObserved.Store(true); }
        }

        ~FReloadFixture()
        {
            if (ObservingRejection)
            { GLog->RemoveOutputDevice(this); }
            if (RestoreAutoReload && IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload")) != nullptr)
            {
                IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->Set(
                    PreviousAutoReload, AutoReloadPriority);
            }
            if (Custom.IsValid() && Material.IsValid())
            {
                Custom->IncludeFilePaths.Reset();
                Material->UpdateCachedExpressionData();
            }
            IFileManager::Get().Delete(*RootFile, false, true);
            IFileManager::Get().Delete(*FixtureFile, false, true);
            IFileManager::Get().DeleteDirectory(*Directory, false, false);
            FlushShaderFileCache();
        }
    };

    class FReloadSequence final : public IAutomationLatentCommand
    {
    public:
        FReloadSequence(FAutomationTestBase& InTest, TFunction<bool(int32)> InStart,
            TFunction<bool(int32)> InReady)
            : Test(InTest), Start(MoveTemp(InStart)), Ready(MoveTemp(InReady))
        {}

        auto
            Update()
            -> bool override
        {
            if (Started == false)
            {
                if (Start(Stage) == false)
                { return true; }
                Deadline = FPlatformTime::Seconds() + 60.0;
                Started = true;
                return false;
            }
            if (Ready(Stage))
            {
                ++Stage;
                Started = false;
                return Stage == 4;
            }
            if (FPlatformTime::Seconds() > Deadline)
            {
                Test.AddError(FString::Printf(TEXT("Timed out waiting for reload test stage %d (watcher, manual, missing-source rejection, identical-byte restoration)."), Stage));
                return true;
            }
            return false;
        }

    private:
        FAutomationTestBase& Test;
        TFunction<bool(int32)> Start;
        TFunction<bool(int32)> Ready;
        int32 Stage = 0;
        bool Started = false;
        double Deadline = 0.0;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_ShaderReload,
    "CkTests.UnitTests.CkUsf.ShaderReloadUpdatesExistingMID",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

auto
    FCkTest_Usf_ShaderReload::RunTest(const FString& Parameters)
    -> bool
{
    if (FApp::CanEverRender() == false || GEditor == nullptr)
    {
        AddError(TEXT("Shader reload pixel verification requires an editor with a real RHI."));
        return false;
    }

    const auto World = TObjectPtr<UWorld>{GEditor->GetEditorWorldContext().World()};
    if (TestNotNull(TEXT("Editor world exists"), World.Get()) == false)
    { return false; }

    if (TestNotNull(TEXT("Production reload command is registered"),
        IConsoleManager::Get().FindConsoleObject(TEXT("Ck_Usf_ReloadShaders"))) == false)
    { return false; }

    // A unique fixture inside the existing mapping exercises actual file/hash caches without
    // overwriting shipped shaders or registering/removing a process-global directory mapping.
    const auto& Mappings = AllShaderSourceDirectoryMappings();
    if (TestTrue(TEXT("Production /CkUsf mapping exists"), Mappings.Contains(TEXT("/CkUsf"))) == false)
    { return false; }
    const auto FixtureName = FString::Printf(TEXT("ReloadTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const auto FixtureDirectory = FPaths::Combine(Mappings.FindChecked(TEXT("/CkUsf")), FixtureName);
    const auto FixtureFile = FPaths::Combine(FixtureDirectory, TEXT("Color.ush"));
    const auto RootFile = FPaths::Combine(FixtureDirectory, TEXT("Root.ush"));
    const auto VirtualFile = FString::Printf(TEXT("/CkUsf/%s/Root.ush"), *FixtureName);

    const auto Package = TStrongObjectPtr<UPackage>{CreatePackage(*FString::Printf(TEXT("/Temp/%s"), *FixtureName))};
    Package->SetFlags(RF_Transient);
    const auto Material = TStrongObjectPtr<UMaterial>{NewObject<UMaterial>(Package.Get(), NAME_None, RF_Transient)};
    const auto Unrelated = TStrongObjectPtr<UMaterial>{NewObject<UMaterial>(Package.Get(), NAME_None, RF_Transient)};
    const auto Custom = TObjectPtr<UMaterialExpressionCustom>{NewObject<UMaterialExpressionCustom>(Material.Get())};
    if (TestNotNull(TEXT("Custom expression created"), Custom.Get()) == false)
    { return false; }

    const auto Fixture = MakeShared<FReloadFixture>();
    Fixture->Material.Reset(Material.Get());
    Fixture->Unrelated.Reset(Unrelated.Get());
    Fixture->Custom.Reset(Custom.Get());
    Fixture->RootFile = RootFile;
    Fixture->FixtureFile = FixtureFile;
    Fixture->Directory = FixtureDirectory;
    Fixture->RejectionMessage = FString::Printf(
        TEXT("CkUsf reload rejected: shader source is absent or unreadable [/CkUsf/%s/Color.ush]"), *FixtureName);
    if (TestNotNull(TEXT("Automatic reload CVar is registered"),
        IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))) == false)
    { return false; }
    Fixture->PreviousAutoReload = IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->GetInt();
    Fixture->AutoReloadPriority = static_cast<EConsoleVariableFlags>(
        IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->GetFlags() & ECVF_SetByMask);
    Fixture->RestoreAutoReload = true;
    IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->Set(1, Fixture->AutoReloadPriority);

    if (TestTrue(TEXT("Unique fixture directory created"),
        IFileManager::Get().MakeDirectory(*FixtureDirectory)) == false)
    { return false; }

    const auto Write_Color = [this, FixtureFile](const TCHAR* InColor) -> bool
    {
        return TestTrue(TEXT("Fixture shader saved"), FFileHelper::SaveStringToFile(
            *FString::Printf(TEXT("float3 CkUsfReloadTestColor() { return float3(%s); }\n"), InColor),
            *FixtureFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    };
    if (Write_Color(TEXT("1, 0, 0")) == false)
    { return false; }
    if (TestTrue(TEXT("Root fixture includes nested shader"), FFileHelper::SaveStringToFile(
        *FString::Printf(TEXT("#include \"/CkUsf/%s/Color.ush\"\n"), *FixtureName),
        *RootFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) == false)
    { return false; }

    Custom->IncludeFilePaths.Add(VirtualFile);
    const auto Gain = TObjectPtr<UMaterialExpressionScalarParameter>{NewObject<UMaterialExpressionScalarParameter>(Material.Get())};
    Gain->ParameterName = TEXT("ReloadGain");
    Gain->DefaultValue = 1.0f;
    Material->GetExpressionCollection().AddExpression(Gain.Get());
    auto GainInput = FCustomInput{};
    GainInput.InputName = TEXT("Gain");
    GainInput.Input.Connect(0, Gain.Get());
    Custom->Inputs.Reset();
    Custom->Inputs.Add(GainInput);
    Custom->Code = TEXT("return CkUsfReloadTestColor() * Gain;");
    Custom->OutputType = CMOT_Float3;
    Material->SetShadingModel(MSM_Unlit);
    Material->GetExpressionCollection().AddExpression(Custom.Get());
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Custom.Get());
    FlushShaderFileCache();
    Material->PostEditChange();
    Unrelated->PostEditChange();
    FAssetCompilingManager::Get().FinishAllCompilation();
    FlushRenderingCommands();
    Material->GetOutermost()->SetDirtyFlag(false);
    const auto UnrelatedStateId = Unrelated->StateId;

    const auto Mid = TStrongObjectPtr<UMaterialInstanceDynamic>{
        UMaterialInstanceDynamic::Create(Material.Get(), GetTransientPackage())};
    const auto Target = TStrongObjectPtr<UTextureRenderTarget2D>{
        NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient)};
    if (TestNotNull(TEXT("MID created"), Mid.Get()) == false)
    { return false; }
    Mid->SetScalarParameterValue(TEXT("ReloadGain"), 0.5f);
    Target->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
    Target->InitAutoFormat(8, 8);
    Target->UpdateResourceImmediate(true);
    Fixture->Mid.Reset(Mid.Get());
    Fixture->Target.Reset(Target.Get());

    const auto Check_Color = [this, Fixture, World](const TCHAR* InStage, const FLinearColor& InExpected) -> bool
    {
        const auto& Target = Fixture->Target;
        const auto& Mid = Fixture->Mid;
        UKismetRenderingLibrary::ClearRenderTarget2D(World.Get(), Target.Get(), FLinearColor::Black);
        UKismetRenderingLibrary::DrawMaterialToRenderTarget(World.Get(), Target.Get(), Mid.Get());
        FlushRenderingCommands();
        auto Pixels = TArray<FLinearColor>{};
        if (TestNotNull(TEXT("Render target resource exists"), Target->GameThread_GetRenderTargetResource()) == false)
        { return false; }
        if (TestTrue(TEXT("Actual render target pixels read"),
            Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels)) == false)
        { return false; }
        if (TestFalse(TEXT("Pixel readback is nonempty"), Pixels.IsEmpty()) == false)
        { return false; }
        const auto& Pixel = Pixels[Pixels.Num() / 2];
        AddInfo(FString::Printf(TEXT("%s rendered RGB=(%.3f, %.3f, %.3f)"), InStage, Pixel.R, Pixel.G, Pixel.B));
        return TestTrue(InStage, FMath::IsNearlyEqual(Pixel.R, InExpected.R, 0.05f) &&
            FMath::IsNearlyEqual(Pixel.G, InExpected.G, 0.05f) &&
            FMath::IsNearlyEqual(Pixel.B, InExpected.B, 0.05f));
    };

    if (Check_Color(TEXT("Initial include renders red with MID gain"), FLinearColor{0.5f, 0.0f, 0.0f}) == false)
    { return false; }

    ADD_LATENT_AUTOMATION_COMMAND(FReloadSequence(*this,
        [this, Fixture, Write_Color, World](int32 InStage) -> bool
        {
            Fixture->BeforeReload = Fixture->Material->StateId;
            if (InStage == 2)
            {
                AddExpectedMessage(Fixture->RejectionMessage, ELogVerbosity::Warning,
                    EAutomationExpectedMessageFlags::Contains, 1, false);
                GLog->AddOutputDevice(&Fixture.Get());
                Fixture->ObservingRejection = true;
                return TestTrue(TEXT("Only nested fixture include deleted"),
                    IFileManager::Get().Delete(*Fixture->FixtureFile, true, true));
            }
            if (Write_Color(InStage == 0 ? TEXT("0, 1, 0") : TEXT("0, 0, 1")) == false)
            { return false; }
            // First edit relies solely on the real directory watcher. The second also exercises
            // the public manual command; both must yield frames for production ticker dispatch.
            return InStage != 1 || TestTrue(TEXT("Production reload command dispatched"),
                IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Ck_Usf_ReloadShaders"), *GLog, World.Get()));
        },
        [this, Fixture, UnrelatedStateId, Check_Color](int32 InStage) -> bool
        {
            if (InStage == 2)
            {
                if (Fixture->RejectionObserved.Load() == false)
                { return false; }
                TestTrue(TEXT("Missing nested include is rejected without material mutation"),
                    Fixture->Material->StateId == Fixture->BeforeReload);
                TestFalse(TEXT("Rejected reload starts no fixture shader compilation"), Fixture->Material->IsCompiling());
                TestTrue(TEXT("Rejected reload leaves unrelated material unchanged"),
                    Fixture->Unrelated->StateId == UnrelatedStateId);
                TestFalse(TEXT("Rejected reload does not dirty fixture package"),
                    Fixture->Material->GetOutermost()->IsDirty());
                Check_Color(TEXT("Rejected missing include preserves previous blue shader and MID gain"),
                    FLinearColor{0.0f, 0.0f, 0.5f});
                return true;
            }
            if (Fixture->Material->StateId == Fixture->BeforeReload)
            { return false; }
            FAssetCompilingManager::Get().FinishAllCompilation();
            FlushRenderingCommands();
            TestTrue(TEXT("Unrelated material StateId unchanged"), Fixture->Unrelated->StateId == UnrelatedStateId);
            TestFalse(TEXT("Reload does not dirty material package"), Fixture->Material->GetOutermost()->IsDirty());
            TestTrue(TEXT("Existing MID retains the original parent"), Fixture->Mid->Parent == Fixture->Material.Get());
            Check_Color(InStage == 0 ? TEXT("Watcher reload renders green and retains MID gain") : InStage == 1 ?
                TEXT("Manual reload renders blue and retains MID gain") :
                TEXT("Restoring identical nested include bytes triggers automatic recovery and preserves MID gain"), InStage == 0 ?
                FLinearColor{0.0f, 0.5f, 0.0f} : FLinearColor{0.0f, 0.0f, 0.5f});
            return true;
        }));
    return true;
}

#endif // WITH_EDITOR
