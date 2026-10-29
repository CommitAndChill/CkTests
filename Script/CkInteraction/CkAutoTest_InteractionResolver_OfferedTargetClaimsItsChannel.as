// Language=angelscript

//============================================================================
// CK INTERACTION - AUTOMATION TEST: AN OFFERED TARGET CLAIMS ITS CHANNEL
//============================================================================
//
// A mapping's channel order is its priority order, and its ChannelPrecedence
// says what a channel counts through (CkInteractionResolver_Utils.cpp,
// DoResolveTargets_Internal):
//   ByUsableTargets (the default): a channel with no target the resolver can
//     interact with now is skipped; a DISABLED target on the first channel is
//     as if absent, and the intent falls through to the second channel.
//   ByOfferedTargets: the first channel with ANY offered target decides. A
//     disabled target on the first channel claims the intent: the best set is
//     EMPTY, and the second channel is never consulted. Enabled again (and
//     re-offered, since enabling alone stamps no re-resolve), it is the best.
//
// Two resolvers on two owners, one per precedence, both over
// [Default, Secondary] with the same two targets: A on Default, disabled
// before either intent starts; B on Secondary, enabled. Then A is enabled and
// re-offered to the claiming resolver.
//
// API provenance: the resolver mapping, Add, Request_* and
// Get_BestInteractTargets as in
// CkAutoTest_InteractionResolver_ReResolvesOnTargetChange.as; Set_Enabled on
// an interact target: CkInteractTarget_Utils.h (Set_Enabled).
//============================================================================

