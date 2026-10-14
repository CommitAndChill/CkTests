#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_ProceduralActionReentry.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Fragment.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Fragment.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_action_managed
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;
    constexpr auto BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    constexpr auto LimbNodPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA");
    constexpr auto ConsumeRejectedAuto = TEXT("ProceduralActionPlayer ConsumeReceipts rejected Auto identity");

    struct FSubject
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
    };

    auto
        ComposeBodySubject(
            UWorld* InWorld)
        -> FSubject
    {
        auto Fixture = FSubject{};
        auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        Fixture.Root = UCk_Utils_Transform_UE::Add(RootEntity, FTransform{FVector{100.0, 200.0, 2000.0}},
            ECk_Replication::DoesNotReplicate);
        auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Fixture.Root);
        const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
            FTransform::Identity, ECk_Replication::DoesNotReplicate);
        auto Layout = FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}};
        Layout.Set_BodyEnvelopeMin(FVector{-2.0, -2.0, -2.0});
        Layout.Set_BodyEnvelopeMax(FVector{2.0, 2.0, 2.0});
        auto Spec = FCk_ProceduralArticulation_Spec{Layout, TArray<FCk_ProceduralLimb_Binding>{}};
        Spec.Set_Presentation(Presentation);
        Fixture.Subject = UCk_Utils_ProceduralArticulation_UE::Add(Fixture.Root, Spec);
        return Fixture;
    }

    auto
        Get_IsSettled(
            const FSubject& InFixture)
        -> bool
    {
        return ck::IsValid(InFixture.Subject)
            && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(InFixture.Subject) > 0;
    }

    auto
        AddPlayer(
            FSubject& InFixture,
            float InPlayRate)
        -> bool
    {
        auto Spec = FCk_ProceduralActionPlayer_Spec{};
        auto Tuning = Spec.Get_Tuning();
        auto Playback = Tuning.Get_Playback();
        Playback.Set_PlayRate(InPlayRate);
        Tuning.Set_Playback(Playback);
        Spec.Set_Tuning(Tuning);
        InFixture.Player = UCk_Utils_ProceduralActionPlayer_UE::Add(InFixture.Subject, Spec);
        InFixture.Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InFixture.Root);
        return ck::IsValid(InFixture.Player) && ck::IsValid(InFixture.Scope);
    }

    auto
        MakeBodyPulsePlay(
            ECk_ProceduralAction_Delivery InDelivery)
        -> FCk_ProceduralAction_PlaySpec
    {
        auto Play = FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{
            LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath)}};
        Play.Set_Delivery(InDelivery);
        return Play;
    }

    auto
        Get_IsRetained(
            const FCk_Handle_ProceduralActionPlayer& InPlayer,
            FCk_ProceduralAction_Identity InIdentity)
        -> bool
    {
        return UCk_Utils_ProceduralActionPlayer_UE::Get_RetainedReceiptIdentities(InPlayer)
            .ContainsByPredicate([InIdentity](const FCk_ProceduralAction_Identity& InRetained)
            { return InRetained.Get_Value() == InIdentity.Get_Value(); });
    }

    auto
        Get_IsAutoDelivered(
            const FCk_Handle_ProceduralActionPlayer& InPlayer,
            FCk_ProceduralAction_Identity InIdentity)
        -> bool
    {
        return InPlayer.Get<ck::FFragment_ProceduralActionPlayer>().Get_Receipts().Get_IsAutoDelivered(InIdentity);
    }

    auto
        DestroyRoot(
            const FSubject& InFixture)
        -> void
    {
        if (ck::Is_NOT_Valid(InFixture.Root)) { return; }
        auto Root = InFixture.Root.ConvertToHandle();
        if (Root.Has<ck::FTag_DestroyEntity_Initiate>()) { return; }
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedAutoDeliveryTest,
    "Ck.ProceduralAnimation.Action.Managed.AutoFinishesOnceAndReleasesItself",
    ck_test_procedural_action_managed::TestFlags)

