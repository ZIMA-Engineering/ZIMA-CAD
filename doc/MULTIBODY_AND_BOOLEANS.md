# Multibody Parts and Boolean operations

As of 2026-09-07, **multibody Parts are integrated and verified by module/GUI
tests**. Each body owns history, origin and placement. Tree, activation,
rollback, Sketcher and references use the owning body, including Parts edited
inside nested Assemblies. Union, difference and intersection are separate Part
history items. New Parts activate their first body.

Whole-body Mirror and linear/circular Pattern are implemented and can feed
Booleans. Assembly Boolean and moving bodies between Parts remain future work.
The chronological stages below record implementation history; this introduction
and final sections describe current state.

## Purpose and model

Inputs are independently modeled branches, such as mold stock and cavity tools,
each requiring its own additive/subtractive/editing history. Output is document
geometry from explicitly selected combinations. Means: multibody Part, parametric
Booleans and existing explicit OCCT calculation.

Typical uses include molds, impressions, punches, cavities and electrodes.
Channels are not the primary motivation; organizational groups can arrange them.
A group alone does not imply an independent geometric result.

## Part

The agreed model:

- Tree root is the filename, or working document name before saving.
- Document Origin and separate body containers sit below it.
- Each body owns a local Origin and modeling history using existing feature
  types, not merely one primitive/imported solid.
- Active body determines where new features are inserted. Activation is not a
  Boolean; creating a body does not automatically unite/subtract it.
- Features add/subtract/modify within one body; the whole branch result can
  become a later Boolean input.
- A Boolean is a history item with explicit inputs and operation type. Further
  bodies/operations may follow.
- Clicking the filename selects the complete document result. No additional
  unowned standalone result-body item is created. A Boolean still has a
  calculated output usable by later operations.
- A document may contain multiple disconnected bodies; whole-document selection
  is not automatic geometric union.
- Source branches remain editable. Source/result visibility must avoid confusing
  overlap without deleting source data.

Example tree:

```text
Mold.prtz                         ← current document result
├─ Document Origin
├─ Body: stock
│  ├─ Body Origin
│  └─ own feature history
├─ Body: cavity tool
│  ├─ Body Origin
│  ├─ Protrusion
│  ├─ further additive/subtractive feature
│  └─ Fillet
├─ Boolean: cavity                ← stock minus cavity tool
├─ Body: another tool
│  └─ own feature history
├─ Boolean: finish                ← cavity minus another tool
└─ Insert Here
```

## Agreed interaction

Boolean has a tree row at body level. Body Properties contains name, activation,
visibility and placement, without add/subtract mode. Boolean Properties contains
name, Union/Difference/Intersection and two explicit inputs: target/tool.
Difference means target minus tool. It consumes both available input results
and exposes a new output under its stable Boolean ID; source bodies/history
remain editable. A later Boolean may use that result plus a new body. Moving
bodies/Booleans validates dependency order before changing the document.

- Without an active body, the panel offers body creation/body-level operations.
- Activating a body offers modeling commands for its own history.
- Active bodies are green in the tree; activation is also in the context menu.
- Body level has its own Insert Here cursor and dependency-safe reordering.
- Properties uses the shared internal OK/Cancel dialog.
- Click the filename to select the document result.

Document/body Origins share the existing document-Origin display size; feature
Origins remain smaller. Reusing one branch would require an explicit usage list;
the initial implementation must not silently duplicate a consumed result.

## History, visibility and regeneration

Each body owns history and a cursor. Activation chooses the editing/insertion
owner; Show/Hide independently chooses visibility. Activating a hidden body does
not reveal it, and activating another body does not hide a visible sibling,
including later independent bodies. The current visible-context set is determined
from the complete body graph and stored visibility, without an active-body override.
Consumed Boolean inputs remain excluded from the document result; activating a
source does not implicitly reveal it. Explicit property rollback retains its
separate transient edit-boundary behavior.

