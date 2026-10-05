# Mission Board image prompts

Mode: built-in image generation. The reference is a design guide; its baked text and controls are not runtime widgets.

## Reference mock

```text
Use case: ui-mockup
Asset type: visual reference for a fully code-authored in-game UMG mission board, 16:9 landscape.
Primary request: polished futuristic action-RPG operations/mission screen in a Mass Effect-like science-fiction visual genre. Original Terminus operations interface; strong navy, charcoal and cyan with restrained amber selection. Practical shipped-game UI hierarchy, crisp readable type, generous line boxes and margins.
Scene/backdrop: dark subdued orbital starfield and faint planetary horizon, subordinate to the UI.
Composition: centered two-column operations panel. Header TERMINUS / OPERATIONS, large title MISSION BOARD, small 4 ACTIVE MISSIONS. Left column approximately55percent: four substantial stacked selectable rows, first selected with amber outline and slim amber accent. Each row has mission title, route/location and status. Exact titles: Restore the relay; Recover the black box; Answer the distress call; Survey the anomaly. First row route: KEPLER / RELAY STATION. First status: IN PROGRESS. Other rows: AVAILABLE. Right column: SELECTED MISSION eyebrow, RESTORE THE RELAY title, a simple stylized orbital route diagram with three thin circle nodes and connecting lines, descriptive text 'Restore communications before the next patrol window.' and a progress readout 'RELAY ALIGNMENT 35%'. Clear large amber COMPLETE OBJECTIVE button and secondary CLEAR SELECTION button. Bottom instruction bar: SELECT MISSION / REVERSE ORDER / RESET BOARD. Keep long copy safely inside boxes.
Style: sharp flat game interface, subtle translucent panels and fine cyan rules, restrained glow, clean type contrast, no tiny unreadable microcopy. The route diagram is modest and visually separable from live data fields.
Constraints: complete front-on screen, no perspective/device frame, no watermarks, no logos from existing games, no unrelated inventory/health/ammo overlays. Clear independently implementable components. Generated text/geometry is a visual target; runtime widgets will be built separately from source.
```

Saved reference: `mission-board-reference.png`.

## Runtime orbital backdrop

```text
Use case: game UI background asset. Generate a cinematic 16:9 dark orbital background for an original futuristic expedition mission board. Sparse fine stars across a near-black navy space sky; the blue atmospheric curve of a planet rests along the lower right edge with a subtle cool cyan rim; a very small distant relay satellite silhouette can appear near the upper right, no central focal object. Keep the center and left extremely dark and calm so layered cyan and amber native UI text will remain readable. Restrained realistic game key-art rendering, no bright nebula, no bloom spilling into the central area. This is only a clean backdrop texture: absolutely no text, lettering, logos, borders, panels, controls, buttons, icons, interface elements, diagram nodes, grids or watermarks. Wide 16:9 full-bleed composition.
```

Saved asset: `../../../Resources/UiAuthoring/ScriptedCollectionProof/mission-board-backdrop.png`.
The generated art contains no interface text or controls; the UMG shell layers authored widgets over it.
