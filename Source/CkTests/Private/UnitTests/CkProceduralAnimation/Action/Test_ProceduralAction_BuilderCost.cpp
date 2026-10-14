#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_ProceduralActionManagedListener.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionBuilder/CkProceduralActionBuilder_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Fragment.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionEvaluation.h"
#include "CkProceduralAnimation/Core/CkProceduralActionProgress.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/CkTests_Log.h"

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------

// Observed-cost rows: native wall-clock timers over fixed counts, numbers reported through AddInfo and the log under the
// [CkAction BUILDERCOST] tag. They assert only that each measured operation succeeded; there are no timing thresholds,
// because the numbers depend on the machine, the build configuration and the load from other test editors.
namespace ck_test_procedural_action_builder_cost
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto Repetitions = 3;
    constexpr auto BuildIterations = 20;
    constexpr auto PoseKeyCount = 64;
    constexpr auto CompileIterations = 200;
    constexpr auto EvaluateSamples = 20000;
    constexpr auto ExpandIterations = 200;
    constexpr auto MeasuredFrames = 300;
    constexpr auto WaitSeconds = 30.0;
    constexpr auto ManagedClipCount = 4;
    constexpr auto ClipDurationSeconds = 60.0;
    const auto ArmSlot = FName{TEXT("Arm")};
    const auto ManagedSlot = FName{TEXT("PerfArm")};

    struct FSpread
    {
        double SumMs = 0.0;
        double MinMs = TNumericLimits<double>::Max();
        double MaxMs = 0.0;
        int32 Count = 0;

        auto Add(double InMs) -> void
        {
            SumMs += InMs;
            MinMs = FMath::Min(MinMs, InMs);
            MaxMs = FMath::Max(MaxMs, InMs);
            ++Count;
        }

        auto Get_AvgMs() const -> double
        { return Count > 0 ? SumMs / Count : 0.0; }

        auto ToString() const -> FString
        { return FString::Printf(TEXT("avgMs=%.6f minMs=%.6f maxMs=%.6f n=%d"), Get_AvgMs(), Count > 0 ? MinMs : 0.0, MaxMs, Count); }
    };

    auto
    ReportCost(
        FAutomationTestBase& InTest,
        const FString& InLine)
        -> void
    {
        const auto Line = FString::Printf(TEXT("[CkAction BUILDERCOST] %s"), *InLine);
        InTest.AddInfo(Line);
        ck::tests::Log(TEXT("{}"), Line);
    }

    auto
    IsReportValid(
        const FCk_ProceduralAction_ValidationReport& InReport)
        -> bool
    { return InReport.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid; }

    // Reach recipe (Prepare/Execute/Recover, one body-local slot) plus PoseKeyCount interior poses spread over the phases.
    auto
    MakePosedDraft(
        FCk_ProceduralAction_ClipDraft& OutDraft,
        double* OutRecipeMs,
        double* OutPoseKeysMs)
        -> bool
    {
        const auto RecipeStart = FPlatformTime::Seconds();
        auto Recipe = FCk_ProceduralAction_RecipeSpec{ECk_ProceduralAction_Recipe::Reach, TArray<FName>{ArmSlot}};
        const auto RecipeValid = IsReportValid(UCk_Utils_ProceduralActionBuilder_UE::Make_FromRecipe(Recipe, OutDraft));
        const auto RecipeEnd = FPlatformTime::Seconds();
        if (NOT RecipeValid)
        { return false; }

        const auto PhaseIds = TArray<FName>{FName{TEXT("Prepare")}, FName{TEXT("Execute")}, FName{TEXT("Recover")}};
        auto AllValid = true;
        const auto KeysStart = FPlatformTime::Seconds();
        for (auto PhaseIndex = 0; PhaseIndex < PhaseIds.Num(); ++PhaseIndex)
        {
            const auto KeysInPhase = PoseKeyCount / PhaseIds.Num() + (PhaseIndex < PoseKeyCount % PhaseIds.Num() ? 1 : 0);
            for (auto KeyIndex = 0; KeyIndex < KeysInPhase; ++KeyIndex)
            {
                const auto Time = static_cast<double>(KeyIndex + 1) / static_cast<double>(KeysInPhase + 1);
                auto Pose = FCk_ProceduralAction_PoseKey{FCk_FloatRange_0to1{Time}};
                Pose.Set_Limbs(TArray<FCk_ProceduralAction_LimbPoseTarget>{FCk_ProceduralAction_LimbPoseTarget{ArmSlot,
                    FVector{20.0 * Time, 5.0 * FMath::Sin(Time * UE_TWO_PI), 2.0 * PhaseIndex}}});
                AllValid &= IsReportValid(UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(OutDraft, PhaseIds[PhaseIndex], Pose));
            }
        }
        const auto KeysEnd = FPlatformTime::Seconds();
        if (OutRecipeMs != nullptr)
        { *OutRecipeMs = (RecipeEnd - RecipeStart) * 1000.0; }
        if (OutPoseKeysMs != nullptr)
        { *OutPoseKeysMs = (KeysEnd - KeysStart) * 1000.0; }
        return AllValid;
    }

    auto
    MakeCustomProgressDraft(
        const FCk_ProceduralAction_ClipDraft& InDraft)
        -> FCk_ProceduralAction_ClipDraft
    {
        auto Curve = FCk_ProceduralAction_ProgressCurve{};
        Curve.Set_Mode(ECk_ProceduralAction_ProgressMode::CustomCurve);
        auto Keys = TArray<FCk_ProceduralAction_ProgressKey>{};
        for (const auto& Pair : TArray<TPair<double, double>>{{0.0, 0.0}, {0.25, 0.1}, {0.5, 0.5}, {0.75, 0.9}, {1.0, 1.0}})
        { Keys.Emplace(FCk_FloatRange_0to1{Pair.Key}, FCk_FloatRange_0to1{Pair.Value}); }
        Curve.Set_Keys(Keys);
        auto Draft = InDraft;
        auto Phases = Draft.Get_Phases();
        for (auto& Phase : Phases)
        {
            Phase.Set_Easing(ECk_ProceduralAction_Easing::Linear);
            Phase.Set_Progress(Curve);
        }
        Draft.Set_Phases(Phases);
        return Draft;
    }

    auto
    MakeIdentityInitial(
        const ck::procedural_action::FCompiledClip& InClip)
        -> ck::procedural_action::FInitialPose
    {
        auto Initial = ck::procedural_action::FInitialPose{};
        for (const auto& Slot : InClip.Get_Slots())
        { Initial.Get_Limbs().Add(Slot.Get_Id(), FTransform::Identity); }
        return Initial;
    }

    auto
    MakeLengths() -> TArray<float>
    { return TArray<float>{80.0f, 80.0f}; }

    auto
    AddLimbSubject(
        UWorld* InWorld,
        const FVector& InLocation,
        const TArray<FName>& InLimbs,
        FCk_Handle_Transform& OutRoot)
        -> FCk_Handle_ProceduralArticulation
    {
        auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        OutRoot = UCk_Utils_Transform_UE::Add(RootEntity, FTransform{InLocation}, ECk_Replication::DoesNotReplicate);
        auto Layouts = TArray<FCk_ProceduralLimb_Layout>{};
        auto Bindings = TArray<FCk_ProceduralLimb_Binding>{};
        for (auto Index = 0; Index < InLimbs.Num(); ++Index)
        {
            Layouts.Emplace(InLimbs[Index], FTransform{FVector{0.0, 80.0 * Index, 0.0}}, MakeLengths(),
                FTransform{FVector{160.0, 0.0, 0.0}}, FVector::UpVector);
            auto Parts = TArray<FCk_Handle_Transform>{};
            for (auto Part = 0; Part < 2; ++Part)
            {
                auto PartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(OutRoot);
                Parts.Add(UCk_Utils_Transform_UE::Add(PartEntity, FTransform{InLocation}, ECk_Replication::DoesNotReplicate));
            }
            Bindings.Emplace(InLimbs[Index], Parts);
        }
        return UCk_Utils_ProceduralArticulation_UE::Add(OutRoot,
            FCk_ProceduralArticulation_Spec{FCk_ProceduralArticulation_Layout{Layouts}, Bindings});
    }

    // One 60 s rest-normalized phase with a small nod near its start: long enough to stay Playing through the measured frames.
    auto
    MakeLongClip() -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
        auto Track = FCk_ProceduralAction_LimbTrack{ManagedSlot};
        Track.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
        auto Keys = TArray<FCk_ProceduralAction_VectorKey>{};
        for (const auto& Key : TArray<TPair<double, double>>{{0.0, 0.0}, {0.02, 0.05}, {0.04, -0.05}, {1.0, 0.0}})
        {
            const auto Radians = FMath::DegreesToRadians(Key.Value);
            Keys.Emplace(FCk_FloatRange_0to1{Key.Key}, FVector{FMath::Cos(Radians) - 1.0, FMath::Sin(Radians), 0.0});
        }
        Track.Set_PositionKeys(Keys);
        auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Hold")}, FCk_Time{ClipDurationSeconds}};
        Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
        Clip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{FCk_ProceduralAction_Slot{ManagedSlot}});
        Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        return Clip;
    }

    struct FStoreSample
    {
        int64 RecordSlots = 0;
        int64 PoseTransforms = 0;
        SIZE_T AllocatedBytes = 0;
        int32 RetainedCount = 0;
    };

    auto
    SampleStore(
        const FCk_Handle_ProceduralActionPlayer& InPlayer)
        -> FStoreSample
    {
        auto Sample = FStoreSample{};
        if (ck::Is_NOT_Valid(InPlayer))
        { return Sample; }
        const auto& Store = InPlayer.Get<ck::FFragment_ProceduralActionPlayer>().Get_Receipts();
        Sample.RecordSlots = Store.Get_RetainedRecordSlots();
        Sample.PoseTransforms = Store.Get_RetainedPoseTransforms();
        Sample.AllocatedBytes = Store.Get_AllocatedSize();
        Sample.RetainedCount = Store.Get_RetainedCount();
        return Sample;
    }

    auto
    DescribeStore(
        const FStoreSample& InSample)
        -> FString
    {
        return FString::Printf(TEXT("retainedStreams=%d recordSlots=%lld poseTransforms=%lld allocatedBytes=%llu"),
            InSample.RetainedCount, static_cast<long long>(InSample.RecordSlots),
            static_cast<long long>(InSample.PoseTransforms), static_cast<unsigned long long>(InSample.AllocatedBytes));
    }

    struct FManagedCase
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TArray<TStrongObjectPtr<UCk_Test_ProceduralActionManagedListener_UE>> Listeners;
        TArray<FCk_ProceduralAction_ManagedStart> Starts;
        FStoreSample BeforePlay;
        FStoreSample AfterRelease;
        int64 MaxRecordSlots = 0;
        int64 MaxPoseTransforms = 0;
        SIZE_T MaxAllocatedBytes = 0;
        FSpread IdleFrames;
        FSpread ManagedFrames;
        int32 RecordsAtMeasureStart = 0;
        int32 RecordsAtMeasureEnd = 0;
        int32 CallsAtMeasureStart = 0;
        int32 CallsAtMeasureEnd = 0;
        bool AllPlayingThroughout = true;
        int32 PollsAfterCompletion = 0;
    };

    auto
    Get_TotalRecords(
        const FManagedCase& InCase,
        int32& OutCalls)
        -> int32
    {
        auto Records = 0;
        OutCalls = 0;
        for (const auto& Listener : InCase.Listeners)
        {
            if (NOT Listener.IsValid())
            { continue; }
            Records += Listener->Records.Num();
            OutCalls += Listener->RecordsCalls;
        }
        return Records;
    }

    class FMeasureFrames final : public IAutomationLatentCommand
    {
    private:
        TSharedRef<FManagedCase> _Case;
        bool _Managed = false;
        int32 _Frame = 0;
        double _PreviousSeconds = 0.0;

    public:
        FMeasureFrames(
            const TSharedRef<FManagedCase>& InCase,
            bool InManaged)
            : _Case(InCase)
            , _Managed(InManaged) {}

        virtual auto Update() -> bool override
        {
            const auto Now = FPlatformTime::Seconds();
            auto& Frames = _Managed ? _Case->ManagedFrames : _Case->IdleFrames;
            if (_Frame == 0 && _Managed)
            { _Case->RecordsAtMeasureStart = Get_TotalRecords(*_Case, _Case->CallsAtMeasureStart); }
            if (_PreviousSeconds > 0.0)
            { Frames.Add((Now - _PreviousSeconds) * 1000.0); }
            _PreviousSeconds = Now;
            if (_Managed)
            {
                const auto Sample = SampleStore(_Case->Player);
                _Case->MaxRecordSlots = FMath::Max(_Case->MaxRecordSlots, Sample.RecordSlots);
                _Case->MaxPoseTransforms = FMath::Max(_Case->MaxPoseTransforms, Sample.PoseTransforms);
                _Case->MaxAllocatedBytes = FMath::Max(_Case->MaxAllocatedBytes, Sample.AllocatedBytes);
                for (const auto& Start : _Case->Starts)
                {
                    _Case->AllPlayingThroughout &= ck::IsValid(Start.Get_Action())
                        && UCk_Utils_ProceduralAction_UE::Get_State(Start.Get_Action()) == ECk_ProceduralAction_State::Playing;
                }
            }
            ++_Frame;
            if (_Frame > MeasuredFrames && _Managed)
            { _Case->RecordsAtMeasureEnd = Get_TotalRecords(*_Case, _Case->CallsAtMeasureEnd); }
            return _Frame > MeasuredFrames;
        }
    };
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderCostRecipePoseKeysBuildIntoTest,
    "Ck.ProceduralAnimation.Action.Performance.Builder.RecipePoseKeysBuildInto",
    ck_test_procedural_action_builder_cost::TestFlags)

