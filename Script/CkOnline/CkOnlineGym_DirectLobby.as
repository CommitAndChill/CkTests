// Language=angelscript

// Two panels over one GameInstance-owned native Direct TCP profile. Gym travel
// replaces these controllers, but never starts or stops the profile implicitly.
class ACk_OnlineDirectGym_HUD : ACkGym_ControlPanelHUD
{
    ECkGym_ControlPanel_Mode Get_InitialPanelMode() override
    { return ECkGym_ControlPanel_Mode::Compact; }

    bool Get_PanelKeysSuspended() override
    {
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectGym_Subsystem);
        return ck::IsValid(Probe) && Probe.Get_IsEditing();
    }
}

namespace CkOnlineDirectGym
{
    bool AllMembersReady(const FCk_OnlineDirect_Snapshot&in InSnapshot)
    {
        if (InSnapshot.Get_Members().Num() != 2) { return false; }
        for (auto Member : InSnapshot.Get_Members())
        { if (!Member.Get_Ready()) { return false; } }
        return true;
    }

    FString RosterSummary(const FCk_OnlineDirect_Snapshot&in InSnapshot)
    {
        FString Result = "";
        for (auto Member : InSnapshot.Get_Members())
        {
            if (Result.Len() > 0) { Result = f"{Result} | "; }
            FString ReadyText = Member.Get_Ready() ? "READY" : "not ready";
            Result = f"{Result}{Member.Get_Name()} {ReadyText}";
        }
        return Result.Len() > 0 ? Result : "none";
    }

    FString MessageSummary(FCk_OnlineDirect_Message InMessage)
    {
        FString Text = InMessage.Get_Text();
        if (Text.Len() > 88) { Text = f"{Text.Left(85)}..."; }
        return f"#{InMessage.Get_Sequence()} {InMessage.Get_SenderName()}: {Text}";
    }

    FString NextStep(const FCk_OnlineDirect_Snapshot&in InSnapshot,
        UCkOnlineDirectGym_Subsystem InProbe, bool InChat)
    {
        auto State = InSnapshot.Get_State();
        if (State == ECk_OnlineDirect_State::Offline)
        { return "Host: J, wait for Hosting, then P copy. Client: K joins copied invite."; }
        if (State == ECk_OnlineDirect_State::Joining)
        { return "Wait for join acknowledgement and the two-member roster."; }
        if (State == ECk_OnlineDirect_State::Failed)
        { return "Read error below; X leaves failed state, then retry explicitly."; }
        if (InSnapshot.Get_Members().Num() < 2)
        { return State == ECk_OnlineDirect_State::Hosting
            ? "P copies the invite; wait for Client K and a two-member roster."
            : "Wait for the host roster; check both separate-process windows."; }
        if (InSnapshot.Get_Members().Num() > 2)
        { return "Bridge needs exactly two members; extra member leaves with X."; }
        if (!InProbe.Get_IsLocalReady())
        { return "R marks you ready; wait for READY in both rosters."; }
        if (!AllMembersReady(InSnapshot))
        { return "Wait for the other member to press R; compare both rosters."; }
        return InChat
            ? "B opens text entry and sends on Enter; verify message in both windows."
            : "Both ready: Host G enters IP gameplay, then Client G joins.";
    }

    FString StateText(ECk_OnlineDirect_State InState)
    {
        if (InState == ECk_OnlineDirect_State::Hosting) { return "Hosting"; }
        if (InState == ECk_OnlineDirect_State::Joining) { return "Joining"; }
        if (InState == ECk_OnlineDirect_State::Joined) { return "Joined"; }
        if (InState == ECk_OnlineDirect_State::Failed) { return "Failed"; }
        return "Offline";
    }

