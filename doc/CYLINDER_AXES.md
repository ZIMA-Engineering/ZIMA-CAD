# Cylindrical references and construction axes

Create a construction axis with **Feature > Axis** and the shared **On axis**
reference mode. The separate cylindrical-face-axis command, dialog and
`cylinder_axis` construction definition have been removed. Its old records are
unsupported; there is no migration or compatibility resolver.

The ordinary Axis uses the existing Container Origin placement controls and
persisted analytical cylindrical references. Removing the retired command does
not change those shared placement equations or automatic model axes.

## Assembly placement using cylinders

Component placement accepts a cylindrical source face on either side of an
Axis coincidence row, including a cylinder paired with an existing axis.
The persisted reference kind is `cylinder_face`; its owner, face key and exact
occurrence path identify the original face. Cylinder radius is irrelevant to
coaxial placement, so shaft and bore diameters may differ.

The analytical cylinder supplies a point and direction to the existing axis
equations. Flip, zero-distance semantics, mobility, dependency ownership and
explicit regeneration remain unchanged. Coaxial placement leaves axial
translation and rotation free; additional references can constrain them.
No auxiliary construction or OCCT calculation is needed to select a cylinder.

## Verification scope

The retained `cylinder_face_placement_contract.inc` matrix compares cylindrical
face references with explicit axes, both Flip directions, mixed cylinder/axis
references, missing references, native Assembly save/reopen and command
Undo/Redo. The obsolete standalone construction tests have been removed.

All five localization catalogs have been checked for valid JSON after removal
of the retired dialog strings. Repository factory native assets do not contain
the removed definition. The ordinary Axis extent and surface-placement tests
remain applicable. Runtime regression results are recorded after rebuilding;
source inspection alone does not establish that these contracts pass.
