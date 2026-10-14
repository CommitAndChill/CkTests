#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/ActionBuilder/CkProceduralActionBuilder_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringCommands.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringSession.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPoseGroups.h"

#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
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
    { return FCk_ProceduralAction_PoseGroup::Get_BodyChannelName(); }

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
        Participants()
        -> TArray<FName>
    { return TArray<FName>{BodyChannel(), Arm}; }

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
        FindGroup(
            const UCk_ProceduralAction_Data& InClip,
            const FGuid& InId)
        -> const FCk_ProceduralAction_PoseGroup*
    {
        return InClip.Get_EditorPoseGroups().FindByPredicate([&InId](const auto& InGroup)
            { return InGroup.Get_Id() == InId; });
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PoseCapture_CaptureUpdateDuplicateMoveDelete,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.PoseCapture.CaptureUpdateDuplicateMoveDelete",
    ck_test_procedural_action_pose_capture::TestFlags)

auto
    FCkTest_ProceduralAction_PoseCapture_CaptureUpdateDuplicateMoveDelete::
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
    auto PoseId = FGuid{};
    if (NOT TestTrue(TEXT("capture a whole pose at an empty time"),
        Commands.CapturePose(Motion, MakePose(0.5, FVector{0.0, 0.0, 5.0}, FVector{15.0, 0.0, 0.0}),
            Participants(), FName{TEXT("Raised")}, PoseId, Error)))
    { AddInfo(Error); return false; }
    TestTrue(TEXT("capture returns a valid pose ID"), PoseId.IsValid());
    TestTrue(TEXT("capture keyed the body"), HasBodyKeyAt(*Clip, 0.5));
    auto Value = FVector::ZeroVector;
    TestTrue(TEXT("capture keyed the arm"), PositionAt(*Clip, Arm, 0.5, Value) && Value.Equals(FVector{15.0, 0.0, 0.0}));
    TestFalse(TEXT("unselected leg stays under ordinary control"), PositionAt(*Clip, Leg, 0.5, Value));
    TestEqual(TEXT("unrelated leg keys untouched"), FindTrack(*Clip, Leg)->Get_PositionKeys().Num(), 2);
    TestEqual(TEXT("marker untouched"), Clip->Get_Markers().Num(), 1);
    const auto* Group = FindGroup(*Clip, PoseId);
    if (NOT TestNotNull(TEXT("capture recorded editor pose metadata"), Group))
    { return false; }
    TestEqual(TEXT("group phase"), Group->Get_PhaseId(), Motion);
    TestEqual(TEXT("group time"), Group->Get_Time().Get_Value(), 0.5);
    TestEqual(TEXT("group channels are the captured participants"), Group->Get_Channels().Num(), 2);
    TestEqual(TEXT("requested capture provenance"), Group->Get_Provenance(), ECk_ProceduralAction_PoseProvenance::Requested);

    const auto PhasesAfterCapture = Clip->Get_Phases();
    auto Ignored = FGuid{};
    TestFalse(TEXT("capturing at an occupied time is refused"),
        Commands.CapturePose(Motion, MakePose(0.5, FVector::ZeroVector, FVector::ZeroVector),
            Participants(), NAME_None, Ignored, Error));
    TestTrue(TEXT("occupied refusal names Update Pose"), Error.Contains(TEXT("Update")));
    TestTrue(TEXT("occupied refusal writes nothing"), SamePhases(*Clip, PhasesAfterCapture));
    TestFalse(TEXT("a missing required participant is refused"),
        Commands.CapturePose(Motion, MakePose(0.25, FVector::ZeroVector, FVector::ZeroVector),
            TArray<FName>{BodyChannel(), Arm, Leg}, NAME_None, Ignored, Error));
    TestTrue(TEXT("missing participant refusal names the channel"), Error.Contains(Leg.ToString()));
    TestTrue(TEXT("missing participant refusal writes nothing"), SamePhases(*Clip, PhasesAfterCapture));

    if (NOT TestTrue(TEXT("update the captured pose"),
        Commands.UpdatePose(PoseId, MakePose(0.5, FVector{0.0, 0.0, 6.0}, FVector{20.0, 0.0, 0.0}), Error)))
    { AddInfo(Error); return false; }
    TestTrue(TEXT("update replaced the arm value"), PositionAt(*Clip, Arm, 0.5, Value) && Value.Equals(FVector{20.0, 0.0, 0.0}));
    TestNotNull(TEXT("update keeps the pose ID"), FindGroup(*Clip, PoseId));

    auto CopyId = FGuid{};
    if (NOT TestTrue(TEXT("duplicate at another time"), Commands.DuplicatePose(PoseId, Motion, 0.25, CopyId, Error)))
    { AddInfo(Error); return false; }
    TestTrue(TEXT("duplicate has its own ID"), CopyId.IsValid() && CopyId != PoseId);
    TestTrue(TEXT("duplicate copied the arm value"), PositionAt(*Clip, Arm, 0.25, Value) && Value.Equals(FVector{20.0, 0.0, 0.0}));
    TestTrue(TEXT("duplicate copied the body"), HasBodyKeyAt(*Clip, 0.25));

    if (NOT TestTrue(TEXT("move the duplicate"), Commands.MovePose(CopyId, Motion, 0.75, Error)))
    { AddInfo(Error); return false; }
    TestFalse(TEXT("move removed the arm key at the old time"), PositionAt(*Clip, Arm, 0.25, Value));
    TestFalse(TEXT("move removed the body key at the old time"), HasBodyKeyAt(*Clip, 0.25));
    TestTrue(TEXT("move wrote the arm key at the new time"), PositionAt(*Clip, Arm, 0.75, Value) && Value.Equals(FVector{20.0, 0.0, 0.0}));
    const auto* Moved = FindGroup(*Clip, CopyId);
    TestTrue(TEXT("move keeps the ID and updates its time"), Moved != nullptr && Moved->Get_Time().Get_Value() == 0.75);

    if (NOT TestTrue(TEXT("delete the moved pose"), Commands.DeletePose(CopyId, Error)))
    { AddInfo(Error); return false; }
    TestFalse(TEXT("delete removed its arm key"), PositionAt(*Clip, Arm, 0.75, Value));
    TestFalse(TEXT("delete removed its body key"), HasBodyKeyAt(*Clip, 0.75));
    TestNull(TEXT("delete removed its metadata"), FindGroup(*Clip, CopyId));
    TestNotNull(TEXT("the first pose keeps its ID throughout"), FindGroup(*Clip, PoseId));
    TestEqual(TEXT("leg keys untouched by every pose command"), FindTrack(*Clip, Leg)->Get_PositionKeys().Num(), 2);

    auto Groups = TArray<FCk_ProceduralActionPoseGroupView>{};
    ck::procedural_action_editor::DerivePoseGroups(*Clip, Groups);
    const auto* Start = Groups.FindByPredicate([](const auto& InView)
        { return InView.Get_Group().Get_Time().Get_Value() == 0.0; });
    if (NOT TestNotNull(TEXT("the endpoint keys form a derived group"), Start))
    { return false; }
    if (NOT TestTrue(TEXT("moving the phase-start pose is written even though no key is left at 0"),
        Commands.MovePose(Start->Get_Group().Get_Id(), Motion, 0.1, Error)))
    { AddInfo(Error); return false; }
    auto Compiled = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    TestTrue(TEXT("the moved clip compiles: its tracks now start from the pose the phase begins with"),
        ck::procedural_action::Compile(*Clip, Compiled, Diagnostics));
    TestNotNull(TEXT("the moved pose keeps its ID"), FindGroup(*Clip, Start->Get_Group().Get_Id()));
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PoseCapture_UndoRedoRestoresTracksAndGroups,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.PoseCapture.UndoRedoRestoresTracksAndGroups",
    ck_test_procedural_action_pose_capture::TestFlags)

