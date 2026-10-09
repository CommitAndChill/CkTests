# Runtime Mesh / Runtime Convex gyms

Two switchboard entries on the shared gym level (`Content/TestGyms/TestGyms_CkTests_Level`):
- **Runtime Mesh** (category `CkRuntimeMesh`), described below.
- **Runtime Convex** (category `CkJolt`), documented in its own section.

## Runtime Mesh

### Launch

1. Open `TestGyms_CkTests_Level` and start PIE with a real renderer (not `-nullrhi`).
2. Press **Tab**, open the `CkRuntimeMesh` group, and pick **Runtime Mesh**.
3. From the console, `Ck_Gym_Next` and `Ck_Gym_Prev` travel out and back. `Ck_Gym_Restart` resets every station.

### Files

All files are under `Plugins/CkTests/Script/CkRuntimeMesh/`.

| File | Holds |
|---|---|
| `CkRuntimeMeshGym_GameMode.as` | The 7-line GameMode. |
| `CkRuntimeMeshGym_PlayerController.as` | Stations, panel rows, key dispatch, camera framing, the lights, EndPlay teardown and the tuners (plane offset, yaw and pitch, explode distance, re-cut axis, selections, orbit view). |
| `CkRuntimeMeshGym_Fixture.as` | The `CkRuntimeMeshGym` helpers: asset paths, display spec, cap, plane builders, and the conservation bound read from the default `FCk_RuntimeMesh_CutLimits`. Also one fixture object per station. |

**Fixture asset.** `/CkTests/CkRuntimeMesh/Render/SM_Checker_CPU` is a 10 cm cube with corners 0..10 cm, CPU access, slot 0 blue checker and slot 1 red checker. Cap triangles use material ID 2 (`M_Checker_Cap`, green checker) at 10 cm per UV unit, which is the face checker density.

**Ownership.** Each fixture owns one root entity per generation, created under the PlayerController's entity. A reset destroys that root whole and builds a new one on the same call stack.

**Generation guard.**
- Every slice carries an operation ID built from (station, generation, sequence).
- A resolution that is not the pending ID is counted as stale, its result entities are destroyed, and nothing else changes. This covers a reset, a closed gym, and a travel out.
- The panel shows the stale count on every station.

### Stations

Keys **1/2/3** pick the station that the panel controls and frame it. The keys below apply to every station.

| Key | Action |
|---|---|
| F | Re-frame and step the orbit view: front-left, front-right, behind, top-down. |
| R | Reset this station. |
| X | Reset all stations. |

The selected piece is drawn as a white wire box. The **Verdict** row summarizes the focused station.

#### 1 Plane

The source is shown at x20 display scale, off-origin and rotated (pitch 15, yaw 30).
- The cyan translucent slab is the cut plane. It is posed from the same source-local plane that the slice receives.
- The yellow line marks the positive side.

| Key | Action |
|---|---|
| I / K | Offset ±0.5 cm along the normal from the cube centre (clamped ±9 cm, so NoIntersection is reachable). |
| L / J | Yaw ±15. |
| U / O | Pitch ±15. |
| T | Cut the source. Disabled while a cut is in flight or before the source is Ready. |
| V | Select the source, positive or negative result, and frame it. |

- The source is immutable. Each successful cut replaces the previous pair, and the old pair is destroyed only once the new one exists.
- Results are displayed beside the source at the same rotation and scale: positive at +380 cm world Y, negative at −380.
- The rows show the plane in source-local cm, the last outcome, pair volumes and cap triangle counts, display states, and the selected piece's local bounds.
- The **Pair cut at** row shows the plane the displayed pair was actually cut with, and says whether the guide has moved since.

#### 2 Surface

The source sits beside one pair cut by a fixed oblique plane. The station cuts once automatically when its source becomes Ready.

| Key | Action |
|---|---|
| I / K | Explode ±0.5 cm per half along the plane normal. This moves the pair's display Transforms only, as the label says; the geometry is untouched and nothing simulates. |
| T | Re-cut, which replaces the pair. |
| V | Select a piece and frame it. |

The PlayerController creates a shadow-casting directional light and a sky light, following the VisualLod precedent. A 16 m grey receiver plane sits 2.3 m below the pair.

#### 3 Validation

The source stays alive as geometry. While it is a leaf, it is shown through a separate display entity.

| Key | Action |
|---|---|
| V | Select a leaf. |
| Y | Re-cut axis: X, Y, Z or diagonal, through the leaf centroid. |
| T | Cut the selected leaf. Disabled at 16 leaves or while a cut is in flight. |
| N | Plane at x=20, normal +X, on the source. Expect NoIntersection. |
| M | Plane at x=10. Expect TouchingOnly. |
| Z | Plane at x=0.001 with a 0.01 cm minimum normal extent. Expect RejectedTooSmall. |

The cases match `Test_RuntimeMesh_Slice.cpp` MissTouchAndInvalid. Leaves are displayed spread out by their centroid offset, which is a display offset only.

