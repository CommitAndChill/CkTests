# Widget breakdown

The UMG shell creates the scenery backdrop and a ScaleToFit 1920 x 1080 authored surface. The main region has 79 source IDs. The six spell use IDs appear below; template-local IDs are separate because the contract validator inventories only the region source tree.

The spell-slot template declares image art, number cooldown, number remaining, and literal key parameters. Each instance expands to slot (column), frame (column), inlay (column), art (hud-ability), and key (key-hint). CSS sizes the nested frame at 76 x 76, inlay at 66 x 66, and art at 62 x 62. The key hint is a centered native text widget; only an authored action makes it interactive.

| Instance | Art | Cooldown fraction | Seconds remaining | Key |
| --- | --- | --- | --- | --- |
| skill-1 | AbilityCut | CooldownReady | RemainingReady | 1 |
| skill-2 | AbilityFlurry | CooldownFlurry | RemainingFlurry | 2 |
| skill-3 | AbilityShadow | SkillCooldown | RemainingShadow | 3 |
| skill-4 | AbilityDash | CooldownReady | RemainingReady | 4 |
| skill-left | AbilityStrike | CooldownStrike | RemainingStrike | R |
| skill-right | AbilityPierce | CooldownReady | RemainingReady | T |

Shadow, flurry, and strike start at 6.2/10, 1.92/8, and 9.36/12 seconds. Widget Tick decrements seconds and derives the fractions; ready slots stay at zero. CAST and gym key 0 restart shadow at 10 seconds. Reset restores the initial values.

The orb maps moving liquid UVs onto circular geometry under fixed lighting and a separate glass highlight, with the fill clipped by its bound fraction. The spell uses a clockwise radial cooldown mask and a centered remaining-seconds label while active. Current visual verification is pending; earlier screenshots predate this revision.

