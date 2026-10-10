#include "CkStateMachineGym_Runtime.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/Persistence/CkPersistenceHydration.h"
#include "CkRecord/Record/CkRecord_Utils.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#include "CkEcs/ContextOwner/CkContextOwner_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace ck_state_machine_gym_runtime
{
    bool IsCurrentWorldMember(const FCk_Handle& InEntity,
        const FCk_Handle& InWorldRoot, const FCk_Handle& InRequiredAncestor)
    {
        TSet<FCk_Handle> Seen;
        auto Current = InEntity;
        bool FoundAncestor = Current == InRequiredAncestor;
        int32 Depth = 0;
        while (Current != InWorldRoot)
        {
            if (++Depth > 256 || !ck::IsValid(Current) || Seen.Contains(Current) ||
                Current.Get_RegistryView().Get_RegistryHandle() !=
                    InWorldRoot.Get_RegistryView().Get_RegistryHandle() ||
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Current,
                    ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                !Current.Has<ck::FFragment_LifetimeOwner>())
            { return false; }
            Seen.Add(Current);
            Current = Current.Get<ck::FFragment_LifetimeOwner>().Get_Entity();
            FoundAncestor |= Current == InRequiredAncestor;
        }
        return FoundAncestor;
    }

    bool HasExactDnrNoDriver(const FCk_Handle& Entity,
        const FCk_Handle& WorldRoot)
    {
        if (!Entity.Has<ck::FFragment_Net_Params>() ||
            !WorldRoot.Has<ck::FFragment_Net_Params>()) { return false; }
        const auto& Actual = Entity.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings();
        const auto& World = WorldRoot.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings();
        return Actual.Get_Replication() == ECk_Replication::DoesNotReplicate &&
            Actual.Get_NetMode() == World.Get_NetMode() &&
            Actual.Get_NetRole() == ECk_Net_EntityNetRole::Authority &&
            Entity.Has<ck::FTag_HasAuthority>() &&
            !UCk_Utils_EntityReplicationDriver_UE::Has(Entity) &&
            !Entity.Has<TWeakObjectPtr<UCk_Ecs_ReplicatedObject_UE>>() &&
            !Entity.Has<ck::FRequest_EntityScript_Replicate>();
    }
}

auto
    UCk_StateMachineGym_Runtime_UE::
    TryGet_SourceContext(
        const FCk_Handle& InCallback, FCk_Handle& OutContext)
    -> bool
{
    // The input may alias OutContext; copy before clearing the failure output.
    const auto Callback = InCallback;
    OutContext = {};
    if (!IsInGameThread() || Callback == FCk_Handle{}) { return false; }
    const auto Registry = Callback.Get_RegistryView();
    if (ck::registry_table::Get_IsInParallelRegion(
            Registry.Get_RegistryHandle()) ||
        !ck::IsValid(Callback) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Callback,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        !Callback.Has<ck::FFragment_ContextOwner>())
    { return false; }

    const auto Context = Callback.Get<ck::FFragment_ContextOwner>().Get_Entity();
    if (!ck::IsValid(Context) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Context,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        Callback.Get_RegistryView().Get_RegistryHandle() !=
            Context.Get_RegistryView().Get_RegistryHandle())
    { return false; }

    const FCk_Handle WorldRoot{Registry.Get_TransientEntity(),
        Registry.Get_RegistryHandle()};
    if (!ck::IsValid(WorldRoot) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(WorldRoot,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        !WorldRoot.Has<TWeakObjectPtr<UWorld>>())
    { return false; }
    auto* const World = WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get();
    if (!IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown ||
        World->IsBeingCleanedUp() ||
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) !=
            WorldRoot ||
        !ck_state_machine_gym_runtime::IsCurrentWorldMember(
            Context, WorldRoot, Context) ||
        !ck_state_machine_gym_runtime::IsCurrentWorldMember(
            Callback, WorldRoot, Context))
    { return false; }

    OutContext = Context;
    return true;
}

