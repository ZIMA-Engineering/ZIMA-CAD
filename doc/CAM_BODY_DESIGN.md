# CAM Body design and implementation

Status: agreed product direction; implementation requested after noncircular
Offset tangency on 2026-11-23. On that date the user explicitly approved the first
stage and native persistence scope below, including templates, affected factory
assets and verification on backed-up protected-document copies. The first-stage
authoring/calculation workflow is complete in the Windows 2026112503 source;
its finite acceptance and remaining extensions are recorded below.

## Document and setup

A CAM Body follows the existing special-purpose Body organization used by FEM.
Each setup references a stock Body and a desired result Body. Prefer selecting
another existing Body as stock rather than duplicating its authored definition.
One document can contain multiple setups and separate NC programs, for example
machining one side, turning the part over and machining the second side.

The calculated remaining material from a CAM setup can become the stock input
of a later CAM setup. Preserve that dependency explicitly and reject cycles.
Keep desired design geometry distinct from simulated remaining material.
Repositioning for another clamping must not change the source design Body.

Clamping locations are defined with Sketches on the initial stock. Fixtures,
including a vise, are explicit obstacle geometry. Collision checking must account
for those fixtures and the complete tool assembly, including its non-cutting
shank. A generated rapid transfer must not pass through the vise.

### Clamping boundaries and retained grip

On 2026-11-23 the user clarified two different constraints: the space extending
normally outward from a clamping Sketch is forbidden, while cutting into stock
below the upper Sketch (the top of the vise jaws) should produce a warning about
the clamping. Retain this distinction rather than forbidding the entire stock
below the jaw height.

Define the contact Sketch and its outward side explicitly; do not infer the
forbidden side from a global axis, camera direction or curve traversal order.
The bounded contact region extruded outward represents an exclusion volume.
Actual fixture Bodies remain authoritative additional obstacles, including the
vise base and other geometry that a contact Sketch does not describe. Changing
stock during simulation must never shrink those obstacles or turn their space
into machinable material. The upper Sketch defines a separate oriented height
boundary for that clamping, transformed into the current setup coordinates.

Separate the following results and retain their operation, block, tool and
clamping identities:

- Intersection of the cutter or any non-cutting tool region with a fixture or
  outward exclusion volume is a collision, including rapid moves. It prevents
  accepting the result as stock for a later setup.
- Actual cutting of remaining stock below the jaw-top boundary is a warning.
  Test the removed material, not only the tool-tip height: a move through an
  already empty cavity must not be reported as another loss of grip material.
- Removal at the authored contact region requires a distinct grip-loss warning.
  Geometric contact remaining after an operation is useful evidence, but is not
  proof of sufficient holding force, stiffness or resistance to cutting loads.

Do not choose a universal minimum grip depth. A protected material allowance or
minimum contact requirement, if provided, must be an explicit setup parameter.
Warnings must remain visible when a calculation otherwise succeeds; accepting a
warning must never suppress a collision. Neither a clear simulation nor positive
contact area certifies workholding without force, material and load information.

Verify both outward sides, translated/rotated work frames, holes in contact
regions, cutter/shank intersections, cutting above/on/below the upper boundary,
air cuts, repeated passes and multiple clampings. Save/reopen, Undo/Redo and
source edits must retain orientation and invalidate dependent calculated reports.
Use bounding rejection and the existing exact swept solids/material differences;
do not run a second independent sampled simulation for these checks.

The first-stage calculation implements these geometric checks and retains their
reports. The earlier unsupported-clamping gate has been replaced by verified
exclusion volumes, jaw-top warnings and contact-area accounting. Publication
rejects incomplete reports; collisions prevent downstream stock reuse. This is
not a clamping-force certification. See the
[2026-11-25 acceptance matrix](verification/20261125-cam-authoring-and-clamping.md).

Reference: Autodesk's [project geometry documentation](https://help.autodesk.com/cloudhelp/2025/ENU/CAMTP-Help/files/CAMTP_Help_5_ManagingtheProjectGeometry_html.html)
distinguishes stock removal from fixture collision testing against the complete
tool assembly. The Sketch-derived regions and grip warnings above are ZIMA-CAD
requirements, not a claim that Autodesk implements this exact scheme.

