# Direct gameplay IP editor gate

**Status: manual verification pending.** A same-process PIE test exercised host/join/proof/disconnect/rejoin but **failed** on engine Iris ensures in `OnlineDirectGates-06.log`. The separate-process check below has not been observed. This tests Unreal gameplay IP transport, not an online lobby or discovery.

## Setup

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`.

In **Play > Advanced Settings > Multiplayer Options**:

| Setting | Value |
|---|---|
| Number of Players | **2** |
| Play Net Mode | **Play Standalone** |
| Launch Separate Server | **Off** |
| Run Under One Process | **Off** |

Launch **New Editor Window (PIE)**. In both windows: **Tab > CkOnline > Online Direct IP**. **H** cycles the panel. Guided Compact is the default; **F2** shows full marker, peer ID and proof details. Both must start offline, with no remote peer or proof.

## Required checks

1. **Host — window 1:** Press **J** and wait through its `listen` map travel.

   **Pass:** Hosting at `127.0.0.1:7787`, a nonempty session marker, one local peer; window 2 remains offline.

2. **Join — window 2:** After Host shows **Hosting**, press **K** on Client to connect to `127.0.0.1:7787`. Wait for **Connected**.

   **Pass:** Connected and two peers in both rosters. Press **F2** in both windows to compare the identical full markers, distinct peer IDs and increased host join count.

3. **Remote proof — client:** Record its proof sequence, then press **P**.

   **Pass:** Client `Network proof` becomes **OBSERVED**. Use **F2** to compare the higher sequence and last-proof ID with the client's roster ID; Host shows the same sequence and ID.

4. **Leave/rejoin — client:** Press **X**, wait for offline, then press **R**. After rejoining press **P** again.

   **Pass:** Host roster falls **2 → 1 → 2**, leave and join counts rise, and host marker stays unchanged. Client proof returns to **PENDING** before the second **P**; afterward both show a newer sequence. Host stays Hosting.

**Optional host-loss check:** Press host **X** and wait for client Offline. Client **R** with no listener must fail and return Offline with an error, no false peer/proof. Host **J** and client **R** must then produce a new matching marker, two peers, and fresh client **P** proof.

The panel's **Next step** is an instruction, not a pass result. Green means observed local connection/proof, blue means instruction/pending, amber means warning and red means an actual error; there is no automatic overall PASS. Record markers, peer IDs, counters, and transitions in full details. **Any ensure, script error, or unexpected travel/network error in either Output Log fails this gate**, even if panels look correct.
