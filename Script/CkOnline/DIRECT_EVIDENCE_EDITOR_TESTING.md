# Two log checkpoints

**Goal:** leave readable evidence that Lobby → Chat kept the connection and ready states, and both peers received the same message sequence.

Use the [same two-window setup](DIRECT_LOBBY_EDITOR_TESTING.md#setup). After the build, reopen the editor so **V** is available.

1. **Lobby:** host, join and ready both peers as before. Wait for **Succeeded** and matching rosters, then press **V** in each window.
2. **Chat:** switch both to **Online Chat**, send one message from each peer, wait until both transcripts agree, then press **V** in each window.

**Pass:** each window keeps its own generation, epoch, local ID and ready roster across travel. Both Chat checkpoints have the same epoch, roster and ordered message sequence/sender IDs. Generations differ between processes; compare each to its own earlier checkpoint.

The logs under `Saved/Logs/` contain `[OnlineCheckpoint]`. No invite, names or message text are recorded. Keep checking message text visually. **V** records a sample; it does not automatically declare PASS. If updates were still arriving, wait and press **V** again.

Report “both checkpoints match” or the first mismatch. Gameplay IP has its [own gate](DIRECT_IP_EDITOR_TESTING.md).
