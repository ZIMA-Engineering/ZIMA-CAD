# Standalone Sketch properties in Assembly

## Data model and shared commands

Every standalone Assembly Sketch belongs to a Sketch HistoryContainer.
The Assembly stores it in `sketch_containers`; the Sketch uses
`owner_container_id`. The container owns Placement, Origin, stable identity,
and locks. A Sketch inside a cut belongs directly to that cut. Native validation
rejects orphaned or duplicate ownership. Loading legacy data does not create
missing containers.

`sketch.create`, `sketch.set`, and `sketch.reference.set` use the same commit
operation as GUI Sketch Properties. Name, XY/XZ/YZ plane, offset, placement,
FRONT/BACK, rotation, and references are stored in one transaction.
`placement.get/set/reference.set/reference.remove` and `value_lock.list/set`
support the owning container. `sketch.get` returns Sketch and container IDs;
`tree` returns the hierarchy including Origin.

A reference object uses `{"owner":"…","key":"…","instance_path":"…"}`.
The path distinguishes the exact occurrence of a repeated Part or nested Assembly.
The first plane reference defines the Sketch's working FRONT. Replacing it also
moves its automatic orientation row to FRONT; the new face must not remain both
FRONT and TOP. Without a position plane, explicit orientation references remain independent.

Standalone Sketch changes use persisted ZIMA geometry and local frame solving.
They do not calculate OCCT bodies, move components, solve their mates, or regenerate
existing cuts. `sketch.set` returns `body_calculated=false`. Editing a cut-owned
Sketch commits the entire profile cut and can calculate bodies. Tab switching
never triggers this calculation.

## Conversion to a cut

By explicit user rule, Assembly Extrusion and Revolution always subtract material.
This applies to creation, editing, and standalone Sketch conversion through GUI
and CLI. The shared transaction rejects CombineMode::Add before changing the
Assembly. Only an immediate editable Part occurrence may be a target.

Conversion preserves container ID, Origin, placement, references, and Sketch
identity. It changes the feature kind and atomically replaces the entry in
`sketch_containers` with an entry in `cuts`. Undo restores the original standalone
Sketch and bodies. Cancel leaves the original container unchanged.

Conversion transfers the work-plane offset lock from standalone Sketch parameters
to profile-feature parameters. The lock has one persisted owner. Internal Sketch
Properties read and commit it through the shared adapter; choosing another
command cannot bypass the locked value.

## Deletion and dependencies

`sketch.delete` accepts a standalone Sketch ID and removes its container too.
GUI uses the same operation. The command rejects a cut-owned Sketch; remove its
owning feature instead. Undo/Redo restores/reapplies the operation.

Sketch placement references participate in component and construction dependency
checks. Cycles are rejected before commit. Deleting a Sketch leaves dependent
constructions with their last usable frame and a missing-reference state.

The tree shows a standalone container with its Origin and Sketch. Ordinary View
selection offers its profile; Sketcher helpers appear only during active editing.
Preview in an active subassembly uses the exact occurrence path and preserves
the full parent Assembly as passive context.

## Native files

At this stage, `.asmz` uses INI version 18 and internal JSON version 27. Required
`sketch_containers` entries contain ID, parent identity, name, suppression,
Placement, and locks. `config/templates/start_assembly.asmz` was updated while
preserving other template metadata. Extensions are unchanged. This change does
not alter the Part format or start Part structure.

All required data remains in native `.prtz/.asmz/.drwz` files. No required
auxiliary files, revision storage, or cache directories are introduced.

## Verification

`assembly_sketch_properties_tests` checks placement in translated and rotated
frames, two occurrences of the same Part, first-plane replacement, locks, invalid
and cyclic sources, dependencies, native ownership, Undo/Redo, and deletion.
It also checks unchanged component positions and shared calculated geometry.

An independent volume check uses a 10 × 10 × 10 mm box: an R1 circular profile
extruded 2 mm leaves 1000 − 2π mm³; revolving a rectangle between radii 1 and 2 mm
with height 2 mm leaves 1000 − 6π mm³. The other occurrence retains 1000 mm³.
Both profiles must preserve the original container, and rejected addition must
leave the Assembly unchanged.

The GUI console runs the same Properties scenario for Part and Assembly:
Cancel/OK, name, plane, offset, FRONT/BACK, rotation, references, and independent
FRONT. It compares complete reopened definitions rather than selected numerical
values. The existing GUI scenario converts an Assembly Sketch to a cut.

Component-deletion dependency checks include external Sketch references directly
in an Assembly without a dependent Part context. References in another Assembly
do not block unrelated occurrences.

The GUI regression also activates a rotated subassembly, opens its Sketch,
selects and drags a specific point, and verifies the local plane and saved
coordinates. Passive parent Assembly geometry remains visible during dragging.
The display model regression checks two occurrences of the same subassembly and
unchanged shared data for the inactive occurrence.

The catalog contains 292 commands and CTest registers 159 tests. Both applications
and all test programs built. The full regression passed **159/159 in 647.73 s**,
including the separate CLI process, full GUI, new Assembly Sketches, properties,
reference dependencies, native saving, drawings, model operations, and exact-spline
tests. Logs: `build/assembly-sketch-release-verified-build.log` and
`build/assembly-sketch-release-verified-tests.log`.
New errors have translations in all five language files; Sketch Properties also
uses translated messages when rejecting changes.

After the final localization addition, another build completed and translations
plus the complete GUI/CLI console scenario passed again: **2/2 in 122.21 s**.
Logs: `build/assembly-sketch-localized-build.log` and
`build/assembly-sketch-localized-tests.log`.
