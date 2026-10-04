// Language=angelscript

// Direct gameplay IP gate. Both play instances begin offline; only a panel action
// starts listening or joins the displayed endpoint.
class ACk_OnlineDirectIpGym_HUD : ACkGym_ControlPanelHUD
{
    ECkGym_ControlPanel_Mode Get_InitialPanelMode() override
    { return ECkGym_ControlPanel_Mode::Compact; }
}

class ACk_OnlineGym_DirectIp_GameMode : ACkTests_Gym_Base_GameMode
{
    default GameStateClass = ACkOnlineDirectIpTest_GameState;
    default PlayerControllerClass = ACk_OnlineGym_DirectIp_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACk_OnlineDirectIpGym_HUD;
}

class ACk_OnlineGym_DirectIp_PlayerController : ACk_Gym_Base_PlayerController
{
    default Replicates = true;

    UPROPERTY(DefaultComponent)
    UCkOnlineDirectIpTest_ProofComponent ProofComponent;

    // A replicated sequence from a previous connection is not proof from this controller.
    private int32 _ProofSequenceBeforePress = -1;
    private FString _ProofRequestedSessionMarker;

    FString Get_ControlPanelTitle() override
    {
        return "ONLINE DIRECT IP";
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectIpTest_Subsystem);
        if (ck::Is_NOT_Valid(Probe))
        {
            Rows.Add(CkGym_Control::Status("Verdict", "FAIL: direct IP test subsystem unavailable", true));
            return Rows;
        }

        bool IsHosting = Probe.Get_IsHosting();
        bool IsConnected = Probe.Get_IsConnected();
        bool IsOffline = Probe.Get_IsOffline();
        bool IsFailed = Probe.Get_StateText().StartsWith("Failed");
        bool HasLobbyBridgeContext = Probe.Get_HasLobbyBridgeContext();
        bool ClientProofObserved = !IsHosting && IsConnected && Probe.Get_PeerCount() >= 2
            && _ProofSequenceBeforePress >= 0
            && Probe.Get_SessionMarkerText() == _ProofRequestedSessionMarker
            && Probe.Get_ProofSequence() > _ProofSequenceBeforePress;

