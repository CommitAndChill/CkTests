#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_ProceduralActionManagedListener.h"
#include "CkTest_ProceduralActionReentry.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Fragment.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Fragment.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Processor.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Algo/AllOf.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace ck_test_procedural_action_managed
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;
    constexpr auto BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    constexpr auto LimbNodPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA");
    constexpr auto CreateRejectedScope = TEXT("ProceduralAction Create rejected scope");
    constexpr auto ConsumeRejectedManaged = TEXT("ProceduralActionPlayer ConsumeReceipts rejected managed identity");

    using FListener = UCk_Test_ProceduralActionManagedListener_UE;
    using EReentry = ECk_Test_ProceduralActionManagedReentry;

    struct FSubject
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
    };

    auto
    ComposeBodySubject(
        UWorld* InWorld,
        const FVector& InLocation)
        -> FSubject
    {
        auto Fixture = FSubject{};
        auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        Fixture.Root = UCk_Utils_Transform_UE::Add(RootEntity, FTransform{InLocation},
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
    ComposeLimbSubject(
        UWorld* InWorld,
        const FVector& InLocation,
        const TArray<FName>& InLimbs)
        -> FSubject
    {
        auto Fixture = FSubject{};
        auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        Fixture.Root = UCk_Utils_Transform_UE::Add(RootEntity, FTransform{InLocation},
            ECk_Replication::DoesNotReplicate);
        auto Layouts = TArray<FCk_ProceduralLimb_Layout>{};
        auto Bindings = TArray<FCk_ProceduralLimb_Binding>{};
        for (auto Index = 0; Index < InLimbs.Num(); ++Index)
        {
            auto PartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Fixture.Root);
            const auto Part = UCk_Utils_Transform_UE::Add(PartEntity, FTransform::Identity,
                ECk_Replication::DoesNotReplicate);
            Layouts.Add(FCk_ProceduralLimb_Layout{InLimbs[Index],
                FTransform{FQuat::Identity, FVector{0.0, 100.0 * Index, 0.0}},
                TArray<float>{100.0f}, FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}},
                FVector::UpVector});
            Bindings.Add(FCk_ProceduralLimb_Binding{InLimbs[Index], TArray<FCk_Handle_Transform>{Part}});
        }
        Fixture.Subject = UCk_Utils_ProceduralArticulation_UE::Add(Fixture.Root,
            FCk_ProceduralArticulation_Spec{FCk_ProceduralArticulation_Layout{Layouts}, Bindings});
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
        const FCk_ProceduralActionPlayer_Spec& InSpec)
        -> bool
    {
        InFixture.Player = UCk_Utils_ProceduralActionPlayer_UE::Add(InFixture.Subject, InSpec);
        InFixture.Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InFixture.Root);
        return ck::IsValid(InFixture.Player) && ck::IsValid(InFixture.Scope);
    }

    auto
    MakeSlowPlayerSpec(
        float InPlayRate)
        -> FCk_ProceduralActionPlayer_Spec
    {
        auto Spec = FCk_ProceduralActionPlayer_Spec{};
        auto Tuning = Spec.Get_Tuning();
        auto Playback = Tuning.Get_Playback();
        Playback.Set_PlayRate(InPlayRate);
        Tuning.Set_Playback(Playback);
        Spec.Set_Tuning(Tuning);
        return Spec;
    }

    auto
    MakeRejectedSpec(
        UCk_ProceduralAction_Data* InLimbNod)
        -> FCk_ProceduralAction_LoadedPlaySpec
    {
        auto Spec = FCk_ProceduralAction_LoadedPlaySpec{InLimbNod};
        Spec.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
            FCk_ProceduralAction_SlotBinding{FName{TEXT("Nod")}, FName{TEXT("MissingLimb")}}});
        return Spec;
    }

    auto
    MakeBoundLimbSpec(
        UCk_ProceduralAction_Data* InLimbNod,
        FName InLimb)
        -> FCk_ProceduralAction_LoadedPlaySpec
    {
        auto Spec = FCk_ProceduralAction_LoadedPlaySpec{InLimbNod};
        Spec.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
            FCk_ProceduralAction_SlotBinding{FName{TEXT("Nod")}, InLimb}});
        return Spec;
    }

    auto
    Get_RetainedCount(
        FCk_Handle_ProceduralActionPlayer& InPlayer)
        -> int32
    {
        return InPlayer.Get<ck::FFragment_ProceduralActionPlayer>().Get_Receipts().Get_RetainedCount();
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
    MakeListener(
        const FSubject& InFixture)
        -> TStrongObjectPtr<FListener>
    {
        auto Listener = TStrongObjectPtr<FListener>{NewObject<FListener>()};
        Listener->Player = InFixture.Player;
        Listener->Scope = InFixture.Scope;
        Listener->PlayerRoot = InFixture.Root.ConvertToHandle();
        return Listener;
    }

    auto
    BindRecords(
        FCk_Handle_ProceduralActionPlayer& InPlayer,
        FCk_ProceduralAction_Identity InIdentity,
        FListener& InListener)
        -> void
    {
        UCk_Utils_ProceduralActionPlayer_UE::BindTo_OnManagedRecords(InPlayer, InIdentity,
            InListener.MakeRecordsDelegate());
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
    FCkProceduralActionManagedNoCallbackBeforePlayReturnsTest,
    "Ck.ProceduralAnimation.Action.Managed.NoCallbackBeforePlayReturns",
    ck_test_procedural_action_managed::TestFlags)

auto
FCkProceduralActionManagedNoCallbackBeforePlayReturnsTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        TStrongObjectPtr<FListener> Listener;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        FCk_ProceduralAction_ManagedStart Start;
        uint64 PlayFrame = 0;
    };
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld, FVector{100.0, 200.0, 2000.0});
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Player and scope compose"),
                AddPlayer(State->Fixture, FCk_ProceduralActionPlayer_Spec{})))
            { return; }
            State->Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
                LoadObject<UCk_ProceduralAction_Data>(nullptr, LimbNodPath)};
            if (NOT TestTrue(TEXT("LimbNod clip loads"), State->Clip.IsValid())) { return; }
            State->Listener = MakeListener(State->Fixture);
            State->PlayFrame = GFrameCounter;
            State->Start = UCk_Utils_ProceduralAction_UE::Play_Loaded(State->Fixture.Scope,
                State->Fixture.Player, MakeRejectedSpec(State->Clip.Get()),
                State->Listener->MakeCompletionDelegate());
            const auto Action = State->Start.Get_Action();
            TestEqual(TEXT("No completion fires inside Play_Loaded"), State->Listener->CompletionCount, 0);
            TestTrue(TEXT("A synchronously rejected start still returns its action"), ck::IsValid(Action));
            TestEqual(TEXT("The created start reports Valid"), State->Start.Get_Report().Get_Status(),
                ECk_ProceduralAction_ReportStatus::Valid);
            TestTrue(TEXT("The start carries the action's independent identity"),
                State->Start.Get_Identity().Get_Value() > 0
                    && State->Start.Get_Identity().Get_Value()
                        == UCk_Utils_ProceduralAction_UE::Get_Identity(Action).Get_Value());
            TestEqual(TEXT("The rejected loaded clip finished synchronously"),
                UCk_Utils_ProceduralAction_UE::Get_State(Action), ECk_ProceduralAction_State::Finished);
            TestTrue(TEXT("Delivery is managed from the moment Play returns"),
                UCk_Utils_ProceduralActionPlayer_UE::Get_IsManagedDelivery(State->Fixture.Player,
                    State->Start.Get_Identity()));
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(State->Fixture.Player,
                State->Start.Get_Identity(), 0, 8);
            TestEqual(TEXT("A managed identity stays readable"), Page.Get_Result(),
                ECk_ProceduralAction_ReadResult::Succeeded);
            TestEqual(TEXT("The synchronous rejection already holds closure and terminal"),
                Page.Get_Records().Num(), 2);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return State->Listener.IsValid() && State->Listener->CompletionCount > 0; }),
        WaitSeconds, TEXT("Managed completion arrives on a later pass")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT State->Listener.IsValid()) { return; }
            const auto& Completion = State->Listener->LastCompletion;
            TestEqual(TEXT("Completion fires exactly once"), State->Listener->CompletionCount, 1);
            TestTrue(TEXT("Completion fires on a frame after Play returned"),
                State->Listener->CompletionFrame > State->PlayFrame);
            TestEqual(TEXT("Completion names the started identity"), Completion.Get_Identity().Get_Value(),
                State->Start.Get_Identity().Get_Value());
            TestEqual(TEXT("The producer outcome is the binding rejection"),
                Completion.Get_Result().Get_Outcome(), ECk_ProceduralAction_Outcome::Rejected);
            TestEqual(TEXT("Every record was delivered"), Completion.Get_Delivery(),
                ECk_ProceduralAction_DeliveryStatus::Complete);
            TestTrue(TEXT("The seal lasts exactly as long as the stream is retained"),
                UCk_Utils_ProceduralActionPlayer_UE::Get_IsManagedDelivery(State->Fixture.Player,
                    State->Start.Get_Identity())
                == Get_IsRetained(State->Fixture.Player, State->Start.Get_Identity()));
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedInvalidScopeTest,
    "Ck.ProceduralAnimation.Action.Managed.InvalidScopeReturnsFailedReportWithoutCallback",
    ck_test_procedural_action_managed::TestFlags)