auto
    UCk_StateMachineGym_Runtime_UE::
    TryGet_DirectPassChild(
        const FCk_Handle& InCallback, const FCk_Handle& InPrimary,
        FCk_Handle& OutPassChild)
    -> bool
{
    const auto Callback = InCallback;
    const auto Primary = InPrimary;
    OutPassChild = {};
    FCk_Handle Context;
    if (!TryGet_SourceContext(Callback, Context) || Context != Primary ||
        Callback == Primary) { return false; }

    const auto RegistryHandle = Primary.Get_RegistryView().Get_RegistryHandle();
    TSet<FCk_Handle> Seen;
    auto Current = Callback;
    for (int32 Depth = 0; Depth < 256; ++Depth)
    {
        if (!ck::IsValid(Current) || Seen.Contains(Current) ||
            Current.Get_RegistryView().Get_RegistryHandle() != RegistryHandle ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Current,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !Current.Has<ck::FFragment_LifetimeOwner>()) { return false; }
        Seen.Add(Current);
        const auto Parent = Current.Get<ck::FFragment_LifetimeOwner>().Get_Entity();
        if (!ck::IsValid(Parent) ||
            Parent.Get_RegistryView().Get_RegistryHandle() != RegistryHandle ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Parent,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Parent).Contains(Current))
        { return false; }
        if (Parent == Primary)
        {
            OutPassChild = Current;
            return true;
        }
        Current = Parent;
    }
    return false;
}

auto
    UCk_StateMachineGym_Runtime_UE::
    Get_IsDivergenceSourceClassNonReplicating(
        TSubclassOf<UCk_EntityScript_UE> InClass, bool bPrimary)
    -> bool
{
    if (!IsInGameThread() || !ck::IsValid(InClass)) { return false; }
    auto* const Class = InClass.Get();
    if (Class->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated |
            CLASS_NewerVersionExists)) { return false; }
    const auto& ExpectedPath = bPrimary
        ? TEXT("/Script/Angelscript.Ck_SmTest_DivergencePrimaryScript")
        : TEXT("/Script/Angelscript.Ck_SmTest_DivergencePassScript");
    if (Class->GetPathName() != ExpectedPath ||
        !Class->IsChildOf(bPrimary
            ? UCk_EntityScript_WithActor_UE::StaticClass()
            : UCk_GenericEntityScript_UE::StaticClass()) ||
        (!bPrimary && Class->IsChildOf(UCk_EntityScript_WithActor_UE::StaticClass())))
    { return false; }
    auto* const CDO = Cast<UCk_EntityScript_UE>(Class->GetDefaultObject(false));
    return ck::IsValid(CDO) &&
        !CDO->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        CDO->GetClass() == Class &&
        CDO->Get_EffectiveReplication() == ECk_Replication::DoesNotReplicate;
}

auto
    UCk_StateMachineGym_Runtime_UE::
    TryValidate_DivergencePrimary(
        const FCk_Handle& InPrimary, AActor* InActor)
    -> bool
{
    const auto Primary = InPrimary;
    if (!IsInGameThread() || Primary == FCk_Handle{} ||
        !ck::IsValid(InActor) ||
        InActor->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject |
            RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    FCk_Handle Context;
    if (!TryGet_SourceContext(Primary, Context) || Context != Primary)
    { return false; }
    const auto Registry = Primary.Get_RegistryView();
    const FCk_Handle WorldRoot{Registry.Get_TransientEntity(),
        Registry.Get_RegistryHandle()};
    if (!ck::IsValid(WorldRoot) ||
        !Primary.Has<ck::FFragment_LifetimeOwner>() ||
        Primary.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != WorldRoot ||
        !UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(WorldRoot).Contains(Primary) ||
        !Primary.Has<TWeakObjectPtr<UWorld>>()) { return false; }
    auto* const World = WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get();
    if (!IsValid(World) || World->GetNetMode() == NM_Client ||
        InActor->GetWorld() != World || !InActor->HasAuthority() ||
        Primary.Get<TWeakObjectPtr<UWorld>>().Get() != World ||
        !ck_state_machine_gym_runtime::HasExactDnrNoDriver(Primary, WorldRoot) ||
        !Primary.Has<ck::FFragment_OwningActor>())
    { return false; }
    auto* const Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Primary);
    if (!ck::IsValid(Script) ||
        !Get_IsDivergenceSourceClassNonReplicating(Script->GetClass(), true))
    { return false; }
    auto* const WithActor = Cast<UCk_EntityScript_WithActor_UE>(Script);
    return ck::IsValid(WithActor) &&
        WithActor->Get_OwningActor() == InActor &&
        UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Primary) == InActor &&
        UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(InActor) == Primary;
}

