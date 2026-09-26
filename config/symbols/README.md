# Symbol library

Library structure for reusable symbols:

- `surface-texture`: surface texture and roughness indications.
- `welding`: welding symbols.
- `geometric-tolerances`: form, orientation, location and run-out indications.
- `machine-elements`: machine-element symbols and reusable representations.
- `drawing-conventions`: projection methods and other drawing conventions.
- `general`: other symbols without a more specific category.

The projection asset is `drawing-conventions/ZE-PROJECTION-METHOD.symz`, containing
first-angle and third-angle variants in separate coplanar Sketches. Native
version 2 supports named text fields, choices and variant rows. Sketcher
**Insert symbol** embeds an independent occurrence in the owning Sketch;
its geometry never participates in solid profiles. Title-block occurrences
can select their variant from the owning Drawing sheet's projection method.
The library directory is configurable under Global Settings / Paths / Symbols
(`Paths/Symbols`, default `symbols` relative to the configuration).

Definition authoring and model-surface attachment remain separate development
steps. See `doc/SYMBOLS_DESIGN.md` for implemented scope and pending work.

Initial list/custom-text examples are also available:

- `surface-texture/ZE-SURFACE-TEXTURE-ISO21920.symz`: three surface-texture
  process variants for individual surfaces and a configurable specification.
- `general/ZE-GENERAL-SURFACE-TEXTURE-ISO21920.symz`: a separate
  title-block/general indication, with the default requirement followed by a
  bare symbol in parentheses. Three manufacturing-process variants are offered.
- `general/ZE-GENERAL-EDGES-ISO13715.symz`: all, external, internal or combined
  external/internal edge requirements; each scope can have no exception, one
  specified exception or a bare symbol representing multiple exceptions.

All five factory title blocks embed independent copies of the general surface
texture and edge definitions. Texture strokes are yellow; local texture text
and general title-block texture text are green. All colours are stored
in the embedded definition and respected by Sketcher and Drawing rendering.
Ra 3.2 and the signed edge values are editable
factory presets, not requirements prescribed by the standards. Existing Drawing
copies are not automatically replaced when a library or title block changes.

Projection outlines and edge-symbol strokes that are not leaders use the thin
green pen. Projection axes are finite: the cone has its own horizontal axis,
and the circular view has four independent radial axis segments. The factory
title-block occurrence is anchored at X=47.8 mm, 3 mm to the right of its previous
position in the drawing's right-to-left coordinate convention.

Factory title blocks own their tolerance fields. The default references are
`ISO 2768-m` and `ISO 8015:2011`; Part, Skeleton and Assembly start templates
do not carry `general_tolerance` or `tolerancing` parameters. The editable Accuracy
list offers ISO 2768 dimensional classes f/m/c/v. Geometrical tolerances must be
prescribed separately; ISO 8015 specifies GPS principles. See
[`TITLE_BLOCK_TOLERANCES.md`](../../doc/TITLE_BLOCK_TOLERANCES.md).

These examples can be inserted into a Sketch or title block. The general texture
composition is intended for the title block; the local texture asset describes
individual surfaces. They do not yet
provide model-surface attachment or an exhaustive standard-symbol catalog.


The historical `surface-texture/ZE-SURFACE-TEXTURE-ISO1302-1978.symz` contains
two variants: unspecified process and required material removal. Its editable text above the bar defaults to `3,2` (optional `Ra` prefix) and enables the ordinary per-text Drawing readability option. Authors can set
this same option in Symbol Sketch Text Properties; no symbol ID is required.
See `doc/SYMBOLS_USER_GUIDE.md` for orientation and scope.

The geometric-tolerance catalog contains fourteen dynamic frame definitions.
The welding catalog contains fillet, square-butt, V-butt and bevel-butt symbols
with ISO 2553 system A arrow-side/other-side variants. Their reference lines grow
with field text. See `doc/SYMBOLS_USER_GUIDE.md` for insertion, bilateral leaders,
editing and the exact implemented scope.
