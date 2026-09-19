# Family Table

## Workflow

Open **Tools > Family Table** in a Part or Assembly. The internal properties
window requests 2280 px width (three times its former width) and stays inside the
main window. Horizontal scrolling accommodates additional columns.
The same command and icon sit immediately after **Parameters** in the View toolbar
and in the context menu of a Part or Assembly filename at the root of Tree. A
root command always edits that root document even while a nested component is
active; the toolbar command follows the active Part or Assembly.

The first row represents the generic document. Click its reference cell, then
click an original solid in View or an owned item in Tree. The column uses the
element name and variant cells offer **Yes / No**. In Part, original feature
presence and whole independent Body presence are supported. In Assembly, select
an immediate owned component. A component column refers to that occurrence,
including when another occurrence uses the same source file.

Presence cells display the localized Yes/No label on an opaque editor background;
the canonical `yes`/`no` value remains internal and does not paint beneath it.

Double-click a Part feature in View to show its dimensions, then click a dimension
to bind the active column. Sheet state is controlled by the presence of the
separate **Unbend** or **Bend Back** history feature, using the ordinary Yes/No
binding. An empty value inherits the generic presence. The obsolete per-profile
numeric state parameter has been removed. Variant edits update their override,
while generic presence and sibling overrides remain independent.

For numeric parameters, click a dimension
to bind the active column. Its header uses the existing stable secondary name
(`d1`, `d2`, …), while the base cell shows the current numeric value. A renamed
element retains its identity. Missing or unsupported references are rejected.

The green outline marks the one active input. The adjacent eye independently
toggles azure inspection of an assigned reference. A short middle click ends
entry and clears inspection, preserving values. Middle-button double-click uses
the shared OK contract. OK commits the table in one Undo step; Cancel discards
pending table edits. Picking and inspection never invoke OCCT.

Name variants in the rows below the generic. Empty values inherit the generic;
numeric values override dimensions, and **No** suppresses the selected feature,
Body or component. Suppressing a subtractive feature removes its cut. Columns
cannot independently control both a Body's presence and presence of its own
features, which would make the result depend on column order.

Use the Open icon in a variant's row header (or its **Open variant** context action) to
commit the table and open the calculated variant in a separate tab. Generation
uses a private draft and validates the resulting history before inserting or
updating that tab. Invalid dimensions, unsolved Sketches and unavailable required
references leave the generic geometry and existing variant unchanged.

Row delete/entry indicators occupy the first table cell, using the same indicator
widgets as Container Placement. The row header is reserved for Open; double-click
the variant name to edit it normally. The generic row cannot be deleted and one
blank variant row remains available. Material, Parameters and Relations use the
same first-cell actions with numbered row headers.

## Linked models, saving and drawings

The 2026-09-17 row-layout change was verified with the entry-table and general UI
contracts, Family Table data and GUI contracts, native file-rename contract, and a
dedicated GUI rename contract covering Part and Assembly variants. The latter
uses the actual Rename dialog and Family Table editor, checks tab labels and
stable row/document IDs, and verifies that native file paths do not change.
Screenshots of all four entry tables were inspected after the first-cell controls
were moved; superseded cell widgets are hidden immediately during refresh.

Each variant is a linked view of one row in its parent Part or Assembly. Its
stable identity is the parent document ID plus the row ID. Renaming a row changes
its display name, never its identity. A variant cannot own another family;
opening Family Table from a variant edits the parent's table and opens siblings.
**Rename** on an open variant edits that row's name and immediately updates the
tab; it does not rename the parent's native file. Editing the name in Family Table
updates any already open variant tab after OK. Both directions preserve the row
and document IDs and reuse calculated geometry. Native `rename_file` rejects a
family member; use its name or rename the generic file explicitly.

Corresponding features, Sketch curves and their semantic topology ancestry retain
their identities across variants, qualified by the variant document identity.
This does not guarantee identical resulting topology: dimensions and suppression
can remove or split geometry. An unavailable reference must remain unresolved
rather than silently binding to a different edge or face.

Drawing model annotations follow that same authored identity. When a view source
is switched between the generic and a Family Table member, shown dimensions,
axes and construction geometry retain their visibility and local layout, then
reproject from the selected member's current calculated geometry. An annotation
absent from that member is hidden as unresolved; its display intent is retained
and restored if a later selected member supplies the same authored annotation.

- Editing a dimension or presence controlled by a column changes that variant's
  row. The generic baseline and other rows retain their values.
- Editing a parameter outside the columns, adding/removing a feature, or changing
  common model data updates the parent and every evaluated variant. Missing bound
  references removed from a variant are also removed from the table.
- Undo/Redo from any member operates on the same parent history and restores open
  variants together. Calculations and validation finish before publication.
- Ordinary **Save** from a member saves the entire family into the parent's one
  `.prtz` or `.asmz` file. Closing a variant closes its tab; unsaved changes
  remain owned by the parent. Closing the parent also closes its variant tabs.
