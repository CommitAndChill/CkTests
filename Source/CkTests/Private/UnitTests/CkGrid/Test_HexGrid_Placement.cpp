#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkHexPlacementTestListener.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Fragment.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGrid_Projection.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkGrid/2dGridSystem/Grid/Ck2dGridSystem_Utils.h"
#include "CkGrid/2dGridSystem/Object/Ck2dGridObject_Utils.h"
#include "CkGrid/2dGridSystem/Occupancy/Ck2dGridOccupancy_Utils.h"
#include "CkGrid/2dGridSystem/Placement/Ck2dGridPlacement_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_hex_placement_test
{
    using Place = UCk_Utils_HexGridPlacement_UE;
    using Occupancy = UCk_Utils_HexGridOccupancy_UE;
    using Grid = UCk_Utils_HexGrid_UE;
    using Cell = UCk_Utils_HexCell_UE;
    using Object = UCk_Utils_HexGridObject_UE;
    using Life = UCk_Utils_EntityLifetime_UE;
    using Failure = ECk_HexGridFailure;
    using Outcome = ECk_Request_OperationResult;
    using Readiness = ECk_HexGridReadiness;
    using Rotation = ECk_HexRotation;
    using Connectivity = ECk_HexGridConnectivity;
    using View = FCk_HexGridPlacement_View;
    using Result = FCk_HexGridPlacement_Result;
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Hazard, "Ck.Test.HexPlacement.Hazard");

    FCk_HexGridPlacement_Spec Candidate(FCk_Handle_HexGridObject ObjectHandle, FCk_HexCoord Anchor = {},
        Rotation Turn = Rotation::Turn0, Connectivity Policy = Connectivity::Ignore)
    {
        FCk_HexGridPlacement_Spec Spec;
        Spec.Set_Object(ObjectHandle);
        Spec.Set_Anchor(Anchor);
        Spec.Set_Rotation(Turn);
        Spec.Set_Connectivity(Policy);
        return Spec;
    }

    struct FCase
    {
        FAutomationTestBase& Test;
        FCk_Handle Parent;
        FCk_Handle_HexGrid Board, Other;
        FCk_Handle_HexGridObject A, B, Pair;
        FCk_Handle_HexCell Center;
        FCk_Handle_2dGridSystem Square;
        FCk_Handle_2dGridPlacement SquarePlacement;
        FCk_Handle_HexGridPlacement LastPlacement;
        FGuid Id;
        uint64 Revision = 0;
        TArray<Result> Typed;
        TArray<Outcome> Generic;
        TArray<FName> Events;
        TArray<View> Signals;
        TArray<FCk_HexGridReadiness_Result> ReadinessChanges;
        TStrongObjectPtr<UCk_HexPlacementTestListener> Listener;
        FCk_Delegate_HexGridPlacement_OnResult OnResult;
        FCk_Delegate_Request_OnCompleted OnCompleted;
        TFunction<void(FName, const View&)> DuringSignal;
        TFunction<void(const Result&)> DuringResult;
        TFunction<void(FCk_Handle)> DuringCompletion;

        explicit FCase(FAutomationTestBase& InTest) : Test(InTest) {}
        ~FCase()
        {
            if (Listener.IsValid())
            {
                Listener->Result = nullptr;
                Listener->Completed = nullptr;
                Listener->Changed = nullptr;
                Listener->Readiness = nullptr;
            }
        }

        FCk_Handle_HexGrid MakeBoard()
        {
            FCk_HexDomain Domain;
            Domain.Set_RingRadius(3);
            FCk_HexGrid_Spec Spec;
            Spec.Set_Domain(Domain);
            FCk_Handle_HexGrid Made;
            ECk_HexBoardFailure Reason{};
            Test.TestTrue(TEXT("Real authority board composed"), Grid::TryCreate(Parent, FTransform::Identity, Spec, Made, Reason));
            return Made;
        }

        FCk_Handle_HexGridObject Shape(TArray<FCk_HexCoord> Offsets, bool AvoidHazard = false)
        {
            FCk_HexGridObject_Spec Spec;
            Spec.Set_FootprintOffsets(Offsets);
            if (AvoidHazard) { Spec.Set_ForbiddenCellTags(FGameplayTagContainer{Hazard}); }
            FCk_Handle_HexGridObject Made;
            ECk_HexGridObject_Failure Reason{};
            Test.TestTrue(TEXT("Real footprint composed"), Object::TryCreate(Parent, Spec, Made, Reason));
            return Made;
        }

        FCk_Handle_HexCell CellAt(FCk_HexCoord Coord)
        {
            FCk_Handle_HexCell Found;
            ECk_HexBoardFailure Reason{};
            Test.TestTrue(TEXT("Coordinate materialized"), Grid::TryGet_CellAt(Board, Coord, Found, Reason));
            return Found;
        }

        void Init(UWorld* World)
        {
            Parent = Life::Request_CreateEntity_TransientOwner(World);
            UCk_Utils_Net_UE::Add(Parent, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
                ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
            Board = MakeBoard();
            Other = MakeBoard();
            A = Shape({{}});
            B = Shape({{}});
            Pair = Shape({{}, {1,0}}, true);
            Center = CellAt({});
            Listener.Reset(NewObject<UCk_HexPlacementTestListener>());
            Listener->Result = [this](const Result& Value)
            {
                Events.Add(TEXT("Typed"));
                Typed.Add(Value);
                if (DuringResult) { DuringResult(Value); }
            };
            Listener->Completed = [this](FCk_Handle Target, Outcome Value)
            {
                Events.Add(TEXT("Generic"));
                Generic.Add(Value);
                if (DuringCompletion) { DuringCompletion(Target); }
            };
            Listener->Readiness = [this](FCk_Handle_HexGrid Subject, const FCk_HexGridReadiness_Result& Value)
            {
                Test.TestTrue(TEXT("Readiness notification observes transition"), Grid::Get_Readiness(Subject).Get_State() == Value.Get_State());
                ReadinessChanges.Add(Value);
            };
            Listener->Changed = [this](FName Event, FCk_Handle_HexGrid Subject, const View& Value)
            {
                Events.Add(Event);
                Signals.Add(Value);
                const bool Removed = Event == TEXT("Removed");
                const auto Found = Occupancy::TryGet_Placement(Subject, Value.Get_PlacementId());
                Test.TestTrue(TEXT("ID query observes complete signal state"), Found.Get_Succeeded() != Removed);
                if (!Removed)
                {
                    Test.TestTrue(TEXT("Live handle and backing ID visible during signal"),
                        ck::IsValid(Value.Get_Handle()) && Found.Get_Placement().Get_Handle() == Value.Get_Handle());
                    const auto Handle = Object::Cast(Value.Get_Occupant());
                    if (ck::IsValid(Handle))
                    {
                        Test.TestTrue(TEXT("Occupant backref visible during signal"),
                            Occupancy::TryGet_PlacementForOccupant(Handle).Get_PlacementId() == Value.Get_PlacementId());
                    }
                }
                for (const auto Coord : Value.Get_Cells())
                {
                    Test.TestTrue(TEXT("All cell owners agree during signal"),
                        (Occupancy::TryGet_PlacementAt(Subject, Coord).Get_PlacementId() == Value.Get_PlacementId()) != Removed);
                    FCk_Handle_HexCell CellHandle;
                    ECk_HexBoardFailure Reason{};
                    if (Grid::TryGet_CellAt(Subject, Coord, CellHandle, Reason))
                    {
                        const bool HasStamp = CellHandle.Has<ck::FFragment_HexCell_Placement>() &&
                            CellHandle.Get<ck::FFragment_HexCell_Placement>()._PlacementId == Value.Get_PlacementId();
                        Test.TestTrue(TEXT("Physical cell stamp agrees with durable index during signal"), HasStamp != Removed);
                    }
                }
                if (DuringSignal) { DuringSignal(Event, Value); }
            };
            OnResult.BindDynamic(Listener.Get(), &UCk_HexPlacementTestListener::OnResult);
            OnCompleted.BindDynamic(Listener.Get(), &UCk_HexPlacementTestListener::OnCompleted);
            FCk_Delegate_HexGridPlacement_ObjectPlaced Placed;
            Placed.BindDynamic(Listener.Get(), &UCk_HexPlacementTestListener::OnPlaced);
            FCk_Delegate_HexGridPlacement_ObjectMoved Moved;
            Moved.BindDynamic(Listener.Get(), &UCk_HexPlacementTestListener::OnMoved);
            FCk_Delegate_HexGridPlacement_ObjectRemoved Removed;
            Removed.BindDynamic(Listener.Get(), &UCk_HexPlacementTestListener::OnRemoved);
            Place::BindTo_OnObjectPlaced(Board, Placed);
            Place::BindTo_OnObjectMoved(Board, Moved);
            Place::BindTo_OnObjectRemoved(Board, Removed);
        }

        void Submit(const FCk_HexGridPlacement_Spec& Spec, FCk_Handle_HexGrid& Target)
        {
            FCk_Request_HexGridPlacement_Place Request{Spec};
            Request.Set_OnResult(OnResult);
            Place::Request_Place(Target, Request, OnCompleted);
        }
        void Put(FCk_Handle_HexGridObject ObjectHandle, FCk_HexCoord Anchor = {})
        { Submit(Candidate(ObjectHandle, Anchor), Board); }

        void Move(FGuid PlacementId, FCk_HexCoord Anchor, Rotation Turn = Rotation::Turn0)
        {
            FCk_Request_HexGridPlacement_Move Request{PlacementId, Anchor, Turn};
            Request.Set_OnResult(OnResult);
            Place::Request_Move(Board, Request, OnCompleted);
        }
        void Remove(FGuid PlacementId)
        {
            FCk_Request_HexGridPlacement_Remove Request{PlacementId};
            Request.Set_OnResult(OnResult);
            Place::Request_Remove(Board, Request, OnCompleted);
        }
        void Outcomes(std::initializer_list<Failure> Expected)
        {
            if (!Test.TestEqual(TEXT("Typed completions exactly once"), Typed.Num(), static_cast<int32>(Expected.size()))) { return; }
            int32 Index = 0;
            for (const auto Reason : Expected)
            {
                Test.TestTrue(*FString::Printf(TEXT("Request %d expected %s, received %s"), Index,
                    *UEnum::GetValueAsString(Reason), *UEnum::GetValueAsString(Typed[Index].Get_Failure())),
                    Typed[Index].Get_Failure() == Reason && Typed[Index].Get_Succeeded() == (Reason == Failure::None));
                ++Index;
            }
        }
        uint64 CurrentRevision() const { return ck::hex_grid::ReadCommittedProjection(Board)._Revision; }
    };

    using State = TSharedRef<FCase>;

    State Begin(FAutomationTestBase& Test)
    {
        auto Case = MakeShared<FCase>(Test);
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [Case](UWorld* World) { Case->Init(World); })));
        return Case;
    }
    void Step(State Case, TFunction<void(FCase&)> Body)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [Case, Body=MoveTemp(Body)](UWorld*)
            { if (!Case->Test.HasAnyErrors()) { Body(Case.Get()); } })));
    }
    void Wait(State Case, TFunction<bool(const FCase&)> Predicate, FString Description)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&Case->Test,
            FCk_NetAutoTest_Condition::CreateLambda([Case, Predicate=MoveTemp(Predicate)]()
            { return Case->Test.HasAnyErrors() || Predicate(Case.Get()); }), 10.f, Description));
    }
    void WaitResults(State Case, int32 Count)
    {
        Wait(Case, [Count](const FCase& S) { return S.Typed.Num() >= Count; },
            FString::Printf(TEXT("Placement request %d completed"), Count));
    }
    void End() { ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE()); }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementPreview, "Ck.HexGrid.Placement.PreviewAndFailureReasons", ck_hex_placement_test::Flags)
