# Unrelated network failure isolation check

**Status:** unchecked manual check; no automatic pass. The malformed unrelated network event must be rejected while genuine gameplay loss recovery remains intact.

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. In Play > Advanced Settings set **Number of Players 2**, **Play Net Mode Play Standalone**, **Launch Separate Server Off**, and **Run Under One Process Off**. Launch **New Editor Window (PIE)**; in both windows select **Tab > CkOnline > Online Lobby**.

1. Host presses **J**, waits for **Hosting**, then **P**; Client presses **K**. Both press **R**. Record lobby epoch and member IDs. Host presses **G**, waits for IP **Hosting**; Client presses **G**, waits **Connected**, then **P** for fresh proof.
2. In each established gameplay window press **F2** only if details are hidden, then **F8**. Confirm a new **OBSERVED** number; Host remains IP **Hosting**, Client **Connected**, and the IP session marker, bridge epoch, members, and proof remain intact. Client presses **P** again; confirm the proof sequence advances.
3. Host presses **X** first. Confirm Client detects connection loss and automatically returns to Lobby. Confirm the same ready control profile, epoch and member IDs remain. A manual Client **X** return does not prove automatic recovery.

F8 tests rejection of an unrelated malformed event; it is not a real network disconnect. Script errors or ensures fail this check.
