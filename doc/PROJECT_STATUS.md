# Current project status

Reviewed on 2026-10-22 against the current C++ implementation, focused feature
guides, verification records and public GitHub release. This is the current
capability index. Dated design proposals in the roadmap and development notes
must not be read as a list of features still missing today.

## Available implementation

"Implemented" means the documented command and native workflow exist. It does
not imply universal geometric convergence, every proposed extension, or a new
Linux verification. The linked guides give supported cases and finite checks.

| Area | Implemented scope | Details |
| --- | --- | --- |
| Family Table | Part/Assembly variant editor, stable dimension/presence bindings, shared parent file and linked members, shared edits, Assembly insertion/Replace, Drawing variant selection, persistence and Undo/Redo | [Family Table](FAMILY_TABLE.md) |
| Sketcher | Points, segments, circles/arcs, ellipses, splines, profiles, constraints and dimensions, dragging, Trim, native offsets, exact external references, native text and symbols | [Sketcher](SKETCHER.md), [text](verification/20261019-sketch-text-anchor.md), [symbols](SYMBOLS_USER_GUIDE.md) |
| Part Features | One Feature interface for Point, Axis, Plane, Sketch and Extrusion/Revolution; independent sides, Solid/Thin/Surface results, draft and existing extent targets | [Feature](UNIFIED_FEATURE_TYPES.md) |
| Bodies and history | Multiple Bodies, activation, history editing, Booleans, linked Body derivation, stable source references and transactional edits | [Bodies](MULTIBODY_AND_BOOLEANS.md), [derivation](BODY_DERIVATION.md) |
| Sweeps and paths | Ordered 3D Curves, 2D/3D Sweeps, multiple profile stations/Loft, Thin modes and Helical Sweep | [3D](3D_CURVE_AND_SWEEP.md), [2D](SWEEP_2D.md), [helical](HELICAL_SWEEP.md) |
| Edge and hole tools | Fillet, equal-distance/two-distance/distance-angle Chamfer, Shell, drilled channels, Hole/Opening, shaft threads and Drill Point | [Edge treatments](EDGE_TREATMENT_COMMANDS.md), [Holes](HOLES.md), [openings](THREADED_OPENING.md) |
| Copies and transformations | Part/Assembly Mirror and linear/circular Pattern, independent Save As with associated Drawings, mirrored-Part workflows | [Mirror/Pattern](MIRROR_AND_PATTERN.md), [copies](DOCUMENT_COPY.md) |
| Surfaces | Profile surface results, General Surface, Fill, Sewing, Intersection, Trim and Surfaces from solid | [Surface tools](NETWORK_SURFACE.md) |
| Sheet Metal | Flat, Bend, revolved/twisted sheet and transitions, cuts, FORM definitions, independent Unbend/Bend Back and intervening edits for supported material regions | [Sheet Metal](SHEET_METAL.md), [state operations](SHEET_STATE_DEVELOPMENT.md) |
| Solid state changes | Straightening and Restore shape for supported original geometry | [Solid straightening](SOLID_STRAIGHTENING.md) |
| Assemblies | Part/subassembly insertion, exact nested activation, mates and placement, source editing, Assembly cuts, occurrence identities and explicit dependency regeneration | [Assembly references](ASSEMBLY_REFERENCES.md), [activation](COMPONENT_ACTIVATION_COMMANDS.md) |
| Drawings | Sheets and views, projected/section/detail workflows in their documented scope, associative dimensions/chains, Show/Erase, frames/title blocks, BOM regions/balloons, symbols, PDF/DXF/image output | [Drawings](DRAWINGS.md), [manual](USER_MANUAL.md) |
| Engineering data | Parameters, Relations, materials, units, physical properties and measurements, native engineering tables and fastener libraries | [Relations](RELATIONS.md), [properties](PHYSICAL_PROPERTIES.md), [library status](FASTENER_REMAINING_WORK.md) |
| Exchange and automation | STEP/IGES/DXF import/export, supported STL output, shared GUI/CLI commands and context-aware AI console | [Coverage](CAD_COMMAND_COVERAGE.md), [AI console](AI_CONSOLE.md) |
| User interface | Czech, English, German, French and Russian; light/dark themes, internal Properties and native file management | [Manual](USER_MANUAL.md), [localization](LOCALIZATION.md), [File Manager](FILE_MANAGER.md) |
| Distribution | Native Windows portable releases, version-owned runtime/resources/source and authenticated updater; Linux has separate native-host acceptance | [Distribution](NATIVE_DISTRIBUTION.md), [updates](UPDATES.md), [Linux](LINUX_RELEASE_HANDOFF.md) |

