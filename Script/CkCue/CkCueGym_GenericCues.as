//============================================================================
// CUE GYM - GAMEPLAY TAGS
//============================================================================

namespace Ck
{
	asset Asset_CueGym_Tags of UCk_GameplayTags
	{
		GameplayTags.Add(n"CueGym.Lifetime.AfterOneFrame");
		GameplayTags.Add(n"CueGym.Lifetime.Persistent");
		GameplayTags.Add(n"CueGym.Lifetime.Timed");
		GameplayTags.Add(n"CueGym.Lifetime.Custom");
		GameplayTags.Add(n"CueGym.Concurrency.Multiple");
		GameplayTags.Add(n"CueGym.Concurrency.Restart");
		GameplayTags.Add(n"CueGym.Owner.SkipInvalid");
		GameplayTags.Add(n"CueGym.Owner.RequireValid");
		GameplayTags.Add(n"CueGym.Restart.Restartable");
		GameplayTags.Add(n"CueGym.Transient.NoOwner");
		GameplayTags.Add(n"CueGym.OwnerDestruction.Attached");
	}
}

//============================================================================
// CUE GYM - SPAWN PARAMS
//============================================================================

USTRUCT()
struct FCkCueGym_SpawnParams
{
	UPROPERTY()
	FTransform InitialTransform = FTransform::Identity;
	UPROPERTY() int32 SourceGeneration = 0;
	UPROPERTY() int32 RoleEpoch = 0;
	UPROPERTY() int64 EventSerial = 0;

	FCkCueGym_SpawnParams() {}
	FCkCueGym_SpawnParams(FTransform InTransform)
	{
		InitialTransform = InTransform;
	}
}

//============================================================================
// CUE GYM - PMG SPHERE HELPER
//============================================================================

namespace CueGym_Pmg
{
	// A total ownership walk binds this draw to the current world's root.
	// Do not use the recursive world getter on malformed lifetime chains.
	bool Get_HasDrawingContext(FCk_Handle Entity, FCk_Handle WorldRoot)
	{
		auto Current = Entity;
		TArray<FCk_Handle> Seen;
		for (int32 Depth = 0; Depth < 64; ++Depth)
		{
			if (ck::Is_NOT_Valid(Current) || Seen.Contains(Current)) { return false; }
			if (utils_entity_lifetime::Get_IsPendingDestroy(
				Current, ECk_EntityLifetime_DestructionPhase::BeginDestroy)) { return false; }
			if (Current == WorldRoot) { return true; }
			if (!utils_entity_lifetime::Has_LifetimeOwner(Current)) { return false; }
			Seen.Add(Current);
			Current = utils_entity_lifetime::Get_LifetimeOwner(Current);
		}
		return false;
	}

	// Draws a PMG filled sphere at entity location + offset, returns handle for cleanup
	FCk_Handle_Pmg_DebugShape DrawSphere(FCk_Handle InEntity, FVector InOffset, FLinearColor InColor, float32 InRadius = 20.0f)
	{
		if (ck::Is_NOT_Valid(InEntity) || !utils_transform::Has(InEntity) ||
			!utils_entity_lifetime::Get_CanCreateEntity(InEntity))
		{ return FCk_Handle_Pmg_DebugShape(); }
		auto WorldRoot = ck::TransientEntity();
		if (ck::Is_NOT_Valid(WorldRoot) ||
			CkGym_Admission::Get_IsClosedUnproven(WorldRoot) ||
			!Get_HasDrawingContext(InEntity, WorldRoot) ||
			!Math::IsFinite(InColor.R) || !Math::IsFinite(InColor.G) ||
			!Math::IsFinite(InColor.B) || !Math::IsFinite(InColor.A))
		{ return FCk_Handle_Pmg_DebugShape(); }
		auto TransformHandle = InEntity.As_Transform();
		if (ck::Is_NOT_Valid(TransformHandle))
		{
			return FCk_Handle_Pmg_DebugShape();
		}

		auto Transform = utils_transform::Get_EntityCurrentTransform(TransformHandle);
		auto Pos = Transform.GetLocation() + InOffset;
		if (!Transform.IsValid() || Transform.ContainsNaN() || Pos.ContainsNaN() ||
			InRadius <= 0.0f || !Math::IsFinite(InRadius))
		{ return FCk_Handle_Pmg_DebugShape(); }
		// The cue's lifetime owns its current visual, including owner cascades.
		auto Shape = utils_entity_lifetime::Request_CreateEntity(InEntity);
		if (ck::Is_NOT_Valid(Shape))
		{ CkGym_Admission::TryCloseWorldUnproven(WorldRoot); return FCk_Handle_Pmg_DebugShape(); }
		auto Result = utils_pmg_basic_shapes::Add_Sphere(Shape, FTransform(Pos),
			InRadius, 12, 12, ECk_Plane_Axis::XY, InColor, true, 2.0f, -1.0f);
		if (ck::Is_NOT_Valid(Result) || !utils_transform::Has(Result))
		{ CkGym_Admission::TryCloseWorldUnproven(WorldRoot); return FCk_Handle_Pmg_DebugShape(); }
		return Result;
	}