bool FCkHexPlacementPreview::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S)
    {
        const auto Revision = S.CurrentRevision();
        const auto Good = Place::Get_CanPlace(S.Board, Candidate(S.Pair));
        S.Test.TestTrue(TEXT("Preview gives canonical cells with no durable or live handle"),
            Good.Get_Succeeded() && !Good.Get_PlacementId().IsValid() && ck::Is_NOT_Valid(Good.Get_Placement().Get_Handle())
            && Good.Get_Placement().Get_Cells() == TArray<FCk_HexCoord>{{0,0},{1,0}});
        const auto Check = [&S](const Result& R, Failure Expected, TArray<FCk_HexCoord> Cells = {})
        {
            S.Test.TestTrue(*FString::Printf(TEXT("Preview failure %s with exact cells"), *UEnum::GetValueAsString(Expected)),
                !R.Get_Succeeded() && R.Get_Failure() == Expected && R.Get_FailedCells() == Cells);
        };
        Check(Place::Get_CanPlace({}, Candidate(S.A)), Failure::InvalidGrid);
        Check(Place::Get_CanPlace(S.Board, {}), Failure::InvalidObject);
        Check(Place::Get_CanMove(S.Board, {}, {}, Rotation::Turn0), Failure::InvalidPlacementId);
        Check(Place::Get_CanPlace(S.Board, Candidate(S.A, {}, static_cast<Rotation>(255))), Failure::InvalidRotation);
        Check(Place::Get_CanPlace(S.Board, Candidate(S.A, {}, Rotation::Turn0, static_cast<Connectivity>(255))),
            Failure::InvalidFootprint);
        Check(Place::Get_CanPlace(S.Board, Candidate(S.Pair, {MAX_int32,0})), Failure::Overflow);
        Check(Place::Get_CanPlace(S.Board, Candidate(S.A, {4,0})), Failure::AnchorOutsideDomain, {{4,0}});
        Check(Place::Get_CanPlace(S.Board, Candidate(S.Pair, {3,0})), Failure::OutsideDomain, {{4,0}});
        S.Test.TestTrue(TEXT("Preview leaves ownership, revision and events unchanged"),
            Occupancy::Get_Placements(S.Board).Get_Placements().IsEmpty()
            && Revision == S.CurrentRevision() && S.Events.IsEmpty());
        auto Doomed = S.Shape({{}});
        S.Put(Doomed);
        Life::Request_DestroyEntity(Doomed);
        auto ProxyOwner = Life::Request_CreateEntity(Life::Get_TransientEntity(S.Parent));
        UCk_Utils_Net_UE::Add(ProxyOwner, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
            ECk_Net_NetModeType::Client, ECk_Net_EntityNetRole::Proxy});
        FCk_Handle_HexGrid ProxyBoard;
        ECk_HexBoardFailure BoardFailure{};
        if (!S.Test.TestTrue(TEXT("Fresh board inherits the proxy owner's role"),
            Grid::TryCreate(ProxyOwner, FTransform::Identity, {}, ProxyBoard, BoardFailure) &&
            !UCk_Utils_Net_UE::Get_HasAuthority(ProxyBoard))) { return; }
        S.Submit(Candidate(S.B), ProxyBoard);
        S.Test.TestTrue(TEXT("Proxy mutation rejected on submission"), S.Typed.Num() == 1 &&
            S.Typed[0].Get_Failure() == Failure::NotAuthority && S.Generic == TArray<Outcome>{Outcome::Failed_NotEnqueued});
    });
    WaitResults(Case, 2);
    Step(Case, [](FCase& S)
    {
        S.Outcomes({Failure::NotAuthority, Failure::InvalidObject});
        S.Test.TestTrue(TEXT("Queued stale object fails without signal or reservation"),
            S.Generic == TArray<Outcome>{Outcome::Failed_NotEnqueued, Outcome::Failed} &&
            S.Signals.IsEmpty() && Occupancy::Get_Placements(S.Board).Get_Placements().IsEmpty());
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementConflict, "Ck.HexGrid.Placement.SameDrainConflict", ck_hex_placement_test::Flags)
bool FCkHexPlacementConflict::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    for (bool Reverse : {false,true})
    {
        Step(Case, [Reverse](FCase& S)
        {
            S.Typed.Reset(); S.Generic.Reset(); S.Events.Reset();
            S.Put(Reverse ? S.B : S.A);
            S.Put(Reverse ? S.A : S.B);
        });
        WaitResults(Case, 2);
        Step(Case, [Reverse](FCase& S)
        {
            S.Outcomes({Failure::None,Failure::Occupied});
            const auto Owner = Occupancy::TryGet_PlacementAt(S.Board, {});
            S.Test.TestTrue(TEXT("First submitter owns the only placement"),
                Owner.Get_Placement().Get_Occupant() == (Reverse ? S.B : S.A) &&
                Occupancy::Get_Placements(S.Board).Get_Placements().Num() == 1 &&
                S.Typed[1].Get_FailedCells() == TArray<FCk_HexCoord>{{0,0}});
            S.Test.TestTrue(TEXT("Signals precede exactly one typed and generic terminal per request"),
                S.Events == TArray<FName>{TEXT("Placed"),TEXT("Typed"),TEXT("Generic"),TEXT("Typed"),TEXT("Generic")}
                && S.Generic == TArray<Outcome>{Outcome::Succeeded,Outcome::Failed});
            S.Remove(Owner.Get_PlacementId());
        });
        WaitResults(Case, 3);
    }
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementRuleOrder, "Ck.HexGrid.Placement.MixedRulesOrdering", ck_hex_placement_test::Flags)
bool FCkHexPlacementRuleOrder::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S)
    {
        S.DuringCompletion = [&S](FCk_Handle Owner)
        {
            if (Owner == S.Center)
            {
                bool Blocked = false;
                S.Test.TestTrue(TEXT("Each cell completion sees its own committed bit"),
                    Cell::TryGet_IsDisabled(S.Center, Blocked) && Blocked == S.Typed.IsEmpty());
                S.Test.TestTrue(TEXT("Rule callback precedes the following Place"),
                    !Occupancy::TryGet_PlacementAt(S.Board, {}).Get_Succeeded());
            }
        };
        Cell::Request_SetBlocked(S.Center, true, S.OnCompleted);
        S.Put(S.A);
        Cell::Request_SetBlocked(S.Center, false, S.OnCompleted);
        S.Put(S.B);
    });
    WaitResults(Case, 2);
    Step(Case, [](FCase& S)
    {
        S.Outcomes({Failure::Blocked,Failure::None});
        S.Test.TestTrue(TEXT("Mixed FIFO preserves four completions, winner and failed cell"),
            S.Typed[0].Get_FailedCells() == TArray<FCk_HexCoord>{{0,0}} &&
            S.Generic == TArray<Outcome>{Outcome::Succeeded,Outcome::Failed,Outcome::Succeeded,Outcome::Succeeded}
            && Occupancy::TryGet_PlacementAt(S.Board, {}).Get_Placement().Get_Occupant() == S.B);
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementReentry, "Ck.HexGrid.Placement.ReentrantSignal", ck_hex_placement_test::Flags)
bool FCkHexPlacementReentry::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S)
    {
        S.DuringSignal = [&S](FName Event, const View& V)
        {
            if (Event == TEXT("Placed") && V.Get_Occupant() == S.A)
            {
                S.Put(S.B, {1,0});
                S.Test.TestFalse(TEXT("Nested request cannot claim a cell inside signal"),
                    Occupancy::TryGet_PlacementAt(S.Board, {1,0}).Get_Succeeded());
            }
        };
        S.Put(S.A);
        S.Put(S.Pair, {1,0});
    });
    WaitResults(Case, 3);
    Step(Case, [](FCase& S)
    {
        S.Outcomes({Failure::None,Failure::None,Failure::Occupied});
        S.Test.TestTrue(TEXT("Nested request follows the complete detached batch"),
            S.Signals.Num() == 2 && S.Signals[0].Get_Occupant() == S.A && S.Signals[1].Get_Occupant() == S.Pair
            && Occupancy::TryGet_PlacementAt(S.Board, {1,0}).Get_Placement().Get_Occupant() == S.Pair);
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementReplace, "Ck.HexGrid.Placement.RemoveThenReplace", ck_hex_placement_test::Flags)
bool FCkHexPlacementReplace::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S) { S.Put(S.A); });
    WaitResults(Case, 1);
    Step(Case, [](FCase& S)
    {
        S.Id = S.Typed[0].Get_PlacementId();
        S.Remove(S.Id);
        S.Put(S.B);
    });
    WaitResults(Case, 3);
    Step(Case, [](FCase& S)
    {
        const auto NewId = S.Typed[2].Get_PlacementId();
        S.Test.TestTrue(TEXT("Removing the first ID makes a fresh ID available in the same drain"),
            S.Id.IsValid() && NewId.IsValid() && S.Id != NewId &&
            !Occupancy::TryGet_Placement(S.Board, S.Id).Get_Succeeded() &&
            Occupancy::TryGet_PlacementAt(S.Board, {}).Get_PlacementId() == NewId);
        S.Put(S.A);
        S.Remove(NewId);
    });
    WaitResults(Case, 5);
    Step(Case, [](FCase& S)
    {
        S.Outcomes({Failure::None,Failure::None,Failure::None,Failure::Occupied,Failure::None});
        S.Test.TestTrue(TEXT("Reverse order cannot admit a contender before removal"),
            !Occupancy::TryGet_PlacementAt(S.Board, {}).Get_Succeeded() && S.Signals.Num() == 4);
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementMove, "Ck.HexGrid.Placement.SelfOverlappingMove", ck_hex_placement_test::Flags)
bool FCkHexPlacementMove::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S) { S.Put(S.Pair); });
    WaitResults(Case, 1);
    Step(Case, [](FCase& S)
    {
        S.Id = S.Typed[0].Get_PlacementId();
        S.DuringSignal = [&S](FName Event, const View& V)
        {
            if (Event == TEXT("Moved"))
            {
                S.Test.TestTrue(TEXT("Old-only cell released at move signal; shared and new cells keep ID"),
                    !Occupancy::TryGet_PlacementAt(S.Board, {}).Get_Succeeded() && V.Get_PlacementId() == S.Id &&
                    V.Get_Cells() == TArray<FCk_HexCoord>{{1,0},{2,0}});
            }
        };
        S.Move(S.Id, {1,0});
    });
    WaitResults(Case, 2);
    Step(Case, [](FCase& S) { S.Revision = S.CurrentRevision(); S.Move(S.Id, {1,0}); });
    WaitResults(Case, 3);
    Step(Case, [](FCase& S)
    {
        S.Outcomes({Failure::None,Failure::None,Failure::None});
        S.Test.TestTrue(TEXT("Idempotent move preserves revision, ID and signal count"),
            S.Typed[2].Get_PlacementId() == S.Id && S.Revision == S.CurrentRevision() && S.Signals.Num() == 2);
    });
    End(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementRetention, "Ck.HexGrid.Placement.FailedReplacementRetainsOld", ck_hex_placement_test::Flags)
