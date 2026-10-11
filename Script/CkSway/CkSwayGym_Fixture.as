// Language=angelscript

struct FCkSwayGym_Fixture
{
    FCk_Handle_Transform Root;
    FCk_Handle_Transform Head;
    TArray<FCk_Handle_Sway> Sways;
    TArray<FCk_Handle_SceneNode> SwayNodes;
    TArray<FCk_Handle_Tween> Tweens;
    TArray<FCk_Handle_SceneNode> CubeNodes;
    TArray<FCk_Handle_UnrealComponent> Components;
    FVector Origin;
    int32 Station = 0;
    private int32 _Generation = 0;
    private int32 _Phase = 0;
    private int32 _NextSway = 0;
    private int32 _NextCube = 0;
    private FCk_Handle _RootIdentity;
    private FCk_Handle _OwnerRoot;
    private FCk_Handle _PendingIdentity;
    private FCk_Handle_PendingEntityScript _Pending;
    private ECkSwayGym_NodeRole _PendingRole = ECkSwayGym_NodeRole::Root;
    private int32 _PendingIndex = -1;
    private FTransform _HeadStart;
    private UObject _Source;
    private bool _Started = false;

    private bool Get_IsSafeVector(FVector Value) const
    {
        return Math::IsFinite(Value.X) && Math::IsFinite(Value.Y) &&
            Math::IsFinite(Value.Z) && Math::Abs(Value.X) <= 10000000.0 &&
            Math::Abs(Value.Y) <= 10000000.0 &&
            Math::Abs(Value.Z) <= 10000000.0;
    }

    private bool Get_HasSourceListener(UObject InMeshListener, FCk_Handle InOwner) const
    {
        if (ck::Is_NOT_Valid(InOwner) ||
            !utils_entity_lifetime::Get_CanCreateEntity(InOwner))
        { return false; }
        if (ck::Is_NOT_Valid(InMeshListener)) { return false; }
        auto Source = Cast<ACk_SwayGym_GameState>(InMeshListener);
        if (ck::Is_NOT_Valid(Source) || Source.IsActorBeingDestroyed() ||
            !Source.HasAuthority() || !Source.Get_HasPublishedCatalog())
        { return false; }
        auto World = Source.GetWorld();
        return ck::IsValid(World) && World.GetGameState() == Source &&
            utils_entity_lifetime::Get_WorldForEntity(InOwner) == World &&
            utils_owning_actor::TryGet_EntityOwningActor_Recursive(InOwner) == Source;
    }

    // The direct lifetime parent of every generic node is itself replicated.
    // Retain the root allocation immediately so a failed staged construction
    // can retire its entire dependent tree.
    private bool Request_Node(FCk_Handle Parent, ECkSwayGym_NodeRole Role,
        int32 Index, FTransform WorldPose)
    {
        if (!Get_HasSourceListener(_Source, Parent) ||
            ck::Is_NOT_Valid(Parent) || !WorldPose.IsValid() ||
            WorldPose.ContainsNaN()) { return false; }
        FCkSwayGym_NodeSpawnParams Params;
        Params.Generation = _Generation;
        Params.Station = Station;
        Params.Index = Index;
        Params.Role = Role;
        Params.InitialTransform = WorldPose;
        _PendingRole = Role;
        _PendingIndex = Index;
        _Pending = utils_entity_script::Request_SpawnEntity(Parent,
            UCk_EntityScript_SwayGym_Node, FInstancedStruct::Make(Params));
        _PendingIdentity = _Pending.Get_EntityUnderConstruction();
        return utils_pending_entity_script::Get_IsValid(_Pending) &&
            ck::IsValid(_PendingIdentity);
    }

    private int32 Get_PendingNodeState()
    {
        if (ck::Is_NOT_Valid(_PendingIdentity)) { return -1; }
        if (!utils_entity_script::Has(_PendingIdentity)) { return 0; }
        auto RawScript = utils_entity_script::TryGet_EntityScript(_PendingIdentity);
        if (ck::Is_NOT_Valid(RawScript)) { return 0; }
        auto Script = Cast<UCk_EntityScript_SwayGym_Node>(RawScript);
        if (ck::Is_NOT_Valid(Script) || Script.Generation != _Generation ||
            Script.Station != Station || Script.Index != _PendingIndex ||
            Script.Role != _PendingRole) { return -1; }
        if (!Script.Get_IsReady() || !utils_transform::Has(_PendingIdentity) ||
            !utils_net::Has(_PendingIdentity) ||
            utils_net::Get_Replication(_PendingIdentity) != ECk_Replication::Replicates ||
            !utils_entity_replication_driver::Has(_PendingIdentity))
        { return 0; }
        return 1;
    }

