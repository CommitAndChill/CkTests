#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkGrid/2dGridSystem/Occupancy/Ck2dGridOccupancy_Utils.h"
#include "CkGrid/2dGridSystem/Placement/Ck2dGridPlacement_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Fragment.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGrid_Projection.h"
#include "CkGrid/HexGridSystem/Persistence/CkHexGridPersistence_Utils.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"
#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"
#include "CkTests/Net/CkAutoTest_NetSubject_MixedGrid.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "../UnitTests/CkGrid/CkHexPlacementTestListener.h"
#include <EngineUtils.h>

namespace ck_hex_net_membership
{
    using BoardActor = ACk_AutoTest_NetSubject_MixedGrid_UE;
    using ObjectActor = ACk_AutoTest_NetSubject_MixedGridObject_UE;
    using Occupancy = UCk_Utils_HexGridOccupancy_UE;
    using Square = UCk_Utils_2dGridOccupancy_UE;
    using Placement = UCk_Utils_HexGridPlacement_UE;
    using namespace ck::auto_test::net;
    using ck::hex_grid::ReadCommittedProjection;
    using ck::hex_grid::TryCommitProjection;

    template <typename T>
    T* Find(UWorld* World, const T* Exclude = nullptr)
    {
        if (!IsValid(World)) { return nullptr; }
        for (TActorIterator<T> It(World); It; ++It)
        { if (*It != Exclude) { return *It; } }
        return nullptr;
    }

    struct FWorldSubject
    {
        TWeakObjectPtr<BoardActor> Board;
        FCk_Handle_HexGrid Grid;
        FCk_Handle_HexGridObject Object;
    };

    struct FSession
    {
        FWorldSubject Server;
        FWorldSubject Client;
        FGuid Moved = FGuid::NewGuid();
        FGuid Removed = FGuid::NewGuid();
        FGuid Named = FGuid::NewGuid();
        FGuid Blocker = FGuid::NewGuid();
        FGuid Key;
        FCk_Handle_HexGridPlacement ClientPlacement;
        uint64 BeforeReplay = 0;
        FGuid FirstReplayId;
        TStrongObjectPtr<UCk_HexPlacementTestListener> Listener;
        int32 ClientPlacementEvents = 0;
    };

