# Advertised gameplay endpoint

**Deferred manual Gate9:** Verify advertised IPv4 becomes the Unreal gameplay target. No two-machine result has been observed.

**Local setup:** Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. In **Play > Advanced Settings** choose **2 players**, **Play Standalone**, **Launch Separate Server Off**, **Run Under One Process Off**. Launch **New Editor Window (PIE)**. Both windows: **Tab > CkOnline > Online Lobby**; **H** reveals a hidden panel.

1. Leave Host at `127.0.0.1`. Host **J**, wait **Hosting**, then **P** copies the invite. Client **K** joins that clipboard invite; wait **Joined** and two members. Both **R** until ready. **F2** shows `Host endpoint` and `Gameplay target`: compare `127.0.0.1:7788` and `127.0.0.1:7787` in both windows.
2. Host **G**, wait gameplay **Hosting**; Client **G**, wait **Connected**, then Client **P** sends proof. Compare two peers, marker, epoch and proof sequence. Client **X**, then Host **X**, returns to Lobby.

**Two machines, deferred during user AFK (2026-10-04):** Each machine: **1 player**, Standalone, same map/settings, Online Lobby. Host **F2**, **L**, enter reachable IPv4, **Enter** *before* **J**; wait **Hosting**, then **P** copies a new invite. Transfer it to the other machine. Remote Client **F2**, **O**, paste invite, **Enter**, then **F3** joins the saved invite. Wait **Joined** and two members; both **R** until ready. Compare endpoints, then Host **G**, wait **Hosting**, Client **G**, Client **P**, Client **X**, Host **X**. Reachable TCP **7788** and gameplay **7787** are prerequisites. Record errors and Output Log ensures.
