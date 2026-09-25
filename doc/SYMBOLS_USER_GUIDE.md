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
place a free symbol. In a Drawing, select original projected geometry or click
empty sheet space for a free placement. Reference hover, confirmation and cycling
use the existing reference-selection workflow.

Enable **Leader with arrow** to connect the contact point to the symbol grip.
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
perpendicular to an edge, or vertical in the current view for a point. In a
Drawing this applies to a projected straight edge. Without this option the
leader has a free direction. In both modes exactly one arrow-bearing segment
joins the horizontal shelf, with no intermediate connector. With perpendicular
mode enabled, the shelf follows the normal so the connection stays continuous. Surface-texture symbols without leaders
attach perpendicular to the selected surface; other symbol types retain their
surface-tangent placement plane. The scale control remains available.

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

Use **File → New → Symbol**, draw in the local XY Sketcher and save as `.symz`.
Local zero is the default grip. Symbols open as editable symbol documents rather
than Parts. A Sketch selector exposes the coplanar Sketches of existing library
definitions. Ordinary Sketch geometry and text remain editable.

Saving an inserted occurrence embeds its definition in the owning native
document. Reopening that document does not require the original library path.
Editing a library file does not silently update existing occurrences.

The graphical authoring workflow preserves existing named fields and variants.
A dedicated editor for creating the field/variant tables is not included yet;
the current catalog definitions are generated by `zima_symbol_catalog_tool`.

## Initial engineering library

Surface-texture definitions live under `config/symbols/surface-texture`.
General roughness is stored in `config/symbols/general`.
The material-removal variants and editable specification text are available in
the common properties window.

`config/symbols/geometric-tolerances` contains fourteen basic ISO 1101
characteristic glyphs. Tolerance frames grow with their text. Basic inputs are
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