- **Save As** from a variant writes a new, independent native document containing
  that variant's current model and geometry. It creates exactly one model file,
  without copying companion Drawings. The original family remains linked.
  A copy of the generic retains its table definitions with a fresh identity;
  its derived variants are calculated when explicitly opened.

The parent file contains shared history, the table and evaluated native packets
for opened variants. No variant sidecar files are required. Unopened rows inherit
future shared edits when calculated. Opening an already evaluated row, switching
tabs and reading a saved Drawing source use persisted calculated data. Name-only
changes reuse geometry. Changing numeric or presence values is an explicit model
transaction that recalculates affected evaluated variants. Close a variant tab
before deleting its table row.

Drawing Settings registers the source files. The bottom **Source** selector
includes their native models and Family Table variants, including evaluated
Assembly variants with suppressed components. An empty source list disables the
selector and hides Part/Assembly navigation. Each sheet retains its own selected
source for navigation and new views; existing views are independent.

A title block captures its source and variant on insertion. Later chooser changes
do not change its title metadata or BOM. Balloons resolve only against this bound
BOM. Removing the source clears the binding without selecting another model.
See [Drawing sources](DRAWING_SOURCES.md).

A renamed variant remains the same Drawing source. Open-source metadata is
authoritative; otherwise the Drawing reads the evaluated variant from its parent
file. Names and title-block values refresh on opening/displaying the Drawing;
closed Drawing files are not rewritten. Geometry changes still require explicit
Drawing **Regenerate**. Moving the parent file uses the existing file relocation
workflow; renaming a row does not move any file.

One evaluated variant can supply any number of independent Drawing views. A
Revolved Sheet centerline used as its feature axis is presented as one axis
annotation, because the authored centerline and calculated axis intentionally
share one persistent reference. This prevents an isometric view from blocking a
later Front, side or top view of the same variant.

## Assembly insertion and Replace

Inserting a Part or Assembly with at least one Family Table variant opens an
internal variant chooser. An empty Family Table skips the chooser and inserts
the native model directly, then opens component Properties. The first chooser
row is the native/generic model; the remaining rows use stable
Family Table identities. This applies both to open models and insertion from a
native file. OK prepares the selected variant and continues to component
Properties. Cancel leaves the Assembly unchanged. An unopened row is calculated
only after confirmation. Middle-button double-click over View confirms the chooser;
a short middle click does not.

Use **Replace…** in an immediate component's context menu to switch between its
generic model and family variants. Activate the component's owning subassembly
before replacing one of its children. Replace preselects the current row and
preserves the occurrence ID, placement, flags, locks, custom name and stored mates.
It updates only that occurrence; other insertions of the same file are independent.
Replace the original of a Mirror/Pattern rather than a generated copy.

Missing mate references do not prevent replacement. Their original keys remain
stored, the affected component is marked red in Tree, and its Properties can
repair the references. No nearest-face substitution or automatic mate deletion
occurs. Undo/Redo restores the complete replacement transaction. Calculation
failures and dependency cycles still leave the live Assembly unchanged.

Native source caches distinguish both parent file path and variant document ID.
Several variants from one file therefore remain distinct after cold opening,
including in nested Assemblies. Unsaved open parent data is authoritative.
Refreshing already calculated source display does not solve mates or run OCCT.

## Replace in Drawing view Properties

The first field in a main view's **Properties** is the source dropdown. It offers
the native model and every family row, including closed and unevaluated variants.
Identity is matched by document/row ID; the shared native file path does not select
a different variant. The source file button also lists the chosen file's family.

Selecting an evaluated row previews its persisted geometry. An unevaluated row
shows an explicit notice and is calculated in a private workspace only on OK.
Cancel and failed calculation publish neither the row packet nor a Drawing edit.
Successful evaluation becomes part of the owning family's native data; save that
parent model to persist the newly evaluated row. No extra variant file is created.

OK reprojects the main view and its projected descendants in one Drawing Undo
transaction. Independent main views retain their own sources. Projected views
cannot select a source independently. Replacing a view leaves the sheet chooser
and title-block binding unchanged. View IDs and annotation
references remain stable; missing geometry follows the existing unresolved
annotation behavior. Source selection is also available after reopening a Drawing
with its parent model closed.

## Supported numeric references

The current catalog includes primitive dimensions; Extrusion/Revolution extents,
profile offsets and thin thickness; Holes diameter/profile offset; opening,
Fillet/Chamfer/Shell dimensions; sweep thickness/pitch; and driving dimensions and
corner radii of ordinary or owned Sketches. Locked/reference-only dimensions are
not offered. Numeric values use the model's existing mm/degree parameter units.
Free text, material changes, constrained placement dimensions, and editing a
component's source Part dimensions from its owning Assembly are outside this
first implementation. Shared container placement contracts are unchanged.

## Native data and commands

