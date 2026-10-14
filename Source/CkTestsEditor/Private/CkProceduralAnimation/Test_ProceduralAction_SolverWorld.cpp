#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkJolt/Query/CkJoltQuery_Utils.h"
#include "CkJolt/Subsystem/CkJolt_Subsystem.h"
#include "CkProceduralAnimationEditor/CkProceduralActionSolverWorld.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Fragment.h"
#include "CkProceduralAnimation/Debug/CkProceduralAnimation_Debug.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Fragment.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkProceduralAnimation/LimbControl/CkProceduralLimbControl_Fragment_Data.h"
#include "CkTest_ProceduralAction_DetachObserver.h"
#include "BaseClasses/ScriptWorldSubsystem.h"

#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/PlatformTime.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"

auto
    UCk_Test_ProceduralAction_DetachObserver_UE::
    OnDetached(
        FCk_Handle InRequestOwner,
        ECk_Request_OperationResult InResult)
    -> void
{
    ++Calls;
    Succeeded = InResult == ECk_Request_OperationResult::Succeeded && InRequestOwner == Leg;
    LegDetached = ck::IsValid(Leg) && Leg.Has<ck::FTag_ProceduralLeg_Detached>();
    SegmentStillLive = ck::IsValid(Segment) && NOT Segment.Has<ck::FTag_DestroyEntity_Initiate>();
    HealthOperational = ck::IsValid(Subject)
        && Subject.Has<ck::FFragment_ProceduralArticulation_Health>()
        && Subject.Get<ck::FFragment_ProceduralArticulation_Health>().Get_State()
            == ECk_ProceduralArticulation_Health::Operational;
    const auto Snapshot = UCk_Utils_ProceduralAnimation_Debug_UE::Get_ControlSnapshot(Subject);
    HadSettledRevision = Snapshot.Get_Available() && Snapshot.Get_SettledRevision() > 0;
    ExposedStalePose = Snapshot.Get_HasSettledPose();
}

