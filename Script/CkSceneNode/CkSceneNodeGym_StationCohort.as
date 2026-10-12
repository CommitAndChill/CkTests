// Six local SceneNode station shells. Only creator-returned identities enroll.
// The local controller owns presentation timing and this cohort's retirement.
USTRUCT()
struct FCkSceneNodeGym_StationCohort
{
    UPROPERTY() private FCk_Handle World;
    UPROPERTY() private FCk_Handle Parent;
    UPROPERTY() private FCk_Handle CohortOwner;
    UPROPERTY() private TArray<FCk_GymStation_SpawnParams> Recipes;
    UPROPERTY() TArray<FCk_Handle_PendingEntityScript> PendingStations;
    UPROPERTY() TArray<FCk_Handle> Stations;
    UPROPERTY() private TArray<FCk_Handle> Targets;
    UPROPERTY() private TArray<FCk_Handle> Carriers;
    UPROPERTY() private TArray<UCk_EntityScript_GymStation> Scripts;
    UPROPERTY() private TArray<FCkGymStation_BaselineCreatorLedger> Frames;
    UPROPERTY() private TArray<int32> Constructed;
    UPROPERTY() private TArray<int32> Completed;
    UPROPERTY() private TArray<FCk_Handle> Known;
    UPROPERTY() private TArray<FCk_Handle> Parents;
    // Conflicting creator returns retain their declared parent without changing
    // the original identity/parent pair in Known/Parents.
    UPROPERTY() private TArray<FCk_Handle> RejectedReturns;
    UPROPERTY() private TArray<FCk_Handle> RejectedParents;
    UPROPERTY() private TArray<FCk_Handle> Retiring;
    UPROPERTY() private int32 Phase=0;
    UPROPERTY() private bool TerminalFailure=false;
    UPROPERTY() private bool RetirementUnproven=false;
    UPROPERTY() private FString LastAdmissionRejection;

    int32 Get_Phase() const { return Phase; }
    bool Get_HasTerminalFailure() const { return TerminalFailure; }
    bool Get_IsRetirementUnproven() const { return RetirementUnproven; }
    FCk_Handle Get_CohortOwner() const { return CohortOwner; }
    FString Get_LastAdmissionRejection() const { return LastAdmissionRejection; }

    private void RejectAdmission(FString Reason)
    { if (LastAdmissionRejection.IsEmpty()) { LastAdmissionRejection=Reason; } }

