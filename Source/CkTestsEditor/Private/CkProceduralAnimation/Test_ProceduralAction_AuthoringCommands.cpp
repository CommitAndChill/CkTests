#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringCommands.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringSession.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"

#include "Editor.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_procedural_action_commands
{
    // One limb target at InTime, as a gizmo release or a Details edit writes it.
    auto
        LimbPose(
            FName InSlot,
            double InTime,
            const FVector& InPosition)
        -> FCk_ProceduralAction_PoseKey
    {
        auto Pose = FCk_ProceduralAction_PoseKey{FCk_FloatRange_0to1{InTime}};
        Pose.Set_Limbs(TArray<FCk_ProceduralAction_LimbPoseTarget>{FCk_ProceduralAction_LimbPoseTarget{InSlot, InPosition}});
        return Pose;
    }

    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_StableIds,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.StableIds",
    ck_test_procedural_action_commands::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_StableIds::
    RunTest(
        const FString&)
    -> bool
{
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    if (NOT TestTrue(TEXT("add first phase"), Commands.AddPhase(FName(TEXT("Windup")), FCk_Time{0.3}, Error))
        || NOT TestTrue(TEXT("add second phase"), Commands.AddPhase(FName(TEXT("Strike")), FCk_Time{0.4}, Error))
        || NOT TestTrue(TEXT("add marker by stable phase ID"),
            Commands.AddMarker(FName(TEXT("Impact")), FName(TEXT("Strike")), 0.5, Error)))
    { return false; }

    if (NOT TestTrue(TEXT("reorder phase"), Commands.ReorderPhase(FName(TEXT("Strike")), 0, Error)))
    { return false; }
    TestEqual(TEXT("phase moved without identity rewrite"), Clip->Get_Phases()[0].Get_Id(), FName(TEXT("Strike")));
    TestEqual(TEXT("marker still names same phase"),
        Clip->Get_Markers()[0].Get_At().Get_PhaseId(), FName(TEXT("Strike")));
    if (NOT TestTrue(TEXT("add half-open event window after reorder"),
        Commands.AddWindow(FName(TEXT("Danger")), FName(TEXT("Strike")), 0.8,
            FName(TEXT("Windup")), 0.6, Error)))
    { return false; }
    TestEqual(TEXT("window start still names same phase"),
        Clip->Get_Windows()[0].Get_Start().Get_PhaseId(), FName(TEXT("Strike")));
    TestFalse(TEXT("reorder refuses to invert event window"),
        Commands.ReorderPhase(FName(TEXT("Windup")), 0, Error));
    TestTrue(TEXT("reorder refusal identifies window"), Error.Contains(TEXT("Danger")));
    TestFalse(TEXT("referenced phase cannot be deleted"), Commands.RemovePhase(FName(TEXT("Strike")), Error));
    TestTrue(TEXT("refusal names affected marker or window"), Error.Contains(TEXT("Impact")));
    TestEqual(TEXT("refused deletion leaves both phases"), Clip->Get_Phases().Num(), 2);
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_WindowBoundaries,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.WindowBoundaries",
    ck_test_procedural_action_commands::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_WindowBoundaries::
    RunTest(
        const FString&)
    -> bool
{
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    const auto A = FName{TEXT("A")};
    const auto B = FName{TEXT("B")};
    const auto C = FName{TEXT("C")};
    if (NOT TestTrue(TEXT("phase A"), Commands.AddPhase(A, FCk_Time{0.2}, Error))
        || NOT TestTrue(TEXT("phase B"), Commands.AddPhase(B, FCk_Time{0.3}, Error))
        || NOT TestTrue(TEXT("phase C"), Commands.AddPhase(C, FCk_Time{0.4}, Error)))
    { return false; }
    TestFalse(TEXT("adjacent A@1 B@0 is zero absolute duration"),
        Commands.AddWindow(FName{TEXT("Zero")}, A, 1.0, B, 0.0, Error));
    TestTrue(TEXT("zero-window rejection leaves clip unchanged"), Clip->Get_Windows().IsEmpty());
    if (NOT TestTrue(TEXT("an interior boundary end is valid"),
        Commands.AddWindow(FName{TEXT("Across")}, A, 0.5, B, 0.0, Error))
        || NOT TestTrue(TEXT("nonadjacent A@1 C@0 includes full B duration"),
            Commands.AddWindow(FName{TEXT("Spanning")}, A, 1.0, C, 0.0, Error)))
    { return false; }
    TestFalse(TEXT("moving start to adjacent equality rejects"),
        Commands.MoveWindowEdge(FName{TEXT("Across")}, true, A, 1.0, Error));
    TestEqual(TEXT("failed start drag keeps old normalized time"),
        Clip->Get_Windows()[0].Get_Start().Get_Time().Get_Value(), 0.5);
    if (NOT TestTrue(TEXT("moving end inside B remains positive"),
        Commands.MoveWindowEdge(FName{TEXT("Across")}, false, B, 0.1, Error)))
    { return false; }
    if (NOT TestTrue(TEXT("start at A end is valid while end is inside B"),
        Commands.MoveWindowEdge(FName{TEXT("Across")}, true, A, 1.0, Error)))
    { return false; }
    TestFalse(TEXT("moving end to boundary equality rejects"),
        Commands.MoveWindowEdge(FName{TEXT("Across")}, false, B, 0.0, Error));
    TestFalse(TEXT("reordering away intervening B rejects zero Spanning interval"),
        Commands.ReorderPhase(B, 2, Error));
    TestEqual(TEXT("failed reorder keeps phase B between A and C"),
        Clip->Get_Phases()[1].Get_Id(), B);
    TestEqual(TEXT("failed reorder preserves both windows"), Clip->Get_Windows().Num(), 2);
    auto Windows = Clip->Get_Windows();
    Windows[1].Set_End(FCk_ProceduralAction_TimeRef{C, FCk_FloatRange_0to1{0.1}});
    Clip->Set_Windows(Windows);
    TestTrue(TEXT("reorder accepts positive interval into adjacent C interior"),
        Commands.ReorderPhase(B, 2, Error));
    TestEqual(TEXT("valid reorder moved B after C"), Clip->Get_Phases()[2].Get_Id(), B);
    auto DraftPhases = Clip->Get_Phases();
    DraftPhases[2].Set_Duration(FCk_Time{});
    Clip->Set_Phases(DraftPhases);
    TestFalse(TEXT("window command rejects invalid draft phase duration before transaction"),
        Commands.AddWindow(FName{TEXT("InvalidDuration")}, A, 0.0, B, 0.5, Error));
    TestEqual(TEXT("invalid-draft window refusal preserves saveable draft"),
        Clip->Get_Windows().Num(), 2);
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_TracksAndUndo,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.TracksAndUndo",
    ck_test_procedural_action_commands::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_TracksAndUndo::
    RunTest(
        const FString&)
    -> bool
{
    if (NOT TestNotNull(TEXT("editor transaction system"), GEditor))
    { return false; }
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    if (NOT TestTrue(TEXT("add phase"), Commands.AddPhase(FName(TEXT("Reach")), FCk_Time{1.0}, Error))
        || NOT TestTrue(TEXT("add slot"), Commands.AddSlot(FName(TEXT("Arm")), Error))
        || NOT TestTrue(TEXT("add an endpoint key, and the slot's track with it"),
            Commands.SetPoseKeys(FName(TEXT("Reach")), ck_test_procedural_action_commands::LimbPose(FName(TEXT("Arm")), 0.0,
                FVector{10.0, 0.0, 0.0}), Error)))
    { return false; }

    TestFalse(TEXT("referenced slot cannot be deleted"), Commands.RemoveSlot(FName(TEXT("Arm")), Error));
    TestTrue(TEXT("slot refusal names affected phase"), Error.Contains(TEXT("Reach")));
    if (NOT TestTrue(TEXT("undo endpoint key command"), GEditor->UndoTransaction()))
    { return false; }
    TestTrue(TEXT("undo removes the key and the track it added"), Clip->Get_Phases()[0].Get_LimbTracks().IsEmpty());
    if (NOT TestTrue(TEXT("redo endpoint key command"), GEditor->RedoTransaction()))
    { return false; }
    TestEqual(TEXT("redo restores key at its normalized time"),
        Clip->Get_Phases()[0].Get_LimbTracks()[0].Get_PositionKeys()[0].Get_Time().Get_Value(), 0.0);
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_EventDrag,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.EventDrag",
    ck_test_procedural_action_commands::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_EventDrag::
    RunTest(
        const FString&)
    -> bool
{
    if (NOT TestNotNull(TEXT("editor transaction system"), GEditor))
    { return false; }
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    const auto Phase = FName{TEXT("Reach")};
    const auto Marker = FName{TEXT("Contact")};
    const auto Window = FName{TEXT("Active")};
    if (NOT TestTrue(TEXT("phase"), Commands.AddPhase(Phase, FCk_Time{1.0}, Error))
        || NOT TestTrue(TEXT("marker"), Commands.AddMarker(Marker, Phase, 0.4, Error))
        || NOT TestTrue(TEXT("window"), Commands.AddWindow(Window, Phase, 0.25, Phase, 0.75, Error)))
    { return false; }
    TestFalse(TEXT("window start cannot cross exclusive end"),
        Commands.MoveWindowEdge(Window, true, Phase, 0.75, Error));
    TestEqual(TEXT("rejected drag leaves window unchanged"),
        Clip->Get_Windows()[0].Get_Start().Get_Time().Get_Value(), 0.25);
    if (NOT TestTrue(TEXT("drag marker by stable ID"), Commands.MoveMarker(Marker, Phase, 0.5, Error))
        || NOT TestTrue(TEXT("drag window start"), Commands.MoveWindowEdge(Window, true, Phase, 0.5, Error)))
    { return false; }
    TestEqual(TEXT("window moved without changing exclusive end"),
        Clip->Get_Windows()[0].Get_End().Get_Time().Get_Value(), 0.75);
    if (NOT TestTrue(TEXT("undo one completed edge drag"), GEditor->UndoTransaction()))
    { return false; }
    TestEqual(TEXT("undo restores original start"),
        Clip->Get_Windows()[0].Get_Start().Get_Time().Get_Value(), 0.25);
    TestEqual(TEXT("marker remains at its separate completed drag"),
        Clip->Get_Markers()[0].Get_At().Get_Time().Get_Value(), 0.5);
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_MirrorAndSpace,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.MirrorAndSpace",
    ck_test_procedural_action_commands::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_MirrorAndSpace::
    RunTest(
        const FString&)
    -> bool
{
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    const auto Phase = FName{TEXT("Reach")};
    const auto Left = FName{TEXT("Left")};
    const auto Right = FName{TEXT("Right")};
    if (NOT TestTrue(TEXT("phase"), Commands.AddPhase(Phase, FCk_Time{1.0}, Error))
        || NOT TestTrue(TEXT("left slot"), Commands.AddSlot(Left, Error))
        || NOT TestTrue(TEXT("right slot"), Commands.AddSlot(Right, Error))
        || NOT TestTrue(TEXT("explicit root-space mode before keying"),
            Commands.SetSlotSpace(Left, ECk_ProceduralAction_TrackSpace::RootAtStart, Error))
        || NOT TestTrue(TEXT("source key"), Commands.SetPoseKeys(Phase,
            ck_test_procedural_action_commands::LimbPose(Left, 0.0, FVector{10.0, 20.0, 30.0}), Error)))
    { return false; }
    TestFalse(TEXT("changing keyed space refuses reinterpretation"),
        Commands.SetSlotSpace(Left, ECk_ProceduralAction_TrackSpace::LimbRestNormalized, Error));
    TestEqual(TEXT("the slot keeps the frame its keys were authored in"),
        Clip->Get_Slots()[0].Get_Space(), ECk_ProceduralAction_TrackSpace::RootAtStart);
    if (NOT TestTrue(TEXT("explicit mirror into destination slot"),
        Commands.MirrorSlot(Left, Right, EAxis::Y, Error)))
    { return false; }
    const auto& Tracks = Clip->Get_Phases()[0].Get_LimbTracks();
    TestEqual(TEXT("mirror adds destination without changing source"), Tracks.Num(), 2);
    TestTrue(TEXT("Y-plane mirror reflects position only along Y"),
        Tracks[1].Get_PositionKeys()[0].Get_Value().Equals(FVector{10.0, -20.0, 30.0}));
    TestFalse(TEXT("mirror never silently overwrites existing destination"),
        Commands.MirrorSlot(Left, Right, EAxis::Y, Error));
    TestEqual(TEXT("rejected overwrite leaves two tracks"), Clip->Get_Phases()[0].Get_LimbTracks().Num(), 2);
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_RetargetMapping,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.RetargetMapping",
    ck_test_procedural_action_commands::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_RetargetMapping::
    RunTest(
        const FString&)
    -> bool
{
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    auto Config = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{
        NewObject<UCk_ProceduralActionPreview_Config>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    const auto Limb = FName{TEXT("PhysicalArm")};
    Config->Set_Layout(FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{
        FCk_ProceduralLimb_Layout{Limb, FTransform::Identity, TArray<float>{80.0f},
            FTransform{FVector{80.0, 0.0, 0.0}}, FVector::UpVector}}});
    auto PreviewBinding = FCk_ProceduralActionPreview_LimbBinding{};
    PreviewBinding.Set_LimbId(Limb);
    PreviewBinding.Set_SegmentFrames(TArray<FTransform>{FTransform::Identity});
    Config->Set_LimbBindings(TArray<FCk_ProceduralActionPreview_LimbBinding>{PreviewBinding});
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    const auto Slot = FName{TEXT("Arm")};
    if (NOT TestTrue(TEXT("declare source slot"), Commands.AddSlot(Slot, Error))
        || NOT TestTrue(TEXT("bind source slot to physical limb"),
            Commands.RetargetSlot(Config.Get(), Slot, Limb, Error)))
    { return false; }
    const auto Subject = ck::procedural_action_editor::Get_Subject(*Clip);
    const auto& Bindings = Subject.Get_SlotBindings();
    if (NOT TestEqual(TEXT("the clip's subject keeps one mapping"), Bindings.Num(), 1))
    { return false; }
    TestEqual(TEXT("mapping keeps authored slot identity"), Bindings[0].Get_Slot(), Slot);
    TestEqual(TEXT("mapping selects physical limb"), Bindings[0].Get_LimbId(), Limb);
    TestTrue(TEXT("the preview config is only read"), Config->Get_SlotBindings().IsEmpty());
    TestFalse(TEXT("unknown target limb rejected"),
        Commands.RetargetSlot(Config.Get(), Slot, FName{TEXT("Missing")}, Error));
    TestEqual(TEXT("rejected retarget preserves mapping"),
        ck::procedural_action_editor::Get_Subject(*Clip).Get_SlotBindings()[0].Get_LimbId(), Limb);
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_GizmoKeys,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.GizmoKeys",
    ck_test_procedural_action_commands::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_GizmoKeys::
    RunTest(
        const FString&)
    -> bool
{
    if (NOT TestNotNull(TEXT("editor transaction system"), GEditor))
    { return false; }
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    const auto Phase = FName{TEXT("Reach")};
    const auto Slot = FName{TEXT("Arm")};
    if (NOT TestTrue(TEXT("phase"), Commands.AddPhase(Phase, FCk_Time{1.0}, Error))
        || NOT TestTrue(TEXT("slot"), Commands.AddSlot(Slot, Error))
        || NOT TestTrue(TEXT("gizmo adds endpoint key"),
            Commands.SetPoseKeys(Phase, ck_test_procedural_action_commands::LimbPose(Slot, 0.5, FVector{10.0, 0.0, 0.0}), Error)))
    { return false; }
    auto Bent = ck_test_procedural_action_commands::LimbPose(Slot, 0.5, FVector{10.0, 0.0, 0.0});
    Bent.Get_Limbs()[0].Set_BendMode(ECk_ProceduralAction_ValueMode::Set).Set_Bend(FVector::YAxisVector);
    if (NOT TestTrue(TEXT("gizmo adds separate bend key"), Commands.SetPoseKeys(Phase, Bent, Error)))
    { return false; }
    TestEqual(TEXT("the bend key holds the direction"),
        Clip->Get_Phases()[0].Get_LimbTracks()[0].Get_BendKeys()[0].Get_Value(), FVector::YAxisVector);
    if (NOT TestTrue(TEXT("undo bend drag"), GEditor->UndoTransaction()))
    { return false; }
    TestTrue(TEXT("undo bend leaves endpoint key intact"),
        Clip->Get_Phases()[0].Get_LimbTracks()[0].Get_BendKeys().IsEmpty()
            && Clip->Get_Phases()[0].Get_LimbTracks()[0].Get_PositionKeys().Num() == 1);
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_RenameSlot,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.RenameSlot",
    ck_test_procedural_action_commands::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_RenameSlot::
    RunTest(
        const FString&)
    -> bool
{
    if (NOT TestNotNull(TEXT("editor transaction system"), GEditor))
    { return false; }
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    auto Config = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{
        NewObject<UCk_ProceduralActionPreview_Config>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    const auto OldId = FName{TEXT("OldArm")};
    const auto NewId = FName{TEXT("NewArm")};
    const auto LimbId = FName{TEXT("PhysicalArm")};
    auto Slot = FCk_ProceduralAction_Slot{OldId};
    Slot.Set_InitialSource(ECk_ProceduralAction_InitialSource::AuthoredRest);
    Slot.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
    Clip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{Slot});
    auto Phases = TArray<FCk_ProceduralAction_Phase>{};
    for (const auto Index : {0, 1, 2})
    {
        auto Phase = FCk_ProceduralAction_Phase{
            FName{*FString::Printf(TEXT("Phase%d"), Index)}, FCk_Time{0.2}};
        auto Track = FCk_ProceduralAction_LimbTrack{OldId};
        Track.Set_Interpolation(ECk_ProceduralAction_Interpolation::Smooth);
        Track.Set_ReachPolicy(ECk_ProceduralAction_ReachPolicy::ProjectToReach);
        auto Position = FCk_ProceduralAction_VectorKey{
            FCk_FloatRange_0to1{0.5}, FVector{static_cast<double>(Index), 2.0, 3.0}};
        Position.Set_ArriveTangent(FVector{4.0, 5.0, 6.0});
        Position.Set_LeaveTangent(FVector{7.0, 8.0, 9.0});
        Track.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{Position});
        Track.Set_OrientationKeys(TArray<FCk_ProceduralAction_OrientationKey>{
            FCk_ProceduralAction_OrientationKey{FCk_FloatRange_0to1{0.5}, FQuat::Identity}});
        Track.Set_BendKeys(TArray<FCk_ProceduralAction_VectorKey>{
            FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.5}, FVector::UpVector}});
        Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
        Phases.Add(Phase);
    }
    Clip->Set_Phases(Phases);
    Config->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{OldId, LimbId}});
    const auto CapturedEndpoint = FTransform{FQuat{FVector::UpVector, 0.15}, FVector{13.0, 14.0, 15.0}};
    const auto CapturedBend = FVector{0.2, 0.3, 0.4};
    auto InitialEndpoints = TMap<FName, FTransform>{};
    InitialEndpoints.Add(OldId, CapturedEndpoint);
    Config->Set_InitialLimbEndpoints(InitialEndpoints);
    auto InitialBends = TMap<FName, FVector>{};
    InitialBends.Add(OldId, CapturedBend);
    Config->Set_InitialBendDirections(InitialBends);
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    const auto OldStatePreserved = [&]()
    {
        return Clip->Get_Slots().Num() >= 1 && Clip->Get_Phases().Num() == 3
            && Clip->Get_Phases()[0].Get_LimbTracks().Num() == 1
            && Config->Get_SlotBindings().Num() >= 1
            && Clip->Get_Slots()[0].Get_Id() == OldId
            && Clip->Get_Phases()[0].Get_LimbTracks()[0].Get_Slot() == OldId
            && Config->Get_SlotBindings()[0].Get_Slot() == OldId
            && Config->Get_InitialLimbEndpoints().Contains(OldId)
            && NOT Config->Get_InitialLimbEndpoints().Contains(NewId)
            && Config->Get_InitialLimbEndpoints().FindChecked(OldId).Equals(CapturedEndpoint)
            && Config->Get_InitialBendDirections().Contains(OldId)
            && NOT Config->Get_InitialBendDirections().Contains(NewId)
            && Config->Get_InitialBendDirections().FindChecked(OldId).Equals(CapturedBend);
    };
    TestFalse(TEXT("empty new ID rejects atomically"),
        Commands.RenameSlot(Config.Get(), OldId, NAME_None, Error));
    TestTrue(TEXT("empty new ID leaves clip and preview intact"), OldStatePreserved());
    TestFalse(TEXT("missing old ID rejects atomically"),
        Commands.RenameSlot(Config.Get(), FName{TEXT("Missing")}, NewId, Error));
    TestTrue(TEXT("missing old ID leaves clip and preview intact"), OldStatePreserved());
    auto DuplicateSlots = Clip->Get_Slots();
    DuplicateSlots.Add(FCk_ProceduralAction_Slot{NewId});
    Clip->Set_Slots(DuplicateSlots);
    TestFalse(TEXT("duplicate target ID rejects atomically"),
        Commands.RenameSlot(Config.Get(), OldId, NewId, Error));
    TestTrue(TEXT("duplicate target leaves old references intact"), OldStatePreserved());
    Clip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{Slot});
    Config->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{OldId, LimbId},
        FCk_ProceduralAction_SlotBinding{OldId, FName{TEXT("OtherArm")}}});
    TestFalse(TEXT("ambiguous preview bindings reject atomically"),
        Commands.RenameSlot(Config.Get(), OldId, NewId, Error));
    TestTrue(TEXT("ambiguous preview leaves old references intact"), OldStatePreserved());
    Config->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{OldId, LimbId},
        FCk_ProceduralAction_SlotBinding{NewId, FName{TEXT("OtherArm")}}});
    TestFalse(TEXT("new preview binding collision rejects atomically"),
        Commands.RenameSlot(Config.Get(), OldId, NewId, Error));
    TestTrue(TEXT("new preview binding collision leaves old references intact"),
        OldStatePreserved());
    Config->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{OldId, LimbId}});
    InitialEndpoints.Add(NewId, FTransform{FVector{99.0, 0.0, 0.0}});
    Config->Set_InitialLimbEndpoints(InitialEndpoints);
    TestFalse(TEXT("new preview endpoint key collision rejects atomically"),
        Commands.RenameSlot(Config.Get(), OldId, NewId, Error));
    TestTrue(TEXT("endpoint collision keeps clip and both preview values"),
        Clip->Get_Slots()[0].Get_Id() == OldId
            && Config->Get_SlotBindings()[0].Get_Slot() == OldId
            && Config->Get_InitialLimbEndpoints().Num() == 2
            && Config->Get_InitialLimbEndpoints().FindChecked(OldId).Equals(CapturedEndpoint)
            && Config->Get_InitialLimbEndpoints().FindChecked(NewId).GetTranslation().X == 99.0);
    InitialEndpoints.Remove(NewId);
    Config->Set_InitialLimbEndpoints(InitialEndpoints);
    InitialBends.Add(NewId, FVector::RightVector);
    Config->Set_InitialBendDirections(InitialBends);
    TestFalse(TEXT("new preview bend key collision rejects atomically"),
        Commands.RenameSlot(Config.Get(), OldId, NewId, Error));
    TestTrue(TEXT("bend collision keeps clip and both preview values"),
        Clip->Get_Slots()[0].Get_Id() == OldId
            && Config->Get_SlotBindings()[0].Get_Slot() == OldId
            && Config->Get_InitialBendDirections().Num() == 2
            && Config->Get_InitialBendDirections().FindChecked(OldId).Equals(CapturedBend)
            && Config->Get_InitialBendDirections().FindChecked(NewId).Equals(FVector::RightVector));
    InitialBends.Remove(NewId);
    Config->Set_InitialBendDirections(InitialBends);

    if (NOT TestTrue(TEXT("rename updates clip and selected preview in one transaction"),
        Commands.RenameSlot(Config.Get(), OldId, NewId, Error)))
    { AddError(Error); return false; }
    const auto RenamedStatePreserved = [&]()
    {
        if (Clip->Get_Slots().Num() != 1 || Clip->Get_Slots()[0].Get_Id() != NewId
            || Config->Get_SlotBindings().Num() != 1
            || Config->Get_SlotBindings()[0].Get_Slot() != NewId
            || Config->Get_SlotBindings()[0].Get_LimbId() != LimbId
            || Config->Get_InitialLimbEndpoints().Contains(OldId)
            || NOT Config->Get_InitialLimbEndpoints().Contains(NewId)
            || NOT Config->Get_InitialLimbEndpoints().FindChecked(NewId).Equals(CapturedEndpoint)
            || Config->Get_InitialBendDirections().Contains(OldId)
            || NOT Config->Get_InitialBendDirections().Contains(NewId)
            || NOT Config->Get_InitialBendDirections().FindChecked(NewId).Equals(CapturedBend))
        { return false; }
        const auto& RenamedSlot = Clip->Get_Slots()[0];
        if (RenamedSlot.Get_Space() != Slot.Get_Space()
            || RenamedSlot.Get_InitialSource() != Slot.Get_InitialSource())
        { return false; }
        if (Clip->Get_Phases().Num() != 3)
        { return false; }
        for (auto Index = 0; Index < 3; ++Index)
        {
            const auto& Track = Clip->Get_Phases()[Index].Get_LimbTracks()[0];
            const auto& Original = Phases[Index].Get_LimbTracks()[0];
            if (Track.Get_Slot() != NewId
                || Track.Get_Interpolation() != Original.Get_Interpolation()
                || Track.Get_ReachPolicy() != Original.Get_ReachPolicy()
                || Track.Get_PositionKeys().Num() != 1 || Track.Get_OrientationKeys().Num() != 1
                || Track.Get_BendKeys().Num() != 1
                || NOT Track.Get_PositionKeys()[0].Get_Value().Equals(Original.Get_PositionKeys()[0].Get_Value())
                || NOT Track.Get_PositionKeys()[0].Get_ArriveTangent().Equals(Original.Get_PositionKeys()[0].Get_ArriveTangent())
                || NOT Track.Get_PositionKeys()[0].Get_LeaveTangent().Equals(Original.Get_PositionKeys()[0].Get_LeaveTangent())
                || NOT Track.Get_OrientationKeys()[0].Get_Value().Equals(Original.Get_OrientationKeys()[0].Get_Value())
                || NOT Track.Get_BendKeys()[0].Get_Value().Equals(Original.Get_BendKeys()[0].Get_Value()))
            { return false; }
        }
        return true;
    };
    TestTrue(TEXT("all phases, key channels, policies and rest fields survive rename"),
        RenamedStatePreserved());
    if (NOT TestTrue(TEXT("one undo restores clip and preview identities"), GEditor->UndoTransaction()))
    { return false; }
    TestTrue(TEXT("undo restores all old references together"), OldStatePreserved());
    if (NOT TestTrue(TEXT("one redo reapplies clip and preview identities"), GEditor->RedoTransaction()))
    { return false; }
    TestTrue(TEXT("redo preserves every authored channel"), RenamedStatePreserved());
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_RenamePhaseUpdatesReferences,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.RenamePhaseUpdatesReferences",
    ck_test_procedural_action_commands::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_RenamePhaseUpdatesReferences::
    RunTest(
        const FString&)
    -> bool
{
    if (NOT TestNotNull(TEXT("editor transaction system"), GEditor))
    { return false; }
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    const auto Windup = FName{TEXT("Windup")};
    const auto Strike = FName{TEXT("Strike")};
    const auto Hit = FName{TEXT("Hit")};
    if (NOT TestTrue(TEXT("add first phase"), Commands.AddPhase(Windup, FCk_Time{0.3}, Error))
        || NOT TestTrue(TEXT("add second phase"), Commands.AddPhase(Strike, FCk_Time{0.4}, Error))
        || NOT TestTrue(TEXT("add marker in the renamed phase"), Commands.AddMarker(FName{TEXT("Impact")}, Strike, 0.5, Error))
        || NOT TestTrue(TEXT("add window ending in the renamed phase"),
            Commands.AddWindow(FName{TEXT("Danger")}, Windup, 0.5, Strike, 0.5, Error)))
    { AddError(Error); return false; }
    const auto Arm = FName{TEXT("Arm")};
    auto Pose = FCk_ProceduralAction_PoseKey{FCk_FloatRange_0to1{0.5}};
    Pose.Set_Limbs(TArray<FCk_ProceduralAction_LimbPoseTarget>{FCk_ProceduralAction_LimbPoseTarget{Arm, FVector{10.0, 0.0, 0.0}}});
    if (NOT TestTrue(TEXT("add a slot"), Commands.AddSlot(Arm, Error))
        || NOT TestTrue(TEXT("record a labelled pose in the renamed phase"), Commands.RecordPose(Strike, Pose, FName{TEXT("Reach")}, Error)))
    { AddError(Error); return false; }

    const auto ReferencesName = [&Clip, Windup](FName InPhaseId)
    {
        return Clip->Get_Phases().Num() == 2
            && Clip->Get_Phases()[0].Get_Id() == Windup
            && Clip->Get_Phases()[1].Get_Id() == InPhaseId
            && FMath::IsNearlyEqual(Clip->Get_Phases()[1].Get_Duration().Get_Seconds(), 0.4)
            && Clip->Get_Markers().Num() == 1 && Clip->Get_Markers()[0].Get_At().Get_PhaseId() == InPhaseId
            && Clip->Get_Windows().Num() == 1 && Clip->Get_Windows()[0].Get_Start().Get_PhaseId() == Windup
            && Clip->Get_Windows()[0].Get_End().Get_PhaseId() == InPhaseId
            && ck::procedural_action_editor::Get_PoseLabels(*Clip).Num() == 1
            && ck::procedural_action_editor::Get_PoseLabels(*Clip)[0].Get_PhaseId() == InPhaseId;
    };

    TestFalse(TEXT("a duplicate name is refused"), Commands.RenamePhase(Strike, Windup, Error));
    TestTrue(TEXT("the refusal names the conflict"), Error.Contains(Windup.ToString()));
    TestTrue(TEXT("a refused duplicate changes nothing"), ReferencesName(Strike));
    TestFalse(TEXT("an unknown section is refused"), Commands.RenamePhase(FName{TEXT("Missing")}, Hit, Error));
    TestFalse(TEXT("an empty name is refused"), Commands.RenamePhase(Strike, NAME_None, Error));
    TestTrue(TEXT("refused renames change nothing"), ReferencesName(Strike));

    if (NOT TestTrue(TEXT("rename the referenced phase"), Commands.RenamePhase(Strike, Hit, Error)))
    { AddError(Error); return false; }
    TestTrue(TEXT("the phase, marker, window edge and pose group follow the rename"), ReferencesName(Hit));
    if (NOT TestTrue(TEXT("undo the rename"), GEditor->UndoTransaction()))
    { return false; }
    TestTrue(TEXT("undo restores every reference to the old name"), ReferencesName(Strike));
    return NOT HasAnyErrors();
}

#endif
