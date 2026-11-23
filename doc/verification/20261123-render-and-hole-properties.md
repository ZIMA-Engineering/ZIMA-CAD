# Render, Sketcher and Hole/Holes Windows verification

Verification on 2026-11-23, MSVC, Qt 6.11.0 and OCCT 8.0.1. Signed Windows
[2026112301](../releases/2026112301.md) passed the separate publication gates:
public download, publisher signature, safe extraction and updater trust/current
status. Its GUI/CLI/updater binaries match the tested candidate and the installed
local development binaries. See the [publication evidence](20261123-windows-2301-publication.json).
The shared C++ implementation is used on both platforms; native Linux execution
remains unverified for this change.

## Render scope before the gallery extension

The initial approved native extension stores Render Bodies, selected source identities,
camera/light definitions and settings inside the Part. Existing material/FEM
definitions use their established serialization. No required sidecars or legacy
migration paths were introduced. The renderer consumes calculated solid Bodies
and private immutable snapshots; it does not calculate CAD geometry.

| Area | Finite verification |
| --- | --- |
| Definition and Body lifecycle | Native round-trip, validation, visible/explicit source modes, changed/suppressed/deleted sources, Boolean results, dependency direction, material/FEM exclusions, Undo/Redo |
| Placed objects | Camera, point light and sun; numeric/reference placement, whole-Origin, replacement, independent inspection, Cancel, unchanged OK, native reference loss and retained frame |
| Interaction | Internal Properties, command availability, Tree selection and double-click inspection, shared middle-button confirmation, revision/calculation counters |
| Rendering | Independent ray/triangle, camera, bounds and light checks; curved/sharp Bodies, shadows, reflections, opacity, deterministic worker output, private source ownership |
| Export | Shared File Export action; JPEG decoded at requested size, lossless PNG compared with the independently invoked renderer, export Cancel, stale/cancelled result rejection |
| Background lifetime | High-sample cancellation, changed settings while running, stale publication prevention |
| Window layout | Normal and 1080p-sized main window; growing canvas, compact footer immediately below the image, progress hidden after completion |
| Localization | All five catalogs validated; actual Render/Camera titles and controls checked after changing language |

The table above describes the previously tested result-window implementation.
The subsequent gallery acceptance below supersedes its result-window and stale
result rejection behavior; an earlier-state snapshot now remains usable.

## Separate tab, retained gallery and concurrent modeling

The updated actual Windows GUI matrix passed at normal and 1080p-sized windows
in 16.22 and 15.46 seconds. It covers the separate tab, return to Modeling,
closing/reopening the gallery without cancelling its worker, explicit Stop,
image retention, inline rename, deletion and Undo/Redo. Two retained images
survived native Save, actual GUI document Close/Open and gallery reopening
without another render. PNG exports matched an independent renderer invocation;
JPEG decoded at the configured size. Five-language checks include gallery
controls and existing scene/object Properties. Light/dark/light changes preserve
readable text, the image and document revision. The image uses additional window
height; its footer remains adjacent and completed progress controls disappear.

During a 256-sample job, the test changes source appearance and Render exposure,
opens ordinary Extrusion Properties and confirms a doubled source thickness.
The explicit model edit completed in 90/89 ms on this small synthetic fixture.
The job completed, remained exportable/retainable, and was labelled historical.
Its retained PNG matched the independently rendered **pre-edit** input byte for
byte. No CAD calculation occurred from rendering; only explicit Extrusion OK
calculated material. These timings do not establish performance on large models.

Actual camera View single-click selects its Tree row without dimensions;
double-click exposes annotations without opening Properties, calculating or
changing revision. Passing the selected ID by value keeps it alive through the
scene refresh. Render marker wires now use the existing ordinary marker picker;
native checks cover both Render and the existing FEM marker behavior.

The common viewer suite exposed an older assertion expecting only two Reference
to outline selection kinds. The implementation has also accepted native Sketch
segments/curves since commit `4c828d0bf`. The assertion now verifies all four
established kinds; no Reference selection or Sketch equation behavior was changed.

Fifteen focused native suites passed in 15.82 seconds before the subsequent
Holes endpoint change: Render image/document/operations, Hole rollback, history
recovery, model calculation, multibody, FEM document/operations/condition,
Body link/scale/command and image/View export. Holes and its dependent recovery
checks are recorded separately below. This is not exhaustive application testing.

A native image inspection used three material objects and a floor: 640 x 400,
128 samples, 1,780 triangles, 74,959,245 traced rays, 8.01 seconds on this host.
The image showed curved highlights, shared shadows and reflections. This is a
single measured scene, not a universal speed claim. Low samples retain visible
noise. Assembly rendering, glass refraction, textures, animation, depth of field
and denoising are outside the delivered scope.

## Hole rollback after a failed predecessor

The real pre-Hole boundary must be shown even when an earlier failed operation
has retained only a lightweight cached boundary. The repaired path restores
that known prefix using the existing failure recovery, without treating the
failed definition as valid or substituting the cached final Body.

