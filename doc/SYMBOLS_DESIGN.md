# Shared symbols

## Current agreement and feasibility review (2026-09-22)

The agreed next design supersedes the single-Sketch grouping prototype described
below: one `.symz` owns one common local Origin and several coplanar Sketches.
A family-style table selects Sketch visibility, individual text visibility and
text choices. Text options reuse the title-block list/custom-value contract.
Hiding a Sketch also hides its texts. Ordinary Sketcher layers are not required.

Each inserted occurrence has its own row, allowed text overrides, position and
optional leaders. Typed CAD values can select rows or fill fields; the projection
symbol uses its owning Drawing sheet's projection method. Existing Part/Assembly
Family Table text binding is not already implemented and must not be assumed.

The user confirmed constant on-screen glyph size in 3D, like Origins, and fixed
paper size in Drawings independent of model view scale. Attachment position stays
associative. Real target regions may still have their own model-space dimensions.

The complete category matrix, attachment/3D analysis, implementation gaps and
verification gates are in [Symbol feasibility review](SYMBOLS_FEASIBILITY.md).
The implementation now uses the multi-Sketch version-2 schema described below.

## Drawing coordinate and anchoring contract

Drawing frames and title blocks intentionally measure horizontal coordinates
from right to left. The user confirmed the reason on 2026-09-22: engineering
title blocks are anchored from the right, so replacing the frame or changing
the sheet format must preserve the title block's intended position.

This convention is not a rendering defect. A symbol definition can use ordinary
local XY coordinates, but insertion and rendering must convert those coordinates
to the title-block convention without changing document coordinates or the
right-hand anchor. Symbol geometry must retain its intended handedness and text
must remain readable. Frame-change checks must verify the anchor independently
of symbol-orientation checks.

## Implementation checkpoint

Native `.symz` version 2 is self-contained UTF-8 JSON (`zima.symbol`, units `mm`).
A definition owns coplanar XY Sketches with one shared local Origin, named variant
rows and named text fields. Rows select Sketches, hidden fields and text values.
Fields can restrict values to a list or allow custom input. Definitions reject
external references, nested symbols, duplicate Sketch IDs and invalid row/field
references. Library files use atomic writes.

Inserted symbols are separate Sketch annotations, never curves in the profile
or entities in the constraint solver. Each occurrence embeds its definition,
variant, explicit text overrides, visibility, position, rotation and uniform
scale. Selecting an occurrence highlights the complete symbol. The Sketcher
command **Insert symbol** and later **Properties** share one internal property
window with OK/Cancel and middle-button confirmation. The same window allows
manual variants or the definition's declared CAD source. Text fields are directly
editable without an override checkbox. Editing a field records an occurrence
override; untouched fields follow the selected row.

Title-block occurrences are embedded in `.drwz`. The layout evaluates
`drawing.projection_method` against the owning sheet on every layout request,
without model regeneration or a library lookup. Manual variants remain manual.
Saving a `.tblz` excludes symbol strokes from flattened template lines, avoiding
duplicate static geometry. Reopening an existing Drawing retains its embedded
title block; a factory library update does not rewrite user documents.

Every insertion is an independent embedded copy, including symbols brought in
through a title-block template. No source-file path or original-occurrence link
is needed to edit or reopen it. Double-clicking a symbol stroke or text in a
Drawing opens the shared Symbol Properties dialog. OK updates only that sheet's
occurrence and participates in Drawing Undo/Redo; Cancel leaves it unchanged.
The optional projection-method provider reads the owning sheet, not the source
library or template.

