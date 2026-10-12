# Linux build and distribution handoff — 2026-09-15

## File Manager follow-up (2026-10-12)

The 2026101208 follow-up exposes only Overview and Files. Without localized or
generic Overview HTML, the native Overview shows the localized directory title,
logo and subdirectory cards, following the ZIMA-Parts automatic index. A single
click opens a card; native files remain in Files. Project metadata and the main
image appear in the same Overview, with editing available through the existing
context menu. Verify initial Files/Overview choice, directory/project navigation,
localized preview precedence and Light/Dark/Light on Linux. The engineering-table
snapshot now has complete five-language labels for all 312 directories and
reviewed concise English storage names. Empty INI labels no longer consume the
next line. Directory IDs, settings, protection and reference assets are retained.
The Windows checks do not establish Linux desktop or filesystem behavior.

The shared C++ application now includes the native File Manager, metadata
version 3, configurable data sources and native Part/Assembly parameter editing.
Build `zima_file_manager_ui_tests` and the product on Linux, then run the
standalone and integrated File Manager contracts in a native desktop session.
Verify multiple tabs, source trees, Project/Overview/Files views, native save
and reopen, metadata mirroring, application shutdown with an open filesystem
model and switching between CAD, File Manager and WebEngine. Confirm all five
languages and factory source paths in the packaged layout. No production project
data is part of the Windows fixture. OCCT remains 8.0.0 and no native document
schema changed.

The 2026101204 follow-up keeps every source row above the active tree, opens a
source root when its row is clicked, hides `0000-index` and adds a text filter
to each textual Files column. Confirm these interactions on the native Linux
desktop, including horizontal header scrolling and direct editing of metadata
and native parameter cells. Double-click must open an entry only from the File
name column. Windows candidate/signed smoke and packaged File Manager acceptance
pass; those results do not establish native Linux presentation or interaction.

The 2026101205 follow-up prevents all automatic native-parameter metadata writes
for sources configured as read-only. On Linux, browse a read-only source that
contains native Parts and verify that parameters remain visible and non-editable,
and that no `0000-index` directory or `metadata.ini` is created. Repeat the
writable-source edit and mirror checks to confirm that their behavior is
unchanged. Windows local, packaged and public release checks pass; native Linux
filesystem and desktop behavior still require Linux-host verification.

The 2026101206 follow-up adds the project-overview context action to the source
row itself and displays metadata-backed directory labels in both the Tree and
Files views. Verify project creation at the root of a writable source, the
disabled action for a read-only source, cached label refresh after editing and
the hidden `0000-index` directory. The shared factory data now includes the
filtered `03-ENGINEERING-TABLES` snapshot: 1,448 files, including 279 HTML
pages, and 312 metadata version 3 directories. Local web assets were copied
beside their pages, and links were rewritten to the short English physical
directory names. Linux must verify localized page choice, ordinary `index.html`
fallback, automatic tiles, SVG/PNG tree branding and browsing this committed
data from the packaged read-only source. Signed Windows 2026101206 passed the
packaged File Manager contract, archive inventory, native smoke and public
update discovery. Those checks do not establish Linux filesystem performance
or native presentation.

The 2026101207 follow-up separates explicit directory-metadata administration
from read-only source content. Directory Properties must remain editable for a
read-only source, while native parameter cells and Move/Delete remain disabled.
It exposes and persists `Directory/PreventRemoval`; the lock blocks removal or
moving of direct contents, the protected directory itself and any ancestor
containing a protected descendant. Verify localized label changes, the lock
checkbox, save errors on a physically read-only filesystem and retained copy-out
behavior. The engineering-table import already has the lock enabled in all 312
metadata directories. Signed Windows 2026101207 passed clean candidate and
signed smoke, packaged File Manager verification, both packaged table-tree
inventories and public updater discovery. Those Windows results do not establish
native Linux filesystem enforcement or presentation.

## Shared Feature follow-up (2026-10-12)

The next Windows follow-up uses the shared ordinary Feature command and native
parameters in Parts and Assemblies, including independent Axis Length/Up to/None
ends and transient owned Sketch editing. Assembly material operations remain
subtractions from immediate editable Part occurrences. Rebuild and run the
Feature parameter, Assembly profile, placement/cylinder, Axis extent and
five-language contracts on Linux, then the ordinary Axis, cylinder-mate and
inserted FORM GUI checks in the native desktop session. Verify the actual
STEP cylinder picker, movement, Flip and native persistence, and the FORM angle
witnesses in the sheet plane. Windows verification does not establish these
Linux desktop results. OCCT remains pinned to 8.0.0; use one matching Qt kit
for all Linux Qt modules and runtime plugins.

## Browser graphics follow-up (2026-10-12)

Windows 2026101201 selects the shared OpenGL Qt Quick graphics API before
QApplication to keep WebEngine and the CAD OpenGL widgets in one native window.
Qt Quick is now an explicit build dependency. Re-run the browser harness and
`zima_cpp_browser_workspace_ui_contract` in the Linux desktop session, including
native pixel composition, first opening with/without a CAD document, repeated
tab switches, resizing, Light/Dark/Light and the five-language settings width.
Offscreen tests explicitly skip native-screen pixel capture and do not verify
the window lifecycle on XWayland or Wayland. The Windows negative-control mode
`zima_browser_ui_tests --default-graphics-api` diagnoses the former mixed-API
configuration; it is not a supported product setting.

## Native Linux continuation (2026-10-11)

The integrated browser follow-up adds Qt WebEngine Widgets, WebChannel and the
Qt DBus development component required by the shared QtKeychain 0.15.0 backend.
Provision matching system Qt modules before rebuilding. Run
`zima_browser_bookmarks` and `zima_browser_ui` in the native desktop session,
including an available Secret Service or KWallet credential service. The Linux
packager now includes the WebEngine helper in its ELF dependency and license
closure, plus resource data and locale files. Verify actual web navigation,
downloads, password save/autofill/removal, profile persistence and all five
languages from the packaged runtime. Windows browser results do not verify
these Linux contracts. The ordinary Axis None controls and imported Assembly
closing/source-save behavior also require native Linux GUI verification.

