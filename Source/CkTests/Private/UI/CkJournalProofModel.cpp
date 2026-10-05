#include "CkJournalProofModel.h"

#include "CkCore/Macros/CkMacros.h"

namespace ck_journal_proof
{
    constexpr int32 MaxQuests = 512;
    constexpr int32 MaxKeyLength = 256;
    constexpr int32 MaxObjectives = 32;
    constexpr int32 MaxTitleLength = 256;
    constexpr int32 MaxSummaryLength = 4096;
    constexpr int32 MaxObjectiveLength = 512;

    auto MakeObjective(const TCHAR* InKey, const TCHAR* InText, bool InCompleted = false) -> FCkJournalObjective
    {
        return {FName{InKey}, InText, InCompleted};
    }

    auto MakeQuest(const TCHAR* InKey, const TCHAR* InTitle, const TCHAR* InSummary,
        TArray<FCkJournalObjective> InObjectives, bool InTracked = false,
        bool InCompleted = false, bool InMiscellaneous = false) -> FCkJournalQuest
    {
        return {FName{InKey}, InTitle, InSummary, MoveTemp(InObjectives),
            InTracked, InCompleted, InMiscellaneous};
    }

    auto MakeFixture(bool InStress) -> TArray<FCkJournalQuest>
    {
        auto Quests = TArray<FCkJournalQuest>{
            MakeQuest(TEXT("before-the-storm"), TEXT("Before the Storm"),
                TEXT("Reach Riverwood and carry word of the dragon attack to Whiterun."),
                {MakeObjective(TEXT("riverwood"), TEXT("Speak with a friend in Riverwood"), true),
                    MakeObjective(TEXT("jarl"), TEXT("Tell the Jarl of Whiterun about the dragon"))}, true),
            MakeQuest(TEXT("bleak-falls-barrow"), TEXT("Bleak Falls Barrow"),
                TEXT("Farengar needs an old stone tablet from the mountain barrow. Follow the trail through its chambers, keep an eye out for traps, and return the tablet to Dragonsreach."),
                {MakeObjective(TEXT("reach-barrow"), TEXT("Reach Bleak Falls Barrow")),
                    MakeObjective(TEXT("find-tablet"), TEXT("Recover the Dragonstone")),
                    MakeObjective(TEXT("return-tablet"), TEXT("Bring the Dragonstone to Farengar"))}),
            MakeQuest(TEXT("dragon-rising"), TEXT("Dragon Rising"),
                TEXT("A dragon has been seen near the western watchtower. Join the guard and investigate."),
                {MakeObjective(TEXT("meet-guard"), TEXT("Meet Irileth near the watchtower")),
                    MakeObjective(TEXT("investigate"), TEXT("Investigate the attack"))}),
            MakeQuest(TEXT("way-of-the-voice"), TEXT("The Way of the Voice"),
                TEXT("The Greybeards have called from High Hrothgar. Climb the mountain to hear what they know."),
                {MakeObjective(TEXT("climb"), TEXT("Travel to High Hrothgar")),
                    MakeObjective(TEXT("speak"), TEXT("Speak to Arngeir"))}),
            MakeQuest(TEXT("golden-claw"), TEXT("The Golden Claw"),
                TEXT("Lucan's missing claw may be hidden in the barrow. Return it to his shop in Riverwood."),
                {MakeObjective(TEXT("recover-claw"), TEXT("Find the Golden Claw")),
                    MakeObjective(TEXT("return-claw"), TEXT("Return the claw to Lucan"))}),
            MakeQuest(TEXT("blade-in-the-dark"), TEXT("A Blade in the Dark"),
                TEXT("Delphine suspects a dragon burial near Kynesgrove. Meet her there and watch the mound."),
                {MakeObjective(TEXT("meet-delphine"), TEXT("Meet Delphine in Kynesgrove")),
                    MakeObjective(TEXT("watch-mound"), TEXT("Investigate the burial mound"))}),
            MakeQuest(TEXT("unbound"), TEXT("Unbound"),
                TEXT("The escape from Helgen is over. The road ahead is open."),
                {MakeObjective(TEXT("escape"), TEXT("Escape Helgen"), true)}, false, true),
            MakeQuest(TEXT("misc-forge"), TEXT("Visit the Riverwood Forge"),
                TEXT("A local smith has offered a quick lesson in working metal."),
                {MakeObjective(TEXT("speak-smith"), TEXT("Speak with the smith"))}, false, false, true),
            MakeQuest(TEXT("misc-ingredients"), TEXT("Gather Ingredients"),
                TEXT("Collect a few plants for an alchemist's next mixture."),
                {MakeObjective(TEXT("collect-plants"), TEXT("Gather three mountain flowers"))}, false, false, true),
            MakeQuest(TEXT("misc-rumor"), TEXT("Ask About Rumors"),
                TEXT("The innkeeper may know of work along the road."),
                {MakeObjective(TEXT("ask-innkeeper"), TEXT("Ask the innkeeper for rumors"))}, false, false, true),
        };

        if (InStress)
        {
            Quests.Reserve(Quests.Num() + 120);
            for (int32 Index = 1; Index <= 120; ++Index)
            {
                const FString Key = FString::Printf(TEXT("fixture-quest-%03d"), Index);
                FString Title = FString::Printf(TEXT("Fixture Quest %03d"), Index);
                FString Summary = FString::Printf(
                    TEXT("Deterministic journal row %03d for scrolling and virtualization checks."), Index);
                Quests.Add({FName{*Key}, MoveTemp(Title), MoveTemp(Summary),
                    {MakeObjective(TEXT("review"), TEXT("Review this fixture entry"))}});
            }
        }
        return Quests;
    }

