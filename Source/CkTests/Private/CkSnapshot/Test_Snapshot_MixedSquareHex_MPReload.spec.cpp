#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkAutoTest_Utils.h"
#include "CkGrid/2dGridSystem/Occupancy/Ck2dGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Fragment.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGrid_Projection.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"
#include "CkTests/Net/CkAutoTest_NetSubject_MixedGrid.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/Snapshot/CkSnapshot_TestHarness_Common.h"
#include <Engine/NetConnection.h>
#include <Engine/NetDriver.h>
#include <EngineUtils.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>

namespace ck_test_mixed_grid_reload
{
    using FBoard = ACk_AutoTest_NetSubject_MixedGrid_UE;
    using FPiece = ACk_AutoTest_NetSubject_MixedGridObject_UE;
    using FSquare = UCk_Utils_2dGridOccupancy_UE;
    using FHex = UCk_Utils_HexGrid_UE;
    using FPlacement = UCk_Utils_HexGridPlacement_UE;
    using namespace ck::auto_test::net;

    const FName Slot{TEXT("CkHexNetMixedSquareReload")};
    const FName TravelCVar{TEXT("net.AllowPIESeamlessTravel")};
    const FIntPoint SquareCell{2, 2};

    struct FRun
    {
        FGuid Moving = FGuid::NewGuid();
        FGuid Removing = FGuid::NewGuid();
        FGuid Named = FGuid::NewGuid();
        FGuid Contributor = FGuid::NewGuid();
        FGuid ObjectKey;
        FGuid AuthoredBlocker;
        TWeakObjectPtr<UWorld> OldServer;
        TWeakObjectPtr<UWorld> OldClient;
        TArray<FCk_Handle> OldHandles;
    };

    template<typename TActor>
    auto Subject(UWorld* InWorld) -> TActor*
    {
        if (!IsValid(InWorld)) { return nullptr; }
        TActorIterator<TActor> It(InWorld);
        if (!It) { return nullptr; }
        auto* Result = *It;
        ++It;
        return It ? nullptr : Result;
    }

    auto Entry(FGuid Id, FCk_HexCoord Coord, ECk_HexRotation Rotation) -> FCk_HexGridPlacement_View
    {
        FCk_HexGridPlacement_View Value;
        Value.Set_PlacementId(Id).Set_Anchor(Coord).Set_Rotation(Rotation)
            .Set_FootprintOffsets({FCk_HexCoord{}}).Set_Cells({Coord});
        return Value;
    }

    template<typename TResult>
    auto ReadBlueprintResult(UObject& Instance, FName Name, const FCk_Handle_HexGrid& Grid,
        const FGuid* PlacementId, TResult& OutResult) -> bool
    {
        auto* Fn = Instance.FindFunction(Name);
        if (!Fn || Fn->Script.IsEmpty()) { return false; }
        auto* BoardPin = FindFProperty<FStructProperty>(Fn, TEXT("InGrid"));
        auto* IdPin = FindFProperty<FStructProperty>(Fn, TEXT("InPlacementId"));
        auto* ReturnPin = CastField<FStructProperty>(Fn->GetReturnProperty());
        if (!BoardPin || BoardPin->Struct != FCk_Handle_HexGrid::StaticStruct() ||
            !ReturnPin || ReturnPin->Struct != TResult::StaticStruct() ||
            (PlacementId && (!IdPin || IdPin->Struct != TBaseStructure<FGuid>::Get()))) { return false; }
        FStructOnScope Frame(Fn);
        auto* Params = Frame.GetStructMemory();
        BoardPin->CopyCompleteValue(BoardPin->ContainerPtrToValuePtr<void>(Params), &Grid);
        if (PlacementId) { IdPin->CopyCompleteValue(IdPin->ContainerPtrToValuePtr<void>(Params), PlacementId); }
        Instance.ProcessEvent(Fn, Params);
        ReturnPin->CopyCompleteValue(&OutResult, ReturnPin->ContainerPtrToValuePtr<void>(Params));
        return true;
    }

