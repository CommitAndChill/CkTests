# Skyrim journal authored widget breakdown

The code-created host owns `USafeZone`, a fit-scaled 1920 × 1080 frame, and the authored surface. The source has one `main` region. Its `journal-region` overlay paints a host-bound scene before `journal-layout`; all coordinates below are logical pixels. The [native gallery](native-gallery.md) shows the previous source revision, while `design-contract.json` records the current 74 IDs and parent graph for static validation.

The `journal-art` leaves are passive native drawing, never whole-screen pictures. `frame` supplies the body outline and x≈442 divider; `tab-rail` supplies the header outline; `tab-markers` draws small active-tab pointers from numeric `JournalTabIndex`; `title-ornament` frames the quest title; `footer-line` draws the footer rule. They should be hit-test invisible and cannot replace the real tabs, trees, text, or button.

| ID | Parent | Kind and role | CSS / binding or content |
| --- | --- | --- | --- |
| `journal-region` | `main` | Unsized root overlay | Native mount supplies root geometry; sized backdrop and layout are children. |
| `replica-backdrop` | `journal-region` | Bound scenic image | `.replica-backdrop`: 1920 × 1080; Image `ReplicaBackdrop`. |
| `journal-layout` | `journal-region` | Fixed logical column | `.journal-layout`: 1920 × 1080. |
| `top-gap` | `journal-layout` | Header vertical offset | `.top-gap`: 72 high. |
| `journal-stage-row` | `journal-layout` | Horizontal page placement | `.journal-stage-row`: 1920 × 819. |
| `stage-left-margin` | `journal-stage-row` | Page left offset | `.stage-left-margin`: 246 wide. |
| `journal-stage` | `journal-stage-row` | Art/content overlay | `.journal-stage`: 1428 × 819. |
| `journal-art-stack` | `journal-stage` | Art vertical stack | `.journal-art-stack`: 1428 × 819. |
| `header-art` | `journal-art-stack` | Header-art overlay | `.header-art`: centered 1293 × 66. |
| `tab-rail` | `header-art` | Native art leaf | `.header-art-leaf`: 1293 × 66; `piece=tab-rail`. |
| `tab-markers` | `header-art` | Native active-tab pointer | `.header-art-leaf`: 1293 × 66; `piece=tab-markers`, Number `JournalTabIndex`. |
| `header-body-gap` | `journal-art-stack` | Gap between header/body art | `.header-body-gap`: 15 high. |
| `journal-body-art` | `journal-art-stack` | Body art overlay | `.journal-body-art`: 1428 × 738. |
| `journal-body-smoke` | `journal-body-art` | Translucent body shade | `.journal-body-smoke`: smoky black alpha 0.67, 1428 × 738. |
| `journal-frame` | `journal-body-art` | Native body frame/divider | `.journal-frame`: 1428 × 738; `piece=frame`. |
| `journal-tabs` | `journal-stage` | Real native tab control | `.journal-tabs`: 1428 × 819; three centered 430-wide headers, 25-pixel Barlow Condensed text, transparent native focus outline and zero underline; String `JournalTab` and `SelectJournalTab`. |
| `quests-tab` | `journal-tabs` | Quests tab | Key `quests`, label `QUESTS`. |
| `quests-panel` | `quests-tab` | Tab body column | `.journal-tab-panel`: 1428 × 753. |
| `quests-panel-gap` | `quests-panel` | Header/body gap | `.header-body-gap`: 15 high. |
| `quests-body` | `quests-panel` | Split body row | `.journal-body`: 1428 × 738. |
| `quest-list-pane` | `quests-body` | Left quest column | `.quest-list-pane`: 442 × 738, aligned to art divider. |
| `quest-list-top-gap` | `quest-list-pane` | Normal-list lower placement | `.quest-list-top-gap`: 365 high; long-list stress may require a host/model layout variant. |
| `quest-list` | `quest-list-pane` | Native quest tree | `.quest-tree`: 422 × 330, transparent rows and subtle selection; `Quests`, `SelectQuest`, 46-pixel rows. |
| `quest-row` | `quest-list` | Read-only authored tree row | `.quest-row`: 400 × 46, 6 gap; row data supplies name/marker/color. |
| `quest-row-grow` | `quest-row` | Flexible leading spacer | `.row-grow`: flex grow 1 for right alignment. |
| `quest-row-name` | `quest-row` | Right-aligned quest name | `.quest-row-name`: max 350 wide, 22-pixel text, ellipsis; Text field `name`, Color field `name-color`. |
| `quest-row-marker` | `quest-row` | Selection/active pointer | `.quest-row-marker`: 30 wide, 18-pixel text; Text field `marker`. |
| `quest-details` | `quests-body` | Right detail column | `.quest-details`: 986 × 738 with 16 bottom inset to keep state text clear of the frame. |
| `details-top-gap` | `quest-details` | Detail heading offset | `.details-top-gap`: 8 high. |
| `quest-heading` | `quest-details` | Title/ornament overlay | `.quest-heading`: centered 936 × 112. |
| `quest-ornament` | `quest-heading` | Native symmetric ornament | `.quest-ornament`: 936 × 112; `piece=title-ornament`. |
| `quest-title` | `quest-heading` | Centered uppercase title | `.quest-title`: max 620 wide, 33-pixel text/ellipsis; Text `QuestTitle`. |
| `quest-summary` | `quest-details` | Ordinary-quest description | `.quest-summary`: 900 × 150, 20-pixel wrapped text; Text `QuestSummary`, Visibility `QuestSummaryVisible`. |
| `details-list-gap` | `quest-details` | Heading/list breathing space | `.details-list-gap`: 12 high. |
| `objective-list` | `quest-details` | Native objective tree | `.objective-tree`: 900 wide, flexible 320–522 high; `Objectives`, `SelectObjective`, 52-pixel rows. |
| `objective-row` | `objective-list` | Read-only authored objective row | `.objective-row`: 864 × 52, 12 gap. |
| `objective-row-marker` | `objective-row` | Hollow objective bullet | `.objective-row-marker`: 30 wide, 19-pixel text; Text field `marker`. |
| `objective-row-name` | `objective-row` | Objective description | `.objective-row-name`: 820 × 52, 20-pixel wrapped text; Text field `name`, Color field `name-color`. |
| `quest-state` | `quest-details` | Fixture active-state line | `.quest-state`: 900 × 38, muted 16-pixel text; Text `QuestState`. |
| `stats-tab` | `journal-tabs` | Stats tab | Key `stats`, label `GENERAL STATS`. |
| `stats-panel` | `stats-tab` | Stats tab body column | `.journal-tab-panel`: 1428 × 753. |
| `stats-panel-gap` | `stats-panel` | Header/body gap | `.header-body-gap`: 15 high. |
| `stats-body` | `stats-panel` | Split body row | `.journal-body`: 1428 × 738. |
| `stats-left-pane` | `stats-body` | Empty left frame field | `.quest-list-pane`: 442 × 738, retaining common divider. |
| `stats-details` | `stats-body` | Fixture details column | `.simple-details`: 986 × 738 with inset/padding. |
| `stats-title` | `stats-details` | Section heading | `.simple-title`: 32-pixel text; literal `GENERAL STATS`. |
| `stats-summary` | `stats-details` | Bounded fixture copy | `.simple-summary`: 846 × 390, wrapped 21-pixel text; Text `StatsSummary`. |
| `system-tab` | `journal-tabs` | System tab | Key `system`, label `SYSTEM`. |
| `system-panel` | `system-tab` | System tab body column | `.journal-tab-panel`: 1428 × 753. |
| `system-panel-gap` | `system-panel` | Header/body gap | `.header-body-gap`: 15 high. |
| `system-body` | `system-panel` | Split body row | `.journal-body`: 1428 × 738. |
| `system-left-pane` | `system-body` | Empty left frame field | `.quest-list-pane`: 442 × 738. |
| `system-details` | `system-body` | Fixture details column | `.simple-details`: 986 × 738 with inset/padding. |
| `system-title` | `system-details` | Section heading | `.simple-title`: 32-pixel text; literal `SYSTEM`. |
| `system-summary` | `system-details` | Bounded fixture copy | `.simple-summary`: 846 × 390, wrapped 21-pixel text; Text `SystemSummary`. |
| `journal-close` | `system-details` | Native close button | `.journal-close`: 250 × 58, quiet outline; Action `CloseJournal`, literal `Close Journal`. |
| `footer-top-gap` | `journal-layout` | Footer rule offset | `.footer-top-gap`: 54 high. |
| `footer-line` | `journal-layout` | Native hairline art | `.footer-line`: centered 1648 × 2; `piece=footer-line`. |
| `footer-text-gap` | `journal-layout` | Rule/text gap | `.footer-text-gap`: 36 high. |
| `journal-footer` | `journal-layout` | Footer text row | `.journal-footer`: 1920 × 58, 136 horizontal inset. |
| `footer-keys` | `journal-footer` | Quest-only hint row | `.footer-keys`: 520 wide; Visibility `QuestFooterVisible`. |
| `footer-enter-key`, `footer-map-key` | `footer-keys` | Passive shared key hints | Literal `ENTER` and `M`, with 31-high centered text labels `footer-toggle-label` and `footer-map-label`. |
| `footer-category-keys` | `journal-footer` | Other-tab hint row | `.footer-keys`: 520 wide; Visibility `CategoryFooterVisible`. |
| `footer-previous-key`, `footer-next-key`, `footer-close-key` | `footer-category-keys` | Passive shared key hints | Literal `Q`, `E`, `ESC`; 31-high centered labels `footer-category-label` and `footer-close-label`. |
| `footer-status` | `journal-footer` | Fixture response/status | `.footer-status`: max 450 wide, 30 high, 17-pixel ellipsis; Text `FooterStatus`. |
| `footer-grow` | `journal-footer` | Date push spacer | `.footer-grow`: flex grow 1. |
| `footer-date` | `journal-footer` | Right-aligned in-game date | `.footer-date`: max 600 wide, 31 high, 18-pixel text; Text `FooterDate`. |

