# Current Development Handoff

Date: 2026-11-16

This file preserves the current development context across Codex restarts. A
new chat must read `AGENTS.md` completely and then read this handoff before
taking any action.

## Current state and resume protocol

- Development and verification resumed on 2026-11-14. The worktree remains a
  deliberate uncommitted checkpoint until the remaining manual catalog and
  release gates are complete.
- Do not describe the complete repair/release as finished yet. Automated gates
  listed below pass, but the exact historical edge-treatment catalog still
  requires the user's manual acceptance where the retained source files exist.
- Do not discard, reset, overwrite, or broadly refactor the existing changes.
- Before editing anything in a new chat:
  1. Read `AGENTS.md` completely.
  2. Read this file completely.
  3. Run `git status --short` and `git diff --stat`.
  4. Inspect the diffs separately by workstream and identify which changes form
     a coherent checkpoint and which may be regressions.
  5. Resume with the verification-first protocol below. Preserve working cases
     and make the smallest coherent correction.

## Repository state at the current checkpoint

- Repository: `C:\Users\vladi\Prace\ZIMA-Engineering\00-SOFTWARE\01-ZIMA\ZIMA-CAD`
- Branch: `codex/sketcher-mechanisms`
- HEAD and origin when verification resumed: `bb07d1bdb`
- Normal Windows development launcher: `zima-cad.bat`

The current tracked diff spans the agreed container placement, Sketcher,
Fillet/Chamfer, invalid-history, Line Bend preview, measurement snapshot and
File/Exit workstreams, together with their tests, five localization catalogs
and English verification documentation. Treat its size as a risk signal:
review it by behavior and stage only the coherent product, test, localization
and documentation files. Keep the user's `config/config.ini` changes and all
unrelated local artifacts out of the commit.

## Verification update after resuming

The following checks passed from the current Windows Release build on
2026-11-14. These are automated engineering gates, not a substitute for the
remaining user-directed edge-treatment visual acceptance:

- The complete CMake Release build completed successfully, including all 263
  required compile/link steps.
- The focused placement/history/model group passed 10/10. It covers surface and
  exact-curve placement, reference assignment/removal, command persistence,
  work planes, history deletion, a missing profile with dependent operations,
  and model calculation. One old curve-placement expectation was corrected to
  the approved current rule: Edge/Plane/Point defines local XY in both authored
  reference orders.
- The placement/history GUI group passed 10/10, and the isolated current
  container-frame dialog contract also passed. This includes a generally
  oblique plane, projected Body Origin, whole-Origin entry, frame preview,
  source-plane regeneration, missing-profile scene retention, placed Sketch,
  Sheet Profile, application lifecycle and all five localization catalogs.
- The complete Fillet/Chamfer focused suite passed 11/11. It includes the OCCT
  support and ZIMA planar calculations, ordinary and symmetric preview checks,
  commands/queries, invalid dependent repair and independent verification that
  every preview rail endpoint lies on its terminal section.
- After the current `RADIUS.prtz` Chamfer 01 and Fillet 15 reports, a further
  16/16 focused Windows run passed. It includes both kernel builders, direct
  batch and support cases, split-edge recovery, history recovery, automatic
  missing-reference pruning, all five localization catalogs and ordinary,
  symmetric and selection GUI contracts.
- All 40 Sketcher GUI contracts passed in one uninterrupted serial run
  (1,651.46 seconds). The previously timing-sensitive keypoint-contact case
  passed in that run. The additional mandatory Trim, native junction,
  rectilinear solver and profile-dimension matrix also passed.
- A further 21/21 dependent suite passed for measurements, history recovery,
  Sheet Profile, Sheet exchange, Line Bend, thin-feature wires and the Sketcher
  supporting matrix.
- `git diff --check` reports no whitespace errors.

