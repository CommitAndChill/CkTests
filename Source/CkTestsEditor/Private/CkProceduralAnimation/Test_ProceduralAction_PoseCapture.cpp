#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/ActionBuilder/CkProceduralActionBuilder_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringCommands.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringSession.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorData.h"

#include "Editor.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_procedural_action_pose_capture
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const FName Motion{TEXT("Motion")};
    const FName Arm{TEXT("Arm")};
    const FName Leg{TEXT("Leg")};
    const FName MarkerId{TEXT("Mid")};

    auto
        BodyChannel()
        -> FName
    { return ck::procedural_action_editor::Get_BodyChannelName(); }

    auto
        MakeTransientClip()
        -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        return TStrongObjectPtr<UCk_ProceduralAction_Data>{
            NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    }

    auto
        VectorKey(
            double InTime,
            const FVector& InValue)
        -> FCk_ProceduralAction_VectorKey
    { return FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{InTime}, InValue}; }

    // A compiling clip with body keys, two limb tracks and a marker, built without pose groups (an "old" clip).
    auto
        FillClip(
            UCk_ProceduralAction_Data& InClip)
        -> bool
    {
        auto Draft = FCk_ProceduralAction_ClipDraft{};
        Draft.Set_BodyChannel(FCk_ProceduralAction_BodyChannel{}.Set_Enabled(true));
        Draft.Set_Slots(TArray<FCk_ProceduralAction_Slot>{FCk_ProceduralAction_Slot{Arm}, FCk_ProceduralAction_Slot{Leg}});
        auto Phase = FCk_ProceduralAction_Phase{Motion, FCk_Time{1.0}};
        Phase.Set_BodyKeys(TArray<FCk_ProceduralAction_TransformKey>{
            FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector, FQuat::Identity},
            FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0}, FVector::ZeroVector, FQuat::Identity}});
        auto ArmTrack = FCk_ProceduralAction_LimbTrack{Arm};
        ArmTrack.Set_Interpolation(ECk_ProceduralAction_Interpolation::Smooth);
        ArmTrack.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{
            VectorKey(0.0, FVector{10.0, 0.0, 0.0}), VectorKey(1.0, FVector{30.0, 0.0, 0.0})});
        auto LegTrack = FCk_ProceduralAction_LimbTrack{Leg};
        LegTrack.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{
            VectorKey(0.0, FVector{0.0, 10.0, 0.0}), VectorKey(1.0, FVector{0.0, 20.0, 0.0})});
        Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{ArmTrack, LegTrack});
        Draft.Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        Draft.Set_Markers(TArray<FCk_ProceduralAction_Marker>{FCk_ProceduralAction_Marker{MarkerId,
            FCk_ProceduralAction_TimeRef{Motion, FCk_FloatRange_0to1{0.5}}}});
        return UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Draft, &InClip).Get_Status()
            == ECk_ProceduralAction_ReportStatus::Valid;
    }

    auto
        MakePose(
            double InTime,
            const FVector& InBody,
            const FVector& InArm)
        -> FCk_ProceduralAction_PoseKey
    {
        auto Pose = FCk_ProceduralAction_PoseKey{FCk_FloatRange_0to1{InTime}};
        Pose.Set_BodyMode(ECk_ProceduralAction_ValueMode::Set);
        Pose.Set_Body(FTransform{InBody});
        Pose.Set_Limbs(TArray<FCk_ProceduralAction_LimbPoseTarget>{FCk_ProceduralAction_LimbPoseTarget{Arm, InArm}});
        return Pose;
    }

    auto
        FindTrack(
            const UCk_ProceduralAction_Data& InClip,
            FName InSlot)
        -> const FCk_ProceduralAction_LimbTrack*
    {
        return InClip.Get_Phases().IsEmpty() ? nullptr
            : InClip.Get_Phases()[0].Get_LimbTracks().FindByPredicate([InSlot](const auto& InTrack)
                { return InTrack.Get_Slot() == InSlot; });
    }

    auto
        PositionAt(
            const UCk_ProceduralAction_Data& InClip,
            FName InSlot,
            double InTime,
            FVector& OutValue)
        -> bool
    {
        const auto* Track = FindTrack(InClip, InSlot);
        const auto* Key = Track == nullptr ? nullptr : Track->Get_PositionKeys().FindByPredicate([InTime](const auto& InKey)
            { return InKey.Get_Time().Get_Value() == InTime; });
        if (Key != nullptr)
        { OutValue = Key->Get_Value(); }
        return Key != nullptr;
    }

    auto
        HasBodyKeyAt(
            const UCk_ProceduralAction_Data& InClip,
            double InTime)
        -> bool
    {
        return NOT InClip.Get_Phases().IsEmpty() && InClip.Get_Phases()[0].Get_BodyKeys().ContainsByPredicate(
            [InTime](const auto& InKey) { return InKey.Get_Time().Get_Value() == InTime; });
    }

    auto
        LabelAt(
            const UCk_ProceduralAction_Data& InClip,
            FName InPhaseId,
            double InTime)
        -> TOptional<FName>
    {
        const auto Labels = ck::procedural_action_editor::Get_PoseLabels(InClip);
        const auto* Label = ck::procedural_action_editor::FindPoseLabel(Labels, InPhaseId, InTime);
        return Label != nullptr ? TOptional<FName>{Label->Get_Label()} : TOptional<FName>{};
    }

    auto
        SamePhases(
            const UCk_ProceduralAction_Data& InA,
            const TArray<FCk_ProceduralAction_Phase>& InB)
        -> bool
    {
        if (InA.Get_Phases().Num() != InB.Num())
        { return false; }
        for (auto Index = 0; Index < InB.Num(); ++Index)
        {
            if (NOT FCk_ProceduralAction_Phase::StaticStruct()->CompareScriptStruct(
                &InA.Get_Phases()[Index], &InB[Index], PPF_None))
            { return false; }
        }
        return true;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Poses_RecordWritesKeysAndOneLabel,
    "Ck.ProceduralAnimation.Action.Editor.Poses.RecordWritesKeysAndOneLabel",
    ck_test_procedural_action_pose_capture::TestFlags)