Regression coverage checks both the visible-context graph and actual displayed
triangle ownership while switching between visible/hidden bodies. Activation and
visibility reuse the same calculated geometry and fingerprints. No new UI text
is introduced by this change; the existing localized activation and Show/Hide
actions remain in use.

Editing one body does not recalculate independent bodies in OCCT. Invalidation
follows actual references/Boolean inputs; later order alone is not dependency.
Explicit calculation refreshes changed branches and dependent outputs. Ordinary
display/activation use saved results.

## Mirror and Pattern sources

Mirror and linear/circular Pattern reference a source body/Boolean output and
own an Origin/placement. Copies do not duplicate modeling history; edit source
dimensions. Mirror creates an independent body-type result. A whole Pattern is
one result containing generated copies. Both can be hidden or used as Boolean
target/tool (e.g. multiple impressions cut from stock). Source stays separate;
Pattern count includes it.

With a Body active, these commands offer its own individual solid features.
At Part level, whole Bodies and solid features are both available. A subtractive
solid repeats its subtraction; the copy's history result replaces the input Body
instead of displaying positive cutter copies. Linear and circular modes share
this rule. Source dimensions and the Add/Subtract operation remain source-owned.

Linear Pattern uses local X/Y/Z and pitch. Circular Pattern uses axis, count and
angle or full-circle distribution. Switching to linear retains the axis.
Containers are editable; copies have no independent feature history. See
[Mirror and Pattern](MIRROR_AND_PATTERN.md) for interaction and Assemblies.

## Assembly and Drawing

Assembly/Drawing tree roots also use filenames. Assembly contains Origin,
components and Assembly operations; Drawing contains sheets/views and does not
gain Boolean modeling.

Future Assembly Booleans may combine exact placed Part/subassembly occurrences,
for example electrode shape design. Results belong to the Assembly and must
not unexpectedly overwrite component sources. Insertion remains an independent
component, not automatic union of all components.

Immediate owning Assembly retains placement authority; a parent cannot control
internal subassembly component placement. Stable instance paths distinguish
repeated occurrences. Dependency regeneration remains explicit, never hidden
in tab switches/tree refresh. Current source display sharing is governed by
[ASSEMBLY_GEOMETRY_SHARING.md](ASSEMBLY_GEOMETRY_SHARING.md).

## Import and references

A desired use is importing STEP or another `.prtz` as tooling and subtracting
it from another body. Linked versus independent copy, selecting bodies from a
multibody source and interaction were left for further design. Linked sources
must respect explicit Regenerate and cycle rejection.

Body/operation/dependency identities must be stable and persisted. Topology and
references use ZIMA ancestry, not OCCT face/edge enumeration. Downstream Boolean
output addressing must retain source topology ancestry.

## Implementation and verification gates

Before implementation, define ownership, input/output addressing, calculation
graph, rollback, visibility and serialization. Data-model changes are allowed;
legacy compatibility branches are not required. The user subsequently approved
body placement and document → body → container hierarchy while preserving
existing mate meaning.

Implement multibody Part/branch operations before Assembly. This did not
reschedule the wider roadmap or bring forward the comprehensive Undo/Redo audit.

Verify independent branches, chained Booleans, source edits, cycle rejection,
suppression, OK/Cancel, calculation-error recovery, save/load, disconnected
outputs and repeated Assembly occurrences. Mold tests need both modeled and
imported cavity tools. Sound architecture does not guarantee every geometric
operation; reject invalid/degenerate input without damaging the last result.

## Technical scope of the first change

The initial C++ audit identified:

- `PartDocument` had one history/order/cursor. Introduce stable Body ownership,
  per-body order/cursor and explicitly addressed inter-body outputs. Active
  body must use an ID.
- `DocumentSession` had a linear calculated-boundary vector. Partition cache by
  owner/dependency so editing another branch invokes no OCCT for unchanged bodies.
  Undo/Redo must retain graph and caches.
