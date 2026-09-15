# Import through shared model transactions

## Stage scope

Import menus and `import.step`, `import.iges`, `import.dxf` share
`workspace::import_part` for Part and `workspace::import_assembly` for Assembly.
Targeted DXF into an existing sketch uses `workspace::import_sketch`; GUI drafts and
committed command targets share `prepare_sketch_dxf`. Geometry conversion stays in
existing native interchange functions; Body/component placement rules are unchanged.

This stage covered normal active Parts and root Assemblies. Import into active
nested occurrences or pending GUI previews was tracked separately in command coverage.
DXF now also accepts embedded-profile IDs and standalone/embedded sketches in root
Assemblies. Exports: [EXPORT_COMMANDS.md](EXPORT_COMMANDS.md).

## Commands and units

- `import.step path [mesh_deflection_mm] [output_directory]`
- `import.iges path [mesh_deflection_mm] [output_directory]`
- `import.dxf path [sketch] [unitless_scale_mm] [maximum_entities] [output_directory]`

All accept optional `document` to verify active-document identity. Extensions must
match commands (`.stp/.step`, `.igs/.iges`, `.dxf`, case-insensitive). Relative paths
use the command working directory. Paths/saved names are UTF-8, including on Windows.

`mesh_deflection_mm` is positive and finite: display-mesh deflection in millimetres,
not a change to exact geometry. Omission uses document-accuracy `mesh_deflection`;
new documents receive it from normal config/templates. Values of 1, 2 or more mm
are allowed. The GUI import dialog's template-based default is described in
[STEP_IMPORT_EXPORT.md](STEP_IMPORT_EXPORT.md).

Part DXF without `sketch` creates a new owned sketch in the active Body, or a
standard Body if none is active. With a sketch ID, it adds an independent imported
block. Targets include serialized Sweep/Loft, Helical or Hole profiles; discover IDs
with `sketch.list`. Sibling profiles and the owner remain unchanged. The owning
Body must be active and cannot be a derived copy. `unitless_scale_mm` (default 1)
applies only without specified units; `$INSUNITS` takes priority. `maximum_entities`
is an integer 1–1000000, default 100000. Source counts include unsupported DXF types,
whose warnings are returned.

```json
{"command":"import.step","arguments":{"path":"import/screw.step","mesh_deflection_mm":2}}
{"command":"import.dxf","arguments":{"path":"import/outline.dxf","sketch":"SKETCH_ID","maximum_entities":10000}}
```

## Part result and transaction

Results contain `document`, `source`, new `bodies`/`containers` IDs, `sketch`,
`import_block`, `source_entities`, `imported_entities`, `warnings`, `body_calculated`,
`changed` and new `revision`. DXF-specific fields are empty/zero for 3D imports.

Calculation owns an input-Part snapshot. GUI uses existing background jobs; CLI is
synchronous. Workers do not mutate Workspace. After success, the main thread checks
open identity, revision, data generation and active Body before one combined
model/result commit and one Undo step. Failure, close/reopen or concurrent target
changes preserve current data.

DXF preserves the last calculated body. STEP/IGES explicitly calculate imported
geometry. None regenerates parent Assemblies or automatically saves the native
document; `save` remains explicit. STEP/IGES geometry and original topology identity
live inside `.prtz`, so after saving the source interchange file is unnecessary for
reopening or regeneration.

CLI separates OCCT diagnostics onto stderr, leaving exactly one result JSON line
per command on stdout.

## Part verification

`zima_cpp_import_command_tests` checks a 10×20×30 mm box (6000 mm³) from STEP/IGES,
saved accuracy, Undo/Redo, source STEP deletion followed by regeneration, a 20×10 mm
DXF rectangle, authoritative header units, Czech names, invalid input and late jobs.
Process tests run real CLI with an invalid Qt platform and check all three formats
in Czech paths, native files and clean protocol. GUI regression uses both a command
and the real Import action through QFileDialog.

Full suite: 70/71 in `build/part-import-full-tests.log`; the corrected new GUI test
then passed 1/1 in `build/part-import-gui-tests.log`. The fix concerned test file
preselection while the dialog proxy model loaded, not model import. This stage had
105 commands.

## Assembly import

The same commands in an active Assembly create native sources and insert one root
occurrence. STEP preserves real product hierarchy; repeated components share one
source Part/subassembly while retaining occurrence identities. Solids and surfaces
inside one STEP product do not become separate components.

IGES/DXF without `sketch` create one Part from the current Part template, containing
an imported Body or native owned sketch respectively. DXF with `sketch` adds geometry
directly to an existing Assembly sketch. New sources inherit owning-Assembly accuracy
and display units; STEP coordinates and geometry calculations remain mm.