auto
FCkProceduralActionBuilderCostRecipePoseKeysBuildIntoTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder_cost;
    auto Destination = TStrongObjectPtr<UCk_ProceduralAction_Data>{NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
    for (auto Repetition = 1; Repetition <= Repetitions; ++Repetition)
    {
        auto RecipeSpread = FSpread{};
        auto PoseKeySpread = FSpread{};
        auto BuildSpread = FSpread{};
        auto AllValid = true;
        for (auto Iteration = 0; Iteration < BuildIterations; ++Iteration)
        {
            auto Draft = FCk_ProceduralAction_ClipDraft{};
            auto RecipeMs = 0.0;
            auto PoseKeysMs = 0.0;
            AllValid &= MakePosedDraft(Draft, &RecipeMs, &PoseKeysMs);
            const auto BuildStart = FPlatformTime::Seconds();
            AllValid &= IsReportValid(UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Draft, Destination.Get()));
            BuildSpread.Add((FPlatformTime::Seconds() - BuildStart) * 1000.0);
            RecipeSpread.Add(RecipeMs);
            PoseKeySpread.Add(PoseKeysMs);
        }
        TestTrue(TEXT("Every recipe, pose key and Build_Into succeeded"), AllValid);
        auto Keys = 0;
        for (const auto& Phase : Destination->Get_Phases())
        {
            for (const auto& Track : Phase.Get_LimbTracks())
            { Keys += Track.Get_PositionKeys().Num(); }
        }
        TestEqual(TEXT("The built clip holds the recipe's 6 keys plus the posed keys"), Keys, 6 + PoseKeyCount);
        ReportCost(*this, FString::Printf(TEXT("row=RecipePoseKeysBuildInto repetition=%d iterations=%d poseKeys=%d builtPositionKeys=%d"),
            Repetition, BuildIterations, PoseKeyCount, Keys));
        ReportCost(*this, FString::Printf(TEXT("row=RecipePoseKeysBuildInto repetition=%d stage=Make_FromRecipe %s"), Repetition, *RecipeSpread.ToString()));
        ReportCost(*this, FString::Printf(TEXT("row=RecipePoseKeysBuildInto repetition=%d stage=Add_PoseKey_x%d %s perKeyAvgUs=%.3f"),
            Repetition, PoseKeyCount, *PoseKeySpread.ToString(), PoseKeySpread.Get_AvgMs() * 1000.0 / PoseKeyCount));
        ReportCost(*this, FString::Printf(TEXT("row=RecipePoseKeysBuildInto repetition=%d stage=Build_Into %s"), Repetition, *BuildSpread.ToString()));
    }
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderCostCompileEvaluateTest,
    "Ck.ProceduralAnimation.Action.Performance.Builder.CompileAndEvaluatePresetVsCustomCurve",
    ck_test_procedural_action_builder_cost::TestFlags)

