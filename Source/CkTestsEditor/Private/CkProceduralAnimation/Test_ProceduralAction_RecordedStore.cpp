#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimationEditor/CkProceduralActionRecordedStore.h"

namespace ck_test_procedural_action_recorded_store
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    auto
        MakeRecord(
            FCk_ProceduralAction_Identity InIdentity,
            int64 InSequence)
        -> FCk_ProceduralAction_Record
    {
        return FCk_ProceduralAction_Record{InIdentity, InSequence,
            ECk_ProceduralAction_RecordKind::AcceptedPose, {}, {}, {}};
    }

    auto
        MakePoseRecord(
            FCk_ProceduralAction_Identity InIdentity,
            int64 InSequence)
        -> FCk_ProceduralAction_Record
    {
        const auto Limb = FCk_ProceduralArticulation_LimbPose{
            TEXT("Front"), FTransform::Identity, FTransform::Identity,
            TArray<FTransform>{FTransform::Identity, FTransform::Identity, FTransform::Identity},
            0.0f, ECk_ProceduralRig_ChainState::Clear,
            ECk_ProceduralArticulation_ContactEvidence::None};
        const auto Pose = FCk_ProceduralArticulation_Pose{
            FTransform::Identity, FTransform::Identity, false,
            TArray<FCk_ProceduralArticulation_LimbPose>{Limb}};
        const auto Sample = FCk_ProceduralAction_Sample{
            {}, Pose, ECk_ProceduralAction_SampleEvidence::Evaluated};
        const auto Receipt = FCk_ProceduralAction_Receipt{
            true, {}, {}, Pose, Pose, TArray<FCk_ProceduralAction_Sample>{Sample}, {}, 0};
        return FCk_ProceduralAction_Record{InIdentity, InSequence,
            ECk_ProceduralAction_RecordKind::AcceptedPose, Receipt, {}, {}};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_RecordedStore_CopiedPagesAndGaps,
    "Ck.ProceduralAnimation.Action.Editor.Recorded.CopiedPagesAndGaps",
    ck_test_procedural_action_recorded_store::TestFlags)

auto
    FCkTest_ProceduralAction_RecordedStore_CopiedPagesAndGaps::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_recorded_store;
    const auto Identity = FCk_ProceduralAction_Identity{42};
    auto Store = FCk_ProceduralActionRecordedStore{};
    Store.Reset(Identity, {TEXT("Fixture_A"), TEXT("/Clip_A"), TEXT("Input_A"), FCk_Time{1.0 / 60.0}, 7});
    auto First = FCk_ProceduralAction_ReadPage{ECk_ProceduralAction_ReadResult::Succeeded,
        TArray<FCk_ProceduralAction_Record>{MakeRecord(Identity, 1), MakeRecord(Identity, 3)},
        3, false, true};
    if (NOT TestTrue(TEXT("first page copied"), Store.AppendPage(First)))
    { return false; }
    First = {};
    if (NOT TestEqual(TEXT("records survive source page release"), Store.Get_Records().Num(), 2)
        || NOT TestEqual(TEXT("missing sequence remains visible"), Store.Get_Gaps().Num(), 1)
        || NOT TestEqual(TEXT("gap first"), Store.Get_Gaps()[0].Get_First(), int64{2})
        || NOT TestEqual(TEXT("gap last"), Store.Get_Gaps()[0].Get_Last(), int64{2}))
    { return false; }

    const auto Second = FCk_ProceduralAction_ReadPage{ECk_ProceduralAction_ReadResult::Succeeded,
        TArray<FCk_ProceduralAction_Record>{MakeRecord(Identity, 3), MakeRecord(Identity, 4)},
        4, true, false};
    if (NOT TestTrue(TEXT("overlapping page copied idempotently"), Store.AppendPage(Second))
        || NOT TestEqual(TEXT("duplicate does not appear twice"), Store.Get_Records().Num(), 3)
        || NOT TestTrue(TEXT("terminal state copied"), Store.Get_Terminal())
        || NOT TestEqual(TEXT("source fixture retained"), Store.Get_Provenance().Get_Fixture(), FString{TEXT("Fixture_A")}))
    { return false; }

    const auto Wrong = FCk_ProceduralAction_ReadPage{ECk_ProceduralAction_ReadResult::Succeeded,
        TArray<FCk_ProceduralAction_Record>{MakeRecord(FCk_ProceduralAction_Identity{99}, 5)},
        5, false, false};
    if (NOT TestFalse(TEXT("another identity cannot contaminate this recording"), Store.AppendPage(Wrong))
        || NOT TestEqual(TEXT("rejected page leaves copy unchanged"), Store.Get_Records().Num(), 3))
    { return false; }

    auto BudgetRecords = TArray<FCk_ProceduralAction_Record>{};
    BudgetRecords.Reserve(FCk_ProceduralActionRecordedStore::MaximumCopiedRecords - 3);
    for (auto Sequence = int64{5};
        Sequence <= FCk_ProceduralActionRecordedStore::MaximumCopiedRecords + 1; ++Sequence)
    { BudgetRecords.Add(MakeRecord(Identity, Sequence)); }
    const auto BudgetPage = FCk_ProceduralAction_ReadPage{
        ECk_ProceduralAction_ReadResult::Succeeded, MoveTemp(BudgetRecords),
        FCk_ProceduralActionRecordedStore::MaximumCopiedRecords + 1, true, false};
    if (NOT TestTrue(TEXT("recording accepts its finite budget"), Store.AppendPage(BudgetPage))
        || NOT TestEqual(TEXT("recording reaches its finite budget"), Store.Get_Records().Num(),
            FCk_ProceduralActionRecordedStore::MaximumCopiedRecords))
    { return false; }
    const auto BeyondBudget = FCk_ProceduralAction_ReadPage{
        ECk_ProceduralAction_ReadResult::Succeeded,
        TArray<FCk_ProceduralAction_Record>{
            MakeRecord(Identity, FCk_ProceduralActionRecordedStore::MaximumCopiedRecords + 2)},
        FCk_ProceduralActionRecordedStore::MaximumCopiedRecords + 2, true, false};
    return TestFalse(TEXT("budget excess is diagnosed instead of silently discarded"),
            Store.AppendPage(BeyondBudget))
        && TestEqual(TEXT("budget rejection preserves copied records"), Store.Get_Records().Num(),
            FCk_ProceduralActionRecordedStore::MaximumCopiedRecords)
        && TestFalse(TEXT("budget rejection carries a diagnostic"), Store.Get_LastError().IsEmpty());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_RecordedStore_PoseTransformBudget,
    "Ck.ProceduralAnimation.Action.Editor.Recorded.PoseTransformBudget",
    ck_test_procedural_action_recorded_store::TestFlags)

