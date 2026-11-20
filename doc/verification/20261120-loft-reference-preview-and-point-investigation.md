# Loft reference preview and intermittent point placement

## Scope and outcome

The owned Sketch Properties preview inside Loft previously sent the raw
`general_surface_definition_mesh` edges to the cyan transient renderer. That
packet includes the Sketch's read-only external references. Consequently two
projected reference lines in the user's circle Sketch appeared as solid cyan
profile edges outside Sketcher.

The Properties callback now reuses `loft_definition_mesh`, the same authored
profile preview used by the parent Loft dialog. It excludes auxiliary references
and retains material profiles, Thin preview contours, matching links and
parameter dimensions. The raw Sketch editor data is unchanged. There is no
placement-solver, persistence, geometry-calculation or snapping behavior change.

The intermittent point-placement report is **not resolved** by this change.
The user reported that it started working again without a deployed point fix.
Do not describe the preview correction as a fix for that separate symptom.

## Read-only user evidence

Copies are under `build/sketch-point31-20261120`; originals were not modified.

| Copy | SHA-256 | Observation |
| --- | --- | --- |
| `original31.prtz` | `25959898F48C4AA79E7620643DF05398CA7AE64B2E4ABF5E90665EF9360D229C` | An ordinary Sketch has a free point 1.2836363343693904 mm outside its circle, without an incidence constraint. The owned Loft circle has no rim points in this copy. |
| `recurrent31.prtz` | `F6F284C6E499A0EC08B26E5CE22EB14FC9951EC80439E95FE2FAE53EBD788F40` | Four owned circle points have C to both the circle and the projected line; their radial equations are satisfied. |
| `recurrent31-latest.prtz` | `19C9A5AB82DB52E8A5F22D9C7349629C8EA6E25AC3D29A33BD5F2704D320C438` | The same valid four circle points, plus a third slot profile. |

The last screenshot with two off-circle points and only H/V constraints is not
represented by either later saved copy. The save result alone therefore does
not reproduce that transient editing state.

## Verification matrix

| Check | Result / evidence |
| --- | --- |
| Owned Properties preview using the user's two-profile fixture | PASS: preview edge count matches the parent Loft generator; no raw external-reference edge survives; screenshot inspected. `gui-user-rmb/loft/point-circle-owned-preview.png`. |
| Final deployed executable, latest three-profile fixture | PASS: preview assertion plus 32 native circle point placements over eight Sketch entries, normal and rotated views, cursor orbit, return to normal and close zoom. Every created point retains circle incidence; maximum radial residual including existing points is 2.14e-14 mm. `gui-final/results-loft.json`, 27.45 s. |
| Initial independent two-circle fixture | PASS: 16 placements over four Sketch entries, including cursor orbit and return to normal. `gui/results-loft.json`, 14.32 s. |
| Full Loft GUI contract | PASS: five UI languages, profile placement/edit/add/remove, reference and whole-Origin entry, preview, inspection, resizing, rollback, unchanged OK, Thin sides, native reopen and Undo/Redo. `gui-full/results-loft.json`, 30.57 s. |
| Native Loft geometry and document contracts | PASS: `zima_cpp_loft_tests`, `zima_cpp_loft_document_tests`. Includes solid, surface and Thin results. |
| Localization | PASS: `zima_cpp_translations_contract`; full Loft GUI checks all five supported languages. No user-visible text was added or changed. |
| Native Sketch dimension, relation and curve command contracts | PASS after rebuilding their stale executables: `zima_cpp_sketch_dimension_command_tests`, `zima_cpp_sketch_relation_command_tests`, `zima_cpp_sketch_curve_command_tests`. The first run used old binaries and rejected current-format start templates; no factory files were changed to make the tests pass. |
| Final executable, coordinate-incidence GUI matrix | PASS for native line, circle and arc: three successive driving-coordinate value edits, three support-point drags, independent coordinate/incidence equations after each action, retained point identity, Undo/Redo, native save/reopen. `gui-coordinate/results.json`, 75.19 s. |

The point probe is selected with `ZIMA_VERIFY_LOFT_ONLY=1` and
`ZIMA_VERIFY_POINT_CIRCLE_ONLY=1` during `--verify-startup`.
`ZIMA_POINT_CIRCLE_FIXTURE` optionally supplies a read-only native user fixture;
otherwise it creates independent native circles. All edits happen in the new
test document and pending Loft, which is cancelled at the end.

One early probe expected the circle even when the common picker had selected
an overlapping external line first. It produced a point on that line with a
1.71976 mm circle residual. This is evidence of the existing support-priority
contract, **not** evidence of a lost circle constraint or displaced cursor.
The final probe explicitly cycles to the circle with RMB when necessary and
checks the selected support before each LMB. No picking priorities were changed.

The probes cover a bounded set of mouse interactions. They do not establish
exhaustive incidence/dimension coverage. In particular, the reported failing
session, all automatic-inference settings, every reference/driving/lock state,
all selection orders, external circular supports, trimming, perspective and
Linux are not reproduced by this point probe. Existing command tests provide
additional dimension, edit, persistence and rejection checks; they are not a
substitute for reproducing the intermittent mouse symptom.

## Local deployment

After the preview and Loft checks passed, the normal repository Ninja target
rebuilt `build/cpp-windows-release/zima-cad-cpp.exe`. The stable root
`zima-cad.bat` continues to launch that file. This is a local patch to build
identity `2026112002`, not a published release or a format change.

Deployed GUI SHA-256:
`06B7857E7B495895190F8B97B7FBB2A30C6E9D663D77B3B981D547FA9A91672A`.
The preceding executable is preserved in
`build/sketch-point31-20261120/before-preview/zima-cad-cpp.exe`, SHA-256
`308CDBE332443390E3BF1B3FE6B908BAE89CD5806BC8F9A8F3AC04E93F863B66`.
The exact deployed executable passed the three-profile GUI probe above.
