# Character/equipment deficiency audit — 2026-10-08

Audit baseline: `runtime-1920x1080.png` versus `character-equipment-reference.png`, plus the authored markup, CSS, AngelScript model and native item-slot implementation. This is the complete observed gap inventory for this pass, not a claim that unknown usability or platform defects have been excluded. The generated reference is a visual target; gameplay rules must remain coherent rather than copy inconsistent generated text.

## Behavior and control truth

| ID | Deficiency | Required correction |
| --- | --- | --- |
| B01 | Footer always says EQUIP; E actually unequips equipped selections and does nothing useful for supplies. | One context-sensitive primary action and matching clickable key hint; explicit unavailable state. |
| B02 | Persistent EQUIP/UNEQUIP/TOGGLE COMPARISON button slab does not match the reference footer and crowds comparison content. | Move contextual actions into a compact footer and preserve correct enabled state. |
| B03 | Filters have no selected binding; blue focus can be mistaken for active filter. | Separate selected state from keyboard focus using reusable action-card. |
| B04 | Comparison toggle hides only one text block; tooltip comparison ignores it, and heading stays behind. | Gate complete comparison presentation consistently and show its state. |
| B05 | Comparison is a copied multiline string; improvement derives from rarity rather than numeric values. | Typed stats, real signed deltas and per-row positive/negative/neutral presentation. |
| B06 | Character totals are constant despite equipment changes. | Derive totals from base plus equipped modifiers. |
| B07 | Inventory count is filtered-result count, not occupied capacity. | Owned slots / capacity plus separate filter result context. |
| B08 | Status says select an item while one is already selected. | Contextual selection/action feedback. |
| B09 | Ring default chooses an empty slot and may omit comparison with the existing ring. | Explicit destination/comparison policy and a way to choose the other ring. |
| B10 | Keyboard navigation skips empty equipment slots; cannot target Ring2 directly. | Provide a clear keyboard destination control or slot-target navigation. |
| B11 | Potions have quantities but cannot be used. | Implement fixture consumption or clearly identify inspect-only behavior; do not advertise EQUIP. |
| B12 | ESC close works, but its small top-right target and missing CLOSE caption do not match footer reference. | Shared actionable ESC/CLOSE footer treatment with reasonable hit target. |
| B13 | Filtering can preserve equipped selection while grid changes, without explicit context. | Make active filter, equipped selection and inspection context clear. |

## Composition, typography and art

| ID | Deficiency | Required correction |
| --- | --- | --- |
| V01 | Nine equipment slots and half the inventory are empty in the showcase. | Populate a believable equipped character and denser inventory fixture while retaining empty-state cases. |
| V02 | Equipment labels are above slots; right-side order and BOOTS/WEAPON/QUIVER labels differ. | Match reference order and caption placement, with real compatible item kinds. |
| V03 | Inventory cells are wide rectangles rather than near squares. | Rebalance columns, spacing and details region without fractional-scale wrapping. |
| V04 | Character name/class are split left/right rather than centered above the portrait. | Restore centered hierarchy and reference-like scale. |
| V05 | Character is isolated over a nearly solid dark slab; alcove, floor, fog, candles and banners are absent. | Separate atmospheric character backdrop from portrait and live controls. |
| V06 | The outer decorative frame is mostly buried; side carvings, burgundy fabric and panel dividers are absent. | Reusable frame/surface artwork with verified content-safe regions; no baked UI apertures. |
| V07 | Inventory heading lacks its burgundy textured title strip. | Restore a distinct section header treatment. |
| V08 | Slots are uniform thin rounded borders; rarity frames lack bevel, wear, highlights and inner texture. | Layer reusable slot art and restrained state-specific borders. |
| V09 | Empty equipment boxes have no category silhouette. | Shared category placeholder treatment. |
| V10 | Attribute strip lacks icons, dividers and reference order. | Add readable symbolic artwork and structured stat columns. |
| V11 | Selected/equipped cards lack item thumbnails, item power, primary-stat hierarchy, affix rows, colored deltas and flavor-text separation. | Structured reusable comparison-card composition. |
| V12 | Detail cards are shallow; empty grid area dominates. | Allocate more height to information and reduce unused grid footprint. |
| V13 | Typography uses different case, size, tracking and serif/sans distribution from reference. | Explicit role-based fonts and measured natural line heights; inspect actual pixels. |
| V14 | Key hints are pale filled rectangles instead of dark inset framed keys; filter/actions look generic and focus leaks engine blue. | Palette-consistent shared key/button states. |
| V15 | Tooltip is a fixed 478px slab with large blank gaps and duplicates lower cards. | Natural measured height, concise hierarchy and conditional content. |
| V16 | Shell is almost opaque and largely textureless. | Layer restrained texture/translucency with readable text contrast. |
| V17 | Gym debug overlays cover part of header and footer in captures. | Distinguish gym chrome from UI; capture clean presentation where supported while retaining production-path evidence. |
| V18 | Supply inspection inherits meaningless gear affix rows and ITEM POWER 0. | Add a supply-specific conditional details presentation; observed during revised-state capture. |

