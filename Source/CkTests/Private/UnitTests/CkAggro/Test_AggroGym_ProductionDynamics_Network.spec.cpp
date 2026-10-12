#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_NetworkRuntime.h"
#include "CkGymRegistry_Utils.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkAggro/CkAggro_Utils.h"
#include "CkAggro/CkAggro_Fragment.h"
#include "CkAggro/CkAggroTarget_Utils.h"
#include "CkAggro/CkAggroTarget_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTimer/CkTimer_Utils.h"
#include "CkEntityScriptGym_Runtime.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_aggro_gym_production_dynamics
{
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_AggroGym_GameState");
    constexpr auto ControllerPath = TEXT("/Script/Angelscript.Ck_AggroGym_PlayerController");
    constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");

    struct FObserved
    {
        int32 Generation = 0;
        int32 Sequence = 0;
        FVector Chase = FVector::ZeroVector;
        FVector Orbiter = FVector::ZeroVector;
        double ThreatSum = 0.0;
        int32 ActiveIndex = -1;
        TSharedPtr<FStructOnScope> Copy;
    };

    struct FFixture
    {
        ECkGym_StartupMode PriorStartup;
        FString PriorLast;
        TArray<FString> PriorRecent;
        UClass* StateClass = nullptr;
        UClass* ControllerClass = nullptr;
        FStructProperty* Snapshot = nullptr;
        FIntProperty* Generation = nullptr;
        FIntProperty* Sequence = nullptr;
        FBoolProperty* Ready = nullptr;
        FBoolProperty* Failed = nullptr;
        FStructProperty* ChaseGuard = nullptr;
        FStructProperty* PerceptionOrbiter = nullptr;
        FArrayProperty* ChaseThreats = nullptr;
        FIntProperty* ChaseActiveIndex = nullptr;
        FStructProperty* ServerChase = nullptr;
        FStructProperty* ServerPerception = nullptr;
        FStructProperty* ServerStress = nullptr;
        FIntProperty* ResetSerial = nullptr;
        FIntProperty* ResetResult = nullptr;
        FIntProperty* ResetArg = nullptr;
        UFunction* ResetRpc = nullptr;
        TArray<FObserved> History;
        TArray<FCk_Handle> OldKnown;
        TArray<FCk_Handle> OldTimers;
        FCk_Handle OldRoots[3];
        FCk_Handle ChaseGuardEntity;
        FCk_Handle PerceptionOrbiterEntity;
        TArray<FCk_Handle> StressGuardEntities;
        FVector FirstChase = FVector::ZeroVector;
        FVector FirstOrbiter = FVector::ZeroVector;
        float FirstThreat = 0.0f;
        int32 FirstSequence = 0;
        int32 FirstGeneration = 0;
        bool bSawChaseMove = false;
        bool bSawOrbiterMove = false;
        bool bSawThreatChange = false;
        bool bSawStressActive = false;
        bool bSawPublishedChase = false;
        bool bSawPublishedOrbiter = false;
        bool bSawPublishedThreat = false;
        bool bSawPublishedActive = false;
        bool bSawBothClients = false;
        bool bSawClientChase[2] = {false, false};
        bool bSawClientOrbiter[2] = {false, false};
        bool bSawClientThreat[2] = {false, false};
        bool bSawClientActive[2] = {false, false};

        FFixture()
        {
            const auto* Settings = GetDefault<UCkGym_StartupSettings>();
            PriorStartup = Settings->StartupMode;
            PriorLast = Settings->LastGymName;
            PriorRecent = Settings->RecentGymNames;
        }

        void Restore() const
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = PriorStartup;
            Settings->LastGymName = PriorLast;
            Settings->RecentGymNames = PriorRecent;
            Settings->SaveConfig();
        }
    };

    auto Registry(UWorld* World) -> UCkGym_Registry_Subsystem*
    {
        auto* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    auto State(UWorld* World, const FFixture& Fixture) -> AGameStateBase*
    {
        auto* Result = IsValid(World) ? World->GetGameState() : nullptr;
        return IsValid(Result) && Result->IsA(Fixture.StateClass) ? Result : nullptr;
    }

    auto Value(AGameStateBase* InState, const FFixture& Fixture) -> const void*
    {
        return IsValid(InState) ? Fixture.Snapshot->ContainerPtrToValuePtr<void>(InState) : nullptr;
    }

    auto Gen(AGameStateBase* InState, const FFixture& Fixture) -> int32
    {
        const auto* Data = Value(InState, Fixture);
        return Data ? Fixture.Generation->GetPropertyValue_InContainer(Data) : 0;
    }

    auto Seq(AGameStateBase* InState, const FFixture& Fixture) -> int32
    {
        const auto* Data = Value(InState, Fixture);
        return Data ? Fixture.Sequence->GetPropertyValue_InContainer(Data) : 0;
    }

    auto IsReady(AGameStateBase* InState, const FFixture& Fixture) -> bool
    {
        const auto* Data = Value(InState, Fixture);
        return Data && Fixture.Generation->GetPropertyValue_InContainer(Data) > 0 &&
            Fixture.Sequence->GetPropertyValue_InContainer(Data) > 0 &&
            Fixture.Ready->GetPropertyValue_InContainer(Data) &&
            !Fixture.Failed->GetPropertyValue_InContainer(Data);
    }

    auto Root(AGameStateBase* InState, FStructProperty* Property) -> FCk_Handle
    {
        if (!IsValid(InState) || !Property || Property->Struct != FCk_Handle::StaticStruct())
        { return {}; }
        const auto* Handle = Property->ContainerPtrToValuePtr<FCk_Handle>(InState);
        return Handle ? *Handle : FCk_Handle{};
    }

    auto CaptureBranch(const FCk_Handle& InRoot, TSet<FCk_Handle>& Seen,
                       int32& Aggro, int32& Targets, int32& Timers,
                       TArray<FCk_Handle>* CapturedTimers = nullptr, int32 Depth = 0) -> bool
    {
        if (Depth > 8 || Seen.Num() >= 512) { return false; }
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InRoot) ||
            Seen.Contains(InRoot)) { return false; }
        if (!ck::IsValid(InRoot) ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                InRoot, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
        { return false; }
        Seen.Add(InRoot);
        Aggro += UCk_Utils_Aggro_UE::Has(InRoot) ? 1 : 0;
        Targets += UCk_Utils_AggroTarget_UE::Has(InRoot) ? 1 : 0;
        if (UCk_Utils_Timer_UE::Has(InRoot))
        {
            ++Timers;
            if (CapturedTimers) { CapturedTimers->Add(InRoot); }
        }
        if (!InRoot.Has<ck::FFragment_LifetimeDependents>()) { return true; }
        const auto Registry = InRoot.Get_RegistryView().Get_RegistryHandle();
        for (const auto& Child : InRoot.Get<ck::FFragment_LifetimeDependents,
            ck::IsValid_Policy_IncludePendingKill>().Get_Entities())
        {
            if (Child == FCk_Handle{} ||
                Child.Get_RegistryView().Get_RegistryHandle() != Registry)
            { return false; }
            // LifetimeDependents retains historical entries after physical retirement.
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child))
            { continue; }
            if (!ck::IsValid(Child) ||
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Child, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) != InRoot ||
                !CaptureBranch(Child, Seen, Aggro, Targets, Timers, CapturedTimers, Depth + 1))
            { return false; }
        }
        return true;
    }

    auto HasNoClientAggro(UWorld* World) -> bool
    {
        if (!IsValid(World) || World->bIsTearingDown) { return false; }
        const auto Context = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!ck::IsValid(Context)) { return false; }
        int32 Owners = 0;
        int32 Targets = 0;
        const auto Registry = Context.Get_RegistryView();
        Registry.View<ck::FFragment_Aggro>().ForEach(
            [&Owners](FCk_Entity, const ck::FFragment_Aggro&) { ++Owners; });
        Registry.View<ck::FTag_AggroTarget>().ForEach(
            [&Targets](FCk_Entity) { ++Targets; });
        return Owners == 0 && Targets == 0;
    }

    auto FindRole(const FCk_Handle& RootHandle, bool WantAggro) -> FCk_Handle
    {
        if (!ck::IsValid(RootHandle)) { return {}; }
        FCk_Handle Found;
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(RootHandle))
        {
            if (!ck::IsValid(Child) || UCk_Utils_Timer_UE::Has(Child) ||
                !UCk_Utils_Transform_UE::Has(Child) ||
                UCk_Utils_Aggro_UE::Has(Child) != WantAggro) { continue; }
            if (ck::IsValid(Found)) { return {}; }
            Found = Child;
        }
        return Found;
    }

    auto ThreatSum(const FCk_Handle& ChaseRoot, const FCk_Handle& Guard) -> float
    {
        if (!ck::IsValid(ChaseRoot) || !ck::IsValid(Guard) ||
            !UCk_Utils_Aggro_UE::Has(Guard) ||
            !Guard.Has<ck::FFragment_Aggro>() ||
            !Guard.Has<ck::FFragment_Aggro_TargetMap>()) { return -1.0f; }
        auto Aggro = UCk_Utils_Aggro_UE::CastChecked(Guard);
        float Total = 0.0f;
        int32 Count = 0;
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(ChaseRoot))
        {
            if (!ck::IsValid(Child) || Child == Guard ||
                !UCk_Utils_Transform_UE::Has(Child)) { continue; }
            auto Target = UCk_Utils_Aggro_UE::TryGet_Target_ByTrackedEntity(Aggro, Child);
            if (!ck::IsValid(Target) || !UCk_Utils_AggroTarget_UE::Has(Target) ||
                !ck::UAggroTarget_TrackedEntity_Utils::Has(Target) ||
                !Target.Has<ck::FFragment_AggroTarget_TargetInfo>() ||
                !Target.Has<ck::FFragment_AggroTarget_Threat>() ||
                UCk_Utils_AggroTarget_UE::Get_AggroOwner(Target) != Guard ||
                UCk_Utils_AggroTarget_UE::Get_TrackedEntity(Target) != Child)
            { return -1.0f; }
            Total += UCk_Utils_AggroTarget_UE::Get_Threat(Target);
            ++Count;
        }
        return Count == 3 ? Total : -1.0f;
    }

    auto ReadPublishedDynamics(const void* Data, const FFixture& Fixture,
                               FObserved& Out) -> bool
    {
        if (!Data) { return false; }
        const auto* Chase = Fixture.ChaseGuard->ContainerPtrToValuePtr<FVector>(Data);
        const auto* Orbiter = Fixture.PerceptionOrbiter->ContainerPtrToValuePtr<FVector>(Data);
        if (!Chase || !Orbiter || Chase->ContainsNaN() || Orbiter->ContainsNaN())
        { return false; }
        const auto Index = Fixture.ChaseActiveIndex->GetPropertyValue_InContainer(Data);
        if (Index < -1 || Index >= 3) { return false; }
        const auto* Numeric = CastField<FNumericProperty>(Fixture.ChaseThreats->Inner);
        if (!Numeric || !Numeric->IsFloatingPoint()) { return false; }
        FScriptArrayHelper Threats(Fixture.ChaseThreats,
            Fixture.ChaseThreats->ContainerPtrToValuePtr<void>(Data));
        if (Threats.Num() != 3) { return false; }
        double Total = 0.0;
        for (int32 I = 0; I < Threats.Num(); ++I)
        {
            const auto Threat = Numeric->GetFloatingPointPropertyValue(Threats.GetRawPtr(I));
            if (!FMath::IsFinite(Threat) || Threat < 0.0) { return false; }
            Total += Threat;
        }
        Out.Chase = *Chase;
        Out.Orbiter = *Orbiter;
        Out.ThreatSum = Total;
        Out.ActiveIndex = Index;
        return true;
    }

    auto CapturePublication(FFixture& Fixture) -> bool
    {
        auto* ServerState = State(ck::auto_test::net::Get_ServerWorld(), Fixture);
        if (!IsReady(ServerState, Fixture) || Gen(ServerState, Fixture) != Fixture.FirstGeneration)
        { return false; }
        const auto ServerSeq = Seq(ServerState, Fixture);
        if (!Fixture.History.IsEmpty() && ServerSeq < Fixture.History.Last().Sequence)
        { return false; }
        if (Fixture.History.IsEmpty() || Fixture.History.Last().Sequence != ServerSeq)
        {
            FObserved Record;
            if (!ReadPublishedDynamics(Value(ServerState, Fixture), Fixture, Record))
            { return false; }
            Record.Generation = Fixture.FirstGeneration;
            Record.Sequence = ServerSeq;
            Record.Copy = MakeShared<FStructOnScope>(Fixture.Snapshot->Struct.Get());
            Fixture.Snapshot->Struct->CopyScriptStruct(
                Record.Copy->GetStructMemory(), Value(ServerState, Fixture));
            if (!Fixture.History.IsEmpty())
            {
                const auto& First = Fixture.History[0];
                Fixture.bSawPublishedChase |= FVector::Dist(Record.Chase, First.Chase) > 2.0;
                Fixture.bSawPublishedOrbiter |= FVector::Dist(Record.Orbiter, First.Orbiter) > 2.0;
                Fixture.bSawPublishedThreat |= FMath::Abs(Record.ThreatSum - First.ThreatSum) > 0.25;
                Fixture.bSawPublishedActive |= Record.ActiveIndex != First.ActiveIndex;
            }
            Fixture.History.Add(MoveTemp(Record));
        }
        for (int32 Index = 0; Index < 2; ++Index)
        {
            auto* Client = State(ck::auto_test::net::Get_ClientWorld(Index), Fixture);
            if (!IsReady(Client, Fixture) || Gen(Client, Fixture) != Fixture.FirstGeneration)
            { continue; }
            const auto ClientSeq = Seq(Client, Fixture);
            const auto* Match = Fixture.History.FindByPredicate(
                [ClientSeq](const FObserved& Item) { return Item.Sequence == ClientSeq; });
            if (!Match || !Match->Copy.IsValid() ||
                !Fixture.Snapshot->Struct->CompareScriptStruct(
                    Value(Client, Fixture), Match->Copy->GetStructMemory(), 0))
            { continue; }
            const auto& First = Fixture.History[0];
            if (ClientSeq < Fixture.FirstSequence + 2) { continue; }
            Fixture.bSawClientChase[Index] |= FVector::Dist(Match->Chase, First.Chase) > 2.0;
            Fixture.bSawClientOrbiter[Index] |= FVector::Dist(Match->Orbiter, First.Orbiter) > 2.0;
            Fixture.bSawClientThreat[Index] |= FMath::Abs(Match->ThreatSum - First.ThreatSum) > 0.25;
            Fixture.bSawClientActive[Index] |= Match->ActiveIndex != First.ActiveIndex;
        }
        Fixture.bSawBothClients =
            Fixture.bSawClientChase[0] && Fixture.bSawClientOrbiter[0] &&
            Fixture.bSawClientThreat[0] && Fixture.bSawClientActive[0] &&
            Fixture.bSawClientChase[1] && Fixture.bSawClientOrbiter[1] &&
            Fixture.bSawClientThreat[1] && Fixture.bSawClientActive[1];
        return true;
    }

    auto ObserveDynamics(FFixture& Fixture) -> bool
    {
        if (!CapturePublication(Fixture) || !ck::IsValid(Fixture.ChaseGuardEntity) ||
            !ck::IsValid(Fixture.PerceptionOrbiterEntity) ||
            !UCk_Utils_Transform_UE::Has(Fixture.ChaseGuardEntity) ||
            !UCk_Utils_Transform_UE::Has(Fixture.PerceptionOrbiterEntity))
        { return false; }
        const auto Chase = UCk_Utils_Transform_UE::Get_EntityCurrentLocation(
            UCk_Utils_Transform_UE::CastChecked(Fixture.ChaseGuardEntity));
        const auto Orbiter = UCk_Utils_Transform_UE::Get_EntityCurrentLocation(
            UCk_Utils_Transform_UE::CastChecked(Fixture.PerceptionOrbiterEntity));
        const auto Threat = ThreatSum(Fixture.OldRoots[0], Fixture.ChaseGuardEntity);
        if (Threat < 0.0f) { return false; }
        Fixture.bSawChaseMove |= FVector::Dist(Chase, Fixture.FirstChase) > 2.0f;
        Fixture.bSawOrbiterMove |= FVector::Dist(Orbiter, Fixture.FirstOrbiter) > 2.0f;
        Fixture.bSawThreatChange |= FMath::Abs(Threat - Fixture.FirstThreat) > 0.25f;
        if (Fixture.StressGuardEntities.Num() != 24) { return false; }
        for (const auto& Guard : Fixture.StressGuardEntities)
        {
            if (!ck::IsValid(Guard) || !UCk_Utils_Aggro_UE::Has(Guard) ||
                !Guard.Has<ck::FFragment_Aggro>() ||
                !Guard.Has<ck::FFragment_Aggro_TargetMap>()) { return false; }
            const auto Active = UCk_Utils_Aggro_UE::Get_ActiveTarget(
                UCk_Utils_Aggro_UE::CastChecked(Guard));
            if (!ck::IsValid(Active)) { continue; }
            if (!UCk_Utils_AggroTarget_UE::Has(Active) ||
                !ck::UAggroTarget_TrackedEntity_Utils::Has(Active) ||
                !Active.Has<ck::FFragment_AggroTarget_TargetInfo>() ||
                !Active.Has<ck::FFragment_AggroTarget_Threat>() ||
                UCk_Utils_AggroTarget_UE::Get_AggroOwner(Active) != Guard)
            { return false; }
            const auto Tracked = UCk_Utils_AggroTarget_UE::Get_TrackedEntity(Active);
            if (!ck::IsValid(Tracked) || !Fixture.OldKnown.Contains(Tracked) ||
                !UCk_Utils_Transform_UE::Has(Tracked) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Tracked) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Tracked) != Fixture.OldRoots[2])
            { return false; }
            Fixture.bSawStressActive = true;
        }
        if (Fixture.OldTimers.Num() != 3) { return false; }
        for (const auto& Timer : Fixture.OldTimers)
        {
            if (!ck::IsValid(Timer) || !UCk_Utils_Timer_UE::Has(Timer) ||
                UCk_Utils_Timer_UE::Get_CurrentState(
                    UCk_Utils_Timer_UE::CastChecked(Timer)) != ECk_Timer_State::Running)
            { return false; }
        }
        auto* ServerState = State(ck::auto_test::net::Get_ServerWorld(), Fixture);
        return Fixture.bSawChaseMove && Fixture.bSawOrbiterMove &&
            Fixture.bSawThreatChange && Fixture.bSawStressActive &&
            Fixture.bSawPublishedChase && Fixture.bSawPublishedOrbiter &&
            Fixture.bSawPublishedThreat && Fixture.bSawPublishedActive &&
            Fixture.bSawBothClients && Seq(ServerState, Fixture) >= Fixture.FirstSequence + 2;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkAggroGym_ProductionDynamics,
    "Ck.Aggro.Gym.ProductionDynamics.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkAggroGym_ProductionDynamics::RunTest(const FString& Parameters)
{
    using namespace ck_aggro_gym_production_dynamics;
    bool DedicatedServer = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, DedicatedServer))
    { AddError(TEXT("unsupported Aggro network topology")); return false; }
    auto* BaseMode = FSoftClassPath{BaseModePath}.TryLoadClass<AGameModeBase>();
    const auto Fixture = MakeShared<FFixture>();
    Fixture->StateClass = FSoftClassPath{StatePath}.TryLoadClass<AGameStateBase>();
    Fixture->ControllerClass = FSoftClassPath{ControllerPath}.TryLoadClass<APlayerController>();
    if (!IsValid(BaseMode) || !IsValid(Fixture->StateClass) || !IsValid(Fixture->ControllerClass))
    { AddError(TEXT("authored Aggro gym classes unavailable")); return false; }
    Fixture->Snapshot = FindFProperty<FStructProperty>(Fixture->StateClass, TEXT("Snapshot"));
    auto* Schema = Fixture->Snapshot ? Fixture->Snapshot->Struct.Get() : nullptr;
    if (!Schema) { AddError(TEXT("Aggro Snapshot schema unavailable")); return false; }
    Fixture->Generation = FindFProperty<FIntProperty>(Schema, TEXT("Generation"));
    Fixture->Sequence = FindFProperty<FIntProperty>(Schema, TEXT("Sequence"));
    Fixture->Ready = FindFProperty<FBoolProperty>(Schema, TEXT("Ready"));
    Fixture->Failed = FindFProperty<FBoolProperty>(Schema, TEXT("Failed"));
    Fixture->ChaseGuard = FindFProperty<FStructProperty>(Schema, TEXT("ChaseGuard"));
    Fixture->PerceptionOrbiter = FindFProperty<FStructProperty>(Schema, TEXT("PerceptionOrbiter"));
    Fixture->ChaseThreats = FindFProperty<FArrayProperty>(Schema, TEXT("ChaseThreats"));
    Fixture->ChaseActiveIndex = FindFProperty<FIntProperty>(Schema, TEXT("ChaseActiveIndex"));
    Fixture->ServerChase = FindFProperty<FStructProperty>(Fixture->StateClass, TEXT("ServerChase"));
    Fixture->ServerPerception = FindFProperty<FStructProperty>(Fixture->StateClass, TEXT("ServerPerception"));
    Fixture->ServerStress = FindFProperty<FStructProperty>(Fixture->StateClass, TEXT("ServerStress"));
    Fixture->ResetSerial = FindFProperty<FIntProperty>(Fixture->ControllerClass, TEXT("LastResetSerial"));
    Fixture->ResetResult = FindFProperty<FIntProperty>(Fixture->ControllerClass, TEXT("LastResetResult"));
    Fixture->ResetRpc = Fixture->ControllerClass->FindFunctionByName(TEXT("Server_RequestAggroReset"));
    Fixture->ResetArg = Fixture->ResetRpc ? FindFProperty<FIntProperty>(Fixture->ResetRpc, TEXT("InSerial")) : nullptr;
    if (!Fixture->Generation || !Fixture->Sequence || !Fixture->Ready || !Fixture->Failed ||
        !Fixture->ChaseGuard || Fixture->ChaseGuard->Struct != TBaseStructure<FVector>::Get() ||
        !Fixture->PerceptionOrbiter || Fixture->PerceptionOrbiter->Struct != TBaseStructure<FVector>::Get() ||
        !Fixture->ChaseThreats || !Fixture->ChaseActiveIndex ||
        !CastField<FNumericProperty>(Fixture->ChaseThreats->Inner) ||
        !CastField<FNumericProperty>(Fixture->ChaseThreats->Inner)->IsFloatingPoint() ||
        !Fixture->ServerChase || !Fixture->ServerPerception ||
        !Fixture->ServerStress || !Fixture->ResetSerial || !Fixture->ResetResult || !Fixture->ResetRpc ||
        !Fixture->ResetArg || !Fixture->ResetArg->HasAnyPropertyFlags(CPF_Parm) ||
        Fixture->ResetRpc->NumParms != 1 ||
        !Fixture->ResetRpc->HasAllFunctionFlags(static_cast<EFunctionFlags>(FUNC_Net | FUNC_NetServer)) ||
        Fixture->ServerChase->Struct != FCk_Handle::StaticStruct() ||
        Fixture->ServerPerception->Struct != FCk_Handle::StaticStruct() ||
        Fixture->ServerStress->Struct != FCk_Handle::StaticStruct())
    { AddError(TEXT("Aggro reflected producer/reset schema rejected")); return false; }
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters, BaseMode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* Registry = ck_aggro_gym_production_dynamics::Registry(Server);
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            return IsValid(Registry) && Registry->Find_GymIndexByName(TEXT("Aggro")) != INDEX_NONE &&
                IsValid(PC) && PC->IsLocalController();
        }), 30.0, TEXT("actual catalog and owning Aggro travel requester")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* Client)
        {
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owning client requests registered Aggro travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::TryRequest_TravelToGymFromPlayer(PC, TEXT("Aggro")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Registry = ck_aggro_gym_production_dynamics::Registry(Server);
            auto* GameState = State(Server, *Fixture);
            if (!IsValid(Registry) || !IsReady(GameState, *Fixture) ||
                Registry->Get_CurrentGymIndex() != Registry->Find_GymIndexByName(TEXT("Aggro")))
            { return false; }
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* ClientWorld = ck::auto_test::net::Get_ClientWorld(Index);
                if (!IsReady(State(ClientWorld, *Fixture), *Fixture) ||
                    !HasNoClientAggro(ClientWorld))
                { return false; }
            }
            return true;
        }), 60.0, TEXT("one actual authority Aggro generation and two client snapshots")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* GameState = State(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            if (!IsReady(GameState, *Fixture)) { return false; }
            Fixture->FirstGeneration = Gen(GameState, *Fixture);
            Fixture->FirstSequence = Seq(GameState, *Fixture);
            Fixture->OldRoots[0] = Root(GameState, Fixture->ServerChase);
            Fixture->OldRoots[1] = Root(GameState, Fixture->ServerPerception);
            Fixture->OldRoots[2] = Root(GameState, Fixture->ServerStress);
            TSet<FCk_Handle> Seen;
            constexpr int32 ExpectedAggro[] = {1, 1, 24};
            constexpr int32 ExpectedTargets[] = {3, 1, 120};
            for (int32 Index = 0; Index < 3; ++Index)
            {
                int32 Aggro = 0, Targets = 0, Timers = 0;
                const bool bCaptured = CaptureBranch(Fixture->OldRoots[Index], Seen,
                    Aggro, Targets, Timers, &Fixture->OldTimers);
                if (!bCaptured || Aggro != ExpectedAggro[Index] ||
                    Targets != ExpectedTargets[Index] || Timers != 1)
                {
                    AddError(FString::Printf(TEXT("Aggro producer branch %d lacks exact authored shape: captured=%d Aggro=%d/%d Targets=%d/%d Timers=%d/1"),
                        Index, bCaptured ? 1 : 0, Aggro, ExpectedAggro[Index],
                        Targets, ExpectedTargets[Index], Timers));
                    return false;
                }
            }
            if (Fixture->OldTimers.Num() != 3) { return false; }
            for (const auto& Known : Seen) { Fixture->OldKnown.Add(Known); }
            Fixture->ChaseGuardEntity = FindRole(Fixture->OldRoots[0], true);
            Fixture->PerceptionOrbiterEntity = FindRole(Fixture->OldRoots[1], false);
            if (ck::IsValid(Fixture->OldRoots[2]))
            {
                for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Fixture->OldRoots[2]))
                {
                    if (ck::IsValid(Child) && UCk_Utils_Aggro_UE::Has(Child))
                    { Fixture->StressGuardEntities.Add(Child); }
                }
            }
            if (!ck::IsValid(Fixture->ChaseGuardEntity) ||
                !ck::IsValid(Fixture->PerceptionOrbiterEntity) ||
                Fixture->StressGuardEntities.Num() != 24) { return false; }
            Fixture->FirstChase = UCk_Utils_Transform_UE::Get_EntityCurrentLocation(
                UCk_Utils_Transform_UE::CastChecked(Fixture->ChaseGuardEntity));
            Fixture->FirstOrbiter = UCk_Utils_Transform_UE::Get_EntityCurrentLocation(
                UCk_Utils_Transform_UE::CastChecked(Fixture->PerceptionOrbiterEntity));
            Fixture->FirstThreat = ThreatSum(Fixture->OldRoots[0], Fixture->ChaseGuardEntity);
            return TestTrue(TEXT("source producer threat is readable"), Fixture->FirstThreat >= 0.0f);
        }), TEXT("capture authored source ECS, timer, and target identities")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        { return ObserveDynamics(*Fixture); }),
        12.0, TEXT("actual Aggro producer motion/threat and exact two-client publication history")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* Client)
        {
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            if (!TestTrue(TEXT("actual Aggro reset owner and reflected RPC"),
                IsValid(PC) && PC->IsA(Fixture->ControllerClass))) { return; }
            struct FParams { int32 InSerial = 1401; } Params;
            PC->ProcessEvent(Fixture->ResetRpc, &Params);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* ServerState = State(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            auto* ClientWorld = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(ClientWorld) ? ClientWorld->GetFirstPlayerController() : nullptr;
            if (!IsReady(ServerState, *Fixture) || Gen(ServerState, *Fixture) != Fixture->FirstGeneration + 1 ||
                !IsValid(PC) || !PC->IsA(Fixture->ControllerClass) ||
                Fixture->ResetSerial->GetPropertyValue_InContainer(PC) != 1401 ||
                Fixture->ResetResult->GetPropertyValue_InContainer(PC) != 1)
            { return false; }
            for (const auto& Old : Fixture->OldKnown)
            { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Old)) { return false; } }
            for (const auto& OldTimer : Fixture->OldTimers)
            { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(OldTimer)) { return false; } }
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* IterClientWorld = ck::auto_test::net::Get_ClientWorld(Index);
                auto* Client = State(IterClientWorld, *Fixture);
                if (!IsReady(Client, *Fixture) ||
                    Gen(Client, *Fixture) != Fixture->FirstGeneration + 1 ||
                    !HasNoClientAggro(IterClientWorld))
                { return false; }
            }
            return ck::IsValid(Root(ServerState, Fixture->ServerChase)) &&
                Root(ServerState, Fixture->ServerChase) != Fixture->OldRoots[0] &&
                Root(ServerState, Fixture->ServerPerception) != Fixture->OldRoots[1] &&
                Root(ServerState, Fixture->ServerStress) != Fixture->OldRoots[2];
        }), 45.0, TEXT("owner reset replaces source trio and drains every observed old entity/timer")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* ServerState = State(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            if (!IsReady(ServerState, *Fixture) ||
                Gen(ServerState, *Fixture) != Fixture->FirstGeneration + 1) { return false; }
            const FCk_Handle NewRoots[] = {
                Root(ServerState, Fixture->ServerChase),
                Root(ServerState, Fixture->ServerPerception),
                Root(ServerState, Fixture->ServerStress)
            };
            constexpr int32 ExpectedAggro[] = {1, 1, 24};
            constexpr int32 ExpectedTargets[] = {3, 1, 120};
            TSet<FCk_Handle> NewKnown;
            for (int32 Index = 0; Index < 3; ++Index)
            {
                int32 Aggro = 0, Targets = 0, Timers = 0;
                if (!ck::IsValid(NewRoots[Index]) || NewRoots[Index] == Fixture->OldRoots[Index] ||
                    !CaptureBranch(NewRoots[Index], NewKnown, Aggro, Targets, Timers) ||
                    Aggro != ExpectedAggro[Index] || Targets != ExpectedTargets[Index] || Timers != 1)
                { AddError(FString::Printf(TEXT("replacement Aggro branch %d lacks exact authored shape"), Index)); return false; }
            }
            for (const auto& Old : Fixture->OldKnown)
            { if (NewKnown.Contains(Old) || UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Old)) { return false; } }
            return true;
        }), TEXT("replacement source has distinct live producer and timer roles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            Fixture->Restore();
            return TestTrue(TEXT("Aggro production PIE worlds ended"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
        }), TEXT("restore gym startup preferences"), true));
    return true;
}

#endif
