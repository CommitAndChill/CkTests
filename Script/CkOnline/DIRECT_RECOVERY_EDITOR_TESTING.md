# Direct lobby recovery: separate manual gate

**Status (2026-10-04): Deferred by user.** Gate8 F6/F7 recovery remains unchecked; the earlier Gate6 round trip was reported passing separately.

This is separate from the accepted round trip. F6/F7 issue real Direct service requests. The known same-process Iris failure is separate.

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`. In **Play > Advanced Settings** choose **2 players**, **Play Standalone**, **Launch Separate Server Off**, **Run Under One Process Off**. Launch **New Editor Window (PIE)**. Both windows: **Tab > CkOnline > Online Lobby**. Call window 1 Host, window 2 Client.

Host **J**, wait **Hosting**, then **P** copies invite. Client **K** joins clipboard invite; wait **Joined** and two members. Both **R** until ready. Host **G**, wait IP **Hosting**; Client **G**, wait **Connected**, then **P** for observed proof. In gameplay **F2** shows details.

1. **Readiness loss:** Client **F6**. Both automatically return to Lobby with a short red loss error; neither retains Connected or old gameplay proof. TCP profiles, identities and epoch remain; Client is unready. Client **R** until both ready; repeat Host **G**, Client **G**, Client **P** for fresh proof.
2. **Control stop:** From connected gameplay press **F7** in either window. Both return automatically; issuing TCP profile stops. Host F7 makes Client **Failed**; Client F7 leaves Host **Hosting** with one member. Neither shows old Connected or gameplay proof.
3. **Fresh recovery:** **X** on Failed profile; Host **X** if still Hosting. Host **J**, wait **Hosting**, **P** copies fresh invite; Client **K** joins it. Both **R** until ready, then repeat Host **G**, Client **G**, Client **P**.