auto
FCkProceduralActionManagedInvalidScopeTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        TStrongObjectPtr<FListener> Listener;
        int32 BaselineRetained = 0;
    };
    const auto State = MakeShared<FState>();
    AddExpectedError(CreateRejectedScope, EAutomationExpectedErrorFlags::Contains, 0);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld, FVector{100.0, 200.0, 2000.0});
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Player composes"),
                AddPlayer(State->Fixture, FCk_ProceduralActionPlayer_Spec{})))
            { return; }
            State->BaselineRetained = Get_RetainedCount(State->Fixture.Player);
            State->Listener = MakeListener(State->Fixture);
            auto* Clip = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
            if (NOT TestNotNull(TEXT("BodyPulse clip loads"), Clip)) { return; }
            auto InvalidScope = FCk_Handle{};
            const auto LoadedStart = UCk_Utils_ProceduralAction_UE::Play_Loaded(InvalidScope,
                State->Fixture.Player, FCk_ProceduralAction_LoadedPlaySpec{Clip},
                State->Listener->MakeCompletionDelegate());
            const auto SoftStart = UCk_Utils_ProceduralAction_UE::Play(InvalidScope,
                State->Fixture.Player, FCk_ProceduralAction_PlaySpec{
                    TSoftObjectPtr<UCk_ProceduralAction_Data>{FSoftObjectPath{BodyPulsePath}}},
                State->Listener->MakeCompletionDelegate());
            for (const auto& Start : {LoadedStart, SoftStart})
            {
                TestFalse(TEXT("A rejected start returns an invalid action"), ck::IsValid(Start.Get_Action()));
                TestEqual(TEXT("A rejected start reports Invalid"), Start.Get_Report().Get_Status(),
                    ECk_ProceduralAction_ReportStatus::Invalid);
                TestFalse(TEXT("A rejected start explains itself"), Start.Get_Report().Get_Diagnostics().IsEmpty());
                TestEqual(TEXT("A rejected start has no identity"), Start.Get_Identity().Get_Value(), int64{0});
            }
            TestEqual(TEXT("No completion fires for a rejected start"), State->Listener->CompletionCount, 0);
            TestEqual(TEXT("A rejected start leaves no stream"), Get_RetainedCount(State->Fixture.Player),
                State->BaselineRetained);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(10));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT State->Listener.IsValid()) { return; }
            TestEqual(TEXT("No completion fires later either"), State->Listener->CompletionCount, 0);
            TestEqual(TEXT("Storage stays at its baseline"), Get_RetainedCount(State->Fixture.Player),
                State->BaselineRetained);
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedDrainsWithoutListenersTest,
    "Ck.ProceduralAnimation.Action.Managed.DrainsWithoutListenersAndReturnsStorageToBaseline",
    ck_test_procedural_action_managed::TestFlags)

