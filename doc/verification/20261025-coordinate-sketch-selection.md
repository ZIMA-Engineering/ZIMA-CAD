# Coordinate, Sketch view and selection acceptance — 2026-10-25

This record covers the approved origin-coordinate correction and the requested
H/V, hover, construction-point and Mirror lifecycle repairs. Shared placement
solving, native canonical Sketch coordinates and geometric side choices remain
unchanged. The same C++ implementation uses OCCT 8.0.1 and Qt 6.11.0.

## Observed causes and changes

- XZ Sketch Y is the canonical negative-Z basis. Public coordinate values
  previously exposed that basis directly while the View showed positive Feature
  Origin Z. Explicit work-plane conversion now serves View, Properties, Tree,
  CLI, Relations and Family Table. Generic distances retain their semantics.
- H/V labels and input follow the current perpendicular camera assignment;
  oblique views retain the last assignment. Native solver relations are stable.
- Native points and curves precede reference/axis candidates. Right-button
  cycling and explicit command priorities remain available.
- Axis points also hide active construction-curve endpoint markers. The real
  points remain pickable, snappable and draggable. A shared profile vertex keeps
  its profile role even when its construction curve was authored first.
- Whole-feature selection uses the authored preview even when only its helper
  axis survives in the calculated final body. Mirror/Pattern transform the same
  complete source preview, including the FORM Properties tool wire instead of
  Boolean fragments of the supporting stock. Their source inspection eye consumes that wire.
- Mirror released its Tree editing transaction after its final refresh, leaving
  Insert Here absent until regeneration. The transaction now ends before that
  refresh; OK, Cancel and Undo restore the row immediately.
- Shell adjacency naming read partially enriched neighboring parent tokens.
  Snapshotting the original tokens removes traversal-order dependence without
  changing geometry or numerical accuracy. Offline conversion audits existing
  authored references before accepting changed disposable generated names.
- Qt can format negative zero without its sign. Unchanged coordinate Properties
  preserve the authored branch; intentional edits retain signed-zero handling.

## Native and Windows GUI matrix

| Area | Passing verification |
| --- | --- |
| Coordinate equations | 144 cases: XY/XZ/YZ, X/Y component, both Origin selection orders and both axis factories, both sides, driving/reference and locked/unlocked states. Five successive numeric edits, three drags, fixed-point rejection and exact Sketch readback. |
| Parameter consumers | Local and rigidly placed Feature Origin projection; Relations read/write and units; Family enumeration, positive/negative variants and embedded save/reopen. CLI coordinate matrices, atomic rejection, Undo/Redo and explicit downstream regeneration. |
| H/V | 768 native combinations with dimensions, repeated edits/drags and independent equations; 32 actual Windows GUI front/back/quarter-turn cases, oblique persistence, repeated mouse drags, Undo/Redo and native readback. |
| Properties | 24 plane/component/support combinations, arithmetic and invalid input; signed rectangle inline edits, signed-zero input and 12 unchanged zero-coordinate cases. |
| Hover and points | Eight endpoint/axis overlap cases, reference-intersection input and right-button cycling; construction-point framebuffer/picking checks; hidden construction endpoint dragging in the real Sketch editor. |
| Axis visibility | 192 Windows framebuffer combinations across themes, native readback, editing/passive context, display mode and visibility preferences, plus hidden picking and component endpoints. Offscreen Qt is not used as evidence for OpenGL pixels. |
| Selection | Ordinary View click/Tree synchronization/double-click inspection without revision change or face fill; complete authored Tree wires across a private 16-step model, including the affected extrusion and three Mirrors. |
| Copy lifecycle | Mirror OK, edit Cancel and Undo, Pattern branching/save, immediate Insert Here restoration and independent source-eye inspection. Four native local/placed Body Mirror/Pattern wire cases with deliberately incomplete downstream geometry. |
| Shell | Four independent rounded-stock calculations, 180 face/edge/vertex bindings and native round trips; two fresh private-model calculations preserve all 16 boundary reference packets and regenerated identities. |
| Dependencies | Existing keypoint contacts, tangent mirrors, rounded mirrors, midpoint profiles, equal-length component edits, Bend commands and Family Table suites pass. |
| Localization | Catalog/boundary validation in cs/en/de/fr/ru; localized Axis points tooltip and actual five-language axes GUI verification. |

## Authoring conversion

Sketch numeric contract 36 replaces 35. No product legacy-reader branch or
required external cache is added. Independently embedded FORM Parts are
re-expressed and recalculated before their receiving Part; their required
source surface remains inside the native document. A placed four-boundary
FORM model passes native conversion/readback with its independently retained
source definition. The one-time authoring tool preserves raw
solver geometry, UUIDs and side flags, re-expresses limits/tolerances and uses
Relations AST rewriting and stable Family bindings. Exact reference comparison
ignores only serialization indices, repeated identical vertex storage and
primitive enumeration; oriented triangles, curve direction, identities, side
metadata and every floating-point coordinate remain checked without rounding.

All 1,185 committed factory native assets were freshly calculated or rewritten
by the changed native implementation, saved and reopened. All 36 affected native
regression fixtures also pass current-runtime round trips. Nine older fixtures
first required regeneration by the preceding runtime: their existing stale
caches/derived Sketch refreshes were reproduced before conversion, and the new
results match that refreshed baseline exactly. Six drawing-template fixtures
retain their already embedded text contours through the native preparation hook.
The FORM definition and command suites, native template commands, ordinary FORM
GUI insertion/editing/Undo/Redo and five-language template GUI checks pass.
The full placed Corner FORM GUI matrix also passes after conversion: independent
source replacement, Cancel/unchanged OK, longitudinal placement, complete
Pattern/Mirror previews, Unbend/Bend Back display and native save/reopen.
Twelve factory reference Drawings pass regeneration, exact resolved dimension
checks, Save As and reopen with the converted Part dependencies.

Start Part and Assembly creation was exercised through the real new-document
GUI, checking normal enabled commands and the Part's first active editable Body.
Private authoring outputs and original backups remain isolated from Git. Factory
installation stages the committed baseline separately from private working edits.
Private file replacement is a separate approval step, rather than a side effect
of factory installation.

Evidence is retained under `build/loft-audit/coordinate-*`,
`build/coordinate-factory/verification-factory.json` and the corresponding native
conversion/readback reports. Private geometry and identifiers are not published.

## Limits

This is a finite matrix, not exhaustive Sketcher acceptance. The full nested
Assembly mouse/drag matrix, every trim interaction and native Linux execution
have not been repeated here. Existing broad OCCT upgrade baseline failures
recorded in the preceding release are not claimed as repaired. Windows packaging,
portable GUI/native smoke, committed resource identity, signatures and public
publication are separate release gates.

## Packaged Windows publication

Signed Windows [2026102501](../releases/2026102501.md#windows-publication-acceptance)
passes the separate archive, dependency, native smoke, production-trust,
20 packaged GUI-suite, exact source/resource, public-download and previous-version
update-discovery gates. Private source files are not published or overwritten by
these acceptance checks.

The extended Sketch mouse diagnostic fixtures use ASCII work directories.
Their older `path.string()`/UTF-8 conversions prevented the complete matrix in
Unicode fixture directories; that variant is explicitly unverified. Unicode
package extraction, native CLI/model/export smoke and ordinary GUI startup pass.