	// Draws a PMG filled box at entity location + offset, returns handle for cleanup
	FCk_Handle_Pmg_DebugShape DrawBox(FCk_Handle InEntity, FVector InOffset, FLinearColor InColor, FVector InExtent = FVector(20.0f, 20.0f, 20.0f))
	{
		if (ck::Is_NOT_Valid(InEntity) || !utils_transform::Has(InEntity) ||
			!utils_entity_lifetime::Get_CanCreateEntity(InEntity))
		{ return FCk_Handle_Pmg_DebugShape(); }
		auto WorldRoot = ck::TransientEntity();
		if (ck::Is_NOT_Valid(WorldRoot) ||
			CkGym_Admission::Get_IsClosedUnproven(WorldRoot) ||
			!Get_HasDrawingContext(InEntity, WorldRoot) ||
			!Math::IsFinite(InColor.R) || !Math::IsFinite(InColor.G) ||
			!Math::IsFinite(InColor.B) || !Math::IsFinite(InColor.A))
		{ return FCk_Handle_Pmg_DebugShape(); }
		auto TransformHandle = InEntity.As_Transform();
		if (ck::Is_NOT_Valid(TransformHandle))
		{
			return FCk_Handle_Pmg_DebugShape();
		}

		auto Transform = utils_transform::Get_EntityCurrentTransform(TransformHandle);
		auto Pos = Transform.GetLocation() + InOffset;
		if (!Transform.IsValid() || Transform.ContainsNaN() || Pos.ContainsNaN() ||
			InExtent.ContainsNaN() || InExtent.X <= 0.0 || InExtent.Y <= 0.0 ||
			InExtent.Z <= 0.0)
		{ return FCk_Handle_Pmg_DebugShape(); }
		auto Shape = utils_entity_lifetime::Request_CreateEntity(InEntity);
		if (ck::Is_NOT_Valid(Shape))
		{ CkGym_Admission::TryCloseWorldUnproven(WorldRoot); return FCk_Handle_Pmg_DebugShape(); }
		auto Result = utils_pmg_basic_shapes::Add_Box(Shape, FTransform(Pos),
			InExtent, ECk_Plane_Axis::XY, InColor, true, 2.0f, -1.0f);
		if (ck::Is_NOT_Valid(Result) || !utils_transform::Has(Result))
		{ CkGym_Admission::TryCloseWorldUnproven(WorldRoot); return FCk_Handle_Pmg_DebugShape(); }
		return Result;
	}

