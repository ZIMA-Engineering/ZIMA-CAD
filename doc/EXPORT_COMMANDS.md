# Export through the shared command layer

GUI menus and console/CLI share `workspace::export_file` for models/sketches and
`drawing_render` for Drawing PDF/DXF. Export uses the open document's current
calculated state without Regenerate, loading changed source Parts, model mutation
or Undo steps. Source-geometry ownership is described in
[ASSEMBLY_GEOMETRY_SHARING.md](ASSEMBLY_GEOMETRY_SHARING.md).

## Commands

- `export.step path [overwrite] [document]`
- `export.stl path [overwrite] [document]`
- `export.dxf path [sketch] [overwrite] [document] [sheet]`
- `export.pdf path [overwrite] [document]`
- `export.image` with JSON `path`, `sheet`, optional `dpi`, `crop_mm`, `quality`,
  `overwrite`, `document`

Optional `document` verifies the active document. Boolean `overwrite` defaults to
`false`. Relative paths use the console working directory. Extensions must match;
STEP accepts `.step`/`.stp`. Parent directories must exist. GUI uses its file dialog's
overwrite confirmation.

```json
{"command":"export.step","arguments":{"path":"results/assembly.step"}}
{"command":"export.stl","arguments":{"path":"results/part.stl","overwrite":true}}
{"command":"export.dxf","arguments":{"path":"results/outline.dxf","sketch":"SKETCH_ID"}}
```

Results contain `document`, absolute UTF-8 `path`, `source_revision`, `bytes`,
`model_changed:false`. Geometry uses mm; DXF writes `$INSUNITS=4`. STL has no unit
header and uses mm coordinates. Existing STL tessellation uses 0.1 mm deflection
and 0.5 rad angular limit, without changing model display or saved meshes.

## Scope and limits

STEP preserves Part and nested-Assembly product structure and consumes calculated
occurrence results in the open document. The initial implementation required parent
regeneration to obtain source changes. Current shared-source display follows
[Assembly geometry sharing](ASSEMBLY_GEOMETRY_SHARING.md); Assembly-owned operations
and mates still require explicit regeneration.

STL supports Parts and nested/repeated Assembly occurrences, flattening them into
one mesh without names/product hierarchy. Sketch DXF supports segments, axes,
standalone points, circles, circular/elliptical arcs, ellipses and B-splines, including
saved offsets/trims and owned profiles. Corner rounds export as exact circular arcs
with tangent-trimmed segments. Text exports saved closed outlines as `LWPOLYLINE`,
preserving holes, rotation and flipping. These are letter outlines, not editable
DXF TEXT. Invalid shapes are rejected before writing.

Drawing `export.pdf` exports all sheets; `export.dxf` requires `sheet` for one sheet
and rejects `sketch`. Model DXF instead requires `sketch` and rejects `sheet`.
Existing positional sketch-export order is preserved; use named JSON arguments for
Drawings. Drawing DXF represents saved paper-mm projections with outlines converted
to segments, unlike exact sketch DXF. See [DRAWING_COMMANDS.md](DRAWING_COMMANDS.md).
`export.image` provides PNG/JPEG sheets/crops at explicit DPI. At this stage,
interactive-View PNG/JPEG remained GUI adapters pending explicit-camera commands;
current View export is described in [VIEW_EXPORT_COMMAND.md](VIEW_EXPORT_COMMAND.md).
GUI Drawing JPEG shares the atomic image encoder.

## Writing and errors

Workers own input snapshots and write completed output in private temporary
subdirectories beside targets, publishing only after success. Failure preserves
existing target files. Without `overwrite`, concurrent target creation during export
is also rejected. Temporary files are cleaned after success/failure; no permanent
sidecar format is introduced.

STL uses OCCT streams and `filesystem::path` because OCCT 8's filename overload
opened a narrow `std::ofstream` and failed on Czech Windows directories. STEP/STL/DXF
have Czech-path regressions. Real CLI routes OCCT messages to stderr, keeping JSON
on stdout.

## Verification

Model tests independently check STEP volume after reimport and closed-STL volume
by integrating binary triangles: a 10×20×30 mm box is 6000 mm³. They check original
and explicitly regenerated state, inserted Assemblies, DXF circles/arcs, incomplete
geometry rejection and target conflicts. Process tests export after Part close/reopen
with an invalid Qt platform. GUI uses console plus actual menu/file-dialog actions.

Final full Windows Release: **72/72**, 397.87 s, `build/export-full-tests.log`.
GUI/CLI builds matched that state. Catalog at this milestone: 108 commands.

## Nested-Assembly STL

`export.stl` and GUI share a saved-component snapshot. Workers compose a transient
body using existing `assembly::calculate_component_body`, without changing numeric
placement or mate solving. Transforms compose from Part through all owners to the
target Assembly. Repeated sources remain separate mesh copies. Hidden, suppressed
and dependency-suppressed branches are omitted.

If a component owns a completed cut/derived-copy body, that result is exported
instead of its original uncut descendants. Otherwise saved child bodies are selected
by occurrence ID. Missing visible geometry returns `calculation_required`; empty
visible results return `empty_geometry`. Traversal uses the existing component depth
limit 256.

Export neither opens sources nor solves mates/recalculates history. Worker OCCT only
composes calculated bodies and triangulates for export. Transient bodies are not
saved to Assembly or history/shared snapshots. Cost depends on B-Rep complexity;
0.1 mm deflection and 0.5 rad angular limit are unchanged.

