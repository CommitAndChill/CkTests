#include "CkTest_HexPersistenceAssets.h"

#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_Spec.h"
#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"
#include <HAL/FileManager.h>
#include <Misc/AutomationTest.h>
#include <Misc/PackageName.h>
#include <Misc/Paths.h>
#include <UObject/SavePackage.h>
#include <Engine/Level.h>
#include <Engine/World.h>
#include <GameFramework/WorldSettings.h>
#include <GameFramework/GameModeBase.h>

namespace ck::tests::hex_persistence
{
    auto EnsureGymMap(FAutomationTestBase& InTest) -> bool
    {
        const FString PackageName = TEXT("/CkTests/CkGrid/HexGrid/HexPersistenceGym");
        auto* Mode = LoadClass<AGameModeBase>(nullptr, TEXT("/Script/Angelscript.Ck_HexPersistenceGym_GameMode"));
        if (!InTest.TestNotNull(TEXT("Persistence gym game mode is compiled"), Mode)) { return false; }
        auto* World = LoadObject<UWorld>(nullptr, *(PackageName + TEXT(".HexPersistenceGym")), nullptr, LOAD_NoWarn);
        if (!World)
        {
            // The existing 86 KB gym map is self-contained. Refuse a future partitioned/streaming source
            // rather than producing a partial copy or duplicating external actor packages implicitly.
            auto* Source = LoadObject<UWorld>(nullptr, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level.TestGyms_CkTests_Level"));
            if (!InTest.TestTrue(TEXT("Gym template is a self-contained map"), Source && Source->PersistentLevel &&
                !Source->GetWorldPartition() && !Source->PersistentLevel->IsUsingExternalActors() && Source->GetStreamingLevels().IsEmpty()))
            { return false; }
            auto* Package = CreatePackage(*PackageName);
            Package->SetPackageFlags(PKG_ContainsMap);
            World = Cast<UWorld>(StaticDuplicateObject(Source, Package, TEXT("HexPersistenceGym")));
            if (!InTest.TestNotNull(TEXT("Duplicated gym world"), World)) { return false; }
            World->SetFlags(RF_Public | RF_Standalone);
            World->GetWorldSettings()->DefaultGameMode = Mode;
            const auto File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetMapPackageExtension());
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
            FSavePackageArgs Args;
            Args.TopLevelFlags = RF_Public | RF_Standalone;
            if (!InTest.TestTrue(TEXT("Saved dedicated persistence gym map"), UPackage::SavePackage(Package, World, *File, Args)))
            { return false; }
        }
        return InTest.TestTrue(TEXT("Saved gym map retains its game mode across absolute load travel"),
            World->GetWorldSettings() && World->GetWorldSettings()->DefaultGameMode == Mode);
    }

    auto EnsureAssets(FAutomationTestBase& InTest) -> bool
    {
        const FString PackageName = TEXT("/CkTests/CkGrid/HexGrid/DA_HexGrid_PersistenceBoard");
        auto* Asset = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, BoardAssetPath, nullptr, LOAD_NoWarn);
        if (!Asset)
        {
            // The fixture is authored once through the editor's package writer, never synthesized by a runtime test.
            Asset = NewObject<UCk_HexGrid_AuthoringSpec>(CreatePackage(*PackageName),
                TEXT("DA_HexGrid_PersistenceBoard"), RF_Public | RF_Standalone);
            FCk_HexGrid_Spec Spec;
            FCk_HexDomain Domain;
            Domain.Set_RingRadius(2);
            Spec.Set_Domain(Domain);
            FCk_HexLayout Layout;
            Layout.Set_OuterRadius(46.0);
            Spec.Set_Layout(Layout);
            FGameplayTagContainer Tags;
            Tags.AddTag(AuthoredLocal);
            Spec.Set_PerCellTags({FCk_HexCellTagOverride{FCk_HexCoord{0,0}, Tags}});
            Spec.Set_Blockers({FCk_HexGridAuthoredBlocker{{FCk_HexCoord{-2,0}}, AuthoredBlocker, false}});
            Asset->Set_Spec(Spec);

            const auto File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
            FSavePackageArgs Args;
            Args.TopLevelFlags = RF_Public | RF_Standalone;
            if (!InTest.TestTrue(TEXT("Saved persistence board authoring asset"),
                UPackage::SavePackage(Asset->GetOutermost(), Asset, *File, Args)))
            { return false; }
        }

