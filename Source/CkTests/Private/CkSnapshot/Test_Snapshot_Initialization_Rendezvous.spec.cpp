#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityBridge/CkEntityBridge_Fragment_Data.h"
#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Fragment_Data.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkSnapshot/Inspection/CkSnapshot_Inspection.h"
#include "CkTests/Snapshot/CkAutoTest_Snapshot_RendezvousProbe.h"
#include "CkTests/Snapshot/CkSnapshot_TestHarness_Common.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace ck_autotest_rendezvous_gate
{
    const FName Slot{TEXT("CkTest_Initialization_Rendezvous")};
    const FName Mutation{TEXT("Rendezvous.PreSaveMutation")};
    using EDisposition = ck::persistence::EInitializationDisposition;
    using FObservation = ck::FFragment_AutoTest_RendezvousObservation;

    struct FRun
    {
        TWeakObjectPtr<UGameInstance> Instance;
        TWeakObjectPtr<UWorld> SourceWorld;
        TWeakObjectPtr<UWorld> InstalledOn;
        FDelegateHandle Tick;
        uint32 ActorId = 0, PlayerId = 0, DefinitionId = 0;
    };

    // Lives only in the destination registry. The spec never holds a source handle across travel.
    struct FFragment_FalseClaim
    {
        FCk_Handle Claimant;
    };

    auto GetBridge(UWorld* InWorld) -> FCk_Handle
    {
        if (!InWorld) { return {}; }
        for (TActorIterator<ACk_AutoTest_Snapshot_BridgeRendezvousActor_UE> It{InWorld}; It; ++It)
        { return UCk_Utils_EntityBridge_UE::TryGet_Entity(*It); }
        return {};
    }

    auto GetPlayer(UWorld* InWorld) -> FCk_Handle
    {
        const auto* Controller = InWorld ? InWorld->GetFirstPlayerController() : nullptr;
        const auto* Player = Controller ? Controller->GetPlayerState<APlayerState>() : nullptr;
        return Player && Player->GetPawn() ? UCk_Utils_EntityBridge_UE::TryGet_Entity(Player->GetPawn()) : FCk_Handle{};
    }

    void BindPlayer(FRun& InRun, UWorld* InWorld)
    {
        if (!InWorld || !InWorld->HasBegunPlay() || InWorld->WorldType != EWorldType::PIE ||
            InWorld->GetNetMode() == NM_Client || InWorld->GetGameInstance() != InRun.Instance.Get() || InRun.InstalledOn == InWorld)
        { return; }
        auto* Controller = InWorld->GetFirstPlayerController();
        auto* Player = Controller ? Controller->GetPlayerState<APlayerState>() : nullptr;
        if (!Player) { return; }
        if (!Controller->GetPawn())
        {
            if (auto* NewPawn = InWorld->SpawnActor<ADefaultPawn>())
            { Controller->Possess(NewPawn); }
        }
        APawn* const Pawn = Controller->GetPawn();
        if (!Pawn || Player->GetPawn() != Pawn || !Pawn->IsPlayerControlled()) { return; }
        auto Transient = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InWorld);
        if (ck::Is_NOT_Valid(Transient)) { return; }

        if (InRun.SourceWorld != InWorld)
        {
            auto Claimant = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Transient);
            auto Identity = ck::persistence::FInitializationIdentity{};
            Identity._Kind = ck::persistence::EInitializationIdentity::Player;
            Identity._PlayerId = Player->GetUniqueId().IsValid() ? Player->GetUniqueId().ToString() : FString{};
            ck::persistence::RequestInitialization(Claimant, FCk_SaveData_EntityTags::StaticStruct(), Identity);
            Transient.AddOrGet<FFragment_FalseClaim>().Claimant = Claimant;
        }
        InRun.InstalledOn = InWorld;
        auto* Bridge = Pawn->FindComponentByClass<UCk_EntityBridge_ActorComponent_UE>();
        if (!Bridge)
        {
            Bridge = NewObject<UCk_EntityBridge_ActorComponent_UE>(Pawn);
            Pawn->AddInstanceComponent(Bridge);
            Bridge->RegisterComponent();
        }
        Bridge->Request_SpawnLinkedEntity(UCk_AutoTest_Snapshot_PlayerRendezvous_UE::StaticClass());
    }

    auto GetDefinition(UWorld* InWorld) -> FCk_Handle
    {
        const auto Owner = GetBridge(InWorld);
        if (ck::IsValid(Owner))
        {
            for (const auto Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Owner))
            {
                if (ck::IsValid(Child) && Child.Has<ck::FTag_AutoTest_DefinitionProbe>())
                { return Child; }
            }
        }
        return {};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Snapshot_Initialization_Rendezvous,
    "Ck.Snapshot.Initialization.Rendezvous", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCk_Snapshot_Initialization_Rendezvous::RunTest(const FString& /*InParameters*/)
{
    using namespace ck_autotest_rendezvous_gate;
    using namespace ck::auto_test::snapshot;
    auto Run = MakeShared<FRun>();
    auto Spec = FCk_SnapshotRoundTrip_Spec{};
    Spec.NumPIEClients = 1;
    Spec.NumCycles = 1;
    Spec.SlotName = Slot;
    Spec.Spawn = FCk_NetAutoTest_ServerAction::CreateLambda([Run](UWorld* World)
    {
        Run->Instance = World->GetGameInstance();
        Run->SourceWorld = World;
        Run->Tick = FWorldDelegates::OnWorldPreActorTick.AddLambda([Run](UWorld* TickWorld, ELevelTick, float)
        { BindPlayer(*Run, TickWorld); });
        World->SpawnActor<ACk_AutoTest_Snapshot_BridgeRendezvousActor_UE>();
    });
    Spec.SubjectReady = FCk_NetAutoTest_Assertion::CreateLambda([]()
    {
        auto* World = Get_PostTravelServerWorld();
        const auto Actor = GetBridge(World);
        const auto Player = GetPlayer(World);
        return ck::IsValid(Actor) && ck::IsValid(Player) && Actor.Has<FObservation>() && Player.Has<FObservation>()
            && Actor.Get<FObservation>().ConstructedCallbackRan && Player.Get<FObservation>().ConstructedCallbackRan;
    });
    Spec.Mutate = FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
    {
        auto Actor = GetBridge(World);
        auto Player = GetPlayer(World);
        if (!TestTrue(TEXT("Original bridged actor and possessed pawn exist"), ck::IsValid(Actor) && ck::IsValid(Player))) { return; }
        auto Definition = UCk_Utils_EntityReplicationDriver_UE::Request_BuildAndReplicate(
            Actor, FCk_EntityReplicationDriver_Spec{UCk_AutoTest_Snapshot_DefinitionRendezvous_UE::StaticClass()});
        if (!TestTrue(TEXT("Definition built synchronously under the runtime actor"), ck::IsValid(Definition))) { return; }
        Run->ActorId = static_cast<uint32>(Actor.Get_Entity().Get_ID());
        Run->PlayerId = static_cast<uint32>(Player.Get_Entity().Get_ID());
        Run->DefinitionId = static_cast<uint32>(Definition.Get_Entity().Get_ID());
        for (auto Entity : {Actor, Player, Definition})
        {
            TestTrue(TEXT("Original build declares fresh"), Entity.Get<FObservation>().DuringConstruction.Get_Disposition() == EDisposition::Fresh);
            UCk_Utils_EntityTag_UE::Add(Entity, Mutation);
        }
    });
    Spec.PostSave = FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
    {
        const auto Document = ck::snapshot::Inspect_SaveSlot(Slot);
        const auto CheckRow = [&](uint32 Id, ECk_Snapshot_V3_Provenance Expected, const TCHAR* Name)
        {
            const auto Index = Document.TryGet_EntityIndexForSavedId(Id);
            TestTrue(Name, Document.Get_Entities().IsValidIndex(Index) &&
                Document.Get_Entities()[Index].Get_Entry().Get_Provenance() == Expected);
        };
        CheckRow(Run->ActorId, ECk_Snapshot_V3_Provenance::RuntimeSpawned, TEXT("Saved actor uses the runtime bridge route"));
        CheckRow(Run->PlayerId, ECk_Snapshot_V3_Provenance::EngineOwned, TEXT("Saved player uses engine rendezvous"));
        CheckRow(Run->DefinitionId, ECk_Snapshot_V3_Provenance::DefinitionBuilt, TEXT("Saved construction uses the DefinitionBuilt route"));
        const auto Index = Document.TryGet_EntityIndexForSavedId(Run->PlayerId);
        if (Document.Get_Entities().IsValidIndex(Index))
        { TestFalse(TEXT("Player has no overriding SaveKey"), Document.Get_Entities()[Index].Get_Entry().Get_SaveKey().IsValid()); }
    });
    Spec.Assert = FCk_NetAutoTest_Assertion::CreateLambda([this]()
    {
        auto* World = Get_PostTravelServerWorld();
        auto Actor = GetBridge(World);
        auto Player = GetPlayer(World);
        auto Definition = GetDefinition(World);
        const auto* Payload = FCk_SaveData_EntityTags::StaticStruct();
        for (const auto& Subject : {Actor, Player, Definition})
        {
            if (!TestTrue(TEXT("Destination subject exists with observation"), ck::IsValid(Subject) && Subject.Has<FObservation>()))
            { continue; }
            const auto& Obs = Subject.Get<FObservation>();
            const auto Direct = Obs.DuringConstruction.Get_Disposition();
            TestTrue(TEXT("Direct construction is pending for this mapped subject"), Direct == EDisposition::BaselineExpected ||
                (Subject != Definition && Direct == EDisposition::IdentityUnresolved));
            if (Subject != Definition)
            { TestTrue(TEXT("Actual EntityScript Constructed callback observed a pending baseline"), Obs.ConstructedCallbackRan &&
                (Obs.OnConstructed.Get_Disposition() == EDisposition::BaselineExpected ||
                 Obs.OnConstructed.Get_Disposition() == EDisposition::IdentityUnresolved)); }
            const auto Final = ck::persistence::Query(Subject, Payload);
            TestTrue(TEXT("Coherent release belongs to the construction epoch"), Obs.DuringConstruction.Get_Epoch() != 0 &&
                Final.Get_Epoch() == Obs.DuringConstruction.Get_Epoch() && Final.Get_Disposition() == EDisposition::Released);
            TestTrue(TEXT("Actual saved tag mutation survived"), UCk_Utils_EntityTag_UE::Has(Subject, Mutation));
        }
        auto Transient = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(World);
        if (!TestTrue(TEXT("An unrelated entity claimed the player's ID first"), Transient.Has<FFragment_FalseClaim>()))
        { return false; }
        const auto Claimant = Transient.Get<FFragment_FalseClaim>().Claimant;
        if (!TestTrue(TEXT("False claimant survived to observe terminal result"), ck::IsValid(Claimant))) { return false; }
        TestTrue(TEXT("False claimant cannot release a saved baseline"),
            ck::persistence::Query(Claimant, Payload).Get_Disposition() == EDisposition::Unavailable);
        TestFalse(TEXT("False claimant never received the player's mutation"), UCk_Utils_EntityTag_UE::Has(Claimant, Mutation));
        return !HasAnyErrors();
    });
    EnqueueRoundTrip(this, Spec);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda([Run]()
    {
        FWorldDelegates::OnWorldPreActorTick.Remove(Run->Tick);
        return true;
    }), TEXT("Removed player fixture world observer")));
    return true;
}

#endif
