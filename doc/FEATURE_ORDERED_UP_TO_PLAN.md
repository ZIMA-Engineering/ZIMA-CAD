# Ordered Feature Up To limits and signed correction

Status: implementation design; scoped native-contract approval received from the
user on 2026-10-23. Implementation and acceptance verification remain in progress.
This document records the next modeling task after accepted Windows
[2026102004](releases/2026102004.md). It does not claim an implemented command.

## Inputs, means and result

Inputs are the Feature's owned Sketch, its existing placement, independent End
and Start operations, an ordered sequence of original target references, retained
patch choices and a signed path correction. The result is an exact limiting
chain consumed by the existing additive/subtractive Solid, Thin or Surface
operation. Assembly modeling remains subtractive and positions only immediate
Part occurrences. Use the shared C++ implementation on both platforms.

Reuse the existing Feature dialog, rollback boundary, native references, side
ancestry, kernel topology-history propagation, shared reference cells, numeric
units/locks and Undo/Redo transactions. Do not change shared container placement.
The current source stores a target vector but rejects more than one target in
Part calculation/preview and Assembly profile preparation. Kernel requests
currently carry one forward limit and an optional reverse limit. The target
type is also used by Hole/Opening/Thread; this extension must remain Feature-local.

## Geometric interpretation

- Preserve target selection order. Adjacent targets intersect consecutively;
  their intersections delimit retained patches of a joined limiting surface.
  Do not interpret the list as first-hit priority or intersect every target's
  half-space indiscriminately. Both convex and concave chains are required.
- Permit separated finite faces when their underlying supports can be extended
  to a unique adjacent intersection. Retain the selected original reference,
  oriented side and visible fragment. Extension does not authorize changing the
  source document or substituting another face. A single existing target keeps
  its established behavior.
- Each adjacent split needs an explicit retained-patch side. Seed the initial
  choice from the selected face's authored domain only when it is unambiguous;
  make the opposite patch selectable in the Feature's target controls and store
  that choice. Row reordering must preserve reference identity, then validate
  the new adjacency. Never derive persistent identity from kernel enumeration.
- Reject missing, parallel, coincident or multiply ambiguous intersections,
  disconnected limiting patches, self-intersections and uncovered required
  profile regions with a localized diagnostic identifying the affected rows.
  Do not silently bridge gaps, change order or select an arbitrary branch.
  Validation/calculation failure leaves the current document unchanged.
- Correction follows the operation path: Extrusion translates the joined limit
  along its directed unit vector in document length units; Revolution rotates
  it about its directed axis in angle units. Positive advances beyond the
  contact and negative stops before it. This is distinct from a surface-normal
  thickness offset. Keep End/Start and symmetric reflection semantics and
  reject invalid spans or self-intersection. Preserve oriented sides at zero.
- Retain independent length and angle correction values when switching between
  Extrusion and Revolution. Twist, datum Axis and unrelated Hole/Thread/Opening
  definitions are outside the new multi-surface/correction behavior.

An independent rational-arithmetic design probe uses a 12 by 3 mm footprint
and three planar height patches. The convex chain has volume 396 mm3 and the
concave chain 408 mm3. Blind intersection of all half-spaces gives only 180 mm3
for that concave case. Corrections of -1.5/0/+1.5 mm change the intended volume
by exactly footprint area times correction. This checks the interpretation;
it is not a native OCCT, reference-identity or GUI test.
Evidence: `build/ordered-up-to-measure-probe.json`.

An isolated OCCT 8.0.0 C++ probe also passes 32 finite planar cases. Three
already delimited patches are sewn into one shell, converted to a half-space
with an explicit retained-side point and intersected with a 12 by 3 by 30 mm
prism. Independent `BRepGProp` volumes match those analytic integrals within
1e-7 mm3 for both shapes, -1.5/-0/+0/+1.5 mm correction, both face-insertion
orders and both shell orientations. Face-insertion order here is a runtime
sewing variation, not a test of the user-defined target chain order. This proves
a usable clipping primitive, not automatic patch construction, curved-support
intersection, Revolution, persisted sides/identity or GUI behavior. No production
code or native document was changed. Evidence:
`build/ordered-up-to-kernel-probe.log`; private probe source remains under `tmp`.

## Proposed native data scope and factory conversion

Approval is required by [AGENTS.md](../AGENTS.md), File-format compatibility:
"Before changing a native document format or its required persisted contracts,
inform the user and agree the scope, affected library assets, conversion and
verification plan."

Extend only the Feature definition with per-side length/angle Up To correction
and per-target retained-patch choices needed for ordered adjacency. Keep these
inside the existing native documents, including Assembly-owned Feature cuts;
introduce no sidecar, required external geometry or alternate legacy path.
Defaults for reauthored factory definitions are zero correction and the current
single-target intent. Preserve inactive settings and authored geometry.
Final field names and representation must follow the existing serialization
and side-identity contracts, without introducing redundant stored derivations.

A read-only structural inventory of the accepted committed factory config at
`b96503988b7ba8f98951e9ca2076cf211685c4e0` finds:

