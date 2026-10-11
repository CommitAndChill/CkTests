#include "CoreMinimal.h"
#include "CoreGlobals.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkGym_StartupSettings.h"
#include "CkGym_ControlPanelTypes.h"
#include "CkGym_WireTransformRuntime.h"
#include "CkRotateTowardsGym_Runtime.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/Net/ReplicatedFragmentContainer/CkReplicatedFragmentContainer.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkUnrealComponent/CkUnrealComponent_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_rotate_two_owner
{
    constexpr auto Map = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");

    template<typename T>
    auto Field(const UStruct* Type, const TCHAR* Name) -> T*
    { return Type ? FindFProperty<T>(Type, Name) : nullptr; }

    auto I32(const UStruct* Type, const void* Data, const TCHAR* Name) -> int32
    {
        const auto* P = Field<FIntProperty>(Type, Name);
        return P && Data ? P->GetPropertyValue_InContainer(Data) : MIN_int32;
    }
    auto Flag(const UStruct* Type, const void* Data, const TCHAR* Name) -> bool
    {
        const auto* P = Field<FBoolProperty>(Type, Name);
        return P && Data && P->GetPropertyValue_InContainer(Data);
    }
    auto Object(const UStruct* Type, const void* Data, const TCHAR* Name) -> UObject*
    {
        const auto* P = Field<FObjectPropertyBase>(Type, Name);
        return P && Data ? P->GetObjectPropertyValue_InContainer(Data) : nullptr;
    }

    // Every nested FTransform uses the same directional authored/source -> Iris actual
    // comparison as TransformGym. No arbitrary epsilon or source re-quantization.
    auto WireValue(const FProperty* P, const void* Source, const void* Actual) -> bool
    {
        if (!P || !Source || !Actual) { return false; }
        if (const auto* Struct = CastField<FStructProperty>(P))
        {
            if (Struct->Struct == TBaseStructure<FTransform>::Get())
            {
                return UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
                    *static_cast<const FTransform*>(Source), *static_cast<const FTransform*>(Actual));
            }
            if (Struct->Struct == TBaseStructure<FRotator>::Get())
            {
                return UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisRotator(
                    *static_cast<const FRotator*>(Source), *static_cast<const FRotator*>(Actual));
            }
            if (!Struct->Struct) { return false; }
            for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
            {
                if (!WireValue(*It, It->ContainerPtrToValuePtr<void>(Source),
                               It->ContainerPtrToValuePtr<void>(Actual))) { return false; }
            }
            return true;
        }
        if (const auto* Array = CastField<FArrayProperty>(P))
        {
            FScriptArrayHelper A(Array, const_cast<void*>(Source));
            FScriptArrayHelper B(Array, const_cast<void*>(Actual));
            if (A.Num() != B.Num()) { return false; }
            for (int32 I = 0; I < A.Num(); ++I)
            { if (!WireValue(Array->Inner, A.GetRawPtr(I), B.GetRawPtr(I))) { return false; } }
            return true;
        }
        return P->Identical(Source, Actual, PPF_None);
    }
    auto WireSnapshot(const UScriptStruct* Type, const void* Source, const void* Actual) -> bool
    {
        if (!Type || !Source || !Actual) { return false; }
        for (TFieldIterator<FProperty> It(Type); It; ++It)
        {
            if (!WireValue(*It, It->ContainerPtrToValuePtr<void>(Source),
                           It->ContainerPtrToValuePtr<void>(Actual))) { return false; }
        }
        return true;
    }

    struct FFixture
    {
        UClass* Mode = nullptr;
        UClass* StateClass = nullptr;
        UClass* PCClass = nullptr;
        UScriptStruct* SnapshotType = nullptr;
        UScriptStruct* CommitType = nullptr;
        FStructProperty* Snapshot = nullptr;
        FStructProperty* Installed = nullptr;
        FStructProperty* LastCommit = nullptr;
        UFunction* Advance = nullptr;
        UFunction* Presentation = nullptr;
        UFunction* CompleteSnapshot = nullptr;
        UFunction* InstalledCommit = nullptr;
        UFunction* Rows = nullptr;
        UFunction* Draw = nullptr;
        UFunction* Control = nullptr;
        UFunction* VisualComponents = nullptr;
        TWeakObjectPtr<UWorld> Worlds[3];
        TWeakObjectPtr<AGameStateBase> States[3];
        TWeakObjectPtr<APlayerController> Local[2];
        TWeakObjectPtr<APlayerController> Remote[2];
        TWeakObjectPtr<APlayerController> Host;
        TWeakObjectPtr<APawn> LocalPawn[2];
        TWeakObjectPtr<APawn> RemotePawn[2];
        FDelegateHandle TickGuard;
        FDelegateHandle PrivacyTick;
        ECkGym_StartupMode Startup;
        FString Last;
        TArray<FString> Recents;
        TUniquePtr<FStructOnScope> FrozenA;
        TUniquePtr<FStructOnScope> FrozenB;
        TUniquePtr<FStructOnScope> FrozenClientA;
        TUniquePtr<FStructOnScope> FrozenClientB;
        int32 FrozenAckA = 0;
        int32 FrozenAckB = 0;
        int32 PublicationSequence = 0;
        int32 PrivacyTicks = 0;
        bool PrivacyWatch = false;
        bool BeforeO[3] = {false, false, false};
        bool Dedicated = false;
        bool SawGuard = false;
        int32 MotionGeneration = 0;
        TArray<FTransform> AuthorityHistory[11];
        TArray<FTransform> ClientNativeHistory[2][11];
        FTransform AuthoritySeed[11];
        bool AuthoritySeeded[11]{};
        bool AuthorityMoved[11]{};
        uint64 NativeFrame[2][11]{};
        uint64 MeshFrame[2][11]{};
        int32 NativeMatches[2][11]{};
        int32 MeshMatches[2][11]{};
        int32 MovedNativeMatches[2][11]{};
        int32 MovedMeshMatches[2][11]{};
        uint64 MovedNativeFrame[2][11]{};
        uint64 MovedMeshFrame[2][11]{};
        FCk_Handle ReplicaIdentity[3][3][11]{};
        bool ReplicaCaptured[3]{};
        FString Detail;

        void Restore()
        {
            if (TickGuard.IsValid())
            { FWorldDelegates::OnWorldTickStart.Remove(TickGuard); TickGuard.Reset(); }
            if (PrivacyTick.IsValid())
            { FWorldDelegates::OnWorldPostActorTick.Remove(PrivacyTick); PrivacyTick.Reset(); }
            if (auto* S = States[0].Get()) { S->SetActorTickEnabled(true); }
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = Startup;
            Settings->LastGymName = Last;
            Settings->RecentGymNames = Recents;
        }
        bool Schema() const
        {
            if (!Mode || !StateClass || !PCClass || !SnapshotType || !CommitType ||
                !Snapshot || Snapshot->Struct != SnapshotType ||
                !Snapshot->HasAnyPropertyFlags(CPF_Net) || !Installed ||
                Installed->Struct != SnapshotType || Installed->HasAnyPropertyFlags(CPF_Net) ||
                !LastCommit || LastCommit->Struct != CommitType ||
                !LastCommit->HasAnyPropertyFlags(CPF_Net) || !Advance || Advance->NumParms != 0 ||
                !Presentation || !CompleteSnapshot || !InstalledCommit || !Rows || !Draw || !Control ||
                !VisualComponents || !VisualComponents->HasAllFunctionFlags(FUNC_BlueprintPure) ||
                !Control->HasAllFunctionFlags(FUNC_Exec) || Control->NumParms != 1 ||
                !Field<FIntProperty>(Control, TEXT("Index"))) { return false; }
            const TCHAR* CommitNames[] = {TEXT("DeliverySerial"), TEXT("RequestSerial"), TEXT("OriginGeneration")};
            for (auto* N : CommitNames) { if (!Field<FIntProperty>(CommitType, N)) { return false; } }
            if (!Field<FStructProperty>(CommitType, TEXT("Snapshot")) ||
                Field<FStructProperty>(CommitType, TEXT("Snapshot"))->Struct != SnapshotType ||
                !Field<FObjectPropertyBase>(CommitType, TEXT("OriginState")) ||
                !Field<FObjectPropertyBase>(CommitType, TEXT("Owner")) ||
                !Field<FObjectPropertyBase>(CommitType, TEXT("OwnerPawn"))) { return false; }
            const TCHAR* PCNames[] = {TEXT("LastControlAttemptSerial"), TEXT("LastControlSerial"),
                TEXT("LastControlResult"), TEXT("LastAcknowledgedDeliverySerial")};
            for (auto* N : PCNames) { if (!Field<FIntProperty>(PCClass, N)) { return false; } }
            const auto* ComponentsArray = Field<FArrayProperty>(
                VisualComponents, TEXT("ReturnValue"));
            const auto* ComponentsInner = ComponentsArray ?
                CastField<FStructProperty>(ComponentsArray->Inner) : nullptr;
            if (VisualComponents->NumParms != 1 || !ComponentsArray ||
                !ComponentsArray->HasAnyPropertyFlags(CPF_ReturnParm) ||
                !ComponentsInner || ComponentsInner->Struct !=
                    FCk_Handle_UnrealComponent::StaticStruct()) { return false; }
            const auto* VisualsArray = Field<FArrayProperty>(SnapshotType, TEXT("Visuals"));
            const auto* VisualRow = VisualsArray ?
                CastField<FStructProperty>(VisualsArray->Inner) : nullptr;
            if (!VisualRow ||
                Field<FStructProperty>(VisualRow->Struct, TEXT("Transform")) ||
                !Field<FIntProperty>(VisualRow->Struct, TEXT("Slot")) ||
                !Field<FIntProperty>(VisualRow->Struct, TEXT("Station"))) { return false; }
            const auto* Turrets = Field<FArrayProperty>(SnapshotType, TEXT("Turrets"));
            const auto* TurretRow = Turrets ? CastField<FStructProperty>(Turrets->Inner) : nullptr;
            if (!TurretRow || Field<FStructProperty>(TurretRow->Struct, TEXT("Transform")) ||
                Field<FStructProperty>(TurretRow->Struct, TEXT("Target")) ||
                Field<FStructProperty>(TurretRow->Struct, TEXT("Rest"))) { return false; }
            const auto* RowArray = Field<FArrayProperty>(Rows, TEXT("ReturnValue"));
            const auto* RowStruct = RowArray ? CastField<FStructProperty>(RowArray->Inner) : nullptr;
            const auto* CompleteInput = Field<FStructProperty>(CompleteSnapshot, TEXT("S"));
            const auto* CompleteResult = Field<FBoolProperty>(CompleteSnapshot, TEXT("ReturnValue"));
            return Field<FIntProperty>(SnapshotType, TEXT("Generation")) &&
                Field<FIntProperty>(SnapshotType, TEXT("Sequence")) &&
                Field<FBoolProperty>(SnapshotType, TEXT("Ready")) &&
                Field<FBoolProperty>(SnapshotType, TEXT("Failed")) &&
                RowStruct && RowStruct->Struct == FCkGym_ControlRow::StaticStruct() &&
                CompleteSnapshot->HasAllFunctionFlags(FUNC_BlueprintPure) &&
                CompleteSnapshot->NumParms == 2 && CompleteInput &&
                CompleteInput->Struct == SnapshotType &&
                CompleteInput->HasAnyPropertyFlags(CPF_Parm) &&
                !CompleteInput->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm | CPF_ReturnParm) &&
                CompleteResult && CompleteResult->HasAnyPropertyFlags(CPF_ReturnParm) &&
                Field<FBoolProperty>(Presentation, TEXT("ReturnValue")) &&
                Field<FBoolProperty>(Draw, TEXT("ReturnValue")) &&
                Field<FStructProperty>(InstalledCommit, TEXT("ReturnValue")) &&
                Field<FStructProperty>(InstalledCommit, TEXT("ReturnValue"))->Struct == CommitType;
        }
        bool Context() const
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || Server != Worlds[0].Get() ||
                ck::auto_test::net::Get_AllPIEWorlds().Num() != 3 ||
                ck::auto_test::net::Get_NumClientWorlds() != 2 ||
                Server->GetNetMode() != (Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
                !IsValid(Server->GetAuthGameMode()) ||
                Server->GetAuthGameMode()->GetClass() != Mode ||
                !IsValid(States[0].Get()) || Server->GetGameState() != States[0].Get() ||
                States[0]->GetClass() != StateClass || States[0]->IsActorTickEnabled())
            { return false; }
            auto* GI = Server->GetGameInstance();
            if (!IsValid(GI) || GI->GetWorld() != Server) { return false; }
            if (Dedicated)
            { if (!GI->GetLocalPlayers().IsEmpty() || Host.IsValid()) { return false; } }
            else
            {
                if (GI->GetLocalPlayers().Num() != 1) { return false; }
                auto* LP = GI->GetLocalPlayers()[0];
                auto* H = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
                if (!IsValid(H) || H != Host.Get() || H->GetClass() != PCClass ||
                    !H->HasAuthority() || !H->IsLocalController()) { return false; }
            }
            for (int32 I = 0; I < 2; ++I)
            {
                auto* W = ck::auto_test::net::Get_ClientWorld(I);
                auto* C = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
                auto* R = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
                if (!IsValid(W) || W != Worlds[I + 1].Get() ||
                    !IsValid(States[I + 1].Get()) || W->GetGameState() != States[I + 1].Get() ||
                    States[I + 1]->GetClass() != StateClass ||
                    !IsValid(C) || C != Local[I].Get() || C->GetClass() != PCClass ||
                    !C->IsLocalController() || C->HasAuthority() || C->GetWorld() != W ||
                    !IsValid(R) || R != Remote[I].Get() || R->GetClass() != PCClass ||
                    !R->HasAuthority() || R->GetWorld() != Server ||
                    !IsValid(C->GetPawn()) || C->GetPawn() != LocalPawn[I].Get() ||
                    C->GetPawn()->GetController() != C ||
                    !IsValid(R->GetPawn()) || R->GetPawn() != RemotePawn[I].Get() ||
                    R->GetPawn()->GetController() != R) { return false; }
            }
            return true;
        }
        bool Capture()
        {
            if (Worlds[0].IsValid()) { return Context(); }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || ck::auto_test::net::Get_AllPIEWorlds().Num() != 3 ||
                ck::auto_test::net::Get_NumClientWorlds() != 2 || !IsValid(Server->GetGameState()) ||
                Server->GetGameState()->GetClass() != StateClass) { return false; }
            APlayerController* HostPC = nullptr;
            if (!Dedicated)
            {
                auto* GI = Server->GetGameInstance();
                if (!IsValid(GI) || GI->GetLocalPlayers().Num() != 1) { return false; }
                auto* LP = GI->GetLocalPlayers()[0];
                HostPC = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
                if (!IsValid(HostPC) || HostPC->GetClass() != PCClass) { return false; }
            }
            UWorld* ClientWorlds[2]{};
            AGameStateBase* ClientStates[2]{};
            APlayerController* ClientPCs[2]{};
            APlayerController* ServerPCs[2]{};
            APawn* ClientPawns[2]{};
            APawn* ServerPawns[2]{};
            for (int32 I = 0; I < 2; ++I)
            {
                auto* W = ck::auto_test::net::Get_ClientWorld(I);
                if (!IsValid(W)) { return false; }
                ClientWorlds[I] = W;
                ClientStates[I] = W->GetGameState();
                ClientPCs[I] = W->GetFirstPlayerController();
                ServerPCs[I] = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
                ClientPawns[I] = IsValid(ClientPCs[I]) ? ClientPCs[I]->GetPawn() : nullptr;
                ServerPawns[I] = IsValid(ServerPCs[I]) ? ServerPCs[I]->GetPawn() : nullptr;
                if (!IsValid(ClientStates[I]) || ClientStates[I]->GetClass() != StateClass ||
                    !IsValid(ClientPCs[I]) || ClientPCs[I]->GetClass() != PCClass ||
                    !IsValid(ServerPCs[I]) || ServerPCs[I]->GetClass() != PCClass ||
                    !IsValid(ClientPawns[I]) || !IsValid(ServerPawns[I])) { return false; }
            }
            Worlds[0] = Server; States[0] = Server->GetGameState(); Host = HostPC;
            for (int32 I = 0; I < 2; ++I)
            {
                Worlds[I + 1] = ClientWorlds[I]; States[I + 1] = ClientStates[I];
                Local[I] = ClientPCs[I]; Remote[I] = ServerPCs[I];
                LocalPawn[I] = ClientPawns[I]; RemotePawn[I] = ServerPawns[I];
            }
            if (Context()) { return true; }
            for (auto& W : Worlds) { W.Reset(); }
            for (auto& S : States) { S.Reset(); }
            for (auto& C : Local) { C.Reset(); }
            for (auto& R : Remote) { R.Reset(); }
            for (auto& P : LocalPawn) { P.Reset(); }
            for (auto& P : RemotePawn) { P.Reset(); }
            Host.Reset();
            return false;
        }
        const void* Sample(int32 World) const
        { return Context() ? Snapshot->ContainerPtrToValuePtr<void>(States[World].Get()) : nullptr; }
        const void* Commit(int32 Owner, bool Client) const
        {
            auto* PC = Client ? Local[Owner].Get() : Remote[Owner].Get();
            return Context() ? LastCommit->ContainerPtrToValuePtr<void>(PC) : nullptr;
        }
        bool Pump() const
        {
            auto* S = States[0].Get();
            if (!Context() || S->FindFunction(Advance->GetFName()) != Advance) { return false; }
            S->ProcessEvent(Advance, nullptr); return true;
        }
    };

    auto PoseNear(const FTransform& A, const FTransform& B) -> bool
    {
        return FVector::Dist(A.GetLocation(), B.GetLocation()) <= 5.0 &&
            FMath::RadiansToDegrees(A.GetRotation().AngularDistance(
                B.GetRotation())) <= 2.0 &&
            FVector::Dist(A.GetScale3D(), B.GetScale3D()) <= 0.02;
    }
    auto PoseMoved(const FTransform& A, const FTransform& B) -> bool
    {
        return FVector::Dist(A.GetLocation(), B.GetLocation()) > 5.0 ||
            FMath::RadiansToDegrees(A.GetRotation().AngularDistance(
                B.GetRotation())) > 2.0;
    }
    auto AppliedCubePose(const FCk_Handle& Cube, FTransform& Out) -> bool
    {
        if (!ck::IsValid(Cube) || !Cube.Has<ck::FFragment_Transform>() ||
            !Cube.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>() ||
            Cube.Has<ck::FTag_RepFragments_PendingApply>()) { return false; }
        if (Cube.Has<ck::FFragment_Transform_Requests>())
        {
            const auto& Requests = Cube.Get<ck::FFragment_Transform_Requests>();
            if (!Requests.Get_LocationRequests().IsEmpty() ||
                !Requests.Get_RotationRequests().IsEmpty() ||
                Requests.Get_ScaleRequests().IsSet() ||
                !Requests.Get_ForceRefreshRequests().IsEmpty()) { return false; }
        }
        auto* Driver = Cube.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
        if (!IsValid(Driver) || Driver->Get_AssociatedEntity() != Cube) { return false; }
        const auto* L = Driver->FindEntry(FCk_RepData_Location::StaticStruct());
        const auto* R = Driver->FindEntry(FCk_RepData_Rotation::StaticStruct());
        const auto* S = Driver->FindEntry(FCk_RepData_Scale::StaticStruct());
        if (!L || !R || !S || L->_PendingApply || R->_PendingApply ||
            S->_PendingApply || !L->_WasEverApplied || !R->_WasEverApplied ||
            !S->_WasEverApplied ||
            L->_LastAppliedData.GetScriptStruct() != FCk_RepData_Location::StaticStruct() ||
            R->_LastAppliedData.GetScriptStruct() != FCk_RepData_Rotation::StaticStruct() ||
            S->_LastAppliedData.GetScriptStruct() != FCk_RepData_Scale::StaticStruct())
        { return false; }
        const FTransform Applied(
            R->_LastAppliedData.Get<FCk_RepData_Rotation>().Value,
            L->_LastAppliedData.Get<FCk_RepData_Location>().Value,
            S->_LastAppliedData.Get<FCk_RepData_Scale>().Value);
        Out = Cube.Get<ck::FFragment_Transform>().Get_Transform();
        // Both are client-local values; network/history tolerances do not apply.
        return Out.IsValid() && !Out.ContainsNaN() && Out.Equals(Applied);
    }
    auto RecordProductionCubes(FFixture& F, UWorld* World) -> void
    {
        if (!F.Context() || !IsValid(World)) { return; }
        const int32 WorldIndex = World == F.Worlds[0].Get() ? 0 :
            World == F.Worlds[1].Get() ? 1 :
            World == F.Worlds[2].Get() ? 2 : -1;
        if (WorldIndex < 0) { return; }
        const void* Sample = F.Snapshot->ContainerPtrToValuePtr<void>(
            F.States[WorldIndex].Get());
        const int32 Generation = I32(F.SnapshotType, Sample, TEXT("Generation"));
        if (Generation <= 0 || !Flag(F.SnapshotType, Sample, TEXT("Ready")) ||
            Flag(F.SnapshotType, Sample, TEXT("Failed"))) { return; }
        if (WorldIndex == 0 && F.MotionGeneration != Generation)
        {
            F.MotionGeneration = Generation;
            for (int32 Slot = 0; Slot < 11; ++Slot)
            {
                F.AuthorityHistory[Slot].Reset();
                F.AuthoritySeeded[Slot] = false;
                F.AuthorityMoved[Slot] = false;
                for (int32 Client = 0; Client < 2; ++Client)
                {
                    F.ClientNativeHistory[Client][Slot].Reset();
                    F.NativeFrame[Client][Slot] = 0;
                    F.MeshFrame[Client][Slot] = 0;
                    F.NativeMatches[Client][Slot] = 0;
                    F.MeshMatches[Client][Slot] = 0;
                    F.MovedNativeMatches[Client][Slot] = 0;
                    F.MovedMeshMatches[Client][Slot] = 0;
                    F.MovedNativeFrame[Client][Slot] = 0;
                    F.MovedMeshFrame[Client][Slot] = 0;
                }
            }
        }
        if (Generation != F.MotionGeneration) { return; }
        for (int32 Slot = 0; Slot < 11; ++Slot)
        {
            const auto Cube = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(
                World, Generation, Slot);
            if (!ck::IsValid(Cube)) { continue; }
            const FTransform Pose =
                Cube.Get<ck::FFragment_Transform>().Get_Transform();
            if (!Pose.IsValid() || Pose.ContainsNaN()) { continue; }
            if (WorldIndex == 0)
            {
                if (!F.AuthoritySeeded[Slot])
                { F.AuthoritySeed[Slot] = Pose; F.AuthoritySeeded[Slot] = true; }
                F.AuthorityMoved[Slot] |= PoseMoved(Pose, F.AuthoritySeed[Slot]);
                F.AuthorityHistory[Slot].Add(Pose);
                if (F.AuthorityHistory[Slot].Num() > 512)
                { F.AuthorityHistory[Slot].RemoveAt(0, 256); }
                continue;
            }
            const int32 Client = WorldIndex - 1;
            FTransform Applied;
            if (!AppliedCubePose(Cube, Applied)) { continue; }
            bool MatchesAuthority = false;
            bool MatchesMovedAuthority = false;
            for (const auto& Authority : F.AuthorityHistory[Slot])
            {
                if (!PoseNear(Applied, Authority)) { continue; }
                MatchesAuthority = true;
                MatchesMovedAuthority |= F.AuthoritySeeded[Slot] &&
                    PoseMoved(Authority, F.AuthoritySeed[Slot]);
            }
            if (!MatchesAuthority) { continue; }
            F.ClientNativeHistory[Client][Slot].Add(Applied);
            if (F.ClientNativeHistory[Client][Slot].Num() > 512)
            { F.ClientNativeHistory[Client][Slot].RemoveAt(0, 256); }
            if (MatchesAuthority && F.NativeFrame[Client][Slot] != GFrameCounter)
            {
                F.NativeFrame[Client][Slot] = GFrameCounter;
                ++F.NativeMatches[Client][Slot];
            }
            if (MatchesMovedAuthority &&
                F.MovedNativeFrame[Client][Slot] != GFrameCounter)
            {
                F.MovedNativeFrame[Client][Slot] = GFrameCounter;
                ++F.MovedNativeMatches[Client][Slot];
            }
            auto* Mesh = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCubeMesh(
                World, Generation, Slot);
            if (!IsValid(Mesh) || !Mesh->IsVisible() || Mesh->bHiddenInGame)
            { continue; }
            bool MeshMatchesNative = false;
            bool MeshMoved = false;
            for (const auto& Native : F.ClientNativeHistory[Client][Slot])
            {
                if (!PoseNear(Mesh->GetComponentTransform(), Native)) { continue; }
                MeshMatchesNative = true;
                MeshMoved |= F.AuthoritySeeded[Slot] &&
                    PoseMoved(Native, F.AuthoritySeed[Slot]);
            }
            if (MatchesAuthority && MeshMatchesNative &&
                F.MeshFrame[Client][Slot] != GFrameCounter)
            {
                F.MeshFrame[Client][Slot] = GFrameCounter;
                ++F.MeshMatches[Client][Slot];
            }
            if (MatchesMovedAuthority && MeshMoved &&
                F.MovedMeshFrame[Client][Slot] != GFrameCounter)
            {
                F.MovedMeshFrame[Client][Slot] = GFrameCounter;
                ++F.MovedMeshMatches[Client][Slot];
            }
        }
    }
    auto ProductionCubesMovedMatched(const FFixture& F) -> bool
    {
        if (F.MotionGeneration != 3) { return false; }
        for (int32 Slot = 0; Slot < 11; ++Slot)
        {
            for (int32 Client = 0; Client < 2; ++Client)
            {
                if (F.NativeMatches[Client][Slot] < 2 ||
                    F.MeshMatches[Client][Slot] < 2) { return false; }
            }
        }
        constexpr int32 MovingSlots[4] = {0, 1, 3, 6};
        for (int32 Slot : MovingSlots)
        {
            if (!F.AuthorityMoved[Slot]) { return false; }
            for (int32 Client = 0; Client < 2; ++Client)
            {
                if (F.MovedNativeMatches[Client][Slot] < 2 ||
                    F.MovedMeshMatches[Client][Slot] < 2) { return false; }
            }
        }
        return true;
    }
    auto CaptureAndCheckReplicaGeneration(FFixture& F, int32 Generation) -> bool
    {
        if (!F.Context() || Generation < 1 || Generation > 3 ||
            I32(F.SnapshotType, F.Sample(0), TEXT("Generation")) != Generation)
        { return false; }
        const int32 Index = Generation - 1;
        FCk_Handle Current[3][11];
        for (int32 World = 0; World < 3; ++World)
        {
            for (int32 Slot = 0; Slot < 11; ++Slot)
            {
                Current[World][Slot] = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(
                    F.Worlds[World].Get(), Generation, Slot);
                if (!ck::IsValid(Current[World][Slot]) ||
                    !IsValid(UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCubeMesh(
                        F.Worlds[World].Get(), Generation, Slot)))
                { return false; }
                if (Index > 0 &&
                    UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                        F.ReplicaIdentity[Index - 1][World][Slot]))
                { return false; }
            }
        }
        if (!F.ReplicaCaptured[Index])
        {
            for (int32 World = 0; World < 3; ++World)
            { for (int32 Slot = 0; Slot < 11; ++Slot)
              { F.ReplicaIdentity[Index][World][Slot] = Current[World][Slot]; } }
            F.ReplicaCaptured[Index] = true;
        }
        else
        {
            for (int32 World = 0; World < 3; ++World)
            { for (int32 Slot = 0; Slot < 11; ++Slot)
              { if (F.ReplicaIdentity[Index][World][Slot] != Current[World][Slot])
                { return false; } } }
        }
        return true;
    }
    auto ClientOnlyReplicaTree(UWorld* World, int32 Generation) -> bool
    {
        if (!IsValid(World) || World->GetNetMode() != NM_Client) { return false; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        const auto First = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(
            World, Generation, 0);
        if (!ck::IsValid(Root) || !ck::IsValid(First) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(First)) { return false; }
        const auto Owner = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(First);
        if (!ck::IsValid(Owner) || Owner.Has<ck::FFragment_Transform>() ||
            Owner.Has<ck::FFragment_SceneNode>() ||
            !UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveMotionEntity(Owner, Root))
        { return false; }
        TArray<FCk_Handle> Direct;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                Owner, Root, Direct) || Direct.Num() != 11) { return false; }
        TSet<FCk_Handle> Expected;
        for (int32 Slot = 0; Slot < 11; ++Slot)
        {
            const auto Cube = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(
                World, Generation, Slot);
            if (!ck::IsValid(Cube) || Cube.Has<ck::FFragment_SceneNode>() ||
                !UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveMotionEntity(Cube, Root))
            { return false; }
            Expected.Add(Cube);
        }
        if (Expected.Num() != 11) { return false; }
        for (const auto& H : Direct)
        { if (!Expected.Contains(H)) { return false; } }
        return true;
    }

    auto BoolGetter(UObject* Target, UFunction* Function) -> bool
    {
        if (!IsValid(Target) || !Function || Target->FindFunction(Function->GetFName()) != Function)
        { return false; }
        FStructOnScope Args(Function);
        Target->ProcessEvent(Function, Args.GetStructMemory());
        return Field<FBoolProperty>(Function, TEXT("ReturnValue"))->GetPropertyValue_InContainer(
            Args.GetStructMemory());
    }
    auto Rows(FFixture& F, APlayerController* PC, const FKey& Key,
              int32& OutIndex, bool& OutActive) -> bool
    {
        OutIndex = INDEX_NONE; OutActive = false;
        if (!IsValid(PC) || PC->GetClass() != F.PCClass ||
            PC->FindFunction(F.Rows->GetFName()) != F.Rows) { return false; }
        FStructOnScope Args(F.Rows);
        PC->ProcessEvent(F.Rows, Args.GetStructMemory());
        const auto* P = Field<FArrayProperty>(F.Rows, TEXT("ReturnValue"));
        FScriptArrayHelper A(P, P->ContainerPtrToValuePtr<void>(Args.GetStructMemory()));
        for (int32 I = 0; I < A.Num(); ++I)
        {
            const auto* R = reinterpret_cast<const FCkGym_ControlRow*>(A.GetRawPtr(I));
            if (!R || R->Key != Key) { continue; }
            if (OutIndex != INDEX_NONE) { return false; }
            OutIndex = I; OutActive = R->Active;
            if (Key == EKeys::X && (R->Kind != ECkGym_ControlKind::Action || !R->Enabled))
            { return false; }
            if (Key == EKeys::O && (R->Kind != ECkGym_ControlKind::Toggle || !R->Enabled))
            { return false; }
        }
        return OutIndex != INDEX_NONE;
    }
    auto Command(FFixture& F, int32 Owner, const FKey& Key) -> bool
    {
        auto* PC = F.Local[Owner].Get(); int32 Index; bool Active;
        if (!F.Context() || !Rows(F, PC, Key, Index, Active) ||
            PC->FindFunction(F.Control->GetFName()) != F.Control) { return false; }
        FStructOnScope Args(F.Control);
        Field<FIntProperty>(F.Control, TEXT("Index"))->SetPropertyValue_InContainer(
            Args.GetStructMemory(), Index);
        PC->ProcessEvent(F.Control, Args.GetStructMemory());
        return true;
    }
    auto Complete(const FFixture& F, int32 World, const void* Sample, int32 Generation) -> bool
    {
        if (!F.Context() || World < 0 || World > 2 || !Sample ||
            !Flag(F.SnapshotType, Sample, TEXT("Ready")) ||
            Flag(F.SnapshotType, Sample, TEXT("Failed")) ||
            I32(F.SnapshotType, Sample, TEXT("Generation")) != Generation ||
            I32(F.SnapshotType, Sample, TEXT("Sequence")) <= 0) { return false; }
        auto* State = F.States[World].Get();
        if (!IsValid(State) || State->FindFunction(F.CompleteSnapshot->GetFName()) !=
            F.CompleteSnapshot) { return false; }
        FStructOnScope Args(F.CompleteSnapshot);
        const auto* Input = Field<FStructProperty>(F.CompleteSnapshot, TEXT("S"));
        Input->CopyCompleteValue(Input->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), Sample);
        State->ProcessEvent(F.CompleteSnapshot, Args.GetStructMemory());
        return Field<FBoolProperty>(F.CompleteSnapshot, TEXT("ReturnValue"))
            ->GetPropertyValue_InContainer(Args.GetStructMemory());
    }
    auto HeaderComplete(const FFixture& F, const void* Sample, int32 Generation) -> bool
    {
        return Sample && Flag(F.SnapshotType, Sample, TEXT("Ready")) &&
            !Flag(F.SnapshotType, Sample, TEXT("Failed")) &&
            I32(F.SnapshotType, Sample, TEXT("Generation")) == Generation &&
            I32(F.SnapshotType, Sample, TEXT("Sequence")) > 0;
    }
    auto RequestRejected(FFixture& F, int32 Owner) -> bool
    {
        if (!F.Context()) { return false; }
        const int32 Attempt = I32(F.PCClass, F.Remote[Owner].Get(),
                                  TEXT("LastControlAttemptSerial"));
        const int32 Result = I32(F.PCClass, F.Remote[Owner].Get(),
                                 TEXT("LastControlResult"));
        if (Attempt != 1 || Result <= 1) { return false; }
        F.Detail = FString::Printf(TEXT("owner=%d actual server request=%d result=%d"),
                                   Owner, Attempt, Result);
        return true;
    }
    auto Published(FFixture& F, int32 Generation) -> bool
    {
        const void* S = F.Sample(0);
        if (!S || Flag(F.SnapshotType, S, TEXT("Failed"))) { return false; }
        if (!Complete(F, 0, S, Generation))
        { return HeaderComplete(F, S, Generation) ? false : (F.Pump() && false); }
        return true; // Stop pumping as soon as one whole authority sample is Ready.
    }
    auto Presented(FFixture& F, int32 Generation) -> bool
    {
        const void* Server = F.Sample(0);
        if (!Complete(F, 0, Server, Generation)) { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            const void* Client = F.Sample(I + 1);
            const void* Installed = F.Installed->ContainerPtrToValuePtr<void>(F.Local[I].Get());
            if (!Complete(F, I + 1, Client, Generation) ||
                !Complete(F, I + 1, Installed, Generation) ||
                !WireSnapshot(F.SnapshotType, Server, Client) ||
                !F.SnapshotType->CompareScriptStruct(Client, Installed, PPF_None) ||
                !BoolGetter(F.Local[I].Get(), F.Presentation) ||
                !ClientOnlyReplicaTree(F.Worlds[I + 1].Get(), Generation))
            { return false; }
        }
        return F.Dedicated || BoolGetter(F.Host.Get(), F.Presentation);
    }
    // The presentation exposes the actual replicated cube handles and their native
    // components. Probe one component in the same stack to reject a forged pose.
    auto ReplicaComponentConformance(FFixture& F) -> bool
    {
        if (!F.Context() || !Presented(F, 1)) { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            auto* World = F.Worlds[I + 1].Get();
            auto* PC = F.Local[I].Get();
            if (!IsValid(World) || !IsValid(PC) ||
                PC->FindFunction(F.VisualComponents->GetFName()) != F.VisualComponents)
            { return false; }
            FStructOnScope Args(F.VisualComponents);
            PC->ProcessEvent(F.VisualComponents, Args.GetStructMemory());
            const auto* Prop = Field<FArrayProperty>(F.VisualComponents, TEXT("ReturnValue"));
            FScriptArrayHelper Components(Prop,
                Prop->ContainerPtrToValuePtr<void>(Args.GetStructMemory()));
            if (Components.Num() != 11) { return false; }
            for (int32 Slot = 0; Slot < 11; ++Slot)
            {
                const auto Cube = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(
                    World, 1, Slot);
                auto* Mesh = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCubeMesh(
                    World, 1, Slot);
                if (!ck::IsValid(Cube) || !IsValid(Mesh) ||
                    Mesh->Mobility != EComponentMobility::Movable ||
                    *reinterpret_cast<const FCk_Handle_UnrealComponent*>(
                        Components.GetRawPtr(Slot)) !=
                        UCk_Utils_UnrealComponent_UE::TryGet_OwningHandle_FromComponent(Mesh))
                { return false; }
            }
            constexpr int32 Probe = 4; // The rest marker does not follow a mover.
            auto* Mesh = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCubeMesh(
                World, 1, Probe);
            if (!UCk_RotateTowardsGym_Runtime_UE::Get_IsReplicaCubePoseConformant(
                    World, 1, Probe)) { return false; }
            const FTransform Original = Mesh->GetComponentTransform();
            FTransform Wrong = Original;
            Wrong.AddToTranslation(FVector(1.0, 0.0, 0.0));
            Mesh->SetWorldTransform(Wrong);
            const bool Rejected =
                !UCk_RotateTowardsGym_Runtime_UE::Get_IsReplicaCubePoseConformant(
                    World, 1, Probe);
            Mesh->SetWorldTransform(Original);
            if (!Rejected ||
                !UCk_RotateTowardsGym_Runtime_UE::Get_IsReplicaCubePoseConformant(
                    World, 1, Probe)) { return false; }
        }
        return Presented(F, 1);
    }
    auto CommitReady(FFixture& F, int32 Owner, int32 Origin) -> bool
    {
        const void* Server = F.Commit(Owner, false);
        const void* Client = F.Commit(Owner, true);
        if (!Server || !Client || !Complete(F, 0, F.Sample(0), Origin + 1) ||
            !Presented(F, Origin + 1)) { return false; }
        const int32 Serial = I32(F.CommitType, Server, TEXT("RequestSerial"));
        const int32 Delivery = I32(F.CommitType, Server, TEXT("DeliverySerial"));
        if (Serial != 1 || Delivery != 1 ||
            I32(F.CommitType, Server, TEXT("OriginGeneration")) != Origin ||
            I32(F.CommitType, Client, TEXT("RequestSerial")) != Serial ||
            I32(F.CommitType, Client, TEXT("DeliverySerial")) != Delivery ||
            I32(F.CommitType, Client, TEXT("OriginGeneration")) != Origin ||
            I32(F.PCClass, F.Remote[Owner].Get(), TEXT("LastControlAttemptSerial")) != Serial ||
            I32(F.PCClass, F.Remote[Owner].Get(), TEXT("LastControlSerial")) != Serial ||
            I32(F.PCClass, F.Remote[Owner].Get(), TEXT("LastControlResult")) != 1 ||
            I32(F.PCClass, F.Remote[Owner].Get(), TEXT("LastAcknowledgedDeliverySerial")) != Delivery ||
            I32(F.PCClass, F.Local[Owner].Get(), TEXT("LastControlSerial")) != Serial ||
            I32(F.PCClass, F.Local[Owner].Get(), TEXT("LastControlResult")) != 1 ||
            I32(F.PCClass, F.Local[Owner].Get(), TEXT("LastAcknowledgedDeliverySerial")) != Delivery ||
            Object(F.CommitType, Server, TEXT("Owner")) != F.Remote[Owner].Get() ||
            Object(F.CommitType, Client, TEXT("Owner")) != F.Local[Owner].Get() ||
            Object(F.CommitType, Server, TEXT("OwnerPawn")) != F.RemotePawn[Owner].Get() ||
            Object(F.CommitType, Client, TEXT("OwnerPawn")) != F.LocalPawn[Owner].Get() ||
            Object(F.CommitType, Server, TEXT("OriginState")) != F.States[0].Get() ||
            Object(F.CommitType, Client, TEXT("OriginState")) != F.States[Owner + 1].Get())
        { return false; }
        const auto* S = Field<FStructProperty>(F.CommitType, TEXT("Snapshot"));
        const void* ServerSnap = S->ContainerPtrToValuePtr<void>(Server);
        const void* ClientSnap = S->ContainerPtrToValuePtr<void>(Client);
        const void* Installed = nullptr;
        if (F.Local[Owner]->FindFunction(F.InstalledCommit->GetFName()) != F.InstalledCommit)
        { return false; }
        FStructOnScope Args(F.InstalledCommit);
        F.Local[Owner]->ProcessEvent(F.InstalledCommit, Args.GetStructMemory());
        Installed = Field<FStructProperty>(F.InstalledCommit, TEXT("ReturnValue"))
            ->ContainerPtrToValuePtr<void>(Args.GetStructMemory());
        return Complete(F, 0, ServerSnap, Origin + 1) &&
            Complete(F, Owner + 1, ClientSnap, Origin + 1) &&
            F.SnapshotType->CompareScriptStruct(ServerSnap, F.Sample(0), PPF_None) &&
            WireSnapshot(F.SnapshotType, ServerSnap, ClientSnap) &&
            F.CommitType->CompareScriptStruct(Client, Installed, PPF_None);
    }
    auto Freeze(FFixture& F, int32 Owner) -> bool
    {
        const void* C = F.Commit(Owner, false);
        const void* Client = F.Commit(Owner, true);
        if (!C || !Client) { return false; }
        auto Copy = MakeUnique<FStructOnScope>(F.CommitType);
        auto ClientCopy = MakeUnique<FStructOnScope>(F.CommitType);
        F.CommitType->CopyScriptStruct(Copy->GetStructMemory(), C);
        F.CommitType->CopyScriptStruct(ClientCopy->GetStructMemory(), Client);
        if (Owner == 0)
        { F.FrozenA = MoveTemp(Copy); F.FrozenClientA = MoveTemp(ClientCopy);
          F.FrozenAckA = I32(F.PCClass, F.Remote[0].Get(), TEXT("LastAcknowledgedDeliverySerial")); }
        else
        { F.FrozenB = MoveTemp(Copy); F.FrozenClientB = MoveTemp(ClientCopy);
          F.FrozenAckB = I32(F.PCClass, F.Remote[1].Get(), TEXT("LastAcknowledgedDeliverySerial")); }
        return true;
    }
    auto Retained(FFixture& F, int32 Owner) -> bool
    {
        auto* Frozen = Owner == 0 ? F.FrozenA.Get() : F.FrozenB.Get();
        auto* FrozenClient = Owner == 0 ? F.FrozenClientA.Get() : F.FrozenClientB.Get();
        const int32 Ack = Owner == 0 ? F.FrozenAckA : F.FrozenAckB;
        const void* Server = F.Commit(Owner, false);
        const void* Client = F.Commit(Owner, true);
        if (!Frozen || !FrozenClient || !Server || !Client ||
            F.Local[Owner]->FindFunction(F.InstalledCommit->GetFName()) != F.InstalledCommit)
        { return false; }
        FStructOnScope InstalledArgs(F.InstalledCommit);
        F.Local[Owner]->ProcessEvent(F.InstalledCommit, InstalledArgs.GetStructMemory());
        const void* Installed = Field<FStructProperty>(F.InstalledCommit, TEXT("ReturnValue"))
            ->ContainerPtrToValuePtr<void>(InstalledArgs.GetStructMemory());
        return F.CommitType->CompareScriptStruct(Installed, FrozenClient->GetStructMemory(), PPF_None) &&
            F.CommitType->CompareScriptStruct(Server, Frozen->GetStructMemory(), PPF_None) &&
            F.CommitType->CompareScriptStruct(Client, FrozenClient->GetStructMemory(), PPF_None) &&
            I32(F.PCClass, F.Remote[Owner].Get(), TEXT("LastAcknowledgedDeliverySerial")) == Ack &&
            I32(F.PCClass, F.Local[Owner].Get(), TEXT("LastAcknowledgedDeliverySerial")) == Ack &&
            I32(F.CommitType, Client, TEXT("DeliverySerial")) == Ack &&
            I32(F.PCClass, F.Remote[Owner].Get(), TEXT("LastControlAttemptSerial")) == 1 &&
            I32(F.PCClass, F.Remote[Owner].Get(), TEXT("LastControlSerial")) == 1 &&
            I32(F.PCClass, F.Remote[Owner].Get(), TEXT("LastControlResult")) == 1 &&
            I32(F.PCClass, F.Local[Owner].Get(), TEXT("LastControlSerial")) == 1 &&
            I32(F.PCClass, F.Local[Owner].Get(), TEXT("LastControlResult")) == 1 &&
            WireSnapshot(F.SnapshotType,
                Field<FStructProperty>(F.CommitType, TEXT("Snapshot"))->ContainerPtrToValuePtr<void>(Server),
                Field<FStructProperty>(F.CommitType, TEXT("Snapshot"))->ContainerPtrToValuePtr<void>(Client));
    }
    auto Diagnostic(FFixture& F) -> FString
    {
        if (!F.Context()) { return TEXT(" context=lost"); }
        const void* S = F.Sample(0);
        FString Out = FString::Printf(TEXT(" source=%d/%d ready=%d failed=%d"),
            I32(F.SnapshotType, S, TEXT("Generation")),
            I32(F.SnapshotType, S, TEXT("Sequence")),
            Flag(F.SnapshotType, S, TEXT("Ready")), Flag(F.SnapshotType, S, TEXT("Failed")));
        for (int32 I = 0; I < 2; ++I)
        {
            const void* C = F.Commit(I, false);
            const void* L = F.Commit(I, true);
            const void* CS = F.Sample(I + 1);
            Out += FString::Printf(
                TEXT(" owner%d{attempt=%d result=%d serial=%d ack=%d delivery=%d clientDelivery=%d clientAck=%d sample=%d/%d wire=%d presentation=%d}"),
                I, I32(F.PCClass, F.Remote[I].Get(), TEXT("LastControlAttemptSerial")),
                I32(F.PCClass, F.Remote[I].Get(), TEXT("LastControlResult")),
                I32(F.PCClass, F.Remote[I].Get(), TEXT("LastControlSerial")),
                I32(F.PCClass, F.Remote[I].Get(), TEXT("LastAcknowledgedDeliverySerial")),
                I32(F.CommitType, C, TEXT("DeliverySerial")),
                I32(F.CommitType, L, TEXT("DeliverySerial")),
                I32(F.PCClass, F.Local[I].Get(), TEXT("LastAcknowledgedDeliverySerial")),
                I32(F.SnapshotType, CS, TEXT("Generation")),
                I32(F.SnapshotType, CS, TEXT("Sequence")),
                WireSnapshot(F.SnapshotType, S, CS),
                BoolGetter(F.Local[I].Get(), F.Presentation));
        }
        return Out;
    }
    struct FStep { FString Name; TFunction<bool()> Check; bool Wait; };
    class FScenario final : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        TSharedRef<FFixture> F;
        TArray<FStep> Steps;
        int32 Index = 0;
        double Began = -1;
    public:
        FScenario(FAutomationTestBase* T, TSharedRef<FFixture> Fixture, TArray<FStep>&& InSteps)
            : Test(T), F(Fixture), Steps(MoveTemp(InSteps)) {}
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed() || Index >= Steps.Num()) { return true; }
            if (Began < 0) { Began = FPlatformTime::Seconds(); }
            auto& S = Steps[Index];
            if (S.Check()) { ++Index; Began = -1; return Index >= Steps.Num(); }
            if (S.Wait && F->Detail.IsEmpty() && FPlatformTime::Seconds() - Began < 45)
            { return false; }
            const FString Why = FString::Printf(TEXT("RotateTowards two-owner phase %d failed: %s %s"),
                Index, *S.Name, *(F->Detail + Diagnostic(*F)));
            Test->AddError(Why); ck::auto_test::net::FailSession(Why); return true;
        }
    };
    class FRestore final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> F;
    public:
        explicit FRestore(TSharedRef<FFixture> InF) : F(InF) {}
        bool Update() override { F->Restore(); return true; }
    };

    auto Run(FAutomationTestBase& Test, const FString& Topology) -> bool
    {
        auto F = MakeShared<FFixture>();
        if (!ck::auto_test::net::TryGet_DedicatedServer(Topology, F->Dedicated)) { return false; }
        F->Mode = FSoftClassPath(TEXT("/Script/Angelscript.Ck_RotateTowardsGym_GameMode"))
            .TryLoadClass<AGameModeBase>();
        F->StateClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_RotateTowardsGym_GameState"))
            .TryLoadClass<AGameStateBase>();
        F->PCClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_RotateTowardsGym_PlayerController"))
            .TryLoadClass<APlayerController>();
        if (F->StateClass && F->PCClass)
        {
            F->Snapshot = Field<FStructProperty>(F->StateClass, TEXT("Snapshot"));
            F->Installed = Field<FStructProperty>(F->PCClass, TEXT("_InstalledLiveSample"));
            F->LastCommit = Field<FStructProperty>(F->PCClass, TEXT("LastControlCommit"));
            F->SnapshotType = F->Snapshot ? F->Snapshot->Struct : nullptr;
            F->CommitType = F->LastCommit ? F->LastCommit->Struct : nullptr;
            F->Advance = F->StateClass->FindFunctionByName(TEXT("AdvanceAuthority"));
            F->Presentation = F->PCClass->FindFunctionByName(TEXT("Get_RotateTowardsPresentationReady"));
            F->CompleteSnapshot = F->StateClass->FindFunctionByName(TEXT("Get_IsCompleteSnapshot"));
            F->InstalledCommit = F->PCClass->FindFunctionByName(TEXT("Get_InstalledRotateTowardsCommit"));
            F->Rows = F->PCClass->FindFunctionByName(TEXT("Get_ControlRows"));
            F->Draw = F->PCClass->FindFunctionByName(TEXT("Get_DrawTargetsForPlayer"));
            F->Control = F->PCClass->FindFunctionByName(TEXT("Ck_RotateTowards_Control"));
            F->VisualComponents = F->PCClass->FindFunctionByName(TEXT("Get_LocalVisualComponents"));
        }
        if (!Test.TestTrue(TEXT("real Rotate classes and exact reflected schema"), F->Schema()))
        { return false; }
        const auto* Settings = GetDefault<UCkGym_StartupSettings>();
        F->Startup = Settings->StartupMode; F->Last = Settings->LastGymName;
        F->Recents = Settings->RecentGymNames;
        GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
        F->TickGuard = FWorldDelegates::OnWorldTickStart.AddLambda([F](UWorld* W, ELevelTick, float)
        {
            if (!IsValid(W) || W->WorldType != EWorldType::PIE ||
                !IsValid(W->GetGameState()) || W->GetGameState()->GetClass() != F->StateClass ||
                !W->GetGameState()->HasAuthority()) { return; }
            W->GetGameState()->SetActorTickEnabled(false); F->SawGuard = true;
        });
        F->PrivacyTick = FWorldDelegates::OnWorldPostActorTick.AddLambda([F](UWorld* W, ELevelTick, float)
        {
            RecordProductionCubes(*F, W);
            if (F->PrivacyWatch && W == F->Worlds[0].Get()) { ++F->PrivacyTicks; }
        });
        Test.AddCommand(new FCk_Latent_StartPIEMultiClient(3, Map, Topology, F->Mode));
        Test.AddCommand(new FCk_Latent_WaitForPIEReady(3, 60));
        TArray<FStep> Steps;
        auto Act = [&](FString N, TFunction<bool()> C) { Steps.Add({MoveTemp(N), MoveTemp(C), false}); };
        auto Wait = [&](FString N, TFunction<bool()> C) { Steps.Add({MoveTemp(N), MoveTemp(C), true}); };
        Wait(TEXT("three real worlds, two pinned owner/pawn pairs, held authority tick"), [F]()
        { return F->SawGuard && F->Capture(); });
        Wait(TEXT("initial generation whole authority publication"), [F]()
        { return Published(*F, 1); });
        Wait(TEXT("both initial installed presentations are exact"), [F]()
        { return Presented(*F, 1); });
        Wait(TEXT("real replicated cube components reject local pose tampering"), [F]()
        { return ReplicaComponentConformance(*F); });
        Wait(TEXT("capture exact generation-one server and client cube identities"), [F]()
        { return CaptureAndCheckReplicaGeneration(*F, 1); });
        Act(TEXT("client A invokes actual reflected X row"), [F]()
        { return Command(*F, 0, EKeys::X); });
        Wait(TEXT("A accepted, generation two complete and ACKed on real server/client"), [F]()
        {
            if (RequestRejected(*F, 0)) { return false; }
            const void* S = F->Sample(0);
            if (!Complete(*F, 0, S, 2))
            { return HeaderComplete(*F, S, 2) ? false : (F->Pump() && false); }
            return CommitReady(*F, 0, 1);
        });
        Wait(TEXT("generation-one cube replicas retire before distinct generation-two replicas"), [F]()
        { return CaptureAndCheckReplicaGeneration(*F, 2); });
        Act(TEXT("freeze A typed authority commit after exact ACK"), [F]()
        { return Freeze(*F, 0); });
        Act(TEXT("client B invokes independent actual reflected X row"), [F]()
        { return Retained(*F, 0) && Command(*F, 1, EKeys::X); });
        Wait(TEXT("B accepted, generation three complete and ACKed; A retained"), [F]()
        {
            if (RequestRejected(*F, 1)) { return false; }
            if (!Retained(*F, 0)) { return false; }
            const void* S = F->Sample(0);
            if (!Complete(*F, 0, S, 3))
            { return HeaderComplete(*F, S, 3) ? false : (F->Pump() && false); }
            return CommitReady(*F, 1, 2);
        });
        Wait(TEXT("generation-two cube replicas retire before distinct generation-three replicas"), [F]()
        { return CaptureAndCheckReplicaGeneration(*F, 3); });
        Act(TEXT("freeze B and verify owner A remains an older exact commit"), [F]()
        {
            if (!Freeze(*F, 1) || !Retained(*F, 0) || !Retained(*F, 1)) { return false; }
            const void* S = F->Sample(0);
            F->PublicationSequence = I32(F->SnapshotType, S, TEXT("Sequence"));
            return Complete(*F, 0, S, 3) && F->PublicationSequence > 0;
        });
        Wait(TEXT("one later real sequence in generation three reaches both owners"), [F]()
        {
            if (!Retained(*F, 0) || !Retained(*F, 1)) { return false; }
            const void* S = F->Sample(0);
            if (!S || Flag(F->SnapshotType, S, TEXT("Failed"))) { return false; }
            const int32 Seq = I32(F->SnapshotType, S, TEXT("Sequence"));
            if (Seq <= F->PublicationSequence)
            {
                return F->Pump() && false;
            }
            return Complete(*F, 0, S, 3) && Presented(*F, 3) &&
                Retained(*F, 0) && Retained(*F, 1);
        });
        Act(TEXT("A O row changes only A HUD and neither owner serial"), [F]()
        {
            if (!F->Context() || !Retained(*F, 0) || !Retained(*F, 1)) { return false; }
            bool Before[3]{}; int32 Index; bool Active;
            for (int32 I = 0; I < 2; ++I)
            {
                if (!Rows(*F, F->Local[I].Get(), EKeys::O, Index, Active) ||
                    BoolGetter(F->Local[I].Get(), F->Draw) != Active) { return false; }
                Before[I] = Active;
            }
            if (!F->Dedicated)
            {
                if (!Rows(*F, F->Host.Get(), EKeys::O, Index, Active) ||
                    BoolGetter(F->Host.Get(), F->Draw) != Active) { return false; }
                Before[2] = Active;
            }
            const int32 AAttempt = I32(F->PCClass, F->Remote[0].Get(), TEXT("LastControlAttemptSerial"));
            const int32 BAttempt = I32(F->PCClass, F->Remote[1].Get(), TEXT("LastControlAttemptSerial"));
            if (!Command(*F, 0, EKeys::O)) { return false; }
            bool AAfter = false, BAfter = false, HostAfter = Before[2];
            if (!Rows(*F, F->Local[0].Get(), EKeys::O, Index, AAfter) ||
                BoolGetter(F->Local[0].Get(), F->Draw) != AAfter ||
                !Rows(*F, F->Local[1].Get(), EKeys::O, Index, BAfter) ||
                BoolGetter(F->Local[1].Get(), F->Draw) != BAfter) { return false; }
            if (!F->Dedicated &&
                (!Rows(*F, F->Host.Get(), EKeys::O, Index, HostAfter) ||
                 BoolGetter(F->Host.Get(), F->Draw) != HostAfter)) { return false; }
            const bool Private = AAfter != Before[0] && BAfter == Before[1] &&
                HostAfter == Before[2] && Retained(*F, 0) && Retained(*F, 1) &&
                I32(F->PCClass, F->Remote[0].Get(), TEXT("LastControlAttemptSerial")) == AAttempt &&
                I32(F->PCClass, F->Remote[1].Get(), TEXT("LastControlAttemptSerial")) == BAttempt;
            if (Private)
            {
                for (int32 I = 0; I < 3; ++I) { F->BeforeO[I] = Before[I]; }
                F->PrivacyTicks = 0; F->PrivacyWatch = true;
            }
            return Private;
        });
        Wait(TEXT("O remains owner-local and production cube motion reaches both clients"), [F]()
        {
            if (!F->Context() || !Retained(*F, 0) || !Retained(*F, 1) ||
                F->PrivacyTicks < 6 || !ProductionCubesMovedMatched(*F))
            { return false; }
            int32 Index; bool A, B, H = false;
            if (!Rows(*F, F->Local[0].Get(), EKeys::O, Index, A) ||
                A == F->BeforeO[0] ||
                !Rows(*F, F->Local[1].Get(), EKeys::O, Index, B) ||
                B != F->BeforeO[1] ||
                BoolGetter(F->Local[0].Get(), F->Draw) != A ||
                BoolGetter(F->Local[1].Get(), F->Draw) != B) { return false; }
            if (!F->Dedicated &&
                (!Rows(*F, F->Host.Get(), EKeys::O, Index, H) ||
                 H != F->BeforeO[2] ||
                 BoolGetter(F->Host.Get(), F->Draw) != H)) { return false; }
            F->PrivacyWatch = false;
            return true;
        });
        Act(TEXT("normal source EndPlay follows production retirement"), [F]()
        { return F->Context() && F->States[0]->Destroy(); });
        Wait(TEXT("actor-backed source loss retires all generation-three replicas"), [F]()
        {
            if (!F->ReplicaCaptured[2]) { return false; }
            for (int32 World = 0; World < 3; ++World)
            {
                for (int32 Slot = 0; Slot < 11; ++Slot)
                {
                    if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                            F->ReplicaIdentity[2][World][Slot]) ||
                        ck::IsValid(UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(
                            F->Worlds[World].Get(), 3, Slot)))
                    { return false; }
                }
            }
            return true;
        });
        Test.AddCommand(new FScenario(&Test, F, MoveTemp(Steps)));
        Test.AddCommand(new FRestore(F));
        Test.AddCommand(new FCk_Latent_EndPIE());
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkRotateTowardsGym_TwoOwner,
    "Ck.RotateTowardsGym.Network.Control.TwoOwner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkRotateTowardsGym_TwoOwner::RunTest(const FString& Parameters)
{ return ck_rotate_two_owner::Run(*this, Parameters); }
#endif