namespace ck_test_procedural_action_solver_world
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const auto LimbId = FName{TEXT("PreviewArm")};

    struct FState
    {
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> Config;
        TUniquePtr<FCk_ProceduralActionSolverWorld> Solver;
        TWeakObjectPtr<UWorld> FirstWorld;
        TWeakObjectPtr<ACk_EcsWorld_Actor_UE> FirstScheduler;
        FCk_Handle_ProceduralArticulation FirstSubject;
        double DeadlineSeconds = 0.0;
        bool CheckedSingleFrame = false;
        bool Rebuilding = false;
        bool CheckFinishedBoundary = false;
        bool CheckExplicitFutureStep = false;
        bool CheckFutureBoundary = false;
        bool FinishedBoundaryDone = false;
        TOptional<FTransform> FirstAttackReceiptEndpoint;
        TStrongObjectPtr<UCk_Test_ProceduralAction_DetachObserver_UE> DetachObserver;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
    };

    auto
        MakeConfig()
        -> TStrongObjectPtr<UCk_ProceduralActionPreview_Config>
    {
        auto Result = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{
            NewObject<UCk_ProceduralActionPreview_Config>(GetTransientPackage())};
        const auto Root = FTransform{FVector{0.0, 0.0, 200.0}};
        Result->Set_RootFrame(Root);
        Result->Set_PresentationFrame(Root);
        const auto Layout = FCk_ProceduralLimb_Layout{LimbId, FTransform::Identity,
            TArray<float>{100.0f}, FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector};
        Result->Set_Layout(FCk_ProceduralArticulation_Layout{
            TArray<FCk_ProceduralLimb_Layout>{Layout}});
        auto Binding = FCk_ProceduralActionPreview_LimbBinding{};
        Binding.Set_LimbId(LimbId);
        Binding.Set_SegmentFrames(TArray<FTransform>{FTransform{FVector{50.0, 0.0, 200.0}}});
        Binding.Set_TerminalFrame(FTransform{FVector{100.0, 0.0, 200.0}});
        Result->Set_LimbBindings(TArray<FCk_ProceduralActionPreview_LimbBinding>{Binding});
        Result->Set_HasStaticFloor(true);
        Result->Set_StaticFloorFrame(FTransform{FVector{0.0, 0.0, -10.0}});
        return Result;
    }

    auto
        MakeBodyConfig()
        -> TStrongObjectPtr<UCk_ProceduralActionPreview_Config>
    {
        auto Result = MakeConfig();
        Result->Set_Layout(FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}});
        Result->Set_LimbBindings(TArray<FCk_ProceduralActionPreview_LimbBinding>{});
        return Result;
    }

    auto
        LastAcceptedTime(const FCk_ProceduralActionSolverWorld& InSolver)
        -> double
    {
        auto Result = 0.0;
        for (const auto& Record : InSolver.GetRecording().Get_Records())
        {
            if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose)
            { Result = FMath::Max(Result, Record.Get_Receipt().Get_Interval().Get_ToTime().Get_Seconds()); }
        }
        return Result;
    }

    struct FTuningState
    {
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> DefaultConfig;
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> TunedConfig;
        TUniquePtr<FCk_ProceduralActionSolverWorld> DefaultSolver;
        TUniquePtr<FCk_ProceduralActionSolverWorld> TunedSolver;
        double DeadlineSeconds = 0.0;
        bool OriginalDirty = false;
        TArray<FCk_ProceduralAction_Phase> OriginalPhases;
    };

    class FRunPreviewTuning final : public IAutomationLatentCommand
    {
    public:
        FRunPreviewTuning(FAutomationTestBase* InTest, TSharedRef<FTuningState> InState)
            : _Test(InTest), _State(InState) {}

        auto Update() -> bool override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _Test->AddError(TEXT("two production Solver tuning runs timed out"));
                _State->DefaultSolver->Reset();
                _State->TunedSolver->Reset();
                return true;
            }
            if (_State->DefaultSolver->GetStatus() == FCk_ProceduralActionSolverWorld::EStatus::Error
                || _State->TunedSolver->GetStatus() == FCk_ProceduralActionSolverWorld::EStatus::Error)
            {
                _Test->AddError(_State->DefaultSolver->GetError() + TEXT(" / ")
                    + _State->TunedSolver->GetError());
                _State->DefaultSolver->Reset();
                _State->TunedSolver->Reset();
                return true;
            }
            _State->DefaultSolver->TickOncePerEditorFrame();
            _State->TunedSolver->TickOncePerEditorFrame();
            if (_State->DefaultSolver->GetStep() < 36 || _State->TunedSolver->GetStep() < 36)
            { return false; }
            const auto DefaultTime = LastAcceptedTime(*_State->DefaultSolver);
            const auto TunedTime = LastAcceptedTime(*_State->TunedSolver);
            _Test->TestTrue(TEXT("both production Solvers accepted authored body motion"),
                DefaultTime > 0.0 && TunedTime > 0.0);
            _Test->TestTrue(TEXT("preview-only half play rate advances less accepted clip time"),
                DefaultTime > TunedTime + 0.04);
            const auto& CurrentPhases = _State->Clip->Get_Phases();
            _Test->TestEqual(TEXT("Solver tuning leaves authored clip phase count unchanged"),
                CurrentPhases.Num(), _State->OriginalPhases.Num());
            if (NOT CurrentPhases.IsEmpty() && NOT _State->OriginalPhases.IsEmpty())
            {
                _Test->TestEqual(TEXT("Solver tuning leaves authored phase duration unchanged"),
                    CurrentPhases[0].Get_Duration().Get_Seconds(),
                    _State->OriginalPhases[0].Get_Duration().Get_Seconds());
            }
            _Test->TestEqual(TEXT("Solver tuning leaves authored clip package dirty state unchanged"),
                _State->Clip->GetOutermost()->IsDirty(), _State->OriginalDirty);
            _State->DefaultSolver->Reset();
            _State->TunedSolver->Reset();
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FTuningState> _State;
    };

    auto
        MakeBatch()
        -> FCk_ProceduralAction_PoseBatch
    {
        const auto Radians = FMath::DegreesToRadians(1.0f);
        const auto Offset = FTransform{FQuat{FVector::UpVector, Radians},
            FVector{FMath::Cos(Radians) - 1.0f, FMath::Sin(Radians), 0.0f}};
        auto Target = FCk_ProceduralAction_LimbTarget{LimbId, Offset};
        Target.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
        auto Batch = FCk_ProceduralAction_PoseBatch{};
        Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{Target});
        return Batch;
    }

    auto
        MakeWalkingBatch()
        -> FCk_ProceduralAction_PoseBatch
    {
        auto Target = FCk_ProceduralAction_LimbTarget{FName{TEXT("Leg0")}, FTransform::Identity};
        Target.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
        auto Batch = FCk_ProceduralAction_PoseBatch{};
        Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{Target});
        return Batch;
    }

    auto
        MakeWalkingConfig()
        -> TStrongObjectPtr<UCk_ProceduralActionPreview_Config>
    {
        auto Result = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{
            NewObject<UCk_ProceduralActionPreview_Config>(GetTransientPackage())};
        Result->Set_SubjectMode(ECk_ProceduralActionPreview_SubjectMode::Walking);
        Result->Set_RootFrame(FTransform{FVector{0.0, 0.0, 60.0}});
        Result->Set_HasStaticFloor(true);
        Result->Set_StaticFloorFrame(FTransform{FVector{0.0, 0.0, -25.0}});
        Result->Set_StaticFloorHalfExtents(FVector{800.0, 800.0, 25.0});
        auto* Rig = NewObject<UCk_ProceduralRig_Data>(Result.Get());
        auto* Gait = NewObject<UCk_ProceduralGait_Data>(Result.Get());
        auto Legs = TArray<FCk_ProceduralLeg_Spec>{};
        const auto Corners = TArray<FVector2D>{{40.0, 30.0}, {40.0, -30.0}, {-40.0, 30.0}, {-40.0, -30.0}};
        for (auto Index = 0; Index < Corners.Num(); ++Index)
        {
            const auto& Corner = Corners[Index];
            auto Placement = FCk_ProceduralLeg_Placement{
                FVector{Corner.X, Corner.Y, 0.0}, FVector{Corner.X, Corner.Y * 2.0, -60.0}};
            Placement.Set_PhaseOffset(Index == 0 || Index == 3 ? 0.0f : 0.5f);
            Legs.Emplace(FName{*FString::Printf(TEXT("Leg%d"), Index)}, Placement,
                FCk_ProceduralLeg_ChainGeometry{TArray<float>{60.0f, 80.0f}});
        }
        Rig->Set_Legs(Legs);
        Result->Set_WalkingRig(Rig);
        Result->Set_WalkingGait(Gait);
        auto Binding = FCk_ProceduralActionPreview_LimbBinding{};
        Binding.Set_LimbId(FName{TEXT("Leg0")});
        Binding.Set_SegmentFrames(TArray<FTransform>{
            FTransform{FVector{40.0, 30.0, 60.0}}, FTransform{FVector{40.0, 45.0, 30.0}}});
        Binding.Set_TerminalFrame(FTransform{FVector{40.0, 60.0, 0.0}});
        Result->Set_LimbBindings(TArray<FCk_ProceduralActionPreview_LimbBinding>{Binding});
        auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
        Locomotion.Set_Support(FCk_ProceduralLimbControl_Support{
            ECk_ProceduralLimbControl_SupportMode::CountOnly, 3});
        auto Return = FCk_ProceduralLimbControl_Return{};
        Return.Set_Duration(FCk_Time{0.2});
        Return.Set_Timeout(FCk_Time{1.0});
        Locomotion.Set_Return(Return);
        Result->Set_WalkingLocomotion(Locomotion);
        return Result;
    }

    auto
        FirstScheduler(UWorld& InWorld)
        -> TWeakObjectPtr<ACk_EcsWorld_Actor_UE>
    {
        for (TActorIterator<ACk_EcsWorld_Actor_UE> It{&InWorld}; It; ++It)
        { return *It; }
        return {};
    }

    class FRunSolver final : public IAutomationLatentCommand
    {
    public:
        FRunSolver(FAutomationTestBase* InTest, TSharedRef<FState> InState)
            : _Test(InTest), _State(InState) {}

        virtual bool Update() override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _Test->AddError(TEXT("private production Solver did not complete within 15 seconds"));
                _State->Solver->Reset();
                return true;
            }
            if (_State->Solver->GetStatus() == FCk_ProceduralActionSolverWorld::EStatus::Error)
            {
                _Test->AddError(_State->Solver->GetError());
                _State->Solver->Reset();
                return true;
            }
            if (_State->CheckFinishedBoundary)
            {
                _State->CheckFinishedBoundary = false;
                const auto FrameBefore = GFrameCounter;
                _Test->TestFalse(TEXT("terminal Solver refuses a direct tick on a later editor frame"),
                    _State->Solver->TickOncePerEditorFrame());
                _Test->TestEqual(TEXT("terminal direct tick preserves its step"),
                    _State->Solver->GetStep(), int64{120});
                _Test->TestEqual(TEXT("terminal direct tick preserves the global frame counter"),
                    GFrameCounter, FrameBefore);
                if (NOT _Test->TestTrue(TEXT("explicit future seek reopens the terminal Solver"),
                    _State->Solver->SeekToStep(121)))
                { _State->Solver->Reset(); return true; }
                _State->CheckExplicitFutureStep = true;
                return false;
            }
            if (_State->CheckExplicitFutureStep)
            {
                const auto FrameBefore = GFrameCounter;
                _State->Solver->TickOncePerEditorFrame();
                _Test->TestEqual(TEXT("explicit seek does not mutate the global frame counter"),
                    GFrameCounter, FrameBefore);
                if (_State->Solver->GetStep() < 121)
                { return false; }
                _Test->TestEqual(TEXT("explicit seek advances one step beyond terminal"),
                    _State->Solver->GetStep(), int64{121});
                _Test->TestEqual(TEXT("explicit seek returns to Finished at its target"),
                    _State->Solver->GetStatus(), FCk_ProceduralActionSolverWorld::EStatus::Finished);
                _State->CheckExplicitFutureStep = false;
                _State->CheckFutureBoundary = true;
                return false;
            }
            if (_State->CheckFutureBoundary)
            {
                _State->CheckFutureBoundary = false;
                const auto FrameBefore = GFrameCounter;
                _Test->TestFalse(TEXT("completed explicit seek refuses another direct tick"),
                    _State->Solver->TickOncePerEditorFrame());
                _Test->TestEqual(TEXT("completed explicit seek stops at its target"),
                    _State->Solver->GetStep(), int64{121});
                _Test->TestEqual(TEXT("completed seek leaves global frame counter unchanged"),
                    GFrameCounter, FrameBefore);
            }
            if (NOT _State->Rebuilding)
            {
                const auto Before = _State->Solver->GetStep();
                const auto FirstTick = _State->Solver->TickOncePerEditorFrame();
                if (FirstTick && NOT _State->CheckedSingleFrame)
                {
                    _State->CheckedSingleFrame = true;
                    const auto AfterFirst = _State->Solver->GetStep();
                    _Test->TestFalse(TEXT("second world tick in same real editor frame is refused"),
                        _State->Solver->TickOncePerEditorFrame());
                    _Test->TestEqual(TEXT("refused tick does not advance simulation"),
                        _State->Solver->GetStep(), AfterFirst);
                    _Test->TestTrue(TEXT("first tick advances at most one action step while admission prepares"),
                        AfterFirst == Before || AfterFirst == Before + 1);
                }
                if (NOT _State->FirstAttackReceiptEndpoint.IsSet())
                {
                    for (const auto& Record : _State->Solver->GetRecording().Get_Records())
                    {
                        if (Record.Get_Kind() != ECk_ProceduralAction_RecordKind::AcceptedPose
                            || NOT Record.Get_Receipt().Get_HasAcceptedPose())
                        { continue; }
                        const auto* Limb = Record.Get_Receipt().Get_CurrentPose().Get_Limbs()
                            .FindByPredicate([](const auto& InLimb) { return InLimb.Get_Id() == LimbId; });
                        if (Limb != nullptr)
                        { _State->FirstAttackReceiptEndpoint = Limb->Get_AcceptedEndpoint(); break; }
                    }
                }
                if (_State->Solver->GetStep() < 120)
                { return false; }
                if (NOT _State->FinishedBoundaryDone)
                {
                    _State->FinishedBoundaryDone = true;
                    _Test->TestEqual(TEXT("terminal playback stops before explicit future seek"),
                        _State->Solver->GetStatus(), FCk_ProceduralActionSolverWorld::EStatus::Finished);
                    _State->CheckFinishedBoundary = true;
                    return false;
                }
                const auto& Recording = _State->Solver->GetRecording();
                _Test->TestTrue(TEXT("production manual action copied an accepted receipt"),
                    Recording.Get_Records().ContainsByPredicate([](const auto& InRecord)
                    { return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose; }));
                auto AcceptedCount = int32{0};
                for (const auto& Record : Recording.Get_Records())
                {
                    if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose)
                    { ++AcceptedCount; }
                }
                _Test->TestTrue(TEXT("copied and acknowledged stream passed its 64 ordinary-record capacity"),
                    AcceptedCount > 64);
                _Test->TestTrue(TEXT("production manual action copied a terminal record"), Recording.Get_Terminal());
                const auto* LastAccepted = static_cast<const FCk_ProceduralAction_Record*>(nullptr);
                for (const auto& Record : Recording.Get_Records())
                {
                    if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose
                        && Record.Get_Receipt().Get_HasAcceptedPose())
                    { LastAccepted = &Record; }
                }
                const auto Live = UCk_Utils_ProceduralAnimation_Debug_UE::Get_ControlSnapshot(
                    _State->Solver->GetSubject().ConvertToHandle());
                _Test->TestTrue(TEXT("postterminal subject retains a healthy settled live pose"),
                    Live.Get_Available() && Live.Get_HasSettledPose() && Live.Get_SettledRevision() > 0);
                if (LastAccepted != nullptr && Live.Get_HasSettledPose())
                {
                    const auto* RecordedLimb = LastAccepted->Get_Receipt().Get_CurrentPose().Get_Limbs()
                        .FindByPredicate([](const auto& InLimb) { return InLimb.Get_Id() == LimbId; });
                    const auto* LiveLimb = Live.Get_SettledPose().Get_Limbs()
                        .FindByPredicate([](const auto& InLimb) { return InLimb.Get_Id() == LimbId; });
                    _Test->TestTrue(TEXT("settled return and last attack receipt retain separate limb geometry"),
                        RecordedLimb != nullptr && LiveLimb != nullptr
                        && FVector::Distance(RecordedLimb->Get_AcceptedEndpoint().GetLocation(),
                            LiveLimb->Get_AcceptedEndpoint().GetLocation()) > 0.1);
                }
                const auto* FirstRecorded = Recording.Get_Records().FindByPredicate([](const auto& InRecord)
                    { return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose
                        && InRecord.Get_Receipt().Get_HasAcceptedPose(); });
                const auto* FirstRecordedLimb = FirstRecorded != nullptr
                    ? FirstRecorded->Get_Receipt().Get_CurrentPose().Get_Limbs().FindByPredicate(
                        [](const auto& InLimb) { return InLimb.Get_Id() == LimbId; })
                    : nullptr;
                _Test->TestTrue(TEXT("return observation leaves the earlier recorded attack pose unchanged"),
                    _State->FirstAttackReceiptEndpoint.IsSet() && FirstRecordedLimb != nullptr
                    && _State->FirstAttackReceiptEndpoint.GetValue().Equals(
                        FirstRecordedLimb->Get_AcceptedEndpoint()));
                _Test->TestTrue(TEXT("production Jolt floor completed body admission"),
                    _State->Solver->GetIsFloorReady());
                const auto Hit = UCk_Utils_JoltQuery_UE::Get_RayCast(_State->Solver->GetWorld(),
                    FVector{0.0, 0.0, 100.0}, FVector{0.0, 0.0, -100.0}, FCk_Jolt_QueryFilter{});
                _Test->TestTrue(TEXT("private Solver world has a real Jolt floor query"), Hit.Get_HasHit());
                _State->FirstWorld = _State->Solver->GetWorld();
                _State->FirstScheduler = FirstScheduler(*_State->Solver->GetWorld());
                _State->FirstSubject = _State->Solver->GetSubject();
                if (NOT _State->Solver->SeekToStep(10))
                {
                    _Test->AddError(_State->Solver->GetError());
                    _State->Solver->Reset();
                    return true;
                }
                _State->Rebuilding = true;
                _Test->TestEqual(TEXT("backward seek rebuilt from canonical step zero"),
                    _State->Solver->GetStep(), int64{0});
                _Test->TestFalse(TEXT("old subject registry invalidated by rebuild"),
                    ck::IsValid(_State->FirstSubject));
                return false;
            }
            _State->Solver->TickOncePerEditorFrame();
            if (_State->Solver->GetStep() < 10)
            { return false; }
            _Test->TestEqual(TEXT("cancellable seek reaches requested fixed step"),
                _State->Solver->GetStep(), int64{10});
            _Test->TestTrue(TEXT("recorded provenance tracks preview revision"),
                _State->Solver->GetRecording().Get_Provenance().Get_PreviewRevision() == 17);
            _Test->TestTrue(TEXT("forward replay can be scheduled"),
                _State->Solver->SeekToStep(30));
            _State->Solver->CancelSeek();
            _Test->TestEqual(TEXT("cancelled replay stays at its current step"),
                _State->Solver->GetStep(), int64{10});
            _Test->TestFalse(TEXT("cancelled replay does not tick its world"),
                _State->Solver->TickOncePerEditorFrame());
            _State->Solver->Reset();
            const auto Removed = UCk_Utils_ProceduralAnimation_Debug_UE::Get_ControlSnapshot(
                _State->FirstSubject.ConvertToHandle());
            _Test->TestFalse(TEXT("destroyed preview subject exposes no stale settled pose"),
                Removed.Get_Available() || Removed.Get_HasSettledPose());
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
            _Test->TestFalse(TEXT("old private world collected"), _State->FirstWorld.IsValid());
            _Test->TestFalse(TEXT("old production scheduler collected"), _State->FirstScheduler.IsValid());
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FState> _State;
    };

    class FRunWalkingBodyClip final : public IAutomationLatentCommand
    {
    public:
        FRunWalkingBodyClip(FAutomationTestBase* InTest, TSharedRef<FState> InState)
            : _Test(InTest), _State(InState) {}

        virtual bool Update() override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _Test->AddError(TEXT("the walking preview did not finish its body clip within 20 seconds"));
                _State->Solver->Reset();
                return true;
            }
            _State->Solver->TickOncePerEditorFrame();
            if (_State->Solver->GetStatus() == FCk_ProceduralActionSolverWorld::EStatus::Error)
            {
                _Test->AddError(_State->Solver->GetError());
                _State->Solver->Reset();
                return true;
            }
            if (NOT _State->Solver->GetIsActionTerminal())
            { return false; }
            const auto Action = _State->Solver->GetAction();
            _Test->TestTrue(TEXT("the body clip completes on the walking subject"), ck::IsValid(Action)
                && UCk_Utils_ProceduralAction_UE::Get_Result(Action).Get_Outcome() == ECk_ProceduralAction_Outcome::Completed);
            _State->Solver->Reset();
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FState> _State;
    };

    class FRunWalkingSolver final : public IAutomationLatentCommand
    {
    public:
        FRunWalkingSolver(FAutomationTestBase* InTest, TSharedRef<FState> InState)
            : _Test(InTest), _State(InState) {}

        virtual bool Update() override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _Test->AddError(TEXT("walking preview did not borrow and replant within 20 seconds"));
                _State->Solver->Reset();
                return true;
            }
            _State->Solver->TickOncePerEditorFrame();
            if (_DetachRequested && _State->DetachObserver.IsValid()
                && _State->DetachObserver->Calls > 0)
            {
                const auto& Observer = *_State->DetachObserver.Get();
                _Test->TestTrue(TEXT("public detach request completed in the production world"), Observer.Succeeded);
                _Test->TestTrue(TEXT("callback observes detached structural limb"), Observer.LegDetached);
                _Test->TestTrue(TEXT("released segment remains live under KeepBodyOwned"),
                    Observer.SegmentStillLive);
                _Test->TestTrue(TEXT("detachment is observed before a new articulation fault or settlement"),
                    Observer.HealthOperational && Observer.HadSettledRevision);
                _Test->TestFalse(TEXT("detached live parts are not exposed as current settled subject geometry"),
                    Observer.ExposedStalePose);
                _State->Solver->Reset();
                return true;
            }
            if (_State->Solver->GetStatus() == FCk_ProceduralActionSolverWorld::EStatus::Error)
            {
                _Test->AddError(_State->Solver->GetError());
                _State->Solver->Reset();
                return true;
            }
            if (NOT _Scheduled)
            {
                if (_State->Solver->GetStatus() != FCk_ProceduralActionSolverWorld::EStatus::Ready)
                { return false; }
                _Test->TestTrue(TEXT("walking preparation waited for Jolt floor"),
                    _State->Solver->GetIsFloorReady());
                _Test->TestEqual(TEXT("walking action time begins after gait preparation"),
                    _State->Solver->GetStep(), int64{0});
                _Test->TestTrue(TEXT("walking action admits a borrowed limb"),
                    UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(
                        _State->Solver->GetSubject(), FName{TEXT("Leg0")}).Get_State()
                        == ECk_ProceduralLimbControl_State::Controlled);
                if (NOT _State->Solver->QueuePoseAtStep(1, MakeWalkingBatch())
                    || NOT _State->Solver->QueueEndAtStep(5))
                { _Test->AddError(TEXT("walking preview rejected its deterministic manual schedule")); return true; }
                _Scheduled = true;
                return false;
            }
            if (_DetachRequested)
            { return false; }
            if (_State->Solver->GetStep() < 90)
            { return false; }
            _Test->TestTrue(TEXT("walking receipt stream reaches terminal"),
                _State->Solver->GetRecording().Get_Terminal());
            const auto TerminalStep = _State->Solver->GetStep();
            _Test->TestFalse(TEXT("seek beyond walking observation horizon rejects"),
                _State->Solver->SeekToStep(121));
            _Test->TestTrue(TEXT("rejected horizon seek identifies its limit"),
                _State->Solver->GetError().Contains(TEXT("observation horizon")));
            _Test->TestEqual(TEXT("rejected horizon seek retains the current step"),
                _State->Solver->GetStep(), TerminalStep);
            _Test->TestEqual(TEXT("rejected horizon seek keeps the scenario resumable"),
                _State->Solver->GetStatus(), FCk_ProceduralActionSolverWorld::EStatus::Ready);
            _Test->TestTrue(TEXT("same-step terminal seek preserves active observation"),
                _State->Solver->SeekToStep(TerminalStep));
            _Test->TestEqual(TEXT("observation remains Ready before its horizon"),
                _State->Solver->GetStatus(), FCk_ProceduralActionSolverWorld::EStatus::Ready);
            _Test->TestTrue(TEXT("same-step observation resumes its finite horizon"),
                _State->Solver->Resume());
            _Test->TestEqual(TEXT("borrowed walking limb returns to the gait"),
                UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(
                    _State->Solver->GetSubject(), FName{TEXT("Leg0")}).Get_State(),
                ECk_ProceduralLimbControl_State::Default);
            const auto& Subject = _State->Solver->GetSubject();
            const auto Before = UCk_Utils_ProceduralAnimation_Debug_UE::Get_ControlSnapshot(
                Subject.ConvertToHandle());
            if (NOT _Test->TestTrue(TEXT("walking limb has a settled pose before public detach"),
                Before.Get_HasSettledPose()))
            { _State->Solver->Reset(); return true; }
            const auto& Bindings = Subject.Get<ck::FFragment_ProceduralArticulation_Bindings>();
            const auto Index = Bindings.Get_Bindings().IndexOfByPredicate([](const auto& InBinding)
                { return InBinding.Get_LimbId() == FName{TEXT("Leg0")}; });
            if (NOT _Test->TestTrue(TEXT("walking limb has a bound segment"),
                Bindings.Get_Bindings().IsValidIndex(Index)
                && NOT Bindings.Get_Bindings()[Index].Get_Segments().IsEmpty()))
            { _State->Solver->Reset(); return true; }
            auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(Subject, FName{TEXT("Leg0")});
            _State->DetachObserver.Reset(NewObject<UCk_Test_ProceduralAction_DetachObserver_UE>(
                GetTransientPackage()));
            _State->DetachObserver->Subject = Subject.ConvertToHandle();
            _State->DetachObserver->Leg = Leg.ConvertToHandle();
            _State->DetachObserver->Segment = Bindings.Get_Bindings()[Index].Get_Segments()[0].ConvertToHandle();
            auto Completion = FCk_Delegate_Request_OnCompleted{};
            Completion.BindDynamic(_State->DetachObserver.Get(),
                &UCk_Test_ProceduralAction_DetachObserver_UE::OnDetached);
            UCk_Utils_ProceduralLeg_UE::Request_Detach(Leg,
                FCk_Request_ProceduralLeg_Detach{ECk_ProceduralLeg_ReleasedPartsOwnership::KeepBodyOwned},
                Completion);
            _DetachRequested = true;
            return false;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FState> _State;
        bool _Scheduled = false;
        bool _DetachRequested = false;
    };

    struct FAppearanceScaleState
    {
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> Config;
        TUniquePtr<FCk_ProceduralActionSolverWorld> Solver;
        FCk_ProceduralArticulation_Pose FirstAcceptedPose;
        double DeadlineSeconds = 0.0;
        int32 Pass = 0;
    };

    auto
        QueueAppearanceProof(
            FCk_ProceduralActionSolverWorld& InSolver)
        -> bool
    {
        return InSolver.QueuePoseAtStep(8, MakeBatch())
            && InSolver.QueueEndAtStep(30)
            && InSolver.SeekToStep(60);
    }

    auto
        FirstAcceptedPose(
            const FCk_ProceduralActionRecordedStore& InRecording,
            FCk_ProceduralArticulation_Pose& OutPose)
        -> bool
    {
        const auto* Record = InRecording.Get_Records().FindByPredicate([](const auto& InRecord)
        {
            return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose
                && InRecord.Get_Receipt().Get_HasAcceptedPose();
        });
        if (Record == nullptr)
        { return false; }
        OutPose = Record->Get_Receipt().Get_CurrentPose();
        return true;
    }

    class FRunAppearanceScale final : public IAutomationLatentCommand
    {
    public:
        FRunAppearanceScale(FAutomationTestBase* InTest, TSharedRef<FAppearanceScaleState> InState)
            : _Test(InTest), _State(InState) {}

        auto Update() -> bool override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _Test->AddError(TEXT("appearance-only scale comparison did not finish within 15 seconds"));
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
            if (_State->Solver->GetStep() < 60)
            { return false; }
            auto Pose = FCk_ProceduralArticulation_Pose{};
            if (NOT _Test->TestTrue(TEXT("production preview produced an accepted limb pose"),
                FirstAcceptedPose(_State->Solver->GetRecording(), Pose)))
            { _State->Solver->Reset(); return true; }
            if (_State->Pass == 0)
            {
                _State->FirstAcceptedPose = Pose;
                _State->Solver->Reset();
                _State->Config->Set_BodyPreviewMeshScale(FVector{1.5, 0.5, 2.0});
                auto Bindings = _State->Config->Get_LimbBindings();
                Bindings[0].Set_PreviewMeshScale(FVector{0.8, 1.2, 1.7});
                _State->Config->Set_LimbBindings(Bindings);
                auto AppearanceError = FString{};
                if (NOT _Test->TestTrue(TEXT("nonuniform appearance scale validates"),
                    _State->Config->ValidateAppearance(AppearanceError)))
                { return true; }
                auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LimbId}};
                Control.Set_InputTimeout(FCk_Time{30.0});
                if (NOT _Test->TestTrue(TEXT("production preview rebuilds with only appearance scale changed"),
                    _State->Solver->StartManual(_State->Config.Get(), Control, 23)
                    && QueueAppearanceProof(*_State->Solver)))
                { _Test->AddError(_State->Solver->GetError()); _State->Solver->Reset(); return true; }
                _State->Pass = 1;
                return false;
            }
            const auto& Before = _State->FirstAcceptedPose.Get_Limbs();
            const auto& After = Pose.Get_Limbs();
            _Test->TestEqual(TEXT("appearance scale preserves accepted limb count"), After.Num(), Before.Num());
            if (Before.Num() == 1 && After.Num() == 1)
            {
                _Test->TestTrue(TEXT("appearance scale preserves accepted endpoint frame"),
                    After[0].Get_AcceptedEndpoint().Equals(Before[0].Get_AcceptedEndpoint(), 1.0e-3));
                _Test->TestEqual(TEXT("appearance scale preserves solved segment count"),
                    After[0].Get_Segments().Num(), Before[0].Get_Segments().Num());
                if (Before[0].Get_Segments().Num() == 1 && After[0].Get_Segments().Num() == 1)
                {
                    const auto FirstLength = FVector::Distance(Before[0].Get_Segments()[0].GetLocation(),
                        Before[0].Get_AcceptedEndpoint().GetLocation());
                    const auto SecondLength = FVector::Distance(After[0].Get_Segments()[0].GetLocation(),
                        After[0].Get_AcceptedEndpoint().GetLocation());
                    _Test->TestTrue(TEXT("appearance scale preserves measured solver segment length"),
                        FMath::IsNearlyEqual(FirstLength, SecondLength, 1.0e-3));
                }
            }
            _State->Solver->Reset();
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FAppearanceScaleState> _State;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SolverWorld_ManualReplay,
    "Ck.ProceduralAnimation.Action.Editor.SolverWorld.ManualReplay",
    ck_test_procedural_action_solver_world::TestFlags)

