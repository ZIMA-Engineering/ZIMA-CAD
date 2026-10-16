# Authoring planar Form definitions

This guide describes the current outward library definitions selected by the
user on 2026-10-11. Form represents manufacturing intent and
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

The current Window selects the outer sheet face and grows outside it. Its
`form_outward` intent selects the authored normal side for full stock thickness.
The command cuts the
replacement region and joins the formed wall. A pocket uses the same planar
workflow as the window, with continuous walls/bottom instead of an opening.
No extra cutting line is needed merely to distinguish a pocket.

Test a new definition at the intended sheet thicknesses: connected material,
no residual hole, wall direction and rim contact, followed by changed-thickness
Regenerate, Unbend/Bend Back, symbols, save/reopen and dependent copies.
This workflow does not establish acceptance of arbitrary pocket geometry;
no specific user-authored pocket fixture was verified in this investigation.

## Corner definitions

The current corner uses a closed solid in `FORM_CUT`, followed by the linked
zero-thickness Shell in `FORM`. Attach it to two outer flat faces adjoining one
90-degree Bend. The inserted feature performs solid Cut, full stock-thickness
outward wall creation, exact outer Bend trim, Fuse and transition Fillets at the
FORM–Bend junction in the order `t`, then `0.25t`. Preserve the source solid and
its explicit face ancestry. Two normal prisms extending to the radius overcut
sloped geometry and must not replace the shaped cutter.

The ordinary library contains only `CornerGusset90.prtz` and
`VentilationWindow.prtz`. Historical inward controls live under
`cpp/tests/fixtures/form`. Already inserted definitions remain independent;
adopting a different source requires explicit Replace definition.

See [Sheet Form design](SHEET_FORM_DESIGN.md),
[the experiment](performance/20261009-planar-solid-form-experiment.md) and
[model export](EXPORT_COMMANDS.md). IGES does not preserve native Form roles/history.

## Dimpled Hole and Bridge Lance

`DimpledHole.prtz` preserves the authored spatial revolution and Ø7.4 mm flat
precut. Its two concentric manufacturing circles are construction centerlines.
`BridgeLance.prtz` preserves the tangent arch and rectangular cut footprint;
its FORM_FLAT Body is intentionally empty. Both are independent library copies
with ordinary Body ownership; the working FORM-KOULE and FORM-UCHO files are
not overwritten during factory preparation.

An Extrusion Up to target may be an existing point in a preceding Sketch.
Its current position defines the stop plane perpendicular to extrusion. A
symmetric extrusion mirrors this limit about its profile plane. Changing the
ending to Up to automatically activates reference entry. This point support
belongs to extrusion; rotation retains its existing plane/face target contract.
