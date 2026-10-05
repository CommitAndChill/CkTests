// Language=angelscript

// Human-operated Steam lobby check. The GameInstance owns the profile; gym
// travel does not start or stop it. No account IDs are rendered or logged.
class ACk_OnlineSteamGym_HUD : ACkGym_ControlPanelHUD
{
    ECkGym_ControlPanel_Mode Get_InitialPanelMode() override
    { return ECkGym_ControlPanel_Mode::Compact; }
}

namespace CkOnlineSteamGym
{
    FString StateText(ECk_OnlineSteam_State InState)
    {
        if (InState == ECk_OnlineSteam_State::Unavailable) { return "Unavailable"; }
        if (InState == ECk_OnlineSteam_State::LoggedOut) { return "Logged out"; }
        if (InState == ECk_OnlineSteam_State::Ready) { return "Ready"; }
        if (InState == ECk_OnlineSteam_State::Creating) { return "Creating"; }
        if (InState == ECk_OnlineSteam_State::Searching) { return "Searching"; }
        if (InState == ECk_OnlineSteam_State::Joining) { return "Joining"; }
        if (InState == ECk_OnlineSteam_State::Joined) { return "Joined"; }
        if (InState == ECk_OnlineSteam_State::Leaving) { return "Leaving"; }
        if (InState == ECk_OnlineSteam_State::CleaningUp) { return "Cleaning up"; }
        if (InState == ECk_OnlineSteam_State::CleanupUnknown) { return "Cleanup unknown"; }
        if (InState == ECk_OnlineSteam_State::Failed) { return "Failed"; }
        return "Stopped";
    }

    FString ActionText(ECk_OnlineSteam_Action InAction)
    {
        if (InAction == ECk_OnlineSteam_Action::Login) { return "Login"; }
        if (InAction == ECk_OnlineSteam_Action::CreateLobby) { return "Create"; }
        if (InAction == ECk_OnlineSteam_Action::FindLobbies) { return "Find"; }
        if (InAction == ECk_OnlineSteam_Action::JoinLobby) { return "Join"; }
        if (InAction == ECk_OnlineSteam_Action::LeaveLobby) { return "Leave"; }
        if (InAction == ECk_OnlineSteam_Action::Cancel) { return "Cancel"; }
        return "None";
    }

    FString ResultText(ECk_OnlineSteam_Result InResult)
    {
        if (InResult == ECk_OnlineSteam_Result::Succeeded) { return "succeeded"; }
        if (InResult == ECk_OnlineSteam_Result::Cancelled) { return "cancelled"; }
        if (InResult == ECk_OnlineSteam_Result::TimedOut) { return "timed out"; }
        if (InResult == ECk_OnlineSteam_Result::ProviderUnavailable) { return "provider unavailable"; }
        if (InResult == ECk_OnlineSteam_Result::NotLoggedIn) { return "not logged in"; }
        if (InResult == ECk_OnlineSteam_Result::IdentityChanged) { return "identity changed"; }
        if (InResult == ECk_OnlineSteam_Result::Rejected) { return "rejected"; }
        if (InResult == ECk_OnlineSteam_Result::Failed) { return "failed"; }
        return "invalid state";
    }

    FString NextStep(bool InStarted, ECk_OnlineSteam_State InState, int32 InResultCount)
    {
        if (InState == ECk_OnlineSteam_State::CleaningUp)
        { return "Cleanup in progress; wait for provider release. S cannot restart yet."; }
        if (InState == ECk_OnlineSteam_State::CleanupUnknown)
        { return "Cleanup unknown: provider remains reserved. Inspect diagnostics; F cannot guarantee release."; }
        if (!InStarted) { return "S starts the explicit Steam profile and reads the current Steam client account."; }
        if (InState == ECk_OnlineSteam_State::Unavailable)
        { return "Steam provider unavailable here. Check provider/AppID setup; no live pass."; }
        if (InState == ECk_OnlineSteam_State::LoggedOut)
        { return "Sign into the Steam client, then L refreshes this account."; }
        if (InState == ECk_OnlineSteam_State::Ready)
        { return InResultCount > 0 ? "Host J creates; Client N selects a result, then P joins."
            : "Host J creates a lobby; Client K finds it."; }
        if (InState == ECk_OnlineSteam_State::Joined)
        { return "Compare this lobby ID with the other computer; X leaves."; }
        if (InState == ECk_OnlineSteam_State::Failed)
        { return "Inspect result and provider setup; F stops the profile."; }
        return "Wait for the pending operation; C can request cancellation.";
    }
}

