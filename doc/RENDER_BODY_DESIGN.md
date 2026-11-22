# Render Body: proposed first delivery

The user authorized implementation after the current Sketcher Windows release,
followed by verification, documentation, commit/push and another Windows release.
The user approved this native-storage scope and verification plan on 2026-11-22,
including protected-project checks on recoverable copies without overwriting
originals. This satisfies the separate format agreement required by AGENTS.md.
No Render format or product implementation has been changed yet.

## User workflow

Create a Render Body from an already calculated material Body in the same Part.
It references that source, following the existing FEM Body organization, and
owns cameras, lights and render settings. It does not create material geometry.
The first delivery targets one source Body, including a calculated Body Boolean;
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

- Stable Render Body identity/name and an earlier material source Body ID.
- Stable camera/light IDs, names, visibility/enabled state and their complete
  ordinary placements, including references and retained resolved frames.
- Camera field of view and the active camera ID.
- Light kind, color, intensity and size.
- Image width/height, sampling quality, exposure, surface-finish settings and
  the initial background colors copied from the View.

All required information belongs in `.prtz`, including when the Part is embedded
in another native document. Render pixels, acceleration structures and intermediate
samples are disposable derived data. Image export is an optional output, never a
required sidecar for reopening or rendering the document.

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

Background work owns its immutable input. Cancellation, closing a document or
changing the source invalidates pending publication. A cache is reusable only
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
