# Focused verification

The final fresh Development Editor gate passed **11/11 selected tests**, with zero failed, skipped or contaminated tests. All 40 frozen source hashes matched before and after the run. Five fresh native captures were copied without modification and visually inspected: initial 1280x720, 1920x1080 and 2560x1440, plus armed/fired 1920x1080. Text and the button label were readable without clipping in these states.

The [receipt](verification-receipt.json) records exact test identities, source/capture hashes, dimensions/timestamps, logs and boot history. The final separate incremental Editor build completed four actions in 10.44 seconds; the test-only run took 6m 2s. Source remained frozen throughout the final build/gate sequence.

The final log contained no Unreal Errors/Fatals, ensures, stale-script or script-compile errors. Chromium separately logged one USB property error and 17 CRL verification errors; those are recorded separately from Unreal/test diagnostics.

| Passing test | Focused production evidence |
| --- | --- |
| `Ck.UiAuthoring.UMG.Session.AdmissionAndScalars` | Atomic declaration rejection/retry, frozen declarations, typed failure preservation and terminal release |
| `Ck.UiAuthoring.UMG.Session.NativeTransactionsAndEvents` | Actual search/slider/checkbox/button proposals, explicit acceptance, rejected/accepted source transactions, release during callbacks and valid Core base font |
| `Ck.UiAuthoring.UMG.Session.OwnerRetirementAndGc` | Surface replacement preserves its newer owner; native references do not root a dead session |
| `Ck.UiAuthoring.ScriptingCard.PIE` | Real selector travel, code-created UMG, native pointer input to script handlers, source rejection/reload, GC and EndPlay retirement |
| `Ck.UiAuthoring.UMG.SurfaceLifetime` | Compatible authored-surface lifetime |
| `Ck.UiAuthoring.SlateLayout.OwnerRelease` | Compatible native owner retirement |
| `Ck.UiAuthoring.FormStyles.RetainedNativeControls` | Compatible retained native control styling |
| `Ck.UiAuthoring.Fonts.NativeRuntime` | Compatible named font runtime behavior |
| `Ck.UiAuthoring.Journal.PIE` | Existing journal production fixture |
| `Ck.UiAuthoring.PowerWheel.PIE` | Existing squad wheel production fixture |
| `CkPlugins.UiAuthoring.Startup.ProjectBinaries` | Expected project binaries loaded |

The PIE test checks all 13 authored `text` elements against their allocated natural width and height at each capture. The button label was visually inspected. Actual native input produced the [armed](armed-1920x1080.png) and [fired](fired-1920x1080.png) states; model getters alone are not the visual evidence.

The source preflight passed 28 authored IDs, 25 referenced/declared CSS classes and 28 matching contract nodes. It checks static identities rather than the full native grammar or renderer.

| Attempt | Editor boots | Result / reason for another run |
| --- | ---: | --- |
| Gate 01 | 2 | Headless discovery completed; renderer discovery hit an idle-output timeout during default-material loading. Zero tests, no runtime verdict. |
| Gate 02 | 3 | 8/11 passed. Initialization had not prepared retained region mounts; configuration now declares and initializes the complete region set. |
| Gate 03 | 4 | 9/11 runtime results succeeded. Direct region-root sizing rejected the card, and a slider assertion incorrectly required exact 30 instead of its normalized 30.0000019. Inline headless discovery also rejected renderer-only roots, so the population verdict was invalid. |
| Gate 04 | 1 | 10/11 passed. Empty base font had positive size without a font source; [missing glyphs](missing-default-font-1920x1080.png) failed text bounds. The session now supplies Core Regular explicitly. |
| Gate 05 | 1 | 10/11 passed. Readable glyphs exposed [undersized title/count line boxes](undersized-line-boxes-1920x1080.png); CSS font-size values become Slate points, including with `px`. Row/enclosing heights were increased without loosening the fit threshold. |
| Gate 06 | 1 | 11/11 passed after the separate incremental build. All five fresh captures inspected. |

The original three-boot plan grew to 12 boots. Each additional run addressed observed runtime or launch evidence. Later launches used engine-supported, process-scoped `UE-CmdLineArgs` for full stdout/shader progress; the caller's environment was restored immediately after launch. No caches/build artifacts were deleted, and no user Editor was terminated. A separate build followed by test-only execution avoided the combined run's inline discovery population issue.

This establishes the focused Development Editor scalar fixture. Packaging, installed-plugin resource lookup, reflected collections/font resources/custom factories, compound model transactions, menu/pause ownership, physical gamepads, localization, accessibility scaling and real weapon integration remain unverified. The historical journal/font 26-test receipt remains separate; this gate reruns only the named compatible siblings above.
