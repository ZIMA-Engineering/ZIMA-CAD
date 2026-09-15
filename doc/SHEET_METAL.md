# Sheet Metal

## Implemented document defaults

Select **Sheet Metal** in the application dropdown above the right-hand Part
commands. The panel starts with **Selection**, followed by **Sheet Metal
Properties...**. That shortcut opens **File Settings > Sheet Metal**. Opening
File Settings from Tools accesses the same fields, dialog and transaction.

- **Default material thickness** is optional. Clear **Specified** to leave it
  unset. A specified value is a finite positive number in millimeters, up to
  1,000,000 mm. These units are explicit and independent of display units.
- **Default K factor** is the Part's material property `SHEETMETAL_K_FACTOR`.
  It accepts finite numbers from 0 to 1. When the material has no value, the
  initial default is 0.5; it is not a material-specific manufacturing calibration.

Defaults are stored in the native Part. Thickness uses the shared document
parameter `SHEETMETAL_THICKNESS`; K factor uses the existing material property,
with dimensionless unit `1`. These are existing native metadata stores; no file
format change, sidecar, or template conversion is needed. Loading another material
can change its K factor while leaving the document thickness intact.

OK commits both defaults together. Cancel discards pending edits. The shared
properties-window behavior includes confirmation by middle-button double-click
over the owning View. Undo/Redo and native save/reopen retain the settings.
Changing these defaults preserves the calculated model geometry. The settings
belong to the active Part source, including while editing it inside an Assembly.

Sheet Metal Properties is an editing shortcut, so it is absent from **Insert**.
Assembly file settings have no Sheet Metal page.

## Console

`document.settings.get` returns a `sheet_metal` object for Parts:

```json
{"thickness_mm": null, "k_factor": 0.5}
```

`document.settings.set` accepts partial updates through the same workspace
operation as the GUI:

```json
{"command":"document.settings.set","arguments":{"sheet_metal":{"thickness_mm":2.5,"k_factor":0.42}}}
```

Set `thickness_mm` to `null` to clear the default. Invalid values fail before
changing the document. Unchanged values do not create an Undo entry.

## Agreed first Bend feature — planned

The following design was agreed on 2026-09-16. The Bend geometry command is not
implemented in build 2026091603; the right-hand panel retains its placeholder.

- Bend will use ordinary container placement regardless of the preceding solid's
  type. It consumes the existing placement contract. The first planar reference
  supplies the Sketch plane and the second supplies TOP orientation.
- An owned Sketch starts with a straight segment whose length sets bend width.
  Rotating its thickened profile creates the elementary bend.
- Parameters include inner radius, thickness, angle from 0 to 180 degrees and
  K factor. Thickness and K factor can follow document defaults or use local
  overrides. A concrete Bend requires a thickness even when the document default
  is unset. Material extends from the segment toward the rotation axis; with
  inner radius R and thickness t, the outer segment lies R+t from the axis.
- **Bend** and **Unbend** are states of one history container. Unbend retains
  the specified angle/radius, creates the developed straight material strip using
  the neutral-layer length, and displays a bend-axis line. The proposed line
  location is the center of the developed bend region.
- **Start** and **End** retain their respective persisted face identities across
  both states, allowing later containers to follow those faces after explicit
  regeneration. Their location/orientation may change. OCCT traversal order must
  not define these identities.
- The zero-angle boundary needs an explicit representation without a degenerate
  solid. Hem contact and touching-layer separation will be addressed with future
  hem features, not by adding a hidden clearance to every Bend.

## Verification

Windows Release, 2026-09-16: seven selected contracts passed in 130.26 s
(`build/sheet-settings-tests.log`), including metadata commands, engineering
metadata, Updates UI, full workspace startup, application tools, Surface profiles
and Sketch Offset UI. The application-tools test covers command order, opening
the shared File Settings page, Cancel, confirmation over View and native save.
Metadata tests cover optional thickness, invalid input, unchanged transactions,
Undo/Redo, geometry preservation and save/reopen. The settings screenshot was
inspected. Fourteen packaging/source/signing-input tests also passed.
