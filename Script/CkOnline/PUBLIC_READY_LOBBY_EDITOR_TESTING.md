# Public ready-lobby binding check

**Purpose:** the gym now consumes Foundation ready-lobby validation. Readiness blocks gameplay travel, and a matching lobby preserves identity through gameplay return. **Status:** unchecked manual check; no automatic pass.

## Setup

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. In Play > Advanced Settings set **Number of Players 2**, **Play Net Mode Play Standalone**, **Launch Separate Server Off**, and **Run Under One Process Off**. Launch **New Editor Window (PIE)**. In both windows select **Tab > CkOnline > Online Lobby**.

## Checks

1. Host presses **J**, waits for **Hosting**, then **P**; Client presses **K**. Before either presses **R**, confirm **G** is disabled; pressing it causes no gameplay travel.
2. Both press **R** and confirm both are READY. Show details with **F2** only if hidden. Record each window's service generation, lobby epoch, and member IDs. Host presses **G**, waits for IP **Hosting**, then Client presses **G** and waits for **Connected**. Client presses **P**; confirm Network proof reads **OBSERVED on connected client** with a newer proof sequence.
3. Client presses **X** to return to Lobby; then Host presses **X**. Confirm both are back in Lobby with the same generation, epoch, member IDs, and READY states recorded before travel.

Report the first mismatch or that all three checks matched. Script errors or ensures fail the check.
