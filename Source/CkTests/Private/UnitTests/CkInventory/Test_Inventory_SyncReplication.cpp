#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Persistence/CkPersistenceHandlerRegistry.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkGrid/2dGridSystem/Grid/Ck2dGridSystem_Utils.h"
#include "CkInventory/Inventory/CkInventory_Utils.h"
#include "CkInventory/Inventory/DataOnly/CkInventory_DataOnly_Fragment.h"
#include "CkInventory/Inventory/DataOnly/CkInventory_DataOnly_Utils.h"
#include "CkInventory/Inventory/Spatial/CkInventory_Spatial_Fragment.h"
#include "CkInventory/Inventory/Spatial/CkInventory_Spatial_Utils.h"
#include "CkInventory/Item/CkItem_Utils.h"
#include "CkInventory/ItemTrait/Dimensions/CkItemTrait_Dimensions.h"
#include "CkTests/CkTests_Fragment_Data.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "InventorySyncReplication_TestTypes.h"

#include "Engine/World.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

namespace ck_test_inventory_sync
{
    constexpr auto WaitSeconds = 10.0;
    const auto SourceCoordinate = FIntPoint{0, 0};
    const auto TargetCoordinate = FIntPoint{2, 2};

    enum class EOrder : uint8
    {
        Live,
        DestroyInitiated,
        FullyReclaimed,
        TargetFirstTransfer,
        LiveDisconnectedBatch,
        DuplicateRemovalProjection
    };

    enum class EEndpoint : uint8
    {
        Source,
        Target
    };

    struct FIncomingDelta
    {
        EEndpoint _Endpoint = EEndpoint::Source;
        TArray<FCk_Handle_Item> _Current;
        TOptional<TArray<FCk_Handle_Item>> _Previous;
    };

    struct FCase
    {
        bool _Spatial = false;
        EOrder _Order = EOrder::Live;
    };

    struct FState
    {
        FCase _Case;
        FCk_Handle _Root;
        FCk_Handle _SourceOwner;
        FCk_Handle _TargetOwner;
        FCk_Handle_Inventory _Source;
        FCk_Handle_Inventory _Target;
        FCk_Handle_Item _Item;
        FCk_Handle_Item _SecondaryItem;
        TArray<FCk_Handle_Item> _SourceItems;
        TStrongObjectPtr<UCk_InventoryItem_Definition> _Definition;
        TStrongObjectPtr<UCk_InventorySyncReplication_Listener_UE> _SourceListener;
        TStrongObjectPtr<UCk_InventorySyncReplication_Listener_UE> _TargetListener;

        ~FState()
        {
            if (ck::IsValid(_Root))
            {
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(_Root);
            }
        }
    };

    auto
        HasPendingSync(
            const FState& InState,
            const FCk_Handle_Inventory& InInventory)
        -> bool
    {
        return InState._Case._Spatial
            ? InInventory.Has<ck::FFragment_Inventory_Spatial_SyncReplication>()
            : InInventory.Has<ck::FFragment_Inventory_DataOnly_SyncReplication>();
    }

    auto
        RawContains(
            const FCk_Handle_Inventory& InInventory,
            const FCk_Handle_Item& InItem)
        -> bool
    {
        return UCk_Utils_Inventory_UE::RecordOfInventoryItems_Utils::Get_Entries(InInventory).Contains(InItem);
    }

    auto
        HasPendingRotation(
            const FCk_Handle_Item& InItem)
        -> bool
    {
        return ck::IsValid(InItem) && InItem.Has<ck::FFragment_Transform_Requests>()
            && NOT InItem.Get<ck::FFragment_Transform_Requests>().Get_RotationRequests().IsEmpty();
    }

    auto
        GridContainsIdentity(
            const FCk_Handle_Inventory& InInventory,
            const FCk_Handle_Item& InItem)
        -> bool
    {
        const auto Spatial = UCk_Utils_Inventory_Spatial_UE::CastChecked(InInventory);
        const auto Grid = UCk_Utils_Inventory_Spatial_UE::Get_Grid(Spatial);
        auto Found = false;
        UCk_Utils_2dGridSystem_UE::ForEach_Cell(Grid, ECk_2dGridSystem_CellFilter::OnlyActiveCells,
            [&Found, &InItem](FCk_Handle_2dGridCell InCell)
            {
                Found |= ck::TUtils_InventorySlot_ItemRef::Get_StoredEntity(InCell) == InItem;
            });
        return Found;
    }

