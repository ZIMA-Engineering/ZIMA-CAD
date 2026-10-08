# Sheet Form design

User agreement: 2026-10-07. This document records the intended behavior;
implementation and verification status must be recorded separately.

## Native library definition

A library definition is an ordinary native Part with ordinary editable Bodies.
The four role names are resolved when inserting or replacing the definition:

Role lookup is independent of Body creation order and Tree order. `FORM_CUT`
may be authored after `FORM`, for example when its cutting Sketch references
the already authored outer surface. Required names must remain distinct when
loading a library Part. Insertion stores the matching native Body IDs; later
renaming or reordering of the copied Bodies does not reassign their roles.
Ordinary reference dependencies still follow the Part history rules.

The factory Ventilation Window definition is authored with its cutting and
symbol Sketches in XY. Its outer shell and the separately oriented cutting
profile were rotated together, preserving source IDs, external supports,
constraints and the two R15 cut corners. The insertion frame derives from
the actual cutting Sketch; Body order and a global source-plane assumption
must not define that frame.

| Body | Definition |
| --- | --- |
| `FORM_CUT` | Cutting Sketch on the outer sheet surface; may support `FORM`. |
| `FORM` | Spatial forming geometry whose retained surface defines the outside. |
| `FORM_FLAT` | Optional cutting Sketch in the flat pattern. An empty Body means no precut. |
| `FORM_SYMBOL` | Sketch used as the Drawing symbol. |

The current `Projects/FORM.prtz` contains all four Bodies. `FORM_FLAT` is
intentionally empty. Roles must be stored explicitly in the inserted operation,
so later Body renaming does not change their meaning. Ordinary Part ownership,
history, visibility, activation and Sketch reference rules remain authoritative.

Use the agreed library location `config/lib/01-SHEETMETAL/01-FORM/` and native
file browser. Insert an independent editable copy of the definition and its
internal references into the destination `.prtz`; opening, regeneration and
export must not require the original library file or a geometry sidecar.

Global Settings now exposes the independently configurable `Paths/Forms` path,
with `lib/01-SHEETMETAL/01-FORM` relative to the factory configuration as its
default. The five-language shared Settings dialog, path persistence, Cancel and
preservation of the separate symbol path are verified on Windows. The Sheet Metal
FORM action and Tree use the `sheet-form` icon. Insertion and replacement use the
shared native file browser with a single `.prtz` filter.

The independently saved [Ventilation Window](../config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz)
now contains the user's four native Body roles and intentionally empty flat
role. Its document namespace and internal self-references were copied through
the native writer. No Drawing accompanies the library asset. The definition
reader validates unique roles, native ownership, active standalone Sketches
(including a general Feature in Sketch mode), a calculated outer surface and
self-contained references. Invalid or missing roles are rejected. A persisted
existing surface identity anchors the connected shell; traversal positions do
not define new topology identities.

Windows native tests passed for role validation, independent copy references,
cold regeneration with stable shell ancestry and all three 1 mm thickening
directions of the actual Ventilation Window (35.03 s). Independent checks compare
both resulting skins with source surface points offset along their normals on
every patch. The one-sided modes also retain all original skin vertices.
Native geometry and persistence now have an initial Windows regression for
insertion into a planar sheet, one connected valid solid, an empty flat-cut
variant, restoration of the authored shape, independent embedded definition
namespaces and native save/reopen. Stored Body IDs retain the roles after renaming.
The shared internal Properties dialog supports insertion and later editing,
independent definition replacement, native face entry and rotation about the
selected sheet normal. The authored definition remains editable as an ordinary
library Part; the inserted independent copy can be replaced in FORM Properties.
The initial native regression passed in 31.94 s; this includes multiple
calculations and independent B-Rep and volume checks, not one user action.

## Placement and thickness

The first implementation targets one planar Flat. Forming across a Flat/Bend
corner remains deferred. Align the definition planes with the selected outer
sheet side, with the fixed forming direction pointing into the sheet. The
defined outer surface stays in place; the sheet thickness grows inward.

Form Properties now presents a feature-specific placement form: the sheet face,
two positioning references with signed offsets, and one in-plane angle. It
uses the shared reference-entry items, entry indicators and independent inspection
eyes. The generic Origin, FRONT/TOP, flip and numeric XYZ controls are not shown.
Creation and later editing use the same internal Properties dialog.

