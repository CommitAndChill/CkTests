# Pipeline findings and limits

The HTML/CSS-to-Slate pipeline drives this UMG composition with a retained authored session. HTML declares hierarchy, templates, bindings, and actions; CSS supplies geometry, color, typography, and native custom styles; AngelScript supplies data, art, timer state, and lifecycle. The shell supplies scenery and scales the authored surface. The [source inventory](widget-breakdown.md) and [contract](design-contract.json) reflect the current markup.

| Need | Shared implementation | Current source behavior |
| --- | --- | --- |
| Circular red and violet resource orbs | hud-orb binds fraction, liquid image, and bezel image; a circular mesh animates liquid UVs under fixed lighting, with a stationary glass highlight | Script health and mana changes drive vertically clipped fills; final rendered captures and phase comparison verify moving liquid |
| Six framed spells with visible cooldown | One spell-slot template expands into frame, inlay, hud-ability, and key-hint; hud-ability draws a clockwise radial mask and remaining-seconds label | Six instances bind art, fraction, seconds, and key; shadow/flurry/strike tick at 10/8/12-second durations, ready slots stay zero |
| Consistent key legends | Reusable retained key-hint supports literal label, admitted font family, CSS styling, and optional action | E closes the wheel; spell, potion, resource, and SPACE hints are passive; native text measurement centers all labels |
| Centered caption text | Shared authored text-align: center maps to native text justification | Hotbar caption declares the property; native text alignment test passed |
| Illustrated wheel options | Collection Image field retains an admitted texture brush; radial selector reads record images and admitted font | Eight keyed images and labels are declared; previous collection-image rejection test passed |
| Fantasy wheel geometry | Radial fantasy mode paints wedge, spokes, ring, labels, and icons beneath the center slot | final focused renderer captures retain the wheel and show a centered E hint |

Each spell frame is now authored as a nested box around its art, so the border, inlay, and content share layout geometry. The ornamental frame remains separate artwork behind the bar; a continuous authored backing covers its central apertures, and the six new slot frames own their content bounds. Dedicated ornamental slot artwork would improve fidelity without changing the layout contract. The previous offset between spell art and apertures was an authoring alignment issue, not a failure of the row layout.

The orb and cooldown geometry is reusable native rendering code rather than baked animation. The orb's UV motion and lighting field are separate, which keeps the highlight fixed while liquid moves. The mask and timer read live scalar bindings. The authored session currently offers individual TrySetNumber calls rather than a batch scalar update; the gym retires the surface if a required cooldown publication fails, so a partially updated display does not continue running.

The minimap, party list, quest, clock, and chat remain illustrative. The reference game's artwork and proprietary font were not copied. The fixed 1920 x 1080 canvas still needs shipping safe-zone, aspect-ratio, and type-size rules, live gameplay adapters, and cooked resource verification. The generated art and frame topology do not establish a pixel match to the supplied reference.

Verification on 2026-10-07: incremental C++ build passed; the 246-test UI-authoring run passed 243, including the new key-hint, text-alignment, and mesh tests. The same prior pause-menu and two capture-bound failures remained. Visual inspection caught that a null texture proxy caused color-only meshes to be skipped; allowing Slate's white fallback fixed both cooldown shade and glass lobes. The final focused DarkFantasy run passed 3/3 with an added real-pixel cooldown regression and captures at 720p, 1080p, and 1350p. Texture-region pixel comparisons confirm animation. This is focused verification, not a repository-wide green verdict.

The template-aware source validator and its 18 Python tests pass. Runtime admission still owns widget property types and values. DX12 previously stopped at an engine startup shader-frequency assertion; the final visual run used DX11 temporarily, and the original config was restored exactly.