- `calculate_part` sent one operation sequence to the kernel. Separate local
  histories from Booleans and identify exact output dependencies. Disconnected
  results are aggregation, not implicit fuse.
- `resolve_constructions` and `construction_reference_geometry_for` primarily
  handled document and local Curve3D-point frames. Body introduces another level:
  document → body → container → nested point/Sketch, with no double transform.
- Stored references identify owner/source frame. Picking, highlighting and
  dimensions use the same transforms as explicit calculation, including cross-body
  references, independent Assembly ownership and stable instance paths.

**Approval to extend placement was granted in the follow-up discussion.**
It adds the body frame and cross-frame reference conversion, retaining FRONT/TOP,
Flip, offset and confirmation semantics.

Minimum verification: an identity-placed single body matches the prior model;
body translation/rotation applies exactly once; cross-frame references resolve;
editing B does not recalculate independent A; cycles reject; Cancel restores;
save/load retains geometry, activity, identities and references. Full UI/Assembly
integration follows only after this boundary passes.

## Calculation foundation

`HistoryOperation::body` identifies a branch and placement. A standalone Boolean
step owns ID/type/target/tool IDs. It consumes calculated inputs without an
auxiliary primitive or replaying tool history. One branch's operations occupy
a contiguous plan segment; disjoint reuse of branch ID or repeated feature owner
is invalid. Kernel validates dependency order, then evaluates each local history
through the existing history calculator.

Document `BodyResult` holds `body_boundaries` (local histories), `body_inputs`
(placed independent results) and `body_outputs` (bodies/standalone Booleans).
Local snapshots do not recursively contain document caches. Shared viewer packets
persist them; unchanged branches reuse fingerprints after loading without a
live OCCT cache.

Independent results form a compound, not a fuse. Boolean retains original
reference geometry from both inputs and creates no reference identity from
OCCT enumeration. Placement transforms viewer geometry, analytic face data and
edge-treatment directions together.

`zima_cpp_multibody_contract_tests` covers three Boolean volumes, chained cut,
empty result, invalid dependencies/owners, rotation/translation, original
references and cache serialization. Another case removes the source STEP after
first calculation, edits a second branch with a fresh kernel and requires the
unchanged import to reuse persisted output.

This foundation is connected to PartDocument ownership and Part UI. Per-body
cursors, cross-frame references, branch rollback and passive context display
without calculation are integrated. The outer boundary list cannot replace
body-local history; body editing must read `body_boundaries`.

### Ownership model and branch order

`BodyHistoryGraph` owns body/Boolean order, active ID, document insertion position
and per-branch cursors. Every history item has one owner. Ordering/property
changes validate on a copy before commit. Explicit dependencies/Boolean inputs
must precede dependent branches. Activation/cursors/context selection call no OCCT.

It builds a calculation plan from feature-compiler local operations and persists
ownership. Tests connect it to an OCCT two-body cut. `visible_context` offers
available earlier branch outputs and the active body, without redisplaying
already-consumed Boolean sources.

**PartDocument stores the graph directly in `.prtz`**, validating complete
ownership/order before calculation. `set_body_history` maps branch order into
the common tree; active-body insertion updates only that body's history.
`DocumentSession` retains the graph transactionally and returns the edited
feature's real local input from its branch cache. Resolver converts references
among document/body/container frames. Tests cover document-origin point, own
body Origin, earlier-body reference and analytic-face conversion without
mutating source snapshots. At this stage passive context, tree and remaining
reference audit were pending; later sections record their completion. Body
Properties must reuse `PropertiesSubWindow::ensure_origin_selection_button`
and the same Origin action wiring as other modeling containers.

### Native persistence and edit boundaries

This stage introduced Part INI **15** (internal JSON 41) and mandatory
`Document.body_history`; old files are not migrated. Low-level document tests
may use an empty branch graph with ungrouped history; normal new-Part UI creates/
activates the first body. That current state still writes the mandatory field;
it is not a legacy adapter.

