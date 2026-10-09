#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && WITH_CK_DEBUG_OVERLAY

#include "CkGym_StartupSettings.h"
#include "CkGymRegistry_Utils.h"
#include "CkSelectionGym_SourceComposer.h"
#include "CkSelectionGym_SourceSession.h"
#include "CkSelectionGym_Mirror.h"
#include "../../CkSelectionGym_WireValidation.h"
#include "CkGym_WireTransformRuntime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityScriptGym_Runtime.h"
#include "../../CkSelectionGym_BankCensus.h"
#include "CkEntityDebugOverlay/Selection/CkDebugOverlay_SourceAdmission.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkLabel/CkLabel_Fragment.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Fragment.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_selection_authored_network
{
    constexpr int32 Worlds = 3, Clients = 2;
    constexpr auto EntryMap = TEXT("/Engine/Maps/Entry");
    constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_SelectionGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_SelectionGym_GameState");
    constexpr auto PCPath = TEXT("/Script/Angelscript.Ck_SelectionGym_PlayerController");
    constexpr auto PawnPath = TEXT("/Script/Angelscript.Ck_Gym_Base_Pawn");
    constexpr auto MarkerPath = TEXT("/Script/Angelscript.Ck_SelectionGym_LabelMarker");
    constexpr auto WallPath = TEXT("/Script/Angelscript.Ck_Gym_ObstacleWall");

    template<class T> T* Field(const UStruct* Type, const TCHAR* Name)
    { return Type ? FindFProperty<T>(Type, Name) : nullptr; }

    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    FCk_Handle Root(UWorld* World)
    { return IsValid(World) ? UCk_Utils_EcsWorld_Subsystem_UE::
        TryGet_TransientEntityForWorld(*World) : FCk_Handle{}; }

    bool Direct(const FCk_Handle& Child, const FCk_Handle& Parent)
    {
        return Allocated(Child) && Allocated(Parent) &&
            UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child) &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Parent;
    }

    bool Same(const FCkSelectionGym_WireSnapshot& A,
        const FCkSelectionGym_WireSnapshot& B)
    { return FCkSelectionGym_WireSnapshot::StaticStruct()->
        CompareScriptStruct(&A, &B, PPF_None); }

    bool ExpectedIris(const FCkSelectionGym_WireSnapshot& Source,
        const FCkSelectionGym_WireSnapshot& Received)
    {
        if (Source.Nodes.Num() != Received.Nodes.Num() ||
            Source.Visuals.Num() != Received.Visuals.Num()) { return false; }
        auto Expected = Source;
        for (int32 I = 0; I < Source.Nodes.Num(); ++I)
        {
            const auto& A = Source.Nodes[I];
            const auto& B = Received.Nodes[I];
            if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
                    A.WorldPose, B.WorldPose) ||
                !UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
                    A.SceneNodeLocalOffset, B.SceneNodeLocalOffset)) { return false; }
            Expected.Nodes[I].WorldPose = B.WorldPose;
            Expected.Nodes[I].SceneNodeLocalOffset = B.SceneNodeLocalOffset;
        }
        for (int32 I = 0; I < Source.Visuals.Num(); ++I)
        {
            const auto& A = Source.Visuals[I];
            const auto& B = Received.Visuals[I];
            if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
                    A.ActorPose, B.ActorPose) ||
                !UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
                    A.MeshRelativePose, B.MeshRelativePose) ||
                !UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
                    A.LabelRelativePose, B.LabelRelativePose)) { return false; }
            Expected.Visuals[I].ActorPose = B.ActorPose;
            Expected.Visuals[I].MeshRelativePose = B.MeshRelativePose;
            Expected.Visuals[I].LabelRelativePose = B.LabelRelativePose;
        }
        return Same(Expected, Received);
    }

    FString Identity(const FCkSelectionGym_WireSnapshot& Frame)
    {
        return FString::Printf(TEXT("%lld/%lld/%lld"),
            Frame.SourceGeneration, Frame.ScenarioGeneration, Frame.Sequence);
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
            if (!IsValid(Target) || !IsValid(Function) || !Args ||
                !Args->GetStructMemory()) { return false; }
            Target->ProcessEvent(Function, Args->GetStructMemory());
            return true;
        }
    };

    bool Activate(APlayerController* PC, int32 Row)
    {
        FCall Call(PC, TEXT("Request_ControlActivated"));
        if (!Call.Function || !Call.Args || !Call.Args->GetStructMemory())
        { return false; }
        auto* Index = Field<FIntProperty>(Call.Function, TEXT("RowIndex"));
        if (!Index || Call.Function->NumParms != 1) { return false; }
        Index->SetPropertyValue_InContainer(Call.Args->GetStructMemory(), Row);
        return Call.Send();
    }

    bool PresentationReady(APlayerController* PC)
    {
        FCall Call(PC, TEXT("Get_IsLocalPresentationReady"));
        if (!Call.Function || !Call.Args || !Call.Args->GetStructMemory())
        { return false; }
        auto* Return = Field<FBoolProperty>(Call.Function, TEXT("ReturnValue"));
        return Return && Call.Function->NumParms == 1 && Call.Send() &&
            Return->GetPropertyValue_InContainer(Call.Args->GetStructMemory());
    }

    bool Ack(APlayerController* PC, int32 Serial, int32 Action, int64 PreviousG,
        int64& OutCompletedG, int64& OutCompletedQ, int32& OutCompletedScenario)
    {
        OutCompletedG = 0; OutCompletedQ = 0; OutCompletedScenario = -1;
        auto* Property = IsValid(PC)
            ? Field<FStructProperty>(PC->GetClass(), TEXT("LastControlAck")) : nullptr;
        if (!Property) { return false; }
        const void* Data = Property->ContainerPtrToValuePtr<void>(PC);
        auto* S = Field<FIntProperty>(Property->Struct, TEXT("Serial"));
        auto* G = Field<FInt64Property>(Property->Struct, TEXT("ScenarioGeneration"));
        auto* Source = Field<FInt64Property>(Property->Struct, TEXT("SourceGeneration"));
        auto* A = Field<FIntProperty>(Property->Struct, TEXT("Action"));
        auto* R = Field<FIntProperty>(Property->Struct, TEXT("Result"));
        auto* CompletedG = Field<FInt64Property>(Property->Struct,
            TEXT("CompletedScenarioGeneration"));
        auto* CompletedQ = Field<FInt64Property>(Property->Struct,
            TEXT("CompletedSequence"));
        auto* CompletedScenario = Field<FIntProperty>(Property->Struct,
            TEXT("CompletedScenario"));
        if (!S || !G || !Source || !A || !R || !CompletedG ||
            !CompletedQ || !CompletedScenario ||
            !(S->GetPropertyValue_InContainer(Data) == Serial &&
            G->GetPropertyValue_InContainer(Data) == PreviousG &&
            Source->GetPropertyValue_InContainer(Data) == 1 &&
            A->GetPropertyValue_InContainer(Data) == Action &&
            R->GetPropertyValue_InContainer(Data) == 1)) { return false; }
        OutCompletedG = CompletedG->GetPropertyValue_InContainer(Data);
        OutCompletedQ = CompletedQ->GetPropertyValue_InContainer(Data);
        OutCompletedScenario = CompletedScenario->GetPropertyValue_InContainer(Data);
        return OutCompletedG > PreviousG ||
            (OutCompletedG == PreviousG && OutCompletedQ > 0);
    }

    struct FRun
    {
        bool Dedicated = false, Captured = false, SettingsCaptured = false;
        bool Failed = false;
        UClass* BaseMode = nullptr;
        UClass* Mode = nullptr;
        UClass* StateClass = nullptr;
        UClass* PCClass = nullptr;
        UClass* PawnClass = nullptr;
        UClass* MarkerClass = nullptr;
        UClass* WallClass = nullptr;
        FStructProperty* SnapshotProperty = nullptr;
        FObjectProperty* CompositionProperty = nullptr;
        TWeakObjectPtr<UWorld> World[Worlds];
        TWeakObjectPtr<AGameStateBase> State[Worlds];
        TWeakObjectPtr<APlayerController> Local[Clients], Remote[Clients], Host;
        FCk_Handle WorldRoot[Worlds];
        FCk_Handle SourceRoot, MirrorRoot[Clients];
        TArray<FCk_Handle> SourceDirectRoots, BeforeSourceDirectRoots;
        FCk_Handle BeforeSourceRoot, BeforeMirrorRoot[Clients];
        FCkSelectionGym_WireSnapshot LastAuthority, BeforeAction;
        TMap<FString, FCkSelectionGym_WireSnapshot> AuthorityHistory;
        TMap<int64, FCk_Handle> BeforeX[Clients];
        int64 MovingKey = 0;
        FVector MovingBefore = FVector::ZeroVector;
        int32 NextSerial[3]{1, 1, 1};
        int32 ActionIndex = -1;
        int32 SentSerial = 0, SentAction = -1, SentOwner = -1;
        int64 PreviousG = 0;
        enum class EStep : uint8 { Baseline, Send, Wait, Motion, Done } Step = EStep::Baseline;
        double Deadline = 0.0;
        ECkGym_StartupMode Startup{};
        FString LastGym;
        TArray<FString> Recents;
        void Restore()
        {
            if (!SettingsCaptured) { return; }
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = Startup;
            Settings->LastGymName = LastGym;
            Settings->RecentGymNames = Recents;
            Settings->SaveConfig();
            SettingsCaptured = false;
        }
        ~FRun() { Restore(); }
    };

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

    bool SameGameStateIris(const FRun& R)
    {
        const uint64 AuthorityId = IrisId(R.World[0].Get(), R.State[0].Get());
        if (AuthorityId == 0) { return false; }
        for (int32 I = 0; I < Clients; ++I)
        { if (AuthorityId != IrisId(R.World[I + 1].Get(), R.State[I + 1].Get()))
          { return false; } }
        return true;
    }

    const FCkSelectionGym_WireSnapshot* Frame(const FRun& R, int32 Index)
    {
        return R.SnapshotProperty && R.State[Index].IsValid()
            ? R.SnapshotProperty->ContainerPtrToValuePtr<FCkSelectionGym_WireSnapshot>(
                R.State[Index].Get()) : nullptr;
    }

    UCkSelectionGym_SourceComposition* Composition(const FRun& R)
    {
        auto* GS = R.State[0].Get();
        return IsValid(GS) && R.CompositionProperty
            ? Cast<UCkSelectionGym_SourceComposition>(
                R.CompositionProperty->GetObjectPropertyValue_InContainer(GS)) : nullptr;
    }

    bool PlayerReady(APlayerController* PC, UWorld* World, const FRun& R)
    {
        if (!IsValid(PC) || PC->GetClass() != R.PCClass ||
            PC->GetWorld() != World || !PC->HasActorBegunPlay()) { return false; }
        auto* Pawn = PC->GetPawn().Get();
        auto* PS = PC->PlayerState.Get();
        return IsValid(Pawn) && Pawn->GetClass() == R.PawnClass &&
            Pawn->GetWorld() == World && Pawn->GetController() == PC &&
            Pawn->HasActorBegunPlay() && IsValid(PS) &&
            PS->GetWorld() == World && Pawn->GetPlayerState() == PS;
    }

    bool Context(FRun& R)
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        if (!IsValid(Server) || !Server->HasBegunPlay() ||
            Server->GetNetMode() != (R.Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
            ck::auto_test::net::Get_AllPIEWorlds().Num() != Worlds ||
            ck::auto_test::net::Get_NumClientWorlds() != Clients ||
            !IsValid(Server->GetAuthGameMode()) ||
            Server->GetAuthGameMode()->GetClass() != R.Mode ||
            !IsValid(Server->GetGameState()) ||
            Server->GetGameState()->GetClass() != R.StateClass) { return false; }
        UWorld* WorldPtrs[Worlds]{Server};
        AGameStateBase* States[Worlds]{Server->GetGameState()};
        APlayerController* Locals[Clients]{};
        APlayerController* Remotes[Clients]{};
        FCk_Handle Roots[Worlds]{Root(Server)};
        if (!Allocated(Roots[0])) { return false; }
        for (int32 I = 0; I < Clients; ++I)
        {
            WorldPtrs[I + 1] = ck::auto_test::net::Get_ClientWorld(I);
            States[I + 1] = IsValid(WorldPtrs[I + 1])
                ? WorldPtrs[I + 1]->GetGameState() : nullptr;
            Locals[I] = IsValid(WorldPtrs[I + 1])
                ? WorldPtrs[I + 1]->GetFirstPlayerController() : nullptr;
            Remotes[I] = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
            Roots[I + 1] = Root(WorldPtrs[I + 1]);
            if (!IsValid(WorldPtrs[I + 1]) || WorldPtrs[I + 1]->GetNetMode() != NM_Client ||
                !WorldPtrs[I + 1]->HasBegunPlay() || !IsValid(States[I + 1]) ||
                States[I + 1]->GetClass() != R.StateClass ||
                !PlayerReady(Locals[I], WorldPtrs[I + 1], R) ||
                !Locals[I]->IsLocalController() ||
                !PlayerReady(Remotes[I], Server, R) ||
                Remotes[I]->IsLocalController() || !Allocated(Roots[I + 1]) ||
                Roots[I + 1] == Roots[0]) { return false; }
        }
        if (Locals[0] == Locals[1] || Remotes[0] == Remotes[1] ||
            Roots[1] == Roots[2]) { return false; }
        auto* GI = Server->GetGameInstance();
        if (!IsValid(GI) || GI->GetLocalPlayers().Num() != (R.Dedicated ? 0 : 1))
        { return false; }
        APlayerController* Host = nullptr;
        if (!R.Dedicated)
        {
            auto* LP = GI->GetLocalPlayers()[0];
            Host = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
            if (!PlayerReady(Host, Server, R) || !Host->IsLocalController())
            { return false; }
        }
        if (R.Captured)
        {
            if (R.Host.Get() != Host) { return false; }
            for (int32 I = 0; I < Worlds; ++I)
            { if (R.World[I].Get() != WorldPtrs[I] || R.State[I].Get() != States[I] ||
                  R.WorldRoot[I] != Roots[I]) { return false; } }
            for (int32 I = 0; I < Clients; ++I)
            { if (R.Local[I].Get() != Locals[I] || R.Remote[I].Get() != Remotes[I])
              { return false; } }
            return true;
        }
        for (int32 I = 0; I < Worlds; ++I)
        { R.World[I] = WorldPtrs[I]; R.State[I] = States[I]; R.WorldRoot[I] = Roots[I]; }
        for (int32 I = 0; I < Clients; ++I)
        { R.Local[I] = Locals[I]; R.Remote[I] = Remotes[I]; }
        R.Host = Host;
        R.Captured = true;
        return true;
    }

    bool Station(APlayerController* PC, UWorld* World)
    {
        const auto W = Root(World);
        const auto Owner = IsValid(PC)
            ? UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(PC) : FCk_Handle{};
        if (!Allocated(W) || !Allocated(Owner)) { return false; }
        const FName Tag(*FString::Printf(TEXT("Gym.Selection_%s"), *PC->GetName()));
        const auto Matches = UCk_Utils_EntityTag_UE::ForEach_Entity(W, Tag);
        if (Matches.Num() != 1 || !Allocated(Matches[0]) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Matches[0]))
        { return false; }
        const auto Cohort = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Matches[0]);
        return Direct(Matches[0], Cohort) && Direct(Cohort, Owner);
    }

    bool SourceBank(FRun& R, const FCkSelectionGym_WireSnapshot& Wire)
    {
        auto* GS = R.State[0].Get();
        auto* C = Composition(R);
        if (!IsValid(GS) || !IsValid(C) || C->GetOuter() != GS ||
            !IsValid(UCkSelectionGym_SourceComposer_UE::Get_Session(C)))
        { return false; }
        FCk_Handle Primary;
        if (!UCkSelectionGym_SourceComposer_UE::TryGet_PrimaryBareRootForTest(
                C, Primary) || !Direct(Primary, R.WorldRoot[0]) ||
            !UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(Primary))
        { return false; }
        auto* Property = Field<FArrayProperty>(C->GetClass(), TEXT("BareRoots"));
        auto* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
        if (!Inner || Inner->Struct != FCk_Handle::StaticStruct()) { return false; }
        FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(C));
        TArray<FCk_Handle> DirectRoots;
        for (int32 I = 0; I < Array.Num(); ++I)
        {
            const auto H = *reinterpret_cast<const FCk_Handle*>(Array.GetRawPtr(I));
            if (!Direct(H, R.WorldRoot[0])) { return false; }
            DirectRoots.Add(H);
        }
        if (DirectRoots.IsEmpty() || DirectRoots[0] != Primary) { return false; }
        // Census takes the primary root separately; keep all receipts for retirement.
        auto AdditionalRoots = DirectRoots;
        AdditionalRoots.RemoveAt(0);
        TArray<FCk_Handle> Census;
        FString Why;
        if (!ck_selection_gym_census::TryCensusAllocatedBank(R.World[0].Get(),
                Primary, AdditionalRoots, Census, Why) || Census.Num() != Wire.Nodes.Num())
        { return false; }
        TMap<int64, FCk_Handle> ByKey;
        for (const auto& H : Census)
        {
            int64 Key = 0;
            if (!ck_selection_gym_census::TryGetOpaqueEntityKey(
                    H, R.WorldRoot[0], Key) || ByKey.Contains(Key)) { return false; }
            ByKey.Add(Key, H);
        }
        for (const auto& Row : Wire.Nodes)
        {
            const auto* H = ByKey.Find(Row.Key);
            if (!H || (Row.RootLabel.IsValid() !=
                    H->Has<ck::FFragment_GameplayLabel>())) { return false; }
            if (Row.RootLabel.IsValid() &&
                H->Get<ck::FFragment_GameplayLabel>().Get_Label() != Row.RootLabel)
            { return false; }
        }
        const auto RootLabel = FGameplayTag::RequestGameplayTag(
            FName(TEXT("Ck.Debug.Selection.Root")), false);
        if (!RootLabel.IsValid()) { return false; }
        int32 DeclaredRoots = 0, PhysicalRoots = 0;
        for (const auto& Row : Wire.Nodes)
        { DeclaredRoots += Row.RootLabel == RootLabel; }
        const auto Registry = R.WorldRoot[0].Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        Registry.View<ck::FFragment_GameplayLabel>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_GameplayLabel& Label)
        {
            const FCk_Handle H{Entity, RegistryHandle};
            PhysicalRoots += Allocated(H) && Label.Get_Label() == RootLabel;
        });
        if (DeclaredRoots == 0 || PhysicalRoots != DeclaredRoots) { return false; }
        R.SourceRoot = Primary;
        R.SourceDirectRoots = MoveTemp(DirectRoots);
        return true;
    }

    bool VisualCount(UWorld* World, AGameStateBase* GS, const FRun& R,
        int32 Expected)
    {
        if (!IsValid(World) || !IsValid(GS)) { return false; }
        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            auto* Actor = *It;
            if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() ||
                Actor->GetWorld() != World || Actor->GetOwner() != GS ||
                (Actor->GetClass() != R.MarkerClass &&
                 Actor->GetClass() != R.WallClass)) { continue; }
            if (Actor->GetIsReplicated()) { return false; }
            ++Count;
        }
        return Count == Expected;
    }

    bool ObserveAuthority(FRun& R)
    {
        const auto* Wire = Frame(R, 0);
        if (!Wire || Wire->Lifecycle != ECkSelectionGym_WireLifecycle::Active ||
            Wire->SourceGeneration != 1 || Wire->Scenario < 0 || Wire->Scenario > 4 ||
            Wire->Sequence <= 0) { return false; }
        FString Why;
        if (!ck_selection_gym_wire::TryValidateActiveValue(*Wire, Why) ||
            !SourceBank(R, *Wire) ||
            !VisualCount(R.World[0].Get(), R.State[0].Get(), R, Wire->Visuals.Num()))
        { return false; }
        ECkSelectionGym_MirrorStatus Status;
        if (UCkSelectionGym_Mirror_UE::TryGetStatus(R.World[0].Get(),
                R.State[0].Get(), Status, Why)) { return false; }
        R.LastAuthority = *Wire;
        R.AuthorityHistory.Add(Identity(*Wire), *Wire);
        return true;
    }

    bool ClientInstalled(FRun& R, int32 I, int32 Scenario, int64 G)
    {
        const auto* Received = Frame(R, I + 1);
        if (!Received || Received->Lifecycle != ECkSelectionGym_WireLifecycle::Active ||
            Received->Scenario != Scenario || Received->SourceGeneration != 1 ||
            Received->ScenarioGeneration != G || Received->Sequence <= 0)
        { return false; }
        const auto* Source = R.AuthorityHistory.Find(Identity(*Received));
        if (!Source || !ExpectedIris(*Source, *Received)) { return false; }
        FString Why;
        ECkSelectionGym_MirrorStatus Status;
        FCkSelectionGym_WireSnapshot Installed;
        FCk_Handle ScenarioRoot;
        if (!UCkSelectionGym_Mirror_UE::TryGetStatus(R.World[I + 1].Get(),
                R.State[I + 1].Get(), Status, Why) ||
            Status != ECkSelectionGym_MirrorStatus::Installed ||
            !UCkSelectionGym_Mirror_UE::TryGetInstalled(R.World[I + 1].Get(),
                R.State[I + 1].Get(), Installed, ScenarioRoot, Why) ||
            !Same(Installed, *Received) || !Allocated(ScenarioRoot) ||
            ScenarioRoot == R.SourceRoot ||
            !VisualCount(R.World[I + 1].Get(), R.State[I + 1].Get(),
                R, Received->Visuals.Num())) { return false; }
        TSet<FCk_Handle> Seen;
        for (const auto& Row : Received->Nodes)
        {
            FCk_Handle H;
            if (!UCkSelectionGym_Mirror_UE::TryGetLocalNodeForTest(
                    R.World[I + 1].Get(), R.State[I + 1].Get(), Row.Key, H) ||
                !Allocated(H) || Seen.Contains(H) ||
                H.Has<ck::FFragment_Timer>() ||
                H.Has<ck::FFragment_IntegerAttribute>() ||
                H.Has<ck::FFragment_Sm>() ||
                !UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(H))
            { return false; }
            Seen.Add(H);
        }
        if (Seen.Num() != Received->Nodes.Num()) { return false; }
        R.MirrorRoot[I] = ScenarioRoot;
        return true;
    }

    bool Converged(FRun& R, int32 Scenario, int64 G)
    {
        if (!Context(R) || !SameGameStateIris(R) || !ObserveAuthority(R) ||
            R.LastAuthority.Scenario != Scenario ||
            R.LastAuthority.ScenarioGeneration != G) { return false; }
        for (int32 I = 0; I < Clients; ++I)
        {
            if (!PresentationReady(R.Local[I].Get()) ||
                !Station(R.Local[I].Get(), R.World[I + 1].Get()) ||
                !ClientInstalled(R, I, Scenario, G)) { return false; }
        }
        if (!R.Dedicated &&
            (!PresentationReady(R.Host.Get()) ||
             !Station(R.Host.Get(), R.World[0].Get()))) { return false; }
        return R.MirrorRoot[0] != R.MirrorRoot[1];
    }

    FString TimeoutSummary(FRun& R)
    {
        FString Result = FString::Printf(
            TEXT("authored Selection transition deadline expired; step=%d action=%d history=%d"),
            static_cast<int32>(R.Step), R.ActionIndex, R.AuthorityHistory.Num());
        const bool ExactContext = Context(R);
        Result += FString::Printf(TEXT(" context=%d iris=%d"),
            ExactContext, ExactContext && SameGameStateIris(R));
        for (int32 I = 0; I < Worlds; ++I)
        {
            auto* GS = R.State[I].Get();
            const auto* Wire = ExactContext ? Frame(R, I) : nullptr;
            bool SourceFailed = false;
            const auto* FailedField = IsValid(GS)
                ? Field<FBoolProperty>(GS->GetClass(), TEXT("SourceFailed")) : nullptr;
            if (FailedField) { SourceFailed = FailedField->GetPropertyValue_InContainer(GS); }
            Result += FString::Printf(TEXT("; w%d gs=%d failedField=%d sourceFailed=%d"),
                I, IsValid(GS), FailedField != nullptr, SourceFailed);
            if (Wire)
            {
                Result += FString::Printf(
                    TEXT(" lifecycle=%d srcG=%lld G=%lld Q=%lld scenario=%d nodes=%d visuals=%d timers=%d integers=%d sm=%d"),
                    static_cast<int32>(Wire->Lifecycle), Wire->SourceGeneration,
                    Wire->ScenarioGeneration, Wire->Sequence, Wire->Scenario,
                    Wire->Nodes.Num(), Wire->Visuals.Num(), Wire->Timers.Num(),
                    Wire->Integers.Num(), Wire->StateMachines.Num());
            }
            if (I == 0)
            {
                auto* C = ExactContext ? Composition(R) : nullptr;
                auto* Session = IsValid(C)
                    ? UCkSelectionGym_SourceComposer_UE::Get_Session(C) : nullptr;
                const auto* ReasonField = IsValid(Session)
                    ? Field<FStrProperty>(Session->GetClass(), TEXT("TerminalReason"))
                    : nullptr;
                const FString TerminalReason = ReasonField
                    ? ReasonField->GetPropertyValue_InContainer(Session)
                    : TEXT("<unavailable>");
                bool ValidWire = false, Bank = false, Visuals = false;
                if (Wire && Wire->Lifecycle == ECkSelectionGym_WireLifecycle::Active)
                {
                    FString Why;
                    ValidWire = ck_selection_gym_wire::TryValidateActiveValue(*Wire, Why);
                    if (ValidWire)
                    {
                        Bank = SourceBank(R, *Wire);
                        Visuals = VisualCount(R.World[0].Get(), GS, R,
                            Wire->Visuals.Num());
                    }
                }
                Result += FString::Printf(
                    TEXT(" composition=%d session=%d terminalReason='%s' wireValid=%d sourceBank=%d visualCount=%d"),
                    IsValid(C), IsValid(Session), *TerminalReason,
                    ValidWire, Bank, Visuals);
            }
            else if (ExactContext)
            {
                const int32 Client = I - 1;
                const bool Ready = PresentationReady(R.Local[Client].Get());
                const bool HasStation = Station(R.Local[Client].Get(), R.World[I].Get());
                ECkSelectionGym_MirrorStatus Status =
                    ECkSelectionGym_MirrorStatus::Unavailable;
                FString StatusWhy;
                const bool HasStatus = UCkSelectionGym_Mirror_UE::TryGetStatus(
                    R.World[I].Get(), GS, Status, StatusWhy);
                FCkSelectionGym_WireSnapshot Installed;
                FCk_Handle ScenarioRoot;
                FString InstalledWhy;
                const bool HasInstalled = UCkSelectionGym_Mirror_UE::TryGetInstalled(
                    R.World[I].Get(), GS, Installed, ScenarioRoot, InstalledWhy);
                const auto* Source = Wire
                    ? R.AuthorityHistory.Find(Identity(*Wire)) : nullptr;
                const bool HistoryExpected = Source && Wire &&
                    ExpectedIris(*Source, *Wire);
                Result += FString::Printf(
                    TEXT(" ready=%d station=%d statusRead=%d status=%d statusWhy='%s' installed=%d installedWhy='%s' installedSame=%d historyFound=%d historyExpectedIris=%d"),
                    Ready, HasStation, HasStatus, static_cast<int32>(Status),
                    *StatusWhy, HasInstalled, *InstalledWhy,
                    HasInstalled && Wire && Same(Installed, *Wire),
                    Source != nullptr, HistoryExpected);
            }
        }
        if (ExactContext && !R.Dedicated)
        {
            Result += FString::Printf(TEXT("; hostReady=%d hostStation=%d"),
                PresentationReady(R.Host.Get()),
                Station(R.Host.Get(), R.World[0].Get()));
        }
        return Result;
    }

    bool Moving(const FCkSelectionGym_WireSnapshot& Wire,
        int64& OutKey, FVector& OutPose)
    {
        OutKey = 0; OutPose = FVector::ZeroVector;
        for (const auto& Visual : Wire.Visuals)
        { if (Visual.LabelText == TEXT("Moving root"))
          { if (OutKey != 0) { return false; } OutKey = Visual.NodeKey; } }
        for (const auto& Node : Wire.Nodes)
        { if (Node.Key == OutKey && Node.HasTransform)
          { OutPose = Node.WorldPose.GetLocation(); return OutKey > 0; } }
        return false;
    }

    bool CaptureKeys(FRun& R, int32 I, TMap<int64, FCk_Handle>& Out)
    {
        Out.Reset();
        const auto* Received = Frame(R, I + 1);
        if (!Received) { return false; }
        for (const auto& Row : Received->Nodes)
        {
            FCk_Handle H;
            if (!UCkSelectionGym_Mirror_UE::TryGetLocalNodeForTest(
                    R.World[I + 1].Get(), R.State[I + 1].Get(), Row.Key, H) ||
                Out.Contains(Row.Key)) { Out.Reset(); return false; }
            Out.Add(Row.Key, H);
        }
        return Out.Num() == Received->Nodes.Num();
    }

    bool ProveX(FRun& R)
    {
        TSet<int64> Current;
        for (const auto& Row : R.LastAuthority.Nodes) { Current.Add(Row.Key); }
        int32 Removed = 0, Added = 0;
        TSet<int64> Previous;
        for (const auto& Row : R.BeforeAction.Nodes) { Previous.Add(Row.Key); }
        for (const auto Key : Previous) { Removed += !Current.Contains(Key); }
        for (const auto Key : Current) { Added += !Previous.Contains(Key); }
        if (Removed != 1 || Added != 1 ||
            R.LastAuthority.Sequence <= R.BeforeAction.Sequence) { return false; }
        for (int32 I = 0; I < Clients; ++I)
        {
            TMap<int64, FCk_Handle> Now;
            if (!CaptureKeys(R, I, Now)) { return false; }
            int32 LocalRemoved = 0, LocalAdded = 0;
            for (const auto& Pair : R.BeforeX[I])
            {
                const auto* SameKey = Now.Find(Pair.Key);
                if (!SameKey) { ++LocalRemoved; if (Allocated(Pair.Value)) { return false; } }
                else if (*SameKey != Pair.Value) { return false; }
            }
            for (const auto& Pair : Now) { LocalAdded += !R.BeforeX[I].Contains(Pair.Key); }
            if (LocalRemoved != 1 || LocalAdded != 1) { return false; }
        }
        return true;
    }

    class FDriver final : public IAutomationLatentCommand
    {
    public:
        FDriver(FAutomationTestBase* InTest, TSharedRef<FRun> InRun)
            : Test(InTest), Run(InRun) {}

        bool Update() override
        {
            if (Run->Step == FRun::EStep::Done) { return true; }
            if (Run->Deadline <= 0.0) { Run->Deadline = FPlatformTime::Seconds() + 60.0; }
            if (FPlatformTime::Seconds() > Run->Deadline)
            {
                const FString Summary = TimeoutSummary(*Run);
                return Stop(*Summary);
            }
            if (!Context(*Run)) { return Stop(TEXT("captured Selection owner/world context changed")); }
            ObserveAuthority(*Run); // collect actual historical G/Q without minting source sequence
            if (Run->Step == FRun::EStep::Baseline)
            {
                if (!Converged(*Run, 0, 1)) { return false; }
                Run->Step = FRun::EStep::Send;
                Run->ActionIndex = 0;
                Run->Deadline = FPlatformTime::Seconds() + 60.0;
                return false;
            }
            constexpr int32 Rows[7]{2, 3, 4, 5, 6, 7, 8};
            constexpr int32 Actions[7]{0, 1, 2, 3, 4, 5, 6};
            constexpr int32 Scenarios[7]{0, 1, 2, 3, 4, 4, 4};
            if (Run->Step == FRun::EStep::Motion)
            {
                int64 Key = 0; FVector After;
                if (!Converged(*Run, 4, 6) ||
                    !Moving(Run->LastAuthority, Key, After) ||
                    Key != Run->MovingKey ||
                    After.Equals(Run->MovingBefore, 5.0)) { return false; }
                Run->Step = FRun::EStep::Send;
                Run->Deadline = FPlatformTime::Seconds() + 60.0;
                return false;
            }
            if (Run->Step == FRun::EStep::Send)
            {
                if (Run->ActionIndex >= 7)
                { Run->Step = FRun::EStep::Done; return true; }
                const int32 Owner = Run->ActionIndex == 5 && !Run->Dedicated
                    ? 2 : Run->ActionIndex % 2;
                auto* Sender = Owner == 2 ? Run->Host.Get() : Run->Local[Owner].Get();
                const int32 ScenarioBefore = Run->ActionIndex <= 4
                    ? (Run->ActionIndex == 0 ? 0 : Run->ActionIndex - 1) : 4;
                const int64 GBefore = Run->ActionIndex == 6 ? 7 : Run->ActionIndex + 1;
                if (!Converged(*Run, ScenarioBefore, GBefore) ||
                    !PresentationReady(Sender)) { return false; }
                Run->BeforeAction = Run->LastAuthority;
                Run->BeforeSourceRoot = Run->SourceRoot;
                Run->BeforeSourceDirectRoots = Run->SourceDirectRoots;
                for (int32 I = 0; I < Clients; ++I)
                { Run->BeforeMirrorRoot[I] = Run->MirrorRoot[I]; }
                if (Run->ActionIndex == 6)
                {
                    for (int32 I = 0; I < Clients; ++I)
                    { if (!CaptureKeys(*Run, I, Run->BeforeX[I]))
                      { return Stop(TEXT("X source key receipts unavailable")); } }
                }
                Run->PreviousG = GBefore;
                Run->SentAction = Actions[Run->ActionIndex];
                Run->SentOwner = Owner;
                Run->SentSerial = Run->NextSerial[Owner]++;
                if (!Activate(Sender, Rows[Run->ActionIndex]))
                { return Stop(TEXT("reflected owned Selection control unavailable")); }
                Run->Step = FRun::EStep::Wait;
                Run->Deadline = FPlatformTime::Seconds() + 60.0;
                return false;
            }
            const int32 I = Run->ActionIndex;
            auto* Sender = Run->SentOwner == 2 ? Run->Host.Get() :
                Run->Local[Run->SentOwner].Get();
            const int64 ExpectedG = I == 6 ? 7 : I + 2;
            int64 CompletedG = 0, CompletedQ = 0;
            int32 CompletedScenario = -1;
            if (!Ack(Sender, Run->SentSerial, Run->SentAction, Run->PreviousG,
                    CompletedG, CompletedQ, CompletedScenario)) { return false; }
            const auto* CompletedFrame = Run->AuthorityHistory.Find(
                FString::Printf(TEXT("1/%lld/%lld"), CompletedG, CompletedQ));
            if (CompletedG != ExpectedG || CompletedScenario != Scenarios[I] ||
                !CompletedFrame || CompletedFrame->Scenario != CompletedScenario ||
                !Converged(*Run, Scenarios[I], ExpectedG)) { return false; }
            const auto* OwnerFrame = Frame(*Run,
                Run->SentOwner == 2 ? 0 : Run->SentOwner + 1);
            if (!OwnerFrame || OwnerFrame->ScenarioGeneration != CompletedG ||
                OwnerFrame->Scenario != CompletedScenario ||
                OwnerFrame->Sequence < CompletedQ ||
                !PresentationReady(Sender)) { return false; }
            if (I != 6)
            {
                if (Run->BeforeSourceDirectRoots.IsEmpty())
                { return Stop(TEXT("prior authority direct-root receipt missing")); }
                for (const auto& Old : Run->BeforeSourceDirectRoots)
                { if (Allocated(Old))
                  { return Stop(TEXT("prior authority source root survived full reset")); } }
                for (const auto& Old : Run->BeforeMirrorRoot)
                { if (Allocated(Old))
                  { return Stop(TEXT("prior client scenario root survived full reset")); } }
            }
            else if (Run->SourceRoot != Run->BeforeSourceRoot ||
                Run->MirrorRoot[0] != Run->BeforeMirrorRoot[0] ||
                Run->MirrorRoot[1] != Run->BeforeMirrorRoot[1])
            { return Stop(TEXT("X replaced a scenario root instead of one leaf")); }
            if (I == 4)
            {
                if (!Moving(Run->LastAuthority,
                        Run->MovingKey, Run->MovingBefore))
                { return Stop(TEXT("authored moving source absent")); }
                Run->ActionIndex++;
                Run->Step = FRun::EStep::Motion;
            }
            else
            {
                if (I == 6 && !ProveX(*Run))
                { return Stop(TEXT("X failed one-leaf replacement and survivor identity")); }
                Run->ActionIndex++;
                Run->Step = FRun::EStep::Send;
            }
            Run->Deadline = FPlatformTime::Seconds() + 60.0;
            return false;
        }

    private:
        bool Stop(const TCHAR* Reason)
        {
            Run->Failed = true;
            Test->AddError(Reason);
            ck::auto_test::net::FailSession(Reason);
            Run->Step = FRun::EStep::Done;
            return true;
        }
        FAutomationTestBase* Test;
        TSharedRef<FRun> Run;
    };

    struct FRestore final : IAutomationLatentCommand
    {
        TSharedRef<FRun> Run;
        explicit FRestore(TSharedRef<FRun> In) : Run(In) {}
        bool Update() override { Run->Restore(); return true; }
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSelectionGym_SharedWorld,
    "Ck.Selection.Gym.SharedWorld.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkSelectionGym_SharedWorld::RunTest(const FString& Parameters)
{
    using namespace ck_selection_authored_network;
    auto Run = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Run->Dedicated))
    { AddError(TEXT("unsupported Selection topology")); return false; }
    Run->BaseMode = FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    Run->Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    Run->StateClass = FSoftClassPath(StatePath).TryLoadClass<AGameStateBase>();
    Run->PCClass = FSoftClassPath(PCPath).TryLoadClass<APlayerController>();
    Run->PawnClass = FSoftClassPath(PawnPath).TryLoadClass<APawn>();
    Run->MarkerClass = FSoftClassPath(MarkerPath).TryLoadClass<AActor>();
    Run->WallClass = FSoftClassPath(WallPath).TryLoadClass<AActor>();
    Run->SnapshotProperty = Field<FStructProperty>(Run->StateClass, TEXT("Snapshot"));
    Run->CompositionProperty = Field<FObjectProperty>(Run->StateClass,
        TEXT("_Composition"));
    if (!TestTrue(TEXT("authored Selection classes and reflected wire/receipt"),
            Run->BaseMode && Run->Mode && Run->StateClass && Run->PCClass &&
            Run->PawnClass && Run->MarkerClass && Run->WallClass &&
            Run->SnapshotProperty && Run->CompositionProperty &&
            Run->SnapshotProperty->Struct == FCkSelectionGym_WireSnapshot::StaticStruct() &&
            Run->CompositionProperty->PropertyClass ==
                UCkSelectionGym_SourceComposition::StaticClass() &&
            Run->PCClass->FindFunctionByName(FName(TEXT("Request_ControlActivated"))) &&
            Run->PCClass->FindFunctionByName(FName(TEXT("Get_IsLocalPresentationReady"))) &&
            Field<FStructProperty>(Run->PCClass, TEXT("LastControlAck"))))
    { return false; }
    const auto* Settings = GetDefault<UCkGym_StartupSettings>();
    Run->Startup = Settings->StartupMode;
    Run->LastGym = Settings->LastGymName;
    Run->Recents = Settings->RecentGymNames;
    Run->SettingsCaptured = true;
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode =
        ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        Worlds, EntryMap, Parameters, Run->BaseMode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(Worlds, 60));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]()
        {
            auto* W = ck::auto_test::net::Get_ServerWorld();
            auto* GI = IsValid(W) ? W->GetGameInstance() : nullptr;
            auto* Registry = IsValid(GI)
                ? GI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
            return IsValid(Registry) &&
                Registry->Find_GymIndexByName(TEXT("Selection")) != INDEX_NONE;
        }), 30, TEXT("real Selection gym registry entry available")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W)
        {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owner requests real Selection gym travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::
                    TryRequest_TravelToGymFromPlayer(PC, TEXT("Selection")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { return Context(*Run); }), 60,
        TEXT("exact Selection worlds, two remote owners and listen host converge")));
    ADD_LATENT_AUTOMATION_COMMAND(FDriver(this, Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FRestore(Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]()
        {
            if (Run->Failed || !Run->Captured ||
                ck::auto_test::net::Get_ServerWorld() != nullptr ||
                ck::auto_test::net::Get_NumClientWorlds() != 0) { return false; }
            for (const auto& H : Run->WorldRoot)
            { if (Allocated(H)) { return false; } }
            for (const auto& H : Run->SourceDirectRoots)
            { if (Allocated(H)) { return false; } }
            for (const auto& H : Run->MirrorRoot)
            { if (Allocated(H)) { return false; } }
            return true;
        }), TEXT("Selection authored worlds close after all control phases"), true));
    return true;
}

#endif
