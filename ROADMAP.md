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

## Current status and work order (2026-10-20)

Use the [current capability and backlog index](doc/PROJECT_STATUS.md) for what
exists today, what is awaiting release acceptance and what remains planned.
**Family Table is implemented**: native Part/Assembly variants, one shared parent
file, parameter/presence overrides, Assembly insertion/Replace and Drawing variant
selection. See [Family Table](doc/FAMILY_TABLE.md); this is not a future feature.

Multibody Parts, native Sketch offsets and Trim, direct Sketch dragging,
2D/3D/helical Sweeps, model Sections, surface tools, sheet-metal Bend and
Unbend/Bend Back, import/export and document Undo/Redo also exist. Their focused
guides define supported scope and remaining cases. Older requests below are
retained as dated design context, not repeated implementation assignments.

The current correction batch is accepted in signed Windows
[2026102004](doc/releases/2026102004.md), including documentation and all five UI
languages. Further newly reported program defects retain priority. Preserve the
unfinished cast-wing/toothed-washer studies and the exclusion of two-part locking
nuts. The next agreed Feature work is ordered multiple Up To faces with signed
correction, followed by scoped surface projection. File Manager directory
prototypes still require discussion. FEM, voice/pointer context and direct mesh
editing are queued design work. See [the detailed queue](doc/FASTENER_REMAINING_WORK.md).
New native persisted or protected placement contracts require the agreed review.

## Earlier priorities and queued requests (2026-10-07)

### Shared operation activity indicator (requested 2026-10-07)

Status: local implementation and focused Windows checks completed; dependent
command verification continues. See [operation activity](doc/OPERATION_ACTIVITY.md).

- Provide one reusable visual activity indicator for explicit operations across
  workspaces, including longer modeling calculations. Show the actual operation
  name with a restrained animated indicator, using a consistent application style.
  The primary panel belongs at the center of the visible View, above Properties;
  shared confirmation also shows a compact indicator beside OK. This placement
  was explicitly requested after the initial status-row proposal.
- Delay its appearance for short operations to avoid flashing. End activity on
  success, failure and supported cancellation, including exception paths.
- Show percentages only when real measurable progress is available. Distinguish
  indeterminate activity from measured progress; never invent completion values.
- Reuse the existing status-operation presentation and lifecycle where suitable.
  Verify that animation remains responsive during long kernel calls: processing
  events before a synchronous calculation alone does not establish responsiveness.
  Trace document ownership and thread safety before moving calculations off the
  UI thread, and prevent conflicting document edits during a running operation.
- Preserve calculated output, reference identity, transaction boundaries and
  existing command behavior. Do not introduce calculations during passive display
  or make a cancellation control available without a safe cancellation path.
- Localize operation text in all five languages. Verify fast and long operations,
  failures, nested lifecycle calls and retirement of the indicator in the GUI.

### Container placement: Default Origin shortcut (agreed 2026-10-07)

Status: local implementation; Body equivalence, five-language and immediate
nested-owner GUI checks passed. See [Default Origin](doc/DEFAULT_ORIGIN.md).

- Add a **Default** button immediately to the left of **Origin** in container
  placement. One click performs the same action as clicking the applicable
  complete Origin in the Tree, without requiring another selection.
- A feature owned directly by a Body uses that Body's Origin. A nested feature
  uses its immediate owning container's Origin. Resolve the actual owner at
  each nesting level and retain its stable reference identity.
- Reuse the existing complete-Origin selection and reference-assignment path,
  including its position and orientation behavior. Preserve the existing
  placement solver, numeric corrections, preview and transaction semantics.
  The existing Origin button remains available for manual selection.
- On implementation, localize the button and tooltip in all five supported
  languages and verify equivalence to Tree selection, nested ownership,
  inspection, OK/Cancel, Undo/Redo and native save/reopen. Follow the protected
  shared-placement review requirements before changing that shared contract.

### Sketch circular tangency studies and rectangle symmetry

Windows 2026100611 is signed, published and verified. Its case studies cover
tangent Reference Profile trimming,
following contacts through radius edits and dragging, directly created arcs on
external supports, three-curve tangent chains and three mutual tangencies.
Automatic rectangle centring on an axis now uses corner-pair symmetry (S).
See [the case studies](doc/AI/SKETCH_TANGENCY_CASE_STUDIES.md) for the executed
matrix and [Windows 2026100611](doc/releases/2026100611.md) for release acceptance.

