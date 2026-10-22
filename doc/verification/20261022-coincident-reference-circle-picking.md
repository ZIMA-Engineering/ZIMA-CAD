# Coincident reference circle picking — 2026-10-22

Status: Windows native, mouse/GUI, five-language and two-theme checks passed.

## Report and cause

The user reported many selectable points on circles drawn onto external
references in `Projects/0101.prtz`. Read-only inspection of the reported file
found six native circles in its source Sketch and three native circles plus
three external references in the destination Sketch, with no native B-splines.
The references use exact rational quadratics; their control data is not native
editable spline geometry.

The defect is in `Sketch::viewer_mesh`: the external-reference crossing path
uses each display chord as a line. Comparing those chords with a native circle
on the same supporting circle publishes artificial discrete intersections.
Different sampling densities produce different false candidate counts.

## Change and limits

Inference and Trim share `Sketch::coincident_circular_support`. It compares the
actual center/radius and requires certification of an available exact rational
curve. Circular-looking display samples cannot certify a noncircular exact
curve. The tolerance is the existing Trim circular-coincidence tolerance;
source accuracy, topology, dimensions and circle/arc types are unchanged.

Scene preparation records coincident pairs once. Crossing loops perform borrowed
identity lookups without copying strings or running the solid kernel. They skip
these continuous coincident supports instead of publishing their display-chord
endpoints. Existing genuine crossings, native points, centers and characteristic
points retain their existing paths. This correction does not redesign general
noncoincident curve intersection accuracy.

There is no change to native persistence, placement, coordinate conventions,
geometry-side choices or solver equations. No factory asset conversion or start
template update is required. No new user-visible text was introduced; localization
review still includes the existing five-language catalog contract.

## Verification matrix

`zima_cpp_coincident_reference_circle_tests` covers 8/17/57/128 reference sampling
intervals, both source directions, full-circle and arc sources, radius and
diameter driving dimensions, locked/unlocked dimensions and free/fixed centers.
Each case performs successive value edits, repeated center drags, exact
center/radius checks, unchanged source checks, read-only packet preparation,
reference-dimension measurement, all three standard Sketch planes and native Sketch serialization/reopen.
Additional cases cover two coincident external rims with different sampling,
noncircular exact data with circular display samples, broken supports, separated
concentric circles, associated Reference-to-contour circles, real line cutting
and native arc retention, document revision stability and Undo/Redo.

The existing external-curve-center GUI matrix is extended with assertions over
both actual hover and the complete candidate-cycling list after creating a circle
on an external circular/arc reference or matching its radius in both selection
orders. Existing mouse creation, constraints, dimensions, dragging, Undo/Redo,
save/reopen and Trim suites remain relevant regression gates.

The native matrix passes **392 cases** (0.94 s in CTest). Direct read-only
verification of the isolated reported file also passes: all three coincident
reference/native-circle pairs have zero artificial crossing candidates. Its
source SHA-256 remains
`39a0461d5107651e42cb6ad9b0217b7508c78c00f2499364990496e054944359`.

The existing Reference command matrix, Sketch dimension command matrix, five-language
translation/catalog contract, circular Reference-to-contour GUI, straight/circular
external-cutter Trim GUI, 12-scenario external conic-center GUI and dimension-entry
GUI pass. The conic-center matrix checks actual hover and every offered candidate,
not only the first highlighted item. It also verifies source-bound center/radius
identity, both C/equality orders, successive numeric edits, rejected bound-center
drags, Undo/Redo and native save/reopen.

The first Reference command run was blocked by the Windows filesystem sandbox at
its atomic Save As hardlink publication. Repeating that same focused executable
outside the sandbox passes; the product's atomic file-publication code was not
changed or bypassed. Broader GUI regression separately exposed the preselected
native-circle C picker gap recorded in the linked companion check, now passing its focused rerun.

The Windows build is the verification platform. Linux, arbitrary nested occurrence transforms and the full
unrelated Sketch dimension matrix are not established by this focused test.
The separately planned Face × Sketch-plane editor commands are still pending.

The separate [preselected native-circle C correction](20261022-preselected-circle-coincidence.md)
was found by the broader regression run and verified independently.

The automatic source-center/rim circle workflow additionally passes all **10**
combinations of cs/en/de/fr/ru and light/dark themes (55.35 s total). Each uses an
isolated working directory and checks native circle ownership, C/equality source
links, actual hover, complete candidate cycling, Undo/Redo and native save/reopen.
The light and dark captures were retained with the local verification evidence.
No test rewrites the reported user file.
