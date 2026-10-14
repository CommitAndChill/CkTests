#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkJolt/Query/CkJoltQuery_Utils.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionSolverWorld.h"
#include "HAL/PlatformTime.h"

namespace ck_test_procedural_action_obstacle_preview
{
    const FName LimbId{TEXT("InspectionArm")};

    auto
        MakeInitialSegments()
        -> TArray<FTransform>
    {
        const auto ElbowHeight = FMath::Sqrt(75.0 * 75.0 - 50.0 * 50.0);
        const auto FirstDirection = FVector{50.0, 0.0, ElbowHeight}.GetSafeNormal();
        const auto SecondDirection = FVector{50.0, 0.0, -ElbowHeight}.GetSafeNormal();
        return {
            FTransform{FQuat::FindBetweenNormals(FVector::ForwardVector, FirstDirection),
                FVector{25.0, 0.0, 200.0 + ElbowHeight * 0.5}},
            FTransform{FQuat::FindBetweenNormals(FVector::ForwardVector, SecondDirection),
                FVector{75.0, 0.0, 200.0 + ElbowHeight * 0.5}}};
    }

    auto
        HasCoherentInitialChain(const UCk_ProceduralActionPreview_Config& InConfig)
        -> bool
    {
        const auto& Bindings = InConfig.Get_LimbBindings();
        if (Bindings.Num() != 1)
        { return false; }
        const auto& Frames = Bindings[0].Get_SegmentFrames();
        if (Frames.Num() != 2)
        { return false; }
        const auto FirstStart = Frames[0].GetLocation()
            - Frames[0].GetRotation().GetAxisX() * 37.5;
        const auto FirstEnd = Frames[0].GetLocation()
            + Frames[0].GetRotation().GetAxisX() * 37.5;
        const auto SecondStart = Frames[1].GetLocation()
            - Frames[1].GetRotation().GetAxisX() * 37.5;
        const auto SecondEnd = Frames[1].GetLocation()
            + Frames[1].GetRotation().GetAxisX() * 37.5;
        return FirstStart.Equals(InConfig.Get_RootFrame().GetLocation(), 0.01)
            && FirstEnd.Equals(SecondStart, 0.01)
            && SecondEnd.Equals(Bindings[0].Get_TerminalFrame().GetLocation(), 0.01);
    }

    auto
        DescribeSolver(
            const FCk_ProceduralActionSolverWorld& InSolver,
            const TCHAR* InRequest)
        -> FString
    {
        const auto& Recording = InSolver.GetRecording();
        auto Accepted = int32{0};
        auto Closure = int32{0};
        auto Terminal = int32{0};
        auto TerminalResult = FString{TEXT("none")};
        for (const auto& Record : Recording.Get_Records())
        {
            switch (Record.Get_Kind())
            {
            case ECk_ProceduralAction_RecordKind::AcceptedPose:
                ++Accepted;
                break;
            case ECk_ProceduralAction_RecordKind::WindowsClosed:
                ++Closure;
                break;
            case ECk_ProceduralAction_RecordKind::Terminal:
                ++Terminal;
                TerminalResult = FString::Printf(TEXT("outcome=%d recovery=%d reason=%s"),
                    static_cast<int32>(Record.Get_Result().Get_Outcome()),
                    static_cast<int32>(Record.Get_Result().Get_Recovery()),
                    *Record.Get_Result().Get_Reason().ToString());
                break;
            }
        }
        const auto Action = InSolver.GetAction();
        const auto ActionStatus = ck::IsValid(Action)
            ? FString::Printf(TEXT("state=%d outcome=%d reason=%s"),
                static_cast<int32>(UCk_Utils_ProceduralAction_UE::Get_State(Action)),
                static_cast<int32>(UCk_Utils_ProceduralAction_UE::Get_Result(Action).Get_Outcome()),
                *UCk_Utils_ProceduralAction_UE::Get_Result(Action).Get_Reason().ToString())
            : FString{TEXT("handle invalid")};
        return FString::Printf(
            TEXT("request=%s solverStatus=%d step=%lld action={%s} records=%d accepted=%d closure=%d terminal=%d {%s} readResult=%d copiedThrough=%lld gaps=%d recordingError=%s solverError=%s"),
            InRequest, static_cast<int32>(InSolver.GetStatus()),
            static_cast<long long>(InSolver.GetStep()), *ActionStatus,
            Recording.Get_Records().Num(), Accepted, Closure, Terminal,
            *TerminalResult, static_cast<int32>(Recording.Get_LastReadResult()),
            static_cast<long long>(Recording.Get_LastCopiedSequence()),
            Recording.Get_Gaps().Num(), *Recording.Get_LastError(), *InSolver.GetError());
    }

