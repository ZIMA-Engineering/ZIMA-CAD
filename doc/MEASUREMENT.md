# Measurement in the View

**Measurement** is on the toolbar above Part/Assembly 3D views and is also available
inside Sketcher. It measures current calculated geometry without regenerating
the model or dependencies.

## Selection and results

The window has two vertically arranged references. First selection immediately
shows that entity's properties and arms the second field. The second reference is
optional. Its properties appear below its field; shared shortest distance appears
below both summaries. Two bodies therefore each have their own volume, area and mass.

| Entity | Information |
| --- | --- |
| Point | X, Y, Z in displayed space |
| Edge or curve | Length |
| Bounded face | Area |
| Body or component | Surface area, volume; mass if density is known |
| Axis | Point and direction of the infinite axis |
| Construction plane | Point and infinite plane |

Two references additionally show shortest distance and a line joining its endpoints.
Faces are measured within their boundaries; axes/construction planes are infinite
for distance calculations. Overlapping bodies and points inside closed bodies have
zero distance.

Selection uses common View candidate ordering; RMB cycles overlapping candidates.
Clicking reference text arms entry/replacement (green outline). The eye independently
toggles azure inspection of the stored reference; the cross removes it.

- Short MMB ends entry and temporary inspection highlights, retaining references
  and results in the window.
- MMB drag navigates the View.
- MMB double-click, including over the View, invokes OK and closes the window.
- **OK** and **Cancel** do not save measurements to history.
- **Save** stores a named measurement in the displayed document tree and closes.
  It expands parent branches, selects the record and scrolls to it. The item also
  appears in the Sketcher tree.

## Accuracy and units

Exact edge lengths/face areas available from explicit body calculation are saved
with geometry and read without further OCCT calls. Whole-component volume/mass
uses the source's last calculated data. Mass is not estimated without material density.

Distances to curved geometry use its polygonal representation. These, and lengths/
areas without saved exact values, are marked **≈**. Their accuracy depends on display
resolution. Distances between points, straight edges and planar polygonal faces are
calculated directly.

Output uses document length/mass units, with squared/cubed length units for area/
volume. Decimal separator is a comma. Rounding to configured precision removes
trailing zeroes: `12mm`, `240mm²`, `0,047kg`, for example.

## Saved measurements

A measurement is information, not a body operation. Part inserts it at the current
Body-history position; Assembly inserts it in the Assembly tree. It stores a name,
stable references including occurrence paths, and last-saved results. In Assembly
context it belongs to the displayed Assembly. Saving/removal requires that document
to be active too. With a nested Part active, inspection remains readable but Save
is disabled; it cannot write to the passive parent Assembly on behalf of the Part.

**Properties** reevaluates references against current geometry. Missing references
are red and replaceable in the same field. Saving is blocked until repair; cancelling
preserves the original record. Context menus also offer **Remove**. Creation,
editing and removal support document Undo/Redo.

Saved values snapshot the last measurement save. Opening Properties reevaluates the
currently displayed calculated model. Source Part changes appear in occurrences
without Assembly regeneration; mate solving and Assembly-owned operations run only
on explicit **Regenerate**. Reading measurements does not trigger them.

## Related dimension controls

A single component click confirms it in azure. Double-click shows its placement
dimensions. Dimensions are independently selected over their values; double-click
opens value editing, RMB offers Dimension Properties. Locks and value bounds remain
effective. Empty clicks or ending dimension display hide them.

Generated diameter dimensions in Sketcher, Part, Assembly and Drawing use
**⌀ (U+2300)** from the shared symbol list, including outputs using shared formatting.

## Verification

- `zima_cpp_measurement_contract_tests`: analytical distances, bounded faces,
  intersections, closed bodies, translated models, saved exact cylinder quantities,
  Part/Assembly persistence, Undo and diameter symbol.
- `zima_cpp_measurement_inspector_ui_contract`: real common picker, both entity
  summaries, MMB actions over View, Save, tree expansion/selection, reopening and
  missing-reference repair. Two components with different densities check both
  summaries and analytical distance. Returning to Part after closing Drawing
  Dimension Properties checks dialog release and restarting Measurement without
  a crash.
- `zima_cpp_assembly_refresh_ui_contract`: component selection, double-click,
  dimension availability and its own editor.

## Read-only CLI commands (2026-09-13)

- `measurement.list [offset] [limit] [document]` lists saved records. Default limit
  2000, range 1–10000; offset is nonnegative.
- `measurement.get object [document]` returns original references and last-saved
  values, including shortest-distance endpoints. `saved_values: true` explicitly
  identifies the saved snapshot. It neither refreshes nor saves.
- `measurement.evaluate references [document]` evaluates one or two references
  against current calculated data without GUI, history, saving, OCCT or mate solving.

```json
{"command":"measurement.evaluate","arguments":{"references":[{"kind":"plane","owner":"<part-id>:origin","key":"origin:plane:xy"},{"kind":"face","owner":"<original-feature-id>","key":"<original-face-key>"}]}}
```

Reference kinds: `point`, `curve`, `face`, `object`, `axis`, `plane`. Topological
references require original `owner/key`; coordinates/substitute geometry are not
accepted. Whole objects use `kind: object`, their `owner` and empty key. Whole
components use empty owner and exact `instance_path`. Complete encoded paths
identify nested occurrences, not names/shared source Part IDs. Kind must match
geometry: curved faces cannot masquerade as infinite planes.

