# Vanilla Skyrim PC HUD replica contract

The [authored Fantasy source](../../../../Resources/UiAuthoring/CombatHudProof/Fantasy/CombatHudProof.ui.html) and [CSS](../../../../Resources/UiAuthoring/CombatHudProof/Fantasy/CombatHudProof.ui.css) replace the prior shared Fantasy dashboard with a sparse 1920 × 1080 vanilla *Skyrim* gameplay HUD. This is a deterministic CkTests fixture, not a connection to Skyrim or the host game's combat state. The root `hud-root` is an unsized overlay; its first full-screen damage art establishes bounds, and the later fixed logical `hud-layout` carries gameplay indicators. The passive UMG SafeZone/ScaleToFit wrapper owns viewport adaptation and input remains with the gym/player.

The primary reference is Bethesda's [Legendary Edition PC manual](https://assets.ctfassets.net/rporu91m20dc/2VLVwistnGywCKuO8CUwCE/12180919f2102d4253bb034b0bb4b632/manual_skyrim-le_pc_en-int.pdf), especially its game-screen and sneaking pages. A [contemporary in-game combat capture](https://cdowinterfacedesign.blogspot.com/2012/04/elder-scrolls-v-skyrim-interface.html) confirms the sparse compass/target/three-bar arrangement. These sources establish topology and state semantics; the coordinates below are authored approximations, not recovered Bethesda pixel measurements. Typeface is the supported native `sans-serif` approximation, not an asserted copy of Skyrim's font. Native `replica-art` vectors supply the thin tapered meter engravings, restrained grey compass ornament, stealth eye, crosshair and damage effect. No generated ornate atlas is used for this layout.

At the logical reference size, the 600 × 28 compass sits at x≈660, y≈28, with separate W/N/E letter nodes over it. A 400 × 16 native red enemy meter starts around x≈760, y≈76; `target-name` follows around y≈100. Magicka, health and stamina are independent 450 × 20 bars at x≈100, 735 and 1370, y≈1020. The 29-unit top spacer centers the reticle/stealth eye art core at y≈540; the 125-unit interaction offset puts its prompt around y≈575, while a bow-only arrow readout sits near the lower right above the stamina rail. Meter colours are restrained blue/red/green rather than panel-wide fantasy accents. Target name, sneak label and right-side arrow copy use content-measured text widths with authored alignment, so the center and right anchors remain legible. The CSS uses row/column growth and overlay alignment; it contains no absolute positioning or transforms.

## Authored node and binding breakdown

All 38 IDs are global within the single `main` region. Indentation in the first column reflects the authored hierarchy. `replica-art` is a registered passive native vector leaf; ordinary `text` remains a native framework widget. Every literal label is element body text, while bound text and visibility read typed live model attributes.