    auto CheckWorld(UWorld* World, const FRun& Run, bool Edited) -> FString
    {
        auto* Board = Subject<FBoard>(World);
        auto* Piece = Subject<FPiece>(World);
        if (!Board || !Piece) { return TEXT("expected one board actor and one independent object actor"); }
        const auto Grid = Board->Get_HexGrid();
        const auto Square = Board->Get_SquareGrid();
        const auto Object = Piece->Get_HexObject();
        if (!FHex::Get_IsReady(Grid) || !ck::IsValid(Square) || !ck::IsValid(Object)) { return TEXT("features are pending"); }
        if (!Run.ObjectKey.IsValid() || ck::hex_grid::GetAcceptedObjectBindingId(Object) != Run.ObjectKey)
        { return TEXT("object binding GUID differs from pre-save authority"); }

        const auto SquarePlacement = FSquare::Get_PlacementAt(Square, SquareCell);
        if (!FSquare::Get_IsOccupied(Square, SquareCell) || !ck::IsValid(SquarePlacement) ||
            FSquare::Get_OccupantAt(Square, SquareCell) != Object ||
            FSquare::Get_PlacementForOccupant(Object) != SquarePlacement || FSquare::Get_Placements(Square).Num() != 1)
        { return TEXT("square cell, placement record and local occupant backref do not agree"); }

        const auto P = ck::hex_grid::ReadCommittedProjection(Grid);
        if (P._Placements.Num() != (Edited ? 2 : 3)) { return TEXT("unexpected hex placement count"); }
        const auto Has = [&](FGuid Id, FCk_HexCoord Coord, ECk_HexRotation Rotation, bool Bound)
        {
            const auto* V = P._Placements.FindByPredicate([&](const auto& Row){ return Row.Get_PlacementId() == Id; });
            if (!V || !ck::IsValid(V->Get_Handle()) || V->Get_Anchor() != Coord || V->Get_Rotation() != Rotation ||
                V->Get_Cells() != TArray<FCk_HexCoord>{Coord} || V->Get_FootprintOffsets() != TArray<FCk_HexCoord>{FCk_HexCoord{}})
            { return false; }
            if (Bound)
            {
                return V->Get_OccupantState() == ECk_HexGridOccupantState::Bound && V->Get_Occupant() == Object &&
                    V->Get_OccupantBindingId() == Run.ObjectKey &&
                    UCk_Utils_HexGridOccupancy_UE::TryGet_PlacementForOccupant(Object).Get_PlacementId() == Id;
            }
            return V->Get_OccupantState() == ECk_HexGridOccupantState::Anonymous &&
                !ck::IsValid(V->Get_Occupant()) && !V->Get_OccupantBindingId().IsValid();
        };
        if (!Has(Run.Named, {1,0}, ECk_HexRotation::Turn1, true)) { return TEXT("bound hex record differs"); }
        if (!Has(Run.Moving, Edited ? FCk_HexCoord{0,-1} : FCk_HexCoord{-1,0},
            Edited ? ECk_HexRotation::Turn3 : ECk_HexRotation::Turn2, false)) { return TEXT("moved hex record differs"); }
        if (!Edited && !Has(Run.Removing, {0,1}, ECk_HexRotation::Turn0, false)) { return TEXT("second anonymous record differs"); }
        if (P._Blockers.Num() != 2 ||
            !P._Blockers.ContainsByPredicate([&](const auto& B){ return B._BlockerId == Run.AuthoredBlocker &&
                !B._Active && B._Cells == TArray<FCk_HexCoord>{{-2,0}}; }) ||
            !P._Blockers.ContainsByPredicate([&](const auto& B){ return B._BlockerId == Run.Contributor &&
                B._Active && B._Cells == TArray<FCk_HexCoord>{{1,0}}; }) ||
            P._ManualBlocks.Num() != 1 || !P._ManualBlocks.Contains({1,0}))
        { return TEXT("blocker identities, contributors or manual vote differ"); }
        return {};
    }

