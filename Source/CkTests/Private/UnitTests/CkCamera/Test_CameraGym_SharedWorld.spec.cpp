#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_StartupSettings.h"
#include "CkGymRegistry_Utils.h"
#include "CkGym_Switchboard_Subsystem.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkCameraGym_LocalAdmission.h"
#include "CkCamera/Camera/CkCamera_Component.h"
#include "CkCamera/Camera/CkCamera_Fragment.h"
#include "CkCamera/Camera/CkCamera_Utils.h"
#include "CkCamera/Camera/CameraLayer/EntityScripts/CkCameraLayer_EntityScript.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "Components/SceneComponent.h"
#include "CkInput/CkInputLayer_Utils.h"
#include "CkInput/CkInputSource_Utils.h"
#include "CkInput/Subsystem/CkInputSource_Subsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/NetDriver.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_camera_authored_network
{
    constexpr int32 Worlds = 3;
    constexpr int32 Clients = 2;
    constexpr auto EntryMap = TEXT("/Engine/Maps/Entry");
    constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_CameraGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_CameraGym_GameState");
    constexpr auto PCPath = TEXT("/Script/Angelscript.Ck_CameraGym_PlayerController");
    constexpr auto PawnPath = TEXT("/Script/Angelscript.Ck_CameraGym_Pawn");
    constexpr auto FloorPath = TEXT("/Script/Angelscript.Ck_CameraGym_SharedFloor");
    constexpr auto PillarPath = TEXT("/Script/Angelscript.Ck_CameraGym_SharedPillar");
    constexpr auto BeaconPath = TEXT("/Script/Angelscript.Ck_CameraGym_SharedBeacon");
    constexpr auto ThirdPath = TEXT("/Script/Angelscript.Ck_CameraLayer_ThirdPerson");
    constexpr auto TopPath = TEXT("/Script/Angelscript.Ck_CameraLayer_TopDown");

    template <class T> T* Field(const UStruct* Type, const TCHAR* Name)
    { return Type ? FindFProperty<T>(Type, Name) : nullptr; }

    struct FCall
    {
        UObject* Target = nullptr;
        UFunction* Function = nullptr;
        TUniquePtr<FStructOnScope> Args;
        FCall(UObject* Object, const TCHAR* Name)
        {
            if (!IsValid(Object)) { return; }
            Function = Object->FindFunction(Name);
            if (!IsValid(Function)) { Function = nullptr; return; }
            Target = Object;
            Args = MakeUnique<FStructOnScope>(Function);
        }
        bool Send() const
        {
            if (!IsValid(Target) || !IsValid(Function) || !Args ||
                !Args->GetStructMemory()) { return false; }
            Target->ProcessEvent(Function, Args->GetStructMemory());
            return true;
        }
    };

    bool Bool(UObject* Object, const TCHAR* Name)
    {
        FCall Call(Object, Name);
        auto* Return = Field<FBoolProperty>(Call.Function, TEXT("ReturnValue"));
        return Return && Call.Function->NumParms == 1 && Call.Send() &&
            Return->GetPropertyValue_InContainer(Call.Args->GetStructMemory());
    }

    bool Integer(UObject* Object, const TCHAR* Name, int32& Out)
    {
        Out = INDEX_NONE;
        FCall Call(Object, Name);
        auto* Return = Field<FIntProperty>(Call.Function, TEXT("ReturnValue"));
        if (!Return || Call.Function->NumParms != 1 || !Call.Send())
        { return false; }
        Out = Return->GetPropertyValue_InContainer(Call.Args->GetStructMemory());
        return true;
    }

    bool Scope(UObject* Object, FCk_Handle& Out)
    {
        Out = {};
        FCall Call(Object, TEXT("Get_LocalCameraScope"));
        auto* Return = Field<FStructProperty>(Call.Function, TEXT("ReturnValue"));
        if (!Return || Return->Struct != FCk_Handle::StaticStruct() ||
            Call.Function->NumParms != 1 || !Call.Send()) { return false; }
        Out = *Return->ContainerPtrToValuePtr<FCk_Handle>(
            Call.Args->GetStructMemory());
        return true;
    }

    bool Allocated(const FCk_Handle& Handle)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle); }

    bool Direct(const FCk_Handle& Child, const FCk_Handle& Parent)
    {
        return ck::IsValid(Child) && ck::IsValid(Parent) &&
            UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child) &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Parent;
    }

    int32 CameraCount(UWorld* World)
    {
        if (!IsValid(World)) { return INDEX_NONE; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!ck::IsValid(Root)) { return INDEX_NONE; }
        const auto Registry = Root.Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        int32 Count = 0;
        Registry.View<ck::FFragment_Camera>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_Camera&)
        { if (Allocated(FCk_Handle{Entity, RegistryHandle})) { ++Count; } });
        return Count;
    }

    struct FRun
    {
        bool Dedicated = false;
        bool RestoreCaptured = false;
        UClass* BaseMode = nullptr;
        UClass* Mode = nullptr;
        UClass* StateClass = nullptr;
        UClass* PCClass = nullptr;
        UClass* PawnClass = nullptr;
        UClass* SourceClass[3]{};
        UClass* ThirdClass = nullptr;
        UClass* TopClass = nullptr;
        TWeakObjectPtr<UWorld> World[Worlds];
        TWeakObjectPtr<AGameStateBase> State[Worlds];
        TWeakObjectPtr<AActor> Bank[Worlds][10];
        TWeakObjectPtr<APlayerController> Local[Clients];
        TWeakObjectPtr<APlayerController> Remote[Clients];
        TWeakObjectPtr<APlayerController> Host;
        FCk_Handle Scope[Clients];
        FCk_Handle HostScope;
        FCk_Handle_InputSource Source[Clients];
        FCk_Handle_InputLayer Layer[Clients];
        FCk_Handle_InputSource HostSource;
        FCk_Handle_InputLayer HostLayer;
        FString LastBaselineStage;
        FVector Owner0LocationBeforeW = FVector::ZeroVector;
        FVector Owner0LocationBeforeFreshW = FVector::ZeroVector;
        FVector BankCenter = FVector::ZeroVector;
        ECkGym_StartupMode Startup{};
        FString LastGym;
        TArray<FString> Recents;
        ~FRun() { Restore(); }
        void Restore()
        {
            if (!RestoreCaptured) { return; }
            RestoreCaptured = false;
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = Startup;
            Settings->LastGymName = LastGym;
            Settings->RecentGymNames = Recents;
            Settings->SaveConfig();
        }
    };

    bool PlayerReady(APlayerController* PC, UWorld* World, const FRun& Run)
    {
        if (!IsValid(PC) || PC->GetWorld() != World ||
            PC->GetClass() != Run.PCClass || !PC->HasActorBegunPlay())
        { return false; }
        auto* Pawn = PC->GetPawn().Get();
        return IsValid(Pawn) && Pawn->GetClass() == Run.PawnClass &&
            Pawn->GetWorld() == World && Pawn->GetController() == PC &&
            Pawn->HasActorBegunPlay();
    }

    FVector RoleLocation(const FVector& Center, int32 Index);

    FString DescribeRole(const FRun& Run, UWorld* World,
        AGameStateBase* State, const FVector& Center, bool HasCenter,
        AActor* Actor, int32 Role, bool& OutMatches)
    {
        OutMatches = false;
        if (!IsValid(Actor))
        { return FString::Printf(TEXT("%d:absent"), Role); }
        if (Actor->IsActorBeingDestroyed())
        { return FString::Printf(TEXT("%d:destroying"), Role); }
        const UClass* Expected = Run.SourceClass[
            Role == 0 ? 0 : Role == 9 ? 2 : 1];
        const FTransform Pose = Actor->GetActorTransform();
        const FVector Location = Pose.GetLocation();
        const FVector Scale = Pose.GetScale3D();
        const FQuat Rotation = Pose.GetRotation();
        const bool Finite = Pose.IsValid() && !Pose.ContainsNaN() &&
            FMath::IsFinite(Location.X) && FMath::IsFinite(Location.Y) &&
            FMath::IsFinite(Location.Z) && FMath::IsFinite(Scale.X) &&
            FMath::IsFinite(Scale.Y) && FMath::IsFinite(Scale.Z) &&
            FMath::IsFinite(Rotation.X) && FMath::IsFinite(Rotation.Y) &&
            FMath::IsFinite(Rotation.Z) && FMath::IsFinite(Rotation.W);
        const FVector ExpectedScale = Role == 0 ? FVector(40, 40, 0.5) :
            Role == 9 ? FVector(1.5, 1.5, 1.5) : FVector(1, 1, 4);
        const bool PoseMatch = HasCenter && Finite &&
            Location.Equals(RoleLocation(Center, Role), 0.1) &&
            Scale.Equals(ExpectedScale, 0.01) &&
            Rotation.IsNormalized() &&
            Rotation.Equals(FQuat::Identity, 0.0001);
        const bool ExactClass = Actor->GetClass() == Expected;
        const bool ExactWorld = Actor->GetWorld() == World;
        const bool ExactOwner = Actor->GetOwner() == State;
        const bool Replicated = Actor->GetIsReplicated();
        const bool Delivery =
            UCk_CameraGym_LocalAdmission_UE::Get_HasReplicationDelivery(Actor);
        const bool ReplicatingMovement = Actor->IsReplicatingMovement();
        OutMatches = ExactClass && ExactWorld && ExactOwner && Delivery &&
            ReplicatingMovement && PoseMatch;
        return FString::Printf(
            TEXT("%d:class=%d,world=%d,owner=%d,rep=%d,delivery=%d,move=%d,finite=%d,pose=%d,L=%s,S=%s"),
            Role, int32(ExactClass), int32(ExactWorld), int32(ExactOwner),
            int32(Replicated), int32(Delivery), int32(ReplicatingMovement),
            int32(Finite), int32(PoseMatch),
            *Location.ToString(), *Scale.ToString());
    }

    FString DescribeWorld(const FRun& Run, UWorld* World, int32 Index)
    {
        if (!IsValid(World))
        { return FString::Printf(TEXT(" W%d=absent"), Index); }
        if (World->bIsTearingDown || World->IsBeingCleanedUp())
        { return FString::Printf(TEXT(" W%d=teardown"), Index); }
        auto* State = World->GetGameState();
        FString Result = FString::Printf(TEXT(" W%d(net=%d,gs=%d"),
            Index, int32(World->GetNetMode()),
            IsValid(State) && State->GetClass() == Run.StateClass ? 1 : 0);
        if (IsValid(State) && State->GetClass() == Run.StateClass)
        {
            auto* Ready = Field<FBoolProperty>(Run.StateClass, TEXT("SourceReady"));
            auto* Failed = Field<FBoolProperty>(Run.StateClass, TEXT("SourceFailed"));
            auto* Generation = Field<FIntProperty>(Run.StateClass, TEXT("SourceGeneration"));
            auto* Array = Field<FArrayProperty>(Run.StateClass, TEXT("SourceActors"));
            int32 ArrayCount = INDEX_NONE;
            AActor* RoleActors[10]{};
            if (Array)
            {
                if (auto* Inner = CastField<FObjectProperty>(Array->Inner))
                {
                    FScriptArrayHelper Actors(Array,
                        Array->ContainerPtrToValuePtr<void>(State));
                    ArrayCount = Actors.Num();
                    for (int32 Role = 0; Role < FMath::Min(ArrayCount, 10); ++Role)
                    {
                        RoleActors[Role] = Cast<AActor>(
                            Inner->GetObjectPropertyValue(Actors.GetRawPtr(Role)));
                    }
                }
            }
            Result += FString::Printf(TEXT(",ready=%d,failed=%d,gen=%d,rows=%d"),
                Ready ? int32(Ready->GetPropertyValue_InContainer(State)) : -1,
                Failed ? int32(Failed->GetPropertyValue_InContainer(State)) : -1,
                Generation ? Generation->GetPropertyValue_InContainer(State) : INDEX_NONE,
                ArrayCount);
            auto* CenterField = Field<FStructProperty>(Run.StateClass,
                TEXT("SourceCenter"));
            const bool HasCenter = CenterField &&
                CenterField->Struct == TBaseStructure<FVector>::Get();
            const FVector Center = HasCenter
                ? *CenterField->ContainerPtrToValuePtr<FVector>(State)
                : FVector::ZeroVector;
            bool FirstMatches = false;
            bool BeaconMatches = false;
            const auto First = DescribeRole(Run, World, State, Center,
                HasCenter, RoleActors[0], 0, FirstMatches);
            const auto Beacon = DescribeRole(Run, World, State, Center,
                HasCenter, RoleActors[9], 9, BeaconMatches);
            Result += FString::Printf(TEXT(",center=%s,first={%s},beacon={%s}"),
                HasCenter ? *Center.ToString() : TEXT("absent"),
                *First, *Beacon);
            if (ArrayCount == 10)
            {
                for (int32 Role = 0; Role < 10; ++Role)
                {
                    bool Matches = false;
                    const auto Description = DescribeRole(Run, World, State,
                        Center, HasCenter, RoleActors[Role], Role, Matches);
                    if (!Matches)
                    {
                        Result += FString::Printf(TEXT(",firstMismatch={%s}"),
                            *Description);
                        break;
                    }
                }
            }
        }
        int32 Physical = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            const UClass* Class = It->GetClass();
            if (Class == Run.SourceClass[0] || Class == Run.SourceClass[1] ||
                Class == Run.SourceClass[2]) { ++Physical; }
        }
        Result += FString::Printf(TEXT(",roles=%d,cameras=%d"),
            Physical, CameraCount(World));
        auto* PC = World->GetFirstPlayerController();
        auto* Pawn = IsValid(PC) ? PC->GetPawn().Get() : nullptr;
        const bool ExactPC = IsValid(PC) && PC->GetWorld() == World &&
            PC->GetClass() == Run.PCClass && PC->HasActorBegunPlay();
        const bool ExactPawn = ExactPC && IsValid(Pawn) &&
            Pawn->GetClass() == Run.PawnClass && Pawn->GetWorld() == World &&
            Pawn->HasActorBegunPlay();
        Result += FString::Printf(TEXT(",pc=%d,local=%d,pawn=%d,tick=%d"),
            int32(ExactPC), ExactPC ? int32(PC->IsLocalController()) : -1,
            int32(ExactPawn), ExactPawn ? int32(Pawn->IsActorTickEnabled()) : -1);
        if (ExactPawn)
        {
            const FCk_Handle ActorEntity =
                UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Pawn);
            FCk_Handle CameraScope;
            const bool HasScope = Scope(Pawn, CameraScope);
            Result += FString::Printf(TEXT(",actorEcs=%s,scope=%s"),
                ck::IsValid(ActorEntity) ? *ActorEntity.ToString() : TEXT("invalid"),
                HasScope && ck::IsValid(CameraScope)
                    ? *CameraScope.ToString() : TEXT("invalid"));
        }
        Result += TEXT(")");
        return Result;
    }

    bool ReportStage(FRun& Run, const FString& Stage)
    {
        if (Run.LastBaselineStage != Stage)
        {
            Run.LastBaselineStage = Stage;
            FString Readout = DescribeWorld(Run,
                ck::auto_test::net::Get_ServerWorld(), 0);
            for (int32 I = 0; I < Clients; ++I)
            { Readout += DescribeWorld(Run,
                ck::auto_test::net::Get_ClientWorld(I), I + 1); }
            UE_LOG(LogTemp, Display, TEXT("[CameraBaseline] stage=%s%s"),
                *Stage, *Readout);
        }
        return false;
    }

    bool Context(FRun& Run, FString* OutStage = nullptr)
    {
        const auto Fail = [&](const FString& Stage) -> bool
        { if (OutStage) { *OutStage = Stage; } return false; };
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        if (!IsValid(Server) ||
            Server->GetNetMode() != (Run.Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
            !IsValid(Server->GetAuthGameMode()) ||
            Server->GetAuthGameMode()->GetClass() != Run.Mode ||
            ck::auto_test::net::Get_AllPIEWorlds().Num() != Worlds ||
            ck::auto_test::net::Get_NumClientWorlds() != Clients)
        { return Fail(TEXT("Context.ServerTopology")); }
        Run.World[0] = Server;
        Run.State[0] = Server->GetGameState();
        if (!Run.State[0].IsValid() || Run.State[0]->GetClass() != Run.StateClass)
        { return Fail(TEXT("Context.ServerState")); }
        for (int32 I = 0; I < Clients; ++I)
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(I);
            auto* Local = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* Remote = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
            if (!IsValid(Client) || Client->GetNetMode() != NM_Client ||
                !PlayerReady(Local, Client, Run) || !Local->IsLocalController() ||
                !PlayerReady(Remote, Server, Run) || Remote->IsLocalController() ||
                !IsValid(Client->GetGameState()) ||
                Client->GetGameState()->GetClass() != Run.StateClass)
            { return Fail(FString::Printf(TEXT("Context.Client%dOwner"), I)); }
            Run.World[I + 1] = Client;
            Run.State[I + 1] = Client->GetGameState();
            Run.Local[I] = Local;
            Run.Remote[I] = Remote;
        }
        if (Run.Local[0] == Run.Local[1] || Run.Remote[0] == Run.Remote[1])
        { return Fail(TEXT("Context.DistinctPeers")); }
        auto* GI = Server->GetGameInstance();
        if (!IsValid(GI) || GI->GetLocalPlayers().Num() != (Run.Dedicated ? 0 : 1))
        { return Fail(TEXT("Context.ServerLocalPlayers")); }
        if (!Run.Dedicated)
        {
            auto* LP = GI->GetLocalPlayers()[0];
            auto* Host = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
            if (!PlayerReady(Host, Server, Run) || !Host->IsLocalController())
            { return Fail(TEXT("Context.ListenHost")); }
            Run.Host = Host;
        }
        return true;
    }

    FVector RoleLocation(const FVector& Center, int32 Index)
    {
        if (Index == 0) { return Center; }
        if (Index == 9) { return Center + FVector(600.0, 0.0, 150.0); }
        static const FVector2D Offsets[8] = {
            {700,0}, {-700,0}, {0,700}, {0,-700},
            {500,500}, {-500,500}, {500,-500}, {-500,-500}};
        return Center + FVector(Offsets[Index - 1].X,
            Offsets[Index - 1].Y, 200.0);
    }

    bool ExactBank(UWorld* World, AGameStateBase* State, const FRun& Run,
        TWeakObjectPtr<AActor>* OutBank = nullptr)
    {
        if (!IsValid(World) || !IsValid(State) || State->GetWorld() != World ||
            State->GetClass() != Run.StateClass ||
            !Field<FBoolProperty>(Run.StateClass, TEXT("SourceReady")) ||
            !Field<FBoolProperty>(Run.StateClass, TEXT("SourceFailed")) ||
            !Field<FIntProperty>(Run.StateClass, TEXT("SourceGeneration")) ||
            !Field<FStructProperty>(Run.StateClass, TEXT("SourceCenter")))
        { return false; }
        auto* Ready = Field<FBoolProperty>(Run.StateClass, TEXT("SourceReady"));
        auto* Failed = Field<FBoolProperty>(Run.StateClass, TEXT("SourceFailed"));
        auto* Generation = Field<FIntProperty>(Run.StateClass, TEXT("SourceGeneration"));
        auto* CenterField = Field<FStructProperty>(Run.StateClass, TEXT("SourceCenter"));
        auto* Array = Field<FArrayProperty>(Run.StateClass, TEXT("SourceActors"));
        auto* Inner = Array ? CastField<FObjectProperty>(Array->Inner) : nullptr;
        if (!Array || !Inner || CenterField->Struct != TBaseStructure<FVector>::Get() ||
            !Ready->GetPropertyValue_InContainer(State) ||
            Failed->GetPropertyValue_InContainer(State) ||
            Generation->GetPropertyValue_InContainer(State) != 1)
        { return false; }
        const auto Center = *CenterField->ContainerPtrToValuePtr<FVector>(State);
        if (Center.ContainsNaN()) { return false; }
        FScriptArrayHelper Actors(Array, Array->ContainerPtrToValuePtr<void>(State));
        if (Actors.Num() != 10) { return false; }
        AActor* Bank[10]{};
        int32 Physical = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            const auto* Class = It->GetClass();
            if (Class == Run.SourceClass[0] || Class == Run.SourceClass[1] ||
                Class == Run.SourceClass[2]) { ++Physical; }
        }
        if (Physical != 10) { return false; }
        for (int32 I = 0; I < 10; ++I)
        {
            auto* Actor = Cast<AActor>(Inner->GetObjectPropertyValue(Actors.GetRawPtr(I)));
            const auto* Expected = Run.SourceClass[I == 0 ? 0 : I == 9 ? 2 : 1];
            const FVector Scale = I == 0 ? FVector(40,40,0.5) :
                I == 9 ? FVector(1.5,1.5,1.5) : FVector(1,1,4);
            if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() ||
                Actor->GetWorld() != World || Actor->GetClass() != Expected ||
                Actor->GetOwner() != State ||
                !UCk_CameraGym_LocalAdmission_UE::Get_HasReplicationDelivery(Actor) ||
                !Actor->IsReplicatingMovement() ||
                !Actor->GetActorLocation().Equals(RoleLocation(Center,I), 0.1) ||
                !Actor->GetActorScale3D().Equals(Scale, 0.01) ||
                Actor->GetActorQuat().ContainsNaN() ||
                !Actor->GetActorQuat().IsNormalized() ||
                !Actor->GetActorQuat().Equals(FQuat::Identity, 0.0001))
            { return false; }
            for (int32 J = 0; J < I; ++J)
            { if (Bank[J] == Actor) { return false; } }
            Bank[I] = Actor;
        }
        if (OutBank)
        { for (int32 I = 0; I < 10; ++I) { OutBank[I] = Bank[I]; } }
        return true;
    }

    uint64 IrisId(UWorld* World, AActor* Actor)
    {
        if (!IsValid(World) || !IsValid(Actor) || Actor->GetWorld() != World)
        { return 0; }
        auto* Driver = World->GetNetDriver();
        auto* System = IsValid(Driver) ? Driver->GetReplicationSystem() : nullptr;
        auto* Bridge = IsValid(System) ? System->GetReplicationBridge() : nullptr;
        if (!IsValid(Bridge)) { return 0; }
        const auto Handle = Bridge->GetReplicatedRefHandle(Actor);
        return Handle.IsValid() ? Handle.GetId() : 0;
    }

    bool SameIrisIdentities(const FRun& Run)
    {
        for (int32 I = 0; I < 10; ++I)
        {
            const auto AuthorityId = IrisId(Run.World[0].Get(),
                Run.Bank[0][I].Get());
            if (AuthorityId == 0) { return false; }
            for (int32 J = 0; J < I; ++J)
            { if (AuthorityId == IrisId(Run.World[0].Get(),
                    Run.Bank[0][J].Get())) { return false; } }
            for (int32 W = 1; W < Worlds; ++W)
            { if (AuthorityId != IrisId(Run.World[W].Get(),
                    Run.Bank[W][I].Get())) { return false; } }
        }
        return true;
    }

    bool Mode(APlayerController* PC, const FRun& Run, int32 Index,
        FCk_Handle* OutScope = nullptr)
    {
        auto* Pawn = IsValid(PC) ? PC->GetPawn().Get() : nullptr;
        int32 Actual = INDEX_NONE;
        FCk_Handle CameraScope;
        if (!IsValid(Pawn) || !Bool(Pawn, TEXT("Get_IsLocalCameraReady")) ||
            !Integer(Pawn, TEXT("Get_ModeIndex"), Actual) || Actual != Index ||
            !Scope(Pawn, CameraScope) || !Direct(CameraScope,
                UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Pawn)) ||
            !CameraScope.Has<ck::FFragment_SceneNode>() ||
            CameraScope.Has<ck::FFragment_OwningActor>() ||
            !CameraScope.Has<ck::FFragment_Net_Params>() ||
            CameraScope.Get<ck::FFragment_Net_Params>()
                .Get_ConnectionSettings().Get_Replication() !=
                    ECk_Replication::DoesNotReplicate ||
            !CameraScope.Has_All<ck::FFragment_Camera,
                ck::FFragment_Camera_Pov>() ||
            !UCk_Utils_Camera_UE::Has(CameraScope))
        { return false; }
        auto* Output = Pawn->GetComponentByClass<UCk_CameraComponent>();
        if (!IsValid(Output) ||
            FCk_Handle(Output->Get_DirectorEntity()) != CameraScope)
        { return false; }
        const auto Director = Output->Get_DirectorEntity();
        auto* ExpectedClass = Index == 0 ? Run.ThirdClass : Run.TopClass;
        if (!UCk_Utils_Camera_UE::Has_Layer(Director, ExpectedClass) ||
            UCk_Utils_Camera_UE::Get_DominantLayerClass(Director) != ExpectedClass)
        { return false; }
        const auto View = UCk_Utils_Camera_UE::Get_ViewInfo(Director);
        if (View.Location.ContainsNaN() || View.Rotation.ContainsNaN() ||
            !FMath::IsFinite(View.FOV) || View.FOV <= 0.0f)
        { return false; }
        const auto Profile = UCk_Utils_Camera_UE::Get_ComposedProfile(Director);
        const float ExpectedArm = Index == 0 ? 350.0f : 1400.0f;
        const float ExpectedFov = Index == 0 ? 80.0f : 70.0f;
        if (!FMath::IsNearlyEqual(Profile.Get_Rig().Get_BoomArmLength(),
                ExpectedArm, 1.0f) ||
            !FMath::IsNearlyEqual(Profile.Get_Sensor().Get_FOV(),
                ExpectedFov, 0.1f) ||
            Profile.Get_Rig().Get_UseFixedBoomRotation() != (Index == 1) ||
            Profile.Get_HasOrientationControl() != (Index == 0) ||
            Profile.Get_HasCollision() != (Index == 0) ||
            !FMath::IsNearlyEqual(View.FOV, ExpectedFov, 0.1f))
        { return false; }
        if (OutScope) { *OutScope = CameraScope; }
        return true;
    }

    bool LocalInput(APlayerController* PC, const FRun& Run,
        FCk_Handle_InputSource& OutSource, FCk_Handle_InputLayer& OutLayer)
    {
        OutSource = {}; OutLayer = {};
        if (!IsValid(PC) || !PC->IsLocalController() ||
            !Bool(PC, TEXT("Get_IsCameraInputReady"))) { return false; }
        auto* LP = PC->GetLocalPlayer();
        auto* Sub = IsValid(LP) ? LP->GetSubsystem<UCk_InputSource_Subsystem>() : nullptr;
        const auto Source = IsValid(Sub) ? Sub->Get_InputSource() : FCk_Handle_InputSource{};
        if (!ck::IsValid(FCk_Handle{Source})) { return false; }
        const auto Layer = UCk_Utils_InputLayer_UE::TryGet_LayerWithPriority(
            Source, UCkGym_Switchboard_Subsystem::LayerPriority_Pawn);
        FCk_Handle CameraScope;
        auto* Pawn = PC->GetPawn().Get();
        if (!ck::IsValid(FCk_Handle{Layer}) || !Scope(Pawn, CameraScope) ||
            !Direct(FCk_Handle{Layer}, CameraScope) ||
            UCk_Utils_InputLayer_UE::Get_InputSource(Layer) != Source ||
            UCk_Utils_InputLayer_UE::Get_Priority(Layer) !=
                UCkGym_Switchboard_Subsystem::LayerPriority_Pawn ||
            UCk_Utils_InputLayer_UE::Get_NumCaptures(Layer) != 10 ||
            !UCk_Utils_InputLayer_UE::Get_HasCaptureForKey(
                Layer, ECk_InputLayer_CaptureMatch::Key, EKeys::E))
        { return false; }
        OutSource = Source; OutLayer = Layer;
        return true;
    }

    bool Baseline(FRun& Run)
    {
        FString ContextStage;
        if (!Context(Run, &ContextStage))
        { return ReportStage(Run, ContextStage); }
        for (int32 I = 0; I < Worlds; ++I)
        { if (!ExactBank(Run.World[I].Get(), Run.State[I].Get(), Run,
                Run.Bank[I]))
            { return ReportStage(Run,
                FString::Printf(TEXT("ExactBank.World%d"), I)); } }
        if (!SameIrisIdentities(Run))
        { return ReportStage(Run, TEXT("IrisIdentity")); }
        const auto* CenterField = Field<FStructProperty>(
            Run.StateClass, TEXT("SourceCenter"));
        if (!CenterField || CenterField->Struct != TBaseStructure<FVector>::Get())
        { return ReportStage(Run, TEXT("CenterSchema")); }
        Run.BankCenter = *CenterField->ContainerPtrToValuePtr<FVector>(
            Run.State[0].Get());
        for (int32 I = 1; I < Worlds; ++I)
        {
            const auto& Center = *CenterField->ContainerPtrToValuePtr<FVector>(
                Run.State[I].Get());
            if (!Center.Equals(Run.BankCenter, 0.1))
            { return ReportStage(Run,
                FString::Printf(TEXT("Center.World%d"), I)); }
        }
        if (CameraCount(Run.World[0].Get()) != (Run.Dedicated ? 0 : 1) ||
            CameraCount(Run.World[1].Get()) != 1 ||
            CameraCount(Run.World[2].Get()) != 1)
        { return ReportStage(Run, TEXT("CameraCounts")); }
        for (int32 I = 0; I < Clients; ++I)
        {
            if (!Mode(Run.Local[I].Get(), Run, 0, &Run.Scope[I]))
            { return ReportStage(Run,
                FString::Printf(TEXT("Local%d.Mode"), I)); }
            if (!LocalInput(Run.Local[I].Get(), Run,
                    Run.Source[I], Run.Layer[I]))
            { return ReportStage(Run,
                FString::Printf(TEXT("Local%d.Input"), I)); }
        }
        if (Run.Scope[0] == Run.Scope[1] || Run.Layer[0] == Run.Layer[1] ||
            Run.Source[0] == Run.Source[1])
        { return ReportStage(Run, TEXT("DistinctLocalReceipts")); }
        for (int32 I = 0; I < Clients; ++I)
        {
            FCk_Handle RemoteScope;
            if (!Scope(Run.Remote[I]->GetPawn(), RemoteScope) ||
                Allocated(RemoteScope) ||
                Bool(Run.Remote[I].Get(), TEXT("Get_IsCameraInputReady")))
            { return ReportStage(Run,
                FString::Printf(TEXT("Remote%d.Inert"), I)); }
        }
        if (Run.Dedicated)
        {
            if (Run.Host.IsValid())
            { return ReportStage(Run, TEXT("Dedicated.HostAbsent")); }
            Run.LastBaselineStage = TEXT("Complete");
            return true;
        }
        if (!Mode(Run.Host.Get(), Run, 0, &Run.HostScope))
        { return ReportStage(Run, TEXT("ListenHost.Mode")); }
        if (!LocalInput(Run.Host.Get(), Run,
                Run.HostSource, Run.HostLayer))
        { return ReportStage(Run, TEXT("ListenHost.Input")); }
        if (Run.HostScope == Run.Scope[0] || Run.HostScope == Run.Scope[1])
        { return ReportStage(Run, TEXT("ListenHost.DistinctScope")); }
        Run.LastBaselineStage = TEXT("Complete");
        return true;
    }

    bool SameBank(const FRun& Run)
    {
        auto* Array = Field<FArrayProperty>(Run.StateClass,
            TEXT("SourceActors"));
        auto* Inner = Array ? CastField<FObjectProperty>(Array->Inner) : nullptr;
        if (!Inner) { return false; }
        for (int32 W = 0; W < Worlds; ++W)
        {
            if (!ExactBank(Run.World[W].Get(), Run.State[W].Get(), Run))
            { return false; }
            FScriptArrayHelper Actors(Array,
                Array->ContainerPtrToValuePtr<void>(Run.State[W].Get()));
            if (Actors.Num() != 10) { return false; }
            for (int32 I = 0; I < 10; ++I)
            { if (Run.Bank[W][I].Get() !=
                    Inner->GetObjectPropertyValue(Actors.GetRawPtr(I)))
                { return false; } }
        }
        return SameIrisIdentities(Run);
    }

    bool StableLocal(FRun& Run, int32 Owner, int32 ModeIndex)
    {
        if (Owner < 0 || Owner >= Clients || !Context(Run)) { return false; }
        FCk_Handle CurrentScope;
        FCk_Handle_InputSource CurrentSource;
        FCk_Handle_InputLayer CurrentLayer;
        return Mode(Run.Local[Owner].Get(), Run, ModeIndex, &CurrentScope) &&
            LocalInput(Run.Local[Owner].Get(), Run,
                CurrentSource, CurrentLayer) &&
            CurrentScope == Run.Scope[Owner] &&
            CurrentSource == Run.Source[Owner] &&
            CurrentLayer == Run.Layer[Owner];
    }

    bool Inject(APlayerController* PC, FKey Key,
        ECk_InputSource_EventType Type)
    {
        if (!IsValid(PC) || !PC->IsLocalController()) { return false; }
        auto* LP = PC->GetLocalPlayer();
        auto* Sub = IsValid(LP) ? LP->GetSubsystem<UCk_InputSource_Subsystem>() : nullptr;
        auto Source = IsValid(Sub) ? Sub->Get_InputSource() : FCk_Handle_InputSource{};
        if (!ck::IsValid(FCk_Handle{Source})) { return false; }
        const FCk_InputSource_RawEvent Event(
            ECk_InputSource_DeviceClass::Keyboard, Key, Type);
        return Allocated(FCk_Handle{UCk_Utils_InputSource_UE::Request_InjectRawEvent(
            Source, FCk_Request_InputSource_InjectRawEvent(Event), {})});
    }

    UCkGym_Switchboard_Subsystem* ExactSwitchboard(APlayerController* PC)
    {
        if (!IsValid(PC) || !PC->IsLocalController()) { return nullptr; }
        auto* LP = PC->GetLocalPlayer();
        auto* Switchboard = IsValid(LP)
            ? LP->GetSubsystem<UCkGym_Switchboard_Subsystem>() : nullptr;
        return IsValid(Switchboard) && Switchboard->GetOuter() == LP &&
            LP->GetWorld() == PC->GetWorld() ? Switchboard : nullptr;
    }

    bool OwnerZeroWMoving(const FRun& Run, const FVector& Before)
    {
        auto* PC = Run.Local[0].Get();
        auto* Pawn = IsValid(PC) ? PC->GetPawn().Get() : nullptr;
        return IsValid(Pawn) && Pawn->GetWorld() == Run.World[1].Get() &&
            !Pawn->GetLastMovementInputVector().ContainsNaN() &&
            Pawn->GetLastMovementInputVector().SizeSquared() > 0.25 &&
            FVector::Dist(Pawn->GetActorLocation(), Before) > 5.0 &&
            UCk_Utils_InputLayer_UE::TryGet_PressOwner(
                Run.Source[0], EKeys::W) == Run.Layer[0];
    }

    // The real client pawn moves in the actor world. Its WithActor entity must
    // sample that movable root and its private identity-offset SceneNode must
    // carry the result into the camera POV input after ordinary scheduler ticks.
    bool OwnerZeroScopeFollowsPawn(FRun& Run)
    {
        if (!StableLocal(Run, 0, 1)) { return false; }
        auto* PC = Run.Local[0].Get();
        auto* Pawn = IsValid(PC) ? PC->GetPawn().Get() : nullptr;
        auto* RootComponent = IsValid(Pawn) ? Pawn->GetRootComponent() : nullptr;
        if (!IsValid(Pawn) || !IsValid(RootComponent) ||
            Pawn->GetWorld() != Run.World[1].Get() ||
            RootComponent->Mobility != EComponentMobility::Movable)
        { return false; }

        const auto PawnEntity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Pawn);
        const auto CameraScope = Run.Scope[0];
        FCk_Handle CurrentScope;
        if (!ck::IsValid(PawnEntity) || !ck::IsValid(CameraScope) ||
            !Scope(Pawn, CurrentScope) || CurrentScope != CameraScope ||
            !Direct(CameraScope, PawnEntity) ||
            !PawnEntity.Has_All<ck::FFragment_Transform,
                ck::FFragment_Transform_RootComponent,
                ck::FTag_Transform_Movable>() ||
            PawnEntity.Has<ck::FTag_Transform_ExternallyDriven>() ||
            !CameraScope.Has_All<ck::FFragment_SceneNode,
                ck::FFragment_Transform>() ||
            PawnEntity.Get<ck::FFragment_Transform_RootComponent>()
                .Get_RootComponent().Get() != RootComponent)
        { return false; }

        const auto ActorLocation = Pawn->GetActorLocation();
        const auto PawnEntityLocation = PawnEntity.Get<ck::FFragment_Transform>()
            .Get_Transform().GetLocation();
        const auto ScopeLocation = CameraScope.Get<ck::FFragment_Transform>()
            .Get_Transform().GetLocation();
        return !ActorLocation.ContainsNaN() &&
            !PawnEntityLocation.ContainsNaN() && !ScopeLocation.ContainsNaN() &&
            PawnEntityLocation.Equals(ActorLocation, 0.1) &&
            ScopeLocation.Equals(PawnEntityLocation, 0.1) &&
            FVector::Dist(ScopeLocation, Run.Owner0LocationBeforeFreshW) > 5.0;
    }

    bool QuietHeldW(FRun& Run, bool ExpectedMenuOpen, int32& Consecutive)
    {
        if (!SameBank(Run) || !Context(Run) || !StableLocal(Run, 0, 1) ||
            !StableLocal(Run, 1, 0) ||
            (!Run.Dedicated && !Mode(Run.Host.Get(), Run, 0)))
        { Consecutive = 0; return false; }
        auto* PC = Run.Local[0].Get();
        auto* Pawn = IsValid(PC) ? PC->GetPawn().Get() : nullptr;
        auto* Menu = ExactSwitchboard(PC);
        if (!IsValid(Pawn) || !IsValid(Menu) ||
            Menu->Get_IsOpen() != ExpectedMenuOpen ||
            UCk_Utils_InputLayer_UE::TryGet_PressOwner(
                Run.Source[0], EKeys::W) != Run.Layer[0] ||
            Pawn->GetLastMovementInputVector().ContainsNaN() ||
            !Pawn->GetLastMovementInputVector().IsNearlyZero(0.01) ||
            Pawn->GetPendingMovementInputVector().ContainsNaN() ||
            !Pawn->GetPendingMovementInputVector().IsNearlyZero(0.01))
        { Consecutive = 0; return false; }
        return ++Consecutive >= 8;
    }

    struct FRestore final : IAutomationLatentCommand
    {
        TSharedRef<FRun> Run;
        explicit FRestore(TSharedRef<FRun> In) : Run(In) {}
        bool Update() override { Run->Restore(); return true; }
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCameraGym_SharedWorld_Network,
    "Ck.Camera.Gym.SharedWorld.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
    EAutomationTestFlags::EngineFilter)

