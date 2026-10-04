#pragma once

#include "CkOnline/Direct/CkOnlineDirect_Fragment_Data.h"
#include "CkOnline/Direct/CkOnlineDirect_Transport.h"

namespace ck::test::ip
{
    /** Value-only correlation for the test consumer; it is not gameplay authentication. */
    struct FLobbyBinding
    {
        FGuid Generation;
        FGuid Epoch;
        FGuid LocalMemberId;
        FString HostAddress;
        TArray<FGuid> Members;
        bool Hosting = false;

        static auto TryCapture(const FCk_OnlineDirect_Snapshot& InSnapshot,
            bool InHosting, FLobbyBinding& Out) -> bool
        {
            Out = {};
            if (InSnapshot.Get_State() != (InHosting ? ECk_OnlineDirect_State::Hosting : ECk_OnlineDirect_State::Joined)
                || !InSnapshot.Get_ServiceGeneration().IsValid() || !InSnapshot.Get_LobbyEpoch().IsValid()
                || !InSnapshot.Get_LocalMemberId().IsValid() || InSnapshot.Get_Members().Num() != 2
                || !ck::direct::FTransport::ValidateHostAddress(InSnapshot.Get_HostAddress()))
            { return false; }
            FLobbyBinding Candidate;
            Candidate.Generation = InSnapshot.Get_ServiceGeneration();
            Candidate.Epoch = InSnapshot.Get_LobbyEpoch();
            Candidate.LocalMemberId = InSnapshot.Get_LocalMemberId();
            Candidate.HostAddress = InSnapshot.Get_HostAddress();
            Candidate.Hosting = InHosting;
            for (const auto& Member : InSnapshot.Get_Members())
            {
                if (!Member.Get_Id().IsValid() || !Member.Get_Ready() || Candidate.Members.Contains(Member.Get_Id()))
                { return false; }
                Candidate.Members.Add(Member.Get_Id());
            }
            if (!Candidate.Members.Contains(Candidate.LocalMemberId)) { return false; }
            Candidate.Members.Sort([](const FGuid& A, const FGuid& B)
            { return A.ToString(EGuidFormats::Digits) < B.ToString(EGuidFormats::Digits); });
            Out = MoveTemp(Candidate);
            return true;
        }

        auto Matches(const FCk_OnlineDirect_Snapshot& InSnapshot) const -> bool
        {
            FLobbyBinding Current;
            return TryCapture(InSnapshot, Hosting, Current) && Generation == Current.Generation &&
                Epoch == Current.Epoch && LocalMemberId == Current.LocalMemberId &&
                HostAddress == Current.HostAddress && Members == Current.Members;
        }
    };
}
