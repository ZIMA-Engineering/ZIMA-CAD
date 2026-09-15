# Inline dimension values and shared commands

Confirming a value directly in the View uses the same model operations as
Properties and CLI. Command and JSON field names remain English.

| View edit | Command | Shared operation |
| --- | --- | --- |
| Catalog thread size of an opening | `opening.set`, `designation` | `select_opening_thread_size` and `commit_opening` |
| Component mate offset | `component.set`, `placement_references` | `prepare_component_edit` and `commit_component_properties` |
| Point radius of a standalone Part/Assembly 3D curve | `construction.set`, `radius_mm` | `commit_construction` |
| Point radius of an embedded Sweep 3D path | `sweep3d.set`, `path.points` | `commit_sweep` |

Edit embedded path points through the owning Sweep, not a standalone construction
command. The point ID and other point IDs remain. The curve is a polyline with
fillets enabled; excessive radius is rejected without mutation. Lengths are mm;
angular offsets are degrees.

Catalog selection preserves custom profile diameter and shared opening-length
rules, including thread runout. Component mates retain limits, value locks,
original references, and immediate-Assembly ownership. Identical values add no
Undo step. Standalone constructions do not recalculate bodies; opening or embedded
path edits explicitly calculate the corresponding operation.

If Properties is open, inline editing still changes its pending proposal. Its
OK/Cancel controls commit. This stage introduces no new drag-based value manipulators.

## Assembly display repair

A new regression found that standalone Assembly 3D curves stored radius but curve
inspection omitted its dimension from the View. Assembly now uses the same
`curve3d_radius_dimensions` as Part. The dimension is derived from persisted curve
data without OCCT and added only for the active document. When displayed inside
a parent Assembly, it follows normal occurrence-scene transformation.

## Verification

`console_ui_verification.cpp` confirms the actual numerical editor and catalog
dropdown. It compares full saved `.prtz` / `.asmz` definitions after GUI and equivalent
CLI edits, checking Undo, identical values, invalid radius, offset limits, and locks.
Two instances of the same subassembly verify that the dimension is offered exactly
once on the active occurrence and editing commits the correct source document.
Existing workspace-window tests also cover editing with Properties open, Cancel,
and dimension redisplay.

Formats and start templates are unchanged. The catalog remains at 296 commands
at this stage. Current suite results: [CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).

Detailed arguments: [OPENING_COMMANDS.md](OPENING_COMMANDS.md),
[COMPONENT_PROPERTY_COMMANDS.md](COMPONENT_PROPERTY_COMMANDS.md),
[CONSTRUCTION_COMMANDS.md](CONSTRUCTION_COMMANDS.md),
[SWEEP_COMMANDS.md](SWEEP_COMMANDS.md).
