# Skyrim journal authored widget breakdown

The code-created host owns the scene dimmer, `USafeZone`, fit-scaled 1920 × 1080 frame, and the authored surface. The source has one `main` region. Its unsized `journal-region` wraps `journal-layout`; all coordinates below are logical pixels. The [native gallery](native-gallery.md) shows the actual viewport results. `SkyrimJournalProof.ui.css` defines the named classes in this inventory, and `design-contract.json` records the same IDs and parent graph for static validation.

The `journal-art` leaves are passive native drawing, never whole-screen pictures. `frame` supplies the body outline and x≈442 divider; `tab-rail` supplies the header outline; `tab-markers` draws small active-tab pointers from numeric `JournalTabIndex`; `title-ornament` frames the quest title; `footer-line` draws the footer rule. They should be hit-test invisible and cannot replace the real tabs, trees, text, or button.

| ID | Parent | Kind and role | CSS / binding or content |
| --- | --- | --- | --- |
| `journal-region` | `main` | Unsized root column | No class; host mount. |
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
| `journal-frame` | `journal-art-stack` | Native body frame/divider | `.journal-frame`: 1428 × 738; `piece=frame`. |
| `journal-tabs` | `journal-stage` | Real native tab control | `.journal-tabs`: 1428 × 819; three centered 430-wide headers, 30-pixel white/gray text, zero underline; String `JournalTab` and `SelectJournalTab`. |
| `quests-tab` | `journal-tabs` | Quests tab | Key `quests`, label `QUESTS`. |
| `quests-panel` | `quests-tab` | Tab body column | `.journal-tab-panel`: 1428 × 753. |
| `quests-panel-gap` | `quests-panel` | Header/body gap | `.header-body-gap`: 15 high. |
| `quests-body` | `quests-panel` | Split body row | `.journal-body`: 1428 × 738. |
| `quest-list-pane` | `quests-body` | Left quest column | `.quest-list-pane`: 442 × 738, aligned to art divider. |
| `quest-list-top-gap` | `quest-list-pane` | Normal-list lower placement | `.quest-list-top-gap`: 365 high; long-list stress may require a host/model layout variant. |
| `quest-list` | `quest-list-pane` | Native quest tree | `.quest-tree`: 422 × 330, transparent rows and subtle selection; `Quests`, `SelectQuest`, 52-pixel rows. |
| `quest-row` | `quest-list` | Read-only authored tree row | `.quest-row`: 400 × 52, 6 gap; row data supplies name/marker/color. |
| `quest-row-grow` | `quest-row` | Flexible leading spacer | `.row-grow`: flex grow 1 for right alignment. |
| `quest-row-name` | `quest-row` | Right-aligned quest name | `.quest-row-name`: max 350 wide, 28-pixel text, ellipsis; Text field `name`, Color field `name-color`. |
| `quest-row-marker` | `quest-row` | Selection/active pointer | `.quest-row-marker`: 30 wide, 22-pixel text; Text field `marker`. |
| `quest-details` | `quests-body` | Right detail column | `.quest-details`: 986 × 738 with 16 bottom inset to keep state text clear of the frame. |
| `details-top-gap` | `quest-details` | Detail heading offset | `.details-top-gap`: 8 high. |
| `quest-heading` | `quest-details` | Title/ornament overlay | `.quest-heading`: centered 936 × 112. |
| `quest-ornament` | `quest-heading` | Native symmetric ornament | `.quest-ornament`: 936 × 112; `piece=title-ornament`. |
| `quest-title` | `quest-heading` | Centered uppercase title | `.quest-title`: max 620 wide, 42-pixel text/ellipsis; Text `QuestTitle`. |
| `quest-summary` | `quest-details` | Ordinary-quest description | `.quest-summary`: 900 × 150, 26-pixel wrapped text; Text `QuestSummary`, Visibility `QuestSummaryVisible`. |
| `details-list-gap` | `quest-details` | Heading/list breathing space | `.details-list-gap`: 12 high. |
| `objective-list` | `quest-details` | Native objective tree | `.objective-tree`: 900 wide, flexible 320–522 high; `Objectives`, `SelectObjective`, 58-pixel rows. |
| `objective-row` | `objective-list` | Read-only authored objective row | `.objective-row`: 864 × 58, 12 gap. |
| `objective-row-marker` | `objective-row` | Hollow objective bullet | `.objective-row-marker`: 30 wide, 24-pixel text; Text field `marker`. |
| `objective-row-name` | `objective-row` | Objective description | `.objective-row-name`: 820 × 58, 26-pixel wrapped text; Text field `name`, Color field `name-color`. |
| `quest-state` | `quest-details` | Fixture active-state line | `.quest-state`: 900 × 38, muted 18-pixel text; Text `QuestState`. |
| `stats-tab` | `journal-tabs` | Stats tab | Key `stats`, label `GENERAL STATS`. |
| `stats-panel` | `stats-tab` | Stats tab body column | `.journal-tab-panel`: 1428 × 753. |
| `stats-panel-gap` | `stats-panel` | Header/body gap | `.header-body-gap`: 15 high. |
| `stats-body` | `stats-panel` | Split body row | `.journal-body`: 1428 × 738. |
| `stats-left-pane` | `stats-body` | Empty left frame field | `.quest-list-pane`: 442 × 738, retaining common divider. |
| `stats-details` | `stats-body` | Fixture details column | `.simple-details`: 986 × 738 with inset/padding. |
| `stats-title` | `stats-details` | Section heading | `.simple-title`: 40-pixel text; literal `GENERAL STATS`. |
| `stats-summary` | `stats-details` | Bounded fixture copy | `.simple-summary`: 846 × 390, wrapped 27-pixel text; Text `StatsSummary`. |
| `system-tab` | `journal-tabs` | System tab | Key `system`, label `SYSTEM`. |
| `system-panel` | `system-tab` | System tab body column | `.journal-tab-panel`: 1428 × 753. |
| `system-panel-gap` | `system-panel` | Header/body gap | `.header-body-gap`: 15 high. |
| `system-body` | `system-panel` | Split body row | `.journal-body`: 1428 × 738. |
| `system-left-pane` | `system-body` | Empty left frame field | `.quest-list-pane`: 442 × 738. |
| `system-details` | `system-body` | Fixture details column | `.simple-details`: 986 × 738 with inset/padding. |
| `system-title` | `system-details` | Section heading | `.simple-title`: 40-pixel text; literal `SYSTEM`. |
| `system-summary` | `system-details` | Bounded fixture copy | `.simple-summary`: 846 × 390, wrapped 27-pixel text; Text `SystemSummary`. |
| `journal-close` | `system-details` | Native close button | `.journal-close`: 250 × 58, quiet outline; Action `CloseJournal`, literal `Close Journal`. |
| `footer-top-gap` | `journal-layout` | Footer rule offset | `.footer-top-gap`: 54 high. |
| `footer-line` | `journal-layout` | Native hairline art | `.footer-line`: centered 1648 × 2; `piece=footer-line`. |
| `footer-text-gap` | `journal-layout` | Rule/text gap | `.footer-text-gap`: 36 high. |
| `journal-footer` | `journal-layout` | Footer text row | `.journal-footer`: 1920 × 58, 136 horizontal inset. |
| `footer-keys` | `journal-footer` | Tab-specific input hints | `.footer-keys`: 520 wide, 24-pixel text; Text `FooterKeys` selects quest actions or category/close hints. |
| `footer-status` | `journal-footer` | Fixture response/status | `.footer-status`: max 450 wide, 20-pixel ellipsis; Text `FooterStatus`. |
| `footer-grow` | `journal-footer` | Date push spacer | `.footer-grow`: flex grow 1. |
| `footer-date` | `journal-footer` | Right-aligned in-game date | `.footer-date`: max 600 wide, 23-pixel text; Text `FooterDate`. |

The tree rows are display-only; native tree selection handles input and publishes the selected key. `name`, `marker`, and `name-color` are required per-row fields (Text, Text, Color). Quests and objectives remain separate native trees because the reference's left navigation and right Miscellaneous objective list have different selection roles. Footer hints reflect the current tab; quest map-target feedback is hidden under Stats and System.

The host registers the licensed Roboto Condensed face and engine Droid Sans fallback through the shared CkWidgets resource helper. The 14 typography classes explicitly select `font-family: journal-ui`; the family is passed to nested row views through the immutable catalog. There is no CSS inheritance or file import syntax. Registration still begins in native code; reflected scripting declarations and packaged-resource acceptance remain future work. The [native gate](verification.md) verified the fixture at three viewport sizes and checked realized row text bounds. Native focus chrome, default scrollbars, tightly placed header ornaments and fixed-height row limits remain documented fidelity/authoring gaps. The design contract is marked `native-focused-passed`; manual user review and broader game acceptance remain separate.
