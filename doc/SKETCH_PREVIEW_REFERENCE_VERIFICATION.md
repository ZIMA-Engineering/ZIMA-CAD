# Sketch preview and reference verification

## Scope and causes

- A newly placed empty Feature published its live Origin in a transient overlay.
  Confirming a face in the View also refreshed the base scene, which copied that
  same pending Origin into the base packet. Later numeric edits replaced only
  the overlay. The repair assigns the pending frame and its placement dimensions
  to one presentation channel. The previous stored-Origin removal filter was
  removed. Reference confirmation, picking and Tree initialization still run.
- A terminal reference-only history row copied an intermediate final-first
  packet whose display mesh was intentionally deferred. Its B-Rep could remain
  valid while the final displayed Body was empty. Requested output boundaries
  now publish the live unchanged solid. This is independent of Sketch validity.
- Exact history output was collected from the retained solid's topology alone.
  An authored cutting-tool axis endpoint can lie outside that solid, so the
  requested endpoint disappeared from the narrow input packet while remaining
  present in the final authored-reference database. Exact publication now also
  carries explicitly requested native axis endpoints from the current prefix.
  It does not substitute untrimmed operand faces for current body fragments.
- Drag preparation followed anchored coordinates through C/H/V and locked axial
  dimensions, but did not follow base-axis symmetry. A source center could slide
  while its reflected center rejected the same slightly off-axis cursor. The
  shared coordinate walk now transfers the exact signed reflection relation.
  Freely movable segment axes are not treated as immutable projection axes.

## Native presentation and reference data

The user approved both additions during the implementation session:

- Part and Assembly documents store `sketch_views`, keyed by stable Sketch ID.
  Each state has `back` and `sector` (0–3 for 0/90/180/270 degrees). This is the
  resulting camera state, not a click counter. It does not modify the Sketch
  frame, topology, side choice, geometric fingerprint or modeling Undo.
  Presentation changes participate in dirty/save detection. Modeling Undo/Redo
  preserves the current camera metadata. Draft views are attached to accepted
  profiles when saving; cancelled new profiles are not native dependencies.
- A finite straight projected edge additionally creates an independently
  removable `edge_midpoint` external point. Its source identity is the same
  original edge/occurrence used by the two existing endpoint records. C binds
  a native point to this read-only source point. The midpoint is recomputed
  from current source geometry and is not a chord substituted for a curve.
  Deleting that point follows endpoint deletion semantics; deleting the parent
  reference removes its derived keypoints. Existing authored references are
  not silently repopulated on load, since an absent keypoint may be intentional.

Midpoint presentation uses the ordinary picking radius and exact point identity.
A pending profile or an endpoint hover must not expose a distant midpoint.
Explicit selection/inspection retains the ordinary reference feedback contract.
No new localized UI strings are introduced; the existing C marker and shared
external-reference cross are reused.

## Verification matrix

The table distinguishes completed evidence from checks still being run. It is
not an assertion of exhaustive coverage of every Sketcher operation.