In grouped documents, every feature, standalone Sketch and construction owns
exactly one branch. Missing owners/order mismatches reject. A body's first
feature cannot implicitly subtract another body's output; inter-body Booleans
have explicit target/tool.

`calculated_body_boundary` and `rollback_boundary` select only local-history
input. The first feature in body 2 sees empty input; its second sees its first
result/original references. Undo/Redo restores graph and calculated state together.

Saving must not replace the final document output with an earlier snapshot
merely because the last feature is suppressed. Document output includes all
branch caches. Tests verify save/load with a suppressed last feature, then
rollback without OCCT.

### Revised agreement: standalone Boolean (2026-09-07)

Combination settings were removed from document `BodyHistory`. `BodyBoolean`
owns identity, name, type, target, tool and visibility. Shared order contains
both body and operation IDs. Validation rejects identical/future inputs and
reuse of consumed outputs. Invalid insertion, editing/reordering reject
atomically. Native files persist both registries and order without old-model
conversion.

Future Assembly Boolean uses analogous immediate-Assembly ownership and must
not edit component sources. It is not yet integrated in UI. Basic standalone
Part tree and Body/Boolean Properties were already connected at this stage.

### Initial Part UI integration (2026-09-07)

The panel offers Create Body and Boolean. Active bodies expose modeling commands;
without one, grouped Parts retain body-level tools. Part/Assembly/Drawing root
shows filename. Part groups existing feature rows by owner and adds body Origins,
standalone Booleans and document/body cursors. Context menu supports Body/Part
activation and Insert Before/After.

`BodyPropertiesDialog` uses shared SubWindow, OK/Cancel, Origin, name, activity,
visibility and placement/rotation. Boolean creation/editing shares one dialog
with type and two available inputs. New rows remain transient until OK; Cancel
restores tree/View. The first body in a live ungrouped document adopts existing
history; this is not legacy-file support.

`DocumentSession::body_context_mesh` consumes persisted results only. Active-body
local boundaries transform into document coordinates; earlier available outputs
are context. Boolean editing shows inputs before its boundary. GUI checks cover
tree grouping, Cancel, MMB-confirmed Body edit, Boolean and native save.

Later stages below add Sketcher/modeling interaction and cross-frame selection,
including Parts activated inside Assemblies.

### Reordering history in the tree

Bodies and Booleans share sibling drag ordering. Offered boundaries validate
Boolean inputs, saved dependencies and cross-body feature references, including
Sketches/target faces. Dragging calls no OCCT and does not mutate the document.
Release commits one transaction; Esc cancels. Calculation reuses unchanged
branch caches.

Features move only within the active body, updating local order and its document
projection. Moving into another branch or moving a subtraction to first position
is forbidden. Both levels own a draggable Insert Here marker. Cursor changes
use persisted geometry without calculation.

### Reference-based body placement

Bodies store the same `Placement` definition as containers: references, offsets,
absolute angles, orientation corrections and solved position. Properties shares
`ContainerPlacementSection` and Tree/View selection, including the whole document
Origin. Sources may be the document or original objects in earlier bodies;
self/later references are rejected. Placement dependencies constrain reordering.

Solving proceeds body by body, passing newly solved Origins to later branches.
Children receive their own Origin in body-local coordinates. Preview changes a
temporary document without OCCT; OK calculates and commits. Tests cover document
Origin conversion into a rotated body, source Origin edits, repeated solving,
self/cycle rejection and definition persistence. GUI tests enter Origin/offset
and confirm.

Open Body Properties displays nonzero available coordinate, reference-offset,
absolute-angle/correction dimensions. Dimension editing updates the same dialog
fields, accepting decimal comma/dot; Enter does not confirm Properties. Locked
coordinates are not offered. Closing removes transient dimensions. Parameter
ownership is distinct from reference eligibility: editing a foreign feature's
dimension must not overwrite the body's field. GUI tests cover offset/correction
transfer and cleanup. Assembly-activated Parts were integrated in later stages.

