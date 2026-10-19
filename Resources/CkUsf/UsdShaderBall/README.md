# USD shader-ball source

The gallery is prepared for the USD Working Group shader ball converted to glTF
by Eric Chadwick. The upstream model and textures use CC BY 4.0, not MIT.

- Source and asset-specific license: https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/USDShaderBallForGltf
- Original geometry/specification: https://github.com/usd-wg/assets/tree/main/full_assets/StandardShaderBall
- License: https://creativecommons.org/licenses/by/4.0/
- Geometry/textures: Chris Rydalch. Specification/validation: Andre Mazzone.
  Original scene, inspiration and consultation: Thomas Anagnostou.
  glTF conversion: Eric Chadwick.

The host's `Tools/UsfMaterials/prepare_shader_ball.py` resolves an immutable upstream
commit, downloads the original GLB and attribution here, and records their SHA256
and modifications in `provenance.json`. It exports one two-slot OBJ and imports
`/CkTests/CkUsf/Showcase/SM_UsdShaderBall`. The complete mesh is centered and scaled
to a 100 cm maximum dimension; original node transforms, valid UVs and normals
are retained. Originally collapsed UV triangles receive dominant-plane charts in
conversion version 2. Specimen and CoreBase slots are resolved by name, independent
of the ordering selected by Unreal's importer. Unknown or duplicate slot names
are rejected. The original OBJ is preserved beside the versioned UV-repaired OBJ.

The user downloaded upstream revision edc7c9e67c639d230715049ee31f9a96a6babbbe.
Its original 32,996-triangle conversion contained 6,912 collapsed UV triangles.
Offline version-2 conversion preserves every triangle/normal and all 26,084 valid
UV triangles, with no collapsed UV faces remaining. Actual reimport/tangent
generation and material appearance require Unreal verification; this offline
result alone is not rendered acceptance.

Conversion 3 corrects the Y-up glTF geometry exported through axis-less OBJ:
positions and normals rotate together as (-Z, -X, Y), preserving winding and UVs.
SM_UsdShaderBall_ZUp3.obj imports with KeepXYZAxes and no additional scene rotation.
The existing editor successfully reimported this version and the Toon specimen
was checked upright in PIE. Artistic appearance and live-edit acceptance remain
with the user. Original source and earlier OBJ versions are preserved.
