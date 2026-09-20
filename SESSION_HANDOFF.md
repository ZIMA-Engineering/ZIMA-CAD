# Development handoff — 2026-09-20

## Linux release 2026092001 preparation

The user authorized documentation, commit, push and a Linux release of the Drawing
work. Build identity is `2026092001`; see [release notes](doc/releases/2026092001.md).
The package must be built from the exact committed/tagged source, signed with
the existing publisher key and accepted before publication. Unrelated untracked
images and the threaded-hole design note are excluded.

## Drawing conventions correction, 2026-09-20

The user clarified three conventions: omit mm from all Drawing length labels,
convert a linear dimension to a chain without requesting another point, and
place chain text perpendicular to the spine on the side opposite the witness
geometry. These are implemented through Drawing-only text formatting, an
explicit conversion action using the existing references, and the shared chain
layout for screen/export. Source model units remain untouched. Tests cover
conversion/Undo, both sides of vertical/horizontal/oblique labels, and implicit
millimetres with retained tolerances and angular degree symbols.


Drawing conventions validation: all five core/export tests and four GUI tests
pass (`build/drawing-conventions-core-tests.log` and
`build/drawing-conventions-ui-tests.log`). These include direct context-menu
conversion with two existing references and Undo, both text sides at three
orientations, hidden millimetres and retained tolerance text. The native root
launcher has been rebuilt.

## Drawing dimension follow-up, 2026-09-20

The user supplied `screenshots/01.png` (not `doc/01.png`) as the running chain
reference and explicitly requested an additional zero at the datum. Drawing
chains now show ordinates from one retained datum, a shared spine, one arrow per
target, perpendicular outside labels and a datum ring with literal `0`. Adding either end
preserves the datum; dragging moves the common spine. Native persistence and
screen/PDF/DXF share the resulting presentations. The break zigzag was sharpened
to 20-degree included angles on the user's subsequent request.

The preceding fixes for cyan selection after double-click, the title-block
10 → 12 → 8 mm master and Sketcher-style automatic two-point direction are
implemented. Their eight-test follow-up and the main-workspace Drawing check
passed (`build/drawing-followup-tests.log`, `drawing-followup-workspace.log`).
The older drag assertion now exercises free movement inside the dimension span,
without crossing the automatic outside-text clearance boundary. The unrelated
3D text-clearance issue remains outside this follow-up.


Final follow-up validation: 13/13 targeted contracts pass in
`build/drawing-chain-final-tests.log`, including the complete Drawing GUI,
measurement GUI, Sketcher, title-block, view breaks and PDF/DXF command checks.
The root launcher's main-workspace Drawing check also passes in
`build/drawing-chain-workspace.log`. The native application has been rebuilt.

## Drawing projection and view breaks completed, 2026-09-20

The interrupted view-break work was recovered and completed together with the
user's revised Drawing-dimension contract. Drawing guides now use horizontal/
vertical 2D rectangles around the displayed projection. Drawing-created dimensions
measure in the projection plane; transferred model dimensions keep their original
values and references. Both snap to the same 2D guides. Part/Assembly spatial
frames remain unchanged.

View Properties has compact groups with labels above adjacent fields, including
a third angle for in-plane rotation. New base/projected views receive numbered
names (`Pohled 1`, etc.); custom names survive. Moving a view on the sheet retains
its dimensions. The user explicitly approved removing Drawing-created dimensions
when its orientation changes, including affected projected descendants. This is
transactional: Cancel preserves them and Undo restores them with the old camera.

Breaks have an independent internal editor with draggable boundaries, numeric
position/length/gap, straight/zigzag/no marks and a result preview. Native
persistence, unchanged measurements, hidden references, clipping and exports are
implemented. Enter in the shared inline numeric field commits only that number,
not the enclosing Properties transaction.

