# Online panel: short editor check

**Status (2026-10-04): Deferred.** Gate7 colors, guidance, details and focus are unchecked; Gate6 was reported passing separately.

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. In **Play > Advanced Settings**, set **2 players**, **Play Standalone**, **Launch Separate Server Off**, **Run Under One Process Off**. Launch **New Editor Window (PIE)**. Both windows: **Tab > CkOnline > Online Lobby**; **H** cycles panels. Window 1 is Host; window 2 is Client.

Host **J**, wait **Hosting**, then **P** copies invite. Client **K** validates and joins clipboard invite; wait **Joined** and two members. Manual entry: **O**, **Enter** saves invite; **F3** joins saved draft in details.

1. **Guidance:** Guided Compact is default. Check **Next step** and roster. Green means observed, blue pending/instruction, amber warning, red error. **F2** shows GUIDs, epoch and proof. No automatic overall PASS.
2. **Chat:** Both **R** until ready; both **Tab > CkOnline > Online Chat**. **B**, type, then **Esc** or close: no message. **B**, type, **Enter** sends; both see it.
3. **Gameplay and return:** In **Online Lobby**, **G** needs two ready members and IP Offline. Host **G**, Client **G**; wait Hosting/Connected. Client **P** sends proof. Compare marker, epoch and proof in details. Client **X**, Host **X** return to Lobby; identity, roster and chat persist.

[Direct recovery fault check](DIRECT_RECOVERY_EDITOR_TESTING.md) is a separate manual acceptance gate.
