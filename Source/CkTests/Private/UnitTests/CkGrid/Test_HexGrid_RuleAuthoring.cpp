#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_Spec.h"
#include "CkGrid/HexGridSystem/Blocker/CkHexGridBlocker_Utils.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkLabel/CkLabel_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "NativeGameplayTags.h"

namespace ck_hex_rule_authoring_test
{
    using Grid = UCk_Utils_HexGrid_UE;
    using Blocker = UCk_Utils_HexGridBlocker_UE;
    using Life = UCk_Utils_EntityLifetime_UE;
    using Transform = UCk_Utils_Transform_UE;
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Door, "Ck.Test.HexRules.Door");

    UE_DEFINE_GAMEPLAY_TAG_STATIC(Gate, "Ck.Test.HexRules.Gate");

    auto Begin() -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    }

    auto Owner(UWorld* InWorld) -> FCk_Handle
    {
        auto Handle = Life::Request_CreateEntity_TransientOwner(InWorld);
        UCk_Utils_Net_UE::Add(Handle, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
            ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
        return Handle;
    }

    auto Declaration(TArray<FCk_HexCoord> InCells, FGameplayTag InName = {}, bool InActive = true)
        -> FCk_HexGridAuthoredBlocker
    {
        return FCk_HexGridAuthoredBlocker{MoveTemp(InCells), InName, InActive};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexRules_AuthoredAtomicity, "Ck.HexGrid.Rules.AuthoredAtomicity",
    ck_hex_rule_authoring_test::Flags)

auto
    FCkHexRules_AuthoredAtomicity::
    RunTest(const FString&) -> bool
{
    using namespace ck_hex_rule_authoring_test;
    Begin();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this](UWorld* World)
        {
            auto Parent = Owner(World);
            auto OwnerTransform = Transform::Add(Parent, FTransform::Identity, ECk_Replication::DoesNotReplicate);
            const auto Before = Life::Get_LifetimeDependents(Parent);
            int32 Notifications = 0;
            const auto Listener = Transform::Get_OnAdded().AddLambda(
                [&](const FCk_Handle_Transform&) { ++Notifications; });
            ON_SCOPE_EXIT { Transform::Get_OnAdded().Remove(Listener); };
            auto CheckRejected = [&](const FCk_HexGrid_Spec& Spec)
            {
                auto* Asset = NewObject<UCk_HexGrid_AuthoringSpec>();
                Asset->Set_Spec(Spec);
                FCk_HexGrid_Spec Resolved;
                ECk_HexBoardFailure Failure{};
                TestTrue(TEXT("Authoring asset rejects complete malformed declaration"),
                    !Asset->TryResolve(Resolved, Failure) && Failure == ECk_HexBoardFailure::InvalidSpec);
                FCk_Handle_HexGrid Board;
                TestTrue(TEXT("Composition on owner fails atomically"), !Grid::TryAdd(OwnerTransform, Spec, Board, Failure)
                    && Failure == ECk_HexBoardFailure::InvalidSpec && ck::Is_NOT_Valid(Board));
                TestTrue(TEXT("Child factory fails before allocating children"), !Grid::TryCreate(Parent,
                    FTransform::Identity, Spec, Board, Failure) && Failure == ECk_HexBoardFailure::InvalidSpec
                    && ck::Is_NOT_Valid(Board));
                TestTrue(TEXT("Unchanged owner, leases and notification count"), !Grid::Has(Parent)
                    && Transform::Has(Parent) && Before == Life::Get_LifetimeDependents(Parent) && Notifications == 0);
                TestTrue(TEXT("No published cells or blocker records"), Grid::Get_Cells(Board).IsEmpty()
                    && Blocker::Get_Blockers(Board).IsEmpty());
            };
            const auto Valid = Declaration({{0,0}}, Door);
            FCk_HexGrid_Spec Spec;
            Spec.Set_Blockers({Valid, Declaration({{0,0},{3,-2}})});
            CheckRejected(Spec);
            Spec.Set_Blockers({Valid, Declaration({{0,0}}, Door)});
            CheckRejected(Spec);
            Spec.Set_Blockers({Valid, Declaration({{0,0},{0,0}})});
            CheckRejected(Spec);
            Spec.Set_Blockers({Valid, Declaration({})});
            CheckRejected(Spec);
            Spec.Set_Blockers({Valid});
            Spec.Set_InitialDisabledCells({{0,0},{1,0}});
            CheckRejected(Spec);
            Spec.Set_InitialDisabledCells({});
            Spec.Set_PerCellTags({FCk_HexCellTagOverride{{0,0},{}}, FCk_HexCellTagOverride{{1,0},{}}});
            CheckRejected(Spec);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexRules_NamedQueries, "Ck.HexGrid.Rules.NamedQueries",
    ck_hex_rule_authoring_test::Flags)

auto
    FCkHexRules_NamedQueries::
    RunTest(const FString&) -> bool
{
    using namespace ck_hex_rule_authoring_test;
    struct FScenario { FCk_Handle_HexGrid Board; FCk_Handle_HexGridBlocker Named; };
    auto Scenario = MakeShared<FScenario>();
    Begin();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld* World)
        {
            auto Parent = Owner(World);
            FCk_HexGrid_Spec Spec;
            Spec.Set_Blockers({Declaration({{0,0}}, Door), Declaration({{0,0}}, {}, false), Declaration({{0,0}}, {}, false)});
            ECk_HexBoardFailure Failure{};
            auto& Board = Scenario->Board;
            if (!TestTrue(TEXT("Board publishes named and anonymous blockers together"),
                Grid::TryCreate(Parent, FTransform::Identity, Spec, Board, Failure)))
            { return; }
            const auto Entries = Blocker::Get_Blockers(Board);
            Scenario->Named = Blocker::Get_BlockerWithTag(Board, Door);
            TestTrue(TEXT("Distinct anonymous blockers are included without hijacking label lookup"),
                Entries.Num() == 3 && Entries.Contains(Scenario->Named) && ck::IsValid(Scenario->Named)
                && ck::Is_NOT_Valid(Blocker::Get_BlockerWithTag(Board, FGameplayTag{})));
            FCk_Handle_HexCell Cell;
            bool Disabled = false;
            TestTrue(TEXT("Authored activation is already committed on return"), Blocker::Get_IsActive(Scenario->Named)
                && Grid::TryGet_CellAt(Board, {}, Cell, Failure)
                && UCk_Utils_HexCell_UE::TryGet_IsDisabled(Cell, Disabled) && Disabled);

            FCk_HexGridBlocker_Spec Duplicate;
            Duplicate.Set_Grid(Board);
            Duplicate.Set_Cells({{0,0}});
            Duplicate.Set_Name(Door);
            FCk_Handle_HexGridBlocker Rejected;
            ECk_HexGridBlocker_Failure BlockerFailure{};
            const auto Leases = Life::Get_LifetimeDependents(Parent);
            TestTrue(TEXT("Duplicate runtime name preserves original lookup and owner leases"),
                !Blocker::TryCreate(Parent, Duplicate, Rejected, BlockerFailure)
                && BlockerFailure == ECk_HexGridBlocker_Failure::DuplicateName && ck::Is_NOT_Valid(Rejected)
                && Scenario->Named == Blocker::Get_BlockerWithTag(Board, Door)
                && Blocker::Get_Blockers(Board).Num() == 3 && Leases == Life::Get_LifetimeDependents(Parent));
            // An entity's unrelated gameplay label must survive TryAdd and must never name an anonymous contributor.
            auto Labeled = Life::Request_CreateEntity(Parent);
            UCk_Utils_GameplayLabel_UE::Add(Labeled, Door);
            Duplicate.Set_Name(Gate);
            TestTrue(TEXT("Conflicting set-once label rejects before contributor composition"),
                !Blocker::TryAdd(Labeled, Duplicate, Rejected, BlockerFailure)
                && BlockerFailure == ECk_HexGridBlocker_Failure::InvalidOwner && ck::Is_NOT_Valid(Rejected)
                && !Blocker::Has(Labeled) && UCk_Utils_GameplayLabel_UE::Get_Label(Labeled) == Door
                && Blocker::Get_Blockers(Board).Num() == 3);
            Duplicate.Set_Name({});
            Duplicate.Set_InitiallyActive(false);
            TestTrue(TEXT("Anonymous contributor permits unrelated entity label"),
                Blocker::TryAdd(Labeled, Duplicate, Rejected, BlockerFailure)
                && ck::IsValid(Rejected) && Blocker::Get_Blockers(Board).Num() == 4);
            auto Dying = FCk_Handle{Scenario->Named};
            Life::Request_DestroyEntity(Dying);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Scenario](){ return ck::Is_NOT_Valid(Scenario->Named); }), 10.f, TEXT("named blocker retires")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld*)
        {
            TestTrue(TEXT("Retired label gone and anonymous entries retained"),
                ck::Is_NOT_Valid(Blocker::Get_BlockerWithTag(Scenario->Board, Door)) &&
                Blocker::Get_Blockers(Scenario->Board).Num() == 3);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