The normal development launcher `zima-cad.bat` starts the current tested
binary. The File menu now ends with a localized Exit action using the existing
red cancel/cross icon and `Alt+F4`. It deliberately has no duplicate toolbar
button. Dirty Exit/Cancel preservation, clean Exit, icon presence and all five
translations pass in the application lifecycle and localization contracts.

### 2026-11-16 edge-treatment and Sketcher completion candidate

- Open Fillet/Chamfer routes now persist both physical end vertices and an
  independent `Cut` / `No cut` decision at each end. New or changed ends default
  to `No cut`; no generated treatment may tunnel through material to a remote
  face. Purple source-node labels expose the two localized choices by
  double-click. Closed routes have no endpoint selector.
- Automatic cutting uses the complete local planar/curved boundary and resolves
  the retained side independently at both ends. The focused matrix covers
  opposite trim directions, asymmetric pockets, Fillet-to-Fillet contact,
  equal/A x B/A + angle Chamfer parity and a complete circular A x B Chamfer.
- Shaft Thread double-click inspection includes the thread length as well as the
  diameter. Opening A x B Chamfer Properties retains the shaded display mode.
- Circle incidence no longer inserts a redundant duplicate contact. Repeated
  circle-point dragging supports both angular and radial movement when the
  remaining degrees of freedom permit it, and the ordinary Sketch Image command
  is enabled. The required native/external, dimension, drag, Trim, Undo/Redo and
  save/reopen matrix is part of the focused verification set.
- Part native format is now 53 / JSON 77. The separate repository conversion
  calculated, saved and reopened all 1,058 Part-based templates, FORM assets,
  symbols and native fixtures before the verified outputs replaced their source
  copies. Metadata fast reading and exact cross-platform fixture fingerprints
  use the same format. No compatibility loader or sidecar was added.
- The direct OCCT Shell attempt may throw on a valid three-Fillet input. The
  exception is contained so the existing inner-offset Shell fallback runs; the
  Shell identity and general contract regressions pass without changing
  tolerance, side or identity rules.

Before publication, repeat the final full Windows Release link, the focused
edge-treatment/Sketcher/native-format matrix, localization validation and the
committed-source package gates. Keep `config/config.ini` and unrelated untracked
user artifacts outside the commit. Native Linux execution remains a separate
host gate.

Relevant untracked material includes:

- `cpp/app/sketch_parallel_inference_ui_verification.inc` (intended regression
  source; include it in the coherent change)
- `doc/verification/ZIMA_EDGE_TREATMENT_REGRESSION_MATRIX.md`
- `doc/verification/ZIMA_EDGE_TREATMENT_VISUAL_CHECKLIST.md`
- `doc/verification/evidence/`
- `POKUS.prtz`
- `.local-backups/`
- several screenshots, temporary inspection files, and other user artifacts

Preserve all unrelated and untracked user files. The two verification documents
and this handoff are tracked project documentation; local evidence images and
user model files remain outside the commit unless explicitly selected.

## Active workstreams

### 1. ZIMA Fillet and Chamfer terminal and node geometry

This is the main risky and incomplete area.

- A ZIMA Fillet or Chamfer must never leave a protruding sweep, tooth, sliver,
  or untrimmed terminal.
- Extend or sweep only as far as required to determine the relevant
  intersections, then trim the result to the real terminal face or neighboring
  geometry.
- Handle non-right-angle corners, adjacent radii, multiple branches, and
  multiple independently selected edges.
- If a requested solution is geometrically invalid, reject it cleanly and do
  not create malformed geometry.
- Preserve all cases that already work. Do not trade an old working case for a
  new special case.
- Fillet and Chamfer should share the same terminal-decision principles where
  their profiles permit it.
- The final calculation and the azure preview must use the same terminal
  interpretation.

### 2. Azure preview correctness

- The preview must apply the same computation independently to every selected
  edge and to both terminals of every segment.
- A known failure made the first branch correct while another branch extended
  outside the expected circular boundary, as if the terminal calculation had
  only been applied once.