| Authored ID | Parent | Tag | Properties / literal | Role |
| --- | --- | --- | --- | --- |
| &nbsp;&nbsp;&nbsp;`hud-root` | `—` | `overlay` | — | Unconstrained region root; source-order layering |
| &nbsp;&nbsp;&nbsp;&nbsp;`damage-overlay` | `hud-root` | `replica-art` | piece=`damage`, value-bind=`DamageOpacity` | Full-screen native damage vignette |
| &nbsp;&nbsp;&nbsp;&nbsp;`hud-layout` | `hud-root` | `column` | — | 1920 × 1080 logical placement frame |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`top-hud` | `hud-layout` | `column` | — | Compass and targeted enemy rail |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`top-offset` | `top-hud` | `column` | — | 28-unit top inset |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`compass-layer` | `top-hud` | `overlay` | — | Top compass artwork with letters above it |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`compass-strip` | `compass-layer` | `replica-art` | piece=`sk-compass`, value-bind=`MapPhase` | Native engraved compass, scrolling phase |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`compass-west` | `compass-layer` | `text` | body=`W` | West cardinal letter |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`compass-north` | `compass-layer` | `text` | body=`N` | North cardinal letter |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`compass-east` | `compass-layer` | `text` | body=`E` | East cardinal letter |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`target-gap` | `top-hud` | `column` | — | Gap below compass |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`target-group` | `top-hud` | `column` | visible=`ShowTarget` | Contextual enemy name and health |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`target-health` | `target-group` | `replica-art` | piece=`sk-meter`, variant=`1`, value-bind=`TargetHealthFraction` | Native tapered red target-health engraving |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`target-name` | `target-group` | `text` | bind=`TargetName` | Target name; fixture Orc Hunter |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`world-space` | `hud-layout` | `column` | — | Grow spacer anchoring bottom bars |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`bottom-bars` | `hud-layout` | `row` | — | Three independently placed resource bars |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`magicka-meter` | `bottom-bars` | `replica-art` | piece=`sk-meter`, variant=`0`, value-bind=`MagickaFraction`, opacity-bind=`MagickaOpacity` | Blue left magicka engraving |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`bar-left-space` | `bottom-bars` | `column` | — | Flexible gap |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`health-meter` | `bottom-bars` | `replica-art` | piece=`sk-meter`, variant=`1`, value-bind=`HealthFraction`, opacity-bind=`HealthOpacity` | Red centered health engraving |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`bar-right-space` | `bottom-bars` | `column` | — | Flexible gap |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`stamina-meter` | `bottom-bars` | `replica-art` | piece=`sk-meter`, variant=`2`, value-bind=`StaminaFraction`, opacity-bind=`StaminaOpacity` | Green right stamina engraving |
| &nbsp;&nbsp;&nbsp;&nbsp;`reticle-cluster` | `hud-root` | `column` | — | Center reticle and stealth label |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`reticle-top-space` | `reticle-cluster` | `column` | — | 29-unit spacer centering the reticle art core |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`reticle-core` | `reticle-cluster` | `overlay` | — | Center artwork stack |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`reticle-bounds` | `reticle-core` | `column` | — | Overlay measurement anchor |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`aim-reticle` | `reticle-core` | `replica-art` | piece=`crosshair` | Small center crosshair |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`sneak-eye` | `reticle-core` | `replica-art` | piece=`sk-eye`, value-bind=`SneakFraction`, visible=`ShowSneak` | Opening/closing stealth eye |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`sneak-label` | `reticle-cluster` | `text` | bind=`SneakText`, visible=`ShowSneak` | HIDDEN/DETECTED cue while crouched |
| &nbsp;&nbsp;&nbsp;&nbsp;`interaction-cluster` | `hud-root` | `column` | — | Context prompt just below screen center |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`interaction-offset` | `interaction-cluster` | `column` | — | Vertical offset below reticle |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`interaction-row` | `interaction-cluster` | `row` | visible=`ShowInteraction` | Context-sensitive E action |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`interaction-key` | `interaction-row` | `text` | body=`[E] Talk` | Literal [E] Talk prompt |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`interaction-target` | `interaction-row` | `text` | body=`Whiterun Guard` | Literal Whiterun Guard target |
| &nbsp;&nbsp;&nbsp;&nbsp;`arrows-layer` | `hud-root` | `column` | — | Bottom-right bow-only readout layer |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`arrows-space` | `arrows-layer` | `column` | — | Grow spacer anchoring arrow text |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`arrow-readout` | `arrows-layer` | `column` | visible=`ShowArrows` | Conditional bow ammunition text |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`ammo-value` | `arrow-readout` | `text` | bind=`AmmoText` | Current arrow count |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;`ammo-label` | `arrow-readout` | `text` | body=`Steel Arrow` | Literal Steel Arrow type |

## Context and visibility contract

- The compass persists during ordinary exploration. Its markers and heading remain part of native artwork/phase presentation, rather than a large objective card. `target-group` appears only for `ShowTarget`; the fixture initially names **Orc Hunter** and shows a live red target bar.
- Magicka left, health center and stamina right each use their own fraction and opacity binding. A full, inactive bar fades through `MagickaOpacity`, `HealthOpacity` or `StaminaOpacity`; depletion/activity returns it. These remain three separate meter leaves, without numeric stat readouts.
- The small center crosshair is always authored. While `ShowSneak`, the eye overlays it and opens/closes with `SneakFraction`; `SneakText` presents **HIDDEN** or **DETECTED** where appropriate. The official manual describes fully closed as hidden, half-open as at risk and fully open as detectable; exact temporal transitions are model/native behavior, not CSS.
- `interaction-row` is conditional on `ShowInteraction` and renders **[E] Talk** / **Whiterun Guard** from literal fixture labels. `arrow-readout` is conditional on `ShowArrows` and contains the bound count plus literal **Steel Arrow**. Vanilla's arrow readout is bow-contextual, never a permanent ammo/reserve panel.
- `damage-overlay` reads `DamageOpacity` and paints first, behind the other HUD leaves. It has no gameplay side effects.

The former weapon panel, ammo/reserve/reload meters, radial cooldown, ability carousel, status plate and branded headings are absent. Authoring permits a `font-size:18; font-weight:bold;` reload probe on `.target-name`; changing it is a visual source reload only. Native runtime parsing and visual review passed the [focused gate](../replica-verification.md): 41/41 selected tests on D3D12/SM6. See the [native gallery](replica-native-gallery.md) and [capture manifest](replica-native-captures.json). Static preflight checks XML shape, unique IDs and declared CSS classes, not binding availability or full runtime style grammar.

Capture-only debug/logging settings were transient and restored. The evidence is scoped to the replica fixture and compatible UI tests; physical gamepad, real gameplay integration and packaged execution remain unverified.
