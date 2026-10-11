// Language=angelscript

struct FCkChainGym_Fixture
{
    FCk_Handle_Transform Root;
    FCk_Handle_Transform Head;
    FCk_Handle_Chain Chain;
    FCk_Handle_Chain SplitChain;
    FCk_Handle_Transform SplineOwner;
    FCk_Handle_Spline Spline;
    FCk_Handle_Tween Travel;
    TArray<FCk_Handle_Transform> Links;
    TArray<FCk_Handle_Transform> Lamps;
    FVector Origin;
    int32 Station = 0;
    float32 Spacing = 10.0f;
    bool Moving = false;
    bool FollowingPawn = false;
    private FCk_Handle _Owner;
    private TArray<FCk_Handle> _TravelReturns;
    private bool _CreateAttempted = false;
    private bool _Started = false;
    private bool _Blocked = false;
    private bool _DestroyRequested = false;

    private bool Get_Has(FCk_Handle H) const
    {
        return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) &&
            ck::IsValid(H) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    private bool Get_HasChild(FCk_Handle H, FCk_Handle Parent) const
    {
        return Get_Has(H) && Get_Has(Parent) &&
            utils_entity_lifetime::Has_LifetimeOwner(H) &&
            utils_entity_lifetime::Get_LifetimeOwner(H) == Parent;
    }

    private bool Get_HasFixedTree() const
    {
        if (!_CreateAttempted || _Blocked || _DestroyRequested ||
            Station < 0 || Station > 3 ||
            !Math::IsFinite(Spacing) || Spacing < 4.0f || Spacing > 25.0f ||
            Origin.ContainsNaN() || !Get_HasChild(Root, _Owner) ||
            !utils_transform::Has(Root) || !Get_HasChild(Head, Root) ||
            !utils_transform::Has(Head) || !Get_HasChild(Chain, Head) ||
            !UCk_ChainGym_Runtime_UE::Get_IsLiveChain(Chain) ||
            !Get_HasChild(SplineOwner, Root) ||
            !utils_transform::Has(SplineOwner) ||
            !Get_HasChild(Spline, SplineOwner) || !utils_spline::Has(Spline))
        { return false; }
        int32 Count = Station == 1 ? 12 : 8;
        if (Links.Num() != Count || Lamps.Num() != (Station == 3 ? 8 : 0))
        { return false; }
        for (int32 I = 0; I < Count; ++I)
        {
            if (!Get_HasChild(Links[I], Root) || !utils_transform::Has(Links[I]))
            { return false; }
            if (Station == 3 && (!Get_HasChild(Lamps[I], Links[I]) ||
                !utils_transform::Has(Lamps[I]) ||
                !utils_scene_node::Has(Lamps[I])))
            { return false; }
        }
        if (SplitChain != FCk_Handle_Chain() &&
            (!Get_HasChild(SplitChain, Links[3]) ||
                !UCk_ChainGym_Runtime_UE::Get_IsLiveChain(SplitChain)))
        { return false; }
        return true;
    }

