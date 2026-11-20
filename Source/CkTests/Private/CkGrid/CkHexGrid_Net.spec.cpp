#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_Spec.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGridInitialization_Data.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGrid_Projection.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Fragment.h"
#include "CkTests/Net/CkAutoTest_NetSubject_MixedGrid.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include <Engine/NetConnection.h>
#include <EngineUtils.h>

namespace ck_test_hex_join
{
    using FBoard = ACk_AutoTest_NetSubject_MixedGrid_UE;
    using FPiece = ACk_AutoTest_NetSubject_MixedGridObject_UE;

    template<typename Actor>
    auto Find(UWorld* World) -> Actor*
    {
        if (!IsValid(World)) { return nullptr; }
        TActorIterator<Actor> It(World);
        return It ? *It : nullptr;
    }

    struct FExpected
    {
        FGuid Moved = FGuid::NewGuid();
        FGuid Kept = FGuid::NewGuid();
        FGuid Removed = FGuid::NewGuid();
        FGuid Named = FGuid::NewGuid();
        FGuid Contributor = FGuid::NewGuid();
        FGuid AuthoredContributor;
        FGuid ObjectKey;
        uint64 FinalServerRevision = 0;
    };

    auto At(FGuid Id, FCk_HexCoord Coord) -> FCk_HexGridPlacement_View
    {
        FCk_HexGridPlacement_View View;
        View.Set_PlacementId(Id).Set_Anchor(Coord).Set_FootprintOffsets({FCk_HexCoord{}}).Set_Cells({Coord});
        return View;
    }

    auto HasFinalProjection(UWorld* World, const FExpected& Expected) -> bool
    {
        auto* Board = Find<FBoard>(World);
        auto* Piece = Find<FPiece>(World);
        if (!Board || !Piece || !UCk_Utils_HexGrid_UE::Get_IsReady(Board->Get_HexGrid()) ||
            !ck::IsValid(Piece->Get_HexObject())) { return false; }
        const auto Current = ck::hex_grid::ReadCommittedProjection(Board->Get_HexGrid());
        if (Current._Placements.Num() != 3 || Current._Blockers.Num() != 2) { return false; }
        const auto Matches = [&](FGuid Id, FCk_HexCoord Coord, ECk_HexRotation Rotation, bool Bound)
        {
            const auto* Entry = Current._Placements.FindByPredicate([&](const auto& Row){ return Row.Get_PlacementId() == Id; });
            const auto Cell = UCk_Utils_HexGridOccupancy_UE::TryGet_PlacementAt(Board->Get_HexGrid(), Coord);
            return Cell.Get_Succeeded() && Cell.Get_PlacementId() == Id && Entry && Entry->Get_Anchor() == Coord && Entry->Get_Rotation() == Rotation &&
                Entry->Get_FootprintOffsets() == TArray<FCk_HexCoord>{FCk_HexCoord{}} &&
                Entry->Get_Cells() == TArray<FCk_HexCoord>{Coord} &&
                (Bound ? (Entry->Get_OccupantState() == ECk_HexGridOccupantState::Bound && Entry->Get_Occupant() == Piece->Get_HexObject() &&
                    Entry->Get_OccupantBindingId() == Expected.ObjectKey && Expected.ObjectKey.IsValid() &&
                    ck::hex_grid::GetAcceptedObjectBindingId(Piece->Get_HexObject()) == Expected.ObjectKey &&
                    UCk_Utils_HexGridOccupancy_UE::TryGet_PlacementForOccupant(Piece->Get_HexObject()).Get_PlacementId() == Id)
                       : Entry->Get_OccupantState() == ECk_HexGridOccupantState::Anonymous &&
                         !Entry->Get_OccupantBindingId().IsValid() && !ck::IsValid(Entry->Get_Occupant()));
        };
        return Matches(Expected.Moved, {-1,0}, ECk_HexRotation::Turn2, false) &&
            Matches(Expected.Kept, {0,1}, ECk_HexRotation::Turn0, false) &&
            Matches(Expected.Named, {1,0}, ECk_HexRotation::Turn0, true) &&
            Current._LocalTags.IsEmpty() && Current._ManualBlocks.Num() == 1 && Current._ManualBlocks.Contains({1,0}) &&
            !UCk_Utils_HexGridOccupancy_UE::TryGet_PlacementAt(Board->Get_HexGrid(), {}).Get_Succeeded() &&
            !UCk_Utils_HexGridOccupancy_UE::TryGet_PlacementAt(Board->Get_HexGrid(), {0,-1}).Get_Succeeded() &&
            Current._Blockers.ContainsByPredicate([&](const auto& B)
            { return B._BlockerId == Expected.AuthoredContributor && !B._Active && B._Cells == TArray<FCk_HexCoord>{{-2,0}}; }) &&
            Current._Blockers.ContainsByPredicate([&](const auto& B)
            { return B._BlockerId == Expected.Contributor && B._Active && B._Cells == TArray<FCk_HexCoord>{{1,0}}; });
    }

