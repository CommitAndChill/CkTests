# Steam lobby editor check

**Pending while AFK.** This checkout has Steam disabled and no App ID. You can check its Unavailable diagnostic here; live joining needs a Steam-enabled project with its App ID and two computers using distinct licensed accounts.

**Setup on each computer:** open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`; **Players 1 / Standalone / Separate Server Off / Run Under One Process Off**. Launch **New Editor Window (PIE)**, then **Tab > CkOnline > Steam Lobby**.

1. **Availability:** press **S**. Here, expect **Unavailable**; **F2** shows why and **F** stops. This is a diagnostic, not a live pass. In the configured project, expect **Ready**. Joining there is **Host S → J / Client S → K → N to select → P**.
2. **Join:** use **Host S → J / Client S → K → N to select the host → P**. Both must show **Joined** and the same nonempty lobby ID. This checks membership in a discoverable lobby; roster, gameplay, chat and invites are outside this slice.
3. **Leave:** first join using **Host S → J / Client S → K → N → P**. Both press **X**. Pass only when both show **Ready**, **Lobby ID: none**, and successful leave completion. Cleaning up or Cleanup unknown is not a pass; stopping with **F** does not guarantee cleanup.

If Logged out, sign into the Steam client and press **L** to refresh; L does not open sign-in. **C** cancels a pending operation. Record states and lobby ID, not account IDs. Live checks remain deferred until the setup is available.
