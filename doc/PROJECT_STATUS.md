# Current project status

Reviewed on 2026-10-20 against the current C++ implementation, focused feature
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

The unreleased 2026102201 batch also includes the approved
[whole-Origin profile correction](PROFILE_WHOLE_ORIGIN_CORRECTION.md): Default
Origin and whole-Origin Tree entry inherit aligned Feature/Sketch axes without
reinterpreting existing stored placements. Windows GUI, persistence, parent
identity, five-language, FORM/library and template checks pass; final signed
package acceptance/publication remains pending.
It additionally contains verified numeric Feature Direction/Relations and
negative extent/angle reversal, opposite-end Revolution grip limits, Symmetric
hover and Slot entry corrections, default Point/Text display, inward axis-based
Opening authoring and partial-bottom Drill Points. See the
[correction matrix](PRE_RELEASE_CORRECTIONS_20261022.md). Factory definitions
and start templates are refreshed. The user's normal-build test was positive;
the arithmetic follow-up is verified and release is authorized. Final immutable
package acceptance and publication remain pending.

Signed Windows [2026102004](releases/2026102004.md) is accepted and public from
`b96503988b7ba8f98951e9ca2076cf211685c4e0`. It passed 42 finite packaged scenarios, all 1,185 native factory
loads in both candidate and signed smoke, 1,026 current mass Relations, production
trust and anonymous public asset verification. The previous 2003 candidate was
rejected before publication; its failure remains recorded, not reclassified.

The released batch includes Drawing axis-cross dimension contacts and curve
balloons; analytic external references and conic centers; sheet-transition
thickness inheritance; saveable missing-dimension Relations; localized Vladimír
guidance; factory-source update repairs; Slot center H/V; 3D angular text landings;
and Unicode/long-path saving. See the linked release for the precise native-data,
library, test and approximation boundaries. Linux acceptance remains separate.

## Still unfinished or planned

| Work | Current boundary / next step |
| --- | --- |
| Current correction/release batch | Completed in Windows 2026102004 with English documentation, all five UI languages, commit/push and verified signed publication. Further new defects retain priority. |
| Remaining fasteners | Cast wing-nut studies and toothed/fan washers remain unfinished. Preserve source-dimension discrepancies and private user edits. Two-part locking nuts are excluded. See [library queue](FASTENER_REMAINING_WORK.md). |
| Multiple Up To faces and correction | [Scoped design and factory-conversion plan](FEATURE_ORDERED_UP_TO_PLAN.md) prepared; an isolated 32-case planar OCCT feasibility probe passed. Native-contract approval and production implementation remain pending. Preserve ordered consecutive intersections, explicit reordering and geometry-side identity. |
| Sketch projection onto faces | Planned within the Feature principle; associative 3D result, direction, clipping and ambiguous intersections still need design. |
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
Final factory integration and Windows publication are in progress. Linux
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