auto
FCkProceduralActionManagedDrainsWithoutListenersTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        TStrongObjectPtr<FListener> Listener;
        FCk_ProceduralAction_ManagedStart Start;
        int32 BaselineRetained = 0;
        int32 PollsAfterCompletion = 0;
    };
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld, FVector{100.0, 200.0, 2000.0});
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Player composes"),
                AddPlayer(State->Fixture, FCk_ProceduralActionPlayer_Spec{})))
            { return; }
            State->BaselineRetained = Get_RetainedCount(State->Fixture.Player);
            auto* Clip = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
            if (NOT TestNotNull(TEXT("BodyPulse clip loads"), Clip)) { return; }
            State->Listener = MakeListener(State->Fixture);
            State->Start = UCk_Utils_ProceduralAction_UE::Play_Loaded(State->Fixture.Scope,
                State->Fixture.Player, FCk_ProceduralAction_LoadedPlaySpec{Clip},
                State->Listener->MakeCompletionDelegate());
            TestTrue(TEXT("Managed BodyPulse starts"), ck::IsValid(State->Start.Get_Action()));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return State->Listener.IsValid() && State->Listener->CompletionCount > 0; }),
        WaitSeconds, TEXT("Managed BodyPulse completes with only a completion recipient")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            ++State->PollsAfterCompletion;
            return Get_RetainedCount(State->Fixture.Player) == State->BaselineRetained
                && NOT Get_IsRetained(State->Fixture.Player, State->Start.Get_Identity());
        }), WaitSeconds, TEXT("Released managed stream leaves the store")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT State->Listener.IsValid()) { return; }
            const auto& Completion = State->Listener->LastCompletion;
            UE_LOG(LogTemp, Display, TEXT("[ManagedDrain] store returned to baseline after %d polls past completion"),
                State->PollsAfterCompletion);
            TestEqual(TEXT("Completion fires once"), State->Listener->CompletionCount, 1);
            TestEqual(TEXT("BodyPulse completes"), Completion.Get_Result().Get_Outcome(),
                ECk_ProceduralAction_Outcome::Completed);
            TestEqual(TEXT("Undelivered pages were drained"), Completion.Get_Delivery(),
                ECk_ProceduralAction_DeliveryStatus::Complete);
            TestTrue(TEXT("Storage returns to baseline within a few frames of completion"),
                State->PollsAfterCompletion <= 4);
            TestEqual(TEXT("Retained count equals the pre-play count"),
                Get_RetainedCount(State->Fixture.Player), State->BaselineRetained);
            TestFalse(TEXT("The completed action was released"), ck::IsValid(State->Start.Get_Action()));
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedRecordOrderTest,
    "Ck.ProceduralAnimation.Action.Managed.RecordsArriveInOrderWithWindowsAndTerminal",
    ck_test_procedural_action_managed::TestFlags)

auto
FCkProceduralActionManagedRecordOrderTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        TStrongObjectPtr<FListener> Listener;
        FCk_ProceduralAction_ManagedStart Start;
    };
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld, FVector{100.0, 200.0, 2000.0});
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Player composes"),
                AddPlayer(State->Fixture, FCk_ProceduralActionPlayer_Spec{})))
            { return; }
            auto* Clip = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
            if (NOT TestNotNull(TEXT("BodyPulse clip loads"), Clip)) { return; }
            State->Listener = MakeListener(State->Fixture);
            State->Start = UCk_Utils_ProceduralAction_UE::Play_Loaded(State->Fixture.Scope,
                State->Fixture.Player, FCk_ProceduralAction_LoadedPlaySpec{Clip},
                State->Listener->MakeCompletionDelegate());
            BindRecords(State->Fixture.Player, State->Start.Get_Identity(), *State->Listener);
            UCk_Utils_ProceduralActionPlayer_UE::BindTo_OnManagedAcceptedPose(State->Fixture.Player,
                State->Start.Get_Identity(), State->Listener->MakePoseDelegate());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return State->Listener.IsValid() && State->Listener->CompletionCount > 0; }),
        WaitSeconds, TEXT("Managed BodyPulse completes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT State->Listener.IsValid()) { return; }
            const auto& Records = State->Listener->Records;
            if (NOT TestTrue(TEXT("Records were delivered"), Records.Num() >= 3)) { return; }
            auto Contiguous = true;
            auto AcceptedCount = 0;
            auto TerminalCount = 0;
            auto ClosureIndex = int32{INDEX_NONE};
            auto AcceptedWithWindow = false;
            for (auto Index = 0; Index < Records.Num(); ++Index)
            {
                const auto& Record = Records[Index];
                Contiguous &= Record.Get_StreamSequence() == Index + 1
                    && Record.Get_Identity().Get_Value() == State->Start.Get_Identity().Get_Value();
                if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose)
                {
                    ++AcceptedCount;
                    AcceptedWithWindow |= NOT Record.Get_Receipt().Get_Interval().Get_Windows().IsEmpty();
                }
                else if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::WindowsClosed)
                { ClosureIndex = Index; }
                else
                { ++TerminalCount; }
            }
            TestTrue(TEXT("Stream sequences start at 1 and increase without gaps"), Contiguous);
            TestTrue(TEXT("Accepted poses expose the clip's windows"), AcceptedWithWindow);
            TestEqual(TEXT("Exactly one terminal record arrives"), TerminalCount, 1);
            TestEqual(TEXT("The last record is the terminal"), Records.Last().Get_Kind(),
                ECk_ProceduralAction_RecordKind::Terminal);
            TestTrue(TEXT("Window closure precedes the terminal"),
                ClosureIndex != INDEX_NONE && ClosureIndex < Records.Num() - 1);
            TestFalse(TEXT("No empty page is delivered"), State->Listener->PageSizes.Contains(0));
            TestEqual(TEXT("The accepted-pose projection sees every accepted record"),
                State->Listener->PoseCalls, AcceptedCount);
            TestEqual(TEXT("Completion follows the last delivered page"),
                State->Listener->RecordsSeenAtCompletion, Records.Num());
            const auto& Completion = State->Listener->LastCompletion;
            TestEqual(TEXT("Producer outcome comes from the terminal record"),
                Completion.Get_Result().Get_Outcome(), Records.Last().Get_Result().Get_Outcome());
            TestEqual(TEXT("BodyPulse completed"), Completion.Get_Result().Get_Outcome(),
                ECk_ProceduralAction_Outcome::Completed);
            TestEqual(TEXT("Return finished before the terminal"), Completion.Get_Result().Get_Recovery(),
                ECk_ProceduralAction_Recovery::Succeeded);
            TestEqual(TEXT("Delivery outcome is separate from producer outcome"), Completion.Get_Delivery(),
                ECk_ProceduralAction_DeliveryStatus::Complete);
            TestEqual(TEXT("Completion fires once"), State->Listener->CompletionCount, 1);
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedSealedTest,
    "Ck.ProceduralAnimation.Action.Managed.PublicConsumeIsSealedReadsStayObservable",
    ck_test_procedural_action_managed::TestFlags)