    auto
        CheckReceiverContract(
            FAutomationTestBase& InTest,
            const FCk_Handle& InEntity)
        -> bool
    {
        if (NOT InTest.TestTrue(TEXT("Receiver fixture entity is live"), ck::IsValid(InEntity)))
        { return false; }
        if (NOT InTest.TestTrue(TEXT("Receiver fixture entity inherits complete network settings"),
            UCk_Utils_Net_UE::Has(InEntity)))
        { return false; }
        if (NOT InTest.TestEqual(TEXT("Receiver fixture entity does not originate network replication"),
            UCk_Utils_Net_UE::Get_EntityReplication(InEntity), ECk_Replication::DoesNotReplicate))
        { return false; }
        if (NOT InTest.TestTrue(TEXT("Receiver fixture entity has the client network mode"),
            UCk_Utils_Net_UE::Get_IsEntityNetMode_Client(InEntity)))
        { return false; }
        return InTest.TestFalse(TEXT("Receiver fixture entity has no authority"),
            UCk_Utils_Net_UE::Get_HasAuthority(InEntity));
    }

    auto
        ApplyItems(
            FAutomationTestBase& InTest,
            FState& InState,
            const FIncomingDelta& InDelta)
        -> bool
    {
        auto& Owner = InDelta._Endpoint == EEndpoint::Target ? InState._TargetOwner : InState._SourceOwner;
        if (NOT InTest.TestTrue(TEXT("NetApply owner is a live production-composed entity"), ck::IsValid(Owner)))
        { return false; }
        const auto* PayloadType = InState._Case._Spatial
            ? FCk_RepData_Inventory_Spatial_NamedItems::StaticStruct()
            : FCk_RepData_Inventory_DataOnly_NamedItems::StaticStruct();
        const auto* Handler = FCk_PersistenceHandlerRegistry::Find(PayloadType);
        if (NOT InTest.TestTrue(TEXT("Registered production net handler exists"), Handler != nullptr && static_cast<bool>(Handler->NetApply)))
        { return false; }
        const auto MakePayload = [&InState, &InDelta](const TArray<FCk_Handle_Item>& InItems) -> FInstancedStruct
        {
            if (InState._Case._Spatial)
            {
                auto Data = FCk_RepData_Inventory_Spatial_NamedItems{};
                auto Snapshot = FCk_Inventory_Spatial_ReplicatedSnapshot{};
                const auto Inventory = InDelta._Endpoint == EEndpoint::Target ? InState._Target : InState._Source;
                Snapshot.InventoryName = Inventory.Get<ck::FFragment_Inventory_Params>().Get_Name();
                for (const auto& Item : InItems)
                {
                    auto Entry = FCk_InventoryItem_Spatial_ReplicatedEntry{Item};
                    const auto SourceCell = Item == InState._SecondaryItem ? FIntPoint{1, 0} : SourceCoordinate;
                    Entry.Set_Coordinate(InDelta._Endpoint == EEndpoint::Target ? TargetCoordinate : SourceCell);
                    Entry.Set_Rotation(InDelta._Endpoint == EEndpoint::Target
                        ? ECk_CardinalRotation::Quarter : ECk_CardinalRotation::None);
                    Snapshot.Items.Add(Entry);
                }
                Data.Inventories.Add(MoveTemp(Snapshot));
                return FInstancedStruct::Make(Data);
            }
            auto Data = FCk_RepData_Inventory_DataOnly_NamedItems{};
            auto Snapshot = FCk_Inventory_DataOnly_ReplicatedSnapshot{};
            const auto Inventory = InDelta._Endpoint == EEndpoint::Target ? InState._Target : InState._Source;
            Snapshot.InventoryName = Inventory.Get<ck::FFragment_Inventory_Params>().Get_Name();
            for (const auto& Item : InItems)
            { Snapshot.Items.Emplace(Item); }
            Data.Inventories.Add(MoveTemp(Snapshot));
            return FInstancedStruct::Make(Data);
        };
        const auto NewData = MakePayload(InDelta._Current);
        const auto Previous = InDelta._Previous.IsSet()
            ? TOptional<FInstancedStruct>{MakePayload(InDelta._Previous.GetValue())} : TOptional<FInstancedStruct>{};
        return InTest.TestEqual(TEXT("Registered NetApply accepts the incoming complete item snapshot"),
            Handler->NetApply(Owner, NewData, Previous), ECk_Persistence_ApplyResult::Applied);
    }

    auto
        BindDelta(
            FCk_Handle_Inventory& InInventory,
            const TStrongObjectPtr<UCk_InventorySyncReplication_Listener_UE>& InListener)
        -> void
    {
        auto Delegate = FCk_Delegate_Inventory_OnItemsChanged{};
        Delegate.BindDynamic(InListener.Get(), &UCk_InventorySyncReplication_Listener_UE::OnItemsChanged);
        UCk_Utils_Inventory_UE::BindTo_OnItemsChanged(InInventory, Delegate,
            ECk_Signal_BindingPolicy::IgnorePayloadInFlight, ECk_Signal_PostFireBehavior::DoNothing);
    }