    auto
        MakeConfig()
        -> TStrongObjectPtr<UCk_ProceduralActionPreview_Config>
    {
        auto Config = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{
            NewObject<UCk_ProceduralActionPreview_Config>(GetTransientPackage())};
        const auto Root = FTransform{FVector{0.0, 0.0, 200.0}};
        Config->Set_RootFrame(Root);
        Config->Set_PresentationFrame(Root);
        auto Limb = FCk_ProceduralLimb_Layout{LimbId, FTransform::Identity,
            TArray<float>{75.0f, 75.0f}, FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector};
        Limb.Set_Clearance(ECk_ProceduralRig_Clearance::Swivel);
        Config->Set_Layout(FCk_ProceduralArticulation_Layout{
            TArray<FCk_ProceduralLimb_Layout>{Limb}});
        auto Clearance = FCk_ProceduralArticulation_ClearanceSettings{};
        Clearance.Set_Query(ECk_ProceduralArticulation_ClearanceQuery::JoltFilter);
        Config->Set_StandaloneClearanceSettings(Clearance);
        auto Binding = FCk_ProceduralActionPreview_LimbBinding{};
        Binding.Set_LimbId(LimbId);
        Binding.Set_SegmentFrames(MakeInitialSegments());
        Binding.Set_TerminalFrame(FTransform{FVector{100.0, 0.0, 200.0}});
        Config->Set_LimbBindings(TArray<FCk_ProceduralActionPreview_LimbBinding>{Binding});
        auto Wall = FCk_ProceduralActionPreview_Obstacle{};
        Wall.Set_Frame(FTransform{FVector{50.0, 0.0, 200.0}});
        Wall.Set_HalfExtents(FVector{5.0, 200.0, 100.0});
        Config->Set_StaticObstacles(TArray<FCk_ProceduralActionPreview_Obstacle>{Wall});
        return Config;
    }