`output_directory` is optional and Assembly-only. It must name a nonexistent directory
under an existing parent; import never overwrites existing sources. Relative paths
use the console working directory. Omission creates unique `<source_name>_zima`,
then `_1`, `_2`, etc., beside the target Assembly or in the working directory for
an unsaved Assembly. GUI uses the same layout for STEP/IGES/DXF, containing only
`.prtz` and `.asmz`, without required manifests or extra formats.

```json
{"command":"new","arguments":{"type":"assembly","name":"assembly"}}
{"command":"import.step","arguments":{"path":"import/unit.step","output_directory":"unit_native","mesh_deflection_mm":2}}
{"command":"save","arguments":{}}
```

Assembly results contain `document`, `source`, `directory`, `files`, `occurrence`,
`source_document`, source-ID lists `parts`/`assemblies`, `sketch`, DXF statistics,
`warnings`, `changed` and new owner `revision`.

Calculation and writing use separate prepared data. Both worker phases check target
open identity, revision and generation afterward. Complete owner changes including
Undo are prepared before inserting sources into live Workspace. Failure or changed/
reopened targets leave no partially inserted documents; cleanup removes only this
unfinished operation's files, never recursively deleting unrelated directories.
Successful source files are saved; the user explicitly saves the owning Assembly.

Undo detaches the single inserted root occurrence but preserves new independent
source documents/files. Redo restores the same occurrence identity. Import changes
neither active/displayed document nor other parent Assemblies through regeneration.

`zima_cpp_assembly_import_command_tests` checks four 10×20×30 mm boxes in two
occurrences of one subassembly: 24000 mm³ total, one source Part, two source Assemblies
and shared geometry. It also checks Unicode paths, units, accuracy, native save and
regeneration without STEP, Undo/Redo, 1000 mm³ IGES, DXF, and failed/concurrent
completion of both phases. Real CLI covers all three Assembly imports and reopening;
GUI uses a STEP command and actual DXF menu action.

The catalog stayed at 108 commands; existing imports now covered both document types.
Integration **7/7**, 17.00 s, `build/assembly-import-integration-tests.log`.
Final regressions **3/3**, 15.52 s, `build/assembly-import-final-tests.log`, including
reopened targets, passive parents and preserving unrelated files during cleanup.
GUI/CLI rebuilt in `build/assembly-import-final-build.log`.

## DXF into owned profiles and GUI drafts

With `sketch`, Part/Assembly creates one block directly in the target sketch, without
new containers, components or directories. `output_directory` is rejected in this
mode. Assembly without `sketch` retains new-component import. Section sketches use
their own section operation.

If the edited profile has a calculated body, run `regenerate` before saving the
completed model so history boundaries match new parameters under the current native
format. Import does not make that decision itself.

Commands import a private sketch copy and then check open identity, revision,
generation and Part active Body. Shared sketch transactions commit with Undo/Redo.
No body is calculated; profile curves affect the body only on explicit Regenerate.

GUI import inside Sketcher uses the same conversion with `mutate_active_sketch`.
Profiles in open Properties remain transient; finishing Sketcher returns them to
owner drafts. Only owner OK calculates/commits the model; Cancel discards imported
draft geometry. Console commands cannot concurrently overwrite an open GUI editor.

## Ellipses, rational splines and axes in DXF

Alongside segments, circles, arcs and polylines, shared import accepts `ELLIPSE`,
`SPLINE`, `XLINE`. Ellipses/arcs become native elliptical geometry, preserving
rotation, parameter interval and negative normal. Splines retain control points,
degree, knots and weights. ZIMA-exported interpolated/periodic forms return as exact
bounded splines, not sampled segment chains. Interchange DXF does not retain
constraints or offset construction history.

Splines require valid knot vectors, control points and positive weights; missing
weights mean ones. Unclamped/periodic inputs are exactly clamped to their active
intervals as described below. Fit-point-only splines remain unsupported. Closed
splines require coincident actual curve endpoints within 1e-8 mm. Source DXF geometry
must lie in XY; existing target sketch planes remain unchanged. Scale/units affect
coordinates/axes, not knots/weights. `XLINE` becomes an infinite construction axis.
Standalone `POINT` is also supported as detailed below.

Each file first becomes an independent block draft. Only a complete validated block
is added to the target sketch copy. Circles/arcs cannot reuse coincident points from
previous imports, so moving a new block does not move old blocks. A second-entity
failure leaves no first entity behind. Target identity, plane and prior references
are preserved.

Tests cover DXF–native Sketch–DXF roundtrip, serialization, repeated-block independence,
units and rejection of incorrect counts, negative weights, unsorted knots, spatial
control points and disconnected closed splines. Command checks cover one Undo/Redo
step and `.asmz`; real CLI saves/reopens `.prtz`. GUI uses actual Import and embedded-
profile regression. Independent explicit extrusion checks an ellipse with semiaxes
3×5 mm by 10 mm (150π mm³) and a rational-spline circle r=5 mm by 10 mm (250π mm³).