Definitions can store white/yellow curve pens by Sketch ID and curve ID. The
general-edge library uses yellow for its leaders, arrow strokes and reference
lines, matching the application's thin drawing pen. Edge geometry and text retain
white. Surface-texture strokes are yellow; general/title-block text is white and
local surface-texture text is green. These colors are application conventions,
not ISO color rules.
Leader/reference lines are continuous narrow lines (ISO 128-22:1999, sections
4–5; the current general line standard is ISO 128-2:2022). Full symbol proportions
remain subject to the verification limitation below. Updating library pens never
silently rewrites already embedded copies.
Sources: [ISO 128-22 terminology and rules](https://www.iso.org/obp/ui/?_escaped_fragment_=iso%3Astd%3A29094%3Aen)
and [ISO 128-2:2022 scope](https://www.iso.org/standard/83355.html).

The projection factory asset contains separate first-angle and third-angle
Sketches. The chosen library outer-circle diameter is 6 mm. Its intended factory
title-block occurrence uses scale 0.5 to fit the existing 5 mm row.

**Still pending:** New/Open `.symz` authoring, the graphical family-table editor,
model-owned surface references and leaders, constant-screen-size 3D annotation
presentation, direct Drawing insertion, general View visibility, dynamic text
borders. Initial surface-texture and undefined-edge library examples are available
for Sketcher/title-block insertion; model attachments are not implemented. A symbol shown
inside an ordinary Sketch currently follows that Sketch's zoom; this is not the
final constant-screen-size model annotation mode.

## Agreed text-field extension

A text field may have no border, one line, or a rectangle in the same Sketch.
The border follows text size and padding. A rectangular field exposes seven
attachment points: its insertion point, four corners and the two side midpoints.
These are persistent semantic attachments, distinct from transient editing
handles. Family rows may control the text and border visibility. Adjacent
geometric-tolerance fields require shared dividers and content-driven layout;
this extension is agreed but not implemented at this checkpoint.

## Inputs, means and outputs

Inputs are user-authored symbol geometry, labels, parameter values, attachment
references and the destination Part, Assembly or Drawing. Existing means are
the native Sketcher, persisted ZIMA reference identities and annotation painters.
The output should be one reusable definition with occurrence-specific placement
and values in any of the three document workspaces.

Define geometry once in a local 2D plane, using the native Sketcher for authoring
from the local origin `[0, 0]`, the default insertion point.
The definition should contain curves, text, parameters and named insertion/leader
attachment points. A Drawing instance places that plane on paper; a Part or
Assembly instance places it in a chosen spatial plane. This is annotation data,
not solid geometry, and must not require OCCT calculation to display or select.

The user requires one shared nominal size in millimetres for Drawing, Part and
Assembly placement, with no separate enlargement merely because the symbol is
in 3D. Camera zoom and output scaling must preserve that intent. Spatial plane orientation and optional
camera-facing presentation need an explicit interaction contract rather than
being inferred from the current camera. References in Assemblies must retain
exact occurrence paths and original-object ownership.

Embed each inserted definition and its instance parameters in its owning native
document. Library files are creation sources, never required sidecars for
reopening an existing model or Drawing. Updating a library definition must not
silently change existing inserted symbols. Define an explicit update operation
later if needed.

## First candidate: title-block projection symbol

The first requested candidate is the projection-method symbol in the title block.
Use one definition with first-angle and third-angle variants, selected from the
owning Drawing's projection setting. The title-block occurrence needs no leader.
The factory asset contains one coplanar Sketch per projection variant. Each
pair of circles shares a center on the horizontal axis. Finite auxiliary lines
are rendered as center lines; the Sketch Origin and its axes are never copied
into the symbol. `variant_source` is `drawing.projection_method`.

The arrangement was checked against figures 4 and 7 of
[ISO 5456-2:1996](https://www.iso.org/standard/11502.html): with a cone widening
to the right, first-angle places the circles on the right and third-angle on
the left. Both variants contain two concentric circles and two centre lines.

`zima_symbol_library_tool` reads the actual asset, validates Sketch and variant references,
checks native save/reopen, rejects invalid references and unsupported versions,
checks both arrangements and creates a preview. Use `--generate` only when
explicitly rebuilding the factory definition. The library API is independent
of Part/Assembly placement and makes no OCCT calls.

Separate the reusable definition (Sketcher geometry, text fields and parameters)
from each inserted occurrence (values, placement, attachment and optional leader
with or without an arrow). The native definition controls multiple Sketches and text fields. Insertion is intended
for Sketcher, Part, Assembly and Drawing. Occurrence edits must not modify other
occurrences or the library source.

Verification must exercise both projection settings, save/reopen and title-block
output, including a check that the displayed variant matches the owning Drawing.

Remaining implementation includes variable text expressions, attachment selection,
leader handling and offering model-owned symbols through Drawing Show/Erase.
The agreed size contract is fixed paper size in Drawings and constant screen size
in 3D. The latter still requires a model-annotation presentation path; ordinary
Sketch geometry is not a substitute. Projection-method title-block binding is
already implemented.

## Verification checkpoint (2026-09-22)

The local Windows executable builds successfully. Passing checks cover symbol
library serialization, embedded instances, variant/text validation, Part
Extrusion isolation, Undo/Redo, Drawing save/reopen, owning-sheet projection
binding, and unchanged right-anchored symbol coordinates when replacing A0/A4
frames. GUI checks cover insertion, editing, cancellation, middle-button
confirmation, Tree/View selection and actual painted symbol geometry. The
paint check caught and now guards against stored/selectable but invisible
Sketch symbols.

Existing template, Drawing and New-document GUI contracts pass, including the
regenerated start templates. Portable settings and five-language translation
catalog checks pass. This verifies the implemented Sketcher/title-block slice;
it does not verify the pending model annotation and authoring workflows.

The Drawing GUI contract also edits one of two embedded general-edge symbols,
checks Cancel, independent values and an unchanged library, then verifies
save/reopen, Undo/Redo and removing the owning title block. Symbol integration
checks verify shared stroke/text hit identity and yellow/thin leader pens. These
checks pass together with the Sketcher symbol, template and translation contracts.

## Initial annotation examples

`surface-texture/ZE-SURFACE-TEXTURE-ISO21920.symz` supplies three process variants
and a list/custom `Specification` field. The graphic includes the distinguishing
short bar shown in ISO 21920-1:2021. The official standard record is
[ISO 21920-1](https://www.iso.org/standard/72196.html).

`surface-texture/ZE-GENERAL-SURFACE-TEXTURE-ISO21920.symz` is a separate general
indication for the title-block area. ISO 21920-1:2021, 9.1.2 and figure 12,
place the default surface requirement before a bare graphical symbol in
parentheses. Three variants distinguish an unspecified manufacturing process,
required material removal and prohibited material removal. The alternative
figure 13 composition listing individual exceptions in parentheses is not yet
implemented. General requirements apply where no individual indication overrides
them; the local surface asset remains available independently.

`general/ZE-GENERAL-EDGES-ISO13715.symz` supplies all-edge, external, internal and
combined external/internal scopes with separate signed text values. Each scope
has no-exception, one-specified-exception and multiple-exception variants (12
rows total). Multiple exceptions use a bare symbol in parentheses. These follow
the general-indication and exception compositions in ISO 13715:2017, 4.5.4–4.5.5;
the adjacent standard reference follows 4.6. The reference is part of each
factory title block's single green localized heading (for example,
`Edges: ISO 13715`), not geometry or text inside the reusable edge symbol.
These are edge requirements, not
modeled chamfer dimensions; see
[ISO 13715](https://www.iso.org/standard/61328.html).

All five factory title blocks contain independent embedded copies of both
general definitions, preserving their existing fields and projection symbol.
The general texture occurrence uses paper X = 185 mm (3 mm left of its initial
placement); paper X increases leftward. This balances the margins in its cell.
All texture strokes, including parentheses, use the yellow/thin pen. General
title-block texture text is white; individual-surface texture text is green.
Symbol mesh and Drawing text rendering preserve the embedded text colour.
Edge leaders and reference lines also retain the yellow/thin pen.
Template text in PDF and the lineweight preview now follows its Drawing pen.
The renderer measures the font's native vertical stem at mid-cap height, then
expands or contracts glyph outlines by the difference from the requested paper
width. This avoids adding the full pen width on top of an already filled glyph.
Ordinary screen display retains native font rendering. Weight-adjusted PDF text
is vector outline geometry rather than selectable/searchable font text. The
nominal text height, alignment and anchor are retained; ink bounds grow with
the requested weight. Font-specific variable stroke contrast remains possible.
DXF explicitly preserves native text entities rather than converting them to
outlines. Regression checks measure 0.5 mm white and 0.25 mm green stems in the
print renderer and retain the existing real-text DXF check.
Ra 3.2 and -0.2/+0.2 are editable factory presets, not standard-mandated values.
Variant and field labels are localized in cs/en/de/fr/ru. Fields belonging to
inactive sketches or hidden exception values disappear from the shared dialog.
Symbol text fields are immediately editable, without an override checkbox.
Selecting a value or typing a custom one creates an instance-local override;
untouched fields continue to follow the selected variant's defaults. Existing
overrides persist through variant changes, while Cancel discards pending edits.
This shared interaction applies to local and general texture, edges and other
symbol definitions; fields with restricted choices remain non-editable combos.
Yellow leaders/reference strokes use the Drawing thin pen; yellow is a ZIMA
display convention, not a standard-mandated colour. Parentheses are native arcs
with the yellow/thin symbol stroke rather than oversized text glyphs.

These are initial editable library examples, not a claim of exhaustive standard
coverage or verified compliance with every prescribed proportion. Their model
attachment/leader workflow remains pending. `zima_symbol_catalog_tool` rebuilds
the examples and their preview. Updating library assets alone never modifies
previously inserted copies; the factory title blocks are explicitly refreshed
as part of this change.
