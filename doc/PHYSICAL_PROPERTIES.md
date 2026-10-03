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
