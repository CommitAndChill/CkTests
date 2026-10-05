# Scripted Pause with a retained Settings child

The fresh focused Editor gate passed **15/15 selected tests**, with **27 fresh visually inspected captures**. The actual script class and real gym shortcut were exercised; this is a focused authoring verdict.

The existing Pause screen now has an [AngelScript widget](../../../Script/CkUiAuthoring/CkScriptedPause_Widget.as) which creates its UMG tree, declares its model/actions, loads the existing HTML/CSS and owns its pause policy. The [gym controller](../../../Script/CkUiAuthoring/CkUiAuthoringGym_PlayerController.as) creates that widget through the real UI Authoring gym's `[1]` shortcut. No Widget Blueprint or new C++ Pause screen host is needed for this path.

Open **CkUI → UI Authoring** in the gym selector. `[1]` opens script Pause; its Settings button pushes a native Settings child. Cancel or Escape returns to the same Pause widget, session and view. Tab/Shift+Tab and arrows navigate the authored buttons; Enter/Space activates them; Escape or Resume closes Pause. `[2]` and `[3]` retain standalone Settings and Inventory. Return to title remains an action-count fixture, without real game travel.

## Widget and ownership breakdown

| Part | Code/source owner | Responsibility |
| --- | --- | --- |
| Overlay root | AngelScript | Compose the backdrop, dimmer and safe content |
| Backdrop ScaleBox + Image | AngelScript and existing loose PNG | Import and retain a UTexture2D, fill/crop with ScaleToFill |
| Dimmer Border | AngelScript | Overlay the same translucent dark tint |
| SafeZone → ScaleBox → SizeBox | AngelScript | Safe content and ScaleToFit within a 1920×1080 reference frame |
| Authored surface | Shared CkWidgets | Mount the accepted `main` region inside the exact script widget tree |
| Panel, pause mark, divider, spacing and key hint | Existing [HTML](../../../Resources/UiAuthoring/PauseMenuProof/PauseMenuProof.ui.html) and [CSS](../../../Resources/UiAuthoring/PauseMenuProof/PauseMenuProof.ui.css) | Native Slate hierarchy and CkYoga layout |
| Seven text values | Script session declarations | Title, subtitle, three button labels and two input-hint labels |
| Three enabled bools and actions | Script session declarations | Resume, Settings and Return; script accepts dispatch only in current active input ownership |
| Required ordinary buttons | Shared source contract | Freeze exact IDs, actions, enabled bindings and region; reject invalid documents before native staging |
| Input, focus and held releases | Shared authored session | Exact admitted player/user, current-ID focus/navigation, disable-focused-button repair and held gesture draining |
| Layer membership | Independent shared CkUI observer | Covered parent retains ownership; terminal removal revokes the exact admission |
| Pause and source polling | AngelScript | Acquire only after accepted layer admission, preserve external pause, reload changed loose files and report rejection |
| Native Settings child | Existing native host's prepared-child seam | Validate the exact parent, prepare before suspension, commit synchronously, retain the parent and close back to it |

Preparation of a Settings child must commit or close before the next CoreTicker pump. A weak one-shot expiry releases an abandoned prepared Slate root; otherwise that root could keep the child UObject alive indefinitely. Commit freezes the exact child admission, makes one focus attempt and rolls back on changed/redirected ownership. Child activation/polling never retries that focus attempt. Standalone native behavior remains compatible.

Covered deactivation ends input without releasing the accepted parent view or pause. Terminal cleanup marks the script terminal, then uses `Request_EndInput(false)` to drain input and discard saved focus without callbacks. CommonUI owns terminal focus handoff; its list removal can be asynchronous. Cleanup clears pause ownership before callbacks, closes the child and releases session/surface ownership. The captured watcher stays alive through callbacks and gates parent removal before Stop. This avoids deleting a newer admission when a child-close callback removes and re-adds the same parent UObject. A rejected entry captures removal authority before teardown callbacks; a failed observation grants no removal authority.

## Pipeline findings

The migration exposed four reusable gaps: reflected required-control contracts, a pure query for current authored-button focus, suspension without immediate saved-focus restoration, and object-handle semantics for the config's interaction owner. The existing CK macro emitted that pointer property as an object reference, which rejected the actual script subclass. A narrow by-value owner accessor preserves null/derived handles while initialization remains the validity authority. The shared session supports these operations without a CkWidgets dependency on CkUI or another parser. Script remains responsible for screen lifecycle and model policy.

This removes the gym Pause path's dependence on native Pause policy; it does **not** delete the old native host. Settings and Inventory still use that host, and legacy native screen swaps remain supported. No existing native Pause functions were removed in this slice, and native child integration adds C++ code. This is a capability and responsibility migration, not a demonstrated net C++ reduction.

The native host diff against CkTests commit `88e894a0b040e9f69411d47e9418240b3dd38f04` is 309 added/22 removed implementation lines and 41 added header lines: net **+328 C++ lines**. This counts accumulated uncommitted host work against that checkout baseline, rather than isolating this migration from earlier proof changes. It establishes no net deletion claim.

Required-button admission covers interaction identity, not a complete visual schema: required title/hint text nodes and button label bindings are not yet reflected as mandatory declarations. Collections, trees, font resources and compound model transactions remain native integration work. Those are the next useful tests of full script authoring.

## Verification and limits

The gate retains the existing Pause capture names and tests the actual script class through discovered reflected properties. It also exercises the real gym `[1]` shortcut, stacked native Settings and standalone Settings/Inventory compatibility. The existing card, journal, wheel, font, session, owner-release and second-player lifetime scenarios run in the same focused invocation.

The [verification receipt](verification-receipt.json) records the exact test population, 68 frozen source/resource hashes, 27 capture hashes and six unchanged historical receipts. The [incremental build](../../../../../Saved/Logs/ScriptedPause-Build-03.log) passed 4 actions in 12.36s; the [final real-renderer gate](../../../../../Saved/Logs/ScriptedPause-Gate-04.log) reused cached discovery and completed in one fresh Editor boot. Its log contains no Unreal errors, script compile/stale-code errors, ensures or fatals. Incidental Chromium diagnostics are counted separately in the receipt. The earlier [input](../OwnedInputProof/README.md) and [membership](../LayerMembershipProof/README.md) receipts remain historical records.

The initial one-boot plan required 8 boots across four logged attempts: combined discovery exposed a rejected owner binding; quiet engine material initialization exceeded the watchdog until the historical verbose shader logging was restored; then the first completed gate passed 14/15 with one popup Escape fixture error. Slate clears focus while dismissing its popup, so the corrected fixture sends both events and separately asserts handled press, popup closure and exact retained child/parent state. The final gate passed after that focused fixture rebuild. Cancelled startup attempts provide no runtime verdict.

Visual QA covered all four contact sheets, with original-resolution checks of long Pause copy, card state and small Settings/Inventory text. Inventory clipping at the bottom of the initial list is its scrolling viewport; the scrolled capture exposes those rows. The development HUD statistics beneath native menus are incidental.

This fixture uses instant stack transitions and Development Editor loose-file resources. Nonzero transition animations, complete terminal focus reentry, packaged lookup, physical gamepads, complete foreign-user action rejection and real game persistence/travel require separate evidence. Child focus-redirection rollback is code-inspected; this gate does not force that callback path. Popup Escape checks dismissal with the child retained, then explicitly focuses Cancel; it does not prove automatic focus recovery after dismissal. The second-player checks overlay one scene view; they do not prove two-view split-screen rendering.