auto
    FCkTest_ProceduralAction_RecordedStore_PoseTransformBudget::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_recorded_store;
    const auto Identity = FCk_ProceduralAction_Identity{43};
    auto Store = FCk_ProceduralActionRecordedStore{18};
    Store.Reset(Identity, {});
    const auto First = FCk_ProceduralAction_ReadPage{
        ECk_ProceduralAction_ReadResult::Succeeded,
        TArray<FCk_ProceduralAction_Record>{MakePoseRecord(Identity, 1)},
        1, false, false};
    if (NOT TestTrue(TEXT("one pose receipt fits the transform budget"), Store.AppendPage(First))
        || NOT TestEqual(TEXT("previous, current, and sample poses counted"),
            Store.Get_CopiedPoseTransforms(), int64{18}))
    { return false; }

    if (NOT TestTrue(TEXT("overlapping page does not count transforms again"), Store.AppendPage(First))
        || NOT TestEqual(TEXT("overlap preserves transform count"),
            Store.Get_CopiedPoseTransforms(), int64{18}))
    { return false; }

    const auto Second = FCk_ProceduralAction_ReadPage{
        ECk_ProceduralAction_ReadResult::Succeeded,
        TArray<FCk_ProceduralAction_Record>{MakePoseRecord(Identity, 2)},
        2, false, false};
    if (NOT TestFalse(TEXT("second receipt exceeds the transform budget"), Store.AppendPage(Second))
        || NOT TestEqual(TEXT("rejection preserves copied cursor"), Store.Get_LastCopiedSequence(), int64{1})
        || NOT TestEqual(TEXT("rejection preserves copied records"), Store.Get_Records().Num(), 1)
        || NOT TestEqual(TEXT("rejection preserves transform count"),
            Store.Get_CopiedPoseTransforms(), int64{18})
        || NOT TestTrue(TEXT("budget diagnostic states the limit"),
            Store.Get_LastError().Contains(TEXT("18-pose-transform budget"))))
    { return false; }

    Store.Reset(Identity, {});
    return TestEqual(TEXT("reset releases copied transform budget"),
        Store.Get_CopiedPoseTransforms(), int64{0});
}

#endif
