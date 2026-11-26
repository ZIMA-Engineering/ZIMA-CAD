# Sketch outline manipulation

## Scope and interaction

Native Segment and Circle outlines use the existing candidate picker. A Segment
translates as a whole when translation is free. Otherwise the existing point
solver projects its movement onto the permitted constraint path, retaining the
length during this gesture. An anchored interior C or midpoint contact acts as a lever pin:
the gesture also retains its arm lengths. Endpoint dragging remains the way to
change a Segment's length. A Circle outline changes its radius; its center stays
in place when permitted, or follows the existing tangent/contact equations.

Locked driving dimensions, reference identities, contact sides, fixed points,
read-only external sources and absolute limits remain authoritative. Unlocked
driving dimensions follow the existing manipulation contract: their values are
measured after the solve and their driving roles are restored. Reference
dimensions remain measurements. Temporary length/radius drivers exist only in
private solver copies; no new point, relation, identifier or native format is
persisted.

An outline click uses the ordinary selection path. Mouse movement must cross
the existing drag threshold before starting manipulation. Ctrl selection does
not arm a contour drag. Each preview starts from the original Sketch snapshot;
the original pick offset/fraction therefore remains stable across events.
Mouse release commits one revision, Escape discards the preview, and returning
to the original position/size creates no revision. The existing private Part,
Assembly and embedded-profile drafts own the transaction. Preview uses the
existing Sketch overlay and does not calculate an OCCT solid. Cancelling also
restores the original selectable scene after retiring the transient overlay.
Returning within 0.01 display pixel of the original position or radial distance
restores the exact original snapshot. This input threshold accommodates View
matrix unprojection; it does not round calculated geometry or relax solver tolerances.

## Verification matrix

The finite matrix is implemented in `cpp/tests/sketch_contour_drag_tests.cpp`
and `cpp/app/sketch_contour_ui_verification.inc`. It is not a claim about every
possible constraint graph.

| Area | Variants and independent checks |
| --- | --- |
| Segment geometry | Horizontal, vertical and oblique; both endpoint orders; grips at 10%, 50% and 90%; three successive outline movements; original length and translation equations. |
| Segment mobility | Free, either fixed end, anchored interior C or midpoint contact; native and external point anchors; fixed pin and lever-arm equations. |
| Dimensions and points | No size dimension, reference, unlocked driving and locked driving; repeated size edits and endpoint/center drags after contour drags; original identifiers and locked values. |
| Other line dimensions | X/Y length, aligned point distance, orientation measurement, axis coordinate and point-line distance; H/V constraints; symmetric point distances; line-pair, point-line, three-point, four-point and symmetric angles; only permitted movement is accepted. |
| Line relations | Parallel, perpendicular and equal native lengths, both orders; external direction supports normalized into the established reference slot. External equal-length relations are not an existing command capability. |
| Circle geometry | Radius and diameter, free/fixed center, both tangent sides and both selection orders; native axes and native/external lines; size, center and tangent equations. |
| Circular relations | Native/external circular tangency with internal/external contact, equal radii with free/locked source size, concentric centers; independent C/T equations and immutable sources. |
| Dependent geometry | Line/circle offsets on both sides; trimmed offsets following a moving native cutter, exact intersection branch and owner; symmetric partner following a tangent circle's radius; external outlines remain read-only. |
| Mechanisms | Slider-crank, four-bar, midpoint branch, Watt and Scott-Russell; native/external anchors, reversed order and rotated frame; three input poses checked against analytic mechanism equations. |
| Rejection | Fully fixed lines, angle-constrained rigid lines, locked circles and equal-radius propagation, circle absolute bounds, zero movement and nonfinite input; unchanged serialized document. |
| Persistence | Sketch round trips after actions; native Part save/reopen and session Undo/Redo. |
| Actual GUI | Part/Assembly Sketches in XY/XZ/YZ and embedded Sweep profiles; actual picking/press/move/release, click jitter, Ctrl selection, Escape, Undo/Redo, repeated inline dimension edits and point dragging, native save/reopen, zero solid calculations during interaction. |
| Localization | All new feedback in cs/en/de/fr/ru; source/catalog coverage, placeholders and active Qt translation checks. |