auto
    FCkProceduralActionManagedAutoDeliveryTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE> Listener;
        FCk_Handle_ProceduralAction Action;
        FCk_ProceduralAction_Identity Identity;
        uint64 CreateFrame = 0;
    };
    const auto State = MakeShared<FState>();
    AddExpectedError(ConsumeRejectedAuto, EAutomationExpectedErrorFlags::Contains, 0);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld);
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Player and scope compose"), AddPlayer(State->Fixture, 1.0f)))
            { return; }
            State->Listener = TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE>{
                NewObject<UCk_Test_ProceduralActionReentry_UE>()};
            State->CreateFrame = GFrameCounter;
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Fixture.Scope,
                State->Fixture.Player, MakeBodyPulsePlay(ECk_ProceduralAction_Delivery::Auto));
            if (NOT TestTrue(TEXT("Auto clip creates an action"), ck::IsValid(State->Action)))
            { return; }
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
            auto Finished = FCk_Delegate_ProceduralAction_OnFinished{};
            Finished.BindDynamic(State->Listener.Get(), &UCk_Test_ProceduralActionReentry_UE::OnFinished);
            UCk_Utils_ProceduralAction_UE::BindTo_OnFinished(State->Action, Finished);
            TestTrue(TEXT("The player acknowledges an Auto stream"),
                Get_IsAutoDelivered(State->Fixture.Player, State->Identity));

            auto Rejected = FCk_Delegate_Request_OnCompleted{};
            Rejected.BindDynamic(State->Listener.Get(), &UCk_Test_ProceduralActionReentry_UE::OnInvalidCompleted);
            UCk_Utils_ProceduralActionPlayer_UE::Request_ConsumeReceipts(State->Fixture.Player,
                FCk_Request_ProceduralActionPlayer_ConsumeReceipts{State->Identity, 0}, Rejected);
            TestEqual(TEXT("Consuming an Auto stream completes synchronously"),
                State->Listener->InvalidCompletionCount, 1);
            TestEqual(TEXT("Consuming an Auto stream is not enqueued"), State->Listener->InvalidResult,
                ECk_Request_OperationResult::Failed_NotEnqueued);
            TestEqual(TEXT("OnFinished does not fire inside Create_FromClip"), State->Listener->FinishedCount, 0);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return State->Listener.IsValid() && State->Listener->FinishedCount > 0; }),
        WaitSeconds, TEXT("The Auto clip finishes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::Is_NOT_Valid(State->Action) && NOT Get_IsRetained(State->Fixture.Player, State->Identity); }),
        WaitSeconds, TEXT("The acknowledged Auto action is destroyed and its stream reclaimed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Terminal = State->Listener->LastTerminal;
            TestEqual(TEXT("OnFinished fires exactly once"), State->Listener->FinishedCount, 1);
            TestTrue(TEXT("OnFinished fires after Create_FromClip returned"),
                State->Listener->FinishedFrame > State->CreateFrame);
            TestEqual(TEXT("The terminal names the action's identity"), Terminal.Get_Identity().Get_Value(),
                State->Identity.Get_Value());
            TestEqual(TEXT("The terminal carries the completed result"), Terminal.Get_Result().Get_Outcome(),
                ECk_ProceduralAction_Outcome::Completed);
            TestEqual(TEXT("The rejected consume never completes again"), State->Listener->InvalidCompletionCount, 1);
            TestFalse(TEXT("A reclaimed stream is no longer Auto-delivered"),
                Get_IsAutoDelivered(State->Fixture.Player, State->Identity));
            TestTrue(TEXT("The scope that owned the action survives"),
                ck::IsValid(State->Fixture.Scope) && NOT State->Fixture.Scope.Has<ck::FTag_DestroyEntity_Initiate>());
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedExplicitReleaseTest,
    "Ck.ProceduralAnimation.Action.Managed.ExplicitReleasesAfterTerminalIsConsumed",
    ck_test_procedural_action_managed::TestFlags)

auto
    FCkProceduralActionManagedExplicitReleaseTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE> Listener;
        FCk_Handle_ProceduralAction Action;
        FCk_ProceduralAction_Identity Identity;
        int64 TerminalSequence = 0;
    };
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld);
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Player and scope compose"), AddPlayer(State->Fixture, 1.0f)))
            { return; }
            State->Listener = TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE>{
                NewObject<UCk_Test_ProceduralActionReentry_UE>()};
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Fixture.Scope,
                State->Fixture.Player, MakeBodyPulsePlay(ECk_ProceduralAction_Delivery::Explicit));
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
            TestFalse(TEXT("An Explicit stream is the caller's to acknowledge"),
                Get_IsAutoDelivered(State->Fixture.Player, State->Identity));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Fixture.Player, State->Identity, 0, 64).Get_Terminal();
        }), WaitSeconds, TEXT("The Explicit clip writes its terminal")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestTrue(TEXT("A finished action with an unconsumed terminal stays alive"),
                ck::IsValid(State->Action) && NOT State->Action.Has<ck::FTag_DestroyEntity_Initiate>());
            TestEqual(TEXT("The live action reports Finished"), UCk_Utils_ProceduralAction_UE::Get_State(State->Action),
                ECk_ProceduralAction_State::Finished);
            TestEqual(TEXT("Its result is readable until it is released"),
                UCk_Utils_ProceduralAction_UE::Get_Result(State->Action).Get_Outcome(),
                ECk_ProceduralAction_Outcome::Completed);
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Fixture.Player, State->Identity, 0, 64);
            State->TerminalSequence = Page.Get_LastStreamSequence();
            if (NOT TestTrue(TEXT("The stream holds records before its terminal"), State->TerminalSequence > 1))
            { return; }
            auto Completion = FCk_Delegate_Request_OnCompleted{};
            Completion.BindDynamic(State->Listener.Get(), &UCk_Test_ProceduralActionReentry_UE::OnConsumeCompleted);
            UCk_Utils_ProceduralActionPlayer_UE::Request_ConsumeReceipts(State->Fixture.Player,
                FCk_Request_ProceduralActionPlayer_ConsumeReceipts{State->Identity, State->TerminalSequence - 1},
                Completion);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return State->Listener.IsValid() && State->Listener->ConsumeCompletionCount > 0; }),
        WaitSeconds, TEXT("The partial consume drains")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("The partial consume succeeds"), State->Listener->ConsumeResult,
                ECk_Request_OperationResult::Succeeded);
            TestTrue(TEXT("Consuming short of the terminal keeps the action alive"),
                ck::IsValid(State->Action) && NOT State->Action.Has<ck::FTag_DestroyEntity_Initiate>());
            auto Completion = FCk_Delegate_Request_OnCompleted{};
            Completion.BindDynamic(State->Listener.Get(), &UCk_Test_ProceduralActionReentry_UE::OnConsumeCompleted);
            UCk_Utils_ProceduralActionPlayer_UE::Request_ConsumeReceipts(State->Fixture.Player,
                FCk_Request_ProceduralActionPlayer_ConsumeReceipts{State->Identity, State->TerminalSequence},
                Completion);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::Is_NOT_Valid(State->Action) && NOT Get_IsRetained(State->Fixture.Player, State->Identity); }),
        WaitSeconds, TEXT("Consuming the terminal releases the action and reclaims its stream")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("Both consumes complete once each"), State->Listener->ConsumeCompletionCount, 2);
            TestEqual(TEXT("The terminal consume succeeds"), State->Listener->ConsumeResult,
                ECk_Request_OperationResult::Succeeded);
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedLoadedClipTest,
    "Ck.ProceduralAnimation.Action.Managed.LoadedClipResolvesInsideCreate",
    ck_test_procedural_action_managed::TestFlags)

