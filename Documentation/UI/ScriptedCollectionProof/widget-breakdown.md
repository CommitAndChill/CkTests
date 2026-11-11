# Scripted Collection Proof: widget breakdown

Status: **source checked; focused runtime verified**. This document describes the authored HTML/CSS and the AngelScript gym source. The script-created UMG `SizeBox` declares a 1920 × 1080 logical canvas. The authored `main` root has no CSS width or height declaration; root geometry is owned by the native mount. These are source declarations, not measured runtime bounds. The generated reference image is illustrative design input, not a runtime capture.

## Authored UI tree

The table lists all 50 authored IDs. Parent IDs follow XML nesting. “Main” nodes belong to region `main`; “Item” nodes belong to the local template under `missions` and repeat for collection records. Sizes, padding, flex grow/shrink, typography, and button styling are direct declarations from the matching CSS class; blank means no corresponding declaration. Inherited/effective values and runtime geometry are not asserted.

| ID | Parent | Type / scope | Role, binding or action | Declared size | Padding | Typography / style |
|---|---|---|---|---|---|---|
| `mission-board` | — | column · Main | Board root; native mount owns root geometry | — | 64px 80px 40px 80px | — |
| `board-header` | `mission-board` | row · Main | Header | — × 140px | — | — |
| `brand` | `board-header` | column · Main | Brand group; flex-grow 1 | — | — | — |
| `board-kicker` | `brand` | text · Main | “TERMINUS / OPERATIONS COMMAND” | — × 34px | — | 22px, #6ECFE6 |
| `board-title` | `brand` | text · Main | “MISSION BOARD” | — × 92px | — | 56px bold, #EDF5F8 |
| `connection` | `board-header` | column · Main | Connection group | 380px × — | 18px 0 0 0 | — |
| `board-online` | `connection` | text · Main | “UPLINK ESTABLISHED” | — × 38px | — | 22px, #79DDB8 |
| `board-status` | `connection` | text · Main | Text `BoardStatus` | — × 38px | — | 20px, #809AAD |
| `board-rule` | `mission-board` | column · Main | Divider | — × 2px | — | — |
| `board-body` | `mission-board` | row · Main | Feed and selected detail | — × 770px | 34px 0 0 0; gap 36px | — |
| `mission-feed` | `board-body` | column · Main | Mission collection feed | 920px × — | — | — |
| `feed-heading` | `mission-feed` | row · Main | Feed heading | — × 56px | — | — |
| `feed-label` | `feed-heading` | text · Main | “ACTIVE OPERATIONS”; flex-grow 1 | — | — | 24px bold, #D9E9EF |
| `mission-count` | `feed-heading` | text · Main | Text `MissionCount` | 230px × — | — | 21px, #6ECFE6 |
| `missions` | `mission-feed` | repeat · Main | Collection `Missions`, vertical; flex-grow 1 | — | — | — |
| `mission-row` | `missions` | column · Item | Repeated record card | — × 148px | 12px 24px 12px 24px | card background/border declared |
| `mission-select` | `mission-row` | button · Item | Text `TitleText`; color `AccentColor`; item action `SelectMission` | — × 54px | button x=10px, y=0 | 29px bold; left/center; declared hover/focus/pressed styles |
| `mission-route-wrap` | `mission-row` | column · Item | Route text wrapper | — × 30px | 0 10px 0 10px | — |
| `mission-route` | `mission-route-wrap` | text · Item | Text `RouteText` | — × 30px | — | 19px, #9CB2C1 |
| `mission-meta` | `mission-row` | row · Item | Status row | — × 30px | 0 10px 0 10px | — |
| `mission-status` | `mission-meta` | text · Item | Text `StatusText`; flex-grow 1 | — | — | 18px, #728E9F |
| `mission-done` | `mission-meta` | text · Item | Visible on `DoneBool`; “COMPLETED” | 160px × — | — | 18px bold, #79DDB8 |
| `selected-panel` | `board-body` | column · Main | Selected mission details; flex-grow 1, flex-shrink 1 | — | 32px | panel background/border declared |
| `selection-label` | `selected-panel` | text · Main | Text `Selection` | — × 34px | — | 21px bold, #F6B65E |
| `detail-title` | `selected-panel` | text · Main | Text `DetailTitle` | — × 62px | — | 37px bold, #EDF5F8 |
| `detail-route` | `selected-panel` | text · Main | Text `DetailRoute` | — × 36px | — | 21px, #9CB2C1 |
| `route-map` | `selected-panel` | column · Main | Source-authored route diagram | — × 96px | 14px 18px 10px 18px | background declared |
| `route-nodes` | `route-map` | row · Main | Route markers and connectors | — × 40px | 0 76px 0 76px | — |
| `route-home` | `route-nodes` | column · Main | Origin marker | 34px × 34px | — | marker background/border declared |
| `route-line-a` | `route-nodes` | column · Main | First connector; flex-grow 1 | — × 2px | — | connector color declared |
| `route-objective` | `route-nodes` | column · Main | Objective marker | 38px × 38px | — | objective background/border declared |
| `route-line-b` | `route-nodes` | column · Main | Second connector; flex-grow 1 | — × 2px | — | connector color declared |
| `route-next` | `route-nodes` | column · Main | Next-jump marker | 34px × 34px | — | marker background/border declared |
| `route-labels` | `route-map` | row · Main | Marker captions | — × 32px | — | — |
| `route-home-label` | `route-labels` | text · Main | “TERMINUS”; flex-grow 1 | — | — | 17px, #6ECFE6 |
| `route-objective-label` | `route-labels` | text · Main | “OBJECTIVE”; flex-grow 1 | — | — | 17px, #6ECFE6 |
| `route-next-label` | `route-labels` | text · Main | “NEXT JUMP”; flex-grow 1 | — | — | 17px, #6ECFE6 |
| `detail-rule` | `selected-panel` | column · Main | Divider | — × 2px | — | — |
| `briefing-label` | `selected-panel` | text · Main | “MISSION BRIEFING” | — × 34px | — | 19px, #6ECFE6 |
| `detail-brief` | `selected-panel` | text · Main | Text `DetailBrief` | — × 90px | — | 25px, #CDDFE8; wraps |
| `detail-spacer` | `selected-panel` | column · Main | Flexible spacer; flex-grow 1 | — | — | — |
| `telemetry` | `selected-panel` | column · Main | Mission telemetry panel | — × 126px | 10px 18px 10px 18px | background/border declared |
| `progress-label` | `telemetry` | text · Main | “OBJECTIVE TELEMETRY” | — × 30px | — | 17px, #809AAD |
| `detail-progress` | `telemetry` | text · Main | Text `DetailProgress` | — × 40px | — | 24px, #D9E9EF |
| `detail-status` | `telemetry` | text · Main | Text `DetailStatus` | — × 36px | — | 20px bold, #F6B65E |
| `mission-complete` | `selected-panel` | button · Main | Action `CompleteMission`; “COMPLETE OBJECTIVE” | — × 60px | button x=20px, y=0 | 25px bold; centered; declared hover/focus/pressed styles |
| `detail-hint` | `selected-panel` | text · Main | “Confirm when the objective has been secured.” | — × 30px | — | 18px, #809AAD |
| `board-footer` | `mission-board` | row · Main | Footer | — × 64px | — | — |
| `footer-brand` | `board-footer` | text · Main | “TERMINUS / EXPEDITION NETWORK”; flex-grow 1 | — | — | 18px, #678394 |
| `footer-code` | `board-footer` | text · Main | “SECURE CHANNEL  //  07.19” | 380px × — | — | 18px, #678394 |

