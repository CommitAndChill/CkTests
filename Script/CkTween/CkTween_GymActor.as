UCLASS(Blueprintable)
class ACk_TweenTest_GymActor : AActor
{
    default bReplicates = true;
    default bReplicateUsingRegisteredSubObjectList = true;
    default bAlwaysRelevant = true;
    default bReplicateMovement = true;

    UPROPERTY(DefaultComponent)
    UStaticMeshComponent Mesh;
    default Mesh.StaticMesh = Cast<UStaticMesh>(utils_i_o::LoadAssetByName("Cube1", ECk_AssetSearchScope::Engine, ECk_AssetSearchStrategy::ExactOnly)._Asset);
    default Mesh.CollisionEnabled = ECollisionEnabled::NoCollision;

    UPROPERTY(DefaultComponent)
    UTextRenderComponent TextRenderer;
    default TextRenderer.RelativeLocation = FVector(0.0f, 0.0f, 75.0f);
    default TextRenderer.SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    default TextRenderer.WorldSize = 40.0f;
    default TextRenderer.TextRenderColor = FColor::Orange;

    UPROPERTY(ExposeOnSpawn, ReplicatedUsing=OnTextUpdated)
    ECk_TweenEasing TweenEasingMethod = ECk_TweenEasing::Linear;

    UPROPERTY(ExposeOnSpawn, Replicated)
    int32 Generation = 0;

    UPROPERTY(ReplicatedUsing=OnPublishedUpdated)
    bool Published = false;

    UPROPERTY(ExposeOnSpawn)
    float32 TweenDuration = 1.0f;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();
        TextRenderer.SetText(ck::Text(f"{TweenEasingMethod}"));
        OnPublishedUpdated();
        if (!HasAuthority() || _Retired || Generation <= 0) { return; }
        _PendingBridge = utils_entity_script_with_actor::Request_SpawnEntityScript_OnActor(
            this, UCk_EntityScript_WithActor_UE);
        if (utils_pending_entity_script::Get_IsValid(_PendingBridge))
        {
            utils_pending_entity_script::Promise_OnConstructed(
                _PendingBridge, FCk_Delegate_EntityScript_Constructed(this, n"OnEntityConstructed"));
        }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    { Request_RetireBridge(); }

    UFUNCTION()
    void OnTextUpdated()
    {
        TextRenderer.SetText(ck::Text(f"{TweenEasingMethod}"));
    }

    UFUNCTION()
    void OnPublishedUpdated()
    {
        Mesh.SetVisibility(Published, false);
        TextRenderer.SetVisibility(Published, false);
    }

    UPROPERTY()
    FVector StartLocation = FVector::ZeroVector;

    UPROPERTY()
    FVector EndLocation = FVector::ZeroVector;

    UPROPERTY()
    FCk_Handle_Tween TweenHandle;

    private FCk_Handle_PendingEntityScript _PendingBridge;
    private FCk_Handle _Bridge;
    private bool _Retired = false;

    private bool Get_IsLiveEntity(FCk_Handle InEntity)
    {
        if (ck::Is_NOT_Valid(InEntity)) { return false; }
        return !utils_entity_lifetime::Get_IsPendingDestroy(InEntity, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(InEntity, ECk_EntityLifetime_DestructionPhase::Teardown) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(InEntity, ECk_EntityLifetime_DestructionPhase::Destroyed);
    }

    bool Get_IsPrepared()
    {
        return !_Retired && HasAuthority() && Generation > 0 && Get_IsLiveEntity(_Bridge) &&
            utils_transform::Has(_Bridge) &&
            utils_owning_actor::TryGet_EntityOwningActor(_Bridge) == this && !ck::IsValid(TweenHandle);
    }

    bool Get_IsTweenReady()
    {
        return !_Retired && HasAuthority() && Generation > 0 && Get_IsLiveEntity(_Bridge) &&
            utils_transform::Has(_Bridge) &&
            utils_owning_actor::TryGet_EntityOwningActor(_Bridge) == this &&
            Get_IsLiveEntity(FCk_Handle(TweenHandle)) && utils_tween::Has(FCk_Handle(TweenHandle));
    }

    FCk_Handle Get_BridgeForRetirement()
    {
        if (utils_pending_entity_script::Get_IsValid(_PendingBridge))
        { return _PendingBridge.Get_EntityUnderConstruction(); }
        return _Bridge;
    }

    FCk_Handle Get_TweenForRetirement()
    { return ck::IsValid(TweenHandle) ? FCk_Handle(TweenHandle) : FCk_Handle(); }

    bool Request_ActivateTween()
    {
        if (!Get_IsPrepared() || Published ||
            !(TweenDuration > 0.0f && TweenDuration < 1000000000.0f) ||
            int32(TweenEasingMethod) < 0 || int32(TweenEasingMethod) >= int32(ECk_TweenEasing::ECk_MAX))
        { return false; }
        StartLocation = GetActorLocation();
        EndLocation = StartLocation + FVector(0.0f, 0.0f, 200.0f);
        TweenHandle = utils_tween::Create_TweenEntityLocation(_Bridge.As_Transform(), EndLocation,
            TweenDuration, TweenEasingMethod, ECk_TweenLoopType::Yoyo, -1, 0.0f);
        return Get_IsTweenReady();
    }

    bool Request_PublishTween()
    {
        if (!Get_IsTweenReady() || Published) { return false; }
        Published = true;
        OnPublishedUpdated();
        ForceNetUpdate();
        return true;
    }

    void Request_RetireBridge()
    {
        if (_Retired) { return; }
        _Retired = true;
        Published = false;
        OnPublishedUpdated();
        if (utils_pending_entity_script::Get_IsValid(_PendingBridge))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingBridge); }
        else if (ck::IsValid(_Bridge))
        { utils_entity_lifetime::Request_DestroyEntity(_Bridge); }
        _PendingBridge = FCk_Handle_PendingEntityScript();
        _Bridge = FCk_Handle();
        TweenHandle = FCk_Handle_Tween();
    }

	UFUNCTION()
	private void OnEntityConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
	{
        auto InEntity = FCk_Handle(InEntityScriptHandle);
        if (_Retired || !HasAuthority() || Generation <= 0 || ck::Is_NOT_Valid(InEntity) ||
            !utils_pending_entity_script::Get_IsValid(_PendingBridge) ||
            InEntity != _PendingBridge.Get_EntityUnderConstruction() ||
            utils_owning_actor::TryGet_EntityOwningActor(InEntity) != this)
        { return; }
        _Bridge = InEntity;
        _PendingBridge = FCk_Handle_PendingEntityScript();
        InEntity.Set_DebugName(n"TweenCube");
	}
};