    bool Create(FCk_Handle InOwner, FVector InOrigin, int32 InStation, float32 InSpacing)
    {
        if (_CreateAttempted || _Blocked || _DestroyRequested ||
            !Get_Has(InOwner) || !utils_entity_lifetime::Get_CanCreateEntity(InOwner) ||
            InOrigin.ContainsNaN() || InStation < 0 || InStation > 3 ||
            !Math::IsFinite(InSpacing) || InSpacing < 4.0f || InSpacing > 25.0f)
        { return false; }
        _CreateAttempted = true;
        _Owner = InOwner;
        Origin = InOrigin;
        Station = InStation;
        Spacing = InSpacing;
        Links.Reset();
        Lamps.Reset();
        Root = utils_transform::Create(InOwner, FTransform(Origin), ECk_Replication::DoesNotReplicate);
        if (!Get_HasChild(Root, InOwner) || !utils_transform::Has(Root))
        { return false; }
        Head = utils_transform::Create(Root, FTransform(Origin), ECk_Replication::DoesNotReplicate);
        if (!Get_HasChild(Head, Root) || !utils_transform::Has(Head))
        { return false; }
        auto Params = FCk_Chain_Spec(InStation == 1 ? ECk_Chain_Solver::DistanceConstraint : ECk_Chain_Solver::PathHistory);
        Params.Set_SampleSpacingCm(InSpacing);
        Params.Set_TeleportDistanceCm(500.0f);
        Params.Set_StartingState(ECk_EnableDisable::Disable);
        if (InStation == 2)
        {
            Params.Set_HistorySeed(ECk_Chain_HistorySeed::HoldUntilCovered);
        }
        Chain = utils_chain::Add(Head, Params);
        if (!Get_HasChild(Chain, Head))
        { return false; }
        auto Count = InStation == 1 ? 12 : 8;
        for (auto Index = 0; Index < Count; Index++)
        {
            auto Location = Origin + FVector(-60.0 * (Index + 1), InStation == 2 ? 180.0 : 0.0, 0.0);
            auto Link = utils_transform::Create(Root, FTransform(Location), ECk_Replication::DoesNotReplicate);
            Links.Add(Link);
            if (!Get_HasChild(Link, Root) || !utils_transform::Has(Link))
            { return false; }
            auto LinkParams = FCk_ChainLink_Spec(float32(60.0 * (Index + 1)));
            LinkParams.Set_Orientation(ECk_Chain_LinkOrientation::FollowPath);
            if (utils_chain::Request_AttachLink(Chain,
                FCk_Request_Chain_AttachLink(Link, LinkParams)) != Chain)
            { return false; }
            if (InStation == 3)
            {
                auto Lamp = utils_scene_node::Create(Link, FTransform(FVector(0.0, 0.0, 65.0)));
                Lamps.Add(Lamp.As_Transform());
                if (!Get_HasChild(Lamps[Index], Link) ||
                    !utils_transform::Has(Lamps[Index]) ||
                    !utils_scene_node::Has(Lamps[Index]))
                { return false; }
            }
        }
        auto Points = TArray<FVector>();
        for (auto Index = 0; Index < 48; Index++)
        {
            auto Angle = 2.0 * Math::PI * Index / 48.0;
            Points.Add(FVector(650.0 * Math::Sin(Angle), 350.0 * Math::Sin(2.0 * Angle), 0.0));
        }
        SplineOwner = utils_transform::Create(Root, FTransform(Origin), ECk_Replication::DoesNotReplicate);
        if (!Get_HasChild(SplineOwner, Root) || !utils_transform::Has(SplineOwner))
        { return false; }
        Spline = utils_spline::Create(SplineOwner, utils_spline::Make_Params_FromPoints(Points, true));
        return Get_HasChild(Spline, SplineOwner) && utils_spline::Has(Spline);
    }

    bool Get_IsPrepared() const
    {
        if (!Get_HasFixedTree() || _Started || SplitChain != FCk_Handle_Chain() ||
            Travel != FCk_Handle_Tween() || utils_chain::Get_IsEnabled(Chain))
        { return false; }
        auto Roster = utils_chain::Get_Links(Chain);
        if (Roster.Num() != Links.Num()) { return false; }
        for (int32 I = 0; I < Roster.Num(); ++I)
        {
            if (FCk_Handle(Roster[I]) != FCk_Handle(Links[I]) ||
                !utils_chain_link::Has(Links[I]) ||
                utils_chain_link::Get_Chain(Roster[I]) != Chain)
            { return false; }
        }
        return true;
    }

    bool Request_StartPrepared()
    {
        if (_Started) { return Get_HasFixedTree(); }
        if (!Get_IsPrepared()) { return false; }
        _Started = true;
        if (utils_chain::Request_EnableDisable(Chain,
                FCk_Request_Chain_EnableDisable(ECk_EnableDisable::Enable)) != Chain)
        { return false; }
        return Station == 2 || Request_StartTravel();
    }

    bool Request_StartTravel()
    {
        if (!_Started || !Get_HasFixedTree() || FollowingPawn)
        { return false; }
        auto Length = utils_spline::Get_Length(Spline);
        if (!Math::IsFinite(Length) || Length <= 0.0f)
        { return false; }
        if (Travel != FCk_Handle_Tween())
        {
            if (!Get_HasChild(Travel, Head) || !utils_tween::Has(Travel))
            { return false; }
            utils_tween::Resume(Travel);
        }
        else
        {
            auto Created = utils_tween::Create_TweenEntityTransform_FollowSpline(
                Head, Spline, 18.0f, ECk_Tween_SplineOrientation::OrientToSpline,
                ECk_TweenEasing::Linear, ECk_TweenLoopType::Restart, -1);
            _TravelReturns.Add(FCk_Handle(Created));
            Travel = Created;
            if (!Get_HasChild(Travel, Head) || !utils_tween::Has(Travel))
            { return false; }
        }
        Moving = true;
        return true;
    }

    private bool DoStopTravel()
    {
        if (Travel == FCk_Handle_Tween()) { Moving = false; return true; }
        if (!Get_HasChild(Travel, Head) || !utils_tween::Has(Travel))
        { return false; }
        utils_tween::Stop(Travel, ECk_TweenStopBehavior::SelfDestruct);
        Travel = FCk_Handle_Tween();
        Moving = false;
        return true;
    }

