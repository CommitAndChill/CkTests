#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimation/ActionBuilder/CkProceduralActionBuilder_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ck_test_procedural_action_mirror
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const auto Valid = ECk_ProceduralAction_ReportStatus::Valid;
    const auto Invalid = ECk_ProceduralAction_ReportStatus::Invalid;
    const auto Left = FName{TEXT("Left")};
    const auto Right = FName{TEXT("Right")};

    auto
    HasDiagnostic(
        const FCk_ProceduralAction_ValidationReport& InReport,
        FName InCode,
        const FString& InFieldPath)
        -> bool
    {
        return InReport.Get_Diagnostics().ContainsByPredicate([&](const FCk_ProceduralAction_Diagnostic& InDiagnostic)
        { return InDiagnostic.Get_Code() == InCode && InDiagnostic.Get_FieldPath() == InFieldPath; });
    }

    auto
    DraftsEqual(
        const FCk_ProceduralAction_ClipDraft& InA,
        const FCk_ProceduralAction_ClipDraft& InB)
        -> bool
    { return FCk_ProceduralAction_ClipDraft::StaticStruct()->CompareScriptStruct(&InA, &InB, PPF_None); }

    auto
    ReflectionMatrix(
        ECk_ProceduralAction_MirrorPlane InPlane)
        -> FMatrix
    {
        auto Matrix = FMatrix::Identity;
        const auto Axis = InPlane == ECk_ProceduralAction_MirrorPlane::XY ? 2 : InPlane == ECk_ProceduralAction_MirrorPlane::XZ ? 1 : 0;
        Matrix.M[Axis][Axis] = -1.0;
        return Matrix;
    }

    auto
    MakeVectorKey(
        double InTime,
        const FVector& InValue,
        const FVector& InArrive,
        const FVector& InLeave)
        -> FCk_ProceduralAction_VectorKey
    {
        auto Key = FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{InTime}, InValue};
        Key.Set_ArriveTangent(InArrive);
        Key.Set_LeaveTangent(InLeave);
        return Key;
    }

    auto
    MakeSourceTrack(
        ECk_ProceduralAction_TrackSpace InSpace)
        -> FCk_ProceduralAction_LimbTrack
    {
        auto Track = FCk_ProceduralAction_LimbTrack{Left};
        Track.Set_Space(InSpace);
        if (InSpace == ECk_ProceduralAction_TrackSpace::AnchorAtStart)
        { Track.Set_AnchorId(FName{TEXT("Target")}); }
        Track.Set_Interpolation(ECk_ProceduralAction_Interpolation::Smooth);
        Track.Set_ReachPolicy(ECk_ProceduralAction_ReachPolicy::ProjectToReach);
        Track.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{
            MakeVectorKey(0.0, FVector{10.0, 20.0, 30.0}, FVector{1.0, 2.0, 3.0}, FVector{4.0, 5.0, 6.0}),
            MakeVectorKey(1.0, FVector{40.0, 50.0, -60.0}, FVector{-1.0, -2.0, -3.0}, FVector{-4.0, -5.0, -6.0})});
        Track.Set_BendKeys(TArray<FCk_ProceduralAction_VectorKey>{
            MakeVectorKey(0.0, FVector{0.0, 0.6, 0.8}, FVector{0.0, 1.0, 0.0}, FVector{0.0, 0.0, 1.0}),
            MakeVectorKey(1.0, FVector{0.6, 0.8, 0.0}, FVector::ZeroVector, FVector::ZeroVector)});
        Track.Set_OrientationKeys(TArray<FCk_ProceduralAction_OrientationKey>{
            FCk_ProceduralAction_OrientationKey{FCk_FloatRange_0to1{0.0}, FQuat::Identity},
            FCk_ProceduralAction_OrientationKey{FCk_FloatRange_0to1{1.0}, FQuat{FVector{1.0, 2.0, 3.0}.GetSafeNormal(), 0.7}}});
        return Track;
    }

    auto
    MakeDraft(
        ECk_ProceduralAction_TrackSpace InSpace)
        -> FCk_ProceduralAction_ClipDraft
    {
        auto Draft = FCk_ProceduralAction_ClipDraft{};
        Draft.Set_Slots(TArray<FCk_ProceduralAction_Slot>{FCk_ProceduralAction_Slot{Left}, FCk_ProceduralAction_Slot{Right}});
        auto Reach = FCk_ProceduralAction_Phase{FName{TEXT("Reach")}, FCk_Time{0.5}};
        Reach.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{MakeSourceTrack(InSpace)});
        auto Hold = FCk_ProceduralAction_Phase{FName{TEXT("Hold")}, FCk_Time{0.5}};
        Draft.Set_Phases(TArray<FCk_ProceduralAction_Phase>{Reach, Hold});
        return Draft;
    }

    auto
    FindTrack(
        const FCk_ProceduralAction_Phase& InPhase,
        FName InSlot)
        -> const FCk_ProceduralAction_LimbTrack*
    {
        return InPhase.Get_LimbTracks().FindByPredicate([InSlot](const FCk_ProceduralAction_LimbTrack& InTrack)
        { return InTrack.Get_Slot() == InSlot; });
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionMirrorPlaneTest,
    "Ck.ProceduralAnimation.Action.Mirror.XZUsesYNormal",
    ck_test_procedural_action_mirror::TestFlags)

