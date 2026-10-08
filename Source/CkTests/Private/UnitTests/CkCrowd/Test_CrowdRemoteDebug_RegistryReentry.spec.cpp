#include "Test_CrowdRemoteDebug_RegistryReentryProbe.h"

#include <Engine/LocalPlayer.h>
#include <Engine/World.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/PlayerController.h>
#include <Misc/AutomationTest.h>
#include <Misc/ScopeExit.h>
#include <UObject/StrongObjectPtr.h>

namespace ck_crowd_remote_debug_registry_reentry_test
{
    auto TryLocalContext(UWorld* World, APlayerController*& OutPC, ULocalPlayer*& OutLP) -> bool
    {
        OutPC = nullptr;
        OutLP = nullptr;
        if (!IsValid(World)) { return false; }
        for (auto It = World->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = It->Get();
            ULocalPlayer* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
            if (IsValid(LP) && PC->IsLocalController() && PC->GetWorld() == World &&
                LP->GetWorld() == World && LP->GetPlayerController(World) == PC)
            {
                OutPC = PC;
                OutLP = LP;
                return true;
            }
        }
        return false;
    }

    auto MakeRow() -> FCkCrowdRemoteAgentRow
    {
        FCkCrowdRemoteAgentRow Row;
        Row.Id.WorldGeneration = 101;
        Row.Id.AgentSerial = 1;
        Row.SampleSequence = 7;
        Row.ConfigurationRevision = 2;
        Row.DisplayId = TEXT("101:1");
        Row.DebugName = TEXT("RegistryReentry");
        Row.Position = FVector{100.0, 0.0, 0.0};
        Row.WorldTransform = FTransform{Row.Position};
        Row.MaxSpeedCmPerSec = 100.0f;
        Row.MaxTurnRateRadPerSec = 1.0f;
        Row.MaxAccelerationCmPerSecSq = 200.0f;
        Row.ArrivalRadiusCm = 20.0f;
        Row.SeparationRadiusCm = 50.0f;
        return Row;
    }

    auto MakeRequest() -> FCkCrowdRemoteCommandRequest
    {
        FCkCrowdRemoteCommandRequest Request;
        Request.Id = MakeRow().Id;
        Request.ClientSerial = 1;
        Request.ObservedConfigurationRevision = 2;
        Request.Command = ECkCrowdRemoteCommand::SetTunables;
        Request.MaxSpeedCmPerSec = 100.0f;
        Request.MaxTurnRateRadPerSec = 1.0f;
        Request.MaxAccelerationCmPerSecSq = 200.0f;
        Request.ArrivalRadiusCm = 20.0f;
        Request.SeparationRadiusCm = 50.0f;
        return Request;
    }
}

using namespace ck_crowd_remote_debug_registry_reentry_test;

auto UCkTest_CrowdRemoteDebug_RegistryReentryProbe::GetWorld() const -> UWorld*
{
    return BoundWorld.Get();
}

auto UCkTest_CrowdRemoteDebug_RegistryReentryProbe::IsForWorld(const UWorld& World) const -> bool
{
    if (BoundWorld.Get() != &World) { return false; }
    if (Mode == 0 || bInCallback) { return true; }
    bInCallback = true;
    const int32 Operation = Mode;
    Mode = 0; // nested registration's IsForWorld cannot repeat the mutation
    if (Operation == 1 || Operation == 2)
    {
        bReentrySucceeded = ActiveToken.IsValid() &&
            FCkCrowdRemoteDebugRegistry::TryUnregister(World, *this, ActiveToken);
        if (Operation == 2 && bReentrySucceeded)
        {
            ReplacementToken = {};
            bReentrySucceeded = FCkCrowdRemoteDebugRegistry::TryRegister(
                *const_cast<UWorld*>(&World),
                *const_cast<UCkTest_CrowdRemoteDebug_RegistryReentryProbe*>(this),
                ReplacementToken);
        }
    }
    else if (Operation == 3)
    {
        bReentrySucceeded = true;
        bInCallback = false;
        const_cast<UCkTest_CrowdRemoteDebug_RegistryReentryProbe*>(this)->MarkAsGarbage();
        return true;
    }
    bInCallback = false;
    return true;
}

auto UCkTest_CrowdRemoteDebug_RegistryReentryProbe::CopyRows(const UWorld& World,
    const APlayerController&, const ULocalPlayer&,
    TArray<FCkCrowdRemoteAgentRow>& Out) const -> bool
{
    ++RowCalls;
    if (BoundWorld.Get() != &World) { return false; }
    Out.Reset();
    Out.Add(MakeRow());
    return true;
}