- Do not replace a valid detailed preview with a simplified path merely because
  the radius exceeds a neighboring radius.
- A calculable feature without an azure preview is a defect.
- The preview must not promise geometry that the final calculation cannot
  create, and the final calculation must not silently use a different solution.

Status on 2026-11-14: the current candidate fixes the reproduced
`READIUS2.prtz` Fillet 03 calculation failure. One optional terminal extension
was wholly outside the retained cylindrical Body half-space. An exact empty
intersection is now treated as an absent optional tail; an empty main route
cutter remains a hard error. The retained-side probe also reaches the actual
Fillet cross-section when a long route makes the Body diagonal much larger than
the treatment radius.

A synthetic stacked-cylinder regression recreates the same sequence of two
independent side Fillets followed by connected five-edge R10 and R9 routes. It
requires a valid final Body, complete face lineage for every selected edge and
zero material outside the cylindrical envelope. Clean regeneration now passes
for `RADIUS.prtz`, `READIUS2.prtz` and `RADIUS-03.prtz`.

The current `RADIUS.prtz` Fillet 15 exposed a distinct additive case. Its one
persisted source edge resolves after preceding treatments to a connected
five-member U route (line, arc, line, arc, line). Extending its optional
curved-terminal tails to remote Body caps created the reported tooth beyond the
real route ends. A non-straight additive route now clips its base sweep to the
local curved Body surface but does not add remote tails. A split straight route
retains the established single-edge-equivalent continuation. The regression
sweeps R4, R8, R9, R10, R11, R12, R14 and R18, requires the changed region not
to pass either route end in its terminal direction, and also requires zero
result volume outside the cylindrical Body envelope.

The later saved `RADIUS.prtz` copy used for the 2026-11-14 regression does not
contain Fillet 15, but it exposes the same terminal principle independently in
Fillet 12. Its one straight vertical route ends tangentially on an earlier
cylindrical Fillet surface and has no remote planar cap. Extending one complete
cross-section along that unbounded cylinder created the reported tooth from
Z40 to Z52 even though the selected source edge ends at Z48. A tangent
analytical surface without a real remote cap no longer authorizes an axial
extension. The actual-model regression now requires every vertex of the added
Fillet material to project within the selected edge endpoint interval; the
changed region is exactly bounded by Z40 and Z48.

A disposable full copy of the current user model regenerated Fillet 15 and
opened its rollback preview successfully. A second disposable copy with only
the downstream Fillet 15 removed regenerated current Chamfer 01 in isolation.
Both previews matched their published wires and Cancel was non-mutating. The
original user file remained read-only with SHA-256
`6BC43B1EBF4E6F4E601BF4DFB59ED71D069D354DBCF092538AC6EDB22F80D39D`.

The Properties rollback preview was additionally checked after clean
regeneration for RADIUS Fillets 04, 05, 09, 11, 12, 13 and 14, RADIUS Chamfer
01, READIUS2 Fillet 03 and RADIUS-03 Fillet 01. Every persisted route resolved,
the published transient wire exactly matched the analytical preview, Cancel
left the document revision unchanged, and the ten captured Windows screenshots
showed continuous route rails and terminal sections. This is an automated and
developer visual precheck, not the user-directed manual acceptance gate.

### 3. Snapping and stable identity for ZIMA edge treatments

- Result faces, edges, and endpoints of ZIMA Fillet and Chamfer must be
  selectable and usable as references.
- The OCCT variant already exposes normal topological selection, but ZIMA owns
  separate primitive/result geometry and must publish stable identities for it.
- Preserve dependent operations, regeneration, save/reopen, and Undo/Redo.
- The user observed a cut/reference attached to a ZIMA Fillet that later lost
  identity. Identity must be based on stable owned/result geometry, not a
  transient display fragment.
- Do not violate the repository's geometry-side identity contract.

