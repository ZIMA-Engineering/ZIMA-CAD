# Noncircular contacts, Render interaction and derived Body follow-up

Local Windows verification uses MSVC, Qt 6.11.0 and OCCT 8.0.1. This report
describes the follow-up to release 2026112301; it is not publication evidence.
The shared C++ implementation has not yet been executed on Linux for this batch.
CAM and rectangular image cropping remain separate pending work.

## Noncircular Offset tangency

Circle/Arc tangency to an Offset uses a real contact point and the current
support geometry. Existing incidence is reused; removing T retains the point
and its independent C relations. The existing constraint fields store the
contact and radial center identities, without a native format extension.

The native regression independently checks incidence, radius and perpendicular
radius/tangent equations after successive edits and drags, with length residuals
below 1e-7. It compares the optimized span projector against the existing exact
projector, including corners and self-crossings where no unique tangent exists.
The bounded solve-local cache compares complete current source geometry,
distance, side and tolerance. It never substitutes stale display geometry.

| Native coverage | Exercised actions |
| --- | --- |
| Line, ellipse and spline Offsets | Both selection orders and Offset sides; three R/diameter changes, Offset distances and center drags |
| Circle and Arc contacts | New and existing C; both radial sides; contact identity and finite Arc domain |
| Dimensions | Driving/reference size, locked/unlocked states, X/Y coordinates, pair distances, point-to-line/symmetric distances and Arc angle |
| Editable source | Ellipse major/minor radii, rotation, equal semiaxes, spline pole, line Offset chain and repeated side changes |
| Constraint states | Underconstrained movement, fully constrained drag rejection, impossible/redundant edit atomicity |
| Dependencies | Target/support trimming, native and read-only external supports, serialization, Undo/Redo and reopening |

The full new native suite and existing circular Offset suite passed in 15.90 s
and 1.45 s respectively. These are finite test timings, not a general speedup
claim. Existing derived-copy and translation suites also passed.
The broader Sketcher suite and both Offset suites plus Offset geometry were
repeated together successfully (four suites, 114.88 s, during compilation).

The GUI matrix contains 32 combinations: Circle/Arc, ellipse/spline Offset,
native/external source, new/existing C and both selection orders. Every case
performs mouse selection, three inline R/diameter edits, actual center drags,
Arc endpoint drags where applicable, equation checks, Undo/Redo and save/reopen.
The fixtures distribute all five UI languages. All 32 integrated GUI cases
passed in 153.09 s. Mouse targets are selected through the ordinary ordered
picker on clear parts of the curve or dimension, avoiding point/witness crossings;
the tests do not invoke a constraint or dimension editor through a shortcut.

This is not exhaustive coverage of arbitrary spline degree, cusps, all elliptical
arc parameterizations or every nested Offset. Nonlinear Offset-of-Offset inputs
that fail the existing precision gate remain rejected atomically. The tested
line Offset chain does not imply unrestricted nested nonlinear support.

## Straighten and failed operations

Dependent features retain their authored references. Coherent attachments to a
single transformed end cap carry their frame through Straighten; multi-face
attachments continue through the ordinary complete reference solver. Subtractive
Extrusions also carry their Up-to limit geometry into the replay frame. Shared
container placement is unchanged.

The native face-transfer suite passed across the existing sweep/helical/3D
fixtures. Twisted-source subtractive cuts passed in both directions. A read-only
copy of the supplied Part with a twisted source, additive Extrusion and Up-to
cut straightened and restored successfully: the straight volume was 274395 mm3,
and restored volume, area, centroid and reference points agreed with the source.
The original user document was not overwritten.

Failed preparation now uses the existing failed-definition lifecycle. The GUI
fixture verifies first OK reports failure, Cancel preserves history, and an
unchanged second OK via middle-button double-click retains the unresolved
definition without repeating calculation. Undo/Redo and native reopening retain
that state. Independent later features remain editable; repairing the input
clears the failure. Unsupported geometry is not reported as a successful result.

## Body axis endpoint presentation

Scale, Mirror and Pattern unwrap endpoint ancestry consistently, including
nested combinations. Hidden helper endpoints remain available to point picking
and references; ordinary display does not reveal permanent black dots. Scaling
also scales the display length and retains the transformed display owner.

Native Body Scale tests cover ten endpoint keys and six operation compositions,
with actual dependent references and native reopening. The viewer matrix covers
five endpoint families and eight nested key compositions through Light/Dark/Light,
ordinary hidden markers, proximity picking and confirmed highlighting. Actual
transformed geometry is additionally covered by the appearance contract.

## Render and FEM presentation

Render camera/light objects are visible normally, hoverable and selectable.
View through camera is available in Properties and context actions. Camera
projection, field of view and runtime viewport anchoring are retained separately
for each document tab; this does not change persisted named-view data.

Image actions occupy the right command column. Wheel zoom anchors at the pointer;
middle/right navigation follows the Drawing interaction. Rename/Delete belong
to the image Tree context menu. Export belongs to the image tab. Save to Part
explicitly retains a result in the native document. Background rendering still
uses an isolated scene snapshot while modeling continues.

FEM marks and Render camera/light strokes and adjacent labels use dimension
yellow. Owned planar/projected FEM Sketch region boundaries use the same color
in ordinary display and pending Properties, retaining the owning condition for
selection. Projected display reuses the existing viewer-packet projection;
native region geometry and authored Sketch editing colors are unchanged.
Projection point grips remain present. Light/Dark/Light framebuffer and picking
checks cover Force/Constraint and Curve/Surface regions in both Sketch modes.
These marker/region checks passed, as did all three native FEM document,
operation and condition suites (1.60 s). The complete FEM GUI workflow also
passed: body/planar/projected regions, static and thermal/combined loads,
measurement, placement/inspection, Cancel/unchanged OK, Undo/Redo, reopening,
five languages and two themes.

Render GUI passed at normal and compact sizes (15.35 s and 14.71 s), including
camera rays, object selection, background modeling, gallery persistence and
JPEG/PNG export. Its synthetic concurrent Extrusion Properties/OK edit took
148 ms and 162 ms respectively; this does not establish a bound for larger
models. Both projections pass two successive tab round trips, including camera
rays and viewport anchoring. Ordinary camera-state updates retain the current
field of view instead of resetting an authored camera's projection.

## Localization and remaining gates

New or changed visible strings are present in cs/en/de/fr/ru. The annotation
color and region changes add no text. Catalog validation and the affected local
GUI checks have passed. Committed Windows packaging gates remain required.
Private models, user configuration and temporary diagnostics are excluded from
the change and release archives.
