# Exact surface references: proposed persisted contract

Status: the user approved this scope, including the native-format change, on
2026-11-25. Implementation and verification are in progress. The later instruction
authorizes commit, push and Windows publication after the repairs and acceptance
gates pass. Publication remains stopped while those gates are incomplete.

## Reason for the change

General surface placement currently intersects and projects onto persisted
triangles (`placement_mesh_surface.hpp`, `placement_mesh_constraints.inc`).
Those triangles cannot define an exact surface normal or offset. Existing
analytic supports and exact spline control geometry already have independent
mathematical representations. The native BRep archive contains additional exact
geometry, but decoding it with OCCT during reference entry would violate the
established no-kernel interaction contract.

## Proposed scope

Persist an immutable mathematical surface description with the existing semantic
face reference. Capture it during explicit body calculation. Preserve existing
analytic definitions, and add rational tensor-product spline data and the
required exact procedural supports (extrusion, revolution and offsets), with
their parameter domains, oriented sides and exact trimming boundaries. Reuse
existing exact curve data for generators and boundaries. Do not invent identities
from kernel enumeration or store triangle indices as attachments.

Shared native C++ evaluators will calculate positions and derivatives. Projection
and constrained contact will use bounded numerical solving with explicit residual
checks, deterministic branch ordering and the existing persisted branch choice.
Maintain the last valid frame when a support is missing or a solution cannot be
established. A failed solve must not silently use a triangle or a tangent plane.

Cache only immutable source evaluation data and input-dependent solutions. Reuse
must be invalidated by changed source geometry, transforms, references, offsets
or sides. Ordinary hover/selection and unchanged OK must not calculate bodies.
Measure cold and warm placement, repeated edits, source changes and regeneration;
report accuracy, operation counts and timings together.

All required data remains inside native documents. No sidecar, legacy execution
path or external geometry cache is proposed. The final schema/version change
will cover the shared calculated/reference packet wherever embedded in Parts,
Assemblies and Drawings; unchanged history fields and extensions remain intact.

## Assets and conversion

Read-only Git inventory on 2026-11-25 found 1,036 factory `.prtz`, two `.asmz`
and 137 `.drwz` files under `config`, including start templates, four FORM
definitions, engineering-table models, title blocks/symbols and reference
drawings. Determine actual affected packets with the native reader. Regenerate,
save and reopen every affected factory asset with the changed implementation;
verify FORM role Bodies and insertions, hierarchy, references, sides and source
dependencies. Commit all affected factory data with the code. GUI-check new
documents created from the actual start templates.

The protected `ZE0176-TOYOTA-G92K1X` project contains seven `.prtz`, one `.asmz`
and two `.drwz` files (1,466,074,680 bytes in this inventory). Before conversion,
resolve its dependency inventory with the native reader, make a recoverable copy
and verify file hashes. Test conversion, reopening and dependent operations on
isolated copies only. This proposal does not authorize replacing user originals.
Preserve other custom libraries and already embedded independent definitions.

## Verification gates

- Compare point coordinates, normals and signed offsets against independent
  analytic fixtures and native-kernel evaluation at calculation time; vary
  display tessellation without changing reference results.
- Cover curved spline and procedural surfaces, trimmed domains and holes,
  transforms, both orientations, positive/negative/signed-zero offsets, repeated
  movement, multiple branches, singular and impossible contacts.
- Exercise point/axis/plane constraints, source edits, insertion of later
  Extrusions, three successive regenerations, native save/reopen and Undo/Redo.
- Verify actual GUI placement, independent inspection, replacement, Cancel,
  no-op OK and failure-frame retention. Check existing analytic and spline paths.
- Review all five locales; evaluator changes must not introduce untranslated
  errors. Record Windows results and explicitly retain Linux verification gaps.

No release or compatibility claim is justified until these gates pass. Format
approval is separate from permission to restart publication.

## Implementation checkpoint

