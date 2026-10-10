// One actor bridge; the pass scripts are actorless and local to authority.
class UCk_SmTest_DivergencePrimaryScript : UCk_EntityScript_WithActor_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;
}

class UCk_SmTest_DivergencePassScript : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    { return ECk_EntityScript_ConstructionFlow::Finished; }
}

// Retained source receipts for StateMachine gym actors. This is a local
// lifecycle prerequisite; the GameState owns whole-bank publication.
UCLASS(Blueprintable)
class ACk_SmTest_GymSourceActor : AActor
{
    private UWorld _SourceWorld;
    private FCk_Handle _SourceWorldRoot;
    private TArray<FCk_Handle_PendingEntityScript> _SourcePending;
    private TArray<FCk_Handle> _SourceRoots;
    private TArray<FCk_Handle> _SourceAccepted;
    // Opt-in mode keeps the primary separate: legacy roots remain A/B only.
    private bool _PrimaryMode = false;
    private FCk_Handle_PendingEntityScript _PrimaryPending;
    private FCk_Handle _PrimaryTarget;
    private bool _PrimaryAccepted = false;
    private bool _SourceCaptured = false;
    private bool _SourceClosed = false;

    UFUNCTION(BlueprintPure)
    bool Get_SourceClosed() const { return _SourceClosed; }
    UFUNCTION(BlueprintPure)
    TArray<FCk_Handle> Get_SourceRoots() const { return _SourceRoots; }
    UFUNCTION(BlueprintPure)
    int32 Get_SourceConstructedCount() const { return _SourceAccepted.Num(); }
    UFUNCTION(BlueprintPure)
    FCk_Handle Get_SourcePrimaryTarget() const { return _PrimaryTarget; }
    UFUNCTION(BlueprintPure)
    FCk_Handle Get_SourcePrimary()
    {
        if (!_PrimaryAccepted || _SourceClosed ||
            !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePrimary(
                _PrimaryTarget, this))
        { return FCk_Handle(); }
        return _PrimaryTarget;
    }

