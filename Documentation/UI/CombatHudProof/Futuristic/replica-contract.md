# Mass Effect 3 combat HUD replica

Reference: original 2012 PC single-player combat HUD. The canonical [original PC gameplay frame](https://www.wsgf.org/f/u/contrib/dr/19999/ingame_16x9.jpg) establishes the eight upper-left quickslots, lower-left ammo and curved bottom-center shield with five red health segments. The [EA PC manual](https://eaassets-a.akamaihd.net/eahelp/manuals/mass-effect-3-manuals_PC.pdf), printed pages 7–8, documents squad portraits and power cooldown semicircles. The [normal combat gameplay frame](https://i0.wp.com/gamingrespawn.com/wp-content/uploads/2021/07/ME3_LE_1.jpg?ssl=1) supplies a supporting view of the two squad portraits; that frame is Legendary Edition, whose HUD scale differs. The replica uses original PC arc proportions and a populated two-person squad fixture.

The runtime source of truth is `CombatHudProof.ui.html` plus `CombatHudProof.ui.css`, mounted by the code-created UMG host through CkSlateLayout and CkYoga. This is a distinct tree, not the prior three-panel dashboard. No Widget Blueprint is required.

| Part | Logical 1920×1080 position | Native rendering / binding |
|---|---|---|
| Eight quickslots | x104, y4; 54×64 each | Translucent blue authored containers, shared numbered key hints, `me-power` glyph variants |
| Ammo and grenade | Lower left x104, near y920–1060 | Reserve383 above loaded50, with loaded `me-display` at font size 36 in a 68-logical-unit line and reserve at font size 20 in a 28-logical-unit line, grenade2, `me-rifle`, clip fraction and contextual reload text |
| Shield | Bottom center x710, y918; 500×48 | `me-shield` shallow blue curved ribbon, `ShieldFraction` |
| Health | Beneath shield, 460×30 | `me-health` five curved red segments, `HealthFraction` |
| Squad | Bottom center below the curves | Garrus/Liara recreated blue portrait thumbnails and last power glyphs |
| Reticle / cooldown | Viewport center (960,540) | Blue side brackets and two red semicircles that approach the reticle as cooldown completes |
| Objective prompt | Conditional above bottom-center HUD | `ShowObjective`; key9 context fixture |
| Empty weapon prompt | Conditional beneath reticle | `ShowNoAmmo`; no permanent status card |
| Backdrop and damage | Full 1920×1080 behind layout | Bound cinematic scene image and native damage artwork |

Complete node hierarchy, parent IDs, literal content, classes, computed class style, and bindings are in [design-contract.json](design-contract.json). Runtime components are `SCkCombatHudReplicaArt`, the existing `SCkUiHudMeter` clip gauge, authored text/images/containers, and the passive UMG wrapper. Model bindings are weak. Each portrait brush roots its texture for surviving image widgets. Compatible source reload retains native artwork identity; rejected source leaves the accepted revision and model untouched. Owner release freezes artwork without further model reads.

The bound cinematic backdrop supplies the world layer; `Resources/UiAuthoring/CombatHudProof/Futuristic/squad-portraits.png` supplies the two portrait UV halves. Registered `me-ui` and `me-display` faces give labels and numeric readouts distinct hierarchy. Old `hud-atlas.png`, generated concept references and prior native captures are historical prototype material.

Keys 1–8 exercise damage, sprint, fire, reload, power, recovery, reset and low health. Key 9 cycles objective context. The simulation is a deterministic presentation fixture, not a port of Mass Effect gameplay systems. Health/shield values, reserve transfer and cooldown drive the actual native widgets in an unpaused gym.

Fidelity limits: fonts and fine glyphs are recreations, curved meters are native vectors, and squad portraits are generated recreations. The fixed reference canvas scales uniformly inside the safe zone; ultrawide layouts retain the centered 16:9 HUD area. There is no claim of pixel-identical game rendering. Earlier native verification in the [gate receipt](../replica-verification.md), [native gallery](replica-native-gallery.md) and [capture manifest](replica-native-captures.json) predates this backdrop and typography revision.

Capture-only debug/logging settings were transient and restored. The evidence is scoped to the replica fixture and compatible UI tests; physical gamepad, real gameplay integration and packaged execution remain unverified.
