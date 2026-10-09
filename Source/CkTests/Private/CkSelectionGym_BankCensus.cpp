#include "CkSelectionGym_BankCensus.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "Engine/World.h"

#include <limits>

namespace ck_selection_gym_census
{
    namespace
    {
        auto Allocated(const FCk_Handle& H) -> bool
        { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

        auto Live(const FCk_Handle& H) -> bool
        {
            return Allocated(H) && ck::IsValid(H) &&
                !H.Has_Any<ck::FTag_DestroyEntity_Initiate,
                    ck::FTag_DestroyEntity_EndPlay, ck::FTag_DestroyEntity_Teardown,
                    ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>();
        }

        auto SameRegistry(const FCk_Handle& H, const FCk_Handle& WorldRoot) -> bool
        {
            return Allocated(H) && Allocated(WorldRoot) &&
                H.Get_RegistryView().Get_RegistryHandle() ==
                    WorldRoot.Get_RegistryView().Get_RegistryHandle();
        }

        auto LiveWorld(UWorld* World) -> bool
        {
            return IsInGameThread() && IsValid(World) &&
                !World->HasAnyFlags(RF_ClassDefaultObject | RF_BeginDestroyed | RF_FinishDestroyed) &&
                World->IsGameWorld() && !World->bIsTearingDown &&
                !World->IsBeingCleanedUp();
        }

        auto ExactWorldRoot(const FCk_Handle& WorldRoot, UWorld* World) -> bool
        {
            if (!LiveWorld(World) || !Live(WorldRoot) ||
                !WorldRoot.Has<TWeakObjectPtr<UWorld>>())
            { return false; }
            const auto WeakWorld = WorldRoot.Get<TWeakObjectPtr<UWorld>>();
            if (WeakWorld.Get() != World) { return false; }
            const auto Registry = WorldRoot.Get_RegistryView();
            return FCk_Handle{Registry.Get_TransientEntity(), Registry.Get_RegistryHandle()} == WorldRoot &&
                UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) == WorldRoot;
        }

        auto DirectOwner(const FCk_Handle& Child, const FCk_Handle& Owner,
            const FCk_Handle& WorldRoot) -> bool
        {
            return SameRegistry(Child, WorldRoot) && SameRegistry(Owner, WorldRoot) &&
                Child.Has<ck::FFragment_LifetimeOwner>() &&
                Child.Get<ck::FFragment_LifetimeOwner,
                    ck::IsValid_Policy_IncludePendingKill>().Get_Entity() == Owner;
        }
    }

    auto TryGetOpaqueEntityKey(const FCk_Handle& Entity,
        const FCk_Handle& WorldRoot, int64& OutKey) -> bool
    {
        OutKey = 0;
        if (!Live(WorldRoot) || !WorldRoot.Has<TWeakObjectPtr<UWorld>>())
        { return false; }
        const auto WeakWorld = WorldRoot.Get<TWeakObjectPtr<UWorld>>();
        if (!ExactWorldRoot(WorldRoot, WeakWorld.Get()) ||
            !Live(Entity) || !SameRegistry(Entity, WorldRoot))
        { return false; }
        const auto& Id = Entity.Get_Entity();
        if (Id.Get_IsTombstone()) { return false; }
        const uint64 RawId = static_cast<uint64>(Id.Get_ID());
        if (RawId >= static_cast<uint64>(std::numeric_limits<int64>::max()))
        { return false; }
        OutKey = static_cast<int64>(RawId + 1);
        return OutKey > 0;
    }

    auto TryCensusAllocatedBank(UWorld* World,
        const FCk_Handle& ScenarioRoot,
        const TArray<FCk_Handle>& DirectWorldTransientRoots,
        TArray<FCk_Handle>& OutHandles, FString& OutReason) -> bool
    {
        // Both input references may point into the output array.
        const auto RetainedScenarioRoot = ScenarioRoot;
        const auto RetainedRoots = DirectWorldTransientRoots;
        OutHandles.Reset();
        OutReason.Reset();
        auto Fail = [&](const TCHAR* Why) -> bool
        {
            OutHandles.Reset();
            OutReason = Why;
            return false;
        };

        if (!LiveWorld(World))
        { return Fail(TEXT("World unavailable")); }

        const auto WorldRoot =
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!ExactWorldRoot(WorldRoot, World))
        { return Fail(TEXT("World transient root unavailable or bound elsewhere")); }
        if (!Live(RetainedScenarioRoot) ||
            !DirectOwner(RetainedScenarioRoot, WorldRoot, WorldRoot))
        { return Fail(TEXT("Scenario root is not a live direct world child")); }

        TArray<FCk_Handle> Queue;
        TSet<FCk_Handle> Seen;
        auto AddRoot = [&](const FCk_Handle& Root) -> bool
        {
            if (!Live(Root) || !DirectOwner(Root, WorldRoot, WorldRoot) ||
                Seen.Contains(Root))
            { return false; }
            Seen.Add(Root);
            Queue.Add(Root);
            return true;
        };
        if (!AddRoot(RetainedScenarioRoot))
        { return Fail(TEXT("Scenario root duplicated")); }
        for (const auto& Root : RetainedRoots)
        {
            if (!AddRoot(Root))
            { return Fail(TEXT("Retained direct world root invalid or duplicated")); }
        }

