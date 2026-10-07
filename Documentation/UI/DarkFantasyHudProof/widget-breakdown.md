# Widget breakdown

The UMG shell is `FantasyOverlay` (`UOverlay`), `FantasyBackdropScale` (`UScaleBox`), `FantasyBackdrop` (`UImage`), `FantasyScale` (`UScaleBox`), `FantasyFrame` (`USizeBox`, 1920×1080), and `FantasySurface` (`UCk_UI_AuthoredSurface_UE`). Every HUD label and control below belongs to the code-authored main region. The backdrop is scenery only.

| Widget id | Tag | Parent | Binding / visibility / action / class | Text or role |
| --- | --- | --- | --- | --- |
| `fantasy-root` | `overlay` | `shell` | — | — |
| `ornamental-frame` | `hud-graphic` | `fantasy-root` | image-bind=HudFrame | — |
| `hud-layout` | `column` | `fantasy-root` | — | — |
| `header` | `row` | `hud-layout` | — | — |
| `player-header` | `column` | `header` | — | — |
| `player-name` | `text` | `player-header` | — | LYRA / LEVEL 42 |
| `player-class` | `text` | `player-header` | — | ROGUE - WORLD TIER IV |
| `quest` | `column` | `header` | — | — |
| `quest-title` | `text` | `quest` | — | BENEATH THE ASHEN CITY |
| `quest-objective` | `text` | `quest` | — | Find the keeper of the forgotten gate |
| `quest-progress` | `text` | `quest` | — | THE DROWNED PASSAGE / 2 OF 3 SEALS |
| `map-panel` | `column` | `header` | — | — |
| `map-title` | `text` | `map-panel` | — | THE FORSAKEN SEWERS |
| `minimap` | `hud-graphic` | `map-panel` | image-bind=Minimap | — |
| `map-meta` | `row` | `map-panel` | — | — |
| `map-danger` | `text` | `map-meta` | — | HOSTILE TERRITORY |
| `map-time` | `text` | `map-meta` | — | 23:47 |
| `playfield` | `row` | `hud-layout` | — | — |
| `party` | `column` | `playfield` | — | — |
| `party-title` | `text` | `party` | — | PARTY / 3 |
| `party-lyra` | `column` | `party` | — | — |
| `party-lyra-name` | `text` | `party-lyra` | — | 42  LYRA |
| `party-lyra-vitals` | `column` | `party-lyra` | — | — |
| `party-lyra-class` | `text` | `party-lyra` | — | ROGUE |
| `party-orin` | `column` | `party` | — | — |
| `party-orin-name` | `text` | `party-orin` | — | 40  ORIN |
| `party-orin-vitals` | `column` | `party-orin` | — | — |
| `party-orin-class` | `text` | `party-orin` | — | BARBARIAN |
| `party-mira` | `column` | `party` | — | — |
| `party-mira-name` | `text` | `party-mira` | — | 41  MIRA |
| `party-mira-vitals` | `column` | `party-mira` | — | — |
| `party-mira-class` | `text` | `party-mira` | — | SORCERER |
| `playfield-spacer` | `column` | `playfield` | — | — |
| `chat-zone` | `column` | `playfield` | — | — |
| `chat-top-space` | `column` | `chat-zone` | — | — |
| `chat` | `column` | `chat-zone` | — | — |
| `chat-channel` | `text` | `chat` | — | PARTY / LOCAL |
| `chat-orin` | `text` | `chat` | — | Orin: Stay close. Something moves. |
| `chat-mira` | `text` | `chat` | — | Mira: The gate is just ahead. |
| `chat-action` | `text` | `chat` | bind=ActionStatus | — |
| `chat-hint` | `text` | `chat` | — | ENTER TO CHAT |
| `bottom-hud` | `row` | `hud-layout` | — | — |
| `health-panel` | `column` | `bottom-hud` | — | — |
| `health-orb` | `hud-orb` | `health-panel` | value-bind=HealthFraction, liquid-bind=OrbHealth, frame-bind=OrbBezel | — |
| `potion-hint` | `row` | `health-panel` | — | — |
| `potion-keycap` | `column` | `potion-hint` | — | — |
| `potion-key` | `text` | `potion-keycap` | — | Q |
| `potion-count` | `text` | `potion-hint` | — | 9/9 |
| `hotbar-panel` | `column` | `bottom-hud` | — | — |
| `hotbar-caption` | `text` | `hotbar-panel` | — | Gained Berserk |
| `hotbar` | `row` | `hotbar-panel` | — | — |
| `skill-1` | `column` | `hotbar` | — | — |
| `ability-cut` | `hud-ability` | `skill-1` | cooldown-bind=CooldownReady, image-bind=AbilityCut | — |
| `skill-1-keycap` | `column` | `skill-1` | — | — |
| `skill-1-key` | `text` | `skill-1-keycap` | — | 1 |
| `skill-2` | `column` | `hotbar` | — | — |
| `ability-flurry` | `hud-ability` | `skill-2` | cooldown-bind=CooldownFlurry, image-bind=AbilityFlurry | — |
| `skill-2-keycap` | `column` | `skill-2` | — | — |
| `skill-2-key` | `text` | `skill-2-keycap` | — | 2 |
| `skill-3` | `column` | `hotbar` | — | — |
| `ability-shadow` | `hud-ability` | `skill-3` | cooldown-bind=SkillCooldown, image-bind=AbilityShadow | — |
| `skill-3-keycap` | `column` | `skill-3` | — | — |
| `skill-3-key` | `text` | `skill-3-keycap` | — | 3 |
| `skill-4` | `column` | `hotbar` | — | — |
| `ability-dash` | `hud-ability` | `skill-4` | cooldown-bind=CooldownReady, image-bind=AbilityDash | — |
| `skill-4-keycap` | `column` | `skill-4` | — | — |
| `skill-4-key` | `text` | `skill-4-keycap` | — | 4 |
| `skill-left` | `column` | `hotbar` | — | — |
| `ability-strike` | `hud-ability` | `skill-left` | cooldown-bind=CooldownStrike, image-bind=AbilityStrike | — |
| `skill-left-keycap` | `column` | `skill-left` | — | — |
| `skill-left-key` | `text` | `skill-left-keycap` | — | R |
| `skill-right` | `column` | `hotbar` | — | — |
| `ability-pierce` | `hud-ability` | `skill-right` | cooldown-bind=CooldownReady, image-bind=AbilityPierce | — |
| `skill-right-keycap` | `column` | `skill-right` | — | — |
| `skill-right-key` | `text` | `skill-right-keycap` | — | T |
| `utility-row` | `row` | `hotbar-panel` | — | — |
| `evade-keycap` | `column` | `utility-row` | — | — |
| `evade-hint` | `text` | `evade-keycap` | — | SPACE |
| `open-wheel` | `button` | `utility-row` | action=OpenWheel | WHEEL |
| `advance-cooldown` | `button` | `utility-row` | action=AdvanceCooldown | CAST |
| `mana-panel` | `column` | `bottom-hud` | — | — |
| `mana-orb` | `hud-orb` | `mana-panel` | value-bind=ManaFraction, liquid-bind=OrbEnergy, frame-bind=OrbBezel | — |
| `resource-keycap` | `column` | `mana-panel` | — | — |
| `resource-hint` | `text` | `resource-keycap` | — | T |
| `wheel-zone` | `column` | `fantasy-root` | visible=WheelOpen | — |
| `wheel-dots` | `row` | `wheel-zone` | — | — |
| `dot-active` | `text` | `wheel-dots` | — | ● |
| `dot-two` | `text` | `wheel-dots` | — | ● |
| `dot-three` | `text` | `wheel-dots` | — | ● |
| `wheel-stage` | `overlay` | `wheel-zone` | — | — |
| `wheel-vignette` | `hud-graphic` | `wheel-stage` | image-bind=WheelVignette, visible=WheelOpen | — |
| `interaction-wheel` | `radial-selector` | `wheel-stage` | items-bind=Interactions, selection-bind=Selection, select=SelectInteraction, activate=ActivateInteraction, visible=WheelOpen, font-family=fantasy-ui | — |
| `wheel-center` | `column` | `interaction-wheel` | — | — |
| `center-top-space` | `column` | `wheel-center` | — | — |
| `center-disc` | `column` | `wheel-center` | — | — |
| `close-wheel` | `button` | `center-disc` | action=CloseWheel | E |
| `wheel-close-label` | `text` | `center-disc` | — | Close |
| `center-bottom-space` | `column` | `wheel-center` | — | — |
| `customize-shell` | `column` | `wheel-zone` | — | — |
| `customize-wheel` | `button` | `customize-shell` | action=ResetFixture | CUSTOMIZE |

`interaction-wheel` also renders eight keyed native collection records. Each record has `actor:Text`, `glyph:Text`, `angle:Number`, `cooldown:Number`, `enabled:Bool`, `queued:Bool`, and `image:Image`. The image field carries its ivory gesture art; actor labels and the selection wedge remain live. The `wheel-center` content slot owns the E/Close control. Six `hud-ability` controls each bind live cooldown state and generated spell art. Two `hud-orb` controls bind live values, generated liquid art, and a shared generated bezel. The keycap columns provide translucent backgrounds while their intrinsic-width text children center the glyphs.
