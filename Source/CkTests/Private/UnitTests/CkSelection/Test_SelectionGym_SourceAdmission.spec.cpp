#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityDebugOverlay/Selection/CkDebugOverlay_SourceAdmission.h"
#include "CkEntityDebugOverlay/Provider/CkDebugOverlay_ObservationBank.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

namespace ck_selection_source_admission_test
{
    using Admission = UCk_DebugOverlay_SourceAdmission_UE;

    struct FRun
    {
        FCk_Handle ServerUnmanaged;
        FCk_Handle ServerScopeRoot;
        FCk_Handle ServerChild;
        FCk_Handle Foreign;
        FCk_Handle ClientUnmanaged;
        FCk_Handle SourceRoot;
        FCk_Handle OutlierRoot;
        FCk_Handle Child;
        FCk_Handle Grandchild;
        FCk_Handle Transferred;
        FCk_Handle EscapedChild;
        FCk_Handle OutlierChild;
        FCk_Handle ReplacementChild;
        FCk_Handle LateChild;
        FCk_Handle RejectedRoot;
        FCk_Handle NewRoot;
        bool Failed = false;
    };

    auto Allocated(const FCk_Handle& Handle) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle); }

    auto RootFor(UWorld* World) -> FCk_Handle
    {
        return IsValid(World)
            ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World)
            : FCk_Handle{};
    }

    auto RejectPublish(UWorld* World, const FCk_Handle& WorldRoot, int64 Generation,
        const TArray<FCk_Handle>& Members) -> bool
    {
        FString Reason;
        return NOT Admission::TryPublish(World, WorldRoot, Generation, Members, Reason) &&
            NOT Reason.IsEmpty();
    }

    auto Gone(const FCk_Handle& Handle) -> bool
    { return NOT Allocated(Handle); }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSelectionGym_SourceAdmission,
    "Ck.Selection.Gym.SourceAdmission.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkSelectionGym_SourceAdmission::RunTest(const FString& Parameters)
{
    using namespace ck_selection_source_admission_test;
    bool Dedicated = false;
    if (NOT ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Selection admission topology")); return false; }
    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            return IsValid(Server) && IsValid(Client0) && IsValid(Client1) &&
                IsValid(Server->GetGameState()) &&
                Server->GetGameState()->HasActorBegunPlay() &&
                Client0->GetNetMode() == NM_Client &&
                Client1->GetNetMode() == NM_Client &&
                Allocated(RootFor(Server)) && Allocated(RootFor(Client0)) &&
                Allocated(RootFor(Client1));
        }), 60.0, TEXT("real Selection authority and two client ECS worlds ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            const auto Root = RootFor(World);
            if (NOT TestTrue(TEXT("authority has exact ECS root"), Allocated(Root)))
            { Run->Failed = true; return; }
            Run->ServerUnmanaged =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("unmanaged authority entity stays admitted"),
                Allocated(Run->ServerUnmanaged) &&
                Admission::IsAdmitted(Run->ServerUnmanaged)))
            { Run->Failed = true; return; }
            Run->ServerScopeRoot =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("authority bare source root allocated"),
                Allocated(Run->ServerScopeRoot)))
            { Run->Failed = true; return; }
            FString Reason;
            const bool Began = Admission::TryBegin(
                World, Root, 1, {Run->ServerScopeRoot}, Reason);
            if (NOT TestTrue(TEXT("authority begins source scope before any child composition"),
                Began && Reason.IsEmpty() &&
                NOT Admission::IsAdmitted(Run->ServerScopeRoot) &&
                Admission::IsAdmitted(Run->ServerUnmanaged)))
            {
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->ServerScopeRoot);
                Run->Failed = true; return;
            }
            Run->ServerChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                Run->ServerScopeRoot);
            if (NOT TestTrue(TEXT("authority child hidden until complete publication"),
                Allocated(Run->ServerChild) &&
                NOT Admission::IsAdmitted(Run->ServerChild)))
            {
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->ServerScopeRoot);
                Run->Failed = true; return;
            }
            const bool Published = Admission::TryPublish(World, Root, 1,
                {Run->ServerScopeRoot, Run->ServerChild}, Reason);
            const bool Visible = Admission::IsAdmitted(Run->ServerScopeRoot) &&
                Admission::IsAdmitted(Run->ServerChild);
            const bool Closed = Admission::TryClose(World, Root, 1, Reason);
            const bool Hidden = NOT Admission::IsAdmitted(Run->ServerScopeRoot) &&
                NOT Admission::IsAdmitted(Run->ServerChild);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->ServerScopeRoot);
            if (NOT TestTrue(TEXT("authority scope publishes, closes and preserves unrelated entity"),
                Published && Visible && Closed && Hidden &&
                Admission::IsAdmitted(Run->ServerUnmanaged)))
            { Run->Failed = true; }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            const auto Root = RootFor(World);
            if (NOT TestTrue(TEXT("second client has exact ECS root"), Allocated(Root)))
            { Run->Failed = true; return; }
            Run->Foreign = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("foreign client entity remains unmanaged and admitted"),
                Allocated(Run->Foreign) && Admission::IsAdmitted(Run->Foreign)))
            { Run->Failed = true; }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->Failed) { return; }
            const auto Root = RootFor(World);
            if (NOT TestTrue(TEXT("exact client source admission prerequisites"),
                Allocated(Root) && Allocated(Run->Foreign) &&
                World->GetNetMode() == NM_Client))
            { Run->Failed = true; return; }
            Run->ClientUnmanaged =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            Run->SourceRoot = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            Run->OutlierRoot = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("bare direct roots and unrelated client entity allocated"),
                Allocated(Run->ClientUnmanaged) && Allocated(Run->SourceRoot) &&
                Allocated(Run->OutlierRoot) &&
                Admission::IsAdmitted(Run->ClientUnmanaged)))
            { Run->Failed = true; return; }

            FString Reason;
            const TArray<FCk_Handle> Roots{Run->SourceRoot, Run->OutlierRoot};
            const bool DuplicateRootRejected =
                NOT Admission::TryBegin(World, Root, 1,
                    {Run->SourceRoot, Run->SourceRoot}, Reason) &&
                NOT Reason.IsEmpty();
            const bool Began = Admission::TryBegin(World, Root, 1, Roots, Reason);
            if (NOT TestTrue(TEXT("same-action source begin closes both direct roots"),
                DuplicateRootRejected && Began && Reason.IsEmpty() &&
                Admission::IsAdmitted(Run->ClientUnmanaged) &&
                NOT Admission::IsAdmitted(Run->SourceRoot) &&
                NOT Admission::IsAdmitted(Run->OutlierRoot)))
            { Run->Failed = true; return; }

            Run->Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                Run->SourceRoot);
            if (NOT TestTrue(TEXT("live source child"), Allocated(Run->Child)))
            { Run->Failed = true; return; }
            Run->Grandchild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                Run->Child);
            Run->Transferred = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                Run->Child);
            Run->OutlierChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                Run->OutlierRoot);
            if (NOT TestTrue(TEXT("real nested source forest allocated and hidden before publication"),
                Allocated(Run->Child) && Allocated(Run->Grandchild) &&
                Allocated(Run->Transferred) && Allocated(Run->OutlierChild) &&
                NOT Admission::IsAdmitted(Run->Child) &&
                NOT Admission::IsAdmitted(Run->Grandchild) &&
                NOT Admission::IsAdmitted(Run->OutlierChild)))
            { Run->Failed = true; return; }

            const TArray<FCk_Handle> Members{Run->SourceRoot, Run->OutlierRoot,
                Run->Child, Run->Grandchild, Run->Transferred, Run->OutlierChild};
            auto TimerFact = FCk_DebugOverlay_SourceObservation{};
            TimerFact.Source = Run->Child;
            TimerFact.Timers.Add(FCk_DebugOverlay_TimerFact{
                FGameplayTag{}, ECk_Timer_State::Paused});
            auto StagedFact = TimerFact;
            const bool FactsStaged = FCk_DebugOverlay_ObservationBankUtils::TryPublish(
                World, Root, Run->SourceRoot, 1, 1, {TimerFact});
            const bool FactsHidden =
                NOT FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                    Run->Child, StagedFact) &&
                StagedFact.Source == FCk_Handle{} && StagedFact.Timers.IsEmpty();
            if (NOT TestTrue(TEXT("value facts stage while source scope stays closed"),
                FactsStaged && FactsHidden))
            { Run->Failed = true; return; }
            auto WithDefault = Members;
            WithDefault.Add({});
            auto WithForeign = Members;
            WithForeign.Add(Run->Foreign);
            auto WithDuplicate = Members;
            WithDuplicate.Add(Run->Child);
            const TArray<FCk_Handle> MissingParent{Run->SourceRoot,
                Run->OutlierRoot, Run->Grandchild, Run->Transferred, Run->OutlierChild};
            const bool InvalidForestRejected =
                RejectPublish(World, Root, 1, WithDefault) &&
                RejectPublish(World, Root, 1, WithForeign) &&
                RejectPublish(World, Root, 1, WithDuplicate) &&
                RejectPublish(World, Root, 1, MissingParent) &&
                RejectPublish(World, Root, 2, Members) &&
                RejectPublish(nullptr, Root, 1, Members) &&
                NOT Admission::IsAdmitted(Run->SourceRoot) &&
                NOT Admission::IsAdmitted(Run->Child) &&
                NOT Admission::IsAdmitted(Run->OutlierRoot) &&
                Admission::IsAdmitted(Run->ClientUnmanaged);
            if (NOT TestTrue(TEXT("invalid source forests leave the closed scope intact"),
                InvalidForestRejected))
            { Run->Failed = true; return; }
            const bool Published =
                Admission::TryPublish(World, Root, 1, Members, Reason);
            auto PublishedFact = FCk_DebugOverlay_SourceObservation{};
            if (NOT TestTrue(TEXT("malformed whole forest rejected; exact two-root forest publishes"),
                Published && Reason.IsEmpty() &&
                Admission::IsAdmitted(Run->SourceRoot) &&
                Admission::IsAdmitted(Run->Child) &&
                Admission::IsAdmitted(Run->Grandchild) &&
                Admission::IsAdmitted(Run->Transferred) &&
                Admission::IsAdmitted(Run->OutlierRoot) &&
                Admission::IsAdmitted(Run->OutlierChild) &&
                Admission::IsAdmitted(Run->ClientUnmanaged) &&
                FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                    Run->Child, PublishedFact) && PublishedFact.Source == Run->Child &&
                PublishedFact.Timers.Num() == 1 &&
                PublishedFact.Timers[0].State == ECk_Timer_State::Paused))
            { Run->Failed = true; return; }

            Run->ReplacementChild =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run->SourceRoot);
            if (NOT TestTrue(TEXT("new descendant waits for a complete replacement"),
                Allocated(Run->ReplacementChild) &&
                NOT Admission::IsAdmitted(Run->ReplacementChild)))
            { Run->Failed = true; return; }
            auto Replacement = Members;
            Replacement.Add(Run->ReplacementChild);
            auto PriorFact = FCk_DebugOverlay_SourceObservation{};
            const bool VisibleUnaffected =
                RejectPublish(World, Root, 1, WithDefault) &&
                RejectPublish(World, Root, 1, WithForeign) &&
                RejectPublish(World, Root, 1, WithDuplicate) &&
                RejectPublish(World, Root, 1, MissingParent) &&
                RejectPublish(World, Root, 2, Members) &&
                NOT Admission::TryClose(World, Root, 2, Reason) &&
                NOT Admission::TryClose(nullptr, Root, 1, Reason) &&
                Admission::IsAdmitted(Run->Child) &&
                Admission::IsAdmitted(Run->OutlierChild) &&
                NOT Admission::IsAdmitted(Run->ReplacementChild) &&
                FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                    Run->Child, PriorFact) &&
                PriorFact.Source == Run->Child && PriorFact.Timers.Num() == 1 &&
                PriorFact.Timers[0].State == ECk_Timer_State::Paused;
            if (NOT TestTrue(TEXT("malformed replacement preserves visible members and facts"),
                VisibleUnaffected))
            { Run->Failed = true; return; }
            const bool Replaced = Admission::TryPublish(
                World, Root, 1, Replacement, Reason);
            const bool ReplacementVisible =
                Admission::IsAdmitted(Run->ReplacementChild) &&
                Admission::IsAdmitted(Run->Child);
            if (NOT TestTrue(TEXT("complete same-generation replacement admits new child"),
                Replaced && ReplacementVisible))
            { Run->Failed = true; return; }
            UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(
                Run->Transferred, Run->ClientUnmanaged);
            if (NOT TestTrue(TEXT("transferred known member remains denied outside source roots"),
                Allocated(Run->Transferred) &&
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(
                    Run->Transferred) == Run->ClientUnmanaged &&
                NOT Admission::IsAdmitted(Run->Transferred) &&
                Admission::IsAdmitted(Run->Child) &&
                Admission::IsAdmitted(Run->ClientUnmanaged)))
            { Run->Failed = true; return; }
            Run->EscapedChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                Run->Transferred);
            if (NOT TestTrue(TEXT("descendant of escaped known member is also denied"),
                Allocated(Run->EscapedChild) &&
                NOT Admission::IsAdmitted(Run->EscapedChild)))
            { Run->Failed = true; return; }

            const bool Closed = Admission::TryClose(World, Root, 1, Reason);
            auto HiddenFact = PublishedFact;
            const bool ClosedFactsHidden =
                NOT FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                    Run->Child, HiddenFact) &&
                HiddenFact.Source == FCk_Handle{} && HiddenFact.Timers.IsEmpty();
            Replacement.RemoveSingle(Run->Transferred);
            const bool Reopened = Admission::TryPublish(
                World, Root, 1, Replacement, Reason);
            const bool ReopenedFactsVisible =
                FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                    Run->Child, PublishedFact) &&
                PublishedFact.Source == Run->Child &&
                Admission::IsAdmitted(Run->ReplacementChild) &&
                NOT Admission::IsAdmitted(Run->Transferred);
            const bool ClosedAgain = Admission::TryClose(World, Root, 1, Reason);
            if (NOT TestTrue(TEXT("close hides facts and same-generation valid replacement reopens"),
                Closed && ClosedFactsHidden && Reopened && ReopenedFactsVisible &&
                ClosedAgain))
            { Run->Failed = true; return; }
            Run->LateChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                Run->SourceRoot);
            const bool OldStillAllocated =
                NOT Admission::TryBegin(World, Root, 2,
                    {Run->ClientUnmanaged}, Reason) && NOT Reason.IsEmpty();
            if (NOT TestTrue(TEXT("closed scope hides published and late descendants"),
                Allocated(Run->LateChild) && OldStillAllocated &&
                NOT Admission::IsAdmitted(Run->SourceRoot) &&
                NOT Admission::IsAdmitted(Run->Child) &&
                NOT Admission::IsAdmitted(Run->Grandchild) &&
                NOT Admission::IsAdmitted(Run->OutlierRoot) &&
                NOT Admission::IsAdmitted(Run->OutlierChild) &&
                NOT Admission::IsAdmitted(Run->ReplacementChild) &&
                NOT Admission::IsAdmitted(Run->LateChild) &&
                NOT Admission::IsAdmitted(Run->Transferred) &&
                NOT Admission::IsAdmitted(Run->EscapedChild) &&
                Admission::IsAdmitted(Run->ClientUnmanaged)))
            { Run->Failed = true; return; }

            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->SourceRoot);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->OutlierRoot);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (Run->Failed) { return true; }
            return Gone(Run->SourceRoot) && Gone(Run->OutlierRoot) &&
                Gone(Run->Child) && Gone(Run->Grandchild) &&
                Gone(Run->OutlierChild) && Gone(Run->ReplacementChild) &&
                Gone(Run->LateChild);
        }), 15.0, TEXT("old source roots and in-tree members physically retired")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->Failed) { return; }
            const auto Root = RootFor(World);
            if (NOT TestTrue(TEXT("client world root remains live after old closure"),
                Allocated(Root)))
            { Run->Failed = true; return; }
            Run->RejectedRoot = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("new bare root and transferred old member remain allocated"),
                Allocated(Run->RejectedRoot) &&
                Allocated(Run->Transferred)))
            { Run->Failed = true; return; }
            FString Reason;
            const bool KnownMemberBlocksNewGeneration =
                NOT Admission::TryBegin(World, Root, 2, {Run->RejectedRoot}, Reason) &&
                NOT Reason.IsEmpty();
            const bool RejectedRootStayedUnmanaged =
                Admission::IsAdmitted(Run->RejectedRoot);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->RejectedRoot);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->Transferred);
            if (NOT TestTrue(TEXT("escaped known member blocks newer generation after roots are gone"),
                KnownMemberBlocksNewGeneration &&
                RejectedRootStayedUnmanaged))
            { Run->Failed = true; }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return Run->Failed ||
            (Gone(Run->RejectedRoot) && Gone(Run->Transferred) &&
                Gone(Run->EscapedChild)); }),
        15.0, TEXT("rejected bare root and transferred known member physically retired")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->Failed) { return; }
            const auto Root = RootFor(World);
            if (NOT TestTrue(TEXT("all previous source identities physically gone"),
                Allocated(Root) && Gone(Run->SourceRoot) &&
                Gone(Run->OutlierRoot) && Gone(Run->Child) &&
                Gone(Run->Grandchild) && Gone(Run->Transferred) &&
                Gone(Run->EscapedChild) &&
                Gone(Run->OutlierChild) && Gone(Run->ReplacementChild) &&
                Gone(Run->LateChild) && Gone(Run->RejectedRoot)))
            { Run->Failed = true; return; }
            Run->NewRoot = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("fresh bare root for accepted generation allocated"),
                Allocated(Run->NewRoot)))
            { Run->Failed = true; return; }
            FString Reason;
            const bool Began = Admission::TryBegin(
                World, Root, 2, {Run->NewRoot}, Reason);
            const bool Hidden = NOT Admission::IsAdmitted(Run->NewRoot);
            const bool Published = Admission::TryPublish(
                World, Root, 2, {Run->NewRoot}, Reason);
            const bool Visible = Admission::IsAdmitted(Run->NewRoot);
            const bool Closed = Admission::TryClose(World, Root, 2, Reason);
            if (NOT TestTrue(TEXT("new generation admits only after old physical closure"),
                Began && Hidden && Published && Visible && Closed &&
                NOT Admission::IsAdmitted(Run->NewRoot) &&
                Admission::IsAdmitted(Run->ClientUnmanaged)))
            { Run->Failed = true; }
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->NewRoot);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            return TestFalse(TEXT("all PIE worlds ended"),
                    NOT ck::auto_test::net::Get_AllPIEWorlds().IsEmpty()) &&
                TestFalse(TEXT("stale scoped entity is never admitted"),
                    Admission::IsAdmitted(Run->NewRoot)) &&
                TestFalse(TEXT("stale unmanaged entity is never admitted"),
                    Admission::IsAdmitted(Run->ClientUnmanaged)) &&
                TestFalse(TEXT("all real source admission assertions completed"),
                    Run->Failed);
        }), TEXT("Selection admission teardown and no skipped assertions"), true));
    return true;
}

#endif