    // Station 0 = Turntable, 1 = Shuttle, 2 = Compare. Creation advances
    // across frames because generic EntityScript children finish asynchronously.
    bool Create(FCk_Handle InOwner, UObject InMeshListener, FVector InOrigin,
        int32 InStation, int32 InGeneration)
    {
        auto Source = Cast<ACk_SwayGym_GameState>(InMeshListener);
        if (_Phase != 0 || ck::IsValid(_RootIdentity) || ck::IsValid(Root) ||
            ck::IsValid(Head) || !Sways.IsEmpty() || !SwayNodes.IsEmpty() ||
            !Tweens.IsEmpty() || !CubeNodes.IsEmpty() || !Components.IsEmpty() ||
            ck::Is_NOT_Valid(Source) ||
            utils_owning_actor::TryGet_ActorEntityHandle(Source) != InOwner ||
            !Get_HasSourceListener(InMeshListener, InOwner) ||
            InStation < 0 || InStation >= CkSwayGymCatalog::StationCount ||
            InGeneration <= 0 || !Get_IsSafeVector(InOrigin)) { return false; }
        Origin = InOrigin;
        Station = InStation;
        _Generation = InGeneration;
        _Source = InMeshListener;
        _OwnerRoot = InOwner;
        const bool Turns = Station != 1;
        const bool Shuttles = Station != 0;
        _HeadStart = FTransform(FRotator(0.0, Turns ? -90.0 : 0.0, 0.0),
            Origin - FVector(Shuttles ? 400.0 : 0.0, 0.0, 0.0), FVector::OneVector);
        const bool Queued = Request_Node(InOwner, ECkSwayGym_NodeRole::Root, 0,
            FTransform(Origin));
        _RootIdentity = _PendingIdentity;
        if (!Queued) { return false; }
        _Phase = 1;
        return true;
    }

    private int32 Get_SwayCount() const { return Station == 2 ? 3 : 1; }
    private int32 Get_CubeCount() const { return Station == 2 ? 3 : 2; }
    private float Get_SwayY(int32 Index) const
    { return Station == 2 ? float(Index - 1) * 120.0f : 0.0f; }
    private bool Get_IsRigidCube(int32 Index) const
    { return Station != 2 && Index == 1; }

    private bool FinishSway(int32 Index)
    {
        if (!utils_transform::Has(_PendingIdentity)) { return false; }
        auto Transform = utils_transform::DoCastChecked(_PendingIdentity);
        auto Scene = utils_scene_node::Add(Transform, Head,
            FTransform(FVector(80.0, Get_SwayY(Index), 0.0)));
        if (ck::Is_NOT_Valid(Scene)) { return false; }
        SwayNodes.Add(Scene);
        FCk_Sway_Spec Spec = Make_DemoSpec();
        if (Station == 2 && Index == 1) { Spec = Make_HeavySpec(); }
        if (Station == 2 && Index == 2) { Spec = Make_DisabledSpec(); }
        auto Sway = utils_sway::Add(Scene, Spec);
        if (ck::Is_NOT_Valid(Sway)) { return false; }
        Sways.Add(Sway);
        return true;
    }

    private bool FinishCube(int32 Index)
    {
        if (!utils_transform::Has(_PendingIdentity)) { return false; }
        auto Script = Cast<UCk_EntityScript_SwayGym_Node>(
            utils_entity_script::TryGet_EntityScript(_PendingIdentity));
        if (ck::Is_NOT_Valid(Script)) { return false; }
        auto Component = Script.Get_Component();
        if (ck::Is_NOT_Valid(Component)) { return false; }
        Components.Add(Component); // retain child allocation before SceneNode Add
        auto Transform = utils_transform::DoCastChecked(_PendingIdentity);
        const bool Rigid = Get_IsRigidCube(Index);
        auto Parent = Rigid ? Head : SwayNodes[Index].As_Transform();
        const float Scale = Rigid ? 0.2f : 0.4f;
        auto Offset = FTransform(FRotator::ZeroRotator,
            Rigid ? FVector(80.0,0.0,0.0) : FVector::ZeroVector,
            FVector(Scale,Scale,Scale));
        auto Scene = utils_scene_node::Add(Transform, Parent, Offset);
        if (ck::Is_NOT_Valid(Scene)) { return false; }
        CubeNodes.Add(Scene);
        return true;
    }

