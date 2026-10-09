# Authoring planar Form definitions

This guide describes the retained production implementation. Solid/surface
experiments have not replaced it. Form represents manufacturing intent and
appearance; it does not simulate material flow or local thinning.

## Ventilation window or unpierced planar pocket

1. Copy `config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz` into a new
   native Part. Keep the ordinary Body hierarchy and four role names:
   `FORM_CUT`, `FORM`, `FORM_FLAT`, `FORM_SYMBOL`.
2. In `FORM_CUT`, author a closed cutting profile on the attachment plane.
   It marks the original sheet region to replace, not necessarily a physical
   hole in the finished sheet. Do not substitute an open slit or leave this
   Body empty for a planar pocket.
3. In `FORM`, calculate a connected outside surface shell. For a pocket,
   include its walls and bottom without a ventilation opening. The boundary
   must meet the replacement region around the full perimeter. Keep the
   current window's attachment frame and surface orientation.
4. Leave `FORM_FLAT` empty when no flat precut is required. Its optional Sketch
   describes an actual flat-state precut, unlike the spatial replacement region.
5. Author an independent manufacturing Sketch in `FORM_SYMBOL`. Save the
   calculated Part and insert it with Sheet Metal > Form.

The command thickens the shell inward by destination sheet thickness, cuts the
replacement region and joins the formed wall. A pocket uses the same planar
workflow as the window, with continuous walls/bottom instead of an opening.
No extra cutting line is needed merely to distinguish a pocket.

Test a new definition at the intended sheet thicknesses: connected material,
no residual hole, wall direction and rim contact, followed by changed-thickness
Regenerate, Unbend/Bend Back, symbols, save/reopen and dependent copies.
This workflow does not establish acceptance of arbitrary pocket geometry;
no specific user-authored pocket fixture was verified in this investigation.

## Corner definitions

The production corner gusset retains its separate closed-solid workflow with
XY/XZ closing faces and empty `FORM_CUT`. Attach it to two outer flat faces
adjoining one Bend. Do not convert the production corner definition to a surface
while preparing a planar pocket. Cut presence or perpendicular-plane count
alone cannot identify the two styles.

See [Sheet Form design](SHEET_FORM_DESIGN.md),
[the experiment](performance/20261009-planar-solid-form-experiment.md) and
[model export](EXPORT_COMMANDS.md). IGES does not preserve native Form roles/history.
