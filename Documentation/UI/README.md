# UI authoring gyms

Open **CkTests → CkUI → UI Authoring** in the gym selector. Its info panel offers **[1] Pause**, **[2] Settings**, and **[3] Inventory**. Close an active menu before selecting another shortcut. The project retains the previously approved 30 FPS Editor startup cap.

**UI: Character & Equipment** is the next dark-fantasy proof: ten equipment slots, a six-column inventory, generated item/character art, native keyed drag/drop, rich comparison tooltips and owned custom-control focus. Its [breakdown and verification status](CharacterEquipmentProof/README.md) distinguish fixture behavior from shared pipeline additions.

**UI: World Map** is a scripted Vesper city-map fixture with keyed objectives, services, hazards and a player marker. Its info panel exposes open/close, filters, reset, source reload, invalid-record rejection and a long-label exercise on **1–7**. The [proof, source contract and widget breakdown](WorldMapProof/README.md) define its original reference and authored state. The native map aspect-fits and clips its image, retains a local camera, and proposes marker selection to the script model. Route display is a straight illustrative vector, not pathfinding; normalized marker coordinates and a 1024-record cap bound this control. Source preflight and design dimensions do not establish native pixels, input behavior or packaged lookup; those require the proof's focused runtime receipt.

Combat HUDs have separate selector entries: **UI HUD: Mass Effect 3**, **UI HUD: Battlefield 4**, and **UI HUD: Skyrim**. Each starts a passive code-created UMG HUD with its own art and authored layout. Its info panel exposes keys **1–8** for damage, sprint, fire, reload/replenishment, power/capture/spell, recovery, reset and low health; **9** cycles game-specific context. See the [combat HUD overview](CombatHudProof/README.md) for the current verification status.

**UI: Mass Effect 3 Power Wheel** is a separate squad-command gym. Hold Shift to open, hover or use arrows to select, click or Enter to queue, and release Shift to deploy. Its info panel also offers a latched preview on **1**, target protection on **2**, and fixture reset on **3**. The [power-wheel proof](PowerWheelProof/README.md) records the widget breakdown and verification status.

**UI: Skyrim Journal** exercises native quest and objective lists, tracking, completion, tab navigation and a 130-entry scrolling fixture. J opens/closes; its info panel exposes the fixture commands. The [journal proof](SkyrimJournalProof/README.md) records its full breakdown and [pipeline findings](SkyrimJournalProof/pipeline-findings.md). Its [focused native gate](SkyrimJournalProof/verification.md) passed 26 tests with 11 inspected captures at three viewport sizes. Shared deferred tree selection replaces the journal's timing workaround, and its typography now uses registered CSS families through the shared font-resource bridge. The separate [scalar scripting gate](ScriptingCardProof/verification.md) passed 11 tests with five inspected captures. [Richer bindings/resources and packaged lookup](font-authoring-gap.md) remain follow-up work; the [scripted Pause migration](ScriptedPauseProof/README.md) now moves the real gym Pause policy into AngelScript while retaining native Settings/Inventory.

The workbench proves code-authored in-game UI through **CkSlateLayout and CkYoga**. The menus use a code-created CommonUI UMG host; combat HUDs use a passive UMG host that leaves the world and player input active. Both mount an authored surface with native Slate controls and layout descendants. HTML/CSS files use the framework's supported grammar. Menu image-generation references guide design and measured atlas sprites supply menu artwork. The combat replicas use actual game references, native vector graphics and recreated squad portraits.

