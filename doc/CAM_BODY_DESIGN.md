# CAM Body design backlog

Status: agreed product direction, not implemented. Recorded from the user's
requirements during the November 2026 Render work. CAM is a later work package;
the Render release does not claim NC generation or machine-safe simulation.

## Document and setup

A CAM Body follows the existing special-purpose Body organization used by FEM.
Each setup references a stock Body and a desired result Body. Prefer selecting
another existing Body as stock rather than duplicating its authored definition.
One document can contain multiple setups and separate NC programs, for example
machining one side, turning the part over and machining the second side.

The calculated remaining material from a CAM setup can become the stock input
of a later CAM setup. Preserve that dependency explicitly and reject cycles.
Keep desired design geometry distinct from simulated remaining material.
Repositioning for another clamping must not change the source design Body.

Clamping locations are defined with Sketches on the initial stock. Fixtures,
including a vise, are explicit obstacle geometry. Collision checking must account
for those fixtures and the complete tool assembly, including its non-cutting
shank. A generated rapid transfer must not pass through the vise.

## Tools and machines

Initially provide internal, editable tool definitions with cutting conditions
and geometry; an external tool library can be added later. The user can create
new tools rather than being restricted to a fixed catalog.

For rotational tools, author a profile around the tool axis. The tool origin is
the boundary between cutting portion and non-cutting shank, as explicitly
confirmed by the user. The cutting profile extends to one side of that origin;
the shank profile revolves on the opposite side. This boundary expresses the
end of usable cutting length. Machine tool-tip coordinates and length offsets
must be derived explicitly rather than silently redefining this authored origin.

Each setup selects its operation and machine: milling or turning, with a stated
3-, 4- or 5-axis capability. Define supported combinations and their postprocessor
contracts before implementation. A tool shape alone does not establish a safe
machine envelope or valid controller dialect.

## Paths, NC code and simulation

Generate toolpaths from the selected tool, operation, stock and target geometry.
Also allow manual path construction as ordered blocks: one cutting loop, a
rapid transfer, another loop, and subsequent operations. Preserve the difference
between cutting and non-cutting moves in the editable path model.

An NC pane beside the model View shows the actual generated program. Selecting
or stepping through its lines moves the displayed tool through the corresponding
path. Simulation includes material removal and collision inspection, and produces
remaining stock that a subsequent setup can reference.

## Implementation gates to define next

- Initial machine/postprocessor and operation subset, units, work coordinates,
  tool offsets and supported arcs/feed semantics.
- Exact ownership of stock, fixtures, tools, operations and generated results;
  invalidation when any source changes; explicit regeneration.
- Collision tolerances and swept-volume coverage for cutter, shank and holder;
  separate rapid-transfer and cutting checks.
- Native persistence scope, template/library impact and verification agreement
  before modifying the document format for CAM.
- Repeated editing, Undo/Redo, save/reopen, chained setups and independently
  checked path/material results. Generated programs require machine-specific
  acceptance; a visual animation is not that acceptance.

No external sidecar may become required document storage. Reuse the shared
placement, reference-entry, selection, localization and command lifecycle
contracts rather than adding CAM-specific alternatives.