auto
    FCkTest_ProceduralAction_PoseCapture_UndoRedoRestoresTracksAndGroups::
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
    auto PoseId = FGuid{};
    if (NOT TestTrue(TEXT("capture"), Commands.CapturePose(Motion, MakePose(0.5, FVector::ZeroVector,
            FVector{15.0, 0.0, 0.0}), Participants(), FName{TEXT("Raised")}, PoseId, Error))
        || NOT TestTrue(TEXT("update"), Commands.UpdatePose(PoseId, MakePose(0.5, FVector::ZeroVector,
            FVector{25.0, 0.0, 0.0}), Error)))
    { AddInfo(Error); return false; }

    auto Value = FVector::ZeroVector;
    if (NOT TestTrue(TEXT("undo the update"), GEditor->UndoTransaction()))
    { return false; }
    TestTrue(TEXT("undo restored the captured value"), PositionAt(*Clip, Arm, 0.5, Value) && Value.Equals(FVector{15.0, 0.0, 0.0}));
    TestNotNull(TEXT("undo of an update keeps the metadata"), FindGroup(*Clip, PoseId));
    if (NOT TestTrue(TEXT("undo the capture"), GEditor->UndoTransaction()))
    { return false; }
    TestFalse(TEXT("undo removed the captured arm key"), PositionAt(*Clip, Arm, 0.5, Value));
    TestFalse(TEXT("undo removed the captured body key"), HasBodyKeyAt(*Clip, 0.5));
    TestTrue(TEXT("undo removed the metadata in the same step"), Clip->Get_EditorPoseGroups().IsEmpty());
    if (NOT TestTrue(TEXT("redo the capture"), GEditor->RedoTransaction()))
    { return false; }
    TestTrue(TEXT("redo restored the arm key"), PositionAt(*Clip, Arm, 0.5, Value) && Value.Equals(FVector{15.0, 0.0, 0.0}));
    TestNotNull(TEXT("redo restored the same pose ID"), FindGroup(*Clip, PoseId));
    if (NOT TestTrue(TEXT("redo the update"), GEditor->RedoTransaction()))
    { return false; }
    TestTrue(TEXT("redo restored the updated value"), PositionAt(*Clip, Arm, 0.5, Value) && Value.Equals(FVector{25.0, 0.0, 0.0}));
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PoseCapture_SaveReopenKeepsIds,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.PoseCapture.SaveReopenKeepsIds",
    ck_test_procedural_action_pose_capture::TestFlags)

