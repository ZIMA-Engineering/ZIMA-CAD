# Native Part defaults for STEP and IGES import

Assembly import creates independent native Part definitions. Each new STEP/IGES
Part inherits the configured start Part's parameter definitions, ordering,
localized labels, Relations and unrelated custom defaults. There is no separate
hard-coded parameter schema. Geometry and imported definition identities remain
those of the import; repeated occurrences still share one source definition.

The name parameter uses the imported product definition's name (the source
filename for IGES). Standard, stock, material and mass start empty, replacing
any sample values and localized product-specific values in those fields. The
current importer does not extract verified material, stock or standard metadata
from STEP/IGES. It does not infer them from filenames or geometry. No S235JR or
other physical material is assigned automatically. After an explicit material
assignment, the preserved mass Relation can use its density and actual geometry.
Quantity is the start Part's per-item default; occurrence counts belong to the
Assembly/BOM rather than being written into a shared source Part.

Import into an existing Part preserves its parameters and material. DXF keeps its
existing workflow. Source Part precision and units follow the existing Assembly
import convention; this change does not rewrite geometry or placements.

Template inheritance applies when the import host supplies its configured
document templates, as the normal GUI and configured command host do. A
low-level caller without templates retains its existing bare-default behavior;
the import operation does not introduce a global factory-template path.
