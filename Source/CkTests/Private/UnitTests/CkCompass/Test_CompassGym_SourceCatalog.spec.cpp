#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkCompassGym_SourceCatalog.h"
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
        ECkCompassGym_CatalogOutcome PingOutcome =
            ECkCompassGym_CatalogOutcome::Pending;
        TArray<FCkCompassGym_SourceRow> PingRows;
        FString PingFailure;
        TArray<FCkCompassGym_SourceReceipt> Receipts;
        TArray<FCkCompassGym_SourceReceipt> OriginalNine;
        TArray<FCkCompassGym_SourceReceipt> StagedStress;
        TArray<FCkCompassGym_SourceReceipt> RetiringStress;
        TArray<FCkCompassGym_SourceRow> PhaseActiveRows;
        TArray<FCkCompassGym_SourceRow> PhaseStagedRows;
        FString PhaseFailure;
        ECkCompassGym_CatalogOutcome PhaseOutcome =
            ECkCompassGym_CatalogOutcome::Pending;
        FCk_Handle UnexpectedStressChild;
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
        Receipt.CatalogId = Run.Receipts.Num() + 1;
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

    auto DirectChildren(const FCk_Handle& Parent, const FCk_Handle& WorldRoot,
        TArray<FCk_Handle>& Out) -> bool
    {
        return UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryGet_AllocatedDirectLifetimeDependents(Parent, WorldRoot, Out);
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
        })));
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
            // Fixture membership preparation uses the same FCk_Handle primitive
            // as Poi::Add. This test does not claim an ordered commit scheduler.
            for (auto& Receipt : Run->StagedStress)
            {
                if (!TestTrue(TEXT("stress host remains unexposed before admission"),
                    Allocated(Receipt.Host) &&
                    !Receipt.Host.Has<ck::FTag_Poi>())) { return; }
                Receipt.Host.Add<ck::FTag_Poi>();
            }
            Run->Receipts.Append(Run->StagedStress);
            Run->StagedStress.Reset();
            Active.Reset();
            Staged.Reset();
            Failure.Reset();
            const auto ActiveOutcome = ReadTransaction(World, *Run,
                Run->Receipts, {}, {}, true, Active, Staged, Failure);
            TestTrue(TEXT("admitted real 500 stress tags yield complete 509 active rows"),
                ActiveOutcome == ECkCompassGym_CatalogOutcome::Complete &&
                Active.Num() == 509 && Staged.IsEmpty());
            Active.AddDefaulted();
            Failure.Reset();
            const auto Strict = Read(World, *Run, Run->Receipts,
                true, Active, Failure);
            TestTrue(TEXT("legacy strict catalog independently accepts 509 active rows"),
                Strict == ECkCompassGym_CatalogOutcome::Complete &&
                Active.Num() == 509);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->Receipts.Num() != 509) { return; }
            const auto StressTag = Tag(TEXT("Poi.Category.Stress"));
            Run->RetiringStress.Reserve(500);
            for (int32 Index = 9; Index < 509; ++Index)
            {
                auto Receipt = Run->Receipts[Index];
                if (!TestTrue(TEXT("retiring stress host allocated before tag withdrawal"),
                    Allocated(Receipt.Host) &&
                    Receipt.Host.Has<ck::FTag_Poi>())) { return; }
                Receipt.Host.Try_Remove<ck::FTag_Poi>();
                UCk_Utils_EntityTag_UE::Request_TryRemove_UsingGameplayTag(
                    Receipt.Host, StressTag, {});
                Run->RetiringStress.Add(Receipt);
            }
            Run->Receipts.SetNum(9);
        })));
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
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Receipt.Host); }
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
            auto Alias = Run->Receipts;
            Alias.Last().Marker = Alias.Last().Timer;
            TArray<FCkCompassGym_SourceRow> Rows;
            Rows.AddDefaulted();
            FString Failure;
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
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->PingHost);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return Run->PingHost == FCk_Handle{} ||
            !Allocated(Run->PingHost); }),
        30.0, TEXT("negative ping cohort physically removed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
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
            if (!TestTrue(TEXT("nine-row healthy cohort reverified before marker loss"),
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
