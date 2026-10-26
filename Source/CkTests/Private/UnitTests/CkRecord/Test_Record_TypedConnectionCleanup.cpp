#if WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkCore/Ensure/CkEnsure_Utils.h"
#include "CkCore/Settings/CkCore_Settings.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Processor.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkLabel/CkLabel_Utils.h"
#include "CkRecord/Record/CkRecord_Utils.h"
#include "CkRecord/RecordEntry/CkRecordEntry_Processor.h"

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Misc/ScopeExit.h"
#include "EngineGlobals.h"

namespace ck_test_record_typed_connection_cleanup
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto Connected = true;
    constexpr auto Disconnected = false;
    UE_DEFINE_GAMEPLAY_TAG_STATIC(EntryLabel, "CkTests.Record.TypedConnectionCleanup.Entry");

    CK_DEFINE_RECORD_OF_ENTITIES_AND_UTILS(FRecordAUtils, FRecordA, FCk_Handle);
    CK_DEFINE_RECORD_OF_ENTITIES_AND_UTILS(FRecordBUtils, FRecordB, FCk_Handle);

    template <typename T_Utils>
    auto
        ExpectConnection(
            FAutomationTestBase& InTest,
            const TCHAR* InMessage,
            const FCk_Handle& InOwner,
            const FCk_Handle& InEntry,
            bool InConnected)
        -> void
    {
        using State = typename T_Utils::EConnectionState;
        InTest.TestTrue(InMessage, T_Utils::INTERNAL__Get_ConnectionState(InOwner, InEntry)
            == (InConnected ? State::Connected : State::Disconnected));
    }

    auto
        ComposeRecords(
            FCk_Handle& InOwner)
        -> void
    {
        FRecordAUtils::AddIfMissing(InOwner, ECk_Record_EntryHandlingPolicy::DisallowDuplicateNames);
        FRecordBUtils::AddIfMissing(InOwner, ECk_Record_EntryHandlingPolicy::DisallowDuplicateNames);
    }

    auto
        ComposeEntry(
            FCk_Handle& InLifetimeOwner)
        -> FCk_Handle
    {
        auto Entry = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InLifetimeOwner, {});
        UCk_Utils_GameplayLabel_UE::Add(Entry, EntryLabel);
        return Entry;
    }

    auto
        ConnectBoth(
            FCk_Handle& InOwner,
            FCk_Handle& InEntry)
        -> void
    {
        FRecordAUtils::Request_Connect(InOwner, InEntry, ECk_Record_LabelRequirementPolicy::Required, {});
        FRecordBUtils::Request_Connect(InOwner, InEntry, ECk_Record_LabelRequirementPolicy::Required, {});
    }

    auto
        DestroyEntry(
            const FCk_Registry& InRegistry,
            FCk_Handle& InEntry)
        -> void
    {
        // Native registry fixtures drive the production phases explicitly, including feature EndPlay before reclamation.
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(InEntry);
        ck::FProcessor_EntityLifetime_DestructionPhase_Endplay::ForEachEntity(FCk_Time{}, InEntry);
        ck::FProcessor_RecordEntry_Destructor{InRegistry}.ForEachEntity(
            FCk_Time{}, InEntry, InEntry.Get<ck::FFragment_RecordEntry, ck::IsValid_Policy_IncludePendingKill>());
        ck::FProcessor_EntityLifetime_DestructionPhase_Teardown::ForEachEntity(FCk_Time{}, InEntry);
        ck::FProcessor_EntityLifetime_DestructionPhase_Await::ForEachEntity(FCk_Time{}, InEntry);
        ck::FProcessor_EntityLifetime_DestructionPhase_Finalize::ForEachEntity(FCk_Time{}, InEntry);
        ck::FProcessor_EntityLifetime_DestroyEntity{InRegistry}.DoTick(FCk_Time{});
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Record_TypedDisconnectPreservesSibling,
    "CkTests.UnitTests.CkRecord.TypedDisconnectPreservesSibling",
    ck_test_record_typed_connection_cleanup::TestFlags)

