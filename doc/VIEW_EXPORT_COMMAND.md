# Capture the current View

`export.view` saves the currently displayed interactive 3D view as PNG or JPEG,
using its camera, visibility, and rendered markers. It does not calculate geometry,
change the document or Undo/Redo, or run Fit.

```json
{"command":"export.view","arguments":{"path":"view.png"}}
{"command":"export.view","arguments":{"path":"view.jpg","quality":90,"overwrite":true}}
```

- `path`: a `.png`, `.jpg`, or `.jpeg` path, relative to the working directory
  when not absolute. The destination directory must exist.
- `quality`: integer 0–100, default 95; affects JPEG. PNG is lossless.
- `overwrite`: default false. Existing files are overwritten only when true.
- `document`: optional active-document ID check.

Output contains `document`, `displayed_document`, `path`, `bytes`, `width_px`,
`height_px`, `camera`, and `model_changed: false`. Dimensions match the actual
framebuffer pixels; no assumed print resolution is added. Activating a Part in an
Assembly still captures the complete displayed Assembly, so active Part and
displayed Assembly IDs are reported separately.

A host without a 3D View, including standalone batch CLI, returns `view_unavailable`
and creates no image. Drawings use `export.image` for sheets or crops;
`export.view` does not export hidden 3D content behind a drawing. The usual console
operation guard protects pending GUI edits.

Capture runs on the View thread. Writing uses an immutable image copy and may run
on a worker thread. GUI 3D-view export uses the same atomic writer: finish a temporary
file before publishing the result. Errors and unauthorized overwrites preserve the
original file. This introduces no required document files or native-format changes.

## Tested contracts

Regressions check PNG pixels, readable JPEG, dimensions, UTF-8 paths, overwriting,
capture/write errors, thread separation, exact activated-occurrence context, and
unchanged calculated geometry and history. A separate CLI process checks the missing
View error. GUI compares the real framebuffer with saved PNG and verifies unchanged camera.

All six affected tests were verified. The first integration run confirmed translations,
CLI process, other exports, host, and actual GUI (82.15 s). The new model fixture
initially used the wrong dimension type and an unencoded occurrence path; after
correcting inputs it passed separately, **1/1 in 0.56 s**.
Logs: `build/view-export-tests.log`, `build/view-export-model-tests.log`.
Both applications and all targets built. At this stage the catalog has **266 commands**
and the suite **146 tests**; the new full suite has not yet run.
