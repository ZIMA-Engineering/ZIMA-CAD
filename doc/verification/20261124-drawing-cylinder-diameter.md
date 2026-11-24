# Cylinder silhouette diameter and linear dimensions

The Diameter command previously rejected a cylinder's side silhouette because
its measuring support was a line rather than a camera-normal circle. The user's
`ZE-CAD/50.drwz` also contains trimmed cylinder faces without complete circular
rims: their visible tessellation silhouettes had no measuring reference.

The shared C++ implementation now accepts a cylinder silhouette in Diameter
entry and presents the result with linear witnesses and the diameter symbol.
Linear and Chain entry also accepts the exact supporting line of a displayed
cylindrical silhouette. Two independently rounded end faces can define an overall
width; either face can be paired with a displayed model axis. Radius and ordinary
point entry retain their previous selection rules.

Complete analytic contours use the opposite generators of the same face and
occurrence. Trimmed cylinders supply exact diameter supports from their native
analytic surface. Only displayed, longitudinal silhouette fragments whose
adjacent triangles belong to that same face receive those references. Their
display coordinates, depth and visibility remain unchanged. These diameter-only
supports do not expose approximate trimming endpoints as ordinary point
references. Cones, arbitrary parallel lines and other occurrences cannot
substitute for a cylinder diameter. Independent lines intentionally remain valid
for linear distances. Source changes replace the view's measurement data
and invalidate the prepared picker.

No native schema or required external storage was added. Both supports use the
existing persisted measuring curves; the attachment retains one stable source
reference. Interactive opening rebuilds camera-derived supports from the already
embedded viewer packet. Precise views retain the measuring curves without that
packet. Lost support produces an unresolved dimension with its last presentation.
No modeling, Sketch solver or protected container-placement code changed.

The rounded-shaft regression also exposed a reopening defect: an inherited
cylinder support can retain its pre-Fillet axial range. For a complete rotational
band, its two actual circular trimming rims now determine finite contour ends.
Their centers, normals and radii must still satisfy the exact supporting surface.
This restores the same references after native reopening without changing the
packet schema or adding a compatibility path.

## Verification

Windows Release, Qt 6.11 / OCCT 8.0.1:

- Six targeted suites pass: Drawing dimension commands, measurement contracts,
  prepared picker equivalence, Section measurements, Thread drawings and
  localization coverage.
- The cylinder matrix covers 432 combinations of axis, camera tilt/facing,
  scale, occurrence, rim order and selected side. It includes successive radius
  changes, repeated label drags, exact diameter values, linear witnesses, native
  reopen, precise output and missing-support behavior. Existing 128 rotational
  radius cases and 24 native shaft Fillet checks also pass.
- Twelve additional native rounded-shaft cases verify that changing the end
  Fillet does not change the cylinder's 20 mm diameter or lose it on reopen.
  Twelve semicircle cases cover both orientations, Radius/Diameter and label drags.
- Independent semicylindrical faces with different owners and occurrences cover
  144 Linear/Chain, axis/contour, selection-order, automatic/explicit line-entry,
  orientation and successive-radius cases. Measurements use exact surfaces even
  when coarse display samples omit the extrema. Native interactive and precise
  reopen pass, as do missing-support and nonselectable synthetic-endpoint checks.
- Negative checks reject cone generators, arbitrary lines, different owners or
  occurrences, malformed opposite identities and stale camera projections.
- Actual GUI mouse entry on an in-memory copy of `50.drwz` creates the expected
  60 mm boss diameter from either side. Cancel leaves the revision unchanged;
  OK, repeated drags, Undo/Redo and native reopen preserve the attachment and
  value. Interaction performs no Body calculation and retains the input geometry.
- Twelve GUI cases select both independent end silhouettes or an axis and either
  silhouette, in both selection orders and orientations. The known values are
  58 mm overall and 23/35 mm from the axis. Cancel, OK, repeated label drags,
  Undo/Redo and native reopen pass without a Body calculation. Existing axis-cross
  and manual-dimension GUI verification also passes.
- Real-file picker checks compare ordered cached/uncached results and prove
  identical interactive stroke coordinates, visibility and vertex depths with
  diameter-reference generation enabled and disabled.
- No new localized prose was introduced. Existing Diameter terminology, error
  translations and dimension-symbol formatting are reused in all five languages.

Evidence is under `build/drawing-diameter-20261124/`: `tests.log`, `gui.log`,
`picker-50.log`, `diameter-0.png` and `diameter-1.png`. User files are read only;
all save/reopen checks use temporary copies. Picker timings in that log compare
the existing prepared picker with full traversal, not a before/after speedup
claim for this fix.

Linux execution is not verified here. This change does not add cylindrical
section-intersection references where the Section projection itself lacks them,
nor does it expose curved trimming endpoints for ordinary length dimensions.
Arbitrary freeform curved silhouettes are outside this verified scope.

## Local deployment

The main Windows application passed startup, factory-template creation, native
Box calculation/save and rendered-view verification. Its existing CAM setup
verification passed 153 checks with no CAD calculation during setup editing.
The verified executable was copied to the existing development runtime used by
`zima-cad.bat`; the launcher and build identity were not changed. Deployment
hashes and the recoverable previous executable are recorded in
`build/drawing-diameter-20261124/deployment.json`. Pending CAM library work was
preserved separately during this build and restored byte-for-byte afterward;
the unfinished library UI is not included in this executable.