Status on 2026-11-14: the explicit ZIMA path is covered and passes. The
calculated result publishes selectable owned faces, body edges and both stable
edge endpoints. `zima_cpp_sketch_reference_command_tests` now forces
`calculation_method=zima` and verifies a result-edge Reference, its two endpoint
References, a linked native edge outline, an analytic cylindrical face circle,
radius editing, Undo/Redo and cold native save/reopen. The ordinary viewer
candidate contract accepts persisted `fillet:edge:` and `chamfer:edge:` result
keys while continuing to reject anonymous operational edges.

`zima_cpp_model_calculation_tests` additionally creates a real subtractive Cut
whose placement is driven by a ZIMA Fillet result endpoint. Changing the
Fillet radius preserves the exact authored reference; resizing the source Body
moves the Cut with the regenerated endpoint; Undo/Redo and native save/reopen
retain a valid calculated chain. This uses the stable owned result identity,
not the transient preview fragment, and changes no side or signed-offset
contract.

### 4. Invalid dependency editing crash

Reproduction reported in `RADIUS.prtz`:

1. Use the last three Fillet features.
2. Increase the first Fillet radius until that feature is invalid and confirm.
3. Edit the next dependent Fillet.
4. The application crashed.

Required behavior:

- An invalid red feature and its invalid dependents remain safely editable.
- The user can change the radius, remove or replace references, delete the
  feature, or otherwise repair the history without a crash.
- Do not introduce a compulsory modal chain-repair workflow. The user rejected
  the need for a separate repair panel.
- Preserve the last valid frame after reference loss as required by
  `AGENTS.md`; retained display state does not make an invalid dependency valid.

The dedicated GUI regression now covers the complete repair path. It makes an
upstream ZIMA Fillet invalid, verifies that the last valid Body remains visible,
opens the failed dependent Fillet without another blocking prompt, changes its
radius, removes one reference and cancels without changing the document. It
then deletes the failed dependent, exercises deletion Undo/Redo, repairs the
upstream radius, exercises repair Undo/Redo, saves and reloads the fully
calculated chain. This passes in
`zima_cpp_invalid_dependent_fillet_ui_contract`. A history command that commits
the requested change while another definition remains invalid intentionally
returns the `calculation_errors` diagnostic; the regression verifies the
committed document state instead of misreading that diagnostic as a rejected
delete.

Fillet and Chamfer Properties now prune unresolved treatment references before
presenting or committing the definition. Only exact, descendant, counterpart
or uniquely reconnected edges survive; proximity guesses are deliberately not
used. An absent edge is omitted automatically and the remaining valid routes
are recalculated. Empty routes and stale explicit ends are removed with it, so
an obsolete edge does not block an otherwise valid treatment or require a
manual repair action. The all-missing case remains safely editable against the
cached rollback Body and Cancel preserves the authored definition.

History deletion applies the same repair before deciding whether a dependent
Fillet or Chamfer is unavailable. Deleting an earlier treatment is allowed.
Missing members, their empty routes and aligned endpoint metadata are removed;
if at least one exact route remains, the dependent treatment stays active and
is recalculated. Only a treatment with no usable route remains red and
repairable in history. The command-level regression covers a later Fillet with
one lost and one surviving route, calculation of the survivor, deletion
Undo/Redo and native save/reopen. The GUI deletion contract and the complete
focused edge-treatment suite pass from the same Windows Release build. The GUI
contract now also performs that mixed-route deletion through the real
confirmation window, verifies that the dependent treatment remains a healthy
Tree row, and repeats save/reopen and Undo/Redo on the automatically pruned
definition.

Additional 2026-11-14 verification did not reproduce model disappearance from
a missing profile alone. Both the dedicated `Extrusion` record and the current
general `Feature` extrusion were tested with their Sketch removed before a
dependent Fillet was appended. The valid preceding solid remained in the Body
scene after native reopen and explicit Regenerate; both failed definitions
remained red and repairable; restoring the Sketch, Undo and Redo recovered the
chain. This is now covered by `zima_cpp_history_deletion_tests` and
`zima_cpp_missing_profile_scene_ui_contract`. A user document which still
reproduces disappearance is needed to identify the differing dependency or UI
transition; do not infer that the reported broader defect is disproved.