    private bool Get_AreComponentsReady() const
    {
        if (Components.Num() != Get_CubeCount()) { return false; }
        for (auto Component : Components)
        {
            if (ck::Is_NOT_Valid(Component) ||
                !utils_unreal_component::Has(FCk_Handle(Component))) { return false; }
            auto Mesh = Cast<UStaticMeshComponent>(utils_unreal_component::Get_Component(Component));
            if (ck::Is_NOT_Valid(Mesh) || ck::Is_NOT_Valid(Mesh.StaticMesh))
            { return false; }
        }
        return true;
    }

    private bool CreatePausedTweens()
    {
        if (ck::Is_NOT_Valid(Head) || !Get_AreComponentsReady() ||
            !Tweens.IsEmpty()) { return false; }
        if (Station != 1)
        {
            auto Tween = utils_tween::Create_TweenEntityRotation(Head,
                FRotator(0.0,90.0,0.0), 2.0f, ECk_TweenEasing::InOutSine,
                ECk_TweenLoopType::Yoyo,-1);
            if (ck::Is_NOT_Valid(Tween)) { return false; }
            Tweens.Add(Tween);
            if (utils_tween::Pause(Tween) != Tween) { return false; }
        }
        if (Station != 0)
        {
            auto Tween = utils_tween::Create_TweenEntityLocation(Head,
                Origin + FVector(400.0,0.0,0.0), 2.0f,
                ECk_TweenEasing::InOutSine, ECk_TweenLoopType::Yoyo,-1);
            if (ck::Is_NOT_Valid(Tween)) { return false; }
            Tweens.Add(Tween);
            if (utils_tween::Pause(Tween) != Tween) { return false; }
        }
        return Tweens.Num() == (Station == 2 ? 2 : 1);
    }

    bool Request_Advance()
    {
        if (_Phase <= 0 || !Get_HasSourceListener(_Source,_OwnerRoot))
        { return false; }
        if (_Phase == 9 || _Phase == 10) { return true; }
        if (_Phase == 1 || _Phase == 2 || _Phase == 4 || _Phase == 6)
        {
            const int32 State = Get_PendingNodeState();
            if (State < 0) { return false; }
            if (State == 0) { return true; }
            if (_Phase == 1)
            {
                if (!utils_transform::Has(_PendingIdentity)) { return false; }
                Root = utils_transform::DoCastChecked(_PendingIdentity);
                if (ck::Is_NOT_Valid(Root) ||
                    !Request_Node(FCk_Handle(Root), ECkSwayGym_NodeRole::Head,
                        0, _HeadStart)) { return false; }
                _Phase = 2;
                return true;
            }
            if (_Phase == 2)
            {
                if (!utils_transform::Has(_PendingIdentity)) { return false; }
                Head = utils_transform::DoCastChecked(_PendingIdentity);
                if (ck::Is_NOT_Valid(Head)) { return false; }
                _Phase = 3;
                return true;
            }
            if (_Phase == 4)
            {
                if (!FinishSway(_NextSway)) { return false; }
                ++_NextSway;
                _Phase = 3;
                return true;
            }
            if (!FinishCube(_NextCube)) { return false; }
            ++_NextCube;
            _Phase = 5;
            return true;
        }
        if (_Phase == 3)
        {
            if (_NextSway >= Get_SwayCount()) { _Phase = 5; return true; }
            const auto WorldPose = FTransform(_HeadStart.Rotator(),
                _HeadStart.TransformPosition(FVector(80.0,Get_SwayY(_NextSway),0.0)),
                FVector::OneVector);
            if (!Request_Node(FCk_Handle(Head),ECkSwayGym_NodeRole::Sway,
                _NextSway,WorldPose)) { return false; }
            _Phase = 4;
            return true;
        }
        if (_Phase == 5)
        {
            if (_NextCube >= Get_CubeCount()) { _Phase = 7; return true; }
            const bool Rigid = Get_IsRigidCube(_NextCube);
            const auto Parent = Rigid ? FCk_Handle(Head) : FCk_Handle(SwayNodes[_NextCube]);
            const auto Position = _HeadStart.TransformPosition(
                FVector(80.0, Rigid ? 0.0 : Get_SwayY(_NextCube), 0.0));
            const float Scale = Rigid ? 0.2f : 0.4f;
            const auto WorldPose = FTransform(_HeadStart.Rotator(),Position,
                FVector(Scale,Scale,Scale));
            if (!Request_Node(Parent,ECkSwayGym_NodeRole::Cube,
                _NextCube,WorldPose)) { return false; }
            _Phase = 6;
            return true;
        }
        if (_Phase == 7)
        {
            if (!Get_AreComponentsReady()) { return true; }
            if (!CreatePausedTweens()) { return false; }
            _Phase = 8;
            return true;
        }
        if (_Phase == 8)
        {
            for (auto Tween : Tweens)
            { if (utils_tween::Get_State(Tween) != ECk_TweenState::Paused)
                { return true; } }
            _Phase = 9;
            return true;
        }
        return false;
    }

