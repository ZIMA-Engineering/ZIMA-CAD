# ZIMA-CAD Roadmap

ZIMA-CAD is an open-source parametric 3D CAD system for designers and
mechanical engineers.

The roadmap describes the intended development direction. Its order may change
as the document model, geometric kernel integration and user workflows evolve.

The application is now native C++/Qt. The obsolete Python implementation and
runtime were removed on 2026-09-15. See [native architecture](doc/CXX_ARCHITECTURE.md)
for module boundaries and [distribution rules](doc/DISTRIBUTION_CLEANUP_PLAN.md)
for the agreed packaging direction. Dated entries below preserve planning context;
newer completion notes and focused contracts supersede their earlier status.

## Requested follow-up features (2026-09-24)

- General spherical-surface feature. Agree on its inputs and editing parameters
  before implementation; this request does not specify the construction method.
- Modeling command **Straighten** (localized Czech label: **Narovnat**): take a
  profile and straighten it according to user-defined parameters. Define the
  preserved dimensions, reference direction and supported profile types first.
- Complete the engineering-symbol functionality and library; review missing
  symbols and editing/insertion behavior with the user.

These are backlog entries, not part of the current UI, import and measurement fixes.

## Container placement on curved surfaces (2026-09-25)

- Teach shared container placement to attach to cylindrical and other curved
  surfaces, including elliptical and spline surfaces. Agree on the contact
  position, orientation, offset and remaining degrees of freedom before
  implementation. Distinguish attachment to a cylinder's surface from attachment
  to its axis.
- Resolve the actual surface contact and local normal from persisted native
  reference data; never substitute the plane of the first display triangle.
  Preserve original-object identity, occurrence path and contact side through
  editing, regeneration, save/reopen and Undo/Redo.
- Keep picking and property editing fast and free of OCCT body calculations.
  Preserve existing point, axis, plane and curve placement behavior.
- Status: requested backlog only. Changes to the protected shared placement
  contract still require explicit approval of the proposed design.

## Agreed command console and Codex integration (2026-09-09)

The shared console and CLI coverage for supported CAD operations are complete
as of 2026-09-15; see [command coverage](doc/CAD_COMMAND_COVERAGE.md). AI integration
remains a separate stage. The original agreement below preserves its intended
interaction and architecture; it does not restart the completed console work.

- A Qt console spans the full application width at the bottom, including the
  area beneath the Tree and side panels; drag to resize or collapse to one line.
- Identical CAD commands and behavior on Windows and Linux, with command history,
  completion, and readable results/errors.
- Unix-style progress on one continuously updated line: operation text, a text
  progress bar and percentage for measurable progress, otherwise an activity indicator.
- A shared command layer for GUI, console, scripts/macros, and AI. Gradually expose
  all supported CAD operations without duplicating their logic. Preserve active
  document/occurrence, ownership, units, stable references, explicit regeneration,
  and shared reversible transactions.
- An optional system-terminal mode (such as Bash on Linux or PowerShell on Windows);
  the CAD console must not depend on a particular shell.
- Subsequently verify and connect Codex through App Server: sign-in, conversation,
  and incremental results inside the application. Give AI access to model state,
  current selection, parameters, and operation results so it can both perform and
  verify modeling through the same CAD commands. Sign-in alone does not provide this capability.

