//============================================================================
// CkUnrealComponent Gym - Driver Actor
//
// Each station spawns one ACk_UnrealComponentGym_Driver. The driver (steps 1-3
// in its entity script's DoConstruct):
//   1. Spawns an ECS entity with Transform fragment.
//   2. Calls UCk_Utils_UnrealComponent_UE::Add() to attach a UActorComponent
//      of the chosen ComponentType to the entity. The component is created
//      as a subobject of UCk_ComponentHost_Subsystem_UE (no AActor outer).
//   3. Binds OnAdded so it can configure the component once the Setup
//      processor has registered it.
//   4. Animates the entity transform per-frame; the PushTransform processor
//      replicates that motion onto the component.
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
	default bReplicates = true;
	default bReplicateUsingRegisteredSubObjectList = true;
	default bAlwaysRelevant = true;

	// The driver itself has no visible representation - the attached component
	// is the visual. We keep a SceneRoot so the actor has a transform.
	UPROPERTY(DefaultComponent, RootComponent)
	USceneComponent SceneRoot;

	UPROPERTY(DefaultComponent)
	UCk_EntityBridge_ActorComponent_UE EntityBridge;

	UPROPERTY(ExposeOnSpawn)
	ECk_UnrealComponentGym_Type ComponentType = ECk_UnrealComponentGym_Type::StaticMesh;

	// --- ECS state (server-authoritative) ---
	FCk_Handle EcsEntity;
	FCk_Handle_UnrealComponent ComponentHandle;
	bool ComponentReady = false;
	int32 OnAddedFireCount = 0;

	// --- Animation state ---
	FVector OrbitOrigin = FVector::ZeroVector;
	bool OrbitOriginCaptured = false;
	float32 ElapsedTime = 0.0f;
	float32 OrbitRadius = 120.0f;
	float32 OrbitSpeed = 1.5f;
	float32 OrbitBaseHeight = 180.0f;
	float32 OrbitBobAmplitude = 50.0f;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
	    auto _CkPerfScope = ck::ScopedStat();
		if (HasAuthority() == false)
		{ return; }

		EntityBridge.Request_SpawnLinkedEntity(UCk_UnrealComponentGym_Driver_EntityScript);
	}

	UFUNCTION()
	private void OnComponentAdded(FCk_Handle_UnrealComponent InHandle)
	{
		OnAddedFireCount = OnAddedFireCount + 1;
		ComponentReady = true;

		auto Comp = utils_unreal_component::Get_Component(InHandle);
		if (ck::IsValid(Comp) == false)
		{ return; }

		// Per-component-type configuration. Each branch casts to the concrete
		// class and sets up class-specific defaults so the component renders
		// or does something visible.
		if (ComponentType == ECk_UnrealComponentGym_Type::StaticMesh)
		{
			auto Mesh = Cast<UStaticMeshComponent>(Comp);
			if (ck::IsValid(Mesh))
			{
				auto CubeMesh = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Cube.Cube"));
				if (ck::IsValid(CubeMesh)) { Mesh.SetStaticMesh(CubeMesh); }
				Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Mesh.SetWorldScale3D(FVector(1.0f, 1.0f, 1.0f));
			}
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::PointLight)
		{
			auto Light = Cast<UPointLightComponent>(Comp);
			if (ck::IsValid(Light))
			{
				Light.SetIntensity(50000.0f);
				Light.SetAttenuationRadius(1200.0f);
				Light.SetLightColor(FLinearColor(1.0f, 0.4f, 0.2f, 1.0f));
			}
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::SpotLight)
		{
			auto Light = Cast<USpotLightComponent>(Comp);
			if (ck::IsValid(Light))
			{
				Light.SetIntensity(100000.0f);
				Light.SetAttenuationRadius(1500.0f);
				Light.SetInnerConeAngle(20.0f);
				Light.SetOuterConeAngle(35.0f);
				Light.SetLightColor(FLinearColor(0.3f, 0.7f, 1.0f, 1.0f));
			}
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::TextRender)
		{
			auto Text = Cast<UTextRenderComponent>(Comp);
			if (ck::IsValid(Text))
			{
				Text.SetText(FText::FromString("ENTITY"));
				Text.SetWorldSize(40.0f);
				Text.SetTextRenderColor(FColor(255, 255, 0, 255));
				Text.SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
			}
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::Arrow)
		{
			auto Arrow = Cast<UArrowComponent>(Comp);
			if (ck::IsValid(Arrow))
			{
				Arrow.SetArrowColor(FLinearColor(1.0f, 0.0f, 1.0f, 1.0f));
				Arrow.ArrowSize = 5.0f;
				Arrow.ArrowLength = 100.0f;
				Arrow.bIsScreenSizeScaled = false;
				// UArrowComponent is editor-only by default - opt in to game rendering.
				Arrow.bIsEditorOnly = false;
				Arrow.SetHiddenInGame(false);
			}
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::Box)
		{
			auto Box = Cast<UBoxComponent>(Comp);
			if (ck::IsValid(Box))
			{
				Box.SetBoxExtent(FVector(80.0f, 80.0f, 80.0f), true);
				Box.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Box.SetHiddenInGame(false);
			}
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::Sphere)
		{
			auto Sphere = Cast<USphereComponent>(Comp);
			if (ck::IsValid(Sphere))
			{
				Sphere.SetSphereRadius(80.0f, true);
				Sphere.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Sphere.SetHiddenInGame(false);
			}
		}
		else if (ComponentType == ECk_UnrealComponentGym_Type::Capsule)
		{
			auto Capsule = Cast<UCapsuleComponent>(Comp);
			if (ck::IsValid(Capsule))
			{
				Capsule.SetCapsuleSize(50.0f, 100.0f, true);
				Capsule.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Capsule.SetHiddenInGame(false);
			}
		}
	}

	UFUNCTION()
	private void OnFrameTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (ck::IsValid(EcsEntity) == false)
		{ return; }

		auto DeltaSeconds = float32(InDeltaT.Get_Seconds());
		ElapsedTime = ElapsedTime + DeltaSeconds;

		// Capture the spawn origin once. The driver actor's transform is bound
		// to the entity, so each Request_SetLocation moves the driver itself
		// we cannot use GetActorTransform() as a stable orbit center.
		if (OrbitOriginCaptured == false)
		{
			OrbitOrigin = GetActorTransform().GetLocation();
			OrbitOriginCaptured = true;
		}

		// Orbit the entity in a small circle anchored at the spawn origin so the
		// component stays inside the station alcove. PushTransform mirrors this
		// onto the scene component each tick.
		auto Angle = ElapsedTime * OrbitSpeed;
		auto Offset = FVector(
			OrbitRadius * Math::Cos(Angle),
			OrbitRadius * Math::Sin(Angle),
			OrbitBaseHeight + OrbitBobAmplitude * Math::Sin(Angle * 2.0f));

		auto NewLocation = OrbitOrigin + Offset;
		utils_transform::Request_SetLocation(EcsEntity, NewLocation, ECk_LocalWorld::World);

		// For directional components, also rotate so the orientation is visible.
		if (ComponentType == ECk_UnrealComponentGym_Type::Arrow
			|| ComponentType == ECk_UnrealComponentGym_Type::SpotLight
			|| ComponentType == ECk_UnrealComponentGym_Type::TextRender)
		{
			auto Yaw = (Math::Atan2(Offset.Y, Offset.X) * 180.0f / 3.14159265f) + 90.0f;
			utils_transform::Request_SetRotation(EcsEntity, FRotator(0.0f, Yaw, 0.0f), ECk_LocalWorld::World);
		}
	}

	UClass Get_ComponentClass()
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

