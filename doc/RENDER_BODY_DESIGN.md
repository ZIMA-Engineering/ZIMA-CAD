# Render Body: proposed first delivery

The user authorized implementation after the current Sketcher Windows release,
followed by verification, documentation, commit/push and another Windows release.
The user approved this native-storage scope and verification plan on 2026-11-22,
including protected-project checks on recoverable copies without overwriting
originals. This satisfies the separate format agreement required by AGENTS.md.
Native rendering, persistence and the first GUI/export matrix are implemented
and verified locally. Start-template regeneration and GUI creation passed;
factory FORM copies were recalculated and reopened. Packaged-release acceptance
is still in progress. See the [user workflow](RENDER.md) and
[finite Windows verification](verification/20261123-render-and-hole-properties.md).

## User workflow

Create a Render Body in the same Part, following the FEM Body organization.
It owns cameras, lights and render settings without creating material geometry.
On 2026-11-23 the user approved a shared scene from visible Bodies by default,
with an optional explicit selection of multiple source Bodies or Boolean results.
All included Bodies share lighting, shadows and reflections. Source candidates
precede the Render Body in history; visible mode uses available material results
and excludes consumed inputs, hidden/suppressed Bodies and other Render/FEM Bodies.
Explicit selection retains its chosen identities independently of View visibility.
Assembly scenes and animation are later work.

Initialize one camera from the current View and one useful default light. The
user can add, edit and delete cameras and point lights or suns. A camera has a
placed position and orientation, plus a field of view. A point light uses its
position; a sun uses its direction. Both expose color and intensity. Light size
controls soft shadows. Camera and light markers use wire presentation in the
View and ordinary Tree selection; double-click opens their inspection/edit path.

The first image keeps the existing View gradient background and source colors.
Render settings provide image size, quality, exposure and surface finish
(roughness/metallic response). Rendering produces an antialiased image with
lighting, soft shadows and reflections. An explicit Render command reports
progress and supports cancellation. The result can be inspected and exported
as PNG or JPEG. No external rendering application is required.

## Native information

Keep the existing native extensions. Add a Render definition to the Body graph:

- Stable Render Body identity/name, source-selection mode and earlier material source Body IDs.
- Stable camera/light IDs, names, visibility/enabled state and their complete
  ordinary placements, including references and retained resolved frames.
- Camera field of view and the active camera ID.
- Light kind, color, intensity and size.
- Image width/height, sampling quality, exposure, surface-finish settings and
  the initial background colors copied from the View.

All required information belongs in `.prtz`, including when the Part is embedded
in another native document. Explicitly retained image pixels and thumbnails also live in the Render
definition. Live previews, acceleration structures and intermediate samples
remain disposable derived data. Image export is an optional output, never a
required sidecar for reopening or rendering the document.

Image export reuses the existing **File > Export (F7)** action, shared file
dialog and atomic image encoder. In the Render workspace this action exports
the explicitly selected completed snapshot as JPEG or PNG at its pixel dimensions;
it does not capture the modeling viewport or its markers. A right-toolbar
shortcut uses that same action. Normal Save still writes the native Part and
the Render definition, including retained images. Export cancellation and file-write errors must leave
the document and an existing output file intact. A missing image must not silently export another image. Earlier model states
remain exportable as explicitly labelled snapshots. This follows the user's request
on 2026-11-23 to match the application's existing export workflow.

A missing/suppressed source remains an explicit repairable dependency. Camera
and light reference loss retains the last valid position/orientation and marks
the unresolved input. It must not silently reset a frame or render from invalid
inputs. A Render Body cannot be a material Boolean tool, a FEM source or a source
of a dependency cycle.

## Implementation boundaries

Reuse Body graph transactions and the existing placement/reference-entry controls
used by FEM. Creation and editing share the same properties dialogs, inside the
main application. Properties expose OK/Cancel. Unchanged OK creates no Undo step;
Cancel discards the draft. Whole-Origin entry, reference replacement, independent
inspection, degrees of freedom and middle-button confirmation retain the shared
contract. No change to the protected placement solver is planned.

The renderer consumes a private snapshot of already calculated source geometry
and appearance. Opening properties, selecting objects, moving a camera or changing
settings must not recalculate CAD geometry. Reuse existing scene/reference paths;
do not publish an unused final scene before the command's actual context scene.

Use one shared native rendering implementation on Windows and Linux. The initial
technical direction is a CPU ray renderer with triangle acceleration, surface
normals, sampled lights/reflections and progressive image accumulation. Its
quality and performance must be measured on real curved Bodies before acceptance;
the existing viewport image alone is not a completed high-quality renderer.