	// Replacing a draw requires its exact previous owned receipt. A lost or
	// foreign receipt closes admission; it is never adopted or destroyed.
	bool TryRetirePreviousShape(FCk_Handle Owner, FCk_Handle_Pmg_DebugShape Shape, bool Expected)
	{
		if (ck::Is_NOT_Valid(Owner)) { return false; }
		auto Root = ck::TransientEntity();
		if (ck::Is_NOT_Valid(Root) || !Get_HasDrawingContext(Owner, Root)) { return false; }
		if (!utils_entity_lifetime::Get_CanCreateEntity(Owner)) { return false; }
		if (ck::Is_NOT_Valid(Shape))
		{
			if (Expected) { CkGym_Admission::TryCloseWorldUnproven(Root); return false; }
			return !CkGym_Admission::Get_IsClosedUnproven(Root);
		}
		// A receipt already entering destruction is lost for replacement. Do not
		// inspect its PMG/lifetime fragments or enqueue destruction again.
		if (utils_entity_lifetime::Get_IsPendingDestroy(
				Shape, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
			utils_entity_lifetime::Get_IsPendingDestroy(
				Shape, ECk_EntityLifetime_DestructionPhase::Teardown) ||
			utils_entity_lifetime::Get_IsPendingDestroy(
				Shape, ECk_EntityLifetime_DestructionPhase::Destroyed))
		{ CkGym_Admission::TryCloseWorldUnproven(Root); return false; }
		if (!UCk_Utils_Pmg_DebugShape_UE::Has(FCk_Handle(Shape)) ||
			!utils_entity_lifetime::Has_LifetimeOwner(Shape) ||
			utils_entity_lifetime::Get_LifetimeOwner(Shape) != Owner)
		{ CkGym_Admission::TryCloseWorldUnproven(Root); return false; }
		utils_entity_lifetime::Request_DestroyEntity(Shape);
		return !CkGym_Admission::Get_IsClosedUnproven(Root);
	}
}

//============================================================================
// LIFETIME CUES
//============================================================================

// AfterOneFrame: Destroyed immediately after BeginPlay (one-frame lifetime)
class UCk_CueGym_Cue_AfterOneFrame : UCk_GenericCue_EntityScript
{
	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	default _Replication = ECk_Replication::DoesNotReplicate;
	default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::AfterOneFrame;
	default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::AllowMultiple;

	UFUNCTION(BlueprintOverride)
	FGameplayTag Get_CueName() const
	{
		return GameplayTags::ResolveGameplayTag(n"CueGym.Lifetime.AfterOneFrame");
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		ck::Trace("CueGym: AfterOneFrame cue constructed - will be destroyed after one frame");
		return ECk_EntityScript_ConstructionFlow::Finished;
	}
}

// Persistent: Stays alive indefinitely until explicitly destroyed
class UCk_CueGym_Cue_Persistent : UCk_GenericCue_EntityScript
{
	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	default _Replication = ECk_Replication::DoesNotReplicate;
	default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::Persistent;
	default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::AllowMultiple;

	FCk_Handle_Pmg_DebugShape ShapeHandle;
	private bool _VisualDrawAttempted = false;

	// An invalid receipt after this flag is set is a failed draw, not startup.
	UFUNCTION(BlueprintPure)
	bool Get_HasVisualDrawAttempted() const
	{
		return _VisualDrawAttempted;
	}

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_VisualShape() const
	{
		return FCk_Handle(ShapeHandle);
	}

	UFUNCTION(BlueprintOverride)
	FGameplayTag Get_CueName() const
	{
		return GameplayTags::ResolveGameplayTag(n"CueGym.Lifetime.Persistent");
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_CueGym_Cue_Persistent");

		// Tick timer for continuous sphere drawing
		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DrawTick"));

		ck::Trace("CueGym: Persistent cue constructed - will stay alive indefinitely");
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DrawTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CueGym_Pmg::TryRetirePreviousShape(ck::ToEntity(this), ShapeHandle, _VisualDrawAttempted)) { return; }
		_VisualDrawAttempted = true;
		ShapeHandle = CueGym_Pmg::DrawSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 100.0f),
			FLinearColor(0.0f, 1.0f, 0.0f, 0.3f), 20.0f);
	}
}

// Timed: Auto-destroys after _LifetimeDuration seconds
class UCk_CueGym_Cue_Timed : UCk_GenericCue_EntityScript
{
	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	default _Replication = ECk_Replication::DoesNotReplicate;
	default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::Timed;
	default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::AllowMultiple;
	default _LifetimeDuration = FCk_Time(5.0f);

	FCk_Handle_Pmg_DebugShape ShapeHandle;
	private bool _VisualDrawAttempted = false;