    bool Request_TogglePawnFollow(FCk_Handle_Transform InPawn)
    {
        if (!_Started || Station != 1 || !Get_HasFixedTree())
        { return false; }
        if (FollowingPawn) { return Request_StopPawnFollow(); }
        if (!Get_Has(InPawn) || !utils_transform::Has(InPawn) ||
            !DoStopTravel()) { return false; }
        auto Created = utils_tween::Create_TweenEntityLocation_FollowTarget(
            Head, InPawn, 0.8f, ECk_TweenEasing::Linear,
            ECk_TweenLoopType::Restart, -1);
        _TravelReturns.Add(FCk_Handle(Created));
        Travel = Created;
        if (!Get_HasChild(Travel, Head) || !utils_tween::Has(Travel))
        { return false; }
        FollowingPawn = true;
        Moving = true;
        return true;
    }

    bool Request_StopPawnFollow()
    {
        if (!_Started || Station != 1 || !FollowingPawn ||
            !Get_HasFixedTree() || !DoStopTravel())
        { return false; }
        FollowingPawn = false;
        return Request_StartTravel();
    }

    bool Request_Teleport()
    {
        if (!_Started || Station != 0 || !Get_HasFixedTree() ||
            !Get_HasChild(Travel, Head) || !utils_tween::Has(Travel))
        { return false; }
        auto Current = utils_transform::Get_EntityCurrentLocation(Head);
        if (Current.ContainsNaN()) { return false; }
        utils_tween::Pause(Travel);
        Moving = false;
        utils_transform::Request_SetLocation(Head,
            FCk_Request_Transform_SetLocation(Current + FVector(0.0, 850.0, 0.0)));
        return true;
    }

    bool Request_SplitAtFour()
    {
        if (!_Started || Station != 0 || !Get_HasFixedTree())
        { return false; }
        if (SplitChain != FCk_Handle_Chain())
        {
            FCkChainGym_StationFrame Complete;
            return TryGet_Frame(Complete) && Complete.HasSplit &&
                Complete.LinkCount == 3 && Complete.SplitLinkCount == 4;
        }
        auto Roster = utils_chain::Get_Links(Chain);
        if (Roster.Num() != 8 || FCk_Handle(Roster[3]) != FCk_Handle(Links[3]))
        { return false; }
        SplitChain = utils_chain::Request_Split(Chain,
            FCk_Request_Chain_Split(Roster[3]));
        return Get_HasChild(SplitChain, Links[3]);
    }

    bool Request_ReseedHistory()
    {
        if (!_Started || Station != 0 || !Get_HasFixedTree())
        { return false; }
        return utils_chain::Request_ReseedHistory(
            Chain, FCk_Request_Chain_ReseedHistory()) == Chain;
    }

    bool TryGet_Frame(FCkChainGym_StationFrame&out Out)
    {
        Out = FCkChainGym_StationFrame();
        if (!_Started || !Get_HasFixedTree() ||
            !utils_chain::Get_IsEnabled(Chain) ||
            (SplitChain != FCk_Handle_Chain() &&
                !utils_chain::Get_IsEnabled(SplitChain)) ||
            (Station != 2 && (!Get_HasChild(Travel, Head) ||
                !utils_tween::Has(Travel))) ||
            (Station == 2 && Travel != FCk_Handle_Tween() &&
                (!Get_HasChild(Travel, Head) || !utils_tween::Has(Travel))))
        { return false; }
        TArray<FVector> History;
        TArray<FVector> SplitHistory;
        TArray<FVector> Targets;
        TArray<bool> HasTargets;
        TArray<bool> Held;
        if (!UCk_ChainGym_Runtime_UE::TryGet_DebugValues(
                Chain, SplitChain, Links, History, SplitHistory,
                Targets, HasTargets, Held) ||
            Targets.Num() != Links.Num() || HasTargets.Num() != Links.Num() ||
            Held.Num() != Links.Num())
        { return false; }
        FCkChainGym_StationFrame Frame;
        Frame.Index = Station;
        Frame.Origin = Origin;
        Frame.Head = utils_transform::Get_EntityCurrentTransform(Head);
        Frame.Spacing = Spacing;
        Frame.Moving = Moving;
        Frame.FollowingPawn = FollowingPawn;
        Frame.History = History;
        Frame.SplitHistory = SplitHistory;
        Frame.HasSplit = SplitChain != FCk_Handle_Chain();
        Frame.LinkCount = utils_chain::Get_NumLinks(Chain);
        Frame.SplitLinkCount = Frame.HasSplit ? utils_chain::Get_NumLinks(SplitChain) : 0;
        if (Frame.Head.ContainsNaN() || !Frame.Head.IsValid())
        { return false; }
        for (int32 I = 0; I < Links.Num(); ++I)
        {
            FCkChainGym_LinkFrame LinkFrame;
            LinkFrame.Pose = utils_transform::Get_EntityCurrentTransform(Links[I]);
            LinkFrame.TargetLocation = Targets[I];
            LinkFrame.HasTargetPose = HasTargets[I];
            LinkFrame.IsHeld = Held[I];
            if (LinkFrame.Pose.ContainsNaN() || !LinkFrame.Pose.IsValid())
            { return false; }
            Frame.Links.Add(LinkFrame);
        }
        for (auto Lamp : Lamps)
        {
            auto Pose = utils_transform::Get_EntityCurrentTransform(Lamp);
            if (Pose.ContainsNaN() || !Pose.IsValid()) { return false; }
            Frame.Lamps.Add(Pose);
        }
        Out = Frame;
        return true;
    }

