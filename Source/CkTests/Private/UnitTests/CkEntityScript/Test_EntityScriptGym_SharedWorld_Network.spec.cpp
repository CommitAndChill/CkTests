#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEntityScriptGym_Runtime.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkDynamic/CkDynamic_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"

#include <Engine/GameInstance.h>
#include <Engine/World.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/PlayerController.h>
#include <StructUtils/InstancedStruct.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>
#include <UObject/UObjectIterator.h>
#include <UObject/StrongObjectPtr.h>
#include <UObject/WeakObjectPtr.h>
#include <Misc/OutputDevice.h>
#include <Misc/ScopeLock.h>
#include <Misc/OutputDeviceRedirector.h>

namespace ck_entity_script_gym_shared_test
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_EntityScriptGym_Spawn_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_EntityScriptGym_Spawn_GameState");
    constexpr auto PCPath = TEXT("/Script/Angelscript.Ck_EntityScriptGym_Spawn_PlayerController");
    constexpr auto PlainPath = TEXT("/Script/Angelscript.Ck_EntityScript_EntityScriptGym_Spawn");
    constexpr auto RepPath = TEXT("/Script/Angelscript.Ck_EntityScript_EntityScriptGym_SpawnReplicated");
    constexpr auto RootPath = TEXT("/Script/Angelscript.Ck_EntityScript_EntityScriptGym_WorldRoot");
    constexpr auto PresentationPath = TEXT("/Script/Angelscript.Ck_EntityScript_EntityScriptGym_Presentation");
    constexpr auto ParamsPath = TEXT("/Script/Angelscript.EntityScriptGym_SpawnParams");
    constexpr auto AnchorsPath = TEXT("/Script/Angelscript.CkGym_Station_Anchors");
    const FName Tags[2] = {FName(TEXT("Gym.EntityScript.Spawn")),
        FName(TEXT("Gym.EntityScript.SpawnReplicated"))};
    const FName ScriptTags[2] = {FName(TEXT("TAG_EntityScriptGym_Spawn")),
        FName(TEXT("TAG_EntityScriptGym_SpawnReplicated"))};
    const FName ProjectionTags[2] = {FName(TEXT("TAG_EntityScriptGym_PresentationPlain")),
        FName(TEXT("TAG_EntityScriptGym_PresentationReplicated"))};

    class FRemoteDestroyWarningCounter final : public FOutputDevice
    {
    public:
        auto Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity,
                       const FName& Category) -> void override
        {
            if (!Message || Category != FName(TEXT("LogIris")) ||
                Verbosity > ELogVerbosity::Warning ||
                !FString(Message).Contains(
                    TEXT("Unable to create RPC for function Request_TriggerDestroyAssociatedEntity")))
            { return; }
            FScopeLock Guard(&Mutex);
            ++Count;
        }

        auto Attach() -> bool
        {
            if (!GLog) { return false; }
            if (bAttached) { return true; }
            GLog->AddOutputDevice(this);
            bAttached = true;
            return true;
        }

        auto Detach() -> int32
        {
            if (bAttached && GLog) { GLog->RemoveOutputDevice(this); }
            bAttached = false;
            FScopeLock Guard(&Mutex);
            return Count;
        }

        ~FRemoteDestroyWarningCounter() override { Detach(); }

    private:
        FCriticalSection Mutex;
        int32 Count = 0;
        bool bAttached = false;
    };

    struct FFixture
    {
        struct FAction
        {
            FCk_Handle OldSelected;
            FCk_Handle OldSibling;
            FCk_Handle OldReplica[2];
            FCk_Handle OldClientRoot[2];
            TStrongObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep> OldReplicaDriver[2];
            TWeakObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep> OldReplicaWeak[2];
            TStrongObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep> RootDriver[2];
            FRemoteDestroyWarningCounter Warnings;
            int32 BeforeSequence = 0;
        };
        UClass* Mode = nullptr;
        UClass* StateClass = nullptr;
        UClass* PCClass = nullptr;
        UClass* PlainClass = nullptr;
        UClass* RepClass = nullptr;
        UClass* RootClass = nullptr;
        UClass* PresentationClass = nullptr;
        UScriptStruct* ParamsType = nullptr;
        UScriptStruct* AnchorsType = nullptr;
        FStructProperty* Snapshot = nullptr;
        FIntProperty* Generation = nullptr;
        FIntProperty* Sequence = nullptr;
        FBoolProperty* Ready = nullptr;
        FBoolProperty* Failed = nullptr;
        FIntProperty* Epoch[2]{};
        FStructProperty* Sample[2]{};
        struct FValues
        {
            FNameProperty* Name = nullptr;
            FIntProperty* Number = nullptr;
            FNumericProperty* Float = nullptr;
            FStructProperty* Transform = nullptr;
            bool IsValid() const { return Name && Number && Float && Float->IsFloatingPoint() &&
                Transform && Transform->Struct == TBaseStructure<FTransform>::Get(); }
        } SampleValues, ParamValues;
        FBoolProperty* NamePass = nullptr;
        FBoolProperty* IntPass = nullptr;
        FBoolProperty* FloatPass = nullptr;
        FStructProperty* AnchorPanel = nullptr;
        FStructProperty* ServerHandle[3]{};
        FIntProperty* ServerCount = nullptr;
        FIntProperty* LocalGeneration = nullptr;
        FBoolProperty* LocalReady = nullptr;
        FBoolProperty* LocalFailed = nullptr;
        FIntProperty* LastSerial = nullptr;
        FIntProperty* AttemptSerial = nullptr;
        FIntProperty* Result = nullptr;
        UFunction* Rpc = nullptr;
        FIntProperty* RpcSerial = nullptr;
        FIntProperty* RpcBranch = nullptr;
        int32 BaseGeneration = 0;
        int32 BaseSequence = 0;
        FCk_Handle ServerRoot;
        FCk_Handle ServerBranch[2];
        FCk_Handle ClientReplica[2];
        FCk_Handle ClientPresentation[2][2];
        ECkGym_StartupMode OldStartup = ECkGym_StartupMode::Cycler;
        FString InitialReadinessTuple[3];
        FString OldLast;
        TArray<FString> OldRecents;

        void CaptureSettings()
        {
            const auto* S = GetDefault<UCkGym_StartupSettings>();
            OldStartup = S->StartupMode; OldLast = S->LastGymName;
            OldRecents = S->RecentGymNames;
        }
        void RestoreSettings() const
        {
            auto* S = GetMutableDefault<UCkGym_StartupSettings>();
            S->StartupMode = OldStartup; S->LastGymName = OldLast;
            S->RecentGymNames = OldRecents; S->SaveConfig();
        }
    };

    auto Live(const FCk_Handle& Handle) -> bool
    {
        return ck::IsValid(Handle) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                Handle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    auto State(UWorld* World, const FFixture& F) -> AGameStateBase*
    {
        auto* S = IsValid(World) ? World->GetGameState() : nullptr;
        return IsValid(S) && S->IsA(F.StateClass) ? S : nullptr;
    }

    auto Snapshot(AGameStateBase* S, const FFixture& F) -> const void*
    { return IsValid(S) ? F.Snapshot->ContainerPtrToValuePtr<void>(S) : nullptr; }

    auto Int(const FIntProperty* P, const void* Owner) -> int32
    { return Owner ? P->GetPropertyValue_InContainer(Owner) : INDEX_NONE; }

    auto Ready(AGameStateBase* S, const FFixture& F, int32 Gen,
               int32 PlainEpoch, int32 RepEpoch) -> bool
    {
        const auto* Snap = Snapshot(S, F);
        return Snap && Int(F.Generation, Snap) == Gen && Int(F.Sequence, Snap) > 0 &&
            F.Ready->GetPropertyValue_InContainer(Snap) &&
            !F.Failed->GetPropertyValue_InContainer(Snap) &&
            Int(F.Epoch[0], Snap) == PlainEpoch && Int(F.Epoch[1], Snap) == RepEpoch;
    }

    auto Handle(AGameStateBase* S, FStructProperty* P) -> FCk_Handle
    { return IsValid(S) ? *P->ContainerPtrToValuePtr<FCk_Handle>(S) : FCk_Handle{}; }

    auto Direct(const FCk_Handle& Child, const FCk_Handle& Owner) -> bool
    {
        return Live(Child) && Live(Owner) &&
            UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child) &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Owner;
    }

    auto ClassIs(const FCk_Handle& H, UClass* Expected) -> bool
    {
        if (!Live(H) || !UCk_Utils_EntityScript_UE::Has(H) || !IsValid(Expected)) { return false; }
        const auto Typed = UCk_Utils_EntityScript_UE::CastChecked(H);
        return UCk_Utils_EntityScript_UE::Get_ScriptClass(Typed).Get() == Expected;
    }

    auto Tagged(UWorld* World, const FName Tag) -> TArray<FCk_Handle>
    {
        if (!IsValid(World)) { return {}; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(World);
        return Live(Root) ? UCk_Utils_EntityTag_UE::ForEach_Entity(Root, Tag) : TArray<FCk_Handle>{};
    }

    auto OneStation(UWorld* World, int32 Index, const FFixture& F) -> FCk_Handle
    {
        if (!IsValid(World) || Index < 0 || Index > 1) { return {}; }
        const auto Matches = Tagged(World, Tags[Index]);
        return Matches.Num() == 1 && Live(Matches[0]) &&
            UCk_Utils_DynamicFragment_UE::Has_Fragment(Matches[0], F.AnchorsType)
            ? Matches[0] : FCk_Handle{};
    }

    auto ActorRoot(AGameStateBase* S) -> FCk_Handle
    { return IsValid(S) ? UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(S) : FCk_Handle{}; }

    auto OneTaggedDirect(const FCk_Handle& Parent, const FName Tag,
                         UClass* Expected) -> FCk_Handle
    {
        if (!Live(Parent)) { return {}; }
        const auto Matches = UCk_Utils_EntityTag_UE::ForEach_Entity(Parent, Tag);
        return Matches.Num() == 1 && Direct(Matches[0], Parent) && ClassIs(Matches[0], Expected)
            ? Matches[0] : FCk_Handle{};
    }

    auto ReadDynamic(const FCk_Handle& H, UScriptStruct* Type) -> const void*
    {
        if (!Live(H) || !IsValid(Type)) { return nullptr; }
        if (!UCk_Utils_DynamicFragment_UE::Has_Fragment(H, Type)) { return nullptr; }
        const auto* Value = UCk_Utils_DynamicFragment_UE::TryGet_Fragment_TypeUnsafe(H, Type);
        return Value && Value->GetScriptStruct() == Type ? Value->GetMemory() : nullptr;
    }

    auto SampleMatches(const void* Owner, const FFixture::FValues& V,
                       const FVector& ExpectedCenter) -> bool
    {
        if (!Owner) { return false; }
        const auto Name = V.Name->GetPropertyValue_InContainer(Owner);
        const auto Number = Int(V.Number, Owner);
        const auto Float = V.Float->GetFloatingPointPropertyValue(
            V.Float->ContainerPtrToValuePtr<void>(Owner));
        const auto* Xf = V.Transform->ContainerPtrToValuePtr<FTransform>(Owner);
        return Name == FName(TEXT("GymTestEntity")) && Number == 42 &&
            FMath::IsFinite(Float) && FMath::IsNearlyEqual(Float, 3.14, 0.001) &&
            Xf && !Xf->ContainsNaN() && Xf->GetLocation().Equals(ExpectedCenter, 0.01) &&
            Xf->GetRotation().Equals(FRotator(0, 180, 0).Quaternion(), 0.001);
    }

    auto AuthorityReplicationPublished(UWorld* World, const FCk_Handle& Root,
                                       const FCk_Handle& Child, const FFixture& F,
                                       const FVector& ExpectedCenter) -> bool
    {
        if (!IsValid(World) || !Live(Root) || !Live(Child) || !IsValid(F.RepClass) ||
            !IsValid(F.ParamsType) || !F.ParamValues.IsValid()) { return false; }
        if (!Direct(Child, Root) || !ClassIs(Root, F.RootClass) || !ClassIs(Child, F.RepClass)) { return false; }
        if (Child.Has<ck::FRequest_EntityScript_Replicate>() ||
            Child.Has<ck::FTag_EntityScript_FinishConstruction>() ||
            !Child.Has<ck::FTag_EntityScript_HasBegunPlay>()) { return false; }
        if (!UCk_Utils_EntityReplicationDriver_UE::Has(Root) ||
            !UCk_Utils_EntityReplicationDriver_UE::Has(Child)) { return false; }
        auto* RootDriver = Root.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
        auto* Driver = Child.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
        if (!IsValid(RootDriver) || !IsValid(Driver)) { return false; }
        if (RootDriver->GetWorld() != World || Driver->GetWorld() != World ||
            RootDriver->Get_AssociatedEntity() != Root || Driver->Get_AssociatedEntity() != Child) { return false; }
        const auto& Wire = Driver->Get_ReplicationData_EntityScript();
        auto* WireClass = Wire.Get_EntityScriptClass().Get();
        auto* WireOwner = Wire.Get_OwningEntityDriver().Get();
        if (!IsValid(WireClass) || !IsValid(WireOwner)) { return false; }
        if (WireClass != F.RepClass || WireOwner != RootDriver) { return false; }
        const auto& Spawn = Wire.Get_SpawnParams();
        if (!Spawn.IsValid() || !IsValid(Spawn.GetScriptStruct()) || Spawn.GetScriptStruct() != F.ParamsType)
        { return false; }
        return SampleMatches(Spawn.GetMemory(), F.ParamValues, ExpectedCenter);
    }

    auto CheckBranch(UWorld* World, AGameStateBase* S, const FFixture& F,
                     int32 Index, bool Authority, FCk_Handle* Out = nullptr) -> bool
    {
        const auto Station = OneStation(World, Index, F);
        if (!Live(Station)) { return false; }
        const auto* Anchors = ReadDynamic(Station, F.AnchorsType);
        if (!Anchors) { return false; }
        const auto* Panel = F.AnchorPanel->ContainerPtrToValuePtr<FVector>(Anchors);
        if (!Panel || Panel->ContainsNaN()) { return false; }
        const auto Root = ActorRoot(S);
        const auto Owner = Index == 0 ? Station : Root;
        const auto Child = OneTaggedDirect(Owner, ScriptTags[Index],
            Index == 0 ? F.PlainClass : F.RepClass);
        if (Index == 0 && !Authority &&
            (UCk_Utils_EntityTag_UE::ForEach_Entity(Station, ScriptTags[0]).Num() != 0 ||
             Tagged(World, ScriptTags[0]).Num() != 0))
        { return false; }
        if (Authority ? !Live(Child) : (Index == 0 ? Live(Child) : !Live(Child)))
        { return false; }
        if (Index == 0 && !Authority)
        { if (Out) { *Out = {}; } return true; }
        if (Index == 1 && (!Live(Root) || !ClassIs(Root, F.RootClass) ||
            !UCk_Utils_EntityReplicationDriver_UE::Has(Root)))
        { return false; }
        if (UCk_Utils_Net_UE::Get_EntityReplication(Child) !=
            (Index == 0 ? ECk_Replication::DoesNotReplicate : ECk_Replication::Replicates))
        { return false; }
        if (Index == 1 && Authority && !AuthorityReplicationPublished(World, Root, Child, F, *Panel))
        { return false; }
        const auto* Received = ReadDynamic(Child, F.ParamsType);
        const auto* Snap = Snapshot(S, F);
        const auto* Sample = Snap ? F.Sample[Index]->ContainerPtrToValuePtr<void>(Snap) : nullptr;
        if (!SampleMatches(Received, F.ParamValues, *Panel) ||
            !SampleMatches(Sample, F.SampleValues, *Panel) ||
            !F.NamePass->GetPropertyValue_InContainer(Sample) ||
            !F.IntPass->GetPropertyValue_InContainer(Sample) ||
            !F.FloatPass->GetPropertyValue_InContainer(Sample))
        { return false; }
        if (Out) { *Out = Child; }
        return true;
    }

    auto CheckPresentation(UWorld* World, const FFixture& F, bool Dedicated,
                           FCk_Handle Out[2] = nullptr) -> bool
    {
        for (int32 I = 0; I < 2; ++I)
        {
            const auto Station = OneStation(World, I, F);
            if (!Live(Station)) { return false; }
            const auto Projection = OneTaggedDirect(Station, ProjectionTags[I], F.PresentationClass);
            if (Dedicated ? Live(Projection) : !Live(Projection)) { return false; }
            if (Out) { Out[I] = Projection; }
        }
        return true;
    }

    auto WorldReady(UWorld* World, const FFixture& F, int32 Gen,
                    int32 PlainEpoch, int32 RepEpoch, bool Authority, bool Dedicated,
                    FCk_Handle* OutPlain = nullptr, FCk_Handle* OutRep = nullptr,
                    FCk_Handle OutPresentation[2] = nullptr) -> bool
    {
        auto* S = State(World, F);
        if (!Ready(S, F, Gen, PlainEpoch, RepEpoch) ||
            Int(F.LocalGeneration, S) != (Dedicated ? 0 : Gen) ||
            (Dedicated ? F.LocalReady->GetPropertyValue_InContainer(S) :
                !F.LocalReady->GetPropertyValue_InContainer(S)) ||
            F.LocalFailed->GetPropertyValue_InContainer(S) ||
            !CheckPresentation(World, F, Dedicated, OutPresentation)) { return false; }
        FCk_Handle Plain, Rep;
        if (!CheckBranch(World, S, F, 0, Authority, &Plain) ||
            !CheckBranch(World, S, F, 1, Authority, &Rep)) { return false; }
        if (Authority)
        {
            if (Int(F.ServerCount, S) != 2 ||
                Handle(S, F.ServerHandle[0]) != Plain ||
                Handle(S, F.ServerHandle[1]) != Rep ||
                Handle(S, F.ServerHandle[2]) != ActorRoot(S)) { return false; }
        }
        if (OutPlain) { *OutPlain = Plain; }
        if (OutRep) { *OutRep = Rep; }
        return true;
    }

    // Read-only pipeline evidence: never advance construction, replication, or callbacks.
    auto ConstructionPipelineTuple(const FCk_Handle& H, const FCk_Handle& Root,
                                   UWorld* World, const FFixture& F) -> FString
    {
        if (!Live(H)) { return TEXT("pipeline={live:0}"); }
        const bool HasNet = H.Has<ck::FFragment_Net_Params>();
        const auto* Net = HasNet ? &H.Get<ck::FFragment_Net_Params>().Get_ConnectionSettings() : nullptr;
        auto* Script = H.Has<ck::FFragment_EntityScript>() ?
            H.Get<ck::FFragment_EntityScript>().Get_Script().Get() : nullptr;
        const bool ScriptLive = IsValid(Script);
        auto* Actor = UCk_Utils_OwningActor_UE::Has(H) ?
            UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(H) : nullptr;
        const bool ActorLive = IsValid(Actor);
        FString Result = FString::Printf(
            TEXT("pipeline={netParams:%d netMode:%d netRole:%d hostTag:%d clientTag:%d authorityTag:%d ")
            TEXT("actor:%s actorWorld:%d actorNetMode:%d actorRole:%d actorRemoteRole:%d ")
            TEXT("script:%s effectiveReplication:%d instancing:%d continue:%d finish:%d begin:%d begun:%d ended:%d retry:%d constructedFrame:%d"),
            HasNet, Net ? static_cast<int32>(Net->Get_NetMode()) : INDEX_NONE,
            Net ? static_cast<int32>(Net->Get_NetRole()) : INDEX_NONE,
            H.Has<ck::FTag_NetMode_IsHost>(), H.Has<ck::FTag_NetMode_IsClient>(), H.Has<ck::FTag_HasAuthority>(),
            ActorLive ? *Actor->GetPathName() : TEXT("missing"), ActorLive && Actor->GetWorld() == World,
            ActorLive ? static_cast<int32>(Actor->GetNetMode()) : INDEX_NONE,
            ActorLive ? static_cast<int32>(Actor->GetLocalRole()) : INDEX_NONE,
            ActorLive ? static_cast<int32>(Actor->GetRemoteRole()) : INDEX_NONE,
            ScriptLive ? *Script->GetClass()->GetPathName() : TEXT("missing"),
            ScriptLive ? static_cast<int32>(Script->Get_EffectiveReplication()) : INDEX_NONE,
            ScriptLive ? static_cast<int32>(Script->Get_InstancingPolicy()) : INDEX_NONE,
            H.Has<ck::FTag_EntityScript_ContinueConstruction>(), H.Has<ck::FTag_EntityScript_FinishConstruction>(),
            H.Has<ck::FTag_EntityScript_BeginPlay>(), H.Has<ck::FTag_EntityScript_HasBegunPlay>(),
            H.Has<ck::FTag_EntityScript_HasEndedPlay>(), H.Has<ck::FTag_EntityScript_PendingReplicationRetry>(),
            H.Has<ck::FTag_EntityScript_ConstructedThisFrame>());
        // Observe the actual owner chain without recursive helpers on malformed/circular input.
        TArray<FCk_Handle> Seen;
        auto Cursor = H;
        bool ChainHost = false;
        bool ChainComplete = false;
        while (Live(Cursor) && Seen.Num() < 32 && !Seen.Contains(Cursor))
        {
            Seen.Add(Cursor);
            if (Cursor.Has<ck::FTag_NetMode_IsHost>()) { ChainHost = true; ChainComplete = true; break; }
            if (!UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Cursor)) { ChainComplete = true; break; }
            Cursor = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Cursor);
        }
        Result += FString::Printf(TEXT(" ownerChainHost:%d ownerChainComplete:%d ownerChainDepth:%d replicateRequest:%d"),
            ChainHost, ChainComplete, Seen.Num(), H.Has<ck::FRequest_EntityScript_Replicate>());
        if (H.Has<ck::FRequest_EntityScript_Replicate>())
        {
            const auto& Request = H.Get<ck::FRequest_EntityScript_Replicate>();
            const auto Owner = Request.Get_Owner();
            auto* RequestScript = Request.Get_Script().Get();
            const bool RequestScriptLive = IsValid(RequestScript);
            const auto& Spawn = Request.Get_SpawnParams();
            auto* SpawnType = Spawn.GetScriptStruct();
            const bool SpawnValid = Spawn.IsValid() && IsValid(SpawnType);
            const auto& Override = Request.Get_ContextOwnerOverride();
            Result += FString::Printf(
                TEXT(" request={ownerLive:%d ownerRoot:%d ownerExactRoot:%d script:%s scriptLive:%d scriptSame:%d scriptExact:%d ")
                TEXT("spawnValid:%d spawnType:%s spawnExact:%d contextOverride:%d contextOverrideLive:%d}"),
                Live(Owner), Live(Owner) && Live(Root) && Owner == Root, ClassIs(Owner, F.RootClass),
                RequestScriptLive ? *RequestScript->GetClass()->GetPathName() : TEXT("missing"),
                RequestScriptLive, RequestScriptLive && ScriptLive && RequestScript == Script,
                RequestScriptLive && IsValid(F.RepClass) && RequestScript->GetClass() == F.RepClass,
                SpawnValid, SpawnValid ? *SpawnType->GetPathName() : TEXT("missing"),
                SpawnValid && IsValid(F.ParamsType) && SpawnType == F.ParamsType,
                Override.IsSet(), Override.IsSet() && Live(Override.GetValue()));
        }
        return Result + TEXT("}");
    }

    // Observe real production entities only. This tuple never repairs or creates state.
    auto InitialReadinessTuple(UWorld* World, const FFixture& F, bool IsReady) -> FString
    {
        if (!IsValid(World)) { return TEXT("world=missing"); }
        auto* S = State(World, F);
        if (!IsValid(S))
        { return FString::Printf(TEXT("mode=%d state=missing"), static_cast<int32>(World->GetNetMode())); }
        const auto* Snap = Snapshot(S, F);
        if (!Snap) { return TEXT("snapshot=missing"); }
        const auto* ReasonProperty = FindFProperty<FStrProperty>(F.Snapshot->Struct.Get(), TEXT("FailureReason"));
        FString Reason = ReasonProperty ? ReasonProperty->GetPropertyValue_InContainer(Snap) : TEXT("unavailable");
        Reason.ReplaceInline(TEXT("\n"), TEXT(" "));
        Reason.ReplaceInline(TEXT("\r"), TEXT(" "));
        const auto Root = ActorRoot(S);
        const bool RootLive = Live(Root);
        FString Result = FString::Printf(
            TEXT("mode=%d ready=%d snapshot={gen:%d seq:%d ready:%d failed:%d reason:%s} ")
            TEXT("root={live:%d exact:%d driver:%d} local={gen:%d ready:%d failed:%d} ")
            TEXT("server={count:%d plainLive:%d repLive:%d rootLive:%d rootMatches:%d}"),
            static_cast<int32>(World->GetNetMode()), IsReady,
            Int(F.Generation, Snap), Int(F.Sequence, Snap),
            F.Ready->GetPropertyValue_InContainer(Snap), F.Failed->GetPropertyValue_InContainer(Snap), *Reason,
            RootLive, ClassIs(Root, F.RootClass),
            RootLive && UCk_Utils_EntityReplicationDriver_UE::Has(Root),
            Int(F.LocalGeneration, S), F.LocalReady->GetPropertyValue_InContainer(S),
            F.LocalFailed->GetPropertyValue_InContainer(S), Int(F.ServerCount, S),
            Live(Handle(S, F.ServerHandle[0])), Live(Handle(S, F.ServerHandle[1])),
            Live(Handle(S, F.ServerHandle[2])), Handle(S, F.ServerHandle[2]) == Root);
        Result += TEXT(" root") + ConstructionPipelineTuple(Root, Root, World, F);
        const auto Transient = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!Live(Transient)) { return Result + TEXT(" ecs=unavailable"); }
        for (int32 I = 0; I < 2; ++I)
        {
            const auto Stations = UCk_Utils_EntityTag_UE::ForEach_Entity(Transient, Tags[I]);
            const auto Station = Stations.Num() == 1 && Live(Stations[0]) &&
                UCk_Utils_DynamicFragment_UE::Has_Fragment(Stations[0], F.AnchorsType) ?
                Stations[0] : FCk_Handle{};
            const bool StationLive = Live(Station);
            const auto Presentations = StationLive ?
                UCk_Utils_EntityTag_UE::ForEach_Entity(Station, ProjectionTags[I]) : TArray<FCk_Handle>{};
            Result += FString::Printf(TEXT(" station%d={tags:%d admitted:%d presentationTags:%d}"),
                I, Stations.Num(), StationLive, Presentations.Num());
            for (const auto& Presentation : Presentations)
            {
                Result += FString::Printf(TEXT(" presentation%d={live:%d direct:%d exact:%d}"),
                    I, Live(Presentation), Direct(Presentation, Station), ClassIs(Presentation, F.PresentationClass));
            }
        }
        const auto Children = RootLive ?
            UCk_Utils_EntityTag_UE::ForEach_Entity(Root, ScriptTags[1]) : TArray<FCk_Handle>{};
        Result += FString::Printf(TEXT(" originalReplicatedTags=%d"), Children.Num());
        for (const auto& Child : Children)
        {
            const bool ChildLive = Live(Child);
            const bool HasNet = ChildLive && UCk_Utils_Net_UE::Has(Child);
            Result += TEXT(" child") + ConstructionPipelineTuple(Child, Root, World, F);
            const auto* Params = ReadDynamic(Child, F.ParamsType);
            Result += FString::Printf(TEXT(" child={live:%d exact:%d direct:%d net:%d driver:%d params:%d"),
                ChildLive, ClassIs(Child, F.RepClass), Direct(Child, Root),
                HasNet ? static_cast<int32>(UCk_Utils_Net_UE::Get_EntityReplication(Child)) : INDEX_NONE,
                ChildLive && UCk_Utils_EntityReplicationDriver_UE::Has(Child), Params != nullptr);
            if (Params)
            {
                const auto Name = F.ParamValues.Name->GetPropertyValue_InContainer(Params);
                const auto Number = Int(F.ParamValues.Number, Params);
                const auto Float = F.ParamValues.Float->GetFloatingPointPropertyValue(
                    F.ParamValues.Float->ContainerPtrToValuePtr<void>(Params));
                const auto* Transform = F.ParamValues.Transform->ContainerPtrToValuePtr<FTransform>(Params);
                const auto* Sample = F.Sample[1]->ContainerPtrToValuePtr<void>(Snap);
                const auto* ExpectedTransform = F.SampleValues.Transform->ContainerPtrToValuePtr<FTransform>(Sample);
                const auto ExpectedName = F.SampleValues.Name->GetPropertyValue_InContainer(Sample);
                const auto ExpectedFloat = F.SampleValues.Float->GetFloatingPointPropertyValue(
                    F.SampleValues.Float->ContainerPtrToValuePtr<void>(Sample));
                Result += FString::Printf(TEXT(" name:%s int:%d float:%.9g transform:%s ")
                    TEXT("snapshotName:%s snapshotInt:%d snapshotFloat:%.9g snapshotTransform:%s"),
                    *Name.ToString(), Number, Float, *Transform->ToString(),
                    *ExpectedName.ToString(), Int(F.SampleValues.Number, Sample), ExpectedFloat,
                    *ExpectedTransform->ToString());
            }
            Result += TEXT("}");
            Result += FString::Printf(TEXT(" childTransform=%d"),
                ChildLive && UCk_Utils_Transform_UE::Has(Child));
        }
        UCk_Fragment_EntityReplicationDriver_Rep* RootDriver = nullptr;
        if (RootLive && UCk_Utils_EntityReplicationDriver_UE::Has(Root))
        { RootDriver = Root.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get(); }
        int32 WorldDrivers = 0;
        TArray<FString> DriverTuples;
        for (TObjectIterator<UCk_Fragment_EntityReplicationDriver_Rep> It; It; ++It)
        {
            auto* Driver = *It;
            if (!IsValid(Driver) || Driver->IsTemplate() || Driver->GetWorld() != World) { continue; }
            ++WorldDrivers;
            const auto& Wire = Driver->Get_ReplicationData_EntityScript();
            auto* WireClass = Wire.Get_EntityScriptClass().Get();
            auto* OwnerDriver = Wire.Get_OwningEntityDriver().Get();
            const bool OwnerValid = IsValid(OwnerDriver);
            const auto Associated = Driver->Get_AssociatedEntity();
            const bool AssociatedLive = Live(Associated);
            const bool AssociatedExact = ClassIs(Associated, F.RepClass);
            const bool AssociatedDirect = Direct(Associated, Root);
            const bool OwnerRoot = OwnerValid && IsValid(RootDriver) && OwnerDriver == RootDriver &&
                OwnerDriver->GetWorld() == World && OwnerDriver->Get_AssociatedEntity() == Root;
            // Unknown relay drivers are counted only; print actual gym-child/data/owner candidates.
            if (WireClass != F.RepClass && !AssociatedExact && !AssociatedDirect &&
                !(OwnerRoot && Driver != RootDriver)) { continue; }
            const bool HasScript = AssociatedLive && UCk_Utils_EntityScript_UE::Has(Associated);
            auto* Script = HasScript ? UCk_Utils_EntityScript_UE::TryGet_EntityScript(Associated) : nullptr;
            const auto& Spawn = Wire.Get_SpawnParams();
            auto* SpawnType = Spawn.GetScriptStruct();
            const bool SpawnValid = Spawn.IsValid() && IsValid(SpawnType);
            const bool SpawnExact = SpawnValid && SpawnType == F.ParamsType;
            const auto& Objects = Wire.Get_ReplicatedObjectsData().Get_Objects();
            int32 Unresolved = 0;
            for (const auto& Object : Objects) { if (!Object.IsValid()) { ++Unresolved; } }
            FString Tuple = FString::Printf(
                TEXT(" driver={object:%s class:%s associated:%s live:%d exact:%d direct:%d boundDriver:%d ")
                TEXT("script:%s tag:%d transform:%d wireClass:%s owner:%s ownerWorld:%d ownerRoot:%d ")
                TEXT("spawnValid:%d spawnType:%s spawnExact:%d objects:%d unresolved:%d"),
                *Driver->GetPathName(), *Driver->GetClass()->GetPathName(),
                AssociatedLive ? *Associated.ToString() : TEXT("not-live"),
                AssociatedLive, AssociatedExact, AssociatedDirect,
                AssociatedLive && UCk_Utils_EntityReplicationDriver_UE::Has(Associated) &&
                    Associated.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get() == Driver,
                IsValid(Script) ? *Script->GetClass()->GetPathName() : TEXT("missing"),
                AssociatedLive && UCk_Utils_EntityTag_UE::Has(Associated, ScriptTags[1]),
                AssociatedLive && UCk_Utils_Transform_UE::Has(Associated),
                IsValid(WireClass) ? *WireClass->GetPathName() : TEXT("missing"),
                OwnerValid ? *OwnerDriver->GetPathName() : TEXT("missing"),
                OwnerValid && OwnerDriver->GetWorld() == World, OwnerRoot,
                SpawnValid, IsValid(SpawnType) ? *SpawnType->GetPathName() : TEXT("missing"),
                SpawnExact, Objects.Num(), Unresolved);
            if (SpawnExact && Spawn.GetMemory())
            {
                const auto* Memory = Spawn.GetMemory();
                const auto Name = F.ParamValues.Name->GetPropertyValue_InContainer(Memory);
                const auto Number = Int(F.ParamValues.Number, Memory);
                const auto Float = F.ParamValues.Float->GetFloatingPointPropertyValue(
                    F.ParamValues.Float->ContainerPtrToValuePtr<void>(Memory));
                const auto* Transform = F.ParamValues.Transform->ContainerPtrToValuePtr<FTransform>(Memory);
                const auto* Expected = F.Sample[1]->ContainerPtrToValuePtr<void>(Snap);
                const auto* ExpectedTransform = F.SampleValues.Transform->ContainerPtrToValuePtr<FTransform>(Expected);
                Tuple += FString::Printf(TEXT(" name:%s int:%d float:%.9g transform:%s matchesSnapshot:%d"),
                    *Name.ToString(), Number, Float, *Transform->ToString(),
                    Name == F.SampleValues.Name->GetPropertyValue_InContainer(Expected) &&
                    Number == Int(F.SampleValues.Number, Expected) &&
                    FMath::Abs(Float - F.SampleValues.Float->GetFloatingPointPropertyValue(
                        F.SampleValues.Float->ContainerPtrToValuePtr<void>(Expected))) < 0.001 &&
                    Transform->GetLocation().Equals(ExpectedTransform->GetLocation(), 0.001));
            }
            Tuple += TEXT("}");
            DriverTuples.Add(MoveTemp(Tuple));
        }
        DriverTuples.Sort();
        Result += FString::Printf(TEXT(" worldDrivers=%d originalDriverCandidates=%d"), WorldDrivers, DriverTuples.Num());
        for (const auto& Tuple : DriverTuples) { Result += Tuple; }
        return Result;
    }

    auto Invoke(APlayerController* PC, const FFixture& F, int32 Serial, int32 Branch) -> bool
    {
        if (!IsValid(PC) || !PC->IsA(F.PCClass)) { return false; }
        FStructOnScope Params(F.Rpc);
        auto* Memory = Params.GetStructMemory();
        F.RpcSerial->SetPropertyValue_InContainer(Memory, Serial);
        F.RpcBranch->SetPropertyValue_InContainer(Memory, Branch);
        PC->ProcessEvent(F.Rpc, Memory);
        return true;
    }

    auto Ack(UWorld* Client, const FFixture& F, int32 Attempt,
             int32 Accepted, int32 Result) -> bool
    {
        auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
        return IsValid(PC) && PC->IsA(F.PCClass) &&
            Int(F.AttemptSerial, PC) == Attempt && Int(F.LastSerial, PC) == Accepted &&
            Int(F.Result, PC) == Result;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkEntityScriptGym_SharedWorld_Network,
    "Ck.Gym.Network.EntityScript.SharedSpawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
        EAutomationTestFlags::EngineFilter)