Background work owns its immutable input. Explicit cancellation or closing the
owning document stops its job. Source edits allow the captured batch to finish
as an earlier-state snapshot. A result describes the current model only
when all actual source geometry, appearance and render inputs match. No reduced
model precision or persistent approximated geometry is introduced for speed.

## Format and factory verification plan

Inventory repository-owned native assets and start Part/Assembly templates, plus
the already designated protected test project and its dependencies. Preserve
verified recoverable copies before any conversion. Existing factory files do not
yet contain Render definitions; determine any further affected serialization
contracts from the final implementation rather than assuming every asset is
unaffected.

Regenerate/save/reopen affected native factory assets with the changed native
implementation and verify authored history, roles, references and applicable
dependent operations. Regenerate the start templates and create new documents
through the GUI, checking the first active editable context and normal commands.
Run native factory loads and the existing release gates. Do not patch only a
version field or add legacy migration execution paths.

Check the protected project only on verified copies, distinguish unsupported
pre-existing formats from regressions, and report finite coverage. Conversion is
limited to affected native definitions. Neither this proposal nor factory work
authorizes overwriting user originals or custom libraries. Public reports use
generic test descriptions and contain no private project identifiers.

## Acceptance matrix

- Render Body creation/editing, source replacement/loss, source changes and
  activation; material/FEM behavior remains covered by existing contracts.
- Camera and both light kinds: creation, numeric and reference/whole-Origin
  placement, replacement, removal, independent inspection, reference loss,
  unchanged OK, Cancel, Undo/Redo and native save/reopen.
- Actual View/Tree mouse interaction and command availability; five-language
  localization and dialog layouts, including a 1080p display.
- Independent ray/triangle, bounds, camera and lighting checks; curved and sharp
  surfaces, self-shadowing, reflections, transformed source geometry, source
  colors and exported image dimensions/content.
- Image quality inspected on representative calculated Bodies. Record settings,
  sample counts, source size, elapsed times and cancellation responsiveness.
- Repeated renders and edits do not change CAD geometry, references or document
  revision; stale results cannot overwrite a current result. Save/reopen retains
  the same camera/light/settings definitions.
- Verify the packaged Windows runtime, native dependency closure, signed/public
  archive and updater. Report native Linux execution separately.

## Gallery and background snapshot scope agreed on 2026-11-23

The user approved a separate Render tab and several explicitly retained images
inside `.prtz`. The right command toolbar exposes **Render gallery** when images
or a job are available. Rendering captures an immutable input batch; later
modeling edits do not cancel that batch. Closing the tab does not stop the job.
Completed earlier states remain exportable and retainable with an explicit
snapshot label. A dedicated Stop action cancels the worker.

The additive Render definition stores image identity, authored name, capture
time, dimensions, compressed PNG bytes and a thumbnail. Immutable buffers are
shared through document edits and Undo; no required sidecar is introduced. This
extends the previously approved Render-only native schema, not ordinary Body
serialization. Re-run the existing factory/start-template and protected-copy
checks with the final implementation; never overwrite protected originals.

Acceptance adds actual tab switching during rendering and model editing,
unchanged snapshot pixels after source edits, Stop versus tab close, two retained
images, rename/delete Undo/Redo, export, native save/reopen without a new job,
five-language controls and Light -> Dark -> Light. Record measured interaction
costs and finite coverage before publication.

## Gallery interaction follow-up (2026-11-23)

The image tab uses the application's vertical command column on the right.
Image Export belongs in that tab; the Render Body command column does not expose
it. Saving a completed snapshot into the Part remains an explicit action named
**Save to Part**. Rename and Delete belong to the saved image's context
menu in the left gallery. Remove the Fit image and 100% toolbar commands.

Mouse navigation follows Drawing: wheel zoom anchored at the pointer, with the
same direction and step, and middle + right drag to pan. Navigation never edits
the pixels, starts a calculation or creates an Undo transaction. Selecting
another image initially fits it; progressive updates retain the current view.

Visible cameras and lights must appear before hover, remain selectable through
the common picker and synchronize Tree selection. **View from camera** is a
view-only action in Camera Properties and its View/Tree context menus. Properties
uses the resolved pending camera, including projection and field of view.
Ordinary camera/light marks and their adjacent labels use dimension yellow
(`#F5CD50`), as do FEM condition marks and labels. Hover and selection retain the
shared interaction colors. An unresolved Render object retains its error color.

Planned image editing: start with rectangular cropping of a selected result,
with explicit confirmation and a new retained image so the original is kept.
Further image-management tools are unspecified; this is not an implemented
editing suite or an automatic change to existing image data.
