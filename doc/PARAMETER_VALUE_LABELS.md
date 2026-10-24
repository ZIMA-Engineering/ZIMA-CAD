# Numeric parameter value labels

Model View parameter inspection uses the existing label-only annotation
presentation for helper values. Ordinary single-click selection still shows the
shared authored wire; Properties and double-click inspection expose parameters.

Helper labels have a localized semantic name, a numeric value and applicable
units. Direction is +1 or -1; counts remain integers; angles and lengths follow
the document display units. The label does not introduce witness or dimension
lines. Actual geometric dimensions retain their established presentation.

The unified helpers cover feature direction, linear/circular pattern spacing,
counts and angle, Helical Sweep pitch, and Up To length/angle corrections.
Multiple linear pattern directions retain their numeric direction index.
The raw values, quantities, semantic keys and dimension identifiers are retained;
Relations and Family Table never depend on the translated label. Existing locks,
validation and read-only derived full-circle angles remain in force.

Czech action names are `Tažení`, `2D tažení`, `3D tažení`, `Přechod`, `Krut`
and `Špička`; English uses `Extrude`, `2D Sweep`, `3D Sweep`, `Loft`, `Twist`
and `Drill Point`. Newly generated names follow the selected language. Existing
user-authored names and native enum identifiers are preserved.

The unified public CLI remains `feature.create`, `feature.set` and `feature.get`.
Its side-operation spelling is `extrude`; `extrusion` is rejected at the public
boundary. Native serialization continues to use its existing enum spelling.
The CLI fixtures, library generators, examples and packaging smoke use the new
public spelling. `twist` and `drill_point.*` retain their canonical English names.