        TMap<FCk_Handle, TSet<FCk_Handle>> ReverseChildren;
        for (int32 Index = 0; Index < Queue.Num(); ++Index)
        {
            const auto Parent = Queue[Index];
            if (!Live(Parent) || !SameRegistry(Parent, WorldRoot))
            { return Fail(TEXT("Retained parent ceased to be live in this world")); }

            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
                TryGet_AllocatedDirectLifetimeDependents(Parent, WorldRoot, Children))
            { return Fail(TEXT("Direct lifetime dependents could not be read")); }

            auto& Reverse = ReverseChildren.Add(Parent);
            if (Parent.Has<ck::FFragment_LifetimeDependents>())
            {
                TSet<FCk_Handle> RawCurrent;
                const auto& Raw = Parent.Get<ck::FFragment_LifetimeDependents,
                    ck::IsValid_Policy_IncludePendingKill>().Get_Entities();
                for (const auto& Child : Raw)
                {
                    if (!Allocated(Child)) { continue; } // historical reverse entry
                    if (!SameRegistry(Child, WorldRoot) ||
                        !Child.Has<ck::FFragment_LifetimeOwner>())
                    { return Fail(TEXT("Allocated raw reverse child is foreign or ownerless")); }
                    if (Child.Get<ck::FFragment_LifetimeOwner,
                            ck::IsValid_Policy_IncludePendingKill>().Get_Entity() != Parent)
                    { continue; } // transferred child, historical reverse entry
                    const bool ChildLive = Live(Child);
                    const bool ChildRepeated = RawCurrent.Contains(Child);
                    if (!ChildLive || ChildRepeated)
                    {
                        // IDs belong to the captured handles; no rejected-object reads.
                        return Fail(*FString::Printf(
                            TEXT("Allocated reverse member: parent=%llu child=%llu live=%d repeated=%d"),
                            static_cast<uint64>(Parent.Get_Entity().Get_ID()),
                            static_cast<uint64>(Child.Get_Entity().Get_ID()),
                            ChildLive, ChildRepeated));
                    }
                    RawCurrent.Add(Child);
                }
                if (RawCurrent.Num() != Children.Num())
                { return Fail(TEXT("Raw and filtered reverse dependents differ")); }
            }
            for (const auto& Child : Children)
            {
                if (!Live(Child) || !DirectOwner(Child, Parent, WorldRoot) ||
                    Reverse.Contains(Child) || Seen.Contains(Child))
                { return Fail(TEXT("Invalid, duplicate, or cyclic lifetime child")); }
                Reverse.Add(Child);
                Seen.Add(Child);
                Queue.Add(Child);
            }
        }

        bool InverseInvalid = false;
        TMap<FCk_Handle, TSet<FCk_Handle>> ActualChildren;
        const auto Registry = WorldRoot.Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        Registry.View<ck::FFragment_LifetimeOwner>().ForEach(
            [&](FCk_Entity Id, const ck::FFragment_LifetimeOwner& OwnerFragment)
            {
                const FCk_Handle Child{Id, RegistryHandle};
                if (!Allocated(Child)) { return; }
                const auto Owner = OwnerFragment.Get_Entity();
                if (!Seen.Contains(Owner)) { return; }
                if (!Live(Child) || !SameRegistry(Child, WorldRoot) ||
                    !Live(Owner) || Child == Owner || !Seen.Contains(Child))
                { InverseInvalid = true; return; }
                ActualChildren.FindOrAdd(Owner).Add(Child);
            });
        if (InverseInvalid) { return Fail(TEXT("Inverse owner census found an unclaimed child")); }

        for (const auto& Parent : Queue)
        {
            const auto* Reverse = ReverseChildren.Find(Parent);
            const auto* Actual = ActualChildren.Find(Parent);
            if (Reverse == nullptr) { return Fail(TEXT("Reverse owner set missing")); }
            if (Reverse->Num() != (Actual == nullptr ? 0 : Actual->Num()))
            { return Fail(TEXT("Lifetime reverse/owner census differs")); }
            for (const auto& Child : *Reverse)
            {
                if (Actual == nullptr || !Actual->Contains(Child))
                { return Fail(TEXT("Lifetime reverse/owner census differs")); }
            }
        }

        // Roots and every retained descendant stay live through the entire read.
        for (const auto& H : Queue)
        {
            if (!Live(H) || !SameRegistry(H, WorldRoot))
            { return Fail(TEXT("Census changed while reading")); }
        }
        if (!ExactWorldRoot(WorldRoot, World))
        { return Fail(TEXT("World transient root changed while reading")); }
        OutHandles = MoveTemp(Queue);
        return true;
    }
}
