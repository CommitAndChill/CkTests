#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkHexGridBindingConflictWitness.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Persistence/CkPersistenceInitialization.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_Spec.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGridInitialization_Data.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Fragment.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Persistence/CkHexGridPersistence_Utils.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"
#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/World.h"

namespace ck_hex_binding_test
{
    using EStatus = ECk_Persistence_ApplyResult;
    using EOccupant = ECk_HexGridOccupantState;
    using Occupancy = UCk_Utils_HexGridOccupancy_UE;
    using Life = UCk_Utils_EntityLifetime_UE;
    namespace projection = ck::hex_grid::persistence;
    constexpr uint64 Epoch = 42;

    auto MakeEntity(UWorld* InWorld) -> FCk_Handle
    {
        auto Entity = Life::Request_CreateEntity_TransientOwner(InWorld);
        UCk_Utils_Net_UE::Add(Entity, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
            ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
        return Entity;
    }
    auto MakeBoard(UWorld* InWorld, const UCk_HexGrid_AuthoringSpec& InAsset) -> FCk_Handle_HexGrid
    {
        auto Entity = MakeEntity(InWorld);
        FCk_HexGrid_Spec Spec;
        FCk_HexGridRecipeIdentity Recipe;
        FCk_HexGridInitialization Init;
        FCk_Handle_HexGrid Board;
        ECk_HexBoardFailure Failure{};
        if (InAsset.TryResolveWithIdentity(Spec, Recipe, Failure))
        { UCk_Utils_HexGrid_UE::TryAddForAuthoring(Entity, FTransform::Identity, Spec, Board, Failure, &Init, &Recipe); }
        return Board;
    }
    auto MakeRestoringObject(UWorld* InWorld) -> FCk_Handle_HexGridObject
    {
        auto Entity = MakeEntity(InWorld);
        ck::persistence::TryDeclare(Entity, FCk_RepData_HexGridObjectIdentity::StaticStruct(), Epoch,
            ck::persistence::EInitializationDisposition::BaselineExpected);
        FCk_Handle_HexGridObject Object;
        ECk_HexGridObject_Failure Failure{};
        UCk_Utils_HexGridObject_UE::TryAdd(Entity, {}, Object, Failure);
        return Object;
    }
    auto ApplyObjectIdentity(FCk_Handle InObject, FGuid InKey, bool InRelease = true) -> EStatus
    {
        const auto* Type = FCk_RepData_HexGridObjectIdentity::StaticStruct();
        const auto* Handler = FCk_PersistenceHandlerRegistry::Find(Type);
        if (!Handler || !Handler->HydrationApply) { return EStatus::Rejected; }
        const auto Data = FInstancedStruct::Make(FCk_RepData_HexGridObjectIdentity{}.Set_BindingId(InKey));
        const auto Result = Handler->HydrationApply(InObject, Data, {});
        if (Result == EStatus::Applied)
        {
            ck::persistence::TryAccept(InObject, Type, Epoch);
            if (InRelease) { ck::persistence::TryRelease(InObject, Type, Epoch); }
        }
        return Result;
    }
    auto Entry(FCk_HexCoord InCoord, FGuid InKey = {}, FCk_Handle InHint = {}) -> FCk_HexGridPlacement_PersistedEntry
    {
        return FCk_HexGridPlacement_PersistedEntry{}.Set_PlacementId(FGuid::NewGuid())
            .Set_HasOccupantReference(InKey.IsValid()).Set_OccupantBindingId(InKey).Set_Occupant(InHint)
            .Set_FootprintOffsets({FCk_HexCoord{}}).Set_Cells({InCoord}).Set_Anchor(InCoord);
    }
    auto Apply(FCk_Handle_HexGrid& InBoard, const FCk_RepData_HexGridState& InData) -> FCk_HexGridApply_Result
    { return projection::TryReplaceCommittedProjection(InBoard, InData, ECk_HexGridApplySource::Hydration); }

    struct FRun
    {
        FCk_Handle_HexGrid Board;
        FGuid DeathId;
        FGuid ConflictId;
        TStrongObjectPtr<UCk_HexGridBindingConflictWitness> Witness;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexGrid_ReplayAndReferences, "Ck.HexGrid.Persistence.ReplayAndReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCk_HexGrid_ReplayAndReferences::RunTest(const FString&)
{
    using namespace ck_hex_binding_test;
    auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
    {
        const auto* Asset = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, ck::tests::hex_persistence::BoardAssetPath);
        if (!TestNotNull(TEXT("Authored fixture asset"), Asset)) { return; }
        Run->Board = MakeBoard(World, *Asset);
        auto Other = MakeBoard(World, *Asset);
        if (!TestTrue(TEXT("Two distinct authored boards are ready"),
            UCk_Utils_HexGrid_UE::Get_IsReady(Run->Board) && UCk_Utils_HexGrid_UE::Get_IsReady(Other))) { return; }
        auto& Board = Run->Board;
        Run->Witness.Reset(NewObject<UCk_HexGridBindingConflictWitness>());
        const auto MakeFresh = [&](ECk_Replication Replication, ECk_Net_NetModeType Mode, ECk_Net_EntityNetRole Role)
        {
            auto Owner = Life::Request_CreateEntity_TransientOwner(World);
            UCk_Utils_Net_UE::Add(Owner, FCk_Net_ConnectionSettings{Replication, Mode, Role});
            FCk_Handle_HexGridObject Object;
            ECk_HexGridObject_Failure Failure{};
            UCk_Utils_HexGridObject_UE::TryCreate(Owner, {}, Object, Failure);
            return Object;
        };
        const auto Proxy = MakeFresh(ECk_Replication::Replicates, ECk_Net_NetModeType::Client, ECk_Net_EntityNetRole::Proxy);
        const auto Local = MakeFresh(ECk_Replication::DoesNotReplicate, ECk_Net_NetModeType::Client, ECk_Net_EntityNetRole::Authority);
        const auto Listen = MakeFresh(ECk_Replication::Replicates, ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority);
        TestTrue(TEXT("Listen authority and client-local authority mint distinct accepted keys"),
            ck::hex_grid::GetAcceptedObjectBindingId(Local).IsValid() && ck::hex_grid::GetAcceptedObjectBindingId(Listen).IsValid() &&
            ck::hex_grid::GetAcceptedObjectBindingId(Local) != ck::hex_grid::GetAcceptedObjectBindingId(Listen));
        const auto Spec = FCk_HexGridPlacement_Spec{}.Set_Object(Proxy).Set_Anchor({2,0});
        TestTrue(TEXT("Proxy can preview geometry before receiving identity"),
            !ck::hex_grid::GetAcceptedObjectBindingId(Proxy).IsValid() && UCk_Utils_HexGridPlacement_UE::Get_CanPlace(Board, Spec).Get_Succeeded());
        FCk_Delegate_HexGridPlacement_OnResult PlaceResult;
        PlaceResult.BindDynamic(Run->Witness.Get(), &UCk_HexGridBindingConflictWitness::OnPlace);
        UCk_Utils_HexGridPlacement_UE::Request_Place(Board, FCk_Request_HexGridPlacement_Place{Spec}.Set_OnResult(PlaceResult), {});
        const auto FirstKey = FGuid::NewGuid();
        const auto SecondKey = FGuid::NewGuid();
        auto First = MakeRestoringObject(World);
        auto Second = MakeRestoringObject(World);
        auto Impostor = MakeRestoringObject(World);
        auto WrongHint = MakeRestoringObject(World);
        if (!TestTrue(TEXT("Objects constructed, restored keys initially unpublished"), ck::IsValid(First) &&
            ck::IsValid(Second) && ck::IsValid(Impostor) && ck::IsValid(WrongHint) &&
            !ck::hex_grid::GetAcceptedObjectBindingId(First).IsValid())) { return; }
        TestTrue(TEXT("Object-first baseline accepted"), ApplyObjectIdentity(First, FirstKey) == EStatus::Applied);
        TestTrue(TEXT("Identical identity replay accepted"), ApplyObjectIdentity(First, FirstKey) == EStatus::Applied);
        TestTrue(TEXT("Identity immutable for accepted handle"), ApplyObjectIdentity(First, SecondKey) == EStatus::Rejected &&
            ck::hex_grid::GetAcceptedObjectBindingId(First) == FirstKey);
        TestTrue(TEXT("A different object cannot claim the accepted key"), ApplyObjectIdentity(Impostor, FirstKey) == EStatus::Rejected &&
            !ck::hex_grid::GetAcceptedObjectBindingId(Impostor).IsValid());

        auto Input = projection::ReadCommittedProjection(Board);
        auto Anonymous = Entry({-1,0});
        auto ObjectFirst = Entry({1,0}, FirstKey);
        auto BoardFirst = Entry({0,1}, SecondKey, Second);
        auto Conflict = Entry({0,-1}, FGuid::NewGuid(), WrongHint);
        Input.Set_Placements({Anonymous, ObjectFirst, BoardFirst, Conflict});
        if (!TestTrue(TEXT("Mixed anonymous, bound and unresolved entries commit"), Apply(Board, Input).Get_Status() == EStatus::Applied))
        { return; }
        const auto Get = [&](const FCk_HexGridPlacement_PersistedEntry& InEntry)
        { return Occupancy::TryGet_Placement(Board, InEntry.Get_PlacementId()).Get_Placement(); };
        TestTrue(TEXT("Anonymous has no binding identity"), Get(Anonymous).Get_OccupantState() == EOccupant::Anonymous &&
            !Get(Anonymous).Get_OccupantBindingId().IsValid());
        TestTrue(TEXT("Object-first placement binds the actual accepted object"), Get(ObjectFirst).Get_Occupant() == First);
        TestTrue(TEXT("Live remapped hint alone cannot bind"), Get(BoardFirst).Get_OccupantState() == EOccupant::AwaitingBinding &&
            ck::Is_NOT_Valid(Get(BoardFirst).Get_Occupant()));
        const auto Recaptured = projection::ReadCommittedProjection(Board);
        const auto* Pending = Recaptured.Get_Placements().FindByPredicate([&](const auto& P)
        { return P.Get_PlacementId() == BoardFirst.Get_PlacementId(); });
        TestTrue(TEXT("Unresolved reservation keeps its reference flag and stable key on capture"),
            Pending && Pending->Get_HasOccupantReference() && Pending->Get_OccupantBindingId() == SecondKey);
        auto Collision = projection::ReadCommittedProjection(Other);
        Collision.Set_Placements({Entry({}, SecondKey)});
        TestTrue(TEXT("Duplicate unresolved key on another board rejects the whole projection"),
            Apply(Other, Collision).Get_Failure() == ECk_HexGridFailure::OccupantAlreadyPlaced &&
            projection::ReadCommittedProjection(Other).Get_Placements().IsEmpty());
        const auto StableHandle = Get(BoardFirst).Get_Handle();
        TestTrue(TEXT("Identical board payload is idempotent"), Apply(Board, Input).Get_Status() == EStatus::Applied &&
            Get(BoardFirst).Get_Handle() == StableHandle);
        TestTrue(TEXT("Object publication completes board-first reservation"),
            ApplyObjectIdentity(Second, SecondKey) == EStatus::Applied && Get(BoardFirst).Get_Occupant() == Second &&
            Occupancy::TryGet_PlacementForOccupant(Second).Get_PlacementId() == BoardFirst.Get_PlacementId());

        FCk_Delegate_HexGridPlacement_BindingConflict Delegate;
        Delegate.BindDynamic(Run->Witness.Get(), &UCk_HexGridBindingConflictWitness::OnConflict);
        UCk_Utils_HexGridPlacement_UE::BindTo_OnOccupantBindingConflict(Board, Delegate);
        TestTrue(TEXT("Committed mismatch awaits object release"), ApplyObjectIdentity(WrongHint, FGuid::NewGuid(), false) == EStatus::Applied &&
            Get(Conflict).Get_OccupantState() == EOccupant::AwaitingBinding && Run->Witness->Count == 0);
        TestTrue(TEXT("Released wrong remapped handle marks only the conflicting reservation"),
            ck::persistence::TryRelease(WrongHint, FCk_RepData_HexGridObjectIdentity::StaticStruct(), Epoch) &&
            Get(Conflict).Get_OccupantState() == EOccupant::BindingConflict &&
            Get(Conflict).Get_OccupantBindingId() == Conflict.Get_OccupantBindingId() &&
            Occupancy::TryGet_PlacementAt(Board, Conflict.Get_Anchor()).Get_PlacementId() == Conflict.Get_PlacementId());
        Run->ConflictId = Conflict.Get_PlacementId();
        TestTrue(TEXT("Replaying conflict is idempotent"), Apply(Board, Input).Get_Status() == EStatus::Applied);
        BoardFirst.Set_Anchor({1,-1}).Set_Cells({FCk_HexCoord{1,-1}});
        Input.Set_Placements({ObjectFirst, BoardFirst, Conflict});
        TestTrue(TEXT("Same-id move replaces old cells and clears removed anonymous placement"),
            Apply(Board, Input).Get_Status() == EStatus::Applied && Get(BoardFirst).Get_Handle() == StableHandle &&
            !Occupancy::TryGet_PlacementAt(Board, {0,1}).Get_Succeeded() &&
            !Occupancy::TryGet_Placement(Board, Anonymous.Get_PlacementId()).Get_Succeeded() &&
            Occupancy::TryGet_PlacementAt(Board, {1,-1}).Get_PlacementId() == BoardFirst.Get_PlacementId());
        auto Retiring = MakeRestoringObject(World);
        const auto RetiringKey = FGuid::NewGuid();
        TestTrue(TEXT("Retiring fixture starts with accepted identity"), ApplyObjectIdentity(Retiring, RetiringKey) == EStatus::Applied);
        Life::Request_DestroyEntity(Retiring);
        const auto DyingObjectReservation = Entry({}, RetiringKey);
        const auto DyingPlacementReservation = Entry({1,0}, FGuid::NewGuid());
        auto Terminal = projection::ReadCommittedProjection(Other);
        Terminal.Set_Placements({DyingObjectReservation, DyingPlacementReservation});
        if (TestTrue(TEXT("Board can reserve the key after object has begun destruction"),
            !ck::hex_grid::GetAcceptedObjectBindingId(Retiring).IsValid() && Apply(Other, Terminal).Get_Status() == EStatus::Applied))
        {
            const auto Waiting = Occupancy::TryGet_Placement(Other, DyingObjectReservation.Get_PlacementId()).Get_Placement();
            TestTrue(TEXT("Dying object never becomes the bound occupant"), Waiting.Get_OccupantState() == EOccupant::AwaitingBinding);
            auto Handle = Occupancy::TryGet_Placement(Other, DyingPlacementReservation.Get_PlacementId()).Get_Placement().Get_Handle();
            Life::Request_DestroyEntity(Handle);
            auto Late = MakeRestoringObject(World);
            TestTrue(TEXT("An arriving object cannot bind a placement already being destroyed"),
                ApplyObjectIdentity(Late, DyingPlacementReservation.Get_OccupantBindingId()) == EStatus::Applied &&
                !Occupancy::TryGet_PlacementForOccupant(Late).Get_Succeeded() &&
                Occupancy::TryGet_Placement(Other, DyingPlacementReservation.Get_PlacementId()).Get_Placement().Get_OccupantState() != EOccupant::Bound);
        }
        Run->DeathId = BoardFirst.Get_PlacementId();
        Life::Request_DestroyEntity(Second);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(12));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda([this, Run]()
    {
        if (!TestTrue(TEXT("Setup completed"), ck::IsValid(Run->Board) && Run->Witness.IsValid())) { return false; }
        TestTrue(TEXT("Authority rejects mutation with proxy's absent identity"), Run->Witness->PlaceResults == 1 &&
            Run->Witness->PlaceResult.Get_Failure() == ECk_HexGridFailure::InvalidObject &&
            !Occupancy::TryGet_PlacementAt(Run->Board, {2,0}).Get_Succeeded());
        TestFalse(TEXT("Occupant death retired its placement"), Occupancy::TryGet_Placement(Run->Board, Run->DeathId).Get_Succeeded());
        TestEqual(TEXT("Exactly one late conflict result"), Run->Witness->Count, 1);
        TestTrue(TEXT("Conflict result identifies preserved reservation"),
            Run->Witness->Last.Get_Failure() == ECk_HexGridFailure::LateOccupantConflict &&
            Run->Witness->Last.Get_PlacementId() == Run->ConflictId);
        return !HasAnyErrors();
    }), TEXT("Replay and occupant association complete")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
