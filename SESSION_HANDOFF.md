# Development handoff — 2026-09-15

## Current application

Boolean split-edge repair: shared Add/Subtract topology completion assigns distinct
parent-derived identities to disconnected edge fragments and persists their new
endpoints for Fillet/Chamfer routes and variable R1. The original `Projects/11.prtz`
was inspected without modification; reproductions also cover subtractive Boxes,
asymmetric walls and multiple solids. Existing calculated documents require one
explicit **Regenerate**. No shared placement or native file-structure change is
involved. See [Boolean edge fragments](doc/EDGE_TREATMENT_COMMANDS.md#boolean-edge-fragments-2026-09-15).

Fillet/Chamfer annotations now retain LMB confirmation on the way to their purple
grips and after View drags. Create/edit Properties stages annotation placement and
commits it with the feature in one Undo step; Cancel discards it. All five treatment
modes passed actual picker/grip tests in Properties and ordinary View, including
direct numeric edits. Chamfer distances now use their true section plane. See
[annotation grips](doc/EDGE_TREATMENT_COMMANDS.md#fillet-and-chamfer-annotation-grips-2026-09-15).
The missing updater translations in Czech, German, French and Russian were also
completed; the exact catalog/placeholder contract passes for all five UI languages.
Final GUI/CLI and all-test-target build passed, as did all 12 related contracts
and the separate treatment-grip GUI regression. Logs and captures are linked in
the edge-treatment documentation. These changes are in the root BAT's development
build and the signed `2026091504` package described below.

ZIMA-CAD is a native C++/Qt application. Python, its old runtime and its packaging
tools have been removed with the user's approval. The old migration and cutover
checklists have been retired; previous source and documentation remain in Git.
Use [native architecture](doc/CXX_ARCHITECTURE.md),
[native behavior](doc/NATIVE_BEHAVIOR_CONTRACT.md) and [AGENTS.md](AGENTS.md).
All project documentation must remain English; application localization is separate.

The user's agreed Windows development entry point is the repository-root
`zima-cad.bat`. Preserve its stable name/location and keep it targeting the current
local build with the repository working directory. Release packaging must not
change this daily workflow. Direct EXE navigation is not required of the user.

Recent feature work is documented in [Holes](doc/HOLES.md),
[work-plane selection](doc/WORK_PLANES.md) and the
[Assembly rotation arm](doc/ASSEMBLY_ROTATION_HANDLE.md). The shared View toolbar
starts with Regenerate, followed by the CAD console toggle using the terminal
icon from ZIMA-CAD-Parts. GUI, console and CLI use shared model operations;
[command coverage](doc/CAD_COMMAND_COVERAGE.md) records the current scope.

The follow-up Holes Sketcher repair moves diameter below plane offset, retires
the properties preview/offset handle while Sketcher is active, and restores
brown axes with a black idle Sketch origin. New dialog-owned Sketches explicitly
use their owning Body's insertion cursor and existing coordinate transform for
external references and reference-to-outline projection. No shared placement
solver, native format or factory template changes are involved. See
[Holes interaction and verification](doc/HOLES.md).

Windows Release validation for this repair passed: native Holes operations,
Sketch reference commands (including draft boundaries and translated Body),
Holes GUI mouse interaction, and the work-plane GUI contract. Captured new/edit
Sketcher views confirm the normal axis/origin colors. Logs are under
`build/holes-sketch-*.log`; captures are under `Projects/test/holes-*.png`.

These changes are in the local development GUI/CLI build. The immutable
`2026091503` portable candidate described below predates this repair.

Holes Properties also has an analytical cyan cylinder preview and one editable
diameter dimension attached to an existing cylinder. The anchor is recomputed
from stable segment identities and disappears for an empty Sketch. The five
View display modes now use distinct palette-aware cube SVG icons (wireframe,
hidden edges, visible edges, shaded with edges, shaded). They retain the same
actions, tooltips and display-mode behavior.

This preview/icon follow-up passed the native Holes, Holes GUI and work-plane
GUI tests (3/3) with a successful GUI/CLI build. The captured cylinder wires,
diameter annotation and five toolbar icons were visually inspected. See
`build/holes-preview-final-build.log`, `build/holes-preview-tests.log` and
`Projects/test/holes-cylinder-preview.png`. No new portable release was built.

The Holes controls follow-up adds the shared purple diameter grips in both View
and Properties, transactional annotation placement, and a visible persisted axis
for each source segment. Direct View edits of diameter and Sketch plane offset
now use the Holes transaction; previously the generic inline editor had no
Holes branch and rejected the submitted offset. This does not change the
shared container-placement solver or the native file schema. See [Holes](doc/HOLES.md).
GUI/CLI builds and all seven targeted contracts passed. The direct View test
verifies offset persistence, axis movement, diameter edits and Undo; grip tests
verify pending Cancel/OK and save/reopen behavior. Logs are under
`build/holes-controls-*.log`; `Projects/test/holes-view-controls.png` shows the
ordinary View controls. Existing calculated models need explicit regeneration
to populate the new persisted drilling axes. The root BAT remains the launch
entry point; no new portable release was produced.

A subsequent Holes selection repair makes the shared feature-properties
annotation filter retain LMB confirmation during pointer movement. Its previous
advance-on-hover behavior cleared the selected diameter before the pointer
could reach a rim grip. The GUI regression now uses actual picker clicks,
button-free travel through empty View space, and drags of both rim grips;
it no longer substitutes programmatic confirmation for that interaction.
The regression reproduced the failure before the fix; the final Holes,
inline-dimension and work-plane GUI contracts passed (3/3). Evidence is in
`build/holes-selection-reproduction.log` and `build/holes-selection-tests.log`.

## Distribution and Linux continuation

Read [the binding distribution rules](doc/DISTRIBUTION_CLEANUP_PLAN.md) and
[the Linux release handoff](doc/LINUX_RELEASE_HANDOFF.md) before changing packaging.
The user will finish Linux support on Linux. Do not install WSL as part of this
Windows task. The removed Conda runtime was still referenced by Linux development
presets, so replace that dependency setup on Linux before building there.

The native Windows launcher, committed-source candidate builder and shared
portable `config/` layers are implemented; see
[Native distribution](doc/NATIVE_DISTRIBUTION.md). The signed updater is now
implemented; Linux runtime acceptance remains pending. Do not publish old-style development builds as compliant
releases. Current Windows native dependencies remain available in the build/vcpkg
setup. A fresh GUI/CLI build and console UI contract passed with the old runtime
absent (see the Linux handoff for validation details).

Current build ID: `2026091504`; release notes are in
[2026091504](doc/releases/2026091504.md). The earlier `2026091503` Windows candidate from commit
`3d7eaaf89bdd6a8a7f47081ae587c2a8c449d6cc` passed the full package smoke and archive
checks; the exact SHA-256, local output locations and test scope are recorded in
[Native distribution](doc/NATIVE_DISTRIBUTION.md#windows-verification-on-2026-09-15).
The unpacked local portable installation holds versions `2026091503` and
`2026091502` and preserves shared `config/`. It is an unsigned local candidate,
not a GitHub release. The new signed `2026091504` package is ready separately in
`.dist-output/release-2026091504/`. It passed candidate and finalized-archive smoke,
production bootstrap verification and the packaged Updates GUI contract.
See [signed build acceptance](doc/NATIVE_DISTRIBUTION.md#signed-windows-build-2026091504)
for its exact commit, SHA-256 and logs. Public release publication and Linux
acceptance remain separate release work; older local installations are unchanged.

## Application updates

The 2026-09-15 update request is implemented using the current ZCP design adapted
to native CAD/CMake. Settings has General/Updates tabs. Startup checks run in a
background helper and expose only a small status-bar link for verified newer
versions. One explicit Install and restart action downloads, verifies and restarts;
dirty documents and active edits prevent both its start and the final restart.
Cancel or closing Settings revokes that approval, including a queued restart.
Cached preparation alone never activates a build. Successful startup keeps current
plus previous per platform, with signed-inventory checks before cleanup/rollback.
Root launchers recover interrupted selection and GUI/CLI processes register their
runtime lifetimes. No other CAD process is forcibly stopped.

Read [Application updates](doc/UPDATES.md) for the manifest, public trust anchor,
signing commands and tests. The existing ZCP publisher public key is embedded;
its protected private key was used locally for signing without export or inclusion
in the package. Development continues through the same root
BAT. It can check releases, while installation requires a signed portable bundle.
Windows and Linux may now publish independently with immutable release assets.
Linux execution stays assigned to the Linux host; test Linux manifests are not
evidence of Linux runtime support. No GitHub release was created by this work.

## Work order

On 2026-09-15 the user confirmed this immediate order: finish the Holes preview,
diameter annotation and display-mode icons; then complete program updates;
then continue Sketcher offsets, especially external and STEP source curves.
Updater implementation and Windows signed-bundle acceptance are complete.
The first signed bundle is prepared locally; public release publication is pending.
Modeling work next returns to Sketcher offsets.

New explicit user instructions take precedence. The agreed Part sequence is in
[ROADMAP.md](ROADMAP.md#agreed-next-steps-for-part-2026-09-06), beginning with
Sketcher offsets, especially projected STEP geometry. Current offset support is
in [SKETCH_OFFSET.md](doc/SKETCH_OFFSET.md); review remaining practical coverage
before treating the historical plan as unimplemented work. The comprehensive
Undo/Redo audit stays after the agreed modeling features are broadly implemented.
AI integration is a separate next stage after the shared CAD commands.

Preserve current user configuration and unrelated working files. Required native
fixtures under `tests/fixtures/cross_language` are still consumed by C++ tests;
the historical directory name is not a reason to delete them.
