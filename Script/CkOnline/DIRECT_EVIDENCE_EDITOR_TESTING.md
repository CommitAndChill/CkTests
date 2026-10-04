# Two log checkpoints

**Goal:** leave readable evidence that Lobby → Chat kept the connection and ready states, and both peers received the same message sequence.

After the build, reopen the editor and open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. In **Play > Advanced Settings**: **2 players**, **Play Standalone**, **Launch Separate Server Off**, **Run Under One Process Off**. Launch **New Editor Window (PIE)**. In both windows: **Tab > CkOnline > Online Lobby**; **H** cycles the panel. Guided Compact is the default; **F2** shows full GUID/epoch details.

**How to join:** Host **J**, wait for **Hosting**, then **P** to copy the invite. Client **K** validates that clipboard invite and joins directly. Wait for Client **Joined** and the same two-member roster/epoch in both windows. For a manual invite, **O**, **Enter** saves a draft; **F2** shows **F3** for joining that draft.

1. **Lobby:** press **R** in each window until both members are ready. Wait for **Succeeded** and matching rosters, then press **V** in each window.
2. **Chat:** **Tab > CkOnline > Online Chat** in both. Host **B**, type, **Enter** sends; repeat on Client. **Esc** or window close cancels without sending. Wait until both transcripts agree, then press **V** in each window.

**Pass:** each window keeps its own generation, epoch, local ID and ready roster across travel. Both Chat checkpoints have the same epoch, roster and ordered message sequence/sender IDs. Generations differ between processes; compare each to its own earlier checkpoint.

The logs under `Saved/Logs/` contain `[OnlineCheckpoint]`. No invite, names or message text are recorded. Keep checking message text visually. **V** records a sample in Compact or full details; it does not automatically declare PASS. If updates were still arriving, wait and press **V** again.

Report “both checkpoints match” or the first mismatch. Gameplay IP has its [own gate](DIRECT_IP_EDITOR_TESTING.md).
