# Convert Solid to Sheet Metal and Rip

Status: the associative conversion core was implemented on 2026-11-10. Stable
source bindings, generated-child ownership, source-driven regeneration and
last-valid recovery are implemented. Rip, detach/explode and partial-unfold
extensions remain design proposals.

## Goal

Convert an authored solid into an associative sheet-metal definition without
discarding the original parametric geometry. The converted sheet must remain
traceable to the solid's real source features so that the user can inspect and
edit the original Sketch geometry and dimensions.

## Proposed history ownership

- `Convert Solid to Sheet Metal` is the owning history container.
- Generated sheet faces, Bends and other conversion results are nested children
  with stable persistent identities.
- Exploding the complete conversion releases all children into ordinary history
  while retaining the former parent row as required by the established history
  contract.
- Removing or detaching one child from the conversion releases only that child
  from the nested ownership; it does not destroy or re-identify the remaining
  children.
- Generated geometry keeps an explicit reference to the source solid feature.
  Context inspection must expose the original authored preview wire and its
  dimensions through the ordinary feature-inspection path. It must not create a
  duplicate hidden solid or reconstruct editable geometry from Boolean result
  fragments.

The implemented Part format 51 persists the owning conversion, source Body,
stable source face/edge roles and generated child bindings. Bound Flat/Bend
children are nested below the conversion row. A source edit regenerates only
the conversion boundary and its dependent result while retaining child and
Sketch identities. If a required source face disappears, the dependent sheet
keeps its last valid geometry and reports the broken association instead of
moving or being silently rebuilt from an unrelated face.

Conversion within the same Body replaces that Body's material result. Conversion
to another Body keeps the authored source solid as editable reference history,
but the source solid is not included in the destination sheet result. Imported
STEP reconstruction deliberately remains an ordinary independent sequence of
Flat and Bend features; it is not locked inside this associative owner.

## Rip definition

Use the stable internal command and data-model name `Rip`. The recommended Czech
UI term is **Nastřižení**, matching the official Czech Autodesk Inventor
terminology. **Střih** is understandable but sounds like the broader manufacturing
operation rather than the local opening needed for unfolding. Do not use
**Zlom**, because it implies a Bend or a fracture, and avoid the generic **Řez**,
which would conflict with material-removal commands.

`Rip` is an optional sheet-metal preparation feature created before conversion.
Its initial requested mode is a linear Sketch definition on a selected solid
face. The path joins the relevant sharp corner or vertex to the end of the bent
solid. When conversion encounters the Rip, it opens the body along that exact
definition. When no explicit Rip applies, conversion uses the established
automatic seam-selection rules.

The design should not hide the authored Sketch inside an opaque calculation.
The Rip owns or references a normal persisted Sketch, remains editable in
history, keeps stable support/side identity, and retains its last valid frame
after reference loss. Future modes may support direct edge, point-to-point and
face-extent definitions without changing the command identity.

The terminology and interaction are consistent with established CAD usage:

- Autodesk Inventor exposes a history `Rip` feature with Single Point,
  Point-to-Point and Face Extents methods:
  <https://help.autodesk.com/cloudhelp/2022/ENU/Inventor-Help/files/GUID-B30BDD38-9CF6-496E-9133-52A59CDCAEE6.htm>
- SOLIDWORKS accepts model edges, linear Sketch entities, or a combination as a
  sheet-metal Rip definition:
  <https://help.solidworks.com/2025/english/SolidWorks/sldworks/c_rips.htm>
- Autodesk Fusion treats conversion of an existing solid body as a dedicated
  Convert to Sheet Metal workflow:
  <https://help.autodesk.com/cloudhelp/ENU/Fusion-Sheet-Metal/files/SM-TO-CONVERT-TO-SM.htm>

## Partial operations

Two operations must remain distinct in the UI and implementation:

1. **Detach one generated child** changes history ownership but leaves the same
   modeled state and identity.
2. **Partially unfold a sheet region** changes the calculated sheet state and
   requires a defined stationary region, connected-region rules and Rip/seam
   boundaries.

The user's proposal explicitly requires the first operation. The second is a
desirable capability, but its exact meaning and selection contract must be
agreed before implementation. Czech UI wording must not use one ambiguous
translation of "explode/unfold" for both actions.

## Remaining design gates

The implemented associative core satisfies stable ownership, source identity,
Undo/Redo, save/reopen and native-format persistence. The following gates still
apply before adding Rip, detach/explode or partial unfold:

- define editing, suppression, deletion, detach/explode, Undo/Redo and
  save/reopen behavior for released children;
- define automatic seam selection and deterministic precedence between explicit
  Rips and automatic rules;
- define supported source solids, thickness inference, corner classification,
  failure diagnostics and non-developable regions;
- reuse the existing history rollback, Properties, reference-entry, selection
  wire and dimension-inspection contracts;
- keep one shared OCCT implementation on Windows and Linux and verify geometry,
  side identity and persistence on both platforms;
- localize every final UI label and diagnostic in Czech, English, German,
  French and Russian.

## Verification matrix

Implemented Windows coverage includes planar source-dimension edits, repeated
cylindrical radius edits, one and multiple generated Bends, stable child and
Sketch identities, unchanged regeneration, broken source-face last-valid
retention, two-level Undo/Redo, cold save/reopen and exact calculated-boundary
fingerprints. Rip, direct-edge versus Sketch-line seams, detach one child,
explode all, partial unfold and equivalent Linux execution remain future matrix
items.