## Tools and machines

Initially provide internal, editable tool definitions with cutting conditions
and geometry; an external tool library can be added later. The user can create
new tools rather than being restricted to a fixed catalog.

For rotational tools, author a profile around the tool axis. The tool origin is
the boundary between cutting portion and non-cutting shank, as explicitly
confirmed by the user. The cutting profile extends to one side of that origin;
the shank profile revolves on the opposite side. This boundary expresses the
end of usable cutting length. Machine tool-tip coordinates and length offsets
must be derived explicitly rather than silently redefining this authored origin.

Each setup selects its operation and machine: milling or turning, with a stated
3-, 4- or 5-axis capability. Define supported combinations and their postprocessor
contracts before implementation. A tool shape alone does not establish a safe
machine envelope or valid controller dialect.

On 2026-11-23 the user confirmed that machine selection belongs in each CAM
setup and that machine-specific postprocessors can follow later. The initial
implementation should provide setup, a generic NC program, paths and simulation
with explicit machine capabilities. The user further clarified that generic NC
generation and postprocessing are separate responsibilities. The generic program
describes ordered operations, tool changes, cutting conditions and rapid/cutting
motions in defined work coordinates and units. Its blocks retain their mapping
to tool motion for editing and simulation in the NC pane.

A postprocessor translates that program into the selected controller's syntax
and conventions. Machine selection constrains axis capabilities and machining
operations before postprocessing; it is not merely an export option. Generic NC
generation must remain usable without an installed controller postprocessor.
Identify the generic program explicitly in the UI and export; do not label it
as executable code for a particular controller until its postprocessor exists.

## Paths, NC code and simulation

Generate toolpaths from the selected tool, operation, stock and target geometry.
Also allow manual path construction as ordered blocks: one cutting loop, a
rapid transfer, another loop, and subsequent operations. Preserve the difference
between cutting and non-cutting moves in the editable path model.

An NC pane beside the model View shows the actual generated program. Selecting
or stepping through its lines moves the displayed tool through the corresponding
path. Simulation includes material removal and collision inspection, and produces
remaining stock that a subsequent setup can reference.

## Implementation gates to define next

- Initial machine/postprocessor and operation subset, units, work coordinates,
  tool offsets and supported arcs/feed semantics.
- Exact ownership of stock, fixtures, tools, operations and generated results;
  invalidation when any source changes; explicit regeneration.
- Collision tolerances and swept-volume coverage for cutter, shank and holder;
  separate rapid-transfer and cutting checks.
- Native persistence scope, template/library impact and verification agreement
  before modifying the document format for CAM.
- Repeated editing, Undo/Redo, save/reopen, chained setups and independently
  checked path/material results. Generated programs require machine-specific
  acceptance; a visual animation is not that acceptance.

No external sidecar may become required document storage. Reuse the shared
placement, reference-entry, selection, localization and command lifecycle
contracts rather than adding CAM-specific alternatives.

## Proposed first implementation and native storage scope

The first working stage is 3-axis milling. Each CAM Body represents one setup,
selects its machine and work frame, references stock and target Bodies, and owns
ordered machining operations. Machine definitions state machining type, axes
and travel limits; turning and 4/5-axis generation remain explicitly unavailable
until their own motion models exist. Controller/postprocessor selection is a
separate optional setting.

Implement a generic NC program with defined units and absolute work coordinates,
tool/spindle/feed commands, linear rapid/cutting moves and planar circular moves.
The NC pane distinguishes this generic dialect from a postprocessed controller
program. Blocks and motions retain stable identities for selection and stepping.
Manual cutting loops and explicit transfers use the same program representation
as generated paths; preview and simulation consume that representation.