auto
    FCkProceduralActionManagedLoadedClipTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
    };
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld);
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Player and scope compose"), AddPlayer(State->Fixture, 1.0f)))
            { return; }
            auto* BodyPulse = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
            auto* LimbNod = LoadObject<UCk_ProceduralAction_Data>(nullptr, LimbNodPath);
            if (NOT TestNotNull(TEXT("BodyPulse loads"), BodyPulse) || NOT TestNotNull(TEXT("LimbNod loads"), LimbNod))
            { return; }

            const auto Loaded = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Fixture.Scope,
                State->Fixture.Player, FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{BodyPulse}});
            if (TestTrue(TEXT("A loaded clip creates an action"), ck::IsValid(Loaded)))
            {
                TestEqual(TEXT("A loaded clip skips the asynchronous load"), UCk_Utils_ProceduralAction_UE::Get_State(Loaded),
                    ECk_ProceduralAction_State::PendingAdmission);
                TestTrue(TEXT("A loaded clip is compiled before Create_FromClip returns"),
                    Loaded.Has<ck::FFragment_ProceduralAction_Compiled>());
                TestFalse(TEXT("A loaded clip holds no loading state"),
                    Loaded.Has<ck::FFragment_ProceduralAction_Loading>());
            }

            auto Invalid = TStrongObjectPtr<UCk_ProceduralAction_Data>{
                DuplicateObject<UCk_ProceduralAction_Data>(BodyPulse, GetTransientPackage())};
            Invalid->Set_Phases({});
            auto InvalidScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Fixture.Root);
            const auto InvalidAction = UCk_Utils_ProceduralAction_UE::Create_FromClip(InvalidScope,
                State->Fixture.Player, FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{Invalid.Get()}});
            if (TestTrue(TEXT("An invalid loaded clip still returns its action"), ck::IsValid(InvalidAction)))
            {
                TestEqual(TEXT("An invalid loaded clip comes back Finished"),
                    UCk_Utils_ProceduralAction_UE::Get_State(InvalidAction), ECk_ProceduralAction_State::Finished);
                TestEqual(TEXT("An invalid loaded clip reports InvalidContent"),
                    UCk_Utils_ProceduralAction_UE::Get_Result(InvalidAction).Get_Outcome(),
                    ECk_ProceduralAction_Outcome::InvalidContent);
                TestTrue(TEXT("The synchronous failure already holds its terminal record"),
                    UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(State->Fixture.Player,
                        UCk_Utils_ProceduralAction_UE::Get_Identity(InvalidAction), 0, 8).Get_Terminal());
            }

            auto Unbound = FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{LimbNod}};
            Unbound.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
                FCk_ProceduralAction_SlotBinding{FName{TEXT("Nod")}, FName{TEXT("MissingLimb")}}});
            auto UnboundScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Fixture.Root);
            const auto UnboundAction = UCk_Utils_ProceduralAction_UE::Create_FromClip(UnboundScope,
                State->Fixture.Player, Unbound);
            if (TestTrue(TEXT("A loaded clip that cannot bind still returns its action"), ck::IsValid(UnboundAction)))
            {
                TestEqual(TEXT("A loaded clip that cannot bind comes back Finished"),
                    UCk_Utils_ProceduralAction_UE::Get_State(UnboundAction), ECk_ProceduralAction_State::Finished);
                TestEqual(TEXT("A loaded clip that cannot bind is Rejected"),
                    UCk_Utils_ProceduralAction_UE::Get_Result(UnboundAction).Get_Outcome(),
                    ECk_ProceduralAction_Outcome::Rejected);
            }
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedPlayerEndPlayTest,
    "Ck.ProceduralAnimation.Action.Managed.PlayerEndPlayDestroysLiveAutoAction",
    ck_test_procedural_action_managed::TestFlags)

auto
    FCkProceduralActionManagedPlayerEndPlayTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        FCk_Handle IndependentScope;
        FCk_Handle_ProceduralAction Action;
    };
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld);
            State->IndependentScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Slow player composes"), AddPlayer(State->Fixture, 0.1f)))
            { return; }
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->IndependentScope,
                State->Fixture.Player, MakeBodyPulsePlay(ECk_ProceduralAction_Delivery::Auto));
            TestTrue(TEXT("Auto clip creates an action under an independent scope"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Playing;
        }), WaitSeconds, TEXT("The Auto clip plays")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*) { DestroyRoot(State->Fixture); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return ck::Is_NOT_Valid(State->Action); }),
        WaitSeconds, TEXT("The player's end of play destroys its live Auto action")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestTrue(TEXT("The scope that owned the action survives"),
                ck::IsValid(State->IndependentScope)
                    && NOT State->IndependentScope.Has<ck::FTag_DestroyEntity_Initiate>());
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->IndependentScope);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
