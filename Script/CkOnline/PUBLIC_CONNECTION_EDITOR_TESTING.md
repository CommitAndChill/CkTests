# Public connection owner check

**Status:** pending, unchecked human observation while AFK; no automatic pass. A live transport alone does not mean the gym's ready-lobby check has passed.

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. In Play > Advanced Settings set **Number of Players 2**, **Play Net Mode Play Standalone**, **Launch Separate Server Off**, and **Run Under One Process Off**. Launch **New Editor Window (PIE)**; in both windows select **Tab > CkOnline > Online Lobby**.

1. Host presses **J**, waits for **Hosting**, then **P**; Client presses **K**. Both press **R**. Show details with **F2** only if hidden. Confirm the same two ready Lobby members; Foundation remains **Idle** before gameplay entry.
2. Host presses **G**, waits for IP **Hosting**; Client presses **G**, waits **Connected**, then **P** for fresh proof. Show details with **F2** only if hidden in each panel. Confirm Foundation is **Active**, with a session ID and successful **ConfirmGameReady** result. Client **X** and Host **X** return separately; confirm each readback reports its completed **Return** and **Idle** owner.
3. Re-enter gameplay: Host **G**, wait for IP **Hosting**; Client **G**, then **P** for fresh proof. Host presses **X** first; confirm Client detects connection loss and returns automatically, then verify the Foundation owner/readback settles idle. Use the separate [control recovery check](DIRECT_RECOVERY_EDITOR_TESTING.md) for F6/F7.

All checks remain unobserved until run. No automatic pass or provider authentication is claimed.