    bool Request_Start()
    {
        if (_Started) { return _Phase == 10; }
        if (_Phase != 9 || !Get_AreComponentsReady()) { return false; }
        for (auto Tween : Tweens)
        {
            if (ck::Is_NOT_Valid(Tween) ||
                utils_tween::Get_State(Tween) != ECk_TweenState::Paused ||
                utils_tween::Resume(Tween) != Tween) { return false; }
        }
        _Started = true;
        _Phase = 10;
        return true;
    }

    bool Get_IsReady() const { return _Phase == 9 && Get_AreComponentsReady(); }

    // Demonstration tune. The gym tweens drive the head at ~70 deg/s and ~300 cm/s (a mouse flick is 5-10x that),
    // so the framework defaults would read as a rigid node here; gains and clamps are raised until the lag is
    // unmistakable against the rigid twin. Not a recommended gameplay tune.
    FCk_Sway_Spec Make_DemoSpec() const
    {
        auto Spec = FCk_Sway_Spec();
        Spec.Set_Location(FCk_Sway_Response(FVector(30.0, 30.0, 30.0), 3.5f, 0.7f));
        Spec.Set_Rotation(FCk_Sway_Response(FVector(25.0, 25.0, 25.0), 4.5f, 0.75f));
        Spec.Set_RotationFromAngularVelocity(FVector(0.05, 0.05, 0.05));
        Spec.Set_LocationFromLinearVelocity(FVector(0.03, 0.03, 0.03));
        Spec.Set_LateralCmFromYawRate(0.12f);
        Spec.Set_VerticalCmFromPitchRate(0.08f);
        Spec.Set_RollDegFromLateralVelocity(0.02f);
        Spec.Set_PitchDegFromForwardVelocity(0.0f);
        return Spec;
    }

    FCk_Sway_Spec Make_HeavySpec() const
    {
        const auto Default = Make_DemoSpec();
        auto Spec = Make_DemoSpec();

        auto Location = Spec.Get_Location();
        Location.Set_FrequencyHz(2.0f);
        Location.Set_DampingRatio(0.5f);
        Spec.Set_Location(Location);

        auto Rotation = Spec.Get_Rotation();
        Rotation.Set_FrequencyHz(2.5f);
        Rotation.Set_DampingRatio(0.45f);
        Spec.Set_Rotation(Rotation);

        Spec.Set_RotationFromAngularVelocity(Default.Get_RotationFromAngularVelocity() * 2.0);
        Spec.Set_LocationFromLinearVelocity(Default.Get_LocationFromLinearVelocity() * 2.0);
        Spec.Set_LateralCmFromYawRate(Default.Get_LateralCmFromYawRate() * 2.0f);
        Spec.Set_VerticalCmFromPitchRate(Default.Get_VerticalCmFromPitchRate() * 2.0f);
        Spec.Set_RollDegFromLateralVelocity(Default.Get_RollDegFromLateralVelocity() * 2.0f);
        Spec.Set_PitchDegFromForwardVelocity(Default.Get_PitchDegFromForwardVelocity() * 2.0f);
        return Spec;
    }

    FCk_Sway_Spec Make_DisabledSpec() const
    {
        auto Spec = FCk_Sway_Spec();
        Spec.Set_StartingState(ECk_EnableDisable::Disable);
        return Spec;
    }

    void Draw()
    {
        if (ck::Is_NOT_Valid(Head))
        {
            return;
        }
        const auto HeadPose = utils_transform::Get_EntityCurrentTransform(Head);
        utils_debug_draw::DrawDebugSolidBox(HeadPose.GetLocation(), FVector(25.0, 20.0, 20.0),
            FLinearColor(1.0f, 0.4f, 0.05f, 1.0f), HeadPose.Rotator());
    }

    void Request_Destroy()
    {
        _Started = false;
        if (ck::IsValid(_RootIdentity))
        { utils_entity_lifetime::Request_DestroyEntity(_RootIdentity); }
    }
}