auto
    FCk_Record_TypedDisconnectPreservesSibling::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_record_typed_connection_cleanup;
    auto World = ck::FEcsWorld{};
    auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto Entry = ComposeEntry(Root);
    ComposeRecords(Owner);
    ConnectBoth(Owner, Entry);
    ExpectConnection<FRecordAUtils>(*this, TEXT("A is fully connected beside B"), Owner, Entry, Connected);
    ExpectConnection<FRecordBUtils>(*this, TEXT("B is fully connected beside A"), Owner, Entry, Connected);
    TestTrue(TEXT("A's public label lookup resolves the composed entry"),
        FRecordAUtils::Get_ValidEntry_ByTag(Owner, EntryLabel) == Entry);
    TestTrue(TEXT("B's public label lookup resolves the composed entry"),
        FRecordBUtils::Get_ValidEntry_ByTag(Owner, EntryLabel) == Entry);

    TestTrue(TEXT("typed disconnect A succeeds"), FRecordAUtils::Request_Disconnect(Owner, Entry, {}));
    ExpectConnection<FRecordAUtils>(*this, TEXT("A has no remaining connection edges"), Owner, Entry, Disconnected);
    ExpectConnection<FRecordBUtils>(*this, TEXT("disconnect A preserves all B edges"), Owner, Entry, Connected);
    TestTrue(TEXT("B remains publicly discoverable by label after A disconnect"),
        FRecordBUtils::Get_ValidEntry_ByTag(Owner, EntryLabel) == Entry);

    DestroyEntry(World.Get_Registry(), Entry);
    TestFalse(TEXT("entry is actually reclaimed before cleanup assertions"),
        ck::IsValid(Entry, ck::IsValid_Policy_IncludePendingKill{}));
    ExpectConnection<FRecordAUtils>(*this, TEXT("destruction leaves A disconnected"), Owner, Entry, Disconnected);
    ExpectConnection<FRecordBUtils>(*this, TEXT("destruction removes B including its identity label index"), Owner, Entry, Disconnected);
    TestTrue(TEXT("both public forward records are empty"),
        FRecordAUtils::Get_Entries(Owner).IsEmpty() && FRecordBUtils::Get_Entries(Owner).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Record_TypedDestructionUnlinksBoth,
    "CkTests.UnitTests.CkRecord.TypedDestructionUnlinksBoth",
    ck_test_record_typed_connection_cleanup::TestFlags)

auto
    FCk_Record_TypedDestructionUnlinksBoth::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_record_typed_connection_cleanup;
    auto World = ck::FEcsWorld{};
    auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto Entry = ComposeEntry(Root);
    ComposeRecords(Owner);
    ConnectBoth(Owner, Entry);
    ExpectConnection<FRecordAUtils>(*this, TEXT("both-live A is fully connected"), Owner, Entry, Connected);
    ExpectConnection<FRecordBUtils>(*this, TEXT("both-live B is fully connected"), Owner, Entry, Connected);
    DestroyEntry(World.Get_Registry(), Entry);
    TestFalse(TEXT("both-connected entry is actually reclaimed"),
        ck::IsValid(Entry, ck::IsValid_Policy_IncludePendingKill{}));
    ExpectConnection<FRecordAUtils>(*this, TEXT("A's typed destruction callback removes all A edges"), Owner, Entry, Disconnected);
    ExpectConnection<FRecordBUtils>(*this, TEXT("B's typed destruction callback removes all B edges"), Owner, Entry, Disconnected);
    TestTrue(TEXT("both typed callbacks clear their respective forward records"),
        FRecordAUtils::Get_Entries(Owner).IsEmpty() && FRecordBUtils::Get_Entries(Owner).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Record_CrossRegistryDestructionPreservesOwnerIdentity,
    "CkTests.UnitTests.CkRecord.CrossRegistryDestructionPreservesOwnerIdentity",
    ck_test_record_typed_connection_cleanup::TestFlags)

auto
    FCk_Record_CrossRegistryDestructionPreservesOwnerIdentity::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_record_typed_connection_cleanup;
    auto OwnerWorld = ck::FEcsWorld{};
    auto EntryWorld = ck::FEcsWorld{};
    auto OwnerRoot = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(OwnerWorld.Get_Registry());
    auto EntryRoot = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(EntryWorld.Get_Registry());
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(OwnerRoot, {});
    auto Decoy = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(EntryRoot, {});
    if (NOT TestTrue(TEXT("fixture has colliding entity IDs in distinct registries"),
        Owner.Get_Entity() == Decoy.Get_Entity() && Owner != Decoy))
    { return false; }
    auto Entry = ComposeEntry(EntryRoot);
    auto Sentinel = ComposeEntry(EntryRoot);
    ComposeRecords(Owner);
    ComposeRecords(Decoy);
    ConnectBoth(Owner, Entry);
    ConnectBoth(Decoy, Sentinel);
    ExpectConnection<FRecordAUtils>(*this, TEXT("foreign A stores the full owner identity"), Owner, Entry, Connected);
    ExpectConnection<FRecordBUtils>(*this, TEXT("foreign B stores the full owner identity"), Owner, Entry, Connected);

    DestroyEntry(EntryWorld.Get_Registry(), Entry);
    TestFalse(TEXT("cross-registry entry is actually reclaimed"),
        ck::IsValid(Entry, ck::IsValid_Policy_IncludePendingKill{}));
    ExpectConnection<FRecordAUtils>(*this, TEXT("foreign A is unlinked in the original owner's registry"), Owner, Entry, Disconnected);
    ExpectConnection<FRecordBUtils>(*this, TEXT("foreign B is unlinked in the original owner's registry"), Owner, Entry, Disconnected);
    TestTrue(TEXT("the actual foreign owner's records are empty"),
        FRecordAUtils::Get_Entries(Owner).IsEmpty() && FRecordBUtils::Get_Entries(Owner).IsEmpty());
    ExpectConnection<FRecordAUtils>(*this, TEXT("colliding local owner keeps A's sentinel"), Decoy, Sentinel, Connected);
    ExpectConnection<FRecordBUtils>(*this, TEXT("colliding local owner keeps B's sentinel"), Decoy, Sentinel, Connected);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Record_InvalidDisconnectPreservesLiveConnections,
    "CkTests.UnitTests.CkRecord.InvalidDisconnectPreservesLiveConnections",
    ck_test_record_typed_connection_cleanup::TestFlags)

