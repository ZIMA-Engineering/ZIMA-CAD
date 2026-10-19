# Shaped fasteners, 20 October 2026

The following requested families are authored and installed as current native
Parts, with one ordinary Body, editable history, current physical properties,
derived BOM mass, five translated names, thumbnails and native reference Drawings:

| Family | Factory offer | Repeated native edits | GUI dimension edits |
| --- | --- | --- | --- |
| DIN approximately 6923 flange nuts | Eight coarse sizes M5–M20; smooth seating face | 96 | 12 across M5/M20 |
| DIN 74361 A spherical collar nut | M20×1.5, SW27, R18 | 9 | 3 |
| DIN 74361 F conical seat nuts | M12×1.5, separate 60° and 90° included angles | 12 | 12 across both angles |
| DIN 74361 C spherical spring washer | M20 companion, bore 20.5, outside 34, transition 24, height 8, R18 | 15 | 3 |

## Sources and selected CAD details

Reviewed facts, source URLs and exact private-source SHA-256 values live in
`tools/data/flange-nut-reviewed-dimensions.json` and
`tools/data/wheel-fastener-reviewed-dimensions.json`. Copyrighted source scans
remain outside Git and the release. The wheel families use the original
DIN 74361 Part 2:1982 facsimile; they are not relabeled as another edition.

The flange family uses Aspen's nominal dimensions, cross-checked against
Fabory's dimensional catalog. Seating serrations are intentionally omitted as
requested. The source gives an upper root-radius limit; this representation
keeps a sharp hexagon/flange junction. The selected 40-degree flange roof,
lower round (dc-dw_min)/4, upper round c/5, zero bearing slope, 15-degree top
hexagon bevel and 90-degree thread mouths are identified in every language.
The generator checks minimum wrench height. Native checks independently verify
the exact annular bearing area and its minimum diameter.

Two-dimensional corner rounds belong to the flange meridian. An initial 3D
fillet candidate differed from the ideal analytic section by 0.000014 mm3 at
M6; its cached volume agreed with a separate adaptive BRep calculation. That
candidate was rejected. Authored circular Sketch rounds eliminate that
construction discrepancy and pass the original volume tolerance. No kernel
precision or acceptance tolerance was relaxed.

The spherical nut preserves the real lower clearance. Its native opening
references the actual clearance floor and its usable thread length follows
the current body and clearance dimensions. Upper bevel 30 degrees is selected
within the primary drawing's range. Type F uses the evidenced two seat angles;
the lower countersink ends at d4 and each execution has a unique native file.

The user explicitly confirmed the washer's compressed state. The M20 model
uses the tabulated spherical face and an inner flat. Its two exposed right-angle
corners use selected R0.1 rounds. The agreed parallel 45-degree, 1 mm axial gap
reuses the existing spring-washer cutter. That helper now optionally accepts
explicit real end faces for a revolved blank; existing extruded-washer calls
retain their original selection path. A normal native Axis feature belongs
to the same Body, with its length related to height. No substitute solid or
external geometry is used to display its axis.

Only the M20 washer is offered. The other four original source rows are retained
as dimensional facts. The initially assumed meridian is inconsistent for the
M12 nominal row, and the smaller profiles have not been accepted. They are not
silently extrapolated or installed. Free spring shape, preload, zinc coatings,
manufacturing tolerances and complete standards certification are outside these
nominal CAD models. Mass always follows calculated material volume and the
assigned 7,850 kg/m3 density, rather than indicative catalog mass.

## Verification and persistence

All baseline bodies and disposable edit copies pass independent native BRep
validity and single-solid checks, exact adaptive volume/cached volume/BOM mass
comparison, bore clearance, native thread extents, and applicable spherical or
conical equations. Every Sketch undergoes successive unlocked and locked point
drags with constraint residuals below 1e-8, stable point identities and read-only
external supports. The explicit edit matrix includes Undo/Redo, regeneration,
stable face references, save/reopen and unchanged source hashes after each edit.

GUI checks use the existing actual mouse dimension editor and shared Properties
confirmation. They cover both flange corner radii, the spherical radii, and
both conical radial/angle drivers. Their saved output Parts pass the independent
native checker again. Point-drag coverage for these exact assets is native;
the GUI receipts cover dimension editing, not an exhaustive asset-specific
mouse-drag matrix. The separately documented Sketcher regression matrix covers
the repaired common manipulation behavior.

Thirty neutral/localized Drawings have four real associative dimensions each,
native axes in both first-angle projected views and an axial section. The
spherical nut and washer additionally show their real model R18 dimension using
the established Show/Erase path. No invented projected circle or printed-only
replacement dimension is used. Regeneration and Save As/reopen retain the
model dimension owner/key and follow the exact cloned source Part identity.
All drawings were visually inspected in representative Czech output; all five
localized variants passed native regeneration and reference checks.

Evidence directories:

- `build/flange-nuts-v4`, `flange-nuts-edit-v1`, `flange-nuts-art-v1`,
  `flange-drawings-reopen-v1`, `flange-gui-independent-v1`.
- `build/wheel-spherical-nuts-v2`, `wheel-spherical-edit-v1`,
  `wheel-spherical-art-v3`, `wheel-spherical-drawings-reopen-v2`,
  `wheel-spherical-gui-independent-v1`.
- `build/wheel-conical-nuts-v1`, `wheel-conical-edit-v1`,
  `wheel-conical-art-v1`, `wheel-conical-drawings-reopen-v2`,
  `wheel-conical-gui-independent-v1`.
- `build/spherical-washer-m20-v5`, `wheel-washer-edit-v2`,
  `wheel-washer-art-v4`, `wheel-washer-drawings-reopen-v2`,
  `wheel-washer-gui-independent-v1`.
- `build/shaped-fastener-gui-v1` and final washer
  `build/shaped-fastener-gui-v2`.

The installer requires matching source/output hashes, every edit receipt,
representative GUI checks, every Drawing reopen receipt, unique native
identities and case-insensitive filenames, positive mass, five names and exact
thumbnail sizes before copying factory assets. Custom libraries remain intact.

This work changes no native schema, shared placement contract or modeling
algorithm. Qt 6.11.0 / OCCT 8.0.0 Windows checks passed. Linux execution and
rendering remain unverified on this Windows host.
