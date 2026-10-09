//============================================================================
// CkGymStation_EntityScript
//
// Pure-ECS GymStation. The station entity is the root of a SceneNode hierarchy;
// every visible piece (body geometry, text, spotlight) and every anchor is its
// own child SceneNode entity, with a UActorComponent attached via CkUnrealComponent.
//
// No Actor is involved. SceneNode propagation flows the station's world
// transform through the hierarchy; CkUnrealComponent's PushTransform processor
// mirrors each child's world transform onto its component every tick. Moving
// the station via Request_SetTransform automatically propagates to every piece.
//
// Geometry conventions:
//   - Pivot at alcove centre on the ground (Z=0).
//   - Alcove opens toward +X (player approaches from world +X looking -X).
//   - Width along Y, Depth along X, Height along Z.
//   - Width / Depth / Height in 100xcm units (Width=6 -> 600cm).
//   - WallThickness / FloorThickness / TrimDepth in cm.
//
// Pieces (lit + opaque, lit by /Engine/BasicShapes/Cube's BasicShapeMaterial):
//   - Back wall + floor stage (BodyColour)
//   - Four trim pieces framing the back wall (TrimColour)
//   - Title + description text on the back wall (UTextRenderComponent)
//   - Floor description text reading from above (Pitch=90)
//   - Spotlight illuminating the back wall (USpotLightComponent)
//
// Anchors:
//   - 7 anchor SceneNodes are always created - they are the canonical
//     world-position source for Get_*WorldLocation() getters. The getters
//     read the SceneNode's world transform via utils_transform.
//   - When ShowAnchors=true, each anchor SceneNode also gets a small PMG
//     sphere visual via Add_Sphere. Cyan for the 4 agent-spawn anchors,
//     magenta for the 3 panel/footprint anchors.
//
// Lifecycle:
//   - DoConstruct: build SceneNode hierarchy + attach all components + setup
//     anchor visuals if requested. Component setup is async - OnAdded
//     handlers configure each component once Setup processor instantiates it.
//   - DoEndPlay: nothing manual. SceneNode children cascade-destroy with the
//     station; each child entity's CkUnrealComponent EndPlay processor tears
//     down the component.
//
// Coexistence note: ACk_GymStation (the static-mesh actor in CkGymStation.as)
// remains in use by ACk_NavigationGym_GameMode::BeginPlay until that gym is
// migrated to spawn UCk_EntityScript_GymStation directly.
//============================================================================

// Where the floor description sits along the alcove's depth (X axis).
//   Front  -> toward +X (the alcove opening)
//   Center -> middle of the floor stage along X
//   Back   -> toward -X (against the back wall)
// Y position is controlled by FloorTextAlignment, mirroring wall-text behaviour.
UENUM()
enum ECk_GymStation_FloorTextPlacement
{
	Front,
	Center,
	Back
}

// Named anchor positions on a GymStation. Pass one to Get_AnchorWorldLocation
// (on the EntityScript) or to ACk_Gym_Base_PlayerController::Get_StationAnchorLocation
// (cross-gym helper) to retrieve the corresponding world position.
//
// Quick guide for picking one:
//   - FootprintCenter - alcove pivot on the floor (Z=0). Use for nav/physics/spatial
//     tests where elements need to sit on the navmesh.
//   - PanelCenter     - geometric centre of the alcove at mid-height. Use for
//     debug-shape tests so visual elements sit inside the alcove without
//     intersecting back wall / floor / trim. Default for most gyms.
//   - PanelTopFront   - top edge of the front opening. Marker / banner attach point.
//   - AgentSpawn{Front,Back,Left,Right} - perimeter anchors that respect AgentSpawnOffset.
UENUM()
enum ECk_GymStation_Anchor
{
	FootprintCenter,
	AgentSpawnFront,
	AgentSpawnBack,
	AgentSpawnLeft,
	AgentSpawnRight,
	PanelTopFront,
	PanelCenter
}

// Snapshot of all seven anchor world positions, populated by the GymStation
// EntityScript in DoConstruct. Lives on the station entity so any gym script
// can read the positions via Handle.Get_Fragment(FCkGym_Station_Anchors)
// without needing access to the EntityScript instance.
//
// Note: snapshot is computed at construction from InitialTransform x local
// offsets. If a station is moved at runtime, this fragment goes stale (the
// EntityScript's per-tick math via Get_EntityCurrentLocation stays correct,
// but the cross-gym fragment-based read does not). Acceptable for static
// stations, which is the current case for every gym.
USTRUCT()
struct FCkGym_Station_Anchors
{
	UPROPERTY() FVector FootprintCenter = FVector::ZeroVector;
	UPROPERTY() FVector AgentSpawnFront = FVector::ZeroVector;
	UPROPERTY() FVector AgentSpawnBack  = FVector::ZeroVector;
	UPROPERTY() FVector AgentSpawnLeft  = FVector::ZeroVector;
	UPROPERTY() FVector AgentSpawnRight = FVector::ZeroVector;
	UPROPERTY() FVector PanelTopFront   = FVector::ZeroVector;
	UPROPERTY() FVector PanelCenter     = FVector::ZeroVector;
}

USTRUCT()
struct FCk_GymStation_SpawnParams
{
	UPROPERTY()
	FTransform InitialTransform = FTransform::Identity;

	// Alcove dimensions - 100xcm units.
	UPROPERTY() double Width = 6.0;
	UPROPERTY() double Depth = 5.0;
	UPROPERTY() double Height = 5.0;

	// Wall thicknesses - cm.
	UPROPERTY() double WallThickness = 15.0;
	UPROPERTY() double FloorThickness = 15.0;

	// How far the trim pieces extend forward from the back wall - cm.
	UPROPERTY() double TrimDepth = 30.0;

	// How far past the alcove edge each agent-spawn anchor sits - cm.
	// 0 = anchor sits right on the alcove footprint edge. Increase for clearance
	// when spawning physical test agents that shouldn't clip the trim.
	UPROPERTY() double AgentSpawnOffset = 0.0;

	// Body colour for the back wall + floor stage.
	UPROPERTY() FLinearColor BodyColour = FLinearColor(0.02, 0.02, 0.02, 1.0);

	// Trim colour for the four frame pieces - lighter grey by default.
	UPROPERTY() FLinearColor TrimColour = FLinearColor(0.3, 0.3, 0.3, 1.0);

	// Title.
	UPROPERTY() FText TitleText = FText::FromString("Title");
	UPROPERTY() double TitleScale = 20.0;
	UPROPERTY() FColor TitleColour = FColor(255, 255, 255, 255);

	// Description (back wall).
	UPROPERTY() TArray<FText> DescriptionText;
	UPROPERTY() double DescriptionScale = 12.0;
	UPROPERTY() FColor DescriptionColour = FColor(255, 255, 255, 255);
	UPROPERTY() EHorizTextAligment TextAlignment = EHorizTextAligment::EHTA_Left;

	// Floor description (optional). Empty array = no floor text rendered.
	UPROPERTY() TArray<FText> FloorDescriptionText;
	UPROPERTY() double FloorDescriptionScale = 12.0;
	UPROPERTY() FColor FloorDescriptionColour = FColor(255, 255, 255, 255);
	UPROPERTY() EHorizTextAligment FloorTextAlignment = EHorizTextAligment::EHTA_Left;
	UPROPERTY() ECk_GymStation_FloorTextPlacement FloorTextPlacement = ECk_GymStation_FloorTextPlacement::Front;

	// Toggles.
	UPROPERTY() bool ShowSpotlight = true;
	UPROPERTY() bool ShowAnchors = false;

	// Debug overlays - when true, draws a translucent wireframe PMG box at
	// each piece's *expected* world transform (computed from InitialTransform
	// + the local-offset math). If a piece's actual rendered geometry doesn't
	// match its overlay, the resize plumbing is broken for that piece.
	// Distinct colour per piece type.
	UPROPERTY() bool ShowDebugOverlays = false;

	// When true, Width and Height are computed from the title + description
	// text content at spawn AND grown at runtime if a fragment update via
	// CkGym_Common::Update_StationDisplay would overflow the alcove. The
	// Width / Height fields above are ignored when AutoSize=true. Grow-only:
	// the alcove never shrinks, eliminating frame-to-frame "pulsing".
	UPROPERTY() bool AutoSize = false;

	// Tags to apply to the spawned station entity (via utils_entity_tag::Add)
	// so the base PC's Get_StationHandle / Get_StationTransform can find it
	// later by tag. Set by CkGym_Common::Request_SpawnNewStation when
	// translating from FCkGym_Station_SpawnParams_Payload; you don't normally
	// set this directly.
	UPROPERTY() TArray<FName> StationTags;
}

// ====================================================================================================================

USTRUCT()
struct FCkGym_StationVisibleGeometry
{
    UPROPERTY() bool AutoSize = false;
    UPROPERTY() double Width = 0.0;
    UPROPERTY() double Depth = 0.0;
    UPROPERTY() double Height = 0.0;
    UPROPERTY() FTransform StationWorldTransform = FTransform::Identity;
    // Fixed role order: six body meshes, three text pieces, spotlight.
    UPROPERTY() TArray<FCkGym_StationVisiblePiece> Pieces;
    // Fixed ECk_GymStation_Anchor order, including the hidden markers.
    UPROPERTY() TArray<FTransform> Anchors;
}

// Fixed raw creator-return slots: 0..9 piece nodes, 10..16 anchor nodes,
// 17..26 matching piece components, 27 the display timer child.
USTRUCT()
struct FCkGymStation_BaselineCreatorLedger
{
	UPROPERTY() FCk_Handle Station;
	UPROPERTY() TArray<FCk_Handle> Handles;
	UPROPERTY() TArray<FCk_Handle> Parents;
	UPROPERTY() TArray<FCk_Handle> ExtraReturns;
	UPROPERTY() TArray<FCk_Handle> ExtraParents;
	UPROPERTY() bool Started = false;
	UPROPERTY() bool Finished = false;
	UPROPERTY() bool Broken = false;
	UPROPERTY() bool UnsupportedTopologyAttempted = false;
	UPROPERTY() bool PresentationClosed = false;
}

