# Replica proof presentation resources

These resources provide a representative scene and licensed typography for the code-authored Mass Effect and Skyrim gyms. Each backdrop is separate from the interactive widget tree: all labels, key hints, meters, glyphs, focus, selection and cooldowns are rendered by the native pipeline.

## Typography

Rajdhani (Regular, SemiBold, Bold) is used for `me-ui` and `me-display`. Barlow Condensed (Regular, Medium, SemiBold) is used for `skyrim-ui`, `skyrim-display` and the compatible `journal-ui` alias. They are free substitutes, not the games' proprietary typefaces. The corresponding OFL texts are included beside the font files. Source: [Google Fonts Rajdhani](https://github.com/google/fonts/tree/main/ofl/rajdhani) and [Google Fonts Barlow Condensed](https://github.com/google/fonts/tree/main/ofl/barlowcondensed). Font files were downloaded from those repositories on 2026-10-08.

## Scene artwork

`me-backdrop.png` and `skyrim-backdrop.png` were generated on 2026-10-08 using the built-in image generation tool, then copied without image manipulation. They depict game-inspired scenes, not captures of the original games.

Mass Effect direction: ruined alien spaceport, over-the-shoulder armored soldier with a rifle, cool blue architecture, smoke and restrained orange embers; no UI, text, labels or HUD.

Skyrim direction: snowy Nordic watchtower courtyard, a hostile bandit in the middle distance, first-person sword and magic hands at the edges, natural muted winter lighting; no UI, text, labels or HUD.

The shared private presentation loader admits fonts and an owned texture brush before publishing any bindings. Both image files, six font files and their license notices are staged as NonUFS resources by CkTests.Build.cs. Packaged execution has not been verified.