### 5. Edge-treatment regression catalog

- Store examples with the naming convention `radius-01`, `radius-02`, and so
  on. The user explicitly corrected the earlier inconsistent spelling.
- Existing untracked verification material is listed in the repository-state
  section above. Inspect and reconcile it before adding another catalog.
- A Fillet or Chamfer change must not proceed unless known examples remain
  correct.
- Automated checks are necessary but not sufficient. After they pass, the user
  intends to inspect the examples manually in ZIMA-CAD.
- Do not claim a case is fixed without geometry, preview, and dependent-operation
  evidence.

### 6. Sketcher parallel inference from references

Status on 2026-11-14: implemented and verified from the current Windows
development build.

- Interactive line creation now infers Parallel from suitable oblique native
  and external line references, displays the support in the shared azure
  inference preview, and persists the Parallel constraint on confirmation.
- Exact horizontal or vertical external lines retain the established H/V
  inference instead of creating a redundant pair relation. Broken external
  references are excluded.
- The Image command is immediately below Insert Symbol in an ordinary Sketch.
- `zima_cpp_sketch_parallel_inference_ui_contract` covers native and external
  oblique supports, preview, confirmation, repeated GUI dragging, read-only
  external geometry, H/V precedence, broken references, toolbar order,
  Undo/Redo, and native save/reopen.
- The mandatory supporting matrix passed:
  `zima_cpp_sketch_external_direction_order_ui_contract`,
  `zima_cpp_sketch_trim_fragment_tests`,
  `zima_cpp_trim_native_junction_ui_contract`,
  `zima_cpp_sketch_dimension_entry_ui_contract`,
  `zima_cpp_sketch_mechanism_ui_contract`,
  `zima_cpp_rectilinear_solver_tests`, and
  `zima_cpp_parallel_profile_dimension_ui_contract`. These jointly cover both
  applicable selection orders, native/external references, three orientations,
  driving/reference and locked/unlocked dimensions, repeated value edits and
  drags, underconstrained/fully constrained states, Trim, mouse interaction,
  Undo/Redo, and save/reopen. Contact side is not applicable to a direction-only
  Parallel relation.
- The profile-dimension GUI fixture must be regenerated by its dependent
  rectilinear solver test before an isolated GUI run; a stale pre-format fixture
  correctly fails to open.
- No user-visible text was added or changed. The localization catalogs require
  no update for this work.

### 7. Container placement behavior

Status on 2026-11-14: the specifically approved one-plane behavior is
implemented and its focused regression set passes.

- With exactly one positional planar reference, the container/Sketch origin is
  the orthogonal projection of the owning Body Origin onto that plane. The
  arbitrary face click no longer supplies an unintended in-plane translation.
- The existing solver still supplies FRONT/TOP/work-plane orientation. A second
  orientation-only reference may resolve roll but cannot move the projected
  origin. Replacing the plane recomputes from the Body Origin without drift.
- The focused GUI contract uses generally oblique planes and verifies projected
  point coincidence, one aligned frame axis, orientation-only second reference,
  replacement, OK persistence, Undo/Redo, and native reopen.
- Ordered three-point placement now defines the local XY work plane consistently
  with the dialog: P1 is the origin, P1 to P2 is local +X, and P3 selects the
  positive local +Y half-plane; local +Z is their cross product. This recognizes
  the actual persisted form (P1 positional, P2/P3 orientation-only direction
  references), not only the older three-positional-point shortcut.
- Edge/plane/point station placement also opens its owned Sketch in the solved
  local XY plane containing the edge and plane. The historical explicit FRONT
  rule remains local XZ where no complete XY frame definition is present.
