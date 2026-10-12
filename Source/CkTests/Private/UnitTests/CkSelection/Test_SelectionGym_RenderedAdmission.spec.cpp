#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && WITH_CK_DEBUG_OVERLAY

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityDebugOverlay/Selection/CkDebugOverlay_SourceAdmission.h"
#include "CkEntityDebugOverlay/Settings/CkDebugOverlay_SelectionSettings.h"
#include "CkEntityDebugOverlay/Subsystem/CkDebugOverlay_Subsystem.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkLabel/CkLabel_Utils.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"

namespace ck_selection_rendered_admission_test
{
    using Admission = UCk_DebugOverlay_SourceAdmission_UE;

    struct FViewer
    {
        FCk_Handle Root;
        FCk_Handle Source;
        FCk_Handle Target;
        FCk_Handle Unrelated;
        bool Begun = false;
    };

    struct FRun
    {
        FViewer Server;
        FViewer Clients[2];
        FCk_DebugOverlay_SelectionConfig OriginalConfig;
        bool Failed = false;
    };

    auto Allocated(const FCk_Handle& Handle) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle); }

    auto ExactPlayer(UWorld* World) -> ULocalPlayer*
    {
        if (!IsValid(World) || !IsValid(World->GetGameInstance())) { return nullptr; }
        for (auto* Player : World->GetGameInstance()->GetLocalPlayers())
        {
            if (IsValid(Player) && Player->GetWorld() == World &&
                IsValid(Player->GetPlayerController(World)) &&
                Player->GetPlayerController(World)->GetLocalPlayer() == Player)
            { return Player; }
        }
        return nullptr;
    }

    auto ExactOverlay(UWorld* World) -> UCk_DebugOverlay_Subsystem*
    {
        auto* Player = ExactPlayer(World);
        return IsValid(Player) ? Player->GetSubsystem<UCk_DebugOverlay_Subsystem>() : nullptr;
    }

    auto BeginAndCompose(UWorld* World, FViewer& Viewer) -> bool
    {
        if (!IsValid(World) || !World->HasBegunPlay()) { return false; }
        Viewer.Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!Allocated(Viewer.Root)) { return false; }
        Viewer.Unrelated = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Viewer.Root);
        Viewer.Source = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Viewer.Root);
        if (!Allocated(Viewer.Unrelated) || !Allocated(Viewer.Source)) { return false; }
        FString Reason;
        Viewer.Begun = Admission::TryBegin(World, Viewer.Root, 1, {Viewer.Source}, Reason);
        if (!Viewer.Begun ||
            Admission::IsAdmitted(Viewer.Source) || !Admission::IsAdmitted(Viewer.Unrelated))
        { return false; }

        const auto RootTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Ck.Debug.Selection.Root")), false);
        if (!RootTag.IsValid()) { return false; }
        UCk_Utils_GameplayLabel_UE::Add(Viewer.Source, RootTag);
        if (!UCk_Utils_GameplayLabel_UE::MatchesExact(Viewer.Source, RootTag)) { return false; }

        auto* Player = ExactPlayer(World);
        if (!IsValid(Player)) { return false; }
        auto* Controller = Player->GetPlayerController(World);
        auto ViewLocation = FVector::ZeroVector;
        auto ViewRotation = FRotator::ZeroRotator;
        Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
        Viewer.Target = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Viewer.Source);
        if (!Allocated(Viewer.Target) ||
            !Allocated(UCk_Utils_Transform_UE::Add(Viewer.Target,
                FTransform(ViewRotation, ViewLocation + ViewRotation.Vector() * 250.0),
                ECk_Replication::DoesNotReplicate)))
        { return false; }
        return !Admission::IsAdmitted(Viewer.Target) &&
            Admission::IsAdmitted(Viewer.Unrelated);
    }

    auto Publish(UWorld* World, const FViewer& Viewer) -> bool
    {
        if (!IsValid(World) || !Allocated(Viewer.Root) ||
            !Allocated(Viewer.Source) || !Allocated(Viewer.Target))
        { return false; }
        FString Reason;
        return Admission::TryPublish(World, Viewer.Root, 1,
                {Viewer.Source, Viewer.Target}, Reason) &&
            Admission::IsAdmitted(Viewer.Source) &&
            Admission::IsAdmitted(Viewer.Target);
    }

    auto Mount(UWorld* World) -> bool
    {
        auto* Overlay = ExactOverlay(World);
        if (!IsValid(Overlay)) { return false; }
        if (!Overlay->Get_TestState().HasRootWidget)
        { Overlay->Toggle_LocalVisibilityForTest(); }
        const auto State = Overlay->Get_TestState();
        return State.HasRootWidget && State.HasAttachedViewport && State.HasTicker &&
            State.HasInputProcessor && State.HasSelectionUi &&
            Overlay->Get_TestRootWidget().IsValid() &&
            Overlay->Get_TestAttachedPlayer() == ExactPlayer(World);
    }

    auto HasSource(UWorld* World, const FViewer& Viewer) -> bool
    {
        const auto* Overlay = ExactOverlay(World);
        return IsValid(Overlay) &&
            Overlay->Get_TestSelectionSnapshot().Gathered.Contains(Viewer.Source) &&
            Overlay->Get_TestSelectionSnapshot().Gathered.Contains(Viewer.Target) &&
            Overlay->Get_TestSelectionSnapshot().Candidates.Contains(Viewer.Source);
    }

    auto HasHiddenSource(UWorld* World, const FViewer& Viewer) -> bool
    {
        const auto* Overlay = ExactOverlay(World);
        if (!IsValid(Overlay)) { return false; }
        const auto State = Overlay->Get_TestSelectionSnapshot();
        return State.Gathered.Contains(Viewer.Unrelated) &&
            !State.Gathered.Contains(Viewer.Source) &&
            !State.Gathered.Contains(Viewer.Target) &&
            !State.Candidates.Contains(Viewer.Source);
    }

    auto DescribeSelection(UWorld* World, const FViewer& Viewer) -> FString
    {
        const auto* Overlay = ExactOverlay(World);
        if (!IsValid(Overlay))
        { return TEXT("exact local overlay unavailable"); }
        const auto State = Overlay->Get_TestSelectionSnapshot();
        const auto Ui = Overlay->Get_TestState();
        return FString::Printf(TEXT("mounted=%d ticker=%d sourceAllocated=%d targetAllocated=%d sourceAdmitted=%d targetAdmitted=%d unrelatedGathered=%d sourceGathered=%d targetGathered=%d sourceCandidate=%d"),
            Ui.HasRootWidget ? 1 : 0, Ui.HasTicker ? 1 : 0,
            Allocated(Viewer.Source) ? 1 : 0, Allocated(Viewer.Target) ? 1 : 0,
            Admission::IsAdmitted(Viewer.Source) ? 1 : 0,
            Admission::IsAdmitted(Viewer.Target) ? 1 : 0,
            State.Gathered.Contains(Viewer.Unrelated) ? 1 : 0,
            State.Gathered.Contains(Viewer.Source) ? 1 : 0,
            State.Gathered.Contains(Viewer.Target) ? 1 : 0,
            State.Candidates.Contains(Viewer.Source) ? 1 : 0);
    }

    auto CloseAndRetire(UWorld* World, FViewer& Viewer) -> bool
    {
        auto* Overlay = ExactOverlay(World);
        FString Reason;
        const bool Closed = !Viewer.Begun ||
            Admission::TryClose(World, Viewer.Root, 1, Reason);
        const bool UnrelatedOpen = !Allocated(Viewer.Unrelated) ||
            Admission::IsAdmitted(Viewer.Unrelated);
        if (IsValid(Overlay) && Overlay->Get_TestState().HasRootWidget)
        { Overlay->Toggle_LocalVisibilityForTest(); }
        if (Allocated(Viewer.Source))
        { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Viewer.Source); }
        if (Allocated(Viewer.Unrelated))
        { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Viewer.Unrelated); }
        return Closed && UnrelatedOpen;
    }

    auto HasClosedPeer(UWorld* ServerWorld, UWorld* ClosedWorld, UWorld* PeerWorld,
        const FRun& Run, bool Dedicated) -> bool
    {
        auto* Closed = ExactOverlay(ClosedWorld);
        auto* Peer = ExactOverlay(PeerWorld);
        if (!IsValid(Closed) || !IsValid(Peer)) { return false; }
        const auto State = Closed->Get_TestSelectionSnapshot();
        const auto Other = Peer->Get_TestSelectionSnapshot();
        auto* Host = Dedicated ? nullptr : ExactOverlay(ServerWorld);
        const bool HostClosed = Dedicated || (IsValid(Host) &&
            !Host->Get_TestSelectionSnapshot().Gathered.Contains(Run.Server.Source) &&
            !Host->Get_TestSelectionSnapshot().Candidates.Contains(Run.Server.Source) &&
            Host->Get_TestSelectionSnapshot().Focused != Run.Server.Source &&
            Host->Get_TestSelectionSnapshot().Locked != Run.Server.Source &&
            !Host->Get_TestSelectionSnapshot().FamilyVisible);
        return HostClosed && !State.Gathered.Contains(Run.Clients[0].Source) &&
            !State.Gathered.Contains(Run.Clients[0].Target) &&
            !State.Candidates.Contains(Run.Clients[0].Source) &&
            State.Focused != Run.Clients[0].Source &&
            State.Locked != Run.Clients[0].Source &&
            !State.FamilyVisible &&
            Other.Gathered.Contains(Run.Clients[1].Target) &&
            Other.Candidates.Contains(Run.Clients[1].Source) &&
            Peer->Get_TestState().HasRootWidget;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSelectionGym_RenderedAdmission,
    "Ck.Selection.Gym.RenderedAdmission.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
        EAutomationTestFlags::NonNullRHI)

