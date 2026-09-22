# Symbol feasibility review

Research date: 2026-09-22. This is an architectural assessment, not an
implementation or a declaration of full standards compliance. Product manuals
describe vendor behavior; ISO public summaries and previews establish scope and
selected rules. Exact library geometry and all combinations need separate
validation against the applicable editions before release.

## Conclusion

The agreed design is viable: one `.symz`, one local Origin, several coplanar
Sketches, text fields with choices, and a family-style variant table. No general
Sketcher layer system is needed. This covers appearance and finite variants well.
It does not by itself implement attachment, 3D annotation presentation, dynamic
layout or engineering meaning. Those require shared annotation services and a
few category-specific rules.

The first six user examples and every additional proposed category are assessed
below. Shape tolerances are part of geometrical tolerancing rather than a separate
technical standard family; see [ISO 1101](https://www.iso.org/standard/66777.html).

## Inputs, means, outputs and independent checks

Inputs: authored Sketches, stable text IDs and options, selected variant, CAD
properties, attachment references, presentation context and nominal size.
Means: native Sketcher, existing text-choice UI, table editing patterns, persisted
ZIMA references, shared painters and Drawing annotation projection.
Outputs: an editable symbol occurrence in a Sketch, title block, Drawing view,
Part or Assembly, with consistent screen and export geometry.

Three independent stress directions expose the limits:

1. Change text length without changing geometry references: a fixed-width box
   fails; a measured text layout succeeds without modifying the Sketch solver.
2. Change a Drawing view from 1:1 to 1:5: a nominal 5 mm label must remain 5 mm
   on paper, while a real 10 mm target area becomes 2 mm on paper.
3. Repeat the same Part twice in an Assembly and move one occurrence: references
   using only the source document or entity ID fail. Exact occurrence paths and
   view-specific presentation distinguish the two instances.

These are analytical checks, not executed GUI acceptance tests.

## Current code versus intended design

- `cpp/modules/symbols/include/zima/symbols/definition.hpp` now contains
  multiple coplanar Sketches, named variant rows, text choices/overrides and a
  declarative `variant_source` string. Native version 2 supersedes the initial
  single-Sketch grouping prototype.
- Sketcher insertion/editing and Drawing-owned projection-method evaluation
  are implemented. A symbol-definition document/editor remains pending.
- `FamilyColumn` in `cpp/modules/document_core/include/zima/document/engineering_metadata.hpp`
  supports dimension, feature, body and component bindings. `family_operations.cpp`
  applies numeric overrides and presence. It is not already a text-variant engine.
- Title-block text options already exist in `sketch_text_properties_dialog.cpp`
  and `TextAction.<text-id>` metadata. Reuse the choices/custom-value contract;
  extract common UI/data handling rather than coupling symbols to title-block code.
- Existing Drawing annotations and viewer references retain occurrence paths.
  They are useful infrastructure, not a completed symbol placement service.
- Native `.symz` GUI authoring, model-surface attachment, constant-screen-size
  model presentation and semantic PMI export remain pending. See
  [the implementation checkpoint](SYMBOLS_DESIGN.md#implementation-checkpoint)
  for the current scope; this feasibility review is not an acceptance report.

## Category assessment

“Basic” means straightforward after the shared symbol runtime exists, not already
implemented. “Extended” requires attachment/layout rules. “Specialized” means
that a graphical symbol is easy but a useful associative engineering feature
needs additional domain data and behavior. 3D recommendations below are ZIMA
design proposals unless explicitly attributed to a vendor.

| Category | Drawing appearance and attachment | Part / Assembly presentation | Fit and missing behavior |
|---|---|---|---|
| Projection method | Two cone/circle arrangements; normally free in the title block, no leader. Bind the variant to the owning sheet's projection method. | No inherent 3D surface attachment. A free annotation can be displayed, but a Part must not acquire a unique projection method merely because one Drawing uses it. | **Basic.** Two variants and a contextual enum provider. The two arrangements are covered by [ISO 5456-2](https://www.iso.org/standard/11502.html). |
| General surface texture | A general indication near/in the title block or notes, with values and possible exceptions. Scope is the document or explicitly named set of surfaces. | Document-level manufacturing annotation on an annotation plane; not an arbitrary single-face reference. | **Basic/extended.** Options and multiple text fields work; general scope and exceptions need data. Use [ISO 21920-1](https://www.iso.org/standard/72196.html), which replaces ISO 1302:2002, as the current profile-texture starting point. |
| Local surface texture | Symbol directly on a relevant contour/extension or with a leader. Values and supplementary fields must keep their intended positions. | Associate with the actual model surface, retaining an independent annotation plane and optional leader. | **Extended.** Surface owner versus displayed edge must remain distinct. PTC's [surface-finish API](https://support.ptc.com/help/creo_toolkit/otk_java_pma/r12/usascii/creo_toolkit/api/dita/c-wfcModel-WModel.html) separates surface attachment, leaders, annotation plane and variable text. |
| Undefined-edge indication | Edge glyph with permitted deviation values, local leader to the edge or a general indication. | Reference the persistent model edge and its occurrence; draw the callout on an annotation plane. | **Extended.** Preserve sign, edge meaning and internal/external context. It is not a substitute for a defined chamfer such as 1 x 45 degrees: [ISO 13715](https://www.iso.org/standard/61328.html) explicitly distinguishes these. |
| Welding / brazing / soldering | Arrow, reference line, elementary/supplementary glyphs, optional tail and values. Arrow-side/other-side meaning must survive movement. | Associate with a joint or selected references; put the readable annotation on a plane. A generic glyph does not create a physical weld feature. | **Extended/specialized.** Need left/right attachment ports, side semantics, optional decorations and text layout. [ISO 2553](https://www.iso.org/obp/ui?_escaped_fragment_=iso:std:iso:2553:ed-5:v2:en) distinguishes dual-reference-line system A from single-line system B; do not mix their rules. |
| Geometrical tolerances: orientation, location, run-out | A compartmented frame with characteristic, tolerance value/modifiers and ordered datum references, attached to a relevant feature or annotation. | The frame belongs to an annotation plane and refers to model features and datum identities, not only printed letters. | **Specialized.** Sketches supply glyphs, but variable cell count/width, stacked frames and validation require structured layout. [ISO 1101](https://www.iso.org/standard/66777.html) defines the geometrical specification language. |
| Geometrical tolerances: form/profile | Same frame family; fields and reference requirements depend on the selected characteristic and specification. | Same semantic feature and annotation-plane mechanism. | **Specialized.** Reuse the preceding frame service. Do not impose one fixed datum-column pattern on every characteristic. |
| Datum feature A, B, C | Letter box and triangular terminator attached to the indicated feature, or associated with a dimension/GTOL presentation. | Surface/feature reference plus datum identity; annotation-plane placement. | **Extended/specialized.** The box is simple; a working datum system needs semantic IDs. PTC permits [attachment to surfaces, dimensions and GTOL frames](https://support.ptc.com/help/creo/creo_pma/r13/usascii/model-based_definition/datum_feature_symbol_placement.html), with movement following the parent annotation. [ISO 5459:2024](https://www.iso.org/standard/87855.html) covers datum systems. |
| Datum target | Divided callout with target identifier and optional size, linked to a point, line or area indication. | Place the target geometry on the model and the readable callout on its annotation plane. | **Specialized.** Target position/extent uses model units; callout size uses annotation units. The [SOLIDWORKS target API](https://help.solidworks.com/2026/english/api/sldworksapi/SolidWorks.Interop.sldworks~SolidWorks.Interop.sldworks.IModelDocExtension~InsertDatumTargetSymbol3.html) exposes target shape and numeric area size separately. |
| Revision mark | A revision identifier in a configured outline, placed near a change; optionally linked to a revision record/cloud. | Optional model-level change annotation; a 2D Drawing revision does not automatically become a Part revision. | **Basic** when manual; **extended** with revision-table association. [SOLIDWORKS revision symbols](https://help.solidworks.com/2025/English/SolidWorks/sldworks/c_revision_symbols_intro.htm) associate changes with a revision table. |
| Technical-note reference | Number/identifier in an outline, optionally with one or several leaders. | Free annotation or association with one or several model features. | **Basic** for manual values; **extended** for automatic note numbering. Proposed reusable use of the common annotation engine, not a newly asserted universal symbol standard. |
| Dowel-pin hole mark | Mark centered on a selected circular hole; geometry can follow hole size and have an orientation. | Possible as an explicitly enabled annotation on a suitable plane; not physical pin geometry. | **Extended.** Needs geometry-driven size rather than only paper size. [SOLIDWORKS](https://help.solidworks.com/2025/english/solidworks/sldworks/c_dowel_pin_symbols.htm) scales the mark to the hole and limits Drawing placement to a normal-facing hole plane. |
| Centre of mass | Marker at the projection of a computed point in a particular view. | Marker at the computed point for the selected model/occurrence scope. | **Specialized.** Display is simple; the provider must track the correct mass result and invalidation. [SOLIDWORKS documents](https://help.solidworks.com/2024/english/SolidWorks/sldworks/c_reference_center_mass_drawings.htm?format=P&value=) showing the model item in Drawings; its [Assembly demonstration](https://www.youtube.com/watch?v=sf9SmFmOD3w) describes updates when components change. |
| Motion / rotation direction | Straight or curved arrow, optional label; independent drawing annotation or aligned with a reference. | Associate the direction with a persisted axis/vector/plane and occurrence. | **Basic** when manually oriented; **extended** when driven. Arrow appearance does not establish a mechanism or verify its motion. This is a proposed library category. |
| Grain / rolling direction | Direction mark and optional text on the appropriate view. | A material/manufacturing direction in the Part frame, transformed through the exact Assembly occurrence. | **Basic/extended.** Must preserve direction under view rotation and distinguish a manual label from a typed material property. Proposed category; no universal appearance was verified here. |
| Inspection / special characteristic | Organization-specific outline, identifier and optional leader to the inspected requirement. | Link to the model feature, dimension or tolerance being inspected. | **Basic** as a label; **specialized** if integrated with inspection plans or automatic numbering. Use organization-defined libraries rather than assuming one universal symbol. |
| Assembly process marks | Labels for adhesive, lubrication, matching/alignment or marking locations. | Reference components, surfaces or a set of locations in the Assembly. | **Basic** as annotations; **extended/specialized** for process regions and records. Explicitly proposed company/process-specific symbols. |
| Hydraulic / pneumatic / electrical schematic symbols | Graphic components, identifiers and connection ports on a schematic sheet. | Optional identification of a physical component; the schematic itself remains a diagram, not a physical 3D shape. | **Basic** for graphics; **specialized** for connectivity, nets and circuit validation. [ISO 1219-1](https://www.iso.org/standard/60184.html) covers fluid-power graphical symbols; this does not establish an electrical-symbol standard or implement a schematic CAD system. |

## What must be added to the common design

### 1. Definition, selected variant and occurrence

The library definition owns the common Origin, coplanar Sketches, stable text
fields, choice lists, named attachment ports and variant rows. The occurrence
owns its selected row, permitted text overrides, placement and attachments.
Embed the definition and values in the native owning file; no required library
path remains after insertion. Library edits must not silently alter documents.

Keep family-table interaction patterns, but do not route symbol row evaluation
through Part/Assembly body suppression or OCCT regeneration. A symbol evaluator
selects visible Sketches/texts and values only. Hidden entities stay stored.
Effective text visibility is Sketch visibility AND text visibility. Shared
texts retain one stable identity even when used by multiple rows.

Choice keys must be stable and separate from translated labels. Precedence:
library default, row-selected choice/value, then explicitly permitted occurrence
override. A CAD-driven field is read-only until the user explicitly changes its
source to manual. Missing CAD values or deleted choices are reported, not silently
replaced with another valid-looking specification.

### 2. Attachment is more than an arrow switch

Represent zero or more leaders, each with a target reference, attachment point,
bends and terminator type. Initial terminators: none, arrow, dot and datum
triangle. Define allowed types per category; not every endpoint is meaningful
everywhere. Named symbol ports let a leader stay attached when the variant changes.

Reference classes must distinguish free sheet coordinates, Drawing view geometry,
persisted model point/edge/face/axis, an annotation subpart, and a computed provider.
An edge picked in a Drawing is not automatically the semantic face for a surface
specification. Resolve or ask for the intended feature. Preserve oriented-side
identity, original owner and full occurrence path; do not use OCCT traversal IDs.

An annotation reference identifies a dimension witness line, leader elbow or frame
edge by stable role, not by a transient painted segment number. Prevent cycles.
One symbol can cover multiple references, and an anchor can be associated without
showing a leader. These are distinct choices. Creo provides free, attached,
offset and multiple-leader workflows in its [placement tool](https://support.ptc.com/help/creo/creo_pma/r12/usascii/detail/to_place_a_symbol.html).
Its [existing-leader attachment](https://support.ptc.com/help/creo/creo_pma/r12/usascii/detail/leaders_attach_to.html)
also demonstrates why an annotation-to-annotation relation is needed.

### 3. Model meaning versus view presentation

Store engineering ownership/references separately from label position. A model
annotation can have independent Drawing-view positions without changing the Part.
Show/Erase controls each presentation, not the underlying specification. Two views
or two occurrences must not share accidental placement state.

In 3D, define an annotation plane, rotation and viewing side. Screen-facing
presentation should be an explicit capability, not the universal default for
all tolerances/datums. PTC separates plane, named-view and screen-facing orientation
and restricts the last mode for some annotation classes. See [annotation orientation](https://support.ptc.com/help/creo_toolkit/protoolkit_plus/usascii/creo_toolkit/user_guide/Annotation_Orientation.html)
and [3D symbol planes](https://support.ptc.com/help/creo_toolkit/otk_java_plus/usascii/creo_toolkit/user_guide/Creating_Reading_and_Modifying_3D_Symbols.html).

Drawing presentations project semantic anchors through the selected view, then
lay out readable glyphs and text in the sheet plane. Do not blindly project and
foreshorten an entire 3D label. If the reference is hidden, cropped, removed or
incompatible with that view, apply an explicit display/broken-reference policy.
Hidden geometry remains selectable only under the active command's contract.

In a normal Sketch, inserting an annotation must not silently add profile curves
to an Extrusion or change constraints. Symbol authoring uses Sketcher, but an
inserted annotation is not automatically modeling geometry. Converting it into
editable profile curves would be a separate future operation.

### 4. Size and readable layout

The user clarified the size contract during this review: in 3D, keep symbol
glyphs at a constant screen size through zoom, following the existing Origin
presentation behavior. In a Drawing, keep a fixed nominal paper size independent
of the model view scale. Ordinary zoom of the Drawing sheet still magnifies the
whole paper presentation; it does not change the printed millimetre size.
Model anchors move/project with the geometry while glyph sizing is resolved
separately. Preserve the common nominal size setting with a renderer conversion
for screen/DPI and paper output, not a model-space enlargement factor. This size
decision does not automatically decide whether a symbol is screen-facing or lies
on an oriented annotation plane; those are separate presentation properties.

Support a separate model-size mode only for geometry that represents actual size,
such as target areas and hole-fitted marks. One occurrence may combine a model-sized
target with an annotation-sized callout. Creo similarly distinguishes fixed,
drawing-unit, model-unit and text-related sizing in [symbol height settings](https://support.ptc.com/help/creo/creo_pma/r12/usascii/detail/Controlling_Symbol_Instance_Height.html).

Measure final text before laying out frames/reference lines. Keep glyphs, text
and leader decorations independently orientable: a left/right leader must not
mirror letters or change weld-side meaning. Preserve filled triangles, line
widths, dash patterns and vector output through the common painter. Text layout
and text glyph generation do not require an OCCT body calculation.

Family rows should encode useful named configurations, not every possible number
or combination. Ten independent yes/no options already produce 1,024 combinations.
Values, optional modifiers and repeatable frame cells must remain editable fields.
PTC's [symbol customization](https://support.ptc.com/help/creo/creo_pma/r12/usascii/detail/about_the_symbol_tab.html)
uses both graphic choices and variable text; its [weld customization](https://support.ptc.com/help/creo/creo_pma/r12/usascii/detail/User_Defined_Parametric_Weld_Symbols.html)
illustrates category-specific groups and leader origins. That weld page references
an older ISO library: use it for software design, not as the current normative drawing rule.

### 5. CAD value providers and engineering semantics

Use typed named providers with explicit context: sheet projection method, selected
model parameter, a saved mass result, revision record or note identifier. Providers
return enums, strings, quantities, points or directions with a validity state.
The library maps these values to rows or fields. No arbitrary script is necessary.

For example, two sheets of the same model can use different projection settings.
Their title-block symbols evaluate against their own sheets. A Part symbol cannot
read the last active Drawing as an implicit source. A missing projection provider
requires an explicit manual value or unresolved state.

Associative geometry and readable graphics do not by themselves create semantic
GD&T, datum-system validation, weld process data or inspection export. Implement
those as typed domain records when required. The renderer can be shared. Do not
claim semantic STEP PMI export merely because the PDF looks correct.
[ISO 16792](https://www.iso.org/standard/73871.html) covers digital definition for
both model-only and model-with-Drawing approaches; this review does not certify
conformance to it.

## Recommended sequence and acceptance gates

1. Multi-Sketch `.symz` editor and row/text selection, isolated from ordinary
   Sketcher layers. Verify create/edit/Cancel, duplicate row, visibility, stable
   IDs, save/reopen and Undo/Redo. Regenerate the prototype library in the chosen
   new format rather than keeping two parallel authoring implementations.
2. Free title-block occurrences: projection binding and general surface-texture
   text choices. Verify two sheets with different projection settings and two
   independent instances of the same library asset, including detached libraries.
3. Shared leader/reference service and model annotation plane. Verify point,
   edge, surface, multiple leaders and exact nested Assembly occurrence identity.
   Reuse the common candidate picker; no separate hover/click hit testing.
4. Local texture, undefined-edge and datum-feature annotations. Test attachment
   to dimensions, leader flips, deleted references, rotated views and hidden lines.
5. Dynamic text frames and typed welding/GD&T records; then targets and computed
   markers. Check long localized text, stacked rows, mixed target/callout scales
   and validity of engineering references independently from image comparisons.

Across these gates, test 1:1/1:2/2:1 Drawing views, perspective and orthographic
3D, section/detail/cropped views, save/reopen, native data embedding, PDF/DXF
vector output and all five UI languages. Property creation and editing use the
same in-application OK/Cancel dialog and shared confirmation contract.

No shared container-placement code should be changed merely for symbols.
If implementation proves that the shared placement contract must change, obtain
the separately required approval before making that change.

This research changed documentation only. Localization review: no application
strings or catalogs changed. No runtime regression tests were needed or run for
this document-only assessment. Remaining uncertainty is exact normative layout
for each complete library and actual GUI/runtime integration, not the feasibility
of the common-Origin/multiple-Sketch approach.
