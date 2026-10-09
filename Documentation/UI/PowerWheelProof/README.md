# Mass Effect 3 squad power wheel

Select **UI: Mass Effect 3 Power Wheel** in the CkTests gym selector. Hold Shift to pause and open the wheel, hover a power or use arrows to select it, then click or press Enter to queue it. Release Shift to deploy. Escape cancels the whole queue. The info panel also offers a latched preview on 1, target protection on 2 and fixture reset on 3; Space deploys a latched preview.

The layout follows the original console radial wheel, adapted to Windows mouse and keyboard. The [2012 Omega screenshot](https://cdn.shazoo.ru/31253_M1WMOW2NSS_pgw_omega_205_1.jpg) supplies the circular geometry, center selection label, side description, blue glyph frames and lower vitals. The [official Xbox 360 manual](https://eaassets-a.akamaihd.net/eahelp/manuals/mass-effect-3-manuals_Microsoft%20XBOX360.pdf#page=11) establishes queueing up to one power per actor and deploying on release. The original PC game uses a different tactical menu.

The [widget breakdown](widget-breakdown.md) and [design contract](design-contract.json) record all 60 authored nodes, their bindings and parent relationships, plus the UMG host hierarchy and native drawing parts. The implementation uses CkSlateLayout HTML/CSS, CkYoga layout, a retained radial selector and a code-created CommonUI/UMG host. A bound cinematic scene and an authored full-frame black tint sit behind the wheel; its darkening overlay, native glyphs, selection, cooldown veil, health arcs and reticle keep the tactical information legible. Garrus and Liara reuse the existing portrait atlas. Authored text uses the registered `me-ui` face, with `me-display` for the selected power name; passive `key-hint` controls show SHIFT, ESC, 2, CLICK and ENTER.

The deterministic fixture supplies eight Shepard powers and four powers per squadmate. Shepard mixes classes to exercise more art and state paths. The fixture returns a validated command batch and starts shared actor cooldowns; it does not dispatch real gameplay abilities. Typefaces and icons are recreated, and the gym environment differs from the original battle scene. Physical gamepad and packaged-game behavior remain outside the current verification scope.

The reusable control owns selection, pointer/keyboard input and retained rendering. The host owns visibility, the local Slate user, pause, cursor, focus and source reloads. Closing revokes the authored owner before detaching the surface; queue cancellation and rejected deployment produce no commands. A wheel opened over an already paused world preserves that pause when it closes.

The previous version passed a focused **10/10** real-renderer gate; its [eight unedited native captures](native-gallery.md) cover 720p, 1080p, ultrawide, queued squad powers, cooldown, shield-blocked selection and closed states. Those captures predate the new backdrop, typography and key hints. See the [verification details](verification.md) and [receipt](verification-receipt.json) for their exact source/capture hashes; current source needs a fresh visual gate.

## Mass Effect and Skyrim remake evidence

The revised presentation and current scoped native evidence are collected in the [remake gallery](../ReplicaRemakes/README.md). Earlier screenshots and receipts on this page describe their original verification revision.
