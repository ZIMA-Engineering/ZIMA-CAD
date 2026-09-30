# Assembly locked-offset diagnosis

Status: fixed after the user's explicit approval of this shared edit-geometry
change. Baseline source: `eb25c44d3510eb80c40b54856e766686850182fe`.

The Assembly profile reference test expects a cutter to remain at Z = 3 mm
when its existing positional reference is assigned again with offset locked.
The referenced leaf cap is at local Z = 5 mm and its parent occurrence adds
2 mm. The required distance is therefore `3 - (5 + 2) = -4 mm`.

Instrumenting the existing test established:

- Before locking: placement Z = 3, reference offset = -4.
- After committing the lock: placement Z = 3, reference offset = -4.
- Measurement with `build_scene().original_references`: -2.
- Measurement with `build_drawing_scene().original_references`: -4.
- Reassigning the locked reference with the current edit packet moves Z to 5.

Ordinary Assembly packets intentionally retain analytic surfaces in the leaf
frame while triangle samples include occurrence placement. The common placement
solver expects analytic surfaces in the same frame as its input point. The
Assembly branch of `workspace::placement_edit_geometry` currently passes the
ordinary packet, so distance capture uses a different frame from calculation.
The existing drawing-packet conversion already resolves the full occurrence
chain and is also used during Assembly construction resolution.

The fix uses that existing conversion in the Assembly edit-geometry branch.
Do not alter ordinary scene packets, mate conventions, placement equations,
side flags, signed offsets or persistence. This shared input also serves
Assembly Sketch/construction reference editing and placement-value operations,
so those consumers need regression checks, including rotated/repeated nested
occurrences, locking, Undo/Redo and native reopening.

The test checks the independently derived -4 mm distance before the existing
locked-reassignment assertion. The complete Assembly profile contract now
passes, including Extrusion/Revolution, exact occurrence paths, lock retention,
reference removal, Undo/Redo, native save/reopen and unchanged source Part data.

The reference geometry contract additionally uses two repeated nested sources
with two successive quarter-turns and different translations. It verifies the
independently calculated transformed face origins/normals, measured distances
-4, -0, +0 and +3, and agreement with placement resolution. It checks that the
authored offset sign, source geometry and document generation/Undo state remain
unchanged. Part profile reference and Assembly construction reference contracts
also pass. No user-visible strings or localization keys are added.

GUI surface-placement and all-five-language translation contracts pass. One
additional owned-profile external-reference GUI scenario remains unavailable:
it fails with `Calculated history boundary does not match its parameters`.
Rebuilding with the baseline `placement_edit.cpp` reproduces the identical
failure before this correction. That separate fixture/validation issue is not
changed here; this run does not establish coverage of its later GUI steps.

The local Windows GUI/CLI are rebuilt from the final commit for `zima-cad.bat`.
No native format, template, version identity or published archive is changed.
