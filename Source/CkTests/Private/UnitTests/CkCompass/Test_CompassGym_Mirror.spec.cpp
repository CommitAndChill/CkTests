#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkCompassGym_Mirror.h"
#include "CkGym_AdmissionRuntime.h"
#include "CkCompassGym_MirrorRetirementProbe.h"
#include "CkCompassGym_PingTtlProbe.h"
#include "CkCompassGym_SourceCatalog.h"
#include "CkCompassGym_SourceMembership.h"
#include "CkCompassGym_Wire.h"
#include "CkEntityScriptGym_Runtime.h"
#include "../../CkCompassGym_Mirror_Private.h"
#include "../../CkCompassGym_SourceMembership_Private.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkPoi/CkPoi_Utils.h"
#include "CkPoiDisplayDefinition/CkPoiDisplayDefinition_Utils.h"
#include "CkPmg/CkPmg_Utils_BasicShapes.h"
#include "CkTimer/CkTimer_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTimer/CkTimer_Fragment_Data.h"
#include "CkCompass/CkCompass_Utils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/StrongObjectPtr.h"

// This fixture deliberately forwards compact values captured from real
// authority SourceCatalog receipts. It proves client-world composition and
// lifecycle, not delivery through Iris or a replicated GameState property.
namespace ck_compass_mirror_test
{
    struct FClient
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AGameStateBase> GameState;
        TWeakObjectPtr<AGameStateBase> ReplacementGameState;
        FCk_Handle WorldRoot;
        FCk_Handle MirrorRoot;
        TArray<FCk_Handle> BaseHosts;
        TArray<FCk_Handle> DepartedHosts;
        FCk_Handle PingHost;
        FCk_Handle PingMarker;
    };

    struct FRun
    {
        TWeakObjectPtr<AGameStateBase> SourceGameState;
        FCk_Handle SourceWorldRoot;
        FCk_Handle SourceRoot;
        TArray<FCkCompassGym_SourceReceipt> Base;
        TArray<FCkCompassGym_SourceReceipt> Stress;
        TArray<FCkCompassGym_SourceReceipt> FrozenStress;
        FCkCompassGym_SourceReceipt Ping;
        TStrongObjectPtr<UCkCompassGym_MirrorRetirementProbe> RetirementProbe;
        TStrongObjectPtr<UCkCompassGym_PingTtlProbe> SourcePingProbe;
        FCk_Delegate_Timer SourcePingDone;
        FClient Clients[2];
        int64 LastSourceRevision = 0;
        bool RetirePhysicallyComplete = false;
        bool Failed = false;
    };

    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    FString RetireFailureSummary(const FRun& Run)
    {
        int32 SourceAllocated = 0;
        for (const auto& R : Run.FrozenStress)
        { SourceAllocated += int32(Allocated(R.Host)); }
        FString Summary = FString::Printf(
            TEXT("Stress retirement incomplete: source allocated=%d/%d"),
            SourceAllocated, Run.FrozenStress.Num());
        const double Now = FPlatformTime::Seconds();
        if (Allocated(Run.SourceRoot) && ck::IsValid(Run.SourceRoot) &&
            Run.SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
        {
            const auto& Source = Run.SourceRoot.Get<
                ck::FFragment_CompassGym_SourceMembership>();
            Summary += FString::Printf(
                TEXT("; source closed=%d result=%d request=%d cursor=%d/%d "
                     "complete=%d deadline=%.3f now=%.3f remaining=%.3f "
                     "failure=%s"),
                int32(Source.ClosedUnproven), int32(Source.Result),
                int32(Source.Request), Source.FrozenStressCursor,
                Source.FrozenRetiring.Num(),
                int32(Source.FrozenStressRetirementComplete),
                Source.FrozenStressDeadlineSeconds, Now,
                Source.FrozenStressDeadlineSeconds - Now, *Source.Failure);
            const auto& P = Source.Profile;
            Summary += FString::Printf(
                TEXT("; sourceProfileCallsMs=stress:%llu/%.3f "
                     "union:%llu/%.3f"),
                static_cast<unsigned long long>(P.ProcessFrozenStress.Calls),
                P.ProcessFrozenStress.Seconds * 1000.0,
                static_cast<unsigned long long>(P.ProveFrozenUnion.Calls),
                P.ProveFrozenUnion.Seconds * 1000.0);
        }
        else
        {
            Summary += FString::Printf(
                TEXT("; source state unavailable now=%.3f"), Now);
        }
        for (int32 I = 0; I < 2; ++I)
        {
            const auto& Client = Run.Clients[I];
            int32 DepartedAllocated = 0;
            for (const auto& H : Client.DepartedHosts)
            { DepartedAllocated += int32(Allocated(H)); }
            bool Busy = false;
            bool Failed = false;
            FString Why;
            const bool Status = UCkCompassGym_Mirror::TryGet_Status(
                Client.World.Get(), Client.GameState.Get(), Client.MirrorRoot,
                Busy, Failed, Why);
            int32 Phase = -1;
            int32 Retiring = -1;
            bool Closed = false;
            FString FrameAndProfile;
            if (Allocated(Client.MirrorRoot) &&
                ck::IsValid(Client.MirrorRoot) &&
                Client.MirrorRoot.Has<ck::FFragment_CompassGym_Mirror>())
            {
                const auto& State = Client.MirrorRoot.Get<
                    ck::FFragment_CompassGym_Mirror>();
                Phase = int32(State.Phase);
                Retiring = State.Retiring.Num();
                Closed = State.ClosedUnproven;
                const auto& P = State.Profile;
                FrameAndProfile = FString::Printf(
                    TEXT(" installed=%d frame=(schema%d g%lld q%lld "
                         "lifecycle%d declared%d rows%d) pending=(g%lld q%lld "
                         "rows%d stageCursor%d) profileCallsMs="
                         "census:%llu/%.3f physical:%llu/%.3f "
                         "commit:%llu/%.3f retire:%llu/%.3f stage:%llu/%.3f"),
                    int32(State.HasInstalled), State.InstalledFrame.SchemaVersion,
                    static_cast<long long>(State.InstalledFrame.SourceGeneration),
                    static_cast<long long>(State.InstalledFrame.Sequence),
                    int32(State.InstalledFrame.Lifecycle),
                    State.InstalledFrame.DeclaredRowCount,
                    State.InstalledFrame.Rows.Num(),
                    static_cast<long long>(State.PendingFrame.SourceGeneration),
                    static_cast<long long>(State.PendingFrame.Sequence),
                    State.PendingRows.Num(), State.StageCursor,
                    static_cast<unsigned long long>(P.CompleteCensus.Calls),
                    P.CompleteCensus.Seconds * 1000.0,
                    static_cast<unsigned long long>(P.InstalledPhysical.Calls),
                    P.InstalledPhysical.Seconds * 1000.0,
                    static_cast<unsigned long long>(P.Commit.Calls),
                    P.Commit.Seconds * 1000.0,
                    static_cast<unsigned long long>(P.RetireBatch.Calls),
                    P.RetireBatch.Seconds * 1000.0,
                    static_cast<unsigned long long>(P.StageLoop.Calls),
                    P.StageLoop.Seconds * 1000.0);
            }
            Summary += FString::Printf(
                TEXT("; client%d status=%d busy=%d failed=%d phase=%d "
                     "retiring=%d closed=%d departedAllocated=%d/%d why=%s"),
                I, int32(Status), int32(Busy), int32(Failed), Phase,
                Retiring, int32(Closed), DepartedAllocated,
                Client.DepartedHosts.Num(), *Why);
            Summary += FrameAndProfile;
        }
        return Summary;
    }

    bool NoMirrorInAuthorityWorld(UWorld* World)
    {
        if (!IsValid(World) || World->IsNetMode(NM_Client)) { return false; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!Allocated(Root)) { return false; }
        const auto Registry = Root.Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        bool Empty = true;
        Registry.View<ck::FFragment_CompassGym_Mirror>().ForEach(
            [&](FCk_Entity Entity,
                const ck::FFragment_CompassGym_Mirror& State) {
                (void)State;
                Empty &= !Allocated(FCk_Handle{Entity, RegistryHandle});
            });
        return Empty;
    }

    FGameplayTag Tag(const TCHAR* Name)
    { return FGameplayTag::RequestGameplayTag(FName(Name), false); }

    FLinearColor SourceColor(ECkCompassGym_SourceKind Kind,
        const FGameplayTag& Category)
    {
        if (Kind == ECkCompassGym_SourceKind::Waypoint)
        { return FLinearColor(0.7f, 0.35f, 0.95f); }
        const auto Name = Category.GetTagName();
        if (Name == TEXT("Poi.Category.Quest"))
        { return FLinearColor(1.0f, 0.78f, 0.25f); }
        if (Name == TEXT("Poi.Category.Shop"))
        { return FLinearColor(0.95f, 0.5f, 0.2f); }
        if (Name == TEXT("Poi.Category.Danger"))
        { return FLinearColor(0.9f, 0.25f, 0.25f); }
        return FLinearColor(0.35f, 0.6f, 0.95f);
    }

    bool AddSource(FRun& Run, int64 Id, ECkCompassGym_SourceKind Kind,
        const TCHAR* CategoryName, const FVector& Location, int32 Priority)
    {
        FCkCompassGym_SourceReceipt R;
        R.CatalogId = Id;
        R.Kind = Kind;
        R.ExpectedCategory = Tag(CategoryName);
        if (!R.ExpectedCategory.IsValid()) { return false; }
        R.Host = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run.SourceRoot);
        if (!Allocated(R.Host)) { return false; }
        const auto Transform = UCk_Utils_Transform_UE::Add(R.Host,
            FTransform(FRotator::ZeroRotator, Location),
            ECk_Replication::DoesNotReplicate);
        if (!Allocated(Transform)) { return false; }
        const auto Poi = UCk_Utils_Poi_UE::Add(
            R.Host, FCk_Poi_Spec(R.ExpectedCategory));
        if (!Allocated(Poi)) { return false; }
        auto Spec = FCk_PoiDisplayDefinition_Spec(Tag(TEXT("Poi.Consumer.Compass")));
        Spec.Set_Priority(Priority).Set_OffscreenPolicy(
            Kind == ECkCompassGym_SourceKind::Waypoint ?
                ECk_Poi_OffscreenPolicy::ClampToEdge :
                ECk_Poi_OffscreenPolicy::Hide);
        if (!Allocated(UCk_Utils_PoiDisplayDefinition_UE::Add(R.Host, Spec)))
        { return false; }
        const float Radius = Kind == ECkCompassGym_SourceKind::Waypoint ?
            55.0f : 40.0f;
        R.Marker = UCk_Utils_Pmg_BasicShapes::Create_Sphere(R.Host,
            FTransform(FRotator::ZeroRotator, Location + FVector(0, 0, 60)),
            Radius, 12, 12, ECk_Plane_Axis::XY,
            SourceColor(Kind, R.ExpectedCategory), true, 2.0f, -1.0f);
        if (!Allocated(R.Marker)) { return false; }
        Run.Base.Add(R);
        return true;
    }

    bool BuildBase(FRun& Run, UWorld* World)
    {
        Run.SourceGameState = World->GetGameState();
        Run.SourceWorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!Allocated(Run.SourceWorldRoot)) { return false; }
        Run.SourceRoot = UCk_Utils_EntityLifetime_UE::
            Request_CreateEntity(Run.SourceWorldRoot);
        if (!Allocated(Run.SourceRoot)) { return false; }
        const TCHAR* Categories[8] = {
            TEXT("Poi.Category.Quest"), TEXT("Poi.Category.Shop"),
            TEXT("Poi.Category.Danger"), TEXT("Poi.Category.Info"),
            TEXT("Poi.Category.Quest"), TEXT("Poi.Category.Shop"),
            TEXT("Poi.Category.Danger"), TEXT("Poi.Category.Info") };
        const FVector2D Offsets[8] = {
            {600, 0}, {424.3, 424.3}, {0, 600}, {-424.3, 424.3},
            {-600, 0}, {-424.3, -424.3}, {0, -600}, {424.3, -424.3} };
        const FVector Center(-800, 0, 100);
        for (int32 I = 0; I < 8; ++I)
        {
            if (!AddSource(Run, I + 1, ECkCompassGym_SourceKind::Ring,
                    Categories[I], Center +
                        FVector(Offsets[I].X, Offsets[I].Y, 0), I + 1))
            { return false; }
        }
        return AddSource(Run, 9, ECkCompassGym_SourceKind::Waypoint,
            TEXT("Poi.Category.Waypoint"), Center + FVector(-2500, 0, 0),
            10) && Run.Base.Num() == 9;
    }

    bool BuildStress(FRun& Run)
    {
        const auto Category = Tag(TEXT("Poi.Category.Stress"));
        if (!Category.IsValid() || Run.Base.Num() != 9 ||
            !Run.Stress.IsEmpty()) { return false; }
        Run.Stress.Reserve(500);
        for (int32 I = 0; I < 500; ++I)
        {
            FCkCompassGym_SourceReceipt R;
            R.CatalogId = 10000 + I;
            R.Kind = ECkCompassGym_SourceKind::Stress;
            R.ExpectedCategory = Category;
            R.Host = UCk_Utils_EntityLifetime_UE::
                Request_CreateEntity(Run.SourceRoot);
            Run.Stress.Add(R);
            if (!Allocated(R.Host)) { return false; }
            const auto Transform = UCk_Utils_Transform_UE::Add(R.Host,
                FTransform(FRotator::ZeroRotator,
                    FVector(10.0 * I, 5000.0, 100.0)),
                ECk_Replication::DoesNotReplicate);
            if (!Allocated(Transform)) { return false; }
            const auto Tagged = UCk_Utils_EntityTag_UE::
                Add_UsingGameplayTag(R.Host, Category);
            if (!Allocated(Tagged) || R.Host.Has<ck::FTag_Poi>())
            { return false; }
        }
        return Run.Stress.Num() == 500;
    }

    bool StageRealPing(FRun& Run, UWorld* World)
    {
        auto& R = Run.Ping;
        R.CatalogId = 10;
        R.Kind = ECkCompassGym_SourceKind::Ping;
        R.ExpectedCategory = Tag(TEXT("Poi.Category.Ping"));
        R.Host = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run.SourceRoot);
        if (!Allocated(R.Host) || !R.ExpectedCategory.IsValid()) { return false; }
        const FVector Location(1450, 0, 100);
        const auto Transform = UCk_Utils_Transform_UE::Add(R.Host,
            FTransform(FRotator::ZeroRotator, Location),
            ECk_Replication::DoesNotReplicate);
        if (!Allocated(Transform)) { return false; }
        const auto Tagged = UCk_Utils_EntityTag_UE::Add_UsingGameplayTag(
            R.Host, R.ExpectedCategory);
        if (!Allocated(Tagged)) { return false; }
        auto Spec = FCk_PoiDisplayDefinition_Spec(Tag(TEXT("Poi.Consumer.Compass")));
        Spec.Set_Priority(20).Set_OffscreenPolicy(
            ECk_Poi_OffscreenPolicy::ClampToEdge);
        const auto Display = UCk_Utils_PoiDisplayDefinition_UE::Add(R.Host, Spec);
        if (!Allocated(Display)) { return false; }
        R.Marker = UCk_Utils_Pmg_BasicShapes::Create_Sphere(R.Host,
            FTransform(FRotator::ZeroRotator, Location + FVector(0, 0, 60)),
            45.0f, 12, 12, ECk_Plane_Axis::XY,
            FLinearColor(0.3f, 0.9f, 0.95f), true, 2.0f, -1.0f);
        if (!Allocated(R.Marker)) { return false; }
        auto TimerSpec = FCk_Timer_Spec(FCk_Time(5.0f));
        TimerSpec.Set_Behavior(ECk_Timer_Behavior::StopOnDone)
            .Set_StartingState(ECk_Timer_State::Running)
            .Set_CountDirection(ECk_Timer_CountDirection::CountUp);
        R.Timer = UCk_Utils_Timer_UE::Add(R.Host, TimerSpec);
        if (!Allocated(R.Timer) || R.Host.Has<ck::FTag_Poi>())
        { return false; }
        Run.SourcePingProbe.Reset(NewObject<UCkCompassGym_PingTtlProbe>());
        if (!IsValid(Run.SourcePingProbe.Get())) { return false; }
        Run.SourcePingDone.BindDynamic(Run.SourcePingProbe.Get(),
            &UCkCompassGym_PingTtlProbe::OnDone);
        auto TypedTimer = ck::StaticCast<FCk_Handle_Timer>(R.Timer);
        const auto Bound = UCk_Utils_Timer_UE::BindTo_OnDone(TypedTimer,
            Run.SourcePingDone,
            ECk_Signal_BindingPolicy::IgnorePayloadInFlight);
        if (Bound != R.Timer || !Run.SourcePingDone.IsBound() ||
            !Run.SourcePingProbe->ArmJournal(World,
                Run.SourceGameState.Get(), Run.SourceRoot, R))
        { return false; }
        FString Why;
        return UCkCompassGym_SourceMembership::TryStage_Ping(
            World, Run.SourceGameState.Get(), Run.SourceRoot, R, Why) &&
            Why.IsEmpty();
    }

    bool BindClient(FClient& Client, UWorld* World)
    {
        if (!IsValid(World) || !World->IsNetMode(NM_Client) ||
            !IsValid(World->GetGameState())) { return false; }
        Client.World = World;
        Client.GameState = World->GetGameState();
        Client.WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!Allocated(Client.WorldRoot)) { return false; }
        Client.MirrorRoot = UCk_Utils_EntityLifetime_UE::
            Request_CreateEntity(Client.WorldRoot);
        return Allocated(Client.MirrorRoot);
    }

    bool SourceRows(UWorld* World, const FRun& Run, int32 Count,
        TArray<FCkCompassGym_SourceRow>& OutRows, int64& OutRevision)
    {
        FString Why;
        OutRevision = 0;
        OutRows.Reset();
        return UCkCompassGym_SourceMembership::TryGet_CurrentRows(
            World, Run.SourceGameState.Get(), Run.SourceRoot,
            OutRevision, OutRows, Why) && OutRows.Num() == Count &&
            OutRevision > 0 && Why.IsEmpty();
    }

    FCkCompassGym_WireSnapshot MakeWire(
        const TArray<FCkCompassGym_SourceRow>& Source, int64 Sequence)
    {
        FCkCompassGym_WireSnapshot Wire;
        Wire.SourceGeneration = 1;
        Wire.Sequence = Sequence;
        Wire.Lifecycle = ECkCompassGym_WireLifecycle::Active;
        Wire.DeclaredRowCount = Source.Num();
        Wire.Rows = Source;
        for (auto& Row : Wire.Rows) { Row.Marker.Lines.Reset(); }
        return Wire;
    }

    bool QueueBoth(FRun& Run, const FCkCompassGym_WireSnapshot& Wire)
    {
        for (auto& Client : Run.Clients)
        {
            FString Why;
            if (!UCkCompassGym_Mirror::TryQueue_Snapshot(
                    Client.World.Get(), Client.GameState.Get(),
                    Client.MirrorRoot, Wire, Why) || !Why.IsEmpty())
            { return false; }
        }
        return true;
    }

    bool Installed(FClient& Client, int32 Count, int64 Sequence,
        bool RequireIdle)
    {
        if (!IsValid(Client.World.Get()) ||
            !Allocated(Client.MirrorRoot)) { return false; }
        int64 Generation = 0, ActualSequence = 0;
        TArray<FCkCompassGym_SourceRow> Rows;
        FString Why;
        if (!UCkCompassGym_Mirror::TryGet_Installed(
                Client.World.Get(), Client.GameState.Get(),
                Client.MirrorRoot, Generation, ActualSequence, Rows, Why) ||
            Generation != 1 || ActualSequence != Sequence ||
            Rows.Num() != Count || !Why.IsEmpty()) { return false; }
        bool Busy = false, Failed = false;
        if (!UCkCompassGym_Mirror::TryGet_Status(Client.World.Get(),
                Client.GameState.Get(), Client.MirrorRoot,
                Busy, Failed, Why) || Failed || (RequireIdle && Busy))
        { return false; }
        int32 Markers = 0, Stress = 0, Pings = 0;
        for (const auto& Row : Rows)
        {
            if (Row.HasMarker)
            { if (Row.Marker.Lines.Num() != 96) { return false; } ++Markers; }
            if (Row.Kind == ECkCompassGym_SourceKind::Stress) { ++Stress; }
            if (Row.Kind == ECkCompassGym_SourceKind::Ping) { ++Pings; }
        }
        if (Markers != 9 + Pings ||
            Stress != (Count >= 509 ? 500 : 0) ||
            Pings != (Count == 10 || Count == 510 ? 1 : 0))
        { return false; }
        if (!Allocated(Client.MirrorRoot) || !ck::IsValid(Client.MirrorRoot) ||
            !Client.MirrorRoot.Has<ck::FFragment_CompassGym_Mirror>())
        { return false; }
        const auto& State = Client.MirrorRoot.Get<ck::FFragment_CompassGym_Mirror>();
        if (State.Installed.Num() != Count) { return false; }
        for (const auto& E : State.Installed)
        {
            if (!Allocated(E.Host) || !E.Host.Has<ck::FTag_Poi>() ||
                E.Host.Has<ck::FFragment_Timer_Params>() ||
                (E.Row.HasMarker && !Allocated(E.Marker)))
            { return false; }
        }
        return true;
    }

    bool CaptureBase(FClient& Client)
    {
        Client.BaseHosts.Reset();
        if (!Allocated(Client.MirrorRoot) || !ck::IsValid(Client.MirrorRoot) ||
            !Client.MirrorRoot.Has<ck::FFragment_CompassGym_Mirror>())
        { return false; }
        const auto& State = Client.MirrorRoot.Get<ck::FFragment_CompassGym_Mirror>();
        for (const auto& E : State.Installed)
        {
            if (E.Row.CatalogId >= 1 && E.Row.CatalogId <= 9)
            {
                if (!Allocated(E.Host) || !ck::IsValid(E.Host))
                { Client.BaseHosts.Reset(); return false; }
                Client.BaseHosts.Add(E.Host);
            }
        }
        if (Client.BaseHosts.Num() != 9)
        { Client.BaseHosts.Reset(); return false; }
        return true;
    }

    bool StableBase(const FClient& Client)
    {
        if (Client.BaseHosts.Num() != 9) { return false; }
        if (!Allocated(Client.MirrorRoot) || !ck::IsValid(Client.MirrorRoot) ||
            !Client.MirrorRoot.Has<ck::FFragment_CompassGym_Mirror>())
        { return false; }
        const auto& State = Client.MirrorRoot.Get<ck::FFragment_CompassGym_Mirror>();
        for (int32 I = 0; I < 9; ++I)
        {
            int32 Matches = 0;
            for (const auto& E : State.Installed)
            { Matches += E.Row.CatalogId == I + 1 && E.Host == Client.BaseHosts[I] ? 1 : 0; }
            if (Matches != 1) { return false; }
        }
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCompassGym_MirrorBaseStress,
    "Ck.Compass.Gym.MirrorBaseStress.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCompassGym_MirrorBaseStress::RunTest(const FString& Parameters)
{
    using namespace ck_compass_mirror_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported mirror topology")); return false; }
    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            if (!IsValid(Server) || !IsValid(Client0) || !IsValid(Client1) ||
                !IsValid(Server->GetGameState()) ||
                !IsValid(Client0->GetGameState()) ||
                !IsValid(Client1->GetGameState()) ||
                Server == Client0 || Server == Client1 || Client0 == Client1 ||
                !Client0->IsNetMode(NM_Client) || !Client1->IsNetMode(NM_Client))
            { return false; }
            for (auto* World : {Server, Client0, Client1})
            {
                if (!Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                        TryGet_TransientEntityForWorld(*World)))
                { return false; }
            }
            return true;
        }), 60.0, TEXT("two actual NM_Client roots and authority ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            Run->Failed = !BuildBase(*Run, World) ||
                !BindClient(Run->Clients[0],
                    ck::auto_test::net::Get_ClientWorld(0)) ||
                !BindClient(Run->Clients[1],
                    ck::auto_test::net::Get_ClientWorld(1));
            TestTrue(TEXT("real source Base9 and distinct client mirror roots authored"),
                !Run->Failed && Run->Base.Num() == 9 &&
                Run->Clients[0].MirrorRoot != Run->Clients[1].MirrorRoot &&
                NoMirrorInAuthorityWorld(World));
            if (Run->Failed) { return; }
            FCkCompassGym_WireSnapshot Default;
            FString Why;
            const bool InvalidRoot = !UCkCompassGym_Mirror::TryQueue_Snapshot(
                Run->Clients[0].World.Get(), Run->Clients[0].GameState.Get(),
                {}, Default, Why) && !Why.IsEmpty();
            TArray<FCk_Handle> Children;
            const bool NoPrefix = UCk_EntityScriptGym_Spawn_Runtime_UE::
                TryGet_AllocatedDirectLifetimeDependents(
                    Run->Clients[0].MirrorRoot,
                    Run->Clients[0].WorldRoot, Children) && Children.IsEmpty();
            TestTrue(TEXT("default mirror root rejects without creating local membership"),
                InvalidRoot && NoPrefix &&
                !Run->Clients[0].MirrorRoot.Has<ck::FFragment_CompassGym_Mirror>());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Why;
            return UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
                World, Run->SourceGameState.Get(), Run->SourceRoot,
                true, Run->Base, Rows, Why) ==
                ECkCompassGym_CatalogOutcome::Complete && Rows.Num() == 9;
        }), 60.0, TEXT("actual authority Base9 physical source settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            FString Why;
            const bool Bound = UCkCompassGym_SourceMembership::
                TryInitialize_BaseCohort(World, Run->SourceGameState.Get(),
                    Run->SourceRoot, Run->Base, Why);
            TestTrue(TEXT("whole authority Base9 enters ordered membership"),
                Bound && Why.IsEmpty());
            Run->Failed = !Bound;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            return SourceRows(World, *Run, 9, Rows, Revision);
        }), 30.0, TEXT("authority publishes Base9 source values")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            const bool Captured = SourceRows(World, *Run, 9, Rows, Revision);
            Run->LastSourceRevision = Revision;
            const auto Wire = MakeWire(Rows, Revision);
            const bool Queued = Captured && QueueBoth(*Run, Wire);
            bool BothBusy = Queued;
            for (auto& Client : Run->Clients)
            {
                bool Busy = false, Failed = false;
                FString Why;
                int64 Generation = -1, Sequence = -1;
                TArray<FCkCompassGym_SourceRow> InstalledRows;
                BothBusy &= UCkCompassGym_Mirror::TryGet_Status(
                    Client.World.Get(), Client.GameState.Get(),
                    Client.MirrorRoot, Busy, Failed, Why) && Busy && !Failed &&
                    !UCkCompassGym_Mirror::TryGet_Installed(
                        Client.World.Get(), Client.GameState.Get(),
                        Client.MirrorRoot, Generation, Sequence,
                        InstalledRows, Why) && Generation == 0 &&
                    Sequence == 0 && InstalledRows.IsEmpty();
            }
            TestTrue(TEXT("authority-produced compact wire queues whole client Base9 without prefix"),
                Queued && BothBusy);
            Run->Failed = !Queued || !BothBusy;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            return Installed(Run->Clients[0], 9, Run->LastSourceRevision, true) &&
                Installed(Run->Clients[1], 9, Run->LastSourceRevision, true);
        }), 60.0, TEXT("both local physical Base9 cohorts installed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            const bool CapturedBase = CaptureBase(Run->Clients[0]) &&
                CaptureBase(Run->Clients[1]);
            TestTrue(TEXT("both settled client Base9 Host receipts captured"),
                CapturedBase);
            if (!CapturedBase) { Run->Failed = true; return; }
            FString Why;
            const bool Staged = BuildStress(*Run);
            const bool Accepted = Staged && UCkCompassGym_SourceMembership::
                TryRequest_StressAdmission(World, Run->SourceGameState.Get(),
                    Run->SourceRoot, Run->Base, Run->Stress, 1, Why);
            TestTrue(TEXT("authority stages exactly 500 real unexposed Stress hosts"),
                Accepted && Why.IsEmpty() &&
                Run->Clients[0].BaseHosts.Num() == 9 &&
                Run->Clients[1].BaseHosts.Num() == 9);
            Run->Failed = !Accepted;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Rows, Historical;
            TArray<FCkCompassGym_SourceReceipt> Retirees;
            FString Why, ResultWhy;
            int64 Revision = 0;
            return SourceRows(World, *Run, 509, Rows, Revision) &&
                UCkCompassGym_SourceMembership::Get_Result(
                    World, Run->SourceGameState.Get(), Run->SourceRoot,
                    1, Historical, Retirees, ResultWhy) ==
                    ECkCompassGym_MembershipResult::Committed &&
                Historical.Num() == 509 && Retirees.IsEmpty() &&
                Revision > Run->LastSourceRevision;
        }), 60.0, TEXT("authority commits whole 509 before forwarding wire")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            const bool Captured = SourceRows(World, *Run, 509, Rows, Revision);
            Run->LastSourceRevision = Revision;
            const bool Queued = Captured && QueueBoth(*Run,
                MakeWire(Rows, Revision));
            TestTrue(TEXT("whole 509 authority values queue to both clients"), Queued);
            Run->Failed = !Queued;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            return Installed(Run->Clients[0], 509, Run->LastSourceRevision, true) &&
                Installed(Run->Clients[1], 509, Run->LastSourceRevision, true) &&
                StableBase(Run->Clients[0]) && StableBase(Run->Clients[1]);
        }), 90.0, TEXT("both local physical 509 cohorts keep Base9 Host identities")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            bool SafeMirrors = true;
            for (auto& Client : Run->Clients)
            {
                SafeMirrors &= Installed(Client, 509,
                    Run->LastSourceRevision, true) &&
                    Allocated(Client.MirrorRoot) &&
                    ck::IsValid(Client.MirrorRoot) &&
                    Client.MirrorRoot.Has<ck::FFragment_CompassGym_Mirror>();
            }
            TestTrue(TEXT("whole 509 local receipts remain live before withdrawal capture"),
                SafeMirrors);
            if (!SafeMirrors) { Run->Failed = true; return; }
            for (auto& Client : Run->Clients)
            {
                const auto& State = Client.MirrorRoot.Get<
                    ck::FFragment_CompassGym_Mirror>();
                Client.DepartedHosts.Reset();
                for (const auto& E : State.Installed)
                { if (E.Row.Kind == ECkCompassGym_SourceKind::Stress)
                    { Client.DepartedHosts.Add(E.Host); } }
            }
            TArray<FCkCompassGym_SourceReceipt> Active = Run->Base;
            Active.Append(Run->Stress);
            FString Why;
            const bool Requested = UCkCompassGym_SourceMembership::
                TryRequest_StressWithdrawal(World, Run->SourceGameState.Get(),
                    Run->SourceRoot, Active, 2, Why);
            TestTrue(TEXT("authority queues exact 500 Stress withdrawal"),
                Requested && Why.IsEmpty() &&
                Run->Clients[0].DepartedHosts.Num() == 500 &&
                Run->Clients[1].DepartedHosts.Num() == 500);
            Run->Failed = !Requested;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Rows, Historical;
            TArray<FCkCompassGym_SourceReceipt> Frozen;
            FString Why;
            int64 Revision = 0;
            return SourceRows(World, *Run, 9, Rows, Revision) &&
                UCkCompassGym_SourceMembership::Get_Result(
                    World, Run->SourceGameState.Get(), Run->SourceRoot,
                    2, Historical, Frozen, Why) ==
                    ECkCompassGym_MembershipResult::Committed &&
                Historical.Num() == 9 && Frozen.Num() == 500 &&
                Revision > Run->LastSourceRevision;
        }), 60.0, TEXT("authority publishes exact Base9 withdrawal frame")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            TArray<FCkCompassGym_SourceRow> Rows, Historical;
            TArray<FCkCompassGym_SourceReceipt> Frozen;
            FString Why;
            int64 Revision = 0;
            const bool Source = SourceRows(World, *Run, 9, Rows, Revision);
            const auto Result = UCkCompassGym_SourceMembership::Get_Result(
                World, Run->SourceGameState.Get(), Run->SourceRoot,
                2, Historical, Frozen, Why);
            Run->FrozenStress = Frozen;
            Run->LastSourceRevision = Revision;
            const bool Queued = Source &&
                Result == ECkCompassGym_MembershipResult::Committed &&
                Frozen.Num() == 500 && QueueBoth(*Run, MakeWire(Rows, Revision));
            TestTrue(TEXT("both client mirrors accept nine-only withdrawal frame"), Queued);
            Run->Failed = !Queued;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            if (!Installed(Run->Clients[0], 9, Run->LastSourceRevision, true) ||
                !Installed(Run->Clients[1], 9, Run->LastSourceRevision, true) ||
                !StableBase(Run->Clients[0]) || !StableBase(Run->Clients[1]))
            { return false; }
            for (const auto& Client : Run->Clients)
            { for (const auto& H : Client.DepartedHosts)
                { if (Allocated(H)) { return false; } } }
            for (const auto& R : Run->FrozenStress)
            { if (Allocated(R.Host)) { return false; } }
            Run->RetirePhysicallyComplete = true;
            return true;
        }), 90.0, TEXT("local and source 500 Stress leaves physically retire")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool {
            if (Run->Failed || Run->RetirePhysicallyComplete) { return true; }
            AddError(RetireFailureSummary(*Run));
            return true;
        }), TEXT("exact mirror retirement failure summary"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            TestTrue(TEXT("9 to 509 to 9 stays stable and complete in both actual clients"),
                !Run->Failed && StableBase(Run->Clients[0]) &&
                StableBase(Run->Clients[1]) &&
                Run->FrozenStress.Num() == 500 &&
                NoMirrorInAuthorityWorld(World));
            if (Run->Failed) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            const bool Source = SourceRows(World, *Run, 9, Rows, Revision);
            TestTrue(TEXT("terminal wire probes start from whole current Base9"),
                Source && Rows.Num() == 9 && Revision == Run->LastSourceRevision);
            if (!Source || Rows.Num() != 9 ||
                Revision != Run->LastSourceRevision)
            { Run->Failed = true; return; }
            auto Stale = MakeWire(Rows, Revision - 1);
            auto Changed = MakeWire(Rows, Revision + 1);
            ++Changed.Rows[0].Priority;
            TArray<FCkCompassGym_SourceRow> Expanded;
            const bool BothValueValid = Source &&
                UCkCompassGym_Wire::TryValidateAndExpand(Stale, Expanded) &&
                UCkCompassGym_Wire::TryValidateAndExpand(Changed, Expanded);
            FString StaleWhy, ChangedWhy;
            const bool StaleAccepted = BothValueValid &&
                UCkCompassGym_Mirror::TryQueue_Snapshot(
                    Run->Clients[0].World.Get(),
                    Run->Clients[0].GameState.Get(),
                    Run->Clients[0].MirrorRoot, Stale, StaleWhy);
            const bool ChangedAccepted = BothValueValid &&
                UCkCompassGym_Mirror::TryQueue_Snapshot(
                    Run->Clients[1].World.Get(),
                    Run->Clients[1].GameState.Get(),
                    Run->Clients[1].MirrorRoot, Changed, ChangedWhy);
            bool BothClosed = BothValueValid && !StaleAccepted &&
                !ChangedAccepted && !StaleWhy.IsEmpty() &&
                !ChangedWhy.IsEmpty();
            for (auto& Client : Run->Clients)
            {
                bool Busy = true, Failed = false;
                FString Why;
                int64 Generation = -1, Sequence = -1;
                TArray<FCkCompassGym_SourceRow> InstalledRows;
                TArray<FCk_Handle> Children;
                InstalledRows.AddDefaulted();
                BothClosed &= UCkCompassGym_Mirror::TryGet_Status(
                    Client.World.Get(), Client.GameState.Get(),
                    Client.MirrorRoot, Busy, Failed, Why) &&
                    !Busy && Failed &&
                    UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                        Client.WorldRoot) &&
                    !UCkCompassGym_Mirror::TryGet_Installed(
                        Client.World.Get(), Client.GameState.Get(),
                        Client.MirrorRoot, Generation, Sequence,
                        InstalledRows, Why) &&
                    Generation == 0 && Sequence == 0 &&
                    InstalledRows.IsEmpty() &&
                    UCk_EntityScriptGym_Spawn_Runtime_UE::
                        TryGet_AllocatedDirectLifetimeDependents(
                            Client.MirrorRoot, Client.WorldRoot, Children) &&
                    Children.Num() == 9;
            }
            TestTrue(TEXT("stale and changed retained-ID wires terminally close only their exact clients"),
                BothClosed);
            if (Allocated(Run->SourceRoot))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->SourceRoot); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            TestFalse(TEXT("BaseStress fixture never entered an early failure path"),
                Run->Failed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

namespace ck_compass_mirror_test
{
    struct FDeadlineRun
    {
        FRun Source;
        TArray<FCk_Handle> UnrequestedSuffix;
        TArray<FCkCompassGym_SourceRow> LastNine;
        int32 CursorAtFault = -1;
        int64 WithdrawalRevision = 0;
        bool Failed = false;
    };

    bool Uninitiated(const FCk_Handle& H)
    {
        return Allocated(H) && ck::IsValid(H) &&
            !H.Has_Any<ck::FTag_DestroyEntity_Initiate,
                ck::FTag_DestroyEntity_EndPlay,
                ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await,
                ck::FTag_DestroyEntity_Finalize>();
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCompassGym_MirrorSourceDeadline,
    "Ck.Compass.Gym.MirrorSourceDeadline.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCompassGym_MirrorSourceDeadline::RunTest(const FString& Parameters)
{
    using namespace ck_compass_mirror_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported mirror topology")); return false; }
    const auto Run = MakeShared<FDeadlineRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            if (!IsValid(Server) || !IsValid(Client0) || !IsValid(Client1) ||
                !IsValid(Server->GetGameState()) ||
                !IsValid(Client0->GetGameState()) ||
                !IsValid(Client1->GetGameState()) ||
                Server == Client0 || Server == Client1 || Client0 == Client1 ||
                !Client0->IsNetMode(NM_Client) || !Client1->IsNetMode(NM_Client))
            { return false; }
            for (auto* World : {Server, Client0, Client1})
            {
                if (!Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                        TryGet_TransientEntityForWorld(*World)))
                { return false; }
            }
            return true;
        }), 60.0, TEXT("deadline fixture has authority and two real clients")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            auto& S = Run->Source;
            const bool Built = BuildBase(S, World) &&
                BindClient(S.Clients[0], ck::auto_test::net::Get_ClientWorld(0)) &&
                BindClient(S.Clients[1], ck::auto_test::net::Get_ClientWorld(1)) &&
                NoMirrorInAuthorityWorld(World);
            TestTrue(TEXT("deadline source Base9 and two local mirror roots are real"),
                Built);
            Run->Failed = !Built;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Why;
            return UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
                World, Run->Source.SourceGameState.Get(),
                Run->Source.SourceRoot, true, Run->Source.Base, Rows, Why) ==
                ECkCompassGym_CatalogOutcome::Complete && Rows.Num() == 9;
        }), 60.0, TEXT("deadline source Base9 physically settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            FString Why;
            const bool Bound = UCkCompassGym_SourceMembership::
                TryInitialize_BaseCohort(World,
                    Run->Source.SourceGameState.Get(),
                    Run->Source.SourceRoot, Run->Source.Base, Why);
            TestTrue(TEXT("deadline source Base9 enters real journal"),
                Bound && Why.IsEmpty());
            Run->Failed = !Bound;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            return SourceRows(World, Run->Source, 9, Rows, Revision);
        }), 30.0, TEXT("deadline source publishes Base9")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            const bool Queued = SourceRows(World, Run->Source, 9,
                Rows, Revision) && QueueBoth(Run->Source, MakeWire(Rows, Revision));
            Run->Source.LastSourceRevision = Revision;
            TestTrue(TEXT("real authority Base9 wire reaches both client mirrors"),
                Queued);
            Run->Failed = !Queued;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            return Installed(Run->Source.Clients[0], 9,
                       Run->Source.LastSourceRevision, true) &&
                Installed(Run->Source.Clients[1], 9,
                       Run->Source.LastSourceRevision, true);
        }), 60.0, TEXT("both clients retain only physical Base9")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            auto& S = Run->Source;
            const bool BaseStable = CaptureBase(S.Clients[0]) &&
                CaptureBase(S.Clients[1]);
            FString Why;
            const bool Accepted = BaseStable && BuildStress(S) &&
                UCkCompassGym_SourceMembership::TryRequest_StressAdmission(
                    World, S.SourceGameState.Get(), S.SourceRoot,
                    S.Base, S.Stress, 1, Why);
            TestTrue(TEXT("real 500-source admission requested without client 500"),
                Accepted && Why.IsEmpty());
            Run->Failed = !Accepted;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Current, Historical;
            TArray<FCkCompassGym_SourceReceipt> Frozen;
            FString Why;
            int64 Revision = 0;
            return SourceRows(World, Run->Source, 509, Current, Revision) &&
                UCkCompassGym_SourceMembership::Get_Result(
                    World, Run->Source.SourceGameState.Get(),
                    Run->Source.SourceRoot, 1, Historical, Frozen, Why) ==
                    ECkCompassGym_MembershipResult::Committed &&
                Historical.Num() == 509 && Frozen.IsEmpty();
        }), 60.0, TEXT("real source commits 509 before withdrawal")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            auto& S = Run->Source;
            TArray<FCkCompassGym_SourceReceipt> Active = S.Base;
            Active.Append(S.Stress);
            FString Why;
            const bool Accepted = UCkCompassGym_SourceMembership::
                TryRequest_StressWithdrawal(World,
                    S.SourceGameState.Get(), S.SourceRoot, Active, 2, Why);
            TestTrue(TEXT("real source accepts exact 509-to-9 withdrawal"),
                Accepted && Why.IsEmpty());
            Run->Failed = !Accepted;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Current, Historical;
            TArray<FCkCompassGym_SourceReceipt> Frozen;
            FString Why;
            int64 Revision = 0;
            return SourceRows(World, Run->Source, 9, Current, Revision) &&
                UCkCompassGym_SourceMembership::Get_Result(
                    World, Run->Source.SourceGameState.Get(),
                    Run->Source.SourceRoot, 2, Historical, Frozen, Why) ==
                    ECkCompassGym_MembershipResult::Committed &&
                Historical.Num() == 9 && Frozen.Num() == 500 &&
                Revision > Run->Source.LastSourceRevision;
        }), 60.0, TEXT("real source commits nine-only withdrawal")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            auto& S = Run->Source;
            TArray<FCkCompassGym_SourceRow> Historical;
            TArray<FCkCompassGym_SourceReceipt> Frozen;
            FString Why;
            int64 Revision = 0;
            const bool Captured = SourceRows(World, S, 9,
                    Run->LastNine, Revision) &&
                UCkCompassGym_SourceMembership::Get_Result(
                    World, S.SourceGameState.Get(), S.SourceRoot, 2,
                    Historical, Frozen, Why) ==
                    ECkCompassGym_MembershipResult::Committed &&
                Historical.Num() == 9 && Frozen.Num() == 500 &&
                Allocated(S.SourceRoot) && ck::IsValid(S.SourceRoot) &&
                S.SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>();
            bool ExactSuffix = Captured;
            if (Captured)
            {
                auto& State = S.SourceRoot.Get<
                    ck::FFragment_CompassGym_SourceMembership>();
                Run->CursorAtFault = State.FrozenStressCursor;
                Run->WithdrawalRevision = Revision;
                ExactSuffix &= State.JournalEnabled &&
                    State.Result == ECkCompassGym_MembershipResult::Committed &&
                    State.Serial == 2 && State.FrozenRetiring.Num() == 500 &&
                    Run->CursorAtFault >= 0 && Run->CursorAtFault < 500 &&
                    State.FrozenStressDeadlineSeconds > FPlatformTime::Seconds();
                if (ExactSuffix)
                {
                    for (int32 I = Run->CursorAtFault; I < 500; ++I)
                    {
                        const auto& Receipt = State.FrozenRetiring[I];
                        if (Receipt.CatalogId != Frozen[I].CatalogId ||
                            Receipt.Kind != Frozen[I].Kind ||
                            Receipt.Host != Frozen[I].Host ||
                            Receipt.Marker != Frozen[I].Marker ||
                            Receipt.Timer != Frozen[I].Timer ||
                            Receipt.ExpectedCategory != Frozen[I].ExpectedCategory ||
                            Receipt.Admitted != Frozen[I].Admitted ||
                            !Uninitiated(Receipt.Host))
                        { ExactSuffix = false; break; }
                        Run->UnrequestedSuffix.Add(Receipt.Host);
                    }
                    ExactSuffix &= !Run->UnrequestedSuffix.IsEmpty();
                }
                if (ExactSuffix)
                { State.FrozenStressDeadlineSeconds = FPlatformTime::Seconds() - 1.0; }
            }
            TestTrue(TEXT("accepted journal has nonempty unrequested suffix; only deadline is expired"),
                ExactSuffix);
            Run->Failed = !ExactSuffix;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            const auto Root = Run->Source.SourceRoot;
            if (!Allocated(Root) || !ck::IsValid(Root) ||
                !Root.Has<ck::FFragment_CompassGym_SourceMembership>())
            { return false; }
            const auto& State = Root.Get<
                ck::FFragment_CompassGym_SourceMembership>();
            return State.Result == ECkCompassGym_MembershipResult::Rejected &&
                State.ClosedUnproven;
        }), 15.0, TEXT("expired accepted source deadline terminally rejects")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            auto& S = Run->Source;
            const bool RootSafe = Allocated(S.SourceRoot) &&
                ck::IsValid(S.SourceRoot) &&
                S.SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>();
            TestTrue(TEXT("rejected source journal remains inspectable"), RootSafe);
            if (!RootSafe) { Run->Failed = true; return; }
            const auto& State = S.SourceRoot.Get<
                ck::FFragment_CompassGym_SourceMembership>();
            bool SuffixUntouched = Run->CursorAtFault >= 0 &&
                State.FrozenStressCursor == Run->CursorAtFault &&
                State.FrozenRetiring.Num() == 500 &&
                Run->UnrequestedSuffix.Num() == 500 - Run->CursorAtFault;
            for (const auto& Host : Run->UnrequestedSuffix)
            { SuffixUntouched &= Uninitiated(Host); }
            TArray<FCkCompassGym_SourceRow> ResultRows, Current;
            TArray<FCkCompassGym_SourceReceipt> ResultFrozen;
            FString ResultWhy, CurrentWhy, RetryWhy;
            int64 CurrentRevision = -1;
            ResultRows.AddDefaulted();
            ResultFrozen.AddDefaulted();
            Current.AddDefaulted();
            const auto Result = UCkCompassGym_SourceMembership::Get_Result(
                World, S.SourceGameState.Get(), S.SourceRoot, 2,
                ResultRows, ResultFrozen, ResultWhy);
            const bool CurrentReadable = UCkCompassGym_SourceMembership::
                TryGet_CurrentRows(World, S.SourceGameState.Get(),
                    S.SourceRoot, CurrentRevision, Current, CurrentWhy);
            const bool Retried = UCkCompassGym_SourceMembership::
                TryRequest_StressAdmission(World, S.SourceGameState.Get(),
                    S.SourceRoot, S.Base, S.Stress, 3, RetryWhy);
            const bool Closed = UCk_Utils_Compass_UE::
                Get_IsWorldProjectionClosed(S.SourceWorldRoot) &&
                UCkGym_AdmissionRuntime_UE::Get_IsClosedUnproven(
                    S.SourceWorldRoot);
            const bool Terminal = State.Result ==
                    ECkCompassGym_MembershipResult::Rejected &&
                State.ClosedUnproven && State.Serial == 2 &&
                State.Failure.Contains(TEXT("Frozen Stress retirement timed out")) &&
                Result == ECkCompassGym_MembershipResult::Rejected &&
                ResultRows.IsEmpty() && ResultFrozen.IsEmpty() &&
                !ResultWhy.IsEmpty() && !CurrentReadable &&
                CurrentRevision == 0 && Current.IsEmpty() &&
                !CurrentWhy.IsEmpty() && !Retried && !RetryWhy.IsEmpty() &&
                Closed && SuffixUntouched;
            TestTrue(TEXT("timeout closes source/projection without advancing or initiating frozen suffix"),
                Terminal);
            Run->Failed = !Terminal;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            if (Run->Failed) { return; }
            auto FailedWire = FCkCompassGym_WireSnapshot{};
            FailedWire.SourceGeneration = 1;
            FailedWire.Sequence = Run->WithdrawalRevision + 1;
            FailedWire.Lifecycle = ECkCompassGym_WireLifecycle::Failed;
            FailedWire.DeclaredRowCount = 0;
            TArray<FCkCompassGym_SourceRow> Expanded;
            const bool ValidFailed =
                UCkCompassGym_Wire::TryValidateAndExpand(FailedWire, Expanded) &&
                Expanded.IsEmpty();
            bool BothClosed = ValidFailed;
            for (auto& Client : Run->Source.Clients)
            {
                FString Why, RetryWhy;
                const bool Queued = UCkCompassGym_Mirror::TryQueue_Snapshot(
                    Client.World.Get(), Client.GameState.Get(),
                    Client.MirrorRoot, FailedWire, Why);
                bool Busy = true, Failed = false;
                FString StatusWhy;
                const bool Status = UCkCompassGym_Mirror::TryGet_Status(
                    Client.World.Get(), Client.GameState.Get(),
                    Client.MirrorRoot, Busy, Failed, StatusWhy);
                auto Active = MakeWire(Run->LastNine, FailedWire.Sequence + 1);
                const bool Revived = UCkCompassGym_Mirror::TryQueue_Snapshot(
                    Client.World.Get(), Client.GameState.Get(),
                    Client.MirrorRoot, Active, RetryWhy);
                int64 Generation = -1, Sequence = -1;
                TArray<FCkCompassGym_SourceRow> Rows;
                Rows.AddDefaulted();
                FString ReadWhy;
                const bool Readable = UCkCompassGym_Mirror::TryGet_Installed(
                    Client.World.Get(), Client.GameState.Get(),
                    Client.MirrorRoot, Generation, Sequence, Rows, ReadWhy);
                TArray<FCk_Handle> Children;
                const bool NineChildren =
                    UCk_EntityScriptGym_Spawn_Runtime_UE::
                        TryGet_AllocatedDirectLifetimeDependents(
                            Client.MirrorRoot, Client.WorldRoot, Children) &&
                    Children.Num() == 9;
                BothClosed &= !Queued && !Why.IsEmpty() && Status &&
                    !Busy && Failed &&
                    UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                        Client.WorldRoot) && !Revived && !RetryWhy.IsEmpty() &&
                    !Readable && Generation == 0 && Sequence == 0 &&
                    Rows.IsEmpty() && NineChildren &&
                    StableBase(Client);
            }
            TestTrue(TEXT("newer valid Failed wire terminally closes both Base9 mirrors without recovery"),
                BothClosed);
            Run->Failed = !BothClosed;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            if (Run->Failed) { return; }
            const auto Root = Run->Source.SourceRoot;
            const bool RootSafe = Allocated(Root) && ck::IsValid(Root) &&
                Root.Has<ck::FFragment_CompassGym_SourceMembership>();
            bool StillTerminal = RootSafe;
            if (RootSafe)
            {
                const auto& State = Root.Get<
                    ck::FFragment_CompassGym_SourceMembership>();
                StillTerminal &= State.Result ==
                    ECkCompassGym_MembershipResult::Rejected &&
                    State.ClosedUnproven && State.Serial == 2 &&
                    State.FrozenStressCursor == Run->CursorAtFault;
            }
            for (const auto& Host : Run->UnrequestedSuffix)
            { StillTerminal &= Uninitiated(Host); }
            for (const auto& Client : Run->Source.Clients)
            {
                bool Busy = true, Failed = false;
                FString Why;
                StillTerminal &= UCkCompassGym_Mirror::TryGet_Status(
                    Client.World.Get(), Client.GameState.Get(),
                    Client.MirrorRoot, Busy, Failed, Why) &&
                    !Busy && Failed &&
                    UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                        Client.WorldRoot) && StableBase(Client);
            }
            TestTrue(TEXT("source suffix and both client closures remain terminal after another barrier"),
                StillTerminal);
            Run->Failed = !StillTerminal;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            TestFalse(TEXT("deadline fixture never entered an unexpected early failure"),
                Run->Failed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCompassGym_MirrorPingRetirement,
    "Ck.Compass.Gym.MirrorPingRetirement.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCompassGym_MirrorPingRetirement::RunTest(const FString& Parameters)
{
    using namespace ck_compass_mirror_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported mirror Ping topology")); return false; }
    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            return IsValid(Server) && IsValid(Client0) && IsValid(Client1) &&
                IsValid(Server->GetGameState()) &&
                IsValid(Client0->GetGameState()) &&
                IsValid(Client1->GetGameState()) &&
                Client0->IsNetMode(NM_Client) &&
                Client1->IsNetMode(NM_Client) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Server)) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Client0)) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Client1));
        }), 60.0, TEXT("Ping source and two actual client worlds ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            Run->Failed = !BuildBase(*Run, World) ||
                !BindClient(Run->Clients[0], ck::auto_test::net::Get_ClientWorld(0)) ||
                !BindClient(Run->Clients[1], ck::auto_test::net::Get_ClientWorld(1));
            TestTrue(TEXT("real Ping source base and two client roots authored"),
                !Run->Failed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Why;
            return UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
                World, Run->SourceGameState.Get(), Run->SourceRoot,
                true, Run->Base, Rows, Why) ==
                ECkCompassGym_CatalogOutcome::Complete && Rows.Num() == 9;
        }), 60.0, TEXT("Ping source base physical proof settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            FString Why;
            const bool Bound = UCkCompassGym_SourceMembership::
                TryInitialize_BaseCohort(World, Run->SourceGameState.Get(),
                    Run->SourceRoot, Run->Base, Why);
            TestTrue(TEXT("Ping source whole Base9 initialized"),
                Bound && Why.IsEmpty());
            Run->Failed = !Bound;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            return IsValid(World) && SourceRows(World, *Run, 9, Rows, Revision);
        }), 30.0, TEXT("Ping source initial nine current rows")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            const bool Source = SourceRows(World, *Run, 9, Rows, Revision);
            Run->LastSourceRevision = Revision;
            const bool Queued = Source && QueueBoth(*Run, MakeWire(Rows, Revision));
            TestTrue(TEXT("Ping baseline authority wire queued to both clients"), Queued);
            Run->Failed = !Queued;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            return Installed(Run->Clients[0], 9, Run->LastSourceRevision, true) &&
                Installed(Run->Clients[1], 9, Run->LastSourceRevision, true);
        }), 60.0, TEXT("Ping baseline physical client nine")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            const bool CapturedBase = CaptureBase(Run->Clients[0]) &&
                CaptureBase(Run->Clients[1]);
            TestTrue(TEXT("both Ping-client Base9 Host receipts captured"),
                CapturedBase);
            if (!CapturedBase) { Run->Failed = true; return; }
            const bool Staged = StageRealPing(*Run, World);
            TestTrue(TEXT("real authority Ping timer bound before ordered staging"),
                Staged && Run->Ping.Timer != FCk_Handle{} &&
                Run->SourcePingProbe.IsValid());
            Run->Failed = !Staged;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            if (!IsValid(World) ||
                Run->SourcePingProbe->Get_DoneCount() != 0)
            { Run->Failed = true; return true; }
            return SourceRows(World, *Run, 10, Rows, Revision) &&
                Revision > Run->LastSourceRevision;
        }), 25.0, TEXT("real authority timer Ping publishes ten before Done")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            const bool Source = SourceRows(World, *Run, 10, Rows, Revision);
            Run->LastSourceRevision = Revision;
            const bool Queued = Source && QueueBoth(*Run, MakeWire(Rows, Revision));
            TestTrue(TEXT("real timer Ping ten-value frame manually queued"), Queued);
            Run->Failed = !Queued;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            return Installed(Run->Clients[0], 10, Run->LastSourceRevision, true) &&
                Installed(Run->Clients[1], 10, Run->LastSourceRevision, true) &&
                StableBase(Run->Clients[0]) && StableBase(Run->Clients[1]);
        }), 30.0, TEXT("both client Ping mirrors install without local timer")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            if (Run->Failed) { return; }
            bool SafeMirrors = true;
            for (auto& Client : Run->Clients)
            {
                SafeMirrors &= Installed(Client, 10,
                    Run->LastSourceRevision, true) &&
                    Allocated(Client.MirrorRoot) &&
                    ck::IsValid(Client.MirrorRoot) &&
                    Client.MirrorRoot.Has<ck::FFragment_CompassGym_Mirror>();
            }
            TestTrue(TEXT("whole local Ping receipt remains live before callback binding"),
                SafeMirrors);
            if (!SafeMirrors) { Run->Failed = true; return; }
            for (auto& Client : Run->Clients)
            {
                const auto& State = Client.MirrorRoot.Get<
                    ck::FFragment_CompassGym_Mirror>();
                for (const auto& E : State.Installed)
                { if (E.Row.Kind == ECkCompassGym_SourceKind::Ping)
                    { Client.PingHost = E.Host; Client.PingMarker = E.Marker; } }
            }
            Run->RetirementProbe.Reset(
                NewObject<UCkCompassGym_MirrorRetirementProbe>());
            const bool Armed = IsValid(Run->RetirementProbe.Get()) &&
                Allocated(Run->Clients[0].PingHost) &&
                Allocated(Run->Clients[0].PingMarker) &&
                Allocated(Run->Clients[1].PingHost) &&
                Run->RetirementProbe->Arm(Run->Clients[0].World.Get(),
                    Run->Clients[0].MirrorRoot,
                    Run->Clients[0].PingHost, Run->Clients[0].PingMarker);
            TestTrue(TEXT("real first-client local Ping Marker destruction callback armed"),
                Armed);
            Run->Failed = !Armed;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            if (!IsValid(World)) { return false; }
            if (Run->SourcePingProbe->Get_Failed())
            { Run->Failed = true; return true; }
            return Run->SourcePingProbe->Get_DoneCount() == 1 &&
                SourceRows(World, *Run, 9, Rows, Revision) &&
                Revision > Run->LastSourceRevision &&
                !Allocated(Run->Ping.Host) &&
                !Allocated(Run->Ping.Marker) &&
                !Allocated(Run->Ping.Timer);
        }), 35.0, TEXT("real source Done retires timer and publishes nine")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            int64 Revision = 0;
            const bool Source = SourceRows(World, *Run, 9, Rows, Revision);
            Run->LastSourceRevision = Revision;
            const bool Queued = Source && QueueBoth(*Run, MakeWire(Rows, Revision));
            TestTrue(TEXT("real Ping removal frame reaches both client mirrors"), Queued);
            Run->Failed = !Queued;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            bool Busy = false, Failed = false;
            FString Why;
            const auto& Client = Run->Clients[0];
            return Run->RetirementProbe->Get_CallbackCount() == 1 &&
                UCkCompassGym_Mirror::TryGet_Status(Client.World.Get(),
                    Client.GameState.Get(), Client.MirrorRoot,
                    Busy, Failed, Why) && Failed && !Busy &&
                Installed(Run->Clients[1], 9, Run->LastSourceRevision, true) &&
                !Allocated(Run->Clients[1].PingHost) &&
                !Allocated(Run->Clients[1].PingMarker);
        }), 35.0, TEXT("first client fails after unknown child; peer Ping retires cleanly")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            if (Run->Failed) { return; }
            const auto& Bad = Run->Clients[0];
            const auto Unknown = Run->RetirementProbe->Get_UnknownChild();
            int64 Generation = -1, Sequence = -1;
            TArray<FCkCompassGym_SourceRow> Rows;
            Rows.AddDefaulted();
            FString Why;
            const bool InstalledFrame = UCkCompassGym_Mirror::TryGet_Installed(
                Bad.World.Get(), Bad.GameState.Get(), Bad.MirrorRoot,
                Generation, Sequence, Rows, Why);
            const bool ChildUntouched = Allocated(Unknown) && ck::IsValid(Unknown) &&
                Unknown.Has<ck::FFragment_LifetimeOwner>() &&
                Unknown.Get<ck::FFragment_LifetimeOwner>().Get_Entity() ==
                    Bad.PingHost &&
                !Unknown.Has_Any<ck::FTag_DestroyEntity_Initiate,
                    ck::FTag_DestroyEntity_EndPlay,
                    ck::FTag_DestroyEntity_Teardown,
                    ck::FTag_DestroyEntity_Await,
                    ck::FTag_DestroyEntity_Finalize>();
            TestTrue(TEXT("unexpected child stays uninitiated; exact client closes without prefix"),
                !Run->RetirementProbe->Get_Failed() && ChildUntouched &&
                Allocated(Bad.PingHost) &&
                UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(Bad.WorldRoot) &&
                !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Clients[1].WorldRoot) &&
                !InstalledFrame && Generation == 0 && Sequence == 0 &&
                Rows.IsEmpty() && !Why.IsEmpty() &&
                StableBase(Run->Clients[1]));
            if (Allocated(Run->SourceRoot))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->SourceRoot); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            TestFalse(TEXT("Ping fixture never swallowed an early source or mirror failure"),
                Run->Failed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCompassGym_MirrorMalformed,
    "Ck.Compass.Gym.MirrorMalformed.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCompassGym_MirrorMalformed::RunTest(const FString& Parameters)
{
    using namespace ck_compass_mirror_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported malformed mirror topology")); return false; }
    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            return IsValid(Server) && IsValid(Client0) && IsValid(Client1) &&
                IsValid(Server->GetGameState()) &&
                IsValid(Client0->GetGameState()) &&
                IsValid(Client1->GetGameState()) &&
                Client0->IsNetMode(NM_Client) && Client1->IsNetMode(NM_Client) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Server)) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Client0)) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Client1));
        }), 60.0, TEXT("malformed fixture actual source and clients ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            Run->Failed = !BuildBase(*Run, World) ||
                !BindClient(Run->Clients[0],
                    ck::auto_test::net::Get_ClientWorld(0)) ||
                !BindClient(Run->Clients[1],
                    ck::auto_test::net::Get_ClientWorld(1));
            TestTrue(TEXT("malformed test uses real SourceCatalog hosts"),
                !Run->Failed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Why;
            return UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
                World, Run->SourceGameState.Get(), Run->SourceRoot,
                true, Run->Base, Rows, Why) ==
                ECkCompassGym_CatalogOutcome::Complete && Rows.Num() == 9;
        }), 60.0, TEXT("malformed fixture authority value witness settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            TArray<FCkCompassGym_SourceRow> Actual;
            FString ReadWhy;
            const bool Source = UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
                World, Run->SourceGameState.Get(), Run->SourceRoot,
                true, Run->Base, Actual, ReadWhy) ==
                ECkCompassGym_CatalogOutcome::Complete && Actual.Num() == 9;
            TestTrue(TEXT("malformed inputs start from whole actual authority Base9"),
                Source && ReadWhy.IsEmpty());
            if (!Source || !ReadWhy.IsEmpty())
            { Run->Failed = true; return; }
            auto Truncated = MakeWire(Actual, 1);
            Truncated.Rows.Pop(); // DeclaredRowCount remains nine.
            auto BadKind = MakeWire(Actual, 1);
            BadKind.Rows[0].Kind = ECkCompassGym_SourceKind::Invalid;
            FString TruncatedWhy, KindWhy;
            const bool TruncatedAccepted = Source &&
                UCkCompassGym_Mirror::TryQueue_Snapshot(
                    Run->Clients[0].World.Get(),
                    Run->Clients[0].GameState.Get(),
                    Run->Clients[0].MirrorRoot, Truncated, TruncatedWhy);
            const bool KindAccepted = Source &&
                UCkCompassGym_Mirror::TryQueue_Snapshot(
                    Run->Clients[1].World.Get(),
                    Run->Clients[1].GameState.Get(),
                    Run->Clients[1].MirrorRoot, BadKind, KindWhy);
            bool Closed = Source && !TruncatedAccepted && !KindAccepted &&
                !TruncatedWhy.IsEmpty() && !KindWhy.IsEmpty();
            for (auto& Client : Run->Clients)
            {
                bool Busy = true, Failed = false;
                FString Why;
                int64 Generation = -1, Sequence = -1;
                TArray<FCkCompassGym_SourceRow> InstalledRows;
                TArray<FCk_Handle> Children;
                InstalledRows.AddDefaulted();
                const bool Retry = UCkCompassGym_Mirror::TryQueue_Snapshot(
                    Client.World.Get(), Client.GameState.Get(),
                    Client.MirrorRoot, MakeWire(Actual, 2), Why);
                Closed &= !Retry &&
                    UCkCompassGym_Mirror::TryGet_Status(Client.World.Get(),
                        Client.GameState.Get(), Client.MirrorRoot,
                        Busy, Failed, Why) && Failed && !Busy &&
                    UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                        Client.WorldRoot) &&
                    !UCkCompassGym_Mirror::TryGet_Installed(
                        Client.World.Get(), Client.GameState.Get(),
                        Client.MirrorRoot, Generation, Sequence,
                        InstalledRows, Why) &&
                    Generation == 0 && Sequence == 0 && InstalledRows.IsEmpty() &&
                    UCk_EntityScriptGym_Spawn_Runtime_UE::
                        TryGet_AllocatedDirectLifetimeDependents(
                            Client.MirrorRoot, Client.WorldRoot, Children) &&
                    Children.IsEmpty();
            }
            TestTrue(TEXT("truncated and invalid-kind values close exact client worlds without prefix or retry"),
                Closed);
            if (Allocated(Run->SourceRoot))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->SourceRoot); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            TestFalse(TEXT("Malformed fixture source stayed valid throughout"),
                Run->Failed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCompassGym_MirrorContextReplacement,
    "Ck.Compass.Gym.MirrorContextReplacement.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCompassGym_MirrorContextReplacement::RunTest(
    const FString& Parameters)
{
    using namespace ck_compass_mirror_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported mirror context topology")); return false; }
    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            return IsValid(Server) && IsValid(Client0) && IsValid(Client1) &&
                IsValid(Server->GetGameState()) &&
                IsValid(Client0->GetGameState()) &&
                IsValid(Client1->GetGameState()) &&
                Client0->IsNetMode(NM_Client) && Client1->IsNetMode(NM_Client) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Server)) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Client0)) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Client1));
        }), 60.0, TEXT("context replacement source and clients ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            Run->Failed = !BuildBase(*Run, World) ||
                !BindClient(Run->Clients[0],
                    ck::auto_test::net::Get_ClientWorld(0)) ||
                !BindClient(Run->Clients[1],
                    ck::auto_test::net::Get_ClientWorld(1));
            TestTrue(TEXT("context replacement real base and exact client roots"),
                !Run->Failed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Why;
            return UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
                World, Run->SourceGameState.Get(), Run->SourceRoot,
                true, Run->Base, Rows, Why) ==
                ECkCompassGym_CatalogOutcome::Complete && Rows.Num() == 9;
        }), 60.0, TEXT("context replacement source physically complete")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (Run->Failed) { return; }
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Why;
            const bool Source = UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
                World, Run->SourceGameState.Get(), Run->SourceRoot,
                true, Run->Base, Rows, Why) ==
                ECkCompassGym_CatalogOutcome::Complete && Rows.Num() == 9;
            Run->LastSourceRevision = 1;
            const bool Queued = Source && QueueBoth(*Run, MakeWire(Rows, 1));
            TestTrue(TEXT("context replacement baseline queued to both mirrors"),
                Queued);
            Run->Failed = !Queued;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            return Installed(Run->Clients[0], 9, 1, true) &&
                Installed(Run->Clients[1], 9, 1, true);
        }), 60.0, TEXT("context replacement baseline physically installed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            if (Run->Failed) { return; }
            auto& Client = Run->Clients[0];
            auto* World = Client.World.Get();
            auto* NewGameState = IsValid(World) ?
                World->SpawnActor<AGameStateBase>() : nullptr;
            const bool Replacement = IsValid(NewGameState) &&
                NewGameState != Client.GameState.Get();
            if (Replacement)
            {
                Client.ReplacementGameState = NewGameState;
                World->SetGameState(NewGameState);
            }
            TestTrue(TEXT("real client World GameState identity replaced while old object survives"),
                Replacement && World->GetGameState() == NewGameState &&
                IsValid(Client.GameState.Get()));
            Run->Failed = !Replacement;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (Run->Failed) { return true; }
            return UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                Run->Clients[0].WorldRoot);
        }), 20.0, TEXT("captured exact first client world closes after GS replacement")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            if (Run->Failed) { return; }
            auto& Changed = Run->Clients[0];
            int64 Generation = -1, Sequence = -1;
            TArray<FCkCompassGym_SourceRow> Rows;
            Rows.AddDefaulted();
            FString Why;
            const bool OldTupleInstalled = UCkCompassGym_Mirror::TryGet_Installed(
                Changed.World.Get(), Changed.GameState.Get(),
                Changed.MirrorRoot, Generation, Sequence, Rows, Why);
            TestTrue(TEXT("captured world closes while unrelated client remains healthy"),
                !OldTupleInstalled && Generation == 0 && Sequence == 0 &&
                Rows.IsEmpty() &&
                UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Changed.WorldRoot) &&
                !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Clients[1].WorldRoot) &&
                Installed(Run->Clients[1], 9, 1, true));
            if (IsValid(Changed.World.Get()) &&
                IsValid(Changed.GameState.Get()))
            { Changed.World->SetGameState(Changed.GameState.Get()); }
            if (Allocated(Run->SourceRoot))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->SourceRoot); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            TestFalse(TEXT("Context fixture never entered an early failure path"),
                Run->Failed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
