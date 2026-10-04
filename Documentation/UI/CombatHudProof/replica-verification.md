# Combat HUD replica verification

The current focused gate passed for the Mass Effect 3, Battlefield 4 and Skyrim HUD gyms, alongside Pause, Settings and Inventory, in D:\Repos\CkPlugins_2. This verifies authored layouts and deterministic CkTests presentation fixtures; it does not establish pixel-perfect likeness, packaged execution or real gameplay integration.

Build: [UiTypography-Build-02.log](../../../../../Saved/Logs/UiTypography-Build-02.log), four incremental actions, successful. Gate: [UiTypography-Gate-03.log](../../../../../Saved/Logs/UiTypography-Gate-03.log), **41 passed / 0 failed / 0 skipped / 0 contaminated**, fresh serial D3D12/SM6 editor using the selected project binaries and the existing 41-test catalogue. The fresh log contains no ensures, assertions, script errors or automation failures. Existing scheduler/write-ordering, console-lookup and missing Editor-platform-icon warnings remain.

The earlier CombatHudReplica-Gate-02 passed 41 tests but visual acceptance missed the oversized Mass Effect ammo digits. UiTypography-Gate-01 reproduced their full native line needing 80 × 83 units in a 76 × 58 slot. The authored correction uses native Slate font size 40 in a 68-unit line, with a taller ammo stack and a smaller reserve label. Final Gate03 also verifies the wider Battlefield ticket row. Gate02 passed 40/41: the new fit checks caught faction labels measuring slightly over 30.5 units at 720p in 30-unit boxes. Those boxes are now 34 units wide, leaving room for font hinting at the tested scales. The PIE test now asserts valid native text geometry and unconstrained font measurement fitting the ammo and six ticket-row labels at each capture. These checks reject missing or invalid geometry separately from overflow. The oversized source is applied transiently through the production reload path, then the installed corrected source is restored before captures. No runtime source edits occurred during the final gate: all 19 runtime hashes match the frozen snapshot in each manifest.

The [all-six typography receipt](../typography-verification.md) and [capture manifest](../typography-captures.json) record fresh menu and HUD pixels. Other text was inspected visually; Inventory's scrolling rows and metadata ellipsis are intentional.

The PIE test travels through the actual selector entries, keeps the player viewport focused and world unpaused, routes keys 1–9, rejects malformed and incomplete source atomically, retains native art on compatible reload, and checks owner release across travel/teardown. Context screenshots wait for visible native ancestors after the routed keypress. Cleanup asserts restoration of original CVar values and flags, registry suppression and startup gym settings; viewport resizing is cleaned up through the captured weak window. The native art test covers registration rejection, finite bindings, retained identity and frozen owner behavior.

Native captures comprise eight states per gym: initial at 1280×720, 1920×1080 and 2560×1080, plus reload, ability/action, low-health and two contexts at 1920×1080. They were inspected after the process exited; byte-identical copies, dimensions, timestamps and hashes are recorded in the theme manifests. Static contracts match **65 / 61 / 38** source nodes; their validator only checks XML/ID/class/contract consistency.

[Mass Effect 3 gallery](Futuristic/replica-native-gallery.md) · [Battlefield 4 gallery](Tactical/replica-native-gallery.md) · [Skyrim gallery](Fantasy/replica-native-gallery.md)

## Focused test results

| Test | Result |
|---|---|
| Ck.UiAuthoring.ActionCard.NativeRetentionAndRelease | Success |
| Ck.UiAuthoring.ActionCard.RepeatItemScope | Success |
| Ck.UiAuthoring.Checkbox.RetainedModelBoundKeyboard | Success |
| Ck.UiAuthoring.CombatHudProof.ReplicaArt.NativeRetentionAndFreeze | Success |
| Ck.UiAuthoring.CombatHudProof.Model | Success |
| Ck.UiAuthoring.CombatHudProof.PIE | Success |
| Ck.UiAuthoring.CustomSlots.NativeRuntimeAndAtomicity | Success |
| Ck.UiAuthoring.CustomSlots.SchemaAndTemplates | Success |
| Ck.UiAuthoring.FormStyles.RetainedNativeControls | Success |
| Ck.UiAuthoring.HudControls.NativeRetentionAndAnimation | Success |
| Ck.UiAuthoring.InventoryMenuProof.PIE | Success |
| Ck.UiAuthoring.Parser.RejectsInvalidAndPreservesOutput | Success |
| Ck.UiAuthoring.Parser.ValidDocumentStylesTokens | Success |
| Ck.UiAuthoring.PauseMenuProof.GymSelector.PIE | Success |
| Ck.UiAuthoring.PauseMenuProof.PIE | Success |
| Ck.UiAuthoring.Repeat.CollectionReplacement | Success |
| Ck.UiAuthoring.Repeat.ConstrainedGeometry | Success |
| Ck.UiAuthoring.Repeat.CustomActionAndFloatSeries | Success |
| Ck.UiAuthoring.Repeat.EmptyPreflight | Success |
| Ck.UiAuthoring.Repeat.FailedChildAtomicity | Success |
| Ck.UiAuthoring.Repeat.IncrementalFailureAtomicity | Success |
| Ck.UiAuthoring.Repeat.ItemTypedEvents | Success |
| Ck.UiAuthoring.Repeat.KeyedRetentionAndActions | Success |
| Ck.UiAuthoring.Repeat.NestedChildBinding | Success |
| Ck.UiAuthoring.Repeat.RemovedItemClearsFocus | Success |
| Ck.UiAuthoring.Select.CallbackReentryReloadReconciles | Success |
| Ck.UiAuthoring.Select.CallbackReentryTeardownRejectsHeldInput | Success |
| Ck.UiAuthoring.Select.PopupClosesOnRemoval | Success |
| Ck.UiAuthoring.Select.PopupSurvivesCompatibleReload | Success |
| Ck.UiAuthoring.Select.RetainedCollectionKeyboard | Success |
| Ck.UiAuthoring.SettingsMenuProof.PIE | Success |
| Ck.UiAuthoring.SlateLayout.OwnerRelease | Success |
| Ck.UiAuthoring.Slider.CustomStylesAndLifecycle | Success |
| Ck.UiAuthoring.Slider.MultiUserOwnership | Success |
| Ck.UiAuthoring.Slider.Orientation | Success |
| Ck.UiAuthoring.Slider.PointerAndReload | Success |
| Ck.UiAuthoring.Slider.RoutedFocusNavigation | Success |
| Ck.UiAuthoring.Slider.TouchKeyboardController | Success |
| Ck.UiAuthoring.UMG.SurfaceLifetime | Success |
| Ck.UiAuthoring.View.DataBindingsAndSearchRetention | Success |
| CkPlugins.UiAuthoring.Startup.ProjectBinaries | Success |
