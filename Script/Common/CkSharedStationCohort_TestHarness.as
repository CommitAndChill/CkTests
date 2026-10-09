// Real callback owner for shared-cohort integration. All handles remain world-local.
class ACkSharedStationCohort_TestHarness : AActor
{
    default bReplicates = false;
    UPROPERTY() private FCkGym_SharedStationCohort Main;
    UPROPERTY() private FCkGym_SharedStationCohort BusyProbe;
    UPROPERTY() private FCk_Handle World;
    UPROPERTY() private FCk_Handle CurrentAdmissionOwner;
    UPROPERTY() private TArray<FCk_GymStation_SpawnParams> Recipes;
    UPROPERTY() private TArray<FCk_Handle> Ledger;
    UPROPERTY() private TArray<FCk_Handle> Early;
    UPROPERTY() private TArray<int32> ActualCompleted;
    UPROPERTY() private TArray<int32> ActualConstructed;
    UPROPERTY() private TArray<FCk_Handle> Geometry;
    UPROPERTY() private FCk_Handle Late;
    UPROPERTY() private FCk_Handle Deep;
    UPROPERTY() private FCk_Handle NewOwner;
    UPROPERTY() private FCk_Handle ReentrantChild;
    UPROPERTY() private FCk_Handle ReentrantParent;
    private bool Interrupted = false;
    private int32 Scenario = -1; // 0 transfer, 1 interruption, 2 malformed, 3 reentrant.
    private bool Broken = false;
    private bool Stopped = false;
    private bool Finished = false;
    private bool InterruptionObserved = false;
    private bool OrdinaryObserved = false;
    private bool CarrierExpiryObserved = false;
    private bool HealthyRetirementObserved = false;
    private bool TransferObserved = false;
    private bool MalformedObserved = false;
    private bool TransformCorruptionObserved = false;
    private bool MalformNext = false;
    private bool ReentrantArmed = false;
    private bool ReentrantCallbackObserved = false;
    private bool ReentrantChildWasLive = false;
    private bool ReentrantProofObserved = false;
    private int32 Stage = 0;
    private int32 Episode = 0;
    private double Deadline = 0.0;
    private FString Failure;