## Pipeline and verification gaps

| ID | Deficiency | Boundary / correction |
| --- | --- | --- |
| P01 | Passing callback tests was treated as visual completion. | Explicit reference checklist and side-by-side rendered review are acceptance requirements. |
| P02 | Drag test calls native proposal API, not a full routed pointer drag. | Add real Slate pointer drag to the production proof. |
| P03 | Drag ghost is raw record-key text. | Reusable image/name drag presentation needs a native adapter addition if included in this pass. |
| P04 | Keyboard/gamepad coverage does not prove physical controller usability or device-aware hints. | Keep limitation explicit and add input-device hint policy separately. |
| P05 | No resize/aspect/UI-scale matrix beyond three 16:9 sizes. | Preserve current bounds proof; other aspect ratios and user scale remain acceptance work. |
| P06 | Resource/font loading is loose-file Editor code. | Cooked Windows resource proof remains separate. |
| P07 | Portrait is static and does not reflect equipment visually; no persistent/live gameplay backend. | Distinguish the UI fixture from a production character renderer and inventory adapter. |
| P08 | Shared suite has three unresolved sibling failures. | Preserve exact evidence; do not claim repository-wide green. |

Most visible deficits are incomplete authoring, model projection or missing art, not absent HTML/CSS capability. Existing templates, atlas-image, image bindings, visibility bindings, action-card selection, styled button focus and measured tooltips already cover most corrections. New native functionality must be justified by a verified missing capability.

## Implementation order

1. Correct action/filter/comparison truth and typed stat projection.
2. Recompose square slots, populated equipment, centered identity, structured comparison cards and compact action footer.
3. Add separate atmospheric/frame/icon artwork and palette-consistent states.
4. Strengthen production pointer/control assertions; render, compare and record remaining gaps by ID.

## Revision result

This audit was written before implementation. The following records the verified first correction pass, not completion of the entire fidelity target.

