# Widget and behavior breakdown

All visible content is authored in [HTML](../../../Resources/UiAuthoring/ScriptingCardProof/ScriptingCardProof.ui.html) and [CSS](../../../Resources/UiAuthoring/ScriptingCardProof/ScriptingCardProof.ui.css). The root UMG hierarchy is created in [AngelScript](../../../Script/CkUiAuthoring/CkScriptingCard_Gym.as): `UUserWidget -> UScaleBox -> USizeBox -> UCk_UI_AuthoredSurface_UE`. The SizeBox provides a 1920x1080 design space; ScaleBox fits it into the viewport. The centered card measures 820x824 design units, with 42-unit panel padding. No Widget Blueprint or new binary asset is used.

| HTML ID | Element / CSS class | Data or purpose |
| --- | --- | --- |
| `main` | Region | The one accepted region mounted into the UMG surface |
| `card-composition` | column / composition | Full-height 1080 design space |
| `card-top-space` | column / outer-space | Flexible centering space |
| `card-panel` | column / panel | Dark translucent panel, cyan/steel frame |
| `card-header` | row / header | Equipment manufacturer and unit |
| `card-kicker` | text / kicker | Static HELIOS / ARMORY label |
| `card-unit` | text / unit | Static MK. VII identifier |
| `card-title` | text / title | Static PULSE CARBINE name |
| `card-subtitle` | text / subtitle | Static operator prompt |
| `card-divider` | column / divider | Two-unit visual separator |
| `card-telemetry` | row / telemetry | Counter and status layout |
| `card-shot-readout` | column / readout | Fixed-width counter group |
| `card-shot-label` | text / readout-label | Static ROUNDS FIRED label |
| `card-shot-value` | text / shot-value | Text binding `ShotCount`, starts at `0` |
| `card-state-readout` | column / state-readout | Remaining width for bounded status copy |
| `card-state-label` | text / readout-label | Static SYSTEM STATUS label |
| `card-status` | text / status | Text binding `Status`, starts at `READY` |
| `card-charge-header` | row / charge-header | Charge name and accepted value |
| `card-charge-label` | text / field-label | Static CAPACITOR CHARGE label |
| `card-charge-value` | text / field-value | Text binding `ChargeLabel`, starts at `65%` |
| `card-charge` | slider / charge-slider | Number binding `Charge`, range 0-100, step 1; `interaction=Charge` proposes a commit |
| `card-safety-row` | row / safety-row | Interlock explanation and control |
| `card-safety-copy` | column / safety-copy | Flexible label/helper group |
| `card-safety-label` | text / field-label | Static SAFETY INTERLOCK label |
| `card-safety-hint` | text / helper | Static disengagement hint |
| `card-safety` | checkbox / safety-checkbox | Boolean binding `Safety`, initially true; `changed=Safety` proposes an edit |
| `card-fire` | button / fire-button | Declared action `Fire`; explicit normal/hover/focus/pressed/disabled styles |
| `card-footer` | text / footer | Static training-link label |
| `card-bottom-space` | column / outer-space | Flexible centering space |

| Design allocation | Units / typography |
| --- | --- |
| Panel | 820x824, padding 42; its fixed children total 734 units before the panel's remaining space |
| Header / title / subtitle / divider | Heights 30 / 80 / 44 / 2; text point sizes 19 / 44 bold / 21 |
| Telemetry | Height 162, top/bottom padding 26/12; counter column width 252, status uses remaining width |
| Telemetry labels / shot value / status | Heights 30 / 88 / 64; text point sizes 18 / 52 bold / 31 bold |
| Charge label row / slider | Heights 44 / 42; value column width 112; slider width 736, thumb 24x28 |
| Safety row | Height 102, top/bottom padding 20/16; checkbox 44x44, glyph size 40 |
| Fire / footer | Heights 72 / 56; text point sizes 25 bold / 16 |

The grammar accepts `px` on font-size values but passes the number to Slate's
point-size field. These allocations account for the full native line box rather
than assuming a 44-unit font needs only 44 units of row height. The final runtime
gate must establish fit; the design arithmetic alone is not visual evidence.

| Event or command | Script policy | Accepted visible state |
| --- | --- | --- |
| Slider commit | Clamp proposal to 0-100, call `TrySetNumber`, then update text | Charge percentage and thumb reflect the accepted value |
| Checkbox edit | Accept through `TrySetBool` | `SAFETY LOCK` when checked, `ARMED` when unchecked |
| Fire with safety on | Record action, leave shot count unchanged | `SAFETY LOCK` |
| Fire with safety off | Record action and increment fixture shots | Updated shot counter and `SHOT FIRED` |
| `1` reset | Reset script counters and accepted scalar values | Initial charge, safety and status |
| `2` reload | Read both authored files through shared native transaction | New document revision, existing model values retained |
| `3` reject candidate | Request an undeclared text binding | Existing revision and mounted content retained; info panel shows diagnostics |
| Widget destruction / controller EndPlay | Retire session before detaching UMG | Held native widgets lose dispatch authority |

The session freezes declarations after successful initialization. A failed declaration set publishes no model subset or view, and can be retried. Successful initialization is one-shot; release is terminal, including release before initialization. Names use the native parser grammar and are case-sensitive; names are unique across text, number, boolean and action declarations. Setters reject unknown names, incorrect types and nonfinite numeric values without changing accepted state. Rejected getters preserve their output arguments.

One session can mount into one surface during its lifetime. Entry rejection preserves the current surface. A surface replacement retires the displaced view; subsequent operations on its session fail. Session release retires only its own native view, so it cannot clear a newer surface owner. Native attributes and callbacks hold the session weakly; the script owns the session and surface through UObject properties.

The proof deliberately uses bounded English fixture copy. A green text-bounds check at three viewport sizes does not establish arbitrary localization, long unbounded counters or accessibility scaling. Native focus/navigation remains a follow-up alongside shared menu ownership.