    private bool Live(FCk_Handle H)
    { return ck::IsValid(H) && !utils_entity_lifetime::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    private bool Allocated(FCk_Handle H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
    private bool Capture(FCk_Handle Root)
    {
        if (!Allocated(Root)) { return true; }
        TArray<FCk_Handle> Queue; Queue.Add(Root);
        TArray<FCk_Handle> Visited;
        for (int32 i = 0; i < Queue.Num(); ++i)
        {
            auto H = Queue[i];
            if (Visited.Contains(H)) { continue; }
            Visited.Add(H);
            if (!Allocated(H)) { continue; }
            if (!Ledger.Contains(H)) { Ledger.Add(H); }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H, World, Children))
            { return false; }
            for (auto Child : Children) { Queue.Add(Child); }
        }
        return true;
    }
    private bool CaptureAll()
    {
        // Callbacks and terminal ticks can arrive while PIE is tearing down.
        // Reject before any fragment read on a stale world root.
        if (!Live(World) || World != ck::TransientEntity()) { return false; }
        // Revisit all trusted roots every pass; newly created descendants are not hidden.
        auto Prior = Ledger;
        for (auto H : Prior) { if (!Capture(H)) { return false; } }
        if (Main.Get_Phase() == 1 || Main.Get_Phase() == 2)
        { for (auto P : Main.PendingStations)
        {
            auto H = P.Get_EntityUnderConstruction();
            if (!Live(H) || !utils_entity_lifetime::Has_LifetimeOwner(H)) { return false; }
            if (!Capture(utils_entity_lifetime::Get_LifetimeOwner(H))) { return false; }
        }
        }
        if (World.Has_Fragment(FCkGym_StationAdmission))
        {
            auto Admission = World.Get_Fragment(FCkGym_StationAdmission);
            if (Allocated(Admission.ActiveOwner) && !Capture(Admission.ActiveOwner)) { return false; }
        }
        return true;
    }
    private void Fail(FString Why)
    { if (!Broken) { Failure = Why; } Broken = true; Stage = -1; }
    private void Next(int32 Value)
    { Stage = Value; Deadline = System::GetGameTimeInSeconds() + 30.0; }
    private bool AllRetired()
    { for (auto H : Ledger) { if (Allocated(H)) { return false; } } return true; }
    private int32 FindTarget(FCk_Handle H)
    {
        for (int32 i = 0; i < Main.PendingStations.Num(); ++i)
        { if (Main.PendingStations[i].Get_EntityUnderConstruction() == H) { return i; } }
        return -1;
    }
    private void MakeRecipes()
    {
        Recipes.Empty();
        for (int32 i = 0; i < 2; ++i)
        {
            FCk_GymStation_SpawnParams R;
            R.InitialTransform = FTransform(FRotator(0.0, 90.0 * i, 0.0), FVector(i * 1300.0, 0.0, 0.0));
            R.Width = 6.0 + i; R.Depth = 5.0 + i; R.Height = 5.0 + i;
            R.WallThickness = 15.0 + i; R.FloorThickness = 16.0 + i; R.TrimDepth = 30.0 + i;
            R.AgentSpawnOffset = 20.0 + i; R.TitleScale = 20.0 + i; R.DescriptionScale = 12.0 + i;
            R.FloorDescriptionScale = 13.0 + i; R.ShowSpotlight = i == 0; R.ShowAnchors = false;
            R.AutoSize = false; R.ShowDebugOverlays = false;
            R.TitleText = FText::FromString(f"Cohort station {i}");
            R.DescriptionText.Add(FText::FromString("Original complete station recipe"));
            R.FloorDescriptionText.Add(FText::FromString("Original floor text"));
            R.StationTags.Add(FName(f"Test.SharedCohort.Episode{Episode}.Station{i}"));
            Recipes.Add(R);
        }
    }
    private bool Begin()
    {
        Main = FCkGym_SharedStationCohort(); ++Episode; MakeRecipes();
        ActualCompleted.Empty(); ActualConstructed.Empty(); Geometry.Empty(); Early.Empty();
        for (int32 i = 0; i < 2; ++i) { ActualCompleted.Add(0); ActualConstructed.Add(0); Geometry.Add(FCk_Handle()); }
        if (Main.TryBegin(World, Recipes, FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"),
            FCk_Delegate_EntityScript_Constructed(this, n"OnConstructed")) != ECkGym_SharedStationBegin::Started)
        { return false; }
        if (!World.Has_Fragment(FCkGym_StationAdmission)) { return false; }
        CurrentAdmissionOwner = World.Get_Fragment(FCkGym_StationAdmission).ActiveOwner;
        if (!Live(CurrentAdmissionOwner)) { return false; }
        if (!CaptureAll()) { return false; }
        for (auto P : Main.PendingStations)
        {
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(P.Get_EntityUnderConstruction(), World, Children))
            { return false; }
            for (auto Child : Children) { Early.Add(Child); }
        }
        return Early.Num() == 2;
    }
    // Full original recipe check belongs to the real caller, beyond helper structural Ready.
    private bool ExactRecipe(FCk_Handle H, int32 Index)
    {
        if (!Live(H) || Index < 0 || Index >= Recipes.Num()) { return false; }
        auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
        if (ck::Is_NOT_Valid(Script) || Script.GetClass() != UCk_EntityScript_GymStation) { return false; }
        auto S = Cast<UCk_EntityScript_GymStation>(Script); auto R = Recipes[Index];
        if (S.InitialTransform.GetTranslation() != R.InitialTransform.GetTranslation() ||
            S.InitialTransform.GetRotation() != R.InitialTransform.GetRotation() || S.InitialTransform.GetScale3D() != R.InitialTransform.GetScale3D() ||
            S.Width != R.Width || S.Depth != R.Depth || S.Height != R.Height || S.WallThickness != R.WallThickness ||
            S.FloorThickness != R.FloorThickness || S.TrimDepth != R.TrimDepth || S.AgentSpawnOffset != R.AgentSpawnOffset ||
            S.BodyColour != R.BodyColour || S.TrimColour != R.TrimColour || S.TitleText.ToString() != R.TitleText.ToString() ||
            S.TitleScale != R.TitleScale || S.TitleColour != R.TitleColour || S.DescriptionScale != R.DescriptionScale ||
            S.DescriptionColour != R.DescriptionColour || S.TextAlignment != R.TextAlignment ||
            S.FloorDescriptionScale != R.FloorDescriptionScale || S.FloorDescriptionColour != R.FloorDescriptionColour ||
            S.FloorTextAlignment != R.FloorTextAlignment || S.FloorTextPlacement != R.FloorTextPlacement ||
            S.ShowSpotlight != R.ShowSpotlight || S.ShowAnchors != R.ShowAnchors || S.ShowDebugOverlays != R.ShowDebugOverlays || S.AutoSize != R.AutoSize ||
            S.DescriptionText.Num() != R.DescriptionText.Num() || S.FloorDescriptionText.Num() != R.FloorDescriptionText.Num() ||
            S.StationTags.Num() != R.StationTags.Num()) { return false; }
        for (int32 j = 0; j < R.DescriptionText.Num(); ++j) { if (S.DescriptionText[j].ToString() != R.DescriptionText[j].ToString()) { return false; } }
        for (int32 j = 0; j < R.FloorDescriptionText.Num(); ++j) { if (S.FloorDescriptionText[j].ToString() != R.FloorDescriptionText[j].ToString()) { return false; } }
        for (int32 j = 0; j < R.StationTags.Num(); ++j)
        {
            if (S.StationTags[j] != R.StationTags[j]) { return false; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, R.StationTags[j]);
            if (Matches.Num() != 1 || Matches[0] != H) { return false; }
        }
        if (!H.Has_Fragment(FCkGym_Station_Anchors)) { return false; }
        auto A = H.Get_Fragment(FCkGym_Station_Anchors); auto T = R.InitialTransform;
        return A.FootprintCenter == T.TransformPosition(FVector::ZeroVector) &&
            A.AgentSpawnFront == T.TransformPosition(FVector(R.Depth * 50.0 + R.AgentSpawnOffset, 0.0, 0.0)) &&
            A.AgentSpawnBack == T.TransformPosition(FVector(-R.Depth * 50.0 - R.AgentSpawnOffset, 0.0, 0.0)) &&
            A.AgentSpawnLeft == T.TransformPosition(FVector(0.0, R.Width * 50.0 + R.AgentSpawnOffset, 0.0)) &&
            A.AgentSpawnRight == T.TransformPosition(FVector(0.0, -R.Width * 50.0 - R.AgentSpawnOffset, 0.0)) &&
            A.PanelTopFront == T.TransformPosition(FVector(R.Depth * 50.0, 0.0, R.Height * 100.0)) &&
            A.PanelCenter == T.TransformPosition(FVector(0.0, 0.0, R.Height * 50.0));
    }
    // A station geometry SCN owns its separate UnrealComponent entity.
    private bool IsGeometry(FCk_Handle H, FCk_Handle Station)
    {
        if (!Live(H) || !Live(Station) || !utils_scene_node::Has(H) ||
            !utils_entity_lifetime::Has_LifetimeOwner(H) ||
            utils_entity_lifetime::Get_LifetimeOwner(H) != Station) { return false; }
        TArray<FCk_Handle> Components;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H, World, Components))
        { return false; }
        for (auto Component : Components)
        {
            if (Live(Component) && utils_unreal_component::Has(Component) &&
                utils_entity_lifetime::Has_LifetimeOwner(Component) &&
                utils_entity_lifetime::Get_LifetimeOwner(Component) == H) { return true; }
        }
        return false;
    }
    private bool WholeReady()
    {
        if (Main.PendingStations.Num() != 2 || Main.Stations.Num() != 2 || ActualCompleted.Num() != 2 || ActualConstructed.Num() != 2 || Geometry.Num() != 2 || Main.Completed.Num() != 2 || Main.Constructed.Num() != 2)
        { return false; }
        for (int32 i = 0; i < 2; ++i)
        {
            if (ActualCompleted[i] != 1 || ActualConstructed[i] != 1 || Main.Completed[i] != 1 || Main.Constructed[i] != 1 ||
                !ExactRecipe(Main.Stations[i], i) || !Live(Geometry[i]) || !utils_entity_lifetime::Has_LifetimeOwner(Geometry[i]) ||
                utils_entity_lifetime::Get_LifetimeOwner(Geometry[i]) != Main.Stations[i] ||
                !IsGeometry(Geometry[i], Main.Stations[i])) { return false; }
        }
        for (auto H : Early) { if (Allocated(H)) { return false; } }
        return true;
    }
    UFUNCTION()
    private void OnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (Stopped || Broken || Main.Get_Phase() != 1) { return; }
        int32 i = FindTarget(InOwner);
        if (i < 0 || i >= ActualCompleted.Num() || i >= ActualConstructed.Num() || i >= Geometry.Num() ||
            ActualCompleted[i] != 0 || ActualConstructed[i] != 0 || !CaptureAll())
        { Fail("Missing exact completed-after-Construct/before-constructed opportunity"); return; }
        ++ActualCompleted[i];
        if (MalformNext)
        {
            Main.Completed.Empty();
            MalformedObserved = !Main.TryRecord_Completed(InOwner, InOwner, InResult) && Main.Get_HasTerminalFailure();
            if (!MalformedObserved) { Fail("Malformed public parallel arrays were not rejected"); }
            return;
        }
        if (!Main.TryRecord_Completed(InOwner, InOwner, InResult)) { Fail("Real completion rejected"); return; }
        // Normal Pending poll captures newly constructed descendants before required enrollment.
        auto Poll = Main.PollConstruction();
        if (Interrupted && Episode == 1 && InterruptionObserved &&
            Poll == ECkGym_SharedStationPoll::Failed && Main.Get_HasTerminalFailure())
        { return; }
        if (Poll != ECkGym_SharedStationPoll::Pending) { Fail("Completion ordering was not pending constructed"); return; }
        TArray<FCk_Handle> Children;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(InOwner, World, Children))
        { Fail("Completed target census rejected"); return; }
        for (auto Child : Children)
        {
            if (IsGeometry(Child, InOwner))
            { Geometry[i] = Child; break; }
        }
        if (!Live(Geometry[i]) || (!Main.TryRequireOwned(Geometry[i], InOwner) || !Main.TryRequireOwned(Geometry[i], InOwner)))
        { Fail(f"No captured real geometry SCN/component pair admitted as required: slot {i}, direct children {Children.Num()}, selected live {Live(Geometry[i])}, terminal {Main.Get_HasTerminalFailure()}"); return; }
        if (Interrupted && Episode == 1 && !InterruptionObserved)
        {
            InterruptionObserved = true;
            utils_entity_lifetime::Request_DestroyEntity(Geometry[i]);
        }
    }
    UFUNCTION()
    private void OnConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (Stopped || Broken || Main.Get_Phase() != 1) { return; }
        auto H = FCk_Handle(InHandle); int32 i = FindTarget(H);
        if (i < 0 || i >= ActualConstructed.Num() || ActualConstructed[i] != 0 || !CaptureAll())
        { Fail("Invalid real constructed callback"); return; }
        ++ActualConstructed[i];
        if (!Main.TryRecord_Constructed(H, InHandle))
        {
            if (!(Interrupted && Episode == 1 && InterruptionObserved && Main.Get_HasTerminalFailure()))
            { Fail("Real constructed callback rejected unexpectedly: " + Main.Get_LastConstructedReject()); }
        }
    }
    UFUNCTION()
    private void OnStationBeginDestroy(FCk_Handle InStation)
    {
        if (!ReentrantArmed || ReentrantCallbackObserved) { return; }
        ReentrantCallbackObserved = true;
        if (Main.Stations.Num() != 2 ||
            (InStation != Main.Stations[0] && InStation != Main.Stations[1])) { return; }
        ReentrantParent = InStation == Main.Stations[0] ? Main.Stations[1] : Main.Stations[0];
        if (!Live(ReentrantParent) || !utils_entity_lifetime::Get_CanCreateEntity(ReentrantParent)) { return; }
        ReentrantChild = utils_entity_lifetime::Request_CreateEntity(ReentrantParent);
        ReentrantChildWasLive = Live(ReentrantChild) &&
            utils_entity_lifetime::Has_LifetimeOwner(ReentrantChild) &&
            utils_entity_lifetime::Get_LifetimeOwner(ReentrantChild) == ReentrantParent;
    }
    UFUNCTION(BlueprintCallable)
    bool Request_StartScenario(int32 InScenario)
    {
        if (Stage != 0 || Stopped || InScenario < 0 || InScenario > 4) { return false; }
        Scenario = InScenario;
        World = ck::TransientEntity(); Interrupted = Scenario == 1;
        if (!Live(World) || !Begin()) { Fail("Initial real admission failed"); return false; }
        auto Active = World.Get_Fragment(FCkGym_StationAdmission).ActiveOwner;
        TArray<FCk_Handle> BeforeChildren; TArray<FCk_Handle> AfterChildren;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(World, World, BeforeChildren))
        { Fail("Busy preflight census rejected"); return false; }
        auto Busy = BusyProbe.TryBegin(World, Recipes, FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"), FCk_Delegate_EntityScript_Constructed(this, n"OnConstructed"));
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(World, World, AfterChildren) ||
            Busy != ECkGym_SharedStationBegin::Busy || BusyProbe.Get_Phase() != 0 || BusyProbe.PendingStations.Num() != 0 ||
            BeforeChildren.Num() != AfterChildren.Num() ||
            World.Get_Fragment(FCkGym_StationAdmission).ActiveOwner != Active)
        { Fail("Second helper allocated or changed exact admission"); return false; }
        for (auto H : BeforeChildren) { if (!AfterChildren.Contains(H)) { Fail("Busy changed world children"); return false; } }
        Next(1); return true;
    }
    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (Stopped || Broken || Stage == 0) { return; }
        if (Finished)
        { if (!CaptureAll()) { Fail("Terminal post-tick census rejected"); } return; }
        if (!Live(World) || World != ck::TransientEntity()) { Fail("Harness world identity lost"); return; }
        if (System::GetGameTimeInSeconds() > Deadline) { Fail(f"Bounded episode timeout: stage {Stage}"); return; }
        if (!CaptureAll()) { Fail("Allocated closure census unproven"); return; }
        if (Stage == 1 || Stage == 5)
        {
            // Hold the second real cohort after its callbacks for the native malformed
            // transform injection; do not let Tick make it Ready before the probe.
            if (Stage == 5 && Scenario == 4) { return; }
            auto Poll = Main.PollConstruction();
            if (Stage == 1 && Interrupted)
            {
                if (Poll == ECkGym_SharedStationPoll::Ready) { Fail("Interrupted required geometry became Ready"); return; }
                if (Poll == ECkGym_SharedStationPoll::Failed)
                { if (!InterruptionObserved) { Fail("Missing genuine interruption"); return; } Next(7); }
                return;
            }
            if (Stage == 5 && Scenario == 2)
            {
                if (Poll == ECkGym_SharedStationPoll::Ready) { Fail("Malformed callbacks became Ready"); return; }
                if (Poll == ECkGym_SharedStationPoll::Failed)
                { if (!MalformedObserved) { Fail("Malformed wrapper opportunity missing"); return; } Next(7); }
                return;
            }
            if (Poll == ECkGym_SharedStationPoll::Failed) { Fail("Ordinary construction failed"); return; }
            if (Poll != ECkGym_SharedStationPoll::Ready || !WholeReady()) { return; }
            CarrierExpiryObserved = true;
            if (Stage == 1)
            {
                OrdinaryObserved = true;
                Main.Request_Retire();
                if (Main.Get_IsRetirementUnproven())
                { Fail("Healthy complete cohort could not begin proven retirement"); return; }
                Next(4); return;
            }
            if (Scenario == 0)
            {
                Late = utils_entity_lifetime::Request_CreateEntity(Main.Stations[0]);
                if (!Live(Late)) { Fail("Late child allocation failed"); return; }
                Deep = utils_entity_lifetime::Request_CreateEntity(Late);
                NewOwner = utils_entity_lifetime::Request_CreateEntity(World);
                if (!Live(Deep) || !Live(NewOwner) || !CaptureAll() ||
                    Main.PollConstruction() != ECkGym_SharedStationPoll::Ready ||
                    !Main.TryRequireOwned(Late, Main.Stations[0]) ||
                    !Main.TryRequireOwned(Deep, Late))
                { Fail("Late/deep original cohort enrollment rejected"); return; }
                Next(2); return;
            }
            if (Scenario == 3)
            {
                if (Main.Stations.Num() != 2 || !Live(Main.Stations[0]) || !Live(Main.Stations[1]))
                { Fail("Reentrant retirement needs two live stations"); return; }
                utils_entity_lifetime::BindTo_OnBeginDestroy(Main.Stations[0],
                    FCk_Delegate_OnBeginDestroy(this, n"OnStationBeginDestroy"));
                utils_entity_lifetime::BindTo_OnBeginDestroy(Main.Stations[1],
                    FCk_Delegate_OnBeginDestroy(this, n"OnStationBeginDestroy"));
                ReentrantArmed = true;
                Main.Request_Retire();
                ReentrantArmed = false;
                if (!ReentrantCallbackObserved || !ReentrantChildWasLive ||
                    !Allocated(ReentrantChild) || !Live(ReentrantParent) ||
                    !Main.Get_IsRetirementUnproven() || Main.PollRetirement())
                { Fail("Reentrant unknown child was destroyed or retirement claimed proven"); return; }
                ReentrantProofObserved = true;
                if (!Capture(ReentrantChild)) { Fail("Reentrant child census rejected"); return; }
                Next(7); return;
            }
            Fail("Unexpected second cohort scenario"); return;
        }
        if (Stage == 2)
        {
            if (!Live(Late) || !Live(NewOwner) || !utils_entity_lifetime::Has_LifetimeOwner(Late))
            { Fail("Transfer operands lost before native request"); return; }
            if (utils_entity_lifetime::Get_LifetimeOwner(Late) == NewOwner)
            {
                TransferObserved = true;
                if (Main.PollConstruction() != ECkGym_SharedStationPoll::Failed ||
                    !Main.Get_HasTerminalFailure())
                { Fail("Transferred required late child did not fail terminally"); return; }
                Next(7);
            }
            return;
        }
        if (Stage == 4)
        {
            if (Main.Get_IsRetirementUnproven()) { Fail("Healthy retirement became unproven"); return; }
            if (!Main.PollRetirement() || !AllRetired()) { return; }
            HealthyRetirementObserved = true;
            MalformNext = Scenario == 2;
            if (!Begin()) { Fail("Second cohort admission after proven retirement failed"); return; }
            Next(5); return;
        }
        if (Stage == 7)
        {
            if (!Main.Get_HasTerminalFailure() || !Main.Get_IsRetirementUnproven() ||
                Main.Get_Phase() != 3 || Main.PollRetirement() ||
                !CkGym_Admission::Get_IsClosedUnproven(World))
            { Fail("Failed cohort did not retain terminal world closure"); return; }
            if (Interrupted && !InterruptionObserved)
            { Fail("Interrupted construction had no real geometry loss"); return; }
            if (!Interrupted && (!HealthyRetirementObserved || !OrdinaryObserved ||
                !CarrierExpiryObserved || (Scenario == 0 && !TransferObserved) ||
                (Scenario == 2 && !MalformedObserved) ||
                (Scenario == 3 && !ReentrantProofObserved) ||
                (Scenario == 4 && !TransformCorruptionObserved)))
            { Fail("Healthy retirement/rebegin or terminal scenario evidence missing"); return; }
            TArray<FCk_Handle> BeforeChildren;
            TArray<FCk_Handle> AfterChildren;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                    World, World, BeforeChildren))
            { Fail("Terminal world census rejected"); return; }
            auto Busy = BusyProbe.TryBegin(World, Recipes,
                FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"),
                FCk_Delegate_EntityScript_Constructed(this, n"OnConstructed"));
            if (Busy != ECkGym_SharedStationBegin::Rejected ||
                !UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                    World, World, AfterChildren) || BeforeChildren.Num() != AfterChildren.Num())
            { Fail("Terminal world admitted or allocated another cohort"); return; }
            for (auto H : BeforeChildren)
            { if (!AfterChildren.Contains(H)) { Fail("Terminal begin changed world children"); return; } }
            Finished = true;
        }
    }
    UFUNCTION(BlueprintPure) bool Get_IsWaitingTransformCorruption() const
    {
        return Scenario == 4 && Stage == 5 && !Broken && !Stopped &&
            Main.Get_Phase() == 1 && Main.Stations.Num() == 2 &&
            Main.Completed.Num() == 2 && Main.Constructed.Num() == 2 &&
            Main.Completed[0] == 1 && Main.Completed[1] == 1 &&
            Main.Constructed[0] == 1 && Main.Constructed[1] == 1 &&
            !Main.Get_HasTerminalFailure() && Main.Stations[0] != FCk_Handle();
    }
    UFUNCTION(BlueprintPure) FCk_Handle Get_TransformCorruptionTarget() const
    { return Get_IsWaitingTransformCorruption() ? Main.Stations[0] : FCk_Handle(); }
    UFUNCTION(BlueprintCallable) bool Request_ProbeMalformedTransform()
    {
        if (!Get_IsWaitingTransformCorruption()) { return false; }
        auto Poll = Main.PollConstruction();
        auto Visible = Main.Get_Stations();
        TransformCorruptionObserved = Poll == ECkGym_SharedStationPoll::Failed &&
            Visible.IsEmpty() && Main.Get_HasTerminalFailure() &&
            Main.Get_IsRetirementUnproven() && Main.Get_Phase() == 3 &&
            CkGym_Admission::Get_IsClosedUnproven(World);
        if (!TransformCorruptionObserved) { Fail("Malformed real station transform was not rejected atomically"); return false; }
        Next(7);
        return true;
    }
    UFUNCTION(BlueprintCallable) void Request_Stop()
    {
        if (Stopped) { return; }
        Stopped = true; ReentrantArmed = false; Main.Request_Retire(); BusyProbe.Request_Retire();
    }
    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason Reason) { Request_Stop(); }
    UFUNCTION(BlueprintPure) bool Get_IsDone() const { return Finished && !Broken; }
    UFUNCTION(BlueprintPure) bool Get_HasFailed() const { return Broken; }
    UFUNCTION(BlueprintPure) FString Get_Failure() const { return Failure; }
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_EarlyCandidates() const { return Early; }
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_Captured() const { return Ledger; }
    UFUNCTION(BlueprintPure) FCk_Handle Get_TerminalOwner() const
    { return CurrentAdmissionOwner; }
    UFUNCTION(BlueprintPure) int32 Get_CurrentRecipeTagCount() const
    {
        if (!ck::IsValid(World)) { return -1; }
        int32 Count = 0;
        for (auto Recipe : Recipes)
        { for (auto Tag : Recipe.StationTags)
          { Count += utils_entity_tag::ForEach_Entity(World, Tag).Num(); } }
        return Count;
    }
    UFUNCTION(BlueprintPure) bool Get_HealthyRetirementObserved() const { return HealthyRetirementObserved; }
    UFUNCTION(BlueprintPure) FCk_Handle Get_Late() const { return Late; }
    UFUNCTION(BlueprintPure) FCk_Handle Get_NewOwner() const { return NewOwner; }
    UFUNCTION(BlueprintPure) bool Get_IsWaitingTransfer() const { return Stage == 2 && !Broken; }
}
