# Surface operation suite development

## Authorized scope, 2026-10-04

Implement the surface requirements in [Boundary Surface](NETWORK_SURFACE.md):
general surfaces owning ordinary Sketches and 3D Curves, reference-based Fill Surface with per-boundary
G0/G1/G2, reusable Surface Intersection curves/points, Surface Trim, Sewing and
Fillet on sewn shells. Consolidate existing surface commands into Modeling
without removing their capabilities. Also deliver the manually edited bend-note
library under `config/symbols/sheetm`. Grid and G3 are explicitly excluded.

The user subsequently authorized final localization/documentation, commit, push
and a tested Windows version. This supersedes the initial goal wording that no
release was requested. Use the repository's committed-source packaging,
validation and release workflow. Do not publish a partial suite as complete.
The existing `VERSION` at the start of work is `2026100303`; assign the final
build identity only when preparing the completed Windows version.

Preserve the earlier verified solid-state, axis/reference and dependency fixes
already present in the working tree. Separate unrelated user settings and
untracked projects/backups from the implementation/release input. No cleanup
was needed: the initial C: check showed approximately 67 GiB free. Cleanup is
authorized only if needed, after confirming the exact disposable paths; never
remove user projects, settings or the active development build.

## Current implementation checkpoint

- Boundary definitions and kernel packets use ordered variable-length chains,
  retaining a four-row default. The current filling path now supports closed
  two-chain, three-chain and N-chain contours. Source/corner ancestry and the
  existing exact-curve adapter are preserved.
- Chain orientation uses two-state propagation with linear work in boundary
  count, replacing a fixed four-chain bit-mask search. Calculation still checks
  perimeter closure, crossings, fitting tolerance, validity and positive area.
- The reference dialog has a boundary-count field. Pending count changes retain
  existing rows where applicable, arm missing input, preserve independent
  inspection and remain cancellable. Extra window height expands the table's
  viewport with fixed row heights. No kernel work belongs to these draft edits.
- New shared strings were added to all five language catalogs. A PowerShell
  validation initially used case-insensitive JSON properties and rejected
  existing `Plocha`/`plocha` keys; case-sensitive hashtable parsing confirmed the
  resulting catalogs. No key-case normalization or catalog rewrite was applied.
- The native `ZE-BEND-NOTE.symz` asset was generated with ordinary Body/Sketch
  ownership and an unrestricted Text field. Five language variants provide
  manually editable examples. No angle/direction inference, flipping of wording
  or bend-specific UI was added. The generated preview was visually inspected.
- Native start templates were rewritten/reopened with the current serializer;
  the corresponding new-document GUI acceptance remains outstanding.

## Verification completed at this checkpoint

- `zima_cpp_boundary_surface_tests`: passed, 1.87 s. Includes independent planar
  area checks for 3/5/17-sided contours with reversed chains and a two-arc closed
  perimeter, malformed packet rejection, persisted source/corner identity,
  triangular native save/reopen, existing four-chain surfaces and subsequent
  cuts, invalid edit rejection, unchanged OK and Undo/Redo.
- `zima_cpp_boundary_surface_dialog_tests`: passed, 0.10 s. Includes count edits,
  preserved references/inspection, expanded input, viewport resizing, structural
  clear, OK and Cancel, plus the existing four-row interaction checks.
- `zima_cpp_translations_contract`: passed, 5.12 s. A further explicit check of
  the new count and instruction labels was added afterward and awaits rerun.
- `zima_cpp_symbol_document`: passed, 0.27 s. Includes all five bend-note variants,
  unrestricted authored text, rotated rendering and ordinary Body ownership,
  alongside existing symbol edit, persistence and history contracts.

Sandboxed native tests initially encountered Windows access errors while
canonicalizing an EXE path and renaming a test temporary native file. The same
surface/localization tests passed outside that sandbox; no product fallback was
introduced to conceal an environment restriction.

## Work remaining

### Continuity implementation in progress

