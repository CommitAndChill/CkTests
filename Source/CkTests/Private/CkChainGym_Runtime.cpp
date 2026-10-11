#include "CkChainGym_Runtime.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkChain/CkChain_Fragment.h"
#include "CkChain/CkChain_Utils.h"
#include "CkChain/CkChainLink_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"

namespace ck_chain_gym_runtime
{
    constexpr int32 MaxHistoryPerChain = 512;

    bool AllocatedAndLive(const FCk_Handle& Handle)
    {
        return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle) &&
            ck::IsValid(Handle) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                Handle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    bool CopyHistory(const FCk_Handle_Chain& Chain, TArray<FVector>& Out)
    {
        Out.Reset();
        const auto Samples = UCk_Utils_Chain_UE::Get_HistorySamples(Chain);
        if (Samples.Num() > MaxHistoryPerChain) { return false; }
        float PreviousArc = 0.0f;
        bool HasPrevious = false;
        for (const auto& Sample : Samples)
        {
            const auto Location = Sample.Get_Location();
            const auto Arc = Sample.Get_ArcDistanceCm();
            if (Location.ContainsNaN() || !FMath::IsFinite(Arc) ||
                (HasPrevious && Arc <= PreviousArc))
            { Out.Reset(); return false; }
            Out.Add(Location);
            PreviousArc = Arc;
            HasPrevious = true;
        }
        return true;
    }
}

bool UCk_ChainGym_Runtime_UE::Get_IsLiveChain(const FCk_Handle& Chain)
{
    return ck_chain_gym_runtime::AllocatedAndLive(Chain) &&
        UCk_Utils_Chain_UE::Has(Chain) &&
        !Chain.Has<ck::FTag_Chain_NeedsSetup>() &&
        !Chain.Has<ck::FTag_Chain_SplitPending>();
}

bool UCk_ChainGym_Runtime_UE::Get_IsSameFrame(
    const FCkChainGym_WireSnapshot& Left,
    const FCkChainGym_WireSnapshot& Right)
{
    return FCkChainGym_WireSnapshot::StaticStruct()->CompareScriptStruct(
        &Left, &Right, PPF_None);
}

bool UCk_ChainGym_Runtime_UE::TryGet_DebugValues(
    const FCk_Handle_Chain& Chain,
    const FCk_Handle_Chain& SplitChain,
    const TArray<FCk_Handle_Transform>& Links,
    TArray<FVector>& OutHistory,
    TArray<FVector>& OutSplitHistory,
    TArray<FVector>& OutTargets,
    TArray<bool>& OutHasTargets,
    TArray<bool>& OutHeld)
{
    OutHistory.Reset();
    OutSplitHistory.Reset();
    OutTargets.Reset();
    OutHasTargets.Reset();
    OutHeld.Reset();

    if ((Links.Num() != 8 && Links.Num() != 12) ||
        !ck_chain_gym_runtime::AllocatedAndLive(Chain) ||
        !UCk_Utils_Chain_UE::Has(Chain) ||
        Chain.Has<ck::FTag_Chain_NeedsSetup>() ||
        Chain.Has<ck::FTag_Chain_SplitPending>())
    { return false; }

    const bool HasSplit = SplitChain != FCk_Handle_Chain{};
    if (HasSplit && (Links.Num() != 8 ||
        !ck_chain_gym_runtime::AllocatedAndLive(SplitChain) ||
        !UCk_Utils_Chain_UE::Has(SplitChain) ||
        SplitChain.Has<ck::FTag_Chain_NeedsSetup>() ||
        SplitChain.Has<ck::FTag_Chain_SplitPending>() ||
        UCk_Utils_Chain_UE::Get_Head(SplitChain) != Links[3]))
    { return false; }

    const auto SourceRoster = UCk_Utils_Chain_UE::Get_Links(Chain);
    const auto SplitRoster = HasSplit
        ? UCk_Utils_Chain_UE::Get_Links(SplitChain)
        : TArray<FCk_Handle_ChainLink>{};
    if (SourceRoster.Num() != (HasSplit ? 3 : Links.Num()) ||
        SplitRoster.Num() != (HasSplit ? 4 : 0))
    { return false; }

    for (int32 I = 0; I < Links.Num(); ++I)
    {
        if (!ck_chain_gym_runtime::AllocatedAndLive(Links[I]) ||
            !UCk_Utils_Transform_UE::Has(Links[I]))
        { return false; }
        if (HasSplit && I == 3)
        {
            if (UCk_Utils_ChainLink_UE::Has(Links[I])) { return false; }
            continue;
        }
        const auto& Member = HasSplit && I > 3
            ? SplitRoster[I - 4] : SourceRoster[I];
        if (FCk_Handle(Member) != FCk_Handle(Links[I]) ||
            !UCk_Utils_ChainLink_UE::Has(Links[I]) ||
            UCk_Utils_ChainLink_UE::Get_Chain(Member) !=
                (HasSplit && I > 3 ? SplitChain : Chain))
        { return false; }
    }

    TArray<FVector> History;
    TArray<FVector> SplitHistory;
    if (!ck_chain_gym_runtime::CopyHistory(Chain, History) ||
        (HasSplit && !ck_chain_gym_runtime::CopyHistory(SplitChain, SplitHistory)))
    { return false; }

    TArray<FVector> Targets;
    TArray<bool> HasTargets;
    TArray<bool> Held;
    for (int32 I = 0; I < Links.Num(); ++I)
    {
        if (HasSplit && I == 3)
        {
            Targets.Add(FVector::ZeroVector);
            HasTargets.Add(false);
            Held.Add(false);
            continue;
        }
        const auto Member = HasSplit && I > 3
            ? SplitRoster[I - 4] : SourceRoster[I];
        const bool HasTarget = UCk_Utils_ChainLink_UE::Get_HasTargetPose(Member);
        const bool IsHeld = UCk_Utils_ChainLink_UE::Get_IsHeld(Member);
        const FVector Target = HasTarget
            ? UCk_Utils_ChainLink_UE::Get_TargetPose(Member).GetLocation()
            : FVector::ZeroVector;
        if (Target.ContainsNaN()) { return false; }
        Targets.Add(Target);
        HasTargets.Add(HasTarget);
        Held.Add(IsHeld);
    }

    OutHistory = MoveTemp(History);
    OutSplitHistory = MoveTemp(SplitHistory);
    OutTargets = MoveTemp(Targets);
    OutHasTargets = MoveTemp(HasTargets);
    OutHeld = MoveTemp(Held);
    return true;
}
