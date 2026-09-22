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
- `surface-texture/ZE-GENERAL-SURFACE-TEXTURE-ISO21920.symz`: a separate
  title-block/general indication, with the default requirement followed by a
  bare symbol in parentheses. Three manufacturing-process variants are offered.
- `general/ZE-GENERAL-EDGES-ISO13715.symz`: all, external, internal or combined
  external/internal edge requirements; each scope can have no exception, one
  specified exception or a bare symbol representing multiple exceptions.

All five factory title blocks embed independent copies of the general surface
texture and edge definitions. Texture strokes are yellow; local texture text
is green and general title-block texture text is white. All colours are stored
in the embedded definition and respected by Sketcher and Drawing rendering.
Ra 3.2 and the signed edge values are editable
factory presets, not requirements prescribed by the standards. Existing Drawing
copies are not automatically replaced when a library or title block changes.

These examples can be inserted into a Sketch or title block. The general texture
composition is intended for the title block; the local texture asset describes
individual surfaces. They do not yet
provide model-surface attachment or an exhaustive standard-symbol catalog.
