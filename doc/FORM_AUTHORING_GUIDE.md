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

Planar FORMs select a sheet face and grow into it.
For planar definitions the authored skin is the starting surface: its datum
coincides with the selected stock face, without a stock-thickness translation.
Thickness is added on the opposite side of the skin using the first surface
side. Keep the authored surface orientation consistent with this intent.
The factory Dimpled Hole and Bridge Lance now follow this same convention.
Author the raised skin on the positive side of the `FORM_CUT` attachment normal;
the planar insertion reverses the source attachment normal while retaining its
X axis and datum. This is a rigid half-turn, not a stock-thickness translation.
Select the opposite stock face for the opposite result.
Keep the cut, flat precut and manufacturing symbol in their corresponding datum
frame. A symmetric surface extrusion may contain separate native shells; FORM
must consume their complete connected skin rather than only its anchor shell.
Planar reference entry retains the initial clicked position: subsequent point,
line and plane references receive signed offsets measured at that position.
Corner entry aligns to the first face normal, then uses the second face to seat
and orient the pending tool in the corner. The third reference locates it along
that already oriented corner. These command-specific drafts consume the shared
placement implementation without changing its general reference contract.
Every planar FORM already exposes its own 10 mm reference axis at the insertion
point, normal to the selected stock face. This is the FORM placement axis; it
does not require an additional centroid axis from the source extrusion.
An ordinary feature double-click exposes its embedded authored Sketch dimensions
in this same frame. Double-clicking a driving dimension edits only the inserted
FORM's embedded native definition and recalculates its dependent geometry.
Reference dimensions remain read-only. Invalid solves or calculations are
rejected atomically; unchanged values do not calculate or create Undo entries.
The library file and other insertions remain independent. Ordinary Sketch lock
policy is retained: locking geometry dragging does not prohibit intentional
numeric edits. Single-click selection does not expose dimensions. Placement dimensions retain
their existing editing behavior.
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
