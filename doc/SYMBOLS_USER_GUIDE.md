# Symbol annotations

The `config/symbols/annotations/ZE-TEXT.symz` library symbol provides a plain
editable text note for a surface, a leader or free placement. Its **Text** field
accepts custom content. The grip is local XY zero at the start of the text;
the default nominal text height is 2.5 mm. It uses the same placement, scale,
rotation and reference-loss behavior as other symbols.

## Insert and edit

Use **Insert symbol** in a Part, Assembly or Drawing and select a `.symz` library
file. In Part (including Sheet Metal) and Assembly workspaces, this command is
the last item in the command toolbar, below a green separator. Insertion and
the Symbols visibility filter share the document icon with a yellow roughness
mark in Part, Assembly and Drawing workspaces.
The properties window controls the variant, editable text values, offset,
angle and scale. Creation and later editing use the same window. **OK** commits
one change; **Cancel** discards the preview. Middle-button double-click confirms.

In a model, arm the reference field and select an original planar, cylindrical
or conical face, an original edge or a point. The contact follows that reference. The numeric origin can also
place a free symbol. In a Drawing, select projected geometry before insertion:
the preview is hidden and OK is disabled until an entity is selected. Clicking
empty sheet space keeps reference entry active. Reference hover, confirmation and cycling
use the existing reference-selection workflow.

Direct Drawing surface-texture marks follow the local tangent of their selected
edge. Their leader mode defaults to perpendicular contact, using the same
projected tangent as datum-feature leaders. Manual rotation remains an offset.
The ISO 21920 factory library places the text 1 mm below its upper line and
1 mm farther right than the previous layout. That line fits `Ra 12,5` with
1 mm of right clearance. These dimensions are application styling choices.
Geometric-tolerance library geometry and frames are yellow; lettering is green.
Existing inserted symbols retain their embedded library geometry and text colors;
insert from the updated library to use the revised factory layout.

Enable **Leader line** to connect the contact point to the symbol grip. **Leader
ending** selects an arrow, filled triangle or filled dot. The datum-feature
library defaults to a yellow triangle with its base on the referenced entity
and its apex toward the leader. A triangle always enforces perpendicularity;
the corresponding checkbox is checked and disabled. The choice persists with
each insertion. On monochrome output, the fill follows the drawing ink.
In **Under symbol** mode, the grip is the center of a horizontal shelf whose
width follows the actual symbol or text, including its scale. The glyph is above
the shelf. **To attachment point** uses a short shelf ending at the authored
symbol origin. In 3D, the
glyph and shelf face the camera; in Drawings, they remain horizontal on the
sheet. Moving across the contact switches the leader between shelf ends without
mirroring the glyph. X and Y move the grip in its stored annotation frame.
The angle applies to symbols placed directly, without a leader. Moving a grip does not replace its contact reference. The eye control
inspects the stored reference. The reference field arms replacement; its cross
detaches the symbol while preserving the last frame.

Select a symbol in the View or Tree to open Properties or remove it. The model
**Symbols** display filter is independent of Sketches and Dimensions; hidden
symbols are not offered by the picker.

A selected leader exposes three purple handles: the arrow contact, the elbow
and the shelf end. In a model, dragging the grip opens Properties and changes only the
pending offset; OK commits and Cancel restores the original placement. Clicking
the contact arms reference replacement. Direct Drawing symbols expose the same
three handles. Dragging the arrow contact follows its original edge, including
while Properties is open; the reference field remains the way to replace it.
Curved edges use their local tangent for the perpendicular leader. A point-only
reference has no unique tangent and therefore cannot define this perpendicularity.
Dragging the shelf moves the annotation independently. A symbol without a leader
stays directly on its referenced entity. Escape restores the drag state. Inherited Drawing annotations
retain their model contact and their separate paper presentation controls.

Leaders and arrowheads are yellow. Enabling a leader at zero offset supplies an
initial 15 mm / 5 mm grip offset so the arrow is visible. **Leader perpendicular
to entity** optionally constrains the first leader segment: normal to a face,
or vertical on screen for a model edge or point. In a Drawing the leader remains
perpendicular to its projected edge. Constrained model leaders retain a fixed
length in millimetres, derived from the stored contact-to-grip distance, with a
minimum of twice the arrow length. Orbiting does not change that length; zooming
magnifies it together with the model. Dragging the elbow adjusts the stored
distance, while resizing a short shelf leaves that distance unchanged. Without this option the
leader has a free direction. In both modes exactly one arrow-bearing segment
joins the horizontal shelf, with no intermediate connector. With perpendicular
mode enabled, the shelf follows the normal so the connection stays continuous.
Symbols without leaders attach perpendicular to the selected surface, including
custom library symbols. The stored surface side determines the local upward
direction at zero offset as well. The scale control remains available.