	// An invalid receipt after this flag is set is a failed draw, not startup.
	UFUNCTION(BlueprintPure)
	bool Get_HasVisualDrawAttempted() const
	{
		return _VisualDrawAttempted;
	}

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_VisualShape() const
	{
		return FCk_Handle(ShapeHandle);
	}

	UFUNCTION(BlueprintOverride)
	FGameplayTag Get_CueName() const
	{
		return GameplayTags::ResolveGameplayTag(n"CueGym.Lifetime.Timed");
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);

		// Tick timer for sphere drawing while alive
		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DrawTick"));

		ck::Trace("CueGym: Timed cue constructed - will auto-destroy after 5s");
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DrawTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CueGym_Pmg::TryRetirePreviousShape(ck::ToEntity(this), ShapeHandle, _VisualDrawAttempted)) { return; }
		_VisualDrawAttempted = true;
		ShapeHandle = CueGym_Pmg::DrawSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 100.0f),
			FLinearColor(0.0f, 0.5f, 1.0f, 0.3f), 20.0f);
	}
}

// Custom: No automatic lifetime management - self-destructs via internal timer
class UCk_CueGym_Cue_Custom : UCk_GenericCue_EntityScript
{
	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	default _Replication = ECk_Replication::DoesNotReplicate;
	default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::Custom;
	default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::AllowMultiple;

	FCk_Handle_Pmg_DebugShape ShapeHandle;
	private bool _VisualDrawAttempted = false;

	// An invalid receipt after this flag is set is a failed draw, not startup.
	UFUNCTION(BlueprintPure)
	bool Get_HasVisualDrawAttempted() const
	{
		return _VisualDrawAttempted;
	}

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_VisualShape() const
	{
		return FCk_Handle(ShapeHandle);
	}

	UFUNCTION(BlueprintOverride)
	FGameplayTag Get_CueName() const
	{
		return GameplayTags::ResolveGameplayTag(n"CueGym.Lifetime.Custom");
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);

		// Tick timer for sphere drawing while alive
		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DrawTick"));

		// Self-destruct timer: 3 seconds
		auto DestroyParams = FCk_Timer_Spec(FCk_Time(3.0f));
		DestroyParams.Set_StartingState(ECk_Timer_State::Running);
		DestroyParams.Set_Behavior(ECk_Timer_Behavior::PauseOnDone);
		auto DestroyTimer = utils_timer::Add(InHandle, DestroyParams);
		DestroyTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnSelfDestructTimer"));

		ck::Trace("CueGym: Custom lifetime cue constructed - will self-destruct after 3s");
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DrawTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CueGym_Pmg::TryRetirePreviousShape(ck::ToEntity(this), ShapeHandle, _VisualDrawAttempted)) { return; }
		_VisualDrawAttempted = true;
		ShapeHandle = CueGym_Pmg::DrawSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 100.0f),
			FLinearColor(1.0f, 0.5f, 0.0f, 0.3f), 20.0f);
	}

	UFUNCTION()
	private void OnSelfDestructTimer(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		ck::Trace("CueGym: Custom lifetime cue self-destructing");
		utils_entity_lifetime::Request_DestroyEntity(ck::ToEntity(this));
	}
}

//============================================================================
// CONCURRENCY CUES
//============================================================================

// AllowMultiple: Multiple instances can run simultaneously
class UCk_CueGym_Cue_Multiple : UCk_GenericCue_EntityScript
{
	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	default _Replication = ECk_Replication::DoesNotReplicate;
	default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::Timed;
	default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::AllowMultiple;
	default _LifetimeDuration = FCk_Time(10.0f);

	float ElapsedTime = 0.0f;
	float LifetimeSeconds = 10.0f;
	FCk_Handle_Pmg_DebugShape ShapeHandle;
	private bool _VisualDrawAttempted = false;

	// An invalid receipt after this flag is set is a failed draw, not startup.
	UFUNCTION(BlueprintPure)
	bool Get_HasVisualDrawAttempted() const
	{
		return _VisualDrawAttempted;
	}

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_VisualShape() const
	{
		return FCk_Handle(ShapeHandle);
	}

