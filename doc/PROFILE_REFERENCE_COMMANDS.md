# Part and Assembly profile placement references

`extrusion.reference.set` and `revolution.reference.set` assign original references
to existing Extrusion/Revolution features in the active Part and Assembly profile
cuts. They use `commit_profile` / `commit_assembly_profile`, shared with Properties
confirmation, preserving the owned Sketch and selected target components.

```json
{"command":"extrusion.reference.set","arguments":{"container":"EXTRUSION-ID","index":0,"reference":{"owner":"PART-ID:origin","key":"origin:plane:xy"},"offset_mm":7}}
{"command":"revolution.reference.set","arguments":{"container":"REVOLUTION-ID","index":0,"reference":{"owner":"ASSEMBLY-ID:origin","key":"origin:plane:xy"},"offset_mm":1}}
{"command":"extrusion.reference.set","arguments":{"container":"CUT-ID","index":0,"reference":{"owner":"ORIGINAL-FEATURE-ID","key":"ORIGINAL-FACE-KEY","instance_path":"EXACT-OCCURRENCE-PATH"},"offset_mm":-4,"derive_orientation":false}}
```

Arguments match the [shared entry](PLACEMENT_REFERENCE_COMMANDS.md): `index` 0–2
for position, 3 FRONT, and 4 TOP; `reference` contains `owner`, `key`, and optional
`instance_path`. Part uses local references with empty paths. Assembly uses empty
paths for owned Origins/constructions and full paths for exact inserted geometry,
including every subassembly level. Clients obtain paths and keys from original
references, not Part names or OCCT face numbers.

`offset_mm`, `flip`, `derive_orientation`, and `document` are optional. Results
match the corresponding `.get` plus `changed`. `.get` returns profile parameters,
owned Sketch identity, and `reference_valid`, without a separate `placement` field.
General `placement.reference.set`, with `object` instead of `container`, handles
Part profiles. Assembly profile cuts use the two specific commands above.

## Rules

Part accepts persisted original references preceding the feature in history,
Part Origin, and available Body Origins. The owned profile Sketch, feature itself,
and later objects are invalid sources. Existing protections cover inactive/derived
bodies. Assembly uses persisted original geometry in its own frame, excluding the
owned profile and container. Repeated source Parts have distinct full-path references.

Position fields and FRONT/TOP are independent. Default `derive_orientation:true`
adds orientation to a free orientation field under existing rules; replacing a
position reference does not remove other orientations. Parallel planes assigned
as FRONT and TOP conflict and are rejected. `derive_orientation:false` keeps
position/orientation assignment separate. Replacing a locked reference preserves
measured distance under shared assignment rules.

Part and Assembly share reference preparation: original-identity lookup, field
separation, direction, lock, and existing assignment/placement solving. The user
explicitly approved this extraction. Solver algorithm and GUI contract are unchanged;
profiles retain existing FRONT and Sketch-coordinate normalization.

Commit calculates bodies and stores profile plus Sketch in one transaction. Assembly
calculates only its own cut without editing or recalculating source Parts. Identical
assignment neither calculates nor adds Undo. Invalid requests leave no partial change.
An active GUI editor blocks command mutation until OK/Cancel. `.prtz`/`.asmz` formats
and start templates are unchanged.

## Verification

Part tests measure a 2 × 3 mm rectangle extruded 5 mm (30 mm³) and a full revolution
of a rectangle between radii 2 and 4 mm with height 3 mm (36π mm³). Offset changes
move actual geometry while preserving volume, identity, and owned Sketch curves.

Assembly tests subtract from a 10 × 10 × 10 mm box. A 2 × 3 mm profile extruded
4 mm removes 24 mm³; moving it to the edge leaves 12 mm³ intersection. Revolution
between radii 1 and 2 mm with height 2 mm removes 6π mm³. Tests include the same
Part in two occurrences of one subassembly: changing the full path moves the cut
from Z = 1 to Z = 3 mm, preserving untargeted geometry. Source Part revision and
calculated bodies remain unchanged.

Coverage includes locks, no-ops, conflicting/self references, incomplete paths,
duplicate fields, wrong types, large indexes, active editing, Undo/Redo, and native
saving. Actual CLI opens an Assembly, assigns references, performs Undo/Redo, and
saves; the test reloads `.asmz` and measures volume. GUI opens both profile Properties
dialogs and checks offset, Cancel, OK, Undo/Redo, and reentry into the owned Sketch.

Both applications and all targets built. Full Windows Release regression passed
**150/150 in 682.52 s**, including actual CLI, translations, GUI console, and full
GUI run: `build/assembly-profile-reference-full-tests.log`. Separate Part/Assembly
profile GUI checks also passed (`build/assembly-profile-reference-gui-tests.log`).
The new GUI test finishes tree iteration before opening Properties because rollback
rebuilds the tree; retaining a live iterator had caused a test-fixture error.
