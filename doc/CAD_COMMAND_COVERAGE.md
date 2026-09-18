# CAD command coverage

## Current scope — 2026-09-18

The command layer covers the currently supported CAD operations with **324
commands**. GUI and CLI use shared model operations; the audit of remaining
direct GUI writes is complete. [Holes](HOLES.md) adds `holes.create/get/set`,
using finite Sketch segments and a common diameter for Part subtraction.
Family Table reference queries and instance opening, plus `component.replace`,
cover the linked-variant workflow described in [Family Table](FAMILY_TABLE.md).
Historical [Body measurement](BODY_PROPERTIES.md) adds five commands sharing the
GUI's cached geometry, history-boundary and transaction contracts.
[Drawing balloons](DRAWING_BALLOONS.md) adds seven commands for first-level BOM
positions, reference repair and visibility through the same GUI transaction.

[Sheet state operations](SHEET_STATE_DEVELOPMENT.md) add `unbend.create/set`
and `bend_back.create/set` through the same workspace transaction as GUI OK.
They support all or selected source regions, intervening material edits, native
reopening and Undo/Redo. The former `bend.create/set` state argument is removed.
The count above was checked against the native CLI `help` catalog.

The GUI does not launch the CLI executable for each action. Both applications
call the same command/model layer. Mouse picking, camera movement and transient
dialog presentation remain GUI adapters. Their model results are expressible
through explicit command references and parameters. A CLI without a View does
not invent a camera or selection.

This closes coverage for existing supported features, not every future roadmap
item. AI integration and the comprehensive Undo/Redo audit remain separate
stages. See [CLI/GUI audit](CLI_GUI_AUDIT.md), [CLI usage](CAD_COMMAND_LINE.md)
and [CAD console](CAD_CONSOLE.md).

## Completion criteria

- GUI and console/CLI commit the same document transaction.
- References use stable ZIMA IDs and exact occurrence paths, never OCCT face
  numbers or Tree labels.
- Data queries do not calculate bodies. Calculation is explicit and preserves
  reference solving, ownership and reversible history.
- Arguments have defined types, units, validation, errors and results.
- Native model and process tests verify actual results; affected GUI paths are
  checked as well. A rejected request leaves no partial mutation.
- Save/export checks inspect real output files. Required persistent data stays
  in native `.prtz`, `.asmz` and `.drwz` documents.

A missing operation must be implemented in the shared layer, not bypassed by
editing serialized data or calling GUI widgets from the standalone CLI.

## Coverage by area

All rows below describe implemented current scope. Focused documents retain
parameters, detailed limits and the verification evidence for each stage.

