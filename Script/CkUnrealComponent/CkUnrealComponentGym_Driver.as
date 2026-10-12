//============================================================================
// CkUnrealComponent Gym - Driver Actor
//
// The authority GameState owns eight fixed drivers. Each driver:
//   1. Spawns an ECS entity with Transform fragment.
//   2. Calls UCk_Utils_UnrealComponent_UE::Add() to attach a UActorComponent
//      of the chosen ComponentType to the entity. The component is created
//      on the transient actor owned by the ComponentHost subsystem.
//   3. Binds OnAdded so it can configure the component once the Setup
//      processor has registered it.
//   4. Animates the entity transform per-frame; the PushTransform processor
//      pushes that motion onto the component. GameState publishes pose values.
//
// This exercises the CkUnrealComponent module's full surface for arbitrary
// engine-provided component classes.
//============================================================================

UENUM()
enum ECk_UnrealComponentGym_Type
{
	StaticMesh,
	PointLight,
	SpotLight,
	TextRender,
	Arrow,
	Box,
	Sphere,
	Capsule,
	ECk_MAX
}

UCLASS(Blueprintable)
class ACk_UnrealComponentGym_Driver : AActor
{
	default bReplicates = false;

	// The driver itself has no visible representation - the attached component
	// is the visual. We keep a SceneRoot so the actor has a transform.
	UPROPERTY(DefaultComponent, RootComponent)
	USceneComponent SceneRoot;

	UPROPERTY(ExposeOnSpawn)
	ECk_UnrealComponentGym_Type ComponentType = ECk_UnrealComponentGym_Type::StaticMesh;
	UPROPERTY(ExposeOnSpawn)
	ACk_UnrealComponentGym_GameState SourceState;
	UPROPERTY(ExposeOnSpawn)
	int32 SourceGeneration = 0;
	UPROPERTY(ExposeOnSpawn)
	int32 SourceRole = -1;
	UPROPERTY(ExposeOnSpawn)
	FCk_Handle SourceOwner;

	// --- ECS state (server-authoritative) ---
	FCk_Handle EcsEntity;
	FCk_Handle_UnrealComponent ComponentHandle;
	FCk_Handle_Timer TimerHandle;
	FCk_Handle_PendingEntityScript PendingWithActor;
	FCk_Handle PendingTarget;
	bool ComponentReady = false;
	int32 OnAddedFireCount = 0;
	private UWorld _SourceWorld;
	private ACk_UnrealComponentGym_GameState _CapturedSource;
	private FCk_Handle _CapturedOwner;
	private int32 _CapturedGeneration = 0;
	private int32 _CapturedRole = -1;
	private ECk_UnrealComponentGym_Type _CapturedType = ECk_UnrealComponentGym_Type::ECk_MAX;
	private UStaticMesh _CubeMesh;
	private UActorComponent _CapturedComponent;
	private AActor _ComponentHost;
	private FTransform _AnchorPose;
	private bool _SpawnAttempted = false;
	private bool _EntityConstructed = false;
	private bool _StyleConfigured = false;
	private bool _Closed = false;
	private bool _Failed = false;
	private bool _Ending = false;
	private bool _ReadyPublished = false;
	private int32 _ResetPreparedGeneration = 0;

	// --- Animation state ---
	FVector OrbitOrigin = FVector::ZeroVector;
	float32 ElapsedTime = 0.0f;
	float32 OrbitRadius = 120.0f;
	float32 OrbitSpeed = 1.5f;
	float32 OrbitBaseHeight = 180.0f;
	float32 OrbitBobAmplitude = 50.0f;

