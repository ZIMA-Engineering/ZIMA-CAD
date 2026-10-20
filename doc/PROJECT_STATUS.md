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

## Current source changes and release acceptance

The latest accepted public Windows release at this review is
[2026102002](releases/2026102002.md). Candidate 2003 was rejected before
publication after a Unicode-path verifier failure. Its earlier smoke passes do
not establish acceptance. Candidate [2026102004](releases/2026102004.md) remains
unpublished until its complete fresh-build, archive, signed-runtime and GUI
acceptance gates pass.

Current source work includes Drawing axis-cross dimension contacts and curve
balloons; analytic external references and conic centers; sheet-transition
thickness inheritance; saveable missing-dimension Relations; localized Vladimír
guidance; factory-source update repairs; Slot center H/V and 3D angular text
landings. Unicode diagnostics and native long-path saving receive additional
verification. The linked release notes distinguish implementation from package
acceptance and describe approved native data/library updates.

## Still unfinished or planned

| Work | Current boundary / next step |
| --- | --- |
| Current corrections and Windows release | Finish remaining GUI/package checks, English documentation and five-language review; commit/push and publish only the verified signed package. |
| Remaining fasteners | Cast wing-nut studies and toothed/fan washers remain unfinished. Preserve source-dimension discrepancies and private user edits. Two-part locking nuts are excluded. See [library queue](FASTENER_REMAINING_WORK.md). |
| Multiple Up To faces and correction | Agreed ordered consecutive surface intersections, explicit reordering and signed correction; design/implementation still pending. Preserve geometry-side identity and obtain any required persisted-contract approval. |
| Sketch projection onto faces | Planned within the Feature principle; associative 3D result, direction, clipping and ambiguous intersections still need design. |
| File Manager directory prototypes | Discuss ordinary/linked metadata types and configurable prototype `0000-index` folders before implementation. |
| Direct mesh/Surface editing | Design ideas: independent meshes, point groups, primitives, subdivision, deformation, optional explicit point-to-model bindings and conversion to suitable shells/solids. No implemented Blender-style mesh editor is claimed. |
| FEM | Requested analysis as an additional Body type within a Part; supported materials, boundary conditions, meshing, persistence and independently checked results remain to be designed/implemented. |
| Voice and pointer context | Requested speech input and lightweight semantic mouse context for AI; design and integration pending. |
| Further modeling extensions | Dedicated spring/pipe workflows, additional pattern variants and individual unsupported surface/solid cases remain scoped follow-ups, not missing foundations. |
| Continuing verification | Real-project geometry/reference recovery, larger Assemblies, solver edge cases, production Drawing workflows and a later comprehensive Undo/Redo audit. Undo/Redo itself is already implemented. |
| Linux | Each new change/release requires native build and GUI/package verification on the Linux host. Windows results do not establish that acceptance. |

The agreed immediate order remains program corrections and release acceptance,
then the approved modeling follow-ups. FEM, voice and direct mesh brainstorming
do not interrupt the current work. Native Sketch offsets already exist;
STEP-derived offsets are outside the requested scope.

## Maintaining this index

Update this page, the root README and the corresponding active roadmap entry
when a capability changes status. Link implementation/verification evidence and
state release/platform limits. Preserve historical release records as history;
do not rewrite their original acceptance results into current promises. Search
engines and generated summaries can retain older snapshots and need not update
immediately after a Git push.