The root Linux launcher uses the rebuilt native application. Twelve targeted
contracts and the main-workspace Drawing check passed the initial acceptance.
The subsequent follow-up fixes title-block master dimensions and cyan selection
after dimension Properties, and adds Sketcher-style automatic two-point direction
selection during placement. The older colour assertion now scans the actual
antialiased glyph region. The unrelated 3D text-clearance failure remains
recorded in [Drawing acceptance](doc/DRAWINGS.md#drawing-projection-and-break-acceptance-2026-09-20).
See [view breaks](doc/DRAWING_BREAKS.md) for the completed interaction contract.
No release was published and no commit was requested. Unrelated untracked
`doc/01.png`, `doc/02.png` and `doc/AI/THREADED_HOLE_DESIGN.md` were left untouched.

## Application lifecycle follow-up, 2026-09-19

The main workspace now protects application exit with one Save All / Discard /
Cancel confirmation for all unsaved owners, including source documents and
drawing templates. Pending edits and failed saves keep the window open.
Language changes offer a complete workspace restart, retain the old translator
until restart, and reopen saved documents using the same configuration source.
Update installation retains exclusive ownership of its own restart.
See [the lifecycle contract](doc/APPLICATION_LIFECYCLE.md).

Seven targeted native/GUI contracts passed; final lifecycle and Updates UI
checks passed again after the update-handoff guard. Evidence is in
`build/lifecycle-*.log`. Signed Windows release 2026091903 is published and
verified; see [acceptance](doc/releases/2026091903.md). The lifecycle GUI
contract also passed from the final package. Public hashes, production trust
and update discovery from 2026091902 passed. Parts 2026091902 is also published
with native ZIMA-CAD parameter import; that earlier authorized follow-up is done.
Local GUI testing used the `lifecycle-test` output directory because the user's
normal development executable was still running. The CMake output override is
removed; rebuild the normal development executable after the user closes it.

## Sheet follow-up verified on 2026-09-19

Twisted Sheet creation waits for the first reference before showing its wire
and operation axis. The axis is centred on the starting section. Attached
geometry persists its independently derived material side; the user-provided
profile-side case reproduced a one-thickness shift and is now a native fixture.
Both endpoints and chains of up to three Sheet Profiles pass calculation,
Unbend, Bend Back and persistence. The shared placement solver was not changed.
Part INI 41 / JSON 65 and the regenerated start Part carry the side property.
The GUI New-document contract passes; Assembly/Drawing formats are unchanged.

Unbend/Bend Back expose exclusive All and individual-selection checkboxes;
All is checked by default and the active choice cannot be unchecked in place.
Rotated Sheet Cut checks pass on planar and cylindrical material, including
both directions, analytic removed-volume checks, native persistence, cold
regeneration and state changes. No Sheet Cut implementation repair was needed.
Fifteen distinct focused contracts passed; see [Sheet Metal](doc/SHEET_METAL.md)
for evidence. The local GUI/CLI are rebuilt. This follow-up is not a new release.
The original `Projects/01.prtz` is unchanged; the calculated repair is
`Projects/test/01-twist-repaired.prtz`.

The subsequent ZIMA-CAD parameter import task in the sibling ZIMA-CAD-Parts
repository is complete, following its existing Pro/E metadata mapping.
Tests, documentation, commit and push are complete; Windows 2026091902 is
published in that repository.

## Windows release published on 2026-09-19

Signed Windows build 2026091901 is published as the latest stable release at
[GitHub](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026091901).
The immutable tag identifies source commit
`45f1eb05c2bfeb2619dd9719a3b0583da830beac`. Candidate and signed-archive smoke,
fresh bootstrap trust, all three public asset digests, and production update
discovery passed. A disposable 2026091704 installation offers 2026091901 as
installable, and 2026091901 reports current. The user's installation was not
updated. See [acceptance and known limitations](doc/releases/2026091901.md).
The development BAT still launches the current local build. User-owned
untracked `config/config.ini.3`, `config/config.ini.4` and
`sketch-offset-view.png` were preserved and excluded from the release.

## Local changes verified on 2026-09-19

The follow-up Body ownership work adds a native `DerivedCopy` history feature:
Mirror/Pattern created with an active Body remain inside that Body and use its
Boolean chain. Root Part copies remain independent Bodies; Assembly copies
remain immediate components. Overlapping Pattern operands are fused before
combining them with the input Body. The common placement solver is unchanged.

Geometry side identity is a mandatory agent rule in `AGENTS.md`; see
[Geometry side contract](doc/GEOMETRY_SIDE_CONTRACT.md). Bend deliberately uses
signed zero for opposite endpoint offsets. Cache normalization is limited to
equivalent rigid-frame values; authored offsets retain their sign bits.
New checks cover signed-zero persistence and distinct directed cache entries.

The local GUI and CLI are rebuilt; `zima-cad.bat` still launches the current
development executable. `VERSION` is 2026091901 for the requested Windows
release; its packaging/publication record is maintained in
[the release notes](doc/releases/2026091901.md).

The broad native regression run passed 155 of 156 checks in
`build/cache-preservation-broad-tests.log`. Only the previously reproduced
standard-title master-dimension failure remains. The complete derived-copy
command suite, Assembly zero-side matrix, Bend signed-zero persistence,
projected-dimension signed-zero editing and CLI process suite pass.
Ten focused GUI contracts pass in `build/inbody-gui-tests.log`. The additional
dimension-entry GUI regression initially exposed Qt focus-out erasing `-0`;
the fixed dialog passes in `build/geometry-side-fix-tests.log`.
Packaging tests pass (nine cases, two Linux-only skips), and all sixteen
publisher gate tests pass. Final release builds and acceptance logs use the
`build/release-1901-` prefix.

The final local build reports version 2026091901. All twelve final GUI suites
pass in `build/release-1901-gui-tests.log` (163 seconds), including the repaired
signed-zero dialog, copy ownership, Assembly properties/refresh, Drawing sources,
Family Table, native templates, numeric fields and Sketch offsets.

The first-open file-list delay remains unconfirmed. Fresh-process probes loaded
the repository directory in 0.36 s and Projects in 0.61 s; these measurements
do not reproduce or explain the user's reported cold-start delay.

- Mirror/Pattern accept only their own placed Origin planes/axes in Parts and
  Assemblies, including repeated nested occurrences. Double-clicking a derived
  child shows source Edit dimensions without opening source Properties.
- Drawing Settings manages registered native sources with staged removal,
  consequence confirmation, zero-source support and Undo/Redo. The Source
  chooser is independent of the title/BOM source captured on title insertion.
  Navigation icons follow the selected Part/Assembly variant. See
  [Drawing sources](doc/DRAWING_SOURCES.md).
- Parameters, Family Table and Relations have distinct icons; Family Table
  follows Parameters in Tools. Czech start-template parameter labels are
  lowercase without diacritics, with matching standard title-block expressions.
- The obsolete splash SVG was removed. The native application contained no
  remaining splash implementation. Native Sketch offsets were verified;
  STEP offsets are explicitly outside the user's requested scope.

All ten focused Drawing/native-document/Family Table/template contracts pass
in `build/drawing-settings-acceptance-tests.log`. They include actual GUI New
Part/Assembly creation, enabled commands, Czech parameter labels, source
registration/removal, missing files, zero sources, confirmation Cancel, Settings
Cancel, MMB confirmation, Undo/Redo, persistence, navigation and title binding.
Mirror/Pattern GUI and Sketch offset GUI pass in
`build/drawing-settings-gui-recheck.log`; the related kernel/command checks pass
in `build/drawing-settings-unit-tests.log`. The final Drawing UI screenshot is
`build/drawing-settings-preview.png` and was visually inspected.

Two broader checks are still failing and must not be reported as passing:
`zima_cpp_drawing_contract_tests` cannot resize the standard title sketch's
`d1` master from 10 to 12 mm; the same failure was reproduced with the unchanged
HEAD template in `build/template-baseline-check`. The full workspace startup
contract stops at “Universal Dimension did not enter angular placement after
two segments”. These are separate from the focused acceptance suite above.

Build diagnostic: this machine's localized MSVC include prefix was incorrectly
recorded by CMake, producing objects with zero header dependencies and stale
binary layouts. The generated compiler configuration now matches the actual
UTF-8 prefix `Poznámka: Včetně souboru: `; affected sources were rebuilt and Ninja
header dependencies verified. A future fresh configure should verify this
prefix before trusting incremental builds. Existing native dependencies were
retained (`VCPKG_MANIFEST_INSTALL=OFF` in the local build cache).

## Current application

Development build **2026091512** distinguishes **Body measurement** (Czech
**Měření tělesa**) from the existing Body container's **Body properties** dialog.
The measured point is labelled **Center of gravity** (**Těžiště**) and its exact
frame is highlighted in azure immediately when the inspector opens, including
with ordinary Origins hidden. Selecting its Tree row highlights the same frame.
Only explicit **Save** inserts or updates a measurement. OK, Cancel, window close
and middle-button double-click close without storing pending changes, matching
Measurement. The Body's actual Origin and placement are independent.
See [Body measurement](doc/BODY_PROPERTIES.md).

Acceptance for `2026091512`: the five targeted contracts pass (measurement
inspector UI, shared UI, body properties, measurement commands and translations)
in `build/body-measurement-tests.log` and
`build/body-measurement-translations.log`. The inspector test used a temporary
copy of `Projects/01.prtz`; the original SHA-256 remained
`D0D6A98CD4762A9C18538ECB2101B63D408F70D7728BA4CDDD50CC861B6E7B5B`.
GUI/CLI and affected test targets build successfully in
`build/body-measurement-build.log`; the final GUI rebuild is recorded in
`build/body-measurement-startup-build.log`. The source/Tree centroid captures
listed in the feature document were visually inspected.

The broader `zima_cpp_workspace_startup_contract` now **passes** in
`build/startup-insertion-tests.log` (104.42 s). Its earlier insertion failure was
a fixture sequencing error: finishing Assembly Sketch returns to its pending
Properties, and the test tried to insert a component without first confirming
that window. The fixture now checks for the returned Properties and clicks OK
before insertion. The production insertion guard and Family Table behavior were
correct and remain unchanged. An empty Family Table inserts the native model
directly; at least one instance opens the variant chooser. The preceding fixes
also updated obsolete Global Settings field-count and unsaved-tab expectations.
Final GUI rebuild: `build/startup-insertion-build.log`. This follow-up changes
verification and documentation only; development build remains `2026091512`.
The separate Family Table GUI scenario also passes (4.64 s) in
`build/startup-family-insertion-tests.log`, with `ZIMA_VERIFY_FAMILY_ONLY=1`.

Development build `2026091511` added **Balloons** (Czech **Pozice**) to the
right Drawing toolbar. Show all labels the first BOM level, including whole
subassemblies; Erase all retains hidden balloons and their placements. Manual
creation, reference replacement, purple center/endpoint grips and shared
Properties use one Undo transaction. Text defaults to 5 mm on paper. Native
storage and PDF/DXF/JPEG output are covered in [Drawing balloons](doc/DRAWING_BALLOONS.md).

Measurement and Body properties now remain before Insert Here and survive later
features in an active Body. The tree must identify the actual Body role, because
the insertion marker carries the same object ID. Body properties offers Hide/Show
in its context menu. Scrollable results keep the footer accessible in small
windows. The explicit Save behavior above supersedes its former OK-to-save behavior.

Current formats are Part INI 26 / payload 50, Assembly INI 22 / payload 34 and
Drawing INI 17 / payload 9. Part/Assembly start templates are unchanged in this
revision. The root `zima-cad.bat` still launches the development executable.
The published Windows release remains `2026091508`.

Acceptance for `2026091511`: all 15 selected GUI, CLI, native-document, rendering, translation,
measurement and balloon contracts pass in `build/balloons-acceptance-tests.log`.
GUI/CLI and affected test targets build successfully; final build log:
`build/balloons-acceptance-build.log`. The CLI process fixture now follows the
already implemented solid-source and bound Family Table contracts. Captures
`build/balloons-ui.png`, `build/balloons-ui.png.dialog.png` and
`build/balloons-measurement-ui.png.mass.png` were visually inspected.

## Previous development and published application

Development build `2026091510` introduced historical Body properties: volume,
surface area, centroid, mass and central inertia at the record's insertion
boundary. The displayed Origin follows the centroid and has editable rotation.
Its ten selected contracts passed across `build/body-properties-tests.log`
and `build/body-properties-final-tests.log`; build log:
`build/body-properties-final-build.log`.

Development build `2026091509` adds the native/instance chooser to Assembly
insertion and **Replace…** to an immediate component's context menu. Replace
preserves occurrence identity and mates; missing references remain stored and
mark the component red for repair. Cold and nested Assembly loading distinguishes
variants sharing one parent file. The shared placement implementation is unchanged.

Drawing main-view Properties now starts with a source dropdown containing the
generic model and all family rows. OK replaces the source of that view and its
projected descendants in one Undo step, retaining independent main views. An
unevaluated row is calculated privately only on OK and its packet is published
to the parent after the full Drawing edit succeeds. Save the parent to persist
that newly evaluated row. Part, Assembly and Drawing format versions are unchanged.
See [Family Table](doc/FAMILY_TABLE.md) for the interaction and persistence contract.
Build `2026091509` is local development; the published Windows release remains
`2026091508`. The repository-root `zima-cad.bat` launches the development build.
Verification covers 17 distinct selected core/GUI contracts; final source-error
and GUI corrections pass in `build/family-drawing-replace-final-tests.log`, with
cold unevaluated-row persistence in `build/family-drawing-replace-cold-tests.log`.
The Family Table document records the full log list and independent geometry checks.

Published build `2026091508` implements linked Family Table ownership and
multiline Drawing Text. Ordinary Save stores all family data in one parent native
file; Save As from an instance creates an independent copy. Row-controlled edits
update that row, other edits update shared history and all evaluated variants.
Undo/Redo belongs to the parent. Stable instance IDs survive renaming and Drawing
references resolve the row from the common parent file. Nested families are
rejected. See [Family Table](doc/FAMILY_TABLE.md) and [Drawing text](doc/DRAWING_TEXT.md).
Part INI 25 / payload 49, Assembly payload 33 and Drawing INI 16 / payload 8 were
used by signed Windows release `2026091508`, with candidate and
final archive smoke checks passed. The production updater in signed `2026091505`
verified the public manifest and offered the new version as installable. See
[release acceptance](doc/releases/2026091508.md) for the exact commit, hash and logs.

Development build `2026091506` repairs Mirror/Pattern source selection by editing
scope: own solid features in an active Body, Bodies and solids at Part level,
and immediate components in Assembly. Linear and circular Pattern use the same
picker and persist the exact selected solid. Subtractive solids repeat their cuts
in the source Body. See [Mirror and Pattern](doc/MIRROR_AND_PATTERN.md).
That revision used Part INI version 23 and Assembly payload version 31 to persist
the inherited subtraction state; the current versions are listed above. These
Pattern changes are included in Windows stable `2026091508`.

Acceptance: GUI/CLI builds and all eight related contracts passed, including
actual View picking, RMB cycling between a solid and its Body, circular/subtractive
GUI commits, native reload and cold regeneration. See the verification section
in [Mirror and Pattern](doc/MIRROR_AND_PATTERN.md). The subsequently authorized
Family Table task is implemented above. Sketcher offsets remain later in the queue.

Version `2026091505` adds **Settings > AI** and `codex` mode in the
desktop CAD console. It uses the user's own ChatGPT account through the native
Codex App Server, following ZIMA-CAD-Parts. Each request follows the active Part,
Assembly or Drawing and its active occurrence; a changed tab, selection or
revision invalidates pending commands. Changes use inline approval and the shared
CAD command host. See [AI console](doc/AI_CONSOLE.md) for setup, protocol checks,
privacy and the remaining account-connected acceptance step.

The model/drawing navigation button now shares the main toolbar's green hover and
pressed feedback. Wheel navigation with both Perspective and Fly enabled now
matches the ordinary zoom direction. Both wheel directions passed in all four
projection/navigation combinations.

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

Previous published build ID: `2026091504`; release notes are in
[2026091504](doc/releases/2026091504.md). The earlier `2026091503` Windows candidate from commit
`3d7eaaf89bdd6a8a7f47081ae587c2a8c449d6cc` passed the full package smoke and archive
checks; the exact SHA-256, local output locations and test scope are recorded in
[Native distribution](doc/NATIVE_DISTRIBUTION.md#windows-verification-on-2026-09-15).
The unpacked local portable installation holds versions `2026091503` and
`2026091502` and preserves shared `config/`. It is an unsigned local candidate,
not a GitHub release. The signed `2026091504` package is stored separately in
`.dist-output/release-2026091504/`. It passed candidate and finalized-archive smoke,
production bootstrap verification and the packaged Updates GUI contract.
See [signed build acceptance](doc/NATIVE_DISTRIBUTION.md#signed-windows-build-2026091504)
for its exact commit, SHA-256 and logs. The user-approved
[Windows release](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026091504)
was published on 2026-09-15 at 14:11:02 UTC with all three verified assets. Public
updater discovery reports version `2026091504` as available from a disposable
older-version selection (`build/updates-public-release-04.log`). Linux acceptance
remains assigned to Linux.

Current stable Windows build `2026091505` was published with user approval on
2026-09-15 at 15:00:15 UTC:
[GitHub release](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026091505).
The clean tagged source is commit `a2a5b1a6910e8d938bc493d45c8a9ab4140f04be`.
The signed ZIP, native smoke, bootstrap trust and packaged AI/Updates Settings
checks passed, and all remote asset hashes match the accepted files. The previous
production updater offers the new build as installable and passed a public
download/preparation check in a disposable signed `2026091504` installation.
Shared settings, the test project and the selected previous version were preserved;
the prepared update was not activated. See
[2026091505 acceptance](doc/NATIVE_DISTRIBUTION.md#signed-windows-build-2026091505)
for the exact hash, byte size and logs. The development BAT keeps its current
local build; release publication does not switch its launcher.

## Application updates

The 2026-09-15 update request is implemented using the current ZCP design adapted
to native CAD/CMake. Settings has General/Updates/AI tabs. Startup checks run in a
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
evidence of Linux runtime support. The first signed Windows release is now public.

## Work order

**2026-09-19 correction:** the user confirmed that native Sketcher offsets are
already implemented and that STEP-derived offsets are not required. Current
work prioritizes reported Part defects. This supersedes the historical offset
follow-up below; do not schedule STEP offset work or repeat that reminder.

On 2026-09-15 the user confirmed this immediate order: finish the Holes preview,
diameter annotation and display-mode icons; then complete program updates;
then continue Sketcher offsets, especially external and STEP source curves.
Updater implementation, Windows signed-bundle acceptance and the explicitly
approved publication are complete. The user then prioritized AI integration in
the console, following ZIMA-CAD-Parts, with the active Part/Assembly/Drawing tab as
context. That integration is implemented and published for Windows in `2026091505`.
Live authenticated AI behavior remains to be tried after the user signs in through
Settings > AI. The former offset follow-up is superseded by the correction above.

New explicit user instructions take precedence. The agreed Part sequence is in
[ROADMAP.md](ROADMAP.md#agreed-next-steps-for-part-2026-09-06). Native offsets are
implemented and STEP-derived offsets are outside the required scope. Current offset support is
in [SKETCH_OFFSET.md](doc/SKETCH_OFFSET.md); review remaining practical coverage
before treating the historical plan as unimplemented work. The comprehensive
Undo/Redo audit stays after the agreed modeling features are broadly implemented.
AI uses the shared commands; it does not introduce an independent modeling path.

Preserve current user configuration and unrelated working files. Required native
fixtures under `tests/fixtures/cross_language` are still consumed by C++ tests;
the historical directory name is not a reason to delete them.
