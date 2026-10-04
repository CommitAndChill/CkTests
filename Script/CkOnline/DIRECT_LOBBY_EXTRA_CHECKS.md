# Lobby and chat: extra checks

Do these after the [five-step first pass](DIRECT_LOBBY_EDITOR_TESTING.md). They are still required before recording full manual acceptance.

## Ready off

Client presses **R** again.

**Pass:** Both rosters show Bob **not ready**.

## Leave and rejoin

Within five minutes of hosting, Client presses **X**, waits for **Offline**, then **K** using the retained invite.

**Pass:** Host stays **Hosting**; its roster goes **2 → 1 → 2**. The lobby epoch stays the same. Bob gets a **new member ID**. Send another message; both windows show the next sequence with that ID.

## Invite expiry

Leave Client offline. Wait more than **300 seconds from Host's J press**, then Client **K**.

**Pass:** Join completes **Expired**, and Host admits no new member.

Client **X** returns Offline. Host **X**, then **J** creates a new lobby. Host **P**; Client **O**, **Enter**, **K**.

**Pass:** Both join the new epoch with two members. An old invite cannot admit into it.

## Host loss and recovery

With Client joined, close Host.

**Pass:** Client shows **Failed**, an error, and no connected roster or cached transcript presented as connected state.

Client **X** returns Offline. Restart Host and create a fresh lobby/invite, then join explicitly.

**Pass:** Both show the new epoch and roster. Send a new message to verify the new connection.

## Notes

- Use **Z** to stop the gym's profile before changing its name/address settings and hosting again.
- Invite lifetime is five minutes from hosting; copying it again does not renew it.
- The service retains 32 messages; the panel shows the newest six.
- Keep **127.0.0.1** for local tests. Optional cross-machine LAN/Tailscale testing needs Host **L** before hosting, a reachable IPv4 address and TCP 7788 access. That path remains unverified.
- Record errors/ensures from both Output Logs. These gyms test TCP lobby control, independently of UE gameplay replication.
