# STEP import and export

Other supported imports: [IGES and DXF into Parts and Assemblies](IGES_DXF_IMPORT.md).

Projecting spline edges into sketches: [exact geometry and separate references](SKETCH_EXACT_PROJECTION.md).

## Settings before STEP / IGES import

After file selection, Part and Assembly open the shared internal Import Settings
window. It shows the file name, size and format, and the STEP schema when available
in the initial header. Reading this information does not convert geometry and reads
at most the first 64 KiB of the file.

Display accuracy (deflection in mm) defaults to the corresponding start template
selected in config, rather than the potentially modified accuracy of the open
document. Current templates use 0.1 mm. Values of 1 mm or greater are allowed.
Smaller values produce a finer mesh without changing exact geometry or dimensions.
The OCCT angular criterion also limits the triangle count.

OK starts import with the selected value. Cancel performs no calculation or file
writes. The value is stored in imported containers in the native Part and also
applies during regeneration. It changes neither config, target-document accuracy,
nor previous imports. All newly imported Parts in a STEP Assembly receive the
value. DXF retains its existing import workflow.

### Display-accuracy sanity check

For a STEP cylinder Ø200 × 200 mm, deflections of 0.1, 1 and 5 mm produced 396,
124 and 100 display triangles respectively. The exact calculated volume did not
change. This checks the setting on a simple model; it is not a large-Assembly
import timing measurement.

## Import into a Part

**File → Import → STEP** places each leaf Part / occurrence from STEP into a
separate **Body**, containing a normal imported-STEP container. Repeated occurrences
create additional Bodies at their positions. Existing Part Bodies remain intact;
import does not automatically Boolean-unite them.

Positions from the complete source hierarchy are converted into Part coordinates.
Names and physical dimensions are preserved: for example, 1 inch becomes 25.4 mm.
ZIMA-CAD uses millimetres internally. Display tessellation uses the import accuracy
selected above, with the corresponding template providing its default.

The container stores frozen B-Rep and source topology identities. Saving, reopening
and regenerating therefore do not require the original STEP file.

## Import into an Assembly

Import keeps the current tab and active editing occurrence. Imported Part and
subassembly sources remain available in memory and in their native files, but
do not create document tabs automatically. Explicitly opening an imported source
reveals its tab. This also applies when importing into an activated subassembly:
the top-level Assembly stays displayed. The tab policy is runtime UI state and
does not change the persistent Assembly hierarchy.

The 2026-09-17 Windows import and component-properties GUI contracts verify
background source registration, unchanged tab count and displayed Assembly
during import into an activated subassembly, and exactly one new tab when an
imported source is explicitly opened. See `build/origin-depth-import-tabs-tests.log`.

Import creates one inserted STEP Assembly, preserving its subassembly hierarchy,
individual Parts and local placements. Each unique source Part has one `.prtz`
file and each unique subassembly source one `.asmz` file. Repeated occurrences
share their source. Each imported Part contains a Body with a STEP container.

Files are created directly in the working directory, including when the target
Assembly is saved elsewhere. Names use the import filename stem followed by
`_part-N.prtz` or `_assembly-N.asmz`; collisions receive a numeric suffix.
Existing files are preserved and no automatic subdirectory is created. An explicit
CLI `output_directory` still reserves the requested new directory. Files without
an original hierarchy produce a flat Assembly. A standalone STEP Part is also
inserted through a root STEP Assembly.

One source STEP product remains one Part even when it contains both a solid and
separate surfaces, such as auxiliary screw-thread surfaces. Geometric items within
one product do not create an artificial subassembly. Real subassemblies are
identified by product relationships in the original STEP, not by the number of
solids or surfaces. This grouping also applies to Part import. It takes effect on
new imports; previously saved Assemblies are not automatically rebuilt.

## Export

**File → Export → STEP** uses the document's current calculated state. A Part
exports visible result Bodies separately. Bodies consumed by Booleans are not
exported again. An Assembly preserves subassemblies, Parts, repeated definitions,
names and placements. Hidden and suppressed components are omitted. Export writes
millimetre units and changes neither the model nor its history.

The initial implementation used saved Assembly geometry snapshots and required
Assembly **Regenerate** to refresh source changes. Current source-display ownership
is defined in [Assembly geometry sharing](ASSEMBLY_GEOMETRY_SHARING.md): source Parts
own their current calculated geometry, while Assembly mate solving and owned
operations still require explicit regeneration. A document missing geometry required
for export reports the need to regenerate.

The STEP changes do not add nested-Assembly support to STL export. Importing face
colours remains a separate follow-up task.

## Verification

`zima_cpp_step_model_contract_tests` performs real STEP writes and reads through
OCCT. It checks two Part types, a repeated subassembly, rotations around all three
axes, global geometry bounds, volumes, separate Bodies on repeated import,
Assembly save/reopen, inch-based STEP, and Part regeneration after deleting the
original STEP file.
