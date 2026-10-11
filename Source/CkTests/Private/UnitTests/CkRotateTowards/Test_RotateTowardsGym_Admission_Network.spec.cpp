#include "CoreMinimal.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkGym_StartupSettings.h"
#include "CkRotateTowardsGym_Runtime.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkUnrealComponent/CkUnrealComponent_Fragment.h"
#include "Components/StaticMeshComponent.h"
#include "CkRotateTowards/CkRotateTowards_Fragment.h"
#include "CkTween/CkTween_Fragment.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_rotate_admission
{
    constexpr auto Map = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    auto Allocated(const FCk_Handle& H) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
    auto Live(const FCk_Handle& H) -> bool
    {
        return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
            H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }
    struct FFixture
    {
        UClass* Mode = nullptr;
        UClass* StateClass = nullptr;
        UClass* PCClass = nullptr;
        FStructProperty* Admission = nullptr;
        FStructProperty* Snapshot = nullptr;
        FStructProperty* InstalledSample = nullptr;
        FIntProperty* SnapshotGeneration = nullptr;
        FIntProperty* SnapshotSequence = nullptr;
        FBoolProperty* SnapshotReady = nullptr;
        FBoolProperty* SnapshotFailed = nullptr;
        FStrProperty* SnapshotFailureReason = nullptr;
        FBoolProperty* ReadyResult = nullptr;
        UFunction* Advance = nullptr;
        UFunction* PresentationReady = nullptr;
        TWeakObjectPtr<UWorld> Worlds[3];
        TWeakObjectPtr<AGameStateBase> ServerState;
        TWeakObjectPtr<AGameStateBase> ClientStates[2];
        bool ClientStatePinned[2] = {false, false};
        TWeakObjectPtr<APlayerController> Local[2];
        TWeakObjectPtr<APlayerController> Remote[2];
        TWeakObjectPtr<APlayerController> HostLocal;
        FDelegateHandle PreTick;
        FDelegateHandle PostTick;
        ECkGym_StartupMode Startup = ECkGym_StartupMode::Cycler;
        FString Last;
        TArray<FString> Recents;
        FCkRotateTowardsGym_MotionAdmission Dormant;
        FCkRotateTowardsGym_MotionAdmission IdleAdmission;
        FCkRotateTowardsGym_MotionAdmission StaticAdmission;
        TArray<FTransform> DormantMotion;
        TArray<FCk_Handle> Kernels;
        TArray<FCk_Handle> FrozenKnown;
        FCk_Handle UnknownChild;
        FCk_Handle SourceOwner;
        TArray<FTransform> RetiredPoses;
        bool RejectUnknown = false;
        int32 RetirementWorldTicks = 0;
        FTransform DisabledBefore;
        int32 IdleWorldTicks = 0;
        int32 DormantWorldTicks = 0;
        int32 MotionFrames = 0;
        bool SawMoved[4] = {false, false, false, false};
        bool Dedicated = false;
        bool SawPreTick = false;
        bool CapturedContext = false;
        bool WatchingDormant = false;
        bool WatchingIdle = false;
        bool DestroyedSource = false;
        bool WatchingRefusal = false;
        FString DormantFailure;
        FString ShutdownFailure;
        void Restore()
        {
            if (PreTick.IsValid())
            { FWorldDelegates::OnWorldTickStart.Remove(PreTick); PreTick.Reset(); }
            if (PostTick.IsValid())
            { FWorldDelegates::OnWorldPostActorTick.Remove(PostTick); PostTick.Reset(); }
            if (auto* S = ServerState.Get()) { S->SetActorTickEnabled(true); }
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = Startup;
            Settings->LastGymName = Last;
            Settings->RecentGymNames = Recents;
        }
        AGameStateBase* State() const
        {
            auto* W = Worlds[0].Get();
            auto* S = IsValid(W) ? W->GetGameState() : nullptr;
            return IsValid(S) && S->GetClass() == StateClass &&
                S->GetWorld() == W &&
                (!CapturedContext || S == ServerState.Get()) ? S : nullptr;
        }
        const FCkRotateTowardsGym_MotionAdmission* Frame() const
        {
            auto* S = State();
            return S && Admission
                ? Admission->ContainerPtrToValuePtr<FCkRotateTowardsGym_MotionAdmission>(S)
                : nullptr;
        }
        bool Pump() const
        {
            auto* S = State();
            return S && Advance && S->FindFunction(Advance->GetFName()) == Advance &&
                Advance->NumParms == 0 &&
                (S->ProcessEvent(Advance, nullptr), true);
        }
        bool Context() const
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || Server != Worlds[0].Get() ||
                ck::auto_test::net::Get_AllPIEWorlds().Num() != 3 ||
                ck::auto_test::net::Get_NumClientWorlds() != 2 ||
                Server->GetNetMode() !=
                    (Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
                !IsValid(Server->GetAuthGameMode()) ||
                Server->GetAuthGameMode()->GetClass() != Mode || !State())
            { return false; }
            auto* GI = Server->GetGameInstance();
            if (!IsValid(GI) || GI->GetWorld() != Server) { return false; }
            if (Dedicated)
            {
                if (!GI->GetLocalPlayers().IsEmpty() || HostLocal.IsValid())
                { return false; }
            }
            else
            {
                if (GI->GetLocalPlayers().Num() != 1) { return false; }
                auto* LP = GI->GetLocalPlayers()[0];
                auto* Host = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
                if (!IsValid(Host) || Host != HostLocal.Get() ||
                    Host->GetClass() != PCClass || !Host->IsLocalController() ||
                    !Host->HasAuthority() || Host->GetWorld() != Server ||
                    Host->GetLocalPlayer() != LP) { return false; }
            }
            for (int32 I = 0; I < 2; ++I)
            {
                auto* W = ck::auto_test::net::Get_ClientWorld(I);
                auto* LocalPC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
                auto* R = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
                if (!IsValid(W) || W != Worlds[I + 1].Get() ||
                    !IsValid(LocalPC) || LocalPC != Local[I].Get() ||
                    LocalPC->GetClass() != PCClass || !LocalPC->IsLocalController() ||
                    !IsValid(R) || R != Remote[I].Get() || R->GetClass() != PCClass ||
                    R->GetWorld() != Server) { return false; }
                if (ClientStatePinned[I] && W->GetGameState() != ClientStates[I].Get())
                { return false; }
            }
            return CapturedContext && State() == ServerState.Get() &&
                !State()->IsActorTickEnabled();
        }
        bool CaptureContext()
        {
            if (CapturedContext) { return Context(); }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || ck::auto_test::net::Get_AllPIEWorlds().Num() != 3 ||
                ck::auto_test::net::Get_NumClientWorlds() != 2 ||
                Server->GetNetMode() != (Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
                !IsValid(Server->GetAuthGameMode()) ||
                Server->GetAuthGameMode()->GetClass() != Mode) { return false; }
            auto* S = Server->GetGameState();
            if (!IsValid(S) || S->GetClass() != StateClass || S->GetWorld() != Server)
            { return false; }
            auto* GI = Server->GetGameInstance();
            if (!IsValid(GI) || GI->GetWorld() != Server) { return false; }
            APlayerController* Host = nullptr;
            if (Dedicated)
            {
                if (!GI->GetLocalPlayers().IsEmpty()) { return false; }
            }
            else
            {
                if (GI->GetLocalPlayers().Num() != 1) { return false; }
                auto* LP = GI->GetLocalPlayers()[0];
                Host = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
                if (!IsValid(Host) || Host->GetClass() != PCClass ||
                    !Host->IsLocalController() || !Host->HasAuthority() ||
                    Host->GetWorld() != Server || Host->GetLocalPlayer() != LP)
                { return false; }
            }
            UWorld* Clients[2] = {};
            APlayerController* Locals[2] = {};
            APlayerController* Remotes[2] = {};
            for (int32 I = 0; I < 2; ++I)
            {
                Clients[I] = ck::auto_test::net::Get_ClientWorld(I);
                Locals[I] = IsValid(Clients[I])
                    ? Clients[I]->GetFirstPlayerController() : nullptr;
                Remotes[I] = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
                if (!IsValid(Clients[I]) || !IsValid(Locals[I]) ||
                    Locals[I]->GetClass() != PCClass || !Locals[I]->IsLocalController() ||
                    !IsValid(Remotes[I]) ||
                    Remotes[I]->GetClass() != PCClass || Remotes[I]->GetWorld() != Server)
                { return false; }
            }
            Worlds[0] = Server; ServerState = S; HostLocal = Host;
            for (int32 I = 0; I < 2; ++I)
            { Worlds[I + 1] = Clients[I]; Local[I] = Locals[I]; Remote[I] = Remotes[I]; }
            CapturedContext = true;
            return Context();
        }
    };
    auto Schema(FFixture& F) -> bool
    {
        if (!F.Mode || !F.StateClass || !F.PCClass) { return false; }
        F.Admission = FindFProperty<FStructProperty>(F.StateClass, TEXT("MotionAdmission"));
        F.Snapshot = FindFProperty<FStructProperty>(F.StateClass, TEXT("Snapshot"));
        F.InstalledSample = FindFProperty<FStructProperty>(
            F.PCClass, TEXT("_InstalledLiveSample"));
        F.Advance = F.StateClass->FindFunctionByName(TEXT("AdvanceAuthority"));
        F.PresentationReady = F.PCClass->FindFunctionByName(
            TEXT("Get_RotateTowardsPresentationReady"));
        F.ReadyResult = F.PresentationReady
            ? FindFProperty<FBoolProperty>(F.PresentationReady, TEXT("ReturnValue"))
            : nullptr;
        if (!F.Admission || F.Admission->Struct !=
                FCkRotateTowardsGym_MotionAdmission::StaticStruct() ||
            !F.Snapshot || !F.Snapshot->HasAnyPropertyFlags(CPF_Net) ||
            !F.InstalledSample || F.InstalledSample->Struct != F.Snapshot->Struct ||
            F.InstalledSample->HasAnyPropertyFlags(CPF_Net) ||
            !F.PresentationReady ||
            !F.PresentationReady->HasAllFunctionFlags(FUNC_BlueprintPure) ||
            F.PresentationReady->NumParms != 1 || !F.ReadyResult ||
            !F.ReadyResult->HasAnyPropertyFlags(CPF_ReturnParm) ||
            !F.Advance || F.Advance->NumParms != 0 ||
            !F.Advance->HasAllFunctionFlags(FUNC_BlueprintCallable) ||
            F.Advance->HasAnyFunctionFlags(FUNC_Net)) { return false; }
        F.SnapshotGeneration = FindFProperty<FIntProperty>(
            F.Snapshot->Struct, TEXT("Generation"));
        F.SnapshotSequence = FindFProperty<FIntProperty>(
            F.Snapshot->Struct, TEXT("Sequence"));
        F.SnapshotReady = FindFProperty<FBoolProperty>(F.Snapshot->Struct, TEXT("Ready"));
        F.SnapshotFailed = FindFProperty<FBoolProperty>(F.Snapshot->Struct, TEXT("Failed"));
        F.SnapshotFailureReason = FindFProperty<FStrProperty>(
            F.Snapshot->Struct, TEXT("FailureReason"));
        const auto* Visuals = FindFProperty<FArrayProperty>(
            F.Snapshot->Struct, TEXT("Visuals"));
        const auto* VisualRow = Visuals ? CastField<FStructProperty>(Visuals->Inner) : nullptr;
        const auto* Turrets = FindFProperty<FArrayProperty>(
            F.Snapshot->Struct, TEXT("Turrets"));
        const auto* TurretRow = Turrets ? CastField<FStructProperty>(Turrets->Inner) : nullptr;
        return F.SnapshotGeneration && F.SnapshotSequence && F.SnapshotReady &&
            F.SnapshotFailed && F.SnapshotFailureReason && VisualRow && TurretRow &&
            !FindFProperty<FStructProperty>(VisualRow->Struct, TEXT("Transform")) &&
            !FindFProperty<FStructProperty>(TurretRow->Struct, TEXT("Transform")) &&
            !FindFProperty<FStructProperty>(TurretRow->Struct, TEXT("Target")) &&
            !FindFProperty<FStructProperty>(TurretRow->Struct, TEXT("Rest"));
    }
    auto ExactPresentation(FFixture& F, APlayerController* PC,
                           AGameStateBase* GS) -> bool
    {
        if (!IsValid(PC) || PC->GetClass() != F.PCClass ||
            !PC->IsLocalController() || !IsValid(GS) ||
            GS->GetClass() != F.StateClass || !IsValid(GS->GetWorld()) ||
            PC->GetWorld() != GS->GetWorld() ||
            GS->GetWorld()->GetGameState() != GS ||
            !F.InstalledSample || !F.Snapshot || !F.PresentationReady ||
            !F.ReadyResult || !F.SnapshotGeneration || !F.SnapshotSequence ||
            PC->FindFunction(F.PresentationReady->GetFName()) !=
                F.PresentationReady) { return false; }
        const void* Current = F.Snapshot->ContainerPtrToValuePtr<void>(GS);
        const void* Installed = F.InstalledSample->ContainerPtrToValuePtr<void>(PC);
        if (!Current || !Installed ||
            F.SnapshotGeneration->GetPropertyValue_InContainer(Current) != 1 ||
            F.SnapshotSequence->GetPropertyValue_InContainer(Current) <= 0 ||
            !F.Snapshot->Struct->CompareScriptStruct(Current, Installed, PPF_None))
        { return false; }
        FStructOnScope Args(F.PresentationReady);
        PC->ProcessEvent(F.PresentationReady, Args.GetStructMemory());
        return F.ReadyResult->GetPropertyValue_InContainer(Args.GetStructMemory());
    }
    auto AllOwnerPresentationsReady(FFixture& F) -> bool
    {
        if (!F.Context()) { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            auto* W = F.Worlds[I + 1].Get();
            auto* GS = IsValid(W) ? W->GetGameState() : nullptr;
            if (!IsValid(GS) || GS->GetClass() != F.StateClass ||
                GS->GetWorld() != W) { return false; }
            if (!F.ClientStatePinned[I])
            { F.ClientStates[I] = GS; F.ClientStatePinned[I] = true; }
            if (GS != F.ClientStates[I].Get() ||
                !ExactPresentation(F, F.Local[I].Get(), GS)) { return false; }
        }
        if (!F.Dedicated && !ExactPresentation(
                F, F.HostLocal.Get(), F.ServerState.Get())) { return false; }
        return F.Context();
    }
    auto SnapshotData(const FFixture& F, AGameStateBase* GS) -> const void*
    {
        return IsValid(GS) && GS->GetClass() == F.StateClass && F.Snapshot
            ? F.Snapshot->ContainerPtrToValuePtr<void>(GS) : nullptr;
    }
    auto DescribePresentation(FFixture& F, const TCHAR* Name, UWorld* W,
                              APlayerController* PC) -> FString
    {
        auto* GS = IsValid(W) ? W->GetGameState() : nullptr;
        const void* Current = SnapshotData(F, GS);
        if (!Current || !F.SnapshotReady || !F.SnapshotFailed ||
            !F.SnapshotFailureReason || !F.SnapshotGeneration || !F.SnapshotSequence)
        { return FString::Printf(TEXT(" %s{world/state/schema unavailable}"), Name); }
        FString Out = FString::Printf(
            TEXT(" %s{state=%d/%d/%d/%d reason=%s pinned=%d"), Name,
            F.SnapshotReady->GetPropertyValue_InContainer(Current),
            F.SnapshotFailed->GetPropertyValue_InContainer(Current),
            F.SnapshotGeneration->GetPropertyValue_InContainer(Current),
            F.SnapshotSequence->GetPropertyValue_InContainer(Current),
            *F.SnapshotFailureReason->GetPropertyValue_InContainer(Current),
            GS == F.ServerState.Get() || GS == F.ClientStates[0].Get() ||
                GS == F.ClientStates[1].Get());
        const bool PCValid = IsValid(PC) && PC->GetClass() == F.PCClass &&
            PC->GetWorld() == W && PC->IsLocalController();
        const void* Installed = PCValid && F.InstalledSample
            ? F.InstalledSample->ContainerPtrToValuePtr<void>(PC) : nullptr;
        if (!Installed) { return Out + TEXT(" pc/installed=unavailable}"); }
        const bool CanRead = F.PresentationReady && F.ReadyResult &&
            PC->FindFunction(F.PresentationReady->GetFName()) == F.PresentationReady;
        if (!CanRead) { return Out + TEXT(" readyGetter=unavailable}"); }
        FStructOnScope Args(F.PresentationReady);
        PC->ProcessEvent(F.PresentationReady, Args.GetStructMemory());
        Out += FString::Printf(
            TEXT(" installed=%d/%d/%d/%d equal=%d getter=%d}"),
            F.SnapshotReady->GetPropertyValue_InContainer(Installed),
            F.SnapshotFailed->GetPropertyValue_InContainer(Installed),
            F.SnapshotGeneration->GetPropertyValue_InContainer(Installed),
            F.SnapshotSequence->GetPropertyValue_InContainer(Installed),
            F.Snapshot->Struct->CompareScriptStruct(Current, Installed, PPF_None),
            F.ReadyResult->GetPropertyValue_InContainer(Args.GetStructMemory()));
        return Out;
    }
    auto PublicationDiagnostic(FFixture& F) -> FString
    {
        FString Out = DescribePresentation(F, TEXT("authority"), F.Worlds[0].Get(),
            F.Dedicated ? nullptr : F.HostLocal.Get());
        for (int32 I = 0; I < 2; ++I)
        {
            Out += DescribePresentation(F, I == 0 ? TEXT("client0") : TEXT("client1"),
                F.Worlds[I + 1].Get(), F.Local[I].Get());
        }
        return Out;
    }
    auto StaticReady(const FFixture& F) -> bool
    {
        const auto* A = F.Frame();
        if (!A || A->Generation != 1 || A->Activated || A->Closed ||
            A->Roots.Num() != 3 || A->Motion.Num() != 5 ||
            A->Turrets.Num() != 7 || A->Tweens.Num() != 4 ||
            A->CubeNodes.Num() != 11 || A->CubeComponents.Num() != 11 ||
            A->ComponentAdded.Num() != 11 || !Live(A->Owner))
        { return false; }
        TSet<FCk_Handle> Ids;
        for (const auto& H : A->Roots) { if (!Live(H)) { return false; } Ids.Add(H); }
        for (const auto& H : A->Motion) { if (!Live(H)) { return false; } Ids.Add(H); }
        for (const auto& H : A->Turrets) { if (!Live(H)) { return false; } Ids.Add(H); }
        for (int32 Slot = 0; Slot < 11; ++Slot)
        {
            const auto& H = A->CubeNodes[Slot];
            const auto& C = A->CubeComponents[Slot];
            auto* Mesh = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCubeMesh(
                F.Worlds[0].Get(), 1, Slot);
            if (!Live(H) || !Live(C) ||
                H != UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(
                    F.Worlds[0].Get(), 1, Slot) ||
                !IsValid(Mesh) ||
                Mesh != C.Get<ck::FFragment_UnrealComponent>().Get_Component().Get())
            { return false; }
            Ids.Add(H); Ids.Add(C);
        }
        for (int32 Count : A->ComponentAdded)
        { if (Count != 1) { return false; } }
        for (const auto& H : A->Tweens)
        { if (FCk_Handle(H) != FCk_Handle()) { return false; } }
        return Ids.Num() == 37;
    }
    auto StaticDiagnostic(const FFixture& F) -> FString
    {
        auto* S = F.State();
        const auto* A = F.Frame();
        const bool HasContext = F.Context();
        bool SnapshotReady = false;
        bool SnapshotFailed = false;
        FString FailureReason;
        if (S && F.Snapshot && F.Snapshot->Struct)
        {
            const void* D = F.Snapshot->ContainerPtrToValuePtr<void>(S);
            if (D)
            {
                if (auto* P = FindFProperty<FBoolProperty>(F.Snapshot->Struct, TEXT("Ready")))
                { SnapshotReady = P->GetPropertyValue_InContainer(D); }
                if (auto* P = FindFProperty<FBoolProperty>(F.Snapshot->Struct, TEXT("Failed")))
                { SnapshotFailed = P->GetPropertyValue_InContainer(D); }
                if (auto* P = FindFProperty<FStrProperty>(F.Snapshot->Struct, TEXT("FailureReason")))
                { FailureReason = P->GetPropertyValue_InContainer(D); }
            }
        }
        if (!A)
        {
            return FString::Printf(TEXT(" [context=%d state=%d tick=%d admission=missing snapshotReady=%d snapshotFailed=%d reason=%s]"),
                HasContext, S != nullptr, S && S->IsActorTickEnabled(),
                SnapshotReady, SnapshotFailed, *FailureReason);
        }
        TSet<FCk_Handle> Ids;
        int32 LiveCount = 0;
        for (const auto& H : A->Roots) { if (Live(H)) { ++LiveCount; } Ids.Add(H); }
        for (const auto& H : A->Motion) { if (Live(H)) { ++LiveCount; } Ids.Add(H); }
        for (const auto& H : A->Turrets) { if (Live(H)) { ++LiveCount; } Ids.Add(H); }
        for (const auto& H : A->CubeNodes) { if (Live(H)) { ++LiveCount; } Ids.Add(H); }
        for (const auto& H : A->CubeComponents) { if (Live(H)) { ++LiveCount; } Ids.Add(H); }
        int32 CallbackOnes = 0;
        for (int32 Count : A->ComponentAdded) { if (Count == 1) { ++CallbackOnes; } }
        int32 NonDefaultTweens = 0;
        for (const auto& H : A->Tweens)
        { if (FCk_Handle(H) != FCk_Handle()) { ++NonDefaultTweens; } }
        return FString::Printf(TEXT(" [context=%d tick=%d snapshotReady=%d snapshotFailed=%d reason=%s generation=%d activated=%d closed=%d ownerLive=%d roots=%d motion=%d turrets=%d tweens=%d nodes=%d components=%d callbacks=%d callbackOnes=%d nonDefaultTweens=%d liveEntries=%d uniqueEntries=%d]"),
            HasContext, S && S->IsActorTickEnabled(), SnapshotReady, SnapshotFailed,
            *FailureReason, A->Generation, A->Activated, A->Closed, Live(A->Owner),
            A->Roots.Num(), A->Motion.Num(), A->Turrets.Num(), A->Tweens.Num(),
            A->CubeNodes.Num(), A->CubeComponents.Num(), A->ComponentAdded.Num(),
            CallbackOnes, NonDefaultTweens, LiveCount, Ids.Num());
    }
    auto Poses(const FCkRotateTowardsGym_MotionAdmission& A,
               TArray<FTransform>& Out) -> bool
    {
        Out.Reset();
        for (const auto& H : A.Motion)
        {
            if (!Live(H) || !H.Has<ck::FFragment_Transform>()) { return false; }
            Out.Add(H.Get<ck::FFragment_Transform>().Get_Transform());
        }
        for (const auto& H : A.Turrets)
        {
            if (!Live(H) || !H.Has<ck::FFragment_Transform>()) { return false; }
            Out.Add(H.Get<ck::FFragment_Transform>().Get_Transform());
        }
        return Out.Num() == 12;
    }
    auto SamePoses(const FCkRotateTowardsGym_MotionAdmission& A,
                   const TArray<FTransform>& Before) -> bool
    {
        TArray<FTransform> After;
        if (!Poses(A, After) || After.Num() != Before.Num()) { return false; }
        for (int32 I = 0; I < After.Num(); ++I)
        { if (!After[I].Equals(Before[I], 0.0)) { return false; } }
        return true;
    }
    auto CopyRejects(const FFixture& F) -> bool
    {
        auto* S = F.State();
        const auto* Stored = F.Frame();
        if (!F.Context() || !IsValid(F.Worlds[1].Get()) || !S || !Stored ||
            !UCk_RotateTowardsGym_Runtime_UE::TryValidate_Cohort(S, *Stored))
        { return false; }
        const auto Before = *Stored;
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*S->GetWorld());
        const auto ForeignRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*F.Worlds[1].Get());
        if (UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveAllocatedMotionEntity({}, Root) ||
            UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveAllocatedMotionEntity(Root, Root) ||
            UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveAllocatedMotionEntity(Before.Owner, ForeignRoot) ||
            !UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveAllocatedMotionEntity(Before.Owner, Root))
        { return false; }
        for (const auto& H : Before.Turrets)
        { if (UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveAllocatedMotionEntity(H, Root)) { return false; } }
        for (const auto& H : Before.Tweens)
        { if (UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveAllocatedMotionEntity(H, Root)) { return false; } }
        TArray<FTransform> BeforePoses;
        if (!Poses(Before, BeforePoses)) { return false; }
        auto Candidate = Before;
        Candidate = {};
        if (UCk_RotateTowardsGym_Runtime_UE::TryValidate_Cohort(S, Candidate)) { return false; }
        Candidate = Before; Candidate.Roots[0] = Candidate.Roots[1];
        if (UCk_RotateTowardsGym_Runtime_UE::TryValidate_Cohort(S, Candidate)) { return false; }
        Candidate = Before; ++Candidate.Generation;
        if (UCk_RotateTowardsGym_Runtime_UE::TryValidate_Cohort(S, Candidate)) { return false; }
        Candidate = Before;
        auto* Client = F.Worlds[1].Get();
        if (!IsValid(Client)) { return false; }
        Candidate.Owner = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*Client);
        if (UCk_RotateTowardsGym_Runtime_UE::TryValidate_Cohort(S, Candidate)) { return false; }
        Candidate = Before; Candidate.Motion[0] = Candidate.Roots[0];
        if (UCk_RotateTowardsGym_Runtime_UE::TryValidate_Cohort(S, Candidate)) { return false; }
        return FCkRotateTowardsGym_MotionAdmission::StaticStruct()->CompareScriptStruct(
                   Stored, &Before, PPF_None) &&
            SamePoses(*Stored, BeforePoses) &&
            !UCk_RotateTowardsGym_Runtime_UE::TryClaim_NewOwner(
                S, Stored->Owner, Stored->Generation) &&
            FCkRotateTowardsGym_MotionAdmission::StaticStruct()->CompareScriptStruct(
                Stored, &Before, PPF_None) && SamePoses(*Stored, BeforePoses);
    }
    auto CheckQueuedActiveAdmission(FFixture& F) -> bool
    {
        using State = ECkRotateTowardsGym_ActiveCohortState;
        auto* S = F.State();
        const auto* A = F.Frame();
        auto* DefaultSource = F.StateClass
            ? Cast<AGameStateBase>(F.StateClass->GetDefaultObject(false)) : nullptr;
        if (!F.Context() || !S || !A || !DefaultSource || !F.Snapshot ||
            !F.Snapshot->Struct || !F.SnapshotFailed || !F.SnapshotSequence ||
            UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortState(nullptr) != State::Rejected ||
            UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortState(DefaultSource) != State::Rejected ||
            UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortState(S) != State::Ready ||
            A->CubeNodes.Num() != 11 || A->CubeComponents.Num() != 11)
        { return false; }
        const FCk_Handle Node = A->CubeNodes[0];
        const auto Component = A->CubeComponents[0];
        if (!Live(Node) || !Node.Has<ck::FFragment_SceneNode>() ||
            !Node.Has<ck::FFragment_Transform>() ||
            (Node.Has<ck::FFragment_SceneNode_Requests>() &&
                !Node.Get<ck::FFragment_SceneNode_Requests>().Get_Requests().IsEmpty()) ||
            !Live(Component) || !Component.Has<ck::FFragment_UnrealComponent>())
        { return false; }
        auto* Mesh = Cast<UStaticMeshComponent>(
            Component.Get<ck::FFragment_UnrealComponent>().Get_Component().Get());
        if (!IsValid(Mesh) || Mesh->GetWorld() != S->GetWorld() ||
            !Mesh->IsRegistered() || !Mesh->IsVisible() || Mesh->bHiddenInGame)
        { return false; }
        // This is the real public request path. It leaves a request for the
        // SceneNode processor even though the desired offset equals the current one.
        auto SceneNode = UCk_Utils_SceneNode_UE::Cast(Node);
        if (!Live(SceneNode)) { return false; }
        const auto Offset = UCk_Utils_SceneNode_UE::Get_Offset(SceneNode);
        if (UCk_Utils_SceneNode_UE::Request_UpdateOffset(SceneNode,
                FCk_Request_SceneNode_UpdateRelativeTransform{Offset}, {}) != SceneNode ||
            !Node.Has<ck::FFragment_SceneNode_Requests>() ||
            Node.Get<ck::FFragment_SceneNode_Requests>().Get_Requests().Num() != 1 ||
            UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortState(S) != State::Pending)
        { return false; }
        const auto BeforeAdmission = *A;
        FStructOnScope BeforeSnapshot(F.Snapshot->Struct);
        F.Snapshot->Struct->CopyScriptStruct(BeforeSnapshot.GetStructMemory(),
            F.Snapshot->ContainerPtrToValuePtr<void>(S));
        // A real registered piece has the wrong style while the queued request
        // still exists. Invalidity must beat Pending; the query must not repair it.
        Mesh->SetVisibility(false);
        const bool Rejected =
            UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortState(S) == State::Rejected &&
            !Mesh->IsVisible() &&
            Node.Get<ck::FFragment_SceneNode_Requests>().Get_Requests().Num() == 1 &&
            F.Snapshot->Struct->CompareScriptStruct(
                F.Snapshot->ContainerPtrToValuePtr<void>(S),
                BeforeSnapshot.GetStructMemory(), PPF_None);
        Mesh->SetVisibility(true);
        if (!Rejected || !Mesh->IsVisible() ||
            UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortState(S) != State::Pending ||
            !FCkRotateTowardsGym_MotionAdmission::StaticStruct()->CompareScriptStruct(
                A, &BeforeAdmission, PPF_None))
        { return false; }
        // Source publication sees Pending and must leave the entire actual
        // replicated snapshot untouched. ECS motion resumes after this step.
        if (!F.Pump()) { return false; }
        const void* After = F.Snapshot->ContainerPtrToValuePtr<void>(S);
        return After &&
            UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortState(S) == State::Pending &&
            !F.SnapshotFailed->GetPropertyValue_InContainer(After) &&
            F.SnapshotSequence->GetPropertyValue_InContainer(After) ==
                F.SnapshotSequence->GetPropertyValue_InContainer(BeforeSnapshot.GetStructMemory()) &&
            F.Snapshot->Struct->CompareScriptStruct(
                After, BeforeSnapshot.GetStructMemory(), PPF_None) &&
            FCkRotateTowardsGym_MotionAdmission::StaticStruct()->CompareScriptStruct(
                A, &BeforeAdmission, PPF_None);
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
        FScenario(FAutomationTestBase* InTest, TSharedRef<FFixture> InF,
                  TArray<FStep>&& InSteps)
            : Test(InTest), F(InF), Steps(MoveTemp(InSteps)) {}
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed() || Index >= Steps.Num())
            { return true; }
            if (!F->DormantFailure.IsEmpty())
            {
                Test->AddError(F->DormantFailure);
                ck::auto_test::net::FailSession(F->DormantFailure);
                return true;
            }
            if (Began < 0) { Began = FPlatformTime::Seconds(); }
            auto& Step = Steps[Index];
            if (Step.Check()) { ++Index; Began = -1; return Index >= Steps.Num(); }
            if (Step.Wait && FPlatformTime::Seconds() - Began < 45) { return false; }
            FString Why = FString::Printf(TEXT("RotateTowards admission phase %d failed: %s"),
                Index, *Step.Name);
            if (Index == 3) { Why += StaticDiagnostic(*F); }
            if (Index == 11 && F->State())
            { Why += FString::Printf(TEXT(" active=%s"),
                  *UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortDiagnostic(F->State())); }
            if (Index == 12) { Why += PublicationDiagnostic(*F); }
            if (Index == 14 && !F->ShutdownFailure.IsEmpty())
            { Why += FString::Printf(TEXT(" stage=%s"), *F->ShutdownFailure); }
            Test->AddError(Why);
            ck::auto_test::net::FailSession(Why);
            return true;
        }
    };
    class FRestore final : public IAutomationLatentCommand
    {
        TSharedRef<FFixture> F;
    public:
        explicit FRestore(TSharedRef<FFixture> InF) : F(InF) {}
        bool Update() override { F->Restore(); return true; }
    };
    auto Run(FAutomationTestBase& Test, const FString& Topology, bool RejectUnknown = false) -> bool
    {
        auto F = MakeShared<FFixture>();
        F->RejectUnknown = RejectUnknown;
        if (!ck::auto_test::net::TryGet_DedicatedServer(Topology, F->Dedicated))
        { return false; }
        F->Mode = FSoftClassPath(TEXT("/Script/Angelscript.Ck_RotateTowardsGym_GameMode"))
            .TryLoadClass<AGameModeBase>();
        F->StateClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_RotateTowardsGym_GameState"))
            .TryLoadClass<AGameStateBase>();
        F->PCClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_RotateTowardsGym_PlayerController"))
            .TryLoadClass<APlayerController>();
        if (!Test.TestTrue(TEXT("actual Rotate classes and reflected source schema"),
                           Schema(*F))) { return false; }
        const auto* Settings = GetDefault<UCkGym_StartupSettings>();
        F->Startup = Settings->StartupMode;
        F->Last = Settings->LastGymName;
        F->Recents = Settings->RecentGymNames;
        GetMutableDefault<UCkGym_StartupSettings>()->StartupMode =
            ECkGym_StartupMode::Cycler;
        // Installed before PIE: the first authority Actor Tick cannot advance startup.
        F->PreTick = FWorldDelegates::OnWorldTickStart.AddLambda(
            [F](UWorld* W, ELevelTick, float)
            {
                if (!IsValid(W) || W->WorldType != EWorldType::PIE ||
                    !IsValid(W->GetGameState()) ||
                    W->GetGameState()->GetClass() != F->StateClass ||
                    !W->GetGameState()->HasAuthority()) { return; }
                W->GetGameState()->SetActorTickEnabled(false);
                F->SawPreTick = true;
            });
        F->PostTick = FWorldDelegates::OnWorldPostActorTick.AddLambda(
            [F](UWorld* W, ELevelTick, float)
            {
                if (W != F->Worlds[0].Get() || !F->DormantFailure.IsEmpty()) { return; }
                if (F->RejectUnknown && F->WatchingRefusal)
                {
                    bool Intact = Live(F->UnknownChild) && SamePoses(F->Dormant, F->RetiredPoses);
                    for (const auto& H : F->FrozenKnown) { Intact &= Live(H); }
                    if (!Intact)
                    { F->DormantFailure = TEXT("RotateTowards rejected unknown closure was destroyed or its stopped producers moved before actor loss"); return; }
                    ++F->RetirementWorldTicks;
                    return;
                }
                const auto* A = F->Frame();
                if (F->WatchingIdle)
                {
                    const void* Data = F->State() && F->Snapshot
                        ? F->Snapshot->ContainerPtrToValuePtr<void>(F->State()) : nullptr;
                    if (!F->Context() || !A || !Data || !F->SnapshotGeneration ||
                        F->SnapshotGeneration->GetPropertyValue_InContainer(Data) != 0 ||
                        !FCkRotateTowardsGym_MotionAdmission::StaticStruct()->CompareScriptStruct(
                            A, &F->IdleAdmission, PPF_None))
                    { F->DormantFailure = TEXT("RotateTowards authority advanced independently while Actor Tick was held"); return; }
                    ++F->IdleWorldTicks;
                }
                if (!F->WatchingDormant) { return; }
                if (!F->Context() || !A ||
                    !FCkRotateTowardsGym_MotionAdmission::StaticStruct()->CompareScriptStruct(
                        A, &F->StaticAdmission, PPF_None) ||
                    !SamePoses(*A, F->DormantMotion))
                {
                    F->DormantFailure = TEXT("RotateTowards dormant fixture moved or lost its exact source on a world post-actor tick");
                    return;
                }
                ++F->DormantWorldTicks;
            });
        Test.AddCommand(new FCk_Latent_StartPIEMultiClient(3, Map, Topology, F->Mode));
        Test.AddCommand(new FCk_Latent_WaitForPIEReady(3, 60));
        TArray<FStep> Steps;
        auto Act = [&](FString N, TFunction<bool()> C)
        { Steps.Add({MoveTemp(N), MoveTemp(C), false}); };
        auto Wait = [&](FString N, TFunction<bool()> C)
        { Steps.Add({MoveTemp(N), MoveTemp(C), true}); };
        Wait(TEXT("three real worlds, two real owners, authority tick held"), [F]()
        {
            return F->SawPreTick && F->CaptureContext() &&
                !F->State()->IsActorTickEnabled();
        });
        Act(TEXT("capture authority admission before any explicit source step"), [F]()
        {
            const auto* A = F->Frame();
            if (!F->Context() || !A || !F->Snapshot || !F->SnapshotGeneration ||
                A->Generation != 0 || A->Owner != FCk_Handle()) { return false; }
            const void* Data = F->Snapshot->ContainerPtrToValuePtr<void>(F->State());
            if (F->SnapshotGeneration->GetPropertyValue_InContainer(Data) != 0) { return false; }
            F->IdleAdmission = *A; F->IdleWorldTicks = 0; F->WatchingIdle = true;
            return true;
        });
        Wait(TEXT("six real world ticks cannot advance authority independently"), [F]()
        {
            if (!F->DormantFailure.IsEmpty() || F->IdleWorldTicks < 6) { return false; }
            F->WatchingIdle = false;
            return F->Context();
        });
        Wait(TEXT("real station and static 37-entity fixture with 11 callbacks"), [F]()
        {
            if (!F->Context()) { return false; }
            // Catch the completed callbacks before a further source step can create Tweens.
            return StaticReady(*F) || (F->Pump() && StaticReady(*F));
        });
        Act(TEXT("capture static poses before any Tween exists"), [F]()
        {
            const auto* A = F->Frame();
            if (!A || !StaticReady(*F) || !Poses(*A, F->DormantMotion))
            { return false; }
            F->StaticAdmission = *A;
            F->DormantWorldTicks = 0;
            F->WatchingDormant = true;
            return true;
        });
        Wait(TEXT("six consecutive world post-actor ticks cannot move dormant fixture"), [F]()
        {
            if (!F->DormantFailure.IsEmpty()) { return false; }
            if (F->DormantWorldTicks < 6) { return false; }
            F->WatchingDormant = false;
            return F->Context() && F->Frame() &&
                SamePoses(*F->Frame(), F->DormantMotion);
        });
        Act(TEXT("enqueue four real Tween creates and Pause requests"), [F]()
        { return F->Pump(); });
        Wait(TEXT("four genuine paused queue-empty Tweens"), [F]()
        {
            auto* S = F->State(); const auto* A = F->Frame();
            return S && A && UCk_RotateTowardsGym_Runtime_UE::TryValidate_Cohort(S, *A);
        });
        Act(TEXT("copied malformed admissions and repeated owner claim reject without mutation"),
            [F]() { return CopyRejects(*F); });
        Act(TEXT("single source step consumes four receipts and atomically activates"), [F]()
        {
            if (!F->Pump()) { return false; }
            auto* S = F->State(); const auto* A = F->Frame();
            if (!S || !A || !A->Activated || !UCk_RotateTowardsGym_Runtime_UE::
                    TryValidate_ActiveCohort(S)) { return false; }
            for (int32 I = 0; I < 7; ++I)
            {
                if (!Live(A->Turrets[I]) ||
                    A->Turrets[I].Has<ck::FTag_RotateTowards_Disabled>() != (I == 6))
                { return false; }
            }
            for (const auto& H : A->Tweens)
            {
                if (!Live(H) || !H.Has<ck::FTag_Tween_Playing>() ||
                    H.Get<ck::FFragment_Tween>().Get_State() != ECk_TweenState::Playing)
                { return false; }
            }
            F->Dormant = *A;
            F->SourceOwner = A->Owner;
            F->DisabledBefore =
                A->Turrets[6].Get<ck::FFragment_Transform>().Get_Transform();
            for (const auto& H : A->Turrets) { F->Kernels.Add(H); }
            for (const auto& H : A->Tweens) { F->Kernels.Add(H); }
            return F->Kernels.Num() == 11 && CheckQueuedActiveAdmission(*F);
        });
        Act(TEXT("repeat activation rejects with actual source unchanged"), [F]()
        {
            auto* S = F->State(); const auto* A = F->Frame();
            if (!S || !A) { return false; }
            const auto Before = *A;
            return !UCk_RotateTowardsGym_Runtime_UE::TryActivate_Cohort(S) &&
                FCkRotateTowardsGym_MotionAdmission::StaticStruct()->CompareScriptStruct(
                    A, &Before, PPF_None);
        });
        Wait(TEXT("genuine ECS motion and fixed seventh disabled turret"), [F]()
        {
            const auto* A = F->Frame();
            if (!A || !F->Context() || !F->State() ||
                !UCk_RotateTowardsGym_Runtime_UE::TryValidate_ActiveCohort(F->State()) ||
                !A->Turrets[6].Get<ck::FFragment_Transform>().Get_Transform()
                    .Equals(F->DisabledBefore, 0.0)) { return false; }
            TArray<FTransform> Now;
            if (!Poses(*A, Now)) { return false; }
            constexpr int32 MovingSlots[4] = {0, 1, 3, 4};
            for (int32 I = 0; I < 4; ++I)
            {
                F->SawMoved[I] |= !Now[MovingSlots[I]].Equals(
                    F->DormantMotion[MovingSlots[I]], 0.0);
            }
            return F->SawMoved[0] && F->SawMoved[1] &&
                F->SawMoved[2] && F->SawMoved[3] && ++F->MotionFrames >= 3;
        });
        Wait(TEXT("both actual client presentations and listen host install current replicated sample"), [F]()
        {
            if (!F->Context()) { return false; }
            const void* Data = SnapshotData(*F, F->State());
            if (!Data || F->SnapshotFailed->GetPropertyValue_InContainer(Data))
            { return false; }
            if (!F->SnapshotReady->GetPropertyValue_InContainer(Data))
            {
                // Use the actual authority step only until its first complete
                // publication. Once Ready, hold the sequence fixed for Iris and
                // both owner projections to converge on this exact sample.
                if (!F->Pump()) { return false; }
                return false;
            }
            return AllOwnerPresentationsReady(*F);
        });
        if (RejectUnknown)
        {
            Act(TEXT("real unknown owner child rejects the immutable 42-entity closure"), [F]()
            {
                auto* S = F->State(); const auto* A = F->Frame();
                if (!F->Context() || !S || !A ||
                    !UCk_RotateTowardsGym_Runtime_UE::TryValidate_ActiveCohort(S)) { return false; }
                F->FrozenKnown.Add(A->Owner);
                for (const auto& H : A->Roots) { F->FrozenKnown.Add(H); }
                for (const auto& H : A->Motion) { F->FrozenKnown.Add(H); }
                for (const auto& H : A->Turrets) { F->FrozenKnown.Add(H); }
                for (const auto& H : A->Tweens) { F->FrozenKnown.Add(H); }
                for (const auto& H : A->CubeNodes) { F->FrozenKnown.Add(H); }
                for (const auto& H : A->CubeComponents) { F->FrozenKnown.Add(H); }
                const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*S->GetWorld());
                TSet<FCk_Handle> Distinct;
                for (const auto& H : F->FrozenKnown) { if (!Live(H)) { return false; } Distinct.Add(H); }
                if (F->FrozenKnown.Num() != 42 || Distinct.Num() != 42 ||
                    !UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
                        F->FrozenKnown, F->FrozenKnown, WorldRoot)) { return false; }
                F->UnknownChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(A->Owner);
                TArray<FCk_Handle> Children;
                if (!Live(F->UnknownChild) || F->FrozenKnown.Contains(F->UnknownChild) ||
                    !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(F->UnknownChild) ||
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(F->UnknownChild) != A->Owner ||
                    !UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                        A->Owner, WorldRoot, Children) || !Children.Contains(F->UnknownChild) ||
                    UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
                        F->FrozenKnown, F->FrozenKnown, WorldRoot) ||
                    UCk_RotateTowardsGym_Runtime_UE::TryValidate_ActiveCohort(S)) { return false; }
                return true;
            });
        }
        Act(RejectUnknown ? TEXT("unknown child triggers source refusal and synchronous producer quiescence") :
            TEXT("normal EndPlay synchronously stops every retained allocated producer"), [F]()
        {
            auto Reject = [F](FString Stage) -> bool
            { F->ShutdownFailure = MoveTemp(Stage); return false; };
            auto* S = F->State(); auto* W = F->Worlds[0].Get();
            const auto* A = F->Frame();
            if (!S || !IsValid(W) || !W->HasBegunPlay() || !A || !F->Context())
            { return Reject(TEXT("source-world-frame-context")); }
            if (!A->Activated || A->Closed)
            { return Reject(TEXT("source-activation-flags")); }
            if (F->Kernels.Num() != 11 || A->Turrets.Num() != 7 || A->Tweens.Num() != 4)
            { return Reject(TEXT("kernel-schema")); }
            if (!F->RejectUnknown && !UCk_RotateTowardsGym_Runtime_UE::TryValidate_ActiveCohort(S))
            { return Reject(TEXT("normal-active-cohort")); }
            if (F->RejectUnknown &&
                (F->FrozenKnown.Num() != 42 || !Live(F->UnknownChild) ||
                 UCk_RotateTowardsGym_Runtime_UE::TryValidate_ActiveCohort(S)))
            { return Reject(TEXT("unknown-closure-prerequisite")); }
            TSet<FCk_Handle> Distinct;
            for (int32 I = 0; I < 11; ++I)
            {
                const auto& H = F->Kernels[I];
                if (!Live(H) ||
                    (I < 7 ? H != FCk_Handle(A->Turrets[I])
                           : H != FCk_Handle(A->Tweens[I - 7])))
                { return Reject(FString::Printf(TEXT("kernel-identity slot=%d"), I)); }
                Distinct.Add(H);
            }
            if (Distinct.Num() != 11) { return Reject(TEXT("kernel-duplicate")); }
            // Freeze the actual poses at the refusal or EndPlay boundary.
            if (F->RejectUnknown && !Poses(*A, F->RetiredPoses))
            { return Reject(TEXT("pre-refusal-pose-capture")); }
            if (F->RejectUnknown)
            {
                if (!F->Pump()) { return Reject(TEXT("source-refusal-pump")); }
                const void* Sample = SnapshotData(*F, S);
                auto* Unproven = S->FindFunction(TEXT("Get_IsRetirementUnproven"));
                if (!Sample || !F->SnapshotFailed->GetPropertyValue_InContainer(Sample) ||
                    F->SnapshotReady->GetPropertyValue_InContainer(Sample) ||
                    F->SnapshotGeneration->GetPropertyValue_InContainer(Sample) != 1 ||
                    !Unproven || !Unproven->HasAllFunctionFlags(FUNC_BlueprintPure))
                { return Reject(TEXT("unknown-closure-did-not-fail-closed")); }
                FStructOnScope Args(Unproven);
                S->ProcessEvent(Unproven, Args.GetStructMemory());
                auto* Result = FindFProperty<FBoolProperty>(Unproven, TEXT("ReturnValue"));
                if (!Result || !Result->GetPropertyValue_InContainer(Args.GetStructMemory()) ||
                    !Live(F->UnknownChild))
                { return Reject(TEXT("unknown-closure-not-unproven")); }
                F->WatchingRefusal = true;
            }
            else
            {
                if (!S->Destroy()) { return Reject(TEXT("source-destroy-rejected")); }
                F->DestroyedSource = true;
            }
            const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
            if (Allocated(F->SourceOwner))
            {
                if (!UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveAllocatedMotionEntity(F->SourceOwner, Root) ||
                    UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveMotionEntity(F->SourceOwner, Root) != F->RejectUnknown)
                { return Reject(TEXT("source-owner-passive-or-live-shape")); }
            }
            // EndPlay is synchronous. DestroyInitiate producers still enter the
            // normal CK_IGNORE_PENDING_KILL processor queries until deallocation.
            for (int32 I = 0; I < 11; ++I)
            {
                const auto& H = F->Kernels[I];
                if (!Allocated(H)) { continue; }
                if (!ck::IsValid(H, ck::IsValid_Policy_IncludePendingKill{}) ||
                    UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveAllocatedMotionEntity(H, Root))
                { return Reject(FString::Printf(TEXT("kernel-passive slot=%d"), I)); }
                if (I < 7)
                {
                    if (!H.Has<ck::FTag_RotateTowards_Disabled>())
                    { return Reject(FString::Printf(TEXT("turret-not-disabled slot=%d"), I)); }
                }
                else if (!H.Has<ck::FFragment_Tween>() ||
                         !H.Has<ck::FTag_Tween_Paused>() ||
                         H.Has<ck::FTag_Tween_Playing>() ||
                         H.Get<ck::FFragment_Tween,
                             ck::IsValid_Policy_IncludePendingKill>().Get_State() !=
                             ECk_TweenState::Paused)
                { return Reject(FString::Printf(TEXT("tween-not-paused slot=%d"), I)); }
            }
            if (F->RejectUnknown)
            {
                if (!Live(F->UnknownChild)) { return Reject(TEXT("unknown-child-not-live")); }
                for (int32 I = 0; I < F->FrozenKnown.Num(); ++I)
                { if (!Live(F->FrozenKnown[I]))
                  { return Reject(FString::Printf(TEXT("known-not-live slot=%d"), I)); } }
                if (!SamePoses(F->Dormant, F->RetiredPoses))
                { return Reject(TEXT("post-refusal-pose-moved")); }
            }
            return true;
        });
        Wait(RejectUnknown ? TEXT("unknown closure stays intact and inert for six world ticks") :
             TEXT("all eleven retained producer identities actually deallocated"), [F]()
        {
            auto* W = F->Worlds[0].Get();
            if ((!F->RejectUnknown && !F->DestroyedSource) ||
                !IsValid(W) || !W->HasBegunPlay() ||
                F->Kernels.Num() != 11) { return false; }
            if (F->RejectUnknown)
            {
                if (!F->DormantFailure.IsEmpty() || F->RetirementWorldTicks < 6 ||
                    !Live(F->UnknownChild) || !SamePoses(F->Dormant, F->RetiredPoses)) { return false; }
                for (const auto& H : F->FrozenKnown) { if (!Live(H)) { return false; } }
                return true;
            }
            for (const auto& H : F->Kernels)
            { if (Allocated(H)) { return false; } }
            return true;
        });
        if (RejectUnknown)
        {
            Act(TEXT("actor loss independently retires the unproven owned subtree"), [F]()
            {
                auto* S = F->State();
                if (!F->Context() || !S || !F->WatchingRefusal ||
                    F->RetirementWorldTicks < 6) { return false; }
                F->WatchingRefusal = false;
                F->DestroyedSource = S->Destroy();
                return F->DestroyedSource;
            });
            Wait(TEXT("bridge cascade retires known roles, unknown child and both client replicas"), [F]()
            {
                if (!F->DestroyedSource || Allocated(F->UnknownChild)) { return false; }
                for (const auto& H : F->FrozenKnown)
                { if (Allocated(H)) { return false; } }
                for (int32 Client = 0; Client < 2; ++Client)
                {
                    for (int32 Slot = 0; Slot < 11; ++Slot)
                    {
                        if (ck::IsValid(UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(
                                F->Worlds[Client + 1].Get(), 1, Slot)))
                        { return false; }
                    }
                }
                return true;
            });
        }
        Test.AddCommand(new FScenario(&Test, F, MoveTemp(Steps)));
        Test.AddCommand(new FRestore(F));
        Test.AddCommand(new FCk_Latent_EndPIE());
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkRotateTowardsGym_Admission_Production,
    "Ck.RotateTowardsGym.Network.Admission.Production",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkRotateTowardsGym_Admission_Production::RunTest(const FString& Parameters)
{ return ck_rotate_admission::Run(*this, Parameters); }
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkRotateTowardsGym_Admission_UnknownChild,
    "Ck.RotateTowardsGym.Network.Admission.UnknownChild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkRotateTowardsGym_Admission_UnknownChild::RunTest(const FString& Parameters)
{ return ck_rotate_admission::Run(*this, Parameters, true); }
#endif
