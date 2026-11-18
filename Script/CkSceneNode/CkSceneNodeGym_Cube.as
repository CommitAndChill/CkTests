UENUM()
enum ECk_SceneNodeGym_Behavior
{
	ParentChild,
	OffsetUpdates,
	MultipleChildren,
	Hierarchy,
	PropagateOnly,
	AnchorOffset,
	ECk_MAX
}

UCLASS(Blueprintable)
class ACk_SceneNodeGym_Cube : AActor
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

	UPROPERTY(DefaultComponent)
	UCk_EntityBridge_ActorComponent_UE EntityBridge;

	UPROPERTY(ExposeOnSpawn)
	ECk_SceneNodeGym_Behavior Behavior = ECk_SceneNodeGym_Behavior::ParentChild;

	// --- Shared state ---
	FCk_Handle EcsEntity;
	FCk_Handle_Transform ParentTransform;
	float32 ElapsedTime = 0.0f;

	// --- ParentChild state ---
	FCk_Handle_SceneNode ChildNode;
	ACk_SceneNodeGym_ChildCube ChildCubeActor;
	float32 OrbitRadius = 150.0f;
	float32 OrbitSpeed = 1.5f;

	// --- OffsetUpdates state ---
	FCk_Handle_SceneNode OffsetNode;
	ACk_SceneNodeGym_ChildCube OffsetCubeActor;
	float32 AnimSpeed = 1.0f;

	// --- MultipleChildren state ---
	TArray<FCk_Handle_SceneNode> ChildNodes;
	TArray<ACk_SceneNodeGym_ChildCube> ChildCubeActors;
	float32 ParentRotationSpeed = 30.0f;

	// --- Hierarchy state ---
	FCk_Handle_SceneNode HierarchyChild;
	FCk_Handle_SceneNode HierarchyGrandchild;
	ACk_SceneNodeGym_ChildCube HierarchyChildCube;
	ACk_SceneNodeGym_ChildCube HierarchyGrandchildCube;
	float32 RootRotationSpeed = 25.0f;
	float32 ChildRotationSpeed = 45.0f;

	// --- PropagateOnly state ---
	// Solar-system style: only the root rotates. Planet + moon have static
	// local offsets and no per-frame updates, so if scene-node transform
	// propagation works, planet should orbit root and moon should orbit
	// planet via parent motion alone. If propagation is broken, planet
	// and moon stay frozen at their initial world positions.
	FCk_Handle_SceneNode PropagatePlanet;
	FCk_Handle_SceneNode PropagateMoon;
	ACk_SceneNodeGym_ChildCube PropagatePlanetCube;
	ACk_SceneNodeGym_ChildCube PropagateMoonCube;
	float32 PropagateRootRotationSpeed = 40.0f;
	float32 PropagatePlanetOrbitRadius = 160.0f;
	float32 PropagateMoonOrbitRadius = 80.0f;

	// --- AnchorOffset state ---
	// The cube's own Mesh (a moving USceneComponent) is the anchor. A SceneNode
	// follows it at a fixed relative offset via CreateAndAttachToUnrealComponent;
	// rotating the actor moves the mesh in world so the follower tracks it, and
	// the offset height is animated each frame to show runtime-mutable offsets.
	FCk_Handle_SceneNode AnchorNode;
	ACk_SceneNodeGym_ChildCube AnchorCubeActor;
	float32 AnchorRotationSpeed = 35.0f;
	FVector AnchorBaseOffset = FVector(180.0f, 0.0f, 40.0f);

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
	    auto _CkPerfScope = ck::ScopedStat();
		TextRenderer.SetText(ck::Text(f"{Behavior}"));

		if (HasAuthority())
		{
			EntityBridge.Promise_OnEntityReady(FCk_Delegate_EntityBridge_OnEntityReady(this, n"OnEntityReady"));
			EntityBridge.Request_SpawnLinkedEntity(UCk_SceneNodeGym_Cube_EntityScript);
		}
	}

	// The scene nodes and the per-frame tick are composed by UCk_SceneNodeGym_Cube_EntityScript; this spawns the
	// child cubes that show them.
	UFUNCTION()
	private void OnEntityReady(AActor InActor, FCk_Handle InEntity)
	{
		if (System::IsServer() == false)
		{ return; }

		if (Behavior == ECk_SceneNodeGym_Behavior::ParentChild)
		{
			Setup_ParentChild();
		}
		else if (Behavior == ECk_SceneNodeGym_Behavior::OffsetUpdates)
		{
			Setup_OffsetUpdates();
		}
		else if (Behavior == ECk_SceneNodeGym_Behavior::MultipleChildren)
		{
			Setup_MultipleChildren();
		}
		else if (Behavior == ECk_SceneNodeGym_Behavior::Hierarchy)
		{
			Setup_Hierarchy();
		}
		else if (Behavior == ECk_SceneNodeGym_Behavior::PropagateOnly)
		{
			Setup_PropagateOnly();
		}
		else if (Behavior == ECk_SceneNodeGym_Behavior::AnchorOffset)
		{
			Setup_AnchorOffset();
		}
	}

	//------------------------------------------------------------------------
	// HELPERS
	//------------------------------------------------------------------------

	private ACk_SceneNodeGym_ChildCube SpawnChildCube(FString InLabel)
	{
		auto SpawnedChild = Cast<ACk_SceneNodeGym_ChildCube>(
			SpawnActor(ACk_SceneNodeGym_ChildCube, GetActorLocation(), FRotator()));
		SpawnedChild.Set_Label(InLabel);
		return SpawnedChild;
	}

	FTransform Get_SceneNodeWorldTransform(FCk_Handle_SceneNode InNode)
	{
		// Get the parent transform handle that this scene node is attached to
		auto ParentHandle = utils_scene_node::Get_Parent(InNode);
		auto ParentWorldTransform = utils_transform::Get_EntityCurrentTransform(ParentHandle);
		auto LocalOffset = utils_scene_node::Get_Offset(InNode);
		// Compose: parent world * local offset
		return LocalOffset * ParentWorldTransform;
	}

	private void SyncChildCube(ACk_SceneNodeGym_ChildCube InCube, FCk_Handle_SceneNode InNode)
	{
		if (ck::IsValid(InCube) == false)
		{ return; }

		auto WorldTransform = Get_SceneNodeWorldTransform(InNode);
		InCube.SetActorLocationAndRotation(WorldTransform.GetLocation(), WorldTransform.GetRotation().Rotator());
		InCube.SetActorScale3D(WorldTransform.GetScale3D());
	}

	//------------------------------------------------------------------------
	// SETUP FUNCTIONS (child cubes; the nodes they show are composed by the entity script)
	//------------------------------------------------------------------------

	private void Setup_ParentChild()
	{
		ChildCubeActor = SpawnChildCube("Child");
	}

	private void Setup_OffsetUpdates()
	{
		OffsetCubeActor = SpawnChildCube("Offset");
	}

	private void Setup_MultipleChildren()
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			ChildCubeActors.Add(SpawnChildCube(f"Child {Index + 1}"));
		}
	}

	private void Setup_Hierarchy()
	{
		HierarchyChildCube = SpawnChildCube("Child");
		HierarchyGrandchildCube = SpawnChildCube("Grandchild");
	}

	private void Setup_PropagateOnly()
	{
		PropagatePlanetCube = SpawnChildCube("Planet");
		PropagateMoonCube = SpawnChildCube("Moon");
	}

	private void Setup_AnchorOffset()
	{
		AnchorCubeActor = SpawnChildCube("Anchored");
	}

	//------------------------------------------------------------------------
	// PER-FRAME TICK
	//------------------------------------------------------------------------

	UFUNCTION()
	private void OnFrameTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		auto DeltaSeconds = float32(InDeltaT.Get_Seconds());
		ElapsedTime = ElapsedTime + DeltaSeconds;

		if (Behavior == ECk_SceneNodeGym_Behavior::ParentChild)
		{
			Tick_ParentChild(DeltaSeconds);
		}
		else if (Behavior == ECk_SceneNodeGym_Behavior::OffsetUpdates)
		{
			Tick_OffsetUpdates(DeltaSeconds);
		}
		else if (Behavior == ECk_SceneNodeGym_Behavior::MultipleChildren)
		{
			Tick_MultipleChildren(DeltaSeconds);
		}
		else if (Behavior == ECk_SceneNodeGym_Behavior::Hierarchy)
		{
			Tick_Hierarchy(DeltaSeconds);
		}
		else if (Behavior == ECk_SceneNodeGym_Behavior::PropagateOnly)
		{
			Tick_PropagateOnly(DeltaSeconds);
		}
		else if (Behavior == ECk_SceneNodeGym_Behavior::AnchorOffset)
		{
			Tick_AnchorOffset(DeltaSeconds);
		}
	}

	private void Tick_ParentChild(float32 DeltaSeconds)
	{
		// Orbit child around parent using offset updates
		auto Angle = ElapsedTime * OrbitSpeed;
		auto OffsetX = OrbitRadius * Math::Cos(Angle);
		auto OffsetY = OrbitRadius * Math::Sin(Angle);

		utils_scene_node::Request_UpdateOffset_Location(ChildNode, FVector(OffsetX, OffsetY, 0.0f), ECk_RelativeAbsolute::Absolute);
		SyncChildCube(ChildCubeActor, ChildNode);
	}

	private void Tick_OffsetUpdates(float32 DeltaSeconds)
	{
		// Animate all three offset components simultaneously
		auto SineVal = Math::Sin(ElapsedTime * AnimSpeed);
		auto CosVal = Math::Cos(ElapsedTime * AnimSpeed);

		// Location: oscillate along X
		auto LocationOffset = FVector(100.0f + SineVal * 80.0f, CosVal * 50.0f, 50.0f + SineVal * 30.0f);
		utils_scene_node::Request_UpdateOffset_Location(OffsetNode, LocationOffset, ECk_RelativeAbsolute::Absolute);

		// Rotation: oscillate yaw
		auto RotationOffset = FRotator(0.0f, SineVal * 45.0f, 0.0f);
		utils_scene_node::Request_UpdateOffset_Rotation(OffsetNode, RotationOffset, ECk_RelativeAbsolute::Absolute);

		// Scale: pulse between 0.3 and 0.7
		auto ScaleVal = 0.5f + SineVal * 0.2f;
		utils_scene_node::Request_UpdateOffset_Scale(OffsetNode, FVector(ScaleVal, ScaleVal, ScaleVal), ECk_RelativeAbsolute::Absolute);

		SyncChildCube(OffsetCubeActor, OffsetNode);
	}

	private void Tick_MultipleChildren(float32 DeltaSeconds)
	{
		// Rotate the parent - children follow automatically
		utils_transform::Request_AddRotationOffset(EcsEntity, FRotator(0.0f, ParentRotationSpeed * DeltaSeconds, 0.0f), ECk_LocalWorld::World);

		// Bounded by the cubes: the nodes are composed before the cubes are spawned at Ready.
		for (int32 Index = 0; Index < ChildCubeActors.Num(); ++Index)
		{
			SyncChildCube(ChildCubeActors[Index], ChildNodes[Index]);
		}
	}

	private void Tick_Hierarchy(float32 DeltaSeconds)
	{
		// Rotate root
		utils_transform::Request_AddRotationOffset(EcsEntity, FRotator(0.0f, RootRotationSpeed * DeltaSeconds, 0.0f), ECk_LocalWorld::World);

		// Rotate child's offset
		auto ChildRotation = FRotator(0.0f, ChildRotationSpeed * DeltaSeconds, 0.0f);
		utils_scene_node::Request_UpdateOffset_Rotation(HierarchyChild, ChildRotation, ECk_RelativeAbsolute::Relative);

		// Grandchild gets its own slow counter-rotation
		auto GrandchildRotation = FRotator(0.0f, -ChildRotationSpeed * 0.5f * DeltaSeconds, 0.0f);
		utils_scene_node::Request_UpdateOffset_Rotation(HierarchyGrandchild, GrandchildRotation, ECk_RelativeAbsolute::Relative);

		SyncChildCube(HierarchyChildCube, HierarchyChild);
		SyncChildCube(HierarchyGrandchildCube, HierarchyGrandchild);
	}

	private void Tick_PropagateOnly(float32 DeltaSeconds)
	{
		// ONLY the root rotates. Planet and Moon have their offsets locked in
		// at setup and are never touched again - so their world transforms
		// depend entirely on scene-node transform propagation picking up the
		// root's rotation change each frame.
		utils_transform::Request_AddRotationOffset(EcsEntity, FRotator(0.0f, PropagateRootRotationSpeed * DeltaSeconds, 0.0f), ECk_LocalWorld::World);

		SyncChildCube(PropagatePlanetCube, PropagatePlanet);
		SyncChildCube(PropagateMoonCube, PropagateMoon);
	}

	private void Tick_AnchorOffset(float32 DeltaSeconds)
	{
		// Rotate the whole actor - its Mesh (the anchor component) moves in world,
		// so the follower node tracks it at the offset.
		utils_transform::Request_AddRotationOffset(EcsEntity, FRotator(0.0f, AnchorRotationSpeed * DeltaSeconds, 0.0f), ECk_LocalWorld::World);

		// Animate the offset height to show the anchor offset is runtime-mutable
		// Request_UpdateOffset now sticks for anchor-follow nodes (was a no-op before).
		auto BobbedOffset = AnchorBaseOffset + FVector(0.0f, 0.0f, Math::Sin(ElapsedTime * 1.5f) * 60.0f);
		utils_scene_node::Request_UpdateOffset_Location(AnchorNode, BobbedOffset, ECk_RelativeAbsolute::Absolute);

		SyncAnchorCube();
	}

	private void SyncAnchorCube()
	{
		if (ck::IsValid(AnchorCubeActor) == false)
		{ return; }

		// Read the node's OWN composed world (offset * mesh world), not the structural-
		// parent composition - the Unreal anchor drives this node directly.
		auto NodeWorld = utils_transform::Get_EntityCurrentTransform(AnchorNode.As_Transform());
		AnchorCubeActor.SetActorLocationAndRotation(NodeWorld.GetLocation(), NodeWorld.GetRotation().Rotator());
		AnchorCubeActor.SetActorScale3D(NodeWorld.GetScale3D());
	}
}

