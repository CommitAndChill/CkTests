#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkNetAutomationWorldAdmission,
    "Ck.NetAutomation.Network.WorldAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkNetAutomationWorldAdmission::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* ActionWorld)
    {
        UWorld* const Server = ck::auto_test::net::Get_ServerWorld();
        UWorld* const Client = ck::auto_test::net::Get_ClientWorld(0);
        const TArray<UWorld*> HealthyWorlds = ck::auto_test::net::Get_AllPIEWorlds();
        if (!TestTrue(TEXT("healthy two-world PIE getter prerequisites"),
            IsValid(ActionWorld) && IsValid(Server) && IsValid(Client) &&
            ActionWorld == Server && Server != Client &&
            !Server->bIsTearingDown && !Client->bIsTearingDown &&
            (Server->GetNetMode() == NM_DedicatedServer ||
                Server->GetNetMode() == NM_ListenServer) &&
            Client->GetNetMode() == NM_Client &&
            HealthyWorlds.Num() == 2 && HealthyWorlds[0] == Server &&
            HealthyWorlds[1] == Client &&
            ck::auto_test::net::Get_NumClientWorlds() == 1))
        { return; }

        UWorld* ServerLookupWithDyingServer = nullptr;
        UWorld* ClientLookupWithDyingServer = nullptr;
        TArray<UWorld*> WorldsWithDyingServer;
        int32 ClientCountWithDyingServer = -1;
        {
            const bool Original = Server->bIsTearingDown;
            const auto RestoreServer = ::ScopeExitSupport::FScopeGuardSyntaxSupport() +
                [Server, Original]() { Server->bIsTearingDown = Original; };
            Server->bIsTearingDown = true;
            ServerLookupWithDyingServer = ck::auto_test::net::Get_ServerWorld();
            ClientLookupWithDyingServer = ck::auto_test::net::Get_ClientWorld(0);
            WorldsWithDyingServer = ck::auto_test::net::Get_AllPIEWorlds();
            ClientCountWithDyingServer = ck::auto_test::net::Get_NumClientWorlds();
        }
        if (!TestTrue(TEXT("dying server is rejected without substituting client"),
            !Server->bIsTearingDown &&
            ServerLookupWithDyingServer == nullptr &&
            ClientLookupWithDyingServer == Client &&
            WorldsWithDyingServer.Num() == 1 &&
            WorldsWithDyingServer[0] == Client &&
            ClientCountWithDyingServer == 1 &&
            ck::auto_test::net::Get_ServerWorld() == Server &&
            ck::auto_test::net::Get_ClientWorld(0) == Client))
        { return; }

        UWorld* ServerLookupWithDyingClient = nullptr;
        UWorld* ClientLookupWithDyingClient = nullptr;
        TArray<UWorld*> WorldsWithDyingClient;
        int32 ClientCountWithDyingClient = -1;
        {
            const bool Original = Client->bIsTearingDown;
            const auto RestoreClient = ::ScopeExitSupport::FScopeGuardSyntaxSupport() +
                [Client, Original]() { Client->bIsTearingDown = Original; };
            Client->bIsTearingDown = true;
            ServerLookupWithDyingClient = ck::auto_test::net::Get_ServerWorld();
            ClientLookupWithDyingClient = ck::auto_test::net::Get_ClientWorld(0);
            WorldsWithDyingClient = ck::auto_test::net::Get_AllPIEWorlds();
            ClientCountWithDyingClient = ck::auto_test::net::Get_NumClientWorlds();
        }
        TestTrue(TEXT("dying client is rejected without replacing server or client"),
            !Client->bIsTearingDown &&
            ServerLookupWithDyingClient == Server &&
            ClientLookupWithDyingClient == nullptr &&
            WorldsWithDyingClient.Num() == 1 &&
            WorldsWithDyingClient[0] == Server &&
            ClientCountWithDyingClient == 0 &&
            ck::auto_test::net::Get_ServerWorld() == Server &&
            ck::auto_test::net::Get_ClientWorld(0) == Client &&
            ck::auto_test::net::Get_NumClientWorlds() == 1 &&
            ck::auto_test::net::Get_AllPIEWorlds() == HealthyWorlds);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