    // The harness wait advances on timeout. Always run the same inspection as an explicit assertion.
    void Await(FAutomationTestBase& Test, const TCHAR* Label, TFunction<FString()> Inspect)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(FCk_NetAutoTest_Assertion::CreateLambda([&Test, Inspect]()
        { return Test.HasAnyErrors() || Inspect().IsEmpty(); }), 60.f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(&Test, FCk_NetAutoTest_Assertion::CreateLambda([&Test, Label, Inspect]()
        {
            if (Test.HasAnyErrors()) { return true; }
            return Test.TestEqual(Label, Inspect(), FString{});
        }), Label));
    }

    auto CheckBoth(const FRun& Run, bool Edited) -> FString
    {
        const auto Server = CheckWorld(Get_ServerWorld(), Run, Edited);
        if (!Server.IsEmpty()) { return TEXT("server: ") + Server; }
        const auto Client = CheckWorld(Get_ClientWorld(0), Run, Edited);
        return Client.IsEmpty() ? FString{} : TEXT("client: ") + Client;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexNetMixedSquareReload, "Ck.HexGrid.Net.MixedSquareHex_MPReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkHexNetMixedSquareReload::RunTest(const FString&)
{
    using namespace ck_test_mixed_grid_reload;
    using ck::auto_test::snapshot::Get_SnapshotSubsystem;
    auto Run = MakeShared<FRun>();
    UCk_Utils_AutoTest_UE::Request_PushCVarOverride(TravelCVar, TEXT("1"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
    {
        TestNotNull(TEXT("Spawned independent replicated object subject"), World->SpawnActor<FPiece>());
        TestNotNull(TEXT("Spawned replicated mixed board subject"), World->SpawnActor<FBoard>());
    })));
    Await(*this, TEXT("Both worlds composed correlated features"), []() -> FString
    {
        for (auto* World : {Get_ServerWorld(), Get_ClientWorld(0)})
        {
            auto* Board = Subject<FBoard>(World);
            auto* Piece = Subject<FPiece>(World);
            if (!Board || !Piece || !FHex::Get_IsReady(Board->Get_HexGrid()) || !ck::IsValid(Board->Get_SquareGrid()) ||
                !ck::hex_grid::GetAcceptedObjectBindingId(Piece->Get_HexObject()).IsValid()) { return TEXT("feature pending"); }
        }
        return {};
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
    {
        if (HasAnyErrors()) { return; }
        auto* Board = Subject<FBoard>(World);
        auto* Piece = Subject<FPiece>(World);
        if (!Board || !Piece) { AddError(TEXT("Mixed subjects disappeared")); return; }
        auto Grid = Board->Get_HexGrid();
        auto State = ck::hex_grid::ReadCommittedProjection(Grid);
        if (!TestEqual(TEXT("One authored blocker before placement"), State._Blockers.Num(), 1)) { return; }
        Run->AuthoredBlocker = State._Blockers[0]._BlockerId;
        Run->ObjectKey = ck::hex_grid::GetAcceptedObjectBindingId(Piece->Get_HexObject());
        auto Named = Entry(Run->Named, {1,0}, ECk_HexRotation::Turn1);
        Named.Set_OccupantState(ECk_HexGridOccupantState::Bound).Set_Occupant(Piece->Get_HexObject());
        State._Placements = {Named, Entry(Run->Moving, {-1,0}, ECk_HexRotation::Turn2),
            Entry(Run->Removing, {0,1}, ECk_HexRotation::Turn0)};
        if (!TestTrue(TEXT("Three committed hex records"), ck::hex_grid::TryCommitProjection(Grid, State).Get_Succeeded())) { return; }
        State._Blockers.Add({Run->Contributor, {}, {{1,0}}, true});
        State._ManualBlocks.Add({1,0});
        TestTrue(TEXT("Blocker and manual vote over placed object"), ck::hex_grid::TryCommitProjection(Grid, State).Get_Succeeded());
        TestTrue(TEXT("Same object placed on square grid"), Board->Request_PlaceSquare(Piece, SquareCell));
    })));
    Await(*this, TEXT("Exact live server/client state before save"), [Run]() { return CheckBoth(*Run, false); });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* Server)
    {
        if (HasAnyErrors()) { return; }
        Run->OldServer = Server;
        Run->OldClient = Get_ClientWorld(0);
        for (auto* World : {Server, Run->OldClient.Get()})
        {
            const auto* Board = Subject<FBoard>(World);
            const auto* Piece = Subject<FPiece>(World);
            if (!Board || !Piece) { AddError(TEXT("No subject when recording pre-travel identity")); return; }
            Run->OldHandles.Add(Board->Get_HexGrid());
            Run->OldHandles.Add(Piece->Get_HexObject());
        }
        auto* Sub = Get_SnapshotSubsystem(Server);
        if (!TestNotNull(TEXT("Save subsystem"), Sub)) { return; }
        Sub->Request_Save(Slot, {});
        const auto& Save = Sub->Get_LastSaveReport();
        if (!TestTrue(TEXT("Save succeeded without durable loss"),
            Save.Get_Result() == ECk_SnapshotResult::Success && Save.Get_Losses().IsEmpty())) { return; }
        Sub->Request_Load(Slot, {});
        TestTrue(TEXT("Real asynchronous reload started"), Sub->Get_IsLoadInProgress());
    })));
    Await(*this, TEXT("Travel restored the same square and hex identities on fresh worlds"), [Run]() -> FString
    {
        auto* Server = Get_ServerWorld();
        auto* Client = Get_ClientWorld(0);
        if (!IsValid(Server) || !IsValid(Client) || Server == Run->OldServer.Get() || Client == Run->OldClient.Get() ||
            !Server->HasBegunPlay() || !Client->HasBegunPlay() || Server->GetNetMode() != NM_ListenServer ||
            Client->GetNetMode() != NM_Client) { return TEXT("physical listen-server/client map travel incomplete"); }
        const auto* Driver = Client->GetNetDriver();
        if (!Driver || !Driver->ServerConnection || Driver->ServerConnection->GetConnectionState() != USOCK_Open)
        { return TEXT("post-travel client connection is not open"); }
        if (Run->OldHandles.ContainsByPredicate([](const auto& H) { return ck::IsValid(H); }))
        { return TEXT("old-world handle still live"); }
        auto* Sub = Get_SnapshotSubsystem(Server);
        if (!Sub || Sub->Get_IsLoadInProgress()) { return TEXT("load unfinished"); }
        const auto& Load = Sub->Get_LastLoadReport();
        if (Load.Get_Result() != ECk_SnapshotResult::Success || !Load.Get_IsAccountingClosed() || !Load.Get_PayloadLosses().IsEmpty())
        { return TEXT("load reported a loss or unfinished accounting"); }
        return CheckBoth(*Run, false);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
    {
        if (HasAnyErrors()) { return; }
        auto* Class = LoadClass<UObject>(nullptr,
            TEXT("/CkTests/CkGrid/HexGrid/BP_HexGrid_NetProjectionFixture.BP_HexGrid_NetProjectionFixture_C"));
        if (!TestNotNull(TEXT("Saved net consumer class after travel"), Class)) { return; }
        for (auto* LocalWorld : {World, Get_ClientWorld(0)})
        {
            auto* LocalBoard = Subject<FBoard>(LocalWorld);
            auto* LocalPiece = Subject<FPiece>(LocalWorld);
            if (!LocalBoard || !LocalPiece) { AddError(TEXT("Post-travel Blueprint subjects disappeared")); return; }
            auto* Instance = NewObject<UObject>(LocalWorld, Class);
            FCk_HexGridReadiness_Result Readiness;
            FCk_HexGridPlacement_Result Placement;
            const auto BoardHandle = LocalBoard->Get_HexGrid();
            TestTrue(TEXT("Compiled K2 reads coherent state and resolves the pre-save placement GUID after travel"),
                ReadBlueprintResult(*Instance, TEXT("Exercise_Get_Readiness"), BoardHandle, nullptr, Readiness) &&
                Readiness.Get_State() == ECk_HexGridReadiness::Ready &&
                ReadBlueprintResult(*Instance, TEXT("Exercise_TryGet_Placement"), BoardHandle, &Run->Named, Placement) &&
                Placement.Get_Succeeded() && Placement.Get_Placement().Get_PlacementId() == Run->Named &&
                Placement.Get_Placement().Get_Occupant() == LocalPiece->Get_HexObject());
        }
        const auto* Board = Subject<FBoard>(World);
        if (!Board) { AddError(TEXT("Post-travel authority board disappeared")); return; }
        auto Grid = Board->Get_HexGrid();
        FPlacement::Request_Move(Grid, FCk_Request_HexGridPlacement_Move{Run->Moving, {0,-1}, ECk_HexRotation::Turn3}, {});
        FPlacement::Request_Remove(Grid, FCk_Request_HexGridPlacement_Remove{Run->Removing}, {});
    })));
    Await(*this, TEXT("Ordinary post-load move/removal converge with square backref intact"),
        [Run]() { return CheckBoth(*Run, true); });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda([]()
    {
        UCk_Utils_AutoTest_UE::Request_PopCVarOverride(TravelCVar);
        return true;
    }), TEXT("Restore scoped PIE seamless-travel setting")));
    return true;
}

#endif