| Area | Coverage and boundaries | Details |
| --- | --- | --- |
| CLI process and catalog | UTF-8, scripts, stdin, config, context and data Tree; typed strings/numbers/integers/booleans/objects/arrays validated before mutation | [CLI](CAD_COMMAND_LINE.md) |
| Documents | New/Open/Save/Save As, activation/close, working directory, archives, file deletion and rename; shared data/file transactions | [Document operations](DOCUMENT_OPERATIONS.md), [archives](ARCHIVE_COMMANDS.md), [rename](NATIVE_FILE_RENAME.md), [Unicode paths](UNICODE_NATIVE_FILE_COMMANDS.md) |
| Regenerate and Undo/Redo | Shared Part/Assembly/Drawing operations; regressions accompany each changed transaction | [Part transactions](PART_SESSION_TRANSACTIONS.md), [state publication](WORKSPACE_STATE_PUBLICATION.md) |
| Primitives | Box, cylinder, sphere, cone, pyramid and wedge creation, queries and parameter changes, including locks and precision | [Placement](PLACEMENT_COMMANDS.md), [value locks](VALUE_LOCK_COMMANDS.md) |
| Sketch-driven Holes | Finite cylinders along non-construction segments, common diameter, Part subtraction, shared GUI/CLI commit | [Holes](HOLES.md) |
| Sheet Metal | Flat, Sheet Profile, Revolved Sheet, Twisted Sheet with measured flat-pattern correction, Sheet Cut; Unbend/Bend Back for supported selected material regions with intervening cuts and additions | [Sheet Metal](SHEET_METAL.md), [state operations](SHEET_STATE_DEVELOPMENT.md) |
| Part history | Dependency-valid movement, suppression, deletion and insertion cursor, including bodies and Booleans | [History commands](HISTORY_COMMANDS.md), [reordering](HISTORY_TREE_REORDER.md) |
| Bodies and Booleans | Create/query/activate, name, visibility, body/global cursors, Boolean operations, order and deletion; original placement references | [Body commands](BODY_COMMANDS.md), [multibody model](MULTIBODY_AND_BOOLEANS.md) |
| Placement | `placement.get/set`, locks, shared reference entry/removal for bodies, constructions, six primitives, Part profiles and other supported containers; component placement through `component.set` | [Placement references](PLACEMENT_REFERENCE_COMMANDS.md), [removal](PLACEMENT_REFERENCE_REMOVAL.md), [work planes](WORK_PLANES.md) |
| Construction geometry | List/get/create/set/delete, root and curve-point references, tangents, corner radii and ordered points; embedded path-point changes commit the complete Sweep | [Construction](CONSTRUCTION_COMMANDS.md), [construction references](CONSTRUCTION_REFERENCE_COMMANDS.md), [Sweep point references](SWEEP_POINT_REFERENCES.md) |
| Sketch geometry | Standalone and owned Sketches, Part/Assembly properties, points, segments, circles/arcs, ellipses, B-splines, rectangles/polygons, move, construction geometry and native text; DXF into owned profiles | [Sketch commands](SKETCH_COMMANDS.md), [Sketch properties](SKETCH_PROPERTIES_COMMANDS.md), [Assembly Sketches](ASSEMBLY_SKETCH_PROPERTIES.md) |
| Sketch constraints and editing | All 15 supported constraint kinds, solver and removal; 16 dimension kinds with values, properties, labels and deletion; offset create/get/set/free, complete support/interval preservation, intersection trim, mirror, oriented rectangle, tangent and corner radius | [Sketcher](SKETCHER.md), [offset](SKETCH_OFFSET.md), [inline dimensions](INLINE_DIMENSION_COMMANDS.md) |
| External Sketch references | Local/context creation, exact projection, refresh and release; owned profiles, dependency transactions, Undo/Redo and closed-owner summaries at explicit regeneration | [Exact projection](SKETCH_EXACT_PROJECTION.md), [context geometry](CONTEXT_REFERENCE_GEOMETRY.md), [reference transactions](CONTEXT_REFERENCE_TRANSACTIONS.md), [owned refresh](OWNED_SKETCH_REFERENCE_REFRESH.md) |
| Extrusion and Revolution | Part profiles and Assembly-owned cuts; create/get/set, owned Sketch, Thin, directions, original end references and exact targets; atomic Sketch batches and placement references | [Profiles](PROFILE_COMMANDS.md), [profile references](PROFILE_REFERENCE_COMMANDS.md), [Assembly cuts](ASSEMBLY_CUT_HISTORY_COMMANDS.md) |
| Sweep | 2D/3D/helical create/get/set; full path, stations, profile/mapping management, 2D path-plane reference, helical base offset and placement references | [Sweeps](SWEEP_COMMANDS.md), [Sweep references](SWEEP_REFERENCE_COMMANDS.md) |
| Holes and threads | Thread catalog; Opening and native Hole dimensions/profiles, chamfers, tips, direction, through-all, independent original bore/thread targets and optional components; shaft threads and standalone drill points | [Openings](OPENING_COMMANDS.md), [native Hole](NATIVE_HOLE_COMMANDS.md), [drill references](DRILL_REFERENCE_COMMANDS.md), [optional components](OPENING_COMPONENT_COMMANDS.md), [shaft thread](SHAFT_THREAD_COMMANDS.md), [drill points](DRILL_POINT_COMMANDS.md) |
| Fillet, Chamfer and Shell | Actual input edges/faces, create/get/set, common tangent routes and member/route/last-item removal | [Edge treatments](EDGE_TREATMENT_COMMANDS.md), [Shell](SHELL_COMMANDS.md) |
| Mirror and Pattern | Part sources and immediate Assembly components; linear/circular modes, planes/axes, placement, locks, unsaved sources and Undo/Redo | [Derived copies](DERIVED_COPY_COMMANDS.md), [Mirror/Pattern](MIRROR_AND_PATTERN.md) |
| Assembly | Query/insert/open source, properties, deletion and exact nested activation; four supported placement mate kinds with limits/locks; dependency validation, atomic reference summaries and cut history | [Components](COMPONENT_COMMANDS.md), [properties](COMPONENT_PROPERTY_COMMANDS.md), [activation](COMPONENT_ACTIVATION_COMMANDS.md), [removal](COMPONENT_REMOVAL_COMMAND.md), [references](ASSEMBLY_REFERENCES.md) |
| Drawing | Sheets, templates, history, view create/query/edit/delete/regenerate, model annotations, Show/Erase, measured dimensions and chains, title blocks, BOM source parameters, hatch styles, PDF/DXF/PNG/JPEG sheet or region export | [Drawing commands](DRAWING_COMMANDS.md), [labels](DRAWING_LABEL_COMMANDS.md), [annotation layout](DRAWING_ANNOTATION_LAYOUT_COMMANDS.md), [hatches](DRAWING_HATCH_COMMANDS.md) |
| Template editor | New/open/get/save and atomic Sketch editing for frames/title blocks; native text, images and BOM regions, locks, shared transactions and Undo/Redo | [Templates](TEMPLATE_COMMANDS.md), [template objects](TEMPLATE_OBJECT_COMMANDS.md) |
| Named views | Part/Assembly list/get/set/delete, complete camera, native persistence and Undo/Redo | [Named views](NAMED_VIEW_COMMANDS.md) |
| Appearance and 3D dimensions | Part/exact occurrence styles, face groups, source inheritance/reset, history/persistence; original dimension identities, full text style, layout and reset without body calculation | [Appearance](APPEARANCE_COMMANDS.md), [dimension layout](MODEL_DIMENSION_LAYOUT_COMMANDS.md) |
| Sections | List/get/components/create/set/activate/delete, full open section line, owned Sketch batches, placement references, exact occurrences and hatching | [Sections](SECTION_COMMANDS.md), [section references](SECTION_REFERENCE_COMMANDS.md) |
| Measurement | List/get/evaluate/create/set/delete with original references and saved results | [Measurement](MEASUREMENT.md) |
| Engineering metadata | Shared parameters, units, precision, relations, material library, reference-bound Family Table columns and generated Part/Assembly variants. Relations driving geometry dimensions remain separate future work | [Metadata](METADATA_COMMANDS.md), [engineering metadata](ENGINEERING_METADATA_COMMANDS.md), [Family Table](FAMILY_TABLE.md) |
| Import/export | Part/Assembly STEP/IGES/DXF and imported-feature properties; nested STEP and STL, exact Sketch DXF including trims/offsets/text/corner radii and unclamped/periodic splines | [Import](IMPORT_COMMANDS.md), [export](EXPORT_COMMANDS.md), [imported features](IMPORTED_FEATURE_COMMANDS.md) |
| Interactive View image | `export.view` uses the real GUI View adapter; a batch host without a View returns `view_unavailable` | [View export](VIEW_EXPORT_COMMAND.md) |