bool FCkSelectionGym_RenderedAdmission::RunTest(const FString& Parameters)
{
    using namespace ck_selection_rendered_admission_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported rendered Selection topology")); return false; }
    const auto Run = MakeShared<FRun>();
    auto* Settings = UCk_DebugOverlay_SelectionSettings::Get_Mutable();
    if (!IsValid(Settings))
    { AddError(TEXT("Selection settings unavailable before PIE")); return false; }
    Run->OriginalConfig = Settings->Get_Config();
    auto FixtureConfig = Run->OriginalConfig;
    FixtureConfig.Hierarchy = ECk_DebugOverlay_SelectionHierarchy::MeaningfulRoots;
    FixtureConfig.RootAnchor = ECk_DebugOverlay_SelectionRootAnchor::Member;
    FixtureConfig.Scope = ECk_DebugOverlay_SelectionScope::Nearby;
    FixtureConfig.Targeting = ECk_DebugOverlay_SelectionTargeting::Weighted;
    FixtureConfig.SearchRadius = 10000.0f;
    FixtureConfig.ViewBias = 0.90f;
    if (!Settings->TrySet_Config(FixtureConfig, false))
    { AddError(TEXT("Selection fixture policy rejected before PIE")); return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            return IsValid(Server) && IsValid(Client0) && IsValid(Client1) &&
                Server->HasBegunPlay() && Client0->HasBegunPlay() &&
                Client1->HasBegunPlay() &&
                Server->GetNetMode() == (Dedicated ? NM_DedicatedServer : NM_ListenServer) &&
                Client0->GetNetMode() == NM_Client && Client1->GetNetMode() == NM_Client &&
                (Dedicated ? ExactPlayer(Server) == nullptr : ExactPlayer(Server) != nullptr) &&
                ExactPlayer(Client0) != nullptr && ExactPlayer(Client1) != nullptr;
        }), 60.0, TEXT("three real Selection worlds and exact local viewers ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Dedicated](UWorld* World)
        {
            if (Dedicated)
            {
                Run->Failed |= !TestTrue(TEXT("dedicated server has no local overlay"),
                    ExactPlayer(World) == nullptr);
                return;
            }
            Run->Failed |= !TestTrue(TEXT("listen host real source composed and mounted"),
                BeginAndCompose(World, Run->Server) && Mount(World));
        })));
    for (int32 Index = 0; Index < 2; ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Index,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Index](UWorld* World)
            {
                Run->Failed |= !TestTrue(TEXT("client real source composed and mounted"),
                    BeginAndCompose(World, Run->Clients[Index]) && Mount(World));
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run, Dedicated]() -> bool
        {
            return Run->Failed ||
                (HasHiddenSource(ck::auto_test::net::Get_ClientWorld(0), Run->Clients[0]) &&
                HasHiddenSource(ck::auto_test::net::Get_ClientWorld(1), Run->Clients[1]) &&
                (Dedicated || HasHiddenSource(ck::auto_test::net::Get_ServerWorld(), Run->Server)));
        }), 15.0, TEXT("ordinary mounted ticker hides staged sources but gathers unrelated entities")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Dedicated](UWorld*)
        {
            if (Run->Failed) { return; }
            const bool Hidden =
                HasHiddenSource(ck::auto_test::net::Get_ClientWorld(0), Run->Clients[0]) &&
                HasHiddenSource(ck::auto_test::net::Get_ClientWorld(1), Run->Clients[1]) &&
                (Dedicated || HasHiddenSource(ck::auto_test::net::Get_ServerWorld(), Run->Server));
            if (!Hidden)
            {
                Run->Failed = true;
                AddError(FString::Printf(TEXT("staged selection timeout: client0 %s; client1 %s; host %s"),
                    *DescribeSelection(ck::auto_test::net::Get_ClientWorld(0), Run->Clients[0]),
                    *DescribeSelection(ck::auto_test::net::Get_ClientWorld(1), Run->Clients[1]),
                    Dedicated ? TEXT("dedicated") :
                        *DescribeSelection(ck::auto_test::net::Get_ServerWorld(), Run->Server)));
            }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Dedicated](UWorld* World)
        {
            if (!Dedicated && !Run->Failed)
            { Run->Failed |= !TestTrue(TEXT("listen host complete source publication"),
                Publish(World, Run->Server)); }
        })));
    for (int32 Index = 0; Index < 2; ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Index,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Index](UWorld* World)
            {
                if (!Run->Failed)
                { Run->Failed |= !TestTrue(TEXT("client complete source publication"),
                    Publish(World, Run->Clients[Index])); }
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run, Dedicated]() -> bool
        {
            return Run->Failed ||
                (HasSource(ck::auto_test::net::Get_ClientWorld(0), Run->Clients[0]) &&
                HasSource(ck::auto_test::net::Get_ClientWorld(1), Run->Clients[1]) &&
                (Dedicated || HasSource(ck::auto_test::net::Get_ServerWorld(), Run->Server)));
        }), 15.0, TEXT("ordinary overlay ticker gathers each published source and spatial anchor")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Dedicated](UWorld*)
        {
            if (Run->Failed) { return; }
            const bool Ready =
                HasSource(ck::auto_test::net::Get_ClientWorld(0), Run->Clients[0]) &&
                HasSource(ck::auto_test::net::Get_ClientWorld(1), Run->Clients[1]) &&
                (Dedicated || HasSource(ck::auto_test::net::Get_ServerWorld(), Run->Server));
            if (!Ready)
            {
                Run->Failed = true;
                AddError(FString::Printf(TEXT("published selection timeout: client0 %s; client1 %s; host %s"),
                    *DescribeSelection(ck::auto_test::net::Get_ClientWorld(0), Run->Clients[0]),
                    *DescribeSelection(ck::auto_test::net::Get_ClientWorld(1), Run->Clients[1]),
                    Dedicated ? TEXT("dedicated") :
                        *DescribeSelection(ck::auto_test::net::Get_ServerWorld(), Run->Server)));
            }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Dedicated](UWorld* World)
        {
            if (Dedicated || Run->Failed) { return; }
            auto* Overlay = ExactOverlay(World);
            if (!TestTrue(TEXT("listen host overlay remains mounted"), IsValid(Overlay)))
            { Run->Failed = true; return; }
            if (GEngine != nullptr)
            {
                GEngine->Exec(World, TEXT("ck.DebugOverlay.Select"));
                GEngine->Exec(World, TEXT("ck.DebugOverlay.Lock"));
                GEngine->Exec(World, TEXT("ck.DebugOverlay.Family"));
            }
            const auto Active = Overlay->Get_TestSelectionSnapshot();
            FString Reason;
            const bool Closed = Admission::TryClose(World, Run->Server.Root, 1, Reason);
            if (GEngine != nullptr)
            {
                GEngine->Exec(World, TEXT("ck.DebugOverlay.Select"));
                GEngine->Exec(World, TEXT("ck.DebugOverlay.Lock"));
                GEngine->Exec(World, TEXT("ck.DebugOverlay.Family"));
            }
            const auto Denied = Overlay->Get_TestSelectionSnapshot();
            Run->Failed |= !TestTrue(TEXT("listen-host commands select/lock/family and reject closed source"),
                Active.Focused == Run->Server.Source &&
                Active.Locked == Run->Server.Source &&
                Active.SelectionRoot == Run->Server.Source && Active.FamilyVisible &&
                Closed && Denied.Focused != Run->Server.Source &&
                Denied.Locked != Run->Server.Source && !Denied.FamilyVisible);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->Failed) { return; }
            auto* Overlay = ExactOverlay(World);
            if (!TestTrue(TEXT("exact client overlay remains mounted"), IsValid(Overlay)))
            { Run->Failed = true; return; }
            const auto Before = Overlay->Get_TestSelectionSnapshot();
            const bool Gathered = Before.Gathered.Contains(Run->Clients[0].Target) &&
                Before.Candidates.Contains(Run->Clients[0].Source);
            if (GEngine != nullptr) { GEngine->Exec(World, TEXT("ck.DebugOverlay.Select")); }
            const auto Selected = Overlay->Get_TestSelectionSnapshot();
            const bool Focused = Selected.Focused == Run->Clients[0].Source;
            if (GEngine != nullptr) { GEngine->Exec(World, TEXT("ck.DebugOverlay.Lock")); }
            const auto Locked = Overlay->Get_TestSelectionSnapshot();
            if (GEngine != nullptr) { GEngine->Exec(World, TEXT("ck.DebugOverlay.Family")); }
            const auto Family = Overlay->Get_TestSelectionSnapshot();
            Run->Failed |= !TestTrue(TEXT("real world commands select, lock, and open source family"),
                Gathered && Focused && Locked.Locked == Run->Clients[0].Source &&
                Family.FamilyVisible && Family.SelectionRoot == Run->Clients[0].Source);
            FString Reason;
            const bool Closed = Admission::TryClose(World,
                Run->Clients[0].Root, 1, Reason);
            const auto BeforeTick = Overlay->Get_TestSelectionSnapshot();
            Run->Failed |= !TestTrue(TEXT("scope closes immediately without changing peer admission"),
                Closed && !Admission::IsAdmitted(Run->Clients[0].Target) &&
                Admission::IsAdmitted(Run->Clients[0].Unrelated) &&
                BeforeTick.Gathered.Contains(Run->Clients[0].Source));
            if (GEngine != nullptr)
            {
                GEngine->Exec(World, TEXT("ck.DebugOverlay.Select"));
                GEngine->Exec(World, TEXT("ck.DebugOverlay.Lock"));
                GEngine->Exec(World, TEXT("ck.DebugOverlay.Family"));
            }
            const auto Rejected = Overlay->Get_TestSelectionSnapshot();
            Run->Failed |= !TestTrue(TEXT("commands cannot retain denied focus or family"),
                Rejected.Focused != Run->Clients[0].Source &&
                Rejected.Locked != Run->Clients[0].Source &&
                !Rejected.FamilyVisible);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run, Dedicated]() -> bool
        {
            return Run->Failed || HasClosedPeer(
                ck::auto_test::net::Get_ServerWorld(),
                ck::auto_test::net::Get_ClientWorld(0),
                ck::auto_test::net::Get_ClientWorld(1), *Run, Dedicated);
        }), 15.0, TEXT("ordinary ticker removes closed selection while peer remains mounted")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Dedicated](UWorld*)
        {
            if (Run->Failed) { return; }
            const bool Ready = HasClosedPeer(
                ck::auto_test::net::Get_ServerWorld(),
                ck::auto_test::net::Get_ClientWorld(0),
                ck::auto_test::net::Get_ClientWorld(1), *Run, Dedicated);
            if (!Ready)
            {
                Run->Failed = true;
                AddError(FString::Printf(TEXT("selection closure timeout: closed client %s; peer %s; host %s"),
                    *DescribeSelection(ck::auto_test::net::Get_ClientWorld(0), Run->Clients[0]),
                    *DescribeSelection(ck::auto_test::net::Get_ClientWorld(1), Run->Clients[1]),
                    Dedicated ? TEXT("dedicated") :
                        *DescribeSelection(ck::auto_test::net::Get_ServerWorld(), Run->Server)));
            }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        { Run->Failed |= !TestTrue(TEXT("closed client's unrelated entity stays admitted"),
            Admission::IsAdmitted(Run->Clients[0].Unrelated) &&
            CloseAndRetire(World, Run->Clients[0])); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        { Run->Failed |= !TestTrue(TEXT("peer source closes only on explicit peer cleanup"),
            CloseAndRetire(World, Run->Clients[1])); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Dedicated](UWorld* World)
        {
            if (!Dedicated)
            { Run->Failed |= !TestTrue(TEXT("listen host source closes on own cleanup"),
                CloseAndRetire(World, Run->Server)); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            auto* Settings = UCk_DebugOverlay_SelectionSettings::Get_Mutable();
            const bool Restored = IsValid(Settings) &&
                Settings->TrySet_Config(Run->OriginalConfig, false);
            const bool Ended = ck::auto_test::net::Get_AllPIEWorlds().IsEmpty();
            const bool Completed = !Run->Failed;
            const bool SettingsOk = TestTrue(TEXT("Selection policy restored without save"), Restored);
            const bool WorldsOk = TestTrue(TEXT("all real PIE worlds ended"), Ended);
            const bool FixtureOk = TestTrue(TEXT("rendered admission fixture completed every phase"), Completed);
            return SettingsOk && WorldsOk && FixtureOk;
        }), TEXT("rendered Selection admission closure"), true));
    return true;
}

#endif
