# Shared symbol placement work

Implementation and verification record, 2026-09-24. This describes the initial
symbol workflow and explicitly lists the remaining authoring and standards scope.

## Agreed behavior

The reusable SYMZ definition is authored around local XY zero, its grip. An
occurrence embeds that definition and its selected variant and text values.
Moving or rotating an occurrence never modifies the library definition.

The same placement service must support Parts, Assemblies and Drawings. A
symbol can contact geometry directly or use a leader terminating in an arrow.
Moving the symbol with a leader preserves its contact. Reattaching to another
object is a separate reference edit. References retain original-object identity,
source-document identity, exact occurrence path and oriented side.

Losing a reference must retain the symbol, values, last valid spatial frame and
leader. It marks the attachment unresolved; it must not delete, reset or relocate
the annotation. A replacement reference can restore the association.

Model annotations must be available through the Drawing's existing Show/Erase
workflow, with source values and separate view-specific presentation. Direct
Drawing-only insertion must remain possible. Library files are not required
sidecars for reopening an inserted annotation.

## Existing means and implementation

The existing Definition supports coplanar Sketches, variants, editable text
fields, insertion point and native SYMZ persistence. Existing Sketch instances
are embedded and already support angle, scale and XY placement. Title blocks
consume these without baking them into unrelated geometry.

`symbols::Placement` adds a validated orthonormal annotation frame, optional
typed reference, unresolved state, leader bends and arrow length. It renders
only vector annotation strokes and does not call OCCT. Reference refresh consumes
persisted original analytic face records. Plane contacts retain local XY;
cylinder/cone contacts retain angle and axial position, so a radius change keeps
the contact on the surface. Exact occurrence identity and the oriented side are
retained. Missing or incompatible geometry preserves the stored frame.

`symbol_operations` provides an editing carrier using the existing Sketch
session and Undo/Redo, while preserving the definition's variant and field
metadata. Native saving writes SYMZ, not a Part archive with a different suffix.
Deleted text/curves remove dangling field/pen metadata from the exported
definition. Undo restores the original metadata associations from the carrier.

The UI additions expose New Symbol, Open SYMZ, Save and Save Copy, with a Sketch
selector for definitions containing more than one Sketch. Recursive symbol
insertion and external model references are disabled in the symbol editor.
Authoring the variant/field table itself remains separate work.

## Verification status

The placement contract executable and pre-existing symbol integration executable
passed locally on Windows. Covered: spatial rotation, leader contact and grip,
loss/recovery of reference, unresolved JSON round-trip, oriented-side retention,
invalid frame rejection without mutation, hidden annotations and zero-length
leader handling. Existing embedding, variant choices, title-block projection,
profile isolation and Undo/Redo checks also passed.

The editor and model-placement GUI contracts passed on Windows, including actual
common-picker face selection, Cancel/OK, Tree/View synchronization and reopening
stored properties. Native Part, Assembly and Drawing round-trip/copy tests passed.
Copy tests exposed and fixed traversal into an embedded library definition's
private namespace. Contact transaction tests cover source movement, deletion and
Undo/Redo restoration. Annotations do not become modeling topology references.

All five translation catalogs passed coverage. Start Part, Skeleton and Assembly
templates were regenerated, and the new-document GUI contract passed. Existing
template and symbol integration contracts also passed. GUI screenshots include
`Projects/test/symbol-authoring.png` and `symbol-attachment.png`.

Drawing inheritance through Show/Erase, paper-size projection and vector painting
passed their core contracts. Direct sheet insertion and exports are covered by
the end-to-end GUI contract described below.

## Deliberately limited scope

- The symbol editor creates and edits geometry and text. A dedicated UI for
  authoring named field/variant tables is not included; existing metadata is
  preserved, and the repository catalog generator defines the supplied fields.
- Advanced geometric-tolerance modifiers and a complete normative semantic
  checker remain outside this initial library.
- Direct 3D attachments currently support original planar, cylindrical and
  conical surfaces. Unsupported surface types do not acquire fabricated contacts.

## Standards research

ISO's public record identifies [ISO 1101:2017](https://committee.iso.org/cms/live/live/en/sites/isoorg/contents/data/standard/06/67/66777.html?browse=ics)
as the published basis for geometrical specification symbols and interpretation;
the record says it was confirmed in 2022. It also explicitly discusses 3D model
attachment through ISO 16792. This establishes scope, not complete implementation
conformance.

The instrument manufacturer's [KEYENCE symbol reference](https://www.keyence.com/ss/products/measure-sys/gd-and-t/symbol-list/)
is useful for identifying characteristic families and associated modifiers, but
contains both ISO and ASME material. Do not copy its mixed list into an
unrestricted ISO modifier menu. Applicable datum and material-condition rules
must be checked individually before the corresponding library controls ship.

Tolerance frames require content-driven cell sizing, ordered datum references,
and vector characteristic/modifier glyphs. Fixed decorative boxes with arbitrary
text do not establish a valid geometric specification. Continue the existing
[symbol feasibility review](SYMBOLS_FEASIBILITY.md) and
[symbol design](SYMBOLS_DESIGN.md), including their nominal-size and dynamic-border
requirements.

## Drawing implementation

Model symbols now enter Show/Erase as a distinct category. Their nominal paper
size is independent of view scale. A Drawing keeps its own grip override while
source text/variant updates remain linked. Repeated Assembly occurrences retain
separate identities and transforms. Core projection, source loss, persistence,
scale and repeated-occurrence checks passed on Windows.

