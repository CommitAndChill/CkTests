# Queued Lobby travel admission check

**Status:** unchecked manual check; no automatic pass. F9 checks that a competing join is rejected while a real local Lobby reload is queued; it does not simulate a gameplay disconnect.

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. In Play > Advanced Settings set **Number of Players 2**, **Play Net Mode Play Standalone**, **Launch Separate Server Off**, and **Run Under One Process Off**. Launch **New Editor Window (PIE)**; in both windows select **Tab > CkOnline > Online Lobby**.

1. Host presses **J**, waits for **Hosting**, then **P**; Client presses **K**. Both press **R**. Record service generation, lobby epoch, member IDs, and readiness in both windows.
2. Show details with **F2** only if hidden. Client presses **F9**. Wait for the real Lobby reload and a new numbered **OBSERVED**. Confirm the competing join was rejected, the queued reload completed into an idle Lobby, and the ready control identity/epoch/member IDs/readiness match the record. The busy-travel rejection in Bridge last error is expected.
3. Host presses **G**, waits for IP **Hosting**; Client presses **G**, waits **Connected**, then **P** for fresh proof. Client presses **X**, then Host **X**. Confirm both return to Lobby with the same control identity.

The F9 diagnostic proves queued-entry preservation. It does not claim an actual gameplay disconnect; Host **X** exercises the ordinary return path.

Script errors or ensures fail this check.