auto
    FCkTest_ProceduralAction_PoseCapture_SaveReopenKeepsIds::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_pose_capture;
    if (NOT TestTrue(TEXT("editor transaction system is idle"), GEditor != nullptr && GEditor->Trans != nullptr
        && NOT GEditor->Trans->IsActive() && NOT GEditor->IsPlaySessionInProgress()))
    { return false; }

    const auto Unique = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const auto MountRoot = FString::Printf(TEXT("/CkActionPoseGroups_%s/"), *Unique);
    const auto MountDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ProceduralAction"),
        TEXT("PoseGroups"), Unique) + TEXT("/");
    const auto PackageName = MountRoot + TEXT("Clip");
    const auto ObjectPath = PackageName + TEXT(".Clip");
    if (NOT TestTrue(TEXT("disposable directory"), IFileManager::Get().MakeDirectory(*MountDirectory, true)))
    { return false; }
    FPackageName::RegisterMountPoint(MountRoot, MountDirectory);
    const auto PackageFile = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

    // A private transaction buffer, so unloading the disposable package never resets the editor's undo history.
    auto PreviousTransactions = TStrongObjectPtr<UTransactor>{GEditor->Trans.Get()};
    auto PrivateTransactions = TStrongObjectPtr<UTransBuffer>{NewObject<UTransBuffer>()};
    PrivateTransactions->Initialize(8 * 1024 * 1024);
    GEditor->Trans = PrivateTransactions.Get();
    ON_SCOPE_EXIT
    {
        if (GEditor->Trans.Get() == PrivateTransactions.Get())
        { GEditor->Trans = PreviousTransactions.Get(); }
        if (auto* Package = FindPackage(nullptr, *PackageName))
        {
            auto Unload = UPackageTools::FUnloadPackageParams{TArray<UPackage*>{Package}};
            Unload.bResetTransBuffer = false;
            UPackageTools::UnloadPackages(Unload);
        }
        CollectGarbage(RF_NoFlags);
        FPackageName::UnRegisterMountPoint(MountRoot, MountDirectory);
        IFileManager::Get().Delete(*PackageFile);
        IFileManager::Get().DeleteDirectory(*MountDirectory, false, true);
    };

    auto SavedId = FGuid{};
    {
        auto* Package = CreatePackage(*PackageName);
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{NewObject<UCk_ProceduralAction_Data>(
            Package, TEXT("Clip"), RF_Public | RF_Standalone | RF_Transactional)};
        if (NOT TestTrue(TEXT("fixture clip compiles"), FillClip(*Clip)))
        { return false; }
        auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
        auto Error = FString{};
        if (NOT TestTrue(TEXT("capture before save"), Commands.CapturePose(Motion, MakePose(0.5, FVector::ZeroVector,
            FVector{15.0, 0.0, 0.0}), Participants(), FName{TEXT("Raised")}, SavedId, Error)))
        { AddInfo(Error); return false; }
        Package->MarkPackageDirty();
        auto SaveArgs = FSavePackageArgs{};
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        SaveArgs.Error = GLog;
        if (NOT TestTrue(TEXT("save the disposable clip"), UPackage::SavePackage(Package, Clip.Get(), *PackageFile, SaveArgs)))
        { return false; }
    }

    PrivateTransactions->Reset(FText::FromString(TEXT("Discard disposable pose group test transactions")));
    {
        auto* Package = FindPackage(nullptr, *PackageName);
        auto Unload = UPackageTools::FUnloadPackageParams{TArray<UPackage*>{Package}};
        Unload.bResetTransBuffer = false;
        if (NOT TestTrue(TEXT("unload the disposable clip"), Package != nullptr && UPackageTools::UnloadPackages(Unload)))
        { return false; }
        CollectGarbage(RF_NoFlags);
    }
    if (NOT TestNull(TEXT("the clip left memory before reopening"), FindObject<UCk_ProceduralAction_Data>(nullptr, *ObjectPath)))
    { return false; }

    auto* Reopened = LoadObject<UCk_ProceduralAction_Data>(nullptr, *ObjectPath);
    if (NOT TestNotNull(TEXT("reopen the saved clip"), Reopened))
    { return false; }
    const auto* Group = FindGroup(*Reopened, SavedId);
    if (NOT TestNotNull(TEXT("the pose ID survived save and reopen"), Group))
    { return false; }
    TestEqual(TEXT("label survived"), Group->Get_Label(), FName{TEXT("Raised")});
    TestEqual(TEXT("time survived"), Group->Get_Time().Get_Value(), 0.5);
    TestEqual(TEXT("channels survived"), Group->Get_Channels().Num(), 2);
    auto Value = FVector::ZeroVector;
    TestTrue(TEXT("the grouped key survived"), PositionAt(*Reopened, Arm, 0.5, Value) && Value.Equals(FVector{15.0, 0.0, 0.0}));
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PoseCapture_OldClipDerivesTemporaryGroups,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.PoseCapture.OldClipDerivesTemporaryGroups",
    ck_test_procedural_action_pose_capture::TestFlags)

