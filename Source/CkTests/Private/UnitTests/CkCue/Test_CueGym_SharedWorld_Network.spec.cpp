// This fixture deliberately observes authored scripts and native PMG meshes.
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_StartupSettings.h"
#include "CkGymRegistry_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkCueGym_SourceVisuals.h"
#include "CkCueGym_Presenter.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkCue/CkCueRelay_Actor.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkDynamic/CkDynamic_Utils.h"
#include "CkPmg/CkPmg_Fragment.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "IPAddress.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_cue_shared_world_test
{
    constexpr int32 Worlds = 3, Clients = 2, Roles = 6;
    constexpr auto Entry = TEXT("/Engine/Maps/Entry");
    constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_CueGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_CueGym_GameState");
    constexpr auto PCPath = TEXT("/Script/Angelscript.Ck_CueGym_PlayerController");
    constexpr auto TransientPath = TEXT("/Script/Angelscript.Ck_CueGym_Cue_Transient");
    constexpr auto PersistentPath = TEXT("/Script/Angelscript.Ck_CueGym_Cue_Persistent");
    constexpr const TCHAR* RolePaths[Roles] = {
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_Lifetime"),
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_Concurrency"),
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_OwnerValidation"),
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_Restart"),
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_Transient"),
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_OwnerDestruction")};

    template<class T> T* Field(const UStruct* Type, const TCHAR* Name)
    { return Type ? FindFProperty<T>(Type, Name) : nullptr; }
    int32 I32(const UStruct* T, const void* D, const TCHAR* N)
    { const auto* P=Field<FIntProperty>(T,N); return P&&D?P->GetPropertyValue_InContainer(D):INDEX_NONE; }
    int64 I64(const UStruct* T, const void* D, const TCHAR* N)
    { const auto* P=Field<FInt64Property>(T,N); return P&&D?P->GetPropertyValue_InContainer(D):-1; }
    int32 Lifecycle(const UStruct* T, const void* D)
    {
        if (!D) { return INDEX_NONE; }
        if (const auto* P=Field<FByteProperty>(T,TEXT("Lifecycle")))
        { return P->GetPropertyValue_InContainer(D); }
        const auto* P=Field<FEnumProperty>(T,TEXT("Lifecycle"));
        return P&&P->GetUnderlyingProperty() ? int32(P->GetUnderlyingProperty()->
            GetUnsignedIntPropertyValue(P->ContainerPtrToValuePtr<void>(D))) : INDEX_NONE;
    }
    const TArray<FCkCueGym_SourceVisualRow>* Rows(const UStruct* T,const void* D)
    {
        const auto* P=Field<FArrayProperty>(T,TEXT("VisualRows"));
        const auto* Inner=P?CastField<FStructProperty>(P->Inner):nullptr;
        return D&&Inner&&Inner->Struct==FCkCueGym_SourceVisualRow::StaticStruct()
            ? P->ContainerPtrToValuePtr<TArray<FCkCueGym_SourceVisualRow>>(D):nullptr;
    }
    const TArray<int32>* Epochs(const UStruct* T,const void* D)
    {
        const auto* P=Field<FArrayProperty>(T,TEXT("RoleEpochs"));
        return D&&P&&CastField<FIntProperty>(P->Inner)
            ? P->ContainerPtrToValuePtr<TArray<int32>>(D):nullptr;
    }
    struct FCall
    {
        UObject* Target=nullptr; UFunction* Function=nullptr; TUniquePtr<FStructOnScope> Args;
        FCall(UObject* O,const TCHAR* Name)
        {
            if (!IsValid(O)) { return; }
            Function=O->FindFunction(Name);
            if (!IsValid(Function)) { Function=nullptr; return; }
            Target=O; Args=MakeUnique<FStructOnScope>(Function);
        }
        bool Send() const
        { if (!IsValid(Target)||!IsValid(Function)||!Args||!Args->GetStructMemory()) return false;
          Target->ProcessEvent(Function,Args->GetStructMemory()); return true; }
    };
    int64 InstalledSequence(UObject* PC,UScriptStruct* SampleType)
    {
        FCall C(PC,TEXT("Get_InstalledCueSample"));
        const auto* P=Field<FStructProperty>(C.Function,TEXT("ReturnValue"));
        if (!P||P->Struct!=SampleType||C.Function->NumParms!=1||!C.Send()) return -1;
        return I64(SampleType,P->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory()),TEXT("Sequence"));
    }
    bool InstalledCleared(UObject* PC,UScriptStruct* SampleType)
    {
        FCall C(PC,TEXT("Get_InstalledCueSample"));
        const auto* P=Field<FStructProperty>(C.Function,TEXT("ReturnValue"));
        if (!P||P->Struct!=SampleType||C.Function->NumParms!=1||!C.Send()) return false;
        const void* D=P->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory());
        const auto* Visuals=Rows(SampleType,D);
        return D&&Visuals&&Visuals->IsEmpty()&&
            I32(SampleType,D,TEXT("Generation"))==0&&
            I64(SampleType,D,TEXT("Sequence"))==0&&
            I32(SampleType,D,TEXT("VisualRowCount"))==0;
    }
    FCk_Handle VisualHandle(UObject* Script)
    {
        FCall C(Script,TEXT("Get_VisualShape"));
        const auto* P=Field<FStructProperty>(C.Function,TEXT("ReturnValue"));
        return P&&P->Struct==FCk_Handle::StaticStruct()&&C.Function->NumParms==1&&C.Send()
            ? *P->ContainerPtrToValuePtr<FCk_Handle>(C.Args->GetStructMemory()) : FCk_Handle{};
    }
    bool Rpc(UObject* PC,int32 Serial,int32 Generation,int32 Role)
    {
        FCall C(PC,TEXT("Server_RequestCueReset"));
        auto* S=Field<FIntProperty>(C.Function,TEXT("Serial"));
        auto* G=Field<FIntProperty>(C.Function,TEXT("Generation"));
        auto* R=Field<FIntProperty>(C.Function,TEXT("Role"));
        if (!S||!G||!R||C.Function->NumParms!=3||!C.Args||
            !C.Function->HasAllFunctionFlags(static_cast<EFunctionFlags>(FUNC_Net|FUNC_NetServer)))
        { return false; }
        S->SetPropertyValue_InContainer(C.Args->GetStructMemory(),Serial);
        G->SetPropertyValue_InContainer(C.Args->GetStructMemory(),Generation);
        R->SetPropertyValue_InContainer(C.Args->GetStructMemory(),Role);
        return C.Send();
    }
    bool Ack(UObject* PC,int32 Serial,int32 Generation,int32 Result)
    {
        FCall C(PC,TEXT("Get_CueControlAck"));
        const auto* P=Field<FStructProperty>(C.Function,TEXT("ReturnValue"));
        if (!P||C.Function->NumParms!=1||!C.Send()) return false;
        const void* D=P->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory());
        return I32(P->Struct,D,TEXT("RequestSerial"))==Serial &&
            I32(P->Struct,D,TEXT("Generation"))==Generation &&
            I32(P->Struct,D,TEXT("Result"))==Result;
    }
    bool Handles(UObject* O,const TCHAR* Name,TArray<FCk_Handle>& Out)
    {
        FCall C(O,Name); const auto* P=Field<FArrayProperty>(C.Function,TEXT("ReturnValue"));
        const auto* Inner=P?CastField<FStructProperty>(P->Inner):nullptr;
        if (!Inner||Inner->Struct!=FCk_Handle::StaticStruct()||C.Function->NumParms!=1||!C.Send())
        { return false; }
        Out=*P->ContainerPtrToValuePtr<TArray<FCk_Handle>>(C.Args->GetStructMemory()); return true;
    }
    int64 I64Getter(UObject* O,const TCHAR* Name)
    {
        FCall C(O,Name);
        const auto* P=Field<FInt64Property>(C.Function,TEXT("ReturnValue"));
        return P&&C.Function->NumParms==1&&C.Send()
            ? P->GetPropertyValue_InContainer(C.Args->GetStructMemory()):-1;
    }
    FCk_Handle HandleProperty(UObject* O,const TCHAR* Name)
    {
        const auto* P=IsValid(O)?Field<FStructProperty>(O->GetClass(),Name):nullptr;
        return P&&P->Struct==FCk_Handle::StaticStruct()
            ? *P->ContainerPtrToValuePtr<FCk_Handle>(O):FCk_Handle{};
    }
    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
    bool Direct(const FCk_Handle& Child,const FCk_Handle& Parent)
    { return Allocated(Child)&&Allocated(Parent)&&
        UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child)&&
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child)==Parent; }
    FCk_Handle WorldRoot(UWorld* W)
    { return IsValid(W)?UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W):FCk_Handle{}; }
    UProceduralMeshComponent* ShapeMesh(const FCk_Handle& Shape,UWorld* W)
    {
        if (!Allocated(Shape)||!Shape.Has<ck::FFragment_Pmg_DebugShape>()||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Shape,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy)) return nullptr;
        auto* M=Shape.Get<ck::FFragment_Pmg_DebugShape>().Get_MeshComponent().Get();
        return IsValid(M)&&M->IsRegistered()&&M->GetWorld()==W&&
            M->GetProcMeshSection(0) ? M:nullptr;
    }
    bool FindUnder(const FCk_Handle& Root,const FCk_Handle& World,int64 Key,FCk_Handle& Out)
    {
        Out={}; if (!Allocated(Root)||!Allocated(World)||Key<=0) return false;
        TArray<FCk_Handle> Search{Root};
        for (int32 I=0;I<Search.Num();++I)
        {
            const auto Current=Search[I];
            if (int64(uint32(Current.Get_Entity().Get_ID()))+1==Key)
            { Out=Current; return true; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                    Current,World,Children)) return false;
            for (const auto& Child:Children)
            { if (Search.Contains(Child)) return false; Search.Add(Child); }
        }
        return false;
    }
    struct FSection
    { TArray<FProcMeshVertex> Vertices; TArray<uint32> Indices; };
    bool CopySection(UProceduralMeshComponent* M,int32 Index,FSection& Out)
    {
        const auto* S=IsValid(M)?M->GetProcMeshSection(Index):nullptr;
        if (!S) return false;
        Out.Vertices=S->ProcVertexBuffer; Out.Indices=S->ProcIndexBuffer;
        return !Out.Vertices.IsEmpty()&&!Out.Indices.IsEmpty();
    }
    bool SameSection(const FSection& A,const FProcMeshSection* B)
    {
        if (!B||A.Vertices.Num()!=B->ProcVertexBuffer.Num()||
            A.Indices.Num()!=B->ProcIndexBuffer.Num()) return false;
        for (int32 I=0;I<A.Vertices.Num();++I)
        {
            const auto& X=A.Vertices[I]; const auto& Y=B->ProcVertexBuffer[I];
            if (!X.Position.Equals(Y.Position,KINDA_SMALL_NUMBER)||
                !X.Normal.Equals(Y.Normal,KINDA_SMALL_NUMBER)||
                !X.UV0.Equals(Y.UV0,KINDA_SMALL_NUMBER)||X.Color!=Y.Color||
                !X.Tangent.TangentX.Equals(Y.Tangent.TangentX,KINDA_SMALL_NUMBER)||
                X.Tangent.bFlipTangentY!=Y.Tangent.bFlipTangentY) return false;
        }
        for (int32 I=0;I<A.Indices.Num();++I)
        { if (A.Indices[I]!=B->ProcIndexBuffer[I]) return false; }
        return true;
    }
    bool SameRow(const FCkCueGym_SourceVisualRow& A,const FCkCueGym_SourceVisualRow& B)
    {
        if (A.Key!=B.Key||A.Role!=B.Role||A.Kind!=B.Kind||
            A.OutlineRecipeVersion!=B.OutlineRecipeVersion||
            !A.Pose.Equals(B.Pose,0.0)||A.Color!=B.Color||A.Radius!=B.Radius||
            A.Extent!=B.Extent||A.Segments!=B.Segments||A.Rings!=B.Rings||
            A.Axis!=B.Axis||A.DrawLines!=B.DrawLines||A.RenderMode!=B.RenderMode||
            A.VisibleByRenderMode!=B.VisibleByRenderMode||
            A.EnableCollision!=B.EnableCollision||
            A.LineThickness!=B.LineThickness||A.Duration!=B.Duration||
            A.Lines.Num()!=B.Lines.Num()) return false;
        for (int32 I=0;I<A.Lines.Num();++I)
        {
            const auto& X=A.Lines[I]; const auto& Y=B.Lines[I];
            if (X.Start!=Y.Start||X.End!=Y.End||X.Color!=Y.Color||
                X.Thickness!=Y.Thickness) return false;
        }
        return true;
    }
    bool MaterialColor(UProceduralMeshComponent* M,int32 Slot,FLinearColor& Out)
    {
        auto* MID=IsValid(M)?Cast<UMaterialInstanceDynamic>(M->GetMaterial(Slot)):nullptr;
        return IsValid(MID)&&IsValid(MID->Parent.Get())&&MID->GetOuter()==M&&
            MID->GetVectorParameterValue(FHashedMaterialParameterInfo{FName(TEXT("Color"))},Out);
    }
    struct FVisual
    {
        FCkCueGym_SourceVisualRow Row; FSection Fill,Lines; bool HasLines=false;
        FLinearColor FillColor,LineColor;
        bool MeshVisible=false,HiddenInGame=true,FillVisible=false,LineVisible=false;
        ECollisionEnabled::Type Collision=ECollisionEnabled::NoCollision;
    };
    struct FFrame
    { int32 Generation=0,VisualRowCount=0,TransientEpochStartGeneration=0;
      int64 Sequence=0,FrameSerial=0,TransientRequestedSerial=0;
      double CapturedAt=0.0;
      TArray<int32> RoleEpochs; TArray<FString> Bodies,Instructions;
      TArray<FVisual> Visuals; };
    struct FRun
    {
        bool Dedicated=false;
        bool ForcePortFallback=false;
        uint16 RequestedPort=0;
        int32 InitialBoundPort=0;
        ISocketSubsystem* ReservationSubsystem=nullptr;
        FSocket* Reservation=nullptr;
        UClass* BaseMode=nullptr; UClass* Mode=nullptr; UClass* StateClass=nullptr;
        UClass* PCClass=nullptr; UClass* RoleClass[Roles]{};
        UClass* TransientClass=nullptr; UClass* PersistentClass=nullptr;
        FStructProperty* Snapshot=nullptr; UScriptStruct* SampleType=nullptr;
        UScriptStruct* DisplayType=nullptr;
        TWeakObjectPtr<UWorld> World[Worlds];
        TWeakObjectPtr<APlayerController> Local[Clients],Remote[Clients],Host;
        TWeakObjectPtr<AGameStateBase> State[Worlds];
        TWeakObjectPtr<ACk_CueGym_Presenter_UE> Presenter[Clients];
        FCk_Handle SourceOwner; FCk_Handle SourceRoles[Roles];
        TArray<FCk_Handle> Stations[Clients];
        TArray<FCk_Handle> HostStations;
        TMap<int64,TSharedPtr<FFrame>> History;
        int32 InitialGeneration=0; int64 FirstSequence=0;
        int32 FirstEpochs[Roles]{};
        int32 SelectedRole=1; bool SelectedCaptured=false, AllCaptured=false;
        FCk_Handle SelectedOld,UnchangedOld;
        int64 SeenTransientSerial[Worlds][2]{};
        int64 BeforeSelectedTransientSerial[Worlds]{};
        bool BadTransient=false; bool SawTwoParityFrames[Clients]{};
        int64 LastParityFrame[Clients]{-1,-1}; bool TerminalReactivated=false; int32 TerminalTicks=0;
        FCk_Handle DestroyedRole,DestroyedCue,DestroyedShape;
        bool DestroyedRoleWasLive=false,VisualLossInjected=false;
        bool WireInjectionArmed=false,WireInjected=false,WirePresentationViolated=false;
        int32 WireOriginalCount=0,WireFailureTicks=0,WireBaselineMaterializedCount=0;
        int64 WireCorruptedSequence=0,WireBaselineInstalledSequence=0;
        TWeakObjectPtr<ACk_CueGym_Presenter_UE> WireBaselinePresenter;
        FDelegateHandle WireObserver;
        FString Readiness;
        FString ContextFailure;
        double LastReadinessLogAt=-1.0;
        ECkGym_StartupMode Startup{}; FString Last; TArray<FString> Recents;
        FDelegateHandle Observer;
        ~FRun() { ReleaseReservation(); }
        bool ReserveConfiguredPort()
        {
            const auto* Settings=GetDefault<ULevelEditorPlaySettings>();
            if (!Settings) return false;
            Settings->GetServerPort(RequestedPort);
            if (RequestedPort<1||RequestedPort>=65535) return false;
            ReservationSubsystem=ISocketSubsystem::Get();
            if (!ReservationSubsystem) return false;
            const auto Addresses=ReservationSubsystem->GetLocalBindAddresses();
            if (Addresses.IsEmpty()) return false;
            const auto BindAddress=Addresses[0];
            Reservation=ReservationSubsystem->CreateSocket(NAME_DGram,
                TEXT("CueGymPortFallbackReservation"),BindAddress->GetProtocolType());
            if (!Reservation) return false;
            BindAddress->SetPort(RequestedPort);
            if (!Reservation->SetReuseAddr(false)||!Reservation->Bind(*BindAddress))
            { ReleaseReservation(); return false; }
            return true;
        }
        void ReleaseReservation()
        {
            if (Reservation&&ReservationSubsystem)
            { ReservationSubsystem->DestroySocket(Reservation); }
            Reservation=nullptr;
            ReservationSubsystem=nullptr;
        }
        const void* Data(int32 I) const
        { return Snapshot&&State[I].IsValid()?Snapshot->ContainerPtrToValuePtr<void>(State[I].Get()):nullptr; }
        void NoteReadiness(const TCHAR* Stage)
        {
            const double Now=FPlatformTime::Seconds();
            if (Readiness==Stage&&LastReadinessLogAt>=0.0&&Now-LastReadinessLogAt<5.0) return;
            Readiness=Stage; LastReadinessLogAt=Now;
            const void* D=Data(0);
            const auto* Reason=Field<FStrProperty>(SampleType,TEXT("FailureReason"));
            const FString Failure=Reason&&D?Reason->GetPropertyValue_InContainer(D):TEXT("<unavailable>");
            UE_LOG(LogTemp,Warning,
                TEXT("Cue shared readiness: %s; server lifecycle=%d generation=%d sequence=%lld frame=%lld history=%d failure=%s; relay seen=[%lld,%lld,%lld] received=[%lld,%lld,%lld] published=[%lld,%lld,%lld]"),
                Stage,Lifecycle(SampleType,D),I32(SampleType,D,TEXT("Generation")),
                I64(SampleType,D,TEXT("Sequence")),I64(SampleType,D,TEXT("FrameSerial")),
                History.Num(),*Failure,
                SeenTransientSerial[0][0],SeenTransientSerial[1][0],SeenTransientSerial[2][0],
                I64Getter(State[0].Get(),TEXT("Get_ReceivedTransientSerial")),
                I64Getter(State[1].Get(),TEXT("Get_ReceivedTransientSerial")),
                I64Getter(State[2].Get(),TEXT("Get_ReceivedTransientSerial")),
                I64(SampleType,Data(0),TEXT("TransientRequestedSerial")),
                I64(SampleType,Data(1),TEXT("TransientRequestedSerial")),
                I64(SampleType,Data(2),TEXT("TransientRequestedSerial")));
            const auto* VisualRows=Rows(SampleType,D);
            int64 LineCount=0;
            if (VisualRows) for (const auto& Row:*VisualRows) LineCount+=Row.Lines.Num();
            UE_LOG(LogTemp,Warning,TEXT("Cue source payload: rows=%d sampledLines=%lld"),
                VisualRows?VisualRows->Num():-1,LineCount);
            auto* Server=ck::auto_test::net::Get_ServerWorld();
            for (int32 I=0;I<Clients;++I)
            {
                auto* Client=ck::auto_test::net::Get_ClientWorld(I);
                auto* PC=IsValid(Client)?Client->GetFirstPlayerController():nullptr;
                auto* RemoteController=IsValid(Server)?ck::auto_test::net::TryGet_RemoteClientPlayerController(Server,I):nullptr;
                auto* ClientState=IsValid(Client)?Client->GetGameState():nullptr;
                auto* Connection=IsValid(PC)?PC->GetNetConnection():nullptr;
                APawn* LocalPawn=IsValid(PC)?PC->GetPawn():nullptr;
                APawn* RemotePawn=IsValid(RemoteController)?RemoteController->GetPawn():nullptr;
                const void* ClientData=Snapshot&&IsValid(ClientState)&&
                    ClientState->GetClass()==StateClass?
                    Snapshot->ContainerPtrToValuePtr<void>(ClientState):nullptr;
                const auto* Captured=History.Find(I64(SampleType,ClientData,TEXT("Sequence")));
                const double SourceAge=Captured&&Captured->IsValid()?
                    Now-(*Captured)->CapturedAt:-1.0;
                UE_LOG(LogTemp,Warning,
                    TEXT("Cue context client %d: failure=%s world=%p mode=%d localPC=%p class=%s local=%d pawn=%p remotePC=%p class=%s authority=%d pawn=%p state=%p class=%s wireSequence=%lld wireFrame=%lld sourceAgeSeconds=%.3f connection=%p netSpeed=%d"),
                    I,*ContextFailure,Client,IsValid(Client)?int32(Client->GetNetMode()):-1,
                    PC,IsValid(PC)?*PC->GetClass()->GetName():TEXT("<invalid>"),
                    IsValid(PC)&&PC->IsLocalController(),LocalPawn,
                    RemoteController,IsValid(RemoteController)?*RemoteController->GetClass()->GetName():TEXT("<invalid>"),
                    IsValid(RemoteController)&&RemoteController->HasAuthority(),RemotePawn,
                    ClientState,IsValid(ClientState)?*ClientState->GetClass()->GetName():TEXT("<invalid>"),
                    I64(SampleType,ClientData,TEXT("Sequence")),
                    I64(SampleType,ClientData,TEXT("FrameSerial")),
                    SourceAge,Connection,IsValid(Connection)?Connection->CurrentNetSpeed:-1);
                FCall AckCall(Local[I].Get(),TEXT("Get_CueControlAck"));
                const auto* AckReturn=Field<FStructProperty>(AckCall.Function,TEXT("ReturnValue"));
                const bool AckReady=AckReturn&&AckCall.Function->NumParms==1&&AckCall.Send();
                const void* AckData=AckReady?AckReturn->ContainerPtrToValuePtr<void>(
                    AckCall.Args->GetStructMemory()):nullptr;
                UE_LOG(LogTemp,Warning,
                    TEXT("Cue reset ACK client %d: available=%d serial=%d generation=%d result=%d"),
                    I,AckReady,
                    I32(AckReady?AckReturn->Struct:nullptr,AckData,TEXT("RequestSerial")),
                    I32(AckReady?AckReturn->Struct:nullptr,AckData,TEXT("Generation")),
                    I32(AckReady?AckReturn->Struct:nullptr,AckData,TEXT("Result")));
            }
        }
        void Restore()
        {
            ReleaseReservation();
            if (WireObserver.IsValid()) { FWorldDelegates::OnWorldPreActorTick.Remove(WireObserver); WireObserver.Reset(); }
            if (Observer.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(Observer); Observer.Reset(); }
            auto* Settings=GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode=Startup; Settings->LastGymName=Last;
            Settings->RecentGymNames=Recents; Settings->SaveConfig();
        }
    };
    UCkGym_Registry_Subsystem* Registry(UWorld* W)
    { auto* GI=IsValid(W)?W->GetGameInstance():nullptr;
      return IsValid(GI)?GI->GetSubsystem<UCkGym_Registry_Subsystem>():nullptr; }
    int32 BoundPort(UWorld* W)
    {
        auto* Driver=IsValid(W)?W->GetNetDriver():nullptr;
        const auto Address=IsValid(Driver)?Driver->GetLocalAddr():nullptr;
        return Address.IsValid()?Address->GetPort():0;
    }
    bool PlayerReady(APlayerController* PC,UWorld* W)
    {
        if (!IsValid(PC)||PC->IsActorBeingDestroyed()||PC->GetWorld()!=W||
            !PC->HasActorBegunPlay()) return false;
        APawn* Pawn=PC->GetPawn(); auto* PS=PC->PlayerState.Get();
        return IsValid(Pawn)&&!Pawn->IsActorBeingDestroyed()&&Pawn->GetWorld()==W&&
            Pawn->HasActorBegunPlay()&&Pawn->GetController()==PC&&
            IsValid(PS)&&!PS->IsActorBeingDestroyed()&&PS->GetWorld()==W&&
            PS->HasActorBegunPlay()&&Pawn->GetPlayerState()==PS;
    }
    bool BasePeersReady(const FRun& F)
    {
        auto* Server=ck::auto_test::net::Get_ServerWorld();
        if (!IsValid(Server)||Server->GetNetMode()!=(F.Dedicated?NM_DedicatedServer:NM_ListenServer)||
            !IsValid(Server->GetAuthGameMode())||Server->GetAuthGameMode()->GetClass()!=F.BaseMode||
            ck::auto_test::net::Get_NumClientWorlds()!=Clients||BoundPort(Server)<1) return false;
        TArray<APlayerController*> Remote;
        for (int32 I=0;I<Clients;++I)
        {
            auto* Client=ck::auto_test::net::Get_ClientWorld(I);
            auto* Local=IsValid(Client)?Client->GetFirstPlayerController():nullptr;
            auto* ServerPC=ck::auto_test::net::TryGet_RemoteClientPlayerController(Server,I);
            if (!IsValid(Client)||Client==Server||Client->GetNetMode()!=NM_Client||
                !PlayerReady(Local,Client)||!Local->IsLocalController()||
                !PlayerReady(ServerPC,Server)||ServerPC->IsLocalController()||
                !IsValid(ServerPC->GetNetConnection())||Remote.Contains(ServerPC)) return false;
            Remote.Add(ServerPC);
        }
        return true;
    }
    bool TravelEndpointsMatch(const FRun& F)
    {
        auto* Server=ck::auto_test::net::Get_ServerWorld();
        if (F.InitialBoundPort<1||BoundPort(Server)!=F.InitialBoundPort) return false;
        for (int32 I=0;I<Clients;++I)
        {
            auto* W=ck::auto_test::net::Get_ClientWorld(I);
            auto* Driver=IsValid(W)?W->GetNetDriver():nullptr;
            auto* Connection=IsValid(Driver)?Driver->ServerConnection.Get():nullptr;
            const auto Remote=IsValid(Connection)?Connection->GetRemoteAddr():nullptr;
            if (!Remote.IsValid()||Remote->GetPort()!=F.InitialBoundPort) return false;
        }
        return true;
    }
    bool Context(FRun& F)
    {
        auto Fail=[&F](const TCHAR* Reason) { F.ContextFailure=Reason; return false; };
        auto* S=ck::auto_test::net::Get_ServerWorld();
        if (!IsValid(S)||S->GetNetMode()!=(F.Dedicated?NM_DedicatedServer:NM_ListenServer)||
            !IsValid(S->GetAuthGameMode())||S->GetAuthGameMode()->GetClass()!=F.Mode||
            ck::auto_test::net::Get_NumClientWorlds()!=Clients)
            return Fail(TEXT("server world, mode, game mode, or client count"));
        F.World[0]=S; F.State[0]=S->GetGameState();
        if (!F.State[0].IsValid()||F.State[0]->GetClass()!=F.StateClass)
            return Fail(TEXT("server game state"));
        for (int32 I=0;I<Clients;++I)
        {
            auto* W=ck::auto_test::net::Get_ClientWorld(I);
            auto* PC=IsValid(W)?W->GetFirstPlayerController():nullptr;
            auto* Remote=ck::auto_test::net::TryGet_RemoteClientPlayerController(S,I);
            if (!IsValid(W)||W==S||W->GetNetMode()!=NM_Client)
                return Fail(I==0?TEXT("client 0 world or net mode"):TEXT("client 1 world or net mode"));
            if (!IsValid(PC)||PC->GetClass()!=F.PCClass||!PC->IsLocalController())
                return Fail(I==0?TEXT("client 0 local controller"):TEXT("client 1 local controller"));
            if (!IsValid(Remote)||Remote->GetClass()!=F.PCClass||!Remote->HasAuthority())
                return Fail(I==0?TEXT("client 0 remote controller correlation"):TEXT("client 1 remote controller correlation"));
            if (!IsValid(PC->GetPawn())||!IsValid(Remote->GetPawn()))
                return Fail(I==0?TEXT("client 0 local or remote pawn"):TEXT("client 1 local or remote pawn"));
            if (!IsValid(W->GetGameState())||W->GetGameState()->GetClass()!=F.StateClass)
                return Fail(I==0?TEXT("client 0 game state"):TEXT("client 1 game state"));
            F.World[I+1]=W; F.State[I+1]=W->GetGameState();
            F.Local[I]=PC; F.Remote[I]=Remote;
        }
        if (F.Local[0]==F.Local[1]||F.Remote[0]==F.Remote[1])
            return Fail(TEXT("distinct client or remote controller identity"));
        auto* GI=S->GetGameInstance();
        if (!IsValid(GI)||GI->GetLocalPlayers().Num()!=(F.Dedicated?0:1))
            return Fail(TEXT("server game instance local player count"));
        if (!F.Dedicated)
        {
            auto* LP=GI->GetLocalPlayers()[0]; auto* Host=IsValid(LP)?LP->GetPlayerController(S):nullptr;
            if (!IsValid(Host)||Host->GetClass()!=F.PCClass||!Host->IsLocalController()||
                !Host->HasAuthority()) return Fail(TEXT("listen host controller"));
            F.Host=Host;
        }
        F.ContextFailure.Reset();
        return true;
    }
    bool Source(FRun& F,bool Capture)
    {
        auto* S=F.State[0].Get(); auto* W=F.World[0].Get();
        if (!IsValid(S)||!IsValid(W)) return false;
        const auto Owner=HandleProperty(S,TEXT("SourceOwner"));
        const auto Root=WorldRoot(W);
        if (!Direct(Owner,Root)||(!Capture&&Owner!=F.SourceOwner)) return false;
        const auto* P=Field<FArrayProperty>(F.StateClass,TEXT("ServerRoles"));
        const auto* Inner=P?CastField<FStructProperty>(P->Inner):nullptr;
        if (!Inner||Inner->Struct!=FCk_Handle::StaticStruct()) return false;
        const auto* RolesArray=P->ContainerPtrToValuePtr<TArray<FCk_Handle>>(S);
        if (!RolesArray||RolesArray->Num()!=Roles) return false;
        for (int32 I=0;I<Roles;++I)
        {
            const auto Entity=(*RolesArray)[I];
            auto* Script=Allocated(Entity)?UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity):nullptr;
            if (!Direct(Entity,Owner)||!IsValid(Script)||Script->GetClass()!=F.RoleClass[I]||
                (!Capture&&I!=F.SelectedRole&&F.SourceRoles[I]!=Entity)) return false;
            if (Capture) F.SourceRoles[I]=Entity;
        }
        if (Capture) F.SourceOwner=Owner;
        return true;
    }
    bool CaptureFrame(FRun& F,FFrame& Out)
    {
        auto* W=F.World[0].Get(); const void* D=F.Data(0);
        if (!IsValid(W)||Lifecycle(F.SampleType,D)!=1||!Source(F,false)) return false;
        Out.Generation=I32(F.SampleType,D,TEXT("Generation"));
        Out.Sequence=I64(F.SampleType,D,TEXT("Sequence"));
        Out.FrameSerial=I64(F.SampleType,D,TEXT("FrameSerial"));
        Out.VisualRowCount=I32(F.SampleType,D,TEXT("VisualRowCount"));
        Out.TransientRequestedSerial=I64(F.SampleType,D,TEXT("TransientRequestedSerial"));
        Out.TransientEpochStartGeneration=I32(F.SampleType,D,TEXT("TransientEpochStartGeneration"));
        const auto* E=Epochs(F.SampleType,D); const auto* Wire=Rows(F.SampleType,D);
        auto* Bodies=Field<FArrayProperty>(F.SampleType,TEXT("Bodies"));
        auto* Instructions=Field<FArrayProperty>(F.SampleType,TEXT("Instructions"));
        const auto* SourceBodies=Bodies&&CastField<FStrProperty>(Bodies->Inner)
            ? Bodies->ContainerPtrToValuePtr<TArray<FString>>(D):nullptr;
        const auto* SourceInstructions=Instructions&&CastField<FStrProperty>(Instructions->Inner)
            ? Instructions->ContainerPtrToValuePtr<TArray<FString>>(D):nullptr;
        if (!E||E->Num()!=Roles||!Wire||!SourceBodies||!SourceInstructions||
            SourceBodies->Num()!=Roles||SourceInstructions->Num()!=Roles||
            Out.Sequence<=0||Out.FrameSerial<=0||Out.VisualRowCount!=Wire->Num()) return false;
        Out.RoleEpochs=*E; Out.Bodies=*SourceBodies; Out.Instructions=*SourceInstructions;
        TArray<FCk_Handle> Roots; for (const auto& H:F.SourceRoles) Roots.Add(H);
        TArray<FCkCueGym_SourceVisualRow> Native; FString Failure;
        if (UCkCueGym_SourceVisuals::TryGet_SourceVisuals(W,F.SourceOwner,Roots,Native,Failure)!=
                ECkCueGym_SourceVisualOutcome::Complete||Native.Num()!=Wire->Num()) return false;
        for (int32 I=0;I<Native.Num();++I)
        {
            const auto& R=Native[I];
            if (!SameRow(R,(*Wire)[I])||R.Role==4) return false;
            FCk_Handle Cue;
            if (!FindUnder(F.SourceRoles[R.Role],WorldRoot(W),R.Key,Cue)) return false;
            auto* Script=UCk_Utils_EntityScript_UE::TryGet_EntityScript(Cue);
            const auto Shape=VisualHandle(Script);
            auto* Mesh=ShapeMesh(Shape,W);
            if (!IsValid(Mesh)||!Direct(Shape,Cue)||
                !Mesh->GetComponentTransform().Equals(R.Pose,0.01)) return false;
            FVisual V; V.Row=R;
            if (!CopySection(Mesh,0,V.Fill)) return false;
            V.HasLines=Mesh->GetProcMeshSection(1)!=nullptr;
            if (V.HasLines&&!CopySection(Mesh,1,V.Lines)) return false;
            V.MeshVisible=Mesh->IsVisible(); V.HiddenInGame=Mesh->bHiddenInGame;
            V.FillVisible=Mesh->IsMeshSectionVisible(0);
            V.LineVisible=V.HasLines&&Mesh->IsMeshSectionVisible(1);
            V.Collision=Mesh->GetCollisionEnabled();
            if (!MaterialColor(Mesh,0,V.FillColor)||
                (V.HasLines&&!MaterialColor(Mesh,1,V.LineColor))) return false;
            Out.Visuals.Add(MoveTemp(V));
        }
        return true;
    }
    bool SameReceivedSourceFrame(FRun& F,const void* D,
        const TArray<FCkCueGym_SourceVisualRow>& Raw,const FFrame& Frame)
    {
        const auto* E=Epochs(F.SampleType,D);
        auto* Bodies=Field<FArrayProperty>(F.SampleType,TEXT("Bodies"));
        auto* Instructions=Field<FArrayProperty>(F.SampleType,TEXT("Instructions"));
        auto* Reason=Field<FStrProperty>(F.SampleType,TEXT("FailureReason"));
        const auto* SourceBodies=D&&Bodies&&CastField<FStrProperty>(Bodies->Inner)
            ? Bodies->ContainerPtrToValuePtr<TArray<FString>>(D):nullptr;
        const auto* SourceInstructions=D&&Instructions&&CastField<FStrProperty>(Instructions->Inner)
            ? Instructions->ContainerPtrToValuePtr<TArray<FString>>(D):nullptr;
        if (!D||!E||!SourceBodies||!SourceInstructions||!Reason||
            I32(F.SampleType,D,TEXT("CatalogVersion"))!=1||
            Lifecycle(F.SampleType,D)!=1||
            I32(F.SampleType,D,TEXT("Generation"))!=Frame.Generation||
            I64(F.SampleType,D,TEXT("Sequence"))!=Frame.Sequence||
            I64(F.SampleType,D,TEXT("FrameSerial"))!=Frame.FrameSerial||
            I32(F.SampleType,D,TEXT("ResetRole"))!=-2||
            I32(F.SampleType,D,TEXT("VisualRowCount"))!=Frame.VisualRowCount||
            Frame.VisualRowCount!=Raw.Num()||
            I64(F.SampleType,D,TEXT("TransientRequestedSerial"))!=Frame.TransientRequestedSerial||
            I32(F.SampleType,D,TEXT("TransientEpochStartGeneration"))!=Frame.TransientEpochStartGeneration||
            !Reason->GetPropertyValue_InContainer(D).IsEmpty()||
            *E!=Frame.RoleEpochs||*SourceBodies!=Frame.Bodies||
            *SourceInstructions!=Frame.Instructions) return false;
        TArray<FCkCueGym_SourceVisualRow> Expanded;
        for (const auto& Row:Raw)
        { if (Row.OutlineRecipeVersion!=1||!Row.Lines.IsEmpty()) return false; }
        if (!UCkCueGym_SourceVisuals::TryExpand_SourceVisualRows(Raw,Expanded)||
            Expanded.Num()!=Frame.Visuals.Num()) return false;
        for (int32 Index=0;Index<Expanded.Num();++Index)
        { if (!SameRow(Expanded[Index],Frame.Visuals[Index].Row)) return false; }
        return true;
    }
    ACk_CueGym_Presenter_UE* FindPresenter(UWorld* W,APlayerController* PC)
    {
        ACk_CueGym_Presenter_UE* Found=nullptr;
        for (TActorIterator<ACk_CueGym_Presenter_UE> It(W);It;++It)
        { if (It->GetOwner()!=PC||Found) return nullptr; Found=*It; }
        return Found;
    }
    bool Passive(FRun& F,int32 Client)
    {
        auto* W=F.World[Client+1].Get(); auto* PC=F.Local[Client].Get();
        if (!IsValid(W)||!IsValid(PC)) return false;
        auto* P=FindPresenter(W,PC);
        if (!IsValid(P)||P->Get_IsFailed()||P->Get_IsSuspended()||
            !P->TryCheckHealth()||
            (F.Presenter[Client].IsValid()&&F.Presenter[Client].Get()!=P)) return false;
        F.Presenter[Client]=P;
        const auto* D=F.Data(Client+1);
        const int64 Sequence=I64(F.SampleType,D,TEXT("Sequence"));
        const auto* Frame=F.History.Find(Sequence);
        if (!Frame||!Frame->IsValid()||Lifecycle(F.SampleType,D)!=1||
            P->Get_InstalledSequence()!=Sequence||
            P->Get_InstalledGeneration()!=(*Frame)->Generation||
            P->Get_MaterializedRowCount()!=(*Frame)->Visuals.Num()) return false;
        const auto* Wire=Rows(F.SampleType,D);
        TArray<FCkCueGym_SourceVisualRow> ExpandedWire;
        if (!Wire) return false;
        for (const auto& Raw:*Wire)
        { if (Raw.OutlineRecipeVersion!=1||!Raw.Lines.IsEmpty()) return false; }
        if (!UCkCueGym_SourceVisuals::TryExpand_SourceVisualRows(*Wire,ExpandedWire)||
            ExpandedWire.Num()!=(*Frame)->Visuals.Num()) return false;
        for (int32 I=0;I<(*Frame)->Visuals.Num();++I)
        {
            const auto& V=(*Frame)->Visuals[I];
            if (!SameRow(V.Row,ExpandedWire[I])) return false;
            FCkCueGym_PassiveReceipt R;
            if (!P->TryGet_Receipt(V.Row.Key,R)||R.Role!=V.Row.Role||
                R.Kind!=V.Row.Kind||!IsValid(R.Mesh.Get())||R.Mesh->GetWorld()!=W||
                !R.Mesh->IsRegistered()||!R.Mesh->GetComponentTransform().Equals(V.Row.Pose,0.01)||
                !SameSection(V.Fill,R.Mesh->GetProcMeshSection(0))||
                (V.HasLines!=(R.Mesh->GetProcMeshSection(1)!=nullptr))||
                (V.HasLines&&!SameSection(V.Lines,R.Mesh->GetProcMeshSection(1)))||
                R.Mesh->IsVisible()!=V.MeshVisible||R.Mesh->bHiddenInGame!=V.HiddenInGame||
                R.Mesh->IsMeshSectionVisible(0)!=V.FillVisible||
                (V.HasLines&&R.Mesh->IsMeshSectionVisible(1)!=V.LineVisible)||
                R.Mesh->GetCollisionEnabled()!=V.Collision||
                R.FillColor!=V.FillColor||
                (V.HasLines&&R.LineColor!=V.LineColor)) return false;
            FLinearColor FillColor,LineColor;
            if (!MaterialColor(R.Mesh,0,FillColor)||FillColor!=V.FillColor||
                (V.HasLines&&(!MaterialColor(R.Mesh,1,LineColor)||
                    LineColor!=V.LineColor))) return false;
        }
        return true;
    }
    void ObserveTransient(FRun& F,int32 WorldIndex)
    {
        auto* W=F.World[WorldIndex].Get(); auto* State=F.State[WorldIndex].Get();
        const auto Root=WorldRoot(W);
        if (!Allocated(Root)||!IsValid(F.TransientClass)||!IsValid(State)) return;
        TArray<FCk_Handle> Receipts;
        if (!Handles(State,TEXT("Get_TransientReceipts"),Receipts)) return;
        for (const auto& Cue:UCk_Utils_EntityTag_UE::ForEach_Entity(Root,
            FName(TEXT("TAG_CueGym_Cue_Transient"))))
        {
            auto* Script=Allocated(Cue)?UCk_Utils_EntityScript_UE::TryGet_EntityScript(Cue):nullptr;
            if (!IsValid(Script)||Script->GetClass()!=F.TransientClass) continue;
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                Cue,Root,Children)) continue;
            bool Physical=false;
            for (const auto& Shape:Children)
            { Physical|=Direct(Shape,Cue)&&IsValid(ShapeMesh(Shape,W)); }
            if (!Physical) continue;
            const int32 Generation=I32(Script->GetClass(),Script,TEXT("SourceGeneration"));
            const int32 Epoch=I32(Script->GetClass(),Script,TEXT("RoleEpoch"));
            const int64 Serial=I64(Script->GetClass(),Script,TEXT("EventSerial"));
            const auto Channel=UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Cue)
                ? UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Cue):FCk_Handle{};
            auto* Relay=Allocated(Channel)
                ? Cast<ACk_CueRelay_UE>(UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Channel))
                : nullptr;
            if ((Epoch!=1&&Epoch!=2)||Generation!=(Epoch==1?1:3)||Serial<=0||
                !Receipts.Contains(Cue)||!Direct(Cue,Channel)||!IsValid(Relay)||
                Relay->GetWorld()!=W||Relay->GetClass()!=ACk_CueRelay_UE::StaticClass()||
                UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Relay)!=Channel)
            { F.BadTransient=true; continue; }
            F.SeenTransientSerial[WorldIndex][Epoch-1]=FMath::Max(
                F.SeenTransientSerial[WorldIndex][Epoch-1],Serial);
        }
    }
    bool NoClientSources(FRun& F,int32 Client)
    {
        auto Root=WorldRoot(F.World[Client+1].Get());
        if (!Allocated(Root)) return false;
        constexpr const TCHAR* Tags[Roles]={
            TEXT("TAG_CueGym_Lifetime"),TEXT("TAG_CueGym_Concurrency"),
            TEXT("TAG_CueGym_OwnerValidation"),TEXT("TAG_CueGym_Restart"),
            TEXT("TAG_CueGym_Transient"),TEXT("TAG_CueGym_OwnerDestruction")};
        for (const auto* Tag:Tags)
        { if (!UCk_Utils_EntityTag_UE::ForEach_Entity(Root,FName(Tag)).IsEmpty()) return false; }
        return true;
    }
    bool ChangedRoles(FRun& F,int32 Generation,int32 ChangedRole)
    {
        auto* S=F.State[0].Get(); const void* D=F.Data(0);
        const auto* E=Epochs(F.SampleType,D);
        const auto* P=Field<FArrayProperty>(F.StateClass,TEXT("ServerRoles"));
        if (!IsValid(S)||Lifecycle(F.SampleType,D)!=1||
            I32(F.SampleType,D,TEXT("Generation"))!=Generation||!E||E->Num()!=Roles||!P)
        { return false; }
        const auto* Current=P->ContainerPtrToValuePtr<TArray<FCk_Handle>>(S);
        if (!Current||Current->Num()!=Roles) return false;
        for (int32 I=0;I<Roles;++I)
        {
            const bool Changed=ChangedRole==-1||ChangedRole==I;
            if ((*E)[I] != F.FirstEpochs[I]+(Changed?1:0)||
                (Changed ? ((*Current)[I]==F.SourceRoles[I]||Allocated(F.SourceRoles[I])) :
                    (*Current)[I]!=F.SourceRoles[I])||
                !Direct((*Current)[I],F.SourceOwner)) return false;
            auto* Script=UCk_Utils_EntityScript_UE::TryGet_EntityScript((*Current)[I]);
            if (!IsValid(Script)||Script->GetClass()!=F.RoleClass[I]) return false;
        }
        return true;
    }
    bool CaptureNewRoles(FRun& F,int32 Generation)
    {
        auto* S=F.State[0].Get(); const void* D=F.Data(0);
        const auto* E=Epochs(F.SampleType,D);
        const auto* P=Field<FArrayProperty>(F.StateClass,TEXT("ServerRoles"));
        if (!IsValid(S)||I32(F.SampleType,D,TEXT("Generation"))!=Generation||
            !E||E->Num()!=Roles||!P) return false;
        const auto* Current=P->ContainerPtrToValuePtr<TArray<FCk_Handle>>(S);
        if (!Current||Current->Num()!=Roles) return false;
        for (int32 I=0;I<Roles;++I)
        { F.SourceRoles[I]=(*Current)[I]; F.FirstEpochs[I]=(*E)[I]; }
        return true;
    }
    bool RetainedSourceCohort(FRun& F)
    {
        auto* S=F.State[0].Get();
        const auto* P=Field<FArrayProperty>(F.StateClass,TEXT("ServerRoles"));
        const auto* Inner=P?CastField<FStructProperty>(P->Inner):nullptr;
        if (!IsValid(S)||!Inner||Inner->Struct!=FCk_Handle::StaticStruct()) return false;
        const auto* Current=P->ContainerPtrToValuePtr<TArray<FCk_Handle>>(S);
        if (!Current||Current->Num()!=Roles||!Allocated(F.SourceOwner)) return false;
        for (int32 I=0;I<Roles;++I)
        {
            if ((*Current)[I]!=F.SourceRoles[I]||
                !Direct((*Current)[I],F.SourceOwner)) return false;
            auto* Script=UCk_Utils_EntityScript_UE::TryGet_EntityScript((*Current)[I]);
            if (!IsValid(Script)||Script->GetClass()!=F.RoleClass[I]) return false;
        }
        return true;
    }
    bool CurrentPersistentVisual(FRun& F,FCk_Handle& OutCue,FCk_Handle& OutShape)
    {
        OutCue={}; OutShape={};
        auto* W=F.World[0].Get(); const auto* Wire=Rows(F.SampleType,F.Data(0));
        if (!IsValid(W)||!IsValid(F.PersistentClass)||!Wire||
            !Direct(F.SourceRoles[0],F.SourceOwner)) return false;
        for (const auto& Row:*Wire)
        {
            FCk_Handle Cue;
            if (Row.Role!=0||!FindUnder(F.SourceRoles[0],WorldRoot(W),Row.Key,Cue)) continue;
            auto* Script=Allocated(Cue)?UCk_Utils_EntityScript_UE::TryGet_EntityScript(Cue):nullptr;
            if (!IsValid(Script)||Script->GetClass()!=F.PersistentClass||
                !Direct(Cue,F.SourceRoles[0])) continue;
            const auto Shape=VisualHandle(Script);
            if (!Direct(Shape,Cue)||!IsValid(ShapeMesh(Shape,W))) continue;
            OutCue=Cue; OutShape=Shape; return true;
        }
        return false;
    }
    bool StationsStable(FRun& F,int32 Client)
    {
        auto* PC=Client<0?F.Host.Get():F.Local[Client].Get();
        const auto& Expected=Client<0?F.HostStations:F.Stations[Client];
        TArray<FCk_Handle> Current;
        if (!Handles(PC,TEXT("Get_CapturedCueStations"),Current)||
            Current.Num()!=Roles||Expected.Num()!=Roles) return false;
        for (int32 I=0;I<Roles;++I)
        { if (Current[I]!=Expected[I]) return false; }
        return true;
    }
    bool Displays(FRun& F,int32 Client)
    {
        const void* D=F.Data(Client<0?0:Client+1);
        const auto& Station=Client<0?F.HostStations:F.Stations[Client];
        auto* Bodies=Field<FArrayProperty>(F.SampleType,TEXT("Bodies"));
        auto* Instructions=Field<FArrayProperty>(F.SampleType,TEXT("Instructions"));
        if (!D||!IsValid(F.DisplayType)||!Bodies||!Instructions||
            !CastField<FStrProperty>(Bodies->Inner)||
            !CastField<FStrProperty>(Instructions->Inner)||
            Station.Num()!=Roles) return false;
        const auto* B=Bodies->ContainerPtrToValuePtr<TArray<FString>>(D);
        const auto* I=Instructions->ContainerPtrToValuePtr<TArray<FString>>(D);
        if (!B||!I||B->Num()!=Roles||I->Num()!=Roles) return false;
        auto* Title=Field<FTextProperty>(F.DisplayType,TEXT("Title"));
        auto* Description=Field<FTextProperty>(F.DisplayType,TEXT("Description"));
        auto* Guidance=Field<FTextProperty>(F.DisplayType,TEXT("Instructions"));
        if (!Title||!Description||!Guidance) return false;
        for (int32 Role=0;Role<Roles;++Role)
        {
            const auto H=Station[Role];
            if (!Allocated(H)||!UCk_Utils_DynamicFragment_UE::Has_Fragment(H,F.DisplayType))
                return false;
            const auto* V=UCk_Utils_DynamicFragment_UE::TryGet_Fragment_TypeUnsafe(H,F.DisplayType);
            if (!V||V->GetScriptStruct()!=F.DisplayType) return false;
            const void* T=V->GetMemory();
            if (!T||Description->GetPropertyValue_InContainer(T).ToString()!=(*B)[Role]||
                Guidance->GetPropertyValue_InContainer(T).ToString()!=(*I)[Role]||
                Title->GetPropertyValue_InContainer(T).IsEmpty()) return false;
        }
        return true;
    }
    void Observe(FRun& F,UWorld* W,ELevelTick Tick)
    {
        if (Tick!=LEVELTICK_All) return;
        if (F.WireInjected&&W==F.World[1].Get())
        {
            auto* P=F.WireBaselinePresenter.Get();
            auto* Current=FindPresenter(W,F.Local[0].Get());
            if (!IsValid(P)||Current!=P||
                P->Get_InstalledSequence()>F.WireBaselineInstalledSequence||
                (!P->Get_IsFailed()&&P->Get_MaterializedRowCount()!=F.WireBaselineMaterializedCount)||
                (F.WireFailureTicks>0&&(!P->Get_IsFailed()||!P->IsHidden()||
                    P->Get_MaterializedRowCount()!=0||
                    !InstalledCleared(F.Local[0].Get(),F.SampleType))))
            { F.WirePresentationViolated=true; }
            if (IsValid(P)&&P->Get_IsFailed()) ++F.WireFailureTicks;
        }
        for (int32 I=0;I<Worlds;++I)
        { if (W==F.World[I].Get()) ObserveTransient(F,I); }
        if (W!=F.World[0].Get()||!F.SampleType) return;
        if (Lifecycle(F.SampleType,F.Data(0))==3)
        { ++F.TerminalTicks; return; }
        if (F.TerminalTicks>0&&Lifecycle(F.SampleType,F.Data(0))==1)
        { F.TerminalReactivated=true; }
        if (Lifecycle(F.SampleType,F.Data(0))!=1||F.InitialGeneration==0) return;
        const int64 Sequence=I64(F.SampleType,F.Data(0),TEXT("Sequence"));
        if (F.History.Contains(Sequence)) return;
        auto Captured=MakeShared<FFrame>();
        Captured->CapturedAt=FPlatformTime::Seconds();
        if (CaptureFrame(F,*Captured)) F.History.Add(Sequence,Captured);
    }
    struct FRestore final:IAutomationLatentCommand
    { TSharedRef<FRun> F; explicit FRestore(TSharedRef<FRun> X):F(X){}
      bool Update() override { F->Restore(); return true; } };
}