    void Await(FAutomationTestBase& Test, FCk_NetAutoTest_Assertion Condition, const TCHAR* Message)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(Condition, 20.f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(&Test, Condition, Message));
    }

    bool Resolve(UWorld* World, FWorldSubject& Subject)
    {
        auto* Board = Find<BoardActor>(World);
        auto* Piece = Find<ObjectActor>(World);
        if (!Board || !Piece || !UCk_Utils_HexGrid_UE::Get_IsReady(Board->Get_HexGrid()) ||
            !ck::hex_grid::GetAcceptedObjectBindingId(Piece->Get_HexObject()).IsValid()) { return false; }
        Subject = {Board, Board->Get_HexGrid(), Piece->Get_HexObject()};
        return true;
    }

    void Begin(FAutomationTestBase& Test, TSharedRef<FSession> Session)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* World)
        {
            World->SpawnActor<BoardActor>();
            World->SpawnActor<ObjectActor>();
        })));
        Await(Test, FCk_NetAutoTest_Assertion::CreateLambda([Session]()
        {
            if (!Resolve(Get_ServerWorld(), Session->Server) || !Resolve(Get_ClientWorld(0), Session->Client)) { return false; }
            Session->Key = ck::hex_grid::GetAcceptedObjectBindingId(Session->Server.Object);
            return Session->Key == ck::hex_grid::GetAcceptedObjectBindingId(Session->Client.Object) &&
                Session->Server.Object != Session->Client.Object && Session->Server.Grid != Session->Client.Grid;
        }), TEXT("Server and real client constructed distinct handles and accepted the same object key"));
    }

    FCk_HexGridPlacement_View One(FGuid Id, FCk_HexCoord At)
    {
        FCk_HexGridPlacement_View Row;
        Row.Set_PlacementId(Id).Set_Anchor(At).Set_FootprintOffsets({FCk_HexCoord{}}).Set_Cells({At});
        return Row;
    }

    bool CheckStage(const FSession& Session, const FWorldSubject& Subject, bool Moved, bool Died)
    {
        if (!UCk_Utils_HexGrid_UE::Get_IsReady(Subject.Grid)) { return false; }
        const auto P = ReadCommittedProjection(Subject.Grid);
        const auto* LocalTags = P._LocalTags.Find(FCk_HexCoord{});
        if (P._Placements.Num() != (Died ? 1 : Moved ? 2 : 3) || P._Blockers.Num() != (Moved ? 1 : 2) ||
            (!Moved && (!LocalTags || !LocalTags->IsEmpty() || !P._ManualBlocks.Contains({1,0}))) ||
            (Moved && (!P._ManualBlocks.IsEmpty() || !P._LocalTags.IsEmpty()))) { return false; }
        const auto Cells = Moved ? TArray<FCk_HexCoord>{{-1,1},{0,1}} : TArray<FCk_HexCoord>{{-1,0},{0,0}};
        const auto Anchor = Moved ? FCk_HexCoord{0,1} : FCk_HexCoord{-1,0};
        const auto Multi = Occupancy::TryGet_Placement(Subject.Grid, Session.Moved);
        if (!Multi.Get_Succeeded()) { return false; }
        const auto& V = Multi.Get_Placement();
        if (V.Get_Anchor() != Anchor || V.Get_Rotation() != (Moved ? ECk_HexRotation::Turn3 : ECk_HexRotation::Turn0) ||
            V.Get_Cells() != Cells || V.Get_FootprintOffsets() != TArray<FCk_HexCoord>{{0,0},{1,0}} ||
            V.Get_OccupantState() != ECk_HexGridOccupantState::Anonymous || V.Get_OccupantBindingId().IsValid() ||
            !V.Get_RequiredCellTags().HasTagExact(ck::tests::hex_persistence::AuthoredLocal)) { return false; }
        for (const auto& Coord : Cells)
        { if (Occupancy::TryGet_PlacementAt(Subject.Grid, Coord).Get_PlacementId() != Session.Moved) { return false; } }
        const auto Removed = Occupancy::TryGet_Placement(Subject.Grid, Session.Removed);
        if (Removed.Get_Succeeded() == Moved || (!Moved && (Removed.Get_Placement().Get_Cells() != TArray<FCk_HexCoord>{{0,-1}} ||
            Removed.Get_Placement().Get_OccupantState() != ECk_HexGridOccupantState::Anonymous))) { return false; }
        if (!Moved && !P._Blockers.ContainsByPredicate([&](const auto& B)
            { return B._BlockerId == Session.Blocker && B._Active && B._Cells == TArray<FCk_HexCoord>{{1,0}}; })) { return false; }
        const auto Named = Occupancy::TryGet_Placement(Subject.Grid, Session.Named);
        if (Died) { return !Named.Get_Succeeded() && !Occupancy::TryGet_PlacementAt(Subject.Grid, {1,0}).Get_Succeeded(); }
        return Named.Get_Succeeded() && Named.Get_Placement().Get_OccupantState() == ECk_HexGridOccupantState::Bound &&
            Named.Get_Placement().Get_Anchor() == FCk_HexCoord{1,0} && Named.Get_Placement().Get_Occupant() == Subject.Object &&
            Named.Get_Placement().Get_OccupantBindingId() == Session.Key &&
            Occupancy::TryGet_PlacementForOccupant(Subject.Object).Get_PlacementId() == Session.Named;
    }

    void WatchClient(FSession& Session)
    {
        Session.Listener.Reset(NewObject<UCk_HexPlacementTestListener>());
        Session.Listener->Changed = [&Session](FName, FCk_Handle_HexGrid, const FCk_HexGridPlacement_View&)
        { ++Session.ClientPlacementEvents; };
        FCk_Delegate_HexGridPlacement_ObjectPlaced Placed;
        FCk_Delegate_HexGridPlacement_ObjectMoved Moved;
        FCk_Delegate_HexGridPlacement_ObjectRemoved Removed;
        Placed.BindDynamic(Session.Listener.Get(), &UCk_HexPlacementTestListener::OnPlaced);
        Moved.BindDynamic(Session.Listener.Get(), &UCk_HexPlacementTestListener::OnMoved);
        Removed.BindDynamic(Session.Listener.Get(), &UCk_HexPlacementTestListener::OnRemoved);
        Placement::BindTo_OnObjectPlaced(Session.Client.Grid, Placed);
        Placement::BindTo_OnObjectMoved(Session.Client.Grid, Moved);
        Placement::BindTo_OnObjectRemoved(Session.Client.Grid, Removed);
    }

    bool MixedMember(const FWorldSubject& Subject, const FGuid Id)
    {
        if (!Subject.Board.IsValid() || !ck::IsValid(Subject.Object)) { return false; }
        const auto G = Subject.Board->Get_SquareGrid();
        const FIntPoint Coord{2,3};
        const auto At = Square::Get_PlacementAt(G, Coord);
        return ck::IsValid(At) && Square::Get_OccupantAt(G, Coord) == Subject.Object &&
            Square::Get_PlacementForOccupant(Subject.Object) == At && UCk_Utils_2dGridPlacement_UE::Get_Anchor(At) == Coord &&
            UCk_Utils_2dGridPlacement_UE::Get_Grid(At) == G && Id.IsValid() &&
            Occupancy::TryGet_PlacementForOccupant(Subject.Object).Get_PlacementId() == Id &&
            Occupancy::TryGet_PlacementAt(Subject.Grid, {1,0}).Get_PlacementId() == Id;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexNet_IdentityReplayMoveRemove, "Ck.HexGrid.Net.IdentityReplayMoveRemove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkHexNet_IdentityReplayMoveRemove::RunTest(const FString&)
{
    using namespace ck_hex_net_membership;
    const auto S = MakeShared<FSession>();
    Begin(*this, S);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this,S](UWorld*)
    {
        if (!S->Server.Board.IsValid() || !S->Client.Board.IsValid()) { AddError(TEXT("Missing subjects")); return; }
        WatchClient(*S);
        auto P = ReadCommittedProjection(S->Server.Grid);
        auto Multi = One(S->Moved, {-1,0});
        FGameplayTagContainer Required;
        Required.AddTag(ck::tests::hex_persistence::AuthoredLocal);
        Multi.Set_FootprintOffsets({{0,0},{1,0}}).Set_Cells({{-1,0},{0,0}}).Set_RequiredCellTags(Required);
        auto Named = One(S->Named, {1,0});
        Named.Set_OccupantState(ECk_HexGridOccupantState::Bound).Set_Occupant(S->Server.Object);
        P._Placements = {Multi, One(S->Removed, {0,-1}), Named};
        P._ManualBlocks.Add({1,0});
        P._LocalTags.Add(FCk_HexCoord{}, FGameplayTagContainer{});
        P._Blockers.Add({S->Blocker, {}, {{1,0}}, true});
        TestTrue(TEXT("Authority published exact geometry and rules"), TryCommitProjection(S->Server.Grid, P).Get_Succeeded());
    })));
    Await(*this, FCk_NetAutoTest_Assertion::CreateLambda([S]()
    { return CheckStage(*S, S->Server, false, false) && CheckStage(*S, S->Client, false, false); }),
        TEXT("Both worlds agree on stable IDs, exact shapes, cell index, binding, votes and tags"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this,S](UWorld*)
    {
        if (!CheckStage(*S, S->Client, false, false)) { return; }
        S->BeforeReplay = ReadCommittedProjection(S->Client.Grid)._Revision;
        S->ClientPlacement = Occupancy::TryGet_Placement(S->Client.Grid, S->Moved).Get_Placement().Get_Handle();
        const auto* Published = UCk_Utils_Net_UE::TryGetContainerFragmentData<FCk_RepData_HexGridState>(S->Server.Grid);
        if (!TestTrue(TEXT("Authority driver holds all three published records"), Published && Published->Get_Placements().Num() == 3))
        { return; }
        auto Replay = *Published;
        // Change only wire order. Iris can coalesce byte-identical properties, and placement identity is keyed by GUID.
        Replay.Get_Placements().Swap(0, 1);
        S->FirstReplayId = Replay.Get_Placements()[0].Get_PlacementId();
        TestTrue(TEXT("Replay sent over the authority driver"),
            UCk_Utils_Net_UE::TryUpdateContainerFragment<FCk_RepData_HexGridState>(S->Server.Grid, Replay));
    })));
    Await(*this, FCk_NetAutoTest_Assertion::CreateLambda([S]()
    {
        if (!S->FirstReplayId.IsValid() || !S->Client.Grid.Has<ck::FFragment_IncomingReplicationDriver>()) { return false; }
        const auto Driver = S->Client.Grid.Get<ck::FFragment_IncomingReplicationDriver>()._Driver;
        auto* Entry = Driver.IsValid() ? Driver->FindEntry(FCk_RepData_HexGridState::StaticStruct()) : nullptr;
        const auto* Applied = Entry ? Entry->_LastAppliedData.GetPtr<FCk_RepData_HexGridState>() : nullptr;
        return Applied && !Entry->_PendingApply && Applied->Get_Placements().Num() == 3 &&
            Applied->Get_Placements()[0].Get_PlacementId() == S->FirstReplayId &&
            ReadCommittedProjection(S->Client.Grid)._Revision == S->BeforeReplay &&
            Occupancy::TryGet_Placement(S->Client.Grid, S->Moved).Get_Placement().Get_Handle() == S->ClientPlacement &&
            CheckStage(*S, S->Client, false, false);
    }), TEXT("Client actually received and accepted reordered baseline with unchanged projection and placement handle"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this,S](UWorld*)
    {
        if (HasAnyErrors()) { return; }
        auto P = ReadCommittedProjection(S->Server.Grid);
        auto* Move = P._Placements.FindByPredicate([&](const auto& V){ return V.Get_PlacementId() == S->Moved; });
        if (!TestNotNull(TEXT("Authority placement to move"), Move)) { return; }
        Move->Set_Anchor({0,1}).Set_Rotation(ECk_HexRotation::Turn3).Set_Cells({{-1,1},{0,1}});
        P._Placements.RemoveAll([&](const auto& V){ return V.Get_PlacementId() == S->Removed; });
        P._Blockers.RemoveAll([&](const auto& B){ return B._BlockerId == S->Blocker; });
        P._ManualBlocks.Reset();
        P._LocalTags.Reset();
        TestTrue(TEXT("Authority replaced occupied cells and removed obsolete contributor"), TryCommitProjection(S->Server.Grid, P).Get_Succeeded());
    })));
    Await(*this, FCk_NetAutoTest_Assertion::CreateLambda([S]()
    {
        return CheckStage(*S, S->Server, true, false) && CheckStage(*S, S->Client, true, false) &&
            Occupancy::TryGet_Placement(S->Client.Grid, S->Moved).Get_Placement().Get_Handle() == S->ClientPlacement &&
            !Occupancy::TryGet_PlacementAt(S->Client.Grid, {-1,0}).Get_Succeeded();
    }), TEXT("Same placement moved, prior cells and removed ID released"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([S](UWorld*)
    { if (ck::IsValid(S->Server.Object)) { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(S->Server.Object); } })));
    Await(*this, FCk_NetAutoTest_Assertion::CreateLambda([S]()
    { return CheckStage(*S, S->Server, true, true) && CheckStage(*S, S->Client, true, true) && S->ClientPlacementEvents == 0; }),
        TEXT("External authority death releases reservations on both worlds without client gameplay callbacks"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexNet_MixedTopologyMembership, "Ck.HexGrid.Net.MixedTopologyMembership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkHexNet_MixedTopologyMembership::RunTest(const FString&)
{
    using namespace ck_hex_net_membership;
    const auto S = MakeShared<FSession>();
    Begin(*this, S);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this,S](UWorld* World)
    {
        if (!S->Server.Board.IsValid()) { return; }
        auto* Piece = Find<ObjectActor>(World);
        TestTrue(TEXT("Square placement accepts the object"), S->Server.Board->Request_PlaceSquare(Piece, {2,3}));
        TestTrue(TEXT("Public hex placement accepts same object"), S->Server.Board->Request_PlaceHex(Piece, {1,0}, ECk_HexRotation::Turn2));
        World->SpawnActor<BoardActor>();
    })));
    Await(*this, FCk_NetAutoTest_Assertion::CreateLambda([S]()
    {
        S->Named = Occupancy::TryGet_PlacementForOccupant(S->Server.Object).Get_PlacementId();
        auto* ServerOther = Find<BoardActor>(Get_ServerWorld(), S->Server.Board.Get());
        auto* ClientOther = Find<BoardActor>(Get_ClientWorld(0), S->Client.Board.Get());
        return ServerOther && ClientOther && UCk_Utils_HexGrid_UE::Get_IsReady(ServerOther->Get_HexGrid()) &&
            UCk_Utils_HexGrid_UE::Get_IsReady(ClientOther->Get_HexGrid()) && MixedMember(S->Server, S->Named) && MixedMember(S->Client, S->Named);
    }), TEXT("Correlated object owns square and hex placements on each world"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this,S](UWorld* World)
    {
        auto* Other = Find<BoardActor>(World, S->Server.Board.Get());
        if (!Other || !MixedMember(S->Server, S->Named)) { AddError(TEXT("Mixed membership not established")); return; }
        auto Grid = Other->Get_HexGrid();
        const auto Before = ReadCommittedProjection(Grid);
        auto P = Before;
        auto Duplicate = One(FGuid::NewGuid(), {});
        Duplicate.Set_Occupant(S->Server.Object).Set_OccupantState(ECk_HexGridOccupantState::Bound);
        P._Placements.Add(Duplicate);
        const auto Rejected = TryCommitProjection(Grid, P);
        TestTrue(TEXT("Second authority board rejects duplicate object without disturbing either membership"),
            !Rejected.Get_Succeeded() && Rejected.Get_Failure() == ECk_HexGridFailure::OccupantAlreadyPlaced &&
            ReadCommittedProjection(Grid)._Revision == Before._Revision && ReadCommittedProjection(Grid)._Placements.IsEmpty() &&
            MixedMember(S->Server, S->Named));
        auto* ClientOther = Find<BoardActor>(Get_ClientWorld(0), S->Client.Board.Get());
        if (!ClientOther) { AddError(TEXT("Missing correlated second board")); return; }
        auto ClientGrid = ClientOther->Get_HexGrid();
        auto Bad = ck::hex_grid::persistence::ReadCommittedProjection(ClientGrid);
        auto Source = ck::hex_grid::persistence::ReadCommittedProjection(S->Client.Grid);
        if (Source.Get_Placements().Num() != 1) { AddError(TEXT("Missing client-side incumbent")); return; }
        Bad.Set_Placements(Source.Get_Placements());
        const auto ClientBefore = ReadCommittedProjection(ClientGrid);
        const auto Result = ck::hex_grid::persistence::TryReplaceCommittedProjection(ClientGrid, Bad, ECk_HexGridApplySource::Replication);
        TestTrue(TEXT("Second client board rejects cross-board claim atomically"), Result.Get_Status() == ECk_Persistence_ApplyResult::Rejected &&
            Result.Get_Failure() == ECk_HexGridFailure::OccupantAlreadyPlaced &&
            ReadCommittedProjection(ClientGrid)._Revision == ClientBefore._Revision &&
            ReadCommittedProjection(ClientGrid)._Placements.IsEmpty() && MixedMember(S->Client, S->Named));
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

namespace ck_hex_net_membership
{
    struct FReplacementOnChildDeath { TFunction<void()> Run; };

    void ApplyReplacement(FCk_Handle Child)
    {
        Child.Get<FReplacementOnChildDeath, ck::IsValid_Policy_IncludePendingKill>().Run();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexNet_ReceiverEdges, "Ck.HexGrid.Net.ReceiverLifecycleEdges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkHexNet_ReceiverEdges::RunTest(const FString&)
{
    using namespace ck_hex_net_membership;
    const auto S = MakeShared<FSession>();
    Begin(*this, S);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this,S](UWorld* World)
    {
        if (!S->Server.Board.IsValid()) { AddError(TEXT("No authority board")); return; }
        TestTrue(TEXT("Authority submitted placement"), S->Server.Board->Request_PlaceHex(Find<ObjectActor>(World), {1,0}, ECk_HexRotation::Turn0));
    })));
    Await(*this, FCk_NetAutoTest_Assertion::CreateLambda([S]()
    {
        const auto Result = Occupancy::TryGet_PlacementForOccupant(S->Client.Object);
        if (!Result.Get_Succeeded()) { return false; }
        S->Named = Result.Get_PlacementId();
        S->ClientPlacement = Result.Get_Placement().Get_Handle();
        return S->Named.IsValid();
    }), TEXT("Real wire baseline bound old incumbent"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0, FCk_NetAutoTest_ServerAction::CreateLambda([this,S](UWorld*)
    {
        auto Board = FCk_Handle{S->Client.Grid};
        const auto* BoardHandler = FCk_PersistenceHandlerRegistry::Find(FCk_RepData_HexGridState::StaticStruct());
        const auto* ObjectHandler = FCk_PersistenceHandlerRegistry::Find(FCk_RepData_HexGridObjectIdentity::StaticStruct());
        if (!S->Named.IsValid() || !BoardHandler || !ObjectHandler) { AddError(TEXT("Missing accepted client fixtures")); return; }
        const auto Wire = FInstancedStruct::Make(ck::hex_grid::persistence::ReadCommittedProjection(S->Client.Grid));
        TestTrue(TEXT("Disk hydration rejects a real incoming board"), BoardHandler->HydrationApply(Board, Wire, {}) == ECk_Persistence_ApplyResult::Rejected);
        FCk_Handle_HexGridObject Replacement;
        ECk_HexGridObject_Failure Failure{};
        if (!TestTrue(TEXT("Separate live client driver composes a replacement without a provisional key"),
            UCk_Utils_HexGridObject_UE::TryAdd(Board, {}, Replacement, Failure) &&
            !ck::hex_grid::GetAcceptedObjectBindingId(Replacement).IsValid())) { return; }
        auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S->Client.Object);
        Child.Add<FReplacementOnChildDeath>().Run = [this,S,Replacement,ObjectHandler]() mutable
        {
            const auto Prior = Occupancy::TryGet_Placement(S->Client.Grid, S->Named);
            TestTrue(TEXT("Old occupant is still bound between dependent and owner BeginDestroy callbacks"),
                Prior.Get_Succeeded() && Prior.Get_Placement().Get_Occupant() == S->Client.Object);
            FCk_Handle Receiver = Replacement;
            const auto Identity = FInstancedStruct::Make(FCk_RepData_HexGridObjectIdentity{}.Set_BindingId(S->Key));
            TestTrue(TEXT("Replacement receives same key before old entity deregisters"),
                ObjectHandler->NetApply(Receiver, Identity, {}) == ECk_Persistence_ApplyResult::Applied);
        };
        (void)ck::UUtils_Signal_OnEntityBeginDestroy::Bind<&ApplyReplacement, ECk_Signal_BindingPolicy::IgnorePayloadInFlight,
            ECk_Signal_PostFireBehavior::DoNothing>(Child);
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(S->Client.Object);
        const auto Bound = Occupancy::TryGet_PlacementForOccupant(Replacement);
        TestTrue(TEXT("Old destruction leaves replacement bound to unchanged placement ID and handle"),
            Bound.Get_Succeeded() && Bound.Get_PlacementId() == S->Named && Bound.Get_Placement().Get_Handle() == S->ClientPlacement);
        FCk_Handle_HexCell Cell;
        ECk_HexBoardFailure CellFailure{};
        if (!UCk_Utils_HexGrid_UE::TryGet_CellAt(S->Client.Grid, {1,0}, Cell, CellFailure)) { AddError(TEXT("Missing occupied cell")); return; }
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Cell);
        BoardHandler->NetRemove(Board);
        const auto Cleared = ReadCommittedProjection(S->Client.Grid);
        TestTrue(TEXT("Terminal removal releases every claim despite retiring cell and rejects stale replay"),
            UCk_Utils_HexGrid_UE::Get_Readiness(S->Client.Grid).Get_Failure() == ECk_HexGridFailure::Destroyed &&
            Cleared._Placements.IsEmpty() && Cleared._Blockers.IsEmpty() &&
            !Occupancy::TryGet_PlacementForOccupant(Replacement).Get_Succeeded() &&
            BoardHandler->NetApply(Board, Wire, {}) == ECk_Persistence_ApplyResult::Rejected);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