    auto
        UnbindDelta(
            FCk_Handle_Inventory& InInventory,
            const TStrongObjectPtr<UCk_InventorySyncReplication_Listener_UE>& InListener)
        -> void
    {
        auto Delegate = FCk_Delegate_Inventory_OnItemsChanged{};
        Delegate.BindDynamic(InListener.Get(), &UCk_InventorySyncReplication_Listener_UE::OnItemsChanged);
        UCk_Utils_Inventory_UE::UnbindFrom_OnItemsChanged(InInventory, Delegate);
    }

    auto
        CheckDelta(
            FAutomationTestBase& InTest,
            const FCk_Handle_Inventory& InInventory,
            const UCk_InventorySyncReplication_Listener_UE& InListener,
            const FCk_Handle_Item& InItem,
            bool InAdded)
        -> void
    {
        if (NOT InTest.TestEqual(TEXT("Exactly one inventory delta is published"), InListener._Deltas.Num(), 1))
        { return; }
        const auto& Delta = InListener._Deltas[0];
        InTest.TestTrue(TEXT("Delta names its source inventory identity"), Delta._Inventory == InInventory);
        InTest.TestTrue(TEXT("Delta preserves the exact item identity and has no opposite mutation"),
            InAdded ? Delta._Added == TArray<FCk_Handle_Item>{InItem} && Delta._Removed.IsEmpty()
                : Delta._Removed == TArray<FCk_Handle_Item>{InItem} && Delta._Added.IsEmpty());
    }
}