| Inventory | Count |
| --- | ---: |
| All factory native assets | 1,185 |
| Parts containing serialized Feature side definitions | 1,016 |
| Affected FORM Parts within that count | 4 |
| Additional currently authored factory Assembly/Drawing/frame/title-block/symbol definitions containing Feature sides | 0 |

The four FORM Parts are BridgeLance, CornerGusset90, DimpledHole and
VentilationWindow. Repeated Container/Entity representations are not distinct
models. Evidence: `tmp/feature-limit-structural-scope-2004.json`. This inventory
does not establish successful conversion or permission to rewrite custom files.

A fresh read-only inventory at `6ea08e046da3b1315d9612f4e91775fd144a4631`
confirms these same counts: 1,185 factory assets, 1,016 affected Feature documents,
four FORM definitions and 6,396 repeated serialized Feature records, with no
parse errors. It streams committed Git objects rather than private working files.
Evidence: `build/feature-ordered-up-to-current-inventory.json`. The separate
native-contract scope was approved on 2026-10-23; the inventory changes no model.

After approval, calculate, save and reopen all affected factory models using
the changed native implementation in isolated committed-source copies. Verify
history, role Bodies, geometry, volume/mass, Relations, original references and
both sides; verify FORM insertion and dependent operations. Update committed
factory assets only after those checks pass. Regenerate all six Part/Skeleton/
Assembly start templates and create real new documents through the GUI to
verify active editable context and normal command availability. Load all 1,185
native assets and check affected dependent Drawings/Assemblies before release.
Preserve personal configuration, the user's rough M10 model, Projects and custom
libraries. Do not bulk-convert those files. An affected unverified asset blocks
publication; Linux verification remains a separate native-host gate.

## Command lifecycle and finite verification plan

Open creation/Properties at the existing input/rollback boundary and publish its
required scene once. Reference entry, inspection, row selection/reordering and
numeric drafts use persisted geometry and lightweight overlays without OCCT.
Unchanged OK performs no calculation and creates no Undo step. Changed OK
validates and calculates privately, then commits once. Cancel restores the
unchanged full-history view, selection and highlights.

Use shared reference cells with independent inspection, meaningful per-row
removal/replacement, exactly one ordering checkbox and Up/Down buttons. Preserve
identity when reordering; disable unavailable moves. Keep upper fields anchored
and give a lower target-list viewport the extra dialog height. Localize all new
labels, diagnostics and guidance in cs/en/de/fr/ru and verify both themes and
live language/theme changes.

The regression matrix must cover one/two/three-plus targets, convex/concave
chains, planes/curved supports, separate finite faces, valid/invalid ordering,
duplicates and ambiguous intersections; Add/Cut, Solid/Thin/Surface; both
directions, independent End/Start and symmetry; zero/positive/negative
length/angle correction; source dimensional changes and reference loss;
repeated edits, value locks/Relations, stable cap/edge/point ancestry and oldest
coplanar owners; native save/reopen, Undo/Redo and actual GUI entry, inspection,
reordering, resizing, unchanged OK and Cancel. Measure kernel/scene operation
counts as well as elapsed time. Report unsupported or unverified variants.

Sketch projection through Feature follows this work. Its associative 3D curves,
visibility, clipping and native result representation require a separate scoped
design; this plan does not approve a new projection format or a mesh feature.

## Implementation checkpoint — 2026-10-23

The operation-local OCCT builder now extends finite supports, delimits each
support by its immediate neighbours, resolves an unambiguous authored retained
side, and sews the patches into one shell. It rejects ambiguous adjacency,
ambiguous authored side, empty patches, disconnected chains and non-adjacent
intersections. Private copies isolate OCCT topology flags and surface caches
from the source operands. A transformed chain retains each semantic input row's
actual descendant face in the transformed shell.

Transient `OrderedUpToLimit` requests are available to the native Extrusion and
Revolution implementations. They retain separate authored source identities,
including occurrence paths, and numerical target resolution. Exact clipping
names each replacement cap from its logical cap/profile parent and selected
source reference, before Boolean history locates the runtime descendants.
Clipping must retain the complete initial profile and remove the complete
overrun boundary. Corrections translate/rotate the entire limiting shell along
the directed operation path. The existing single-limit request remains the
unchanged path used by Hole, Thread and Opening.

The current native kernel regression passes 776 checks. It covers the convex
396 mm3 and concave 408 mm3 planar integrals, source face orientations and input
order, mirrored and rotated clipping, sphere/plane seams at radii 5, 0.1 and
0.01 mm, Solid/Thin/Surface profiles, circular profiles and holes, Solid/Surface
draft, native Revolution, signed corrections, exact borrowed occurrence sources,
cold persisted bindings, source edits and accepted-request cache reuse. A real
1e-9 curved Bezier sag remains curved; plane recognition requires a certified
machine-roundoff bound on every control pole, never model tolerance alone.
Source topology bytes remain unchanged after both successful and rejected builds.