bool FCkEntityScriptGym_SharedWorld_Network::RunTest(const FString& Parameters)
{
    using namespace ck_entity_script_gym_shared_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported EntityScript shared gym topology")); return false; }
    const auto F = MakeShared<FFixture>();
    F->Mode = FSoftClassPath{ModePath}.TryLoadClass<AGameModeBase>();
    F->StateClass = FSoftClassPath{StatePath}.TryLoadClass<AGameStateBase>();
    F->PCClass = FSoftClassPath{PCPath}.TryLoadClass<APlayerController>();
    F->PlainClass = FSoftClassPath{PlainPath}.TryLoadClass<UCk_EntityScript_UE>();
    F->RepClass = FSoftClassPath{RepPath}.TryLoadClass<UCk_EntityScript_UE>();
    F->RootClass = FSoftClassPath{RootPath}.TryLoadClass<UCk_EntityScript_UE>();
    F->PresentationClass = FSoftClassPath{PresentationPath}.TryLoadClass<UCk_EntityScript_UE>();
    F->ParamsType = FindObject<UScriptStruct>(nullptr, ParamsPath);
    F->AnchorsType = FindObject<UScriptStruct>(nullptr, AnchorsPath);
    bool Schema = TestNotNull(TEXT("EntityScript gym mode"), F->Mode) &&
        TestNotNull(TEXT("EntityScript gym state"), F->StateClass) &&
        TestNotNull(TEXT("EntityScript gym PC"), F->PCClass) &&
        TestNotNull(TEXT("original DNR class"), F->PlainClass) &&
        TestNotNull(TEXT("original replicated class"), F->RepClass) &&
        TestNotNull(TEXT("actor-linked world root class"), F->RootClass) &&
        TestNotNull(TEXT("pure local presentation class"), F->PresentationClass) &&
        TestNotNull(TEXT("original constructor fragment"), F->ParamsType) &&
        TestNotNull(TEXT("station anchors fragment"), F->AnchorsType);
    if (!Schema) { return false; }
    F->Snapshot = FindFProperty<FStructProperty>(F->StateClass, TEXT("Snapshot"));
    if (!TestNotNull(TEXT("replicated atomic snapshot"), F->Snapshot)) { return false; }
    auto* SnapType = F->Snapshot->Struct.Get();
    F->Generation = FindFProperty<FIntProperty>(SnapType, TEXT("Generation"));
    F->Sequence = FindFProperty<FIntProperty>(SnapType, TEXT("Sequence"));
    F->Ready = FindFProperty<FBoolProperty>(SnapType, TEXT("Ready"));
    F->Failed = FindFProperty<FBoolProperty>(SnapType, TEXT("Failed"));
    F->Epoch[0] = FindFProperty<FIntProperty>(SnapType, TEXT("PlainEpoch"));
    F->Epoch[1] = FindFProperty<FIntProperty>(SnapType, TEXT("ReplicatedEpoch"));
    F->Sample[0] = FindFProperty<FStructProperty>(SnapType, TEXT("Plain"));
    F->Sample[1] = FindFProperty<FStructProperty>(SnapType, TEXT("Replicated"));
    F->AnchorPanel = FindFProperty<FStructProperty>(F->AnchorsType, TEXT("PanelCenter"));
    F->ServerHandle[0] = FindFProperty<FStructProperty>(F->StateClass, TEXT("ServerPlain"));
    F->ServerHandle[1] = FindFProperty<FStructProperty>(F->StateClass, TEXT("ServerReplicated"));
    F->ServerHandle[2] = FindFProperty<FStructProperty>(F->StateClass, TEXT("ServerReplicatedRoot"));
    F->ServerCount = FindFProperty<FIntProperty>(F->StateClass, TEXT("ServerCohortCount"));
    F->LocalGeneration = FindFProperty<FIntProperty>(F->StateClass, TEXT("LocalPresentationGeneration"));
    F->LocalReady = FindFProperty<FBoolProperty>(F->StateClass, TEXT("LocalPresentationReady"));
    F->LocalFailed = FindFProperty<FBoolProperty>(F->StateClass, TEXT("LocalPresentationFailed"));
    F->LastSerial = FindFProperty<FIntProperty>(F->PCClass, TEXT("LastControlSerial"));
    F->AttemptSerial = FindFProperty<FIntProperty>(F->PCClass, TEXT("LastControlAttemptSerial"));
    F->Result = FindFProperty<FIntProperty>(F->PCClass, TEXT("LastControlResult"));
    F->Rpc = F->PCClass->FindFunctionByName(TEXT("Server_RequestEntityScriptSpawnControl"));
    Schema = TestNotNull(TEXT("both typed snapshot samples"), F->Sample[0]) &&
        TestNotNull(TEXT("replicated snapshot sample"), F->Sample[1]) &&
        TestNotNull(TEXT("owning control RPC"), F->Rpc);
    if (!Schema) { return false; }
    auto* SampleType = F->Sample[0]->Struct.Get();
    F->SampleValues.Name = FindFProperty<FNameProperty>(SampleType, TEXT("TestName"));
    F->SampleValues.Number = FindFProperty<FIntProperty>(SampleType, TEXT("TestInt"));
    F->SampleValues.Float = FindFProperty<FNumericProperty>(SampleType, TEXT("TestFloat"));
    F->SampleValues.Transform = FindFProperty<FStructProperty>(SampleType, TEXT("InitialTransform"));
    F->ParamValues.Name = FindFProperty<FNameProperty>(F->ParamsType, TEXT("TestName"));
    F->ParamValues.Number = FindFProperty<FIntProperty>(F->ParamsType, TEXT("TestInt"));
    F->ParamValues.Float = FindFProperty<FNumericProperty>(F->ParamsType, TEXT("TestFloat"));
    F->ParamValues.Transform = FindFProperty<FStructProperty>(F->ParamsType, TEXT("InitialTransform"));
    F->NamePass = FindFProperty<FBoolProperty>(SampleType, TEXT("NamePass"));
    F->IntPass = FindFProperty<FBoolProperty>(SampleType, TEXT("IntPass"));
    F->FloatPass = FindFProperty<FBoolProperty>(SampleType, TEXT("FloatPass"));
    F->RpcSerial = FindFProperty<FIntProperty>(F->Rpc, TEXT("InSerial"));
    F->RpcBranch = FindFProperty<FIntProperty>(F->Rpc, TEXT("InBranch"));
    Schema = F->Snapshot->HasAnyPropertyFlags(CPF_Net) &&
        F->Snapshot->Struct->GetName() == TEXT("CkEntityScriptGym_SpawnSnapshot") &&
        F->Sample[0]->Struct == F->Sample[1]->Struct &&
        F->Sample[0]->Struct->GetName() == TEXT("CkEntityScriptGym_SpawnSample") &&
        F->Generation && F->Sequence && F->Ready && F->Failed && F->Epoch[0] && F->Epoch[1] &&
        F->SampleValues.IsValid() && F->ParamValues.IsValid() &&
        F->NamePass && F->IntPass && F->FloatPass && F->AnchorPanel &&
        F->AnchorPanel->Struct == TBaseStructure<FVector>::Get() &&
        F->ServerHandle[0] && F->ServerHandle[1] && F->ServerHandle[2] &&
        F->ServerHandle[0]->Struct == FCk_Handle::StaticStruct() &&
        F->ServerHandle[1]->Struct == FCk_Handle::StaticStruct() &&
        F->ServerHandle[2]->Struct == FCk_Handle::StaticStruct() &&
        F->ServerCount && F->LocalGeneration && F->LocalReady && F->LocalFailed &&
        F->LastSerial && F->AttemptSerial && F->Result &&
        F->LastSerial->HasAnyPropertyFlags(CPF_Net) &&
        F->AttemptSerial->HasAnyPropertyFlags(CPF_Net) &&
        F->Result->HasAnyPropertyFlags(CPF_Net) &&
        F->RpcSerial && F->RpcBranch && F->Rpc->NumParms == 2 &&
        F->Rpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer);
    if (!TestTrue(TEXT("exact reflected EntityScript shared gym schema"), Schema)) { return false; }

    F->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, F->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F, Dedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            const auto* Snap = Snapshot(S, *F);
            const auto Gen = Snap ? Int(F->Generation, Snap) : 0;
            UWorld* Worlds[3] = {Server, ck::auto_test::net::Get_ClientWorld(0),
                ck::auto_test::net::Get_ClientWorld(1)};
            bool AllReady = Gen > 0;
            for (int32 I = 0; I < 3; ++I)
            {
                // Evaluate every actual world independently, including when a preceding world fails.
                const bool ThisReady = Gen > 0 &&
                    WorldReady(Worlds[I], *F, Gen, 1, 1, I == 0, I == 0 && Dedicated);
                const auto Tuple = InitialReadinessTuple(Worlds[I], *F, ThisReady);
                if (Tuple != F->InitialReadinessTuple[I])
                {
                    F->InitialReadinessTuple[I] = Tuple;
                    UE_LOG(LogTemp, Display, TEXT("[EntityScriptGymInitial] worldSlot=%d %s"), I, *Tuple);
                }
                AllReady = ThisReady && AllReady;
            }
            return AllReady &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1));
        }), 45.0, TEXT("one authority original pair and two actual remote replicas ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F, Dedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            const auto* Snap = Snapshot(S, *F);
            if (!TestTrue(TEXT("real server topology and initial shared cohort"),
                IsValid(Server) && IsValid(Server->GetAuthGameMode()) &&
                Server->GetAuthGameMode()->IsA(F->Mode) &&
                Server->GetNetMode() == (Dedicated ? NM_DedicatedServer : NM_ListenServer) &&
                Snap && Int(F->Generation, Snap) > 0 &&
                WorldReady(Server, *F, Int(F->Generation, Snap), 1, 1,
                    true, Dedicated, &F->ServerBranch[0], &F->ServerBranch[1]) &&
                WorldReady(ck::auto_test::net::Get_ClientWorld(0), *F,
                    Int(F->Generation, Snap), 1, 1, false, false,
                    nullptr, &F->ClientReplica[0], F->ClientPresentation[0]) &&
                WorldReady(ck::auto_test::net::Get_ClientWorld(1), *F,
                    Int(F->Generation, Snap), 1, 1, false, false,
                    nullptr, &F->ClientReplica[1], F->ClientPresentation[1])))
            { return false; }
            F->BaseGeneration = Int(F->Generation, Snap);
            F->BaseSequence = Int(F->Sequence, Snap);
            F->ServerRoot = ActorRoot(S);
            return TestTrue(TEXT("one actor-linked root and exact captured original identities"),
                Live(F->ServerRoot) &&
                UCk_Utils_Net_UE::Get_EntityReplication(F->ServerRoot) == ECk_Replication::Replicates &&
                UCk_Utils_EntityReplicationDriver_UE::Has(F->ServerRoot) &&
                Live(F->ServerBranch[0]) && Live(F->ServerBranch[1]) &&
                Live(F->ClientReplica[0]) && Live(F->ClientReplica[1]) &&
                F->ServerBranch[1] != F->ClientReplica[0] &&
                F->ServerBranch[1] != F->ClientReplica[1]) &&
                TestTrue(TEXT("actual authority replicate request drained before initial shared readiness"),
                    !F->ServerBranch[1].Has<ck::FRequest_EntityScript_Replicate>());
        }), TEXT("capture one shared world cohort and real replicated mirrors")));

    auto QueueRerun = [this, F, Dedicated](int32 Client, int32 Serial, int32 Branch,
                                           int32 PlainEpoch, int32 RepEpoch,
                                           const TCHAR* Label)
    {
        const auto A = MakeShared<FFixture::FAction>();
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, F, A, Branch](UWorld* World)
            {
                A->OldSelected = F->ServerBranch[Branch];
                A->OldSibling = F->ServerBranch[1 - Branch];
                A->OldReplica[0] = F->ClientReplica[0];
                A->OldReplica[1] = F->ClientReplica[1];
                A->BeforeSequence = F->BaseSequence;
                TestTrue(TEXT("rerun begins from captured complete cohort"),
                    Live(A->OldSelected) && Live(A->OldSibling) &&
                    Live(A->OldReplica[0]) && Live(A->OldReplica[1]) &&
                    A->BeforeSequence > 0 && IsValid(World));
            })));
        for (int32 I = 0; I < 2; ++I)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(I,
                FCk_NetAutoTest_ServerAction::CreateLambda([this, F, A, I](UWorld* World)
                {
                    auto* S = State(World, *F);
                    const auto Root = ActorRoot(S);
                    const auto Replica = A->OldReplica[I];
                    const bool bRepresented = Live(Root) && Live(Replica) &&
                        UCk_Utils_EntityReplicationDriver_UE::Has(Root) &&
                        UCk_Utils_EntityReplicationDriver_UE::Has(Replica);
                    if (!TestTrue(TEXT("capture actual remote replication drivers before rerun"),
                        bRepresented)) { return; }
                    auto* RootDriver = Root.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
                    auto* ReplicaDriver = Replica.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
                    if (!TestTrue(TEXT("captured remote drivers have exact live associations"),
                        IsValid(RootDriver) && IsValid(ReplicaDriver) &&
                        RootDriver->GetWorld() == World && ReplicaDriver->GetWorld() == World &&
                        RootDriver->Get_AssociatedEntity() == Root &&
                        ReplicaDriver->Get_AssociatedEntity() == Replica)) { return; }
                    A->OldClientRoot[I] = Root;
                    A->RootDriver[I].Reset(RootDriver);
                    A->OldReplicaDriver[I].Reset(ReplicaDriver);
                    A->OldReplicaWeak[I] = ReplicaDriver;
                })));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, A, Branch](UWorld*)
            {
                if (Branch == 1 &&
                    TestTrue(TEXT("both real remote drivers retained before replacement"),
                        A->OldReplicaDriver[0].IsValid() && A->OldReplicaDriver[1].IsValid()))
                { TestTrue(TEXT("Iris warning capture attached before replacement"),
                    A->Warnings.Attach()); }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Client,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, F, Serial, Branch](UWorld* World)
            {
                TestTrue(TEXT("owning remote invokes real EntityScript control RPC"),
                    Invoke(IsValid(World) ? World->GetFirstPlayerController() : nullptr,
                        *F, Serial, Branch));
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda(
                [F, A, Dedicated, Client, Serial, Branch, PlainEpoch, RepEpoch]() -> bool
            {
                auto* Server = ck::auto_test::net::Get_ServerWorld();
                auto* S = State(Server, *F);
                const auto* Snap = Snapshot(S, *F);
                if (!Snap || Int(F->Sequence, Snap) <= A->BeforeSequence ||
                    !Ack(ck::auto_test::net::Get_ClientWorld(Client), *F,
                        Serial, Serial, 1) ||
                    !WorldReady(Server, *F, F->BaseGeneration, PlainEpoch,
                        RepEpoch, true, Dedicated) ||
                    UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(A->OldSelected) ||
                    Handle(S, F->ServerHandle[Branch]) == A->OldSelected ||
                    Handle(S, F->ServerHandle[1 - Branch]) != A->OldSibling ||
                    Handle(S, F->ServerHandle[2]) != F->ServerRoot)
                { return false; }
                for (int32 I = 0; I < 2; ++I)
                {
                    auto* ClientWorld = ck::auto_test::net::Get_ClientWorld(I);
                    FCk_Handle Replica;
                    if (!WorldReady(ClientWorld, *F, F->BaseGeneration,
                            PlainEpoch, RepEpoch, false, false, nullptr, &Replica) ||
                        (Branch == 1 && (Replica == A->OldReplica[I] ||
                            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(A->OldReplica[I]))) ||
                        (Branch == 0 && Replica != A->OldReplica[I]))
                    { return false; }
                }
                return true;
            }), 25.0, Label));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
            FCk_NetAutoTest_Assertion::CreateLambda([this, F, A, PlainEpoch, RepEpoch,
                Branch]() -> bool
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
                auto* S = State(ck::auto_test::net::Get_ServerWorld(), *F);
                auto* Snap = Snapshot(S, *F);
                if (!TestTrue(TEXT("selected old branch retired, sibling retained, epoch advanced"),
                    Snap && !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(A->OldSelected) &&
                    Handle(S, F->ServerHandle[Branch]) != A->OldSelected &&
                    Handle(S, F->ServerHandle[1 - Branch]) == A->OldSibling &&
                    Int(F->Epoch[0], Snap) == PlainEpoch &&
                    Int(F->Epoch[1], Snap) == RepEpoch)) { return false; }
                F->ServerBranch[Branch] = Handle(S, F->ServerHandle[Branch]);
                F->BaseSequence = Int(F->Sequence, Snap);
                for (int32 I = 0; I < 2; ++I)
                {
                    auto* W = ck::auto_test::net::Get_ClientWorld(I);
                    auto* ClientState = State(W, *F);
                    const auto CurrentRoot = ActorRoot(ClientState);
                    const auto CurrentReplica = OneTaggedDirect(CurrentRoot,
                        ScriptTags[1], F->RepClass);
                    const bool bRepresented = Live(CurrentRoot) && Live(CurrentReplica) &&
                        UCk_Utils_EntityReplicationDriver_UE::Has(CurrentRoot) &&
                        UCk_Utils_EntityReplicationDriver_UE::Has(CurrentReplica);
                    if (!TestTrue(TEXT("replacement and survivor retain real remote drivers"),
                        bRepresented && CurrentRoot == A->OldClientRoot[I] &&
                        A->RootDriver[I].IsValid()))
                    { return false; }
                    auto* CurrentRootDriver =
                        CurrentRoot.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
                    auto* CurrentDriver =
                        CurrentReplica.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
                    if (!TestTrue(TEXT("remote root driver survived branch rerun"),
                        IsValid(CurrentRootDriver) && CurrentRootDriver == A->RootDriver[I].Get() &&
                        CurrentRootDriver->Get_AssociatedEntity() == CurrentRoot &&
                        CurrentRootDriver->GetWorld() == W)) { return false; }
                    if (Branch == 1)
                    {
                        if (!TestTrue(TEXT("old remote subobject explicitly retired and replaced"),
                            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(A->OldReplica[I]) &&
                            !A->OldReplicaWeak[I].IsValid() &&
                            IsValid(CurrentDriver) &&
                            TWeakObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>(CurrentDriver) !=
                                A->OldReplicaWeak[I] &&
                            CurrentDriver->Get_AssociatedEntity() == CurrentReplica &&
                            CurrentDriver->GetWorld() == W)) { return false; }
                    }
                    else if (!TestTrue(TEXT("unselected remote subobject instance survived"),
                        CurrentDriver == A->OldReplicaWeak[I].Get() &&
                        IsValid(CurrentDriver) && CurrentDriver->Get_AssociatedEntity() == CurrentReplica))
                    { return false; }
                    F->ClientReplica[I] = CurrentReplica;
                }
                return true;
            }), TEXT("capture committed rerun and retained sibling identity")));
        if (Branch == 1)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
                FCk_NetAutoTest_Assertion::CreateLambda([this, A]() -> bool
                {
                    return TestEqual(TEXT("no Iris RPC attempted on a retired remote driver"),
                        A->Warnings.Detach(), 0);
                }), TEXT("retired remote subobject emits no stale Iris RPC"), true));
        }
    };
    QueueRerun(0, 100, 0, 2, 1, TEXT("DNR-only rerun retires old root and keeps replica"));
    QueueRerun(1, 200, 1, 2, 2, TEXT("replica-only rerun retires old root and keeps DNR"));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* World)
        {
            auto* PC = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("fresh unsupported branch reached owning RPC"),
                Invoke(PC, *F, 201, 99));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() -> bool
        {
            return Ack(ck::auto_test::net::Get_ClientWorld(1), *F, 201, 200, 4);
        }), 10.0, TEXT("unsupported branch returns correlated rejection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, F](UWorld* World)
        {
            auto* PC = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("stale replay traversed owning RPC"),
                Invoke(PC, *F, 200, 1));
            TestTrue(TEXT("zero serial traversed owning RPC"),
                Invoke(PC, *F, 0, 0));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F, Dedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* S = State(Server, *F);
            const auto* Snap = Snapshot(S, *F);
            return TestTrue(TEXT("malformed and stale controls leave all identities, samples and watermarks"),
                Ack(ck::auto_test::net::Get_ClientWorld(1), *F, 201, 200, 4) &&
                Snap && Int(F->Sequence, Snap) == F->BaseSequence &&
                WorldReady(Server, *F, F->BaseGeneration, 2, 2, true, Dedicated) &&
                Handle(S, F->ServerHandle[0]) == F->ServerBranch[0] &&
                Handle(S, F->ServerHandle[1]) == F->ServerBranch[1] &&
                Handle(S, F->ServerHandle[2]) == F->ServerRoot &&
                WorldReady(ck::auto_test::net::Get_ClientWorld(0), *F,
                    F->BaseGeneration, 2, 2, false, false) &&
                WorldReady(ck::auto_test::net::Get_ClientWorld(1), *F,
                    F->BaseGeneration, 2, 2, false, false));
        }), TEXT("rejected controls cause zero shared mutation")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, F]() -> bool
        {
            F->RestoreSettings();
            return TestTrue(TEXT("EntityScript PIE worlds drained"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
        }), TEXT("restore startup settings and cleanly end three PIE worlds"), true));
    return true;
}

#endif
