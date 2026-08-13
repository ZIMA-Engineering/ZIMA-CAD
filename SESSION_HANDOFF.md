# Session handoff — 2026-08-13

## Current state

The current work concentrated on Part Protrusion/Revolve interaction, stable
placement references, thin solids, Sketch dimension direction and exact
Fillet/Chamfer inspection. The working tree also contains unrelated user edits
and diagnostic images; preserve them.

Implemented and manually verified in the GUI:

- Protrusion/Revolve Properties keep their rows anchored below Container
  placement while extent-specific rows appear below them without moving the
  upper controls;
- ordinary extrusion defaults to 50 mm; the second two-sided value defaults to
  60 mm, and Revolve offers 360 degrees for one side and 45 degrees when a
  two-sided or symmetric mode has no previously entered value;
- the direction/extent switch keeps explicit Start and End identities stable;
  flipping a two-sided feature swaps both sides and their dimensions, while a
  symmetric feature keeps the same Start and End faces;
- purple Protrusion manipulators and yellow dimensions are visible throughout
  active Properties, including initial creation, thin mode and after dragging;
  the manipulator drag is continuous and its displayed value snaps to 1 mm;
- Enter commits the focused numeric editor without accepting Properties;
  negative in-view length/angle input flips the feature direction while the
  displayed driving magnitude remains positive;
- open profiles create thin solids in the ordinary Part modeler; closed
  profiles create ordinary solids. Standalone surface output is intentionally
  reserved for a future surface-modeling workspace;
- thin features persist semantic Inside/Outside and Start/End topology ancestry
  and reference recovery follows the nearest surviving descendant, then its
  parent when no descendant exists;
- profile-plane offsets, including negative values and RX/RY/RZ correction,
  use one physical frame for the Sketch, body calculation, yellow dimensions
  and purple manipulator;
- Sketch distance dimensions are unsigned except for X/Y axis dimensions;
  entering a negative ordinary distance mirrors the movable side and persists
  a positive length;
- non-negative spin boxes step by 1 mm at and above 1 mm, by 0.1 mm below it,
  and stop at 0.1 mm through the buttons while retaining finer manual input;
- Part hover/click/RMB uses the viewer's ordered candidate list. Fillet and
  Chamfer highlight only the exact persisted boundary wire, never the complete
  result body;
- Fillet/Chamfer double-click dimensions use a stable parametric curve frame.
  Circular treatments derive their section from the owning local axes; radius
  dimensions point to the arc between both rim circles and chamfer extension
  lines start on the two actual rim circles;
- deleting an upstream feature removes invalid operational edge picks and
  leaves the dependent history item visibly failed rather than silently valid;
- creation paths for Sketch, Protrusion, Revolve and constrained primitives
  were exercised after fixing the accidental Protrusion call to Revolve-only
  extent state.

Verification at handoff:

- the user manually confirmed Protrusion placement/rotation, thin behavior,
  offset planes, exact Fillet/Chamfer selection and the final treatment
  dimensions;
- `python -m py_compile zima_cad/app.py`, `git diff --check`, and the five
  architecture tests pass;
- the first open of a document may populate caches; a second open of the tested
  Part was fast. The dimension-frame calculation runs only when its inspection
  dimension is activated and does not run during document loading.

## Continue next

1. Add focused regression tests for stable Protrusion Start/End references,
   two-sided Flip, offset/RX frames, thin-to-solid ancestry and dependent
   feature recovery after regeneration and save/reload.
2. Add viewer-data tests for circular Fillet/Chamfer normal sections, both rim
   witnesses and activation-lifetime frame stability.
3. Profile cold and warm opening of a long real Part and add stage timings for
   document decoding, persisted BodyResult loading, regeneration and GPU scene
   upload.
4. Finish first-point Sketch inference at origin/reference intersections,
   including simultaneous coincident/axis suggestions and deterministic snap.
5. Continue Drawing and title-block work without restoring user-deleted fields
   or obsolete template coordinates.

## Development rules to retain

Follow `AGENTS.md`: feature/property editors are internal Qt SubWindows using
the shared properties interaction; creation and editing use one dialog;
calculation occurs only at the explicit staged calculation boundary; history
editing rolls back to the real preceding geometry; UI selection and inspection
consume persisted ZIMA viewer data and never reconstruct live OCCT topology;
legacy Part/Assembly compatibility is not required.
