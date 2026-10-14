# STEP placement preview investigation

## User report and required behavior

The user selected the first placement location on a planar imported STEP face
in `Projects/02.prtz`. The purple Extrusion extent appears away from that
location. The supplied screenshot is `Snímek obrazovky 2026-10-14 034441.png`.
Properties shows X = 2019.624 mm, Y = -321.668 mm, Z = 912.120 mm.
The isolated extent is labelled 50 mm.

The user explicitly confirmed that showing this extent before authoring the
profile is correct. It must remain visible and follow the selected placement;
an experiment hiding empty-profile cues was discarded. Ordinary native-model
placement appears correct according to the user, so investigate imported face
input and preview frame transfer before changing shared placement behavior.

The imported-body GUI harness can select the unified Feature action by setting
`ZIMA_VERIFY_IMPORT_FEATURE_PREVIEW=1`. Its first-reference diagnostics report
the pending placement and published extent origin/direction. Profile offset
edits consume the existing inline-parameter editor. A read-only View accessor
exposes the actual published extent packet for regression checks.

The existing Feature prototype test fails with `Empty Feature has no
offset-plane border` on unchanged HEAD as well as the discarded experiment.
Its unsupported-plane assumptions require separate analysis; the failure is
recorded in `build/feature-empty-cues-baseline.log`.

## Cause and fix

The imported Body has translation (-1500, 0, -1000) mm. Its work-plane carrier
already consumes Body placement, but Feature transient extents and dimensions
were submitted in Body-local coordinates without that presentation mapping.
The baseline handle origin (1709.32, -619.363, 965.177) mm therefore disagreed
with the displayed work plane. With the existing Body/occurrence mapping in
place, the actual published handle origin is (209.319, -619.363, -34.8228) mm.
The error is a Feature preview consumer defect, not corrupt STEP geometry.
Non-identity native Body placements can expose it as well.

Feature Properties now initializes the established Body presentation context
and its transient point transform before the first synchronous preview, using
the existing frame and occurrence-to-scene helpers. Cleanup clears that context.
No extra intermediate scene, geometry calculation or undo transaction is added.
The extent remains visible before a profile exists, as requested.

No shared placement implementation, native format or localized UI text has
been changed. Localization coverage passed (`build/feature-position-localization.log`).
Linux GUI verification is not available on this Windows host.

## Verification matrix

| Case | Result / evidence |
| --- | --- |
| Exact user document, first actual View face click | Baseline fails extent/work-plane coincidence; final build passes both coincidence and the independent ray/triangle clicked-point check (`build/02-feature-first-reference.log`, `build/02-feature-final-verified.log`) |
| Imported STEP with identity Body placement | First-reference preview passes (`build/synthetic-feature-first-reference.log`) |
| Imported STEP with translated/rotated Body and a following empty Body | New `zima_cpp_import_feature_frame_ui_contract` passes, with translation (100, -200, 300) mm and rotation (20, -30, 40) degrees |
| Repeated profile offsets 0, +3, -2, 0 mm | Independent source-plane equations and extent/work-plane coincidence pass |
| Repeated lengths 25, 75, 50 mm | Published extent changes; document state and base mesh revision stay unchanged |
| Cancel | Original document and camera state retained |
| Native Extrusion/Revolution frame GUI regression | `zima_cpp_profile_frame_ui_contract` passes creation/editing in XY/XZ/YZ and its existing reference-frame cases |
| Feature axes and language GUI regression | `zima_cpp_feature_axes_ui_contract` passes its five-language checks |
| Wider surface placement GUI suite | Cylinder branches and repeated reference-offset edits pass; later idle-visibility origin/name check fails identically with unchanged Properties source (`build/surface-visibility-baseline.log`) |
| Wider Feature prototype GUI suite | Unsupported-plane expectation fails on unchanged HEAD (`build/feature-empty-cues-baseline.log`); not claimed as passing |

Regression results above are in `build/feature-body-frame-regressions.log`.
The final five-test gate passes imported Extrusion, Body preview, the translated
Feature frame, translation coverage and Feature axis highlighting
(`build/feature-preview-final-confirmed.log`). The stronger independent
clicked-point test also passes on the translated/rotated synthetic Body
(`build/feature-actual-click-regression.log`) and the exact user file.
These checks do not claim an exhaustive Sketcher constraint/drag matrix: the
change consumes the existing resolved frame and does not modify Sketcher
geometry, constraints or persistence. Exact user face 18.36 was not singled out;
the user-document View test selected the visible planar face `step:face:#1771436`.
The Body translation applies uniformly to both references.
At that stage the original user Part, user configuration and custom
`ZE-TEXT.symz` retained their pre-investigation SHA-256 values. The user then
saved a newer Part at 05:04:20; its separate snapshot and subsequent investigation
are recorded in [STEP datums and dimensions](20261014-step-datums-and-dimensions.md).
The root development launcher is unchanged.

## Selected navigation experiment

Twenty warm framebuffer samples per state on `02.prtz`, at 662 x 699 logical
pixels, reproduce the slow selected display wire.

| Mode | Original selected median | Experimental selected median |
| --- | ---: | ---: |
| Wire | 143.537 ms | 138.332 ms |
| Shaded with Edges | 141.903 ms | 145.405 ms |
| Shaded | 140.015 ms | 152.208 ms |

Reusing projected endpoints, calculating segment length once and submitting
stack-backed convex rectangles did not yield consistent improvement and was
reverted. The remaining selected pass paints 55,980 antialiased screen-space
segments. No new rotation speedup is claimed. Logs are
`build/navigation-02-azure-before.log` and `build/navigation-02-azure-after.log`.
Endpoint captures differ by at most one channel; document state and base mesh
revision remain unchanged in the navigation contract.

## Accepted selected-wire batching

The selected pass now submits the same ordered screen-space segments in one
`drawLines` array, with the same selected color, width, antialiasing and flat
ends. It replaces per-segment convex rectangle submissions. Source/reference
geometry and the common picker are unchanged.

| Mode | Original median / p90 | Batched median / p90 |
| --- | ---: | ---: |
| Wire | 143.537 / 149.314 ms | 79.447 / 84.183 ms |
| Shaded with Edges | 141.903 / 152.698 ms | 76.373 / 80.621 ms |
| Shaded | 140.015 / 144.279 ms | 72.648 / 83.006 ms |

The twenty-sample user-document navigation test passes, including unchanged
document state and base mesh revision. All four selected Wire/Edges endpoint
images have zero changed pixels. Selected Shaded endpoints differ on two and
one pixels respectively, by at most one channel; unselected shaded images also
show bounded GPU rounding differences. The existing one-channel image oracle
passes. Log and captures are `build/navigation-02-line-batch.log` and
`build/navigation-02-line-batch/`. These timings are machine-specific observations.