	UFUNCTION(BlueprintOverride)
	FGameplayTag Get_CueName() const
	{
		return GameplayTags::ResolveGameplayTag(n"CueGym.Concurrency.Multiple");
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_CueGym_Cue_Multiple");

		// Tick timer for animated sphere
		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DrawTick"));

		ck::Trace("CueGym: AllowMultiple cue spawned");
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DrawTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CueGym_Pmg::TryRetirePreviousShape(ck::ToEntity(this), ShapeHandle, _VisualDrawAttempted)) { return; }
		_VisualDrawAttempted = true;
		ElapsedTime = ElapsedTime + float(InDeltaT.Get_Seconds());
		auto Progress = Math::Clamp(ElapsedTime / LifetimeSeconds, 0.0, 1.0);

		// Sphere shrinks from 25 -> 5 over lifetime
		auto Radius = float32(Math::Lerp(25.0, 5.0, Progress));

		// Color fades from bright green to dim red over lifetime
		auto Green = float32(1.0 - Progress);
		auto Red = float32(Progress);

		ShapeHandle = CueGym_Pmg::DrawSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 100.0f),
			FLinearColor(Red, Green, 0.1f, 0.3f), Radius);
	}
}

// RestartExisting: New execution restarts any already-running instance
class UCk_CueGym_Cue_Restart : UCk_GenericCue_EntityScript
{
	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	default _Replication = ECk_Replication::DoesNotReplicate;
	default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::Timed;
	default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::RestartExisting;
	default _LifetimeDuration = FCk_Time(10.0f);

	int32 RestartCount = 0;
	float ElapsedTime = 0.0f;
	float LifetimeSeconds = 10.0f;
	FCk_Handle_Pmg_DebugShape ShapeHandle;
	private bool _VisualDrawAttempted = false;

	// An invalid receipt after this flag is set is a failed draw, not startup.
	UFUNCTION(BlueprintPure)
	bool Get_HasVisualDrawAttempted() const
	{
		return _VisualDrawAttempted;
	}

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_VisualShape() const
	{
		return FCk_Handle(ShapeHandle);
	}

	UFUNCTION(BlueprintOverride)
	FGameplayTag Get_CueName() const
	{
		return GameplayTags::ResolveGameplayTag(n"CueGym.Concurrency.Restart");
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_CueGym_Cue_Restart");

		// Tick timer for animated sphere
		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DrawTick"));

		ck::Trace("CueGym: RestartExisting cue spawned");
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void DoRestart(FCk_Handle InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		RestartCount++;
		ElapsedTime = 0.0f;
		ck::Trace(f"CueGym: RestartExisting cue restarted (count: {RestartCount})");
	}

	UFUNCTION()
	private void DrawTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CueGym_Pmg::TryRetirePreviousShape(ck::ToEntity(this), ShapeHandle, _VisualDrawAttempted)) { return; }
		_VisualDrawAttempted = true;
		ElapsedTime = ElapsedTime + float(InDeltaT.Get_Seconds());
		auto Progress = Math::Clamp(ElapsedTime / LifetimeSeconds, 0.0, 1.0);

		// Sphere shrinks from 25 -> 5 over lifetime, snaps back to 25 on restart
		auto Radius = float32(Math::Lerp(25.0, 5.0, Progress));

		// Color fades from bright red to dim over lifetime, snaps back on restart
		auto Brightness = float32(1.0 - Progress * 0.7);

		ShapeHandle = CueGym_Pmg::DrawSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 100.0f),
			FLinearColor(Brightness, 0.1f, 0.1f, 0.3f), Radius);
	}
}

//============================================================================
// OWNER VALIDATION CUES
//============================================================================

// SkipIfInvalid: Cue executes even if owner entity is invalid
class UCk_CueGym_Cue_OwnerSkip : UCk_GenericCue_EntityScript
{
	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	default _Replication = ECk_Replication::DoesNotReplicate;
	default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::Timed;
	default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::AllowMultiple;
	default _OwnerValidationPolicy = ECk_Cue_OwnerValidationPolicy::SkipIfInvalid;
	default _LifetimeDuration = FCk_Time(5.0f);