The selected panel’s fixed child declarations occupy 670px within its 672px content height; the remaining 2px is the layout allowance. This source-derived fit accounts for the selection label, route and briefing sections, telemetry, completion control, and hint.

## Scripted collection contract

The script registers collection `Missions` and seven required fields:

| Field | Kind | UI use |
|---|---|---|
| `TitleText` | Text | Repeated selection button label |
| `StatusText` | Text | Repeated status line |
| `RouteText` | Text | Repeated route line |
| `DoneBool` | Bool | Repeated completion visibility |
| `AccentColor` | Color | Repeated selection button color |
| `PriorityInteger` | Integer | Record data, formatted into selected detail text |
| `ProgressNumber` | Number | Record data, formatted into selected detail text |

The four stable record keys seeded by AngelScript are `relay`, `salvage`, `distress`, and `survey`. The declared event names are item action `SelectMission` and ordinary action `CompleteMission`. The script also registers text keys `MissionCount`, `BoardStatus`, `Selection`, `DetailTitle`, `DetailRoute`, `DetailBrief`, `DetailProgress`, and `DetailStatus`.

The gym controller handles keys 1–6 through its control panel: reverse mission order, remove selected mission, reinsert removed key, reset missions, reload HTML/CSS, and reject invalid data and source. Selection and completion counters are exposed by the widget. The separate gym panel reports selected key and source acceptance/error.

## Separate UMG shell objects

These are constructed around the mounted authored surface in AngelScript and are not among the 50 markup IDs:

| Object | Parent | Source-declared setup |
|---|---|---|
| `MissionSurface` (`UCk_UI_AuthoredSurface_UE`) | `MissionFrame` | Receives the mounted `main` region |
| `MissionBackdrop` (`UImage`) | `MissionBackdropScale` | Uses imported `mission-board-backdrop.png` texture |
| `MissionBackdropScale` (`UScaleBox`) | `MissionOverlay` | ScaleToFill, clip to bounds, hit-test invisible |
| `MissionFrame` (`USizeBox`) | `MissionScale` | Width 1920, height 1080 |
| `MissionScale` (`UScaleBox`) | `MissionOverlay` | ScaleToFit |
| `MissionOverlay` (`UOverlay`, root) | Viewport via `SetRootWidget` | Backdrop and content each assigned fill alignment |

## Fidelity boundary

The collection schema is flat and exposes Text, Number, Bool, Integer, and Color. Image fields, nested collections, and controller/focus interaction within repeated items are not exposed by this proof. The backdrop is a shell-level texture, not a collection Image field. The authored route diagram uses native UI elements. No runtime capture, measured bounds, or runtime validation result is claimed here.
