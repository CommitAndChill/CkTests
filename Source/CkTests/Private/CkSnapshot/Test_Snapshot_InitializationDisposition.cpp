#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CkDynamic/CkDynamic_Fragment_Data.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Persistence/CkPersistenceInitialization.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkEntityTag/CkEntityTag_Fragment_Data.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCk_Snapshot_InitializationDisposition,
    "Ck.Snapshot.Initialization.Disposition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool
    FCk_Snapshot_InitializationDisposition::
    RunTest(
        const FString& /*InParameters*/)
{
    using namespace ck::persistence;
    using EDisposition = EInitializationDisposition;
    using EFailure = EInitializationFailure;

    auto World = ck::FEcsWorld{};
    auto First = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(World.Get_Registry());
    auto Second = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(World.Get_Registry());
    auto Invalid = FCk_Handle{};
    const auto* Tags = FCk_SaveData_EntityTags::StaticStruct();
    const auto* Dynamic = FCk_SaveData_DynamicFragments::StaticStruct();
    constexpr uint64 Epoch = 12;
    constexpr uint64 NextEpoch = 13;

    const auto ExpectStatus = [this](const TCHAR* InLabel, const FCk_Handle& InEntity,
        const UScriptStruct* InPayload, uint64 InEpoch, EDisposition InDisposition,
        EFailure InFailure = EFailure::None)
    {
        const auto Status = Query(InEntity, InPayload);
        TestEqual(FString::Printf(TEXT("%s epoch"), InLabel), Status.Get_Epoch(), InEpoch);
        TestTrue(FString::Printf(TEXT("%s disposition"), InLabel), Status.Get_Disposition() == InDisposition);
        TestTrue(FString::Printf(TEXT("%s failure"), InLabel), Status.Get_Failure() == InFailure);
    };

    TestFalse(TEXT("Invalid entity cannot declare a baseline"), TryDeclare(Invalid, Tags, Epoch, EDisposition::BaselineExpected));
    TestFalse(TEXT("Null payload cannot declare a baseline"), TryDeclare(First, nullptr, Epoch, EDisposition::BaselineExpected));
    TestFalse(TEXT("Zero epoch cannot declare a baseline"), TryDeclare(First, Tags, 0, EDisposition::BaselineExpected));
    ExpectStatus(TEXT("Undeclared entity"), First, Tags, 0, EDisposition::Undeclared);
    auto Observed = TArray<EDisposition>{};
    const auto Witness = FName{TEXT("InitializationDispositionTest")};
    TestTrue(TEXT("Listener can observe a single entity and payload"),
        Subscribe(First, Tags, Witness, [&Observed](FCk_Handle, const FInitializationStatus& InStatus)
        {
            Observed.Add(InStatus.Get_Disposition());
        }));
    TestTrue(TEXT("Unresolved entity can wait for identity"), TryDeclare(First, Tags, Epoch, EDisposition::IdentityUnresolved));
    TestFalse(TEXT("Unresolved identity cannot accept a baseline"), TryAccept(First, Tags, Epoch));
    ExpectStatus(TEXT("Unresolved after early apply"), First, Tags, Epoch, EDisposition::IdentityUnresolved);
    TestTrue(TEXT("Different payload can expect a baseline"), TryDeclare(First, Dynamic, Epoch, EDisposition::BaselineExpected));
    TestTrue(TEXT("Second entity can wait for identity"), TryDeclare(Second, Tags, Epoch, EDisposition::IdentityUnresolved));
    TestTrue(TEXT("Fresh identity resolves independently"), TryDeclare(Second, Tags, Epoch, EDisposition::Fresh));
    TestFalse(TEXT("Fresh identity rejects same-epoch reclassification"), TryDeclare(Second, Tags, Epoch, EDisposition::BaselineExpected));
    ExpectStatus(TEXT("Fresh entity"), Second, Tags, Epoch, EDisposition::Fresh);
    ExpectStatus(TEXT("Independent unresolved entity"), First, Tags, Epoch, EDisposition::IdentityUnresolved);
    ExpectStatus(TEXT("Unrelated payload on second entity"), Second, Dynamic, 0, EDisposition::Undeclared);

    TestTrue(TEXT("Resolved identity can expect a baseline"), TryDeclare(First, Tags, Epoch, EDisposition::BaselineExpected));
    TestTrue(TEXT("Release before apply closes the payload"), TryRelease(First, Dynamic, Epoch));
    ExpectStatus(TEXT("Missing baseline"), First, Dynamic, Epoch, EDisposition::Unavailable, EFailure::MissingBaseline);
    TestTrue(TEXT("Missing baseline closes at release"), Query(First, Dynamic).Get_IsReleased());
    TestFalse(TEXT("Late apply cannot revive missing baseline"), TryAccept(First, Dynamic, Epoch));
    ExpectStatus(TEXT("Independent expected baseline"), First, Tags, Epoch, EDisposition::BaselineExpected);
    TestTrue(TEXT("Applied baseline commits"), TryAccept(First, Tags, Epoch));
    ExpectStatus(TEXT("Committed before release"), First, Tags, Epoch, EDisposition::BaselineCommitted);
    TestTrue(TEXT("Coherent baseline releases"), TryRelease(First, Tags, Epoch));
    ExpectStatus(TEXT("Released baseline"), First, Tags, Epoch, EDisposition::Released);

    TestTrue(TEXT("New epoch can replace terminal state"), TryDeclare(First, Dynamic, NextEpoch, EDisposition::BaselineExpected));
    TestFalse(TEXT("Previous epoch cannot redeclare"), TryDeclare(First, Dynamic, Epoch, EDisposition::Fresh));
    TestFalse(TEXT("Previous epoch cannot commit"), TryAccept(First, Dynamic, Epoch));
    TestFalse(TEXT("Previous epoch cannot discard"), TryDiscard(First, Dynamic, Epoch, EFailure::Rejected));
    TestFalse(TEXT("Previous epoch cannot release"), TryRelease(First, Dynamic, Epoch));
    ExpectStatus(TEXT("New epoch survives stale callbacks"), First, Dynamic, NextEpoch, EDisposition::BaselineExpected);
    TestTrue(TEXT("Rejected baseline is discarded"), TryDiscard(First, Dynamic, NextEpoch, EFailure::Rejected));
    TestFalse(TEXT("Rejected payload waits for coherent release"), Query(First, Dynamic).Get_IsReleased());
    TestFalse(TEXT("Failed baseline rejects later success"), TryAccept(First, Dynamic, NextEpoch));
    TestFalse(TEXT("Failure rejects reclassification"), TryDeclare(First, Dynamic, NextEpoch, EDisposition::Fresh));
    ExpectStatus(TEXT("Rejected baseline retains cause"), First, Dynamic, NextEpoch, EDisposition::Unavailable, EFailure::Rejected);
    ExpectStatus(TEXT("Other payload survives failure"), First, Tags, Epoch, EDisposition::Released);
    ExpectStatus(TEXT("Other entity survives failure"), Second, Tags, Epoch, EDisposition::Fresh);
    const auto Expected = TArray<EDisposition>{EDisposition::Undeclared, EDisposition::IdentityUnresolved,
        EDisposition::BaselineExpected, EDisposition::BaselineCommitted, EDisposition::Released};
    TestTrue(TEXT("Subscriber receives precisely its payload's coherent transitions"), Observed == Expected);
    TestTrue(TEXT("Subscriber can detach"), Unsubscribe(First, Tags, Witness));
    TestTrue(TEXT("Later epoch declares after listener detaches"), TryDeclare(First, Tags, NextEpoch, EDisposition::Fresh));
    TestTrue(TEXT("Detached subscriber receives no more transitions"), Observed == Expected);

    auto Fresh = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(World.Get_Registry());
    RequestInitialization(Fresh, Tags);
    ExpectStatus(TEXT("Ordinary request outside a load"), Fresh, Tags, 0, EDisposition::Fresh);

    const auto Transient = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    const auto OneShot = FName{TEXT("OneShot")};
    auto Seen = TArray<EDisposition>{};
    auto OtherSeen = TArray<EDisposition>{};
    auto ReleasedFailedSibling = TArray<bool>{};
    auto FailedReleaseEdges = TArray<bool>{};
    TestTrue(TEXT("Observer can await rejected payload's release"),
        Subscribe(First, Dynamic, Witness, [&FailedReleaseEdges](FCk_Handle, const FInitializationStatus& InStatus)
        { FailedReleaseEdges.Add(InStatus.Get_IsReleased()); }));
    TestTrue(TEXT("Batch subscriber registered"),
        Subscribe(Fresh, Dynamic, Witness, [&Seen, &First, Dynamic, &ReleasedFailedSibling](FCk_Handle, const FInitializationStatus& InStatus)
        {
            Seen.Add(InStatus.Get_Disposition());
            if (InStatus.Get_Disposition() == EDisposition::Released)
            { ReleasedFailedSibling.Add(Query(First, Dynamic).Get_IsReleased()); }
        }));
    TestFalse(TEXT("Duplicate listener name rejected"),
        Subscribe(Fresh, Dynamic, Witness, [](FCk_Handle, const FInitializationStatus&) {}));
    TestTrue(TEXT("Independent subscriber can remove itself during notification"),
        Subscribe(Fresh, Dynamic, OneShot, [this, &OtherSeen, Dynamic, OneShot](FCk_Handle InEntity, const FInitializationStatus& InStatus)
        {
            OtherSeen.Add(InStatus.Get_Disposition());
            if (InStatus.Get_Disposition() == EDisposition::Released)
            {
                TestTrue(TEXT("Subscriber removed from its own callback"), Unsubscribe(InEntity, Dynamic, OneShot));
                TestFalse(TEXT("Same listener cannot rebind during its own callback"),
                    Subscribe(InEntity, Dynamic, OneShot, [](FCk_Handle, const FInitializationStatus&) {}));
            }
        }));
    const auto Initial = TArray<EDisposition>{EDisposition::Undeclared};
    TestTrue(TEXT("New subscriptions notify only their own listener"), Seen == Initial && OtherSeen == Initial);
    {
        FScopedInitializationNotifications Outer{Transient};
        TestTrue(TEXT("Scoped baseline declared"), TryDeclare(Fresh, Dynamic, Epoch, EDisposition::BaselineExpected));
        {
            FScopedInitializationNotifications Inner{Transient};
            TestTrue(TEXT("Scoped baseline committed"), TryAccept(Fresh, Dynamic, Epoch));
            TestTrue(TEXT("Scoped baseline released"), TryRelease(Fresh, Dynamic, Epoch));
            TestTrue(TEXT("Rejected sibling released after successful sibling"), TryRelease(First, Dynamic, NextEpoch));
            TestTrue(TEXT("Repeated release of failed sibling is idempotent"), TryRelease(First, Dynamic, NextEpoch));
        }
        ExpectStatus(TEXT("Scoped release query is coherent"), Fresh, Dynamic, Epoch, EDisposition::Released);
        TestTrue(TEXT("Inner scope never published early"), Seen == Initial && OtherSeen == Initial);
    }
    const auto Completed = TArray<EDisposition>{EDisposition::Undeclared, EDisposition::Released};
    const auto RejectedThenReleased = TArray<bool>{false, true};
    TestTrue(TEXT("Outermost scope reports one final state per listener"), Seen == Completed && OtherSeen == Completed);
    TestTrue(TEXT("Successful release observer sees failed sibling released"), ReleasedFailedSibling == TArray<bool>{true});
    TestTrue(TEXT("Failure observer receives one coherent release edge"), FailedReleaseEdges == RejectedThenReleased);
    ExpectStatus(TEXT("Coherent release preserved rejection cause"), First, Dynamic, NextEpoch, EDisposition::Unavailable, EFailure::Rejected);
    TestTrue(TEXT("Next epoch opens after batched release"), TryDeclare(Fresh, Dynamic, NextEpoch, EDisposition::Fresh));
    TestTrue(TEXT("Removed one-shot listener stayed detached"), OtherSeen == Completed);
    TestTrue(TEXT("Remaining listener continued to receive transitions"), Seen.Num() == 3 && Seen.Last() == EDisposition::Fresh);
    Unsubscribe(Fresh, Dynamic, Witness);
    Unsubscribe(First, Dynamic, Witness);
    return NOT HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
