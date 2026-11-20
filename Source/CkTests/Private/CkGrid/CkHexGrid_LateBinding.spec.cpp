#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkHexGridNetConflictWitness.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Fragment.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGrid_Projection.h"
#include "CkGrid/HexGridSystem/Persistence/CkHexGridPersistence_Utils.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"
#include "CkTests/Net/CkAutoTest_NetSubject_MixedGrid.h"
#include "CkTests/Net/CkAutoTest_NetSubject_Refill.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "EngineUtils.h"

namespace ck::hex_net_late_binding
{
    using BoardActor = ACk_AutoTest_NetSubject_MixedGrid_UE;
    using Subject = ACk_AutoTest_NetSubject;
    using MismatchedSubject = ACk_AutoTest_NetSubject_Refill;
    using Occupancy = UCk_Utils_HexGridOccupancy_UE;
    using State = ECk_HexGridOccupantState;
    using namespace auto_test::net;
    using namespace hex_grid;

    template<class T>
    auto FindExact(UWorld* World, const T* Skip = nullptr) -> T*
    {
        if (!World) { return nullptr; }
        for (TActorIterator<T> It(World); It; ++It)
        { if (It->GetClass() == T::StaticClass() && *It != Skip) { return *It; } }
        return nullptr;
    }

    struct FSide
    {
        TWeakObjectPtr<BoardActor> Actor;
        FCk_Handle_HexGrid Board;
        FCk_Handle Piece;
        FCk_Handle Wrong;
        FCk_Handle_HexGrid Other;

        bool Resolve(UWorld* World)
        {
            Actor = FindExact<BoardActor>(World);
            if (!Actor.IsValid()) { return false; }
            Board = Actor->Get_HexGrid();
            auto* First = FindExact<Subject>(World);
            auto* Second = FindExact<MismatchedSubject>(World);
            if (!First || !Second) { return false; }
            Piece = UCk_Utils_EntityBridge_UE::TryGet_Entity(First);
            Wrong = UCk_Utils_EntityBridge_UE::TryGet_Entity(Second);
            return UCk_Utils_HexGrid_UE::Get_IsReady(Board) && ck::IsValid(Piece) && ck::IsValid(Wrong) &&
                Piece != Wrong && Piece != Board && Wrong != Board;
        }

        bool ResolveOther(UWorld* World)
        {
            auto* OtherActor = FindExact<BoardActor>(World, Actor.Get());
            if (!OtherActor) { return false; }
            Other = OtherActor->Get_HexGrid();
            return UCk_Utils_HexGrid_UE::Get_IsReady(Other);
        }
    };

    struct FSession
    {
        FSide Host, Client;
        FGuid FirstId = FGuid::NewGuid(), PrivateId = FGuid::NewGuid(), WrongId = FGuid::NewGuid(), AnonymousId = FGuid::NewGuid();
        FGuid ExpectedWrong = FGuid::NewGuid(), FirstKey, PrivateKey;
        FCk_Handle_HexGridPlacement ClientFirst, ClientWrong;
        FCk_Handle_HexGridObject ClientPiece, ClientMismatch;
        uint64 OtherRevision = 0;
        TStrongObjectPtr<UCk_HexGridNetConflictWitness> Witness;
    };

    FCk_Handle_HexGridObject Compose(FCk_Handle Handle)
    {
        FCk_Handle_HexGridObject Result;
        ECk_HexGridObject_Failure Failure{};
        if (ck::IsValid(Handle)) { UCk_Utils_HexGridObject_UE::TryAdd(Handle, {}, Result, Failure); }
        return Result;
    }

    auto Row(FGuid Id, FCk_HexCoord Cell) -> FCk_HexGridPlacement_View
    {
        FCk_HexGridPlacement_View View;
        return View.Set_PlacementId(Id).Set_Anchor(Cell).Set_FootprintOffsets({FCk_HexCoord{}}).Set_Cells({Cell});
    }

