// Language=angelscript

// Direct gameplay IP gate. Both play instances begin offline; only a panel action
// starts listening or joins the displayed endpoint.
class ACk_OnlineGym_DirectIp_GameMode : ACkTests_Gym_Base_GameMode
{
    default GameStateClass = ACkOnlineDirectIpTest_GameState;
    default PlayerControllerClass = ACk_OnlineGym_DirectIp_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
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
        bool ClientProofObserved = !IsHosting && IsConnected && Probe.Get_PeerCount() >= 2
            && _ProofSequenceBeforePress >= 0
            && Probe.Get_SessionMarkerText() == _ProofRequestedSessionMarker
            && Probe.Get_ProofSequence() > _ProofSequenceBeforePress;

        Rows.Add(CkGym_Control::Header("DIRECT GAMEPLAY IP"));
        Rows.Add(CkGym_Control::Status("Verdict", "PENDING: compare both windows and complete disconnect / rejoin", true));
        Rows.Add(CkGym_Control::Status("Purpose", "Manual host, join, proof, disconnect, rejoin in two instances"));
        Rows.Add(CkGym_Control::Status("Role / state", Probe.Get_StateText()));
        Rows.Add(CkGym_Control::Status("Endpoint", Probe.Get_Endpoint()));
        Rows.Add(CkGym_Control::Status("Session marker", Probe.Get_SessionMarkerText()));
        Rows.Add(CkGym_Control::Status("Peer roster", Probe.Get_PeerRosterText()));
        Rows.Add(CkGym_Control::Status("Peer count", f"{Probe.Get_PeerCount()}"));
        Rows.Add(CkGym_Control::Status("Network proof", ClientProofObserved
            ? "OBSERVED on connected client" : "PENDING: client presses P after joining", !ClientProofObserved));
        Rows.Add(CkGym_Control::Status("Proof sequence / player ID",
            f"{Probe.Get_ProofSequence()} / {Probe.Get_LastProofPlayerId()}"));
        Rows.Add(CkGym_Control::Status("Joins / leaves", f"{Probe.Get_JoinCount()} / {Probe.Get_LeaveCount()}"));
        Rows.Add(CkGym_Control::Status("Last error", Probe.Get_LastError()));

        Rows.Add(CkGym_Control::Header("ACTIONS: TWO OFFLINE INSTANCES FIRST"));
        Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Host on port 7787", IsOffline));
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Join 127.0.0.1:7787", IsOffline));
        Rows.Add(CkGym_Control::Action(EKeys::P, "P", "Send client network proof", IsConnected && !IsHosting));
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", "Cancel join / disconnect / stop host", !IsOffline));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Rejoin last endpoint", IsOffline));
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
    }
}