namespace ck_test_inventory_sync
{
    auto MakeNamedBatch(const FState& State, const TArray<FCk_Handle_Item>& SourceItems,
        const TArray<FCk_Handle_Item>& TargetItems, bool DuplicateNames = false) -> FInstancedStruct
    {
        const auto SourceName = State._Source.Get<ck::FFragment_Inventory_Params>().Get_Name();
        const auto TargetName = DuplicateNames ? SourceName : State._Target.Get<ck::FFragment_Inventory_Params>().Get_Name();
        if (State._Case._Spatial)
        {
            auto Data = FCk_RepData_Inventory_Spatial_NamedItems{};
            for (auto Index = 0; Index < 2; ++Index)
            {
                auto Row = FCk_Inventory_Spatial_ReplicatedSnapshot{};
                Row.InventoryName = Index == 0 ? SourceName : TargetName;
                const auto& Items = Index == 0 ? SourceItems : TargetItems;
                for (const auto& Item : Items)
                {
                    auto Entry = FCk_InventoryItem_Spatial_ReplicatedEntry{Item};
                    Entry.Set_Coordinate(Index == 0 ? FIntPoint{0, 0}
                        : Item == State._SecondaryItem ? FIntPoint{3, 2} : FIntPoint{2, 2});
                    Row.Items.Add(Entry);
                }
                Data.Inventories.Add(MoveTemp(Row));
            }
            return FInstancedStruct::Make(Data);
        }
        auto Data = FCk_RepData_Inventory_DataOnly_NamedItems{};
        auto Source = FCk_Inventory_DataOnly_ReplicatedSnapshot{};
        Source.InventoryName = SourceName;
        for (const auto& Item : SourceItems) { Source.Items.Emplace(Item); }
        auto Target = FCk_Inventory_DataOnly_ReplicatedSnapshot{};
        Target.InventoryName = TargetName;
        for (const auto& Item : TargetItems) { Target.Items.Emplace(Item); }
        Data.Inventories = {MoveTemp(Source), MoveTemp(Target)};
        return FInstancedStruct::Make(Data);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkInventoryNamedSnapshotAdmissionTest,
    "Ck.Inventory.Net.NamedSiblingSnapshots.AdmissionAndQueuedReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkInventoryNamedSnapshotAdmissionTest::RunTest(const FString&) -> bool
{
    using namespace ck_test_inventory_sync;
    // Each of the two shape cases emits both the native ensure and the
    // editor message-log diagnostic from this latent action's editor context.
    AddExpectedError(TEXT("Inventory named snapshot repeats an item identity"), EAutomationExpectedErrorFlags::Contains, 4);
    AddExpectedError(TEXT("Inventory named snapshot has an invalid or repeated inventory name"), EAutomationExpectedErrorFlags::Contains, 4);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    for (const auto Spatial : {false, true})
    {
        const auto State = MakeShared<FState>();
        State->_Case._Spatial = Spatial;
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
            {
                State->_Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
                UCk_Utils_Net_UE::Add(State->_Root, FCk_Net_ConnectionSettings{
                    ECk_Replication::DoesNotReplicate, ECk_Net_NetModeType::Client, ECk_Net_EntityNetRole::Proxy});
                State->_SourceOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->_Root);
                State->_TargetOwner = State->_SourceOwner;
                const auto MakeInventory = [State, World](FGameplayTag Name) -> FCk_Handle_Inventory
                {
                    if (State->_Case._Spatial)
                    {
                        return UCk_Utils_Inventory_Spatial_UE::Add(State->_SourceOwner,
                            FCk_Inventory_Spatial_Spec{Name, FIntPoint{4, 4}}, ECk_Replication::DoesNotReplicate, World);
                    }
                    return UCk_Utils_Inventory_DataOnly_UE::Add(State->_SourceOwner,
                        UCk_Utils_Inventory_DataOnly_UE::Make_Params(Name, {}, {}), ECk_Replication::DoesNotReplicate, World);
                };
                State->_Source = MakeInventory(TAG_Inventory_AutoTest_Net.GetTag());
                State->_Target = MakeInventory(TAG_Inventory_AutoTest_Net_Spatial.GetTag());
                auto* Dimensions = NewObject<UCk_ItemTrait_Dimensions>(GetTransientPackage());
                State->_Definition.Reset(UCk_Utils_Item_UE::GetOrCreate_TransientItemDefinition(GetTransientPackage(),
                    FName{*FGuid::NewGuid().ToString(EGuidFormats::Digits)},
                    FCk_InventoryItem_CoreInfo{FText::FromString(TEXT("Named snapshot fixture"))}, {Dimensions}));
                auto First = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->_Root);
                auto Second = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->_Root);
                State->_Item = UCk_Utils_Item_UE::Add(First, State->_Definition.Get());
                State->_SecondaryItem = UCk_Utils_Item_UE::Add(Second, State->_Definition.Get());
                State->_SourceListener.Reset(NewObject<UCk_InventorySyncReplication_Listener_UE>());
                State->_TargetListener.Reset(NewObject<UCk_InventorySyncReplication_Listener_UE>());
                BindDelta(State->_Source, State->_SourceListener);
                BindDelta(State->_Target, State->_TargetListener);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            {
                return ck::IsValid(State->_Source) && ck::IsValid(State->_Target)
                    && ck::IsValid(State->_Item) && ck::IsValid(State->_SecondaryItem)
                    && UCk_Utils_2dGridSystem_UE::Has(State->_Item)
                    && UCk_Utils_2dGridSystem_UE::Has(State->_SecondaryItem);
            }), WaitSeconds, TEXT("Named sibling production composition finishes")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*)
            {
                const auto* Type = State->_Case._Spatial ? FCk_RepData_Inventory_Spatial_NamedItems::StaticStruct()
                    : FCk_RepData_Inventory_DataOnly_NamedItems::StaticStruct();
                const auto* Handler = FCk_PersistenceHandlerRegistry::Find(Type);
                if (NOT TestTrue(TEXT("Named wire handler exists"), Handler && Handler->NetApply)) { return; }
                const auto Initial = MakeNamedBatch(*State, {State->_Item}, {State->_SecondaryItem});
                TestEqual(TEXT("Initial named snapshot stages"), Handler->NetApply(State->_SourceOwner, Initial, {}), ECk_Persistence_ApplyResult::Applied);
                const auto Latest = MakeNamedBatch(*State, {}, {State->_Item, State->_SecondaryItem});
                TestEqual(TEXT("Newer snapshot replaces pending target before consumption"),
                    Handler->NetApply(State->_SourceOwner, Latest, TOptional<FInstancedStruct>{Initial}), ECk_Persistence_ApplyResult::Applied);
                const auto Unresolved = MakeNamedBatch(*State, {State->_Item}, {FCk_Handle_Item{}});
                TestEqual(TEXT("Unresolved sibling rejects whole batch without replacing existing queues"),
                    Handler->NetApply(State->_SourceOwner, Unresolved, {}), ECk_Persistence_ApplyResult::NotReady);
                const auto DuplicateItem = MakeNamedBatch(*State, {State->_Item}, {State->_Item});
                TestEqual(TEXT("Duplicate cross-sibling item identity rejects whole batch"),
                    Handler->NetApply(State->_SourceOwner, DuplicateItem, {}), ECk_Persistence_ApplyResult::Rejected);
                const auto DuplicateName = MakeNamedBatch(*State, {State->_Item}, {State->_SecondaryItem}, true);
                TestEqual(TEXT("Duplicate sibling name rejects whole batch"),
                    Handler->NetApply(State->_SourceOwner, DuplicateName, {}), ECk_Persistence_ApplyResult::Rejected);
                TestTrue(TEXT("Admission and rejected batches do not mutate either record"),
                    UCk_Utils_Inventory_UE::Get_Items(State->_Source).IsEmpty()
                    && UCk_Utils_Inventory_UE::Get_Items(State->_Target).IsEmpty());
                TestTrue(TEXT("Admission publishes no partial downstream deltas"),
                    State->_SourceListener->_Deltas.IsEmpty() && State->_TargetListener->_Deltas.IsEmpty());
                if (State->_Case._Spatial)
                {
                    const auto& Source = State->_Source.Get<ck::FFragment_Inventory_Spatial_SyncReplication>();
                    const auto& Target = State->_Target.Get<ck::FFragment_Inventory_Spatial_SyncReplication>();
                    TestTrue(TEXT("Rejected batches preserve newest current and earliest empty previous"),
                        Source.Get_ItemsToReplicate().IsEmpty() && Source.Get_ItemsToReplicate_Previous().IsEmpty()
                        && Target.Get_ItemsToReplicate().Num() == 2 && Target.Get_ItemsToReplicate_Previous().IsEmpty());
                }
                else
                {
                    const auto& Source = State->_Source.Get<ck::FFragment_Inventory_DataOnly_SyncReplication>();
                    const auto& Target = State->_Target.Get<ck::FFragment_Inventory_DataOnly_SyncReplication>();
                    TestTrue(TEXT("Rejected batches preserve newest current and earliest empty previous"),
                        Source.Get_ItemsToReplicate().IsEmpty() && Source.Get_ItemsToReplicate_Previous().IsEmpty()
                        && Target.Get_ItemsToReplicate().Num() == 2 && Target.Get_ItemsToReplicate_Previous().IsEmpty());
                }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            {
                return NOT HasPendingSync(*State, State->_Source) && NOT HasPendingSync(*State, State->_Target)
                    && UCk_Utils_Inventory_UE::Get_NumItems(State->_Source) == 0
                    && UCk_Utils_Inventory_UE::Get_NumItems(State->_Target) == 2;
            }), WaitSeconds, TEXT("Only newest named target is consumed into exact sibling memberships")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*)
            {
                TestTrue(TEXT("Both item parents point only at the destination sibling"),
                    UCk_Utils_Item_UE::Get_ParentInventory(State->_Item) == State->_Target
                    && UCk_Utils_Item_UE::Get_ParentInventory(State->_SecondaryItem) == State->_Target);
                if (State->_Case._Spatial)
                {
                    TestTrue(TEXT("Spatial destination cells contain both exact identities"),
                        GridContainsIdentity(State->_Target, State->_Item) && GridContainsIdentity(State->_Target, State->_SecondaryItem));
                }
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkInventorySyncReplicationLifecycleTest,
    "Ck.Inventory.Net.SyncReplication.RemovalLifecycleAndTargetFirstTransfer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkInventorySyncReplicationLifecycleTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_inventory_sync;
    AddExpectedError(TEXT("Does the RecordEntry exist in the Record?"),
        EAutomationExpectedErrorFlags::Contains, 2);
    AddExpectedError(TEXT("replication removal repeats an item identity"),
        EAutomationExpectedErrorFlags::Contains, 2);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));

    const FCase Cases[] = {
        {false, EOrder::Live}, {false, EOrder::DestroyInitiated},
        {false, EOrder::FullyReclaimed}, {false, EOrder::TargetFirstTransfer},
        {false, EOrder::LiveDisconnectedBatch}, {false, EOrder::DuplicateRemovalProjection},
        {true, EOrder::Live}, {true, EOrder::DestroyInitiated},
        {true, EOrder::FullyReclaimed}, {true, EOrder::TargetFirstTransfer},
        {true, EOrder::LiveDisconnectedBatch}, {true, EOrder::DuplicateRemovalProjection}};
    for (const auto Case : Cases)
    {
        const auto State = MakeShared<FState>();
        State->_Case = Case;
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld)
            {
                AddInfo(FString::Printf(TEXT("SyncReplication case: spatial=%d removalOrder=%d"),
                    State->_Case._Spatial, static_cast<int32>(State->_Case._Order)));
                TestEqual(TEXT("Fixture runs in a real production client world"), InWorld->GetNetMode(), NM_Client);
                State->_Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
                if (NOT TestTrue(TEXT("Receiver lifetime root exists before network composition"), ck::IsValid(State->_Root)))
                { return; }
                UCk_Utils_Net_UE::Add(State->_Root, FCk_Net_ConnectionSettings{
                    ECk_Replication::DoesNotReplicate, ECk_Net_NetModeType::Client, ECk_Net_EntityNetRole::Proxy});
                State->_SourceOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->_Root);
                State->_TargetOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->_Root);
                const auto MakeInventory = [State, InWorld](FCk_Handle& InOwner) -> FCk_Handle_Inventory
                {
                    if (State->_Case._Spatial)
                    {
                        return UCk_Utils_Inventory_Spatial_UE::Add(InOwner,
                            FCk_Inventory_Spatial_Spec{TAG_Inventory_AutoTest_Net_Spatial.GetTag(), FIntPoint{4, 4}},
                            ECk_Replication::DoesNotReplicate, InWorld);
                    }
                    return UCk_Utils_Inventory_DataOnly_UE::Add(InOwner,
                        UCk_Utils_Inventory_DataOnly_UE::Make_Params(TAG_Inventory_AutoTest_Net.GetTag(), {}, {}),
                        ECk_Replication::DoesNotReplicate, InWorld);
                };
                State->_Source = MakeInventory(State->_SourceOwner);
                State->_Target = MakeInventory(State->_TargetOwner);
                auto* Dimensions = NewObject<UCk_ItemTrait_Dimensions>(GetTransientPackage());
                State->_Definition.Reset(UCk_Utils_Item_UE::GetOrCreate_TransientItemDefinition(GetTransientPackage(),
                    FName{*FGuid::NewGuid().ToString(EGuidFormats::Digits)},
                    FCk_InventoryItem_CoreInfo{FText::FromString(TEXT("Sync lifecycle fixture"))}, {Dimensions}));
                auto ItemEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->_Root);
                State->_Item = UCk_Utils_Item_UE::Add(ItemEntity, State->_Definition.Get());
                State->_SourceItems.Add(State->_Item);
                if (State->_Case._Order == EOrder::LiveDisconnectedBatch)
                {
                    auto SecondaryEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->_Root);
                    State->_SecondaryItem = UCk_Utils_Item_UE::Add(SecondaryEntity, State->_Definition.Get());
                    State->_SourceItems.Add(State->_SecondaryItem);
                }
                State->_SourceListener.Reset(NewObject<UCk_InventorySyncReplication_Listener_UE>());
                State->_TargetListener.Reset(NewObject<UCk_InventorySyncReplication_Listener_UE>());
                TestTrue(TEXT("Both inventories and the real definition-composed item exist"),
                    ck::IsValid(State->_Source) && ck::IsValid(State->_Target) && ck::IsValid(State->_Item));
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            {
                if (ck::Is_NOT_Valid(State->_Source) || ck::Is_NOT_Valid(State->_Target))
                { return false; }
                for (const auto& Item : State->_SourceItems)
                {
                    if (ck::Is_NOT_Valid(Item) || NOT UCk_Utils_2dGridSystem_UE::Has(Item))
                    { return false; }
                }
                return NOT State->_SourceItems.IsEmpty();
            }), WaitSeconds, TEXT("Public dimensions-trait construction and inventory composition finish")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*)
            {
                if (NOT CheckReceiverContract(*this, State->_Root)
                    || NOT CheckReceiverContract(*this, State->_SourceOwner)
                    || NOT CheckReceiverContract(*this, State->_TargetOwner)
                    || NOT CheckReceiverContract(*this, State->_Source)
                    || NOT CheckReceiverContract(*this, State->_Target))
                { return; }
                for (const auto& Item : State->_SourceItems)
                {
                    if (NOT CheckReceiverContract(*this, Item))
                    { return; }
                }
                ApplyItems(*this, *State, FIncomingDelta{
                    ._Endpoint = EEndpoint::Source, ._Current = State->_SourceItems});
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            {
                if (ck::Is_NOT_Valid(State->_Source) || HasPendingSync(*State, State->_Source))
                { return false; }
                for (const auto& Item : State->_SourceItems)
                {
                    if (ck::Is_NOT_Valid(Item) || NOT RawContains(State->_Source, Item)
                        || UCk_Utils_Item_UE::Get_ParentInventory(Item) != State->_Source
                        || (State->_Case._Spatial && NOT GridContainsIdentity(State->_Source, Item)))
                    { return false; }
                }
                return true;
            }), WaitSeconds, TEXT("Registered initial addition publishes membership, parent and spatial cells")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*)
            {
                BindDelta(State->_Source, State->_SourceListener);
                BindDelta(State->_Target, State->_TargetListener);
                if (State->_Case._Order == EOrder::TargetFirstTransfer)
                {
                    ApplyItems(*this, *State, FIncomingDelta{
                        ._Endpoint = EEndpoint::Target, ._Current = {State->_Item}});
                }
                else if (State->_Case._Order == EOrder::FullyReclaimed)
                {
                    auto Item = State->_Item.ConvertToHandle();
                    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Item);
                }
                else if (State->_Case._Order == EOrder::LiveDisconnectedBatch)
                {
                    auto Source = State->_Source.ConvertToHandle();
                    auto CorruptItem = State->_SecondaryItem;
                    TestTrue(TEXT("Public disconnect creates the live inventory/replication membership mismatch"),
                        UCk_Utils_Inventory_UE::RecordOfInventoryItems_Utils::Request_Disconnect(Source, CorruptItem, {}));
                    TestTrue(TEXT("A live valid item still precedes the disconnected live item in the incoming old snapshot"),
                        ck::IsValid(State->_Item) && ck::IsValid(CorruptItem)
                            && RawContains(State->_Source, State->_Item) && NOT RawContains(State->_Source, CorruptItem));
                }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            {
                if (State->_Case._Order == EOrder::FullyReclaimed)
                { return ck::Is_NOT_Valid(State->_Item.ConvertToHandle(), ck::IsValid_Policy_IncludePendingKill{}); }
                if (State->_Case._Order == EOrder::TargetFirstTransfer)
                {
                    if (HasPendingSync(*State, State->_Target) || HasPendingRotation(State->_Item)
                        || State->_TargetListener->_Deltas.IsEmpty()
                        || NOT RawContains(State->_Target, State->_Item)
                        || UCk_Utils_Item_UE::Get_ParentInventory(State->_Item) != State->_Target)
                    { return false; }
                    if (State->_Case._Spatial)
                    {
                        const auto Transform = UCk_Utils_Transform_UE::CastChecked(State->_Item);
                        return GridContainsIdentity(State->_Target, State->_Item)
                            && FMath::IsNearlyEqual(UCk_Utils_Transform_UE::Get_EntityCurrentRotation(Transform).Yaw, 90.0, 1.0e-3);
                    }
                }
                return true;
            }), WaitSeconds, TEXT("Removal starts after the requested destruction or target-first admission state")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*)
            {
                if (State->_Case._Order == EOrder::FullyReclaimed)
                {
                    TestFalse(TEXT("Production EndPlay already removed the record's forward link"),
                        RawContains(State->_Source, State->_Item));
                }
                else
                {
                    TestTrue(TEXT("Old snapshot still names a real source record entry before removal"),
                        RawContains(State->_Source, State->_Item));
                }
                if (State->_Case._Order == EOrder::DestroyInitiated)
                {
                    auto Item = State->_Item.ConvertToHandle();
                    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Item);
                    TestTrue(TEXT("Destroy initiation is real and the entity has not been reclaimed"),
                        ck::IsValid(Item, ck::IsValid_Policy_IncludePendingKill{})
                            && Item.Has<ck::FTag_DestroyEntity_Initiate>());
                    TestFalse(TEXT("Removal arrives before production EndPlay runs"), Item.Has<ck::FTag_DestroyEntity_EndPlay>());
                }
                auto PreviousItems = State->_SourceItems;
                if (State->_Case._Order == EOrder::DuplicateRemovalProjection)
                {
                    PreviousItems.Add(State->_Item);
                    // The wire cache is not the consumed baseline anymore. Inject
                    // the malformed duplicate at the actual local sync boundary.
                    if (State->_Case._Spatial)
                    {
                        auto& Applied = State->_Source.Get<ck::TFragment_Inventory_AppliedReplication<FCk_InventoryItem_Spatial_ReplicatedEntry>>().Items;
                        const auto Duplicate = Applied[0];
                        Applied.Add(Duplicate);
                    }
                    else
                    {
                        auto& Applied = State->_Source.Get<ck::TFragment_Inventory_AppliedReplication<FCk_InventoryItem_DataOnly_ReplicatedEntry>>().Items;
                        const auto Duplicate = Applied[0];
                        Applied.Add(Duplicate);
                    }
                    TestTrue(TEXT("Duplicate old projection starts from one accepted live membership"),
                        State->_SourceItems.Num() == 1 && RawContains(State->_Source, State->_Item)
                            && UCk_Utils_Item_UE::Get_ParentInventory(State->_Item) == State->_Source);
                }
                ApplyItems(*this, *State, FIncomingDelta{
                    ._Endpoint = EEndpoint::Source,
                    ._Current = {},
                    ._Previous = TOptional<TArray<FCk_Handle_Item>>{MoveTemp(PreviousItems)}});
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            {
                if (ck::Is_NOT_Valid(State->_Source) || HasPendingSync(*State, State->_Source)
                    || State->_Source.Has<ck::FTag_Inventory_MayHaveChanged>() || HasPendingRotation(State->_Item))
                { return false; }
                if (State->_Case._Order == EOrder::LiveDisconnectedBatch
                    || State->_Case._Order == EOrder::DuplicateRemovalProjection)
                { return true; }
                return NOT RawContains(State->_Source, State->_Item) && NOT State->_SourceListener->_Deltas.IsEmpty();
            }), WaitSeconds, TEXT("Actual sync removal consumes its payload and publishes its identity delta")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*)
            {
                if (State->_Case._Order == EOrder::LiveDisconnectedBatch
                    || State->_Case._Order == EOrder::DuplicateRemovalProjection)
                {
                    TestTrue(TEXT("Rejected batch preserves accepted live forward membership and parent"),
                        RawContains(State->_Source, State->_Item)
                            && UCk_Utils_Item_UE::Get_ParentInventory(State->_Item) == State->_Source);
                    if (State->_Case._Order == EOrder::LiveDisconnectedBatch)
                    {
                        TestTrue(TEXT("Rejected batch preserves the disconnected live item's state and parent"),
                            NOT RawContains(State->_Source, State->_SecondaryItem)
                                && UCk_Utils_Item_UE::Get_ParentInventory(State->_SecondaryItem) == State->_Source);
                    }
                    if (State->_Case._Order == EOrder::DuplicateRemovalProjection)
                    {
                        TestTrue(TEXT("Duplicate rejection retains exactly the admitted source record entries"),
                            UCk_Utils_Inventory_UE::RecordOfInventoryItems_Utils::Get_Entries(State->_Source) == State->_SourceItems);
                    }
                    TestTrue(TEXT("Rejected batch leaves the target record empty"),
                        UCk_Utils_Inventory_UE::RecordOfInventoryItems_Utils::Get_Entries(State->_Target).IsEmpty());
                    TestTrue(TEXT("Rejected batch publishes no source or target inventory delta"),
                        State->_SourceListener->_Deltas.IsEmpty() && State->_TargetListener->_Deltas.IsEmpty());
                    if (State->_Case._Spatial)
                    {
                        for (auto Index = 0; Index < State->_SourceItems.Num(); ++Index)
                        {
                            const auto& Item = State->_SourceItems[Index];
                            TestTrue(TEXT("Rejected batch preserves all source cell references"), GridContainsIdentity(State->_Source, Item));
                            TestFalse(TEXT("Rejected batch creates no target cell references"), GridContainsIdentity(State->_Target, Item));
                            const auto Transform = UCk_Utils_Transform_UE::CastChecked(Item);
                            TestTrue(TEXT("Rejected batch preserves the accepted transform rotation"),
                                FMath::IsNearlyZero(UCk_Utils_Transform_UE::Get_EntityCurrentRotation(Transform).Yaw, 1.0e-3));
                            if (TestTrue(TEXT("Rejected batch preserves each item placement fragment"),
                                Item.Has<ck::FFragment_Item_SpatialPlacement>()))
                            {
                                const auto& Placement = Item.Get<ck::FFragment_Item_SpatialPlacement>();
                                TestTrue(TEXT("Rejected batch preserves each source anchor and rotation"),
                                    Placement.Get_Anchor() == FIntPoint{Index, 0}
                                        && Placement.Get_Rotation() == ECk_CardinalRotation::None);
                            }
                        }
                    }
                }
                else
                {
                    constexpr auto Removed = false;
                    CheckDelta(*this, State->_Source, *State->_SourceListener, State->_Item, Removed);
                    if (State->_Case._Spatial)
                    {
                        TestFalse(TEXT("Source grid holds no cell reference to the removed identity, including stale identities"),
                            GridContainsIdentity(State->_Source, State->_Item));
                    }
                    if (State->_Case._Order == EOrder::TargetFirstTransfer)
                    {
                        constexpr auto Added = true;
                        CheckDelta(*this, State->_Target, *State->_TargetListener, State->_Item, Added);
                        TestTrue(TEXT("Source removal preserves target membership and target parent"),
                            RawContains(State->_Target, State->_Item)
                                && UCk_Utils_Item_UE::Get_ParentInventory(State->_Item) == State->_Target);
                        if (State->_Case._Spatial)
                        {
                            TestTrue(TEXT("Target cell occupancy survives late source removal"), GridContainsIdentity(State->_Target, State->_Item));
                            if (TestTrue(TEXT("Target-authored placement fragment survives late source removal"),
                                State->_Item.Has<ck::FFragment_Item_SpatialPlacement>()))
                            {
                                const auto& Placement = State->_Item.Get<ck::FFragment_Item_SpatialPlacement>();
                                TestTrue(TEXT("Target placement coordinate and rotation remain authoritative"),
                                    Placement.Get_Anchor() == TargetCoordinate && Placement.Get_Rotation() == ECk_CardinalRotation::Quarter);
                            }
                            const auto Transform = UCk_Utils_Transform_UE::CastChecked(State->_Item);
                            TestTrue(TEXT("Source cleanup does not enqueue a transform rotation reset over the target"),
                                FMath::IsNearlyEqual(UCk_Utils_Transform_UE::Get_EntityCurrentRotation(Transform).Yaw, 90.0, 1.0e-3));
                        }
                    }
                    else if (ck::IsValid(State->_Item))
                    {
                        TestFalse(TEXT("Ordinary removal clears the source parent"),
                            ck::IsValid(UCk_Utils_Item_UE::Get_ParentInventory(State->_Item)));
                    }
                }
                UnbindDelta(State->_Source, State->_SourceListener);
                UnbindDelta(State->_Target, State->_TargetListener);
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->_Root);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            { return ck::Is_NOT_Valid(State->_Root, ck::IsValid_Policy_IncludePendingKill{}); }),
            WaitSeconds, TEXT("Inventory fixture lifetime subtree is fully reclaimed")));
    }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
