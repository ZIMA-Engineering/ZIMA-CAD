# Fastener artwork, finite naming and smooth outlines

## Scope

The user approved the reference artwork after moving the radius leader above
the neighbouring extension line, then right to make it more inclined, and placing
the historical 15-degree arrows outside the sector. The two real native views
come from the saved `Projects/HHS-ISO4017-M3X20.drwz` pattern. The filename is not
used to infer geometry. Factory representatives have their own Parts and sibling
relative Drawing sources. Original Projects and the independent user prototypes
are outside factory promotion.

The combined A/B chapters now contain one length per diameter/pitch, rather than
extra long B copies. The finite library has 200 products. Leading zeros affect
filenames only; stock, standards and thread parameters retain their actual values.
General file sorting is unchanged. Product grade remains a geometry authoring
input but is removed from native parameters and overview columns. C retains its
agreed bearing-face geometry. All thumbnails are horizontal on white.

## Native and asset verification

- All 200 metadata-only candidates retain byte-identical calculated caches and
  document UUIDs. Save/reopen retains the reduced parameter list. Promotion
  checks every retired factory file against Git before any replacement.
- The independent native verifier passes all 200 installed single solids,
  dimensions/mass, bearing choices, underhead radii, locking holes, constrained
  Sketches and thread limits.
- Five independent representative copies (small full thread, partial thread,
  grade C, fine thread and locking hole) pass 15 successive length edits with
  independent geometry checks, Undo/Redo, stock/mass and save/reopen. Earlier
  full-library length evidence remains applicable to the identical cached
  geometry; it is not presented as a new 600-edit run.
- Ten illustrations retain two native views and their applicable resolved
  dimensions, including b and d1 where required. Numerical checks cover l, k,
  d, s, e, underhead radius, chamfer, end depth, and applicable c/dw/b/d1.
  Text and dimension layouts survive native save/reopen.
- Fifty localized PNGs cover cs/en/de/fr/ru. Neutral images use English; chapter
  `01.html` and localized `01_cs.html` variants coexist. Long Russian titles
  are fitted and every image retains at least eight white pixels at its border.
- Fifteen fastener/naming/localized-artwork tests and five whole-library logical
  reference tests pass. The native File Manager suite verifies index priority,
  localized 01 pages, neutral fallback, independent navigation and existing
  five-language Light/Dark/Light behavior. The native translation contract passes.
- Clean-browser checks cover all 35 affected chapter/language pages with no
  missing images, layout or contrast failures. Preview regeneration uses the
  same pages; retained source schematics are not shown as duplicate new figures.

## Fillet and Sweep outline verification

The former viewer chooses complete internal triangulation edges where adjacent
face normals change facing. On irregular curved triangulations that can expose
short transverse strokes. Native-face outlines now follow the zero of the same
interpolated corner-normal field used for shading, inside each triangle. Faceted
unreferenced geometry retains its established path. This changes presentation,
not OCCT solids, reference identities, persistence, picking or side choices.

The native viewer contract checks multiple radii, both sides, exact vertex/edge
contacts, tangent planar patches, invalid-buffer alignment and faceted fallback.
Existing picking verification compares all 144 candidate lists and confirms
changed-source invalidation.

The final real OpenGL matrix covers 12 scenes and 432 frames: eight synthetic
sphere/fillet scenes at four radii, the user's screw, grade C, and two calculated
Sweep fixtures; six directions, two projections and three display modes.
Actual wheel/pan events do not rebuild unchanged silhouettes. Changed source
vertices invalidate the contour. Native source hashes remain unchanged. A
separate 36-frame close-up comparison covers the user's native fillet. These
are finite representative views, not every camera position or arbitrary Sweep.

Paired earlier 288-frame captures took 1,675.79 ms before and 1,723.51 ms after
for framebuffer capture/readback in total. This is not isolated contour timing
and establishes no speedup. No modeling precision is reduced. Normal-field
inputs and view direction must match exactly before cached contours are reused.

Windows execution is verified locally. Linux execution requires its own host;
the shared C++ implementation, OCCT 8.0.0, Qt 6.11.0, native formats and protected
container-placement implementation are unchanged. Package acceptance is recorded
separately in the release note.