| Row | Shows |
|---|---|
| Leaves | Count against 16, Ready displays, cut count. |
| Last cut | parent → positive + negative, with the difference against max(0.01 cm³, 1e-4 × parent). |
| All leaves | The leaf sum against the source, and the summed per-cut bound. |
| Case result | Expected against observed outcome. A mismatch is flagged as a warning. |

### Limits

- **The conservation rows are not independent evidence.** The kernel rejects any cut that would break the same bound
  before it publishes a result (`Internal/CkRuntimeMesh_Slice.cpp:314-315`), so a `Succeeded` pair can never show "DOES
  NOT conserve". A would-be violation surfaces as `RejectedTooSmall`. A green row shows that the gym reads the bound
  correctly; it does not check the kernel.
- No physics. Physical separation, floor contact and displays following bodies belong to the Runtime Convex gym.
- One slice in flight per station. A cut is disabled while one is pending; R and X stay live by design.
- A station whose rebuild finds no live owner entity goes **Closed**. Its Verdict turns to a warning, and R, X or `Ck_Gym_Restart` replace it with a new fixture.
- A reset is synchronous, but a slice resolves within a frame or two. You cannot reliably hit "reset while pending" by hand, so automation forces it on the same call stack (see below).
- This gym changes no console variables, so there is nothing to restore. The lights belong to the PlayerController and go with it on travel.

### Gates and results (2026-10-09, editor closed)

All runs used `CkAuto/UnrealToolbox.exe --test --no-live --test-pattern <P> --project=D:\Repositories\CkRepos\Orion\Mars.uproject`. The first run added `--discover-fresh`. Logs are in the session scratchpad under `gymgates/`.

| Pattern | Result |
|---|---|
| `RuntimeMeshGym`, earlier runs (first with `--discover-fresh`) | 2 selected, 1 passed. The registry test failed only because the "Runtime Convex" entry had not landed yet. Toolbox exit 1. |
| `RuntimeMeshGym`, after the review fixes (final source, `Exec-fix-RuntimeMeshGym.log`) | 2 of 2 passed, Contaminated 0, 0 script errors, toolbox exit 0. |
| `GymRegistry` (after the registry edit) | 3 of 3 passed, exit 0. Baseline was 3 of 3. |

The tests:
- `Ck_AutoTest_RuntimeMeshGym_ResetDuringPendingCutCommitsNothing`:
  - A Plane fixture cuts and resets on the same call stack.
  - The in-flight cut resolves FailedCancelled, is counted stale exactly once, and commits no pair, results or outcome in the new generation.
  - The reset generation's root and source are destroyed, so the reset leaves nothing behind as well as committing nothing.
  - A cut on the new generation then commits two 500 cm³ halves, each with a display.
  - Closing the fixture destroys the live root, and a Closed fixture refuses `Request_Reset`. The test's `DoEndPlay` closes the fixture on every exit path.
- `Ck_AutoTest_RuntimeMeshGym_RegistryListsRuntimeMeshAndRuntimeConvex`: after `CkTests_Gyms::RegisterAll`, "Runtime Mesh" resolves under `CkRuntimeMesh` and "Runtime Convex" under `CkJolt`. It checks display name and category only.

### Open: only PIE with a real renderer can verify

- Station framing and every key. Programmatic dispatch through `Ck_RuntimeMeshGym_Control <row>` does not prove physical keyboard input.
- The guide slab tracking the plane tuners.
- Cut, re-cut and reset on screen.
- Caps rendering green checker, faces rendering blue and red checker, and seams showing no cracks between exploded halves.
- Cast shadows on the receiver plane.
- Travel out and back through the switchboard with no leftover pieces or panel and input errors.

## Runtime Convex

### Launch

Open `TestGyms_CkTests_Level` and start PIE with a real renderer. Press **Tab**, then open the `CkJolt` group and pick
**Runtime Convex**. `Ck_Gym_List` and `Ck_Gym_GoTo <i>` also work.

Files: `Script/CkJolt/CkRuntimeConvexGym_{GameMode,PlayerController,Fixture}.as`.

### Ownership and guard

Fixtures are UObjects (`UCk_RuntimeConvexGym_FixtureBase`, with Hull and Slice subclasses). Each owns one root per
generation and a real Jolt floor: a Static ExplicitShape box body, drawn by a NoCollision, DoNotBake mesh on a separate
scaled entity.

The generation guard has two parts:
- The generation number rides in word 0 of the slice OperationID.
- Every completion is matched against the current cut's operation ID and its half body handles.

A stale completion destroys what it produced and counts it.

### Stations

Global keys:

| Key | Action |
|---|---|
| 1 / 2 / 3 | Focus and frame a station. |
| X | Reset the focused station. |
| J | Toggle the Jolt hull view (`ck.Jolt.DebugDraw.Enabled`). It reads the cvar live and flips its current value; EndPlay pops the override, restoring your value. |

#### Hull

A Dynamic RuntimeConvex body built from 9 public-API points: the corners of a 100 cm cube plus its centre. It has an
explicit mass of 50 kg and unit scale. It is drawn by an unscaled engine cube on the body entity and sits on the Jolt
floor.

CkUnrealComponent pushes the owner's full world transform, scale included, so a component-level scale would be
overwritten.

