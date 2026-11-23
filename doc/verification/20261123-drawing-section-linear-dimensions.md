# Linear dimensions on Section boundaries

## Reproduction and correction

In the user's `ZE-CAD/50.drwz`, Section A-A shows a 20 mm middle web between
40 mm bosses. Mouse selection of the two web boundaries failed before the
change: neither click supplied a dimension reference. The existing 40 mm
edge-on circular supports were already selectable.

Section cap boundaries were anonymous triangle intersections. The Section
calculation now retains transient source-face provenance alongside its unchanged
boundary geometry. Drawing projection derives a straight measurement support
only when the complete source face is planar and its Section intersection is
collinear. Identity uses the source face, exact occurrence path, Section ID and
persisted Section Sketch segment IDs, never coordinates or triangle indices.
Bent-section closing bars and ambiguous overlapping sources remain anonymous.

The Drawing uses the existing Line attachment, measurement geometry and native
serialization. No schema, version or required persisted field changes. No
factory conversion is required. The original 3D cap/rim geometry still has no
placement reference; the shared placement solver is unchanged. Only the local
Drawing projection receives the derived measurement references.

Picking retains the actual clipped boundary fragments, including holes. It
does not invent selectable vertices or a midpoint inside an empty interval.
Whole-line visibility is checked at the cursor hit instead of the persisted
dimension witness midpoint. For coincident projections of the two halves of
an edge-on circle, the foreground half determines visibility.

Existing saved views need an explicit Drawing view refresh to acquire the new
derived supports. Opening, hovering and dimension selection do not regenerate
the source Body. Source geometry is neither approximated further nor recalculated
to create these dimensions.

## Windows verification

The native Section measurement test checks:

- Exact 20 mm, both selection orders, repeated placement and three layout drags.
- Rotated cameras, both retained sides, reversed tessellation order and stable
  source identities.
- Successive source thickness changes 20 -> 24 -> 16 -> 20 mm, independently
  checked against the expected values.
- A lost/restored cut, native save/reopen, holes with unpickable empty intervals,
  distinct bent-section legs, repeated occurrences and occurrence exclusions.
- Rejection of nonplanar faces and unchanged non-reference 3D cap behavior.

The native picker test checks a visible line end whose midpoint is occluded,
foreground/rear circular halves, complete occlusion, Hidden Edges mode and
equivalent prepared/unprepared candidate results.

The Qt mouse harness checks the synthetic Section and the actual `50.drwz`
20 mm web, in both pick orders, including Cancel, repeated entry, one committed
dimension, Undo/Redo and native save/reopen. It asserts zero Body calculations
and unchanged shared measurement geometry during the interaction. The user
document is read only; temporary files receive all test saves. The actual-file
test refreshes its Section from the already embedded source viewer packet.
That original check did not exercise Part regeneration followed by the Drawing
Regenerate action. The subsequent [Body Origin reference regression](20261123-section-body-origin-regeneration.md)
documents that separate failure and its end-to-end verification.

Eight CTest targets passed: `section_contract_tests`, `section_measurement_tests`,
`measurement_picker_tests`, `measurement_dimension_contract_tests`,
`drawing_dimension_command_tests`, `measurement_dimension_ui_contract`,
`drawing_parallel_ui_contract` and `translations_contract` (all with the
`zima_cpp_` prefix). Existing Section area, holes, hatching and persistence
checks passed alongside the new provenance handling.

Localization review found no new or changed user-visible strings. Source
coverage and all five language catalogs passed validation. The shared C++
implementation and OCCT 8.0.1 pins are unchanged; Linux was not built or tested
on this Windows host. General curved-surface intersection dimensions are not
added by this planar-support change. Broader Sketcher behavior is unchanged.

Disposable local evidence: `build/drawing-section-line-before.log`,
`build/drawing-section-line-final.log`, `build/drawing-parallel-after.png` and
the CTest logs in `build/cpp-windows-release/Testing/Temporary`.

## Local deployment

The Drawing fix is commit `d5f4f3da6`. Deployment branch
`codex/drawing-section-linear` applies it as `ef0991c95` on the previously
verified local baseline `b29d6d60d`. This keeps the unfinished CAM integration
out of the installed executable while retaining its work on the development
branch. All eight tests passed again on this deployment source.

The final Windows executable has SHA-256
`7D8D7067765D04A4D8E96C0A4B4BF369D0357F6C5BB390133ED500C465874EB7`.
Candidate and installed startup tests both passed factory-template creation,
Box calculation, native save and rendered View. The candidate also retained
the user-model Fillet preview exactly: 16 strokes, 10 source-node labels,
zero geometric difference, and successful Cancel restoration.

Both 20 mm and 40 mm real-file mouse workflows passed on the deployment build,
including a repeat after compilation finished. One earlier 40 mm harness
invocation ended with Windows status `0xC0000008` and no output. It did not
recur in the two subsequent 40 mm runs; the inspected recent Application log
contained no matching crash event. Its cause remains unconfirmed. This is not
reported as a repaired application defect or silently counted as a passing run.

The verified EXE was installed at `build/cpp-windows-release/zima-cad-cpp.exe`
after confirming that ZIMA-CAD was closed and preserving a hash-verified copy
of the preceding EXE. `zima-cad.bat` remains unchanged. The original `50.drwz`
and all three pre-existing dirty user configuration files retained their hashes.
No portable release was produced. Deployment logs and the previous executable
are local disposable artifacts under `build/drawing-section-20261123`.
