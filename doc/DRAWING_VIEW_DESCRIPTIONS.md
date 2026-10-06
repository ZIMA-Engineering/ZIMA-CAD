# Drawing view descriptions

View Properties places Name below Source. Enable **Show view description** to
display a stack below the view. The three optional rows are Name, Scale and Text.
Each row has its own visibility, paper-mm text height and color. Name follows the
view name; Scale follows the effective view scale using `1:2` or `2:1` notation.
Text uses the existing title-block parameter substitution, not a new expression
language. For example, `&document.file_stem.&REV` combines the source filename
stem, a literal dot and the source Part/Assembly's `REV` parameter. Drawing-local
tokens such as `&drawing.revision` use the owning sheet's local parameters.

The checkbox in the Order column selects one row for the shared-arrow Up/Down
buttons. Reordering retains that row's settings and ordering selection. Clicking
a text field edits its value without changing the ordering selection. Name and
Scale text fields are derived and read-only. Their displayed hints show the
current name and scale. Available interactive colors use the established white,
green, yellow and red annotation palette; Name defaults to white and additional
rows default to green. Printing and PDF/DXF/image export follow the existing
black output-ink convention. All paths retain the existing ISO technical font.

Visible nonempty rows stack in their saved order below the calculated view
bounds. An empty resolved Text row consumes no height. The description is one
movable annotation block with the existing purple manipulation point. Its offset
is relative to the view in paper millimetres; moving the view also moves its
description. Changing model scale does not scale text heights. Section labels
retain their separate placement and controls.
Detail views use the same description fields in their existing detail dialog;
new details keep the scale row visible. Editing description text preserves an
existing detail's exact scale and position even when its scale control rounds
the numerical presentation. Unchanged detail OK creates no Undo transaction.

Creation and editing use the existing in-application View Properties window,
transient preview and shared OK/Cancel behavior. Unchanged OK does not create an
Undo transaction. Description-only edits reuse calculated projection/reference
geometry without loading a source, recalculating a camera or refreshing children.
Parameter metadata is captured outside painting; previews cache it per source,
and confirmed display/export refresh reads current source parameters. Definitions
are embedded in `.drwz`, including order, visibility, text, height and color.

The CLI exposes the same definitions through `drawing.view.create`,
`drawing.view.set` and `drawing.view.get` as `description_rows`. Supply exactly
one row of each kind, ordered as required:

```json
[
  {"kind":"name", "visible":true, "height_mm":5, "color":"#ffffff"},
  {"kind":"scale", "visible":true, "height_mm":3.5, "color":"#00ff00"},
  {"kind":"text", "visible":true, "text":"&document.file_stem.&REV", "height_mm":2.5, "color":"#00ff00"}
]
```

The `show_caption` flag enables the complete description block. Validation uses
the existing localized invalid-view message and rejects duplicate/missing kinds,
invalid heights, control characters and invalid RGB color strings atomically.

## Verification

Eleven affected native/GUI suites passed on Windows with Qt 6.11 and OCCT 8.0.0
(41.17 seconds for the combined run). Final detail/DXF follow-up checks passed
after integrating the shared detail controls, including exact scale preservation.
The main application Drawing workspace verification also passed with actual
Tree synchronization, description editing and native reopening.

Focused native tests cover source-free annotation
edits, unchanged geometry, rejected definitions and native round trips. DXF tests
check resolved parameters, hidden rows, order, paper heights, live source changes
and view-relative positioning. The Drawing GUI contract exercises creation,
reordering, unchanged OK, five-language labels, Cancel and resizing, together with
the existing annotation manipulation and dependent-view workflow.

The annotation-only edit gate records zero source loads and zero interactive or
vector camera calculations while retaining the original projection and reference
geometry. DXF checks distinguish two independent source Parts, source parameter
changes, paper heights and view movement. Five-language property screenshots were
inspected. An outdated inch-tolerance test expectation was corrected to the
existing decimal-point/no-leading-zero Drawing convention; product dimension
formatting was not changed.

The verification baseline is `16db7e34`, with the requested change applied.
This evidence establishes Windows acceptance of this new feature. The user's
Linux verification of preceding changes remains separate from Linux acceptance
of these new description controls.
