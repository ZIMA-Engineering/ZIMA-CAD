# Sketch properties through GUI and CLI

GUI and commands commit the entire Sketch and its placement through
`commit_sketch_properties` for both Part and Assembly. A standalone Assembly
Sketch has its own persisted container in `.asmz`; editing it does not calculate
component mates or cuts. See [Assembly Sketch properties](ASSEMBLY_SKETCH_PROPERTIES.md).

## Commands

`sketch.set` accepts `sketch` and optional `name`, `plane` (XY/XZ/YZ),
`plane_offset_mm`, `placement`, `back`, `quarter_turns` (0–3), and `document`.
Offset is in mm and must be between −1,000,000 and 1,000,000 mm.
`placement` is an object of shared placement numerical fields, for example
`{"x":10,"y":20,"z":30}` or `{"reference_offset:0":5}`. Numerical input cannot
overwrite locked or reference-driven fields.

`sketch.reference.set` accepts `sketch`, `index` (0–4), an original `reference`
with `owner` and `key`, and optional `offset_mm`, `flip`, and `document`.
The first plane source defines the work plane. Later position rows preserve its
orientation under the same rules as Sketch Properties. Original identity is not
derived from OCCT edge or face order.

Standalone containers also support general `placement.reference.set` and
`placement.reference.remove` using their `object` ID. `sketch.get` additionally
returns `plane_offset_mm`; `placement.get` reads full placement. Reads use no OCCT.

## Transactions and ownership

Name, plane, offset, and placement are committed in one Undo/Redo step. Geometry
and identities are preserved. GUI Cancel leaves the original document unchanged.
Invalid sources, sources beyond the history boundary, inactive or derived bodies,
and unsolvable placement are rejected before publication.

For an Extrusion- or Revolution-owned Sketch, the Sketch offset and owner's offset
parameter update together. OK and command mutations explicitly calculate the Part.
Creating a standalone Part Sketch uses the same transaction. No-ops add no history.

The work-plane rule moved unchanged from the dialog into
`document/sketch_placement.hpp`. The shared placement solver is unchanged.
This stage does not change the Part format or start Part.

## Verification

The model test covers ordinary and rotated Parts, standalone and owned Sketches,
original planes, invalid/forward references, locks, Undo/Redo, native saving, and
reopening. It independently checks the volume of a cylinder from an R3 circle
and length 5 against 45π mm³, and checks its position. The GUI test compares
complete saved Sketch and container definitions with CLI results after Cancel/OK
and reference-offset editing.

The dialog now commits and displays independent FRONT/TOP rows too. Previously,
it collected only position rows and discarded orientation fields on OK. The test
also compares independent FRONT without a position-plane reference. A new Sketch
working frame is solved before Extrusion/Revolution calculation; the position
check caught missing frame preparation in the new transaction.

Both applications and all tests built. Final related regression passed **12/12 in
244.41 s**: GUI profile frames, dialog contracts, translations, separate CLI process,
Sketches, reference removal, owned profile Sketches, profile operations/references,
new Sketch Properties, command catalog, and GUI console. This Part stage had 291
commands and 158 tests; subsequent Assembly verification is in the
[separate overview](ASSEMBLY_SKETCH_PROPERTIES.md).
