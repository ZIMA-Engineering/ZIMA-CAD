# Ordered Feature Up To limits and signed correction

Status: implementation design, awaiting scoped native-contract approval.
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
native-contract approval request remains pending; this check changes no model.

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