### Sketcher in body coordinates

Sketch display transforms geometry from owning-body to document coordinates.
Input rays reverse the chain: scene → Part occurrence → body → Sketch. Ownership
uses Sketch/container ID; temporary Sweep profiles may use the active body.
Display leaves local 2D data/solved Sketch frames unchanged. Drawing previews use
the same point transform; normal view transforms body normal/X before Assembly
occurrence placement.

GUI tests use translated bodies rotated 90° about Y/Z, checking existing Sketch
position, mouse point creation/hover at the same screen position and persisted
local data. Picking/rendering call no OCCT. A complete subassembly-command audit
was still follow-up work at this stage.

Sketch Properties resolves references in owning-body coordinates. New Sketches
use active body, existing ones their actual owner. Plane, geometry, dimensions
and offset handle share one frame; Assembly occurrence placement follows it.
Input Sketch remains valid for reference collection and tracking its tree row.

A new Sketch preview row belongs under the body at its local boundary. The
multibody tree no longer creates the obsolete helper cursor that could be deleted
twice during edit refresh. GUI tests cover create/edit, body-Origin attachment,
rotated offset and preview removal on Cancel.

### Editing a body Sketch in a nested Assembly

Active-Part scene and point/dimension drag previews use persisted body history
boundaries, without new OCCT calls. Only the exact active occurrence path is
replaced in the top Assembly; all others, including repeated source Parts, remain
passive context. Body Origins are available in the active occurrence.

GUI tests open source documents, activate a Part through a rotated subassembly
and check Sketch position in a rotated body. They verify later-body suppression
only in the active occurrence, surrounding Assembly retention during point drag
and changed local coordinates saved to the source Part. This does not cover
all Body Properties or Assembly Sketcher commands.

### External references across bodies

Source picking and stored projection refresh transform original references from
document into target-body coordinates, then Sketcher projects into local 2D.
Owner, semantic key and occurrence path remain; shared source geometry is not
mutated. Assembly context references use the same transform after conversion
into the target Part.

Sketches offer original objects of earlier bodies and earlier features of their
own body. Later objects and Boolean result topology are not sources. Solving
also persists external Sketch cross-body dependencies. Body frame changes
participate in calculation convergence; explicit Regenerate saves changed frames,
Sketches and dependencies.

Tests cover first GUI selection of an earlier body's point into a rotated Sketch,
later-source rejection, save/regenerate. Calculation tests change both target-body
placement and source point, verifying new projection with unchanged source
identity/geometry.

### Expanded verification: chained Booleans, STEP and Sketch trim

Calculation regression changes tool-body placement before two chained Booleans,
after restoring persisted results into a fresh kernel. It checks intermediate/
final difference volumes, changes the last step to union/intersection and retains
an unchanged independent branch.

A mold case subtracts an imported STEP tool from stock. After removing the STEP
file and restoring saved data, it resizes stock and checks cavity volume plus
exact retained tool data. Unchanged tooling must not be reimported.

Trim candidates now transform through the body frame like other Sketch geometry,
both in Part and active Assembly occurrences. Previously they used only the
Sketch-local frame and missed placed-body geometry. GUI regression checks
candidate plane, common picking, one-piece preview removal and Escape without
saving pending trim to the source Part.

In Assembly Sketcher, disabling ordinary Select no longer disables the active
command's selection contract. Commands still offer their own candidate kinds,
as in standalone Part Sketcher. Trim regression uses actual clicks, not only
rendered-data inspection.

### New Parts and tree activation

A new Part from a start template creates/activates a fresh **Body 1**. Templates
store no body identity; every document allocates a new ID. Modeling commands and
local cursor are available immediately after New Document confirmation.