| Proof | What it exercises | Full breakdown |
| --- | --- | --- |
| Tactical spatial inventory | Multi-cell footprints, rotation, cross-container routed drag, occupancy rejection, container case and inspection | [Tactical inventory](TacticalInventoryProof/README.md) |
| Scripted world map | Keyed native map markers, visible-record filters, model-owned selection/route, local pan and zoom, source/invalid-state exercises | [Vesper world map](WorldMapProof/README.md) |
| Scripted Mission Board | AngelScript-owned five-kind flat records, keyed repeat actions, pure reorder, removal/reinsertion, completion, atomic failure/reload and teardown | [Mission Board](ScriptedCollectionProof/README.md) |
| Scripted Pause | Script-created UMG shell, shared source contracts, native buttons, input/pause ownership, retained Settings child and exact terminal admission cleanup | [Scripted Pause](ScriptedPauseProof/README.md); [original native breakdown](PauseMenuProof/README.md) |
| Settings | Typed editable fixture state, slider, checkbox, select popup, Apply/Cancel, retained reload and close/reopen behavior | [Settings menu](SettingsMenuProof/README.md) |
| Inventory | Typed keyed collection, generated RGBA icon atlas, wrapping repeated action cards, scroll, filters, debounced search, empty state, equip/use actions and saved session state | [Inventory menu](InventoryMenuProof/README.md) |
| Mass Effect 3 HUD | Upper-left quickslots, lower-left ammo, bottom-center curved shield/five health segments and squad portraits, reticle cooldown semicircles | [Mass Effect 3 HUD](CombatHudProof/Futuristic/README.md) |
| Battlefield 4 HUD | Conquest minimap, tickets and squad at lower left; compact ammo/firemode/health at lower right; conditional capture and kill feed | [Battlefield 4 HUD](CombatHudProof/Tactical/README.md) |
| Skyrim HUD | Thin compass and enemy health/name, independently fading three resource bars, sneak eye, interaction prompt and conditional arrows | [Skyrim HUD](CombatHudProof/Fantasy/README.md) |
| Mass Effect 3 power wheel | Native radial glyph selection, three independent actor queues, atomic release, shared cooldowns, target protection, pause ownership, fade, retained reload and cleanup | [Squad power wheel](PowerWheelProof/README.md) |
| Skyrim journal | Native quest/objective trees, tracking/completion, equal-width centered tabs, long copy, virtualized scrolling, pause ownership and retained reload | [Quest journal](SkyrimJournalProof/README.md) |
| Scripted UMG card | Script-created UMG tree, shared reflected scalar session, native edit proposals, script acceptance/actions, atomic source reload, owner retirement and GC; focused Editor gate passed | [Scripted card](ScriptingCardProof/README.md) |
| Owned authored-session input | Real CommonUI cover/uncover, nonzero-player focus/navigation, callback reentry, stable-ID reload, held-input draining and retained-control GC | [Shared input/focus proof](OwnedInputProof/README.md) |
| Layer admission lifetime | Exact weak player/context and admission generation, covered retention, inherited remove/re-push, pooled reuse, paused retirement, GC/reentry and actual AngelScript delegate consumer | [Shared layer membership proof](LayerMembershipProof/README.md) |

[Typography verification](typography-verification.md) records the corrected ammo/ticket layouts and fresh native captures of all six proofs at the three tested resolutions. The later [native default-font regression](DefaultFontProof/README.md) repaired placeholder glyphs when a host omits its base font; its 12-test focused gate and separate receipt include compatible scripting/card/journal/wheel/ownership coverage.

The [owned-session input gate](OwnedInputProof/README.md) passed 13 focused checks
with 12 inspected compatible Pause/card captures. It extends the shared reflected
session with an optional CommonUI owner and authored button focus/navigation.
The input API was exercised from C++ through a real second player's CkUI stack.
The later [layer membership gate](LayerMembershipProof/README.md) also passed
13 focused checks, including an actual AngelScript consumer of that observer.
The later [scripted Pause gate](ScriptedPauseProof/README.md) passed 15 selected checks with 27 inspected captures. The real `[1]` shortcut creates the script widget; Cancel/Escape from its native Settings child returns to the same parent/session/view/revision. Terminal cleanup preserves a newer admission of the same UObject. Settings and Inventory remain native; no net C++ reduction is claimed. The [migration record](menu-scripting-migration-plan.md) describes this boundary and the next resource proof.

