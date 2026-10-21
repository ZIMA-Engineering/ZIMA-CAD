# Sketch axis visibility and endpoint color — 2026-10-21

The reported Revolution Sketch axis followed the Sketches switch instead of
Axes, while its native endpoint markers remained visible and used the ordinary
Sketch foreground outside editing. The model-view renderer now classifies
passive Sketch centerlines and their private construction points as axes.
Active Sketch geometry retains its existing editing presentation. Native point
incidence already distinguishes a private construction point from a point
shared with an ordinary profile; no coordinate matching or new native field is
required.

Picking applies the same axis visibility before constructing edge-derived
Container and occurrence offers. Filtering only exact edge candidates afterward
would leave the parent selectable from invisible geometry. The optional picking
predicate leaves all other callers unchanged. Explicit reference entry,
inspection and command preview retain their established exceptions. The
confirmed-point overlay also respects its marker's existing visibility rule.

Standalone Sketch component publication retains the visible centerline points
already admitted by the shared hidden-construction filter. Previously it deleted
every construction point after that filtering. Ordinary hidden auxiliary
geometry remains filtered through the existing Sketch path.

## Verification scope

`zima_cpp_sketch_axis_visibility_contract` exercises 192 finite combinations:
light/dark palette, native Sketch round-trip, active/passive Sketch, all three
display modes, Sketches on/off, Axes on/off and Points on/off. It verifies actual
framebuffer line/point colors, an ordinary profile, repeated axis visibility
changes, hidden exact-edge and parent picking, and unchanged native Sketch data.
Additional assertions cover a previously confirmed hidden endpoint, shared
profile/axis incidence, standalone component endpoint retention and native
Part/Assembly source hydration with two repeated occurrences. Curved centerline
checks include Circle, Arc, Ellipse, Elliptical Arc and BSpline point visibility.
These checks do not claim a new solver matrix or
complete Assembly workspace interaction coverage; the solver and Sketch native
data are unchanged.

The initial component round-trip test incorrectly treated the solid-vertex
`BodyResult` packet as complete Sketch presentation. That packet intentionally
omits Sketch point roles. Production native Assembly loading hydrates its source
Part and rebuilds Sketch display from persisted native incidence. The corrected
test exercises that real `.prtz`/`.asmz` save/reopen path; the serializer was not
changed to satisfy the wrong diagnostic assumption.

Seven final contracts passed in 30.38 seconds on Windows: axis visibility,
existing Feature-axis highlights, active Sketch line styles, all-language
translation validation, native tangent contacts, native arc/axis dimension and
drag cases, and appearance/component publication. Evidence is in
`build/sketch-axis-visibility-final-tests.log`. These existing native solver
regressions supplement the display matrix; they are not newly implemented
solving paths.

The ordinary `zima-cad-cpp` target was rebuilt in
`build/cpp-windows-release`, the unchanged root `zima-cad.bat` destination.
Subsequent test-only rebuilds are in
`build/sketch-axis-visibility-final-build.log`. No new portable release archive
has been published for this isolated fix.

Framebuffer evidence is written under the CTest working directory's `build/`:
`build/cpp-windows-release/build/sketch-axis-{shown,hidden}-{light,dark}.png`.
Shown and hidden dark images were visually inspected after the initial passing
matrix. They preserve ordinary profile points while the axis line and its own
endpoints disappear together.

There are no new or changed user-visible strings. The existing translations of
Axes, Sketches and Points remain shared in Czech, English, German, French and
Russian. No native format, solver, general container-placement contract,
factory library or template conversion is involved. This is shared C++ code;
Linux execution still requires verification on the Linux host.
