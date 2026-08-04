# ZIMA-CAD Roadmap

ZIMA-CAD is an open-source parametric 3D CAD system for designers and
mechanical engineers.

The roadmap describes the intended development direction. Its order may change
as the document model, geometric kernel integration and user workflows evolve.

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
- remaining work:
  - intelligent entity input with inference, snapping and predictable
    continuation between consecutively created entities
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
- additive and subtractive operations
- extrusion up to a selected face or plane
- profile validation
- independent profile-plane offset, including correctly offset in-view length
  dimensions

## 4. Revolve

**Status: Basic implementation**

- full and partial revolution
- first sketch construction line used as the revolution axis
- additive and subtractive operations
- shared line, arc and spline profile builder with Protrusion
- independent profile-plane offset used by both geometry and angular dimensions
- revolved thin features

## 5. Sweep

**Status: Planned**

- profile sweep along a path
- profile orientation control
- guide curves
- additive and subtractive operations

## 6. Blend / Loft

**Status: Planned**

- transitions between multiple profiles
- guide curves
- section alignment
- continuity control

## 7. Advanced Part Modeling

**Status: Initial Fillet implemented; remaining features planned**

- single-edge history Fillet selected after command activation, stored through
  a stable semantic edge reference and preserving the last valid body on error
- multi-edge fillets and chamfers
- shell and thickness
- holes and threads
- mirrors and patterns
- robust feature history and reference recovery
- additional Boolean and surface operations

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
- component colors stored per instance rather than globally
- rename workflow updates component and external-reference file links
- remaining work:
  - add diagnostics and recovery for invalid, conflicting and redundant mates
  - replace temporary face-index references with stable semantic topology IDs
  - extend explicit tree selection to every supported reference type
  - add component grounding controls
  - insertion and nesting of subassemblies
  - degrees-of-freedom visualization
  - interference and collision checking
  - bill of materials

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
  - complete hover and selection of real model points, edges and faces through
    drawing views and reuse it consistently in every drawing command
  - continue stabilizing curved silhouettes, coincident geometry and hidden
    line classification for multi-body parts and assemblies
  - sections and detail views
  - complete ISO dimensions, tolerances, datums, surface/feature symbols,
    position annotations, leaders, labels and editable annotation placement
  - implement ISO technical lettering/font support and paper-space text styles
  - finish sheet frames, zones, configurable title blocks/stamps and drawing
    format templates, including parameter-driven fields
  - assembly drawings and bills of materials
  - sheet-metal flat-pattern drawings
  - PDF and DXF export
