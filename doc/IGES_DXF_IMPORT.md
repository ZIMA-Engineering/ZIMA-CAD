# IGES and DXF import

Implemented 2026-09-09. **File → Import** operates on the document being edited,
including an activated Part or subassembly inside an Assembly. BREP import is
outside this change. STEP retains its product-hierarchy importer, described in
[STEP import/export](STEP_IMPORT_EXPORT.md).

## DXF: Part → Body → Sketch → import block

- Without an active Body, create a new Body containing a Sketch.
- With an active Body, insert a new Sketch at its current history position.
- Inside an active sketch, add the import block to that sketch.
- In an Assembly, create a new Part containing Body → Sketch → DXF block and
  insert it into the Assembly being edited. An activated Part inside an Assembly
  follows Part rules; it does not create another Part.

A new Sketch uses the Body's local XY plane. Its placement can later be edited
through normal Sketch/Body Properties. Only the immediate owning Assembly positions
a component. The shared placement contract is unchanged.

An import block contains ZIMA Sketcher geometry without the original CAD history
or a live DXF link. Existing Sketcher tools can transform it. Reopening the Part
does not require the original file. DXF creates sketch geometry for modeling,
not a solid Body.

### Native reader and scope

The reader is implemented in C++, without a paid converter or DXF parsing library.
It reads text DXF model space:

- `LINE`, `CIRCLE`, `ARC`;
- `LWPOLYLINE` and 2D `POLYLINE` / `VERTEX` / `SEQEND`;
- open and closed polylines, including positive and negative `bulge` arcs;
- `$INSUNITS` units, converted to internal millimetres. With missing units or
  value 0, the UI assumes 1 unit = 1 mm.

Supported geometry is planar XY with zero elevation and thickness and a +Z normal.
Other configurations are rejected as geometrically ambiguous instead of silently
flattened. Invalid numbers, incomplete pairs, invalid polylines and more than
100,000 source records are rejected without changing the target sketch.

`INSERT`/block references, `ELLIPSE`, `SPLINE`, text, dimensions, hatches, 3D meshes
and binary DXF are not yet supported. Unsupported entities are skipped with a
warning; if no supported geometry remains, no new Part/Sketch is created. Paper
space is not imported. Lines on the `CONSTRUCTION` layer are construction geometry.

## IGES

The OCCT reader handles `.igs` and `.iges`. In a Part it creates a new Body with
an imported-geometry container. One file produces one import container. Surface
and wire geometry retain their type; no substitute solid is manufactured. This
version does not split IGES into a STEP-like product hierarchy.

In an Assembly, import creates one source Part containing this Body and inserts
it as a component. Existing target Part/Assembly contents remain intact. IGES
cannot be imported inside an active sketch. IGES export is not implemented.

Import explicitly calculates frozen B-Rep, the display mesh and the reference map.
Identity derives from the original IGES directory entry and semantic role; split
source objects add a distinguishing geometric locator. Shared edges/vertices use
the most specific available source entity, breaking ties by the lowest directory
pointer. OCCT traversal order never defines identity. Ambiguous locator matches
are not offered as persistent references.

The existing frozen STEP-import mechanism is reused (`ImportedStep`,
`imported_step`, and `StepRequest` remain internal names of the shared storage).
The source path and `iges:` identity prefix distinguish the format. There is no
second editing, placement or persistence implementation. Opening and references
consume saved ZIMA data; Regenerate uses saved B-Rep without the original IGES.

## Source Parts in an Assembly

The new `.prtz` is created beside the target Assembly, or in the working directory
for an unsaved Assembly. Name collisions receive numeric suffixes. Import does not
replace the displayed top-level Assembly. Sketch Parts are visible in nested
Assemblies too. Display refresh picks up current calculated source Part geometry
without OCCT. Mates and Assembly-owned operations run only on explicit
**Regenerate**, never on a tab switch.

## Verification

`zima_cpp_import_model_contract_tests` checks:

- new/active Body, active Sketch, independent blocks and persistence;
- real extrusion of a 20 × 10 mm DXF rectangle by 5 mm (volume 1,000 mm³);
- polylines, negative bulge, inch-to-mm conversion and atomic invalid-data rejection;
- sketch-Part insertion into Assemblies, nested ownership and explicit regeneration;
- a real external DXF (22 entities) and a 10 mm IGES cube (volume 1,000 mm³);
- an IGES box, 26 topology identities, save and regeneration without the source file;
- an IGES wire and rejection of a damaged file.

Small external samples, exact source revisions, checksums and licences are in
[cpp/tests/fixtures/import](../cpp/tests/fixtures/import/README.md).

Windows Release verification: all 27/27 tests passed. After the final block-
independence and IGES-highlighting changes, all three affected suites (import
model, interchange, viewer) passed again. The application was rebuilt.

## Comparison of supplied STEP and IGES (2026-09-11)