Native and GUI checks cover synthetic failure and a copied user document.
The copied case restores 5,076 input triangle indices while its 6,039-index
final result remains unchanged. Three successive openings, cached reopening,
changed-depth Cancel and unchanged OK preserve revision and history. Original
user files are not overwritten. Synthetic and copied GUI runs passed in 4.16
and 3.89 seconds respectively.

## Holes properties and endpoints

The Sketch button uses the shared compact work-plane row beside **In View**.
The existing Windows dialog matrix passed 660 dialog/language/window-size
combinations with no failures, including Holes and all five languages.

The axis endpoint regression initially failed because the explicit drilling
axes had no endpoint references. Holes now reuses `profile_centerlines::endpoints`
for preview and calculated reference packets. The endpoints are the actual
segment limits; the decorative axis overhang does not move them. Identity derives
from the persisted feature and segment, independent of list order.

Native checks independently compare endpoints with authored world coordinates
after diameter changes, Undo/Redo, crossing segments, reorder, construction-line
exclusion, plane rotation/translation, reversed blind depth, save/reopen and cold
regeneration. Shared start points keep distinct axis ancestry. GUI checks click
both endpoints through the ordinary Point reference command, assert no CAD
calculation/document change during selection, and exercise Cancel and dependent
Point persistence/Undo/Redo. The full Holes workflow also covers conversion,
owned Sketch return, pending edits, diameter/offset value changes and dimension
layout dragging. No Sketch solver or shared placement contract is changed.

## Native assets and protected data

The inventory covers 1,210 tracked native assets and six start templates.
The changed native helper regenerated and reopened all six templates; their
bytes remain identical. Automated GUI creation confirmed active Part/Assembly
editing contexts and normal commands. Four factory FORM definitions were
recalculated, saved and reopened on isolated copies with history/roles/reference
and physical-result checks. Original factory files remained byte-identical.
The complete packaged factory-load gate remains part of release acceptance.

The protected-project backup contains 172 verified files, including ten native
documents. Both Drawings saved and reopened on copies. Eight Part/Assembly
documents use an already unsupported format: the candidate and the published
2206 runtime both reject them. Their modeling/reference operations therefore
remain unverified; no conversion or original replacement is claimed. All
protected originals retained their recorded hashes. Private project identifiers
and copies are excluded from Git and release artifacts.

## Final integration checks

The separate [Sketcher matrix](../SKETCHER_OFFSET_FOLLOWUP_20261123.md) covers
circular Offset tangency and virtual arc/Offset rounding, including immutable
external sources, repeated dimension changes and actual point dragging.
Its final 78-case native matrix passed in 0.99 seconds, six dependent native
suites in 61.62 seconds and the 16-case native/external Offset GUI matrix in
26.54 seconds. The final native helper repeated the template, FORM and protected
copy audit successfully; all original hashes remained unchanged.

The current Holes GUI workflow passed in 13.65 seconds and the supplied Hole
rollback copy in 3.26 seconds. The expanded 38-case curve-fillet GUI matrix
passed in 134.24 seconds. Current template GUI creation passed in 3.02 seconds;
the full FEM Body GUI workflow passed in 76.69 seconds, including static and
thermal calculations, combined loads, measurement points, source selection,
placement, Undo/Redo, native reopening, languages and themes. This checks the
shared calculated-source path reused by Render.

The 660-case property-dialog layout suite passed in 52.70 seconds and translation
catalog/coverage checks in 14.29 seconds. Sketcher changes reuse existing UI text;
all new Render labels and errors are present in cs/en/de/fr/ru. These are local
checks; the packaged-runtime evidence is recorded separately after publication.

## Committed Windows candidate

Candidate `2026112301` was built from `1a5036d555894dd4c4399179393f7a69f6ff58b7`.
All twelve sheet-branch gates passed in 169.51 seconds, and FEM GUI passed in
74.99 seconds. The independently extracted candidate passed dependency, archive,
launcher, PDF/JPEG/font, native factory and GUI smoke checks. All 1,185 packaged
factory native assets loaded successfully.

Nine additional GUI workflows passed using the packaged executables, libraries
and localization catalogs:

| Workflow | Seconds |
| --- | ---: |
| Render, gallery and background modeling | 15.40 |
| Render compact layout | 14.89 |
| Holes workflow and axis endpoints | 9.04 |
| Hole rollback after invalid input | 2.29 |
| 16 native/external Offset contact cases | 25.68 |
| 38 native/virtual curve-fillet cases | 131.37 |
| Part/Assembly start-template contexts | 2.61 |
| Multi-line arc contact regression | 25.26 |
| Copied-circle contact regression | 31.05 |

The packaged Render workflow measured 125 ms for the concurrent Extrusion
Properties/OK edit on its small fixture. This is finite responsiveness evidence,
not a bound for large production models. The reopened gallery was also visually
inspected with two retained images and Czech controls.
