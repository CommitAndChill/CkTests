# Lobby and chat: extra checks

These checks are required before recording full manual acceptance. Keep your joined lobby if continuing the [five-step first pass](DIRECT_LOBBY_EDITOR_TESTING.md), or use the setup and join steps below for a fresh session.

**Setup:** open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. In **Play > Advanced Settings**: **2 players**, **Play Standalone**, **Launch Separate Server Off**, **Run Under One Process Off**. Launch **New Editor Window (PIE)**. In both windows: **Tab > CkOnline > Online Lobby**; **H** cycles the panel. Use **N**, type `Alice` on Host / `Bob` on Client, then **Enter**. **F2** shows full GUID/epoch details.

**How to join:** Host **J**, wait for **Hosting**, then **P** to copy the invite. Client **K** validates the clipboard invite and joins directly. Wait for Client **Joined** and matching two-member rosters/epochs. Press **R** in each window until both are ready before starting the checks below. For a manual invite, **O** edits and **Enter** saves; **F2**, then **F3** joins that saved draft.

## Ready off

Client presses **R** again.

**Pass:** Both rosters show Bob **not ready**.

## Leave and rejoin

Within five minutes of hosting, Client presses **X**, waits for **Offline**, then shows full details with **F2** if needed and presses **F3** to rejoin using the retained invite without depending on clipboard contents.

**Pass:** Host stays **Hosting**; its roster goes **2 → 1 → 2**. The lobby epoch stays the same. Bob gets a **new member ID**. Send another message; both windows show the next sequence with that ID.

## Invite expiry

Leave Client offline. Wait more than **300 seconds from Host's J press**, then Client shows full details with **F2** if needed and presses **F3** to try the retained invite.

**Pass:** Join completes **Expired**, and Host admits no new member.

Client **X** returns Offline. Host **X**, then **J** creates a new lobby. Host **P** copies its new invite; Client **K** joins from that clipboard invite.

**Pass:** Both join the new epoch with two members. An old invite cannot admit into it.

## Host loss and recovery

With Client joined, close Host.

**Pass:** Client shows **Failed**, an error, and no connected roster or cached transcript presented as connected state.

Client **X** returns Offline. Restart Host and select **Tab > CkOnline > Online Lobby**. Host **J**, wait for **Hosting**, then **P** copies the new invite. Client **K** joins from that clipboard invite; wait for **Joined**.

**Pass:** Both show the new epoch and roster. Send a new message to verify the new connection.

## Notes

- Use **Z** to stop the gym's profile before changing its name/address settings and hosting again.
- Invite lifetime is five minutes from hosting; copying it again does not renew it.
- The service retains 32 messages; the panel shows the newest six.
- Keep **127.0.0.1** for local tests. Optional cross-machine LAN/Tailscale testing needs Host **L** before hosting, a reachable IPv4 address and TCP 7788 access. That path remains unverified.
- Record errors/ensures from both Output Logs. These gyms test TCP lobby control, independently of UE gameplay replication.