The native evaluator and calculation-only OCCT capture adapter have passed
standalone Windows tests comparing polynomial/rational values, first and second
derivatives, procedural and nested offsets, orientation, reflections, scale,
singular rejection, exact trimming and mathematical JSON round-trips. These
checks do not yet establish the complete document lifecycle contract.

Integration now carries immutable exact support data with semantic face
references, transforms it with the owning geometry, and persists bounded patches
independently when multiple patches share one authored identity. The old
triangle-based contact implementation has been replaced in the working tree.
The first integrated Windows build and placement matrix passed, including exact
surface capture, signed sides, bounded contacts, alternatives and packet
round-trips. The downstream Wrap regression also passed three regenerations,
native reopening and Undo/Redo after an additional Extrusion. The expanded gates
below remain required; native format promotion and factory/user-copy conversion
have not been performed.

The existing Flat attachment matrix exposed two independent regeneration defects:
Sheet Profile refresh read the final Body instead of its own input boundary, and
Bend endpoint refresh dereferenced an absent standalone point marker even though
the native edge carried the exact endpoint. Refresh now consumes the history
input boundary and the edge's identity/position pair. The Windows Flat matrix
passed for trimmed fragments on both skins, endpoint/midpoint attachment,
Flat/Bend/Profile, placed Bodies, regeneration, native reopen and Undo/Redo.
The broader Bend and sheet-branch gates remain required after integration.

The evaluator has no mutable shared working state. Its immutable inputs permit
independent calls from multiple CPU workers. This is not a claim that all contact
searches or history operations are scheduled in parallel. Dependency ordering,
bounded worker counts, deterministic results and measured overhead remain the
criteria for any later parallel scheduling. GPU execution is outside this change.

Ten protected-project native documents now have isolated, SHA-256-verified
backups under the local verification workspace. Original hashes were rechecked
after copying. The seven Part documents use native format 50 and the Assembly
uses format 37; they predate the current product formats independently of this
change. Explicit offline verification/conversion must account for that fact
without adding legacy paths to the product reader.

Linux validation, final factory conversion and
protected-project conversion remain open verification gates. The user has now
requested continuation through commit, push and a Windows GitHub build after
the repairs are finished and verified. Publication is authorized conditionally;
no release has been created and the verification gates remain binding.

## Expanded verification checkpoint

The actual Point dialog matrix exposed two integration defects: curved offset
updates published only constrained Cartesian components, and owned Curve/Sweep
Points captured View hits only for the older analytic support types. The shared
controls now publish the complete unlocked projected result, and the existing
owned-Point capture path also recognizes exact general supports. Construction
orientation uses the normal at the exact contact, including after offset edits.

An elliptical extrusion then exposed numerical termination at a nearest-point
solution: squared-distance improvement rounded to zero before the raw parameter
gradient crossed its threshold, discarding the interior solution. Termination
now checks a physical tangent correction below 1e-8 model units, while retaining
equation residual and exact-domain validation. A native regression repeats
0, +1, -1, +0.5, -0.5 and 0 offsets at six ellipse parameters and both orientations.
It failed before the correction and passes afterward, together with the existing
exact-surface, OCCT-capture and document-placement matrices. The contact search
uses finite deterministic multistart numerical solving; these tests do not prove
exhaustive global root discovery for arbitrary surfaces.

All eight circular/elliptical Curve/Sweep Point GUI configurations now pass
actual clicks, repeated signed offsets, transformed frames, Cancel, native
persistence and Undo/Redo (31.83 seconds on this Windows host). Elliptical Sweep
confirmation exposed repeated numerical drift of approximately 3e-10 model units.
The contact solver now applies its final small correction, and placement retains
a validated fixed point only within 64 machine epsilons at the coordinate scale.
It still evaluates the current mathematics and trim on every solve. A further
56-case native matrix checks twelve identical successive resolutions, signed
zero/both orientations, and movement after a real source change. The previously
failing saved Sweep settles on its second native pass.