Part INI version **26** (internal payload 50) and Assembly INI **22** / payload **34** carry
the current Family Table schema. Both tracked start templates and native fixtures
are updated. All definitions remain inside `.prtz` / `.asmz`; no sidecar is needed.

Query `document.family.references` to obtain valid `owner`, `key`, `kind`, `name`
and base `value`. A table uses ordered column names and a binding for each column:

```json
{
  "columns": ["d7", "Cut"],
  "bindings": {
    "d7": {"kind": "dimension", "owner": "<feature-id>", "key": "parameter:length"},
    "Cut": {"kind": "feature", "owner": "<cut-id>", "key": ""}
  },
  "instances": [
    {"id": "", "name": "Long", "values": {"d7": "20", "Cut": "no"}}
  ]
}
```

Pass this object as `table` to `document.family.set`. An empty instance `id`
allocates a stable row identity; subsequent updates retain it, including renames.
`document.family.get` returns the normalized table. `document.family.open` takes
`instance: "Long"` and opens the resulting document. Command mutations follow the
existing active-document and pending-dialog guards.

## Verification

Independent volume checks use a 10 × 8 × 6 mm block, a 2 × 2 × 6 mm subtractive
feature and a separate 2 × 2 × 2 mm Body. The generic has 464 mm³. Changing length
to 20 mm gives 944 mm³; removing the cut gives 968 mm³; omitting the separate Body
gives 936 mm³. The tests also cover Undo/Redo, stable row/column identities, cold
native regeneration, Drawing source persistence, CLI access, missing references
and atomic calculation failure. The GUI regression exercises real View picking,
dimension double-click, reference states, middle-click and opening a row in a tab.

### Verification commands

Run `zima_cpp_family_table_tests` for row versus shared edits, shared Undo/Redo,
one-file persistence, independent Save As, stable rename/closed Drawing resolution,
Part and Assembly variants, CLI and atomic rejection. The GUI scenario is
`zima_cpp_workspace_startup_contract` with `ZIMA_VERIFY_FAMILY_ONLY=1`; it drives
actual View/Tree picking, reference inspection, middle-click confirmation,
Save/Save As and Drawing source selection. Generated test artifacts are not release
inputs. Release acceptance is recorded in the current release notes.

Acceptance logs (2026-09-15): `build/family-text-all-build.log`,
`build/family-text-accept-tests.log` (initial broad run),
`build/family-text-source-errors-tests.log` (source identity corrections),
`build/family-command-host-tests.log` (catalog/Save As commands), and
`build/family-linked-assembly-gui-tests.log` (Part and Assembly Drawing sources).
All selected contracts ultimately passed. The multiline Drawing capture was
visually inspected and its PDF text was independently extracted.

Follow-up session checks cover consecutive edits through the same open Part or
Assembly variant, monotonic document revisions/viewer generations, and refreshed
Assembly occurrence geometry without Assembly regeneration. Evidence:
`build/family-session-generation-tests.log` and `build/family-final-gui-tests.log`.

### Insertion and replacement acceptance (development 2026091509)

The component fixture independently checks 480, 960 and 240 mm³ variants,
two occurrences sharing one parent file, native/variant replacement, preserved
mates and locks, a suppressed source with unresolved mates, Undo/Redo, CLI,
unsaved-parent updates, cold nested sources and family dependency cycles.
The GUI scenario exercises the actual insertion menu and Replace context action,
native preselection, middle-button confirmation, Cancel, red missing-reference
state and subsequent mate repair.

Drawing GUI checks include Part and Assembly source replacement, stable selection
when several rows share a file, unevaluated and invalid rows, Cancel, projected
children/grandchildren, independent main views, one-step Undo/Redo and cold parent
loading. The projected width is checked independently against the 5 mm variant;
an unevaluated 3 × 6 × 4 mm variant is calculated with its parent closed and saved
back into that single native file, where its volume is checked as 72 mm³.

Build and regression evidence: `build/family-replace-final-core-build.log`,
`build/family-replace-final-core-tests.log`,
`build/family-drawing-replace-final-build.log`,
`build/family-drawing-replace-final-tests.log`,
`build/family-drawing-replace-cold-build.log` and
`build/family-drawing-replace-cold-tests.log`. The initial broad run exposed two
reference error-translation regressions; the corrected refresh/geometry tests
pass in the final log. Seventeen distinct selected contracts pass across these
runs. Chooser, unresolved-component and Drawing Properties captures under
`Projects/test/family-*.png` were visually inspected and remain disposable test
artifacts. This verification does not publish a new portable release.

The `2026091512` follow-up passes the complete workspace startup contract in
`build/startup-insertion-tests.log`, including direct insertion of the native
model with an empty Family Table. Its earlier failure came from the test leaving
Assembly Sketch Properties unconfirmed, not from the variant chooser. The
separate GUI family scenario (`ZIMA_VERIFY_FAMILY_ONLY=1`) also passes in
`build/startup-family-insertion-tests.log`, covering variant insertion, native
selection, Cancel and replacement. Production behavior is unchanged.
