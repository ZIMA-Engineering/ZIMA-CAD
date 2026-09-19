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

An authored contribution with a known material owner is partitioned only by that
region's own start/end planes. Other selected bends must not cut this already
attributed material. This fixes a tilted cone that unfolded individually but
failed the surface-fit tolerance check during Unbend All alongside adjacent
Sheet Profiles. The fitting tolerance is unchanged; avoiding unrelated partitions
also avoids unnecessary Boolean work.

## Bend lines in developed material

Each unfolded Sheet Profile publishes a persisted axis on its inner skin at
half its developed curved length: `theta * (R + K*t) / 2`. The axis runs across
the sheet width. Its endpoints interpolate the authored start/end sections at
that midpoint, so changed profile widths are respected. A straight continuation
does not move the bend line. A Revolved Sheet also receives an inner-skin
centerline halfway through its developed angular span. On a cone this follows
the annular sector's middle generator. It identifies the development midpoint,
not a claim that the rolled surface is manufactured with one press-brake stroke.
The inner skin respects the region's thickness sign and inherited orientation.

The axis belongs to the derived state feature and its semantic identity records
the source Sheet Profile/arc or Revolved Sheet/generator. Viewer and Drawing consume the
already calculated axis packet; displaying, picking and dimensioning it require
no OCCT work. Native viewer/reference serialization already stores these axes,
so no new document schema or template revision is needed.

Subsequent cuts retain the currently active bend lines. Selective Bend Back
removes only lines belonging to regions that become folded. Historical axes
remain original references, but Drawing Show/Erase offers bend lines only for
the current material state. They use the ordinary Axis annotation and dimension
attachment mechanism, with an individual width envelope rather than the bounds
of the whole part. Assembly occurrences transform them through the existing
source/occurrence path.

Unbend hides the original Revolved Sheet rotation axis from the active Viewer
and Drawing annotations. Bend Back restores that axis, copying its original
coordinates exactly when its attachment returns to the authored state. Original
reference packets remain intact throughout this visibility change.

## Interaction and persistence

Unbend and Bend Back use one shared internal properties dialog with OK/Cancel,
two exclusive checkboxes, and a manual region list. **Unbend all** or
**Bend back all** is visibly checked by default. Clicking **Select individual
features** enables manual View/Tree selection and unchecks All. Clicking All
switches back in one click. Clicking the already selected mode cannot uncheck
it: exactly one mode is always active. Manual values
survive switching between these modes, and editing restores the saved mode.
Common View candidates resolve to source material
regions through calculated metadata; no second picker or OCCT traversal is
introduced. The user's selection filter remains active.

Unbend and Bend Back remain editable history rows, but they are transparent to
ordinary View container selection. Their calculated faces keep state-owned
topology identities for later references while carrying the first authored sheet
feature as a separate display owner. Hover, confirmation and cyan highlighting
therefore select the original Sheet Profile, Revolved Sheet or attached sheet
feature through the currently displayed developed/refolded geometry. Geometry
created only by a state operation has no ordinary container candidate. Repeated
state cycles preserve the same authored display owner. When a Part occurrence is
active inside an Assembly, ordinary selection is restricted to authored
containers of that exact occurrence; the other occurrences remain visible as
passive context.

The Sheet Metal toolbar and history tree use six distinct, theme-aware SVG
symbols for Sheet Blank, Sheet Profile, Revolved Sheet, Sheet Cut, Unbend and
Bend Back. Unbend/Bend Back no longer reuse their creator's icons. The inactive
Piping entry is omitted from the Applications menu. Green separators after
Sheet Metal Properties, Revolved Sheet, Sheet Cut and Bend Back divide the
right-hand command panel into its functional groups.

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
states. Through holes, pockets, bosses and enclosed voids are covered. Both an
ordinary subtractive Extrusion and Sheet Cut are authored after Unbend, carried
through Bend Back, and compared again after a second Unbend. Cuts in
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
placement or Tree ownership. The same check confirms that state rows are not
ordinary View candidates, that the authored feature is offered through state
geometry, and that this display ownership survives native serialization and
repeated state cycles. Both dialog screenshots were inspected, including cyan
region wires and the passive Assembly context.

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

## Bend-line and tilted-cone follow-up — 2026-09-18

The reported `04.prtz` failure was reproduced before the partition fix. The
cache-free `tilted-cone-with-bends.prtz` regression fixture preserves its authored
geometry. Unbend All and individual region selection now pass without relaxing
the 0.05 mm document tolerance. Known rigid material contributions bypass the
splitter entirely, which also preserves attached Flats during state changes.

Five sheet-state CTest contracts passed in 52.24 seconds. Coverage includes:

- Twenty cylinder/cone combinations of generator slope, axis endpoint order
  and revolution direction; eight also cut the developed cone, fold it and
  develop it again. Source-state cut vertices are checked within 0.000001 mm.
- Midpoint station, inner-skin depth, section-width interpolation, persistence
  through later material edits, selective visibility, and exact restoration of
  the source rotation axis.
- Actual Drawing annotation discovery, line picking and an edge-to-bend-line
  dimension with the expected numerical value, including serialized references.
- Cold native regeneration and save/reopen of the attached tilted-cone model,
  as well as Unbend/Bend Back history cycles.
- Default all-region behavior, checked individual selection, restoration on
  Properties editing, View/Tree picking, MMB confirmation, and repeated Assembly
  occurrences.

The GUI verification also opens the tilted-cone model, checks its three developed
bend lines and hidden rotation axis, then folds it and checks restoration.
Screenshots of both states, the Assembly dialog and the six icons at 24/48 px
were visually inspected. Evidence is under
`build/cpp-windows-release/sheet-state-ui-contract/`; the focused test log is
`build/sheet-lines-tests.log`. Windows desktop and CLI targets were rebuilt for
the normal local launcher. This is local-build verification, not a new portable
release.

The follow-up regression run passed another **13 contracts in 159.22 seconds**:
Sheet Profile and Flat commands, profile creation, model calculation, native
documents, Viewer and context references, Drawing annotation commands, Show/Erase,
measurement dimensions, profile-on-sheet GUI, bend attachment GUI and application
tools GUI. Together the final runs passed **18 distinct contracts**. The regression
log is `build/sheet-lines-regression-tests.log`; no full-suite or release-signing
claim is implied.
