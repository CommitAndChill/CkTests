#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkCompass/CkCompass_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkPoi/CkPoi_Utils.h"
#include "Engine/World.h"

namespace ck_compass_closure_test
{
    struct FWorldFixture
    {
        TWeakObjectPtr<UWorld> World;
        FCk_Handle Root;
        FCk_Handle Observer;
        FCk_Handle_Transform ObserverTransform;
        FCk_Handle_Compass Compass;
        FCk_Handle PoiHost;
        FCk_Handle_Transform PoiTransform;
        FCk_Handle_Poi Poi;
        FCk_Handle AdditionalPoiHost;
        FCk_Handle_Transform AdditionalPoiTransform;
        FCk_Handle AdditionalObserver;
        FCk_Handle_Transform AdditionalObserverTransform;
        FCk_Handle_Compass AdditionalCompass;
    };

    struct FRun
    {
        FWorldFixture Server;
        FWorldFixture Client0;
        FWorldFixture Client1;
        bool Dedicated = false;
        int32 ClosedPolls = 0;
    };

    auto Allocated(const FCk_Handle& Handle) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle); }

    auto HasActualPose(const FCk_Handle& Host,
        const FCk_Handle_Transform& Transform,
        const FVector& Expected) -> bool
    {
        if (!Allocated(Host) || !Allocated(Transform) ||
            !ck::IsValid(Transform) ||
            Transform != Host ||
            !Host.Has<ck::FFragment_Transform>())
        { return false; }
        const auto Location = UCk_Utils_Transform_UE::
            Get_EntityCurrentLocation(Transform);
        return FMath::IsFinite(Location.X) &&
            FMath::IsFinite(Location.Y) &&
            FMath::IsFinite(Location.Z) &&
            Location.Equals(Expected, 0.01);
    }

    auto AddPoi(FWorldFixture& Fixture, const FVector& Position,
        FCk_Handle& OutHost,
        FCk_Handle_Transform& OutTransform) -> bool
    {
        OutHost = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Fixture.Root);
        if (!Allocated(OutHost)) { return false; }
        OutTransform = UCk_Utils_Transform_UE::Add(OutHost,
            FTransform(FRotator::ZeroRotator, Position),
            ECk_Replication::DoesNotReplicate);
        if (!Allocated(OutTransform) || !ck::IsValid(OutTransform) ||
            OutTransform != OutHost ||
            !OutHost.Has<ck::FFragment_Transform>())
        { return false; }
        const auto Category = FGameplayTag::RequestGameplayTag(
            FName(TEXT("Poi.Category.Quest")), false);
        if (!Category.IsValid()) { return false; }
        return Allocated(UCk_Utils_Poi_UE::Add(
            OutHost, FCk_Poi_Spec(Category)));
    }

    auto Build(FWorldFixture& Fixture, UWorld* World) -> bool
    {
        Fixture.World = World;
        Fixture.Root = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!Allocated(Fixture.Root)) { return false; }
        Fixture.Observer = UCk_Utils_EntityLifetime_UE::
            Request_CreateEntity(Fixture.Root);
        if (!Allocated(Fixture.Observer)) { return false; }
        Fixture.ObserverTransform = UCk_Utils_Transform_UE::Add(Fixture.Observer,
            FTransform(FRotator::ZeroRotator, FVector::ZeroVector),
            ECk_Replication::DoesNotReplicate);
        if (!Allocated(Fixture.ObserverTransform) ||
            !ck::IsValid(Fixture.ObserverTransform) ||
            Fixture.ObserverTransform != Fixture.Observer ||
            !Fixture.Observer.Has<ck::FFragment_Transform>())
        { return false; }
        auto Params = FCk_Compass_Spec(360.0f);
        Params.Set_MaxEntries(4);
        Fixture.Compass = UCk_Utils_Compass_UE::Create(
            Fixture.Observer, Params);
        if (!Allocated(Fixture.Compass)) { return false; }
        if (!AddPoi(Fixture, FVector(600, 0, 0),
            Fixture.PoiHost, Fixture.PoiTransform))
        { return false; }
        Fixture.Poi = UCk_Utils_Poi_UE::CastChecked(Fixture.PoiHost);
        return Allocated(Fixture.Poi);
    }

    auto ProjectedOne(const FWorldFixture& Fixture) -> bool
    {
        if (!Allocated(Fixture.Compass) || !Allocated(Fixture.Poi) ||
            !HasActualPose(Fixture.Observer, Fixture.ObserverTransform,
                FVector::ZeroVector) ||
            !HasActualPose(Fixture.PoiHost, Fixture.PoiTransform,
                FVector(600, 0, 0)))
        { return false; }
        const auto Entries = UCk_Utils_Compass_UE::Get_Entries(Fixture.Compass);
        return Entries.Num() == 1 && Entries[0].Get_Poi() == Fixture.Poi;
    }

    auto ProjectedNone(const FWorldFixture& Fixture) -> bool
    {
        return Allocated(Fixture.Compass) &&
            UCk_Utils_Compass_UE::Get_Entries(Fixture.Compass).IsEmpty();
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCompass_WorldProjectionClosure,
    "Ck.Compass.WorldProjectionClosure.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCompass_WorldProjectionClosure::RunTest(const FString& Parameters)
{
    using namespace ck_compass_closure_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Compass closure topology")); return false; }
    const auto Run = MakeShared<FRun>();
    Run->Dedicated = Dedicated;

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            if (!IsValid(Server) || !IsValid(Client0) || !IsValid(Client1))
            { return false; }
            for (auto* World : {Server, Client0, Client1})
            {
                if (!Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*World)))
                { return false; }
            }
            return true;
        }), 60.0, TEXT("three distinct PIE ECS worlds have real transient roots")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            if (!TestTrue(TEXT("three distinct PIE worlds are present"),
                IsValid(World) && IsValid(Client0) && IsValid(Client1) &&
                World != Client0 && World != Client1 && Client0 != Client1))
            { return; }
            TestTrue(TEXT("real local Compass and POI composed in each PIE world"),
                Build(Run->Server, World) &&
                Build(Run->Client0, Client0) &&
                Build(Run->Client1, Client1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            return HasActualPose(Run->Server.Observer,
                    Run->Server.ObserverTransform, FVector::ZeroVector) &&
                HasActualPose(Run->Server.PoiHost,
                    Run->Server.PoiTransform, FVector(600, 0, 0)) &&
                ProjectedOne(Run->Client0) &&
                ProjectedOne(Run->Client1) &&
                (Run->Dedicated ? ProjectedNone(Run->Server) :
                    ProjectedOne(Run->Server));
        }), 30.0, TEXT("real Compass projection settled in each cosmetic world")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            TestTrue(TEXT("default root rejected without closing valid world"),
                !UCk_Utils_Compass_UE::TryCloseWorldProjection({}) &&
                !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed({}) &&
                !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Server.Root));
            TestTrue(TEXT("live child rejected without closing valid world"),
                !UCk_Utils_Compass_UE::TryCloseWorldProjection(
                    Run->Server.Observer) &&
                !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Server.Observer) &&
                !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Server.Root));
            TestTrue(TEXT("child from another PIE world rejected without changing either world"),
                !UCk_Utils_Compass_UE::TryCloseWorldProjection(
                    Run->Client0.Observer) &&
                !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Server.Root) &&
                !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Client0.Root) &&
                !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Client1.Root));
            TestTrue(TEXT("server closure is accepted and idempotent"),
                Run->Server.World.Get() == World &&
                UCk_Utils_Compass_UE::TryCloseWorldProjection(
                    Run->Server.Root) &&
                UCk_Utils_Compass_UE::TryCloseWorldProjection(
                    Run->Server.Root) &&
                UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Server.Root));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            return ProjectedNone(Run->Server) &&
                ProjectedOne(Run->Client0) &&
                ProjectedOne(Run->Client1);
        }), 30.0, TEXT("server closure drains entries only in exact world")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            TestTrue(TEXT("first client closure is accepted and idempotent"),
                UCk_Utils_Compass_UE::TryCloseWorldProjection(
                    Run->Client0.Root) &&
                UCk_Utils_Compass_UE::TryCloseWorldProjection(
                    Run->Client0.Root) &&
                UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Client0.Root) &&
                !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Client1.Root));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            return ProjectedNone(Run->Server) &&
                ProjectedNone(Run->Client0) &&
                ProjectedOne(Run->Client1);
        }), 30.0, TEXT("first client drains while second client remains projected")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            if (!TestTrue(TEXT("real new POI created in each closed world"),
                AddPoi(Run->Server, FVector(900, 0, 0),
                    Run->Server.AdditionalPoiHost,
                    Run->Server.AdditionalPoiTransform) &&
                AddPoi(Run->Client0, FVector(900, 0, 0),
                    Run->Client0.AdditionalPoiHost,
                    Run->Client0.AdditionalPoiTransform))) { return; }
            Run->Client0.AdditionalObserver = UCk_Utils_EntityLifetime_UE::
                Request_CreateEntity(Run->Client0.Root);
            if (!TestTrue(TEXT("new closed-world Compass observer allocated"),
                Allocated(Run->Client0.AdditionalObserver))) { return; }
            Run->Client0.AdditionalObserverTransform =
                UCk_Utils_Transform_UE::Add(
                    Run->Client0.AdditionalObserver,
                    FTransform(FRotator::ZeroRotator, FVector::ZeroVector),
                    ECk_Replication::DoesNotReplicate);
            if (!TestTrue(TEXT("late observer has allocated typed Transform and real fragment"),
                Allocated(Run->Client0.AdditionalObserverTransform) &&
                ck::IsValid(Run->Client0.AdditionalObserverTransform) &&
                Run->Client0.AdditionalObserverTransform ==
                    Run->Client0.AdditionalObserver &&
                Run->Client0.AdditionalObserver.Has<
                    ck::FFragment_Transform>())) { return; }
            Run->Client0.AdditionalCompass = UCk_Utils_Compass_UE::Create(
                Run->Client0.AdditionalObserver, FCk_Compass_Spec(360.0f));
            TestTrue(TEXT("new Compass genuinely composed after local world closure"),
                Allocated(Run->Client0.AdditionalCompass));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (!Allocated(Run->Server.AdditionalPoiHost) ||
                !Allocated(Run->Client0.AdditionalPoiHost) ||
                !HasActualPose(Run->Server.AdditionalPoiHost,
                    Run->Server.AdditionalPoiTransform,
                    FVector(900, 0, 0)) ||
                !HasActualPose(Run->Client0.AdditionalPoiHost,
                    Run->Client0.AdditionalPoiTransform,
                    FVector(900, 0, 0)) ||
                !HasActualPose(Run->Client0.AdditionalObserver,
                    Run->Client0.AdditionalObserverTransform,
                    FVector::ZeroVector) ||
                !Run->Server.AdditionalPoiHost.Has<ck::FTag_Poi>() ||
                !Run->Client0.AdditionalPoiHost.Has<ck::FTag_Poi>() ||
                !Allocated(Run->Client0.AdditionalCompass) ||
                Run->Client0.AdditionalCompass.Has<
                    ck::FTag_Compass_NeedsSetup>() ||
                UCk_Utils_Compass_UE::Get_Observer(
                    Run->Client0.AdditionalCompass) !=
                    Run->Client0.AdditionalObserver)
            { return false; }
            const auto Category = FGameplayTag::RequestGameplayTag(
                FName(TEXT("Poi.Category.Quest")), false);
            if (!Category.IsValid() ||
                !UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(
                    Run->Server.AdditionalPoiHost).HasTagExact(Category) ||
                !UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(
                    Run->Client0.AdditionalPoiHost).HasTagExact(Category))
            { return false; }
            ++Run->ClosedPolls;
            return Run->ClosedPolls >= 3;
        }), 10.0, TEXT("three later observations after eligible POI creation in closed worlds")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            TestTrue(TEXT("closed worlds never repopulate and unrelated client remains live"),
                ProjectedNone(Run->Server) &&
                ProjectedNone(Run->Client0) &&
                Allocated(Run->Client0.AdditionalCompass) &&
                UCk_Utils_Compass_UE::Get_Entries(
                    Run->Client0.AdditionalCompass).IsEmpty() &&
                ProjectedOne(Run->Client1) &&
                UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Server.Root) &&
                UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Client0.Root) &&
                !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(
                    Run->Client1.Root));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