	FCk_Handle_Pmg_DebugShape ShapeHandle;
	private bool _VisualDrawAttempted = false;

	// An invalid receipt after this flag is set is a failed draw, not startup.
	UFUNCTION(BlueprintPure)
	bool Get_HasVisualDrawAttempted() const
	{
		return _VisualDrawAttempted;
	}

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_VisualShape() const
	{
		return FCk_Handle(ShapeHandle);
	}

	UFUNCTION(BlueprintOverride)
	FGameplayTag Get_CueName() const
	{
		return GameplayTags::ResolveGameplayTag(n"CueGym.Owner.SkipInvalid");
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);

		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DrawTick"));

		ck::Trace("CueGym: SkipIfInvalid cue executed");
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DrawTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CueGym_Pmg::TryRetirePreviousShape(ck::ToEntity(this), ShapeHandle, _VisualDrawAttempted)) { return; }
		_VisualDrawAttempted = true;
		ShapeHandle = CueGym_Pmg::DrawSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 100.0f),
			FLinearColor(0.2f, 0.8f, 0.8f, 0.3f), 15.0f);
	}
}

// RequireValid: Cue only executes if owner entity exists
class UCk_CueGym_Cue_OwnerRequire : UCk_GenericCue_EntityScript
{
	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	default _Replication = ECk_Replication::DoesNotReplicate;
	default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::Timed;
	default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::AllowMultiple;
	default _OwnerValidationPolicy = ECk_Cue_OwnerValidationPolicy::RequireValid;
	default _LifetimeDuration = FCk_Time(5.0f);

	FCk_Handle_Pmg_DebugShape ShapeHandle;
	private bool _VisualDrawAttempted = false;

	// An invalid receipt after this flag is set is a failed draw, not startup.
	UFUNCTION(BlueprintPure)
	bool Get_HasVisualDrawAttempted() const
	{
		return _VisualDrawAttempted;
	}

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_VisualShape() const
	{
		return FCk_Handle(ShapeHandle);
	}

	UFUNCTION(BlueprintOverride)
	FGameplayTag Get_CueName() const
	{
		return GameplayTags::ResolveGameplayTag(n"CueGym.Owner.RequireValid");
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);

		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DrawTick"));

		ck::Trace("CueGym: RequireValid cue executed");
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DrawTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CueGym_Pmg::TryRetirePreviousShape(ck::ToEntity(this), ShapeHandle, _VisualDrawAttempted)) { return; }
		_VisualDrawAttempted = true;
		ShapeHandle = CueGym_Pmg::DrawSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 100.0f),
			FLinearColor(0.8f, 0.8f, 0.2f, 0.3f), 15.0f);
	}
}

//============================================================================
// RESTART CUE
//============================================================================

// Restartable: Tests the Restart() method via RestartExisting concurrency
class UCk_CueGym_Cue_Restartable : UCk_GenericCue_EntityScript
{
	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	default _Replication = ECk_Replication::DoesNotReplicate;
	default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::Timed;
	default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::RestartExisting;
	default _LifetimeDuration = FCk_Time(8.0f);

	int32 RestartCount = 0;
	FCk_Handle_Pmg_DebugShape ShapeHandle;
	private bool _VisualDrawAttempted = false;

	// An invalid receipt after this flag is set is a failed draw, not startup.
	UFUNCTION(BlueprintPure)
	bool Get_HasVisualDrawAttempted() const
	{
		return _VisualDrawAttempted;
	}

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_VisualShape() const
	{
		return FCk_Handle(ShapeHandle);
	}

	UFUNCTION(BlueprintOverride)
	FGameplayTag Get_CueName() const
	{
		return GameplayTags::ResolveGameplayTag(n"CueGym.Restart.Restartable");
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_CueGym_Cue_Restartable");

		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DrawTick"));

		ck::Trace("CueGym: Restartable cue constructed");
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void DoRestart(FCk_Handle InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		RestartCount++;
		ck::Trace(f"CueGym: Restartable cue restarted (count: {RestartCount})");
	}

	UFUNCTION()
	private void DrawTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CueGym_Pmg::TryRetirePreviousShape(ck::ToEntity(this), ShapeHandle, _VisualDrawAttempted)) { return; }
		_VisualDrawAttempted = true;
		// Color cycles continuously based on restart count using sine waves
		auto Phase = float(RestartCount) * 0.8f;
		auto Red = float32(Math::Sin(Phase) * 0.5f + 0.5f);
		auto Green = float32(Math::Sin(Phase + 2.094f) * 0.5f + 0.5f);
		auto Blue = float32(Math::Sin(Phase + 4.189f) * 0.5f + 0.5f);