Family Table is therefore an existing feature, not a future implementation task.
Its linked editing and single-parent-file workflow were already included in
[Windows 2026091508](releases/2026091508.md); later work extends and verifies it.
Six old standalone primitives are removed; current modeling uses the Feature
and profile workflows rather than the former primitive commands.

## Latest released correction batch

Signed Windows [2026102201](releases/2026102201.md#windows-publication-acceptance)
was published at `2026-10-22T13:49:48Z` from immutable source
`a49735ec42d3442a5d1bb0c29a0eb94c7c568e04`. It includes original preview-wire selection, aligned profile/Origin
entry, stable side reversal and numeric Direction/Relations, negative arithmetic
length/angle input, Revolution grip limits, Sketch entry/inspection corrections,
axis-based Opening direction, partial-bottom Drill Points and the accepted Sketch
command grouping/icons. Candidate and signed native smoke load all 1,185 factory
assets. Fourteen candidate and seven fresh signed GUI scenarios,
7,353 exact factory resource comparisons,
17,224 signed immutable checksums, production trust,
all authenticated draft downloads and all anonymous public downloads pass.
Trusted 2004 offers the update without installation. The native factory/template
refresh is included; private user files and the development BAT are preserved.
Linked diagnostic-path, finite geometry/GUI and native Linux limits remain explicit.

The preceding [2026102004](releases/2026102004.md) release remains immutable;
its accepted library/mass and other historical verification are preserved in
that release record. Newer verification does not erase earlier rejected probes.

Post-release development restricts [ordinary Sketch Reference entry](verification/20261022-individual-sketch-reference-entry.md)
to individual geometry and repairs [Fillet on connected segments of one original curve](verification/20261022-connected-fillet-curves.md),
verified on isolated copies of `2000.prtz`. These corrections are in the current
local build and source, not retroactively in the immutable 2201 archive.
The subsequent [symmetric Feature endpoint correction](verification/20261022-symmetric-feature-endpoints.md)
completes shared operational junctions through existing Boolean ancestry and
repairs the variable Fillet radius law across a complete multi-edge rail.
Focused native, real mouse/GUI, FORM GUI and localization checks pass. An older
standalone FORM test has separate archive-reader/fixture assumptions under audit;
the verification record preserves that failure and release-baseline comparison.

The approved [native source-binding extension](verification/20261022-sketch-intersection-source-bindings.md)
now supplies cold open-solid intersection ancestry and preserves bindings through
rigid placement and derived copies. Its 108-case matrix, factory refresh and all
1,185 read-only native loads pass. All 137 Drawings pass reference/regeneration
comparison against the released runtime without rewriting their factory files.
The new Sketch editor commands and branch definitions remain unfinished.
The subsequent [coincident reference-circle picking correction](verification/20261022-coincident-reference-circle-picking.md)
removes artificial display-chord crossings while retaining native circles/arcs.
Its 392-case native matrix, reported `0101.prtz` pairs, mouse checks and all five
languages in both themes pass. A separate [preselected circular C input fix](verification/20261022-preselected-circle-coincidence.md)
passes the existing saved-equation GUI regression. Both are local post-release
changes; the immutable 2201 archive is unchanged.

## Still unfinished or planned

| Work | Current boundary / next step |
| --- | --- |
| Current correction/release batch | Completed in Windows 2026102201 with English documentation, all five UI languages, commit/push and verified signed publication. Further new defects retain priority. |
| Remaining fasteners | Cast wing-nut studies and toothed/fan washers remain unfinished. Preserve source-dimension discrepancies and private user edits. Two-part locking nuts are excluded. See [library queue](FASTENER_REMAINING_WORK.md). |
| Sketch intersection references | Approved Face × Sketch-plane scope and 108-case native matrix, including certified toroidal radial branches and cold open-solid/transformed sources; source binding and factory verification are recorded in the linked plan. Editor commands, persistent selected branches and the full manipulation matrix remain pending. See [plan](SKETCH_INTERSECTION_REFERENCE_PLAN.md). |
| View-relative H/V | Requested display/input mapping for 90-degree turns and view flips; retain the internal geometric constraints and persisted meanings. |
| Multiple Up To faces and correction | [Scoped design and factory-conversion plan](FEATURE_ORDERED_UP_TO_PLAN.md) prepared; an isolated 32-case planar OCCT feasibility probe passed. Native-contract approval and production implementation remain pending. Preserve ordered consecutive intersections, explicit reordering and geometry-side identity. Each side uses the shared table with at least three visible rows and its correction below. |
| Sketch Projection Feature | Requested as another Feature type consuming a Sketch; associative 3D result, direction, clipping and ambiguous intersections still need design. |
| File Manager directory prototypes | Discuss ordinary/linked metadata types and configurable prototype `0000-index` folders before implementation. |
| Direct mesh/Surface editing | Design ideas: independent meshes, point groups, primitives, subdivision, deformation, optional explicit point-to-model bindings and conversion to suitable shells/solids. No implemented Blender-style mesh editor is claimed. |
| FEM | Requested analysis as an additional Body type within a Part; supported materials, boundary conditions, meshing, persistence and independently checked results remain to be designed/implemented. |
| Voice and pointer context | Requested speech input and lightweight semantic mouse context for AI; design and integration pending. |
| Further modeling extensions | Dedicated spring/pipe workflows, additional pattern variants and individual unsupported surface/solid cases remain scoped follow-ups, not missing foundations. |
| Continuing verification | Real-project geometry/reference recovery, larger Assemblies, solver edge cases, production Drawing workflows and a later comprehensive Undo/Redo audit. Undo/Redo itself is already implemented. |
| Linux | Each new change/release requires native build and GUI/package verification on the Linux host. Windows results do not establish that acceptance. |

This correction/release batch is complete. Resume the agreed remaining library
and modeling follow-ups; newly reported program defects retain priority. FEM, voice and direct mesh brainstorming
do not interrupt the current work. Native Sketch offsets already exist;
STEP-derived offsets are outside the requested scope.

## Maintaining this index

User-selected interoperability bookmarks are retained in
[External resources](EXTERNAL_RESOURCES.md). The approved next Sketch reference
extension is scoped in [Sketch-plane intersection references](SKETCH_INTERSECTION_REFERENCE_PLAN.md).
The [external trim and cap-reference corrections](verification/20261021-external-reference-trim.md)
pass their Windows native and Solid/Thin fixture mouse matrices. After changing
face picking to current visible fragments, the actual `1000.prtz` final-feature
start cap passes; its end cap has no matching offered Face candidate and remains
unresolved. The former original-geometry cap test is not acceptance of this
changed visible-face behavior. The new intersection calculation has a
[51-case native foundation matrix](verification/20261021-sketch-face-plane-foundation.md);
the new Sketch command and persisted branch definition remain in progress.
The current [command-preview selection correction](verification/20261022-command-preview-selection.md)
uses the actual analytical creation/Properties wire for Modeling Features,
Extrusions and Revolutions, shared by hover, click and inspection. Exact current
Face boundaries for placement are retained. Direct GUI segment comparisons pass
for the synthetic and both L30X30X3 snapshots, along with Sheet/Solid state
inspection. The user accepted the L30X30X3 experiment. The broader
[factory origin/preview audit](verification/20261022-factory-origin-preview-audit.md)
passes all 1,185 native factory loads, existing frames/native Sketch data in
1,034 Parts, 21 finite preview GUI cases and fifteen dependent contracts.
The separately approved [Feature side reversal](verification/20261022-feature-side-reversal.md)
retains the original panels, locks, references and logical Start/End while
reversing physical direction; its focused native and actual GUI matrices pass.
Factory integration and Windows 2026102201 publication are complete. Linux
execution remains unverified. The preceding
[GPU-wire investigation](verification/20261021-feature-selection-gpu-wire.md)
retains historical `1000.prtz` feature 003 measurements. The
[endpoint equality and rounded-corner annotation correction](verification/20261021-sketch-equal-endpoint-corner-markers.md)
records the native dimension/drag matrix and actual mouse interaction cases.

Update this page, the root README and the corresponding active roadmap entry
when a capability changes status. Link implementation/verification evidence and
state release/platform limits. Preserve historical release records as history;
do not rewrite their original acceptance results into current promises. Search
engines and generated summaries can retain older snapshots and need not update
immediately after a Git push.
