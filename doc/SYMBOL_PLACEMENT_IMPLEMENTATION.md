# Shared symbol placement work

Implementation and verification record. The latest checkpoint is followed by
the historical workflow and its authoring and standards scope.

## Shared leader detail update (2026-09-28)

Inputs are persisted symbol geometry, the selected characteristic/fields and
the original attachment frame. The means are the existing `AnnotationStroke`
layout, shared Properties window and viewer handles. Outputs are consistent
model/Drawing strokes, editable grips and native annotation persistence; this
path performs no OCCT calculation and does not alter container placement.

Handle discovery now uses the leader stroke, available for every leader-bearing
symbol, instead of requiring a separate full-width shelf. This repairs missing
model handles and coincident Drawing handles for structured weld/tolerance
symbols. Framed annotations use the same layout with an external horizontal
landing ending at the nearest side midpoint. The frame-side grip adjusts the
landing length and the elbow grip translates the annotation.

Triangle endings are filled yellow, have their base centered on the reference
contact and always enforce perpendicularity, including on projected edges.
Frame cells center actual geometry/text bounds in both directions. The default
tolerance variant is Position; datum fields appear before placement controls,
and form-only variants explain why they have no datums. Existing per-variant
applicability and datum-order validation remain in force.

`Placement::weld_all_around` persists the weld-junction circle. It is effective
only for a leader-bearing reference-line symbol and shares the same junction
and camera/paper axes as the leader. Its diameter is the arrow length. It does
not add a Body, change a source reference or replace the weld geometry.
The factory identification-line gap changes from 3 mm to 1 mm, an application
style choice rather than a claimed ISO requirement. The user guide records
the reviewed primary sources and the limits of that review.

New regression checks cover all fourteen tolerance variants, datum-entry
visibility, centered cells, triangle direction/fill/perpendicularity, framed
landings on both sides, all-around circle placement, model/Drawing handles,
drag/Cancel, native round-trip and five UI languages.

Windows verification passed all thirteen focused contracts: symbol library,
placement, document, Drawing, integration and GUI; drawing annotation commands,
model dimension layout, balloon backend/GUI, dimension layout GUI, translation
coverage and new-document GUI. The complete interrupted native build and the
final application rebuild succeeded. The three affected factory libraries were
regenerated with their original document, Body, container and feature identities
retained by matching their named objects, then reopened. Start Part, Skeleton and Assembly templates were
rewritten with the current serializer and remained byte-identical; GUI creation
from those templates passed its active-context and command-availability checks.

The GUI drag check repaints the actual GPU child before picking and checks the
rendered handle displacement, including perpendicular placement where changing
shelf length can move the frame without changing authored X/Y. The tolerance
properties screenshot and exported PDF were visually reviewed; DXF reimport
passed. This is focused local Windows verification, not a full application
regression run or a portable release.

## Native library checkpoint (2026-09-28)

Standalone symbol files now preserve ordinary native Part history, Body ownership
and Sketch identities. The editor enters Sketches through ordinary Properties
and Finish, and uses the shared Body activation and context display. Native
metadata stores symbol text choices, visibility and insertion behavior. Saving
or copying the library preserves its complete Part model. Insertion currently
consumes the documented coplanar XY Sketch content.

The four existing weld types are consolidated into twelve Family variants
(arrow side, other side and both sides). Fourteen tolerance characteristics share
one library, and a separate datum-feature library supplies the editable boxed
letter. Per-insertion leader endings include arrow, outlined triangle and filled
dot. Model surface orientation now uses the actual surface normal and stored side
for custom symbols as well as factory symbols.

Before removing the eighteen superseded files, the catalog tool compared every
original variant with its combined replacement. Rendered edge coordinates, pen
colors and filled contours were identical; named fields, preset choices and
custom-entry permissions were retained. Existing occurrences keep their embedded
definitions. References to the former library filenames must be updated when
selecting a library for a new insertion. The replacement is not an assertion of
complete ISO weld or tolerance validation; see the user guide for supported types.

The start Part and Skeleton templates were regenerated with the native metadata
serializer. The unchanged Assembly template was regenerated and reopened too.
GUI checks create new Part and Assembly documents from the configured templates
and verify their normal editable context and commands.

The recovery also removed disposable dependency build/package caches and
generated startup-test directories, freeing 17.1 GiB. Native projects, current
build dependencies and release packages were retained. After rebuilding, the
repository directory measured 32.2 decimal GB (30.0 GiB). The normal development
launcher remains `zima-cad.bat`.

Final Windows verification passed all nineteen focused CTest contracts in
113.07 seconds, covering the native title/frame families, ordinary Family
editing and source replacement, start-template creation, balloons, symbol
library/document/placement/Drawing/integration/GUI and five-language translation
coverage. Family and symbol GUI tests also passed three consecutive runs each.
The symbol GUI covers text lists, variant visibility, OK/Cancel, resizing,
middle-button confirmation, native save/reopen, model/Assembly contacts and
Drawing PDF/DXF output. Editor, frame/title anchoring and weld/tolerance output
images were visually reviewed.

Regression fixtures now distinguish symmetric extrusion extent from X width
and suppressed result geometry from missing original topology. Missing-reference
markers are tested with an unrelated replacement Part, including Undo/Redo and
reference repair. Translation-only verification runs directly, independently of
the unrelated native Windows hover-style animation check. This is focused local
Windows verification, not a complete application regression run or a release.

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

- The symbol editor now exposes Family Table for Sketch/text visibility and
  text-list authoring through the shared Text Properties controls. The editable
  definition participates in the carrier's Undo session and is persisted only
  in SYMZ; the Part/Assembly native formats are unchanged.
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
