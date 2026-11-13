# Current Development Handoff

Date: 2026-11-13

This file preserves the current development context after the user deliberately
stopped work so the computer and Codex chat could be restarted. A new chat must
read `AGENTS.md` completely and then read this handoff before taking any action.

## Stop state and resume protocol

- Development, builds, tests, commits, pushes, and release work were stopped at
  the user's request.
- The dirty worktree contains unverified work in progress. It must not be
  described as finished or regression-safe.
- Do not discard, reset, overwrite, or broadly refactor the existing changes.
- Before editing anything in a new chat:
  1. Read `AGENTS.md` completely.
  2. Read this file completely.
  3. Run `git status --short` and `git diff --stat`.
  4. Inspect the diffs separately by workstream and identify which changes form
     a coherent checkpoint and which may be regressions.
  5. Resume with the verification-first protocol below. Preserve working cases
     and make the smallest coherent correction.

## Repository state at the stop point

- Repository: `C:\Users\vladi\Prace\ZIMA-Engineering\00-SOFTWARE\01-ZIMA\ZIMA-CAD`
- Branch: `codex/sketcher-mechanisms`
- HEAD: `2e73b1531`
- Normal Windows development launcher: `zima-cad.bat`

Tracked modified files observed immediately before this handoff:

- `config/config.ini`
- `cpp/app/workspace/sketch_preview.cpp`
- `cpp/app/workspace/sketch_snapping.cpp`
- `cpp/app/workspace/toolbars.cpp`
- `cpp/modules/kernel_occt/src/occt_edge_treatment_terminals.hpp`
- `cpp/modules/kernel_occt/src/occt_planar_edge_chamfer.cpp`
- `cpp/modules/kernel_occt/src/occt_planar_edge_fillet.cpp`
- `cpp/modules/ui/src/container_placement_section.cpp`
- `cpp/tests/occt_planar_edge_chamfer_tests.cpp`
- `cpp/tests/occt_planar_edge_fillet_tests.cpp`
- `cpp/tests/placement_reference_assignment_tests.cpp`
- `cpp/tests/thin_feature_wire_tests.cpp`

The observed diff contained 974 insertions and 162 deletions across these 12
files. Treat that size as a risk signal: review it by behavior, do not assume it
is one finished change.

Relevant untracked material included:

- `doc/verification/ZIMA_EDGE_TREATMENT_REGRESSION_MATRIX.md`
- `doc/verification/ZIMA_EDGE_TREATMENT_VISUAL_CHECKLIST.md`
- `doc/verification/evidence/`
- `POKUS.prtz`
- `.local-backups/`
- several screenshots, temporary inspection files, and other user artifacts

Preserve all unrelated and untracked user files. This handoff file itself will
also remain untracked until a later, explicitly approved commit.

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

- While drawing a line, infer and hold parallelism to suitable native or
  external reference edges, except where the established horizontal/vertical
  behavior takes precedence.
- When the line is created, add the corresponding Parallel constraint.
- Reported example: `RADIUS-03`, `Tažení 03`, line 002 could not be made
  parallel to the reference from Fillet/radius 03.
- `cpp/app/workspace/sketch_preview.cpp` and
  `cpp/app/workspace/sketch_snapping.cpp` contain unverified WIP changes.
- The Sketcher Image command was intended to move below Insert Symbol;
  `cpp/app/workspace/toolbars.cpp` is also WIP and needs GUI verification.
- Any Sketcher change must satisfy the complete dimension, repeated-drag,
  value-edit, selection-order, external-reference, trim, save/reopen, and
  Undo/Redo matrix required by `AGENTS.md`.

### 7. Container placement behavior

- Reported defect: after using the Origin button, it stayed armed and blocked
  later reference selection. It should disarm after a successful selection.
- The user explicitly approved a targeted improvement for planar placement, but
  the shared placement contract remains protected.
- For a planar target face or plane, the first selection supplies the normal and
  necessary orientation. Relocate the container origin by the minimum required
  projection/normal displacement and preserve as much of the existing frame as
  possible.
- Determine the second/top direction through the existing solver rather than a
  new parallel placement implementation.
- Changing the target face must correctly recompute the frame.
- Do not break placement on general or cylindrical faces, Hole placement, or
  Thread behavior. Thread is a different command; the user originally mentioned
  it only by mistake.
- Inspect what the current solver already provides and omit only genuinely
  redundant movement. Do not rewrite the shared placement system speculatively.
- `cpp/modules/ui/src/container_placement_section.cpp` and placement tests are
  unverified WIP.
- Read the protected Container Placement section of `AGENTS.md` before touching
  this code. Broader changes beyond the specifically approved behavior require
  new user approval.

### 8. View-only Fillet and Chamfer node labels

- Label contact/end nodes in the 3D view as `1.1`, `1.2`, `2.1`, `2.2`, and so
  on.
- Do not add a second node table to the Properties GUI. The user explicitly
  decided that labels should exist only in the view because the segment table
  and derived node table are independent structures.
- A future node-solution selector may be activated through these view labels.
- This was added to the requested work package but was not known to be
  implemented or verified at the stop point.
- Preserve ordinary wire-only selection and inspection rules while adding any
  view annotations.

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

- Sheet-metal Bend by line: inward-side semantics, bend axis at the true bend
  center, selectable result, chainable new edges, unfold support, no stray
  lines across the sheet, angle display, and consistent command-button layout.
- Planar Surface from Sketch: optional offset, nested closed loops, and no
  extrusion-direction row in Surface properties.
- STEP import of large assemblies and interactive rotation performance.
- Future FEM Part concept: a derived/updated FEM body, projected sketch,
  load/constraint/temperature containers, mesh generation, calculation, and
  result visualization. This is a concept only and is not authorized for
  implementation as part of the current repairs.

## Exact prompt for the next chat

Use this prompt after opening the repository in a new chat:

> Read `AGENTS.md` and `doc/AI/CURRENT_DEVELOPMENT_HANDOFF.md` completely,
> inspect the dirty worktree without discarding anything, and resume from the
> verification-first protocol. Do not build or change code until you have
> identified which WIP changes are coherent and which are regressions.
