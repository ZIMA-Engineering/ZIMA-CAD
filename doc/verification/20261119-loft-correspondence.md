# Loft correspondence, route and sheet attachment follow-up

Status: targeted Windows verification passed; package/publication acceptance is
recorded separately with the immutable release artifacts.

Baseline: `9559490a34bb219b2313251cfb597b9b7605280f`, Windows native Release,
Qt 6.11.0 and OCCT 8.0.1. The implementation is shared C++ for Windows/Linux.
Windows 2026111813 was withheld after additional user findings. The replacement
build identity is 2026111901; publication requires the committed-source gates.

## Scope and operation ownership

- Sketch context conversion offers Twist in both View and Tree. Curve3D offers
  3D Sweep, preserving its authored trajectory and dependent Point identity.
- Point placement offers a whole native route immediately after its first
  eligible segment/axis candidate after all command-specific sorting. A 2D
  Sweep path reference resolves as authored Sketch geometry rather than a
  calculated material edge. Point Properties retain preceding Sweep material.
- 3D Sweep creation and ordinary feature inspection share the authored path and
  profile wire generator. Selection does not calculate a solid or recolor faces.
- Coincident parent datum strokes cannot erase the owned Loft working-plane
  cyan preview at zero offset.
- A failed/incomplete Loft may retain its structurally valid definition and
  diagnostic, with no invalid material. Unchanged OK remains a no-op.
- Loft point order reuses Sweep's native profile correspondence and editor.
  Explicit starts use native point IDs; equal numbers connect. Numbered side
  guides must contact the same numbered point at every station. Unmatched
  automatic profiles retain OCCT compatibility rather than an artificial
  equal-count requirement.
- Preceding profile points are available to an owned guide's placement and
  ordinary external-reference entry while the parent remains a draft. The
  command-local source packet is transient; persisted references and all
  required definitions live in the native Part. The protected shared container
  placement solver/reference contract is unchanged.

The parent owns the rollback scene and transient preview. Child Properties and
Sketcher edit only the pending owned Sketch; parent OK commits. Point ordering,
selection, inspection and reference entry consume native/viewer data, without
OCCT. Calculation occurs on explicit changed confirmation/regeneration.

## Persisted contract and data protection

The user approved the required Loft `correspondence_starts` map and explicitly
included the protected Toyota project copy. The map keys are owned profile
feature IDs and values are native Sketch point IDs. Missing chosen points retain
failed definitions for repair, with no silent reassignment or product migration
branch. Existing extensions and other feature records remain unchanged.

The inventory contains 1,210 tracked native assets and no factory Loft records.
The user models 15 and 16 and the protected Skeleton are conversion/verification
inputs only through isolated copies. The complete protected-project backup has
172 files, 2,603,459,762 bytes, and per-file verified SHA-256 hashes. No original
is an output target. Historical format-48 recovery files are outside this
Loft-only conversion scope.

Local evidence root: `build/release-2026111813/` (the existing investigation
directory name is retained). Inventory and backup manifests:
`loft-format-inventory.json`,
`protected-project-before-correspondence.sha256.json`.
Prepared JSON input copies are only an offline bridge; they do not count as
accepted conversion until the changed native implementation calculates, saves
and reopens them. Start-template checks must include the actual New Document
dialog, active editing context and command availability in mm and inch units.

## Finite verification matrix

The initial native pass before the final guide-reference extension verified:

- Square/circle 1-to-1 longitudinal endpoints, eight real native anchors,
  first-point changes, native save/cache reopen, Undo/Redo and missing-point
  failure retention.
- Mapped rectangular guided/unguided Lofts and rejection of a guide joining
  unrelated numbers; four polygon/circle-or-ellipse direction combinations
  with cold-regeneration ancestry checks.
- Incomplete 0/1-profile and failed guide definitions: strict rejection versus
  retained definition, diagnostics, no-op confirmation, reopen, repair,
  Undo/Redo and structural ownership rejection.
- Six first-plane XY/XZ/YZ cases and twelve manual-plane cases with both offset
  sides, parent rotation, frame/volume/centroid checks and native persistence.
- Native 3D and Helical Curve/Sweep round trips, Point references, rollback
  material, and a two-segment 2D Sweep whole-route Point.
- All complete picking candidate lists in the existing 144-case viewer matrix,
  including the centerline-to-native-route adapter.
- The full existing Sweep command suite after extracting the shared profile
  correspondence packet: volumes, ownership, locks, atomic errors and Undo/Redo.

The final native document matrix passed twelve square/circle combinations:
one/two guides, straight/curved guides and both directions of every guide.
It independently checked 2,640 boundary samples against the calculated faces,
native endpoints, validity, persistence and Undo/Redo. The native kernel suite
also passed after the exact four-boundary patch fix. The owned external-point
matrix passed 96 dimension states, 144 successive edits and 108 permitted drags.
Six start templates regenerated byte-identically; actual GUI New Document checks
passed for mm and inch in all five languages, including active context and normal
command availability. Copies 15 and 16 calculated, saved and reopened natively.

Closed rectangular Thin Loft remains a known limitation: six closed-profile
cases reject thickening identically with and without numbering. Six corresponding
open-profile cases pass. This change does not claim to repair that pre-existing
thickening limitation. Mixed rational guided networks beyond two stations are
not covered by the four-boundary repair.