| Widget ID | Tag | Parent | Source binding, action, or literal |
| --- | --- | --- | --- |
| fantasy-root | overlay | main region | - |
| ornamental-frame | hud-graphic | fantasy-root | image-bind=HudFrame |
| hud-layout | column | fantasy-root | - |
| header | row | hud-layout | - |
| player-header | column | header | - |
| player-name | text | player-header | text=LYRA / LEVEL 42 |
| player-class | text | player-header | text=ROGUE - WORLD TIER IV |
| quest | column | header | - |
| quest-title | text | quest | text=BENEATH THE ASHEN CITY |
| quest-objective | text | quest | text=Find the keeper of the forgotten gate |
| quest-progress | text | quest | text=THE DROWNED PASSAGE / 2 OF 3 SEALS |
| map-panel | column | header | - |
| map-title | text | map-panel | text=THE FORSAKEN SEWERS |
| minimap | hud-graphic | map-panel | image-bind=Minimap |
| map-meta | row | map-panel | - |
| map-danger | text | map-meta | text=HOSTILE TERRITORY |
| map-time | text | map-meta | text=23:47 |
| playfield | row | hud-layout | - |
| party | column | playfield | - |
| party-title | text | party | text=PARTY / 3 |
| party-lyra | column | party | - |
| party-lyra-name | text | party-lyra | text=42  LYRA |
| party-lyra-vitals | column | party-lyra | - |
| party-lyra-class | text | party-lyra | text=ROGUE |
| party-orin | column | party | - |
| party-orin-name | text | party-orin | text=40  ORIN |
| party-orin-vitals | column | party-orin | - |
| party-orin-class | text | party-orin | text=BARBARIAN |
| party-mira | column | party | - |
| party-mira-name | text | party-mira | text=41  MIRA |
| party-mira-vitals | column | party-mira | - |
| party-mira-class | text | party-mira | text=SORCERER |
| playfield-spacer | column | playfield | - |
| chat-zone | column | playfield | - |
| chat-top-space | column | chat-zone | - |
| chat | column | chat-zone | - |
| chat-channel | text | chat | text=PARTY / LOCAL |
| chat-orin | text | chat | text=Orin: Stay close. Something moves. |
| chat-mira | text | chat | text=Mira: The gate is just ahead. |
| chat-action | text | chat | bind=ActionStatus |
| chat-hint | text | chat | text=ENTER TO CHAT |
| bottom-hud | row | hud-layout | - |
| health-panel | column | bottom-hud | - |
| health-orb | hud-orb | health-panel | value-bind=HealthFraction, liquid-bind=OrbHealth, frame-bind=OrbBezel |
| potion-hint | row | health-panel | - |
| potion-key | key-hint | potion-hint | label=Q, font-family=fantasy-ui |
| potion-count | text | potion-hint | text=9/9 |
| hotbar-panel | column | bottom-hud | - |
| hotbar-caption | text | hotbar-panel | text=Gained Berserk |
| hotbar | row | hotbar-panel | - |
| skill-1 | use | hotbar | template=spell-slot, art-bind=AbilityCut, cooldown-bind=CooldownReady, remaining-bind=RemainingReady, key=1 |
| skill-2 | use | hotbar | template=spell-slot, art-bind=AbilityFlurry, cooldown-bind=CooldownFlurry, remaining-bind=RemainingFlurry, key=2 |
| skill-3 | use | hotbar | template=spell-slot, art-bind=AbilityShadow, cooldown-bind=SkillCooldown, remaining-bind=RemainingShadow, key=3 |
| skill-4 | use | hotbar | template=spell-slot, art-bind=AbilityDash, cooldown-bind=CooldownReady, remaining-bind=RemainingReady, key=4 |
| skill-left | use | hotbar | template=spell-slot, art-bind=AbilityStrike, cooldown-bind=CooldownStrike, remaining-bind=RemainingStrike, key=R |
| skill-right | use | hotbar | template=spell-slot, art-bind=AbilityPierce, cooldown-bind=CooldownReady, remaining-bind=RemainingReady, key=T |
| utility-row | row | hotbar-panel | - |
| evade-hint | key-hint | utility-row | label=SPACE, font-family=fantasy-ui |
| open-wheel | button | utility-row | action=OpenWheel, text=WHEEL |
| advance-cooldown | button | utility-row | action=AdvanceCooldown, text=CAST |
| mana-panel | column | bottom-hud | - |
| mana-orb | hud-orb | mana-panel | value-bind=ManaFraction, liquid-bind=OrbEnergy, frame-bind=OrbBezel |
| resource-hint | key-hint | mana-panel | label=T, font-family=fantasy-ui |
| wheel-zone | column | fantasy-root | visible=WheelOpen |
| wheel-dots | row | wheel-zone | - |
| dot-active | text | wheel-dots | text=● |
| dot-two | text | wheel-dots | text=● |
| dot-three | text | wheel-dots | text=● |
| wheel-stage | overlay | wheel-zone | - |
| wheel-vignette | hud-graphic | wheel-stage | image-bind=WheelVignette, visible=WheelOpen |
| interaction-wheel | radial-selector | wheel-stage | items-bind=Interactions, selection-bind=Selection, select=SelectInteraction, activate=ActivateInteraction, visible=WheelOpen, font-family=fantasy-ui |
| wheel-center | column | interaction-wheel | - |
| center-top-space | column | wheel-center | - |
| center-disc | column | wheel-center | - |
| close-wheel | key-hint | center-disc | label=E, action=CloseWheel, font-family=fantasy-ui |
| wheel-close-label | text | center-disc | text=Close |
| center-bottom-space | column | wheel-center | - |
| customize-shell | column | wheel-zone | - |
| customize-wheel | button | customize-shell | action=ResetFixture, text=CUSTOMIZE |

The interaction-wheel renders eight collection records keyed by greet, thanks, follow, wait, help, trade, inspect, and leave. Each record has text actor/glyph, numeric angle/cooldown, bool enabled/queued, and image art. The wheel-center slot contains the actionable E key hint and Close label.
