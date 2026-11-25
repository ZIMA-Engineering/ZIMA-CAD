# Filled Sketch relief and cylindrical junctions

## Scope and cause

The reported `50.prtz` was inspected and calculated from an isolated copy.
Its SHA-256 is
`b9a9df53fbddf5422a4a270598230c89eeff0e28a4da7c0e3d77a1fb2ad82969`.
The original user document was not modified.

Projection Relief incorrectly required the Surface command's retained-region
selection. It now derives the complete material area from the ordinary filled
Sketch, including inner contours and disconnected islands. Surface retains its
explicit region selection. Projection Relief supports persisted parallel
projection depth and normal-to-surface depth. New GUI definitions default to
parallel depth; an existing definition without the optional direction field
retains its authored normal-depth interpretation.

Touching relief tools previously reached the Body Boolean as a compound. That
could leave an internal wall at the junction of two cylindrical faces despite
a valid BRep and correct total volume. The tools are now joined first. Stable
identities for newly split junction arcs also include their authored source
curve ancestry. Material classification at the junction, rather than volume
alone, verifies removal of the wall.

All Wrap outputs now follow connected current Body faces on the same supporting
surface and material side. A physical gap or a different radius is not an
automatic continuation. Historical untrimmed supports cannot extend coverage.
Different cylindrical and planar supports can be selected manually in order;
cone and sphere wrapping retain their existing single-support mappings.

The first failed OK leaves the draft open. The second unchanged OK uses the
shared cached confirmation result, closes Properties and retains a red failed
feature. There is no second geometry calculation. Cancel, native persistence
and Undo/Redo retain their established meanings.

## Verification environment

Windows, MSVC Release, Qt 6.11.0 and OCCT 8.0.1; changes based on commit
`29cfd92b6b6e0671c60dba7e39c8faf2aa4d85e4`.
The implementation is shared with Linux. Linux compilation and execution were
not performed on this Windows host.

The junction matrix covers 24 combinations: two-sided versus successive
Extrusions, both Projection depth modes versus Wrap, outer versus bore material
sides, and Add versus Subtract. Checks include independent volume equations,
material probes on both sides of and directly on the junction, unchanged
surrounding material, native validity, preview direction and colors, no-op
calculation counts, mode edits, Undo/Redo, cached reopen and cold regeneration.
Both physical-gap and different-radius rejection cases pass without changing
the document. Both commands retain actual geometry failures after second OK,
including save/reopen and Undo/Redo.

The full Projection document contract passes 48 filled-profile combinations:
plane, cylinder exterior and bore, both depth modes, Add/Subtract, with/without
an inner contour and with/without a second island. Each exercises repeated
height edits, no-op, Undo/Redo, references and cold native reopening. Twelve
analytic cone and equatorial/polar sphere cases pass independent volume checks.
Projection history, Wrap document/history, directional projection and feature
parameter tests also pass. Existing source-Sketch dimension/drag and external
read-only reference cases remain in the full Projection document test.

The independent curved-profile volume oracle uses span-aware Gauss-Kronrod
integration. Ordinary adaptive Gauss integration falsely converged on one
spline-bounded cylinder patch (35.7056 rather than the analytic 35.3429 mm3).
Gauss-Kronrod agrees with the analytic equation; the geometry was not changed
to satisfy the oracle. The filled-profile matrix uses the document's default
0.001 mm tolerance. An exploratory 0.000001 mm version rejected a circular
projection during parameter-curve certification; it is not claimed as covered
by this matrix. The straight-sided junction fixtures do pass at 0.000001 mm.

GUI checks pass for all five Projection outputs and all four Wrap outputs:
reference entry, replacement and inspection, whole-Origin placement, white
source Sketch and azure relief tool, both depth choices, no-op, Cancel,
Undo/Redo, native reopen, and red failed-feature confirmation. All five UI
languages pass the affected control checks and the translation catalog contract.
The Wrap GUI fixture uses a two-sided cylinder and retains both authored target
rows. It checks explicit ordering selection, Up/Down limits, independent
inspection, replacement, removal/reinsertion and stable rows during resizing.

Compact native display tags share analytic descriptors with the reference
packet. Preview resolves that metadata by exact face identity while retaining
current triangles and adjacency. The compact-packet regression compares wire
points and clipped image vertices with the full packet. Reopen checks use the
ordinary session rollback API, which assembles shared reference data, and
verify that both Projection and Wrap preview span both sides of the junction
without a kernel calculation.

## Ordered arc–arc–line wrapping

The native fixture has a radius-10 quarter-cylinder, a tangent radius-5
quarter-cylinder and a following plane. All eight combinations of Curves,
Surface, Relief and Split with both depth signs pass. Independent checks cover
developed lengths, exact native perimeter, volume integrals, unchanged material
for non-material outputs, embedded image coordinates and material probes at
both support junctions. Repeated targets retain the same result. An omitted or
out-of-order support is rejected. Two further relief cases preserve a hole
crossing the radius change and a disconnected island on the flat.

The same outline authored as a native Sketch and Extrusion passes ordered-target
persistence, duplicate-row retention, calculation-free wire/image preview,
Undo/Redo, cold regeneration and stable curve identities. Source coordinates
partition the filled Sketch at each support transition. Shared native mask
vertices preserve the cutting edge's ancestry during wire assembly. Split tools
retain their own chart's target ancestry; a cylindrical curve is not assigned
to an unrelated planar support.

Display chord lengths are not an exact curve-length oracle. The perimeter check
uses native measured lengths; image checks allow the current display mesh's
deflection plus texture interpolation error. Neither changes model tolerance.

## Measured user-model operations

These are observed Release-build times on the saved user model, not promises
for other models or machines. Setup, file loading and test assertions are
excluded from the individual changed-OK measurements.

| Operation | Native changed OK | Signed volume change |
| --- | ---: | ---: |
| Normal depth, subtract 3 mm | 5,708 ms | -2,781.08 mm3 |
| Normal depth, add 3 mm | 5,923 ms | +3,073.83 mm3 |
| Projection depth, subtract 3 mm | 5,501 ms | -2,712.86 mm3 |
| Projection depth, add 3 mm | 5,356 ms | +2,712.86 mm3 |

All four match independent whole-region volume equations at the operation's
actual output boundary. The final Body also contains a later Wrap feature and
must not be mistaken for the Projection output when comparing these volumes.

The final GUI run measured 578 ms to open Properties, 7,098 ms for changed
parallel-depth OK, and 315 ms for unchanged OK after that edit, including
display restoration.
Unchanged OK performs zero kernel calculations and creates no Undo transaction.
The complete changed operation still includes downstream Wrap, derived Body and
Family work; these results do not claim that the entire operation is subsecond.

## Native data and templates

The optional relief-direction field stays within the current native format.
An inventory of 1,212 tracked native files found no affected factory Projection
Relief definitions. The ten native documents in the protected Toyota project
also contained no Projection Relief definitions. No user originals were
converted or overwritten.

All six Part/Skeleton/Assembly start templates were rewritten and reopened with
the changed serializer and remained byte-identical. Actual GUI creation passes
for millimetre and inch templates in all five languages, including active
editing contexts and normal command availability. Release factory-reader and
packaged-runtime gates remain part of publication verification.
