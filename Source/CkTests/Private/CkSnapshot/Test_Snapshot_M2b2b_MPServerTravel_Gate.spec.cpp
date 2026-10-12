// CkSnapshot M2b-2b GATE — multiplayer SEAMLESS ServerTravel + client re-derivation.
// Server-side: the M2b-2a contract (restore + respawn + re-bridge + driver re-establish), now under NM_ListenServer
// after a seamless travel. Client-side: the connected client rides the seamless travel (preserved UNetConnection)
// and independently re-derives the restored entity + actor + replicated state, purely via replication, with no
// client-side snapshot code.
// Surface in Session Frontend: Ck.Snapshot.M2b2b.MPServerTravel

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h" // TActorIterator
#include "HAL/IConsoleManager.h" // net.AllowPIESeamlessTravel
#include "UObject/StrongObjectPtr.h"

#include "CkAttribute/FloatAttribute/CkFloatAttribute_Utils.h"
#include "CkAttribute/FloatAttribute/CkFloatAttribute_Fragment.h"

#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"
#include "CkEcs/Snapshot/CkSaveKey_Fragment.h"

#include "CkTests/Net/CkAutoTest_NetSubject_M2bProbe_Replicated.h"
#include "CkTests/Net/CkAutoTest_NetSubject_M2bProbe.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "CkSnapshot_SaveCompletionProbe.h"
namespace
{
    constexpr auto M2b2b_MapPath          = TEXT("/Engine/Maps/Entry");
    constexpr auto M2b2b_AttributeTagName = TEXT("FloatAttribute.Health");
    constexpr auto M2b2b_ExpectedFinal    = 42.5f;
    constexpr auto M2b2b_SavedFinal       = 73.25f;
    constexpr auto M2b2b_LiveFinal        = 19.75f;
    const auto     M2b2b_SlotName         = FName{TEXT("CkSnapshot_M2b2b_GateSlot")};
    const auto     M2b2b_SavedLocation    = FVector{100.0, 200.0, 300.0};
    const auto     M2b2b_NonrepLocation   = FVector{400.0, 500.0, 600.0};
    const auto     M2b2b_SaveKey          = FGuid{0xB2B2B2B2, 0x13572468, 0x24681357, 0xABCDEF01};

    static TWeakObjectPtr<UWorld> GM2b2b_PreServerWorld;

    static TWeakObjectPtr<UWorld> GM2b2b_PreClientWorld[2];
    static TWeakObjectPtr<AActor> GM2b2b_PreServerProbe;
    static TWeakObjectPtr<AActor> GM2b2b_PreClientProbe[2];
    static uint32 GM2b2b_PreServerWorldId = 0;
    static uint32 GM2b2b_PreServerProbeId = 0;
    static uint32 GM2b2b_PreClientWorldId[2] = {};
    static uint32 GM2b2b_PreClientProbeId[2] = {};
    struct FM2b2b_RunState
    {
        IConsoleVariable* SeamlessCVar = nullptr;
        int32 OriginalSeamlessValue = 0;
        EConsoleVariableFlags OriginalSeamlessPriority = ECVF_Default;
        bool SeamlessOverrideApplied = false;
        bool SaveSucceeded = false;
        TStrongObjectPtr<UCkSnapshot_SaveCompletionProbe> SaveProbe;
        void Restore()
        {
            if (!SeamlessCVar || !SeamlessOverrideApplied) { return; }
            if (OriginalSeamlessPriority == ECVF_SetByCode)
            { SeamlessCVar->Set(OriginalSeamlessValue, ECVF_SetByCode); }
            else
            { SeamlessCVar->Unset(ECVF_SetByCode); }
            SeamlessOverrideApplied = false;
        }
        bool IsRestored() const
        {
            return SeamlessCVar &&
                SeamlessCVar->GetInt() == OriginalSeamlessValue &&
                (SeamlessCVar->GetFlags() & ECVF_SetByMask) == OriginalSeamlessPriority;
        }
        ~FM2b2b_RunState() { Restore(); }
    };
    auto M2b2b_MapNameOf(UWorld* InWorld) -> FString
    {
        return InWorld != nullptr ? InWorld->RemovePIEPrefix(InWorld->GetOutermost()->GetName()) : FString{};
    }

    auto M2b2b_Subsystem(UWorld* InWorld) -> UCk_Snapshot_Subsystem_UE*
    {
        if (InWorld == nullptr || InWorld->GetGameInstance() == nullptr) { return nullptr; }
        return InWorld->GetGameInstance()->GetSubsystem<UCk_Snapshot_Subsystem_UE>();
    }

