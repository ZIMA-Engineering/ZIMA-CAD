# MECH keyways

User agreements recorded on 2026-11-27. Implementation was requested after the
design discussion. This document separates agreed behavior, implementation
proposals, and verification still required. It is not a completion report.

The implemented Windows development command, native key library and measured
checks are recorded in [the verification report](verification/20261127-mech-keyways.md).
That report also identifies unverified combinations and the protected project's
pre-existing unsupported native files; the design agreements below do not imply
that those verification boundaries have been closed.

## Scope and interaction

MECH is another Part application. Its first command creates a keyway inside the
active editable Body. The module label is MECH in all five languages; descriptive
translations are recorded in POST_RELEASE_IDEAS_20261126.md. Initial icons are
resources/icons/mech.svg and resources/icons/keyway.svg, using the existing
palette-aware SVG renderer and cyan feature accent.

1. Select the actual support face. Classify internal/external material side and
   cylindrical/conical analytic support using exact persisted geometry.
2. Offer applicable key/keyway variants and a table-derived size suggestion.
   Parallel keys and Woodruff keys are distinct choices. The user subsequently
   restricted tapered shafts to Woodruff keys only. Do not offer a parallel key
   on an external cone. Verify the circular cutter seat and its depth station.
3. Select the axial placement style and its reference fields. Shaft placement
   supports an offset from an end/shoulder face or plane, and symmetric placement
   about a plane. A midpoint between two end faces is a proposed way of defining
   that symmetry plane.
4. An internal keyway uses the bore, starting face, an orientation reference and
   an Up To end reference. Reuse the existing native feature limit behavior.
   The user subsequently confirmed independent limits on both sides of a
   reference plane for **both cylindrical and conical hubs**. Each side accepts
   Up To with its own end reference and outward correction. Both target rows
   are always visible for internal cylinders and cones, and hidden for shafts.
   Hub grooves are always through: no authored length, blind termination or
   rounded end is offered. Length is a derived informational dimension.
   This is one universal
   command, not a cone-only special case. Preserve direction and selected
   limit identity when the support or hub length changes.
5. Both external and internal keyways provide angular correction about the
   support axis relative to the orientation reference, initially zero.

Keep placement, axial extent, and end shape separate. A closed shaft slot has
rounded ends. A slot open through one shaft end has a rectangular portion and
one semicircular closed end. Its straight end may overrun the real shaft end;
the Boolean cut produces the actual opening. Two open ends use a straight-ended
cutting profile. Do not reject valid cuts merely because the cutter overhangs.
The longitudinal round ends and the bottom-to-wall fillet are separate radii.
Length and offsets refer to actual profile extremities, including round ends.

No pattern controls belong in the MECH command. The complete MECH feature must
be a valid source for the existing independent circular Pattern command,
including the completed bottom fillet. This integration requires verification.

Further agreements during implementation:

- Regeneration follows the current support diameter and selects the applicable
  catalogue size again. A cone uses the diameter at the Woodruff cutter centre.
  Keep reference identity, position and angular orientation through size changes.
- Width is table-controlled and informational, with no direct numeric override
  while the feature remains managed by MECH. Independent shape editing becomes
  available after full decomposition. Shaft parallel-key length remains editable
  both in Properties and by its dimension in the View. Hub length is derived
  from its two required Up To end references; Woodruff shaft length follows
  the selected disc. These are distinct from the catalogue width restriction.
- Ordinary single click selects. Double click in the View or Tree displays all
  feature dimensions, including informational width and the applicable length,
  placement and angle, through the shared inspection path. Inspection does not
  calculate geometry or create an Undo step.
- Update the key/keyway table illustrations using the user's original drawings
  where useful. Keep historical CSN and reviewed DIN data explicitly distinct.
- Author ordinary native library key models using the existing library naming,
  metadata and verification conventions. Parallel-key models have two rounded
  ends, including the retaining-screw variants. Open-ended SHAFT KEYWAYS do not
  authorize square-ended key models. Add Woodruff key models as well.
- Use one reviewed dimensional catalogue for MECH and the library generator.
  Width/diameter assignments, manufacturing dimensions and retaining-hole data
  must retain their exact standard/source identities.
