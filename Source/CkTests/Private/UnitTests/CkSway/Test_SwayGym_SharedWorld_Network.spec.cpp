#include "Misc/AutomationTest.h"
#include "CoreGlobals.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_StartupSettings.h"
#include "CkGym_ControlPanelTypes.h"
#include "CkSwayGym_Runtime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/Net/ReplicatedFragmentContainer/CkReplicatedFragmentContainer.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkSway/CkSway_Fragment.h"
#include "CkSway/CkSway_Utils.h"
#include "CkTween/CkTween_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Utils.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace ck_sway_shared_network
{
    constexpr int32 Worlds = 3;
    constexpr int32 Clients = 2;
    constexpr auto Map = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr const TCHAR* Tags[3] = {
        TEXT("Gym.CkSway.Turntable"), TEXT("Gym.CkSway.Shuttle"), TEXT("Gym.CkSway.Compare")};

    template<class T> auto Field(const UStruct* S, const TCHAR* N) -> T*
    { return S ? FindFProperty<T>(S, N) : nullptr; }
    auto I32(const UStruct* S, const void* D, const TCHAR* N) -> int32
    { const auto* P = Field<FIntProperty>(S, N); return D && P ? P->GetPropertyValue_InContainer(D) : INDEX_NONE; }
    auto I64(const UStruct* S, const void* D, const TCHAR* N) -> int64
    { const auto* P = Field<FInt64Property>(S, N); return D && P ? P->GetPropertyValue_InContainer(D) : -1; }
    auto Lifecycle(const UStruct* S, const void* D) -> int32
    {
        if (!D) { return INDEX_NONE; }
        if (const auto* P = Field<FByteProperty>(S, TEXT("Lifecycle")))
        { return P->GetPropertyValue_InContainer(D); }
        const auto* P = Field<FEnumProperty>(S, TEXT("Lifecycle"));
        return P && P->GetUnderlyingProperty()
            ? int32(P->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(P->ContainerPtrToValuePtr<void>(D)))
            : INDEX_NONE;
    }
    auto Active(const UStruct* S, const void* D, int32 G = 0) -> bool
    {
        return Lifecycle(S, D) == 1 && I32(S, D, TEXT("CatalogVersion")) == 1 &&
            I32(S, D, TEXT("Generation")) > 0 &&
            (G == 0 || I32(S, D, TEXT("Generation")) == G) &&
            I64(S, D, TEXT("Sequence")) > 0 && I64(S, D, TEXT("FrameSerial")) > 0;
    }
    auto Failed(const UStruct* S, const void* D, int32 G) -> bool
    { return Lifecycle(S, D) == 3 && I32(S, D, TEXT("Generation")) == G &&
        I64(S, D, TEXT("Sequence")) > 0 && I64(S, D, TEXT("FrameSerial")) == 0; }
    auto Key(int32 G, int64 Q) -> uint64
    { return (uint64(uint32(G)) << 32) ^ uint64(Q); }
    auto ArrayCount(const UStruct* S, const void* D, const TCHAR* N) -> int32
    {
        const auto* P = Field<FArrayProperty>(S, N);
        if (!P || !D) { return INDEX_NONE; }
        FScriptArrayHelper A(P, P->ContainerPtrToValuePtr<void>(D));
        return A.Num();
    }
    auto ExactValueShape(const UStruct* S, const void* D) -> bool
    {
        return Active(S, D) && ArrayCount(S,D,TEXT("SwayYawDegrees")) == 5 &&
            ArrayCount(S,D,TEXT("SwayXCm")) == 5 &&
            ArrayCount(S,D,TEXT("SwaySettled")) == 5;
    }

    struct FCall
    {
        UObject* Target = nullptr;
        UFunction* Function = nullptr;
        TUniquePtr<FStructOnScope> Args;
        FCall(UObject* T, const TCHAR* N)
        {
            if (!IsValid(T)) { return; }
            auto* F = T->FindFunction(N);
            if (!IsValid(F)) { return; }
            Target = T; Function = F; Args = MakeUnique<FStructOnScope>(F);
        }
        bool Send() const
        { if (!IsValid(Target) || !IsValid(Function) || !Args) { return false; }
          Target->ProcessEvent(Function, Args->GetStructMemory()); return true; }
    };
    template<class T> auto PureObject(UObject* Target, const TCHAR* Name) -> T*
    {
        FCall C(Target, Name);
        const auto* P = Field<FObjectPropertyBase>(C.Function, TEXT("ReturnValue"));
        return P && C.Function->NumParms == 1 && C.Send()
            ? Cast<T>(P->GetObjectPropertyValue_InContainer(C.Args->GetStructMemory())) : nullptr;
    }
    auto PureBool(UObject* Target, const TCHAR* Name) -> bool
    {
        FCall C(Target, Name);
        const auto* P = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        return P && C.Function->NumParms == 1 && C.Send() &&
            P->GetPropertyValue_InContainer(C.Args->GetStructMemory());
    }
    auto PureString(UObject* Target, const TCHAR* Name) -> FString
    {
        FCall C(Target, Name);
        const auto* P = Field<FStrProperty>(C.Function, TEXT("ReturnValue"));
        return P && C.Function->NumParms == 1 && C.Send()
            ? P->GetPropertyValue_InContainer(C.Args->GetStructMemory()) : FString{};
    }
    auto HasPresentationSample(UObject* Target, const UScriptStruct* Type) -> bool
    {
        FCall C(Target, TEXT("TryGet_PresentationSample"));
        const auto* Out = Field<FStructProperty>(C.Function, TEXT("Out"));
        const auto* Return = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        return Out && Out->Struct == Type && Return && C.Function->NumParms == 2 &&
            C.Send() && Return->GetPropertyValue_InContainer(C.Args->GetStructMemory());
    }
    auto PureStruct(UObject* Target, const TCHAR* Name, const UScriptStruct* Type) -> TSharedPtr<FStructOnScope>
    {
        FCall C(Target, Name);
        const auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!P || P->Struct != Type || C.Function->NumParms != 1 || !C.Send()) { return {}; }
        auto Copy = MakeShared<FStructOnScope>(Type);
        Type->CopyScriptStruct(Copy->GetStructMemory(), P->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory()));
        return Copy;
    }
    auto PureHandles(UObject* Target, const TCHAR* Name, TArray<FCk_Handle>& Out) -> bool
    {
        FCall C(Target, Name);
        const auto* P = Field<FArrayProperty>(C.Function, TEXT("ReturnValue"));
        const auto* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
        if (!Inner || Inner->Struct != FCk_Handle::StaticStruct() || C.Function->NumParms != 1 || !C.Send())
        { return false; }
        Out = *P->ContainerPtrToValuePtr<TArray<FCk_Handle>>(C.Args->GetStructMemory());
        return true;
    }
    auto PureHandle(UObject* Target, const TCHAR* Name) -> FCk_Handle
    {
        FCall C(Target, Name);
        const auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        return P && P->Struct == FCk_Handle::StaticStruct() && C.Function->NumParms == 1 && C.Send()
            ? *P->ContainerPtrToValuePtr<FCk_Handle>(C.Args->GetStructMemory()) : FCk_Handle{};
    }
    auto PureIndexedHandle(UObject* Target, const TCHAR* Name,
        const TCHAR* IndexName, int32 Index) -> FCk_Handle
    {
        FCall C(Target, Name);
        auto* A = Field<FIntProperty>(C.Function, IndexName);
        const auto* R = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!A || !R || R->Struct != FCk_Handle::StaticStruct() ||
            C.Function->NumParms != 2) { return {}; }
        A->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Index);
        return C.Send() ? *R->ContainerPtrToValuePtr<FCk_Handle>(C.Args->GetStructMemory())
            : FCk_Handle{};
    }
    auto NodeTag(int32 Generation, int32 Role, int32 Station, int32 Index) -> FName
    { return FName(*FString::Printf(TEXT("SwayGym_G%d_Role%d_Station%d_Index%d"),
        Generation, Role, Station, Index)); }
    auto TaggedNode(UWorld* World, int32 Generation, int32 Role,
        int32 Station, int32 Index) -> FCk_Handle
    {
        if (!IsValid(World)) { return {}; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!ck::IsValid(Root)) { return {}; }
        const auto Found = UCk_Utils_EntityTag_UE::ForEach_Entity(
            Root, NodeTag(Generation,Role,Station,Index));
        return Found.Num() == 1 ? Found[0] : FCk_Handle{};
    }
    auto Driver(const FCk_Handle& H) -> UCk_Fragment_EntityReplicationDriver_Rep*
    {
        return ck::IsValid(H) && H.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>()
            ? H.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get() : nullptr;
    }
    auto HasTransformEntries(const FCk_Handle& H, bool Applied) -> bool
    {
        auto* D = Driver(H);
        if (!IsValid(D) || D->Get_AssociatedEntity() != H) { return false; }
        for (auto* Type : {FCk_RepData_Location::StaticStruct(),
            FCk_RepData_Rotation::StaticStruct(), FCk_RepData_Scale::StaticStruct()})
        {
            const auto* E = D->FindEntry(Type);
            if (!E || !E->Data.IsValid() ||
                (Applied && (E->_PendingApply || !E->_WasEverApplied || !E->_LastAppliedData.IsValid())))
            { return false; }
        }
        return true;
    }
    auto NativePose(const FCk_Handle& H, FTransform& Pose) -> bool
    {
        if (!ck::IsValid(H) || !H.Has<ck::FFragment_Transform>() ||
            !UCk_Utils_Transform_UE::Has(H)) { return false; }
        Pose = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::Cast(H));
        return Pose.IsValid() && !Pose.ContainsNaN();
    }
    auto PoseNear(const FTransform& A, const FTransform& B) -> bool
    {
        return A.GetTranslation().Equals(B.GetTranslation(), 3.0) &&
            A.GetRotation().AngularDistance(B.GetRotation()) < FMath::DegreesToRadians(1.0) &&
            A.GetScale3D().Equals(B.GetScale3D(), 0.02);
    }
    auto Moved(const FTransform& A, const FTransform& B) -> bool
    {
        return FVector::Dist(A.GetTranslation(), B.GetTranslation()) > 5.0 ||
            A.GetRotation().AngularDistance(B.GetRotation()) > FMath::DegreesToRadians(1.0) ||
            !A.GetScale3D().Equals(B.GetScale3D(), 0.05);
    }
    auto CubeMesh(const FCk_Handle& Cube, UWorld* World) -> UStaticMeshComponent*
    {
        if (!ck::IsValid(Cube)) { return nullptr; }
        UStaticMeshComponent* Found = nullptr;
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Cube))
        {
            if (!ck::IsValid(Child) || !Child.Has<ck::FFragment_UnrealComponent>() ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) != Cube)
            { continue; }
            auto* Mesh = Cast<UStaticMeshComponent>(UCk_Utils_UnrealComponent_UE::Get_Component(
                UCk_Utils_UnrealComponent_UE::CastChecked(Child)));
            if (!IsValid(Mesh) || Mesh->GetWorld() != World || !Mesh->IsRegistered() || Found)
            { return nullptr; }
            Found = Mesh;
        }
        return Found;
    }
    auto ControlRows(UObject* Target, TArray<FCkGym_ControlRow>& Out) -> bool
    {
        FCall C(Target, TEXT("Get_SwayControlRows"));
        const auto* P = Field<FArrayProperty>(C.Function, TEXT("ReturnValue"));
        const auto* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
        if (!Inner || Inner->Struct != FCkGym_ControlRow::StaticStruct() || C.Function->NumParms != 1 || !C.Send())
        { return false; }
        Out = *P->ContainerPtrToValuePtr<TArray<FCkGym_ControlRow>>(C.Args->GetStructMemory());
        return true;
    }
    auto InvokeControl(UObject* Target, FKey Key) -> bool
    {
        TArray<FCkGym_ControlRow> Rows;
        if (!ControlRows(Target, Rows)) { return false; }
        int32 Index = INDEX_NONE;
        for (int32 I = 0; I < Rows.Num(); ++I)
        { if (Rows[I].Key == Key && Rows[I].Enabled) { if (Index != INDEX_NONE) { return false; } Index = I; } }
        FCall C(Target, TEXT("Ck_Sway_Control"));
        auto* P = Field<FIntProperty>(C.Function, TEXT("Index"));
        if (Index == INDEX_NONE || !P || C.Function->NumParms != 1) { return false; }
        P->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Index);
        return C.Send();
    }
    auto Rpc(UObject* Target, int32 Serial, int32 Generation) -> bool
    {
        FCall C(Target, TEXT("Server_RequestSwayRebuild"));
        auto* A = Field<FIntProperty>(C.Function, TEXT("Serial"));
        auto* B = Field<FIntProperty>(C.Function, TEXT("Generation"));
        if (!A || !B || C.Function->NumParms != 2 ||
            !C.Function->HasAllFunctionFlags(static_cast<EFunctionFlags>(FUNC_Net | FUNC_NetServer)) ||
            C.Function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(FUNC_NetClient | FUNC_NetMulticast | FUNC_Static)))
        { return false; }
        A->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Serial);
        B->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Generation);
        return C.Send();
    }
    auto Ack(UObject* Target, int32& Serial, int32& Generation, int32& Result) -> bool
    {
        FCall C(Target, TEXT("Get_SwayControlAck"));
        const auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!P || C.Function->NumParms != 1 || !C.Send()) { return false; }
        const auto* D = P->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory());
        Serial = I32(P->Struct, D, TEXT("RequestSerial"));
        Generation = I32(P->Struct, D, TEXT("Generation"));
        Result = I32(P->Struct, D, TEXT("Result"));
        return Serial >= 0 && Generation >= 0 && Result >= 0;
    }

    struct FRun
    {
        UClass *Mode = nullptr, *StateClass = nullptr, *PCClass = nullptr;
        FStructProperty* Snapshot = nullptr;
        UScriptStruct* SnapshotType = nullptr;
        TWeakObjectPtr<UWorld> World[Worlds];
        TWeakObjectPtr<AGameStateBase> State[Worlds];
        TWeakObjectPtr<APlayerController> Local[Clients], Remote[Clients], Host;
        TWeakObjectPtr<APawn> LocalPawn[Clients], RemotePawn[Clients], HostPawn;
        TArray<FCk_Handle> Shells[Clients], HostShells;
        FCk_Handle ShellOwner[Clients], HostShellOwner;
        TWeakObjectPtr<UCk_SwayGym_SourceReceipt_UE> FirstSource, SecondSource;
        TArray<FCk_Handle> FirstKnown, SecondKnown;
        FCk_Handle SourceOwnerRoot;
        TArray<FCk_Handle> FirstReplicas[Clients], SecondReplicas[Clients];
        TArray<FTransform> AuthorityHeadHistory[3], AuthorityCubeHistory[7];
        FTransform FirstClientHead[Clients][3], FirstClientCube[Clients][7];
        FTransform FirstClientCubeMesh[Clients][7];
        bool HasFirstClientHead[Clients][3]{}, HasFirstClientCube[Clients][7]{};
        bool HasFirstClientCubeMesh[Clients][7]{};
        bool ClientHeadMoved[Clients][3]{}, ClientCubeMoved[Clients][7]{};
        bool ClientCubeMeshMoved[Clients][7]{};
        uint64 LastMatchFrameHead[Clients][3]{}, LastMatchFrameCube[Clients][7]{};
        uint64 LastMatchFrameCubeMesh[Clients][7]{};
        int32 MatchFramesHead[Clients][3]{}, MatchFramesCube[Clients][7]{};
        int32 MatchFramesCubeMesh[Clients][7]{};
        TArray<FTransform> AppliedCubeHistory[Clients][7];
        TMap<int32,uint64> ObserverTickTypes[Worlds];
        uint64 RecorderCalls[Clients]{}, RecorderContextPasses[Clients]{};
        uint64 RecorderGenerationPasses[Clients]{}, RecorderNativePoseSamples[Clients]{};
        int32 VisualGeneration = 0;
        TMap<int64, TSharedPtr<FStructOnScope>> History;
        TMap<uint64, FCkSwayGym_Frame> SourceFrames;
        FDelegateHandle Observer;
        bool Dedicated = false, PrematureSuccessor = false, SawDynamicFrame = false;
        bool TerminalObserved = false, TerminalReactivated = false;
        bool PhysicalMismatch = false;
        int32 FirstGeneration = 0;
        int64 FirstSequence = 0;
        int32 BeforeAckSerial[Clients]{};
        ECkGym_StartupMode Startup = ECkGym_StartupMode::Cycler;
        FString Last;
        TArray<FString> Recents;

        AGameStateBase* ExactState(int32 I) const
        {
            auto* W = World[I].Get(); auto* S = IsValid(W) ? W->GetGameState() : nullptr;
            return IsValid(S) && S->GetClass() == StateClass && S->GetWorld() == W ? S : nullptr;
        }
        const void* Data(int32 I) const
        { auto* S = ExactState(I); return S && Snapshot ? Snapshot->ContainerPtrToValuePtr<void>(S) : nullptr; }
        void Restore()
        {
            if (Observer.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(Observer); Observer.Reset(); }
            auto* S = GetMutableDefault<UCkGym_StartupSettings>();
            S->StartupMode = Startup; S->LastGymName = Last; S->RecentGymNames = Recents; S->SaveConfig();
        }
    };

    auto AllGone(const TArray<FCk_Handle>& IDs) -> bool
    {
        if (IDs.Num() != 29) { return false; }
        for (const auto& H : IDs)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return false; } }
        return true;
    }
    auto ReplicasGone(const TArray<FCk_Handle>& IDs) -> bool
    {
        if (IDs.Num()!=25) { return false; }
        for (const auto& H:IDs)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return false; } }
        return true;
    }
    auto RetainedSource(FRun& F) -> bool
    {
        auto* Source = PureObject<UCk_SwayGym_SourceReceipt_UE>(F.ExactState(0), TEXT("Get_SourceReceipt"));
        if (!IsValid(Source) || UCk_SwayGym_SourceRuntime_UE::Get_RetirementState(Source) !=
            ECk_SwayGym_RetirementState::Active) { return false; }
        const auto Roles = UCk_SwayGym_SourceRuntime_UE::Get_CapturedRoles(Source);
        const auto Known = UCk_SwayGym_SourceRuntime_UE::Get_CapturedKnown(Source);
        if (Roles.Roots.Num() != 3 || Roles.Heads.Num() != 3 || Roles.Tweens.Num() != 4 ||
            Roles.Sways.Num() != 5 || Roles.CubeNodes.Num() != 7 || Roles.Components.Num() != 7 ||
            Known.Num() != 29)
        { return false; }
        TSet<FCk_Handle> Unique;
        for (const auto& H : Known)
        {
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) ||
                !H.Has<ck::FFragment_LifetimeOwner>() || Unique.Contains(H))
            { return false; }
            Unique.Add(H);
        }
        for (const auto& Set : {Roles.Roots, Roles.Heads, Roles.Tweens, Roles.Sways, Roles.CubeNodes, Roles.Components})
        { for (const auto& H : Set) { if (!Unique.Contains(H)) { return false; } } }
        auto* World = F.World[0].Get();
        const auto WorldRoot = IsValid(World)
            ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) : FCk_Handle{};
        if (!ck::IsValid(WorldRoot) || !ck::IsValid(Roles.OwnerRoot) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Roles.OwnerRoot) != WorldRoot ||
            UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(F.ExactState(0)) != Roles.OwnerRoot ||
            UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Roles.OwnerRoot) != F.ExactState(0) ||
            UCk_Utils_Net_UE::Get_EntityReplication(Roles.OwnerRoot) != ECk_Replication::Replicates ||
            !IsValid(Driver(Roles.OwnerRoot)) ||
            Driver(Roles.OwnerRoot)->GetOwningActor() != F.ExactState(0) ||
            (ck::IsValid(F.SourceOwnerRoot) && F.SourceOwnerRoot != Roles.OwnerRoot))
        { return false; }
        F.SourceOwnerRoot = Roles.OwnerRoot;
        for (int32 I=0;I<3;++I)
        {
            if (!Roles.Roots[I].Has<ck::FFragment_Transform>() ||
                Roles.Roots[I].Has<ck::FFragment_SceneNode>() ||
                !Roles.Heads[I].Has<ck::FFragment_Transform>() ||
                Roles.Heads[I].Has<ck::FFragment_SceneNode>() ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Roles.Roots[I]) != Roles.OwnerRoot ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Roles.Heads[I]) != Roles.Roots[I] ||
                TaggedNode(World,I32(F.SnapshotType,F.Data(0),TEXT("Generation")),0,I,0) != Roles.Roots[I] ||
                TaggedNode(World,I32(F.SnapshotType,F.Data(0),TEXT("Generation")),1,I,0) != Roles.Heads[I] ||
                !Roles.Roots[I].Has<ck::FFragment_EntityScript>() ||
                !Roles.Heads[I].Has<ck::FFragment_EntityScript>() ||
                !IsValid(Driver(Roles.Roots[I])) || !HasTransformEntries(Roles.Heads[I],false))
            { return false; }
        }
        for (int32 I=0;I<4;++I)
        {
            const int32 Station = I<2 ? I : 2;
            if (UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Roles.Tweens[I]) != Roles.Heads[Station] ||
                !Roles.Tweens[I].Has<ck::FFragment_Tween>()) { return false; }
        }
        for (int32 I=0;I<5;++I)
        {
            const int32 Station = I<2 ? I : 2;
            const int32 Index = I<2 ? 0 : I-2;
            if (UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Roles.Sways[I]) != Roles.Heads[Station] ||
                !Roles.Sways[I].Has<ck::FFragment_Sway>() ||
                !Roles.Sways[I].Has<ck::FFragment_SceneNode>() ||
                !Roles.Sways[I].Has<ck::FFragment_EntityScript>() ||
                !IsValid(Driver(Roles.Sways[I])) ||
                TaggedNode(World,I32(F.SnapshotType,F.Data(0),TEXT("Generation")),2,Station,Index) != Roles.Sways[I])
            { return false; }
        }
        const FCk_Handle CubeParents[7] = {
            Roles.Sways[0], Roles.Heads[0], Roles.Sways[1], Roles.Heads[1],
            Roles.Sways[2], Roles.Sways[3], Roles.Sways[4]};
        for (int32 I=0;I<7;++I)
        {
            const int32 Station = I<4 ? I/2 : 2;
            const int32 Index = I<4 ? I%2 : I-4;
            if (!Roles.CubeNodes[I].Has<ck::FFragment_SceneNode>() ||
                Roles.CubeNodes[I].Has<ck::FFragment_Sway>() ||
                !Roles.CubeNodes[I].Has<ck::FFragment_EntityScript>() ||
                !HasTransformEntries(Roles.CubeNodes[I],false) ||
                TaggedNode(World,I32(F.SnapshotType,F.Data(0),TEXT("Generation")),3,Station,Index) != Roles.CubeNodes[I] ||
                !Roles.Components[I].Has<ck::FFragment_UnrealComponent>() ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Roles.CubeNodes[I]) != CubeParents[I] ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Roles.Components[I]) != Roles.CubeNodes[I])
            { return false; }
        }
        FCkSwayGym_Frame Latest;
        if (!UCk_SwayGym_SourceRuntime_UE::TryCopyLatestFrame(Source, Latest) ||
            Latest.HeadWorldPoses.Num() != 3 || Latest.CubeWorldPoses.Num() != 7 ||
            Latest.SwayYawDegrees.Num() != 5 || Latest.SwayXCm.Num() != 5 ||
            Latest.SwaySettled.Num() != 5) { return false; }
        if (!F.FirstSource.IsValid())
        { F.FirstSource = Source; F.FirstKnown = Known; }
        else if (Source != F.FirstSource.Get() && !F.SecondSource.IsValid())
        { F.SecondSource = Source; F.SecondKnown = Known; }
        return true;
    }
    auto FrameMatchesPhysicalSource(const FCkSwayGym_SourceRoles& Roles,
        const FCkSwayGym_Frame& Frame, UWorld* World) -> bool
    {
        if (!IsValid(World) || Roles.Heads.Num() != 3 || Roles.Sways.Num() != 5 ||
            Roles.Components.Num() != 7 || Frame.HeadWorldPoses.Num() != 3 ||
            Frame.CubeWorldPoses.Num() != 7 || Frame.SwayYawDegrees.Num() != 5 ||
            Frame.SwayXCm.Num() != 5 || Frame.SwaySettled.Num() != 5)
        { return false; }
        for (int32 I = 0; I < 3; ++I)
        {
            const auto H = Roles.Heads[I];
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) ||
                !H.Has<ck::FFragment_Transform>() || H.Has<ck::FFragment_SceneNode>())
            { return false; }
            const auto Typed = UCk_Utils_Transform_UE::CastChecked(H);
            if (!UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Typed)
                .Equals(Frame.HeadWorldPoses[I], 0.01)) { return false; }
        }
        for (int32 I = 0; I < 5; ++I)
        {
            const auto H = Roles.Sways[I];
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) ||
                !H.Has<ck::FFragment_Sway>() || !H.Has<ck::FFragment_SceneNode>())
            { return false; }
            const auto Typed = UCk_Utils_Sway_UE::CastChecked(H);
            if (FMath::Abs(UCk_Utils_Sway_UE::Get_RotationOffset(Typed).Yaw -
                    Frame.SwayYawDegrees[I]) > 0.01f ||
                FMath::Abs(UCk_Utils_Sway_UE::Get_LocationOffset(Typed).X -
                    Frame.SwayXCm[I]) > 0.01f ||
                UCk_Utils_Sway_UE::Get_IsSettled(Typed) != Frame.SwaySettled[I])
            { return false; }
        }
        for (int32 I = 0; I < 7; ++I)
        {
            const auto H = Roles.Components[I];
            FTransform CubePose;
            if (!NativePose(Roles.CubeNodes[I],CubePose) ||
                !CubePose.Equals(Frame.CubeWorldPoses[I],0.01)) { return false; }
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) ||
                !H.Has<ck::FFragment_UnrealComponent>()) { return false; }
            const auto Typed = UCk_Utils_UnrealComponent_UE::CastChecked(H);
            auto* Mesh = Cast<UStaticMeshComponent>(
                UCk_Utils_UnrealComponent_UE::Get_Component(Typed));
            if (!IsValid(Mesh) || Mesh->GetWorld() != World || !Mesh->IsRegistered() ||
                !Mesh->GetComponentTransform().Equals(Frame.CubeWorldPoses[I], 0.01))
            { return false; }
        }
        return true;
    }
    auto ClientProjection(FRun& F, int32 I) -> bool
    {
        const int32 W = I + 1;
        auto* World = F.World[W].Get();
        auto* State = F.ExactState(W);
        const void* D = F.Data(W);
        if (!IsValid(World) || !IsValid(State) || !ExactValueShape(F.SnapshotType,D) ||
            PureObject<UCk_SwayGym_SourceReceipt_UE>(State,TEXT("Get_SourceReceipt")))
        { return false; }
        const int32 Generation = I32(F.SnapshotType,D,TEXT("Generation"));
        const auto Node = [World,Generation](int32 Role,int32 Station,int32 Index,
            const FCk_Handle& Parent, bool HasPose) -> FCk_Handle
        {
            const auto H = TaggedNode(World,Generation,Role,Station,Index);
            if (!ck::IsValid(H) || !H.Has<ck::FFragment_EntityScript>() ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H) != Parent ||
                UCk_Utils_Net_UE::Get_EntityReplication(H) != ECk_Replication::Replicates ||
                !IsValid(Driver(H)) ||
                !UCk_Utils_EntityReplicationDriver_UE::Get_IsReplicationComplete(H) ||
                H.Has<ck::FTag_RepFragments_PendingApply>() ||
                H.Has<ck::FFragment_SceneNode>() || H.Has<ck::FFragment_Sway>() ||
                H.Has<ck::FFragment_Tween>() ||
                (HasPose ? !HasTransformEntries(H,true) || !H.Has<ck::FFragment_Transform>()
                    : H.Has<ck::FFragment_Transform>() ||
                        Driver(H)->FindEntry(FCk_RepData_Location::StaticStruct()) ||
                        Driver(H)->FindEntry(FCk_RepData_Rotation::StaticStruct()) ||
                        Driver(H)->FindEntry(FCk_RepData_Scale::StaticStruct())))
            { return {}; }
            return H;
        };
        const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        const auto Owner = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(State);
        if (!ck::IsValid(WorldRoot) || !ck::IsValid(Owner) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Owner) != WorldRoot ||
            !IsValid(Driver(Owner)) || Driver(Owner)->Get_AssociatedEntity()!=Owner ||
            Driver(Owner)->GetOwningActor() != State)
        { return false; }
        TArray<FCk_Handle> IDs;
        FCk_Handle Roots[3], Heads[3], Sways[5];
        for (int32 Station=0;Station<3;++Station)
        {
            Roots[Station]=Node(0,Station,0,Owner,false);
            Heads[Station]=Node(1,Station,0,Roots[Station],true);
            if (!ck::IsValid(Roots[Station]) || !ck::IsValid(Heads[Station]) ||
                PureIndexedHandle(State,TEXT("Get_ReplicatedHead"),TEXT("Station"),Station)!=Heads[Station])
            { return false; }
            IDs.Add(Roots[Station]); IDs.Add(Heads[Station]);
        }
        for (int32 Slot=0;Slot<5;++Slot)
        {
            const int32 Station=Slot<2?Slot:2;
            const int32 Index=Slot<2?0:Slot-2;
            Sways[Slot]=Node(2,Station,Index,Heads[Station],false);
            if (!ck::IsValid(Sways[Slot])) { return false; }
            IDs.Add(Sways[Slot]);
        }
        const int32 CubeSway[7]={0,-1,1,-1,2,3,4};
        const int32 CubeHead[7]={0,0,1,1,2,2,2};
        for (int32 Slot=0;Slot<7;++Slot)
        {
            const int32 Station=Slot<4?Slot/2:2;
            const int32 Index=Slot<4?Slot%2:Slot-4;
            const auto Parent=CubeSway[Slot]>=0?Sways[CubeSway[Slot]]:Heads[CubeHead[Slot]];
            const auto Cube=Node(3,Station,Index,Parent,true);
            FTransform Pose;
            auto* Mesh=CubeMesh(Cube,World);
            if (!ck::IsValid(Cube) ||
                PureIndexedHandle(State,TEXT("Get_ReplicatedCube"),TEXT("Slot"),Slot)!=Cube ||
                !NativePose(Cube,Pose) || !IsValid(Mesh) ||
                !PoseNear(Pose,Mesh->GetComponentTransform()) || !Mesh->IsVisible())
            { return false; }
            IDs.Add(Cube);
            int32 Components=0;
            for (const auto& Child:UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Cube))
            {
                if (ck::IsValid(Child) && Child.Has<ck::FFragment_UnrealComponent>() &&
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child)==Cube)
                { IDs.Add(Child); ++Components; }
            }
            if (Components!=1) { return false; }
        }
        if (IDs.Num()!=25) { return false; }
        TSet<FCk_Handle> Unique;
        for (const auto& H:IDs)
        { if (Unique.Contains(H)) { return false; } Unique.Add(H); }
        auto& Captured=Generation==F.FirstGeneration?F.FirstReplicas[I]:F.SecondReplicas[I];
        if (Captured.IsEmpty()) { Captured=IDs; }
        else if (Captured!=IDs) { return false; }
        return !WorldRoot.Get_RegistryView().Has_AnyLiveEntityWith<ck::FFragment_Sway>();
    }
    auto RecordClientReplicas(FRun& F, int32 I) -> void
    {
        ++F.RecorderCalls[I];
        auto* World=F.World[I+1].Get();
        auto* State=F.ExactState(I+1);
        const int32 G=I32(F.SnapshotType,F.Data(I+1),TEXT("Generation"));
        if (!IsValid(World) || !IsValid(State) || G<=0) { return; }
        ++F.RecorderContextPasses[I];
        if (F.VisualGeneration!=G) { return; }
        ++F.RecorderGenerationPasses[I];
        static const UClass* NodeClass=FSoftClassPath(
            TEXT("/Script/Angelscript.Ck_EntityScript_SwayGym_Node")).TryLoadClass<UObject>();
        if (!IsValid(NodeClass)) { return; }
        for (int32 Role=1;Role<=3;Role+=2)
        {
            const int32 Count=Role==1?3:7;
            for (int32 Slot=0;Slot<Count;++Slot)
            {
                const bool Head=Role==1;
                const int32 Station=Head?Slot:Slot<4?Slot/2:2;
                const int32 Index=Head?0:Slot<4?Slot%2:Slot-4;
                const auto H=PureIndexedHandle(State,Head?TEXT("Get_ReplicatedHead"):
                    TEXT("Get_ReplicatedCube"),Head?TEXT("Station"):TEXT("Slot"),Slot);
                auto* D=Driver(H);
                if (!ck::IsValid(H) ||
                    H!=TaggedNode(World,G,Role,Station,Index) ||
                    UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(H)!=World ||
                    !H.Has<ck::FFragment_EntityScript>() ||
                    !IsValid(H.Get<ck::FFragment_EntityScript>().Get_Script().Get()) ||
                    H.Get<ck::FFragment_EntityScript>().Get_Script()->GetClass()!=NodeClass ||
                    !IsValid(D) || D->Get_AssociatedEntity()!=H ||
                    H.Has<ck::FTag_RepFragments_PendingApply>() ||
                    !HasTransformEntries(H,true)) { continue; }
                if (H.Has<ck::FFragment_Transform_Requests>())
                {
                    const auto& Requests=H.Get<ck::FFragment_Transform_Requests>();
                    if (!Requests.Get_LocationRequests().IsEmpty() ||
                        !Requests.Get_RotationRequests().IsEmpty() ||
                        Requests.Get_ScaleRequests().IsSet() ||
                        !Requests.Get_ForceRefreshRequests().IsEmpty())
                    { continue; }
                }
                const auto* L=D->FindEntry(FCk_RepData_Location::StaticStruct());
                const auto* R=D->FindEntry(FCk_RepData_Rotation::StaticStruct());
                const auto* S=D->FindEntry(FCk_RepData_Scale::StaticStruct());
                if (!L || !R || !S ||
                    L->_LastAppliedData.GetScriptStruct()!=FCk_RepData_Location::StaticStruct() ||
                    R->_LastAppliedData.GetScriptStruct()!=FCk_RepData_Rotation::StaticStruct() ||
                    S->_LastAppliedData.GetScriptStruct()!=FCk_RepData_Scale::StaticStruct())
                { continue; }
                FTransform Pose;
                const FTransform Applied(R->_LastAppliedData.Get<FCk_RepData_Rotation>().Value,
                    L->_LastAppliedData.Get<FCk_RepData_Location>().Value,
                    S->_LastAppliedData.Get<FCk_RepData_Scale>().Value);
                if (!NativePose(H,Pose) || !PoseNear(Pose,Applied)) { continue; }
                ++F.RecorderNativePoseSamples[I];
                auto& First=Head?F.FirstClientHead[I][Slot]:F.FirstClientCube[I][Slot];
                auto& HasFirst=Head?F.HasFirstClientHead[I][Slot]:F.HasFirstClientCube[I][Slot];
                auto& SawMoved=Head?F.ClientHeadMoved[I][Slot]:F.ClientCubeMoved[I][Slot];
                auto& LastFrame=Head?F.LastMatchFrameHead[I][Slot]:F.LastMatchFrameCube[I][Slot];
                auto& Matches=Head?F.MatchFramesHead[I][Slot]:F.MatchFramesCube[I][Slot];
                const auto& History=Head?F.AuthorityHeadHistory[Slot]:F.AuthorityCubeHistory[Slot];
                if (!HasFirst) { First=Pose; HasFirst=true; }
                else { SawMoved |= Moved(Pose,First); }
                if (SawMoved && History.Num()>=2)
                {
                    for (const auto& Authority:History)
                    {
                        if (Moved(Authority,History[0]) && PoseNear(Pose,Authority))
                        {
                            if (LastFrame!=GFrameCounter)
                            { LastFrame=GFrameCounter; Matches=FMath::Min(Matches+1,2); }
                            break;
                        }
                    }
                }
                if (Head) { continue; }
                auto& AppliedHistory=F.AppliedCubeHistory[I][Slot];
                auto* Mesh=CubeMesh(H,World);
                if (IsValid(Mesh) && Mesh->IsVisible())
                {
                    const auto MeshPose=Mesh->GetComponentTransform();
                    bool MatchesApplied=false,MatchesAuthority=false;
                    for (const auto& Previous:AppliedHistory)
                    { if (PoseNear(MeshPose,Previous)) { MatchesApplied=true; break; } }
                    if (History.Num()>=2)
                    {
                        for (const auto& Authority:History)
                        {
                            if (Moved(Authority,History[0]) && PoseNear(MeshPose,Authority))
                            { MatchesAuthority=true; break; }
                        }
                    }
                    auto& MeshFirst=F.FirstClientCubeMesh[I][Slot];
                    auto& HasMeshFirst=F.HasFirstClientCubeMesh[I][Slot];
                    auto& MeshMoved=F.ClientCubeMeshMoved[I][Slot];
                    auto& MeshLastFrame=F.LastMatchFrameCubeMesh[I][Slot];
                    auto& MeshMatches=F.MatchFramesCubeMesh[I][Slot];
                    if (!HasMeshFirst) { MeshFirst=MeshPose; HasMeshFirst=true; }
                    else { MeshMoved |= Moved(MeshPose,MeshFirst); }
                    if (MeshMoved && MatchesApplied && MatchesAuthority &&
                        MeshLastFrame!=GFrameCounter)
                    { MeshLastFrame=GFrameCounter; MeshMatches=FMath::Min(MeshMatches+1,2); }
                }
                AppliedHistory.Add(Pose);
                if (AppliedHistory.Num()>2048) { AppliedHistory.RemoveAt(0); }
            }
        }
    }
    auto ReplicasMatched(const FRun& F) -> bool
    {
        for (int32 I=0;I<Clients;++I)
        {
            for (int32 Slot=0;Slot<3;++Slot)
            { if (!F.ClientHeadMoved[I][Slot] || F.MatchFramesHead[I][Slot]<2) { return false; } }
            for (int32 Slot=0;Slot<7;++Slot)
            { if (!F.ClientCubeMoved[I][Slot] || F.MatchFramesCube[I][Slot]<2 ||
                  !F.ClientCubeMeshMoved[I][Slot] || F.MatchFramesCubeMesh[I][Slot]<2)
              { return false; } }
        }
        return true;
    }
    auto LogPoseDriverEntries(const FCk_Handle& H, int32 World, bool Head, int32 Slot) -> void
    {
        FTransform Pose;
        const bool HasPose=NativePose(H,Pose);
        auto* D=Driver(H);
        const auto* L=IsValid(D)?D->FindEntry(FCk_RepData_Location::StaticStruct()):nullptr;
        const auto* R=IsValid(D)?D->FindEntry(FCk_RepData_Rotation::StaticStruct()):nullptr;
        const auto* S=IsValid(D)?D->FindEntry(FCk_RepData_Scale::StaticStruct()):nullptr;
        const bool CurrentL=L && L->Data.GetScriptStruct()==FCk_RepData_Location::StaticStruct();
        const bool CurrentR=R && R->Data.GetScriptStruct()==FCk_RepData_Rotation::StaticStruct();
        const bool CurrentS=S && S->Data.GetScriptStruct()==FCk_RepData_Scale::StaticStruct();
        const bool AppliedL=L && L->_WasEverApplied &&
            L->_LastAppliedData.GetScriptStruct()==FCk_RepData_Location::StaticStruct();
        const bool AppliedR=R && R->_WasEverApplied &&
            R->_LastAppliedData.GetScriptStruct()==FCk_RepData_Rotation::StaticStruct();
        const bool AppliedS=S && S->_WasEverApplied &&
            S->_LastAppliedData.GetScriptStruct()==FCk_RepData_Scale::StaticStruct();
        const auto DeltaL=[&](bool Valid,const FInstancedStruct* Data) -> double
        { return HasPose && Valid ? FVector::Dist(Pose.GetTranslation(),
            Data->Get<FCk_RepData_Location>().Value) : -1.0; };
        const auto DeltaR=[&](bool Valid,const FInstancedStruct* Data) -> double
        { return HasPose && Valid ? FMath::RadiansToDegrees(Pose.GetRotation().AngularDistance(
            Data->Get<FCk_RepData_Rotation>().Value)) : -1.0; };
        const auto DeltaS=[&](bool Valid,const FInstancedStruct* Data) -> double
        { return HasPose && Valid ? FVector::Dist(Pose.GetScale3D(),
            Data->Get<FCk_RepData_Scale>().Value) : -1.0; };
        int32 LocationRequests=0,RotationRequests=0,ScaleRequests=0,RefreshRequests=0;
        if (ck::IsValid(H) && H.Has<ck::FFragment_Transform_Requests>())
        {
            const auto& Requests=H.Get<ck::FFragment_Transform_Requests>();
            LocationRequests=Requests.Get_LocationRequests().Num();
            RotationRequests=Requests.Get_RotationRequests().Num();
            ScaleRequests=Requests.Get_ScaleRequests().IsSet();
            RefreshRequests=Requests.Get_ForceRefreshRequests().Num();
        }
        UE_LOG(LogTemp,Display,TEXT("Sway driver world%d %s%d assoc=%d pose=%d current(LRS)=%d%d%d pending(LRS)=%d%d%d applied(LRS)=%d%d%d currentDelta(cm,deg,scale)=%.3f/%.3f/%.3f lastAppliedDelta=%.3f/%.3f/%.3f transformRequests(LRSA)=%d/%d/%d/%d"),
            World,Head?TEXT("head"):TEXT("cube"),Slot,
            IsValid(D) && D->Get_AssociatedEntity()==H,HasPose,
            CurrentL,CurrentR,CurrentS,L&&L->_PendingApply,R&&R->_PendingApply,S&&S->_PendingApply,
            AppliedL,AppliedR,AppliedS,
            DeltaL(CurrentL,L?&L->Data:nullptr),DeltaR(CurrentR,R?&R->Data:nullptr),
            DeltaS(CurrentS,S?&S->Data:nullptr),
            DeltaL(AppliedL,L?&L->_LastAppliedData:nullptr),
            DeltaR(AppliedR,R?&R->_LastAppliedData:nullptr),
            DeltaS(AppliedS,S?&S->_LastAppliedData:nullptr),
            LocationRequests,RotationRequests,ScaleRequests,RefreshRequests);
    }
    auto LogReplicaMatchDiagnostics(const FRun& F) -> void
    {
        auto* Source=PureObject<UCk_SwayGym_SourceReceipt_UE>(F.ExactState(0),TEXT("Get_SourceReceipt"));
        if (IsValid(Source))
        {
            const auto Roles=UCk_SwayGym_SourceRuntime_UE::Get_CapturedRoles(Source);
            for (int32 Slot=0;Slot<Roles.Heads.Num();++Slot)
            { LogPoseDriverEntries(Roles.Heads[Slot],0,true,Slot); }
            for (int32 Slot=0;Slot<Roles.CubeNodes.Num();++Slot)
            { LogPoseDriverEntries(Roles.CubeNodes[Slot],0,false,Slot); }
        }
        for (int32 I=0;I<Clients;++I)
        {
            FString TickCounts;
            for (const auto& Pair:F.ObserverTickTypes[I+1])
            { TickCounts+=FString::Printf(TEXT(" type%d=%llu"),Pair.Key,Pair.Value); }
            UE_LOG(LogTemp,Display,TEXT("Sway replica observer client%d calls=%llu contextPasses=%llu generationPasses=%llu nativePoseSamples=%llu tickTypes:%s visualGeneration=%d wireGeneration=%d"),
                I,F.RecorderCalls[I],F.RecorderContextPasses[I],
                F.RecorderGenerationPasses[I],F.RecorderNativePoseSamples[I],*TickCounts,
                F.VisualGeneration,I32(F.SnapshotType,F.Data(I+1),TEXT("Generation")));
            auto* State=F.ExactState(I+1);
            if (!IsValid(State)) { continue; }
            for (int32 Role=1;Role<=3;Role+=2)
            {
                const bool Head=Role==1;
                const int32 Count=Head?3:7;
                for (int32 Slot=0;Slot<Count;++Slot)
                {
                    const auto H=PureIndexedHandle(State,Head?TEXT("Get_ReplicatedHead"):
                        TEXT("Get_ReplicatedCube"),Head?TEXT("Station"):TEXT("Slot"),Slot);
                    FTransform Pose;
                    const bool HasPose=NativePose(H,Pose);
                    const bool HasFirst=Head?F.HasFirstClientHead[I][Slot]:F.HasFirstClientCube[I][Slot];
                    const auto& First=Head?F.FirstClientHead[I][Slot]:F.FirstClientCube[I][Slot];
                    const bool SawMoved=Head?F.ClientHeadMoved[I][Slot]:F.ClientCubeMoved[I][Slot];
                    const int32 Matches=Head?F.MatchFramesHead[I][Slot]:F.MatchFramesCube[I][Slot];
                    const auto& History=Head?F.AuthorityHeadHistory[Slot]:F.AuthorityCubeHistory[Slot];
                    int32 MovedAuthority=0;
                    double BestLocation=TNumericLimits<double>::Max();
                    double BestRotation=TNumericLimits<double>::Max();
                    double BestScale=TNumericLimits<double>::Max();
                    bool NearMovedAuthority=false;
                    if (HasPose && History.Num()>0)
                    {
                        for (const auto& Authority:History)
                        {
                            if (!Moved(Authority,History[0])) { continue; }
                            ++MovedAuthority;
                            BestLocation=FMath::Min(BestLocation,FVector::Dist(
                                Pose.GetTranslation(),Authority.GetTranslation()));
                            BestRotation=FMath::Min(BestRotation,FMath::RadiansToDegrees(
                                Pose.GetRotation().AngularDistance(Authority.GetRotation())));
                            BestScale=FMath::Min(BestScale,FVector::Dist(
                                Pose.GetScale3D(),Authority.GetScale3D()));
                            NearMovedAuthority|=PoseNear(Pose,Authority);
                        }
                    }
                    UE_LOG(LogTemp,Display,TEXT("Sway replica client%d %s%d handle=%d pose=%d first=%d moved=%d matches=%d history=%d movedAuthority=%d nearMoved=%d bestDelta(cm,deg,scale)=%.3f/%.3f/%.3f current=%s firstPose=%s"),
                        I,Head?TEXT("head"):TEXT("cube"),Slot,ck::IsValid(H),HasPose,HasFirst,
                        SawMoved,Matches,History.Num(),MovedAuthority,NearMovedAuthority,
                        BestLocation,BestRotation,BestScale,
                        HasPose?*Pose.ToString():TEXT("<none>"),
                        HasFirst?*First.ToString():TEXT("<none>"));
                    LogPoseDriverEntries(H,I+1,Head,Slot);
                    if (!Head)
                    {
                        auto* Mesh=CubeMesh(H,F.World[I+1].Get());
                        const bool Visible=IsValid(Mesh) && Mesh->IsVisible();
                        const auto MeshPose=Visible?Mesh->GetComponentTransform():FTransform::Identity;
                        const auto& AppliedHistory=F.AppliedCubeHistory[I][Slot];
                        bool NearApplied=false,NearAuthority=false;
                        for (const auto& Applied:AppliedHistory)
                        { if (Visible && PoseNear(MeshPose,Applied)) { NearApplied=true; break; } }
                        if (Visible && History.Num()>0)
                        {
                            for (const auto& Authority:History)
                            { if (Moved(Authority,History[0]) && PoseNear(MeshPose,Authority))
                                { NearAuthority=true; break; } }
                        }
                        UE_LOG(LogTemp,Display,TEXT("Sway mesh client%d cube%d visible=%d first=%d moved=%d matches=%d appliedHistory=%d nearApplied=%d nearMovedAuthority=%d current=%s firstPose=%s"),
                            I,Slot,Visible,F.HasFirstClientCubeMesh[I][Slot],
                            F.ClientCubeMeshMoved[I][Slot],F.MatchFramesCubeMesh[I][Slot],
                            AppliedHistory.Num(),NearApplied,NearAuthority,
                            Visible?*MeshPose.ToString():TEXT("<none>"),
                            F.HasFirstClientCubeMesh[I][Slot]
                                ?*F.FirstClientCubeMesh[I][Slot].ToString():TEXT("<none>"));
                    }
                }
            }
        }
    }
    auto Context(FRun& F) -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        if (!IsValid(Server) || Server->GetNetMode() !=
            (F.Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
            !IsValid(Server->GetAuthGameMode()) || Server->GetAuthGameMode()->GetClass() != F.Mode ||
            ck::auto_test::net::Get_NumClientWorlds() != Clients)
        { return false; }
        F.World[0] = Server;
        if (!IsValid(F.ExactState(0))) { return false; }
        for (int32 I = 0; I < Clients; ++I)
        {
            auto* W = ck::auto_test::net::Get_ClientWorld(I);
            if (!IsValid(W) || W == Server || W->GetNetMode() != NM_Client ||
                (I == 1 && W == F.World[1].Get())) { return false; }
            F.World[I+1] = W;
            auto* P = W->GetFirstPlayerController();
            auto* R = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server,I);
            if (!IsValid(P) || !IsValid(R) || P->GetClass() != F.PCClass ||
                R->GetClass() != F.PCClass || !P->IsLocalController() || !R->HasAuthority() ||
                !IsValid(P->GetPawn()) || !IsValid(R->GetPawn()) || !IsValid(F.ExactState(I+1)))
            { return false; }
            F.Local[I] = P; F.Remote[I] = R;
            F.LocalPawn[I] = P->GetPawn(); F.RemotePawn[I] = R->GetPawn();
        }
        if (F.Local[0] == F.Local[1] || F.Remote[0] == F.Remote[1] ||
            F.LocalPawn[0] == F.LocalPawn[1] || F.RemotePawn[0] == F.RemotePawn[1])
        { return false; }
        auto* GI = Server->GetGameInstance();
        if (!IsValid(GI) || GI->GetLocalPlayers().Num() != (F.Dedicated ? 0 : 1))
        { return false; }
        if (!F.Dedicated)
        {
            auto* LP = GI->GetLocalPlayers()[0];
            auto* Host = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
            if (!IsValid(Host) || Host->GetClass() != F.PCClass || !Host->IsLocalController() ||
                !Host->HasAuthority() || !IsValid(Host->GetPawn()) ||
                Host == F.Remote[0] || Host == F.Remote[1]) { return false; }
            F.Host = Host; F.HostPawn = Host->GetPawn();
        }
        return true;
    }
    auto SameSourceSampleOnWire(const FRun& F, const void* Source,
        const void* Client) -> bool
    { return F.Snapshot && Source && Client && F.Snapshot->Identical(Source,Client,0); }
    auto CurrentObserved(const FRun& F, int32 I) -> bool
    {
        const auto* D = F.Data(I);
        if (!ExactValueShape(F.SnapshotType,D) || !F.Snapshot) { return false; }
        const auto* Old = F.History.Find(I64(F.SnapshotType,D,TEXT("Sequence")));
        if (!Old || !Old->IsValid()) { return false; }
        const void* Source = (*Old)->GetStructMemory();
        return I == 0 ? F.Snapshot->Identical(D,Source,0) :
            SameSourceSampleOnWire(F,Source,D);
    }
    auto AuthorityFrameIsPhysical(const FRun& F) -> bool
    {
        const auto* D = F.Data(0);
        if (!ExactValueShape(F.SnapshotType,D)) { return false; }
        const int32 G = I32(F.SnapshotType,D,TEXT("Generation"));
        const int64 Serial = I64(F.SnapshotType,D,TEXT("FrameSerial"));
        const auto* Frame = F.SourceFrames.Find(Key(G,Serial));
        if (!Frame || Frame->Generation != G || Frame->FrameSerial != Serial) { return false; }
        const auto* YawP = Field<FArrayProperty>(F.SnapshotType,TEXT("SwayYawDegrees"));
        const auto* XP = Field<FArrayProperty>(F.SnapshotType,TEXT("SwayXCm"));
        const auto* SettledP = Field<FArrayProperty>(F.SnapshotType,TEXT("SwaySettled"));
        if (!YawP || !XP || !SettledP) { return false; }
        const auto* Yaw = YawP->ContainerPtrToValuePtr<TArray<float>>(D);
        const auto* X = XP->ContainerPtrToValuePtr<TArray<float>>(D);
        const auto* Settled = SettledP->ContainerPtrToValuePtr<TArray<bool>>(D);
        if (!Yaw || !X || !Settled ||
            Yaw->Num()!=5 || X->Num()!=5 || Settled->Num()!=5)
        { return false; }
        for (int32 I=0;I<5;++I)
        {
            if (FMath::Abs((*Yaw)[I]-Frame->SwayYawDegrees[I])>0.01f ||
                FMath::Abs((*X)[I]-Frame->SwayXCm[I])>0.01f ||
                (*Settled)[I]!=Frame->SwaySettled[I]) { return false; }
        }
        return true;
    }
    auto CaptureStations(FRun& F) -> bool
    {
        for (int32 I = 0; I < Clients; ++I)
        {
            auto* P = F.Local[I].Get();
            TArray<FCk_Handle> Shells;
            if (!IsValid(P) || !PureBool(P,TEXT("Get_SwayPresentationReady")) ||
                !PureHandles(P,TEXT("Get_CapturedSwayStations"),Shells) || Shells.Num() != 3)
            { return false; }
            const auto Owner = PureHandle(P,TEXT("Get_CapturedSwayStationOwner"));
            if (!ck::IsValid(Owner)) { return false; }
            TSet<FCk_Handle> Unique;
            for (int32 J = 0; J < 3; ++J)
            {
                if (!ck::IsValid(Shells[J]) || Unique.Contains(Shells[J]) ||
                    !Shells[J].Has<ck::FFragment_LifetimeOwner>() ||
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Shells[J]) != Owner)
                { return false; }
                Unique.Add(Shells[J]);
            }
            if (F.Shells[I].IsEmpty()) { F.Shells[I] = Shells; F.ShellOwner[I] = Owner; }
            else if (F.Shells[I] != Shells || F.ShellOwner[I] != Owner) { return false; }
            const auto Installed = PureStruct(P,TEXT("Get_InstalledSwaySample"),F.SnapshotType);
            if (!Installed.IsValid() || !F.Snapshot->Identical(Installed->GetStructMemory(),F.Data(I+1),0))
            { return false; }
        }
        return true;
    }
    auto Row(UObject* PC, FKey Key, FCkGym_ControlRow& Out) -> bool
    {
        TArray<FCkGym_ControlRow> Rows;
        if (!ControlRows(PC,Rows)) { return false; }
        bool Found = false;
        for (const auto& Candidate : Rows)
        {
            if (Candidate.Key != Key) { continue; }
            if (Found) { return false; }
            Out = Candidate; Found = true;
        }
        return Found;
    }
    auto CameraAt(UObject* PC, const FRun& F, int32 Slot) -> bool
    {
        auto* Controller = Cast<APlayerController>(PC);
        APawn* const Pawn = IsValid(Controller) ? Controller->GetPawn().Get() : nullptr;
        auto* State=IsValid(Controller)?Controller->GetWorld()->GetGameState():nullptr;
        const auto Head=PureIndexedHandle(State,TEXT("Get_ReplicatedHead"),TEXT("Station"),Slot);
        FTransform Pose;
        if (!IsValid(Pawn) || Slot<0 || Slot>=3 || !NativePose(Head,Pose)) { return false; }
        const auto Target = Pose.GetLocation();
        return Pawn->GetActorLocation().Equals(Target + FVector(-700,-900,450), 2.0);
    }
    auto TerminalPresentationHidden(const FRun& F, int32 I) -> bool
    {
        if (I < 0 || I >= Clients ||
            PureBool(F.Local[I].Get(),TEXT("Get_SwayPresentationReady")))
        { return false; }
        auto* State=F.ExactState(I+1);
        if (!IsValid(State) || F.SecondReplicas[I].Num()!=25) { return false; }
        for (const auto& H:F.SecondReplicas[I])
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return false; } }
        for (int32 Station=0;Station<3;++Station)
        { if (ck::IsValid(PureIndexedHandle(State,TEXT("Get_ReplicatedHead"),TEXT("Station"),Station)))
            { return false; } }
        for (int32 Slot=0;Slot<7;++Slot)
        { if (ck::IsValid(PureIndexedHandle(State,TEXT("Get_ReplicatedCube"),TEXT("Slot"),Slot)))
            { return false; } }
        return true;
    }
    auto Observe(FRun& F, UWorld* W, ELevelTick TickType) -> void
    {
        if (!F.SnapshotType) { return; }
        for (int32 I=0;I<Worlds;++I)
        { if (W==F.World[I].Get()) { ++F.ObserverTickTypes[I].FindOrAdd(int32(TickType)); break; } }
        if (TickType != LEVELTICK_All) { return; }
        for (int32 I=0;I<Clients;++I)
        { if (W==F.World[I+1].Get()) { RecordClientReplicas(F,I); return; } }
        if (W != F.World[0].Get()) { return; }
        auto* Source = PureObject<UCk_SwayGym_SourceReceipt_UE>(F.ExactState(0),TEXT("Get_SourceReceipt"));
        FCkSwayGym_Frame Frame;
        if (IsValid(Source) &&
            UCk_SwayGym_SourceRuntime_UE::Get_RetirementState(Source) ==
                ECk_SwayGym_RetirementState::Active &&
            UCk_SwayGym_SourceRuntime_UE::TryCopyLatestFrame(Source,Frame) &&
            !F.SourceFrames.Contains(Key(Frame.Generation,Frame.FrameSerial)))
        {
            const auto Roles = UCk_SwayGym_SourceRuntime_UE::Get_CapturedRoles(Source);
            if (!FrameMatchesPhysicalSource(Roles,Frame,W))
            { F.PhysicalMismatch = true; }
            else
            {
                if (F.VisualGeneration!=Frame.Generation)
                {
                    F.VisualGeneration=Frame.Generation;
                    for (auto& History:F.AuthorityHeadHistory) { History.Reset(); }
                    for (auto& History:F.AuthorityCubeHistory) { History.Reset(); }
                    for (int32 Client=0;Client<Clients;++Client)
                    {
                        for (int32 Slot=0;Slot<3;++Slot)
                        {
                            F.HasFirstClientHead[Client][Slot]=false;
                            F.ClientHeadMoved[Client][Slot]=false;
                            F.MatchFramesHead[Client][Slot]=0;
                            F.LastMatchFrameHead[Client][Slot]=0;
                        }
                        for (int32 Slot=0;Slot<7;++Slot)
                        {
                            F.HasFirstClientCube[Client][Slot]=false;
                            F.ClientCubeMoved[Client][Slot]=false;
                            F.MatchFramesCube[Client][Slot]=0;
                            F.LastMatchFrameCube[Client][Slot]=0;
                            F.HasFirstClientCubeMesh[Client][Slot]=false;
                            F.ClientCubeMeshMoved[Client][Slot]=false;
                            F.MatchFramesCubeMesh[Client][Slot]=0;
                            F.LastMatchFrameCubeMesh[Client][Slot]=0;
                            F.AppliedCubeHistory[Client][Slot].Reset();
                        }
                    }
                }
                F.SourceFrames.Add(Key(Frame.Generation,Frame.FrameSerial),Frame);
                for (int32 Slot=0;Slot<3;++Slot)
                { F.AuthorityHeadHistory[Slot].Add(Frame.HeadWorldPoses[Slot]); }
                for (int32 Slot=0;Slot<7;++Slot)
                { F.AuthorityCubeHistory[Slot].Add(Frame.CubeWorldPoses[Slot]); }
            }
        }
        const auto* D = F.Data(0);
        if (Failed(F.SnapshotType,D,F.FirstGeneration+1)) { F.TerminalObserved = true; }
        else if (F.TerminalObserved && Active(F.SnapshotType,D)) { F.TerminalReactivated = true; }
        if (!ExactValueShape(F.SnapshotType,D)) { return; }
        const int32 G = I32(F.SnapshotType,D,TEXT("Generation"));
        const int64 Q = I64(F.SnapshotType,D,TEXT("Sequence"));
        if (G == F.FirstGeneration + 1 && !F.FirstKnown.IsEmpty() && !AllGone(F.FirstKnown))
        { F.PrematureSuccessor = true; }
        if (const auto* Existing = F.History.Find(Q))
        {
            if (!F.Snapshot->Identical(D,(*Existing)->GetStructMemory(),0))
            { F.PrematureSuccessor = true; }
            return;
        }
        auto Copy = MakeShared<FStructOnScope>(F.SnapshotType);
        F.SnapshotType->CopyScriptStruct(Copy->GetStructMemory(),D);
        F.History.Add(Q,Copy);
        if (G == F.FirstGeneration && I64(F.SnapshotType,D,TEXT("FrameSerial")) > F.FirstSequence + 1)
        { F.SawDynamicFrame = true; }
    }
    class FRestore final : public IAutomationLatentCommand
    {
        TSharedRef<FRun> F;
    public:
        explicit FRestore(TSharedRef<FRun> InF) : F(InF) {}
        bool Update() override { F->Restore(); return true; }
    };
    auto Run(FAutomationTestBase& Test, const FString& Topology) -> bool
    {
        auto F = MakeShared<FRun>();
        if (!ck::auto_test::net::TryGet_DedicatedServer(Topology,F->Dedicated))
        { return false; }
        F->Mode = FSoftClassPath(TEXT("/Script/Angelscript.Ck_SwayGym_GameMode"))
            .TryLoadClass<AGameModeBase>();
        F->StateClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_SwayGym_GameState"))
            .TryLoadClass<AGameStateBase>();
        F->PCClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_SwayGym_PlayerController"))
            .TryLoadClass<APlayerController>();
        F->Snapshot = Field<FStructProperty>(F->StateClass,TEXT("Snapshot"));
        F->SnapshotType = F->Snapshot ? F->Snapshot->Struct.Get() : nullptr;
        if (!Test.TestTrue(TEXT("real Sway AS classes and value-only snapshot schema"),
            F->Mode && F->StateClass && F->PCClass && F->SnapshotType &&
            Field<FIntProperty>(F->SnapshotType,TEXT("Generation")) &&
            Field<FInt64Property>(F->SnapshotType,TEXT("Sequence")) &&
            Field<FInt64Property>(F->SnapshotType,TEXT("FrameSerial")) &&
            !F->SnapshotType->FindPropertyByName(TEXT("HeadWorldPoses")) &&
            !F->SnapshotType->FindPropertyByName(TEXT("CubeWorldPoses")) &&
            !F->StateClass->FindFunctionByName(TEXT("Get_PassiveReceipt")) &&
            F->StateClass->FindFunctionByName(TEXT("Get_ReplicatedHead")) &&
            F->StateClass->FindFunctionByName(TEXT("Get_ReplicatedCube")) &&
            Field<FArrayProperty>(F->SnapshotType,TEXT("SwayYawDegrees")) &&
            Field<FArrayProperty>(F->SnapshotType,TEXT("SwayXCm")) &&
            Field<FArrayProperty>(F->SnapshotType,TEXT("SwaySettled"))))
        { return false; }
        const auto* Settings = GetDefault<UCkGym_StartupSettings>();
        F->Startup = Settings->StartupMode;
        F->Last = Settings->LastGymName;
        F->Recents = Settings->RecentGymNames;
        GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
        // Multicast delegates invoke newer registrations first. Register before PIE
        // so the native post-tick sampler runs before this physical observer.
        F->Observer = FWorldDelegates::OnWorldPostActorTick.AddLambda(
            [F](UWorld* W,ELevelTick TickType,float) { Observe(*F,W,TickType); });
        Test.AddCommand(new FCk_Latent_StartPIEMultiClient(Worlds,Map,Topology,F->Mode));
        Test.AddCommand(new FCk_Latent_WaitForPIEReady(Worlds,60));
        auto InitialSince = MakeShared<double>(-1.0);
        auto InitialLogged = MakeShared<bool>(false);
        Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([F,InitialSince,InitialLogged]() -> bool
            {
                if (*InitialSince < 0.0) { *InitialSince = FPlatformTime::Seconds(); }
                const TCHAR* Blocked = TEXT("Context");
                const bool Ready = [&]() -> bool
                {
                    if (!Context(*F)) { return false; }
                    Blocked = TEXT("AuthorityShape");
                    if (!ExactValueShape(F->SnapshotType,F->Data(0))) { return false; }
                    if (F->FirstGeneration == 0)
                    {
                        F->FirstGeneration = I32(F->SnapshotType,F->Data(0),TEXT("Generation"));
                        F->FirstSequence = I64(F->SnapshotType,F->Data(0),TEXT("FrameSerial"));
                    }
                    Blocked = TEXT("RetainedSource");
                    if (!RetainedSource(*F)) { return false; }
                    Blocked = TEXT("CaptureStations");
                    if (!CaptureStations(*F)) { return false; }
                    for (int32 I=0;I<Clients;++I)
                    {
                        Blocked = I == 0 ? TEXT("Client0Shape") : TEXT("Client1Shape");
                        if (!ExactValueShape(F->SnapshotType,F->Data(I+1))) { return false; }
                        Blocked = I == 0 ? TEXT("Client0Projection") : TEXT("Client1Projection");
                        if (!ClientProjection(*F,I)) { return false; }
                    }
                    return true;
                }();
                if (!Ready && !*InitialLogged &&
                    FPlatformTime::Seconds() - *InitialSince >= 45.0)
                {
                    *InitialLogged = true;
                    UE_LOG(LogTemp, Display, TEXT("Sway initial wait blocked=%s worlds=%d gen=%d history=%d sourceFrames=%d"),
                        Blocked, ck::auto_test::net::Get_NumClientWorlds(),
                        F->FirstGeneration, F->History.Num(), F->SourceFrames.Num());
                    if (const auto* D = F->Data(0))
                    {
                        UE_LOG(LogTemp, Display, TEXT("Sway authority wire gen=%d seq=%lld frame=%lld lifecycle=%d"),
                            I32(F->SnapshotType,D,TEXT("Generation")),
                            I64(F->SnapshotType,D,TEXT("Sequence")),
                            I64(F->SnapshotType,D,TEXT("FrameSerial")), Lifecycle(F->SnapshotType,D));
                    }
                    if (auto* S = F->ExactState(0))
                    {
                        auto* Source = PureObject<UCk_SwayGym_SourceReceipt_UE>(S,TEXT("Get_SourceReceipt"));
                        if (IsValid(Source))
                        {
                            FCkSwayGym_Frame Latest;
                            const bool HasLatest = UCk_SwayGym_SourceRuntime_UE::TryCopyLatestFrame(Source,Latest);
                            UE_LOG(LogTemp, Display, TEXT("Sway source state=%d reason=%s latest=%d gen=%d frame=%lld"),
                                int32(UCk_SwayGym_SourceRuntime_UE::Get_RetirementState(Source)),
                                *UCk_SwayGym_SourceRuntime_UE::Get_ClosureReason(Source).ToString(),
                                HasLatest, Latest.Generation, Latest.FrameSerial);
                        }
                    }
                    for (int32 I=0;I<Clients;++I)
                    {
                        const void* D = F->Data(I+1);
                        UE_LOG(LogTemp, Display, TEXT("Sway client%d wire gen=%d seq=%lld frame=%lld lifecycle=%d pc=%d"),
                            I, I32(F->SnapshotType,D,TEXT("Generation")),
                            I64(F->SnapshotType,D,TEXT("Sequence")),
                            I64(F->SnapshotType,D,TEXT("FrameSerial")), Lifecycle(F->SnapshotType,D),
                            IsValid(F->Local[I].Get()));
                        if (IsValid(F->Local[I].Get()))
                        {
                            auto* PC = F->Local[I].Get();
                            auto* State = F->ExactState(I+1);
                            const auto Installed = PureStruct(PC,
                                TEXT("Get_InstalledSwaySample"),F->SnapshotType);
                            const void* P = Installed.IsValid() ? Installed->GetStructMemory() : nullptr;
                            TArray<FCk_Handle> Shells;
                            const bool HasShellReadback = PureHandles(PC,
                                TEXT("Get_CapturedSwayStations"),Shells);
                            const auto Head=PureIndexedHandle(State,TEXT("Get_ReplicatedHead"),TEXT("Station"),0);
                            const auto Cube=PureIndexedHandle(State,TEXT("Get_ReplicatedCube"),TEXT("Slot"),0);
                            UE_LOG(LogTemp, Display, TEXT("Sway client%d installed gen=%d seq=%lld frame=%lld pcReady=%d shellsRead=%d shells=%d ownerValid=%d gsSample=%d head=%d cube=%d projection=%d reason=%s"),
                                I, I32(F->SnapshotType,P,TEXT("Generation")),
                                I64(F->SnapshotType,P,TEXT("Sequence")),
                                I64(F->SnapshotType,P,TEXT("FrameSerial")),
                                PureBool(PC,TEXT("Get_SwayPresentationReady")),
                                HasShellReadback,Shells.Num(),
                                ck::IsValid(PureHandle(PC,TEXT("Get_CapturedSwayStationOwner"))),
                                HasPresentationSample(State,F->SnapshotType),ck::IsValid(Head),
                                ck::IsValid(Cube),ClientProjection(*F,I),
                                *PureString(State,TEXT("Get_CurrentFailureReason")));
                            const int64 WireSeq = I64(F->SnapshotType,D,TEXT("Sequence"));
                            const auto* Recorded = F->History.Find(WireSeq);
                            UE_LOG(LogTemp, Display,
                                TEXT("Sway client%d history seq=%lld present=%d wholeExact=%d"),
                                I, WireSeq, Recorded != nullptr,
                                F->Snapshot && D && Recorded && Recorded->IsValid() &&
                                F->Snapshot->Identical(D,(*Recorded)->GetStructMemory(),0));
                            if (F->SnapshotType && Recorded && Recorded->IsValid() && D)
                            {
                                for (TFieldIterator<FProperty> It(F->SnapshotType); It; ++It)
                                {
                                    if (!It->Identical_InContainer(
                                            D,(*Recorded)->GetStructMemory()))
                                    {
                                        UE_LOG(LogTemp, Display,
                                            TEXT("Sway client%d history property differs: %s"),
                                            I,*It->GetName());
                                    }
                                }
                            }
                        }
                    }
                }
                return Ready;
            }),60,TEXT("one real Sway source and two owner-local presentations in CS/LS")));
        auto DynamicSince = MakeShared<double>(-1.0);
        auto DynamicLogged = MakeShared<bool>(false);
        Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([F,DynamicSince,DynamicLogged]() -> bool
            {
                if (*DynamicSince < 0.0) { *DynamicSince = FPlatformTime::Seconds(); }
                const TCHAR* Blocked = TEXT("Context");
                const bool Ready = [&]() -> bool
                {
                    if (!Context(*F)) { return false; }
                    Blocked = TEXT("Client0History");
                    if (!CurrentObserved(*F,1)) { return false; }
                    Blocked = TEXT("Client1History");
                    if (!CurrentObserved(*F,2)) { return false; }
                    Blocked = TEXT("PhysicalMismatch");
                    if (F->PhysicalMismatch) { return false; }
                    Blocked = TEXT("AuthorityFramePhysical");
                    if (!AuthorityFrameIsPhysical(*F)) { return false; }
                    Blocked = TEXT("CaptureStations");
                    if (!CaptureStations(*F)) { return false; }
                    Blocked = TEXT("DynamicFrame");
                    if (!F->SawDynamicFrame) { return false; }
                    Blocked = TEXT("ProductionReplicasMovedMatched");
                    if (!ReplicasMatched(*F)) { return false; }
                    for (int32 I=0;I<Clients;++I)
                    {
                        Blocked = I == 0 ? TEXT("Client0Projection") : TEXT("Client1Projection");
                        if (!ClientProjection(*F,I)) { return false; }
                        Blocked = I == 0 ? TEXT("Client0Generation") : TEXT("Client1Generation");
                        if (I32(F->SnapshotType,F->Data(I+1),TEXT("Generation")) != F->FirstGeneration)
                        { return false; }
                    }
                    return true;
                }();
                if (!Ready && !*DynamicLogged &&
                    FPlatformTime::Seconds() - *DynamicSince >= 20.0)
                {
                    *DynamicLogged = true;
                    const void* D = F->Data(0);
                    UE_LOG(LogTemp, Display, TEXT("Sway dynamic wait blocked=%s mismatch=%d sawDynamic=%d history=%d sourceFrames=%d firstGen=%d firstFrame=%lld authoritySeq=%lld authorityFrame=%lld"),
                        Blocked, F->PhysicalMismatch, F->SawDynamicFrame,
                        F->History.Num(), F->SourceFrames.Num(), F->FirstGeneration,
                        F->FirstSequence, I64(F->SnapshotType,D,TEXT("Sequence")),
                        I64(F->SnapshotType,D,TEXT("FrameSerial")));
                    for (int32 I=0;I<Clients;++I)
                    {
                        const void* Wire = F->Data(I+1);
                        const auto Installed = IsValid(F->Local[I].Get())
                            ? PureStruct(F->Local[I].Get(),TEXT("Get_InstalledSwaySample"),F->SnapshotType)
                            : TSharedPtr<FStructOnScope>{};
                        const void* P = Installed.IsValid() ? Installed->GetStructMemory() : nullptr;
                        UE_LOG(LogTemp, Display, TEXT("Sway client%d observed=%d wireSeq=%lld wireFrame=%lld installedSeq=%lld installedFrame=%lld"),
                            I, D && CurrentObserved(*F,I+1),
                            I64(F->SnapshotType,Wire,TEXT("Sequence")),
                            I64(F->SnapshotType,Wire,TEXT("FrameSerial")),
                            I64(F->SnapshotType,P,TEXT("Sequence")),
                            I64(F->SnapshotType,P,TEXT("FrameSerial")));
                    }
                    if (FCString::Strcmp(Blocked,TEXT("ProductionReplicasMovedMatched"))==0)
                    { LogReplicaMatchDiagnostics(*F); }
                }
                return Ready;
            }),30,TEXT("both client diagnostics and moving production Head/Cube replicas match authority")));
        Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
            FCk_NetAutoTest_Assertion::CreateLambda([F]() -> bool
            {
                for (int32 I=0;I<Clients;++I)
                {
                    auto* S=F->ExactState(I+1);
                    if (!IsValid(S) ||
                        ck::IsValid(PureIndexedHandle(S,TEXT("Get_ReplicatedHead"),TEXT("Station"),-1)) ||
                        ck::IsValid(PureIndexedHandle(S,TEXT("Get_ReplicatedHead"),TEXT("Station"),3)) ||
                        ck::IsValid(PureIndexedHandle(S,TEXT("Get_ReplicatedCube"),TEXT("Slot"),-1)) ||
                        ck::IsValid(PureIndexedHandle(S,TEXT("Get_ReplicatedCube"),TEXT("Slot"),7)) ||
                        !ClientProjection(*F,I)) { return false; }
                }
                return true;
            }),TEXT("malformed replica lookup indices reject without changing source or presentation")));
        Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
            FCk_NetAutoTest_Assertion::CreateLambda([F]() -> bool
            {
                if (!Context(*F) || F->FirstGeneration != 1 ||
                    F->ShellOwner[0] == F->ShellOwner[1] || F->Shells[0] == F->Shells[1] ||
                    !F->FirstSource.IsValid() || F->FirstKnown.Num() != 29)
                { return false; }
                if (F->Dedicated) { return true; }
                auto* Host = F->Host.Get();
                if (!IsValid(Host) || !PureBool(Host,TEXT("Get_SwayPresentationReady")))
                { return false; }
                return PureHandles(Host,TEXT("Get_CapturedSwayStations"),F->HostShells) &&
                    F->HostShells.Num() == 3 &&
                    ck::IsValid(F->HostShellOwner = PureHandle(Host,TEXT("Get_CapturedSwayStationOwner")));
            }),TEXT("authority bridge is stable, clients have distinct local shells and shared replicas")));
        for (int32 Slot=0; Slot<3; ++Slot)
        {
            Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
                FCk_NetAutoTest_Assertion::CreateLambda([F,Slot]() -> bool
                {
                    if (!Context(*F) || !IsValid(F->LocalPawn[1].Get()))
                    { return false; }
                    const auto OtherBefore = F->LocalPawn[1]->GetActorLocation();
                    if (!InvokeControl(F->Local[0].Get(),
                        Slot == 0 ? EKeys::One : Slot == 1 ? EKeys::Two : EKeys::Three))
                    { return false; }
                    return CameraAt(F->Local[0].Get(),*F,Slot) &&
                        F->LocalPawn[1]->GetActorLocation().Equals(OtherBefore,0.01);
                }),TEXT("1/2/3 camera framing remains local to the requesting owner")));
        }
        Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
            FCk_NetAutoTest_Assertion::CreateLambda([F]() -> bool
            {
                FCkGym_ControlRow BeforeA,BeforeB,AfterA,AfterB;
                if (!Row(F->Local[0].Get(),EKeys::O,BeforeA) ||
                    !Row(F->Local[1].Get(),EKeys::O,BeforeB) ||
                    !InvokeControl(F->Local[0].Get(),EKeys::O) ||
                    !Row(F->Local[0].Get(),EKeys::O,AfterA) ||
                    !Row(F->Local[1].Get(),EKeys::O,AfterB)) { return false; }
                if (BeforeA.Active == AfterA.Active || BeforeB.Active != AfterB.Active)
                { return false; }
                if (F->Dedicated) { return true; }
                FCkGym_ControlRow HostBefore,HostAfter,ClientStill;
                return Row(F->Host.Get(),EKeys::O,HostBefore) &&
                    InvokeControl(F->Host.Get(),EKeys::O) &&
                    Row(F->Host.Get(),EKeys::O,HostAfter) &&
                    Row(F->Local[0].Get(),EKeys::O,ClientStill) &&
                    HostBefore.Active != HostAfter.Active && ClientStill.Active == AfterA.Active;
            }),TEXT("O overlay is independent for both clients and the listen host")));
        Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
            FCk_NetAutoTest_Assertion::CreateLambda([F]() -> bool
            {
                int32 Serial,Generation,Result;
                if (!Ack(F->Local[0].Get(),Serial,Generation,Result)) { return false; }
                F->BeforeAckSerial[0] = Serial;
                return InvokeControl(F->Local[0].Get(),EKeys::X);
            }),TEXT("owner A sends one real X reset request")));
        Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
            {
                int32 Serial,Generation,Result;
                if (!Context(*F) || !Ack(F->Local[0].Get(),Serial,Generation,Result) ||
                    Serial <= F->BeforeAckSerial[0] || Generation != F->FirstGeneration || Result != 1 ||
                    !ExactValueShape(F->SnapshotType,F->Data(0)) ||
                    I32(F->SnapshotType,F->Data(0),TEXT("Generation")) != F->FirstGeneration+1 ||
                    UCk_SwayGym_SourceRuntime_UE::Get_RetirementState(F->FirstSource.Get()) !=
                        ECk_SwayGym_RetirementState::Complete ||
                    !AllGone(F->FirstKnown) || !RetainedSource(*F) ||
                    F->PhysicalMismatch || !AuthorityFrameIsPhysical(*F) ||
                    !F->SecondSource.IsValid() || F->SecondSource.Get() == F->FirstSource.Get() ||
                    F->PrematureSuccessor || !CaptureStations(*F) ||
                    !ReplicasMatched(*F)) { return false; }
                if (!F->Dedicated)
                {
                    TArray<FCk_Handle> HostNow;
                    if (!PureHandles(F->Host.Get(),TEXT("Get_CapturedSwayStations"),HostNow) ||
                        HostNow != F->HostShells ||
                        PureHandle(F->Host.Get(),TEXT("Get_CapturedSwayStationOwner")) != F->HostShellOwner)
                    { return false; }
                }
                for (int32 I=0;I<Clients;++I)
                {
                    if (!ClientProjection(*F,I) || !CurrentObserved(*F,I+1) ||
                        I32(F->SnapshotType,F->Data(I+1),TEXT("Generation")) != F->FirstGeneration+1 ||
                        !ReplicasGone(F->FirstReplicas[I]) || F->SecondReplicas[I].Num()!=25)
                    { return false; }
                }
                return true;
            }),60,TEXT("accepted X drains all 29 old IDs before one shared successor; shells remain stable")));
        Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
            FCk_NetAutoTest_Assertion::CreateLambda([F]() -> bool
            {
                int32 Generation,Result;
                if (!Context(*F) || !F->SecondSource.IsValid() ||
                    !Active(F->SnapshotType,F->Data(0),F->FirstGeneration+1) ||
                    !Ack(F->Local[0].Get(),F->BeforeAckSerial[0],Generation,Result) ||
                    !Rpc(F->Local[0].Get(),0,F->FirstGeneration+1))
                { return false; }
                return true;
            }),TEXT("default serial request is sent through real owner RPC")));
        auto DefaultSince=MakeShared<double>(-1.0);
        Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([F,DefaultSince]() -> bool
            {
                if (*DefaultSince<0.0) { *DefaultSince=FPlatformTime::Seconds(); }
                if (FPlatformTime::Seconds()-*DefaultSince<0.3) { return false; }
                int32 Serial,Generation,Result;
                return Context(*F) && Ack(F->Local[0].Get(),Serial,Generation,Result) &&
                    Serial==F->BeforeAckSerial[0] &&
                    Active(F->SnapshotType,F->Data(0),F->FirstGeneration+1) &&
                    UCk_SwayGym_SourceRuntime_UE::Get_RetirementState(F->SecondSource.Get())==
                        ECk_SwayGym_RetirementState::Active &&
                    ClientProjection(*F,0) && ClientProjection(*F,1);
            }),5,TEXT("default serial leaves ack, source and production replicas unchanged")));
        Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
            FCk_NetAutoTest_Assertion::CreateLambda([F]() -> bool
            { return Rpc(F->Local[0].Get(),700,F->FirstGeneration); }),
            TEXT("old generation request is sent through real owner RPC")));
        Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
            {
                int32 Serial,Generation,Result;
                return Context(*F) && Ack(F->Local[0].Get(),Serial,Generation,Result) &&
                    Serial==700 && Generation==F->FirstGeneration && Result==3 &&
                    Active(F->SnapshotType,F->Data(0),F->FirstGeneration+1) &&
                    UCk_SwayGym_SourceRuntime_UE::Get_RetirementState(F->SecondSource.Get())==
                        ECk_SwayGym_RetirementState::Active &&
                    RetainedSource(*F) && AuthorityFrameIsPhysical(*F) &&
                    ClientProjection(*F,0) && ClientProjection(*F,1);
            }),20,TEXT("malformed and stale RPCs reject without retiring or replacing source replicas")));
        Test.AddCommand(new FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([F](UWorld* W) -> void
            {
                if (!IsValid(W) || W != F->World[0].Get() || !F->SecondSource.IsValid())
                { ck::auto_test::net::FailSession(TEXT("Sway dependency-loss source unavailable")); return; }
                const auto Roles = UCk_SwayGym_SourceRuntime_UE::Get_CapturedRoles(F->SecondSource.Get());
                if (Roles.CubeNodes.Num() != 7 ||
                    !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Roles.CubeNodes[0]))
                { ck::auto_test::net::FailSession(TEXT("Sway exact source cube role unavailable")); return; }
                auto Target = Roles.CubeNodes[0];
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Target);
            })));
        Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
            {
                if (!Context(*F) || F->PrematureSuccessor || !F->SecondSource.IsValid() ||
                    UCk_SwayGym_SourceRuntime_UE::Get_RetirementState(F->SecondSource.Get()) !=
                        ECk_SwayGym_RetirementState::Unproven)
                { return false; }
                for (int32 I=0;I<Worlds;++I)
                {
                    const auto* D = F->Data(I);
                    if (!Failed(F->SnapshotType,D,F->FirstGeneration+1) ||
                        !Field<FStrProperty>(F->SnapshotType,TEXT("FailureReason")) ||
                        Field<FStrProperty>(F->SnapshotType,TEXT("FailureReason"))
                            ->GetPropertyValue_InContainer(D).IsEmpty() ||
                        ArrayCount(F->SnapshotType,D,TEXT("SwayYawDegrees")) != 0 ||
                        ArrayCount(F->SnapshotType,D,TEXT("SwayXCm")) != 0 ||
                        ArrayCount(F->SnapshotType,D,TEXT("SwaySettled")) != 0)
                    { return false; }
                }
                return AllGone(F->SecondKnown) && TerminalPresentationHidden(*F,0) &&
                    TerminalPresentationHidden(*F,1);
            }),30,TEXT("real source-role loss publishes terminal Failed with no partial Active sample")));
        Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
            FCk_NetAutoTest_Assertion::CreateLambda([F]() -> bool
            {
                return Rpc(F->Local[1].Get(),701,F->FirstGeneration+1);
            }),TEXT("owner B attempts X after terminal source loss")));
        Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
            {
                int32 Serial,Generation,Result;
                return Ack(F->Local[1].Get(),Serial,Generation,Result) &&
                    Serial == 701 && Generation == F->FirstGeneration+1 && Result == 3;
            }),20,TEXT("terminal world rejects later owner B X with typed receipt")));
        auto ObserveUntil = MakeShared<double>(-1.0);
        Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
            FCk_NetAutoTest_Condition::CreateLambda([F,ObserveUntil]() -> bool
            {
                if (*ObserveUntil < 0.0) { *ObserveUntil = FPlatformTime::Seconds(); }
                return FPlatformTime::Seconds() - *ObserveUntil >= 2.0;
            }),5,TEXT("observe terminal state across later frames")));
        Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
            FCk_NetAutoTest_Assertion::CreateLambda([F]() -> bool
            {
                if (F->PrematureSuccessor || F->TerminalReactivated ||
                    !F->TerminalObserved || !AllGone(F->FirstKnown) ||
                    !AllGone(F->SecondKnown)) { return false; }
                for (int32 I=0;I<Worlds;++I)
                { if (!Failed(F->SnapshotType,F->Data(I),F->FirstGeneration+1)) { return false; } }
                return TerminalPresentationHidden(*F,0) &&
                    TerminalPresentationHidden(*F,1);
            }),TEXT("Failed world neither republishes Active nor admits generation 3")));
        Test.AddCommand(new FCk_Latent_EndPIE());
        Test.AddCommand(new FRestore(F));
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSwayGym_SharedWorld_Network,
    "Ck.SwayGym.Network.SharedWorld",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSwayGym_SharedWorld_Network::RunTest(const FString& Parameters)
{ return ck_sway_shared_network::Run(*this,Parameters); }

#endif
