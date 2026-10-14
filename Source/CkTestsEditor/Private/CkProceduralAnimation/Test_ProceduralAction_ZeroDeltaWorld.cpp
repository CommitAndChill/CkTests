#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"

#include "CoreGlobals.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_procedural_action_zero_delta
{
    constexpr auto Step = 1.0f / 60.0f;
    constexpr auto MaxAdmissionTicks = 120;
    constexpr auto MaxResumeTicks = 30;

    struct FState
    {
        FTestWorldWrapper Fixture;
        FCk_Handle_ProceduralAction Action;
        FCk_Time Before;
        uint64 LastEditorFrame = MAX_uint64;
        int32 AdmissionTicks = 0;
        int32 ResumeTicks = 0;
        double DeadlineSeconds = 0.0;

        enum class EStage : uint8 { AwaitAccepted, ZeroTick, ResumePositive };
        EStage Stage = EStage::AwaitAccepted;
    };

    class FRunZeroDelta final : public IAutomationLatentCommand
    {
    public:
        FRunZeroDelta(FAutomationTestBase* InTest, TSharedRef<FState> InState)
            : _Test(InTest), _State(InState) {}

        auto Update() -> bool override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _Test->AddError(TEXT("Private Game world zero-delta proof timed out"));
                return Finish();
            }
            if (_State->LastEditorFrame == GFrameCounter)
            { return false; }
            _State->LastEditorFrame = GFrameCounter;

            auto* World = _State->Fixture.GetTestWorld();
            if (World == nullptr)
            {
                _Test->AddError(TEXT("Private Game world disappeared before zero-delta proof"));
                return Finish();
            }

            switch (_State->Stage)
            {
            case FState::EStage::AwaitAccepted:
            {
                World->Tick(LEVELTICK_All, Step);
                ++_State->AdmissionTicks;
                const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(_State->Action);
                if (Receipt.Get_HasAcceptedPose())
                {
                    _State->Before = Receipt.Get_Interval().Get_ToTime();
                    _State->Stage = FState::EStage::ZeroTick;
                    return false;
                }
                if (_State->AdmissionTicks < MaxAdmissionTicks)
                { return false; }
                _Test->AddError(TEXT("Private Game world published no accepted pose in 120 editor frames"));
                return Finish();
            }
            case FState::EStage::ZeroTick:
            {
                World->Tick(LEVELTICK_All, 0.0f);
                _Test->TestTrue(TEXT("Private world actually received zero delta"),
                    FMath::IsNearlyZero(World->GetDeltaSeconds()));
                _Test->TestEqual(TEXT("Zero delta preserves accepted action clock"),
                    UCk_Utils_ProceduralAction_UE::Get_LastReceipt(_State->Action)
                        .Get_Interval().Get_ToTime(), _State->Before);
                _State->Stage = FState::EStage::ResumePositive;
                return false;
            }
            case FState::EStage::ResumePositive:
            {
                World->Tick(LEVELTICK_All, Step);
                ++_State->ResumeTicks;
                const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(_State->Action);
                if (Receipt.Get_HasAcceptedPose()
                    && Receipt.Get_Interval().Get_ToTime() > _State->Before)
                { return Finish(); }
                if (_State->ResumeTicks < MaxResumeTicks)
                { return false; }
                _Test->AddError(TEXT("Accepted clock did not advance within 30 positive private-world ticks"));
                return Finish();
            }
            }
            _Test->AddError(TEXT("Unknown zero-delta test stage"));
            return Finish();
        }

    private:
        auto Finish() -> bool
        {
            const auto Destroyed = _State->Fixture.DestroyTestWorld(true);
            _State->Fixture.ForwardErrorMessages(_Test);
            _Test->TestTrue(TEXT("Private production world tears down"), Destroyed);
            return true;
        }

        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FState> _State;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralActionZeroDeltaWorldTest,
    "Ck.ProceduralAnimation.Action.Editor.ZeroDeltaProductionWorldPreservesAcceptedClock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
FCkProceduralActionZeroDeltaWorldTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_zero_delta;
    auto State = MakeShared<FState>();
    if (NOT TestTrue(TEXT("Private production Game world created"),
            State->Fixture.CreateTestWorld(EWorldType::Game))
        || NOT TestTrue(TEXT("Private production Game world begins play"),
            State->Fixture.BeginPlayInTestWorld()))
    {
        State->Fixture.ForwardErrorMessages(this);
        return false;
    }
    auto* World = State->Fixture.GetTestWorld();
    if (NOT TestNotNull(TEXT("Private Game world exists"), World))
    { return false; }
    World->GetWorldSettings()->MinUndilatedFrameTime = 0.0f;

    auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
    auto Root = UCk_Utils_Transform_UE::Add(RootEntity, FTransform::Identity,
        ECk_Replication::DoesNotReplicate);
    auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
    const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
        FTransform::Identity, ECk_Replication::DoesNotReplicate);
    auto Layout = FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}};
    Layout.Set_BodyEnvelopeMin(FVector{-2.0});
    Layout.Set_BodyEnvelopeMax(FVector{2.0});
    auto Spec = FCk_ProceduralArticulation_Spec{Layout,
        TArray<FCk_ProceduralLimb_Binding>{}};
    Spec.Set_Presentation(Presentation);
    auto Subject = UCk_Utils_ProceduralArticulation_UE::Add(Root, Spec);
    auto Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
        Subject, FCk_ProceduralActionPlayer_Spec{});
    if (NOT TestTrue(TEXT("Production articulation and player compose"),
        ck::IsValid(Subject) && ck::IsValid(Player)))
    { return false; }

    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>()};
    auto Body = FCk_ProceduralAction_BodyChannel{};
    Body.Set_Enabled(true);
    Clip->Set_BodyChannel(Body);
    auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Move")}, FCk_Time{0.2}};
    Phase.Set_BodyKeys({
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0},
            FVector::ZeroVector, FQuat::Identity},
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0},
            FVector{1.0, 0.0, 0.0}, FQuat::Identity}});
    Clip->Set_Phases({Phase});
    auto Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
    State->Action = UCk_Utils_ProceduralAction_UE::Create_FromLoadedClip(
        Scope, Player, FCk_ProceduralAction_LoadedPlaySpec{Clip.Get()});
    if (NOT TestTrue(TEXT("Loaded action reserves against private Game world"),
        ck::IsValid(State->Action)))
    { return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 15.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunZeroDelta(this, State));
    return true;
}

#endif