| IDs | Result and evidence |
| --- | --- |
| B01-B04, B07-B08, B12-B13 | Contextual clickable footer, active-filter action cards independent of selection, full comparison-card visibility and tooltip comparison toggle, owned capacity and selection context. Focused PIE assertions pass. Unavailable primary key is hidden while the label indicates unavailable. |
| B05-B06 | Typed damage, armor, strength, dexterity, life, critical chance and power; derived totals and signed comparison deltas. PIE proves Strength 39 to 50 after weapon swap and ring-dependent Life/Dexterity changes. Both detailed cards share one template. |
| B09-B10 | R cycles an explicit Ring1/Ring2 target, including empty Ring2. Tests prove destination selection, occupied-slot replacement and compatible swaps. Ordinary arrow navigation still skips empty equipment positions; explicit ring targeting resolves the ambiguous destination. |
| B11 | USE consumes one fixture potion, removes a depleted stack and reconciles selection. It does not claim to heal a live gameplay actor. |
| V01, V03-V05, V08-V09, V11-V13, V15 | Six-by-four square inventory layout, 16 inventory records plus nine occupied equipment slots, centered identity, independent cathedral/portrait layers, shared textured backplates and dim category placeholders, serif body/italic flavor faces, structured detail cards and measured tooltips. Captures inspected at 1280x720, 1920x1080 and 2400x1350. |
| V02 | Slot ordering and captions corrected. **Remaining:** the quiver still uses a bow atlas cell and needs dedicated artwork. |
| V06-V07, V10, V16 | Thin content-safe perimeter, distinct burgundy headings, illustrated stat strip and translucent surfaces improve the baseline. **Second pass:** textured title fabric and dedicated glyphs added. **Remaining:** richer carved side panels and closer reference material/lighting matching. |
| V14 | Shared dark inset key hints, active filter styling and removal of generic action slab are in place. **Remaining:** focused item captures can still show Slate's extra blue focus outline; custom slot border color alone does not suppress that separate cue. |
| V17 | Footer moved clear of bottom debug text. Gym reminder/status chrome remains visible in full-viewport proof captures; a clean presentation capture mode remains follow-up. |
| V18 | Second pass hides the entire gear affix group and power for supplies. Rendered capture and effective-visibility assertions pass. |
| P01-P03 | Explicit reference audit plus rendered review. Real routed Slate drag now asserts an active drag and accepted model mutation. Shared native drag decorator supports item name/atlas thumbnail, atomic optional-group validation, invalid-index/size fallback and stale-source suppression; focused native tests pass. |
| P04-P08 | Physical gamepad/device-aware hints, alternate aspect/UI scale, cooked Windows lookup, live character/gameplay adapters and the three older sibling failures remain open. No broader green or 99% fidelity claim. |

## Additional pipeline findings

The reflected authored session did not register `action-card`, although native hosts could use it. It now registers the reusable control alongside its other standard controls, admitting the screen's selected filters through the actual script path.

Scalar Color bindings are not exposed by the reflected session. This proof uses typed positive/negative/neutral visibility variants for per-row delta colors; a scalar-color bridge is a future authoring ergonomics improvement, not a blocker to the current visual effect.

Detailed evidence is in [verification.md](verification.md). The original rendered baseline is retained as `runtime-before-detail-revision.png`.


## Follow-up: materials, stat symbols and systemic spacing

The user's follow-up identified gaps still visible after the first correction pass. The previous green interaction gate did not resolve them.

| ID | Additional finding | Second-pass implementation / acceptance |
| --- | --- | --- |
| V19 | Character stats still used unrelated item thumbnails and a flat background. | Dedicated fist/dagger/shield/life glyph atlas, framed leather strip, separate columns and dividers. Inspect actual symbols at the smallest capture. |
| V20 | Item affixes lacked glyphs, deliberate column layout and mirrored gain/loss presentation. | Shared inspection template now owns icon, right-justified value, left-justified label and right-justified signed delta; matching gain/loss arrows on both cards. Fixture has a real Strength gain and Dexterity loss. |
| V21 | Header, character identity and detail panels lacked material treatment. | Independent burgundy title strips and framed leather panel artwork, kept separate from live text and controls. |
| V22 | Name, level and class lacked independent color/style and safe insets. | Separate LYRA / LEVEL / 42 / ROGUE nodes with explicit face, size, color and alignment inside a padded identity owner. |
| P09 | Repeated one-off spacing corrections had no shared session-level token configuration or inner-content geometry checks. | Expose the renderer's existing token support through reflected session config; define spacing/ink roles once and verify identity, stat and card text containment at three viewport sizes. |

The final material/spacing screen gate passes 2/2 tests, and the shared authored-session gate passes 4/4 including the new token bridge. Captures were inspected at 1280x720, 1920x1080 and 2400x1350, plus the supply/tooltip state. New geometry assertions initially caught real identity and stat line-box overflow; corrected container allocation now passes at all three scales. Dedicated glyphs, materials, semantic text roles, aligned mirrored deltas and supply-specific visibility are in place. This does not establish pixel-identical reference fidelity or close the remaining production integration gaps.
