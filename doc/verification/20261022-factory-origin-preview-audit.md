# Factory origin and preview audit, 2026-10-22

The user accepted the analytical creation-wire experiment on L30X30X3 and
requested factory FORM/library verification before integration and Windows
publication. This record distinguishes load checks from calculation, editing
and mouse checks. The side-reversal persistence proposal remains separate.

## Read-only native inventory

All 1,185 tracked native document paths load with the experimental Windows
native reader: 1,034 Parts, 137 Drawings, ten symbols, one frame, one title block
and two Assemblies. Symbol/frame/title-block aliases used by the CLI preserve
their exact bytes. The run took 76.454 seconds; every source SHA-256 remained
unchanged. This establishes reader acceptance, not regeneration certification.

Evidence: `build/feature-side-factory-inventory.json`,
`build/native-factory-current-report.json`,
`build/native-factory-current-load.log`.

A second run uses the accepted Git versions of the two locally modified native
assets (the M10 pressed wing nut and ZE-TEXT symbol), copied privately from HEAD.
It passes all 1,185 loads in 74.485 seconds. Their private user edits remain in
the working tree and are excluded from factory acceptance and publication.
Evidence: `build/native-factory-accepted-report.json` and
`build/native-factory-accepted-load.log`.

## Existing factory frames

All 1,034 accepted factory Parts pass reference resolution without changing the
frames of their 3,243 persisted Sketches, their owning Features or the containing
Bodies. Reference identities, view orientation and signed-zero placement offsets
are retained. Vector/rotation comparisons use an absolute tolerance of 1e-5;
identity, role and state comparisons are exact. This is a frame check, not a
claim that every factory geometry has been recalculated or edited interactively.
The additional complete native Sketch-data comparison also passes all 1,034
Parts and 3,243 Sketches. It checks points, native curves, constraints, dimensions,
external-reference payloads and all other serialized Sketch fields, with the
already checked resolved frames excluded from this second comparison. Structural
values and identities remain exact; numeric fields use the same 1e-5 tolerance.
Dimension/offset signed-zero values retain their signs. This is not an interactive
dimension-edit/drag matrix or a recalculation of every factory body.

Evidence: `build/factory-part-frame-body-audit.jsonl`,
`build/factory-frame-audit-summary.json`. All accepted native source hashes remain
unchanged after the audit. Complete Sketch-data evidence:
`build/factory-part-geometry-audit.jsonl`,
`build/factory-geometry-audit-summary.json`.

## Rollback axes

The first GUI audit rejected FORM Sweep editing because rollback omitted an
original solid Sweep axis. The stored axis was an exact reference edge rather
than a ViewerAxis record. The shared boundary presentation now exposes that
stored stroke once, with the same material-state exclusion as existing axes.
It does not modify the cached body, calculate geometry or create an Undo step.

The native transaction regression checks three repeated boundary reads, exact
endpoints/reference identity, no duplicates and unchanged revision/calculation/
Undo state. The corrected seven-test run passes: document-session transactions,
FORM Sweep Sketch GUI editing, work planes, and calculation/insertion checks for
all four factory FORM definitions, including the existing persistence, copy and
unbend cases where enabled. Total time: 66.20 seconds.

Evidence: `build/form-library-origin-preview-audit.log` (initial failure),
`build/form-library-audit-build.log`,
`build/form-library-rollback-verification.log` (seven passing checks).

## Exact command-preview comparison

The strengthened GUI oracle compares ordinary selected/inspected wire endpoints
against the actual Properties transient packet in scene coordinates. MeshView
already applies Body/occurrence placement when the transient packet is submitted;
the test must not apply it again. Each feature's owning Body is activated;
picking cycles through occluded candidates. Sketch-only features have no
operation preview and belong to the separate Sketch editing matrix. Flat and
Bend use their actual shared Sketch Properties dialogs for this comparison.

The initial six-file run passes DimpledHole, VentilationWindow and two library
reference Parts, but rejects BridgeLance and CornerGusset90. BridgeLance loses a
cross-Body point end target after Properties extracts its local Body carrier.
CornerGusset90 has the correct Sketch frame; its initial comparison incorrectly
applies the rotated Body frame a second time. The containing-Part correction
addresses the genuine BridgeLance reference-context failure. The Corner test is
corrected to compare the two scene packets directly. Neither finding authorizes
rewriting the authored factory models.

Undirected strokes are matched one to one, retaining exact segment counts and
the original 1e-7 endpoint tolerance. Exact lexicographic ordering of floating
coordinates is not a geometric equivalence test. Unmatched strokes still fail
with both packets and the pending parameter definition in the diagnostic output.

The correction passes the containing Part explicitly for native end-reference
lookup while keeping the pending Sketch and preview Body-local. This reuses the
same analytical generators without another Part copy, OCCT call or format
change. The native context comparison passes all four FORM definitions in
0.52 seconds (`build/factory-preview-context-native.log`).

The final 21-file GUI run passes all four FORM, ordinary screw/wing-nut/washer
samples and all fourteen first-TOP library review files (thirteen formed-nose
washer sizes and their reference Part). Every operation preview in the tested
Modeling Feature/Flat/Bend scope matches Properties, with ordinary View/Tree
selection, double-click inspection and unchanged Cancel/document state. Total
process time: 88.921 seconds. Every source hash remains unchanged.

The final fifteen-contract run passes in 169.58 seconds: synthetic Feature
selection, all four factory FORM preview checks, the formed-nose washer, native
title blocks, symbols, new-document templates, FORM Sweep Sketch editing, Sketch
entry/cap orientation, native work planes, native FORM preview context, and
Sheet/Solid state selection/inspection. Both private L30X30X3 snapshots also
pass the final scene-packet comparison. These checks use the experimental Windows
build, not a released archive.

Evidence: `build/library-preview-oracles/report.json`,
`build/BridgeLance-frame-diagnostic.log`,
`build/CornerGusset90-frame-diagnostic.log` (initial investigations),
`build/library-preview-oracles-final/report.json`,
`build/library-preview-oracles-final.log`,
`build/factory-origin-preview-final-contracts.log`,
`build/L30X30X3-selection-fixture-final-gui.log`,
`build/L30X30X3-current-selection-fixture-final-gui.log` (final passing runs).

All factory sources remain read-only. User projects and the user's privately
remodeled wing nut are not conversion targets. No new user-visible strings are
introduced by the rollback or reference-context fixes. Linux execution and the
remaining library editing/placement matrix are unverified.

## Integration boundary

The accepted preview/origin prerequisites are verified to the finite scope above.
The [Feature side-reversal scope](../FEATURE_SIDE_REVERSAL_PLAN.md) subsequently
received explicit approval on 2026-10-22. Its implementation and factory
regeneration are recorded in the
[direction verification](20261022-feature-side-reversal.md). This earlier
read-only audit itself did not change native definitions or publish a release;
the final factory and release gates must use the approved final implementation.