Angular correction follows the original first circular contact before applying
its signed value. It rejects contacts crossing zero or a complete turn instead
of normalizing them into a different revolution. Twelve invalid-span checks cover
both directed axes and reflected targets, including a correction that crosses
zero for only part of the profile. Valid spans near zero and a full turn,
large positive corrections, hollow profiles and Surface walls match independent
angular volume/area integrals. Beyond half a turn, only excess in the terminal
angular window is removed; a global limiting half-space would also cut an earlier
orbit segment. The window follows the actual initial profile ray, including a
nonzero authored start and rigid placement. Both target cap identities survive.

The oriented planar half-space is bounded using all actual extended supports.
Using only the original world envelope could let a rotated UV rectangle protrude
past its neighbour's artificial boundary and split an otherwise valid patch.
The enlarged support remains the exact same analytic plane, with its authored
orientation; no modeling tolerance or source geometry is changed.

The optional native per-side `up_to` block stores independent
`length_correction` and `angle_correction_degrees`, directed source/adjacent
retained-side relationships, and an optional `primary_cap_source`. Neutral
settings omit that block using the existing neutral-override convention, while
negative zero remains an authored value. The primary source anchors the original
generic end cap when the list grows or is reordered; other cap patches use
source-derived names. Display rows never identify persisted geometry.

Explicit calculation returns transient resolution receipts. The owning Feature
accepts previously unspecified choices before the settling comparison; the
kernel records the accepted fingerprint without another calculation. An unchanged
accepted request shares its native shape and face-binding storage. Receipts are
not additional persisted geometry.

Native Workspace checks cover repeated corrections, independent End/Start
limits, symmetry, side reversal with appropriate target sides, Relations and
locks in length/angle units, original cap ancestry, atomic invalid-order and
missing-source rejection, unchanged OK, Undo/Redo, native save/reopen and explicit
cold regeneration. Assembly cuts consume exact occurrence input snapshots through
the same native source reader as Sketch intersections. A real Part-cap source
case checks the Assembly cut, correction edits, cold reopen and occurrence motion
without changing the source Part. Nested metadata and suppression are validated
before descending to a component source; a complete nested-Assembly GUI matrix
remains unverified.

The Feature dialog uses shared reference cells, separate row numbers, one ordering
checkbox, independent eyes, previous/next retained-side choices, Up/Down controls
and a correction field under each target table. It preserves independent linear
and angular values when the operation changes. Axis still requires exactly one
reference; replacing its source clears obsolete multi-target adjacency choices.
The table creates hidden rows lazily and exposes at least three rows. Wide parent
windows grow the dialog to its minimum content width; smaller parents retain
horizontal access rather than clipping the last controls.

Changed multi-target and corrected Revolution drafts use a command-local private
kernel to calculate the exact original operation wire. A 150 ms debounce combines
rapid edits; one worker runs at a time and stale results cannot publish. Failed
drafts clear their wire and expose localized status. Inspection, opening unchanged
Properties and unchanged OK do not invoke the kernel. Opening reuses persisted
original operand strokes; missing stored strokes are reported without an implicit
calculation. The previous nominal untrimmed draft was rejected by user review.
After acceptance,
ordinary selection uses the stored original operand wire at the Feature's own
creation boundary, before later body Booleans can introduce unrelated edges.
There is no new solid fill or thick selection stroke.

The main-window GUI regression exercises actual three-face mouse entry, independent
inspection, short middle-button termination, Cancel, changed and unchanged OK,
native save, correction annotation and Undo/Redo. Standalone dialog checks cover
replacement/removal, duplicates, reordering, retained choices, Axis transition,
precise independent corrections and negative zero, both themes, five languages,
live new-control translation, resizing and delegate colors. Catalog validation,
unit input, shared entry tables and locks pass. Related existing Feature and
Sketch-reference GUI regressions also pass.

Current logs:

- `build/ordered-up-to-final-ui-native.log`: all three new command suites pass.
- `build/ordered-up-to-final-native-regressions.log`: eight kernel/definition/
  existing-limit/side-reversal/Sketch-reference suites pass.
- `build/ordered-up-to-final-localization-units.log`: four catalog/unit/entry/lock
  suites pass.
- `build/ordered-up-to-existing-feature-sketch-ui.log`: five related existing GUI
  suites pass.

Native/editor and factory acceptance are complete in the documented finite
scope. All 1,016 affected Parts pass final native calculation/save/reopen and
identity/property checks; all 1,185 native assets load and all 137 Drawings match
the released-runtime dependency baseline without rewriting their factory files.
All six refreshed templates pass actual new-document GUI creation in mm/in and
all five languages. All four FORM insertion/dependency probes pass. See the
[finite verification and measured results](verification/20261023-ordered-feature-up-to.md).

This is not portable-release acceptance. The full generic physical-cursor hover
suite has a runner limitation (`widgetAt(QCursor::pos())` returns null); dedicated
reference colors, shared tables and actual main-window entry pass. Arbitrary
mixed curved/draft chains and a complete nested-Assembly GUI matrix remain
unverified. Linux execution remains unverified. The private M10 Part, custom
libraries and Projects are preserved. Final committed-source packaging, exact
packaged factory assets, native smoke and publication remain separate gates.
