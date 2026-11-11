#include "UI/CkJournalProofModel.h"

#include "CkCore/Macros/CkMacros.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkJournalModel_FixtureAndActions,
    "Ck.UiAuthoring.Journal.Model.FixtureAndActions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkJournalModel_FixtureAndActions::RunTest(const FString&)
{
    FCkJournalProofModel Model;
    TestTrue(TEXT("normal fixture has named Skyrim quests and errands"), Model.Get_Quests().Num() >= 10);
    const auto* LongQuest = Model.FindQuest(TEXT("bleak-falls-barrow"));
    if (NOT TestTrue(TEXT("named quest is present"), LongQuest != nullptr)) { return false; }
    TestTrue(TEXT("long summary and multiple objectives are present"),
        LongQuest->Summary.Len() > 100 && LongQuest->Objectives.Num() == 3);
    TestEqual(TEXT("one quest starts complete"), Model.Get_CompletedQuestCount(), 1);
    TestTrue(TEXT("default selection is first quest"),
        Model.Get_SelectedKey() == FName{TEXT("before-the-storm")});
    TestTrue(TEXT("default tab is quests"), Model.Get_TabKey() == FName{TEXT("quests")});

    const uint64 InitialRevision = Model.Get_Revision();
    TestTrue(TEXT("selecting the same quest succeeds"), Model.TrySelect(TEXT("before-the-storm")));
    TestTrue(TEXT("same selection does not revise"), Model.Get_Revision() == InitialRevision);
    TestTrue(TEXT("selecting miscellaneous group succeeds"), Model.TrySelect(TEXT("miscellaneous")));
    TestTrue(TEXT("selecting stats succeeds"), Model.TrySetTab(TEXT("stats")));
    TestTrue(TEXT("selection persists on stats tab"),
        Model.Get_SelectedKey() == FName{TEXT("miscellaneous")});
    const uint64 StatsRevision = Model.Get_Revision();
    TestTrue(TEXT("selecting same tab succeeds"), Model.TrySetTab(TEXT("stats")));
    TestTrue(TEXT("same tab does not revise"), Model.Get_Revision() == StatsRevision);
    TestTrue(TEXT("quest tab restores without changing selection"), Model.TrySetTab(TEXT("quests"))
        && Model.Get_SelectedKey() == FName{TEXT("miscellaneous")});

    TestTrue(TEXT("tracked quest can be untracked"), Model.TryToggleTracked(TEXT("before-the-storm")));
    TestFalse(TEXT("tracked flag was cleared"), Model.FindQuest(TEXT("before-the-storm"))->Tracked);
    TestTrue(TEXT("quest can be tracked again"), Model.TryToggleTracked(TEXT("before-the-storm")));
    const uint64 BeforeCompletion = Model.Get_Revision();
    TestTrue(TEXT("final objective can complete"),
        Model.TryCompleteObjective(TEXT("before-the-storm"), TEXT("jarl")));
    const auto* Completed = Model.FindQuest(TEXT("before-the-storm"));
    TestTrue(TEXT("completion and tracking clear atomically"), Completed->Completed && NOT Completed->Tracked);
    TestTrue(TEXT("completion revises once"), Model.Get_Revision() == BeforeCompletion + 1);
    TestEqual(TEXT("completed count updates"), Model.Get_CompletedQuestCount(), 2);
    TestFalse(TEXT("completed quest cannot be tracked"), Model.TryToggleTracked(TEXT("before-the-storm")));
    TestFalse(TEXT("completed objective is stale"),
        Model.TryCompleteObjective(TEXT("before-the-storm"), TEXT("jarl")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkJournalModel_AtomicFailures,
    "Ck.UiAuthoring.Journal.Model.AtomicFailures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkJournalModel_AtomicFailures::RunTest(const FString&)
{
    FCkJournalProofModel Model;
    const auto Original = Model.Get_Quests();
    const FName OriginalSelection = Model.Get_SelectedKey();
    const FName OriginalTab = Model.Get_TabKey();
    const uint64 OriginalRevision = Model.Get_Revision();
    auto Reject = [this, &Model, &Original, OriginalSelection, OriginalTab, OriginalRevision]
        (const TCHAR* InLabel, const TArray<FCkJournalQuest>& InCandidate)
    {
        TestFalse(InLabel, Model.TryConfigure(InCandidate));
        TestTrue(TEXT("rejected config preserves revision, selection, tab and rows"),
            Model.Get_Revision() == OriginalRevision
                && Model.Get_SelectedKey() == OriginalSelection
                && Model.Get_TabKey() == OriginalTab
                && Model.Get_Quests().Num() == Original.Num()
                && Model.Get_Quests()[0].Key == Original[0].Key
                && Model.Get_Quests()[0].Tracked == Original[0].Tracked);
    };

    Reject(TEXT("empty config rejects"), {});
    Reject(TEXT("default quest rejects"), {FCkJournalQuest{}});
    auto Invalid = Original;
    Invalid[0].Key = NAME_None;
    Reject(TEXT("empty quest key rejects"), Invalid);
    Invalid = Original;
    Invalid[0].Key = FName{*FString::ChrN(257, TCHAR('k'))};
    Reject(TEXT("oversized quest key rejects before native tree publication"), Invalid);
    Invalid = Original;
    Invalid[1].Key = Invalid[0].Key;
    Reject(TEXT("duplicate quest key rejects"), Invalid);
    Invalid = Original;
    Invalid[1].Key = FName{TEXT("miscellaneous")};
    Reject(TEXT("reserved group key rejects"), Invalid);
    Invalid = Original;
    Invalid[0].Title.Empty();
    Reject(TEXT("empty title rejects"), Invalid);
    Invalid = Original;
    Invalid[0].Title = FString::ChrN(257, TEXT('x'));
    Reject(TEXT("oversized title rejects"), Invalid);
    Invalid = Original;
    Invalid[0].Summary = FString::ChrN(4097, TCHAR('x'));
    Reject(TEXT("oversized summary rejects"), Invalid);
    Invalid = Original;
    Invalid[0].Objectives.Empty();
    Reject(TEXT("empty objective set rejects"), Invalid);
    Invalid = Original;
    while (Invalid[0].Objectives.Num() <= 32)
    {
        auto Extra = Invalid[0].Objectives[0];
        Extra.Key = FName{*FString::Printf(TEXT("extra-objective-%02d"), Invalid[0].Objectives.Num())};
        Invalid[0].Objectives.Add(MoveTemp(Extra));
    }
    Reject(TEXT("more than 32 objectives rejects"), Invalid);
    Invalid = Original;
    Invalid[0].Objectives[1].Key = Invalid[0].Objectives[0].Key;
    Reject(TEXT("duplicate objective key rejects"), Invalid);
    Invalid = Original;
    Invalid[0].Objectives[0].Key = FName{*FString::ChrN(257, TCHAR('k'))};
    Reject(TEXT("oversized objective key rejects before native tree publication"), Invalid);
    Invalid = Original;
    Invalid[0].Objectives[0].Text.Empty();
    Reject(TEXT("empty objective text rejects"), Invalid);
    Invalid = Original;
    Invalid[0].Objectives[0].Text = FString::ChrN(513, TEXT('x'));
    Reject(TEXT("oversized objective text rejects"), Invalid);
    Invalid = Original;
    Invalid[0].Completed = true;
    Reject(TEXT("completion mismatch rejects"), Invalid);
    Invalid = Original;
    Invalid[6].Tracked = true;
    Reject(TEXT("tracked completed quest rejects"), Invalid);
    Invalid = Original;
    while (Invalid.Num() <= 512)
    {
        auto Extra = Original[0];
        Extra.Key = FName{*FString::Printf(TEXT("extra-%03d"), Invalid.Num())};
        Invalid.Add(MoveTemp(Extra));
    }
    Reject(TEXT("more than 512 quests rejects"), Invalid);

    TestFalse(TEXT("default key rejects selection"), Model.TrySelect(NAME_None));
    TestFalse(TEXT("stale key rejects selection"), Model.TrySelect(TEXT("missing")));
    TestFalse(TEXT("invalid tab rejects"), Model.TrySetTab(TEXT("inventory")));
    TestFalse(TEXT("group cannot be tracked"), Model.TryToggleTracked(TEXT("miscellaneous")));
    TestFalse(TEXT("stale quest cannot be tracked"), Model.TryToggleTracked(TEXT("missing")));
    TestFalse(TEXT("stale quest objective rejects"),
        Model.TryCompleteObjective(TEXT("missing"), TEXT("jarl")));
    TestFalse(TEXT("stale objective rejects"),
        Model.TryCompleteObjective(TEXT("before-the-storm"), TEXT("missing")));
    TestFalse(TEXT("already complete objective rejects"),
        Model.TryCompleteObjective(TEXT("before-the-storm"), TEXT("riverwood")));
    TestTrue(TEXT("invalid actions preserve revision, selection and output"),
        Model.Get_Revision() == OriginalRevision
            && Model.Get_SelectedKey() == OriginalSelection
            && Model.Get_TabKey() == OriginalTab
            && Model.Get_Quests()[0].Tracked == Original[0].Tracked
            && NOT Model.FindQuest(TEXT("before-the-storm"))->Completed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkJournalModel_ConfigurationAndStress,
    "Ck.UiAuthoring.Journal.Model.ConfigurationAndStress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkJournalModel_ConfigurationAndStress::RunTest(const FString&)
{
    FCkJournalProofModel Model;
    TestTrue(TEXT("quest can be selected before reconfiguration"),
        Model.TrySelect(TEXT("dragon-rising")));
    const uint64 BeforeConfig = Model.Get_Revision();
    auto Candidate = Model.Get_Quests();
    TestTrue(TEXT("valid configuration succeeds"), Model.TryConfigure(Candidate));
    TestTrue(TEXT("configuration increments exactly once"), Model.Get_Revision() == BeforeConfig + 1);
    TestTrue(TEXT("stable selection survives configuration"),
        Model.Get_SelectedKey() == FName{TEXT("dragon-rising")});
    Candidate.RemoveAt(2);
    TestTrue(TEXT("configuration without selection succeeds"), Model.TryConfigure(Candidate));
    TestTrue(TEXT("missing selection falls back to first"),
        Model.Get_SelectedKey() == Candidate[0].Key);
    TestTrue(TEXT("normal reset succeeds"), Model.TryReset());
    const uint64 ResetRevision = Model.Get_Revision();
    TestTrue(TEXT("same reset succeeds"), Model.TryReset());
    TestTrue(TEXT("same reset does not revise"), Model.Get_Revision() == ResetRevision);
    TestTrue(TEXT("stress reset succeeds"), Model.TryReset(true));
    TestEqual(TEXT("stress fixture adds 120 rows"), Model.Get_Quests().Num(), 130);
    TestTrue(TEXT("last stress row is deterministic"),
        Model.Get_Quests().Last().Key == FName{TEXT("fixture-quest-120")});
    const uint64 StressRevision = Model.Get_Revision();
    TestTrue(TEXT("same stress reset succeeds"), Model.TryReset(true));
    TestTrue(TEXT("same stress reset does not revise"), Model.Get_Revision() == StressRevision);
    TestTrue(TEXT("return to normal fixture succeeds"), Model.TryReset());
    TestEqual(TEXT("normal fixture count restored"), Model.Get_Quests().Num(), 10);
    TestTrue(TEXT("normal fixture selection restored"),
        Model.Get_SelectedKey() == FName{TEXT("before-the-storm")});
    return true;
}