auto
    UCk_StateMachineGym_Runtime_UE::
    TryValidate_DivergencePass(
        const FCk_Handle& InPass, const FCk_Handle& InPrimary)
    -> bool
{
    const auto Pass = InPass;
    const auto Primary = InPrimary;
    FCk_Handle Direct;
    if (!TryGet_DirectPassChild(Pass, Primary, Direct) || Direct != Pass ||
        !Primary.Has<ck::FFragment_OwningActor>()) { return false; }
    auto* const Actor = UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Primary);
    if (!TryValidate_DivergencePrimary(Primary, Actor) ||
        Pass.Has<ck::FFragment_OwningActor>()) { return false; }
    const auto Registry = Pass.Get_RegistryView();
    const FCk_Handle WorldRoot{Registry.Get_TransientEntity(),
        Registry.Get_RegistryHandle()};
    if (!ck_state_machine_gym_runtime::HasExactDnrNoDriver(Pass, WorldRoot))
    { return false; }
    auto* const Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Pass);
    return ck::IsValid(Script) &&
        Get_IsDivergenceSourceClassNonReplicating(Script->GetClass(), false);
}


namespace ck_state_machine_gym_forest
{
    enum class ERole : uint8 { Primary, Machine, State, Task, Transition, Condition, Timer };

    struct FNode
    {
        FCk_Handle Handle;
        FCk_Handle Parent;
        ERole Role;
        int32 Depth = 0;
    };

    template <typename TRecord, typename TAppend>
    bool ReadDirectRecord(const FCk_Handle& Owner, TAppend&& Append)
    {
        if (!Owner.Has<TRecord>()) { return true; }
        using FReader = ck::TUtils_RecordOfEntities<TRecord>;
        TArray<typename FReader::RecordEntryMaybeTypeSafeHandle> Entries;
        if (!FReader::TryGet_DirectEntries(Owner, Entries)) { return false; }
        for (const auto& Entry : Entries)
        {
            const FCk_Handle Raw{Entry};
            if (Raw == FCk_Handle{}) { return false; }
            // The direct record retains historical handles; only physical absence
            // permits skipping an entry. Allocated pending-kill is rejected by Append.
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Raw))
            { continue; }
            if (!Append(Raw)) { return false; }
        }
        return true;
    }
}