## Model annotations in Drawings

Use **Show/Erase → Symbols** to expose model annotations in a Drawing view.
Source values remain linked. Each view retains its own presentation position;
changing view scale does not scale the nominal paper size of a symbol. Repeated
Assembly occurrences remain separate annotation sources.

A direct Drawing symbol belongs to its sheet. Its geometry reference also
identifies the projected view. The glyph and leader use paper dimensions and
are included in vector PDF/DXF export. Moving the view rectangle translates the
whole annotation, including its shelf, arrow and any extension, by the same
distance. The mouse-drag path only translates cached presentation; it does not
reproject or calculate body geometry. View-scale changes resolve the contact
again while preserving nominal paper glyph size.

Dragging a straight-edge contact beyond an endpoint extends the supporting
line in yellow from that endpoint, 2 mm beyond the contact. This also applies
to symbols without leaders. Moving back onto the edge removes the extension.
The contact remains on the original edge direction rather than detaching into
free sheet space. Free symbols without a view reference stay on the sheet.

Deleting a referenced object or view does not delete its symbol. An unresolved
annotation retains its last frame and values, with an unresolved display state.
Reopen Properties to inspect or replace the reference.

## Create a library symbol

Use **File → New → Symbol** and save as `.symz`. The document is a native Part
with an active Body. Open a Sketch through its ordinary Properties window and
use **Sketch** to enter editing. Local XY zero is the default insertion grip.
Sketches and modeling history belong inside Bodies. Deactivating the Body returns
to the ordinary Body-list commands. The Tree uses the Symbol document icon.
Library insertion currently consumes coplanar XY Sketch geometry, not arbitrary
Part solids. The complete modeling document remains stored in the library file.

Saving an inserted occurrence embeds its definition in the owning native
document. Reopening that document does not require the original library path.
Editing a library file does not silently update existing occurrences.

Visible preceding geometry remains editing context under the ordinary Part
rules. Finish Sketch returns to the same properties workflow as other Parts.

**Family Table** edits the symbol's named variants. Each row is offered in the
inserted symbol's **Variant** dropdown. Checked Sketch and text columns mean
visible; unchecked columns mean hidden. A hidden Sketch also hides its texts.
Rows can be added, renamed and removed, and a default variant selected. Create
Sketches through the ordinary Sketch command in an active Body. OK commits
the table; Cancel discards the pending changes.

In Text Properties, **Value action → Select from list** defines the values
offered for that text in an inserted symbol. **Allow custom value** controls
whether other values are allowed. Restricted lists must retain values already
used by variants and contain the default text. Text and list edits commit as
one Undo step. Family Table changes also support Undo/Redo and SYMZ save/reopen.
The catalog generator supplies factory defaults; using it is not required to
edit a symbol's variants or text choices.

## Initial engineering library

Surface-texture definitions live under `config/symbols/surface-texture`.
General roughness is stored in `config/symbols/general`.
The material-removal variants and editable specification text are available in
the common properties window.

Symbol Properties opens at the natural size of its complete form. Attachment
controls remain first, followed by the symbol variant, text choices and numeric
placement values. Scrolling is needed only when the main window cannot accommodate
the complete form or the user manually reduces the dialog.

The historical and current surface-texture definitions, including general
roughness, use named Sketch components and a shared Specification field. Their
variant rows select geometry while the default roughness comes from the authored
text. Existing inserted occurrences retain their own embedded definition;
editing the library does not update them automatically.

`config/symbols/geometric-tolerances/ZE-GEOMETRIC-TOLERANCES-ISO1101.symz`
contains fourteen basic ISO 1101 characteristic variants in one native Family.
`ZE-DATUM-FEATURE.symz` separately provides an editable datum letter in a frame,
with a triangular leader ending. Tolerance frames grow with their text. Basic inputs are
a positive numerical tolerance and applicable ordered datum identifiers. Empty
trailing datum cells are omitted. Profile controls may omit all datums; form
controls have no datum cells. Other supplied controls require a primary datum.

This initial library does not provide advanced modifiers, composite frames,
semantic datum-feature binding or a complete ISO conformance checker. It must
not be treated as verification of a manufacturing specification. The research
basis and implementation boundaries are recorded in
[Symbol placement implementation](SYMBOL_PLACEMENT_IMPLEMENTATION.md).


## Historical surface texture and readable symbol text

`surface-texture/ZE-SURFACE-TEXTURE-ISO1302-1978.symz` provides the historical
basic symbol and the material-removal-required variant. Both consist of ordinary
editable Sketch segments and text, with the grip at the lower tip. They do not
use a private glyph renderer. The editable specification sits above the joining bar (or its location in the
basic variant), to the left of the long arm. Its default is `3,2`, meaning Ra
in micrometres under the historical convention. The editable choices also offer
explicit `Ra` prefixes. Right alignment lets longer values grow away from the
long arm; readability rotation still uses the center of the text contours.