- `zima_cpp_work_plane_contract_tests`, the focused container-frame dialog
  contract, `zima_cpp_sketch_return_frame_ui_contract`, and
  `zima_cpp_work_plane_ui_contract` passed after this correction, including a
  generally oblique plane, source-plane regeneration and native reopen.
- Twelve placement regressions passed, including mesh/surface, curve, face,
  reference assignment/removal, whole-origin preview, work-plane, command, and
  UI contracts. The existing one-shot Origin disarm was already present in the
  released interaction path and `zima_cpp_whole_origin_ui_contract` also passed.
- Curved supports and Thread retain their existing behavior. No user-visible
  text changed, so no localization catalog update is required.
- The complete shared placement contract remains protected. Any broader change
  still requires explicit user approval.

### 8. View-only Fillet and Chamfer node labels

Status on 2026-11-14: implemented and verified in the current Windows
development build.

- Each connected continuous selected path is numbered independently. Every
  distinct source node is labelled in traversal order (`1.1`, `1.2`, `1.3`,
  and so on); the next disconnected path begins at `2.1`. A closed loop's
  shared start/closing node is labelled once as `1.1`. Display samples inside
  a curve do not receive labels. These are source-node identifiers, not labels
  for generated Fillet/Chamfer contacts or the ends of the resulting treatment.
- Generated terminal contacts remain available only to the verification path
  which checks preview trimming; they are deliberately not presented as
  numbered object nodes.
- Do not add a second node table to the Properties GUI. The user explicitly
  decided that labels should exist only in the view because the segment table
  and derived node table are independent structures.
- A future node-solution selector may be activated through these view labels.
- Preserve ordinary wire-only selection and inspection rules while adding any
  view annotations.
- The labels are transient presentation data. OK, Cancel and closing Properties
  clear them; they never enter the native document, calculation or Undo history.
  Unit coverage checks open, disconnected and closed paths, every unique source
  node and exact final treatment contacts separately. The ordinary and
  symmetric Fillet/Chamfer GUI contracts cover the same calculated node list.
  Earlier RADIUS/READIUS2 captures used endpoint-only labels and must be rerun
  against the current all-source-node contract.
- Only numeric identifiers were added. No translatable user-visible text or
  localization catalog changed.

### 9. Line Bend preview and attached Sheet Profile work plane

Status on 2026-11-14: the earlier Line Bend backlog was re-audited and its
focused native and GUI contracts pass from the current Windows development
build.

- A new Line Bend persists the identity of its one ordinary Sketch segment
  only when OK commits the feature. Its live Properties preview now derives
  that identity in a transient feature copy, so the complete bent-sheet azure
  wire appears immediately after the line is drawn and follows a replacement
  line without modifying the document or creating Undo history.
- The existing Line Bend contracts cover the inward Sketch-plane side, true
  bend-axis centre, selectable owned result, chainable new boundaries,
  Unbend/Bend Back, oblique-sheet seam cleanup, angle display, fixed side,
  native reopen, unchanged OK, Cancel and Undo/Redo.
- The generic ordered P1/P2/P3 and Edge/Plane/Point placement frames continue
  to define local XY. An edge-attached Sheet Profile is a command-specific
  cross-section and retains its established local XZ route plane; generic
  automatic work-plane resolution no longer overwrites it.
- `zima_cpp_sheet_exchange_tests`, `zima_cpp_thin_feature_wire_tests`,
  `zima_cpp_line_bend_ui_contract`, `zima_cpp_ui_contract_tests`,
  `zima_cpp_work_plane_contract_tests` and `zima_cpp_sheet_profile_tests`
  passed after the correction.
- No user-visible text or native format changed. Localization catalogs and
  factory native assets require no update for this correction.

### 10. Large STEP import and interactive rotation re-audit

The earlier large-assembly backlog was re-audited on 2026-11-14 against the
current Windows Release build and the read-only 175,494,212-byte G92K1 STEP
fixture. The current product import path still uses one shared
`StepSourceDocument`; the diagnostic `--capture-file` path intentionally performs
a separate inspection transfer and must not be used as the product import-time
measurement.