    private bool Allocated(FCk_Handle H) const
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
    private bool Live(FCk_Handle H) const
    { return ck::IsValid(H) && !utils_entity_lifetime::Get_IsPendingDestroy(
        H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    private bool Arrays() const
    {
        return Recipes.Num()==6 && PendingStations.Num()==6 && Stations.Num()==6 &&
            Targets.Num()==6 && Carriers.Num()==6 && Scripts.Num()==6 &&
            Frames.Num()==6 && Constructed.Num()==6 && Completed.Num()==6 &&
            Known.Num()==Parents.Num() && RejectedReturns.Num()==RejectedParents.Num();
    }
    private void Fail()
    {
        TerminalFailure=true;
        // Failure freezes all already captured producer scripts immediately.
        // No station lookup or entity destruction is performed here.
        for (auto S:Scripts)
        { if (ck::IsValid(S) && !S.Request_CloseBaselinePresentation())
          { RetirementUnproven=true; } }
    }
    private bool Remember(FCk_Handle H, FCk_Handle ExpectedParent)
    {
        if (H==FCk_Handle()) { Fail(); return false; }
        int32 Old=Known.FindIndex(H);
        if (Old>=0)
        {
            if (Old>=Parents.Num() || Parents[Old]!=ExpectedParent)
            { RetainRejected(H,ExpectedParent); Fail(); return false; }
            return true;
        }
        if (Known.Num()!=Parents.Num()) { Fail(); return false; }
        Known.Add(H); Parents.Add(ExpectedParent);
        if (!Known.Contains(ExpectedParent) && ExpectedParent!=World && ExpectedParent!=Parent)
        { Fail(); return false; }
        return true;
    }
    private void RetainRejected(FCk_Handle H, FCk_Handle DeclaredParent)
    {
        if (H==FCk_Handle()) { return; }
        for (int32 i=0;i<RejectedReturns.Num() && i<RejectedParents.Num();++i)
        { if (RejectedReturns[i]==H && RejectedParents[i]==DeclaredParent) { return; } }
        RejectedReturns.Add(H); RejectedParents.Add(DeclaredParent);
    }
    private bool Fresh() const
    {
        return Phase==0 && !TerminalFailure && !RetirementUnproven &&
            World==FCk_Handle() && Parent==FCk_Handle() &&
            CohortOwner==FCk_Handle() && Recipes.IsEmpty() && PendingStations.IsEmpty() &&
            Stations.IsEmpty() && Targets.IsEmpty() && Carriers.IsEmpty() && Scripts.IsEmpty() &&
            Frames.IsEmpty() && Constructed.IsEmpty() && Completed.IsEmpty() && Known.IsEmpty() &&
            Parents.IsEmpty() && RejectedReturns.IsEmpty() &&
            RejectedParents.IsEmpty() && Retiring.IsEmpty();
    }
    // Local PC calls this before TryBeginLocal. The per-parent tag prevents a
    // second local player from claiming another player's authored station tag.
    bool TryBuildLocalRecipes(FCk_Handle InParent,
        TArray<FCk_GymStation_SpawnParams>&out OutRecipes) const
    {
        OutRecipes.Empty();
        if (!Live(InParent)) { return false; }
        FString OwnerKey=utils_handle::Conv_HandleToString(InParent);
        if (OwnerKey.IsEmpty()) { return false; }
        TArray<FCk_GymStation_SpawnParams> Built;
        for (int32 i=0;i<6;++i)
        {
            FCkGym_Station_SpawnParams_Payload P;
            if (!CkSceneNodeGymCatalog::TryGet_LaidOutStation(i,P) ||
                P.Tags.Num()!=1 || P.Tags[0]==FName() || P.AutoSize)
            { return false; }
            FCk_GymStation_SpawnParams R;
            R.InitialTransform=P.Transform;
            R.Width=double(P.Width); R.Depth=double(P.Depth); R.Height=double(P.Height);
            R.TitleText=P.Title; R.DescriptionText=P.Description;
            R.TitleScale=double(P.TitleScale);
            R.DescriptionScale=double(P.DescriptionScale);
            R.TitleColour=P.TitleColour.ToFColor(true);
            R.DescriptionColour=P.DescriptionColour.ToFColor(true);
            R.AutoSize=P.AutoSize; R.ShowDebugOverlays=P.ShowDebugOverlays;
            R.StationTags.Add(FName(f"Gym.SceneNode.Local.{OwnerKey}.Station{i}"));
            if (R.StationTags[0]==FName() || !R.InitialTransform.IsValid() ||
                R.InitialTransform.ContainsNaN() ||
                !Math::IsFinite(R.Width) || R.Width<=0.0 ||
                !Math::IsFinite(R.Depth) || R.Depth<=0.0 ||
                !Math::IsFinite(R.Height) || R.Height<=0.0)
            { return false; }
            Built.Add(R);
        }
        OutRecipes=Built;
        return OutRecipes.Num()==6;
    }
    private bool Get_IsExactLocalRecipe(FCk_GymStation_SpawnParams InRecipe,
        int32 InSlot, FCk_Handle InParent) const
    {
        if (InSlot<0 || InSlot>=6 || !InRecipe.InitialTransform.IsValid() ||
            InRecipe.InitialTransform.ContainsNaN()) { return false; }
        TArray<FCk_GymStation_SpawnParams> Expected;
        if (!TryBuildLocalRecipes(InParent,Expected) || Expected.Num()!=6) { return false; }
        auto A=FInstancedStruct::Make(InRecipe);
        auto B=FInstancedStruct::Make(Expected[InSlot]);
        return A.IsValid() && B.IsValid() &&
            A.GetScriptStruct()==FCk_GymStation_SpawnParams &&
            B.GetScriptStruct()==FCk_GymStation_SpawnParams && A==B;
    }
    bool TryGet_RetainedRecipe(int32 InSlot, FCk_GymStation_SpawnParams&out Out) const
    {
        Out=FCk_GymStation_SpawnParams();
        if (InSlot<0 || InSlot>=6 || !Arrays()) { return false; }
        Out=Recipes[InSlot]; return true;
    }

    private bool Preflight(FCk_Handle InWorld, FCk_Handle InParent,
        const TArray<FCk_GymStation_SpawnParams>&in InRecipes,
        FCk_Delegate_Request_OnCompleted OnCompleted,
        FCk_Delegate_EntityScript_Constructed OnConstructed,
        FString&out Failure) const
    {
        Failure="";
        if (!OnCompleted.IsBound()) { Failure="completed-delegate"; return false; }
        if (!OnConstructed.IsBound()) { Failure="constructed-delegate"; return false; }
        if (!Live(InWorld)) { Failure="world-live"; return false; }
        if (InWorld!=ck::TransientEntity()) { Failure="world-root"; return false; }
        if (!Live(InParent)) { Failure="parent-live"; return false; }
        if (!utils_entity_lifetime::Get_CanCreateEntity(InParent))
        { Failure="parent-can-create"; return false; }
        if (InRecipes.Num()!=6) { Failure=f"recipe-count={InRecipes.Num()}"; return false; }
        TArray<FName> Tags;
        for (int32 i=0;i<6;++i)
        {
            auto P=InRecipes[i];
            if (!Get_IsExactLocalRecipe(P,i,InParent))
            { Failure=f"recipe-finite slot={i}"; return false; }
            if (P.StationTags.IsEmpty())
            { Failure=f"recipe-tags-empty slot={i}"; return false; }
            for (auto Tag:P.StationTags)
            {
                if (Tag==FName()) { Failure=f"tag-default slot={i}"; return false; }
                if (Tags.Contains(Tag)) { Failure=f"tag-duplicate slot={i}"; return false; }
                auto Matches=utils_entity_tag::ForEach_Entity(InWorld,Tag);
                if (!Matches.IsEmpty())
                { Failure=f"tag-vacancy slot={i} matches={Matches.Num()}"; return false; }
                Tags.Add(Tag);
            }
            auto Typed=FInstancedStruct::Make(P);
            if (!Typed.IsValid()) { Failure=f"recipe-typed-invalid slot={i}"; return false; }
            if (Typed.GetScriptStruct()!=FCk_GymStation_SpawnParams)
            { Failure=f"recipe-typed-class slot={i}"; return false; }
        }
        return true;
    }
    private void Prefill(FCk_Handle InWorld, FCk_Handle InParent,
        const TArray<FCk_GymStation_SpawnParams>&in InRecipes)
    {
        World=InWorld; Parent=InParent; Recipes=InRecipes; Phase=1;
        for (int32 i=0;i<6;++i)
        {
            PendingStations.Add(FCk_Handle_PendingEntityScript()); Stations.Add(FCk_Handle());
            Targets.Add(FCk_Handle()); Carriers.Add(FCk_Handle());
            Scripts.Add(nullptr); Frames.Add(FCkGymStation_BaselineCreatorLedger());
            Constructed.Add(0); Completed.Add(0);
        }
    }
    ECkGym_SharedStationBegin TryBeginLocal(FCk_Handle InWorld, FCk_Handle InParent,
        const TArray<FCk_GymStation_SpawnParams>&in InRecipes,
        FCk_Delegate_Request_OnCompleted OnCompleted,
        FCk_Delegate_EntityScript_Constructed OnConstructed)
    {
        LastAdmissionRejection="";
        if (Phase!=0) { RejectAdmission(f"phase-busy phase={Phase}"); return ECkGym_SharedStationBegin::Busy; }
        if (!Fresh()) { RejectAdmission("fresh-state"); Fail(); RetirementUnproven=true; return ECkGym_SharedStationBegin::Rejected; }
        FString PreflightFailure;
        if (!Preflight(InWorld,InParent,InRecipes,OnCompleted,OnConstructed,PreflightFailure))
        { RejectAdmission(PreflightFailure); return ECkGym_SharedStationBegin::Rejected; }
        if (InParent==InWorld)
        { RejectAdmission("local-parent-is-world"); return ECkGym_SharedStationBegin::Rejected; }
        if (!utils_entity_lifetime::Has_LifetimeOwner(InParent))
        { RejectAdmission("local-parent-has-owner"); return ECkGym_SharedStationBegin::Rejected; }
        // Validate the local parent in this world without adopting its unrelated
        // direct children into this station shell cohort.
        TArray<FCk_Handle> UnusedChildren;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
            InParent,InWorld,UnusedChildren))
        { RejectAdmission("local-parent-allocated-census"); return ECkGym_SharedStationBegin::Rejected; }
        Prefill(InWorld,InParent,InRecipes);
        return BeginStations(OnCompleted,OnConstructed);
    }
    private ECkGym_SharedStationBegin BeginStations(
        FCk_Delegate_Request_OnCompleted OnCompleted,
        FCk_Delegate_EntityScript_Constructed OnConstructed)
    {
        CohortOwner=utils_entity_lifetime::Request_CreateEntity(Parent);
        if (CohortOwner!=FCk_Handle() && !Remember(CohortOwner,Parent))
        { RejectAdmission("cohort-owner-remember"); return ECkGym_SharedStationBegin::Rejected; }
        if (!Live(CohortOwner)) { RejectAdmission("cohort-owner-create"); Fail(); return ECkGym_SharedStationBegin::Rejected; }
        for (int32 i=0;i<6;++i)
        {
            if (Phase!=1) { RejectAdmission(f"spawn-phase slot={i} phase={Phase}"); Fail(); return ECkGym_SharedStationBegin::Rejected; }
            if (TerminalFailure) { RejectAdmission(f"spawn-terminal slot={i}"); Fail(); return ECkGym_SharedStationBegin::Rejected; }
            if (!Live(World)) { RejectAdmission(f"spawn-world-live slot={i}"); Fail(); return ECkGym_SharedStationBegin::Rejected; }
            if (!Live(Parent)) { RejectAdmission(f"spawn-parent-live slot={i}"); Fail(); return ECkGym_SharedStationBegin::Rejected; }
            if (!Live(CohortOwner)) { RejectAdmission(f"spawn-owner-live slot={i}"); Fail(); return ECkGym_SharedStationBegin::Rejected; }
            if (!utils_entity_lifetime::Get_CanCreateEntity(CohortOwner))
            { RejectAdmission(f"spawn-owner-can-create slot={i}"); Fail(); return ECkGym_SharedStationBegin::Rejected; }
            auto Params=FInstancedStruct::Make(Recipes[i]);
            if (!Params.IsValid()) { RejectAdmission(f"spawn-params-invalid slot={i}"); Fail(); return ECkGym_SharedStationBegin::Rejected; }
            if (Params.GetScriptStruct()!=FCk_GymStation_SpawnParams)
            { RejectAdmission(f"spawn-params-class slot={i}"); Fail(); return ECkGym_SharedStationBegin::Rejected; }
            auto Pending=UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                CohortOwner,UCk_EntityScript_GymStation,Params,OnCompleted);
            if (!utils_pending_entity_script::Get_IsValid(Pending))
            { RejectAdmission(f"spawn-pending slot={i}"); Fail(); return ECkGym_SharedStationBegin::Rejected; }
            auto Target=Pending.Get_EntityUnderConstruction();
            // Freeze the target before querying its transient request carrier.
            PendingStations[i]=Pending; Targets[i]=Target;
            if (!Remember(Target,CohortOwner)) { RejectAdmission(f"spawn-target-remember slot={i}"); return ECkGym_SharedStationBegin::Rejected; }
            FCk_Handle Carrier;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryCapture_StationSpawnCarrierWithParent(
                Target,CohortOwner,UCk_EntityScript_GymStation,World,Parent,Carrier))
            { RejectAdmission(f"spawn-carrier-capture slot={i}"); Fail(); return ECkGym_SharedStationBegin::Rejected; }
            Carriers[i]=Carrier;
            if (!Remember(Carrier,Target)) { RejectAdmission(f"spawn-carrier-remember slot={i}"); return ECkGym_SharedStationBegin::Rejected; }
            utils_pending_entity_script::Promise_OnConstructed(Pending,OnConstructed);
            if (TerminalFailure || Phase!=1)
            { RejectAdmission(f"spawn-constructed-bind slot={i} phase={Phase} terminal={TerminalFailure}"); return ECkGym_SharedStationBegin::Rejected; }
        }
        return ECkGym_SharedStationBegin::Started;
    }

    private int32 FindTarget(FCk_Handle H) const
    {
        if (!Allocated(H) || !Arrays()) { return -1; }
        int32 Found=-1;
        for (int32 i=0;i<6;++i)
        { if (Targets[i]==H) { if (Found>=0) { return -1; } Found=i; } }
        return Found;
    }
    private bool CaptureScript(int32 i)
    {
        if (i<0 || i>=6 || !Live(Targets[i]) || !utils_entity_script::Has(Targets[i]))
        { return false; }
        auto S=Cast<UCk_EntityScript_GymStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(Targets[i]));
        if (ck::Is_NOT_Valid(S) || S.GetClass()!=UCk_EntityScript_GymStation ||
            UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(S)!=Targets[i])
        { return false; }
        if (ck::IsValid(Scripts[i]) && Scripts[i]!=S) { Fail(); return false; }
        Scripts[i]=S;
        return true;
    }
    private bool CaptureBaseline(int32 i, bool RequireFinished)
    {
        if (!Arrays() || i<0 || i>=6 || !CaptureScript(i)) { return false; }
        FCkGymStation_BaselineCreatorLedger Current;
        if (!Scripts[i].TryGet_BaselineCreatorLedger(Current) ||
            Current.Station!=Targets[i] || !Current.Started ||
            Current.Handles.Num()!=28 || Current.Parents.Num()!=28 ||
            Current.ExtraReturns.Num()!=Current.ExtraParents.Num()) { Fail(); return false; }
        // Record every nondefault creator return before interpreting the frame.
        bool Good=true;
        TArray<FCk_Handle> FrameSeen;
        for (int32 j=0;j<28;++j)
        {
            auto H=Current.Handles[j];
            if (H==FCk_Handle())
            { if (Frames[i].Handles.Num()==28 && Frames[i].Handles[j]!=FCk_Handle()) { Good=false; } continue; }
            if (FrameSeen.Contains(H)) { Good=false; }
            FrameSeen.Add(H);
            auto Declared=Current.Parents[j];
            auto Expected=j<17?Targets[i]:j<27?Current.Handles[j-17]:Targets[i];
            // Retain the producer's exact declared edge before checking its role.
            if (!Remember(H,Declared)) { Good=false; }
            if (Declared!=Expected) { RetainRejected(H,Declared); Good=false; }
            if (Frames[i].Handles.Num()==28 && Frames[i].Handles[j]!=FCk_Handle() &&
                (Frames[i].Handles[j]!=H || Frames[i].Parents[j]!=Current.Parents[j])) { Good=false; }
        }
        for (int32 j=0;j<Current.ExtraReturns.Num();++j)
        {
            RetainRejected(Current.ExtraReturns[j],Current.ExtraParents[j]);
            if (!Remember(Current.ExtraReturns[j],Current.ExtraParents[j])) { Good=false; }
        }
        if (!Good || Current.Broken || Current.UnsupportedTopologyAttempted ||
            !Current.ExtraReturns.IsEmpty() || (RequireFinished && !Current.Finished))
        { Fail(); return false; }
        Frames[i]=Current;
        return true;
    }
    bool TryRecord_Constructed(FCk_Handle H, FCk_Handle_EntityScript Script)
    {
        int32 i=FindTarget(H);
        if (TerminalFailure || Phase!=1 || i<0 || Constructed[i]!=0 ||
            FCk_Handle(Script)!=H || !CaptureBaseline(i,true))
        { Fail(); return false; }
        Constructed[i]=1; Stations[i]=H;
        return true;
    }
    private void LogCompletionRejection(int32 i,
        ECk_Request_OperationResult Result, bool Captured)
    {
        bool HasRaw=false, Started=false, Finished=false, Broken=false;
        bool Unsupported=false, Closed=false;
        int32 Occupied=-1, FirstMissing=-1, ParentMismatches=-1;
        int32 Aliases=-1, Extras=-1;
        if (i>=0 && i<6 && Scripts.Num()==6 && Targets.Num()==6 &&
            ck::IsValid(Scripts[i]))
        {
            FCkGymStation_BaselineCreatorLedger Raw;
            HasRaw=Scripts[i].TryGet_BaselineCreatorLedger(Raw);
            if (HasRaw)
            {
                Started=Raw.Started; Finished=Raw.Finished; Broken=Raw.Broken;
                Unsupported=Raw.UnsupportedTopologyAttempted;
                Closed=Raw.PresentationClosed; Extras=Raw.ExtraReturns.Num();
                if (Raw.Handles.Num()==28 && Raw.Parents.Num()==28)
                {
                    Occupied=0; ParentMismatches=0; Aliases=0;
                    for (int32 j=0;j<28;++j)
                    {
                        auto H=Raw.Handles[j];
                        if (H==FCk_Handle())
                        { if (FirstMissing<0) { FirstMissing=j; } continue; }
                        ++Occupied;
                        auto Expected=j<17?Targets[i]:j<27?Raw.Handles[j-17]:Targets[i];
                        if (Raw.Parents[j]!=Expected) { ++ParentMismatches; }
                        for (int32 k=0;k<j;++k)
                        { if (Raw.Handles[k]==H) { ++Aliases; break; } }
                    }
                }
            }
        }
        System::PrintString(f"[SceneNodeStationCompletion] phase={Phase} slot={i} result={int32(Result)} captured={Captured} terminal={TerminalFailure} unproven={RetirementUnproven} raw={HasRaw} started={Started} finished={Finished} broken={Broken} unsupported={Unsupported} closed={Closed} occupied={Occupied} firstMissing={FirstMissing} parentMismatch={ParentMismatches} aliases={Aliases} extras={Extras}",
            false,true,FLinearColor::White,0.0f,NAME_None);
    }
    bool TryRecord_Completed(FCk_Handle H, FCk_Handle CompletedEntity,
        ECk_Request_OperationResult Result)
    {
        int32 i=FindTarget(H);
        if (TerminalFailure || Phase!=1 || i<0 || Completed[i]!=0 ||
            CompletedEntity!=H || !Live(H) ||
            !utils_entity_lifetime::Has_LifetimeOwner(H) ||
            utils_entity_lifetime::Get_LifetimeOwner(H)!=CohortOwner)
        { LogCompletionRejection(i,Result,false); Fail(); return false; }
        // Even a real failed completion can follow partial DoConstruct returns.
        // Read only the exact live target before rejecting its failed receipt.
        bool Captured=CaptureBaseline(i,false);
        if (!Captured || Result!=ECk_Request_OperationResult::Succeeded)
        { LogCompletionRejection(i,Result,Captured); Fail(); return false; }
        Completed[i]=1;
        return true;
    }
    bool TryRequireOwned(FCk_Handle H, FCk_Handle ExpectedParent)
    {
        if (TerminalFailure || (Phase!=1 && Phase!=2) || !Arrays())
        { Fail(); return false; }
        int32 Index=Known.FindIndex(H);
        if (Index<0 || Index>=Parents.Num() || Parents[Index]!=ExpectedParent ||
            !Live(H) || !Live(ExpectedParent) ||
            !utils_entity_lifetime::Has_LifetimeOwner(H) ||
            utils_entity_lifetime::Get_LifetimeOwner(H)!=ExpectedParent)
        { Fail(); return false; }
        return true;
    }
    // -1 rejects structure, 0 awaits component readiness, 1 is ready.
    // This read never discovers or repairs an identity.
    private int32 Get_PanelStatus(bool RequireAuthoredDescription) const
    {
        if (!Arrays()) { return -1; }
        for (int32 i=0;i<6;++i)
        {
            if (!Live(Stations[i]) || ck::Is_NOT_Valid(Scripts[i]) ||
                UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(Scripts[i])!=Stations[i] ||
                !Get_IsExactLocalRecipe(Recipes[i],i,Parent) ||
                !Stations[i].Has_Fragment(FCkGym_Station_Anchors) ||
                !utils_transform::Has(Stations[i])) { return -1; }
            FCkGym_StationVisibleGeometry G;
            if (!Scripts[i].TryGet_VisibleGeometry(G)) { return 0; }
            // OnAdded can expose authored text before OnDisplayTick has
            // created the fragment the local controller must stage.
            if (!Stations[i].Has_Fragment(FCkGym_Station_TitleAndDescription))
            { return 0; }
            if (G.AutoSize ||
                Scripts[i].AutoSize ||
                !Scripts[i].InitialTransform.Equals(Recipes[i].InitialTransform,0.0) ||
                Scripts[i].Width!=Recipes[i].Width ||
                Scripts[i].Depth!=Recipes[i].Depth ||
                Scripts[i].Height!=Recipes[i].Height ||
                G.Pieces.Num()!=10 || G.Anchors.Num()!=7 ||
                G.Width!=Recipes[i].Width || G.Depth!=Recipes[i].Depth ||
                G.Height!=Recipes[i].Height ||
                !G.StationWorldTransform.Equals(Recipes[i].InitialTransform,0.0))
            { return -1; }
            FTransform Panel;
            if (!CkSceneNodeGymCatalog::TryGet_PanelCenter(i,Panel) ||
                !G.Anchors[6].Equals(Panel,0.0) ||
                G.Pieces[6].Role!=ECkGym_StationVisibleRole::TitleText ||
                G.Pieces[7].Role!=ECkGym_StationVisibleRole::DescriptionText ||
                G.Pieces[8].Role!=ECkGym_StationVisibleRole::FloorDescriptionText)
            { return -1; }
            if (!G.Pieces[6].Visible || G.Pieces[6].HiddenInGame ||
                !G.Pieces[7].Visible || G.Pieces[7].HiddenInGame ||
                G.Pieces[6].Text.IsEmpty() ||
                (RequireAuthoredDescription && G.Pieces[7].Text.IsEmpty()) ||
                G.Pieces[6].FontPath.IsEmpty() || G.Pieces[7].FontPath.IsEmpty() ||
                G.Pieces[8].FontPath.IsEmpty())
            { return 0; }
        }
        return 1;
    }

    ECkGym_SharedStationPoll PollConstruction()
    {
        if (TerminalFailure || (Phase!=1 && Phase!=2) || !Arrays())
        { Fail(); return ECkGym_SharedStationPoll::Failed; }
        if (!Live(World) || World!=ck::TransientEntity() || !Live(Parent) ||
            !Live(CohortOwner))
        { Fail(); return ECkGym_SharedStationPoll::Failed; }
        bool Pending=false;
        TArray<FCk_Handle> Distinct;
        if (Distinct.Contains(CohortOwner)) { Fail(); return ECkGym_SharedStationPoll::Failed; }
        Distinct.Add(CohortOwner);
        for (int32 i=0;i<6;++i)
        {
            if (!Live(Targets[i]) || !utils_pending_entity_script::Get_IsValid(PendingStations[i]) ||
                PendingStations[i].Get_EntityUnderConstruction()!=Targets[i] ||
                Distinct.Contains(Targets[i]) || Distinct.Contains(Carriers[i]) ||
                Targets[i]==Carriers[i] ||
                (Constructed[i]!=0 && Constructed[i]!=1) ||
                (Completed[i]!=0 && Completed[i]!=1))
            { Fail(); return ECkGym_SharedStationPoll::Failed; }
            Distinct.Add(Targets[i]); Distinct.Add(Carriers[i]);
            bool CarrierAllocated=false;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrierWithParent(
                Carriers[i],Targets[i],CohortOwner,World,Parent,CarrierAllocated))
            { Fail(); return ECkGym_SharedStationPoll::Failed; }
            // A live script may have returned baseline children before either
            // receipt fires; retain those raw IDs before another Pending tick.
            if (utils_entity_script::Has(Targets[i]))
            {
                if (!CaptureScript(i)) { Fail(); return ECkGym_SharedStationPoll::Failed; }
                FCkGymStation_BaselineCreatorLedger Probe;
                if (Scripts[i].TryGet_BaselineCreatorLedger(Probe) &&
                    !CaptureBaseline(i,false))
                { Fail(); return ECkGym_SharedStationPoll::Failed; }
            }
            if (Constructed[i]==0 || Completed[i]==0) { Pending=true; continue; }
            if (!CaptureBaseline(i,true)) { Fail(); return ECkGym_SharedStationPoll::Failed; }
            auto F=Frames[i];
            if (!F.Finished || F.Broken || F.UnsupportedTopologyAttempted ||
                F.PresentationClosed ||
                F.ExtraReturns.Num()!=0) { Fail(); return ECkGym_SharedStationPoll::Failed; }
            for (int32 j=0;j<28;++j)
            {
                auto H=F.Handles[j]; auto P=F.Parents[j];
                if (H==FCk_Handle() || Distinct.Contains(H) || !TryRequireOwned(H,P))
                { Fail(); return ECkGym_SharedStationPoll::Failed; }
                Distinct.Add(H);
            }
            for (auto Tag:Recipes[i].StationTags)
            {
                auto Matches=utils_entity_tag::ForEach_Entity(World,Tag);
                if (Matches.IsEmpty()) { Pending=true; continue; }
                if (Matches.Num()!=1 || Matches[0]!=Targets[i])
                { Fail(); return ECkGym_SharedStationPoll::Failed; }
            }
        }
        if (Pending) { return ECkGym_SharedStationPoll::Pending; }
        if (!Get_HasExactKnownProof()) { Fail(); return ECkGym_SharedStationPoll::Failed; }
        for (auto H:Known)
        {
            if (!Live(H)) { continue; } // A completed factory carrier may be gone.
            if (!UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveMotionEntity(H,World))
            { Fail(); return ECkGym_SharedStationPoll::Failed; }
        }
        int32 PanelStatus=Get_PanelStatus(true);
        if (PanelStatus<0) { Fail(); return ECkGym_SharedStationPoll::Failed; }
        if (PanelStatus==0) { return ECkGym_SharedStationPoll::Pending; }
        Phase=2; return ECkGym_SharedStationPoll::Ready;
    }

    // Pure controller/source readiness: no polling, enrollment, failure latch,
    // new request or destroy. Check current raw frames against frozen captures.
    bool Get_HasExactReadyCohort() const
    {
        if (Phase!=2 || TerminalFailure || RetirementUnproven || !Arrays() ||
            !Live(World) || World!=ck::TransientEntity() || !Live(Parent) ||
            !Live(CohortOwner) || !Get_HasExactKnownProof()) { return false; }
        TArray<FCk_Handle> Distinct;
        if (Distinct.Contains(CohortOwner)) { return false; }
        Distinct.Add(CohortOwner);
        for (int32 i=0;i<6;++i)
        {
            if (Constructed[i]!=1 || Completed[i]!=1 || !Live(Targets[i]) ||
                Stations[i]!=Targets[i] || Distinct.Contains(Targets[i]) ||
                Carriers[i]==FCk_Handle() || Carriers[i]==Targets[i] ||
                Distinct.Contains(Carriers[i]) ||
                !utils_pending_entity_script::Get_IsValid(PendingStations[i]) ||
                PendingStations[i].Get_EntityUnderConstruction()!=Targets[i] ||
                ck::Is_NOT_Valid(Scripts[i]) || Scripts[i].GetClass()!=UCk_EntityScript_GymStation ||
                UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(Scripts[i])!=Targets[i])
            { return false; }
            Distinct.Add(Targets[i]); Distinct.Add(Carriers[i]);
            bool CarrierAllocated=false;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrierWithParent(
                Carriers[i],Targets[i],CohortOwner,World,Parent,CarrierAllocated)) { return false; }
            FCkGymStation_BaselineCreatorLedger Current;
            if (!Scripts[i].TryGet_BaselineCreatorLedger(Current) ||
                Current.Station!=Targets[i] || !Current.Started || !Current.Finished ||
                Current.Broken || Current.UnsupportedTopologyAttempted ||
                Current.PresentationClosed || Current.Handles.Num()!=28 ||
                Current.Parents.Num()!=28 || !Current.ExtraReturns.IsEmpty() ||
                !Current.ExtraParents.IsEmpty() || Frames[i].Handles.Num()!=28 ||
                Frames[i].Parents.Num()!=28 || !Frames[i].Finished)
            { return false; }
            for (int32 j=0;j<28;++j)
            {
                auto H=Current.Handles[j]; auto P=Current.Parents[j];
                auto Expected=j<17?Targets[i]:j<27?Current.Handles[j-17]:Targets[i];
                int32 K=Known.FindIndex(H);
                if (H==FCk_Handle() || P!=Expected || Distinct.Contains(H) ||
                    Frames[i].Handles[j]!=H || Frames[i].Parents[j]!=P ||
                    K<0 || K>=Parents.Num() || Parents[K]!=P || !Live(H) ||
                    !utils_entity_lifetime::Has_LifetimeOwner(H) ||
                    utils_entity_lifetime::Get_LifetimeOwner(H)!=P)
                { return false; }
                Distinct.Add(H);
            }
            for (auto Tag:Recipes[i].StationTags)
            {
                auto Matches=utils_entity_tag::ForEach_Entity(World,Tag);
                if (Matches.Num()!=1 || Matches[0]!=Targets[i]) { return false; }
            }
        }
        if (!RejectedReturns.IsEmpty() || Distinct.Num()!=Known.Num()) { return false; }
        for (auto H:Known)
        { if (!Live(H) && !Carriers.Contains(H)) { return false; }
          if (Live(H) && !UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveMotionEntity(H,World))
          { return false; } }
        return Get_PanelStatus(false)==1;
    }

    private bool Get_HasExactKnownProof() const
    {
        if (Known.Num()!=Parents.Num() ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
                Known,Known,World)) { return false; }
        for (int32 i=0;i<Known.Num();++i)
        {
            auto H=Known[i];
            if (!Allocated(H)) { continue; }
            if (H==World ||
                !UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveAllocatedMotionEntity(H,World) ||
                utils_entity_lifetime::Get_LifetimeOwner(
                H,ECk_PendingKill_Policy::IncludePendingKill)!=Parents[i]) { return false; }
        }
        return true;
    }
    // Source invokes this before motion quiescence. Only previously captured
    // exact scripts are touched; it never scans, discovers, or destroys entities.
    bool Request_CloseKnownPresentation()
    {
        if (Phase==0 && Scripts.IsEmpty()) { return true; }
        bool Good=Scripts.Num()==6;
        for (auto S:Scripts)
        {
            if (ck::Is_NOT_Valid(S)) { continue; }
            if (!S.Request_CloseBaselinePresentation()) { Good=false; }
        }
        if (!Good) { TerminalFailure=true; RetirementUnproven=true; }
        return Good;
    }
    private bool CloseScripts()
    {
        bool Good=Request_CloseKnownPresentation();
        if (!Arrays()) { return false; }
        for (int32 i=0;i<Scripts.Num();++i)
        {
            if (ck::IsValid(Scripts[i]) &&
                !Scripts[i].Request_CloseBaselinePresentation()) { Good=false; }
            // No first script or raw creator enrollment after retirement starts.
            if (Frames[i].Started==false && Allocated(Targets[i]))
            {
                TArray<FCk_Handle> Children;
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                    Targets[i],World,Children)) { Good=false; }
                for (auto Child:Children)
                { if (!Known.Contains(Child)) { Good=false; } }
            }
        }
        return Good;
    }
    void Request_Retire()
    {
        if (Phase==3) { return; }
        if (Phase==0) { return; }
        TerminalFailure=true;
        bool Closed=CloseScripts();
        Phase=3;
        Retiring=Known;
        if (!Closed || !Get_HasExactKnownProof())
        { RetirementUnproven=true; return; }
        // Only a proved frozen graph can be destroyed. Each synchronous callback
        // is followed by the same whole-read-set proof before another request.
        for (;;)
        {
            bool LiveAny=false, Progress=false;
            for (auto H:Retiring)
            {
                if (!Live(H)) { continue; }
                LiveAny=true;
                if (!Get_HasExactKnownProof()) { RetirementUnproven=true; return; }
                TArray<FCk_Handle> Children;
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                    H,World,Children)) { RetirementUnproven=true; return; }
                bool Leaf=true;
                for (auto Child:Children)
                { if (!Known.Contains(Child)) { RetirementUnproven=true; return; }
                  if (Live(Child)) { Leaf=false; } }
                if (!Leaf) { continue; }
                utils_entity_lifetime::Request_DestroyEntity(H);
                if (Live(H) || !Get_HasExactKnownProof())
                { RetirementUnproven=true; return; }
                Progress=true;
            }
            if (!LiveAny) { return; }
            if (!Progress) { RetirementUnproven=true; return; }
        }
    }
    bool PollRetirement()
    {
        if (Phase==0) { return Fresh(); }
        if (Phase!=3) { return false; }
        if (RetirementUnproven || !Get_HasExactKnownProof())
        { RetirementUnproven=true; return false; }
        for (auto H:Retiring) { if (Allocated(H)) { return false; } }
        // Keep identities, frames and scripts until the full all-gone proof.
        World=FCk_Handle(); Parent=FCk_Handle();
        CohortOwner=FCk_Handle(); Recipes.Empty(); PendingStations.Empty(); Stations.Empty();
        Targets.Empty(); Carriers.Empty(); Scripts.Empty(); Frames.Empty(); Constructed.Empty();
        Completed.Empty(); Known.Empty(); Parents.Empty(); RejectedReturns.Empty();
        RejectedParents.Empty(); Retiring.Empty(); Phase=0;
        TerminalFailure=false;
        return true;
    }
}
