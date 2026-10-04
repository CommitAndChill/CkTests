# Lobby and chat: start here

**Goal:** connect two game instances, share ready states and chat, then prove the connection survives switching gyms.

22 focused automated tests passed. **You reported all five earlier editor steps passed on 2026-10-03.** Logs corroborate the sessions and travel; roster, readiness and message delivery rely on your visual check. The simpler guided panel is a new UI pass, pending manual observation.

## Setup

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`.

In **Play > Advanced Settings**:

| Setting | Value |
|---|---|
| Number of Players | **2** |
| Play Net Mode | **Play Standalone** |
| Launch Separate Server | **Off** |
| Run Under One Process | **Off** |

Launch **New Editor Window (PIE)**. In both windows: **Tab > CkOnline > Online Lobby**. **H** cycles the panel if hidden. It starts in guided Compact view; **F2** shows full details for GUID/epoch comparisons.

Both must start **Offline**. Call window 1 **Host**, window 2 **Client**. Standalone is expected: **J/K** create the TCP connection.

## Five steps

### 1. Host

Use **N** to name Host `Alice` and Client `Bob`; **Enter** saves text. Host presses **J**, then **P** to copy its invite.

**Pass:** Host shows **Hosting**, one member. Client stays **Offline**.

### 2. Join

Client presses **K** to read and validate the copied invite and join immediately. Wait for **Joined**. **O**, **Enter** saves a manually edited invite; **F2**, then **F3** joins that saved draft without rereading the clipboard.

**Pass:** Client shows **Joined**. Both show two named members. Press **F2** in both windows to compare the full epoch and member IDs.

### 3. Ready

Client presses **R**. Then Host presses **R**.

**Pass:** Both rosters show each ready change.

**G** becomes available only with exactly two ready members and the IP helper Offline.

### 4. Switch gyms

With full details visible (use **F2** if needed), note each window's **service generation**, the **lobby epoch** and roster. In both: **Tab > CkOnline > Online Chat**.

**Pass:** All those values remain unchanged. No reconnect needed.

### 5. Chat

Host: **B**, type `hello from Alice`, then **Enter** to send. Client repeats with `hello from Bob`. **Esc** or closing the text window cancels. **I** in full details resends a saved draft only when deliberately requested.

**Pass:** Both windows show both messages with matching text/sender IDs and increasing sequence numbers.

Wait for **Succeeded** and the expected readback at each step. Guided view shows connection, concise roster/chat and **Next step**. Green means observed local state, blue means instruction/pending, amber means warning, red means an actual error; there is no automatic overall PASS. **F2** exposes the full readback. Script errors or ensures are a failure.

That's the first pass. Report the first failing step and visible error, or that all five passed.

[Short guided-panel check](ONLINE_PANEL_EDITOR_TESTING.md) · [Next: Lobby → gameplay → Lobby](LOBBY_GAMEPLAY_EDITOR_TESTING.md) · [Two log checkpoints](DIRECT_EVIDENCE_EDITOR_TESTING.md) · [Optional extra checks](DIRECT_LOBBY_EXTRA_CHECKS.md) · [Standalone IP check](DIRECT_IP_EDITOR_TESTING.md)