| Area | Matrix | Evidence/status |
| --- | --- | --- |
| Pending Origin | Actual View face confirmation, analytic cone, three successive numeric corrections, single frame and current placement dimension, OK/Cancel | GUI regression failed before the presentation repair and passed afterwards |
| Final Body display | Terminal Plane and suppressed terminal datum, three radius edits, independent cylinder volume, current Body context, serialized reopen | Native model-calculation suite passed after the kernel repair |
| Symmetric center drag | X/Y base axes, both participants, reversed symmetry order, radius/diameter, driving/reference and locked/unlocked size, successive edits/drags, constrained center rejection, serialization | 48 native cases and both centers in the supplied 02.prtz copy passed |
| Sketch camera | Part/Assembly, two Sketches, both sides and all four sectors, fresh-window native reopen, unchanged geometry and modeling revision | All 16 GUI cases passed |
| Camera session | Dirty/save state, copy isolation, unchanged update, two Sketches, modeling Undo/Redo retained, invalid metadata rejected | Part/Assembly checks and complete named-view command suite passed |
| Actual 02.prtz | Real center picking and six off-axis mouse drags, Undo/Redo, hole dimension creation, three inline size edits, owning Feature OK, displayed final Body, save/reopen; actual placement face click, repeated editable numeric corrections, one Origin and Cancel | Complete hidden-window GUI workflow passed after the exact-boundary repair on the first-Sketch-edited copy, and again on the current five-row R150/third-circle/Tažení 04 copy. The symmetric hole pair is identified by persisted center relations rather than by the total number of circles |
| First Sketch in 02.prtz | Source point X changed to 106, 104 and its original value, explicit regeneration and native save after every edit; final export | All three edits passed with no failed container; the pre-repair case failed at Tažení 03. Final mesh has 6,396 triangles and 3,192 distinct float32 vertices; bidirectional nearest-vertex difference from the baseline export is 0.000015259 mm, with changed triangle ordering |
| Native Feature datum through history | Point/Axis/Plane/Sketch/Modeling, point/axis/plane/whole-Origin supports, both sides, translated and rotated Body, intervening Plane, three source moves | All 38 cases passed. Missing, suppressed and future sources are rejected in Body and document contexts. An incomplete Modeling profile retains its current authored frame without publishing failed calculated faces |
| Tažení 04 in current 02.prtz | Native Feature point through Rovina 01, unchanged OK, three actual diameter edits, one revision per edit, unchanged preceding Sketches, displayed final Body and fresh native reopen | Complete hidden-window GUI workflow passed; the original failure was the omission of native work-frame datums from exact calculated-boundary reference views |
| Tažení 03 R150 | Radius/diameter, either symmetric arc, reversed contact/equality order, X/Y orientations, native/external origin anchors, driving/reference and locked/unlocked states, repeated numeric edits and endpoint drags, impossible fixed edits, Undo/Redo and document roundtrip | All 48 synthetic radial cases passed. Actual 02.prtz R150 changed to 155, 145 and 150 through View label double-click and numeric entry; draft Undo/Redo, Feature OK, save and fresh-window reopen passed |
| Complex underconstrained profile | 38 native points, 12 arcs, 12 segments, eight dimension kinds, both applicable selection orders, two orientations, reference/driving/locked states, repeated edits/drags and independent equations | All 96 synthetic many-arc cases passed. The supplied sheet Part's 38-point Tabule 01 passed 40→45→35→40 and 83→88→78→83, including actual GUI label double-click, Undo/Redo, Feature OK, unchanged preceding sources and native reopen |
| External midpoint | Three line orientations, both source directions, original/body supports, reference/driving/locked length dimensions, C, repeated source edits, source immutability, independent deletion, serialization and Undo/Redo | All 36 native cases and Part/Assembly mouse workflows passed |
| Existing Mirror GUI | Base X/Y, native vertical/oblique axes, rounded/unrounded profiles, source/axis mouse picking, R edits, Cancel, Undo/Redo, dragging and reopen; Mirror/Trim hole dimension from either circle | All eight axis/profile cases and both mirrored hole selection orders passed |
| Midpoint pixels/picking | Native/reference points, front/back, all sectors, Light/Dark with two zoom levels, base/transient scene; endpoint and near-center images compared with a packet lacking the midpoint | All 64 GPU cases passed, including absent distant markers, visible near-center feedback and exact point picking. During native dragging, read-only reference crosses remain in the pickable base context as in the application |
| Existing external references | Frame/reference retirement, finite-outline dimensions, Fillet, rational curves, trim, offset, detachment and nested source identity | Complete reference-command regression passed again with the final kernel, including 384 retirement and 80 finite-outline dimension cases |
| Intersection references | Straight/circular/conic/spline branches, disconnected sections, original/Assembly/context supports, repeated edits, native references and cold reopen | All 541 cases passed; straight-branch midpoint equations and independent branch removal checked explicitly |
| Shared Sketcher equations | External supports, point-pair Distance/X/Y including the new midpoint, R/D, angles, trimming, slots, native and external C/T, fixed/locked/reference cases, repeated edits/drags and serialization | Complete native Sketcher contract executable passed, including 60 external point-pair cases, 288 trim variants, 128 transformed FORM profiles and 64 slot variants |
| Factory data | All 1,048 native files under config, native read/write/reopen in separate audit copies, complete authored definition and cached boundary comparisons | 1,048/1,048 passed, repeated with the midpoint implementation |
| Factory FORM | VentilationWindow, DimpledHole, CornerGusset90 and BridgeLance recalculated, saved and reopened by the native tool | All four authored/current geometry and exact readback comparisons passed; full offline history/role analysis persisted. Native role-order/copy, independent thickness equations, VentilationWindow commands/rotation/symbols, outward Window/Corner insertion, DimpledHole/BridgeLance insertion, Unbend/Bend Back, Undo/Redo and reopen passed |
| Start templates | Four Part/Skeleton and two Assembly templates regenerated with native code, reopen, GUI new-document active context and enabled commands | Native regeneration and GUI context/command checks passed |
| Localization | cs/en/de/fr/ru catalogs, nonempty values and formatting placeholders; review of affected UI text | 2,666 matching keys per language; no new user-visible text |
| Axis/UI contracts | Agreed Axis cleanup, direct local direction, ordered limits, five languages, Light/Dark and actual GPU datum rendering | Feature parameter and Feature Type UI contracts passed on Windows with hidden windows. Offscreen-only rendering produced no OpenGL wire pixels; the native Windows renderer passed |
| Packaging tools | Archive safety/provenance and publisher input rejection, including clean source, exact runtime, tag, launcher and smoke evidence | 15 package tests passed with two platform-specific skips; all 16 publisher tests passed |

