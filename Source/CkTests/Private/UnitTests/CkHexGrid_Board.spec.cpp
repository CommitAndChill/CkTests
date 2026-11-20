#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && WITH_ANGELSCRIPT_CK

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_EntityScript.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/Net/CkAutoTest_NetSubject_Utils.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"
#include "Misc/ScopeExit.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_hex_board_test
{
    using Grid = UCk_Utils_HexGrid_UE;
    using Cell = UCk_Utils_HexCell_UE;
    using Life = UCk_Utils_EntityLifetime_UE;
    using Fail = ECk_HexBoardFailure;
    using Space = ECk_HexGeometryFailure;
    using Coord = FCk_HexCoord;
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    UE_DEFINE_GAMEPLAY_TAG_STATIC(DefaultTag, "Ck.Test.HexBoard.Default");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(OverrideTag, "Ck.Test.HexBoard.Override");

    void Begin()
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    }

    FCk_Handle Owner(UWorld* World)
    {
        auto Handle = Life::Request_CreateEntity_TransientOwner(World);
        UCk_Utils_Net_UE::Add(Handle, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
            ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
        return Handle;
    }

    FCk_HexGrid_Spec Disk(int32 Radius)
    {
        FCk_HexDomain Domain;
        Domain.Set_RingRadius(Radius);
        FCk_HexGrid_Spec Spec;
        Spec.Set_Domain(Domain);
        return Spec;
    }

    FCk_HexGrid_Spec Mask(TArray<Coord> Coordinates)
    {
        FCk_HexDomain Domain;
        Domain.Set_Kind(ECk_HexDomainKind::Explicit);
        Domain.Set_Coordinates(Coordinates);
        FCk_HexGrid_Spec Spec;
        Spec.Set_Domain(Domain);
        return Spec;
    }

    FCk_Handle_HexGrid Make(FAutomationTestBase& T, FCk_Handle& Parent,
        const FCk_HexGrid_Spec& Spec, const FTransform& Transform = FTransform::Identity)
    {
        FCk_Handle_HexGrid Result;
        Fail Failure{};
        T.TestTrue(TEXT("Factory creates a board"), Grid::TryCreate(Parent, Transform, Spec, Result, Failure)
            && Failure == Fail::None && ck::IsValid(Result));
        return Result;
    }

    void DomainAndRegistry(FAutomationTestBase& T, FCk_Handle& Parent)
    {
        const Coord Directions[] = {{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}};
        Fail Failure{};
        FCk_Handle_HexCell First;
        for (int Radius = 0; Radius <= 2; ++Radius)
        {
            auto Board = Make(T, Parent, Disk(Radius));
            const auto Cells = Grid::Get_Cells(Board);
            T.TestEqual(TEXT("Expected disk cardinality"), Cells.Num(), 1 + 3 * Radius * (Radius + 1));
            int Index = 0;
            for (int R = -Radius; R <= Radius; ++R)
            {
                for (int Q = -Radius; Q <= Radius; ++Q)
                {
                    if (FMath::Abs(Q + R) > Radius) { continue; }
                    FCk_Handle_HexCell Found;
                    Coord Read;
                    T.TestTrue(TEXT("Stable row-major enumeration contains real readable cells"),
                        Cells.IsValidIndex(Index) && Grid::TryGet_CellAt(Board, {Q,R}, Found, Failure)
                        && Cells[Index] == Found && Cell::TryGet_Coordinate(Found, Read) && Read == Coord{Q,R});
                    ++Index;
                }
            }
            FCk_Handle_HexCell Origin;
            if (!T.TestTrue(TEXT("Origin present"), Grid::TryGet_CellAt(Board, {}, Origin, Failure))) { return; }
            if (Radius == 0) { First = Origin; }
            else
            {
                T.TestTrue(TEXT("Overlapping coordinates use distinct registry handles"), First != Origin);
                for (int I = 0; I < 6; ++I)
                {
                    FCk_Handle_HexCell Found;
                    Coord Read;
                    T.TestTrue(TEXT("Public neighbor direction matches literal delta"), Grid::TryGet_NeighborCell(
                        Board, {}, static_cast<ECk_HexDirection>(I), Found, Failure)
                        && Cell::TryGet_Coordinate(Found, Read) && Read == Directions[I]);
                }
            }
        }
        auto Board = Make(T, Parent, Mask({{2,0},{-1,-1},{1,0}}));
        const auto Cells = Grid::Get_Cells(Board);
        T.TestEqual(TEXT("Mask has exactly declared cells"), Cells.Num(), 3);
        auto Out = First;
        T.TestFalse(TEXT("Hole is absent"), Grid::TryGet_CellAt(Board, {}, Out, Failure));
        T.TestTrue(TEXT("Missing-cell output is cleared"), ck::Is_NOT_Valid(Out) && Failure == Fail::MissingCell);
        T.TestFalse(TEXT("Neighbor cannot pass through missing source"), Grid::TryGet_NeighborCell(
            Board, {}, static_cast<ECk_HexDirection>(0), Out, Failure));
        T.TestFalse(TEXT("Missing neighbor rejected"), Grid::TryGet_NeighborCell(
            Board, {2,0}, static_cast<ECk_HexDirection>(0), Out, Failure));
        T.TestFalse(TEXT("Invalid direction rejected"), Grid::TryGet_NeighborCell(
            Board, {1,0}, static_cast<ECk_HexDirection>(99), Out, Failure));
        T.TestTrue(TEXT("Invalid direction reason"), Failure == Fail::InvalidDirection);
        Board = Make(T, Parent, Mask({{MAX_int32,0}}));
        T.TestFalse(TEXT("Neighbor beyond int32 rejected"), Grid::TryGet_NeighborCell(
            Board, {MAX_int32,0}, static_cast<ECk_HexDirection>(0), Out, Failure));
        T.TestTrue(TEXT("Coordinate overflow reason"), Failure == Fail::CoordinateOverflow);
    }

    void FactoryAtomicity(FAutomationTestBase& T, FCk_Handle& Parent)
    {
        auto Transform = UCk_Utils_Transform_UE::Add(Parent, FTransform::Identity, ECk_Replication::DoesNotReplicate);
        const auto Before = Life::Get_LifetimeDependents(Parent);
        auto Spec = Disk(1);
        auto CheckReject = [&](const FCk_HexGrid_Spec& Bad, Fail Reason)
        {
            Fail Failure{};
            FCk_Handle_HexGrid Result;
            T.TestTrue(TEXT("TryAdd rejected before publish"),
                !Grid::TryAdd(Transform, Bad, Result, Failure) && ck::Is_NOT_Valid(Result) && Failure == Reason);
            T.TestTrue(TEXT("TryCreate rejected before publish"),
                !Grid::TryCreate(Parent, FTransform::Identity, Bad, Result, Failure)
                && ck::Is_NOT_Valid(Result) && Failure == Reason);
            T.TestTrue(TEXT("No grid, pivot or child leaked; pre-existing features survive"),
                !Grid::Has(Parent) && UCk_Utils_Transform_UE::Has(Parent) && UCk_Utils_Net_UE::Has(Parent)
                && Before == Life::Get_LifetimeDependents(Parent));
        };
        Spec.Set_InitialDisabledCells({{0,0},{4,4}});
        CheckReject(Spec, Fail::InvalidSpec);
        Spec = Disk(1);
        const FCk_HexCellTagOverride Good{{0,0},{}};
        Spec.Set_PerCellTags({Good,FCk_HexCellTagOverride{{4,4},{}}});
        CheckReject(Spec, Fail::InvalidSpec);
        Spec.Set_PerCellTags({Good,Good});
        CheckReject(Spec, Fail::InvalidSpec);
        Spec = Disk(-1);
        CheckReject(Spec, Fail::InvalidSpec);
        Spec = Disk(0);
        Spec.Set_Layout(FCk_HexLayout{ECk_HexOrientation::Pointy,0.0});
        CheckReject(Spec, Fail::InvalidSpec);
        CheckReject(Disk(200), Fail::CapacityExceeded);
        Spec = Disk(0);
        Spec.Set_Pivot(FTransform{FQuat::Identity,FVector::ZeroVector,FVector{1,2,1}});
        CheckReject(Spec, Fail::UnsupportedTransform);
        {
            ck::registry_table::FScopedAllocationCapacityForTest Capacity(1);
            ck::FEcsWorld::ECreateResult WorldResult{};
            auto Leased = ck::FEcsWorld::TryCreate(WorldResult);
            T.TestTrue(TEXT("One actual registry slot leased"), Leased.IsValid());
            CheckReject(Disk(0), Fail::CapacityExceeded);
            Leased.Reset();
            const auto Reacquired = ck::FEcsWorld::TryCreate(WorldResult);
            T.TestTrue(TEXT("Freed capacity immediately admits another world"), Reacquired.IsValid());
        }
        FCk_Handle_HexGrid Reused;
        Fail Failure{};
        {
            bool ObservedPivot = false;
            bool NestedSucceeded = false;
            Fail NestedFailure{};
            FCk_Handle_HexGrid NestedGrid;
            const auto Listener = UCk_Utils_Transform_UE::Get_OnAdded().AddLambda(
                [&](const FCk_Handle_Transform& Added)
                {
                    if (ObservedPivot || Life::Get_LifetimeOwner(Added) != Parent) { return; }
                    ObservedPivot = true;
                    NestedSucceeded = Grid::TryAdd(Transform, Disk(1), NestedGrid, NestedFailure);
                });
            ON_SCOPE_EXIT { UCk_Utils_Transform_UE::Get_OnAdded().Remove(Listener); };
            T.TestTrue(TEXT("TryAdd can retry on original owner"),
                Grid::TryAdd(Transform, Disk(0), Reused, Failure) && Reused == Parent);
            T.TestTrue(TEXT("Transform-added observer cannot publish a competing grid"),
                ObservedPivot && !NestedSucceeded && NestedFailure == Fail::AlreadyComposed
                && ck::Is_NOT_Valid(NestedGrid) && Grid::Get_Cells(Reused).Num() == 1);
            T.TestEqual(TEXT("Only the accepted board published a pivot"),
                Life::Get_LifetimeDependents(Parent).Num(), Before.Num() + 1);
        }
        FCk_Handle_HexGrid Duplicate;
        T.TestTrue(TEXT("Already composed owner cannot be reconfigured"), !Grid::TryAdd(
            Transform, Disk(1), Duplicate, Failure) && Failure == Fail::AlreadyComposed && ck::Is_NOT_Valid(Duplicate)
            && Grid::Get_Cells(Reused).Num() == 1);
        auto Child = Make(T, Parent, Disk(0));
        T.TestTrue(TEXT("TryCreate makes distinct child"), Child != Parent && Life::Get_LifetimeOwner(Child) == Parent);
    }

    void CheckPayload(FAutomationTestBase& T, FCk_Handle_HexGrid Board)
    {
        FCk_Handle_HexCell Found;
        FGameplayTagContainer Tags;
        bool Disabled = false;
        Fail Failure{};
        for (const auto CoordValue : {Coord{}, Coord{1,0}})
        {
            if (!T.TestTrue(TEXT("Lookup authored coordinate"), Grid::TryGet_CellAt(Board, CoordValue, Found, Failure)))
            { return; }
            T.TestTrue(TEXT("Default tags merge with overrides"), Cell::TryGet_Tags(Found, Tags)
                && Tags.HasTagExact(DefaultTag) && Tags.HasTagExact(OverrideTag) == (CoordValue == Coord{}));
            T.TestTrue(TEXT("Initial disabled cells match authoring"), Cell::TryGet_IsDisabled(Found, Disabled)
                && Disabled == (CoordValue == Coord{}));
        }
    }
}

