# ZIMA-CAD icons: base set

The first clean SVG icon set for ZIMA-CAD.

## Style

- Viewport: `0 0 24 24`
- Main artwork: `stroke="currentColor"`
- Stroke width: `1.75`
- Line caps and joins: `round`
- Transparent background
- Green accent: `#80AA1A`
- No gradients, shadows, or embossed effects

`currentColor` lets icons adapt automatically to light/dark application themes.

### Recommended colors

- Light theme: `#1E1E1E`
- Dark theme: `#F2F2F2`
- Active/highlighted: `#80AA1A`
- Disabled: `#909090`

## Contents

Reference geometry: `origin.svg`, `point.svg`, `axis.svg`, `plane.svg`.

Modeling: `sketch.svg`, `sketch-3d.svg`, `wedge.svg`,
`protrusion.svg`, `revolve.svg`, `sweep.svg`,
`fillet.svg`, `chamfer.svg`, `shell.svg`, `blend.svg`.

Documents: `part.svg`, `assembly.svg`, `drawing.svg`, `drawing-format.svg`, `title-block.svg`.

Document icons share one semantic set across tabs, trees, New Document, and Qt file dialogs:

- Part: a blue cube with three face shades and a contrasting outline.
- Assembly: all three cube faces filled with shades of yellow.
- Drawing: a sheet with views and a dimension line.
- Drawing Format: a sheet with an inner frame.
- Title Block: a tabular title block.

Basic GUI: `new.svg`, `open.svg`, `save.svg`, `undo.svg`, `redo.svg`,
`delete.svg`, `view-fit.svg`, `measure.svg`, `settings.svg`.

Sketcher uses `sketch-common-tangent.svg`: two curves, their common tangent
segment, and two green contact points. It represents creation of new parametric
geometry, not the standalone Tangent constraint.

## Qt

SVGs can be colored through styling or rendering. Some icons contain a semantic
green accent that must remain intact.

`terminal.svg` is reused from ZIMA-CAD-Parts (`gfx/navigation/terminal.svg`)
for the CAD console toggle immediately after Regenerate in the View toolbar.

## Menu action icons (2026-09-24)

Shared 24 px SVG assets: `close`, `rename`, `working-directory`, `properties`,
`edit`, `suppress`, `restore`, `hide` and `show`. They use the same 1.75 px rounded
`currentColor` strokes as the base set. Reuse `delete` for removal commands and
submenus. Property/inspection actions use the property-list icon; direct editing
uses the pencil. Suppression is distinct from visibility (pause versus eye).

Select Parent (`select-parent.svg`), Ground/Release (`ground.svg`, `release.svg`)
and Regenerate (`regenerate.svg`) use the same 24 px outline style. Ground and
Release depict fixing a component to ground and lifting it from ground.