Continue extending these regression studies for newly reported constraint
combinations. Mixed larger native/external networks, ellipse/spline contacts,
internal arc tangency and additional dimension combinations remain verification
work; the current studies do not claim universal solver convergence. Linux
verification of this change belongs on the Linux host.

### Part direct modeling and basic FEM (requested 2026-10-06)

- Add direct modeling of imported STEP solid geometry within a Part. Define the
  supported editing operations and preserve ordinary Body ownership, history,
  reference identity, native persistence and Undo/Redo.
- Complete a basic FEM analysis workflow for Parts. Define the initial scope of
  materials, loads, boundary conditions, meshing and results before implementation;
  verify it against independently checked reference cases.

These are queued requests, not implemented capabilities. Detailed command and
analysis scope remains to be agreed.

Solid Straightening / Restore shape was completed and published in Windows build
2026100301; see [the implementation and verification](doc/SOLID_STRAIGHTENING.md).
The mixed Extrusion/Revolution side direction and rounding the second end of an
already rounded Sketch segment are fixed, verified and pushed in commits
`6ca431bd` and `68d5b01a` and included in subsequent portable Windows releases.
The unit audit was completed and published in Windows 2026100302; System Setup
and independent Sheet from Body are also implemented and published.
On 2026-10-06 the user confirmed completing Linux verification and publication
of the preceding changes. This is user-reported Linux acceptance; the Windows
verification records do not claim to have run Linux checks themselves.
The previously agreed modeling queue retains the
[Surface command suite](doc/NETWORK_SURFACE.md)
and optional Drawing view descriptions below; neither is superseded by the audit.
The file manager below is an additional TODO; recording it does not interrupt
the current implementation or claim that these capabilities already exist.

### Unit audit and corrections (approved 2026-10-03)

See [the audit and engineering assessment](doc/DOCUMENT_UNITS_AUDIT.md) for
implementation evidence, tolerance/precision boundaries and remaining gates.

- Audit all unit-dependent input and output: Sketch and Feature dimensions,
  placement/mates, measurements and physical properties, relations and Family,
  drawings, native Parts/Assemblies, and import/export boundaries.
- Preserve canonical native geometry and real size when changing document units
  or combining millimetre, centimetre, metre and inch documents in an Assembly.
  Audit angle and mass selections as well. Counts and dimensionless values must
  never be treated as lengths; drawing paper and explicitly fixed-unit API fields
  need their documented semantics preserved.
- Initial finding: file-settings changes retain calculated geometry, and physical
  properties and measurements already convert length units. Feature numeric fields
  originally ignored document units. The first correction now covers Feature
  parameters and selected 2D/3D/Helical Sweep parameters through one canonical
  input control; other command paths remain under audit. Keep mm/cm/m/in support.
- Repair confirmed discrepancies with shared conversion logic at presentation
  boundaries. Preserve precision, signed zero/side identity, reference identity,
  no-op confirmations, persistence and Undo/Redo. Do not change the protected
  container-placement contract without the required explicit review.
- Confirmed choice: keep one unit system per document. A later explicit unit
  conversion preserves real size and converts relevant Family presentation and
  relation expressions in one undoable transaction. Do not add a separate hidden
  unit basis to relations or automatically change a Part's units on insertion.
  Distinguish dimensional constants from dimensionless multipliers/counts and
  user-authored text. Reject a conversion atomically with a specific explanation
  when an expression cannot be converted reliably. Unit selection is not locked
  permanently after creation.
- Add representative tests for mixed-unit Assemblies, unit switching, editing
  displayed values and expressions, native save/reopen, and exchange-file units.
  Complete the five-language review and English documentation with the changes.
- Keep display decimals, physical manufacturing tolerances, kernel accuracy,
  mesh approximation and drafting conventions independent. Conversion must not
  silently alter manufacturing limit intervals, general tolerance rules or
  thread/fit designations. The detailed assessment is a proposed implementation
  design, not a claim of completed ISO/ASME support.

### Built-in first-run setup (approved 2026-10-03)