class UCk_EntityScript_GymStation : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn) FTransform InitialTransform = FTransform::Identity;

	UPROPERTY(ExposeOnSpawn) double Width = 6.0;
	UPROPERTY(ExposeOnSpawn) double Depth = 5.0;
	UPROPERTY(ExposeOnSpawn) double Height = 5.0;

	UPROPERTY(ExposeOnSpawn) double WallThickness = 15.0;
	UPROPERTY(ExposeOnSpawn) double FloorThickness = 15.0;

	UPROPERTY(ExposeOnSpawn) double TrimDepth = 30.0;
	UPROPERTY(ExposeOnSpawn) double AgentSpawnOffset = 0.0;

	UPROPERTY(ExposeOnSpawn) FLinearColor BodyColour = FLinearColor(0.02, 0.02, 0.02, 1.0);
	UPROPERTY(ExposeOnSpawn) FLinearColor TrimColour = FLinearColor(0.3, 0.3, 0.3, 1.0);

	UPROPERTY(ExposeOnSpawn) FText TitleText = FText::FromString("Title");
	UPROPERTY(ExposeOnSpawn) double TitleScale = 20.0;
	UPROPERTY(ExposeOnSpawn) FColor TitleColour = FColor(255, 255, 255, 255);

	UPROPERTY(ExposeOnSpawn) TArray<FText> DescriptionText;
	UPROPERTY(ExposeOnSpawn) double DescriptionScale = 12.0;
	UPROPERTY(ExposeOnSpawn) FColor DescriptionColour = FColor(255, 255, 255, 255);
	UPROPERTY(ExposeOnSpawn) EHorizTextAligment TextAlignment = EHorizTextAligment::EHTA_Left;

	UPROPERTY(ExposeOnSpawn) TArray<FText> FloorDescriptionText;
	UPROPERTY(ExposeOnSpawn) double FloorDescriptionScale = 12.0;
	UPROPERTY(ExposeOnSpawn) FColor FloorDescriptionColour = FColor(255, 255, 255, 255);
	UPROPERTY(ExposeOnSpawn) EHorizTextAligment FloorTextAlignment = EHorizTextAligment::EHTA_Left;
	UPROPERTY(ExposeOnSpawn) ECk_GymStation_FloorTextPlacement FloorTextPlacement = ECk_GymStation_FloorTextPlacement::Front;

	UPROPERTY(ExposeOnSpawn) bool ShowSpotlight = true;
	UPROPERTY(ExposeOnSpawn) bool ShowAnchors = false;
	UPROPERTY(ExposeOnSpawn) bool ShowDebugOverlays = false;
	UPROPERTY(ExposeOnSpawn) bool AutoSize = false;
	UPROPERTY(ExposeOnSpawn) TArray<FName> StationTags;

	// Cached fragment values for the display watcher. Compared each tick
	// against the FCkGym_Station_TitleAndDescription fragment on this entity;
	// when a field changes, the corresponding text component is re-applied.
	private FText _CachedTitle;
	private FText _CachedDescription;
	private FText _CachedInstructions;

	// Body component handles - used by OnBodyMeshAdded/OnTrimMeshAdded to
	// disambiguate which piece fired the callback (since one handler serves
	// multiple pieces of the same type).
	private FCk_Handle_UnrealComponent _BackWallHandle;
	private FCk_Handle_UnrealComponent _FloorStageHandle;
	private FCk_Handle_UnrealComponent _LeftTrimHandle;
	private FCk_Handle_UnrealComponent _RightTrimHandle;
	private FCk_Handle_UnrealComponent _TopTrimHandle;
	private FCk_Handle_UnrealComponent _BottomTrimHandle;

	// Body SceneNode handles - stored directly so resize updates the right
	// SCN every time. (Get_OwningEntity -> As_SceneNode chain proved unreliable
	// during runtime resizes - partial-update bugs that left the floor at
	// the new size while the back wall stayed at the old.)
	private FCk_Handle_SceneNode _BackWallSCN;
	private FCk_Handle_SceneNode _FloorStageSCN;
	private FCk_Handle_SceneNode _LeftTrimSCN;
	private FCk_Handle_SceneNode _RightTrimSCN;
	private FCk_Handle_SceneNode _TopTrimSCN;
	private FCk_Handle_SceneNode _BottomTrimSCN;

	// Text + spotlight component handles + their SceneNodes.
	private FCk_Handle_UnrealComponent _TitleHandle;
	private FCk_Handle_UnrealComponent _DescriptionHandle;
	private FCk_Handle_UnrealComponent _FloorDescriptionHandle;
	private FCk_Handle_UnrealComponent _SpotlightHandle;
	private FCk_Handle_SceneNode _TitleSCN;
	private FCk_Handle_SceneNode _DescriptionSCN;
	private FCk_Handle_SceneNode _FloorDescriptionSCN;
	private FCk_Handle_SceneNode _SpotlightSCN;

	// Coloured-body runs. One UTextRenderComponent per run of consecutive
	// same-coloured lines, because a UTextRenderComponent holds one colour for
	// its whole text. Parallel arrays rather than a struct array: only whole
	// elements are ever assigned, which is the shape AngelScript handles
	// predictably for value types stored in a TArray.
	private TArray<FCk_Handle_UnrealComponent> _ColouredRunHandles;
	private TArray<FCk_Handle_SceneNode>       _ColouredRunSCNs;
	private TArray<FString>                    _ColouredRunAppliedText;
	private TArray<FColor>                     _ColouredRunAppliedColour;

	// First body-line index each run starts at - the run's vertical offset below
	// the description anchor is a multiple of it.
	private TArray<int32> _ColouredRunStartLine;

	// The plain description component renders the spawn-payload text at the
	// same anchor the coloured runs stack from, so a coloured station must
	// blank it or the two overlap. False until the async component exists and
	// the clear actually landed.
	private bool _DescriptionClearedForColour = false;

	// PMG handles for debug overlays (when ShowDebugOverlays=true).
	private TArray<FCk_Handle_Pmg_DebugShape> _DebugOverlays;

	// Anchor SceneNodes - always created; queried by Get_*WorldLocation getters.
	private FCk_Handle_SceneNode _AnchorFootprintCenter;
	private FCk_Handle_SceneNode _AnchorAgentSpawnFront;
	private FCk_Handle_SceneNode _AnchorAgentSpawnBack;
	private FCk_Handle_SceneNode _AnchorAgentSpawnLeft;
	private FCk_Handle_SceneNode _AnchorAgentSpawnRight;
	private FCk_Handle_SceneNode _AnchorPanelTopFront;
	private FCk_Handle_SceneNode _AnchorPanelCenter;

	// First-return evidence is retained even if a handle later becomes pending kill.
	private FCk_Handle _CreatorStation;
	private TArray<FCk_Handle> _CreatorHandles;
	private TArray<FCk_Handle> _CreatorParents;
	private TArray<FCk_Handle> _CreatorExtraReturns;
	private TArray<FCk_Handle> _CreatorExtraParents;
	private FCk_Handle_Timer _DisplayTickHandle;
	private bool _CreatorStarted = false;
	private bool _CreatorFinished = false;
	private bool _CreatorBroken = false;
	private bool _CreatorUnsupportedTopologyAttempted = false;
	private bool _BaselinePresentationClosed = false;
	private void Reject_BaselineCreation()
	{ _CreatorBroken = true; _BaselinePresentationClosed = true; }

	// A terminal world admission closes all station presentation, including
	// pending Shared construction under a different direct parent. The raw
	// failed-owner check also witnesses the exact retained admission hierarchy.
	private bool Get_IsClosedAdmissionWorld() const
	{ return CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()); }

	private bool Get_IsClosedAdmissionStation(FCk_Handle InStation) const
	{
		if (InStation == FCk_Handle() || ck::Is_NOT_Valid(InStation) ||
			!utils_entity_lifetime::Has_LifetimeOwner(InStation)) { return false; }
		auto Parent = utils_entity_lifetime::Get_LifetimeOwner(InStation);
		return CkGym_Admission::Get_IsClosedOwner(ck::TransientEntity(), Parent);
	}

	private bool Get_IsPresentationBlocked()
	{
		if (_CreatorBroken || _BaselinePresentationClosed) { return true; }
		if (!Get_IsClosedAdmissionWorld() &&
			!Get_IsClosedAdmissionStation(_CreatorStation)) { return false; }
		Reject_BaselineCreation();
		return true;
	}

	private bool Can_CreateBaselineChild(FCk_Handle InOwner) const
	{
		if (Get_IsClosedAdmissionWorld() ||
			Get_IsClosedAdmissionStation(_CreatorStation)) { return false; }
		if (ck::Is_NOT_Valid(InOwner)) { return false; }
		if (utils_entity_lifetime::Get_IsPendingDestroy(InOwner,
			ECk_EntityLifetime_DestructionPhase::BeginDestroy)) { return false; }
		return utils_entity_lifetime::Get_CanCreateEntity(InOwner);
	}

	private bool Get_IsLiveBaselineHandle(FCk_Handle InHandle) const
	{
		if (ck::Is_NOT_Valid(InHandle)) { return false; }
		return !utils_entity_lifetime::Get_IsPendingDestroy(InHandle,
			ECk_EntityLifetime_DestructionPhase::BeginDestroy);
	}

	private bool Get_IsCurrentCreatorStation() const
	{
		if (!_CreatorStarted || !Get_IsLiveBaselineHandle(_CreatorStation) ||
			Get_IsClosedAdmissionWorld() ||
			Get_IsClosedAdmissionStation(_CreatorStation)) { return false; }
		return UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(this) == _CreatorStation;
	}

	private bool Get_IsExpectedBaselineComponent(FCk_Handle_UnrealComponent InHandle,
		int32 InFirstRole, int32 InLastRole) const
	{
		if (!Get_IsCurrentCreatorStation() ||
			_CreatorHandles.Num() != 28 || _CreatorParents.Num() != 28 ||
			InFirstRole < 17 || InLastRole > 26 || InFirstRole > InLastRole) { return false; }
		auto H = FCk_Handle(InHandle);
		if (!Get_IsLiveBaselineHandle(H) || !utils_unreal_component::Has(H)) { return false; }
		int32 Role = -1;
		for (int32 i = InFirstRole; i <= InLastRole; ++i)
		{ if (_CreatorHandles[i] == H) { Role = i; break; } }
		if (Role < 0) { return false; }
		auto Parent = _CreatorParents[Role];
		if (Parent != _CreatorHandles[Role - 17] || !Get_IsLiveBaselineHandle(Parent) ||
			!utils_scene_node::Has(Parent) || !utils_transform::Has(Parent)) { return false; }
		if (!utils_entity_lifetime::Has_LifetimeOwner(H) ||
			utils_entity_lifetime::Get_LifetimeOwner(H) != Parent ||
			!utils_entity_lifetime::Has_LifetimeOwner(Parent) ||
			utils_entity_lifetime::Get_LifetimeOwner(Parent) != _CreatorStation) { return false; }
		return utils_unreal_component::Get_OwningEntity(InHandle) == Parent;
	}

	private bool Get_IsExpectedOptionalTextComponent(FCk_Handle_UnrealComponent InHandle) const
	{
		if (!Get_IsCurrentCreatorStation() ||
			_CreatorExtraReturns.Num() != _CreatorExtraParents.Num()) { return false; }
		auto H = FCk_Handle(InHandle);
		if (!Get_IsLiveBaselineHandle(H) || !utils_unreal_component::Has(H)) { return false; }
		for (int32 i = 0; i < _CreatorExtraReturns.Num(); ++i)
		{
			if (_CreatorExtraReturns[i] != H) { continue; }
			auto Parent = _CreatorExtraParents[i];
			if (!Get_IsLiveBaselineHandle(Parent) || !utils_scene_node::Has(Parent) ||
				!utils_transform::Has(Parent) ||
				!utils_entity_lifetime::Has_LifetimeOwner(H) ||
				utils_entity_lifetime::Get_LifetimeOwner(H) != Parent ||
				!utils_entity_lifetime::Has_LifetimeOwner(Parent) ||
				utils_entity_lifetime::Get_LifetimeOwner(Parent) != _CreatorStation ||
				utils_unreal_component::Get_OwningEntity(InHandle) != Parent) { return false; }
			return true;
		}
		return false;
	}

	private bool Get_IsFiniteBaselineRecipe() const
	{
		if (!InitialTransform.IsValid() || InitialTransform.ContainsNaN() ||
			!Math::IsFinite(Width) || Width <= 0.0 ||
			!Math::IsFinite(Depth) || Depth <= 0.0 ||
			!Math::IsFinite(Height) || Height <= 0.0 ||
			!Math::IsFinite(WallThickness) || WallThickness <= 0.0 ||
			!Math::IsFinite(FloorThickness) || FloorThickness <= 0.0 ||
			!Math::IsFinite(TrimDepth) || TrimDepth < 0.0 ||
			!Math::IsFinite(AgentSpawnOffset) || AgentSpawnOffset < 0.0 ||
			!Math::IsFinite(TitleScale) || TitleScale <= 0.0 ||
			!Math::IsFinite(DescriptionScale) || DescriptionScale <= 0.0 ||
			!Math::IsFinite(FloorDescriptionScale) || FloorDescriptionScale <= 0.0 ||
			!Math::IsFinite(Width * 100.0) || !Math::IsFinite(Depth * 100.0) ||
			!Math::IsFinite(Height * 100.0) ||
			!Math::IsFinite(Depth * 50.0 + WallThickness + TrimDepth) ||
			!Math::IsFinite(Width * 50.0 + AgentSpawnOffset) ||
			!Math::IsFinite(Depth * 50.0 + AgentSpawnOffset)) { return false; }
		auto Position = InitialTransform.GetTranslation();
		auto Scale = InitialTransform.GetScale3D();
		auto Rotation = InitialTransform.GetRotation();
		return Math::IsFinite(Position.X) && Math::IsFinite(Position.Y) && Math::IsFinite(Position.Z) &&
			Math::IsFinite(Scale.X) && Math::IsFinite(Scale.Y) && Math::IsFinite(Scale.Z) &&
			Scale.X > 0.0 && Scale.Y > 0.0 && Scale.Z > 0.0 &&
			Math::IsFinite(Rotation.X) && Math::IsFinite(Rotation.Y) &&
			Math::IsFinite(Rotation.Z) && Math::IsFinite(Rotation.W) &&
			Rotation.IsNormalized() && Math::Abs(Rotation.SizeSquared() - 1.0) <= 1.0e-12 &&
			Math::IsFinite(BodyColour.R) && Math::IsFinite(BodyColour.G) &&
			Math::IsFinite(BodyColour.B) && Math::IsFinite(BodyColour.A) &&
			Math::IsFinite(TrimColour.R) && Math::IsFinite(TrimColour.G) &&
			Math::IsFinite(TrimColour.B) && Math::IsFinite(TrimColour.A);
	}

	private void Record_BaselineReturn(int32 InRole, FCk_Handle InHandle, FCk_Handle InParent)
	{
		if (InHandle == FCk_Handle()) { Reject_BaselineCreation(); return; }
		if (InParent == FCk_Handle()) { Reject_BaselineCreation(); }
		if (InRole < 0)
		{
			_CreatorUnsupportedTopologyAttempted = true;
			_CreatorExtraReturns.Add(InHandle);
			_CreatorExtraParents.Add(InParent);
			return;
		}
		if (InRole >= 28)
		{
			Reject_BaselineCreation();
			_CreatorExtraReturns.Add(InHandle);
			_CreatorExtraParents.Add(InParent);
			return;
		}
		if (_CreatorHandles.Num() != 28 || _CreatorParents.Num() != 28)
		{ Reject_BaselineCreation(); return; }
		for (int32 i = 0; i < _CreatorHandles.Num(); ++i)
		{ if (i != InRole && _CreatorHandles[i] == InHandle) { Reject_BaselineCreation(); } }
		if (_CreatorHandles[InRole] == FCk_Handle())
		{
			_CreatorHandles[InRole] = InHandle;
			_CreatorParents[InRole] = InParent;
		}
		else if (_CreatorHandles[InRole] != InHandle || _CreatorParents[InRole] != InParent)
		{
			Reject_BaselineCreation();
			_CreatorExtraReturns.Add(InHandle);
			_CreatorExtraParents.Add(InParent);
		}
	}

	// No entity/child dereference: the caller establishes this script's identity
	// while live and freezes these values before any destroy request.
	UFUNCTION(BlueprintPure)
	bool TryGet_BaselineCreatorLedger(FCkGymStation_BaselineCreatorLedger&out Out) const
	{
		Out = FCkGymStation_BaselineCreatorLedger();
		if (!_CreatorStarted || _CreatorStation == FCk_Handle() ||
			_CreatorHandles.Num() != 28 || _CreatorParents.Num() != 28 ||
			_CreatorExtraReturns.Num() != _CreatorExtraParents.Num()) { return false; }
		FCkGymStation_BaselineCreatorLedger Candidate;
		Candidate.Station = _CreatorStation;
		Candidate.Handles = _CreatorHandles;
		Candidate.Parents = _CreatorParents;
		Candidate.ExtraReturns = _CreatorExtraReturns;
		Candidate.ExtraParents = _CreatorExtraParents;
		Candidate.Started = _CreatorStarted;
		Candidate.Finished = _CreatorFinished;
		Candidate.Broken = _CreatorBroken;
		Candidate.UnsupportedTopologyAttempted = _CreatorUnsupportedTopologyAttempted;
		Candidate.PresentationClosed = _BaselinePresentationClosed ||
			Get_IsClosedAdmissionWorld() ||
			Get_IsClosedAdmissionStation(_CreatorStation);
		Out = Candidate;
		return true;
	}

	// Caller has already proved this exact script belongs to its station.
	// Close only presentation callbacks; do not request, destroy, or clear evidence.
	UFUNCTION(BlueprintCallable)
	bool Request_CloseBaselinePresentation()
	{
		if (!_CreatorStarted || _CreatorStation == FCk_Handle()) { return false; }
		_BaselinePresentationClosed = true;
		return true;
	}

	//------------------------------------------------------------------------
	// Lifecycle
	//------------------------------------------------------------------------

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow
	DoConstruct(FCk_Handle& InHandle)
	{
		if (_CreatorStarted)
		{ Reject_BaselineCreation(); return ECk_EntityScript_ConstructionFlow::Continue; }
		_CreatorStarted = true;
		_CreatorStation = InHandle;
		_CreatorHandles.SetNum(28);
		_CreatorParents.SetNum(28);
		if (Get_IsPresentationBlocked())
		{ return ECk_EntityScript_ConstructionFlow::Continue; }
		_CreatorUnsupportedTopologyAttempted =
			_CreatorUnsupportedTopologyAttempted || ShowAnchors || ShowDebugOverlays;
		if (!Can_CreateBaselineChild(InHandle))
		{ Reject_BaselineCreation(); return ECk_EntityScript_ConstructionFlow::Continue; }
		if (!Get_IsFiniteBaselineRecipe())
		{ Reject_BaselineCreation(); return ECk_EntityScript_ConstructionFlow::Continue; }
		// Compute auto-size from spawn-params text BEFORE building geometry,
		// so Width/Height reflect the content from frame zero.
		if (AutoSize)
		{
			Apply_AutoSize_FromSpawnParams();
			if (!Get_IsFiniteBaselineRecipe())
			{ Reject_BaselineCreation(); return ECk_EntityScript_ConstructionFlow::Continue; }
		}

		auto StationTH = utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		if (ck::Is_NOT_Valid(StationTH))
		{ Reject_BaselineCreation(); return ECk_EntityScript_ConstructionFlow::Continue; }
		if (Get_IsPresentationBlocked() || !utils_transform::Has(FCk_Handle(StationTH)))
		{ Reject_BaselineCreation(); return ECk_EntityScript_ConstructionFlow::Continue; }

		Build_Body(StationTH);
		if (Get_IsPresentationBlocked())
		{ return ECk_EntityScript_ConstructionFlow::Continue; }
		Build_Text(StationTH);
		if (Get_IsPresentationBlocked())
		{ return ECk_EntityScript_ConstructionFlow::Continue; }
		Build_Spotlight(StationTH);
		if (Get_IsPresentationBlocked())
		{ return ECk_EntityScript_ConstructionFlow::Continue; }
		Build_Anchors(StationTH);
		if (Get_IsPresentationBlocked())
		{ return ECk_EntityScript_ConstructionFlow::Continue; }
		Populate_AnchorsFragment(InHandle);
		if (Get_IsPresentationBlocked())
		{ return ECk_EntityScript_ConstructionFlow::Continue; }

		if (ShowAnchors)
		{
			Build_AnchorVisuals(InHandle);
			if (Get_IsPresentationBlocked())
			{ return ECk_EntityScript_ConstructionFlow::Continue; }
		}

		if (ShowDebugOverlays)
		{
			Build_DebugOverlays(InHandle);
			if (Get_IsPresentationBlocked())
			{ return ECk_EntityScript_ConstructionFlow::Continue; }
		}

		// Tag the station entity so the base PC's Get_StationHandle /
		// Get_StationTransform can find it later by tag.
		for (auto Tag : StationTags)
		{
			utils_entity_tag::Add(InHandle, Tag);
			if (Get_IsPresentationBlocked())
			{ return ECk_EntityScript_ConstructionFlow::Continue; }
		}

		// Watch the FCkGym_Station_TitleAndDescription fragment so directors
		// calling CkGym_Common::Update_StationDisplay get reflected in the
		// text components.
		// Exact Create_Tick spec and sequence, with the returned child saved
		// before the OnUpdate binding can call back.
		if (!Can_CreateBaselineChild(InHandle))
		{ Reject_BaselineCreation(); return ECk_EntityScript_ConstructionFlow::Continue; }
		auto TimerOwner = InHandle;
		auto TimerParams = FCk_Timer_Spec();
		TimerParams.Set_StartingState(ECk_Timer_State::Running);
		TimerParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		_DisplayTickHandle = utils_timer::Add(TimerOwner, TimerParams);
		Record_BaselineReturn(27, FCk_Handle(_DisplayTickHandle), InHandle);
		if (Get_IsPresentationBlocked() ||
			ck::Is_NOT_Valid(_DisplayTickHandle) || !utils_timer::DoCast(FCk_Handle(_DisplayTickHandle)).IsSet())
		{ Reject_BaselineCreation(); return ECk_EntityScript_ConstructionFlow::Continue; }
		auto Bound = utils_timer::BindTo_OnUpdate(_DisplayTickHandle,
			FCk_Delegate_Timer(this, n"OnDisplayTick"));
		if (Bound != _DisplayTickHandle || Get_IsPresentationBlocked() ||
			ck::Is_NOT_Valid(Bound) || !utils_timer::DoCast(FCk_Handle(Bound)).IsSet())
		{ Reject_BaselineCreation(); return ECk_EntityScript_ConstructionFlow::Continue; }
		_CreatorFinished = true;

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	// Snapshot the seven anchor world positions into a fragment on the station
	// entity so other gym scripts can read them via Handle.Get_Fragment.
	// Computed from InitialTransform x local offset (matches the Build_Anchors
	// math). Static stations only - moving stations would need this re-run.
	private void Populate_AnchorsFragment(FCk_Handle& InHandle)
	{
		if (Get_IsPresentationBlocked()) { return; }
		const auto W_cm = Width * 100.0;
		const auto D_cm = Depth * 100.0;
		const auto H_cm = Height * 100.0;

		auto& Fragment = InHandle.AddOrGet_Fragment(FCkGym_Station_Anchors);
		Fragment.FootprintCenter = InitialTransform.TransformPosition(FVector(0.0, 0.0, 0.0));
		Fragment.AgentSpawnFront = InitialTransform.TransformPosition(FVector( D_cm * 0.5 + AgentSpawnOffset, 0.0, 0.0));
		Fragment.AgentSpawnBack  = InitialTransform.TransformPosition(FVector(-D_cm * 0.5 - AgentSpawnOffset, 0.0, 0.0));
		Fragment.AgentSpawnLeft  = InitialTransform.TransformPosition(FVector(0.0,  W_cm * 0.5 + AgentSpawnOffset, 0.0));
		Fragment.AgentSpawnRight = InitialTransform.TransformPosition(FVector(0.0, -W_cm * 0.5 - AgentSpawnOffset, 0.0));
		Fragment.PanelTopFront   = InitialTransform.TransformPosition(FVector(D_cm * 0.5, 0.0, H_cm));
		Fragment.PanelCenter     = InitialTransform.TransformPosition(FVector(0.0, 0.0, H_cm * 0.5));
	}

	//------------------------------------------------------------------------
	// Fragment-driven display updates - directors push live status text via
	// CkGym_Common::Update_StationDisplay, which writes the fragment on this
	// entity. We poll the fragment each tick and re-apply text components
	// when any field changes.
	//------------------------------------------------------------------------
	UFUNCTION()
	private void OnDisplayTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (Get_IsPresentationBlocked()) { return; }
		auto Tick = FCk_Handle(InTimer);
		if (!Get_IsCurrentCreatorStation() || InTimer != _DisplayTickHandle ||
			!Get_IsLiveBaselineHandle(Tick) || !utils_timer::DoCast(FCk_Handle(InTimer)).IsSet() ||
			!utils_entity_lifetime::Has_LifetimeOwner(Tick) ||
			utils_entity_lifetime::Get_LifetimeOwner(Tick) != _CreatorStation)
		{ Reject_BaselineCreation(); return; }
		auto SelfEntity = _CreatorStation;

		// Colour wins when a station opted into it: the two bodies would
		// otherwise render on top of each other.
		auto& ColouredBody = SelfEntity.AddOrGet_Fragment(FCkGym_Station_ColoredBody);
		if (ColouredBody.Active)
		{
			Apply_ColouredBody(ColouredBody);
			if (Get_IsPresentationBlocked()) { return; }

			if (AutoSize)
			{
				Refit_FromMeasuredBounds();
			}

			return;
		}

		auto& Fragment = SelfEntity.AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);

		if (!Fragment.Title.EqualTo(_CachedTitle))
		{
			_CachedTitle = Fragment.Title;
			Apply_TitleText(Fragment.Title);
			if (Get_IsPresentationBlocked()) { return; }
		}
		if (!Fragment.Description.EqualTo(_CachedDescription))
		{
			_CachedDescription = Fragment.Description;
			Apply_DescriptionText(Fragment.Description);
			if (Get_IsPresentationBlocked()) { return; }
		}
		if (!Fragment.Instructions.EqualTo(_CachedInstructions))
		{
			_CachedInstructions = Fragment.Instructions;
			Apply_FloorDescriptionText(Fragment.Instructions);
			if (Get_IsPresentationBlocked()) { return; }
		}

		// Grow-only resize from actual rendered text bounds (read after
		// Apply_*Text() has called SetText). Avoids per-tick pulsing because
		// the resize only fires when measured bounds exceed current alcove.
		if (AutoSize)
		{
			Refit_FromMeasuredBounds();
		}
	}

	private void Apply_TitleText(FText InText)
	{
		if (Get_IsPresentationBlocked()) { return; }
		if (!Get_IsExpectedBaselineComponent(_TitleHandle, 23, 23))
		{ Reject_BaselineCreation(); return; }
		auto Comp = utils_unreal_component::Get_Component(_TitleHandle);
		auto Text = Cast<UTextRenderComponent>(Comp);
		if (Text == nullptr) { Reject_BaselineCreation(); return; }
		Text.SetText(InText);
	}

	private void Apply_DescriptionText(FText InText)
	{
		if (Get_IsPresentationBlocked()) { return; }
		if (!Get_IsExpectedBaselineComponent(_DescriptionHandle, 24, 24))
		{ Reject_BaselineCreation(); return; }
		auto Comp = utils_unreal_component::Get_Component(_DescriptionHandle);
		auto Text = Cast<UTextRenderComponent>(Comp);
		if (Text == nullptr) { Reject_BaselineCreation(); return; }
		Text.SetText(InText);
	}

	private void Apply_FloorDescriptionText(FText InText)
	{
		if (Get_IsPresentationBlocked()) { return; }
		if (!Get_IsExpectedBaselineComponent(_FloorDescriptionHandle, 25, 25))
		{ Reject_BaselineCreation(); return; }
		auto Comp = utils_unreal_component::Get_Component(_FloorDescriptionHandle);
		auto Text = Cast<UTextRenderComponent>(Comp);
		if (Text == nullptr) { Reject_BaselineCreation(); return; }

		// Toggle visibility based on whether the fragment carries text.
		const auto HasText = !InText.IsEmpty();
		Text.SetVisibility(HasText, false);
		if (HasText) { Text.SetText(InText); }
	}

	//------------------------------------------------------------------------
	// Coloured body - same title and floor-instruction channels as the plain
	// path; only the body is different, and it is rendered as one component per
	// run of consecutive same-coloured lines.
	//------------------------------------------------------------------------

	private void Apply_ColouredBody(FCkGym_Station_ColoredBody& InBody)
	{
		if (Get_IsPresentationBlocked()) { return; }
		_CreatorUnsupportedTopologyAttempted = true;
		if (!_DescriptionClearedForColour)
		{
			if (!Get_IsExpectedBaselineComponent(_DescriptionHandle, 24, 24))
			{ Reject_BaselineCreation(); return; }
			auto DescText = Cast<UTextRenderComponent>(utils_unreal_component::Get_Component(_DescriptionHandle));
			if (DescText == nullptr) { Reject_BaselineCreation(); return; }
			DescText.SetText(FText::FromString(""));
			_CachedDescription = FText::FromString("");
			_DescriptionClearedForColour = true;
		}

		if (!InBody.Title.EqualTo(_CachedTitle))
		{
			_CachedTitle = InBody.Title;
			Apply_TitleText(InBody.Title);
			if (Get_IsPresentationBlocked()) { return; }
		}
		if (!InBody.Instructions.EqualTo(_CachedInstructions))
		{
			_CachedInstructions = InBody.Instructions;
			Apply_FloorDescriptionText(InBody.Instructions);
			if (Get_IsPresentationBlocked()) { return; }
		}

		Apply_ColouredRuns(InBody.Lines);
	}

	private void Apply_ColouredRuns(TArray<FCkGym_ColoredLine>& InLines)
	{
		if (Get_IsPresentationBlocked()) { return; }
		auto RunTexts = TArray<FString>();
		auto RunColours = TArray<FColor>();
		auto RunStartLines = TArray<int32>();

		for (auto Index = 0; Index < InLines.Num(); Index++)
		{
			const auto LastRun = RunTexts.Num() - 1;

			if (LastRun >= 0 && Colours_Match(RunColours[LastRun], InLines[Index].Colour))
			{
				RunTexts[LastRun] = f"{RunTexts[LastRun]}\n{InLines[Index].Text}";
				continue;
			}

			RunTexts.Add(InLines[Index].Text);
			RunColours.Add(InLines[Index].Colour);
			RunStartLines.Add(Index);
		}

		// Components are only torn down when the RUN COUNT moves. Text and
		// colour changes reuse what is already there - the display ticks every
		// frame, so recreating on content would churn components continuously.
		const auto RunCountChanged = RunTexts.Num() != _ColouredRunHandles.Num();
		const auto LayoutChanged = RunCountChanged || Get_ColouredRunLayoutChanged(RunStartLines);

		_ColouredRunStartLine = RunStartLines;

		if (RunCountChanged)
		{
			Rebuild_ColouredRunComponents(RunTexts.Num());
			if (Get_IsPresentationBlocked()) { return; }
		}
		else if (LayoutChanged)
		{
			Update_ColouredRunTransforms();
		}

		for (auto Index = 0; Index < RunTexts.Num(); Index++)
		{
			Apply_ColouredRun(Index, RunTexts[Index], RunColours[Index]);
			if (Get_IsPresentationBlocked()) { return; }
		}
	}

	private bool Get_ColouredRunLayoutChanged(TArray<int32>& InStartLines)
	{
		if (InStartLines.Num() != _ColouredRunStartLine.Num())
		{
			return true;
		}

		for (auto Index = 0; Index < InStartLines.Num(); Index++)
		{
			if (InStartLines[Index] != _ColouredRunStartLine[Index])
			{
				return true;
			}
		}

		return false;
	}

	// A run whose component has not been instantiated yet leaves its applied
	// cache untouched, so the next display tick retries rather than dropping
	// the text on the floor.
	private void Apply_ColouredRun(int32 InIndex, FString InText, FColor InColour)
	{
		if (Get_IsPresentationBlocked()) { return; }
		if (InIndex < 0 || InIndex >= _ColouredRunHandles.Num() ||
			InIndex >= _ColouredRunAppliedText.Num() || InIndex >= _ColouredRunAppliedColour.Num())
		{
			Reject_BaselineCreation();
			return;
		}

		if (_ColouredRunAppliedText[InIndex] == InText && Colours_Match(_ColouredRunAppliedColour[InIndex], InColour))
		{
			return;
		}

		if (!Get_IsExpectedOptionalTextComponent(_ColouredRunHandles[InIndex]))
		{ Reject_BaselineCreation(); return; }
		auto Text = Cast<UTextRenderComponent>(utils_unreal_component::Get_Component(_ColouredRunHandles[InIndex]));
		if (Text == nullptr) { Reject_BaselineCreation(); return; }

		Text.SetText(FText::FromString(InText));
		Text.SetWorldSize(float(DescriptionScale));
		Text.SetTextRenderColor(InColour);
		Text.SetHorizontalAlignment(TextAlignment);
		Text.SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextTop);

		_ColouredRunAppliedText[InIndex] = InText;
		_ColouredRunAppliedColour[InIndex] = InColour;
	}

	private void Rebuild_ColouredRunComponents(int32 InCount)
	{
		_CreatorUnsupportedTopologyAttempted = true;
		if (Get_IsPresentationBlocked() || !Get_IsCurrentCreatorStation())
		{ Reject_BaselineCreation(); return; }
		Destroy_ColouredRunComponents();

		auto SelfEntity = _CreatorStation;
		auto StationTH = SelfEntity.As_Transform();

		for (auto Index = 0; Index < InCount; Index++)
		{
			if (Get_IsPresentationBlocked()) { return; }
			auto RunSCN = FCk_Handle_SceneNode();
			auto RunComponent = Spawn_TextPiece(StationTH, -1, n"ColouredRunText", n"OnColouredRunAdded",
				Get_ColouredRunLocalLocation(Index),
				FRotator::ZeroRotator,
				RunSCN);

			_ColouredRunHandles.Add(RunComponent);
			_ColouredRunSCNs.Add(RunSCN);
			_ColouredRunAppliedText.Add("");
			_ColouredRunAppliedColour.Add(FColor(0, 0, 0, 0));
		}
	}

	private void Destroy_ColouredRunComponents()
	{
		if (Get_IsPresentationBlocked()) { return; }
		// Destroying the run's SceneNode entity takes its UTextRenderComponent
		// with it - same cascade the station itself relies on.
		for (auto RunSCN : _ColouredRunSCNs)
		{
			utils_entity_lifetime::Request_DestroyEntity(RunSCN);
		}

		_ColouredRunHandles.Empty();
		_ColouredRunSCNs.Empty();
		_ColouredRunAppliedText.Empty();
		_ColouredRunAppliedColour.Empty();
	}

	UFUNCTION()
	private void OnColouredRunAdded(FCk_Handle_UnrealComponent InHandle)
	{
		if (Get_IsPresentationBlocked()) { return; }
		if (!Get_IsExpectedOptionalTextComponent(InHandle))
		{ Reject_BaselineCreation(); return; }
		auto Text = Cast<UTextRenderComponent>(utils_unreal_component::Get_Component(InHandle));
		if (Text == nullptr) { Reject_BaselineCreation(); return; }

		Text.SetWorldSize(float(DescriptionScale));
		Text.SetHorizontalAlignment(TextAlignment);
		Text.SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextTop);
	}

	// Runs stack downward from the same anchor the plain description uses, one
	// line-height per body line above them.
	private FVector Get_ColouredRunLocalLocation(int32 InIndex)
	{
		const auto BackWallInnerX = -Depth * 50.0 + WallThickness;
		const auto TextX = BackWallInnerX + 5.0;
		const auto PanelTopZ = Height * 100.0;
		const auto DescZ = PanelTopZ - (TitleScale + 60.0);
		const auto TextY = TextAlignmentOffset(TextAlignment, 1.0);

		auto StartLine = 0;
		if (InIndex < _ColouredRunStartLine.Num())
		{
			StartLine = _ColouredRunStartLine[InIndex];
		}

		return FVector(TextX, TextY, DescZ - double(StartLine) * Get_ColouredRunLineHeight_cm());
	}

	// Mirrors the 1.5 line-height factor Compute_AutoHeight sizes the alcove
	// with, so a stacked run lands where the auto-size math expects its lines.
	private double Get_ColouredRunLineHeight_cm()
	{
		return DescriptionScale * 1.5;
	}

	private void Update_ColouredRunTransforms()
	{
		if (Get_IsPresentationBlocked()) { return; }
		for (auto Index = 0; Index < _ColouredRunSCNs.Num(); Index++)
		{
			auto RunSCN = _ColouredRunSCNs[Index];
			Update_PieceLocAndRot(RunSCN, Get_ColouredRunLocalLocation(Index), FRotator::ZeroRotator);
		}
	}

	private bool Colours_Match(FColor InA, FColor InB)
	{
		return InA.R == InB.R && InA.G == InB.G && InA.B == InB.B && InA.A == InB.A;
	}

	// DoEndPlay intentionally omitted - SceneNode children cascade-destroy with
	// the station entity, and the CkUnrealComponent EndPlay processor tears down
	// each child's component when its entity dies.

	//========================================================================
	// Body - back wall, floor stage, four trims.
	//========================================================================

	private void Build_Body(FCk_Handle_Transform& InStationTH)
	{
		const auto W_cm = Width * 100.0;
		const auto D_cm = Depth * 100.0;
		const auto H_cm = Height * 100.0;

		const auto WT = WallThickness;
		const auto FT = FloorThickness;

		// Back wall: thin slab at -X end, full Width x full Height.
		_BackWallHandle = Spawn_StaticMeshPiece(InStationTH, 0, n"BackWall", n"OnBodyMeshAdded",
			FVector(-D_cm * 0.5 + WT * 0.5, 0.0, H_cm * 0.5),
			FVector(WT / 100.0, Width, Height),
			_BackWallSCN);
		if (Get_IsPresentationBlocked()) { return; }

		// Floor stage at Z=0.
		_FloorStageHandle = Spawn_StaticMeshPiece(InStationTH, 1, n"FloorStage", n"OnBodyMeshAdded",
			FVector(0.0, 0.0, FT * 0.5),
			FVector(Depth, Width, FT / 100.0),
			_FloorStageSCN);
		if (Get_IsPresentationBlocked()) { return; }

		// Trim - picture-frame around the back wall, flush with the back wall's
		// front face, projecting forward by TrimDepth.
		const auto TrimCenterX = -D_cm * 0.5 + WT + TrimDepth * 0.5;

		_LeftTrimHandle = Spawn_StaticMeshPiece(InStationTH, 2, n"LeftTrim", n"OnTrimMeshAdded",
			FVector(TrimCenterX, W_cm * 0.5 - WT * 0.5, H_cm * 0.5),
			FVector(TrimDepth / 100.0, WT / 100.0, Height),
			_LeftTrimSCN);
		if (Get_IsPresentationBlocked()) { return; }

		_RightTrimHandle = Spawn_StaticMeshPiece(InStationTH, 3, n"RightTrim", n"OnTrimMeshAdded",
			FVector(TrimCenterX, -W_cm * 0.5 + WT * 0.5, H_cm * 0.5),
			FVector(TrimDepth / 100.0, WT / 100.0, Height),
			_RightTrimSCN);
		if (Get_IsPresentationBlocked()) { return; }

		_TopTrimHandle = Spawn_StaticMeshPiece(InStationTH, 4, n"TopTrim", n"OnTrimMeshAdded",
			FVector(TrimCenterX, 0.0, H_cm - WT * 0.5),
			FVector(TrimDepth / 100.0, Width, WT / 100.0),
			_TopTrimSCN);
		if (Get_IsPresentationBlocked()) { return; }

		_BottomTrimHandle = Spawn_StaticMeshPiece(InStationTH, 5, n"BottomTrim", n"OnTrimMeshAdded",
			FVector(TrimCenterX, 0.0, FT + WT * 0.5),
			FVector(TrimDepth / 100.0, Width, WT / 100.0),
			_BottomTrimSCN);
	}

	private FCk_Handle_UnrealComponent Spawn_StaticMeshPiece(
		FCk_Handle_Transform& InStationTH,
		int32 InRole,
		FName InDebugName,
		FName InOnAddedHandler,
		FVector InLocalLocation,
		FVector InLocalScale,
		FCk_Handle_SceneNode& OutSCN)
	{
		if (Get_IsPresentationBlocked() || !Can_CreateBaselineChild(FCk_Handle(InStationTH)))
		{ Reject_BaselineCreation(); return FCk_Handle_UnrealComponent(); }
		auto LocalTransform = FTransform(FRotator::ZeroRotator, InLocalLocation, InLocalScale);
		OutSCN = utils_scene_node::Create(InStationTH, LocalTransform);
		Record_BaselineReturn(InRole, FCk_Handle(OutSCN), FCk_Handle(InStationTH));
		if (Get_IsPresentationBlocked() || ck::Is_NOT_Valid(OutSCN) || !utils_scene_node::Has(FCk_Handle(OutSCN)) ||
			!utils_transform::Has(FCk_Handle(OutSCN)))
		{ Reject_BaselineCreation(); return FCk_Handle_UnrealComponent(); }
		if (!Can_CreateBaselineChild(FCk_Handle(OutSCN)))
		{ Reject_BaselineCreation(); return FCk_Handle_UnrealComponent(); }

		auto PieceTH = OutSCN.As_Transform();
		auto PieceEntity = FCk_Handle(PieceTH);

		const auto Params = utils_unreal_component::Make_Params(UStaticMeshComponent, ECk_UnrealComponent_TickPolicy::DoNotTick, InDebugName);
		auto ComponentHandle = utils_unreal_component::Add(PieceEntity, Params);
		Record_BaselineReturn(17 + InRole, FCk_Handle(ComponentHandle), FCk_Handle(OutSCN));
		if (Get_IsPresentationBlocked() || ck::Is_NOT_Valid(ComponentHandle) ||
			!utils_unreal_component::Has(FCk_Handle(ComponentHandle)))
		{ Reject_BaselineCreation(); return ComponentHandle; }

		auto Bound = utils_unreal_component::BindTo_OnAdded(
			ComponentHandle,
			FCk_Delegate_UnrealComponent_OnAdded(this, InOnAddedHandler));
		if (Bound != ComponentHandle || Get_IsPresentationBlocked() ||
			ck::Is_NOT_Valid(Bound) || !utils_unreal_component::Has(FCk_Handle(Bound)))
		{ Reject_BaselineCreation(); return ComponentHandle; }

		return ComponentHandle;
	}

	UFUNCTION()
	private void OnBodyMeshAdded(FCk_Handle_UnrealComponent InHandle)
	{
		if (Get_IsPresentationBlocked()) { return; }
		if (!Get_IsExpectedBaselineComponent(InHandle, 17, 18))
		{ Reject_BaselineCreation(); return; }
		Configure_StaticMesh(InHandle, BodyColour);
	}

	UFUNCTION()
	private void OnTrimMeshAdded(FCk_Handle_UnrealComponent InHandle)
	{
		if (Get_IsPresentationBlocked()) { return; }
		if (!Get_IsExpectedBaselineComponent(InHandle, 19, 22))
		{ Reject_BaselineCreation(); return; }
		Configure_StaticMesh(InHandle, TrimColour);
	}

	private void Configure_StaticMesh(FCk_Handle_UnrealComponent InHandle, FLinearColor InColour)
	{
		if (Get_IsPresentationBlocked()) { return; }
		auto Comp = utils_unreal_component::Get_Component(InHandle);
		auto Mesh = Cast<UStaticMeshComponent>(Comp);
		if (Mesh == nullptr) { Reject_BaselineCreation(); return; }

		auto Cube = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Cube.Cube"));
		if (Cube != nullptr) { Mesh.SetStaticMesh(Cube); }

		Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);

		auto MID = Mesh.CreateDynamicMaterialInstance(0);
		if (MID != nullptr)
		{
			MID.SetVectorParameterValue(n"Color", InColour);
		}
	}

	//========================================================================
	// Text - title + description on back wall, optional floor description.
	//========================================================================

	private void Build_Text(FCk_Handle_Transform& InStationTH)
	{
		const auto BackWallInnerX = -Depth * 50.0 + WallThickness;
		const auto TextX = BackWallInnerX + 5.0;          // 5cm in front of inner face
		const auto PanelTopZ = Height * 100.0;
		const auto DescZ  = PanelTopZ - (TitleScale + 60.0);
		const auto TitleZ = PanelTopZ - (TitleScale + 60.0) / 2.0;
		const auto TextY = TextAlignmentOffset(TextAlignment, 1.0);

		_TitleHandle = Spawn_TextPiece(InStationTH, 6, n"TitleText", n"OnTitleAdded",
			FVector(TextX, TextY, TitleZ),
			FRotator::ZeroRotator,
			_TitleSCN);
		if (Get_IsPresentationBlocked()) { return; }

		_DescriptionHandle = Spawn_TextPiece(InStationTH, 7, n"DescriptionText", n"OnDescriptionAdded",
			FVector(TextX, TextY, DescZ),
			FRotator::ZeroRotator,
			_DescriptionSCN);
		if (Get_IsPresentationBlocked()) { return; }

		// Floor description is created unconditionally; the OnAdded handler hides
		// the component when FloorDescriptionText is empty.
		const auto FloorTopZ = FloorThickness + 2.0;
		const auto FloorX = FloorPlacementOffset(1.0);
		const auto FloorY = TextAlignmentOffset(FloorTextAlignment, 1.0);

		_FloorDescriptionHandle = Spawn_TextPiece(InStationTH, 8, n"FloorDescriptionText", n"OnFloorDescriptionAdded",
			FVector(FloorX, FloorY, FloorTopZ),
			FRotator(90.0, 0.0, 0.0),
			_FloorDescriptionSCN);
	}

	private FCk_Handle_UnrealComponent Spawn_TextPiece(
		FCk_Handle_Transform& InStationTH,
		int32 InRole,
		FName InDebugName,
		FName InOnAddedHandler,
		FVector InLocalLocation,
		FRotator InLocalRotation,
		FCk_Handle_SceneNode& OutSCN)
	{
		if (Get_IsPresentationBlocked() || !Can_CreateBaselineChild(FCk_Handle(InStationTH)))
		{ Reject_BaselineCreation(); return FCk_Handle_UnrealComponent(); }
		auto LocalTransform = FTransform(InLocalRotation, InLocalLocation, FVector(1.0, 1.0, 1.0));
		OutSCN = utils_scene_node::Create(InStationTH, LocalTransform);
		Record_BaselineReturn(InRole, FCk_Handle(OutSCN), FCk_Handle(InStationTH));
		if (Get_IsPresentationBlocked() || ck::Is_NOT_Valid(OutSCN) || !utils_scene_node::Has(FCk_Handle(OutSCN)) ||
			!utils_transform::Has(FCk_Handle(OutSCN)))
		{ Reject_BaselineCreation(); return FCk_Handle_UnrealComponent(); }
		if (!Can_CreateBaselineChild(FCk_Handle(OutSCN)))
		{ Reject_BaselineCreation(); return FCk_Handle_UnrealComponent(); }

		auto PieceTH = OutSCN.As_Transform();
		auto PieceEntity = FCk_Handle(PieceTH);

		const auto Params = utils_unreal_component::Make_Params(UTextRenderComponent, ECk_UnrealComponent_TickPolicy::DoNotTick, InDebugName);

		auto ComponentHandle = utils_unreal_component::Add(PieceEntity, Params);
		Record_BaselineReturn(InRole < 0 ? -1 : 17 + InRole,
			FCk_Handle(ComponentHandle), FCk_Handle(OutSCN));
		if (Get_IsPresentationBlocked() || ck::Is_NOT_Valid(ComponentHandle) ||
			!utils_unreal_component::Has(FCk_Handle(ComponentHandle)))
		{ Reject_BaselineCreation(); return ComponentHandle; }

		auto Bound = utils_unreal_component::BindTo_OnAdded(
			ComponentHandle,
			FCk_Delegate_UnrealComponent_OnAdded(this, InOnAddedHandler));
		if (Bound != ComponentHandle || Get_IsPresentationBlocked() ||
			ck::Is_NOT_Valid(Bound) || !utils_unreal_component::Has(FCk_Handle(Bound)))
		{ Reject_BaselineCreation(); return ComponentHandle; }

		return ComponentHandle;
	}

	UFUNCTION()
	private void OnTitleAdded(FCk_Handle_UnrealComponent InHandle)
	{
		if (Get_IsPresentationBlocked()) { return; }
		if (!Get_IsExpectedBaselineComponent(InHandle, 23, 23))
		{ Reject_BaselineCreation(); return; }
		auto Text = Cast<UTextRenderComponent>(utils_unreal_component::Get_Component(InHandle));
		if (Text == nullptr) { Reject_BaselineCreation(); return; }

		Text.SetText(TitleText);
		Text.SetWorldSize(float(TitleScale));
		Text.SetTextRenderColor(TitleColour);
		Text.SetHorizontalAlignment(TextAlignment);
		Text.SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextTop);

		// First chance to measure; description component may not be ready yet
		// (Refit_FromMeasuredBounds early-outs in that case).
		if (AutoSize) { Refit_FromMeasuredBounds(); }
	}

	UFUNCTION()
	private void OnDescriptionAdded(FCk_Handle_UnrealComponent InHandle)
	{
		if (Get_IsPresentationBlocked()) { return; }
		if (!Get_IsExpectedBaselineComponent(InHandle, 24, 24))
		{ Reject_BaselineCreation(); return; }
		auto Text = Cast<UTextRenderComponent>(utils_unreal_component::Get_Component(InHandle));
		if (Text == nullptr) { Reject_BaselineCreation(); return; }

		Text.SetText(Format_MultilineText(DescriptionText));
		Text.SetWorldSize(float(DescriptionScale));
		Text.SetTextRenderColor(DescriptionColour);
		Text.SetHorizontalAlignment(TextAlignment);
		Text.SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextTop);

		if (AutoSize) { Refit_FromMeasuredBounds(); }
	}

	UFUNCTION()
	private void OnFloorDescriptionAdded(FCk_Handle_UnrealComponent InHandle)
	{
		if (Get_IsPresentationBlocked()) { return; }
		if (!Get_IsExpectedBaselineComponent(InHandle, 25, 25))
		{ Reject_BaselineCreation(); return; }
		auto Text = Cast<UTextRenderComponent>(utils_unreal_component::Get_Component(InHandle));
		if (Text == nullptr) { Reject_BaselineCreation(); return; }

		const auto HasFloorText = FloorDescriptionText.Num() > 0;
		Text.SetVisibility(HasFloorText, false);
		if (HasFloorText == false) { return; }

		Text.SetText(Format_MultilineText(FloorDescriptionText));
		Text.SetWorldSize(float(FloorDescriptionScale));
		Text.SetTextRenderColor(FloorDescriptionColour);
		Text.SetHorizontalAlignment(FloorTextAlignment);
		Text.SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	}

	// Y offset for text based on the given alignment + width adjustment.
	// EHTA_Left -> +Y, EHTA_Right -> -Y, Center -> 0.
	private double TextAlignmentOffset(EHorizTextAligment InAlignment, double InWidthAdjustment)
	{
		const auto Magnitude = ((Width - InWidthAdjustment) * 0.5) * 100.0;

		if (InAlignment == EHorizTextAligment::EHTA_Center) { return 0.0; }
		if (InAlignment == EHorizTextAligment::EHTA_Left)   { return Magnitude; }
		return -Magnitude;
	}

	// X offset for the floor text based on FloorTextPlacement + depth adjustment.
	// Front -> +X (toward opening), Back -> -X (toward back wall), Center -> 0.
	private double FloorPlacementOffset(double InDepthAdjustment)
	{
		const auto Magnitude = ((Depth - InDepthAdjustment) * 0.5) * 100.0;

		if (FloorTextPlacement == ECk_GymStation_FloorTextPlacement::Center) { return 0.0; }
		if (FloorTextPlacement == ECk_GymStation_FloorTextPlacement::Front)  { return Magnitude; }
		return -Magnitude;
	}

	private FText Format_MultilineText(TArray<FText> InLines)
	{
		auto Result = "";
		const auto LineCount = InLines.Num();

		for (int i = 0; i < LineCount; ++i)
		{
			Result = f"{Result}{InLines[i].ToString()}";
			if (i < LineCount - 1)
			{
				Result = f"{Result}\n";
			}
		}

		return FText::FromString(Result);
	}

	//========================================================================
	// Spotlight - illuminates the back wall from inside the alcove.
	//========================================================================

	private void Build_Spotlight(FCk_Handle_Transform& InStationTH)
	{
		if (Get_IsPresentationBlocked() || !Can_CreateBaselineChild(FCk_Handle(InStationTH)))
		{ Reject_BaselineCreation(); return; }
		const auto SpotX = Depth * 30.0;     // 30% of half-depth toward front opening
		const auto SpotZ = Height * 90.0;    // near the top of the alcove

		// Default spotlight aims +X. Yaw=180 flips to -X (toward back wall);
		// Pitch=-30 angles down a bit.
		auto LocalTransform = FTransform(FRotator(-30.0, 180.0, 0.0), FVector(SpotX, 0.0, SpotZ), FVector(1.0, 1.0, 1.0));
		_SpotlightSCN = utils_scene_node::Create(InStationTH, LocalTransform);
		Record_BaselineReturn(9, FCk_Handle(_SpotlightSCN), FCk_Handle(InStationTH));
		if (Get_IsPresentationBlocked() || ck::Is_NOT_Valid(_SpotlightSCN) ||
			!utils_scene_node::Has(FCk_Handle(_SpotlightSCN)) ||
			!utils_transform::Has(FCk_Handle(_SpotlightSCN)))
		{ Reject_BaselineCreation(); return; }
		if (!Can_CreateBaselineChild(FCk_Handle(_SpotlightSCN)))
		{ Reject_BaselineCreation(); return; }
		auto PieceTH = _SpotlightSCN.As_Transform();
		auto PieceEntity = FCk_Handle(PieceTH);

		const auto Params = utils_unreal_component::Make_Params(USpotLightComponent, ECk_UnrealComponent_TickPolicy::DoNotTick, n"Spotlight");

		_SpotlightHandle = utils_unreal_component::Add(PieceEntity, Params);
		Record_BaselineReturn(26, FCk_Handle(_SpotlightHandle), FCk_Handle(_SpotlightSCN));
		if (Get_IsPresentationBlocked() || ck::Is_NOT_Valid(_SpotlightHandle) ||
			!utils_unreal_component::Has(FCk_Handle(_SpotlightHandle)))
		{ Reject_BaselineCreation(); return; }

		auto Bound = utils_unreal_component::BindTo_OnAdded(
			_SpotlightHandle,
			FCk_Delegate_UnrealComponent_OnAdded(this, n"OnSpotlightAdded"));
		if (Bound != _SpotlightHandle || Get_IsPresentationBlocked() ||
			ck::Is_NOT_Valid(Bound) || !utils_unreal_component::Has(FCk_Handle(Bound)))
		{ Reject_BaselineCreation(); return; }
	}

	UFUNCTION()
	private void OnSpotlightAdded(FCk_Handle_UnrealComponent InHandle)
	{
		if (Get_IsPresentationBlocked()) { return; }
		if (!Get_IsExpectedBaselineComponent(InHandle, 26, 26))
		{ Reject_BaselineCreation(); return; }
		auto Light = Cast<USpotLightComponent>(utils_unreal_component::Get_Component(InHandle));
		if (Light == nullptr) { Reject_BaselineCreation(); return; }

		Light.SetVisibility(ShowSpotlight, false);
		if (ShowSpotlight == false) { return; }

		Light.SetIntensity(50000.0);
		Light.SetAttenuationRadius(2000.0);
		Light.SetInnerConeAngle(20.0);
		Light.SetOuterConeAngle(40.0);
	}

	//========================================================================
	// Anchors - 7 SceneNodes always created. They are the canonical
	// world-position source for Get_*WorldLocation() getters.
	//========================================================================

	private void Build_Anchors(FCk_Handle_Transform& InStationTH)
	{
		const auto W_cm = Width * 100.0;
		const auto D_cm = Depth * 100.0;
		const auto H_cm = Height * 100.0;

		_AnchorFootprintCenter  = Make_AnchorSCN(InStationTH, 10, FVector(0.0, 0.0, 0.0));
		if (Get_IsPresentationBlocked()) { return; }
		_AnchorAgentSpawnFront  = Make_AnchorSCN(InStationTH, 11, FVector( D_cm * 0.5 + AgentSpawnOffset, 0.0, 0.0));
		if (Get_IsPresentationBlocked()) { return; }
		_AnchorAgentSpawnBack   = Make_AnchorSCN(InStationTH, 12, FVector(-D_cm * 0.5 - AgentSpawnOffset, 0.0, 0.0));
		if (Get_IsPresentationBlocked()) { return; }
		_AnchorAgentSpawnLeft   = Make_AnchorSCN(InStationTH, 13, FVector(0.0,  W_cm * 0.5 + AgentSpawnOffset, 0.0));
		if (Get_IsPresentationBlocked()) { return; }
		_AnchorAgentSpawnRight  = Make_AnchorSCN(InStationTH, 14, FVector(0.0, -W_cm * 0.5 - AgentSpawnOffset, 0.0));
		if (Get_IsPresentationBlocked()) { return; }
		_AnchorPanelTopFront    = Make_AnchorSCN(InStationTH, 15, FVector(D_cm * 0.5, 0.0, H_cm));
		if (Get_IsPresentationBlocked()) { return; }
		_AnchorPanelCenter      = Make_AnchorSCN(InStationTH, 16, FVector(0.0, 0.0, H_cm * 0.5));
	}

	private FCk_Handle_SceneNode Make_AnchorSCN(FCk_Handle_Transform& InStationTH, int32 InRole, FVector InLocalLocation)
	{
		if (Get_IsPresentationBlocked() || !Can_CreateBaselineChild(FCk_Handle(InStationTH)))
		{ Reject_BaselineCreation(); return FCk_Handle_SceneNode(); }
		auto LocalTransform = FTransform(FRotator::ZeroRotator, InLocalLocation, FVector(1.0, 1.0, 1.0));
		auto Node = utils_scene_node::Create(InStationTH, LocalTransform);
		Record_BaselineReturn(InRole, FCk_Handle(Node), FCk_Handle(InStationTH));
		if (Get_IsPresentationBlocked() || ck::Is_NOT_Valid(Node) ||
			!utils_scene_node::Has(FCk_Handle(Node)) || !utils_transform::Has(FCk_Handle(Node)))
		{ Reject_BaselineCreation(); }
		return Node;
	}

	// Spawns a small filled PMG sphere per anchor for in-world visualisation.
	// Cyan for the 4 agent-spawn anchors; magenta for footprint + panel anchors.
	//
	// We use Create_Sphere (spawns a fresh child entity owned by the station)
	// rather than Add_Sphere (which would ENSURE - Add_Sphere internally calls
	// utils_transform::Add, but the anchor SCNs already have Transform fragments
	// from utils_scene_node::Create).
	//
	// World transforms are computed manually from InitialTransform x LocalOffset
	// because at DoConstruct time the SceneNode propagation processor hasn't yet
	// written world transforms to the anchor SCNs - so Get_EntityCurrentLocation
	// would return stale values.
	//
	// Trade-off: spheres are positioned once and won't follow the station if it
	// moves later. Acceptable for a static showcase; revisit if stations become
	// mobile.
	private void Build_AnchorVisuals(FCk_Handle& InStation)
	{
		if (Get_IsPresentationBlocked()) { return; }
		_CreatorUnsupportedTopologyAttempted = true;
		// PMG idiom: translucent fill (alpha 0.35) + wireframe overlay so the
		// underlying station geometry stays readable through the marker.
		const auto AgentColour = FLinearColor(0.0, 1.0, 1.0, 0.35);   // cyan
		const auto PanelColour = FLinearColor(1.0, 0.0, 1.0, 0.35);   // magenta
		const auto Radius = 20.0f;

		const auto W_cm = Width * 100.0;
		const auto D_cm = Depth * 100.0;
		const auto H_cm = Height * 100.0;

		Create_AnchorSphere(InStation, FVector( D_cm * 0.5 + AgentSpawnOffset, 0.0, 0.0), AgentColour, Radius);
		Create_AnchorSphere(InStation, FVector(-D_cm * 0.5 - AgentSpawnOffset, 0.0, 0.0), AgentColour, Radius);
		Create_AnchorSphere(InStation, FVector(0.0,  W_cm * 0.5 + AgentSpawnOffset, 0.0), AgentColour, Radius);
		Create_AnchorSphere(InStation, FVector(0.0, -W_cm * 0.5 - AgentSpawnOffset, 0.0), AgentColour, Radius);
		Create_AnchorSphere(InStation, FVector(0.0, 0.0, 0.0),                            PanelColour, Radius);
		Create_AnchorSphere(InStation, FVector(D_cm * 0.5, 0.0, H_cm),                    PanelColour, Radius);
		Create_AnchorSphere(InStation, FVector(0.0, 0.0, H_cm * 0.5),                     PanelColour, Radius);
	}

	private void Create_AnchorSphere(FCk_Handle& InOwner, FVector InLocalLocation, FLinearColor InColour, float InRadius)
	{
		if (Get_IsPresentationBlocked()) { return; }
		const auto WorldLoc = InitialTransform.TransformPosition(InLocalLocation);
		const auto WorldRot = InitialTransform.Rotator();
		const auto WorldT = FTransform(WorldRot, WorldLoc, FVector(1.0, 1.0, 1.0));

		UCk_Utils_Pmg_BasicShapes::Create_Sphere(
			InOwner,
			WorldT,
			InRadius,
			16,                       // Segments
			16,                       // Rings
			ECk_Plane_Axis::XY,
			InColour,
			true,                     // InDrawLines = true - wireframe overlay (PMG idiom)
			2.0f,                     // InLineThickness
			-1.0f);                   // InDuration = -1.0f for persistent
	}

	//========================================================================
	// Anchor world-position getter - reads the named anchor SCN's current
	// world transform via the framework's transform util. SceneNode
	// propagation keeps this correct even if the station moves (which the
	// FCkGym_Station_Anchors fragment snapshot doesn't).
	//
	// External gym scripts can either:
	//   (a) Call ACk_Gym_Base_PlayerController::Get_StationAnchorLocation(InTag, EAnchor),
	//       which uses the snapshot fragment - fine for static stations.
	//   (b) Call this getter directly - needs the EntityScript instance.
	//========================================================================

	UFUNCTION()
	FVector Get_AnchorWorldLocation(ECk_GymStation_Anchor InAnchor)
	{
		auto SCN = Get_AnchorSCN(InAnchor);
		return utils_transform::Get_EntityCurrentLocation(SCN.As_Transform());
	}

	private FCk_Handle_SceneNode Get_AnchorSCN(ECk_GymStation_Anchor InAnchor)
	{
		if (InAnchor == ECk_GymStation_Anchor::FootprintCenter) { return _AnchorFootprintCenter; }
		if (InAnchor == ECk_GymStation_Anchor::AgentSpawnFront) { return _AnchorAgentSpawnFront; }
		if (InAnchor == ECk_GymStation_Anchor::AgentSpawnBack)  { return _AnchorAgentSpawnBack; }
		if (InAnchor == ECk_GymStation_Anchor::AgentSpawnLeft)  { return _AnchorAgentSpawnLeft; }
		if (InAnchor == ECk_GymStation_Anchor::AgentSpawnRight) { return _AnchorAgentSpawnRight; }
		if (InAnchor == ECk_GymStation_Anchor::PanelTopFront)   { return _AnchorPanelTopFront; }
		return _AnchorPanelCenter;
	}

	// Reads the actual applied native components. All role and world checks precede capture.
	// Whole-graph retirement proof remains a separate responsibility.
	UFUNCTION(BlueprintPure)
	bool TryGet_VisibleGeometry(FCkGym_StationVisibleGeometry&out Out)
	{
		Out = FCkGym_StationVisibleGeometry();
		auto Station = UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(this);
		if (ck::Is_NOT_Valid(Station) || utils_entity_lifetime::Get_IsPendingDestroy(
			Station, ECk_EntityLifetime_DestructionPhase::BeginDestroy)) { return false; }
		if (!utils_transform::Has(Station) ||
			!Math::IsFinite(Width) || Width <= 0 || !Math::IsFinite(Depth) || Depth <= 0 ||
			!Math::IsFinite(Height) || Height <= 0) { return false; }
		if (Station.Has_Fragment(FCkGym_Station_ColoredBody) &&
			Station.Get_Fragment(FCkGym_Station_ColoredBody).Active) { return false; }
		auto World = ck::TransientEntity();
		if (ck::Is_NOT_Valid(World) || utils_entity_lifetime::Get_IsPendingDestroy(
			World, ECk_EntityLifetime_DestructionPhase::BeginDestroy)) { return false; }
		if (ck::Is_NOT_Valid(GetWorld())) { return false; }

		TArray<FCk_Handle_SceneNode> Nodes;
		Nodes.Add(_BackWallSCN); Nodes.Add(_FloorStageSCN); Nodes.Add(_LeftTrimSCN);
		Nodes.Add(_RightTrimSCN); Nodes.Add(_TopTrimSCN); Nodes.Add(_BottomTrimSCN);
		Nodes.Add(_TitleSCN); Nodes.Add(_DescriptionSCN); Nodes.Add(_FloorDescriptionSCN);
		Nodes.Add(_SpotlightSCN);
		Nodes.Add(_AnchorFootprintCenter); Nodes.Add(_AnchorAgentSpawnFront);
		Nodes.Add(_AnchorAgentSpawnBack); Nodes.Add(_AnchorAgentSpawnLeft);
		Nodes.Add(_AnchorAgentSpawnRight); Nodes.Add(_AnchorPanelTopFront);
		Nodes.Add(_AnchorPanelCenter);
		TArray<FCk_Handle_UnrealComponent> Components;
		Components.Add(_BackWallHandle); Components.Add(_FloorStageHandle);
		Components.Add(_LeftTrimHandle); Components.Add(_RightTrimHandle);
		Components.Add(_TopTrimHandle); Components.Add(_BottomTrimHandle);
		Components.Add(_TitleHandle); Components.Add(_DescriptionHandle);
		Components.Add(_FloorDescriptionHandle); Components.Add(_SpotlightHandle);
		if (Nodes.Num() != 17 || Components.Num() != 10) { return false; }

		// Catch aliases (including a body node substituted with the spotlight node) globally.
		TArray<FCk_Handle> Seen;
		for (auto Node : Nodes)
		{
			FCk_Handle H = FCk_Handle(Node);
			if (ck::Is_NOT_Valid(H) || utils_entity_lifetime::Get_IsPendingDestroy(
				H, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
				!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) ||
				Seen.Contains(H)) { return false; }
			Seen.Add(H);
		}
		for (auto Component : Components)
		{
			FCk_Handle H = FCk_Handle(Component);
			if (ck::Is_NOT_Valid(H) || utils_entity_lifetime::Get_IsPendingDestroy(
				H, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
				!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) ||
				Seen.Contains(H)) { return false; }
			Seen.Add(H);
		}
		if (Seen.Num() != 27) { return false; }

		// The total native census validates the selected registry and exact direct owner.
		TArray<FCk_Handle> StationChildren;
		if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
			Station, World, StationChildren)) { return false; }
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			auto Node = FCk_Handle(Nodes[i]);
			if (!StationChildren.Contains(Node) || !utils_scene_node::Has(Node) ||
				!utils_transform::Has(Node) ||
				utils_scene_node::Get_Parent(Nodes[i]) != Station.As_Transform())
			{ return false; }
			if (i >= Components.Num()) { continue; }
			TArray<FCk_Handle> NodeChildren;
			if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
				Node, World, NodeChildren) ||
				!NodeChildren.Contains(FCk_Handle(Components[i])) ||
				!utils_unreal_component::Has(FCk_Handle(Components[i])))
			{ return false; }
		}

		TArray<ECkGym_StationVisibleRole> Roles;
		Roles.Add(ECkGym_StationVisibleRole::BackWall);
		Roles.Add(ECkGym_StationVisibleRole::FloorStage);
		Roles.Add(ECkGym_StationVisibleRole::LeftTrim);
		Roles.Add(ECkGym_StationVisibleRole::RightTrim);
		Roles.Add(ECkGym_StationVisibleRole::TopTrim);
		Roles.Add(ECkGym_StationVisibleRole::BottomTrim);
		Roles.Add(ECkGym_StationVisibleRole::TitleText);
		Roles.Add(ECkGym_StationVisibleRole::DescriptionText);
		Roles.Add(ECkGym_StationVisibleRole::FloorDescriptionText);
		Roles.Add(ECkGym_StationVisibleRole::Spotlight);
		FCkGym_StationVisibleGeometry Candidate;
		Candidate.AutoSize = AutoSize;
		Candidate.Width = Width; Candidate.Depth = Depth; Candidate.Height = Height;
		Candidate.StationWorldTransform = utils_transform::Get_EntityCurrentTransform(Station.As_Transform());
		if (!Candidate.StationWorldTransform.IsValid() ||
			Candidate.StationWorldTransform.ContainsNaN()) { return false; }
		for (int32 i = 0; i < Components.Num(); ++i)
		{
			FCkGym_StationVisiblePiece Piece;
			if (!UCk_GymStation_VisibleRuntime_UE::TryCapture_VisiblePiece(
				Station, Nodes[i], Components[i], Roles[i], GetWorld(), Piece) ||
				Piece.Role != Roles[i]) { return false; }
			Candidate.Pieces.Add(Piece);
		}
		for (int32 i = Components.Num(); i < Nodes.Num(); ++i)
		{
			auto T = utils_transform::Get_EntityCurrentTransform(Nodes[i].As_Transform());
			if (!T.IsValid() || T.ContainsNaN()) { return false; }
			Candidate.Anchors.Add(T);
		}
		if (Candidate.Pieces.Num() != 10 || Candidate.Anchors.Num() != 7) { return false; }
		Out = Candidate;
		return true;
	}

	//========================================================================
	// Auto-sizing - heuristic Width/Height from text content.
	//
	// Spawn-time: walk the spawn-params Title + DescriptionText (TArray<FText>)
	// to find longest line + line count. Override Width/Height before geometry
	// is built. Exact since the array gives us per-line counts.
	//
	// Runtime: when Update_StationDisplay writes the fragment, recompute
	// required size from Fragment.Title + Fragment.Description (which may
	// contain "\n" for multi-line). Grow-only - Width/Height only ever
	// increase. Avoids pulsing as live-status text fluctuates.
	//
	// Heuristic uses approximate char-width (0.6 x DescriptionScale) and
	// line-height (1.5 x DescriptionScale). Imprecise but safely over-sizes
	// rather than truncating.
	//========================================================================

	private double Compute_AutoWidth(int32 InLongestLineChars)
	{
		const auto Padding_cm = 60.0;
		const auto CharWidthFactor = 0.6;        // approx world-cm per char per scale unit
		const auto MinWidth_units = 3.0;
		const auto TextWidth_cm = double(InLongestLineChars) * DescriptionScale * CharWidthFactor;
		return Math::Max(MinWidth_units, (TextWidth_cm + 2.0 * Padding_cm) / 100.0);
	}

	private double Compute_AutoHeight(int32 InDescLineCount)
	{
		const auto Padding_cm = 60.0;
		const auto LineHeightFactor = 1.5;
		const auto MinHeight_units = 2.0;
		const auto TextHeight_cm = TitleScale + double(InDescLineCount) * DescriptionScale * LineHeightFactor;
		return Math::Max(MinHeight_units, (TextHeight_cm + 2.0 * Padding_cm) / 100.0);
	}

	private void Apply_AutoSize_FromSpawnParams()
	{
		if (Get_IsPresentationBlocked()) { return; }
		int32 LongestChars = TitleText.ToString().Len();
		for (auto Line : DescriptionText)
		{
			const auto Len = Line.ToString().Len();
			if (Len > LongestChars) { LongestChars = Len; }
		}

		Width  = Compute_AutoWidth(LongestChars);
		Height = Compute_AutoHeight(DescriptionText.Num());
	}

	// Grow-only resize using the actual rendered bounds from the
	// UTextRenderComponents. Called after every SetText (in OnTitleAdded,
	// OnDescriptionAdded, Apply_TitleText, Apply_DescriptionText) so the
	// alcove tracks the high-water mark of measured text size.
	//
	// GetTextLocalSize returns the local bounding box of the rendered text
	// quad: Y = horizontal width (text reading direction), Z = vertical
	// height (line stack). X is the quad's normal and is always 0.
	private void Refit_FromMeasuredBounds()
	{
		if (Get_IsPresentationBlocked()) { return; }
		// A synchronous OnAdded callback may precede the other text creator.
		if (_CreatorHandles.Num() != 28 || _CreatorHandles[23] == FCk_Handle() ||
			_CreatorHandles[24] == FCk_Handle()) { return; }
		if (!Get_IsExpectedBaselineComponent(_TitleHandle, 23, 23) ||
			!Get_IsExpectedBaselineComponent(_DescriptionHandle, 24, 24))
		{ Reject_BaselineCreation(); return; }
		auto TitleComp = Cast<UTextRenderComponent>(utils_unreal_component::Get_Component(_TitleHandle));
		auto DescComp  = Cast<UTextRenderComponent>(utils_unreal_component::Get_Component(_DescriptionHandle));

		// Both components must exist for the measurement; this method is
		// called from each Apply_*Text handler so it may be invoked before
		// the other component is ready - early out and we'll catch up next call.
		// A recorded peer may still be awaiting its own OnAdded payload.
		if (TitleComp == nullptr || DescComp == nullptr) { return; }

		const auto TitleSize = TitleComp.GetTextLocalSize();
		const auto DescSize  = DescComp.GetTextLocalSize();

		auto MaxWidth_cm    = Math::Max(TitleSize.Y, DescSize.Y);
		auto TotalHeight_cm = TitleSize.Z + DescSize.Z;

		// Coloured runs render outside the description component, so the alcove
		// would never grow to fit them without measuring each one. With no runs
		// this loop is a no-op and the plain path measures what it always did.
		for (auto Index = 0; Index < _ColouredRunHandles.Num(); Index++)
		{
			if (!Get_IsExpectedOptionalTextComponent(_ColouredRunHandles[Index]))
			{ Reject_BaselineCreation(); return; }
			auto RunComp = Cast<UTextRenderComponent>(utils_unreal_component::Get_Component(_ColouredRunHandles[Index]));
			if (RunComp == nullptr) { continue; }

			const auto RunSize = RunComp.GetTextLocalSize();
			MaxWidth_cm = Math::Max(MaxWidth_cm, RunSize.Y);
			TotalHeight_cm += RunSize.Z;
		}

		const auto Padding_cm    = 60.0;
		const auto GapBetween_cm = 30.0;
		const auto MinW_units    = 3.0;
		const auto MinH_units    = 2.0;

		const auto RequiredW = Math::Max(MinW_units, (MaxWidth_cm + 2.0 * Padding_cm) / 100.0);
		const auto RequiredH = Math::Max(MinH_units, (TotalHeight_cm + GapBetween_cm + 2.0 * Padding_cm) / 100.0);

		const auto NeedsGrow = (RequiredW > Width) || (RequiredH > Height);

		if (ShowDebugOverlays)
		{
			Print(f"[GymStation Refit] TitleSize={TitleSize}  DescSize={DescSize}  Required=({RequiredW :.2}, {RequiredH :.2})  Current=({Width :.2}, {Height :.2})  NeedsGrow={NeedsGrow}");
		}

		if (NeedsGrow == false) { return; }

		Width  = Math::Max(Width,  RequiredW);
		Height = Math::Max(Height, RequiredH);

		Resize_Alcove();
	}

	//========================================================================
	// Resize - re-applies local transforms to all SCNs based on current
	// Width / Depth / Height. Used by Refit_IfOverflow on grow.
	//========================================================================

	private void Resize_Alcove()
	{
		if (Get_IsPresentationBlocked()) { return; }
		Update_BodyTransforms();
		Update_TextTransforms();
		Update_SpotlightTransform();
		Update_AnchorTransforms();

		// Populate_AnchorsFragment takes FCk_Handle& (non-const ref) so it
		// can call AddOrGet_Fragment. ck::ToEntity(this) returns a temporary,
		// so stash it in a local first.
		auto SelfEntity = ck::ToEntity(this);
		Populate_AnchorsFragment(SelfEntity);
	}

	private void Update_BodyTransforms()
	{
		if (Get_IsPresentationBlocked()) { return; }
		const auto W_cm = Width * 100.0;
		const auto D_cm = Depth * 100.0;
		const auto H_cm = Height * 100.0;
		const auto WT = WallThickness;
		const auto FT = FloorThickness;
		const auto TrimCenterX = -D_cm * 0.5 + WT + TrimDepth * 0.5;

		if (ShowDebugOverlays)
		{
			Print(f"[GymStation Resize] Width={Width :.2}  Depth={Depth :.2}  Height={Height :.2}");
		}

		Update_PieceLocAndScale_Logged(_BackWallSCN, "BackWall",
			FVector(-D_cm * 0.5 + WT * 0.5, 0.0, H_cm * 0.5),
			FVector(WT / 100.0, Width, Height));

		Update_PieceLocAndScale_Logged(_FloorStageSCN, "FloorStage",
			FVector(0.0, 0.0, FT * 0.5),
			FVector(Depth, Width, FT / 100.0));

		Update_PieceLocAndScale_Logged(_LeftTrimSCN, "LeftTrim",
			FVector(TrimCenterX, W_cm * 0.5 - WT * 0.5, H_cm * 0.5),
			FVector(TrimDepth / 100.0, WT / 100.0, Height));

		Update_PieceLocAndScale_Logged(_RightTrimSCN, "RightTrim",
			FVector(TrimCenterX, -W_cm * 0.5 + WT * 0.5, H_cm * 0.5),
			FVector(TrimDepth / 100.0, WT / 100.0, Height));

		Update_PieceLocAndScale_Logged(_TopTrimSCN, "TopTrim",
			FVector(TrimCenterX, 0.0, H_cm - WT * 0.5),
			FVector(TrimDepth / 100.0, Width, WT / 100.0));

		Update_PieceLocAndScale_Logged(_BottomTrimSCN, "BottomTrim",
			FVector(TrimCenterX, 0.0, FT + WT * 0.5),
			FVector(TrimDepth / 100.0, Width, WT / 100.0));

		if (ShowDebugOverlays)
		{
			Rebuild_DebugOverlays();
		}
	}

	private void Update_PieceLocAndScale_Logged(FCk_Handle_SceneNode& InSCN, FString InName, FVector InLoc, FVector InScale)
	{
		if (ShowDebugOverlays)
		{
			Print(f"[GymStation Resize]   {InName}: loc={InLoc}  scale={InScale}");
		}
		Update_PieceLocAndScale(InSCN, InLoc, InScale);
	}

	private void Update_TextTransforms()
	{
		if (Get_IsPresentationBlocked()) { return; }
		const auto BackWallInnerX = -Depth * 50.0 + WallThickness;
		const auto TextX = BackWallInnerX + 5.0;
		const auto PanelTopZ = Height * 100.0;
		const auto DescZ  = PanelTopZ - (TitleScale + 60.0);
		const auto TitleZ = PanelTopZ - (TitleScale + 60.0) / 2.0;
		const auto TextY = TextAlignmentOffset(TextAlignment, 1.0);

		Update_PieceLocAndRot(_TitleSCN,       FVector(TextX, TextY, TitleZ), FRotator::ZeroRotator);
		Update_PieceLocAndRot(_DescriptionSCN, FVector(TextX, TextY, DescZ),  FRotator::ZeroRotator);

		const auto FloorTopZ = FloorThickness + 2.0;
		const auto FloorX = FloorPlacementOffset(1.0);
		const auto FloorY = TextAlignmentOffset(FloorTextAlignment, 1.0);
		Update_PieceLocAndRot(_FloorDescriptionSCN, FVector(FloorX, FloorY, FloorTopZ), FRotator(90.0, 0.0, 0.0));

		// No-op on a station that never wrote a coloured body.
		Update_ColouredRunTransforms();
	}

	private void Update_SpotlightTransform()
	{
		if (Get_IsPresentationBlocked()) { return; }
		const auto SpotX = Depth * 30.0;
		const auto SpotZ = Height * 90.0;
		Update_PieceLocAndRot(_SpotlightSCN, FVector(SpotX, 0.0, SpotZ), FRotator(-30.0, 180.0, 0.0));
	}

	private void Update_AnchorTransforms()
	{
		if (Get_IsPresentationBlocked()) { return; }
		const auto W_cm = Width * 100.0;
		const auto D_cm = Depth * 100.0;
		const auto H_cm = Height * 100.0;

		Update_PieceLocOnly(_AnchorFootprintCenter,  FVector(0.0, 0.0, 0.0));
		Update_PieceLocOnly(_AnchorAgentSpawnFront,  FVector( D_cm * 0.5 + AgentSpawnOffset, 0.0, 0.0));
		Update_PieceLocOnly(_AnchorAgentSpawnBack,   FVector(-D_cm * 0.5 - AgentSpawnOffset, 0.0, 0.0));
		Update_PieceLocOnly(_AnchorAgentSpawnLeft,   FVector(0.0,  W_cm * 0.5 + AgentSpawnOffset, 0.0));
		Update_PieceLocOnly(_AnchorAgentSpawnRight,  FVector(0.0, -W_cm * 0.5 - AgentSpawnOffset, 0.0));
		Update_PieceLocOnly(_AnchorPanelTopFront,    FVector(D_cm * 0.5, 0.0, H_cm));
		Update_PieceLocOnly(_AnchorPanelCenter,      FVector(0.0, 0.0, H_cm * 0.5));
	}

	// Direct SCN updates - issued as a SINGLE full-transform request per piece.
	//
	// Why not the per-component Request_UpdateOffset_Location / _Rotation /
	// _Scale helpers: those each read the current offset, mutate one field,
	// and queue a full-transform request. SCN requests are processed in batch
	// later by FProcessor_SceneNode_HandleRequests, so issuing two helpers
	// in sequence means the second read sees the still-unmodified offset
	// (its mutation lands on stale values for the OTHER fields) and its
	// queued request clobbers the first when both run. Net effect during
	// runtime resize: only the *last* helper's mutated field survives;
	// everything else reverts to pre-resize values. Concretely with
	// Location-then-Scale, locations were dropped on every resize, leaving
	// pieces visually stuck at their initial Z while scales kept growing.
	//
	// Composing the full FTransform up front and submitting it once avoids
	// the round-trip entirely.
	private void Update_PieceLocAndScale(FCk_Handle_SceneNode& InSCN, FVector InLoc, FVector InScale)
	{
		if (Get_IsPresentationBlocked()) { return; }
		auto NewOffset = utils_scene_node::Get_Offset(InSCN);
		NewOffset.SetLocation(InLoc);
		NewOffset.SetScale3D(InScale);
		utils_scene_node::Request_UpdateOffset(InSCN, FCk_Request_SceneNode_UpdateRelativeTransform(NewOffset));
	}

	private void Update_PieceLocAndRot(FCk_Handle_SceneNode& InSCN, FVector InLoc, FRotator InRot)
	{
		if (Get_IsPresentationBlocked()) { return; }
		auto NewOffset = utils_scene_node::Get_Offset(InSCN);
		NewOffset.SetLocation(InLoc);
		NewOffset.SetRotation(InRot.Quaternion());
		utils_scene_node::Request_UpdateOffset(InSCN, FCk_Request_SceneNode_UpdateRelativeTransform(NewOffset));
	}

	private void Update_PieceLocOnly(FCk_Handle_SceneNode& InSCN, FVector InLoc)
	{
		if (Get_IsPresentationBlocked()) { return; }
		auto NewOffset = utils_scene_node::Get_Offset(InSCN);
		NewOffset.SetLocation(InLoc);
		utils_scene_node::Request_UpdateOffset(InSCN, FCk_Request_SceneNode_UpdateRelativeTransform(NewOffset));
	}

	//========================================================================
	// Debug overlays - translucent wireframe PMG boxes drawn at each piece's
	// EXPECTED world transform. Useful for spotting a partial-resize bug:
	// if a piece's actual rendered geometry doesn't sit inside its overlay,
	// that piece's transform isn't tracking the latest dimensions.
	//
	// Colour key:
	//   Back wall  -> red
	//   Floor      -> blue
	//   Top trim   -> yellow
	//   Bottom trim-> orange
	//   Left trim  -> cyan
	//   Right trim -> magenta
	//========================================================================

	private void Build_DebugOverlays(FCk_Handle& InOwner)
	{
		if (Get_IsPresentationBlocked()) { return; }
		_CreatorUnsupportedTopologyAttempted = true;
		Destroy_DebugOverlays();
		Push_BodyOverlays(InOwner);
	}

	private void Rebuild_DebugOverlays()
	{
		if (Get_IsPresentationBlocked()) { return; }
		_CreatorUnsupportedTopologyAttempted = true;
		auto SelfEntity = ck::ToEntity(this);
		Destroy_DebugOverlays();
		Push_BodyOverlays(SelfEntity);
	}

	private void Destroy_DebugOverlays()
	{
		if (Get_IsPresentationBlocked()) { return; }
		for (auto Handle : _DebugOverlays)
		{
			utils_entity_lifetime::Request_DestroyEntity(Handle);
		}
		_DebugOverlays.Empty();
	}

	private void Push_BodyOverlays(FCk_Handle& InOwner)
	{
		if (Get_IsPresentationBlocked()) { return; }
		const auto W_cm = Width * 100.0;
		const auto D_cm = Depth * 100.0;
		const auto H_cm = Height * 100.0;
		const auto WT = WallThickness;
		const auto FT = FloorThickness;
		const auto TrimCenterX = -D_cm * 0.5 + WT + TrimDepth * 0.5;

		// Back wall - red
		Push_OverlayBoxAt(InOwner,
			FVector(-D_cm * 0.5 + WT * 0.5, 0.0, H_cm * 0.5),
			FVector(WT * 0.5, W_cm * 0.5, H_cm * 0.5),
			FLinearColor(1.0, 0.0, 0.0, 0.25));

		// Floor - blue
		Push_OverlayBoxAt(InOwner,
			FVector(0.0, 0.0, FT * 0.5),
			FVector(D_cm * 0.5, W_cm * 0.5, FT * 0.5),
			FLinearColor(0.0, 0.4, 1.0, 0.25));

		// Left trim - cyan
		Push_OverlayBoxAt(InOwner,
			FVector(TrimCenterX, W_cm * 0.5 - WT * 0.5, H_cm * 0.5),
			FVector(TrimDepth * 0.5, WT * 0.5, H_cm * 0.5),
			FLinearColor(0.0, 1.0, 1.0, 0.25));

		// Right trim - magenta
		Push_OverlayBoxAt(InOwner,
			FVector(TrimCenterX, -W_cm * 0.5 + WT * 0.5, H_cm * 0.5),
			FVector(TrimDepth * 0.5, WT * 0.5, H_cm * 0.5),
			FLinearColor(1.0, 0.0, 1.0, 0.25));

		// Top trim - yellow
		Push_OverlayBoxAt(InOwner,
			FVector(TrimCenterX, 0.0, H_cm - WT * 0.5),
			FVector(TrimDepth * 0.5, W_cm * 0.5, WT * 0.5),
			FLinearColor(1.0, 1.0, 0.0, 0.25));

		// Bottom trim - orange
		Push_OverlayBoxAt(InOwner,
			FVector(TrimCenterX, 0.0, FT + WT * 0.5),
			FVector(TrimDepth * 0.5, W_cm * 0.5, WT * 0.5),
			FLinearColor(1.0, 0.5, 0.0, 0.25));
	}

	private void Push_OverlayBoxAt(FCk_Handle& InOwner, FVector InLocalLocation, FVector InHalfExtent, FLinearColor InColour)
	{
		if (Get_IsPresentationBlocked()) { return; }
		const auto WorldLoc = InitialTransform.TransformPosition(InLocalLocation);
		const auto WorldRot = InitialTransform.Rotator();
		const auto BoxTransform = FTransform(WorldRot, WorldLoc, FVector(1.0, 1.0, 1.0));

		auto Box = UCk_Utils_Pmg_BasicShapes::Create_Box(
			InOwner,
			BoxTransform,
			InHalfExtent,
			ECk_Plane_Axis::XY,
			InColour,
			true,    // wireframe overlay
			3.0f,
			-1.0f);  // persistent

		_DebugOverlays.Add(Box);
	}
}
