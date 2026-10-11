#include "CoreMinimal.h"
#include "CoreGlobals.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkGym_StartupSettings.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkGym_WireTransformRuntime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include <cmath>

namespace ck_transform_shared_network
{
    constexpr int32 Slots = 6;
    constexpr auto Map = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr const TCHAR* Tags[Slots] = {TEXT("Gym.Transform.SetLocation"), TEXT("Gym.Transform.SetRotation"),
        TEXT("Gym.Transform.SetScale"), TEXT("Gym.Transform.AddLocationOffset"),
        TEXT("Gym.Transform.AddRotationOffset"), TEXT("Gym.Transform.DirectionalVectors")};

    template<class T> auto Field(const UStruct* S, const TCHAR* N) -> T*
    { return S ? FindFProperty<T>(S, N) : nullptr; }
    auto I32(const UStruct* S, const void* D, const TCHAR* N) -> int32
    { const auto* P = Field<FIntProperty>(S, N); return D && P ? P->GetPropertyValue_InContainer(D) : INDEX_NONE; }
    auto Flag(const UStruct* S, const void* D, const TCHAR* N) -> bool
    { const auto* P = Field<FBoolProperty>(S, N); return D && P && P->GetPropertyValue_InContainer(D); }
    auto Obj(const UStruct* S, const void* D, const TCHAR* N) -> UObject*
    { const auto* P = Field<FObjectPropertyBase>(S, N); return D && P ? P->GetObjectPropertyValue_InContainer(D) : nullptr; }
    auto Str(const UStruct* S, const void* D, const TCHAR* N) -> FString
    { const auto* P = Field<FStrProperty>(S, N); return D && P ? P->GetPropertyValue_InContainer(D) : FString{}; }
    auto EnumOrdinal(const UStruct* S,const void* D,const TCHAR* N) -> int32
    {
        if (!D) { return INDEX_NONE; }
        if (const auto* P=Field<FByteProperty>(S,N)) { return P->GetPropertyValue_InContainer(D); }
        const auto* E=Field<FEnumProperty>(S,N);
        return E && E->GetUnderlyingProperty()
            ? int32(E->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(E->ContainerPtrToValuePtr<void>(D)))
            : INDEX_NONE;
    }
    auto Live(const FCk_Handle& H) -> bool
    { return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    auto Root(UWorld* W) -> FCk_Handle
    { return IsValid(W) ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W) : FCk_Handle{}; }

