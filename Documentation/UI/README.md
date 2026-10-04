# UI authoring gyms

Open **CkTests → CkUI → UI Authoring** in the gym selector. Its info panel offers **[1] Pause**, **[2] Settings**, and **[3] Inventory**. Close an active menu before selecting another shortcut. The project retains the previously approved 30 FPS Editor startup cap.

Combat HUDs have separate selector entries: **UI HUD: Mass Effect 3**, **UI HUD: Battlefield 4**, and **UI HUD: Skyrim**. Each starts a passive code-created UMG HUD with its own art and authored layout. Its info panel exposes keys **1–8** for damage, sprint, fire, reload/replenishment, power/capture/spell, recovery, reset and low health; **9** cycles game-specific context. See the [combat HUD overview](CombatHudProof/README.md) for the current verification status.

The workbench proves code-authored in-game UI through **CkSlateLayout and CkYoga**. The menus use a code-created CommonUI UMG host; combat HUDs use a passive UMG host that leaves the world and player input active. Both mount an authored surface with native Slate controls and layout descendants. HTML/CSS files use the framework's supported grammar. Menu image-generation references guide design and measured atlas sprites supply menu artwork. The combat replicas use actual game references, native vector graphics and recreated squad portraits.

| Proof | What it exercises | Full breakdown |
| --- | --- | --- |
| Pause | Centered panel, native buttons, action dispatch, focus, Escape, pause ownership and scaling | [Pause menu](PauseMenuProof/README.md) |
| Settings | Typed editable fixture state, slider, checkbox, select popup, Apply/Cancel, retained reload and close/reopen behavior | [Settings menu](SettingsMenuProof/README.md) |
| Inventory | Typed keyed collection, generated RGBA icon atlas, wrapping repeated action cards, scroll, filters, debounced search, empty state, equip/use actions and saved session state | [Inventory menu](InventoryMenuProof/README.md) |
| Mass Effect 3 HUD | Upper-left quickslots, lower-left ammo, bottom-center curved shield/five health segments and squad portraits, reticle cooldown semicircles | [Mass Effect 3 HUD](CombatHudProof/Futuristic/README.md) |
| Battlefield 4 HUD | Conquest minimap, tickets and squad at lower left; compact ammo/firemode/health at lower right; conditional capture and kill feed | [Battlefield 4 HUD](CombatHudProof/Tactical/README.md) |
| Skyrim HUD | Thin compass and enemy health/name, independently fading three resource bars, sneak eye, interaction prompt and conditional arrows | [Skyrim HUD](CombatHudProof/Fantasy/README.md) |

[Typography verification](typography-verification.md) records the corrected ammo/ticket layouts and fresh native captures of all six proofs at the three tested resolutions.

## Authoring sequence

1. Generate a visual reference and identify reusable art separately. Save the prompts. Derive an explicit layout specification; treat generated text, counts and geometry as illustrative.
2. Write the widget breakdown: every authored ID, parent, repeat/slot scope, native role, dimensions, bindings, actions and interaction states. Inventory's [design contract](InventoryMenuProof/design-contract.json) is the first complete machine-readable example.
3. Define typed model state and collection fields independently of the artwork. Reject malformed seeds and invalid actions before publishing; update the model and its collection coherently.
4. Author hierarchy and bindings in `.ui.html`, then dimensions, typography and supported native visual states in `.ui.css`. Consult [CkSlateLayout authoring](../../../CkFoundation/Source/CkSlateLayout/AUTHORING.md) for the actual grammar.
5. Compose existing controls first. Add a reusable native registered control when its interaction or measurement needs a new capability; keep game fixture behavior in CkTests. Inventory adds `action-card`, whose required content slot is authored separately.
6. Run [the source preflight](validate_authoring_contract.py) to catch undefined classes, duplicate scoped IDs and contract identity mismatches. It accepts a single `main` region; it does not replace the framework parser or runtime validation.
7. Verify the production path with UnrealToolbox: real gym shortcut, native focus/input, state mutation, invalid requests, reload, owner release and teardown. Respect native control timing: search debounces typed changes, and screen focus may settle on a subsequent frame.
8. Inspect native captures at 1280×720, 1920×1080 and 2560×1080. Use empty, disabled, selected, scrolled and edited states as appropriate. Check text and important values for truncation, then record exact build/gate evidence and remaining limits in the proof README.

The current proofs use in-memory fixtures. They do not yet establish packaged-game execution, physical gamepad behavior, real gameplay persistence, arbitrary large/virtualized inventories, or a complete production visual skin for every native control. Those require focused additions and evidence rather than extrapolating from these screens.