namespace ck_hex_board_test
{
    void PickingAndOutline(FAutomationTestBase& T, FCk_Handle& Parent)
    {
        const Coord Ring[] = {{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}};
        const auto Sqrt3 = FMath::Sqrt(3.0);
        const auto Pose = FTransform{FRotator{18,47,-9}, FVector{300,53,-77}, FVector{2,2,2}};
        for (auto Orientation : {ECk_HexOrientation::Flat, ECk_HexOrientation::Pointy})
        {
            const bool Flat = Orientation == ECk_HexOrientation::Flat;
            auto Spec = Disk(2);
            Spec.Set_Layout(FCk_HexLayout{Orientation,100.0});
            const auto Board = Make(T, Parent, Spec, Pose);
            FVector Center;
            TArray<FVector> Corners;
            FBox Bounds{ForceInit};
            Fail Failure{};
            if (!T.TestTrue(TEXT("World center, six corners and bounds accessible"), Grid::TryGet_CellWorldCenter(
                Board, {}, Center, Failure) && Center.Equals(Pose.GetTranslation(), 1.e-6)
                && Grid::TryGet_CellWorldCorners(Board, {}, Corners, Failure) && Corners.Num() == 6
                && Grid::TryGet_CellWorldBounds(Board, {}, Bounds, Failure)))
            { return; }
            FBox ExpectedBounds{ForceInit};
            const auto ExpectedCenter = [Flat,Sqrt3](Coord C)
            {
                const double Q = C.Get_Q();
                const double R = C.Get_R();
                return Flat ? FVector{150*Q,100*Sqrt3*(R+Q/2),0} : FVector{100*Sqrt3*(Q+R/2),150*R,0};
            };
            const auto CheckPick = [&](FVector Local)
            {
                auto Best = Coord{};
                auto Min = Local.Size();
                for (auto C : Ring)
                {
                    const auto D = FVector::Distance(Local, ExpectedCenter(C));
                    if (D < Min - 1.e-8 || (FMath::IsNearlyEqual(D, Min, 1.e-8)
                        && (C.Get_Q() < Best.Get_Q() || (C.Get_Q() == Best.Get_Q() && C.Get_R() < Best.Get_R()))))
                    { Min = D; Best = C; }
                }
                auto Spatial = Space::None;
                FCk_Handle_HexCell PointCell;
                FCk_Handle_HexCell RayCell;
                Coord Read;
                FVector Hit;
                const auto World = Pose.TransformPosition(Local);
                const auto Normal = Pose.GetRotation().GetAxisZ();
                T.TestTrue(TEXT("Independent polygon tie oracle agrees with world point and ray"), Grid::TryGet_CellFromWorldPoint(
                    Board, World, PointCell, Spatial, Failure) && Cell::TryGet_Coordinate(PointCell, Read) && Read == Best
                    && Grid::TryGet_CellFromWorldRay(Board, World + 100*Normal, -3*Normal, RayCell, Hit, Spatial, Failure)
                    && RayCell == PointCell && Hit.Equals(World, 1.e-6));
            };
            for (int I = 0; I < 6; ++I)
            {
                const auto Angle = FMath::DegreesToRadians(60.0*I + (Flat ? 0.0 : 30.0));
                const FVector Vertex{100*FMath::Cos(Angle),100*FMath::Sin(Angle),0};
                const auto World = Pose.TransformPosition(Vertex);
                ExpectedBounds += World;
                T.TestTrue(TEXT("Outline includes independent rotated vertex"),
                    Corners.ContainsByPredicate([World](const FVector& V){ return V.Equals(World,1.e-6); }));
                CheckPick(Vertex);
                const auto Edge = ExpectedCenter(Ring[I]) / 2;
                CheckPick(Edge);
                CheckPick(Edge * 0.999);
                CheckPick(Edge * 1.001);
            }
            T.TestTrue(TEXT("World AABB equals extrema of transformed vertices"), Bounds.Min.Equals(ExpectedBounds.Min, 1.e-6)
                && Bounds.Max.Equals(ExpectedBounds.Max, 1.e-6));
            FCk_Handle_HexCell CellOut;
            FVector Hit = FVector::OneVector;
            auto Spatial = Space::None;
            T.TestTrue(TEXT("Off-plane point retains geometry rejection"), !Grid::TryGet_CellFromWorldPoint(
                Board, Pose.TransformPosition(FVector{0,0,1}), CellOut, Spatial, Failure)
                && Failure == Fail::SpatialQueryFailed && Spatial == Space::OffPlane);
            T.TestTrue(TEXT("Parallel ray resets hit"), !Grid::TryGet_CellFromWorldRay(Board, Center,
                Pose.GetRotation().GetAxisX(), CellOut, Hit, Spatial, Failure) && Hit.IsZero() && Spatial == Space::ParallelRay);
            auto HoleSpec = Mask({{1,0}});
            HoleSpec.Set_Layout(Spec.Get_Layout());
            const auto Hole = Make(T, Parent, HoleSpec, Pose);
            T.TestTrue(TEXT("Missing lexicographic winner is a miss even with tied member"), !Grid::TryGet_CellFromWorldPoint(
                Hole, Pose.TransformPosition(ExpectedCenter(Ring[0])/2), CellOut, Spatial, Failure)
                && ck::Is_NOT_Valid(CellOut) && Failure == Fail::MissingCell && Spatial == Space::OutsideDomain);
        }
    }

