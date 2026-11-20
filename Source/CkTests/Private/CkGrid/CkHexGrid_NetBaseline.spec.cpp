#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkHexGridNetRejectionWitness.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/Persistence/CkPersistenceHydration_Processor.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_Spec.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGridInitialization_Data.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGrid_Projection.h"
#include "CkGrid/HexGridSystem/Persistence/CkHexGridPersistence_Utils.h"
#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"
#include "CkTests/Net/CkAutoTest_NetSubject_MixedGrid.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <EngineUtils.h>

namespace ck_test_hex_net_baseline
{
    using namespace ck::auto_test::net;
    using FBoardActor = ACk_AutoTest_NetSubject_MixedGrid_UE;
    using FObjectActor = ACk_AutoTest_NetSubject_MixedGridObject_UE;

    template <typename T>
    auto Find(UWorld* InWorld) -> T*
    {
        if (!IsValid(InWorld)) { return nullptr; }
        TActorIterator<T> It(InWorld);
        return It ? *It : nullptr;
    }

    auto Wait(FAutomationTestBase& Test, FCk_NetAutoTest_Assertion Predicate, const TCHAR* Message) -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(Predicate, 30.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(&Test, Predicate, Message));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexNet_InitialAndEmptyBaseline, "Ck.HexGrid.Net.InitialAndEmptyBaseline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkHexNet_InitialAndEmptyBaseline::RunTest(const FString&)
{
    using namespace ck_test_hex_net_baseline;
    const auto Join = MakeShared<FCk_NetAutoTest_LateJoinResult>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* World)
    { World->SpawnActor<FBoardActor>(); })));
    Wait(*this, FCk_NetAutoTest_Assertion::CreateLambda([]()
    {
        const auto* Actor = Find<FBoardActor>(Get_ServerWorld());
        return Actor && UCk_Utils_HexGrid_UE::Get_IsReady(Actor->Get_HexGrid());
    }), TEXT("Authoritative authored board is ready"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
    {
        const auto* Actor = Find<FBoardActor>(World);
        if (!Actor) { AddError(TEXT("Missing authoritative board")); return; }
        auto Board = Actor->Get_HexGrid();
        if (!TestTrue(TEXT("Authoring creates a nonempty blocker recipe"), ck::IsValid(Board) &&
            !ck::hex_grid::ReadCommittedProjection(Board)._Blockers.IsEmpty())) { return; }
        TestTrue(TEXT("Authority clears whole projection without ever placing an occupant"),
            ck::hex_grid::TryCommitProjection(Board, {}).Get_Succeeded());
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RequestLateJoin(Join, 30.0));
    Wait(*this, FCk_NetAutoTest_Assertion::CreateLambda([Join]()
    {
        if (Join->Status != ECk_NetAutoTest_LateJoinStatus::Joined) { return false; }
        const auto* Actor = Find<FBoardActor>(Join->ClientWorld.Get());
        if (!Actor || Actor->Get_ConstructReadiness().Get_State() != ECk_HexGridReadiness::AwaitingReplication ||
            !UCk_Utils_EntityReplicationDriver_UE::Get_IsIncomingReplica(Actor->Get_HexGrid()) ||
            !UCk_Utils_HexGrid_UE::Get_IsReady(Actor->Get_HexGrid())) { return false; }
        const auto Result = ck::hex_grid::ReadCommittedProjection(Actor->Get_HexGrid());
        return Result._Blockers.IsEmpty() && Result._ManualBlocks.IsEmpty() && Result._LocalTags.IsEmpty() &&
            Result._Placements.IsEmpty();
    }), TEXT("Fresh client observed pending inside construction and committed an explicitly empty first baseline"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexNet_MissingBaselineDeadline, "Ck.HexGrid.Net.MissingBaselineDeadline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkHexNet_MissingBaselineDeadline::RunTest(const FString&)
{
    using namespace ck_test_hex_net_baseline;
    const auto Observed = MakeShared<FCk_Handle_HexGrid>();
    const auto Start = MakeShared<double>(0.0);
    const auto Witness = MakeShared<TStrongObjectPtr<UCk_HexGridNetRejectionWitness>>(NewObject<UCk_HexGridNetRejectionWitness>());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* World)
    { World->SpawnActor<FObjectActor>(); })));
    Wait(*this, FCk_NetAutoTest_Assertion::CreateLambda([]()
    {
        const auto* Actor = Find<FObjectActor>(Get_ClientWorld(0));
        return Actor && UCk_Utils_EntityReplicationDriver_UE::Get_IsIncomingReplica(Actor->Get_HexObject());
    }), TEXT("Piece replicated independently on its real incoming driver"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0, FCk_NetAutoTest_ServerAction::CreateLambda([this, Observed, Start](UWorld* World)
    {
        // Only this client composes a board on the real piece driver. The server has no board to send.
        const auto* Actor = Find<FObjectActor>(World);
        const auto* Asset = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, ck::tests::hex_persistence::BoardAssetPath);
        if (!Actor || !Asset) { AddError(TEXT("Missing timeout fixture")); return; }
        FCk_Handle Entity = Actor->Get_HexObject();
        FCk_HexGrid_Spec Spec;
        FCk_HexGridRecipeIdentity Recipe;
        ECk_HexBoardFailure Failure{};
        const FCk_HexGridInitialization Init;
        *Start = FPlatformTime::Seconds();
        TestTrue(TEXT("Correlated client begins waiting for the intentionally absent board baseline"),
            Asset->TryResolveWithIdentity(Spec, Recipe, Failure) && UCk_Utils_HexGrid_UE::TryAddForAuthoring(
                Entity, FTransform::Identity, Spec, *Observed, Failure, &Init, &Recipe) &&
            UCk_Utils_HexGrid_UE::Get_Readiness(*Observed).Get_State() == ECk_HexGridReadiness::AwaitingReplication);
    })));
    Wait(*this, FCk_NetAutoTest_Assertion::CreateLambda([Observed]()
    {
        return UCk_Utils_HexGrid_UE::Get_Readiness(*Observed).Get_Failure() == ECk_HexGridFailure::ReplicationTimedOut;
    }), TEXT("Missing baseline reaches terminal timeout"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda([Observed, Start]()
    {
        const auto Result = UCk_Utils_HexGrid_UE::Get_Readiness(*Observed);
        return FPlatformTime::Seconds() - *Start >= ck::PendingApplyTimeoutSeconds + 5.0 &&
            Result.Get_State() == ECk_HexGridReadiness::Unavailable &&
            !ck::hex_grid::ReadCommittedProjection(*Observed)._Blockers.IsEmpty();
    }), TEXT("Real wall deadline elapsed and unaccepted authored projection was never cleared")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0, FCk_NetAutoTest_ServerAction::CreateLambda([this, Observed](UWorld*)
    {
        const auto* Handler = FCk_PersistenceHandlerRegistry::Find(FCk_RepData_HexGridState::StaticStruct());
        if (!TestTrue(TEXT("Actual registered receiver available"), Handler && Handler->NetApply && ck::IsValid(*Observed))) { return; }
        auto Wire = ck::hex_grid::persistence::ReadCommittedProjection(*Observed);
        Wire.Set_SchemaVersion(-1);
        auto Entity = FCk_Handle{*Observed};
        const auto Data = FInstancedStruct::Make(Wire);
        TestTrue(TEXT("Rejected first baseline reports invalid schema with original recipe intact"),
            Handler->NetApply(Entity, Data, {}) == ECk_Persistence_ApplyResult::Rejected &&
            UCk_Utils_HexGrid_UE::Get_Readiness(*Observed).Get_Failure() == ECk_HexGridFailure::InvalidSchema &&
            !ck::hex_grid::ReadCommittedProjection(*Observed)._Blockers.IsEmpty());
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
    {
        const auto* Actor = Find<FObjectActor>(World);
        const auto* Asset = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, ck::tests::hex_persistence::BoardAssetPath);
        if (!Actor || !Asset) { AddError(TEXT("Missing source for delayed authority baseline")); return; }
        FCk_Handle Entity = Actor->Get_HexObject();
        FCk_HexGrid_Spec Spec;
        FCk_HexGridRecipeIdentity Recipe;
        ECk_HexBoardFailure Failure{};
        const FCk_HexGridInitialization Init;
        FCk_Handle_HexGrid Grid;
        TestTrue(TEXT("Authority composes previously absent board on the existing piece driver"),
            Asset->TryResolveWithIdentity(Spec, Recipe, Failure) && UCk_Utils_HexGrid_UE::TryAddForAuthoring(
                Entity, FTransform::Identity, Spec, Grid, Failure, &Init, &Recipe));
    })));
    Wait(*this, FCk_NetAutoTest_Assertion::CreateLambda([Observed]()
    { return UCk_Utils_HexGrid_UE::Get_IsReady(*Observed); }), TEXT("Valid delayed network baseline recovers the same live board"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0, FCk_NetAutoTest_ServerAction::CreateLambda([this, Observed, Witness](UWorld*)
    {
        if (!UCk_Utils_HexGrid_UE::Get_IsReady(*Observed)) { return; }
        auto Entity = FCk_Handle{*Observed};
        const auto* Handler = FCk_PersistenceHandlerRegistry::Find(FCk_RepData_HexGridState::StaticStruct());
        if (!TestTrue(TEXT("Network receiver remains available"), Handler && Handler->NetApply)) { return; }
        FCk_Delegate_HexGrid_ReplicationBaselineRejected Delegate;
        Delegate.BindDynamic(Witness->Get(), &UCk_HexGridNetRejectionWitness::OnRejected);
        UCk_Utils_HexGrid_UE::BindTo_OnReplicationBaselineRejected(*Observed, Delegate,
            ECk_Signal_BindingPolicy::IgnorePayloadInFlight);
        const auto Before = ck::hex_grid::ReadCommittedProjection(*Observed);
        const auto Accepted = ck::hex_grid::persistence::ReadCommittedProjection(*Observed);
        const auto AssertUnchanged = [&](const TCHAR* Description, const FCk_RepData_HexGridState& Candidate)
        {
            const auto Rejected = Handler->NetApply(Entity, FInstancedStruct::Make(Candidate), {});
            const auto After = ck::hex_grid::persistence::ReadCommittedProjection(*Observed);
            TestTrue(Description, Rejected == ECk_Persistence_ApplyResult::Rejected &&
                UCk_Utils_HexGrid_UE::Get_IsReady(*Observed) &&
                ck::hex_grid::ReadCommittedProjection(*Observed)._Revision == Before._Revision &&
                FCk_RepData_HexGridState::StaticStruct()->CompareScriptStruct(&Accepted, &After, 0));
        };
        auto Wire = Accepted;
        Wire.Set_SchemaVersion(-1);
        AssertUnchanged(TEXT("Invalid schema preserves accepted state"), Wire);

        Wire = Accepted;
        Wire.Get_Recipe().Set_RecipeRevision(Accepted.Get_Recipe().Get_RecipeRevision() + 1);
        AssertUnchanged(TEXT("Stale recipe preserves the same accepted state"), Wire);

        Wire = Accepted;
        Wire.Set_Blockers({});
        FCk_HexGridPlacement_PersistedEntry First;
        First.Set_PlacementId(FGuid{0,0,0,1}).Set_FootprintOffsets({FCk_HexCoord{}}).Set_Cells({FCk_HexCoord{}});
        auto Last = First;
        Last.Set_PlacementId(FGuid{0,0,0,2});
        Wire.Set_Placements({First, Last});
        AssertUnchanged(TEXT("Final overlapping entry rolls back placements and proposed blocker removals"), Wire);
    })));
    Wait(*this, FCk_NetAutoTest_Assertion::CreateLambda([Witness]()
    {
        const auto& Results = (*Witness)->Rejections;
        return (*Witness)->AllNotificationsSawReady && Results.Num() == 3 &&
            Results[0].Get_Failure() == ECk_HexGridFailure::InvalidSchema &&
            Results[1].Get_Failure() == ECk_HexGridFailure::RecipeMismatch &&
            Results[2].Get_Failure() == ECk_HexGridFailure::Occupied &&
            Results[2].Get_PlacementId() == FGuid{0,0,0,2};
    }), TEXT("Each failed update reports its own cause and implicated ID while the prior board is Ready"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