Integration **10/10** (38.20 s), `build/dxf-import-curves-integration-tests.log`;
volume checks **1/1** (0.45 s), `build/dxf-import-curves-volume-tests.log`; units
**1/1** (0.10 s), `build/dxf-import-curves-units-tests.log`. Initial unit-test cleanup
failed on Windows because its input stream remained open; closing it fixed cleanup
without changing production import. GUI validation used
`build/cpp-windows-release/zima-cad-dxf-import-validation.exe`, leaving the running
user CAD untouched. Catalog: 152 commands; native format/templates unchanged.

## Standalone DXF points (2026-09-13)

`import.dxf` accepts planar POINT entities, including point-only files. Each explicit
POINT receives a distinct stable ZIMA ID even at coincident coordinates or segment
ends. Curves still share coincident vertices only within their import. Repeated
imports do not bind new points to previous blocks.

The CONSTRUCTION layer preserves construction-point status. `$INSUNITS` is
authoritative; `unitless_scale_mm` applies only without units. Spatial points,
incomplete/infinite coordinates, scale overflow and `maximum_entities` overflow
reject the whole import, preserving sketch/history.

Existing block fields are reused: `point_ids` for all points, `geometry_ids` for
curves. Curve lists may be empty if points remain. Native fields/extensions are
unchanged, with no new required files/cache. Point blocks use existing transformation
operations. Individual deletion of owned points still respects whole-block protection.

Part without `sketch` creates an owned sketch; an ID adds a block to that Part/Assembly
sketch. Assembly without an ID creates a component referencing a native point Part.
Save and Undo/Redo preserve all IDs without calculating a body for point sketches.
Re-export writes explicit POINT entities, not merely curve-editing handles.

Model tests cover point-only/mixed entities, coincident coordinates with distinct IDs,
construction flags, mm/inch/unitless files, exact new-block translation/rotation
without changing old blocks, point merging, invalid late entities, limits, native
Part/Assembly and Undo/Redo. Model suites **5/5 in 1.16 s**, point-Part insertion
extension **1/1 in 0.13 s**, full suite **132/132 in 553.70 s**
(`build/dxf-points-full-tests.log`). An extra mixed-block test found merging the
last segment's endpoints also detached remaining standalone points. Blocks now remain
when those points remain; existing segment-to-one-point merging is unchanged.
The fix passed **2/2 in 0.38 s**. Both apps/all tests rebuilt; final suites passed
**7/7 in 72.34 s**, including real CLI and GUI menu import
(`build/dxf-points-final-build.log`, `build/dxf-points-final-tests.log`).

## Exact unclamped spline import (2026-09-13)

`import.dxf` and GUI Import accept open/periodic SPLINE with unclamped end knots.
Fields/flags follow [Autodesk DXF SPLINE documentation](https://help.autodesk.com/cloudhelp/2016/ENU/AutoCAD-DXF/files/GUID-E1F884F8-AA90-4864-A215-3182D47A9C74.htm).
The active interval runs from the knot at degree index to the knot at pole-count
index. Exact knot insertion and trimming inactive ends preserve shape, parameterization
and rational weights, without segment approximation or OCCT.

Import and native periodic Sketcher curves share `clamp_curve_geometry` and the same
knot-insertion algorithm as exact trimming. Results use existing native clamped
splines; serialization/templates are unchanged. Already clamped splines return
unchanged. Closure checks actual result endpoints; periodic input endpoint poles
need not coincide.

Invalid counts, nonpositive weights, infinite values, invalid knot multiplicities,
empty intervals and disconnected shapes marked closed reject the whole import.
Fit-point-only inputs remain unsupported in GUI too. Import does not calculate a
body; the previous cache stays available.

Model checks compare analytical parabolas and rational variants, periodic shapes,
degrees 1–5, partially clamped ends, exact trimming, block independence, atomic
errors, Undo/Redo and native Part/Assembly. A closed four-quadratic-arc profile has
area 40/3 mm²; explicit 3 mm extrusion verifies 40 mm³.

Model suites **3/3 in 0.53 s**. After both apps/all tests built, integration passed
**13/13 in 91.43 s**, including real CLI, GUI import, offsets and exact splines
(`build/dxf-spline-model-tests.log`, `build/dxf-spline-integration-build.log`,
`build/dxf-spline-integration-tests.log`). Independent ezdxf 1.4.4 generated 24 curves
and compared CLI/native Part/DXF roundtrips at 402 samples per curve. Maximum
deviation: 1.168e-14 mm; export audit: 0 errors, 0 fixes.
Result: `build/dxf-spline-ezdxf-validation.json`.

## Editing imported features

`import.get`, `import.set`, `import.reference.set` operate on existing STEP/IGES
features without original files. Properties/placement references commit through
the same GUI operation. Details/limits:
[IMPORTED_FEATURE_COMMANDS.md](IMPORTED_FEATURE_COMMANDS.md).
