# Gameplay-backed inventory slice

This extends the existing Inventory menu through the real CkInventory DataOnly
backend. It is a small local-authority demonstration, not a consumer game's
inventory implementation or a multiplayer UI adapter.

In **UI Authoring**, close the menu to access **4 Gameplay inventory**, **5 Save**,
and **6 Load**. The backend initially owns Potion x6, Arrow x24 and Sword x1 from
the existing code-defined inventory item assets. **Discard removes the entire
selected item/stack**. Closing and reopening projects the same ECS inventory.
The gameplay host captures UI input without pausing ECS request processing.

The gym save slot is `CkUiGameplayInventoryProof`. Version 1 stores exactly three
catalog keys and committed quantities, including zero. ECS handles and UI
selection are never persisted. Load validates the complete payload before
creating a candidate; the previous accepted owner survives until all required
add requests, attribute updates and quantity checks finish. Malformed or
unsupported saves fail explicitly. Save refuses in-flight inventory work and
reports storage failure.

The same CSS and shared native cards/key hints are reused. Gameplay markup binds
definition `FText` and optional definition icons instead of using the fixture's
item paintings. The existing three demo definitions have no icons; their image
areas are blank. This slice establishes the backend boundary, not visual
acceptance for new item artwork. Unknown item categories remain under All, and
gameplay detail shows quantities rather than invented equipment statistics.

Gameplay input adds D-pad card navigation, shoulders for focus traversal and B
to close. Keyboard/mouse behavior remains available. Localized chrome uses
`FText` bindings, including the search hint; item text preserves definition
identity. Translation catalogs, glyph coverage, physical controllers and
independent user-selected UI scaling still require their own acceptance gates.

## Validation status

`Ck.UiAuthoring.GameplayInventoryMenuProof.PIE` covers real committed seed counts,
non-pausing host admission, synthetic controller routing, deferred discard,
pending duplicate/save rejection, unchanged collection revisions, close/reopen,
same-process disk save/recreate/load, malformed/version rejection and stale
owners. The compatible fixture menu tests remain separate.

On 2026-10-11 the incremental Editor build and rendered menu gate passed 5/5 tests, zero failed or contaminated, using D3D12 SM6. Receipt: `Saved/Logs/GameplayInventory-Gate-10.log`. No fresh script errors or CK ensures were found. This covers the gameplay test and four compatible fixture menu tests, not a repository-wide regression claim.

## Windows packaged gate

The gameplay markup, shared stylesheet/backdrop and menu fonts have NonUFS runtime
dependency declarations in `CkTests.Build.cs`; both plugin Script roots are
staged by `DefaultGame.ini`. The final stage was inspected for markup, stylesheet,
backdrop and menu fonts; its test script hash matches the final source.

Verified checks in the selected checkout:

1. Incremental Toolbox Editor build and fresh real-renderer menu test gate;
   the final gate used two Editor boots for fresh discovery and test execution.
   Earlier diagnostic iterations exposed owner metadata, deferred cleanup and
   synthetic CommonUI routing issues; the final receipt covers the corrected source.
2. Toolbox Game build succeeded in Development Win64; receipt: `Saved/Logs/GameplayInventory-Game-01.log`. It compiled 1227 actions and emitted Jolt import linker warnings. No Editor boot was required.
3. Toolbox v1.61 was vendored from published clean source `c467aeea` after the provenance guard passed. Its supported prerequisite path built UnrealPak, then the final cook/stage reused that executable and the Game build. `Saved/Logs/GameplayInventory-Package-05.log` and `Stage-20261011-04/UnrealToolboxWindowsPackage.json` record successful Development Win64 staging. Fresh cook has no script compile warnings/errors.
4. `InventorySave` passed in the staged Game client (37 seconds), then
   `InventoryLoad` passed in a separate fresh Game client (16 seconds).
   Receipts: `Saved/Logs/GameplayInventory-StagedSave-01.log.json` and
   `Saved/Logs/GameplayInventory-StagedLoad-01.log.json`. Both have exit 0,
   PASS and explicit Gauntlet completion markers. The save phase verified
   6/24/1 seed, deferred Potion removal/cleanup and saved 0/24/1. The fresh
   load restored 25 units, admitted the authored menu and verified two visible
   projected entries. Both used D3D12 SM6, windowed 1280x720.

Failed package receipts/stages are retained. No destructive cleanup or duplicate
checkout was used. Both client logs have 13 superclass binding-registration
warnings but no script-file compile diagnostics, failed ensures or fatal errors.
Rendered font/art fidelity, physical gamepad navigation, independent UI scaling
and repeated-open allocation/performance measurements remain separate checks.
The scenario-owned save slot is intentionally retained and must not be overwritten.
See `WINDOWS_PACKAGE_GATE.md` for the one-shot rerun procedure and full receipts.

## Integration lessons

- Definition-built transient item owners need explicit net metadata even when
  replication is disabled; validate this before creating inventory items.
- An inventory completion callback can precede deferred lifetime cleanup.
  Saving requires the coordinator and owned subtree to be drained as well as
  committed quantities to be correct.
- CommonUI can turn controller Accept into a virtual mouse press/release.
  Focus and a handled event alone do not prove activation: establish hover and
  hit reachability, then assert one action and its backend effect.
- Keep a synthetic FPointerEvent's pressed-button set alive through dispatch;
  the event borrows it rather than copying it.

The synthetic CommonUI Accept test explicitly establishes cursor position and
hover before native press/release; it asserts one action and backend removal.
Physical controller focus-to-cursor synchronization remains unverified.

Package attempt 02 completed cooking 338 packages, then failed because the engine
lacks UnrealPak.exe. Receipt: Saved/Logs/GameplayInventory-Package-02.log. The
missing IOS images were restored from the existing cached dependency pack after
verifying pack and file SHA-1 hashes; no engine Git source was changed. Four
shadowing warnings in the older mission-board/world-map scripts were corrected
with local variable renames; the final cook validated these changes without
script compile warnings. The exact cached resources and supported Toolbox
UnrealPak build resolved both packaging prerequisites.