The native Part extension stores CAM setup definitions, machine and tool
definitions, stock/target/fixture references, clamping Sketch references,
operation parameters, authored path blocks, generic program data and calculated
remaining-stock results. Tool profiles distinguish cutting and non-cutting
regions across the agreed origin. Generated results retain their input identity
and become stale when an input changes. A later setup may reference a preceding
setup's remaining stock; dependency cycles are rejected. Missing sources preserve
the last valid work frame and are repairable, without permitting calculation from
incomplete inputs. No required sidecars or external caches are introduced.

Use the existing Body hierarchy and session transactions. Properties opening,
selection, inspection, tab switching and unchanged OK do not generate paths,
simulate material or calculate solids. Explicit generation/simulation owns its
calculation and publishes only a matching current result. View stepping moves
the tool overlay without regenerating source Bodies. Cancel discards drafts;
accepted definition changes form one Undo transaction.

Before releasing a format change, regenerate the Part and Assembly start
templates with the changed native runtime and verify actual GUI creation and
active editing context. Inventory and regenerate every affected factory native
asset, including FORM definitions and Part-based drawing assets; verify history,
role Bodies, references, insertion and dependent operations after reopening.
Update no format version by text replacement and add no legacy migration path.
Verify protected user documents only on backed-up copies, preserving originals
and excluding their content and identifiers from Git. Report unsupported inputs
and platform-specific gaps explicitly. The same committed library and shared
C++ implementation serve Windows and Linux.

Acceptance covers repeated setup/tool/operation editing, machine capability
validation, source loss/replacement, dependency invalidation, exact unchanged-OK
operation counts, Undo/Redo and native save/reopen; generic NC block/motion
mapping; independently checked path and remaining-stock examples; fixture,
shank and rapid-transfer collision cases; and chaining two setups. UI controls
reuse shared reference entry and in-application properties windows, with all
text in the five supported languages. A machine postprocessor is not a gate for
the generic program, but executable export for that controller remains absent
until its translation and acceptance checks are implemented.

## Initial calculation implementation, 2026-11-23

The first automatic operation is face milling down to the highest point of the
target, optionally retaining a positive axial allowance. It complements manual
linear/circular blocks; pockets, contours, turning and simultaneous rotary-axis
motion are not implied by this first operation. Face milling requires a tool
with a nonzero flat tip radius. The raster spacing cannot exceed its tip
diameter, and each depth increment cannot exceed the cutting length. The
complete authored profile, including shoulders and non-cutting regions, remains
part of material and collision calculation.

Generated transfers retract above the stock, target and fixture extents with an
explicit clearance. Every generated program still passes machine limits and
exact material/collision inspection before it can be accepted as a calculated
result. Fixture intersections do not authorize silently omitting a pass.

Generic NC positions describe the tool tip in millimetres internally; an inch
display changes serialization units only. The authored tool origin remains the
cutting/shank boundary. The mounting point is the axial end of the non-cutting
profile. Machine travel checks use that mounting point plus the declared work
offset, including complete circular-motion extrema. These limits are geometric
axis limits, not a controller-specific tool-change or acceleration model.

The calculation core constructs native swept solids for piecewise linear radial
profiles and straight or planar circular tip motion. Linear motion sweeps
monotone boundary strips; circular motion combines exact annular sectors with
endpoint profiles. It does not sample material removal at animation frames.
Boolean calculations use an explicit default linear tolerance of 0.0000001 mm.
Pure contact has zero intersection volume; penetration exceeding the cube of
that tolerance is reported. Fixture, target-gouge, rapid-stock and non-cutting
stock intersections remain distinguishable and retain the corresponding block
identity. Reported collisions do not remove material for rapid/non-cutting
motion, and a colliding result must not become accepted stock for another setup.

Native simulation owns private copies before validation, bounding-box inspection
or Boolean calculation, because OCCT helpers may update lazy topology flags.
The document publishes only a completed result matching all source inputs.
This section defines the implementation in progress; it is not evidence that
the CAM Body, persistence or user interface is complete.

## Native setup integration checkpoint, 2026-11-23

