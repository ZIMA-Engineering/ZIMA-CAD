# Family Table

## Workflow

Open **Tools > Family Table** in a Part or Assembly. The internal properties
window requests 2280 px width (three times its former width) and stays inside the
main window. Horizontal scrolling accommodates additional columns.

The first row represents the generic document. Click its reference cell, then
click an original solid in View or an owned item in Tree. The column uses the
element name and instance cells offer **Yes / No**. In Part, original feature
presence and whole independent Body presence are supported. In Assembly, select
an immediate owned component. A component column refers to that occurrence,
including when another occurrence uses the same source file.

Double-click a Part feature in View to show its dimensions, then click a dimension
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

Double-click an instance name (or use its **Open instance** context action) to
commit the table and open the calculated variant in a separate tab. Generation
uses a private draft and validates the resulting history before inserting or
updating that tab. Invalid dimensions, unsolved Sketches and unavailable required
references leave the generic geometry and existing variant unchanged.

## Models and drawings

An instance is a complete native Part or Assembly with its own document identity,
original feature identities and calculated geometry. Use ordinary **Save** to
choose its `.prtz` or `.asmz` path. The instance can regenerate after reopening
without the generic document. Create its Drawing with the ordinary Drawing action,
or select its saved native document as a Drawing source. A Drawing stores the
exact instance document identity and ordinary native source path. The Drawing
**Variant** selector lists the generic and its open instances. Save the target
instance before choosing it; selection reprojects all views of that source in one
Drawing transaction while preserving unrelated view sources.

Opening an unchanged row reuses its tab. After generic/table edits, opening the
row explicitly recalculates an unmodified generated tab. If that tab has its own
edits, generation refuses to overwrite them; save/close it first. A separately
reopened native instance is an independent model, not a live link that changes on
tab switching. The temporary native copy used for identity/path rebasing is
discarded after generation and is never required to reopen the instance.

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

Part INI version **24** (internal payload 48) and Assembly payload **32** carry
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

### Acceptance on 2026-09-15

Windows development GUI/CLI builds passed. All six relevant contracts passed:
native document format, engineering metadata commands, Family Table model
generation, shared UI behavior, translation catalogs and the Family GUI scenario.
The latter also saves the generated geometry, binds whole-Body presence through
Tree, switches a Drawing to its family instance and undoes that source change.
The dialog capture was visually inspected for width and readable references.

Run the GUI scenario through `zima_cpp_workspace_startup_contract` with
`ZIMA_VERIFY_FAMILY_ONLY=1`. Local logs: `build/family-tests.log` (native format),
`build/family-final-tests.log` (model/UI/translations), and
`build/family-gui-final-tests.log` (complete GUI workflow). The local capture is
`Projects/test/family-table-ui.png`. These generated test artifacts are not release
inputs. This development revision has not been packaged or published as a release.