    TArray<FCkGym_ControlRow> BuildRows(bool InChat)
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectGym_Subsystem);
        auto Direct = Subsystem::GetGameInstanceSubsystem(UCk_OnlineDirect_Subsystem_UE);
        auto Ip = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectIpTest_Subsystem);
        if (ck::Is_NOT_Valid(Probe) || ck::Is_NOT_Valid(Direct))
        {
            Rows.Add(CkGym_Control::Status("Verdict", "FAIL: Direct TCP subsystem unavailable", true));
            return Rows;
        }
        auto Snapshot = Direct.Get_Snapshot();
        bool Active = Snapshot.Get_State() == ECk_OnlineDirect_State::Hosting
            || Snapshot.Get_State() == ECk_OnlineDirect_State::Joined;
        bool Offline = Snapshot.Get_State() == ECk_OnlineDirect_State::Offline;

        Rows.Add(CkGym_Control::Header(InChat ? "DIRECT TCP CHAT" : "DIRECT TCP LOBBY")); // 0
        Rows.Add(CkGym_Control::StatusTone("Next step", NextStep(Snapshot, Probe, InChat), ECkGym_ControlTone::Info)); // 1
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Human check", "Compare both separate-process windows; no automatic pass"))); // 2
        Rows.Add(CkGym_Control::StatusTone("Connection", StateText(Snapshot.Get_State()),
            Snapshot.Get_State() == ECk_OnlineDirect_State::Failed ? ECkGym_ControlTone::Error
            : Active ? ECkGym_ControlTone::Success : ECkGym_ControlTone::Info)); // 3
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Service generation", Snapshot.Get_ServiceGeneration().ToString()))); // 4
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Lobby epoch", Snapshot.Get_LobbyEpoch().ToString()))); // 5
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Local member ID", Snapshot.Get_LocalMemberId().ToString()))); // 6
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Capacity / invite expiry", "4 members / 300 seconds from host J or completed Y"))); // 7
        FString HostAddress = Snapshot.Get_HostAddress();
        FString ControlEndpoint = Active
            ? (HostAddress.Len() > 0 ? f"{HostAddress}:7788" : "none (missing active host address)")
            : (Offline ? f"draft {Probe.Get_AdvertisedAddress()}:7788 (not hosted)" : "none (not admitted)");
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Host endpoint", ControlEndpoint))); // 8
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Host join descriptor", Snapshot.Get_JoinDescriptor()))); // 9
        Rows.Add(CkGym_Control::StatusTone("Local name", Probe.Get_Name(), ECkGym_ControlTone::Neutral)); // 10
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Advertised IPv4", Probe.Get_AdvertisedAddress()))); // 11
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Join draft", Probe.Get_JoinDescriptorDraft()))); // 12
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Chat draft", Probe.Get_ChatDraft()))); // 13
        Rows.Add(CkGym_Control::StatusTone("Roster", RosterSummary(Snapshot), ECkGym_ControlTone::Neutral)); // 14
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Roster count", f"{Snapshot.Get_Members().Num()}"))); // 15
        Rows.Add(CkGym_Control::StatusTone("Local ready", Probe.Get_IsLocalReady() ? "READY" : "not ready",
            Probe.Get_IsLocalReady() ? ECkGym_ControlTone::Success : ECkGym_ControlTone::Info)); // 16
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Accepted / completed", Probe.Get_QueueText()))); // 17
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Last completion", Probe.Get_CompletionText()))); // 18
        FString LastError = f"{Probe.Get_LastError()} {Snapshot.Get_LastError()}";
        FString BriefError = LastError.Len() > 112 ? f"{LastError.Left(109)}..." : LastError;
        Rows.Add(CkGym_Control::StatusTone("Last error", LastError.Len() > 1 ? BriefError : "none",
            LastError.Len() > 1 ? ECkGym_ControlTone::Error : ECkGym_ControlTone::Neutral)); // 19
        Rows.Add(CkGym_Control::Header("ACTIONS: EDIT WINDOWS ACCEPT TEXT; TAB SWITCHES GYMS")); // 20
        Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Host TCP lobby", Offline)); // 21
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Join copied invite", Offline)); // 22
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Toggle local ready", Active)); // 23
        Rows.Add(CkGym_Control::Action(EKeys::P, "P", "Copy host descriptor", Snapshot.Get_State() == ECk_OnlineDirect_State::Hosting)); // 24
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::O, "O", "Edit / paste join descriptor"))); // 25
        Rows.Add(CkGym_Control::Action(EKeys::N, "N", "Edit local name")); // 26
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::L, "L", "Edit advertised IPv4"))); // 27
        Rows.Add(CkGym_Control::Action(EKeys::B, "B", "Type and send chat", Active)); // 28
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::I, "I", "Send saved chat draft", Active))); // 29
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", "Leave / cancel join / recover failure", !Offline)); // 30
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::Z, "Z", "Stop this gym's owned profile", Probe.Get_IsOwnedProfile()))); // 31
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::V, "V", "Log sanitized evidence checkpoint", Active && Probe.Get_IsOwnedProfile()))); // 32
        Rows.Add(CkGym_Control::Action(EKeys::G, "G", "Enter IP gameplay from ready lobby",
            Active && Probe.Get_IsOwnedProfile() && AllMembersReady(Snapshot)
                && ck::IsValid(Ip) && Ip.Get_IsOffline())); // 33

        Rows.Add(CkGym_Control::Action(EKeys::F2, "F2", "Show / hide details")); // 34
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::F3, "F3", "Join saved invite (O edits it)", Offline))); // 35
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Gameplay bridge",
            ck::IsValid(Ip) ? Ip.Get_LobbyBridgeText() : "IP fixture unavailable"))); // 36
        FString BridgeError = ck::IsValid(Ip) ? Ip.Get_LastError() : "IP fixture unavailable";
        FString BriefBridgeError = BridgeError.Len() > 112 ? f"{BridgeError.Left(109)}..." : BridgeError;
        Rows.Add(CkGym_Control::StatusTone("Bridge last error", BridgeError.Len() > 0 ? BriefBridgeError : "none",
            BridgeError.Len() > 0 ? ECkGym_ControlTone::Error : ECkGym_ControlTone::Neutral)); // 37
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Full Direct error", LastError))); // 38
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Roster with member IDs", Probe.Get_RosterText()))); // 39
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Full bridge error", BridgeError))); // 40
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Gameplay target",
            HostAddress.Len() > 0 ? f"{HostAddress}:{Probe.Get_GameplayPort()}"
                : f"none (no active lobby host; selected port {Probe.Get_GameplayPort()})"))); // 41
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::Y, "Y", "Renew host invite",
            Snapshot.Get_State() == ECk_OnlineDirect_State::Hosting && Probe.Get_IsOwnedProfile()
                && Probe.Get_IsEditing() == false))); // 42
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::F4, "F4",
            f"Edit gameplay port ({Probe.Get_GameplayPort()})", Probe.Get_CanEditGameplayPort()))); // 43

        FString QueuedTravelText = ck::IsValid(Ip)
            ? Ip.Get_QueuedTravelIsolationText() : "Not run: Direct IP fixture unavailable.";
        ECkGym_ControlTone QueuedTravelTone = ECkGym_ControlTone::Info;
        if (QueuedTravelText.StartsWith("OBSERVED")) { QueuedTravelTone = ECkGym_ControlTone::Success; }
        else if (QueuedTravelText.StartsWith("CHANGED")) { QueuedTravelTone = ECkGym_ControlTone::Error; }
        else if (QueuedTravelText.StartsWith("Not run")) { QueuedTravelTone = ECkGym_ControlTone::Warning; }
        Rows.Add(CkGym_Control::Details(CkGym_Control::StatusTone("Queued travel isolation",
            QueuedTravelText, QueuedTravelTone))); // 44
        bool CanTestQueuedTravel = Snapshot.Get_State() == ECk_OnlineDirect_State::Joined
            && Probe.Get_IsOwnedProfile() && AllMembersReady(Snapshot)
            && ck::IsValid(Ip) && Ip.Get_IsOffline();
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::F9, "F9",
            "Test: preserve queued Lobby reload", CanTestQueuedTravel))); // 45

        if (InChat)
        {
            Rows.Add(CkGym_Control::Details(CkGym_Control::Header("AUTHORITATIVE TRANSCRIPT: NEWEST 6 SHOWN, 32 RETAINED")));
            int32 Count = Snapshot.Get_Messages().Num();
            Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Retained messages", f"{Count}")));
            if (Count == 0) { Rows.Add(CkGym_Control::Status("Messages", "none")); }
            for (int32 Index = 0; Index < Count && Index < 6; ++Index)
            {
                auto Message = CkGym_Control::StatusTone(f"Message {Index + 1}",
                    MessageSummary(Snapshot.Get_Messages()[Count - 1 - Index]), ECkGym_ControlTone::Neutral);
                Rows.Add(Index < 2 ? Message : CkGym_Control::Details(Message));
            }
            Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Full transcript with IDs",
                Probe.Get_TranscriptText(6))));
        }
        else
        {
            int32 Count = Snapshot.Get_Messages().Num();
            Rows.Add(CkGym_Control::StatusTone("Latest chat", Count > 0
                ? MessageSummary(Snapshot.Get_Messages()[Count - 1]) : "none", ECkGym_ControlTone::Neutral));
            Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Latest chat with ID",
                Probe.Get_MessageLineFromNewest(0))));
        }

        return Rows;
    }

    void Activate(int32 InRowIndex, bool InChat, ACk_OnlineDirectGym_HUD InHud)
    {
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectGym_Subsystem);
        if (ck::Is_NOT_Valid(Probe)) { return; }
        if (InRowIndex == 45)
        {
            auto Ip = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectIpTest_Subsystem);
            auto Direct = Subsystem::GetGameInstanceSubsystem(UCk_OnlineDirect_Subsystem_UE);
            if (ck::Is_NOT_Valid(Ip) || ck::Is_NOT_Valid(Direct) || !Probe.Get_IsOwnedProfile()
                || !Ip.Get_IsOffline()) { return; }
            auto Snapshot = Direct.Get_Snapshot();
            if (Snapshot.Get_State() != ECk_OnlineDirect_State::Joined || !AllMembersReady(Snapshot)) { return; }
            Ip.Request_TestQueuedLobbyTravel();
            return;
        }
        if (InRowIndex == 21) { Probe.Request_Host(); }
        else if (InRowIndex == 22) { Probe.Request_JoinCopiedInvite(); }
        else if (InRowIndex == 23) { Probe.Request_ToggleReady(); }
        else if (InRowIndex == 24) { Probe.Request_CopyDescriptor(); }
        else if (InRowIndex == 25) { Probe.Request_EditText(ECkOnlineDirectGym_TextField::JoinDescriptor); }
        else if (InRowIndex == 26) { Probe.Request_EditText(ECkOnlineDirectGym_TextField::Name); }
        else if (InRowIndex == 27) { Probe.Request_EditText(ECkOnlineDirectGym_TextField::AdvertisedAddress); }
        else if (InRowIndex == 28) { Probe.Request_EditChatAndSend(); }
        else if (InRowIndex == 29) { Probe.Request_SendChat(); }
        else if (InRowIndex == 30) { Probe.Request_Leave(); }
        else if (InRowIndex == 31) { Probe.Request_StopOwnedProfile(); }
        else if (InRowIndex == 32) { Probe.Request_LogCheckpoint(InChat); }
        else if (InRowIndex == 33)
        {
            auto Direct = Subsystem::GetGameInstanceSubsystem(UCk_OnlineDirect_Subsystem_UE);
            auto Ip = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectIpTest_Subsystem);
            if (ck::Is_NOT_Valid(Direct) || ck::Is_NOT_Valid(Ip) || !Probe.Get_IsOwnedProfile()) { return; }
            auto Snapshot = Direct.Get_Snapshot();
            if (Snapshot.Get_State() == ECk_OnlineDirect_State::Hosting)
            { Ip.Request_HostFromReadyLobby(Probe.Get_GameplayPort()); }
            else if (Snapshot.Get_State() == ECk_OnlineDirect_State::Joined)
            { Ip.Request_JoinFromReadyLobby(f"{Snapshot.Get_HostAddress()}:{Probe.Get_GameplayPort()}"); }
        }
        else if (InRowIndex == 42) { Probe.Request_RenewInvite(); }
        else if (InRowIndex == 43) { Probe.Request_EditText(ECkOnlineDirectGym_TextField::GameplayPort); }
        else if (InRowIndex == 34)
        {
            if (ck::IsValid(InHud)) { InHud.Request_ToggleDetails(); }
        }
        else if (InRowIndex == 35) { Probe.Request_Join(); }
    }
}

class ACk_OnlineGym_DirectLobby_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_OnlineGym_DirectLobby_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACk_OnlineDirectGym_HUD;
}

class ACk_OnlineGym_DirectLobby_PlayerController : ACk_Gym_Base_PlayerController
{
    FString Get_ControlPanelTitle() override { return "ONLINE LOBBY - NATIVE DIRECT TCP"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override { return CkOnlineDirectGym::BuildRows(false); }
    void Request_ControlActivated(int32 InRowIndex) override
    { CkOnlineDirectGym::Activate(InRowIndex, false, Cast<ACk_OnlineDirectGym_HUD>(GetHUD())); }
}
