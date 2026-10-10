#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkAutoTest_NetSubject_Divergence.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "CkEcs/OwningActor/CkOwningActor_Utils.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace ck_divergence_net_bodies
{
    constexpr auto UntimedBody = TEXT("/Script/Angelscript.Ck_AutoTest_StateMachine_DivergenceFirstBranch");
    constexpr auto TimedBody = TEXT("/Script/Angelscript.Ck_AutoTest_StateMachine_DivergenceFirstBranchTimed");
    constexpr auto UntimedSource = TEXT("/Script/Angelscript.Ck_SmTest_DivergenceFirstBranch_GymActor");
    constexpr auto TimedSource = TEXT("/Script/Angelscript.Ck_SmTest_DivergenceTimed_GymActor");

    struct FRun
    {
        TWeakObjectPtr<UWorld> Server;
        TWeakObjectPtr<ACk_AutoTest_NetSubject_Divergence_UE> Subject;
        FString SourceClassPath;
        bool bTimed = false;
        bool bSpawnFailed = false;
    };

    bool ExactSubject(UWorld* World, ACk_AutoTest_NetSubject_Divergence_UE*& Out)
    {
        Out = nullptr;
        if (!IsInGameThread() || !IsValid(World) || World->bIsTearingDown ||
            !World->IsGameWorld()) { return false; }
        int32 Count = 0;
        for (TActorIterator<ACk_AutoTest_NetSubject> It(World); It; ++It)
        {
            auto* Candidate = Cast<ACk_AutoTest_NetSubject_Divergence_UE>(*It);
            if (!IsValid(Candidate) || Candidate->IsActorBeingDestroyed() ||
                Candidate->GetClass() != ACk_AutoTest_NetSubject_Divergence_UE::StaticClass())
            { return false; }
            Out = Candidate;
            ++Count;
        }
        return Count == 1 && Out != nullptr && Out->HasActorBegunPlay() &&
            Out->GetWorld() == World && Out->GetIsReplicated() &&
            ck::IsValid(UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Out));
    }

    bool ExactCounters(const FCk_AutoTest_DivergenceFrame& F)
    {
        return F.Snap_A_Enter == 1 && F.Snap_A_Idle == 1 &&
            F.Snap_A_Branch == 1 && F.Snap_A_Left == 1 &&
            F.Snap_A_Right == 0 && F.Snap_A_Finish == 1 &&
            F.Snap_B_Enter == 1 && F.Snap_B_Idle == 1 &&
            F.Snap_B_Branch == 1 && F.Snap_B_Left == 0 &&
            F.Snap_B_Right == 1 && F.Snap_B_Finish == 1;
    }

    bool HasNoOwnedActors(UWorld* World, const FString& SourceClassPath)
    {
        if (!IsValid(World) || World->bIsTearingDown) { return false; }
        for (TActorIterator<ACk_AutoTest_NetSubject> It(World); It; ++It)
        { if (IsValid(*It) && !It->IsActorBeingDestroyed()) { return false; } }
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (IsValid(*It) && !It->IsActorBeingDestroyed() &&
                It->GetClass()->GetPathName() == SourceClassPath)
            { return false; }
        }
        return true;
    }

    class FRetireSubject final : public IAutomationLatentCommand
    {
    public:
        explicit FRetireSubject(TSharedRef<FRun> InRun) : Run(InRun) {}

        bool Update() override
        {
            auto* World = Run->Server.Get();
            auto* Subject = Run->Subject.Get();
            if (IsValid(World) && !World->bIsTearingDown &&
                IsValid(Subject) && !Subject->IsActorBeingDestroyed() &&
                Subject->GetWorld() == World && Subject->HasAuthority() &&
                Subject->GetClass() == ACk_AutoTest_NetSubject_Divergence_UE::StaticClass())
            { Subject->Destroy(); }
            Run->Subject.Reset();
            return true;
        }

    private:
        TSharedRef<FRun> Run;
    };

    bool RunCase(FAutomationTestBase* Test, const FString& Parameters, bool bTimed)
    {
        bool Dedicated = false;
        if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
        { Test->AddError(TEXT("Divergence net body requires a supported CS/LS topology")); return false; }

        const auto BodyPath = FString{bTimed ? TimedBody : UntimedBody};
        auto* BodyClass = FSoftClassPath(BodyPath).TryLoadClass<UObject>();
        const auto* SubjectProperty = IsValid(BodyClass) ?
            FindFProperty<FClassProperty>(BodyClass, TEXT("_NetSubjectClass")) : nullptr;
        const auto* BodyCdo = IsValid(BodyClass) ? BodyClass->GetDefaultObject() : nullptr;
        const auto* ConfiguredSubject = SubjectProperty && IsValid(BodyCdo) ?
            SubjectProperty->GetPropertyValue_InContainer(BodyCdo).Get() : nullptr;
        if (!IsValid(BodyClass) || !IsValid(BodyCdo) || !SubjectProperty ||
            ConfiguredSubject != ACk_AutoTest_NetSubject_Divergence_UE::StaticClass())
        {
            Test->AddError(TEXT("Divergence body metadata lacks the exact native subject class"));
            return false;
        }

        auto Run = MakeShared<FRun>();
        Run->bTimed = bTimed;
        Run->SourceClassPath = bTimed ? TimedSource : UntimedSource;

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
            2, TEXT("/Engine/Maps/Entry"), Parameters));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, Run](UWorld* World)
            {
                if (!IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown ||
                    (World->GetNetMode() != NM_ListenServer &&
                     World->GetNetMode() != NM_DedicatedServer))
                {
                    Test->AddError(TEXT("Divergence subject has no exact authority world"));
                    Run->bSpawnFailed = true;
                    ck::auto_test::net::FailSession(TEXT("Divergence subject lacks an exact authority world"));
                    return;
                }
                auto* Probe = World->SpawnActorDeferred<ACk_AutoTest_NetSubject_Divergence_UE>(
                    ACk_AutoTest_NetSubject_Divergence_UE::StaticClass(), FTransform::Identity);
                if (!IsValid(Probe))
                {
                    Test->AddError(TEXT("Divergence malformed subject probe could not spawn"));
                    Run->bSpawnFailed = true;
                    ck::auto_test::net::FailSession(TEXT("Divergence malformed probe spawn failed"));
                    return;
                }
                Probe->SetReplicates(false);
                Probe->FinishSpawning(FTransform::Identity);
                const bool ProbeReady = IsValid(Probe) && !Probe->IsActorBeingDestroyed() &&
                    Probe->HasActorBegunPlay() && Probe->GetWorld() == World &&
                    !Probe->GetIsReplicated();
                Test->TestTrue(TEXT("malformed probe is a real non-replicating authority subject"), ProbeReady);
                if (!ProbeReady)
                {
                    if (IsValid(Probe) && !Probe->IsActorBeingDestroyed()) { Probe->Destroy(); }
                    Run->bSpawnFailed = true;
                    ck::auto_test::net::FailSession(TEXT("Divergence malformed probe lost its exact shape"));
                    return;
                }
                Test->TestFalse(TEXT("null source actor admission is rejected"),
                    Probe->TryRetain_SourceActor(nullptr, Run->bTimed));
                Test->TestFalse(TEXT("rejected subject cannot publish a completion"),
                    Probe->TryPublish_CompletedSource());
                const auto ProbeFrame = Probe->Get_Snapshot();
                Test->TestFalse(TEXT("rejected subject has no completed frame"), ProbeFrame.bCompleted);
                Test->TestEqual(TEXT("rejected subject has no source generation"), ProbeFrame.SourceGeneration, int64{0});
                Test->TestEqual(TEXT("rejected subject has no sequence"), ProbeFrame.Sequence, int64{0});
                Test->TestNull(TEXT("rejected subject has no source reference"), ProbeFrame.SourceActor.Get());
                if (!Probe->Destroy())
                {
                    Test->AddError(TEXT("malformed probe did not retire before real subject"));
                    Run->bSpawnFailed = true;
                    ck::auto_test::net::FailSession(TEXT("Divergence malformed probe did not retire"));
                    return;
                }
                FActorSpawnParameters Params;
                Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                auto* Subject = World->SpawnActor<ACk_AutoTest_NetSubject_Divergence_UE>(
                    ACk_AutoTest_NetSubject_Divergence_UE::StaticClass(), FTransform::Identity, Params);
                if (!IsValid(Subject) || Subject->GetWorld() != World ||
                    !Subject->HasAuthority() || !Subject->GetIsReplicated())
                {
                    if (IsValid(Subject) && !Subject->IsActorBeingDestroyed() &&
                        Subject->GetWorld() == World &&
                        Subject->GetClass() == ACk_AutoTest_NetSubject_Divergence_UE::StaticClass())
                    { Subject->Destroy(); }
                    Test->AddError(TEXT("Exact authority Divergence subject spawn failed"));
                    Run->bSpawnFailed = true;
                    ck::auto_test::net::FailSession(TEXT("Divergence authority subject failed its exact shape"));
                    return;
                }
                Run->Server = World;
                Run->Subject = Subject;
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([Run]()
            {
                if (Run->bSpawnFailed) { return false; }
                const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
                if (Worlds.Num() != 2 || Worlds[0] != Run->Server.Get() ||
                    Worlds[1] == Worlds[0] || !IsValid(Worlds[1]) ||
                    Worlds[1]->GetNetMode() != NM_Client) { return false; }
                for (auto* World : Worlds)
                {
                    ACk_AutoTest_NetSubject_Divergence_UE* Subject = nullptr;
                    if (!ExactSubject(World, Subject) ||
                        Subject->HasAuthority() != (World == Worlds[0])) { return false; }
                }
                return true;
            }), 30.0, TEXT("exact Divergence subject and bridged entity reach both worlds")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunAsTestOnAllWorlds(
            Test, BodyPath, 30.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(Test,
            FCk_NetAutoTest_Assertion::CreateLambda([Test, Run]()
            {
                const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
                if (Worlds.Num() != 2 || Worlds[0] != Run->Server.Get())
                { Test->AddError(TEXT("Divergence completion lost its exact PIE worlds")); return false; }
                const auto ServerSubject = Run->Subject.Get();
                if (!IsValid(ServerSubject))
                { Test->AddError(TEXT("Divergence authority subject disappeared")); return false; }
                const auto ServerFrame = ServerSubject->Get_Snapshot();
                if (!ServerFrame.bCompleted || ServerFrame.SchemaVersion != 1 ||
                    ServerFrame.SourceGeneration != 1 || ServerFrame.Sequence != 1 ||
                    ServerFrame.bTimed != Run->bTimed || ServerFrame.SourceNetIdentity <= 0 ||
                    !ExactCounters(ServerFrame))
                { Test->AddError(TEXT("Divergence authority published an incomplete or incorrect source frame")); return false; }
                for (auto* World : Worlds)
                {
                    ACk_AutoTest_NetSubject_Divergence_UE* Subject = nullptr;
                    if (!ExactSubject(World, Subject))
                    { Test->AddError(TEXT("Divergence completion lacks exact local subject bridge")); return false; }
                    const auto Frame = Subject->Get_Snapshot();
                    if (!Frame.bCompleted || Frame.SchemaVersion != 1 ||
                        Frame.SourceGeneration != ServerFrame.SourceGeneration ||
                        Frame.Sequence != ServerFrame.Sequence || Frame.bTimed != Run->bTimed ||
                        Frame.SourceNetIdentity != ServerFrame.SourceNetIdentity ||
                        !ExactCounters(Frame) || !IsValid(Frame.SourceActor.Get()) ||
                        Frame.SourceActor->IsActorBeingDestroyed() ||
                        Frame.SourceActor->GetWorld() != World ||
                        Frame.SourceActor->GetOwner() != Subject ||
                        Frame.SourceActor->GetClass()->GetPathName() != Run->SourceClassPath ||
                        ACk_AutoTest_NetSubject_Divergence_UE::Get_IrisActorIdentity(
                            Frame.SourceActor.Get()) != Frame.SourceNetIdentity)
                    { Test->AddError(TEXT("Divergence completed frame/source actor did not arrive intact")); return false; }
                }
                return true;
            }), TEXT("all-world Divergence source frames")));
        ADD_LATENT_AUTOMATION_COMMAND(FRetireSubject(Run));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([Run]()
            {
                const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
                if (Worlds.Num() != 2) { return false; }
                for (auto* World : Worlds)
                { if (!HasNoOwnedActors(World, Run->SourceClassPath)) { return false; } }
                return true;
            }), 15.0, TEXT("Divergence subject and owned source retire in both worlds")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkStateMachine_DivergenceFirstBranch_ReplicatedBody,
    "Ck.StateMachine.Gym.DivergenceFirstBranch.ReplicatedBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
        EAutomationTestFlags::EngineFilter)

bool FCkStateMachine_DivergenceFirstBranch_ReplicatedBody::RunTest(const FString& Parameters)
{ return ck_divergence_net_bodies::RunCase(this, Parameters, false); }

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkStateMachine_DivergenceFirstBranchTimed_ReplicatedBody,
    "Ck.StateMachine.Gym.DivergenceFirstBranchTimed.ReplicatedBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
        EAutomationTestFlags::EngineFilter)

bool FCkStateMachine_DivergenceFirstBranchTimed_ReplicatedBody::RunTest(const FString& Parameters)
{ return ck_divergence_net_bodies::RunCase(this, Parameters, true); }

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