auto
    UCk_StateMachineGym_Runtime_UE::
    TryCollect_DivergenceHealthyForest(
        const FCk_Handle& InPrimary, AActor* InActor,
        const TArray<FCk_Handle>& InPasses,
        TArray<FCk_Handle>& OutForest, TArray<FCk_Handle>& OutParents)
    -> bool
{
    const auto Primary = InPrimary;
    const auto Passes = InPasses;
    OutForest.Reset();
    OutParents.Reset();
    if (&OutForest == &OutParents || !TryValidate_DivergencePrimary(Primary, InActor) ||
        Passes.Num() < 1 || Passes.Num() > 2) { return false; }

    const auto Registry = Primary.Get_RegistryView();
    const auto RegistryHandle = Registry.Get_RegistryHandle();
    const FCk_Handle WorldRoot{Registry.Get_TransientEntity(), RegistryHandle};
    TArray<ck_state_machine_gym_forest::FNode> Nodes;
    TMap<FCk_Handle, int32> Seen;
    Nodes.Reserve(128);
    Nodes.Add({Primary, WorldRoot, ck_state_machine_gym_forest::ERole::Primary, 0});
    Seen.Add(Primary, 0);

    const auto Add = [&](const FCk_Handle& Child, const FCk_Handle& Parent,
        ck_state_machine_gym_forest::ERole Role, int32 Depth) -> bool
    {
        if (Child == FCk_Handle{} || Child == Primary || Depth > 256 ||
            Nodes.Num() >= 4096 ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child) ||
            !ck::IsValid(Child) ||
            Child.Get_RegistryView().Get_RegistryHandle() != RegistryHandle ||
            !Child.Has<ck::FFragment_LifetimeOwner>() ||
            Child.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Parent ||
            !UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Parent).Contains(Child))
        { return false; }
        FCk_Handle Context;
        if (!TryGet_SourceContext(Child, Context) || Context != Primary ||
            !ck_state_machine_gym_runtime::HasExactDnrNoDriver(Child, WorldRoot) ||
            Child.Has<ck::FFragment_OwningActor>() ||
            Child.Has<ck::FFragment_RecordOfEntityExtensions>())
        { return false; }
        if (const auto* Existing = Seen.Find(Child))
        {
            const auto& Prior = Nodes[*Existing];
            return Prior.Parent == Parent && Prior.Role == Role;
        }
        Seen.Add(Child, Nodes.Num());
        Nodes.Add({Child, Parent, Role, Depth});
        return true;
    };

    TSet<FCk_Handle> PassSet;
    for (const auto& Pass : Passes)
    {
        if (PassSet.Contains(Pass) ||
            !TryValidate_DivergencePass(Pass, Primary) ||
            !Pass.Has_All<ck::FFragment_Sm_Params, ck::FFragment_Sm>() ||
            Pass.Has<ck::FTag_Sm_IsSubMachine>()) { return false; }
        PassSet.Add(Pass);
        if (!Add(Pass, Primary, ck_state_machine_gym_forest::ERole::Machine, 1))
        { return false; }
    }

    for (int32 Index = 1; Index < Nodes.Num(); ++Index)
    {
        const auto Node = Nodes[Index];
        const auto H = Node.Handle;
        const auto Parent = Node.Parent;
        const auto NextDepth = Node.Depth + 1;
        using R = ck_state_machine_gym_forest::ERole;
        const auto AddRole = [&](const FCk_Handle& Child, R Role) -> bool
        { return Add(Child, H, Role, NextDepth); };
        const auto ReadTimers = [&]() -> bool
        {
            return ck_state_machine_gym_forest::ReadDirectRecord<ck::FFragment_RecordOfTimers>(
                H, [&](const FCk_Handle& Child)
                { return AddRole(Child, R::Timer); });
        };

        switch (Node.Role)
        {
            case R::Machine:
            {
                if (!H.Has_All<ck::FFragment_Sm_Params, ck::FFragment_Sm>())
                { return false; }
                if (Parent != Primary)
                {
                    if (!H.Has<ck::FTag_Sm_IsSubMachine>() ||
                        !Parent.Has<ck::FFragment_SmTask_SubStateMachine>() ||
                        FCk_Handle{Parent.Get<ck::FFragment_SmTask_SubStateMachine>()
                            .Get_SubStateMachineHandle()} != H)
                    { return false; }
                }
                if (!ck_state_machine_gym_forest::ReadDirectRecord<ck::FFragment_RecordOfSmStates>(
                    H, [&](const FCk_Handle& Child)
                    { return AddRole(Child, R::State); })) { return false; }
                const auto& Current = H.Get<ck::FFragment_Sm>();
                const FCk_Handle CurrentState{Current.Get_CurrentStateHandle()};
                if (CurrentState != FCk_Handle{} &&
                    !AddRole(CurrentState, R::State)) { return false; }
                if (H.Has<ck::FFragment_Sm_PendingTransition>())
                {
                    const FCk_Handle Previous{H.Get<ck::FFragment_Sm_PendingTransition>()
                        .Get_PreviousStateHandle()};
                    if (Previous != FCk_Handle{} &&
                        UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Previous) &&
                        !AddRole(Previous, R::State)) { return false; }
                }
                break;
            }
            case R::State:
            {
                if (!H.Has<ck::FFragment_SmState_Params>() ||
                    !ck::TUtils_Sm_OwningStateMachine::Has(H) ||
                    FCk_Handle{ck::TUtils_Sm_OwningStateMachine::Get_StoredEntity(H)} != Parent)
                { return false; }
                if (!ck_state_machine_gym_forest::ReadDirectRecord<ck::FFragment_RecordOfSmTasks>(
                    H, [&](const FCk_Handle& Child)
                    { return AddRole(Child, R::Task); }) ||
                    !ck_state_machine_gym_forest::ReadDirectRecord<ck::FFragment_RecordOfSmTransitions>(
                    H, [&](const FCk_Handle& Child)
                    { return AddRole(Child, R::Transition); })) { return false; }
                break;
            }
            case R::Task:
            {
                if (!H.Has_All<ck::FFragment_SmTask, ck::FFragment_SmTask_Params>() ||
                    H.Has<ck::FTag_SmScript_PendingAttach>() ||
                    H.Has<ck::FFragment_SmScript_PendingAttach>() ||
                    !ck::TUtils_Sm_ParentState::Has(H) ||
                    FCk_Handle{ck::TUtils_Sm_ParentState::Get_StoredEntity(H)} != Parent)
                { return false; }
                if (H.Has<ck::FFragment_SmTask_SubStateMachine>())
                {
                    const FCk_Handle Sub{H.Get<ck::FFragment_SmTask_SubStateMachine>()
                        .Get_SubStateMachineHandle()};
                    if (Sub == FCk_Handle{}) { return false; }
                    if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Sub) &&
                        !AddRole(Sub, R::Machine)) { return false; }
                }
                if (!ReadTimers()) { return false; }
                break;
            }
            case R::Transition:
            {
                if (!H.Has_All<ck::FFragment_SmTransition,
                        ck::FFragment_SmTransition_Params>() ||
                    !ck::TUtils_Sm_ParentState::Has(H) ||
                    FCk_Handle{ck::TUtils_Sm_ParentState::Get_StoredEntity(H)} != Parent)
                { return false; }
                if (!ck_state_machine_gym_forest::ReadDirectRecord<ck::FFragment_RecordOfSmConditions>(
                    H, [&](const FCk_Handle& Child)
                    { return AddRole(Child, R::Condition); })) { return false; }
                break;
            }
            case R::Condition:
            {
                if (!H.Has_All<ck::FFragment_SmCondition,
                        ck::FFragment_SmCondition_Params>() ||
                    H.Has<ck::FTag_SmScript_PendingAttach>() ||
                    H.Has<ck::FFragment_SmScript_PendingAttach>() ||
                    !ck::TUtils_Sm_ParentTransition::Has(H) ||
                    FCk_Handle{ck::TUtils_Sm_ParentTransition::Get_StoredEntity(H)} != Parent ||
                    !ReadTimers()) { return false; }
                break;
            }
            case R::Timer:
            {
                if (!H.Has_All<ck::FFragment_Timer_Params, ck::FFragment_Timer>() ||
                    H.Has<ck::FTag_Timer_NeedsSetup>()) { return false; }
                break;
            }
            default: return false;
        }
    }

    TArray<FCk_Handle> Known;
    Known.Reserve(Nodes.Num());
    for (const auto& Node : Nodes) { Known.Add(Node.Handle); }
    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
            Known, Known, WorldRoot)) { return false; }
    for (int32 Index = Nodes.Num() - 1; Index >= 0; --Index)
    {
        OutForest.Add(Nodes[Index].Handle);
        OutParents.Add(Nodes[Index].Parent);
    }
    return true;
}