    private bool HasLiveSourceContext()
    {
        if (_SourceClosed || !HasAuthority() || IsActorBeingDestroyed()) { return false; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) ||
            World.GetNetMode() == ENetMode::NM_Client ||
            (_SourceCaptured && (ck::Is_NOT_Valid(_SourceWorld) ||
                _SourceWorld != World))) { return false; }
        auto Root = ck::TransientEntity();
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Root) ||
            ck::Is_NOT_Valid(Root) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                Root, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            utils_entity_lifetime::Get_WorldForEntity(Root) != World ||
            (_SourceCaptured && _SourceWorldRoot != Root)) { return false; }
        return true;
    }

    // Primary admission retains the first-return receipt before callbacks.
    bool Request_PrimarySource(FName Callback)
    {
        if (_PrimaryMode || _SourceCaptured || !_SourceRoots.IsEmpty() ||
            !_SourcePending.IsEmpty() || !_SourceAccepted.IsEmpty() ||
            !HasLiveSourceContext() || Callback == NAME_None ||
            !UCk_StateMachineGym_Runtime_UE::Get_IsDivergenceSourceClassNonReplicating(
                UCk_SmTest_DivergencePrimaryScript, true))
        { Request_CloseSource(); return false; }
        _PrimaryMode = true;
        _SourceWorld = GetWorld();
        _SourceWorldRoot = ck::TransientEntity();
        _SourceCaptured = true;
        auto Params = FCk_EntityScript_WithActor_SpawnParams();
        Params._OwningActor = this;
        auto Pending = utils_entity_script::Request_SpawnEntity(
            _SourceWorldRoot, UCk_SmTest_DivergencePrimaryScript, Params);
        auto Target = Pending.Get_EntityUnderConstruction();
        _PrimaryPending = Pending;
        _PrimaryTarget = Target; // first-return receipt before any validation
        if (!utils_pending_entity_script::Get_IsValid(Pending) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Target) ||
            ck::Is_NOT_Valid(Target) ||
            !utils_entity_lifetime::Has_LifetimeOwner(Target) ||
            utils_entity_lifetime::Get_LifetimeOwner(Target) != _SourceWorldRoot ||
            utils_entity_lifetime::Get_WorldForEntity(Target) != _SourceWorld)
        { Request_CloseSource(); return false; }
        utils_pending_entity_script::Promise_OnConstructed(
            Pending, FCk_Delegate_EntityScript_Constructed(this, Callback));
        return !_SourceClosed;
    }

    bool TryAccept_PrimarySource(FCk_Handle_EntityScript InHandle)
    {
        if (_SourceClosed) { return false; }
        auto Target = FCk_Handle(InHandle);
        if (!_PrimaryMode || _PrimaryAccepted || !HasLiveSourceContext() ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Target) ||
            ck::Is_NOT_Valid(Target) || Target != _PrimaryTarget ||
            _PrimaryPending.Get_EntityUnderConstruction() != Target ||
            !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePrimary(Target, this))
        { Request_CloseSource(); return false; }
        _PrimaryAccepted = true;
        return true;
    }

    bool Request_PassSource(FName Callback)
    {
        if (!_PrimaryMode || !_PrimaryAccepted || _SourceRoots.Num() >= 2 ||
            _SourceRoots.Num() != _SourceAccepted.Num() ||
            !Get_HasCurrentSourceContext() || Callback == NAME_None ||
            !UCk_StateMachineGym_Runtime_UE::Get_IsDivergenceSourceClassNonReplicating(
                UCk_SmTest_DivergencePassScript, false))
        { Request_CloseSource(); return false; }
        auto Pending = utils_entity_script::Request_SpawnEntity(
            _PrimaryTarget, UCk_SmTest_DivergencePassScript, FInstancedStruct());
        auto Target = Pending.Get_EntityUnderConstruction();
        const bool Duplicate = Target == _PrimaryTarget || _SourceRoots.Contains(Target);
        _SourcePending.Add(Pending);
        _SourceRoots.Add(Target); // A/B first-return receipts only
        if (!utils_pending_entity_script::Get_IsValid(Pending) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Target) ||
            ck::Is_NOT_Valid(Target) ||
            !utils_entity_lifetime::Has_LifetimeOwner(Target) ||
            utils_entity_lifetime::Get_LifetimeOwner(Target) != _PrimaryTarget ||
            utils_entity_lifetime::Get_WorldForEntity(Target) != _SourceWorld ||
            Duplicate)
        { Request_CloseSource(); return false; }
        utils_pending_entity_script::Promise_OnConstructed(
            Pending, FCk_Delegate_EntityScript_Constructed(this, Callback));
        return !_SourceClosed;
    }

    bool TryAccept_PassSource(FCk_Handle_EntityScript InHandle)
    {
        if (_SourceClosed) { return false; }
        auto Target = FCk_Handle(InHandle);
        const int32 Next = _SourceAccepted.Num();
        if (!_PrimaryMode || !_PrimaryAccepted || !HasLiveSourceContext() ||
            Next >= _SourceRoots.Num() || Next >= _SourcePending.Num() || Next >= 2 ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Target) ||
            ck::Is_NOT_Valid(Target) || _SourceRoots[Next] != Target ||
            _SourcePending[Next].Get_EntityUnderConstruction() != Target ||
            _SourceAccepted.Contains(Target) ||
            !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePass(
                Target, _PrimaryTarget))
        { Request_CloseSource(); return false; }
        _SourceAccepted.Add(Target);
        return true;
    }

    bool Request_SourceEntity(FName Callback)
    {
        if (_PrimaryMode) { Request_CloseSource(); return false; }
        if (!HasLiveSourceContext() || Callback == NAME_None)
        { Request_CloseSource(); return false; }
        auto World = GetWorld();
        auto Root = ck::TransientEntity();
        if (!_SourceCaptured)
        { _SourceWorld = World; _SourceWorldRoot = Root; _SourceCaptured = true; }
        auto Params = FCk_EntityScript_WithActor_SpawnParams();
        Params._OwningActor = this;
        auto Pending = utils_entity_script::Request_SpawnEntity(
            Root, UCk_EntityScript_WithActor_UE, Params);
        auto Target = Pending.Get_EntityUnderConstruction();
        const bool Duplicate = _SourceRoots.Contains(Target);
        _SourcePending.Add(Pending);
        _SourceRoots.Add(Target); // retain even a rejected pending request's partial target
        if (!utils_pending_entity_script::Get_IsValid(Pending) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Target) ||
            ck::Is_NOT_Valid(Target) ||
            !utils_entity_lifetime::Has_LifetimeOwner(Target) ||
            utils_entity_lifetime::Get_LifetimeOwner(Target) != Root ||
            utils_entity_lifetime::Get_WorldForEntity(Target) != World || Duplicate)
        { Request_CloseSource(); return false; }
        utils_pending_entity_script::Promise_OnConstructed(
            Pending, FCk_Delegate_EntityScript_Constructed(this, Callback));
        return !_SourceClosed;
    }

    bool TryAccept_SourceEntity(FCk_Handle_EntityScript InHandle)
    {
        if (_PrimaryMode) { Request_CloseSource(); return false; }
        if (_SourceClosed) { return false; }
        auto Target = FCk_Handle(InHandle);
        int32 Matches = 0;
        for (auto Pending : _SourcePending)
        { if (Pending.Get_EntityUnderConstruction() == Target) { Matches++; } }
        if (!HasLiveSourceContext() ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Target) ||
            ck::Is_NOT_Valid(Target) ||
            Matches != 1 || !_SourceRoots.Contains(Target) ||
            _SourceAccepted.Contains(Target) ||
            !utils_entity_lifetime::Has_LifetimeOwner(Target) ||
            utils_entity_lifetime::Get_LifetimeOwner(Target) != _SourceWorldRoot ||
            utils_entity_lifetime::Get_WorldForEntity(Target) != _SourceWorld ||
            !utils_entity_script::Has(Target) ||
            !utils_owning_actor::Has(Target))
        { Request_CloseSource(); return false; }
        auto Script = utils_entity_script::TryGet_EntityScript(Target);
        if (ck::Is_NOT_Valid(Script) ||
            Script.GetClass() != UCk_EntityScript_WithActor_UE ||
            utils_owning_actor::TryGet_EntityOwningActor(Target) != this)
        { Request_CloseSource(); return false; }
        _SourceAccepted.Add(Target);
        return true;
    }

    // Closed callbacks are inert. Active callbacks validate the exact captured
    // source and every accepted script before touching SM state or presentation.
    bool Get_HasCurrentSourceContext()
    {
        if (_SourceClosed) { return false; }
        if (!_SourceCaptured || !HasLiveSourceContext())
        { Request_CloseSource(); return false; }
        if (_PrimaryMode)
        {
            if (!_PrimaryAccepted ||
                !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePrimary(
                    _PrimaryTarget, this) ||
                _SourceRoots.Num() > 2 ||
                _SourceAccepted.Num() > _SourceRoots.Num() ||
                _SourcePending.Num() != _SourceRoots.Num())
            { Request_CloseSource(); return false; }
            for (int32 I = 0; I < _SourceRoots.Num(); ++I)
            {
                auto Pass = _SourceRoots[I];
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Pass) ||
                    ck::Is_NOT_Valid(Pass))
                { Request_CloseSource(); return false; }
                if (I < _SourceAccepted.Num())
                {
                    if (_SourceAccepted[I] != Pass ||
                        !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePass(
                            Pass, _PrimaryTarget))
                    { Request_CloseSource(); return false; }
                }
                else if (I != _SourceAccepted.Num() ||
                    _SourcePending[I].Get_EntityUnderConstruction() != Pass ||
                    utils_entity_lifetime::Get_IsPendingDestroy(
                        Pass, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                    !utils_entity_lifetime::Has_LifetimeOwner(Pass) ||
                    utils_entity_lifetime::Get_LifetimeOwner(Pass) != _PrimaryTarget ||
                    utils_entity_lifetime::Get_WorldForEntity(Pass) != _SourceWorld)
                { Request_CloseSource(); return false; }
            }
            return true;
        }
        for (auto Source : _SourceAccepted)
        {
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Source) ||
                ck::Is_NOT_Valid(Source) ||
                utils_entity_lifetime::Get_IsPendingDestroy(
                    Source, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                !utils_entity_lifetime::Has_LifetimeOwner(Source) ||
                utils_entity_lifetime::Get_LifetimeOwner(Source) != _SourceWorldRoot ||
                utils_entity_lifetime::Get_WorldForEntity(Source) != _SourceWorld ||
                !utils_entity_script::Has(Source) ||
                !utils_owning_actor::Has(Source))
            { Request_CloseSource(); return false; }
            auto Script = utils_entity_script::TryGet_EntityScript(Source);
            if (ck::Is_NOT_Valid(Script) ||
                Script.GetClass() != UCk_EntityScript_WithActor_UE ||
                utils_owning_actor::TryGet_EntityOwningActor(Source) != this)
            { Request_CloseSource(); return false; }
        }
        return true;
    }

    UFUNCTION()
    void Request_CloseSource()
    {
        if (_SourceClosed) { return; }
        _SourceClosed = true; // a destruction callback must not resume source mutation

        if (_PrimaryMode)
        {
            // Closure is terminal. Only a completely proved healthy tree or
            // the exact unconstructed primary request may be retired here.
            // Partial or unproved setup stays closed until the session ends.
            if (_PrimaryAccepted && !_SourceRoots.IsEmpty() &&
                _SourceRoots.Num() == _SourceAccepted.Num())
            {
                for (int32 I = 0; I < _SourceRoots.Num(); ++I)
                { if (_SourceRoots[I] != _SourceAccepted[I]) { return; } }
                UCk_StateMachineGym_Runtime_UE::TryRetire_DivergenceHealthySource(
                    _PrimaryTarget, this, _SourceRoots);
            }
            else if (!_PrimaryAccepted && _SourceRoots.IsEmpty() &&
                _SourcePending.IsEmpty() && _SourceAccepted.IsEmpty() &&
                _PrimaryPending.Get_EntityUnderConstruction() == _PrimaryTarget)
            {
                UCk_StateMachineGym_Runtime_UE::TryCancel_DivergencePendingPrimary(
                    _PrimaryPending, this);
            }
            return;
        }
        // Never inspect child ownership against a stale transient root.
        if (!_SourceCaptured || ck::Is_NOT_Valid(_SourceWorld) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(_SourceWorldRoot) ||
            ck::Is_NOT_Valid(_SourceWorldRoot) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                _SourceWorldRoot, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            utils_entity_lifetime::Get_WorldForEntity(_SourceWorldRoot) != _SourceWorld)
        { return; }
        auto Roots = _SourceRoots;
        auto Pending = _SourcePending;
        for (int32 I = 0; I < Roots.Num(); ++I)
        {
            auto Root = Roots[I];
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Root) ||
                ck::Is_NOT_Valid(Root) ||
                utils_entity_lifetime::Get_IsPendingDestroy(
                    Root, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                !utils_entity_lifetime::Has_LifetimeOwner(Root) ||
                utils_entity_lifetime::Get_LifetimeOwner(Root) != _SourceWorldRoot ||
                utils_entity_lifetime::Get_WorldForEntity(Root) != _SourceWorld)
            { continue; }
            if (_SourceAccepted.Contains(Root))
            { utils_entity_lifetime::Request_DestroyEntity(Root); }
            else if (I < Pending.Num() &&
                Pending[I].Get_EntityUnderConstruction() == Root)
            { utils_pending_entity_script::Request_DestroyEntity(Pending[I]); }
        }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    { Request_CloseSource(); }
}