When authoring a Symbol document, Text Properties exposes **Drawing orientation**:
**With symbol** (default) or **Keep readable**. The choice belongs to each text,
not to a particular library symbol or symbol name, and persists with that text.
It is enabled for the supplied historical roughness; existing symbols retain
their behavior. The option does not change ordinary Sketch text or 3D display.

The Drawing renderer computes the combined paper angle of the symbol frame,
symbol occurrence and authored text. Relative to conventional paper XY, angles
in (-90, 90] degrees remain unchanged; the other half-plane receives a 180-degree
rotation of the text contours about their center. The symbol, text center, slope,
contact and leader remain unchanged. Direct Drawing symbols and model symbols
shown in a Drawing use the same policy, including vector printing/export.
This is an orientation correction, not automatic text relocation or collision
avoidance; the authored text position can be edited in the Symbol Sketch.

The basis for this historical symbol is ISO 1302:1978, clauses 4.1, 4.5 and 5.1, Figures 6 and 14-16:
readability from the bottom or right, with a separate orientation allowance for
simple symbols without special texture indications. This is not a general
permission to independently rotate the contents of every current ISO symbol.
Source: https://cdn.standards.iteh.ai/samples/5882/b9e1d1ff1f7b4876b693a368aba4ca1a/ISO-1302-1978.pdf


## Annotation command icons

Symbol insertion and visibility use the same document/roughness icon, with a
yellow roughness stroke. Drawing dimension commands use the yellow dimension
icon also shown in the Tree; chain dimensions have a distinct adjacent-span
icon. Show/Erase retains its eye with a yellow dimension, and the balloon icon
uses a yellow circle while retaining its white number. These changes affect
command graphics only, not annotation geometry or export pens. Existing
localized action labels are reused without new text.

## Verification: horizontal shelves and view attachment (2026-09-25)

The Windows development build passes symbol placement, Drawing attachment,
native document, library integration and dimension-layout contracts. GUI
verification covers the actual view-rectangle drag and checks that every symbol
stroke receives the same translation, plus symbol editing, cancellation,
reference picking, PDF/DXF export and all five UI languages. Localization
coverage/catalog validation, template editing and new-document creation from
regenerated factory templates also pass. The 3D and Drawing screenshots were
reviewed for shelf direction and readable symbol geometry.

## Leader shelf and three grips

The horizontal **leader shelf** has two modes:

- **Under symbol** spans the visible symbol width. Its length follows the
  geometry and text; moving either shelf grip translates the symbol.
- **To attachment point** ends at the symbol definition's insertion origin.
  Its configurable length defaults to 3 mm (an application default, not a
  claimed normative dimension). Author the symbol around that insertion point
  with ordinary Sketcher geometry. Side changes never mirror its text.

Three purple grips represent the arrow tip, elbow and shelf end. Moving the
elbow translates the shelf and symbol while retaining the contact. Moving the
short shelf end changes its length and moves the symbol, retaining the elbow.
The shelf stays horizontal, and optional perpendicularity remains enforced.
Moving the arrow tip uses reference entry in 3D; dragging it offers geometry
through the common picker and releasing accepts the offered reference. In a
Drawing it moves on the attached entity or its extension. A reference is never
silently discarded by dragging into empty space.

The leader remains exactly two segments: arrow-to-elbow and horizontal shelf.
No intermediate diagonal or 45-degree rule is used. The two shelf options and
length are stored with the native annotation and restored when editing.

Verification of the three-grip revision (2026-09-25): native placement and
Drawing tests pass, including insertion-origin alignment, both shelf sides,
length changes, translation and serialization. The symbol GUI contract passes
actual elbow/end dragging in the model, short-shelf and arrow-contact dragging
in Drawing Properties, perpendicularity on a straight edge, reference retention,
Cancel restoration and all five UI languages. The native Drawing test also
covers local tangents and contact movement on an arc.
The Windows development executable was rebuilt and the generated model/sheet
screenshots were inspected. The separate cross-command first-plane audit has
an unresolved Sweep2D finding documented in `PROFILE_CENTERLINES.md`.

## Geometrical tolerances and welds (2026092602)

