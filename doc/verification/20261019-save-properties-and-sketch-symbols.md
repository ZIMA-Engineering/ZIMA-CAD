# Save analysis and native Sketch symbols — 2026-10-19

## Scope and ownership

The user approved physical analysis on Save/Save As, a real solver insertion
point for Sketch symbols, the required native point-to-symbol link, affected
factory/start-template verification, and a context-menu Explode action. Custom
user libraries and already embedded user definitions are outside the factory
update. Windows and Linux consume the same C++ implementation and committed data.

Save prepares an immutable job. Its worker integrates existing native solids,
updates physical-dependent Relations and writes the file. It does not replay
history, assign driving dimensions, change appearance, solve mates or calculate
Assembly cuts. Publication requires matching revision, calculated generation,
runtime identity and current saved sources. Derived results preserve Undo/Redo
and current Body activation. Explicit Regenerate retains full Relation-driven
modeling.

Already evaluated Family packets receive the same analysis. Unevaluated rows
are not manufactured by Save. Open linked tabs receive the calculated result;
each Assembly row retains its own cuts and presence states. An uncut row uses
an uncut occurrence of the same current source, or resolves the current native
source when every generic occurrence is cut. Cut geometry is never substituted
for source geometry merely because the source document IDs match.

Sketch symbols now persist an ordinary construction point and its stable ID.
The shared solver updates the symbol's XY position. Deleting a symbol preserves
an independently fixed, dimensioned, constrained, shared or external point.
Explode copies editable native curves and text, preserving incidence and exact
rational poles/knots/weights. Offset/trim recipes are detached from their current
exact result. Source-definition constraints are not copied. Reflection preserves
the visible arc portion and readable text under the drawing right-anchor convention.

Text entry uses the shared active Sketch accessor, including Assembly and newly
owned draft profiles. Finishing Sketcher returns to the owning Properties window;
Cancel there discards the complete pending profile.

## Native and GUI matrices

`symbol_anchor_tests.cpp` exercises 348 counted cases, plus fixed-point,
fully constrained coordinate, external-point, merge/deletion and persistence
checks:

| Area | Matrix and independent checks |
| --- | --- |
| Point-pair dimensions | Plain point and symbol; XY/XZ/YZ; both sides and participant orders; X/Y/aligned distance; reference, unlocked driver and locked driver; three successive edits/drags; fixed support and geometric equations |
| Point-to-line | Native and read-only external supports; both sides; horizontal, 37-degree and vertical; all three dimension states; repeated edits/sliding and native reopen |
| Angular/symmetric | Plain point and symbol; three-point, point-line and four-point angles, symmetric coordinate; both orders/sides; all three states; repeated edits/drags; independent cosine/distance equations |
| Identity and rejection | Fixed and fully constrained drag rejection without mutation; signed coordinate values 15/0/-20; external point binding; both merge orders; connected-curve removal; symbol deletion with retained dimensions; Part/Assembly Undo/Redo and reopen |

The matrix exposed ordinary-point defects as well as symbol behavior: a locked
aligned distance rejected movement along its free circle, and three-/four-point
angle correction tried to drive the fixed ray when it was selected last. The
fixes retry existing equations or choose the free ray without rewriting stored
participants or side intent.

`symbol_explode_tests.cpp` passes 929 factory/synthetic combinations of variant,
rotation, scale and ordinary/template coordinates. Synthetic cases include
standalone points, lines, circles, arcs, ellipses, elliptical arcs, splines,
trimmed rational offsets, corner radii and text. Independent transformed native
coordinates, radii, arc intervals, ellipse handedness, poles/knots/weights,
sampled exact spline equations and text contour/rebuild orientation are checked.
Native reopen, one-step Undo/Redo and subsequent native editing also pass.

`sketch_symbol_controls_ui_verification.inc` passed actual Part and Assembly
mouse dragging, Tree decomposition with Undo/Redo, Text OK/Cancel, newly owned
profile Text and parent Cancel. Nine XY GUI dimension cases cover X/Y/aligned
distance with reference, unlocked and locked drivers, 27 successive mouse drags,
18 inline value edits, independent geometric equations, fixed supports,
stable symbol-point incidence, saved readback and drag Undo/Redo. A Sketch
dimension lock retains its driving value during dragging; explicit inline value
editing remains available under the established Sketcher contract.

The native angular/symmetric matrix is XY; the counted point-pair matrix covers
all three principal planes. GUI angle/symmetric/external combinations are not
claimed as an exhaustive new symbol-specific mouse matrix. Trimming applies
to decomposed curves, rather than the symbol's point itself; exact retained
trim/offset geometry is covered by the decomposition matrix.

Actual normal View selection and its right-click decomposition offer pass in
both Part and Assembly. The Tree decomposition label follows Czech, English,
German, French and Russian after changing application language. The shared
translation coverage/catalog contract also passes. Save Copy, derived-copy Save,
new-document options and Relation-template GUI contracts pass.

`save_properties_tests.cpp` passes a deferred 6000 mm³ native curved prism,
physical-only Relations, exact geometry/reference packets with analysis fields
excluded recursively, clean stale mass, material/unit changes, no-op Save,
Save As identity/source independence, concurrent edits, actual Body-focus changes,
Undo/Redo, nested mixed-material Assemblies, repeated/hidden/suppressed components,
unavailable density, unsaved current source geometry and explicit Regenerate.
Part and Assembly Family rows cover open/closed tabs, component presence, a cut
plus an uncut occurrence, and every generic occurrence cut with an uncut variant.

Observed Windows release-build timings on this machine: deferred Part Save
9.88 ms; 50 unchanged ordinary Save checks averaged 0.024 ms; the all-cut Family
case averaged 2.66 ms over 50 unchanged checks. These synthetic observations
are not hardware-independent speed guarantees.

## Factory data

The explicit native refresh calculated, saved and reopened the 15 insertion
points in `config/formats/ZE-TITLE-BLOCK.tblz`. Five independent language fixtures
under `tests/fixtures/drawing-library` each received three real points through
the native template writer and exact reopen comparison. Recursive inspection of
tracked native/library payloads found no other stale Sketch symbol instances.
This includes JSON payloads embedded as strings, not only top-level INI Sketches.

The current native start-template tool regenerated/reopened the six configured
mm/in Part, Skeleton and Assembly templates. GUI New Document options verified
creation from the templates, active first editable context and normal commands.
The native title-block Family verification passed all five variants, insertion,
frame changes, repeat regions/BOM positions, orientation and save/reopen.

## Additional Drawing contact regression

The unchanged 1904 source/build reproduces `Symbol split contact not offered`
in `symbol_drawing_tests.cpp`. Dimension picking deliberately offers geometric
characteristic points and lines, while a symbol needs a continuous edge contact.
An explicit transient request flag enables that contact for symbol entry, with
the default dimension policy retained. Prepared and direct pickers check several
contacts on a retained Boolean fragment against its original edge/occurrence
and original-support parameter. This changes no native document schema.
The Drawing symbol GUI also verifies an arbitrary 7.3 mm position on a 60 mm
straight edge, including Cancel/OK and downstream reference persistence. The
native symbol Drawing, prepared-picker matrix, dimension measurement, Drawing
contract and Drawing source suites pass with the explicit contact request.

## Platform boundary

All evidence above is Windows-native. Linux compilation, GUI execution and
packaged-runtime acceptance have not been performed on a Linux host. No separate
platform modeling algorithm or dependency-version change is introduced.
