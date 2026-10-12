// Authored gym ownership gate. Fake capture exercises TX without claiming a physical mic.
#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkVoiceChat/Capture/CkVoiceChat_CaptureSource.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "VoiceModule.h"

namespace ck_tests_voice_gym_owner
{
    class FFailingCaptureSource final : public ICk_VoiceChat_CaptureSource
    {
    public:
        auto Start() -> bool override { return false; }
        auto Stop() -> void override {}
        auto DrainPcm(TArray<uint8>&) -> void override {}
    };

    auto InvokeLatch(APlayerController* InController) -> bool
    {
        auto* Function = IsValid(InController)
            ? InController->FindFunction(TEXT("Request_LatchTransmit")) : nullptr;
        if (!IsValid(Function))
        { return false; }
        InController->ProcessEvent(Function, nullptr);
        return true;
    }

    auto GetCaptureFailed(APlayerController* InController, bool& OutFailed) -> bool
    {
        auto* Function = IsValid(InController)
            ? InController->FindFunction(TEXT("Get_IsCaptureFailed")) : nullptr;
        auto* Return = IsValid(Function)
            ? FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue")) : nullptr;
        if (Return == nullptr)
        { return false; }
        TArray<uint8> Params;
        Params.SetNumZeroed(Function->ParmsSize);
        InController->ProcessEvent(Function, Params.GetData());
        OutFailed = Return->GetPropertyValue_InContainer(Params.GetData());
        return true;
    }

    constexpr auto MapPath = TEXT("/Engine/Maps/Entry");
    constexpr auto GameModePath = TEXT("/Script/Angelscript.Ck_VoiceChatGym_GameMode");
    constexpr auto PawnPath = TEXT("/Script/Angelscript.Ck_Gym_Base_Pawn");

    struct FScenario
    {
        UClass* GameModeClass = nullptr;
        UClass* PawnClass = nullptr;
        FCk_Handle_VoiceTalker ExercisedTalker;
        TWeakObjectPtr<APlayerController> ExercisedController;
        bool HadVoiceKey = false;
        bool OriginalVoiceEnabled = false;
        ECkGym_StartupMode OriginalStartupMode = ECkGym_StartupMode::Cycler;
        FString OriginalLastGym;
        TArray<FString> OriginalRecentGyms;
    };

