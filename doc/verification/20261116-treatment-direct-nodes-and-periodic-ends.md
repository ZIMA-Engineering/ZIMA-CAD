# Direct treatment node editing and periodic terminal cleanup

## Changes

Double-clicking a numbered editable Fillet or Chamfer node in ordinary
inspection now opens the shared solution menu directly. It does not construct
or open Properties. Choosing a different solution commits through the existing
atomic edge-treatment calculation path; Undo/Redo remains available. Dismissal
and selecting the current solution perform no calculation or transaction.
The menu checks document runtime identity, generation and active occurrence
again after its nested event loop. Endpoint choices follow persisted vertex
identity, including reversed route traversal.

The user's R1 edge between Zaobleni 04 and 01 produced a valid Boolean Body but
failed during redundant-seam cleanup with `Courbes non jointives`. The normal
cleanup remains unchanged. If its edge concatenation raises an OCCT exception,
the implementation validates the untouched Boolean result and retries only
face unification, retaining the exact boundary edges and protected ancestry.
The resulting cleanup and history still pass the existing validity checks.
This is not a switch from the ZIMA method to the OCCT Fillet algorithm.

The larger-radius comparison also found two analytic-preview defects. A fully
materialized rollback mesh could let triangulated cylinder chords compete with
exact cylinder intersections; those cylinders now use only their analytic
intersections. Tangent-ray discriminant rejection now converts the existing
radial tolerance into discriminant units instead of squaring that tolerance
alone. This prevents tiny fitted-axis roundoff from discarding an entire
otherwise valid terminal trim. Neither change builds a preview Body.

## Verification

Windows x64 Release, Qt 6.11.0 and OCCT 8.0.1. The original user document was
only read. Disposable snapshots and logs are under
`build/test-models/nonjoint-fillet-20261116` and
`build/test-models/node-alternatives-20261116`.

- Six direct-node GUI fixtures cover additive and subtractive Fillet/Chamfer
  and both variants of the user's five-route pocket. Each exercises cold
  inspection, actual Tree and node double-click events, numbered nodes,
  spike/rounding, No cut/Cut, popup dismissal, current-choice no-op, repeated
  changes, Undo/Redo, native reopening, camera and Tree selection. The popup
  creates no Properties dialog, even a hidden one.
- The reported R1 edge was reproduced on the saved model before the fix.
  The corrected strict-ZIMA command passes create, save, Undo, Redo, complete
  regeneration and save on the latest snapshot through Zaobleni 08. The GUI
  case independently checks analytic draft without Body calculation, OK,
  retained ZIMA selection, native reopening, Undo/Redo and unchanged OK.
- Comparing the prepared rollback wire with the native result passes R1,
  R3, R4, R5, R6 and R8. Observed wire calculation was 0.62-0.78 ms, with zero
  Body calculations and maximum boundary deviation about 4.4e-8 mm against a
  1e-5 mm gate. At R8, a wire segment merged into a support face must classify
  independently as ON that face, never inside or outside the Body. These are
  observations on this model and host, not universal timing promises.
- A native two-radius fixture checks R1/R3/R5 in both edge orientations,
  validated shapes, nonempty generated Fillet faces/history and equivalent
  volume and area. Analytic wire regressions tighten cylinder accuracy and
  exercise translated tangent contacts at R1/R3/R5/R8.
- The latest saved model's existing Zaobleni 01 and 04 also pass the same
  R1/R3/R4/R5/R6/R8 wire/native comparison, including their unchanged stored
  radii. Maximum deviation is about 6.4e-8 mm. The test explicitly builds an
  oracle for an unchanged compact output instead of mistaking its intentionally
  omitted B-Rep archive for a failed modeling operation.
- All eight focused CTest contracts pass: native planar Fillet, analytic
  treatment wires, treatment commands, translations, Properties editing,
  creation, symmetric endpoint editing and ordinary treatment selection.
  An earlier concurrent build/test run collided with Qt DLL deployment;
  serializing runtime deployment before execution resolved that harness issue.

## Scope and limits

The implementation remains shared C++. Native document format, side identity,
dependency versions, container placement and calculation-method policy are
unchanged. The UI reuses existing translated keys in cs/en/de/fr/ru; there are
no new user-visible strings.

A cold compact history may still need one explicit calculation to prepare its
real shaded rollback input when Properties is opened. That established input
materialization is separate from analytic draft interaction. Unchanged OK
itself does not calculate or create an Undo transaction. Direct node menu
opening does not materialize that rollback scene.

Linux execution and nested Assembly occurrence interaction were not exercised
on this Windows host. The delivered binary is the local development build
launched by the unchanged repository-root `zima-cad.bat`, not a new portable
release archive.
