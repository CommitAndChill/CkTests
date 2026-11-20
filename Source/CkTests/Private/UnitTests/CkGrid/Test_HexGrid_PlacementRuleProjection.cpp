#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkGrid/HexGridSystem/Blocker/CkHexGridBlocker_Utils.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Fragment.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGrid_Projection.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"

namespace ck_hex_rule_projection_test
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(AuthoredTag, "Ck.Test.HexRuleProjection.Authored");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Door, "Ck.Test.HexRuleProjection.Door");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Alarm, "Ck.Test.HexRuleProjection.Alarm");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacement_RuleProjection, "Ck.HexGrid.Placement.RuleProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexPlacement_RuleProjection::RunTest(const FString&)
{
    using namespace ck_hex_rule_projection_test;
    using namespace ck::hex_grid;
    using Grid = UCk_Utils_HexGrid_UE;
    using Blocker = UCk_Utils_HexGridBlocker_UE;
    using Cell = UCk_Utils_HexCell_UE;
    using Life = UCk_Utils_EntityLifetime_UE;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this](UWorld* World)
        {
            auto Parent = Life::Request_CreateEntity_TransientOwner(World);
            UCk_Utils_Net_UE::Add(Parent, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
                ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
            FCk_HexGrid_Spec Spec;
            const FCk_HexCoord Origin{};
            Spec.Set_PerCellTags({FCk_HexCellTagOverride{Origin, FGameplayTagContainer{AuthoredTag}}});
            Spec.Set_Blockers({FCk_HexGridAuthoredBlocker{{Origin}, Door, true}});
            FCk_Handle_HexGrid Board;
            FCk_Handle_HexCell Center;
            ECk_HexBoardFailure Failure{};
            if (!TestTrue(TEXT("Real authored board and cell"), Grid::TryCreate(Parent, FTransform::Identity, Spec, Board, Failure)
                && Grid::TryGet_CellAt(Board, Origin, Center, Failure))) { return; }
            const auto Initial = ReadCommittedProjection(Board);
            if (!TestEqual(TEXT("One authored contributor"), Initial._Blockers.Num(), 1)) { return; }
            const auto Stable = Blocker::Get_BlockerWithTag(Board, Door);
            auto Modified = Initial;
            Modified._ManualBlocks.Add(Origin);
            Modified._LocalTags.Add(Origin, FGameplayTagContainer{});
            Modified._Blockers[0]._Active = false;
            Modified._Blockers.Add({FGuid::NewGuid(), Alarm, {Origin}, true});
            Modified._Blockers.Add({FGuid::NewGuid(), {}, {Origin}, true});
            if (!TestTrue(TEXT("Commit complete active projection"), TryCommitProjection(Board, Modified).Get_Succeeded()))
            { return; }
            FGameplayTagContainer Tags;
            bool Disabled = false;
            auto RoundTrip = ReadCommittedProjection(Board);
            TestTrue(TEXT("Clear is retained and all three contributor identities remain"),
                RoundTrip._LocalTags.Contains(Origin) && RoundTrip._LocalTags.FindRef(Origin).IsEmpty()
                && RoundTrip._ManualBlocks.Contains(Origin) && RoundTrip._Blockers.Num() == 3
                && Cell::TryGet_LocalTags(Center,Tags) && Tags.IsEmpty()
                && Stable == Blocker::Get_BlockerWithTag(Board, Door) && !Blocker::Get_IsActive(Stable)
                && Blocker::Get_IsActive(Blocker::Get_BlockerWithTag(Board, Alarm))
                && Cell::TryGet_IsDisabled(Center, Disabled) && Disabled);
            auto Bad = Initial;
            Bad._Blockers.Add({FGuid::NewGuid(), {}, {FCk_HexCoord{8,-2}}, true});
            const auto Before = Life::Get_LifetimeDependents(Board);
            TestTrue(TEXT("Invalid final rule preserves all contributors, local state, revision and leases"),
                !TryCommitProjection(Board, Bad).Get_Succeeded() && Before == Life::Get_LifetimeDependents(Board)
                && ReadCommittedProjection(Board)._Revision == RoundTrip._Revision && Blocker::Get_Blockers(Board).Num() == 3
                && Cell::TryGet_LocalTags(Center,Tags) && Tags.IsEmpty());

            auto AlarmBlocker = Blocker::Get_BlockerWithTag(Board, Alarm);
            FCk_HexGridObject_Spec Shape;
            Shape.Set_FootprintOffsets({Origin});
            FCk_Handle_HexGridObject ChildObject;
            ECk_HexGridObject_Failure ObjectFailure{};
            if (!TestTrue(TEXT("Object owned by a dedicated blocker"),
                UCk_Utils_HexGridObject_UE::TryCreate(AlarmBlocker, Shape, ChildObject, ObjectFailure))) { return; }
            FCk_HexGridPlacement_View Bound;
            Bound.Set_PlacementId(FGuid::NewGuid()).Set_OccupantState(ECk_HexGridOccupantState::Bound)
                .Set_Occupant(ChildObject).Set_FootprintOffsets({Origin}).Set_Cells({Origin});
            auto Incompatible = Initial;
            Incompatible._Placements.Add(Bound);
            TestTrue(TEXT("Reject removing owner of a desired bound occupant before altering either half"),
                TryCommitProjection(Board, Incompatible).Get_Failure() == ECk_HexGridFailure::InvalidProjection
                && ReadCommittedProjection(Board)._Revision == RoundTrip._Revision
                && ck::IsValid(ChildObject) && Blocker::Get_BlockerWithTag(Board, Alarm) == AlarmBlocker
                && ReadCommittedProjection(Board)._Placements.IsEmpty());

            for (auto& View : RoundTrip._Blockers) { View._Active = false; }
            if (!TestTrue(TEXT("Release all contributor votes independently"),
                TryCommitProjection(Board, RoundTrip).Get_Succeeded() && Cell::TryGet_IsDisabled(Center, Disabled) && Disabled))
            { return; }
            RoundTrip._ManualBlocks.Reset();
            TestTrue(TEXT("Clearing manual vote frees cell"), TryCommitProjection(Board, RoundTrip).Get_Succeeded()
                && Cell::TryGet_IsDisabled(Center, Disabled) && !Disabled);
            TestTrue(TEXT("Restoring initial projection restores recipe tags and the original authored blocker"),
                TryCommitProjection(Board, Initial).Get_Succeeded() && Blocker::Get_Blockers(Board).Num() == 1
                && Stable == Blocker::Get_BlockerWithTag(Board, Door) && Blocker::Get_IsActive(Stable)
                && Cell::TryGet_LocalTags(Center,Tags) && Tags.HasTagExact(AuthoredTag)
                && ReadCommittedProjection(Board)._LocalTags.IsEmpty() && Cell::TryGet_IsDisabled(Center, Disabled) && Disabled);

            FCk_HexGridBlocker_Spec SiblingSpec;
            SiblingSpec.Set_Grid(Board).Set_Cells({Origin}).Set_InitiallyActive(false);
            FCk_Handle_HexGridBlocker Sibling;
            ECk_HexGridBlocker_Failure BlockerFailure{};
            if (!TestTrue(TEXT("Board and independently owned contributor are lifetime siblings"),
                Blocker::TryCreate(Parent, SiblingSpec, Sibling, BlockerFailure) &&
                Life::Get_LifetimeOwner(Sibling) == Life::Get_LifetimeOwner(Board))) { return; }
            const auto BeforeTransfer = ReadCommittedProjection(Board);
            auto RemoveAll = BeforeTransfer;
            RemoveAll._Blockers.Reset();
            auto Pivot = Board.Get<ck::FFragment_HexGrid>().Get_Pivot();
            const auto CheckProtected = [&](FCk_Handle Target, const TCHAR* Context)
            {
                const auto PreviousOwner = Life::Get_LifetimeOwner(Target);
                Life::Request_TransferLifetimeOwner(Target, Sibling);
                if (!TestTrue(Context, Life::Get_LifetimeOwner(Target) == Sibling)) { return; }
                const auto Children = Life::Get_LifetimeDependents(Sibling);
                const auto Result = TryCommitProjection(Board, RemoveAll);
                FVector WorldCenter;
                TestTrue(Context, Result.Get_Failure() == ECk_HexGridFailure::InvalidProjection &&
                    Grid::Get_IsReady(Board) && Blocker::Get_Blockers(Board).Num() == 2 &&
                    Blocker::Get_IsActive(Stable) && Life::Get_LifetimeDependents(Sibling) == Children &&
                    ReadCommittedProjection(Board)._Revision == BeforeTransfer._Revision &&
                    Grid::TryGet_CellWorldCenter(Board, Origin, WorldCenter, Failure));
                Life::Request_TransferLifetimeOwner(Target, PreviousOwner);
            };
            CheckProtected(Board, TEXT("Reject retiring the board's transferred lifetime owner"));
            CheckProtected(Pivot, TEXT("Reject retiring the spatial pivot's transferred lifetime owner"));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