	private bool Get_HasSourceContext()
	{
		if (_Ending || _Closed || _Failed || IsActorBeingDestroyed() || !HasAuthority() ||
			ck::Is_NOT_Valid(_SourceWorld) || GetWorld() != _SourceWorld ||
			ck::Is_NOT_Valid(_CapturedSource) || _CapturedSource.IsActorBeingDestroyed() ||
			_CapturedSource.GetWorld() != _SourceWorld || _SourceWorld.GetGameState() != _CapturedSource ||
			SourceState != _CapturedSource || SourceOwner != _CapturedOwner ||
			SourceGeneration != _CapturedGeneration || SourceRole != _CapturedRole ||
			ComponentType != _CapturedType || _CapturedGeneration <= 0 ||
			_CapturedRole < 0 || _CapturedRole >= 8 || _CapturedRole != int32(_CapturedType) ||
			ck::Is_NOT_Valid(_CapturedOwner)) { return false; }
		return _CapturedSource.Get_IsSourceContext(this, _CapturedRole, _CapturedGeneration, _CapturedOwner);
	}

	private void FailClosed(FString Reason = "UnrealComponent Driver failed")
	{
		if (_Failed || _Ending) { return; }
		_Failed = true;
		_Closed = true;
		ComponentReady = false;
		if (ck::IsValid(_CapturedSource) && !_CapturedSource.IsActorBeingDestroyed())
		{ _CapturedSource.Request_Fail(Reason); }
	}

	private bool Get_HasExactEntity() const
	{
		if (ck::Is_NOT_Valid(PendingTarget) || ck::Is_NOT_Valid(EcsEntity) ||
			PendingTarget != EcsEntity || !utils_entity_lifetime::Has_LifetimeOwner(EcsEntity))
		{ return false; }
		return utils_entity_lifetime::Get_LifetimeOwner(EcsEntity) == _CapturedOwner &&
			utils_owning_actor::TryGet_EntityOwningActor(EcsEntity) == this;
	}