- Parallel-key width is a linear dimension between the sides, not the end-arc
  radius or diameter. Overall length is measured between the exact arc extrema,
  including native contact points where necessary. The same overall convention
  applies to decomposed keyway profiles; neither dimension uses a text override.
- The lower planar seating face of parallel-key library models has a subdued
  ochre appearance (`#D5A24C`, group `key-seating-face`). Its persisted native
  face reference identifies the extrusion's lower cap at Z=0; color is only a
  visual cue. Dimensions, chamfers and regeneration must retain that reference.
  This distinguishes the seat from the future counterbored upper face. The
  current standalone parallel-key library contains plain keys; perforated key
  variants have not yet been authored.
- The library must not be restricted to CSN. The requested ISO search found
  ISO/R 773:1969 and ISO 3912:1977 withdrawn. Include reviewed DIN 6885-1 and
  DIN 6888 choices with separate catalogue identities; retain the existing CSN
  sources. Details and review limits are in MECH_KEYWAYS_STANDARDS_REVIEW.md.
- The user subsequently approved DIN 6885-1, form A, as the default parallel-key
  series and DIN 6888 as the default Woodruff series. Provide a standard/size
  series selector; the chosen series controls key size, keyway depth and
  tolerances. CSN remains an explicit alternative for existing designs. Carry
  the same exact standard identity into library names and BOM metadata. This
  agreement does not remove the need to verify each implemented numerical row.

## Future Assembly key insertion (idea, not current implementation scope)

The user proposed selecting a shaft keyway in an Assembly and using a context
action to insert the matching key. The placement would reference the bottom of
the shaft keyway. This proposal concerns the shaft seat; a hub keyway alone is
not the proposed placement support. Preserve the keyway's catalogue identity,
dimensions and stable floor reference so this can be considered later.

While authoring library keys, a generated native key with a Family table is one
possible way to supply the size variants. Separate library models and other
generation approaches remain alternatives. The user explicitly described this
as an idea on 2026-11-27, not a decision to implement a Family table or Assembly
insertion now. Decide the mechanism when the library and Assembly workflow are
designed; do not treat this note as a new mandatory persistence contract.

## Native feature composition

Use ordinary Part history operations: authored Sketch, subtractive Extrusion or
appropriate ordinary material feature, bottom Fillet where needed, optional
ordinary threaded Holes, a following annotation Sketch, and existing Symbols.
Do not substitute an opaque final solid for editable authored history.

The compound MECH owner manages stable child identities and semantic roles.
Its properties consume the protected shared placement solver, with only the
explicitly approved cone extension described below. Creation and later editing use the same internal properties
window and shared reference-entry controls. Reuse the common placement
capability predicate, orientation correction, Origin handling and confirmation.

Following decomposition testing, the user revised placement on 2026-11-27:
the default compound Origin lies on the selected cylindrical/conical support.
The transverse face/plane controls axial position, and a longitudinal plane
controls angular position. Those two planes may be entered in either order;
the command identifies their geometric role and advances reference entry.
Consume the ordinary placement controls and exact reference solver. Explicit
numeric/whole-Origin placement on the axis remains a valid alternative.

For a parallel key on a cylindrical shaft, author the planar slot profile on
the surface and subtract inward by the catalogue depth. For a hub, author the
profile at the bore surface and use two radial sides: outward by the groove
depth, inward Up To the exact cylindrical/conical support. These radial sides
are independent of the two longitudinal Up To limits. For an axis-parallel
floor on a cone, place the planar cutting Sketch on the largest-radius end
generator and adjust the outward depth to retain the specified floor; this
prevents the inward target from selecting the opposite bore wall. Preserve the
ordinary editable operations, floor references, fillets, dimensions and symbols
after full decomposition. Woodruff grooves retain the actual circular cutter.

The user explicitly approved extending the protected common angular-placement
solver from cylinders to cones on 2026-11-27. A directed angle selects an exact
cone generator; its radius varies with axial station. A normal surface offset
changes the generator intercept by offset/cos(semi_angle). No serialized fields
are added. Verification must cover cylinders and cones, both plane orders,
reference sides, diameter/slope edits, save/reopen and Undo/Redo. This paragraph
records the approved change; completion requires the follow-up verification.

The new, unreleased payload has no duplicate axial-face reference: the ordinary
placement owns that dependency, including its offset and side. Two Up To end
references remain independent keyway extent inputs. Geometry and generated
children are authored in the owning Body's coordinates, with exactly one
conversion of document-level datum/reference packets at that boundary.

