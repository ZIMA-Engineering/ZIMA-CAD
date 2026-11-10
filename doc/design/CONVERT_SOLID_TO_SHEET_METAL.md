# Convert Solid to Sheet Metal and Rip

Status: design proposal recorded from the user discussion on 2026-11-10. It is
not an implementation claim and must remain a separate change from the Sheet
Profile work.

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

The exact Tree operations and persistence representation require an agreed
native-format design before implementation.

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

## Required design gates

Before implementation:

- define stable ownership and source-reference identities for every generated
  child;
- agree the native `.prtz` format change and regenerate all affected factory
  assets and templates;
- define editing, suppression, deletion, detach/explode, Undo/Redo and
  save/reopen behavior;
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

## Initial verification matrix

Cover at least: no Rip versus explicit Rip; direct edge versus Sketch line;
single and multiple corners; one and multiple generated Bends; detach one child;
explode all; source-dimension edits; broken/repaired source references; stable
face/edge identities; suppression; repeated Undo/Redo; save/reopen; partial
unfold if implemented; and equivalent Windows/Linux calculation results.