	private bool Get_HasExactTimer() const
	{
		if (!Get_HasExactEntity() || ck::Is_NOT_Valid(TimerHandle) ||
			!utils_timer::Has_Any(EcsEntity) ||
			!utils_entity_lifetime::Has_LifetimeOwner(FCk_Handle(TimerHandle)))
		{ return false; }
		return utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(TimerHandle)) == EcsEntity;
	}

	private bool TryGet_ExactSample(FCkUnrealComponentGym_SourceSample& OutSample)
	{
		if (!Get_HasSourceContext() || !Get_HasExactEntity() ||
			ck::Is_NOT_Valid(ComponentHandle) || ck::Is_NOT_Valid(_CapturedComponent) ||
			ck::Is_NOT_Valid(_ComponentHost)) { return false; }
		return UCk_UnrealComponentGym_Runtime_UE::TryGetSourceSample(
			this, _ComponentHost, _CapturedComponent, EcsEntity,
			FCk_Handle(ComponentHandle), _CapturedRole, OutSample);
	}

	private bool Get_HasExactComponent(bool RequireStyle = true)
	{
		FCkUnrealComponentGym_SourceSample Sample;
		return TryGet_ExactSample(Sample) && Sample.Role == _CapturedRole &&
			(!RequireStyle || Sample.bStyleValid);
	}

	private void TryPublishReady()
	{
		if (_Closed || _Failed || !_StyleConfigured || !_EntityConstructed ||
			!Get_HasExactComponent() || !Get_HasExactTimer() ||
			utils_timer::Get_CurrentState(TimerHandle) != ECk_Timer_State::Running)
		{ return; }
		_ReadyPublished = true;
		ComponentReady = true;
	}

	bool Get_IsFailed() const { return _Failed; }
	UFUNCTION(BlueprintPure)
	FCk_Handle Get_SourceEntity() const { return EcsEntity; }
	UFUNCTION(BlueprintPure)
	FCk_Handle Get_SourceComponentHandle() const { return FCk_Handle(ComponentHandle); }
	UFUNCTION(BlueprintPure)
	FCk_Handle Get_SourceTimerHandle() const { return FCk_Handle(TimerHandle); }
	UFUNCTION(BlueprintPure)
	UActorComponent Get_SourceComponentObject() const { return _CapturedComponent; }
	UFUNCTION(BlueprintPure)
	AActor Get_SourceHostActor() const { return _ComponentHost; }

	bool Get_HasValidComposition()
	{
		return _ReadyPublished && ComponentReady && !_Closed && !_Failed &&
			Get_HasExactComponent() && Get_HasExactTimer() &&
			utils_timer::Get_CurrentState(TimerHandle) == ECk_Timer_State::Running;
	}
	bool TryGet_PhysicalSample(FCkUnrealComponentGym_SourceSample& OutSample)
	{
		OutSample = FCkUnrealComponentGym_SourceSample();
		if (!Get_HasValidComposition()) { return false; }
		return TryGet_ExactSample(OutSample) &&
			OutSample.Role == _CapturedRole && OutSample.bStyleValid;
	}

	FTransform Get_ResetPose() const
	{
		auto Rotation = FRotator(0.0f, 0.0f, 0.0f);
		if (_CapturedType == ECk_UnrealComponentGym_Type::Arrow ||
			_CapturedType == ECk_UnrealComponentGym_Type::SpotLight ||
			_CapturedType == ECk_UnrealComponentGym_Type::TextRender)
		{ Rotation = FRotator(0.0f, 90.0f, 0.0f); }
		else { Rotation = _AnchorPose.GetRotation().Rotator(); }
		return FTransform(Rotation, OrbitOrigin + FVector(OrbitRadius, 0.0f, OrbitBaseHeight),
			FVector(1.0f, 1.0f, 1.0f));
	}

	bool Request_PrepareReset(int32 NewGeneration)
	{
		if (!Get_HasValidComposition() || _ResetPreparedGeneration == NewGeneration ||
			(NewGeneration != _CapturedGeneration && NewGeneration <= _CapturedGeneration) ||
			ck::Is_NOT_Valid(_CapturedSource) || _CapturedSource.Get_CanAnimateSource() ||
			NewGeneration != _CapturedSource.Snapshot.Generation)
		{ FailClosed("UnrealComponent Driver reset prerequisites failed"); return false; }
		SourceGeneration = NewGeneration;
		_CapturedGeneration = NewGeneration;
		_ResetPreparedGeneration = NewGeneration;
		ElapsedTime = 0.0f;
		if (!Get_HasExactComponent())
		{ FailClosed("UnrealComponent Driver reset epoch rejected"); return false; }
		auto ResetPose = Get_ResetPose();
		utils_transform::Request_SetLocation(EcsEntity, ResetPose.GetLocation(), ECk_LocalWorld::World);
		if (!Get_HasExactComponent()) { FailClosed("UnrealComponent Driver reset location failed"); return false; }
		utils_transform::Request_SetRotation(EcsEntity, ResetPose.GetRotation().Rotator(), ECk_LocalWorld::World);
		if (!Get_HasExactComponent()) { FailClosed("UnrealComponent Driver reset rotation failed"); return false; }
		utils_transform::Request_SetScale(EcsEntity, FVector(1.0f, 1.0f, 1.0f), ECk_LocalWorld::World);
		if (!Get_HasExactComponent()) { FailClosed("UnrealComponent Driver reset scale failed"); return false; }
		return true;
	}

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		auto _CkPerfScope = ck::ScopedStat();
		if (!HasAuthority()) { return; }
		_SourceWorld = GetWorld();
		_CapturedSource = SourceState;
		_CapturedOwner = SourceOwner;
		_CapturedGeneration = SourceGeneration;
		_CapturedRole = SourceRole;
		_CapturedType = ComponentType;
		_AnchorPose = GetActorTransform();
		OrbitOrigin = _AnchorPose.GetLocation();
		if (!Get_HasSourceContext() || !_AnchorPose.IsValid() || _AnchorPose.ContainsNaN() ||
			OrbitOrigin.Size() > 100000.0f || !Math::IsFinite(OrbitRadius) ||
			!Math::IsFinite(OrbitSpeed) || !Math::IsFinite(OrbitBaseHeight) ||
			!Math::IsFinite(OrbitBobAmplitude))
		{ FailClosed("UnrealComponent Driver invalid source or anchor"); return; }
		if (_CapturedType == ECk_UnrealComponentGym_Type::StaticMesh)
		{
			_CubeMesh = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Cube.Cube"));
			if (ck::Is_NOT_Valid(_CubeMesh)) { FailClosed("UnrealComponent Driver cube missing"); return; }
		}
		auto SpawnParams = FCk_EntityScript_WithActor_SpawnParams();
		SpawnParams._OwningActor = this;
		_SpawnAttempted = true;
		PendingWithActor = utils_entity_script::Request_SpawnEntity(
			_CapturedOwner, UCk_UnrealComponentGym_SourceWithActor, SpawnParams);
		if (!utils_pending_entity_script::Get_IsValid(PendingWithActor))
		{ FailClosed("UnrealComponent Driver spawn failed"); return; }
		PendingTarget = PendingWithActor.Get_EntityUnderConstruction();
		if (ck::Is_NOT_Valid(PendingTarget) || !Get_HasSourceContext())
		{ FailClosed("UnrealComponent Driver pending target failed"); return; }
		utils_pending_entity_script::Promise_OnConstructed(
			PendingWithActor, FCk_Delegate_EntityScript_Constructed(this, n"OnEntityConstructed"));
	}

	UFUNCTION()
	private void OnEntityConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
	{
		auto InEntity = FCk_Handle(InEntityScriptHandle);
		if (_Closed) { return; }
		if (_EntityConstructed || !Get_HasSourceContext() || ck::Is_NOT_Valid(InEntity) ||
			InEntity != PendingTarget || !utils_entity_lifetime::Has_LifetimeOwner(InEntity) ||
			utils_entity_lifetime::Get_LifetimeOwner(InEntity) != _CapturedOwner ||
			utils_owning_actor::TryGet_EntityOwningActor(InEntity) != this)
		{ FailClosed("UnrealComponent Driver entity mismatch"); return; }
		_EntityConstructed = true;
		EcsEntity = InEntity;
		InEntity.Set_DebugName(FName(f"UnrealComponent_{ComponentType}"));
		if (!Get_HasExactEntity() || !Get_HasSourceContext())
		{ FailClosed("UnrealComponent Driver entity lost"); return; }
		auto ComponentClass = Get_ComponentClass();
		if (ck::Is_NOT_Valid(ComponentClass))
		{ FailClosed("UnrealComponent Driver class missing"); return; }
		const auto Params = utils_unreal_component::Make_Params(ComponentClass,
			ECk_UnrealComponent_TickPolicy::DoNotTick, n"UnrealComponentGym");
		ComponentHandle = utils_unreal_component::Add(InEntity, Params);
		if (ck::Is_NOT_Valid(ComponentHandle) || !Get_HasExactEntity() || !Get_HasSourceContext())
		{ FailClosed("UnrealComponent Driver component add failed"); return; }
		utils_unreal_component::BindTo_OnAdded(
			ComponentHandle, FCk_Delegate_UnrealComponent_OnAdded(this, n"OnComponentAdded"));
		if (!Get_HasSourceContext() || !Get_HasExactEntity())
		{ FailClosed("UnrealComponent Driver component bind failed"); return; }
		auto TimerParams = FCk_Timer_Spec();
		TimerParams.Set_StartingState(ECk_Timer_State::Running);
		TimerParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		TimerHandle = utils_timer::Add(InEntity, TimerParams);
		if (ck::Is_NOT_Valid(TimerHandle))
		{ FailClosed("UnrealComponent Driver timer add failed"); return; }
		utils_timer::BindTo_OnUpdate(TimerHandle, FCk_Delegate_Timer(this, n"OnFrameTick"));
		TryPublishReady();
	}
	UFUNCTION()
	private void OnComponentAdded(FCk_Handle_UnrealComponent InHandle)
	{
		if (_Closed) { return; }
		if (_StyleConfigured || !Get_HasSourceContext() ||
			ck::Is_NOT_Valid(InHandle) || InHandle != ComponentHandle ||
			!Get_HasExactEntity())
		{ FailClosed("UnrealComponent Driver OnAdded identity mismatch"); return; }
		UActorComponent Comp;
		AActor Host;
		if (!UCk_UnrealComponentGym_Runtime_UE::TryCaptureSourceComponent(
			this, EcsEntity, FCk_Handle(InHandle), _CapturedRole, Comp, Host))
		{ FailClosed("UnrealComponent Driver OnAdded capture failed"); return; }
		_CapturedComponent = Comp;
		_ComponentHost = Host;
		if (!Get_HasExactComponent(false))
		{ FailClosed("UnrealComponent Driver OnAdded composition failed"); return; }

		// Configure exactly the captured native component. Every setter rechecks
		// source epoch and physical identity before the next mutation.
		// Per-component-type configuration. Each branch casts to the concrete
		// class and sets up class-specific defaults so the component renders
		// or does something visible.
		if (ComponentType == ECk_UnrealComponentGym_Type::StaticMesh)
		{
			auto Mesh = Cast<UStaticMeshComponent>(Comp);
			if (ck::IsValid(Mesh) && ck::IsValid(_CubeMesh))
			{
				Mesh.SetStaticMesh(_CubeMesh);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Mesh.SetWorldScale3D(FVector(1.0f, 1.0f, 1.0f));
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
			}
			else { FailClosed(); return; }
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::PointLight)
		{
			auto Light = Cast<UPointLightComponent>(Comp);
			if (ck::IsValid(Light))
			{
				Light.SetIntensity(50000.0f);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Light.SetAttenuationRadius(1200.0f);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Light.SetLightColor(FLinearColor(1.0f, 0.4f, 0.2f, 1.0f));
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
			}
			else { FailClosed(); return; }
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::SpotLight)
		{
			auto Light = Cast<USpotLightComponent>(Comp);
			if (ck::IsValid(Light))
			{
				Light.SetIntensity(100000.0f);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Light.SetAttenuationRadius(1500.0f);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Light.SetInnerConeAngle(20.0f);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Light.SetOuterConeAngle(35.0f);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Light.SetLightColor(FLinearColor(0.3f, 0.7f, 1.0f, 1.0f));
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
			}
			else { FailClosed(); return; }
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::TextRender)
		{
			auto Text = Cast<UTextRenderComponent>(Comp);
			if (ck::IsValid(Text))
			{
				Text.SetText(FText::FromString("ENTITY"));
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Text.SetWorldSize(40.0f);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Text.SetTextRenderColor(FColor(255, 255, 0, 255));
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Text.SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
			}
			else { FailClosed(); return; }
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::Arrow)
		{
			auto Arrow = Cast<UArrowComponent>(Comp);
			if (ck::IsValid(Arrow))
			{
				Arrow.SetArrowColor(FLinearColor(1.0f, 0.0f, 1.0f, 1.0f));
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Arrow.ArrowSize = 5.0f;
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Arrow.ArrowLength = 100.0f;
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Arrow.bIsScreenSizeScaled = false;
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				// UArrowComponent is editor-only by default - opt in to game rendering.
				Arrow.bIsEditorOnly = false;
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Arrow.SetHiddenInGame(false);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
			}
			else { FailClosed(); return; }
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::Box)
		{
			auto Box = Cast<UBoxComponent>(Comp);
			if (ck::IsValid(Box))
			{
				Box.SetBoxExtent(FVector(80.0f, 80.0f, 80.0f), true);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Box.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Box.SetHiddenInGame(false);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
			}
			else { FailClosed(); return; }
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::Sphere)
		{
			auto Sphere = Cast<USphereComponent>(Comp);
			if (ck::IsValid(Sphere))
			{
				Sphere.SetSphereRadius(80.0f, true);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Sphere.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Sphere.SetHiddenInGame(false);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
			}
			else { FailClosed(); return; }
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::Capsule)
		{
			auto Capsule = Cast<UCapsuleComponent>(Comp);
			if (ck::IsValid(Capsule))
			{
				Capsule.SetCapsuleSize(50.0f, 100.0f, true);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Capsule.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
				Capsule.SetHiddenInGame(false);
				if (!Get_HasExactComponent(false)) { FailClosed(); return; }
			}
			else { FailClosed(); return; }
		}
		else { FailClosed(); return; }
		if (_Closed || !Get_HasExactComponent())
		{ FailClosed(); return; }
		_StyleConfigured = true;
		OnAddedFireCount = OnAddedFireCount + 1;
		TryPublishReady();
	}

	UFUNCTION(BlueprintOverride)
	void EndPlay(EEndPlayReason Reason)
	{
		if (!_Closed && !_Ending && ck::IsValid(_CapturedSource) &&
			!_CapturedSource.IsActorBeingDestroyed() &&
			_CapturedSource.Get_CanAnimateSource())
		{ _CapturedSource.Request_Fail("UnrealComponent Driver ended during active source"); }
		_Ending = true;
		_Closed = true;
		ComponentReady = false;
	}

	UFUNCTION()
	private void OnFrameTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (_Closed) { return; }
		if (!_ReadyPublished)
		{
			TryPublishReady();
			if (!_ReadyPublished) { return; }
		}
		if (InHandle != TimerHandle || !Get_HasValidComposition())
		{ FailClosed("UnrealComponent Driver tick lost physical composition"); return; }
		// The GameState closes animation throughout reset preparation and the
		// physical pose/queue readback boundary.
		if (!_CapturedSource.Get_CanAnimateSource()) { return; }
		auto DeltaSeconds = float32(InDeltaT.Get_Seconds());
		if (!Math::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f ||
			!Math::IsFinite(ElapsedTime + DeltaSeconds) ||
			ElapsedTime + DeltaSeconds > 100000.0f)
		{ FailClosed("UnrealComponent Driver tick delta invalid"); return; }
		ElapsedTime = ElapsedTime + DeltaSeconds;
		auto Angle = ElapsedTime * OrbitSpeed;
		auto Offset = FVector(
			OrbitRadius * Math::Cos(Angle),
			OrbitRadius * Math::Sin(Angle),
			OrbitBaseHeight + OrbitBobAmplitude * Math::Sin(Angle * 2.0f));
		auto NewLocation = OrbitOrigin + Offset;
		if (NewLocation.ContainsNaN() || NewLocation.Size() > 100000.0f)
		{ FailClosed("UnrealComponent Driver orbit invalid"); return; }
		utils_transform::Request_SetLocation(EcsEntity, NewLocation, ECk_LocalWorld::World);
		if (!Get_HasValidComposition() || !_CapturedSource.Get_CanAnimateSource())
		{ FailClosed("UnrealComponent Driver lost source after location"); return; }
		if (_CapturedType == ECk_UnrealComponentGym_Type::Arrow ||
			_CapturedType == ECk_UnrealComponentGym_Type::SpotLight ||
			_CapturedType == ECk_UnrealComponentGym_Type::TextRender)
		{
			auto Yaw = (Math::Atan2(Offset.Y, Offset.X) * 180.0f / 3.14159265f) + 90.0f;
			if (!Math::IsFinite(Yaw))
			{ FailClosed("UnrealComponent Driver yaw invalid"); return; }
			utils_transform::Request_SetRotation(
				EcsEntity, FRotator(0.0f, Yaw, 0.0f), ECk_LocalWorld::World);
			if (!Get_HasValidComposition())
			{ FailClosed("UnrealComponent Driver lost source after rotation"); return; }
		}
	}
	UClass Get_ComponentClass() const
	{
		if (ComponentType == ECk_UnrealComponentGym_Type::StaticMesh) { return UStaticMeshComponent; }
		if (ComponentType == ECk_UnrealComponentGym_Type::PointLight) { return UPointLightComponent; }
		if (ComponentType == ECk_UnrealComponentGym_Type::SpotLight)  { return USpotLightComponent; }
		if (ComponentType == ECk_UnrealComponentGym_Type::TextRender) { return UTextRenderComponent; }
		if (ComponentType == ECk_UnrealComponentGym_Type::Arrow)      { return UArrowComponent; }
		if (ComponentType == ECk_UnrealComponentGym_Type::Box)        { return UBoxComponent; }
		if (ComponentType == ECk_UnrealComponentGym_Type::Sphere)     { return USphereComponent; }
		if (ComponentType == ECk_UnrealComponentGym_Type::Capsule)    { return UCapsuleComponent; }
		return nullptr;
	}
}