    auto IsValidText(const FString& InText, int32 InMaxLength) -> bool
    {
        return InText.Len() <= InMaxLength && NOT InText.TrimStartAndEnd().IsEmpty();
    }

    auto IsValidConfiguration(const TArray<FCkJournalQuest>& InQuests) -> bool
    {
        if (InQuests.IsEmpty() || InQuests.Num() > MaxQuests) { return false; }
        auto SeenQuests = TSet<FName>{};
        for (const auto& Quest : InQuests)
        {
            if (Quest.Key.IsNone() || Quest.Key.ToString().Len() > MaxKeyLength
                || Quest.Key == FName{TEXT("miscellaneous")}
                || SeenQuests.Contains(Quest.Key)
                || NOT IsValidText(Quest.Title, MaxTitleLength)
                || NOT IsValidText(Quest.Summary, MaxSummaryLength)
                || Quest.Objectives.IsEmpty() || Quest.Objectives.Num() > MaxObjectives
                || (Quest.Tracked && Quest.Completed))
            {
                return false;
            }
            SeenQuests.Add(Quest.Key);
            auto SeenObjectives = TSet<FName>{};
            bool AllCompleted = true;
            for (const auto& Objective : Quest.Objectives)
            {
                if (Objective.Key.IsNone() || Objective.Key.ToString().Len() > MaxKeyLength
                    || SeenObjectives.Contains(Objective.Key)
                    || NOT IsValidText(Objective.Text, MaxObjectiveLength))
                {
                    return false;
                }
                SeenObjectives.Add(Objective.Key);
                AllCompleted &= Objective.Completed;
            }
            if (Quest.Completed != AllCompleted) { return false; }
        }
        return true;
    }

    auto IsSameFixture(const TArray<FCkJournalQuest>& InLeft,
        const TArray<FCkJournalQuest>& InRight) -> bool
    {
        if (InLeft.Num() != InRight.Num()) { return false; }
        for (int32 Index = 0; Index < InLeft.Num(); ++Index)
        {
            const auto& Left = InLeft[Index];
            const auto& Right = InRight[Index];
            if (Left.Key != Right.Key || Left.Title != Right.Title || Left.Summary != Right.Summary
                || Left.Tracked != Right.Tracked || Left.Completed != Right.Completed
                || Left.Miscellaneous != Right.Miscellaneous
                || Left.Objectives.Num() != Right.Objectives.Num())
            {
                return false;
            }
            for (int32 ObjectiveIndex = 0; ObjectiveIndex < Left.Objectives.Num(); ++ObjectiveIndex)
            {
                const auto& LeftObjective = Left.Objectives[ObjectiveIndex];
                const auto& RightObjective = Right.Objectives[ObjectiveIndex];
                if (LeftObjective.Key != RightObjective.Key
                    || LeftObjective.Text != RightObjective.Text
                    || LeftObjective.Completed != RightObjective.Completed)
                {
                    return false;
                }
            }
        }
        return true;
    }
}

FCkJournalProofModel::FCkJournalProofModel()
{
    TryReset();
}

auto FCkJournalProofModel::FindQuest(FName InKey) const -> const FCkJournalQuest*
{
    if (InKey.IsNone()) { return nullptr; }
    return _Quests.FindByPredicate([InKey](const FCkJournalQuest& InQuest)
    {
        return InQuest.Key == InKey;
    });
}