CAM Body persistence, metadata transactions, source freshness, chained remaining
stock and private explicit calculation now have native regression coverage.
Two setups using different work frames are verified through native save/reopen
and repeated calculation. See
[document and calculation verification](verification/20261123-cam-document-and-calculation.md)
for the exact evidence and remaining UI, template/library and platform gates.

The user explicitly reaffirmed machine parameters on 2026-11-23: travel capability,
maximum working feed, maximum spindle speed and rapid feed belong to machine
settings. Actual feed and spindle commands must remain within these limits.

## Setup UI checkpoint, 2026-11-24

The source-Part setup editor now integrates stock/target/fixture reference entry,
the shared work-frame controls and the machine editor. The user confirmed
translucent stock with opaque target and fixtures; this is a display-only CAM
override and preserves authored Modeling appearance. See
[setup UI and stock verification](verification/20261124-cam-setup-ui.md) for GUI
coverage, calculation counts and presentation checks. Tool/operation authoring,
NC stepping, clamping-Sketch checks and the agreed deployment gates remain open.

The user requested yellow cutting-feed paths and red rapid-transfer paths
(confirmed on 2026-11-24). The intended presentation uses solid yellow cutting
paths and dashed red rapid paths, with the
selected motion emphasized and linked to its generic NC block. Stepping should
show the tool at that motion's position. Derive all display paths from the actual
calculated program and preserve exact motion/block identity; drawing the paths
must not recalculate solids or become a separate source of machining geometry.

## Library definition checkpoint, 2026-11-24

Global settings contain a CAM section with independent paths for machine,
tool and workholding libraries. Generic native definitions and dedicated tool
templates are authored and verified; see the
[verification record](verification/20261124-cam-library-definitions.md).
The shared file chooser supports nested directories and lazy enumeration. Machine
and tool import copy explicit parameters into the setup without loading or
depending on the complete library. The initial tool adapter creates cylindrical
envelopes from explicit CAM dimensions; arbitrary modeled-solid conversion and
workholding-library placement/import remain separate extensions.

Tools use ordinary Part modeling and Body ownership through the agreed `.toolz`
extension and a dedicated start template. The cutting Body is yellow; the shank
Body is red. Their common origin is the cutting/shank interface, with cutting
geometry towards negative local Z and shank geometry towards positive Z.
Parameters and Relations drive geometry and manually authored cutting presets;
there is no automatic material database. Presets must identify their intended
conditions and sources. Machine profiles contain the established axis, travel,
feed, spindle and postprocessor parameters; a machine solid is optional graphics.

The first vise definition needs only two named contact Sketches in an ordinary
Body. Fixed-jaw contact lies at Y=0; moving-jaw contact lies at Y=opening. Both
upper jaw edges lie at Z=0. The opening parameter moves the second Sketch only;
stable role identities must not depend on editable Sketch names. A 3D vise model
is optional presentation. Whole-definition placement uses the existing shared
reference contract; the machining work origin remains independently selectable.
The vise definition remains available for ordinary native editing. Automatic
placement/import of that library definition into a CAM setup is not implemented;
the setup accepts existing source-Part clamping Sketches and fixture Bodies.

## First-stage workflow acceptance, 2026-11-25

Tool/profile, operation/block and clamping editors now integrate with the setup
draft. Explicit background calculation publishes exact remaining stock only for
unchanged current inputs. Generic NC inspection, stepping, final-stock display,
collision/grip reports and atomic text export use the actual compiled program.
Unchanged confirmation and inspection do not regenerate CAD geometry.

The approved nested CAM schema is 2; ordinary Part storage remains format 79.
Start templates and protected copies were checked, and the factory inventory
contains no CAM Body requiring conversion. Eleven native/UI test targets and
275 actual GUI assertions pass. See the
[verification record](verification/20261125-cam-authoring-and-clamping.md) and
[Windows release status](releases/2026112503.md#distribution-status).

The supported first stage is three-axis Facing and manual linear/XY circular
motion. Controller postprocessing, pocket/contour operations, turning, rotary
axes, workholding-library placement and arbitrary-solid tool conversion are not
included. Native Linux execution remains unverified.