The real product Part import completed without the historical kernel crash. It
took 430,090 ms and produced 205,818 vertices, 284,803 triangles, 17,216 original
edges and a volume of approximately 1,944,860 mm3. The profiled costs were
4,404 ms for source reading, 9,862 ms for OCCT transfer, 21,759 ms for topology
capture and 348,261 ms for exact volume and inertia. Exact physical-property
integration remains the dominant import cost; no precision or property contract
was reduced. Saving the current-format 303,258,609-byte native Part took
12,849 ms.

The current viewer benchmark then passed its complete connectivity, stable-frame,
hover, confirmation, camera and changed/cleared/restored mesh checks on that
fresh native result. At 1200 x 800 with framebuffer readback, opaque unhighlighted
warm-frame medians were 12.402 ms Wire, 15.400 ms Hidden edges, 13.444 ms No
hidden edges, 12.851 ms Shaded with edges and 12.332 ms Shaded. Transparent
shaded modes measured approximately 35.7-40.7 ms. These measurements establish
current-fixture behavior on this Windows system, not a universal frame-rate
promise. The robust import and ordinary opaque rotation items are implemented;
further exact-import acceleration remains performance work rather than a
functional repair or release blocker.

## Concrete model examples and reported failures

File names below preserve the actual names reported by the user, including the
existing `READIUS2.prtz` typo. Locate model files read-only before assuming an
exact directory.

### `RADIUS.prtz`

- **Fillet 04:** incomplete. At a small radius the azure preview looked good.
  When the value exceeded the side Fillet radius, the preview became simplified
  and the feature failed even where a valid terminal solution should exist.
  It must use correct terminal logic and be completely trimmed.
- **Fillet 05:** produced teeth/jagged leftovers. Eliminate them rather than
  hiding them in the preview.
- **Fillet 09:** previously worked and then regressed. The reported result had
  bad trimming or leftover geometry. This is a mandatory non-regression case.
- **Fillet 11:** worked only for a small radius and could break while still
  smaller than the adjacent radius. It needs a robust transition and should be
  rejected only at a real geometric limit.
- **Fillet 12:** exposed a broader error in terminal/long-transition handling.
- **Fillets 13 and 14:** additional special cases created for the catalog;
  inspect and preserve them.
- **Chamfer 01:** began calculating, but the result with a preceding Fillet was
  not considered correct. When the Chamfer size exceeds the source Fillet/arc,
  discard the obsolete node-arc solution and extend/trim the Chamfer to the
  terminating face, following the Fillet terminal principle.
- The invalid-dependency crash was reproduced among the last three Fillets.

### `READIUS2.prtz`

- Contains multiple independent selected branches/edges.
- **Fillet 03:** preview terminal treatment appeared to run only once. One
  branch was correct while another endpoint extended outside the circle. Each
  selected edge must independently run the same calculation.
- Some features calculated final geometry but had an absent or incorrect azure
  preview.
- Several simpler cases now appeared good. They must remain regression cases,
  not be rewritten casually.

### `RADIUS-03` / `radius-03`

- **Fillet 01:** OCCT handled the case, while the ZIMA method created malformed
  geometry. Preserve it as a teaching and regression example.
- **Tažení 03, line 002:** Sketcher parallel-to-reference inference/constraint
  failed against a Fillet/radius 03 reference.

### `POKUS.prtz`

This local file belongs to earlier Bend-by-line experiments. It is useful
context but is not the primary current edge-treatment fix.

## Agreed geometry decisions

- Prefer a clean, manufacturable terminal that reaches and is trimmed to the
  relevant face over a protruding Fillet or Chamfer sweep.
- Fillet and Chamfer should make compatible terminal decisions.
- Crossing the size of an adjacent/source radius may require a different node
  solution. Do not retain an arc after it has ceased to be geometrically valid.