Face entry consumes the common confirmed face-hit point and fixes the sheet
side with zero offset. New positioning rows remain empty until explicitly
selected by the user; replacing the support retains already entered references.
Either positioning row accepts a straight
segment, point, plane or planar face. A plane contributes its ordinary signed
distance; a segment contributes the signed distance to its projection in the
sheet plane, with the distance normal defined by sheet normal cross segment
direction. A point contributes the zero-angle sheet-frame X coordinate in row
2 or Z coordinate in row 3, plus that row's offset. The same point can therefore
drive both independent coordinates. Parallel/dependent constraints, curved
segments and missing sources are rejected without replacing the calculated frame.
Each position edit preserves the support side and normal. The angle rotates the
definition about that normal without changing its insertion point.

The preview axis and the ordinary result axis follow the local-Y support normal.
The result axis has no endpoint grip points. Point/text visibility controls sit
at the left edge below the definition and angle fields; the replacement action
shares the definition row. Existing translation keys cover all five languages.

Local Windows build 2026100801 passed the Form GUI, native geometry and
five-language translation suites (106.77 s total). Actual GUI confirmation at
-370 degrees measured 1.401 s, including result publication. The GUI matrix
checks manual coordinates, support selection/replacement, incomplete-entry OK
rejection, successive angles, offsets, result-axis direction, absence of axis
endpoint grips, resizing, Cancel, unchanged OK, replacement, Undo/Redo and native
save/reopen. Separate native probes at -370 and -10 degrees completed with valid
connected solids. The user's unusually long calculation was not reproduced on
this synthetic sheet; its original unsaved input is unavailable. These results
do not establish timings for arbitrary sheets or definitions. Linux verification
and portable-release acceptance for this build remain separate.

The placement equations belong only to Form. Persisted references retain their
original source identities, offsets and side flags in the existing native schema;
the shared general placement solver is unchanged. The ordinary native FRONT
solution supplies the exact side-aware orientation, with its free absolute
local-Y angle. Preview and confirmation consume the same Body-local resolver;
hover, click and cycling retain the viewer's common candidate list. No OCCT
calculation occurs during reference entry, offset editing or rotation.
During explicit reference regeneration, FORM consumes the current support's
native thickness metadata and updates its inward offset. Three successive sheet
thickness changes, actual skin metadata and thickness Undo/Redo passed.

The command is localized as **Form** in English, **Tváření** in Czech,
**Umformen** in German, **Formage** in French and **Формовка** in Russian.
The literal native role names `FORM_CUT`, `FORM`, `FORM_FLAT` and `FORM_SYMBOL`
and user-authored names remain unchanged.

The Ventilation Window's first `FORM_CUT` Sketch now rounds its two corners at
the 20 mm end with R15, matching the two quarter-circle guide corners of the
outer Form surface. This is distinct from its separate R3 spatial edge treatment.
The original 100 by 20 mm dimensions, four design points and segment identities
remain intact. The same native edit was applied to the user's `Projects/FORM.prtz`;
its previous contents were preserved as `FORM-before-cut-radius-20261007.prtz`.

The feature-specific placement extension passed eight focused Windows suites.
Native equations cover both sheet sides, signed offsets from points, straight
segments, planes and planar faces, rejected dependent lines and persisted signed
zero. The actual GUI checks successive reference/offset/angle edits, removing and
restoring both positioning rows, exact native preview/confirmation agreement,
unchanged OK, rollback, replacement, Cancel, resizing, Undo/Redo and save/reopen.
All five translated placement controls and tooltips were checked. The current
native schema and general placement implementation are unchanged. Linux execution
and arbitrary curved-sheet insertion remain outside this verification scope.

## Interactive performance requirement

The user clarified on 2026-10-07 that FORM is a symbolic manufacturing
representation. Its appearance matters; it is not a forming-process simulation
or a prediction of the exact machine-produced surface. The target is about 1 s
for an ordinary confirmed insertion, with 2 s as the maximum acceptable time.
Selection, rotation and draft display must remain immediate and must not invoke
the solid kernel. Unchanged OK must create neither calculation nor Undo entry.

The agreed input is the complete calculated, connected outer shell, not a
replay of the modeling history that created it. Its normal offset closes the
open rims into an idealized constant-thickness volume. Actual forming can thin
the sheet; FORM deliberately does not simulate that process. The main purpose
is recognizable manufacturing geometry in the model and Drawing. Dimensionable
edges and tool identification are secondary and must not make insertion slow.
User-created definitions remain ordinary native Sketch/Body models.