The only decomposition action is **Rozlozit vse** (the existing localized UI
key is `Rozložit vše`). It releases the complete feature as ordinary editable
operations. Preserve the exact result, child/reference identities, annotation
associations and dependencies. One Undo restores the compound definition.
There is no partial decomposition action. Normal body-history rules apply.

## Manufacturing dimension

The user confirmed one **Keyway** command for shafts and hubs on 2026-11-27.
Identify external/internal support from the selected analytic material face,
show that classification explicitly, and expose the appropriate parameters in
the same create/edit dialog. Separate shaft/hub shortcuts are not required.
The MECH application icon is a gear; the Keyway command has its own slot icon.

After constructing a parallel-key keyway, create a dependent transverse annotation Sketch.
It references the finished floor and the exact supporting cylinder. A real
referenced point on the opposite side of the cylinder lies on the measurement
line through the support axis in the keyway symmetry plane. Keep the selected
opposite branch stable. Add a non-driving dimension between that point and the
floor reference. The dependency direction is feature -> annotation Sketch ->
dimension; it must never feed back into the feature's geometry.

For a cylindrical shaft the nominal value is D - shaft_depth; for a cylindrical
bore it is D + hub_depth. The user's hub example is 38.3 with upper deviation
+0.2 and lower deviation +0.1. Store measured value and presentation/tolerances
separately. A width dimension can display 10P9 while retaining its numeric width.
Do not copy a depth tolerance unchanged to the opposite-side dimension.

The existing SketchDimension.driving flag and shared tolerance fields provide
the non-driving behavior. Exact bindings across the generated solid and transfer
to Drawing remain to implement and verify. A cone additionally requires an
explicit measurement station and floor convention; cylinder equations must not
be silently applied at an arbitrary cone diameter.

For a Woodruff shaft seat the user selected the direct depth **t1** dimension
from the nominal shaft surface to the seat floor, rather than the opposite-side
diameter dimension. On a taper, evaluate both points at the cutter-centre axial
station. Keep this dimension non-driving with exact native support/floor
references and use the depth deviations without reversing their signs.

Tapered hubs are explicitly in scope. Verify their longitudinal groove floor,
depth convention and key-top clearance against the mating Woodruff key and
tapered shaft. Do not infer that the shaft's circular cutter section is also
the hub groove, or that a parallel cylindrical-hub floor can simply be reused.
The paired shaft/key/hub construction and its measurement station still need
an independently checked source and a native regression fixture.

## Surface texture

Provide the agreed Show roughness checkbox. Populate the definition selector
from config/symbols/surface-texture; currently ISO 1302:1978 and ISO 21920.
Default to the ISO 21920 definition. Embed/reuse the selected symbol's existing
property controls and allowed text choices, separately for walls and floor.
Prefill supported values from reviewed table data and retain user overrides.
A missing recommendation remains unset. Do not present an old Ra-only table
entry as a complete verified specification under a newer standard.

The MECH dialog and direct 3D symbol properties edit the same symbol occurrences.
Preserve each actual face association, readable drawing orientation and the
ordinary click/inspection/edit contract. Changing geometry must not reset a
user-authored symbol value. Referenced Symbol definitions remain embedded under
the existing native Symbol contract. Standard selection and roughness values
must not become independent duplicate metadata with competing owners.

## Optional retaining screws

The user explicitly made retaining screws optional. For the shaft Part command,
the proposed output is the corresponding threaded holes; screw components are
separate Assembly items. The supplied local table shows one screw at l/2 or two
screws positioned b from the ends with pitch f = l - 2b. The screw holes must be
referenced to the intended key length/position, especially for open-ended cuts.
Do not use a cutter's artificial overrun as the key's nominal length.

The local source is 04-MECHANICAL_TABLES/08-PERA A DRAZKY/
02-Pera tesna a pera s pridrznymi srouby/0000-index/index_cs.html and its 01.png
and 02.png. Its shaft-thread depth t2, thread d, key counterbore D1/d1/k1 and
length availability have different meanings. Counterbores in the key must not
be cut into the shaft. Extraction-thread holes are not retaining-screw holes.

