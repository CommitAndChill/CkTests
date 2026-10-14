#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimationEditor/CkProceduralActionSolverWorld.h"
#include "CkTest_ProceduralAction_FinishedObserver.h"

#include "HAL/PlatformTime.h"
#include "Tests/AutomationCommon.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

auto
    UCk_Test_ProceduralAction_FinishedObserver_UE::
    OnFinished(
        FCk_Handle_ProceduralAction InAction,
        FCk_ProceduralAction_Terminal InTerminal)
    -> void
{
    ++Calls;
    Terminal = InTerminal;
}

namespace ck_test_procedural_action_managed_clip
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    struct FState
    {
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> Config;
        TUniquePtr<FCk_ProceduralActionSolverWorld> Solver;
        FCk_Handle_ProceduralArticulation Subject;
        double DeadlineSeconds = 0.0;
    };

    class FRunClip final : public IAutomationLatentCommand
    {
    public:
        FRunClip(FAutomationTestBase* InTest, TSharedRef<FState> InState)
            : _Test(InTest), _State(InState) {}

        virtual bool Update() override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _Test->AddError(TEXT("persisted managed clip did not reach a terminal production receipt in 15 seconds"));
                _State->Solver->Reset();
                return true;
            }
            if (_State->Solver->GetStatus() == FCk_ProceduralActionSolverWorld::EStatus::Error)
            {
                _Test->AddError(_State->Solver->GetError());
                _State->Solver->Reset();
                return true;
            }
            _State->Solver->TickOncePerEditorFrame();
            const auto& Recording = _State->Solver->GetRecording();
            if (NOT Recording.Get_Terminal())
            { return false; }

            auto Windup = false;
            auto FollowThrough = false;
            auto Boundary = false;
            auto WindowA = false;
            auto WindowB = false;
            auto Accepted = int32{0};
            auto HasBodyMotion = false;
            for (const auto& Record : Recording.Get_Records())
            {
                if (Record.Get_Kind() != ECk_ProceduralAction_RecordKind::AcceptedPose)
                { continue; }
                const auto& Receipt = Record.Get_Receipt();
                if (NOT Receipt.Get_HasAcceptedPose())
                { continue; }
                ++Accepted;
                Windup |= Receipt.Get_Interval().Get_PhaseId() == FName{TEXT("Windup")};
                FollowThrough |= Receipt.Get_Interval().Get_PhaseId() == FName{TEXT("FollowThrough")};
                for (const auto& Event : Receipt.Get_Interval().Get_Events())
                { Boundary |= Event.Get_Id() == FName{TEXT("PhaseBoundary")}; }
                for (const auto& Window : Receipt.Get_Interval().Get_Windows())
                {
                    WindowA |= Window == FName{TEXT("WindowA")};
                    WindowB |= Window == FName{TEXT("WindowB")};
                }
                if (Receipt.Get_CurrentPose().Get_HasBodyPresentation()
                    && Receipt.Get_CurrentPose().Get_BodyPresentation().GetLocation().X
                        > Receipt.Get_PreviousPose().Get_BodyPresentation().GetLocation().X + 0.01)
                { HasBodyMotion = true; }
            }
            _Test->TestTrue(TEXT("managed clip published accepted production poses"), Accepted > 0);
            _Test->TestTrue(TEXT("Windup phase survived runtime clip load"), Windup);
            _Test->TestTrue(TEXT("FollowThrough phase survived runtime clip load"), FollowThrough);
            _Test->TestTrue(TEXT("boundary marker was published"), Boundary);
            _Test->TestTrue(TEXT("WindowA was published"), WindowA);
            _Test->TestTrue(TEXT("WindowB was published"), WindowB);
            _Test->TestTrue(TEXT("accepted body geometry moved along the authored X pulse"), HasBodyMotion);
            _Test->TestTrue(TEXT("copied stream has a terminal record"), Recording.Get_Terminal());
            _Test->TestTrue(TEXT("recording identifies persisted source clip"),
                Recording.Get_Provenance().Get_SourceAsset().Contains(TEXT("Ck_ProceduralAction_BodyPulse_DA")));
            _State->Solver->Reset();
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
            _Test->TestFalse(TEXT("managed clip subject invalid after preview teardown"),
                ck::IsValid(_State->Subject));
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FState> _State;
    };

    class FRunLimbClip final : public IAutomationLatentCommand
    {
    public:
        FRunLimbClip(FAutomationTestBase* InTest, TSharedRef<FState> InState)
            : _Test(InTest), _State(InState) {}

        auto Update() -> bool override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _Test->AddError(TEXT("persisted LimbNod clip did not reach a terminal production receipt in 15 seconds"));
                _State->Solver->Reset();
                return true;
            }
            if (_State->Solver->GetStatus() == FCk_ProceduralActionSolverWorld::EStatus::Error)
            {
                _Test->AddError(_State->Solver->GetError());
                _State->Solver->Reset();
                return true;
            }
            _State->Solver->TickOncePerEditorFrame();
            const auto& Recording = _State->Solver->GetRecording();
            if (NOT Recording.Get_Terminal())
            { return false; }
            auto Accepted = int32{0};
            auto MovedLimb = false;
            for (const auto& Record : Recording.Get_Records())
            {
                if (Record.Get_Kind() != ECk_ProceduralAction_RecordKind::AcceptedPose
                    || NOT Record.Get_Receipt().Get_HasAcceptedPose())
                { continue; }
                ++Accepted;
                const auto& Receipt = Record.Get_Receipt();
                const auto* Current = Receipt.Get_CurrentPose().Get_Limbs().FindByPredicate(
                    [](const auto& InLimb) { return InLimb.Get_Id() == FName{TEXT("InspectionArm")}; });
                const auto* Previous = Receipt.Get_PreviousPose().Get_Limbs().FindByPredicate(
                    [](const auto& InLimb) { return InLimb.Get_Id() == FName{TEXT("InspectionArm")}; });
                MovedLimb |= Current != nullptr && Previous != nullptr
                    && FVector::Dist(Current->Get_AcceptedEndpoint().GetLocation(),
                        Previous->Get_AcceptedEndpoint().GetLocation()) > 0.01;
            }
            _Test->TestTrue(TEXT("persisted LimbNod produced accepted limb poses"), Accepted > 0);
            _Test->TestTrue(TEXT("persisted LimbNod moved its configured inspection arm"), MovedLimb);
            _Test->TestTrue(TEXT("copied LimbNod stream has a terminal record"), Recording.Get_Terminal());
            _Test->TestTrue(TEXT("recording identifies persisted LimbNod source clip"),
                Recording.Get_Provenance().Get_SourceAsset().Contains(TEXT("Ck_ProceduralAction_LimbNod_DA")));
            _State->Solver->Reset();
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
            _Test->TestFalse(TEXT("persisted LimbNod subject invalid after preview teardown"),
                ck::IsValid(_State->Subject));
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FState> _State;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_ManagedClip_BodyPulse,
    "Ck.ProceduralAnimation.Action.Editor.ManagedClip.BodyPulse",
    ck_test_procedural_action_managed_clip::TestFlags)