In a Drawing, use **Insert symbol** and choose a native file from the configured
Symbols library. `geometric-tolerances/` contains fourteen ISO 1101 indications:
straightness, flatness, circularity, cylindricity, line/surface profile,
parallelism, perpendicularity, angularity, position, coaxiality, symmetry and
circular/total run-out. Enter the positive tolerance and, where applicable,
ordered primary/secondary/tertiary datums. Cells grow with their text; empty
optional datum cells are omitted. New insertions default to **Position** so the
datum fields are immediately available. **Tolerance**, **Primary datum**,
**Secondary datum** and **Tertiary datum** appear in that order before numeric
placement controls. Enter A, B and C (or other datum identifiers) in these
fields; the secondary and tertiary entries start empty. Switching to a form
tolerance hides these inapplicable fields and displays an explanation. Switching
back restores entered values. Filled cells, including the separate datum letter,
are centered horizontally and vertically using their actual contour bounds.

`welding/ZE-WELDING-ISO2553.symz` contains fillet, square-butt, V-butt and
bevel-butt variants using ISO 2553 system A. Choose the type and **Arrow side**,
**Other side** or **Both sides**, then edit the weld
size and length/count/pitch indication. The solid and dashed reference lines
retain their meaning when the leader is moved. Size examples (`a3`, `z4`, `s5`)
and length examples are editable presets, not calculated weld requirements.
The opposite-side row lies on the dashed reference line. These definitions cover
single- and double-sided elementary indications; mixed-type combined welds, site flags,
finish/contour symbols and process tails are not provided by this catalog.

**All-around weld** adds an unfilled circle at the junction of the leader and
solid reference line. It belongs to the placement and follows the junction when
the approach side changes. Its diameter follows the leader arrow length
(2.5 mm by default), and adjoining strokes stop at its perimeter. Turning the
leader off hides the ring without discarding the option. The factory dashed
line is now 1 mm below the solid line at scale 1, with other-side glyphs moved
with it. This is an application drafting-style choice, not a verified mandatory
ISO distance. Existing embedded definitions retain their authored gap.

New tolerance/weld insertions enable the leader automatically. Move the symbol
left or right of its contact to change the approached end. The leader meets a
tolerance frame through a horizontal landing at the middle of its nearest side
and a weld at the nearest end
of its reference line. Text, glyphs, datum order and weld-side meaning never
mirror. The reference line grows with the visible fields. Use the same Properties
dialog for creation and editing; OK commits once and Cancel discards the preview.
Long forms scroll while confirmation buttons remain outside the scroll area.
The same three purple manipulation handles are available for welds, framed
tolerances, datum indicators and text: contact, elbow and landing end. For a
framed symbol, **Shelf length** controls the horizontal landing (initially
3 mm). Dragging its frame-side handle changes this length while keeping the
elbow fixed; dragging the elbow translates the frame and landing together.
Instances embed their definition and values in the Drawing; changing the library
does not rewrite previously inserted copies.

Standards references: [ISO 1101:2017](https://www.iso.org/standard/66777.html) and
[ISO 2553:2019](https://www.iso.org/standard/72740.html). The catalog implements
these stated indications, not an exhaustive standards-compliance checker.
Manufacturing suitability and tolerance/weld sizing remain the author's decision.

The system-A side convention was checked against [TWI's weld-symbol guide](https://www.twi-global.com/technical-knowledge/job-knowledge/weld-symbols).
The separate datum-feature indicator follows the rectangle/letter and triangle
description in [KEYENCE's datum guide](https://www.keyence.com/ss/products/measure-sys/gd-and-t/basic/datum.jsp).
Attaching an indicator to a surface does not create a semantic datum axis or
solve a tolerance specification.

Additional checks used [KEYENCE's feature-control-frame guide](https://www.keyence.com/ss/products/measure-sys/gd-and-t/basic/tolerance-entry-frame.jsp)
for ordered datum compartments and its [form-tolerance guide](https://www.keyence.com/ss/products/measure-sys/gd-and-t/type/form-tolerance.jsp)
for the absence of datum references in form controls.
[AutoCAD Mechanical's datum command](https://help.autodesk.com/cloudhelp/2025/ENU/AutoCAD-Mechanical/files/GUID-A0347CAE-3654-4D01-8C1B-4643B3E6AF64.htm)
confirms the perpendicular first-segment convention. The triangle reversal and
yellow display fill implement the requested ZIMA presentation.
[SOLIDWORKS weld properties](https://help.solidworks.com/2024/English/SolidWorks/sldworks/HIDD_WELD.htm)
place the all-around circle at the leader bend;
[Inventor's weld style](https://help.autodesk.com/cloudhelp/2023/ENU/Inventor-Help/files/GUID-5CCDE668-DFA2-4C33-8154-F88E24003DD5.htm)
treats identification-line spacing as a configurable offset related to text
height. The public sources reviewed do not establish a universal spacing in mm.
The current datum-system standard is [ISO 5459:2024](https://www.iso.org/standard/87855.html).
These checks cover the stated presentation rules, not every modifier or
application rule in the full standards.