        Rows.Add(CkGym_Control::Header("DIRECT GAMEPLAY IP"));
        FString NextStep;
        if (IsFailed)
        { NextStep = HasLobbyBridgeContext
            ? "Read error below. Return to Lobby, then Host G, wait for Hosting, Client G."
            : "Read error below; X recovers, then retry explicitly."; }
        else if (IsOffline)
        { NextStep = HasLobbyBridgeContext
            ? "Back in Lobby: Host G, wait for IP Hosting; Client G retries."
            : "Host: J, wait for Hosting. Client: K joins 127.0.0.1:7787."; }
        else if (IsHosting)
        { NextStep = Probe.Get_IsLobbyBridge()
            ? "Wait for Client G from Lobby; then compare client P proof."
            : "Wait for Client K; then compare roster and client P proof."; }
        else if (IsConnected && !ClientProofObserved)
        { NextStep = "Client P sends network proof; wait for observed sequence."; }
        else if (IsConnected)
        { NextStep = "Proof observed here. X disconnects; compare both windows."; }
        else
        { NextStep = HasLobbyBridgeContext
            ? "Wait for IP admission. Failed entry returns to Lobby; retry Host G then Client G."
            : "Wait for connection, or X to cancel / recover."; }
        Rows.Add(CkGym_Control::StatusTone("Next step", NextStep, ECkGym_ControlTone::Info)); // 1
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Human check", HasLobbyBridgeContext
            ? "Compare lobby continuity and gameplay in both windows; no automatic pass"
            : "Compare both windows and complete disconnect / rejoin; no automatic pass"))); // 2
        Rows.Add(CkGym_Control::StatusTone("Connection", Probe.Get_StateText(),
            IsFailed ? ECkGym_ControlTone::Error
            : IsConnected || IsHosting ? ECkGym_ControlTone::Success : ECkGym_ControlTone::Info)); // 3
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Endpoint", Probe.Get_Endpoint()))); // 4
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Session marker", Probe.Get_SessionMarkerText()))); // 5
        Rows.Add(CkGym_Control::StatusTone("Peer roster", Probe.Get_PeerRosterText(), ECkGym_ControlTone::Neutral)); // 6
        Rows.Add(CkGym_Control::StatusTone("Peer count", f"{Probe.Get_PeerCount()}", ECkGym_ControlTone::Neutral)); // 7
        Rows.Add(CkGym_Control::StatusTone("Network proof", ClientProofObserved
            ? "OBSERVED on connected client" : "Client presses P after joining",
            ClientProofObserved ? ECkGym_ControlTone::Success : ECkGym_ControlTone::Info)); // 8
        Rows.Add(CkGym_Control::StatusTone("Proof seq / player", f"{Probe.Get_ProofSequence()} / {Probe.Get_LastProofPlayerId()}",
            ClientProofObserved ? ECkGym_ControlTone::Success : ECkGym_ControlTone::Neutral)); // 9
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Joins / leaves", f"{Probe.Get_JoinCount()} / {Probe.Get_LeaveCount()}"))); // 10
        FString LastError = Probe.Get_LastError();
        FString BriefError = LastError.Len() > 112 ? f"{LastError.Left(109)}..." : LastError;
        Rows.Add(CkGym_Control::StatusTone("Last error", LastError.Len() > 0 ? BriefError : "none",
            LastError.Len() > 0 ? ECkGym_ControlTone::Error : ECkGym_ControlTone::Neutral)); // 11

        Rows.Add(CkGym_Control::Header("ACTIONS: TWO OFFLINE INSTANCES FIRST"));
        Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Host on port 7787", IsOffline));
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Join 127.0.0.1:7787", IsOffline));
        Rows.Add(CkGym_Control::Action(EKeys::P, "P", "Send client network proof", IsConnected && !IsHosting));
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", HasLobbyBridgeContext
            ? "Return to Online Lobby" : "Cancel join / disconnect / stop host", !IsOffline));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Rejoin standalone endpoint only", Probe.Get_CanRejoin()));
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Bridge context", Probe.Get_LobbyBridgeText()))); // 18
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Replicated lobby epoch", Probe.Get_LobbyEpochText()))); // 19
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::V, "V", "Log sanitized bridge checkpoint",
            Probe.Get_IsLobbyBridge() && (IsHosting || IsConnected)))); // 20
        Rows.Add(CkGym_Control::Action(EKeys::F2, "F2", "Show / hide details")); // 21
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Purpose", HasLobbyBridgeContext
            ? "Stock IP gameplay plus TCP control continuity; fixture, no member admission proof"
            : "Manual stock IP host, join, proof, disconnect and rejoin fixture"))); // 22
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Full error", LastError))); // 23
        bool CanTestControlLoss = Probe.Get_IsLobbyBridge() && (IsHosting || IsConnected);
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::F6, "F6",
            "Test: remove my lobby readiness", CanTestControlLoss))); // 24
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::F7, "F7",
            "Test: stop my TCP control profile", CanTestControlLoss))); // 25
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Recovery test",
            "Expect exit to Lobby and an actual loss error; compare both windows, no automatic PASS."))); // 26
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectIpTest_Subsystem);
        if (ck::Is_NOT_Valid(Probe))
        { return; }

        // Header/status rows occupy indices too. Keep these indices aligned with Get_ControlRows().
        if (InRowIndex == 13) { Probe.Request_Host(7787); }
        else if (InRowIndex == 14) { Probe.Request_Join("127.0.0.1:7787"); }
        else if (InRowIndex == 15)
        {
            const int32 SequenceBeforePress = Probe.Get_ProofSequence();
            const FString SessionMarkerAtPress = Probe.Get_SessionMarkerText();
            if (Probe.Request_Proof())
            {
                _ProofSequenceBeforePress = SequenceBeforePress;
                _ProofRequestedSessionMarker = SessionMarkerAtPress;
            }
        }
        else if (InRowIndex == 16) { Probe.Request_Disconnect(); }
        else if (InRowIndex == 17) { Probe.Request_Rejoin(); }
        else if (InRowIndex == 20) { Probe.Request_LogCheckpoint(); }
        else if (InRowIndex == 21)
        {
            auto Hud = Cast<ACk_OnlineDirectIpGym_HUD>(GetHUD());
            if (ck::IsValid(Hud)) { Hud.Request_ToggleDetails(); }
        }
        else if (InRowIndex == 24) { Probe.Request_TestControlReadyLoss(); }
        else if (InRowIndex == 25) { Probe.Request_TestControlStop(); }
    }
}
