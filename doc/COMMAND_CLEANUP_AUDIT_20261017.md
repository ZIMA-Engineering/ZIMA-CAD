# Command and unused-code audit, 2026-10-17

## Scope and evidence

The audit covers the tracked C++ implementation, its tests, command registration,
factory fastener authorship, and packaging consumers. The initial inventory
contained 1,154 C++ source/header files and 623 compiled translation units. Every
tracked implementation file was represented in the Windows build. This is a
build-coverage check, not proof that every function or branch is reachable.

Candidate removal requires tracing callers, registrations, callbacks, templates,
and build/tool references. A single search result alone is insufficient for
public commands, operating-system entry points, or externally callable APIs.

## Removed uncalled helpers

The following definitions had no callers or registrations in the tracked source,
tests, or tools and were removed without replacing their active neighbors:

- `mate_status_name` and `mate_status_from_name` in `assembly_document.cpp`;
- `viewer_mesh_contains_point` in `part_document.cpp`;
- `point_in_triangle` in `drawing_document.cpp`;
- `scale_source_key` in `scale_geometry.hpp`;
- `transformed_axis` in `occt_kernel.cpp`;
- `screen_segment_distance` in `mesh_view.cpp`.

The two additional uncalled placement helpers remain unchanged because their
area requires separate explicit approval. `wmain` is an operating-system entry
point and is not unused code.

## Live dependencies and agreed direction

Ordinary modeling uses the current `feature.*` definition and transaction.
Sheet Cut and Sheet Revolution are meaningful distinct operations that still
consume shared extrusion/revolution kernel requests and native parameter types.
Those shared geometry implementations must remain. Removing a public ordinary
command does not establish that its underlying C++ request type is unused.

The agreed cleanup retires ordinary `extrusion.*` and `revolution.*` public
commands after their live callers have been moved to `feature.*`. Sheet-specific
public commands must enforce their sheet operation rather than expose a switch
back to an ordinary extrusion/revolution. No compatibility aliases or native
format migration are required.

The older `hole.*` command family remains registered and callable. Factory
fasteners do not use it, but that fact alone does not justify deleting a live
public capability. `holes.*` is a separate Sketch-segment operation.

## Factory locking holes

The factory inventory identified 26 ordinary locking-hole extrusions: 24 bolt
models and two reference Parts for CSN 02 1205 and CSN 02 1208. These were authored
with the native implementation as current modeling Defaults, subtracting Through
All independently on both sides. Document, Sketch, container and feature IDs
were retained. Each candidate was calculated, saved, reopened, and compared for
volume, surface area and preceding-face ancestry before installation.

The generator now authors the same current definition. Its dedicated document
naming settings preserve canonical factory filenames independently of personal
name-conversion preferences.

## Verification status

This audit is distinct from Windows package acceptance. The
current Part profile command matrix passes, including solid/thin/surface output,
driving edits, locks, atomic rejection, Undo/Redo, native persistence and 18
point-end result/direction/extent combinations with repeated source motion.
Assembly history, cursor and Sketch conversion matrices also pass.

The retired public commands are absent and their consumers use the current API.
Part/Assembly reference assignment and removal, Assembly history/cursor and
conversion, work planes, activation, value locks, complete Bend, sheet state and
disconnected-fragment matrices pass. All 200 factory bolts and fresh locking-hole
authorship pass. Three successive bolt-length edits, Undo/Redo and reopening pass.
The current locking-hole GUI verifies Cancel, unchanged OK, both Through All
sides, editing, Undo/Redo and native reopening. The CLI process contract,
working-directory startup, File Manager, five-language catalogs, 1,405 localized
browser pages and packaging/publisher input tests pass.
The complete console GUI contract passes current feature dialogs, adjacent Shell
walls, full-edge Fillet/Chamfer equations, Assembly cuts, references and history.
Those downstream fixtures use a single current Default side to preserve their
original full-wall/full-edge purpose, rather than silently treating separate
End/Start ancestry fragments as one reference.
Actual bolt Sketch successive angle edits, supported dragging, Undo/Redo and
native persistence pass. Refresh-scope and component-reference GUI scenarios
pass after their last retired-command calls were converted to `feature.*`.

The broad UI suite stops at its existing confirmed-wire-versus-hover thickness
assertion. The production width code is unchanged from the previous release;
targeted point-marker and translation checks pass. Do not interpret these
targeted results as a passing complete UI suite. Windows acceptance is recorded
in the release document after packaging; Linux execution has not been verified
on this Windows host.

The fragment matrix uses a single native kernel sweep to preserve its original
purpose: disconnected fragments of one source. An initial conversion to two
independent Default sides additionally split connected edges at the Sketch plane,
which follows their distinct End/Start ancestry rather than indicating missing
material. The fixture therefore retains direct shared-kernel authorship, without
restoring a retired public command. Independent Default side combinations remain
covered by the profile matrices and converted factory locking holes.
Physical-property comparisons complete a copy of deferred native analysis instead
of interpreting a not-yet-calculated volume as zero. The Assembly fragment test
reacquires its document-state pointer after explicit regeneration: relation
evaluation may atomically replace the Workspace, invalidating a previously held
state pointer. The document and reference identities themselves remain stable.


## Finite interaction coverage

| Group | Exercised coverage | Remaining limit |
| --- | --- | --- |
| Current profile API | Part/Assembly result and extent matrices, 18 point-end combinations, repeated source changes, locks, rejection, Undo/Redo and reopening | These command matrices do not enumerate every Sketch dimension creation order or mouse orientation. |
| Bolt Sketch GUI | Existing angular dimension double-click, three successive values, supported endpoint dragging, external support retention, Undo/Redo and native persistence; four representative packaged bolt workflows | This is an AngleBetween edit/drag study, not an exhaustive matrix of every dimension kind, selection order, orientation and contact side. |
| Locking-hole conversion | All 26 native candidates; all 200 factory bolts; fresh fine-pitch authoring and three length changes; actual current Properties, independent Through All sides, Cancel, unchanged OK, Undo/Redo and reopening | The dedicated hole editor GUI case uses one representative fine-pitch bolt; it is not a GUI run on all 26 files. |
| Sheet operations | The complete existing Bend command suite, Sheet Cut clearances/projection/cones and Sheet Revolution modes, history, identities and persistence | This finite suite is not a certification of all possible sheet topology. |
| Broad UI styling | Point-marker and five-language checks pass | The broad suite stops at its existing confirmed-wire thickness assertion; later cases from that invocation are not claimed as verified. |
| Linux | Shared source and committed native assets retained | No Linux execution was performed. |