The source through Windows 2026101106 has been fetched and the patched native
Linux SDK, product and test targets are built. The contact, current factory
FORM, retained copy, placement, persistence and desktop results are recorded in
[Linux build verification](LINUX_BUILD.md#native-continuation-on-2026-10-11).
That record explicitly retains failures and finite coverage limits. Large STEP
preparation completes on both supplied G92 fixtures. Large-model desktop
lifecycle/rotation, committed-source packaging, signing and native smoke are
recorded. Linux [2026101107](releases/2026101107.md) is published
at `2026-10-11T18:13:03Z`; trusted 2026100908 update discovery offers it as
installable. Packaged arc checks pass XWayland; two packaged Wayland attempts
remain explicit failures. Other observed failures remain open as listed;
do not interpret the older Windows-only sections below as current Linux results.

## Outward FORM candidate follow-up (2026-10-11)

After reboot/fetch, additionally run `zima_cpp_native_arc_axis_tests` and
`zima_cpp_native_arc_axis_ui_contract`. Repeat the 3280 native and 48 GUI
centre/end-to-axis/Origin cases in
[Native arc centre and endpoint contacts](SKETCH_ARC_AXIS_CONTACTS.md),
including no pair T, both T orders, reflected/rotated geometry, repeated R/D,
position and angle edits/drags, immutable external sources, trim, save/reopen
and Undo/Redo. Use the shared C++ implementation; Windows results do not close
this native Linux verification task. The user will perform this after reboot.

On 2026-10-11 the user selected only the latest completed library definitions.
Use `config/lib/01-SHEETMETAL/01-FORM/CornerGusset90.prtz` and
`VentilationWindow.prtz`. Both persist `form_outward`; the corner additionally
sets `form_corner_finish`, trims the actual outer Bend-radius domain and uses
ordinary native Fillets from the FORM–Bend junction, in the order `t` then
`0.25t`. The former inward controls are regression fixtures under
`cpp/tests/fixtures/form`, outside the picker. Repeat
`zima_cpp_factory_finished_corner_tests`, `zima_cpp_factory_outward_window_tests`
and the definition contract's exact two-entry library check. Custom libraries
and definitions independently embedded in existing Parts remain untouched.

The delivered corner now uses `FORM_CUT` as a closed solid preceding the linked
zero-thickness Shell in `FORM`. Do not use the rejected two-Sketch radius-depth
cutter as the final candidate: it overcuts sloped geometry in the Bend.
After Git fetch and the native Linux build, use the current canonical factory files
directly; reproduction helpers are `--prepare-corner-solid-cut` and
`--prepare-outward-form`/`--order-form-bodies`. Run the definition,
geometry, persistence/copy and GUI contracts on the Linux desktop. Repeat the
finite thickness/radius and support-side/rotation matrices, independent BRep
validation, full-thickness joint coverage, Undo/Redo, unchanged OK, replacement,
save/reopen, regeneration and manual Fillet selection. Record complete command
timings separately from kernel-only calculations. Include the Window 4 mm
thickening failure and its matched factory control, rather than omitting it.
See [the measured investigation](performance/20261011-corner-outward-surface-experiment.md).
Windows results are not Linux acceptance.

Repeat the finished nine-case thickness/radius matrix, independently measured
Fillet radii and two directed solid differences against the pre-Fillet
Cut/outward Thickness/quarter-cylinder Trim/Fuse construction. Repeat outward
Window signed-direction checks on both outer faces at 1/2/3 mm and 37-degree
rotation. Windows cold medians at 1 mm were 0.829244 s finished corner versus
1.115730 s retained corner, and 1.348830 s outward Window versus 1.279560 s
retained Window. These compare intentionally different styles, not equivalent
geometry. Record fresh native Linux observations rather than reusing them.

Also run `zima_cpp_native_tangent_contact_tests` and
`zima_cpp_native_tangent_contact_ui_contract`, the general Sketcher/C+K suites,
relation/dimension/curve/reference/serialization/Mirror-tangent commands and
common-tangent/dimension-entry GUI checks. Verify circle/arc C+T creation,
shared native arc T-only, independent equations after repeated R/D/position/
angle edits (including negative/wrapped inputs) and point drags, fixed supports, fully constrained rejection,
Undo/Redo, save/reopen, and Light/Dark/Light previews. The native 108-case
matrix and its finite coverage are in
[native tangent contacts](SKETCH_NATIVE_TANGENT_CONTACTS.md). The factory Part
documents were re-saved with the current serializer; use the packaged templates
and verify new-document active context on Linux. The user's original
`Projects/FORM-KOULE.prtz` remains unmodified; its Sketch is retained as a tracked
read-only regression fixture.

Also run `zima_cpp_shell_command_tests`, `zima_cpp_boundary_surface_ui_contract`,
`zima_cpp_surface_thicken_tests`, `zima_cpp_file_dialog_contract_tests`, and the
five-language contract. Verify preceding source selection in an empty Body,
retained solid, exact native child ancestry, translated/rotated Body frames,
source edits/suppression, forbidden forward/self dependencies, independent
inspection, clearing/replacement, Cancel, unchanged OK and save/reopen.
The file selector icon fix moves palette-aware rendering out of its worker
provider and uses model-owned provider lifetime. Repeat idle heartbeat and
Light/Dark/Light acceptance on Linux. See [linked surfaces](LINKED_BODY_SURFACES.md)
and [file-dialog evidence](performance/20261011-file-dialog-thread-ownership.md).

## Approved Windows/Linux parity follow-up (2026-10-10)

The user approved sequential investigation of the G92K1 crash, repeated STEP
reading, large-model rotation performance, and cross-platform calculated-model
behavior. Windows investigation and shared C++ changes come first. The user
will reboot this computer into Linux, fetch the committed changes from GitHub,
and continue Linux verification in that session. Do not install WSL or treat
Windows checks as Linux acceptance. Native Linux execution is explicitly pending.

The subsequent View/Tree follow-up restores palette-aware continuous hierarchy
guides and routes confirmed View containers through their exact Tree menu.
After these follow-up sources are committed and transferred, run
`zima_cpp_tree_branches_ui_contract`, `zima_cpp_history_deletion_ui_contract`,
`zima_cpp_history_deletion_tests`, `zima_cpp_ui_contract_tests` and
`zima_cpp_translations_contract`. Include Light/Dark/Light, arrow expansion,
selection, deletion confirmation/Cancel, dependent-reference state and
Undo/Redo. The native deletion fixture initializes owned Origins before its
exact serialized baseline; meaningful signed zeros remain checked.

The same follow-up adds independent C+K contacts on characteristic curve points,
restricts Fillet/Chamfer/Shell to calculated operation-input topology, and replaces
the Part application dropdown with direct Modeling/Sheet Metal buttons. After
reboot and Git fetch, build and run `zima_cpp_sketch_keypoint_contact_tests`,
`zima_cpp_sketch_keypoint_contact_ui_contract`, `zima_cpp_sketcher_contract_tests`,
`zima_cpp_curve3d_sweep_contract_tests`, `zima_cpp_treatment_selection_ui_contract`,
`zima_cpp_edge_treatment_ui_contract`, `zima_cpp_application_tools_ui_contract`,
`zima_cpp_new_document_options_ui_contract` and the five-language contract above.
Also run the Sketch relation/dimension/curve/reference/serialization/Mirror suites.
The finite matrix and remaining applicable variants are recorded in
[Sketch characteristic contacts](SKETCH_KEYPOINT_CONTACTS.md). Check the native
KDE/Wayland Light/Dark/Light Tree strokes for continuous right-angle branches,
mode-button order/icons, correct restrictions while Properties/Sketcher is open,
and C-only movement after deleting K. Repeat on the large G92H1-T input and
record command timings; Windows verification is not Linux acceptance.

Also retain the existing hidden-Body activation gap: running the desktop with
`ZIMA_VERIFY_BODY_ACTIVATION_ONLY=1` and `--verify-startup` fails at "Activating
hidden body displayed no original geometry" on both the local Windows follow-up
and immutable published 2026101102. Record whether Linux reproduces it; do not
claim complete Body activation acceptance from the passing focused fixtures.

The 2026-10-11 follow-up adds palette-neutral confirmed 3D Curve strokes and
owned points, retaining brown point-entry frames. Run
`zima_cpp_curve_presentation_ui_contract`, `zima_cpp_point_marker_colors_contract`,
`zima_cpp_feature_axis_highlight_contract` and `zima_cpp_translations_contract`
on Linux, including live Light → Dark → Light switching. The same neutral icon
is used by 3D Curve and Surface Intersection.

Dense-import reference validation now retains its entry-scoped cache instead of
discarding all entries at 64 candidates. The large-import GUI test compares the
complete ordered candidate list, forces fresh validation by publishing another
scene revision and verifies exact hover/RMB/click identity. Body confirmation
reuses its fixed reference index and is expected to publish one complete scene.
Repeat the real G92H1-T fixture, numeric reference changes, both curved placement
branches, inspection, Cancel and persistence on the Linux desktop. Windows picker
timings do not establish Linux timings or complete scene-publication performance.

The expanded Windows rigid-Body acceptance also found an existing failing
`zima_cpp_solid_state_spatial_sweep_document_tests` regeneration variant, reproduced
from clean `1676d62b` with the same SDK. Its independent nine-section geometry
check passes, but placement references do not converge during the lifecycle.
Retain and report this case in Linux verification; do not relax tolerances or
skip convergence validation. The measured record distinguishes this existing
gap from corrected test readers and the verified import movement contracts.

The user also approved the shared placement-reference geometry index on
2026-10-10. Build and run `zima_cpp_mesh_surface_placement_tests` and
`zima_cpp_curve_placement_contract_tests`: they compare the complete and indexed
solver inputs, alternative branches, measurements, signed zero and independent
geometry equations. Run `zima_cpp_import_interaction_ui_contract` and
`zima_cpp_surface_placement_ui_contract` on the actual Linux desktop, then repeat
large G92H1 hover, Body movement, reference edits, Cancel, Undo/Redo and reopening.
Compare exact offered/persisted identities and record cold and repeated timings
separately. The Windows cold-hover measurement is one face/model, not a Linux
performance promise. See the linked measured investigation.

Cache packing/unpacking now constructs recursive results directly instead of
copying each complete subtree before replacing its children. Run
`zima_cpp_cache_storage_contract_tests`, including the byte-identical previous
traversal oracle, then measure large native load/save on Linux. The shared
encoding, topology, coordinates, signed zero and document proof are unchanged.

The rigid-Body follow-up also retains immutable local archives/topology/history
and ordered placement/compound operands. Explicit kernel operations and STEP
export must materialize these through `read_calculated_body_shape`; readers
must not assume every root `kernel_shape` is a raw BRep. All operands remain
self-contained in the native document. Include the additional native contracts:

```sh
cmake --build build/cpp-release --parallel 8 --target \
  zima_cpp_body_command_tests zima_cpp_multibody_contract_tests \
  zima_cpp_reference_geometry_transform_tests zima_cpp_body_link_tests \
  zima_cpp_body_scale_tests zima_cpp_assembly_contract_tests
ctest --test-dir build/cpp-release --output-on-failure -R \
  '^zima_cpp_(body_command_tests|multibody_contract_tests|reference_geometry_transform_tests|body_link_tests|body_scale_tests|assembly_contract_tests)$'
```

Repeat complete large-import move/save/Undo/save/Redo/save/reopen, plus dependent
solid operations and STEP export. Check all local and displayed geometry,
properties, side flags and reference identities independently. The measured
Windows native confirmation excludes GUI publication and saving; its roughly
0.52–0.69 seconds is not proof of the user's 0.5-second interaction target.
Leave adequate free space for the source, temporary replacement and numbered
native-document archives when running repeated large-file persistence checks.

After the reboot:

1. Fetch the completed work from `ZIMA-Engineering/ZIMA-CAD`, record the exact
   source commit, and read this handoff plus the linked Windows investigation
   records. Preserve local configuration and projects when updating the checkout.
2. Build with the native Linux SDK and the same pinned OCCT 8.0.0 source. Record
   compiler, Qt and kernel versions; run the affected native and GUI contracts
   described by the completed Windows changes.
3. Open Windows-calculated native Parts and Assemblies without regeneration.
   Start with the factory `VentilationWindow.prtz` asset whose Linux/Windows
   fingerprint mismatch is documented below, then include imported geometry and
   dependent/external references. Opening or switching tabs must not calculate
   bodies, change authored data, or create an Undo transaction.
4. Verify native save/reopen and explicit regeneration in both directions.
   Compare the geometry, physical properties, persistent owners/semantic keys,
   occurrence paths, original-face ancestry and geometry-side choices at the
   affected history boundaries. Test both sides and zero offsets where relevant;
   preserve meaningful signed zero. Verify cache invalidation after real source
   changes and rejection of genuinely stale or corrupt calculated data.
5. Run representative STEP loading and rotation measurements on Linux using
   the same inputs and defined operation scopes. Record platform-specific
   timings separately. Do not infer speed or rendering correctness from Windows.
6. Record actual Linux results and any remaining gaps in English. Publication
   of a Linux version requires its own build identity and native packaging gates;
   an existing published Windows archive must remain immutable.

Keep one shared modeling implementation. Do not solve a fingerprint mismatch
by ignoring validation, reducing precision, globally normalizing numeric data,
or introducing Windows/Linux variants of modeling algorithms. The exact final
source commit, fixture paths and commands are recorded below.

### Receiving build 2026101101

Use the immutable source tag `ZIMA-CAD-2026101101` and record its resolved commit
with `git rev-parse ZIMA-CAD-2026101101^{commit}` after fetching tags. Read the
[Windows investigation](performance/20261010-step-gauss-shared-source.md),
[calculated-model protocol](CALCULATED_MODEL_PARITY.md),
[angular-text repair](DRAWING_ANGULAR_TEXT.md) and
[release acceptance](releases/2026101101.md). The same OCCT 8.0.0 patch SHA-256
is `2f500bc8e2e0346be3c6770351c3ac043d8b1a9da4a904aa4e829f3af45e6863`.
CMake rejects an unpatched or differently patched SDK. Preserve local settings
and Projects; the large user STEP files are not committed Git assets.

From the repository root, using the existing native prerequisites described
later in this handoff:

```sh
bash tools/distribution/build-linux-sdk.sh
(cd cpp && cmake --preset linux-runtime-release)
cmake --build build/cpp-release --parallel 8 --target \
  zima-cad-cpp zima-cad-cli zima-cad-drawing-harness \
  zima_cpp_native_documents_tests zima_cpp_step_model_contract_tests \
  zima_cpp_occt_gauss_interval_contract_tests zima_cpp_step_parallel_properties_tests \
  zima_cpp_import_model_contract_tests zima_cpp_dimension_layout_contract_tests \
  zima_cpp_sheet_form_definition_tests zima_cpp_sheet_form_command_tests \
  zima_cpp_sheet_form_geometry_tests zima_cpp_sheet_form_copy_tests \
  zima_cpp_sheet_form_copy_after_unbend_tests zima_cpp_ui_contract_tests \
  zima_cpp_cache_storage_contract_tests zima_cpp_mesh_surface_placement_tests \
  zima_cpp_curve_placement_contract_tests zima_cpp_placement_command_tests \
  zima_cpp_placement_reference_removal_tests zima_cpp_placement_reference_assignment_tests
ctest --test-dir build/cpp-release --output-on-failure -R \
  '^zima_cpp_(native_documents_tests|step_model_contract_tests|occt_gauss_interval_contract_tests|step_parallel_properties_tests|import_model_contract_tests|dimension_layout_contract_tests|sheet_form_definition_tests|sheet_form_command_tests|sheet_form_geometry_tests|sheet_form_copy_tests|sheet_form_copy_after_unbend_tests|translations_contract|cache_storage_contract_tests|mesh_surface_placement_tests|curve_placement_contract_tests|placement_command_tests|placement_reference_removal_tests|placement_reference_assignment_tests|import_interaction_ui_contract|surface_placement_ui_contract)$'
```

The tracked `cpp/tests/fixtures/parity/ventilation-linux.prtz` retains actual
Linux-calculated geometry and tests loading, exact definition validation and
save/reopen. Factory `config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz`
contains the refreshed Windows calculation. Open both without regeneration,
then explicitly regenerate and compare geometry, stored physical properties,
owner/reference identities and side choices. The current proof is required;
old cached files without it are intentionally unsupported. Do not patch around
this with a legacy adapter or an automatic calculation on open.

When the user STEP data are accessible under Projects/import, compare the
complete prepared packets:

```sh
mkdir -p build/import-followup
build/cpp-release/zima_cpp_step_model_contract_tests --import-preparation-file \
  Projects/import/G92H1-T.step 0 shared build/import-followup/g92h-linux.json
build/cpp-release/zima_cpp_step_model_contract_tests --import-preparation-file \
  Projects/import/G92K1.stp 0 shared build/import-followup/g92k-linux.json
sha256sum build/import-followup/g92h-linux.json build/import-followup/g92k-linux.json
```

Windows accepted packet hashes are documented in the investigation. Cross-OS
floating-point differences must be examined as geometry/properties and identity
contracts; a different packet hash alone is not proof of changed modeling intent.
G92K1 still has a documented Gauss/Gauss-Kronrod physical-property discrepancy;
do not silently reduce integration precision or claim that reported convergence
proves physical accuracy.

On the native desktop, verify new-template activation, complete imported Body
placement and Cancel, Extrusion open/Cancel, all five rotation modes with
selection/inspection, and Drawing angular text above its arc inside/outside the
angle and below the vertex. Include native save/reopen, Undo/Redo and PDF/DXF.
The offscreen test platform alone does not establish native OpenGL or mouse
interaction acceptance. Record actual Linux GUI evidence before publishing a
Linux build; allocate its own new build identity.

Also build `zima_cpp_silhouette_view_benchmark` and run the permanent
`zima_cpp_silhouette_connectivity_contract` on the actual desktop. Compare cached
and forced-fresh complete GPU data across all seventeen reuse/invalidation cases,
then exercise the large import's six Body poses. Verify the lazy complete
reference path with SurfaceIntersection, both Sweeps, surface editing and feature,
Body and Pattern dimensions. Record operation counts, cold-build costs and actual
command timings separately from forced-fresh diagnostics. Windows's eighteen
selected dependent contracts and 200 synthetic/thirteen product frame comparisons
have passed; these do not substitute for native Linux execution.

## Rigid Body archive and display follow-up (2026-10-10, pending verification)

The current Windows follow-up keeps a Body's calculated local geometry immutable
and records rigid placements separately from its opaque BRep archive. A final
Part compound retains ordered, self-contained Body operands. Only explicit
solid calculation/export materializes those operands in OCCT; native loading,
property entry and numeric preview must not reconstruct them with the kernel.
The native files continue to contain every required archive and reference.

`KernelShapeArchive` shares immutable archive bytes in memory through history
and Undo; native JSON still serializes their complete string value. Separate
Body inputs/outputs share one immutable packet. Assembly serialization keeps
its established shared geometry table, including compound operand identities.
A STEP rigid-transform fingerprint treats `+0.0` and `-0.0` as equal only in the
six resolved pose coordinates. It does not rewrite stored placement values,
reference offsets, side choices or directed dimensions.

Imported topology, opaque archive bindings and calculated local Body histories
also use immutable shared arrays. The scoped analytic-surface transform cache
retains its original inputs and reuses only an identical transform invocation.
The Body-command regression compares complete and requested-reference
preparation through full convergence for two imported Bodies, rotated source
geometry, both Flip states and both signs of zero. Curve and mesh-surface tests
compare the exact serialized packets of both index variants. Parts without
native, embedded or Section Sketch consumers skip preparing an unused external
Sketch source while preserving retirement of stale reference errors.

On Linux, rebuild all affected C++ modules and run the multibody, Body command,
STEP import/export, native document, cache storage, Assembly, Body reference,
Body link, Scale, Mirror/Pattern and sheet export contracts. Regenerate factory
templates and run the actual new-Part/new-Assembly GUI acceptance. Verify
complete world geometry after Body OK, Cancel, Undo/Redo and reopening, plus
both zero-offset sides and cylindrical/conical placement alternatives.
Compare the renderer's GPU data and frame output across all five display modes,
hover/confirmation, transparency and changed meshes. Report cold and repeated
Body numeric-preview and confirmation timings on the complete G92H1-T import;
the requested 0.5-second response remains an acceptance target, not an achieved
or cross-platform guarantee. Windows work and final release acceptance are
still in progress; Linux results must be recorded after the user's reboot.

## Imported-model performance and factory cache follow-up (2026-10-10)

The shared Windows 2026101001 changes preserve the full imported Body during
placement, index the existing viewer candidate geometry, freeze STEP source
geometry before one final history preparation, and remove unused Extrusion
lifecycle work. Run the focused import, viewer, profile-frame, measurement and
section contracts on the Linux host; the Windows measurements are recorded in
[STEP and Extrusion performance](performance/20261010-step-load-and-extrusion.md).

Windows rejected the Linux 2026100908 VentilationWindow calculation fingerprint.
Explicit Windows regeneration restores a loadable factory asset. Its definition
matches the preceding Windows definition, all persisted owner/key/occurrence
identity sets match at every boundary, and independent two-way OCCT cuts against
both previous Windows and Linux final shapes have zero area and volume. Linux
versus Windows cached coordinates differ by at most 1.43e-13 mm in this fixture;
the external source-point differences are about 7.11e-15 mm. Fingerprint checks
remain strict. Cross-platform native cache portability is still unresolved:
verify loading this Windows-calculated asset on Linux before claiming acceptance.
No platform-specific modeling algorithm, schema or kernel pin was introduced.

## Linux preview publication (2026-10-10)

[Linux preview 2026100908](releases/2026100908.md) is compiled, signed and publicly
available as a GitHub prerelease. The record lists the accepted packaging,
native, localization and KDE/Wayland checks, the refreshed native Form library
cache, and the remaining native and GUI failures. It is not full Linux feature
acceptance. GNOME verification remains outstanding; the Windows follow-up above
records rejection of the Linux-calculated fingerprint. At preview publication,
the latest stable release was Windows 2026100906; do not promote
the Linux preview into stable updates without resolving its acceptance limits.

## Closed-solid corner Form follow-up (2026-10-09)

The shared implementation adds the native `CornerGusset90.prtz` definition,
two outer-face placement at a 90-degree Bend, automatic closed-solid cutting
and Shell. The current symbolic wall is `t/2`, the cavity transition is `t`,
and the opposite projecting transition is `t/4`; uniform thickness was explicitly
relaxed by the user. Stock thickness is unchanged. Run the corner native
roundtrip/copy fixture, its GUI fixture dependency, four supported thickness
pairs and five tight-bend acceptance cases (`Ri = t`, `t = 0.5, 1, 2, 3, 4` mm).
The compact source profile is 16 mm wide and 12 mm deep. Verify both side/orientation
choices, embedded definition replacement, Unbend symbols, Bend Back, Pattern,
Mirror, exact BRep validity and independently calculated volume/tensor data.
Run `zima_cpp_corner_form_workspace_copy_tests` and the extended corner GUI
contract, including default X, reverse/both/symmetric distributions, Mirror,
unchanged OK, failed-copy atomicity and save/reopen. Check the source-centroid
axis and copied axis identities, short physical extents, Drawing annotations
and suppression of corner geometry, axes and symbols during Unbend in View and
Drawing. Check copies created after Unbend, restored axes on Bend Back, and
parameter seams/smooth joints hidden in the Pattern/Mirror preview.
Run the eight `zima_cpp_ordinary_copy_batch_*` checks and both ordinary derived
copy GUI contracts. Disjoint operands use a compound; overlapping or touching
bounds retain sequential handling. Compare complete topology identities,
material-side metadata, axes, properties and two-way geometric differences.
Corner Form batching was measured and rejected on Windows; it is not enabled.
The copy preparation uses
the exact removed/added material delta; no tolerance has been relaxed.
Run the ordinary Form/Shell/Fillet/Thicken Surface suites too. There is no kernel
pin, native schema or protected shared placement change.

Also run the connected equal-length edit/drag matrix, FORM-EDGE hover/draft
Undo/Redo GUI regression and exact external spline/native endpoint tests.
English documentation and all five catalogs include the corner placement/error
labels. Windows observations do not establish Linux native or GUI acceptance.
The revised library passes those finite `Ri = t` cases on Windows using the
existing quasi-angular circular Fillets. Independently measure the exact
transition surface curvatures, including after save/reopen and regeneration.
This does not
establish arbitrary-profile support. Verify View offset editing in open and
closed Properties, clearing/restoring the first support, disabled zero offset,
Cancel, Undo/Redo and save/reopen. Also run the metadata suite covering sheet
thickness changes while a downstream feature is already failed.

## Sketcher and planar-limit follow-up (2026-10-08)

The shared source for Windows 2026100804 adds paired model/Drawing Save Copy,
Sketch Mirror, common circle tangent branch selection, persistent C+T contacts,
FORM-EDGE dimensioned-arc edits/drags, and planar Up To contacts at zero distance.
Run the new `zima_cpp_sketch_mirror_tangent_tests`, extrusion-limit, Thin/Surface,
centerline and 2D/3D/helical Sweep suites with OCCT 8.0.0. Run Mirror/Common
Tangent GUI contracts, including the native fixture environment paths in CMake,
external-reference/dimension-entry/endpoint/Offset/FORM Sweep GUI contracts,
paired Save Copy and all five localization catalogs. Inspect the actual cyan wires
on the tilted FORM-EDGE Sketch, all Thin sides and Cancel/save/reopen behavior.
The Windows release matrix and finite coverage limits are in
[2026100804](releases/2026100804.md). Linux acceptance remains unverified.
No dependency pin or native schema changes are required.

## Development SDK alignment (2026-10-06)

The next Linux acceptance must also cover shared source tag
`ZIMA-CAD-2026100802`: initial XY/Default Origin frames, preserved manual/XZ
frames, Sketch-to-Sweep conversion, ordinary Sketch rollback and nested
Part/Assembly drag/Trim, Fusion properties sizing after owner resize, Drawing
Origin display and the empty-workspace ZC background. Run the work-plane and
Sweep native suites, profile-frame/Sketch-return/external-reference GUI suites,
the five-language dialog layout matrix, Drawing view/source-picker suites and
actual new-document template checks on the Linux host. The factory Ventilation
Window is now authored in XY; verify native FORM role order, geometry, insertion
and persistence too. Windows acceptance does not establish KDE/Wayland or GNOME
acceptance for these changes. No dependency pin or native schema changes.

Linux development now uses OCCT 8.0.0, matching the pinned Windows dependency.
The 7.x surface-intersection branch was removed; both platforms share the same
modeling implementation. SDK/application builds, focused contracts, actual GUI
scenarios and a relocated native smoke passed, with existing/configuration-
dependent test limits recorded in [Linux build verification](LINUX_BUILD.md#occt-alignment-verification-on-2026-10-06).
The root development launcher selects the rebuilt OCCT 8 application. This does
not replace the immutable signed 2026100601 archive, which uses OCCT 7.9.3.
Signed 2026100602 now includes this alignment and passed its independent release
acceptance gates.

## Current signed release (2026-10-06)

[Linux 2026100602](releases/2026100602.md) is signed and public. It ships OCCT
8.0.0 and the shared Windows/Linux surface-intersection implementation. Clean
committed-source packaging, current template byte checks, candidate/signed smoke,
production trust, packaged Templates/General Surface/Intersection/Trim GUI checks,
remote asset hashes and authenticated update discovery from 2026100601 passed.
Accepted assets are under `.dist-output/linux-0602-signed-final/` and on GitHub.
See the release record for inherited regression limits. GNOME remains unverified.

## Previous signed release (2026-10-06)

[Linux 2026100601](releases/2026100601.md) is signed and public. It includes the
current surface modeling commands and a small OCCT SDK API adaptation for the
pinned Linux 7.9.3 SDK. Candidate/signed native smoke, production trust, focused
geometry/localization checks, packaged templates and three surface GUI scenarios,
remote asset hashes and update discovery from 2026093001 passed. Exact scope and
the extended Boundary Surface GUI fixture limitation are recorded in the release
record. Durable accepted assets are under `.dist-output/linux-0601-signed/` and
on GitHub. GNOME acceptance remains unverified.

## Previous signed release (2026-09-30)

[Linux 2026093001](releases/2026093001.md) is signed and public. The existing
encrypted publisher key was successfully unlocked through a local password
dialog using the unchanged finalizer. Candidate and signed smoke, production
trust, packaged Drawing checks, public asset hashes and update discovery passed
on Debian 13 x86_64 and KDE/Wayland. Temporary preparation paths are disposable;
the durable signed assets are under `.dist-output/linux-3001-signed/` and on
GitHub. See [publisher procedure](UPDATES.md#publisher-procedure) before the next
release. The historical 2026092907 signing preparation is superseded.

## Next Linux acceptance: explicit Fusion themes

Windows source tag `ZIMA-CAD-2026093005` introduces Fusion Qt widgets and the
shared `Application/Theme=light|dark` setting. The default is Light; the OS theme
does not select it automatically. View, Sketch and Drawing keep their technical
ISO font. This is implemented cross-platform, but its Linux desktop appearance
has not been verified by the Windows release run.

On the next native Linux build, run the font/theme GUI contract and the affected
Sweep, Pattern and Drawing GUI checks. Inspect Light/Dark, localized labels,
OK/Cancel and persistence on the supported KDE/Wayland and GNOME environments.
Qt widget styling is shared; system GUI fonts and native window decorations
can still differ. Use the current sweep result-mode/native-template checks too;
Helical feature data now requires result type, thickness and side fields.

## Supported desktop scope (2026-09-27)

The user limited Linux desktop support and verification to **KDE Plasma and
GNOME**. Other desktop environments are outside the supported scope. The next
desktop-integration work must cover application launch, native document file
associations and application/document icons in both supported environments.
Record the tested desktop and session versions on the Linux host; Windows
verification does not establish either environment's acceptance. This is a
scope requirement for follow-up work, not a claim that registration or icon
integration has already been implemented and verified.
The agreed first-launch/Settings workflow is recorded in
[Desktop integration](DESKTOP_INTEGRATION.md).

## Desktop integration implementation handoff (2026-09-27)

The native shared service now has a Linux backend in
`cpp/app/desktop_integration.cpp`, an internal first-launch offer, a Settings
page, and `--desktop-integration=register|repair|remove|status`. Installed
runtimes target the root `ZIMA-CAD.sh`. The backend writes user-local desktop,
MIME and SVG resources, preserves default-app choices, and refreshes caches
with `update-mime-database` and `update-desktop-database`.

This backend has not been compiled or accepted on Linux. On the Linux host:

1. Build the GUI and `zima_desktop_integration_tests` with the native toolchain.
2. Validate the generated desktop entry with `desktop-file-validate` and the
   MIME package with `update-mime-database` in an isolated XDG data directory.
3. Test first-offer OK/Cancel, registration, repair, removal and Settings in
   separate KDE Plasma and GNOME sessions; record session and desktop versions.
4. Open all five supported native types from the file manager, including
   paths with spaces and Unicode, and inspect application/document icons.
5. Verify pre-existing default choices, two installation roots, version
   switching, relocation and portable operation. Remove an old location's
   registration before moving it; registration does not adopt another root.

Windows registry and GUI results do not replace these gates. No Linux release
or acceptance is claimed by this handoff.

## Native Linux follow-up (2026-09-17)

The repository now has a pinned OCCT SDK builder and a committed-source Linux
candidate builder. Follow [Native Debian build and release](LINUX_BUILD.md).
The historical handoff below describes the starting state; actual acceptance
results are recorded in [Linux candidate 2026091701](releases/2026091701.md).
The signed 2026091701 package is now public. Finalized native smoke, production
bootstrap trust and public update discovery passed. The key-transfer and signing
steps below are historical; do not repeat them for the immutable published archive.

## Read first

- [Binding distribution requirements](DISTRIBUTION_CLEANUP_PLAN.md)
- [Portable release entry point](PORTABLE_RELEASE.md)
- [Windows dependency and acceptance rules](WINDOWS_RUNTIME_AND_BUILD.md)
- Repository `AGENTS.md`, including the mandatory English documentation rule.

The user explicitly chose to finish Linux from Linux. Do not infer that Windows
verification establishes Linux support. No Linux package or updater was built
in this session, and no release was published.

## Starting state

C++ is the only application. The obsolete Python application, Python-only tests,
launchers, Conda packaging scripts and benchmark were removed at the user's
request. Git history retains their original source. Keep active C++ native
fixtures, tracked `config/` templates/resources and user Projects.

Windows uses the pinned vcpkg manifest in `cpp/vcpkg.json`. GUI and CLI rebuilt;
console UI verification passed 1/1 after the terminal icon change. Regenerate is first in the
View toolbar for Part, Assembly and Drawing. The next button reuses `ZIMA-CAD-Parts/gfx/navigation/terminal.svg` and the
existing console toggle with Ctrl+Shift+C.

Linux presets in `cpp/CMakePresets.json` still point at
`runtime/linux/python` for native Qt/OCCT libraries. The user subsequently authorized removing the whole old runtime after
Windows checks passed with it detached. The directory and archives are now
deleted. These presets therefore require replacement before a fresh Linux
build; do not restore Conda from Git or package it.

## Next work on Linux

The updater uses Qt Network, OpenSSL 3 Ed25519 and the **matching Qt Core private
headers** for QZipReader. Build `zima-cad-update` with GUI/CLI and deploy it under
`linux/<build>/bin/`, with its native dependencies and TLS plugin. Current manifest
platform identity is `linux-x86_64`; establish and enforce the supported distro/
libc baseline before publishing Linux assets. Add Linux runtime smoke/report
generation to the candidate builder and finalized-archive acceptance; Windows
tests of synthetic Linux metadata do not replace that work. Run the isolated
updater lifecycle tests and exercise KDE/Wayland startup/acknowledgement, normal
shutdown, instance blocking, launcher recovery, permissions and rollback.

1. Inspect installed distro/toolchain and existing C++ build cache. Select and
   record the exact supported Debian x86_64 baseline and KDE/Wayland environment
   from the shared policy. Check actual versions; do not lower CMake's Qt/OCCT
   requirements to fit older system packages.
2. Establish versioned native dependencies using an explicit SDK or compatible
   system development packages. Remove the Conda path from Linux CMake presets
   and runtime search paths. Retain normal developer override mechanisms.
3. Configure and build in a fresh directory, run relevant native CTest contracts,
   then launch GUI and CLI with no Conda directory on PATH or library paths.
   Inspect linked libraries and plugin resolution; verify image/PDF exports,
   a deterministic Part operation and native save/reopen.
4. The obsolete runtime has already been deleted on this checkout. If another
   Linux checkout retains a local ignored copy, remove it after the new native
   checks pass. Check resolved paths and preserve user data. Record proof of
   dependency independence.
5. Extend the existing native candidate builder with Linux dependency deployment
   and smoke checks. The CMake updater, signed metadata, GUI Settings and retention
   are now implemented; read [Application updates](UPDATES.md) and verify their
   actual Linux execution. Do not import unrelated ZCP runtime dependencies.

## Release rules to preserve

`YYYYMMDDNN` is the build identity; tag `ZIMA-CAD-<id>` and archive
`ZIMA-CAD-<id>.zip` must match embedded GUI/CLI metadata. A common root contains
`windows/<id>`, `linux/<id>`, `source/<id>` and separate `custom` directories.
Each version carries its own native libraries and immutable factory resources.
Portable user `config/`, Projects and recovery live outside those directories.
`config/config.ini` holds common overrides; `config/linux/config.ini` holds Linux
path overrides. Preserve the root `config/` instead of introducing `profile/`.

Build official artifacts only from clean committed/tagged source, including
initialized submodules. Never package working artifacts, old Conda runtimes,
private data or required external geometry sidecars. Validate paths/collisions,
member lengths, CRC, SHA-256, clean extraction, dependencies and actual execution.
Hashes prove integrity, not official origin; use the implemented signed manifest
and publisher workflow for authenticated updates. Keep current and previous verified
official versions per OS, preserve custom builds and all user data, and retain
sources for every retained platform build.

Platforms may publish independently. Use a new build ID when adding another
platform; never replace published bytes. Document unsupported native document
versions explicitly; binary rollback does not imply backward file compatibility.

Update English documentation with actual results, remaining limitations and
exact artifact identities. Commit and push the completed changes as requested;
do not publish release assets without completing release validation.

Final Windows run with the entire old runtime absent: GUI/CLI build passed;
console GUI contract passed 1/1 in 139.26 s. Logs:
`build/native-only-toolbar-build.log`, `build/native-only-toolbar-tests.log`.
The user explicitly accepted doing the remaining Linux work from Linux.

## Subsequent Windows implementation

Read [NATIVE_DISTRIBUTION.md](NATIVE_DISTRIBUTION.md) before duplicating work.
`VERSION`, GUI/CLI `--build-info`, shared portable installation detection and
configuration layers now exist. `tools/distribution/package.py` exports exact
Git blobs (including recorded submodule commits) and provides archive validation.
The native Windows launcher and Windows build wrapper are implemented.

`tools/distribution/launcher-linux.sh` is the Linux selection contract to verify
on Linux. Supply `build.ini` (`product=ZIMA-CAD`, matching version/platform),
`version.json`, version-local `config/`, executable `bin/` and dependencies.
Preserve ELF and script executable modes when assembling a Linux archive.
The current ZIP extractor is deliberately a Windows verifier and does not apply
Unix executable modes; extend and verify the Linux extraction path on Linux.
Do not claim KDE/Wayland execution, Debian dependency closure or the signed updater
based on these Windows changes. Keep configuration snapshots before future schema
conversion, separately from ordinary numbered INI backups.

Windows candidate `2026091503` is now validated (commit, hash and exact check scope
are recorded in [NATIVE_DISTRIBUTION.md](NATIVE_DISTRIBUTION.md#windows-verification-on-2026-09-15)).
Use this as evidence for Windows only. Changes needed for Linux will require a
new committed candidate identity and matching Windows build before combining
platforms; do not label a different Linux commit as the existing Windows build.

## Published Windows release and AI follow-up

The first signed Windows release,
[2026091504](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026091504),
is now public. Its accepted archive and signed manifest are immutable. Linux can
publish independently under a new build ID after Linux runtime validation.

The signed Windows release
[2026091505](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026091505)
adds the [AI console](AI_CONSOLE.md). Its native smoke, signed archive and packaged
Settings checks passed; this does not establish Linux support. The CMake
`zima_ai` target uses existing Qt Core/Gui dependencies, AUTOMOC and a bundled
engineering-reasoning resource. Codex remains a separately installed optional
native executable; do not bundle accounts or a developer's Codex profile.
Settings stores `AI/Model` and `AI/CodexExecutableLinux` in the shared installation
configuration. Verify native executable discovery, the desktop sign-in browser,
OS keyring, the private application-local profile and cancellation on Linux.
Run `zima_ai_contract` without an account first; authenticated AI acceptance is a
separate step. Windows results do not establish Linux authentication/runtime support.

## Resume after switching to Windows (2026-09-17)

The user will obtain the existing publisher key on Windows. Linux compilation
is complete; GUI, CLI and updater all report `2026091701`. Repository-root
`./zima-cad` selects `build/cpp-native-release/zima-cad-cpp`. Local build provenance
includes the pre-existing untracked documentation files; those files are excluded
from the committed-source candidate.

The GitHub **draft**, tagged `ZIMA-CAD-2026091701`, holds the unsigned candidate
ZIP and its validation report. Its source tag must remain at
`48465a3fe34c5db3c2dc14cbf80a736b7b474fb1`; later documentation commits do not
change the candidate identity. See [exact checks and hash](releases/2026091701.md).
No Windows binary was added and no existing public release was replaced.

To continue:

1. Fetch the repository and tags. Download both draft assets together if working
   on another machine; retain the candidate ZIP beside its validation report.
2. Locate the existing protected publisher key described in
   [Application updates](UPDATES.md#discovery-and-authentication). Do not generate
   a replacement key. Windows DPAPI material cannot be directly decrypted on Linux;
   use the existing interactive `export-key` command to create an encrypted PEM
   outside repositories and distributable directories. Never send its contents or
   password through conversation/logs.
3. Prefer returning with that encrypted key to Linux for finalization. A clean
   source checkout is already prepared at `/tmp/zima-1701-signing-repo` and a fresh
   verified candidate extraction at `/tmp/zima-1701-signing/ZIMA-CAD`; these paths
   are temporary and must be recreated after a reboot if absent. The durable
   candidate is `.dist-output/linux-1701-candidate/` and on the GitHub draft.
4. Run the documented `finalize` procedure with the exact candidate/report,
   protected key, clean repository and a new output directory. It must produce
   the signed ZIP, manifest and signature, and pass the finalized Linux smoke.
   Signing on Windows alone does not run the Linux smoke; the signed archive
   would still need fresh extraction and native verification on Linux.
5. Verify production bootstrap trust and public manifest compatibility, update
   acceptance notes, replace only the **unpublished draft** candidate assets with
   the accepted signed assets, then publish. Published bytes remain immutable.

The broad dialog width assertion and offscreen/OpenGL limitations are documented
in the release notes. They must not be misreported as a complete GUI test pass.
