# Vesper world map widget contract

The generated [visual reference](world-map-reference.png) is 1672 × 941 pixels. The separately authored runtime background asset is 1774 × 887 pixels; live markers, labels, selection and route belong to the native map control. The logical UI canvas is 1920 × 1080. The UMG `UScaleBox` fits it uniformly at narrower viewports and centers it on wider ones. The authored region root has no explicit size; its `screen-shell` child owns the 1880 × 1040 composition.

| Region / ID | Owner | Geometry at 1920 × 1080 | Role and state |
| --- | --- | --- | --- |
| `screen-shell` | Authored column | 1880 × 1040, 20 inset | Translucent dark structural frame |
| `topbar` | Authored row | 1840 × 76 | Map title, city, live district status |
| `mission-rail` | Authored column | 360 × 878 | Selected marker detail, route action, operations note |
| `mission-card` | Authored column | 360 × 656, 24 inset, 8 gap | Selected title, status, distance, description; empty selection copy |
| `route-button`, `clear-route-button` | Ordinary buttons | 310 × 54 each | Exclusive `RouteHidden` / `RouteVisible` states; plot or clear the current route; disabled for player and hazards |
| `map-toolbar` | Authored row | 1460 × 54 | Three mutually exclusive filter states; camera spacer and zoom, reset, 180-pixel center controls |
| `map-view` | Native `world-map` | 1458 × 720 | Texture, keyed markers, selection proposal, route, local camera |
| `map-legend` | Authored row | 1460 × 56 | Semantic marker key and live status |
| `footer` | Authored row | 1840 × 46 | Shared key hints for pan, selection, zoom and close; flexible spacer before the right group |

All marker records remain in the collection when a filter changes; the required `visible` field controls projection. A hidden or removed selected key is replaced with the first enabled eligible objective or service before the collection update. Empty collections clear selection and route. Disabled records remain visible but cannot be selected. Scripted actions publish selection and route; native map input owns pan and zoom. The six filter buttons expose their active state through distinct visibility booleans, so focus cannot masquerade as selection.

The 92 authored IDs and their exact parent, classes, attributes and current CSS declarations are recorded in [design-contract.json](design-contract.json). Rajdhani uses a 1276/1000 hhea line box, so the header's `MAP / VESPER` font size is 30 points in the current CSS; 45/43 point source sizes overran the 54-pixel title row. The mission title at 23 points and description at 15 points use width-constrained natural wrapping inside the 656-pixel card, with 24-pixel padding and 8-pixel gaps. Their text blocks have no fixed heights, so the long-label fixture can grow without reserving blank space in shorter states. Each text and button specifies `map-ui` or `map-display` through its own class, while key hints and the native map specify their font family in markup because ordinary CSS font inheritance is unavailable. The rail pulse uses ASCII bars for dependable glyph coverage. The map bezel says `N ↑ · CITY ATLAS` because a fixed geographic scale would be incorrect after zooming.

At 1280 × 720, the full composition should scale to two-thirds. At 2560 × 1080, the canvas should remain 1920 × 1080 and center with 320-pixel side bars. Native captures must confirm type, insets, marker contrast, hit testing and edge labels; these dimensions are source targets, not rendered proof.
