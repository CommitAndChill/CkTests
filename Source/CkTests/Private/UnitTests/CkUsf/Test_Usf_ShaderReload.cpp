#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "CkUsfEditor/Reload/CkUsf_ShaderReload.h"
#include "CkUsfEditor_Log.h"

#include "AssetCompilingManager.h"
#include "CoreGlobals.h"
#include "DirectoryWatcherModule.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/CriticalSection.h"
#include "HAL/IConsoleManager.h"
#include "IDirectoryWatcher.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Logging/LogScopedVerbosityOverride.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
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
    constexpr auto NegativeObservationSeconds = 1.0;

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
        FString ColorRejectionMessage;
        TAtomic<bool> RejectionObserved{false};
        TAtomic<int32> ReloadStarts{0};
        TAtomic<int32> ReloadSuccesses{0};
        TAtomic<int32> ReloadFailures{0};
        bool ObservingRejection = false;
        FString Scope;
        FString ExpectedEventFile;
        TArray<FString> ExtraFiles;
        TArray<FString> ScopedWarnings;
        TMap<FString, int32> EventCounts;
        TMap<FString, double> EventTimes;
        FCriticalSection ObservationMutex;
        FDelegateHandle DirectoryObserver;
        int32 EventsBefore = 0;
        int32 WarningsBefore = 0;
        int32 ReloadStartsBefore = 0;
        int32 ReloadSuccessesBefore = 0;
        int32 ReloadFailuresBefore = 0;
        bool ManualReloadRequested = false;
        bool IdenticalSaveRequested = false;
        FGuid AcceptedState;
        const FMaterialResource* AcceptedResource = nullptr;
        TUniquePtr<FLogScopedVerbosityOverride> LogVerbosity;

        auto
            Serialize(const TCHAR* InMessage, ELogVerbosity::Type InVerbosity, const FName& InCategory)
            -> void override
        {
            // Automation can downgrade an expected warning before later output devices see it.
            const auto Message = FString{InMessage};
            if (Message.Contains(TEXT("CkUsf reload started:")))
            { ++ReloadStarts; }
            if (Message.Contains(TEXT("CkUsf reload complete:")))
            {
                if (Message.Contains(TEXT("master(s), [0] failed,")))
                { ++ReloadSuccesses; }
                else
                { ++ReloadFailures; }
            }
            FScopeLock Guard(&ObservationMutex);
            if (NOT RejectionMessage.IsEmpty() && Message.Contains(RejectionMessage))
            { RejectionObserved.Store(true); }
            if (Message.Contains(Scope) && Message.Contains(TEXT("CkUsf"))
                && (Message.Contains(TEXT("rejected")) || Message.Contains(TEXT("failed"))))
            { ScopedWarnings.Add(Message); }
        }

        auto
            BeginObservation(const FString& InExpectedFile, const FString& InRejectionMessage)
            -> void
        {
            FScopeLock Guard(&ObservationMutex);
            ExpectedEventFile = InExpectedFile;
            EventsBefore = EventCounts.FindRef(InExpectedFile);
            WarningsBefore = ScopedWarnings.Num();
            ReloadStartsBefore = ReloadStarts.Load();
            ReloadSuccessesBefore = ReloadSuccesses.Load();
            ReloadFailuresBefore = ReloadFailures.Load();
            RejectionMessage = InRejectionMessage;
            RejectionObserved.Store(false);
        }

        auto
            ObservedSuccessfulBatch() const
            -> bool
        {
            const auto Starts = ReloadStarts.Load();
            const auto Successes = ReloadSuccesses.Load();
            const auto Failures = ReloadFailures.Load();
            return Starts > ReloadStartsBefore && Successes > ReloadSuccessesBefore
                && Failures == ReloadFailuresBefore && Successes + Failures == Starts;
        }

        auto
            ObservedNegativeWindow()
            -> bool
        {
            FScopeLock Guard(&ObservationMutex);
            return EventCounts.FindRef(ExpectedEventFile) > EventsBefore
                && FPlatformTime::Seconds() - EventTimes.FindRef(ExpectedEventFile) >= NegativeObservationSeconds;
        }

        auto
            HasNewWarning(const FString& InSource = {})
            -> bool
        {
            FScopeLock Guard(&ObservationMutex);
            for (auto Index = WarningsBefore; Index < ScopedWarnings.Num(); ++Index)
            {
                if (InSource.IsEmpty() || ScopedWarnings[Index].Contains(InSource))
                { return true; }
            }
            return false;
        }

        ~FReloadFixture()
        {
            if (DirectoryObserver.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("DirectoryWatcher")))
            {
                auto* Watcher = FModuleManager::GetModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")).Get();
                if (Watcher != nullptr)
                { Watcher->UnregisterDirectoryChangedCallback_Handle(Directory, DirectoryObserver); }
            }
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
            for (const auto& File : ExtraFiles)
            { IFileManager::Get().Delete(*File, false, true); }
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
                return Stage == 19;
            }
            if (FPlatformTime::Seconds() > Deadline)
            {
                Test.AddError(FString::Printf(TEXT("Timed out waiting for shader reload stage %d (watcher, rejection, recovery, or owned-source event observation)."), Stage));
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

    struct FUnsupportedReloadFixture
    {
        TStrongObjectPtr<UPackage> Package;
        TStrongObjectPtr<UMaterial> Material;
        TStrongObjectPtr<UMaterialInstanceDynamic> Mid;
    };

    class FUnsupportedReloadCheck final : public IAutomationLatentCommand
    {
    public:
        explicit FUnsupportedReloadCheck(TFunction<void()> InCheck)
            : _Check(MoveTemp(InCheck)), _SubmittedFrame(GFrameCounter)
        {}

        auto
            Update()
            -> bool override
        {
            if (GFrameCounter == _SubmittedFrame)
            { return false; }
            _Check();
            return true;
        }

    private:
        TFunction<void()> _Check;
        uint64 _SubmittedFrame;
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
    if (NOT ck::usf_editor::FShaderReload::Get_IsSupported())
    {
        const TCHAR* Commands[] = {TEXT("Ck_Usf_ReloadShaders"), TEXT("Ck_Usf_ReloadGlobalShaders"), TEXT("Ck_Usf_ReloadShadingModels")};
        for (const auto* Command : Commands)
        {
            if (NOT TestNotNull(TEXT("Unsupported reload command remains registered"),
                IConsoleManager::Get().FindConsoleObject(Command)))
            { return false; }
        }
        const auto Fixture = MakeShared<FUnsupportedReloadFixture>();
        Fixture->Package.Reset(CreatePackage(*FString::Printf(TEXT("/Temp/UnsupportedShaderReload_%s"),
            *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
        Fixture->Package->SetFlags(RF_Transient);
        Fixture->Material.Reset(NewObject<UMaterial>(Fixture->Package.Get(), NAME_None, RF_Transient));
        const auto Gain = TObjectPtr<UMaterialExpressionScalarParameter>{NewObject<UMaterialExpressionScalarParameter>(Fixture->Material.Get())};
        Gain->ParameterName = TEXT("UnsupportedReloadGain");
        Gain->DefaultValue = 0.25f;
        Fixture->Material->GetExpressionCollection().AddExpression(Gain.Get());
        Fixture->Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Gain.Get());
        Fixture->Material->SetShadingModel(MSM_Unlit);
        Fixture->Material->UpdateCachedExpressionData();
        Fixture->Mid.Reset(UMaterialInstanceDynamic::Create(Fixture->Material.Get(), Fixture->Package.Get()));
        if (NOT TestNotNull(TEXT("Unsupported reload fixture creates a MID"), Fixture->Mid.Get()))
        { return false; }
        Fixture->Mid->SetScalarParameterValue(TEXT("UnsupportedReloadGain"), 0.5f);
        if (NOT TestTrue(TEXT("Unsupported fixture has a real retained MID parameter"),
            FMath::IsNearlyEqual(Fixture->Mid->K2_GetScalarParameterValue(TEXT("UnsupportedReloadGain")), 0.5f)))
        { return false; }
        FAssetCompilingManager::Get().FinishAllCompilation();
        FlushRenderingCommands();
        Fixture->Package->SetDirtyFlag(false);
        const auto StateId = Fixture->Material->StateId;
        const auto ShaderDevelopment = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderDevelopmentMode"));
        const auto DevelopmentMode = ShaderDevelopment != nullptr ? ShaderDevelopment->GetInt() : INDEX_NONE;
        AddExpectedMessagePlain(TEXT("CkUsf shader reload rejected: this engine lacks ShaderSourceDependencies API v1."),
            ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);
        for (const auto* Command : Commands)
        {
            TestTrue(TEXT("Actual unsupported reload command dispatches its explicit rejection"),
                IConsoleManager::Get().ProcessUserConsoleInput(Command, *GLog, nullptr));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FUnsupportedReloadCheck([this, Fixture, StateId, ShaderDevelopment, DevelopmentMode]()
        {
            TestTrue(TEXT("Unsupported commands leave material identity and shading model unchanged"),
                Fixture->Material->StateId == StateId && Fixture->Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
            TestTrue(TEXT("Unsupported commands preserve the live MID parent and parameter"),
                Fixture->Mid->Parent == Fixture->Material.Get()
                    && FMath::IsNearlyEqual(Fixture->Mid->K2_GetScalarParameterValue(TEXT("UnsupportedReloadGain")), 0.5f));
            TestFalse(TEXT("Unsupported commands never dirty the fixture package"), Fixture->Package->IsDirty());
            TestFalse(TEXT("Unsupported commands never enqueue fixture material compilation"), Fixture->Material->IsCompiling());
            TestTrue(TEXT("Unsupported global commands never enable shader development retries"),
                ShaderDevelopment == nullptr || ShaderDevelopment->GetInt() == DevelopmentMode);
        }));
        return true;
    }
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
    const auto VirtualColor = FString::Printf(TEXT("/CkUsf/%s/Color.ush"), *FixtureName);
    const auto BFile = FPaths::Combine(FixtureDirectory, TEXT("B.ush"));
    const auto CFile = FPaths::Combine(FixtureDirectory, TEXT("C.ush"));
    const auto ForeignFile = FPaths::Combine(FixtureDirectory, TEXT("Foreign.ush"));
    const auto VirtualB = FString::Printf(TEXT("/CkUsf/%s/B.ush"), *FixtureName);
    const auto VirtualC = FString::Printf(TEXT("/CkUsf/%s/C.ush"), *FixtureName);
    const auto MissingB = FString::Printf(TEXT("shader source is absent or unreadable [%s]"), *VirtualB);
    const auto MissingC = FString::Printf(TEXT("shader source is absent or unreadable [%s]"), *VirtualC);

    const auto Package = TStrongObjectPtr<UPackage>{CreatePackage(*FString::Printf(TEXT("/Temp/%s"), *FixtureName))};
    Package->SetFlags(RF_Transient);
    const auto Material = TStrongObjectPtr<UMaterial>{NewObject<UMaterial>(Package.Get(), NAME_None, RF_Transient)};
    const auto Unrelated = TStrongObjectPtr<UMaterial>{NewObject<UMaterial>(Package.Get(), NAME_None, RF_Transient)};
    const auto Custom = TObjectPtr<UMaterialExpressionCustom>{NewObject<UMaterialExpressionCustom>(Material.Get())};
    if (TestNotNull(TEXT("Custom expression created"), Custom.Get()) == false)
    { return false; }

    const auto Fixture = MakeShared<FReloadFixture>();
    Fixture->LogVerbosity = MakeUnique<FLogScopedVerbosityOverride>(&CkUsfEditor, ELogVerbosity::Log);
    Fixture->Material.Reset(Material.Get());
    Fixture->Unrelated.Reset(Unrelated.Get());
    Fixture->Custom.Reset(Custom.Get());
    Fixture->RootFile = RootFile;
    Fixture->FixtureFile = FixtureFile;
    Fixture->Directory = FixtureDirectory;
    Fixture->Scope = FixtureName;
    Fixture->ExtraFiles = {BFile, CFile, ForeignFile};
    Fixture->ColorRejectionMessage = FString::Printf(
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

    auto* DirectoryWatcher = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")).Get();
    if (TestNotNull(TEXT("Real directory observer is available"), DirectoryWatcher) == false)
    { return false; }
    const auto WeakFixture = TWeakPtr<FReloadFixture>{Fixture};
    if (TestTrue(TEXT("Independent observer witnesses fixture file events"),
        DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(FixtureDirectory,
            IDirectoryWatcher::FDirectoryChanged::CreateLambda([WeakFixture](const TArray<FFileChangeData>& InChanges)
            {
                const auto Observed = WeakFixture.Pin();
                if (NOT Observed.IsValid())
                { return; }
                FScopeLock Guard(&Observed->ObservationMutex);
                for (const auto& Change : InChanges)
                {
                    const auto File = FPaths::GetCleanFilename(Change.Filename);
                    ++Observed->EventCounts.FindOrAdd(File);
                    Observed->EventTimes.FindOrAdd(File) = FPlatformTime::Seconds();
                }
            }), Fixture->DirectoryObserver)) == false)
    { return false; }

    const auto Write_File = [this](const FString& InFile, const FString& InContents) -> bool
    {
        return TestTrue(TEXT("Dependency fixture source saved"), FFileHelper::SaveStringToFile(InContents,
            *InFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    };
    const auto Write_Root = [RootFile, Write_File](const TArray<FString>& InIncludes) -> bool
    {
        auto Contents = FString{};
        for (const auto& Include : InIncludes)
        { Contents += FString::Printf(TEXT("#include \"%s\"\n"), *Include); }
        return Write_File(RootFile, Contents);
    };
    const auto Delete_File = [this](const FString& InFile) -> bool
    {
        constexpr auto RequireExists = true;
        constexpr auto EvenReadOnly = true;
        return TestTrue(TEXT("Only the named fixture source is deleted"),
            IFileManager::Get().Delete(*InFile, RequireExists, EvenReadOnly));
    };

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

    const auto IsComplete = [Fixture]() -> bool
    {
        const auto* Resource = Fixture->Material->GetMaterialResource(GMaxRHIShaderPlatform);
        return NOT Fixture->Material->IsCompiling() && Resource != nullptr
            && Resource->IsCompilationFinished() && Resource->GetCompileErrors().IsEmpty()
            && Resource->GetGameThreadShaderMap() != nullptr && Resource->IsGameThreadShaderMapComplete();
    };
    const auto CheckAcceptedBlue = [this, Fixture, IsComplete](const TCHAR* InPoint) -> void
    {
        TestEqual(FString{InPoint} + TEXT(" preserves the accepted blue material identity"),
            Fixture->Material->StateId, Fixture->AcceptedState);
        TestTrue(FString{InPoint} + TEXT(" preserves the accepted blue material resource"),
            Fixture->Material->GetMaterialResource(GMaxRHIShaderPlatform) == Fixture->AcceptedResource);
        TestTrue(FString{InPoint} + TEXT(" retains a finished, error-free, complete shader map"), IsComplete());
    };

    if (Check_Color(TEXT("Initial include renders red with MID gain"), FLinearColor{0.5f, 0.0f, 0.0f}) == false)
    { return false; }
    GLog->AddOutputDevice(&Fixture.Get());
    Fixture->ObservingRejection = true;

    ADD_LATENT_AUTOMATION_COMMAND(FReloadSequence(*this,
        [this, Fixture, Write_Color, Write_File, Write_Root, Delete_File, World,
            VirtualColor, VirtualB, VirtualC, BFile, CFile, ForeignFile, MissingB, MissingC](int32 InStage) -> bool
        {
            Fixture->BeforeReload = Fixture->Material->StateId;
            Fixture->BeginObservation({}, {});
            if (InStage >= 4)
            {
                switch (InStage)
                {
                    case 4:
                        Fixture->BeginObservation(TEXT("Foreign.ush"), {});
                        return Write_File(ForeignFile, TEXT("float CkUsfForeignUnused() { return 1; }\n"));
                    case 5:
                        Fixture->BeginObservation(TEXT("Foreign.ush"), {});
                        return Delete_File(ForeignFile);
                    case 6:
                        AddExpectedMessagePlain(MissingB, ELogVerbosity::Warning,
                            EAutomationExpectedMessageFlags::Contains, 0);
                        Fixture->BeginObservation(TEXT("Root.ush"), MissingB);
                        if (NOT Write_Root({VirtualColor, VirtualB}))
                        { return false; }
                        FlushShaderFileCache();
                        return true;
                    case 7:
                        return Write_File(BFile, TEXT("float CkUsfReloadAuxB() { return 0; }\n"));
                    case 8:
                        Fixture->BeginObservation(TEXT("B.ush"), MissingB);
                        return Write_Color(TEXT("0, 1, 0")) && Delete_File(BFile);
                    case 9:
                        return Write_File(BFile, TEXT("float CkUsfReloadAuxB() { return 0; }\n"));
                    case 10:
                        return Write_File(CFile, TEXT("float CkUsfReloadAuxC() { return 0; }\n"))
                            && Write_Root({VirtualColor, VirtualB, VirtualC});
                    case 11:
                        AddExpectedMessagePlain(MissingC, ELogVerbosity::Warning,
                            EAutomationExpectedMessageFlags::Contains, 0);
                        Fixture->BeginObservation(TEXT("C.ush"), MissingC);
                        return Delete_File(CFile);
                    case 12:
                        Fixture->BeginObservation(TEXT("Root.ush"), MissingC);
                        return Write_Root({VirtualColor, VirtualC});
                    case 13:
                        Fixture->BeginObservation(TEXT("B.ush"), {});
                        return Delete_File(BFile);
                    case 14:
                        Fixture->BeginObservation(TEXT("C.ush"), {});
                        return Write_File(CFile, TEXT("float CkUsfReloadAuxC() { return 0; }\n"));
                    case 15:
                        IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->Set(0, Fixture->AutoReloadPriority);
                        Fixture->ManualReloadRequested = false;
                        Fixture->BeginObservation(TEXT("C.ush"), MissingC);
                        return Write_Color(TEXT("0, 0, 1")) && Delete_File(CFile);
                    case 16:
                        IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->Set(1, Fixture->AutoReloadPriority);
                        return Write_File(CFile, TEXT("float CkUsfReloadAuxC() { return 0; }\n"));
                    case 17:
                        Fixture->Custom->IncludeFilePaths.Reset();
                        Fixture->Material->UpdateCachedExpressionData();
                        Fixture->BeforeReload = Fixture->Material->StateId;
                        Fixture->BeginObservation(TEXT("Root.ush"), {});
                        return Write_Root({VirtualColor});
                    case 18:
                        Fixture->BeginObservation(TEXT("Root.ush"), {});
                        return Delete_File(Fixture->RootFile) && Delete_File(Fixture->FixtureFile);
                    default:
                        AddError(TEXT("Unknown shader dependency ownership stage"));
                        return false;
                }
            }
            if (InStage == 2)
            {
                Fixture->BeginObservation(TEXT("Color.ush"), Fixture->ColorRejectionMessage);
                AddExpectedMessage(Fixture->ColorRejectionMessage, ELogVerbosity::Warning,
                    EAutomationExpectedMessageFlags::Contains, 1, false);
                return TestTrue(TEXT("Only nested fixture include deleted"),
                    IFileManager::Get().Delete(*Fixture->FixtureFile, true, true));
            }
            if (InStage == 1)
            {
                IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->Set(0, Fixture->AutoReloadPriority);
                Fixture->BeginObservation(TEXT("Color.ush"), {});
            }
            if (Write_Color(InStage == 0 ? TEXT("0, 1, 0") : TEXT("0, 0, 1")) == false)
            { return false; }
            // First edit relies solely on the real directory watcher. The second also exercises
            // the public manual command; both must yield frames for production ticker dispatch.
            return InStage != 1 || TestTrue(TEXT("Production reload command dispatched"),
                IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Ck_Usf_ReloadShaders"), *GLog, World.Get()));
        },
        [this, Fixture, UnrelatedStateId, Check_Color, VirtualB, MissingC, World, IsComplete,
            CheckAcceptedBlue, Write_Color](int32 InStage) -> bool
        {
            if (InStage == 1)
            {
                if (NOT Fixture->ObservedNegativeWindow())
                { return false; }
                if (NOT Fixture->IdenticalSaveRequested)
                {
                    if (Fixture->Material->StateId == Fixture->BeforeReload || NOT IsComplete())
                    { return false; }
                    if (NOT Fixture->ObservedSuccessfulBatch())
                    { return false; }
                    TestEqual(TEXT("Manual blue starts exactly one production batch"),
                        Fixture->ReloadStarts.Load() - Fixture->ReloadStartsBefore, 1);
                    TestEqual(TEXT("Manual blue successfully completes exactly one production batch"),
                        Fixture->ReloadSuccesses.Load() - Fixture->ReloadSuccessesBefore, 1);
                    Fixture->AcceptedState = Fixture->Material->StateId;
                    Fixture->AcceptedResource = Fixture->Material->GetMaterialResource(GMaxRHIShaderPlatform);
                    CheckAcceptedBlue(TEXT("Before manual blue draw"));
                    Check_Color(TEXT("Manual reload renders blue and retains MID gain"), FLinearColor{0.0f, 0.0f, 0.5f});
                    CheckAcceptedBlue(TEXT("After manual blue draw"));
                    IConsoleManager::Get().FindConsoleVariable(TEXT("Ck.Usf.AutoReload"))->Set(1, Fixture->AutoReloadPriority);
                    Fixture->BeginObservation(TEXT("Color.ush"), {});
                    Fixture->IdenticalSaveRequested = true;
                    Write_Color(TEXT("0, 0, 1"));
                    return false;
                }
                TestEqual(TEXT("Identical blue save starts no production batch"),
                    Fixture->ReloadStarts.Load(), Fixture->ReloadStartsBefore);
                TestEqual(TEXT("Identical blue save produces no extra successful completion"),
                    Fixture->ReloadSuccesses.Load(), Fixture->ReloadSuccessesBefore);
                TestEqual(TEXT("Identical blue save produces no failed completion"),
                    Fixture->ReloadFailures.Load(), Fixture->ReloadFailuresBefore);
                CheckAcceptedBlue(TEXT("Before identical blue save draw"));
                Check_Color(TEXT("Identical saved bytes retain accepted blue pixels and MID gain"), FLinearColor{0.0f, 0.0f, 0.5f});
                CheckAcceptedBlue(TEXT("After identical blue save draw"));
                TestTrue(TEXT("Manual and identical-byte reload preserve the unrelated material"),
                    Fixture->Unrelated->StateId == UnrelatedStateId);
                TestFalse(TEXT("Manual and identical-byte reload never dirty the fixture package"),
                    Fixture->Material->GetOutermost()->IsDirty());
                TestTrue(TEXT("Manual and identical-byte reload preserve the existing MID parent"),
                    Fixture->Mid->Parent == Fixture->Material.Get());
                return true;
            }
            if (InStage >= 4)
            {
                const auto IsNegativeEvent = InStage == 4 || InStage == 5 || InStage == 13
                    || InStage == 17 || InStage == 18;
                const auto IsRejected = InStage == 6 || InStage == 8 || InStage == 11 || InStage == 12 || InStage == 15;
                if (InStage == 14 && NOT Fixture->ObservedNegativeWindow())
                { return false; }
                if (InStage == 15 && NOT Fixture->ManualReloadRequested)
                {
                    // Let actual source events drain while automatic reload is disabled, so the
                    // command has no watcher-file work to retain on its behalf.
                    if (NOT Fixture->ObservedNegativeWindow())
                    { return false; }
                    TestTrue(TEXT("Disabled watcher leaves the last accepted green material unchanged"),
                        Fixture->Material->StateId == Fixture->BeforeReload);
                    TestEqual(TEXT("Disabled watcher starts no material reload batch"),
                        Fixture->ReloadStarts.Load(), Fixture->ReloadStartsBefore);
                    TestFalse(TEXT("Disabled watcher starts no material compilation"), Fixture->Material->IsCompiling());
                    Fixture->ManualReloadRequested = TestTrue(TEXT("Manual-only reload command dispatched after disabled watcher events drained"),
                        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Ck_Usf_ReloadShaders"), *GLog, World.Get()));
                    return NOT Fixture->ManualReloadRequested;
                }
                if (IsNegativeEvent)
                {
                    // The independent observer proves delivery. One second after its last event is
                    // a bounded negative check beyond the production watcher's 0.25s debounce.
                    if (NOT Fixture->ObservedNegativeWindow())
                    { return false; }
                    TestFalse(TEXT("No warning attributes an unused or retired source"),
                        Fixture->HasNewWarning(InStage == 13 ? VirtualB : FString{}));
                    TestEqual(TEXT("Unused or retired source events start no material reload batch"),
                        Fixture->ReloadStarts.Load(), Fixture->ReloadStartsBefore);
                }
                else if (IsRejected)
                {
                    if (NOT Fixture->RejectionObserved.Load())
                    { return false; }
                    if (InStage == 6)
                    {
                        TestTrue(TEXT("A newly owned cold-cache missing dependency is rejected during source preflight"),
                            Fixture->HasNewWarning(TEXT("CkUsf reload rejected before material changes:")));
                    }
                    if (InStage == 15)
                    {
                        TestTrue(TEXT("Manual-only reload explicitly rejects the exact missing C dependency"),
                            Fixture->HasNewWarning(MissingC));
                    }
                }
                else if (Fixture->Material->StateId == Fixture->BeforeReload)
                { return false; }
                else if (NOT IsComplete())
                { return false; }
                else if (NOT Fixture->ObservedSuccessfulBatch())
                { return false; }

                if (IsNegativeEvent || IsRejected)
                {
                    TestTrue(TEXT("Rejected, unused, or retired source leaves the material StateId unchanged"),
                        Fixture->Material->StateId == Fixture->BeforeReload);
                    TestFalse(TEXT("Rejected, unused, or retired source starts no material compilation"),
                        Fixture->Material->IsCompiling());
                }
                TestTrue(TEXT("Dependency ownership retains a complete shader map before drawing"), IsComplete());
                TestTrue(TEXT("Dependency ownership leaves unrelated material unchanged"),
                    Fixture->Unrelated->StateId == UnrelatedStateId);
                TestFalse(TEXT("Dependency ownership never dirties the fixture package"),
                    Fixture->Material->GetOutermost()->IsDirty());
                TestTrue(TEXT("Dependency ownership retains the original MID parent"),
                    Fixture->Mid->Parent == Fixture->Material.Get());
                const auto Expected = InStage <= 8 || InStage >= 16 ? FLinearColor{0.0f, 0.0f, 0.5f}
                    : FLinearColor{0.0f, 0.5f, 0.0f};
                Check_Color(*FString::Printf(TEXT("Dependency ownership stage %d preserves or recovers actual MID pixels"), InStage), Expected);
                TestTrue(TEXT("Dependency ownership retains a complete shader map after drawing"), IsComplete());
                return true;
            }
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
                CheckAcceptedBlue(TEXT("Before rejected missing include draw"));
                Check_Color(TEXT("Rejected missing include preserves previous blue shader and MID gain"),
                    FLinearColor{0.0f, 0.0f, 0.5f});
                CheckAcceptedBlue(TEXT("After rejected missing include draw"));
                return true;
            }
            if (Fixture->Material->StateId == Fixture->BeforeReload)
            { return false; }
            if (NOT IsComplete())
            { return false; }
            if (NOT Fixture->ObservedSuccessfulBatch())
            { return false; }
            TestTrue(TEXT("Reload produces a complete shader map before drawing"), IsComplete());
            if (InStage == 3)
            {
                TestTrue(TEXT("Fixture observes actual production reload starts before checking their absence"),
                    Fixture->ReloadStarts.Load() > 0);
            }
            TestTrue(TEXT("Unrelated material StateId unchanged"), Fixture->Unrelated->StateId == UnrelatedStateId);
            TestFalse(TEXT("Reload does not dirty material package"), Fixture->Material->GetOutermost()->IsDirty());
            TestTrue(TEXT("Existing MID retains the original parent"), Fixture->Mid->Parent == Fixture->Material.Get());
            Check_Color(InStage == 0 ? TEXT("Watcher reload renders green and retains MID gain") : InStage == 1 ?
                TEXT("Manual reload renders blue and retains MID gain") :
                TEXT("Restoring identical nested include bytes triggers automatic recovery and preserves MID gain"), InStage == 0 ?
                FLinearColor{0.0f, 0.5f, 0.0f} : FLinearColor{0.0f, 0.0f, 0.5f});
            TestTrue(TEXT("Reload retains a complete shader map after drawing"), IsComplete());
            return true;
        }));
    return true;
}

#endif // WITH_EDITOR
