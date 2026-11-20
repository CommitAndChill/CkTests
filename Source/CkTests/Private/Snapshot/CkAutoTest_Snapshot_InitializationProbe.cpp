#include "CkTests/Snapshot/CkAutoTest_Snapshot_InitializationProbe.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEntityTag/CkEntityTag_Fragment_Data.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGridInitialization_Data.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Blocker/CkHexGridBlocker_Utils.h"
#include "CkLabel/CkLabel_Utils.h"
#include "CkSnapshot/CkSnapshot_Utils.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#include <NativeGameplayTags.h>

namespace ck_autotest_initialization
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(ParentSlot, "Ck.AutoTest.Initialization.Parent");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(SavedSlot, "Ck.AutoTest.Initialization.Saved");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(NewSlot, "Ck.AutoTest.Initialization.New");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(FreshSlot, "Ck.AutoTest.Initialization.ExplicitFresh");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(ConstructionSeed, "Ck.AutoTest.Initialization.Seed");

    auto TryWrite(FCk_Handle InOwner) -> ECk_HexGridBlocker_Failure
    {
        auto Spec = FCk_HexGridBlocker_Spec{};
        Spec.Set_Grid(UCk_Utils_HexGrid_UE::Cast(InOwner)).Set_Cells({FCk_HexCoord{}}).Set_InitiallyActive(false);
        auto Blocker = FCk_Handle_HexGridBlocker{};
        auto Failure = ECk_HexGridBlocker_Failure::InvalidGrid;
        UCk_Utils_HexGridBlocker_UE::TryCreate(InOwner, Spec, Blocker, Failure);
        return Failure;
    }
}

UCk_AutoTest_Snapshot_InitializationProbe_UE::UCk_AutoTest_Snapshot_InitializationProbe_UE()
{
    _Replication = ECk_Replication::DoesNotReplicate;
    _InstancingPolicy = ECk_EntityScript_InstancingPolicy::InstancedPerEntity;
}

auto UCk_AutoTest_Snapshot_InitializationProbe_UE::Get_IsSnapshotRespawnable() const -> bool
{
    return true;
}

