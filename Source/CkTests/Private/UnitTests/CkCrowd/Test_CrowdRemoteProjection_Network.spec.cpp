#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCrowdGym_RemoteSampler.h"
#include "CkCrowdRemoteBodyProjector.h"
#include "CkCrowd/Agent/CkCrowdAgent_Fragment.h"
#include "CkCrowd/Agent/CkCrowdAgent_Utils.h"
#include "CkCrowd/Settings/CkCrowd_DebugSettings.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkPmg/CkPmg_Fragment.h"
#include "CkPmg/CkPmg_Fragment_BasicShapes.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <Engine/LocalPlayer.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>
#include <Misc/AutomationTest.h>
#include <ProceduralMeshComponent.h>
#include <UObject/StrongObjectPtr.h>

namespace ck_crowd_remote_projection_test
{
    constexpr int64 Generation = 71;
    constexpr int64 FirstSequence = 101;
    constexpr int64 SecondSequence = 102;
    constexpr int64 TintSequence = 103;
    constexpr int64 PrioritySequence = 104;
    constexpr int64 EmptySequence = 105;

    auto IsLive(const FCk_Handle& Handle) -> bool
    {
        return ck::IsValid(Handle) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                Handle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    auto TryLocalContext(UWorld* World, APlayerController*& OutPC) -> bool
    {
        OutPC = nullptr;
        if (!IsValid(World) || World->GetNetMode() != NM_Client) { return false; }
        for (auto It = World->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = It->Get();
            ULocalPlayer* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
            if (IsValid(LP) && PC->IsLocalController() && PC->GetWorld() == World &&
                LP->GetWorld() == World && LP->GetPlayerController(World) == PC)
            { OutPC = PC; return true; }
        }
        return false;
    }

    auto Transient(UWorld& World) -> FCk_Handle
    { return UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(World); }

    auto DirectSet(const FCk_Handle& Owner) -> TSet<FCk_Handle>
    {
        TSet<FCk_Handle> Result;
        if (!IsLive(Owner)) { return Result; }
        for (const FCk_Handle& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Owner))
        { if (IsLive(Child)) { Result.Add(Child); } }
        return Result;
    }