auto
    FCkTest_ProceduralAction_SolverWorld_ManualReplay::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_solver_world;
    auto State = MakeShared<FState>();
    State->Config = MakeConfig();
    State->Solver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LimbId}};
    Control.Set_InputTimeout(FCk_Time{30.0});
    if (NOT TestTrue(TEXT("private production manual Solver starts"),
        State->Solver->StartManual(State->Config.Get(), Control, 17)))
    {
        AddError(State->Solver->GetError());
        return false;
    }
    TestEqual(TEXT("private Solver uses marked GamePreview world"),
        State->Solver->GetWorld()->WorldType, EWorldType::GamePreview);
    TestTrue(TEXT("private Solver excludes gameplay AngelScript world subsystems"),
        State->Solver->GetWorld()->GetSubsystemArrayCopy<UScriptWorldSubsystem>().IsEmpty());
    for (auto Step = int64{8}; Step < 78; ++Step)
    {
        if (NOT TestTrue(TEXT("fixed input schedule accepts a pose"),
            State->Solver->QueuePoseAtStep(Step, MakeBatch())))
        { return false; }
    }
    if (NOT TestTrue(TEXT("fixed input schedule accepts an End"),
            State->Solver->QueueEndAtStep(90))
        || NOT TestTrue(TEXT("forward seek is scheduled without synchronous world ticks"),
            State->Solver->SeekToStep(120)))
    { return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 15.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunSolver(this, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SolverWorld_BodyOnly,
    "Ck.ProceduralAnimation.Action.Editor.SolverWorld.BodyOnly",
    ck_test_procedural_action_solver_world::TestFlags)

auto
    FCkTest_ProceduralAction_SolverWorld_BodyOnly::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_solver_world;
    auto Config = MakeConfig();
    Config->Set_Layout(FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}});
    Config->Set_LimbBindings(TArray<FCk_ProceduralActionPreview_LimbBinding>{});
    auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{}};
    Control.Set_BodyChannel(ECk_EnableDisable::Enable);
    auto Solver = FCk_ProceduralActionSolverWorld{};
    const auto Started = Solver.StartManual(Config.Get(), Control, 18);
    TestTrue(TEXT("production Solver accepts body-only articulation"), Started);
    if (NOT Started)
    { AddError(Solver.GetError()); }
    else
    {
        const auto* GameMode = Solver.GetWorld()->GetAuthGameMode();
        TestTrue(TEXT("private Solver uses isolated base GameMode rather than project defaults"),
            GameMode != nullptr && GameMode->GetClass() == AGameModeBase::StaticClass());
        TestEqual(TEXT("body-only Solver uses marked GamePreview world"),
            Solver.GetWorld()->WorldType, EWorldType::GamePreview);
        TestTrue(TEXT("body-only Solver excludes gameplay AngelScript world subsystems"),
            Solver.GetWorld()->GetSubsystemArrayCopy<UScriptWorldSubsystem>().IsEmpty());
    }
    Solver.Reset();
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SolverWorld_UnmarkedPreview,
    "Ck.ProceduralAnimation.Action.Editor.SolverWorld.UnmarkedPreview",
    ck_test_procedural_action_solver_world::TestFlags)

