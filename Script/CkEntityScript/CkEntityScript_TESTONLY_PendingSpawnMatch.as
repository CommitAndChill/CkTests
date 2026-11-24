// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - TEST-ONLY: a replicated parent that spawns replicated children from DoConstruct
//============================================================================
//
// The gondola shape. The parent replicates with a rotated world transform and, in DoConstruct on every machine,
// spawns two Replicates children whose transform param it derives from its own. On a client that value is computed
// from the parent's REPLICATED transform while the child's own copy is the authority's value after its own trip
// through replication, so the two agree only to replication precision. Each child's OnConstructed callback looks
// its slot up by the pending handle the spawn returned. Used by CkAutoTest_Net_EntityScript_PendingSpawnMatchesDerivedTransform.
//============================================================================

namespace constants_pending_spawn_match_test
{
    const FName k_ParentTag = n"TAG_CkAutoTest_PendingSpawnMatchParent";
    const int32 k_ChildCount = 2;
    // A slot's child must land within this of its own expected location; the slots are over a metre apart.
    const float k_SlotLocationTolerance = 1.0f;
}

namespace utils_pending_spawn_match_test
{
    FTransform Get_SlotLocalOffset(int32 InSlot)
    {
        return FTransform(
            FRotator(-12.0f + 7.0f * InSlot, 23.0f, 4.0f),
            FVector(41.3f, -17.9f + 103.7f * InSlot, 22.1f + 96.4f * InSlot),
            FVector(1.0f, 1.0f, 1.0f));
    }

    // The gondola's manual compose (FTransform * is unreliable in AS).
    FTransform Get_SlotWorldTransform(FTransform InParent, int32 InSlot)
    {
        const auto Local = Get_SlotLocalOffset(InSlot);
        const auto WorldLoc = InParent.TransformPosition(Local.GetLocation());
        const auto WorldRot = (InParent.GetRotation() * Local.GetRotation()).Rotator();
        const auto WorldScale = InParent.GetScale3D() * Local.GetScale3D();
        return FTransform(WorldRot, WorldLoc, WorldScale);
    }
}

class UCk_TESTONLY_PendingSpawnMatch_Child_EntityScript : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::Replicates;

    UPROPERTY(ExposeOnSpawn)
    FTransform SpawnTransform = FTransform::Identity;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}

class UCk_TESTONLY_PendingSpawnMatch_Parent_EntityScript : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::Replicates;

    UPROPERTY(ExposeOnSpawn)
    FTransform SpawnTransform = FTransform::Identity;

    // Read by the test: how many child callbacks fired, the slot each one resolved to, and what went wrong.
    int32 CallbackCount = 0;
    TArray<int32> ResolvedSlots;
    FString Failures;

    private TMap<FCk_Handle, int32> _SlotByPendingEntity;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        utils_entity_tag::Add(InHandle, constants_pending_spawn_match_test::k_ParentTag);

        for (int32 Slot = 0; Slot < constants_pending_spawn_match_test::k_ChildCount; ++Slot)
        {
            auto Pending = utils_entity_script::Request_SpawnEntity(
                InHandle,
                UCk_TESTONLY_PendingSpawnMatch_Child_EntityScript,
                UCk_TESTONLY_PendingSpawnMatch_Child_EntityScript::Params(
                    utils_pending_spawn_match_test::Get_SlotWorldTransform(SpawnTransform, Slot)));

            _SlotByPendingEntity.Add(Pending.Get_EntityUnderConstruction(), Slot);

            utils_pending_entity_script::Promise_OnConstructed(
                Pending, FCk_Delegate_EntityScript_Constructed(this, n"OnChildConstructed"));
        }

        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION()
    private void OnChildConstructed(FCk_Handle_EntityScript InChild)
    {
        CallbackCount += 1;

        const auto Pending = utils_pending_entity_script::Get_PendingHandle_ForConstructed(InChild);
        const auto PendingEntity = Pending.Get_EntityUnderConstruction();
        if (_SlotByPendingEntity.Contains(PendingEntity) == false)
        {
            ResolvedSlots.Add(-1);
            Failures += f" callback {CallbackCount}: its pending handle is not one this parent spawned;";
            return;
        }

        const auto Slot = _SlotByPendingEntity[PendingEntity];
        _SlotByPendingEntity.Remove(PendingEntity);
        ResolvedSlots.Add(Slot);

        auto Child = Cast<UCk_TESTONLY_PendingSpawnMatch_Child_EntityScript>(utils_entity_script::TryGet_EntityScript(InChild));
        if (Child == nullptr)
        {
            Failures += f" callback {CallbackCount}: the constructed entity has no child script;";
            return;
        }

        const auto Expected = utils_pending_spawn_match_test::Get_SlotWorldTransform(SpawnTransform, Slot).GetLocation();
        const auto Distance = (Child.SpawnTransform.GetLocation() - Expected).Size();
        if (Distance > constants_pending_spawn_match_test::k_SlotLocationTolerance)
        { Failures += f" callback {CallbackCount}: resolved to slot {Slot} but the child sits {Distance} cm from that slot;"; }
    }
}