The native Sheet Transition, Sheet Exchange and Bend end-constraint checks pass.
The mandatory sheet-branch GUI and its common-parent fixture also pass (99.32
seconds together). Projection GUI and pending Curve Point GUI pass. Section GUI,
including Drawing defaults, passes in 22.10 seconds with its dedicated entry
point; an initial invocation accidentally set the Console-only selector instead
and did not exercise Sections. General-surface Point GUI passes against the
independent trimmed OCCT shape (86.06 seconds). The previous bilinear fixture
assumption was invalid: a four-boundary filling does not guarantee that equation.
Polar datum GUI passes (6.15 seconds), explicitly disabling the automatically
offered second orientation before checking a single-normal/free-azimuth case.
Surface Trim, Intersection, Planar Surface and General Surface GUI pass, as do
the Sketch dimension-entry matrix and the sewn-boundary/Thicken/OCCT Fillet case.
The spline-tangent drag gate passes for B-spline and interpolating curves,
including self-tangent closure, shared endpoints, three real mouse drags,
independent tangent equations and Undo/Redo (10.95 seconds). Its initial failure
was a test viewport issue: the unconstrained point moved beyond the scanned
pixels. The test fits the result before its existing movement assertions.

All 1,063 tracked Part-based assets (including tool definitions and native test
fixtures) received an isolated first comparison. Existing caches omit newly
captured support and continuation metadata. The offline verification now permits
only additions of those declared fields, preserves comparisons of every existing
field, and requires a second regeneration to reproduce the entire new contract.
Pending physical properties are calculated from the saved original BRep, rather
than treating unavailable values as verified zero or replacing the input oracle.
The strengthened comparison completed: 1,061 of 1,063 assets passed. The two
remaining spherical wheel nuts differ only in resolved Euler coordinates at
roundoff (-90 to -90.00000000000001 degrees, and -0 to approximately -4e-31).
The offline comparison now permits only derived rotation changes below 1e-12
degrees, retaining exact checks of authored angles, offsets and sides and the
strict second-regeneration requirement. Both assets passed reverification. No
factory asset has been promoted; a final run after format promotion remains due.
All 137 factory Drawings passed staged source regeneration, authored-sheet checks,
measurement/source-identity comparison, native reopening and repeated regeneration.
The original Parts are separately regenerated for the comparison baseline, since
several saved Drawing caches predate their sources. Reopening may reorder
measurement arrays; exact readback compares their full contents by source identity.
Only derived BOM names/designations/paths are excluded from authored comparison;
quantities, parameters, masses, materials and source document identities remain
checked. No numeric tolerance is applied to native packet readback.

The protected skeleton Part now loads on an isolated prepared copy. Explicit
offline preparation restores the intervening schema's inactive defaults and
retains the original native BRep solely as an independent comparison oracle.
This is not a successful geometric conversion or a published asset: calculation,
original-geometry comparison and dependent-operation checks remain required.

The explicitly approved obsolete 2026112502 archive and four packaged GUI work
trees were removed after preserving and hashing their logs, reports and images.
The signed 2026112503 archive and all user documents remain untouched.

## Current-format preparation and imported support audit

The working implementation now requires Part INI 57 / JSON 80, Assembly INI 39 /
JSON 51, and Drawing INI 22 / JSON 14. The application has one current reader;
older approved inputs are prepared only by the explicit offline verification
tool. Factory source files have not yet been promoted or committed.

All 1,063 Part-based assets and 137 Drawings have passed schema preparation and
native definition readback in `build/ex57/prepared`. Twelve Drawings contained
older Part-70 embedded Sketch-only frames/title blocks; those snapshots were
explicitly prepared with their authored geometry retained. Preparation is not
calculation verification. The final Part calculation batch is running, followed
by Drawing and packaged/template GUI gates. Preparation receipts chain each
output hash to its unchanged repository source and the authoring executable.