Windows evidence does not establish a Linux build or GUI result. Family-instance
camera persistence, all embedded Section/Sweep/Loft camera workflows and a DPI
matrix above the tested monitor scale have not been verified in the GUI. The
listed equations and interactions do not constitute an exhaustive audit of the
entire Sketcher solver. The implementation is shared C++; no kernel or SDK
version changes are part of this work. Original user documents and custom
libraries were not overwritten. The earlier four-row recovery artifact relates
only to the 11:29 snapshot; subsequent tests use separate five-row copies with
Tažení 04 and the newly authored R150 dimension. The R150 snapshot was copied
at its 2026-11-04 15:20:16 saved state (2,545,185 bytes). The protected sheet
project was inspected and tested only through disposable copies. Its geometry
is not a repository fixture or release asset.

Factory readback deliberately discards ephemeral exact-request coverage and its
narrow document-level prefix packet, as the existing loader contract specifies.
The factory verifier normalizes only that disposable expected prefix before
comparing persisted data; durable branch geometry and reference/side bindings
remain subject to exact comparison. A native-file roundtrip followed by a fresh
kernel verifies cold reconstruction of an exact history placement reference.
The model-calculation suite, including this check, passed.

The new axis-endpoint regression explicitly requests the source operation's
exact output packet after a subtractive tool. It failed before the repair with
`Exact requested boundary omitted its authored axis endpoint`. It also checks
successive source lengths, stable attachment identity, resolved position and
independent solid volumes, with and without a Body. The final model-calculation
suite and the supplied-document regression both passed.

Existing test fixtures needed explicit analysis preconditions: named-view
verification must start from completed physical data before asserting that a
later Save does not complete it, and the FORM flatten-volume assertion must
inspect the exact pending body rather than compare its deferred scalar slot to
a measured volume. These changes request native physical analysis on copies;
they do not weaken equations or change modeling behavior.
The dimension-command fixture likewise measures a private native Body snapshot
instead of treating a deferred zero volume slot as a completed measurement. It
additionally checks unchanged cached shape and source fingerprint before explicit
Regenerate, then verifies the independent 100/200 cubic-millimeter equations.

## Coupled dimension audit

R150 failed before the repair because the simultaneous fit rejected a native
Arc/Circle EqualRadius pair, although ordinary constraint solving already
supports that pair. Removing only this relation on an investigation copy made
the edit succeed. A synthetic tangent/symmetry profile reproduced the failure
before the shared radius-equation fix; the accepted edit retains every relation.

The supplied complex sheet profile exposed a separate size/initialization defect.
The simultaneous fit refused every Sketch containing more than 32 native points,
and ordinary line-distance corrections could not settle shared arc endpoints.
Failed circular-profile dimension transactions now fit from the last valid
branch using the existing coupled equations and ordinary final validation.
Successful ordinary solves retain their established behavior. The dense fallback
extends to larger profiles with at most 128 actual scalar unknowns; fixed
points and derived free arc ends are excluded. The existing small-profile path
is retained. This remains a bounded fallback,
not an unbounded sparse nonlinear solver or a claim that every larger graph is
solvable. The fitting algorithm, precision and 48-iteration bound are unchanged.

Observed native edit times on this Windows Release build were approximately
0.7–1.1 ms for the actual R150 sequence and 3.7–5.5 ms for the two complex sheet
dimensions. Setup, Part regeneration and GUI rendering are outside those values.
They establish successful bounded interaction on these inputs, not a universal
performance guarantee.

One old negative EqualRadius fixture fixed only an arc endpoint while leaving
its center free. That geometry has a valid solution at a different radius. The
new mixed-pair support finds it without moving the fixed endpoint. Verification
now checks that valid solution independently and fixes both center and endpoint
for the genuinely impossible, atomically rejected case. No constraint equation
or fixed-point protection was removed.

The audit covers the listed native line/arc/circle workflows. Large imported
profiles containing ellipses, splines, offsets or stored trim modifiers retain
their existing solver paths; this change does not implement those equations in
the bounded coupled fallback. Trimming and external circular supports are covered
by the existing complete contract suite, not by a claim that every 38-point
profile/trim combination was exercised. Native Linux and the earlier GUI gaps
remain separate verification work.