auto
    FCkTest_ProceduralAction_Poses_RecordWritesKeysAndOneLabel::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_pose_capture;
    auto Clip = MakeTransientClip();
    if (NOT TestTrue(TEXT("fixture clip compiles"), FillClip(*Clip)))
    { return false; }
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    if (NOT TestTrue(TEXT("record the body and the arm at an empty time"),
        Commands.RecordPose(Motion, MakePose(0.5, FVector{0.0, 0.0, 5.0}, FVector{15.0, 0.0, 0.0}), FName{TEXT("Raised")}, Error)))
    { AddInfo(Error); return false; }
    TestTrue(TEXT("record keyed the body"), HasBodyKeyAt(*Clip, 0.5));
    auto Value = FVector::ZeroVector;
    TestTrue(TEXT("record keyed the arm"), PositionAt(*Clip, Arm, 0.5, Value) && Value.Equals(FVector{15.0, 0.0, 0.0}));
    TestFalse(TEXT("the unticked leg gets no key"), PositionAt(*Clip, Leg, 0.5, Value));
    TestEqual(TEXT("the leg's keys are untouched"), FindTrack(*Clip, Leg)->Get_PositionKeys().Num(), 2);
    const auto Labels = ck::procedural_action_editor::Get_PoseLabels(*Clip);
    if (NOT TestEqual(TEXT("one label for the recorded pose"), Labels.Num(), 1))
    { return false; }
    TestEqual(TEXT("label phase"), Labels[0].Get_PhaseId(), Motion);
    TestEqual(TEXT("label time"), Labels[0].Get_Time().Get_Value(), 0.5);
    TestEqual(TEXT("label name"), Labels[0].Get_Label(), FName{TEXT("Raised")});

    if (NOT TestTrue(TEXT("record again at the same time with no name"),
        Commands.RecordPose(Motion, MakePose(0.5, FVector{0.0, 0.0, 6.0}, FVector{20.0, 0.0, 0.0}), NAME_None, Error)))
    { AddInfo(Error); return false; }
    TestTrue(TEXT("re-recording replaced the arm value"), PositionAt(*Clip, Arm, 0.5, Value) && Value.Equals(FVector{20.0, 0.0, 0.0}));
    TestEqual(TEXT("re-recording keeps one label"), ck::procedural_action_editor::Get_PoseLabels(*Clip).Num(), 1);
    TestEqual(TEXT("an unnamed re-record keeps the name"), LabelAt(*Clip, Motion, 0.5).Get(NAME_None), FName{TEXT("Raised")});

    if (NOT TestTrue(TEXT("rename the label"), Commands.RenameLabel(Motion, 0.5, FName{TEXT("Lifted")}, Error)))
    { AddInfo(Error); return false; }
    TestEqual(TEXT("renamed"), LabelAt(*Clip, Motion, 0.5).Get(NAME_None), FName{TEXT("Lifted")});
    TestFalse(TEXT("renaming an unlabelled time is refused"), Commands.RenameLabel(Motion, 0.25, FName{TEXT("Nothing")}, Error));

    if (NOT TestTrue(TEXT("move the arm key while the body keeps its key at 0.5"), Commands.MoveKey(Motion, Arm, 0.5, 0.75, Error)))
    { AddInfo(Error); return false; }
    TestTrue(TEXT("the label stays with the key that stayed"), LabelAt(*Clip, Motion, 0.5).IsSet());
    TestFalse(TEXT("the moved key takes no label"), LabelAt(*Clip, Motion, 0.75).IsSet());
    if (NOT TestTrue(TEXT("move the body key, the last key at 0.5"), Commands.MoveKey(Motion, BodyChannel(), 0.5, 0.6, Error)))
    { AddInfo(Error); return false; }
    TestFalse(TEXT("the label left the emptied time"), LabelAt(*Clip, Motion, 0.5).IsSet());
    TestEqual(TEXT("the label followed the last key it named"), LabelAt(*Clip, Motion, 0.6).Get(NAME_None), FName{TEXT("Lifted")});
    if (NOT TestTrue(TEXT("delete the body key the label names"), Commands.DeleteKey(Motion, BodyChannel(), 0.6, Error)))
    { AddInfo(Error); return false; }
    TestTrue(TEXT("a label with no key left is gone"), ck::procedural_action_editor::Get_PoseLabels(*Clip).IsEmpty());
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PoseCapture_UndoRedoRestoresTracksAndLabels,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.PoseCapture.UndoRedoRestoresTracksAndLabels",
    ck_test_procedural_action_pose_capture::TestFlags)