// Composes the cube's entity on the server: the scene nodes for its Behavior and the per-frame tick.
class UCk_SceneNodeGym_Cube_EntityScript : UCk_EntityScript_WithActor_UE
{
	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
		auto Actor = Cast<ACk_SceneNodeGym_Cube>(DoGet_Actor());
		if (!Actor.HasAuthority())
		{ return ECk_EntityScript_ConstructionFlow::Finished; }

		if (System::IsServer() == false)
		{ return ECk_EntityScript_ConstructionFlow::Finished; }

		InHandle.Set_DebugName(FName(f"Cube_{Actor.Behavior}"));
		// Stored here rather than at Ready: the tick composed below reads it.
		Actor.EcsEntity = InHandle;
		Actor.ParentTransform = InHandle.As_Transform();

		if (Actor.Behavior == ECk_SceneNodeGym_Behavior::ParentChild)
		{
			Compose_ParentChild(Actor);
		}
		else if (Actor.Behavior == ECk_SceneNodeGym_Behavior::OffsetUpdates)
		{
			Compose_OffsetUpdates(Actor);
		}
		else if (Actor.Behavior == ECk_SceneNodeGym_Behavior::MultipleChildren)
		{
			Compose_MultipleChildren(Actor);
		}
		else if (Actor.Behavior == ECk_SceneNodeGym_Behavior::Hierarchy)
		{
			Compose_Hierarchy(Actor);
		}
		else if (Actor.Behavior == ECk_SceneNodeGym_Behavior::PropagateOnly)
		{
			Compose_PropagateOnly(Actor);
		}
		else if (Actor.Behavior == ECk_SceneNodeGym_Behavior::AnchorOffset)
		{
			Compose_AnchorOffset(Actor);
		}