The protected G92K1 import exposed 49 spline-support captures whose trim bounds
extend slightly beyond the nonperiodic knot domain. Inserting knots at those
trim bounds raised OCCT `InsertUKnots` / `InsertVKnots`. Capture now retains the
complete nonperiodic basis and its end-span extrapolation; trimming remains an
independent exact definition. Periodic directions are unrolled on private copies.
The OCCT regression failed before this fix and now passes, including boundary
values, derivatives, signed offsets, reversal, persistence and periodic U/V seams.
An independent audit of 6,355 imported faces compared 57,195 points and normals:
zero failures, maximum point difference 6.88e-13 model units and normal difference
4.03e-10. The full G92K1 comparison, native save/reopen and second regeneration
passed in 559.94 seconds while other verification jobs ran. This is a multi-pass
offline audit time, not an interactive performance claim. The protected original
was not changed.

## Protected older-model findings

The skeleton's format-50 Loft omitted the explicit correspondence introduced in
later formats. An empty default twisted its second profile: saved longitudinal
edges of approximately 18.5 units became 25 to 51 units, and the downstream
Fillet failed. An A/B diagnostic using the committed HEAD `occt_kernel.cpp` with
the current document adapter reproduced the same failure; this is not evidence
of a defect introduced by exact-surface capture. Offline preparation now restores
the two-profile correspondence from the saved rail endpoints and native point
identities, including the reversed second profile. It rejects missing or
ambiguous evidence. The changed parameter definition is saved without attaching
an obsolete calculated cache; the unchanged source remains its independent
geometry oracle. Full skeleton recalculation and comparison are still pending.

Part ZE0176-0101-0201 has an expected older-contract difference: its two Bends
fork from the same Flat. The mandatory independent-sheet-branch contract retains
the two separate child branches and their parent contact faces. Native comparison
found four additional contact faces of 104 square units each, accounting for the
entire area difference of 416. Volume and both directed Boolean differences are
unchanged; the committed HEAD kernel reproduces this result. The original and
current BReps are valid. The current result passes full native readback and two
further exact-contract regenerations. This does not assert that every old compact
intermediate boundary can be compared: several old packets contain neither
physical properties nor a native BRep. Original files remain untouched.

## Current-format factory promotion checkpoint

All 1,063 Part-based assets and all 137 Drawings passed the final format-57/22
calculation, retained-contract, native-readback and repeated-regeneration gates.
Their preparation and calculation receipts bind the original, prepared and
calculated SHA-256 values to the verification executable. Together with the two
native-roundtripped Assembly start templates, 1,202 tracked documents were
promoted after a complete hash preflight and checked again after copying.
`build/ex57/promotion-manifest.json` records the exact before/after hashes.
This is local factory promotion, not publication of a release.

Actual GUI new-document creation from the promoted templates passes for mm and
in in all five supported languages, including the active editable context and
normal commands. Native exact-surface, capture, placement, curve-placement,
wrapping document/history, native-document and Assembly-contract tests pass.
The broad Part contract also passes after correcting a stale test setup that
changed Fillet end modes after calculating its save packet. The Drawing contract
now creates its fixture with the current native writer and explicitly rejects
the unsupported older schema; it passes as well. The original old-format files
under `tests/fixtures/cross_language` were not relabeled or silently converted.

The protected skeleton and G92K1-100 checks and protected dependent documents
remain pending. Native Linux and packaged Windows execution remain unverified
for this change. Commit, push and release publication have not occurred.

The promoted-format POKUS5 regression passes three explicit GUI regenerations,
native Save and valid Tree reference states after the additional Extrusion
(3.85 seconds). Translation source coverage and all five catalog/dialog checks
pass with the relinked current-format UI test runtime (18.60 seconds).

## Agreed follow-up after the repaired Windows release

Continue CAM completion and add Sketch contour dragging. A free Line should
move as a whole; existing incidence may instead leave a rotational degree of
freedom, producing lever-like motion about an anchored point. In particular,
an anchored midpoint permits opposite movement of the two ends. Do not impose
a blanket fixed-direction translation that prevents that valid motion.
Dragging a Circle contour should request a diameter change; its center remains
available through the existing point interaction. Existing constraints and
driving dimensions remain authoritative; fully constrained geometry cannot
escape them. This is an agreed follow-up, not implemented or verified here.
Apply the mandatory Sketcher dimension/drag matrix, independent equations,
successive edits, native persistence, Undo/Redo and actual mouse verification.