auto
FCkProceduralActionManagedSealedTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        TStrongObjectPtr<FListener> Listener;
        TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE> Consumer;
        FCk_ProceduralAction_ManagedStart Managed;
        FCk_Handle_ProceduralAction Explicit;
        FCk_ProceduralAction_Identity ExplicitIdentity;
        bool RetainedAfterCompletion = false;
        bool ManagedAfterCompletion = false;
    };
    const auto State = MakeShared<FState>();
    AddExpectedError(ConsumeRejectedManaged, EAutomationExpectedErrorFlags::Contains, 0);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld, FVector{100.0, 200.0, 2000.0});
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Player composes"),
                AddPlayer(State->Fixture, FCk_ProceduralActionPlayer_Spec{})))
            { return; }
            auto* LimbNod = LoadObject<UCk_ProceduralAction_Data>(nullptr, LimbNodPath);
            if (NOT TestNotNull(TEXT("LimbNod loads"), LimbNod)) { return; }
            State->Listener = MakeListener(State->Fixture);
            State->Consumer = TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE>{
                NewObject<UCk_Test_ProceduralActionReentry_UE>()};
            State->Managed = UCk_Utils_ProceduralAction_UE::Play_Loaded(State->Fixture.Scope,
                State->Fixture.Player, MakeRejectedSpec(LimbNod), State->Listener->MakeCompletionDelegate());
            State->Listener->Identity = State->Managed.Get_Identity();
            auto ExplicitScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Fixture.Root);
            State->Explicit = UCk_Utils_ProceduralAction_UE::Create_FromLoadedClip(ExplicitScope,
                State->Fixture.Player, MakeRejectedSpec(LimbNod));
            State->ExplicitIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Explicit);
            const auto ManagedIdentity = State->Managed.Get_Identity();
            if (NOT TestTrue(TEXT("Managed and explicit identities exist"),
                ManagedIdentity.Get_Value() > 0 && State->ExplicitIdentity.Get_Value() > 0))
            { return; }

            const auto& Store = State->Fixture.Player.Get<ck::FFragment_ProceduralActionPlayer>().Get_Receipts();
            TestTrue(TEXT("The store seals the managed stream"), Store.Get_IsManagedDelivery(ManagedIdentity));
            TestFalse(TEXT("The store leaves the explicit stream unsealed"),
                Store.Get_IsManagedDelivery(State->ExplicitIdentity));
            auto AckBefore = int64{-1};
            TestTrue(TEXT("The managed stream exposes its acknowledged position"),
                Store.Get_AcknowledgedThrough(ManagedIdentity, AckBefore));
            const auto ManagedPage = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Fixture.Player, ManagedIdentity, 0, 8);
            TestEqual(TEXT("Reading a managed identity is an ordinary read"), ManagedPage.Get_Result(),
                ECk_ProceduralAction_ReadResult::Succeeded);
            TestEqual(TEXT("Reading a managed identity copies its closure and terminal"),
                ManagedPage.Get_Records().Num(), 2);
            auto AckAfterRead = int64{-1};
            Store.Get_AcknowledgedThrough(ManagedIdentity, AckAfterRead);
            TestEqual(TEXT("A read does not move the managed cursor"), AckAfterRead, AckBefore);

            auto ConsumeCompletion = FCk_Delegate_Request_OnCompleted{};
            ConsumeCompletion.BindDynamic(State->Consumer.Get(),
                &UCk_Test_ProceduralActionReentry_UE::OnInvalidCompleted);
            UCk_Utils_ProceduralActionPlayer_UE::Request_ConsumeReceipts(State->Fixture.Player,
                FCk_Request_ProceduralActionPlayer_ConsumeReceipts{ManagedIdentity,
                    ManagedPage.Get_LastStreamSequence()}, ConsumeCompletion);
            TestEqual(TEXT("A managed consume completes synchronously"),
                State->Consumer->InvalidCompletionCount, 1);
            TestEqual(TEXT("A managed consume is not enqueued"), State->Consumer->InvalidResult,
                ECk_Request_OperationResult::Failed_NotEnqueued);
            auto AckAfterConsume = int64{-1};
            Store.Get_AcknowledgedThrough(ManagedIdentity, AckAfterConsume);
            TestEqual(TEXT("A rejected consume does not move the cursor"), AckAfterConsume, AckBefore);

            TestTrue(TEXT("Managed identity reports managed delivery"),
                UCk_Utils_ProceduralActionPlayer_UE::Get_IsManagedDelivery(State->Fixture.Player, ManagedIdentity));
            TestFalse(TEXT("Explicit identity reports explicit delivery"),
                UCk_Utils_ProceduralActionPlayer_UE::Get_IsManagedDelivery(State->Fixture.Player,
                    State->ExplicitIdentity));
            const auto ExplicitPage = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Fixture.Player, State->ExplicitIdentity, 0, 8);
            TestEqual(TEXT("An explicit terminal stream still reads Succeeded"), ExplicitPage.Get_Result(),
                ECk_ProceduralAction_ReadResult::Succeeded);
            TestEqual(TEXT("An explicit stream keeps closure and terminal"), ExplicitPage.Get_Records().Num(), 2);
            auto ExplicitCompletion = FCk_Delegate_Request_OnCompleted{};
            ExplicitCompletion.BindDynamic(State->Consumer.Get(),
                &UCk_Test_ProceduralActionReentry_UE::OnTuningCompleted);
            UCk_Utils_ProceduralActionPlayer_UE::Request_ConsumeReceipts(State->Fixture.Player,
                FCk_Request_ProceduralActionPlayer_ConsumeReceipts{State->ExplicitIdentity,
                    ExplicitPage.Get_LastStreamSequence()}, ExplicitCompletion);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            if (NOT State->Consumer.IsValid() || NOT State->Listener.IsValid()
                || State->Consumer->TuningCompletionCount == 0 || State->Listener->CompletionCount == 0)
            { return false; }
            State->RetainedAfterCompletion = Get_IsRetained(State->Fixture.Player, State->Managed.Get_Identity());
            State->ManagedAfterCompletion = UCk_Utils_ProceduralActionPlayer_UE::Get_IsManagedDelivery(
                State->Fixture.Player, State->Managed.Get_Identity());
            return true;
        }), WaitSeconds, TEXT("Explicit consume drains and managed delivery completes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT State->Consumer.IsValid() || NOT State->Listener.IsValid()) { return; }
            TestEqual(TEXT("The explicit consume succeeds"), State->Consumer->TuningResult,
                ECk_Request_OperationResult::Succeeded);
            TestEqual(TEXT("The explicit consume completes once"), State->Consumer->TuningCompletionCount, 1);
            TestEqual(TEXT("The managed consume never completes again"), State->Consumer->InvalidCompletionCount, 1);
            TestEqual(TEXT("Managed delivery completes"), State->Listener->LastCompletion.Get_Delivery(),
                ECk_ProceduralAction_DeliveryStatus::Complete);
            TestEqual(TEXT("The managed stream carries its rejection"),
                State->Listener->LastCompletion.Get_Result().Get_Outcome(), ECk_ProceduralAction_Outcome::Rejected);
            TestTrue(TEXT("The seal holds during the completion callback"), State->Listener->ManagedDuringCompletion);
            TestTrue(TEXT("The completed stream is still retained on the next frame"),
                State->RetainedAfterCompletion);
            TestTrue(TEXT("The seal holds while the completed stream is retained"), State->ManagedAfterCompletion);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return NOT Get_IsRetained(State->Fixture.Player, State->Managed.Get_Identity()); }),
        WaitSeconds, TEXT("The released managed stream is reclaimed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestFalse(TEXT("The seal ends with the reclaimed stream"),
                UCk_Utils_ProceduralActionPlayer_UE::Get_IsManagedDelivery(State->Fixture.Player,
                    State->Managed.Get_Identity()));
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedReentrancyTest,
    "Ck.ProceduralAnimation.Action.Managed.ListenerReentrancy",
    ck_test_procedural_action_managed::TestFlags)

