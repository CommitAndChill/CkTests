#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && WITH_CK_DEBUG_OVERLAY

#include "CkSelectionGym_SourceComposer.h"
#include "CkSelectionGym_SourceSession.h"
#include "CkSelectionGym_Mirror.h"
#include "CkSelectionGym_WireValidation.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEntityDebugOverlay/Provider/CkDebugOverlay_ObservationBank.h"
#include "CkEntityDebugOverlay/Selection/CkDebugOverlay_SourceAdmission.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Fragment.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment.h"
#include "CkStateMachine/State/EntityScripts/CkSmState_EntityScript.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_selection_source_mirror_test
{
    constexpr auto MarkerPath = TEXT("/Script/Angelscript.Ck_SelectionGym_LabelMarker");
    constexpr auto WallPath = TEXT("/Script/Angelscript.Ck_Gym_ObstacleWall");
    constexpr auto IdlePath = TEXT("/Script/Angelscript.Ck_SelectionGym_State_Idle");

    enum class EStep : uint8
    {
        Compose, PollStage, Activate, Accept, AdvanceMirrors, Move,
        StartX, AdvanceX, FinishX, StartRetirement, AdvanceRetirement,
        FailCaptured, InjectUnknown, Done
    };

    struct FRun
    {
        TStrongObjectPtr<UClass> Marker;
        TStrongObjectPtr<UClass> Wall;
        TStrongObjectPtr<UClass> Idle;
        TStrongObjectPtr<UCkSelectionGym_SourceComposition> Composition;
        FCk_Handle AuthorityRoot;
        FCk_Handle ClientRoot[2];
        TWeakObjectPtr<UWorld> CapturedWorld[3];
        TWeakObjectPtr<AGameStateBase> CapturedState[3];
        bool CapturedWorlds = false;
        FCk_Handle ClientScenarioRoot[2];
        FCk_Handle PreviousClientScenarioRoot[2];
        FCk_Handle Unknown;
        FCkSelectionGym_WireSnapshot Wire;
        TMap<int64, FCk_Handle> BeforeX[2];
        FVector BeforeMotion = FVector::ZeroVector;
        int64 MovingKey = 0;
        int32 Scenario = 0;
        EStep Step = EStep::Compose;
        bool MoveRequested = false;
        bool XCompleted = false;
        bool Failed = false;
        bool CheckedMalformed = false;
        bool CheckedDeprecatedChannel = false;
        bool CheckedReplay = false;
        bool NegativeMode = false;
        bool NegativePassed = false;
        FString CapturedFailure;
        int32 InstalledScenarios = 0;
        int32 PhysicalRetirements = 0;
        double Deadline = 0.0;
        FString Failure;
    };

    auto Allocated(const FCk_Handle& H) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    auto ExactWorlds(FRun& R) -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        if (!IsValid(Server) || !IsValid(Server->GetGameState()) ||
            !Server->HasBegunPlay()) { return false; }
        const auto AuthorityRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*Server);
        if (!Allocated(AuthorityRoot)) { return false; }
        FCk_Handle ClientRoots[2];
        for (int32 I = 0; I != 2; ++I)
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(I);
            if (!IsValid(Client) || Client->GetNetMode() != NM_Client ||
                !Client->HasBegunPlay() || !IsValid(Client->GetGameState()))
            { return false; }
            ClientRoots[I] = UCk_Utils_EcsWorld_Subsystem_UE::
                TryGet_TransientEntityForWorld(*Client);
            if (!Allocated(ClientRoots[I]) || ClientRoots[I] == AuthorityRoot ||
                (I == 1 && ClientRoots[1] == ClientRoots[0]))
            { return false; }
        }
        if (!R.CapturedWorlds)
        {
            R.CapturedWorld[0] = Server;
            R.CapturedState[0] = Server->GetGameState();
            R.AuthorityRoot = AuthorityRoot;
            for (int32 I = 0; I != 2; ++I)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                R.CapturedWorld[I + 1] = Client;
                R.CapturedState[I + 1] = Client->GetGameState();
                R.ClientRoot[I] = ClientRoots[I];
            }
            R.CapturedWorlds = true;
            return true;
        }
        if (R.CapturedWorld[0].Get() != Server ||
            R.CapturedState[0].Get() != Server->GetGameState() ||
            R.AuthorityRoot != AuthorityRoot)
        { return false; }
        for (int32 I = 0; I != 2; ++I)
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(I);
            if (R.CapturedWorld[I + 1].Get() != Client ||
                R.CapturedState[I + 1].Get() != Client->GetGameState() ||
                R.ClientRoot[I] != ClientRoots[I])
            { return false; }
        }
        return true;
    }

    auto MovingPose(const FCkSelectionGym_WireSnapshot& W, int64& OutKey,
        FVector& Out) -> bool
    {
        OutKey = 0;
        Out = FVector::ZeroVector;
        for (const auto& Visual : W.Visuals)
        {
            if (Visual.LabelText == TEXT("Moving root"))
            {
                if (OutKey != 0 || Visual.NodeKey <= 0) { return false; }
                OutKey = Visual.NodeKey;
            }
        }
        if (OutKey <= 0) { return false; }
        for (const auto& Row : W.Nodes)
        {
            if (Row.Key == OutKey && Row.HasTransform)
            { Out = Row.WorldPose.GetLocation(); return true; }
        }
        return false;
    }

    auto PhysicalPassiveBank(FRun& R, int32 ClientIndex,
        bool CaptureX, bool CheckX, FString& Why) -> bool
    {
        auto* World = ck::auto_test::net::Get_ClientWorld(ClientIndex);
        if (!IsValid(World) || !Allocated(R.ClientRoot[ClientIndex]) ||
            !R.ClientRoot[ClientIndex].Has<FCk_DebugOverlay_ObservationBank>())
        { Why = TEXT("client observation bank unavailable"); return false; }
        const auto& Bank = R.ClientRoot[ClientIndex].Get<FCk_DebugOverlay_ObservationBank>();
        if (Bank.Sources.Num() != R.Wire.Nodes.Num() ||
            Bank.ScenarioRoot != R.ClientScenarioRoot[ClientIndex])
        { Why = TEXT("whole passive membership differs from real source rows"); return false; }
        TMap<int64, FCk_Handle> ByKey;
        TSet<FCk_Handle> Seen;
        for (const auto& Row : R.Wire.Nodes)
        {
            FCk_Handle H;
            if (!UCkSelectionGym_Mirror_UE::TryGetLocalNodeForTest(
                    World, World->GetGameState(), Row.Key, H) ||
                !Allocated(H) || H == R.AuthorityRoot ||
                H.Get_RegistryView().Get_RegistryHandle() !=
                    R.ClientRoot[ClientIndex].Get_RegistryView().Get_RegistryHandle() ||
                H.Has<ck::FFragment_Timer>() ||
                H.Has<ck::FFragment_IntegerAttribute>() ||
                H.Has<ck::FFragment_Sm>() ||
                !UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(H) ||
                !Bank.Sources.Contains(H) || Seen.Contains(H) ||
                ByKey.Contains(Row.Key))
            { Why = TEXT("wire-key source is foreign, simulated, unpublished, or aliased"); return false; }
            Seen.Add(H);
            ByKey.Add(Row.Key, H);
        }
        if (Seen.Num() != Bank.Sources.Num())
        { Why = TEXT("wire-key map omitted a published local source"); return false; }
        if (CaptureX) { R.BeforeX[ClientIndex] = MoveTemp(ByKey); }
        if (CheckX)
        {
            const auto& Before = R.BeforeX[ClientIndex];
            int32 Removed = 0;
            int32 Added = 0;
            for (const auto& Pair : Before)
            {
                const auto* Same = ByKey.Find(Pair.Key);
                if (Same == nullptr)
                {
                    ++Removed;
                    if (Allocated(Pair.Value))
                    { Why = TEXT("retired X leaf remains allocated"); return false; }
                }
                else if (*Same != Pair.Value)
                { Why = TEXT("X changed a surviving local source identity"); return false; }
            }
            for (const auto& Pair : ByKey)
            { if (!Before.Contains(Pair.Key)) { ++Added; } }
            if (Removed != 1 || Added != 1)
            { Why = TEXT("X did not replace exactly one wire-key leaf"); return false; }
        }
        Why.Reset();
        return true;
    }

    class FDrive final : public IAutomationLatentCommand
    {
    public:
        FDrive(FAutomationTestBase* InTest, TSharedRef<FRun> InRun)
            : Test(InTest), Run(InRun) {}

        bool Update() override
        {
            if (Run->Step == EStep::Done) { return true; }
            if (Run->Deadline <= 0.0)
            { Run->Deadline = FPlatformTime::Seconds() + 90.0; }
            if (FPlatformTime::Seconds() > Run->Deadline)
            { return Stop(TEXT("source-to-mirror scenario deadline expired")); }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || !ExactWorlds(*Run))
            { return Stop(TEXT("exact three-world source context lost")); }
            FString Why;
            switch (Run->Step)
            {
            case EStep::Compose:
            {
                const int32 AuthoredScenario = Run->NegativeMode ? 0 : Run->Scenario;
                if (!Run->CheckedMalformed)
                {
                    FString Rejected;
                    FCkSelectionGym_WireSnapshot Empty;
                    const bool Bad = UCkSelectionGym_Mirror_UE::TryAcceptActive(
                        nullptr, nullptr, Empty, nullptr, nullptr, Rejected);
                    ECkSelectionGym_MirrorStatus Status = ECkSelectionGym_MirrorStatus::Installed;
                    const bool HasStatus = UCkSelectionGym_Mirror_UE::TryGetStatus(
                        nullptr, nullptr, Status, Rejected);
                    UCkSelectionGym_SourceComposition* BadSource = nullptr;
                    const bool DefaultSource = UCkSelectionGym_SourceComposer_UE::
                        TryCompose(nullptr, nullptr, {}, 0, 0, 0, 0,
                            FVector::ZeroVector, Run->Marker.Get(), Run->Wall.Get(),
                            Run->Idle.Get(), BadSource, Rejected);
                    UCkSelectionGym_SourceComposition* ForeignSource = nullptr;
                    const bool Foreign = UCkSelectionGym_SourceComposer_UE::TryCompose(
                        Server, Server->GetGameState(), Run->ClientRoot[0],
                        1, 1, 1, 0, FVector::ZeroVector, Run->Marker.Get(),
                        Run->Wall.Get(), Run->Idle.Get(), ForeignSource, Rejected);
                    FCk_Handle BadKey = Run->ClientRoot[0];
                    const bool DefaultKey = UCkSelectionGym_Mirror_UE::
                        TryGetLocalNodeForTest(nullptr, nullptr, 1, BadKey);
                    if (Bad || HasStatus || Status != ECkSelectionGym_MirrorStatus::Unavailable ||
                        DefaultSource || BadSource != nullptr || Foreign ||
                        ForeignSource != nullptr || DefaultKey || Allocated(BadKey))
                    { return Stop(TEXT("default/foreign source or mirror admission was not rejected")); }
                    Run->CheckedMalformed = true;
                }
                UCkSelectionGym_SourceComposition* Raw = nullptr;
                if (!UCkSelectionGym_SourceComposer_UE::TryCompose(Server,
                    Server->GetGameState(), Run->AuthorityRoot, 1,
                    Run->Scenario + 1, Run->Scenario + 1, AuthoredScenario,
                    FVector(Run->Scenario * 4000.0, 0, 0), Run->Marker.Get(),
                    Run->Wall.Get(), Run->Idle.Get(), Raw, Why) || !IsValid(Raw))
                { return Stop(FString::Printf(TEXT("real authority compose: %s"), *Why)); }
                Run->Composition.Reset(Raw);
                Run->Step = EStep::PollStage;
                return false;
            }
            case EStep::PollStage:
            {
                const auto Outcome = UCkSelectionGym_SourceComposer_UE::
                    TryPollStage(Run->Composition.Get(), Why);
                if (Outcome == ECkSelectionGym_SourceOutcome::Invalid)
                { return Stop(FString::Printf(TEXT("real authority stage: %s"), *Why)); }
                if (Outcome == ECkSelectionGym_SourceOutcome::Pending)
                {
                    FCk_Handle PrimaryRoot;
                    if (!UCkSelectionGym_SourceComposer_UE::
                            TryGet_PrimaryBareRootForTest(
                                Run->Composition.Get(), PrimaryRoot) ||
                        !Allocated(PrimaryRoot) ||
                        UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(PrimaryRoot))
                    { return Stop(TEXT("pending real source root was lost or published before settling")); }
                }
                if (Outcome == ECkSelectionGym_SourceOutcome::Complete)
                { Run->Step = EStep::Activate; }
                return false;
            }
            case EStep::Activate:
            {
                const auto Outcome = UCkSelectionGym_SourceComposer_UE::
                    TryActivateAndSample(Run->Composition.Get(), Run->Wire, Why);
                if (Outcome != ECkSelectionGym_SourceOutcome::Complete ||
                    Run->Wire.Scenario != (Run->NegativeMode ? 0 : Run->Scenario) ||
                    !ck_selection_gym_wire::TryValidateActiveValue(Run->Wire, Why))
                { return Stop(FString::Printf(TEXT("real authority Active: %s"), *Why)); }
                if (Run->Scenario == 4 && !MovingPose(Run->Wire,
                    Run->MovingKey, Run->BeforeMotion))
                { return Stop(TEXT("real moving source absent")); }
                Run->Step = EStep::Accept;
                return false;
            }
            case EStep::Accept:
            {
                if (Run->Scenario == 0 && !Run->NegativeMode &&
                    !Run->CheckedDeprecatedChannel)
                {
                    if (Run->Wire.Visuals.IsEmpty())
                    { return Stop(TEXT("real Scenario 0 has no visual for channel rejection")); }
                    auto Malformed = Run->Wire;
                    Malformed.Visuals[0].ObjectChannel = ECC_OverlapAll_Deprecated;
                    for (int32 I = 0; I != 2; ++I)
                    {
                        auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                        auto* GS = Client->GetGameState();
                        TArray<FCk_Handle> Before, After;
                        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
                                TryGet_AllocatedDirectLifetimeDependents(
                                    Run->ClientRoot[I], Run->ClientRoot[I], Before))
                        { return Stop(TEXT("client root census unavailable before malformed intake")); }
                        const auto CountActors = [Client]()
                        {
                            int32 Count = 0;
                            for (TActorIterator<AActor> It(Client); It; ++It) { ++Count; }
                            return Count;
                        };
                        const int32 ActorsBefore = CountActors();
                        FString Rejected;
                        const bool Accepted = UCkSelectionGym_Mirror_UE::TryAcceptActive(
                            Client, GS, Malformed, Run->Marker.Get(), Run->Wall.Get(), Rejected);
                        const bool HasReason = !Rejected.IsEmpty();
                        ECkSelectionGym_MirrorStatus Status = ECkSelectionGym_MirrorStatus::Installed;
                        const bool HasStatus = UCkSelectionGym_Mirror_UE::TryGetStatus(
                            Client, GS, Status, Rejected);
                        FCkSelectionGym_WireSnapshot Installed = Run->Wire;
                        FCkSelectionGym_WireSnapshot Empty;
                        FCk_Handle InstalledRoot = Run->ClientRoot[I];
                        const bool HasInstalled = UCkSelectionGym_Mirror_UE::TryGetInstalled(
                            Client, GS, Installed, InstalledRoot, Rejected);
                        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
                                TryGet_AllocatedDirectLifetimeDependents(
                                    Run->ClientRoot[I], Run->ClientRoot[I], After) ||
                            Accepted || !HasReason || HasStatus ||
                            Status != ECkSelectionGym_MirrorStatus::Unavailable ||
                            HasInstalled || Allocated(InstalledRoot) ||
                            !FCkSelectionGym_WireSnapshot::StaticStruct()->
                                CompareScriptStruct(&Installed, &Empty, PPF_None) ||
                            Before != After || ActorsBefore != CountActors())
                        { return Stop(TEXT("deprecated visual channel created a partial client mirror")); }
                    }
                    Run->CheckedDeprecatedChannel = true;
                }
                for (int32 I = 0; I != 2; ++I)
                {
                    auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                    if (!UCkSelectionGym_Mirror_UE::TryAcceptActive(Client,
                        Client->GetGameState(), Run->Wire, Run->Marker.Get(),
                        Run->Wall.Get(), Why))
                    { return Stop(FString::Printf(TEXT("valid Active intake rejected: %s"), *Why)); }
                    ECkSelectionGym_MirrorStatus Status;
                    if (!UCkSelectionGym_Mirror_UE::TryGetStatus(Client,
                        Client->GetGameState(), Status, Why) ||
                        Status == ECkSelectionGym_MirrorStatus::Failed)
                    { return Stop(FString::Printf(TEXT("client mirror intake: %s"), *Why)); }
                    if (Status == ECkSelectionGym_MirrorStatus::Pending)
                    {
                        if (!UCkSelectionGym_Mirror_UE::TryAcceptActive(Client,
                                Client->GetGameState(), Run->Wire, Run->Marker.Get(),
                                Run->Wall.Get(), Why) || !Why.IsEmpty())
                        { return Stop(TEXT("exact accepted Pending frame replay rejected")); }
                        FCk_Handle Staged = Run->ClientRoot[I];
                        if (UCkSelectionGym_Mirror_UE::TryGetLocalNodeForTest(
                                Client, Client->GetGameState(),
                                Run->Wire.ScenarioRootKey, Staged) || Allocated(Staged))
                        { return Stop(TEXT("staged mirror key leaked before whole-bank publication")); }
                    }
                }
                Run->Step = EStep::AdvanceMirrors;
                return false;
            }
            case EStep::AdvanceMirrors:
            {
                bool Both = true;
                for (int32 I = 0; I != 2; ++I)
                {
                    auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                    UCkSelectionGym_Mirror_UE::TryAdvance(
                        Client, Client->GetGameState(), Why);
                    ECkSelectionGym_MirrorStatus Status;
                    if (!UCkSelectionGym_Mirror_UE::TryGetStatus(Client,
                        Client->GetGameState(), Status, Why) ||
                        Status == ECkSelectionGym_MirrorStatus::Failed)
                    { return Stop(FString::Printf(TEXT("client mirror advance: %s"), *Why)); }
                    Both &= Status == ECkSelectionGym_MirrorStatus::Installed;
                }
                if (!Both) { return false; }
                for (int32 I = 0; I != 2; ++I)
                {
                    auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                    FCkSelectionGym_WireSnapshot Installed;
                    FCk_Handle ScenarioRoot;
                    if (!UCkSelectionGym_Mirror_UE::TryGetInstalled(Client,
                        Client->GetGameState(), Installed, ScenarioRoot, Why) ||
                        !FCkSelectionGym_WireSnapshot::StaticStruct()->
                            CompareScriptStruct(&Installed, &Run->Wire, PPF_None) ||
                        !Allocated(ScenarioRoot) || ScenarioRoot == Run->AuthorityRoot)
                    { return Stop(FString::Printf(TEXT("installed exact value/physical proof: %s"), *Why)); }
                    Run->ClientScenarioRoot[I] = ScenarioRoot;
                    if (Allocated(Run->PreviousClientScenarioRoot[I]))
                    { return Stop(TEXT("previous client scenario root survived complete reset")); }
                    const bool CaptureX = Run->Scenario == 4 && !Run->MoveRequested;
                    const bool CheckX = Run->Scenario == 4 && Run->XCompleted &&
                        Run->BeforeX[I].Num() > 0;
                    if (!PhysicalPassiveBank(*Run, I, CaptureX, CheckX, Why))
                    { return Stop(FString::Printf(TEXT("passive client bank: %s"), *Why)); }
                }
                if (Run->ClientScenarioRoot[0] == Run->ClientScenarioRoot[1])
                { return Stop(TEXT("client scenario roots alias across worlds")); }
                if (Run->NegativeMode)
                { Run->Step = EStep::FailCaptured; return false; }
                if (Run->Scenario == 0 && !Run->CheckedReplay)
                {
                    auto Stale = Run->Wire;
                    Stale.Lifecycle = ECkSelectionGym_WireLifecycle::Failed;
                    Stale.FailureReason = TEXT("stale failure replay");
                    Stale.Nodes.Reset(); Stale.Visuals.Reset();
                    Stale.Timers.Reset(); Stale.Integers.Reset();
                    Stale.StateMachines.Reset();
                    Stale.DeclaredNodeCount = 0;
                    Stale.DeclaredVisualCount = 0;
                    Stale.DeclaredTimerCount = 0;
                    Stale.DeclaredIntegerCount = 0;
                    Stale.DeclaredStateMachineCount = 0;
                    for (int32 I = 0; I != 2; ++I)
                    {
                        auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                        ECkSelectionGym_MirrorStatus Status;
                        if (UCkSelectionGym_Mirror_UE::TryAcceptEnvelope(Client,
                                Client->GetGameState(), Stale, Why) ||
                            !UCkSelectionGym_Mirror_UE::TryGetStatus(Client,
                                Client->GetGameState(), Status, Why) ||
                            Status != ECkSelectionGym_MirrorStatus::Installed)
                        { return Stop(TEXT("stale Failed envelope closed newer bank")); }
                    }
                    Run->CheckedReplay = true;
                }
                if (Run->Scenario == 4 && !Run->MoveRequested)
                { Run->Step = EStep::Move; }
                else if (Run->Scenario == 4 && !Run->XCompleted)
                { Run->Step = EStep::StartX; }
                else
                { ++Run->InstalledScenarios; Run->Step = EStep::StartRetirement; }
                return false;
            }
            case EStep::Move:
            {
                FCkSelectionGym_WireSnapshot Moving;
                const auto Outcome = UCkSelectionGym_SourceComposer_UE::
                    TryMoveAndSample(Run->Composition.Get(),
                        0.25, Moving, Why);
                if (Outcome != ECkSelectionGym_SourceOutcome::Complete)
                { return Stop(FString::Printf(TEXT("real source motion: %s"), *Why)); }
                FVector After;
                int64 MovingKey = 0;
                if (!MovingPose(Moving, MovingKey, After) ||
                    MovingKey != Run->MovingKey ||
                    After.Equals(Run->BeforeMotion, 1.0))
                { return Stop(TEXT("real source motion had no physical displacement")); }
                Run->Wire = MoveTemp(Moving);
                Run->MoveRequested = true;
                Run->Step = EStep::Accept;
                return false;
            }
            case EStep::StartX:
            {
                if (!UCkSelectionGym_SourceComposer_UE::TryStartRespawn(
                    Run->Composition.Get(), Why))
                { return Stop(FString::Printf(TEXT("real X retirement start: %s"), *Why)); }
                if (!HoldClients(Why))
                { return Stop(FString::Printf(TEXT("X Retiring envelope: %s"), *Why)); }
                Run->Step = EStep::AdvanceX;
                return false;
            }
            case EStep::AdvanceX:
            {
                const auto Outcome = UCkSelectionGym_SourceComposer_UE::
                    TryAdvanceRespawn(Run->Composition.Get(), Why);
                if (Outcome == ECkSelectionGym_SourceOutcome::Invalid)
                { return Stop(FString::Printf(TEXT("real X retirement: %s"), *Why)); }
                if (Outcome == ECkSelectionGym_SourceOutcome::Complete)
                { Run->Step = EStep::FinishX; }
                return false;
            }
            case EStep::FinishX:
            {
                const auto Outcome = UCkSelectionGym_SourceComposer_UE::
                    TryFinishRespawn(Run->Composition.Get(), Run->Wire, Why);
                if (Outcome != ECkSelectionGym_SourceOutcome::Complete)
                { return Stop(FString::Printf(TEXT("real X replacement: %s"), *Why)); }
                Run->XCompleted = true;
                Run->Step = EStep::Accept;
                return false;
            }
            case EStep::StartRetirement:
            {
                auto* Session = UCkSelectionGym_SourceComposer_UE::
                    Get_Session(Run->Composition.Get());
                if (!IsValid(Session) ||
                    !UCkSelectionGym_SourceSession_UE::TryStartRetirement(Session, Why) ||
                    !HoldClients(Why))
                { return Stop(FString::Printf(TEXT("real R retirement start: %s"), *Why)); }
                for (int32 I = 0; I != 2; ++I)
                { Run->PreviousClientScenarioRoot[I] = Run->ClientScenarioRoot[I]; }
                Run->Step = EStep::AdvanceRetirement;
                return false;
            }
            case EStep::AdvanceRetirement:
            {
                auto* Session = UCkSelectionGym_SourceComposer_UE::
                    Get_Session(Run->Composition.Get());
                const auto Outcome = UCkSelectionGym_SourceSession_UE::
                    TryAdvanceRetirement(Session, Why);
                if (Outcome == ECkSelectionGym_SourceOutcome::Invalid)
                { return Stop(FString::Printf(TEXT("real R retirement: %s"), *Why)); }
                if (Outcome == ECkSelectionGym_SourceOutcome::Complete)
                {
                    ++Run->PhysicalRetirements;
                    Run->Composition.Reset();
                    if (++Run->Scenario == 5)
                    {
                        Run->NegativeMode = true;
                        Run->Step = EStep::Compose;
                        Run->Deadline = FPlatformTime::Seconds() + 90.0;
                        return false;
                    }
                    Run->MoveRequested = false;
                    Run->XCompleted = false;
                    Run->BeforeX[0].Reset();
                    Run->BeforeX[1].Reset();
                    Run->Step = EStep::Compose;
                    Run->Deadline = FPlatformTime::Seconds() + 90.0;
                }
                return false;
            }
            case EStep::FailCaptured:
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(0);
                auto* Peer = ck::auto_test::net::Get_ClientWorld(1);
                auto* GS = Client->GetGameState();
                FCk_Handle LocalSource;
                if (!UCkSelectionGym_Mirror_UE::TryGetLocalNodeForTest(
                    Client, GS, Run->Wire.ScenarioRootKey, LocalSource))
                { return Stop(TEXT("captured local source unavailable before close")); }
                TArray<TWeakObjectPtr<AActor>> Visuals;
                for (TActorIterator<AActor> It(Client); It; ++It)
                {
                    auto* Actor = *It;
                    if (Actor->GetOwner() == GS &&
                        (Actor->GetClass() == Run->Marker.Get() ||
                         Actor->GetClass() == Run->Wall.Get()))
                    { Visuals.Add(TWeakObjectPtr<AActor>(Actor)); }
                }
                if (Visuals.IsEmpty() || Visuals.Num() != Run->Wire.Visuals.Num())
                { return Stop(TEXT("captured client visual receipts differ from installed wire")); }
                FString WhyInvalid;
                const FString Cause = TEXT("local gym admission lost");
                if (UCkSelectionGym_Mirror_UE::TryFailCaptured(
                        nullptr, GS, Run->ClientRoot[0], Cause, WhyInvalid) ||
                    UCkSelectionGym_Mirror_UE::TryFailCaptured(
                        Client, GS, {}, Cause, WhyInvalid) ||
                    UCkSelectionGym_Mirror_UE::TryFailCaptured(
                        Client, GS, Run->ClientRoot[1], Cause, WhyInvalid) ||
                    UCkSelectionGym_Mirror_UE::TryFailCaptured(
                        Client, Peer->GetGameState(), Run->ClientRoot[0],
                        Cause, WhyInvalid))
                { return Stop(TEXT("foreign/default captured failure was accepted")); }
                for (int32 I = 0; I != 2; ++I)
                {
                    auto* World = ck::auto_test::net::Get_ClientWorld(I);
                    FCkSelectionGym_WireSnapshot Installed;
                    FCk_Handle Root;
                    if (!UCkSelectionGym_Mirror_UE::TryGetInstalled(
                            World, World->GetGameState(), Installed, Root, Why) ||
                        Root != Run->ClientScenarioRoot[I] ||
                        !FCkSelectionGym_WireSnapshot::StaticStruct()->
                            CompareScriptStruct(&Installed, &Run->Wire, PPF_None) ||
                        !PhysicalPassiveBank(*Run, I, false, false, Why))
                    { return Stop(TEXT("invalid captured failure changed installed bank")); }
                }
                FCk_Handle AuthoritySource;
                if (!UCkSelectionGym_SourceComposer_UE::TryGet_PrimaryBareRootForTest(
                        Run->Composition.Get(), AuthoritySource) ||
                    !UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(AuthoritySource))
                { return Stop(TEXT("authority source changed before local failure")); }
                FString Aliased = Cause;
                if (!UCkSelectionGym_Mirror_UE::TryFailCaptured(Client, GS,
                        Run->ClientRoot[0], Aliased, Aliased) ||
                    !Aliased.StartsWith(Cause))
                { return Stop(TEXT("exact captured local failure was not retained")); }
                Run->CapturedFailure = Aliased;
                FString Again;
                if (!UCkSelectionGym_Mirror_UE::TryFailCaptured(Client, GS,
                        Run->ClientRoot[0], TEXT("later reason"), Again) ||
                    Again != Run->CapturedFailure)
                { return Stop(TEXT("captured local failure overwrote its first reason")); }
                ECkSelectionGym_MirrorStatus Status;
                FCkSelectionGym_WireSnapshot Cleared = Run->Wire, Empty;
                FCk_Handle ClearedRoot = Run->ClientScenarioRoot[0];
                if (!UCkSelectionGym_Mirror_UE::TryGetStatus(Client, GS,
                        Status, Why) || Status != ECkSelectionGym_MirrorStatus::Failed ||
                    Why != Run->CapturedFailure ||
                    UCkSelectionGym_Mirror_UE::TryGetInstalled(Client, GS,
                        Cleared, ClearedRoot, Why) || Allocated(ClearedRoot) ||
                    !FCkSelectionGym_WireSnapshot::StaticStruct()->
                        CompareScriptStruct(&Cleared, &Empty, PPF_None) ||
                    !Allocated(LocalSource) ||
                    UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(LocalSource))
                { return Stop(TEXT("failed local bank remained installed or exposed")); }
                FCk_DebugOverlay_SourceObservation HiddenFact;
                if (FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                        LocalSource, HiddenFact))
                { return Stop(TEXT("failed local provider fact remained exposed")); }
                for (const auto& Weak : Visuals)
                {
                    auto* Actor = Weak.Get();
                    if (IsValid(Actor) && !Actor->IsActorBeingDestroyed() &&
                        (!Actor->IsHidden() || Actor->GetActorEnableCollision()))
                    { return Stop(TEXT("failed local visual remains active")); }
                }
                FCkSelectionGym_WireSnapshot PeerInstalled;
                FCk_Handle PeerRoot;
                if (!UCkSelectionGym_Mirror_UE::TryGetInstalled(Peer,
                        Peer->GetGameState(), PeerInstalled, PeerRoot, Why) ||
                    PeerRoot != Run->ClientScenarioRoot[1] ||
                    !FCkSelectionGym_WireSnapshot::StaticStruct()->
                        CompareScriptStruct(&PeerInstalled, &Run->Wire, PPF_None) ||
                    !PhysicalPassiveBank(*Run, 1, false, false, Why) ||
                    !UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(AuthoritySource))
                { return Stop(TEXT("local failure changed peer or authority source")); }
                Run->Step = EStep::InjectUnknown;
                return false;
            }
            case EStep::InjectUnknown:
            {
                FCk_Handle ExactRoot;
                if (!UCkSelectionGym_SourceComposer_UE::TryGet_PrimaryBareRootForTest(
                        Run->Composition.Get(), ExactRoot) || !Allocated(ExactRoot))
                { return Stop(TEXT("real retained source root unavailable for negative")); }
                Run->Unknown = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(ExactRoot);
                if (!Allocated(Run->Unknown))
                { return Stop(TEXT("actual unknown source child creation failed")); }
                auto* Session = UCkSelectionGym_SourceComposer_UE::
                    Get_Session(Run->Composition.Get());
                FCkSelectionGym_WireSnapshot Rejected;
                const auto Outcome = UCkSelectionGym_SourceSession_UE::TrySample(
                    Session, Rejected, Why);
                if (Outcome != ECkSelectionGym_SourceOutcome::Invalid ||
                    !Rejected.Nodes.IsEmpty() || !Rejected.Visuals.IsEmpty() ||
                    Rejected.Sequence != 0 ||
                    !Allocated(Run->Unknown) ||
                    UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                        Run->Unknown, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                    UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(ExactRoot))
                { return Stop(TEXT("unknown source child was admitted, mutated, or partially published")); }
                FCkSelectionGym_WireSnapshot Failed;
                if (!UCkSelectionGym_SourceSession_UE::TryMakeLifecycleEnvelope(
                        Session, ECkSelectionGym_WireLifecycle::Failed, Failed, Why) ||
                    Failed.Lifecycle != ECkSelectionGym_WireLifecycle::Failed ||
                    Failed.Sequence != Run->Wire.Sequence + 1 ||
                    Failed.FailureReason.IsEmpty() || !Failed.Nodes.IsEmpty() ||
                    !Failed.Visuals.IsEmpty() || !Failed.Timers.IsEmpty() ||
                    !Failed.Integers.IsEmpty() || !Failed.StateMachines.IsEmpty())
                { return Stop(TEXT("failed source emitted a partial or advanced successful value")); }
                auto* FailedClient = ck::auto_test::net::Get_ClientWorld(0);
                if (UCkSelectionGym_Mirror_UE::TryAcceptEnvelope(FailedClient,
                        FailedClient->GetGameState(), Failed, Why))
                { return Stop(TEXT("already failed local mirror accepted unrelated authority failure")); }
                for (int32 I = 0; I != 2; ++I)
                {
                    auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                    ECkSelectionGym_MirrorStatus Status;
                    FCkSelectionGym_WireSnapshot Stable;
                    FCk_Handle StableRoot;
                    if (!UCkSelectionGym_Mirror_UE::TryGetStatus(Client,
                            Client->GetGameState(), Status, Why))
                    { return Stop(TEXT("rejected authority subset lost client status")); }
                    if (I == 0)
                    {
                        if (Status != ECkSelectionGym_MirrorStatus::Failed ||
                            Why != Run->CapturedFailure ||
                            UCkSelectionGym_Mirror_UE::TryGetInstalled(Client,
                                Client->GetGameState(), Stable, StableRoot, Why))
                        { return Stop(TEXT("authority failure reopened prior local failure")); }
                    }
                    else if (Status != ECkSelectionGym_MirrorStatus::Installed ||
                        !UCkSelectionGym_Mirror_UE::TryGetInstalled(Client,
                            Client->GetGameState(), Stable, StableRoot, Why) ||
                        StableRoot != Run->ClientScenarioRoot[I] ||
                        !FCkSelectionGym_WireSnapshot::StaticStruct()->
                            CompareScriptStruct(&Stable, &Run->Wire, PPF_None))
                    { return Stop(TEXT("rejected authority subset changed healthy peer bank")); }
                }
                Run->NegativePassed = true;
                Run->Composition.Reset();
                Run->Step = EStep::Done;
                return true;
            }
            default:
                return Stop(TEXT("unknown source-to-mirror driver state"));
            }
        }

    private:
        bool HoldClients(FString& Why)
        {
            auto* Session = UCkSelectionGym_SourceComposer_UE::
                Get_Session(Run->Composition.Get());
            FCkSelectionGym_WireSnapshot Envelope;
            if (!IsValid(Session) ||
                !UCkSelectionGym_SourceSession_UE::TryMakeLifecycleEnvelope(
                    Session, ECkSelectionGym_WireLifecycle::Retiring,
                    Envelope, Why))
            { return false; }
            for (int32 I = 0; I != 2; ++I)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                if (!IsValid(Client) ||
                    !UCkSelectionGym_Mirror_UE::TryAcceptEnvelope(Client,
                        Client->GetGameState(), Envelope, Why))
                { return false; }
                ECkSelectionGym_MirrorStatus Status;
                FCkSelectionGym_WireSnapshot Installed;
                FCk_Handle Root;
                if (!UCkSelectionGym_Mirror_UE::TryGetStatus(Client,
                        Client->GetGameState(), Status, Why) ||
                    Status != ECkSelectionGym_MirrorStatus::Pending ||
                    UCkSelectionGym_Mirror_UE::TryGetInstalled(Client,
                        Client->GetGameState(), Installed, Root, Why))
                { return false; }
                const auto& WorldRoot = Run->ClientRoot[I];
                if (!Allocated(WorldRoot) ||
                    !WorldRoot.Has<FCk_DebugOverlay_ObservationBank>())
                { return false; }
                const auto& Bank = WorldRoot.Get<FCk_DebugOverlay_ObservationBank>();
                if (Bank.Sources.IsEmpty()) { return false; }
                const auto ClosedSource = Bank.Sources.CreateConstIterator().Key();
                FCk_DebugOverlay_SourceObservation HiddenFact;
                if (UCk_DebugOverlay_SourceAdmission_UE::IsAdmitted(ClosedSource) ||
                    FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                        ClosedSource, HiddenFact))
                { return false; }
                FCk_Handle HiddenKey = WorldRoot;
                if (UCkSelectionGym_Mirror_UE::TryGetLocalNodeForTest(
                        Client, Client->GetGameState(),
                        Run->Wire.ScenarioRootKey, HiddenKey) || Allocated(HiddenKey))
                { return false; }
            }
            return true;
        }

        bool Stop(const FString& Message)
        {
            Run->Failed = true;
            Run->Failure = FString::Printf(TEXT("scenario %d: %s"),
                Run->Scenario, *Message);
            Run->Composition.Reset();
            Test->AddError(Run->Failure);
            ck::auto_test::net::FailSession(Run->Failure);
            Run->Step = EStep::Done;
            return true;
        }

        FAutomationTestBase* Test = nullptr;
        TSharedRef<FRun> Run;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSelectionGym_SourceMirrorNetwork,
    "Ck.Selection.Gym.SourceMirror.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkSelectionGym_SourceMirrorNetwork::RunTest(const FString& Parameters)
{
    using namespace ck_selection_source_mirror_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Selection source mirror topology")); return false; }
    const auto Run = MakeShared<FRun>();
    Run->Marker.Reset(FSoftClassPath(MarkerPath).TryLoadClass<AActor>());
    Run->Wall.Reset(FSoftClassPath(WallPath).TryLoadClass<AActor>());
    Run->Idle.Reset(FSoftClassPath(IdlePath).TryLoadClass<UCk_SmState_EntityScript>());
    if (!Run->Marker.IsValid() || !Run->Wall.IsValid() || !Run->Idle.IsValid())
    { AddError(TEXT("actual Selection AngelScript marker/wall/idle classes unavailable")); return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool {
            for (int32 I = -1; I != 2; ++I)
            {
                auto* World = I < 0 ? ck::auto_test::net::Get_ServerWorld() :
                    ck::auto_test::net::Get_ClientWorld(I);
                if (!IsValid(World) || !World->HasBegunPlay() ||
                    !IsValid(World->GetGameState()) ||
                    !Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                        TryGet_TransientEntityForWorld(*World)))
                { return false; }
            }
            return true;
        }), 60.0, TEXT("three exact Selection ECS worlds and GameStates ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FDrive(this, Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() -> bool {
            return !Run->Failed && Run->InstalledScenarios == 5 &&
                Run->PhysicalRetirements == 5 &&
                Run->NegativePassed && Allocated(Run->Unknown) &&
                Run->Step == EStep::Done && !Run->Composition.IsValid();
        }), TEXT("five real source banks mirrored and retired; unknown child rejected without client mutation"),
        true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool {
            return ck::auto_test::net::Get_AllPIEWorlds().IsEmpty();
        }), TEXT("source-to-mirror fixture released every PIE world"), true));
    return true;
}

#endif