auto UCk_AutoTest_Snapshot_InitializationProbe_UE::Construct(
    FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow
{
    using namespace ck::persistence;
    using namespace ck_autotest_initialization;
    const auto* Slot = InSpawnParams.GetPtr<FCk_AutoTest_InitializationSlot>();
    const auto IsParent = Slot != nullptr && Slot->Label == ParentSlot;
    InHandle.Add<ck::FFragment_AutoTest_InitializationObservation>();

    if (NOT IsParent)
    {
        auto Identity = FInitializationIdentity{};
        if (Slot != nullptr)
        {
            Identity._Kind = Slot->ExplicitFresh ? EInitializationIdentity::Fresh : EInitializationIdentity::OwnedLabel;
            Identity._Owner = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(InHandle);
            Identity._Label = Slot->Label.ToString();
        }
        const auto Status = RequestInitialization(InHandle, FCk_SaveData_EntityTags::StaticStruct(), Identity);
        InHandle.Get<ck::FFragment_AutoTest_InitializationObservation>().DuringConstruct = Status;
        if (Slot)
        {
            auto Init = FCk_HexGridInitialization{};
            Init.Set_Kind(Slot->ExplicitFresh ? ECk_HexGridInitialization::Fresh : ECk_HexGridInitialization::PendingOwnedLabel);
            Init.Set_Label(Slot->Label);
            auto Grid = FCk_Handle_HexGrid{};
            auto Failure = ECk_HexBoardFailure::None;
            const auto Added = UCk_Utils_HexGrid_UE::TryAddForAuthoring(InHandle,
                FTransform::Identity, FCk_HexGrid_Spec{}, Grid, Failure, &Init);
            InHandle.Get<ck::FFragment_AutoTest_InitializationObservation>().ComposedBoard = Added;
            if (Added && Status.Get_Epoch() != 0)
            { InHandle.Get<ck::FFragment_AutoTest_InitializationObservation>().ConstructWrite = TryWrite(InHandle); }
        }
    }
    UCk_Utils_EntityTag_UE::Add_UsingGameplayTag(InHandle, ConstructionSeed);
    auto SelfDelegate = FCk_Delegate_EntityScript_Constructed{};
    SelfDelegate.BindDynamic(this, &ThisClass::OnSelfConstructed);
    auto PendingSelf = FCk_Handle_PendingEntityScript{InHandle};
    UCk_Utils_PendingEntityScript_UE::Promise_OnConstructed(PendingSelf, SelfDelegate);

    const auto SpawnChild = [&](FGameplayTag InLabel, bool InExplicitFresh = false)
    {
        auto Params = FCk_AutoTest_InitializationSlot{};
        Params.Label = InLabel;
        Params.ExplicitFresh = InExplicitFresh;
        auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
            InHandle, StaticClass(), FInstancedStruct::Make(Params), {});
        const auto Child = Pending.Get_EntityUnderConstruction();
        InHandle.Get<ck::FFragment_AutoTest_InitializationObservation>().Children.Add(Child);
        _PendingLabels.Add(Child, InLabel);
        auto Delegate = FCk_Delegate_EntityScript_Constructed{};
        Delegate.BindDynamic(this, &ThisClass::OnChildConstructed);
        UCk_Utils_PendingEntityScript_UE::Promise_OnConstructed(Pending, Delegate);
    };
    if (Slot == nullptr)
    {
        SpawnChild(ParentSlot);
    }
    else if (IsParent)
    {
        SpawnChild(SavedSlot);
        // Add two new children during reconstruction, to exercise adoption with no matching saved row.
        // Classification still goes through the production resolver in each child's own Construct.
        const auto* World = UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(InHandle);
        const auto* GameInstance = World ? World->GetGameInstance() : nullptr;
        const auto* Subsystem = GameInstance ? GameInstance->GetSubsystem<UCk_Snapshot_Subsystem_UE>() : nullptr;
        if (Subsystem && Subsystem->Get_IsLoadInProgress())
        {
            SpawnChild(NewSlot);
            SpawnChild(FreshSlot, true);
        }
    }
    return _PendingLabels.IsEmpty() ? ECk_EntityScript_ConstructionFlow::Finished : ECk_EntityScript_ConstructionFlow::Continue;
}

void UCk_AutoTest_Snapshot_InitializationProbe_UE::OnChildConstructed(FCk_Handle_EntityScript InChild)
{
    auto Label = FGameplayTag{};
    if (_PendingLabels.RemoveAndCopyValue(InChild, Label))
    {
        // The parent's adoption key is deliberately published only AFTER its children construct.
        UCk_Utils_GameplayLabel_UE::Add(InChild, Label);
        if (Label == ck_autotest_initialization::ParentSlot)
        {
            UCk_Utils_Snapshot_UE::Request_AssignSaveKey(InChild, TEXT("Ck.AutoTest.Initialization.KeyedOwner"));
            ck::persistence::RequestInitialization(InChild, FCk_SaveData_EntityTags::StaticStruct());
        }
        if (_PendingLabels.IsEmpty())
        {
            DoFinishConstruction();
        }
    }
}

void UCk_AutoTest_Snapshot_InitializationProbe_UE::OnSelfConstructed(FCk_Handle_EntityScript InEntity)
{
    auto& Observation = InEntity.Get<ck::FFragment_AutoTest_InitializationObservation>();
    Observation.OnConstructed = ck::persistence::Query(InEntity, FCk_SaveData_EntityTags::StaticStruct());
    Observation.ConstructedCallbackRan = true;
    if (Observation.ComposedBoard && Observation.OnConstructed.Get_Epoch() != 0)
    { Observation.ConstructedWrite = ck_autotest_initialization::TryWrite(InEntity); }
}