class ACk_OnlineGym_SteamLobby_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_OnlineGym_SteamLobby_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACk_OnlineSteamGym_HUD;
}

class ACk_OnlineGym_SteamLobby_PlayerController : ACk_Gym_Base_PlayerController
{
    int32 SelectedLobbyIndex = 0;
    FGuid LastAdmittedOperationId;
    FString LocalNotice = "No action requested in this gym visit.";

    FString Get_ControlPanelTitle() override { return "STEAM LOBBY - HUMAN CHECK"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        auto Steam = Subsystem::GetGameInstanceSubsystem(UCk_OnlineSteam_Subsystem_UE);
        if (ck::Is_NOT_Valid(Steam))
        {
            Rows.Add(CkGym_Control::StatusTone("Steam facade", "Unavailable in this build",
                ECkGym_ControlTone::Error));
            return Rows;
        }

        auto Snapshot = Steam.Get_Snapshot();
        auto Completion = Steam.Get_LastCompletion();
        auto State = Snapshot.Get_State();
        int32 Count = Snapshot.Get_SearchResults().Num();
        int32 Selection = Count > 0 ? SelectedLobbyIndex % Count : 0;
        bool Started = Steam.Get_IsStarted();
        bool Ready = Started && State == ECk_OnlineSteam_State::Ready;
        bool Joined = Started && State == ECk_OnlineSteam_State::Joined;
        bool Pending = LastAdmittedOperationId.IsValid()
            && Completion.Get_OperationId() != LastAdmittedOperationId;

        Rows.Add(CkGym_Control::Header("STEAM LOBBY: TWO-COMPUTER HUMAN CHECK")); // 0
        Rows.Add(CkGym_Control::StatusTone("Next step",
            CkOnlineSteamGym::NextStep(Started, State, Count), ECkGym_ControlTone::Info)); // 1
        Rows.Add(CkGym_Control::StatusTone("Human proof",
            "Compare Joined and matching lobby ID on both computers; no automatic pass.",
            ECkGym_ControlTone::Info)); // 2
        Rows.Add(CkGym_Control::StatusTone("Steam state", CkOnlineSteamGym::StateText(State),
            State == ECk_OnlineSteam_State::Unavailable || State == ECk_OnlineSteam_State::Failed
                || State == ECk_OnlineSteam_State::CleanupUnknown ? ECkGym_ControlTone::Error
                : Joined || Ready ? ECkGym_ControlTone::Success
                : State == ECk_OnlineSteam_State::Stopped ? ECkGym_ControlTone::Neutral
                : State == ECk_OnlineSteam_State::Creating || State == ECk_OnlineSteam_State::Searching
                    || State == ECk_OnlineSteam_State::Joining || State == ECk_OnlineSteam_State::Leaving
                    || State == ECk_OnlineSteam_State::CleaningUp ? ECkGym_ControlTone::Warning
                : ECkGym_ControlTone::Info)); // 3
        Rows.Add(CkGym_Control::StatusTone("Account confirmed",
            Snapshot.Get_AccountId().Len() > 0 ? "yes" : "no",
            Snapshot.Get_AccountId().Len() > 0 ? ECkGym_ControlTone::Success : ECkGym_ControlTone::Info)); // 4
        Rows.Add(CkGym_Control::StatusTone("Display name", Snapshot.Get_DisplayName().Len() > 0
            ? Snapshot.Get_DisplayName() : "none", ECkGym_ControlTone::Neutral)); // 5
        Rows.Add(CkGym_Control::StatusTone("Lobby ID", Snapshot.Get_LobbyId().Len() > 0
            ? Snapshot.Get_LobbyId() : "none", Joined ? ECkGym_ControlTone::Success : ECkGym_ControlTone::Neutral)); // 6
        Rows.Add(CkGym_Control::Status("Find results", f"{Count} (K refreshes)")); // 7
        FString SelectedText = "none";
        if (Count > 0)
        {
            auto Lobby = Snapshot.Get_SearchResults()[Selection];
            SelectedText = f"{Selection + 1}/{Count}: {Lobby.Get_Label()} | {Lobby.Get_OwnerName()} | {Lobby.Get_OpenSlots()}/{Lobby.Get_MaxMembers()} open | {Lobby.Get_Id()}";
        }
        Rows.Add(CkGym_Control::StatusTone("Selected", SelectedText, ECkGym_ControlTone::Neutral)); // 8
        FString CompletionText = Steam.Get_CompletedCount() > 0
            ? f"{CkOnlineSteamGym::ActionText(Completion.Get_Action())}: {CkOnlineSteamGym::ResultText(Completion.Get_Result())}"
            : "none";
        Rows.Add(CkGym_Control::StatusTone("Last completion", CompletionText,
            Steam.Get_CompletedCount() == 0 ? ECkGym_ControlTone::Neutral
                : Completion.Get_Result() == ECk_OnlineSteam_Result::Succeeded
                    ? ECkGym_ControlTone::Success : ECkGym_ControlTone::Error)); // 9
        Rows.Add(CkGym_Control::StatusTone("Local action", LocalNotice, ECkGym_ControlTone::Info)); // 10
        Rows.Add(CkGym_Control::Header("ACTIONS: S START, L REFRESH, J CREATE, K FIND")); // 11
        Rows.Add(CkGym_Control::Action(EKeys::S, "S", "Start Steam profile",
            !Started && State != ECk_OnlineSteam_State::CleaningUp
                && State != ECk_OnlineSteam_State::CleanupUnknown)); // 12
        Rows.Add(CkGym_Control::Action(EKeys::L, "L", "Refresh Steam client account", Started && State == ECk_OnlineSteam_State::LoggedOut)); // 13
        Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Create lobby (host)", Ready)); // 14
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Find lobbies (client)", Ready)); // 15
        Rows.Add(CkGym_Control::Action(EKeys::N, "N", "Select next result", Count > 1)); // 16
        Rows.Add(CkGym_Control::Action(EKeys::P, "P", "Join selected result", Ready && Count > 0)); // 17
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", "Leave lobby", Joined)); // 18
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::C, "C", "Cancel last admitted operation", Pending))); // 19
        Rows.Add(CkGym_Control::Details(CkGym_Control::Action(EKeys::F, "F", "Stop Steam profile", Started))); // 20
        Rows.Add(CkGym_Control::Action(EKeys::F2, "F2", "Show / hide details")); // 21
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Service generation",
            Snapshot.Get_ServiceGeneration().ToString()))); // 22
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Account generation",
            Snapshot.Get_AccountGeneration().ToString()))); // 23
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Search generation",
            Snapshot.Get_SearchGeneration().ToString()))); // 24
        Rows.Add(CkGym_Control::Details(CkGym_Control::Status("Revision", f"{Snapshot.Get_Revision()}"))); // 25
        Rows.Add(CkGym_Control::Details(CkGym_Control::StatusTone("Provider diagnostic",
            Snapshot.Get_LastError().Len() > 0 ? Snapshot.Get_LastError() : "none",
            Snapshot.Get_LastError().Len() > 0 ? ECkGym_ControlTone::Error : ECkGym_ControlTone::Neutral))); // 26
        int32 Shown = Count < 6 ? Count : 6;
        for (int32 Index = 0; Index < Shown; ++Index)
        {
            auto Lobby = Snapshot.Get_SearchResults()[Index];
            Rows.Add(CkGym_Control::Details(CkGym_Control::Status(f"Result {Index + 1}",
                f"{Lobby.Get_Label()} | {Lobby.Get_OwnerName()} | {Lobby.Get_OpenSlots()}/{Lobby.Get_MaxMembers()} open | {Lobby.Get_Id()}")));
        }
        if (Count > Shown)
        { Rows.Add(CkGym_Control::Details(CkGym_Control::Status("More results", f"{Count - Shown} hidden; N cycles all"))); }
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        auto Steam = Subsystem::GetGameInstanceSubsystem(UCk_OnlineSteam_Subsystem_UE);
        if (ck::Is_NOT_Valid(Steam)) { return; }
        if (InRowIndex == 21)
        {
            auto Hud = Cast<ACk_OnlineSteamGym_HUD>(GetHUD());
            if (ck::IsValid(Hud)) { Hud.Request_ToggleDetails(); }
            return;
        }
        if (InRowIndex == 12)
        {
            auto CurrentSnapshot = Steam.Get_Snapshot();
            auto Current = CurrentSnapshot.Get_State();
            if (Current == ECk_OnlineSteam_State::CleaningUp
                || Current == ECk_OnlineSteam_State::CleanupUnknown) { return; }
            auto Spec = FCk_OnlineSteam_Spec();
            Spec.Set_LocalUserIndex(0).Set_MaxLobbyMembers(4)
                .Set_MaxSearchResults(16).Set_LobbyLabel("CkTests Steam Lobby");
            bool Accepted = Steam.Request_Start(Spec);
            LastAdmittedOperationId = FGuid();
            SelectedLobbyIndex = 0;
            auto StartedSnapshot = Steam.Get_Snapshot();
            LocalNotice = Accepted ? f"Steam profile started: {CkOnlineSteamGym::StateText(StartedSnapshot.Get_State())}."
                : "Start rejected; check provider and project setup.";
            return;
        }
        if (InRowIndex == 20)
        {
            Steam.Request_Stop();
            LastAdmittedOperationId = FGuid();
            SelectedLobbyIndex = 0;
            LocalNotice = "Steam profile stopped.";
            return;
        }
        auto Snapshot = Steam.Get_Snapshot();
        auto State = Snapshot.Get_State();
        int32 Count = Snapshot.Get_SearchResults().Num();
        if (InRowIndex == 16)
        {
            if (Count > 1) { SelectedLobbyIndex = (SelectedLobbyIndex + 1) % Count; }
            return;
        }
        FGuid OperationId;
        bool Accepted = false;
        if (InRowIndex == 13 && State == ECk_OnlineSteam_State::LoggedOut)
        { Accepted = Steam.Request(ECk_OnlineSteam_Action::Login, -1, FGuid(), 15.0, OperationId); }
        else if (InRowIndex == 14 && State == ECk_OnlineSteam_State::Ready)
        { Accepted = Steam.Request(ECk_OnlineSteam_Action::CreateLobby, -1, FGuid(), 15.0, OperationId); }
        else if (InRowIndex == 15 && State == ECk_OnlineSteam_State::Ready)
        {
            SelectedLobbyIndex = 0;
            Accepted = Steam.Request(ECk_OnlineSteam_Action::FindLobbies, -1, FGuid(), 15.0, OperationId);
        }
        else if (InRowIndex == 17 && State == ECk_OnlineSteam_State::Ready && Count > 0)
        {
            SelectedLobbyIndex = SelectedLobbyIndex % Count;
            Accepted = Steam.Request(ECk_OnlineSteam_Action::JoinLobby, SelectedLobbyIndex,
                Snapshot.Get_SearchGeneration(), 15.0, OperationId);
        }
        else if (InRowIndex == 18 && State == ECk_OnlineSteam_State::Joined)
        { Accepted = Steam.Request(ECk_OnlineSteam_Action::LeaveLobby, -1, FGuid(), 15.0, OperationId); }
        else if (InRowIndex == 19 && LastAdmittedOperationId.IsValid())
        { Accepted = Steam.Request_Cancel(LastAdmittedOperationId, OperationId); }
        else { return; }
        if (Accepted)
        {
            LastAdmittedOperationId = InRowIndex == 19 ? FGuid() : OperationId;
            LocalNotice = "Operation admitted; wait for completion.";
        }
        else { LocalNotice = "Request rejected; inspect state and last completion."; }
    }
}