| Key | Action |
|---|---|
| G | Impulse of mass × (0, 150, 300) cm/s. |
| K | Build a hull from 4 coplanar points. Expect Failed / HullFailed; it logs a CK ensure by design. |

#### Slice to physics

The CPU checker cube (`/CkTests/CkRuntimeMesh/Render/SM_Checker_CPU`, 10 cm, materials Blue, Red and Cap) is imported,
displayed, and given a RuntimeConvex body at 1 g/cm³.

A cut runs through the selected piece's centroid:
- **The source is isolated first.** It becomes a Kinematic body on the engine `Spectator` profile, and the slice is
  queued only once that swap has been applied (from the profile request's completion).
  - `Spectator` is the only engine profile that ignores PhysicsBody (BaseEngine.ini:3118).
  - `NoCollision` would not isolate it: the Jolt signature takes a profile's responses, not its CollisionEnabled, and
    NoCollision ignores only Visibility and Camera (BaseEngine.ini:3110).
  - Without isolation, the source stayed in Jolt one frame longer than the admitted halves, and the position solver
    pushed them about 1.3 cm apart.
- Each half gets its own Transform at the source pose, its own display and its own body (`PhysicsActor`).
- Mass is split by volume.
- The source stays until both half bodies are Ready. It is then destroyed, and the halves get a mass × speed kick along
  ±normal.
- **Rollback.** A rejected or failed slice restores the source to `PhysicsActor` + Dynamic at once. A partial
  composition restores it once both halves are torn down.

Policy, which is a gym fixture policy and not a food or momentum model:
- Only a Ready, asleep piece can be cut.
- At most 4 pieces.
- Plane normals alternate between world Y and world X.

| Key | Action |
|---|---|
| G | Cut the selected piece. |
| N | Select the next piece. |
| V | Separation speed: 0, 30, 60 or 120 cm/s. |

#### Lifecycle

The same fixture as Slice to physics.

| Key | Action |
|---|---|
| G | Cut. |
| K | Destroy the selected piece. The others live. |
| Z | Cut, then reset on the call stack that queues the slice. |
| B | Cut, then reset with both half bodies pending. |
| M | Cut, then destroy one half before admission. Both halves go and the source is kept. |

The panel shows:
- fixture-owned pieces and bodies;
- stale completions discarded;
- abandoned entities not yet torn down, which must reach 0. A handle reads invalid only from Teardown, after the JoltBody EndPlay has removed the body, so 0 means no abandoned body is left in Jolt;
- partial compositions.

### Tuners

- Density, max pieces, cap material ID, hull size and hull mass: the constants at the top of `CkRuntimeConvexGym_Fixture.as`.
- Floor height (`k_RuntimeConvexGym_FloorTopZ`) and the separation presets: the PlayerController.

### Limits

- One convex hull per piece, so concave detail is filled.
- 10 cm pieces need close framing.
- Bodies draw nothing; use J to see them.
- The Z interrupt resets inside the profile request's completion, which runs inside the JoltBody HandleRequests
  iteration, so it creates bodies mid-iteration. It passed without ensures but is not proven at the EnTT level. If it
  ever ensures, defer that reset by one tick.

### Tests

| Test | Proves |
|---|---|
| `Ck_AutoTest_RuntimeConvexGym_StaleCompletionsAfterResetLeaveNoBodies` | A reset on the same call stack as a cut commits nothing. A reset inside the slice receiver discards both pending half bodies. |
| `Ck_AutoTest_RuntimeConvexGym_PartialCompositionKeepsSourceAndDestroysBothHalves` | A half destroyed before admission resolves Cancelled. Both halves go and the source stays Ready. The source is Kinematic while the cut is in flight and Dynamic again once both halves are gone. |
| `Ck_AutoTest_RuntimeConvexGym_ChildrenOutliveSourceAndEachOther` | After a commit the source is gone, the halves' masses sum to the source's, and destroying one half leaves the other's entity, body and display alive. |
| `Ck_AutoTest_RuntimeConvexGym_CommitWithoutImpulseDoesNotPushHalvesApart` | With the 0 cm/s preset, the halves' centroid separation along the cut normal stays at its post-cut value within 0.1 cm. On the pre-fix fixture it was red: 4.906 cm expected, 6.190 cm measured. |

## Final gates (2026-10-09, finished tree, editor closed)

`CkAuto/UnrealToolbox.exe --test --no-live --test-pattern <P> --project=D:\Repositories\CkRepos\Orion\Mars.uproject`,
with the first run adding `--discover-fresh`. Every run exited 0, with no AngelScript errors and no new reds.

| Pattern | Baseline (untouched tree) | Final |
|---|---|---|
| `GymRegistry` | 3/3 | 3/3 |
| `RuntimeMeshGym` | none | 2/2 |
| `RuntimeConvex` | 3/3 | 8/8: the 3 baseline rows, 4 Convex gym tests and the registry test |
| `Ck.RuntimeMesh` | 35/35 | 37/37: the baseline, including SliceParity and the renderer-only render test, plus the 2 Runtime Mesh gym tests |

A focused green is not the full gate. Every visual and interactive check is still open until the real-renderer PIE pass.