    auto
        MakeBatch()
        -> FCk_ProceduralAction_PoseBatch
    {
        auto Target = FCk_ProceduralAction_LimbTarget{LimbId,
            FTransform{FVector{100.0, 0.0, 0.0}}};
        Target.Set_Space(ECk_ProceduralAction_TrackSpace::BodyLocal);
        auto Batch = FCk_ProceduralAction_PoseBatch{};
        Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{Target});
        return Batch;
    }

    struct FState
    {
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> Config;
        TUniquePtr<FCk_ProceduralActionSolverWorld> Solver;
        double DeadlineSeconds = 0.0;
        int32 Pass = 0;
    };

    struct FProjectionState
    {
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> Config;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TUniquePtr<FCk_ProceduralActionSolverWorld> Solver;
        double DeadlineSeconds = 0.0;
    };

    auto
        MakeProjectionClip()
        -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
            NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
        auto Slot = FCk_ProceduralAction_Slot{LimbId};
        Slot.Set_InitialSource(ECk_ProceduralAction_InitialSource::AuthoredRest);
        Clip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{Slot});
        auto Track = FCk_ProceduralAction_LimbTrack{LimbId};
        Track.Set_Space(ECk_ProceduralAction_TrackSpace::BodyLocal);
        Track.Set_ReachPolicy(ECk_ProceduralAction_ReachPolicy::ProjectToReach);
        Track.Set_ClearanceResponse(ECk_ProceduralAction_ClearanceResponse::ObserveCrossing);
        Track.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{
            FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.0}, FVector{100.0, 0.0, 0.0}},
            FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{1.0}, FVector{200.0, 0.0, 0.0}}});
        auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Overreach")}, FCk_Time{1.0}};
        Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
        Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        return Clip;
    }

    auto
        Schedule(
            FCk_ProceduralActionSolverWorld& InSolver)
        -> bool
    {
        return InSolver.QueuePoseAtStep(8, MakeBatch())
            && InSolver.QueueEndAtStep(24)
            && InSolver.SeekToStep(60);
    }

    class FRunObstaclePreview final : public IAutomationLatentCommand
    {
    public:
        FRunObstaclePreview(FAutomationTestBase* InTest, TSharedRef<FState> InState)
            : _Test(InTest), _State(InState) {}

        auto Update() -> bool override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _Test->AddInfo(DescribeSolver(*_State->Solver, TEXT("manual BodyLocal endpoint (100,0,0) at step 8; End at step 24")));
                _Test->AddError(TEXT("production obstacle preview did not settle within 20 seconds"));
                _State->Solver->Reset();
                return true;
            }
            _State->Solver->TickOncePerEditorFrame();
            if (_State->Solver->GetStatus() == FCk_ProceduralActionSolverWorld::EStatus::Error)
            {
                _Test->AddInfo(DescribeSolver(*_State->Solver, TEXT("manual BodyLocal endpoint (100,0,0) at step 8; End at step 24")));
                _Test->AddError(_State->Solver->GetError());
                _State->Solver->Reset();
                return true;
            }
            if (_State->Solver->GetStep() < 60)
            { return false; }
            auto Accepted = int32{0};
            auto Crossing = int32{0};
            for (const auto& Record : _State->Solver->GetRecording().Get_Records())
            {
                if (Record.Get_Kind() != ECk_ProceduralAction_RecordKind::AcceptedPose
                    || NOT Record.Get_Receipt().Get_HasAcceptedPose())
                { continue; }
                ++Accepted;
                for (const auto& Limb : Record.Get_Receipt().Get_CurrentPose().Get_Limbs())
                {
                    if (Limb.Get_Id() == LimbId
                        && Limb.Get_ChainState() == ECk_ProceduralRig_ChainState::Crossing)
                    { ++Crossing; }
                }
            }
            _Test->TestTrue(TEXT("production Jolt query participates in articulated preview"), Accepted > 0);
            if (Accepted == 0)
            { _Test->AddInfo(DescribeSolver(*_State->Solver, TEXT("manual BodyLocal endpoint (100,0,0) at step 8; End at step 24"))); }
            if (_State->Pass == 0)
            {
                _Test->TestTrue(TEXT("separate obstacle body was added to production Jolt"),
                    _State->Solver->GetAreObstaclesReady());
                const auto Hit = UCk_Utils_JoltQuery_UE::Get_RayCast(
                    _State->Solver->GetWorld(), FVector{0.0, 0.0, 200.0},
                    FVector{100.0, 0.0, 200.0}, FCk_Jolt_QueryFilter{});
                _Test->TestTrue(TEXT("production Jolt ray intersects the preview obstacle"),
                    Hit.Get_HasHit());
                _Test->TestTrue(TEXT("unavoidable static wall is diagnosed as a crossing"), Crossing > 0);
                _State->Solver->Reset();
                _State->Config->Set_StaticObstacles({});
                auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LimbId}};
                Control.Set_InputTimeout(FCk_Time{30.0});
                if (NOT _Test->TestTrue(TEXT("clear fixture rebuilds through production Solver"),
                    _State->Solver->StartManual(_State->Config.Get(), Control, 45)
                    && Schedule(*_State->Solver)))
                { _Test->AddError(_State->Solver->GetError()); _State->Solver->Reset(); return true; }
                _State->Pass = 1;
                return false;
            }
            _Test->TestEqual(TEXT("same requested pose without obstacle has no crossing"), Crossing, 0);
            _State->Solver->Reset();
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FState> _State;
    };

    class FRunProjectionPreview final : public IAutomationLatentCommand
    {
    public:
        FRunProjectionPreview(FAutomationTestBase* InTest, TSharedRef<FProjectionState> InState)
            : _Test(InTest), _State(InState) {}

        auto Update() -> bool override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _Test->AddInfo(DescribeSolver(*_State->Solver, TEXT("clip BodyLocal endpoint X 100->200, ProjectToReach, 1s")));
                _Test->AddError(TEXT("production reach-projection preview did not settle within 20 seconds"));
                _State->Solver->Reset();
                return true;
            }
            _State->Solver->TickOncePerEditorFrame();
            if (_State->Solver->GetStatus() == FCk_ProceduralActionSolverWorld::EStatus::Error)
            {
                _Test->AddInfo(DescribeSolver(*_State->Solver, TEXT("clip BodyLocal endpoint X 100->200, ProjectToReach, 1s")));
                _Test->AddError(_State->Solver->GetError());
                _State->Solver->Reset();
                return true;
            }
            if (_State->Solver->GetStep() < 120)
            { return false; }
            auto FoundProjection = false;
            for (const auto& Record : _State->Solver->GetRecording().Get_Records())
            {
                if (Record.Get_Kind() != ECk_ProceduralAction_RecordKind::AcceptedPose
                    || NOT Record.Get_Receipt().Get_HasAcceptedPose())
                { continue; }
                for (const auto& Limb : Record.Get_Receipt().Get_CurrentPose().Get_Limbs())
                {
                    if (Limb.Get_Id() == LimbId
                        && Limb.Get_RequestedEndpoint().GetLocation().X > 155.0
                        && Limb.Get_AcceptedEndpoint().GetLocation().X <= 150.1
                        && Limb.Get_EndpointErrorCm() > 4.0f)
                    { FoundProjection = true; }
                }
            }
            _Test->TestTrue(TEXT("production receipt distinguishes unreachable requested from projected accepted endpoint"),
                FoundProjection);
            if (NOT FoundProjection)
            { _Test->AddInfo(DescribeSolver(*_State->Solver, TEXT("clip BodyLocal endpoint X 100->200, ProjectToReach, 1s"))); }
            _State->Solver->Reset();
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FProjectionState> _State;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_ObstaclePreview,
    "Ck.ProceduralAnimation.Action.Editor.SolverWorld.ObstacleQuery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkTest_ProceduralAction_ObstaclePreview::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_obstacle_preview;
    auto State = MakeShared<FState>();
    State->Config = MakeConfig();
    TestTrue(TEXT("initial segment endpoints join the root, elbow, and authored rest endpoint"),
        HasCoherentInitialChain(*State->Config));
    auto Error = FString{};
    TestTrue(TEXT("finite bounded static obstacle is valid"), State->Config->ValidateSurface(Error));
    auto Invalid = State->Config->Get_StaticObstacles();
    Invalid[0].Set_HalfExtents(FVector{5.0, 0.0, 100.0});
    State->Config->Set_StaticObstacles(Invalid);
    TestFalse(TEXT("zero-size obstacle is diagnosed before world creation"),
        State->Config->ValidateSurface(Error));
    TestTrue(TEXT("obstacle diagnostic identifies its index"), Error.Contains(TEXT("obstacle 0")));
    State->Config = MakeConfig();
    State->Solver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LimbId}};
    Control.Set_InputTimeout(FCk_Time{30.0});
    if (NOT TestTrue(TEXT("production Solver composes a clearance-enabled obstacle fixture"),
        State->Solver->StartManual(State->Config.Get(), Control, 44)
        && Schedule(*State->Solver)))
    { AddError(State->Solver->GetError()); State->Solver->Reset(); return false; }
    TestEqual(TEXT("obstacle fixture waits for Jolt before action readiness"),
        State->Solver->GetStatus(), FCk_ProceduralActionSolverWorld::EStatus::Preparing);
    State->DeadlineSeconds = FPlatformTime::Seconds() + 20.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunObstaclePreview(this, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_ProjectionPreview,
    "Ck.ProceduralAnimation.Action.Editor.SolverWorld.ReachProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkTest_ProceduralAction_ProjectionPreview::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_obstacle_preview;
    auto State = MakeShared<FProjectionState>();
    State->Config = MakeConfig();
    TestTrue(TEXT("reach fixture initial segment lengths and joints are coherent"),
        HasCoherentInitialChain(*State->Config));
    State->Config->Set_StaticObstacles({});
    State->Config->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{LimbId, LimbId}});
    State->Clip = MakeProjectionClip();
    State->Solver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    if (NOT TestTrue(TEXT("production Solver admits projection clip"),
        State->Solver->StartClip(State->Config.Get(), State->Clip.Get(),
            State->Config->Get_SlotBindings(), 46)
        && State->Solver->SeekToStep(120)))
    { AddError(State->Solver->GetError()); State->Solver->Reset(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 20.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunProjectionPreview(this, State));
    return true;
}

#endif
