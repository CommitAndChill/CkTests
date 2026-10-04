// Language=angelscript

// Two panels over one GameInstance-owned native Direct TCP profile. Gym travel
// replaces these controllers, but never starts or stops the profile implicitly.
class ACk_OnlineDirectGym_HUD : ACkGym_ControlPanelHUD
{
    bool Get_PanelKeysSuspended() override
    {
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectGym_Subsystem);
        return ck::IsValid(Probe) && Probe.Get_IsEditing();
    }
}

namespace CkOnlineDirectGym
{
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
        Rows.Add(CkGym_Control::Status("Human verdict", "PENDING: compare both separate-process windows", true)); // 1
        Rows.Add(CkGym_Control::Status("Purpose", "Real TCP control lobby; separate from UE gameplay connection")); // 2
        Rows.Add(CkGym_Control::Status("Role / state", StateText(Snapshot.Get_State()))); // 3
        Rows.Add(CkGym_Control::Status("Service generation", Snapshot.Get_ServiceGeneration().ToString())); // 4
        Rows.Add(CkGym_Control::Status("Lobby epoch", Snapshot.Get_LobbyEpoch().ToString())); // 5
        Rows.Add(CkGym_Control::Status("Local member ID", Snapshot.Get_LocalMemberId().ToString())); // 6
        Rows.Add(CkGym_Control::Status("Capacity / invite expiry", "4 members / 300 seconds from host issue")); // 7
        Rows.Add(CkGym_Control::Status("Host endpoint", f"{Probe.Get_AdvertisedAddress()}:7788")); // 8
        Rows.Add(CkGym_Control::Status("Host join descriptor", Snapshot.Get_JoinDescriptor())); // 9
        Rows.Add(CkGym_Control::Status("Local name", Probe.Get_Name())); // 10
        Rows.Add(CkGym_Control::Status("Advertised IPv4", Probe.Get_AdvertisedAddress())); // 11
        Rows.Add(CkGym_Control::Status("Join draft", Probe.Get_JoinDescriptorDraft())); // 12
        Rows.Add(CkGym_Control::Status("Chat draft", Probe.Get_ChatDraft())); // 13
        Rows.Add(CkGym_Control::Status("Authoritative roster", Probe.Get_RosterText())); // 14
        Rows.Add(CkGym_Control::Status("Roster count", f"{Snapshot.Get_Members().Num()}")); // 15
        Rows.Add(CkGym_Control::Status("Local ready", Probe.Get_IsLocalReady() ? "READY" : "not ready")); // 16
        Rows.Add(CkGym_Control::Status("Accepted / completed", Probe.Get_QueueText())); // 17
        Rows.Add(CkGym_Control::Status("Last completion", Probe.Get_CompletionText())); // 18
        Rows.Add(CkGym_Control::Status("Last error", Probe.Get_LastError() + " " + Snapshot.Get_LastError())); // 19
        Rows.Add(CkGym_Control::Header("ACTIONS: EDIT WINDOWS ACCEPT TEXT; TAB SWITCHES GYMS")); // 20
        Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Host TCP lobby", Offline)); // 21
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Join typed descriptor", Offline)); // 22
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Toggle local ready", Active)); // 23
        Rows.Add(CkGym_Control::Action(EKeys::P, "P", "Copy host descriptor", Snapshot.Get_State() == ECk_OnlineDirect_State::Hosting)); // 24
        Rows.Add(CkGym_Control::Action(EKeys::O, "O", "Edit / paste join descriptor")); // 25
        Rows.Add(CkGym_Control::Action(EKeys::N, "N", "Edit local name")); // 26
        Rows.Add(CkGym_Control::Action(EKeys::L, "L", "Edit advertised IPv4")); // 27
        Rows.Add(CkGym_Control::Action(EKeys::B, "B", "Edit chat draft")); // 28
        Rows.Add(CkGym_Control::Action(EKeys::I, "I", "Send chat draft", Active)); // 29
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", "Leave / cancel join / recover failure", !Offline)); // 30
        Rows.Add(CkGym_Control::Action(EKeys::Z, "Z", "Stop this gym's owned profile", Probe.Get_IsOwnedProfile())); // 31

        if (InChat)
        {
            Rows.Add(CkGym_Control::Header("AUTHORITATIVE TRANSCRIPT: NEWEST 6 SHOWN, 32 RETAINED"));
            int32 Count = Probe.Get_MessageCount();
            Rows.Add(CkGym_Control::Status("Retained messages", f"{Count}"));
            if (Count == 0) { Rows.Add(CkGym_Control::Status("Messages", "none")); }
            for (int32 Index = 0; Index < Count && Index < 6; ++Index)
            { Rows.Add(CkGym_Control::Status(f"Message {Index + 1}", Probe.Get_MessageLineFromNewest(Index))); }
        }
        else
        {
            Rows.Add(CkGym_Control::Status("Latest chat", Probe.Get_MessageLineFromNewest(0)));
        }
        return Rows;
    }

    void Activate(int32 InRowIndex)
    {
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectGym_Subsystem);
        if (ck::Is_NOT_Valid(Probe)) { return; }
        if (InRowIndex == 21) { Probe.Request_Host(); }
        else if (InRowIndex == 22) { Probe.Request_Join(); }
        else if (InRowIndex == 23) { Probe.Request_ToggleReady(); }
        else if (InRowIndex == 24) { Probe.Request_CopyDescriptor(); }
        else if (InRowIndex == 25) { Probe.Request_EditText(ECkOnlineDirectGym_TextField::JoinDescriptor); }
        else if (InRowIndex == 26) { Probe.Request_EditText(ECkOnlineDirectGym_TextField::Name); }
        else if (InRowIndex == 27) { Probe.Request_EditText(ECkOnlineDirectGym_TextField::AdvertisedAddress); }
        else if (InRowIndex == 28) { Probe.Request_EditText(ECkOnlineDirectGym_TextField::ChatDraft); }
        else if (InRowIndex == 29) { Probe.Request_SendChat(); }
        else if (InRowIndex == 30) { Probe.Request_Leave(); }
        else if (InRowIndex == 31) { Probe.Request_StopOwnedProfile(); }
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
    void Request_ControlActivated(int32 InRowIndex) override { CkOnlineDirectGym::Activate(InRowIndex); }
}
