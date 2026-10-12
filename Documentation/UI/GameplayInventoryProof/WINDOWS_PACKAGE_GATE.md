# Windows cooked-client gate

Toolbox v1.61 was vendored from published clean source c467aeea after its provenance guard passed. No raw UAT exception is required. It builds a missing UnrealPak prerequisite before cooking and separately records that result.

Prerequisite completed: Toolbox Game build succeeded in Development Win64.
Receipt: Saved/Logs/GameplayInventory-Game-01.log. Reuse this build; do not clean,
rebuild through UAT, change engine settings or create another checkout.

Attempted detached Toolbox package arguments:

```text
--package
--reuse-game-build
--project=D:\Repos\CkPlugins_2
--game-build-log=D:\Repos\CkPlugins_2\Saved\Logs\GameplayInventory-Game-01.log
--package-config=Auto
--package-map=/CkTests/TestGyms/TestGyms_CkTests_Level
--stage-dir=D:\Repos\CkPlugins_2\Saved\UiGameplayInventoryPackage\Stage-20261011
--output=D:\Repos\CkPlugins_2\Saved\Logs\GameplayInventory-Package-01.log
--no-wait
```

The new stage/log must not exist and the Editor must be closed. Cooking requires
one cooking Editor process. Preserve existing cook/DDC/build outputs; no archive
copy or cleanup is included. Monitor the original detached Toolbox process and
read its final receipt; do not launch a duplicate after a timeout.

After successful staging, launch through Toolbox --staged-client with the same
project/stage and a new absolute Saved/Logs output. This is a rendered fresh
Game process, not Editor -game. A manual exit is not an automated test pass.

Verify staged resource paths and fonts, launch into the gym map and select UI
Authoring. Open Gameplay inventory with 4, discard a stack, close and save with
5, then quit. Launch a fresh process, load with 6 and verify committed quantities
and reopen behavior. Record actual executable, renderer, window dimensions,
resource paths and both process logs. Same-process disk recreation and Editor
-game are not substitutes for this gate.

`GauntletTests.json` now registers `InventorySave` and `InventoryLoad` in
`CkGameplayInventory_PackageGauntlet.as`. Run them serially through Toolbox
`--staged-client --staged-test=<name>` after a new successful cook/stage that
includes this script. The save phase verifies the real 6/24/1 seed, removes the
Potion stack through the deferred inventory request, waits for cleanup, saves
0/24/1, and admits the authored menu. The load phase restores those quantities
in a separate Game process and admits the menu again. Both scenarios passed on
2026-10-11 in the staged Development Win64 client; receipts are recorded below.

The baked test slot is one-shot: the save phase rejects an existing slot. Change
its suffix and recook before another run; never overwrite the gym's user slot.
Both phases require an explicit successful test marker and process exit. Menu
admission alone does not prove rendered font/art fidelity or physical input.

Physical gamepad navigation, independent UI scaling and repeated-open allocation
measurements remain distinct acceptance checks. Successful staging and both
fresh staged-client scenario receipts are established below.

## First package attempt

The 2026-10-11 run failed before cooking, with UAT exit 1 / MSB3554:
IOS.Automation.csproj references missing IOS/Resources/GreenCheck.png under the
engine checkout. Zero cooking Editor boots occurred; no staged executable exists.
The failed stage receipt and log are retained. A retry must use a new stage and
output name rather than delete or reuse this failed directory.

## Second package attempt

The two missing iOS PNGs were restored from the existing engine GitDependencies
cache, with pack and file SHA-1 verification. They are dependency-managed assets,
not missing tracked source. No engine source or dependency filter was changed.

`Saved/Logs/GameplayInventory-Package-02.log` records a completed cook of 338
packages, followed by failure because `Engine/Binaries/Win64/UnrealPak.exe` is
absent. UnrealPak is a source-built program, not a GitDependencies asset. Toolbox
currently has no supported prerequisite build path for it, so another package
attempt must wait for that capability or a supplied valid engine program.
The failed stage `Saved/UiGameplayInventoryPackage/Stage-20261011-02` is retained.

The cook also reported four AngelScript shadowing warnings in the missions and
world-map gyms. Their local variables were renamed; fresh compile validation is
pending alongside the new packaged test script. The next stage and output must
use new names, and should reuse the completed Game build without artifact cleanup.

## Prerequisite-capable run

The v1.61 source and clean deployed binary passed the consumer provenance guard
without force. `GameplayInventory-Package-03.log` records a fail-fast lock refusal
while another project's test held the engine; no stage was reserved or cook run.
The subsequent package run uses `GameplayInventory-Package-04.log` and
`Stage-20261011-03`, with normal Toolbox lock waiting. Monitor its original PID
from `Saved/Logs/GameplayInventory-Package-04.pid`; do not launch a duplicate.
This run built UnrealPak successfully in 189 seconds and staged successfully.
Its cook exposed one warning in the new test script (increment inside an if);
the increment was separated from the condition before the final gate.

## Verified packaged result

Final cook/stage: `Saved/Logs/GameplayInventory-Package-05.log`, stage
`Saved/UiGameplayInventoryPackage/Stage-20261011-04`. Toolbox reused UnrealPak
and the completed Development Game build. UAT exited 0, the stage receipt has
`StageValidated=true`, and no fresh AngelScript compile warnings/errors were
found. The staged test script SHA-256 matched the source. Gameplay markup,
shared stylesheet/backdrop, SourceSans3 and MarcellusSC font files exist in the
staged plugin Resources directory.

The two separate staged Game invocations passed:

- `GameplayInventory-StagedSave-01.log` and its `.json` receipt: PASS, exit 0,
  37 seconds. Verified 6/24/1 seed, deferred Potion removal and cleanup, saved
  0/24/1, and admitted the authored menu with two projected visible entries.
- `GameplayInventory-StagedLoad-01.log` and its `.json` receipt: PASS, exit 0,
  16 seconds. Fresh-process hydration restored 0/24/1 and admitted the same menu.

Both real client logs contain `TEST COMPLETE. EXIT CODE: 0`; neither has script
compile errors, script-file warnings, failed ensures, assertions or fatal errors.
They do emit 13 superclass binding-registration warnings per process (replication,
entity-owning actor and provider types); these are distinct from script compile
diagnostics and remain present. Runs used D3D12 SM6, windowed 1280x720.

These results prove the exercised packaged inventory persistence and authored
menu admission/projection paths. Staged font presence does not prove rendered
typography, and these scenarios do not drive physical controller input or
measure repeated-open allocations. The test-owned save slot remains on disk;
do not rerun InventorySave against it without changing the baked slot and recooking.
