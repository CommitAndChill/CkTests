# Opal and coated display sources

This additive pair uses the approved separate Adaptive showcase profile. It does not
change the project's normal Blendable launch or the approved shader-ball gym.

The opal reuses the closed AmberHero body without changing that mesh. Its original
generated color artwork is preserved at `Source/OpalOriginal.png` (1254 by 1254).
`OpalTextures.json` records the source and five derived maps. Domain orientation,
coverage and roughness are artistic estimates, not measured opal data. The material
combines static lit mineral color and native colored specular response beneath a
separate polished Slab. Both layers have zero emission. It does not solve spectral
diffraction or volume scattering.

The display uses four original closed meshes with named material slots: GlassCover,
Panel, Housing and MetalTrim. Dimensions are 18 by 11 by 2.5 centimeters. The glass
is 0.33 cm thick with a gentle 0.10 cm bow, a polished bevel and a gap above the panel.
Geometry.json pins sources, generator, topology checks and axes. The seven display
and coating maps are original 2048-square drawing/finish data, not photographs.
DisplayTextures.json records hashes and color-space conventions. Texture artwork
contains no baked glass reflections. OBJ import already converts the upward source V
axis; the material must not flip it again.

The coating approximates dielectric thin-film RGB reflectance through native Slab F0.
It stays non-emissive; only the opaque panel emits. Neutral Pixel Normal Offset avoids
the unresolved solid-glass self-image path. This is not traced entry/exit refraction
or a caustics demonstration.

## Authoring

- Sources: `Tools/UsfMaterials/author_optical_pair.py`,
  `author_display_textures.py`, and `derive_opal_maps.py` from the host root.
- Recipes: `Tools/UsfMaterials/optical_pair_settings.py`.
- Live shaders: `/CkUsf/Looks/OpticalOpal.ush`, `OpticalCoatedGlass.ush`,
  `OpticalDisplayPanel.ush`, and shared `/CkUsf/OpticalPair.ush`.
- Reproducible offscreen studio: `Tools/UsfMaterials/Launch-OpticalPair.ps1`, then
  `optical_pair_command.py prepare`, `scene`, and `capture --subject opal|display`.
  The launcher preserves any existing project editor and runs through UnrealToolbox.
- Verification: `verify_optical_pair.py reload`, `final`, and `motion` use the owned
  serial bridge. No editor restart is needed for normal shader authoring.

Color, texture sampling and UV transforms, finish normals, roughness, opal domain
strength, layer thickness, glass transmission and coating properties remain in `.ush`.
`FlashStrength=0` removes both opal body coloration and colored reflection; it is a
plain mineral endpoint. `CoatingStrength=0` leaves a plain dielectric glass cover.

No third-party texture download was used for this pair. The opal image is original
generated artwork and its provenance is recorded honestly; no scan or CC0 license
claim is made. All display graphics and geometry were authored in project scripts.