The tree rows are display-only; native tree selection handles input and publishes the selected key. `name`, `marker`, and `name-color` are required per-row fields (Text, Text, Color). Quests and objectives remain separate native trees because the reference's left navigation and right Miscellaneous objective list have different selection roles. The host requests scroll-into-view only for a newly accepted quest or objective selection, so selecting Miscellaneous can reveal its row without repeatedly overriding manual scrolling. Missing, filtered, or unchanged keys cause no scroll request. Footer hints reflect the current tab; quest map-target feedback is hidden under Stats and System.

The host loads licensed Barlow Condensed and a scenic image through `ck_ui_replica_presentation::TryLoad`, then publishes `skyrim-ui` and `skyrim-display` with the engine Droid Sans fallback. Every text-bearing class selects its family explicitly; CSS has no inheritance or file imports. The shared `key-hint` is registered before the immutable registry snapshot. Native captures showed the smoky scene and aligned 30–31-high footer text, but the Stats tab still exposed default blue focus chrome and the Miscellaneous row remained offscreen. Transparent tab focus styling and opt-in deferred selection scrolling now address those two findings; the fresh [34/34 gate and native gallery](../ReplicaRemakes/README.md) confirm the selected row is fully visible. Default scrollbar styling, fixed-height row limits and objective wrapping remain fidelity/authoring gaps.