auto
    FCkTest_ProceduralAction_PoseCapture_UndoRedoRestoresTracksAndLabels::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_pose_capture;
    if (NOT TestNotNull(TEXT("editor transaction system"), GEditor))
    { return false; }
    auto Clip = MakeTransientClip();
    if (NOT TestTrue(TEXT("fixture clip compiles"), FillClip(*Clip)))
    { return false; }
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    if (NOT TestTrue(TEXT("record"), Commands.RecordPose(Motion, MakePose(0.5, FVector::ZeroVector,
            FVector{15.0, 0.0, 0.0}), FName{TEXT("Raised")}, Error))
        || NOT TestTrue(TEXT("re-key"), Commands.SetPoseKeys(Motion, MakePose(0.5, FVector::ZeroVector,
            FVector{25.0, 0.0, 0.0}), Error)))
    { AddInfo(Error); return false; }

    auto Value = FVector::ZeroVector;
    if (NOT TestTrue(TEXT("undo the re-key"), GEditor->UndoTransaction()))
    { return false; }
    TestTrue(TEXT("undo restored the recorded value"), PositionAt(*Clip, Arm, 0.5, Value) && Value.Equals(FVector{15.0, 0.0, 0.0}));
    TestTrue(TEXT("undo of a re-key keeps the label"), LabelAt(*Clip, Motion, 0.5).IsSet());
    if (NOT TestTrue(TEXT("undo the record"), GEditor->UndoTransaction()))
    { return false; }
    TestFalse(TEXT("undo removed the recorded arm key"), PositionAt(*Clip, Arm, 0.5, Value));
    TestFalse(TEXT("undo removed the recorded body key"), HasBodyKeyAt(*Clip, 0.5));
    TestTrue(TEXT("undo removed the label in the same step"), ck::procedural_action_editor::Get_PoseLabels(*Clip).IsEmpty());
    if (NOT TestTrue(TEXT("redo the record"), GEditor->RedoTransaction()))
    { return false; }
    TestTrue(TEXT("redo restored the arm key"), PositionAt(*Clip, Arm, 0.5, Value) && Value.Equals(FVector{15.0, 0.0, 0.0}));
    TestEqual(TEXT("redo restored the label"), LabelAt(*Clip, Motion, 0.5).Get(NAME_None), FName{TEXT("Raised")});
    if (NOT TestTrue(TEXT("redo the re-key"), GEditor->RedoTransaction()))
    { return false; }
    TestTrue(TEXT("redo restored the re-keyed value"), PositionAt(*Clip, Arm, 0.5, Value) && Value.Equals(FVector{25.0, 0.0, 0.0}));
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PoseCapture_CreateFromRecipeMatchesBuilder,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.PoseCapture.CreateFromRecipeMatchesBuilder",
    ck_test_procedural_action_pose_capture::TestFlags)