namespace ck_state_machine_gym_retirement
{
    bool LiveActor(const AActor* Actor)
    {
        return IsValid(Actor) &&
            !Actor->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject |
                RF_BeginDestroyed | RF_FinishDestroyed);
    }

    bool ValidReturn(const UFunction* Function, const FProperty* Return)
    {
        if (!IsValid(Function) || !Return || Function->NumParms != 1 ||
            !Return->HasAnyPropertyFlags(CPF_ReturnParm)) { return false; }
        const int32 Offset = Return->GetOffset_ForUFunction();
        const int32 Size = Return->GetSize();
        return Offset >= 0 && Size > 0 && Offset <= Function->ParmsSize &&
            Size <= Function->ParmsSize - Offset;
    }

    template <typename TProperty, typename TValue>
    bool ReadScalar(AActor* Actor, const TCHAR* Name, TValue& Out)
    {
        auto* const Function = LiveActor(Actor)
            ? Actor->FindFunction(FName{Name}) : nullptr;
        auto* const Property = IsValid(Function)
            ? FindFProperty<TProperty>(Function, TEXT("ReturnValue")) : nullptr;
        if (!ValidReturn(Function, Property) || Property->GetSize() > sizeof(TValue))
        { return false; }
        FStructOnScope Args{Function};
        if (!Args.GetStructMemory()) { return false; }
        Actor->ProcessEvent(Function, Args.GetStructMemory());
        if (!LiveActor(Actor)) { return false; }
        Out = Property->GetPropertyValue_InContainer(Args.GetStructMemory());
        return true;
    }

    bool ReadHandle(AActor* Actor, const TCHAR* Name, FCk_Handle& Out)
    {
        Out = {};
        auto* const Function = LiveActor(Actor)
            ? Actor->FindFunction(FName{Name}) : nullptr;
        auto* const Property = IsValid(Function)
            ? FindFProperty<FStructProperty>(Function, TEXT("ReturnValue")) : nullptr;
        if (!ValidReturn(Function, Property) ||
            Property->Struct != FCk_Handle::StaticStruct() ||
            Property->GetSize() != sizeof(FCk_Handle)) { return false; }
        FStructOnScope Args{Function};
        if (!Args.GetStructMemory()) { return false; }
        Actor->ProcessEvent(Function, Args.GetStructMemory());
        if (!LiveActor(Actor)) { return false; }
        Out = *Property->ContainerPtrToValuePtr<FCk_Handle>(Args.GetStructMemory());
        return true;
    }

    bool ReadRoots(AActor* Actor, TArray<FCk_Handle>& Out)
    {
        Out.Reset();
        auto* const Function = LiveActor(Actor)
            ? Actor->FindFunction(TEXT("Get_SourceRoots")) : nullptr;
        auto* const Property = IsValid(Function)
            ? FindFProperty<FArrayProperty>(Function, TEXT("ReturnValue")) : nullptr;
        auto* const Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
        if (!ValidReturn(Function, Property) || !Inner ||
            Inner->Struct != FCk_Handle::StaticStruct() ||
            Inner->GetSize() != sizeof(FCk_Handle)) { return false; }
        FStructOnScope Args{Function};
        if (!Args.GetStructMemory()) { return false; }
        Actor->ProcessEvent(Function, Args.GetStructMemory());
        if (!LiveActor(Actor)) { return false; }
        FScriptArrayHelper Values{Property,
            Property->ContainerPtrToValuePtr<void>(Args.GetStructMemory())};
        if (Values.Num() < 1 || Values.Num() > 2) { return false; }
        for (int32 I = 0; I < Values.Num(); ++I)
        { Out.Add(*reinterpret_cast<const FCk_Handle*>(Values.GetRawPtr(I))); }
        return true;
    }

    bool SameRetainedReceipts(AActor* Actor, const FCk_Handle& Primary,
        const TArray<FCk_Handle>& Passes)
    {
        if (!LiveActor(Actor)) { return false; }
        const auto Path = Actor->GetClass()->GetPathName();
        if (Path != TEXT("/Script/Angelscript.Ck_SmTest_DivergenceFirstBranch_GymActor") &&
            Path != TEXT("/Script/Angelscript.Ck_SmTest_DivergenceTimed_GymActor"))
        { return false; }
        bool Closed = false;
        FCk_Handle RetainedPrimary;
        TArray<FCk_Handle> RetainedPasses;
        int32 Constructed = INDEX_NONE;
        return ReadScalar<FBoolProperty>(Actor, TEXT("Get_SourceClosed"), Closed) &&
            Closed &&
            ReadHandle(Actor, TEXT("Get_SourcePrimaryTarget"), RetainedPrimary) &&
            RetainedPrimary == Primary &&
            ReadRoots(Actor, RetainedPasses) &&
            RetainedPasses == Passes &&
            ReadScalar<FIntProperty>(Actor, TEXT("Get_SourceConstructedCount"),
                Constructed) &&
            Constructed == Passes.Num();
    }

    bool CheckWorld(const FCk_Handle& WorldRoot, const TWeakObjectPtr<UWorld>& World)
    {
        auto* const W = World.Get();
        if (!IsInGameThread() || !IsValid(W) || !W->IsGameWorld() ||
            W->bIsTearingDown || W->IsBeingCleanedUp() ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(WorldRoot) ||
            !ck::IsValid(WorldRoot) ||
            ck::registry_table::Get_IsInParallelRegion(
                WorldRoot.Get_RegistryView().Get_RegistryHandle()) ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(WorldRoot,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !WorldRoot.Has<TWeakObjectPtr<UWorld>>()) { return false; }
        return WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get() == W &&
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W) ==
                WorldRoot;
    }

    bool CheckFrozen(const TArray<FCk_Handle>& Forest,
        const TArray<FCk_Handle>& Parents, const FCk_Handle& WorldRoot,
        const TWeakObjectPtr<UWorld>& World, int32 RequestedCount)
    {
        if (!CheckWorld(WorldRoot, World) || Forest.Num() != Parents.Num() ||
            RequestedCount < 0 || RequestedCount > Forest.Num()) { return false; }
        const auto Registry = WorldRoot.Get_RegistryView().Get_RegistryHandle();
        for (int32 I = 0; I < Forest.Num(); ++I)
        {
            const auto H = Forest[I];
            const bool Allocated = UCk_EntityScriptGym_Spawn_Runtime_UE::
                Get_IsEntityAllocated(H);
            if (!Allocated)
            {
                if (I >= RequestedCount) { return false; }
                continue;
            }
            if (H.Get_RegistryView().Get_RegistryHandle() != Registry ||
                !H.Has<ck::FFragment_LifetimeOwner>()) { return false; }
            const auto Parent = Parents[I];
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Parent) ||
                Parent.Get_RegistryView().Get_RegistryHandle() != Registry ||
                H.Get<ck::FFragment_LifetimeOwner,
                    ck::IsValid_Policy_IncludePendingKill>().Get_Entity() != Parent)
            { return false; }
            if (H.Has<ck::FFragment_PendingHydration>() ||
                H.Has<ck::FTag_Hydration_Quarantine>()) { return false; }
            const bool Initiated = H.Has<ck::FTag_DestroyEntity_Initiate>();
            if (I < RequestedCount)
            {
                if (!Initiated) { return false; }
            }
            else if (!ck::IsValid(H) || Initiated ||
                !ck_state_machine_gym_runtime::HasExactDnrNoDriver(H, WorldRoot) ||
                (I + 1 < Forest.Num() && H.Has<ck::FFragment_OwningActor>()))
            { return false; }
        }
        return UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryProve_FrozenRetirementLedger(Forest, Forest, WorldRoot);
    }
}

