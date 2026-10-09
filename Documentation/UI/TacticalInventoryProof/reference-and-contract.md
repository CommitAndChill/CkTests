# Reference and widget contract

Visual source: [Escape from Tarkov gear/stash screenshot](https://images.cgames.de/images/gamestar/226/escape-from-tarkov_6019331.jpg),
1440×810, published in [GameStar's inventory guide](https://www.gamestar.de/artikel/escape-from-tarkov-profi-guide-fuenf-praktische-insider-tipps,3324510.html).
This proof reproduces its spatial inventory idiom with original assets and an
explicit inspection column; it is not a pixel-identical copy of that screenshot.

Reference anatomy: slim uppercase navigation, equipment around an operator
silhouette, independently titled storage grids, item-spanned rectangles,
top-right item names, lower-right quantities, subdued charcoal/olive translucent
backplates, pale body ink, warmer selected borders and compact keycaps. Items
remain visible behind overlays. Background art does not define slot geometry.

## Screen regions

| Region/control | Authored parent and geometry at 1920×1080 | State/behavior |
| --- | --- | --- |
| `shell` | Unsized region root; centered 1880×1036 child, 20 horizontal inset | Scales to fit through UMG |
| `navigation` | 52 high, spaced text roles | Gear screen context; other sections are labels, not inert buttons |
| `operator-stage` | 416×552 equipment pane | Independent operator cutout plus reusable six-slot preview template |
| `weapon-preview` / `vitals` | Equipment pane below operator | Read-only loadout and status fixture |
| `pack-grid` | Backpack panel; 6×6 cells at 52 | `Pack` collection; `OnPackMove` proposal |
| `stash-grid` | Stash panel; 10×14 cells at 52 | `Stash` collection; `OnStashMove` proposal |
| `case-panel` / `case-grid` | Backpack column; 4×4 cells at 52 | Entire frame follows `CaseOpen`; `Case` collection; `OnCaseMove` proposal |
| `inspection-panel` | Separate inset column | Selected identity, atlas icon, condition, weight, caliber, footprint, location, quantity, description and eligibility-aware actions |
| `footer` | 50 high | Shared actionable key hints, status feedback |

The fixture contains 30 items: 22 in the stash, five in the backpack and three in
the document case. Record identity is the stable key. Fields: Text `name`, `kind`; Number `x`, `y`,
`w`, `h`, `icon`, `quantity`; Bool `rotated`, `selected`, `enabled`. Width/height
are unrotated cell dimensions. Rotation swaps the footprint and artwork.
Destination events carry `key|x|y|rotated`; the script revalidates complete bounds,
overlap, ownership and the document case's prohibition on containing itself.
Accepted publication retires the whole session if any required scalar or
collection update fails. The reflected session is not a batch scalar transaction.

## Typography and spacing

`tactical-ui` explicitly uses the existing licensed Source Sans 3 Regular/Bold
files in DarkFantasyHudProof/Fonts with an engine fallback. Headings are 15–18
Slate points; body 12; secondary text and grid labels 9–10. Natural line boxes
are distinct from font sizes. Inspection content uses 16 horizontal and 18 top
insets; grid frames own their cells; keycaps use the shared control. CSS variables
resolve from the authored session's frozen StyleTokens map.

## Original image assets and prompts

Built-in image generation produced runtime assets, copied into
`Resources/UiAuthoring/TacticalInventoryProof/`; originals remain in the generated
image directory. No generated text or baked interface is used.

- `item-atlas.png`: transparent 1536×1024, 5×4 equal cells. Prompt: realistic worn
  tactical inventory objects, independent padded cells, no text/grid/UI; ordered
  M4A1, SV98, plate carrier, FAST helmet, IFAK, brass 5.56, document case, STANAG
  magazine, water, F1 grenade, Glock, morphine, rig, pistol magazine, ration,
  keycard, 7.62 rounds, intelligence folder, headset, folding knife.
  Follow-up edits shrank both rifles into their atlas cells and removed background
  haze. All consumers share normalized per-object crops; aspect fitting preserves
  proportions in grid cells, loadout cards, inspection and drag previews.
- `operator.png`: transparent full-body realistic masked operator in olive
  uniform, tan carrier, helmet/headset, low-ready rifle; cool subdued light,
  boots included, padded portrait, no scenery/text/UI.
- `backdrop.png`: defocused abandoned military warehouse, dusty charcoal/olive
  concrete and racks, faint fluorescent lights, low contrast, dark edges, no UI.

These are original proof assets. Their visual similarity does not establish
ownership of or redistribute the game's proprietary art/fonts.
