# Battlefield 4 PC Normal Conquest HUD replica contract

The [Tactical markup](../../../../Resources/UiAuthoring/CombatHudProof/Tactical/CombatHudProof.ui.html) and [stylesheet](../../../../Resources/UiAuthoring/CombatHudProof/Tactical/CombatHudProof.ui.css) replace the old framed, ability-led dashboard with a sparse 1920 × 1080 multiplayer infantry HUD. This is a deterministic CkTests fixture, not a live Battlefield client or a claim of pixel-identical proprietary assets. The [official Battlefield 4 PC manual](https://akamai.cdn.ea.com/eadownloads/u/f/manuals/GAME-BF4/b4-pc-en.pdf#page=14) identifies map, bases, health, fire mode, magazine/total ammunition and grenades as game-screen elements. The [Conquest screenshot reference](https://cdn.mos.cms.futurecdn.net/0e1513788dde3977e866c21723ada121.jpg) informs Normal-mode topology and relative placement. Coordinates below are authored approximations, not extracted source-game measurements.

## Placement and hierarchy

The unsized hud-root overlay mounts in the single main region. The first 1920 × 1080 native damage-overlay establishes overlay bounds; hud-layout and the centered aim-reticle paint above it. The host's passive ScaleToFit/SafeZone presentation maps this logical frame to the viewport. No authored node intercepts gameplay input. Fixed section heights, alignment and grow spacers establish the layout without absolute positioning or transforms.

At logical 1920 × 1080, hud-layout insets 80 left/right and 40 top/bottom. bottom-hud is 322 high and starts near y=718. Its conquest-cluster starts at x=80 and ends y=1040. The A–F flag row is 28 high; the narrower 280-unit RU/CN ticket row and 09:23 timer occupy the next 40 above a 14-unit gap. The 240 × 240 minimap starts at x=80, y=800. Four green squad rows sit to its right, starting x=336. The right weapon-cluster is 230 wide, anchored at x=1610. Its bottom-aligned 118-unit readout starts near y=922. The C diamond and C / 35 m label sit near x=960, y=340; the capture message is on a separate full-screen overlay at y≈738, above the bottom HUD. The conditional killfeed uses the upper-right inset. The small four-tick white crosshair is centered at x=960, y=540.

All IDs below are unique within main. Rows follow source order; text bodies are literals and attributes named bind, value-bind and visible read typed model bindings. A blank properties cell means no binding. replica-art is a passive native vector leaf.

| ID | Parent | Node | Properties / literal |
| --- | --- | --- | --- |
| &nbsp;&nbsp;&nbsp;&nbsp;hud-root | main | overlay | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;damage-overlay | hud-root | replica-art | piece=damage, value-bind=DamageOpacity |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;hud-layout | hud-root | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;upper-hud | hud-layout | row | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;upper-space | upper-hud | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;kill-feed | upper-hud | column | visible=ShowKillFeed |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;kill-feed-text | kill-feed | text | bind=KillFeedText |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;pre-objective-space | hud-layout | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;objective-row | hud-layout | row | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;objective-left-space | objective-row | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;objective-marker | objective-row | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;objective-icon | objective-marker | replica-art | piece=bf-objective |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;objective-distance | objective-marker | text | body=C / 35 m |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;objective-right-space | objective-row | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;central-world-space | hud-layout | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;pre-bottom-space | hud-layout | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;bottom-hud | hud-layout | row | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;conquest-cluster | bottom-hud | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;flag-row | conquest-cluster | row | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;conquest-flags | flag-row | text | bind=FlagText |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;ticket-row | conquest-cluster | row | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;friendly-side | ticket-row | text | body=RU |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;tickets-friendly | ticket-row | text | bind=FriendlyTickets |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;ticket-divider | ticket-row | text | body=/ |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;enemy-side | ticket-row | text | body=CN |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;tickets-enemy | ticket-row | text | bind=EnemyTickets |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;timer-space | ticket-row | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;round-timer | ticket-row | text | body=09:23 |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;map-top-space | conquest-cluster | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;map-and-squad | conquest-cluster | row | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;minimap | map-and-squad | replica-art | piece=bf-map, value-bind=MapPhase |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;squad-list | map-and-squad | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;squad-one | squad-list | text | body=A  ALPHA LEAD |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;squad-two | squad-list | text | body=B  RIFLEMAN |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;squad-three | squad-list | text | body=C  SUPPORT |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;squad-four | squad-list | text | body=D  RECON |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;bottom-center-space | bottom-hud | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;weapon-cluster | bottom-hud | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;weapon-top-space | weapon-cluster | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;ammunition | weapon-cluster | row | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;ammo-value | ammunition | text | bind=AmmoText |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;ammo-divider | ammunition | text | body=/ |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;reserve-value | ammunition | text | bind=ReserveText |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;weapon-mode-row | weapon-cluster | row | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;fire-mode | weapon-mode-row | text | body=AUTO |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;reload-space | weapon-mode-row | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;reload-status | weapon-mode-row | text | bind=ReloadText, visible=ShowReload |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;equipment-row | weapon-cluster | row | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;grenade-icon | equipment-row | replica-art | piece=bf-grenade |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;grenade-count | equipment-row | text | body=1 |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;equipment-space | equipment-row | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;health-plus | equipment-row | text | body=+ |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;health-value | equipment-row | text | bind=HealthText |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;capture-layer | hud-root | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;capture-offset | capture-layer | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;capture-row | capture-layer | row | visible=ShowCapture |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;capture-left-space | capture-row | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;capture-copy | capture-row | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;capture-status | capture-copy | text | bind=CaptureText, visible=ShowCapture |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;capture-right-space | capture-row | column | — |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;aim-reticle | hud-root | replica-art | piece=crosshair |

## Model and context behavior

| State or command | Authored response | Limit |
| --- | --- | --- |
| Normal Conquest fixture | conquest-flags reads FlagText; RU/CN counts read FriendlyTickets and EnemyTickets; 09:23 and four squad rows are static. | No scoreboard, live clock, multiplayer team service or per-letter ownership styling. |
| Movement / map phase | minimap reads numeric MapPhase. Native bf-map draws a deterministic grayscale street grid, blocks and markers and changes phase. | No world geometry, fog, GPS or live objectives. |
| Objective / capture | Native bf-objective supplies the compact C diamond. C / 35 m is static fixture copy. ShowCapture collapses the lower-center row and its bound capture-status; CaptureText supplies the phase. | Distance and capture geometry are representational, not computed from a level actor. |
| Combat feed | ShowKillFeed collapses the upper-right feed when inactive. KillFeedText supplies one event. | No multiplayer event history. |
| Fire | AmmoText updates the large magazine number and ReserveText updates the smaller total to its right. AUTO and one grenade are static. | No attachment panel, ammo bar, endurance or cooldown rail. |
| Reload | ShowReload reveals reload-status beside AUTO; ReloadText supplies the label until completion. | No reload progress bar in this Normal-mode layout. |
| Damage / recover | HealthText updates the compact + number. DamageOpacity drives the full-screen native damage treatment behind the HUD. | No permanent health bar or large frame. |
| Default / absent context | Reticle, map, tickets, squad, ammo and health remain. Conditional capture, feed and reload copy collapse. | No interactive authored controls. |

The functional palette uses friendly cyan (#65D6E6), enemy orange (#EFAB67), squad green (#7FCB78), and white/grey combat readouts. Core ticket, flag and objective copy uses 18–20 Slate units; auxiliary feed and squad text uses 16. The 48-unit magazine number is intentionally larger. This uses the native base face and supported authored weight/size; it does not assert the exact Battlefield typeface or include branded art. At 1280 × 720 the logical frame scales to two-thirds, making the 16-unit auxiliary text the legibility limit. At 2560 × 1080 the map should retain its square proportions and extra horizontal viewport area should not stretch it. The [native gallery](replica-native-gallery.md) and [capture manifest](replica-native-captures.json) record actual scaling and clipping review at three viewport sizes.

The source intentionally omits the former health, stamina, ammo, reload and cooldown meters; compass, ornamental panels, brand cards, ability carousel and status rail. The exact font-size: 18; font-weight: bold; sequence on .objective-distance is the accepted-style reload probe. Changing it should affect authored text without replacing compatible native art IDs. Rejected source must preserve the last accepted revision and native leaves. Static source preflight checks XML shape, unique IDs, CSS classes and design-contract identity; it does not prove typed binding availability, rendering, or screenshot fidelity. The focused native result is in the [gate receipt](../replica-verification.md): 41/41 selected tests passed on D3D12/SM6, with final viewport captures inspected.

Capture-only debug/logging settings were transient and restored. The evidence is scoped to the replica fixture and compatible UI tests; physical gamepad, real gameplay integration and packaged execution remain unverified.