class UCk_AutoTest_InteractionResolver_OfferedTargetClaimsItsChannel : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;

    private FVector k_Base = FVector(0.0, 89000.0, 0.0);

    private FCk_Handle _SelfHandle;
    private FCk_Handle_InteractionResolver _Claiming;
    private FCk_Handle_InteractionResolver _FallingThrough;
    private FCk_Handle_InteractTarget _TargetA;   // Default channel, disabled first
    private FCk_Handle_InteractTarget _TargetB;   // Secondary channel

    private int32 _IntentStartsCompleted = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        _SelfHandle = InHandle;

        _TargetA = DoSpawnTarget(interaction_gym_helpers::DefaultChannel(), FVector(100.0, 0.0, 0.0));
        _TargetB = DoSpawnTarget(interaction_gym_helpers::SecondaryChannel(), FVector(100.0, 50.0, 0.0));
        utils_interact_target::Set_Enabled(_TargetA, ECk_EnableDisable::Disable);

        _Claiming = DoSpawnResolver(ECk_InteractionResolver_ChannelPrecedence::ByOfferedTargets, FVector(0.0, 0.0, 0.0));
        _FallingThrough = DoSpawnResolver(ECk_InteractionResolver_ChannelPrecedence::ByUsableTargets, FVector(0.0, 200.0, 0.0));

        Add_Step(          "offer A (disabled) and B to both resolvers; start both intents", n"Step_OfferAndStart");
        Add_Step_WaitUntil("both StartIntent requests drained",                              n"Check_IntentsStarted");
        Add_Step_WaitUntil("the falling-through resolver picked B",                          n"Check_FallingThroughPicksB");
        Add_Step(          "the claiming resolver picked nothing: disabled A claims Default", n"Step_AssertClaimed");
        Add_Step(          "enable A and re-offer it to the claiming resolver",               n"Step_EnableAndReofferA");
        Add_Step_WaitUntil("the claiming resolver picked A",                                 n"Check_ClaimingPicksA");
        Add_Step(          "A is the claiming resolver's one best target",                   n"Step_AssertA");

        Run_Steps(InHandle);
    }

    private FCk_Handle_InteractionResolver DoSpawnResolver(ECk_InteractionResolver_ChannelPrecedence InPrecedence, FVector InOffset)
    {
        auto Owner = utils_entity_lifetime::Request_CreateEntity(_SelfHandle);
        Owner.Request_OverrideToSelf();
        utils_transform::Add(Owner, FTransform(FRotator::ZeroRotator, k_Base + InOffset), ECk_Replication::DoesNotReplicate);

        auto Channels = TArray<FGameplayTag>();
        Channels.Add(interaction_gym_helpers::DefaultChannel());
        Channels.Add(interaction_gym_helpers::SecondaryChannel());

        auto Mapping = FCk_InteractionResolver_IntentChannelMapping(interaction_gym_helpers::UseIntent(), Channels);
        Mapping.Set_DistanceSorting(ECk_InteractionResolver_DistanceSorting::Disabled);
        Mapping.Set_ChannelPrecedence(InPrecedence);

        auto Mappings = TArray<FCk_InteractionResolver_IntentChannelMapping>();
        Mappings.Add(Mapping);
        return utils_interaction_resolver::Add(Owner, FCk_InteractionResolver_Spec(Mappings), ECk_Replication::DoesNotReplicate);
    }

    private FCk_Handle_InteractTarget DoSpawnTarget(FGameplayTag InChannel, FVector InOffset)
    {
        auto Owner = utils_entity_lifetime::Request_CreateEntity(_SelfHandle);
        Owner.Request_OverrideToSelf();
        utils_transform::Add(Owner, FTransform(FRotator::ZeroRotator, k_Base + InOffset), ECk_Replication::DoesNotReplicate);

        auto TargetParams = FCk_InteractTarget_Spec(InChannel);
        TargetParams.Set_CompletionPolicy(ECk_Interaction_CompletionPolicy::Instant);
        return utils_interact_target::Add(Owner, TargetParams, ECk_Replication::DoesNotReplicate);
    }

    private TArray<FCk_Handle_InteractTarget> DoGet_Best(FCk_Handle_InteractionResolver InResolver)
    {
        return utils_interaction_resolver::Get_BestInteractTargets(InResolver, interaction_gym_helpers::UseIntent());
    }

    private void DoOffer(FCk_Handle_InteractionResolver InResolver, FCk_Handle_InteractTarget InTarget)
    {
        utils_interaction_resolver::Request_AddInteractTarget(InResolver, FCk_Request_InteractionResolver_AddInteractTarget(InTarget));
    }

    UFUNCTION()
    private void OnStartIntentCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        if (InResult != ECk_Request_OperationResult::Succeeded)
        { FinishFailure(f"StartIntent completed {InResult} instead of Succeeded"); return; }
        ++_IntentStartsCompleted;
    }

    UFUNCTION()
    private void Step_OfferAndStart(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        DoOffer(_Claiming, _TargetA);
        DoOffer(_Claiming, _TargetB);
        DoOffer(_FallingThrough, _TargetA);
        DoOffer(_FallingThrough, _TargetB);

        utils_interaction_resolver::Request_StartIntent(_Claiming,
            FCk_Request_InteractionResolver_StartIntent(interaction_gym_helpers::UseIntent()),
            FCk_Delegate_Request_OnCompleted(this, n"OnStartIntentCompleted"));
        utils_interaction_resolver::Request_StartIntent(_FallingThrough,
            FCk_Request_InteractionResolver_StartIntent(interaction_gym_helpers::UseIntent()),
            FCk_Delegate_Request_OnCompleted(this, n"OnStartIntentCompleted"));
    }

    UFUNCTION()
    private void Check_IntentsStarted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_IntentStartsCompleted == 2);
    }

    UFUNCTION()
    private void Check_FallingThroughPicksB(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Best = DoGet_Best(_FallingThrough);
        auto Res = OutResult;
        Res.Set(Best.Num() == 1 && Best[0] == _TargetB);
    }

    // The falling-through resolver has resolved (it picked B past disabled A), so the claiming one, started in the same
    // step, has resolved too: its empty set is a claim, not a resolve that has not happened yet.
    UFUNCTION()
    private void Step_AssertClaimed(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Best = DoGet_Best(_Claiming);
        Assert_Equals_Int(Best.Num(), 0, "ByOfferedTargets: disabled A on the first channel claims the intent, B on the second is never consulted");
    }

    UFUNCTION()
    private void Step_EnableAndReofferA(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        utils_interact_target::Set_Enabled(_TargetA, ECk_EnableDisable::Enable);
        utils_interaction_resolver::Request_RemoveInteractTarget(_Claiming, FCk_Request_InteractionResolver_RemoveInteractTarget(_TargetA));
        DoOffer(_Claiming, _TargetA);
    }

    UFUNCTION()
    private void Check_ClaimingPicksA(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Best = DoGet_Best(_Claiming);
        auto Res = OutResult;
        Res.Set(Best.Num() == 1 && Best[0] == _TargetA);
    }

    UFUNCTION()
    private void Step_AssertA(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Best = DoGet_Best(_Claiming);
        Assert_True(Best.Num() == 1 && Best[0] == _TargetA, "enabled A on the first channel is the one best target");
        // Last step: the sequencer finishes the test on its own.
    }
}
