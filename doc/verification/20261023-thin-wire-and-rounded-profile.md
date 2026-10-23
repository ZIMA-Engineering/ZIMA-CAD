# Detailed Thin wire and rounded-profile review

## Scope and correction of earlier evidence

The preceding reference-recovery GUI checks verified a nonempty preview and
removed a separately appended source Sketch. They did not prove a complete
material wire. The user's subsequent report exposed missing longitudinal edges,
stationary extra contours with two Up To targets, and a failed rounded Sketch
profile. This review replaces those earlier Thin acceptance claims.

The immutable review input is `2000-kontrola-20261023.prtz`, SHA-256
`931ac6c8d091ff5d0394f6a6736fd9f895d0a9643714f0acde39d291c42d8c75`.
All calculations and saves use copies under `build/thin-feature-review/`.
The user's project and configuration are protected by before/after hashes.

## Traced causes and bounded fixes

1. Thin preview generation joined the source curves into closed offset loops,
   then kept only one longitudinal connector per loop. Preserve the native
   source junctions before joining and draw every actual offset junction.
   Exact native endpoint tangents prevent chord normals shifting line/arc
   junctions. Deduplicate shared open-profile closure connectors without
   rounding or rewriting authored coordinates. Revolution consumes the same
   junctions for its swept connector arcs.
2. Exact FeatureGroup preview mixed active material children with stationary
   inactive-side reference profiles. Mark the latter only in transient native
   calculation data and exclude them from the material preview. Their persisted
   reference geometry and side ancestry remain available. A shared pure semantic
   predicate excludes the same datums from stored Part and Assembly previews,
   including existing files, without a kernel calculation or schema extension.
3. Feature 007 had an actual saved calculation error: `Neplatný odsazený průřez`.
   Its Sketch already contained one rounded corner. The generic mixed-curve
   Thin offset used OCCT's open-result mode even for a closed source contour.
   Native validity diagnostics showed a self-intersecting offset wire with
   unmatched first/last corner ends. Select open-result mode only for open
   source wires. Keep the original source-edge ancestry construction, tolerances
   and validity checks. Temporary diagnostics and the unproven alternative wire
   reconstruction were removed.

The fixes share the Windows/Linux C++ implementation. They do not alter shared
container placement, native format, source Sketch geometry or side choices.
No new user-visible labels or errors are introduced; localization review checks
the existing catalogs and reused text.

## Independent acceptance oracles

`thin_feature_wire_tests.cpp` checks rectangle rails by independently calculated
inner/outer corner coordinates. `thin_wire_verification.hpp` additionally checks
the complete starting perimeter length and all eight unique longitudinal
connectors. A nonempty wire or a bounding box alone cannot pass this oracle.
The oracle is also used against the real GUI's transient material wire after
correction edits, each Thin side mode, removal of the second target and its
reinsertion through the real View picker.

Rounded rectangle tests independently integrate the line/arc section area and
compare native volume after every radius edit. They verify exact junction rail
coordinates and source corner identities through serialized Sketch readback.
Actual Feature 007 edits use the shared workspace commit path, native save/reopen
and Undo/Redo. Undo must restore the authored definition exactly while retaining
newly reserved dimension identifiers under the existing allocation contract.

The GUI uses real Ctrl selection and corner dragging, the inline dimension
editor, Finish Sketch, parent Feature OK, native save/reopen and Undo/Redo.
Equal corner dimensions are edited successively. Native three-dimensional
two-edge Fillets are tested separately from rounded Sketch corners; they are
different operations and neither substitutes for the other.

## Execution record

Verified Windows execution:

| Check | Result |
| --- | --- |
| Rectangular extrusion: XY/XZ, three Thin modes, both directions | 12 variants, eight exact rails each |
| Native two-edge Fillets on those bodies, radii 0.1/0.25/0.4 | 36 passed |
| Rounded profiles: XY/XZ, both directions, three Thin modes, one/two opposite corners, three successive radii | 72 volume/rail/identity cases passed |
| Actual Feature 007, one/two corners and radii 6/8.619847297682325/10 | Six edits, native save/reopen and Undo/Redo passed |
| Actual Features 004/007, two native input-body edges, radii 0.1/0.25/0.4/1 | All eight calculations passed without errors |
| Actual multi-target Properties and synthetic ordered-limit GUI | Passed, 34.283 s |
| Thin GUI second corner by dragging | Passed, 4.255 s |
| Thin GUI equal-radius inline edit, parent calculation and history | Passed, 3.641 s |
| Existing rolled-L Sketch dimension/manipulation matrix | Passed, 37.48 s |
| Existing Thin profile contracts | Passed, 0.55 s |
| Assembly profile command contracts | Passed, 2.54 s |
| Ordered Up To kernel contracts | Passed, 12.34 s |
| Ordered native Feature contracts | Passed, 68.53 s |
| Thin revolution: XY/XZ, three Thin modes, both directions | 12 variants, eight swept junctions and independent volume each |
| Final native Thin matrix plus revolution limits | Passed, 4.73 s and 1.45 s |
| GPU source-wire caching and occluded selection | Passed, 5.26 s |
| Five-language translation contract | Passed, 14.86 s |
| Extended actual-model GUI: start perimeter/rails, hover, inspection, save/reopen | Passed, 37.040 s |
| Final equal-radius GUI, successive values 5/2/4 and parent calculation | Passed, 3.929 s |

Records: `final-native-revolution.log`, `dependent-native.log`,
`final-revolution.log`, `final-focused.log` and GUI result/log/image files
under `build/thin-feature-review/`. The earlier failing native offset evidence
is retained in `rounded-before.log` and `offset-status.log`.

A read-only inventory parsed 1,035 factory `.prtz` documents without errors;
none declare a Thin extrusion/revolution/general Feature. It did not rewrite
factory or user assets.

Actual-model hover publishes no base scene and uploads exactly the authored
wire's stroke vertices. The existing parameter inspector publishes exactly one
annotation scene; it must not be mistaken for an extra geometry calculation.
Hover and inspection preserve the document revision. Properties opening performs
zero exact-preview calculations and publishes exactly its one rollback scene;
numeric corrections do not republish that base scene. The GPU tests verify cached
source reuse over repeated repaints, occluded wires and unchanged surface pixels.

Native Linux execution remains unverified. The finite matrices do not claim all curve
types, arbitrary self-intersecting offsets or all possible multi-surface layouts.
Curve/Sketch-plane associative points have their own
[verification record](20261023-curve-plane-points-and-rounded-mirror.md).
Release packaging remains a separate gate.