Order: shared command layer and console with initial commands, gradual operation
coverage, then AI integration. The comprehensive Undo/Redo audit remains a separate
later task under the existing agreement.
Technical reference: [Codex App Server](https://learn.chatgpt.com/docs/app-server).

## STEP: bodies and Assembly structure (2026-09-09)

A newer agreement prioritizes STEP before Sketcher offsets. Part import creates
separate bodies; Assembly import creates source Parts/subassemblies with shared
repeated definitions. Export preserves product structure, placements, and physical
size. Details and verification: [STEP import/export](doc/STEP_IMPORT_EXPORT.md).

## Completed Sketch and Drawing fixes (2026-09-09)

Added signed negative Assembly angles and their dimensions, opening a component's
source in its own tab, and Sketch retention for Assembly Extrusion/Revolution.
Sections use shared Origin selection; inactive sections can display their contour
and hatching over the complete body. Lost references remain visibly marked until
repaired. Empty View and the first Sketch use approximate monitor 1:1 scale.
The solver preserves axial translation when editing pitch.

Owned-Sketch dimensions can be edited directly in open Sketch, Extrusion,
Revolution, and Sweep Properties. Returning from Sketcher preserves pending edits;
OK commits them and Cancel discards them. Trim uses immediate shared preview in
all Sketcher hosts and transfers tangency and retained-point constraints. The solver
handles an arc-radius change with a tangent segment connected to another arc.
Details: [Sketcher](doc/SKETCHER.md).

Drawing views retain their own orientation when selecting a section. Section labels,
traces, and grips share display rules; hatch parameters are shared with the source
model. Title-block and BOM values use the relevant Part/Assembly parameters and
their order. Details: [Drawings](doc/DRAWINGS.md) and [Sections](doc/SECTIONS.md).
Launching Windows through a direct shortcut opens no console.

## Next Drawing proposal: Show/Erase (2026-09-09)

Prepare a filter for original dimensions, axes, and auxiliary source-model geometry.
Persist visibility and moved positions separately per view. The first version does
not change model dimensions; manual dimensioning is a separate step. Add dimension
grips at text and arrow ends. Working guides are gray dashed lines with configurable
first offset and spacing, excluded from PDF printing.
Details and boundaries: [Show/Erase proposal](doc/DRAWING_SHOW_ERASE.md).
This is a future-work proposal, not a claim of completed implementation.

## Sections through placed containers and Sketcher (2026-09-09)

The Sections group immediately follows the document Origin and contains the
non-removable No Section state. A-A, B-B, etc. share container placement, own an
XY/XZ/YZ plane, and support full editing of an open line or polyline in Sketcher.
Activating a section clips View; Drawing uses the same persisted definition.
Hatching, side selection, and separate component modes remain supported.
See [Sections](doc/SECTIONS.md).

## Drawing views and PDF (2026-09-08)

Drawing views add silhouette curves from persisted tessellation and split partially
occluded edges. Hidden edges support dashed or gray presentation; shading works
with and without edges and uses local depth occlusion. Sheets store physical line
widths; multi-sheet PDF export preserves scale, dashes, and widths.
See [Drawings](doc/DRAWINGS.md).

## Text, title blocks, and mass (2026-09-08)

Added ordinary/modeling text modes, editing original title-block text identities,
and writing drawing fields to Part/Assembly Parameters. Russian is the fifth UI
and default parametric-label language. Mass uses material density, document units,
and explicitly refreshed component snapshots; missing data is not reported as zero.
See [Drawings](doc/DRAWINGS.md), [Sketcher](doc/SKETCHER.md),
[Localization](doc/LOCALIZATION.md), and [Physical properties](doc/PHYSICAL_PROPERTIES.md).

## Agreed next steps for Part (2026-09-06)

**Scope correction (2026-09-19):** native Sketcher offsets are implemented; see
[Sketcher offset](doc/SKETCH_OFFSET.md). The user requires offsets from ZIMA
geometry and explicitly excludes STEP-derived offsets from remaining work.
Current work is user-reported Part bug fixes. Do not schedule offset
implementation again based on the historical sequence below.

**Current sheet-metal checkpoint (2026-09-18):** separate Unbend and Bend Back
history operations now cover cylindrical/conical sheet regions, attached planar
material, and material edits between states. Original geometry and shared
container placement remain unchanged. See the [state-operation implementation
and verification status](doc/SHEET_STATE_DEVELOPMENT.md). The old per-profile
state toggle is removed. Closed 360-degree regions still need an explicit seam;
bending an existing flat sheet along a line is a separate future command.
The user requested documentation, commit and push after verification. The general
Part order below remains unchanged after this sheet-metal work.

**Earlier sheet reference issue:** the trajectory/cross-branch behavior exposed
by `Projects/03.prtz` is superseded by derived state geometry with immutable
original references. Source features continue to evaluate forward in history;
their authored placement is not rewritten by state operations. See
[Sheet Metal](doc/SHEET_METAL.md#reference-study-2026-09-16).

Briefly remind the user of this plan when development next resumes. This is a work
order, not authorization to implement every feature immediately. Newer user
instructions take precedence.

On 2026-09-15 the user placed completion of program updates before resuming
this Part sequence, after the current Holes preview, diameter annotation and
View icon refinements. The user subsequently prioritized AI in the CAD console,
following ZIMA-CAD-Parts and using the active Part/Assembly/Drawing tab as context.
That integration is implemented in development build `2026091505`; authenticated
user acceptance is next. The later offset scope correction above supersedes
the earlier instruction to resume offset development.

1. **Inward/outward Sketcher offsets — implemented** for native Sketch curves
   with a specified distance. STEP-derived offsets are not required (2026-09-19).
2. **Complete and stabilize every Extrusion mode**: creation, editing, calculation,
   downstream references, and rejection of invalid input without damaging the model.
3. **Part Mirror**: mirror geometry about a plane as an editable history operation.
4. **Part Pattern**: linear/circular patterns with count, spacing, or angle.
5. **Individual face colors and GUI tool**: selection, color assignment, and Part persistence.
6. **Part and Assembly Sections** with shared interaction, primarily for drawings:
   definition, view direction, hatching, and selected sectioned components.
7. **Save as mirrored Part**: select a planar Part face as the mirror plane and
   choose dependent or independent geometry. The result is a new Part. A dependent
   copy stores its source reference and updates on explicit Regenerate; an
   independent copy has no such link.
8. **Comprehensive Undo/Redo verification and completion only afterward**, once
   agreed features are broadly implemented. Preserve reversible transactions
   throughout, but do not start a separate comprehensive Undo/Redo audit now.

This currently defines the scope of the general Part modeler. Add no other major
features without a new agreement. Multibody Part and Boolean operations were
completed as the agreed architectural direction on 2026-09-07:
[Body-owned histories and operations between branches](doc/MULTIBODY_AND_BOOLEANS.md).
The multibody model is connected to Part UI, Sketcher, and cross-coordinate-system
references, including editing a Part inside a nested Assembly. Boolean is a separate
Tree step (Union / Difference / Intersection); bodies do not have add/subtract
properties. Verified fixes include Body-Origin offsets, Sweep/Loft axes and end
references, and tangent contacts in Sketcher. [Save As](doc/DOCUMENT_COPY.md) copies
the model and linked drawings while leaving the original open.

Whole-body patterns, Assembly Booleans, and moving bodies between Parts are future
extensions. The Assembly Boolean agreement (2026-09-08) says that a component consumed
as an operation tool, such as B in A - B, must not remain a separate BOM item or mass
contribution. This role must be explicit and persisted on the operation; hiding the
component is not a substitute. Suppressing/removing the operation must restore the
tool's original participation. These discussions do not change the work order above:
the later 2026-09-19 scope correction marks native offsets implemented and
excludes STEP-derived offsets. STEP import/export reliability remains a separate
coverage-review area.

### Addition: purple dimension grips (2026-09-06)

Agreed future control for a selected dimension: a **purple point at an arrow tip
only where editing is possible**. Fixed or constraint-blocked sides have no grip.
If both sides can move, both grips may appear. Availability must follow constraints
and solver capabilities, not only dimension type.

- Dragging a purple grip changes the dimension value and corresponding geometry;
  dragging the label continues to change annotation placement only.
- A linear dimension's grip is at the arrow, not the extension-line start. Motion
  projects along the measured direction. The dragged side is the preferred moving
  side, while other constraints remain respected.
- A radius grip lies at the rim arrow and changes the radius radially.
- Diameter grips may appear at both rim arrows, only where editing is possible.
  Dragging changes the entire circle's diameter, not one half independently.
  Preserve the center when constraints permit.
- Apply the same rule to other dimension types; the precise angular-grip path
  still needs design work.
- Invalid solutions must not damage the model. Clarify the interaction with
  locked/reference dimensions during implementation design; this agreement does
  not introduce automatic unlocking.

This records a requirement, not an implemented feature. Its exact development
priority is not yet assigned; the comprehensive Undo/Redo audit remains after
completion of the agreed features.

## 1. Core and Parametric Container Model

**Status: In progress**

- document and container hierarchy
- feature history
- local coordinate systems
- parametric primitive solids
- container references and attachments
- in-view parameter editing
- persistent ZIMA-CAD document formats
- six-degree-of-freedom container placement with independent positional and
  rotational references, editable RX/RY/RZ corrections and optional local
  Front/Top mapping
- independent work-plane/profile offset for Plane, Sketch, Protrusion and
  Revolve, without moving the owning container origin
- automatic locked feature axes for circular protrusions, cylinders, cones and
  spheres, exposed through the common Axes visibility control
- unified native model display modes with topology-aware boundary, sharp,
  tangent, seam and view-dependent silhouette handling
- model-owned safe relations that write evaluated results into ordinary user
  parameters; the first system values expose volume, area, material density
  and mass without executing arbitrary Python
- configurable `START_PART.prtz` template used for every newly created Part

## 2. Sketcher

**Status: In progress**

- implemented foundation:
  - interactive points, segments, rectangles, circles and construction geometry
  - centre/start/end arcs up to an intentionally open full circle and editable
    spline geometry
  - selectable, draggable and editable linear, angular, radius and diameter
    dimensions
  - driving dimensions retain their values in the parametric solver regardless
    of their UI lock state
  - geometric constraints for coincident, horizontal, vertical, parallel,
    perpendicular, tangent, midpoint and point-pair symmetry about a
    construction line
  - one user-facing **Equal** constraint, interpreted as equal length for
    segments and equal radius for fillet arcs; existing conflicting dimensions
    are retained as reference dimensions
  - in-view constraint markers, including `=`, `M`, `T`, `H`, `V` and `∥`
  - circle rim snapping to existing points and line geometry
  - interactive two-edge sketch fillets with radius dimensions and equal-radius
    groups
  - reversible fillet suppression: dragging a fillet back to its sharp corner
    hides it while retaining its radius, dimension and constraint data for
    restoration
  - unified dimension hover, selection, dragging, context menus, deletion and
    value editing in both Select and Dimension modes
  - sketch dimensions shown when a sketch is opened by double-clicking it in
    the 3D view
  - point-on-arc and point-on-circle relations, coincident endpoints and
    tangent line/circle and line/arc workflows
  - sketch references from model edges, faces, arcs, cylinders and datum or
    generated axes, with selectable and deletable in-view representation
  - Ctrl multi-selection in the tree and sketch view, rectangle selection and
    mirrored geometry with persistent point-pair symmetry constraints
  - Czech **Totožnost** naming for point identity and the same green relation
    symbol in every Sketch constraint-list row as in the 3D view
- remaining work:
  - DXF import into the active Sketch with unit, layer and geometry mapping
  - DXF export of Sketch geometry with preserved real dimensions
  - direct manipulation (dragging) of sketch points and geometry with the left
    mouse button, including live constraint solving during the drag
  - intelligent entity input with inference, snapping and predictable
    continuation between consecutively created entities
  - one central cancel state machine shared by `Esc` and the toolbar, plus
    right-button cycling of explicitly previewed valid inference variants
  - one central confirm action shared by view-focused `Enter` and a short
    middle click, without stealing Enter from active value/text editors
  - finish the basic geometry set with polygons and ellipse support, and
    further stabilize arc and spline editing; Trim is deliberately deferred
  - improve coverage, numerical stability, diagnostics and recovery of the
    parametric constraint solver
  - make every constraint independently selectable in the tree and view;
    selecting it must reveal all participating geometry and dependencies, and
    Delete must remove the relation without damaging unrelated geometry
  - complete consistent constraint-symbol placement, hover, selection and
    dependency highlighting for coincident, tangent, equal, midpoint,
    symmetry and future relation types
  - consolidate constraints into independently identified records and solve
    constraints and driving dimensions as one equation/residual system
  - preserve the solution nearest to the previous valid sketch state to avoid
    unexpected branch flips
  - under-, fully- and over-constrained state visualization
  - automatic grey reference dimensions for remaining degrees of freedom;
    each geometric constraint or driving dimension should remove the
    corresponding reference dimension, in the style of Pro/ENGINEER
  - stable references between sketches and 3D geometry
  - stable semantic identities for external topology references
  - production-level recovery of invalid, redundant or conflicting relations
  - central ID remapping for topology-changing operations such as Trim

## 3. Protrusion / Extrusion

**Status: In progress**

- property-panel workflow with return to the originating editor context
- sketch and main container-origin selection from feature properties
- one-sided, two-sided and symmetric extrusion
- stable direction-defined Start and End faces; Flip swaps their meaning and
  their independent dimensions without relying on transient topology order
- additive and subtractive operations
- extrusion up to a selected face or plane
- profile validation
- independent profile-plane offset, including correctly offset in-view length
  dimensions, editable Sketch frame and RX/RY/RZ-corrected manipulator origin
- persistent purple extent handles with continuous drag and 1 mm displayed
  snapping, plus matching editable yellow length dimensions
- staged Apply/OK evaluation: cyan standalone preview remains live across
  direction and extent changes, while the final Boolean runs only on OK
- closed profiles produce ordinary solids and open profiles produce thin
  solids with one-side, other-side or symmetric wall thickness
- persisted thin topology roles (Inside/Outside and Start/End) participate in
  semantic ancestry and recover references when changing between thin and
  ordinary solid results
- Solid, Thin and Surface results share the existing Part Properties window;
  Surface supports open and closed profiles without caps or material volume
- fixed yellow Surface display, a shared visibility toggle, and Drawing exclusion;
  technological threads remain outside this filter ([contract](doc/SURFACE_PROFILES.md))

## 4. Revolve

**Status: Basic implementation**

- full and partial revolution
- first sketch construction line used as the revolution axis
- additive and subtractive operations
- shared line, arc and spline profile builder with Protrusion
- independent profile-plane offset used by both geometry and angular dimensions
- revolved thin features
- staged Apply/OK evaluation equivalent to Protrusion, including persistent
  cyan preview for forward, reverse and two-sided angle changes
- stable Start/End direction semantics and positive displayed angles; negative
  in-view input flips direction
- 360-degree initial one-sided proposal and independent remembered 45-degree
  proposals for newly selected two-sided and symmetric modes
- the same Solid/Thin/Surface profile contract as Protrusion

## 5. Sweep

**Status: Partially implemented**

The C++ application provides Sweep/Loft along a polyline or interpolating
3D curve, profile stations and correspondence. With corner rounding disabled,
straight segments have independent perpendicular caps and no corner transition;
their overlapping volumes are united. Original cap references remain available
for subsequent drill-point operations. See [3D Curve and Sweep/Loft](doc/3D_CURVE_AND_SWEEP.md).
This is distinct from the planned multi-body document architecture.

Further sweep scope:

- profile sweep along a path
- profile orientation control
- guide curves
- additive and subtractive operations
- parametric helix path and sweep along a helix for springs
- a dedicated spring-oriented definition designed around engineering inputs;
  do not reproduce the Pro/ENGINEER spring interaction model
- basic bent Pipe as a semantic 3D-Curve/Sweep feature with diameter, wall
  thickness, bend radius and stable connector frames

### 3D curve path foundation

- add a parent **3D Curve** container whose ordered children are ordinary
  Point containers
- each child Point keeps the standard Origin, X/Y/Z placement, stable ID and
  positional-reference workflow
- tree order, including **Insert here**, defines the curve point order
- moving, inserting, deleting or reordering a Point updates the displayed 3D
  curve and later supplies a path for Sweep
- begin with a polyline through the points; a spline interpolation mode may be
  added without introducing a separate 3D Sketcher

## 6. Blend / Loft

**Status: Partially implemented through Sweep/Loft; general Blend remains planned**

- transitions between multiple profiles
- guide curves
- section alignment
- continuity control

## 7. Advanced Part Modeling

**Status: Multi-edge Fillet and symmetric Chamfer implemented and inspected**

- multi-edge history Fillet with one shared radius, stable semantic edge
  references, unified creation/editing properties and last-valid-body recovery
- symmetric distance Chamfer with stable multi-edge references
- exact boundary-wire hover/selection and normal-section in-view dimensions;
  circular Fillet radius and Chamfer witnesses are anchored to both persisted
  rim circles
- two-distance and distance-plus-angle Chamfer modes (planned)
- shell and thickness
- holes and threads
- Hole as a semantic feature with through/blind, counterbore/countersink and
  drill-point definitions
- cosmetic thread metadata and Drawing representation; real helical BREP
  thread geometry is intentionally out of scope
- mirrors and patterns
- linear and circular semantic Patterns with stable per-occurrence identities,
  followed by curve/table-driven variants
- Part feature/body Mirror with semantic source provenance
- solid cutters can trim Surface results; Surface results cannot remove material
- surface modeling foundation: creation, trimming, extending, joining and
  inspection of surface bodies
- conversion workflows between suitable closed surface sets and solid bodies
- robust feature history and reference recovery
- additional Boolean and surface operations
- per-face color and appearance overrides stored by stable semantic face ID,
  with source-Part inheritance and optional Assembly instance overrides
- named non-destructive Part Sections for clipped inspection, reusable later by
  Drawing section views

## 8. Sheet Metal

**Status: Planned**

- base sheet creation
- bends and flanges
- hems
- corner treatment and bend relief
- sheet-metal cuts
- folded and unfolded states
- bend allowance, K-factor and bend tables
- flat-pattern DXF export

## 9. Assemblies

**Status: Functional foundation; stabilization in progress**

- `.asmz` document creation, loading and versioned saving
- Assembly application with part insertion from `.prtz`
- inserted components shown as separately selectable instances and expanded
  source-part trees
- automatic non-overlapping initial placement; first component marked fixed
- component properties derived from Part container properties
- three paired mate rows selected directly in the 3D view, with compatible
  planar/offset, concentric-axis and angular mate types, dynamic `mm`/degree
  values and Flip
- component, assembly and source-part datum/generated axes selectable for
  concentric mates
- stable nearest-pose orientation solving without branch flipping
- degree-of-freedom-aware mate choices and editable in-view mate dimensions,
  including visible zero plane and concentric constraints
- assembly origin and component-local origins represented in the assembly tree
- relative source-file paths for portable projects
- in-context activation and editing of a component while the assembly tree and
  assembly view remain active
- part tools available only when a component is active; inactive components
  remain selectable as external-reference sources
- assembly-only subtractive Protrusion and Revolve operations, optionally
  restricted to selected component instances
- uncut source-part geometry displayed while a component is active
- live component movement while placement values are edited
- synchronized saving of edited source parts from the assembly workflow
- dependent assembly scene invalidation after an open source Part changes
- per-tab camera preservation without automatic fit on tab switching
- source-document, evaluated-shape and persisted imported-STEP BREP caches
- separate component shapes collected in an OCCT compound instead of an
  assembly-wide Fuse
- component colors stored per instance rather than globally
- rename workflow updates component and external-reference file links
- named Assembly Sections cutting all or selected component instances without
  modifying their source Part geometry
- Assembly component Pattern with stable occurrence IDs
- dependent or independent **Create mirrored Part** documents inserted into
  Assembly as ordinary components; general reflection-matrix Assembly Mirror
  is deliberately deferred
- remaining work:
  - profile and optimize large-Assembly interaction: update only transforms of
    the dragged component and its dependent mate chain during manipulation,
    avoid full scene/tree/highlight rebuilds, improve Assembly opening and tab
    switching, and measure memory use with deeply nested Assemblies on a
    representative production model
  - DXF import from the Part and Assembly applications, with explicit creation
    and placement of a Sketch instead of treating DXF as a solid body
  - add diagnostics and recovery for invalid, conflicting and redundant mates
  - extend stable semantic topology IDs to the remaining unsupported geometry
    and dependency workflows
  - extend explicit tree selection to every supported reference type
  - add component grounding controls
  - insertion and nesting of subassemblies
  - degrees-of-freedom visualization
  - interference and collision checking
  - extend the implemented title-block BOM Repeat Region into a complete
    assembly BOM/table workflow with editing, sorting and production output

## 10. Drawing

**Status: Functional foundation; active development**

- `.drwz` document creation, loading, versioned saving and document tabs
- automatic same-name drawing creation/opening from Part and Assembly tree headers
- reverse navigation from a drawing to its source part or assembly
- persistent relative source-model link with document identity metadata
- multiple sheets with add/remove controls and a separate format per sheet
- fixed ISO sheet rules: A4 portrait; A3, A2, A1 and A0 landscape
- paper geometry in real millimetres, using a bottom-right origin with positive
  X to the left and positive Y upward
- black 2D workspace with a white paper-format outline and white geometry
- wheel zoom, middle-button pan, disabled rotation and animated Fit Sheet
- insertion and cursor placement of model and projected views, including eight
  45-degree projection directions under first- or third-angle projection rules
- selectable drawing views with left-button dragging, persistent projected
  alignment and Delete removal including dependent projected-view cleanup
- per-view Wireframe, Hidden Line, No Hidden, Shaded with Edges and Shaded
  modes; global model display buttons are disabled and unselected in Drawing
- optional auxiliary/smooth-edge display and grey hidden-line presentation
- native renderer-owned topology projection with curved silhouettes, visible
  edge precedence and component/model colors; shaded views use interpolated
  normals, a software Z-buffer and cached rasterization
- embedded View Properties panel with name, scale source, local scale and an
  optional movable 5 mm white caption showing name and `M1:1`-style scale
- basic Family Table instance selector in the drawing workspace
- first ISO-style yellow linear drawing dimensions with associative placement
- runtime regeneration from the live linked model when the drawing tab is
  activated; obsolete persisted 2D projection caches are intentionally not
  supported
- remaining work:
  - finish the Drawing workspace as a complete production workflow and verify
    it progressively on real Part and Assembly examples
  - complete hover and selection of real model points, edges and faces through
    drawing views and reuse it consistently in every drawing command
  - continue stabilizing curved silhouettes, coincident geometry and hidden
    line classification for multi-body parts and assemblies
  - bent/offset sections, Sketcher constraints for section lines and detail views
  - complete ISO dimensions, tolerances, datums, surface/feature symbols,
    position annotations, leaders, labels and editable annotation placement
  - extend the implemented ISO technical lettering and paper-space title-block
    text into the remaining drawing annotations
  - extend the implemented editable sheet-frame/title-block templates and
    parameter-driven fields with production zones and table workflows
  - complete assembly drawing BOM/table workflows beyond the implemented
    title-block Repeat Region, Item Number and Quantity fields
  - sheet-metal flat-pattern drawings
  - DXF export and production verification of the implemented PDF export


## Next-work clarification (2026-09-15)

First finish **Holes** for drilled hydraulic channels: owned-Sketch segments
specify cylinder axes and lengths, with one common diameter and subtraction only.
The historical request to complete **Sketcher offsets** for native and external
curves is superseded by the 2026-09-19 scope correction above. AI integration follows the
agreed modeling features; the comprehensive Undo/Redo audit order is unchanged.

### Approved work-plane unification (2026-09-15)

Sketch, Holes, Protrusion, Revolve, and construction Plane use an automatic base
plane from the first planar reference and persist manual XY/XZ/YZ selection.
Offset follows the selected plane; regeneration does not overwrite manual choice.
The user approved this scoped change to the shared placement rule. The former
offset follow-up is superseded by the 2026-09-19 scope correction above.
