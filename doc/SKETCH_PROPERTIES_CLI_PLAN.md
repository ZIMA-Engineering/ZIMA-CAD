# Adding Sketch Properties to the shared command layer

## Audit findings

GUI `SketchPropertiesDialog` edits name, base XY/XZ/YZ plane, work-plane offset,
placement, and original references. Its implementation is in
`cpp/app/sketch_properties_dialog.cpp`, with commit handling in
`cpp/app/workspace/sketch_properties.cpp`.

At the time of this audit, CLI supports Sketch creation, geometry editing, and
profile-feature properties, but lacks a complete standalone Sketch Properties
operation. The proposal adds `sketch.set` and reference assignment/removal using
existing transactions and shared rules, without calling widgets from CLI.

## Part: existing data model

A standalone Sketch belongs to a Sketch HistoryContainer. Placement already has
a persisted owner. New and edited values will be committed in one step while
preserving the Sketch, geometry identities, and locks. GUI and CLI will share
preparation and commit. Verification will include subsequent extrusion, Undo/Redo,
and native files. This part requires no Part-format change.

## Assembly: approved proposal

The user explicitly approved storing standalone Assembly Sketch placement containers
directly in `.asmz`, preserving the solver, exact occurrence paths, and explicit
regeneration, and updating the start template. Approval covers the change below.

At the time of the audit, the GUI callback stores `committed_placement` for Part
and Assembly-owned cuts but discards it for standalone Assembly Sketches.
`AssemblyDocument` stores Sketches without placement containers, and
`resolve_constructions` solves only construction objects.

Proposed additions:

- Give each standalone Assembly Sketch a persisted placement container using
  existing Placement, stable IDs, and original-reference rules.
- Store the container definition and references in `.asmz`, with no required
  external or sidecar files.
- Share GUI/CLI commit. Cancel discards pending edits; one committed change
  produces one Undo/Redo step.
- Preserve the exact occurrence path in component references. The Sketch must
  not move components or drive subassembly internals.
- Solve the local work frame with existing ZIMA solving, without OCCT. Standalone
  Sketch edits must not solve component mates or Assembly cuts; those retain
  explicit regeneration.
- Update the Assembly start template in config and check start Part. Preserve
  extensions and introduce no legacy migration.

Explicit approval required by Container placement protection in AGENTS.md has
been obtained. No further confirmation is needed for this proposal.

## Verification before completion

Compare full native definitions from GUI and CLI; Cancel/OK changes; locks for
zero and nonzero values; plane references and removal; correct plane and offset
in translated/rotated bodies; invalid and forward sources without mutation;
geometry after explicit regeneration; Assembly occurrences and ownership;
saving and reopening.

## User clarification: Assembly operations

Assembly Extrusion and Revolution always subtract from selected immediate Parts.
This applies to GUI, CLI, and standalone Sketch conversion. Conversion preserves
the placement container and references, but the resulting feature must use
CombineMode::Subtract. A standalone Sketch creates no material.

## Assembly implementation dependencies

Standalone containers will be stored in `sketch_containers`; each standalone
Sketch references its container through existing `owner_container_id`. The
container owns placement, Origin, and locks. Cut-owned Sketches remain direct
children of their cuts. Native validation rejects missing or duplicate ownership;
it does not create containers while loading old files.

The change affects GUI tree and `tree`, selection and Properties, native saving,
dimension listings, references, and locks. Conversion to a cut preserves container
and Origin identity. Sketch Properties changes do not recalculate Assembly
components or their calculated bodies.

The audit also found standalone Assembly Sketch deletion implemented as a direct
GUI callback. The ownership change will move deletion into a shared operation,
preventing orphan containers and allowing identical command behavior. Cut-owned
Sketch deletion remains part of its owner's operation.

## Implementation of the approved proposal

The Assembly Sketch data container, shared Properties, references, locks, deletion,
and conversion to a cut are implemented. Extensions are unchanged; the Assembly
format at this stage is INI 18 / internal JSON 27, and the Assembly start template
is updated. See [ASSEMBLY_SKETCH_PROPERTIES.md](ASSEMBLY_SKETCH_PROPERTIES.md)
for the resulting contract and verification.
