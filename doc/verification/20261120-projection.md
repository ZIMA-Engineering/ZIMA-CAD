# Projection verification

Status: targeted Windows acceptance and signed Windows 2026112004 publication
acceptance passed. The public release was published on 2026-11-20 at 21:41:22 UTC.
See the [publication evidence](20261120-windows-2004-publication.json).

## Scope and environment

Projection consumes the ordinary Feature-owned planar Sketch, placement and
history lifecycle. Its three outputs are native spatial curves, material-
preserving face partitions, and explicitly selected independent surface patches.
The shared placement solver is unchanged. Windows and Linux share the C++
implementation and OCCT 8.0.1 dependency pin.

Local execution uses Windows, MSVC 19.51, Qt 6.11.0 and OCCT 8.0.1. Linux native
execution remains unverified. These are synthetic contract fixtures, not a
performance claim for arbitrary customer models.

## Finite acceptance matrix

| Suite | Coverage | Status |
| --- | --- | --- |
| `directional_projection_tests` | 26 native geometry cases: plane/cylinder/cone/sphere/doubly curved targets; source lines, circular/elliptical arcs and splines; oblique and reversed direction; zero-distance contact; holes, seams, partial occlusion, crossing targets, ambiguous and missing spans; closed/open partition, stable native ancestry and independent endpoint rays | Passed, 0.15 s |
| `feature_parameters_tests` | Projection modes and direction serialization, invalid definitions, retained inactive choices; existing Feature parameter contracts | Passed |
| `projection_history_tests` | All three outputs, independent before/after material differences, native topology, cold regeneration, nested annular and adjacent regions, lost-region rejection, Surface Thicken, Shell, directed native edges, bounded external faces and three successive native-curve Surface Trim edits | Passed, 1.34 s |
| `projection_document_tests` | Transactions, failed OK without mutation, warm/cold no-op, repeated source dimensions and drags, target dependency, Undo/Redo, native save/reopen, external sources in four placed/nested occurrence variants | Passed, 1.64 s |
| `feature_type_ui_tests` | Projection shared reference fields, append/replacement/removal, independent inspection, duplicates, retained choices across output changes, fixed compact columns, language changes in all five languages and both themes; complete existing rendering harness | Passed, 20.03 s |
| Product GUI `ZIMA_VERIFY_PROJECTION_ONLY` | Actual View face/region and axis picking, whole-Origin placement, preview counters, confirmation, cancellation, reopening, Tree/View inspection, repeated source dimensions, Undo/Redo | Passed, 18.19 s |
| Start templates and factory assets | Current implementation rewrite/reopen, new-document GUI context, committed factory inventory and runtime loading | Six templates rewritten/reopened without byte changes; metric and document-options GUI passed, 46.60/45.37 s; packaged factory loading pending |
| Related regressions | External profile target capture, General Surface, cylindrical Up To GUI, localization catalogs | Passed, 5.65/29.95/7.15/67.46 s respectively |
| Windows distribution | Committed-source build, dependency closure, native smoke, archive/signature verification, public download and updater trust | Pending |

The passed source-edit matrix has 48 combinations: three outputs, radius/diameter dimensions,
driving/reference state, locked/unlocked driving values, free/fixed centers, and
three successive value edits plus center drags. It checks the independent radius
equation, permitted or forbidden center movement, native projected-circle
equations, and Sketch serialization after each action. A single-support circle
dimension has no pair-selection order or contact-side choice. Separate mixed
input coverage exercises disconnected lines, circular/elliptical arcs, ellipses,
and open/closed native splines through the document adapters. The matrix performs
144 successive value edits and 144 center drags. The product GUI separately
performs three successive radius edits through the displayed dimension editor.

External target checks cover two independent Assembly branches and nested
occurrences, translated/rotated frames, unsaved authoritative source changes,
native embedded snapshots and read-only sources. Both Curves and Surface outputs
work without preceding material in the destination Body. Missing sources retain
the last valid frame and reject incomplete regeneration. Skeleton restrictions
and direct/indirect dependency cycles are rejected. External Split remains
unsupported because it would modify material owned by a different document.

An extra closed-circle occlusion fixture exposed a generic section approximation
on an exactly planar spline target. Planar projection now preserves the native
source basis and clips it against the forward curtain and actual bounded target.
Spline-plane recognition certifies all positive-weight poles at roundoff
precision; it does not flatten an almost-planar surface. The independent checks
retain their original geometric tolerances and compare the native spatial curve
with its surface parameter curve. An inaccurate parameter curve is repaired or
rejected, never accepted by widening the test tolerance.

