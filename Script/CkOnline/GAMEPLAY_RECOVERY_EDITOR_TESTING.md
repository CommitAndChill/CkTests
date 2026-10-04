# Gameplay recovery

**Deferred, 2026-10-04:** Expected observations only; no manual pass reported.

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. **Play > Advanced Settings:** **2 players**, **Play Standalone**, **Launch Separate Server Off**, **Run Under One Process Off**. Launch **New Editor Window (PIE)**. Both windows: **Tab > CkOnline > Online Lobby**. Host **J**, wait **Hosting**, **P** copy invite; Client **K** join, wait **Joined** and two-member roster. Both **R** until Ready. **F2** shows IDs and epoch.

1. **Client G first:** Leave Host in Lobby; press Client **G**. Expect failed entry and automatic Lobby return; the Failed frame may be brief. Check captured error, Host TCP Hosting, unchanged epoch/IDs/ready roster/chat, and no old proof. Recover: Host **G**, wait IP **Hosting**; Client **G**, then **P** for fresh proof.
2. **Host X first:** Once connected and proven, press Host **X**. Expect Client connection loss and Lobby return with captured error; neither remains gameplay Connected. Compare retained TCP epoch/roster/ready/chat. Re-enter: Host **G**, wait IP **Hosting**; Client **G**, then **P**. Require fresh proof.

Report stuck travel, changed identity, missing error or stale proof. No automatic PASS. Bridged retry uses Lobby **G**, never Direct IP **R**.
