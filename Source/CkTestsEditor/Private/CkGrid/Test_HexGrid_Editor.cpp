#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGridEditor/EdMode/CkHexGrid_PaintService.h"
#include "CkGridEditor/EdMode/CkHexGrid_EdMode.h"
#include "CkGridEditor/EdMode/Ck2dGridSystem_EdMode.h"
#include "CkEntitySpawner/CkEntitySpawner_Actor.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_EntityScript.h"
#include "CkGrid/HexGridSystem/Geometry/CkHexGrid_Geometry_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Utils.h"
#include "CkGrid/2dGridSystem/Authoring/Ck2dGridSystem_EntityScript.h"
#include "CkGrid/2dGridSystem/Authoring/Ck2dGridSystem_Spec.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "Editor/TransBuffer.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "GameplayTagContainer.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace ck_test_hex_editor
{
    using namespace ck::hex_grid_editor;
    using Geometry = UCk_Utils_HexGeometry_UE;
    using Grid = UCk_Utils_HexGrid_UE;
    using Cell = UCk_Utils_HexCell_UE;
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;



    auto PaintTag() -> FGameplayTag
    { return FGameplayTag::RequestGameplayTag(FName{TEXT("Ck.Test.HexRules.Hazard")}); }
    auto DefaultTag() -> FGameplayTag
    { return FGameplayTag::RequestGameplayTag(FName{TEXT("Ck.Test.HexRules.Ground")}); }
    auto Recipe(const FCk_HexGrid_Spec& Spec) -> TArray<uint8>
    {
        TArray<uint8> Bytes;
        ECk_HexBoardFailure Failure{};
        ck::hex_grid::TryEncodeRecipe(Spec, Bytes, Failure);
        return Bytes;
    }

    auto Disk() -> FCk_HexGrid_Spec
    {
        FCk_HexGrid_Spec Spec;
        FCk_HexDomain Domain;
        Domain.Set_RingRadius(2);
        Spec.Set_Domain(Domain);
        Spec.Set_Pivot(FTransform{FRotator{17, 29, 11}, FVector{31, -19, 23}, FVector{1.25}});
        Spec.Set_DefaultCellTags(FGameplayTagContainer{DefaultTag()});
        return Spec;
    }

    // Every test owns a new mounted directory and private undo buffer; no user asset or undo history is reset.
    struct FFixture
    {
        FAutomationTestBase& Test;
        FString Root;
        FString Directory;
        FString PackageName;
        FString Filename;
        FSoftObjectPath Path;
        TStrongObjectPtr<UTransactor> Previous;
        TStrongObjectPtr<UTransBuffer> Transactions;
        TArray<TWeakObjectPtr<ACk_EntitySpawner_UE>> Spawners;

        explicit FFixture(FAutomationTestBase& InTest) : Test(InTest) {}
        auto Initialize() -> bool
        {
            if (!GEditor || !GEditor->Trans || GEditor->Trans->IsActive() || GEditor->IsPlaySessionInProgress())
            { Test.AddError(TEXT("Hex editor tests require an idle editor and inactive transaction.")); return false; }
            Previous.Reset(GEditor->Trans.Get());
            Transactions.Reset(NewObject<UTransBuffer>());
            Transactions->Initialize(8 * 1024 * 1024);
            GEditor->Trans = Transactions.Get();
            const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Root = TEXT("/CkHexEditor_") + Id + TEXT("/");
            Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/HexGrid/Editor"), Id) + TEXT("/");
            if (!IFileManager::Get().MakeDirectory(*Directory, true)) { return false; }
            FPackageName::RegisterMountPoint(Root, Directory);
            PackageName = Root + TEXT("Board");
            Path = FSoftObjectPath{PackageName + TEXT(".Board")};
            Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
            auto* Asset = NewObject<UCk_HexGrid_AuthoringSpec>(CreatePackage(*PackageName),
                TEXT("Board"), RF_Public | RF_Standalone | RF_Transactional);
            Asset->Set_Spec(Disk());
            return Save();
        }
        auto Asset() const -> UCk_HexGrid_AuthoringSpec*
        { return Cast<UCk_HexGrid_AuthoringSpec>(Path.ResolveObject()); }
        auto Save() -> bool
        {
            auto* Value = Asset();
            FSavePackageArgs Args;
            Args.TopLevelFlags = RF_Public | RF_Standalone;
            Args.SaveFlags = SAVE_NoError;
            return Value && UPackage::SavePackage(Value->GetOutermost(), Value, *Filename, Args);
        }
        auto Spawn(UClass* ScriptClass = UCk_HexGrid_EntityScript::StaticClass()) -> ACk_EntitySpawner_UE*
        {
            auto* World = GEditor->GetEditorWorldContext().World();
            if (!World) { return nullptr; }
            FActorSpawnParameters Params;
            Params.ObjectFlags = RF_Transient;
            auto* Spawner = World->SpawnActor<ACk_EntitySpawner_UE>(Params);
            if (!Spawner) { return nullptr; }
            Spawners.Add(Spawner);
            Spawner->EditorOnly_InitializeEntityScript(ScriptClass);
            if (ScriptClass->IsChildOf(UCk_HexGrid_EntityScript::StaticClass()))
            {
                auto* Property = FindFProperty<FSoftObjectProperty>(ScriptClass, TEXT("_Spec"));
                if (!Property) { return nullptr; }
                Property->SetPropertyValue_InContainer(Spawner->Get_EntityScript(), FSoftObjectPtr{Asset()});
            }
            Spawner->SetActorTransform(FTransform{FRotator{13, 41, -7}, FVector{420, -230, 150}, FVector{1.5}});
            return Spawner;
        }
        auto DestroySpawners() -> void
        {
            for (const auto& Value : Spawners)
            { if (Value.IsValid()) { Value->EditorOnly_DestroyEntity(); Value->Destroy(); } }
            Spawners.Reset();
        }
        auto Unload() -> bool
        {
            if (PackageName.IsEmpty()) { return true; }
            Transactions->Reset(FText::FromString(TEXT("Release disposable hex painting transactions")));
            if (auto* Package = FindPackage(nullptr, *PackageName))
            {
                auto Params = UPackageTools::FUnloadPackageParams{TArray<UPackage*>{Package}};
                Params.bResetTransBuffer = false;
                if (!UPackageTools::UnloadPackages(Params)) { return false; }
            }
            CollectGarbage(RF_NoFlags);
            return Path.ResolveObject() == nullptr;
        }
        ~FFixture()
        {
            DestroySpawners();
            const auto Released = Transactions.IsValid() && Unload();
            if (GEditor && Transactions.IsValid() && GEditor->Trans.Get() == Transactions.Get())
            { GEditor->Trans = Previous.Get(); }
            if (Released)
            {
                FPackageName::UnRegisterMountPoint(Root, Directory);
                IFileManager::Get().Delete(*Filename);
                IFileManager::Get().DeleteDirectory(*Directory);
            }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexEditorTypedSelection, "Ck.HexGrid.Editor.TypedSelection", ck_test_hex_editor::Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexEditorTransformedHitAndBrush, "Ck.HexGrid.Editor.TransformedHitAndBrush", ck_test_hex_editor::Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexEditorTransactionalStroke, "Ck.HexGrid.Editor.TransactionalStroke", ck_test_hex_editor::Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexEditorAssetRoundTrip, "Ck.HexGrid.Editor.AssetRoundTrip", ck_test_hex_editor::Flags)

auto FCkHexEditorTypedSelection::RunTest(const FString&) -> bool
{
    using namespace ck_test_hex_editor;
    FFixture Fixture{*this};
    if (!Fixture.Initialize()) { return false; }
    auto* Hex = Fixture.Spawn();
    auto* Square = Fixture.Spawn(UCk_2dGridSystem_EntityScript::StaticClass());
    if (!TestNotNull(TEXT("Real hex spawner"), Hex) || !TestNotNull(TEXT("Real square spawner"), Square)) { return false; }
    TestTrue(TEXT("Hex selects its serialized native asset"), ResolveSelection(Hex).Asset == Fixture.Asset());
    TestFalse(TEXT("Square spawner does not enter the hex adapter"), ResolveSelection(Square).IsValid());
    TestFalse(TEXT("Absent spawner is ignored"), ResolveSelection(nullptr).IsValid());
    TestTrue(TEXT("Only root component contributes passive overlay"), ResolveDrawableSelection(Hex->GetRootComponent(), false).IsValid());
    auto* Extra = NewObject<USceneComponent>(Hex);
    TestFalse(TEXT("Additional component cannot duplicate overlay"), ResolveDrawableSelection(Extra, false).IsValid());
    TestFalse(TEXT("Active hex mode owns overlay exclusively"), ResolveDrawableSelection(Hex->GetRootComponent(), true).IsValid());
    TestTrue(TEXT("Square and hex modes retain different stable IDs"),
        UCk_HexGrid_EdMode::EM_CkHexGridPaintModeId == FName{TEXT("Ck.HexGrid.PaintMode")}
        && UCk_HexGrid_EdMode::EM_CkHexGridPaintModeId != UCk_2dGridSystem_EdMode::EM_Ck2dGridSystemPaintModeId);
    auto* SquareAsset = NewObject<UCk_2dGridSystem_AuthoringSpec>(Fixture.Asset()->GetOutermost());
    auto* SquareProperty = FindFProperty<FObjectProperty>(UCk_2dGridSystem_EntityScript::StaticClass(), TEXT("Spec"));
    if (!TestNotNull(TEXT("Square retains its existing typed asset property"), SquareProperty)) { return false; }
    SquareProperty->SetObjectPropertyValue_InContainer(Square->Get_EntityScript(), SquareAsset);
    auto& Modes = GLevelEditorModeTools();
    const auto WasSquare = Modes.IsModeActive(UCk_2dGridSystem_EdMode::EM_Ck2dGridSystemPaintModeId);
    const auto WasHex = Modes.IsModeActive(UCk_HexGrid_EdMode::EM_CkHexGridPaintModeId);
    TArray<TWeakObjectPtr<AActor>> PreviousSelection;
    for (auto Index = 0; Index < GEditor->GetSelectedActors()->Num(); ++Index)
    { PreviousSelection.Add(Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(Index))); }
    ON_SCOPE_EXIT
    {
        Modes.DeactivateMode(UCk_HexGrid_EdMode::EM_CkHexGridPaintModeId);
        Modes.DeactivateMode(UCk_2dGridSystem_EdMode::EM_Ck2dGridSystemPaintModeId);
        GEditor->SelectNone(false, true, false);
        for (const auto& Actor : PreviousSelection)
        { if (Actor.IsValid()) { GEditor->SelectActor(Actor.Get(), true, false); } }
        GEditor->NoteSelectionChange();
        if (WasSquare) { Modes.ActivateMode(UCk_2dGridSystem_EdMode::EM_Ck2dGridSystemPaintModeId); }
        if (WasHex) { Modes.ActivateMode(UCk_HexGrid_EdMode::EM_CkHexGridPaintModeId); }
    };
    GEditor->SelectNone(false, true, false);
    GEditor->SelectActor(Square, true, false);
    GEditor->NoteSelectionChange();
    Modes.ActivateMode(UCk_2dGridSystem_EdMode::EM_Ck2dGridSystemPaintModeId);
    TestTrue(TEXT("Existing square paint mode enters"), Modes.IsModeActive(UCk_2dGridSystem_EdMode::EM_Ck2dGridSystemPaintModeId));
    GEditor->SelectNone(false, true, false);
    GEditor->SelectActor(Hex, true, false);
    GEditor->NoteSelectionChange();
    TestTrue(TEXT("Production selection event routes square paint to hex paint"),
        Modes.IsModeActive(UCk_HexGrid_EdMode::EM_CkHexGridPaintModeId)
        && !Modes.IsModeActive(UCk_2dGridSystem_EdMode::EM_Ck2dGridSystemPaintModeId));
    GEditor->SelectNone(false, true, false);
    GEditor->SelectActor(Square, true, false);
    GEditor->NoteSelectionChange();
    TestTrue(TEXT("Production selection event routes hex paint back to square paint"),
        Modes.IsModeActive(UCk_2dGridSystem_EdMode::EM_Ck2dGridSystemPaintModeId)
        && !Modes.IsModeActive(UCk_HexGrid_EdMode::EM_CkHexGridPaintModeId));
    return true;
}

auto FCkHexEditorTransformedHitAndBrush::RunTest(const FString&) -> bool
{
    using namespace ck_test_hex_editor;
    FFixture Fixture{*this};
    if (!Fixture.Initialize()) { return false; }
    auto* Spawner = Fixture.Spawn();
    if (!TestNotNull(TEXT("Transformed hex spawner"), Spawner)) { return false; }
    for (const auto Orientation : {ECk_HexOrientation::Flat, ECk_HexOrientation::Pointy})
    {
        auto Spec = Disk();
        auto Layout = Spec.Get_Layout();
        Layout.Set_Orientation(Orientation);
        Spec.Set_Layout(Layout);
        Fixture.Asset()->Set_Spec(Spec);
        auto Selection = ResolveSelection(Spawner);
        TestTrue(TEXT("Adapter composes authored pivot before spawner transform"),
            Selection.PivotWorld.Equals(Spec.Get_Pivot() * Spawner->GetActorTransform()));
        const auto Normal = Selection.PivotWorld.TransformVectorNoScale(FVector::UpVector);
        FVector2D Local;
        ECk_HexGeometryFailure Failure{};
        Geometry::TryGet_LocalCenter({1, -1}, Layout, Local, Failure);
        const auto Center = Selection.PivotWorld.TransformPosition(FVector{Local.X, Local.Y, 0});
        const auto Hit = ResolveCellFromRay(Selection, Center + Normal * 500, -Normal);
        TestTrue(TEXT("Transformed center ray picks exact axial address"), Hit.IsSet() && Hit.GetValue() == FCk_HexCoord{1,-1});
        TestFalse(TEXT("Behind ray rejected"), ResolveCellFromRay(Selection, Center + Normal * 500, Normal).IsSet());
        TestFalse(TEXT("Parallel ray rejected"), ResolveCellFromRay(Selection, Center + Normal * 500,
            Selection.PivotWorld.TransformVectorNoScale(FVector::ForwardVector)).IsSet());
        TestEqual(TEXT("Radius zero covers exactly one cell"), ComputeDisk(Spec, {}, 0).Num(), 1);
        TestEqual(TEXT("Radius one covers seven cells"), ComputeDisk(Spec, {}, 1).Num(), 7);
        TestTrue(TEXT("Edge disk is clipped to authored domain"), ComputeDisk(Spec, {2,0}, 1).Num() < 7);
        auto Domain = Spec.Get_Domain();
        TArray<FCk_HexCoord> Mask;
        Geometry::TryResolve_Domain(Domain, Mask, Failure);
        Mask.Remove(FCk_HexCoord{0,0});
        Domain.Set_Kind(ECk_HexDomainKind::Explicit);
        Domain.Set_RingRadius(0);
        Domain.Set_Coordinates(Mask);
        Spec.Set_Domain(Domain);
        Fixture.Asset()->Set_Spec(Spec);
        Selection = ResolveSelection(Spawner);
        TestTrue(TEXT("Holed explicit fixture is a valid editable selection"), Selection.IsValid());
        const auto Origin = Selection.PivotWorld.GetLocation();
        TestFalse(TEXT("Infinite-lattice hole winner is rejected"),
            ResolveCellFromRay(Selection, Origin + Normal * 500, -Normal).IsSet());
        TestEqual(TEXT("Disk membership excludes holes"), ComputeDisk(Spec, {}, 1).Num(), 6);
        Selection.PivotWorld.SetScale3D(FVector{1,2,1});
        TestFalse(TEXT("Nonuniform pivot rejected"), ResolveCellFromRay(Selection, Center + Normal * 500, -Normal).IsSet());

        Domain.Set_Coordinates({FCk_HexCoord{1,0}});
        Spec.Set_Domain(Domain);
        Fixture.Asset()->Set_Spec(Spec);
        Selection = ResolveSelection(Spawner);
        TestTrue(TEXT("Single-cell edge fixture is a valid editable selection"), Selection.IsValid());
        Geometry::TryGet_LocalCenter({1,0}, Layout, Local, Failure);
        const auto Edge = Selection.PivotWorld.TransformPosition(FVector{Local.X * 0.5, Local.Y * 0.5, 0});
        TestFalse(TEXT("Absent infinite-lattice edge winner cannot snap into present neighbor"),
            ResolveCellFromRay(Selection, Edge + Normal * 500, -Normal).IsSet());
    }
    return true;
}

auto FCkHexEditorTransactionalStroke::RunTest(const FString&) -> bool
{
    using namespace ck_test_hex_editor;
    FFixture Fixture{*this};
    if (!Fixture.Initialize()) { return false; }
    auto* TransientAsset = NewObject<UCk_HexGrid_AuthoringSpec>();
    TransientAsset->Set_Spec(Disk());
    TestFalse(TEXT("Transient-package authoring cannot begin persistent painting"), BeginStroke(TransientAsset).Valid);
    TestFalse(TEXT("Script class default cannot begin persistent painting"),
        BeginStroke(GetMutableDefault<UCk_HexGrid_AuthoringSpec>()).Valid);
    const auto Original = Recipe(Fixture.Asset()->Get_Spec());
    FBrush Brush;
    Brush.PaintDisabled = true;
    Brush.Tag = PaintTag();
    auto Stroke = BeginStroke(Fixture.Asset());
    TestTrue(TEXT("Production stage deduplicates repeated cells"), StageStroke(Stroke, {{0,0},{0,0},{1,0}}, Brush));
    TestTrue(TEXT("Staging leaves serialized asset untouched"), Recipe(Fixture.Asset()->Get_Spec()) == Original);
    TestTrue(TEXT("Changed stroke commits"), CommitStroke(Stroke));
    const auto Edited = Recipe(Fixture.Asset()->Get_Spec());
    TestTrue(TEXT("Stroke changes recipe"), Edited != Original);
    TestEqual(TEXT("Exactly one transaction"), Fixture.Transactions->GetQueueLength(), 1);
    TestEqual(TEXT("Duplicate cell stored once"), Fixture.Asset()->Get_Spec().Get_InitialDisabledCells().Num(), 2);
    TestTrue(TEXT("Package marked dirty"), Fixture.Asset()->GetOutermost()->IsDirty());
    GEditor->UndoTransaction();
    TestTrue(TEXT("Undo restores entire original recipe"), Recipe(Fixture.Asset()->Get_Spec()) == Original);
    GEditor->RedoTransaction();
    TestTrue(TEXT("Redo restores entire edited recipe"), Recipe(Fixture.Asset()->Get_Spec()) == Edited);
    auto NoOp = BeginStroke(Fixture.Asset());
    StageStroke(NoOp, {{0,0},{1,0}}, Brush);
    TestFalse(TEXT("No-op has no commit"), CommitStroke(NoOp));
    auto Cancelled = BeginStroke(Fixture.Asset());
    StageStroke(Cancelled, {{-1,0}}, Brush);
    CancelStroke(Cancelled);
    TestFalse(TEXT("Cancelled stroke has no commit"), CommitStroke(Cancelled));
    auto Invalid = BeginStroke(Fixture.Asset());
    Invalid.Candidate.Set_InitialDisabledCells({{MAX_int32,MAX_int32}});
    TestFalse(TEXT("Complete invalid candidate rejected"), CommitStroke(Invalid));
    TestEqual(TEXT("No-op, cancellation and invalid candidate preserve undo stack"), Fixture.Transactions->GetQueueLength(), 1);
    TestTrue(TEXT("Rejected strokes preserve exact recipe"), Recipe(Fixture.Asset()->Get_Spec()) == Edited);
    auto Erase = BeginStroke(Fixture.Asset());
    Brush.Erase = true;
    TestTrue(TEXT("Erase both manually disabled cells"),
        StageStroke(Erase, {{0,0},{1,0}}, Brush) && CommitStroke(Erase));
    TestTrue(TEXT("Erasing initial disabled restores the original recipe"), Recipe(Fixture.Asset()->Get_Spec()) == Original);

    Brush.PaintDisabled = false;
    Brush.Erase = false;
    auto Tagged = BeginStroke(Fixture.Asset());
    TestTrue(TEXT("Paint the selected per-cell tag"), StageStroke(Tagged, {{0,0}}, Brush) && CommitStroke(Tagged));
    Brush.Erase = true;
    Brush.Tag = DefaultTag();
    auto Inherited = BeginStroke(Fixture.Asset());
    TestTrue(TEXT("Inherited tag erase is a valid stroke"), StageStroke(Inherited, {{0,0}}, Brush));
    TestFalse(TEXT("Erasing an inherited tag produces no change"), CommitStroke(Inherited));
    Brush.Tag = PaintTag();
    auto Untagged = BeginStroke(Fixture.Asset());
    TestTrue(TEXT("Erase only the selected per-cell tag"), StageStroke(Untagged, {{0,0}}, Brush) && CommitStroke(Untagged));
    TestTrue(TEXT("Erasing the last per-cell tag preserves inherited tags and removes the empty override"),
        Recipe(Fixture.Asset()->Get_Spec()) == Original);
    return true;
}



namespace ck_test_hex_editor
{
    struct FRoundTripState
    {
        TSharedPtr<FFixture> Fixture;
        FAutomationTestBase* Test = nullptr;
        TArray<uint8> Original;
        TArray<uint8> Edited;
        TArray<FCk_Handle> Previous;
        double Deadline = 0;
        int32 Stage = 0;

        auto AllBoardsMatch(bool Disabled, bool Tagged) -> bool
        {
            for (int32 Index = 0; Index < Fixture->Spawners.Num(); ++Index)
            {
                const auto& Spawner = Fixture->Spawners[Index];
                if (!Spawner.IsValid()) { return false; }
                const auto Handle = Spawner->Get_EditorEntityHandle();
                if (!ck::IsValid(Handle) || !Grid::Has(Handle)
                    || (Previous.IsValidIndex(Index) && Previous[Index] == Handle)) { return false; }
                const auto Board = Grid::Cast(Handle);
                FCk_Handle_HexCell CellHandle;
                ECk_HexBoardFailure Failure{};
                bool IsDisabled = false;
                FGameplayTagContainer Tags;
                if (!Grid::TryGet_CellAt(Board, {0,0}, CellHandle, Failure)
                    || !Cell::TryGet_IsDisabled(CellHandle, IsDisabled) || IsDisabled != Disabled
                    || !Cell::TryGet_Tags(CellHandle, Tags) || Tags.HasTagExact(PaintTag()) != Tagged
                    || !Tags.HasTagExact(DefaultTag())) { return false; }
                FVector RuntimeCenter;
                if (!Grid::TryGet_CellWorldCenter(Board, {0,0}, RuntimeCenter, Failure)) { return false; }
                const auto Selection = ResolveSelection(Spawner.Get());
                if (!RuntimeCenter.Equals(Selection.PivotWorld.GetLocation(), 0.01)) { return false; }
            }
            return Fixture->Spawners.Num() == 2;
        }
        auto RememberBoards() -> void
        {
            Previous.Reset();
            for (const auto& Spawner : Fixture->Spawners) { Previous.Add(Spawner->Get_EditorEntityHandle()); }
        }
    };

    class FRoundTripCommand final : public IAutomationLatentCommand
    {
        TSharedRef<FRoundTripState> State;
    public:
        explicit FRoundTripCommand(TSharedRef<FRoundTripState> InState) : State(InState) {}
        auto Update() -> bool override
        {
            auto& S = *State;
            auto& T = *S.Test;
            auto& F = *S.Fixture;
            if (FPlatformTime::Seconds() > S.Deadline)
            {
                T.AddError(FString::Printf(TEXT("Saved hex board round trip timed out at stage %d; two real reconstructed boards/pivot/state never matched"), S.Stage));
                return true;
            }
            if (S.Stage == 0)
            {
                if (!S.AllBoardsMatch(false, false)) { return false; }
                T.AddInfo(TEXT("Two real editor ECS boards agree with the authored nonidentity pivot before painting."));
                S.RememberBoards();
                auto Stroke = BeginStroke(F.Asset());
                FBrush Brush;
                Brush.PaintDisabled = true;
                if (!T.TestTrue(TEXT("Production disabled stroke stages"), StageStroke(Stroke, {{0,0}}, Brush))) { return true; }
                Brush.PaintDisabled = false;
                Brush.Tag = PaintTag();
                if (!T.TestTrue(TEXT("Production tag stroke stages"), StageStroke(Stroke, {{0,0}}, Brush))
                    || !T.TestTrue(TEXT("Production stroke commits to serialized DataAsset"), CommitStroke(Stroke))) { return true; }
                S.Edited = Recipe(F.Asset()->Get_Spec());
                for (const auto& Spawner : F.Spawners)
                { T.TestEqual(TEXT("One rebuild request per sharing spawner for changed stroke"), GetRebuildCount(Spawner.Get()), 1); }
                S.Stage = 1;
                return false;
            }
            if (S.Stage == 1)
            {
                if (!S.AllBoardsMatch(true, true)) { return false; }
                auto NoOp = BeginStroke(F.Asset());
                FBrush Brush;
                StageStroke(NoOp, {{0,0}}, Brush);
                T.TestFalse(TEXT("Shared asset no-op cannot commit"), CommitStroke(NoOp));
                auto Cancelled = BeginStroke(F.Asset());
                StageStroke(Cancelled, {{1,0}}, Brush);
                CancelStroke(Cancelled);
                T.TestFalse(TEXT("Shared asset cancellation cannot commit"), CommitStroke(Cancelled));
                for (const auto& Spawner : F.Spawners)
                { T.TestEqual(TEXT("No-op and cancellation enqueue zero additional rebuilds"), GetRebuildCount(Spawner.Get()), 1); }
                S.RememberBoards();
                GEditor->UndoTransaction();
                T.TestTrue(TEXT("Undo restores exact original recipe"), Recipe(F.Asset()->Get_Spec()) == S.Original);
                S.Stage = 2;
                return false;
            }
            if (S.Stage == 2)
            {
                if (!S.AllBoardsMatch(false, false)) { return false; }
                S.RememberBoards();
                GEditor->RedoTransaction();
                T.TestTrue(TEXT("Redo restores exact painted recipe"), Recipe(F.Asset()->Get_Spec()) == S.Edited);
                S.Stage = 3;
                return false;
            }
            if (S.Stage == 3)
            {
                if (!S.AllBoardsMatch(true, true)) { return false; }
                for (const auto& Spawner : F.Spawners)
                { T.TestEqual(TEXT("Commit, undo and redo each refresh both loaded users once"), GetRebuildCount(Spawner.Get()), 3); }
                if (!T.TestTrue(TEXT("Edited native package saved to disk"), F.Save())) { return true; }
                S.RememberBoards();
                F.DestroySpawners();
                S.Stage = 4;
                return false;
            }
            if (S.Stage == 4)
            {
                // Release real editor entities before unloading the asset their construction recipes reference.
                for (const auto& Handle : S.Previous) { if (ck::IsValid(Handle)) { return false; } }
                S.Previous.Reset();
                if (!T.TestTrue(TEXT("Package and asset genuinely unload before disk reopen"), F.Unload())) { return true; }
                auto* Reloaded = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, *(F.PackageName + TEXT(".Board")));
                if (!T.TestNotNull(TEXT("Native asset reloads from saved package"), Reloaded)) { return true; }
                T.TestTrue(TEXT("Disk reopen retains exact logical cells, tags, blockers, layout and pivot"),
                    Recipe(Reloaded->Get_Spec()) == S.Edited);
                for (TFieldIterator<FProperty> It(Reloaded->GetClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
                {
                    if (It->HasAnyPropertyFlags(CPF_Transient)) { continue; }
                    T.TestTrue(FString::Printf(TEXT("Only recipe and revision persist on the board asset (%s)"), *It->GetName()),
                        It->GetFName() == TEXT("_Spec") || It->GetFName() == TEXT("_RecipeRevision"));
                }
                if (!T.TestNotNull(TEXT("First reopened asset user"), F.Spawn())
                    || !T.TestNotNull(TEXT("Second reopened asset user"), F.Spawn())) { return true; }
                for (const auto& Spawner : F.Spawners) { Spawner->EditorOnly_RebuildEntity(); }
                S.Stage = 5;
                return false;
            }
            if (S.Stage == 5)
            {
                if (!S.AllBoardsMatch(true, true)) { return false; }
                T.AddInfo(TEXT("Reopened package reconstructs two real boards with identical saved cell state and nonidentity pivot."));
                S.RememberBoards();
                F.DestroySpawners();
                S.Stage = 6;
                return false;
            }
            for (const auto& Handle : S.Previous) { if (ck::IsValid(Handle)) { return false; } }
            S.Previous.Reset();
            return T.TestTrue(TEXT("Reopened fixture releases cleanly after reconstructed users are destroyed"), F.Unload());
        }
    };
}

auto FCkHexEditorAssetRoundTrip::RunTest(const FString&) -> bool
{
    using namespace ck_test_hex_editor;
    auto State = MakeShared<FRoundTripState>();
    State->Test = this;
    State->Fixture = MakeShared<FFixture>(*this);
    if (!State->Fixture->Initialize()) { return false; }
    State->Original = Recipe(State->Fixture->Asset()->Get_Spec());
    if (!TestNotNull(TEXT("First sharing spawner"), State->Fixture->Spawn())
        || !TestNotNull(TEXT("Second sharing spawner"), State->Fixture->Spawn())) { return false; }
    for (const auto& Spawner : State->Fixture->Spawners) { Spawner->EditorOnly_RebuildEntity(); }
    State->Deadline = FPlatformTime::Seconds() + 25.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRoundTripCommand(State));
    return true;
}

#endif