The reference filling definition now also records original edge sources,
per-boundary G0/G1/G2, an explicit support face and its reversed side. The dialog
reuses the shared reference cells with separate input/inspection for boundary
and support. G1/G2 require an edge belonging to the selected original face;
missing or ambiguous sources are rejected instead of substituted. A bare Sketch
or 3D curve remains a G0 input. The command has not yet received its final
Fill Surface label or separate owned-boundary companion.

When a history actually uses original-edge filling, the explicit kernel calculation retains the original operand edges and vertices
alongside the existing immutable face chain. This is required to resolve source
edges after subsequent trims or booleans without selecting result topology.
It does not introduce a persisted sidecar, change source identity or alter the
protected placement solver. Other histories retain their previous face-only
allocation behavior. Adding the first edge-dependent operation rebuilds an
explicit input prefix whose live cache has no retained original edges.
Reference mesh reuse is disabled for edge-dependent
fill operands: an unchanged reference key can otherwise conceal changed source
geometry. Full unchanged-history reuse remains available.

Continuity fitting uses the document linear tolerance, the document angular
tolerance converted from degrees to radians, and a curvature tolerance of
`1e-5 / mm`. G2 uses the kernel's fourth-order energy criterion. The algorithm's
maximum fitting errors are checked, followed by boundary projection, normals
and curvature operators of the final approximated surface. The curvature check
includes principal directions, not just eigenvalue magnitudes. A shared support
provides the initial surface instead of an unnecessary plane-to-curved fit.
The constructor's degree is not a G3 claim; continuity is never downgraded.

The first G1/G2 tests exposed three concrete integration defects, now fixed:

- Copying an edge alone lost its support pcurve. Each constrained face and its
  edges now share one deep copy, reused within that operation. Source geometry
  remains immutable.
