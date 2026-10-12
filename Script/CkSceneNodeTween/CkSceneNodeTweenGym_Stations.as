// Language=angelscript

USTRUCT()
struct FCkSceneNodeTweenGym_Station_SpawnParams
{
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() int32 Generation = 0;
}

namespace CkSceneNodeTweenGym_Vis
{
    FLinearColor RootColor() { return FLinearColor(1.0f, 0.5f, 0.0f, 1.0f); }
    FLinearColor LeafAligned() { return FLinearColor(1.0f, 0.2f, 1.0f, 0.9f); }
    FLinearColor LeafDrift() { return FLinearColor(1.0f, 1.0f, 0.0f, 1.0f); }
    FLinearColor IntermediateColor(int32 Depth)
    {
        if (Depth == 0) { return FLinearColor(0.2f, 0.9f, 1.0f, 1.0f); }
        if (Depth == 1) { return FLinearColor(0.7f, 0.3f, 1.0f, 1.0f); }
        if (Depth == 2) { return FLinearColor(0.3f, 1.0f, 0.6f, 1.0f); }
        if (Depth == 3) { return FLinearColor(1.0f, 0.7f, 0.2f, 1.0f); }
        return FLinearColor(0.5f, 0.5f, 1.0f, 1.0f);
    }
    FLinearColor EdgeColor(int32 Depth)
    {
        if (Depth == 0) { return FLinearColor(1.0f, 0.7f, 0.2f, 0.8f); }
        if (Depth == 1) { return FLinearColor(0.5f, 0.6f, 1.0f, 0.8f); }
        if (Depth == 2) { return FLinearColor(0.4f, 1.0f, 0.5f, 0.8f); }
        if (Depth == 3) { return FLinearColor(1.0f, 0.4f, 0.4f, 0.8f); }
        return FLinearColor(0.8f, 0.8f, 0.8f, 0.8f);
    }
    void DrawEdge(FVector InStart, FVector InEnd, int32 InDepth)
    {
        utils_pmg_directional_shapes::DrawDashedLine(InStart, InEnd, 15.0f, 10.0f, 2.0f,
            EdgeColor(InDepth), ECk_Plane_Axis::XY, 0.0f);
    }
}