    void QueueWorldRow(FAutomationTestBase& T, void (*Check)(FAutomationTestBase&, FCk_Handle&))
    {
        Begin();
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [&T, Check](UWorld* World){ auto Parent = Owner(World); Check(T, Parent); })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexBoardDomain, "Ck.HexGrid.Board.DomainAndRegistry", ck_hex_board_test::Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexBoardAtomic, "Ck.HexGrid.Board.FactoryAtomicity", ck_hex_board_test::Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexBoardPicking, "Ck.HexGrid.Board.PickingAndOutline", ck_hex_board_test::Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexBoardAuthoring, "Ck.HexGrid.Board.AuthoringProductionPath", ck_hex_board_test::Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexBoardOwnership, "Ck.HexGrid.Board.OwnershipAndTeardown", ck_hex_board_test::Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexBoardSurfaces, "Ck.HexGrid.Board.ThreeSurfaceFixture", ck_hex_board_test::Flags)

bool FCkHexBoardDomain::RunTest(const FString&)
{
    ck_hex_board_test::QueueWorldRow(*this, ck_hex_board_test::DomainAndRegistry);
    return true;
}

bool FCkHexBoardAtomic::RunTest(const FString&)
{
    using namespace ck_hex_board_test;
    struct FInterruptedAdd
    {
        FCk_Handle Parent;
        FCk_Handle Pivot;
    };
    auto Scenario = MakeShared<FInterruptedAdd>();
    Begin();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld* World)
        {
            auto Parent = Owner(World);
            FactoryAtomicity(*this, Parent);
            Scenario->Parent = Owner(World);
            auto Transform = UCk_Utils_Transform_UE::Add(Scenario->Parent, FTransform::Identity,
                ECk_Replication::DoesNotReplicate);
            bool SawPivot = false;
            const auto Observer = UCk_Utils_Transform_UE::Get_OnAdded().AddLambda(
                [&](const FCk_Handle_Transform& Added)
                {
                    if (SawPivot || Life::Get_LifetimeOwner(Added) != Scenario->Parent) { return; }
                    SawPivot = true;
                    Scenario->Pivot = Added;
                    Life::Request_DestroyEntity(Scenario->Pivot);
                });
            ON_SCOPE_EXIT { UCk_Utils_Transform_UE::Get_OnAdded().Remove(Observer); };
            FCk_Handle_HexGrid Result;
            Fail Failure{};
            const auto Succeeded = Grid::TryAdd(Transform, Disk(1), Result, Failure);
            TestTrue(TEXT("Pivot destroyed by observer prevents publication"), SawPivot && !Succeeded
                && Failure == Fail::CompositionFailed && ck::Is_NOT_Valid(Result) && !Grid::Has(Scenario->Parent));
            TestTrue(TEXT("Interrupted TryAdd preserves caller-owned features"),
                UCk_Utils_Transform_UE::Has(Scenario->Parent) && UCk_Utils_Net_UE::Has(Scenario->Parent));

            auto RetiringParent = Owner(World);
            Life::Request_DestroyEntity(RetiringParent);
            TestTrue(TEXT("BeginDestroy rejects a new child before ordinary handle invalidation"),
                ck::IsValid(RetiringParent) && !Grid::TryCreate(RetiringParent, FTransform::Identity, Disk(0), Result, Failure)
                && Failure == Fail::InvalidHandle && ck::Is_NOT_Valid(Result)
                && Life::Get_LifetimeDependents(RetiringParent).IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Scenario]()
        {
            return ck::IsValid(Scenario->Parent)
                && ck::Is_NOT_Valid(Scenario->Pivot, ck::IsValid_Policy_IncludePendingKill{})
                && Life::Get_LifetimeDependents(Scenario->Parent).IsEmpty();
        }), 10.f, TEXT("Rejected board's pivot lease retires through normal lifetime processing")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