Online review is recorded in MECH_KEYWAYS_STANDARDS_REVIEW.md. A current edition
listing does not verify every historical numerical row. Preserve source and
review status; do not invent missing values or silently equate CSN and DIN forms.

## Approved persistence scope

### Deferred parametric key family

The user proposed a generic parallel-key master driven by Relations/Family
variants for b, h and overall length, with optional zero/one/two retaining holes.
Record this as a future proposal, not an implemented replacement for the current
independent library models. Woodruff keys require their own family. A future
Assembly action may insert an appropriate key from a selected shaft keyway and
attach its real seating face to the groove floor. The visual seat color does
not replace that native attachment or the standard/size identity.

### Existing MECH agreement

The user explicitly approved this scope on 2026-11-27 through the format-change
question, including affected factory/template verification and protected-project
backup/copy checks. Original production-project replacement is not authorized.

The present Part model has an explicit FeatureKind discriminator and typed
parameters (part_document.hpp), and strict native JSON format version 80
(part_document.cpp). SheetProfileParameters and SheetProfileBinding demonstrate
the existing compound/native-child pattern but cannot honestly encode keyways.

Proposed extension: a typed MechKeyway owner with exact support/reference IDs,
internal/external and cylinder/cone choice, table identity and chosen dimensions,
placement/extent/end selections, optional retaining-hole settings, and stable
generated-child/symbol/annotation IDs. Reuse shared placement and child operation
definitions. Keep all authored state inside the native document; add no required
sidecar or external geometry. Do not hide the new contract in user text fields
or reuse an unrelated feature kind to evade the format-change agreement.

The scope follows AGENTS.md File-format compatibility and Active production-test
project protection. Inventory currently
finds 1,036 .prtz, 2 .asmz, 137 .drwz, 10 .symz, 1 .frmz and 1 .tblz under config;
these are a candidate impact inventory, not a claim that every asset needs
conversion. Start Part/Skeleton and Assembly templates exist in mm and inch.

Prefer a change confined to the new feature's discriminator/payload. Determine
which existing assets actually require rewriting; if a global version increment
is necessary, include every affected embedded Part document and factory asset.
Use the changed native implementation to regenerate, save and reopen affected
assets and templates. Verify GUI creation from both unit-system templates,
active Body context, normal commands and affected insertion operations. Do not
introduce migration branches or merely rewrite version numbers.

The protected project is
C:/Users/vladi/Prace/ZIMA-Engineering/03-CAD/01-ACTIVE_PROJECTS/ZE0176-TOYOTA-G92K1X.
The read-only inventory found 7 .prtz, 1 .asmz and 2 .drwz; the full directory
contains 172 files and 2,603,459,762 bytes, including historical/other files.
Before any conversion, resolve native dependencies, create and hash-verify a
recoverable backup, and validate reopening/modeling/reference operations on
isolated copies. No original production file has been converted or overwritten
for this work. Any original replacement needs the agreed conversion scope.

## Command lifecycle and verification

Read FEATURE_PERFORMANCE_GUIDE.md. Opening and creating publish the rollback
input scene once. Picking, table/symbol choices and wire previews use current
exact persisted data without OCCT calls. Changed OK privately builds/updates
children, validates and commits one transaction; unchanged OK performs zero
geometry calculations and creates zero Undo entries. Cancel restores the input
and normal full-history scene. Annotation-only edits reuse valid material data.

Verification must include external/internal cylinders and cones; orientation
and both sides; offset/symmetric/Up To; closed and one/two-open-end slots;
parallel/Woodruff variants; no/one/two retaining screws; symbol choices and user
overrides; repeated geometry changes; native save/reopen; Undo/Redo; reference
loss/repair; full decomposition and later native child editing; circular Pattern;
Drawing section dimensions, positive/negative deviations and symbol ownership.
Check equations and material results independently, not just nonempty BReps.

Exercise shared placement entry, Origin entry, inspection, replacement, Cancel,
resizing, ordinary selection and double-click behavior in the GUI. Generated
Sketches must remain editable after decomposition: apply the applicable Sketcher
dimension/drag regression matrix without claiming that one solve proves editing.
Measure complete interactions and calculation/scene counts. Validate all five
locales and Light -> Dark -> Light icons. Windows and Linux share C++ code;
report any missing Linux execution explicitly. No build or completion is claimed
by this design record.
