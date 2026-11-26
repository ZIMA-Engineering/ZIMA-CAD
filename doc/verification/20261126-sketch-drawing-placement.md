# Sketch, Drawing and placement interaction verification

Source baseline: `ecf36d1588c166742d7e796028d059cd1e500d0a`.
Windows Release, MSVC, Qt 6.11.0 and OCCT 8.0.1. The implementation remains
shared C++; Linux execution has not been verified in this batch.

## Scope

- Cylindrical angular placement displays and accepts the angle from the chosen
  plane. Its first witness lies in that plane. The canonical persisted angular
  value, existing pose, side and complete turns are retained through the UI
  conversion. There is no native format change for these interaction updates.
- Wrapping shows the authored source Sketch alongside its result, including
  target and placement reference entry, using the existing lightweight preview.
- Three-point Arc takes start, end and through point. It creates an ordinary
  native circular arc and a native point with the existing C contact. The third
  point selects the finite arc, including major arcs. Collinear/coincident
  inputs are rejected transactionally. Subsequent edits use the ordinary arc
  solver: an unconstrained through point may slide on its arc, rather than
  redefine three unconstrained interpolation controls.
- Arc synchronization accounts for changed endpoint positions in the solver
  residual. A size/chord edit must verify contacts against the updated circle
  before accepting the solution.
- Drawing angular dimensions accept straight cylindrical silhouettes. Linear
  creation commits on placement click and clears references in the same dialog;
  each placement owns one Undo step. OK ends creation. Automatic mode infers
  linear/angular/radius/diameter, retaining explicit manual type selection.
  Chain remains an explicit mode. Context actions reuse existing icons.
- File Manager leaves surplus width to the thumbnail slider; compact New
  Folder, Copy, Move and Delete controls follow it in that order.
- Image insertion reuses the existing nine alignment choices and rotation.
  Its visible insertion point uses Sketch snapping and local coordinates.
  Draft insertion/deletion belongs to the active Sketch transaction. Image
  placement does not introduce a persistent solver contact to the snap source.
- Explicit whole-Origin/default-Origin entry presents XY, XZ, YZ. Complete
  references are reordered only after the existing assignment, preserving
  their orientation roles, side, offset and identity. Existing stored models
  are not geometrically reinterpreted or reordered merely by opening them.

## Finite regression matrix

| Area | Verification |
| --- | --- |
| Three-point Arc mathematics | 144 cases, 864 point drags, 108 dimension edits and 13,462 independent checks. Both contact sides, endpoint orders, rotated frames, small/large arcs, native and read-only external endpoint supports. |
| Arc dimension variants | Radius, diameter, point X/Y/distance, point-to-axis X/Y, three-point angle and chord length; driving/reference and locked/unlocked states; repeated value edits and center/start/end/through-point drags. Unsupported locked reference dimensions and impossible fully fixed movements are rejected unchanged. |
| Arc persistence/dependencies | Sketch serialization after actions, native Part save/reopen, session creation and radius-edit Undo/Redo, trimming/contact identity, collinear and nonfinite rejection. |
| Actual Arc GUI | Mouse creation on both sides, finite major-arc preview, collinear rejection, three successive through-point drags, radius creation and three inline numeric edits, Undo/Redo, Escape and native reopen. No solid calculations during authoring. |
| Existing solver consumers | Native arc/axis, trimmed rectangle/arc, Sketch keypoint contacts and the existing trim-contact matrix. |
| Placement angles | Native model, widget and command tests, plus GUI positive/negative editing, plane witness equations, regeneration, persistence and Undo/Redo. |
| Wrapping | Four output modes, source preview, target selection/clear/inspection, unchanged OK, Cancel, native persistence, embedded image and Undo/Redo; relief positive/negative depth and failed-calculation confirmation. |
| Drawing repeat/automatic GUI | Fifteen manual creations across five types and two views; five automatic classification cases, manual override, cylindrical angular support, selection reset, Cancel retaining completed dimensions, per-dimension Undo/Redo and native reopen. |
| Image GUI | Nine alignment choices at each of two rotations, mouse snapping to a native point, visible anchor, draft Undo/Redo, parent confirmation and embedded-image save/reopen without the input image file. |
| File Manager | Existing focused file controls/filter tests and compact row geometry at a 3,000-pixel window width. |
| Localization | New strings use shared catalogs in cs/en/de/fr/ru; catalog/source coverage and active Qt translation validation. Layout-only image and File Manager changes introduce no new strings. |
| Origin row ordering | Native/widget permutations preserve entire reference records, orientation equations, locks and serialized identity. Actual Part and Assembly default-Origin entry checks XY/XZ/YZ, parent ownership, unchanged frame and Cancel. Whole-Origin GUI also checks persistence and all five languages. |

Local logs are retained in `build/guide-example-20261126/`. The native Arc
matrix took 0.465 seconds in one local run; this is a synthetic test duration,
not an interactive latency guarantee. File Manager controls took 1.04 seconds,
the focused cylindrical model/widget suite 0.15 seconds, image GUI 15.51 seconds
and Arc GUI 6.46 seconds. Whole-suite durations include fixture setup and checks.

After relinking every affected native consumer with the changed solver, all nine
selected final suites passed: Arc, outline drag, native arc/axis, trimmed
rectangle/arc, trim contacts, keypoint contacts, placement commands, measurement
equations and the focused Part/Assembly Origin-row GUI. The latter took 7.40 s;
trim contacts took 78.32 s while another native test was initially running.
These are acceptance durations, not speed comparisons. Full Drawing measurement
GUI and parallel-direction GUI passed in 13.59 s and 2.20 s. Localization passed
in 17.57 s. Whole-Origin GUI passed in 13.55 s.

One synthetic new-Feature check measured 192.037 ms to open Properties and
96.4714 ms to assign Default Origin. Both published zero base scenes; Origin
assignment generated exactly one lightweight preview and resolved valid
XY/XZ/YZ references. This is one local end-to-end sample, not a speedup claim.

## Limits and unrelated test state

The full File Manager HTML-preview suite exceeded a 60-second diagnostic
timeout; the focused controls/filter/layout suite passed. The older general
placement-reference test expects replacement of a position-only reference to
silently add FRONT and expects exclusive orientation checkboxes. Current
baseline code retains the authored position-only choice and supports multiple
orientation equations. That broad test reports a contract mismatch; its old
assertions have not been weakened. The new angle and Origin-order checks run
independently of those assertions. No clean baseline executable comparison is
claimed for that mismatch.

The broad nested-Origin GUI subsequently fails at `Selected Point marker is not
azure`, after the relevant Part Origin checks. Running the unchanged released
2026112504 executable reproduces that exact marker failure. The focused
Origin-row mode retains the original broad test and separately executes both
Part and Assembly Origin assertions before that unrelated selection section.
The broader nested-selection suite is not reported as passing.

The finite Arc GUI matrix does not repeat every native external-support and
dimension combination. No exhaustive arbitrary constraint-graph or Linux GUI
coverage is claimed. Release acceptance and packaged-runtime results are
recorded separately after building the committed source.