    auto GetLocalTalker(UWorld* InWorld, const FScenario& InScenario) -> FCk_Handle_VoiceTalker
    {
        if (!IsValid(InWorld))
        { return {}; }
        for (TActorIterator<APawn> It(InWorld); It; ++It)
        {
            auto* Pawn = *It;
            if (!IsValid(Pawn) || !Pawn->IsA(InScenario.PawnClass))
            { continue; }
            auto* Controller = Cast<APlayerController>(Pawn->GetController());
            if (!IsValid(Controller) || !Controller->IsLocalController())
            { continue; }
            const auto Entity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Pawn);
            if (ck::IsValid(Entity))
            { return UCk_Utils_VoiceTalker_UE::Cast(Entity); }
        }
        return {};
    }

    auto HasIsolatedLocalTalkers(const FScenario& InScenario, int32 InExpectedWorlds) -> bool
    {
        const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
        if (Worlds.Num() != InExpectedWorlds)
        { return false; }

        for (auto* World : Worlds)
        {
            int32 AuthoredPawns = 0;
            int32 LocalPawns = 0;
            int32 Talkers = 0;
            for (TActorIterator<APawn> It(World); It; ++It)
            {
                auto* Pawn = *It;
                if (!IsValid(Pawn) || !Pawn->IsA(InScenario.PawnClass))
                { continue; }
                ++AuthoredPawns;
                const auto Entity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Pawn);
                if (!ck::IsValid(Entity))
                { return false; }

                const auto* Controller = Cast<APlayerController>(Pawn->GetController());
                const auto Local = IsValid(Controller) && Controller->IsLocalController();
                const auto HasTalker = UCk_Utils_VoiceTalker_UE::Has(Entity);
                LocalPawns += Local ? 1 : 0;
                Talkers += HasTalker ? 1 : 0;
                if (Local != HasTalker)
                { return false; }
            }
            const auto ExpectedLocal = World->GetNetMode() == NM_DedicatedServer ? 0 : 1;
            if (AuthoredPawns != 2 || LocalPawns != ExpectedLocal || Talkers != ExpectedLocal)
            { return false; }
        }
        return true;
    }

    auto RestoreVoiceSetting(const FScenario& InScenario) -> void
    {
        if (InScenario.HadVoiceKey)
        { GConfig->SetBool(TEXT("Voice"), TEXT("bEnabled"), InScenario.OriginalVoiceEnabled, GEngineIni); }
        else
        { GConfig->RemoveKey(TEXT("Voice"), TEXT("bEnabled"), GEngineIni); }
    }

    auto RestoreStartupSettings(const FScenario& InScenario) -> void
    {
        auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
        Settings->StartupMode = InScenario.OriginalStartupMode;
        Settings->LastGymName = InScenario.OriginalLastGym;
        Settings->RecentGymNames = InScenario.OriginalRecentGyms;
        Settings->SaveConfig();
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkVoiceGym_LocalOwner,
    "Ck.VoiceChat.Gym.LocalOwnerIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceGym_LocalOwner::RunTest(const FString& Parameters)
{
    using namespace ck_tests_voice_gym_owner;

    const auto Scenario = MakeShared<FScenario>();
    Scenario->GameModeClass = FSoftClassPath{GameModePath}.TryLoadClass<AGameModeBase>();
    Scenario->PawnClass = FSoftClassPath{PawnPath}.TryLoadClass<APawn>();
    if (!TestNotNull(TEXT("authored VoiceChat gym GameMode loads"), Scenario->GameModeClass) ||
        !TestNotNull(TEXT("authored gym pawn loads"), Scenario->PawnClass))
    { return false; }

    Scenario->HadVoiceKey = GConfig->GetBool(
        TEXT("Voice"), TEXT("bEnabled"), Scenario->OriginalVoiceEnabled, GEngineIni);
    GConfig->SetBool(TEXT("Voice"), TEXT("bEnabled"), true, GEngineIni);
    auto* StartupSettings = GetMutableDefault<UCkGym_StartupSettings>();
    Scenario->OriginalStartupMode = StartupSettings->StartupMode;
    Scenario->OriginalLastGym = StartupSettings->LastGymName;
    Scenario->OriginalRecentGyms = StartupSettings->RecentGymNames;
    StartupSettings->StartupMode = ECkGym_StartupMode::Cycler;
    if (FModuleManager::Get().IsModuleLoaded(TEXT("Voice")) && !FVoiceModule::Get().IsVoiceEnabled())
    { FModuleManager::Get().UnloadModule(TEXT("Voice")); }

    const auto ExpectedWorlds = Parameters == TEXT("ClientServer") ? 3 : 2;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        ExpectedWorlds, MapPath, Parameters, Scenario->GameModeClass));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(ExpectedWorlds, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Scenario, ExpectedWorlds]() -> bool
        { return HasIsolatedLocalTalkers(*Scenario, ExpectedWorlds); }),
        60.0, TEXT("authored gym composes one talker per local player and none on remote pawns")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Scenario, ExpectedWorlds]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            return TestTrue(TEXT("server uses authored VoiceChat GameMode"),
                IsValid(Server) && IsValid(Server->GetAuthGameMode()) &&
                Server->GetAuthGameMode()->IsA(Scenario->GameModeClass)) &&
                TestTrue(TEXT("local-only talkers are isolated in every world"),
                    HasIsolatedLocalTalkers(*Scenario, ExpectedWorlds));
        }), TEXT("authored voice ownership")));

    // Exercise the real local pawn's talker with the existing capture seam.
    // The other client and the server's remote pawn must stay silent.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Scenario](UWorld* InClient)
        {
            Scenario->ExercisedTalker = GetLocalTalker(InClient, *Scenario);
            if (!TestTrue(TEXT("first remote client owns its authored talker"),
                ck::IsValid(Scenario->ExercisedTalker)))
            { ck::auto_test::net::FailSession(TEXT("authored voice talker missing on local client")); return; }

            for (TActorIterator<APlayerController> It(InClient); It; ++It)
            {
                if (IsValid(*It) && It->IsLocalController())
                { Scenario->ExercisedController = *It; break; }
            }
            auto* Controller = Scenario->ExercisedController.Get();
            if (!TestTrue(TEXT("authored control-panel latch entry is reflected"),
                IsValid(Controller) && IsValid(Controller->FindFunction(TEXT("Request_LatchTransmit")))))
            { ck::auto_test::net::FailSession(TEXT("authored voice latch unavailable")); return; }
            const auto Fake = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(48000);
            Fake->Enqueue_Sine(FCk_Time{0.5f}, 0.3f, 300.0f);
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(Scenario->ExercisedTalker, Fake);
            InvokeLatch(Controller);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Scenario]() -> bool
        { return ck::IsValid(Scenario->ExercisedTalker) &&
            UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Scenario->ExercisedTalker); }),
        15.0, TEXT("fake capture opens only the chosen local player's talker")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Scenario, ExpectedWorlds]() -> bool
        {
            if (!TestTrue(TEXT("authored gym still has isolated local talkers"),
                HasIsolatedLocalTalkers(*Scenario, ExpectedWorlds)))
            { return false; }
            const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
            for (int32 Index = 0; Index < Worlds.Num(); ++Index)
            {
                if (Index == 1)
                { continue; }
                auto Talker = GetLocalTalker(Worlds[Index], *Scenario);
                if (ck::IsValid(Talker) && !TestFalse(TEXT("other local player is not transmitting"),
                    UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Talker)))
                { return false; }
            }
            return true;
        }), TEXT("fake capture stays with its owning local player")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Scenario](UWorld*)
        {
            if (!TestTrue(TEXT("control-panel latch closes transmission"),
                InvokeLatch(Scenario->ExercisedController.Get())))
            { ck::auto_test::net::FailSession(TEXT("authored voice latch unavailable on stop")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Scenario]() -> bool
        { return ck::IsValid(Scenario->ExercisedTalker) &&
            !UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Scenario->ExercisedTalker); }),
        10.0, TEXT("control-panel latch stops the owned talker")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Scenario](UWorld*)
        {
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(
                Scenario->ExercisedTalker, MakeShared<FFailingCaptureSource>());
            if (!TestTrue(TEXT("control-panel latch requests failing capture"),
                InvokeLatch(Scenario->ExercisedController.Get())))
            { ck::auto_test::net::FailSession(TEXT("authored voice latch unavailable on failure")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Scenario]() -> bool
        {
            bool Failed = false;
            return GetCaptureFailed(Scenario->ExercisedController.Get(), Failed) && Failed;
        }), 10.0, TEXT("failed capture completion reaches authored controller status")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Scenario]() -> bool
        { return TestFalse(TEXT("failed capture never enters transmitting state"),
            UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Scenario->ExercisedTalker)); }),
        TEXT("failed capture is visible and silent")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Scenario](UWorld*)
        {
            const auto Fake = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(48000);
            Fake->Enqueue_Sine(FCk_Time{0.5f}, 0.3f, 300.0f);
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(Scenario->ExercisedTalker, Fake);
            if (!TestTrue(TEXT("control-panel latch retries after capture failure"),
                InvokeLatch(Scenario->ExercisedController.Get())))
            { ck::auto_test::net::FailSession(TEXT("authored voice latch unavailable on retry")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Scenario]() -> bool
        { return UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Scenario->ExercisedTalker); }),
        10.0, TEXT("authored controller retries with good capture")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Scenario](UWorld* InClient)
        {
            auto* Controller = Scenario->ExercisedController.Get();
            auto* Restart = IsValid(Controller) ? Controller->FindFunction(TEXT("Ck_Gym_Restart")) : nullptr;
            if (!TestTrue(TEXT("authored local controller exposes gym restart"), IsValid(Restart)))
            { ck::auto_test::net::FailSession(TEXT("authored local gym restart unavailable")); return; }
            Controller->ProcessEvent(Restart, nullptr);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Scenario]() -> bool
        { return ck::IsValid(Scenario->ExercisedTalker) &&
            !UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Scenario->ExercisedTalker); }),
        10.0, TEXT("gym restart stops local transmission")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Scenario, ExpectedWorlds]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            const auto CurrentTalker = GetLocalTalker(Client, *Scenario);
            return TestTrue(TEXT("restart retains the same pawn talker instead of adding another"),
                ck::IsValid(CurrentTalker) && CurrentTalker == Scenario->ExercisedTalker) &&
                TestTrue(TEXT("restart preserves local-only ownership"),
                    HasIsolatedLocalTalkers(*Scenario, ExpectedWorlds));
        }), TEXT("restart is idempotent and local")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Scenario](UWorld*)
        {
            auto* Controller = Scenario->ExercisedController.Get();
            auto* Restart = IsValid(Controller) ? Controller->FindFunction(TEXT("Ck_Gym_Restart")) : nullptr;
            if (!TestTrue(TEXT("restart is available for queued-start rejection"), IsValid(Restart)))
            { ck::auto_test::net::FailSession(TEXT("authored voice restart unavailable")); return; }
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(
                Scenario->ExercisedTalker, MakeShared<FFailingCaptureSource>());
            if (!TestTrue(TEXT("start request queues before restart"), InvokeLatch(Controller)))
            { ck::auto_test::net::FailSession(TEXT("authored voice latch unavailable")); return; }
            Controller->ProcessEvent(Restart, nullptr);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(3));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Scenario]() -> bool
        {
            bool Failed = true;
            return TestTrue(TEXT("capture status is reflected"),
                    GetCaptureFailed(Scenario->ExercisedController.Get(), Failed)) &&
                TestFalse(TEXT("old queued completion cannot mark restarted gym failed"), Failed) &&
                TestFalse(TEXT("queued start and restart leave talker stopped"),
                    UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Scenario->ExercisedTalker));
        }), TEXT("restart invalidates an old start completion")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Scenario]() -> bool
        { RestoreVoiceSetting(*Scenario); return true; }),
        TEXT("restore Voice setting"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Scenario]() -> bool
        { RestoreStartupSettings(*Scenario); return true; }),
        TEXT("restore gym startup settings"), true));
    return true;
}

#endif
