#include "CoreMinimal.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkGym_ControlPanelTypes.h"
#include "CkGym_StartupSettings.h"
#include "CkGym_WireTransformRuntime.h"
#include "CkGymStation_VisibleRuntime.h"
#include "CkSceneNodeGym_ActorLedger.h"
#include "CkSceneNodeGym_FrameSampler.h"
#include "CkSceneNodeGym_Motion.h"
#include "CkSceneNodeGym_RemoteVisuals.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace ck_scene_node_production_two_owner
{
    constexpr auto Map = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");

    template<typename T>
    T* Field(const UStruct* Type, const TCHAR* Name)
    { return Type ? FindFProperty<T>(Type, Name) : nullptr; }

    int32 Int(const UStruct* Type, const void* Data, const TCHAR* Name)
    {
        const auto* P = Field<FIntProperty>(Type, Name);
        return P && Data ? P->GetPropertyValue_InContainer(Data) : MIN_int32;
    }

    int64 EnumValue(const UStruct* Type, const void* Data, const TCHAR* Name)
    {
        if (!Type || !Data) { return MIN_int64; }
        if (const auto* E = Field<FEnumProperty>(Type, Name))
        {
            const auto* Underlying = E->GetUnderlyingProperty();
            return Underlying ? Underlying->GetSignedIntPropertyValue(
                E->ContainerPtrToValuePtr<void>(Data)) : MIN_int64;
        }
        if (const auto* B = Field<FByteProperty>(Type, Name))
        { return B->GetPropertyValue_InContainer(Data); }
        return MIN_int64;
    }

    bool WireValue(const FProperty* P, const void* Source, const void* Client)
    {
        if (!P || !Source || !Client) { return false; }
        if (const auto* S = CastField<FStructProperty>(P))
        {
            if (S->Struct == TBaseStructure<FTransform>::Get())
            {
                return UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
                    *static_cast<const FTransform*>(Source),
                    *static_cast<const FTransform*>(Client));
            }
            if (!S->Struct) { return false; }
            for (TFieldIterator<FProperty> It(S->Struct); It; ++It)
            {
                if (!WireValue(*It, It->ContainerPtrToValuePtr<void>(Source),
                    It->ContainerPtrToValuePtr<void>(Client))) { return false; }
            }
            return true;
        }
        if (const auto* A = CastField<FArrayProperty>(P))
        {
            FScriptArrayHelper Left(A, const_cast<void*>(Source));
            FScriptArrayHelper Right(A, const_cast<void*>(Client));
            if (Left.Num() != Right.Num()) { return false; }
            for (int32 I = 0; I < Left.Num(); ++I)
            {
                if (!WireValue(A->Inner, Left.GetRawPtr(I), Right.GetRawPtr(I)))
                { return false; }
            }
            return true;
        }
        return P->Identical(Source, Client, PPF_None);
    }

    bool WireSnapshot(const UScriptStruct* Type, const void* Source, const void* Client)
    {
        if (!Type || !Source || !Client) { return false; }
        for (TFieldIterator<FProperty> It(Type); It; ++It)
        {
            if (!WireValue(*It, It->ContainerPtrToValuePtr<void>(Source),
                It->ContainerPtrToValuePtr<void>(Client))) { return false; }
        }
        return true;
    }

    bool AppliedPose(const AActor* Actor, FCk_Handle& Entity)
    {
        Entity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor);
        if (!ck::IsValid(Entity) || !Entity.Has_All<ck::FFragment_Transform,
                TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>() ||
            Entity.Has_Any<ck::FFragment_SceneNode, ck::SceneNodeParent,
                ck::FFragment_SceneNode_UnrealAnchor, ck::FTag_RepFragments_PendingApply,
                ck::FTag_EntityScript_PendingReplicationRetry>() ||
            UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Entity) != Actor)
        { return false; }
        if (Entity.Has<ck::FFragment_Transform_Requests>())
        {
            const auto& Requests = Entity.Get<ck::FFragment_Transform_Requests>();
            if (!Requests.Get_LocationRequests().IsEmpty() || !Requests.Get_RotationRequests().IsEmpty() ||
                Requests.Get_ScaleRequests().IsSet() || !Requests.Get_ForceRefreshRequests().IsEmpty())
            { return false; }
        }
        auto* const Driver = Entity.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
        if (!IsValid(Driver) || Driver->Get_AssociatedEntity() != Entity) { return false; }
        const auto* L = Driver->FindEntry(FCk_RepData_Location::StaticStruct());
        const auto* R = Driver->FindEntry(FCk_RepData_Rotation::StaticStruct());
        const auto* S = Driver->FindEntry(FCk_RepData_Scale::StaticStruct());
        if (!L || !R || !S || L->_PendingApply || R->_PendingApply || S->_PendingApply ||
            !L->_WasEverApplied || !R->_WasEverApplied || !S->_WasEverApplied ||
            L->_LastAppliedData.GetScriptStruct() != FCk_RepData_Location::StaticStruct() ||
            R->_LastAppliedData.GetScriptStruct() != FCk_RepData_Rotation::StaticStruct() ||
            S->_LastAppliedData.GetScriptStruct() != FCk_RepData_Scale::StaticStruct()) { return false; }
        const FTransform Applied(R->_LastAppliedData.Get<FCk_RepData_Rotation>().Value,
            L->_LastAppliedData.Get<FCk_RepData_Location>().Value,
            S->_LastAppliedData.Get<FCk_RepData_Scale>().Value);
        const auto Pose = Entity.Get<ck::FFragment_Transform>().Get_Transform();
        const auto* Root = Cast<UStaticMeshComponent>(Actor->GetRootComponent());
        return IsValid(Root) && Root->GetOwner() == Actor && Root->GetWorld() == Actor->GetWorld() &&
            Root->IsRegistered() && Root->Mobility == EComponentMobility::Movable &&
            IsValid(Root->GetStaticMesh()) && Pose.IsValid() && !Pose.ContainsNaN() &&
            Pose.Equals(Applied) && Root->GetComponentTransform().Equals(Pose);
    }

    struct FFixture;
    void RecordProduction(FFixture& F, UWorld* World);
    bool ProductionProof(const FFixture& F, int32 Generation, FString& Diagnostic);
    FString ClientInstallDiagnostic(const FFixture& F, int32 Owner);
    struct FStationWitness
    {
        TWeakObjectPtr<UWorld> World;
        FCk_Handle Parent;
        FCk_Handle CohortOwner;
        FCk_Handle Stations[6];
        TWeakObjectPtr<UCk_EntityScript_UE> Scripts[6];
    };

    struct FFixture
    {
        UClass* Mode = nullptr;
        UClass* StateClass = nullptr;
        UClass* PCClass = nullptr;
        UClass* StationClass = nullptr;
        UScriptStruct* SampleType = nullptr;
        UScriptStruct* GeometryType = nullptr;
        UScriptStruct* AckType = nullptr;
        FStructProperty* Snapshot = nullptr;
        FStructProperty* LastAck = nullptr;
        FStrProperty* LocalFailureReason = nullptr;
        FObjectPropertyBase* Actors = nullptr;
        FObjectPropertyBase* RemoteVisuals = nullptr;
        FObjectPropertyBase* Roots = nullptr;
        FObjectPropertyBase* Graph = nullptr;
        FObjectPropertyBase* Motion = nullptr;
        FObjectPropertyBase* Sampler = nullptr;
        UFunction* Advance = nullptr;
        UFunction* Presentation = nullptr;
        UFunction* Installed = nullptr;
        UFunction* AckGetter = nullptr;
        UFunction* Stations = nullptr;
        UFunction* CohortOwner = nullptr;
        UFunction* Rows = nullptr;
        UFunction* Control = nullptr;
        FDelegateHandle TickGuard;
        FDelegateHandle HistoryTick;
        ECkGym_StartupMode Startup{};
        bool bSavedSettings = false;
        FString LastGym;
        TArray<FString> Recents;
        TWeakObjectPtr<UWorld> Worlds[3];
        TWeakObjectPtr<AGameStateBase> States[3];
        TWeakObjectPtr<APlayerController> Local[2];
        TWeakObjectPtr<APlayerController> Remote[2];
        TWeakObjectPtr<APlayerController> Host;
        TWeakObjectPtr<APawn> LocalPawn[2];
        TWeakObjectPtr<APawn> RemotePawn[2];
        bool Dedicated = false;
        bool HoldTick = false;
        bool SawTickGuard = false;
        int64 InitialSequence = 0;
        int64 FirstOwnerSequence[2] = {0, 0};
        int64 SecondOwnerSequence[2] = {0, 0};
        TMap<int64, TUniquePtr<FStructOnScope>> SourceHistory;
        int64 FrozenSequence = 0;
        TUniquePtr<FStructOnScope> FrozenA;
        FStationWitness StationWitnesses[3];
        bool bStationsFrozen = false;
        FString Detail;
        FString PresentationDiagnostic;
        int32 PoseGeneration = 0;
        TArray<FTransform> AuthorityPoses[17];
        FTransform InitialAuthorityPose[17];
        bool HasInitialAuthorityPose[17] = {};
        FCk_Handle LastEntities[3][17];
        int32 LastEntityGeneration[3][17] = {};
        int32 NativeMatches[2][17] = {};
        int32 MeshMatches[2][17] = {};
        int32 MovedMatches[2][17] = {};
        int32 EligiblePoseComparisons[2][17] = {};
        int32 AppliedPoseSkips[2][17] = {};
        FTransform LastClientPose[2][17];
        bool HasLastClientPose[2][17] = {};
        FTransform LastMovedPose[2][17];
        bool HasLastMovedPose[2][17] = {};
        uint64 LastPoseFrame[2][17] = {};
        FString LatestInstallReadout[2];

        ~FFixture() { Restore(); }
        void Restore()
        {
            if (TickGuard.IsValid())
            { FWorldDelegates::OnWorldTickStart.Remove(TickGuard); TickGuard.Reset(); }
            if (HistoryTick.IsValid())
            { FWorldDelegates::OnWorldPostActorTick.Remove(HistoryTick); HistoryTick.Reset(); }
            if (auto* S = States[0].Get()) { S->SetActorTickEnabled(true); }
            if (bSavedSettings)
            {
                auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
                Settings->StartupMode = Startup;
                Settings->LastGymName = LastGym;
                Settings->RecentGymNames = Recents;
                bSavedSettings = false;
            }
        }
        const TCHAR* SchemaIssue() const
        {
            if (!Mode) { return TEXT("GameMode class"); }
            if (!StateClass) { return TEXT("GameState class"); }
            if (!PCClass) { return TEXT("PlayerController class"); }
            if (!StationClass) { return TEXT("Station class"); }
            if (!SampleType) { return TEXT("Snapshot struct"); }
            if (!GeometryType) { return TEXT("VisibleGeometry struct"); }
            if (!AckType) { return TEXT("ControlAck struct"); }
            if (!Snapshot) { return TEXT("Snapshot property"); }
            if (!LastAck) { return TEXT("LastControlAck property"); }
            if (!Actors || !RemoteVisuals || !Roots || !Graph || !Motion || !Sampler)
            { return TEXT("GameState receipt property"); }
            if (!Advance) { return TEXT("AdvanceAuthority function"); }
            if (!Presentation) { return TEXT("PresentationReady function"); }
            if (!Installed) { return TEXT("InstalledSample function"); }
            if (!AckGetter) { return TEXT("ControlAck getter"); }
            if (!Stations) { return TEXT("CapturedStations getter"); }
            if (!CohortOwner) { return TEXT("CohortOwner getter"); }
            if (!Rows) { return TEXT("SceneNodeControlRows function"); }
            if (!Control) { return TEXT("Ck_SceneNode_Control exec"); }
            return TEXT("reflected field type or flags");
        }
        bool Schema() const
        {
            const auto* SampleReturn = Field<FStructProperty>(Installed, TEXT("ReturnValue"));
            const auto* StationReturn = Field<FArrayProperty>(Stations, TEXT("ReturnValue"));
            const auto* StationInner = StationReturn ?
                CastField<FStructProperty>(StationReturn->Inner) : nullptr;
            const auto* RowReturn = Field<FArrayProperty>(Rows, TEXT("ReturnValue"));
            const auto* RowInner = RowReturn ?
                CastField<FStructProperty>(RowReturn->Inner) : nullptr;
            const bool NoPoseWire = Field<FArrayProperty>(SampleType, TEXT("WorldPoses")) == nullptr;
            return Mode && StateClass && PCClass && StationClass && SampleType &&
                GeometryType && AckType && LastAck && LastAck->Struct == AckType &&
                LocalFailureReason &&
                LastAck->HasAnyPropertyFlags(CPF_Net) && AckGetter &&
                Field<FStructProperty>(AckGetter, TEXT("ReturnValue")) &&
                Field<FStructProperty>(AckGetter, TEXT("ReturnValue"))->Struct == AckType &&
                Roots && Graph && Motion && Sampler &&
                Snapshot && Snapshot->Struct == SampleType &&
                Snapshot->HasAnyPropertyFlags(CPF_Net) && Actors && RemoteVisuals &&
                Advance && Advance->NumParms == 0 && Presentation && Installed &&
                Stations && CohortOwner && Rows && Control &&
                Field<FStructProperty>(CohortOwner, TEXT("ReturnValue")) &&
                Field<FStructProperty>(CohortOwner, TEXT("ReturnValue"))->Struct ==
                    FCk_Handle::StaticStruct() &&
                Control->HasAllFunctionFlags(FUNC_Exec) && Control->NumParms == 1 &&
                Field<FIntProperty>(Control, TEXT("Index")) &&
                Field<FBoolProperty>(Presentation, TEXT("ReturnValue")) &&
                SampleReturn && SampleReturn->Struct == SampleType &&
                StationInner && StationInner->Struct == FCk_Handle::StaticStruct() &&
                RowInner && RowInner->Struct == FCkGym_ControlRow::StaticStruct() &&
                NoPoseWire &&
                Field<FIntProperty>(SampleType, TEXT("Generation")) &&
                (Field<FEnumProperty>(SampleType, TEXT("Lifecycle")) ||
                 Field<FByteProperty>(SampleType, TEXT("Lifecycle"))) &&
                Field<FInt64Property>(SampleType, TEXT("Sequence")) &&
                Field<FInt64Property>(SampleType, TEXT("FrameSerial")) &&
                Field<FIntProperty>(AckType, TEXT("RequestSerial")) &&
                Field<FIntProperty>(AckType, TEXT("Generation")) &&
                Field<FIntProperty>(AckType, TEXT("Result"));
        }
        const void* Sample(int32 World) const
        {
            return World >= 0 && World < 3 && States[World].IsValid() && Snapshot ?
                Snapshot->ContainerPtrToValuePtr<void>(States[World].Get()) : nullptr;
        }
        bool Capture()
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || ck::auto_test::net::Get_AllPIEWorlds().Num() != 3 ||
                ck::auto_test::net::Get_NumClientWorlds() != 2 ||
                Server->GetNetMode() != (Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
                !IsValid(Server->GetAuthGameMode()) ||
                Server->GetAuthGameMode()->GetClass() != Mode ||
                !IsValid(Server->GetGameState()) ||
                Server->GetGameState()->GetClass() != StateClass) { return false; }
            Worlds[0] = Server; States[0] = Server->GetGameState();
            if (!Dedicated)
            {
                auto* GI = Server->GetGameInstance();
                if (!IsValid(GI) || GI->GetLocalPlayers().Num() != 1) { return false; }
                auto* LP = GI->GetLocalPlayers()[0];
                auto* H = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
                if (!IsValid(H) || H->GetClass() != PCClass || !H->IsLocalController() ||
                    !H->HasAuthority()) { return false; }
                Host = H;
            }
            else if (!IsValid(Server->GetGameInstance()) ||
                !Server->GetGameInstance()->GetLocalPlayers().IsEmpty()) { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                auto* W = ck::auto_test::net::Get_ClientWorld(I);
                auto* C = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
                auto* R = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
                if (!IsValid(W) || W->GetNetMode() != NM_Client ||
                    !IsValid(W->GetGameState()) || W->GetGameState()->GetClass() != StateClass ||
                    !IsValid(C) || C->GetClass() != PCClass || !C->IsLocalController() ||
                    C->HasAuthority() || !IsValid(R) || R->GetClass() != PCClass ||
                    !R->HasAuthority() || !IsValid(C->GetPawn()) ||
                    !IsValid(R->GetPawn()) || C->GetPawn()->GetController() != C ||
                    R->GetPawn()->GetController() != R) { return false; }
                Worlds[I + 1] = W; States[I + 1] = W->GetGameState();
                Local[I] = C; Remote[I] = R;
                LocalPawn[I] = C->GetPawn(); RemotePawn[I] = R->GetPawn();
            }
            return true;
        }
        bool Context() const
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || Server != Worlds[0].Get() ||
                !IsValid(States[0].Get()) || Server->GetGameState() != States[0].Get() ||
                (HoldTick && States[0]->IsActorTickEnabled())) { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                auto* W = ck::auto_test::net::Get_ClientWorld(I);
                if (!IsValid(W) || W != Worlds[I + 1].Get() ||
                    !IsValid(States[I + 1].Get()) ||
                    !IsValid(Local[I].Get()) || !IsValid(Remote[I].Get()) ||
                    !IsValid(LocalPawn[I].Get()) || !IsValid(RemotePawn[I].Get()) ||
                    W->GetGameState() != States[I + 1].Get() ||
                    W->GetFirstPlayerController() != Local[I].Get() ||
                    ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I) !=
                        Remote[I].Get() || Local[I]->GetPawn() != LocalPawn[I].Get() ||
                    Remote[I]->GetPawn() != RemotePawn[I].Get()) { return false; }
            }
            return Dedicated || Host.IsValid();
        }
        bool Pump() const
        {
            auto* S = States[0].Get();
            if (!HoldTick || !Context() || !IsValid(S) ||
                S->FindFunction(Advance->GetFName()) != Advance)
            { return false; }
            S->ProcessEvent(Advance, nullptr);
            return true;
        }
        void RecordHistory(UWorld* W)
        {
            RecordProduction(*this, W);
            for (int32 Owner = 0; Owner < 2; ++Owner)
            {
                if (W == Worlds[Owner + 1].Get())
                { LatestInstallReadout[Owner] = ClientInstallDiagnostic(*this, Owner); }
            }
            if (!IsValid(W) || W != Worlds[0].Get() || !SampleType || !Snapshot ||
                !IsValid(States[0].Get()) || W->GetGameState() != States[0].Get())
            { return; }
            const void* S = Sample(0);
            const auto* SeqProp = Field<FInt64Property>(SampleType, TEXT("Sequence"));
            const auto* GenProp = Field<FIntProperty>(SampleType, TEXT("Generation"));
            if (!S || !SeqProp || !GenProp ||
                EnumValue(SampleType, S, TEXT("Lifecycle")) != 1 ||
                GenProp->GetPropertyValue_InContainer(S) <= 0) { return; }
            const int64 Seq = SeqProp->GetPropertyValue_InContainer(S);
            if (Seq <= 0 || SourceHistory.Contains(Seq) || SourceHistory.Num() >= 1024)
            { return; }
            auto Copy = MakeUnique<FStructOnScope>(SampleType);
            SampleType->CopyScriptStruct(Copy->GetStructMemory(), S);
            SourceHistory.Add(Seq, MoveTemp(Copy));
        }
    };

    bool PoseNear(const FTransform& A, const FTransform& B)
    { return FVector::Dist(A.GetLocation(), B.GetLocation()) <= 5.0 &&
        FMath::RadiansToDegrees(A.GetRotation().AngularDistance(B.GetRotation())) <= 2.0 &&
        FVector::Dist(A.GetScale3D(), B.GetScale3D()) <= 0.02; }
    bool PoseMoved(const FTransform& A, const FTransform& B)
    { return FVector::Dist(A.GetLocation(), B.GetLocation()) > 5.0 ||
        FMath::RadiansToDegrees(A.GetRotation().AngularDistance(B.GetRotation())) > 2.0; }
    bool NoClientSceneNodeOrTimerTree(const FCk_Handle& Root)
    {
        if (!ck::IsValid(Root)) { return false; }
        TSet<FCk_Handle> Seen;
        TArray<FCk_Handle> Pending{Root};
        while (!Pending.IsEmpty())
        {
            const auto Current = Pending.Pop(EAllowShrinking::No);
            if (!ck::IsValid(Current) || Seen.Contains(Current)) { return false; }
            Seen.Add(Current);
            if (Current.Has_Any<ck::FFragment_SceneNode, ck::SceneNodeParent,
                    ck::FFragment_SceneNode_UnrealAnchor, ck::FFragment_Timer>())
            { return false; }
            for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Current))
            {
                if (ck::IsValid(Child, ck::IsValid_Policy_IncludePendingKill{})) { Pending.Add(Child); }
            }
        }
        return true;
    }
    void RecordProduction(FFixture& F, UWorld* W)
    {
        const int32 Index = W == F.Worlds[0].Get() ? 0 : W == F.Worlds[1].Get() ? 1 :
            W == F.Worlds[2].Get() ? 2 : -1;
        if (Index < 0 || !F.Context()) { return; }
        const int32 Generation = Int(F.SampleType, F.Sample(Index), TEXT("Generation"));
        if (Generation <= 0 || EnumValue(F.SampleType, F.Sample(Index), TEXT("Lifecycle")) != 1)
        { return; }
        if (Index == 0 && F.PoseGeneration != Generation)
        {
            F.PoseGeneration = Generation;
            for (int32 Slot = 0; Slot < 17; ++Slot)
            {
                F.AuthorityPoses[Slot].Reset();
                F.HasInitialAuthorityPose[Slot] = false;
                for (int32 Client = 0; Client < 2; ++Client)
                { F.NativeMatches[Client][Slot] = F.MeshMatches[Client][Slot] = F.MovedMatches[Client][Slot] = 0;
                  F.EligiblePoseComparisons[Client][Slot] = F.AppliedPoseSkips[Client][Slot] = 0;
                  F.HasLastClientPose[Client][Slot] = F.HasLastMovedPose[Client][Slot] = false;
                  F.LastPoseFrame[Client][Slot] = 0; }
            }
        }
        if (Generation != F.PoseGeneration) { return; }
        for (TActorIterator<ACk_SceneNodeGym_SourceActor_UE> It(W); It; ++It)
        {
            const int32 Slot = It->Get_ActorRole() - 1;
            if (Slot < 0 || Slot >= 17 || It->Get_ActorSource() != F.States[Index].Get() ||
                It->Get_ActorGeneration() != Generation || It->IsActorBeingDestroyed()) { continue; }
            auto Entity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(*It);
            if (!ck::IsValid(Entity) || !Entity.Has<ck::FFragment_Transform>()) { continue; }
            if (F.LastEntityGeneration[Index][Slot] != 0 && F.LastEntityGeneration[Index][Slot] != Generation &&
                (F.LastEntities[Index][Slot] == Entity ||
                 ck::IsValid(F.LastEntities[Index][Slot], ck::IsValid_Policy_IncludePendingKill{})))
            { continue; }
            F.LastEntities[Index][Slot] = Entity;
            F.LastEntityGeneration[Index][Slot] = Generation;
            const auto Pose = Entity.Get<ck::FFragment_Transform>().Get_Transform();
            if (Index == 0)
            {
                if (!F.HasInitialAuthorityPose[Slot])
                { F.InitialAuthorityPose[Slot] = Pose; F.HasInitialAuthorityPose[Slot] = true; }
                if (F.AuthorityPoses[Slot].Num() == 512)
                { F.AuthorityPoses[Slot].RemoveAt(0, 1, EAllowShrinking::No); }
                F.AuthorityPoses[Slot].Add(Pose);
                continue;
            }
            F.LastClientPose[Index - 1][Slot] = Pose;
            F.HasLastClientPose[Index - 1][Slot] = true;
            if (F.LastPoseFrame[Index - 1][Slot] == GFrameCounter) { continue; }
            if (!AppliedPose(*It, Entity))
            { ++F.AppliedPoseSkips[Index - 1][Slot]; continue; }
            ++F.EligiblePoseComparisons[Index - 1][Slot];
            bool Native = false, Mesh = false, Moved = false;
            const auto Visible = It->GetRootComponent()->GetComponentTransform();
            for (const auto& SourcePose : F.AuthorityPoses[Slot])
            {
                Native |= PoseNear(Pose, SourcePose);
                Mesh |= PoseNear(Visible, SourcePose);
                Moved |= F.HasInitialAuthorityPose[Slot] && PoseNear(Pose, SourcePose) &&
                    PoseMoved(SourcePose, F.InitialAuthorityPose[Slot]);
            }
            if (Native) { ++F.NativeMatches[Index - 1][Slot]; }
            if (Mesh) { ++F.MeshMatches[Index - 1][Slot]; }
            if (Moved && Mesh && (!F.HasLastMovedPose[Index - 1][Slot] ||
                !PoseNear(Pose, F.LastMovedPose[Index - 1][Slot])))
            {
                ++F.MovedMatches[Index - 1][Slot];
                F.LastMovedPose[Index - 1][Slot] = Pose;
                F.HasLastMovedPose[Index - 1][Slot] = true;
            }
            F.LastPoseFrame[Index - 1][Slot] = GFrameCounter;
        }
    }
    FString PoseReadout(const FTransform& Pose)
    {
        return FString::Printf(TEXT("L=%s R=%s S=%s"),
            *Pose.GetLocation().ToString(), *Pose.GetRotation().Rotator().ToString(),
            *Pose.GetScale3D().ToString());
    }
    FString Role17PoseDiagnostic(const FFixture& F, int32 Client)
    {
        constexpr int32 Slot = 16;
        const auto& History = F.AuthorityPoses[Slot];
        FString Result = FString::Printf(
            TEXT(" authorityHistory=%d authorityEntityGen=%d eligible=%d appliedSkips=%d latestAuthority=[%s] latestClient=[%s]"),
            History.Num(), F.LastEntityGeneration[0][Slot],
            F.EligiblePoseComparisons[Client][Slot], F.AppliedPoseSkips[Client][Slot],
            History.IsEmpty() ? TEXT("none") : *PoseReadout(History.Last()),
            F.HasLastClientPose[Client][Slot] ? *PoseReadout(F.LastClientPose[Client][Slot]) : TEXT("none"));
        if (History.IsEmpty() || !F.HasLastClientPose[Client][Slot])
        { return Result + TEXT(" nearest=unavailable"); }
        const FTransform& ClientPose = F.LastClientPose[Client][Slot];
        int32 NearestIndex = INDEX_NONE;
        double NearestScore = 0.0;
        double NearestLocation = 0.0, NearestAngle = 0.0, NearestScale = 0.0;
        for (int32 I = 0; I < History.Num(); ++I)
        {
            const FTransform& SourcePose = History[I];
            const double Location = FVector::Dist(ClientPose.GetLocation(), SourcePose.GetLocation());
            const double Angle = FMath::RadiansToDegrees(
                ClientPose.GetRotation().AngularDistance(SourcePose.GetRotation()));
            const double Scale = FVector::Dist(ClientPose.GetScale3D(), SourcePose.GetScale3D());
            const double Score = FMath::Max(FMath::Max(Location / 5.0, Angle / 2.0), Scale / 0.02);
            if (!FMath::IsFinite(Score) || (NearestIndex != INDEX_NONE && Score >= NearestScore))
            { continue; }
            NearestIndex = I; NearestScore = Score;
            NearestLocation = Location; NearestAngle = Angle; NearestScale = Scale;
        }
        if (NearestIndex == INDEX_NONE) { return Result + TEXT(" nearest=unavailable"); }
        return Result + FString::Printf(
            TEXT(" nearestIndex=%d nearestAuthority=[%s] nearestDeltaL=%.3fcm nearestDeltaR=%.3fdeg nearestDeltaS=%.5f"),
            NearestIndex, *PoseReadout(History[NearestIndex]),
            NearestLocation, NearestAngle, NearestScale);
    }
    bool ProductionProof(const FFixture& F, int32 Generation, FString& Diagnostic)
    {
        if (F.PoseGeneration != Generation)
        { Diagnostic = FString::Printf(TEXT("pose-generation=%d expected=%d"), F.PoseGeneration, Generation); return false; }
        for (int32 Client = 0; Client < 2; ++Client)
        {
            for (int32 Slot = 0; Slot < 17; ++Slot)
            {
                if (F.NativeMatches[Client][Slot] < 2 || F.MeshMatches[Client][Slot] < 2 ||
                    F.LastEntityGeneration[Client + 1][Slot] != Generation)
                {
                    Diagnostic = FString::Printf(TEXT("client%d role%d native=%d mesh=%d moved=%d entityGen=%d expected=%d"),
                        Client, Slot + 1, F.NativeMatches[Client][Slot], F.MeshMatches[Client][Slot],
                        F.MovedMatches[Client][Slot], F.LastEntityGeneration[Client + 1][Slot], Generation);
                    if (Slot == 16) { Diagnostic += Role17PoseDiagnostic(F, Client); }
                    return false;
                }
            }
            for (const int32 Role : {2, 4, 6, 7, 8, 9, 11, 12, 14, 15, 17})
            {
                if (F.MovedMatches[Client][Role - 1] < 2)
                {
                    Diagnostic = FString::Printf(TEXT("client%d role%d native=%d mesh=%d moved=%d entityGen=%d expected=%d"),
                        Client, Role, F.NativeMatches[Client][Role - 1], F.MeshMatches[Client][Role - 1],
                        F.MovedMatches[Client][Role - 1], F.LastEntityGeneration[Client + 1][Role - 1], Generation);
                    if (Role == 17) { Diagnostic += Role17PoseDiagnostic(F, Client); }
                    return false;
                }
            }
        }
        return true;
    }
    bool BoolGetter(UObject* Object, UFunction* Function)
    {
        if (!IsValid(Object) || !Function ||
            Object->FindFunction(Function->GetFName()) != Function) { return false; }
        FStructOnScope Args(Function);
        Object->ProcessEvent(Function, Args.GetStructMemory());
        const auto* Return = Field<FBoolProperty>(Function, TEXT("ReturnValue"));
        return Return && Return->GetPropertyValue_InContainer(Args.GetStructMemory());
    }

    bool InstalledSample(const FFixture& F, APlayerController* PC,
        const void* Expected)
    {
        if (!IsValid(PC) || !Expected ||
            PC->FindFunction(F.Installed->GetFName()) != F.Installed) { return false; }
        FStructOnScope Args(F.Installed);
        PC->ProcessEvent(F.Installed, Args.GetStructMemory());
        const auto* Return = Field<FStructProperty>(F.Installed, TEXT("ReturnValue"));
        return Return && Return->Struct == F.SampleType &&
            F.SampleType->CompareScriptStruct(Expected,
                Return->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), PPF_None);
    }

    bool CopyInstalled(const FFixture& F, APlayerController* PC,
        FStructOnScope& Out)
    {
        if (!IsValid(PC) || !F.Installed || !F.SampleType ||
            PC->FindFunction(F.Installed->GetFName()) != F.Installed)
        { return false; }
        FStructOnScope Args(F.Installed);
        PC->ProcessEvent(F.Installed, Args.GetStructMemory());
        const auto* Return = Field<FStructProperty>(F.Installed, TEXT("ReturnValue"));
        if (!Return || Return->Struct != F.SampleType) { return false; }
        F.SampleType->CopyScriptStruct(Out.GetStructMemory(),
            Return->ContainerPtrToValuePtr<void>(Args.GetStructMemory()));
        return true;
    }

    const void* Ack(const FFixture& F, APlayerController* PC)
    {
        return IsValid(PC) && F.LastAck ?
            F.LastAck->ContainerPtrToValuePtr<void>(PC) : nullptr;
    }

    bool AckReady(const FFixture& F, int32 Owner, int32 OriginGeneration)
    {
        if (Owner < 0 || Owner > 1 || !F.Context()) { return false; }
        auto* ServerPC = F.Remote[Owner].Get();
        auto* ClientPC = F.Local[Owner].Get();
        const void* Server = Ack(F, ServerPC);
        const void* Client = Ack(F, ClientPC);
        if (!Server || !Client ||
            Int(F.AckType, Server, TEXT("RequestSerial")) != 1 ||
            Int(F.AckType, Server, TEXT("Generation")) != OriginGeneration ||
            Int(F.AckType, Server, TEXT("Result")) != 1 ||
            !F.AckType->CompareScriptStruct(Server, Client, PPF_None) ||
            ClientPC->FindFunction(F.AckGetter->GetFName()) != F.AckGetter)
        { return false; }
        FStructOnScope Args(F.AckGetter);
        ClientPC->ProcessEvent(F.AckGetter, Args.GetStructMemory());
        const auto* Return = Field<FStructProperty>(F.AckGetter, TEXT("ReturnValue"));
        return Return && F.AckType->CompareScriptStruct(Client,
            Return->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), PPF_None);
    }

    FString ExpectedStaticBody(int32 Slot)
    {
        if (Slot == 4)
        {
            return TEXT("Solar system: only the root rotates.\n")
                TEXT("Planet + moon have fixed local offsets and no per-frame updates.\n")
                TEXT("Isolates whether scene-node propagation alone moves descendants.");
        }
        if (Slot == 5)
        {
            return TEXT("SceneNode follows a moving Unreal mesh component at a relative offset.\n")
                TEXT("Composes an existing replicated Transform with an Unreal component anchor.");
        }
        return {};
    }

    bool Active(const FFixture& F, const void* Value, int32 Generation)
    {
        const auto* Poses = Field<FArrayProperty>(F.SampleType, TEXT("WorldPoses"));
        const auto* Titles = Field<FArrayProperty>(F.SampleType, TEXT("BaseTitles"));
        const auto* Bodies = Field<FArrayProperty>(F.SampleType, TEXT("Bodies"));
        const auto* Instructions = Field<FArrayProperty>(F.SampleType, TEXT("Instructions"));
        const auto* Reason = Field<FStrProperty>(F.SampleType, TEXT("FailureReason"));
        if (!Value || Poses || !Titles || !Bodies || !Instructions || !Reason)
        { return false; }
        FScriptArrayHelper TitleRows(Titles, Titles->ContainerPtrToValuePtr<void>(Value));
        FScriptArrayHelper BodyRows(Bodies, Bodies->ContainerPtrToValuePtr<void>(Value));
        FScriptArrayHelper InstructionRows(Instructions,
            Instructions->ContainerPtrToValuePtr<void>(Value));
        const bool Shape = F.Context() && Value &&
            Int(F.SampleType, Value, TEXT("CatalogVersion")) == 1 &&
            EnumValue(F.SampleType, Value, TEXT("Lifecycle")) == 1 &&
            Int(F.SampleType, Value, TEXT("Generation")) == Generation &&
            Field<FInt64Property>(F.SampleType, TEXT("Sequence"))
                ->GetPropertyValue_InContainer(Value) > 0 &&
            Field<FInt64Property>(F.SampleType, TEXT("FrameSerial"))
                ->GetPropertyValue_InContainer(Value) > 0 &&
            Reason->GetPropertyValue_InContainer(Value).IsEmpty() &&
            TitleRows.Num() == 6 &&
            BodyRows.Num() == 6 && InstructionRows.Num() == 6;
        if (!Shape || !CastField<FStrProperty>(Bodies->Inner)) { return false; }
        for (int32 Slot = 4; Slot < 6; ++Slot)
        {
            if (*reinterpret_cast<const FString*>(BodyRows.GetRawPtr(Slot)) !=
                ExpectedStaticBody(Slot)) { return false; }
        }
        return true;
    }

    bool Published(FFixture& F, int32 Generation)
    {
        const void* S = F.Sample(0);
        if (!Active(F, S, Generation))
        { if (F.HoldTick) { F.Pump(); } return false; }
        return true; // Stop manual publication at one complete source frame.
    }

    bool Visuals(const FFixture& F, int32 World, const void* Value,
        FString& Diagnostic)
    {
        if (!F.Context() || World < 0 || World > 2 || !Value)
        { Diagnostic = TEXT("world-context-or-sample"); return false; }
        auto* GS = F.States[World].Get();
        auto* Ledger = World == 0 ? Cast<UCk_SceneNodeGym_ActorLedger_UE>(
            F.Actors->GetObjectPropertyValue_InContainer(GS)) : nullptr;
        auto* Receipt = World > 0 ? Cast<UCk_SceneNodeGym_RemoteVisuals_UE>(
            F.RemoteVisuals->GetObjectPropertyValue_InContainer(GS)) : nullptr;
        if (World == 0)
        {
            if (F.RemoteVisuals->GetObjectPropertyValue_InContainer(GS) != nullptr ||
                F.Roots->GetObjectPropertyValue_InContainer(GS) == nullptr ||
                F.Graph->GetObjectPropertyValue_InContainer(GS) == nullptr ||
                F.Motion->GetObjectPropertyValue_InContainer(GS) == nullptr ||
                F.Sampler->GetObjectPropertyValue_InContainer(GS) == nullptr)
            { Diagnostic = TEXT("authority-records"); return false; }
        }
        else if (F.Actors->GetObjectPropertyValue_InContainer(GS) != nullptr ||
            F.Roots->GetObjectPropertyValue_InContainer(GS) != nullptr ||
            F.Graph->GetObjectPropertyValue_InContainer(GS) != nullptr ||
            F.Motion->GetObjectPropertyValue_InContainer(GS) != nullptr ||
            F.Sampler->GetObjectPropertyValue_InContainer(GS) != nullptr)
        { Diagnostic = TEXT("client-has-authority-records"); return false; }
        if ((World == 0 && (!IsValid(Ledger) ||
                !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(Ledger, true))) ||
            (World > 0 && (!IsValid(Receipt) ||
                UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_State(Receipt) !=
                    ECk_SceneNodeGym_RemoteVisualState::Ready)))
        { Diagnostic = TEXT("ledger-or-remote-receipt-state"); return false; }
        FCkSceneNodeGym_Frame NativeFrame;
        if (World == 0)
        {
            auto* ActualMotion = Cast<UCk_SceneNodeGym_Motion_UE>(
                F.Motion->GetObjectPropertyValue_InContainer(GS));
            auto* ActualSampler = Cast<UCk_SceneNodeGym_FrameSampler_UE>(
                F.Sampler->GetObjectPropertyValue_InContainer(GS));
            if (!IsValid(ActualMotion) || !IsValid(ActualSampler) ||
                !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(ActualMotion) ||
                !UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                    ActualSampler, NativeFrame) || NativeFrame.WorldPoses.Num() != 17 ||
                NativeFrame.Generation != Int(F.SampleType, Value, TEXT("Generation")))
            { Diagnostic = TEXT("authority-motion-or-sampler-frame"); return false; }
        }
        int32 SourceActorCount = 0;

        for (TActorIterator<ACk_SceneNodeGym_SourceActor_UE> It(F.Worlds[World].Get()); It; ++It)
        { if (IsValid(*It) && !It->IsActorBeingDestroyed()) { ++SourceActorCount; } }
        if (SourceActorCount != 17)
        { Diagnostic = FString::Printf(TEXT("source-actor-count=%d"), SourceActorCount); return false; }
        for (int32 Role = 0; Role <= 17; ++Role)
        {
            AActor* Actor = nullptr;
            const bool Found = World == 0 ?
                UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                    Ledger, Role, Actor) :
                UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryGet_FinishedRole(
                    Receipt, Role, Actor);
            if (!Found || !IsValid(Actor) || Actor->GetWorld() != F.Worlds[World].Get() ||
                (Role > 0 && !IsValid(Actor->GetRootComponent())) ||
                (Role == 0 && Actor->GetOwner() != GS))
            { Diagnostic = FString::Printf(TEXT("role%d actor-identity-or-root"), Role); return false; }
            if (Role > 0)
            {
                AActor* Scope = nullptr;
                if (!(World == 0 ?
                    UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                        Ledger, 0, Scope) :
                    UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryGet_FinishedRole(
                        Receipt, 0, Scope)) || Actor->GetOwner() != Scope)
                { Diagnostic = FString::Printf(TEXT("role%d scope-owner"), Role); return false; }
                auto Entity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor);
                if (!ck::IsValid(Entity) || !Entity.Has<ck::FFragment_Transform>() ||
                    !Actor->GetIsReplicated() || Actor->IsReplicatingMovement())
                { Diagnostic = FString::Printf(TEXT("role%d actor-bridge entity=%d transform=%d replicated=%d movement=%d"),
                    Role, ck::IsValid(Entity) ? 1 : 0,
                    ck::IsValid(Entity) && Entity.Has<ck::FFragment_Transform>() ? 1 : 0,
                    Actor->GetIsReplicated() ? 1 : 0, Actor->IsReplicatingMovement() ? 1 : 0); return false; }
                TObjectPtr<UStaticMeshComponent> Mesh = Cast<UStaticMeshComponent>(Actor->GetRootComponent());
                TObjectPtr<UTextRenderComponent> Text = Actor->FindComponentByClass<UTextRenderComponent>();
                if (!IsValid(Mesh.Get()) || Mesh->GetOwner() != Actor || !Mesh->IsVisible() ||
                    !IsValid(Text.Get()) || Text->GetOwner() != Actor || !Text->IsVisible())
                {
                    Diagnostic = FString::Printf(TEXT("role%d mesh=%d meshOwner=%d meshVisible=%d text=%d textOwner=%d textVisible=%d clientTree=not-evaluated"),
                        Role, IsValid(Mesh.Get()) ? 1 : 0,
                        IsValid(Mesh.Get()) && Mesh->GetOwner() == Actor ? 1 : 0,
                        IsValid(Mesh.Get()) && Mesh->IsVisible() ? 1 : 0,
                        IsValid(Text.Get()) ? 1 : 0,
                        IsValid(Text.Get()) && Text->GetOwner() == Actor ? 1 : 0,
                        IsValid(Text.Get()) && Text->IsVisible() ? 1 : 0);
                    return false;
                }
                if (World > 0 && !NoClientSceneNodeOrTimerTree(Entity))
                { Diagnostic = FString::Printf(TEXT("role%d clientTreeClear=0"), Role); return false; }
                const auto Actual = Entity.Get<ck::FFragment_Transform>().Get_Transform();
                if (!Actor->GetRootComponent()->GetComponentTransform().Equals(Actual))
                { Diagnostic = FString::Printf(TEXT("role%d root-pose-mismatch"), Role); return false; }
                if (World > 0 && !AppliedPose(Actor, Entity))
                { Diagnostic = FString::Printf(TEXT("role%d fixture-applied-pose"), Role); return false; }
            }
        }
        return true;
    }

    bool SnapshotText(const FFixture& F, const void* Sample, const TCHAR* Name,
        int32 Slot, FString& Out)
    {
        Out.Reset();
        const auto* Array = Field<FArrayProperty>(F.SampleType, Name);
        if (!Array || !CastField<FStrProperty>(Array->Inner) || !Sample ||
            Slot < 0 || Slot >= 6) { return false; }
        FScriptArrayHelper Values(Array, Array->ContainerPtrToValuePtr<void>(Sample));
        if (Values.Num() != 6) { return false; }
        Out = *reinterpret_cast<const FString*>(Values.GetRawPtr(Slot));
        return true;
    }

    bool Stations(const FFixture& F, APlayerController* PC, const void* Sample,
        FString& Diagnostic)
    {
        if (!IsValid(PC) || !Sample ||
            PC->FindFunction(F.Stations->GetFName()) != F.Stations)
        { Diagnostic = TEXT("controller-or-sample"); return false; }
        FStructOnScope Args(F.Stations);
        PC->ProcessEvent(F.Stations, Args.GetStructMemory());
        const auto* Return = Field<FArrayProperty>(F.Stations, TEXT("ReturnValue"));
        if (!Return || !CastField<FStructProperty>(Return->Inner) ||
            CastField<FStructProperty>(Return->Inner)->Struct != FCk_Handle::StaticStruct())
        { Diagnostic = TEXT("station-return-schema"); return false; }
        FScriptArrayHelper A(Return, Return->ContainerPtrToValuePtr<void>(Args.GetStructMemory()));
        if (A.Num() != 6)
        { Diagnostic = FString::Printf(TEXT("station-count=%d"), A.Num()); return false; }
        TSet<FCk_Handle> Distinct;
        for (int32 I = 0; I < 6; ++I)
        {
            const auto& H = *reinterpret_cast<const FCk_Handle*>(A.GetRawPtr(I));
            if (!ck::IsValid(H) || Distinct.Contains(H))
            { Diagnostic = FString::Printf(TEXT("slot%d invalid-or-duplicate"), I); return false; }
            auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
            if (!IsValid(Script) ||
                Script->GetClass() != F.StationClass ||
                    Script->GetWorld() != PC->GetWorld())
            { Diagnostic = FString::Printf(TEXT("slot%d script-class-or-world"), I); return false; }
            Distinct.Add(H);
            UFunction* Geometry = Script->FindFunction(TEXT("TryGet_VisibleGeometry"));
            const auto* Out = Field<FStructProperty>(Geometry, TEXT("Out"));
            const auto* OK = Field<FBoolProperty>(Geometry, TEXT("ReturnValue"));
            if (!Geometry || !Out || Out->Struct != F.GeometryType || !OK ||
                !Geometry->HasAllFunctionFlags(FUNC_BlueprintPure))
            { Diagnostic = FString::Printf(TEXT("slot%d geometry-function-schema"), I); return false; }
            FStructOnScope G(Geometry);
            Script->ProcessEvent(Geometry, G.GetStructMemory());
            if (!OK->GetPropertyValue_InContainer(G.GetStructMemory()))
            { Diagnostic = FString::Printf(TEXT("slot%d geometry-unavailable"), I); return false; }
            const auto* Pieces = Field<FArrayProperty>(F.GeometryType, TEXT("Pieces"));
            const auto* Anchors = Field<FArrayProperty>(F.GeometryType, TEXT("Anchors"));
            if (!Pieces || !Anchors || !CastField<FStructProperty>(Pieces->Inner) ||
                CastField<FStructProperty>(Pieces->Inner)->Struct !=
                    FCkGym_StationVisiblePiece::StaticStruct())
            { Diagnostic = FString::Printf(TEXT("slot%d geometry-pieces-schema"), I); return false; }
            const void* Data = Out->ContainerPtrToValuePtr<void>(G.GetStructMemory());
            FScriptArrayHelper P(Pieces, Pieces->ContainerPtrToValuePtr<void>(Data));
            FScriptArrayHelper B(Anchors, Anchors->ContainerPtrToValuePtr<void>(Data));
            if (P.Num() != 10 || B.Num() != 7)
            { Diagnostic = FString::Printf(TEXT("slot%d geometry-count pieces=%d anchors=%d"), I, P.Num(), B.Num()); return false; }
            FString Base, Body, Instructions;
            if (!SnapshotText(F, Sample, TEXT("BaseTitles"), I, Base) ||
                !SnapshotText(F, Sample, TEXT("Bodies"), I, Body) ||
                !SnapshotText(F, Sample, TEXT("Instructions"), I, Instructions))
            { Diagnostic = FString::Printf(TEXT("slot%d snapshot-text-unavailable"), I); return false; }
            const FString Role = PC->GetWorld()->GetNetMode() == NM_Client ?
                TEXT("CLIENT") : TEXT("CLIENT+HOST");
            const auto& Title = *reinterpret_cast<const FCkGym_StationVisiblePiece*>(P.GetRawPtr(6));
            const auto& Description = *reinterpret_cast<const FCkGym_StationVisiblePiece*>(P.GetRawPtr(7));
            const auto& Floor = *reinterpret_cast<const FCkGym_StationVisiblePiece*>(P.GetRawPtr(8));
            const auto& Panel = *reinterpret_cast<const FTransform*>(B.GetRawPtr(6));
            const FTransform ExpectedPanel(FRotator(0.0, 180.0, 0.0),
                FVector(500.0, -2000.0 + 800.0 * I, 250.0), FVector::OneVector);
            if (!Panel.Equals(ExpectedPanel, 0.0) ||
                Title.Role != ECkGym_StationVisibleRole::TitleText ||
                Description.Role != ECkGym_StationVisibleRole::DescriptionText ||
                Floor.Role != ECkGym_StationVisibleRole::FloorDescriptionText ||
                !Title.Visible || Title.HiddenInGame ||
                !Description.Visible || Description.HiddenInGame ||
                Title.Text != Base + TEXT(" (") + Role + TEXT(")") ||
                (I >= 4 && Body != ExpectedStaticBody(I)) ||
                Description.Text != Body ||
                (Instructions.IsEmpty() ? Floor.Visible :
                    (!Floor.Visible || Floor.HiddenInGame || Floor.Text != Instructions)))
            {
                Diagnostic = FString::Printf(TEXT("slot%d geometry-or-text panel=%d titleRole=%d descriptionRole=%d floorRole=%d "
                    "titleVisible=%d descriptionVisible=%d floorVisible=%d titleText=%d body=%d descriptionText=%d instructions=%d"),
                    I, Panel.Equals(ExpectedPanel, 0.0) ? 1 : 0,
                    Title.Role == ECkGym_StationVisibleRole::TitleText ? 1 : 0,
                    Description.Role == ECkGym_StationVisibleRole::DescriptionText ? 1 : 0,
                    Floor.Role == ECkGym_StationVisibleRole::FloorDescriptionText ? 1 : 0,
                    Title.Visible && !Title.HiddenInGame ? 1 : 0,
                    Description.Visible && !Description.HiddenInGame ? 1 : 0,
                    Floor.Visible && !Floor.HiddenInGame ? 1 : 0,
                    Title.Text == Base + TEXT(" (") + Role + TEXT(")") ? 1 : 0,
                    I < 4 || Body == ExpectedStaticBody(I) ? 1 : 0,
                    Description.Text == Body ? 1 : 0,
                    Instructions.IsEmpty() ? !Floor.Visible :
                        Floor.Visible && !Floor.HiddenInGame && Floor.Text == Instructions ? 1 : 0);
                return false;
            }
        }
        return true;
    }
    bool Stations(const FFixture& F, APlayerController* PC, const void* Sample)
    {
        FString Ignored;
        return Stations(F, PC, Sample, Ignored);
    }

    bool ReadStationWitness(const FFixture& F, APlayerController* PC,
        FStationWitness& Out)
    {
        Out = {};
        if (!IsValid(PC) || !IsValid(PC->GetWorld()) || !F.CohortOwner ||
            !F.Stations || PC->FindFunction(F.CohortOwner->GetFName()) != F.CohortOwner ||
            PC->FindFunction(F.Stations->GetFName()) != F.Stations)
        { return false; }
        const FCk_Handle Parent = UCk_Utils_OwningActor_UE::
            TryGet_ActorEntityHandle(PC);
        if (!ck::IsValid(Parent)) { return false; }
        FStructOnScope OwnerArgs(F.CohortOwner);
        PC->ProcessEvent(F.CohortOwner, OwnerArgs.GetStructMemory());
        const auto* OwnerReturn = Field<FStructProperty>(F.CohortOwner,
            TEXT("ReturnValue"));
        if (!OwnerReturn || OwnerReturn->Struct != FCk_Handle::StaticStruct())
        { return false; }
        const auto& Owner = *reinterpret_cast<const FCk_Handle*>(
            OwnerReturn->ContainerPtrToValuePtr<void>(OwnerArgs.GetStructMemory()));
        if (!ck::IsValid(Owner) || Owner == Parent ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Owner) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Owner) != Parent)
        { return false; }
        FStructOnScope StationArgs(F.Stations);
        PC->ProcessEvent(F.Stations, StationArgs.GetStructMemory());
        const auto* StationReturn = Field<FArrayProperty>(F.Stations,
            TEXT("ReturnValue"));
        const auto* StationInner = StationReturn ?
            CastField<FStructProperty>(StationReturn->Inner) : nullptr;
        if (!StationInner || StationInner->Struct != FCk_Handle::StaticStruct())
        { return false; }
        FScriptArrayHelper Values(StationReturn,
            StationReturn->ContainerPtrToValuePtr<void>(StationArgs.GetStructMemory()));
        if (Values.Num() != 6) { return false; }
        TSet<FCk_Handle> Distinct;
        Out.World = PC->GetWorld(); Out.Parent = Parent; Out.CohortOwner = Owner;
        for (int32 I = 0; I < 6; ++I)
        {
            const auto& Station = *reinterpret_cast<const FCk_Handle*>(
                Values.GetRawPtr(I));
            if (!ck::IsValid(Station) || Distinct.Contains(Station) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Station) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Station) != Owner)
            { return false; }
            auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Station);
            if (!IsValid(Script) || Script->GetWorld() != PC->GetWorld() ||
                Script->GetClass() != F.StationClass)
            { return false; }
            Out.Stations[I] = Station; Out.Scripts[I] = Script;
            Distinct.Add(Station);
        }
        return true;
    }

    bool SameStationWitness(const FStationWitness& Frozen,
        const FStationWitness& Actual)
    {
        if (!Frozen.World.IsValid() || Frozen.World != Actual.World ||
            Frozen.Parent != Actual.Parent ||
            Frozen.CohortOwner != Actual.CohortOwner) { return false; }
        for (int32 I = 0; I < 6; ++I)
        {
            if (!Frozen.Scripts[I].IsValid() ||
                Frozen.Stations[I] != Actual.Stations[I] ||
                Frozen.Scripts[I] != Actual.Scripts[I]) { return false; }
        }
        return true;
    }

    bool ProveStationIdentities(FFixture& F, int32 Generation)
    {
        FStationWitness Current[3];
        const int32 Count = F.Dedicated ? 2 : 3;
        for (int32 I = 0; I < Count; ++I)
        {
            auto* PC = I < 2 ? F.Local[I].Get() : F.Host.Get();
            if (!ReadStationWitness(F, PC, Current[I])) { return false; }
            for (int32 J = 0; J < I; ++J)
            {
                if (Current[I].World == Current[J].World) { return false; }
                for (int32 A = 0; A < 6; ++A)
                {
                    for (int32 B = 0; B < 6; ++B)
                    {
                        if (Current[I].Scripts[A] == Current[J].Scripts[B])
                        { return false; }
                    }
                }
            }
            if (F.bStationsFrozen &&
                !SameStationWitness(F.StationWitnesses[I], Current[I]))
            { return false; }
        }
        if (!F.bStationsFrozen)
        {
            if (Generation != 1) { return false; }
            for (int32 I = 0; I < Count; ++I)
            { F.StationWitnesses[I] = Current[I]; }
            F.bStationsFrozen = true;
        }
        return true;
    }

    int32 CapturedStationCount(const FFixture& F, APlayerController* PC)
    {
        if (!IsValid(PC) || !F.Stations ||
            PC->FindFunction(F.Stations->GetFName()) != F.Stations)
        { return INDEX_NONE; }
        FStructOnScope Args(F.Stations);
        PC->ProcessEvent(F.Stations, Args.GetStructMemory());
        const auto* Return = Field<FArrayProperty>(F.Stations, TEXT("ReturnValue"));
        if (!Return) { return INDEX_NONE; }
        FScriptArrayHelper A(Return, Return->ContainerPtrToValuePtr<void>(Args.GetStructMemory()));
        return A.Num();
    }

    bool Presented(FFixture& F, int32 Generation)
    {
        F.PresentationDiagnostic.Reset();
        FString Diagnostic;
        const void* Source = F.Sample(0);
        if (!Active(F, Source, Generation))
        { F.PresentationDiagnostic = TEXT("source Active"); return false; }
        if (!Visuals(F, 0, Source, Diagnostic))
        { F.PresentationDiagnostic = FString::Printf(TEXT("source Visuals: %s"), *Diagnostic); return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            const void* Client = F.Sample(I + 1);
            if (!Active(F, Client, Generation))
            { F.PresentationDiagnostic = FString::Printf(TEXT("client%d Active"), I); return false; }
            if (!WireSnapshot(F.SampleType, Source, Client))
            { F.PresentationDiagnostic = FString::Printf(TEXT("client%d WireSnapshot"), I); return false; }
            if (!InstalledSample(F, F.Local[I].Get(), Client))
            { F.PresentationDiagnostic = FString::Printf(TEXT("client%d InstalledSample"), I); return false; }
            if (!BoolGetter(F.Local[I].Get(), F.Presentation))
            { F.PresentationDiagnostic = FString::Printf(TEXT("client%d PresentationReady"), I); return false; }
            if (!Visuals(F, I + 1, Client, Diagnostic))
            { F.PresentationDiagnostic = FString::Printf(TEXT("client%d Visuals: %s"), I, *Diagnostic); return false; }
            if (!Stations(F, F.Local[I].Get(), Client, Diagnostic))
            { F.PresentationDiagnostic = FString::Printf(TEXT("client%d Stations: %s"), I, *Diagnostic); return false; }
            if (CapturedStationCount(F, F.Remote[I].Get()) != 0)
            { F.PresentationDiagnostic = FString::Printf(TEXT("client%d remote station count"), I); return false; }
        }
        if (!F.Dedicated)
        {
            if (!BoolGetter(F.Host.Get(), F.Presentation))
            { F.PresentationDiagnostic = TEXT("host PresentationReady"); return false; }
            if (!InstalledSample(F, F.Host.Get(), Source))
            { F.PresentationDiagnostic = TEXT("host InstalledSample"); return false; }
            if (!Stations(F, F.Host.Get(), Source, Diagnostic))
            { F.PresentationDiagnostic = FString::Printf(TEXT("host Stations: %s"), *Diagnostic); return false; }
        }
        if (!ProveStationIdentities(F, Generation))
        { F.PresentationDiagnostic = TEXT("ProveStationIdentities"); return false; }
        if (!ProductionProof(F, Generation, Diagnostic))
        { F.PresentationDiagnostic = FString::Printf(TEXT("ProductionProof: %s"), *Diagnostic); return false; }
        return true;
    }

    bool OwnerLiveWhilePublishing(FFixture& F, int32 Owner,
        int32 Generation, int64& ObservedSequence)
    {
        if (Owner < 0 || Owner > 1 || !F.Context()) { return false; }
        FStructOnScope Installed(F.SampleType);
        if (!CopyInstalled(F, F.Local[Owner].Get(), Installed) ||
            !Active(F, Installed.GetStructMemory(), Generation)) { return false; }
        const void* Client = Installed.GetStructMemory();
        const int64 Seq = Field<FInt64Property>(F.SampleType, TEXT("Sequence"))
            ->GetPropertyValue_InContainer(Client);
        const auto* Source = F.SourceHistory.Find(Seq);
        if (!Source || !*Source ||
            !WireSnapshot(F.SampleType, (*Source)->GetStructMemory(), Client) ||
            !Stations(F, F.Local[Owner].Get(), Client)) { return false; }
        ObservedSequence = Seq;
        return true;
    }

    bool InvokeR(FFixture& F, int32 Owner)
    {
        if (!F.Context() || Owner < 0 || Owner > 1) { return false; }
        auto* PC = F.Local[Owner].Get();
        if (!IsValid(PC) || PC->FindFunction(F.Control->GetFName()) != F.Control)
        { return false; }
        FStructOnScope RowsArgs(F.Rows);
        PC->ProcessEvent(F.Rows, RowsArgs.GetStructMemory());
        const auto* Return = Field<FArrayProperty>(F.Rows, TEXT("ReturnValue"));
        if (!Return) { return false; }
        FScriptArrayHelper Rows(Return, Return->ContainerPtrToValuePtr<void>(RowsArgs.GetStructMemory()));
        if (Rows.Num() < 1) { return false; }
        const auto& Row = *reinterpret_cast<const FCkGym_ControlRow*>(Rows.GetRawPtr(0));
        if (Row.Key != EKeys::R || !Row.Enabled ||
            Row.Kind != ECkGym_ControlKind::Action) { return false; }
        FStructOnScope Args(F.Control);
        Field<FIntProperty>(F.Control, TEXT("Index"))
            ->SetPropertyValue_InContainer(Args.GetStructMemory(), 0);
        PC->ProcessEvent(F.Control, Args.GetStructMemory());
        return true;
    }

    FString SnapshotDiagnostic(const FFixture& F, int32 WorldIndex)
    {
        if (WorldIndex < 0 || WorldIndex > 2) { return TEXT("index-invalid"); }
        auto* World = F.Worlds[WorldIndex].Get();
        auto* State = F.States[WorldIndex].Get();
        if (!IsValid(World) || !IsValid(State) || State->GetWorld() != World ||
            World->GetGameState() != State || !F.SampleType || !F.Snapshot)
        { return TEXT("world-or-state-unavailable"); }
        const void* S = F.Sample(WorldIndex);
        const auto* Seq = Field<FInt64Property>(F.SampleType, TEXT("Sequence"));
        const auto* Frame = Field<FInt64Property>(F.SampleType, TEXT("FrameSerial"));
        const auto* Reason = Field<FStrProperty>(F.SampleType, TEXT("FailureReason"));
        if (!S || !Seq || !Frame || !Reason) { return TEXT("sample-schema-unavailable"); }
        return FString::Printf(TEXT("life=%lld gen=%d seq=%lld frame=%lld reason='%s'"),
            EnumValue(F.SampleType, S, TEXT("Lifecycle")),
            Int(F.SampleType, S, TEXT("Generation")),
            Seq->GetPropertyValue_InContainer(S),
            Frame->GetPropertyValue_InContainer(S),
            *Reason->GetPropertyValue_InContainer(S));
    }

    FString ClientInstallDiagnostic(const FFixture& F, int32 Owner)
    {
        if (Owner < 0 || Owner > 1) { return TEXT("owner-invalid"); }
        const int32 Index = Owner + 1;
        const TWeakObjectPtr<UWorld> World = F.Worlds[Index];
        const TWeakObjectPtr<AGameStateBase> State = F.States[Index];
        if (!World.IsValid() || !State.IsValid() || State->GetWorld() != World.Get() ||
            World->GetGameState() != State.Get() || !F.SampleType || !F.RemoteVisuals)
        { return TEXT("client-context-unavailable"); }
        if (!F.Sample(Index) || !Field<FInt64Property>(F.SampleType, TEXT("Sequence")) ||
            !Field<FInt64Property>(F.SampleType, TEXT("FrameSerial")))
        { return TEXT("received-sample-unavailable"); }
        const FInt64Property& Seq = *Field<FInt64Property>(F.SampleType, TEXT("Sequence"));
        const FInt64Property& Frame = *Field<FInt64Property>(F.SampleType, TEXT("FrameSerial"));
        const int32 Generation = Int(F.SampleType, F.Sample(Index), TEXT("Generation"));
        const TWeakObjectPtr<UCk_SceneNodeGym_RemoteVisuals_UE> Receipt =
            Cast<UCk_SceneNodeGym_RemoteVisuals_UE>(
                F.RemoteVisuals->GetObjectPropertyValue_InContainer(State.Get()));
        FString First = TEXT("all17-applied");
        for (int32 Role = 1; Role <= 17; ++Role)
        {
            TWeakObjectPtr<ACk_SceneNodeGym_SourceActor_UE> Candidate;
            int32 Matches = 0;
            for (TActorIterator<ACk_SceneNodeGym_SourceActor_UE> It(World.Get()); It; ++It)
            {
                if (It->Get_ActorSource() != State.Get() ||
                    It->Get_ActorGeneration() != Generation ||
                    It->Get_ActorRole() != Role) { continue; }
                Candidate = *It;
                ++Matches;
            }
            if (Matches != 1 || !Candidate.IsValid())
            {
                First = FString::Printf(TEXT("role%d actor-matches=%d"), Role, Matches);
                break;
            }
            const FString Reason = UCk_SceneNodeGym_RemoteVisualRuntime_UE::
                DescribeAppliedPose(TWeakObjectPtr<const AActor>{Candidate.Get()});
            if (Reason != TEXT("applied-pose-conformant"))
            {
                First = FString::Printf(TEXT("role%d %s"), Role, *Reason);
                break;
            }
        }
        return FString::Printf(TEXT("recvGen=%d recvSeq=%lld recvFrame=%lld installedSeq=%lld installedFrame=%lld first=%s"),
            Generation, Seq.GetPropertyValue_InContainer(F.Sample(Index)),
            Frame.GetPropertyValue_InContainer(F.Sample(Index)),
            Receipt.IsValid() ? UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_InstalledSequence(Receipt.Get()) : -1,
            Receipt.IsValid() ? UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_InstalledFrameSerial(Receipt.Get()) : -1,
            *First);
    }

    FString OwnerDiagnostic(const FFixture& F, int32 Owner)
    {
        if (Owner < 0 || Owner > 1) { return TEXT("index-invalid"); }
        auto* PC = F.Local[Owner].Get();
        if (!IsValid(PC) || PC->GetWorld() != F.Worlds[Owner + 1].Get())
        { return TEXT("pc-unavailable"); }
        FStructOnScope Installed(F.SampleType);
        const bool Copied = CopyInstalled(F, PC, Installed);
        const void* S = Copied ? Installed.GetStructMemory() : nullptr;
        const auto* Seq = Field<FInt64Property>(F.SampleType, TEXT("Sequence"));
        const int32 Count = CapturedStationCount(F, PC);
        const FString LocalReason = F.LocalFailureReason ?
            F.LocalFailureReason->GetPropertyValue_InContainer(PC) :
            TEXT("local-reason-unavailable");
        auto* State = F.States[Owner + 1].Get();
        UCk_SceneNodeGym_RemoteVisuals_UE* Receipt = nullptr;
        if (IsValid(State) && State->GetWorld() == F.Worlds[Owner + 1].Get() &&
            F.RemoteVisuals)
        { Receipt = Cast<UCk_SceneNodeGym_RemoteVisuals_UE>(
            F.RemoteVisuals->GetObjectPropertyValue_InContainer(State)); }
        const bool RemoteLive = IsValid(Receipt);
        const int32 RemoteState = RemoteLive ? static_cast<int32>(
            UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_State(Receipt)) : -1;
        const int32 RemoteGeneration = RemoteLive ?
            UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_Generation(Receipt) : -1;
        const int64 RemoteSequence = RemoteLive ?
            UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_InstalledSequence(Receipt) : -1;
        const FString RemoteReason = RemoteLive ?
            UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_FailureReason(Receipt).ToString() :
            TEXT("receipt-unavailable");
        return FString::Printf(
            TEXT("installedGen=%d installedSeq=%lld stations=%d localReason='%s' "
                 "remoteState=%d remoteGen=%d remoteSeq=%lld remoteReason='%s' "
                 "poll=[%s] lastPostTick=[%s]"),
            S ? Int(F.SampleType, S, TEXT("Generation")) : -1,
            S && Seq ? Seq->GetPropertyValue_InContainer(S) : -1,
            Count, *LocalReason, RemoteState, RemoteGeneration,
            RemoteSequence, *RemoteReason,
            *ClientInstallDiagnostic(F, Owner), *F.LatestInstallReadout[Owner]);
    }

    FString RemoteClassDiagnostic(const TCHAR* Path)
    {
        UClass* const Class = FindObject<UClass>(nullptr, Path);
        if (!IsValid(Class) ||
            Class->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return TEXT("class-unavailable"); }
        auto* const Default = Cast<ACk_SceneNodeGym_SourceActor_UE>(
            Class->GetDefaultObject(false));
        if (!IsValid(Default) ||
            Default->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return TEXT("cdo-unavailable"); }
        UStaticMeshComponent* const Mesh = Cast<UStaticMeshComponent>(Default->GetRootComponent());
        if (!IsValid(Mesh) ||
            Mesh->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return TEXT("mesh-unavailable"); }
        UStaticMesh* const Asset = Mesh->GetStaticMesh().Get();
        const bool AssetLive = IsValid(Asset) &&
            !Asset->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
        return FString::Printf(
            TEXT("root=%d asset=%d path='%s'"),
            Default->GetRootComponent() == Mesh ? 1 : 0,
            AssetLive ? 1 : 0,
            AssetLive ? *Asset->GetPathName() : TEXT(""));
    }

    FString FailureDiagnostic(const FFixture& F)
    {
        FString Result = FString::Printf(TEXT("context=%d history=%d source=[%s]"),
            F.Context() ? 1 : 0, F.SourceHistory.Num(),
            *SnapshotDiagnostic(F, 0));
        auto* Source = F.States[0].Get();
        if (IsValid(Source) && Source->GetWorld() == F.Worlds[0].Get())
        {
            Result += FString::Printf(
                TEXT(" sourceRecords=%d/%d/%d/%d/%d"),
                F.Actors->GetObjectPropertyValue_InContainer(Source) ? 1 : 0,
                F.Roots->GetObjectPropertyValue_InContainer(Source) ? 1 : 0,
                F.Graph->GetObjectPropertyValue_InContainer(Source) ? 1 : 0,
                F.Motion->GetObjectPropertyValue_InContainer(Source) ? 1 : 0,
                F.Sampler->GetObjectPropertyValue_InContainer(Source) ? 1 : 0);
        }
        Result += FString::Printf(
            TEXT(" remoteParentCDO=[%s] remoteChildCDO=[%s]"),
            *RemoteClassDiagnostic(
                TEXT("/Script/Angelscript.Ck_SceneNodeGym_SourceParent")),
            *RemoteClassDiagnostic(
                TEXT("/Script/Angelscript.Ck_SceneNodeGym_SourceChild")));
        for (int32 I = 0; I < 2; ++I)
        {
            Result += FString::Printf(TEXT(" client%d=[%s] owner%d=[%s]"),
                I, *SnapshotDiagnostic(F, I + 1), I, *OwnerDiagnostic(F, I));
        }
        return Result;
    }
    struct FStep { FString Name; TFunction<bool()> Check; bool Wait = false; };
    class FScenario final : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        TSharedRef<FFixture> F;
        TArray<FStep> Steps;
        int32 Index = 0;
        double Began = -1;
    public:
        FScenario(FAutomationTestBase* InTest, TSharedRef<FFixture> Fixture,
            TArray<FStep>&& InSteps)
            : Test(InTest), F(Fixture), Steps(MoveTemp(InSteps)) {}
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed() || Index >= Steps.Num())
            { return true; }
            if (Began < 0) { Began = FPlatformTime::Seconds(); }
            auto& S = Steps[Index];
            if (S.Check()) { ++Index; Began = -1; return Index >= Steps.Num(); }
            if (S.Wait && F->Detail.IsEmpty() && FPlatformTime::Seconds() - Began < 45)
            { return false; }
            if (F->Detail.IsEmpty() &&
                S.Name == TEXT("both owners converge on frozen source frame"))
            { F->Detail = F->SawTickGuard ? F->PresentationDiagnostic :
                TEXT("SawTickGuard=0; Presented not evaluated"); }
            const FString Observed = FailureDiagnostic(*F);
            const FString Why = FString::Printf(
                TEXT("SceneNode production two-owner phase %d failed: %s %s | %s"),
                Index, *S.Name, *F->Detail, *Observed);
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

    bool Run(FAutomationTestBase& Test, const FString& Topology)
    {
        auto F = MakeShared<FFixture>();
        if (!ck::auto_test::net::TryGet_DedicatedServer(Topology, F->Dedicated))
        { return false; }
        F->Mode = FSoftClassPath(TEXT("/Script/Angelscript.Ck_SceneNodeGym_GameMode"))
            .TryLoadClass<AGameModeBase>();
        F->StateClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_SceneNodeGym_GameState"))
            .TryLoadClass<AGameStateBase>();
        F->PCClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_SceneNodeGym_PlayerController"))
            .TryLoadClass<APlayerController>();
        F->StationClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_EntityScript_GymStation"))
            .TryLoadClass<UCk_EntityScript_UE>();
        F->GeometryType = FindObject<UScriptStruct>(nullptr,
            TEXT("/Script/Angelscript.CkGym_StationVisibleGeometry"));
        if (F->StateClass && F->PCClass)
        {
            F->Snapshot = Field<FStructProperty>(F->StateClass, TEXT("Snapshot"));
            F->LastAck = Field<FStructProperty>(F->PCClass, TEXT("LastControlAck"));
            F->LocalFailureReason = Field<FStrProperty>(F->PCClass,
                TEXT("_LocalFailureReason"));
            F->AckType = F->LastAck ? F->LastAck->Struct : nullptr;
            F->Actors = Field<FObjectPropertyBase>(F->StateClass, TEXT("_Actors"));
            F->RemoteVisuals = Field<FObjectPropertyBase>(F->StateClass,
                TEXT("_RemoteVisuals"));
            F->Roots = Field<FObjectPropertyBase>(F->StateClass, TEXT("_Roots"));
            F->Graph = Field<FObjectPropertyBase>(F->StateClass, TEXT("_Graph"));
            F->Motion = Field<FObjectPropertyBase>(F->StateClass, TEXT("_Motion"));
            F->Sampler = Field<FObjectPropertyBase>(F->StateClass, TEXT("_Sampler"));
            F->SampleType = F->Snapshot ? F->Snapshot->Struct : nullptr;
            F->Advance = F->StateClass->FindFunctionByName(TEXT("AdvanceAuthority"));
            F->Presentation = F->PCClass->FindFunctionByName(
                TEXT("Get_SceneNodePresentationReady"));
            F->Installed = F->PCClass->FindFunctionByName(
                TEXT("Get_InstalledSceneNodeSample"));
            F->AckGetter = F->PCClass->FindFunctionByName(
                TEXT("Get_SceneNodeControlAck"));
            F->Stations = F->PCClass->FindFunctionByName(
                TEXT("Get_CapturedSceneNodeStations"));
            F->CohortOwner = F->PCClass->FindFunctionByName(
                TEXT("Get_CapturedSceneNodeCohortOwner"));
            F->Rows = F->PCClass->FindFunctionByName(TEXT("Get_SceneNodeControlRows"));
            F->Control = F->PCClass->FindFunctionByName(TEXT("Ck_SceneNode_Control"));
        }
        const bool ValidSchema = F->Schema();
        if (!ValidSchema) { F->Detail = F->SchemaIssue(); }
        const FString SchemaLabel = FString::Printf(
            TEXT("real SceneNode classes and reflected schema: %s"), *F->Detail);
        if (!Test.TestTrue(SchemaLabel, ValidSchema))
        { return false; }
        const auto* Settings = GetDefault<UCkGym_StartupSettings>();
        F->Startup = Settings->StartupMode; F->LastGym = Settings->LastGymName;
        F->Recents = Settings->RecentGymNames;
        F->bSavedSettings = true;
        GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
        TWeakPtr<FFixture> Weak(F);
        F->TickGuard = FWorldDelegates::OnWorldTickStart.AddLambda(
            [Weak](UWorld* W, ELevelTick, float)
            {
                auto Pinned = Weak.Pin();
                if (!Pinned.IsValid() || !IsValid(W) || W->WorldType != EWorldType::PIE ||
                    !IsValid(W->GetGameState()) ||
                    W->GetGameState()->GetClass() != Pinned->StateClass ||
                    !W->GetGameState()->HasAuthority() || !Pinned->HoldTick) { return; }
                W->GetGameState()->SetActorTickEnabled(false);
                Pinned->SawTickGuard = true;
            });
        F->HistoryTick = FWorldDelegates::OnWorldPostActorTick.AddLambda(
            [Weak](UWorld* W, ELevelTick, float)
            {
                auto Pinned = Weak.Pin();
                if (Pinned.IsValid()) { Pinned->RecordHistory(W); }
            });
        Test.AddCommand(new FCk_Latent_StartPIEMultiClient(3, Map, Topology, F->Mode));
        Test.AddCommand(new FCk_Latent_WaitForPIEReady(3, 60));
        TArray<FStep> Steps;
        auto Act = [&](FString Name, TFunction<bool()> Check)
        { Steps.Add({MoveTemp(Name), MoveTemp(Check), false}); };
        auto Wait = [&](FString Name, TFunction<bool()> Check)
        { Steps.Add({MoveTemp(Name), MoveTemp(Check), true}); };
        Wait(TEXT("three real worlds and two pinned owner pairs"), [F]()
        { return F->Capture(); });
        Wait(TEXT("first source generation and both live client presentations"), [F]()
        {
            if (!Published(*F, 1)) { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                if (F->FirstOwnerSequence[I] == 0)
                { OwnerLiveWhilePublishing(*F, I, 1, F->FirstOwnerSequence[I]); }
            }
            return F->FirstOwnerSequence[0] > 0 && F->FirstOwnerSequence[1] > 0;
        });
        Wait(TEXT("continuous source updates produce a newer installed frame"), [F]()
        {
            if (!F->Context() || !Active(*F, F->Sample(0), 1)) { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                if (F->SecondOwnerSequence[I] == 0)
                {
                    int64 Current = 0;
                    if (OwnerLiveWhilePublishing(*F, I, 1, Current) &&
                        Current > F->FirstOwnerSequence[I])
                    { F->SecondOwnerSequence[I] = Current; }
                }
            }
            return F->SecondOwnerSequence[0] > F->FirstOwnerSequence[0] &&
                F->SecondOwnerSequence[1] > F->FirstOwnerSequence[1];
        });
        Act(TEXT("hold authority publication after real continuous presentation"), [F]()
        {
            if (!F->Context() || !IsValid(F->States[0].Get())) { return false; }
            F->HoldTick = true;
            F->States[0]->SetActorTickEnabled(false);
            F->SawTickGuard = true;
            return F->Context();
        });
        Wait(TEXT("both owners converge on frozen source frame"), [F]()
        { return F->SawTickGuard && Presented(*F, 1); });
        Act(TEXT("owner A invokes actual R control row"), [F]()
        { return InvokeR(*F, 0); });
        Wait(TEXT("owner A accepted and shared generation two installed"), [F]()
        {
            if (!F->Context()) { return false; }
            const void* A = Ack(*F, F->Remote[0].Get());
            const int32 Result = Int(F->AckType, A, TEXT("Result"));
            if (Result > 1) { F->Detail = TEXT("owner A request rejected"); return false; }
            if (!Published(*F, 2)) { return false; }
            return Result == 1 && AckReady(*F, 0, 1) && Presented(*F, 2);
        });
        Act(TEXT("freeze A accepted control and source whole sample"), [F]()
        {
            if (!Presented(*F, 2)) { return false; }
            F->FrozenA = MakeUnique<FStructOnScope>(F->AckType);
            F->AckType->CopyScriptStruct(F->FrozenA->GetStructMemory(),
                Ack(*F, F->Remote[0].Get()));
            F->FrozenSequence = Field<FInt64Property>(F->SampleType, TEXT("Sequence"))
                ->GetPropertyValue_InContainer(F->Sample(0));
            return AckReady(*F, 0, 1) && F->FrozenSequence > 0;
        });
        Act(TEXT("owner B invokes independent actual R control row"), [F]()
        { return InvokeR(*F, 1); });
        Wait(TEXT("B accepted, A retained, and generation three installed"), [F]()
        {
            if (!F->Context() || !F->FrozenA ||
                !F->AckType->CompareScriptStruct(F->FrozenA->GetStructMemory(),
                    Ack(*F, F->Remote[0].Get()), PPF_None) ||
                !F->AckType->CompareScriptStruct(F->FrozenA->GetStructMemory(),
                    Ack(*F, F->Local[0].Get()), PPF_None)) { return false; }
            const int32 Result = Int(F->AckType, Ack(*F, F->Remote[1].Get()),
                TEXT("Result"));
            if (Result > 1) { F->Detail = TEXT("owner B request rejected"); return false; }
            if (!Published(*F, 3)) { return false; }
            return Result == 1 && AckReady(*F, 1, 2) && Presented(*F, 3);
        });
        Act(TEXT("freeze B control and generation-three publication sequence"), [F]()
        {
            if (!Presented(*F, 3) || !F->FrozenA ||
                !F->AckType->CompareScriptStruct(F->FrozenA->GetStructMemory(),
                    Ack(*F, F->Remote[0].Get()), PPF_None) ||
                !AckReady(*F, 1, 2)) { return false; }
            F->FrozenSequence = Field<FInt64Property>(F->SampleType, TEXT("Sequence"))
                ->GetPropertyValue_InContainer(F->Sample(0));
            return F->FrozenSequence > 0;
        });
        Wait(TEXT("later real generation-three frame reaches both owners"), [F]()
        {
            if (!F->Context() || !F->FrozenA ||
                !F->AckType->CompareScriptStruct(F->FrozenA->GetStructMemory(),
                    Ack(*F, F->Remote[0].Get()), PPF_None) ||
                !AckReady(*F, 1, 2)) { return false; }
            const void* S = F->Sample(0);
            if (!Active(*F, S, 3)) { return false; }
            const int64 Seq = Field<FInt64Property>(F->SampleType, TEXT("Sequence"))
                ->GetPropertyValue_InContainer(S);
            if (Seq <= F->FrozenSequence) { F->Pump(); return false; }
            return Presented(*F, 3);
        });
        Test.AddCommand(new FScenario(&Test, F, MoveTemp(Steps)));
        Test.AddCommand(new FRestore(F));
        Test.AddCommand(new FCk_Latent_EndPIE());
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeGym_ProductionTwoOwner,
    "Ck.SceneNode.Network.Gym.ProductionTwoOwner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeGym_ProductionTwoOwner::RunTest(const FString& Parameters)
{
    return ck_scene_node_production_two_owner::Run(*this, Parameters);
}
#endif
