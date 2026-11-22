# Part FEM implementation design

**Basic FEM is implemented and published in Windows 2026112203.** See the
[current FEM status](FEM_STATUS.md) for delivered scope, verification and limits.
The release includes [steady-state thermal analysis and measurement
points](FEM_THERMAL_AND_MEASUREMENT.md). Heat power uses the existing Body-face
and owned-Sketch region workflow. Measurement points report displacement or
temperature without participating in meshing. The sections below retain the
original staged design; the linked current workflow defines delivered scope.
Thermal expansion is the current follow-up, not part of that release.

## Historical implementation milestones

Status: the first FEM Body/source GUI milestone was published in Windows
2026112101. The first Constraint/Force/Pressure authoring commands are implemented
for 2026112102; [their finite verification](verification/20261121-fem-conditions.md)
records coverage and remaining work. Windows 2026112201 includes native volume
meshing and the first Force/Constraint linear statics solver. The subsequent
[pressure calculation verification](verification/20261122-fem-pressure.md)
covers Body faces and owned planar or projected Sketch Surface regions.
The [result presentation follow-up](verification/20261122-fem-results.md) adds
quadratic-mesh validity checks and displacement/von Mises color maps.
The [agreed functional workflow](MODELING_FOLLOWUP_20261119.md#subsequent-fem-and-surface-modeling-design-2026-11-20)
defines the user-facing scope. This design specifies the first implementation
boundaries. The user approved the scoped native persistence extension on
2026-11-20: FEM Bodies, conditions, mesh and results inside `.prtz`, regeneration
and verification of affected start templates and factory assets, and protected-project
verification on isolated copies against the backup. Protected originals and the
shared container placement solver must remain unchanged. The new Create FEM
command authors a shared source reference only; it does not run an analysis.
See the [Body/source acceptance report](verification/20261121-projection-and-fem-body.md).
The first [native mesher checkpoint](verification/20261120-fem-mesher.md) passed
seven synthetic cases on Windows; it does not yet establish product acceptance.

## Original first-delivery plan (2026-11-20)

Implement source selection, condition authoring, explicit volume meshing and
inspection first. Follow with small-displacement, isotropic linear elastic
static analysis, displacement/stress/reaction inspection and deformation
animation. Static analysis is the proposed initial solver scope; thermal,
contact, plasticity and actual dynamics require separate analysis definitions.
Do not expose unfinished analysis kinds as working commands.

Gmsh is the agreed mesher. Evaluate 4.15.2 built against the same patched OCCT
8.0.1 as the product. Both operating systems use the same adapter and dependency
patch. A Windows probe is not evidence of Linux acceptance. The solver backend
and supported material range must be selected and verified separately.

Use tetrahedral volume elements for solids and sheet metal. Evaluate quadratic
10-node tetrahedra for the initial mechanical solver; linear 4-node tetrahedra
remain useful mesher/patch-test fixtures, not a substitute for a demonstrated
bending-accurate thin-solid formulation. Mesh the real thickness, with explicit
refinement and a convergence check. Never switch a sheet Body to a shell model
implicitly. Mesh quality alone does not establish solution accuracy.

## Ownership and native representation

Extend the existing Part Body hierarchy with an explicitly typed FEM Body.
Reuse Body activation, identity, order, visibility and context Properties.
Do not represent the FEM source as a copied Body, BodyLink or Boolean operand.

The FEM definition references one preceding material Body or BodyBoolean by
stable identity, in the same Part. It owns its analysis settings, condition
containers, mesh controls and calculated analysis data. The source owns its
history and calculated geometry. FEM must contribute no material operation,
mass, volume or assembly occurrence to the CAD result. A FEM Body is not a
valid material input to Boolean, Mirror or Scale. Cyclic and forward source
dependencies are rejected through the history dependency rules.

Runtime access shares immutable calculated `BodyResult` data from
`body_outputs`. Source selection does not replay its history. Native save stores
the CAD source once; the analysis stores its source identity and fingerprints.
Mesher input may be a private transient geometry copy to prevent third-party
meshing caches from modifying the CAD source. This is not a second persisted Body.

The proposed native extension includes:

- The Body kind and typed FEM definition, source reference and analysis settings.
- Ordered conditions with stable identity, type, ordinary placement, region
  references, authored values/units and applicable owned Sketch identity.
- Global/local mesh controls and their geometric reference identities.
- Mesh nodes, element connectivity/order, region bindings and quality diagnostics.
- Results, quantity/unit metadata, solver provenance, input fingerprints and
  analysis diagnostics; future time states have explicit physical times.

Large mesh/result buffers use immutable shared ownership for Undo snapshots and
bounded native archive members, not per-node JSON or unbounded repeated copies.
All information required to reopen stays inside `.prtz`; no required `.msh`,
solver files, sidecars or external caches. Validate sizes, counts, finite values,
connectivity, region bindings and input fingerprints before publishing loaded
data. Disposable worker files are never the authoritative analysis storage.

Schema decisions must update Part templates and affected embedded native data
in Assembly, Drawing and factory library assets in the same change. Regenerate,
save and reopen affected assets with the changed native implementation. Do not
ship old/new model adapters or change only a version number. Preserve authored
histories, roles, references and geometry; verify the packaged committed assets.

The protected project is included in the proposed conversion assessment:
inventory its documents/dependencies, verify recoverable backups and evaluate
isolated copies. Do not replace its originals. Identify unsupported old formats
explicitly rather than silently converting them or claiming reopen coverage.

## Condition containers and geometric regions

One condition implementation and one internal Properties class serve Force,
Pressure and Constraint. Right-menu shortcuts select the initial dropdown type.
The region selector has exactly three geometric kinds: Point, Curve and Surface.
A region enclosed by a Sketch is a Surface, not a fourth kind. Bounded Projection
results are the intended partial-face input; a complete source face is an optional
shortcut. An empty Point region denotes the container origin. The first condition
authoring commands assign existing native geometry; direct owned-Sketch creation
inside their Properties remains a separate integration gate. No condition editor
may claim that an unbound region is already meshed or solved.

The user approved the native Part extension for owned Force and Constraint
Sketches on 2026-11-21. Their conditions share one region-entry workflow.
Point applies at the placed condition Origin. Curve applies along the projected
Sketch curves. Surface applies over the bounded projected region. Both Sketch
modes support an offset work plane and preserve the selected native targets
and projection ancestry. Force direction follows the placed condition normal.
The initial fixed-restraint workflow locks all three translations over the
assigned region. Existing partial restraints retain their authored directions.

The next meshing stage uses separate Mesh Settings and Generate Mesh actions.
Controls are stored in the FEM Body. Generation is explicit. Show the mesh in
the FEM viewport and allow switching between it and the source Body. This
checkpoint can regenerate the mesh after reopening instead of requiring a
sidecar or a persisted mesh. A results workspace belongs to the solver stage.

Development builds enable the native mesher with `ZIMA_ENABLE_FEM_MESHER=ON`
and the Gmsh 4.15.2 SDK built against the same patched OCCT 8.0.1 as CAD. Set
`ZIMA_GMSH_INCLUDE_DIR` and `ZIMA_GMSH_LIBRARY` to that SDK. On Windows set
`ZIMA_GMSH_RUNTIME` to its matching DLL. The pinned native probe documents
[preparing and building that SDK](../tools/verification/fem_gmsh/README.md).
Generation uses a private exact BRep copy. Existing material vertices, edges
and faces bind by archive identity. A point inside a single face or solid is
embedded at the placed Origin. Independent projected patches still require
partitioning the mesher input. A missing region prevents generation.
Each generated condition region retains its point, line or triangle elements
and their nodes. They must belong to the material tetrahedra. This prevents
a detached CAD surface with its own display mesh from accepting a volume
load. Conditions placed at the same Origin share the embedded mesh node.

The user confirmed these choices on 2026-11-21. Temperature belongs to a future
analysis extension. Flow is an analysis kind with its own solver and inlet,
outlet and wall conditions, rather than another mechanical load. Do not expose
these future choices as functional in the initial mechanical condition menu.
Reuse shared reference cells, internal-window helpers, OK/Cancel, middle-button
confirmation and the complete ordinary placement capability. Do not modify the
shared placement solver to specialize FEM. A required change to that protected
contract needs a separate concrete proposal and approval.

The container plane is the authoring frame, not necessarily the loaded surface.
An origin point, owned Sketch path or bounded projected surface region defines
the actual target. References use the existing owner, semantic key, occurrence
and oriented-side contracts. Preserve the last valid frame on reference loss,
show the broken dependency and prevent solving until repaired. Never substitute
a nearest face or node. Projection outputs can delimit regions, but an isolated
surface result is not itself the material source of a volume analysis.

Import the exact material topology into Gmsh. Bind each native vertex, edge and
face to its Gmsh entity explicitly. For projected partial-face regions, partition
the transient mesher input or embed the applicable lower-dimensional entities;
retain ancestry to the source region. Validate complete coverage, holes, seams,
side orientation and ambiguity. Gmsh numeric tags and mesh node numbers are
ephemeral; remeshing resolves CAD-owned conditions again.

Force magnitude means total force over its assigned region. Line load, traction
and pressure use their explicit density units. Assemble consistent equivalent
loads by integrating element shape functions over the actual assigned region;
do not divide equally by node count. Fixed-direction force follows the chosen
oriented container normal by default; pressure follows oriented local normals.
Validate resultant force, moment and equivalent work, including quadratic nodes.

A constraint selects restrained translation components in its local frame.
Solid tetrahedral nodes have three translation degrees of freedom, not six
independent beam-style translations/rotations. Partial restraints must be drawn
and labeled distinctly from a fixed support. Changing condition type retains
compatible references without reinterpreting a force value as pressure.

Use the Part's existing `physical_parameters`, `physical_parameter_units` and
`material_parameter_descriptions` as the material authority. Resolve Young's
modulus, Poisson's ratio and any required density through the shared unit system.
Missing or invalid properties are errors, not zero/default material values.
Use one documented coherent numerical unit system at the solver boundary;
convert coordinates, loads, material properties and results consistently.

## Calculation and invalidation lifecycle

| Action or change | CAD evaluation | Mesh state | Result state |
| --- | --- | --- | --- |
| Open/create FEM Properties, select source, preview | Read current source only | Unchanged draft | Unchanged draft |
| Unchanged OK or Cancel | None | Unchanged | Unchanged; no Undo step |
| Confirm changed source | None | Stale | Stale |
| Source explicitly regenerated | Existing CAD lifecycle | Stale if actual input changed | Stale |
| Change region topology or mesh controls | None | Stale | Stale |
| Change force magnitude, restraint values or material | None | Reuse if geometry/partition unchanged | Stale |
| Generate mesh | Decode current exact input; no CAD history replay | Explicit replacement on success | Stale |
| Calculate | None | Require valid matching mesh | Explicit replacement on success |
| Change FEM/Mesh/Results view, colors, scale or animation | None | Read only | Read only |
| Save with current data | Existing native Save contract | Persist | Persist; never solve |

A background job owns an immutable source/input snapshot and cancellation token.
Publish only if document identity, source calculation generation, region/control
fingerprint and analysis input fingerprint still match. Discard stale/cancelled
job output. Failures retain the previous accepted data with explicit validity
state; do not label a failed or stale mesh as current. Undo/Redo restores a
consistent definition and corresponding immutable analysis snapshots.

Gmsh has global model state. Initially serialize access through one dedicated
meshing worker and restore/finalize its session on failure. No parallel calls into
Gmsh from View painting, inspection or other document workers. Evaluate process
isolation if responsive cancellation cannot be guaranteed through the public API.

## View and command integration

Activating FEM shows the source geometry once without changing the source Body's
persisted visibility. FEM, Mesh and Results select presentation and command sets.
Geometry-plus-mesh is an overlay over the same source. Deactivation restores the
normal Part display. Conditions use owned overlays: force arrows, pressure arrows
and hatched restraint symbols, with stable identifier/value/unit labels.

Single click selects and synchronizes the Tree. Double click exposes parameters;
context Properties opens the shared editor. These actions must not remesh, solve
or modify the document. Preserve wire-only ordinary geometry selection.

Results show a side legend with quantity, units and numeric range. Distinguish
mesh quality from computed physics. Hold the range fixed during animation.
Static deformation playback interpolates displacement for display only, with an
explicit magnification factor. It is not physical time, and never changes CAD.
Use shared palette/localization paths for both themes and all five languages.

## Implementation boundaries

### First native definition checkpoint

The finite Windows checks and outstanding gates are recorded in
[the native definition verification](verification/20261120-fem-definition.md).

The initial implementation adds an explicit FEM alternative to `BodyHistory`.
Its native row has `kind: "fem"` and a required typed definition with a source
identity, analysis type and volume mesh controls. Ordinary material Body rows
retain their existing schema; the Part format discriminators are unchanged.
This is a new document entity, not a legacy-format adapter. Conditions and
mesh/result buffers are not implemented by this checkpoint.

FEM rows do not enter material compilation, available Boolean/Scale/Mirror
operands or material Origin geometry. Activation selects the calculated source
for presentation without changing its stored visibility. A missing source is
retained as a broken reference; a known forward source or another FEM is rejected.
Source replacement resolves only an explicit preceding material result.

The workspace draft/commit path uses `commit_metadata`, shares the immutable
calculated source and returns without a transaction for unchanged OK. Candidate
queries and source inspection do not invoke OCCT. A candidate must already have
a valid native solid result. GUI commands and all calculation jobs remain to be
integrated and verified; this checkpoint does not expose FEM as a finished tool.

`document_core` owns typed definitions and native persistence; `workspace` owns
transactions, source resolution and job publication. Add isolated FEM numerical
and meshing modules rather than coupling solver code to Qt dialogs. The OCCT
adapter decodes the existing kernel archive and exact bindings during explicit
meshing; it does not use viewer tessellation as material geometry. The viewer
consumes prepared mesh/result packets. UI owns presentation and draft input.

Existing paths reviewed: `BodyHistoryGraph` compilation/validation/activation,
`body_operations.cpp` metadata versus geometry commits, `BodyResult::body_outputs`
and kernel bindings, Part native serialization, and Part material storage.
Ordinary Body creation currently calculates CAD: the FEM transaction must use
the metadata path and retain current CAD snapshots. Do not globally suppress
normal Body calculation callbacks to obtain this behavior.

## Acceptance gates

1. Build the same Gmsh source/patch against OCCT 8.0.1. Exercise boxes, holes,
   fillets, thin and twisted volumes. Check positive sampled Jacobians, mesh
   volume, exact face bindings, source immutability and refinement. Expand to
   actual ZIMA native Bodies; a synthetic fixture is not full CAD acceptance.
2. Verify FEM creation/source replacement, Body activation and command filtering,
   no duplicated mass/volume, source sharing, selection/inspection and unchanged
   OK/Cancel counts. Test all five languages and Light/Dark/Light.
3. Exercise every condition/region type, orientations, holes, partial curved
   patches, reference loss/replacement, source edits, remeshing and Undo/Redo.
   Apply the full required Sketcher edit/drag/dimension matrix where affected.
4. Gate a solver on rigid-motion and constant-strain patch tests, analytical
   axial/cantilever cases, pressure/traction force and moment balance, reactions,
   convergence, invalid materials and underconstrained/singular rejection.
   Validate actual thin-solid bending; never infer it from a valid mesh alone.
5. Verify native save/reopen, stale data, cancelled/outdated jobs, missing inputs,
   templates, affected factory data and protected project copies. Record exact
   finite coverage and Linux gaps before a new release.

## Dependency sources

[Gmsh 4.15.2](https://gmsh.info/) and its
[public API manual](https://gmsh.info/doc/texinfo/) describe the supported meshing
interface. The pinned source archive for the initial probe is
`https://gmsh.info/src/gmsh-4.15.2-source.tgz`, SHA-256
`be3f66f225d27ba9fa014f07e83169285da8a051b0e8ab7103d88066b39bdd3e`.
Preserve its GPL-2.0-or-later license, stated linking exception and all enabled
contributed-library notices and corresponding source when distributing it.
This design does not add Gmsh to the already published Windows 2026112004 release.