    auto M2b2b_FindProbe(UWorld* InWorld) -> ACk_AutoTest_NetSubject_M2bProbe_Replicated*
    {
        if (InWorld == nullptr) { return nullptr; }
        for (auto It = TActorIterator<ACk_AutoTest_NetSubject_M2bProbe_Replicated>(InWorld); It; ++It) { return *It; }
        return nullptr;
    }

    auto M2b2b_CountProbes(UWorld* InWorld) -> int32
    {
        auto Count = 0;
        if (InWorld == nullptr) { return 0; }
        for (auto It = TActorIterator<ACk_AutoTest_NetSubject_M2bProbe_Replicated>(InWorld); It; ++It) { ++Count; }
        return Count;
    }

    auto M2b2b_FindNonrepProbe(UWorld* InWorld) -> ACk_AutoTest_NetSubject_M2bProbe*
    {
        if (InWorld == nullptr) { return nullptr; }
        for (auto It = TActorIterator<ACk_AutoTest_NetSubject_M2bProbe>(InWorld); It; ++It) { return *It; }
        return nullptr;
    }

    auto M2b2b_CountNonrepProbes(UWorld* InWorld) -> int32
    {
        int32 Count = 0;
        if (InWorld == nullptr) { return Count; }
        for (auto It = TActorIterator<ACk_AutoTest_NetSubject_M2bProbe>(InWorld); It; ++It) { ++Count; }
        return Count;
    }

    auto M2b2b_ResolveEntity(AActor* InProbe) -> FCk_Handle
    {
        if (InProbe == nullptr) { return {}; }
        return UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(InProbe);
    }

    auto M2b2b_ResolveAttribute(AActor* InProbe) -> FCk_Handle_FloatAttribute
    {
        const auto Entity = M2b2b_ResolveEntity(InProbe);
        if (ck::Is_NOT_Valid(Entity)) { return {}; }
        return UCk_Utils_FloatAttribute_UE::TryGet(Entity, FGameplayTag::RequestGameplayTag(FName{M2b2b_AttributeTagName}));
    }