Implement after the unit audit and corrections. Use one wizard inside ZIMA-CAD,
shared by Windows and Linux, rather than a separate setup executable or script.
Offer it automatically only for a new installation that has not been configured;
persist completion or explicit skipping in shared settings outside version
directories. Updates must not redisplay it or reset existing configuration.
Expose the same wizard later through Tools > System Setup, initialized from current values.

The shared native implementation and Windows contract are described in
[System Setup](doc/SYSTEM_SETUP.md). Linux desktop acceptance remains separate.

Cover document unit presets, working directory, default native templates and
optional desktop integration. Reuse the existing registration service and its
explicit choices. Prepare clear mm/inch Part and Assembly template choices;
unit-suffixed template filenames are the proposed naming convention. Verify
template-versus-global-default precedence so the selected template cannot be
silently overridden with different units. Keep geometric storage canonical.
Review title blocks and drawing formats separately: document length units do
not by themselves select paper size, drafting convention or title-block content.
Localize the wizard in all five languages. Linux desktop acceptance belongs on
the Linux host.

### Surface creation and downstream operations (clarified 2026-10-04)

See [the implemented surface commands](doc/NETWORK_SURFACE.md). The native suite and command UI are implemented and published in signed Windows 2026100401; [release acceptance and regression limitations](doc/releases/2026100401.md) are recorded separately. The requirements below retain the agreed scope.
The implemented scope follows the user's clarification: owned ordinary Sketches
and 3D Curves, G0/G1/G2 filling, Sewing, shell Fillet, Intersection and Trim.

- Keep the owned-boundary general Boundary Surface plan, including its own
  Origin and ordinary Sketch / 3D Curve definitions. Add closed three-boundary
  patches; investigate valid two-boundary and N-sided perimeters using OCCT.
- Preserve the existing reference-based filling workflow as a separate Fill
  Surface command. Accept existing surface edges and retain explicit support
  faces and side identities. Continuity belongs to each boundary: G0, G1 and G2
  are supported by the current filling API. G3 is not required, as clarified
  by the user on the same date. Preserve the existing G0 case and never
  silently downgrade requested continuity.
- Add Surface Intersection for two selected native surface faces, producing
  reusable 3D intersection curves and isolated points where applicable. Retain
  all branches, both source parents and stable native identities. Use actual
  trimmed faces; distinguish disjoint and coincident cases without inventing
  geometry. Creating intersection curves does not modify the input surfaces.
- Add Surface Trim for a target face, trimming surface or suitable native curve,
  and an explicitly selected retained region. Allow direct surface-to-surface
  trimming without requiring a separate intersection feature. Preserve the
  underlying surface geometry, tool geometry, orientation, source ancestry and
  retained-region intent; expose real trim boundaries for subsequent operations.
- Add Sewing as a downstream history operation joining contiguous surface faces
  into a shell. Sewing establishes shared topology and preserves geometry; it
  does not automatically smooth the join or create material volume.
- Extend the existing Fillet integration to appropriate shared sharp edges of
  sewn shells. OCCT supports shell fillets without first creating a solid;
  verify actual input topology, feasible radii, surface ownership, reference
  ancestry and native persistence before claiming application support.
- Preserve rollback, unchanged OK, Cancel, regeneration and Undo/Redo. Consume
  the existing shared properties, placement and picking contracts, and apply
  the feature-performance and five-language localization gates.

### Grid command (deferred by the user, 2026-10-04)

The user requested a future Grid command (Czech working name: `Mřížka`), but
explicitly deferred it until the surface-operation suite above is complete.
Do not design or implement it during the current surface work.

The user subsequently clarified its intended behavior on the same date:

- Start from a selected existing surface and derive its boundary curves.
- Divide the boundary curves into points and construct an editable point grid
  for the surface. Moving its points lets the user freely reshape the surface.
- The user's interaction reference is Blender-style point editing: select and
  move points to shape the surface interactively. This clarifies the intended
  interaction without prescribing a polygon-mesh implementation or importing
  Blender's complete toolset.
- This is a surface-editing feature, not a display grid. The complete grid
  layout, interior points, density controls, point-to-surface relationship and
  boundary/continuity behavior remain to be settled at the deferred design stage.
  Do not assume that sampled points are B-spline control poles or that every
  edited point must be interpolated by the resulting surface.