		ShapeHandle = CueGym_Pmg::DrawSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 100.0f),
			FLinearColor(Red, Green, Blue, 0.3f), 20.0f);
	}
}

//============================================================================
// TRANSIENT CUE
//============================================================================

// Transient: Executes without a persistent owner entity
class UCk_CueGym_Cue_Transient : UCk_GenericCue_EntityScript
{
	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	UPROPERTY(ExposeOnSpawn) int32 SourceGeneration = 0;
	UPROPERTY(ExposeOnSpawn) int32 RoleEpoch = 0;
	UPROPERTY(ExposeOnSpawn) int64 EventSerial = 0;
	private FCk_Handle _OwnedEntity;
	private FCk_Handle _RelayOwner;
	UPROPERTY() private TWeakObjectPtr<ACk_CueRelay_UE> _RelayActor;
	private FCk_Handle_Timer _DrawTimer;
	private ACk_CueGym_GameState _EventState;
	private bool _Recorded = false;
	private bool _VisualDrawAttempted = false;
	private double _WaitStartedAt = 0.0;

	default _Replication = ECk_Replication::DoesNotReplicate;
	default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::Timed;
	default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::AllowMultiple;
	default _OwnerValidationPolicy = ECk_Cue_OwnerValidationPolicy::RequireValid;
	default _LifetimeDuration = FCk_Time(5.0f);

	FCk_Handle_Pmg_DebugShape ShapeHandle;

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_VisualShape() const { return FCk_Handle(ShapeHandle); }
	UFUNCTION(BlueprintPure)
	bool Get_HasVisualDrawAttempted() const { return _VisualDrawAttempted; }