		// Per-frame tick
		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(Actor, n"OnFrameTick"));

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	private void Compose_ParentChild(ACk_SceneNodeGym_Cube InActor)
	{
		auto ChildLocalTransform = FTransform(FRotator(), FVector(InActor.OrbitRadius, 0.0f, 0.0f), FVector(0.5f, 0.5f, 0.5f));
		InActor.ChildNode = utils_scene_node::Create(InActor.ParentTransform, ChildLocalTransform);
	}

	private void Compose_OffsetUpdates(ACk_SceneNodeGym_Cube InActor)
	{
		auto ChildLocalTransform = FTransform(FRotator(), FVector(100.0f, 0.0f, 50.0f), FVector(0.5f, 0.5f, 0.5f));
		InActor.OffsetNode = utils_scene_node::Create(InActor.ParentTransform, ChildLocalTransform);
	}

	private void Compose_MultipleChildren(ACk_SceneNodeGym_Cube InActor)
	{
		auto Offsets = TArray<FVector>();
		Offsets.Add(FVector(120.0f, 0.0f, 0.0f));
		Offsets.Add(FVector(0.0f, 120.0f, 0.0f));
		Offsets.Add(FVector(-120.0f, 0.0f, 0.0f));
		Offsets.Add(FVector(0.0f, -120.0f, 0.0f));

		for (int32 Index = 0; Index < 4; ++Index)
		{
			auto ChildLocalTransform = FTransform(FRotator(), Offsets[Index], FVector(0.4f, 0.4f, 0.4f));
			auto Node = utils_scene_node::Create(InActor.ParentTransform, ChildLocalTransform);
			InActor.ChildNodes.Add(Node);
		}
	}

	private void Compose_Hierarchy(ACk_SceneNodeGym_Cube InActor)
	{
		// Root -> Child
		auto ChildLocalTransform = FTransform(FRotator(), FVector(120.0f, 0.0f, 0.0f), FVector(0.7f, 0.7f, 0.7f));
		InActor.HierarchyChild = utils_scene_node::Create(InActor.ParentTransform, ChildLocalTransform);

		// Child -> Grandchild (parented to child's transform, not root)
		auto ChildTransformHandle = InActor.HierarchyChild.As_Transform();
		auto GrandchildLocalTransform = FTransform(FRotator(), FVector(100.0f, 0.0f, 0.0f), FVector(0.6f, 0.6f, 0.6f));
		InActor.HierarchyGrandchild = utils_scene_node::Create(ChildTransformHandle, GrandchildLocalTransform);
	}

	private void Compose_PropagateOnly(ACk_SceneNodeGym_Cube InActor)
	{
		// Sun (root) is the rotating parent this actor is attached to. The
		// planet and moon have static local offsets and NO per-frame updates
		// of their own - purely relying on scene-node transform propagation
		// to inherit the root's rotation.
		auto PlanetLocalTransform = FTransform(FRotator(), FVector(InActor.PropagatePlanetOrbitRadius, 0.0f, 0.0f), FVector(0.6f, 0.6f, 0.6f));
		InActor.PropagatePlanet = utils_scene_node::Create(InActor.ParentTransform, PlanetLocalTransform);

		auto PlanetTransformHandle = InActor.PropagatePlanet.As_Transform();
		auto MoonLocalTransform = FTransform(FRotator(), FVector(InActor.PropagateMoonOrbitRadius, 0.0f, 0.0f), FVector(0.4f, 0.4f, 0.4f));
		InActor.PropagateMoon = utils_scene_node::Create(PlanetTransformHandle, MoonLocalTransform);
	}

	private void Compose_AnchorOffset(ACk_SceneNodeGym_Cube InActor)
	{
		// Attach a SceneNode to the actor's Mesh component (a foreign USceneComponent)
		// at a fixed relative offset. The node follows the mesh's world at that offset
		// it never drives the mesh back (read-only follower).
		auto Offset = FTransform(FRotator(), InActor.AnchorBaseOffset, FVector(0.5f, 0.5f, 0.5f));
		InActor.AnchorNode = utils_scene_node::CreateAndAttachToUnrealComponent(InActor.ParentTransform, InActor.Mesh, Offset);
	}
}