Direct sheet symbols use the same properties dialog. A cached original curve
reference and normalized curve parameter locate contact in one Drawing view.
The contact follows view movement and scale; the paper-size glyph retains its
independent offset. Removal of a source or view retains the last pose and marks
it unresolved. Local symbol X remains conventional, converted at the sheet
boundary to preserve the intentional right-hand Drawing coordinate convention.
Mouse hover/click/cycling uses the existing measurement candidate list.

The shared sheet renderer emits vector glyphs, filled text contours and leaders
for screen, PDF and DXF. Direct sheet symbols support Tree selection, Properties,
removal and grip dragging. Cancel discards previews; native Drawing transactions
own committed changes. The GUI contract exercises direct insertion, reference
picking, Cancel/OK, reopening properties and vector PDF/DXF export, including
DXF reimport. The final acceptance run is recorded separately below.

## Initial geometric-tolerance library

Fourteen characteristic definitions are generated by the repository catalog tool:
straightness, flatness, circularity, cylindricity, line profile, surface profile,
parallelism, perpendicularity, angularity, position, coaxiality, symmetry,
circular runout and total runout. Glyphs use vector curves, not system-font
characters. Optional frame-layout metadata arranges selected Sketch cells and
calculates border widths from actual content. Empty datum cells take no space.

The initial editable fields are a positive numeric tolerance and, where
applicable, ordered primary/secondary/tertiary datum identifiers. Form controls
have no datum fields. Profile controls may omit datums; the other supplied
orientation/location/runout definitions require a primary datum in the dialog.
These are graphical annotations with basic input validation, not an automatic
ISO specification checker or a declaration that the selected surface satisfies
the tolerance. Material-condition, projected-zone, composite-frame and other
advanced modifiers are not offered by this initial library. A zero tolerance
with a material-condition modifier therefore is not part of the supported input.

The [PTC frame layout documentation](https://supporttest.ptc.com/help/creo/creo_pma/r8.0/usascii/model-based_definition/example_geometric_tolerance_layout.html)
describes separate characteristic, tolerance-value and datum compartments.
Its [datum-reference workflow](https://support.ptc.com/help/creo/creo_pma/r12/usascii/model-based_definition/to_specify_gtol_datum_references_and_material_co.html)
keeps ordered datum references and material conditions distinct. This supports
the compartment design; it does not justify accepting arbitrary modifier
combinations.

[ISO 21920-1:2021](https://www.iso.org/standard/72196.html) is the published
surface-texture indication standard; its replacement project is not a published
replacement. [ISO 5459:2024](https://committee.iso.org/standard/87855.html?browse=tc)
is the current datum-system edition identified during this review.

## Final Windows acceptance

The final symbol GUI contract passed (10.50 seconds). It uses actual common-picker
mouse events in a Part and an Assembly, checks exact source/occurrence identity,
and verifies direct Drawing reference picking, Cancel/OK, persisted properties,
free tolerance-frame insertion, PDF output and DXF reimport. Validation rejects
negative tolerance values, missing required datums and gaps in datum precedence.
The PDF was rendered with Poppler and visually checked for readable text,
characteristic glyph, ordered datum cells and leader orientation.

The symbol library, placement, native document, Drawing and integration contracts
passed, as did five-language catalog coverage and both template/new-document GUI
contracts. Existing Show/Erase, measurement and balloon GUI checks passed.
A stale measurement-test color expectation was corrected to the existing shared
hover color; production colors were not changed.

DXF reimport exposed a repeated path-close point exported as a zero-length LINE.
The paint-device exporter now omits that non-geometric segment. Symbol references
also verify source-document identity before refreshing analytic contacts, so a
replacement document cannot acquire an old attachment merely by sharing geometry
IDs. A transaction regression verifies that the last pose is retained unresolved.

This is a local Windows implementation verification, not a published release or
an assertion of complete ISO specification validation.


## Historical symbols and original point references (2026-09-25)

The ISO 1302:1978 example has basic and material-removal variants made from
ordinary Sketch segments and editable text. The default `3,2` sits above the
joining bar; explicit `Ra` values are available in the editable specification.
Longer values extend leftward to avoid the long arm. This historical convention
is distinct from ISO 1302:1992, which explicitly specifies the Ra prefix.
The user guide records the primary source and supported orientation policy.

A persisted per-text Drawing readability option performs a half-turn around the
local text contour center when the combined paper orientation requires it.
The pivot is transformed with the symbol occurrence; it is not recalculated
from a paper-axis bounding box. Ordinary text and 3D display retain their
previous behavior. The option is exposed in Symbol Text Properties in all five
supported languages and does not depend on the catalog symbol identifier.

Drawing dimensions and symbol contacts prefer original point identities only
when persisted ancestry identifies the same point in the same occurrence and
the three-dimensional position agrees. No nearest-point inference is used.
Missing sources retain the annotation's last placement. New intersections keep
their own identities. See [Drawing dimensions](DRAWING_DIMENSIONS_DESIGN.md).

The final focused Windows run passed all four contracts: translations, symbol
GUI, symbol Drawing and symbol integration (17.96 seconds total). It covers
both historical variants, all preset text bounds, combined rotation including
vertical boundaries, text persistence and disabled-option behavior. The native
Windows application was rebuilt. Earlier template/new-document GUI and point
reference contracts also passed. The catalog orientation preview was visually
checked. Interactive user acceptance remains separate from these checks.