bool FCkHexBoardPicking::RunTest(const FString&)
{
    using namespace ck_hex_board_test;
    Begin();
    auto Board = MakeShared<FCk_Handle_HexGrid>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this,Board](UWorld* World)
        {
            auto Parent = Owner(World);
            PickingAndOutline(*this, Parent);
            *Board = Make(*this, Parent, Disk(1));
            auto Transform = UCk_Utils_Transform_UE::Cast(*Board);
            UCk_Utils_Transform_UE::Request_SetScale(Transform, FCk_Request_Transform_SetScale{FVector{1,2,1}}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Board]()
        {
            Fail Failure{};
            FVector Center;
            return !Grid::TryGet_CellWorldCenter(*Board, {}, Center, Failure) && Failure == Fail::UnsupportedTransform;
        }), 5.f, TEXT("Spatial read observes nonuniform scale applied to the live parent")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this,Board](UWorld*)
        {
            Fail Failure{};
            FCk_Handle_HexCell Found;
            TestTrue(TEXT("Unsupported transform leaves integer membership intact"),
                Grid::TryGet_CellAt(*Board, {}, Found, Failure) && ck::IsValid(Found));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

bool FCkHexBoardAuthoring::RunTest(const FString&)
{
    using namespace ck_hex_board_test;
    struct FScenario
    {
        TStrongObjectPtr<UCk_HexGrid_AuthoringSpec> Asset;
        TStrongObjectPtr<UCk_AutoTest_BodyCapturer_UE> Capture;
    };
    auto Scenario = MakeShared<FScenario>();
    Begin();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this,Scenario](UWorld* World)
        {
            auto Parent = Owner(World);
            auto Spec = Disk(1);
            Spec.Set_DefaultCellTags(FGameplayTagContainer{DefaultTag});
            Spec.Set_PerCellTags({FCk_HexCellTagOverride{{},FGameplayTagContainer{OverrideTag}}});
            Spec.Set_InitialDisabledCells({{}});
            CheckPayload(*this, Make(*this, Parent, Spec));
            Scenario->Asset.Reset(NewObject<UCk_HexGrid_AuthoringSpec>());
            Scenario->Asset->Set_Spec(Spec);
            Scenario->Capture.Reset(NewObject<UCk_AutoTest_BodyCapturer_UE>());
            const FCk_HexGrid_SpawnParams Payload{Scenario->Asset.Get(), FTransform{FVector{300,200,50}}};
            auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(Parent, UCk_HexGrid_EntityScript::StaticClass(),
                FInstancedStruct::Make(Payload), {});
            if (!TestTrue(TEXT("Real EntityScript spawn admitted"),
                UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending))) { return; }
            FCk_Delegate_EntityScript_Constructed OnConstructed;
            OnConstructed.BindDynamic(Scenario->Capture.Get(), &UCk_AutoTest_BodyCapturer_UE::OnBodyConstructed);
            UCk_Utils_PendingEntityScript_UE::Promise_OnConstructed(Pending, OnConstructed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Scenario](){ return Scenario->Capture.IsValid() && Scenario->Capture->_CapturedBodies.Num() == 1; }),
        10.f, TEXT("Authored EntityScript completed and posted its constructed event")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this,Scenario](UWorld*)
        {
            if (!Scenario->Capture.IsValid() || Scenario->Capture->_CapturedBodies.Num() != 1) { return; }
            auto Body = Scenario->Capture->_CapturedBodies[0];
            auto* Script = Cast<UCk_HexGrid_EntityScript>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(Body));
            if (!TestTrue(TEXT("Construction explicitly succeeded"), Script && Script->Get_BoardFailure() == Fail::None
                && Grid::Has(Body))) { return; }
            const auto Board = Grid::Cast(Body);
            CheckPayload(*this, Board);
            Fail Failure{};
            FVector Center;
            TestTrue(TEXT("Spawn transform reached actual board"), Grid::TryGet_CellWorldCenter(Board, {}, Center, Failure)
                && Center.Equals(FVector{300,200,50}, 1.e-6));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

