# Configured gameplay port check

**Status:** unchecked manual editor check; no automatic pass. Both windows must set the same gameplay port; the control TCP invite does not advertise it.

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. In Play > Advanced Settings set **Number of Players 2**, **Play Net Mode Play Standalone**, **Launch Separate Server Off**, and **Run Under One Process Off**. Launch **New Editor Window (PIE)**. In both windows select **Tab > CkOnline > Online Lobby**.

1. While the Direct IP fixture is Offline, show details with **F2** only if hidden. Use **F4** to enter `0`; confirm the port stays at its old value. Set **7789** with **F4** in both windows.
2. Host presses **J**, waits for **Hosting**, then **P**; Client presses **K**. Confirm both show gameplay target port **7789**. Both press **R** and confirm READY; record each lobby's generation, epoch, member IDs, and readiness.
3. Host presses **G**, waits for IP **Hosting**, then Client presses **G** and waits for **Connected**. Client presses **P**; wait for Network proof **OBSERVED on connected client** and a newer proof sequence. Client presses **X**, then Host presses **X**. Confirm both return to Lobby with the recorded generation, epoch, member IDs and READY states.

Script errors or ensures fail this check.