The complete rendering harness also exposed selected Plane wires being painted
over by coincident passive Plane wires. As for existing Axis rendering, selected,
inspected and preview Plane wires now render after passive Plane wires. This
does not change placement or replace wire feedback with filled faces.

## Packaged Windows GUI checks

The committed 2026112004 candidate passes the Projection GUI case (18.245 s),
General Surface context/reference case (9.799 s), ordinary Loft case (34.457 s)
and mixed rectangle/circle/slot Thin/Shell case (49.976 s). The packaged GUI,
CLI and updater all report version 2026112004 and commit
`0d571813e2f64ee917efe9d76bf5e7049c13dd92` with unmodified source identity.
The mixed Thin and Shell screenshots were also inspected.

The first portable Loft harness run failed while loading its manually overridden
translation path: it assumed `config/localization` under the process working
directory, whereas the portable factory catalogs belong to the versioned runtime.
Rerunning that unchanged packaged binary with the runtime working directory
passed the five-language Loft checks and the mixed Thin/Shell case. This was a
test-launch configuration correction; no catalog or product check was bypassed.

## Existing failing control

`zima_cpp_feature_ordered_up_to_native_tests` fails with `Invalid ordered Feature
Up To chain` after its eight Assembly Through-all preview checks. Rebuilding
and running that exact control from accepted pre-Projection commit
`39bcecb4ba603e0aca5f69a553a10b5764a5adaa` produces the same failure. This is a
recorded pre-existing limitation, not a passing Projection check. The separately
targeted cylindrical Up To mouse/preview contract passes. Baseline build and run
logs are retained locally as `baseline-ordered-build.log` and
`baseline-ordered-tests.log` under `build/projection-20261120`.

This does not establish exhaustive Sketcher coverage. Arbitrary source-curve
constraint combinations, tangent-contact selection orders, trim-after-edit,
external Sketch supports and every mouse drag variant still require their
applicable existing suites or explicit additional checks. The Projection tests
must not be described as a general solver certification.

## Persistence and protected data

The approved contract adds a required Projection definition only to the new
Feature type. Existing Feature definitions and native format discriminators
are unchanged. Native output and external source snapshots live in the native
document; no required sidecar or revision directory is introduced. Initial
region pick coordinates classify a patch only once; persisted native boundary
ancestry controls subsequent regeneration.

The pre-change inventory records 1,185 tracked native factory assets and eleven
protected Toyota documents with matching recoverable backups. A post-test hash
comparison confirms every one remains byte-identical. Private originals
are not conversion targets. Previously unsupported Toyota Part/Assembly formats
remain unsupported; this change does not add a legacy reader. Their reopening
cannot be reported as verified merely because current-format fixtures pass.

## Scene and calculation ownership

Shared Feature editing supplies the rollback scene. Projection supplies only
source/output overlays from the current viewer triangles during interaction.
Those samples never become native modeling input or persisted reference curves.
Explicit OK/Regenerate performs exact projection and, when required, partitioning.
Ordinary feature selection consumes the same authored preview generator through
the existing presentation cache. Counters must demonstrate no native history
evaluation for preview, inspection and unchanged confirmation.

## Windows publication and local delivery

Immutable tag `ZIMA-CAD-2026112004` identifies source commit
`0d571813e2f64ee917efe9d76bf5e7049c13dd92`. Candidate and signed native smoke pass,
including every one of the 1,185 factory native assets and portable drawing/font
export. The twelve mandatory sheet-branch gates pass on the committed build.
The signed ZIP contains 17,711 files and is 970,479,292 bytes; its SHA-256 is
`bf28e74005fa05713b61dfdd0a92ae18a1288344f139596ca6072394c2a09fe1`.

All three GitHub asset digests match local signed files and anonymous downloads.
The public archive passes safe extraction and Ed25519 publisher verification.
The production updater reports `trusted: true` and version 2026112004 as current.
Public, signed and candidate GUI/CLI/updater binaries are identical. The normal
local runtime was replaced with those binaries, retaining a previous-file backup;
the three local configuration files and root `zima-cad.bat` remained unchanged.

This documentation-only finalization adds no user-visible application text. The
five-language implementation coverage remains as recorded in the matrix above.
Linux execution and the explicit earlier control limitations remain unverified;
publishing the Windows package does not erase those limits.
