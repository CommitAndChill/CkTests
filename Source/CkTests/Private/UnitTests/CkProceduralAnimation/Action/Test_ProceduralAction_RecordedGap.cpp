#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Action/CkProceduralAction_RecordedStore.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_RecordedGap,
    "Ck.ProceduralAnimation.Action.Receipts.CopiedEmptyGapResumes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkTest_ProceduralAction_RecordedGap::
    RunTest(
        const FString&)
    -> bool
{
    const auto Identity = FCk_ProceduralAction_Identity{17};
    auto Store = FCk_ProceduralActionRecordedStore{};
    Store.Reset(Identity, {});
    const auto EmptyGap = FCk_ProceduralAction_ReadPage{
        ECk_ProceduralAction_ReadResult::Gap, {}, 3, false, false, 1, 3};
    if (NOT TestTrue(TEXT("consumed-only page is observable"), Store.AppendPage(EmptyGap))
        || NOT TestEqual(TEXT("empty gap advances cursor"), Store.Get_LastCopiedSequence(), int64{3})
        || NOT TestEqual(TEXT("one missing range is retained"), Store.Get_Gaps().Num(), 1))
    { return false; }

    const auto FurtherGap = FCk_ProceduralAction_ReadPage{
        ECk_ProceduralAction_ReadResult::Gap, {}, 7, false, false, 4, 7};
    if (NOT TestTrue(TEXT("adjacent consumed-only pages resume"), Store.AppendPage(FurtherGap))
        || NOT TestEqual(TEXT("adjacent gaps merge to bound storage"), Store.Get_Gaps().Num(), 1)
        || NOT TestEqual(TEXT("merged missing range ends at seven"), Store.Get_Gaps()[0].Get_Last(), int64{7}))
    { return false; }

    const auto Record = FCk_ProceduralAction_Record{Identity, 8,
        ECk_ProceduralAction_RecordKind::WindowsClosed, {}, {}, {}};
    const auto Resumed = FCk_ProceduralAction_ReadPage{
        ECk_ProceduralAction_ReadResult::Pending, {Record}, 8, false, false};
    if (NOT TestTrue(TEXT("later retained record can be copied"), Store.AppendPage(Resumed))
        || NOT TestEqual(TEXT("copy contains only observed record"), Store.Get_Records().Num(), 1)
        || NOT TestEqual(TEXT("resumed cursor is exact"), Store.Get_LastCopiedSequence(), int64{8}))
    { return false; }

    const auto Invalid = FCk_ProceduralAction_ReadPage{
        ECk_ProceduralAction_ReadResult::Gap, {}, 9, false, false, 10, 9};
    return TestFalse(TEXT("reversed missing range rejects atomically"), Store.AppendPage(Invalid))
        && TestEqual(TEXT("rejected page preserves cursor"), Store.Get_LastCopiedSequence(), int64{8})
        && TestEqual(TEXT("rejected page preserves evidence"), Store.Get_Records().Num(), 1);
}

#endif