auto
FCkProceduralActionMirrorPlaneTest::RunTest(
    const FString&)
    -> bool
{
    const auto Vector = FVector{1.0, 2.0, 3.0};
    TestEqual(TEXT("XZ negates Y"), ck::procedural_action::ReflectVector(Vector, ECk_ProceduralAction_MirrorPlane::XZ), FVector{1.0, -2.0, 3.0});
    TestEqual(TEXT("XY negates Z"), ck::procedural_action::ReflectVector(Vector, ECk_ProceduralAction_MirrorPlane::XY), FVector{1.0, 2.0, -3.0});
    TestEqual(TEXT("YZ negates X"), ck::procedural_action::ReflectVector(Vector, ECk_ProceduralAction_MirrorPlane::YZ), FVector{-1.0, 2.0, 3.0});
    TestEqual(TEXT("Reflection is an involution"), ck::procedural_action::ReflectVector(
        ck::procedural_action::ReflectVector(Vector, ECk_ProceduralAction_MirrorPlane::XZ), ECk_ProceduralAction_MirrorPlane::XZ), Vector);
    TestEqual(TEXT("The spec defaults to the XZ plane"), FCk_ProceduralAction_MirrorSpec{}.Get_Plane(), ECk_ProceduralAction_MirrorPlane::XZ);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionMirrorReflectsTest,
    "Ck.ProceduralAnimation.Action.Mirror.ReflectsPositionsTangentsBendsAndOrientation",
    ck_test_procedural_action_mirror::TestFlags)

