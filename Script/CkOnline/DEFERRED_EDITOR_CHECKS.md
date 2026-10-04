# Deferred online editor checks

**Status, 2026-10-04:** The user deferred the following manual gym checks while AFK. They are unchecked, not failures or passes. No reminder is scheduled.

- **Gate6, previously reported passing:** The earlier two-window [lobby/gameplay round trip](LOBBY_GAMEPLAY_EDITOR_TESTING.md) was reported working by the user. That report does not cover the newer panel or recovery controls.
- **Gate7, unchecked:** Run the [online panel check](ONLINE_PANEL_EDITOR_TESTING.md) for guided colors, Next step prompts, F2 details, chat input focus/cancel, and return guidance. Its guide repeats the two-Standalone-window setup and Host **J** → Hosting → **P** / Client **K** join sequence.
- **Gate8, unchecked:** Run the [Direct recovery fault check](DIRECT_RECOVERY_EDITOR_TESTING.md) for real F6 readiness loss, F7 control-profile stop, automatic Lobby return, and fresh recovery. Its guide repeats the same setup and join sequence.
- **Gate9, unchecked:** Run the [advertised-endpoint check](ADVERTISED_ENDPOINT_EDITOR_TESTING.md), starting with its local default-address pass. Its self-contained guide repeats Host **J** > Hosting > **P** / Client **K** join, then gives the deferred two-machine manual-invite path. No cross-machine behavior is claimed here.