Missing geometry returns `missing_reference` and zero-based `reference_index`.
The last-saved record stays readable. Queries can target other open documents without
activation and work during open dialogs. Drawing measurement is a separate dimension
area; these commands target Part/Assembly.

Machine output always uses **mm, mm², mm³ and kg**, independently of GUI display
units. `units` describes each quantity. Length, area, volume, mass and distance
contain `value` and `approximate`. Unavailable values are `null`, never zero.
Saved exact circular-edge length stays exact with coarse display; distances to
curved meshes depend on resolution.

Shared `zima_measurement` depends only on `zima_kernel_api`, not Qt, and contains
the inspector's original geometry algorithm shared by GUI/CLI. Viewer only converts
picker candidates to measurement references. Workspace supplies authoritative
volumes, areas and masses. GUI retains whole-occurrence measurement in section views.

Initial model suites passed **2/2 in 0.35 s**: analytical geometry, shortest distance,
independent volumes/units, exact circular length, invalid input, unchanged history/
cache, repeated nested occurrences, inactive-document reads and native Part/Assembly
save. This first stage introduced three reads; record mutations followed below
through a separate shared GUI transaction.

Both applications and all tests built. Integration passed **5/6 in 87.48 s**; the
only failure was an old catalog expectation of 226 instead of 229. After updating it,
catalog/full startup including translations passed **2/2 in 96.19 s**. Additional
valid-axis and exact-circular-length versus approximate-distance checks passed
**1/1 in 0.25 s**. The circular-rim test uses the cylinder's axial plane, avoiding
dependence on top/bottom rim order.

GUI regressions compare values and both shortest-distance points against console
queries, including while the inspector is open, and read GUI-saved measurements.
A separate CLI process runs with an intentionally invalid Qt platform name, proving
no window initialization is required. Checks also preserve exact circular length
and reject curved faces as planes or circles as straight axes. At this stage:
**229 commands**, **128 tests**.

Logs: `build/measurement-query-integration-build.log`,
`build/measurement-query-integration-tests.log`,
`build/measurement-query-catalog-startup-tests.log`,
`build/measurement-query-precision-tests.log`.

## Saving and editing through CLI (2026-09-13)

- `measurement.create` takes `references` and optional `name`, creating a stable
  record anchored to the current Part Body/history position.
- `measurement.set` takes `object`, optional `name` and `references`. Omitted
  references remain. Supplying only `object` explicitly refreshes saved values from
  current calculated geometry.
- `measurement.delete object` removes exactly one record.

```json
{"command":"measurement.create","arguments":{"name":"Check distance","references":[{"kind":"plane","owner":"<part-id>:origin","key":"origin:plane:xy"},{"kind":"face","owner":"<original-feature-id>","key":"<original-face-key>"}]}}
```

All three share GUI Save/Remove transactions. Inputs cannot supply calculated
values, substitute coordinates or history positions. The shared operation evaluates
original references in a private draft and commits a fully valid result in one
document-history step. Identity and anchoring are preserved. Names are trimmed,
validated and checked for duplicates. Lost references/errors cannot overwrite
last-saved results. Unchanged saving adds no Undo step.

Commands target the displayed active Part/Assembly. When a nested Part is activated,
end activation or open its source separately first, keeping measurement ownership
and coordinates unambiguous. Whole-scene inspection remains available. Open inspector
drafts are guarded by revision, data generation and runtime document identity.
A change followed by Undo, or close/reopen, must not make an old draft valid again.

These mutations recalculate no bodies, mates or placement. They store existing
native measurement records only; extensions, formats and start templates are unchanged.
Part/Assembly support Undo/Redo and native save/reopen.

After correcting two fixture names, model suites passed **3/3 in 0.67 s**: creation,
changed values, deletion, no-op, atomic errors, Undo/Redo, drafts becoming stale on
Undo/reopen, identity/history, distinct occurrences, preserved B-Rep and Assembly
source-data sharing.

Both applications and all tests built. Integration passed **8/8 in 223.07 s**:
actual CLI process, complete inspector, command tests, catalog, GUI console and
startup with translations. GUI checks CLI → Properties → Save, unchanged save,
Delete/Undo and readable inspection without passive-Assembly writes.

An extra whole-sketch/solid/plane regression initially failed **0/1 in 0.18 s**:
the picker copied an auxiliary display category into the measurement reference.
Whole-object identity consists of owner/occurrence with an empty subentity key.
After normalization at the picker boundary, it passed **1/1 in 0.17 s**. Both apps
rebuilt and final measurement suites passed **4/4 in 3.52 s**, including GUI and
saving a real standalone-sketch measurement. Its distance uses finite geometry
without inventing a volume.

At this milestone: **232 commands**, **129 tests**. Logs:
`build/measurement-edit-integration-build.log`,
`build/measurement-edit-integration-tests.log`,
`build/measurement-object-identity-baseline-tests.log`,
`build/measurement-object-identity-tests.log`,
`build/measurement-edit-final-build.log`, `build/measurement-edit-final-tests.log`.