auto
FCkProceduralActionManagedReentrancyTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FCase
    {
        EReentry Reentry = EReentry::None;
        FString Name;
        FSubject Fixture;
        TStrongObjectPtr<FListener> Listener;
    };
    struct FState
    {
        TArray<FCase> Cases;
        TStrongObjectPtr<UCk_ProceduralAction_Data> LimbNod;
    };
    const auto State = MakeShared<FState>();
    State->Cases = TArray<FCase>{
        FCase{EReentry::Cancel, TEXT("Cancel")},
        FCase{EReentry::Release, TEXT("Release")},
        FCase{EReentry::DestroyScope, TEXT("DestroyScope")},
        FCase{EReentry::DestroyPlayer, TEXT("DestroyPlayer")},
        FCase{EReentry::Unbind, TEXT("Unbind")},
        FCase{EReentry::PlayAnother, TEXT("PlayAnother")}};

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            for (auto Index = 0; Index < State->Cases.Num(); ++Index)
            {
                auto& Case = State->Cases[Index];
                Case.Fixture = ComposeBodySubject(InWorld, FVector{100.0 + 1000.0 * Index, 200.0, 2000.0});
                TestTrue(*FString::Printf(TEXT("%s subject composes"), *Case.Name), ck::IsValid(Case.Fixture.Subject));
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return Algo::AllOf(State->Cases, [](const FCase& InCase) { return Get_IsSettled(InCase.Fixture); });
        }), WaitSeconds, TEXT("Every reentry subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto* BodyPulse = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
            State->LimbNod = TStrongObjectPtr<UCk_ProceduralAction_Data>{
                LoadObject<UCk_ProceduralAction_Data>(nullptr, LimbNodPath)};
            if (NOT TestNotNull(TEXT("BodyPulse loads"), BodyPulse)
                || NOT TestTrue(TEXT("LimbNod loads"), State->LimbNod.IsValid()))
            { return; }
            for (auto& Case : State->Cases)
            {
                if (NOT TestTrue(*FString::Printf(TEXT("%s player composes"), *Case.Name),
                    AddPlayer(Case.Fixture, FCk_ProceduralActionPlayer_Spec{})))
                { continue; }
                Case.Listener = MakeListener(Case.Fixture);
                Case.Listener->Reentry = Case.Reentry;
                Case.Listener->AnotherScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Case.Fixture.Root);
                Case.Listener->AnotherClip = State->LimbNod.Get();
                const auto Start = UCk_Utils_ProceduralAction_UE::Play_Loaded(Case.Fixture.Scope,
                    Case.Fixture.Player, FCk_ProceduralAction_LoadedPlaySpec{BodyPulse},
                    Case.Listener->MakeCompletionDelegate());
                Case.Listener->Action = Start.Get_Action();
                Case.Listener->Identity = Start.Get_Identity();
                TestTrue(*FString::Printf(TEXT("%s action starts"), *Case.Name), ck::IsValid(Start.Get_Action()));
                BindRecords(Case.Fixture.Player, Start.Get_Identity(), *Case.Listener);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return Algo::AllOf(State->Cases, [](const FCase& InCase)
            {
                return InCase.Listener.IsValid() && InCase.Listener->CompletionCount > 0
                    && (InCase.Reentry != EReentry::PlayAnother || InCase.Listener->AnotherCompletionCount > 0);
            });
        }), 15.0, TEXT("Every reentry case completes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(10));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            for (const auto& Case : State->Cases)
            {
                if (NOT Case.Listener.IsValid()) { continue; }
                const auto& Listener = *Case.Listener;
                const auto& Completion = Listener.LastCompletion;
                const auto Label = [&Case](const TCHAR* InWhat) { return FString::Printf(TEXT("%s: %s"), *Case.Name, InWhat); };
                TestTrue(*Label(TEXT("the reentry ran from a records callback")), Listener.ReentryDone);
                TestEqual(*Label(TEXT("exactly one completion")), Listener.CompletionCount, 1);
                TestEqual(*Label(TEXT("completion names its identity")), Completion.Get_Identity().Get_Value(),
                    Listener.Identity.Get_Value());
                switch (Case.Reentry)
                {
                    case EReentry::Cancel:
                    {
                        TestEqual(*Label(TEXT("cancelled")), Completion.Get_Result().Get_Outcome(),
                            ECk_ProceduralAction_Outcome::Cancelled);
                        TestEqual(*Label(TEXT("delivered")), Completion.Get_Delivery(),
                            ECk_ProceduralAction_DeliveryStatus::Complete);
                        break;
                    }
                    case EReentry::Release:
                    {
                        TestEqual(*Label(TEXT("released as a cancel")), Completion.Get_Result().Get_Outcome(),
                            ECk_ProceduralAction_Outcome::Cancelled);
                        TestEqual(*Label(TEXT("released reason")), Completion.Get_Result().Get_Reason(),
                            FName{TEXT("Released")});
                        TestEqual(*Label(TEXT("delivered")), Completion.Get_Delivery(),
                            ECk_ProceduralAction_DeliveryStatus::Complete);
                        break;
                    }
                    case EReentry::DestroyScope:
                    {
                        TestEqual(*Label(TEXT("owner lost")), Completion.Get_Result().Get_Outcome(),
                            ECk_ProceduralAction_Outcome::OwnerLost);
                        TestEqual(*Label(TEXT("delivered")), Completion.Get_Delivery(),
                            ECk_ProceduralAction_DeliveryStatus::Complete);
                        break;
                    }
                    case EReentry::DestroyPlayer:
                    {
                        TestEqual(*Label(TEXT("player lost")), Completion.Get_Delivery(),
                            ECk_ProceduralAction_DeliveryStatus::PlayerLost);
                        TestEqual(*Label(TEXT("no terminal was delivered")), Completion.Get_Result().Get_Outcome(),
                            ECk_ProceduralAction_Outcome::None);
                        TestTrue(*Label(TEXT("player loss fires while the player ends play")),
                            Listener.PlayerPendingDestroyAtCompletion);
                        break;
                    }
                    case EReentry::Unbind:
                    {
                        TestEqual(*Label(TEXT("no page after unbinding")), Listener.RecordsCalls, 1);
                        TestEqual(*Label(TEXT("delivered")), Completion.Get_Delivery(),
                            ECk_ProceduralAction_DeliveryStatus::Complete);
                        TestEqual(*Label(TEXT("completed")), Completion.Get_Result().Get_Outcome(),
                            ECk_ProceduralAction_Outcome::Completed);
                        break;
                    }
                    case EReentry::PlayAnother:
                    {
                        TestEqual(*Label(TEXT("completed")), Completion.Get_Result().Get_Outcome(),
                            ECk_ProceduralAction_Outcome::Completed);
                        TestTrue(*Label(TEXT("the nested play started")),
                            Listener.AnotherStart.Get_Report().Get_Status() == ECk_ProceduralAction_ReportStatus::Valid
                            && Listener.AnotherStart.Get_Identity().Get_Value() > 0);
                        TestEqual(*Label(TEXT("no nested completion inside Play")), Listener.AnotherCompletionsInsidePlay, 0);
                        TestEqual(*Label(TEXT("exactly one nested completion")), Listener.AnotherCompletionCount, 1);
                        TestTrue(*Label(TEXT("the nested play is eligible on a later pass")),
                            Listener.AnotherCompletionFrame > Listener.AnotherPlayFrame);
                        TestEqual(*Label(TEXT("nested rejection")), Listener.AnotherCompletion.Get_Result().Get_Outcome(),
                            ECk_ProceduralAction_Outcome::Rejected);
                        break;
                    }
                    default:
                        break;
                }
                DestroyRoot(Case.Fixture);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedScopeLostTest,
    "Ck.ProceduralAnimation.Action.Managed.ScopeLostSuppressesDeadRecipientWhileDrainContinues",
    ck_test_procedural_action_managed::TestFlags)

auto
FCkProceduralActionManagedScopeLostTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        TStrongObjectPtr<FListener> Dead;
        TWeakObjectPtr<FListener> DeadWeak;
        TSharedPtr<int32> DeadCompletions = MakeShared<int32>(0);
        TStrongObjectPtr<FListener> Survivor;
        FCk_ProceduralAction_ManagedStart DeadStart;
        FCk_ProceduralAction_ManagedStart SurvivorStart;
        int32 BaselineRetained = 0;
    };
    const auto State = MakeShared<FState>();
    const auto LimbA = FName{TEXT("NodA")};
    const auto LimbB = FName{TEXT("NodB")};

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, LimbA, LimbB](UWorld* InWorld)
        {
            State->Fixture = ComposeLimbSubject(InWorld, FVector{100.0, 200.0, 2000.0},
                TArray<FName>{LimbA, LimbB});
            TestTrue(TEXT("Two-limb subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Two-limb subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, LimbA, LimbB](UWorld*)
        {
            if (NOT TestTrue(TEXT("Slow player composes"), AddPlayer(State->Fixture, MakeSlowPlayerSpec(0.1f))))
            { return; }
            auto* LimbNod = LoadObject<UCk_ProceduralAction_Data>(nullptr, LimbNodPath);
            if (NOT TestNotNull(TEXT("LimbNod loads"), LimbNod)) { return; }
            State->BaselineRetained = Get_RetainedCount(State->Fixture.Player);
            State->Dead = MakeListener(State->Fixture);
            State->DeadWeak = State->Dead.Get();
            State->Dead->ExternalCompletionCount = State->DeadCompletions;
            State->Survivor = MakeListener(State->Fixture);
            State->DeadStart = UCk_Utils_ProceduralAction_UE::Play_Loaded(State->Fixture.Scope,
                State->Fixture.Player, MakeBoundLimbSpec(LimbNod, LimbA), State->Dead->MakeCompletionDelegate());
            State->SurvivorStart = UCk_Utils_ProceduralAction_UE::Play_Loaded(State->Fixture.Scope,
                State->Fixture.Player, MakeBoundLimbSpec(LimbNod, LimbB), State->Survivor->MakeCompletionDelegate());
            TestTrue(TEXT("Both limb actions start under one scope"),
                ck::IsValid(State->DeadStart.Get_Action()) && ck::IsValid(State->SurvivorStart.Get_Action()));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralAction_UE::Get_State(State->DeadStart.Get_Action())
                    == ECk_ProceduralAction_State::Playing
                && UCk_Utils_ProceduralAction_UE::Get_State(State->SurvivorStart.Get_Action())
                    == ECk_ProceduralAction_State::Playing;
        }), WaitSeconds, TEXT("Both limb actions hold claims")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Fixture.Scope);
            auto* Dead = State->Dead.Get();
            State->Dead.Reset();
            Dead->MarkAsGarbage();
            CollectGarbage(RF_NoFlags, true);
            TestFalse(TEXT("The dead recipient is collected"), State->DeadWeak.IsValid());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return State->Survivor.IsValid() && State->Survivor->CompletionCount > 0
                && NOT UCk_Utils_ProceduralActionPlayer_UE::Get_IsManagedDelivery(State->Fixture.Player,
                    State->DeadStart.Get_Identity())
                && Get_RetainedCount(State->Fixture.Player) == State->BaselineRetained;
        }), WaitSeconds, TEXT("Both streams drain after scope loss")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT State->Survivor.IsValid()) { return; }
            TestEqual(TEXT("No callback reaches the dead recipient"), *State->DeadCompletions, 0);
            TestEqual(TEXT("The surviving recipient completes once"), State->Survivor->CompletionCount, 1);
            TestEqual(TEXT("The surviving recipient sees owner loss"),
                State->Survivor->LastCompletion.Get_Result().Get_Outcome(), ECk_ProceduralAction_Outcome::OwnerLost);
            TestTrue(TEXT("Owner loss reports a finished return"),
                State->Survivor->LastCompletion.Get_Result().Get_Recovery() != ECk_ProceduralAction_Recovery::Pending);
            TestEqual(TEXT("The surviving stream was fully delivered"),
                State->Survivor->LastCompletion.Get_Delivery(), ECk_ProceduralAction_DeliveryStatus::Complete);
            TestEqual(TEXT("Store returns to baseline"), Get_RetainedCount(State->Fixture.Player),
                State->BaselineRetained);
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedPlayerLostTest,
    "Ck.ProceduralAnimation.Action.Managed.PlayerLostFiresOnceFromEndPlay",
    ck_test_procedural_action_managed::TestFlags)