Files examined in `Projects/import`: `ze0026-0000-0000.stp` (9,092,072 B) and
`ze0026-0000-0000.igs` (37,994,290 B). These findings concern the supplied export,
not all IGES files.

Reading source IGES entities and independently inspecting directory records confirmed:

- 501 solids (type 186), 13,506 source faces (510), and 738 shells (514).
- 511 named groups (402): 10 contain other groups and 501 are leaves.
- No subfigure definition/instance entities 308/408.
- Each of the four `ZE0026-0101-9001` screw groups (including `_1`, `_2`, `_3`)
  contains one solid, 28 source faces and two shells, plus auxiliary curves and
  edge/vertex lists. Shell count alone does not identify separate surface bodies:
  a shell is also part of a solid.

Current `import_iges_part` always creates one Part with one import container for
the whole file. It does not turn named groups into an Assembly hierarchy. STEP
recognizes 85 unique Parts with repeated occurrences in this model. Thus the same
model is organized differently in the two interchange formats. Matching names or
removing numeric suffixes does not prove shared source-Part identity.

Recommended next IGES steps:

1. Adopt the STEP semantic rule: one source Part/group contains its solid and
   auxiliary surfaces, such as screw threads.
2. Convert actual parent-group relationships to Assemblies. Multiple geometric
   objects within a Part are not sufficient reason to create a subassembly.
3. Select owned solids and remaining independent surfaces/wires within each group.
   Do not reinsert faces and auxiliary entities already contained in a solid.
4. Reuse native persistence, geometry sharing and viewer references. Preserve IGES
   source identities; share repeats only when a common source is demonstrated,
   never inferred from names.

This IGES splitting is not implemented yet. The shared frozen-import path already
uses the corrected archived topology relationships described in
[geometry sharing](ASSEMBLY_GEOMETRY_SHARING.md).

### Actual conversion through the current importer

Local diagnostics called production `OcctKernel::import_iges` with 0.1 mm mesh
deflection, without modifying source or user documents.

- Import before fixing reference-archive lookup: 887.58 s (about 14.8 min).
- Result: 501 solids, 738 shells, 27,012 faces, 132,690 edges and 168,358 vertices
  (unique OCCT objects by kind).
- Display mesh: 2,271,821 triangles; B-Rep: 73,347,359 B.
- Offered original references: 209,767 face, edge and vertex identities in total.
- After import: working set 1,416.36 MiB, private memory 1,521.44 MiB, peak working
  set so far 1,825.23 MiB. These are not whole-GUI Assembly memory figures.

Separate OCCT-only diagnostics read the file in 0.81 s, completed `TransferRoots`
at 37.35 s and validity checking at 46.21 s. Conversion returned six roots and
valid geometry. Reading and transferring IGES alone therefore do not explain the
full elapsed time. Subsequent reference capture, meshing, measurement and viewer-
data preparation need separate profiling. The code repeatedly calculates geometric
locators for children of parent source groups; its exact time contribution has
not been measured. Short separate diagnostics ran on the same computer during the
full import, so these are not isolated STEP-versus-IGES speed benchmarks.

Direct checks of the same converted screw showed 26 faces in its solid and 28
across its shells, exactly two outside the solid. All 28 independent source face
entities already belong to these shells, yet the complete converted group contains
56 faces. Displaying every geometric group member as Part content is therefore
incorrect.

A follow-up audit of original entity types 120/122/128 identified the 28 excess
faces as support surfaces. All 28 belong to the converted group, but none is
identical to a face in the resulting shells. Correct screw import should retain
the 26-face solid and two independent auxiliary surfaces; support surfaces must
not create 28 additional displayed faces. The fix must respect IGES ownership and
dependencies, rather than generically deleting similar/coincident surfaces by a
geometric hash.

The investigation found quadratic lookup in the new `persist_imported_topology`:
for each owned face/edge/vertex, `find_if` rescanned the entire identity table. A
single semantic-key index per topology kind replaces it. Source identities,
archive format and IGES grouping rules are unchanged. The 887.58 s figure above
was measured before this fix.

Checking frozen B-Rep in that run restored all 209,767 offered semantic identities
unchanged (`references_equal=1`). The complete diagnostic run, including viewer-
data recalculation, took 2,106.35 s with peak working set 3,769.64 MiB. This proves
preservation of captured references, not correct IGES grouping or complete
references for every excess face.

A repeated import after adding the archive-identity index took 1,061.30 s and
returned identical counts of solids, faces, edges, vertices, triangles and offered
references; B-Rep again measured 73,347,359 B. Working set after import was
1,423.56 MiB, private memory 1,541.23 MiB. The subsequent repeated restoration was
stopped diagnostically; the complete roundtrip is demonstrated by the first run
above and small regression tests. Concurrent tests and diagnostics prevent an
isolated speed comparison: this repeat does not demonstrate faster total import.
The index removed a specific quadratic algorithm, but dominant conversion costs
still need measurement by phase.