## Protected-project verification checkpoint at 17:34 CET

The corrected isolated skeleton passes all 34 authored operations, native
save/readback and a second complete calculation. Comparing those two current
results finds identical authored history and Body ownership, an empty complete
reference/property contract difference, valid BReps and empty Boolean differences
in both directions. The two-view skeleton Drawing also passes current source
regeneration, annotation/measurement identities, native readback and repeated
regeneration. Its copied absolute source paths were redirected only to the
isolated Part with the same document identity; originals were not edited.

Comparison with the older saved skeleton remains unresolved. Both have 1,176
faces and equal support-type counts; some old/new rounded patches differ.
Fixed-quadrature diagnostic volume is not the authoritative adaptive physical
volume, and both cross-version Boolean differences returned invalid shapes, so
neither result proves equivalence. An A/B calculation of the corrected definition
with the committed HEAD kernel is running to isolate the older contract from this
change. The original model must not be replaced on the basis of this comparison.

The second protected Drawing, `ZE0176-0101-0001.drwz`, passes native preparation
and readback but cannot regenerate: its stored source is the absent
`ZE0176-0101-0001.prtz` (document `01a1727cba3b743198868ba064a8d3f2`). None of the
seven inventoried Part copies has that identity. Source location clarification
was requested; no different Part was substituted.

The G92K1-100 support audit passes 6,344 faces / 57,096 independent point and
normal samples (maximum differences 8.20e-13 and 3.92e-10 respectively). The
multi-pass strict audit reached its 1,200-second limit. Ordinary offline authoring
reached a 600-second limit during its second physical integration; geometry
recalculation completed in 21.34 seconds, and its first span-aware volume pass
took 379.74 seconds. These timeouts are not passing lifecycle gates. A separate
ordinary CLI Regenerate/Save run is measuring the actual user operation without
the authoring tool's additional full-history replay. No integration tolerance or
supported behavior has been reduced to shorten the audit.


The committed HEAD kernel A/B calculation has now completed. It produces the
same skeleton geometry as the changed kernel: equal areas/adaptive volumes,
valid BReps and valid empty Boolean differences in both directions. Authored
history and Body ownership match. Therefore the remaining discrepancy against
the older saved skeleton is not introduced by the exact-surface change. The
comparison intentionally still reports new exact-reference metadata separately.
The original remains untouched; this result does not authorize silently replacing
it with the recalculated copy.

Observed placement timing (Windows Release, finite fixtures) is approximately
2.34 ms per contact with 20,000 reference triangles and 1.04 ms on the elliptical
fixture. Two thousand exact value/derivative evaluations took 44.41 ms. These are
local measurements, not a universal latency guarantee. Large imported physical
volume integration is a separate operation from interactive contact solving.

## Physical-analysis duplication investigation

The ordinary G92K1-100 CLI run confirmed a product-path duplication, independently
of the offline author's additional verification passes. Geometry took 22.44 s;
two consecutive span-aware volume integrations took 377.59 s and 339.76 s. The
complete Regenerate/Save script reached its 900-second limit during Save. This
is not a passing lifecycle result.

The measured Body record reads the local history boundary, while final-body
analysis reads its placed output. They have different source fingerprints, but
this model's local BRep and zero-placement output are identical. The existing
fingerprint-only workspace memo retained the aggregate without making the
completed child analysis available to the measurement request.

The proposed correction shares a bounded memo during explicit analysis. Reuse
requires byte-identical native geometry, identical ordered rigid placements and
the same surface/import integration modes. Compound markers and absent native
archives cannot establish equality. Memo entries retain only physical values,
their source key and shared immutable geometry; they do not copy viewer meshes or
history. Geometry, reference identities and tolerances are unchanged. Completed
values propagate to matching existing snapshots through the established path.