**Make Active** on the filename activates document level; on a body it activates
that body's history. Active level is green. After New Document closes, tree/
command availability refresh so Create Body is no longer left disabled by the
open-dialog state. Boolean still requires two available preceding results and
correctly stays unavailable with one body.

GUI tests use actual context menus/right-panel buttons: document/two-body
activation, Create Body after new Part/Boolean completion, and automatic first-body
activation.

Coincident Origins draw the highlighted axis last so another owner's axis cannot
cover it. Exact identity/path selection remains; paint order does not reorder
candidates.

New construction previews enter active-body history in the working document,
so solving includes the new feature/live dimensions. Confirmed history remains
unchanged until OK. GUI coverage includes a construction point, its dimension
edit and subsequent modeling in the default body. Invalid screen dimension
coordinates (e.g. unprojectable positions during camera changes) reject consistently
in painting, picking and editor placement, avoiding infinite-vector normalization
or Qt crashes.

Body-history solving may replace the working document. Extrusion/Revolution
profile previews therefore reacquire the Sketch by ID rather than reuse an old
iterator. OK validates draft profile ownership before replacing the document,
preventing invalid accesses during preview/confirmation.

Body Properties automatically arms the first free placement reference. Clicking
the document Origin fills all three planes without first selecting a table field.
Every shared placement numeric control, including newly created offset/angle
fields, uses `document_precision.decimal_places` instead of fixed nine-digit
precision. GUI tests cover new/edit body and two-digit precision after reference
selection.

Boolean is shown/enabled only at active Part root (filename). Active-body mode
hides/disables it. GUI tests activate root before creation and check absence
inside a body. Boolean Properties has no Origin button because it operates on
already-placed inputs; Body Properties retains Origin for actual placement.

Extrusion Origin context no longer invents a general axis along the Sketch
normal. Rectangular Extrusions do not offer a nonexistent feature axis when
placing a later container. Calculated profile axes and local-Origin axes remain.
GUI tests inspect a rectangular Extrusion while opening the next container.

Returning from an owned Extrusion/Revolution Sketch uses the boundary before
that feature, as ordinary editing does. A draft definition may exist before its
calculated boundary; display must not request that absent output and hide the
preceding solid. GUI regression retains the original box after Sketcher return
and while changing length before OK.

New Sweep/Loft and point previews register their container in active-body working
history. The same applies to new 3D Curves opening Point Properties. A helper
Sweep/Loft path is display-only in a separate local document and must not shadow
actual body-history path ownership. Point references solve in body/path frames,
then preview transforms back to the document. Actual GUI clicks select box
vertices for 3D Curve and Sweep/Loft in a translated body, checking stored
identity/coordinates and unchanged document on Cancel.

Create Body and Boolean are available only at active Part root; active bodies
offer their own modeling commands. Tree shows one Insert Here cursor, inside
the active body or among bodies at document level. Open Properties replaces it
with the pending item.

Placement references use an orthonormal system solution projected from original
position, preserving free coordinates. This removes drift along oblique faces
from repeated weighted normal-equation solving. Already-satisfied placement stays
unchanged, including regeneration convergence checks. Regression solves an offset
oblique plane 100 times. It also fixes failure to create a later body when an
existing container on a Sweep/Loft oblique end face blocked convergence.

### Origin visibility and references

Active Part root shows document Origin; active body shows only its own Origin.
Document Origin is 25% larger than body Origin; local container sizes are unchanged.
Body Properties may use document Origin. Containers inside bodies reject document
Origin through both View and Tree, using their body Origin or explicit references.
The Origin button can reveal other bodies'/containers' Origins. These changes
concern presentation/offered references, not frame calculation or saved mates.

### Display and placement fixes (2026-09-07)

- Primitive, construction and every Sweep Properties preview uses reference
  geometry in owning-body coordinates. Whole-body Origin selection locks solved
  coordinates and permits plane offsets.