auto
    FCkTest_ProceduralAction_SolverWorld_UnmarkedPreview::
    RunTest(
        const FString&)
    -> bool
{
    auto Initialization = UWorld::InitializationValues{};
    auto* World = UWorld::CreateWorld(EWorldType::GamePreview, false,
        TEXT("CkUnmarkedGamePreview"), nullptr, true, ERHIFeatureLevel::Num,
        &Initialization, true);
    if (NOT TestNotNull(TEXT("unmarked GamePreview world created"), World))
    { return false; }
    World->InitWorld(Initialization);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::GamePreview);
    Context.SetCurrentWorld(World);
    TestFalse(TEXT("unmarked GamePreview has no production preview marker"),
        UCk_EcsWorld_Subsystem_UE::Is_ProductionPreviewWorld(*World));
    TestNull(TEXT("unmarked GamePreview has no production ECS"),
        World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>());
    TestNull(TEXT("unmarked GamePreview has no production Jolt"),
        World->GetSubsystem<UCk_Jolt_Subsystem>());
    World->RemoveFromRoot();
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SolverWorld_WalkingBorrowReturn,
    "Ck.ProceduralAnimation.Action.Editor.SolverWorld.WalkingBorrowReturn",
    ck_test_procedural_action_solver_world::TestFlags)

