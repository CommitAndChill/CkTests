# Online gym return check

**Status:** pending, unobserved editor check; no automatic pass. This card covers the combined normal path. Detailed old-invite, failed-entry, and control-loss checks remain separately deferred below.

## Setup

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. Set **2 players**, **Play Standalone**, **Launch Separate Server Off**, and **Run Under One Process Off** in Play > Advanced Settings. Launch **New Editor Window (PIE)**; in both windows select **Tab > CkOnline > Online Lobby**.

## Checks

1. Show details with **F2** only if hidden. Before gameplay, use **F4** to enter `0`; confirm the old port remains. Set **7789** in both windows and confirm the selected port and readable status colors.
2. Host presses **J**, waits **Hosting**, then **P**; Client presses **K**. Both press **R**, then Host sends one chat message with **B** / Enter. Record each window's generation, epoch, member IDs, readiness, message and completed count. Host presses **Y**; wait for one additional completed operation and Last completion **Succeeded**. Confirm lobby identity and chat are unchanged.
3. Host presses **G**, waits IP **Hosting**; Client presses **G**, waits **Connected**, then presses **P**. Confirm **OBSERVED on connected client** and a newer proof sequence. Client presses **X**, then Host **X**. Confirm the recorded control values are unchanged after return.

Script errors or ensures fail. [Detailed deferred checks](DEFERRED_EDITOR_CHECKS.md) include old-invite rejection and recovery faults.