No claim of exhaustive Sketcher, arbitrary Loft network, or
native Linux execution is made. The owned-point matrix covers distance/X/Y,
both orders and signs, driving/reference, locked/unlocked and fixed/free states,
successive value edits and drags with independently checked coordinates and
immutable source references. It does not claim an unrelated constraint-solver
rewrite or exhaustive trimming/tangency coverage.

Localization review: existing menu/order labels are reused. The two new Loft
diagnostics have cs/en/de/fr/ru entries. Catalog validation and the affected
Loft GUI checks passed in all five languages. Actual GUI checks also cover
point ordering, owned guide editing, saved definitions and Undo/Redo.

The protected Skeleton copy passed calculation of its 22-operation prefix
through the last Loft, including the Loft's dependent input references. Native
save/reopen retained the complete authored definition exactly. The full model
has 34 operations; this is not acceptance of its remaining downstream
operations. The verified copy therefore contains no partial calculated cache.
The evidence is `protected-loft-prefix.json`; its explicit
`full_model_valid: false` records that limited verification scope. The original
project files remain unchanged.

## Sheet editing and midpoint attachment

The user additionally approved a persisted midpoint reference for Flat, Bend
and Sheet Profile. Its owner/path identify the exact native edge and its key
contains that edge's semantic identity, including a trimmed fragment. The
midpoint is derived from its current native endpoints; neither coordinates nor
OCCT enumeration define its identity. The shared placement solver is unchanged.
The exact operation-input packet supplies the joining face and edge. Draft
resolution updates only the edited sheet feature/group, preserving earlier
history frames. Previously, resolving every preceding container against one
final input packet could move a Cut's frame during attachment confirmation.

Midpoint mode uses the section's own dimensions from the origin. Changing the
source edge length moves the midpoint without replacing those dimensions with
endpoint offsets. Endpoint attachment remains available. Sheet Profile exposes
its ordinary width controls in midpoint mode, including symmetry. Flat and
Profile Properties retain their shared Sketch button when their derived working
plane controls are hidden.

The native cut-edge matrix covers two fragments, three anchors and three feature
kinds, both in the default Body frame and a translated Body rotated about all
three axes. The 36 combinations passed material, exact fragment identity,
save/reopen, regeneration and Undo/Redo checks. Centered sections retain their
8 mm width through successive source widths of 46, 34 and 40 mm. Source
suppression retains the last valid feature/Sketch frames, marks the reference
invalid, persists that state and repairs when the source returns.

The midpoint Flat/Bend dimension matrix covers both endpoint sides, both
selection orders, Distance and Distance X, driving/reference and locked/free
states, repeated value edits and permitted drags, independent residual checks,
read-only source references and native Sketch readback. Default attached Flat
checks also retain the applicable existing X/Y/length dimension variants.
Actual hidden-window GUI tests on copy 17 passed short-fragment hover cycling,
click selection, endpoint/midpoint creation, Sketch entry/return, repeated
dimension-label double-click edits, Undo/Redo, native Save, Properties reopening
and Cancel for all twelve fragment/type/anchor combinations. Native translation
catalog validation and the affected controls passed in cs/en/de/fr/ru.

Unchanged confirmation passed for all three centered attachment kinds in both
Body frames: document revision, calculated shape storage and calculation
provenance remain unchanged. The Sheet Profile confirmation now recognizes an
exactly unchanged parent and driver Sketch before rebuilding its generated
group.

Cold regeneration exposed a separate prefix-publication defect. The complete
fresh-source GUI matrix passed once, but a focused repeat failed on iteration 17
for a centered Sheet Profile on the positive cut fragment. A native replay
reproduced the failure without the GUI. Its definition and last frame survived,
but its reference became unresolved. The failing input/output are retained under
`followup-gui/cut-sheet-last/`.

The requested edge, joining face and midpoint existed in the calculated packet.
During recovery, shortening an unchanged history prefix replaced its exact
reference packet with cumulative authored geometry, while retaining its old
coverage flag. The dependent Profile consequently received an empty reference
view. A deterministic regression now exercises shortening and extending a Cut
prefix three times for each fragment and Body frame; it failed on the old code.
The repair retains the exact requested geometry and absence evidence when
republishing an unchanged prefix, without recalculating material or remapping
references. The deterministic regression and model-calculation suite passed
after the repair (15.83 s and 6.94 s). Forty independent cold evaluations of
the previously failing saved input passed. The complete twelve-case fresh-source
GUI matrix passed again, including cold regeneration after each edited case.
The original saved copy also passed all twelve creation/edit/save/Cancel cases;
that mode deliberately retains its saved source packet and does not claim a
cold regeneration of the old packet's ancestry. The final Loft, Sweep and Bend
native suites passed (6.25 s, 10.23 s and 37.32 s), and the final Loft and Sheet
Profile GUI contracts passed. Logs: `short-prefix-fix.log`, `replay-fixed.log`,
`final-prefix-native.log`, `final-prefix-gui.log`, and
`final-prefix-original-gui.log`.

The initially suspected frame-rounding cause was not confirmed. The shared
placement solver remains unchanged; the proposed approval request for modifying
it was withdrawn once the packet-publication defect was isolated.

A saved geometry packet in the original copy 17 also has different split face
ancestry from a fresh native source calculation. That source must be verified
separately from a newly created attachment; the two observations are not yet
proven to have the same cause. No coordinate-based reference remapping or legacy
execution path has been introduced. Newly calculated source geometry and the
newly created attachment have passed cold regeneration. Conversion of an
existing reference to an older packet's different ancestry is not claimed.
Package publication still requires the committed-source and signed-runtime
validation gates.