FORM uses the shared offset builder with a command-specific symbolic policy:
normal offsets, connected solid structure, positive volume and native ancestry
remain checked, while expensive all-pairs interference certification is not
required for this manufacturing indication. Ordinary Surface Thicken retains
its strict interference check and all existing precision requirements.
Sheet Cut does not project its normal-wall calculation onto symbolic FORM
skins. The actual FORM_CUT and optional FORM_FLAT Sketches remain independent.

Calculated surface snapshots retain opaque BRep object addresses alongside
already authored face, edge and vertex references. Addresses only locate those
identities inside that exact archive; they are never topology IDs. A matching
native history fingerprint permits explicit FORM calculation to read the
existing shell directly. Changes invalidate reuse. Both the authored history
and its disposable calculated snapshot stay inside the native Part file.

The first exact implementation does not meet this target: the actual Ventilation
Window measured 8.72 s for insertion, 8.03 s for flattening and 3.49 s for shape
restoration on Windows Release; definition preparation was 0.007 s. These are
initial observations on one model, not an accepted performance result.
The current surface-snapshot and symbolic-validation path measured 0.97 s for
cold insertion and 1.24 s after reopening the native embedded definition.
Flattening measured 0.64 s and restoration measured 1.02 s. These Windows Release observations concern the
actual Ventilation Window and include preparation of geometry, exact volume
properties, viewer data and native ancestry; they are not a universal timing
guarantee. The complete GUI insertion, from OK through calculation, commit and
scene publication, measured 1.95 s on this fixture. Repeated immutable embedded
definition parsing previously made it 4.62 s. A bounded, weakly owned parsed-source
cache now reuses that same native definition without retaining mutable Part
instances. Changing the payload or role metadata invalidates/rejects reuse.
The first packaged candidate exposed an additional unnecessary calculation:
the native Body-local reference resolver expressed a displayed +180 degree Euler
angle as -180 degrees only after preparing the first result. Cold GUI confirmation
reached 2.112 s, so that candidate was not published. FORM now consumes that same
resolver and its cached native input boundary before explicit kernel evaluation.
This command-local preparation preserves reference sides and the shared placement
contract; it does not normalize angles or signed zero globally. Initial local
measurements fell to 1.159 and 1.176 s with one kernel/result preparation instead
of two. Correctly rotated GUI insertion subsequently measured 1.183 s. Packaged
GUI acceptance is recorded with the final release. The signed Windows package
measured 1.586 s, passed the two-second gate, and prepared exactly one body/result.

The FORM rotation control uses the shared absolute local-Y angle that remains
free with a FRONT face reference. The local-Y correction is ignored by that
ordinary reference solution and must not be used as the FORM angle. The preview
also resolves the pending definition through its actual native Body-local input
frame, matching confirmation at Euler singularities. Native checks compare
0, 30 and -70 degree rotations on both sheet sides against independent Rodrigues
equations. GUI checks exercise 30, -40 and 15 degrees, fixed position and normal,
and every preview curve sample against the confirmed native frame. These changes
consume existing placement APIs without changing their shared implementation.
The former 4.91 s restoration
included repeated projection work for sheet-state-wrapped FORM references;
the same FORM-specific UV guide path now recognizes their persisted ancestry.

FORM edge guides consume existing UV p-curves and owning-wire orientation,
instead of repeatedly projecting every sample and two probes onto an entire
spline. Missing or degenerate UV data falls back to the established resolver.
Mesh deviation, display points, exact source curves and identity are retained.
Independent BRep/volume checks passed. A diagnostic comparison with the old
projection resolver passed for side directions within 0.001 in unit-vector
distance; offset splines can have slightly different nearest-point parameters.
The stricter tangent-edge visibility comparison also passed, using the same
classification threshold as the drawing renderer. The diagnostic comparison
runs the old resolver additionally and is excluded from performance timings.
The native geometry regression also passed after a Sheet Cut, verified that
symbolic FORM skins were excluded from its regions, and rejected stale surface
snapshot reuse after changing the authored shell parameters. The definition
regression passed separately. Evidence is recorded in
`build/form-diagnostic/form-latest-tests.log` and
`build/form-diagnostic/form-latest-timing-tests.log`; these are disposable local
verification logs, not required document data. Focused Windows GUI checks passed
for creation, native hover/click identity, planar rotation, independent inspection,
rollback, unchanged OK, definition replacement, Cancel, Undo/Redo and save/reopen.
The extended GUI matrix also passed actual reference-field clicking and a new
insertion point, plus ordinary Thicken Surface on all patches of the authored
Window shell (76.72 s for the complete matrix). The latter keeps the selected
shell's complete inspection wire and leaves no unconsumed surface patch.