auto
    FCkTest_ProceduralAction_ManagedClip_BodyPulse::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed_clip;
    constexpr auto AssetPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    constexpr auto PreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_Preview_DA.Ck_ProceduralAction_BodyPulse_Preview_DA");
    auto State = MakeShared<FState>();
    State->Clip.Reset(LoadObject<UCk_ProceduralAction_Data>(nullptr, AssetPath));
    if (NOT TestNotNull(TEXT("persisted plugin BodyPulse clip loaded"), State->Clip.Get()))
    { return false; }
    State->Config.Reset(LoadObject<UCk_ProceduralActionPreview_Config>(nullptr, PreviewPath));
    if (NOT TestNotNull(TEXT("persisted BodyPulse preview config loaded"), State->Config.Get()))
    { return false; }
    State->Solver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    if (NOT TestTrue(TEXT("persisted managed clip starts through public production player"),
        State->Solver->StartClip(State->Config.Get(), State->Clip.Get(),
            State->Config->Get_SlotBindings(), 27)))
    {
        AddError(State->Solver->GetError());
        return false;
    }
    State->Subject = State->Solver->GetSubject();
    State->DeadlineSeconds = FPlatformTime::Seconds() + 15.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunClip(this, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_ManagedClip_LimbNod,
    "Ck.ProceduralAnimation.Action.Editor.ManagedClip.LimbNod",
    ck_test_procedural_action_managed_clip::TestFlags)

auto
    FCkTest_ProceduralAction_ManagedClip_LimbNod::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_managed_clip;
    constexpr auto AssetPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA");
    constexpr auto PreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_LimbNod_Preview_DA.Ck_ProceduralAction_LimbNod_Preview_DA");
    auto State = MakeShared<FState>();
    State->Clip.Reset(LoadObject<UCk_ProceduralAction_Data>(nullptr, AssetPath));
    State->Config.Reset(LoadObject<UCk_ProceduralActionPreview_Config>(nullptr, PreviewPath));
    if (NOT TestNotNull(TEXT("persisted LimbNod clip loaded"), State->Clip.Get())
        || NOT TestNotNull(TEXT("persisted LimbNod preview config loaded"), State->Config.Get()))
    { return false; }
    State->Solver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    if (NOT TestTrue(TEXT("persisted LimbNod starts through public production player"),
        State->Solver->StartClip(State->Config.Get(), State->Clip.Get(),
            State->Config->Get_SlotBindings(), 28)))
    {
        AddError(State->Solver->GetError());
        return false;
    }
    State->Subject = State->Solver->GetSubject();
    State->DeadlineSeconds = FPlatformTime::Seconds() + 15.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunLimbClip(this, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_ManagedClip_AutoDelivery,
    "Ck.ProceduralAnimation.Action.Editor.ManagedClip.AutoDeliveryFinishesOnceAndReleases",
    ck_test_procedural_action_managed_clip::TestFlags)