    void WaitAndAssert(FAutomationTestBase& Test, const FCk_NetAutoTest_Assertion& Predicate, const FString& Message)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(Predicate, 30.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(&Test, Predicate, Message));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexGrid_TrueLateJoin, "Ck.HexGrid.Net.LateJoin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkHexGrid_TrueLateJoin::RunTest(const FString&)
{
    using namespace ck_test_hex_join;
    using namespace ck::auto_test::net;
    auto Expected = MakeShared<FExpected>();
    auto Join = MakeShared<FCk_NetAutoTest_LateJoinResult>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* World)
    {
        World->SpawnActor<FPiece>();
        World->SpawnActor<FBoard>();
    })));
    WaitAndAssert(*this, FCk_NetAutoTest_Assertion::CreateLambda([]()
    {
        for (auto* World : {Get_ServerWorld(), Get_ClientWorld(0)})
        {
            auto* Board = Find<FBoard>(World);
            auto* Piece = Find<FPiece>(World);
            if (!Board || !Piece || !UCk_Utils_HexGrid_UE::Get_IsReady(Board->Get_HexGrid()) ||
                !ck::IsValid(Piece->Get_HexObject())) { return false; }
        }
        return true;
    }), TEXT("Original client and server have independent live board and object subjects"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this, Expected](UWorld* World)
    {
        auto* Subject = Find<FBoard>(World);
        auto* Piece = Find<FPiece>(World);
        if (!Subject || !Piece) { AddError(TEXT("Authority subjects unavailable")); return; }
        auto Grid = Subject->Get_HexGrid();
        auto State = ck::hex_grid::ReadCommittedProjection(Grid);
        if (!TestEqual(TEXT("One authored blocker before mutation"), State._Blockers.Num(), 1)) { return; }
        Expected->AuthoredContributor = State._Blockers[0]._BlockerId;
        Expected->ObjectKey = ck::hex_grid::GetAcceptedObjectBindingId(Piece->Get_HexObject());
        auto Named = At(Expected->Named, {1,0});
        Named.Set_OccupantState(ECk_HexGridOccupantState::Bound).Set_Occupant(Piece->Get_HexObject());
        State._Placements = {At(Expected->Moved, {}), At(Expected->Kept, {0,1}), At(Expected->Removed, {0,-1}), Named};
        if (!TestTrue(TEXT("Authority seeded four distinct placements"), ck::hex_grid::TryCommitProjection(Grid, State).Get_Succeeded())) { return; }
        State._Placements[0] = At(Expected->Moved, {-1,0}).Set_Rotation(ECk_HexRotation::Turn2);
        State._Placements.RemoveAt(2);
        State._ManualBlocks.Add({1,0});
        State._Blockers.Add({Expected->Contributor, {}, {{1,0}}, true});
        if (!TestTrue(TEXT("Authority moved, removed and blocked before join"), ck::hex_grid::TryCommitProjection(Grid, State).Get_Succeeded())) { return; }
        Expected->FinalServerRevision = ck::hex_grid::ReadCommittedProjection(Grid)._Revision;
    })));
    WaitAndAssert(*this, FCk_NetAutoTest_Assertion::CreateLambda([Expected]()
    { return HasFinalProjection(Get_ClientWorld(0), *Expected); }), TEXT("Existing connection converged before late join"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RequestLateJoin(Join, 30.0));
    WaitAndAssert(*this, FCk_NetAutoTest_Assertion::CreateLambda([Join, Expected]()
    {
        return Join->Status == ECk_NetAutoTest_LateJoinStatus::Joined && Join->ServerConnection.IsValid() &&
            HasFinalProjection(Join->ClientWorld.Get(), *Expected);
    }), TEXT("Distinct new client received the complete committed baseline and correlated its own occupant"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda([Expected]()
    {
        auto* Board = Find<FBoard>(Get_ServerWorld());
        return Board && HasFinalProjection(Get_ServerWorld(), *Expected) &&
            ck::hex_grid::ReadCommittedProjection(Board->Get_HexGrid())._Revision == Expected->FinalServerRevision;
    }), TEXT("No authority mutation was needed to bootstrap the new connection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexGrid_ClientConstructionProvenance, "Ck.HexGrid.Net.ClientConstructionProvenance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkHexGrid_ClientConstructionProvenance::RunTest(const FString&)
{
    using namespace ck_test_hex_join;
    using namespace ck::auto_test::net;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* World)
    { World->SpawnActor<FBoard>(); })));
    WaitAndAssert(*this, FCk_NetAutoTest_Assertion::CreateLambda([]()
    {
        auto* Subject = Find<FBoard>(Get_ClientWorld(0));
        return Subject && ck::IsValid(Subject->Get_HexGrid());
    }), TEXT("Client constructed its actual driver-correlated board"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0, FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
    {
        auto* Subject = Find<FBoard>(World);
        auto* Asset = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, ck::tests::hex_persistence::BoardAssetPath);
        if (!Subject || !Asset) { AddError(TEXT("Client construction fixtures unavailable")); return; }
        auto Parent = Subject->Get_HexGrid();
        if (!ck::IsValid(Parent)) { AddError(TEXT("Missing client parent")); return; }
        FCk_Handle Local = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Parent);
        if (!TestTrue(TEXT("Local subject created"), ck::IsValid(Local))) { return; }
        if (!UCk_Utils_Net_UE::Has(Local))
        {
            UCk_Utils_Net_UE::Add(Local, FCk_Net_ConnectionSettings{ECk_Replication::Replicates,
                ECk_Net_NetModeType::Client, ECk_Net_EntityNetRole::Proxy});
        }
        FCk_HexGrid_Spec Spec;
        FCk_HexGridRecipeIdentity Recipe;
        ECk_HexBoardFailure Failure{};
        const FCk_HexGridInitialization Init;
        FCk_Handle_HexGrid LocalBoard;
        TestTrue(TEXT("Local child has same proxy policy as incoming replica"), UCk_Utils_Net_UE::Has(Local) &&
            UCk_Utils_Net_UE::Get_Replication(Local) == ECk_Replication::Replicates &&
            UCk_Utils_Net_UE::Get_IsEntityNetMode_Client(Local) && !UCk_Utils_Net_UE::Get_HasAuthority(Local));
        TestTrue(TEXT("Independently composed local child is immediately ready"), Asset->TryResolveWithIdentity(Spec, Recipe, Failure) &&
            UCk_Utils_HexGrid_UE::TryAddForAuthoring(Local, FTransform::Identity, Spec, LocalBoard, Failure, &Init, &Recipe) &&
            UCk_Utils_HexGrid_UE::Get_IsReady(LocalBoard));
        TestTrue(TEXT("Driver-created parent was AwaitingReplication inside Construct"),
            Subject->Get_ConstructReadiness().Get_State() == ECk_HexGridReadiness::AwaitingReplication);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
#endif
