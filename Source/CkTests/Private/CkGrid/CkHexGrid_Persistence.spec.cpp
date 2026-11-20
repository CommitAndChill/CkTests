#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Persistence/CkPersistenceHandlerRegistry.h"
#include "CkCore/Validation/CkUntracedStructSafety.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_EntityScript.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGridObject_EntityScript.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Fragment.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGrid_Projection.h"
#include "CkGrid/HexGridSystem/Persistence/CkHexGridPersistence_Fragment_Data.h"
#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"
#include "CkTests/HexGrid/CkHexPersistenceGym.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"
#include "CkTests/Snapshot/CkSnapshot_TestHarness_Common.h"
#include <NativeGameplayTags.h>
#include <Misc/SecureHash.h>

namespace ck_test_hex_persistence
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(RuntimeBlocker, "Ck.AutoTest.HexPersistence.RuntimeBlocker");
    const FGuid Left{1, 0, 0, 1};
    const FGuid Center{1, 0, 0, 2};
    const FGuid Right{1, 0, 0, 3};
    const FGuid BlockerId{2, 0, 0, 1};

    auto Board(UWorld* InWorld) -> FCk_Handle_HexGrid
    {
        return UCk_Utils_HexGrid_UE::Cast(ck::auto_test::snapshot::ResolveEntityBySpawnRecipe(
            InWorld, UCk_HexPersistenceGymBoard::StaticClass()));
    }

    auto Object(UWorld* InWorld) -> FCk_Handle_HexGridObject
    {
        return UCk_Utils_HexGridObject_UE::Cast(ck::auto_test::snapshot::ResolveEntityBySpawnRecipe(
            InWorld, UCk_HexPersistenceGymObject::StaticClass()));
    }

    auto Placement(const FGuid& InId, const FCk_HexCoord& InAnchor, ECk_HexRotation InRotation)
        -> FCk_HexGridPlacement_View
    {
        auto View = FCk_HexGridPlacement_View{};
        View.Set_PlacementId(InId).Set_Anchor(InAnchor).Set_Rotation(InRotation);
        View.Set_FootprintOffsets({FCk_HexCoord{}}).Set_Cells({InAnchor});
        return View;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexGrid_AuthorityRoundTrip, "Ck.HexGrid.Persistence.AuthorityRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkHexGrid_AuthorityRoundTrip::RunTest(const FString&)
{
    using namespace ck_test_hex_persistence;
    using namespace ck::auto_test::snapshot;
    struct FExpected
    {
        FGuid ObjectId;
        FGuid AuthoredBlocker;
        FCk_Handle_HexGrid BeforeBoard;
        FCk_Handle_HexGridObject BeforeObject;
        TArray<FCk_Handle> BeforePlacements;
        int32 Loads = 0;
    };
    const auto Expected = MakeShared<FExpected>();
    auto Spec = FCk_SnapshotRoundTrip_Spec{};
    Spec.NumPIEClients = 1;
    Spec.NumCycles = 2;
    Spec.SaveEveryCycle = true;
    Spec.SlotName = TEXT("CkHexGridAuthorityPersistence");
    Spec.Spawn = FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
    {
        auto* Recipe = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, ck::tests::hex_persistence::BoardAssetPath);
        auto* Piece = LoadObject<UCk_HexGridObject_AuthoringSpec>(nullptr,
            TEXT("/CkTests/CkGrid/HexGrid/DA_HexGrid_RulesObject.DA_HexGrid_RulesObject"));
        if (!TestTrue(TEXT("Saved board and object assets load"), IsValid(Recipe) && IsValid(Piece))) { return; }
        auto Owner = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(World);
        UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner, UCk_HexPersistenceGymBoard::StaticClass(),
            FInstancedStruct::Make(FCk_HexGrid_SpawnParams{Recipe, FTransform::Identity}), {});
        UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner, UCk_HexPersistenceGymObject::StaticClass(),
            FInstancedStruct::Make(FCk_HexGridObject_SpawnParams{Piece}), {});
    });
    Spec.SubjectReady = FCk_NetAutoTest_Assertion::CreateLambda([]()
    {
        auto* World = Get_PostTravelServerWorld();
        return UCk_Utils_HexGrid_UE::Get_IsReady(Board(World)) && ck::IsValid(Object(World));
    });
    Spec.Mutate = FCk_NetAutoTest_ServerAction::CreateLambda([this, Expected](UWorld* World)
    {
        auto Grid = Board(World);
        const auto Piece = Object(World);
        if (!TestTrue(TEXT("Both composed features are available"), ck::IsValid(Grid) && ck::IsValid(Piece))) { return; }
        Expected->ObjectId = ck::hex_grid::GetAcceptedObjectBindingId(Piece);
        TestTrue(TEXT("Fresh object has an authority key"), Expected->ObjectId.IsValid());
        const auto* Handler = FCk_PersistenceHandlerRegistry::Find(FCk_RepData_HexGridState::StaticStruct());
        if (!TestTrue(TEXT("Board producer installed"), Handler && Handler->Produce)) { return; }
        auto BoardHandle = FCk_Handle{Grid};
        auto ObjectHandle = FCk_Handle{Piece};
        TestFalse(TEXT("Absent board produces no baseline"), Handler->Produce(ObjectHandle).IsSet());
        const auto Empty = Handler->Produce(BoardHandle);
        const auto* Initial = Empty.IsSet() ? Empty->GetPtr<FCk_RepData_HexGridState>() : nullptr;
        if (!TestTrue(TEXT("Unoccupied authored board produces a baseline with its recipe contributor"),
            Initial && Initial->Get_Placements().IsEmpty() && Initial->Get_Blockers().Num() == 1)) { return; }
        auto Projection = ck::hex_grid::ReadCommittedProjection(Grid);
        if (!TestEqual(TEXT("Recipe seeds one named contributor"), Projection._Blockers.Num(), 1)) { return; }
        Expected->AuthoredBlocker = Projection._Blockers[0]._BlockerId;
        Projection._Placements.Add(Placement(Left, {-1,0}, ECk_HexRotation::Turn1));
        Projection._Placements.Add(Placement(Center, {}, ECk_HexRotation::Turn2));
        auto Named = Placement(Right, {}, ECk_HexRotation::Turn0);
        Named.Set_FootprintOffsets({{1,0}}).Set_Cells({{1,0}}).Set_Occupant(Piece)
            .Set_OccupantState(ECk_HexGridOccupantState::Bound);
        Projection._Placements.Add(Named);
        if (!TestTrue(TEXT("Three distinct placements committed"),
            ck::hex_grid::TryCommitProjection(Grid, Projection).Get_Succeeded())) { return; }
        Projection = ck::hex_grid::ReadCommittedProjection(Grid);
        Projection._ManualBlocks.Add({0,-1});
        Projection._LocalTags.Add(FCk_HexCoord{}, FGameplayTagContainer{});
        Projection._Blockers.Add({BlockerId, RuntimeBlocker, {{1,0}}, true});
        TestTrue(TEXT("Rules and blocker changed after placement"),
            ck::hex_grid::TryCommitProjection(Grid, Projection).Get_Succeeded());
    });
    Spec.PostSave = FCk_NetAutoTest_ServerAction::CreateLambda([this, Expected](UWorld* World)
    {
        const auto* Snapshot = Get_SnapshotSubsystem(World);
        if (!TestNotNull(TEXT("Capture subsystem"), Snapshot)) { return; }
        const auto& Report = Snapshot->Get_LastSaveReport();
        TestTrue(TEXT("Save completed without durable losses"),
            Report.Get_Result() == ECk_SnapshotResult::Success && Report.Get_Losses().IsEmpty());
        Expected->BeforeBoard = Board(World);
        Expected->BeforeObject = Object(World);
        Expected->BeforePlacements.Reset();
        for (const auto& Placement : ck::hex_grid::ReadCommittedProjection(Expected->BeforeBoard)._Placements)
        { Expected->BeforePlacements.Add(Placement.Get_Handle()); }
    });
    Spec.Assert = FCk_NetAutoTest_Assertion::CreateLambda([this, Expected]()
    {
        auto* World = Get_PostTravelServerWorld();
        const auto Grid = Board(World);
        const auto Piece = Object(World);
        const auto* Snapshot = Get_SnapshotSubsystem(World);
        if (!TestTrue(TEXT("Fresh world reconstructed both recipes and a ready board"),
            Snapshot && UCk_Utils_HexGrid_UE::Get_IsReady(Grid) && ck::IsValid(Piece))) { return false; }
        TestTrue(TEXT("Load report is successful"), Snapshot->Get_LastLoadReport().Get_Result() == ECk_SnapshotResult::Success);
        TestTrue(TEXT("Load report accounting closes"), Snapshot->Get_LastLoadReport().Get_IsAccountingClosed());
        TestTrue(TEXT("Board and object are physically new on every cycle"),
            !ck::IsValid(Expected->BeforeBoard) && !ck::IsValid(Expected->BeforeObject) &&
            Grid != Expected->BeforeBoard && Piece != Expected->BeforeObject);
        TestEqual(TEXT("Exactly three pre-travel placement handles captured"), Expected->BeforePlacements.Num(), 3);
        for (const auto& Placement : Expected->BeforePlacements)
        { TestFalse(TEXT("Old placement handle cannot survive physical world destruction"), ck::IsValid(Placement)); }
        TestEqual(TEXT("Reconstruction has exactly the recipe's nineteen cells"), UCk_Utils_HexGrid_UE::Get_Cells(Grid).Num(), 19);
        TestTrue(TEXT("Object independently recovered binding identity"),
            Expected->ObjectId.IsValid() && ck::hex_grid::GetAcceptedObjectBindingId(Piece) == Expected->ObjectId);
        const auto P = ck::hex_grid::ReadCommittedProjection(Grid);
        TestEqual(TEXT("Exact placement count after load"), P._Placements.Num(), 3);
        const auto CheckPlacement = [this, &P, &Piece, Expected](const FGuid& Id, FCk_HexCoord Anchor,
            ECk_HexRotation Rotation, FCk_HexCoord Cell, bool Bound)
        {
            const auto* V = P._Placements.FindByPredicate([&](const auto& Row) { return Row.Get_PlacementId() == Id; });
            if (!TestNotNull(TEXT("Stable placement ID exists"), V)) { return; }
            TestTrue(TEXT("Anchor, rotation and occupied cells match"), V->Get_Anchor() == Anchor &&
                V->Get_Rotation() == Rotation && V->Get_Cells() == TArray<FCk_HexCoord>{Cell});
            TestTrue(TEXT("Only the referenced placement binds its fresh-world object"),
                Bound ? (V->Get_Occupant() == Piece && V->Get_OccupantState() == ECk_HexGridOccupantState::Bound)
                      : (!ck::IsValid(V->Get_Occupant()) && V->Get_OccupantState() == ECk_HexGridOccupantState::Anonymous));
            TestTrue(TEXT("Only referenced placement preserves the accepted object binding key"),
                Bound ? V->Get_OccupantBindingId() == Expected->ObjectId : !V->Get_OccupantBindingId().IsValid());
            TestTrue(TEXT("Placement footprint retains its exact authored offsets"),
                V->Get_FootprintOffsets() == (Bound ? TArray<FCk_HexCoord>{{1,0}} : TArray<FCk_HexCoord>{{0,0}}));
        };
        CheckPlacement(Left, {-1,0}, ECk_HexRotation::Turn1, {-1,0}, false);
        CheckPlacement(Center, {}, ECk_HexRotation::Turn2, {}, false);
        CheckPlacement(Right, {}, ECk_HexRotation::Turn0, {1,0}, true);
        const auto* Tags = P._LocalTags.Find({});
        TestTrue(TEXT("Authored tags explicitly cleared"), Tags && Tags->IsEmpty() && P._LocalTags.Num() == 1);
        TestTrue(TEXT("Manual blocked vote retained"), P._ManualBlocks.Num() == 1 && P._ManualBlocks.Contains({0,-1}));
        TestEqual(TEXT("Exact contributor count"), P._Blockers.Num(), 2);
        TestTrue(TEXT("Original authored contributor retained"), P._Blockers.ContainsByPredicate([&](const auto& B)
        { return B._BlockerId == Expected->AuthoredBlocker && !B._Active && B._Cells == TArray<FCk_HexCoord>{{-2,0}}; }));
        TestTrue(TEXT("Runtime blocker retained over existing placement"), P._Blockers.ContainsByPredicate([](const auto& B)
        { return B._BlockerId == BlockerId && B._Name == RuntimeBlocker && B._Active && B._Cells == TArray<FCk_HexCoord>{{1,0}}; }));
        for (const auto Coordinate : {FCk_HexCoord{0,-1}, FCk_HexCoord{1,0}, FCk_HexCoord{-2,0}})
        {
            FCk_Handle_HexCell Cell;
            ECk_HexBoardFailure Failure{};
            bool Disabled = false;
            TestTrue(TEXT("Recomputed blocked votes agree with exact restored contributors"),
                UCk_Utils_HexGrid_UE::TryGet_CellAt(Grid, Coordinate, Cell, Failure) &&
                UCk_Utils_HexCell_UE::TryGet_IsDisabled(Cell, Disabled) && Disabled == (Coordinate != FCk_HexCoord{-2,0}));
        }
        ++Expected->Loads;
        return !HasAnyErrors();
    });
    EnqueueRoundTrip(this, Spec);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda(
        [Expected]() { return Expected->Loads == 2; }), TEXT("Two distinct world reloads were checked")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexGrid_PersistenceSchema, "Ck.HexGrid.Persistence.SchemaAndRegistration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexGrid_PersistenceSchema::RunTest(const FString&)
{
    for (const auto* Type : {FCk_RepData_HexGridState::StaticStruct(), FCk_RepData_HexGridObjectIdentity::StaticStruct()})
    {
        const auto* Entry = FCk_PersistenceHandlerRegistry::Find(Type);
        TestTrue(Type->GetName(), Entry && Entry->Posture == ECk_Snapshot_Posture::Durable &&
            Entry->Produce && Entry->HydrationApply && Entry->NetApply && Entry->NetRemove);
        const auto Safety = ck::Analyze_UntracedStructSafety(Type);
        TestTrue(FString::Printf(TEXT("%s schema is GC independent (%s)"), *Type->GetName(), *Safety.FailurePath),
            Safety.Safety == ck::ECk_UntracedStructSafety::GcIndependent);
    }
    const auto CheckFields = [this](const UScriptStruct* Type, std::initializer_list<const TCHAR*> Names)
    {
        for (const auto* Name : Names)
        {
            const auto* Field = Type->FindPropertyByName(FName{Name});
            TestTrue(FString::Printf(TEXT("%s.%s participates in tagged serialization"), *Type->GetName(), Name),
                Field && !Field->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization));
        }
    };
    CheckFields(FCk_RepData_HexGridState::StaticStruct(), {TEXT("_SchemaVersion"), TEXT("_Recipe"), TEXT("_Placements"),
        TEXT("_Blockers"), TEXT("_CellRules")});
    CheckFields(FCk_HexGridRecipeIdentity::StaticStruct(), {TEXT("_AssetPath"),TEXT("_RecipeRevision"),TEXT("_Fingerprint")});
    CheckFields(FCk_HexGridPlacement_PersistedEntry::StaticStruct(), {TEXT("_PlacementId"), TEXT("_OccupantBindingId"),
        TEXT("_HasOccupantReference"), TEXT("_Occupant"), TEXT("_FootprintOffsets"), TEXT("_RequiredCellTags"),
        TEXT("_ForbiddenCellTags"), TEXT("_Anchor"), TEXT("_Rotation"), TEXT("_Connectivity"), TEXT("_Cells")});
    CheckFields(FCk_HexGridBlocker_PersistedEntry::StaticStruct(), {TEXT("_BlockerId"),TEXT("_Name"),TEXT("_Cells"),TEXT("_Active")});
    CheckFields(FCk_HexGridCellRuleDelta::StaticStruct(), {TEXT("_Coordinate"),TEXT("_LocalTags"),TEXT("_ManualBlocked")});
    CheckFields(FCk_RepData_HexGridObjectIdentity::StaticStruct(), {TEXT("_SchemaVersion"),TEXT("_BindingId")});
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexGrid_RecipeFingerprint, "Ck.HexGrid.Persistence.RecipeFingerprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexGrid_RecipeFingerprint::RunTest(const FString&)
{
    auto Spec = FCk_HexGrid_Spec{};
    TArray<uint8> Bytes;
    auto Failure = ECk_HexBoardFailure::None;
    if (!TestTrue(TEXT("Default recipe encodes"), ck::hex_grid::TryEncodeRecipe(Spec, Bytes, Failure))) { return false; }
    TestEqual(TEXT("Frozen canonical v1 wire bytes"), BytesToHex(Bytes.GetData(), Bytes.Num()),
        FString{TEXT("434B4845580100010000000000005940000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000F03F000000000000F03F000000000000F03F000000000000F03F01000000000000000000000000000000000000000000000000000000")});
    auto* Asset = NewObject<UCk_HexGrid_AuthoringSpec>();
    FCk_HexGridRecipeIdentity Identity;
    FCk_HexGrid_Spec Resolved;
    if (!TestTrue(TEXT("Transient recipe resolves"), Asset->TryResolveWithIdentity(Resolved, Identity, Failure))) { return false; }
    TestEqual(TEXT("Fingerprint agrees with independent SHA-1"), Identity.Get_Fingerprint(),
        FString{TEXT("f57451f96da0a6a39ced6e589d6fc0117e17a11d")});
    TestTrue(TEXT("Transient recipe carries no durable path"), Identity.Get_AssetPath().IsEmpty());
    const auto A = FCk_HexCoord{-1,0};
    const auto B = FCk_HexCoord{0,1};
    auto Domain = FCk_HexDomain{};
    Domain.Set_Kind(ECk_HexDomainKind::Explicit).Set_Coordinates({A, B});
    Spec.Set_Domain(Domain).Set_InitialDisabledCells({A, B}).Set_Blockers({
        FCk_HexGridAuthoredBlocker{{A, B}, {}, true}, FCk_HexGridAuthoredBlocker{{A}, {}, false}});
    if (!TestTrue(TEXT("Two-cell recipe encodes"), ck::hex_grid::TryEncodeRecipe(Spec, Bytes, Failure))) { return false; }
    const auto Expected = Bytes;
    Domain.Set_Coordinates({B, A});
    Spec.Set_Domain(Domain).Set_InitialDisabledCells({B, A}).Set_Blockers({
        FCk_HexGridAuthoredBlocker{{A}, {}, false}, FCk_HexGridAuthoredBlocker{{B, A}, {}, true}});
    Spec.Set_Pivot(FTransform{FQuat{0,0,0,-1}, FVector{-0.0,0,0}});
    TestTrue(TEXT("Permutation and equivalent transform preserve all bytes"),
        ck::hex_grid::TryEncodeRecipe(Spec, Bytes, Failure) && Expected == Bytes);
    Spec.Set_InitialDisabledCells({B});
    TestTrue(TEXT("Authored vote changes fingerprint input"),
        ck::hex_grid::TryEncodeRecipe(Spec, Bytes, Failure) && Expected != Bytes);
    Asset->Set_RecipeRevision(2);
    TestTrue(TEXT("Explicit migration revision is independent of the fingerprint"),
        Asset->TryResolveWithIdentity(Resolved, Identity, Failure) && Identity.Get_RecipeRevision() == 2 &&
        Identity.Get_Fingerprint() == TEXT("f57451f96da0a6a39ced6e589d6fc0117e17a11d"));
    Asset->Set_RecipeRevision(0);
    TestFalse(TEXT("Zero revision is rejected"), Asset->TryResolveWithIdentity(Resolved, Identity, Failure));
    TestTrue(TEXT("Rejected recipe publishes no fingerprint"), Identity.Get_Fingerprint().IsEmpty());
    return !HasAnyErrors();
}

#endif