The added regression checks count actual integrations and cover the final Body
plus its measurement, an unchanged repeat, native save/reopen, a source edit,
retained source snapshots, differently placed identical archives and a changed
surface-integration mode. Build/test and measured real-model results are pending
at this checkpoint. No new user-visible text was introduced by this correction.

The completed Windows Release build and `zima_cpp_body_properties_tests` now
pass (89.78 seconds). The new checks verify exactly one integration for a final
Body and its measurement, no additional integration after unchanged analysis or
native reopening, and a fresh integration after a source edit, placement change
or integration-mode change. They also compare the retained geometry packet and
source snapshot. The real G92K1-100 Regenerate/Save measurement is still running;
the unit result is not a substitute for that lifecycle gate.

The real-model run rejected the first optimization as incomplete: geometry
finished in 20.30 s, then the first integration took 357.01 s, but a second
integration still started. The diagnostic was stopped after that evidence;
Regenerate/Save did not pass. Investigation now compares freshly calculated
inputs as well as the previously saved packets. No speedup is accepted from
this run, and publication remains blocked by the unfinished gate.

The skeleton's committed-HEAD/current reference comparison passes when storage
indices are replaced by their actual semantic identities. There are 1,342 old
and 1,374 new face records; every old and new record matches in both directions
after exempting only newly added exact-surface metadata. Existing supports,
sides and ownership are retained, and all other entities and reference curves
match. The 32 additional trim records shift subsequent packet table indices;
those indices are not persistent model identities. The existing strict factory
and current/current regeneration checks were not relaxed by this diagnostic.

Fresh calculation identified the real import distinction: the final output has
one rigid placement, while the measured local boundary has none. Both retain
the same 139,483,926-byte native archive and integration modes. The correction
now reuses the native integral across rigid frames through the existing centroid
and central-inertia transformation equations. It does not treat differently
placed values as equal, rewrite placement/side definitions, or reintegrate an
unchanged support. Tests compare forward, inverse and noncommuting chained rigid
transforms against independently integrated placed BReps, and the document-level
measurement fixture now has a nonzero placement. Verification is pending.

A subsequent oversized-Fillet fixture exposed the same storage-index difference
in the offline format verifier. Its declared-metadata migration path now uses
the semantic comparison when raw packet indices differ. The strict comparison
for repeated calculations in the current format remains unchanged. A dedicated
negative matrix must accept additional exact trims while rejecting changed edge
and point identities, axis geometry, an oriented face side, removed faces and
changed pre-existing exact trims. Existing physical-property and curve checks
still apply to every history boundary.

The separate POKUS import support audit passes 6,354 faces / 57,186 point and
normal samples, with maximum differences 7.28e-13 and 4.60e-10. Its complete
calculation/lifecycle check remains pending. The recovery copy of Part 0201
calculates all eight operations and passes native readback; it has the same
416-unit independent-sheet contact-area difference described above.

## Rigid-frame physical reuse and Save contract checkpoint

The native body-property matrix passes (92.48 s), including independently
integrated forward, inverse and noncommuting rigid-frame comparisons. The Save
suite exposed a stale expectation: it demanded deferred analysis after explicit
Regenerate, although the committed HEAD implementation already publishes current
physical Relations at that boundary, as required by AGENTS.md. The test now
requires that established behavior and counts integrations to ensure subsequent
Save reuses the accepted result. This correction changes no product behavior or
user-visible text. Its rerun and the real G92K1-100 lifecycle remain pending.

## Save and analytic-patch persistence checkpoint

The G92K1-100 CLI lifecycle now completes successfully: all five commands pass,
Regenerate publishes current analysis, Save leaves a clean document, and its
measurement remains valid. There is one native integration instead of the two
previously observed. Geometry took 20.66 s and the integration 378.98 s at unchanged
precision. Whole-script time was 679.29 s; small audits overlapped the final Save
phase, so that total is not an isolated performance benchmark. Cache encoding
still took 220.62 s and remains a substantial large-import cost. Actual GUI reopen
and two unchanged measurement inspections pass (664 / 584 ms for the inspectors;
109.23 s for the full opening harness). No general low-latency claim is made.

