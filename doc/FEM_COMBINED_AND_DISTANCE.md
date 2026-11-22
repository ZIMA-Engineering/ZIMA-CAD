# Combined thermal-mechanical analysis and distance measurement

## Analysis workflow

In FEM Body properties, select **Thermal-mechanical analysis**. Set the ambient
temperature and exterior heat-transfer coefficient. The source Part material
supplies conductivity, Young's modulus, Poisson ratio, thermal expansion
coefficient and the stress-free reference temperature.

Define heat sources, forces, pressure and mechanical restraints in the same FEM
Body. Heat power and pressure can use Body faces or bounded planar/projected
Sketch regions. Generate the volume mesh and run Calculation. The steady-state
temperature field supplies thermal strain to the mechanical calculation, whose
right-hand side also includes force and pressure. Results offer temperature,
displacement and element-average von Mises stress. Measurement points report
temperature and the combined displacement components.

The separate **Steady-state thermal analysis** mode retains its existing
behavior: optional thermal expansion includes mechanical restraints and excludes
force and pressure. Switching analysis modes changes the required mesh regions;
generate the mesh again. Changing a load value reuses compatible mesh geometry
and invalidates the previous solution.

This is one-way, small-strain, isotropic linear thermoelasticity with constant
material properties. Deformation does not feed back into heat transfer or load
geometry. It does not implement contact, plasticity, temperature-dependent
properties, transient heat transfer or actual dynamics. Animation illustrates
the static displacement field.

## Measuring a change in distance

Create two **Measurement point** objects, then choose **Distance measurement**
from the right FEM toolbar. Select the points in the Tree or View. Both points
must belong to the same FEM Body. The second reference is armed automatically
after entering an empty first reference. Clicking a reference field replaces it;
its independent eye inspects the referenced point. A short middle click ends
reference entry without deleting either reference. OK, including the shared
middle-button double-click, commits; Cancel discards the draft.

The result label reports:

- Original distance between the two undeformed positions.
- Deformed distance using the interpolated displacement at each point.
- Signed difference: positive for extension and negative for contraction.

Values use physical millimetres and are independent of display magnification,
animation phase and the optional transparent original. Hidden measurement points
remain usable. Creating or editing a distance reuses the existing mesh and
solution. It does not impose a constraint, add a load or partition the mesh.

A missing or unresolved point reference retains both last valid endpoint
positions for display and repair. It reports an unavailable reference rather
than stale result values. A point outside the FEM volume reports that condition;
it is never snapped to a nearest node. Pure thermal results without expansion
report that mechanical displacement is unavailable. Replace a missing reference
in Properties to resume measurement.

## Native storage and numerical treatment

The approved `.prtz` extension stores the `thermal-mechanical-static` analysis
kind and a `measurement-distance` condition. The latter references two stable
measurement-point IDs and retains their last valid original positions and
reference state. It is a derived measurement, not a placed container; it does
not change the shared container-placement contract. All required data stays in
the native document. Meshes, solutions and point interpolation caches remain
disposable derived data.

With original relative position `d = p2 - p1` and relative displacement
`du = u2 - u1`, the lengths are `L0 = |d|` and `L1 = |d + du|`. The implementation
evaluates the difference as `(2 d·du + du·du) / (L0 + L1)`, with a zero-length
guard and division before multiplication. This avoids subtracting two nearly
equal lengths or adding small displacement to large absolute coordinates before
taking the difference. Both point values use the existing T4/T10 interpolation.
