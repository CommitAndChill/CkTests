#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkHexPlacementTestListener.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkGrid/2dGridSystem/Grid/Ck2dGridSystem_Utils.h"
#include "CkGrid/2dGridSystem/Object/Ck2dGridObject_Utils.h"
#include "CkGrid/2dGridSystem/Occupancy/Ck2dGridOccupancy_Utils.h"
#include "CkGrid/2dGridSystem/Placement/Ck2dGridPlacement_Utils.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGridInitialization_Data.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_Spec.h"
#include "CkGrid/HexGridSystem/Blocker/CkHexGridBlocker_Utils.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Fragment.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGrid_Projection.h"
#include "CkGrid/HexGridSystem/Persistence/CkHexGridPersistence_Utils.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"
#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"

namespace ck_hex_persistence_atomic_test
{
    using namespace ck::hex_grid::persistence;
    using EStatus = ECk_Persistence_ApplyResult;
    using EFailure = ECk_HexGridFailure;

    auto MakeBoard(UWorld* InWorld, UCk_HexGrid_AuthoringSpec& InAsset) -> FCk_Handle_HexGrid
    {
        FCk_HexGrid_Spec Spec;
        FCk_HexGridRecipeIdentity Recipe;
        ECk_HexBoardFailure Reason{};
        if (NOT InAsset.TryResolveWithIdentity(Spec, Recipe, Reason))
        { return {}; }
        auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        UCk_Utils_Net_UE::Add(Entity, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate, ECk_Net_NetModeType::ClientAndHost,
            ECk_Net_EntityNetRole::Authority});
        FCk_Handle_HexGrid Board;
        const FCk_HexGridInitialization Declaration;
        UCk_Utils_HexGrid_UE::TryAddForAuthoring(Entity, FTransform::Identity, Spec, Board, Reason, &Declaration, &Recipe);
        return Board;
    }

    auto At(FGuid InId, FCk_HexCoord InAnchor) -> FCk_HexGridPlacement_PersistedEntry
    {
        FCk_HexGridPlacement_PersistedEntry Entry;
        Entry.Set_PlacementId(InId).Set_Anchor(InAnchor).Set_FootprintOffsets({FCk_HexCoord{}}).Set_Cells({InAnchor});
        return Entry;
    }

    auto Check(FAutomationTestBase& Test, UWorld* InWorld) -> void
    {
        using namespace ck::tests::hex_persistence;
        auto* Asset = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, BoardAssetPath);
        if (NOT Test.TestNotNull(TEXT("Persistence recipe asset"), Asset)) { return; }
        auto Board = MakeBoard(InWorld, *Asset);
        auto Other = MakeBoard(InWorld, *Asset);
        if (NOT Test.TestTrue(TEXT("Two durable authority boards composed"), ck::IsValid(Board) && ck::IsValid(Other))) { return; }
        auto Owner = FCk_Handle{Board};
        FCk_Handle_HexGridObject Piece;
        ECk_HexGridObject_Failure ObjectFailure{};
        if (NOT Test.TestTrue(TEXT("Composed real hex object"), UCk_Utils_HexGridObject_UE::TryCreate(Owner, {}, Piece, ObjectFailure))) { return; }

        FCk_2dGridSystem_Spec SquareSpec{FIntPoint{3,3}, FVector2D{100.,100.}};
        SquareSpec.Set_DefaultCellState(ECk_EnableDisable::Enable);
        auto Square = UCk_Utils_2dGridSystem_UE::Create(Owner, FTransform::Identity, SquareSpec);
        auto PieceHandle = FCk_Handle{Piece};
        UCk_Utils_2dGridObject_UE::Add(PieceHandle, FCk_2dGridObject_Spec{FIntPoint{1,1}});
        const auto SquarePlacement = UCk_Utils_2dGridPlacement_UE::Request_Place(Square, PieceHandle, {1,1}, ECk_CardinalRotation::None, {});
        if (NOT Test.TestTrue(TEXT("Square occupancy established before the hex restore"), ck::IsValid(SquarePlacement))) { return; }

        const auto Initial = ReadCommittedProjection(Board);
        if (NOT Test.TestEqual(TEXT("Asset supplies a real blocker"), Initial.Get_Blockers().Num(), 1)) { return; }
        const auto One = FGuid{1,0,0,1};
        const auto Two = FGuid{1,0,0,2};
        auto Baseline = Initial;
        Baseline.Get_Placements().Add(At(One, {}));
        auto Named = At(Two, {1,0});
        Named.Set_HasOccupantReference(true).Set_Occupant(Piece).Set_OccupantBindingId(ck::hex_grid::GetAcceptedObjectBindingId(Piece));
        Baseline.Get_Placements().Add(Named);
        Baseline.Get_CellRules().AddDefaulted_GetRef().Set_Coordinate({}).Set_ManualBlocked(true);
        auto Apply = [](FCk_Handle_HexGrid& Grid, const FCk_RepData_HexGridState& Data)
        { return TryReplaceCommittedProjection(Grid, Data, ECk_HexGridApplySource::Hydration); };
        if (NOT Test.TestTrue(TEXT("Baseline allows the square occupant and a manual vote over the placement"),
            Apply(Board, Baseline).Get_Status() == EStatus::Applied)) { return; }
        const auto Stable = ReadCommittedProjection(Board);
        const auto Before = ck::hex_grid::ReadCommittedProjection(Board);
        FCk_Handle_HexCell Center;
        ECk_HexBoardFailure CellFailure{};
        if (NOT Test.TestTrue(TEXT("Origin cell exists"), UCk_Utils_HexGrid_UE::TryGet_CellAt(Board, {}, Center, CellFailure))) { return; }
        const auto BlockerHandle = UCk_Utils_HexGridBlocker_UE::Get_BlockerWithTag(Board, AuthoredBlocker);

        int32 Events = 0;
        auto Listener = TStrongObjectPtr<UCk_HexPlacementTestListener>{NewObject<UCk_HexPlacementTestListener>()};
        Listener->Changed = [&Events](FName, FCk_Handle_HexGrid, const FCk_HexGridPlacement_View&) { ++Events; };
        ON_SCOPE_EXIT { Listener->Changed = nullptr; };
        FCk_Delegate_HexGridPlacement_ObjectPlaced Placed;
        Placed.BindDynamic(Listener.Get(), &UCk_HexPlacementTestListener::OnPlaced);
        UCk_Utils_HexGridPlacement_UE::BindTo_OnObjectPlaced(Board, Placed);
        FCk_Delegate_HexGridPlacement_ObjectMoved Moved;
        Moved.BindDynamic(Listener.Get(), &UCk_HexPlacementTestListener::OnMoved);
        UCk_Utils_HexGridPlacement_UE::BindTo_OnObjectMoved(Board, Moved);
        FCk_Delegate_HexGridPlacement_ObjectRemoved Removed;
        Removed.BindDynamic(Listener.Get(), &UCk_HexPlacementTestListener::OnRemoved);
        UCk_Utils_HexGridPlacement_UE::BindTo_OnObjectRemoved(Board, Removed);

        const auto CheckStable = [&]
        {
            const auto After = ReadCommittedProjection(Board);
            const auto Raw = ck::hex_grid::ReadCommittedProjection(Board);
            Test.TestTrue(TEXT("Complete durable projection unchanged"), FCk_RepData_HexGridState::StaticStruct()->CompareScriptStruct(&After, &Stable, 0));
            Test.TestEqual(TEXT("Committed revision unchanged"), Raw._Revision, Before._Revision);
            if (Raw._Placements.Num() == 2 && Before._Placements.Num() == 2)
            {
                Test.TestTrue(TEXT("Placement handles survive"), Raw._Placements[0].Get_Handle() == Before._Placements[0].Get_Handle() &&
                    Raw._Placements[1].Get_Handle() == Before._Placements[1].Get_Handle());
            }
            Test.TestTrue(TEXT("Blocker entity survives"), ck::IsValid(BlockerHandle) &&
                BlockerHandle == UCk_Utils_HexGridBlocker_UE::Get_BlockerWithTag(Board, AuthoredBlocker));
            bool Disabled = false;
            Test.TestTrue(TEXT("Physical manual vote survives"), UCk_Utils_HexCell_UE::TryGet_IsDisabled(Center, Disabled) && Disabled);
            Test.TestEqual(TEXT("No placement success signal escaped"), Events, 0);
        };
        Test.TestTrue(TEXT("Identical baseline replay succeeds"), Apply(Board, Stable).Get_Status() == EStatus::Applied);
        CheckStable();

        const auto Reject = [&](const TCHAR* Label, auto Mutate)
        {
            auto Broken = Stable;
            Mutate(Broken);
            const auto Result = Apply(Board, Broken);
            Test.TestTrue(Label, Result.Get_Status() == EStatus::Rejected && Result.Get_Failure() != EFailure::None);
            CheckStable();
        };
        Reject(TEXT("Last overlapping entry rejected"), [](auto& D) { D.Get_Placements()[1].Set_Anchor({}).Set_Cells({FCk_HexCoord{}}); });
        Reject(TEXT("Duplicate placement ID rejected"), [One](auto& D) { D.Get_Placements()[1].Set_PlacementId(One); });
        Reject(TEXT("Invalid rotation rejected"), [](auto& D) { D.Get_Placements()[1].Set_Rotation(static_cast<ECk_HexRotation>(9)); });
        Reject(TEXT("Malformed footprint rejected"), [](auto& D) { D.Get_Placements()[1].Set_FootprintOffsets({{0,0},{0,0}}); });
        Reject(TEXT("Out-of-domain footprint rejected"), [](auto& D) { D.Get_Placements()[1].Set_Anchor({10,10}).Set_Cells({{10,10}}); });
        Reject(TEXT("Wrong schema rejected"), [](auto& D) { D.Set_SchemaVersion(2); });
        Reject(TEXT("Changed recipe rejected"), [](auto& D) { D.Get_Recipe().Set_Fingerprint(FString::ChrN(40, TEXT('0'))); });
        Reject(TEXT("Duplicate rule coordinate rejected"), [](auto& D) { const auto Copy = D.Get_CellRules()[0]; D.Get_CellRules().Add(Copy); });
        Reject(TEXT("Out-of-domain rule rejected"), [](auto& D) { D.Get_CellRules()[0].Set_Coordinate({50,50}); });
        Reject(TEXT("Duplicate blocker name rejected"), [](auto& D)
        {
            auto Copy = D.Get_Blockers()[0];
            Copy.Set_BlockerId(FGuid{3,0,0,7});
            D.Get_Blockers().Add(Copy);
        });
        Reject(TEXT("Duplicate valid occupant rejected"), [&Named](auto& D)
        { D.Get_Placements()[0].Set_HasOccupantReference(true).Set_OccupantBindingId(Named.Get_OccupantBindingId()).Set_Occupant(Named.Get_Occupant()); });
        Reject(TEXT("Anonymous with binding GUID rejected"), [&Named](auto& D) { D.Get_Placements()[0].Set_OccupantBindingId(Named.Get_OccupantBindingId()); });

        auto Across = ReadCommittedProjection(Other);
        Across.Get_Placements().Add(Named);
        Test.TestTrue(TEXT("Same occupant rejected across hex boards"), Apply(Other, Across).Get_Status() == EStatus::Rejected);
        Test.TestTrue(TEXT("Other board remains empty"), ReadCommittedProjection(Other).Get_Placements().IsEmpty());
        Test.TestTrue(TEXT("Square occupant remains placed"), UCk_Utils_2dGridOccupancy_UE::Get_PlacementForOccupant(PieceHandle) == SquarePlacement);
        CheckStable();

        auto Empty = Initial;
        Empty.Get_Blockers().Reset();
        Test.TestTrue(TEXT("Present empty baseline accepted"), Apply(Board, Empty).Get_Status() == EStatus::Applied);
        const auto Cleared = ReadCommittedProjection(Board);
        Test.TestTrue(TEXT("No placements, contributors or rule deltas survive empty replacement"), Cleared.Get_Placements().IsEmpty() &&
            Cleared.Get_Blockers().IsEmpty() && Cleared.Get_CellRules().IsEmpty());
        FGameplayTagContainer Tags;
        Test.TestTrue(TEXT("Empty restores authored local tags"), UCk_Utils_HexCell_UE::TryGet_LocalTags(Center, Tags) && Tags.HasTagExact(AuthoredLocal));
        Test.TestTrue(TEXT("Empty releases the cross-board claim"), Apply(Other, Across).Get_Status() == EStatus::Applied);

        // Destruction starts synchronously, while the board's death-removal command drains later.
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(PieceHandle);
        if (!Test.TestTrue(TEXT("The retiring reference still passes default handle validity"),
            ck::IsValid(PieceHandle) && UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                PieceHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy))) { return; }
        const auto* Handler = FCk_PersistenceHandlerRegistry::Resolve(FCk_RepData_HexGridState::StaticStruct());
        if (!Test.TestTrue(TEXT("Board has a durable producer"), Handler && Handler->Produce)) { return; }
        auto OtherEntity = FCk_Handle{Other};
        const auto Payload = Handler->Produce(OtherEntity);
        const auto* Captured = Payload.IsSet() ? Payload->GetPtr<FCk_RepData_HexGridState>() : nullptr;
        if (!Test.TestTrue(TEXT("Capture retains the pending death reservation"), Captured && Captured->Get_Placements().Num() == 1)) { return; }
        const auto& Reservation = Captured->Get_Placements()[0];
        Test.TestTrue(TEXT("Retiring occupant is represented by its durable binding and placement only"),
            Reservation.Get_PlacementId() == Two && Reservation.Get_HasOccupantReference() &&
            Reservation.Get_OccupantBindingId() == Named.Get_OccupantBindingId() && Reservation.Get_Occupant() == FCk_Handle{});
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexGridPersistenceAtomic, "Ck.HexGrid.Persistence.AtomicReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkHexGridPersistenceAtomic::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this](UWorld* World) { ck_hex_persistence_atomic_test::Check(*this, World); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
#endif