## Verification record

The final pre-Holes shared-command audit passed the complete Windows Release
suite, **161/161 in 675.84 s**. Adding Holes produced **162/163 in 560.27 s**;
the failure identified truncated console `help` output as the catalog grew.
After correction, the console and expanded Holes GUI checks passed **2/2 in
138.09 s**. Holes model checks include intersection volume, finite length,
rotation, stable identities, native persistence and Undo/Redo.

Work-plane changes then ran **161/165** in the full suite; all four affected
checks passed after updating automatic-plane expectations and embedded template
Sketches. This is a full run plus targeted corrections, not a claim of a later
single 165/165 run. See [work-plane verification](WORK_PLANES.md#verification-2026-09-15).
The subsequent rotation arm passed seven targeted contracts, including real
mouse dragging, limits, cancellation, followers and persistence; see
[rotation arm verification](ASSEMBLY_ROTATION_HANDLE.md#verification-2026-09-15).

After removing the old Python runtime and ordering the View toolbar, the Windows
GUI/CLI rebuilt and the console UI contract passed **1/1 in 139.26 s**. Logs are
`build/native-only-toolbar-build.log` and `build/native-only-toolbar-tests.log`.
These local checks do not establish Linux or portable-release acceptance.

The former chronological migration entries duplicated domain documents and
contained superseded command counts and pending states. They were retired with
the user's approval; Git history and the focused documents preserve the original
evidence. Update this matrix and the relevant domain contract when adding or
changing a command.