## Results

The Windows Release build uses MSVC, Qt 6.11 and OCCT 8.0.1. The source baseline
is `d756111467926f5acf11bd1188ec6f899864c953`. The shared native implementation
and GUI suites passed; executable hashes, environment switches, process timings
and summary output are recorded in [the machine-readable results](20261126-sketch-contour-drag.json).

| Suite | Result |
| --- | --- |
| New native contour matrix | 862 cases; 2,562 outline gestures, 2,016 point drags, 1,512 dimension edits and 103,723 checks. |
| New contour GUI matrix | 26 editor cases: 12 Part, 12 Assembly and two embedded Sweep profiles. 78 outline gestures (60 accepted, 18 locked-circle rejections), 78 inline dimension edits, 78 point drags and 6,827 checks. |
| Existing native mechanisms | 324 mechanism cases, 4,176 angle edits, 2,400 moving-point drags and 1,800 projected drags; an additional tangent-slider matrix has 432 cases, 864 edits and 864 drags. |
| Existing trim contacts | 32 cases, 1,920 dimension edits and 2,688 point drags. |
| Existing trim fragments | 96 circular, 144 parallel and 16 point H/V cases; 300 edits and 378 drags. |
| Existing native curve contacts | 108 cases, no failures. |
| Existing fillet GUI | 38 Part/Assembly cases, five languages, both selection orders, reference replacement/inspection, Cancel, repeated edits/drags, Undo/Redo and reopen. |
| Existing mechanism GUI | Five mechanisms, 30 mouse drags and 15 inline edits, Undo/Redo and native save/reopen. |
| Existing trim-contact GUI | Two contact cases, preview/Cancel, confirmation, repeated inline edits and point drags. |
| Localization | Source/catalog coverage, placeholder checks and active Qt translation checks passed for cs/en/de/fr/ru, including all four new feedback strings. |

The new native matrix took 1.86 seconds internally. Its synthetic translation
measurement, including the caller's private Sketch copy and result checks,
averaged 0.0053 ms per gesture for one curve and 0.323 ms for 200 curves over
100 gestures each. These are local synthetic measurements, not end-to-end GUI
latency or a guarantee for arbitrary constraint graphs. GUI checks independently
require zero body calculations throughout selection, manipulation, dimension
editing and cancellation. Finishing a changed Sweep feature may explicitly
calculate its body through the existing OK path.

The native checks compare independently expected coordinates, lengths, radii,
contacts and mechanism equations. A subsequent solve must preserve all resulting
point coordinates and circle radii within 2e-6; a successful solver status alone
is insufficient. Reference identities, contact sides, locked values and external
sources are checked separately. GUI checks use actual Qt mouse events and the
common picker. Native save/reopen reloads the saved Part, Assembly or embedded
profile and compares the resulting Sketch definition.

## Reproduction and remaining coverage

Build `zima_cpp_sketch_contour_drag_tests` and `zima-cad-cpp` using the normal
Windows Release recipe. Run the native test directly or through CTest. For GUI
verification, run `zima-cad-cpp --verify-startup --working-directory <isolated-dir>`
with `ZIMA_VERIFY_SKETCH_CONTOUR_ONLY=1`, `QT_QPA_PLATFORM=windows` and
`ZIMA_VERIFY_HIDDEN_WINDOWS=1`. The other suite switches are recorded in the JSON
results. Use separate isolated working directories so verification does not
change user projects or settings.

Linux has not been built or run on this Windows host. The GUI matrix uses
selected constraint states in three principal planes; the larger native matrix
does not imply that every constraint combination, imported model or arbitrarily
nested active Part occurrence has been exercised. Embedded Sweep GUI coverage is
XY only. The existing overlay/palette rendering is reused; this change introduces
no new graphics or theme colors. External equal-length relations remain outside
the existing command capability. Arc, spline and derived-offset outline dragging
are outside this requested Segment/Circle extension.

This change adds no native format, template or factory-library contract. No
Windows release archive is updated by this development build. The repository
`zima-cad.bat` continues to launch the current local C++ executable.