    bool Check(const FSession& S, bool Resolved, bool ExpectHint = true)
    {
        const auto Board = S.Client.Board;
        if (!UCk_Utils_HexGrid_UE::Get_IsReady(Board) || Occupancy::Get_Placements(Board).Get_Placements().Num() != 4) { return false; }
        const auto At = [&](FCk_HexCoord Cell) { return Occupancy::TryGet_PlacementAt(Board, Cell).Get_Placement(); };
        const auto A = At({-1,0}), B = At({1,0}), C = At({0,1}), D = At({0,-1});
        if (A.Get_PlacementId() != S.FirstId || A.Get_OccupantBindingId() != S.FirstKey ||
            B.Get_PlacementId() != S.PrivateId || B.Get_OccupantBindingId() != S.PrivateKey ||
            B.Get_OccupantState() != State::AwaitingBinding || ck::IsValid(B.Get_Occupant()) || ck::IsValid(B.Get_ExpectedOccupant()) ||
            C.Get_PlacementId() != S.WrongId || C.Get_OccupantBindingId() != S.ExpectedWrong || ck::IsValid(C.Get_Occupant()) ||
            D.Get_PlacementId() != S.AnonymousId || D.Get_OccupantState() != State::Anonymous || D.Get_OccupantBindingId().IsValid())
        { return false; }
        if (!Resolved)
        {
            return A.Get_OccupantState() == State::AwaitingBinding && !ck::IsValid(A.Get_Occupant()) &&
                C.Get_OccupantState() == State::AwaitingBinding &&
                C.Get_ExpectedOccupant() == (ExpectHint ? S.Client.Wrong : FCk_Handle{});
        }
        return A.Get_OccupantState() == State::Bound && A.Get_Occupant() == S.Client.Piece && A.Get_Handle() == S.ClientFirst &&
            C.Get_OccupantState() == State::BindingConflict && C.Get_Handle() == S.ClientWrong &&
            GetAcceptedObjectBindingId(S.Client.Piece) == S.FirstKey &&
            GetAcceptedObjectBindingId(S.Client.Wrong).IsValid() && GetAcceptedObjectBindingId(S.Client.Wrong) != S.ExpectedWrong &&
            Occupancy::TryGet_PlacementForOccupant(S.ClientPiece).Get_PlacementId() == S.FirstId &&
            !Occupancy::TryGet_PlacementForOccupant(S.ClientMismatch).Get_Succeeded() &&
            S.Witness->Count == 1 && S.Witness->PlacedCount == 0 && S.Witness->PlacementId == S.WrongId && S.Witness->Board == Board &&
            S.Witness->Failure == ECk_HexGridFailure::LateOccupantConflict;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexNetLateBinding, "Ck.HexGrid.Net.LateBindingAndConflicts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkHexNetLateBinding::RunTest(const FString&)
{
    using namespace ck::hex_net_late_binding;
    const auto S = MakeShared<FSession>();
    auto Wait = [this](const TCHAR* Message, auto Condition)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
            [this, Condition]() { return HasAnyErrors() || Condition(); }), 20.f, Message));
    };
    auto Act = [this](auto Action)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, Action](UWorld* World) { if (!HasAnyErrors()) { Action(World); } })));
    };
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.f));
    Act([](UWorld* World)
    {
        World->SpawnActor<BoardActor>();
        World->SpawnActor<Subject>();
        World->SpawnActor<MismatchedSubject>();
    });
    Wait(TEXT("Independent board and both actor entities are linked to their actual network drivers"), [S]()
    {
        return S->Host.Resolve(Get_ServerWorld()) && S->Client.Resolve(Get_ClientWorld(0)) &&
            UCk_Utils_EntityReplicationDriver_UE::Get_IsIncomingReplica(S->Client.Board) &&
            UCk_Utils_EntityReplicationDriver_UE::Get_IsIncomingReplica(S->Client.Piece) &&
            UCk_Utils_EntityReplicationDriver_UE::Get_IsIncomingReplica(S->Client.Wrong);
    });
    Act([this, S](UWorld* World)
    {
        S->Witness.Reset(NewObject<UCk_HexGridNetConflictWitness>());
        FCk_Delegate_HexGrid_OccupantBindingFailed Failed;
        Failed.BindDynamic(S->Witness.Get(), &UCk_HexGridNetConflictWitness::OnBindingFailed);
        UCk_Utils_HexGridPlacement_UE::BindTo_OnOccupantBindingFailed(S->Client.Board, Failed);
        FCk_Delegate_HexGridPlacement_ObjectPlaced Placed;
        Placed.BindDynamic(S->Witness.Get(), &UCk_HexGridNetConflictWitness::OnPlaced);
        UCk_Utils_HexGridPlacement_UE::BindTo_OnObjectPlaced(S->Client.Board, Placed);
        const auto Object = Compose(S->Host.Piece);
        auto Local = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
        UCk_Utils_Net_UE::Add(Local, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
            ECk_Net_NetModeType::Host, ECk_Net_EntityNetRole::Authority});
        const auto Private = Compose(Local);
        S->FirstKey = GetAcceptedObjectBindingId(Object);
        S->PrivateKey = GetAcceptedObjectBindingId(Private);
        if (!TestTrue(TEXT("Authority issued distinct object keys"), S->FirstKey.IsValid() && S->PrivateKey.IsValid() &&
            S->FirstKey != S->PrivateKey)) { return; }
        auto P = ReadCommittedProjection(S->Host.Board);
        P._Placements = {Row(S->FirstId, {-1,0}).Set_Occupant(Object).Set_OccupantState(State::Bound),
            Row(S->PrivateId, {1,0}).Set_Occupant(Private).Set_OccupantState(State::Bound),
            Row(S->WrongId, {0,1}).Set_OccupantBindingId(S->ExpectedWrong)
                .Set_OccupantState(State::AwaitingBinding), Row(S->AnonymousId, {0,-1})};
        TestTrue(TEXT("Authority admitted all four reservations"), TryCommitProjection(S->Host.Board, P).Get_Succeeded());
    });
    Wait(TEXT("Board-first reservations and geometry arrive before the client object features"),
        [S]() { return Check(*S, false, false); });
    Act([this,S](UWorld*)
    {
        // Regular publication is key-only. Introduce an explicit conflicting hint on this wire row to challenge
        // the receiver's compatibility path before the independently correlated object has an accepted key.
        const auto* Current = UCk_Utils_Net_UE::TryGetContainerFragmentData<FCk_RepData_HexGridState>(S->Host.Board);
        if (!TestNotNull(TEXT("Authority board has a wire baseline"), Current)) { return; }
        auto WithHint = *Current;
        auto* Wrong = WithHint.Get_Placements().FindByPredicate([&](const auto& Row) { return Row.Get_PlacementId() == S->WrongId; });
        if (!TestNotNull(TEXT("Conflict challenge row exists"), Wrong)) { return; }
        Wrong->Set_Occupant(S->Host.Wrong);
        TestTrue(TEXT("Conflicting hint sent over actual board driver"),
            UCk_Utils_Net_UE::TryUpdateContainerFragment<FCk_RepData_HexGridState>(S->Host.Board, WithHint));
    });
    Wait(TEXT("Client associated the new hint with an existing unresolved reservation"), [S]() { return Check(*S, false); });
    Act([this, S](UWorld* World)
    {
        S->ClientFirst = Occupancy::TryGet_Placement(S->Client.Board, S->FirstId).Get_Placement().Get_Handle();
        S->ClientWrong = Occupancy::TryGet_Placement(S->Client.Board, S->WrongId).Get_Placement().Get_Handle();
        TestTrue(TEXT("No conflict before independent object identity arrived"), S->Witness->Count == 0);
        S->ClientPiece = Compose(S->Client.Piece);
        S->ClientMismatch = Compose(S->Client.Wrong);
        TestTrue(TEXT("Client accepts object recipes on both already linked drivers"), ck::IsValid(S->ClientPiece) &&
            ck::IsValid(S->ClientMismatch));
        const auto Wrong = Compose(S->Host.Wrong);
        const auto Key = GetAcceptedObjectBindingId(Wrong);
        TestTrue(TEXT("Independent authority object acquired a different valid key"), Key.IsValid() && Key != S->ExpectedWrong);
        World->SpawnActor<BoardActor>();
    });
    Wait(TEXT("Late matching object binds, mismatch notifies exactly once and both reserved cells retain their placement handles"),
        [S]() { return Check(*S, true) && S->Host.ResolveOther(Get_ServerWorld()) && S->Client.ResolveOther(Get_ClientWorld(0)); });
    Act([this, S](UWorld*)
    {
        // Deliver a candidate containing both an admissible anonymous row and a key already reserved on the first board.
        // Use the real fast array so receipt and dispatch are observable independently of the authority index.
        auto Payload = ck::hex_grid::persistence::ReadCommittedProjection(S->Host.Other);
        auto& Rows = Payload.Get_Placements();
        Rows.AddDefaulted_GetRef().Set_PlacementId(FGuid::NewGuid()).Set_Anchor({}).Set_FootprintOffsets({FCk_HexCoord{}}).Set_Cells({FCk_HexCoord{}});
        Rows.AddDefaulted_GetRef().Set_PlacementId(FGuid::NewGuid()).Set_Anchor({1,0}).Set_FootprintOffsets({FCk_HexCoord{}})
            .Set_Cells({{1,0}}).Set_HasOccupantReference(true).Set_OccupantBindingId(S->PrivateKey);
        S->OtherRevision = ReadCommittedProjection(S->Client.Other)._Revision;
        TestTrue(TEXT("Cross-board duplicate sent through separate authority board driver"),
            UCk_Utils_Net_UE::TryUpdateContainerFragment<FCk_RepData_HexGridState>(S->Host.Other, Payload));
    });
    Wait(TEXT("Received duplicate rejects the whole second board update, preserving both the original reservations and single notification"),
        [S]()
        {
            if (!UCk_Utils_EntityReplicationDriver_UE::Get_IsIncomingReplica(S->Client.Other)) { return false; }
            const auto Driver = S->Client.Other.Get<ck::FFragment_IncomingReplicationDriver>()._Driver;
            const auto* Entry = Driver->FindEntry(FCk_RepData_HexGridState::StaticStruct());
            if (!Entry || Entry->_PendingApply || !Entry->_WasEverApplied) { return false; }
            const auto* Received = Entry->Data.GetPtr<FCk_RepData_HexGridState>();
            const auto* Accepted = Entry->_LastAppliedData.GetPtr<FCk_RepData_HexGridState>();
            const auto Other = ReadCommittedProjection(S->Client.Other);
            return Received && Received->Get_Placements().Num() == 2 && Accepted && Accepted->Get_Placements().IsEmpty() &&
                Other._Revision == S->OtherRevision && Other._Placements.IsEmpty() && Check(*S, true);
        });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
