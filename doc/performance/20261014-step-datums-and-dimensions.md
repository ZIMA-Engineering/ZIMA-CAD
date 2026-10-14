# STEP datums, placement dimensions and failed-profile inspection

## Scope and user decisions

The user reported misplaced placement dimensions, slow confirmation of an
unfinished Sketch or a Feature without a profile, and a crash when reopening
Properties for the saved empty Feature. Point, Axis, Plane and 3D Curve commands
were included in the investigation.

The user initially authorized hiding free X/Y/Z placement dimensions, paused
the work, and then explicitly withdrew that decision. All placement dimensions
remain enabled. No change to their visibility was implemented.

The representative document is a read-only snapshot of the user's save at
2026-10-14 05:04:20 Europe/Prague: `build/02-user-20261014-050420.prtz`,
499,764,705 bytes. Its SHA-256 and that of `Projects/02.prtz` are both
`CF32B00BB8ACD2EA9E2CED1AB0FC0389DA074751BEF3976BB149E025C99F47F7`.
GUI tests create and save their own copies under `Projects/test`.

## Implemented changes

1. Active Properties placement dimensions were Body-local, but the scene
   skipped their Body placement while `parameter_dimension_preview_` existed.
   Apply the same owning/editing Body frame used by the pending work-plane
   carrier. This changes presentation coordinates only; reference solving,
   dimensional values, side choices and native definitions are unchanged.
2. `authored_feature_wire` could throw while inspecting a persisted failed
   Extrusion with no closed profile. Its caller runs during scene refresh,
   including the first synchronous Properties preview. Catch an unavailable
   analytical wire and use the established persisted-contribution fallback.
   Failed definitions remain stored, inspectable and repairable.
3. Do not install an intermediate placement-only callback for the generic
   Feature before installing its complete profile callback.
4. An already known native `input_error` must reach recovering evaluation
   before OCCT rebuilds preceding material. Recovery still locates the first
   failing history boundary and retains its error and input.
5. A cold, unchanged STEP prefix followed only by reference-only Feature groups
   can reuse its exact persisted material packet. The suffix uses the existing
   datum/profile builders. No live material ancestry is fabricated: a later
   material operation still rebuilds the real source history. Reference-only
   operations also retain the unchanged material packet on warm evaluations,
   rather than remeshing and reintegrating its physical properties.

The cold reuse is deliberately restricted to independent import/reference-only
histories without requested live source faces, solids, external dependency keys
or edge ancestry. It does not generalize persisted B-Rep reuse to Booleans,
FORM operations or treatments. Fingerprints, precision and signed zeros are
unchanged. Native serialization and factory assets are unchanged.

## Verification

`zima_cpp_profile_command_tests --imported-datums <native>` exercises cold Point,
Axis and Plane Features and empty, open and closed Sketch Features. It compares
the exact imported material mesh, B-Rep, physical properties and native topology;
tests unchanged OK, Undo/Redo and native save/reopen; and compares a subsequent
material operation with a fresh kernel result. Independent Point, Axis, Plane
and 3D Curve creation preserves the complete calculated packet and unchanged
confirmation preserves the document revision.

The small translated/rotated import passes the matrix. Observed confirmations
were 1.2-4.9 ms for the Feature variants and 0.4-0.7 ms for independent
constructions (`build/datum-cold-small-final.out.log`). These are finite,
machine-specific observations, not general speed guarantees.

The large-file probe limits material Feature creation to Point and empty Sketch;
the full six-variant save/reopen and following material-operation oracle uses
the small fixture. On the user snapshot, the Point and Sketch transactions were
2779.33 and 3114.24 ms. Independent Point, Axis, Plane and 3D Curve were
1130.36, 1064.71, 1141.60 and 1462.70 ms. Exact packet checks and unchanged
confirmation passed. Logs: `build/datum-cold-02.out.log` and `.err.log`.
The source STEP prefix is reused; only the reference-only suffix is evaluated.