auto
FCkProceduralActionMirrorReflectsTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_mirror;
    for (const auto Plane : {ECk_ProceduralAction_MirrorPlane::XY, ECk_ProceduralAction_MirrorPlane::XZ, ECk_ProceduralAction_MirrorPlane::YZ})
    {
        const auto Rotation = FQuat{FVector{0.3, -0.5, 0.8}.GetSafeNormal(), 1.1};
        const auto Reflected = ck::procedural_action::ReflectRotation(Rotation, Plane);
        const auto S = ReflectionMatrix(Plane);
        const auto Expected = S * FQuatRotationMatrix{Rotation} * S;
        const auto Actual = FQuatRotationMatrix{Reflected};
        TestTrue(TEXT("Reflected rotation equals S R S"), Actual.Equals(Expected, 1.0e-6));
        TestTrue(TEXT("Reflected rotation is unit length"), FMath::IsNearlyEqual(Reflected.Size(), 1.0, 1.0e-9));
        TestTrue(TEXT("Reflected rotation is proper (no negative scale)"), FMath::IsNearlyEqual(Actual.Determinant(), 1.0, 1.0e-6));
    }

    auto Draft = MakeDraft(ECk_ProceduralAction_TrackSpace::BodyLocal);
    const auto Report = UCk_Utils_ProceduralActionBuilder_UE::Mirror_Slot(Draft, FCk_ProceduralAction_MirrorSpec{Left, Right,
        ECk_ProceduralAction_MirrorPlane::XZ});
    if (NOT TestEqual(TEXT("Mirror accepted"), Report.Get_Status(), Valid))
    { return false; }
    const auto* Source = FindTrack(Draft.Get_Phases()[0], Left);
    const auto* Destination = FindTrack(Draft.Get_Phases()[0], Right);
    if (NOT TestNotNull(TEXT("Destination track written"), Destination) || NOT TestNotNull(TEXT("Source kept"), Source))
    { return false; }
    TestNull(TEXT("A phase without a source track gets no destination track"), FindTrack(Draft.Get_Phases()[1], Right));
    TestEqual(TEXT("Destination keeps the source frame"), Destination->Get_Space(), Source->Get_Space());
    TestEqual(TEXT("Destination keeps interpolation"), Destination->Get_Interpolation(), Source->Get_Interpolation());
    TestEqual(TEXT("Destination keeps reach policy"), Destination->Get_ReachPolicy(), Source->Get_ReachPolicy());
    const auto Reflect = [](const FVector& InVector)
    { return ck::procedural_action::ReflectVector(InVector, ECk_ProceduralAction_MirrorPlane::XZ); };
    for (auto Index = 0; Index < Source->Get_PositionKeys().Num(); ++Index)
    {
        const auto& From = Source->Get_PositionKeys()[Index];
        const auto& To = Destination->Get_PositionKeys()[Index];
        TestTrue(TEXT("Position reflected"), To.Get_Value().Equals(Reflect(From.Get_Value())) && To.Get_Time().Get_Value() == From.Get_Time().Get_Value());
        TestTrue(TEXT("Position tangents reflected"), To.Get_ArriveTangent().Equals(Reflect(From.Get_ArriveTangent()))
            && To.Get_LeaveTangent().Equals(Reflect(From.Get_LeaveTangent())));
    }
    for (auto Index = 0; Index < Source->Get_BendKeys().Num(); ++Index)
    {
        const auto& From = Source->Get_BendKeys()[Index];
        const auto& To = Destination->Get_BendKeys()[Index];
        TestTrue(TEXT("Bend guide and tangents reflected"), To.Get_Value().Equals(Reflect(From.Get_Value()))
            && To.Get_ArriveTangent().Equals(Reflect(From.Get_ArriveTangent())) && To.Get_LeaveTangent().Equals(Reflect(From.Get_LeaveTangent())));
    }
    for (auto Index = 0; Index < Source->Get_OrientationKeys().Num(); ++Index)
    {
        const auto& From = Source->Get_OrientationKeys()[Index];
        const auto& To = Destination->Get_OrientationKeys()[Index];
        TestTrue(TEXT("Orientation reflected as (w, -S v)"), To.Get_Value().Equals(
            ck::procedural_action::ReflectRotation(From.Get_Value(), ECk_ProceduralAction_MirrorPlane::XZ), 1.0e-9));
    }

    auto Compiled = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    TestTrue(TEXT("Mirrored draft compiles"), ck::procedural_action::Compile(ck::procedural_action::MakeClipSource(Draft), Compiled, Diagnostics));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionMirrorRejectsTest,
    "Ck.ProceduralAnimation.Action.Mirror.RejectsOccupiedDestinationMissingSourceAndRestNormalized",
    ck_test_procedural_action_mirror::TestFlags)