auto
    FCkTest_ProceduralAction_SolverWorld_WalkingBorrowReturn::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_solver_world;
    auto State = MakeShared<FState>();
    State->Config = MakeWalkingConfig();
    State->Config->Set_ObservationHorizonStep(120);
    State->Solver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{FName{TEXT("Leg0")}}};
    Control.Set_InputTimeout(FCk_Time{10.0});
    if (NOT TestTrue(TEXT("production walking preview composes"),
        State->Solver->StartManual(State->Config.Get(), Control, 19)))
    {
        AddError(State->Solver->GetError());
        return false;
    }
    TestEqual(TEXT("walking world exposes preparation instead of premature Ready"),
        State->Solver->GetStatus(), FCk_ProceduralActionSolverWorld::EStatus::Preparing);
    State->DeadlineSeconds = FPlatformTime::Seconds() + 20.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunWalkingSolver(this, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SolverWorld_WalkingBodyClip,
    "Ck.ProceduralAnimation.Action.Editor.SolverWorld.WalkingBodyClip",
    ck_test_procedural_action_solver_world::TestFlags)

auto
    FCkTest_ProceduralAction_SolverWorld_WalkingBodyClip::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_solver_world;
    auto Config = MakeWalkingConfig();
    Config->Set_PresentationFrame(Config->Get_RootFrame());
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
    auto Body = FCk_ProceduralAction_BodyChannel{};
    Body.Set_Enabled(true);
    Body.Set_InitialSource(ECk_ProceduralAction_InitialSource::IdentityOffset);
    Clip->Set_BodyChannel(Body);
    auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Lean")}, FCk_Time{0.5}};
    Phase.Set_BodyKeys(TArray<FCk_ProceduralAction_TransformKey>{
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector, FQuat::Identity},
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0}, FVector{10.0, 0.0, 0.0}, FQuat::Identity}});
    Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});

    auto State = MakeShared<FState>();
    State->Config = Config;
    State->Clip = Clip;
    State->Solver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    if (NOT TestTrue(TEXT("a walking preview composes a body presentation and starts a clip that drives the body"),
        State->Solver->StartClip(State->Config.Get(), State->Clip.Get(), {}, 20)))
    {
        AddError(State->Solver->GetError());
        return false;
    }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 20.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunWalkingBodyClip(this, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SolverWorld_AppearanceScale,
    "Ck.ProceduralAnimation.Action.Editor.SolverWorld.AppearanceScale",
    ck_test_procedural_action_solver_world::TestFlags)

