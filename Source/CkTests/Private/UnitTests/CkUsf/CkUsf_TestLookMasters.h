#pragma once

// Generation tests save real packages under a private GUID mount in Saved. The scope outlives
// every material consumer, then retires its loaded materials before removing their files.
#if WITH_EDITOR

#include "CoreMinimal.h"
#include "CkCore/Macros/CkMacros.h"
#include "CkUsf/LookDefinition/CkUsf_LookDefinition_Naming.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PackageTools.h"
#include "RenderingThread.h"
#include "RHIGlobals.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace ck_test_usf
{
    class FGeneratedMasterScope final
    {
    public:
        explicit FGeneratedMasterScope(FAutomationTestBase& InTest)
            : _Test(InTest)
        {
        }

        FGeneratedMasterScope(const FGeneratedMasterScope&) = delete;
        auto operator=(const FGeneratedMasterScope&) -> FGeneratedMasterScope& = delete;

        ~FGeneratedMasterScope()
        {
            if (!_Closed) { Close(); }
        }

        auto Initialize() -> bool
        {
            if (!IsInGameThread() || _InitializationAttempted || _Closed)
            { return Fail(TEXT("Generated-master scope must initialize once on the game thread.")); }
            _InitializationAttempted = true;
            _Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            _BaseDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()
                / TEXT("Automation/CkUsfGeneratedMasters"));
            FPaths::NormalizeDirectoryName(_BaseDirectory);
            _Directory = _BaseDirectory / _Id;
            _MountRoot = TEXT("/CkUsfGeneratedTest_") + _Id + TEXT("/");
            _PackageRoot = _MountRoot.LeftChop(1);
            if (!Get_IsOwnedDirectory() || IFileManager::Get().DirectoryExists(*_Directory))
            { return Fail(TEXT("Generated-master directory is not a new owned GUID directory.")); }
            const auto CreatedDirectory = IFileManager::Get().MakeDirectory(*_Directory, true);
            _DirectoryCreated = IFileManager::Get().DirectoryExists(*_Directory);
            if (!CreatedDirectory || !_DirectoryCreated)
            { return Fail(TEXT("Could not create the generated-master fixture directory.")); }
            FPackageName::RegisterMountPoint(_MountRoot, _Directory + TEXT("/"));
            _Mounted = true;
            return true;
        }

        auto Get_PackageRoot() const -> const FString& { return _PackageRoot; }

        auto Close() -> bool
        {
            if (_Closed) { return _CleanupSucceeded; }
            _Closed = true;
            if (!_DirectoryCreated)
            { _CleanupSucceeded = true; return true; }
            if (!IsInGameThread() || !Get_IsOwnedDirectory())
            { return Fail(TEXT("Generated-master cleanup rejected an invalid thread or directory.")); }

            // Callers end their consumer scope before Close, including strong material references.
            auto MaterialsToFinish = TArray<FMaterial*>{};
            for (TObjectIterator<UMaterial> It; It; ++It)
            {
                if (Get_OwnsPackage(It->GetOutermost()))
                {
                    if (auto* Resource = It->GetMaterialResource(GMaxRHIShaderPlatform))
                    { MaterialsToFinish.Add(Resource); }
                }
            }
            if (!MaterialsToFinish.IsEmpty())
            { FMaterial::FinishCompilation(TEXT("CkUsf generated-master cleanup"), MaterialsToFinish); }
            MaterialsToFinish.Reset();
            FlushRenderingCommands();
            auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
            Registry.WaitForCompletion();

            auto Packages = TArray<UPackage*>{};
            for (TObjectIterator<UPackage> It; It; ++It)
            {
                if (Get_OwnsPackage(*It)) { Packages.Add(*It); }
            }
            if (!Packages.IsEmpty())
            {
                auto Unload = UPackageTools::FUnloadPackageParams{Packages};
                Unload.bResetTransBuffer = false;
                Unload.bUnloadDirtyPackages = true;
                if (!UPackageTools::UnloadPackages(Unload))
                { return Fail(TEXT("Could not unload generated-master packages; fixture files are preserved. ") + Unload.OutErrorMessage.ToString()); }
            }
            CollectGarbage(RF_NoFlags);
            FlushRenderingCommands();
            for (TObjectIterator<UMaterialInterface> It; It; ++It)
            {
                if (Get_OwnsPackage(It->GetOutermost()))
                { return Fail(TEXT("Generated material remains loaded after package unload; fixture files are preserved: ") + It->GetPathName()); }
            }

            if (_Mounted)
            {
                FPackageName::UnRegisterMountPoint(_MountRoot, _Directory + TEXT("/"));
                _Mounted = false;
            }
            // Drain scans caused by mounting/unmounting while the Saved package files still exist.
            Registry.WaitForCompletion();
            auto RemainingAssets = TArray<FAssetData>{};
            Registry.GetAssetsByPath(FName{*_PackageRoot}, RemainingAssets, true);
            if (!RemainingAssets.IsEmpty())
            { return Fail(TEXT("Generated-master asset registry namespace remains populated after unmount; fixture files are preserved: ") + _PackageRoot); }
            for (TObjectIterator<UMaterialInterface> It; It; ++It)
            {
                if (Get_OwnsPackage(It->GetOutermost()))
                { return Fail(TEXT("Generated material reappeared during registry drain; fixture files are preserved: ") + It->GetPathName()); }
            }
            if (!Get_IsOwnedDirectory())
            { return Fail(TEXT("Generated-master directory ownership changed before deletion.")); }
            if (!IFileManager::Get().DeleteDirectory(*_Directory, false, true)
                || IFileManager::Get().DirectoryExists(*_Directory))
            { return Fail(TEXT("Could not remove the retired generated-master fixture directory: ") + _Directory); }
            _DirectoryCreated = false;
            _CleanupSucceeded = true;
            return true;
        }

    private:
        auto Get_IsOwnedDirectory() const -> bool
        {
            return !_Id.IsEmpty() && !_BaseDirectory.IsEmpty() && !_Directory.IsEmpty()
                && !FPaths::IsRelative(_Directory) && !FPaths::IsRelative(_BaseDirectory)
                && FPaths::IsSamePath(FPaths::GetPath(_Directory), _BaseDirectory)
                && FPaths::IsUnderDirectory(_Directory, _BaseDirectory)
                && FPaths::GetCleanFilename(_Directory) == _Id;
        }

        auto Get_OwnsPackage(const UPackage* InPackage) const -> bool
        { return InPackage != nullptr && InPackage->GetName().StartsWith(_MountRoot); }

        auto Fail(const FString& InMessage) -> bool
        { _Test.AddError(InMessage); return false; }

        FAutomationTestBase& _Test;
        FString _Id;
        FString _BaseDirectory;
        FString _Directory;
        FString _MountRoot;
        FString _PackageRoot;
        bool _InitializationAttempted = false;
        bool _Mounted = false;
        bool _DirectoryCreated = false;
        bool _Closed = false;
        bool _CleanupSucceeded = false;
    };
}

#endif // WITH_EDITOR
