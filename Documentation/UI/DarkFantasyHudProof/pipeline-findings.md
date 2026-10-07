# Pipeline findings and limits

## Implementation boundary

The existing Mission Board script provides the retained session, mount, reload and art-shell pattern. The existing power wheel provides the `radial-selector` collection schema and content slot. This proof combines them with the shared reflected HUD controls. It required `FCk_UI_AuthoredSession_Config.Set_RegisterHudControls`, `.Set_RegisterRadialControls`, `.Set_Images`, and `FCk_UI_AuthoredImage.Set_Texture`. Text scalar `Selection` is also the radial selection binding. `ItemActions` route native select/activate StringChanged payloads to the session's `OnItemAction` delegate.

The native HUD registration supports `hud-meter value-bind` and `hud-graphic image-bind`. Health and mana are named Number scalars. The radial schema is exactly actor:Text, glyph:Text, angle:Number, cooldown:Number, enabled:Bool, queued:Bool. All seed angles differ by 45 degrees including the 315-to-0 seam, exceeding the 15-degree minimum. The fixture uses native sector rendering through `-ck-radial-sector-mode: 1`, including numeric glyph and actor label text with authored font sizes; default mode retains the earlier power-wheel rendering.

## Failure and state behavior

Startup requires every shell widget and all three imported textures, usable session configuration, accepted source, mounted surface and filled overlay slots. Required startup failure releases the session/surface and does not publish the widget. `Request_Shutdown()` is safe to repeat, releases the retained session and surface and removes the widget. EndPlay restores the cursor state captured at gym start.

Damage and healing reject negative or nonfinite amounts; mana change rejects nonfinite deltas. Vitals clamp to [0,1]. Selection requires an open wheel and a known interaction key. Activation additionally re-reads current records and rejects a missing, disabled or cooling-down record. Accepted activation appends the selected action to a live chat status scalar and increments its counter. The fixture does not execute game abilities or modify world actors.

`Request_InvalidSource()` attempts an undeclared numeric binding and reports success only when admission rejects and returns a diagnostic. The session owns atomic source admission; the fixture keeps the published source and state on rejection. Successful reload clears the source error. Reload retains scalar and collection state rather than seeding it again.

## Known limitations

- No rasterized HUD screenshot, text, wheel or interaction controls are used. The artwork is scenery/map/frame only.
- The minimap is an illustrative map graphic without navigation or tracked world entities.
- Party list, quest progress, hotbar skill glyphs, chat history and clock are illustrative editable source content. ASCII glyphs are used to avoid missing icon-font dependencies.
- Bundled, SIL OFL-licensed Marcellus SC and Source Sans 3 improve the fantasy hierarchy and compact-copy clarity without copying a proprietary game font. Each authored text/button class selects its family explicitly; the radial control still hard-codes CoreStyle fonts for its sector labels and glyphs. It needs a native font-family styling contract to make that region typographically consistent.
- The meters use the native radial-meter renderer, which shows an arc rather than a simulated liquid fill inside a glass orb.
- Native wheel keyboard selection and activation, runtime startup, source rejection, retained image ownership, teardown and scaled layout passed the focused PIE checks. Registered fonts improved the 720p/1080p/1350p captures, but the fixed 1920x1080 canvas still scales fine hotbar text to two-thirds at 720p. A production HUD needs resolution-aware type sizing and safe-zone rules.
- The 2560x1440 screenshot assertion failed on this 2560x1440 desktop because window chrome limited the PIE client screenshot to 2560x1422. The focused capture gate now requests 2400x1350, a 16:9 size within the available client area, and logs actual dimensions when capture fails.
- The static contract validator originally accepted selector forms such as `#id` that the runtime's restricted CSS grammar rejects. Its top-level selector preflight now rejects `#id`, compound, and grouped selectors, with focused positive and negative checks. It still leaves CSS declarations, values, bindings and runtime admission to the framework parser.
- The authored overlay needed an explicit separate wheel layer to center the wheel independently of the left party and right chat columns. The current row/flex grammar lacks a more direct anchoring constraint.
- The radial collection schema already held numeric glyph and actor label fields, but the earlier native renderer drew its own abstract glyph. Sector mode needed a generic native renderer extension to show the authored record labels. That is a real pipeline capability gap, not something HTML/CSS styling alone could solve.
- The reference's dynamic world map, active skills, party state, chat feed, and liquid-filled glass orbs are beyond this fixture. They require gameplay adapters, updated art/control rendering and animation work; this proof does not establish those behaviors.
- DX12 rendered testing hit an engine shader-frequency assertion during editor startup, before UI tests executed. DX11 real-renderer tests passed; the project's DX12 default was restored after testing. The cause of the DX12 shader mismatch is unproven.
- Resources imported from disk need the caller's NonUFS staging entries for packaged builds; no cooked-build evidence is established.
- The 35-test `UiAuthoring` compatibility gate is focused coverage, not repository-wide regression coverage.