auto
    FCkTest_ProceduralAction_SolverWorld_AppearanceScale::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_solver_world;
    auto State = MakeShared<FAppearanceScaleState>();
    State->Config = MakeConfig();
    State->Config->Set_BodyPreviewMeshScale(FVector{1.0, 0.0, 1.0});
    auto AppearanceError = FString{};
    TestFalse(TEXT("zero appearance scale is diagnosed"),
        State->Config->ValidateAppearance(AppearanceError));
    TestTrue(TEXT("invalid appearance scale explains the correction"),
        AppearanceError.Contains(TEXT("finite, nonzero")));
    State->Config->Set_BodyPreviewMeshScale(FVector::OneVector);
    State->Solver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LimbId}};
    Control.Set_InputTimeout(FCk_Time{30.0});
    if (NOT TestTrue(TEXT("production preview composes with unit appearance scale"),
        State->Solver->StartManual(State->Config.Get(), Control, 22)
        && QueueAppearanceProof(*State->Solver)))
    { AddError(State->Solver->GetError()); State->Solver->Reset(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 15.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunAppearanceScale(this, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SolverWorld_PreviewTuning,
    "Ck.ProceduralAnimation.Action.Editor.SolverWorld.PreviewTuning",
    ck_test_procedural_action_solver_world::TestFlags)

auto
    FCkTest_ProceduralAction_SolverWorld_PreviewTuning::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_solver_world;
    const auto ClipPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    auto State = MakeShared<FTuningState>();
    State->Clip.Reset(LoadObject<UCk_ProceduralAction_Data>(nullptr, ClipPath));
    if (NOT TestTrue(TEXT("saved body clip loads for tuning comparison"), State->Clip.IsValid()))
    { return false; }
    State->OriginalPhases = State->Clip->Get_Phases();
    State->OriginalDirty = State->Clip->GetOutermost()->IsDirty();
    State->DefaultConfig = MakeBodyConfig();
    State->TunedConfig = MakeBodyConfig();
    auto Tuning = State->TunedConfig->Get_ActionTuning();
    auto Playback = Tuning.Get_Playback();
    Playback.Set_PlayRate(0.5f);
    Tuning.Set_Playback(Playback);
    State->TunedConfig->Set_ActionTuning(Tuning);

    auto InvalidConfig = MakeBodyConfig();
    auto InvalidTuning = InvalidConfig->Get_ActionTuning();
    auto InvalidPlayback = InvalidTuning.Get_Playback();
    InvalidPlayback.Set_PlayRate(0.0f);
    InvalidTuning.Set_Playback(InvalidPlayback);
    InvalidConfig->Set_ActionTuning(InvalidTuning);
    auto InvalidSolver = FCk_ProceduralActionSolverWorld{};
    TestFalse(TEXT("invalid preview tuning is rejected before world construction"),
        InvalidSolver.StartClip(InvalidConfig.Get(), State->Clip.Get(), {}, 1));
    TestNull(TEXT("invalid preview tuning does not create a private world"), InvalidSolver.GetWorld());
    TestTrue(TEXT("invalid preview tuning names the offending field"),
        InvalidSolver.GetError().Contains(TEXT("Playback.PlayRate")));

    State->DefaultSolver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    State->TunedSolver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    const auto DefaultStarted = State->DefaultSolver->StartClip(
        State->DefaultConfig.Get(), State->Clip.Get(), {}, 2);
    const auto TunedStarted = State->TunedSolver->StartClip(
        State->TunedConfig.Get(), State->Clip.Get(), {}, 3);
    if (NOT TestTrue(TEXT("two production Solver worlds accept one clip with separate preview tunings"),
        DefaultStarted && TunedStarted))
    {
        AddError(State->DefaultSolver->GetError() + TEXT(" / ") + State->TunedSolver->GetError());
        State->DefaultSolver->Reset();
        State->TunedSolver->Reset();
        return false;
    }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 20.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunPreviewTuning(this, State));
    return true;
}

#endif