    // Reads the LIVE FloatAttribute.Health final value straight from a world's registry (independent of the
    // actor<->entity bridge). Mirrors the proven M2b-2a gate helper. OutCount = how many FloatAttribute entities.
    auto M2b2b_LiveFinalFromRawView(UWorld* InWorld, int32& OutCount) -> float
    {
        OutCount = 0;
        auto Final = -1.0f;
        auto* Ecs = InWorld ? InWorld->GetSubsystem<UCk_EcsWorld_Subsystem_UE>() : nullptr;
        if (ck::Is_NOT_Valid(Ecs)) { return Final; }
        auto& CkRegistry = Ecs->Get_Registry();
        auto* Raw = ck::registry_table::TryResolve(CkRegistry.Get_RegistryHandle());
        if (Raw == nullptr) { return Final; }
        for (const auto Entity : Raw->view<ck::FFragment_FloatAttribute>())
        {
            ++OutCount;
            auto Handle = ck::MakeHandle(FCk_Entity{Entity}, CkRegistry);
            auto AttrHandle = UCk_Utils_FloatAttribute_UE::Cast(Handle);
            Final = static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(AttrHandle));
        }
        return Final;
    }

    // Log both the registry census and this probe's resolved attribute; the server also has a nonreplicated control.
    // Object indices are diagnostic tags only because GC can reuse them. Weak index+serial comparisons prove
    // world and actor freshness in the post-travel assertions below.
    auto M2b2b_LogState(const TCHAR* InWhen, const TCHAR* InRole, UWorld* InWorld) -> void
    {
        auto* Probe = M2b2b_FindProbe(InWorld);
        const auto Entity = M2b2b_ResolveEntity(Probe);
        auto AttrCount = 0;
        const auto Final = M2b2b_LiveFinalFromRawView(InWorld, AttrCount);
        const auto ProbeAttr = M2b2b_ResolveAttribute(Probe);
        const auto ProbeFinal = ck::IsValid(ProbeAttr) ?
            static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(ProbeAttr)) : -1.0f;
        const auto* Sub = M2b2b_Subsystem(InWorld);

        auto ProbeName  = FString{TEXT("none")};
        auto ProbeUid   = uint32{0};
        auto ProbeBegun = 0;
        auto ProbeRole  = -1;
        auto ProbeLoc   = FString{TEXT("n/a")};
        if (Probe != nullptr)
        {
            ProbeName  = Probe->GetName();
            ProbeUid   = Probe->GetUniqueID();
            ProbeBegun = Probe->HasActorBegunPlay() ? 1 : 0;
            ProbeRole  = static_cast<int32>(Probe->GetLocalRole());
            ProbeLoc   = Probe->GetActorLocation().ToString();
        }

        UE_LOG(LogTemp, Display,
            TEXT("DIAG M2b2b [%s] %s: world=[%s] worldUid=[%u] worldBegun=[%d] map=[%s] netmode=[%d] load=[%d] probeCount=[%d] nonrepCount=[%d] bridgeValid=[%d] attrCount=[%d] rawFinal=[%f] probeAttrValid=[%d] probeFinal=[%f] | probeName=[%s] probeUid=[%u] begunPlay=[%d] localRole=[%d] loc=[%s]"),
            InWhen, InRole,
            InWorld ? *InWorld->GetName() : TEXT("null"), InWorld ? InWorld->GetUniqueID() : 0,
            InWorld && InWorld->HasBegunPlay() ? 1 : 0, *M2b2b_MapNameOf(InWorld),
            InWorld ? static_cast<int32>(InWorld->GetNetMode()) : -99, Sub && Sub->Get_IsLoadInProgress() ? 1 : 0,
            M2b2b_CountProbes(InWorld), M2b2b_CountNonrepProbes(InWorld), ck::IsValid(Entity) ? 1 : 0, AttrCount, Final,
            ck::IsValid(ProbeAttr) ? 1 : 0, ProbeFinal,
            *ProbeName, ProbeUid, ProbeBegun, ProbeRole, *ProbeLoc);
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkSnapshot_M2b2b_MPServerTravel_Gate,
    "Ck.Snapshot.M2b2b.MPServerTravel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkSnapshot_M2b2b_MPServerTravel_Gate::RunTest(const FString& Parameters)
{
    auto DedicatedServer = false;
    if (NOT ck::auto_test::net::TryGet_DedicatedServer(Parameters, DedicatedServer))
    { AddError(TEXT("Unsupported topology for MP ServerTravel gate")); return false; }

    constexpr auto NumPIEClients = 3;        // authority plus two real remote clients
    constexpr auto ExpectedTotalWorlds = 3;
    constexpr auto BootstrapTimeoutSeconds = 60.0f; // allow both remote BeginPlay paths to finish
    constexpr auto ReadyTimeoutSeconds = 30.0f;
    constexpr auto FramesPerSettle = 30;
    constexpr auto ReloadTimeoutSeconds = 60.0f; // seamless: ~4s ServerTravelPause + transition + restore + replication

    GM2b2b_PreServerWorld = nullptr;

    GM2b2b_PreServerProbe = nullptr;
    GM2b2b_PreServerWorldId = 0;
    GM2b2b_PreServerProbeId = 0;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        GM2b2b_PreClientWorld[Index] = nullptr;
        GM2b2b_PreClientProbe[Index] = nullptr;
        GM2b2b_PreClientWorldId[Index] = 0;
        GM2b2b_PreClientProbeId[Index] = 0;
    }
    const auto Run = MakeShared<FM2b2b_RunState>();
    Run->SeamlessCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel"));
    if (!Run->SeamlessCVar)
    { AddError(TEXT("net.AllowPIESeamlessTravel CVar unavailable")); return false; }
    Run->OriginalSeamlessValue = Run->SeamlessCVar->GetInt();
    Run->OriginalSeamlessPriority =
        static_cast<EConsoleVariableFlags>(Run->SeamlessCVar->GetFlags() & ECVF_SetByMask);
    if (Run->OriginalSeamlessValue != 1 && Run->OriginalSeamlessPriority > ECVF_SetByCode)
    { AddError(TEXT("net.AllowPIESeamlessTravel has a higher-priority override; cannot safely enable it")); return false; }
    Run->SaveProbe.Reset(NewObject<UCkSnapshot_SaveCompletionProbe>(GetTransientPackage()));
    if (Run->SaveProbe.Get() == nullptr)
    { AddError(TEXT("Cannot allocate the snapshot save-completion probe")); return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(NumPIEClients, FString{M2b2b_MapPath}, Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(ExpectedTotalWorlds, BootstrapTimeoutSeconds));

    // Stage 1 — open the PIE seamless-travel gate + spawn the REPLICATED bridged probe on the server.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(

        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* InServer) -> void
        {
            if (Run->OriginalSeamlessValue != 1)
            {
                Run->SeamlessCVar->Set(1, ECVF_SetByCode);
                Run->SeamlessOverrideApplied = true;
            }
            if (Run->SeamlessCVar->GetInt() != 1)
            {
                ck::auto_test::net::FailSession(TEXT("net.AllowPIESeamlessTravel did not enable"));
                return;
            }
            auto SpawnInfo = FActorSpawnParameters{};
            SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Probe = InServer->SpawnActor<ACk_AutoTest_NetSubject_M2bProbe_Replicated>(
                ACk_AutoTest_NetSubject_M2bProbe_Replicated::StaticClass(), FTransform{M2b2b_SavedLocation}, SpawnInfo);
            if (Probe == nullptr) { AddError(TEXT("Stage 1: replicated probe spawn returned null")); }
            auto* Nonrep = InServer->SpawnActor<ACk_AutoTest_NetSubject_M2bProbe>(
                ACk_AutoTest_NetSubject_M2bProbe::StaticClass(), FTransform{M2b2b_NonrepLocation}, SpawnInfo);
            if (Nonrep == nullptr || Nonrep->GetIsReplicated())
            { AddError(TEXT("Stage 1: nonreplicated probe fixture is unavailable or unexpectedly replicated")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesPerSettle));

    // Stage 2 — both connected clients independently see the actor, bridge and default value.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || ck::auto_test::net::Get_AllPIEWorlds().Num() != 3 ||
                !IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)) ||
                !IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1)) ||
                M2b2b_CountProbes(Server) != 1 || M2b2b_CountNonrepProbes(Server) != 1 ||
                ck::Is_NOT_Valid(M2b2b_ResolveAttribute(M2b2b_FindProbe(Server))) ||
                ck::Is_NOT_Valid(M2b2b_ResolveAttribute(M2b2b_FindNonrepProbe(Server))))
            { return false; }
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(Index);
                auto* Probe = M2b2b_FindProbe(Client);
                const auto Attr = M2b2b_ResolveAttribute(Probe);
                if (!IsValid(Client) || Client->GetNetMode() != NM_Client ||
                    M2b2b_CountProbes(Client) != 1 || M2b2b_CountNonrepProbes(Client) != 0 ||
                    ck::Is_NOT_Valid(M2b2b_ResolveEntity(Probe)) || ck::Is_NOT_Valid(Attr) ||
                    !FMath::IsNearlyEqual(
                        static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(Attr)),
                        M2b2b_ExpectedFinal, 0.01f))
                { return false; }
            }
            return true;
        }), ReadyTimeoutSeconds, TEXT("both remote probe bridges and default attributes ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            M2b2b_LogState(TEXT("pre-reload"), TEXT("server"), ck::auto_test::net::Get_ServerWorld());
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(Index);
                if (!Client) { AddError(TEXT("Stage 2: remote client world missing")); return false; }
                M2b2b_LogState(TEXT("pre-reload"), TEXT("client"), Client);
                TestEqual(TEXT("pre-reload: remote client has exactly one replicated probe"),
                    M2b2b_CountProbes(Client), 1);
                TestEqual(TEXT("pre-reload: remote client has no nonreplicated probe"),
                    M2b2b_CountNonrepProbes(Client), 0);
            }
            return true;
        }),
        TEXT("pre-reload client baseline: replicated probe present on the client")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UWorld* Server = ck::auto_test::net::Get_ServerWorld();
            return ck::IsValid(M2b2b_ResolveEntity(M2b2b_FindProbe(Server)))
                && ck::IsValid(M2b2b_ResolveAttribute(M2b2b_FindProbe(Server)))
                && ck::IsValid(M2b2b_ResolveEntity(M2b2b_FindNonrepProbe(Server)))
                && ck::IsValid(M2b2b_ResolveAttribute(M2b2b_FindNonrepProbe(Server)));
        }),
        ReadyTimeoutSeconds,
        TEXT("replicated and nonreplicated fixture bridges and attributes are ready before save")));

    // Deferred authority mutation: the saved value must differ from Construct's 42.5.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer)
        {
            auto Attr = M2b2b_ResolveAttribute(M2b2b_FindProbe(InServer));
            if (ck::Is_NOT_Valid(Attr))
            { AddError(TEXT("Stage 3: authority attribute unavailable for override")); return; }
            UCk_Utils_FloatAttribute_UE::Request_Override(
                Attr, M2b2b_SavedFinal, ECk_MinMaxCurrent::Current, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto ServerAttr = M2b2b_ResolveAttribute(M2b2b_FindProbe(Server));
            if (ck::Is_NOT_Valid(ServerAttr) ||
                !FMath::IsNearlyEqual(
                    static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(ServerAttr)),
                    M2b2b_SavedFinal, 0.01f)) { return false; }
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(Index);
                const auto Attr = M2b2b_ResolveAttribute(M2b2b_FindProbe(Client));
                if (!IsValid(Client) || M2b2b_CountNonrepProbes(Client) != 0 ||
                    ck::Is_NOT_Valid(Attr) ||
                    !FMath::IsNearlyEqual(
                        static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(Attr)),
                        M2b2b_SavedFinal, 0.01f))
                { return false; }
            }
            return true;
        }), ReadyTimeoutSeconds, TEXT("both clients receive distinctive authority health before save")));
    // Stage 3 — pre-save sanity + Save on the server; stash pre-travel worlds.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* InServer) -> void
        {
            auto* Probe = M2b2b_FindProbe(InServer);
            const auto Attr = M2b2b_ResolveAttribute(Probe);
            if (ck::Is_NOT_Valid(Attr)) { AddError(TEXT("Stage 3: could not resolve attribute pre-save")); return; }
            TestTrue(TEXT("pre-save authority health is distinctive"),
                FMath::IsNearlyEqual(
                    static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(Attr)),
                    M2b2b_SavedFinal, 0.01f));
            auto Entity = M2b2b_ResolveEntity(Probe);
            if (ck::Is_NOT_Valid(Entity))
            { AddError(TEXT("Stage 3: replicated probe bridge unresolved before SaveKey assignment")); return; }
            Entity.AddOrReplace<FFragment_SaveKey>(M2b2b_SaveKey);

            auto* Nonrep = M2b2b_FindNonrepProbe(InServer);
            const auto NonrepEntity = M2b2b_ResolveEntity(Nonrep);
            const auto NonrepAttr = M2b2b_ResolveAttribute(Nonrep);
            if (ck::Is_NOT_Valid(NonrepEntity) || ck::Is_NOT_Valid(NonrepAttr))
            { AddError(TEXT("Stage 3: nonreplicated fixture bridge or attribute unresolved before save")); return; }
            TestFalse(TEXT("pre-save nonreplicated fixture has no replication driver"),
                UCk_Utils_EntityReplicationDriver_UE::Has(NonrepEntity));
            TestEqual(TEXT("pre-save nonreplicated fixture has the expected attribute"),
                static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(NonrepAttr)), M2b2b_ExpectedFinal);

            GM2b2b_PreServerWorld = InServer;

            GM2b2b_PreServerProbe = Probe;
            GM2b2b_PreServerWorldId = InServer->GetUniqueID();
            GM2b2b_PreServerProbeId = Probe->GetUniqueID();
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(Index);
                GM2b2b_PreClientWorld[Index] = Client;
                auto* ClientProbe = M2b2b_FindProbe(Client);
                GM2b2b_PreClientProbe[Index] = ClientProbe;
                GM2b2b_PreClientWorldId[Index] = IsValid(Client) ? Client->GetUniqueID() : 0;
                GM2b2b_PreClientProbeId[Index] = IsValid(ClientProbe) ? ClientProbe->GetUniqueID() : 0;
            }
            auto* Sub = M2b2b_Subsystem(InServer);
            if (Sub == nullptr) { AddError(TEXT("Stage 3: no snapshot subsystem")); return; }
            Run->SaveProbe->Clear();
            auto Completion = FCk_Delegate_OnSaveComplete{};
            Completion.BindDynamic(Run->SaveProbe.Get(), &UCkSnapshot_SaveCompletionProbe::OnSaveComplete);
            Sub->Request_Save(M2b2b_SlotName, Completion);
            Run->SaveSucceeded = !Sub->Get_IsSaveInProgress() && Run->SaveProbe->HasFreshSuccess();
            if (!Run->SaveSucceeded)
            {
                ck::auto_test::net::FailSession(FString::Printf(
                    TEXT("Stage 3: save did not complete exactly once with Success (callbacks=%d, result=%d)"),
                    Run->SaveProbe->CallbackCount, static_cast<int32>(Run->SaveProbe->Result)));
            }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() { return Run->SaveSucceeded; }),
        ReadyTimeoutSeconds, TEXT("successful authority snapshot save completed")));
    // Stage 4 — fire the async load (triggers seamless ServerTravel internally) + assert non-blocking.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* InServer) -> void
        {
            if (!Run->SaveSucceeded)
            { AddError(TEXT("Stage 4: refusing load without a successful save")); return; }
            if (!IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(InServer, 0)) ||
                !IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(InServer, 1)))
            { AddError(TEXT("Stage 4: both remote connections required for seamless load")); return; }
            auto* Sub = M2b2b_Subsystem(InServer);
            if (Sub == nullptr) { AddError(TEXT("Stage 4: no snapshot subsystem")); return; }
            Sub->Request_Load(M2b2b_SlotName, FCk_Delegate_OnLoadComplete{});
            TestTrue(TEXT("Stage 4: Request_Load non-blocking — load in progress after return"),
                Sub->Get_IsLoadInProgress());
        })));

    // UObject indices (GetUniqueID) can be reused after travel GC; weak index+serial identity proves freshness.
    // Stage 5 — fresh authority and both distinct client worlds, bridges and saved values.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([NextDiagnosticSeconds = 0.0]() mutable -> bool
        {
            const auto Now = FPlatformTime::Seconds();
            if (Now >= NextDiagnosticSeconds)
            {
                NextDiagnosticSeconds = Now + 10.0;
                UE_LOG(LogTemp, Display,
                    TEXT("DIAG M2b2b post-load wait: worlds=%d oldWorldIds=%u,%u,%u oldProbeIds=%u,%u,%u"),
                    ck::auto_test::net::Get_AllPIEWorlds().Num(), GM2b2b_PreServerWorldId,
                    GM2b2b_PreClientWorldId[0], GM2b2b_PreClientWorldId[1], GM2b2b_PreServerProbeId,
                    GM2b2b_PreClientProbeId[0], GM2b2b_PreClientProbeId[1]);
                M2b2b_LogState(TEXT("post-load-wait"), TEXT("server"), ck::auto_test::net::Get_ServerWorld());
                M2b2b_LogState(TEXT("post-load-wait"), TEXT("client0"), ck::auto_test::net::Get_ClientWorld(0));
                M2b2b_LogState(TEXT("post-load-wait"), TEXT("client1"), ck::auto_test::net::Get_ClientWorld(1));
            }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || TWeakObjectPtr<UWorld>{Server}.HasSameIndexAndSerialNumber(GM2b2b_PreServerWorld) ||
                GM2b2b_PreServerWorldId == 0 ||
                !Server->HasBegunPlay() || M2b2b_MapNameOf(Server) != M2b2b_MapPath ||
                M2b2b_CountProbes(Server) != 1 || M2b2b_CountNonrepProbes(Server) != 1)
            { return false; }
            auto* Sub = M2b2b_Subsystem(Server);
            auto* Probe = M2b2b_FindProbe(Server);
            const auto Attr = M2b2b_ResolveAttribute(Probe);
            if (!Sub || Sub->Get_IsLoadInProgress() || TWeakObjectPtr<AActor>{Probe}.HasSameIndexAndSerialNumber(GM2b2b_PreServerProbe) ||
                GM2b2b_PreServerProbeId == 0 ||
                !IsValid(Probe) ||
                ck::Is_NOT_Valid(Attr) ||
                !FMath::IsNearlyEqual(
                    static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(Attr)),
                    M2b2b_SavedFinal, 0.01f))
            { return false; }

            UWorld* ClientWorlds[2] = {};
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(Index);
                ClientWorlds[Index] = Client;
                auto* ClientProbe = M2b2b_FindProbe(Client);
                const auto ClientAttr = M2b2b_ResolveAttribute(ClientProbe);
                if (!IsValid(Client) || Client == Server ||
                    TWeakObjectPtr<UWorld>{Client}.HasSameIndexAndSerialNumber(GM2b2b_PreClientWorld[Index]) ||
                    TWeakObjectPtr<UWorld>{Client}.HasSameIndexAndSerialNumber(GM2b2b_PreClientWorld[1 - Index]) ||
                    GM2b2b_PreClientWorldId[Index] == 0 ||
                    GM2b2b_PreClientWorldId[1 - Index] == 0 ||
                    !Client->HasBegunPlay() || Client->GetNetMode() != NM_Client ||
                    M2b2b_MapNameOf(Client) != M2b2b_MapPath ||
                    M2b2b_CountProbes(Client) != 1 || M2b2b_CountNonrepProbes(Client) != 0 ||
                    !IsValid(ClientProbe) || TWeakObjectPtr<AActor>{ClientProbe}.HasSameIndexAndSerialNumber(GM2b2b_PreClientProbe[Index]) ||
                    TWeakObjectPtr<AActor>{ClientProbe}.HasSameIndexAndSerialNumber(GM2b2b_PreClientProbe[1 - Index]) ||
                    GM2b2b_PreClientProbeId[Index] == 0 ||
                    GM2b2b_PreClientProbeId[1 - Index] == 0 ||
                    ck::Is_NOT_Valid(M2b2b_ResolveEntity(ClientProbe)) ||
                    ck::Is_NOT_Valid(ClientAttr) ||
                    !FMath::IsNearlyEqual(
                        static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(ClientAttr)),
                        M2b2b_SavedFinal, 0.01f))
                { return false; }
            }
            return ClientWorlds[0] != ClientWorlds[1] &&
                ck::auto_test::net::Get_AllPIEWorlds().Num() == 3;
        }), ReloadTimeoutSeconds, TEXT("fresh server and both clients independently restore saved health")));
    // Stage 6 — SERVER assertions (the M2b-2a contract after seamless travel).
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, DedicatedServer]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (Server == nullptr) { AddError(TEXT("Stage 6: no post-travel server world (Get_ServerWorld null)")); return false; }
            TestTrue(TEXT("server: real travel happened — world instance changed"), !TWeakObjectPtr<UWorld>{Server}.HasSameIndexAndSerialNumber(GM2b2b_PreServerWorld));
            TestTrue(TEXT("server: respawned probe is a new actor"),
                GM2b2b_PreServerProbeId != 0 &&
                IsValid(M2b2b_FindProbe(Server)) &&
                !TWeakObjectPtr<AActor>{M2b2b_FindProbe(Server)}.HasSameIndexAndSerialNumber(GM2b2b_PreServerProbe));
            TestTrue(TEXT("server: on the destination map"), M2b2b_MapNameOf(Server) == M2b2b_MapPath);
            TestTrue(TEXT("server: retains the requested topology post-travel"),
                Server->GetNetMode() == (DedicatedServer ? NM_DedicatedServer : NM_ListenServer));

            auto* Sub = M2b2b_Subsystem(Server);
            if (Sub == nullptr) { AddError(TEXT("Stage 6: no snapshot subsystem")); return false; }
            TestFalse(TEXT("server: load flag cleared after completion"), Sub->Get_IsLoadInProgress());

            TestEqual(TEXT("server: exactly one probe actor (no duplicate)"), M2b2b_CountProbes(Server), 1);

            auto* Probe = M2b2b_FindProbe(Server);
            if (Probe == nullptr) { AddError(TEXT("Stage 6: probe was not re-spawned on the server")); return false; }

            const auto Entity = M2b2b_ResolveEntity(Probe);
            TestTrue(TEXT("server: actor<->entity bridge resolves (re-bridge worked)"), ck::IsValid(Entity));
            auto KeyedEntity = FCk_Handle{};
            TestTrue(TEXT("server: restored SaveKey resolves on the new authority world"),
                Sub->TryResolve_SaveKey(M2b2b_SaveKey, KeyedEntity));
            TestTrue(TEXT("server: restored SaveKey names the respawned replicated actor bridge"),
                ck::IsValid(Entity) && KeyedEntity == Entity);
            TestTrue(TEXT("server: replication driver re-established on the restored entity"),
                ck::IsValid(Entity) && UCk_Utils_EntityReplicationDriver_UE::Has(Entity));

            const auto RespawnedLoc = Probe->GetActorLocation();
            AddInfo(FString::Printf(TEXT("DIAG M2b2b server position: respawned actor at %s (saved %s)"),
                *RespawnedLoc.ToString(), *M2b2b_SavedLocation.ToString()));
            TestTrue(TEXT("server: respawned actor restored to its saved world location"),
                RespawnedLoc.Equals(M2b2b_SavedLocation, 1.0));

            auto AttrCount = 0;
            const auto Final = M2b2b_LiveFinalFromRawView(Server, AttrCount);
            AddInfo(FString::Printf(TEXT("DIAG M2b2b server Stage 6: FloatAttribute entities=%d | Final=%f"), AttrCount, Final));
            TestEqual(TEXT("server: exactly two fixture attributes survived restore"), AttrCount, 2);

            const auto SavedAttr = M2b2b_ResolveAttribute(Probe);
            TestTrue(TEXT("server: distinctive saved health restored on replicated bridge"),
                ck::IsValid(SavedAttr) &&
                FMath::IsNearlyEqual(
                    static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(SavedAttr)),
                    M2b2b_SavedFinal, 0.01f));
            TestEqual(TEXT("server: exactly one nonreplicated fixture actor after travel"),
                M2b2b_CountNonrepProbes(Server), 1);
            auto* Nonrep = M2b2b_FindNonrepProbe(Server);
            if (Nonrep == nullptr) { AddError(TEXT("Stage 6: nonreplicated fixture did not respawn")); return false; }
            TestTrue(TEXT("server: nonreplicated fixture belongs to the fresh server world"),
                Nonrep->GetWorld() == Server && !TWeakObjectPtr<UWorld>{Server}.HasSameIndexAndSerialNumber(GM2b2b_PreServerWorld));
            TestFalse(TEXT("server: restored fixture actor remains nonreplicated"), Nonrep->GetIsReplicated());
            const auto NonrepEntity = M2b2b_ResolveEntity(Nonrep);
            TestTrue(TEXT("server: nonreplicated fixture bridge resolves"), ck::IsValid(NonrepEntity));
            TestFalse(TEXT("server: nonreplicated fixture has no replication driver"),
                ck::IsValid(NonrepEntity) && UCk_Utils_EntityReplicationDriver_UE::Has(NonrepEntity));
            TestTrue(TEXT("server: nonreplicated fixture location restored"),
                Nonrep->GetActorLocation().Equals(M2b2b_NonrepLocation, 1.0));
            const auto NonrepAttr = M2b2b_ResolveAttribute(Nonrep);
            if (ck::Is_NOT_Valid(NonrepAttr))
            { AddError(TEXT("Stage 6: nonreplicated fixture attribute unresolved")); return false; }
            TestEqual(TEXT("server: nonreplicated fixture value restored"),
                static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(NonrepAttr)), M2b2b_ExpectedFinal);
            return true;
        }),
        TEXT("M2b-2b server: restore + respawn + re-bridge + driver re-establish under NM_ListenServer, no duplicate")));

    // Stage 7 — each remote client independently re-derives the fresh actor and saved value.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {

            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(Index);
                if (!Client) { AddError(TEXT("Stage 7: missing remote client world")); return false; }
                M2b2b_LogState(TEXT("post-reload"), TEXT("client"), Client);
                TestTrue(TEXT("client: fresh distinct destination world"),
                    GM2b2b_PreClientWorldId[Index] != 0 &&
                    !TWeakObjectPtr<UWorld>{Client}.HasSameIndexAndSerialNumber(GM2b2b_PreClientWorld[Index]) &&
                    !TWeakObjectPtr<UWorld>{Client}.HasSameIndexAndSerialNumber(GM2b2b_PreClientWorld[1 - Index]) &&
                    M2b2b_MapNameOf(Client) == M2b2b_MapPath &&
                    Client->GetNetMode() == NM_Client);
                TestEqual(TEXT("client: exactly one replicated probe"), M2b2b_CountProbes(Client), 1);
                TestEqual(TEXT("client: zero nonreplicated controls"), M2b2b_CountNonrepProbes(Client), 0);
                auto* Probe = M2b2b_FindProbe(Client);
                if (!Probe) { AddError(TEXT("Stage 7: client probe missing")); return false; }
                TestTrue(TEXT("client: fresh probe actor"),
                    GM2b2b_PreClientProbeId[Index] != 0 &&
                    !TWeakObjectPtr<AActor>{Probe}.HasSameIndexAndSerialNumber(GM2b2b_PreClientProbe[Index]) &&
                    !TWeakObjectPtr<AActor>{Probe}.HasSameIndexAndSerialNumber(GM2b2b_PreClientProbe[1 - Index]));
                auto AttrCount = 0;
                M2b2b_LiveFinalFromRawView(Client, AttrCount);
                TestEqual(TEXT("client: exactly one FloatAttribute entity"), AttrCount, 1);
                TestTrue(TEXT("client: bridge resolves"), ck::IsValid(M2b2b_ResolveEntity(Probe)));
                const auto Attr = M2b2b_ResolveAttribute(Probe);
                TestTrue(TEXT("client: distinctive saved health replicated"),
                    ck::IsValid(Attr) &&
                    FMath::IsNearlyEqual(
                        static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(Attr)),
                        M2b2b_SavedFinal, 0.01f));
                TestTrue(TEXT("client: saved transform replicated"),
                    Probe->GetActorLocation().Equals(M2b2b_SavedLocation, 1.0));
            }
            return TestTrue(TEXT("client worlds remain distinct"),
                ck::auto_test::net::Get_ClientWorld(0) != ck::auto_test::net::Get_ClientWorld(1));
        }), TEXT("two independent clients observe fresh saved respawn")));
    // A second authority mutation proves the post-load driver remains live.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer)
        {
            auto Attr = M2b2b_ResolveAttribute(M2b2b_FindProbe(InServer));
            if (ck::Is_NOT_Valid(Attr))
            { AddError(TEXT("Stage 8: restored authority attribute unavailable")); return; }
            UCk_Utils_FloatAttribute_UE::Request_Override(
                Attr, M2b2b_LiveFinal, ECk_MinMaxCurrent::Current, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            for (int32 Index = -1; Index < 2; ++Index)
            {
                auto* World = Index < 0 ? ck::auto_test::net::Get_ServerWorld()
                    : ck::auto_test::net::Get_ClientWorld(Index);
                const auto Attr = M2b2b_ResolveAttribute(M2b2b_FindProbe(World));
                if (!IsValid(World) || ck::Is_NOT_Valid(Attr) ||
                    !FMath::IsNearlyEqual(
                        static_cast<float>(UCk_Utils_FloatAttribute_UE::Get_FinalValue(Attr)),
                        M2b2b_LiveFinal, 0.01f))
                { return false; }
            }
            return true;

        }), ReadyTimeoutSeconds, TEXT("both clients receive a second live post-load authority mutation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            Run->Restore();
            const auto CVarRestored = Run->IsRestored();
            GM2b2b_PreServerWorld = nullptr;
            GM2b2b_PreServerProbe = nullptr;
            GM2b2b_PreServerWorldId = 0;
            GM2b2b_PreServerProbeId = 0;
            for (int32 Index = 0; Index < 2; ++Index)
            {
                GM2b2b_PreClientWorld[Index] = nullptr;
                GM2b2b_PreClientProbe[Index] = nullptr;
                GM2b2b_PreClientWorldId[Index] = 0;
                GM2b2b_PreClientProbeId[Index] = 0;
            }
            return TestTrue(TEXT("PIE worlds drained and seamless CVar value and priority restored"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty() && CVarRestored);
        }), TEXT("unconditional snapshot fixture cleanup"), true));
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
