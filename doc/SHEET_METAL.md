# Sheet Metal

## Implemented document defaults

Select **Sheet Metal** in the application dropdown above the right-hand Part
commands. The panel starts with **Selection**, followed by **Sheet Metal
Properties...**. That shortcut opens **File Settings > Sheet Metal**. Opening
File Settings from Tools accesses the same fields, dialog and transaction.

- **Default material thickness** is an always-editable numeric field without a
  checkbox. It starts at **1 mm** when the Part has no stored thickness. OK saves
  the displayed value; Cancel leaves the Part unchanged. It accepts finite positive
  numbers in millimeters, up to 1,000,000 mm. These units are explicit and independent
  of display units.
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

## Bend / Unbend

**Bend** is available in the Sheet Metal toolbar and contextual **Insert** menu.
Creation and editing use the same internal properties window, OK/Cancel and
middle-button double-click confirmation. Changes remain transient until OK.
Editing evaluates the existing history boundary before the Bend.

- The command consumes ordinary container placement: the first plane defines the
  Sketch plane and the second defines TOP. The shared placement contract is unchanged.
- Its owned Sketch initially contains a 40 mm straight segment. Edit the Sketch to
  change the width. Exactly one non-construction segment is required; other modeling
  curves are rejected. Construction geometry may support constraints.
- Set the inside radius (at least 0.001 mm) and angle (0–180 degrees).
  Material extends from the segment toward the rotation axis, at outside radius R+t.
- Thickness and K factor follow the Part settings. **Local value** enables an
  override for each separately. A missing Part thickness evaluates as 1 mm.
- **Bend** rotates the authored thickened section; **Unbend** extrudes the same
  section by `angle_radians * (R + K*t)`. It shows an axis at the developed region's
  midpoint. K is a manufacturing input, not inferred from the geometry. The bent
  and flat simplified solids need not have equal volume when K differs from 0.5.
- At exactly zero degrees the feature contributes no material, keeps its history
  identity and can be edited back to a positive angle. It has no selectable faces
  at that boundary. This avoids a degenerate OCCT solid.
- The cyan wire preview is generated analytically without OCCT. OK and explicit
  Regenerate calculate the solid. Document default changes affect existing Bends
  on the next calculation; switching tabs does not regenerate them.

Start and End retain their respective semantic face identities across Bend and
Unbend. Face and rim identities derive from the feature, section, source segment
and endpoints. Their geometry changes while their persisted ancestry stays stable.
Neither OCCT traversal order nor preview edges define persistent references.
Hem contact, automatic sheet recognition and complete-part flattening are outside
this elementary command.

Native Part INI format **28** / JSON payload **52** stores Bend parameters and its
owned Sketch; `config/templates/start_part.prtz` uses that format. Earlier Part
formats are intentionally unsupported. Assembly format is unchanged.

Console commands `bend.create`, `bend.get` and `bend.set` use the same workspace
transaction as the GUI. Creation accepts `width_mm` (default 40), `radius_mm`,
`angle_degrees`, `thickness_mm`, `k_factor`, `thickness_override`,
`k_factor_override`, `state` (`bend` or `unbend`), `name` and `document`.
Editing uses `container` and the same parameters except width; edit the owned
Sketch to change width. Supplying thickness or K enables its override unless the
corresponding override flag explicitly says false. Readback reports effective values.

```json
{"command":"bend.create","arguments":{"width_mm":40,"radius_mm":5,"angle_degrees":90}}
{"command":"bend.set","arguments":{"container":"<id>","state":"unbend"}}
```

## Verification

The Bend command contract covers analytic volume checks, inherited and overridden
parameters, state transitions, stable faces, invalid input, zero angle, Undo/Redo
and native save/reopen. The application-tools GUI contract exercises command
availability, default settings, Cancel, OK, editing the first history feature and
middle-button double-click confirmation over the View.

Windows Release verification, 2026-09-16: all nine selected contracts passed:
Bend commands, Holes commands, metadata commands, application tools, Drawing UI,
dimension layout, shared UI, Family Table and Holes UI. Logs are
`build/bend-drawing-tests.log` (six tests, 12.32 s) and
`build/bend-shared-tests.log` (three tests, 21.99 s). The final Bend rerun also
checks referenced Start/End planes and Family Table angles of 45 and 0 degrees.
The Bend properties, compact Drawing text dialog and text-mask clearance proof
were visually inspected. GUI and CLI targets built successfully.