bool FCkHexBoardOwnership::RunTest(const FString&)
{
    using namespace ck_hex_board_test;
    struct FScenario { FCk_Handle OwnerHandle; FCk_Handle_HexGrid Board; FCk_Handle_HexCell Retained; };
    auto Scenario = MakeShared<FScenario>();
    Begin();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this,Scenario](UWorld* World)
        {
            Scenario->OwnerHandle = Owner(World);
            Scenario->Board = Make(*this, Scenario->OwnerHandle, Mask({{-17,21}}));
            Fail Failure{};
            FCk_Handle_HexGrid OwnerRead;
            Coord Read;
            TestTrue(TEXT("Stored identity and owner read from a sparse negative coordinate"),
                Grid::TryGet_CellAt(Scenario->Board, {-17,21}, Scenario->Retained, Failure)
                && Cell::TryGet_Coordinate(Scenario->Retained, Read) && Read == Coord{-17,21}
                && Cell::TryGet_Grid(Scenario->Retained, OwnerRead) && OwnerRead == Scenario->Board);
            auto Copy = Scenario->Board;
            Life::Request_DestroyEntity(Copy);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Scenario](){ return ck::Is_NOT_Valid(Scenario->Board) && ck::Is_NOT_Valid(Scenario->Retained); }),
        10.f, TEXT("Board and captured cell become stale after lifetime pumping")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this,Scenario](UWorld*)
        {
            const auto New = Make(*this, Scenario->OwnerHandle, Mask({{-17,21}}));
            FCk_Handle_HexCell Fresh;
            Fail Failure{};
            TestTrue(TEXT("Recreated board cannot resurrect old cell"), Grid::TryGet_CellAt(New, {-17,21}, Fresh, Failure)
                && Fresh != Scenario->Retained && ck::Is_NOT_Valid(Scenario->Retained));
            auto Saved = Coord{99,-77};
            TestTrue(TEXT("Stale read clears coordinate"), !Cell::TryGet_Coordinate(Scenario->Retained, Saved) && Saved == Coord{});
            TestTrue(TEXT("Stale registry query safely returns empty"), Grid::Get_Cells(Scenario->Board).IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

bool FCkHexBoardSurfaces::RunTest(const FString&)
{
    ck_hex_board_test::Begin();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunAsTestOnAllWorlds(this, TEXT("/Script/Angelscript.Ck_HexGrid_BoardFixture"), 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
