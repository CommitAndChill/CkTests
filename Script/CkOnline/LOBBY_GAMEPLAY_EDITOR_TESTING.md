# Lobby → multiplayer gameplay → Lobby

**You are testing:** the TCP lobby, ready roster and chat survive a real Unreal IP gameplay connection and return travel. Gameplay admission as a specific lobby member is a later gate.

**Acceptance:** you reported the earlier four-step round trip worked. The simpler guided panel is new and remains pending a manual UI pass; the steps below describe that current interface.

Reopen the editor after the build and open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. In **Play > Advanced Settings**: **2 players**, **Play Standalone**, **Launch Separate Server Off**, **Run Under One Process Off**. Launch **New Editor Window (PIE)**. In both windows: **Tab > CkOnline > Online Lobby**; **H** cycles the panel. Guided Compact is the default; **F2** shows full IDs, epoch and proof fields.

**How to join:** Host presses **J**, waits for **Hosting**, then **P** to copy the invite. Client presses **K** to validate the clipboard invite and join directly. Wait for Client **Joined** and the same two-member roster/epoch in both windows. **O**, **Enter** saves a manually edited invite; **F2** shows **F3** to join that saved draft.

Send one chat message first: Host **B**, type, **Enter** sends it. **Esc** or closing the text window cancels. This reported local pass used one machine and the default advertised `127.0.0.1` address. **G** now uses the active lobby snapshot's host address for gameplay on port `7787`; **F2** shows the TCP host endpoint and gameplay target in both panels. The separate advertised-endpoint check remains pending.

1. **Ready.** With either member unready, **G** is disabled and no travel occurs. Press **R** in both windows until exactly two members are ready and the IP helper is Offline; **G** becomes available. Press **V** in both to record their identities. **V** works in Compact or full details. Native invalid-input coverage is separate from this disabled UI check.
2. **Enter and prove.** Host **G**; wait for **Hosting (IP listen)**. Client **G**; wait for **Connected**, then Client **P**. Both must show two UE peers. Press **F2** to compare the full gameplay marker, lobby epoch and advanced proof sequence/player ID. Press **V** in both.
3. **Return.** Client **X**; wait for Lobby. Then Host **X**; wait for Lobby. Press **V** in both. Generation/local ID, shared epoch, ready roster and earlier chat must remain unchanged.
4. **Chat again.** Host **B**, type a new message, then **Enter** sends it directly. Both must see it once with the same next sequence/sender ID.

**Failure:** any ensure/script error, wrong epoch, stuck travel or changed identity. Report the first failing step. Logs use `[OnlineCheckpoint]` and `[OnlineGameplayCheckpoint]`; they omit invites, names and message text.

Guided colors report local evidence: green observed, blue instruction/pending, amber warning, red actual error. The **Next step** prompt is guidance; there is no automatic overall PASS.

This is a controlled transport/continuity fixture. It does not authenticate Direct members against gameplay PlayerStates. The old same-process Iris test remains red; this separate-process result is recorded independently.

[Advertised endpoint: local first pass and deferred two-machine check](ADVERTISED_ENDPOINT_EDITOR_TESTING.md)