class UCk_EntityScript_SceneNodeTweenGym_BaseStation : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;
    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    UPROPERTY() int32 StationIndex = 0;
    UPROPERTY() bool AuthorityConstructed = false;
    UPROPERTY() bool ProjectionConstructed = false;

    private bool _Authority = false;
    private bool _Activated = false;
    private FCk_Handle_Transform _TweenTarget;
    private FCk_Handle_Transform _Root;
    private TArray<FCk_Handle_SceneNode> _Links;
    private TArray<FTransform> _Offsets;
    private FCk_Handle_Tween _Tween;
    private FCk_Handle_Timer _Tick;
    private FCk_Handle_Pmg_DebugShape _RootShape;
    private TArray<FCk_Handle_Pmg_DebugShape> _LinkShapes;
    private FCk_Handle_Pmg_DebugShape _LeafShape;
    private FLinearColor _LeafColor;
    private bool _ShapesInitialized = false;
    private bool _AutoRunning = true;
    private FCkGym_AutoConfig _AutoConfig;

    private int32 Get_ExpectedLinks() { return StationIndex == 0 ? 1 : (StationIndex == 1 ? 2 : 5); }
    private FName Get_StationTag()
    {
        if (StationIndex == 0) { return n"Gym.SceneNodeTween.Simple"; }
        if (StationIndex == 1) { return n"Gym.SceneNodeTween.Chain"; }
        return n"Gym.SceneNodeTween.Deep";
    }

    private void Request_BuildOffsets()
    {
        _Offsets.Empty();
        if (StationIndex == 0)
        { _Offsets.Add(FTransform(FRotator::ZeroRotator, FVector(0.0f, 120.0f, 0.0f), FVector(1,1,1))); }
        else if (StationIndex == 1)
        {
            _Offsets.Add(FTransform(FRotator(0.0f, 45.0f, 0.0f), FVector(120.0f, 0.0f, 0.0f), FVector(1,1,1)));
            _Offsets.Add(FTransform(FRotator(0.0f, 0.0f, 30.0f), FVector(0.0f, 100.0f, 0.0f), FVector(1,1,1)));
        }
        else
        {
            _Offsets.Add(FTransform(FRotator(0, 30, 0), FVector(100, 0, 0), FVector(1,1,1)));
            _Offsets.Add(FTransform(FRotator(0,-20, 0), FVector(80, 20, 0), FVector(1,1,1)));
            _Offsets.Add(FTransform(FRotator(0, 15, 0), FVector(60, 0, 0), FVector(1,1,1)));
            _Offsets.Add(FTransform(FRotator(0,-10, 0), FVector(40, 20, 0), FVector(1,1,1)));
            _Offsets.Add(FTransform(FRotator(0, 5, 0), FVector(30, 0, 0), FVector(1,1,1)));
        }
    }

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto State = CkSceneNodeTweenGym::Get_State();
        auto Mode = utils_net::Get_EntityNetMode(InHandle);
        if (ck::Is_NOT_Valid(State) || !CkSceneNodeTweenGym::IsLiveEntity(InHandle) ||
            Generation <= 0 || StationIndex < 0 || StationIndex > 2 ||
            utils_entity_lifetime::Get_LifetimeOwner(InHandle) !=
                CkSceneNodeTweenGym::FindStation(Get_StationTag()) ||
            (Mode != ECk_Net_NetModeType::Host && Mode != ECk_Net_NetModeType::ClientAndHost &&
             Mode != ECk_Net_NetModeType::Client))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        _Authority = Mode != ECk_Net_NetModeType::Client;
        if (_Authority && (!State.HasAuthority() || !State.Get_IsCurrentGeneration(Generation)))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        if (!_Authority && (State.HasAuthority() || !State.Snapshot.Ready ||
                            State.Snapshot.Generation != Generation))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        _TweenTarget = utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        if (!CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_TweenTarget)))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        Request_BuildOffsets();
        if (_Offsets.Num() != Get_ExpectedLinks())
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        _AutoConfig.TotalSteps = 1;
        _AutoConfig.GlobalAutoCommand = "panel [J] Auto (all stations)";
        _AutoConfig.Steps.Add(FCkGym_AutoStep("Tween yoyo", 0, 0));
        if (StationIndex == 0)
        { _AutoConfig.Description = "Single child tracks tweened root.\nDrift > 5 = propagation broken."; }
        else if (StationIndex == 1)
        { _AutoConfig.Description = "Root -> A -> B chain.\nTween drives root along +Y."; }
        else
        { _AutoConfig.Description = "5-level chain under a tweened root.\nStresses propagation depth."; }
        if (!_Authority)
        {
            _Tick = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"OnTick"));
            ProjectionConstructed = CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_Tick));
            return ECk_EntityScript_ConstructionFlow::Finished;
        }

        auto RootNode = utils_scene_node::Create(_TweenTarget, FTransform::Identity);
        if (!CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(RootNode)))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        _Root = RootNode.As_Transform();
        auto Parent = _Root;
        for (int32 i = 0; i < _Offsets.Num(); i++)
        {
            auto Link = utils_scene_node::Create(Parent, _Offsets[i]);
            if (!CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(Link)))
            { return ECk_EntityScript_ConstructionFlow::Finished; }
            _Links.Add(Link);
            Parent = Link.As_Transform();
        }
        AuthorityConstructed = Get_IsPreparedComposition();
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    bool Get_HasPreparedAuthorityComposition()
    { return AuthorityConstructed && Get_IsPreparedComposition(); }

    private bool Get_IsPreparedComposition()
    {
        if (!_Authority) { return false; }
        auto Self = ck::ToEntity(this);
        if (!CkSceneNodeTweenGym::IsLiveEntity(Self) ||
            !CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_TweenTarget)) ||
            !CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_Root)) ||
            FCk_Handle(_TweenTarget) != Self ||
            !utils_transform::Has(Self) || !utils_transform::Has(FCk_Handle(_Root)) ||
            _Links.Num() != Get_ExpectedLinks() || _Offsets.Num() != Get_ExpectedLinks())
        { return false; }
        if (utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_Root)) != Self ||
            !utils_scene_node::Has(FCk_Handle(_Root))) { return false; }
        auto ExpectedOwner = FCk_Handle(_Root);
        for (int32 i = 0; i < _Links.Num(); i++)
        {
            auto Link = FCk_Handle(_Links[i]);
            if (!CkSceneNodeTweenGym::IsLiveEntity(Link) ||
                !utils_scene_node::Has(Link) || !utils_transform::Has(Link) ||
                utils_entity_lifetime::Get_LifetimeOwner(Link) != ExpectedOwner)
            { return false; }
            ExpectedOwner = Link;
        }
        return true;
    }

    bool Request_ActivateAuthority()
    {
        if (_Activated || !Get_HasPreparedAuthorityComposition()) { return false; }
        auto End = InitialTransform.Translation +
            FVector(0.0f, StationIndex == 2 ? 300.0f : 250.0f, 0.0f);
        _Tween = utils_tween::Create_TweenEntityLocation(_TweenTarget, End,
            StationIndex == 2 ? 4.0f : 3.0f,
            ECk_TweenEasing::InOutSine, ECk_TweenLoopType::Yoyo, -1, 0.0f);
        if (!CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_Tween)) ||
            !utils_tween::Has(FCk_Handle(_Tween))) { return false; }
        auto State = CkSceneNodeTweenGym::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_IsCurrentGeneration(Generation)) { return false; }
        _AutoRunning = State.Snapshot.AutoRunning;
        if (!_AutoRunning) { utils_tween::Pause(_Tween); }
        _Tick = utils_timer::Create_Tick(ck::ToEntity(this), FCk_Delegate_Timer(this, n"OnTick"));
        _Activated = CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_Tick));
        return _Activated;
    }

    bool Get_HasValidAuthorityComposition()
    {
        return _Activated && Get_HasPreparedAuthorityComposition() &&
            CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_Tween)) &&
            utils_tween::Has(FCk_Handle(_Tween)) &&
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_Tween)) == ck::ToEntity(this) &&
            CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_Tick)) &&
            utils_timer::DoCast(FCk_Handle(_Tick)).IsSet() &&
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_Tick)) == ck::ToEntity(this) &&
            _Tick.Get_CurrentState() == ECk_Timer_State::Running;
    }

    bool Get_HasValidProjectionComposition()
    {
        auto Self = ck::ToEntity(this);
        return !_Authority && ProjectionConstructed &&
            CkSceneNodeTweenGym::IsLiveEntity(Self) &&
            CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_TweenTarget)) &&
            FCk_Handle(_TweenTarget) == Self && utils_transform::Has(Self) &&
            _Links.Num() == 0 &&
            CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_Tick)) &&
            utils_timer::DoCast(FCk_Handle(_Tick)).IsSet() &&
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_Tick)) == Self &&
            _Tick.Get_CurrentState() == ECk_Timer_State::Running;
    }

    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    {
        if (CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_RootShape)))
        { utils_entity_lifetime::Request_DestroyEntity(_RootShape); }
        for (auto Shape : _LinkShapes)
        {
            if (CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(Shape)))
            { utils_entity_lifetime::Request_DestroyEntity(Shape); }
        }
        if (CkSceneNodeTweenGym::IsLiveEntity(FCk_Handle(_LeafShape)))
        { utils_entity_lifetime::Request_DestroyEntity(_LeafShape); }
    }

    void Request_SetAuto(bool InRunning)
    {
        if (!_Authority || !Get_HasValidAuthorityComposition()) { return; }
        _AutoRunning = InRunning;
        if (InRunning) { utils_tween::Resume(_Tween); }
        else { utils_tween::Pause(_Tween); }
    }

    private FCkSceneNodeTweenGym_StationSample Request_Sample()
    {
        auto Sample = FCkSceneNodeTweenGym_StationSample();
        auto RootTransform = utils_transform::Get_EntityCurrentTransform(_Root);
        Sample.RootPosition = RootTransform.GetLocation();
        auto Acc = RootTransform;
        for (int32 i = 0; i < _Offsets.Num(); i++)
        {
            Acc = _Offsets[i] * Acc;
            Sample.ExpectedLinks.Add(Acc.GetLocation());
            Sample.ActualLinks.Add(utils_transform::Get_EntityCurrentLocation(_Links[i].As_Transform()));
        }
        Sample.Drift = float((Sample.ExpectedLinks.Last() - Sample.ActualLinks.Last()).Size());
        return Sample;
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto State = CkSceneNodeTweenGym::Get_State();
        if (ck::Is_NOT_Valid(State) || Generation != State.Snapshot.Generation) { return; }
        auto Self = ck::ToEntity(this);
        if (_Authority)
        {
            if (!State.Get_IsCurrentServerChild(Self, Generation)) { return; }
            if (!Get_HasValidAuthorityComposition())
            { State.Request_FailCurrentServerChild(Self, Generation, "Station lost a simulation dependency"); return; }
            State.SubmitStationSample(Self, Generation, StationIndex, Request_Sample());
            if (State.Snapshot.Failed) { return; }
        }
        else if (!State.Get_HasAdmittedLocalProjection(Generation)) { return; }
        if (GetWorld().GetNetMode() == ENetMode::NM_DedicatedServer) { return; }
        if (!State.Get_HasCompleteSnapshot()) { return; }
        Request_Draw(State.Snapshot.Stations[StationIndex], State.Snapshot.AutoRunning);
    }

    private bool Get_ColorChanged(FLinearColor InColor)
    {
        return InColor.R != _LeafColor.R || InColor.G != _LeafColor.G ||
            InColor.B != _LeafColor.B || InColor.A != _LeafColor.A;
    }

    private bool Get_IsValidShape(FCk_Handle_Pmg_DebugShape InShape)
    {
        auto Handle = FCk_Handle(InShape);
        return CkSceneNodeTweenGym::IsLiveEntity(Handle) &&
            utils_pmg_debug_shape::Has(Handle) && utils_transform::Has(Handle);
    }

    private void Request_FailPresentation(FString InReason)
    {
        auto State = CkSceneNodeTweenGym::Get_State();
        if (ck::Is_NOT_Valid(State)) { return; }
        auto Self = ck::ToEntity(this);
        if (_Authority) { State.Request_FailCurrentServerChild(Self, Generation, InReason); }
        else { State.Request_FailCurrentLocalChild(Self, Generation, InReason); }
    }

    private void Request_Draw(FCkSceneNodeTweenGym_StationSample InSample, bool InAutoRunning)
    {
        auto Needed = Get_ExpectedLinks();
        if (InSample.ExpectedLinks.Num() != Needed || InSample.ActualLinks.Num() != Needed)
        { return; }
        auto RootLoc = InSample.RootPosition;
        if (!_ShapesInitialized)
        {
            _RootShape = utils_pmg_basic_shapes::DrawFilledBox(RootLoc, FVector(30,30,30),
                CkSceneNodeTweenGym_Vis::RootColor(), true, 2.0f, ECk_Plane_Axis::XY, -1.0f);
            for (int32 i = 0; i < Needed; i++)
            {
                if (StationIndex == 2 && i == Needed - 1) { break; }
                auto Size = StationIndex == 0 ? 20.0f :
                    (StationIndex == 1 ? (i == 0 ? 22.0f : 16.0f) : Math::Max(8.0f, 22.0f - float(i) * 3.0f));
                _LinkShapes.Add(utils_pmg_basic_shapes::DrawFilledBox(InSample.ExpectedLinks[i],
                    FVector(Size,Size,Size), CkSceneNodeTweenGym_Vis::IntermediateColor(i),
                    true, 2.0f, ECk_Plane_Axis::XY, -1.0f));
            }
            _LeafColor = InSample.Drift > 5.0 ? CkSceneNodeTweenGym_Vis::LeafDrift() :
                CkSceneNodeTweenGym_Vis::LeafAligned();
            _LeafShape = utils_pmg_basic_shapes::DrawFilledSphere(InSample.ActualLinks[Needed - 1],
                StationIndex == 2 ? 22.0f : 25.0f, 12, 12,
                _LeafColor, true, 2.0f, ECk_Plane_Axis::XY, -1.0f);
            _ShapesInitialized = true;
        }
        if (_LinkShapes.Num() != (StationIndex == 2 ? 4 : Needed) ||
            !Get_IsValidShape(_RootShape) || !Get_IsValidShape(_LeafShape))
        { Request_FailPresentation("Required display shape is incomplete"); return; }
        for (auto Shape : _LinkShapes)
        {
            if (!Get_IsValidShape(Shape))
            { Request_FailPresentation("Required display link was lost"); return; }
        }
        utils_transform::Request_SetLocation(_RootShape, RootLoc, ECk_LocalWorld::World);
        for (int32 i = 0; i < _LinkShapes.Num(); i++)
        {
            utils_transform::Request_SetLocation(_LinkShapes[i], InSample.ExpectedLinks[i], ECk_LocalWorld::World);
        }
        auto Start = RootLoc;
        for (int32 i = 0; i < Needed; i++)
        {
            CkSceneNodeTweenGym_Vis::DrawEdge(Start, InSample.ExpectedLinks[i], i);
            Start = InSample.ExpectedLinks[i];
        }
        auto LeafLoc = InSample.ActualLinks[Needed - 1];
        auto Color = InSample.Drift > 5.0 ? CkSceneNodeTweenGym_Vis::LeafDrift() :
            CkSceneNodeTweenGym_Vis::LeafAligned();
        if (Get_ColorChanged(Color))
        {
            utils_entity_lifetime::Request_DestroyEntity(_LeafShape);
            _LeafColor = Color;
            _LeafShape = utils_pmg_basic_shapes::DrawFilledSphere(LeafLoc,
                StationIndex == 2 ? 22.0f : 25.0f, 12, 12,
                Color, true, 2.0f, ECk_Plane_Axis::XY, -1.0f);
            if (!Get_IsValidShape(_LeafShape))
            { Request_FailPresentation("Leaf display allocation failed"); return; }
        }
        else { utils_transform::Request_SetLocation(_LeafShape, LeafLoc, ECk_LocalWorld::World); }

        auto LastExpected = InSample.ExpectedLinks[Needed - 1];
        auto LastActual = LeafLoc;
        auto StatusLabel = InSample.Drift > 5.0 ? FString("DESYNC") : FString("OK");
        auto Txt = gym_auto::FormatHeader(_AutoConfig, InAutoRunning);
        Txt = Txt + "Expected sphere position:\n";
        if (StationIndex == 0)
        { Txt = Txt + " co-located with cyan child box\n (leaf IS the child).\n\n"; }
        else if (StationIndex == 1)
        { Txt = Txt + " co-located with purple B box\n (leaf IS node B at end of chain).\n\n"; }
        else
        { Txt = Txt + " OFFSET from the last box. Sphere\n sits past link 4 by the 5th local\n offset (it IS the 5th link).\n\n"; }
        if (StationIndex == 2)
        {
            Txt = f"{Txt}Expected (leaf): {LastExpected.X},{LastExpected.Y},{LastExpected.Z}\n";
            Txt = f"{Txt}Actual (leaf):   {LastActual.X},{LastActual.Y},{LastActual.Z}\n";
            Txt = f"{Txt}Drift (leaf):    {InSample.Drift}\n";
            Txt = f"{Txt}Status:          {StatusLabel}\n";
        }
        else
        {
            Txt = f"{Txt}Expected: {LastExpected.X},{LastExpected.Y},{LastExpected.Z}\n";
            Txt = f"{Txt}Actual:   {LastActual.X},{LastActual.Y},{LastActual.Z}\n";
            Txt = f"{Txt}Drift:    {InSample.Drift}\n";
            Txt = f"{Txt}Status:   {StatusLabel}\n";
        }
        if (StatusLabel == "DESYNC")
        {
            if (StationIndex == 0) { Txt = Txt + "Child not following tweened root.\n"; }
            else if (StationIndex == 1) { Txt = Txt + "Chain leaf not following root.\n"; }
            else { Txt = Txt + "Leaf not following through chain.\n"; }
        }
        Txt = Txt + gym_auto::FormatAutoAndCommands(_AutoConfig, 0, InAutoRunning);
        auto Parent = utils_entity_lifetime::Get_LifetimeOwner(ck::ToEntity(this));
        if (!CkSceneNodeTweenGym::IsLiveEntity(Parent)) { return; }
        auto& Display = Parent.AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
        if (StationIndex == 0) { Display.Title = FText::FromString("TWEEN + SCENE NODE (SIMPLE)"); }
        else if (StationIndex == 1) { Display.Title = FText::FromString("TWEEN + SCENE NODE (CHAIN)"); }
        else { Display.Title = FText::FromString("TWEEN + SCENE NODE (DEEP)"); }
        Display.Description = FText::FromString(Txt);
    }
}

class UCk_EntityScript_SceneNodeTweenGym_SimpleStation : UCk_EntityScript_SceneNodeTweenGym_BaseStation
{
    default StationIndex = 0;
}
class UCk_EntityScript_SceneNodeTweenGym_ChainStation : UCk_EntityScript_SceneNodeTweenGym_BaseStation
{
    default StationIndex = 1;
}
class UCk_EntityScript_SceneNodeTweenGym_DeepStation : UCk_EntityScript_SceneNodeTweenGym_BaseStation
{
    default StationIndex = 2;
}
