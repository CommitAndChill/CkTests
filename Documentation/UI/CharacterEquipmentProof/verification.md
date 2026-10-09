# Character and equipment proof — 2026-10-08

The screen is authored in AngelScript plus HTML/CSS and mounted in a code-created UMG/CommonUI host. No Widget Blueprint or baked full-screen UI image supplies the controls. Generated artwork is separate from live labels, slots and interaction.

## Current material/spacing revision

- Incremental Editor build succeeded in Equipment-Material-Spacing-Final.log. AngelScript accepted the new reflected StyleTokens setter and loaded all four new image bindings through the actual gym path.
- Saved/Logs/Equipment-Material-Spacing-Verified.log: fresh rendered Ck.UiAuthoring.CharacterEquipment gate **2/2 passed**, zero failed/skipped/contaminated, 50 seconds.
- Saved/Logs/Equipment-Session-Tokens-Verified.log: incremental build succeeded; **4/4 Ck.UiAuthoring.UMG.Session tests passed**, zero failed/skipped/contaminated, 1m 7s. Covers style-token geometry/snapshot/failed-reload preservation plus existing admission, native event transactions and owner/GC lifecycle. Toolbox ran one renderer-only sibling in a separate editor. The first new token fixture incorrectly sized its mount-owned root; nesting the sized panel under an unsized root corrected the test.
- Both final logs were inspected with no ensure failure, AngelScript error or automation error; existing engine/plugin warnings and Chromium startup diagnostics remain.
- Measured assertions prove identity top/bottom insets, glyph/label/value ordering and containment, item-card text/delta insets, and the six-by-four inventory layout at 1280x720, 1920x1080 and 2400x1350. The same gate retains routed input, drag/drop, typed stats, filtering, ring targeting, consumption, reload and teardown coverage.
- Source preflight matches **115 authored IDs and 100 CSS classes** plus shared template signatures; the validator's **18 tests pass**.
- Inspected final three-size captures and the supply/tooltip window. No text overlap or clipping was visible in the reviewed regions; this is a bounded visual review, not pixel-difference proof against the generated reference.
- The initial material pass failed identity/stat geometry. The next pass fixed identity but diagnostics showed a stat value line box extending four logical pixels past its cell; increasing the internal cell budget resolved it without lowering assertions. Supply visibility was a test-helper error: a collapsed parent may leave a descendant's local visibility stale. The corrected test checks effective ancestor visibility alongside the false gear bindings and empty power value.
- The current inventory cells are 104 logical units square, with 8-unit gaps and deliberate wrapping allowance. Fonts keep natural measured height. Spacing tokens are shared, but fixed geometry still requires room for actual font metrics.

## Earlier detail revision: focused checks

- `Saved/Logs/Equipment-Detail-Interaction-Gate.log`: incremental Editor build passed; fresh rendered `Ck.UiAuthoring.CharacterEquipment` gate **2/2 passed**, zero failed/skipped/contaminated, 50 seconds of tests.
- `Saved/Logs/Equipment-Detail-ItemSlot-Gate.log`: fresh rendered `Ck.UiAuthoring.ItemSlot` gate **2/2 passed**, zero failed/skipped/contaminated, 35 seconds. Includes drag atlas UV snapshot, readable label/key fallback, zero/negative/nonfinite brush dimensions, invalid index, incomplete atlas admission rejection and the existing ownership/drop lifecycle cases.
- Both final logs inspected: no ensure failure, AngelScript error or automation error. Existing engine/plugin warnings and Chromium USB/CRL startup diagnostics remain; this is not a clean-all-logs claim.
- Source preflight matches **98 authored IDs and 86 CSS classes**, including template contracts. The validator's own **18 tests pass**. Source checks are not a renderer substitute.
- Five final captures inspected in `Saved/Automation/CharacterEquipmentProof`: 1280x720, 1920x1080, 2400x1350, selected supply and containing-window tooltip. All 24 cells are asserted inside their grid with six columns and four rows. The 2400 capture contains a legitimately hovered equipment tooltip; it is not a clean baseline comparison image.

