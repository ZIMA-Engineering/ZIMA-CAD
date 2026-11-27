# Analytic target continuation and Windows taskbar icons

Scope: local Windows development build, 2026-11-27. The native document format,
shared container placement solver and root `zima-cad.bat` launch path are unchanged.
The user's FGM file was inspected through isolated diagnostic copies only.

## Corrected behavior

- Windows uses the same seven-resolution ICO for the executable and registered
  application shortcuts. Its vertical artwork occupies 80% instead of 87.5% of
  the square, retaining its width. Startup refreshes an existing installation's
  content-addressed icon and matching Start menu, Desktop and pinned shortcuts.
  It preserves arguments, working directory, application identity and pinning;
  it neither creates pins nor recreates a removed registration. Unchanged startup
  leaves the icon and shortcut modification times unchanged.
- A restored native history prefix must contain the original face ancestry
  required by the newly edited suffix. Exact matching bindings are recovered
  where available; an incomplete prefix is replayed. Opening an Up To preview
  is no longer prevented by an unrelated invalid downstream Wrap definition.
- A retained failed boundary with deferred display identities now restores its
  selectable face mesh from its exact saved B-Rep and identity bindings. This
  does not regenerate material, rename references, clear failure status, change
  the document or create an Undo transaction. Newly materialized shortened
  histories publish complete face identities directly.
- Wrap continues the selected plane, cylinder, cone or sphere beyond its trims.
  The selected real face must still exist. Side, anchor, singularity checks,
  tolerances and explicit transitions between different supports are retained.
  Split and material Boolean operations still use the actual input Body.
- Project Sketch uses the complete selected analytic support with its ordinary
  nearest-ray and ambiguity rules. Project Split, retained Surface and Relief
  retain their actual bounded domains. Cone continuation stays on the selected
  nappe. OCCT sectioning uses the full conical support to avoid losing a section
  at the apex boundary; hit classification still uses the selected half-cone.
- Derived curve previews follow the same analytic domains. Freeform surfaces
  are not extrapolated, and embedded raster artwork retains its existing mapping.

## Finite verification

Windows x64, MSVC, Qt 6.11, OCCT 8.0.1. The measurements below are observed test
wall times, not application speedup claims.

| Check | Observed result |
| --- | --- |
| Directional projection | Pass, 0.51 s; 35 cases, including extended cylinder/plane, both cone-angle signs, rejection of the other nappe, sphere nearest root, shared support ancestry and existing bounded partitions |
| Projection history | Pass, 2.14 s; exact extension, cold prefix ancestry, deferred face identity restoration and shortened-history picking |
| Wrapping history | Pass, 7.65 s; supported analytic charts, partial/outside domains, transitions and existing material behavior |
| Wrapping document | Pass, 2.83 s; outside-height preview/native agreement, outputs, persistence and dependent operations |
| Projection document | Pass, 98.28 s; analytic preview equations, an outside-trim planar circle, persistence, external references and existing dimension/edit/drag matrix |
| Native ordered Feature Part matrix | 102 checks passed, plus 24 relief edits and existing Assembly Through-all preview checks |
| FGM Up To copy | Exact draft before/after input preparation, successive correction edits, target identity, Undo/Redo and native reopen passed without a prior full regeneration |
| FGM target GUI | Actual mouse selection of the original outer cylindrical target, unchanged identity, zero Body calculations during picking and Cancel restoration passed |
| FGM Wrap copy | Exact cylinder equation, complete authored preview endpoints, unchanged rollback geometry/failure state, Undo/Redo, save/reopen and cold regeneration passed |
| Ordinary Wrap GUI | Target selection, clear, inspection, preview, persistence, unchanged OK, Cancel, embedded image, relief edits and Undo/Redo passed |
| Projection GUI | Pass, 33.42 s; existing command interaction and persistence contract |
| Desktop integration | Pass, 3.99 s; seven ICO sizes, proportions, registration ownership, old pinned icon replacement, preserved custom arguments and foreign registration, unchanged-startup no-op, removal |
| Native Windows registry variant | Pass using an isolated test registry key |
| Localization | Catalog/coverage contract passed, 20.24 s; no new or changed user-visible strings |

The final CTest selection passed all eight suites with zero failures in 131.78 s.

The complete ordered Feature executable also contains an older Assembly-cut
creation fixture rejected by the current Part-only material authoring policy.
The Part matrix was run explicitly with `ZIMA_VERIFY_PART_LIMITS_ONLY=1`; that
obsolete Assembly authoring fixture was not changed or reported as passing.

FGM probes use `ZIMA_VERIFY_REVOLVED_TARGET_SOURCE`,
`ZIMA_VERIFY_WRAP_PARTIAL_MODEL` and, for the GUI,
`ZIMA_VERIFY_WRAPPING_ONLY=1` with `ZIMA_VERIFY_WRAP_TARGET_MODEL`.
The original file was not overwritten. Local logs and screenshots are under
`tmp/fgm-investigation` and `tmp/icon-investigation`.

The actual development installation was repaired with the finished executable.
Its existing Start menu, Desktop and pinned taskbar shortcuts all resolve to
`app-9a3d4516e277.ico`, whose full SHA-256 equals the repository ICO:
`9a3d4516e277a5562d22db7d8a5b71b73f60eb4847521ad74b6406542650161d`.
Their target, arguments and working directory were compared before/after and
remained unchanged. Windows Shell repaint at every display scale is not covered
by these resource/shortcut checks.

The user subsequently reported that the updated build works (2026-11-27).

Linux compilation and desktop-session behavior were not verified here. Native
modeling and preview changes use the shared C++ implementation; Windows-only
branches are limited to Shell integration and icon presentation. No release ZIP
or publication was requested or produced.
