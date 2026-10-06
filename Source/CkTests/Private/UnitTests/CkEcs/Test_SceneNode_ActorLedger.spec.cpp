#include "CkSceneNode_ActorLedgerProbe.h"

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/ScopeExit.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include <cfloat>

ACk_SceneNode_ActorLedgerProbe::ACk_SceneNode_ActorLedgerProbe()
{
    Cube = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisibleCube"));
    SetRootComponent(Cube);
    Cube->SetMobility(EComponentMobility::Movable);
    Cube->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeAsset.Succeeded()) { Cube->SetStaticMesh(CubeAsset.Object); }
}

void ACk_SceneNode_ActorLedgerProbe::BeginPlay()
{
    Super::BeginPlay();
    ++BeginPlayCount;
    const auto* ClaimLedger = Get_ActorLedger();
    const auto* Scope = GetOwner();
    const auto* Source = IsValid(Scope) ? Scope->GetOwner() : nullptr;
    CapturedCountAtBeginPlay = IsValid(ClaimLedger)
        ? UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(ClaimLedger) : -1;
    bObservedNativeClaimAtBeginPlay = IsValid(ClaimLedger) &&
        Get_ActorRole() >= 1 && Get_ActorRole() <= 17 &&
        Get_ActorGeneration() ==
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_Generation(ClaimLedger) &&
        CapturedCountAtBeginPlay > 0 &&
        IsValid(Scope) && IsValid(Source) && IsValid(GetWorld()) &&
        Source == GetWorld()->GetGameState() &&
        !GetIsReplicated() && IsValid(Cube) &&
        Cube->IsRegistered() && Cube->IsVisible() && !Cube->bHiddenInGame &&
        IsValid(Cube->GetStaticMesh());
}