auto
FCkProceduralActionBuilderCostCompileEvaluateTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder_cost;
    auto PresetDraft = FCk_ProceduralAction_ClipDraft{};
    if (NOT TestTrue(TEXT("The posed draft builds"), MakePosedDraft(PresetDraft, nullptr, nullptr)))
    { return true; }
    const auto CustomDraft = MakeCustomProgressDraft(PresetDraft);

    for (const auto Custom : {false, true})
    {
        const auto& Draft = Custom ? CustomDraft : PresetDraft;
        const auto* Mode = Custom ? TEXT("CustomCurve") : TEXT("Preset");
        for (auto Repetition = 1; Repetition <= Repetitions; ++Repetition)
        {
            auto Clip = ck::procedural_action::FCompiledClip{};
            auto CompileValid = true;
            const auto CompileStart = FPlatformTime::Seconds();
            for (auto Iteration = 0; Iteration < CompileIterations; ++Iteration)
            {
                auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
                CompileValid &= ck::procedural_action::Compile(ck::procedural_action::MakeClipSource(Draft), Clip, Diagnostics);
            }
            const auto CompileMs = (FPlatformTime::Seconds() - CompileStart) * 1000.0;
            if (NOT TestTrue(FString::Printf(TEXT("%s draft compiles"), Mode), CompileValid))
            { return true; }

            const auto Initial = MakeIdentityInitial(Clip);
            const auto Duration = Clip.Get_Duration().Get_Seconds();
            auto Checksum = 0.0;
            auto EvaluateValid = true;
            const auto EvaluateStart = FPlatformTime::Seconds();
            for (auto Sample = 0; Sample < EvaluateSamples; ++Sample)
            {
                auto Pose = ck::procedural_action::FEvaluatedPose{};
                const auto Time = Duration * (static_cast<double>(Sample) + 0.5) / EvaluateSamples;
                EvaluateValid &= ck::procedural_action::Evaluate(Clip, Initial, FCk_Time{Time}, Pose);
                if (NOT Pose.Get_Limbs().IsEmpty())
                { Checksum += Pose.Get_Limbs()[0].Get_Position().X; }
            }
            const auto EvaluateMs = (FPlatformTime::Seconds() - EvaluateStart) * 1000.0;

            auto ProgressChecksum = 0.0;
            const auto ProgressStart = FPlatformTime::Seconds();
            for (auto Sample = 0; Sample < EvaluateSamples; ++Sample)
            {
                const auto& Phase = Clip.Get_Phases()[Sample % Clip.Get_Phases().Num()];
                ProgressChecksum += ck::procedural_action::EvaluateProgress(Phase, (static_cast<double>(Sample) + 0.5) / EvaluateSamples);
            }
            const auto ProgressMs = (FPlatformTime::Seconds() - ProgressStart) * 1000.0;

            TestTrue(FString::Printf(TEXT("%s samples evaluate"), Mode), EvaluateValid && FMath::IsFinite(Checksum)
                && FMath::IsFinite(ProgressChecksum));
            ReportCost(*this, FString::Printf(TEXT("row=CompileAndEvaluate mode=%s repetition=%d phases=%d compileIterations=%d compileTotalMs=%.6f compilePerCallUs=%.3f evaluateSamples=%d evaluateTotalMs=%.6f evaluatePerSampleNs=%.1f evaluateProgressPerSampleNs=%.1f checksum=%.6f"),
                Mode, Repetition, Clip.Get_Phases().Num(), CompileIterations, CompileMs, CompileMs * 1000.0 / CompileIterations,
                EvaluateSamples, EvaluateMs, EvaluateMs * 1.0e6 / EvaluateSamples, ProgressMs * 1.0e6 / EvaluateSamples,
                Checksum + ProgressChecksum));
        }
    }
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderCostExpandMirrorsTest,
    "Ck.ProceduralAnimation.Action.Performance.Builder.ExpandMirrorsOneAndEight",
    ck_test_procedural_action_builder_cost::TestFlags)