    bool TryGet_RetirementReceipts(TArray<FCk_Handle>&out Out)
    {
        Out.Reset();
        if (!_Started || !Get_HasFixedTree())
        { return false; }
        if (Travel != FCk_Handle_Tween() &&
            (!_TravelReturns.Contains(FCk_Handle(Travel)) ||
                !Get_HasChild(Travel, Head) || !utils_tween::Has(Travel)))
        { return false; }
        TArray<FCk_Handle> Captured;
        Captured.Add(FCk_Handle(Root));
        if (!Get_HasChild(Head, Root)) { return false; }
        Captured.Add(FCk_Handle(Head));
        if (!Get_HasChild(Chain, Head)) { return false; }
        Captured.Add(FCk_Handle(Chain));
        for (auto Link : Links)
        {
            if (!Get_HasChild(Link, Root)) { return false; }
            Captured.Add(FCk_Handle(Link));
        }
        for (int32 I = 0; I < Lamps.Num(); ++I)
        {
            if (!Links.IsValidIndex(I) || !Get_HasChild(Lamps[I], Links[I]))
            { return false; }
            Captured.Add(FCk_Handle(Lamps[I]));
        }
        if (SplineOwner != FCk_Handle_Transform())
        {
            if (!Get_HasChild(SplineOwner, Root)) { return false; }
            Captured.Add(FCk_Handle(SplineOwner));
        }
        if (Spline != FCk_Handle_Spline())
        {
            if (!Get_HasChild(Spline, SplineOwner)) { return false; }
            Captured.Add(FCk_Handle(Spline));
        }
        if (SplitChain != FCk_Handle_Chain())
        {
            if (!Links.IsValidIndex(3) || !Get_HasChild(SplitChain, Links[3]))
            { return false; }
            Captured.Add(FCk_Handle(SplitChain));
        }
        for (auto Returned : _TravelReturns)
        {
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Returned))
            { continue; }
            if (!utils_entity_lifetime::Get_IsPendingDestroy(
                    Returned, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
                !Get_HasChild(Returned, Head)) { return false; }
            Captured.Add(Returned);
        }
        Out = Captured;
        return true;
    }

    void Request_Block()
    {
        if (_Blocked) { return; }
        _Blocked = true;
        if (Get_HasChild(Travel, Head) && utils_tween::Has(Travel))
        { utils_tween::Stop(Travel, ECk_TweenStopBehavior::SelfDestruct); }
        if (Get_HasChild(Chain, Head) &&
            UCk_ChainGym_Runtime_UE::Get_IsLiveChain(Chain))
        {
            utils_chain::Request_EnableDisable(Chain,
                FCk_Request_Chain_EnableDisable(ECk_EnableDisable::Disable));
        }
        if (Links.IsValidIndex(3) && Get_HasChild(SplitChain, Links[3]) &&
            UCk_ChainGym_Runtime_UE::Get_IsLiveChain(SplitChain))
        {
            utils_chain::Request_EnableDisable(SplitChain,
                FCk_Request_Chain_EnableDisable(ECk_EnableDisable::Disable));
        }
        Moving = false;
        FollowingPawn = false;
    }

    bool Request_Destroy()
    {
        Request_Block();
        if (_DestroyRequested || !Get_HasChild(Root, _Owner))
        { return false; }
        _DestroyRequested = true;
        utils_entity_lifetime::Request_DestroyEntity(Root);
        return true;
    }
}