bool FCkHexPlacementRetention::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S) { S.Put(S.Pair, {-2,0}); S.Put(S.B, {2,0}); });
    WaitResults(Case, 2);
    Step(Case, [](FCase& S)
    {
        S.Id = S.Typed[0].Get_PlacementId();
        S.LastPlacement = S.Typed[0].Get_Placement().Get_Handle();
        auto LastCell = S.CellAt({2,0});
        Cell::Request_AddTag(LastCell, Hazard, {});
        Cell::Request_SetBlocked(LastCell, true, {});
        S.DuringResult = [&S](const Result& R)
        {
            const auto& V = R.Get_Placement();
            S.Test.TestTrue(TEXT("Rejected replacement returns exact old view and retains all ownership"),
                !R.Get_Succeeded() && R.Get_PlacementId() == S.Id && V.Get_PlacementId() == S.Id
                && V.Get_Anchor() == FCk_HexCoord{-2,0} && V.Get_Rotation() == Rotation::Turn0
                && V.Get_Cells() == TArray<FCk_HexCoord>{{-2,0},{-1,0}} && V.Get_Handle() == S.LastPlacement
                && Occupancy::TryGet_PlacementForOccupant(S.Pair).Get_PlacementId() == S.Id
                && Occupancy::TryGet_PlacementAt(S.Board, {-2,0}).Get_PlacementId() == S.Id
                && Occupancy::TryGet_PlacementAt(S.Board, {-1,0}).Get_PlacementId() == S.Id
                && !Occupancy::TryGet_PlacementAt(S.Board, {1,0}).Get_Succeeded());
            if (R.Get_Failure() != Failure::Overflow)
            { S.Test.TestTrue(TEXT("Only the last cell failed"), R.Get_FailedCells() == TArray<FCk_HexCoord>{{2,0}}); }
        };
    });
    Wait(Case, [](const FCase& S)
    {
        FCk_Handle_HexCell CellHandle;
        ECk_HexBoardFailure Why{};
        bool Blocked = false;
        return Grid::TryGet_CellAt(S.Board, {2,0}, CellHandle, Why) && Cell::TryGet_IsDisabled(CellHandle, Blocked) && Blocked;
    }, TEXT("Rule state committed"));
    Step(Case, [](FCase& S) { S.Revision = S.CurrentRevision(); S.Put(S.Pair, {1,0}); });
    WaitResults(Case, 3);
    Step(Case, [](FCase& S)
    {
        S.Test.TestTrue(TEXT("Blocked failure retains revision"), S.CurrentRevision() == S.Revision);
        auto LastCell = S.CellAt({2,0});
        Cell::Request_SetBlocked(LastCell, false, {});
        S.Move(S.Id, {1,0});
        Cell::Request_RemoveTag(LastCell, Hazard, {});
        S.Move(S.Id, {1,0});
        S.Move(S.Id, {MAX_int32,0});
    });
    WaitResults(Case, 6);
    Step(Case, [](FCase& S)
    {
        S.Outcomes({Failure::None,Failure::None,Failure::Blocked,Failure::TagMismatch,Failure::Occupied,Failure::Overflow});
        S.Test.TestTrue(TEXT("Only original placed signals fired and record count stayed two"),
            S.Signals.Num() == 2 && Occupancy::Get_Placements(S.Board).Get_Placements().Num() == 2);
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementUniqueness, "Ck.HexGrid.Placement.UniquenessAndTopologies", ck_hex_placement_test::Flags)
bool FCkHexPlacementUniqueness::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S)
    {
        FCk_2dGridSystem_Spec Spec{FIntPoint{3,3}, FVector2D{100.,100.}};
        Spec.Set_DefaultCellState(ECk_EnableDisable::Enable);
        S.Square = UCk_Utils_2dGridSystem_UE::Create(S.Parent, FTransform::Identity, Spec);
        auto Occupant = FCk_Handle{S.A};
        UCk_Utils_2dGridObject_UE::Add(Occupant, FCk_2dGridObject_Spec{FIntPoint{1,1}});
        S.SquarePlacement = UCk_Utils_2dGridPlacement_UE::Request_Place(
            S.Square, Occupant, {1,1}, ECk_CardinalRotation::None, {});
        S.Test.TestTrue(TEXT("Real square placement created"), ck::IsValid(S.SquarePlacement));
        S.Put(S.A);
        S.Put(S.A, {1,0});
    });
    WaitResults(Case, 2);
    Step(Case, [](FCase& S)
    {
        S.Id = S.Typed[0].Get_PlacementId();
        S.Test.TestTrue(TEXT("Same-board replacement keeps ID and emits one move"),
            S.Id.IsValid() && S.Typed[1].Get_PlacementId() == S.Id &&
            S.Events == TArray<FName>{TEXT("Placed"),TEXT("Typed"),TEXT("Generic"),TEXT("Moved"),TEXT("Typed"),TEXT("Generic")});
        S.Submit(Candidate(S.A), S.Other);
    });
    WaitResults(Case, 3);
    Step(Case, [](FCase& S)
    {
        S.Outcomes({Failure::None,Failure::None,Failure::CrossGridTransfer});
        S.Test.TestTrue(TEXT("Rejected transfer preserves old hex and simultaneous square placement"),
            Occupancy::Get_Placements(S.Other).Get_Placements().IsEmpty() &&
            Occupancy::TryGet_PlacementForOccupant(S.A).Get_PlacementId() == S.Id &&
            UCk_Utils_2dGridOccupancy_UE::Get_PlacementForOccupant(S.A) == S.SquarePlacement &&
            UCk_Utils_2dGridOccupancy_UE::Get_IsOccupied(S.Square, {1,1}));
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementConnectivity, "Ck.HexGrid.Placement.ConnectivityAndHoleAnchor", ck_hex_placement_test::Flags)
bool FCkHexPlacementConnectivity::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S)
    {
        for (int32 Index = 0; Index != 6; ++Index)
        {
            const auto Preview = Place::Get_CanPlace(S.Board,
                Candidate(S.Pair, {}, static_cast<Rotation>(Index), Connectivity::RequireConnected));
            S.Test.TestTrue(TEXT("Every rotated neighbor is connected"), Preview.Get_Succeeded() &&
                Preview.Get_Placement().Get_Cells().Num() == 2);
        }
        auto Islands = S.Shape({{-1,0},{1,0}});
        S.Submit(Candidate(Islands, {}, Rotation::Turn0, Connectivity::RequireConnected), S.Board);
        S.Submit(Candidate(Islands, {}, Rotation::Turn0, Connectivity::Ignore), S.Board);
        auto Hole = S.Shape({{-1,0}});
        S.Submit(Candidate(Hole, {4,0}), S.Board);
    });
    WaitResults(Case, 3);
    Step(Case, [](FCase& S)
    {
        S.Outcomes({Failure::Disconnected,Failure::None,Failure::AnchorOutsideDomain});
        S.Test.TestTrue(TEXT("Hole anchor remains a board member but no reservation occupies it"),
            !Occupancy::TryGet_PlacementAt(S.Board, {}).Get_Succeeded() &&
            S.Typed[1].Get_Placement().Get_Cells() == TArray<FCk_HexCoord>{{-1,0},{1,0}}
            && S.Typed[2].Get_FailedCells() == TArray<FCk_HexCoord>{{4,0}});
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementDeaths, "Ck.HexGrid.Placement.DeathOrders", ck_hex_placement_test::Flags)
bool FCkHexPlacementDeaths::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S) { S.Put(S.A); });
    WaitResults(Case, 1);
    Step(Case, [](FCase& S)
    {
        S.LastPlacement = S.Typed[0].Get_Placement().Get_Handle();
        S.Id = S.Typed[0].Get_PlacementId();
        Life::Request_DestroyEntity(S.A);
        S.Test.TestTrue(TEXT("Reservation remains visible until occupant-death removal commits"),
            Occupancy::TryGet_PlacementAt(S.Board, {}).Get_PlacementId() == S.Id);
        S.Put(S.B);
    });
    Wait(Case, [](const FCase& S) { return S.Typed.Num() >= 2 && ck::Is_NOT_Valid(S.LastPlacement); },
        TEXT("Queued occupant death precedes the next Place and the original placement retires"));
    Step(Case, [](FCase& S)
    {
        S.Test.TestTrue(TEXT("Exactly one removal keeps the old ID and the follower owns the released cell"),
            S.Signals.Num() == 3 && S.Signals[1].Get_PlacementId() == S.Id &&
            !Occupancy::TryGet_Placement(S.Board, S.Id).Get_Succeeded() &&
            Occupancy::TryGet_PlacementAt(S.Board, {}).Get_Placement().Get_Occupant() == S.B);
    });
    WaitResults(Case, 2);
    Step(Case, [](FCase& S)
    {
        S.LastPlacement = S.Typed[1].Get_Placement().Get_Handle();
        Life::Request_DestroyEntity(S.LastPlacement);
    });
    Wait(Case, [](const FCase& S) { return S.Signals.Num() == 4; }, TEXT("Placement-first cleanup signal"));
    Step(Case, [](FCase& S)
    {
        S.Test.TestTrue(TEXT("Occupant outlives externally retired placement with no stale backref"),
            ck::IsValid(S.B) && !Occupancy::TryGet_PlacementForOccupant(S.B).Get_Succeeded());
        S.Put(S.B);
    });
    WaitResults(Case, 3);
    Step(Case, [](FCase& S)
    {
        S.LastPlacement = S.Typed[2].Get_Placement().Get_Handle();
        S.Id = S.Typed[2].Get_PlacementId();
        Life::Request_DestroyEntity(S.Board);
        S.Test.TestTrue(TEXT("Destroying is visible synchronously"), Grid::Get_Readiness(S.Board).Get_State() == Readiness::Destroying);
    });
    Wait(Case, [](const FCase& S) { return ck::Is_NOT_Valid(S.Board) && ck::Is_NOT_Valid(S.LastPlacement); },
        TEXT("Grid-owned placement retires"));
    Step(Case, [](FCase& S)
    {
        S.Test.TestTrue(TEXT("No false success notification on board retirement"), S.Signals.Num() == 5 &&
            !Occupancy::TryGet_PlacementForOccupant(S.B).Get_Succeeded() && ck::IsValid(S.B));
        S.Submit(Candidate(S.B), S.Other);
    });
    WaitResults(Case, 4);
    Step(Case, [](FCase& S)
    {
        S.Outcomes({Failure::None,Failure::None,Failure::None,Failure::None});
        S.Test.TestTrue(TEXT("Released occupant can claim another board with a fresh ID"),
            S.Typed[3].Get_PlacementId().IsValid() && S.Typed[3].Get_PlacementId() != S.Id &&
            Occupancy::TryGet_PlacementForOccupant(S.B).Get_Placement().Get_Grid() == S.Other);
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementCancel, "Ck.HexGrid.Placement.CancellationAndCompletion", ck_hex_placement_test::Flags)
bool FCkHexPlacementCancel::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S)
    {
        S.Submit(Candidate(S.B), S.Other);
        Life::Request_DestroyEntity(S.Other);
    });
    WaitResults(Case, 1);
    Step(Case, [](FCase& S)
    {
        S.DuringSignal = [&S](FName Event, const View&)
        {
            if (Event == TEXT("Placed"))
            {
                S.Put(S.B, {2,0});
                Life::Request_DestroyEntity(S.Board);
            }
        };
        S.Put(S.A);
        S.Put(S.B, {1,0});
        S.Remove(FGuid::NewGuid());
    });
    WaitResults(Case, 5);
    Wait(Case, [](const FCase& S) { return ck::Is_NOT_Valid(S.Board); }, TEXT("Retirement drains pending commands"));
    Step(Case, [](FCase& S)
    {
        S.Outcomes({Failure::Cancelled,Failure::None,Failure::Cancelled,Failure::Cancelled,Failure::Cancelled});
        S.Test.TestTrue(TEXT("Only committed request signals; every cancellation is terminal on both channels"),
            S.Signals.Num() == 1 && S.Generic == TArray<Outcome>{Outcome::Failed_Cancelled,Outcome::Succeeded,
                Outcome::Failed_Cancelled,Outcome::Failed_Cancelled,Outcome::Failed_Cancelled} &&
            S.Events == TArray<FName>{TEXT("Typed"),TEXT("Generic"),TEXT("Placed"),TEXT("Typed"),TEXT("Generic"),
                TEXT("Typed"),TEXT("Generic"),TEXT("Typed"),TEXT("Generic"),TEXT("Typed"),TEXT("Generic")});
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementReadiness, "Ck.HexGrid.Placement.Readiness", ck_hex_placement_test::Flags)
bool FCkHexPlacementReadiness::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S)
    {
        S.Test.TestTrue(TEXT("Fresh empty board immediately Ready"),
            Grid::Get_IsReady(S.Board) && !Grid::Get_IsPending(S.Board) && Occupancy::Get_Placements(S.Board).Get_Succeeded());
        S.Put(S.A);
    });
    WaitResults(Case, 1);
    Step(Case, [](FCase& S)
    {
        FCk_Delegate_HexGrid_ReadinessChanged Observer;
        Observer.BindDynamic(S.Listener.Get(), &UCk_HexPlacementTestListener::OnReadiness);
        Grid::BindTo_OnReadinessChanged(S.Board, Observer, ECk_Signal_BindingPolicy::IgnorePayloadInFlight);
        S.Id = S.Typed[0].Get_PlacementId();
        for (Readiness State : {Readiness::AwaitingRestore,Readiness::AwaitingReplication})
        {
            S.Test.TestTrue(TEXT("Private transition admitted"),
                ck::hex_grid::SetReadiness(S.Board, FCk_HexGridReadiness_Result{State, Failure::NotReady, {}}));
            S.Test.TestTrue(TEXT("Structural cells and feature remain present while awaiting baseline"),
                Grid::Has(S.Board) && Grid::Get_IsPending(S.Board) && !Grid::Get_IsReady(S.Board) && S.CellAt({}) == S.Center);
            bool Blocked = true;
            FGameplayTagContainer Tags{Hazard};
            S.Test.TestTrue(TEXT("Mutable reads fail and clear their outputs"),
                !Cell::TryGet_IsDisabled(S.Center, Blocked) && !Blocked &&
                !Cell::TryGet_Tags(S.Center, Tags) && Tags.IsEmpty());
            S.Test.TestTrue(TEXT("Every public occupancy route identifies pending"),
                Occupancy::Get_Placements(S.Board).Get_Failure() == Failure::NotReady &&
                Occupancy::TryGet_PlacementAt(S.Board, {}).Get_Failure() == Failure::NotReady &&
                Occupancy::TryGet_Placement(S.Board, S.Id).Get_Failure() == Failure::NotReady &&
                Occupancy::TryGet_PlacementForOccupant(S.A).Get_Failure() == Failure::NotReady);
            S.Test.TestTrue(TEXT("Pending read cannot disclose the hidden cell or occupant ID"),
                !Occupancy::TryGet_PlacementAt(S.Board, {}).Get_PlacementId().IsValid() &&
                !Occupancy::TryGet_PlacementForOccupant(S.A).Get_PlacementId().IsValid());
            const auto Projection = ck::hex_grid::ReadCommittedProjection(S.Board);
            S.Test.TestTrue(TEXT("Private capture preserves prior reservation during pending"),
                Projection._Placements.Num() == 1 && Projection._Placements[0].Get_PlacementId() == S.Id);
            Cell::Request_SetBlocked(S.Center, true, S.OnCompleted);
            S.Put(S.B, {1,0});
            S.Test.TestTrue(TEXT("Both mutations rejected immediately"), S.Typed.Last().Get_Failure() == Failure::NotReady &&
                S.Generic.Num() >= 2 && S.Generic.Last() == Outcome::Failed_NotEnqueued &&
                S.Generic[S.Generic.Num()-2] == Outcome::Failed_NotEnqueued);
        }
        const auto Fault = FCk_HexGridReadiness_Result{Readiness::Unavailable, Failure::InvalidProjection, S.Id};
        S.Test.TestTrue(TEXT("Terminal readiness retains cause and ID"),
            ck::hex_grid::SetReadiness(S.Board, Fault) && Grid::Get_Readiness(S.Board).Get_PlacementId() == S.Id &&
            Grid::Get_Readiness(S.Board).Get_Failure() == Failure::InvalidProjection && !Grid::Get_IsPending(S.Board)
            && Occupancy::Get_Placements(S.Board).Get_Failure() == Failure::InvalidProjection &&
            Place::Get_CanPlace(S.Board, Candidate(S.B)).Get_Failure() == Failure::InvalidProjection);
        S.Outcomes({Failure::None,Failure::NotReady,Failure::NotReady});
        S.Test.TestFalse(TEXT("Late success cannot reopen terminal failure"),
            ck::hex_grid::SetReadiness(S.Board, FCk_HexGridReadiness_Result{Readiness::Ready, Failure::None, {}}));
        S.Test.TestFalse(TEXT("A later failure cannot replace the first terminal cause"),
            ck::hex_grid::SetReadiness(S.Board, FCk_HexGridReadiness_Result{Readiness::Unavailable, Failure::RestoreTimedOut, {}}));
        S.Test.TestTrue(TEXT("Observers saw both pending transitions and terminal cause"),
            S.ReadinessChanges.Num() == 3 && S.ReadinessChanges.Last().Get_Failure() == Failure::InvalidProjection &&
            Grid::Get_Readiness(S.Board).Get_PlacementId() == S.Id && !Grid::Get_IsReady(S.Board));
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementProjection, "Ck.HexGrid.Placement.ProjectionAtomicity", ck_hex_placement_test::Flags)
bool FCkHexPlacementProjection::RunTest(const FString&)
{
    using namespace ck_hex_placement_test;
    auto Case = Begin(*this);
    Step(Case, [](FCase& S) { S.Put(S.A); S.Submit(Candidate(S.B), S.Other); });
    WaitResults(Case, 2);
    Step(Case, [](FCase& S) { Cell::Request_SetBlocked(S.Center, true, S.OnCompleted); });
    Wait(Case, [](const FCase& S) { bool Block = false; return Cell::TryGet_IsDisabled(S.Center, Block) && Block; },
        TEXT("Existing placement subsequently blocked"));
    Step(Case, [](FCase& S)
    {
        auto Projection = ck::hex_grid::ReadCommittedProjection(S.Board);
        S.Id = Occupancy::TryGet_PlacementForOccupant(S.A).Get_PlacementId();
        if (!S.Test.TestTrue(TEXT("Captured placement and late manual block"),
            Projection._Placements.Num() == 1 && Projection._ManualBlocks.Contains(FCk_HexCoord{}))) { return; }
        S.Test.TestTrue(TEXT("Committed placement survives blocking after admission"),
            ck::hex_grid::TryCommitProjection(S.Board, Projection).Get_Succeeded() &&
            Occupancy::TryGet_PlacementAt(S.Board, {}).Get_PlacementId() == S.Id);

        const auto Anonymous = [&S](FCk_HexCoord Coord)
        {
            View V;
            V.Set_PlacementId(FGuid::NewGuid());
            V.Set_Grid(S.Board);
            V.Set_OccupantState(ECk_HexGridOccupantState::Anonymous);
            V.Set_Anchor(Coord);
            V.Set_FootprintOffsets({FCk_HexCoord{}});
            V.Set_Cells({Coord});
            return V;
        };
        const auto Left = Anonymous({-1,0});
        const auto Right = Anonymous({1,0});
        Projection._Placements.Add(Left);
        Projection._Placements.Add(Right);
        if (!S.Test.TestTrue(TEXT("Two distinct anonymous internal reservations installed atomically"),
            ck::hex_grid::TryCommitProjection(S.Board, Projection).Get_Succeeded())) { return; }
        const auto Committed = ck::hex_grid::ReadCommittedProjection(S.Board);
        const auto Reject = [&S,&Committed,&Left,&Right](const FCk_HexGridCommittedProjection& Bad, const TCHAR* Label)
        {
            const auto Failed = ck::hex_grid::TryCommitProjection(S.Board, Bad);
            const auto After = ck::hex_grid::ReadCommittedProjection(S.Board);
            S.Test.TestTrue(Label, !Failed.Get_Succeeded() && After._Revision == Committed._Revision &&
                After._Placements.Num() == 3 && After._ManualBlocks.Num() == 1 && After._ManualBlocks.Contains(FCk_HexCoord{}) &&
                Occupancy::TryGet_PlacementAt(S.Board, {-1,0}).Get_PlacementId() == Left.Get_PlacementId() &&
                Occupancy::TryGet_PlacementAt(S.Board, {1,0}).Get_PlacementId() == Right.Get_PlacementId() &&
                Occupancy::TryGet_PlacementForOccupant(S.A).Get_PlacementId() == S.Id &&
                Occupancy::TryGet_PlacementForOccupant(S.B).Get_Placement().Get_Grid() == S.Other);
        };
        auto Bad = Committed;
        Bad._Placements.Add(Left);
        Reject(Bad, TEXT("Final duplicate ID rejected without changing ownership or revision"));
        Bad = Committed;
        Bad._Placements.Add(Anonymous(Right.Get_Anchor()));
        Reject(Bad, TEXT("Last overlapping anonymous entry rejected atomically"));
        const auto NewBound = [&](FCk_Handle_HexGridObject Body)
        {
            auto V = Anonymous({0,1});
            V.Set_Occupant(Body);
            V.Set_OccupantState(ECk_HexGridOccupantState::Bound);
            return V;
        };
        FCk_Handle_HexGridObject Foreign;
        ECk_HexGridObject_Failure ObjectFailure{};
        if (!S.Test.TestTrue(TEXT("Compose a real object in the board's private cell registry"),
            Object::TryAdd(S.Center, FCk_HexGridObject_Spec{}, Foreign, ObjectFailure))) { return; }
        for (const auto Body : {S.A,S.B,Foreign})
        {
            Bad = Committed;
            Bad._Placements.Add(NewBound(Body));
            Reject(Bad, TEXT("Duplicate, already-placed or foreign-registry occupant rejected atomically"));
        }
        Bad = Committed;
        Bad._ManualBlocks.Add({999,999});
        Reject(Bad, TEXT("Out-of-domain manual vote rejected without partial changes"));
        Bad = Committed;
        Bad._LocalTags.Add({999,999}, FGameplayTagContainer{Hazard});
        Reject(Bad, TEXT("Out-of-domain local tags rejected without partial changes"));
        Bad = Committed;
        FCk_HexGridBlockerView InvalidBlocker;
        InvalidBlocker._BlockerId = FGuid::NewGuid();
        InvalidBlocker._Cells = {{0,0}, {999,999}};
        Bad._Blockers.Add(InvalidBlocker);
        Reject(Bad, TEXT("Invalid final blocker cell rejected without partial changes"));
        S.Put({});
    });
    WaitResults(Case, 3);
    Step(Case, [](FCase& S)
    {
        S.Outcomes({Failure::None,Failure::None,Failure::InvalidObject});
        S.Test.TestEqual(TEXT("Public anonymous rejection preserves private projection"), Occupancy::Get_Placements(S.Board).Get_Placements().Num(), 3);
        auto Drifted = ck::hex_grid::ReadCommittedProjection(S.Board);
        auto* Bound = Drifted._Placements.FindByPredicate([&S](const View& V){ return V.Get_PlacementId() == S.Id; });
        if (!S.Test.TestNotNull(TEXT("Captured bound reservation"), Bound)) { return; }
        Bound->Set_FootprintOffsets({{}, {0,1}});
        Bound->Set_Cells({{}, {0,1}});
        Bound->Set_RequiredCellTags(FGameplayTagContainer{Hazard});
        S.Test.TestTrue(TEXT("Internal projection preserves the captured footprint and policy at the original anchor"),
            ck::hex_grid::TryCommitProjection(S.Board, Drifted).Get_Succeeded() &&
            Occupancy::TryGet_PlacementAt(S.Board, {0,1}).Get_PlacementId() == S.Id);
        Cell::Request_SetBlocked(S.Center, false, {});
    });
    Wait(Case, [](const FCase& S) { bool Blocked = true; return Cell::TryGet_IsDisabled(S.Center, Blocked) && !Blocked; },
        TEXT("Admission reopened after hydrated reservation"));
    Step(Case, [](FCase& S) { S.Revision = S.CurrentRevision(); S.Events.Reset(); S.Put(S.A); });
    WaitResults(Case, 4);
    Step(Case, [](FCase& S)
    {
        const auto& R = S.Typed[3];
        S.Test.TestTrue(TEXT("Same-anchor Place recaptures drifted shape and tags as one move"),
            R.Get_Succeeded() && R.Get_PlacementId() == S.Id && R.Get_Placement().Get_RequiredCellTags().IsEmpty() &&
            R.Get_Placement().Get_FootprintOffsets() == TArray<FCk_HexCoord>{{0,0}} &&
            !Occupancy::TryGet_PlacementAt(S.Board, {0,1}).Get_Succeeded() && S.CurrentRevision() == S.Revision + 1 &&
            S.Events == TArray<FName>{TEXT("Moved"),TEXT("Typed"),TEXT("Generic")});
    });
    End(); return true;
}

#endif