auto
    FCkTest_ProceduralAction_PoseCapture_OldClipDerivesTemporaryGroups::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_pose_capture;
    auto Clip = MakeTransientClip();
    if (NOT TestTrue(TEXT("fixture clip compiles"), FillClip(*Clip)))
    { return false; }
    const auto PhasesBeforeOpen = Clip->Get_Phases();
    const auto First = FCk_ProceduralActionAuthoringSession{Clip.Get()};
    const auto Second = FCk_ProceduralActionAuthoringSession{Clip.Get()};
    TestTrue(TEXT("opening an old clip writes no metadata"), Clip->Get_EditorPoseGroups().IsEmpty());
    TestTrue(TEXT("opening an old clip writes no keys"), SamePhases(*Clip, PhasesBeforeOpen));
    TestEqual(TEXT("one temporary group per keyed time"), First.GetPoseGroups().Num(), 2);
    for (const auto& View : First.GetPoseGroups())
    {
        TestFalse(TEXT("derived groups are temporary"), View.Get_Persisted());
        TestFalse(TEXT("derived groups are whole"), View.Get_IsBroken());
        TestEqual(TEXT("derived group covers body, arm and leg"), View.Get_Group().Get_Channels().Num(), 3);
        TestNotNull(TEXT("derived IDs are stable across sessions"), Second.FindPoseGroup(View.Get_Group().Get_Id()));
    }

    const auto& End = First.GetPoseGroups().Last();
    auto Pose = FCk_ProceduralAction_PoseKey{};
    auto Error = FString{};
    if (NOT TestTrue(TEXT("read the derived pose back from its keys"),
        ck::procedural_action_editor::ReadPoseKey(Clip->Get_Phases(), End.Get_Group(), Pose, Error)))
    { AddInfo(Error); return false; }
    auto Limbs = Pose.Get_Limbs();
    Limbs[0].Set_Position(Limbs[0].Get_Position() + FVector{0.0, 0.0, 1.0});
    Pose.Set_Limbs(Limbs);
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    if (NOT TestTrue(TEXT("an explicit edit of a temporary group"), Commands.UpdatePose(End.Get_Group().Get_Id(), Pose, Error)))
    { AddInfo(Error); return false; }
    TestNotNull(TEXT("the edited group is persisted under its derived ID"), FindGroup(*Clip, End.Get_Group().Get_Id()));
    TestEqual(TEXT("only the edited group is persisted"), Clip->Get_EditorPoseGroups().Num(), 1);

    auto Phases = Clip->Get_Phases();
    auto Tracks = Phases[0].Get_LimbTracks();
    auto* LegTrack = Tracks.FindByPredicate([](const auto& InTrack) { return InTrack.Get_Slot() == Leg; });
    auto LegKeys = LegTrack->Get_PositionKeys();
    LegKeys.Last().Set_Time(FCk_FloatRange_0to1{0.9});
    LegTrack->Set_PositionKeys(LegKeys);
    Phases[0].Set_LimbTracks(Tracks);
    Clip->Set_Phases(Phases);
    auto Refreshed = FCk_ProceduralActionAuthoringSession{Clip.Get()};
    const auto* Broken = Refreshed.FindPoseGroup(End.Get_Group().Get_Id());
    if (NOT TestNotNull(TEXT("an advanced track edit keeps the persisted group listed"), Broken))
    { return false; }
    TestTrue(TEXT("the group whose leg key moved is reported broken"), Broken->Get_IsBroken());
    TestTrue(TEXT("the broken diagnostic names the channel"), Broken->Get_BrokenReason().Contains(Leg.ToString()));
    TestFalse(TEXT("a broken group cannot be updated"), Commands.UpdatePose(End.Get_Group().Get_Id(), Pose, Error));
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PoseCapture_StagingIsSessionOwned,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.PoseCapture.StagingIsSessionOwned",
    ck_test_procedural_action_pose_capture::TestFlags)