ACk_SceneNode_ActorLedgerReplicatedProbe::ACk_SceneNode_ActorLedgerReplicatedProbe()
{ bReplicates = true; }

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace ck_scene_actor_ledger_test
{
    struct FState
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AGameStateBase> Source;
        TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
        TWeakObjectPtr<AActor> Scope;
        TArray<TWeakObjectPtr<ACk_SceneNode_ActorLedgerProbe>> Visuals;
        TWeakObjectPtr<AActor> Foreign;
        bool bCompleted = false;
    };

    static TArray<TSubclassOf<AActor>> MakeClasses()
    {
        TArray<TSubclassOf<AActor>> Out;
        Out.Init(ACk_SceneNode_ActorLedgerProbe::StaticClass(), 17);
        return Out;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeActorLedger,
    "Ck.SceneNode.Network.ActorLedger.FullCohort",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeActorLedger::RunTest(const FString& Parameters)
{
    using namespace ck_scene_actor_ledger_test;
    const auto S = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this, FCk_NetAutoTest_Condition::CreateLambda([]()
    {
        UWorld* const Server = ck::auto_test::net::Get_ServerWorld();
        UWorld* const Client = ck::auto_test::net::Get_ClientWorld(0);
        return IsValid(Server) && IsValid(Client) &&
            IsValid(Server->GetAuthGameMode()) &&
            IsValid(Server->GetGameState()) &&
            Server->GetGameState()->HasActorBegunPlay() &&
            IsValid(Client->GetGameState());
    }), 15.0, TEXT("authority and remote GameStates reach BeginPlay")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld* World)
    {
        if (!TestTrue(TEXT("initialized authority GameState and remote client"),
            IsValid(World) && World->GetAuthGameMode() != nullptr &&
            IsValid(World->GetGameState()) &&
            IsValid(ck::auto_test::net::Get_ClientWorld(0)) &&
            IsValid(ck::auto_test::net::Get_ClientWorld(0)->GetGameState())))
        { return; }
        auto* Source = World->GetGameState();
        S->World = World;
        S->Source = Source;
        // The production admission reads already-constructed CDOs without
        // creating them as a side effect of a purity check.
        const auto* ScopeCDO = AActor::StaticClass()->GetDefaultObject<AActor>();
        const auto* ProbeCDO =
            ACk_SceneNode_ActorLedgerProbe::StaticClass()
                ->GetDefaultObject<ACk_SceneNode_ActorLedgerProbe>();
        const auto* ReplicatedCDO =
            ACk_SceneNode_ActorLedgerReplicatedProbe::StaticClass()
                ->GetDefaultObject<ACk_SceneNode_ActorLedgerReplicatedProbe>();
        if (!TestTrue(TEXT("real probe classes have constructed default objects"),
            IsValid(ScopeCDO) && IsValid(ProbeCDO) && IsValid(ReplicatedCDO) &&
            IsValid(ProbeCDO->Cube) && IsValid(ProbeCDO->Cube->GetStaticMesh()) &&
            !ScopeCDO->GetIsReplicated() && !ProbeCDO->GetIsReplicated() &&
            ReplicatedCDO->GetIsReplicated()))
        { return; }
        auto FrozenClasses = MakeClasses();
        int32 RawAllocations = 0;
        const auto Observer = World->AddOnActorPreSpawnInitialization(
            FOnActorSpawned::FDelegate::CreateLambda([&](AActor*) { ++RawAllocations; }));
        const auto RemoveObserver = ::ScopeExitSupport::FScopeGuardSyntaxSupport() +
            [World, Observer]() { World->RemoveOnActorPreSpawnInitialization(Observer); };

        auto BadCount = FrozenClasses;
        BadCount.SetNum(16);
        auto BadClass = FrozenClasses;
        BadClass[0] = AActor::StaticClass();
        auto BadReplicated = FrozenClasses;
        BadReplicated[0] = ACk_SceneNode_ActorLedgerReplicatedProbe::StaticClass();
        const int32 BeforeInvalid = RawAllocations;
        const bool NullSourceRejected =
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
                nullptr, 1, FrozenClasses) == nullptr;
        const bool ClientSourceRejected =
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
                ck::auto_test::net::Get_ClientWorld(0)->GetGameState(),
                1, FrozenClasses) == nullptr;
        const bool BadGenerationRejected =
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
                Source, 0, FrozenClasses) == nullptr;
        const bool BadCountRejected =
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
                Source, 1, BadCount) == nullptr;
        const bool BadClassRejected =
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
                Source, 1, BadClass) == nullptr;
        const bool ReplicatedClassRejected =
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
                Source, 1, BadReplicated) == nullptr;
        TestTrue(TEXT("bad source, generation, visual cardinality/class/CDO reject preallocation"),
            NullSourceRejected && ClientSourceRejected && BadGenerationRejected &&
            BadCountRejected && BadClassRejected && ReplicatedClassRejected &&
            RawAllocations == BeforeInvalid);
        auto* Ledger = UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
            Source, 1, FrozenClasses);
        if (!TestTrue(TEXT("one real authority source claims retained ledger"),
            IsValid(Ledger) &&
            Source->GetInstanceComponents().Contains(Ledger) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_Generation(Ledger) == 1 &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) == 0))
        { return; }
        S->Ledger = Ledger;
        AActor* RejectedOut = Source;
        const FTransform Excessive(FQuat::Identity,
            FVector(DBL_MAX / 2.0, 0.0, 0.0), FVector::OneVector);
        const int32 BeforeMalformed = RawAllocations;
        TestTrue(TEXT("default and out-of-envelope pose reject before raw allocation"),
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
                nullptr, 0, FTransform::Identity, RejectedOut) && RejectedOut == nullptr &&
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
                Ledger, 0, Excessive, RejectedOut) && RejectedOut == nullptr &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) == 0 &&
            RawAllocations == BeforeMalformed &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger));
        AActor* Scope = nullptr;
        if (!TestTrue(TEXT("role zero is exact plain scope owned by authority GameState"),
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
                Ledger, 0, FTransform::Identity, Scope) && IsValid(Scope) &&
            Scope->GetClass() == AActor::StaticClass() && Scope->GetOwner() == Source &&
            !Scope->GetIsReplicated() &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) == 1))
        { return; }
        S->Scope = Scope;
        AActor* FinishedScope = Source;
        TestTrue(TEXT("finished scope only is returned and partial complete proof rejects"),
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                Ledger, 0, FinishedScope) && FinishedScope == Scope &&
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
                Ledger, true));
        for (int32 Role = 1; Role <= 17; ++Role)
        {
            AActor* Finished = nullptr;
            if (!TestTrue(*FString::Printf(TEXT("real configured visual role %d finishes"), Role),
                UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
                    Ledger, Role, FTransform(FVector(Role * 10.0, 0.0, 0.0)), Finished) &&
                IsValid(Finished) &&
                Finished->GetClass() == ACk_SceneNode_ActorLedgerProbe::StaticClass() &&
                Finished->GetOwner() == Scope && !Finished->GetIsReplicated() &&
                UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) == Role + 1))
            { return; }
            auto* Probe = Cast<ACk_SceneNode_ActorLedgerProbe>(Finished);
            AActor* FromLedger = Source;
            if (!TestTrue(TEXT("BeginPlay observed exact native claim and registered cube"),
                IsValid(Probe) && Probe->BeginPlayCount == 1 &&
                Probe->bObservedNativeClaimAtBeginPlay &&
                Probe->CapturedCountAtBeginPlay == Role + 1 &&
                Probe->Get_ActorRole() == Role && Probe->Get_ActorGeneration() == 1 &&
                Probe->Get_ActorLedger() == Ledger && IsValid(Probe->Cube) &&
                Probe->Cube->IsRegistered() && Probe->Cube->IsVisible() &&
                !Probe->Cube->bHiddenInGame && IsValid(Probe->Cube->GetStaticMesh()) &&
                UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                    Ledger, Role, FromLedger) && FromLedger == Finished))
            { return; }
            S->Visuals.Add(Probe);
        }
        S->bCompleted = TestTrue(TEXT("all 18 exact captured roles form current complete cohort"),
            S->Visuals.Num() == 17 &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) == 18 &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
                Ledger, true) && UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger));
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld* World)
    {
        auto* Ledger = S->Ledger.Get();
        auto* Scope = S->Scope.Get();
        if (!TestTrue(TEXT("real finished generation retained for ownership challenge"),
            S->bCompleted && IsValid(World) && World == S->World.Get() &&
            IsValid(Ledger) && IsValid(Scope) && S->Visuals.Num() == 17 &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(Ledger, true)))
        { return; }
        auto* Unrelated = World->SpawnActor<AActor>();
        if (!TestTrue(TEXT("test-owned unrelated actor can be destroyed"),
            IsValid(Unrelated) && Unrelated->GetOwner() == nullptr &&
            Unrelated->Destroy())) { return; }
        if (!TestTrue(TEXT("unrelated actor loss leaves complete native cohort open"),
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
                Ledger, true) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) == 18 &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_ClosureReason(Ledger).IsNone()))
        { return; }
        auto* Foreign = World->SpawnActor<AActor>();
        if (!TestTrue(TEXT("test-owned ownerless actor created outside ledger"),
            IsValid(Foreign) && Foreign->GetOwner() == nullptr)) { return; }
        S->Foreign = Foreign;
        Foreign->SetOwner(Scope);
        TestTrue(TEXT("point-in-time unknown scope owner rejects full census, never adopted"),
            Foreign->GetOwner() == Scope &&
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
                Ledger, true) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) == 18 &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_ClosureReason(Ledger).IsNone());
        Foreign->SetOwner(nullptr);
        if (!TestTrue(TEXT("removing test-owned foreign edge restores current cohort"),
            Foreign->GetOwner() == nullptr &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
                Ledger, true) && UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger)))
        { return; }
        auto* IndirectParent = S->Visuals[2].Get();
        if (!TestTrue(TEXT("indirect owner anchor is real finished visual"),
            IsValid(IndirectParent) && IndirectParent->GetOwner() == Scope))
        { return; }
        Foreign->SetOwner(IndirectParent);
        TestTrue(TEXT("indirect unknown descendant rejects full census without adoption"),
            Foreign->GetOwner() == IndirectParent &&
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
                Ledger, true) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) == 18 &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_ClosureReason(Ledger).IsNone());
        Foreign->SetOwner(nullptr);
        if (!TestTrue(TEXT("removing indirect foreign edge restores full cohort"),
            Foreign->GetOwner() == nullptr &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
                Ledger, true) && UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger)))
        { return; }
        auto* Known = S->Visuals[3].Get();
        if (!TestTrue(TEXT("known exact test-owned visual still live before external loss"),
            IsValid(Known) && Known->GetOwner() == Scope &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_Source(Ledger) == S->Source.Get()))
        { return; }
        if (!TestTrue(TEXT("external Destroy of known visual accepted"), Known->Destroy()))
        { return; }
        int32 OtherLive = 0;
        bool bOtherRolesIntact = true;
        for (int32 Role = 0; Role < S->Visuals.Num(); ++Role)
        {
            if (Role == 3) { continue; }
            auto* Other = S->Visuals[Role].Get();
            bOtherRolesIntact = bOtherRolesIntact && IsValid(Other) &&
                Other->GetOwner() == Scope &&
                Other->GetClass() == ACk_SceneNode_ActorLedgerProbe::StaticClass() &&
                !Other->IsActorBeingDestroyed();
            if (IsValid(Other)) { ++OtherLive; }
        }
        if (!TestTrue(TEXT("source retained through exact known-actor loss"),
            IsValid(S->Source.Get()))) { return; }
        int32 RejectedAllocations = 0;
        const auto RejectedObserver = World->AddOnActorPreSpawnInitialization(
            FOnActorSpawned::FDelegate::CreateLambda(
                [&](AActor*) { ++RejectedAllocations; }));
        const auto RemoveRejectedObserver =
            ::ScopeExitSupport::FScopeGuardSyntaxSupport() +
            [World, RejectedObserver]()
            { World->RemoveOnActorPreSpawnInitialization(RejectedObserver); };
        AActor* RejectedOut = Scope;
        AActor* RetainedLiveOut = nullptr;
        AActor* DestroyedOut = Scope;
        const int32 BeforeComponents = S->Source->GetInstanceComponents().Num();
        const int32 BeforeRejected = UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger);
        TestTrue(TEXT("known external loss closes synchronously without cleanup or successor claim"),
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_ClosureReason(Ledger) ==
                FName(TEXT("KnownActorDestroyed")) &&
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
                Ledger, true) && BeforeRejected == 18 && OtherLive == 16 &&
            bOtherRolesIntact &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                Ledger, 1, RetainedLiveOut) &&
            RetainedLiveOut == S->Visuals[0].Get() &&
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                Ledger, 4, DestroyedOut) && DestroyedOut == nullptr &&
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
                Ledger, 3, FTransform::Identity, RejectedOut) && RejectedOut == nullptr &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) == 18 &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
                S->Source.Get(), 2, MakeClasses()) == nullptr &&
            S->Source->GetInstanceComponents().Num() == BeforeComponents &&
            RejectedAllocations == 0 && IsValid(Scope) && IsValid(Foreign));

        TStrongObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> PinnedLedger(Ledger);
        bool bLedgerDestroyed = false;
        const auto RestoreLedger = ::ScopeExitSupport::FScopeGuardSyntaxSupport() +
            [Source = S->Source.Get(), Ledger, &bLedgerDestroyed]()
            {
                if (!bLedgerDestroyed && IsValid(Source) &&
                    !Source->IsActorBeingDestroyed() &&
                    IsValid(Ledger))
                { Source->AddInstanceComponent(Ledger); }
            };
        S->Source->RemoveInstanceComponent(Ledger);
        const bool bDetached =
            !S->Source->GetInstanceComponents().Contains(Ledger);
        const int32 BeforeDetachedAttempt = RejectedAllocations;
        auto* UnexpectedSuccessor =
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
                S->Source.Get(), 2, MakeClasses());
        TestTrue(TEXT("source lifetime claim survives instance-component removal"),
            bDetached && PinnedLedger.Get() == Ledger &&
            UnexpectedSuccessor == nullptr &&
            S->Source->GetInstanceComponents().Num() == BeforeComponents - 1 &&
            RejectedAllocations == BeforeDetachedAttempt &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) == 18 &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_ClosureReason(Ledger) ==
                FName(TEXT("KnownActorDestroyed")));
        S->Source->AddInstanceComponent(Ledger);
        TestTrue(TEXT("test restores the original retained component before another frame"),
            S->Source->GetInstanceComponents().Contains(Ledger) &&
            S->Source->GetInstanceComponents().Num() == BeforeComponents);

        // Destroy only this fixture-owned ledger after all closed-ledger reads.
        // The source's native lifetime claim must survive losing both public
        // component-list references; no ledger method/read follows Destroy.
        auto* const LiveSource = S->Source.Get();
        UActorComponent* const LedgerIdentity = Ledger;
        const int32 BeforeOwnedComponents = LiveSource->GetComponents().Num();
        const int32 BeforeInstanceComponents = LiveSource->GetInstanceComponents().Num();
        PinnedLedger.Reset();
        bLedgerDestroyed = true;
        Ledger->DestroyComponent();
        const bool bRemovedFromBothLists =
            !LiveSource->GetComponents().Contains(LedgerIdentity) &&
            !LiveSource->GetInstanceComponents().Contains(LedgerIdentity) &&
            LiveSource->GetComponents().Num() == BeforeOwnedComponents - 1 &&
            LiveSource->GetInstanceComponents().Num() == BeforeInstanceComponents - 1;
        bool bSurvivorsIntact = IsValid(Scope) && Scope->GetOwner() == LiveSource;
        int32 SurvivingVisuals = 0;
        for (int32 Role = 0; Role < S->Visuals.Num(); ++Role)
        {
            if (Role == 3) { continue; }
            auto* const Other = S->Visuals[Role].Get();
            if (IsValid(Other)) { ++SurvivingVisuals; }
            bSurvivorsIntact = bSurvivorsIntact && IsValid(Other) &&
                Other->GetOwner() == Scope &&
                Other->GetClass() == ACk_SceneNode_ActorLedgerProbe::StaticClass() &&
                !Other->IsActorBeingDestroyed();
        }
        const int32 BeforeDestroyedClaimAttempt = RejectedAllocations;
        auto* const PostDestroySuccessor =
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
                LiveSource, 2, MakeClasses());
        TestTrue(TEXT("source lifetime claim survives complete ledger component destruction"),
            bRemovedFromBothLists && IsValid(LiveSource) &&
            LiveSource->GetWorld() == World && LiveSource->HasAuthority() &&
            World->GetGameState() == LiveSource &&
            PostDestroySuccessor == nullptr &&
            RejectedAllocations == BeforeDestroyedClaimAttempt &&
            LiveSource->GetComponents().Num() == BeforeOwnedComponents - 1 &&
            LiveSource->GetInstanceComponents().Num() == BeforeInstanceComponents - 1 &&
            !LiveSource->GetComponents().Contains(LedgerIdentity) &&
            !LiveSource->GetInstanceComponents().Contains(LedgerIdentity) &&
            bSurvivorsIntact && SurvivingVisuals == 16 && IsValid(Foreign));
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
#endif
