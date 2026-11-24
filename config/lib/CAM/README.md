# CAM library definitions

These are ordinary editable native Parts. Open a definition in ZIMA-CAD to edit
its Bodies, Sketches, Parameters and Relations. Tool files use `.toolz`; machine
and workholding definitions currently use `.prtz`. Nested folders are supported
by the ordinary file manager. The CAM page in global settings contains separate
machine, tool and workholding directory paths, resolved relative to configuration.

- `Tools/Milling/End_mill_10.toolz`: generic three-flute end-mill envelope,
  cutting diameter 10 mm, cutting length 22 mm, shank diameter 10 mm and overall
  length 72 mm. The yellow cutting Body extends from Z=-22 to 0; the red shank
  extends from Z=0 to 50. This is a rotational machining envelope, without flute
  detail. The common origin is the cutting/shank interface.
- `Fixtures/Vises/Parallel_jaws.prtz`: two closed, named contact Sketches in one
  ordinary Body. Each is 101.6 mm wide and 44.5 mm high. The fixed contact is at
  Y=0, the moving contact at Y=`cam_opening_mm` (initially 100 mm), and both top
  edges are at Z=0. This is intentionally a reference-only definition, without
  a solid vise or a claimed physical mass.
- `Machines/Milling/Three_axis_mill.prtz`: generic three-axis example with travel
  from -500 to +500 mm on each axis, maximum spindle speed 12000/min, maximum
  working feed 10000 mm/min and rapid feed 20000 mm/min. These are editable
  example limits, not the specification of a particular machine. Its empty
  active Body is available for optional presentation geometry.

All standard start-Part Parameters remain available. `stock`/Polotovar and
Norma retain their descriptive meanings; users may enter a tool description,
dimensions or manufacturer there. CAM numeric parameters are separate and are
not parsed from those descriptions. No manufacturer is assigned to these assets.
Dimensional tool parameters drive the native geometry through Relations.
Changes take effect through explicit Regenerate. Cutting settings are manually
authored; there is no automatic material database or implied speed/feed adjustment.

The supplied cutting preset describes conventional side milling of cast aluminium
(ISO N 23-25): 7767/min, 2663 mm/min, 244 m/min, 0.114 mm/tooth, axial depth 15 mm,
radial engagement 5 mm. The source publishes rounded values independently; they
are retained as published rather than forced into exact equality. These example
values must be adjusted for the actual tool, machine and cutting conditions.

The dedicated mm/in start-tool templates create fresh cutting/shank Body IDs,
activate the cutting Body and preserve ordinary Part ownership. Role parameters
store stable Body/Sketch IDs independently of their editable display names.

This checkpoint covers native definitions, editing and paths. Selection/import
into a CAM setup, validation of authored envelopes, clamping-Sketch collision
checks and the tool/operation editor are subsequent integration work. A Sketch-only
vise must not be treated as a validated solid collision fixture by the current
calculation path.

Authoring sources and verification are recorded in
[`doc/verification/20261124-cam-library-definitions.md`](../../../doc/verification/20261124-cam-library-definitions.md).