// Composes the driver's entity on the server: the Unreal component of the driver's ComponentType (its OnAdded bound
// to the driver) and the per-frame tick.
class UCk_UnrealComponentGym_Driver_EntityScript : UCk_EntityScript_WithActor_UE
{
	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
		auto Actor = Cast<ACk_UnrealComponentGym_Driver>(DoGet_Actor());
		if (!Actor.HasAuthority())
		{ return ECk_EntityScript_ConstructionFlow::Finished; }

		if (System::IsServer() == false)
		{ return ECk_EntityScript_ConstructionFlow::Finished; }

		InHandle.Set_DebugName(FName(f"UnrealComponent_{Actor.ComponentType}"));
		// Stored here rather than at Ready: the tick composed below reads it.
		Actor.EcsEntity = InHandle;

		// UCk_EntityScript_WithActor_UE already adds the Transform fragment
		// (linked to the owning actor's RootComponent) - do not re-add.

		auto ComponentClass = Actor.Get_ComponentClass();
		if (ck::IsValid(ComponentClass) == false)
		{ return ECk_EntityScript_ConstructionFlow::Finished; }

		const auto Params = utils_unreal_component::Make_Params(ComponentClass, ECk_UnrealComponent_TickPolicy::DoNotTick, n"UnrealComponentGym");
		Actor.ComponentHandle = utils_unreal_component::Add(InHandle, Params);

		utils_unreal_component::BindTo_OnAdded(
			Actor.ComponentHandle,
			FCk_Delegate_UnrealComponent_OnAdded(Actor, n"OnComponentAdded"));

		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(Actor, n"OnFrameTick"));

		return ECk_EntityScript_ConstructionFlow::Finished;
	}
}
