#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkCompassGym_SourceCatalog.h"
#include "CkCompassGym_SourceMembership.h"
#include "CkCompassGym_PingTtlProbe.h"
#include "../../CkCompassGym_SourceMembership_Private.h"
#include "CkCompass/CkCompass_Utils.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkPoi/CkPoi_Utils.h"
#include "CkPoi/CkPoi_Fragment.h"
#include "CkPoiDisplayDefinition/CkPoiDisplayDefinition_Utils.h"
#include "CkPmg/CkPmg_Utils_BasicShapes.h"
#include "CkPmg/CkPmg_Fragment.h"
#include "CkPmg/CkPmg_Fragment_BasicShapes.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTagContainer.h"
#include "ProceduralMeshComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_compass_catalog_test
{
    struct FRun
    {
        FCk_Handle WorldRoot;
        FCk_Handle SourceRoot;
        FCk_Handle ExtraChild;
        FCk_Handle LostMarker;
        FCk_Handle BadDurationMarker;
        FCk_Handle RetiredHost;
        FCk_Handle RootBoundHost;
        TWeakObjectPtr<AActor> RootBoundActor;
        FCk_Handle PingHost;
        FCk_Handle_Timer PingTimer;
        FCkCompassGym_SourceReceipt FrozenPing;
        FCk_Handle RetiringPingExtraChild;
        FCk_Handle NaturalPingHost;
        FCk_Handle_Timer NaturalPingTimer;
        TStrongObjectPtr<UCkCompassGym_PingTtlProbe> NaturalPingProbe;
        FCk_Delegate_Timer NaturalPingDoneDelegate;
        bool NaturalPingSetupFailed = false;
        ECkCompassGym_CatalogOutcome NaturalPingOutcome =
            ECkCompassGym_CatalogOutcome::Pending;
        ECkCompassGym_CatalogOutcome PingOutcome =
            ECkCompassGym_CatalogOutcome::Pending;
        TArray<FCkCompassGym_SourceRow> PingRows;
        FString PingFailure;
        TArray<FCkCompassGym_SourceReceipt> Receipts;
        int64 NextCatalogId = 1;
        TArray<FCkCompassGym_SourceReceipt> OriginalNine;
        TArray<FCkCompassGym_SourceReceipt> StagedStress;
        TArray<FCkCompassGym_SourceReceipt> RetiringStress;
        TArray<FCkCompassGym_SourceRow> PhaseActiveRows;
        TArray<FCkCompassGym_SourceRow> PhaseStagedRows;
        FString PhaseFailure;
        ECkCompassGym_CatalogOutcome PhaseOutcome =
            ECkCompassGym_CatalogOutcome::Pending;
        FCk_Handle UnexpectedStressChild;
        FCk_Handle CompassObserver;
        FCk_Handle_Compass Compass;
        bool Dedicated = false;
        bool AdmissionPartialProjection = false;
        bool WithdrawalPartialProjection = false;
        ECkCompassGym_MembershipResult MembershipResult =
            ECkCompassGym_MembershipResult::Unavailable;
        TArray<FCkCompassGym_SourceRow> CommittedRows;
        TArray<FCkCompassGym_SourceReceipt> CommittedRetirees;
        FString MembershipFailure;
        TWeakObjectPtr<AGameStateBase> GameState;
        FCk_Handle_PoiDisplayDefinition WaypointDisplay;
        ECkCompassGym_CatalogOutcome LastOutcome = ECkCompassGym_CatalogOutcome::Pending;
        FString LastFailure;
        TArray<FCkCompassGym_SourceRow> LastRows;
        bool Built = false;
    };

    auto Allocated(const FCk_Handle& Handle) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle); }

    auto Tag(const TCHAR* Name) -> FGameplayTag
    { return FGameplayTag::RequestGameplayTag(FName(Name), false); }

    auto Color(const ECkCompassGym_SourceKind Kind, const FGameplayTag& Category)
        -> FLinearColor
    {
        if (Kind == ECkCompassGym_SourceKind::Waypoint)
        { return FLinearColor(0.7f, 0.35f, 0.95f); }
        if (Kind == ECkCompassGym_SourceKind::Ping)
        { return FLinearColor(0.3f, 0.9f, 0.95f); }
        const auto Name = Category.GetTagName();
        if (Name == TEXT("Poi.Category.Quest"))
        { return FLinearColor(1.0f, 0.78f, 0.25f); }
        if (Name == TEXT("Poi.Category.Shop"))
        { return FLinearColor(0.95f, 0.5f, 0.2f); }
        if (Name == TEXT("Poi.Category.Danger"))
        { return FLinearColor(0.9f, 0.25f, 0.25f); }
        return FLinearColor(0.35f, 0.6f, 0.95f);
    }

    auto AttachDisplay(FCk_Handle& Host, int32 Priority,
        ECk_Poi_OffscreenPolicy Policy,
        FVector2D SizeHint = FVector2D(32, 32))
        -> FCk_Handle_PoiDisplayDefinition
    {
        auto Spec = FCk_PoiDisplayDefinition_Spec(Tag(TEXT("Poi.Consumer.Compass")));
        Spec.Set_Priority(Priority).Set_OffscreenPolicy(Policy)
            .Set_SizeHint(SizeHint);
        return UCk_Utils_PoiDisplayDefinition_UE::Add(Host, Spec);
    }

    auto AddPoi(FRun& Run, ECkCompassGym_SourceKind Kind,
        const TCHAR* CategoryName, const FVector& Location, int32 Priority,
        bool WithDisplay) -> bool
    {
        FCkCompassGym_SourceReceipt Receipt;
        Receipt.CatalogId = Run.NextCatalogId++;
        Receipt.Kind = Kind;
        Receipt.ExpectedCategory = Tag(CategoryName);
        if (!Receipt.ExpectedCategory.IsValid()) { return false; }
        Receipt.Host = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run.SourceRoot);
        if (!Allocated(Receipt.Host)) { return false; }
        UCk_Utils_Transform_UE::Add(Receipt.Host,
            FTransform(FRotator::ZeroRotator, Location),
            ECk_Replication::DoesNotReplicate);
        const auto Poi = UCk_Utils_Poi_UE::Add(
            Receipt.Host, FCk_Poi_Spec(Receipt.ExpectedCategory));
        if (!Allocated(Poi)) { return false; }
        if (WithDisplay && !Allocated(AttachDisplay(Receipt.Host, Priority,
            (Kind == ECkCompassGym_SourceKind::Waypoint ||
             Kind == ECkCompassGym_SourceKind::Ping)
                ? ECk_Poi_OffscreenPolicy::ClampToEdge
                : ECk_Poi_OffscreenPolicy::Hide)))
        { return false; }
        const float Radius = Kind == ECkCompassGym_SourceKind::Waypoint ? 55.0f :
            Kind == ECkCompassGym_SourceKind::Ping ? 45.0f : 40.0f;
        const auto Shape = UCk_Utils_Pmg_BasicShapes::Create_Sphere(
            Receipt.Host,
            FTransform(FRotator::ZeroRotator, Location + FVector(0, 0, 60)),
            Radius, 12, 12, ECk_Plane_Axis::XY,
            Color(Kind, Receipt.ExpectedCategory), true, 2.0f, -1.0f);
        Receipt.Marker = Shape;
        if (!Allocated(Receipt.Marker)) { return false; }
        Run.Receipts.Add(Receipt);
        return true;
    }

    auto StageStress(FRun& Run) -> bool
    {
        const auto Category = Tag(TEXT("Poi.Category.Stress"));
        if (!Category.IsValid() || Run.Receipts.Num() != 9 ||
            !Run.StagedStress.IsEmpty()) { return false; }
        Run.StagedStress.Reserve(500);
        for (int32 Index = 0; Index < 500; ++Index)
        {
            FCkCompassGym_SourceReceipt Receipt;
            Receipt.CatalogId = 10000 + Index;
            Receipt.Kind = ECkCompassGym_SourceKind::Stress;
            Receipt.ExpectedCategory = Category;
            Receipt.Host = UCk_Utils_EntityLifetime_UE::
                Request_CreateEntity(Run.SourceRoot);
            // Capture the raw first return before subsequent composition.
            Run.StagedStress.Add(Receipt);
            if (!Allocated(Receipt.Host)) { return false; }
            UCk_Utils_Transform_UE::Add(Receipt.Host,
                FTransform(FRotator::ZeroRotator,
                    FVector(10.0 * Index, 5000.0, 100.0)),
                ECk_Replication::DoesNotReplicate);
            if (!Receipt.Host.Has<ck::FFragment_Transform>() ||
                Receipt.Host.Has<ck::FTag_Poi>() ||
                !Receipt.Host.Has<ck::FFragment_LifetimeOwner>() ||
                Receipt.Host.Get<ck::FFragment_LifetimeOwner>()
                    .Get_Entity() != Run.SourceRoot)
            { return false; }
            const auto Tagged = UCk_Utils_EntityTag_UE::
                Add_UsingGameplayTag(Receipt.Host, Category);
            if (!Allocated(Tagged)) { return false; }
        }
        return Run.StagedStress.Num() == 500;
    }

    auto Read(UWorld* World, const FRun& Run,
        const TArray<FCkCompassGym_SourceReceipt>& Receipts,
        bool CompositionComplete, TArray<FCkCompassGym_SourceRow>& OutRows,
        FString& OutFailure) -> ECkCompassGym_CatalogOutcome
    {
        return UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
            World, Run.GameState.Get(), Run.SourceRoot,
            CompositionComplete, Receipts, OutRows, OutFailure);
    }

    auto ReadTransaction(UWorld* World, const FRun& Run,
        const TArray<FCkCompassGym_SourceReceipt>& Active,
        const TArray<FCkCompassGym_SourceReceipt>& Staged,
        const TArray<FCkCompassGym_SourceReceipt>& Retiring,
        bool CompositionComplete,
        TArray<FCkCompassGym_SourceRow>& OutActive,
        TArray<FCkCompassGym_SourceRow>& OutStaged,
        FString& OutFailure) -> ECkCompassGym_CatalogOutcome
    {
        return UCkCompassGym_SourceCatalog::TryGet_SourceCatalogTransaction(
            World, Run.GameState.Get(), Run.SourceRoot, CompositionComplete,
            Active, Staged, Retiring, OutActive, OutStaged, OutFailure);
    }

    auto ReadRetiringPing(UWorld* World, const FRun& Run,
        const TArray<FCkCompassGym_SourceReceipt>& Active,
        const TArray<FCkCompassGym_SourceReceipt>& FrozenPings,
        TArray<FCkCompassGym_SourceRow>& OutActive,
        FString& OutFailure) -> ECkCompassGym_CatalogOutcome
    {
        const TArray<FCkCompassGym_SourceReceipt> Empty;
        TArray<FCkCompassGym_SourceRow> Staged;
        return UCkCompassGym_SourceCatalog::
            TryGet_SourceCatalogTransactionWithRetiringPings(
                World, Run.GameState.Get(), Run.SourceRoot, true,
                Active, Empty, Empty, FrozenPings,
                OutActive, Staged, OutFailure);
    }

    auto DirectChildren(const FCk_Handle& Parent, const FCk_Handle& WorldRoot,
        TArray<FCk_Handle>& Out) -> bool
    {
        return UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryGet_AllocatedDirectLifetimeDependents(Parent, WorldRoot, Out);
    }

    auto Result(UWorld* World, const FRun& Run, int64 Serial,
        TArray<FCkCompassGym_SourceRow>& OutRows,
        TArray<FCkCompassGym_SourceReceipt>& OutRetirees,
        FString& OutFailure) -> ECkCompassGym_MembershipResult
    {
        return UCkCompassGym_SourceMembership::Get_Result(World,
            Run.GameState.Get(), Run.SourceRoot, Serial,
            OutRows, OutRetirees, OutFailure);
    }

    auto ProjectedExact(const FRun& Run,
        const TArray<FCkCompassGym_SourceReceipt>& Expected) -> bool
    {
        if (Run.Dedicated || !Allocated(Run.Compass)) { return false; }
        const auto Entries = UCk_Utils_Compass_UE::Get_Entries(Run.Compass);
        if (Entries.Num() != Expected.Num()) { return false; }
        for (const auto& Receipt : Expected)
        {
            int32 Matches = 0;
            for (const auto& Entry : Entries)
            { Matches += Entry.Get_Poi() == Receipt.Host ? 1 : 0; }
            if (Matches != 1) { return false; }
        }
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCompassGym_SourceCatalog,
    "Ck.Compass.Gym.SourceCatalog.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCompassGym_SourceCatalog::RunTest(const FString& Parameters)
{
    using namespace ck_compass_catalog_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Compass catalog topology")); return false; }
    const auto Run = MakeShared<FRun>();
    Run->Dedicated = Dedicated;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            return IsValid(World) && IsValid(World->GetGameState()) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*World));
        }), 60.0, TEXT("authority GameState and ECS world root ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            Run->GameState = World->GetGameState();
            Run->WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
                TryGet_TransientEntityForWorld(*World);
            Run->SourceRoot = UCk_Utils_EntityLifetime_UE::
                Request_CreateEntity(Run->WorldRoot);
            if (!TestTrue(TEXT("retained source root is allocated"),
                Allocated(Run->SourceRoot))) { return; }
            constexpr const TCHAR* Categories[8] = {
                TEXT("Poi.Category.Quest"), TEXT("Poi.Category.Shop"),
                TEXT("Poi.Category.Danger"), TEXT("Poi.Category.Info"),
                TEXT("Poi.Category.Quest"), TEXT("Poi.Category.Shop"),
                TEXT("Poi.Category.Danger"), TEXT("Poi.Category.Info") };
            const FVector2D Offsets[8] = {
                {600, 0}, {424.3, 424.3}, {0, 600}, {-424.3, 424.3},
                {-600, 0}, {-424.3, -424.3}, {0, -600}, {424.3, -424.3} };
            const FVector Center(-800, 0, 100);
            bool Good = true;
            for (int32 Index = 0; Index < 8 && Good; ++Index)
            {
                Good = AddPoi(*Run, ECkCompassGym_SourceKind::Ring,
                    Categories[Index],
                    Center + FVector(Offsets[Index].X, Offsets[Index].Y, 0),
                    Index + 1, true);
            }
            Good = Good && AddPoi(*Run, ECkCompassGym_SourceKind::Waypoint,
                TEXT("Poi.Category.Waypoint"), Center + FVector(-2500, 0, 0),
                10, false);
            Run->Built = Good && Run->Receipts.Num() == 9;
            if (!TestTrue(TEXT("8 ring and 1 waypoint composed with real POI and PMG helpers"),
                Run->Built)) { return; }

            // The first ring category/mesh await production processors. The
            // later waypoint has an actually authored invalid display value.
            Run->WaypointDisplay = AttachDisplay(Run->Receipts.Last().Host,
                10, ECk_Poi_OffscreenPolicy::ClampToEdge,
                FVector2D(-1, 32));
            if (!TestTrue(TEXT("invalid-size display composed with real helper"),
                Allocated(Run->WaypointDisplay))) { return; }
            TestTrue(TEXT("earlier ring is genuinely awaiting category and PMG setup"),
                UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(
                    Run->Receipts[0].Host).HasTagExact(
                        Run->Receipts[0].ExpectedCategory) == false &&
                Run->Receipts[0].Marker.Has<
                    ck::FTag_Pmg_DebugShape_NeedsSetup>());
            TArray<FCkCompassGym_SourceRow> Rows;
            Rows.AddDefaulted();
            FString Failure;
            const auto Outcome = Read(World, *Run, Run->Receipts, true, Rows, Failure);
            TestTrue(TEXT("later invalid display wins over startup Pending atomically"),
                Outcome == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && Failure.Contains(TEXT("display current")));
            Rows.AddDefaulted();
            Failure.Reset();
            auto Reversed = Run->Receipts;
            Reversed.Swap(0, Reversed.Num() - 1);
            const auto ReverseOutcome = Read(World, *Run, Reversed,
                true, Rows, Failure);
            TestTrue(TEXT("earlier invalid display wins over later Pending atomically"),
                ReverseOutcome == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && Failure.Contains(TEXT("display current")));
            Rows.AddDefaulted();
            Failure.Reset();
            const auto Uncomposed = Read(World, *Run, Run->Receipts,
                false, Rows, Failure);
            TestTrue(TEXT("known invalid display rejects even while composition is incomplete"),
                Uncomposed == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && Failure.Contains(TEXT("display current")));
            UCk_Utils_PoiDisplayDefinition_UE::Request_SetSizeHint(
                Run->WaypointDisplay,
                FCk_Request_PoiDisplayDefinition_SetSizeHint(FVector2D(32, 32)),
                {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            return Allocated(Run->WaypointDisplay) &&
                UCk_Utils_PoiDisplayDefinition_UE::Get_SizeHint(
                    Run->WaypointDisplay).Equals(FVector2D(32, 32));
        }), 30.0, TEXT("real display request repaired invalid size")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!Run->Built || !Allocated(Run->WaypointDisplay)) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            Rows.AddDefaulted();
            FString Failure;
            const auto Pending = Read(World, *Run, Run->Receipts,
                false, Rows, Failure);
            TestTrue(TEXT("composition gate Pending publishes no prefix"),
                Pending == ECkCompassGym_CatalogOutcome::Pending &&
                Rows.IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (!Run->Built) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            Run->LastRows.AddDefaulted();
            Run->LastOutcome = Read(World, *Run, Run->Receipts, true,
                Run->LastRows, Run->LastFailure);
            return Run->LastOutcome != ECkCompassGym_CatalogOutcome::Pending;
        }), 60.0, TEXT("real POI category and PMG physical materialization")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("complete 9-row retained physical source catalog"),
                Run->LastOutcome == ECkCompassGym_CatalogOutcome::Complete &&
                Run->LastRows.Num() == 9)) { return; }
            for (const auto& Row : Run->LastRows)
            {
                TestTrue(TEXT("each row contains a physical persistent 12x12 sphere and 96 actual lines"),
                    Row.HasMarker && Row.Marker.Duration == -1.0 &&
                    Row.Marker.Segments == 12 && Row.Marker.Rings == 12 &&
                    Row.Marker.Lines.Num() == 96);
            }
            if (!Run->Dedicated)
            {
                Run->CompassObserver = UCk_Utils_EntityLifetime_UE::
                    Request_CreateEntity(Run->WorldRoot);
                if (!TestTrue(TEXT("listen-server Compass observer is a real world-root sibling"),
                    Allocated(Run->CompassObserver))) { return; }
                UCk_Utils_Transform_UE::Add(Run->CompassObserver,
                    FTransform(FRotator::ZeroRotator, FVector(-800, 0, 100)),
                    ECk_Replication::DoesNotReplicate);
                if (!TestTrue(TEXT("listen-server Compass observer has a real Transform"),
                    Run->CompassObserver.Has<ck::FFragment_Transform>()))
                { return; }
                auto Spec = FCk_Compass_Spec(360.0f);
                Spec.Set_MaxEntries(1024);
                Run->Compass = UCk_Utils_Compass_UE::Create(
                    Run->CompassObserver, Spec);
                TestTrue(TEXT("listen-server Compass created with capacity above full 509 cohort"),
                    Allocated(Run->Compass) &&
                    UCk_Utils_Compass_UE::Get_ArcDegrees(Run->Compass) == 360.0f);
            }
        })));
    if (!Dedicated)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
            { return ProjectedExact(*Run, Run->Receipts); }),
            30.0, TEXT("listen-server real Compass projects exactly original nine POIs")));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->LastOutcome != ECkCompassGym_CatalogOutcome::Complete ||
                Run->LastRows.Num() != 9) { return; }
            Run->OriginalNine = Run->Receipts;
            if (!TestTrue(TEXT("500 stress hosts retained before phase observation"),
                StageStress(*Run))) { return; }
            TArray<FCkCompassGym_SourceRow> Active, Staged;
            Active.AddDefaulted();
            Staged.AddDefaulted();
            FString Failure;
            const auto Pending = ReadTransaction(World, *Run, Run->Receipts,
                Run->StagedStress, {}, false, Active, Staged, Failure);
            TestTrue(TEXT("staging composition Pending publishes neither active nor staged prefix"),
                Pending == ECkCompassGym_CatalogOutcome::Pending &&
                Active.IsEmpty() && Staged.IsEmpty());
            Active.AddDefaulted();
            Staged.AddDefaulted();
            Failure.Reset();
            const auto Deferred = ReadTransaction(World, *Run, Run->Receipts,
                Run->StagedStress, {}, true, Active, Staged, Failure);
            TestTrue(TEXT("deferred stress category Pending publishes no prefix"),
                Deferred == ECkCompassGym_CatalogOutcome::Pending &&
                Active.IsEmpty() && Staged.IsEmpty());
            Active.AddDefaulted();
            Failure.Reset();
            const auto Strict = Read(World, *Run, Run->Receipts,
                true, Active, Failure);
            TestTrue(TEXT("legacy strict catalog rejects 500 unclaimed staged hosts"),
                Strict == ECkCompassGym_CatalogOutcome::Invalid &&
                Active.IsEmpty() && !Failure.IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (Run->StagedStress.Num() != 500) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            Run->PhaseActiveRows.AddDefaulted();
            Run->PhaseStagedRows.AddDefaulted();
            Run->PhaseOutcome = ReadTransaction(World, *Run,
                Run->Receipts, Run->StagedStress, {}, true,
                Run->PhaseActiveRows, Run->PhaseStagedRows,
                Run->PhaseFailure);
            return Run->PhaseOutcome != ECkCompassGym_CatalogOutcome::Pending;
        }), 60.0, TEXT("500 deferred EntityTag categories materialized for staged proof")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("staged phase proves exactly 9 active and 500 stress rows"),
                Run->PhaseOutcome == ECkCompassGym_CatalogOutcome::Complete &&
                Run->PhaseActiveRows.Num() == 9 &&
                Run->PhaseStagedRows.Num() == 500 &&
                Run->OriginalNine.Num() == 9 &&
                Run->StagedStress.Num() == 500)) { return; }
            bool StableNine = Run->Receipts.Num() == 9;
            for (int32 Index = 0; Index < 9 && StableNine; ++Index)
            {
                StableNine = Run->Receipts[Index].CatalogId ==
                    Run->OriginalNine[Index].CatalogId &&
                    Run->Receipts[Index].Host == Run->OriginalNine[Index].Host &&
                    Run->Receipts[Index].Marker ==
                        Run->OriginalNine[Index].Marker;
            }
            TestTrue(TEXT("original nine IDs and retained handles did not change"),
                StableNine);
            const auto StressTag = Tag(TEXT("Poi.Category.Stress"));
            bool AllStaged = true;
            for (int32 Index = 0; Index < 500 && AllStaged; ++Index)
            {
                const auto& Receipt = Run->StagedStress[Index];
                const auto& Row = Run->PhaseStagedRows[Index];
                AllStaged = Allocated(Receipt.Host) &&
                    Receipt.CatalogId == 10000 + Index &&
                    !Receipt.Host.Has<ck::FTag_Poi>() &&
                    UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(
                        Receipt.Host).HasTagExact(StressTag) &&
                    Row.CatalogId == Receipt.CatalogId &&
                    Row.Kind == ECkCompassGym_SourceKind::Stress &&
                    Row.Category == StressTag && !Row.HasMarker &&
                    !Row.HasDisplay;
            }
            TestTrue(TEXT("staged rows match all 500 real, unexposed stress hosts"),
                AllStaged);
            if (AllStaged)
            {
                for (auto& Receipt : Run->StagedStress)
                { Receipt.Admitted = true; }
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->PhaseOutcome != ECkCompassGym_CatalogOutcome::Complete ||
                Run->StagedStress.Num() != 500) { return; }
            auto RejectPhase = [this, World, Run](
                const TCHAR* Label,
                const TArray<FCkCompassGym_SourceReceipt>& Active,
                const TArray<FCkCompassGym_SourceReceipt>& Staged,
                const TArray<FCkCompassGym_SourceReceipt>& Retiring)
            {
                for (const bool Complete : {true, false})
                {
                    TArray<FCk_Handle> BeforeRoot, AfterRoot;
                    TArray<FCk_Handle> BeforeChild, AfterChild;
                    const bool Before = DirectChildren(Run->SourceRoot,
                        Run->WorldRoot, BeforeRoot) &&
                        DirectChildren(Run->StagedStress[0].Host,
                            Run->WorldRoot, BeforeChild);
                    TArray<FCkCompassGym_SourceRow> ActiveRows, StagedRows;
                    ActiveRows.AddDefaulted();
                    StagedRows.AddDefaulted();
                    FString Failure;
                    const auto Outcome = ReadTransaction(World, *Run,
                        Active, Staged, Retiring, Complete,
                        ActiveRows, StagedRows, Failure);
                    const bool After = DirectChildren(Run->SourceRoot,
                        Run->WorldRoot, AfterRoot) &&
                        DirectChildren(Run->StagedStress[0].Host,
                            Run->WorldRoot, AfterChild);
                    bool SetsUnchanged =
                        BeforeRoot.Num() == AfterRoot.Num() &&
                        BeforeChild.Num() == AfterChild.Num();
                    for (const auto& Child : BeforeRoot)
                    { if (!AfterRoot.Contains(Child)) { SetsUnchanged = false; break; } }
                    for (const auto& Child : BeforeChild)
                    { if (!AfterChild.Contains(Child)) { SetsUnchanged = false; break; } }
                    const auto StressTag = Tag(TEXT("Poi.Category.Stress"));
                    bool TagsUnchanged = true;
                    for (const auto& Receipt : Run->StagedStress)
                    {
                        if (!Allocated(Receipt.Host) ||
                            Receipt.Host.Has<ck::FTag_Poi>() ||
                            !UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(
                                Receipt.Host).HasTagExact(StressTag))
                        { TagsUnchanged = false; break; }
                    }
                    const FString CaseName = FString::Printf(TEXT("%s (%s)"),
                        Label, Complete ? TEXT("complete") : TEXT("composing"));
                    TestTrue(CaseName,
                        Before && After &&
                        Outcome == ECkCompassGym_CatalogOutcome::Invalid &&
                        ActiveRows.IsEmpty() && StagedRows.IsEmpty() &&
                        !Failure.IsEmpty() &&
                        SetsUnchanged && TagsUnchanged);
                }
            };
            auto Bad = Run->StagedStress;
            Bad.Pop();
            RejectPhase(TEXT("partial staged ledger rejects without mutation"),
                Run->Receipts, Bad, {});
            Bad = Run->StagedStress;
            Bad[1].CatalogId = Bad[0].CatalogId;
            RejectPhase(TEXT("duplicate staged ID rejects without mutation"),
                Run->Receipts, Bad, {});
            Bad = Run->StagedStress;
            Bad[1].Host = Bad[0].Host;
            RejectPhase(TEXT("duplicate staged handle rejects without mutation"),
                Run->Receipts, Bad, {});
            Bad = Run->StagedStress;
            Bad[0].Host = Run->WorldRoot;
            RejectPhase(TEXT("foreign staged handle rejects without mutation"),
                Run->Receipts, Bad, {});
            Bad = Run->StagedStress;
            Bad[0].Host = Run->Receipts[0].Host;
            RejectPhase(TEXT("active/staged handle alias rejects without mutation"),
                Run->Receipts, Bad, {});
            RejectPhase(TEXT("aliased staged and retiring ledgers reject"),
                Run->Receipts, Run->StagedStress, Run->StagedStress);
            TArray<FCkCompassGym_SourceRow> AliasedOutput;
            AliasedOutput.AddDefaulted();
            FString AliasFailure;
            const auto AliasOutcome = ReadTransaction(World, *Run,
                Run->Receipts, Run->StagedStress, {}, true,
                AliasedOutput, AliasedOutput, AliasFailure);
            TestTrue(TEXT("same output array rejects a valid 9+500 source atomically"),
                AliasOutcome == ECkCompassGym_CatalogOutcome::Invalid &&
                AliasedOutput.IsEmpty() &&
                AliasFailure.Contains(TEXT("outputs alias")));
            Run->UnexpectedStressChild =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                    Run->StagedStress[0].Host);
            if (TestTrue(TEXT("real unexpected staged child allocated"),
                Allocated(Run->UnexpectedStressChild)))
            {
                RejectPhase(TEXT("unexpected staged child rejects without mutation"),
                    Run->Receipts, Run->StagedStress, {});
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(
                    Run->UnexpectedStressChild);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return !Allocated(Run->UnexpectedStressChild); }),
        30.0, TEXT("unexpected staged dependent physically removed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->PhaseOutcome != ECkCompassGym_CatalogOutcome::Complete ||
                Run->StagedStress.Num() != 500) { return; }
            TArray<FCkCompassGym_SourceRow> Active, Staged;
            FString Failure;
            const auto Restored = ReadTransaction(World, *Run,
                Run->Receipts, Run->StagedStress, {}, true,
                Active, Staged, Failure);
            if (!TestTrue(TEXT("staged 9+500 proof restored after child removal"),
                Restored == ECkCompassGym_CatalogOutcome::Complete &&
                Active.Num() == 9 && Staged.Num() == 500)) { return; }
            TArray<FCk_Handle> RootBefore, RootAfter;
            const bool CensusBefore = DirectChildren(Run->SourceRoot,
                Run->WorldRoot, RootBefore);
            const auto Untagged = [&]() -> bool
            {
                for (const auto& Receipt : Run->StagedStress)
                {
                    if (!Allocated(Receipt.Host) ||
                        Receipt.Host.Has<ck::FTag_Poi>()) { return false; }
                }
                return true;
            };
            auto Partial = Run->StagedStress;
            Partial.Pop();
            Failure.Reset();
            const bool Malformed = UCkCompassGym_SourceMembership::
                TryRequest_StressAdmission(World, Run->GameState.Get(),
                    Run->SourceRoot, Run->Receipts, Partial, 1, Failure);
            TArray<FCkCompassGym_SourceRow> ResultRows;
            TArray<FCkCompassGym_SourceReceipt> ResultRetirees;
            FString ResultFailure;
            const auto Missing = Result(World, *Run, 1,
                ResultRows, ResultRetirees, ResultFailure);
            const bool CensusAfter = DirectChildren(Run->SourceRoot,
                Run->WorldRoot, RootAfter);
            TestTrue(TEXT("partial admission rejects without queue record, source tags, or child mutation"),
                CensusBefore && CensusAfter && RootBefore.Num() == 509 &&
                RootBefore == RootAfter && !Malformed && !Failure.IsEmpty() &&
                Missing == ECkCompassGym_MembershipResult::Unavailable &&
                ResultRows.IsEmpty() && ResultRetirees.IsEmpty() && Untagged());

            Failure.Reset();
            if (!TestTrue(TEXT("production source queues serial-one stress admission"),
                UCkCompassGym_SourceMembership::TryRequest_StressAdmission(
                    World, Run->GameState.Get(), Run->SourceRoot,
                    Run->Receipts, Run->StagedStress, 1, Failure) &&
                Failure.IsEmpty())) { return; }
            ResultRows.AddDefaulted();
            ResultRetirees.AddDefaulted();
            const auto Pending = Result(World, *Run, 1,
                ResultRows, ResultRetirees, ResultFailure);
            Failure.Reset();
            const bool Busy = UCkCompassGym_SourceMembership::
                TryRequest_StressAdmission(World, Run->GameState.Get(),
                    Run->SourceRoot, Run->Receipts, Run->StagedStress, 2,
                    Failure);
            FString ReplayFailure;
            const bool Replay = UCkCompassGym_SourceMembership::
                TryRequest_StressAdmission(World, Run->GameState.Get(),
                    Run->SourceRoot, Run->Receipts, Run->StagedStress, 1,
                    ReplayFailure);
            ResultRows.AddDefaulted();
            ResultRetirees.AddDefaulted();
            ResultFailure.Reset();
            const auto Retained = Result(World, *Run, 1,
                ResultRows, ResultRetirees, ResultFailure);
            TArray<FCk_Handle> RootAfterBusy;
            const bool CensusAfterBusy = DirectChildren(Run->SourceRoot,
                Run->WorldRoot, RootAfterBusy);
            TestTrue(TEXT("busy and replayed admission preserve serial-one Pending record and unexposed 500"),
                Pending == ECkCompassGym_MembershipResult::Pending &&
                Retained == ECkCompassGym_MembershipResult::Pending &&
                ResultRows.IsEmpty() && ResultRetirees.IsEmpty() &&
                !Busy && !Failure.IsEmpty() && !Replay &&
                !ReplayFailure.IsEmpty() && Untagged() &&
                CensusAfterBusy && RootAfterBusy == RootBefore);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            if (!Run->Dedicated && Run->Receipts.Num() == 9 &&
                Run->StagedStress.Num() == 500)
            {
                auto Combined = Run->Receipts;
                Combined.Append(Run->StagedStress);
                Run->AdmissionPartialProjection |=
                    !ProjectedExact(*Run, Run->Receipts) &&
                    !ProjectedExact(*Run, Combined);
            }
            Run->MembershipResult = Result(World, *Run, 1,
                Run->CommittedRows, Run->CommittedRetirees,
                Run->MembershipFailure);
            return Run->MembershipResult != ECkCompassGym_MembershipResult::Pending;
        }), 30.0, TEXT("ordered source membership admission serial one committed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("serial-one production commit publishes whole 509 and no retirees"),
                Run->MembershipResult == ECkCompassGym_MembershipResult::Committed &&
                Run->CommittedRows.Num() == 509 &&
                Run->CommittedRetirees.IsEmpty() &&
                Run->MembershipFailure.IsEmpty())) { return; }
            Run->Receipts.Append(Run->StagedStress);
            Run->StagedStress.Reset();
            TArray<FCkCompassGym_SourceRow> StrictRows;
            FString StrictFailure;
            const auto Strict = Read(World, *Run, Run->Receipts,
                true, StrictRows, StrictFailure);
            bool StableNine = Run->OriginalNine.Num() == 9;
            for (int32 Index = 0; Index < 9 && StableNine; ++Index)
            {
                StableNine = Run->Receipts[Index].CatalogId ==
                    Run->OriginalNine[Index].CatalogId &&
                    Run->Receipts[Index].Host == Run->OriginalNine[Index].Host &&
                    Run->Receipts[Index].Marker == Run->OriginalNine[Index].Marker &&
                    Run->CommittedRows[Index].CatalogId ==
                        Run->OriginalNine[Index].CatalogId;
            }
            TestTrue(TEXT("strict 509 and original nine identities survive queued admission"),
                Strict == ECkCompassGym_CatalogOutcome::Complete &&
                StrictRows.Num() == 509 && StableNine);
            FString ReplayFailure;
            const bool Replay = UCkCompassGym_SourceMembership::
                TryRequest_StressWithdrawal(World, Run->GameState.Get(),
                    Run->SourceRoot, Run->Receipts, 1, ReplayFailure);
            TArray<FCkCompassGym_SourceRow> StillCommitted;
            TArray<FCkCompassGym_SourceReceipt> NoRetirees;
            FString ResultFailure;
            const auto ResultOne = Result(World, *Run, 1,
                StillCommitted, NoRetirees, ResultFailure);
            TestTrue(TEXT("committed serial-one replay rejects without replacing readable 509 result"),
                !Replay && !ReplayFailure.IsEmpty() &&
                ResultOne == ECkCompassGym_MembershipResult::Committed &&
                StillCommitted.Num() == 509 && NoRetirees.IsEmpty());
        })));
    if (!Dedicated)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
            {
                if (Run->Receipts.Num() != 509) { return false; }
                const bool NewWhole = ProjectedExact(*Run, Run->Receipts);
                Run->AdmissionPartialProjection |=
                    !NewWhole && !ProjectedExact(*Run, Run->OriginalNine);
                return NewWhole;
            }),
            30.0, TEXT("listen-server real Compass projects whole 509 POI cohort")));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->Receipts.Num() != 509) { return; }
            if (!Run->Dedicated)
            {
                TestFalse(TEXT("listen-server admission never projected a partial POI cohort"),
                    Run->AdmissionPartialProjection);
            }
            TArray<FCk_Handle> RootBefore, RootAfter;
            const bool CensusBefore = DirectChildren(Run->SourceRoot,
                Run->WorldRoot, RootBefore);
            const auto AllExposed = [&]() -> bool
            {
                for (int32 Index = 9; Index < 509; ++Index)
                {
                    if (!Allocated(Run->Receipts[Index].Host) ||
                        !Run->Receipts[Index].Host.Has<ck::FTag_Poi>())
                    { return false; }
                }
                return true;
            };
            auto Partial = Run->Receipts;
            Partial.Pop();
            FString Failure;
            const bool Malformed = UCkCompassGym_SourceMembership::
                TryRequest_StressWithdrawal(World, Run->GameState.Get(),
                    Run->SourceRoot, Partial, 2, Failure);
            TArray<FCkCompassGym_SourceRow> PreviousRows;
            TArray<FCkCompassGym_SourceReceipt> PreviousRetirees;
            FString PreviousFailure;
            const auto Previous = Result(World, *Run, 1,
                PreviousRows, PreviousRetirees, PreviousFailure);
            const bool CensusAfter = DirectChildren(Run->SourceRoot,
                Run->WorldRoot, RootAfter);
            TestTrue(TEXT("partial withdrawal rejects while serial-one committed record and 509 tags remain"),
                CensusBefore && CensusAfter && RootBefore == RootAfter &&
                !Malformed && !Failure.IsEmpty() && AllExposed() &&
                Previous == ECkCompassGym_MembershipResult::Committed &&
                PreviousRows.Num() == 509 && PreviousRetirees.IsEmpty());

            Failure.Reset();
            if (!TestTrue(TEXT("production source queues serial-two stress withdrawal"),
                UCkCompassGym_SourceMembership::TryRequest_StressWithdrawal(
                    World, Run->GameState.Get(), Run->SourceRoot,
                    Run->Receipts, 2, Failure) && Failure.IsEmpty()))
            { return; }
            TArray<FCkCompassGym_SourceRow> ResultRows;
            TArray<FCkCompassGym_SourceReceipt> ResultRetirees;
            FString ResultFailure;
            const auto Pending = Result(World, *Run, 2,
                ResultRows, ResultRetirees, ResultFailure);
            Failure.Reset();
            const bool Busy = UCkCompassGym_SourceMembership::
                TryRequest_StressWithdrawal(World, Run->GameState.Get(),
                    Run->SourceRoot, Run->Receipts, 3, Failure);
            FString ReplayFailure;
            const bool Replay = UCkCompassGym_SourceMembership::
                TryRequest_StressWithdrawal(World, Run->GameState.Get(),
                    Run->SourceRoot, Run->Receipts, 2, ReplayFailure);
            ResultRows.AddDefaulted();
            ResultRetirees.AddDefaulted();
            ResultFailure.Reset();
            const auto Retained = Result(World, *Run, 2,
                ResultRows, ResultRetirees, ResultFailure);
            TArray<FCk_Handle> RootAfterBusy;
            const bool CensusAfterBusy = DirectChildren(Run->SourceRoot,
                Run->WorldRoot, RootAfterBusy);
            TestTrue(TEXT("busy and replayed withdrawal preserve serial-two Pending record and 509 tags"),
                Pending == ECkCompassGym_MembershipResult::Pending &&
                Retained == ECkCompassGym_MembershipResult::Pending &&
                ResultRows.IsEmpty() && ResultRetirees.IsEmpty() &&
                !Busy && !Failure.IsEmpty() && !Replay &&
                !ReplayFailure.IsEmpty() && AllExposed() &&
                CensusAfterBusy && RootAfterBusy == RootBefore);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            if (!Run->Dedicated && Run->Receipts.Num() == 509)
            {
                Run->WithdrawalPartialProjection |=
                    !ProjectedExact(*Run, Run->Receipts) &&
                    !ProjectedExact(*Run, Run->OriginalNine);
            }
            Run->MembershipResult = Result(World, *Run, 2,
                Run->CommittedRows, Run->CommittedRetirees,
                Run->MembershipFailure);
            return Run->MembershipResult != ECkCompassGym_MembershipResult::Pending;
        }), 30.0, TEXT("ordered source membership withdrawal serial two committed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("serial-two production commit returns original nine and exact frozen 500"),
                Run->MembershipResult == ECkCompassGym_MembershipResult::Committed &&
                Run->CommittedRows.Num() == 9 &&
                Run->CommittedRetirees.Num() == 500 &&
                Run->MembershipFailure.IsEmpty())) { return; }
            bool SameRetirement = true;
            for (int32 Index = 0; Index < 500 && SameRetirement; ++Index)
            {
                const auto& Frozen = Run->CommittedRetirees[Index];
                const auto& Prior = Run->Receipts[9 + Index];
                SameRetirement = Frozen.CatalogId == Prior.CatalogId &&
                    Frozen.Host == Prior.Host && Frozen.Marker == Prior.Marker &&
                    Frozen.Timer == Prior.Timer &&
                    Frozen.ExpectedCategory == Prior.ExpectedCategory &&
                    !Frozen.Host.Has<ck::FTag_Poi>();
            }
            TestTrue(TEXT("frozen retirement receipts preserve all 500 exact identities and remove membership"),
                SameRetirement);
            FString ReplayFailure;
            const bool Replay = UCkCompassGym_SourceMembership::
                TryRequest_StressWithdrawal(World, Run->GameState.Get(),
                    Run->SourceRoot, Run->Receipts, 2, ReplayFailure);
            TArray<FCkCompassGym_SourceRow> StillCommitted;
            TArray<FCkCompassGym_SourceReceipt> SameFrozen;
            FString ResultFailure;
            const auto ResultTwo = Result(World, *Run, 2,
                StillCommitted, SameFrozen, ResultFailure);
            TestTrue(TEXT("committed serial-two replay rejects without replacing frozen 9/500 result"),
                !Replay && !ReplayFailure.IsEmpty() &&
                ResultTwo == ECkCompassGym_MembershipResult::Committed &&
                StillCommitted.Num() == 9 && SameFrozen.Num() == 500);
            Run->RetiringStress = Run->CommittedRetirees;
            Run->Receipts.SetNum(9);
            bool StableNine = Run->OriginalNine.Num() == 9;
            for (int32 Index = 0; Index < 9 && StableNine; ++Index)
            {
                StableNine = Run->Receipts[Index].CatalogId ==
                    Run->OriginalNine[Index].CatalogId &&
                    Run->Receipts[Index].Host == Run->OriginalNine[Index].Host &&
                    Run->Receipts[Index].Marker == Run->OriginalNine[Index].Marker &&
                    Run->CommittedRows[Index].CatalogId ==
                        Run->OriginalNine[Index].CatalogId;
            }
            TestTrue(TEXT("original nine POI identities retained through queued withdrawal"),
                StableNine);
            const auto StressTag = Tag(TEXT("Poi.Category.Stress"));
            for (const auto& Receipt : Run->RetiringStress)
            {
                auto MutableHost = Receipt.Host;
                UCk_Utils_EntityTag_UE::Request_TryRemove_UsingGameplayTag(
                    MutableHost, StressTag, {});
            }
            TArray<FCkCompassGym_SourceRow> Active, Staged;
            FString PhaseFailure;
            const auto Retiring = ReadTransaction(World, *Run,
                Run->Receipts, {}, Run->RetiringStress, true,
                Active, Staged, PhaseFailure);
            TestTrue(TEXT("ordered withdrawal exposes original nine while frozen 500 remain allocated"),
                Retiring == ECkCompassGym_CatalogOutcome::Complete &&
                Active.Num() == 9 && Staged.IsEmpty());
        })));
    if (!Dedicated)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
            {
                if (Run->Receipts.Num() != 9 ||
                    Run->RetiringStress.Num() != 500) { return false; }
                auto OldWhole = Run->Receipts;
                OldWhole.Append(Run->RetiringStress);
                const bool NewWhole = ProjectedExact(*Run, Run->Receipts);
                Run->WithdrawalPartialProjection |=
                    !NewWhole && !ProjectedExact(*Run, OldWhole);
                return NewWhole;
            }),
            30.0, TEXT("listen-server real Compass projects original nine after withdrawal")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
            {
                TestFalse(TEXT("listen-server withdrawal never projected a partial POI cohort"),
                    Run->WithdrawalPartialProjection);
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (Run->RetiringStress.Num() != 500) { return true; }
            const auto StressTag = Tag(TEXT("Poi.Category.Stress"));
            for (const auto& Receipt : Run->RetiringStress)
            {
                if (!Allocated(Receipt.Host) ||
                    Receipt.Host.Has<ck::FTag_Poi>() ||
                    UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(
                        Receipt.Host).HasTagExact(StressTag))
                { return false; }
            }
            return true;
        }), 60.0, TEXT("real deferred stress category withdrawals materialized")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->RetiringStress.Num() != 500) { return; }
            TArray<FCkCompassGym_SourceRow> Active, Staged;
            Active.AddDefaulted();
            Staged.AddDefaulted();
            FString Failure;
            const auto Retiring = ReadTransaction(World, *Run,
                Run->Receipts, {}, Run->RetiringStress,
                true, Active, Staged, Failure);
            TestTrue(TEXT("withdrawn 500 allocated retirees expose only original nine"),
                Retiring == ECkCompassGym_CatalogOutcome::Complete &&
                Active.Num() == 9 && Staged.IsEmpty());
            for (auto& Receipt : Run->RetiringStress)
            { auto MutableHost = Receipt.Host; UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(MutableHost); }
            Active.AddDefaulted();
            Staged.AddDefaulted();
            Failure.Reset();
            const auto Initiated = ReadTransaction(World, *Run,
                Run->Receipts, {}, Run->RetiringStress,
                true, Active, Staged, Failure);
            TestTrue(TEXT("all 500 initiated pending-kill retirees remain accounted"),
                Initiated == ECkCompassGym_CatalogOutcome::Complete &&
                Active.Num() == 9 && Staged.IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (Run->RetiringStress.Num() != 500) { return true; }
            for (const auto& Receipt : Run->RetiringStress)
            { if (Allocated(Receipt.Host)) { return false; } }
            return true;
        }), 60.0, TEXT("all 500 retiring stress hosts physically deallocated")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->RetiringStress.Num() != 500) { return; }
            TArray<FCkCompassGym_SourceRow> Active, Staged;
            FString Failure;
            const auto Retired = ReadTransaction(World, *Run,
                Run->Receipts, {}, Run->RetiringStress,
                true, Active, Staged, Failure);
            TestTrue(TEXT("frozen 500 physically absent receipts still prove nine"),
                Retired == ECkCompassGym_CatalogOutcome::Complete &&
                Active.Num() == 9 && Staged.IsEmpty());
            Active.AddDefaulted();
            Failure.Reset();
            const auto Strict = Read(World, *Run, Run->Receipts,
                true, Active, Failure);
            TestTrue(TEXT("legacy strict nine restored after real retirement"),
                Strict == ECkCompassGym_CatalogOutcome::Complete &&
                Active.Num() == 9);
            bool StableNine = Run->OriginalNine.Num() == 9 &&
                Run->Receipts.Num() == 9;
            for (int32 Index = 0; Index < 9 && StableNine; ++Index)
            {
                StableNine = Run->Receipts[Index].CatalogId ==
                    Run->OriginalNine[Index].CatalogId &&
                    Run->Receipts[Index].Host ==
                        Run->OriginalNine[Index].Host &&
                    Run->Receipts[Index].Marker ==
                        Run->OriginalNine[Index].Marker;
            }
            TestTrue(TEXT("original nine retained identities survive stress cycle"),
                StableNine);
            Run->RetiringStress.Reset();
            Run->StagedStress.Reset();
            Run->PhaseActiveRows.Reset();
            Run->PhaseStagedRows.Reset();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            // Re-stage real hosts after the first 500 have physically gone.
            // A malformed base-nine request would otherwise be rejected by
            // the frozen-retirement busy check or missing staged receipts,
            // without exercising the retained identity check.
            if (!TestTrue(TEXT("base-nine provenance probe starts after exact stress retirement"),
                Run->Receipts.Num() == 9 && Run->RetiringStress.IsEmpty() &&
                Run->StagedStress.IsEmpty())) { return; }
            if (!TestTrue(TEXT("second real 500-host stress stage composed"),
                StageStress(*Run)))
            {
                for (const auto& Receipt : Run->StagedStress)
                {
                    if (Allocated(Receipt.Host))
                    { auto MutableHost = Receipt.Host; UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(MutableHost); }
                }
                return;
            }
            TArray<FCk_Handle> Before, After;
            const bool BeforeCensus = DirectChildren(
                Run->SourceRoot, Run->WorldRoot, Before);
            // Deliberately corrupt only the private retained weak authority
            // for one synchronous request, then restore the entire record.
            // This does not replace the world's real GameState or tick a
            // processor while the fault exists.
            auto MutableRoot = Run->SourceRoot;
            auto Saved = MutableRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
            MutableRoot.Get<ck::FFragment_CompassGym_SourceMembership>()
                .GameState.Reset();
            FString ForeignFailure;
            const bool ForeignAccepted = UCkCompassGym_SourceMembership::
                TryRequest_StressAdmission(World, Run->GameState.Get(),
                    Run->SourceRoot, Run->Receipts, Run->StagedStress,
                    3, ForeignFailure);
            MutableRoot.Get<ck::FFragment_CompassGym_SourceMembership>() =
                MoveTemp(Saved);
            TestTrue(TEXT("mismatched retained weak GameState rejects without replacing record"),
                !ForeignAccepted && !ForeignFailure.IsEmpty());
            auto ChangedBase = Run->Receipts;
            ChangedBase[0].CatalogId = 900001;
            FString Failure;
            const bool Accepted = UCkCompassGym_SourceMembership::
                TryRequest_StressAdmission(World, Run->GameState.Get(),
                    Run->SourceRoot, ChangedBase, Run->StagedStress, 3, Failure);
            TArray<FCkCompassGym_SourceRow> OldRows, NewRows;
            TArray<FCkCompassGym_SourceReceipt> OldFrozen, NewFrozen;
            FString OldFailure, NewFailure;
            const auto Old = Result(World, *Run, 2,
                OldRows, OldFrozen, OldFailure);
            const auto New = Result(World, *Run, 3,
                NewRows, NewFrozen, NewFailure);
            const bool AfterCensus = DirectChildren(
                Run->SourceRoot, Run->WorldRoot, After);
            bool Untagged = true;
            for (const auto& Receipt : Run->StagedStress)
            {
                if (!Allocated(Receipt.Host) || Receipt.Host.Has<ck::FTag_Poi>())
                { Untagged = false; break; }
            }
            TestTrue(TEXT("changed base-nine ID rejects before queue and preserves serial-two result and source"),
                BeforeCensus && AfterCensus && Before == After &&
                Before.Num() == 509 && !Accepted && !Failure.IsEmpty() &&
                Untagged && Old == ECkCompassGym_MembershipResult::Committed &&
                OldRows.Num() == 9 && OldFrozen.Num() == 500 &&
                New == ECkCompassGym_MembershipResult::Unavailable &&
                NewRows.IsEmpty() && NewFrozen.IsEmpty());
            for (const auto& Receipt : Run->StagedStress)
            {
                if (Allocated(Receipt.Host))
                { auto MutableHost = Receipt.Host; UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(MutableHost); }
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (Run->StagedStress.IsEmpty()) { return true; }
            for (const auto& Receipt : Run->StagedStress)
            { if (Allocated(Receipt.Host)) { return false; } }
            return true;
        }), 60.0, TEXT("base-nine provenance probe hosts physically deallocated")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->StagedStress.IsEmpty()) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Failure;
            const auto Outcome = Read(World, *Run, Run->Receipts,
                true, Rows, Failure);
            TestTrue(TEXT("strict original-nine source restored after provenance probe cleanup"),
                Outcome == ECkCompassGym_CatalogOutcome::Complete &&
                Rows.Num() == 9);
            Run->StagedStress.Reset();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->Receipts.Num() != 9) { return; }
            auto Reject = [this, World, Run](
                const TCHAR* Label, TArray<FCkCompassGym_SourceReceipt> Receipts)
            {
                TArray<FCkCompassGym_SourceRow> Rows;
                Rows.AddDefaulted();
                FString Failure;
                const auto Outcome = Read(World, *Run, Receipts, true, Rows, Failure);
                TestTrue(Label, Outcome == ECkCompassGym_CatalogOutcome::Invalid &&
                    Rows.IsEmpty() && !Failure.IsEmpty());
                return Failure;
            };
            auto Bad = Run->Receipts;
            Bad[0].Marker = {};
            Reject(TEXT("default marker receipt rejected atomically"), Bad);
            Bad = Run->Receipts;
            Bad[0].Marker = Bad[1].Marker;
            Reject(TEXT("foreign marker alias rejected atomically"), Bad);
            Bad = Run->Receipts;
            Bad[0].Timer = Bad[1].Marker;
            Reject(TEXT("unexpected timer alias rejected atomically"), Bad);
            Bad = Run->Receipts;
            Bad[0].Kind = ECkCompassGym_SourceKind::Waypoint;
            Bad[0].ExpectedCategory = Tag(TEXT("Poi.Category.Waypoint"));
            const auto CountReason = Reject(
                TEXT("complete kind-count mismatch rejected atomically"), Bad);
            TestTrue(TEXT("count mismatch reached the full-count gate"),
                CountReason.Contains(TEXT("kind counts")));
            Bad = Run->Receipts;
            Bad[0].Host = {};
            Reject(TEXT("default host receipt rejected atomically"), Bad);
            Run->ExtraChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                Run->Receipts[0].Host);
            if (TestTrue(TEXT("real extra host child allocated"),
                Allocated(Run->ExtraChild)))
            { Reject(TEXT("unclaimed direct child rejected atomically"), Run->Receipts); }
            if (Allocated(Run->ExtraChild))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->ExtraChild); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return !Allocated(Run->ExtraChild); }),
        30.0, TEXT("unclaimed child physically removed before source loss")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            if (!Run->Built ||
                Run->LastOutcome != ECkCompassGym_CatalogOutcome::Complete)
            { return; }
            if (!TestTrue(TEXT("real ping POI and marker composed"),
                AddPoi(*Run, ECkCompassGym_SourceKind::Ping,
                    TEXT("Poi.Category.Ping"), FVector(1500, 0, 100),
                    20, true))) { return; }
            Run->PingHost = Run->Receipts.Last().Host;
            auto TimerSpec = FCk_Timer_Spec(FCk_Time(5.0f));
            TimerSpec.Set_Behavior(ECk_Timer_Behavior::StopOnDone)
                .Set_StartingState(ECk_Timer_State::Paused)
                .Set_CountDirection(ECk_Timer_CountDirection::CountUp);
            Run->PingTimer = UCk_Utils_Timer_UE::Add(Run->PingHost, TimerSpec);
            Run->Receipts.Last().Timer = Run->PingTimer;
            TestTrue(TEXT("real paused CountUp ping timer returned under ping host"),
                Allocated(Run->PingTimer) &&
                !Run->PingTimer.Has<ck::FTag_Timer_Countdown>());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (Run->PingHost == FCk_Handle{} ||
                Run->PingTimer == FCk_Handle{}) { return true; }
            if (!Allocated(Run->PingHost) || !Allocated(Run->PingTimer))
            { return false; }
            const auto Marker = Run->Receipts.Last().Marker;
            if (!Allocated(Marker) ||
                Marker.Has<ck::FTag_Pmg_DebugShape_NeedsSetup>() ||
                Marker.Has<ck::FTag_Pmg_DebugShape_LinesNeedBaking>() ||
                !Marker.Has<ck::FFragment_Pmg_DebugShape>())
            { return false; }
            auto* Mesh = Marker.Get<ck::FFragment_Pmg_DebugShape>()
                .Get_MeshComponent().Get();
            return !Run->PingTimer.Has<ck::FTag_Timer_NeedsSetup>() &&
                UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(Run->PingHost)
                    .HasTagExact(Tag(TEXT("Poi.Category.Ping"))) &&
                IsValid(Mesh) && Mesh->IsRegistered() &&
                Mesh->GetProcMeshSection(0) && Mesh->GetProcMeshSection(1);
        }), 30.0, TEXT("actual ping category, timer, and PMG mesh setup")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([Run](UWorld*)
        {
            if (Run->PingHost == FCk_Handle{}) { return; }
            if (!Allocated(Run->PingTimer))
            {
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->PingHost);
                return;
            }
            UCk_Utils_Timer_UE::Request_Resume(Run->PingTimer, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (Run->PingHost == FCk_Handle{} ||
                !Allocated(Run->PingTimer)) { return true; }
            return Run->PingTimer.Has<ck::FTag_Timer_NeedsUpdate>();
        }), 5.0, TEXT("production timer resume request entered running state")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (Run->PingHost == FCk_Handle{} ||
                !Allocated(Run->PingTimer)) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            Run->PingRows.AddDefaulted();
            Run->PingOutcome = Read(World, *Run, Run->Receipts,
                true, Run->PingRows, Run->PingFailure);
            return Run->PingOutcome != ECkCompassGym_CatalogOutcome::Pending;
        }), 5.0, TEXT("running five-second CountUp ping reaches complete ten-row catalog")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->PingHost == FCk_Handle{} ||
                !Allocated(Run->PingTimer)) { return; }
            if (!TestTrue(TEXT("healthy CountUp ping completes ten-row physical catalog"),
                Run->PingOutcome == ECkCompassGym_CatalogOutcome::Complete &&
                Run->PingRows.Num() == 10 &&
                Run->PingRows.Last().HasMarker &&
                Run->PingRows.Last().Marker.Lines.Num() == 96))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->PingHost); return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Failure;
            auto CheckActiveChildTag = [&](const FCk_Handle& Child) -> bool
            {
                if (!Allocated(Child)) { return false; }
                auto MutableChild = Child;
                MutableChild.Add<ck::FTag_Poi>();
                if (!Allocated(Child)) { return false; }
                const bool Added = Child.Has<ck::FTag_Poi>();
                Rows.AddDefaulted();
                Failure.Reset();
                const auto Rejected = Added
                    ? Read(World, *Run, Run->Receipts, true, Rows, Failure)
                    : ECkCompassGym_CatalogOutcome::Pending;
                const bool AtomicallyRejected = Added &&
                    Rejected == ECkCompassGym_CatalogOutcome::Invalid &&
                    Rows.IsEmpty() && Failure.Contains(TEXT("POI child"));
                if (!Allocated(Child)) { return false; }
                const bool RemovedChild = Child.Has<ck::FTag_Poi>() &&
                    MutableChild.Try_Remove<ck::FTag_Poi>();
                if (!Allocated(Child) || !RemovedChild ||
                    Child.Has<ck::FTag_Poi>()) { return false; }
                Rows.Reset();
                Failure.Reset();
                const auto Restored = Read(World, *Run, Run->Receipts,
                    true, Rows, Failure);
                return AtomicallyRejected &&
                    Restored == ECkCompassGym_CatalogOutcome::Complete &&
                    Rows.Num() == 10 && Failure.IsEmpty();
            };
            if (!TestTrue(TEXT("actual active Ping Marker POI tag rejected and restored"),
                    CheckActiveChildTag(Run->Receipts.Last().Marker)) ||
                !TestTrue(TEXT("actual active Ping Timer POI tag rejected and restored"),
                    CheckActiveChildTag(Run->Receipts.Last().Timer)))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->PingHost); return; }
            auto Alias = Run->Receipts;
            Alias.Last().Marker = Alias.Last().Timer;
            Rows.AddDefaulted();
            const auto AliasOutcome = Read(World, *Run, Alias,
                true, Rows, Failure);
            TestTrue(TEXT("actual ping Marker=Timer alias rejected atomically"),
                AliasOutcome == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && Failure.Contains(TEXT("receipt")));
            UCk_Utils_Timer_UE::Request_ChangeCountDirection(
                Run->PingTimer, ECk_Timer_CountDirection::CountDown, {});
            Rows.AddDefaulted();
            Failure.Reset();
            const auto CountdownOutcome = Read(World, *Run, Run->Receipts,
                true, Rows, Failure);
            TestTrue(TEXT("real Countdown timer rejected atomically"),
                Run->PingTimer.Has<ck::FTag_Timer_Countdown>() &&
                CountdownOutcome == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && Failure.Contains(TEXT("Ping timer")));
            Run->Receipts.Last().Admitted = true;
            Run->FrozenPing = Run->Receipts.Last();
            auto Base = Run->Receipts;
            Base.Pop();
            const TArray<FCkCompassGym_SourceReceipt> Retiring{Run->FrozenPing};
            const TArray<FCkCompassGym_SourceReceipt> Empty;
            Rows.AddDefaulted();
            Failure.Reset();
            const auto Unclaimed = ReadRetiringPing(World, *Run, Base, Empty,
                Rows, Failure);
            TestTrue(TEXT("omitted live Ping rejected as unclaimed source child"),
                Unclaimed == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && !Failure.IsEmpty());
            auto Malformed = Retiring;
            Malformed[0].Marker = {};
            Rows.AddDefaulted();
            Failure.Reset();
            const auto BadReceipt = ReadRetiringPing(World, *Run, Base,
                Malformed, Rows, Failure);
            TestTrue(TEXT("default frozen Ping marker rejected atomically"),
                BadReceipt == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && Failure.Contains(TEXT("identity")));
            Rows.AddDefaulted();
            Failure.Reset();
            const auto Exposed = ReadRetiringPing(World, *Run, Base, Retiring,
                Rows, Failure);
            TestTrue(TEXT("frozen Ping remains invalid while host is POI-visible"),
                Exposed == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && Failure.Contains(TEXT("exposed")));
            auto MutableHost = Run->FrozenPing.Host;
            const bool Removed = MutableHost.Try_Remove<ck::FTag_Poi>();
            if (!TestTrue(TEXT("retiring Ping POI tag removed before destruction"),
                Allocated(Run->FrozenPing.Host) && Removed &&
                !Run->FrozenPing.Host.Has<ck::FTag_Poi>()))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->PingHost); return; }
            auto CheckRetiringChildTag = [&](const FCk_Handle& Child) -> bool
            {
                if (!Allocated(Child)) { return false; }
                auto MutableChild = Child;
                MutableChild.Add<ck::FTag_Poi>();
                if (!Allocated(Child)) { return false; }
                const bool Added = Child.Has<ck::FTag_Poi>();
                Rows.AddDefaulted();
                Failure.Reset();
                const auto Rejected = Added
                    ? ReadRetiringPing(World, *Run, Base, Retiring, Rows, Failure)
                    : ECkCompassGym_CatalogOutcome::Pending;
                const bool AtomicallyRejected = Added &&
                    Rejected == ECkCompassGym_CatalogOutcome::Invalid &&
                    Rows.IsEmpty() && Failure.Contains(TEXT("POI-visible"));
                if (!Allocated(Child)) { return false; }
                const bool RemovedChild = Child.Has<ck::FTag_Poi>() &&
                    MutableChild.Try_Remove<ck::FTag_Poi>();
                if (!Allocated(Child) || !RemovedChild ||
                    Child.Has<ck::FTag_Poi>()) { return false; }
                Rows.Reset();
                Failure.Reset();
                const auto Restored = ReadRetiringPing(World, *Run, Base,
                    Retiring, Rows, Failure);
                return AtomicallyRejected &&
                    Restored == ECkCompassGym_CatalogOutcome::Complete &&
                    Rows.Num() == 9 && Failure.IsEmpty();
            };
            if (!TestTrue(TEXT("actual retiring Ping Marker POI tag rejected and restored"),
                    CheckRetiringChildTag(Run->FrozenPing.Marker)) ||
                !TestTrue(TEXT("actual retiring Ping Timer POI tag rejected and restored"),
                    CheckRetiringChildTag(Run->FrozenPing.Timer)))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->PingHost); return; }
            auto MutableMarker = Run->FrozenPing.Marker;
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(MutableMarker);
            Rows.AddDefaulted();
            Failure.Reset();
            const auto RetiringOutcome = ReadRetiringPing(World, *Run, Base,
                Retiring, Rows, Failure);
            TestTrue(TEXT("retiring Ping marker initiation preserves exact nine rows"),
                RetiringOutcome == ECkCompassGym_CatalogOutcome::Complete &&
                Rows.Num() == 9 && Failure.IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return Run->FrozenPing.Marker == FCk_Handle{} ||
            !Allocated(Run->FrozenPing.Marker); }),
        30.0, TEXT("retiring Ping marker physically removed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("retiring Ping partial phase retains Host and Timer after Marker loss"),
                Run->FrozenPing.Host != FCk_Handle{} &&
                !Allocated(Run->FrozenPing.Marker) &&
                Allocated(Run->FrozenPing.Host) &&
                Allocated(Run->FrozenPing.Timer)))
            { return; }
            auto Base = Run->Receipts;
            Base.Pop();
            const TArray<FCkCompassGym_SourceReceipt> Retiring{Run->FrozenPing};
            TArray<FCkCompassGym_SourceRow> Rows;
            Rows.AddDefaulted();
            FString Failure;
            const auto Partial = ReadRetiringPing(World, *Run, Base, Retiring,
                Rows, Failure);
            TestTrue(TEXT("partial Ping cascade remains complete nine rows"),
                Partial == ECkCompassGym_CatalogOutcome::Complete &&
                Rows.Num() == 9 && Failure.IsEmpty());
            auto Duplicate = Retiring;
            Duplicate.Add(Run->FrozenPing);
            Rows.AddDefaulted();
            Failure.Reset();
            const auto Bad = ReadRetiringPing(World, *Run, Base, Duplicate,
                Rows, Failure);
            TestTrue(TEXT("duplicate frozen Ping identity rejected atomically"),
                Bad == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && !Failure.IsEmpty());
            Run->RetiringPingExtraChild =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                    Run->FrozenPing.Host);
            if (TestTrue(TEXT("retiring Ping unexpected child actually allocated"),
                Allocated(Run->RetiringPingExtraChild)))
            {
                Rows.AddDefaulted();
                Failure.Reset();
                const auto Unknown = ReadRetiringPing(World, *Run, Base,
                    Retiring, Rows, Failure);
                TestTrue(TEXT("retiring Ping unknown child rejected atomically"),
                    Unknown == ECkCompassGym_CatalogOutcome::Invalid &&
                    Rows.IsEmpty() && !Failure.IsEmpty());
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(
                    Run->RetiringPingExtraChild);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return Run->RetiringPingExtraChild == FCk_Handle{} ||
            !Allocated(Run->RetiringPingExtraChild); }),
        30.0, TEXT("retiring Ping unexpected child physically removed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->FrozenPing.Host == FCk_Handle{} ||
                !Allocated(Run->FrozenPing.Host)) { return; }
            auto Base = Run->Receipts;
            Base.Pop();
            const TArray<FCkCompassGym_SourceReceipt> Retiring{Run->FrozenPing};
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Failure;
            const auto Restored = ReadRetiringPing(World, *Run, Base,
                Retiring, Rows, Failure);
            TestTrue(TEXT("retiring Ping exact cohort restored after child removal"),
                Restored == ECkCompassGym_CatalogOutcome::Complete &&
                Rows.Num() == 9 && Failure.IsEmpty());
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->PingHost);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return Run->FrozenPing.Host == FCk_Handle{} ||
            (!Allocated(Run->FrozenPing.Host) &&
             !Allocated(Run->FrozenPing.Marker) &&
             !Allocated(Run->FrozenPing.Timer)); }),
        30.0, TEXT("retiring Ping Host Marker Timer physically removed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->FrozenPing.Host != FCk_Handle{} &&
                !Allocated(Run->FrozenPing.Host) &&
                !Allocated(Run->FrozenPing.Marker) &&
                !Allocated(Run->FrozenPing.Timer) && Run->Receipts.Num() == 10)
            {
                auto Base = Run->Receipts;
                Base.Pop();
                const TArray<FCkCompassGym_SourceReceipt> Retiring{Run->FrozenPing};
                TArray<FCkCompassGym_SourceRow> Rows;
                Rows.AddDefaulted();
                FString Failure;
                const auto Gone = ReadRetiringPing(World, *Run, Base, Retiring,
                    Rows, Failure);
                TestTrue(TEXT("fully deallocated Ping frozen receipt remains proven"),
                    Gone == ECkCompassGym_CatalogOutcome::Complete &&
                    Rows.Num() == 9 && Failure.IsEmpty());
            }
            if (Run->PingHost != FCk_Handle{} &&
                !Allocated(Run->PingHost) && Run->Receipts.Num() == 10)
            { Run->Receipts.Pop(); }
            if (!Run->Built ||
                Run->LastOutcome != ECkCompassGym_CatalogOutcome::Complete)
            { return; }
            if (!TestTrue(TEXT("live baseline restored after extra child removal"),
                Allocated(Run->SourceRoot))) { return; }
            TArray<FCkCompassGym_SourceRow> BeforeLoss;
            FString BeforeLossFailure;
            const auto BeforeLossOutcome = Read(World, *Run, Run->Receipts,
                true, BeforeLoss, BeforeLossFailure);
            if (!TestTrue(TEXT("nine-row healthy cohort reverified before natural Ping TTL"),
                BeforeLossOutcome == ECkCompassGym_CatalogOutcome::Complete &&
                BeforeLoss.Num() == 9)) { return; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            if (!TestTrue(TEXT("real natural TTL Ping authored from healthy nine"),
                Run->Receipts.Num() == 9 &&
                AddPoi(*Run, ECkCompassGym_SourceKind::Ping,
                    TEXT("Poi.Category.Ping"), FVector(1600, 0, 100),
                    20, true))) { Run->NaturalPingSetupFailed = true; return; }
            Run->NaturalPingHost = Run->Receipts.Last().Host;
            auto Spec = FCk_Timer_Spec(FCk_Time(5.0f));
            Spec.Set_Behavior(ECk_Timer_Behavior::StopOnDone)
                .Set_StartingState(ECk_Timer_State::Running)
                .Set_CountDirection(ECk_Timer_CountDirection::CountUp);
            Run->NaturalPingTimer = UCk_Utils_Timer_UE::Add(
                Run->NaturalPingHost, Spec);
            Run->Receipts.Last().Timer = Run->NaturalPingTimer;
            if (!TestTrue(TEXT("natural TTL Timer first return retained"),
                Allocated(Run->NaturalPingTimer)))
            { Run->NaturalPingSetupFailed = true; return; }
            Run->NaturalPingProbe.Reset(NewObject<UCkCompassGym_PingTtlProbe>());
            if (!TestTrue(TEXT("reflected natural TTL probe allocated before running setup"),
                IsValid(Run->NaturalPingProbe.Get())))
            { Run->NaturalPingSetupFailed = true; return; }
            Run->NaturalPingDoneDelegate.BindDynamic(Run->NaturalPingProbe.Get(),
                &UCkCompassGym_PingTtlProbe::OnDone);
            if (!TestTrue(TEXT("real Done dynamic delegate bound before running setup"),
                Run->NaturalPingDoneDelegate.IsBound()))
            { Run->NaturalPingSetupFailed = true; return; }
            const auto Bound = UCk_Utils_Timer_UE::BindTo_OnDone(
                Run->NaturalPingTimer, Run->NaturalPingDoneDelegate,
                ECk_Signal_BindingPolicy::IgnorePayloadInFlight);
            if (!TestTrue(TEXT("real Done signal bound to exact running Timer"),
                Bound == Run->NaturalPingTimer &&
                Allocated(Run->NaturalPingTimer)))
            { Run->NaturalPingSetupFailed = true; return; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (Run->NaturalPingSetupFailed) { return true; }
            if (!Run->NaturalPingProbe.IsValid() ||
                Run->NaturalPingProbe->Get_DoneCount() > 0 ||
                Run->NaturalPingProbe->Get_Failed())
            { Run->NaturalPingSetupFailed = true; return true; }
            if (!Allocated(Run->NaturalPingHost) ||
                !Allocated(Run->NaturalPingTimer) ||
                Run->Receipts.Num() != 10) { return false; }
            const auto Marker = Run->Receipts.Last().Marker;
            if (!Allocated(Marker) ||
                Marker.Has<ck::FTag_Pmg_DebugShape_NeedsSetup>() ||
                Marker.Has<ck::FTag_Pmg_DebugShape_LinesNeedBaking>() ||
                !Marker.Has<ck::FFragment_Pmg_DebugShape>() ||
                Run->NaturalPingTimer.Has<ck::FTag_Timer_NeedsSetup>() ||
                !Run->NaturalPingTimer.Has<ck::FTag_Timer_NeedsUpdate>())
            { return false; }
            auto* Mesh = Marker.Get<ck::FFragment_Pmg_DebugShape>()
                .Get_MeshComponent().Get();
            if (!IsValid(Mesh) || !Mesh->IsRegistered() ||
                !Mesh->GetProcMeshSection(0) || !Mesh->GetProcMeshSection(1))
            { return false; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Failure;
            Run->NaturalPingOutcome = Read(World, *Run, Run->Receipts,
                true, Rows, Failure);
            return Run->NaturalPingOutcome != ECkCompassGym_CatalogOutcome::Pending;
        }), 10.0, TEXT("running natural TTL Timer materializes complete ten-row source before Done")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("natural TTL Ping complete receipt available"),
                !Run->NaturalPingSetupFailed &&
                Run->NaturalPingOutcome == ECkCompassGym_CatalogOutcome::Complete &&
                Run->Receipts.Num() == 10 &&
                Allocated(Run->NaturalPingHost) &&
                Allocated(Run->NaturalPingTimer)))
            { Run->NaturalPingSetupFailed = true; return; }
            if (!TestTrue(TEXT("reflected probe armed by exact ten-row physical proof"),
                Run->NaturalPingProbe->Arm(World, Run->GameState.Get(),
                    Run->SourceRoot, Run->Receipts)))
            { Run->NaturalPingSetupFailed = true; return; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return Run->NaturalPingSetupFailed || (Run->NaturalPingProbe.IsValid() &&
            (Run->NaturalPingProbe->Get_DoneCount() > 0 ||
             Run->NaturalPingProbe->Get_Failed())); }),
        20.0, TEXT("real five-second StopOnDone Ping callback fired")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("natural Done callback once in exact world before Host destroy"),
                Run->NaturalPingProbe.IsValid() &&
                Run->NaturalPingProbe->Get_DoneCount() == 1 &&
                !Run->NaturalPingProbe->Get_Failed() &&
                Run->NaturalPingProbe->Get_ExactWorldCallback() &&
                Run->NaturalPingProbe->Get_HostLiveBeforeDestroy() &&
                Run->NaturalPingProbe->Get_FrozeBeforeDestroy() &&
                Run->NaturalPingProbe->Get_RequestedDestroy() &&
                Run->NaturalPingProbe->Get_DoneElapsedSeconds() >= 5.0))
            { return; }
            auto Base = Run->Receipts;
            Base.Pop();
            const TArray<FCkCompassGym_SourceReceipt> Retiring{
                Run->NaturalPingProbe->Get_FrozenReceipt()};
            TArray<FCkCompassGym_SourceRow> Rows;
            Rows.AddDefaulted();
            FString Failure;
            const auto Outcome = ReadRetiringPing(World, *Run, Base, Retiring,
                Rows, Failure);
            TestTrue(TEXT("natural callback frozen Ping proves nine whole rows"),
                Outcome == ECkCompassGym_CatalogOutcome::Complete &&
                Rows.Num() == 9 && Failure.IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (Run->NaturalPingSetupFailed) { return true; }
            if (!Run->NaturalPingProbe.IsValid() ||
                Run->NaturalPingProbe->Get_DoneCount() != 1) { return false; }
            const auto R = Run->NaturalPingProbe->Get_FrozenReceipt();
            return !Allocated(R.Host) && !Allocated(R.Marker) &&
                !Allocated(R.Timer);
        }), 30.0, TEXT("natural TTL Ping Host Marker Timer physically deallocated")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("natural TTL remains exactly once after cascade"),
                Run->NaturalPingProbe.IsValid() &&
                Run->NaturalPingProbe->Get_DoneCount() == 1 &&
                !Run->NaturalPingProbe->Get_Failed() &&
                Run->Receipts.Num() == 10)) { return; }
            auto Base = Run->Receipts;
            Base.Pop();
            const TArray<FCkCompassGym_SourceReceipt> Retiring{
                Run->NaturalPingProbe->Get_FrozenReceipt()};
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Failure;
            const auto Outcome = ReadRetiringPing(World, *Run, Base, Retiring,
                Rows, Failure);
            if (!TestTrue(TEXT("fully retired natural Ping keeps complete nine rows"),
                Outcome == ECkCompassGym_CatalogOutcome::Complete &&
                Rows.Num() == 9 && Failure.IsEmpty())) { return; }
            Run->Receipts.Pop();
            TArray<FCkCompassGym_SourceRow> BeforeLoss;
            FString BeforeLossFailure;
            const auto BeforeLossOutcome = Read(World, *Run, Run->Receipts,
                true, BeforeLoss, BeforeLossFailure);
            if (!TestTrue(TEXT("nine-row baseline restored before source loss"),
                BeforeLossOutcome == ECkCompassGym_CatalogOutcome::Complete &&
                BeforeLoss.Num() == 9)) { return; }
            for (auto& Receipt : Run->Receipts) { Receipt.Admitted = true; }
            Run->LostMarker = Run->Receipts[0].Marker;
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->LostMarker);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return Run->LostMarker == FCk_Handle{} ||
            !Allocated(Run->LostMarker); }),
        30.0, TEXT("admitted marker physically deallocated")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->LostMarker == FCk_Handle{}) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            Rows.AddDefaulted();
            FString Failure;
            const auto Outcome = Read(World, *Run, Run->Receipts,
                true, Rows, Failure);
            TestTrue(TEXT("admitted marker loss is Invalid with no prefix"),
                Outcome == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && !Failure.IsEmpty());
            Run->BadDurationMarker = UCk_Utils_Pmg_BasicShapes::Create_Sphere(
                Run->Receipts[0].Host,
                FTransform(FRotator::ZeroRotator, FVector(-200, 0, 160)),
                40.0f, 12, 12, ECk_Plane_Axis::XY,
                FLinearColor(1.0f, 0.78f, 0.25f), true, 2.0f, -2.0f);
            Run->Receipts[0].Marker = Run->BadDurationMarker;
            Run->Receipts[0].Admitted = false;
            TestTrue(TEXT("replacement marker authored with real persistent PMG helper"),
                Allocated(Run->BadDurationMarker));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (Run->BadDurationMarker == FCk_Handle{}) { return true; }
            if (!Allocated(Run->BadDurationMarker) ||
                Run->BadDurationMarker.Has<ck::FTag_Pmg_DebugShape_NeedsSetup>() ||
                Run->BadDurationMarker.Has<ck::FTag_Pmg_DebugShape_LinesNeedBaking>() ||
                !Run->BadDurationMarker.Has_All<
                    ck::FFragment_Pmg_DebugShape,
                    ck::FFragment_Pmg_DebugShape_Common,
                    ck::FFragment_Pmg_DebugShape_Lines>())
            { return false; }
            auto* Mesh = Run->BadDurationMarker.Get<ck::FFragment_Pmg_DebugShape>()
                .Get_MeshComponent().Get();
            return IsValid(Mesh) && Mesh->IsRegistered() &&
                Mesh->GetProcMeshSection(0) && Mesh->GetProcMeshSection(1) &&
                IsValid(Mesh->GetMaterial(0)) &&
                IsValid(Mesh->GetMaterial(1)) &&
                Run->BadDurationMarker.Get<
                    ck::FFragment_Pmg_DebugShape_Common>()
                    .Get_Duration().Get_Seconds() == -2.0 &&
                Run->BadDurationMarker.Get<
                    ck::FFragment_Pmg_DebugShape_Lines>()
                    .Get_Lines().Num() == 96 &&
                Run->BadDurationMarker.Has<
                    ck::FTag_Pmg_DebugShape_PersistentDuration>();
        }), 30.0, TEXT("incorrect-duration marker physically materialized")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->BadDurationMarker == FCk_Handle{}) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            Rows.AddDefaulted();
            FString Failure;
            const auto Outcome = Read(World, *Run, Run->Receipts,
                true, Rows, Failure);
            TestTrue(TEXT("physically materialized persistent -2 marker rejected atomically"),
                Outcome == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && Failure.Contains(TEXT("marker physical")));
            Run->RetiredHost = Run->Receipts[0].Host;
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->RetiredHost);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return Run->RetiredHost == FCk_Handle{} ||
            !Allocated(Run->RetiredHost); }),
        30.0, TEXT("former first host cascade deallocated")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->RetiredHost == FCk_Handle{} ||
                Allocated(Run->RetiredHost)) { return; }
            auto* Actor = World->SpawnActor<AActor>();
            if (!TestTrue(TEXT("real root-bound source actor spawned"),
                IsValid(Actor))) { return; }
            Run->RootBoundActor = Actor;
            auto* Component = NewObject<USceneComponent>(Actor);
            if (!TestTrue(TEXT("real root component allocated"),
                IsValid(Component))) { return; }
            Actor->SetRootComponent(Component);
            Component->RegisterComponent();
            Actor->SetActorLocation(FVector(-200, 0, 100));
            if (!TestTrue(TEXT("root component registered"),
                Component->IsRegistered())) { return; }
            Run->RootBoundHost = UCk_Utils_EntityLifetime_UE::
                Request_CreateEntity(Run->SourceRoot);
            if (!TestTrue(TEXT("replacement source host allocated"),
                Allocated(Run->RootBoundHost))) { return; }
            const auto Transform = UCk_Utils_Transform_UE::
                AddAndAttachToUnrealComponent(Run->RootBoundHost, Component,
                    ECk_Replication::DoesNotReplicate);
            if (!TestTrue(TEXT("host has real attached-transform feature"),
                Allocated(Transform) &&
                Run->RootBoundHost.Has_All<
                    ck::FFragment_LifetimeOwner,
                    ck::FFragment_Transform_RootComponent>() &&
                Run->RootBoundHost.Get<ck::FFragment_LifetimeOwner>()
                    .Get_Entity() == Run->SourceRoot &&
                Run->RootBoundHost.Get<
                    ck::FFragment_Transform_RootComponent>()
                    .Get_RootComponent().Get() == Component)) { return; }
            const auto Category = Tag(TEXT("Poi.Category.Quest"));
            const auto Poi = UCk_Utils_Poi_UE::Add(
                Run->RootBoundHost, FCk_Poi_Spec(Category));
            if (!TestTrue(TEXT("root-bound host has POI feature"),
                Allocated(Poi) && Allocated(AttachDisplay(
                    Run->RootBoundHost, 1,
                    ECk_Poi_OffscreenPolicy::Hide)))) { return; }
            const auto Marker = UCk_Utils_Pmg_BasicShapes::Create_Sphere(
                Run->RootBoundHost,
                FTransform(FRotator::ZeroRotator, FVector(-200, 0, 160)),
                40.0f, 12, 12, ECk_Plane_Axis::XY,
                FLinearColor(1.0f, 0.78f, 0.25f), true, 2.0f, -1.0f);
            Run->Receipts[0].Host = Run->RootBoundHost;
            Run->Receipts[0].Marker = Marker;
            if (!TestTrue(TEXT("root-bound replacement marker directly owned"),
                Allocated(Marker) && Marker.Has<ck::FFragment_LifetimeOwner>() &&
                Marker.Get<ck::FFragment_LifetimeOwner>().Get_Entity() ==
                    Run->RootBoundHost)) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            Rows.AddDefaulted();
            FString Failure;
            const auto Outcome = Read(World, *Run, Run->Receipts,
                true, Rows, Failure);
            TestTrue(TEXT("actual RootComponent-backed host rejected atomically"),
                Allocated(Marker) &&
                Outcome == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && Failure.Contains(TEXT("RootComponent")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([Run](UWorld*)
        {
            if (Allocated(Run->SourceRoot))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->SourceRoot); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return !Allocated(Run->SourceRoot); }),
        30.0, TEXT("catalog fixture source root cascade deallocated")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            TArray<FCkCompassGym_SourceRow> Rows;
            Rows.AddDefaulted();
            FString Failure;
            const auto Outcome = Read(World, *Run, Run->Receipts,
                true, Rows, Failure);
            TestTrue(TEXT("stale absent root fails closed without partial rows"),
                Outcome == ECkCompassGym_CatalogOutcome::Invalid &&
                Rows.IsEmpty() && !Failure.IsEmpty());
            if (Run->RootBoundActor.IsValid())
            { Run->RootBoundActor->Destroy(); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
