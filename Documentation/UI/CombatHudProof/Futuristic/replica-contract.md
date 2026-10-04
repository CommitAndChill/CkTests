# Mass Effect 3 combat HUD replica

Reference: original 2012 PC single-player combat HUD. The canonical [original PC gameplay frame](https://www.wsgf.org/f/u/contrib/dr/19999/ingame_16x9.jpg) establishes the eight upper-left quickslots, lower-left ammo and curved bottom-center shield with five red health segments. The [EA PC manual](https://eaassets-a.akamaihd.net/eahelp/manuals/mass-effect-3-manuals_PC.pdf), printed pages 7–8, documents squad portraits and power cooldown semicircles. The [normal combat gameplay frame](https://i0.wp.com/gamingrespawn.com/wp-content/uploads/2021/07/ME3_LE_1.jpg?ssl=1) supplies a supporting view of the two squad portraits; that frame is Legendary Edition, whose HUD scale differs. The replica uses original PC arc proportions and a populated two-person squad fixture.

The runtime source of truth is `CombatHudProof.ui.html` plus `CombatHudProof.ui.css`, mounted by the code-created UMG host through CkSlateLayout and CkYoga. This is a distinct tree, not the prior three-panel dashboard. No Widget Blueprint is required.

| Part | Logical 1920×1080 position | Native rendering / binding |
|---|---|---|
| Eight quickslots | x104, y4; 54×64 each | Rounded translucent blue authored containers, numbered text, `me-power` line glyph variants |
| Ammo and grenade | Lower left x104, near y920–1060 | Reserve383 above loaded50, with loaded text at native Slate font size 40 in a 68-logical-unit line and reserve text at font size 18 in a 28-logical-unit line to prevent clipping, grenade2, `me-rifle`, clip fraction and contextual reload text |
| Shield | Bottom center x710, y918; 500×48 | `me-shield` shallow blue curved ribbon, `ShieldFraction` |
| Health | Beneath shield, 460×30 | `me-health` five curved red segments, `HealthFraction` |
| Squad | Bottom center below the curves | Garrus/Liara recreated blue portrait thumbnails and last power glyphs |
| Reticle / cooldown | Viewport center (960,540) | Blue side brackets and two red semicircles that approach the reticle as cooldown completes |
| Objective prompt | Conditional above bottom-center HUD | `ShowObjective`; key9 context fixture |
| Empty weapon prompt | Conditional beneath reticle | `ShowNoAmmo`; no permanent status card |
| Damage | Screen edges behind layout | Bound native damage artwork |

Complete node hierarchy, parent IDs, literal content, classes, computed class style, and bindings are in [design-contract.json](design-contract.json). Runtime components are `SCkCombatHudReplicaArt`, the existing `SCkUiHudMeter` clip gauge, authored text/images/containers, and the passive UMG wrapper. Model bindings are weak. Each portrait brush roots its texture for surviving image widgets. Compatible source reload retains native artwork identity; rejected source leaves the accepted revision and model untouched. Owner release freezes artwork without further model reads.

The only current bitmap is `Resources/UiAuthoring/CombatHudProof/Futuristic/squad-portraits.png`, generated for this prototype. Its dimensions are 1774×887; the left/right normalized UV halves are square portraits. The original image remains in the generation output directory. The prompt is preserved in `squad-portraits-prompt.txt` beside this document. Old `hud-atlas.png`, generated concept references and prior native captures are historical prototype material.

Keys 1–8 exercise damage, sprint, fire, reload, power, recovery, reset and low health. Key 9 cycles objective context. The simulation is a deterministic presentation fixture, not a port of Mass Effect gameplay systems. Health/shield values, reserve transfer and cooldown drive the actual native widgets in an unpaused gym.

Fidelity limits: fonts are available Unreal substitutes, fine glyphs and curves are recreated vectors, squad portraits are generated recreations, and the underlying world remains the CkTests gym. The fixed reference canvas scales uniformly inside the safe zone; ultrawide layouts retain the centered 16:9 HUD area. There is no claim of pixel-identical game rendering. Scoped native verification is recorded in the [gate receipt](../replica-verification.md), [native gallery](replica-native-gallery.md) and [capture manifest](replica-native-captures.json).

Capture-only debug/logging settings were transient and restored. The evidence is scoped to the replica fixture and compatible UI tests; physical gamepad, real gameplay integration and packaged execution remain unverified.
