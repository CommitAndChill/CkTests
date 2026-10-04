# Lobby and chat: start here

**Goal:** connect two game instances, share ready states and chat, then prove the connection survives switching gyms.

22 focused automated tests pass. **You reported all five editor steps passed on 2026-10-03.** Logs corroborate the sessions and travel; roster, readiness and message delivery rely on your visual check.

## Setup

Open `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap`.

In **Play > Advanced Settings**:

| Setting | Value |
|---|---|
| Number of Players | **2** |
| Play Net Mode | **Play Standalone** |
| Launch Separate Server | **Off** |
| Run Under One Process | **Off** |

Launch **New Editor Window (PIE)**. In both windows: **Tab > Online Lobby** under `CkOnline`. **H** shows the panel if hidden.

Both must start **Offline**. Call window 1 **Host**, window 2 **Client**. Standalone is expected: **J/K** create the TCP connection.

## Five steps

### 1. Host

Use **N** to name Host `Alice` and Client `Bob`; **Enter** saves text. Host presses **J**, then **P** to copy its invite.

**Pass:** Host shows **Hosting**, one member. Client stays **Offline**.

### 2. Join

Client presses **O** to paste the invite, **Enter**, then **K**.

**Pass:** Client shows **Joined**. Both show the same lobby epoch and the same two member IDs/names.

### 3. Ready

Client presses **R**. Then Host presses **R**.

**Pass:** Both rosters show each ready change.

### 4. Switch gyms

Note each window's **service generation**, the **lobby epoch** and roster. In both: **Tab > Online Chat**.

**Pass:** All those values remain unchanged. No reconnect needed.

### 5. Chat

Host: **B**, type `hello from Alice`, **Enter**, **I**. Client repeats with `hello from Bob`.

**Pass:** Both windows show both messages with matching text/sender IDs and increasing sequence numbers.

Wait for **Succeeded** and the expected readback at each step. The on-screen **Human verdict** stays **PENDING** by design. Script errors or ensures are a failure.

That's the first pass. Report the first failing step and visible error, or that all five passed.

[Next: two log checkpoints](DIRECT_EVIDENCE_EDITOR_TESTING.md) · [Optional extra checks](DIRECT_LOBBY_EXTRA_CHECKS.md) · [Gameplay IP check](DIRECT_IP_EDITOR_TESTING.md)