auto
    FCk_Record_InvalidDisconnectPreservesLiveConnections::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_record_typed_connection_cleanup;
    auto World = ck::FEcsWorld{};
    auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto Canary = ComposeEntry(Root);
    ComposeRecords(Owner);
    ConnectBoth(Owner, Canary);

    auto RetiredOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto RetiredEntry = ComposeEntry(Root);
    ComposeRecords(RetiredOwner);
    ConnectBoth(RetiredOwner, RetiredEntry);
    DestroyEntry(World.Get_Registry(), RetiredEntry);
    if (NOT TestFalse(TEXT("invalid-input fixture entry is fully reclaimed"),
        ck::IsValid(RetiredEntry, ck::IsValid_Policy_IncludePendingKill{})))
    { return false; }

    const auto CheckCanary = [&]()
    {
        ExpectConnection<FRecordAUtils>(*this, TEXT("rejected disconnect preserves all canary A edges"), Owner, Canary, Connected);
        ExpectConnection<FRecordBUtils>(*this, TEXT("rejected disconnect preserves all canary B edges"), Owner, Canary, Connected);
        TestTrue(TEXT("rejected disconnect preserves both exact public forward lists"),
            FRecordAUtils::Get_Entries(Owner) == TArray<FCk_Handle>{Canary}
                && FRecordBUtils::Get_Entries(Owner) == TArray<FCk_Handle>{Canary});
        TestTrue(TEXT("rejected disconnect preserves both public label lookups"),
            FRecordAUtils::Get_ValidEntry_ByTag(Owner, EntryLabel) == Canary
                && FRecordBUtils::Get_ValidEntry_ByTag(Owner, EntryLabel) == Canary);
    };
    CheckCanary();
    const auto OriginalPolicy = UCk_Utils_Core_UserSettings_UE::Get_EnsureDisplayPolicy();
    ON_SCOPE_EXIT
    { UCk_Utils_Core_UserSettings_UE::Set_EnsureDisplayPolicy(OriginalPolicy); };
    UCk_Utils_Core_UserSettings_UE::Set_EnsureDisplayPolicy(ECk_EnsureDisplay_Policy::LogOnly);
    // Outside PIE the editor also copies ensures to its Message Log; count actual occurrences independently below.
    const auto DiagnosticCopies = GIsEditor && NOT GIsPlayInEditorWorld ? 2 : 1;
    AddExpectedError(TEXT("Invalid Record handle"), EAutomationExpectedErrorFlags::Contains, DiagnosticCopies);
    AddExpectedError(TEXT("Invalid RecordEntry handle"), EAutomationExpectedErrorFlags::Contains, 2 * DiagnosticCopies);
    const auto EnsuresBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();
    auto DefaultOwner = FCk_Handle{};
    auto DefaultEntry = FCk_Handle{};
    TestFalse(TEXT("default record is rejected before fragment inspection"),
        FRecordAUtils::Request_Disconnect(DefaultOwner, Canary, {}));
    CheckCanary();
    TestFalse(TEXT("default entry is rejected before fragment inspection"),
        FRecordAUtils::Request_Disconnect(Owner, DefaultEntry, {}));
    CheckCanary();
    TestFalse(TEXT("fully reclaimed entry is rejected before fragment inspection"),
        FRecordAUtils::Request_Disconnect(Owner, RetiredEntry, {}));
    CheckCanary();
    TestEqual(TEXT("each rejected operation fires exactly one ensure"),
        UCk_Utils_Ensure_UE::Get_EnsureCount(), EnsuresBefore + 3);
    return true;
}

#endif
