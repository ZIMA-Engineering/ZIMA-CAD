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

## Agreed future sequence and product decisions

- Next: DXF import into active Sketcher, small Sketcher fixes exposed by real
  DXF files, then DXF import from Part/Assembly through one shared Import/Export
  framework. Part/Assembly import creates an explicitly placed ordinary Sketch.
- Redesign Assembly STEP import so imported data becomes a normal component,
  preferably a generated `.prtz` referenced relatively by the Assembly.
- Add unified export including Sketch DXF, model formats and offscreen PNG
  image export; JPEG is optional and secondary.
- Add per-Part/per-Assembly-instance transparency. Colored wireframe is deferred
  because it is rarely used.
- Then implement a separate 3D Curve history container, ordinary Sweep and a
  parametric Helix/Sweep workflow for springs. Do not copy the Pro/ENGINEER
  spring workflow; agree the ZIMA engineering parameter model with the user
  before coding it.
- A first useful Pipe feature can remain simple: a 3D Curve plus automatic
  circular/annular Sweep, outside diameter, wall thickness, bend radius and
  stable Start/End connector frames. Catalog piping systems come later.
- Follow Sweep with semantic Hole features and cosmetic thread metadata. Never
  build real helical solid thread geometry.
- Add per-face color/appearance in Part through stable semantic `FaceRef`
  ownership. Assembly inherits source face colors and may add instance-local
  overrides; imported STEP colors are converted into the same persisted model.
- Implement named **Section** definitions in Part and Assembly before expanding
  Drawing section views. Sections are non-destructive model-space clipping and
  inspection definitions; Drawing should later reuse them rather than own an
  incompatible duplicate definition.
- Pattern and Mirror are high-priority Part/Assembly features after the core
  Part set. Start with linear/circular feature patterns and Part feature/body
  Mirror, then Assembly component Pattern. Every occurrence must have a
  stable semantic ID; count changes never shift references to another copy.
  Do not initially implement a general Assembly reflection transform. Provide
  **Create mirrored Part** instead: dependent creates a separately identified
  derived `.prtz` linked to the source; independent creates an ordinary
  standalone mirrored `.prtz`. Both insert into Assembly as normal components
  with their own BOM and Drawing identity.
- Complete Protrusion target extents (face, plane, point, through-all and
  offsets) with independent stable Start/End definitions and own-value fallback.
- Central Undo/Redo is intentionally late. Preserve atomic confirmed changes,
  transient previews and stable IDs now so it can be added after the document
  model settles.

## Development rules to retain

Follow `AGENTS.md`: feature/property editors are internal Qt SubWindows using
the shared properties interaction; creation and editing use one dialog;
calculation occurs only at the explicit staged calculation boundary; history
editing rolls back to the real preceding geometry; UI selection and inspection
consume persisted ZIMA viewer data and never reconstruct live OCCT topology;
legacy Part/Assembly compatibility is not required.

All future reference-driven parameters obey the mandatory own-value fallback
contract in `doc/STABLE_TOPOLOGY_NAMING.md`: persist and refresh the last
successfully evaluated independent value, follow supported descendant-to-parent
ancestry, use that value if the reference becomes missing or ambiguous, keep
the broken reference visible for repair, and automatically resume association
if the same semantic reference returns. Never reset to zero or silently bind to
a different runtime object.