The oversized Fillet uncovered an additional persistence defect. Original-face
packet rows distinguished exact trim handles but still merged distinct analytic
or contour supports sharing one authored identity when the exact handles were
null. The writer now distinguishes those immutable support handles as well,
without changing owner/key/occurrence identities, side values, schema fields or
native extensions. A repeated round-trip test covers differently oriented
cylinders, including reversed sides, in placement and contour-only supports.
The affected format-57 factory data is being recalculated and verified again.

Native exact-surface placement, native-document and Save tests pass (8.33 s total).
The Save fixture now models an existing Assembly-owned cut directly: new material
cut creation has intentionally been disabled since committed HEAD. The fixture
still exercises supported cut editing, Family mass, Save and unchanged Save; it
does not re-enable obsolete creation behavior. The oversized current fixture
now passes the complete-history strict verification and repeated native
regeneration (13.87 s). A separate raw saved-packet comparison still distinguishes
ordinary authoring's deferred intermediate physical values from the fully analyzed
verification output. It is not an empty complete-contract comparison; the strict
gate independently analyzes the retained original BReps before comparison.
All code changes in this checkpoint introduce no user-visible strings.

## Final local factory and protected-project checkpoint

The latest analytic-patch writer passes all 1,063 tracked Part-based factory/test
assets, all 137 dependent Drawings and both Assembly start-template native
round trips. The Drawing run used the promoted current Part assets. Actual GUI
creation from mm/in start templates passes in all five languages. The 1,202
verified file hashes and preceding verification hashes are recorded in
`20261125-exact-surface-assets.json`; the packaged files must match them exactly.
All ten protected TOYOTA originals and backup hashes were rechecked unchanged.

G92K1-100's saved old/new semantic reference comparison passes: all 6,341 face
rows retain their identities and existing support/side data, reference curves
match, and only declared exact-surface metadata is added. Authored history and
Body ownership are equal; area and adaptive volume agree within the existing
verification tolerances. This supplements the successful CLI lifecycle and GUI
measurement checks above.

The POKUS import completes CLI Open/Regenerate/Save/Documents/Tree in 245.24 s;
all commands succeed and the measurement has no error. A separate reopen takes
51.83 s, leaves the document clean and retains the measurement identity and frame.
The isolated TOYOTA Assembly also completes all five commands, retaining its two
source/occurrence identities and leaving a clean document (732.35 s for the full
script). These runs overlapped other verification work and are not isolated
performance benchmarks. Assembly source loading is still expensive; this result
does not claim that all large-model I/O costs are resolved.

The old model-50 release fixtures have deferred or absent physical values in
some intermediate packets. Their retained final BReps compare as valid and equal
in both Boolean directions, and separately regenerated current copies preserve
the complete current contracts across another calculation. The Assembly-reference
fixture additionally repairs six derived FEM `reference_valid` flags (generic
and evaluated Family rows) and refreshes the evaluated Family mass from the same
unchanged final BRep. The raw Body-history comparison includes those derived flags;
it must not be described as an ownership/geometry change or an unchanged raw packet.
Missing old intermediate BReps are not reconstructed and accepted as an old oracle.

The saved Assembly also passes exact native save/reopen (174.98 s). All four
current release GUI fixture copies pass another authored calculation and an empty
complete current-contract comparison; their retained original final BReps have
valid empty Boolean differences in both directions. The oversized-Fillet current
copy likewise matches its old final BRep independently.

POKUS5's originally broken saved copy is not an unchanged-frame oracle: its stored
Wrap frame is approximately 62.069 degrees, while explicit repair resolves the
same definition to approximately 65.050 degrees. That repair was already present
in the successful three-regeneration GUI output. Reauthoring that repaired output
with the final serializer preserves its history, sides and frames. The separate
native downstream matrix starts with a valid exact cylinder placement and proves
that adding later material, repeated regeneration, native reopening and Undo/Redo
do not move it. Package regression uses the repaired copy; the original broken
input and its diagnostic differences remain retained for traceability.
