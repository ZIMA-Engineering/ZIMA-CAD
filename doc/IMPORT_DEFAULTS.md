# Native Part defaults for STEP and IGES import

Assembly import creates independent native Part definitions. Each new STEP/IGES
Part inherits the configured start Part's parameter values, ordering and all
localized labels/values and parameter relations. Derived values such as mass
are refreshed from the imported geometry and assigned material using the existing
relation evaluator. Geometry and imported definition identities remain those
of the import; repeated occurrences still share one source definition.

The initial material is S235JR from
`01_steels/structural/S235JR.matz` in the configured Materials library. Properties,
units and descriptions are copied into the native document, so reopening does
not depend on the library file. S235JR is an editable starting assignment, not a
claim that the supplier's imported geometry is made of that steel. If the selected
library cannot supply it, the normal material-library error rejects the import
before files or live documents are committed.

Import into an existing Part preserves its parameters and material. DXF keeps its
existing workflow. Source Part precision and units follow the existing Assembly
import convention; this change does not rewrite geometry or placements.