- Sketcher automatically shows its owning container Origin instead of active-body
  Origin. This is display only; stored references remain.
- Extrusion/Revolution Origin preview adds no independent cyan helper axis.

### Future body transfer between Parts

The 2026-09-07 discussion proposed moving a body, its history and local Origin
into another Part while preserving position and converting references. Transfer
alone would not unite geometry; Boolean remains explicit. Removing an emptied
source Part would require resolving other documents' links. This is a proposal,
not an available command.

### Verification at this stage

Debug build, all 14 module tests, standalone window tests and complete GUI startup
contract passed. Focused GUI cases cover rotated-body offsets, container Origin
in Sketcher, cross-body references, nested Assembly activation and copying a
model with its Drawing. New regressions also check upper/lower common tangents
of fixed circles with C + T. See [document/Drawing copy](DOCUMENT_COPY.md),
[Sweep/Loft axes and end faces](3D_CURVE_AND_SWEEP.md) and [Sketcher](SKETCHER.md).

### Deletion at Part level

With the whole Part active (no active body), Body/Boolean/Mirror/Pattern context
menus offer **Delete**. Ordinary bodies delete their history, Sketches and
construction objects. Deleting a Boolean retains its input bodies and restores
them as available outputs. Mirror/Pattern deletes without removing the source.
Undo/Redo is supported.

If another Boolean, Mirror, Pattern or explicitly dependent body uses the node,
deletion rejects with the dependent object's name and leaves the document unchanged.
Remove/edit that dependent operation first. Other unresolved geometric references
follow ordinary history deletion: definitions remain for repair but stale calculated
shapes are hidden.

Deleting a Boolean redisplays its released inputs. Deleting Mirror/Pattern
redisplays source unless another operation consumes it. Undo restores previous
visibility too.

### Body-feature context menus

Individual feature context menus in Tree/View are available only inside their
active body. Activating another body or the whole Part does not make them editable.
The rule includes Sketches, dimensions, constraints and subfeatures; bulk menu
deletion excludes passive-body entities. The body's own menu remains available
for activation/Part-level operations.

Opening a Part or returning from Drawing via **Part** activates the first ordinary
body in history, skipping Booleans/Mirrors/Patterns. A Part without bodies stays
at document level. Automatic activation only changes working context: no geometry
calculation, Undo or dirty-state change. Normal tab switches retain manual activity.

## Default Origin references and Body Edit (2026-09-11)

New bodies have three placement references to Part Origin XY/XZ/YZ and two
orientation references: FRONT to XZ, TOP to XY. New modeled bodies start at zero
offsets/rotation. These are persisted relationships, not just equal numeric
coordinates. A new Part's first body follows the same rule using its newly
allocated Part ID; templates retain no permanent model-object IDs.

New STEP/IGES bodies and the new DXF body follow the same policy. STEP occurrences
with existing global transforms inherit corresponding offsets/corrections so
attachment does not move/rotate them. Mirror/Pattern retains its special dependent
ownership/placement. Loading saved bodies does not rewrite their references.

Ordinary Body context menus offer Properties and Edit. Edit shows nonzero body
placement/angle dimensions; double-click changes them using the same explicit
calculation as Properties OK. Zeroing a dimension hides it. Zero values,
references and corrections remain available in Properties to initiate movement.
Dimensions belong to the body, not its first feature; all features move together.

Implementation reuses Placement, references, solver and dimension rendering.
Only body creation policy assigns defaults; shared data format/container placement
contract is unchanged. All data remains in prtz/asmz/drwz.

Verification: Windows Release and all **49 CTest tests** passed (400.83 s).
Body tests check zero and translated/rotated frames after solving Part references
and save/load. UI opens Edit from context, changes offset 5 → 7 → 5 mm and angle
15 → 0°; zero dimension disappears. Resulting body-dimension screenshot was
visually checked.
