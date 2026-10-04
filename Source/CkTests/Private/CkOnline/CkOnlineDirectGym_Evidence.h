#pragma once

#include "CkOnline/Direct/CkOnlineDirect_Fragment_Data.h"
#include "HAL/PlatformProcess.h"

namespace ck::test::direct
{
    inline auto FormatCheckpoint(const FCk_OnlineDirect_Snapshot& InSnapshot,
        const bool InChat, const int32 InOrdinal) -> FString
    {
        const auto State = [Value = InSnapshot.Get_State()]() -> const TCHAR*
        {
            switch (Value)
            {
                case ECk_OnlineDirect_State::Offline: return TEXT("Offline");
                case ECk_OnlineDirect_State::Hosting: return TEXT("Hosting");
                case ECk_OnlineDirect_State::Joining: return TEXT("Joining");
                case ECk_OnlineDirect_State::Joined: return TEXT("Joined");
                case ECk_OnlineDirect_State::Failed: return TEXT("Failed");
            }
            return TEXT("Unknown");
        }();

        const auto& Members = InSnapshot.Get_Members();
        TArray<FString> Roster;
        for (int32 Index = 0; Index < Members.Num() && Index < 8; ++Index)
        {
            const auto& Member = Members[Index];
            Roster.Add(FString::Printf(TEXT("%s:%d"),
                *Member.Get_Id().ToString(EGuidFormats::Digits), Member.Get_Ready() ? 1 : 0));
        }
        Roster.Sort();

        const auto& Messages = InSnapshot.Get_Messages();
        TArray<FString> MessageMetadata;
        const int32 FirstMessage = FMath::Max(0, Messages.Num() - 32);
        for (int32 Index = FirstMessage; Index < Messages.Num(); ++Index)
        {
            const auto& Message = Messages[Index];
            MessageMetadata.Add(FString::Printf(TEXT("%d:%s"), Message.Get_Sequence(),
                *Message.Get_SenderId().ToString(EGuidFormats::Digits)));
        }

        auto Line = FString::Printf(
            TEXT("[OnlineCheckpoint] schema=1 view=%s ordinal=%d pid=%u generation=%s epoch=%s local=%s revision=%d state=%s rosterCount=%d roster=[%s] retained=%d messages=[%s]"),
            InChat ? TEXT("Chat") : TEXT("Lobby"), InOrdinal,
            FPlatformProcess::GetCurrentProcessId(),
            *InSnapshot.Get_ServiceGeneration().ToString(EGuidFormats::Digits),
            *InSnapshot.Get_LobbyEpoch().ToString(EGuidFormats::Digits),
            *InSnapshot.Get_LocalMemberId().ToString(EGuidFormats::Digits),
            InSnapshot.Get_Revision(), State, Members.Num(), *FString::Join(Roster, TEXT(",")),
            Messages.Num(), *FString::Join(MessageMetadata, TEXT(",")));
        if (Members.Num() > 8) { Line += TEXT(" rosterTruncated=1"); }
        if (Messages.Num() > 32) { Line += TEXT(" messagesTruncated=1"); }
        return Line;
    }
}