auto FCkJournalProofModel::Get_CompletedQuestCount() const -> int32
{
    int32 Count = 0;
    for (const auto& Quest : _Quests)
    {
        if (Quest.Completed) { ++Count; }
    }
    return Count;
}

auto FCkJournalProofModel::TryConfigure(const TArray<FCkJournalQuest>& InQuests) -> bool
{
    const bool Valid = ck_journal_proof::IsValidConfiguration(InQuests);
    if (NOT Valid) { return false; }

    const FName PreviousSelection = _SelectedKey;
    _Quests = InQuests;
    _SelectedKey = _Quests[0].Key;
    if (PreviousSelection == FName{TEXT("miscellaneous")})
    {
        if (_Quests.ContainsByPredicate([](const FCkJournalQuest& InQuest)
            { return InQuest.Miscellaneous; }))
        {
            _SelectedKey = PreviousSelection;
        }
    }
    else if (FindQuest(PreviousSelection) != nullptr)
    {
        _SelectedKey = PreviousSelection;
    }
    ++_Revision;
    return true;
}

auto FCkJournalProofModel::TrySelect(FName InKey) -> bool
{
    const bool IsMiscellaneousGroup = InKey == FName{TEXT("miscellaneous")}
        && _Quests.ContainsByPredicate([](const FCkJournalQuest& InQuest)
            { return InQuest.Miscellaneous; });
    const bool Valid = IsMiscellaneousGroup || FindQuest(InKey) != nullptr;
    if (NOT Valid) { return false; }
    if (_SelectedKey != InKey)
    {
        _SelectedKey = InKey;
        ++_Revision;
    }
    return true;
}

auto FCkJournalProofModel::TrySetTab(FName InKey) -> bool
{
    const bool Valid = InKey == FName{TEXT("quests")}
        || InKey == FName{TEXT("stats")}
        || InKey == FName{TEXT("system")};
    if (NOT Valid) { return false; }
    if (_TabKey != InKey)
    {
        _TabKey = InKey;
        ++_Revision;
    }
    return true;
}

auto FCkJournalProofModel::TryToggleTracked(FName InKey) -> bool
{
    const FCkJournalQuest* Quest = FindQuest(InKey);
    const bool Valid = Quest != nullptr && NOT Quest->Completed;
    if (NOT Valid) { return false; }
    const int32 Index = static_cast<int32>(Quest - _Quests.GetData());
    _Quests[Index].Tracked = NOT _Quests[Index].Tracked;
    ++_Revision;
    return true;
}

auto FCkJournalProofModel::TryCompleteObjective(FName InQuestKey, FName InObjectiveKey) -> bool
{
    const FCkJournalQuest* Quest = FindQuest(InQuestKey);
    if (Quest == nullptr || Quest->Completed || InObjectiveKey.IsNone()) { return false; }
    const FCkJournalObjective* Objective = Quest->Objectives.FindByPredicate(
        [InObjectiveKey](const FCkJournalObjective& InObjective)
        {
            return InObjective.Key == InObjectiveKey;
        });
    const bool Valid = Objective != nullptr && NOT Objective->Completed;
    if (NOT Valid) { return false; }

    const int32 QuestIndex = static_cast<int32>(Quest - _Quests.GetData());
    const int32 ObjectiveIndex = static_cast<int32>(Objective - Quest->Objectives.GetData());
    auto& MutableQuest = _Quests[QuestIndex];
    MutableQuest.Objectives[ObjectiveIndex].Completed = true;
    const bool AllCompleted = MutableQuest.Objectives.ContainsByPredicate(
        [](const FCkJournalObjective& InObjective) { return NOT InObjective.Completed; }) == false;
    if (AllCompleted)
    {
        MutableQuest.Completed = true;
        MutableQuest.Tracked = false;
    }
    ++_Revision;
    return true;
}

auto FCkJournalProofModel::TryReset(bool InStress) -> bool
{
    const auto Fixture = ck_journal_proof::MakeFixture(InStress);
    if (NOT ck_journal_proof::IsValidConfiguration(Fixture)) { return false; }

    const bool AlreadyReset = _SelectedKey == Fixture[0].Key
        && _TabKey == FName{TEXT("quests")}
        && ck_journal_proof::IsSameFixture(_Quests, Fixture);
    if (AlreadyReset) { return true; }

    _Quests = Fixture;
    _SelectedKey = _Quests[0].Key;
    _TabKey = FName{TEXT("quests")};
    ++_Revision;
    return true;
}