auto UCk_StateMachineGym_Runtime_UE::TryRetire_DivergenceHealthySource(
    const FCk_Handle& InPrimary, AActor* InActor,
    const TArray<FCk_Handle>& InPasses) -> bool
{
    // Input arrays could be actor-owned storage; freeze values before callbacks.
    const auto Primary = InPrimary;
    const auto Passes = InPasses;
    if (!IsInGameThread() || !IsValid(InActor) ||
        !ck_state_machine_gym_retirement::SameRetainedReceipts(
            InActor, Primary, Passes) ||
        !TryValidate_DivergencePrimary(Primary, InActor)) { return false; }
    for (const auto& Pass : Passes)
    { if (!TryValidate_DivergencePass(Pass, Primary)) { return false; } }

    TArray<FCk_Handle> Forest;
    TArray<FCk_Handle> Parents;
    if (!TryCollect_DivergenceHealthyForest(
            Primary, InActor, Passes, Forest, Parents) ||
        Forest.Num() == 0 || Forest.Num() != Parents.Num() ||
        Forest.Last() != Primary) { return false; }
    const auto Registry = Primary.Get_RegistryView();
    const FCk_Handle WorldRoot{Registry.Get_TransientEntity(),
        Registry.Get_RegistryHandle()};
    TWeakObjectPtr<UWorld> World{InActor->GetWorld()};
    TWeakObjectPtr<AActor> Actor{InActor};
    TMap<FCk_Handle, int32> Indices;
    for (int32 I = 0; I < Forest.Num(); ++I)
    {
        if (Indices.Contains(Forest[I])) { return false; }
        Indices.Add(Forest[I], I);
    }
    for (int32 I = 0; I < Forest.Num(); ++I)
    {
        if (Forest[I] == Primary)
        { if (Parents[I] != WorldRoot) { return false; } }
        else
        {
            const auto* ParentIndex = Indices.Find(Parents[I]);
            if (!ParentIndex || *ParentIndex <= I) { return false; }
        }
    }
    if (!ck_state_machine_gym_retirement::CheckFrozen(
            Forest, Parents, WorldRoot, World, 0)) { return false; }

    for (int32 I = 0; I < Forest.Num(); ++I)
    {
        // Do not recollect: already initiated nodes fail live-only validators.
        if (!ck_state_machine_gym_retirement::CheckFrozen(
                Forest, Parents, WorldRoot, World, I)) { return false; }
        auto* const CurrentActor = Actor.Get();
        if (!IsValid(CurrentActor) || CurrentActor->GetWorld() != World.Get() ||
            !ck_state_machine_gym_retirement::SameRetainedReceipts(
                CurrentActor, Primary, Passes) ||
            !TryValidate_DivergencePrimary(Primary, CurrentActor)) { return false; }
        auto Target = Forest[I];
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Target);
        // Primary's callback may end the actor; do not dereference it again.
        if (!ck_state_machine_gym_retirement::CheckFrozen(
                Forest, Parents, WorldRoot, World, I + 1)) { return false; }
    }
    // Request initiation only. Physical deallocation needs a later observer.
    return true;
}
