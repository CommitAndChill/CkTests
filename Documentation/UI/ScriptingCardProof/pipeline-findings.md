# What this slice tests in the authoring pipeline

The earlier proofs established an HTML/CSS layout under a UMG host, but agents still had to write a new native host to construct bindings and receive edits. The scripted card addresses that authoring boundary through one reusable reflected session. Its [focused Editor gate](verification.md) passed all 11 selected tests, with five inspected captures. The following evidence covers the scalar card and named compatible siblings.

| Deficiency | Implemented change | Established focused evidence |
| --- | --- | --- |
| Native-only view construction and typed binding maps | A reflected UObject accepts named scalar declarations and constructs one native view | Script compiled and the real gym selector created the card |
| A new C++ screen host for every simple UI | AngelScript creates the UMG hierarchy and owns model/event policy | Native pointer input reached script handlers; this card has no C++ screen host or Widget Blueprint |
| Editing controls can obscure who accepts gameplay values | Events carry proposed edits; typed setters publish accepted values | Native tests kept model values unchanged before explicit acceptance; PIE handlers accepted and formatted labels |
| Script callers cannot use the native source transaction | Shared in-memory and file reload methods return errors | Bad bindings/files preserved revision/mount; valid source reload retained model |
| Script ownership can outlive or disappear before Slate callbacks | Weak adapters, terminal release and script properties | GC retained the active session, collected unowned sessions, and held widgets were inert after teardown |
| The retained native region contract is hidden from script configuration | Frozen region names prepare every native mount before loading; reload must match the whole set | Empty/duplicate declarations and missing/extra/renamed source regions rejected without partial state |
| Static preflight misses native region-root sizing constraints | The card leaves its direct region root unsized and puts design dimensions in UMG | Production source admission passed; initial captures fit at three resolutions |
| An empty base font can silently render missing-glyph boxes | The session explicitly supplies Core's Regular composite; CSS still chooses size/weight | Native composite identity passed; all five inspected captures rendered readable glyphs |
| A CSS font-size value is smaller than its full native line box | Title/count rows and their enclosing layout provide additional height | All 13 `text` elements passed unchanged natural-fit thresholds across the five captures; the button label was visually clear |

This is a scalar authoring seam rather than a general menu framework. One initialized session freezes declarations and mounts into one surface. The script owns the viewport lifetime and fixture rules. This original card proof does not establish CommonUI pause-layer ownership or a reusable menu abstraction.

The later [owned-session input proof](../OwnedInputProof/README.md) adds optional
CommonUI ownership, authored-ID focus/navigation, cover/uncover input suspension
and held-release draining to the same shared session. Its separate 13-test gate
exercises the API from C++ with a real second local player. It preserves this
card's historical receipt. The later
[layer membership proof](../LayerMembershipProof/README.md) verifies a shared
admission-generation observer and actual AngelScript delegate consumer in its
own 13-test gate. The later [scripted Pause proof](../ScriptedPauseProof/README.md) passed 15 selected tests and migrates real screen policy, retained native Settings and terminal cleanup into the script consumer. It preserves the historical card receipt and records 27 newly inspected captures.

The later [Mission Board proof](../ScriptedCollectionProof/README.md) delivers reflected flat collections and keyed actions in a separate 32-test gate. The remaining model gaps are Image/FloatSeries fields, nested/tree models and custom resource/control factories. The journal and inventory still use native model/custom-control adapters. The later [scripted-font proof](../ScriptedFontProof/README.md) exposes named font declarations through the same session and verifies the actual card consumer in a separate 15-test gate. Multi-value gameplay transactions also need an explicit API if a screen must atomically publish several accepted fields. The card's fixed valid declarations let its reset update several independent scalars safely, but that does not establish a general transaction primitive.

The session supplies its base font explicitly. A subsequent [native default-font
regression](../DefaultFontProof/README.md) reproduced and repaired LastResort selection
for omitted/empty sources, preserving positive caller typography and normalizing
nonfinite sizes. Its separate 12-test gate includes this card and the compatible
siblings; five fresh card captures were inspected again. This document's original
11-test receipt remains the historical evidence for the scalar-session slice.

The development resource path and bounded English copy deliberately limit the claim. Packaging, installed-plugin discovery, localization, physical controllers, accessibility scaling and real game systems need focused proofs. Continue expanding the current pipeline; this slice provides no reason to deprecate it or incorporate ReactorUMG yet. The [Pause migration record](../menu-scripting-migration-plan.md) describes the delivered boundary. Named font declarations in this same card now have a separate focused proof; packaged lookup and richer model bindings remain further exercises.