    struct FClient
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<APlayerController> PC;
        TStrongObjectPtr<UCkCrowdRemoteBodyProjector> Projector;
        FCk_Handle ProjectionRoot;
        FCk_Handle Unknown;
        TArray<TWeakObjectPtr<UProceduralMeshComponent>> CapturedMeshes;
        TSet<FCk_Handle> OriginalTransientChildren;
        TSet<FCk_Handle> KnownDirectChildren;
        bool bRootCaptured = false;
        TSet<FCk_Handle> CapturedTree;
        bool bBound = false;
        bool bSubmitted = false;
        bool bInitialGeometry = false;
        bool bUpdatedGeometry = false;
        bool bTintGeometry = false;
        bool bPriorityGeometry = false;
        bool bEmpty = false;
        bool bRetired = false;
    };

    struct FRun
    {
        TWeakObjectPtr<UWorld> Server;
        FCk_Handle Owner;
        FCk_Handle Entities[2];
        FCk_Handle_CrowdAgent Agents[2];
        TSet<FCk_Handle> OriginalServerTransientChildren;
        TArray<FCkCrowdGymServerAgentRecord> Roster;
        TArray<FCkCrowdRemoteAgentRow> Rows;
        TArray<FCkCrowdRemoteAgentRow> UpdatedRows;
        TArray<FCkCrowdRemoteAgentRow> TintRows;
        TArray<FCkCrowdRemoteAgentRow> PriorityRows;
        FClient Clients[2];
        bool bServerReady = false;
        bool bSamplerValidated = false;
        bool bServerRetired = false;
    };

    auto SameColor(const FLinearColor& A, const FLinearColor& B) -> bool
    { return A.Equals(B, 0.001f); }

    auto ExpectedTint(const FCkCrowdRemoteAgentRow& Row) -> FLinearColor
    {
        if (Row.bIsPathPending)
        { return FMath::Lerp(Row.BaseColor, FLinearColor(1.0f, 0.92f, 0.20f, 1.0f), 0.55f); }
        if (Row.bIsDisabled)
        { return FMath::Lerp(Row.BaseColor, FLinearColor(0.45f, 0.45f, 0.45f, 1.0f), 0.65f); }
        return Row.BaseColor;
    }

    // Capture exact identities, not just an incremented transient-child count.
    auto TryCaptureProjectionRoot(FClient& Client, const TSet<FCk_Handle>& Before) -> bool
    {
        UWorld* World = Client.World.Get();
        if (!IsValid(World)) { return false; }
        const FCk_Handle WorldRoot = Transient(*World);
        if (!IsLive(WorldRoot)) { return false; }
        TSet<FCk_Handle> NewRoots = DirectSet(WorldRoot);
        for (const FCk_Handle& Old : Before)
        { NewRoots.Remove(Old); }
        if (NewRoots.Num() != 1) { return false; }
        Client.ProjectionRoot = *NewRoots.CreateConstIterator();
        return IsLive(Client.ProjectionRoot) &&
            Client.ProjectionRoot.Has<ck::FFragment_LifetimeOwner>() &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Client.ProjectionRoot) == WorldRoot;
    }

    auto TryInspectBodies(FClient& Client, const TArray<FCkCrowdRemoteAgentRow>& Rows,
        bool bCapture) -> bool
    {
        if (!IsLive(Client.ProjectionRoot) || Rows.Num() != 2) { return false; }
        const TSet<FCk_Handle> Bodies = DirectSet(Client.ProjectionRoot);
        if (Bodies.Num() != 2) { return false; }
        TSet<FCk_Handle> Tree;
        Tree.Add(Client.ProjectionRoot);
        for (const FCk_Handle& Body : Bodies)
        {
            if (!IsLive(Body) || UCk_Utils_CrowdAgent_UE::Has(Body) ||
                !UCk_Utils_Transform_UE::Has(Body) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Body) != Client.ProjectionRoot)
            { return false; }
            Tree.Add(Body);
            const auto Transform = UCk_Utils_Transform_UE::CastChecked(Body);
            const FTransform WorldTransform = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Transform);
            const FCkCrowdRemoteAgentRow* Match = Rows.FindByPredicate(
                [&WorldTransform](const FCkCrowdRemoteAgentRow& Row)
                { return Row.WorldTransform.Equals(WorldTransform, 0.01f); });
            if (Match == nullptr) { return false; }
            const TSet<FCk_Handle> Shapes = DirectSet(Body);
            if (Shapes.Num() != 2) { return false; }
            bool CapsuleFound = false;
            bool ConeFound = false;
            for (const FCk_Handle& Shape : Shapes)
            {
                if (!IsLive(Shape) || UCk_Utils_CrowdAgent_UE::Has(Shape) ||
                    !UCk_Utils_Transform_UE::Has(Shape) ||
                    !UCk_Utils_SceneNode_UE::Has(Shape) ||
                    !Shape.Has<ck::FFragment_Pmg_DebugShape_Common>() ||
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Shape) != Body)
                { return false; }
                const auto Node = UCk_Utils_SceneNode_UE::CastChecked(Shape);
                if (UCk_Utils_SceneNode_UE::Get_Parent(Node) != Transform) { return false; }
                const auto& Common = Shape.Get<ck::FFragment_Pmg_DebugShape_Common>();
                if (!SameColor(Common.Get_Color(), ExpectedTint(*Match))) { return false; }
                const FTransform Offset = UCk_Utils_SceneNode_UE::Get_Offset(Node);
                if (Shape.Has<ck::FFragment_Pmg_Capsule_Params>())
                {
                    const auto& P = Shape.Get<ck::FFragment_Pmg_Capsule_Params>();
                    if (CapsuleFound || P.Get_Radius() != Match->RadiusCm ||
                        P.Get_HalfHeight() != Match->HeightCm * 0.5f ||
                        !Offset.GetLocation().Equals(FVector(0.0, 0.0, Match->HeightCm * 0.5f), 0.01f))
                    { return false; }
                    CapsuleFound = true;
                }
                else if (Shape.Has<ck::FFragment_Pmg_Cone_Params>())
                {
                    const auto& P = Shape.Get<ck::FFragment_Pmg_Cone_Params>();
                    if (ConeFound || P.Get_Radius() != Match->RadiusCm * 0.36f ||
                        P.Get_Height() != Match->HeightCm * 0.5f * 0.625f ||
                        !Offset.GetLocation().Equals(FVector(Match->RadiusCm * 1.4f,
                            0.0f, Match->HeightCm * 0.5f), 0.01f))
                    { return false; }
                    ConeFound = true;
                }
                else { return false; }
                Tree.Add(Shape);
            }
            if (!CapsuleFound || !ConeFound) { return false; }
        }
        if (bCapture) { Client.CapturedTree = MoveTemp(Tree); }
        else
        {
            if (Client.CapturedTree.Num() != Tree.Num()) { return false; }
            for (const FCk_Handle& Handle : Tree)
            { if (!Client.CapturedTree.Contains(Handle)) { return false; } }
        }
        return true;
    }

    auto TryRetiredTree(const FClient& Client) -> bool
    {
        if (!Client.Projector || !Client.Projector->IsRetired()) { return false; }
        for (const FCk_Handle& Handle : Client.CapturedTree)
        { if (ck::IsValid(Handle, ck::IsValid_Policy_IncludePendingKill{})) { return false; } }
        UWorld* World = Client.World.Get();
        if (!IsValid(World) || !IsLive(Transient(*World))) { return false; }
        return !DirectSet(Transient(*World)).Contains(Client.ProjectionRoot);
    }

    auto CaptureVisibleMeshes(FClient& Client) -> bool
    {
        Client.CapturedMeshes.Reset();
        if (!Client.bRootCaptured || Client.CapturedTree.Num() != 7) { return false; }
        for (const FCk_Handle& H : Client.CapturedTree)
        {
            if (!IsLive(H) || !H.Has<ck::FFragment_Pmg_DebugShape>()) { continue; }
            if (!H.Has<ck::FFragment_Pmg_Capsule_Params>() &&
                !H.Has<ck::FFragment_Pmg_Cone_Params>()) { return false; }
            auto* Mesh = H.Get<ck::FFragment_Pmg_DebugShape>().Get_MeshComponent().Get();
            if (!IsValid(Mesh) || !Mesh->IsVisible()) { return false; }
            Client.CapturedMeshes.Add(Mesh);
        }
        return Client.CapturedMeshes.Num() == 4;
    }

    auto KnownMeshesHidden(const FClient& Client) -> bool
    {
        if (Client.CapturedMeshes.Num() != 4) { return false; }
        for (const auto& Captured : Client.CapturedMeshes)
        {
            auto* Mesh = Captured.Get();
            if (!IsValid(Mesh) || Mesh->IsVisible()) { return false; }
        }
        return true;
    }

    auto BlockedUnknownIsRetained(const FClient& Client) -> bool
    {
        UWorld* World = Client.World.Get();
        if (!IsValid(World) || !Client.Projector || !IsLive(Client.ProjectionRoot) ||
            !IsLive(Client.Unknown) || !IsLive(Transient(*World)) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Client.Unknown) ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Client.Unknown) !=
                Client.ProjectionRoot || Client.KnownDirectChildren.Num() != 2 ||
            Client.Projector->IsRetired() ||
            Client.Projector->IsReadyFor(Generation, FirstSequence) ||
            !KnownMeshesHidden(Client)) { return false; }
        auto Direct = DirectSet(Client.ProjectionRoot);
        if (Direct.Num() != 3 || !Direct.Contains(Client.Unknown)) { return false; }
        for (const auto& Known : Client.KnownDirectChildren)
        { if (!Direct.Contains(Known)) { return false; } }
        auto Roots = DirectSet(Transient(*World));
        if (!Roots.Remove(Client.ProjectionRoot) ||
            Roots.Num() != Client.OriginalTransientChildren.Num()) { return false; }
        for (const auto& Original : Client.OriginalTransientChildren)
        { if (!Roots.Contains(Original)) { return false; } }
        return true;
    }

    class FObserveBlockedUnknown final : public IAutomationLatentCommand
    {
    public:
        FObserveBlockedUnknown(FAutomationTestBase* InTest, TSharedRef<FRun> InRun)
            : Test(InTest), Run(MoveTemp(InRun)) {}
        bool Update() override
        {
            if (Started < 0.0) { Started = FPlatformTime::Seconds(); }
            if (FPlatformTime::Seconds() - Started >= 10.0)
            {
                if (Test) { Test->AddError(TEXT("foreign Crowd projection terminal observation did not advance 30 frames")); }
                return true;
            }
            if (GFrameCounter == LastFrame) { return false; }
            LastFrame = GFrameCounter;
            if (!BlockedUnknownIsRetained(Run->Clients[0]))
            {
                if (Test) { Test->AddError(TEXT("foreign Crowd projection child or original root changed during terminal observation")); }
                return true;
            }
            return ++ObservedFrames >= 30;
        }
    private:
        FAutomationTestBase* Test = nullptr;
        TSharedRef<FRun> Run;
        uint64 LastFrame = MAX_uint64;
        int32 ObservedFrames = 0;
        double Started = -1.0;
    };

    class FRestoreDrawBody final : public IAutomationLatentCommand
    {
    public:
        explicit FRestoreDrawBody(bool InOldDraw) : OldDraw(InOldDraw) {}
        bool Update() override
        {
            GetMutableDefault<UCk_Crowd_DebugSettings_UE>()->Set_DrawAgentBody(OldDraw);
            return true;
        }
    private:
        bool OldDraw = false;
    };

    // Unlike the harness's pure WaitUntil, projection requires an explicit, bounded tick.
    class FTickProjectorsUntil final : public IAutomationLatentCommand
    {
    public:
        FTickProjectorsUntil(FAutomationTestBase* Test, TSharedRef<FRun> Run,
            int64 Sequence, double TimeoutSeconds)
            : _Test(Test), _Run(MoveTemp(Run)), _Sequence(Sequence),
              _TimeoutSeconds(TimeoutSeconds) {}

        bool Update() override
        {
            if (_Started < 0.0) { _Started = FPlatformTime::Seconds(); }
            bool Complete = true;
            for (FClient& Client : _Run->Clients)
            {
                if (!Client.Projector) { Complete = false; continue; }
                Client.Projector->TickProjection();
                if (!Client.Projector->IsReadyFor(Generation, _Sequence))
                { Complete = false; }
            }
            if (Complete) { return true; }
            if (FPlatformTime::Seconds() - _Started < _TimeoutSeconds) { return false; }
            if (_Test) { _Test->AddError(TEXT("both Crowd body projectors did not settle")); }
            return true; // continue to unconditional retirement and EndPIE
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FRun> _Run;
        int64 _Sequence = 0;
        double _TimeoutSeconds = 0.0;
        double _Started = -1.0;
    };

    class FAlwaysRetire final : public IAutomationLatentCommand
    {
    public:
        explicit FAlwaysRetire(TSharedRef<FRun> Run) : _Run(MoveTemp(Run)) {}
        bool Update() override
        {
            for (FClient& Client : _Run->Clients)
            { if (Client.Projector) { Client.Projector->RequestRetire(); } }
            if (IsLive(_Run->Owner))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(_Run->Owner); }
            return true;
        }
    private:
        TSharedRef<FRun> _Run;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCrowdRemoteProjection_Network,
    "Ck.Crowd.RemoteDebug.SamplerProjector.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkCrowdRemoteProjection_Network::RunTest(const FString& Parameters) -> bool
{
    using namespace ck_crowd_remote_projection_test;
    const TSharedRef<FRun> Run = MakeShared<FRun>();
    const bool OriginalDrawBody = UCk_Utils_Crowd_DebugSettings_UE::Get_DrawAgentBody();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UWorld* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || Server->GetNetMode() == NM_Client ||
                !IsLive(Transient(*Server))) { return false; }
            for (int32 Index = 0; Index < 2; ++Index)
            {
                APlayerController* PC = nullptr;
                UWorld* Client = ck::auto_test::net::Get_ClientWorld(Index);
                if (!TryLocalContext(Client, PC) || !IsLive(Transient(*Client)) ||
                    !IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, Index)))
                { return false; }
            }
            return true;
        }), 60.0, TEXT("authority and two correlated remote local contexts ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("authority world for genuine Crowd sample"),
                IsValid(World) && World->GetNetMode() != NM_Client && IsLive(Transient(*World))))
            { return; }
            Run->Server = World;
            TArray<FCkCrowdRemoteAgentRow> Rows;
            FCkCrowdRemoteAgentDetail Detail;
            TestTrue(TEXT("empty valid server roster samples atomically"),
                FCkCrowdGymRemoteSampler::TrySample(*World, Generation, FirstSequence,
                    {}, {}, nullptr, 0, Rows, Detail));
            TestEqual(TEXT("empty roster has zero rows"), Rows.Num(), 0);
            Run->OriginalServerTransientChildren = DirectSet(Transient(*World));
            Run->Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
            if (!TestTrue(TEXT("world-owned disposable Crowd root"), IsLive(Run->Owner)))
            { return; }
            auto NewServerRoots = DirectSet(Transient(*World));
            for (const FCk_Handle& Old : Run->OriginalServerTransientChildren)
            { NewServerRoots.Remove(Old); }
            TestTrue(TEXT("exactly one captured server Crowd root was added"),
                NewServerRoots.Num() == 1 && NewServerRoots.Contains(Run->Owner));
            for (int32 Index = 0; Index < 2; ++Index)
            {
                Run->Entities[Index] = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run->Owner);
                if (!TestTrue(TEXT("server Crowd child created"), IsLive(Run->Entities[Index])))
                { return; }
                const FVector Position(Index * 300.0, 80.0, 110.0);
                auto Transform = UCk_Utils_Transform_UE::Add(Run->Entities[Index],
                    FTransform(FRotator(0.0, Index * 35.0, 0.0), Position),
                    ECk_Replication::DoesNotReplicate);
                if (!TestTrue(TEXT("server Crowd transform feature"), ck::IsValid(Transform)))
                { return; }
                Run->Agents[Index] = UCk_Utils_CrowdAgent_UE::Add(Transform,
                    FCk_CrowdAgent_Spec{42.0f + Index * 6.0f, 192.0f + Index * 12.0f});
                if (!TestTrue(TEXT("server CrowdAgent production feature"), ck::IsValid(Run->Agents[Index])))
                { return; }
                const FLinearColor Color = Index == 0
                    ? FLinearColor(0.15f, 0.7f, 0.25f, 1.0f)
                    : FLinearColor(0.7f, 0.2f, 0.75f, 1.0f);
                UCk_Utils_CrowdAgent_UE::Set_DebugColor(Run->Agents[Index], Color);
                FCkCrowdGymServerAgentRecord Entry;
                Entry.Id.WorldGeneration = Generation;
                Entry.Id.AgentSerial = Index + 1;
                Entry.Agent = Run->Agents[Index];
                Entry.ConfigurationRevision = 3;
                Run->Roster.Add(Entry);
            }
            Run->bServerReady = Run->Roster.Num() == 2;
            TestTrue(TEXT("server Crowd root owns exactly two real agent entities"),
                DirectSet(Run->Owner).Num() == 2 &&
                DirectSet(Run->Owner).Contains(Run->Entities[0]) &&
                DirectSet(Run->Owner).Contains(Run->Entities[1]));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            UWorld* World = Run->Server.Get();
            if (!Run->bServerReady || !IsValid(World)) { return false; }
            for (int32 i = 0; i < 2; ++i)
            {
                const FCk_Handle Agent = Run->Agents[i];
                if (!IsLive(Agent) || !UCk_Utils_CrowdAgent_UE::Has(Agent) ||
                    !UCk_Utils_Transform_UE::Has(Agent) ||
                    !Agent.Has<ck::FFragment_CrowdAgent_Tunables>() ||
                    !Agent.Has<ck::FFragment_CrowdAgent_PathFollow>() ||
                    !Agent.Has<ck::FFragment_CrowdAgent_DesiredVelocity>())
                { return false; }
            }
            return true;
        }), 10.0, TEXT("two server CrowdAgent/Transform dependency sets composed")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("server sample world remains exact"),
                World == Run->Server.Get() && Run->Roster.Num() == 2)) { return; }
            FCkCrowdRemoteAgentDetail Detail;
            Run->Rows.AddDefaulted(); // failure must clear a pre-existing partial output
            Run->bSamplerValidated = FCkCrowdGymRemoteSampler::TrySample(*World,
                Generation, FirstSequence, Run->Roster, {}, nullptr, 0, Run->Rows, Detail);
            if (!TestTrue(TEXT("two genuine Crowd rows sample"), Run->bSamplerValidated) ||
                !TestEqual(TEXT("two complete rows"), Run->Rows.Num(), 2)) { return; }
            for (int32 i = 0; i < 2; ++i)
            {
                const auto& Row = Run->Rows[i];
                const auto ExpectedTransform = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                    UCk_Utils_Transform_UE::CastChecked(Run->Entities[i]));
                TestTrue(TEXT("stable issued server ID"),
                    Row.Id.WorldGeneration == Generation && Row.Id.AgentSerial == i + 1 &&
                    Row.SampleSequence == FirstSequence && Row.ConfigurationRevision == 3);
                TestTrue(TEXT("row copies full current transform and position"),
                    Row.WorldTransform.Equals(ExpectedTransform, 0.01f) &&
                    Row.Position.Equals(ExpectedTransform.GetLocation(), 0.01f));
                TestTrue(TEXT("row copies authored body dimensions and color"),
                    Row.RadiusCm == 42.0f + i * 6.0f &&
                    Row.HeightCm == 192.0f + i * 12.0f &&
                    SameColor(Row.BaseColor,
                        UCk_Utils_CrowdAgent_UE::Get_DebugColor(Run->Agents[i])));
            }
            auto Reject = [this, World, Run](TArray<FCkCrowdGymServerAgentRecord> Roster,
                const TCHAR* Label)
            {
                TArray<FCkCrowdRemoteAgentRow> Out = Run->Rows;
                FCkCrowdRemoteAgentDetail Detail;
                const bool Accepted = FCkCrowdGymRemoteSampler::TrySample(*World,
                    Generation, FirstSequence, Roster, {}, nullptr, 0, Out, Detail);
                TestFalse(Label, Accepted);
                TestEqual(TEXT("rejected sampler roster clears output"), Out.Num(), 0);
            };
            { auto Bad = Run->Roster; Bad[0].Agent = {}; Reject(Bad, TEXT("default agent rejected")); }
            { auto Bad = Run->Roster; Bad[0].Agent = Run->Agents[1]; Reject(Bad, TEXT("duplicate handle rejected")); }
            { auto Bad = Run->Roster; Bad[1].Id = Bad[0].Id; Reject(Bad, TEXT("duplicate ID rejected")); }
            { auto Bad = Run->Roster; Bad[0].Agent = ck::StaticCast<FCk_Handle_CrowdAgent>(Run->Owner); Reject(Bad, TEXT("missing feature rejected")); }
            TArray<FCkCrowdRemoteAgentRow> Missing;
            FCkCrowdRemoteAgentDetail MissingDetail;
            FCkCrowdRemoteAgentId MissingId;
            MissingId.WorldGeneration = Generation;
            MissingId.AgentSerial = 999;
            TestFalse(TEXT("missing selected server ID rejects"),
                FCkCrowdGymRemoteSampler::TrySample(*World, Generation, FirstSequence,
                    Run->Roster, {}, &MissingId, 77, Missing, MissingDetail));
            TestEqual(TEXT("missing selection clears rows"), Missing.Num(), 0);
            TArray<FCkCrowdRemoteAgentRow> SelectedRows;
            FCkCrowdRemoteAgentDetail SelectedDetail;
            TestTrue(TEXT("genuine selected detail sampled with roster"),
                FCkCrowdGymRemoteSampler::TrySample(*World, Generation, FirstSequence,
                    Run->Roster, {}, &Run->Roster[0].Id, 78, SelectedRows, SelectedDetail));
            TestTrue(TEXT("selected identity and sample correlation"),
                SelectedRows.Num() == 2 &&
                SelectedDetail.Id == Run->Roster[0].Id &&
                SelectedDetail.Sample.Id == Run->Roster[0].Id &&
                SelectedDetail.SelectionRequestSerial == 78 &&
                SelectedDetail.SampleSequence == FirstSequence);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
        {
            if (!TestTrue(TEXT("server world remains live before projector rejection"),
                IsValid(World))) { return; }
            APlayerController* PC = World->GetFirstPlayerController();
            auto Projector = TStrongObjectPtr<UCkCrowdRemoteBodyProjector>(
                NewObject<UCkCrowdRemoteBodyProjector>(World));
            TestFalse(TEXT("server or listen host cannot bind client-only body projector"),
                Projector->TryBind(World, PC));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            FClient& Client = Run->Clients[0];
            APlayerController* PC = nullptr;
            if (!TestTrue(TEXT("first exact remote context"), TryLocalContext(World, PC))) { return; }
            Client.World = World; Client.PC = PC;
            TArray<FCkCrowdRemoteAgentRow> Rejected = Run->Rows;
            FCkCrowdRemoteAgentDetail Detail;
            TestFalse(TEXT("client world cannot sample authority Crowd roster"),
                FCkCrowdGymRemoteSampler::TrySample(*World, Generation, FirstSequence,
                    Run->Roster, {}, nullptr, 0, Rejected, Detail));
            TestEqual(TEXT("client sampler rejection clears rows"), Rejected.Num(), 0);
            Client.Projector.Reset(NewObject<UCkCrowdRemoteBodyProjector>(World));
            Client.bBound = Client.Projector && Client.Projector->TryBind(World, PC);
            const TSet<FCk_Handle> BeforeApply = DirectSet(Transient(*World));
            Client.bSubmitted = Client.bBound && Run->bSamplerValidated &&
                Client.Projector->TryApply(Generation, FirstSequence, Run->Rows);
            Client.bRootCaptured = Client.bSubmitted &&
                TryCaptureProjectionRoot(Client, BeforeApply);
            TestTrue(TEXT("first pure client projector admits sampled rows"), Client.bSubmitted);
            TestTrue(TEXT("first client captures sole immediate projector root"),
                Client.bRootCaptured);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            FClient& Client = Run->Clients[1];
            APlayerController* PC = nullptr;
            if (!TestTrue(TEXT("second exact remote context"), TryLocalContext(World, PC))) { return; }
            Client.World = World; Client.PC = PC;
            Client.Projector.Reset(NewObject<UCkCrowdRemoteBodyProjector>(World));
            Client.bBound = Client.Projector && Client.Projector->TryBind(World, PC);
            const TSet<FCk_Handle> BeforeApply = DirectSet(Transient(*World));
            Client.bSubmitted = Client.bBound && Run->bSamplerValidated &&
                Client.Projector->TryApply(Generation, FirstSequence, Run->Rows);
            Client.bRootCaptured = Client.bSubmitted &&
                TryCaptureProjectionRoot(Client, BeforeApply);
            TestTrue(TEXT("second pure client projector admits sampled rows"), Client.bSubmitted);
            TestTrue(TEXT("second client captures sole immediate projector root"),
                Client.bRootCaptured);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FTickProjectorsUntil(this, Run, FirstSequence, 20.0));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            bool Complete = true;
            for (int32 Index = 0; Index < 2; ++Index)
            {
                FClient& Client = Run->Clients[Index];
                UWorld* World = Client.World.Get();
                const bool RootIdentity = Client.bRootCaptured && IsValid(World) &&
                    IsLive(Client.ProjectionRoot) &&
                    Client.ProjectionRoot.Has<ck::FFragment_LifetimeOwner>() &&
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Client.ProjectionRoot) ==
                        Transient(*World);
                const bool Ready = Client.Projector &&
                    Client.Projector->IsReadyFor(Generation, FirstSequence);
                Client.bInitialGeometry = RootIdentity && Ready &&
                    TryInspectBodies(Client, Run->Rows, true);
                const FString Prefix = FString::Printf(TEXT("client %d initial projection "), Index);
                Complete &= TestTrue(Prefix + TEXT("root identity"), RootIdentity);
                Complete &= TestTrue(Prefix + TEXT("published readiness"), Ready);
                Complete &= TestTrue(Prefix + TEXT("exact capsule/cone geometry"),
                    Client.bInitialGeometry);
            }
            return Complete;
        }), TEXT("two exact local projection trees")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("client projection root captured for foreign-registry rejection"),
                World == Run->Server.Get() && IsLive(Run->Clients[0].ProjectionRoot)))
            { return; }
            auto Bad = Run->Roster;
            Bad[0].Agent = ck::StaticCast<FCk_Handle_CrowdAgent>(
                Run->Clients[0].ProjectionRoot);
            TArray<FCkCrowdRemoteAgentRow> Out = Run->Rows;
            FCkCrowdRemoteAgentDetail Detail;
            TestFalse(TEXT("server sampler rejects client-registry handle before feature reads"),
                FCkCrowdGymRemoteSampler::TrySample(*World, Generation, FirstSequence,
                    Bad, {}, nullptr, 0, Out, Detail));
            TestEqual(TEXT("foreign registry clears candidate rows"), Out.Num(), 0);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("server remains authoritative for updated sample"),
                World == Run->Server.Get() && Run->bSamplerValidated)) { return; }
            auto T = UCk_Utils_Transform_UE::CastChecked(Run->Entities[0]);
            UCk_Utils_Transform_UE::Request_SetTransform(T,
                FCk_Request_Transform_SetTransform{FTransform(FRotator(0.0, 55.0, 0.0),
                    FVector(170.0, 110.0, 130.0))}, {});
            UCk_Utils_CrowdAgent_UE::Set_DebugColor(Run->Agents[1],
                FLinearColor(0.05f, 0.4f, 0.9f, 1.0f));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            UWorld* World = Run->Server.Get();
            if (!IsValid(World) || Run->Roster.Num() != 2) { return false; }
            if (!IsLive(Run->Entities[0]) || !IsLive(Run->Agents[1]) ||
                !UCk_Utils_Transform_UE::Has(Run->Entities[0])) { return false; }
            const auto Current = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                UCk_Utils_Transform_UE::CastChecked(Run->Entities[0]));
            return !Current.Equals(Run->Rows[0].WorldTransform, 0.01f) &&
                !SameColor(UCk_Utils_CrowdAgent_UE::Get_DebugColor(Run->Agents[1]),
                    Run->Rows[1].BaseColor);
        }), 10.0, TEXT("real server transform and color produce new frozen DTOs")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            FCkCrowdRemoteAgentDetail Detail;
            const bool Complete = World == Run->Server.Get() &&
                FCkCrowdGymRemoteSampler::TrySample(*World, Generation, SecondSequence,
                    Run->Roster, {}, nullptr, 0, Run->UpdatedRows, Detail) &&
                Run->UpdatedRows.Num() == 2;
            TestTrue(TEXT("updated rows come from real authority sampler"), Complete);
        })));

    for (int32 Index = 0; Index < 2; ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Index,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Index](UWorld* World)
            {
                FClient& Client = Run->Clients[Index];
                if (!TestTrue(TEXT("projector retains exact local world"),
                    World == Client.World.Get() && Client.Projector && Client.bInitialGeometry &&
                    Run->UpdatedRows.Num() == 2))
                { return; }
                auto Bad = Run->UpdatedRows;
                Bad[1].Id = Bad[0].Id;
                TestFalse(TEXT("duplicate DTO rejects before projection mutation"),
                    Client.Projector->TryApply(Generation, SecondSequence, Bad));
                Bad = Run->UpdatedRows; Bad[1].SampleSequence++;
                TestFalse(TEXT("mixed sample sequence rejects before projection mutation"),
                    Client.Projector->TryApply(Generation, SecondSequence, Bad));
                Bad = Run->UpdatedRows; Bad[0].Id = {};
                TestFalse(TEXT("default row ID rejects before projection mutation"),
                    Client.Projector->TryApply(Generation, SecondSequence, Bad));
                Bad = Run->UpdatedRows; Bad[0].WorldTransform.SetRotation(FQuat(0, 0, 0, 0));
                TestFalse(TEXT("malformed rotation rejects before projection mutation"),
                    Client.Projector->TryApply(Generation, SecondSequence, Bad));
                TestTrue(TEXT("rejected DTO preserves prior complete lease"),
                    Client.Projector->IsReadyFor(Generation, FirstSequence) &&
                    TryInspectBodies(Client, Run->Rows, false));
                TestTrue(TEXT("complete updated DTO admitted"),
                    Client.Projector->TryApply(Generation, SecondSequence, Run->UpdatedRows));
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FTickProjectorsUntil(this, Run, SecondSequence, 15.0));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            bool Complete = true;
            for (FClient& Client : Run->Clients)
            {
                Client.bUpdatedGeometry = Client.Projector &&
                    Client.Projector->IsReadyFor(Generation, SecondSequence) &&
                    TryInspectBodies(Client, Run->UpdatedRows, false);
                Complete &= TestTrue(TEXT("updated body preserves exact identity/geometry"),
                    Client.bUpdatedGeometry);
            }
            return Complete;
        }), TEXT("two updated local projection trees")));

    // Presentation-only contract: vary valid DTO flags on copies of the genuine server sample.
    // The authority sampler above remains the source of all identity, geometry and baseline data.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("genuine two-row sample available for tint DTO contract"),
                World == Run->Server.Get() && Run->UpdatedRows.Num() == 2)) { return; }
            Run->TintRows = Run->UpdatedRows;
            for (auto& Row : Run->TintRows)
            {
                Row.SampleSequence = TintSequence;
                Row.bIsPathPending = false;
                Row.bIsDisabled = false;
            }
            Run->TintRows[0].bIsPathPending = true;
            Run->TintRows[1].bIsDisabled = true;
            Run->PriorityRows = Run->TintRows;
            for (auto& Row : Run->PriorityRows) { Row.SampleSequence = PrioritySequence; }
            Run->PriorityRows[0].bIsDisabled = true; // Pending tint wins when both flags are set.
            Run->PriorityRows[1].bIsDisabled = false; // Reverting the flag restores base color.
            TestTrue(TEXT("flag variants remain complete valid DTO rows"),
                FCkCrowdRemoteDebugValidation::IsValid(Run->TintRows[0]) &&
                FCkCrowdRemoteDebugValidation::IsValid(Run->TintRows[1]) &&
                FCkCrowdRemoteDebugValidation::IsValid(Run->PriorityRows[0]) &&
                FCkCrowdRemoteDebugValidation::IsValid(Run->PriorityRows[1]));
            TestTrue(TEXT("contract tint variants are distinct from authority base colors"),
                !SameColor(ExpectedTint(Run->TintRows[0]), Run->TintRows[0].BaseColor) &&
                !SameColor(ExpectedTint(Run->TintRows[1]), Run->TintRows[1].BaseColor) &&
                SameColor(ExpectedTint(Run->PriorityRows[0]),
                    ExpectedTint(Run->TintRows[0])) &&
                SameColor(ExpectedTint(Run->PriorityRows[1]),
                    Run->PriorityRows[1].BaseColor));
        })));
    for (int32 Index = 0; Index < 2; ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Index,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Index](UWorld* World)
            {
                FClient& Client = Run->Clients[Index];
                TestTrue(TEXT("independent client accepts path-pending and disabled DTO tints"),
                    World == Client.World.Get() && Client.Projector &&
                    Client.bUpdatedGeometry && Run->TintRows.Num() == 2 &&
                    Client.Projector->TryApply(Generation, TintSequence, Run->TintRows));
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FTickProjectorsUntil(this, Run, TintSequence, 15.0));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            bool Complete = true;
            for (FClient& Client : Run->Clients)
            {
                Client.bTintGeometry = Client.Projector &&
                    Client.Projector->IsReadyFor(Generation, TintSequence) &&
                    TryInspectBodies(Client, Run->TintRows, false);
                Complete &= TestTrue(TEXT("both DTO flag tints reached exact client shapes"),
                    Client.bTintGeometry);
            }
            return Complete;
        }), TEXT("independent path-pending and disabled body tints")));
    for (int32 Index = 0; Index < 2; ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Index,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Index](UWorld* World)
            {
                FClient& Client = Run->Clients[Index];
                TestTrue(TEXT("independent client accepts pending-priority and color-revert DTO"),
                    World == Client.World.Get() && Client.Projector &&
                    Client.bTintGeometry && Run->PriorityRows.Num() == 2 &&
                    Client.Projector->TryApply(Generation, PrioritySequence, Run->PriorityRows));
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FTickProjectorsUntil(this, Run, PrioritySequence, 15.0));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            bool Complete = true;
            for (FClient& Client : Run->Clients)
            {
                Client.bPriorityGeometry = Client.Projector &&
                    Client.Projector->IsReadyFor(Generation, PrioritySequence) &&
                    TryInspectBodies(Client, Run->PriorityRows, false);
                Complete &= TestTrue(TEXT("pending tint precedence and base-color revert"),
                    Client.bPriorityGeometry);
            }
            return Complete;
        }), TEXT("priority tint and base-color restoration")));

    for (int32 Index = 0; Index < 2; ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Index,
            FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Index](UWorld* World)
            {
                FClient& Client = Run->Clients[Index];
                TestTrue(TEXT("empty roster update accepted"),
                    World == Client.World.Get() && Client.Projector &&
                    Client.bPriorityGeometry &&
                    Client.Projector->TryApply(Generation, EmptySequence, {}));
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FTickProjectorsUntil(this, Run, EmptySequence, 15.0));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            bool Complete = true;
            for (FClient& Client : Run->Clients)
            {
                Client.bEmpty = Client.Projector &&
                    Client.Projector->IsReadyFor(Generation, EmptySequence);
                Complete &= TestTrue(TEXT("local empty roster projection ready"), Client.bEmpty);
            }
            return Complete;
        }), TEXT("both local empty rosters")));

    ADD_LATENT_AUTOMATION_COMMAND(FAlwaysRetire(Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            for (FClient& Client : Run->Clients)
            { if (!TryRetiredTree(Client)) { return false; } Client.bRetired = true; }
            for (int32 i = 0; i < 2; ++i)
            { if (ck::IsValid(Run->Entities[i], ck::IsValid_Policy_IncludePendingKill{})) { return false; } }
            Run->bServerRetired = !ck::IsValid(Run->Owner,
                ck::IsValid_Policy_IncludePendingKill{});
            return Run->bServerRetired;
        }), 20.0, TEXT("server Crowd and both captured client body trees fully retire")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("server world remains available for stale-handle probe"),
                World == Run->Server.Get())) { return; }
            TArray<FCkCrowdRemoteAgentRow> Out = Run->Rows;
            FCkCrowdRemoteAgentDetail Detail;
            TestFalse(TEXT("fully retired server Crowd member rejects stale sampling"),
                FCkCrowdGymRemoteSampler::TrySample(*World, Generation, EmptySequence + 1,
                    Run->Roster, {}, nullptr, 0, Out, Detail));
            TestEqual(TEXT("stale roster publishes no partial row"), Out.Num(), 0);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run, OriginalDrawBody]() -> bool
        {
            return TestTrue(TEXT("all sampled/projected/retired phases completed"),
                    Run->bServerReady && Run->bSamplerValidated && Run->bServerRetired &&
                    Run->Clients[0].bInitialGeometry && Run->Clients[1].bInitialGeometry &&
                    Run->Clients[0].bUpdatedGeometry && Run->Clients[1].bUpdatedGeometry &&
                    Run->Clients[0].bTintGeometry && Run->Clients[1].bTintGeometry &&
                    Run->Clients[0].bPriorityGeometry && Run->Clients[1].bPriorityGeometry &&
                    Run->Clients[0].bEmpty && Run->Clients[1].bEmpty &&
                    Run->Clients[0].bRetired && Run->Clients[1].bRetired) &&
                TestEqual(TEXT("global Crowd body debug setting preserved"),
                    UCk_Utils_Crowd_DebugSettings_UE::Get_DrawAgentBody(), OriginalDrawBody);
        }), TEXT("real server sampler and two pure-client projector lifetimes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCrowdRemoteProjection_UnknownChild_Network,
    "Ck.Crowd.RemoteDebug.ProjectorUnknownChild.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkCrowdRemoteProjection_UnknownChild_Network::RunTest(const FString& Parameters) -> bool
{
    using namespace ck_crowd_remote_projection_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Crowd projector terminal topology")); return false; }
    const TSharedRef<FRun> Run = MakeShared<FRun>();
    const bool OldDraw = UCk_Utils_Crowd_DebugSettings_UE::Get_DrawAgentBody();
    GetMutableDefault<UCk_Crowd_DebugSettings_UE>()->Set_DrawAgentBody(true);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || Server->GetNetMode() == NM_Client ||
                !IsLive(Transient(*Server)) ||
                ck::auto_test::net::Get_AllPIEWorlds().Num() != 3) { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                APlayerController* PC = nullptr;
                if (!TryLocalContext(Client, PC) || !IsLive(Transient(*Client)) ||
                    !IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I)))
                { return false; }
            }
            return true;
        }), 60.0, TEXT("three real worlds and exact owning client contexts ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("actual authority world for Crowd source sample"),
                IsValid(World) && World->GetNetMode() != NM_Client &&
                IsLive(Transient(*World)))) { return; }
            Run->Server = World;
            Run->OriginalServerTransientChildren = DirectSet(Transient(*World));
            Run->Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
            if (!TestTrue(TEXT("one real world-owned Crowd source"), IsLive(Run->Owner)))
            { return; }
            auto NewRoots = DirectSet(Transient(*World));
            for (const auto& Old : Run->OriginalServerTransientChildren) { NewRoots.Remove(Old); }
            if (!TestTrue(TEXT("exact single server source provenance"),
                NewRoots.Num() == 1 && NewRoots.Contains(Run->Owner))) { return; }
            for (int32 I = 0; I < 2; ++I)
            {
                Run->Entities[I] = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run->Owner);
                if (!TestTrue(TEXT("real Crowd source entity allocated"), IsLive(Run->Entities[I])))
                { return; }
                auto Transform = UCk_Utils_Transform_UE::Add(Run->Entities[I],
                    FTransform(FRotator(0.0, I * 35.0, 0.0), FVector(I * 300.0, 80.0, 110.0)),
                    ECk_Replication::DoesNotReplicate);
                if (!TestTrue(TEXT("real Crowd source transform"), ck::IsValid(Transform)))
                { return; }
                Run->Agents[I] = UCk_Utils_CrowdAgent_UE::Add(Transform,
                    FCk_CrowdAgent_Spec{42.0f + I * 6.0f, 192.0f + I * 12.0f});
                if (!TestTrue(TEXT("real CrowdAgent feature"), ck::IsValid(Run->Agents[I])))
                { return; }
                UCk_Utils_CrowdAgent_UE::Set_DebugColor(Run->Agents[I],
                    I == 0 ? FLinearColor(0.15f, 0.7f, 0.25f, 1.0f) :
                        FLinearColor(0.7f, 0.2f, 0.75f, 1.0f));
                FCkCrowdGymServerAgentRecord Entry;
                Entry.Id.WorldGeneration = Generation;
                Entry.Id.AgentSerial = I + 1;
                Entry.Agent = Run->Agents[I];
                Entry.ConfigurationRevision = 3;
                Run->Roster.Add(Entry);
            }
            Run->bServerReady = Run->Roster.Num() == 2 &&
                DirectSet(Run->Owner).Num() == 2;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (!Run->bServerReady) { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                const auto H = Run->Entities[I];
                if (!IsLive(H) || !UCk_Utils_Transform_UE::Has(H) ||
                    !UCk_Utils_CrowdAgent_UE::Has(H) ||
                    !H.Has<ck::FFragment_CrowdAgent_Tunables>() ||
                    !H.Has<ck::FFragment_CrowdAgent_PathFollow>() ||
                    !H.Has<ck::FFragment_CrowdAgent_DesiredVelocity>())
                { return false; }
            }
            return true;
        }), 10.0, TEXT("two real authority CrowdAgent features settled")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            FCkCrowdRemoteAgentDetail Detail;
            Run->bSamplerValidated = World == Run->Server.Get() &&
                FCkCrowdGymRemoteSampler::TrySample(*World, Generation, FirstSequence,
                    Run->Roster, {}, nullptr, 0, Run->Rows, Detail) && Run->Rows.Num() == 2;
            TestTrue(TEXT("actual full authority sample supplies client values"),
                Run->bSamplerValidated);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            auto& Client = Run->Clients[0]; APlayerController* PC = nullptr;
            if (!TestTrue(TEXT("exact owning client world for passive projector"),
                TryLocalContext(World, PC) && Run->bSamplerValidated)) { return; }
            Client.World = World; Client.PC = PC;
            Client.Projector.Reset(NewObject<UCkCrowdRemoteBodyProjector>(World));
            const auto Before = DirectSet(Transient(*World));
            Client.OriginalTransientChildren = Before;
            Client.bSubmitted = Client.Projector && Client.Projector->TryBind(World, PC) &&
                Client.Projector->TryApply(Generation, FirstSequence, Run->Rows);
            Client.bRootCaptured = Client.bSubmitted && TryCaptureProjectionRoot(Client, Before);
            TestTrue(TEXT("production client projector creates one captured source root"),
                Client.bRootCaptured);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            auto& Client = Run->Clients[0];
            if (!Client.Projector || !Client.bRootCaptured) { return false; }
            Client.Projector->TickProjection();
            return Client.Projector->IsReadyFor(Generation, FirstSequence);
        }), 20.0, TEXT("one actual owning client body tree installed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            auto& Client = Run->Clients[0];
            if (!TestTrue(TEXT("exact real capsule/cone tree initially physically visible"),
                Client.World.Get() == World && Client.Projector &&
                Client.Projector->IsReadyFor(Generation, FirstSequence) &&
                TryInspectBodies(Client, Run->Rows, true) &&
                CaptureVisibleMeshes(Client))) { return; }
            Client.KnownDirectChildren = DirectSet(Client.ProjectionRoot);
            if (!TestTrue(TEXT("two captured known direct projection bodies before foreign injection"),
                Client.KnownDirectChildren.Num() == 2)) { return; }
            Client.Unknown = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Client.ProjectionRoot);
            const auto Before = DirectSet(Transient(*World));
            if (!TestTrue(TEXT("real unexpected direct projection child admitted"),
                IsLive(Client.Unknown) &&
                UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Client.Unknown) &&
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Client.Unknown) ==
                    Client.ProjectionRoot &&
                 DirectSet(Client.ProjectionRoot).Num() == 3 &&
                Before.Contains(Client.ProjectionRoot))) { return; }
            Client.Projector->RequestRetire();
            TestTrue(TEXT("unproven projector rejects root retirement without foreign destroy"),
                IsLive(Client.ProjectionRoot) && IsLive(Client.Unknown) &&
                !Client.Projector->IsRetired() &&
                !Client.Projector->IsReadyFor(Generation, FirstSequence) &&
                !Client.Projector->TryApply(Generation, SecondSequence, Run->Rows) &&
                DirectSet(Transient(*World)).Contains(Client.ProjectionRoot));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            const auto& Client = Run->Clients[0];
            return IsLive(Client.ProjectionRoot) && IsLive(Client.Unknown) &&
                KnownMeshesHidden(Client);
        }), 10.0, TEXT("known real PMG meshes become hidden after queued processor frames")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            const auto& Client = Run->Clients[0]; auto* World = Client.World.Get();
            return TestTrue(TEXT("blocked projector stays terminal with foreign child retained"),
                IsValid(World) && BlockedUnknownIsRetained(Client));
        }), TEXT("client foreign child cannot be destroyed or hidden by a healthy replacement")));
    ADD_LATENT_AUTOMATION_COMMAND(FObserveBlockedUnknown(this, Run));
    ADD_LATENT_AUTOMATION_COMMAND(FRestoreDrawBody(OldDraw));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
