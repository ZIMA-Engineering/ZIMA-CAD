# Mass, units, and Parameters

## Calculation

Body calculation uses millimeters. Persisted volume is in mm³ and area in mm².
Changing document length units does not resize geometry; `model.volume` and
`model.area` convert to the third and second powers of the document length unit.

Part mass is **volume x material density**. `MASS_DENSITY` and its own unit belong
to document material data. Supported density units are `kg/mm^3`, `kg/m^3`,
`g/cm^3`, and `lb/in^3`. A density number without a known unit is not verified data.

`model.mass` uses **File Settings -> Mass** units (`kg`, `g`, `t`, `lb`).
`material.density` uses the same mass unit per cube of the selected length unit.
For example, a 100 x 100 x 100 mm steel cube at 7850 kg/m³ has volume
1,000,000 mm³ and mass 7.85 kg = 7850 g. Unit changes must not change physical mass.

New documents inherit units from the applicable `config.ini`, including local
working-directory settings. Existing documents use their persisted units.
File-setting `decimal_places` controls textual result precision.

Configured units take precedence over start-template unit metadata when creating
a Part, Sheet Metal Part, Skeleton or Assembly. Templates supply structure and
precision; opening an existing document retains that document's saved units.
The built-in first-run setup wizard is a separate planned feature.

## Modeling input and manufacturing annotations

Supported length units are mm, cm, m and in; angles use degrees or radians.
Model property fields, Sketch input, View labels, symbol coordinates and Family
presentation use the active writable document's units. Canonical lengths and
angles stay in mm/degrees. Counts, scale and K factors remain dimensionless.
Thread designations/catalogs, kernel tolerances, sweep approximation and import
mesh deflection retain their existing physical meanings. Sheet settings explicitly
label the reserved thickness parameter and cut tolerance in mm.

Inline View and Sketch dimension entry accepts an arithmetic expression with
one optional trailing unit. For example, `0,254inch` in a millimetre document
stores 6.4516 mm; `25.4mm` in an inch document displays 1 in. Decimal commas and
points are accepted. An explicit input unit affects that value only. Invalid or
quantity-incompatible input does not replace the stored value. Unchanged rounded
text preserves the original canonical value rather than rounding the model.

An authored manufacturing dimension retains its own annotation unit, decimal
precision, trailing zeros and tolerances. Exact finite conversions preserve the
complete specification. When exact display conversion is unavailable, the
original specification remains authoritative with a marked approximate secondary
nominal. Display precision never changes kernel accuracy or an acceptance limit.
Source unit changes do not rewrite Drawing tolerances. Paper sizes, annotation
placement, pen widths and exported Drawing DXF use paper millimetres; view scale
does not multiply measured model values. STEP exports physical millimetres;
STEP/IGES imports respect declared source units. IGES is import-only.

Time, temperature and stress settings are currently persisted unit metadata;
there is no numerical modeling consumer for them. Arbitrary user parameter or
symbol text is not parsed and converted as a physical quantity.

## Relations and history

Default templates contain `mass = model.mass`. Physical inputs such as
`model.mass` read the current calculated geometry and material data. Relations
publish their results to Parameters only on explicit **Regenerate**; a material
edit, document opening or tab switch does not evaluate them. Explicit document
unit conversion rewrites quantity-bearing relations and converts their existing
numeric outputs together. Parameters stores one value shared by all languages.
Undo/Redo restores model data, units and calculated parameters together.

Physical queries read persisted volume/area without OCCT. Refreshing these inputs
does not assign relation-driven dimensions. Drawing reads the last published
parameter; relation-driven mass cannot be overwritten manually through the title
block.

## Assemblies

Assembly sums physical mass across all unsuppressed occurrences. Repeated insertions
count repeatedly. Hiding a component does not remove its mass; suppression does.
Each nested sum converts through kilograms into its owner's units.

Part density and inserted-subassembly mass accompany the component's current
calculated data. Ordinary occurrences share current open-source geometry; source
refresh also updates the associated mass and its volume basis. Repeated and nested
occurrences must receive the same current physical data without creating an Undo
entry, evaluating relations, solving mates or running OCCT. Source document units
are presentation metadata, never an occurrence scale. Unsaved source geometry and
material changes are authoritative.

Assembly-owned cuts keep their calculated results until explicit **Regenerate**.
Refreshing a source does not overwrite that operation boundary or assign an exact
post-cut mass from an uncut heterogeneous source.

Missing/invalid density is not treated as zero: dependent mass is unavailable and
the title block shows a placeholder. Volume ratios cannot establish exact mass
after cutting a subassembly containing different materials. If such a cut changes
the composite snapshot volume, mass is unavailable because post-cut material-volume
partitioning is not yet implemented. A cut into an immediate Part uses its density
and actual remaining volume.

## Title block

`&document.mass_unit` displays the source-document unit. Supplied title blocks use
`[&document.mass_unit]` instead of fixed `[kg]`, keeping value and unit consistent.
BOM rows also carry their source units. Editing details: [Drawings](DRAWINGS.md).
