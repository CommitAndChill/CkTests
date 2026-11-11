#pragma once

#include "CoreMinimal.h"

struct CKTESTS_API FCkJournalObjective
{
    FName Key;
    FString Text;
    bool Completed = false;
};

struct CKTESTS_API FCkJournalQuest
{
    FName Key;
    FString Title;
    FString Summary;
    TArray<FCkJournalObjective> Objectives;
    bool Tracked = false;
    bool Completed = false;
    bool Miscellaneous = false;
};

class CKTESTS_API FCkJournalProofModel final
{
public:
    FCkJournalProofModel();

    auto Get_Quests() const -> const TArray<FCkJournalQuest>& { return _Quests; }
    auto Get_SelectedKey() const -> FName { return _SelectedKey; }
    auto Get_TabKey() const -> FName { return _TabKey; }
    auto Get_Revision() const -> uint64 { return _Revision; }
    auto FindQuest(FName InKey) const -> const FCkJournalQuest*;
    auto Get_CompletedQuestCount() const -> int32;

    auto TryConfigure(const TArray<FCkJournalQuest>& InQuests) -> bool;
    auto TrySelect(FName InKey) -> bool;
    auto TrySetTab(FName InKey) -> bool;
    auto TryToggleTracked(FName InKey) -> bool;
    auto TryCompleteObjective(FName InQuestKey, FName InObjectiveKey) -> bool;
    auto TryReset(bool InStress = false) -> bool;

private:
    TArray<FCkJournalQuest> _Quests;
    FName _SelectedKey;
    FName _TabKey = FName{TEXT("quests")};
    uint64 _Revision = 0;
};
