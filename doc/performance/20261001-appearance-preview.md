# Assembly appearance preview — 2026-10-01

## Change and boundary

Inputs are the displayed Assembly, its current session generation and the pending
appearance. The required output is the same per-occurrence style map and rendered
image, without rebuilding unchanged geometry on every color edit.

Previously `update_viewer_body_colors()` built the entire authoritative Assembly
scene for each preview, solely to extract occurrence paths from triangle references.
The window now retains that path set for repeated previews. It still resolves each
occurrence and reads current appearance on every call. No mesh, appearance value,
reference geometry or calculated body is cached by this change.

Reuse requires both the same Assembly runtime identity and session data generation.
A full scene refresh clears the set, as does any non-preview appearance update.
Closing the dialog restores the normal scene. The first preview still builds the
scene through the existing validated path; committed appearance updates retain
that path too. Source updates, Undo/Redo and session replacement change generation;
full refresh also invalidates reuse across Workspace publication. Only one set is
retained per window. No native format, placement, regeneration or Undo contract
changes are introduced.

## Measurements

Baseline: `5b58227e`, with the same expanded GUI test but without the optimization.
Both builds used MSVC Release on this Windows host. The synthetic fixture contains
256 occurrences of a calculated cube. Six alternating color edits are measured
inside the actual Appearance dialog, then repeated after an unsaved source Part
height change and source-refresh/Undo/Redo checks.

| Preview sequence | Before, warm median ms | After, warm median ms |
| --- | ---: | ---: |
| Original Assembly | 42.964 | 0.739 |
| After source geometry edit | 44.794 | 0.753 |

Warm medians cover edits 1–5; edit 0 populates the cache. This is approximately
58–60 times faster for the measured CPU callback, saving about 42–44 ms per repeated
edit. Initial previews still cost 34–35 ms in the changed run. Measurements exclude
queued painting and do not establish full-frame latency or a universal application
speedup. This is one before/after synthetic run, not representative user-model
profiling. [Raw measurements and image hashes](20261001-appearance-preview.txt).

## Verification

The expanded refresh-scope GUI test checks that each preview leaves the base mesh
revision, Tree model, camera and scroll unchanged. It verifies that color editing
changes the framebuffer and restoring the original color restores exactly the
original framebuffer. Original and colored captures from both source-geometry
states are byte-identical PNGs between baseline and changed builds.

All seven focused suites passed: refresh-scope GUI, Assembly-refresh GUI,
appearance contracts, Workspace contracts, component properties, Family and
translations. The existing appearance suite also checks appearance persistence,
transparency and dialog behavior. Logs are in
`build/appearance-preview-before.log`, `build/appearance-preview-after.log` and
`build/appearance-preview-tests.log`.

Localization review found no new or changed user-visible text. The shared
cs/en/de/fr/ru translation contract passed. Local GUI and CLI built successfully;
the repository-root development launcher remains unchanged. No release archive was
created. Linux verification remains deferred to the Linux host.
