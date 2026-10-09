// Opt-in structural cohort for GymStation EntityScripts. This value belongs to one
// authoritative GameState or one owning client's local projection; it is never
// replicated. The caller owns timers, catalog identity, snapshots and presentation.

UENUM()
enum ECkGym_SharedStationBegin
{
    Started,
    Busy,
    Rejected
}

UENUM()
enum ECkGym_SharedStationPoll
{
    Pending,
    Ready,
    Failed
}

USTRUCT()
struct FCkGym_SharedStationCohort
{
    // All fields are reflected because a caller may retain this value on a UObject.
    // No field is marked Replicated: handles and pending requests are world-local.
    UPROPERTY() private FCk_Handle World;
    UPROPERTY() private FCk_Handle AdmissionOwner;
    UPROPERTY() private int32 AdmissionTicket = 0;
    UPROPERTY() private FCk_Handle CohortOwner;
    UPROPERTY() private TArray<FCk_GymStation_SpawnParams> Recipes;
    UPROPERTY() TArray<FCk_Handle_PendingEntityScript> PendingStations;
    UPROPERTY() TArray<FCk_Handle> Stations;
    UPROPERTY() TArray<int32> Constructed;
    UPROPERTY() TArray<int32> Completed;
    UPROPERTY() private TArray<FCk_Handle> KnownAllocated;
    UPROPERTY() private TArray<FCk_Handle> KnownParents;
    UPROPERTY() private TArray<FCk_Handle> Retiring;
    UPROPERTY() private TArray<FCk_Handle> RequiredHandles;
    UPROPERTY() private TArray<FCk_Handle> RequiredParents;
    UPROPERTY() private int32 Phase = 0; // 0 idle, 1 constructing, 2 ready, 3 retiring.
    UPROPERTY() private bool TerminalFailure = false;
    UPROPERTY() private bool OwnsAdmission = false;
    UPROPERTY() private bool RetirementUnproven = false; // Caller must bound this terminal barrier; never means empty.
    UPROPERTY() private FString LastConstructedReject;

    int32 Get_Phase() const { return Phase; }
    bool Get_HasTerminalFailure() const { return TerminalFailure; }
    bool Get_IsRetirementUnproven() const { return RetirementUnproven; }
    FString Get_LastConstructedReject() const { return LastConstructedReject; }

    // The caller supplies real UObject delegate wrappers. Completion is only
    // request-drained; Promise_OnConstructed is the construction-finished path.
    ECkGym_SharedStationBegin TryBegin(
        FCk_Handle InWorld,
        const TArray<FCk_GymStation_SpawnParams>&in InRecipes,
        FCk_Delegate_Request_OnCompleted InCompletion,
        FCk_Delegate_EntityScript_Constructed InConstructed)
    {
        if (TerminalFailure) { return ECkGym_SharedStationBegin::Rejected; }
        if (Phase != 0) { return ECkGym_SharedStationBegin::Busy; }
        if (!Get_IsPristineIdle())
        {
            TerminalFailure = true; RetirementUnproven = true;
            CkGym_Admission::TryCloseWorldUnproven(InWorld);
            return ECkGym_SharedStationBegin::Rejected;
        }
        if (!InCompletion.IsBound() || !InConstructed.IsBound() ||
            !Get_IsLive(InWorld) || InWorld != ck::TransientEntity() ||
            !utils_entity_lifetime::Get_CanCreateEntity(InWorld) ||
            InRecipes.Num() == 0) { return ECkGym_SharedStationBegin::Rejected; }

        TArray<FName> AllTags;
        for (auto Recipe : InRecipes)
        {
            if (!Get_IsValidRecipe(Recipe)) { return ECkGym_SharedStationBegin::Rejected; }
            for (auto Tag : Recipe.StationTags)
            {
                if (AllTags.Contains(Tag) ||
                    utils_entity_tag::ForEach_Entity(InWorld, Tag).Num() != 0)
                { return ECkGym_SharedStationBegin::Rejected; }
                AllTags.Add(Tag);
            }
        }

        if (CkGym_Admission::Get_IsClosedUnproven(InWorld))
        { return ECkGym_SharedStationBegin::Rejected; }
        if (CkGym_Admission::Get_IsBusy(InWorld))
        { return ECkGym_SharedStationBegin::Busy; }

        // Reserve before the first fallible allocation. A failed return retains
        // its exact raw identity and closes this world without destroying children.
        AdmissionTicket = CkGym_Admission::TryReserve(InWorld);
        if (AdmissionTicket == 0) { return ECkGym_SharedStationBegin::Rejected; }
        World = InWorld;
        Recipes = InRecipes;
        AdmissionOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!Get_IsLive(AdmissionOwner))
        { FailClosed(); return ECkGym_SharedStationBegin::Rejected; }
        if (!Remember(AdmissionOwner, World))
        { FailClosed(); return ECkGym_SharedStationBegin::Rejected; }
        if (!CkGym_Admission::TryCommitOwner(World, AdmissionTicket, AdmissionOwner))
        { FailClosed(); return ECkGym_SharedStationBegin::Rejected; }
        OwnsAdmission = true;

        CohortOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!Get_IsLive(CohortOwner))
        { FailClosed(); return ECkGym_SharedStationBegin::Rejected; }
        if (!Remember(CohortOwner, World))
        { FailAndRetire(); return ECkGym_SharedStationBegin::Rejected; }
        Phase = 1;
        // Allocate all parallel slots before any Promise can synchronously replay a callback.
        for (int32 i = 0; i < Recipes.Num(); i++)
        { PendingStations.Add(FCk_Handle_PendingEntityScript()); Stations.Add(FCk_Handle());
          Constructed.Add(0); Completed.Add(0); }
        for (int32 i = 0; i < Recipes.Num(); i++)
        {
            auto Recipe = Recipes[i];
            auto Params = FInstancedStruct::Make(Recipe);
            // This was preflighted before admission; repeat before each request
            // because a malformed copy must close the world before another admission.
            if (!Params.IsValid() || Params.GetScriptStruct() != FCk_GymStation_SpawnParams)
            { FailClosed(); return ECkGym_SharedStationBegin::Rejected; }
            auto Owner = CohortOwner;
            auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                Owner, UCk_EntityScript_GymStation, Params, InCompletion);
            // Preserve the first returned pending identity even when it is rejected.
            PendingStations[i] = Pending;
            if (Phase != 1 || TerminalFailure ||
                CkGym_Admission::Get_IsClosedUnproven(World) ||
                !utils_pending_entity_script::Get_IsValid(Pending))
            { FailClosed(); return ECkGym_SharedStationBegin::Rejected; }
            if (!Remember(Pending.Get_EntityUnderConstruction(), CohortOwner))
            { FailAndRetire(); return ECkGym_SharedStationBegin::Rejected; }
            utils_pending_entity_script::Promise_OnConstructed(Pending, InConstructed);
            if (Phase != 1 || TerminalFailure)
            { return ECkGym_SharedStationBegin::Rejected; }
        }
        return ECkGym_SharedStationBegin::Started;
    }

    // The callback's exact entity-under-construction handle is required. A
    // duplicate, foreign, failed or wrong-owner callback faults the whole run.
    bool TryRecord_Constructed(FCk_Handle InPendingEntity,
        FCk_Handle_EntityScript InConstructedScript)
    {
        LastConstructedReject = "";
        if (Phase != 1)
        { LastConstructedReject = f"phase={Phase}, expected constructing phase 1"; FailAndRetire(); return false; }
        if (!Get_HasParallelArrays())
        { LastConstructedReject = f"parallel arrays invalid: recipes={Recipes.Num()}, pending={PendingStations.Num()}, stations={Stations.Num()}, constructed={Constructed.Num()}, completed={Completed.Num()}"; FailAndRetire(); return false; }
        if (!Get_HasRunnableOwnerContext())
        { LastConstructedReject = "runnable world/cohort owner context invalid"; FailAndRetire(); return false; }
        auto Entity = FCk_Handle(InConstructedScript);
        int32 Index = FindPending(InPendingEntity);
        if (Index < 0 || Index >= Stations.Num() || Index >= Constructed.Num() || Index >= Completed.Num())
        { LastConstructedReject = f"pending target not found or index out of bounds: index={Index}"; FailAndRetire(); return false; }
        if (Constructed[Index] != 0 || !KnownAllocated.Contains(InPendingEntity) || Entity != InPendingEntity)
        { LastConstructedReject = f"callback identity invalid: slot={Index}, priorConstructed={Constructed[Index]}, known={KnownAllocated.Contains(InPendingEntity)}, entityMatchesPending={Entity == InPendingEntity}"; FailAndRetire(); return false; }
        // DoConstruct queues tag Adds; OnConstructed runs before their request processor.
        // Validate the constructed body now and require exact applied tags before Ready.
        if (!Get_IsExactStation(Entity, Index, false))
        { LastConstructedReject = f"exact station slot {Index}: {DescribeExactStationReject(Entity, Index, false)}"; FailAndRetire(); return false; }
        Stations[Index] = Entity;
        Constructed[Index] = 1;
        if (!Remember(Entity, CohortOwner)) { FailAndRetire(); return false; }
        return true;
    }

    bool TryRecord_Completed(FCk_Handle InPendingEntity,
        FCk_Handle InCompletedEntity, ECk_Request_OperationResult InResult)
    {
        if (Phase != 1 || !Get_HasParallelArrays() || !Get_HasRunnableOwnerContext())
        { FailAndRetire(); return false; }
        int32 Index = FindPending(InPendingEntity);
        if (Index < 0 || Index >= Completed.Num() || Index >= Constructed.Num() ||
            Index >= Stations.Num() || Completed[Index] != 0 ||
            !KnownAllocated.Contains(InPendingEntity) || InCompletedEntity != InPendingEntity ||
            InResult != ECk_Request_OperationResult::Succeeded || !Get_IsLive(InCompletedEntity) ||
            !utils_entity_lifetime::Has_LifetimeOwner(InCompletedEntity) ||
            utils_entity_lifetime::Get_LifetimeOwner(InCompletedEntity) != CohortOwner)
        { FailAndRetire(); return false; }
        Completed[Index] = 1;
        return true;
    }

    ECkGym_SharedStationPoll PollConstruction()
    {
        if (TerminalFailure) { return ECkGym_SharedStationPoll::Failed; }
        if (CkGym_Admission::Get_IsClosedUnproven(World))
        { FailClosed(); return ECkGym_SharedStationPoll::Failed; }
        if (Phase == 2)
        {
            if (!Get_HasExactStations(true)) { FailAndRetire(); return ECkGym_SharedStationPoll::Failed; }
            return ECkGym_SharedStationPoll::Ready;
        }
        if (Phase != 1) { return ECkGym_SharedStationPoll::Failed; }
        if (!Get_IsLive(World) || World != ck::TransientEntity() || !Get_IsLive(CohortOwner) ||
            !Get_OwnsAdmission() || !Get_HasParallelArrays() || !RefreshClosure(true) ||
            !Get_HasRequiredLive())
        { FailAndRetire(); return ECkGym_SharedStationPoll::Failed; }
        for (int32 i = 0; i < Recipes.Num(); i++)
        {
            if (!utils_pending_entity_script::Get_IsValid(PendingStations[i]) ||
                !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                    PendingStations[i].Get_EntityUnderConstruction()))
            { FailAndRetire(); return ECkGym_SharedStationPoll::Failed; }
            if ((Constructed[i] != 0 && Constructed[i] != 1) ||
                (Completed[i] != 0 && Completed[i] != 1))
            { FailAndRetire(); return ECkGym_SharedStationPoll::Failed; }
            if (Constructed[i] == 0 || Completed[i] == 0)
            { return ECkGym_SharedStationPoll::Pending; }
        }
        if (!Get_HasExactStations(false))
        { FailAndRetire(); return ECkGym_SharedStationPoll::Failed; }
        bool TagsPending = false;
        for (int32 i = 0; i < Recipes.Num(); ++i)
        {
            for (auto Tag : Recipes[i].StationTags)
            {
                auto Matches = utils_entity_tag::ForEach_Entity(World, Tag);
                if (Matches.Num() == 0) { TagsPending = true; continue; }
                if (Matches.Num() != 1 || Matches[0] != Stations[i])
                { FailAndRetire(); return ECkGym_SharedStationPoll::Failed; }
            }
        }
        if (TagsPending) { return ECkGym_SharedStationPoll::Pending; }
        if (!Get_HasExactStations(true))
        { FailAndRetire(); return ECkGym_SharedStationPoll::Failed; }
        if (!ReleaseAdmission()) { FailAndRetire(); return ECkGym_SharedStationPoll::Failed; }
        Phase = 2;
        return ECkGym_SharedStationPoll::Ready;
    }

    TArray<FCk_Handle> Get_Stations()
    {
        if (TerminalFailure || Phase != 2) { return TArray<FCk_Handle>(); }
        if (!Get_HasExactStations(true)) { FailAndRetire(); return TArray<FCk_Handle>(); }
        return Stations;
    }

    // The family caller may require a persistent owned descendant in addition to the
    // structural station roots. This never enrolls a foreign or previously unseen handle.
    bool TryRequireOwned(FCk_Handle InHandle, FCk_Handle InExpectedParent)
    {
        if ((Phase != 1 && Phase != 2) || TerminalFailure ||
            !Get_HasRunnableOwnerContext() || !RefreshClosure(true))
        { FailAndRetire(); return false; }
        int32 Index = KnownAllocated.FindIndex(InHandle);
        if (Index < 0 || Index >= KnownParents.Num() || !Get_IsLive(InHandle) ||
            !Get_IsLive(InExpectedParent) || KnownParents[Index] != InExpectedParent ||
            !utils_entity_lifetime::Has_LifetimeOwner(InHandle) ||
            utils_entity_lifetime::Get_LifetimeOwner(InHandle) != InExpectedParent)
        { FailAndRetire(); return false; }
        if (RequiredHandles.Num() != RequiredParents.Num())
        { FailAndRetire(); return false; }
        int32 RequiredIndex = RequiredHandles.FindIndex(InHandle);
        if (RequiredIndex >= 0)
        {
            if (RequiredIndex < RequiredParents.Num() && RequiredParents[RequiredIndex] == InExpectedParent)
            { return true; }
            FailAndRetire(); return false;
        }
        RequiredHandles.Add(InHandle); RequiredParents.Add(InExpectedParent);
        return true;
    }

    // Capture before ANY destroy request. Include every remembered handle and
    // every presently enumerable descendant. Pending-kill handles remain in
    // Retiring even though ordinary ck::IsValid is already false.
    void Request_Retire()
    {
        if (Phase == 3) { return; }
        if (Get_IsPristineIdle()) { return; }
        if (Phase != 2 || TerminalFailure || RetirementUnproven ||
            CkGym_Admission::Get_IsClosedUnproven(World))
        { FailClosed(); return; }
        // Refresh trusted closure before any destruction. A failed allocated census is unproven,
        // never an empty closure. Foreign callback payloads are never enrolled or retired.
        if ((UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(CohortOwner) && !KnownAllocated.Contains(CohortOwner)) ||
            (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(AdmissionOwner) && !KnownAllocated.Contains(AdmissionOwner)))
        { RetirementUnproven = true; }
        bool MembershipArraysExact = KnownAllocated.Num() == KnownParents.Num() &&
            RequiredHandles.Num() == RequiredParents.Num();
        if (!MembershipArraysExact || !RefreshClosure(false)) { RetirementUnproven = true; }
        for (auto Known : KnownAllocated)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Known) && !Retiring.Contains(Known))
          { Retiring.Add(Known); } }
        Phase = 3;
        // Freeze the complete trusted ledger before the first destruction request.
        // Never refresh/adopt membership after a synchronous destruction callback.
        if (RetirementUnproven || !Get_HasSafeRetirementLedger())
        { FailClosed(); return; }
        for (;;)
        {
            bool HasLive = false;
            bool Progress = false;
            for (auto Old : Retiring)
            {
                if (!Get_IsLive(Old)) { continue; }
                HasLive = true;
                // Earlier BeginDestroy callbacks can change a later target's direct children.
                if (!Get_HasSafeRetirementLedger())
                { FailClosed(); return; }
                TArray<FCk_Handle> Children;
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(Old, World, Children))
                { FailClosed(); return; }
                bool ChildrenPending = true;
                for (auto Child : Children)
                {
                    if (!KnownAllocated.Contains(Child) || !Retiring.Contains(Child))
                    { FailClosed(); return; }
                    if (Get_IsLive(Child)) { ChildrenPending = false; }
                }
                // This is the actual native destroy read-set. A transferred stale entry
                // must also be already pending before native recursion can safely run.
                for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Old))
                {
                    if (!KnownAllocated.Contains(Child) || !Retiring.Contains(Child))
                    { FailClosed(); return; }
                    if (Get_IsLive(Child)) { ChildrenPending = false; }
                }
                if (!ChildrenPending) { continue; }
                // All allocated dependents are already pending/deallocated; recursive
                // native requests cannot start another live child destruction callback.
                utils_entity_lifetime::Request_DestroyEntity(Old);
                if (Get_IsLive(Old) || RetirementUnproven || !Get_HasSafeRetirementLedger())
                { FailClosed(); return; }
                Progress = true;
            }
            if (!HasLive) { break; }
            // A live cycle or rejected/partial closure cannot be declared retired.
            if (!Progress)
            { FailClosed(); return; }
        }
        // Retain the complete provenance and required sets until retirement is proven.
    }

    bool PollRetirement()
    {
        if (Phase != 3 || RetirementUnproven) { return false; }
        if (!Get_HasSafeRetirementLedger())
        { FailClosed(); return false; }
        for (auto Old : Retiring)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Old))
          { return false; } }
        if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(AdmissionOwner))
        { return false; }
        if (!ReleaseAdmission()) { return false; }
        Retiring.Empty();
        KnownAllocated.Empty(); KnownParents.Empty();
        RequiredHandles.Empty(); RequiredParents.Empty();
        PendingStations.Empty(); Stations.Empty();
        Constructed.Empty(); Completed.Empty(); Recipes.Empty();
        CohortOwner = FCk_Handle();
        AdmissionOwner = FCk_Handle();
        World = FCk_Handle();
        Phase = 0;
        return true;
    }

    // Read-only retirement proof; no membership enrollment and no lifetime mutation.
    private bool Get_HasSafeRetirementLedger()
    {
        if (RetirementUnproven || CkGym_Admission::Get_IsClosedUnproven(World) ||
            KnownAllocated.Num() != KnownParents.Num() ||
            RequiredHandles.Num() != RequiredParents.Num()) { return false; }
        // Preserve the full per-target and post-callback proof, using native hash-set
        // membership to avoid repeated interpreted linear scans of the frozen ledger.
        return UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
            KnownAllocated, Retiring, World);
    }

    private bool Get_IsLive(FCk_Handle InHandle)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InHandle) && ck::IsValid(InHandle) &&
        !utils_entity_lifetime::Get_IsPendingDestroy(InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }

    private bool Get_IsPristineIdle()
    {
        return Phase == 0 && !OwnsAdmission && AdmissionTicket == 0 && !RetirementUnproven &&
            World == FCk_Handle() && AdmissionOwner == FCk_Handle() &&
            CohortOwner == FCk_Handle() && Recipes.Num() == 0 &&
            PendingStations.Num() == 0 && Stations.Num() == 0 &&
            Constructed.Num() == 0 && Completed.Num() == 0 &&
            KnownAllocated.Num() == 0 && KnownParents.Num() == 0 &&
            Retiring.Num() == 0 && RequiredHandles.Num() == 0 && RequiredParents.Num() == 0;
    }

    private bool Get_IsValidRecipe(FCk_GymStation_SpawnParams InRecipe)
    {
        if (InRecipe.StationTags.Num() == 0 || !InRecipe.InitialTransform.IsValid() ||
            InRecipe.InitialTransform.ContainsNaN() ||
            !Math::IsFinite(InRecipe.Width) || InRecipe.Width <= 0.0 ||
            !Math::IsFinite(InRecipe.Depth) || InRecipe.Depth <= 0.0 ||
            !Math::IsFinite(InRecipe.Height) || InRecipe.Height <= 0.0 ||
            !Math::IsFinite(InRecipe.WallThickness) || InRecipe.WallThickness <= 0.0 ||
            !Math::IsFinite(InRecipe.FloorThickness) || InRecipe.FloorThickness <= 0.0 ||
            !Math::IsFinite(InRecipe.TrimDepth) || InRecipe.TrimDepth < 0.0 ||
            !Math::IsFinite(InRecipe.AgentSpawnOffset) || InRecipe.AgentSpawnOffset < 0.0 ||
            !Math::IsFinite(InRecipe.TitleScale) || InRecipe.TitleScale <= 0.0 ||
            !Math::IsFinite(InRecipe.DescriptionScale) || InRecipe.DescriptionScale <= 0.0 ||
            !Math::IsFinite(InRecipe.FloorDescriptionScale) || InRecipe.FloorDescriptionScale <= 0.0 ||
            !Math::IsFinite(InRecipe.BodyColour.R) || !Math::IsFinite(InRecipe.BodyColour.G) ||
            !Math::IsFinite(InRecipe.BodyColour.B) || !Math::IsFinite(InRecipe.BodyColour.A) ||
            !Math::IsFinite(InRecipe.TrimColour.R) || !Math::IsFinite(InRecipe.TrimColour.G) ||
            !Math::IsFinite(InRecipe.TrimColour.B) || !Math::IsFinite(InRecipe.TrimColour.A))
        { return false; }
        auto Position = InRecipe.InitialTransform.GetTranslation();
        auto Scale = InRecipe.InitialTransform.GetScale3D();
        auto Rotation = InRecipe.InitialTransform.GetRotation();
        if (!Math::IsFinite(Position.X) || !Math::IsFinite(Position.Y) || !Math::IsFinite(Position.Z) ||
            !Math::IsFinite(Scale.X) || !Math::IsFinite(Scale.Y) || !Math::IsFinite(Scale.Z) ||
            Scale.X <= 0.0 || Scale.Y <= 0.0 || Scale.Z <= 0.0 ||
            !Math::IsFinite(Rotation.X) || !Math::IsFinite(Rotation.Y) ||
            !Math::IsFinite(Rotation.Z) || !Math::IsFinite(Rotation.W) ||
            !Rotation.IsNormalized() || Math::Abs(Rotation.SizeSquared() - 1.0) > 1.0e-12)
        { return false; }
        TArray<FName> LocalTags;
        for (auto Tag : InRecipe.StationTags)
        { if (Tag == FName() || LocalTags.Contains(Tag)) { return false; }
          LocalTags.Add(Tag); }
        auto Params = FInstancedStruct::Make(InRecipe);
        return Params.IsValid() && Params.GetScriptStruct() == FCk_GymStation_SpawnParams;
    }

    private int32 FindPending(FCk_Handle InEntity)
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InEntity))
        { return -1; }
        for (int32 i = 0; i < PendingStations.Num(); i++)
        { if (PendingStations[i].Get_EntityUnderConstruction() == InEntity) { return i; } }
        return -1;
    }

    private bool Get_IsExactStation(FCk_Handle InStation, int32 InIndex, bool InRequireAppliedTags)
    {
        if (InIndex < 0 || InIndex >= Recipes.Num() || !Get_IsLive(World) ||
            !Get_HasRunnableOwnerContext() || !Get_IsLive(InStation) ||
            !utils_entity_lifetime::Has_LifetimeOwner(InStation) ||
            utils_entity_lifetime::Get_LifetimeOwner(InStation) != CohortOwner)
        { return false; }
        auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(InStation);
        if (ck::Is_NOT_Valid(Script) || Script.GetClass() != UCk_EntityScript_GymStation ||
            !InStation.Has_Fragment(FCkGym_Station_Anchors))
        { return false; }
        auto StationScript = Cast<UCk_EntityScript_GymStation>(Script);
        auto Recipe = Recipes[InIndex];
        // IsValid inspects raw members without FTransform getter diagnostics. A malformed
        // script or recipe must not be sanitized while its rejection is being checked.
        if (!StationScript.InitialTransform.IsValid() || !Recipe.InitialTransform.IsValid())
        { return false; }
        if (StationScript.InitialTransform.GetTranslation() != Recipe.InitialTransform.GetTranslation() ||
            StationScript.InitialTransform.GetRotation() != Recipe.InitialTransform.GetRotation() ||
            StationScript.InitialTransform.GetScale3D() != Recipe.InitialTransform.GetScale3D()) { return false; }
        // AutoSize and later display refits legitimately change dimensions; the
        // current script fields determine the anchors actually built by GymStation.
        if (!Math::IsFinite(StationScript.Width) || StationScript.Width <= 0.0 ||
            !Math::IsFinite(StationScript.Depth) || StationScript.Depth <= 0.0 ||
            !Math::IsFinite(StationScript.Height) || StationScript.Height <= 0.0 ||
            !Math::IsFinite(StationScript.AgentSpawnOffset) || StationScript.AgentSpawnOffset < 0.0)
        { return false; }
        auto Anchors = InStation.Get_Fragment(FCkGym_Station_Anchors);
        auto T = Recipe.InitialTransform;
        double W = StationScript.Width * 100.0;
        double D = StationScript.Depth * 100.0;
        double H = StationScript.Height * 100.0;
        double SpawnOffset = StationScript.AgentSpawnOffset;
        if (Anchors.FootprintCenter != T.TransformPosition(FVector(0.0, 0.0, 0.0)) ||
            Anchors.AgentSpawnFront != T.TransformPosition(FVector(D * 0.5 + SpawnOffset, 0.0, 0.0)) ||
            Anchors.AgentSpawnBack != T.TransformPosition(FVector(-D * 0.5 - SpawnOffset, 0.0, 0.0)) ||
            Anchors.AgentSpawnLeft != T.TransformPosition(FVector(0.0, W * 0.5 + SpawnOffset, 0.0)) ||
            Anchors.AgentSpawnRight != T.TransformPosition(FVector(0.0, -W * 0.5 - SpawnOffset, 0.0)) ||
            Anchors.PanelTopFront != T.TransformPosition(FVector(D * 0.5, 0.0, H)) ||
            Anchors.PanelCenter != T.TransformPosition(FVector(0.0, 0.0, H * 0.5))) { return false; }
        if (InRequireAppliedTags)
        {
            for (auto Tag : Recipes[InIndex].StationTags)
            { auto Matches = utils_entity_tag::ForEach_Entity(World, Tag);
              if (Matches.Num() != 1 || Matches[0] != InStation) { return false; } }
        }
        return true;
    }

    // Failure-only, side-effect-free explanation of the unchanged exact station gate.
    // Every object/fragment read follows the same sequential prerequisite checks.
    private FString DescribeExactStationReject(FCk_Handle InStation, int32 InIndex, bool InRequireAppliedTags)
    {
        if (InIndex < 0 || InIndex >= Recipes.Num()) { return "recipe index unavailable"; }
        if (!Get_IsLive(World) || !Get_HasRunnableOwnerContext()) { return "world or cohort owner context invalid"; }
        if (!Get_IsLive(InStation)) { return "station not live"; }
        if (!utils_entity_lifetime::Has_LifetimeOwner(InStation) ||
            utils_entity_lifetime::Get_LifetimeOwner(InStation) != CohortOwner)
        { return "station lifetime owner differs from cohort owner"; }
        auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(InStation);
        if (ck::Is_NOT_Valid(Script) || Script.GetClass() != UCk_EntityScript_GymStation)
        { return "station script missing or wrong class"; }
        if (!InStation.Has_Fragment(FCkGym_Station_Anchors)) { return "station anchors fragment missing"; }
        auto StationScript = Cast<UCk_EntityScript_GymStation>(Script);
        auto Recipe = Recipes[InIndex];
        if (!StationScript.InitialTransform.IsValid() || !Recipe.InitialTransform.IsValid())
        { return "script or recipe transform invalid"; }
        bool TranslationEqual = StationScript.InitialTransform.GetTranslation() == Recipe.InitialTransform.GetTranslation();
        bool RotationEqual = StationScript.InitialTransform.GetRotation() == Recipe.InitialTransform.GetRotation();
        bool ScaleEqual = StationScript.InitialTransform.GetScale3D() == Recipe.InitialTransform.GetScale3D();
        if (!TranslationEqual || !RotationEqual || !ScaleEqual)
        { return f"exact transform mismatch: translation={TranslationEqual}, rotation={RotationEqual}, scale={ScaleEqual}"; }
        if (!Math::IsFinite(StationScript.Width) || StationScript.Width <= 0.0 ||
            !Math::IsFinite(StationScript.Depth) || StationScript.Depth <= 0.0 ||
            !Math::IsFinite(StationScript.Height) || StationScript.Height <= 0.0 ||
            !Math::IsFinite(StationScript.AgentSpawnOffset) || StationScript.AgentSpawnOffset < 0.0)
        { return f"script dimensions invalid: width={StationScript.Width}, depth={StationScript.Depth}, height={StationScript.Height}, offset={StationScript.AgentSpawnOffset}"; }
        auto Anchors = InStation.Get_Fragment(FCkGym_Station_Anchors);
        auto T = Recipe.InitialTransform;
        double W = StationScript.Width * 100.0;
        double D = StationScript.Depth * 100.0;
        double H = StationScript.Height * 100.0;
        double Offset = StationScript.AgentSpawnOffset;
        if (Anchors.FootprintCenter != T.TransformPosition(FVector(0.0, 0.0, 0.0))) { return "anchor FootprintCenter differs"; }
        if (Anchors.AgentSpawnFront != T.TransformPosition(FVector(D * 0.5 + Offset, 0.0, 0.0))) { return "anchor AgentSpawnFront differs"; }
        if (Anchors.AgentSpawnBack != T.TransformPosition(FVector(-D * 0.5 - Offset, 0.0, 0.0))) { return "anchor AgentSpawnBack differs"; }
        if (Anchors.AgentSpawnLeft != T.TransformPosition(FVector(0.0, W * 0.5 + Offset, 0.0))) { return "anchor AgentSpawnLeft differs"; }
        if (Anchors.AgentSpawnRight != T.TransformPosition(FVector(0.0, -W * 0.5 - Offset, 0.0))) { return "anchor AgentSpawnRight differs"; }
        if (Anchors.PanelTopFront != T.TransformPosition(FVector(D * 0.5, 0.0, H))) { return "anchor PanelTopFront differs"; }
        if (Anchors.PanelCenter != T.TransformPosition(FVector(0.0, 0.0, H * 0.5))) { return "anchor PanelCenter differs"; }
        if (InRequireAppliedTags)
        {
            for (int32 i = 0; i < Recipe.StationTags.Num(); ++i)
            {
                auto Matches = utils_entity_tag::ForEach_Entity(World, Recipe.StationTags[i]);
                if (Matches.Num() != 1 || Matches[0] != InStation)
                {
                    bool ExactIdentity = Matches.Num() == 1 && Matches[0] == InStation;
                    return f"station tag index={i} count={Matches.Num()} exactIdentity={ExactIdentity}";
                }
            }
        }
        return "exact station changed between gate and diagnostic";
    }

    private bool Get_HasRunnableOwnerContext()
    {
        return Get_IsLive(World) && World == ck::TransientEntity() &&
            !CkGym_Admission::Get_IsClosedUnproven(World) && Get_IsLive(CohortOwner) &&
            utils_entity_lifetime::Has_LifetimeOwner(CohortOwner) &&
            utils_entity_lifetime::Get_LifetimeOwner(CohortOwner) == World;
    }

    private bool Get_HasParallelArrays()
    {
        if (Recipes.Num() <= 0 || PendingStations.Num() != Recipes.Num() || Stations.Num() != Recipes.Num() ||
            Constructed.Num() != Recipes.Num() || Completed.Num() != Recipes.Num()) { return false; }
        for (int32 i = 0; i < Recipes.Num(); i++)
        { if ((Constructed[i] != 0 && Constructed[i] != 1) || (Completed[i] != 0 && Completed[i] != 1)) { return false; } }
        return true;
    }

    private bool Get_HasExactStations(bool InRequireAppliedTags)
    {
        if (!Get_IsLive(World) || World != ck::TransientEntity() || !Get_IsLive(CohortOwner) ||
            !Get_HasParallelArrays() || KnownAllocated.Num() != KnownParents.Num() ||
            !KnownAllocated.Contains(CohortOwner) || !KnownAllocated.Contains(AdmissionOwner) ||
            !Get_HasRequiredLive()) { return false; }
        TArray<FCk_Handle> DistinctStations;
        TArray<FName> DistinctTags;
        for (int32 i = 0; i < Recipes.Num(); i++)
        {
            if (DistinctStations.Contains(Stations[i])) { return false; }
            DistinctStations.Add(Stations[i]);
            for (auto Tag : Recipes[i].StationTags)
            { if (DistinctTags.Contains(Tag)) { return false; } DistinctTags.Add(Tag); }
            if (Constructed[i] != 1 || Completed[i] != 1 || !Get_IsValidRecipe(Recipes[i]) ||
                !utils_pending_entity_script::Get_IsValid(PendingStations[i]) ||
                !KnownAllocated.Contains(Stations[i]) || Stations[i] != PendingStations[i].Get_EntityUnderConstruction() ||
                !Get_IsExactStation(Stations[i], i, InRequireAppliedTags)) { return false; }
        }
        return RefreshClosure(true) && Get_HasRequiredLive();
    }

    private bool Get_IsRequiredIdentityLive(FCk_Handle InHandle, FCk_Handle InParent)
    {
        if (!Get_IsLive(InHandle) || !Get_IsLive(InParent) ||
            !utils_entity_lifetime::Has_LifetimeOwner(InHandle) ||
            utils_entity_lifetime::Get_LifetimeOwner(InHandle) != InParent) { return false; }
        int32 Index = KnownAllocated.FindIndex(InHandle);
        return Index >= 0 && Index < KnownParents.Num() && KnownParents[Index] == InParent;
    }

    private bool Get_HasRequiredLive()
    {
        if (!Get_IsLive(World) || World != ck::TransientEntity() ||
            !Get_IsRequiredIdentityLive(AdmissionOwner, World) ||
            !Get_IsRequiredIdentityLive(CohortOwner, World) ||
            RequiredHandles.Num() != RequiredParents.Num() || !Get_HasParallelArrays()) { return false; }
        for (int32 i = 0; i < PendingStations.Num(); i++)
        {
            if (!utils_pending_entity_script::Get_IsValid(PendingStations[i]) ||
                !Get_IsRequiredIdentityLive(PendingStations[i].Get_EntityUnderConstruction(), CohortOwner))
            { return false; }
            if (Constructed[i] == 1 &&
                !Get_IsRequiredIdentityLive(Stations[i], CohortOwner)) { return false; }
        }
        for (int32 i = 0; i < RequiredHandles.Num(); i++)
        { if (!Get_IsRequiredIdentityLive(RequiredHandles[i], RequiredParents[i])) { return false; } }
        return true;
    }

    private bool Get_OwnsAdmission()
    {
        if (!OwnsAdmission || !Get_IsLive(World) || !Get_IsLive(AdmissionOwner) ||
            !World.Has_Fragment(FCkGym_StationAdmission)) { return false; }
        return CkGym_Admission::Get_IsOwned(World, AdmissionTicket, AdmissionOwner);
    }

    private bool ReleaseAdmission()
    {
        if (!OwnsAdmission) { return AdmissionTicket == 0; }
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(World))
        { OwnsAdmission = false; AdmissionTicket = 0; return true; }
        if (!CkGym_Admission::TryReleasePublished(World, AdmissionTicket, AdmissionOwner))
        { FailClosed(); return false; }
        OwnsAdmission = false; AdmissionTicket = 0;
        return true;
    }

    // Permanent provenance dedup is distinct from per-pass cycle prevention. Every known root
    // is revisited on each pass; new late descendants are enrolled only under its original parent.
    private bool Remember(FCk_Handle InRoot, FCk_Handle InParent)
    {
        TArray<FCk_Handle> Children;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(InRoot, World, Children))
        {
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InRoot))
            { if (!Retiring.Contains(InRoot)) { Retiring.Add(InRoot); } RetirementUnproven = true; }
            return false;
        }
        if (InRoot == World || utils_entity_lifetime::Get_LifetimeOwner(InRoot,
            ECk_PendingKill_Policy::IncludePendingKill) != InParent) { return false; }
        int32 Index = KnownAllocated.FindIndex(InRoot);
        if (Index >= 0)
        { return Index < KnownParents.Num() && KnownParents[Index] == InParent; }
        if (KnownAllocated.Num() != KnownParents.Num()) { return false; }
        KnownAllocated.Add(InRoot); KnownParents.Add(InParent);
        return RefreshClosure(false);
    }

    private bool RefreshClosure(bool RequireRunnable)
    {
        if (KnownAllocated.Num() != KnownParents.Num()) { return false; }
        TArray<FCk_Handle> Visited;
        bool Exact = true;
        for (int32 i = 0; i < KnownAllocated.Num(); i++)
        {
            auto Known = KnownAllocated[i];
            if (Visited.Contains(Known)) { continue; }
            Visited.Add(Known);
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Known))
            { continue; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(Known, World, Children)) { return false; }
            if (Known == World) { return false; }
            bool ParentMatches = utils_entity_lifetime::Get_LifetimeOwner(Known,
                ECk_PendingKill_Policy::IncludePendingKill) == KnownParents[i];
            if (RequireRunnable && !ParentMatches) { return false; }
            for (auto Child : Children)
            {
                if (KnownAllocated.Contains(Child)) { continue; }
                // An escaped member is still ours to retire, but unknown new dependents after
                // reparenting are not trusted. Do not adopt/destroy a new owner's foreign tree.
                if (!ParentMatches) { Exact = false; continue; }
                KnownAllocated.Add(Child); KnownParents.Add(Known);
            }
        }
        return Exact;
    }

    private void FailClosed()
    {
        TerminalFailure = true; RetirementUnproven = true; Phase = 3;
        if (AdmissionTicket != 0)
        { CkGym_Admission::TryCloseUnproven(World, AdmissionTicket, AdmissionOwner); }
        CkGym_Admission::TryCloseWorldUnproven(World);
    }

    private void FailAndRetire()
    { FailClosed(); }
}