auto
FCkProceduralActionManagedPlayerLostTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        FCk_Handle IndependentScope;
        TStrongObjectPtr<FListener> Listener;
        FCk_ProceduralAction_ManagedStart Start;
        int32 PollsAfterCompletion = 0;
    };
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld, FVector{100.0, 200.0, 2000.0});
            State->IndependentScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Slow player composes"), AddPlayer(State->Fixture, MakeSlowPlayerSpec(0.1f))))
            { return; }
            auto* Clip = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
            if (NOT TestNotNull(TEXT("BodyPulse loads"), Clip)) { return; }
            State->Listener = MakeListener(State->Fixture);
            State->Start = UCk_Utils_ProceduralAction_UE::Play_Loaded(State->IndependentScope,
                State->Fixture.Player, FCk_ProceduralAction_LoadedPlaySpec{Clip},
                State->Listener->MakeCompletionDelegate());
            State->Listener->Action = State->Start.Get_Action();
            BindRecords(State->Fixture.Player, State->Start.Get_Identity(), *State->Listener);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return State->Listener.IsValid() && State->Listener->RecordsCalls > 0; }),
        WaitSeconds, TEXT("A first page is delivered")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("No completion before the player is lost"), State->Listener->CompletionCount, 0);
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return State->Listener->CompletionCount > 0; }),
        WaitSeconds, TEXT("Player loss completes the managed entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            ++State->PollsAfterCompletion;
            return ck::Is_NOT_Valid(State->Start.Get_Action());
        }), WaitSeconds, TEXT("The lost action entity is destroyed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(10));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Completion = State->Listener->LastCompletion;
            UE_LOG(LogTemp, Display, TEXT("[ManagedPlayerLost] the lost action became invalid after %d polls past completion"),
                State->PollsAfterCompletion);
            TestEqual(TEXT("Player loss fires once"), State->Listener->CompletionCount, 1);
            TestEqual(TEXT("Delivery reports PlayerLost"), Completion.Get_Delivery(),
                ECk_ProceduralAction_DeliveryStatus::PlayerLost);
            TestEqual(TEXT("No terminal was delivered, so none is reported"), Completion.Get_Result().Get_Outcome(),
                ECk_ProceduralAction_Outcome::None);
            TestEqual(TEXT("Completion names the identity"), Completion.Get_Identity().Get_Value(),
                State->Start.Get_Identity().Get_Value());
            TestTrue(TEXT("Player loss fires from the player's end of play"),
                State->Listener->PlayerPendingDestroyAtCompletion);
            TestTrue(TEXT("The lost action is already being destroyed when PlayerLost fires"),
                State->Listener->ActionPendingDestroyAtCompletion);
            TestTrue(TEXT("The scope that owned the action survives"),
                ck::IsValid(State->IndependentScope)
                    && NOT State->IndependentScope.Has<ck::FTag_DestroyEntity_Initiate>());
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->IndependentScope);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedBudgetTest,
    "Ck.ProceduralAnimation.Action.Managed.BudgetFairnessAndZeroDeltaT",
    ck_test_procedural_action_managed::TestFlags)