        const auto& Spec = Asset->Get_Spec();
        const auto& Tags = Spec.Get_PerCellTags();
        const auto& Blockers = Spec.Get_Blockers();
        // Existing content is validated rather than rewritten every test (or silently replacing a user's edits).
        return InTest.TestTrue(TEXT("Persistence board is disk 2 with the stable authored local tag and inactive named blocker"),
            Spec.Get_Domain().Get_Kind() == ECk_HexDomainKind::Disk && Spec.Get_Domain().Get_RingRadius() == 2 &&
            Tags.Num() == 1 && Tags[0].Get_Coordinate() == FCk_HexCoord{0,0} && Tags[0].Get_Tags().HasTagExact(AuthoredLocal) &&
            Blockers.Num() == 1 && Blockers[0].Get_Name() == AuthoredBlocker && !Blockers[0].Get_InitiallyActive() &&
            Blockers[0].Get_Cells() == TArray<FCk_HexCoord>{FCk_HexCoord{-2,0}});
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPersistenceAssets, "Ck.HexGrid.Persistence.AuthoringAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexPersistenceAssets::RunTest(const FString&)
{
    return ck::tests::hex_persistence::EnsureAssets(*this) && ck::tests::hex_persistence::EnsureGymMap(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexRadius32Recipe, "Ck.HexGrid.Scale.AuthoringAsset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexRadius32Recipe::RunTest(const FString&)
{
    using namespace ck::tests::hex_persistence;
    const auto* Source = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, BoardAssetPath);
    if (!TestNotNull(TEXT("Existing durability control"), Source)) { return false; }
    FCk_HexGrid_Spec Expected;
    FCk_HexGridRecipeIdentity Original;
    ECk_HexBoardFailure Failure{};
    if (!TestTrue(TEXT("Control is the saved disk-2 recipe"), !Source->GetOutermost()->IsDirty() &&
        Source->TryResolveWithIdentity(Expected, Original, Failure) && Original.Get_AssetPath() == BoardAssetPath &&
        Expected.Get_Domain().Get_Kind() == ECk_HexDomainKind::Disk && Expected.Get_Domain().Get_RingRadius() == 2)) { return false; }
    auto Domain = Expected.Get_Domain();
    Domain.Set_RingRadius(32);
    Expected.Set_Domain(Domain);

    const auto PackageName = FPackageName::ObjectPathToPackageName(FString{Radius32BoardAssetPath});
    const auto File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
    UCk_HexGrid_AuthoringSpec* Asset = nullptr;
    if (IFileManager::Get().FileExists(*File))
    {
        Asset = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, Radius32BoardAssetPath);
    }
    else
    {
        if (!TestNull(TEXT("New fixture path has no unsaved package"), FindPackage(nullptr, *PackageName))) { return false; }
        auto* Package = CreatePackage(*PackageName);
        Asset = NewObject<UCk_HexGrid_AuthoringSpec>(Package, FName(*FPackageName::GetShortName(PackageName)), RF_Public | RF_Standalone);
        Asset->Set_Spec(Expected);
        Asset->Set_RecipeRevision(Original.Get_RecipeRevision());
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        if (!TestTrue(TEXT("Saved separate radius-32 recipe"), UPackage::SavePackage(Package, Asset, *File, Args))) { return false; }
    }
    if (!TestNotNull(TEXT("Radius-32 asset has expected class"), Asset) ||
        !TestTrue(TEXT("Serialized radius-32 package on disk"), !Asset->GetOutermost()->IsDirty() &&
            IFileManager::Get().FileSize(*File) > 0 && FPackageName::DoesPackageExist(PackageName))) { return false; }
    FCk_HexGrid_Spec Actual;
    FCk_HexGridRecipeIdentity Identity;
    TArray<uint8> ActualBytes, ExpectedBytes;
    if (!TestTrue(TEXT("Full authored recipe differs from control only by radius and has its own identity"),
        Asset->TryResolveWithIdentity(Actual, Identity, Failure) && Identity.Get_AssetPath() == Radius32BoardAssetPath &&
        Identity.Get_RecipeRevision() == Original.Get_RecipeRevision() && !Identity.Get_Fingerprint().IsEmpty() &&
        Identity.Get_Fingerprint() != Original.Get_Fingerprint() &&
        ck::hex_grid::TryEncodeRecipe(Expected, ExpectedBytes, Failure) &&
        ck::hex_grid::TryEncodeRecipe(Actual, ActualBytes, Failure) && ActualBytes == ExpectedBytes)) { return false; }
    AddInfo(FString::Printf(TEXT("Saved fixture %s bytes=%lld revision=%d fingerprint=%s"),
        *File, IFileManager::Get().FileSize(*File), Identity.Get_RecipeRevision(), *Identity.Get_Fingerprint()));
    return true;
}
