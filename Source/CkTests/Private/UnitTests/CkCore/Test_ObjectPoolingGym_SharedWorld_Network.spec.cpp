#include "CoreMinimal.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkGym_StartupSettings.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkGym_WireTransformRuntime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkCore/Object/CkObject_Utils.h"
#include "CkCore/ObjectPooling/CkObjectPooling_Subsystem.h"
#include "CkDynamic/CkDynamic_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkStateMachine/State/EntityScripts/CkSmState_EntityScript.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace ck_object_pooling_shared_network
{
    constexpr auto Map = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr const TCHAR* Tags[2] = {TEXT("Gym.ObjectPooling.ScriptChurn"),TEXT("Gym.ObjectPooling.SmChurn")};
    constexpr const TCHAR* Titles[2] = {TEXT("OBJECT POOLING - SCRIPT CHURN STRESS"),
        TEXT("OBJECT POOLING - STATEMACHINE CHURN STRESS")};
    template<class T> auto Field(const UStruct* S,const TCHAR* N) -> T*
    { return S ? FindFProperty<T>(S,N) : nullptr; }
    auto I32(const UStruct* S,const void* D,const TCHAR* N) -> int32
    { const auto* P=Field<FIntProperty>(S,N); return D && P ? P->GetPropertyValue_InContainer(D) : INDEX_NONE; }
    auto Flag(const UStruct* S,const void* D,const TCHAR* N) -> bool
    { const auto* P=Field<FBoolProperty>(S,N); return D && P && P->GetPropertyValue_InContainer(D); }
    auto Obj(const UStruct* S,const void* D,const TCHAR* N) -> UObject*
    { const auto* P=Field<FObjectPropertyBase>(S,N); return D && P ? P->GetObjectPropertyValue_InContainer(D) : nullptr; }
    auto Str(const UStruct* S,const void* D,const TCHAR* N) -> FString
    { const auto* P=Field<FStrProperty>(S,N); return D && P ? P->GetPropertyValue_InContainer(D) : FString{}; }
    auto Dbl(const UStruct* S,const void* D,const TCHAR* N) -> double
    { const auto* P=Field<FDoubleProperty>(S,N); return D && P ? P->GetPropertyValue_InContainer(D) : std::numeric_limits<double>::quiet_NaN(); }
    auto Live(const FCk_Handle& H) -> bool
    { return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(H,ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    auto Allocated(const FCk_Handle& H) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
    auto Root(UWorld* W) -> FCk_Handle
    { return IsValid(W) ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W) : FCk_Handle{}; }
    auto Tagged(UWorld* W,int32 Slot) -> TArray<FCk_Handle>
    { const auto R=Root(W); return Live(R) && Slot>=0 && Slot<2
        ? UCk_Utils_EntityTag_UE::ForEach_Entity(R,FName(Tags[Slot])) : TArray<FCk_Handle>{}; }
    auto Direct(const FCk_Handle& H,const FCk_Handle& Owner) -> bool
    { return Live(H) && Live(Owner) && UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H) &&
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H)==Owner; }
    auto Closure(const FCk_Handle& H,const FCk_Handle& WorldRoot,TSet<FCk_Handle>& Out) -> bool
    {
        if (!Allocated(H) || Out.Contains(H)) { return true; }
        if (!Allocated(WorldRoot)) { return false; }
        TArray<FCk_Handle> Children;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H,WorldRoot,Children))
        { return false; }
        Out.Add(H);
        for (const auto& Child:Children) { if (!Closure(Child,WorldRoot,Out)) { return false; } }
        return true;
    }
    auto Gone(const TSet<FCk_Handle>& Set) -> bool
    { for (const auto& H:Set) { if (Allocated(H)) { return false; } } return true; }
    struct FCall
    {
        UObject* Target=nullptr; UFunction* Function=nullptr; TUniquePtr<FStructOnScope> Args;
        FCall(UObject* T,const TCHAR* N,EFunctionFlags Flags)
        {
            if (!IsValid(T)) { return; }
            auto* Fn=T->FindFunction(N);
            if (!IsValid(Fn) || !Fn->HasAllFunctionFlags(Flags)) { return; }
            Target=T; Function=Fn; Args=MakeUnique<FStructOnScope>(Fn);
        }
        bool Send() const
        { if (!IsValid(Target) || !IsValid(Function) || !Args) { return false; }
          Target->ProcessEvent(Function,Args->GetStructMemory()); return true; }
    };
    auto PureString(UObject* T,const TCHAR* N,FString& Out) -> bool
    { FCall C(T,N,FUNC_BlueprintPure); const auto* P=Field<FStrProperty>(C.Function,TEXT("ReturnValue"));
      if (!P || C.Function->NumParms!=1 || !C.Send()) { return false; }
      Out=P->GetPropertyValue_InContainer(C.Args->GetStructMemory()); return true; }
    auto PureBool(UObject* T,const TCHAR* N,bool& Out) -> bool
    { FCall C(T,N,FUNC_BlueprintPure); auto* P=Field<FBoolProperty>(C.Function,TEXT("ReturnValue"));
      if (!P || C.Function->NumParms!=1 || !C.Send()) { return false; }
      Out=P->GetPropertyValue_InContainer(C.Args->GetStructMemory()); return true; }
    auto PureHandles(UObject* T,const TCHAR* N,TArray<FCk_Handle>& Out) -> bool
    { FCall C(T,N,FUNC_BlueprintPure); const auto* P=Field<FArrayProperty>(C.Function,TEXT("ReturnValue"));
      const auto* Inner=P ? CastField<FStructProperty>(P->Inner) : nullptr;
      if (!Inner || Inner->Struct!=FCk_Handle::StaticStruct() || C.Function->NumParms!=1 || !C.Send()) { return false; }
      Out=*P->ContainerPtrToValuePtr<TArray<FCk_Handle>>(C.Args->GetStructMemory()); return true; }
    auto PureInts(UObject* T,const TCHAR* N,TArray<int32>& Out) -> bool
    { FCall C(T,N,FUNC_BlueprintPure); const auto* P=Field<FArrayProperty>(C.Function,TEXT("ReturnValue"));
      if (!P || !CastField<FIntProperty>(P->Inner) || C.Function->NumParms!=1 || !C.Send()) { return false; }
      Out=*P->ContainerPtrToValuePtr<TArray<int32>>(C.Args->GetStructMemory()); return true; }
    auto PureHandle(UObject* T,const TCHAR* N,FCk_Handle& Out) -> bool
    { FCall C(T,N,FUNC_BlueprintPure); const auto* P=Field<FStructProperty>(C.Function,TEXT("ReturnValue"));
      if (!P || P->Struct!=FCk_Handle::StaticStruct() || C.Function->NumParms!=1 || !C.Send()) { return false; }
      Out=*P->ContainerPtrToValuePtr<FCk_Handle>(C.Args->GetStructMemory()); return true; }
    auto Copy(const UScriptStruct* Type,const void* Data) -> TSharedPtr<FStructOnScope>
    { if (!IsValid(Type) || !Data) { return {}; } auto Value=MakeShared<FStructOnScope>(Type);
      Type->CopyScriptStruct(Value->GetStructMemory(),Data); return Value; }
    auto PureStruct(UObject* T,const TCHAR* N,const UScriptStruct* Type) -> TSharedPtr<FStructOnScope>
    { FCall C(T,N,FUNC_BlueprintPure); const auto* P=Field<FStructProperty>(C.Function,TEXT("ReturnValue"));
      return P && P->Struct==Type && C.Function->NumParms==1 && C.Send()
        ? Copy(Type,P->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory())) : nullptr; }
    auto PoolHealthy(const FCk_ObjectPooling_PoolStats& S) -> bool
    { return S.Get_NumHits()>=0 && S.Get_NumMisses()>=0 && S.Get_NumFree()>=0 &&
        S.Get_NumInUse()>=0 && S.Get_NumLiveInstances()==S.Get_NumFree()+S.Get_NumInUse() &&
        S.Get_HighWaterMark()>=S.Get_NumInUse(); }
    auto PoolZero(const FCk_ObjectPooling_PoolStats& S) -> bool
    { return S.Get_NumHits()==0 && S.Get_NumMisses()==0 && S.Get_NumInUse()==0 &&
        S.Get_NumLiveInstances()==0 && S.Get_NumFree()==0; }

    struct FFixture
    {
        UClass *Mode=nullptr,*StateClass=nullptr,*PCClass=nullptr,*StationClass=nullptr,*PoolClass=nullptr;
        UClass *PingClass=nullptr,*PongClass=nullptr,*ConditionClass=nullptr;
        UClass* PoolKeys[4]{};
        UScriptStruct *SnapshotType=nullptr,*CommitType=nullptr,*CadenceType=nullptr,*RecipeType=nullptr;
        UScriptStruct *RestartAdmissionType=nullptr,*CommitAdmissionType=nullptr;
        FStructProperty *Snapshot=nullptr,*Commit=nullptr,*CommitSnapshot=nullptr;
        FArrayProperty *Recipes=nullptr,*Titles=nullptr,*Bodies=nullptr;
        TWeakObjectPtr<UWorld> Worlds[3]; TWeakObjectPtr<AGameStateBase> States[3];
        TWeakObjectPtr<APlayerController> Local[2],Remote[2],Host;
        TWeakObjectPtr<APawn> LocalPawn[2],RemotePawn[2],HostPawn;
        TMap<uint64,TSharedPtr<FStructOnScope>> AuthorityHistory;
        TArray<uint64> AuthorityOrder;
        TSharedPtr<FStructOnScope> FirstCommit;
        TSet<FCk_Handle> OldClosure;
        TSet<FCk_Handle> TerminalClosure;
        TSet<FCk_Handle> FailedLocalClosure;
        TWeakObjectPtr<UCk_EntityScript_UE> ReuseScript;
        TArray<TWeakObjectPtr<AActor>> ActorCarriers;
        FCk_Handle ReuseFirstTarget,TickOwner,OldHerdOwner,ServerStations[2];
        FCk_Handle UnknownChild;
        int32 ServerPostTicks=0,FailedAtPostTick=0,FailedSpawned=0,FailedDestroyed=0;
        int32 Generation=0,FirstSerial=101,SecondSerial=202;
        int32 FirstSpawned=0,FirstDestroyed=0;
        int32 RejectedSerial=303;
        int32 BeforeLocalFailureSequence=0;
        int32 AckBefore=0,AckDelivery=0,AckSerial=0;
        FCk_ObjectPooling_PoolStats BeforeRestart[4];
        bool Dedicated=false,Captured=false,SawReuse=false,SawOldGoneBeforeReady=false;
        bool SawNewReady=false,SawReadyBeforeRetirementProof=false;
        FDelegateHandle Observer;
        ECkGym_StartupMode Startup=ECkGym_StartupMode::Cycler; FString Last; TArray<FString> Recents;
        FString LastDiagnostic;
        static uint64 Key(int32 G,int32 Q) { return (uint64(uint32(G))<<32)|uint32(Q); }
        AGameStateBase* State(int32 I) const
        { auto* W=Worlds[I].Get(); auto* S=IsValid(W)?W->GetGameState():nullptr;
          return IsValid(S) && S->GetClass()==StateClass && S->GetWorld()==W ? S : nullptr; }
        const void* Data(int32 I) const
        { auto* S=State(I); return S && Snapshot ? Snapshot->ContainerPtrToValuePtr<void>(S) : nullptr; }
        const void* OwnerCommit(int32 I,bool ServerSide) const
        { auto* P=ServerSide?Remote[I].Get():Local[I].Get();
          return IsValid(P) && P->GetClass()==PCClass && Commit ? Commit->ContainerPtrToValuePtr<void>(P) : nullptr; }
        void Restore()
        { if (Observer.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(Observer); Observer.Reset(); }
          auto* S=GetMutableDefault<UCkGym_StartupSettings>(); S->StartupMode=Startup; S->LastGymName=Last; S->RecentGymNames=Recents; }
        bool Context() const
        {
            auto* Server=ck::auto_test::net::Get_ServerWorld();
            const auto All=ck::auto_test::net::Get_AllPIEWorlds();
            if (!IsValid(Server) || Server!=Worlds[0].Get() || All.Num()!=3 ||
                ck::auto_test::net::Get_NumClientWorlds()!=2 ||
                Server->GetNetMode()!=(Dedicated?NM_DedicatedServer:NM_ListenServer) ||
                !Server->HasBegunPlay() || !IsValid(Server->GetAuthGameMode()) ||
                Server->GetAuthGameMode()->GetClass()!=Mode || !IsValid(State(0)) ||
                (Captured && States[0].Get()!=State(0))) { return false; }
            for (int32 I=0;I<2;++I)
            {
                auto* W=ck::auto_test::net::Get_ClientWorld(I);
                auto* P=IsValid(W)?W->GetFirstPlayerController():nullptr;
                auto* R=ck::auto_test::net::TryGet_RemoteClientPlayerController(Server,I);
                auto* GI=IsValid(W)?W->GetGameInstance():nullptr;
                auto* LP=IsValid(P)?P->GetLocalPlayer():nullptr;
                if (!IsValid(W) || W!=Worlds[I+1].Get() || !All.Contains(W) || W==Server ||
                    W->GetNetMode()!=NM_Client || !W->HasBegunPlay() || !IsValid(State(I+1)) ||
                    !IsValid(P) || !IsValid(R) || P->GetClass()!=PCClass || R->GetClass()!=PCClass ||
                    !P->IsLocalController() || P->GetWorld()!=W || !R->HasAuthority() || R->GetWorld()!=Server ||
                    !IsValid(P->GetPawn()) || !IsValid(R->GetPawn()) ||
                    P->GetPawn()->GetController()!=P || R->GetPawn()->GetController()!=R ||
                    P->GetPawn()->GetWorld()!=W || R->GetPawn()->GetWorld()!=Server ||
                    !IsValid(GI) || !IsValid(LP) || GI->GetWorld()!=W || GI->GetLocalPlayers().Num()!=1 ||
                    LP->GetPlayerController(W)!=P ||
                    (Captured && (States[I+1].Get()!=State(I+1) || Local[I].Get()!=P || Remote[I].Get()!=R ||
                        LocalPawn[I].Get()!=P->GetPawn() || RemotePawn[I].Get()!=R->GetPawn()))) { return false; }
            }
            if (Local[0].IsValid() && (Local[0]==Local[1] || Remote[0]==Remote[1])) { return false; }
            auto* ServerGI=Server->GetGameInstance();
            if (!IsValid(ServerGI)) { return false; }
            if (Dedicated) { return ServerGI->GetLocalPlayers().IsEmpty() && (!Captured || !Host.IsValid()); }
            if (ServerGI->GetLocalPlayers().Num()!=1 || !IsValid(ServerGI->GetLocalPlayers()[0])) { return false; }
            auto* H=ServerGI->GetLocalPlayers()[0]->GetPlayerController(Server);
            return IsValid(H) && H->GetClass()==PCClass && H->IsLocalController() && H->HasAuthority() &&
                IsValid(H->GetPawn()) && H->GetPawn()->GetController()==H && H->GetWorld()==Server &&
                (!Captured || (Host.Get()==H && HostPawn.Get()==H->GetPawn()));
        }
    };

    auto Schema(FFixture& F,FAutomationTestBase& Test) -> bool
    {
        F.Snapshot=Field<FStructProperty>(F.StateClass,TEXT("Snapshot"));
        F.Commit=Field<FStructProperty>(F.PCClass,TEXT("LastControlCommit"));
        F.SnapshotType=F.Snapshot?F.Snapshot->Struct:nullptr;
        F.CommitType=F.Commit?F.Commit->Struct:nullptr;
        F.CommitSnapshot=Field<FStructProperty>(F.CommitType,TEXT("Snapshot"));
        F.Recipes=Field<FArrayProperty>(F.SnapshotType,TEXT("Stations"));
        F.RecipeType=F.Recipes && CastField<FStructProperty>(F.Recipes->Inner)
            ? CastField<FStructProperty>(F.Recipes->Inner)->Struct:nullptr;
        F.Titles=Field<FArrayProperty>(F.SnapshotType,TEXT("Titles"));
        F.Bodies=Field<FArrayProperty>(F.SnapshotType,TEXT("Bodies"));
        // Resolve the pure return type without invoking the CDO's getter.
        FCall CadenceCall(F.StateClass->GetDefaultObject(),TEXT("Get_LastRestartCadence"),FUNC_BlueprintPure);
        const auto* CadenceReturn=Field<FStructProperty>(CadenceCall.Function,TEXT("ReturnValue"));
        F.CadenceType=CadenceReturn?CadenceReturn->Struct:nullptr;
        FCall RestartCall(F.StateClass->GetDefaultObject(),TEXT("Get_LastRestartAdmissionAudit"),FUNC_BlueprintPure);
        FCall RejectCall(F.StateClass->GetDefaultObject(),TEXT("Get_LastRestartAdmissionReject"),FUNC_BlueprintPure);
        FCall OwnerCall(F.PCClass->GetDefaultObject(),TEXT("Get_LastCommitAdmissionAudit"),FUNC_BlueprintPure);
        const auto* RestartReturn=Field<FStructProperty>(RestartCall.Function,TEXT("ReturnValue"));
        const auto* OwnerReturn=Field<FStructProperty>(OwnerCall.Function,TEXT("ReturnValue"));
        F.RestartAdmissionType=RestartReturn?RestartReturn->Struct:nullptr;
        F.CommitAdmissionType=OwnerReturn?OwnerReturn->Struct:nullptr;
        const auto* AllocatedField=Field<FArrayProperty>(F.RestartAdmissionType,TEXT("Allocated"));
        const auto* AllocatedInner=AllocatedField?CastField<FStructProperty>(AllocatedField->Inner):nullptr;
        bool Good=RejectCall.Function && RejectCall.Function->NumParms==1 &&
            Field<FStrProperty>(RejectCall.Function,TEXT("ReturnValue")) &&
            F.SnapshotType && F.CommitType && F.RecipeType && F.CadenceType &&
            F.RestartAdmissionType && F.CommitAdmissionType &&
            AllocatedInner && AllocatedInner->Struct==FCk_Handle::StaticStruct() &&
            Field<FBoolProperty>(F.RestartAdmissionType,TEXT("Captured")) &&
            Field<FIntProperty>(F.RestartAdmissionType,TEXT("RequestSerial")) &&
            Field<FIntProperty>(F.RestartAdmissionType,TEXT("OriginGeneration")) &&
            Field<FObjectPropertyBase>(F.RestartAdmissionType,TEXT("Owner")) &&
            Field<FBoolProperty>(F.CommitAdmissionType,TEXT("Observed")) &&
            Field<FIntProperty>(F.CommitAdmissionType,TEXT("RequestSerial")) &&
            Field<FIntProperty>(F.CommitAdmissionType,TEXT("DeliverySerial")) &&
            Field<FIntProperty>(F.CommitAdmissionType,TEXT("AcknowledgedAtPublish")) &&
            Field<FBoolProperty>(F.CommitAdmissionType,TEXT("CanAcceptAnother")) &&
            F.CommitSnapshot && F.CommitSnapshot->Struct==F.SnapshotType &&
            F.Titles && F.Bodies && CastField<FStrProperty>(F.Titles->Inner) &&
            CastField<FStrProperty>(F.Bodies->Inner) &&
            Field<FIntProperty>(F.SnapshotType,TEXT("CatalogVersion")) &&
            Field<FIntProperty>(F.SnapshotType,TEXT("Generation")) &&
            Field<FIntProperty>(F.SnapshotType,TEXT("Sequence")) &&
            Field<FBoolProperty>(F.SnapshotType,TEXT("Ready")) &&
            Field<FBoolProperty>(F.SnapshotType,TEXT("Failed")) &&
            Field<FStrProperty>(F.SnapshotType,TEXT("FailureReason")) &&
            Field<FIntProperty>(F.SnapshotType,TEXT("TotalSpawned")) &&
            Field<FIntProperty>(F.SnapshotType,TEXT("TotalDestroyed")) &&
            Field<FIntProperty>(F.SnapshotType,TEXT("HerdSize")) &&
            Field<FIntProperty>(F.CommitType,TEXT("DeliverySerial")) &&
            Field<FIntProperty>(F.CommitType,TEXT("RequestSerial")) &&
            Field<FIntProperty>(F.CommitType,TEXT("OriginGeneration")) &&
            Field<FIntProperty>(F.CommitType,TEXT("Action")) &&
            Field<FIntProperty>(F.CommitType,TEXT("BurstIssued")) &&
            Field<FIntProperty>(F.CommitType,TEXT("BurstConstructed")) &&
            Field<FIntProperty>(F.CommitType,TEXT("BurstCompleted")) &&
            Field<FObjectPropertyBase>(F.CommitType,TEXT("OriginState")) &&
            Field<FObjectPropertyBase>(F.CommitType,TEXT("Owner")) &&
            Field<FObjectPropertyBase>(F.CommitType,TEXT("OwnerPawn")) &&
            Field<FBoolProperty>(F.CadenceType,TEXT("Complete")) &&
            Field<FIntProperty>(F.CadenceType,TEXT("RequestSerial")) &&
            Field<FIntProperty>(F.CadenceType,TEXT("OriginGeneration")) &&
            Field<FIntProperty>(F.CadenceType,TEXT("ResultGeneration")) &&
            Field<FObjectPropertyBase>(F.CadenceType,TEXT("Owner")) &&
            Field<FDoubleProperty>(F.CadenceType,TEXT("SmBefore")) &&
            Field<FDoubleProperty>(F.CadenceType,TEXT("SmAfter")) &&
            Field<FDoubleProperty>(F.CadenceType,TEXT("DisplayBefore")) &&
            Field<FDoubleProperty>(F.CadenceType,TEXT("DisplayAfter")) &&
            Field<FStructProperty>(F.RecipeType,TEXT("InitialTransform")) &&
            Field<FArrayProperty>(F.RecipeType,TEXT("StationTags")) &&
            Field<FTextProperty>(F.RecipeType,TEXT("TitleText"));
        for (const TCHAR* N:{TEXT("Script"),TEXT("Ping"),TEXT("Pong"),TEXT("Condition")})
        {
            const auto* Sample=Field<FStructProperty>(F.SnapshotType,N);
            Good=Good && Sample && Sample->Struct &&
                Field<FIntProperty>(Sample->Struct,TEXT("Hits")) &&
                Field<FIntProperty>(Sample->Struct,TEXT("Misses")) &&
                Field<FIntProperty>(Sample->Struct,TEXT("Live")) &&
                Field<FIntProperty>(Sample->Struct,TEXT("Free")) &&
                Field<FIntProperty>(Sample->Struct,TEXT("InUse")) &&
                Field<FIntProperty>(Sample->Struct,TEXT("HighWater"));
        }
        for (const TCHAR* N:{TEXT("LastControlAttemptSerial"),TEXT("LastControlSerial"),
            TEXT("LastControlResult"),TEXT("LastAcknowledgedDeliverySerial"),
            TEXT("LastAcceptedBurstSerial"),TEXT("LastAcceptedBurstOriginGeneration"),
            TEXT("LastProjectionRejectedSerial"),TEXT("LastRejectedAckAttemptCount"),
            TEXT("LastRejectedAckDelivery"),TEXT("LastRejectedAckRequestSerial"),
            TEXT("LastRejectedAckReason")})
        { Good=Good && Field<FIntProperty>(F.PCClass,N); }
        Good=Good && Field<FBoolProperty>(F.PCClass,TEXT("OwnerSessionFailed"));
        for (const TCHAR* N:{TEXT("LastRestartOriginGeneration"),TEXT("LastRestartRequestSerial")})
        { Good=Good && Field<FIntProperty>(F.StateClass,N); }
        Good=Good && Field<FObjectPropertyBase>(F.StateClass,TEXT("LastRestartOwner"));
        return Test.TestTrue(TEXT("whole reflected ObjectPooling V4 public schema"),Good);
    }
    auto RecipeExact(const FFixture& F,const void* D,int32 Slot) -> bool
    {
        if (!D || Slot<0 || Slot>1) { return false; }
        const auto* Transform=Field<FStructProperty>(F.RecipeType,TEXT("InitialTransform"));
        const auto* X=Transform && Transform->Struct==TBaseStructure<FTransform>::Get()
            ? Transform->ContainerPtrToValuePtr<FTransform>(D):nullptr;
        // Preserve the authored yaw-180 recipe, accepting only its exact Iris wire form.
        const FTransform Authored(FRotator(0,180,0),FVector(500,Slot==0?-400:400,0));
        if (!X || !UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                Authored,*X)) { return false; }
        const struct { const TCHAR* N; double Expected; } Numbers[] = {
            {TEXT("Width"),6},{TEXT("Depth"),5},{TEXT("Height"),5},
            {TEXT("WallThickness"),15},{TEXT("FloorThickness"),15},{TEXT("TrimDepth"),30},
            {TEXT("AgentSpawnOffset"),0},{TEXT("TitleScale"),20},
            {TEXT("DescriptionScale"),12},{TEXT("FloorDescriptionScale"),12}};
        for (const auto& Entry:Numbers)
        { if (Dbl(F.RecipeType,D,Entry.N)!=Entry.Expected) { return false; } }
        const auto* Body=Field<FStructProperty>(F.RecipeType,TEXT("BodyColour"));
        const auto* Trim=Field<FStructProperty>(F.RecipeType,TEXT("TrimColour"));
        const auto* TitleColor=Field<FStructProperty>(F.RecipeType,TEXT("TitleColour"));
        const auto* DescriptionColor=Field<FStructProperty>(F.RecipeType,TEXT("DescriptionColour"));
        const auto* FloorColor=Field<FStructProperty>(F.RecipeType,TEXT("FloorDescriptionColour"));
        if (!Body || Body->Struct!=TBaseStructure<FLinearColor>::Get() ||
            !Trim || Trim->Struct!=TBaseStructure<FLinearColor>::Get() ||
            !TitleColor || TitleColor->Struct!=TBaseStructure<FColor>::Get() ||
            !DescriptionColor || DescriptionColor->Struct!=TBaseStructure<FColor>::Get() ||
            !FloorColor || FloorColor->Struct!=TBaseStructure<FColor>::Get()) { return false; }
        if (*Body->ContainerPtrToValuePtr<FLinearColor>(D)!=FLinearColor(.02,.02,.02,1) ||
            *Trim->ContainerPtrToValuePtr<FLinearColor>(D)!=FLinearColor(.3,.3,.3,1) ||
            *TitleColor->ContainerPtrToValuePtr<FColor>(D)!=FColor::White ||
            *DescriptionColor->ContainerPtrToValuePtr<FColor>(D)!=FColor::White ||
            *FloorColor->ContainerPtrToValuePtr<FColor>(D)!=FColor::White ||
            !Flag(F.RecipeType,D,TEXT("ShowSpotlight")) ||
            Flag(F.RecipeType,D,TEXT("ShowAnchors")) ||
            Flag(F.RecipeType,D,TEXT("ShowDebugOverlays")) ||
            Flag(F.RecipeType,D,TEXT("AutoSize"))) { return false; }
        const auto* Title=Field<FTextProperty>(F.RecipeType,TEXT("TitleText"));
        if (!Title || Title->GetPropertyValue_InContainer(D).ToString()!=Titles[Slot]) { return false; }
        const auto* TagsProp=Field<FArrayProperty>(F.RecipeType,TEXT("StationTags"));
        const auto* Description=Field<FArrayProperty>(F.RecipeType,TEXT("DescriptionText"));
        const auto* FloorDescription=Field<FArrayProperty>(F.RecipeType,TEXT("FloorDescriptionText"));
        if (!TagsProp || !CastField<FNameProperty>(TagsProp->Inner) ||
            !Description || !CastField<FTextProperty>(Description->Inner) ||
            !FloorDescription || !CastField<FTextProperty>(FloorDescription->Inner)) { return false; }
        FScriptArrayHelper TagList(TagsProp,TagsProp->ContainerPtrToValuePtr<FScriptArray>(D));
        FScriptArrayHelper DescList(Description,Description->ContainerPtrToValuePtr<FScriptArray>(D));
        FScriptArrayHelper FloorList(FloorDescription,FloorDescription->ContainerPtrToValuePtr<FScriptArray>(D));
        if (TagList.Num()!=1 || *reinterpret_cast<const FName*>(TagList.GetRawPtr(0))!=FName(Tags[Slot]) ||
            DescList.Num()!=3 || FloorList.Num()!=0) { return false; }
        const TCHAR* Lines[2][3] = {
            {TEXT("Sustained spawn/destroy waves of poolable EntityScripts."),
             TEXT("Healthy pooling: LIVE plateaus, HITS climbs, MISSES freezes."),TEXT("Warming up...")},
            {TEXT("Herd of Ping<->Pong StateMachines, slices respawned every second."),
             TEXT("SM state + condition scripts recycle continuously under load."),TEXT("Warming up...")}};
        for (int32 I=0;I<3;++I)
        { if (reinterpret_cast<const FText*>(DescList.GetRawPtr(I))->ToString()!=Lines[Slot][I]) { return false; } }
        FStructOnScope DefaultRecipe(F.RecipeType);
        if (!DefaultRecipe.GetStructMemory()) { return false; }
        for (TFieldIterator<FProperty> It(F.RecipeType);It;++It)
        {
            const auto Name=It->GetFName();
            if (Name==FName(TEXT("InitialTransform")) || Name==FName(TEXT("StationTags")) ||
                Name==FName(TEXT("TitleText")) || Name==FName(TEXT("DescriptionText"))) { continue; }
            if (!It->Identical_InContainer(D,DefaultRecipe.GetStructMemory())) { return false; }
        }
        return true;
    }
    auto Complete(const FFixture& F,const void* D) -> bool
    {
        if (!D || !Flag(F.SnapshotType,D,TEXT("Ready")) || Flag(F.SnapshotType,D,TEXT("Failed")) ||
            !Str(F.SnapshotType,D,TEXT("FailureReason")).IsEmpty() ||
            I32(F.SnapshotType,D,TEXT("CatalogVersion"))!=1 ||
            I32(F.SnapshotType,D,TEXT("Generation"))<=0 || I32(F.SnapshotType,D,TEXT("Sequence"))<=0 ||
            I32(F.SnapshotType,D,TEXT("HerdSize"))!=20 ||
            I32(F.SnapshotType,D,TEXT("TotalSpawned"))<0 ||
            I32(F.SnapshotType,D,TEXT("TotalDestroyed"))<0) { return false; }
        const auto* A=F.Recipes->ContainerPtrToValuePtr<FScriptArray>(D);
        const auto* T=F.Titles->ContainerPtrToValuePtr<FScriptArray>(D);
        const auto* B=F.Bodies->ContainerPtrToValuePtr<FScriptArray>(D);
        if (!A || !T || !B || A->Num()!=2 || T->Num()!=2 || B->Num()!=2) { return false; }
        FScriptArrayHelper TitleArray(F.Titles,const_cast<FScriptArray*>(T));
        FScriptArrayHelper BodyArray(F.Bodies,const_cast<FScriptArray*>(B));
        FScriptArrayHelper RecipeArray(F.Recipes,const_cast<FScriptArray*>(A));
        for (int32 I=0;I<2;++I)
        { if (!RecipeExact(F,RecipeArray.GetRawPtr(I),I) ||
              *reinterpret_cast<const FString*>(TitleArray.GetRawPtr(I))!=Titles[I] ||
              reinterpret_cast<const FString*>(BodyArray.GetRawPtr(I))->IsEmpty()) { return false; } }
        return true;
    }
    auto SaveSource(FFixture& F) -> void
    {
        const void* D=F.Data(0); if (!Complete(F,D)) { return; }
        const auto K=FFixture::Key(I32(F.SnapshotType,D,TEXT("Generation")),I32(F.SnapshotType,D,TEXT("Sequence")));
        if (F.AuthorityHistory.Contains(K)) { return; }
        F.AuthorityHistory.Add(K,Copy(F.SnapshotType,D)); F.AuthorityOrder.Add(K);
        if (F.AuthorityOrder.Num()>4096)
        { F.AuthorityHistory.Remove(F.AuthorityOrder[0]); F.AuthorityOrder.RemoveAt(0); }
    }
    auto SameSnapshot(const FFixture& F,const void* Source,const void* Candidate) -> bool
    {
        if (!Complete(F,Source) || !Complete(F,Candidate)) { return false; }
        // The saved authority sample remains raw. Only the recipe transform may take its
        // exact Iris wire representation; every other reflected field is identical.
        for (TFieldIterator<FProperty> It(F.SnapshotType);It;++It)
        {
            if (It->GetFName()==FName(TEXT("Stations"))) { continue; }
            if (!It->Identical_InContainer(Source,Candidate,PPF_None)) { return false; }
        }
        const auto* SourceArray=F.Recipes->ContainerPtrToValuePtr<FScriptArray>(Source);
        const auto* CandidateArray=F.Recipes->ContainerPtrToValuePtr<FScriptArray>(Candidate);
        if (!SourceArray || !CandidateArray || SourceArray->Num()!=2 || CandidateArray->Num()!=2)
        { return false; }
        FScriptArrayHelper SourceRecipes(F.Recipes,const_cast<FScriptArray*>(SourceArray));
        FScriptArrayHelper CandidateRecipes(F.Recipes,const_cast<FScriptArray*>(CandidateArray));
        const auto* Transform=Field<FStructProperty>(F.RecipeType,TEXT("InitialTransform"));
        if (!Transform || Transform->Struct!=TBaseStructure<FTransform>::Get()) { return false; }
        for (int32 I=0;I<2;++I)
        {
            const void* Authored=SourceRecipes.GetRawPtr(I);
            const void* Actual=CandidateRecipes.GetRawPtr(I);
            if (!Authored || !Actual) { return false; }
            for (TFieldIterator<FProperty> It(F.RecipeType);It;++It)
            {
                if (It->GetFName()==FName(TEXT("InitialTransform"))) { continue; }
                if (!It->Identical_InContainer(Authored,Actual,PPF_None)) { return false; }
            }
            const auto* A=Transform->ContainerPtrToValuePtr<FTransform>(Authored);
            const auto* B=Transform->ContainerPtrToValuePtr<FTransform>(Actual);
            if (!A || !B || !UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(*A,*B))
            { return false; }
        }
        return true;
    }
    auto SameSource(const FFixture& F,const void* Candidate) -> bool
    {
        if (!Complete(F,Candidate)) { return false; }
        const auto K=FFixture::Key(I32(F.SnapshotType,Candidate,TEXT("Generation")),
            I32(F.SnapshotType,Candidate,TEXT("Sequence")));
        const auto* Source=F.AuthorityHistory.Find(K);
        return Source && Source->IsValid() &&
            SameSnapshot(F,(*Source)->GetStructMemory(),Candidate);
    }
    auto Pool(const FFixture& F,int32 World,int32 Key) -> FCk_ObjectPooling_PoolStats
    { return UCk_Utils_Object_UE::Get_ObjectPoolStats(F.Worlds[World].Get(),F.PoolKeys[Key],nullptr); }
    auto PureStationSource(const FFixture& F,int32 World,TArray<FCk_Handle>& Out) -> bool
    {
        Out.Empty();
        if (World==0)
        {
            const auto* P=Field<FArrayProperty>(F.StateClass,TEXT("ServerStations"));
            const auto* S=F.State(0);
            const auto* Inner=P?CastField<FStructProperty>(P->Inner):nullptr;
            if (!P || !S || !Inner || Inner->Struct!=FCk_Handle::StaticStruct()) { return false; }
            Out=*P->ContainerPtrToValuePtr<TArray<FCk_Handle>>(S); return true;
        }
        return PureHandles(F.Local[World-1].Get(),TEXT("Get_PoolingLocalStations"),Out);
    }
    auto StationsReady(const FFixture& F,int32 World,FString* OutFailure=nullptr) -> bool
    {
        auto Reject=[OutFailure](const FString& Why) -> bool
        { if (OutFailure) { *OutFailure=Why; } return false; };
        if (OutFailure) { OutFailure->Empty(); }
        auto* W=F.Worlds[World].Get(); const auto R=Root(W);
        TArray<FCk_Handle> Stations;
        const void* Snapshot=F.Data(World);
        if (!Live(R)) { return Reject(TEXT("world root not live")); }
        if (!Complete(F,Snapshot)) { return Reject(TEXT("snapshot incomplete")); }
        if (!PureStationSource(F,World,Stations)) { return Reject(TEXT("station source getter failed")); }
        if (Stations.Num()!=2) { return Reject(FString::Printf(TEXT("station count=%d"),Stations.Num())); }
        if (Stations[0]==Stations[1]) { return Reject(TEXT("duplicate station handles")); }
        FScriptArrayHelper Recipes(F.Recipes,F.Recipes->ContainerPtrToValuePtr<FScriptArray>(Snapshot));
        if (Recipes.Num()!=2) { return Reject(FString::Printf(TEXT("recipe count=%d"),Recipes.Num())); }
        for (int32 I=0;I<2;++I)
        {
            auto Tag=Tagged(W,I);
            if (!Live(Stations[I])) { return Reject(FString::Printf(TEXT("station%d not live"),I)); }
            if (Tag.Num()!=1) { return Reject(FString::Printf(TEXT("station%d tag count=%d"),I,Tag.Num())); }
            if (Tag[0]!=Stations[I]) { return Reject(FString::Printf(TEXT("station%d tag identity"),I)); }
            auto* Script=UCk_Utils_EntityScript_UE::TryGet_EntityScript(Stations[I]);
            if (!IsValid(Script)) { return Reject(FString::Printf(TEXT("station%d script missing"),I)); }
            if (Script->GetClass()!=F.StationClass)
            { return Reject(FString::Printf(TEXT("station%d script class=%s"),I,*GetNameSafe(Script->GetClass()))); }
            const void* Recipe=Recipes.GetRawPtr(I);
            if (!Recipe) { return Reject(FString::Printf(TEXT("station%d recipe missing"),I)); }
            for (TFieldIterator<FProperty> It(F.RecipeType);It;++It)
            {
                auto* OnScript=FindFProperty<FProperty>(Script->GetClass(),It->GetFName());
                if (!OnScript) { return Reject(FString::Printf(TEXT("station%d %s script property missing"),I,*It->GetName())); }
                if (!OnScript->SameType(*It))
                { return Reject(FString::Printf(TEXT("station%d %s property type"),I,*It->GetName())); }
                const void* Authored=OnScript->ContainerPtrToValuePtr<void>(Script);
                const void* Observed=It->ContainerPtrToValuePtr<void>(Recipe);
                if (!Authored || !Observed)
                { return Reject(FString::Printf(TEXT("station%d %s property address"),I,*It->GetName())); }
                if (World>0 && It->GetFName()==TEXT("InitialTransform"))
                {
                    const auto* Transform=CastField<FStructProperty>(*It);
                    if (!Transform || Transform->Struct!=TBaseStructure<FTransform>::Get() ||
                        !UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
                            *static_cast<const FTransform*>(Authored),
                            *static_cast<const FTransform*>(Observed)))
                    { return Reject(FString::Printf(TEXT("station%d InitialTransform exact authored/Iris mismatch"),I)); }
                }
                else if (!OnScript->Identical(Authored,Observed,PPF_None))
                { return Reject(FString::Printf(TEXT("station%d %s exact property mismatch"),I,*It->GetName())); }
            }
        }
        return true;
    }
    auto SimulationReady(const FFixture& F) -> bool
    {
        if (!Complete(F,F.Data(0)) || !StationsReady(F,0)) { return false; }
        bool Ready=false;
        if (!PureBool(F.State(0),TEXT("Get_IsReady"),Ready) || !Ready) { return false; }
        FCk_Handle HerdOwner,Tick;
        TArray<FCk_Handle> Herd;
        if (!PureHandle(F.State(0),TEXT("Get_ServerHerdOwner"),HerdOwner) ||
            !PureHandle(F.State(0),TEXT("Get_ServerTickOwner"),Tick) ||
            !PureHandles(F.State(0),TEXT("Get_ServerHerd"),Herd) || Herd.Num()!=20 ||
            !Direct(HerdOwner,Root(F.Worlds[0].Get())) || !Direct(Tick,Root(F.Worlds[0].Get()))) { return false; }
        for (const auto& H:Herd)
        {
            if (!Direct(H,HerdOwner)) { return false; }
            if (!UCk_Utils_StateMachine_UE::Has(H)) { return false; }
            auto Sm=UCk_Utils_StateMachine_UE::Cast(H);
            if (!ck::IsValid(Sm) ||
                UCk_Utils_StateMachine_UE::Get_RunStatus(Sm)!=ECk_SmRunStatus::Running) { return false; }
            auto* Current=UCk_Utils_StateMachine_UE::Get_CurrentStateClass(Sm).Get();
            if (Current!=F.PingClass && Current!=F.PongClass) { return false; }
            if (!ck::IsValid(UCk_Utils_StateMachine_UE::Get_CurrentStateHandle(Sm))) { return false; }
        }
        return true;
    }
    auto PresentationReady(const FFixture& F,int32 World) -> bool
    {
        if (World<1 || World>2 || !Complete(F,F.Data(World)) || !StationsReady(F,World)) { return false; }
        auto* P=F.Local[World-1].Get();
        bool Ready=false;
        const auto Installed=PureStruct(P,TEXT("Get_InstalledPoolingSample"),F.SnapshotType);
        if (!PureBool(P,TEXT("Get_PoolingPresentationReady"),Ready) || !Ready ||
            !Installed.IsValid() || !SameSource(F,Installed->GetStructMemory())) { return false; }
        const auto* Actual=F.Data(World);
        return F.SnapshotType->CompareScriptStruct(Actual,Installed->GetStructMemory(),PPF_None);
    }
    auto ClientHasNoSimulation(const FFixture& F,int32 World) -> bool
    {
        if (World<1 || World>2) { return false; }
        for (int32 K=0;K<4;++K) { if (!PoolZero(Pool(F,World,K))) { return false; } }
        TArray<FCk_Handle> Herd,Wave,Pending;
        return PureHandles(F.State(World),TEXT("Get_ServerHerd"),Herd) && Herd.IsEmpty() &&
            PureHandles(F.State(World),TEXT("Get_ServerWave"),Wave) && Wave.IsEmpty() &&
            PureHandles(F.State(World),TEXT("Get_CurrentPendingWaveTargets"),Pending) && Pending.IsEmpty();
    }
    auto BootstrapDetail(const FFixture& F) -> FString
    {
        if (!F.Context()) { return TEXT(" context=0"); }
        FString Detail=FString::Printf(TEXT(" sim=%d source=%d reuse=%d spawned=%d destroyed=%d"),
            SimulationReady(F),Complete(F,F.Data(0)),F.SawReuse,
            I32(F.SnapshotType,F.Data(0),TEXT("TotalSpawned")),
            I32(F.SnapshotType,F.Data(0),TEXT("TotalDestroyed")));
        for (int32 W=0;W<3;++W)
        {
            TArray<FCk_Handle> Shells;
            const bool Source=PureStationSource(F,W,Shells);
            bool Ready=false;
            const bool ReadyCall=W>0 && PureBool(F.Local[W-1].Get(),TEXT("Get_PoolingPresentationReady"),Ready);
            FString StationFailure;
            const bool ExactStations=StationsReady(F,W,&StationFailure);
            Detail+=FString::Printf(TEXT(" w%d[complete=%d stations=%d reason='%s' source=%d count=%d tag0=%d tag1=%d projection=%d/%d presentation=%d noSim=%d]"),
                W,Complete(F,F.Data(W)),ExactStations,*StationFailure,Source,Shells.Num(),
                Tagged(F.Worlds[W].Get(),0).Num(),Tagged(F.Worlds[W].Get(),1).Num(),
                ReadyCall,Ready,W>0 ? PresentationReady(F,W) : 0,
                W>0 ? ClientHasNoSimulation(F,W) : 0);
        }
        return Detail;
    }
    auto PoolSourceHealthy(const FFixture& F) -> bool
    {
        for (int32 K=0;K<4;++K)
        { const auto S=Pool(F,0,K); if (!PoolHealthy(S) || S.Get_NumInUse()<=0 || S.Get_NumHits()<=0) { return false; } }
        return true;
    }
    auto ObserveReuse(FFixture& F) -> void
    {
        if (!F.Context() || !SimulationReady(F)) { return; }
        TArray<FCk_Handle> Wave;
        if (!PureHandles(F.State(0),TEXT("Get_ServerWave"),Wave)) { return; }
        auto* Sub=F.Worlds[0]->GetSubsystem<UCk_ObjectPooling_Subsystem_UE>();
        if (!IsValid(Sub)) { return; }
        if (!F.ReuseScript.IsValid() && !Allocated(F.ReuseFirstTarget))
        {
            for (const auto& H:Wave)
            {
                if (!Live(H) || !Direct(H,F.ServerStations[0])) { continue; }
                auto* Script=UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
                if (!IsValid(Script) || Script->GetClass()!=F.PoolClass ||
                    Script->GetWorld()!=F.Worlds[0].Get() || !Sub->Get_IsTrackedObject(Script)) { continue; }
                F.ReuseFirstTarget=H; F.ReuseScript=Script; break;
            }
        }
        if (F.ReuseScript.IsValid() && !Allocated(F.ReuseFirstTarget))
        {
            for (const auto& H:Wave)
            {
                if (!Live(H) || H==F.ReuseFirstTarget || !Direct(H,F.ServerStations[0])) { continue; }
                auto* Script=UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
                if (IsValid(Script) && Script==F.ReuseScript.Get() &&
                    Script->GetWorld()==F.Worlds[0].Get() && Sub->Get_IsTrackedObject(Script))
                { F.SawReuse=true; break; }
            }
        }
    }
    auto Rpc(UObject* Owner,int32 Serial,int32 Generation,int32 Action) -> bool
    {
        FCall C(Owner,TEXT("Server_RequestPoolingControl"),static_cast<EFunctionFlags>(FUNC_Net|FUNC_NetServer));
        auto* A=Field<FIntProperty>(C.Function,TEXT("Serial"));
        auto* B=Field<FIntProperty>(C.Function,TEXT("Generation"));
        auto* D=Field<FIntProperty>(C.Function,TEXT("Action"));
        const auto Plain=[](const FProperty* P) -> bool
        { return P && P->ArrayDim==1 && P->HasAnyPropertyFlags(CPF_Parm) &&
            !P->HasAnyPropertyFlags(CPF_OutParm|CPF_ReturnParm|CPF_ReferenceParm); };
        if (!Plain(A) || !Plain(B) || !Plain(D) || C.Function->NumParms!=3 ||
            C.Function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(FUNC_NetClient|FUNC_NetMulticast|FUNC_Static)))
        { return false; }
        A->SetPropertyValue_InContainer(C.Args->GetStructMemory(),Serial);
        B->SetPropertyValue_InContainer(C.Args->GetStructMemory(),Generation);
        D->SetPropertyValue_InContainer(C.Args->GetStructMemory(),Action);
        return C.Send();
    }
    auto AckRpc(UObject* Owner,UObject* State,APawn* Pawn,int32 Delivery,int32 Serial,UClass* StateClass) -> bool
    {
        FCall C(Owner,TEXT("Server_AcknowledgePoolingCommit"),static_cast<EFunctionFlags>(FUNC_Net|FUNC_NetServer));
        auto* S=Field<FObjectPropertyBase>(C.Function,TEXT("State"));
        auto* P=Field<FObjectPropertyBase>(C.Function,TEXT("Pawn"));
        auto* D=Field<FIntProperty>(C.Function,TEXT("Delivery"));
        auto* R=Field<FIntProperty>(C.Function,TEXT("Serial"));
        const auto Plain=[](const FProperty* V) -> bool
        { return V && V->ArrayDim==1 && V->HasAnyPropertyFlags(CPF_Parm) &&
            !V->HasAnyPropertyFlags(CPF_OutParm|CPF_ReturnParm|CPF_ReferenceParm); };
        if (!Plain(S) || !Plain(P) || !Plain(D) || !Plain(R) || C.Function->NumParms!=4 ||
            S->PropertyClass!=StateClass || !P->PropertyClass ||
            !P->PropertyClass->IsChildOf(APawn::StaticClass()) ||
            C.Function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(FUNC_NetClient|FUNC_NetMulticast|FUNC_Static)))
        { return false; }
        auto* Memory=C.Args->GetStructMemory();
        S->SetObjectPropertyValue_InContainer(Memory,State);
        P->SetObjectPropertyValue_InContainer(Memory,Pawn);
        D->SetPropertyValue_InContainer(Memory,Delivery);
        R->SetPropertyValue_InContainer(Memory,Serial);
        return C.Send();
    }
    auto Receipt(const FFixture& F,int32 Owner,int32 Serial,int32 Action) -> bool
    {
        if (Owner<0 || Owner>1) { return false; }
        auto* C=F.Local[Owner].Get(); auto* S=F.Remote[Owner].Get();
        const auto* Local=F.OwnerCommit(Owner,false); const auto* Authority=F.OwnerCommit(Owner,true);
        if (!IsValid(C) || !IsValid(S) || !Local || !Authority ||
            I32(F.PCClass,S,TEXT("LastControlSerial"))!=Serial ||
            I32(F.PCClass,S,TEXT("LastControlResult"))!=1 ||
            I32(F.CommitType,Authority,TEXT("RequestSerial"))!=Serial ||
            I32(F.CommitType,Authority,TEXT("Action"))!=Action ||
            I32(F.CommitType,Local,TEXT("RequestSerial"))!=Serial ||
            I32(F.CommitType,Local,TEXT("Action"))!=Action ||
            I32(F.CommitType,Local,TEXT("OriginGeneration"))!=
                I32(F.CommitType,Authority,TEXT("OriginGeneration")) ||
            I32(F.CommitType,Local,TEXT("DeliverySerial"))!=
                I32(F.CommitType,Authority,TEXT("DeliverySerial"))) { return false; }
        const int32 Delivery=I32(F.CommitType,Local,TEXT("DeliverySerial"));
        const auto Installed=PureStruct(C,TEXT("Get_InstalledPoolingCommit"),F.CommitType);
        const auto* Sample=F.CommitSnapshot->ContainerPtrToValuePtr<void>(Local);
        const auto* AuthoritySample=F.CommitSnapshot->ContainerPtrToValuePtr<void>(Authority);
        return Delivery>0 && I32(F.PCClass,S,TEXT("LastAcknowledgedDeliverySerial"))==Delivery &&
            Installed.IsValid() && F.CommitType->CompareScriptStruct(Installed->GetStructMemory(),Local,PPF_None) &&
            SameSnapshot(F,AuthoritySample,Sample) &&
            Obj(F.CommitType,Authority,TEXT("OriginState"))==F.State(0) &&
            Obj(F.CommitType,Authority,TEXT("Owner"))==S &&
            Obj(F.CommitType,Authority,TEXT("OwnerPawn"))==F.RemotePawn[Owner].Get() &&
            Obj(F.CommitType,Local,TEXT("OriginState"))==F.State(Owner+1) &&
            Obj(F.CommitType,Local,TEXT("Owner"))==C &&
            Obj(F.CommitType,Local,TEXT("OwnerPawn"))==F.LocalPawn[Owner].Get() &&
            SameSource(F,Sample);
    }
    auto BurstEvidence(const FFixture& F,int32 Owner,int32 Serial,int32 OriginG) -> bool
    {
        auto* P=F.Remote[Owner].Get(); const auto* Commit=F.OwnerCommit(Owner,true);
        TArray<FCk_Handle> Targets; TArray<int32> Constructed,Completed;
        if (!IsValid(P) || !Commit ||
            I32(F.PCClass,P,TEXT("LastAcceptedBurstSerial"))!=Serial ||
            I32(F.PCClass,P,TEXT("LastAcceptedBurstOriginGeneration"))!=OriginG ||
            I32(F.CommitType,Commit,TEXT("BurstIssued"))!=100 ||
            I32(F.CommitType,Commit,TEXT("BurstConstructed"))!=100 ||
            I32(F.CommitType,Commit,TEXT("BurstCompleted"))!=100 ||
            !PureHandles(P,TEXT("Get_LastAcceptedBurstTargets"),Targets) ||
            !PureInts(P,TEXT("Get_LastAcceptedBurstConstructed"),Constructed) ||
            !PureInts(P,TEXT("Get_LastAcceptedBurstCompleted"),Completed) ||
            Targets.Num()!=100 || Constructed.Num()!=100 || Completed.Num()!=100) { return false; }
        TSet<FCk_Handle> Unique;
        for (int32 I=0;I<100;++I)
        { if (Unique.Contains(Targets[I]) || Constructed[I]!=1 || Completed[I]!=1) { return false; }
          Unique.Add(Targets[I]); }
        return Unique.Num()==100;
    }
    auto PreAckAdmissionProof(const FFixture& F) -> bool
    {
        const auto Audit=PureStruct(F.Remote[0].Get(),TEXT("Get_LastCommitAdmissionAudit"),F.CommitAdmissionType);
        const auto* Commit=F.OwnerCommit(0,true);
        if (!Audit.IsValid() || !Commit) { return false; }
        const auto* A=Audit->GetStructMemory();
        const int32 Delivery=I32(F.CommitType,Commit,TEXT("DeliverySerial"));
        return Flag(F.CommitAdmissionType,A,TEXT("Observed")) &&
            I32(F.CommitAdmissionType,A,TEXT("RequestSerial"))==F.FirstSerial &&
            I32(F.CommitAdmissionType,A,TEXT("DeliverySerial"))==Delivery &&
            Delivery>0 && I32(F.CommitAdmissionType,A,TEXT("AcknowledgedAtPublish"))<Delivery &&
            !Flag(F.CommitAdmissionType,A,TEXT("CanAcceptAnother"));
    }
    auto RestartAdmissionProof(const FFixture& F,const TArray<FCk_Handle>& Last) -> bool
    {
        const auto Audit=PureStruct(F.State(0),TEXT("Get_LastRestartAdmissionAudit"),F.RestartAdmissionType);
        if (!Audit.IsValid()) { return false; }
        const auto* A=Audit->GetStructMemory();
        const auto* P=Field<FArrayProperty>(F.RestartAdmissionType,TEXT("Allocated"));
        const auto* Members=P?P->ContainerPtrToValuePtr<TArray<FCk_Handle>>(A):nullptr;
        if (!Flag(F.RestartAdmissionType,A,TEXT("Captured")) ||
            I32(F.RestartAdmissionType,A,TEXT("RequestSerial"))!=F.SecondSerial ||
            I32(F.RestartAdmissionType,A,TEXT("OriginGeneration"))!=F.Generation ||
            Obj(F.RestartAdmissionType,A,TEXT("Owner"))!=F.Remote[1].Get() ||
            !Members || Members->IsEmpty() || Members->Num()!=Last.Num()) { return false; }
        TSet<FCk_Handle> Set;
        for (const auto& H:*Members)
        { if (Set.Contains(H) || Allocated(H)) { return false; } Set.Add(H); }
        if (!Set.Contains(F.OldHerdOwner) || Set.Num()!=Members->Num()) { return false; }
        for (const auto& H:Last) { if (!Set.Contains(H)) { return false; } }
        return true;
    }
    auto CaptureOld(FFixture& F) -> bool
    {
        F.OldClosure.Empty();
        TArray<FCk_Handle> Roots,Herd,Wave,Pending;
        if (!PureHandles(F.State(0),TEXT("Get_CurrentSimulationTargets"),Roots) || Roots.Num()<21 ||
            !PureHandles(F.State(0),TEXT("Get_ServerHerd"),Herd) || Herd.Num()!=20 ||
            !PureHandles(F.State(0),TEXT("Get_ServerWave"),Wave) ||
            !PureHandles(F.State(0),TEXT("Get_CurrentPendingWaveTargets"),Pending) ||
            !Live(F.TickOwner) || !Live(F.ServerStations[0]) || !Live(F.ServerStations[1])) { return false; }
        const auto WorldRoot=Root(F.Worlds[0].Get());
        FCk_Handle HerdOwner;
        if (!PureHandle(F.State(0),TEXT("Get_ServerHerdOwner"),HerdOwner) ||
            !Direct(HerdOwner,WorldRoot) || !Roots.Contains(HerdOwner)) { return false; }
        F.OldHerdOwner=HerdOwner;
        for (const auto& H:Herd) { if (!Roots.Contains(H)) { return false; } }
        for (const auto& H:Wave) { if (Allocated(H) && !Roots.Contains(H)) { return false; } }
        for (const auto& H:Pending) { if (Allocated(H) && !Roots.Contains(H)) { return false; } }
        // Freeze complete independent allocated subtrees before validating any getter member.
        // The getter includes pending-kill descendants from prior herd retirements; their
        // current parent may be a nested SM state/transition/condition, not HerdOwner.
        TSet<FCk_Handle> HerdClosure,WaveClosure;
        if (!Closure(HerdOwner,WorldRoot,HerdClosure)) { return false; }
        for (const auto& H:Roots)
        {
            if (!Allocated(H) || H==F.TickOwner || H==F.ServerStations[0] || H==F.ServerStations[1] ||
                H==WorldRoot) { return false; }
            if (HerdClosure.Contains(H)) { continue; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H,WorldRoot,Children))
            { return false; }
            const auto Parent=UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H,ECk_PendingKill_Policy::IncludePendingKill);
            if (Parent==F.ServerStations[0] && !Closure(H,WorldRoot,WaveClosure)) { return false; }
        }
        for (const auto& H:Roots)
        {
            if (!HerdClosure.Contains(H) && !WaveClosure.Contains(H)) { return false; }
            const auto Parent=UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H,ECk_PendingKill_Policy::IncludePendingKill);
            const bool ExactParent=H==HerdOwner ? Parent==WorldRoot :
                Herd.Contains(H) ? Parent==HerdOwner :
                (Wave.Contains(H) || Pending.Contains(H)) ? Parent==F.ServerStations[0] :
                HerdClosure.Contains(H) ? HerdClosure.Contains(Parent) :
                WaveClosure.Contains(H) ? Parent==F.ServerStations[0] || WaveClosure.Contains(Parent) : false;
            if (!ExactParent) { return false; }
        }
        for (const auto& H:HerdClosure)
        { if (H==F.TickOwner || H==F.ServerStations[0] || H==F.ServerStations[1] || H==WorldRoot)
          { return false; } F.OldClosure.Add(H); }
        for (const auto& H:WaveClosure)
        { if (H==F.TickOwner || H==F.ServerStations[0] || H==F.ServerStations[1] || H==WorldRoot ||
              F.OldClosure.Contains(H)) { return false; }
          F.OldClosure.Add(H); }
        return F.OldClosure.Num()>=21 && !F.OldClosure.Contains(F.TickOwner) &&
            !F.OldClosure.Contains(F.ServerStations[0]) && !F.OldClosure.Contains(F.ServerStations[1]);
    }
    auto ObserveRestartBoundary(FFixture& F) -> void
    {
        if (F.SawNewReady || F.Generation<=0 || F.OldClosure.IsEmpty()) { return; }
        const void* D=F.Data(0);
        if (!D || I32(F.SnapshotType,D,TEXT("Generation"))!=F.Generation+1 ||
            !Flag(F.SnapshotType,D,TEXT("Ready"))) { return; }
        F.SawNewReady=true;
        TArray<FCk_Handle> Last;
        if (!PureHandles(F.State(0),TEXT("Get_LastRestartRetired"),Last) || Last.IsEmpty() ||
            !Gone(F.OldClosure))
        { F.SawReadyBeforeRetirementProof=true; return; }
        TSet<FCk_Handle> LastSet;
        for (const auto& H:Last)
        {
            if (LastSet.Contains(H) || Allocated(H))
            { F.SawReadyBeforeRetirementProof=true; return; }
            LastSet.Add(H);
        }
        if (!RestartAdmissionProof(F,Last))
        { F.SawReadyBeforeRetirementProof=true; return; }
        // Some pre-R handles may naturally retire before the owner RPC is admitted.
        // Every captured identity must be gone, but need not be in the R ledger.
    }
    auto RestartProof(const FFixture& F) -> bool
    {
        auto* S=F.State(0); const void* D=F.Data(0);
        FCk_Handle Tick; TArray<FCk_Handle> Current,Last;
        const auto Cadence=PureStruct(S,TEXT("Get_LastRestartCadence"),F.CadenceType);
        if (!F.SawNewReady || F.SawReadyBeforeRetirementProof ||
            !Complete(F,D) || I32(F.SnapshotType,D,TEXT("Generation"))!=F.Generation+1 ||
            !Gone(F.OldClosure) || !PureHandle(S,TEXT("Get_ServerTickOwner"),Tick) || Tick!=F.TickOwner ||
            !PureStationSource(F,0,Current) || Current.Num()!=2 ||
            Current[0]!=F.ServerStations[0] || Current[1]!=F.ServerStations[1] ||
            !PureHandles(S,TEXT("Get_LastRestartRetired"),Last) || Last.IsEmpty() || !Cadence.IsValid())
        { return false; }
        TSet<FCk_Handle> LastSet;
        for (const auto& H:Last)
        { if (LastSet.Contains(H) || Allocated(H)) { return false; } LastSet.Add(H); }
        if (LastSet.Num()!=Last.Num() || !RestartAdmissionProof(F,Last) ||
            I32(F.StateClass,S,TEXT("LastRestartOriginGeneration"))!=F.Generation ||
            I32(F.StateClass,S,TEXT("LastRestartRequestSerial"))!=F.SecondSerial ||
            Obj(F.StateClass,S,TEXT("LastRestartOwner"))!=F.Remote[1].Get()) { return false; }
        for (int32 K=0;K<4;++K)
        {
            const auto Now=Pool(F,0,K),& Before=F.BeforeRestart[K];
            if (!PoolHealthy(Now) || Now.Get_NumHits()<Before.Get_NumHits() ||
                Now.Get_NumMisses()<Before.Get_NumMisses() ||
                Now.Get_HighWaterMark()<Before.Get_HighWaterMark()) { return false; }
        }
        const void* C=Cadence->GetStructMemory();
        const auto* RCommit=F.OwnerCommit(1,true);
        const auto* RSample=RCommit && F.CommitSnapshot
            ? F.CommitSnapshot->ContainerPtrToValuePtr<void>(RCommit):nullptr;
        return Flag(F.CadenceType,C,TEXT("Complete")) &&
            I32(F.CadenceType,C,TEXT("RequestSerial"))==F.SecondSerial &&
            I32(F.CadenceType,C,TEXT("OriginGeneration"))==F.Generation &&
            I32(F.CadenceType,C,TEXT("ResultGeneration"))==F.Generation+1 &&
            Obj(F.CadenceType,C,TEXT("Owner"))==F.Remote[1].Get() &&
            Dbl(F.CadenceType,C,TEXT("SmBefore"))==Dbl(F.CadenceType,C,TEXT("SmAfter")) &&
            Dbl(F.CadenceType,C,TEXT("DisplayBefore"))==Dbl(F.CadenceType,C,TEXT("DisplayAfter")) &&
            RSample && I32(F.SnapshotType,RSample,TEXT("TotalSpawned"))==0 &&
            I32(F.SnapshotType,RSample,TEXT("TotalDestroyed"))==0 &&
            SimulationReady(F);
    }
    auto RejectedAttempt(const FFixture& F,int32 Owner,int32 Serial,int32 Accepted,
        int32 ExpectedResult,int32 ExpectedG,const TSharedPtr<FStructOnScope>& Frozen) -> bool
    {
        auto* P=F.Remote[Owner].Get(); const auto* C=F.OwnerCommit(Owner,true);
        return IsValid(P) && C && Frozen.IsValid() &&
            I32(F.PCClass,P,TEXT("LastControlAttemptSerial"))==Serial &&
            I32(F.PCClass,P,TEXT("LastControlSerial"))==Accepted &&
            I32(F.PCClass,P,TEXT("LastControlResult"))==ExpectedResult &&
            I32(F.SnapshotType,F.Data(0),TEXT("Generation"))==ExpectedG &&
            F.CommitType->CompareScriptStruct(C,Frozen->GetStructMemory(),PPF_None);
    }
    auto CaptureLocalFailure(FFixture& F) -> bool
    {
        F.FailedLocalClosure.Empty();
        TArray<FCk_Handle> Shells;
        if (!PureHandles(F.Local[0].Get(),TEXT("Get_PoolingLocalStations"),Shells) ||
            Shells.Num()!=2 || Shells[0]==Shells[1]) { return false; }
        const auto R=Root(F.Worlds[1].Get());
        for (const auto& H:Shells)
        { if (!Live(H) || !Closure(H,R,F.FailedLocalClosure)) { return false; } }
        if (F.FailedLocalClosure.Num()<2) { return false; }
        auto Destroy=Shells[0]; UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Destroy);
        return true;
    }
    auto CaptureSourceFailure(FFixture& F) -> bool
    {
        F.TerminalClosure.Empty();
        auto* W=F.Worlds[0].Get(); const auto R=Root(W);
        FCk_Handle Tick,HerdOwner; TArray<FCk_Handle> Roots;
        if (!Live(R) || !PureHandle(F.State(0),TEXT("Get_ServerTickOwner"),Tick) ||
            !PureHandle(F.State(0),TEXT("Get_ServerHerdOwner"),HerdOwner) ||
            !PureHandles(F.State(0),TEXT("Get_CurrentSimulationTargets"),Roots) || Roots.Num()<21 ||
            !Closure(F.ServerStations[0],R,F.TerminalClosure) ||
            !Closure(F.ServerStations[1],R,F.TerminalClosure) ||
            !Closure(Tick,R,F.TerminalClosure) || !Closure(HerdOwner,R,F.TerminalClosure)) { return false; }
        for (const auto& H:Roots) { if (!Closure(H,R,F.TerminalClosure)) { return false; } }
        if (F.TerminalClosure.Num()<24 || !F.TerminalClosure.Contains(F.ServerStations[0]) ||
            !F.TerminalClosure.Contains(F.ServerStations[1]) || !F.TerminalClosure.Contains(Tick)) { return false; }
        auto Destroy=F.ServerStations[0]; UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Destroy);
        return true;
    }
    struct FStep { FString Name; TFunction<bool()> Check; bool Wait=false; bool Bootstrap=false; };
    class FScenario final : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test; TSharedRef<FFixture> F; TArray<FStep> Steps; int32 Index=0; double Began=-1;
    public:
        FScenario(FAutomationTestBase* T,TSharedRef<FFixture> InF,TArray<FStep>&& InSteps)
            :Test(T),F(InF),Steps(MoveTemp(InSteps)){}
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed() || Index>=Steps.Num()) { return true; }
            if (Began<0) { Began=FPlatformTime::Seconds(); }
            auto& Step=Steps[Index];
            const bool CanRun=Step.Bootstrap ? Index==0 : F->Context();
            if (CanRun && Step.Check()) { ++Index; Began=-1; return Index>=Steps.Num(); }
            if (Step.Wait && FPlatformTime::Seconds()-Began<45) { return false; }
            FString Why=TEXT("ObjectPooling network phase failed: ")+Step.Name;
            Why+=FString::Printf(TEXT(" [phase=%d g=%d q=%d old=%d gone=%d reuse=%d]"),Index,
                I32(F->SnapshotType,F->Data(0),TEXT("Generation")),
                I32(F->SnapshotType,F->Data(0),TEXT("Sequence")),F->OldClosure.Num(),
                Gone(F->OldClosure),F->SawReuse);
            if (Index==2) { Why+=BootstrapDetail(*F); }
            if (Index==6)
            { FString Detail;
              if (PureString(F->State(0),TEXT("Get_LastRestartAdmissionReject"),Detail))
              { Why+=FString::Printf(TEXT(" [admission=%s]"),*Detail); }
              else { Why+=TEXT(" [admission getter/schema rejected]"); } }
            Test->AddError(Why); ck::auto_test::net::FailSession(Why); return true;
        }
    };
    class FRestore final : public IAutomationLatentCommand
    { TSharedRef<FFixture> F; public: explicit FRestore(TSharedRef<FFixture> InF):F(InF){}
      bool Update() override { F->Restore(); return true; } };

    class FVerifyTeardown final : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test; TSharedRef<FFixture> F;
    public:
        FVerifyTeardown(FAutomationTestBase* InTest,TSharedRef<FFixture> InF):Test(InTest),F(InF){}
        bool Update() override
        {
            const auto Active=ck::auto_test::net::Get_AllPIEWorlds();
            for (const auto& W:F->Worlds)
            { if (W.IsValid() && Active.Contains(W.Get()))
              { Test->AddError(TEXT("ObjectPooling PIE world remained active after EndPIE")); return true; } }
            for (const auto& Actor:F->ActorCarriers)
            { if (Actor.IsValid() && Active.Contains(Actor->GetWorld()))
              { Test->AddError(TEXT("ObjectPooling fixture actor carrier remained in active PIE world")); return true; } }
            return true;
        }
    };
    auto Run(FAutomationTestBase& Test,const FString& Topology,bool RejectUnknownChild=false) -> bool
    {
        auto F=MakeShared<FFixture>();
        if (!ck::auto_test::net::TryGet_DedicatedServer(Topology,F->Dedicated)) { return false; }
        F->Mode=FSoftClassPath(TEXT("/Script/Angelscript.Ck_ObjectPoolingGym_GameMode")).TryLoadClass<AGameModeBase>();
        F->StateClass=FSoftClassPath(TEXT("/Script/Angelscript.Ck_ObjectPoolingGym_GameState")).TryLoadClass<AGameStateBase>();
        F->PCClass=FSoftClassPath(TEXT("/Script/Angelscript.Ck_ObjectPoolingGym_PlayerController")).TryLoadClass<APlayerController>();
        F->StationClass=FSoftClassPath(TEXT("/Script/Angelscript.Ck_EntityScript_GymStation")).TryLoadClass<UCk_EntityScript_UE>();
        F->PoolClass=FSoftClassPath(TEXT("/Script/Angelscript.Ck_ObjectPoolingTest_PoolableScript")).TryLoadClass<UCk_EntityScript_UE>();
        F->PingClass=FSoftClassPath(TEXT("/Script/Angelscript.Ck_PoolSmGym_State_Ping")).TryLoadClass<UObject>();
        F->PongClass=FSoftClassPath(TEXT("/Script/Angelscript.Ck_PoolSmGym_State_Pong")).TryLoadClass<UObject>();
        F->ConditionClass=FSoftClassPath(TEXT("/Script/Angelscript.Ck_PoolSmTest_Condition_ShortDelay")).TryLoadClass<UObject>();
        F->PoolKeys[0]=F->PoolClass; F->PoolKeys[1]=F->PingClass;
        F->PoolKeys[2]=F->PongClass; F->PoolKeys[3]=F->ConditionClass;
        if (!Test.TestTrue(TEXT("real ObjectPooling AS classes, four pool keys and reflected V5 schema"),
            F->Mode && F->StateClass && F->PCClass && F->StationClass &&
            F->PoolClass && F->PingClass && F->PongClass && F->ConditionClass && Schema(*F,Test)))
        { return false; }
        const auto* Settings=GetDefault<UCkGym_StartupSettings>();
        F->Startup=Settings->StartupMode; F->Last=Settings->LastGymName; F->Recents=Settings->RecentGymNames;
        GetMutableDefault<UCkGym_StartupSettings>()->StartupMode=ECkGym_StartupMode::Cycler;
        Test.AddCommand(new FCk_Latent_StartPIEMultiClient(3,Map,Topology,F->Mode));
        Test.AddCommand(new FCk_Latent_WaitForPIEReady(3,60));
        TArray<FStep> Steps;
        auto Act=[&](FString N,TFunction<bool()> C){Steps.Add({MoveTemp(N),MoveTemp(C),false});};
        auto Wait=[&](FString N,TFunction<bool()> C){Steps.Add({MoveTemp(N),MoveTemp(C),true});};
        Steps.Add({TEXT("exact two-remote-owner three-world topology and shared authority ready"),[F]()
        {
            F->Worlds[0]=ck::auto_test::net::Get_ServerWorld();
            F->Worlds[1]=ck::auto_test::net::Get_ClientWorld(0);
            F->Worlds[2]=ck::auto_test::net::Get_ClientWorld(1);
            return F->Context() && SimulationReady(*F);
        },true,true});
        Act(TEXT("capture exact actual world/state/owner/pawn contexts and post-tick authority history"),[F]()
        {
            for (int32 I=0;I<3;++I) { F->States[I]=F->State(I); }
            for (int32 I=0;I<2;++I)
            {
                F->Local[I]=F->Worlds[I+1]->GetFirstPlayerController();
                F->Remote[I]=ck::auto_test::net::TryGet_RemoteClientPlayerController(F->Worlds[0].Get(),I);
                if (!F->Local[I].IsValid() || !F->Remote[I].IsValid()) { return false; }
                F->LocalPawn[I]=F->Local[I]->GetPawn(); F->RemotePawn[I]=F->Remote[I]->GetPawn();
            }
            if (!F->Dedicated)
            {
                auto* GI=F->Worlds[0]->GetGameInstance();
                if (!IsValid(GI) || GI->GetLocalPlayers().Num()!=1) { return false; }
                F->Host=GI->GetLocalPlayers()[0]->GetPlayerController(F->Worlds[0].Get());
                F->HostPawn=F->Host.IsValid()?F->Host->GetPawn():nullptr;
            }
            F->Captured=true;
            if (!F->Context()) { return false; }
            // Full actor-carrier ledger: server mode/state, owner PCs/pawns, and listen host.
            F->ActorCarriers.Add(F->Worlds[0]->GetAuthGameMode());
            for (int32 I=0;I<3;++I) { F->ActorCarriers.Add(F->State(I)); }
            for (int32 I=0;I<2;++I)
            { F->ActorCarriers.Add(F->Local[I].Get()); F->ActorCarriers.Add(F->Remote[I].Get());
              F->ActorCarriers.Add(F->LocalPawn[I].Get()); F->ActorCarriers.Add(F->RemotePawn[I].Get()); }
            if (!F->Dedicated) { F->ActorCarriers.Add(F->Host.Get()); F->ActorCarriers.Add(F->HostPawn.Get()); }
            F->Observer=FWorldDelegates::OnWorldPostActorTick.AddLambda([F](UWorld* W,ELevelTick,float)
            {
                if (!F->Context()) { return; }
                if (W==F->Worlds[0].Get())
                {
                    ++F->ServerPostTicks;
                    SaveSource(*F); ObserveReuse(*F);
                    ObserveRestartBoundary(*F);
                    if (!F->OldClosure.IsEmpty() && Gone(F->OldClosure) &&
                        I32(F->SnapshotType,F->Data(0),TEXT("Generation"))==F->Generation &&
                        !Flag(F->SnapshotType,F->Data(0),TEXT("Ready")))
                    { F->SawOldGoneBeforeReady=true; }
                }
            });
            return F->Observer.IsValid();
        });
        Wait(TEXT("two original stations, actual running herd/churn, all four authority pools, two source-matched owner shells"),[F]()
        {
            if (!F->Context() || !SimulationReady(*F) || !StationsReady(*F,0) ||
                !StationsReady(*F,1) || !StationsReady(*F,2)) { return false; }
            SaveSource(*F);
            TArray<FCk_Handle> Authority;
            if (!PureStationSource(*F,0,Authority) || Authority.Num()!=2) { return false; }
            F->ServerStations[0]=Authority[0]; F->ServerStations[1]=Authority[1];
            // These identities were captured from the actual typed spawn
            // requests during admission, before their processor removes that type.
            TArray<FCk_Handle> Carriers, CarrierTargets;
            if (!PureHandles(F->State(0),TEXT("Get_StartupSpawnCarriers"),Carriers) ||
                !PureHandles(F->State(0),TEXT("Get_StartupCarrierTargets"),CarrierTargets) ||
                Carriers.Num()!=2 || CarrierTargets.Num()!=2 ||
                Carriers[0]==FCk_Handle{} || Carriers[1]==FCk_Handle{} ||
                Carriers[0]==Carriers[1] || Allocated(Carriers[0]) || Allocated(Carriers[1]) ||
                CarrierTargets[0]!=Authority[0] || CarrierTargets[1]!=Authority[1])
            { return false; }
            TSet<FCk_Handle> StationClosure;
            if (!Closure(Authority[0],Root(F->Worlds[0].Get()),StationClosure) ||
                StationClosure.Contains(Carriers[0]) || StationClosure.Contains(Carriers[1]))
            { return false; }
            if (!PureHandle(F->State(0),TEXT("Get_ServerTickOwner"),F->TickOwner) ||
                !Direct(F->TickOwner,Root(F->Worlds[0].Get()))) { return false; }
            for (int32 I=0;I<2;++I)
            { if (!PresentationReady(*F,I+1) || !ClientHasNoSimulation(*F,I+1)) { return false; } }
            if (!F->Dedicated)
            {
                TArray<FCk_Handle> HostShells; bool HostReady=false;
                const auto HostSample=PureStruct(F->Host.Get(),TEXT("Get_InstalledPoolingSample"),F->SnapshotType);
                if (!PureHandles(F->Host.Get(),TEXT("Get_PoolingLocalStations"),HostShells) ||
                    !HostShells.IsEmpty() || !PureBool(F->Host.Get(),TEXT("Get_PoolingPresentationReady"),HostReady) ||
                    !HostReady || !HostSample.IsValid() || !SameSource(*F,HostSample->GetStructMemory()))
                { return false; }
            }
            return PoolSourceHealthy(*F) && F->SawReuse &&
                I32(F->SnapshotType,F->Data(0),TEXT("TotalSpawned"))>110 &&
                I32(F->SnapshotType,F->Data(0),TEXT("TotalDestroyed"))>0;
        });
        Act(TEXT("startup carrier queries reject malformed and foreign inputs without mutation"),[F]()
        {
            if (!F->Context() || !Live(F->ServerStations[0])) { return false; }
            const auto WorldRoot=Root(F->Worlds[0].Get());
            const auto Owner=UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(F->ServerStations[0]);
            TSet<FCk_Handle> Before,After;
            if (!Closure(WorldRoot,WorldRoot,Before)) { return false; }
            const auto BeforeSnapshot=Copy(F->SnapshotType,F->Data(0));
            if (!BeforeSnapshot.IsValid()) { return false; }
            FCk_Handle Output=F->ServerStations[0];
            // Aliased input/output must be copied before rejected output is cleared.
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::TryCapture_StationSpawnCarrier(
                    Output,Owner,{},WorldRoot,Output) || Output!=FCk_Handle{}) { return false; }
            bool IsAllocated=true;
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrier(
                    {},F->ServerStations[0],Owner,WorldRoot,IsAllocated) || IsAllocated)
            { return false; }
            IsAllocated=true;
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrier(
                    Root(F->Worlds[1].Get()),F->ServerStations[0],Owner,WorldRoot,IsAllocated) || IsAllocated)
            { return false; }
            if (!Closure(WorldRoot,WorldRoot,After) || Before.Num()!=After.Num() ||
                !F->SnapshotType->CompareScriptStruct(BeforeSnapshot->GetStructMemory(),F->Data(0),0))
            { return false; }
            for (const auto& H:Before) { if (!After.Contains(H)) { return false; } }
            return true;
        });
        if (RejectUnknownChild)
        {
            Act(TEXT("create a real unknown allocated station0 leaf before owner R"),[F]()
            {
                if (!F->Context() || !SimulationReady(*F) ||
                    !Live(F->ServerStations[0]) ||
                    !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(F->ServerStations[0]) ||
                    Allocated(F->UnknownChild)) { return false; }
                // SharedStationCohort owns both stations through its cohort
                // entity, which is itself directly owned by the real world root.
                const auto CohortOwner=UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(F->ServerStations[0]);
                if (!Direct(CohortOwner,Root(F->Worlds[0].Get())) ||
                    !Direct(F->ServerStations[0],CohortOwner) ||
                    !Direct(F->ServerStations[1],CohortOwner)) { return false; }
                F->Generation=I32(F->SnapshotType,F->Data(0),TEXT("Generation"));
                if (F->Generation<=0 ||
                    !PureHandle(F->State(0),TEXT("Get_ServerTickOwner"),F->TickOwner)) { return false; }
                F->UnknownChild=UCk_Utils_EntityLifetime_UE::Request_CreateEntity(F->ServerStations[0]);
                TArray<FCk_Handle> Children;
                if (!Live(F->UnknownChild) || !Direct(F->UnknownChild,F->ServerStations[0]) ||
                    !UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                        F->ServerStations[0],Root(F->Worlds[0].Get()),Children) ||
                    !Children.Contains(F->UnknownChild)) { return false; }
                return Rpc(F->Local[1].Get(),F->SecondSerial,F->Generation,1);
            });
            Wait(TEXT("R independently censuses unknown leaf and rejects before destroy"),[F]()
            {
                if (!F->Context()) { return false; }
                const auto* D=F->Data(0);
                if (!D || !Flag(F->SnapshotType,D,TEXT("Failed")) ||
                    Flag(F->SnapshotType,D,TEXT("Ready")) ||
                    Str(F->SnapshotType,D,TEXT("FailureReason"))!=TEXT("ObjectPooling allocated simulation closure unproven") ||
                    I32(F->SnapshotType,D,TEXT("Generation"))!=F->Generation ||
                    !Allocated(F->UnknownChild) || !Direct(F->UnknownChild,F->ServerStations[0]))
                { return false; }
                const auto Audit=PureStruct(F->State(0),TEXT("Get_LastRestartAdmissionAudit"),F->RestartAdmissionType);
                const auto* A=Audit.IsValid()?Audit->GetStructMemory():nullptr;
                const auto* P=Field<FArrayProperty>(F->RestartAdmissionType,TEXT("Allocated"));
                const auto* Members=A && P?P->ContainerPtrToValuePtr<TArray<FCk_Handle>>(A):nullptr;
                if (!A || !Members || !Flag(F->RestartAdmissionType,A,TEXT("Captured")) ||
                    I32(F->RestartAdmissionType,A,TEXT("RequestSerial"))!=F->SecondSerial ||
                    I32(F->RestartAdmissionType,A,TEXT("OriginGeneration"))!=F->Generation ||
                    Obj(F->RestartAdmissionType,A,TEXT("Owner"))!=F->Remote[1].Get() ||
                    Members->Num()<22 || !Members->Contains(F->UnknownChild)) { return false; }
                TSet<FCk_Handle> Unique;
                for (const auto& H:*Members)
                { if (Unique.Contains(H)) { return false; } Unique.Add(H); }
                const auto* Commit=F->OwnerCommit(1,true);
                TArray<FCk_Handle> Retired;
                if (Unique.Num()!=Members->Num() || !Commit ||
                    I32(F->PCClass,F->Remote[1].Get(),TEXT("LastControlAttemptSerial"))!=F->SecondSerial ||
                    I32(F->PCClass,F->Remote[1].Get(),TEXT("LastControlSerial"))!=0 ||
                    I32(F->PCClass,F->Remote[1].Get(),TEXT("LastControlResult"))!=3 ||
                    I32(F->CommitType,Commit,TEXT("DeliverySerial"))!=0 ||
                    I32(F->CommitType,Commit,TEXT("RequestSerial"))!=0 ||
                    I32(F->StateClass,F->State(0),TEXT("LastRestartRequestSerial"))!=0 ||
                    Obj(F->StateClass,F->State(0),TEXT("LastRestartOwner"))!=nullptr ||
                    !PureHandles(F->State(0),TEXT("Get_LastRestartRetired"),Retired) || !Retired.IsEmpty())
                { return false; }
                FCk_Handle Tick; TArray<FCk_Handle> Stations;
                if (!PureHandle(F->State(0),TEXT("Get_ServerTickOwner"),Tick) || Tick!=F->TickOwner ||
                    !PureStationSource(*F,0,Stations) || Stations.Num()!=2 ||
                    Stations[0]!=F->ServerStations[0] || Stations[1]!=F->ServerStations[1])
                { return false; }
                F->FailedAtPostTick=F->ServerPostTicks;
                F->FailedSpawned=I32(F->SnapshotType,D,TEXT("TotalSpawned"));
                F->FailedDestroyed=I32(F->SnapshotType,D,TEXT("TotalDestroyed"));
                return F->FailedSpawned>=0 && F->FailedDestroyed>=0;
            });
            Wait(TEXT("failed source stops producers and cannot publish partial restart"),[F]()
            {
                const auto* D=F->Data(0);
                if (!F->Context() || F->ServerPostTicks<F->FailedAtPostTick+3 || !D ||
                    !Flag(F->SnapshotType,D,TEXT("Failed")) || Flag(F->SnapshotType,D,TEXT("Ready")) ||
                    I32(F->SnapshotType,D,TEXT("Generation"))!=F->Generation ||
                    I32(F->SnapshotType,D,TEXT("TotalSpawned"))!=F->FailedSpawned ||
                    I32(F->SnapshotType,D,TEXT("TotalDestroyed"))!=F->FailedDestroyed ||
                    !Allocated(F->UnknownChild) || !Direct(F->UnknownChild,F->ServerStations[0]) ||
                    I32(F->PCClass,F->Remote[1].Get(),TEXT("LastControlSerial"))!=0 ||
                    I32(F->StateClass,F->State(0),TEXT("LastRestartRequestSerial"))!=0)
                { return false; }
                bool Ready=true;
                return PureBool(F->State(0),TEXT("Get_IsReady"),Ready) && !Ready;
            });
            Act(TEXT("destroy only fixture-owned unknown leaf with public lifetime API"),[F]()
            {
                if (!Allocated(F->UnknownChild) || !Direct(F->UnknownChild,F->ServerStations[0]))
                { return false; }
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(F->UnknownChild);
                return true;
            });
            Wait(TEXT("fixture-owned unknown leaf has deallocated"),[F]()
            { return !Allocated(F->UnknownChild); });
        }
        else
        {
        Act(TEXT("owner zero real client B control against current authority generation"),[F]()
        {
            F->Generation=I32(F->SnapshotType,F->Data(0),TEXT("Generation"));
            F->FirstSpawned=I32(F->SnapshotType,F->Data(0),TEXT("TotalSpawned"));
            F->FirstDestroyed=I32(F->SnapshotType,F->Data(0),TEXT("TotalDestroyed"));
            return F->Generation>0 && Rpc(F->Local[0].Get(),F->FirstSerial,F->Generation,0);
        });
        Wait(TEXT("authority B retains 100 constructed and completed targets with durable pre-ACK admission proof"),[F]()
        {
            SaveSource(*F);
            const auto* Held=F->OwnerCommit(0,true);
            if (!PreAckAdmissionProof(*F) || !Held ||
                !BurstEvidence(*F,0,F->FirstSerial,F->Generation) ||
                I32(F->PCClass,F->Remote[0].Get(),TEXT("LastControlSerial"))!=F->FirstSerial ||
                I32(F->CommitType,Held,TEXT("RequestSerial"))!=F->FirstSerial ||
                I32(F->CommitType,Held,TEXT("DeliverySerial"))<=0 ||
                I32(F->SnapshotType,F->Data(0),TEXT("TotalSpawned"))<F->FirstSpawned+100 ||
                !SimulationReady(*F)) { return false; }
            F->FirstCommit=Copy(F->CommitType,Held);
            return F->FirstCommit.IsValid();
        });
        Act(TEXT("capture all actual old allocated simulation roots before owner one R"),[F]()
        { if (!CaptureOld(*F)) { return false; }
          for (int32 K=0;K<4;++K) { F->BeforeRestart[K]=Pool(*F,0,K); }
          return Rpc(F->Local[1].Get(),F->SecondSerial,F->Generation,1); });
        Wait(TEXT("R keeps stations/tick/cadence and cumulative pools, rebuilds running herd, preserves peer B receipt"),[F]()
        {
            SaveSource(*F);
            const auto* Held=F->OwnerCommit(0,true);
            if (!PreAckAdmissionProof(*F) || !RestartProof(*F) ||
                !Receipt(*F,0,F->FirstSerial,0) || !Receipt(*F,1,F->SecondSerial,1) ||
                !F->FirstCommit.IsValid() || !Held ||
                !F->CommitType->CompareScriptStruct(Held,F->FirstCommit->GetStructMemory(),PPF_None) ||
                !BurstEvidence(*F,0,F->FirstSerial,F->Generation) ||
                !PresentationReady(*F,1) || !PresentationReady(*F,2)) { return false; }
            for (int32 K=0;K<4;++K)
            { if (!PoolHealthy(Pool(*F,0,K)) || !PoolZero(Pool(*F,1,K)) || !PoolZero(Pool(*F,2,K)))
              { return false; } }
            return true;
        });
        Act(TEXT("send a real owning-client stale ACK after accepted R ACK"),[F]()
        {
            const auto* C=F->OwnerCommit(1,false);
            if (!C || !Receipt(*F,1,F->SecondSerial,1)) { return false; }
            F->AckBefore=I32(F->PCClass,F->Remote[1].Get(),TEXT("LastRejectedAckAttemptCount"));
            F->AckDelivery=I32(F->CommitType,C,TEXT("DeliverySerial"));
            F->AckSerial=I32(F->CommitType,C,TEXT("RequestSerial"));
            return F->AckBefore>=0 && F->AckDelivery>0 && F->AckSerial==F->SecondSerial &&
                AckRpc(F->Local[1].Get(),F->State(2),F->LocalPawn[1].Get(),
                    F->AckDelivery,F->AckSerial,F->StateClass);
        });
        Wait(TEXT("server receives stale ACK and records reason 2 without replacing the accepted delivery"),[F]()
        {
            const auto* C=F->OwnerCommit(1,true);
            return C && I32(F->PCClass,F->Remote[1].Get(),TEXT("LastRejectedAckAttemptCount"))==F->AckBefore+1 &&
                I32(F->PCClass,F->Remote[1].Get(),TEXT("LastRejectedAckDelivery"))==F->AckDelivery &&
                I32(F->PCClass,F->Remote[1].Get(),TEXT("LastRejectedAckRequestSerial"))==F->AckSerial &&
                I32(F->PCClass,F->Remote[1].Get(),TEXT("LastRejectedAckReason"))==2 &&
                I32(F->PCClass,F->Remote[1].Get(),TEXT("LastAcknowledgedDeliverySerial"))==F->AckDelivery &&
                I32(F->CommitType,C,TEXT("RequestSerial"))==F->SecondSerial;
        });
        Act(TEXT("malformed action uses real owner RPC without admitting another control"),[F]()
        { return Rpc(F->Local[0].Get(),F->RejectedSerial,F->Generation+1,7); });
        Wait(TEXT("malformed action rejected with original owner B receipt and R generation intact"),[F]()
        { return RejectedAttempt(*F,0,F->RejectedSerial,F->FirstSerial,3,F->Generation+1,F->FirstCommit) &&
            SimulationReady(*F); });
        Act(TEXT("stale generation uses a newer real owner RPC serial"),[F]()
        { return Rpc(F->Local[0].Get(),F->RejectedSerial+1,F->Generation,0); });
        Wait(TEXT("stale generation rejected without a new burst or restart"),[F]()
        { return RejectedAttempt(*F,0,F->RejectedSerial+1,F->FirstSerial,3,F->Generation+1,F->FirstCommit) &&
            I32(F->PCClass,F->Remote[0].Get(),TEXT("LastAcceptedBurstSerial"))==F->FirstSerial &&
            I32(F->StateClass,F->State(0),TEXT("LastRestartRequestSerial"))==F->SecondSerial; });
        Act(TEXT("destroy one actual owner-zero local station closure"),[F]()
        { F->BeforeLocalFailureSequence=I32(F->SnapshotType,F->Data(0),TEXT("Sequence"));
          return F->BeforeLocalFailureSequence>0 && CaptureLocalFailure(*F); });
        Wait(TEXT("only owner zero terminal projection retires both shells; peer and shared source remain healthy"),[F]()
        {
            bool LocalReady=true,PeerReady=false,SourceReady=false;
            if (!Gone(F->FailedLocalClosure) ||
                !PureBool(F->Local[0].Get(),TEXT("Get_PoolingPresentationReady"),LocalReady) || LocalReady ||
                !Flag(F->PCClass,F->Remote[0].Get(),TEXT("OwnerProjectionRetired")) ||
                !Flag(F->PCClass,F->Local[0].Get(),TEXT("OwnerProjectionRetired")) ||
                !PureBool(F->Local[1].Get(),TEXT("Get_PoolingPresentationReady"),PeerReady) || !PeerReady ||
                !PureBool(F->State(0),TEXT("Get_IsReady"),SourceReady) || !SourceReady ||
                I32(F->SnapshotType,F->Data(0),TEXT("Sequence"))<=F->BeforeLocalFailureSequence ||
                !SimulationReady(*F)) { return false; }
            return true;
        });
        Act(TEXT("failed owner's newer actual control RPC is rejected before admission"),[F]()
        { return Rpc(F->Local[0].Get(),F->RejectedSerial+2,F->Generation+1,1); });
        Wait(TEXT("projection-retired owner cannot alter attempt, receipt, restart or peer/shared state"),[F]()
        {
            const auto* C=F->OwnerCommit(0,true);
            return C && F->FirstCommit.IsValid() &&
                I32(F->PCClass,F->Remote[0].Get(),TEXT("LastProjectionRejectedSerial"))==F->RejectedSerial+2 &&
                I32(F->PCClass,F->Remote[0].Get(),TEXT("LastControlAttemptSerial"))==F->RejectedSerial+1 &&
                I32(F->PCClass,F->Remote[0].Get(),TEXT("LastControlSerial"))==F->FirstSerial &&
                F->CommitType->CompareScriptStruct(C,F->FirstCommit->GetStructMemory(),PPF_None) &&
                I32(F->StateClass,F->State(0),TEXT("LastRestartRequestSerial"))==F->SecondSerial &&
                I32(F->SnapshotType,F->Data(0),TEXT("Generation"))==F->Generation+1 &&
                PresentationReady(*F,2) && SimulationReady(*F);
        });
        Act(TEXT("destroy actual required authority station after capturing the whole allocated closure"),[F]()
        { return CaptureSourceFailure(*F); });
        Wait(TEXT("required shared child loss withdraws every source and retires both owner presentations"),[F]()
        {
            if (!Gone(F->TerminalClosure) || !Gone(F->FailedLocalClosure)) { return false; }
            bool Unproven=true,OtherReady=true;
            if (!PureBool(F->State(0),TEXT("Get_IsRetirementUnproven"),Unproven) || Unproven ||
                !Flag(F->SnapshotType,F->Data(0),TEXT("Failed")) ||
                Flag(F->SnapshotType,F->Data(0),TEXT("Ready")) ||
                !Flag(F->SnapshotType,F->Data(1),TEXT("Failed")) ||
                !Flag(F->SnapshotType,F->Data(2),TEXT("Failed")) ||
                !PureBool(F->Local[1].Get(),TEXT("Get_PoolingPresentationReady"),OtherReady) || OtherReady)
            { return false; }
            TArray<FCk_Handle> PeerShells;
            if (!PureHandles(F->Local[1].Get(),TEXT("Get_PoolingLocalStations"),PeerShells) ||
                PeerShells.Num()!=2 || Allocated(PeerShells[0]) || Allocated(PeerShells[1])) { return false; }
            for (int32 K=0;K<4;++K)
            { const auto S=Pool(*F,0,K); if (!PoolHealthy(S) || S.Get_NumInUse()!=0 ||
                !PoolZero(Pool(*F,1,K)) || !PoolZero(Pool(*F,2,K))) { return false; } }
            return true;
        });
        }
        Test.AddCommand(new FScenario(&Test,F,MoveTemp(Steps)));
        Test.AddCommand(new FRestore(F));
        Test.AddCommand(new FCk_Latent_EndPIE());
        Test.AddCommand(new FVerifyTeardown(&Test,F));
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkObjectPoolingGym_SharedWorld_Network,
    "Ck.ObjectPoolingGym.Network.SharedWorld",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCkObjectPoolingGym_SharedWorld_Network::RunTest(const FString& Parameters)
{ return ck_object_pooling_shared_network::Run(*this,Parameters); }

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkObjectPoolingGym_RestartAdmissionRejectsUnknownChild,
    "Ck.ObjectPoolingGym.Network.RestartAdmissionRejectsUnknownChild",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCkObjectPoolingGym_RestartAdmissionRejectsUnknownChild::RunTest(const FString& Parameters)
{ return ck_object_pooling_shared_network::Run(*this,Parameters,true); }
#endif
