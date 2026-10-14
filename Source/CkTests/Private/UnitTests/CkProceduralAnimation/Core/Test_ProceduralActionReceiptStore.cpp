#include "CkProceduralAnimation/Core/CkProceduralActionReceiptStore.h"

#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace ck_test_procedural_action_receipts
{
    auto
    MakeAccepted(
        FCk_ProceduralAction_Identity InIdentity,
        int64 InSampleSequence)
        -> FCk_ProceduralAction_Receipt
    {
        const auto Stamp = FCk_ProceduralAction_Stamp{InIdentity, 1, InSampleSequence, 1, 1, 1};
        const auto Interval = FCk_ProceduralAction_Interval{FCk_Time{0.0}, FCk_Time{0.1},
            FName{TEXT("Reach")}, InSampleSequence == 1
                ? ECk_ProceduralAction_Continuity::FirstSample
                : ECk_ProceduralAction_Continuity::Continuous, {}, {}};
        const auto Pose = FCk_ProceduralArticulation_Pose{FTransform::Identity,
            FTransform::Identity, true, {}};
        const auto Sampling = FCk_ProceduralAction_SamplingReport{1, 1, 0.0f, 0.0f, FCk_Time{0.0}};
        return FCk_ProceduralAction_Receipt{true, Stamp, Interval, Pose, Pose, Sampling};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionReceiptSamplingReportTest,
    "Ck.ProceduralAnimation.Action.ReceiptStoreRejectsMalformedSamplingReport",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionReceiptSamplingReportTest::RunTest(
    const FString&)
    -> bool
{
    auto Store = ck::procedural_action::FReceiptStore{};
    if (NOT TestTrue(TEXT("Capacity configured"), Store.Configure(FCk_ProceduralAction_Capacity{})))
    { return false; }
    auto Identity = FCk_ProceduralAction_Identity{};
    if (NOT TestTrue(TEXT("Body stream reserved"), Store.Reserve({}, true, Identity)
        == ck::procedural_action::EReceiptStoreWrite::Accepted))
    { return false; }

    const auto Base = ck_test_procedural_action_receipts::MakeAccepted(Identity, 1);
    const auto Underreported = FCk_ProceduralAction_Receipt{true, Base.Get_Stamp(), Base.Get_Interval(),
        Base.Get_PreviousPose(), Base.Get_CurrentPose(),
        FCk_ProceduralAction_SamplingReport{0, 1, 0.0f, 0.0f, FCk_Time{}}};
    TestTrue(TEXT("Evaluated count must cover the settled sample"),
        Store.PublishAccepted(Identity, Underreported) == ck::procedural_action::EReceiptStoreWrite::InvalidRecord);
    const auto NoAccepted = FCk_ProceduralAction_Receipt{true, Base.Get_Stamp(), Base.Get_Interval(),
        Base.Get_PreviousPose(), Base.Get_CurrentPose(),
        FCk_ProceduralAction_SamplingReport{1, 0, 0.0f, 0.0f, FCk_Time{}}};
    TestTrue(TEXT("Accepted record requires one settled sample"), Store.PublishAccepted(Identity, NoAccepted)
        == ck::procedural_action::EReceiptStoreWrite::InvalidRecord);
    TestTrue(TEXT("Rejected records did not consume sequence or emit closure"),
        Store.Read(Identity, 0, 3).Get_Records().IsEmpty());

    // An entry blend may evaluate several candidates inside one zero-length interval.
    const auto ZeroInterval = FCk_ProceduralAction_Interval{FCk_Time{}, FCk_Time{},
        FName{TEXT("EntryBlend")}, ECk_ProceduralAction_Continuity::FirstSample, {}, {}};
    const auto ZeroLength = FCk_ProceduralAction_Receipt{true, Base.Get_Stamp(), ZeroInterval,
        Base.Get_PreviousPose(), Base.Get_CurrentPose(),
        FCk_ProceduralAction_SamplingReport{3, 1, 0.0f, 0.0f, FCk_Time{}}};
    TestTrue(TEXT("A zero-length blend interval with several evaluated candidates remains valid"),
        Store.PublishAccepted(Identity, ZeroLength) == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestEqual(TEXT("Only the valid record was retained"), Store.Read(Identity, 0, 3).Get_Records().Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionReceiptMemoryTest,
    "Ck.ProceduralAnimation.Action.ReceiptStoreAccountsNestedHeapCapacity",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionReceiptMemoryTest::RunTest(
    const FString&)
    -> bool
{
    auto Short = ck::procedural_action::FReceiptStore{};
    auto Long = ck::procedural_action::FReceiptStore{};
    auto ShortId = FCk_ProceduralAction_Identity{};
    auto LongId = FCk_ProceduralAction_Identity{};
    const auto LimbId = FName{TEXT("MemoryArm")};
    auto Populate = [this, LimbId](auto& InStore, auto& OutIdentity, int32 InLinks)
    {
        InStore.Configure(FCk_ProceduralAction_Capacity{});
        InStore.Reserve(TArray<FName>{LimbId}, false, OutIdentity);
        auto Segments = TArray<FTransform>{};
        Segments.Init(FTransform::Identity, InLinks);
        const auto Limb = FCk_ProceduralArticulation_LimbPose{LimbId, FTransform::Identity,
            FTransform::Identity, Segments, 0.0f, ECk_ProceduralRig_ChainState::Clear,
            ECk_ProceduralArticulation_ContactEvidence::None};
        const auto Pose = FCk_ProceduralArticulation_Pose{FTransform::Identity, FTransform::Identity,
            false, TArray<FCk_ProceduralArticulation_LimbPose>{Limb}};
        const auto Base = ck_test_procedural_action_receipts::MakeAccepted(OutIdentity, 1);
        const auto Receipt = FCk_ProceduralAction_Receipt{true, Base.Get_Stamp(), Base.Get_Interval(),
            Pose, Pose, Base.Get_Sampling()};
        TestTrue(TEXT("Measured receipt admitted"), InStore.PublishAccepted(OutIdentity, Receipt)
            == ck::procedural_action::EReceiptStoreWrite::Accepted);
    };
    Populate(Short, ShortId, 1);
    Populate(Long, LongId, 8);
    TestTrue(TEXT("Nested segment capacity is counted in both retained poses"),
        Long.Get_AllocatedSize() >= Short.Get_AllocatedSize() + 14 * sizeof(FTransform));
    const auto BeforeRead = Long.Get_AllocatedSize();
    Long.Read(LongId, 0, 8);
    TestEqual(TEXT("Read copies do not change store-owned capacity"), Long.Get_AllocatedSize(), BeforeRead);
    Short.Consume(ShortId, 1);
    Long.Consume(LongId, 1);
    TestTrue(TEXT("Consumed diagnostic receipt retains its nested allocations"),
        Long.Get_AllocatedSize() >= Short.Get_AllocatedSize() + 14 * sizeof(FTransform));
    auto Retire = [](auto& InStore, auto InIdentity)
    {
        InStore.CloseWindows(InIdentity, TEXT("Done"));
        InStore.Finish(InIdentity, FCk_ProceduralAction_Result{ECk_ProceduralAction_Outcome::Completed,
            ECk_ProceduralAction_Recovery::Succeeded, NAME_None});
        InStore.MarkEntityRemoved(InIdentity);
        InStore.Consume(InIdentity, InStore.Read(InIdentity, 1, 8).Get_LastStreamSequence());
    };
    Retire(Short, ShortId);
    Retire(Long, LongId);
    TestEqual(TEXT("Stream retirement releases nested geometry; equal map slack remains"),
        Long.Get_AllocatedSize(), Short.Get_AllocatedSize());
    TestEqual(TEXT("No stream remains"), Long.Get_RetainedCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBudgetValidationTest,
    "Ck.ProceduralAnimation.Action.PlayerBudgetValidation",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionBudgetValidationTest::RunTest(
    const FString&)
    -> bool
{
    auto Field = FName{};
    auto Tuning = FCk_ProceduralAction_Tuning{};
    TestTrue(TEXT("Default tuning valid"), ck::procedural_action::ValidateTuning(Tuning, Field));
    auto Sampling = Tuning.Get_Sampling();
    Sampling.Set_MaxSamples(ck::procedural_action::receipt_limits::MaxSamplingSteps + 1);
    Tuning.Set_Sampling(Sampling);
    TestFalse(TEXT("Excess sample budget rejected"), ck::procedural_action::ValidateTuning(Tuning, Field));
    TestEqual(TEXT("Named field"), Field, FName{TEXT("Sampling.MaxSamples")});
    using namespace ck::procedural_action;
    auto Capacity = FCk_ProceduralAction_Capacity{};
    TestTrue(TEXT("Default capacity valid"), ValidateCapacity(Capacity, Field));
    TestEqual(TEXT("Default retains four streams per concurrent action"), Get_MaxRetainedInstances(Capacity), 16);
    TestEqual(TEXT("Default record budget covers every retained stream's records, closure and terminal"),
        Get_MaxRetainedRecords(Capacity), int64{1056});
    TestEqual(TEXT("Default pose transform budget"), Get_MaxRetainedPoseTransforms(Capacity), int64{32768});
    Capacity.Set_MaxConcurrentInstances(receipt_limits::MaxConcurrentInstances + 1);
    TestFalse(TEXT("Excess concurrency rejected"), ValidateCapacity(Capacity, Field));
    TestEqual(TEXT("Concurrency field"), Field, FName{TEXT("Capacity.MaxConcurrentInstances")});
    Capacity = FCk_ProceduralAction_Capacity{};
    Capacity.Set_MaxConcurrentInstances(0);
    TestFalse(TEXT("Zero concurrency rejected"), ValidateCapacity(Capacity, Field));
    TestEqual(TEXT("Zero concurrency field"), Field, FName{TEXT("Capacity.MaxConcurrentInstances")});
    Capacity = FCk_ProceduralAction_Capacity{};
    Capacity.Set_RecordsPerInstance(receipt_limits::MaxOrdinaryRecordsPerInstance + 1);
    TestFalse(TEXT("Excess per-action records rejected"), ValidateCapacity(Capacity, Field));
    TestEqual(TEXT("Per-action records field"), Field, FName{TEXT("Capacity.RecordsPerInstance")});
    Capacity = FCk_ProceduralAction_Capacity{};
    Capacity.Set_RecordsPerInstance(0);
    TestFalse(TEXT("Zero per-action records rejected"), ValidateCapacity(Capacity, Field));
    TestEqual(TEXT("Zero per-action records field"), Field, FName{TEXT("Capacity.RecordsPerInstance")});
    Capacity.Set_MaxConcurrentInstances(receipt_limits::MaxConcurrentInstances);
    Capacity.Set_RecordsPerInstance(receipt_limits::MaxOrdinaryRecordsPerInstance);
    TestTrue(TEXT("Largest capacity valid"), ValidateCapacity(Capacity, Field));
    TestEqual(TEXT("Retained streams are capped"), Get_MaxRetainedInstances(Capacity), receipt_limits::MaxRetainedInstances);
    TestEqual(TEXT("Retained records are capped"), Get_MaxRetainedRecords(Capacity), receipt_limits::MaxRetainedRecords);
    TestEqual(TEXT("Retained pose transforms are capped"), Get_MaxRetainedPoseTransforms(Capacity),
        receipt_limits::MaxRetainedPoseTransforms);
    Capacity = FCk_ProceduralAction_Capacity{};
    auto Store = FReceiptStore{};
    if (NOT TestTrue(TEXT("Default capacity configured"), Store.Configure(Capacity)))
    { return false; }
    auto Identities = TArray<FCk_ProceduralAction_Identity>{};
    for (auto Index = 0; Index < Capacity.Get_MaxConcurrentInstances(); ++Index)
    {
        auto Identity = FCk_ProceduralAction_Identity{};
        if (NOT TestTrue(TEXT("Concurrent body stream reserved"),
            Store.Reserve({}, true, Identity) == EReceiptStoreWrite::Accepted))
        { return false; }
        Identities.Add(Identity);
    }
    auto Excess = FCk_ProceduralAction_Identity{};
    TestTrue(TEXT("A reservation beyond the concurrent limit is refused"),
        Store.Reserve({}, true, Excess) == EReceiptStoreWrite::CapacityExceeded);
    for (const auto Identity : Identities)
    {
        if (NOT TestTrue(TEXT("Every reserved stream publishes its first accepted pose"),
            Store.PublishAccepted(Identity, ck_test_procedural_action_receipts::MakeAccepted(Identity, 1))
                == EReceiptStoreWrite::Accepted))
        { return false; }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionReceiptReservedRecordsTest,
    "Ck.ProceduralAnimation.Action.ReceiptStoreOverflowKeepsClosureAndTerminal",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionReceiptReservedRecordsTest::RunTest(
    const FString&)
    -> bool
{
    auto Capacity = FCk_ProceduralAction_Capacity{};
    Capacity.Set_RecordsPerInstance(1);
    auto Store = ck::procedural_action::FReceiptStore{};
    if (NOT TestTrue(TEXT("Capacity accepted"), Store.Configure(Capacity))) { return false; }
    auto Identity = FCk_ProceduralAction_Identity{};
    if (NOT TestTrue(TEXT("Reservation accepted"), Store.Reserve({}, true, Identity)
        == ck::procedural_action::EReceiptStoreWrite::Accepted)) { return false; }
    TestTrue(TEXT("First accepted"), Store.PublishAccepted(Identity,
        ck_test_procedural_action_receipts::MakeAccepted(Identity, 1))
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Overflow closes windows without overwriting accepted evidence"), Store.PublishAccepted(Identity,
        ck_test_procedural_action_receipts::MakeAccepted(Identity, 2))
        == ck::procedural_action::EReceiptStoreWrite::Overflow);
    TestTrue(TEXT("Terminal uses reserved slot"), Store.Finish(Identity,
        FCk_ProceduralAction_Result{ECk_ProceduralAction_Outcome::ReceiptOverflow,
            ECk_ProceduralAction_Recovery::Succeeded, TEXT("ReceiptOverflow")})
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    const auto Page = Store.Read(Identity, 0, 3);
    TestEqual(TEXT("Three records retained"), Page.Get_Records().Num(), 3);
    if (Page.Get_Records().Num() == 3)
    {
        TestTrue(TEXT("Accepted first"), Page.Get_Records()[0].Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose);
        TestTrue(TEXT("Closure second"), Page.Get_Records()[1].Get_Kind() == ECk_ProceduralAction_RecordKind::WindowsClosed);
        TestTrue(TEXT("Terminal third"), Page.Get_Records()[2].Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal);
        TestTrue(TEXT("Sequence is action-local ordered"), Page.Get_Records()[0].Get_StreamSequence() == 1
            && Page.Get_Records()[1].Get_StreamSequence() == 2 && Page.Get_Records()[2].Get_StreamSequence() == 3);
    }
    TestTrue(TEXT("Acknowledgement succeeds"), Store.Consume(Identity, 3) == ECk_ProceduralAction_ReadResult::Succeeded);
    TestEqual(TEXT("Live finished result still retained"), Store.Get_RetainedCount(), 1);
    TestTrue(TEXT("Entity removal reclaims acknowledged terminal"), Store.MarkEntityRemoved(Identity));
    TestEqual(TEXT("Stream reclaimed"), Store.Get_RetainedCount(), 0);
    TestTrue(TEXT("Old identity diagnosed expired"), Store.Read(Identity, 0, 3).Get_Result()
        == ECk_ProceduralAction_ReadResult::StreamExpired);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionAggregateBudgetTest,
    "Ck.ProceduralAnimation.Action.ReceiptStoreAggregateBudgetAndLatest",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionAggregateBudgetTest::RunTest(
    const FString&)
    -> bool
{
    auto Capacity = FCk_ProceduralAction_Capacity{};
    Capacity.Set_MaxConcurrentInstances(2);
    Capacity.Set_RecordsPerInstance(1);
    auto Store = ck::procedural_action::FReceiptStore{};
    if (NOT TestTrue(TEXT("Small aggregate budget configured"), Store.Configure(Capacity)))
    { return false; }
    // Enough claimed limbs that one receipt's two poses outgrow what the aggregate budget has left.
    constexpr auto ArmCount = 16;
    auto ArmIds = TArray<FName>{};
    for (auto Index = 0; Index < ArmCount; ++Index)
    { ArmIds.Add(FName{*FString::Printf(TEXT("BudgetArm%d"), Index)}); }
    auto First = FCk_ProceduralAction_Identity{};
    auto Second = FCk_ProceduralAction_Identity{};
    TestTrue(TEXT("First reserves closure and terminal globally"), Store.Reserve({}, true, First)
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Second reserves closure and terminal globally"), Store.Reserve(ArmIds, false, Second)
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestEqual(TEXT("Four reserved record slots"), Store.Get_RetainedRecordSlots(), int64{4});
    TestTrue(TEXT("First stream publishes an accepted pose"), Store.PublishAccepted(First,
        ck_test_procedural_action_receipts::MakeAccepted(First, 1))
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestEqual(TEXT("Accepted pose counts four retained transforms"),
        Store.Get_RetainedPoseTransforms(), int64{4});

    auto Segments = TArray<FTransform>{};
    Segments.Init(FTransform::Identity, ck::procedural_action::receipt_limits::MaxSegmentsPerLimb);
    auto Limbs = TArray<FCk_ProceduralArticulation_LimbPose>{};
    for (const auto ArmId : ArmIds)
    {
        Limbs.Emplace(ArmId, FTransform::Identity, FTransform::Identity, Segments, 0.0f,
            ECk_ProceduralRig_ChainState::Clear, ECk_ProceduralArticulation_ContactEvidence::None);
    }
    const auto ArmPose = FCk_ProceduralArticulation_Pose{FTransform::Identity, FTransform::Identity, false, Limbs};
    const auto SecondBase = ck_test_procedural_action_receipts::MakeAccepted(Second, 1);
    const auto Large = FCk_ProceduralAction_Receipt{true, SecondBase.Get_Stamp(), SecondBase.Get_Interval(),
        ArmPose, ArmPose, FCk_ProceduralAction_SamplingReport{1, 1, 0.0f, 0.0f, FCk_Time{}}};
    const auto LargeTransforms = int64{2} * (1 + ArmCount * (2 + Segments.Num()));
    if (NOT TestTrue(TEXT("Precondition: the large receipt alone exceeds what the aggregate budget has left"),
        LargeTransforms > ck::procedural_action::Get_MaxRetainedPoseTransforms(Capacity) - Store.Get_RetainedPoseTransforms()))
    { return false; }
    TestTrue(TEXT("Other stream overflows on transform budget without stealing accepted history"),
        Store.PublishAccepted(Second, Large) == ck::procedural_action::EReceiptStoreWrite::Overflow);
    TestEqual(TEXT("The overflow leaves the first stream's transforms in place"),
        Store.Get_RetainedPoseTransforms(), int64{4});
    TestTrue(TEXT("Overflowed stream still has terminal reservation"), Store.Finish(Second,
        FCk_ProceduralAction_Result{ECk_ProceduralAction_Outcome::ReceiptOverflow,
            ECk_ProceduralAction_Recovery::NotRequired, TEXT("AggregateBudget")})
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestEqual(TEXT("Overflow stream closure and terminal ordered"),
        Store.Read(Second, 0, 2).Get_Records().Num(), 2);
    TestTrue(TEXT("Consuming accepted pose succeeds"), Store.Consume(First, 1)
        == ECk_ProceduralAction_ReadResult::Succeeded);
    TestTrue(TEXT("Last accepted pose survives consumption in the store"),
        Store.Get_LastAccepted(First).Get_HasAcceptedPose());
    TestEqual(TEXT("Diagnostic copy remains accounted"),
        Store.Get_RetainedPoseTransforms(), int64{4});
    TestTrue(TEXT("Newer accepted pose replaces consumed diagnostic copy"), Store.PublishAccepted(First,
        ck_test_procedural_action_receipts::MakeAccepted(First, 2))
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestEqual(TEXT("Replacement does not double pose usage"),
        Store.Get_RetainedPoseTransforms(), int64{4});
    TestEqual(TEXT("Latest sample is second"),
        Store.Get_LastAccepted(First).Get_Stamp().Get_SampleSequence(), int64{2});
    TestTrue(TEXT("Finishing first stream succeeds"), Store.Finish(First,
        FCk_ProceduralAction_Result{ECk_ProceduralAction_Outcome::Completed,
            ECk_ProceduralAction_Recovery::Succeeded, NAME_None})
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Consume first terminal"), Store.Consume(First, 4)
        == ECk_ProceduralAction_ReadResult::Succeeded);
    TestTrue(TEXT("Removing acknowledged first entity releases payload"), Store.MarkEntityRemoved(First));
    TestEqual(TEXT("No retained transforms after expiry"), Store.Get_RetainedPoseTransforms(), int64{0});
    TestEqual(TEXT("Only second reservation remains"), Store.Get_RetainedRecordSlots(), int64{2});

    auto QueuedCapacity = FCk_ProceduralAction_Capacity{};
    QueuedCapacity.Set_MaxConcurrentInstances(1);
    QueuedCapacity.Set_RecordsPerInstance(2);
    auto QueuedStore = ck::procedural_action::FReceiptStore{};
    TestTrue(TEXT("Two-record capacity configured"), QueuedStore.Configure(QueuedCapacity));
    auto QueuedIdentity = FCk_ProceduralAction_Identity{};
    TestTrue(TEXT("Queued stream reserved"), QueuedStore.Reserve({}, true, QueuedIdentity)
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("First queued receipt published"), QueuedStore.PublishAccepted(QueuedIdentity,
        ck_test_procedural_action_receipts::MakeAccepted(QueuedIdentity, 1))
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Second queued receipt published"), QueuedStore.PublishAccepted(QueuedIdentity,
        ck_test_procedural_action_receipts::MakeAccepted(QueuedIdentity, 2))
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Older queued receipt consumed"), QueuedStore.Consume(QueuedIdentity, 1)
        == ECk_ProceduralAction_ReadResult::Succeeded);
    TestEqual(TEXT("Newer queued receipt is sole latest payload"),
        QueuedStore.Get_RetainedPoseTransforms(), int64{4});
    TestEqual(TEXT("Latest queued receipt remains second"),
        QueuedStore.Get_LastAccepted(QueuedIdentity).Get_Stamp().Get_SampleSequence(), int64{2});
    TestTrue(TEXT("Latest queued receipt consumed"), QueuedStore.Consume(QueuedIdentity, 2)
        == ECk_ProceduralAction_ReadResult::Succeeded);
    TestEqual(TEXT("Latest consumed receipt is retained once"),
        QueuedStore.Get_RetainedPoseTransforms(), int64{4});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionReceiptReusableCapacityTest,
    "Ck.ProceduralAnimation.Action.ReceiptStoreReusesConsumedCapacity",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionReceiptReusableCapacityTest::RunTest(
    const FString&)
    -> bool
{
    auto Capacity = FCk_ProceduralAction_Capacity{};
    Capacity.Set_RecordsPerInstance(2);
    auto Store = ck::procedural_action::FReceiptStore{};
    if (NOT TestTrue(TEXT("Capacity accepted"), Store.Configure(Capacity))) { return false; }
    auto Identity = FCk_ProceduralAction_Identity{};
    if (NOT TestTrue(TEXT("Reservation accepted"), Store.Reserve({}, true, Identity)
        == ck::procedural_action::EReceiptStoreWrite::Accepted)) { return false; }
    auto Cursor = int64{0};
    for (auto Index = int64{1}; Index <= 6; ++Index)
    {
        TestTrue(TEXT("Long stream accepts after prior consumption"), Store.PublishAccepted(Identity,
            ck_test_procedural_action_receipts::MakeAccepted(Identity, Index))
            == ck::procedural_action::EReceiptStoreWrite::Accepted);
        const auto Page = Store.Read(Identity, Cursor, 1);
        TestEqual(TEXT("One new record returned"), Page.Get_Records().Num(), 1);
        TestEqual(TEXT("Cursor follows returned record"), Page.Get_LastStreamSequence(), Index);
        Cursor = Page.Get_LastStreamSequence();
        TestTrue(TEXT("Consumption releases ordinary capacity"), Store.Consume(Identity, Cursor)
            == ECk_ProceduralAction_ReadResult::Succeeded);
    }
    TestTrue(TEXT("Closure remains available"), Store.CloseWindows(Identity, NAME_None)
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Terminal remains available"), Store.Finish(Identity,
        FCk_ProceduralAction_Result{ECk_ProceduralAction_Outcome::Completed,
            ECk_ProceduralAction_Recovery::Succeeded, NAME_None})
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestEqual(TEXT("Reserved records remain"), Store.Read(Identity, Cursor, 2).Get_Records().Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionReceiptDeferredBindingTest,
    "Ck.ProceduralAnimation.Action.ReceiptStoreDeferredBinding",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionReceiptDeferredBindingTest::RunTest(
    const FString&)
    -> bool
{
    auto Store = ck::procedural_action::FReceiptStore{};
    Store.Configure(FCk_ProceduralAction_Capacity{});
    auto Identity = FCk_ProceduralAction_Identity{};
    TestTrue(TEXT("Clip reserves before content resolves"), Store.ReserveUnbound(Identity)
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Unbound stream rejects accepted pose"), Store.PublishAccepted(Identity,
        ck_test_procedural_action_receipts::MakeAccepted(Identity, 1))
        == ck::procedural_action::EReceiptStoreWrite::InvalidRecord);
    TestTrue(TEXT("Complete resource set binds once"), Store.BindChannels(Identity, {}, true)
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Rebinding is forbidden"), Store.BindChannels(Identity, {}, true)
        == ck::procedural_action::EReceiptStoreWrite::InvalidRecord);
    TestTrue(TEXT("Bound stream publishes"), Store.PublishAccepted(Identity,
        ck_test_procedural_action_receipts::MakeAccepted(Identity, 1))
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    auto Failed = FCk_ProceduralAction_Identity{};
    TestTrue(TEXT("Second pre-load reservation succeeds"), Store.ReserveUnbound(Failed)
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Failed creation rolls back empty reservation"), Store.AbortUnpublished(Failed));
    TestFalse(TEXT("Published evidence cannot be aborted"), Store.AbortUnpublished(Identity));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionReceiptPagedDrainTest,
    "Ck.ProceduralAnimation.Action.ReceiptStorePagedDrainPreservesEvents",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionReceiptPagedDrainTest::RunTest(
    const FString&)
    -> bool
{
    auto Store = ck::procedural_action::FReceiptStore{};
    Store.Configure(FCk_ProceduralAction_Capacity{});
    auto Identity = FCk_ProceduralAction_Identity{};
    Store.Reserve({}, true, Identity);
    for (auto Index = int64{1}; Index <= 3; ++Index)
    { Store.PublishAccepted(Identity, ck_test_procedural_action_receipts::MakeAccepted(Identity, Index)); }
    Store.Finish(Identity, FCk_ProceduralAction_Result{ECk_ProceduralAction_Outcome::Completed,
        ECk_ProceduralAction_Recovery::Succeeded, NAME_None});
    auto Cursor = int64{0};
    auto Seen = TArray<ECk_ProceduralAction_RecordKind>{};
    for (auto Index = 0; Index < 5; ++Index)
    {
        const auto Page = Store.Read(Identity, Cursor, 1);
        if (NOT TestEqual(TEXT("Each page carries one record"), Page.Get_Records().Num(), 1)) { return false; }
        TestTrue(TEXT("Terminal state visible while pages remain"), Page.Get_Terminal());
        TestEqual(TEXT("Returned cursor advances by one"), Page.Get_LastStreamSequence(), Cursor + 1);
        TestEqual(TEXT("HasMore matches remaining pages"), Page.Get_HasMore(), Index < 4);
        Seen.Add(Page.Get_Records()[0].Get_Kind());
        Cursor = Page.Get_LastStreamSequence();
    }
    TestEqual(TEXT("All accepted records retained"), Seen.Num(), 5);
    if (Seen.Num() == 5)
    {
        TestTrue(TEXT("Three accepted poses precede closure and terminal"),
            Seen[0] == ECk_ProceduralAction_RecordKind::AcceptedPose
            && Seen[1] == ECk_ProceduralAction_RecordKind::AcceptedPose
            && Seen[2] == ECk_ProceduralAction_RecordKind::AcceptedPose
            && Seen[3] == ECk_ProceduralAction_RecordKind::WindowsClosed
            && Seen[4] == ECk_ProceduralAction_RecordKind::Terminal);
    }
    const auto Empty = Store.Read(Identity, Cursor, 1);
    TestTrue(TEXT("Drained page empty"), Empty.Get_Records().IsEmpty());
    TestEqual(TEXT("Empty page preserves input cursor"), Empty.Get_LastStreamSequence(), Cursor);
    TestFalse(TEXT("Drained page has no more"), Empty.Get_HasMore());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionReceiptCursorTest,
    "Ck.ProceduralAnimation.Action.ReceiptStoreIdentityScopedCursors",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionReceiptCursorTest::RunTest(
    const FString&)
    -> bool
{
    auto Store = ck::procedural_action::FReceiptStore{};
    Store.Configure(FCk_ProceduralAction_Capacity{});
    auto First = FCk_ProceduralAction_Identity{};
    auto Second = FCk_ProceduralAction_Identity{};
    Store.Reserve({}, true, First);
    Store.Reserve({}, true, Second);
    Store.PublishAccepted(First, ck_test_procedural_action_receipts::MakeAccepted(First, 1));
    TestTrue(TEXT("Future cursor rejected for other stream"), Store.Consume(Second, 1)
        == ECk_ProceduralAction_ReadResult::InvalidCursor);
    TestTrue(TEXT("First stream acknowledgement succeeds"), Store.Consume(First, 1)
        == ECk_ProceduralAction_ReadResult::Succeeded);
    const auto EmptyGap = Store.Read(First, 0, 1);
    TestTrue(TEXT("Fully consumed live page reports an explicit gap"),
        EmptyGap.Get_Result() == ECk_ProceduralAction_ReadResult::Gap
            && EmptyGap.Get_Records().IsEmpty()
            && EmptyGap.Get_FirstMissingSequence() == 1
            && EmptyGap.Get_LastMissingSequence() == 1
            && EmptyGap.Get_LastStreamSequence() == 1
            && NOT EmptyGap.Get_HasMore() && NOT EmptyGap.Get_Terminal());
    Store.PublishAccepted(First, ck_test_procedural_action_receipts::MakeAccepted(First, 2));
    Store.PublishAccepted(First, ck_test_procedural_action_receipts::MakeAccepted(First, 3));
    const auto PartialGap = Store.Read(First, 0, 1);
    TestTrue(TEXT("Partial gap includes the first remaining record without skipping a page"),
        PartialGap.Get_Result() == ECk_ProceduralAction_ReadResult::Gap
            && PartialGap.Get_FirstMissingSequence() == 1
            && PartialGap.Get_LastMissingSequence() == 1
            && PartialGap.Get_Records().Num() == 1
            && PartialGap.Get_Records()[0].Get_StreamSequence() == 2
            && PartialGap.Get_LastStreamSequence() == 2
            && PartialGap.Get_HasMore());
    const auto Continued = Store.Read(First, PartialGap.Get_LastStreamSequence(), 1);
    TestTrue(TEXT("Advanced cursor drains the next retained record without another gap"),
        Continued.Get_Result() == ECk_ProceduralAction_ReadResult::Pending
            && Continued.Get_FirstMissingSequence() == 0
            && Continued.Get_LastMissingSequence() == 0
            && Continued.Get_Records().Num() == 1
            && Continued.Get_Records()[0].Get_StreamSequence() == 3
            && NOT Continued.Get_HasMore());
    TestTrue(TEXT("Negative read cursor remains invalid"), Store.Read(First, -1, 1).Get_Result()
        == ECk_ProceduralAction_ReadResult::InvalidCursor);
    TestTrue(TEXT("Future read cursor remains invalid"), Store.Read(First, 4, 1).Get_Result()
        == ECk_ProceduralAction_ReadResult::InvalidCursor);
    TestTrue(TEXT("First stream closes before terminal"), Store.CloseWindows(First, NAME_None)
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("First stream terminates"), Store.Finish(First,
        FCk_ProceduralAction_Result{ECk_ProceduralAction_Outcome::Completed,
            ECk_ProceduralAction_Recovery::Succeeded, NAME_None})
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("All terminal records can be explicitly consumed"), Store.Consume(First, 5)
        == ECk_ProceduralAction_ReadResult::Succeeded);
    const auto TerminalGap = Store.Read(First, 0, 1);
    TestTrue(TEXT("Fully consumed terminal stream reports its complete gap without records"),
        TerminalGap.Get_Result() == ECk_ProceduralAction_ReadResult::Gap
            && TerminalGap.Get_FirstMissingSequence() == 1
            && TerminalGap.Get_LastMissingSequence() == 5
            && TerminalGap.Get_LastStreamSequence() == 5
            && TerminalGap.Get_Records().IsEmpty()
            && TerminalGap.Get_Terminal() && NOT TerminalGap.Get_HasMore());
    TestTrue(TEXT("Second stream not acknowledged"), Store.Read(Second, 0, 1).Get_Result()
        == ECk_ProceduralAction_ReadResult::Pending);
    TestTrue(TEXT("Duplicate acknowledgement is idempotent"), Store.Consume(First, 1)
        == ECk_ProceduralAction_ReadResult::Succeeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionReceiptOrphanTest,
    "Ck.ProceduralAnimation.Action.ReceiptStoreOrphanExpiresWithoutTombstones",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionReceiptOrphanTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck::procedural_action;
    auto Capacity = FCk_ProceduralAction_Capacity{};
    Capacity.Set_MaxConcurrentInstances(1);
    auto Store = FReceiptStore{};
    Store.Configure(Capacity);
    auto Orphans = TArray<FCk_ProceduralAction_Identity>{};
    for (auto Index = 0; Index < Get_MaxRetainedInstances(Capacity); ++Index)
    {
        auto Orphan = FCk_ProceduralAction_Identity{};
        Store.Reserve({}, true, Orphan);
        Store.Finish(Orphan, FCk_ProceduralAction_Result{ECk_ProceduralAction_Outcome::Completed,
            ECk_ProceduralAction_Recovery::Succeeded, NAME_None});
        Store.MarkEntityRemoved(Orphan);
        Orphans.Add(Orphan);
    }
    const auto First = Orphans[0];
    const auto RetainedOrphans = Store.Get_RetainedIdentities();
    TestTrue(TEXT("Terminal orphans remain discoverable after entity removal"),
        RetainedOrphans.Num() == Orphans.Num() && RetainedOrphans[0].Get_Value() == First.Get_Value());
    auto Second = FCk_ProceduralAction_Identity{};
    TestTrue(TEXT("Unread orphans consume retained capacity"), Store.Reserve({}, true, Second)
        == EReceiptStoreWrite::CapacityExceeded);
    const auto HalfRetention = FCk_Time{receipt_limits::OrphanRetention.Get_Seconds() * 0.5};
    TestTrue(TEXT("Service advances short of the retention"), Store.AdvanceService(HalfRetention));
    TestEqual(TEXT("Orphans outlive half the retention"), Store.Get_RetainedIdentities().Num(), Orphans.Num());
    TestTrue(TEXT("Service advances through the retention"), Store.AdvanceService(HalfRetention));
    TestTrue(TEXT("Orphan expires"), Store.Read(First, 0, 3).Get_Result()
        == ECk_ProceduralAction_ReadResult::StreamExpired);
    TestTrue(TEXT("Expired stream disappears from discovery"), Store.Get_RetainedIdentities().IsEmpty());
    TestTrue(TEXT("Capacity becomes available"), Store.Reserve({}, true, Second)
        == EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Identity never reused"), Second.Get_Value() > Orphans.Last().Get_Value());
    const auto RetainedSecond = Store.Get_RetainedIdentities();
    TestTrue(TEXT("New identity appears after expiry"),
        RetainedSecond.Num() == 1 && RetainedSecond[0].Get_Value() == Second.Get_Value());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionReceiptIdentityOrderTest,
    "Ck.ProceduralAnimation.Action.ReceiptStoreIdentityDiscoveryOrdered",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionReceiptIdentityOrderTest::RunTest(
    const FString&)
    -> bool
{
    auto Store = ck::procedural_action::FReceiptStore{};
    Store.Configure(FCk_ProceduralAction_Capacity{});
    auto First = FCk_ProceduralAction_Identity{};
    auto Second = FCk_ProceduralAction_Identity{};
    auto Third = FCk_ProceduralAction_Identity{};
    Store.Reserve({}, true, First);
    Store.Reserve({}, true, Second);
    Store.Reserve({}, true, Third);
    const auto All = Store.Get_RetainedIdentities();
    TestTrue(TEXT("Discovery sorted by monotonic identity"), All.Num() == 3
        && All[0].Get_Value() == First.Get_Value()
        && All[1].Get_Value() == Second.Get_Value()
        && All[2].Get_Value() == Third.Get_Value());
    Store.AbortUnpublished(Second);
    const auto Remaining = Store.Get_RetainedIdentities();
    TestTrue(TEXT("Aborted reservation disappears without reordering"), Remaining.Num() == 2
        && Remaining[0].Get_Value() == First.Get_Value()
        && Remaining[1].Get_Value() == Third.Get_Value());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionReceiptMalformedTest,
    "Ck.ProceduralAnimation.Action.ReceiptStoreRejectsPartialAndStaleSamples",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionReceiptMalformedTest::RunTest(
    const FString&)
    -> bool
{
    auto Store = ck::procedural_action::FReceiptStore{};
    Store.Configure(FCk_ProceduralAction_Capacity{});
    auto Identity = FCk_ProceduralAction_Identity{};
    Store.Reserve(TArray<FName>{FName{TEXT("Hand")}}, false, Identity);
    TestTrue(TEXT("Missing claimed channel rejected"), Store.PublishAccepted(Identity,
        ck_test_procedural_action_receipts::MakeAccepted(Identity, 1))
        == ck::procedural_action::EReceiptStoreWrite::InvalidRecord);
    const auto HandBase = ck_test_procedural_action_receipts::MakeAccepted(Identity, 1);
    const auto BadSegment = FTransform{FQuat::Identity,
        FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, FVector::OneVector};
    const auto Limb = FCk_ProceduralArticulation_LimbPose{FName{TEXT("Hand")},
        FTransform::Identity, FTransform::Identity, TArray<FTransform>{BadSegment}, 0.0f,
        ECk_ProceduralRig_ChainState::Clear, ECk_ProceduralArticulation_ContactEvidence::None};
    const auto BadLimbPose = FCk_ProceduralArticulation_Pose{FTransform::Identity,
        FTransform::Identity, false, TArray<FCk_ProceduralArticulation_LimbPose>{Limb}};
    const auto BadLimbReceipt = FCk_ProceduralAction_Receipt{true, HandBase.Get_Stamp(),
        HandBase.Get_Interval(), {}, BadLimbPose, HandBase.Get_Sampling()};
    TestTrue(TEXT("Malformed claimed segment rejected atomically"), Store.PublishAccepted(Identity, BadLimbReceipt)
        == ck::procedural_action::EReceiptStoreWrite::InvalidRecord);
    auto BodyIdentity = FCk_ProceduralAction_Identity{};
    Store.Reserve({}, true, BodyIdentity);
    const auto Accepted = ck_test_procedural_action_receipts::MakeAccepted(BodyIdentity, 1);
    const auto BadRoot = FTransform{FQuat::Identity,
        FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, FVector::OneVector};
    const auto BadPose = FCk_ProceduralArticulation_Pose{BadRoot, FTransform::Identity, true, {}};
    const auto BadGeometry = FCk_ProceduralAction_Receipt{true, Accepted.Get_Stamp(),
        Accepted.Get_Interval(), Accepted.Get_PreviousPose(), BadPose, Accepted.Get_Sampling()};
    TestTrue(TEXT("Nonfinite accepted geometry rejected"), Store.PublishAccepted(BodyIdentity, BadGeometry)
        == ck::procedural_action::EReceiptStoreWrite::InvalidRecord);
    const auto BadEvents = TArray<FCk_ProceduralAction_Event>{
        FCk_ProceduralAction_Event{FCk_Time{0.05}, ECk_ProceduralAction_EventKind::WindowEnter,
            FName{TEXT("ReachWindow")}, 1},
        FCk_ProceduralAction_Event{FCk_Time{0.05}, ECk_ProceduralAction_EventKind::WindowExit,
            FName{TEXT("ReachWindow")}, 2}};
    const auto BadInterval = FCk_ProceduralAction_Interval{FCk_Time{0.0}, FCk_Time{0.1},
        FName{TEXT("Reach")}, ECk_ProceduralAction_Continuity::FirstSample, {}, BadEvents};
    const auto BadOrder = FCk_ProceduralAction_Receipt{true, Accepted.Get_Stamp(), BadInterval,
        Accepted.Get_PreviousPose(), Accepted.Get_CurrentPose(), Accepted.Get_Sampling()};
    TestTrue(TEXT("Wrong equal-time event order rejected"), Store.PublishAccepted(BodyIdentity, BadOrder)
        == ck::procedural_action::EReceiptStoreWrite::InvalidRecord);
    TestTrue(TEXT("Malformed receipts did not append"), Store.Read(BodyIdentity, 0, 3).Get_Records().IsEmpty());
    TestTrue(TEXT("First sequence accepted"), Store.PublishAccepted(BodyIdentity, Accepted)
        == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Repeated sample sequence rejected"), Store.PublishAccepted(BodyIdentity, Accepted)
        == ck::procedural_action::EReceiptStoreWrite::InvalidRecord);
    return true;
}
#endif