- Pro/ENGINEER examples demonstrated more than one valid solution: terminating
  at a face, blending into an adjacent Fillet, or extending an existing Fillet.
  ZIMA's preferred automatic result in the discussed cases is generally the
  clean face-trimmed, manufacturable solution.
- Some difficult cases are not solved even by Pro/ENGINEER. Clean rejection is
  acceptable; malformed geometry is not.
- Backward compatibility with legacy native files is not required. Do not add
  hidden compatibility paths or duplicate old/new algorithms.

## Verification and release gates

1. Read `doc/FEATURE_PERFORMANCE_GUIDE.md` before extending a modeling command.
2. Keep one shared C++ geometry implementation for Windows and Linux. Do not
   duplicate algorithms by platform.
3. Run focused geometry/unit tests for each changed terminal rule.
4. Verify all catalog cases, including those that already worked before the
   change.
5. Verify preview and final geometry use the same interpretation.
6. Verify selectable result edges/endpoints, dependent operations, identity,
   invalid-history editing, save/reopen, and Undo/Redo.
7. Run the mandatory Sketcher matrix if Sketcher code is changed.
8. Perform localization review for every change. If no user-visible text was
   changed, record that fact; otherwise update `cs`, `en`, `de`, `fr`, and `ru`.
9. Ask the user for manual ZIMA-CAD inspection only after automated gates pass.
10. Do not commit, push, or publish a Windows package from the current WIP state.

A later Windows release must follow `doc/WINDOWS_RUNTIME_AND_BUILD.md`, use
committed Git data, pass all packaging validation gates, and keep
`zima-cad.bat` as the normal development launcher. No release was completed at
this stop point.

## Earlier backlog that must be re-audited before resuming

These topics were discussed earlier, but their current implementation status is
not established by this handoff. Do not assume they are finished or immediately
active without inspecting code and tests.

- Sheet-metal Bend by line was re-audited on 2026-11-14. Its completed status
  and regression evidence are recorded in active workstream 9 above; retain it
  as a regression gate rather than reimplementing it.
- Planar Surface from Sketch was re-audited on 2026-11-13 and is already
  implemented. The command is first in the Surfaces application, owns its
  Sketch and work plane, exposes the plane offset without extrusion controls,
  and preserves General Surface independently. Its focused GUI contract passed
  from the current development build; retain it as a regression gate rather
  than reimplementing it.
- STEP import of large assemblies and interactive rotation performance was
  re-audited on 2026-11-14. Robust import and ordinary opaque rotation pass on
  the current G92K1 fixture; exact physical-property integration remains the
  dominant import cost. See active workstream 10 above and retain its current
  measured contracts as release regression gates.
- Future FEM Part concept: a derived/updated FEM body, projected sketch,
  load/constraint/temperature containers, mesh generation, calculation, and
  result visualization. This is a concept only and is not authorized for
  implementation as part of the current repairs.

## Deferred OCCT type-alias maintenance

After the current functional repairs and release are complete, replace the
OCCT 8.0.1 deprecated collection aliases and convenience headers used by the
shared C++ implementation with their corresponding `NCollection` types. Keep
this as a separate maintenance change: the aliases currently map to the same
containers and do not cause a geometry or runtime defect, while a broad
mechanical rewrite during Fillet/Chamfer work would make regressions harder to
attribute. Apply one shared Windows/Linux change and run the affected kernel,
persistence and packaging checks before accepting it.

This deferred maintenance changes no user-visible text; no localization catalog
update is required.

## Exact prompt for the next chat

Use this prompt after opening the repository in a new chat:

> Read `AGENTS.md` and `doc/AI/CURRENT_DEVELOPMENT_HANDOFF.md` completely,
> inspect the dirty worktree without discarding anything, and resume from the
> verification-first protocol. Do not build or change code until you have
> identified which WIP changes are coherent and which are regressions.