The later [scripted-font gate](ScriptedFontProof/README.md) passed 15 selected checks in one fresh real-renderer boot, with 27 inspected compatible captures. The same card declares named Regular/Bold/fallback files in AngelScript and selects them in CSS. Failed admission is retryable and failed font-source reload preserves the accepted tree/model; retained text owns its inline faces after session release and GC. Packaged lookup and richer reflected models remain open.

**UI: Scripted Mission Board** is a separate CkUI gym. Its info panel exposes keys 1–6 for reverse, remove, reinsert, reset, reload and invalid-input exercises. The [collection proof](ScriptedCollectionProof/README.md) passed 32 selected checks in the final fresh Editor boot with 33 inspected captures. A reusable reflected flat collection bridge and native pure-order publication fix replace the missing scripting path. Ordinary row buttons still recreate during repeat refresh; nested reflected models, repeated-item focus and packaged lookup remain open. The later Dark Fantasy proof adds retained Image fields to the reflected collection bridge.

## Standing UI authoring rules

The canonical [in-game UI authoring rules](../../../CkFoundation/Source/CkSlateLayout/UI_AUTHORING_RULES.md)
live with CkFoundation so consuming projects can discover and apply them without CkTests.
Read them before every UI task. Update reusable lessons there; this README owns gym
entry points, examples and verification evidence. This heading remains for existing links.

## Authoring sequence

Follow the [framework authoring sequence](../../../CkFoundation/Source/CkSlateLayout/UI_AUTHORING_RULES.md#authoring-sequence).
For a CkTests proof:

1. Keep fixture behavior, gym registration, shortcuts and info-panel text in CkTests. Store the widget/state breakdown, reference and exact verification receipts with the proof; the [inventory contract](InventoryMenuProof/design-contract.json) is one complete example.
2. Run [source preflight](validate_authoring_contract.py) against the authored markup, stylesheet and design contract. It checks XML structure, scoped IDs, template signatures/uses, top-level single-class CSS rules and contract identity; it does not validate widget property values or runtime behavior.
3. Use UnrealToolbox to exercise the real gym entry point and native interactions. Inspect captures at the proof's declared viewport sizes, including state variants. Record exact gates, failures and remaining platform/input/resource limits in the proof README.

The current proofs use in-memory fixtures. They do not yet establish packaged-game execution, physical gamepad behavior, real gameplay persistence, arbitrary large/virtualized inventories, or a complete production visual skin for every native control. Those require focused additions and evidence rather than extrapolating from these screens.

## Mass Effect and Skyrim remake evidence

The revised presentation and current scoped native evidence are collected in the [remake gallery](ReplicaRemakes/README.md). Earlier screenshots and receipts on this page describe their original verification revision.

## Older gym revamp queue

Apply the current framework rules to the existing gyms rather than adding duplicate stations.
The October 9 reference/native audit identified these remaining slices:

- **Battlefield HUD:** named body/display fonts, detailed minimap art, distinct squad role glyphs,
  ownership color, translucent contrast layers, and native text-fit checks for combat and context labels.
  This slice is now verified: [current captures and focused receipt](CombatHudProof/Tactical/README.md).
- **Inventory menu:** contain all intended card rows, leave fractional-scale wrapping slack, improve
  item-art proportions, use structured label/value stats and a framed detail view, and replace copied
  keycaps with `key-hint`. Preserve stable item identity, filtering and rejected-action behavior.
  This slice is now verified: [current captures and focused receipt](InventoryMenuProof/README.md).
- **Settings menu:** named typography and shared hints, then reference-led slider fill and dropdown
  styling with visible keyboard focus. Preserve Apply/Cancel and source-reload behavior.
  Typography, shared hint, anchored card, amber slider fill and native states are now verified in the
  [final focused Menu Gate03 and Slider Gate03 receipts](SettingsMenuProof/README.md).
- **Pause menu:** shared named typography and key hints while preserving the newer scripted pause
  host's ownership and retained Settings-child behavior. This slice is now verified in the
  [final focused Menu Gate03 receipt and captures](PauseMenuProof/README.md).

These are separate visual acceptance slices. Historical behavioral receipts do not establish
that the revamped pixels pass; record a fresh scoped receipt and native captures for each slice.