bool FCkCameraGym_SharedWorld_Network::RunTest(const FString& Parameters)
{
    using namespace ck_camera_authored_network;
    auto Run = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters,
            Run->Dedicated))
    { AddError(TEXT("unsupported Camera network topology")); return false; }
    Run->BaseMode = FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    Run->Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    Run->StateClass = FSoftClassPath(StatePath).TryLoadClass<AGameStateBase>();
    Run->PCClass = FSoftClassPath(PCPath).TryLoadClass<APlayerController>();
    Run->PawnClass = FSoftClassPath(PawnPath).TryLoadClass<APawn>();
    Run->SourceClass[0] = FSoftClassPath(FloorPath).TryLoadClass<AActor>();
    Run->SourceClass[1] = FSoftClassPath(PillarPath).TryLoadClass<AActor>();
    Run->SourceClass[2] = FSoftClassPath(BeaconPath).TryLoadClass<AActor>();
    Run->ThirdClass = FSoftClassPath(ThirdPath)
        .TryLoadClass<UCk_CameraLayer_EntityScript>();
    Run->TopClass = FSoftClassPath(TopPath)
        .TryLoadClass<UCk_CameraLayer_EntityScript>();
    if (!TestTrue(TEXT("all authored Camera and reference actor classes resolve"),
            Run->BaseMode && Run->Mode && Run->StateClass &&
            Run->PCClass && Run->PawnClass && Run->SourceClass[0] &&
            Run->SourceClass[1] && Run->SourceClass[2] &&
            Run->ThirdClass && Run->TopClass))
    { return false; }
    if (!TestTrue(TEXT("local readiness, scope, input and mode functions reflect"),
            Run->PawnClass->FindFunctionByName(FName(TEXT("Get_IsLocalCameraReady"))) &&
            Run->PawnClass->FindFunctionByName(FName(TEXT("Get_LocalCameraScope"))) &&
            Run->PawnClass->FindFunctionByName(FName(TEXT("Get_ModeIndex"))) &&
            Run->PCClass->FindFunctionByName(FName(TEXT("Get_IsCameraInputReady")))))
    { return false; }
    if (!TestFalse(TEXT("null actor has no replication delivery"),
            UCk_CameraGym_LocalAdmission_UE::Get_HasReplicationDelivery(nullptr)) ||
        !TestFalse(TEXT("source CDO has no replication delivery"),
            UCk_CameraGym_LocalAdmission_UE::Get_HasReplicationDelivery(
                Run->SourceClass[0]->GetDefaultObject<AActor>())))
    { return false; }
    const auto* Settings = GetDefault<UCkGym_StartupSettings>();
    Run->Startup = Settings->StartupMode;
    Run->LastGym = Settings->LastGymName;
    Run->Recents = Settings->RecentGymNames;
    Run->RestoreCaptured = true;
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode =
        ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        Worlds, EntryMap, Parameters, Run->BaseMode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(Worlds, 60));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() {
            auto* W = ck::auto_test::net::Get_ServerWorld();
            auto* GI = IsValid(W) ? W->GetGameInstance() : nullptr;
            auto* Registry = IsValid(GI)
                ? GI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
            return IsValid(Registry) &&
                Registry->Find_GymIndexByName(TEXT("Camera")) != INDEX_NONE;
        }), 30, TEXT("authored Camera gym registered")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W) {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owner requests real Camera gym travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::
                    TryRequest_TravelToGymFromPlayer(PC, TEXT("Camera")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            FString Stage;
            if (Context(*Run, &Stage)) { return true; }
            return ReportStage(*Run, Stage);
        }), 60, TEXT("post-travel Camera worlds and exact players converge")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { return Baseline(*Run); }), 60,
        TEXT("one authority Camera bank and each local camera/input converge")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]()
        {
            return SameBank(*Run) && StableLocal(*Run, 0, 0) &&
                StableLocal(*Run, 1, 0) &&
                (Run->Dedicated || Mode(Run->Host.Get(), *Run, 0));
        }),
        TEXT("whole Camera source bank and exact local owners remain ready"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W) {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owner zero injects E through real CkInput source"),
                Inject(PC, EKeys::E, ECk_InputSource_EventType::Pressed));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return SameBank(*Run) && StableLocal(*Run, 0, 1) &&
                StableLocal(*Run, 1, 0) &&
                (Run->Dedicated || Mode(Run->Host.Get(), *Run, 0)) &&
                UCk_Utils_InputLayer_UE::TryGet_PressOwner(
                    Run->Source[0], EKeys::E) == Run->Layer[0] &&
                UCk_Utils_InputLayer_UE::TryGet_PressOwner(
                    Run->Source[1], EKeys::E) != Run->Layer[1];
        }), 20, TEXT("E routes through owner zero layer, settles TopDown, peer stays ThirdPerson")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W) {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owner zero releases routed E"),
                Inject(PC, EKeys::E, ECk_InputSource_EventType::Released));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* W) {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            auto* Pawn = IsValid(PC) ? PC->GetPawn().Get() : nullptr;
            if (IsValid(Pawn))
            { Run->Owner0LocationBeforeW = Pawn->GetActorLocation(); }
            TestTrue(TEXT("owner zero injects W into its real input source"),
                IsValid(Pawn) && PC == Run->Local[0].Get() &&
                Inject(PC, EKeys::W, ECk_InputSource_EventType::Pressed));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return SameBank(*Run) && StableLocal(*Run, 0, 1) &&
                OwnerZeroWMoving(*Run, Run->Owner0LocationBeforeW);
        }), 10, TEXT("routed W commands real owner-zero movement")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* W) {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            auto* Menu = ExactSwitchboard(PC);
            if (IsValid(Menu) && PC == Run->Local[0].Get())
            { Menu->Request_Open(); }
            TestTrue(TEXT("owner-zero real local switchboard opens before W keyup"),
                IsValid(Menu) && Menu->Get_IsOpen());
        })));
    auto MenuQuiet = MakeShared<int32>(0);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run, MenuQuiet]() {
            return QuietHeldW(*Run, true, *MenuQuiet);
        }), 10, TEXT("open menu clears held W and eight successive movement inputs stay zero")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* W) {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            auto* Menu = ExactSwitchboard(PC);
            if (IsValid(Menu) && PC == Run->Local[0].Get())
            { Menu->Request_Close(); }
            TestTrue(TEXT("owner-zero real local switchboard closes with W still held"),
                IsValid(Menu) && !Menu->Get_IsOpen());
        })));
    auto ClosedQuiet = MakeShared<int32>(0);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run, ClosedQuiet]() {
            return QuietHeldW(*Run, false, *ClosedQuiet);
        }), 10, TEXT("closing menu does not resume stale W across eight frames")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W) {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owner zero releases formerly held W"),
                Inject(PC, EKeys::W, ECk_InputSource_EventType::Released));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return StableLocal(*Run, 0, 1) &&
                UCk_Utils_InputLayer_UE::TryGet_PressOwner(
                    Run->Source[0], EKeys::W) != Run->Layer[0];
        }), 10, TEXT("physical W release retires prior routed press owner")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* W) {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            auto* Pawn = IsValid(PC) ? PC->GetPawn().Get() : nullptr;
            if (IsValid(Pawn))
            { Run->Owner0LocationBeforeFreshW = Pawn->GetActorLocation(); }
            TestTrue(TEXT("owner zero makes a fresh routed W press after menu closure"),
                IsValid(Pawn) && PC == Run->Local[0].Get() &&
                Inject(PC, EKeys::W, ECk_InputSource_EventType::Pressed));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return StableLocal(*Run, 0, 1) &&
                OwnerZeroWMoving(*Run, Run->Owner0LocationBeforeFreshW);
        }), 10, TEXT("fresh W press resumes real local movement")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W) {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owner zero releases fresh routed W"),
                Inject(PC, EKeys::W, ECk_InputSource_EventType::Released));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return StableLocal(*Run, 0, 1) &&
                UCk_Utils_InputLayer_UE::TryGet_PressOwner(
                    Run->Source[0], EKeys::W) != Run->Layer[0];
        }), 10, TEXT("fresh W release clears routed press owner")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(3));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return OwnerZeroScopeFollowsPawn(*Run);
        }), 10, TEXT("local PawnEntity and camera scope settle onto moved owner-zero actor")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            TestTrue(TEXT("fresh W motion reaches the exact owner-local camera scope"),
                World == Run->World[1].Get() && OwnerZeroScopeFollowsPawn(*Run));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() {
            if (!ck::auto_test::net::Get_AllPIEWorlds().IsEmpty()) { return false; }
            for (const auto& Scope : Run->Scope)
            { if (Allocated(Scope)) { return false; } }
            for (int32 W = 0; W < Worlds; ++W)
            { for (int32 I = 0; I < 10; ++I)
                { if (Run->Bank[W][I].IsValid()) { return false; } } }
            return !Allocated(Run->HostScope) &&
                !Allocated(FCk_Handle{Run->Source[0]}) &&
                !Allocated(FCk_Handle{Run->Source[1]}) &&
                !Allocated(FCk_Handle{Run->HostSource}) &&
                !Allocated(FCk_Handle{Run->Layer[0]}) &&
                !Allocated(FCk_Handle{Run->Layer[1]}) &&
                !Allocated(FCk_Handle{Run->HostLayer});
        }), TEXT("EndPIE physically retires all retained local Camera scopes and input layers"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FRestore(Run));
    return true;
}
#endif