The production PIE test travels through the gym selector and mounts the script-created UMG/CommonUI screen. It verifies routed native filter and footer clicks, keyboard/synthetic gamepad navigation, actual Slate pointer drag detection and drop mutation, typed totals/deltas, ring destination and swaps, potion quantity/depletion, rejected requests, filtering, popup bounds, reload and owner release. Routed events are injected through Slate; this is not physical mouse/controller hardware certification.

The first detail gate stopped on an incorrect test lookup of a layout wrapper as an action card. The second revealed that synthetic held-mouse movement skips Slate drag detection; the helper was corrected to use non-synthetic routed movement and to require an active drag before release. A displaced-record assertion now waits for the actual presenter tick. These were test corrections, not production defects disguised by direct callback calls.

Earlier `Equipment-Layout-Gate.log` (2/2) and `Equipment-Ownership-Gate-Fixed.log` item-slot passes remain historical evidence for the preceding revision. They are superseded for this screen by the receipts above.

## Broader run remains non-green

The shared UI run was 246/250 before the final equipment corrections. CharacterEquipment.PIE subsequently passed its focused gates. Three sibling failures remain unresolved:

- `Ck.UiAuthoring.EcsDebugger.StateMachineInspector.AuthoredVariants`: pointer hover did not reveal Stop, followed by callback and teardown failures.
- `Ck.UiAuthoring.PauseMenuProof.GymSelector.PIE`: native pause-menu activation/teardown expectations fail; this was also recorded before this task.
- `Ck.UiAuthoring.SchedulerDebugger.Inspector.AuthoredComposition`: expected repeated dependency widget was absent.

The StateMachine and Scheduler failures repeated before and after the structural sizing-wrapper change. That does not establish their root causes or prove they predate this task. No repository-wide green result is claimed.

## Lessons verified by this screen

- Use `FStructOnScope` for reflected test parameters: allocating only `ParmsSize` and initializing a whole UFunction caused memory corruption in the new helper.
- Keep an authored popup slot owned while closed; transfer it between hidden parking and the current-window popup without duplicate parents.
- Resolve repeated controls by stable record key and local ID, refresh changed presenters before focusing, and reacquire records after updates.
- Structural sizing wrappers must not intercept pointer input intended for descendants or controls beneath decoration.
- Oversized generated frame corners occluded header/footer even though layout bounds were valid. The revision uses a thin perimeter asset with a transparent content-safe interior; screenshots confirm it clears the live controls.
- A 908-unit row of six 143-unit cards and five 10-unit gaps had no rounding allowance. At 1.25 scale it wrapped into five columns. That revision used 140-unit cards. The first detail revision used 112-unit cards in a 748-unit repeat with 8-unit gaps and preserves rounding allowance; the rendered geometry test prevents recurrence.
- Route held pointer moves as non-synthetic for a real Slate drag test; synthetic moves intentionally skip drag detection. Require an active drag and final model mutation, not merely a handled mouse-up.
- A custom control may already exist natively yet be absent from the reflected session registry; verify the actual authoring host.
- Align each child in mixed-font rows. Parent alignment is not inherited; natural text height avoids unnecessary line-box offsets.

These lessons and the earlier font, padding, transparency, key-hint, cooldown and orb lessons are standing requirements in the [general UI notes](../README.md#standing-ui-authoring-rules), referenced by root AGENTS.md.

## Environment and limits

Rendered checks used temporary DX11 because this engine checkout previously hit a DX12 shader-frequency assertion. The original DefaultEngine.ini was restored byte-for-byte afterward (SHA256 `BC18361DD0D9985A2A97CCEBB8AAA2CC04F5896965FAE15D03F66F01746AFC9B`). This is Editor/DX11 evidence, not DX12 or packaged-game verification.

The character is a static transparent illustration. Attributes and item properties are fixture data; there is no persistent inventory, live gameplay adapter or skeletal character preview. The fixed 1920x1080 canvas scales to fit. Arbitrary aspect ratios, user-selected UI scaling, physical gamepad usability and cooked resource/font lookup remain unproven. The screen uses original generated art inspired by Diablo; it is not a measured replica of proprietary assets.
