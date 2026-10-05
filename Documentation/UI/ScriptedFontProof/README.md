# Named fonts from a script-owned UMG screen

This slice closes a resource-authoring gap in the scalar session: AngelScript could own a screen and its model, but selecting a named CSS font still required native setup. `FCk_UI_AuthoredFontFamily` now lets the script declare those files in `Config.FontFamilies`; the session builds its catalog through the existing `FCkUiFontResources` bridge. No second loader, parser or screen-specific C++ host is added.

The [focused fresh Editor gate](verification.md) passed all 15 selected tests with 27 inspected compatible captures. The [receipt](verification-receipt.json) records one fresh real-renderer boot, 68 frozen source/resource hashes and seven unchanged historical receipts. The earlier [scripted card](../ScriptingCardProof/verification.md) and [scripted Pause](../ScriptedPauseProof/README.md) records remain separate evidence.

The existing **UI: Scripted UMG Card** gym remains the consumer. Its script creates the UMG shell and session, declares values/actions, accepts native control proposals and handles reload/teardown. The [script](../../../Script/CkUiAuthoring/CkScriptingCard_Gym.as) supplies one `card-ui` family; the [stylesheet](../../../Resources/UiAuthoring/ScriptingCardProof/ScriptingCardProof.ui.css) selects it on two ordinary text nodes.

| Authored part | Resource or behavior |
| --- | --- |
| `card-title` | `card-ui`, Bold, 44 px; existing 80 px line box |
| `card-subtitle` | `card-ui`, Regular, 21 px; existing 44 px line box |
| Other card text | Existing Core base font and typography |
| Slider, checkbox, fire button | Existing native schemas, bindings and script handlers |
| UMG shell | Existing ScaleToFit and 1920 x 1080 reference frame |

Regular uses the journal's existing Roboto Condensed resource and [OFL file](../../../Resources/UiAuthoring/SkyrimJournalProof/OFL.txt). Bold uses the Engine's `Slate/Fonts/Roboto-Bold.ttf`. Both named fallback faces use the Engine's `DroidSansFallback.ttf`. The primary Regular and Bold files are deliberately different; this is an API proof, not a new typography system. The card has no new downloaded or copied font asset.

The optional font list freezes on successful initialization. Constructors and setters perform no I/O. Initialization checks the complete declaration set, then admits every family into a local catalog before publishing the session view or model. Any resource failure rejects the session; corrected initialization can retry. The native control registry remains the sixth `FCkUiView::Create` argument, and the separate font snapshot is seventh. The immutable catalog and retained composites own the inline bytes and primary/fallback UObject faces.

CSS still accepts the existing family-name and Regular/Bold subset. Unknown families or unavailable faces reject a reload before replacing the accepted tree. Font-file paths do not enter CSS. An empty declaration list preserves the default-font path.

The focused gate exercised the actual script constructor/setters, mounted face and byte identities, failed resource admission and retry, reload preservation and retained text through GC. It reused the card's existing pointer interactions and captured its initial layout at 1280 x 720, 1920 x 1080 and 2560 x 1440, plus armed/fired states at 1920 x 1080. Natural text-fit assertions passed for all 13 ordinary text roles in those captures; visual inspection found readable text without clipped glyphs in the tested states.

Development Editor paths remain the boundary. The journal file is already staged as NonUFS, but this screen still resolves it through `ProjectDir()/Plugins/CkTests`; packaged and installed-plugin discovery require a separate proof. The ASCII card does not visually prove fallback glyph coverage. Collections, custom factories, catalog replacement and multi-value transactions remain further reflected API work. These gaps support continued expansion of the current pipeline; this slice alone does not settle a complete replacement comparison with ReactorUMG.
