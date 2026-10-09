# Power wheel widget breakdown

This is a source-derived inventory of all 60 authored IDs in the [design contract](design-contract.json). Sizes are logical layout units before ScaleToFit. The earlier focused native gate passed 10/10; its [captures and measured text-fit evidence](verification.md) cover 720p, 1080p and ultrawide before this backdrop and typography revision. The sizes below describe the revised source; its [current native gallery and 34/34 gate](../ReplicaRemakes/README.md) cover the new backdrop, fonts and shared keycaps.

The code-created host mounts a fixed 1920 × 1080 backdrop and 0x55 black tint within the mounted overlay around the existing wheel-region and wheel-layout composition. The retained selector is authored at 800 × 720, with local center (400, 360), radius 280.8 and 56-unit tiles. Angles increase clockwise from screen-right. Six broken arcs, chamfered tile frames, token-specific native glyphs, selected and queued rims, and normalized cooldown overlays are source-defined drawing rules, not capture measurements. The selector suppresses Slate's widget-wide focus rectangle; the selected glyph frame remains the focus cue. The center power name uses a natural text width capped at 360 logical units; the 360-unit queue hint is a row of shared key hints and text.

| ID | Parent | Native node | Role | Authored CSS | Binding / literal |
| --- | --- | --- | --- | --- | --- |
| wheel-root | main region | Yoga-backed Slate overlay | Backdrop and retained wheel composition |  | — |
| replica-backdrop | wheel-root | Slate image | Bound cinematic scene image | width: 1920px; height: 1080px | bind=ReplicaBackdrop |
| scene-dim | wheel-root | Yoga-backed Slate column | Passive full-frame tint behind wheel controls | width: 1920px; height: 1080px; background-color: #00000055 | — |
| wheel-region | wheel-root | Yoga-backed Slate column | Unsized main-region mount root; fixed logical dimensions belong to wheel-layout child |  | — |
| wheel-layout | wheel-region | Yoga-backed Slate column | Logical composition root | width: 1920px; height: 1080px | — |
| target-row-1 | wheel-layout | Yoga-backed Slate column | Target identity and protection strips | width: 1920px; height: 106px; padding: 24px 600px 0px | — |
| target-state | target-row-1 | Slate text | Live target name/protection | font-family: me-ui; height: 36px; font-size: 24; color: #E4F8FF; horizontal-align: center; text-wrap: nowrap | bind=TargetState |
| target-bars-1 | target-row-1 | Yoga-backed Slate column | Stacked shield/armor strips | width: 500px; height: 13px; horizontal-align: center; gap: 3px | — |
| target-shield-1 | target-bars-1 | Yoga-backed Slate column | Conditional shield strip | width: 500px; height: 5px; background-color: #65C8EC | visible=TargetShielded |
| target-armor-1 | target-bars-1 | Yoga-backed Slate column | Armor strip | width: 500px; height: 5px; background-color: #C68F64 | — |
| wheel-row-1 | wheel-layout | Yoga-backed Slate row | Description, selector, instruction body | width: 1920px; height: 808px | — |
| description-column-1 | wheel-row-1 | Yoga-backed Slate column | Selected-power details | width: 560px; height: 808px; padding: 0px 32px 0px 112px | — |
| grow-1 | description-column-1 | Yoga-backed Slate column | Flexible spacer | flex-grow: 1 | — |
| actor-name | description-column-1 | Slate text | Selected actor | font-family: me-ui; height: 34px; font-size: 20; color: #E5F8FF; text-wrap: nowrap | bind=ActorName |
| power-description | description-column-1 | Slate text | Selected power description | font-family: me-ui; width: 416px; height: 124px; font-size: 25; color: #9FEAFF; text-wrap: wrap | bind=PowerDescription |
| power-state | description-column-1 | Slate text | Ready, queued or blocked reason | font-family: me-ui; width: 416px; height: 68px; font-size: 18; color: #E0F3FA; text-wrap: wrap | bind=PowerState |
| description-bottom-1 | description-column-1 | Yoga-backed Slate column | Detail-area spacer | height: 112px | — |
| wheel-column-1 | wheel-row-1 | Yoga-backed Slate column | Squad portraits and selector | width: 800px; height: 808px | — |
| squad-row-1 | wheel-column-1 | Yoga-backed Slate row | Two squadmate summaries | width: 800px; height: 88px; padding: 0px 6px | — |
| squad-member-1 | squad-row-1 | Yoga-backed Slate column | Squadmate summary | width: 350px; height: 86px | — |
| portrait-row-1 | squad-member-1 | Yoga-backed Slate row | Portrait and labels | width: 350px; height: 76px; gap: 12px | — |
| garrus-portrait-frame | portrait-row-1 | Yoga-backed Slate column | Framed portrait container; owns border, background and inset | width: 70px; height: 70px; padding: 2px; background-color: #09232BCF; border-color: #8CDDF5; border-width: 1px; border-radius: 5px | — |
| garrus-portrait | garrus-portrait-frame | Slate image | Bound squadmate atlas image | width: 66px; height: 66px | bind=GarrusPortrait |
| portrait-label-1 | portrait-row-1 | Yoga-backed Slate column | Name and queue label | width: 264px; height: 76px; padding: 6px 0px | — |
| squad-name-1 | portrait-label-1 | Slate text | Literal squadmate name | font-family: me-ui; height: 30px; font-size: 18; color: #BAEDFA; text-wrap: nowrap | literal=GARRUS |
| queue-garrus | portrait-label-1 | Slate text | Bound queued-power label | font-family: me-ui; height: 40px; font-size: 15; color: #F1D385; text-wrap: wrap | bind=QueueGarrus |
| squad-gap-1 | squad-row-1 | Yoga-backed Slate column | Flexible squad separation | flex-grow: 1 | — |
| squad-member-2 | squad-row-1 | Yoga-backed Slate column | Squadmate summary | width: 350px; height: 86px | — |
| portrait-row-2 | squad-member-2 | Yoga-backed Slate row | Portrait and labels | width: 350px; height: 76px; gap: 12px | — |
| portrait-label-2 | portrait-row-2 | Yoga-backed Slate column | Name and queue label | width: 264px; height: 76px; padding: 6px 0px | — |
| squad-name-2 | portrait-label-2 | Slate text | Literal squadmate name | font-family: me-ui; height: 30px; font-size: 18; color: #BAEDFA; text-wrap: nowrap | literal=LIARA |
| queue-liara | portrait-label-2 | Slate text | Bound queued-power label | font-family: me-ui; height: 40px; font-size: 15; color: #F1D385; text-wrap: wrap | bind=QueueLiara |
| liara-portrait-frame | portrait-row-2 | Yoga-backed Slate column | Framed portrait container; owns border, background and inset | width: 70px; height: 70px; padding: 2px; background-color: #09232BCF; border-color: #8CDDF5; border-width: 1px; border-radius: 5px | — |
| liara-portrait | liara-portrait-frame | Slate image | Bound squadmate atlas image | width: 66px; height: 66px | bind=LiaraPortrait |
| power-wheel | wheel-column-1 | SCkUiRadialSelector retained native control | Native radial selection and activation | width: 800px; height: 720px; -ck-radial-tile-size: 64px; -ck-radial-tile-background: #073851BB; -ck-radial-ring: #59CEE9; -ck-radial-selected: #E3FCFF; -ck-radial-queued: #FFD17A; -ck-radial-disabled: #F06B6B | items-bind=Powers; selection-bind=Selection; select=SelectPower; activate=QueuePower |
| wheel-center-1 | power-wheel | Yoga-backed Slate column | Content slot within ring | width: 360px; height: 640px; horizontal-align: center | — |
| grow-2 | wheel-center-1 | Yoga-backed Slate column | Flexible spacer | flex-grow: 1 | — |
| target-reticle | wheel-center-1 | retained native vector leaf | Native center reticle | width: 84px; height: 48px; horizontal-align: center | — |
| power-name | wheel-center-1 | Slate text | Bound selected power label | font-family: me-display; max-width: 360px; height: 96px; font-size: 30; color: #D8F9FF; horizontal-align: center; vertical-align: center; text-wrap: wrap | bind=PowerName |
| center-hint-1 | wheel-center-1 | Yoga-backed Slate row | Composed queue hint with shared key caps | width: 360px; height: 30px; gap: 5px; padding: 0px 35px; horizontal-align: center | — |
| center-click-key | center-hint-1 | retained native key-hint control | Pointer activation hint | height: 26px; -ck-key-hint-font-size: 15px; -ck-key-hint-foreground: #DDF7FF; -ck-key-hint-background: #092D3DB8; -ck-key-hint-border-color: #7ED1E8; -ck-key-hint-border-width: 1px; -ck-key-hint-radius: 3px; -ck-key-hint-padding-x: 7px; -ck-key-hint-padding-y: 2px; width: 68px | label=CLICK; font=me-ui |
| center-hint-separator | center-hint-1 | Slate text | Queue hint separator | font-family: me-ui; height: 26px; font-size: 15; color: #A8DAE8; text-wrap: nowrap | literal=/ |
| center-enter-key | center-hint-1 | retained native key-hint control | Keyboard activation hint | height: 26px; -ck-key-hint-font-size: 15px; -ck-key-hint-foreground: #DDF7FF; -ck-key-hint-background: #092D3DB8; -ck-key-hint-border-color: #7ED1E8; -ck-key-hint-border-width: 1px; -ck-key-hint-radius: 3px; -ck-key-hint-padding-x: 7px; -ck-key-hint-padding-y: 2px; width: 74px | label=ENTER; font=me-ui |
| center-hint-action | center-hint-1 | Slate text | Queue hint explanation | font-family: me-ui; height: 26px; font-size: 15; color: #A8DAE8; text-wrap: nowrap | literal=TO QUEUE |
| grow-3 | wheel-center-1 | Yoga-backed Slate column | Flexible spacer | flex-grow: 1 | — |
| instruction-column-1 | wheel-row-1 | Yoga-backed Slate column | Input help | width: 560px; height: 808px; padding: 0px 112px 0px 96px | — |
| grow-4 | instruction-column-1 | Yoga-backed Slate column | Flexible spacer | flex-grow: 1 | — |
| input-label-1 | instruction-column-1 | retained native key-hint control | Passive input key hint | height: 32px; -ck-key-hint-font-size: 18px; -ck-key-hint-foreground: #E6F8FF; -ck-key-hint-background: #092D3DB8; -ck-key-hint-border-color: #80CFE8; -ck-key-hint-border-width: 1px; -ck-key-hint-radius: 3px; -ck-key-hint-padding-x: 8px; -ck-key-hint-padding-y: 3px; width: 88px | label=SHIFT; font=me-ui |
| input-help-1 | instruction-column-1 | Slate text | Literal input explanation | font-family: me-ui; height: 46px; font-size: 18; color: #9EDDEA; text-wrap: wrap | literal=Release to deploy queued powers |
| input-label-2 | instruction-column-1 | retained native key-hint control | Passive input key hint | height: 32px; -ck-key-hint-font-size: 18px; -ck-key-hint-foreground: #E6F8FF; -ck-key-hint-background: #092D3DB8; -ck-key-hint-border-color: #80CFE8; -ck-key-hint-border-width: 1px; -ck-key-hint-radius: 3px; -ck-key-hint-padding-x: 8px; -ck-key-hint-padding-y: 3px; width: 66px | label=ESC; font=me-ui |
| input-help-2 | instruction-column-1 | Slate text | Literal input explanation | font-family: me-ui; height: 46px; font-size: 18; color: #9EDDEA; text-wrap: wrap | literal=Cancel selection |
| input-label-3 | instruction-column-1 | retained native key-hint control | Passive input key hint | height: 32px; -ck-key-hint-font-size: 18px; -ck-key-hint-foreground: #E6F8FF; -ck-key-hint-background: #092D3DB8; -ck-key-hint-border-color: #80CFE8; -ck-key-hint-border-width: 1px; -ck-key-hint-radius: 3px; -ck-key-hint-padding-x: 8px; -ck-key-hint-padding-y: 3px; width: 38px | label=2; font=me-ui |
| input-help-3 | instruction-column-1 | Slate text | Literal input explanation | font-family: me-ui; height: 46px; font-size: 18; color: #9EDDEA; text-wrap: wrap | literal=Toggle target protection |
| instruction-bottom-1 | instruction-column-1 | Yoga-backed Slate column | Help-area spacer | height: 92px | — |
| shepard-status-1 | wheel-layout | Yoga-backed Slate column | Shield, health and queue status | width: 1920px; height: 112px | — |
| shield-meter | shepard-status-1 | retained native vector leaf | Native shield art | width: 360px; height: 28px; horizontal-align: center | — |
| health-meter | shepard-status-1 | retained native vector leaf | Native health art | width: 314px; height: 20px; horizontal-align: center | — |
| shepard-name-1 | shepard-status-1 | Slate text | Literal actor name | font-family: me-ui; height: 30px; font-size: 18; color: #CBF1FC; horizontal-align: center; text-wrap: nowrap | literal=SHEPARD |
| queue-shepard | shepard-status-1 | Slate text | Bound Shepard queue label | font-family: me-ui; height: 34px; font-size: 17; color: #F1D385; horizontal-align: center; text-wrap: nowrap | bind=QueueShepard |
| status-text | wheel-layout | Slate text | Bound host feedback | font-family: me-ui; height: 54px; font-size: 18; color: #C9E8EF; horizontal-align: center; text-wrap: nowrap | bind=StatusText |

The radial collection has a stable key; Text actor, glyph and label; Number angle and cooldown; and Bool enabled and queued. The source connects items-bind=Powers, selection-bind=Selection, select=SelectPower and activate=QueuePower to the host.

Fixture sectors: Shepard 25, 44, 63, 82, 101, 120, 139, 158°; Garrus -178, -158, -138, -118°; Liara -62, -42, -22, -2°. Exact keys, glyph tokens, cooldowns and shield-sensitive flags are recorded in the JSON fixture list.

Blocked powers may be selected for inspection but cannot queue. One slot per actor is replaceable. Release validates the entire queue before returning commands; cancel and rejected release return none. Cooldown advances only while closed, and a shield toggle can invalidate a queued request before release. The model does not activate gameplay abilities.