auto
FCkProceduralActionManagedBudgetTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        TArray<TStrongObjectPtr<FListener>> Listeners;
    };
    const auto State = MakeShared<FState>();
    constexpr auto Budget = 2;
    constexpr auto Identities = 3;

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld, FVector{100.0, 200.0, 2000.0});
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Spec = FCk_ProceduralActionPlayer_Spec{};
            auto Capacity = Spec.Get_Capacity();
            Capacity.Set_ManagedRecordsPerTick(Budget);
            Spec.Set_Capacity(Capacity);
            if (NOT TestTrue(TEXT("Budgeted player composes"), AddPlayer(State->Fixture, Spec))) { return; }
            auto* LimbNod = LoadObject<UCk_ProceduralAction_Data>(nullptr, LimbNodPath);
            if (NOT TestNotNull(TEXT("LimbNod loads"), LimbNod)) { return; }
            for (auto Index = 0; Index < Identities; ++Index)
            {
                auto Listener = MakeListener(State->Fixture);
                const auto Start = UCk_Utils_ProceduralAction_UE::Play_Loaded(State->Fixture.Scope,
                    State->Fixture.Player, MakeRejectedSpec(LimbNod), Listener->MakeCompletionDelegate());
                Listener->Identity = Start.Get_Identity();
                BindRecords(State->Fixture.Player, Start.Get_Identity(), *Listener);
                State->Listeners.Add(Listener);
            }
            const auto CountRecords = [State]() -> int32
            {
                auto Count = 0;
                for (const auto& Listener : State->Listeners) { Count += Listener->Records.Num(); }
                return Count;
            };
            for (auto Pass = 0; Pass < Identities + 2; ++Pass)
            {
                const auto RecordsBefore = CountRecords();
                auto& Player = State->Fixture.Player;
                ck::FProcessor_ProceduralActionPlayer_ManagedDispatch::ForEachEntity(FCk_Time{}, Player,
                    Player.Get<ck::FFragment_ProceduralActionPlayer>(),
                    Player.Get<ck::FFragment_ProceduralActionPlayer_Managed>());
                const auto Delivered = CountRecords() - RecordsBefore;
                TestTrue(*FString::Printf(TEXT("Pass %d delivers at most the budget"), Pass), Delivered <= Budget);
                if (Pass < Identities)
                {
                    TestEqual(*FString::Printf(TEXT("Pass %d uses its budget"), Pass), Delivered, Budget);
                    for (auto Index = 0; Index < Identities; ++Index)
                    {
                        TestEqual(*FString::Printf(TEXT("After pass %d identity %d completion count"), Pass, Index),
                            State->Listeners[Index]->CompletionCount, Index <= Pass ? 1 : 0);
                    }
                }
                else
                {
                    TestEqual(*FString::Printf(TEXT("Zero-DeltaT pass %d delivers nothing new"), Pass), Delivered, 0);
                }
            }
            for (auto Index = 0; Index < Identities; ++Index)
            {
                const auto& Listener = *State->Listeners[Index];
                TestEqual(*FString::Printf(TEXT("Identity %d receives one page"), Index), Listener.RecordsCalls, 1);
                TestEqual(*FString::Printf(TEXT("Identity %d receives closure and terminal"), Index),
                    Listener.Records.Num(), 2);
                TestEqual(*FString::Printf(TEXT("Identity %d completes as rejected"), Index),
                    Listener.LastCompletion.Get_Result().Get_Outcome(), ECk_ProceduralAction_Outcome::Rejected);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            for (auto Index = 0; Index < State->Listeners.Num(); ++Index)
            {
                TestEqual(*FString::Printf(TEXT("Identity %d is never redelivered"), Index),
                    State->Listeners[Index]->RecordsCalls, 1);
                TestEqual(*FString::Printf(TEXT("Identity %d never completes twice"), Index),
                    State->Listeners[Index]->CompletionCount, 1);
            }
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedLateSubscriptionTest,
    "Ck.ProceduralAnimation.Action.Managed.LateSubscriptionHasNoReplay",
    ck_test_procedural_action_managed::TestFlags)

