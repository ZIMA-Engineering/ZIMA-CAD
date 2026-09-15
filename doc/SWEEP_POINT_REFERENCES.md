# Point references in embedded Sweep3D paths

`construction.reference.set` also accepts stable IDs of points in Sweep3D-owned
paths. The point remains inside its Sweep. Position and orientation rows use the
same proposal preparation as standalone constructions and shared Properties reference
input. Successful changes use `commit_sweep`: one calculation, revision, and Undo/Redo step.

`construction.get` returns `owning_feature`, `parent_construction`, and
`coordinate_owner`. Point coordinates are local to the path; Sweep and body frames
are applied in sequence. The helper frame is in memory only. Reference preparation
uses native data without OCCT; body calculation occurs only on explicit commit.

## Usage

~~~json
{"command":"construction.reference.set","arguments":{"construction":"POINT-ID","index":0,"reference":{"owner":"SOURCE-ID","key":"origin:plane:yz"},"offset_mm":3,"derive_orientation":false}}
{"command":"construction.get","arguments":{"construction":"POINT-ID"}}
{"command":"placement.reference.remove","arguments":{"object":"POINT-ID","index":0}}
~~~

Position indexes are 0–2, FRONT 3, and TOP 4. The command preserves distance locks,
orientation pairing, and duplicate checks. Actual changes return `body_calculated: true`;
no-ops return false and add no history.

Allowed sources are local original geometry before the Sweep in history, the path
frame, and points/axes/planes of earlier points in the same path. Self-reference,
later points, result path edges, and the feature's own result body are rejected.
Other-container sources use the same history boundary as other feature references.
Invalid resulting shape, inactive/derived body, or concurrent open editing leaves
the document unchanged.

Edit full Sweep placement through the container ID. Edit point values, radii,
tangents, and order through `sweep3d.set` / `path.points` with the complete ID list.
`construction.set` does not turn embedded paths into standalone objects.
`placement.get/set/reference.set` addresses root Sweep placement and standalone
constructions; embedded-point references use `construction.reference.set` above.

## Verification

The baseline test reproduced rejection of owned points (`construction_not_found`).
New regression also exposed missing frames of other points during numerical-batch
validation. Standalone-curve and embedded-path adapters now supply complete native
frames, preventing `path.points` edits from bypassing a referenced coordinate constraint.

Model scenarios measure circular Sweep volumes with radius 2 mm:
4π√425, 4π√409, and 4π√1713 mm³. They check source transformation into a rotated
Sweep frame, also inside a translated/rotated body, point chains, orientation
references, locked distance, invalid sources, and collapsed paths. They additionally
cover atomic errors, no-ops, Undo/Redo, and native saving.

The actual CLI process assigns a reference, performs Undo/Redo, and loads the saved
body. GUI edits an offset in point Properties: child OK leaves a proposal in the
parent, parent Cancel discards it, and parent OK commits. Equivalent GUI/CLI edits
must save identical complete Sweep definitions.

Broader regression passed **12/12 in 185.54 s**, including actual CLI, GUI console,
references in all domains using shared preparation, reference removal, and Sweep
commands. Both applications and all test targets built. Logs:
`build/sweep-point-all-build.log`, `build/sweep-point-regression-tests.log`.
This was not a new full-suite run. After adding lock verification through actual
`value_lock.set`, the focused test passed again **1/1 in 1.03 s**
(`build/sweep-point-final-test.log`). The catalog has 289 commands and CTest 157 tests
at this stage. File formats and start templates are unchanged; all required data
remains in native documents.