### Optional Drawing view descriptions (requested 2026-10-02)

Implemented and verified on Windows following the user's priority choice on
2026-10-06; see [view descriptions](doc/DRAWING_VIEW_DESCRIPTIONS.md).
The requirements below retain the agreed scope.

- In View Properties, place Name below Source. Below Name, offer ordered optional
  description rows: the view name, scale with an adjacent visibility checkbox,
  and programmable text using the existing title-block parameter substitution
  mechanism. Support drawing/document numbers and combinations such as source
  filename, a literal dot and revision; reuse existing field semantics rather
  than inventing a second expression language.
- Give each displayed row independent visibility, text height and color. Use
  white as the default view-name color and green for additional information in
  the interactive drawing. Retain the existing ISO drawing font. Resolve print
  and export color behavior through the established output conventions.
- Stack enabled rows below the view in their displayed table order, keeping
  the description attached when the view moves. Preserve definitions in the
  native Drawing and support editing, Cancel, Undo/Redo and save/reopen.
- Include the descriptions in DXF output using the existing drawing text/export
  representation, so exported cutting layouts can carry identifying text.
  Verify content and placement in actual exported files, including parameter
  changes and independently hidden rows. Final row controls and parameter
  source scope remain design details to settle before implementation.
- Localize all controls and validation in all five languages.

### Manual bend-note library symbol (requested 2026-10-04)

Completed and published in Windows 2026100401; see
[the symbol user guide](doc/SYMBOLS_USER_GUIDE.md). The user chose
an ordinary symbol with manually authored text over a model-driven bend note.

- Add a simple native `.symz` text symbol under `config/symbols/sheetm`, using
  the existing Insert symbol, editable Text field and Drawing placement workflow.
  Example literal Czech content: `Ohyb 90° nahoru` or `Ohyb 90° dolů`.
- Insert it directly in the Drawing's flat-pattern view beside the bend line,
  with the existing optional leader where useful. No 3D annotation is required.
- The author enters and checks the complete text. Do not calculate the angle,
  infer the direction, reverse the wording when a view flips, or introduce
  special bend-note controls, mandatory value lists or validation.
- Reuse ordinary symbol ownership, editing, Cancel, Undo/Redo, native embedding
  and PDF/DXF output. A library folder does not introduce a separate renderer
  or a new placement contract. Review factory labels/text in all five languages
  when creating the asset; this backlog entry adds no runtime UI text.

