#include "CoreMinimal.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkGym_StartupSettings.h"
#include "CkSceneNodeGym_RemoteVisuals.h"
#include "CkSceneNodeGym_ActorLedger.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

struct FCk_SceneNodeRemoteVisuals_TestAccess
{
    static bool Read(const UCk_SceneNodeGym_RemoteVisuals_UE* R, int64& Sequence,
        FCkSceneNodeGym_Frame& Frame, TArray<TWeakObjectPtr<AActor>>& Actors)
    {
        if (!IsValid(R) || R->State != ECk_SceneNodeGym_RemoteVisualState::Ready || R->Roles.Num() != 18)
        { return false; }
        Sequence = R->InstalledSequence; Frame = R->InstalledFrame; Actors.Reset();
        for (const auto& Role : R->Roles) { Actors.Add(Role.Actor); }
        return Frame.WorldPoses.IsEmpty();
    }
};
namespace ck_scene_remote_test
{
    struct FState
    {
        ECkGym_StartupMode Startup{};
        FString Last;
        TArray<FString> Recent;
        bool Saved = false;
        TWeakObjectPtr<UCk_SceneNodeGym_RemoteVisuals_UE> Receipt;
        TArray<TWeakObjectPtr<AActor>> Actors;
        TWeakObjectPtr<AActor> Extra;
        ~FState() { Restore(); }
        void Restore()
        {
            if (!Saved) { return; }
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = Startup; Settings->LastGymName = Last; Settings->RecentGymNames = Recent;
            Saved = false;
        }
    };
    UCk_SceneNodeGym_RemoteVisuals_UE* Receipt(UWorld* W)
    {
        auto* Source = IsValid(W) ? W->GetGameState() : nullptr;
        const auto* Property = IsValid(Source) ? FindFProperty<FObjectPropertyBase>(Source->GetClass(), TEXT("_RemoteVisuals")) : nullptr;
        return Property ? Cast<UCk_SceneNodeGym_RemoteVisuals_UE>(Property->GetObjectPropertyValue_InContainer(Source)) : nullptr;
    }
    bool Run(FAutomationTestBase& Test, const FString& Parameters, bool Unknown, bool NestedFrozen = false)
    {
        FCkSceneNodeGym_Frame Metadata;
        Test.TestTrue(TEXT("metadata receipt rejects all pose payloads and invalid headers"),
            UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryMakeFrame(1, 1, {}, Metadata) &&
            Metadata.WorldPoses.IsEmpty() &&
            !UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryMakeFrame(1, 1, {FTransform::Identity}, Metadata) &&
            Metadata.Generation == 0 && Metadata.FrameSerial == 0 &&
            !UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryMakeFrame(0, 1, {}, Metadata));
        auto* Mode = FSoftClassPath(TEXT("/Script/Angelscript.Ck_SceneNodeGym_GameMode")).TryLoadClass<AGameModeBase>();
        if (!Test.TestNotNull(TEXT("real SceneNode gym mode"), Mode)) { return false; }
        const auto S = MakeShared<FState>();
        auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
        S->Startup = Settings->StartupMode; S->Last = Settings->LastGymName; S->Recent = Settings->RecentGymNames; S->Saved = true;
        Settings->StartupMode = ECkGym_StartupMode::Cycler;
        Test.AddCommand(new FCk_Latent_StartPIEMultiClient(2, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level"), Parameters, Mode));
        Test.AddCommand(new FCk_Latent_WaitForPIEReady(2, 60.0f));
        Test.AddCommand(new FCk_Latent_WaitUntil(&Test, FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            auto* R = Receipt(ck::auto_test::net::Get_ClientWorld(0));
            int64 Sequence = 0; FCkSceneNodeGym_Frame Frame;
            if (!FCk_SceneNodeRemoteVisuals_TestAccess::Read(R, Sequence, Frame, S->Actors) ||
                !UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_IsInstalledConformant(R, Sequence, Frame)) { return false; }
            S->Receipt = R; S->Restore(); return true;
        }), 45.0f, TEXT("bind real source actor channels and applied Transform outputs")));
        Test.AddCommand(new FCk_Latent_RunOnClient(0, FCk_NetAutoTest_ServerAction::CreateLambda([&Test, S, Unknown, NestedFrozen](UWorld* W)
        {
            auto* R = S->Receipt.Get();
            int64 Sequence = 0; FCkSceneNodeGym_Frame Frame;
            TArray<TWeakObjectPtr<AActor>> Captured;
            if (!Test.TestTrue(TEXT("captured eighteen actual production identities"),
                FCk_SceneNodeRemoteVisuals_TestAccess::Read(R, Sequence, Frame, Captured) && Captured.Num() == 18)) { return; }
            AActor* Scope = Captured[0].Get();
            Test.TestTrue(TEXT("scope is identity-only replicated source provenance"),
                IsValid(Scope) && Scope->GetClass() == ACk_SceneNodeGym_ScopeActor_UE::StaticClass() && Scope->GetIsReplicated());
            FActorSpawnParameters Params;
            if (Unknown) { Params.Owner = NestedFrozen ? Captured[1].Get() : Scope; }
            auto* Extra = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
            S->Extra = Extra;
            if (!Unknown) { Extra->Destroy(); } // Unrelated pending destruction cannot poison the cohort.
            ++Frame.FrameSerial;
            const bool Installed = NestedFrozen
                ? UCk_SceneNodeGym_RemoteVisualRuntime_UE::Request_Advance(R)
                : UCk_SceneNodeGym_RemoteVisualRuntime_UE::TryInstall(R, Sequence + 1, Frame);
            if (Unknown)
            {
                Test.TestTrue(TEXT("unknown scoped actor closes receipt and is never adopted or destroyed"),
                    !Installed && UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_State(R) == ECk_SceneNodeGym_RemoteVisualState::Unproven &&
                    IsValid(Extra) && !Extra->IsActorBeingDestroyed());
                Test.TestFalse(TEXT("unknown closure cannot authorize retirement"),
                    UCk_SceneNodeGym_RemoteVisualRuntime_UE::Request_Retire(R));
            }
            else
            {
                Test.TestTrue(TEXT("unrelated pending actor permits unchanged production bindings"), Installed);
                Test.TestTrue(TEXT("retirement closes presentation without destroying replicas"),
                    UCk_SceneNodeGym_RemoteVisualRuntime_UE::Request_Retire(R) &&
                    UCk_SceneNodeGym_RemoteVisualRuntime_UE::Get_State(R) == ECk_SceneNodeGym_RemoteVisualState::Retiring);
            }
            for (int32 Role = 0; Role < 18; ++Role)
            {
                Test.TestTrue(TEXT("receipt retains exact live actor serial; no local destruction"),
                    Captured[Role].HasSameIndexAndSerialNumber(S->Actors[Role]) &&
                    Captured[Role].IsValid() && !Captured[Role]->IsActorBeingDestroyed());
                if (Role > 0)
                {
                    const TWeakObjectPtr<UStaticMeshComponent> Mesh =
                        Cast<UStaticMeshComponent>(Captured[Role]->GetRootComponent());
                    Test.TestTrue(TEXT("closure hides exact owned production mesh"),
                        Mesh.IsValid() && Mesh->GetOwner() == Captured[Role].Get() && !Mesh->IsVisible());
                }
            }
        })));
        Test.AddCommand(new FCk_Latent_EndPIE());
        Test.AddCommand(new FCk_Latent_AssertCondition(&Test, FCk_NetAutoTest_Assertion::CreateLambda([&Test, S]()
        {
            for (const auto& Actor : S->Actors) { Test.TestFalse(TEXT("world teardown retires actual actor channels"), Actor.IsValid()); }
            S->Restore(); return true;
        }), TEXT("production replica world cleanup"), true));
        return true;
    }
}
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeRemoteUnknown,
    "Ck.SceneNode.Network.Gym.Remote.UnknownClosure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeRemoteUnknown::RunTest(const FString& Parameters)
{ return ck_scene_remote_test::Run(*this, Parameters, true); }
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeRemoteUnrelatedPending,
    "Ck.SceneNode.Network.Gym.Remote.UnrelatedPending",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeRemoteUnrelatedPending::RunTest(const FString& Parameters)
{ return ck_scene_remote_test::Run(*this, Parameters, false); }
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeRemoteNestedFrozen,
    "Ck.SceneNode.Network.Gym.Remote.NestedUnknownFrozenClosure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeRemoteNestedFrozen::RunTest(const FString& Parameters)
{ return ck_scene_remote_test::Run(*this, Parameters, true, true); }
#endif
