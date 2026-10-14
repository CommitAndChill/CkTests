#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimationEditor/CkProceduralActionSolverWorld.h"

#include "HAL/PlatformTime.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"

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

#endif