auto
FCkProceduralActionMirrorRejectsTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_mirror;
    const auto Mirror = FCk_ProceduralAction_MirrorSpec{Left, Right, ECk_ProceduralAction_MirrorPlane::XZ};

    auto Occupied = MakeDraft(ECk_ProceduralAction_TrackSpace::BodyLocal);
    auto Phases = Occupied.Get_Phases();
    auto Tracks = Phases[0].Get_LimbTracks();
    Tracks.Emplace(Right);
    Phases[0].Set_LimbTracks(Tracks);
    Occupied.Set_Phases(Phases);
    auto Before = Occupied;
    const auto OccupiedReport = UCk_Utils_ProceduralActionBuilder_UE::Mirror_Slot(Occupied, Mirror);
    TestEqual(TEXT("An occupied destination rejects"), OccupiedReport.Get_Status(), Invalid);
    TestTrue(TEXT("The occupied destination track is named"), HasDiagnostic(OccupiedReport, TEXT("MirrorDestination"), TEXT("Phases[0].LimbTracks[1]")));
    TestTrue(TEXT("Nothing overwritten"), DraftsEqual(Before, Occupied));

    auto Empty = MakeDraft(ECk_ProceduralAction_TrackSpace::BodyLocal);
    Phases = Empty.Get_Phases();
    Phases[0].Set_LimbTracks({});
    Empty.Set_Phases(Phases);
    Before = Empty;
    const auto MissingReport = UCk_Utils_ProceduralActionBuilder_UE::Mirror_Slot(Empty, Mirror);
    TestEqual(TEXT("A source without tracks rejects"), MissingReport.Get_Status(), Invalid);
    TestTrue(TEXT("Missing source is named"), HasDiagnostic(MissingReport, TEXT("MirrorSource"), TEXT("Mirror.SourceSlot")));
    TestTrue(TEXT("Nothing written"), DraftsEqual(Before, Empty));

    auto Undeclared = MakeDraft(ECk_ProceduralAction_TrackSpace::BodyLocal);
    Before = Undeclared;
    TestEqual(TEXT("An undeclared destination rejects"), UCk_Utils_ProceduralActionBuilder_UE::Mirror_Slot(Undeclared,
        FCk_ProceduralAction_MirrorSpec{Left, FName{TEXT("Tail")}, ECk_ProceduralAction_MirrorPlane::XZ}).Get_Status(), Invalid);
    TestEqual(TEXT("Mirroring a slot onto itself rejects"), UCk_Utils_ProceduralActionBuilder_UE::Mirror_Slot(Undeclared,
        FCk_ProceduralAction_MirrorSpec{Left, Left, ECk_ProceduralAction_MirrorPlane::XZ}).Get_Status(), Invalid);
    TestTrue(TEXT("Nothing written"), DraftsEqual(Before, Undeclared));

    for (const auto Space : {ECk_ProceduralAction_TrackSpace::LimbRestNormalized, ECk_ProceduralAction_TrackSpace::AnchorAtStart})
    {
        auto Draft = MakeDraft(Space);
        Before = Draft;
        const auto SpaceReport = UCk_Utils_ProceduralActionBuilder_UE::Mirror_Slot(Draft, Mirror);
        TestEqual(TEXT("A frame that needs subject topology rejects in the draft"), SpaceReport.Get_Status(), Invalid);
        TestTrue(TEXT("The source track space is named"), HasDiagnostic(SpaceReport, TEXT("MirrorSpace"), TEXT("Phases[0].LimbTracks[0].Space")));
        TestTrue(TEXT("Nothing written"), DraftsEqual(Before, Draft));
    }

    auto Framed = MakeDraft(ECk_ProceduralAction_TrackSpace::BodyLocal);
    Phases = Framed.Get_Phases();
    auto Held = FCk_ProceduralAction_LimbTrack{Right};
    Held.Set_Space(ECk_ProceduralAction_TrackSpace::RootAtStart);
    Phases[1].Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Held});
    Framed.Set_Phases(Phases);
    Before = Framed;
    const auto FrameReport = UCk_Utils_ProceduralActionBuilder_UE::Mirror_Slot(Framed, Mirror);
    TestEqual(TEXT("A destination already framed differently rejects"), FrameReport.Get_Status(), Invalid);
    TestTrue(TEXT("The frame conflict is named"), HasDiagnostic(FrameReport, TEXT("SpaceChangesAcrossPhases"), TEXT("Phases[0].LimbTracks[1].Space")));
    TestTrue(TEXT("Nothing written"), DraftsEqual(Before, Framed));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionMirrorSourceTest,
    "Ck.ProceduralAnimation.Action.Mirror.SourceUnchanged",
    ck_test_procedural_action_mirror::TestFlags)

auto
FCkProceduralActionMirrorSourceTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_mirror;
    auto Draft = MakeDraft(ECk_ProceduralAction_TrackSpace::RootAtStart);
    const auto SourceBefore = *FindTrack(Draft.Get_Phases()[0], Left);
    const auto SlotsBefore = Draft.Get_Slots();
    if (NOT TestEqual(TEXT("Mirror accepted"), UCk_Utils_ProceduralActionBuilder_UE::Mirror_Slot(Draft,
        FCk_ProceduralAction_MirrorSpec{Left, Right, ECk_ProceduralAction_MirrorPlane::YZ}).Get_Status(), Valid))
    { return false; }
    const auto* SourceAfter = FindTrack(Draft.Get_Phases()[0], Left);
    TestTrue(TEXT("Source track is byte-identical after mirroring"), SourceAfter != nullptr
        && FCk_ProceduralAction_LimbTrack::StaticStruct()->CompareScriptStruct(&SourceBefore, SourceAfter, PPF_None));
    TestEqual(TEXT("Mirroring declares no slot"), Draft.Get_Slots().Num(), SlotsBefore.Num());
    TestTrue(TEXT("Mirroring adds no body motion or event"), Draft.Get_BodyChannel().Get_Enabled() == false
        && Draft.Get_Markers().IsEmpty() && Draft.Get_Windows().IsEmpty());
    return true;
}
#endif