auto
    FCkTest_ProceduralAction_ManagedClip_AutoDelivery::
    RunTest(
        const FString&)
    -> bool
{
    constexpr auto AssetPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    const auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        LoadObject<UCk_ProceduralAction_Data>(nullptr, AssetPath)};
    if (NOT TestNotNull(TEXT("persisted plugin BodyPulse clip loaded"), Clip.Get()))
    { return false; }

    auto Fixture = FTestWorldWrapper{};
    if (NOT TestTrue(TEXT("production Game world created"), Fixture.CreateTestWorld(EWorldType::Game))
        || NOT TestTrue(TEXT("production Game world began play"), Fixture.BeginPlayInTestWorld()))
    {
        Fixture.ForwardErrorMessages(this);
        return false;
    }
    auto* World = Fixture.GetTestWorld();
    auto Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
    auto RootTransform = UCk_Utils_Transform_UE::Add(Root, FTransform::Identity,
        ECk_Replication::DoesNotReplicate);
    auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
    const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity, FTransform::Identity,
        ECk_Replication::DoesNotReplicate);
    auto Layout = FCk_ProceduralArticulation_Layout{};
    Layout.Set_BodyEnvelopeMin(FVector{-2.0, -2.0, -2.0});
    Layout.Set_BodyEnvelopeMax(FVector{2.0, 2.0, 2.0});
    auto SubjectSpec = FCk_ProceduralArticulation_Spec{MoveTemp(Layout), TArray<FCk_ProceduralLimb_Binding>{}};
    SubjectSpec.Set_Presentation(Presentation);
    auto Subject = UCk_Utils_ProceduralArticulation_UE::Add(RootTransform, SubjectSpec);
    const auto Player = UCk_Utils_ProceduralActionPlayer_UE::Add(Subject, FCk_ProceduralActionPlayer_Spec{});
    auto Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
    if (NOT TestTrue(TEXT("production body-only subject, player, and scope composed"),
        ck::IsValid(Subject) && ck::IsValid(Player) && ck::IsValid(Scope)))
    {
        Fixture.DestroyTestWorld(true);
        Fixture.ForwardErrorMessages(this);
        return false;
    }

    auto Spec = FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{Clip.Get()}};
    Spec.Set_Delivery(ECk_ProceduralAction_Delivery::Auto);
    auto Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(Scope, Player, Spec);
    if (NOT TestTrue(TEXT("a resident clip creates an Auto action"), ck::IsValid(Action)))
    {
        Fixture.DestroyTestWorld(true);
        Fixture.ForwardErrorMessages(this);
        return false;
    }
    TestEqual(TEXT("a resident clip is compiled and bound inside Create_FromClip"),
        UCk_Utils_ProceduralAction_UE::Get_State(Action), ECk_ProceduralAction_State::PendingAdmission);
    const auto Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(Action);
    const auto Observer = TStrongObjectPtr<UCk_Test_ProceduralAction_FinishedObserver_UE>{
        NewObject<UCk_Test_ProceduralAction_FinishedObserver_UE>(GetTransientPackage())};
    auto OnFinished = FCk_Delegate_ProceduralAction_OnFinished{};
    OnFinished.BindDynamic(Observer.Get(), &UCk_Test_ProceduralAction_FinishedObserver_UE::OnFinished);
    UCk_Utils_ProceduralAction_UE::BindTo_OnFinished(Action, OnFinished);

    const auto IsRetained = [&]()
    {
        return UCk_Utils_ProceduralActionPlayer_UE::Get_RetainedReceiptIdentities(Player)
            .ContainsByPredicate([&](const FCk_ProceduralAction_Identity& InIdentity)
            { return InIdentity.Get_Value() == Identity.Get_Value(); });
    };
    auto FinishedWhileAlive = false;
    for (auto TickIndex = 0; TickIndex < 600 && (Observer->Calls == 0 || ck::IsValid(Action) || IsRetained());
        ++TickIndex)
    {
        if (NOT Fixture.TickTestWorld(1.0f / 60.0f))
        { break; }
        FinishedWhileAlive |= Observer->Calls > 0 && ck::IsValid(Action);
    }
    TestEqual(TEXT("OnFinished fires exactly once"), Observer->Calls, 1);
    TestEqual(TEXT("the terminal names the started identity"),
        Observer->Terminal.Get_Identity().Get_Value(), Identity.Get_Value());
    TestEqual(TEXT("the body clip completes"),
        Observer->Terminal.Get_Result().Get_Outcome(), ECk_ProceduralAction_Outcome::Completed);
    TestTrue(TEXT("the player releases the Auto action after its terminal"), ck::Is_NOT_Valid(Action));
    TestFalse(TEXT("the released action's stream is reclaimed with it"), IsRetained());
    AddInfo(FString::Printf(TEXT("OnFinished observed while the action was still alive: %d"),
        FinishedWhileAlive ? 1 : 0));

    const auto Destroyed = Fixture.DestroyTestWorld(true);
    Fixture.ForwardErrorMessages(this);
    TestTrue(TEXT("production Game world tears down"), Destroyed);
    return NOT HasAnyErrors();
}

#endif
