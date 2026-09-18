# Sheet state history operations

Implemented and verified on Windows, 2026-09-18. This note describes the
architecture, supported scope and regression results. Portable release packaging
is separate from this local development build.

## Inputs, means and outputs

**Inputs:** authored Flat, Sheet Profile and Revolved Sheet regions, their
calculated current material, intervening additive/subtractive history operations,
the selected regions, and the document's sheet calculation tolerance.

**Means:** persisted source ancestry, authored material coordinate frames,
neutral-layer development with the document K factor, explicit OCCT body
calculation, and immutable material contributions reconstructed from native
history. The ordinary container placement contract is unchanged.

**Outputs:** a new original solid owned by Unbend or Bend Back, with new child
identities referring to its source geometry. Earlier features and their original
reference packets retain their identities and coordinates. Later placement and
Sketch references may address the new state's original geometry.

## Forward history and source accuracy

History always runs forward: creation, Unbend, subsequent edits, Bend Back,
and any further state operations. Bend Back does not rewind the document.

Region frames are derived from authored frames and the source parent's known
state at attachment. When that parent returns to the same state, the child's
source origin and axes are copied exactly. They are not reconstructed by
inverting a previously rounded placement transformation. A child created while
its parent is unfolded retains that actual creation state as its source.

Material edits retain the added or removed material in the state in which the
operation was authored. Later state operations evaluate these contributions
directly in the requested target state. They do not repeatedly deform the
already approximated output of the preceding state operation. Returning to an
edit's source state reuses that source geometry. Boolean reconstruction still
has its normal numerical tolerance; this does not promise exact real arithmetic.

The contribution cache is disposable and stays inside the kernel. Native
history parameters and calculated original-reference data remain sufficient to
reopen a document; an explicit cold calculation reconstructs the cache. There
are no required sidecars or revision directories.

## Development maps

For a cylindrical region, developed station is `s = theta * (R + K*t)`, using
the inner radius and the existing Sheet Profile convention. Straight end
extensions and attached planar regions move with their material frames.

A conical region develops to an annular sector. If `beta` is the half-angle,
the sector angle is `theta * sin(beta)` and the neutral slant radius is the
neutral circumferential radius divided by `sin(beta)`. Thickness is measured
normal to the generator, rather than radially to the axis. The implementation
uses the same coordinate map for material and attached frames. The analytic
cone parameterization follows the [OCCT reference](https://dev.opencascade.org/doc/refman/html/class_geom_grid_eval___cone.html).

A mathematical zero-radius hem has a collapsed inner skin. Its inverse material
map uses a small regularization bounded by `min(tolerance/32, thickness*1e-4)`.
At 2 mm thickness and 0.05 mm tolerance this is 0.0002 mm. Regression cases cover
Sheet Cut in a folded hem and ordinary cuts authored in its developed state.

Closed 360-degree material regions currently require an explicit seam and are
rejected. Defining and developing such a seam is still outstanding.
Added material must have an unambiguous material-region assignment; an additive
bridge spanning regions that cannot be separated is rejected instead of being
assigned to an arbitrary region. Cuts are explicitly divided by positive material
domains and can cross a joint between regions.

## Interaction and persistence

Unbend and Bend Back use one shared internal properties dialog with OK/Cancel,
an all-regions checkbox, and a manual region list. Manual values survive toggling
the all-regions checkbox. Common View candidates resolve to source material
regions through calculated metadata; no second picker or OCCT traversal is
introduced. The user's selection filter remains active.

Clicking a region again removes it. Stored list rows have a red cross in the
first cell and a separate inspection eye. Selected inspected regions use cyan
wires. A short middle click ends entry/inspection without changing values;
middle-button double-click invokes OK over the owning View. The operation has
no Origin or container-placement input. Assembly selection follows the exact
active Part occurrence while the top-level context remains visible.

OK performs calculation and commits one history operation. Cancel restores the
unchanged model. Properties editing uses the ordinary boundary before the edited
operation. Dialog opening, list editing, inspection, hover and tab changes must
not invoke OCCT.

The native schema adds `sheet_state` feature parameters and the canonical
`sheet_owner` metadata on calculated faces. Part INI/JSON versions are 39/63;
Assembly versions are 33/45. Start templates follow those versions. No legacy
migration branch is introduced.

The obsolete `bend.unbend` parameter, per-profile View controls, Family numeric
state binding and folded-reference snapshot pass have been removed. Family Table
can control the presence of the separate state feature using its standard
Yes/No binding. Original Sketch references use the ordinary persisted geometry
path in every state.

## Console commands

`unbend.create` and `bend_back.create` accept `all`, `owners`, `name`, and
`document`. Their `.set` counterparts additionally require `container`.
Supplying `owners` selects manual mode unless `all` is explicitly specified.
Owners identify authored sheet creators, including when View selection was made
on a later derived state's geometry. Already unfolded/folded or missing regions
are rejected; an empty all-regions result also produces a validation error.

```json
{"command":"unbend.create","arguments":{"all":true}}
{"command":"bend_back.create","arguments":{"owners":["<sheet-profile-id>"]}}
```

Both commands use the same atomic workspace transaction as GUI OK.

## Verification — 2026-09-18

The geometry test covers cylindrical profiles at 30, 90 and 180
degrees, changed end widths, cylindrical and conical Revolved Sheet in both
directions, a zero-radius hem, straight continuations at 45/90/180 degrees,
selective/all independent regions, attached Flats, and material edits between
states. Through holes, pockets, bosses and enclosed voids are covered. Cuts in
variable-width profiles and across Sheet Profile–Flat joints are also covered. Removed
material is attributed by intersection with authored positive material domains,
so a void need not retain an original skin face to follow its sheet.
Tests distinguish linear position error from
volume error. Fifty frame cycles check exact restoration of source frames;
repeated cut cycles check source-state vertex positions within 0.000001 mm.

Native reopening, history editing, Undo/Redo and the cross-branch box passed the
command suite. A Flat attached to an Unbend-owned original end edge and an ordinary
Extrusion cut authored in the developed state survive Bend Back and another
Unbend. Reloaded native history also recalculates correctly with a new kernel and
no previous calculated boundaries.

GUI checks passed in a Part and an Assembly containing repeated occurrences:
common picking, user filter enforcement, View and Tree selection/removal,
short-MMB, Cancel, OK, editing rollback and MMB double-click confirmation over
the View. Active Part container rows inherit their occurrence path from the
component ancestor; the new command resolves that path without changing general
placement or Tree ownership. Both dialog screenshots were inspected, including
cyan region wires and the passive Assembly context.

The complete Windows C++ target build succeeded. Across the final focused runs,
**27 distinct CTest contracts passed**: the four new sheet-state contracts plus
Flat, Sheet Profile/Sheet Cut, profile commands, Family Table, native documents,
document transactions, model calculation, core and Assembly geometry, original
Sketch/context references, viewer selection, translations, dimension identifiers
and layout, sheet wire previews, and affected GUI workflows.

The six core/creator contracts passed in 89.37 seconds. The broader 22-contract
run and final three-contract rerun together verified the remaining scope; the
final rerun passed in 12.54 seconds. An initial concurrent GUI launch collided
with the working-directory lock and was rerun sequentially. Test fixture issues
in XZ coordinates and inherited Tree paths were corrected before that final run.
This is focused regression verification, not a claim that the entire repository
suite or a new portable release was validated.

Local evidence: `build/sheet-state-regression.log`,
`build/sheet-state-fixes.log`, and
`build/cpp-windows-release/sheet-state-ui-contract/sheet-state-{part,context}/sheet-state-properties.png`.