auto
FCkProceduralActionManagedLateSubscriptionTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed;
    struct FState
    {
        FSubject Fixture;
        TStrongObjectPtr<FListener> Early;
        TStrongObjectPtr<FListener> Late;
        FCk_ProceduralAction_ManagedStart Start;
        int64 EarlyLastAtBind = 0;
    };
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Fixture = ComposeBodySubject(InWorld, FVector{100.0, 200.0, 2000.0});
            TestTrue(TEXT("Body subject composes"), ck::IsValid(State->Fixture.Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(State->Fixture); }),
        WaitSeconds, TEXT("Body subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Slow player composes"), AddPlayer(State->Fixture, MakeSlowPlayerSpec(0.25f))))
            { return; }
            auto* Clip = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
            if (NOT TestNotNull(TEXT("BodyPulse loads"), Clip)) { return; }
            State->Early = MakeListener(State->Fixture);
            State->Late = MakeListener(State->Fixture);
            State->Start = UCk_Utils_ProceduralAction_UE::Play_Loaded(State->Fixture.Scope,
                State->Fixture.Player, FCk_ProceduralAction_LoadedPlaySpec{Clip},
                State->Early->MakeCompletionDelegate());
            BindRecords(State->Fixture.Player, State->Start.Get_Identity(), *State->Early);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return State->Early.IsValid() && State->Early->Records.Num() >= 3; }),
        WaitSeconds, TEXT("Several records are delivered before the late subscription")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("Playback is still running at the late subscription"), State->Early->CompletionCount, 0);
            State->EarlyLastAtBind = State->Early->Records.Last().Get_StreamSequence();
            BindRecords(State->Fixture.Player, State->Start.Get_Identity(), *State->Late);
            TestTrue(TEXT("Binding does not replay earlier pages"), State->Late->Records.IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return State->Early->CompletionCount > 0; }),
        WaitSeconds, TEXT("Managed playback completes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Late = State->Late->Records;
            auto Later = TArray<int64>{};
            for (const auto& Record : State->Early->Records)
            {
                if (Record.Get_StreamSequence() > State->EarlyLastAtBind)
                { Later.Add(Record.Get_StreamSequence()); }
            }
            if (NOT TestTrue(TEXT("The late listener receives later pages"), NOT Late.IsEmpty())) { return; }
            TestEqual(TEXT("The late listener starts right after the last delivered record"),
                Late[0].Get_StreamSequence(), State->EarlyLastAtBind + 1);
            TestEqual(TEXT("The late listener sees exactly the later records"), Late.Num(), Later.Num());
            TestEqual(TEXT("The late listener receives the terminal"), Late.Last().Get_Kind(),
                ECk_ProceduralAction_RecordKind::Terminal);
            DestroyRoot(State->Fixture);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