auto
    FCkTest_ProceduralAction_PoseCapture_StagingIsSessionOwned::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_pose_capture;
    auto Clip = MakeTransientClip();
    if (NOT TestTrue(TEXT("fixture clip compiles"), FillClip(*Clip)))
    { return false; }
    const auto PhasesBefore = Clip->Get_Phases();
    auto Session = FCk_ProceduralActionAuthoringSession{Clip.Get()};
    Session.SetParticipants(Participants());
    Session.SetStagingTime(Motion, FCk_FloatRange_0to1{0.5});
    Session.SetStagingBody(FTransform{FVector{0.0, 0.0, 4.0}});
    Session.SetStagingLimb(FCk_ProceduralAction_LimbPoseTarget{Arm, FVector{12.0, 0.0, 0.0}});
    TestTrue(TEXT("staging edits never write the clip"), SamePhases(*Clip, PhasesBefore));
    TestEqual(TEXT("staging pose holds the arm target"), Session.GetStagingPose().Get_Limbs().Num(), 1);

    Session.SetParticipants(TArray<FName>{Arm});
    TestEqual(TEXT("dropping the body participant releases the staged body"),
        Session.GetStagingPose().Get_BodyMode(), ECk_ProceduralAction_ValueMode::Keep);
    Session.SetParticipants(Participants());
    Session.SetStagingBody(FTransform{FVector{0.0, 0.0, 4.0}});

    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    auto PoseId = FGuid{};
    if (NOT TestTrue(TEXT("capture the staged pose explicitly"), Commands.CapturePose(Session.GetStagingPhaseId(),
        Session.GetStagingPose(), Session.GetParticipants(), NAME_None, PoseId, Error)))
    { AddInfo(Error); return false; }
    Session.Refresh();
    Session.SetStagingLimb(FCk_ProceduralAction_LimbPoseTarget{Arm, FVector::ZeroVector});
    if (NOT TestTrue(TEXT("load staging from the captured group"), Session.LoadStagingFromPoseGroup(PoseId, Error)))
    { AddInfo(Error); return false; }
    TestTrue(TEXT("loaded staging carries the key value, not an interpolated sample"),
        Session.GetStagingPose().Get_Limbs().Num() == 1
        && Session.GetStagingPose().Get_Limbs()[0].Get_Position().Equals(FVector{12.0, 0.0, 0.0}));
    TestEqual(TEXT("loaded staging carries the group time"), Session.GetStagingPose().Get_Time().Get_Value(), 0.5);
    TestEqual(TEXT("loaded staging carries the group channels as participants"), Session.GetParticipants().Num(), 2);
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
    TestFalse(TEXT("recipe keys are persisted as named poses"), EditorClip->Get_EditorPoseGroups().IsEmpty());
    for (const auto& Group : EditorClip->Get_EditorPoseGroups())
    { TestTrue(TEXT("recipe pose groups name the body"), Group.Get_Channels().Contains(BodyChannel())); }

    TestFalse(TEXT("a recipe never overwrites existing content"), Commands.CreateFromRecipe(Recipe, Error));
    if (NOT TestTrue(TEXT("undo the recipe creation"), GEditor->UndoTransaction()))
    { return false; }
    TestTrue(TEXT("undo restores the empty clip"), EditorClip->Get_Phases().IsEmpty() && EditorClip->Get_EditorPoseGroups().IsEmpty());
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
