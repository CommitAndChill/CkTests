# Direct host invite renewal check

**Status:** unchecked manual editor check. There is no automatic pass.

## Setup

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. Set **2** players, **Play Standalone**, **Launch Separate Server Off**, and **Run Under One Process Off** in Play > Advanced Settings. Launch **New Editor Window (PIE)**; in both windows select **Tab > CkOnline > Online Lobby**. Window 1 is Host; window 2 Client.

## Check renewal

Host presses **J**, waits for **Hosting**, then **P**; save the OLD invite outside the clipboard. Client presses **K** and waits for **Joined** / two members. Both press **R** and send chat with **B** / Enter. Record epoch, member IDs, readiness, and chat in both windows (**F2** details).

Show details with **F2** only if hidden; record the host's completed count, then Host presses **Y**. Wait for one additional completed operation and Last completion **Succeeded**. Compare both windows: epoch, member IDs, readiness, and chat remain unchanged. Host presses **P** and saves the new invite separately.

Client presses **X**, shows details with **F2** only if hidden, then **O**; paste the OLD invite, press **Enter**, then **F3**. Expect **Unauthorized** with a new operation ID and no host-side member. Client presses **X**, copies the saved new invite to the clipboard, then **K**. Wait for **Joined**: same epoch, new client member ID.

No five-minute wait or remote-machine check is required. Report mismatches; script errors or ensures fail.