Regression independently reads binary STL for two 10×20×30 mm boxes across three
levels, rotated successively around X/Y/Z. It checks all 16 corners, 24 triangles,
normal orientation and signed volume 12,000 mm³. Real CLI/GUI export the same geometry.
Additional cases: hidden/suppressed sources lacking bodies, missing visible bodies,
source closure during workers, a 3000 mm³ cut result and preserving valid output
on failure.

Integration **7/7** (27.02 s), `build/nested-stl-integration-tests.log`; original
STEP Assembly regression **1/1** (1.31 s), `build/nested-stl-step-regression.log`.
GUI used `build/cpp-windows-release/zima-cad-nested-stl-validation.exe` from current
CMake objects while the user's CAD stayed running. Command count is unchanged;
existing `export.stl` was extended.

## Exact sketch curves in DXF

`interchange::export_dxf` was separated from the parser into `dxf_export.cpp`.
GUI/CLI share validation. Files declare `AC1015` and mm; numbers use 17 significant
digits and a decimal point regardless of locale.

Ellipses/arcs use `ELLIPSE`. The writer normalizes the major axis and shifts the
parameter interval if axes swap; normals preserve reversed direction. `SPLINE`
contains control points, knots and optional weights. Fields follow Autodesk
[ELLIPSE](https://help.autodesk.com/cloudhelp/2025/DEU/AutoCAD-DXF/files/GUID-107CB04F-AD4D-4D2F-8EC9-AC90888063AB.htm)
and [SPLINE](https://help.autodesk.com/cloudhelp/2016/ENU/AutoCAD-DXF/files/GUID-E1F884F8-AA90-4864-A215-3182D47A9C74.htm).

Ordinary, interpolated and closed periodic Sketcher splines use existing exact native
B-spline conversion. Exact externally sourced splines retain knots/weights. This
neither samples segment chains nor calls OCCT. Offsets/trims export current saved
visible curves without duplicate support geometry or source linkage. Export refreshes
no external references and changes no sketch parameters/dependencies.

Standalone points use `POINT`; curve centres/control points are not extra entities.
Infinite axes use `XLINE`; finite construction segments remain `LINE`. Construction
geometry uses layer `CONSTRUCTION`, others `PROFILE`. This initial stage rejected
text/corner rounds; later support is below. Subsequent import accepts `ELLIPSE`,
bounded `SPLINE`, `XLINE` and standalone `POINT`; see
[IMPORT_COMMANDS.md](IMPORT_COMMANDS.md).

Tests read actual group codes and independently check swapped-axis ellipses,
reverse-oriented arcs, rational circles, cubic Bernstein polynomials, interpolation
points and periodic closure. Trimmed circles use rational parameters, generally
not angles. Offset tests check exact trimmed endpoints and absence of duplicate
full support curves. Real CLI and GUI console export the same curve set.

Integration **8/8** (26.16 s), `build/dxf-curves-integration-tests.log`; original
interchange contracts **1/1** (0.09 s), `build/dxf-curves-interchange-tests.log`.
Independent **ezdxf 1.4.4** read 12 entities with no errors/fixes and checked rational
circles, cubic polynomials, interpolation, closure and trimmed offsets. Maximum
interpolation-point deviation: 1.12e-9 mm; ezdxf rounded saved knot
0.3333333333333333 to 0.3333333333 during evaluation.
Log: `build/dxf-curves-ezdxf-validation.json`. The parser is a temporary validation
dependency under `build`, not part of application/runtime.

GUI validation used `build/cpp-windows-release/zima-cad-dxf-curves-validation.exe`,
leaving user CAD running. Catalog: 152 commands; native format/templates unchanged.

## DXF text and corner rounds (2026-09-13)

`export.dxf` and GUI share the extended interface. Corners materialize through
`Sketch::evaluated_profile_sketch`, the same algorithm as display/body input.
Original sketches, corner records/IDs and history remain unchanged. Suppressed
corners stay sharp. Two corners on one segment retain independent tangent endpoints.
Original corner vertices and text anchors are editing points, not extra POINT entities.

Each saved text outline writes as a closed 2D polyline with original coordinates/
orientation. No system font or new text approximation is generated. Model text uses
PROFILE; annotation-only text CONSTRUCTION. Reimport creates ordinary outline
segments. Subsequent standalone POINT support permits complete exports to roundtrip.

Text validation and corner materialization finish before opening targets, even in
the low-level writer. Invalid radius, missing outline or infinite coordinates preserve
existing output. Shared worker transactions also protect against write errors.
No OCCT, format change or template update is needed.

Tests check an R2 arc centred at (2,2), tangent-segment length sum 16 mm, R2/R3 corners
on one segment, suppressed corners, text area 24−4 = 20 mm², native save, exact
saved outlines of rotated/flipped Czech text and target protection. Initial roundtrip
radius comparison used exact double equality; using 1e-8 mm tolerance made all
three model/export/interchange tests pass **3/3 in 0.62 s**
(`build/dxf-details-model-tests.log`).

Independent **ezdxf 1.4.4** read 20 entities including 16 closed text outlines with
no errors/fixes, checking radius, tangent lengths and analytical area. Saved radius
1.9999999999999996 mm differs only by normal double rounding.
Log: `build/dxf-details-ezdxf-validation.json`. Parser remains a temporary `build`
validation dependency, not application code.

Both apps/all tests built. Integration **9/9 in 80.73 s**, including real CLI
(24.24 s), full GUI console (53.69 s), interchange, export commands, text, constraints,
curves and translations. Logs: `build/dxf-details-full-build.log`,
`build/dxf-details-integration-tests.log`. Catalog stayed at 234 commands; total
suite became 131 tests. Full 130/130 preceded this step; affected tests were repeated
after writer changes. Standalone DXF point import was the next step.