Research references: Autodesk's
[bend-note editor](https://help.autodesk.com/cloudhelp/2022/ENU/Inventor-Help/files/GUID-1BBEF02A-DFC5-466F-871D-28E2C94C566F.htm)
describes editable text and direction/angle fields, and its
[flat-pattern documentation](https://help.autodesk.com/cloudhelp/2027/ENU/Inventor-Help/files/GUID-1B336854-4ED3-4C32-ADCB-E7A407485D80.htm)
defines direction relative to the viewed side. These are workflow examples,
not a claim of a mandatory drafting standard or authorization for automation.

### Sheet forming library (design clarified 2026-10-07)

- Start with forming in one planar sheet Flat. Forming across a Flat/Bend corner
  is deferred until a concrete part establishes the required geometry and
  flat-pattern mapping.
- Store library Parts in shared `config/lib/01-SHEETMETAL/01-FORM/`. Directory
  names and numeric prefixes determine category order; later mechanical-table
  categories and ZIMA-Parts integration are separate work.
- Author Bodies named `FORM_CUT`, `FORM`, `FORM_FLAT` and `FORM_SYMBOL`, sharing
  the established definition planes. `FORM_CUT` is the cutting Sketch and may
  support the spatial `FORM` definition. `FORM` supplies the outer forming
  surface. `FORM_FLAT` is the optional cutting Sketch for the flat pattern;
  an empty Body explicitly means no precut is needed. `FORM_SYMBOL` is the
  Drawing symbol Sketch. The user's current `Projects/FORM.prtz` has all four
  Bodies, including an intentionally empty `FORM_FLAT`.
- Place the tool's zero plane on the outer sheet surface, viewed from the tool
  toward the sheet. Preserve the selected thickness side explicitly. Build
  thickness inward from the outer forming surface. After fixing the insertion
  point and normal alignment, allow rotation only around the sheet normal.
- Reuse the existing file browser and insertion/placement paths where suitable.
  Copy the editable definition and required internal references into the target
  native Part with distinct stable identities; do not require an external link
  to the library file. Persist source roles and affected Flat references in the
  forming operation so Unbend does not depend on mutable Body names.
- Properties must allow replacement by a different library definition, using
  the common definition planes. Preserve feature identity, insertion position,
  sheet attachment/side and angle; validate dependencies against the new
  geometry. Include a semantic FORM command icon in the sheet tools and Tree.
- Use the inserted feature name as its tool designation. Suppress spatial
  forming in the flat pattern and retain the simplified sketch for drawing and
  DXF, on a configurable layer initially named `FORMING`. Real holes remain
  independent sheet cuts. Optional designation text must stay distinct from
  cutting geometry. Automatic CAM recognition requires validation with the
  particular receiving system; a layer name is not a universal machine protocol.
- Native insertion, independent replacement, sheet-side placement and rotation,
  inward shell offset, optional flat precut, Unbend/Bend Back and Drawing
  Show/Erase symbols are implemented with focused Windows verification.
  The Ventilation Window GUI insertion measured 1.93–1.95 s. A dedicated
  configurable FORMING layer/tool designation for sheet DXF, insertion across
  bends and direct embedded-definition editing remain follow-up work.
  See [Sheet FORM design](doc/SHEET_FORM_DESIGN.md) for measured scope and limits.

### Native ZIMA-CAD file manager

- Use the ZIMA-Parts Parts tab as a visual/workflow reference when implementing;
  inspect the actual application before claiming an exact visual match.
- Present the file manager in a separate window connected to ZIMA-CAD, as
  clarified by the user on 2026-10-06. Reuse the existing Working Directory
  and available CAD information. It is a simplified ZIMA-Parts-style browser
  focused on native ZIMA files, not integration of the entire ZIMA-Parts product.
  Use the available width for file and Parameter columns. Closing it preserves
  the modeling workspace, active document and panel state. Implementation is
  deferred until after the current Sketch fixes and sheet-forming work.
- Show and change the working directory. List supported native document and
  library types, including `.prtz`, `.asmz` and `.drwz`.
- Open a selected native file by double-clicking its row or using an Open button.
  Reuse the normal document-opening workflow, including activating an already
  open document rather than opening a duplicate, and normal error handling.
  Provide per-row checkboxes for selecting multiple files; Open opens all checked
  files when any are checked, otherwise the selected row. Double-click opens only
  the clicked file. Report individual failures without preventing other selected
  files from opening or discarding the user's selection.
- Show stored document Parameters as table columns alongside file information.
  Investigate reading only the necessary native metadata without opening editor
  tabs, loading body geometry or invoking OCCT/regeneration. Measure cold and
  repeated reads on representative directories before estimating performance.
  Populate rows promptly and load parameter values incrementally; any disposable
  metadata cache must invalidate when its source changes and remain optional.
- Put file-type visibility toggle buttons below the list. Users can independently
  enable Part, Assembly, Drawing and other supported native types.
- Put ordinary file actions, including Copy, Delete and Move, at the lower right.
  Preserve native document identities and dependency semantics; file operations
  must not silently break references or discard unsaved open-document changes.
- Later, offer both a flat list and a configurable tree grouped by document-number
  rules. Users define segment widths and literal separators rather than relying
  on one hard-coded numbering scheme. For example, `ZE0000-0000-0000` can group
  by `ZE0000`, then two two-digit groups, then another two two-digit groups;
  hyphens are separators, not additional hierarchy levels. Define behavior for
  nonmatching numbers without hiding files or changing their actual names.
- Support batch renaming by a user-defined rule, with a preview of old/new names
  and affected references. Update links inside dependent native documents so
  Assemblies retain their Parts/subassemblies and Drawings retain their sources.
  Reuse and extend the existing staged native rename implementation in
  `cpp/modules/workspace/src/file_rename_operations.cpp`; ordinary filesystem
  renaming alone is insufficient. Explicitly define the dependency-search scope
  and report unresolved/out-of-scope dependents rather than claiming global repair.
- Verify collisions, case-only changes, rename cycles, interrupted operations,
  rollback/recovery, relative paths, shared dependencies, open documents and cold
  reopen. Benchmark large tables without reducing the model's precision or
  recalculating stored Parameters as a side effect of browsing.
- Follow shared Qt/Fusion presentation, working-directory configuration and all
  five UI languages. This backlog change introduces no runtime UI strings.

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
- Status: implemented after the scoped approval of 2026-09-29; see
  [general surface placement](doc/GENERAL_SURFACE_PLACEMENT.md). Planes, cylinders
  and cones use analytic behavior; general surfaces use persisted finite mesh
  patches with the documented accuracy and nonlinear-reference limitations.
  Future changes to the protected shared placement contract still require
  explicit approval of the proposed design.

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

**Status: Implemented native foundation; continued verification and extensions**

- document and container hierarchy
- feature history
- local coordinate systems
- unified Point/Axis/Plane/Sketch/Extrusion-Revolution Features; the old six
  standalone primitive commands have been removed
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

**Status: Implemented parametric Sketcher; continuing solver and interaction studies**

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
- Additional implemented tools include polygons, ellipses/elliptical arcs,
  native text and symbols with solver anchors, live point dragging, Trim,
  native offsets and supported DXF import/export. See [Sketcher](doc/SKETCHER.md),
  [DXF/import](doc/IGES_DXF_IMPORT.md) and [offsets](doc/SKETCH_OFFSET.md).
- Remaining work is continued finite regression coverage for mixed constraints,
  successive dimension edits/drags, branch stability, conflict diagnostics,
  reference recovery and new user-reported combinations. Existing tools and
  stable identities are not a missing implementation; universal solver
  convergence is not claimed. See [current corrections](doc/PROJECT_STATUS.md).
  Additional inference/constraint feedback and optional automatic reference
  dimensions for remaining degrees of freedom remain follow-up ideas beyond
  the existing Sketcher foundation; scope them before implementation.

## 3. Protrusion / Extrusion

**Status: Implemented through the shared Feature command; extent extensions queued**

- property-panel workflow with return to the originating editor context
- sketch and main container-origin selection from feature properties
- one-sided, two-sided and symmetric extrusion
- stable logical Start and End faces; Swap sides reverses physical travel while
  retaining the original panels, dimensions, references and locks, independently
  of transient topology order; see the
  [2026-10-22 direction matrix](doc/verification/20261022-feature-side-reversal.md)
- additive and subtractive operations
- extrusion up to a selected face or plane
- profile validation
- independent profile-plane offset, including correctly offset in-view length
  dimensions, editable Sketch frame and RX/RY/RZ-corrected manipulator origin
- persistent purple extent handles with continuous drag and 1 mm displayed
  snapping, plus matching editable yellow length dimensions
- transient editing preview with the shared OK/Cancel contract; explicit OK
  calculates and commits, Cancel restores input and unchanged OK is a no-op
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

**Status: Implemented through the shared Feature command**

- full and partial revolution
- first sketch construction line used as the revolution axis
- additive and subtractive operations
- shared line, arc and spline profile builder with Protrusion
- independent profile-plane offset used by both geometry and angular dimensions
- revolved thin features
- shared transient preview and OK/Cancel evaluation equivalent to Extrusion,
  including forward, reverse and two-sided angle changes
- stable Start/End direction semantics and positive displayed angles; negative
  in-view input flips direction
- 360-degree initial one-sided proposal and independent remembered 45-degree
  proposals for newly selected two-sided and symmetric modes
- the same Solid/Thin/Surface profile contract as Protrusion

## 5. Sweep

**Status: 2D, 3D and Helical Sweep implemented; extensions remain**

The C++ application provides Sweep/Loft along a polyline or interpolating
3D curve, profile stations and correspondence. With corner rounding disabled,
straight segments have independent perpendicular caps and no corner transition;
their overlapping volumes are united. Original cap references remain available
for subsequent drill-point operations. See [3D Curve and Sweep/Loft](doc/3D_CURVE_AND_SWEEP.md).
Multibody Part ownership is also implemented; see [Bodies](doc/MULTIBODY_AND_BOOLEANS.md).
2D Sweep and Helical Sweep have their own implemented workflows; see
[2D Sweep](doc/SWEEP_2D.md) and [Helical Sweep](doc/HELICAL_SWEEP.md).

Further sweep scope:

- additional guide/orientation cases beyond the documented supported paths
- a dedicated spring-oriented definition designed around engineering inputs;
  do not reproduce the Pro/ENGINEER spring interaction model
- basic bent Pipe as a semantic 3D-Curve/Sweep feature with diameter, wall
  thickness, bend radius and stable connector frames

### 3D curve path foundation - implemented

Ordered native points, placement/reference entry, insertion/reordering and
Polyline/Spline paths already supply 3D Sweep. See [3D Curve](doc/3D_CURVE_AND_SWEEP.md).

## 6. Blend / Loft

**Status: Partially implemented through Sweep/Loft; general Blend remains planned**

- transitions between multiple profiles
- guide curves
- section alignment
- continuity control

## 7. Advanced Part Modeling

**Status: Fillet, all three Chamfer modes, Shell, holes/threads, Mirror/Pattern and surface tools implemented**

- multi-edge history Fillet with one shared radius, stable semantic edge
  references, unified creation/editing properties and last-valid-body recovery
- symmetric distance Chamfer with stable multi-edge references
- exact boundary-wire hover/selection and normal-section in-view dimensions;
  circular Fillet radius and Chamfer witnesses are anchored to both persisted
  rim circles
- two-distance and distance-plus-angle Chamfer modes, including editable grips;
  see [edge-treatment verification](doc/EDGE_TREATMENT_COMMANDS.md)
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

**Status: Implemented sheet modeling and supported unfolding/refolding; extensions remain**

Flat, Bend, revolved/twisted sheet, transitions, cuts and FORM operations exist.
Independent Unbend/Bend Back support documented cylindrical/conical regions,
attached material and edits between states. See [Sheet Metal](doc/SHEET_METAL.md)
and [state operations](doc/SHEET_STATE_DEVELOPMENT.md).

Remaining scope includes a seam for closed 360-degree regions, bending an
existing flat sheet along an arbitrary line, additional hems/reliefs and
manufacturing cases, and production flat-pattern interchange verification.
These follow-ups do not make the existing Sheet Metal application "planned".

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
  - extend real-project verification of the implemented STEP/IGES/DXF workflows
  - add diagnostics and recovery for invalid, conflicting and redundant mates
  - extend stable semantic topology IDs to the remaining unsupported geometry
    and dependency workflows
  - extend explicit tree selection to every supported reference type
  - add component grounding controls
  - additional deep/repeated-subassembly ownership and reference-recovery cases
  - degrees-of-freedom visualization
  - interference and collision checking
  - extend the implemented title-block BOM Repeat Region into a complete
    assembly BOM/table workflow with editing, sorting and production output

## 10. Drawing

**Status: Implemented technical Drawing workflow; production verification and extensions continue**

- native documents, multiple sheets, model sources and Family Table variants
- ordinary/projected views and documented section/detail/crop/break workflows
- view properties, scale, display modes and interactive depth-tested geometry
- associative measured dimensions and chains, model Show/Erase and text placement
- native frames/title blocks, parameter fields, BOM regions and balloons
- native symbols, technical text and PDF/DXF/PNG/JPEG exports
- exact occurrence/source/reference identities and document Undo/Redo

See [Drawings](doc/DRAWINGS.md), [the manual](doc/USER_MANUAL.md) and
[shared commands](doc/DRAWING_COMMANDS.md) for supported scope. Ordinary tab
display consumes current calculated model data; it must not regenerate bodies
or run precise vector hidden-line calculation. Explicit output prepares precise
print/vector geometry under the established contract.

Remaining work includes broader real-project occlusion/reference cases,
additional annotation and table workflows, manufacturing flat-pattern cases,
and production export verification. Current axis-cross contacts and arbitrary
curve balloon anchors are development corrections with their own finite
verification and release gates, not a missing Drawing foundation.

## Next-work clarification (2026-09-15)

Historical order, superseded by the current index: **Holes** is implemented for drilled hydraulic channels: owned-Sketch segments
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
