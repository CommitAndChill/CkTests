# Widget breakdown

All authored widgets are in the main region. Native shell: FantasyOverlay (root UOverlay), FantasyBackdropScale (UScaleBox / art), FantasyBackdrop (UImage / scenery), FantasyScale (UScaleBox / ScaleToFit), FantasyFrame (USizeBox / 1920x1080), FantasySurface (UCk_UI_AuthoredSurface_UE / main). No gameplay controls or text are baked into the scenery or ornament.

| Widget id | Tag | Parent | Binding / visibility / action | Editable content / role |
|---|---|---|---|---|
| fantasy-root | overlay | main |  | fantasy-root |
| ornamental-frame | hud-graphic | fantasy-root | image-bind=HudFrame | ornamental-frame |
| hud-layout | column | fantasy-root |  | hud-layout |
| header | row | hud-layout |  | header |
| player-header | column | header |  | player-header |
| player-name | text | player-header |  | LYRA / LEVEL 42 |
| player-class | text | player-header |  | ROGUE - WORLD TIER IV |
| quest | column | header |  | quest |
| quest-title | text | quest |  | BENEATH THE ASHEN CITY |
| quest-objective | text | quest |  | Find the keeper of the forgotten gate |
| quest-progress | text | quest |  | THE DROWNED PASSAGE / 2 OF 3 SEALS |
| map-panel | column | header |  | map-panel |
| map-title | text | map-panel |  | THE FORSAKEN SEWERS |
| minimap | hud-graphic | map-panel | image-bind=Minimap | minimap |
| map-meta | row | map-panel |  | map-meta |
| map-danger | text | map-meta |  | HOSTILE TERRITORY |
| map-time | text | map-meta |  | 23:47 |
| playfield | row | hud-layout |  | playfield |
| party | column | playfield |  | party |
| party-title | text | party |  | PARTY / 3 |
| party-lyra | column | party |  | party-member |
| party-lyra-name | text | party-lyra |  | 42  LYRA |
| party-lyra-vitals | column | party-lyra |  | party-vitals |
| party-lyra-class | text | party-lyra |  | ROGUE |
| party-orin | column | party |  | party-member |
| party-orin-name | text | party-orin |  | 40  ORIN |
| party-orin-vitals | column | party-orin |  | party-vitals |
| party-orin-class | text | party-orin |  | BARBARIAN |
| party-mira | column | party |  | party-member |
| party-mira-name | text | party-mira |  | 41  MIRA |
| party-mira-vitals | column | party-mira |  | party-vitals |
| party-mira-class | text | party-mira |  | SORCERER |
| playfield-spacer | column | playfield |  | grow |
| interaction-wheel | radial-selector | wheel-zone | items-bind=Interactions; selection-bind=Selection; select=SelectInteraction; activate=ActivateInteraction; visible=WheelOpen | interaction-wheel |
| wheel-center | column | content |  | wheel-center |
| center-top-space | column | wheel-center |  | grow |
| wheel-heading | text | wheel-center |  | INTERACT |
| wheel-selection | text | wheel-center | bind=SelectionLabel | wheel-selection |
| activate-selection | button | wheel-center | action=ActivateSelection | CONFIRM |
| close-wheel | button | wheel-center | action=CloseWheel | CLOSE |
| center-bottom-space | column | wheel-center |  | grow |
| chat-zone | column | playfield |  | chat-zone |
| chat-top-space | column | chat-zone |  | grow |
| chat | column | chat-zone |  | chat |
| chat-channel | text | chat |  | PARTY / LOCAL |
| chat-orin | text | chat |  | Orin: Stay close. Something moves. |
| chat-mira | text | chat |  | Mira: The gate is just ahead. |
| chat-action | text | chat | bind=ActionStatus | chat-line action-line |
| chat-hint | text | chat |  | ENTER TO CHAT |
| bottom-hud | row | hud-layout |  | bottom-hud |
| health-panel | column | bottom-hud |  | orb-panel |
| health-orb | overlay | health-panel |  | orb |
| health-meter | hud-meter | health-orb | value-bind=HealthFraction | health-meter |
| health-labels | column | health-orb |  | orb-labels |
| health-top-space | column | health-labels |  | grow |
| health-title | text | health-labels |  | LIFE |
| health-value | text | health-labels | bind=HealthText | orb-value |
| health-bottom-space | column | health-labels |  | grow |
| potion-key | text | health-panel |  | Q / POTION  4/4 |
| hotbar-panel | column | bottom-hud |  | hotbar-panel |
| hotbar-caption | text | hotbar-panel |  | SHADOWS ANSWER YOUR CALL |
| hotbar | row | hotbar-panel |  | hotbar |
| skill-1 | column | hotbar |  | skill |
| skill-1-glyph | text | skill-1 |  | / |
| skill-1-label | text | skill-1 |  | CUT |
| skill-1-key | text | skill-1 |  | 1 |
| skill-2 | column | hotbar |  | skill |
| skill-2-glyph | text | skill-2 |  | X |
| skill-2-label | text | skill-2 |  | FLURRY |
| skill-2-key | text | skill-2 |  | 2 |
| skill-3 | column | hotbar |  | skill |
| skill-3-glyph | text | skill-3 |  | + |
| skill-3-label | text | skill-3 |  | SHADOW |
| skill-3-key | text | skill-3 |  | 3 |
| skill-4 | column | hotbar |  | skill |
| skill-4-glyph | text | skill-4 |  | V |
| skill-4-label | text | skill-4 |  | DASH |
| skill-4-key | text | skill-4 |  | 4 |
| skill-left | column | hotbar |  | skill |
| skill-left-glyph | text | skill-left |  | I |
| skill-left-label | text | skill-left |  | STRIKE |
| skill-left-key | text | skill-left |  | LMB |
| skill-right | column | hotbar |  | skill |
| skill-right-glyph | text | skill-right |  | * |
| skill-right-label | text | skill-right |  | PIERCE |
| skill-right-key | text | skill-right |  | RMB |
| utility-row | row | hotbar-panel |  | utility-row |
| evade-hint | text | utility-row |  | SPACE EVADE |
| open-wheel | button | utility-row | action=OpenWheel | OPEN WHEEL |
| inventory-hint | text | utility-row |  | I BAG |
| mana-panel | column | bottom-hud |  | orb-panel |
| mana-orb | overlay | mana-panel |  | orb |
| mana-meter | hud-meter | mana-orb | value-bind=ManaFraction | mana-meter |
| mana-labels | column | mana-orb |  | orb-labels |
| mana-top-space | column | mana-labels |  | grow |
| mana-title | text | mana-labels |  | ENERGY |
| mana-value | text | mana-labels | bind=ManaText | orb-value |
| mana-bottom-space | column | mana-labels |  | grow |
| resource-hint | text | mana-panel |  | SHADOW ATTUNEMENT |
| wheel-zone | column | fantasy-root |  | wheel-zone |

The radial-selector owns eight native keyed interaction entries (greet, thanks, follow, wait, help, trade, inspect, leave), generated from Interactions records rather than eight markup children. Every record uses the same actor/glyph/angle/cooldown/enabled/queued schema. Its content slot owns the wheel-center column and live confirm/close controls. The party strips and hotbar cells are static editable fixture content; live health/mana use hud-meter, and current interaction output uses chat-action.