	UFUNCTION(BlueprintOverride)
	FGameplayTag Get_CueName() const
	{
		return GameplayTags::ResolveGameplayTag(n"CueGym.Transient.NoOwner");
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		_OwnedEntity = InHandle;
		_WaitStartedAt = System::GetGameTimeInSeconds();
		if (!InitialTransform.IsValid() || InitialTransform.ContainsNaN())
		{ utils_entity_lifetime::Request_DestroyEntity(InHandle); return ECk_EntityScript_ConstructionFlow::Finished; }
		auto TransformReceipt = utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		if (ck::Is_NOT_Valid(TransformReceipt))
		{ utils_entity_lifetime::Request_DestroyEntity(InHandle); return ECk_EntityScript_ConstructionFlow::Finished; }
		auto TagReceipt = utils_entity_tag::Add(InHandle, n"TAG_CueGym_Cue_Transient");
		if (ck::Is_NOT_Valid(TagReceipt))
		{ utils_entity_lifetime::Request_DestroyEntity(InHandle); return ECk_EntityScript_ConstructionFlow::Finished; }

		_DrawTimer = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DrawTick"));
		if (ck::Is_NOT_Valid(_DrawTimer))
		{ utils_entity_lifetime::Request_DestroyEntity(InHandle); return ECk_EntityScript_ConstructionFlow::Finished; }

		ck::Trace("CueGym: Transient cue executed (attached to relay channel owner)");
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DrawTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != _DrawTimer || ck::Is_NOT_Valid(_OwnedEntity) ||
			_OwnedEntity != ck::ToEntity(this)) { return; }
		const bool IsGymEvent = SourceGeneration != 0 || RoleEpoch != 0 || EventSerial != 0;
		if (IsGymEvent)
		{
			auto World = utils_entity_lifetime::Get_WorldForEntity(_OwnedEntity);
			if (ck::Is_NOT_Valid(World)) { return; }
			auto State = Cast<ACk_CueGym_GameState>(World.GetGameState());
			if (ck::Is_NOT_Valid(State))
			{
				// A relay may arrive before the local GameState is available.
				if (System::GetGameTimeInSeconds() - _WaitStartedAt > 20.0)
				{ utils_entity_lifetime::Request_DestroyEntity(_OwnedEntity); }
				return;
			}
			if (_Recorded && (State != _EventState ||
				!utils_entity_lifetime::Has_LifetimeOwner(_OwnedEntity) ||
				utils_entity_lifetime::Get_LifetimeOwner(_OwnedEntity) != _RelayOwner ||
				ck::Is_NOT_Valid(_RelayActor.Get()) ||
				_RelayActor.Get().IsActorBeingDestroyed() ||
				_RelayActor.Get().GetWorld() != World ||
				utils_owning_actor::TryGet_ActorEntityHandle(_RelayActor.Get()) != _RelayOwner))
			{ utils_entity_lifetime::Request_DestroyEntity(_OwnedEntity); return; }
			auto Admission = State.Get_TransientAdmission(SourceGeneration, RoleEpoch, EventSerial);
			if (Admission == ECkCueGym_EventAdmission::Pending)
			{
				if (System::GetGameTimeInSeconds() - _WaitStartedAt > 20.0)
				{ utils_entity_lifetime::Request_DestroyEntity(_OwnedEntity); }
				return;
			}
			if (Admission != ECkCueGym_EventAdmission::Accepted)
			{ utils_entity_lifetime::Request_DestroyEntity(_OwnedEntity); return; }
			if (!_Recorded)
			{
				if (!State.TryRegisterTransient(this, SourceGeneration, RoleEpoch, EventSerial, _OwnedEntity))
				{ utils_entity_lifetime::Request_DestroyEntity(_OwnedEntity); return; }
				_RelayOwner = utils_entity_lifetime::Get_LifetimeOwner(_OwnedEntity);
				_RelayActor = Cast<ACk_CueRelay_UE>(utils_owning_actor::TryGet_EntityOwningActor(_RelayOwner));
				_EventState = State;
				_Recorded = true;
			}
		}
		if (!CueGym_Pmg::TryRetirePreviousShape(ck::ToEntity(this), ShapeHandle, _VisualDrawAttempted)) { return; }
		_VisualDrawAttempted = true;
		ShapeHandle = CueGym_Pmg::DrawSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 100.0f),
			FLinearColor(0.8f, 0.4f, 0.8f, 0.3f), 15.0f);
	}
}

//============================================================================
// OWNER DESTRUCTION CUE
//============================================================================

// OwnerDestruction: Persistent cue that relies on owner destruction to be cleaned up
class UCk_CueGym_Cue_OwnerDestruction : UCk_GenericCue_EntityScript
{
	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	default _Replication = ECk_Replication::DoesNotReplicate;
	default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::Persistent;
	default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::AllowMultiple;
	default _OwnerValidationPolicy = ECk_Cue_OwnerValidationPolicy::RequireValid;

	FCk_Handle_Pmg_DebugShape ShapeHandle;
	private bool _VisualDrawAttempted = false;

	// An invalid receipt after this flag is set is a failed draw, not startup.
	UFUNCTION(BlueprintPure)
	bool Get_HasVisualDrawAttempted() const
	{
		return _VisualDrawAttempted;
	}

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_VisualShape() const
	{
		return FCk_Handle(ShapeHandle);
	}

	UFUNCTION(BlueprintOverride)
	FGameplayTag Get_CueName() const
	{
		return GameplayTags::ResolveGameplayTag(n"CueGym.OwnerDestruction.Attached");
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_CueGym_Cue_OwnerDestruction");

		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DrawTick"));

		ck::Trace("CueGym: OwnerDestruction cue spawned (persistent, will die with owner)");
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DrawTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CueGym_Pmg::TryRetirePreviousShape(ck::ToEntity(this), ShapeHandle, _VisualDrawAttempted)) { return; }
		_VisualDrawAttempted = true;
		ShapeHandle = CueGym_Pmg::DrawSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 100.0f),
			FLinearColor(0.3f, 0.7f, 1.0f, 0.3f), 15.0f);
	}
}