bool RunCueSharedWorldFixture(FAutomationTestBase& Test,
    const FString& Parameters,bool VisualLoss,bool TruncatedWire=false)
{
    using namespace ck_cue_shared_world_test;
    auto F=MakeShared<FRun>();
    auto* TestPtr=&Test;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters,F->Dedicated))
    { Test.AddError(TEXT("unsupported Cue network topology")); return false; }
    F->ForcePortFallback=VisualLoss&&!F->Dedicated;
    F->BaseMode=FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    F->Mode=FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    F->StateClass=FSoftClassPath(StatePath).TryLoadClass<AGameStateBase>();
    F->PCClass=FSoftClassPath(PCPath).TryLoadClass<APlayerController>();
    F->TransientClass=FSoftClassPath(TransientPath).TryLoadClass<UObject>();
    F->PersistentClass=FSoftClassPath(PersistentPath).TryLoadClass<UObject>();
    for (int32 I=0;I<Roles;++I)
    { F->RoleClass[I]=FSoftClassPath(RolePaths[I]).TryLoadClass<UObject>(); }
    F->Snapshot=Field<FStructProperty>(F->StateClass,TEXT("Snapshot"));
    F->SampleType=F->Snapshot?F->Snapshot->Struct.Get():nullptr;
    F->DisplayType=FindObject<UScriptStruct>(nullptr,
        TEXT("/Script/Angelscript.CkGym_Station_TitleAndDescription"));
    bool Classes=F->BaseMode&&F->Mode&&F->StateClass&&F->PCClass&&
        F->TransientClass&&F->PersistentClass&&F->SampleType&&F->DisplayType&&
        Field<FArrayProperty>(F->SampleType,TEXT("VisualRows"))&&
        Field<FIntProperty>(F->SampleType,TEXT("VisualRowCount"))&&
        Field<FBoolProperty>(F->RoleClass[2],TEXT("Phase2_RequireExecuted"));
    for (auto* C:F->RoleClass) Classes&=C!=nullptr;
    if (!Test.TestTrue(TEXT("authored six-role classes and native visual wire"),Classes)) return false;
    if (F->ForcePortFallback&&!F->ReserveConfiguredPort())
    { Test.AddError(TEXT("could not reserve configured PIE listen port for real fallback test")); return false; }
    Test.AddExpectedErrorPlain(TEXT("OwnerEntity is invalid when trying to execute Cue [CueGym.Owner.RequireValid] (OwnerValidationPolicy = RequireValid)"),
        EAutomationExpectedErrorFlags::Contains,0);
    const auto* Settings=GetDefault<UCkGym_StartupSettings>();
    F->Startup=Settings->StartupMode; F->Last=Settings->LastGymName;
    F->Recents=Settings->RecentGymNames;
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode=ECkGym_StartupMode::Cycler;
    F->Observer=FWorldDelegates::OnWorldPostActorTick.AddLambda(
        [F](UWorld* W,ELevelTick T,float){ Observe(*F,W,T); });
    Test.AddCommand(new FCk_Latent_StartPIEMultiClient(Worlds,Entry,Parameters,F->BaseMode));
    Test.AddCommand(new FCk_Latent_WaitForPIEReady(Worlds,60));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([]()
        { auto* W=ck::auto_test::net::Get_ServerWorld(); auto* R=Registry(W);
          return IsValid(R)&&R->Find_GymIndexByName(TEXT("Cue"))!=INDEX_NONE; }),
        30,TEXT("authored Cue gym registered")));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([F]() { return BasePeersReady(*F); }),
        30,TEXT("both base PIE clients have distinct correlated controllers, PlayerStates, and pawns")));
    Test.AddCommand(new FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([F](UWorld* W)
        {
            if (!BasePeersReady(*F))
            { ck::auto_test::net::FailSession(TEXT("base peers lost correlation before travel")); return; }
            F->InitialBoundPort=BoundPort(W);
            if (F->InitialBoundPort<1||
                (F->ForcePortFallback&&F->InitialBoundPort==F->RequestedPort))
            {
                ck::auto_test::net::FailSession(TEXT("PIE did not bind a different live listen port while configured port was reserved"));
                return;
            }
            F->ReleaseReservation();
        })));
    Test.AddCommand(new FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([TestPtr](UWorld* W)
        { auto* PC=IsValid(W)?W->GetFirstPlayerController():nullptr;
          TestPtr->TestTrue(TEXT("owning client requests Cue travel"),IsValid(PC)&&
              UCk_Utils_GymRegistry_UE::TryRequest_TravelToGymFromPlayer(PC,TEXT("Cue"))); })));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([F]()
        {
            if (Context(*F)) return true;
            F->NoteReadiness(TEXT("post-travel two-client context convergence"));
            return false;
        }),60,TEXT("both client controllers, pawns, game states, and server correlation after Cue travel")));
    Test.AddCommand(new FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([TestPtr,F](UWorld*)
        { TestPtr->TestTrue(TEXT("Cue travel preserves initial bound server port and both clients' remote ports"),
            TravelEndpointsMatch(*F)); })));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([F]()
        {
            if (!Context(*F)||Lifecycle(F->SampleType,F->Data(0))!=1)
            { F->NoteReadiness(TEXT("authority context and Active wire")); return false; }
            const bool First=F->InitialGeneration==0;
            if (!Source(*F,First))
            { F->NoteReadiness(TEXT("one real six-role authority source")); return false; }
            const auto* E=Epochs(F->SampleType,F->Data(0));
            if (!E||E->Num()!=Roles) return false;
            if (First)
            { F->InitialGeneration=I32(F->SampleType,F->Data(0),TEXT("Generation"));
              F->FirstSequence=I64(F->SampleType,F->Data(0),TEXT("Sequence"));
              for (int32 I=0;I<Roles;++I) F->FirstEpochs[I]=(*E)[I]; }
            if (F->InitialGeneration!=1||F->History.IsEmpty())
            { F->NoteReadiness(TEXT("native physical source publication history")); return false; }
            for (int32 I=0;I<Clients;++I)
            {
                if (!NoClientSources(*F,I))
                { F->NoteReadiness(I==0?TEXT("client 0 has source tags"):TEXT("client 1 has source tags")); return false; }
                if (!Passive(*F,I))
                { F->NoteReadiness(I==0?TEXT("client 0 passive parity"):TEXT("client 1 passive parity")); return false; }
                if (!Handles(F->Local[I].Get(),TEXT("Get_CapturedCueStations"),F->Stations[I])||
                    F->Stations[I].Num()!=Roles)
                { F->NoteReadiness(I==0?TEXT("client 0 station handles"):TEXT("client 1 station handles")); return false; }
                if (!Displays(*F,I))
                { F->NoteReadiness(I==0?TEXT("client 0 station display"):TEXT("client 1 station display")); return false; }
            }
            if (F->Stations[0][0]==F->Stations[1][0]) return false;
            if (!F->Dedicated)
            {
                if (!F->Host.IsValid()||F->Host==F->Local[0]||F->Host==F->Local[1]||
                    !Handles(F->Host.Get(),TEXT("Get_CapturedCueStations"),F->HostStations)||
                    F->HostStations.Num()!=Roles||!Displays(*F,-1)||
                    F->HostStations[0]==F->Stations[0][0]||
                    F->HostStations[0]==F->Stations[1][0])
                { F->NoteReadiness(TEXT("listen host local stations")); return false; }
                int32 Count=0;
                for (TActorIterator<ACk_CueGym_Presenter_UE> It(F->World[0].Get());It;++It) ++Count;
                if (Count!=0)
                { F->NoteReadiness(TEXT("unexpected listen host presenter")); return false; }
            }
            F->Readiness=TEXT("ready"); return true;
        }),60,TEXT("real six-role source and both owner-local passive projections")));
    Test.AddCommand(new FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([TestPtr,F](UWorld*)
        { if (F->Readiness!=TEXT("ready")) TestPtr->AddError(TEXT("Cue readiness did not complete")); })));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([F]()
        {
            if (!Context(*F))
            { F->NoteReadiness(TEXT("parity: world/controller context")); return false; }
            if (F->BadTransient)
            { F->NoteReadiness(TEXT("parity: invalid observed transient")); return false; }
            for (int32 W=0;W<Worlds;++W)
            { if (F->SeenTransientSerial[W][0]<=0)
              { F->NoteReadiness(W==0?TEXT("parity: server relay physical receipt absent"):
                    W==1?TEXT("parity: client 0 relay physical receipt absent"):
                    TEXT("parity: client 1 relay physical receipt absent")); return false; } }
            for (int32 I=0;I<Clients;++I)
            {
                if (!NoClientSources(*F,I))
                { F->NoteReadiness(I==0?TEXT("parity: client 0 source tags"):
                    TEXT("parity: client 1 source tags")); return false; }
                if (!Passive(*F,I))
                { F->NoteReadiness(I==0?TEXT("parity: client 0 passive mesh"):
                    TEXT("parity: client 1 passive mesh")); return false; }
                if (!StationsStable(*F,I)||!Displays(*F,I))
                { F->NoteReadiness(I==0?TEXT("parity: client 0 station overlay"):
                    TEXT("parity: client 1 station overlay")); return false; }
                const int64 S=I64(F->SampleType,F->Data(I+1),TEXT("Sequence"));
                const auto* Frame=F->History.Find(S);
                if (!Frame||!Frame->IsValid())
                { F->NoteReadiness(I==0?TEXT("parity: client 0 source history sequence missing"):
                    TEXT("parity: client 1 source history sequence missing")); return false; }
                const int64 Serial=(*Frame)->FrameSerial;
                if (I64(F->SampleType,F->Data(I+1),TEXT("FrameSerial"))!=Serial)
                { F->NoteReadiness(I==0?TEXT("parity: client 0 frame serial mismatches source"):
                    TEXT("parity: client 1 frame serial mismatches source")); return false; }
                if (F->LastParityFrame[I]>0&&Serial>F->LastParityFrame[I])
                    F->SawTwoParityFrames[I]=true;
                if (Serial>F->LastParityFrame[I]) F->LastParityFrame[I]=Serial;
            }
            if (!F->Dedicated&&(!StationsStable(*F,-1)||!Displays(*F,-1)))
            { F->NoteReadiness(TEXT("parity: listen host overlay")); return false; }
            if (!F->SawTwoParityFrames[0]||!F->SawTwoParityFrames[1])
            { F->NoteReadiness(TEXT("parity: fewer than two increasing verified installed source frames")); }
            return F->SawTwoParityFrames[0]&&F->SawTwoParityFrames[1];
        }),40,TEXT("actual relay-created transient meshes in every world and two increasing exact CPU frames per client")));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([F]()
        {
            if (!Context(*F)||Lifecycle(F->SampleType,F->Data(0))!=1||
                I32(F->SampleType,F->Data(0),TEXT("Generation"))!=F->InitialGeneration)
            { F->NoteReadiness(TEXT("owner validation: active original source")); return false; }
            const auto Role=F->SourceRoles[2];
            if (!Direct(Role,F->SourceOwner))
            { F->NoteReadiness(TEXT("owner validation: original role ownership")); return false; }
            auto* Script=UCk_Utils_EntityScript_UE::TryGet_EntityScript(Role);
            const auto* Phase2=IsValid(Script)&&Script->GetClass()==F->RoleClass[2]
                ? Field<FBoolProperty>(Script->GetClass(),TEXT("Phase2_RequireExecuted")) : nullptr;
            if (!Phase2||!Phase2->GetPropertyValue_InContainer(Script))
            { F->NoteReadiness(TEXT("owner validation: awaiting real invalid-owner RequireValid phase")); return false; }
            return true;
        }),15,TEXT("authored five-second OwnerValidation timer fires invalid-owner RequireValid before resets")));
    if (TruncatedWire)
    {
        // A received, source-proven new frame is truncated just before the
        // natural client actor tick. The authority and peer values stay intact.
        F->WireObserver=FWorldDelegates::OnWorldPreActorTick.AddLambda(
            [F](UWorld* W,ELevelTick Tick,float)
            {
                if (!F->WireInjectionArmed||F->WireInjected||Tick!=LEVELTICK_All||
                    W!=F->World[1].Get()) return;
                auto* State=F->State[1].Get(); auto* PC=F->Local[0].Get();
                auto* P=F->Presenter[0].Get();
                auto* Count=Field<FIntProperty>(F->SampleType,TEXT("VisualRowCount"));
                auto* Property=Field<FArrayProperty>(F->SampleType,TEXT("VisualRows"));
                auto* Inner=Property?CastField<FStructProperty>(Property->Inner):nullptr;
                if (!IsValid(State)||!IsValid(PC)||!IsValid(P)||P->Get_IsFailed()||
                    P->Get_IsSuspended()||FindPresenter(W,PC)!=P||
                    !F->Snapshot||!Count||!Inner||
                    Inner->Struct!=FCkCueGym_SourceVisualRow::StaticStruct()) return;
                void* D=F->Snapshot->ContainerPtrToValuePtr<void>(State);
                auto* Visuals=Property->ContainerPtrToValuePtr<TArray<FCkCueGym_SourceVisualRow>>(D);
                const int64 Sequence=I64(F->SampleType,D,TEXT("Sequence"));
                const auto* Frame=F->History.Find(Sequence);
                const int64 Installed=InstalledSequence(PC,F->SampleType);
                if (!Visuals||Visuals->Num()<2||!Frame||!Frame->IsValid()||Installed<=0||
                    Sequence<=Installed||P->Get_InstalledSequence()!=Installed||
                    P->Get_MaterializedRowCount()<1||
                    !SameReceivedSourceFrame(*F,D,*Visuals,**Frame)) return;
                F->WireOriginalCount=Visuals->Num();
                F->WireCorruptedSequence=Sequence;
                F->WireBaselineInstalledSequence=Installed;
                F->WireBaselinePresenter=P; F->WireBaselineMaterializedCount=P->Get_MaterializedRowCount();
                Visuals->RemoveAt(Visuals->Num()-1);
                F->WireInjected=true; F->WireInjectionArmed=false;
            });
        Test.AddCommand(new FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda([F](UWorld*)
            { F->WireInjectionArmed=true; })));
        Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([F]()
            {
                if (!F->WireInjected||F->WireOriginalCount<2||
                    F->WireCorruptedSequence<=F->WireBaselineInstalledSequence||
                    F->WirePresentationViolated)
                { F->NoteReadiness(TEXT("truncated wire: new source-proven frame or forbidden prefix install")); return false; }
                auto* P=F->WireBaselinePresenter.Get(); auto* PC=F->Local[0].Get();
                if (!IsValid(P)||!P->Get_IsFailed()||!P->IsHidden()||
                    P->Get_MaterializedRowCount()!=0||F->WireFailureTicks<20||
                    FindPresenter(F->World[1].Get(),PC)!=P||
                    !InstalledCleared(PC,F->SampleType)||!StationsStable(*F,0))
                { F->NoteReadiness(TEXT("truncated wire: client 0 terminal closure")); return false; }
                if (!Context(*F)||Lifecycle(F->SampleType,F->Data(0))!=1||
                    Lifecycle(F->SampleType,F->Data(2))!=1||!Source(*F,false)||
                    !Passive(*F,1)||!StationsStable(*F,1)||
                    !NoClientSources(*F,0)||!NoClientSources(*F,1)||
                    (F->Dedicated?false:(!StationsStable(*F,-1)||!Displays(*F,-1))))
                { F->NoteReadiness(TEXT("truncated wire: authority and independent peer remain active")); return false; }
                return true;
            }),20,TEXT("new received source frame with one missing visual row closes only that client")));
        Test.AddCommand(new FCk_Latent_EndPIE());
        Test.AddCommand(new FRestore(F));
        return true;
    }
    Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
        FCk_NetAutoTest_Assertion::CreateLambda([F]()
        { F->SelectedOld=F->SourceRoles[F->SelectedRole];
          F->UnchangedOld=F->SourceRoles[0];
          for (int32 W=0;W<Worlds;++W)
          { F->BeforeSelectedTransientSerial[W]=F->SeenTransientSerial[W][0]; }
          return Rpc(F->Local[0].Get(),4201,F->InitialGeneration,F->SelectedRole); }),
        TEXT("owner A sends selected-role reset")));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([F]()
        {
            if (!Context(*F)||!Ack(F->Local[0].Get(),4201,F->InitialGeneration,1))
            { F->NoteReadiness(TEXT("selected reset: context or ACK")); return false; }
            if (!F->SelectedCaptured)
            {
                if (!ChangedRoles(*F,F->InitialGeneration+1,F->SelectedRole)||
                    F->SourceRoles[0]!=F->UnchangedOld||
                    !CaptureNewRoles(*F,F->InitialGeneration+1))
                { F->NoteReadiness(TEXT("selected reset: physical role replacement")); return false; }
                F->SelectedCaptured=true;
            }
            for (int32 I=0;I<Clients;++I)
            { if (!Passive(*F,I)||!StationsStable(*F,I)||
                  !NoClientSources(*F,I)||!Displays(*F,I))
              { F->NoteReadiness(I==0?TEXT("selected reset: client 0 projection"):
                    TEXT("selected reset: client 1 projection")); return false; } }
            if (!F->Dedicated&&(!StationsStable(*F,-1)||!Displays(*F,-1)))
            { F->NoteReadiness(TEXT("selected reset: listen host projection")); return false; }
            for (int32 W=0;W<Worlds;++W)
            { if (F->SeenTransientSerial[W][0]<=F->BeforeSelectedTransientSerial[W])
              { F->NoteReadiness(TEXT("selected reset: new relay receipt")); return false; } }
            return true;
        }),60,TEXT("selected role physically replaced, five sources retained, same local overlays")));
    Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
        FCk_NetAutoTest_Assertion::CreateLambda([F]()
        { return Rpc(F->Local[1].Get(),4202,F->InitialGeneration+1,-1); }),
        TEXT("owner B sends all-role R reset")));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([F,VisualLoss]()
        {
            if (!Context(*F)||!Ack(F->Local[1].Get(),4202,F->InitialGeneration+1,1))
            { F->NoteReadiness(TEXT("all reset: context or ACK")); return false; }
            if (!F->AllCaptured)
            {
                if (!ChangedRoles(*F,F->InitialGeneration+2,-1)||
                    !CaptureNewRoles(*F,F->InitialGeneration+2))
                { F->NoteReadiness(TEXT("all reset: physical role replacement")); return false; }
                F->AllCaptured=true;
            }
            for (int32 I=0;I<Clients;++I)
            { if (!Passive(*F,I)||!StationsStable(*F,I)||
                  !NoClientSources(*F,I)||!Displays(*F,I))
              { F->NoteReadiness(I==0?TEXT("all reset: client 0 projection"):
                    TEXT("all reset: client 1 projection")); return false; } }
            if (!F->Dedicated&&(!StationsStable(*F,-1)||!Displays(*F,-1)))
            { F->NoteReadiness(TEXT("all reset: listen host projection")); return false; }
            if (F->BadTransient)
            { F->NoteReadiness(TEXT("all reset: invalid relay receipt")); return false; }
            for (int32 W=0;W<Worlds;++W)
            { if (F->SeenTransientSerial[W][1]<=0)
              { F->NoteReadiness(TEXT("all reset: new relay receipt")); return false; } }
            if (VisualLoss)
            {
                FCk_Handle Cue,Shape;
                if (!CurrentPersistentVisual(*F,Cue,Shape))
                { F->NoteReadiness(TEXT("all reset: persistent source visual")); return false; }
            }
            if (I64(F->SampleType,F->Data(0),TEXT("TransientRequestedSerial"))<=0)
            { F->NoteReadiness(TEXT("all reset: source transient serial")); return false; }
            return true;
        }),60,TEXT("R physically replaces six sources; both independent overlays project generation three")));
    Test.AddCommand(new FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([F,VisualLoss](UWorld* W)
        {
            if (!IsValid(W)||W!=F->World[0].Get()||
                Lifecycle(F->SampleType,F->Data(0))!=1||F->BadTransient||
                I64(F->SampleType,F->Data(0),TEXT("TransientRequestedSerial"))<=0) return;
            F->DestroyedRole=F->SourceRoles[0];
            auto* Script=Allocated(F->DestroyedRole)
                ? UCk_Utils_EntityScript_UE::TryGet_EntityScript(F->DestroyedRole):nullptr;
            F->DestroyedRoleWasLive=Direct(F->DestroyedRole,F->SourceOwner)&&
                IsValid(Script)&&Script->GetClass()==F->RoleClass[0];
            if (!F->DestroyedRoleWasLive) return;
            if (!VisualLoss)
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F->DestroyedRole); return; }
            if (!CurrentPersistentVisual(*F,F->DestroyedCue,F->DestroyedShape)) return;
            F->VisualLossInjected=true;
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F->DestroyedShape);
        })));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([F,VisualLoss]()
        {
            if (!Context(*F)||!F->DestroyedRoleWasLive||
                (VisualLoss ? (!F->VisualLossInjected||Allocated(F->DestroyedShape)||
                    !RetainedSourceCohort(*F)||!Direct(F->DestroyedCue,F->DestroyedRole)) :
                    Allocated(F->DestroyedRole))||F->TerminalReactivated||
                Lifecycle(F->SampleType,F->Data(0))!=3||F->TerminalTicks<20||
                I32(F->SampleType,F->Data(0),TEXT("Generation"))!=F->InitialGeneration+2)
                return false;
            if (VisualLoss)
            {
                auto* CueScript=UCk_Utils_EntityScript_UE::TryGet_EntityScript(F->DestroyedCue);
                if (!IsValid(CueScript)||VisualHandle(CueScript)!=F->DestroyedShape) return false;
                TArray<FCk_Handle> Children;
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                    F->DestroyedCue,WorldRoot(F->World[0].Get()),Children)) return false;
                for (const auto& Child:Children)
                { if (IsValid(ShapeMesh(Child,F->World[0].Get()))) return false; }
            }
            for (int32 I=0;I<Clients;++I)
            {
                auto* P=F->Presenter[I].Get();
                if (Lifecycle(F->SampleType,F->Data(I+1))!=3||!IsValid(P)||
                    !P->Get_IsFailed()||!P->IsHidden()||
                    FindPresenter(F->World[I+1].Get(),F->Local[I].Get())!=P||
                    !NoClientSources(*F,I)||!StationsStable(*F,I)) return false;
            }
            if (!F->Dedicated&&!StationsStable(*F,-1)) return false;
            return true;
        }),40,VisualLoss
            ? TEXT("current authored PMG receipt loss closes source with retained inert cohort")
            : TEXT("real authority role loss yields terminal failed world and fixed closed projections")));
    Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
        FCk_NetAutoTest_Assertion::CreateLambda([F]()
        { return Rpc(F->Local[1].Get(),4203,F->InitialGeneration+2,-1); }),
        TEXT("later owner tries control after terminal failure")));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([F]()
        { return Ack(F->Local[1].Get(),4203,F->InitialGeneration+2,3)&&
            F->TerminalTicks>=30&&!F->TerminalReactivated&&
            Lifecycle(F->SampleType,F->Data(0))==3&&
            F->Presenter[0].IsValid()&&F->Presenter[0]->Get_IsFailed()&&
            F->Presenter[1].IsValid()&&F->Presenter[1]->Get_IsFailed(); }),
        20,TEXT("terminal source rejects later R without retry or presenter replacement")));
    Test.AddCommand(new FCk_Latent_EndPIE());
    Test.AddCommand(new FRestore(F));
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCueGym_SharedWorld_Network,
    "Ck.Cue.Gym.SharedWorld.Network",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|
    EAutomationTestFlags::EngineFilter)

bool FCkCueGym_SharedWorld_Network::RunTest(const FString& Parameters)
{ return RunCueSharedWorldFixture(*this,Parameters,false); }

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCueGym_SharedWorld_VisualReceiptLoss_Network,
    "Ck.Cue.Gym.SharedWorld.VisualReceiptLoss.Network",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|
    EAutomationTestFlags::EngineFilter)

bool FCkCueGym_SharedWorld_VisualReceiptLoss_Network::RunTest(const FString& Parameters)
{ return RunCueSharedWorldFixture(*this,Parameters,true); }

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCueGym_SharedWorld_TruncatedVisualWire_Network,
    "Ck.Cue.Gym.SharedWorld.TruncatedVisualWire.Network",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|
    EAutomationTestFlags::EngineFilter)

bool FCkCueGym_SharedWorld_TruncatedVisualWire_Network::RunTest(const FString& Parameters)
{ return RunCueSharedWorldFixture(*this,Parameters,false,true); }
#endif