auto UCkTest_CrowdRemoteDebug_RegistryReentryProbe::RequestSelectionFromOwningLocalController(
    const UWorld&, APlayerController&, const ULocalPlayer&,
    const FCkCrowdRemoteAgentId&, int64, bool) -> bool
{
    return false;
}

auto UCkTest_CrowdRemoteDebug_RegistryReentryProbe::CopySelectedDetail(const UWorld&,
    const APlayerController&, const ULocalPlayer&,
    const FCkCrowdRemoteAgentId&, int64, FCkCrowdRemoteAgentDetail&) const -> bool
{
    return false;
}

auto UCkTest_CrowdRemoteDebug_RegistryReentryProbe::SubmitFromOwningLocalController(
    const UWorld& World, APlayerController&, const ULocalPlayer&,
    const FCkCrowdRemoteCommandRequest&) -> bool
{
    ++SubmitCalls;
    return BoundWorld.Get() == &World;
}

auto UCkTest_CrowdRemoteDebug_RegistryReentryProbe::CopyCommandResult(const UWorld&,
    const APlayerController&, const ULocalPlayer&, int64,
    FCkCrowdRemoteCommandResult&) const -> bool
{
    return false;
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkTests/Net/CkNetAutomation_Common.h"

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCrowdRemoteDebug_RegistryReentry,
    "Ck.Crowd.RemoteDebug.RegistryReentry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkCrowdRemoteDebug_RegistryReentry::RunTest(const FString& Parameters) -> bool
{
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters, AGameModeBase::StaticClass()));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
        {
            APlayerController* PC = nullptr;
            ULocalPlayer* LP = nullptr;
            if (!TestTrue(TEXT("real owned client PIE local context"),
                    TryLocalContext(World, PC, LP))) { return; }

            TStrongObjectPtr<UCkTest_CrowdRemoteDebug_RegistryReentryProbe> Provider{
                NewObject<UCkTest_CrowdRemoteDebug_RegistryReentryProbe>(World)};
            if (!TestNotNull(TEXT("world-owned reentrant provider"), Provider.Get())) { return; }
            Provider->BoundWorld = World;
            FCkCrowdRemoteProviderToken Token;
            if (!TestTrue(TEXT("ordinary provider registration"),
                    FCkCrowdRemoteDebugRegistry::TryRegister(*World, *Provider, Token)) ||
                !TestTrue(TEXT("ordinary registration publishes token"), Token.IsValid()))
            { return; }
            Provider->ActiveToken = Token;
            ON_SCOPE_EXIT
            {
                if (IsValid(World) && IsValid(Provider.Get()))
                {
                    FCkCrowdRemoteDebugRegistry::TryUnregister(*World, *Provider,
                        Provider->ReplacementToken.IsValid() ? Provider->ReplacementToken : Token);
                }
            };

            TArray<FCkCrowdRemoteAgentRow> Rows;
            const auto Request = MakeRequest();
            if (!TestTrue(TEXT("valid roster control"),
                    FCkCrowdRemoteDebugRegistry::TryCopyRows(*World, *PC, *LP, Rows)) ||
                !TestEqual(TEXT("valid roster contains one authored row"), Rows.Num(), 1) ||
                !TestTrue(TEXT("valid submit control"),
                    FCkCrowdRemoteDebugRegistry::TrySubmit(*World, *PC, *LP, Request)))
            { return; }

            const int32 BeforeRows = Provider->RowCalls;
            Provider->Mode = 1;
            Rows.Add(MakeRow());
            TestFalse(TEXT("callback unregister rejects roster"),
                FCkCrowdRemoteDebugRegistry::TryCopyRows(*World, *PC, *LP, Rows));
            TestTrue(TEXT("unregister happened during IsForWorld"), Provider->bReentrySucceeded);
            TestEqual(TEXT("unregistered callback never reaches CopyRows"), Provider->RowCalls, BeforeRows);
            TestEqual(TEXT("rejected roster clears caller output"), Rows.Num(), 0);

            Provider->Mode = 0;
            if (!TestTrue(TEXT("same live provider may register after exact removal"),
                    FCkCrowdRemoteDebugRegistry::TryRegister(*World, *Provider, Token)))
            { return; }
            Provider->ActiveToken = Token;
            Provider->bReentrySucceeded = false;
            Provider->Mode = 1;
            const int32 BeforeRemovedSubmit = Provider->SubmitCalls;
            TestFalse(TEXT("callback unregister rejects submit"),
                FCkCrowdRemoteDebugRegistry::TrySubmit(*World, *PC, *LP, Request));
            TestTrue(TEXT("submit removed the exact old token"), Provider->bReentrySucceeded);
            TestEqual(TEXT("removed callback never reaches Submit"),
                Provider->SubmitCalls, BeforeRemovedSubmit);
            Provider->Mode = 0;
            if (!TestTrue(TEXT("provider registers again after submit removal"),
                    FCkCrowdRemoteDebugRegistry::TryRegister(*World, *Provider, Token)))
            { return; }
            Provider->ActiveToken = Token;
            Provider->bReentrySucceeded = false;
            Provider->Mode = 2;
            const int32 BeforeReplacementRows = Provider->RowCalls;
            Rows.Add(MakeRow());
            TestFalse(TEXT("callback replacement rejects original roster operation"),
                FCkCrowdRemoteDebugRegistry::TryCopyRows(*World, *PC, *LP, Rows));
            TestTrue(TEXT("same-owner replacement registered during callback"),
                Provider->bReentrySucceeded && Provider->ReplacementToken.IsValid() &&
                Provider->ReplacementToken.Value != Token.Value);
            TestEqual(TEXT("replaced callback never reaches CopyRows"),
                Provider->RowCalls, BeforeReplacementRows);
            TestEqual(TEXT("replaced roster publishes nothing"), Rows.Num(), 0);

            Provider->ActiveToken = Provider->ReplacementToken;
            Provider->ReplacementToken = {};
            Provider->bReentrySucceeded = false;
            Provider->Mode = 2;
            const int32 BeforeSubmit = Provider->SubmitCalls;
            TestFalse(TEXT("callback replacement rejects original submit operation"),
                FCkCrowdRemoteDebugRegistry::TrySubmit(*World, *PC, *LP, Request));
            TestTrue(TEXT("submit replacement registered once"),
                Provider->bReentrySucceeded && Provider->ReplacementToken.IsValid() &&
                Provider->ReplacementToken.Value != Provider->ActiveToken.Value);
            TestEqual(TEXT("replaced callback never reaches Submit"), Provider->SubmitCalls, BeforeSubmit);

            Provider->Mode = 0;
            Rows.Reset();
            TestTrue(TEXT("exact replacement remains registered and readable"),
                FCkCrowdRemoteDebugRegistry::TryCopyRows(*World, *PC, *LP, Rows));
            TestEqual(TEXT("replacement returned one row"), Rows.Num(), 1);
            TestTrue(TEXT("exact replacement token unregisters"),
                FCkCrowdRemoteDebugRegistry::TryUnregister(
                    *World, *Provider, Provider->ReplacementToken));
            Provider->ReplacementToken = {};

            TStrongObjectPtr<UCkTest_CrowdRemoteDebug_RegistryReentryProbe> Invalidating{
                NewObject<UCkTest_CrowdRemoteDebug_RegistryReentryProbe>(World)};
            if (!TestNotNull(TEXT("separate invalidating owner"), Invalidating.Get())) { return; }
            Invalidating->BoundWorld = World;
            Invalidating->Mode = 3;
            FCkCrowdRemoteProviderToken RejectedToken{777};
            TestFalse(TEXT("owner invalidated in registration callback rejects"),
                FCkCrowdRemoteDebugRegistry::TryRegister(*World, *Invalidating, RejectedToken));
            TestEqual(TEXT("invalidated registration preserves output token"),
                RejectedToken.Value, uint64{777});
            Rows.Add(MakeRow());
            TestFalse(TEXT("invalidated owner has no visible roster"),
                FCkCrowdRemoteDebugRegistry::TryCopyRows(*World, *PC, *LP, Rows));
            TestEqual(TEXT("invalidated owner publishes no stale row"), Rows.Num(), 0);

            TStrongObjectPtr<UCkTest_CrowdRemoteDebug_RegistryReentryProbe> Fresh{
                NewObject<UCkTest_CrowdRemoteDebug_RegistryReentryProbe>(World)};
            if (!TestNotNull(TEXT("fresh owner after invalidation"), Fresh.Get())) { return; }
            Fresh->BoundWorld = World;
            FCkCrowdRemoteProviderToken FreshToken;
            if (!TestTrue(TEXT("invalidated registration left no visible entry"),
                    FCkCrowdRemoteDebugRegistry::TryRegister(*World, *Fresh, FreshToken)))
            { return; }
            TestTrue(TEXT("fresh token exact removal"),
                FCkCrowdRemoteDebugRegistry::TryUnregister(*World, *Fresh, FreshToken));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