auto
FCkProceduralActionBuilderCostExpandMirrorsTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder_cost;
    struct FState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        TArray<FName> Limbs;
    };
    const auto State = MakeShared<FState>();
    for (auto Index = 0; Index < 9; ++Index)
    { State->Limbs.Add(FName{*FString::Printf(TEXT("MirrorLimb%d"), Index)}); }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Subject = AddLimbSubject(InWorld, FVector{0.0, -5000.0, 2000.0}, State->Limbs, State->Root);
            TestTrue(TEXT("Nine-limb subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Nine-limb subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Draft = FCk_ProceduralAction_ClipDraft{};
            if (NOT TestTrue(TEXT("The posed draft builds"), MakePosedDraft(Draft, nullptr, nullptr)))
            { return; }
            auto Source = ck::procedural_action::FCompiledClip{};
            auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
            if (NOT TestTrue(TEXT("The posed draft compiles"),
                ck::procedural_action::Compile(ck::procedural_action::MakeClipSource(Draft), Source, Diagnostics)))
            { return; }

            for (const auto MirrorCount : {1, 8})
            {
                auto Mirrors = TArray<FCk_ProceduralAction_MirrorSpec>{};
                auto Bindings = TArray<FCk_ProceduralAction_SlotBinding>{
                    FCk_ProceduralAction_SlotBinding{ArmSlot, State->Limbs[0]}};
                for (auto Index = 0; Index < MirrorCount; ++Index)
                {
                    const auto Destination = FName{*FString::Printf(TEXT("ArmMirror%d"), Index)};
                    Mirrors.Emplace(ArmSlot, Destination, ECk_ProceduralAction_MirrorPlane::XZ);
                    Bindings.Emplace(Destination, State->Limbs[Index + 1]);
                }
                for (auto Repetition = 1; Repetition <= Repetitions; ++Repetition)
                {
                    auto Spread = FSpread{};
                    auto AllValid = true;
                    auto Expanded = ck::procedural_action::FCompiledClip{};
                    for (auto Iteration = 0; Iteration < ExpandIterations; ++Iteration)
                    {
                        auto ExpandDiagnostics = TArray<ck::procedural_action::FDiagnostic>{};
                        const auto Start = FPlatformTime::Seconds();
                        AllValid &= ck::procedural_action::ExpandMirrors(Source, Mirrors, Bindings, State->Subject,
                            Expanded, ExpandDiagnostics);
                        Spread.Add((FPlatformTime::Seconds() - Start) * 1000.0);
                    }
                    TestTrue(FString::Printf(TEXT("%d mirror(s) expand"), MirrorCount), AllValid);
                    TestEqual(FString::Printf(TEXT("%d mirror(s) add their destination slots"), MirrorCount),
                        Expanded.Get_Slots().Num(), Source.Get_Slots().Num() + MirrorCount);
                    auto Keys = 0;
                    for (const auto& Phase : Expanded.Get_Phases())
                    {
                        for (const auto& Track : Phase.Get_LimbTracks())
                        { Keys += Track.Get_PositionKeys().Num(); }
                    }
                    ReportCost(*this, FString::Printf(TEXT("row=ExpandMirrors mirrors=%d repetition=%d sourcePositionKeys=%d expandedPositionKeys=%d %s perCallUs=%.3f"),
                        MirrorCount, Repetition, 6 + PoseKeyCount, Keys, *Spread.ToString(), Spread.Get_AvgMs() * 1000.0));
                }
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            if (ck::IsValid(State->Root))
            {
                auto Root = State->Root.ConvertToHandle();
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderCostManagedDispatchTest,
    "Ck.ProceduralAnimation.Action.Performance.Builder.ManagedDispatchSteadyState",
    ck_test_procedural_action_builder_cost::TestFlags)

auto
FCkProceduralActionBuilderCostManagedDispatchTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder_cost;
    const auto Case = MakeShared<FManagedCase>();
    auto Limbs = TArray<FName>{};
    for (auto Index = 0; Index < ManagedClipCount; ++Index)
    { Limbs.Add(FName{*FString::Printf(TEXT("ManagedLimb%d"), Index)}); }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Case, Limbs](UWorld* InWorld)
        {
            Case->Subject = AddLimbSubject(InWorld, FVector{0.0, 5000.0, 2000.0}, Limbs, Case->Root);
            TestTrue(TEXT("Four-limb subject composes"), ck::IsValid(Case->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Case]
        {
            return ck::IsValid(Case->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(Case->Subject) > 0;
        }), WaitSeconds, TEXT("Four-limb subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Case](UWorld*)
        {
            Case->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(Case->Subject, FCk_ProceduralActionPlayer_Spec{});
            TestTrue(TEXT("Player composes"), ck::IsValid(Case->Player));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(8));
    ADD_LATENT_AUTOMATION_COMMAND(FMeasureFrames(Case, false));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Case, Limbs](UWorld*)
        {
            if (ck::Is_NOT_Valid(Case->Player))
            { return; }
            Case->BeforePlay = SampleStore(Case->Player);
            Case->Clip = MakeLongClip();
            for (const auto Limb : Limbs)
            {
                auto Listener = TStrongObjectPtr<UCk_Test_ProceduralActionManagedListener_UE>{
                    NewObject<UCk_Test_ProceduralActionManagedListener_UE>()};
                auto Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Case->Root);
                auto Spec = FCk_ProceduralAction_LoadedPlaySpec{Case->Clip.Get()};
                Spec.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
                    FCk_ProceduralAction_SlotBinding{ManagedSlot, Limb}});
                const auto Start = UCk_Utils_ProceduralAction_UE::Play_Loaded(Scope, Case->Player, Spec,
                    Listener->MakeCompletionDelegate());
                TestTrue(TEXT("Managed clip starts"), ck::IsValid(Start.Get_Action()));
                UCk_Utils_ProceduralActionPlayer_UE::BindTo_OnManagedRecords(Case->Player, Start.Get_Identity(),
                    Listener->MakeRecordsDelegate());
                Listener->Action = Start.Get_Action();
                Case->Listeners.Add(Listener);
                Case->Starts.Add(Start);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Case]
        {
            if (Case->Starts.Num() != ManagedClipCount)
            { return false; }
            for (auto Index = 0; Index < Case->Starts.Num(); ++Index)
            {
                const auto& Action = Case->Starts[Index].Get_Action();
                if (ck::Is_NOT_Valid(Action)
                    || UCk_Utils_ProceduralAction_UE::Get_State(Action) != ECk_ProceduralAction_State::Playing
                    || Case->Listeners[Index]->RecordsCalls == 0)
                { return false; }
            }
            return true;
        }), WaitSeconds, TEXT("Four managed clips play and deliver records")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(8));
    ADD_LATENT_AUTOMATION_COMMAND(FMeasureFrames(Case, true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [Case](UWorld*)
        {
            for (const auto& Start : Case->Starts)
            {
                auto Action = Start.Get_Action();
                if (ck::IsValid(Action))
                {
                    UCk_Utils_ProceduralAction_UE::Request_Cancel(Action,
                        FCk_Request_ProceduralAction_Cancel{FName{TEXT("MeasurementDone")}}, {});
                }
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Case]
        {
            for (const auto& Listener : Case->Listeners)
            {
                if (NOT Listener.IsValid() || Listener->CompletionCount == 0)
                { return false; }
            }
            return Case->Listeners.Num() == ManagedClipCount;
        }), WaitSeconds, TEXT("Every managed clip completes after cancellation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Case]
        {
            ++Case->PollsAfterCompletion;
            return SampleStore(Case->Player).RetainedCount == Case->BeforePlay.RetainedCount;
        }), WaitSeconds, TEXT("Released managed streams leave the store")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Case](UWorld*)
        {
            Case->AfterRelease = SampleStore(Case->Player);
            const auto MeasuredRecords = Case->RecordsAtMeasureEnd - Case->RecordsAtMeasureStart;
            const auto MeasuredCalls = Case->CallsAtMeasureEnd - Case->CallsAtMeasureStart;
            TestTrue(TEXT("All four clips stayed Playing through the measured frames"), Case->AllPlayingThroughout);
            TestTrue(TEXT("Managed records were delivered during the measured frames"), MeasuredRecords > 0);
            TestEqual(TEXT("Idle frame intervals are complete"), Case->IdleFrames.Count, MeasuredFrames);
            TestEqual(TEXT("Managed frame intervals are complete"), Case->ManagedFrames.Count, MeasuredFrames);
            ReportCost(*this, FString::Printf(TEXT("row=ManagedDispatch clips=%d frames=%d idleFrameInterval %s"),
                ManagedClipCount, MeasuredFrames, *Case->IdleFrames.ToString()));
            ReportCost(*this, FString::Printf(TEXT("row=ManagedDispatch clips=%d frames=%d managedFrameInterval %s"),
                ManagedClipCount, MeasuredFrames, *Case->ManagedFrames.ToString()));
            ReportCost(*this, FString::Printf(TEXT("row=ManagedDispatch measuredRecords=%d measuredRecordsCallbacks=%d recordsPerFrame=%.3f"),
                MeasuredRecords, MeasuredCalls, static_cast<double>(MeasuredRecords) / MeasuredFrames));
            ReportCost(*this, FString::Printf(TEXT("row=ManagedDispatch store=beforePlay %s"), *DescribeStore(Case->BeforePlay)));
            ReportCost(*this, FString::Printf(TEXT("row=ManagedDispatch store=maxDuringMeasured recordSlots=%lld poseTransforms=%lld allocatedBytes=%llu"),
                static_cast<long long>(Case->MaxRecordSlots), static_cast<long long>(Case->MaxPoseTransforms),
                static_cast<unsigned long long>(Case->MaxAllocatedBytes)));
            ReportCost(*this, FString::Printf(TEXT("row=ManagedDispatch store=afterRelease %s pollsAfterCompletion=%d allocationCount=unavailable"),
                *DescribeStore(Case->AfterRelease), Case->PollsAfterCompletion));
            if (ck::IsValid(Case->Root))
            {
                auto Root = Case->Root.ConvertToHandle();
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