    struct FCall
    {
        UObject* Target = nullptr; UFunction* Function = nullptr; TUniquePtr<FStructOnScope> Args;
        FCall(UObject* T, const TCHAR* N, EFunctionFlags Flags)
        {
            if (!IsValid(T)) { return; }
            auto* F = T->FindFunction(N);
            if (!IsValid(F) || !F->HasAllFunctionFlags(Flags)) { return; }
            Target = T; Function = F; Args = MakeUnique<FStructOnScope>(F);
        }
        bool Send() const
        { if (!IsValid(Target) || !IsValid(Function) || !Args) { return false; } Target->ProcessEvent(Function, Args->GetStructMemory()); return true; }
    };
    auto PureBool(UObject* T, const TCHAR* N, bool& Out) -> bool
    {
        FCall C(T, N, FUNC_BlueprintPure);
        auto* R = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        if (!R || C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = R->GetPropertyValue_InContainer(C.Args->GetStructMemory()); return true;
    }
    auto PureInt(UObject* T, const TCHAR* N, int32& Out) -> bool
    {
        FCall C(T, N, FUNC_BlueprintPure);
        auto* R = Field<FIntProperty>(C.Function, TEXT("ReturnValue"));
        if (!R || C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = R->GetPropertyValue_InContainer(C.Args->GetStructMemory()); return true;
    }
    auto Copy(const UScriptStruct* Type, const void* Data) -> TSharedPtr<FStructOnScope>
    {
        if (!IsValid(Type) || !Data) { return {}; }
        auto Value = MakeShared<FStructOnScope>(Type);
        Type->CopyScriptStruct(Value->GetStructMemory(), Data); return Value;
    }
    auto PureStruct(UObject* T, const TCHAR* N, const UScriptStruct* Type) -> TSharedPtr<FStructOnScope>
    {
        FCall C(T, N, FUNC_BlueprintPure);
        const auto* R = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        return R && R->Struct == Type && C.Function->NumParms == 1 && C.Send()
            ? Copy(Type, R->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory())) : nullptr;
    }
    auto PureHandles(UObject* T, const TCHAR* N, TArray<FCk_Handle>& Out) -> bool
    {
        FCall C(T, N, FUNC_BlueprintPure);
        const auto* R = Field<FArrayProperty>(C.Function, TEXT("ReturnValue"));
        const auto* Inner = R ? CastField<FStructProperty>(R->Inner) : nullptr;
        if (!Inner || Inner->Struct != FCk_Handle::StaticStruct() || C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = *R->ContainerPtrToValuePtr<TArray<FCk_Handle>>(C.Args->GetStructMemory()); return true;
    }
    auto Rpc(UObject* Owner, int32 Serial, int32 Generation) -> bool
    {
        FCall C(Owner, TEXT("Server_RequestTransformRebuild"), static_cast<EFunctionFlags>(FUNC_Net | FUNC_NetServer));
        auto* A = Field<FIntProperty>(C.Function, TEXT("InSerial"));
        auto* B = Field<FIntProperty>(C.Function, TEXT("InGeneration"));
        if (!A || !B || C.Function->NumParms != 2 ||
            C.Function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(FUNC_NetClient | FUNC_NetMulticast | FUNC_Static)) ||
            A->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm | CPF_ReferenceParm) ||
            B->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm | CPF_ReferenceParm)) { return false; }
        A->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Serial);
        B->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Generation);
        return C.Send();
    }
    auto CapturedClosure(const FCk_Handle& RootHandle, const FCk_Handle& WorldRoot, TSet<FCk_Handle>& Out) -> bool
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(RootHandle) || Out.Contains(RootHandle)) { return true; }
        Out.Add(RootHandle);
        TArray<FCk_Handle> Children;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(RootHandle, WorldRoot, Children)) { return false; }
        for (const auto& Child : Children) { if (!CapturedClosure(Child, WorldRoot, Out)) { return false; } }
        return true;
    }
    auto Retired(const TSet<FCk_Handle>& Handles) -> bool
    { for (const auto& H : Handles) { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return false; } } return true; }

    struct FFixture
    {
        UClass *Mode = nullptr, *StateClass = nullptr, *PCClass = nullptr, *CubeClass = nullptr, *StationClass = nullptr;
        FStructProperty *Snapshot = nullptr, *Commit = nullptr, *CommitSnapshot = nullptr;
        UScriptStruct *SnapshotType = nullptr, *CommitType = nullptr, *RowType = nullptr;
        FArrayProperty *Rows = nullptr, *Stations = nullptr;
        TWeakObjectPtr<UWorld> Worlds[3]; TWeakObjectPtr<AGameStateBase> States[3];
        TWeakObjectPtr<APlayerController> HostLocal;
        TWeakObjectPtr<APawn> HostPawn;
        TWeakObjectPtr<APlayerController> Local[2], Remote[2];
        TWeakObjectPtr<APawn> LocalPawn[2], RemotePawn[2];
        TMap<uint64, TSharedPtr<FStructOnScope>> History[3];
        TArray<uint64> Order[3];
        TSet<FCk_Handle> OldClosure[3]; TSet<TWeakObjectPtr<AActor>> OldActors[3];
        TSharedPtr<FStructOnScope> FirstRetained;
        FDelegateHandle Observer;
        int32 InitialGeneration = 0, FirstSerial = 101, SecondSerial = 202;
        bool Dedicated = false, Captured = false, Requested = false;
        bool FirstReplacementUsableObserved[3]{}, FirstReplacementUsableOldGone[3]{};
        bool PrematureReplacementUsability[3]{}, BoundarySchemaFailed = false;
        FString BoundarySchemaFailure;
        bool SawInstalledBeforeAck[2]{}; bool SawNonUnitScale[3]{}; bool SawDirectEcsSource = false;
        bool ForeignAttempted = false, ForeignRejected = false;
        FString SnapshotValidationFailure;
        int32 VisualGeneration[3]{}; FTransform FirstVisual[3][Slots];
        bool FirstVisualRecorded[3][Slots]{}, SawVisualMovement[3][Slots]{};
        int32 AuthorityTransformGeneration = 0;
        TArray<FTransform> AuthorityTransformHistory[Slots];
        bool SawAppliedTransform[3][Slots]{}, SawAuthorityTransformMatch[3][Slots]{};
        uint64 LastAuthorityMatchFrame[3][Slots]{};
        uint8 AuthorityMatchFrames[3][Slots]{};
        ECkGym_StartupMode Startup = ECkGym_StartupMode::Cycler; FString Last; TArray<FString> Recents;
        static uint64 Key(int32 G, int32 Q) { return (uint64(uint32(G)) << 32) | uint32(Q); }
        AGameStateBase* State(int32 I) const
        { auto* W = Worlds[I].Get(); auto* S = IsValid(W) ? W->GetGameState() : nullptr;
          return IsValid(S) && S->GetClass() == StateClass && S->GetWorld() == W ? S : nullptr; }
        const void* Data(int32 I) const
        { auto* S = State(I); return S && Snapshot ? Snapshot->ContainerPtrToValuePtr<void>(S) : nullptr; }
        const void* OwnerCommit(int32 I, bool ServerSide) const
        { auto* P = ServerSide ? Remote[I].Get() : Local[I].Get(); return IsValid(P) && P->GetClass() == PCClass && Commit ? Commit->ContainerPtrToValuePtr<void>(P) : nullptr; }
        bool Context() const
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto All = ck::auto_test::net::Get_AllPIEWorlds();
            auto* Client0=ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1=ck::auto_test::net::Get_ClientWorld(1);
            if (!IsValid(Server) || !IsValid(Client0) || !IsValid(Client1) ||
                Server==Client0 || Server==Client1 || Client0==Client1 ||
                All.Num() != 3 || !All.Contains(Server) || !All.Contains(Client0) || !All.Contains(Client1) ||
                ck::auto_test::net::Get_NumClientWorlds() != 2 ||
                Server->GetNetMode() != (Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
                Server->GetAuthGameMode() == nullptr || Server->GetAuthGameMode()->GetClass() != Mode ||
                !Server->HasBegunPlay() || !IsValid(State(0)) ||
                (Captured && (Worlds[0].Get() != Server || States[0].Get() != State(0)))) { return false; }
            auto* ServerGI=Server->GetGameInstance();
            if (!IsValid(ServerGI)) { return false; }
            APlayerController* Host=nullptr;
            if (Dedicated)
            {
                if (!ServerGI->GetLocalPlayers().IsEmpty() || (Captured && (HostLocal.IsValid() || HostPawn.IsValid())))
                { return false; }
            }
            else
            {
                if (ServerGI->GetLocalPlayers().Num()!=1) { return false; }
                auto* CandidateLP=ServerGI->GetLocalPlayers()[0];
                Host=IsValid(CandidateLP) ? CandidateLP->GetPlayerController(Server) : nullptr;
                auto* HostLP=IsValid(Host) ? Host->GetLocalPlayer() : nullptr;
                APawn* HostP=IsValid(Host) ? Host->GetPawn() : nullptr;
                if (!IsValid(Host) || Host->GetClass()!=PCClass || !Host->IsLocalController() ||
                    !Host->HasAuthority() || Host->GetWorld()!=Server || !IsValid(HostP) ||
                    HostP->GetWorld()!=Server || HostP->GetController()!=Host ||
                    ServerGI->GetWorld()!=Server || ServerGI->GetLocalPlayers().Num()!=1 ||
                    !IsValid(HostLP) || HostLP->GetWorld()!=Server || HostLP->GetPlayerController(Server)!=Host ||
                    (Captured && (HostLocal.Get()!=Host || HostPawn.Get()!=HostP))) { return false; }
            }
            auto* FirstLocal=Client0->GetFirstPlayerController();
            auto* FirstRemote=ck::auto_test::net::TryGet_RemoteClientPlayerController(Server,0);
            if (!IsValid(FirstLocal) || !IsValid(FirstRemote) ||
                !IsValid(FirstLocal->GetPawn()) || !IsValid(FirstRemote->GetPawn())) { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                auto* W = I==0 ? Client0 : Client1;
                auto* P = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
                auto* R = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
                auto* GI = IsValid(W) ? W->GetGameInstance() : nullptr;
                auto* LP = IsValid(P) ? P->GetLocalPlayer() : nullptr;
                if (!IsValid(W) || W == Server || !All.Contains(W) || W->GetNetMode() != NM_Client || !W->HasBegunPlay() ||
                    !IsValid(P) || !IsValid(R) || P->GetClass() != PCClass || R->GetClass() != PCClass ||
                    !P->IsLocalController() || P->GetWorld()!=W || !R->HasAuthority() || R->GetWorld() != Server ||
                    !IsValid(P->GetPawn()) || !IsValid(R->GetPawn()) || !IsValid(GI) || !IsValid(LP) ||
                    P->GetPawn()->GetWorld()!=W || P->GetPawn()->GetController()!=P ||
                    R->GetPawn()->GetWorld()!=Server || R->GetPawn()->GetController()!=R ||
                    GI->GetWorld() != W || GI->GetLocalPlayers().Num() != 1 || LP->GetWorld() != W ||
                    LP->GetPlayerController(W) != P || !IsValid(State(I+1)) ||
                    (Captured && (Worlds[I+1].Get() != W || States[I+1].Get() != State(I+1) ||
                        Local[I].Get() != P || Remote[I].Get() != R ||
                        LocalPawn[I].Get() != P->GetPawn() || RemotePawn[I].Get() != R->GetPawn()))) { return false; }
                if (I==1 && (P==FirstLocal || R==FirstRemote ||
                    P->GetPawn()==FirstLocal->GetPawn() || R->GetPawn()==FirstRemote->GetPawn()))
                { return false; }
                if (!Dedicated && (R==Host || R->GetPawn()==Host->GetPawn()))
                { return false; }
            }
            return true;
        }
        void Restore()
        {
            if (Observer.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(Observer); Observer.Reset(); }
            auto* S = GetMutableDefault<UCkGym_StartupSettings>();
            S->StartupMode = Startup; S->LastGymName = Last; S->RecentGymNames = Recents; S->SaveConfig();
        }
    };

    auto Schema(FFixture& F, FAutomationTestBase& Test) -> bool
    {
        auto Reject = [&Test](const UStruct* Type, const TCHAR* Name, const TCHAR* Expected) -> bool
        {
            const auto* Property = Field<FProperty>(Type, Name);
            const FString Actual = Property ? Property->GetCPPType() : TEXT("<missing>");
            Test.AddError(FString::Printf(TEXT("Transform schema: %s.%s expected %s, actual %s"),
                *GetNameSafe(Type), Name, Expected, *Actual));
            return false;
        };
        F.Snapshot = Field<FStructProperty>(F.StateClass, TEXT("Snapshot"));
        F.Commit = Field<FStructProperty>(F.PCClass, TEXT("LastControlCommit"));
        if (!F.Snapshot || !F.Commit || !F.Snapshot->HasAnyPropertyFlags(CPF_Net) ||
            !F.Commit->HasAnyPropertyFlags(CPF_Net)) { return Reject(F.StateClass, TEXT("Snapshot"), TEXT("replicated Snapshot and owner LastControlCommit structs")); }
        F.SnapshotType = F.Snapshot->Struct.Get(); F.CommitType = F.Commit->Struct.Get();
        if (!IsValid(F.SnapshotType) || !IsValid(F.CommitType) ||
            F.SnapshotType->GetName()!=TEXT("CkTransformGym_Snapshot") ||
            F.CommitType->GetName()!=TEXT("CkTransformGym_ControlCommit"))
        { return Reject(F.PCClass, TEXT("LastControlCommit"), TEXT("CkTransformGym_ControlCommit with CkTransformGym_Snapshot")); }
        F.CommitSnapshot = Field<FStructProperty>(F.CommitType, TEXT("Snapshot"));
        F.Rows = Field<FArrayProperty>(F.SnapshotType, TEXT("Rows"));
        F.Stations = Field<FArrayProperty>(F.SnapshotType, TEXT("Stations"));
        const auto* Row = F.Rows ? CastField<FStructProperty>(F.Rows->Inner) : nullptr;
        F.RowType = Row ? Row->Struct.Get() : nullptr;
        const auto* Station = F.Stations ? CastField<FStructProperty>(F.Stations->Inner) : nullptr;
        if (!F.CommitSnapshot || F.CommitSnapshot->Struct != F.SnapshotType || !IsValid(F.RowType) ||
            !Station || !IsValid(Station->Struct) || Station->Struct->GetName() != TEXT("Ck_GymStation_SpawnParams") ||
            F.RowType->GetName() != TEXT("CkTransformGym_Row"))
        { return Reject(F.SnapshotType, TEXT("Rows"), TEXT("CkTransformGym_Row array, Ck_GymStation_SpawnParams Stations array, exact commit Snapshot type")); }
        for (auto N : {TEXT("CatalogVersion"), TEXT("Generation"), TEXT("Sequence")})
        { if (!Field<FIntProperty>(F.SnapshotType, N)) { return Reject(F.SnapshotType, N, TEXT("int32")); } }
        for (auto N : {TEXT("Ready"), TEXT("Failed")}) { if (!Field<FBoolProperty>(F.SnapshotType, N)) { return Reject(F.SnapshotType, N, TEXT("bool")); } }
        if (!Field<FStrProperty>(F.SnapshotType, TEXT("FailureReason"))) { return Reject(F.SnapshotType, TEXT("FailureReason"), TEXT("FString")); }
        for (auto N : {TEXT("StationTitles"), TEXT("StationBodies"), TEXT("StationInstructions")})
        { const auto* A = Field<FArrayProperty>(F.SnapshotType, N); if (!A || !CastField<FStrProperty>(A->Inner)) { return Reject(F.SnapshotType, N, TEXT("FString array")); } }
        for (auto N : {TEXT("Slot"), TEXT("Generation"), TEXT("Corner"), TEXT("RotationStep")})
        { if (!Field<FIntProperty>(F.RowType, N)) { return Reject(F.RowType, N, TEXT("int32")); } }
        for (auto N : {TEXT("Elapsed"), TEXT("CornerDistance"), TEXT("ScaleSpeed"), TEXT("MinScale"),
            TEXT("MaxScale"), TEXT("CircleRadius"), TEXT("CircleSpeed"), TEXT("RotationSpeed"), TEXT("VectorLength")})
        { if (!Field<FDoubleProperty>(F.RowType, N)) { return Reject(F.RowType, N, TEXT("double (AngelScript float)")); } }
        for (auto N : {TEXT("Origin"), TEXT("Forward"), TEXT("Right"), TEXT("Up"), TEXT("Transform")})
        { if (Field<FProperty>(F.RowType, N)) { return Reject(F.RowType, N, TEXT("absent: native Transform owns pose replication")); } }
        if (Field<FProperty>(F.CubeClass, TEXT("SharedVisualScale")) ||
            F.CubeClass->FindFunctionByName(TEXT("Request_PublishCommittedScale")))
        { return Reject(F.CubeClass, TEXT("SharedVisualScale"), TEXT("absent: native Scale replication")); }
        if (!Field<FObjectPropertyBase>(F.RowType, TEXT("Cube")) ||
            (!Field<FEnumProperty>(F.RowType, TEXT("Behavior")) &&
             !Field<FByteProperty>(F.RowType, TEXT("Behavior"))))
        { return Reject(F.RowType, TEXT("Behavior"), TEXT("enum or byte, with object Cube")); }
        for (auto N : {TEXT("DeliverySerial"), TEXT("RequestSerial"), TEXT("OriginGeneration")})
        { if (!Field<FIntProperty>(F.CommitType, N)) { return Reject(F.CommitType, N, TEXT("int32")); } }
        for (auto N : {TEXT("OriginState"), TEXT("Owner"), TEXT("OwnerPawn")})
        { if (!Field<FObjectPropertyBase>(F.CommitType, N)) { return Reject(F.CommitType, N, TEXT("object")); } }
        for (auto N : {TEXT("LastControlAttemptSerial"), TEXT("LastControlSerial"), TEXT("LastControlResult"), TEXT("LastAcknowledgedDeliverySerial")})
        { if (!Field<FIntProperty>(F.PCClass, N)) { return Reject(F.PCClass, N, TEXT("int32")); } }
        if (!Field<FBoolProperty>(F.PCClass, TEXT("OwnerSessionFailed")))
        { return Reject(F.PCClass, TEXT("OwnerSessionFailed"), TEXT("bool")); }
        for (auto N : {TEXT("ServerStations"), TEXT("ServerCubes")})
        { if (!Field<FArrayProperty>(F.StateClass, N)) { return Reject(F.StateClass, N, TEXT("array")); } }
        for (auto N : {TEXT("SharedSlot"), TEXT("SharedGeneration")})
        { if (!Field<FIntProperty>(F.CubeClass, N)) { return Reject(F.CubeClass, N, TEXT("int32")); } }
        const auto CheckPureBool = [&Reject](UClass* Class, const TCHAR* Name) -> bool
        {
            auto* Function = Class ? Class->FindFunctionByName(FName(Name)) : nullptr;
            auto* Result = Field<FBoolProperty>(Function, TEXT("ReturnValue"));
            return Function && Function->HasAllFunctionFlags(FUNC_BlueprintPure) &&
                Function->NumParms == 1 && Result && Result->HasAnyPropertyFlags(CPF_ReturnParm)
                ? true : Reject(Class, Name, TEXT("BlueprintPure bool()"));
        };
        if (!CheckPureBool(F.StateClass, TEXT("Get_IsReady")) ||
            !CheckPureBool(F.PCClass, TEXT("Get_TransformPresentationReady"))) { return false; }
        auto* Installed = F.PCClass->FindFunctionByName(TEXT("Get_InstalledTransformSample"));
        auto* InstalledResult = Field<FStructProperty>(Installed, TEXT("ReturnValue"));
        if (!Installed || !Installed->HasAllFunctionFlags(FUNC_BlueprintPure) ||
            Installed->NumParms != 1 || !InstalledResult ||
            !InstalledResult->HasAnyPropertyFlags(CPF_ReturnParm) ||
            InstalledResult->Struct != F.SnapshotType)
        { return Reject(F.PCClass, TEXT("Get_InstalledTransformSample"), TEXT("BlueprintPure CkTransformGym_Snapshot()")); }
        auto* Publication = F.PCClass->FindFunctionByName(TEXT("Get_PublishedTransformSourceAudit"));
        auto* PublicationResult = Field<FStructProperty>(Publication, TEXT("ReturnValue"));
        const auto* PublicationStorage = Field<FStructProperty>(F.PCClass,TEXT("_PublishedSourceAudit"));
        if (!Publication || !Publication->HasAllFunctionFlags(FUNC_BlueprintPure) ||
            Publication->NumParms != 1 || !PublicationResult ||
            !PublicationResult->HasAnyPropertyFlags(CPF_ReturnParm) ||
            PublicationResult->Struct != F.CommitType || !PublicationStorage ||
            PublicationStorage->Struct != F.CommitType || PublicationStorage->HasAnyPropertyFlags(CPF_Net))
        { return Reject(F.PCClass, TEXT("Get_PublishedTransformSourceAudit"), TEXT("BlueprintPure retained nonreplicated CkTransformGym_ControlCommit()")); }
        auto* Validate = F.StateClass->FindFunctionByName(TEXT("Get_IsCompleteSnapshot"));
        auto* Input = Field<FStructProperty>(Validate, TEXT("S"));
        auto* Output = Field<FBoolProperty>(Validate, TEXT("ReturnValue"));
        if (!Validate || !Validate->HasAllFunctionFlags(FUNC_BlueprintPure) ||
            Validate->NumParms != 2 || !Input || Input->Struct != F.SnapshotType ||
            !Input->HasAnyPropertyFlags(CPF_Parm) ||
            Input->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm | CPF_ReturnParm) ||
            !Output || !Output->HasAnyPropertyFlags(CPF_ReturnParm))
        { return Reject(F.StateClass, TEXT("Get_IsCompleteSnapshot"), TEXT("BlueprintPure bool(CkTransformGym_Snapshot S)")); }
        auto* LedgerFn = F.StateClass->FindFunctionByName(TEXT("Get_CapturedBridgeLedger"));
        auto* LedgerResult = Field<FArrayProperty>(LedgerFn, TEXT("ReturnValue"));
        const auto* LedgerInner = LedgerResult ? CastField<FStructProperty>(LedgerResult->Inner) : nullptr;
        if (!LedgerFn || !LedgerFn->HasAllFunctionFlags(FUNC_BlueprintPure) ||
            LedgerFn->NumParms != 1 || !LedgerResult || !LedgerResult->HasAnyPropertyFlags(CPF_ReturnParm) ||
            !LedgerInner || LedgerInner->Struct != FCk_Handle::StaticStruct())
        { return Reject(F.StateClass, TEXT("Get_CapturedBridgeLedger"), TEXT("BlueprintPure FCk_Handle array()")); }
        return true;
    }

    auto Complete(const FFixture& F, int32 World, const void* D) -> bool
    {
        auto* State = F.State(World);
        FCall C(State, TEXT("Get_IsCompleteSnapshot"), FUNC_BlueprintPure);
        auto* Arg = Field<FStructProperty>(C.Function, TEXT("S"));
        auto* Ret = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        if (!Arg || Arg->Struct != F.SnapshotType || !Ret || C.Function->NumParms != 2 || !D) { return false; }
        Arg->CopyCompleteValue(Arg->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory()), D);
        return C.Send() && Ret->GetPropertyValue_InContainer(C.Args->GetStructMemory());
    }
    auto Ready(const FFixture& F, int32 I) -> bool
    { return Complete(F,I,F.Data(I)) && Flag(F.SnapshotType,F.Data(I),TEXT("Ready")) && !Flag(F.SnapshotType,F.Data(I),TEXT("Failed")); }
    auto Row(const FFixture& F, const void* D, int32 Slot) -> const void*
    {
        if (!D || !F.Rows || Slot < 0 || Slot >= Slots) { return nullptr; }
        FScriptArrayHelper A(F.Rows, F.Rows->ContainerPtrToValuePtr<void>(D));
        return A.Num() == Slots ? A.GetRawPtr(Slot) : nullptr;
    }
    auto CountArray(const UStruct* T, const void* D, const TCHAR* N) -> int32
    {
        const auto* P = Field<FArrayProperty>(T,N); if (!P || !D) { return INDEX_NONE; }
        FScriptArrayHelper A(P,P->ContainerPtrToValuePtr<void>(D)); return A.Num();
    }
    auto RowFloat(const UStruct* T,const void* D,const TCHAR* N) -> double
    { const auto* P=Field<FDoubleProperty>(T,N); return D && P ? P->GetPropertyValue_InContainer(D) : -1.0; }
    auto TextAt(const UStruct* T,const void* D,const TCHAR* N,int32 Slot) -> FString
    {
        const auto* P=Field<FArrayProperty>(T,N);
        if (!D || !P || !CastField<FStrProperty>(P->Inner)) { return {}; }
        FScriptArrayHelper A(P,P->ContainerPtrToValuePtr<void>(D));
        return A.Num()==Slots ? *reinterpret_cast<const FString*>(A.GetRawPtr(Slot)) : FString{};
    }
    auto AuthoredValues(const FFixture& F,int32 World) -> bool
    {
        const void* D=F.Data(World);
        if (!D || CountArray(F.SnapshotType,D,TEXT("StationTitles"))!=Slots ||
            CountArray(F.SnapshotType,D,TEXT("StationBodies"))!=Slots ||
            CountArray(F.SnapshotType,D,TEXT("StationInstructions"))!=Slots ||
            CountArray(F.SnapshotType,D,TEXT("Stations"))!=Slots) { return false; }
        FScriptArrayHelper Params(F.Stations,F.Stations->ContainerPtrToValuePtr<void>(D));
        const auto* PType=CastField<FStructProperty>(F.Stations->Inner);
        const auto* TransformField=PType ? Field<FStructProperty>(PType->Struct,TEXT("InitialTransform")) : nullptr;
        if (!TransformField || TransformField->Struct!=TBaseStructure<FTransform>::Get()) { return false; }
        for (int32 Slot=0;Slot<Slots;++Slot)
        {
            const auto* R=Row(F,D,Slot);
            const auto* Transform=TransformField->ContainerPtrToValuePtr<FTransform>(Params.GetRawPtr(Slot));
            if (!R || !Transform || !Transform->IsValid() ||
                (World==0 && (!Transform->GetTranslation().Equals(FVector(500,-2000+800*Slot,0),0) ||
                 !Transform->GetRotation().Equals(FRotator(0,180,0).Quaternion(),0))) ||
                TextAt(F.SnapshotType,D,TEXT("StationTitles"),Slot).IsEmpty() ||
                TextAt(F.SnapshotType,D,TEXT("StationBodies"),Slot).IsEmpty() ||
                TextAt(F.SnapshotType,D,TEXT("StationInstructions"),Slot).IsEmpty() ||
                RowFloat(F.RowType,R,TEXT("CornerDistance"))!=100 ||
                RowFloat(F.RowType,R,TEXT("ScaleSpeed"))!=1.5f ||
                RowFloat(F.RowType,R,TEXT("MinScale"))!=0.5f ||
                RowFloat(F.RowType,R,TEXT("MaxScale"))!=2 ||
                RowFloat(F.RowType,R,TEXT("CircleRadius"))!=100 ||
                RowFloat(F.RowType,R,TEXT("CircleSpeed"))!=1 ||
                RowFloat(F.RowType,R,TEXT("RotationSpeed"))!=(Slot==5?20:45) ||
                RowFloat(F.RowType,R,TEXT("VectorLength"))!=150) { return false; }
        }
        return true;
    }
    auto StationHandles(const FFixture& F, int32 I, TArray<FCk_Handle>& Out) -> bool
    {
        const auto* A = Field<FArrayProperty>(F.StateClass,TEXT("ServerStations"));
        const auto* Inner = A ? CastField<FStructProperty>(A->Inner) : nullptr;
        auto* State = F.State(I);
        if (!Inner || Inner->Struct != FCk_Handle::StaticStruct() || !State) { return false; }
        Out = *A->ContainerPtrToValuePtr<TArray<FCk_Handle>>(State); return true;
    }
    auto Cubes(const FFixture& F, int32 I) -> TArray<AActor*>
    {
        TArray<AActor*> Result; auto* W = F.Worlds[I].Get();
        if (!IsValid(W) || !IsValid(F.CubeClass)) { return Result; }
        for (TActorIterator<AActor> It(W,F.CubeClass); It; ++It) { Result.Add(*It); }
        return Result;
    }
    auto Tagged(UWorld* W, int32 Slot) -> TArray<FCk_Handle>
    {
        const auto H = Root(W);
        return Live(H) && Slot >= 0 && Slot < Slots
            ? UCk_Utils_EntityTag_UE::ForEach_Entity(H,FName(Tags[Slot])) : TArray<FCk_Handle>{};
    }
    auto Bridge(AActor* Cube) -> FCk_Handle
    {
        FCall C(Cube,TEXT("Get_SharedBridge"),FUNC_BlueprintPure);
        const auto* Ret=Field<FStructProperty>(C.Function,TEXT("ReturnValue"));
        return Ret && Ret->Struct==FCk_Handle::StaticStruct() && C.Send()
            ? *Ret->ContainerPtrToValuePtr<FCk_Handle>(C.Args->GetStructMemory()) : FCk_Handle{};
    }
    auto TransformDriver(const FCk_Handle& H) -> UCk_Fragment_EntityReplicationDriver_Rep*
    {
        return Live(H) && H.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>()
            ? H.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get() : nullptr;
    }
    auto HasNativeTransformEntries(UCk_Fragment_EntityReplicationDriver_Rep* Driver,bool Applied) -> bool
    {
        if (!IsValid(Driver)) { return false; }
        for (auto* Type : {FCk_RepData_Location::StaticStruct(),FCk_RepData_Rotation::StaticStruct(),
            FCk_RepData_Scale::StaticStruct()})
        {
            const auto* Entry=Driver->FindEntry(Type);
            if (!Entry || !Entry->Data.IsValid() ||
                (Applied && (Entry->_PendingApply || !Entry->_WasEverApplied || !Entry->_LastAppliedData.IsValid())))
            { return false; }
        }
        return true;
    }
    auto HasSixActual(const FFixture& F, int32 World) -> bool
    {
        if (!Ready(F,World) || !AuthoredValues(F,World) || CountArray(F.SnapshotType,F.Data(World),TEXT("Stations")) != Slots ||
            CountArray(F.SnapshotType,F.Data(World),TEXT("Rows")) != Slots || Cubes(F,World).Num() != Slots) { return false; }
        const int32 G = I32(F.SnapshotType,F.Data(World),TEXT("Generation"));
        TSet<AActor*> Seen;
        for (int32 Slot=0; Slot<Slots; ++Slot)
        {
            const void* R=Row(F,F.Data(World),Slot);
            auto* Cube=Cast<AActor>(Obj(F.RowType,R,TEXT("Cube")));
            const auto Tag=Tagged(F.Worlds[World].Get(),Slot);
            if (!R || I32(F.RowType,R,TEXT("Slot"))!=Slot || I32(F.RowType,R,TEXT("Generation"))!=G ||
                !IsValid(Cube) || Cube->GetClass()!=F.CubeClass || Cube->GetWorld()!=F.Worlds[World].Get() ||
                !Cube->GetActorTransform().IsValid() || Cube->IsReplicatingMovement() ||
                !IsValid(Cube->GetRootComponent()) ||
                Cube->GetRootComponent()->Mobility!=EComponentMobility::Movable ||
                !Cubes(F,World).Contains(Cube) || Seen.Contains(Cube) ||
                I32(F.CubeClass,Cube,TEXT("SharedSlot"))!=Slot ||
                I32(F.CubeClass,Cube,TEXT("SharedGeneration"))!=G ||
                EnumOrdinal(F.RowType,R,TEXT("Behavior"))!=Slot ||
                EnumOrdinal(F.CubeClass,Cube,TEXT("Behavior"))!=Slot ||
                Tag.Num()!=1 || !Live(Tag[0])) { return false; }
            const auto H=Bridge(Cube);
            auto* Driver=TransformDriver(H);
            if (!Live(H) || !UCk_Utils_Transform_UE::Has(H) ||
                !H.Has<ck::FFragment_Transform>() || !IsValid(Driver) ||
                Driver->Get_AssociatedEntity()!=H || Driver->GetOwningActor()!=Cube ||
                !HasNativeTransformEntries(Driver,false)) { return false; }
            if (World==0 && (!H.Has<ck::FFragment_ContainerRef_Location>() ||
                !H.Has<ck::FFragment_ContainerRef_Rotation>() || !H.Has<ck::FFragment_ContainerRef_Scale>()))
            { return false; }
            Seen.Add(Cube);
            if (World==0)
            {
                TArray<FCk_Handle> ServerStations;
                if (!StationHandles(F,0,ServerStations) || ServerStations.Num()!=Slots ||
                    Tag[0]!=ServerStations[Slot]) { return false; }
            }
        }
        return Seen.Num()==Slots;
    }
    auto NoClientSimulationTimers(const FFixture& F, int32 World) -> bool
    {
        if (World==0 || !HasSixActual(F,World)) { return false; }
        const auto WorldRoot=Root(F.Worlds[World].Get());
        if (!Live(WorldRoot)) { return false; }
        for (auto* Cube : Cubes(F,World))
        {
            FCall C(Cube,TEXT("Get_SharedBridge"),FUNC_BlueprintPure);
            const auto* Ret=Field<FStructProperty>(C.Function,TEXT("ReturnValue"));
            if (!Ret || Ret->Struct!=FCk_Handle::StaticStruct() || !C.Send()) { return false; }
            const auto H=*Ret->ContainerPtrToValuePtr<FCk_Handle>(C.Args->GetStructMemory());
            TSet<FCk_Handle> AllocatedClosure;
            if (!Live(H) || !CapturedClosure(H,WorldRoot,AllocatedClosure) || AllocatedClosure.IsEmpty()) { return false; }
            for (const auto& Candidate:AllocatedClosure)
            { if (Live(Candidate) && Candidate.Has<ck::FFragment_Timer>()) { return false; } }
        }
        return true;
    }
    auto CurrentAuthoritySource(const FFixture& F) -> bool
    {
        if (!HasSixActual(F,0)) { return false; }
        for (int32 Slot=0;Slot<Slots;++Slot)
        {
            const void* R=Row(F,F.Data(0),Slot);
            auto* Cube=Cast<AActor>(Obj(F.RowType,R,TEXT("Cube")));
            const auto H=Bridge(Cube);
            if (!Live(H) || !UCk_Utils_Transform_UE::Has(H)) { return false; }
            const auto Source=UCk_Utils_Transform_UE::Get_EntityCurrentTransform(UCk_Utils_Transform_UE::Cast(H));
            if (!Source.Equals(Cube->GetActorTransform(),0.01)) { return false; }
        }
        return true;
    }
    auto TransformNear(const FTransform& A,const FTransform& B) -> bool
    {
        return A.GetTranslation().Equals(B.GetTranslation(),2.0) &&
            A.GetRotation().AngularDistance(B.GetRotation())<FMath::DegreesToRadians(1.0) &&
            A.GetScale3D().Equals(B.GetScale3D(),0.02);
    }
    auto ObserveActorVisuals(FFixture& F,int32 World) -> void
    {
        if (World<0 || World>=3 || !Ready(F,World)) { return; }
        const int32 G=I32(F.SnapshotType,F.Data(World),TEXT("Generation"));
        if (G<=0) { return; }
        if (F.VisualGeneration[World]!=G)
        {
            F.VisualGeneration[World]=G;
            for (int32 Slot=0;Slot<Slots;++Slot)
            {
                F.FirstVisualRecorded[World][Slot]=false; F.SawVisualMovement[World][Slot]=false;
                F.SawAppliedTransform[World][Slot]=false; F.SawAuthorityTransformMatch[World][Slot]=false;
                F.LastAuthorityMatchFrame[World][Slot]=0; F.AuthorityMatchFrames[World][Slot]=0;
            }
        }
        if (World==0 && F.AuthorityTransformGeneration!=G)
        {
            F.AuthorityTransformGeneration=G;
            for (auto& History:F.AuthorityTransformHistory) { History.Reset(); }
        }
        for (int32 Slot=0;Slot<Slots;++Slot)
        {
            const void* R=Row(F,F.Data(World),Slot);
            auto* Cube=Cast<AActor>(Obj(F.RowType,R,TEXT("Cube")));
            if (!IsValid(Cube) || Cube->GetClass()!=F.CubeClass || Cube->GetWorld()!=F.Worlds[World].Get() ||
                I32(F.CubeClass,Cube,TEXT("SharedSlot"))!=Slot ||
                I32(F.CubeClass,Cube,TEXT("SharedGeneration"))!=G) { continue; }
            const auto Current=Cube->GetActorTransform();
            if (!Current.IsValid()) { continue; }
            const auto H=Bridge(Cube);
            if (!Live(H) || !UCk_Utils_Transform_UE::Has(H) ||
                !H.Has<ck::FFragment_Transform>()) { continue; }
            const auto Native=UCk_Utils_Transform_UE::Get_EntityCurrentTransform(UCk_Utils_Transform_UE::Cast(H));
            const auto& Fragment=H.Get<ck::FFragment_Transform>().Get_Transform();
            if (!TransformNear(Native,Current) || !TransformNear(Fragment,Current)) { continue; }
            if (World==0)
            {
                auto& History=F.AuthorityTransformHistory[Slot];
                History.Add(Native);
                if (History.Num()>2048) { History.RemoveAt(0); }
            }
            if (!F.FirstVisualRecorded[World][Slot])
            { F.FirstVisual[World][Slot]=Current; F.FirstVisualRecorded[World][Slot]=true; continue; }
            const auto& First=F.FirstVisual[World][Slot];
            if (Slot==0 || Slot==3)
            { F.SawVisualMovement[World][Slot]|=FVector::Dist(Current.GetTranslation(),First.GetTranslation())>5.0; }
            else if (Slot==2)
            { F.SawVisualMovement[World][Slot]|=FVector::Dist(Current.GetScale3D(),First.GetScale3D())>0.05; }
            else
            { F.SawVisualMovement[World][Slot]|=Current.GetRotation().AngularDistance(First.GetRotation())>FMath::DegreesToRadians(1.0); }
            if (World>0 && F.SawVisualMovement[World][Slot] && F.AuthorityTransformGeneration==G &&
                HasNativeTransformEntries(TransformDriver(H),true))
            {
                F.SawAppliedTransform[World][Slot]=true;
                for (const auto& Authority:F.AuthorityTransformHistory[Slot])
                {
                    if (TransformNear(Native,Authority))
                    {
                        if (F.AuthorityMatchFrames[World][Slot]==0 ||
                            F.LastAuthorityMatchFrame[World][Slot]!=GFrameCounter)
                        {
                            F.LastAuthorityMatchFrame[World][Slot]=GFrameCounter;
                            if (F.AuthorityMatchFrames[World][Slot]<2)
                            { ++F.AuthorityMatchFrames[World][Slot]; }
                            F.SawAuthorityTransformMatch[World][Slot]=F.AuthorityMatchFrames[World][Slot]>=2;
                        }
                        break;
                    }
                }
            }
        }
    }
    auto AllActorVisualsMoved(const FFixture& F) -> bool
    {
        for (int32 World=0;World<3;++World)
        { for (int32 Slot=0;Slot<Slots;++Slot) { if (!F.SawVisualMovement[World][Slot]) { return false; } } }
        return true;
    }
    auto EqualStationsOnWire(const FFixture& F, const void* Authority, const void* Client) -> bool
    {
        if (!F.Stations || !Authority || !Client) { return false; }
        const auto* Inner = CastField<FStructProperty>(F.Stations->Inner);
        const auto* Transform = Inner ? Field<FStructProperty>(Inner->Struct,TEXT("InitialTransform")) : nullptr;
        if (!Transform || Transform->Struct!=TBaseStructure<FTransform>::Get()) { return false; }
        FScriptArrayHelper A(F.Stations,F.Stations->ContainerPtrToValuePtr<void>(Authority));
        FScriptArrayHelper B(F.Stations,F.Stations->ContainerPtrToValuePtr<void>(Client));
        if (A.Num()!=Slots || B.Num()!=Slots) { return false; }
        for (int32 Slot=0;Slot<Slots;++Slot)
        {
            const void *AS=A.GetRawPtr(Slot), *BS=B.GetRawPtr(Slot);
            if (!AS || !BS) { return false; }
            for (TFieldIterator<FProperty> It(Inner->Struct); It; ++It)
            {
                if (*It==Transform)
                {
                    const auto* Source=Transform->ContainerPtrToValuePtr<FTransform>(AS);
                    const auto* Actual=Transform->ContainerPtrToValuePtr<FTransform>(BS);
                    if (!Source || !Actual ||
                        !UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(*Source,*Actual))
                    { return false; }
                }
                else if (!It->Identical_InContainer(AS,BS)) { return false; }
            }
        }
        return true;
    }
    auto EqualSourceSample(const FFixture& F, const void* Authority, const void* Client) -> bool
    {
        if (!Authority || !Client || !Complete(F,0,Authority)) { return false; }
        for (TFieldIterator<FProperty> It(F.SnapshotType); It; ++It)
        {
            if (*It == F.Rows) { continue; }
            if (*It == F.Stations)
            {
                if (!EqualStationsOnWire(F,Authority,Client)) { return false; }
                continue;
            }
            if (!It->Identical_InContainer(Authority,Client)) { return false; }
        }
        for (int32 Slot=0; Slot<Slots; ++Slot)
        {
            const void *A=Row(F,Authority,Slot), *B=Row(F,Client,Slot);
            if (!A || !B) { return false; }
            for (TFieldIterator<FProperty> It(F.RowType); It; ++It)
            {
                if (It->GetFName()==FName(TEXT("Cube"))) { continue; }
                if (!It->Identical_InContainer(A,B)) { return false; }
            }
        }
        return true;
    }
    auto MatchedSource(const FFixture& F, int32 World) -> bool
    {
        if (World<1 || World>2 || !Ready(F,0) || !Ready(F,World)) { return false; }
        const int32 CurrentGeneration=I32(F.SnapshotType,F.Data(0),TEXT("Generation"));
        if (CurrentGeneration<=0 ||
            I32(F.SnapshotType,F.Data(World),TEXT("Generation"))!=CurrentGeneration) { return false; }
        int32 Pairs=0;
        for (auto Key:F.Order[World])
        {
            if (int32(Key>>32)!=CurrentGeneration) { continue; }
            const auto* A=F.History[0].Find(Key), *B=F.History[World].Find(Key);
            if (A && B && A->IsValid() && B->IsValid() &&
                EqualSourceSample(F,(*A)->GetStructMemory(),(*B)->GetStructMemory())) { ++Pairs; }
        }
        if (Pairs<2) { return false; }
        for (int32 Slot=0;Slot<Slots;++Slot)
        {
            if (!F.SawAppliedTransform[World][Slot] || !F.SawAuthorityTransformMatch[World][Slot])
            { return false; }
        }
        return true;
    }
    auto DynamicsObserved(const FFixture& F) -> bool
    {
        if (F.Order[0].Num()<2) { return false; }
        const int32 G=I32(F.SnapshotType,F.Data(0),TEXT("Generation"));
        bool Corner=false,RotationStep=false,Scale=false,Orbit=false,Spin=false,Vectors=false;
        for (auto Key:F.Order[0])
        {
            if (int32(Key>>32)!=G) { continue; }
            const auto* Held=F.History[0].Find(Key);
            if (!Held || !Held->IsValid()) { continue; }
            const void* D=(*Held)->GetStructMemory();
            const void *R0=Row(F,D,0), *R1=Row(F,D,1), *R2=Row(F,D,2),
                *R3=Row(F,D,3), *R4=Row(F,D,4), *R5=Row(F,D,5);
            if (!R0 || !R1 || !R2 || !R3 || !R4 || !R5) { return false; }
            Corner|=I32(F.RowType,R0,TEXT("Corner"))>0;
            RotationStep|=I32(F.RowType,R1,TEXT("RotationStep"))>0;
            Scale|=F.SawVisualMovement[0][2];
            Orbit|=RowFloat(F.RowType,R3,TEXT("Elapsed"))>0 && F.SawVisualMovement[0][3];
            Spin|=RowFloat(F.RowType,R4,TEXT("Elapsed"))>0 && F.SawVisualMovement[0][4];
            Vectors|=RowFloat(F.RowType,R5,TEXT("Elapsed"))>0 && F.SawVisualMovement[0][5];
        }
        return Corner && RotationStep && Scale && Orbit && Spin && Vectors;
    }
    auto StationDifference(const FArrayProperty* Property,const void* AD,const void* BD,uint64 Key) -> FString
    {
        if (!Property || !AD || !BD) { return TEXT("station diagnostic unavailable"); }
        const auto* Inner=CastField<FStructProperty>(Property->Inner);
        if (!Inner || !IsValid(Inner->Struct)) { return TEXT("station diagnostic schema rejected"); }
        FScriptArrayHelper A(Property,Property->ContainerPtrToValuePtr<void>(AD));
        FScriptArrayHelper B(Property,Property->ContainerPtrToValuePtr<void>(BD));
        FString Result=FString::Printf(TEXT("sameKey=%llu Stations counts source=%d client=%d"),Key,A.Num(),B.Num());
        if (A.Num()!=Slots || B.Num()!=Slots) { return Result; }
        for (int32 Slot=0;Slot<Slots;++Slot)
        {
            const void* AS=A.GetRawPtr(Slot); const void* BS=B.GetRawPtr(Slot);
            if (!AS || !BS) { Result+=FString::Printf(TEXT("; station%d absent"),Slot); continue; }
            for (TFieldIterator<FProperty> It(Inner->Struct); It; ++It)
            {
                if (It->Identical_InContainer(AS,BS)) { continue; }
                FString AT,BT;
                It->ExportTextItem_Direct(AT,It->ContainerPtrToValuePtr<void>(AS),nullptr,nullptr,PPF_None);
                It->ExportTextItem_Direct(BT,It->ContainerPtrToValuePtr<void>(BS),nullptr,nullptr,PPF_None);
                AT.ReplaceInline(TEXT("\n"),TEXT(" ")); BT.ReplaceInline(TEXT("\n"),TEXT(" "));
                Result+=FString::Printf(TEXT("; station%d.%s source='%s' client='%s'"),Slot,*It->GetName(),*AT,*BT);
                const auto* Transform=CastField<FStructProperty>(*It);
                if (!Transform || Transform->Struct!=TBaseStructure<FTransform>::Get()) { continue; }
                const auto* TA=Transform->ContainerPtrToValuePtr<FTransform>(AS);
                const auto* TB=Transform->ContainerPtrToValuePtr<FTransform>(BS);
                if (!TA || !TB) { continue; }
                for (int32 Side=0;Side<2;++Side)
                {
                    const auto& V=Side==0?*TA:*TB;
                    const auto T=V.GetTranslation(),S=V.GetScale3D(); const auto Q=V.GetRotation();
                    Result+=FString::Printf(TEXT(" %s T=(%.17g,%.17g,%.17g) Q=(%.17g,%.17g,%.17g,%.17g) S=(%.17g,%.17g,%.17g)"),
                        Side==0?TEXT("source"):TEXT("client"),T.X,T.Y,T.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z);
                }
            }
        }
        return Result;
    }
    auto SourceDifference(const FFixture& F,int32 World) -> FString
    {
        int32 Keys=0;
        for (auto Key:F.Order[World])
        {
            const auto* A=F.History[0].Find(Key), *B=F.History[World].Find(Key);
            if (!A || !B || !A->IsValid() || !B->IsValid()) { continue; }
            ++Keys;
            const void* AD=(*A)->GetStructMemory(), *BD=(*B)->GetStructMemory();
            for (TFieldIterator<FProperty> It(F.SnapshotType); It; ++It)
            {
                if (*It==F.Rows || It->Identical_InContainer(AD,BD)) { continue; }
                if (It->GetFName()==FName(TEXT("Stations")))
                { return StationDifference(CastField<FArrayProperty>(*It),AD,BD,Key); }
                return FString::Printf(TEXT("sameKey=%llu snapshot.%s differs"),Key,*It->GetName());
            }
            for (int32 Slot=0;Slot<Slots;++Slot)
            {
                const void *AR=Row(F,AD,Slot),*BR=Row(F,BD,Slot);
                if (!AR || !BR) { return FString::Printf(TEXT("sameKey=%llu row%d absent"),Key,Slot); }
                for (TFieldIterator<FProperty> It(F.RowType); It; ++It)
                {
                    if (It->GetFName()==FName(TEXT("Cube")) || It->Identical_InContainer(AR,BR)) { continue; }
                    FString AText,BText;
                    It->ExportTextItem_Direct(AText,It->ContainerPtrToValuePtr<void>(AR),nullptr,nullptr,PPF_None);
                    It->ExportTextItem_Direct(BText,It->ContainerPtrToValuePtr<void>(BR),nullptr,nullptr,PPF_None);
                    AText.ReplaceInline(TEXT("\n"),TEXT(" ")); BText.ReplaceInline(TEXT("\n"),TEXT(" "));
                    return FString::Printf(TEXT("sameKey=%llu row%d.%s source='%s' client='%s'"),
                        Key,Slot,*It->GetName(),*AText.Left(160),*BText.Left(160));
                }
            }
        }
        return FString::Printf(TEXT("sameKeyPairs=%d; no exact source sample"),Keys);
    }
    auto SaveHistory(FFixture& F,UWorld* W) -> void
    {
        const int32 I=W==F.Worlds[0].Get()?0:W==F.Worlds[1].Get()?1:W==F.Worlds[2].Get()?2:INDEX_NONE;
        if (I==INDEX_NONE || !Ready(F,I)) { return; }
        const int32 G=I32(F.SnapshotType,F.Data(I),TEXT("Generation")),Q=I32(F.SnapshotType,F.Data(I),TEXT("Sequence"));
        if (G<=0 || Q<=0) { return; }
        const auto K=FFixture::Key(G,Q);
        if (F.History[I].Contains(K)) { return; }
        F.History[I].Add(K,Copy(F.SnapshotType,F.Data(I))); F.Order[I].Add(K);
        if (F.Order[I].Num()>4096) { F.History[I].Remove(F.Order[I][0]); F.Order[I].RemoveAt(0); }
    }
    auto OldGone(const FFixture& F,int32 World) -> bool
    {
        if (World<0 || World>=3 || !Retired(F.OldClosure[World])) { return false; }
        for (const auto& W:F.OldActors[World]) { if (W.IsValid()) { return false; } }
        return true;
    }
    auto CaptureOld(FFixture& F) -> bool
    {
        for (int32 World=0;World<3;++World)
        {
            const auto WorldRoot=Root(F.Worlds[World].Get());
            const auto Actors=Cubes(F,World);
            if (!Live(WorldRoot) || Actors.Num()!=Slots) { return false; }
            for (int32 Slot=0;Slot<Slots;++Slot)
            {
                const auto TaggedStations=Tagged(F.Worlds[World].Get(),Slot);
                if (TaggedStations.Num()!=1 || !Live(TaggedStations[0]) ||
                    !CapturedClosure(TaggedStations[0],WorldRoot,F.OldClosure[World])) { return false; }
            }
            for (auto* A:Actors)
            {
                if (!IsValid(A)) { return false; }
                F.OldActors[World].Add(TWeakObjectPtr<AActor>(A));
                FCall C(A,TEXT("Get_SharedBridge"),FUNC_BlueprintPure);
                const auto* Ret=Field<FStructProperty>(C.Function,TEXT("ReturnValue"));
                if (!Ret || Ret->Struct!=FCk_Handle::StaticStruct() || !C.Send()) { return false; }
                const auto Bridge=*Ret->ContainerPtrToValuePtr<FCk_Handle>(C.Args->GetStructMemory());
                if (!Live(Bridge) || !CapturedClosure(Bridge,WorldRoot,F.OldClosure[World])) { return false; }
            }
            if (F.OldClosure[World].IsEmpty() || F.OldActors[World].Num()!=Slots) { return false; }
        }
        return true;
    }
    auto RejectNarrowSnapshotMutations(FFixture& F) -> bool
    {
        const auto Fail=[&F](const TCHAR* Why) -> bool
        { F.SnapshotValidationFailure=Why; return false; };
        const void* Authority=F.Data(0);
        const void* Receipt0=F.OwnerCommit(0,true);
        const void* Receipt1=F.OwnerCommit(1,true);
        const auto* Param=F.Stations ? CastField<FStructProperty>(F.Stations->Inner) : nullptr;
        const auto* Transform=Param ? Field<FStructProperty>(Param->Struct,TEXT("InitialTransform")) : nullptr;
        const auto* Width=Param ? Field<FDoubleProperty>(Param->Struct,TEXT("Width")) : nullptr;
        if (!Authority || !Receipt0 || !Receipt1 || !Param ||
            !Transform || Transform->Struct!=TBaseStructure<FTransform>::Get() || !Width ||
            !Ready(F,0) || !Complete(F,0,Authority))
        { return Fail(TEXT("authority source or exact reflected station fields unavailable")); }
        const auto SourceCopy=Copy(F.SnapshotType,Authority);
        const auto ReceiptCopy0=Copy(F.CommitType,Receipt0);
        const auto ReceiptCopy1=Copy(F.CommitType,Receipt1);
        if (!SourceCopy.IsValid() || !ReceiptCopy0.IsValid() || !ReceiptCopy1.IsValid())
        { return Fail(TEXT("real authority source or owner receipts could not be copied")); }
        TArray<uint8> SourceRaw,ReceiptRaw0,ReceiptRaw1;
        SourceRaw.Append(static_cast<const uint8*>(Authority),F.SnapshotType->GetStructureSize());
        ReceiptRaw0.Append(static_cast<const uint8*>(Receipt0),F.CommitType->GetStructureSize());
        ReceiptRaw1.Append(static_cast<const uint8*>(Receipt1),F.CommitType->GetStructureSize());
        const auto ActorsBefore=Cubes(F,0);
        TArray<FCk_Handle> StationsBefore,LedgerBefore;
        if (ActorsBefore.Num()!=Slots || !StationHandles(F,0,StationsBefore) || StationsBefore.Num()!=Slots)
        { return Fail(TEXT("real authority actors or station identities incomplete")); }
        if (!PureHandles(F.State(0),TEXT("Get_CapturedBridgeLedger"),LedgerBefore) || LedgerBefore.IsEmpty())
        { return Fail(TEXT("real authority captured bridge ledger unavailable")); }
        const auto Unchanged=[&]() -> bool
        {
            TArray<FCk_Handle> StationsAfter,LedgerAfter;
            return FMemory::Memcmp(Authority,SourceRaw.GetData(),SourceRaw.Num())==0 &&
                FMemory::Memcmp(Receipt0,ReceiptRaw0.GetData(),ReceiptRaw0.Num())==0 &&
                FMemory::Memcmp(Receipt1,ReceiptRaw1.GetData(),ReceiptRaw1.Num())==0 &&
                F.SnapshotType->CompareScriptStruct(Authority,SourceCopy->GetStructMemory(),PPF_None) &&
                F.CommitType->CompareScriptStruct(Receipt0,ReceiptCopy0->GetStructMemory(),PPF_None) &&
                F.CommitType->CompareScriptStruct(Receipt1,ReceiptCopy1->GetStructMemory(),PPF_None) &&
                Cubes(F,0)==ActorsBefore && StationHandles(F,0,StationsAfter) && StationsAfter==StationsBefore &&
                PureHandles(F.State(0),TEXT("Get_CapturedBridgeLedger"),LedgerAfter) && LedgerAfter==LedgerBefore;
        };
        if (!Complete(F,0,SourceCopy->GetStructMemory()) || !Unchanged())
        { return Fail(TEXT("real copied authority baseline rejected or validation mutated production state")); }
        for (int32 Mutation=0;Mutation<3;++Mutation)
        {
            const auto TestSample=Copy(F.SnapshotType,Authority);
            if (!TestSample.IsValid()) { return Fail(TEXT("real source copy unavailable")); }
            FScriptArrayHelper Entries(F.Stations,F.Stations->ContainerPtrToValuePtr<void>(TestSample->GetStructMemory()));
            if (Entries.Num()!=Slots) { return Fail(TEXT("source station cardinality changed")); }
            void* Station=Entries.GetRawPtr(0);
            auto* T=Transform->ContainerPtrToValuePtr<FTransform>(Station);
            if (!T) { return Fail(TEXT("real station transform unavailable")); }
            if (Mutation==0)
            {
                auto Q=T->GetRotation(); Q.X+=5.0e-13; T->SetRotation(Q);
                if (!Q.IsNormalized() || !T->IsValid() || T->ContainsNaN())
                { return Fail(TEXT("near quaternion mutation was not finite and normalized")); }
            }
            else if (Mutation==1)
            {
                auto P=T->GetTranslation(); P.X=std::nextafter(P.X,P.X+1.0); T->SetTranslation(P);
                if (!T->IsValid() || T->ContainsNaN())
                { return Fail(TEXT("one-bit translation mutation was invalid")); }
            }
            else
            { Width->SetPropertyValue_InContainer(Station,Width->GetPropertyValue_InContainer(Station)+1.0); }
            if (Complete(F,0,TestSample->GetStructMemory()))
            { return Fail(Mutation==0?TEXT("near quaternion accepted"):Mutation==1?TEXT("one-bit translation accepted"):TEXT("non-transform Width accepted")); }
            if (!Unchanged()) { return Fail(TEXT("pure validation changed source, owner receipts, actors, or stations")); }
        }
        F.SnapshotValidationFailure.Empty();
        return true;
    }
    // Installed owner HUD data is immutable across asynchronous actor-reference
    // fixups. Compare every reflected value except the six transient row Cube
    // references; current-world actor identity is proved by HasSixActual.
    auto SameOwnerCommitDataExceptCubes(const FFixture& F,const void* A,const void* B) -> bool
    {
        if (!A || !B || !F.CommitType || !F.SnapshotType || !F.RowType ||
            !F.CommitSnapshot || !F.Rows) { return false; }
        const auto* Cube=Field<FObjectPropertyBase>(F.RowType,TEXT("Cube"));
        const auto* Inner=CastField<FStructProperty>(F.Rows->Inner);
        if (!Cube || Cube->PropertyClass!=F.CubeClass ||
            !Inner || Inner->Struct!=F.RowType) { return false; }
        for (TFieldIterator<FProperty> It(F.CommitType);It;++It)
        {
            if (*It==F.CommitSnapshot) { continue; }
            if (!It->Identical_InContainer(A,B,PPF_None)) { return false; }
        }
        const void* SA=F.CommitSnapshot->ContainerPtrToValuePtr<void>(A);
        const void* SB=F.CommitSnapshot->ContainerPtrToValuePtr<void>(B);
        if (!SA || !SB) { return false; }
        for (TFieldIterator<FProperty> It(F.SnapshotType);It;++It)
        {
            if (*It==F.Rows) { continue; }
            if (!It->Identical_InContainer(SA,SB,PPF_None)) { return false; }
        }
        FScriptArrayHelper ARows(F.Rows,F.Rows->ContainerPtrToValuePtr<FScriptArray>(SA));
        FScriptArrayHelper BRows(F.Rows,F.Rows->ContainerPtrToValuePtr<FScriptArray>(SB));
        if (ARows.Num()!=Slots || BRows.Num()!=Slots) { return false; }
        for (int32 Slot=0;Slot<Slots;++Slot)
        {
            const void* RA=ARows.GetRawPtr(Slot);
            const void* RB=BRows.GetRawPtr(Slot);
            if (!RA || !RB) { return false; }
            for (TFieldIterator<FProperty> It(F.RowType);It;++It)
            {
                if (*It==Cube) { continue; }
                if (!It->Identical_InContainer(RA,RB,PPF_None)) { return false; }
            }
        }
        return true;
    }
    // An accepted delivery persists when a later stale request changes only the
    // latest-attempt result. Keep every owner, source, installed and ACK check.
    auto AcceptedReceipt(const FFixture& F,int32 Owner,int32 Serial) -> bool
    {
        auto* Client=F.Local[Owner].Get(); auto* Server=F.Remote[Owner].Get();
        const auto* S=F.OwnerCommit(Owner,true), *C=F.OwnerCommit(Owner,false);
        if (!IsValid(Client) || !IsValid(Server) || !S || !C ||
            I32(F.PCClass,Server,TEXT("LastControlSerial"))!=Serial ||
            I32(F.CommitType,S,TEXT("RequestSerial"))!=Serial ||
            I32(F.CommitType,C,TEXT("RequestSerial"))!=Serial) { return false; }
        const int32 Delivery=I32(F.CommitType,S,TEXT("DeliverySerial"));
        const void* ClientSample=F.CommitSnapshot->ContainerPtrToValuePtr<void>(C);
        const int32 G=I32(F.SnapshotType,ClientSample,TEXT("Generation"));
        const int32 Q=I32(F.SnapshotType,ClientSample,TEXT("Sequence"));
        const auto* Origin=F.History[0].Find(FFixture::Key(G,Q));
        const auto Publication=PureStruct(Server,TEXT("Get_PublishedTransformSourceAudit"),F.CommitType);
        const void* Audit=Publication.IsValid()?Publication->GetStructMemory():nullptr;
        const void* AuthoritySample=Audit?F.CommitSnapshot->ContainerPtrToValuePtr<void>(Audit):nullptr;
        const void* RetainedSample=F.CommitSnapshot->ContainerPtrToValuePtr<void>(S);
        const auto Installed=PureStruct(Client,TEXT("Get_InstalledTransformCommit"),F.CommitType);
        return Delivery>0 && I32(F.CommitType,C,TEXT("DeliverySerial"))==Delivery &&
            I32(F.PCClass,Server,TEXT("LastAcknowledgedDeliverySerial"))==Delivery &&
            IsValid(F.State(Owner+1)) && Complete(F,Owner+1,ClientSample) &&
            Audit && AuthoritySample && I32(F.CommitType,Audit,TEXT("RequestSerial"))==Serial &&
            I32(F.CommitType,Audit,TEXT("DeliverySerial"))==Delivery &&
            I32(F.CommitType,Audit,TEXT("OriginGeneration"))==G-1 &&
            Obj(F.CommitType,Audit,TEXT("Owner"))==Server &&
            Obj(F.CommitType,Audit,TEXT("OwnerPawn"))==F.RemotePawn[Owner].Get() &&
            Obj(F.CommitType,Audit,TEXT("OriginState"))==F.State(0) &&
            I32(F.SnapshotType,AuthoritySample,TEXT("Generation"))==G &&
            I32(F.SnapshotType,AuthoritySample,TEXT("Sequence"))==Q &&
            F.SnapshotType->CompareScriptStruct(AuthoritySample,RetainedSample,PPF_None) &&
            EqualSourceSample(F,AuthoritySample,ClientSample) &&
            (!Origin || (Origin->IsValid() &&
                F.SnapshotType->CompareScriptStruct((*Origin)->GetStructMemory(),AuthoritySample,PPF_None))) &&
            Installed.IsValid() &&
            SameOwnerCommitDataExceptCubes(F,Installed->GetStructMemory(),C) &&
            Obj(F.CommitType,C,TEXT("Owner"))==Client &&
            Obj(F.CommitType,C,TEXT("OwnerPawn"))==F.LocalPawn[Owner].Get() &&
            Obj(F.CommitType,C,TEXT("OriginState"))==F.State(Owner+1);
    }
    auto Receipt(const FFixture& F,int32 Owner,int32 Serial) -> bool
    {
        auto* Server=Owner>=0 && Owner<2 ? F.Remote[Owner].Get() : nullptr;
        return IsValid(Server) && I32(F.PCClass,Server,TEXT("LastControlResult"))==1 &&
            AcceptedReceipt(F,Owner,Serial);
    }
    auto ReceiptEvidence(const FFixture& F,int32 Owner,int32 Serial) -> FString
    {
        if (Owner<0 || Owner>=2) { return TEXT("invalid owner"); }
        auto* Client=F.Local[Owner].Get(); auto* Server=F.Remote[Owner].Get();
        const void* S=F.OwnerCommit(Owner,true), *C=F.OwnerCommit(Owner,false);
        if (!IsValid(Client) || !IsValid(Server) || !S || !C)
        { return TEXT("client/server/commit missing"); }
        const int32 Delivery=I32(F.CommitType,S,TEXT("DeliverySerial"));
        const void* Sample=F.CommitSnapshot->ContainerPtrToValuePtr<void>(C);
        const int32 G=I32(F.SnapshotType,Sample,TEXT("Generation"));
        const int32 Q=I32(F.SnapshotType,Sample,TEXT("Sequence"));
        const auto* Origin=F.History[0].Find(FFixture::Key(G,Q));
        const auto Publication=PureStruct(Server,TEXT("Get_PublishedTransformSourceAudit"),F.CommitType);
        const void* Audit=Publication.IsValid()?Publication->GetStructMemory():nullptr;
        const void* AuthoritySample=Audit?F.CommitSnapshot->ContainerPtrToValuePtr<void>(Audit):nullptr;
        const auto Installed=PureStruct(Client,TEXT("Get_InstalledTransformCommit"),F.CommitType);
        FString CacheFields; int32 CubeRefFixups=0;
        if (Installed.IsValid())
        {
            auto Describe=[&CacheFields,&CubeRefFixups](const FProperty* P,const void* A,const void* B,const FString& Path)
            {
                if (P->Identical_InContainer(A,B)) { return; }
                if (const auto* Object=CastField<FObjectPropertyBase>(P))
                {
                    // Print raw identities only; never dereference a rejected or retired actor.
                    if (Path.EndsWith(TEXT(".Cube"))) { ++CubeRefFixups; }
                    CacheFields+=FString::Printf(TEXT(" %s[%p/%p]"),*Path,
                        Object->GetObjectPropertyValue_InContainer(A),Object->GetObjectPropertyValue_InContainer(B));
                }
                else { CacheFields+=TEXT(" ")+Path; }
            };
            const void* I=Installed->GetStructMemory();
            for (TFieldIterator<FProperty> It(F.CommitType);It;++It)
            { if (*It!=F.CommitSnapshot) { Describe(*It,I,C,It->GetName()); } }
            const void* IS=F.CommitSnapshot->ContainerPtrToValuePtr<void>(I);
            for (TFieldIterator<FProperty> It(F.SnapshotType);It;++It)
            { if (*It!=F.Rows) { Describe(*It,IS,Sample,TEXT("Snapshot.")+It->GetName()); } }
            FScriptArrayHelper InstalledRows(F.Rows,F.Rows->ContainerPtrToValuePtr<void>(IS));
            FScriptArrayHelper ReceivedRows(F.Rows,F.Rows->ContainerPtrToValuePtr<void>(Sample));
            if (InstalledRows.Num()!=ReceivedRows.Num())
            { CacheFields+=FString::Printf(TEXT(" RowsCount[%d/%d]"),InstalledRows.Num(),ReceivedRows.Num()); }
            for (int32 Slot=0;Slot<FMath::Min(InstalledRows.Num(),ReceivedRows.Num());++Slot)
            {
                for (TFieldIterator<FProperty> It(F.RowType);It;++It)
                {
                    Describe(*It,InstalledRows.GetRawPtr(Slot),ReceivedRows.GetRawPtr(Slot),
                        FString::Printf(TEXT("Rows[%d].%s"),Slot,*It->GetName()));
                }
            }
        }
        return FString::Printf(TEXT("owner%d accepted=%d latestAttempt=%d latestResult=%d serverSerial=%d serverReq=%d clientReq=%d delivery=%d/%d ack=%d key=%d:%d sourceKey=%d sourceEqual=%d publication=%d pubKey=%d:%d pubEqual=%d complete=%d installedDataEqual=%d rawInstalledEqual=%d cubeRefFixups=%d identity=%d%d%d"),
            Owner,AcceptedReceipt(F,Owner,Serial),I32(F.PCClass,Server,TEXT("LastControlAttemptSerial")),
            I32(F.PCClass,Server,TEXT("LastControlResult")),
            I32(F.PCClass,Server,TEXT("LastControlSerial")),
            I32(F.CommitType,S,TEXT("RequestSerial")),I32(F.CommitType,C,TEXT("RequestSerial")),
            Delivery,I32(F.CommitType,C,TEXT("DeliverySerial")),
            I32(F.PCClass,Server,TEXT("LastAcknowledgedDeliverySerial")),G,Q,
            Origin && Origin->IsValid(),
            Origin && Origin->IsValid() && EqualSourceSample(F,(*Origin)->GetStructMemory(),Sample),
            Audit!=nullptr,
            I32(F.SnapshotType,AuthoritySample,TEXT("Generation")),
            I32(F.SnapshotType,AuthoritySample,TEXT("Sequence")),
            AuthoritySample && EqualSourceSample(F,AuthoritySample,Sample),
            Complete(F,Owner+1,Sample),
            Installed.IsValid() && SameOwnerCommitDataExceptCubes(F,Installed->GetStructMemory(),C),
            Installed.IsValid() && F.CommitType->CompareScriptStruct(Installed->GetStructMemory(),C,PPF_None),
            CubeRefFixups,
            Obj(F.CommitType,C,TEXT("Owner"))==Client,
            Obj(F.CommitType,C,TEXT("OwnerPawn"))==F.LocalPawn[Owner].Get(),
            Obj(F.CommitType,C,TEXT("OriginState"))==F.State(Owner+1))+TEXT(" cacheFields=")+CacheFields;
    }
    auto ForeignAckRejected(const FFixture& F) -> bool
    {
        auto* Owner=F.Remote[1].Get(); auto* Other=F.Remote[0].Get();
        auto* CorrectState=F.State(0); auto* ForeignState=F.State(1);
        const auto* Commit=F.OwnerCommit(1,true);
        if (!IsValid(Owner) || !IsValid(Other) || !IsValid(CorrectState) || !IsValid(ForeignState) ||
            CorrectState==ForeignState || !IsValid(Owner->GetPawn()) || !IsValid(Other->GetPawn()) ||
            Owner->GetPawn()==Other->GetPawn() || !Commit) { return false; }
        const int32 Delivery=I32(F.CommitType,Commit,TEXT("DeliverySerial"));
        const int32 Serial=I32(F.CommitType,Commit,TEXT("RequestSerial"));
        const int32 Before=I32(F.PCClass,Owner,TEXT("LastAcknowledgedDeliverySerial"));
        const auto Frozen=Copy(F.CommitType,Commit);
        if (Delivery<=0 || Serial<=0 || Before>=Delivery || !Frozen.IsValid()) { return false; }
        const auto Attempt=[&](AGameStateBase* State,APawn* Pawn) -> bool
        {
            FCall C(Owner,TEXT("Server_AcknowledgeTransformCommit"),static_cast<EFunctionFlags>(FUNC_Net | FUNC_NetServer));
            auto* InState=Field<FObjectPropertyBase>(C.Function,TEXT("InState"));
            auto* InPawn=Field<FObjectPropertyBase>(C.Function,TEXT("InPawn"));
            auto* InDelivery=Field<FIntProperty>(C.Function,TEXT("InDelivery"));
            auto* InSerial=Field<FIntProperty>(C.Function,TEXT("InSerial"));
            const auto PlainInput=[](const FProperty* P) -> bool
            { return P && P->ArrayDim==1 && P->HasAnyPropertyFlags(CPF_Parm) &&
                !P->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm | CPF_ReferenceParm); };
            if (!InState || !InPawn || !InDelivery || !InSerial || C.Function->NumParms!=4 ||
                !PlainInput(InState) || !PlainInput(InPawn) || !PlainInput(InDelivery) || !PlainInput(InSerial) ||
                InState->PropertyClass!=F.StateClass || !InPawn->PropertyClass ||
                !InPawn->PropertyClass->IsChildOf(APawn::StaticClass())) { return false; }
            auto* Memory=C.Args->GetStructMemory();
            InState->SetObjectPropertyValue_InContainer(Memory,State);
            InPawn->SetObjectPropertyValue_InContainer(Memory,Pawn);
            InDelivery->SetPropertyValue_InContainer(Memory,Delivery);
            InSerial->SetPropertyValue_InContainer(Memory,Serial);
            if (!C.Send()) { return false; }
            const auto* After=F.OwnerCommit(1,true);
            return After && I32(F.PCClass,Owner,TEXT("LastAcknowledgedDeliverySerial"))==Before &&
                F.CommitType->CompareScriptStruct(Frozen->GetStructMemory(),After,PPF_None);
        };
        // Two actual foreign contexts, each with otherwise valid retained delivery and serial.
        return Attempt(ForeignState,Owner->GetPawn()) && Attempt(CorrectState,Other->GetPawn());
    }
    struct FStep { FString Name; TFunction<bool()> Check; bool Wait=false; bool Bootstrap=false; };
    class FScenario final : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test; TSharedRef<FFixture> F; TArray<FStep> Steps; int32 Index=0; double Began=-1;
    public:
        FScenario(FAutomationTestBase* T,TSharedRef<FFixture> InF,TArray<FStep>&& InSteps)
            : Test(T),F(InF),Steps(MoveTemp(InSteps)) {}
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed() || Index>=Steps.Num()) { return true; }
            if (Began<0) { Began=FPlatformTime::Seconds(); }
            auto& S=Steps[Index];
            // Only the first designated step may populate Worlds before Context can resolve them.
            // Its own predicate must establish exact Context before it can advance.
            const bool MayEvaluate=S.Bootstrap ? Index==0 : F->Context();
            if (MayEvaluate && S.Check()) { ++Index; Began=-1; return Index>=Steps.Num(); }
            bool Premature = false;
            for (int32 I=0; I<3; ++I) { Premature |= F->PrematureReplacementUsability[I]; }
            if (S.Wait && !Premature && !F->BoundarySchemaFailed && FPlatformTime::Seconds()-Began<45) { return false; }
            FString Why=TEXT("Transform network phase failed: ")+S.Name;
            for (int32 I=0;I<3;++I)
            { Why+=FString::Printf(TEXT(" [w=%d ready=%d failed=%d gen=%d seq=%d cubes=%d history=%d]"),I,
                Flag(F->SnapshotType,F->Data(I),TEXT("Ready")),Flag(F->SnapshotType,F->Data(I),TEXT("Failed")),
                I32(F->SnapshotType,F->Data(I),TEXT("Generation")),I32(F->SnapshotType,F->Data(I),TEXT("Sequence")),
                Cubes(*F,I).Num(),F->History[I].Num()); }
            for (int32 I=0;I<3;++I)
            { Why+=FString::Printf(TEXT(" [oldWorld=%d closure=%d actors=%d gone=%d firstUsable=%d firstGone=%d premature=%d]"),I,
                F->OldClosure[I].Num(),F->OldActors[I].Num(),OldGone(*F,I),
                F->FirstReplacementUsableObserved[I],F->FirstReplacementUsableOldGone[I],F->PrematureReplacementUsability[I]); }
            if (F->BoundarySchemaFailed) { Why+=TEXT(" [boundarySchema=")+F->BoundarySchemaFailure+TEXT("]"); }
            if (!F->SnapshotValidationFailure.IsEmpty()) { Why+=TEXT(" [snapshotValidation=")+F->SnapshotValidationFailure+TEXT("]"); }
            Why+=TEXT(" [sourceClient1=")+SourceDifference(*F,1)+TEXT("] [sourceClient2=")+SourceDifference(*F,2)+TEXT("]");
            Why+=FString::Printf(TEXT(" [installedBeforeAck=%d/%d]"),F->SawInstalledBeforeAck[0],F->SawInstalledBeforeAck[1]);
            if (F->Requested)
            {
                Why+=TEXT(" [")+ReceiptEvidence(*F,0,F->SecondSerial)+TEXT("] [")+
                    ReceiptEvidence(*F,1,F->FirstSerial)+TEXT("]");
                const void* First=F->OwnerCommit(1,true);
                Why+=FString::Printf(TEXT(" [firstFrozen=%d independent=%d]"),F->FirstRetained.IsValid(),
                    F->FirstRetained.IsValid() && First &&
                    SameOwnerCommitDataExceptCubes(*F,First,F->FirstRetained->GetStructMemory()));
            }
            Why+=FString::Printf(TEXT(" [foreignAttempted=%d foreignRejected=%d]"),F->ForeignAttempted,F->ForeignRejected);
            for (int32 I=0;I<3;++I)
            { Why+=FString::Printf(TEXT(" [visualWorld=%d generation=%d moved=%d%d%d%d%d%d]"),I,F->VisualGeneration[I],
                F->SawVisualMovement[I][0],F->SawVisualMovement[I][1],F->SawVisualMovement[I][2],
                F->SawVisualMovement[I][3],F->SawVisualMovement[I][4],F->SawVisualMovement[I][5]); }
            Why+=FString::Printf(TEXT(" [nativeAuthorityGeneration=%d samples=%d/%d/%d/%d/%d/%d]"),
                F->AuthorityTransformGeneration,F->AuthorityTransformHistory[0].Num(),
                F->AuthorityTransformHistory[1].Num(),F->AuthorityTransformHistory[2].Num(),
                F->AuthorityTransformHistory[3].Num(),F->AuthorityTransformHistory[4].Num(),
                F->AuthorityTransformHistory[5].Num());
            for (int32 I=1;I<3;++I)
            { Why+=FString::Printf(TEXT(" [nativeClient=%d applied=%d%d%d%d%d%d matched=%d%d%d%d%d%d]"),I,
                F->SawAppliedTransform[I][0],F->SawAppliedTransform[I][1],F->SawAppliedTransform[I][2],
                F->SawAppliedTransform[I][3],F->SawAppliedTransform[I][4],F->SawAppliedTransform[I][5],
                F->SawAuthorityTransformMatch[I][0],F->SawAuthorityTransformMatch[I][1],
                F->SawAuthorityTransformMatch[I][2],F->SawAuthorityTransformMatch[I][3],
                F->SawAuthorityTransformMatch[I][4],F->SawAuthorityTransformMatch[I][5]); }
            Test->AddError(Why); ck::auto_test::net::FailSession(Why); return true;
        }
    };
    class FRestore final : public IAutomationLatentCommand
    { TSharedRef<FFixture> F; public: explicit FRestore(TSharedRef<FFixture> InF):F(InF){} bool Update() override { F->Restore(); return true; } };

    auto Run(FAutomationTestBase& Test,const FString& Topology) -> bool
    {
        auto F=MakeShared<FFixture>();
        if (!ck::auto_test::net::TryGet_DedicatedServer(Topology,F->Dedicated)) { return false; }
        F->Mode=FSoftClassPath(TEXT("/Script/Angelscript.Ck_TransformGym_GameMode")).TryLoadClass<AGameModeBase>();
        F->StateClass=FSoftClassPath(TEXT("/Script/Angelscript.Ck_TransformGym_GameState")).TryLoadClass<AGameStateBase>();
        F->PCClass=FSoftClassPath(TEXT("/Script/Angelscript.Ck_TransformGym_PlayerController")).TryLoadClass<APlayerController>();
        F->CubeClass=FSoftClassPath(TEXT("/Script/Angelscript.Ck_TransformGym_Cube")).TryLoadClass<AActor>();
        F->StationClass=FSoftClassPath(TEXT("/Script/Angelscript.Ck_EntityScript_GymStation")).TryLoadClass<UCk_EntityScript_UE>();
        if (!Test.TestTrue(TEXT("real Transform AS classes and whole public schema"),
            F->Mode && F->StateClass && F->PCClass && F->CubeClass && F->StationClass && Schema(*F, Test))) { return false; }
        const auto* Settings=GetDefault<UCkGym_StartupSettings>();
        F->Startup=Settings->StartupMode; F->Last=Settings->LastGymName; F->Recents=Settings->RecentGymNames;
        GetMutableDefault<UCkGym_StartupSettings>()->StartupMode=ECkGym_StartupMode::Cycler;
        Test.AddCommand(new FCk_Latent_StartPIEMultiClient(3,Map,Topology,F->Mode));
        Test.AddCommand(new FCk_Latent_WaitForPIEReady(3,60));
        TArray<FStep> Steps;
        auto Act=[&](FString Name,TFunction<bool()> Check) { Steps.Add({MoveTemp(Name),MoveTemp(Check),false}); };
        auto Wait=[&](FString Name,TFunction<bool()> Check) { Steps.Add({MoveTemp(Name),MoveTemp(Check),true}); };
        auto BootstrapWait=[&](FString Name,TFunction<bool()> Check) { Steps.Add({MoveTemp(Name),MoveTemp(Check),true,true}); };
        BootstrapWait(TEXT("exact three-world two-owner topology and authority shared construction"),[F]()
        {
            F->Worlds[0]=ck::auto_test::net::Get_ServerWorld();
            for (int32 I=0;I<2;++I) { F->Worlds[I+1]=ck::auto_test::net::Get_ClientWorld(I); }
            return F->Context() && Ready(*F,0);
        });
        Act(TEXT("capture real local and remote owner/pawn identities and source history"),[F]()
        {
            if (!F->Context()) { return false; }
            for (int32 I=0;I<3;++I) { F->States[I]=F->State(I); }
            if (!F->Dedicated)
            {
                auto* ServerGI=F->Worlds[0]->GetGameInstance();
                F->HostLocal=ServerGI->GetLocalPlayers()[0]->GetPlayerController(F->Worlds[0].Get());
                F->HostPawn=F->HostLocal.IsValid() ? F->HostLocal->GetPawn() : nullptr;
            }
            for (int32 I=0;I<2;++I)
            {
                F->Local[I]=F->Worlds[I+1]->GetFirstPlayerController();
                F->Remote[I]=ck::auto_test::net::TryGet_RemoteClientPlayerController(F->Worlds[0].Get(),I);
                if (!F->Local[I].IsValid() || !F->Remote[I].IsValid()) { return false; }
                F->LocalPawn[I]=F->Local[I]->GetPawn(); F->RemotePawn[I]=F->Remote[I]->GetPawn();
            }
            F->Captured=true;
            if (!F->Context()) { return false; }
            F->Observer=FWorldDelegates::OnWorldPostActorTick.AddLambda([F](UWorld* W,ELevelTick,float)
            {
                if (!F->Context()) { return; }
                SaveHistory(*F,W);
                if (W==F->Worlds[0].Get() && CurrentAuthoritySource(*F)) { F->SawDirectEcsSource=true; }
                const int32 World=W==F->Worlds[0].Get()?0:W==F->Worlds[1].Get()?1:W==F->Worlds[2].Get()?2:INDEX_NONE;
                if (World!=INDEX_NONE) { ObserveActorVisuals(*F,World); }
                if (World!=INDEX_NONE && Ready(*F,World))
                {
                    auto* ScaleCube=Cast<AActor>(Obj(F->RowType,Row(*F,F->Data(World),2),TEXT("Cube")));
                    if (IsValid(ScaleCube) &&
                        !ScaleCube->GetActorScale3D().Equals(FVector::OneVector,0.01))
                    { F->SawNonUnitScale[World]=true; }
                }
                // A client may skip the transient replicated Ready=false state. Check
                // its first usable replacement, not delivery of that state.
                if (World!=INDEX_NONE && F->Requested && !F->BoundarySchemaFailed &&
                    !F->FirstReplacementUsableObserved[World])
                {
                    bool Usable = false;
                    if (World==0)
                    {
                        if (!PureBool(F->State(0),TEXT("Get_IsReady"),Usable))
                        { F->BoundarySchemaFailed=true; F->BoundarySchemaFailure=TEXT("authority Get_IsReady invocation failed"); }
                        else
                        { Usable = Usable && Ready(*F,0) &&
                            I32(F->SnapshotType,F->Data(0),TEXT("Generation"))==F->InitialGeneration+1; }
                    }
                    else
                    {
                        auto* Client=F->Local[World-1].Get();
                        const auto Installed=PureStruct(Client,TEXT("Get_InstalledTransformSample"),F->SnapshotType);
                        if (!PureBool(Client,TEXT("Get_TransformPresentationReady"),Usable) || !Installed.IsValid())
                        { F->BoundarySchemaFailed=true; F->BoundarySchemaFailure=FString::Printf(TEXT("client world %d presentation API invocation failed"),World); }
                        else
                        { Usable = Usable &&
                            I32(F->SnapshotType,Installed->GetStructMemory(),TEXT("Generation"))==F->InitialGeneration+1; }
                    }
                    if (!F->BoundarySchemaFailed && Usable)
                    {
                        F->FirstReplacementUsableObserved[World]=true;
                        F->FirstReplacementUsableOldGone[World]=OldGone(*F,World);
                        F->PrematureReplacementUsability[World]=!F->FirstReplacementUsableOldGone[World];
                    }
                }
                if (W==F->Worlds[0].Get() && !F->ForeignAttempted &&
                    I32(F->CommitType,F->OwnerCommit(1,true),TEXT("RequestSerial"))==F->FirstSerial &&
                    I32(F->CommitType,F->OwnerCommit(1,true),TEXT("DeliverySerial"))>0 &&
                    I32(F->PCClass,F->Remote[1].Get(),TEXT("LastAcknowledgedDeliverySerial"))<
                        I32(F->CommitType,F->OwnerCommit(1,true),TEXT("DeliverySerial")))
                { F->ForeignAttempted=true; F->ForeignRejected=ForeignAckRejected(*F); }
                for (int32 Owner=0;Owner<2;++Owner)
                {
                    if (W!=F->Worlds[Owner+1].Get()) { continue; }
                    const auto* Commit=F->OwnerCommit(Owner,false);
                    const int32 Delivery=I32(F->CommitType,Commit,TEXT("DeliverySerial"));
                    if (Delivery<=0 || I32(F->PCClass,F->Remote[Owner].Get(),TEXT("LastAcknowledgedDeliverySerial"))>=Delivery) { continue; }
                    const auto Installed=PureStruct(F->Local[Owner].Get(),TEXT("Get_InstalledTransformCommit"),F->CommitType);
                    if (Installed.IsValid() && I32(F->CommitType,Installed->GetStructMemory(),TEXT("DeliverySerial"))==Delivery &&
                        SameOwnerCommitDataExceptCubes(*F,Installed->GetStructMemory(),Commit))
                    { F->SawInstalledBeforeAck[Owner]=true; }
                }
            });
            return true;
        });
        Wait(TEXT("all six real authority stations, two six-shell clients, six replicated actors per world and source-history pairs"),[F]()
        {
            for (int32 I=0;I<3;++I) { SaveHistory(*F,F->Worlds[I].Get()); if (!HasSixActual(*F,I)) { return false; } }
            if (!F->Dedicated)
            {
                TArray<FCk_Handle> HostShells; bool HostPresented=false; int32 HostShown=INDEX_NONE;
                const auto HostSample=PureStruct(F->HostLocal.Get(),TEXT("Get_InstalledTransformSample"),F->SnapshotType);
                if (!PureHandles(F->HostLocal.Get(),TEXT("Get_TransformLocalStations"),HostShells) || !HostShells.IsEmpty() ||
                    !PureBool(F->HostLocal.Get(),TEXT("Get_TransformPresentationReady"),HostPresented) || !HostPresented ||
                    !PureInt(F->HostLocal.Get(),TEXT("Get_TransformShownSequence"),HostShown) ||
                    !HostSample.IsValid() || !Complete(*F,0,HostSample->GetStructMemory()) ||
                    I32(F->SnapshotType,HostSample->GetStructMemory(),TEXT("Generation")) !=
                        I32(F->SnapshotType,F->Data(0),TEXT("Generation")) ||
                    HostShown != I32(F->SnapshotType,HostSample->GetStructMemory(),TEXT("Sequence"))) { return false; }
            }
            for (int32 I=0;I<2;++I)
            {
                TArray<FCk_Handle> Local; bool Presented=false; int32 Shown=INDEX_NONE;
                const auto Installed=PureStruct(F->Local[I].Get(),TEXT("Get_InstalledTransformSample"),F->SnapshotType);
                if (!PureHandles(F->Local[I].Get(),TEXT("Get_TransformLocalStations"),Local) || Local.Num()!=Slots ||
                    !PureBool(F->Local[I].Get(),TEXT("Get_TransformPresentationReady"),Presented) || !Presented ||
                    !PureInt(F->Local[I].Get(),TEXT("Get_TransformShownSequence"),Shown) ||
                    !Installed.IsValid() || !Complete(*F,I+1,Installed->GetStructMemory()) ||
                    Shown!=I32(F->SnapshotType,Installed->GetStructMemory(),TEXT("Sequence")) ||
                    !NoClientSimulationTimers(*F,I+1) || !MatchedSource(*F,I+1)) { return false; }
                for (int32 Slot=0;Slot<Slots;++Slot)
                {
                    const auto Tag=Tagged(F->Worlds[I+1].Get(),Slot);
                    if (!Live(Local[Slot]) || Tag.Num()!=1 || Local[Slot]!=Tag[0]) { return false; }
                    auto* Script=UCk_Utils_EntityScript_UE::TryGet_EntityScript(Local[Slot]);
                    if (!IsValid(Script) || Script->GetClass()!=F->StationClass) { return false; }
                }
            }
            return F->SawNonUnitScale[0] && F->SawNonUnitScale[1] && F->SawNonUnitScale[2] &&
                F->SawDirectEcsSource && DynamicsObserved(*F) && AllActorVisualsMoved(*F);
        });
        Act(TEXT("real authority snapshot accepts exact copy and rejects narrow recipe mutations without changing source or receipts"),[F]()
        { return RejectNarrowSnapshotMutations(*F); });
        Act(TEXT("capture actual allocated old cohort and request owner one R on the real client RPC"),[F]()
        {
            F->InitialGeneration=I32(F->SnapshotType,F->Data(0),TEXT("Generation"));
            if (F->InitialGeneration<=0 ||
                I32(F->SnapshotType,F->Data(2),TEXT("Generation"))!=F->InitialGeneration ||
                !CaptureOld(*F)) { return false; }
            F->Requested=Rpc(F->Local[1].Get(),F->FirstSerial,F->InitialGeneration);
            return F->Requested;
        });
        Wait(TEXT("old allocated closure and actors retire before each replacement first becomes usable"),[F]()
        {
            if (F->BoundarySchemaFailed) { return false; }
            for (int32 World=0;World<3;++World)
            {
                if (!F->FirstReplacementUsableObserved[World] || !F->FirstReplacementUsableOldGone[World] ||
                    F->PrematureReplacementUsability[World] || !OldGone(*F,World)) { return false; }
            }
            return true;
        });
        Wait(TEXT("owner one retains whole accepted commit, installs before ACK, and acknowledges exact delivery"),[F]()
        {
            if (!Receipt(*F,1,F->FirstSerial) || !F->SawInstalledBeforeAck[1] ||
                !F->ForeignAttempted || !F->ForeignRejected) { return false; }
            F->FirstRetained=Copy(F->CommitType,F->OwnerCommit(1,true));
            return F->FirstRetained.IsValid();
        });
        Wait(TEXT("replacement six-slot cohort and matched moving source history"),[F]()
        {
            if (!OldGone(*F,0) || !OldGone(*F,1) || !OldGone(*F,2) || !Ready(*F,0) ||
                I32(F->SnapshotType,F->Data(0),TEXT("Generation"))!=F->InitialGeneration+1) { return false; }
            for (int32 I=0;I<3;++I) { if (!HasSixActual(*F,I)) { return false; } }
            return CurrentAuthoritySource(*F) && MatchedSource(*F,1) && MatchedSource(*F,2) &&
                AllActorVisualsMoved(*F);
        });
        Act(TEXT("reject stale generation and duplicate serial without replacing accepted owner receipt"),[F]()
        {
            const auto Before=Copy(F->CommitType,F->OwnerCommit(1,true));
            if (!Before.IsValid() || !Rpc(F->Local[1].Get(),F->FirstSerial,F->InitialGeneration) ||
                !Rpc(F->Local[1].Get(),F->FirstSerial+1,F->InitialGeneration)) { return false; }
            // The replicated attempt result is checked after the network RPC has reached authority.
            return true;
        });
        Wait(TEXT("malformed stale request rejected while first accepted commit remains unchanged"),[F]()
        {
            const auto* C=F->OwnerCommit(1,true);
            return C && F->FirstRetained.IsValid() &&
                I32(F->PCClass,F->Remote[1].Get(),TEXT("LastControlAttemptSerial"))==F->FirstSerial+1 &&
                I32(F->PCClass,F->Remote[1].Get(),TEXT("LastControlResult"))>1 &&
                I32(F->PCClass,F->Remote[1].Get(),TEXT("LastControlSerial"))==F->FirstSerial &&
                Ready(*F,0) && I32(F->SnapshotType,F->Data(0),TEXT("Generation"))==F->InitialGeneration+1 &&
                F->CommitType->CompareScriptStruct(C,F->FirstRetained->GetStructMemory(),PPF_None);
        });
        Act(TEXT("request independent owner zero rebuild against its actual current generation"),[F]()
        { return Rpc(F->Local[0].Get(),F->SecondSerial,I32(F->SnapshotType,F->Data(1),TEXT("Generation"))); });
        Wait(TEXT("owner zero full installed receipt and owner one frozen receipt remain independent"),[F]()
        {
            const auto* First=F->OwnerCommit(1,true);
            return Receipt(*F,0,F->SecondSerial) && AcceptedReceipt(*F,1,F->FirstSerial) &&
                F->SawInstalledBeforeAck[0] && F->FirstRetained.IsValid() && First &&
                SameOwnerCommitDataExceptCubes(*F,First,F->FirstRetained->GetStructMemory()) &&
                I32(F->CommitType,F->OwnerCommit(0,true),TEXT("DeliverySerial"))>0;
        });
        Test.AddCommand(new FScenario(&Test,F,MoveTemp(Steps)));
        Test.AddCommand(new FRestore(F));
        Test.AddCommand(new FCk_Latent_EndPIE());
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkTransformGym_SharedWorld_Network,
    "Ck.TransformGym.Network.SharedWorld", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkTransformGym_SharedWorld_Network::RunTest(const FString& Parameters)
{ return ck_transform_shared_network::Run(*this,Parameters); }
#endif
