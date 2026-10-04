# Power Wheel verification

The final incremental Editor build succeeded with 9 actions in 14.23 seconds. One fresh real-renderer Editor boot then passed all 10 selected tests with no failed, skipped or contaminated tests; the Editor reported exit code 0. The renderer was D3D12 / SM6. Logs are `Saved/Logs/PowerWheel-Build-05.log` and `Saved/Logs/PowerWheel-Gate-11.log` at the host project root.

The registered gym's PIE test drives the actual input processor: Shift opens and pauses, native hover and Enter queue one power for each actor, and Shift release returns three validated commands and restores pause, cursor and viewport focus. It also checks cancel, externally owned pause, cooldown advancement/freeze, shield blocking, compatible source retention, invalid source rejection, authored owner release and inert held controls after shutdown.

The radial test exercises native pointer/keyboard selection, blocked activation, retained reload, malformed registration/configuration, released owners, reentrant owner release and inherited opacity. The three typed model tests cover queue replacement, actor ordering, shared cooldowns, invalid configuration and atomic failure without command publication or cooldown mutation. Compatible sibling tests cover HUD animation/retention, replica art, authored owner release and UMG surface lifetime; the startup test confirms project binaries.

All eight [native captures](native-gallery.md) were visually inspected. The center power name and hint align with the reticle; focus appears on the selected glyph. Text and glyphs fit at 1280×720, 1920×1080 and 2560×1080. Native geometry and text measurement assertions cover the power name, description, state and three queue labels in every open capture. Closed deployment and cancellation captures show no remaining wheel or dimmer.

The fresh log contains no Unreal ensure, script compilation, fatal or automation test errors. Nonfailing Iris, navigation, OSS, ECS-ordering and console-registration warnings, plus Chromium subprocess USB/CRL messages, remain. This is focused verification of the selected ten tests, not a full repository regression result.

The fixture uses recreated vector glyphs and a substituted typeface, reuses the existing squad portrait atlas, and runs against the gym environment. It returns command batches rather than activating gameplay abilities. Physical gamepad and packaged-game behavior remain unverified. The [widget breakdown](widget-breakdown.md) records authored logical sizes separately from native evidence.

The [machine-readable receipt](verification-receipt.json) records the exact test paths, final build/gate log hashes, 14 frozen implementation source hashes, and dimensions, capture timestamps and SHA-256 values for the unedited PNG copies.
