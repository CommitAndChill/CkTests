#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_StartupSettings.h"
#include "CkGymRegistry_Utils.h"
#include "CkGym_WireTransformRuntime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkUnrealComponentGym_Runtime.h"
#include "CkUnrealComponentGym_Presenter.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkTimer/CkTimer_Utils.h"
#include "CkUnrealComponent/CkUnrealComponent_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Utils.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_unreal_component_shared_eight
{
    constexpr int32 Worlds = 3;
    constexpr int32 Clients = 2;
    constexpr int32 Roles = 8;
    constexpr auto Entry = TEXT("/Engine/Maps/Entry");
    constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_UnrealComponentGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_UnrealComponentGym_GameState");
    constexpr auto ControllerPath = TEXT("/Script/Angelscript.Ck_UnrealComponentGym_PlayerController");
    constexpr auto DriverPath = TEXT("/Script/Angelscript.Ck_UnrealComponentGym_Driver");
    constexpr auto SourceScriptPath = TEXT("/Script/Angelscript.Ck_UnrealComponentGym_SourceWithActor");
    constexpr auto ProbePath = TEXT("/Script/Angelscript.Ck_UnrealComponentGym_AdmissionProbe");
    constexpr const TCHAR* Tags[Roles] = {
        TEXT("Gym.UnrealComponent.StaticMesh"), TEXT("Gym.UnrealComponent.PointLight"),
        TEXT("Gym.UnrealComponent.SpotLight"), TEXT("Gym.UnrealComponent.TextRender"),
        TEXT("Gym.UnrealComponent.Arrow"), TEXT("Gym.UnrealComponent.Box"),
        TEXT("Gym.UnrealComponent.Sphere"), TEXT("Gym.UnrealComponent.Capsule")};
    constexpr const TCHAR* VisualNames[Roles] = {
        TEXT("StaticMesh"), TEXT("PointLight"), TEXT("SpotLight"),
        TEXT("TextRender"), TEXT("Arrow"), TEXT("Box"), TEXT("Sphere"),
        TEXT("Capsule")};
    auto VisualClass(int32 Role) -> UClass*
    {
        switch (Role)
        {
        case 0: return UStaticMeshComponent::StaticClass();
        case 1: return UPointLightComponent::StaticClass();
        case 2: return USpotLightComponent::StaticClass();
        case 3: return UTextRenderComponent::StaticClass();
        case 4: return UArrowComponent::StaticClass();
        case 5: return UBoxComponent::StaticClass();
        case 6: return USphereComponent::StaticClass();
        case 7: return UCapsuleComponent::StaticClass();
        default: return nullptr;
        }
    }

    template<class T> auto Field(const UStruct* Struct, const TCHAR* Name) -> T*
    { return Struct ? FindFProperty<T>(Struct, Name) : nullptr; }

    auto I32(const UStruct* Struct, const void* Data, const TCHAR* Name) -> int32
    {
        const auto* P = Field<FIntProperty>(Struct, Name);
        return Data && P ? P->GetPropertyValue_InContainer(Data) : INDEX_NONE;
    }
    auto I64(const UStruct* Struct, const void* Data, const TCHAR* Name) -> int64
    {
        const auto* P = Field<FInt64Property>(Struct, Name);
        return Data && P ? P->GetPropertyValue_InContainer(Data) : -1;
    }
    auto Lifecycle(const UStruct* Struct, const void* Data) -> int32
    {
        if (!Data) { return INDEX_NONE; }
        if (const auto* Byte = Field<FByteProperty>(Struct, TEXT("Lifecycle")))
        { return Byte->GetPropertyValue_InContainer(Data); }
        const auto* Enum = Field<FEnumProperty>(Struct, TEXT("Lifecycle"));
        return Enum && Enum->GetUnderlyingProperty()
            ? int32(Enum->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(
                Enum->ContainerPtrToValuePtr<void>(Data))) : INDEX_NONE;
    }
    auto Poses(const UStruct* Struct, const void* Data) -> const TArray<FTransform>*
    {
        const auto* P = Field<FArrayProperty>(Struct, TEXT("ComponentWorldPoses"));
        const auto* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
        return Data && Inner && Inner->Struct == TBaseStructure<FTransform>::Get()
            ? P->ContainerPtrToValuePtr<TArray<FTransform>>(Data) : nullptr;
    }
    auto Active(const UStruct* Struct, const void* Data, int32 Generation = 0) -> bool
    {
        const auto* A = Poses(Struct, Data);
        return Lifecycle(Struct, Data) == 1 &&
            I32(Struct, Data, TEXT("CatalogVersion")) == 1 &&
            I32(Struct, Data, TEXT("Generation")) > 0 &&
            (Generation == 0 || I32(Struct, Data, TEXT("Generation")) == Generation) &&
            I64(Struct, Data, TEXT("Sequence")) > 0 &&
            I64(Struct, Data, TEXT("FrameSerial")) > 0 &&
            A && A->Num() == Roles;
    }
    auto Failed(const UStruct* Struct, const void* Data, int32 Generation) -> bool
    {
        const auto* A = Poses(Struct, Data);
        const auto* Reason = Field<FStrProperty>(Struct, TEXT("FailureReason"));
        return Lifecycle(Struct, Data) == 3 &&
            I32(Struct, Data, TEXT("Generation")) == Generation &&
            I64(Struct, Data, TEXT("Sequence")) > 0 &&
            I64(Struct, Data, TEXT("FrameSerial")) == 0 &&
            A && A->IsEmpty() && Reason && !Reason->GetPropertyValue_InContainer(Data).IsEmpty();
    }

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
            if (!IsValid(Target) || !IsValid(Function) || !Args) { return false; }
            Target->ProcessEvent(Function, Args->GetStructMemory());
            return true;
        }
    };
    auto PureHandle(UObject* Target, const TCHAR* Name) -> FCk_Handle
    {
        FCall C(Target, Name);
        const auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        return P && P->Struct == FCk_Handle::StaticStruct() &&
            C.Function->NumParms == 1 && C.Send()
            ? *P->ContainerPtrToValuePtr<FCk_Handle>(C.Args->GetStructMemory()) : FCk_Handle{};
    }
    template<class T> auto PureObject(UObject* Target, const TCHAR* Name) -> T*
    {
        FCall C(Target, Name);
        const auto* P = Field<FObjectPropertyBase>(C.Function, TEXT("ReturnValue"));
        return P && C.Function->NumParms == 1 && C.Send()
            ? Cast<T>(P->GetObjectPropertyValue_InContainer(C.Args->GetStructMemory())) : nullptr;
    }
    auto PureFalseBool(UObject* Target, const TCHAR* Name) -> bool
    {
        FCall C(Target, Name);
        const auto* P = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        return P && C.Function->NumParms == 1 && C.Send() &&
            !P->GetPropertyValue_InContainer(C.Args->GetStructMemory());
    }
    auto AdmissionClosed(UWorld* W, UClass* ProbeClass) -> bool
    {
        if (!IsValid(W) || !IsValid(ProbeClass)) { return false; }
        const FCk_Handle Root = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*W);
        if (!ck::IsValid(Root)) { return false; }
        auto* Object = NewObject<UObject>(W, ProbeClass);
        FCall C(Object, TEXT("ClosedAndReserveRejected"));
        auto* Arg = Field<FStructProperty>(C.Function, TEXT("InWorld"));
        auto* Return = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        if (!Arg || Arg->Struct != FCk_Handle::StaticStruct() || !Return ||
            !Return->HasAnyPropertyFlags(CPF_ReturnParm) ||
            C.Function->NumParms != 2) { return false; }
        Arg->Struct->CopyScriptStruct(
            Arg->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory()), &Root);
        return C.Send() && Return->GetPropertyValue_InContainer(
            C.Args->GetStructMemory());
    }
    auto PureHandles(UObject* Target, const TCHAR* Name, TArray<FCk_Handle>& Out) -> bool
    {
        Out.Reset();
        FCall C(Target, Name);
        const auto* P = Field<FArrayProperty>(C.Function, TEXT("ReturnValue"));
        const auto* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
        if (!Inner || Inner->Struct != FCk_Handle::StaticStruct() ||
            C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = *P->ContainerPtrToValuePtr<TArray<FCk_Handle>>(C.Args->GetStructMemory());
        return true;
    }
    auto PureAnchors(UObject* Target, TArray<FVector>& Out) -> bool
    {
        Out.Reset();
        FCall C(Target, TEXT("Get_CapturedUnrealComponentAnchors"));
        const auto* P = Field<FArrayProperty>(C.Function, TEXT("ReturnValue"));
        const auto* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
        if (!Inner || Inner->Struct != TBaseStructure<FVector>::Get() ||
            C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = *P->ContainerPtrToValuePtr<TArray<FVector>>(C.Args->GetStructMemory());
        return true;
    }
    auto PureStruct(UObject* Target, const TCHAR* Name,
        const UScriptStruct* Type) -> TSharedPtr<FStructOnScope>
    {
        FCall C(Target, Name);
        const auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!Type || !P || P->Struct != Type || C.Function->NumParms != 1 ||
            !C.Send()) { return {}; }
        auto Copy = MakeShared<FStructOnScope>(Type);
        Type->CopyScriptStruct(Copy->GetStructMemory(),
            P->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory()));
        return Copy;
    }
    auto Ack(UObject* PC, int32& Serial, int32& Generation, int32& Result) -> bool
    {
        const auto* Struct = PC ? PC->GetClass() : nullptr;
        FCall C(PC, TEXT("Get_UnrealComponentControlAck"));
        const auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!Struct || !P || C.Function->NumParms != 1 || !C.Send()) { return false; }
        const void* Data = P->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory());
        Serial = I32(P->Struct, Data, TEXT("RequestSerial"));
        Generation = I32(P->Struct, Data, TEXT("Generation"));
        Result = I32(P->Struct, Data, TEXT("Result"));
        return Serial >= 0 && Generation >= 0 && Result >= 0;
    }
    auto Rpc(UObject* PC, int32 Serial, int32 Generation) -> bool
    {
        FCall C(PC, TEXT("Server_RequestUnrealComponentReset"));
        auto* A = Field<FIntProperty>(C.Function, TEXT("Serial"));
        auto* B = Field<FIntProperty>(C.Function, TEXT("Generation"));
        if (!A || !B || C.Function->NumParms != 2 ||
            !C.Function->HasAllFunctionFlags(static_cast<EFunctionFlags>(FUNC_Net | FUNC_NetServer)) ||
            C.Function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(
                FUNC_NetClient | FUNC_NetMulticast | FUNC_Static)))
        { return false; }
        A->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Serial);
        B->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Generation);
        return C.Send();
    }

    struct FSource
    {
        TWeakObjectPtr<AActor> Driver, Host;
        TWeakObjectPtr<UActorComponent> Component;
        FCk_Handle Entity, Feature, Timer;
    };
    struct FRun
    {
        bool Dedicated = false;
        UClass *BaseMode = nullptr, *Mode = nullptr, *StateClass = nullptr;
        UClass *PCClass = nullptr, *DriverClass = nullptr, *ProbeClass = nullptr;
        UClass* SourceScriptClass = nullptr;
        FStructProperty* Snapshot = nullptr;
        UScriptStruct* SnapshotType = nullptr;
        TWeakObjectPtr<UWorld> World[Worlds];
        TWeakObjectPtr<APlayerController> Local[Clients], Remote[Clients], Host;
        TArray<FCk_Handle> Stations[Clients], HostStations;
        TArray<FVector> Anchors[Clients], HostAnchors;
        FSource Source[Roles];
        FCk_Handle SourceOwner;
        TMap<int64, TSharedPtr<FStructOnScope>> History;
        FDelegateHandle Observer;
        ECkGym_StartupMode Startup = ECkGym_StartupMode::Cycler;
        FString Last;
        TArray<FString> Recents;
        int32 FirstGeneration = 0;
        int64 FirstSequence = 0;
        bool SawSourceMotion = false, SawClientMotion[Clients]{};
        bool SawResetBoundary = false, TerminalReactivated = false;
        FString FirstReadinessPhase = TEXT("not entered");
        int32 TerminalFrames = 0;
        TWeakObjectPtr<UActorComponent> MutatedComponent;
        TWeakObjectPtr<ACk_UnrealComponentGym_Presenter_UE> Presenter[Clients];

        AGameStateBase* State(int32 Index) const
        {
            auto* W = World[Index].Get();
            auto* S = IsValid(W) ? W->GetGameState() : nullptr;
            return IsValid(S) && S->GetClass() == StateClass && S->GetWorld() == W ? S : nullptr;
        }
        const void* Data(int32 Index) const
        {
            auto* S = State(Index);
            return S && Snapshot ? Snapshot->ContainerPtrToValuePtr<void>(S) : nullptr;
        }
        void Restore()
        {
            if (Observer.IsValid())
            { FWorldDelegates::OnWorldPostActorTick.Remove(Observer); Observer.Reset(); }
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = Startup;
            Settings->LastGymName = Last;
            Settings->RecentGymNames = Recents;
            Settings->SaveConfig();
        }
    };

    auto Registry(UWorld* W) -> UCkGym_Registry_Subsystem*
    {
        auto* GI = IsValid(W) ? W->GetGameInstance() : nullptr;
        return IsValid(GI) ? GI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }
    auto Context(FRun& F) -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        if (!IsValid(Server) || Server->GetNetMode() !=
            (F.Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
            !IsValid(Server->GetAuthGameMode()) ||
            Server->GetAuthGameMode()->GetClass() != F.Mode ||
            ck::auto_test::net::Get_NumClientWorlds() != Clients)
        { return false; }
        F.World[0] = Server;
        if (!IsValid(F.State(0))) { return false; }
        for (int32 I = 0; I < Clients; ++I)
        {
            auto* W = ck::auto_test::net::Get_ClientWorld(I);
            if (!IsValid(W) || W == Server || W->GetNetMode() != NM_Client ||
                (I == 1 && W == F.World[1].Get())) { return false; }
            F.World[I+1] = W;
            auto* PC = W->GetFirstPlayerController();
            auto* Remote = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
            if (!IsValid(PC) || !IsValid(Remote) || PC->GetClass() != F.PCClass ||
                Remote->GetClass() != F.PCClass || !PC->IsLocalController() ||
                !Remote->HasAuthority() || !IsValid(PC->GetPawn()) ||
                !IsValid(Remote->GetPawn()) || !IsValid(F.State(I+1)))
            { return false; }
            F.Local[I] = PC; F.Remote[I] = Remote;
        }
        if (F.Local[0] == F.Local[1] || F.Remote[0] == F.Remote[1]) { return false; }
        auto* GI = Server->GetGameInstance();
        if (!IsValid(GI) || GI->GetLocalPlayers().Num() != (F.Dedicated ? 0 : 1))
        { return false; }
        if (!F.Dedicated)
        {
            auto* LP = GI->GetLocalPlayers()[0];
            auto* Host = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
            if (!IsValid(Host) || Host->GetClass() != F.PCClass ||
                !Host->IsLocalController() || !Host->HasAuthority() ||
                !IsValid(Host->GetPawn())) { return false; }
            F.Host = Host;
        }
        return true;
    }
    auto Drivers(const FRun& F, TArray<AActor*>& Out) -> bool
    {
        Out.Reset();
        auto* State = F.State(0);
        const auto* P = Field<FArrayProperty>(F.StateClass, TEXT("ServerDrivers"));
        const auto* Inner = P ? CastField<FObjectPropertyBase>(P->Inner) : nullptr;
        if (!IsValid(State) || !Inner) { return false; }
        FScriptArrayHelper Array(P, P->ContainerPtrToValuePtr<void>(State));
        if (Array.Num() != Roles) { return false; }
        for (int32 I = 0; I < Roles; ++I)
        {
            auto* A = Cast<AActor>(Inner->GetObjectPropertyValue(Array.GetRawPtr(I)));
            if (!IsValid(A) || A->GetClass() != F.DriverClass ||
                A->GetWorld() != F.World[0].Get() || !A->HasAuthority() ||
                A->GetIsReplicated()) { return false; }
            Out.Add(A);
        }
        return Out.Num() == Roles;
    }
    auto CountDrivers(UWorld* W, UClass* DriverClass) -> int32
    {
        if (!IsValid(W) || !IsValid(DriverClass)) { return INDEX_NONE; }
        int32 Count = 0;
        for (TActorIterator<AActor> It(W); It; ++It)
        { if (IsValid(*It) && It->GetClass() == DriverClass) { ++Count; } }
        return Count;
    }
    auto ReadSource(FRun& F, bool Capture) -> bool
    {
        TArray<AActor*> Actors;
        if (!Drivers(F, Actors) || CountDrivers(F.World[0].Get(), F.DriverClass) != Roles)
        { return false; }
        auto* State = F.State(0);
        const auto* OwnerP = Field<FStructProperty>(F.StateClass, TEXT("SourceOwner"));
        if (!OwnerP || OwnerP->Struct != FCk_Handle::StaticStruct()) { return false; }
        const FCk_Handle Owner = *OwnerP->ContainerPtrToValuePtr<FCk_Handle>(State);
        if (!ck::IsValid(Owner)) { return false; }
        if (Capture) { F.SourceOwner = Owner; }
        else if (Owner != F.SourceOwner) { return false; }
        TSet<FCk_Handle> Entities, Features, Timers;
        TSet<UActorComponent*> Components;
        for (int32 I = 0; I < Roles; ++I)
        {
            auto* Driver = Actors[I];
            const FCk_Handle Entity = PureHandle(Driver, TEXT("Get_SourceEntity"));
            const FCk_Handle Feature = PureHandle(Driver, TEXT("Get_SourceComponentHandle"));
            const FCk_Handle Timer = PureHandle(Driver, TEXT("Get_SourceTimerHandle"));
            auto* Component = PureObject<UActorComponent>(Driver, TEXT("Get_SourceComponentObject"));
            auto* Host = PureObject<AActor>(Driver, TEXT("Get_SourceHostActor"));
            if (!ck::IsValid(Entity) || !ck::IsValid(Feature) || !ck::IsValid(Timer) ||
                !IsValid(Component) || !IsValid(Host) || Entities.Contains(Entity) ||
                Features.Contains(Feature) || Timers.Contains(Timer) ||
                Components.Contains(Component) ||
                !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Entity) ||
                !UCk_Utils_UnrealComponent_UE::Has(Feature) ||
                !UCk_Utils_Timer_UE::Has(Timer) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Feature) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Feature) != Entity ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Timer) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Timer) != Entity ||
                UCk_Utils_Timer_UE::Get_CurrentState(
                    UCk_Utils_Timer_UE::CastChecked(Timer)) != ECk_Timer_State::Running ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Entity) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Entity) != Owner ||
                UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(Entity) != F.World[0].Get() ||
                UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor_Recursive(Entity) != Driver ||
                !IsValid(UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity)) ||
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity)->GetClass() !=
                    F.SourceScriptClass)
            { return false; }
            FCkUnrealComponentGym_SourceSample Sample;
            UActorComponent* Captured = nullptr;
            AActor* CapturedHost = nullptr;
            if (!UCk_UnrealComponentGym_Runtime_UE::TryCaptureSourceComponent(
                    Driver, Entity, Feature, I, Captured, CapturedHost) ||
                Captured != Component || CapturedHost != Host)
            { return false; }
            if (!UCk_UnrealComponentGym_Runtime_UE::TryGetSourceSample(
                    Driver, Host, Component, Entity, Feature, I, Sample) ||
                !Sample.bStyleValid || Sample.Role != I ||
                Sample.ComponentWorldPose.ContainsNaN() ||
                !Sample.ComponentWorldPose.IsValid())
            { return false; }
            if (Capture)
            {
                F.Source[I].Driver = Driver; F.Source[I].Host = Host;
                F.Source[I].Component = Component; F.Source[I].Entity = Entity;
                F.Source[I].Feature = Feature; F.Source[I].Timer = Timer;
            }
            else if (F.Source[I].Driver.Get() != Driver ||
                F.Source[I].Host.Get() != Host ||
                F.Source[I].Component.Get() != Component ||
                F.Source[I].Entity != Entity || F.Source[I].Feature != Feature ||
                F.Source[I].Timer != Timer)
            { return false; }
            Entities.Add(Entity); Features.Add(Feature);
            Timers.Add(Timer); Components.Add(Component);
        }
        return Entities.Num() == Roles && Features.Num() == Roles &&
            Timers.Num() == Roles && Components.Num() == Roles;
    }
    struct FBranchCounts
    {
        int32 WithActor = 0, Feature = 0, Timer = 0;
        TSet<FCk_Handle> Seen;
    };
    auto CountSourceBranch(const FCk_Handle& Node, UClass* SourceScriptClass,
        FBranchCounts& Counts,
        int32 Depth = 0) -> bool
    {
        if (Depth > 12 || Counts.Seen.Num() > 128 || Counts.Seen.Contains(Node) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Node))
        { return false; }
        Counts.Seen.Add(Node);
        auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Node);
        Counts.WithActor += IsValid(Script) &&
            Script->GetClass() == SourceScriptClass;
        Counts.Feature += UCk_Utils_UnrealComponent_UE::Has(Node);
        Counts.Timer += UCk_Utils_Timer_UE::Has(Node);
        for (const FCk_Handle& Child : UCk_Utils_EntityLifetime_UE::
                Get_LifetimeDependents(Node))
        {
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child))
            { continue; }
            if (!UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) != Node ||
                !CountSourceBranch(Child, SourceScriptClass, Counts, Depth+1))
            { return false; }
        }
        return true;
    }
    auto ExactSourceBranch(const FRun& F) -> bool
    {
        FBranchCounts Counts;
        return ck::IsValid(F.SourceOwner) &&
            CountSourceBranch(F.SourceOwner, F.SourceScriptClass, Counts) &&
            Counts.WithActor == Roles && Counts.Feature == Roles &&
            Counts.Timer == Roles;
    }
    auto RetainedSourceIds(const FRun& F) -> bool
    {
        TArray<AActor*> Actors;
        if (!Drivers(F, Actors) ||
            CountDrivers(F.World[0].Get(), F.DriverClass) != Roles)
        { return false; }
        for (int32 I = 0; I < Roles; ++I)
        {
            if (Actors[I] != F.Source[I].Driver.Get() ||
                PureHandle(Actors[I], TEXT("Get_SourceEntity")) !=
                    F.Source[I].Entity ||
                PureHandle(Actors[I], TEXT("Get_SourceComponentHandle")) !=
                    F.Source[I].Feature ||
                PureHandle(Actors[I], TEXT("Get_SourceTimerHandle")) !=
                    F.Source[I].Timer ||
                PureObject<UActorComponent>(Actors[I],
                    TEXT("Get_SourceComponentObject")) !=
                    F.Source[I].Component.Get() ||
                !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                    F.Source[I].Entity))
            { return false; }
        }
        return true;
    }
    auto NoClientSource(const FRun& F, int32 Client) -> bool
    {
        auto* W = F.World[Client+1].Get();
        if (!IsValid(W) || CountDrivers(W, F.DriverClass) != 0) { return false; }
        const FCk_Handle Root = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*W);
        if (!ck::IsValid(Root)) { return false; }
        int32 SourceFeatures = 0, SourceScripts = 0;
        const auto Registry = Root.Get_RegistryView();
        // Local stations also own UnrealComponent features. The source driver
        // authors this distinct debug name for its eight physical features.
        Registry.View<ck::FFragment_UnrealComponent,
            ck::FFragment_UnrealComponent_Params>().ForEach(
            [&SourceFeatures](FCk_Entity, const ck::FFragment_UnrealComponent&,
                const ck::FFragment_UnrealComponent_Params& Params)
            {
                if (Params.Get_DebugName() == FName(TEXT("UnrealComponentGym")))
                { ++SourceFeatures; }
            });
        Registry.View<ck::FFragment_EntityScript>().ForEach(
            [&SourceScripts, &F](FCk_Entity, const ck::FFragment_EntityScript& Fragment)
            {
                auto* Script = Fragment.Get_Script().Get();
                if (IsValid(Script) && Script->GetClass() == F.SourceScriptClass)
                { ++SourceScripts; }
            });
        return SourceFeatures == 0 && SourceScripts == 0;
    }
    auto ExpectedAnchor(int32 I) -> FVector
    {
        return FVector(I == 7 ? 1300.0 : 500.0,
            I == 7 ? 0.0 : -2400.0 + 800.0 * I, 250.0);
    }
    auto Stations(FRun& F, APlayerController* PC,
        TArray<FCk_Handle>& Frozen, TArray<FVector>& FrozenAnchors) -> bool
    {
        if (!IsValid(PC) || !PC->IsLocalController()) { return false; }
        TArray<FCk_Handle> Now;
        TArray<FVector> Anchors;
        if (!PureHandles(PC, TEXT("Get_CapturedUnrealComponentStations"), Now) ||
            !PureAnchors(PC, Anchors) || Now.Num() != Roles || Anchors.Num() != Roles)
        { return false; }
        const FCk_Handle Root = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*PC->GetWorld());
        const FCk_Handle PCEntity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(PC);
        if (!ck::IsValid(Root) || !ck::IsValid(PCEntity)) { return false; }
        FCk_Handle CohortOwner;
        TSet<FCk_Handle> Seen;
        for (int32 I = 0; I < Roles; ++I)
        {
            const FName Tag(*FString::Printf(TEXT("%s_%s"), Tags[I], *PC->GetName()));
            const auto Matches = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, Tag);
            if (Matches.Num() != 1 || Matches[0] != Now[I] ||
                !ck::IsValid(Now[I]) || Seen.Contains(Now[I]) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Now[I]) ||
                !Anchors[I].Equals(ExpectedAnchor(I), 1.0))
            { return false; }
            const FCk_Handle Owner = UCk_Utils_EntityLifetime_UE::
                Get_LifetimeOwner(Now[I]);
            if (!ck::IsValid(Owner) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Owner) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Owner) != PCEntity ||
                (I > 0 && Owner != CohortOwner))
            { return false; }
            if (I == 0) { CohortOwner = Owner; }
            Seen.Add(Now[I]);
        }
        if (Frozen.IsEmpty()) { Frozen = Now; FrozenAnchors = Anchors; }
        return Frozen == Now && FrozenAnchors == Anchors;
    }
    auto RemoteStationsEmpty(APlayerController* PC) -> bool
    {
        if (!IsValid(PC) || PC->IsLocalController()) { return false; }
        TArray<FCk_Handle> Captured;
        TArray<FVector> Anchors;
        return PureHandles(PC, TEXT("Get_CapturedUnrealComponentStations"), Captured) &&
            PureAnchors(PC, Anchors) && Captured.IsEmpty() && Anchors.IsEmpty();
    }
    auto SameAuthorityWire(const FRun& F, const void* Authority,
        const void* Client) -> bool
    {
        if (!Active(F.SnapshotType, Authority) ||
            !Active(F.SnapshotType, Client)) { return false; }
        const auto* A = Poses(F.SnapshotType, Authority);
        const auto* B = Poses(F.SnapshotType, Client);
        if (!A || !B || A->Num() != Roles || B->Num() != Roles) { return false; }
        for (int32 I = 0; I < Roles; ++I)
        {
            if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
                    (*A)[I], (*B)[I])) { return false; }
        }
        const auto* PoseProperty = Field<FArrayProperty>(
            F.SnapshotType, TEXT("ComponentWorldPoses"));
        for (TFieldIterator<FProperty> It(F.SnapshotType); It; ++It)
        {
            if (*It != PoseProperty &&
                !It->Identical_InContainer(Authority, Client)) { return false; }
        }
        return true;
    }
    auto ClientObserved(const FRun& F, int32 Client) -> bool
    {
        const void* D = F.Data(Client+1);
        if (!Active(F.SnapshotType, D)) { return false; }
        const auto* Old = F.History.Find(I64(F.SnapshotType, D, TEXT("Sequence")));
        return Old && Old->IsValid() &&
            SameAuthorityWire(F, (*Old)->GetStructMemory(), D);
    }
    auto FindPresenter(const FRun& F, int32 Client) ->
        ACk_UnrealComponentGym_Presenter_UE*
    {
        auto* W = F.World[Client+1].Get();
        auto* PC = F.Local[Client].Get();
        if (!IsValid(W) || !IsValid(PC)) { return nullptr; }
        ACk_UnrealComponentGym_Presenter_UE* Result = nullptr;
        for (TActorIterator<ACk_UnrealComponentGym_Presenter_UE> It(W); It; ++It)
        {
            if (!IsValid(*It) || It->GetOwner() != PC || Result)
            { return nullptr; }
            Result = *It;
        }
        return Result;
    }
    auto ClientProjection(FRun& F, int32 Client) -> bool
    {
        auto* PC = F.Local[Client].Get();
        auto* Presenter = FindPresenter(F, Client);
        if (!IsValid(PC) || !IsValid(Presenter) ||
            Presenter->Get_IsFailed() || Presenter->Get_IsSuspended() ||
            Presenter->IsHidden() || !Presenter->TryCheckHealth() ||
            !ClientObserved(F, Client)) { return false; }
        const auto Installed = PureStruct(PC,
            TEXT("Get_InstalledUnrealComponentSample"), F.SnapshotType);
        const void* D = Installed.IsValid() ? Installed->GetStructMemory() : nullptr;
        const void* Wire = F.Data(Client+1);
        if (!Active(F.SnapshotType, D) ||
            !F.Snapshot->Identical(D, Wire, 0) ||
            Presenter->Get_InstalledSequence() !=
                I64(F.SnapshotType, D, TEXT("Sequence")) ||
            Presenter->Get_InstalledGeneration() !=
                I32(F.SnapshotType, D, TEXT("Generation")))
        { return false; }
        const auto* Expected = Poses(F.SnapshotType, D);
        TArray<USceneComponent*> Components;
        Presenter->GetComponents<USceneComponent>(Components);
        if (!Expected || Components.Num() != Roles+1) { return false; }
        for (int32 I = 0; I < Roles; ++I)
        {
            auto* Found = Cast<USceneComponent>(
                Presenter->GetDefaultSubobjectByName(FName(VisualNames[I])));
            if (!IsValid(Found) || Found->GetOwner() != Presenter ||
                Found->GetOuter() != Presenter ||
                Found->GetClass() != VisualClass(I) || !Found->IsRegistered() ||
                !Found->IsVisible() || Found->bHiddenInGame ||
                !Found->GetComponentTransform().Equals((*Expected)[I], 0.01))
            { return false; }
            if (auto* Primitive = Cast<UPrimitiveComponent>(Found))
            {
                if (Primitive->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
                { return false; }
            }
        }
        if (F.Presenter[Client].IsValid() &&
            F.Presenter[Client].Get() != Presenter)
        { return false; }
        F.Presenter[Client] = Presenter;
        return true;
    }
    auto TerminalClient(const FRun& F, int32 Client) -> bool
    {
        auto* W = F.World[Client+1].Get();
        auto* Presenter = F.Presenter[Client].Get();
        if (!IsValid(W) || !IsValid(Presenter) ||
            Presenter != FindPresenter(F, Client) ||
            !Presenter->Get_IsFailed() || !Presenter->IsHidden())
        { return false; }
        TArray<USceneComponent*> Components;
        Presenter->GetComponents<USceneComponent>(Components);
        if (Components.Num() != Roles+1) { return false; }
        for (int32 I = 0; I < Roles; ++I)
        {
            auto* Found = Cast<USceneComponent>(
                Presenter->GetDefaultSubobjectByName(FName(VisualNames[I])));
            if (!IsValid(Found) || Found->GetClass() != VisualClass(I) ||
                Found->IsVisible() || !Found->bHiddenInGame)
            { return false; }
            if (auto* P = Cast<UPrimitiveComponent>(Found))
            { if (P->GetCollisionEnabled() != ECollisionEnabled::NoCollision) { return false; } }
        }
        return true;
    }
    auto Observe(FRun& F, UWorld* W, ELevelTick TickType) -> void
    {
        if (W != F.World[0].Get() || TickType != LEVELTICK_All ||
            !F.SnapshotType) { return; }
        const void* D = F.Data(0);
        if (Failed(F.SnapshotType, D, F.FirstGeneration+1))
        {
            ++F.TerminalFrames;
            if (CountDrivers(W, F.DriverClass) != Roles) { F.TerminalReactivated = true; }
            return;
        }
        if (F.TerminalFrames > 0 && Active(F.SnapshotType, D))
        { F.TerminalReactivated = true; }
        if (!Active(F.SnapshotType, D)) { return; }
        const int64 Sequence = I64(F.SnapshotType, D, TEXT("Sequence"));
        if (const auto* Previous = F.History.Find(Sequence))
        {
            if (!F.Snapshot->Identical(D, (*Previous)->GetStructMemory(), 0))
            { F.TerminalReactivated = true; }
            return;
        }
        auto Copy = MakeShared<FStructOnScope>(F.SnapshotType);
        F.SnapshotType->CopyScriptStruct(Copy->GetStructMemory(), D);
        F.History.Add(Sequence, Copy);
        const int32 Generation = I32(F.SnapshotType, D, TEXT("Generation"));
        const auto* Current = Poses(F.SnapshotType, D);
        if (F.FirstGeneration > 0 && Current && F.FirstSequence > 0)
        {
            const auto* First = F.History.Find(F.FirstSequence);
            const auto* FirstPoses = First && First->IsValid()
                ? Poses(F.SnapshotType, (*First)->GetStructMemory()) : nullptr;
            if (Generation == F.FirstGeneration && FirstPoses &&
                !(*Current)[0].GetLocation().Equals(
                    (*FirstPoses)[0].GetLocation(), 2.0))
            { F.SawSourceMotion = true; }
            if (Generation == F.FirstGeneration+1 && !F.SawResetBoundary)
            {
                bool Boundary = true;
                for (int32 I = 0; I < Roles; ++I)
                {
                    const FVector Expected = ExpectedAnchor(I) +
                        FVector(120.0, 0.0, 180.0);
                    Boundary &= (*Current)[I].GetLocation().Equals(Expected, 0.01);
                }
                F.SawResetBoundary = Boundary;
            }
        }
    }
    class FRestore final : public IAutomationLatentCommand
    {
        TSharedRef<FRun> F;
    public:
        explicit FRestore(TSharedRef<FRun> InF) : F(InF) {}
        bool Update() override { F->Restore(); return true; }
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkUnrealComponentGym_SharedEight_Network,
    "Ck.UnrealComponent.Gym.SharedEight.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
    EAutomationTestFlags::EngineFilter)

bool FCkUnrealComponentGym_SharedEight_Network::RunTest(const FString& Parameters)
{
    using namespace ck_unreal_component_shared_eight;
    auto F = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, F->Dedicated))
    { AddError(TEXT("unsupported UnrealComponent network topology")); return false; }
    F->BaseMode = FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    F->Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    F->StateClass = FSoftClassPath(StatePath).TryLoadClass<AGameStateBase>();
    F->PCClass = FSoftClassPath(ControllerPath).TryLoadClass<APlayerController>();
    F->DriverClass = FSoftClassPath(DriverPath).TryLoadClass<AActor>();
    F->SourceScriptClass = FSoftClassPath(SourceScriptPath).TryLoadClass<
        UCk_EntityScript_WithActor_UE>();
    F->ProbeClass = FSoftClassPath(ProbePath).TryLoadClass<UObject>();
    F->Snapshot = Field<FStructProperty>(F->StateClass, TEXT("Snapshot"));
    F->SnapshotType = F->Snapshot ? F->Snapshot->Struct.Get() : nullptr;
    const auto* WirePoses = Field<FArrayProperty>(
        F->SnapshotType, TEXT("ComponentWorldPoses"));
    if (!TestTrue(TEXT("authored classes and exact value-only eight-pose schema"),
        F->BaseMode && F->Mode && F->StateClass && F->PCClass &&
        F->DriverClass && F->SourceScriptClass && F->ProbeClass &&
        F->SnapshotType && WirePoses &&
        Field<FIntProperty>(F->SnapshotType, TEXT("Generation")) &&
        Field<FInt64Property>(F->SnapshotType, TEXT("Sequence")) &&
        Field<FInt64Property>(F->SnapshotType, TEXT("FrameSerial")) &&
        Field<FArrayProperty>(F->SnapshotType, TEXT("Bodies")) &&
        Field<FArrayProperty>(F->SnapshotType, TEXT("Instructions")) &&
        Field<FStrProperty>(F->SnapshotType, TEXT("FailureReason"))))
    { return false; }
    const auto* Settings = GetDefault<UCkGym_StartupSettings>();
    F->Startup = Settings->StartupMode;
    F->Last = Settings->LastGymName;
    F->Recents = Settings->RecentGymNames;
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode =
        ECkGym_StartupMode::Cycler;
    F->Observer = FWorldDelegates::OnWorldPostActorTick.AddLambda(
        [F](UWorld* W, ELevelTick Tick, float) { Observe(*F, W, Tick); });
    AddCommand(new FCk_Latent_StartPIEMultiClient(Worlds, Entry, Parameters, F->BaseMode));
    AddCommand(new FCk_Latent_WaitForPIEReady(Worlds, 60));
    AddCommand(new FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* W = ck::auto_test::net::Get_ServerWorld();
            auto* R = Registry(W);
            auto* C = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(C) ? C->GetFirstPlayerController() : nullptr;
            return IsValid(R) &&
                R->Find_GymIndexByName(TEXT("Unreal Component")) != INDEX_NONE &&
                IsValid(PC) && PC->IsLocalController();
        }), 30, TEXT("actual Unreal Component catalog and owning travel requester")));
    AddCommand(new FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W)
        {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owning client requests registered Unreal Component travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::
                    TryRequest_TravelToGymFromPlayer(PC, TEXT("Unreal Component")));
        })));
    AddCommand(new FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
        {
            if (!Context(*F))
            { F->FirstReadinessPhase = TEXT("three-world controller/state context"); return false; }
            if (!Active(F->SnapshotType, F->Data(0)))
            { F->FirstReadinessPhase = TEXT("authority Active wire"); return false; }
            if (CountDrivers(F->World[0].Get(), F->DriverClass) != Roles)
            { F->FirstReadinessPhase = TEXT("eight authority driver actors"); return false; }
            if (!ReadSource(*F, F->FirstGeneration == 0))
            { F->FirstReadinessPhase = TEXT("authority physical source and timer composition"); return false; }
            auto* R = Registry(F->World[0].Get());
            if (!IsValid(R) ||
                R->Get_CurrentGymIndex() !=
                    R->Find_GymIndexByName(TEXT("Unreal Component")))
            { F->FirstReadinessPhase = TEXT("registered catalog travel identity"); return false; }
            if (F->FirstGeneration == 0)
            {
                F->FirstGeneration = I32(F->SnapshotType, F->Data(0), TEXT("Generation"));
                F->FirstSequence = I64(F->SnapshotType, F->Data(0), TEXT("Sequence"));
                auto Copy = MakeShared<FStructOnScope>(F->SnapshotType);
                F->SnapshotType->CopyScriptStruct(Copy->GetStructMemory(), F->Data(0));
                F->History.Add(F->FirstSequence, Copy);
            }
            if (F->FirstGeneration != 1 || F->FirstSequence <= 0 ||
                !F->History.Contains(F->FirstSequence) ||
                !ExactSourceBranch(*F))
            { F->FirstReadinessPhase = TEXT("source ECS branch census"); return false; }
            for (int32 I = 0; I < Clients; ++I)
            {
                if (!NoClientSource(*F, I))
                { F->FirstReadinessPhase = FString::Printf(TEXT("client %d source census"), I); return false; }
                if (!RemoteStationsEmpty(F->Remote[I].Get()))
                { F->FirstReadinessPhase = FString::Printf(TEXT("remote PC %d empty stations"), I); return false; }
                if (!Stations(*F, F->Local[I].Get(), F->Stations[I], F->Anchors[I]))
                { F->FirstReadinessPhase = FString::Printf(TEXT("local PC %d station anchors"), I); return false; }
                if (!ClientProjection(*F, I))
                { F->FirstReadinessPhase = FString::Printf(TEXT("client %d wire/presenter projection"), I); return false; }
            }
            if (F->Dedicated)
            { F->FirstReadinessPhase = TEXT("ready"); return true; }
            if (!Stations(*F, F->Host.Get(), F->HostStations, F->HostAnchors))
            { F->FirstReadinessPhase = TEXT("listen-host station anchors"); return false; }
            int32 ServerPresenters = 0;
            for (TActorIterator<ACk_UnrealComponentGym_Presenter_UE>
                    It(F->World[0].Get()); It; ++It) { ++ServerPresenters; }
            F->FirstReadinessPhase = ServerPresenters == 0 ?
                TEXT("ready") : TEXT("listen-host unexpected presenter");
            return ServerPresenters == 0; // Listen host reuses authority visuals.
        }), 60, TEXT("one eight-role physical source, two owner-local shells and projections")));
    AddCommand(new FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld*)
        {
            if (F->FirstReadinessPhase != TEXT("ready"))
            { AddError(FString::Printf(TEXT("first readiness blocked at: %s"),
                *F->FirstReadinessPhase)); }
        })));
    AddCommand(new FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
        {
            if (!Context(*F) || !ReadSource(*F, false) ||
                !ExactSourceBranch(*F) ||
                !F->SawSourceMotion) { return false; }
            for (int32 I = 0; I < Clients; ++I)
            {
                if (!NoClientSource(*F, I) ||
                    !ClientProjection(*F, I) ||
                    !Stations(*F, F->Local[I].Get(), F->Stations[I], F->Anchors[I]))
                { return false; }
                const void* D = F->Data(I+1);
                const auto* Old = F->History.Find(F->FirstSequence);
                const auto* First = Old && Old->IsValid()
                    ? Poses(F->SnapshotType, (*Old)->GetStructMemory()) : nullptr;
                const auto* Current = Poses(F->SnapshotType, D);
                F->SawClientMotion[I] |= First && Current &&
                    !(*Current)[0].GetLocation().Equals(
                        (*First)[0].GetLocation(), 2.0);
            }
            return F->SawClientMotion[0] && F->SawClientMotion[1];
        }), 40, TEXT("native physical styles and published motion reach both client displays")));
    AddCommand(new FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([F]() -> bool
        {
            return Rpc(F->Local[0].Get(), 1401, F->FirstGeneration);
        }), TEXT("owner A sends one typed R reset request")));
    AddCommand(new FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
        {
            int32 Serial, Generation, Result;
            if (!Ack(F->Local[0].Get(), Serial, Generation, Result) ||
                Serial != 1401 || Generation != F->FirstGeneration ||
                Result != 1 || !Context(*F) ||
                !Active(F->SnapshotType, F->Data(0), F->FirstGeneration+1) ||
                !ReadSource(*F, false) || !ExactSourceBranch(*F) ||
                !F->SawResetBoundary)
            { return false; }
            for (int32 I = 0; I < Clients; ++I)
            {
                if (!Active(F->SnapshotType, F->Data(I+1), F->FirstGeneration+1) ||
                    !ClientProjection(*F, I) ||
                    !Stations(*F, F->Local[I].Get(), F->Stations[I], F->Anchors[I]))
                { return false; }
            }
            return F->Dedicated || Stations(*F, F->Host.Get(),
                F->HostStations, F->HostAnchors);
        }), 60, TEXT("R admits generation two on same eight actor/entity/component/timer IDs")));
    AddCommand(new FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([F](UWorld* W)
        {
            auto* Component = F->Source[0].Component.Get();
            if (!IsValid(W) || W != F->World[0].Get() ||
                !IsValid(Component) || !Component->IsRegistered() ||
                !ReadSource(*F, false))
            {
                ck::auto_test::net::FailSession(
                    TEXT("exact source component unavailable for dependency-loss injection"));
                return;
            }
            F->MutatedComponent = Component;
            Component->UnregisterComponent();
        })));
    AddCommand(new FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
        {
            if (!Context(*F) || F->TerminalReactivated ||
                !Failed(F->SnapshotType, F->Data(0), F->FirstGeneration+1) ||
                !IsValid(F->MutatedComponent.Get()) ||
                F->MutatedComponent->IsRegistered() ||
                CountDrivers(F->World[0].Get(), F->DriverClass) != Roles ||
                F->TerminalFrames < 20 || !RetainedSourceIds(*F))
            { return false; }
            FCkUnrealComponentGym_SourceSample Sample;
            if (UCk_UnrealComponentGym_Runtime_UE::TryGetSourceSample(
                F->Source[0].Driver.Get(), F->Source[0].Host.Get(),
                F->MutatedComponent.Get(), F->Source[0].Entity,
                F->Source[0].Feature, 0, Sample)) { return false; }
            if (!PureFalseBool(F->State(0), TEXT("Get_CanAnimateSource")))
            { return false; }
            for (int32 I = 0; I < Clients; ++I)
            {
                if (!Failed(F->SnapshotType, F->Data(I+1), F->FirstGeneration+1) ||
                    !TerminalClient(*F, I) || !NoClientSource(*F, I) ||
                    !AdmissionClosed(F->World[I+1].Get(), F->ProbeClass))
                { return false; }
            }
            return AdmissionClosed(F->World[0].Get(), F->ProbeClass);
        }), 40, TEXT("real component loss closes source and both fixed presentations for 20 frames")));
    AddCommand(new FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([F]() -> bool
        { return Rpc(F->Local[1].Get(), 1402, F->FirstGeneration+1); }),
        TEXT("owner B sends later R against terminal source")));
    AddCommand(new FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
        {
            int32 Serial, Generation, Result;
            return Ack(F->Local[1].Get(), Serial, Generation, Result) &&
                Serial == 1402 && Generation == F->FirstGeneration+1 &&
                Result == 3 && !F->TerminalReactivated &&
                CountDrivers(F->World[0].Get(), F->DriverClass) == Roles &&
                TerminalClient(*F, 0) && TerminalClient(*F, 1);
        }), 20, TEXT("later owner request rejected; same source actors remain terminal")));
    AddCommand(new FCk_Latent_EndPIE());
    AddCommand(new FRestore(F));
    return true;
}

#endif