auto
    FCkTest_ProceduralAction_PoseCapture_CreateFromRecipeMatchesBuilder::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_pose_capture;
    if (NOT TestNotNull(TEXT("editor transaction system"), GEditor))
    { return false; }
    auto Recipe = FCk_ProceduralAction_RecipeSpec{ECk_ProceduralAction_Recipe::BodyLean, TArray<FName>{}};
    Recipe.Set_AmplitudeCm(12.0f);

    auto EditorClip = MakeTransientClip();
    auto Commands = FCk_ProceduralActionAuthoringCommands{EditorClip.Get()};
    auto Error = FString{};
    if (NOT TestTrue(TEXT("create from the body lean recipe"), Commands.CreateFromRecipe(Recipe, Error)))
    { AddInfo(Error); return false; }

    auto CodeClip = MakeTransientClip();
    auto Draft = FCk_ProceduralAction_ClipDraft{};
    UCk_Utils_ProceduralActionBuilder_UE::Make_FromRecipe(Recipe, Draft);
    if (NOT TestEqual(TEXT("code route builds"), UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Draft, CodeClip.Get()).Get_Status(),
        ECk_ProceduralAction_ReportStatus::Valid))
    { return false; }
    auto EditorCompiled = ck::procedural_action::FCompiledClip{};
    auto CodeCompiled = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("editor clip compiles"), ck::procedural_action::Compile(*EditorClip, EditorCompiled, Diagnostics))
        || NOT TestTrue(TEXT("code clip compiles"), ck::procedural_action::Compile(*CodeClip, CodeCompiled, Diagnostics)))
    { return false; }
    TestEqual(TEXT("editor and code recipes compile to the same content hash"),
        EditorCompiled.Get_ContentHash(), CodeCompiled.Get_ContentHash());
    TestEqual(TEXT("recipe phases"), EditorClip->Get_Phases().Num(), 3);
    const auto Labels = ck::procedural_action_editor::Get_PoseLabels(*EditorClip);
    TestFalse(TEXT("recipe key times are labelled"), Labels.IsEmpty());
    for (const auto& Label : Labels)
    {
        const auto* Phase = EditorClip->Get_Phases().FindByPredicate([&Label](const FCk_ProceduralAction_Phase& InPhase)
            { return InPhase.Get_Id() == Label.Get_PhaseId(); });
        TestTrue(TEXT("each recipe label names a body key"), Phase != nullptr
            && ck::procedural_action_editor::Get_ChannelsKeyedAt(*Phase, Label.Get_Time().Get_Value()).Contains(BodyChannel()));
    }

    TestFalse(TEXT("a recipe never overwrites existing content"), Commands.CreateFromRecipe(Recipe, Error));
    if (NOT TestTrue(TEXT("undo the recipe creation"), GEditor->UndoTransaction()))
    { return false; }
    TestTrue(TEXT("undo restores the empty clip"), EditorClip->Get_Phases().IsEmpty()
        && ck::procedural_action_editor::Get_PoseLabels(*EditorClip).IsEmpty());
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_MirrorDelegatesToBuilder,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.MirrorDelegatesToBuilder",
    ck_test_procedural_action_pose_capture::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_MirrorDelegatesToBuilder::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_pose_capture;
    const auto Left = FName{TEXT("Left")};
    const auto Right = FName{TEXT("Right")};
    auto Source = FCk_ProceduralAction_ClipDraft{};
    Source.Set_Slots(TArray<FCk_ProceduralAction_Slot>{
        FCk_ProceduralAction_Slot{Left}.Set_Space(ECk_ProceduralAction_TrackSpace::RootAtStart), FCk_ProceduralAction_Slot{Right}});
    auto Track = FCk_ProceduralAction_LimbTrack{Left};
    Track.Set_Interpolation(ECk_ProceduralAction_Interpolation::Smooth);
    auto Positions = TArray<FCk_ProceduralAction_VectorKey>{VectorKey(0.0, FVector{10.0, 20.0, 30.0}),
        VectorKey(0.5, FVector{12.0, 25.0, 31.0}), VectorKey(1.0, FVector{15.0, 22.0, 28.0})};
    Positions[1].Set_ArriveTangent(FVector{1.0, 2.0, 3.0});
    Positions[1].Set_LeaveTangent(FVector{1.0, 2.0, 3.0});
    Track.Set_PositionKeys(Positions);
    Track.Set_OrientationKeys(TArray<FCk_ProceduralAction_OrientationKey>{
        FCk_ProceduralAction_OrientationKey{FCk_FloatRange_0to1{0.0}, FQuat::Identity},
        FCk_ProceduralAction_OrientationKey{FCk_FloatRange_0to1{1.0}, FQuat{FVector{0.0, 0.0, 1.0}, 0.6}}});
    Track.Set_BendKeys(TArray<FCk_ProceduralAction_VectorKey>{VectorKey(0.0, FVector{0.0, 1.0, 0.0}),
        VectorKey(1.0, FVector{0.0, 0.0, 1.0})});
    auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Reach")}, FCk_Time{1.0}};
    Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
    Source.Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});

    auto Clip = MakeTransientClip();
    if (NOT TestEqual(TEXT("fixture builds"), UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Source, Clip.Get()).Get_Status(),
        ECk_ProceduralAction_ReportStatus::Valid))
    { return false; }
    auto Expected = Source;
    if (NOT TestEqual(TEXT("builder mirrors the draft"),
        UCk_Utils_ProceduralActionBuilder_UE::Mirror_Slot(Expected,
            FCk_ProceduralAction_MirrorSpec{Left, Right, ECk_ProceduralAction_MirrorPlane::XZ}).Get_Status(),
        ECk_ProceduralAction_ReportStatus::Valid))
    { return false; }

    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    if (NOT TestTrue(TEXT("editor command mirrors across the Y normal"), Commands.MirrorSlot(Left, Right, EAxis::Y, Error)))
    { AddInfo(Error); return false; }
    TestTrue(TEXT("command and builder produce identical phases, orientation included"), SamePhases(*Clip, Expected.Get_Phases()));

    auto Rejected = MakeTransientClip();
    UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Source, Rejected.Get());
    auto NormalizedSlots = Source.Get_Slots();
    NormalizedSlots[0].Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
    Rejected->Set_Slots(NormalizedSlots);
    TestFalse(TEXT("the command rejects what the builder rejects"),
        FCk_ProceduralActionAuthoringCommands{Rejected.Get()}.MirrorSlot(Left, Right, EAxis::Y, Error));
    TestTrue(TEXT("rejection carries the builder's field code"), Error.Contains(TEXT("MirrorSpace")));
    TestEqual(TEXT("rejected mirror leaves one track"), Rejected->Get_Phases()[0].Get_LimbTracks().Num(), 1);
    return NOT HasAnyErrors();
}

#endif
