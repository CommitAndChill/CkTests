# Power wheel widget breakdown

This is a source-derived inventory of all 53 authored IDs in the [design contract](design-contract.json). Sizes are logical layout units before ScaleToFit. The final focused native gate passed 10/10; [captures and measured text-fit evidence](verification.md) cover 720p, 1080p and ultrawide. The sizes below remain source-derived logical dimensions.

The code-created host mounts an unsized wheel-region root around the fixed 1920 × 1080 wheel-layout child. The retained selector is authored at 800 × 720, with local center (400, 360), radius 280.8 and 56-unit tiles. Angles increase clockwise from screen-right. Six broken arcs, chamfered tile frames, token-specific native glyphs, selected and queued rims, and normalized cooldown overlays are source-defined drawing rules, not capture measurements. The selector suppresses Slate's widget-wide focus rectangle; the selected glyph frame remains the focus cue. Center power name and hint now use natural text widths capped at 360 logical units.

| ID | Parent | Native node | Role | Authored CSS | Binding / literal |
| --- | --- | --- | --- | --- | --- |
| wheel-region | main region | Yoga-backed Slate column | Unsized main-region mount root; fixed logical dimensions belong to wheel-layout child | no explicit CSS | — |
| wheel-layout | wheel-region | Yoga-backed Slate column | Logical composition root | width: 1920px; height: 1080px | — |
| target-row-1 | wheel-layout | Yoga-backed Slate column | Target identity and protection strips | width: 1920px; height: 106px; padding: 24px 600px 0px | — |
| target-state | target-row-1 | Slate text | Live target name/protection | height: 36px; font-size: 20; color: #BCEFFF; horizontal-align: center; text-wrap: nowrap | bind=TargetState |
| target-bars-1 | target-row-1 | Yoga-backed Slate column | Stacked shield/armor strips | width: 500px; height: 13px; horizontal-align: center; gap: 3px | — |
| target-shield-1 | target-bars-1 | Yoga-backed Slate column | Conditional shield strip | width: 500px; height: 5px; background-color: #65C8EC | visible=TargetShielded |
| target-armor-1 | target-bars-1 | Yoga-backed Slate column | Armor strip | width: 500px; height: 5px; background-color: #C68F64 | — |
| wheel-row-1 | wheel-layout | Yoga-backed Slate row | Description, selector, instruction body | width: 1920px; height: 808px | — |
| description-column-1 | wheel-row-1 | Yoga-backed Slate column | Selected-power details | width: 560px; height: 808px; padding: 0px 32px 0px 112px | — |
| grow-1 | description-column-1 | Yoga-backed Slate column | Flexible spacer | flex-grow: 1 | — |
| actor-name | description-column-1 | Slate text | Selected actor | height: 34px; font-size: 18; color: #D5F6FF; text-wrap: nowrap | bind=ActorName |
| power-description | description-column-1 | Slate text | Selected power description | width: 416px; height: 124px; font-size: 23; color: #80E9FF; text-wrap: wrap | bind=PowerDescription |
| power-state | description-column-1 | Slate text | Ready, queued or blocked reason | width: 416px; height: 68px; font-size: 17; color: #DAF5FF; text-wrap: wrap | bind=PowerState |
| description-bottom-1 | description-column-1 | Yoga-backed Slate column | Detail-area spacer | height: 112px | — |
| wheel-column-1 | wheel-row-1 | Yoga-backed Slate column | Squad portraits and selector | width: 800px; height: 808px | — |
| squad-row-1 | wheel-column-1 | Yoga-backed Slate row | Two squadmate summaries | width: 800px; height: 88px; padding: 0px 6px | — |
| squad-member-1 | squad-row-1 | Yoga-backed Slate column | Squadmate summary | width: 350px; height: 86px | — |
| portrait-row-1 | squad-member-1 | Yoga-backed Slate row | Portrait and labels | width: 350px; height: 76px; gap: 12px | — |
| garrus-portrait-frame | portrait-row-1 | Yoga-backed Slate column | Framed portrait container; owns border, background and inset | width: 70px; height: 70px; padding: 2px; background-color: #09232B; border-color: #64D4F1; border-width: 1px; border-radius: 9px | — |
| garrus-portrait | garrus-portrait-frame | Slate image | Bound squadmate atlas image | width: 66px; height: 66px | bind=GarrusPortrait |
| portrait-label-1 | portrait-row-1 | Yoga-backed Slate column | Name and queue label | width: 264px; height: 76px; padding: 6px 0px | — |
| squad-name-1 | portrait-label-1 | Slate text | Literal squadmate name | height: 30px; font-size: 18; color: #BAEDFA; text-wrap: nowrap | literal=GARRUS |
| queue-garrus | portrait-label-1 | Slate text | Bound queued-power label | height: 40px; font-size: 15; color: #F1D385; text-wrap: wrap | bind=QueueGarrus |
| squad-gap-1 | squad-row-1 | Yoga-backed Slate column | Flexible squad separation | flex-grow: 1 | — |
| squad-member-2 | squad-row-1 | Yoga-backed Slate column | Squadmate summary | width: 350px; height: 86px | — |
| portrait-row-2 | squad-member-2 | Yoga-backed Slate row | Portrait and labels | width: 350px; height: 76px; gap: 12px | — |
| portrait-label-2 | portrait-row-2 | Yoga-backed Slate column | Name and queue label | width: 264px; height: 76px; padding: 6px 0px | — |
| squad-name-2 | portrait-label-2 | Slate text | Literal squadmate name | height: 30px; font-size: 18; color: #BAEDFA; text-wrap: nowrap | literal=LIARA |
| queue-liara | portrait-label-2 | Slate text | Bound queued-power label | height: 40px; font-size: 15; color: #F1D385; text-wrap: wrap | bind=QueueLiara |
| liara-portrait-frame | portrait-row-2 | Yoga-backed Slate column | Framed portrait container; owns border, background and inset | width: 70px; height: 70px; padding: 2px; background-color: #09232B; border-color: #64D4F1; border-width: 1px; border-radius: 9px | — |
| liara-portrait | liara-portrait-frame | Slate image | Bound squadmate atlas image | width: 66px; height: 66px | bind=LiaraPortrait |
| power-wheel | wheel-column-1 | SCkUiRadialSelector retained native control | Native radial selection and activation | width: 800px; height: 720px; -ck-radial-tile-size: 56px; -ck-radial-ring: #48BEDB; -ck-radial-selected: #C5F9FF; -ck-radial-queued: #FFCC66; -ck-radial-disabled: #E65B58 | items-bind=Powers, selection-bind=Selection, select=SelectPower, activate=QueuePower |
| wheel-center-1 | power-wheel | Yoga-backed Slate column | Content slot within ring | width: 360px; height: 640px; horizontal-align: center | — |
| grow-2 | wheel-center-1 | Yoga-backed Slate column | Flexible spacer | flex-grow: 1 | — |
| target-reticle | wheel-center-1 | retained native vector leaf | Native center reticle | width: 84px; height: 48px; horizontal-align: center | piece=me-reticle |
| power-name | wheel-center-1 | Slate text | Bound selected power label | max-width: 360px; height: 96px; font-size: 24; color: #90EDFF; horizontal-align: center; vertical-align: center; text-wrap: wrap | bind=PowerName |
| center-hint-1 | wheel-center-1 | Slate text | Literal queue hint | max-width: 360px; height: 28px; font-size: 14; color: #A5DCE8; horizontal-align: center; text-wrap: nowrap | literal=CLICK / ENTER TO QUEUE |
| grow-3 | wheel-center-1 | Yoga-backed Slate column | Flexible spacer | flex-grow: 1 | — |
| instruction-column-1 | wheel-row-1 | Yoga-backed Slate column | Input help | width: 560px; height: 808px; padding: 0px 112px 0px 96px | — |
| grow-4 | instruction-column-1 | Yoga-backed Slate column | Flexible spacer | flex-grow: 1 | — |
| input-label-1 | instruction-column-1 | Slate text | Literal input key | height: 32px; font-size: 18; color: #CFEEF8; text-wrap: nowrap | literal=RELEASE SHIFT |
| input-help-1 | instruction-column-1 | Slate text | Literal input explanation | height: 46px; font-size: 17; color: #73CBE0; text-wrap: wrap | literal=Deploy queued powers |
| input-label-2 | instruction-column-1 | Slate text | Literal input key | height: 32px; font-size: 18; color: #CFEEF8; text-wrap: nowrap | literal=ESC |
| input-help-2 | instruction-column-1 | Slate text | Literal input explanation | height: 46px; font-size: 17; color: #73CBE0; text-wrap: wrap | literal=Cancel selection |
| input-label-3 | instruction-column-1 | Slate text | Literal input key | height: 32px; font-size: 18; color: #CFEEF8; text-wrap: nowrap | literal=2 |
| input-help-3 | instruction-column-1 | Slate text | Literal input explanation | height: 46px; font-size: 17; color: #73CBE0; text-wrap: wrap | literal=Toggle target protection |
| instruction-bottom-1 | instruction-column-1 | Yoga-backed Slate column | Help-area spacer | height: 92px | — |
| shepard-status-1 | wheel-layout | Yoga-backed Slate column | Shield, health and queue status | width: 1920px; height: 112px | — |
| shield-meter | shepard-status-1 | retained native vector leaf | Native shield art | width: 360px; height: 28px; horizontal-align: center | piece=me-shield |
| health-meter | shepard-status-1 | retained native vector leaf | Native health art | width: 314px; height: 20px; horizontal-align: center | piece=me-health |
| shepard-name-1 | shepard-status-1 | Slate text | Literal actor name | height: 30px; font-size: 16; color: #9AEAFF; horizontal-align: center; text-wrap: nowrap | literal=SHEPARD |
| queue-shepard | shepard-status-1 | Slate text | Bound Shepard queue label | height: 34px; font-size: 17; color: #F1D385; horizontal-align: center; text-wrap: nowrap | bind=QueueShepard |
| status-text | wheel-layout | Slate text | Bound host feedback | height: 54px; font-size: 16; color: #B6D4DC; horizontal-align: center; text-wrap: nowrap | bind=StatusText |

The radial collection has a stable key; Text actor, glyph and label; Number angle and cooldown; and Bool enabled and queued. The source connects items-bind=Powers, selection-bind=Selection, select=SelectPower and activate=QueuePower to the host.

Fixture sectors: Shepard 25, 44, 63, 82, 101, 120, 139, 158°; Garrus -178, -158, -138, -118°; Liara -62, -42, -22, -2°. Exact keys, glyph tokens, cooldowns and shield-sensitive flags are recorded in the JSON fixture list.

Blocked powers may be selected for inspection but cannot queue. One slot per actor is replaceable. Release validates the entire queue before returning commands; cancel and rejected release return none. Cooldown advances only while closed, and a shield toggle can invalidate a queued request before release. The model does not activate gameplay abilities.