The user-file GUI test preserves placement dimensions and compares their
witnesses and pre-layout annotation lines against an independent Rz*Ry*Rx Body
transform. It also checks the actual clicked face point, the work-plane equation,
successive signed profile-offset changes and successive length changes.
Annotation lines may subsequently be arranged around model bounds by the
existing viewer layout; their authored source and displayed witnesses retain
the correct Body frame. On this snapshot the Body translation is
(-1500, 0, -1000), the first local position is approximately
(1709.319, -619.363, 965.177), and the scene handle is
(209.319, -619.363, -34.823). The purple 50 mm handle remains enabled.

The same GUI test confirmed an empty material Feature, saved and closed the
copy, reopened it, and opened its Properties without an exception. Confirmation
including GUI processing was 5299.18 ms; saving was 62865.5 ms. Saving remains
a separate expensive operation; no save optimization is claimed. The retained
failed definition reports its native missing-profile error. Log:
`build/02-datum-feature-fixed.out.log` and `.err.log`.

The initial seven native regressions passed: import, STEP, translations,
metadata, physical Body properties, Assembly profiles and Part profiles.
The GUI import, Body preview, translated/rotated Feature frame and reference
geometry transformation checks passed. The expanded GUI test enters two and
three well-conditioned independent planar references and repeatedly changes
their offsets; its small-fixture save/reopen Properties check passes.

An additional construction regression initially used a stale executable
requesting the removed `START_PART.prtz` template. Rebuilding exposed a missing
test-only include for `ComponentReferenceKindIndex`. The harness now includes
its actual header and viewer header search path; no product implementation was
added to repair that test.
The cylindrical picking fixture explicitly removes only display-side analytic
metadata from its disposable scene; the original-reference packet remains
intact. This exercises the intended lookup case even when current display
packets also carry surface metadata.

After rebuilding the affected executables, seven focused regressions pass:
translated/rotated multi-reference GUI, saved empty-feature Properties GUI,
reference transforms, construction commands, Assembly profiles, the new cold
imported-datum matrix and Part profiles (`build/datum-complete-regressions.log`,
10.85 seconds). The GUI fixture is registered as the source fixture for the
two new automatic regression tests.

## Near-parallel shared-panel diagnostic outside this repair

Do not treat the well-conditioned reference matrix as exhaustive. A diagnostic
that selects two nearly parallel STEP planes exposed a separate mismatch in
the protected shared placement panel. The solver calculates a closest position
whose free coordinates can change. `ContainerPlacementSection` updates only
the coordinates classified as constrained, leaving editable fields stale.
In the recorded case the dimension's resolved Y was approximately
-265,122,000 mm, while the pending free Y was still -619.363 mm. The panel also
has a nominal +/-1,000,000 mm display range and preserves picked precision
separately; its visible clipping must be considered in a repair.

The failing diagnostic is recorded in
`build/02-multiple-reference-fixed.err.log`. The test-only well-conditioned
filter selects suitable independent planes for the ordinary regression; it
does not change or reject production references. No solver tolerance was
relaxed and no shared-panel repair has yet been implemented.

The user declined the proposed synchronization of all XYZ fields and later
reported correct ordinary dimension display in the actual workflow. That broader
proposal was premature: free numeric inputs, picked precision, field range and
resolved preview coordinates require separate interpretation and verification.
No approval is pending and this work does not change the protected shared panel.
The unfiltered near-parallel diagnostic remains recorded, without claiming that
ordinary Part placement is defective or that a global XYZ rewrite is its remedy.

Windows Release, Qt 6.11.0 and pinned OCCT 8.0.0 were used. Linux verification
is unavailable here. No user-visible text was added or changed; the five-language
translation contract passes. User configuration and custom `ZE-TEXT.symz`
retain their pre-investigation hashes. The root development launcher is unchanged.