Any simplification
must be explicit, preserve the independent authored definition and its stable
references, and leave ordinary modeling accuracy unchanged. Native Sketch/Body
authoring of user-created FORM definitions remains required.

## Replacement and dependent results

FORM Properties allows replacing the copied definition with another library
Part. Both definitions use the common definition planes. Keep the inserted
feature ID, attachment, selected sheet side, insertion position and rotation.
Replace the copied geometry and role definitions atomically. Validate later
references against the replacement; never present absent geometry as resolved.
An unavailable source retains its provenance and explicit broken-reference
state under the ordinary dependency contract. Replacement Undo/Redo restores
the corresponding resolved/broken states; this native regression passed.
Cancel preserves the previous complete definition.

`FORM_CUT` defines the spatial cut. `FORM_FLAT` defines only the optional flat
cut; an empty definition performs no precut. The spatial forming result is
suppressed in the flat pattern. `FORM_SYMBOL` supplies the Drawing symbol and
must remain distinct from cutting geometry. Add a FORM command and its semantic
icon to sheet tools and the ordinary feature Tree.

The Part viewer also displays the evaluated symbol curves on unfolded material.
These are state-owned display overlays, not placement-reference owners or cuts.
Returning to the spatial state removes the overlays. Drawing Show/Erase continues
to own Drawing symbol visibility independently, without duplicate Part overlays.
Changing the embedded symbol Sketch invalidates its native history fingerprint.

Feature Pattern and Mirror copy the forming operands, material frame and native
ancestry together. Unbend/Bend Back replay each instance's spatial and flat tools;
nested copies resolve the original FORM definition without replacing source IDs.
Reflection carries the symbol's handedness through the reflected material frame.
Properties uses the shared reference controls in a compact four-column table.
The populated required support remains replaceable and inspectable without a
clear/entry indicator. Preview and confirmed axes use a 10 mm nominal length and
do not introduce endpoint grips.

Drawing Show/Erase offers the evaluated native `FORM_SYMBOL` Sketch as one
manufacturing symbol, retaining a stable identity derived from the inserted FORM
feature. The adapter preserves authored curves and text in a standalone annotation
copy; external supports and dimensions remain source editing context. Symbols use
the ordinary nominal paper-size and readable-orientation rules. Native material-
state frame mathematics carries their attachment through Unbend/Bend Back without
OCCT. Body and exact nested occurrence transforms are applied at the existing
Drawing boundary. The source definition and its references remain unchanged.
Native checks passed for Show/Erase, rotated frames, visibility retention, Drawing
annotation persistence, suppression, two Assembly occurrences and Part save/reopen.
They also passed a real nonempty `FORM_FLAT` replacement: its 4 x 4 mm precut removes
16 mm3 from a 1 mm sheet only in the flat state; Undo restores the spatial skin.

Verification is focused, not universal. Insertion across a bend, unsupported
offset shells, recursive symbols inside FORM_SYMBOL, and direct editing of the
embedded definition are not established GUI workflows. Linux build and GUI
acceptance must be performed on the Linux host. Windows 2026100703 is signed,
published and independently verified; see [the release record](releases/2026100703.md).
Windows 2026100704 independently verifies the focused placement extension,
translated saved reference labels, two R15 cut corners and all eleven packaged
GUI cases. Complete packaged confirmation measured 1.365 s; production trust,
public bytes and update discovery passed. See [the current release record](releases/2026100704.md).

## Prerequisites agreed in the same discussion

- A valid subtractive feature that misses the body remains a valid feature
  with no material change. Its Sketch and generated tool must still be valid.
- A feature whose explicit calculation fails retains its definition, Sketch,
  parameters, reference identities and error in the native document. Mark it
  red in the Tree and display the preceding valid calculated geometry. Do not
  lose the definition by refusing to create its history container.
- Add Default immediately left of Origin in container placement. It performs
  the same whole-Origin selection as clicking the appropriate Tree Origin:
  the owning Body for a direct feature, the immediate owning container for a
  nested feature. Reuse existing entry behavior and localization.
- Retained-face conversion must hide retired automatic solid axes in passive
  Bodies as well as active ones. Editing FORM must keep the visible standalone
  Sketch in preceding FORM_CUT available as read-only reference context.
- Thicken Surface operates on the selected connected sewn shell, including
  all its patches, while preserving independent sheets and solids. Its
  Properties window starts at its natural content height.