- OCCT 8.0's indexed error methods allocate the initial sample count even though
  fitting adapts each constraint's count; their subsequent writes can exceed the
  allocation. Use the maxima computed safely by the algorithm, plus the final
  independent derivative checks. See the primary implementation in
  [GeomPlate_BuildPlateSurface](https://github.com/Open-Cascade-SAS/OCCT/blob/V8_0_0/src/ModelingAlgorithms/TKGeomAlgo/GeomPlate/GeomPlate_BuildPlateSurface.cxx).
- The current filling adapter forwards `GeomAbs_Shape` directly to the integer
  Gi order, where order 2 is G2 but `GeomAbs_G2` has value 3. The isolated call
  supplies integer order 2 through the corresponding enum value, with a static
  assertion. This requests actual curvature constraints. Independent cylinder
  normal, principal-curvature and direction checks verify it; see
  [BRepFill_Filling](https://github.com/Open-Cascade-SAS/OCCT/blob/V8_0_0/src/ModelingAlgorithms/TKBool/BRepFill/BRepFill_Filling.cxx)
  and [BRepFill_CurveConstraint](https://github.com/Open-Cascade-SAS/OCCT/blob/V8_0_0/src/ModelingAlgorithms/TKBool/BRepFill/BRepFill_CurveConstraint.cxx).

The consistent Windows build passed the expanded boundary kernel/document test
(4.48 s), GUI creation/picking/edit/Cancel/Undo/Redo (7.17 s), dialog interactions
(0.15 s), all five translations (5.11 s), factory new-document GUI (3.66 s), native
symbol (0.29 s) and Drawing symbol GUI/output (18.17 s). The geometry test includes
changed cylinder radii, original-edge input after a preceding cut, missing input
rejection, cached-prefix reconstruction and persisted support/side choices.

The preceding inconsistent translation build was rebuilt and now passes. The
Windows serializer compilation also exceeded the standard COFF section limit;
`/bigobj` is now scoped to `part_document.cpp`, without changing product behavior.

The current GUI includes the verified three-boundary creation/edit/Cancel and
native-save scenario, plus actual bend-note Drawing insertion and output through
the established symbol pipeline. Continue the remaining owned-boundary and downstream
surface operations, with persisted ancestry/side choice and dependency behavior.
The current command is still labeled Boundary Surface; the separate Fill Surface
and general-surface command distinction has not yet been integrated.

Do not modify the protected placement solver. Trace each new command's opening,
draft input, calculation, rollback/Cancel, no-op confirmation and final scene
publication under [the performance guide](FEATURE_PERFORMANCE_GUIDE.md). Native
format changes require current start templates and real new-document GUI checks.
Retest dependent operations and the earlier fixes, then complete English manuals,
all localization gates, commit/push and Windows packaging/release acceptance.

### General surface ownership clarification

The user's subsequent answer on 2026-10-04 specifies ordinary Sketch or 3D Curve
creation inside the general-surface container. These are independent editable
boundary definitions, with external references supported by their ordinary
editors. This supersedes the proposed 2D/3D Sweep boundary types and the temporary
selected-Sweep-result-edge adapter, which was removed before integration. Reuse
the existing exact Sketch/Curve3D boundary adapter and existing sub-editors. The
parent's ordinary placement carries the complete local definition. Keep Fill
Surface as the separate existing-reference operation. No further user clarification
is currently needed; the user reconfirmed this definition and the complete suite.

### Sewing implementation in progress

Inputs are native calculated surface references at the operation boundary and
document tolerance. Means are an operation-local deep copy, OCCT Sewing and
native parent-derived names. Output is one connected manifold shell, preserving
surface area and zero material volume. Unselected solids/shells retain their
objects. Sewing does not cap an opening or convert a closed shell to a solid.
Reject disconnected, non-manifold, missing or ambiguous inputs.

The kernel request and explicit history path are integrated. Endpoint
ancestry uses spatial buckets only to locate native source parents; coordinates
and OCCT enumeration positions never become identities. Merged boundaries store
all contributing source-parent tokens. Kernel tests cover two perpendicular
patches, selection-order/source-size identity, disconnected rejection and Fillet
on the resulting shell. The expanded kernel suite passed in 7.18 s; it also
checks partial overlapping boundaries and an unaffected solid's volume and area.
The partial overlap retains native split-edge parents. The source point test
exposed an existing face-wire orientation reversal in Boundary Surface; matching
now restores the authored edge orientation before endpoint naming. The derived
reference fingerprint schema was incremented to retire previously cached names.

The native feature, dependency/deletion paths, shared reference properties dialog
and Modeling action are integrated and verified in the consistent Windows build.
Creation and later editing use one dialog and one commit path. Removing a row
removes its list item; the trailing input row remains available. Independent
inspection and short middle-button entry termination do not alter pending data.
Properties opening publishes the actual rollback input once and uses persisted
reference geometry for labels, selection and inspection. Unchanged OK returns
before calculation and transaction creation. The native/kernel suite passed in
12.24 s with closed shells, curved source surfaces, excessive-gap and non-manifold
rejection, feasible opposite-sided shell fillets and excessive-radius rejection.
It covers cascade suppression/prerequisite restoration and source deletion with a
retained unresolved Sewing feature, native reopen and Undo. Dialog interactions
passed in 0.17 s. Actual common-picker creation and subsequent shell Fillet,
Properties/Cancel, native save, unchanged OK and Undo/Redo passed in 9.67 s.
All five translations passed in 5.29 s and factory new-document GUI passed in
3.72 s after rewriting start templates with the current serializer. The GUI
fixture must have its own document ID; the initial copied fixture collided with
the already open source document and waited for the duplicate-document prompt.
The fixed fixture passed. The sewing Properties screenshot was visually checked.

Sewing now has its own semantic icon in the toolbar and Tree. Final surface-group
ordering and the separate Fill Surface name remain pending the general owned
surface companion. The complete requested suite and release remain unfinished.

### General Surface owned definitions in progress

The native General Surface owns ordinary Sketch and Curve3D definitions inside
its history container. Placement derives their display frames; source IDs remain
the authored Sketch/Curve/Point IDs. Request preparation reuses the existing
Boundary Surface adapters through an operation-local carrier without copying
the complete body or invoking OCCT. Native persistence, Body ownership,
dependency collection and deletion are integrated. Calculation and unchanged OK,
Undo/Redo, mixed owned boundaries, transformed frames, area and native cold
reopening passed in the expanded native surface suite (12.09 s).

The shared properties dialog passed ordering, independent inspection, boundary
removal, standard Sketch plane/offset fields, resizing, OK and Cancel tests.
All five language translations passed. Ordinary editor, Tree, toolbar and actual
placement GUI integration remain pending; this command is not yet user-ready.

General Surface now consumes the existing placed-feature lifecycle and common
capability predicate. Its owned Sketch enters the ordinary transient Sketcher;
its owned Curve uses the ordinary Construction properties and nested Point
editors. A command-local carrier resolves the pending native parent and keeps
Curve/Point values local while display and reference frames follow Surface and
Body placement. The protected shared placement solver was not changed. Owned
definitions have Tree rows and remain eligible as preceding external references.
The first complete GUI test passed in 3.39 s: Whole-Origin entry and correction,
Sketch return, Curve/Point creation without premature commits, calculated area,
native ownership, unchanged OK, child/parent Cancel and Undo/Redo. Two initial
fixture assumptions were corrected: Body Origins use the document-origin Tree
role, and the first Whole-Origin plane's correction is not necessarily X.

Visual inspection exposed unused space below the boundary list. The dialog now
opts into the shared expanding-bottom-table layout and no longer repeats its
instruction in the status row. Further verification adds the owned Point
annotation's placed frame and an exact single scene publication when Properties
opens. The reference-based command is renamed Fill Surface (Zaplnit plochu)
with its common dialog title translated in all five catalogs. Native tags and
user-authored feature names retain their meanings.

### Exact sheet-skin cut regression

The additional requested case starts on one Flat skin and ends Up To the
opposite native skin. A 20 x 20 x 2 stock with a 4 x 4 opening must retain
768 cubic millimetres. The initial test reproduced an authored trim-ancestry
error at exactly coincident caps. During explicit calculation, Common can retain
a native profile rim or the exact boundary of a clipped, already named side
face without reporting that face as its edge generator. The Sheet Cut ancestry
path now consumes those existing Sketch-derived identities when generator
history is empty. It introduces no positional identity or UI kernel work.

Both cut directions and both normal/clearance modes passed, including exact
target persistence and cold native regeneration (0.55 s). The complete existing
Bend/Sheet Cut native suite passed in 38.22 s. Further checks add a rotated Flat
and a zero-offset source-skin placement reference. Actual GUI verification and
final release remain pending. No user-visible text changes accompany this fix.

The expanded native matrix passed in 1.17 s, covering both directions, both
calculation modes, rotated/unrotated Flat geometry, zero-offset placement on
the exact starting skin, authored Sketch-curve ancestry and cold reopening.
Actual GUI selection of the opposite original skin through the common picker,
changed OK, save/reopen, Cancel and unchanged OK passed in 5.09 s for both
directions and modes. All five language catalogs passed in 5.08 s. Normal
Sheet Cut fingerprints now include an ancestry schema marker so an older
derived packet cannot hide the changed exact-rim naming path.

### General Surface and Intersection verification checkpoint

General Surface's owned Point/Curve editors now prepare their input reference
data before showing and publish the visible scene once after showing. The
placed annotation witness, owned Tree Point editing, parent/child Cancel and
surface-toolbar ordering checks passed in the actual GUI (3.56 s). The native
suite checks document-wide owned identity collisions and annotation-only edits
without replacement of the calculated shape or fingerprint. The latter expanded
suite passed in 14.56 s before additional curved Intersection cases were added.

Surface Intersection now has a native two-face definition, explicit section
calculation, common-picker Properties, dependency/deletion integration, native
curve/point packets and command-local scene display. Its inputs remain bounded
original faces and its operation leaves the body surfaces unchanged. Parent
names contain both source-face identities and native endpoint ancestry, never
enumeration indices or coordinate strings. Coincident areas report an explicit
error; disjoint faces produce an empty, successful result. Ambiguous branch
ancestry is rejected rather than rebound.

Actual GUI creation, native save, rollback, one opening scene publication,
unchanged OK, Cancel and Undo/Redo passed in 3.11 s; all five dialog translations
passed in 4.85 s. Required face fields expose replacement through the field,
with no removal or Origin input. Visual inspection caught a QTableWidget
re-showing the hidden indicator wrapper; wrapping the shared indicator keeps
its unused child controls hidden. The expanded GUI and dialog checks passed in
3.05 s and 0.18 s respectively.

The next native tests verify cylinder/plane, closed sphere/plane and isolated
tangent contacts, plus consumption by Fill Surface. The cylinder's quarter-arc
length and radius checks pass. The closed sphere fixture exposed Section
segments at periodic support seams, which must be joined into the same analytic
or C1 spline domain before assigning branch identity. This command-local
calculation uses shared vertices only and leaves source geometry immutable;
its verification is still in progress. See the primary
[UnifySameDomain API](https://occt3d.com/dev/doc/refman/html/class_shape_upgrade___unify_same_domain.html).
Intersection is not yet release-complete. Downstream references, source edits
and cold reopening remain to be verified, followed by Surface Trim and the
final release gates.

The expanded native tests now pass (15.70 s): planar, cylinder and warped
B-spline intersections, disconnected branches, a closed sphere/plane loop,
isolated tangent contact, disjoint faces and coincident-area rejection. Fuzzy
Section spline pieces can have a small derivative mismatch at a support seam.
After joining shared-vertex wires, exact bounded pieces are concatenated with
full C0 knot multiplicity when ordinary same-domain unification leaves several
pieces. An explicit complete parameter interval retains a closed wire's common
vertex. This preserves its geometry without fitting a smoother substitute.
See the primary [composite-curve API](https://occt3d.com/dev/doc/refman/html/class_geom_convert___comp_curve_to_b_spline_curve.html).

The tests verify source-change cache invalidation, native cache reopening and
fresh kernel reconstruction with the same branch identity. Fill consumes the
native curve directly. Ordinary Sketch external geometry can project it into
an owned Sweep 2D path; the Sweep is placed at its native starting point and
follows source-surface displacement. The full prerequisite chain participates
in cascade suppression and prerequisite restoration. The Sweep's first test
fixture incorrectly left its Origin away from the path start, violating the
existing Sweep rule; placing it through the ordinary point reference fixed the
fixture without changing Sweep or shared placement behavior.

Further endpoint-order checks exposed Section reversing its parameter direction
when its two input fields were exchanged. This symmetric operation now supplies
its source faces in canonical native-parent order while preserving each face's
orientation. Intersection fingerprints use schema 2. Tests for unchanged endpoint
references and rejection of a removed branch after merging are awaiting the
current validation run. No new user-visible strings accompany these corrections.

The final Intersection checkpoint passed all five focused contracts in 34.03 s:
native surface suite 16.31 s, Fill/Sewing GUI 6.23 s, Intersection GUI 2.80 s,
five-language UI 4.65 s and exact sheet-skin Cut GUI 3.97 s. The native test now
proves stable endpoint references when face fields are exchanged and rejects a
dependent Fill after its selected branch merges into a different curve. General
Surface GUI also passed in the preceding run (3.23 s). No Grid, G3, Trim or
completed portable release is claimed by this checkpoint.

### Surface Trim lifecycle before implementation

Inputs are one current native surface face, preceding native tool faces or
curves and the user's retained-region seed. Output replaces only the selected
surface face with its chosen bounded fragment and exposes actual trim edges.
Unselected surface/solid geometry and tools remain unchanged. The means are the
existing reference controls, persisted viewer geometry, explicit Section/Splitter
calculation and atomic native-history commit. No fitting of a replacement
surface, arbitrary projection of tools or increase in calculation tolerance is
authorized.

Opening installs rollback before one input scene publication. Target/tool entry
and independent inspection use the common picker. A retained-region click can
reuse `candidate_face_triangles`, `ray_at` and `confirmed_face_hit` for the already
offered target face; this only reads viewer data. Explicit OK resolves the seed
on the exact target and partitions it. Unchanged OK returns before calculation
and Undo. Cancel restores the normal scene and command contract.

Region intent must include native parent ancestry, rather than a fragment index.
Creation or explicit seed replacement can capture the selected fragment's
signature after successful calculation. Regeneration must reject an invalidated
signature or a seed on an ambiguous separating boundary instead of silently
keeping another piece. Tool curves must be bounded, lie on the target within
tolerance and actually separate it. Face tools can reuse the Intersection
calculation internally, then partition by its native curves. All source choices
and required region intent belong in the native document. This is the design
checkpoint, not an implemented Trim command; exact data/API choices are still
subject to tracing the existing calculation and serialization paths.

### Surface Trim implementation checkpoint

Surface Trim is now implemented as a native history command with current target
face, native face/edge tools, retained seed and boundary-ancestry guard. It uses
Section internally for face tools and non-destructive Splitter only on explicit
calculation. The original surface, orientation, tools and unrelated operands are
preserved. Native ancestry names retained edges, corners and face; ambiguity is
an explicit error. Captured intent does not enter the geometric fingerprint;
incremental reuse validates it against native reference packets. This keeps the
reference-settling pass from performing another identical split.

The real GUI test passed in 3.18 s: common original-face picking and cycling,
viewer-only region click, one rollback publication, native save, unchanged OK,
Cancel and Undo/Redo. Its screenshot was inspected. Catalog/source coverage and
real five-language dialog checks passed in 4.95 s. Shared surface-dialog tests
passed in 0.18 s; native surface tests passed in 21.56 s, including B-spline
surface preservation, closed-loop interior/exterior with a hole, stable source
edits, rejection after the seed crosses the tool, explicit repair, cold reopen,
and deletion retaining unresolved features. The deletion test exposed an
Intersection serialization guard missing for unresolved references; only that
command-specific guard was added, preserving the existing unresolved-history
contract. Final mixed-solid checks and broad/versioned builds are ongoing.

The subsequent identity review refined Trim regeneration: the saved native
boundary signature selects the retained region directly, rather than reclassifying
the original click after source movement. A changed seed is used only for an
explicit new selection. Added checks move the tool across that old click, remove
and restore the split, and translate both inputs 500 mm away. Internal Section
edges now locate geometry only: trim ancestry directly stores the real target/tool
faces and original edges/points, never the nonexistent intermediate Section
owner. Trim fingerprints use schema 2. These refinements await the current
complete build and validation run; earlier timings describe the preceding
checkpoint. The full drawing test harness also required MSVC /bigobj, matching
the application, after exceeding the object-section limit with current native
feature definitions. No drawing behavior or precision changed.

The complete Windows 2026100401 build succeeded after a transient link retry
(LNK1114, access denied while replacing one generated test library). No source
workaround was needed. The final native surface suite passed in 24.09 s, including
native retained-region following across the original seed and a 500 mm input
translation, missing-region rejection, independent mixed-solid properties and
absence of intermediate Section parents. All seven final surface/skin/dialog/
translation contracts passed in 38.01 s while the build was still linking other
test executables; these timings include that machine load. The broad shared UI
contract passed in 13.92 s. The previously observed native-style hover failure
was not reproduced, and no product/style change was made for it. Factory Part,
Skeleton and Assembly templates were rewritten and reopened with the current
serializer; their tracked bytes are unchanged. Full serial regression and fresh
committed-source Windows packaging remain the final gates.
