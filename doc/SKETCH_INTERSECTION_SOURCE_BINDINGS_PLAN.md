# Complete source-boundary bindings for Sketch intersections

Status: narrow persisted-contract scope approved by the user on 2026-10-22;
Windows implementation and factory verification pass; see the
[verification record](verification/20261022-sketch-intersection-source-bindings.md).
Sketch command integration remains pending.
The user approved intersection reference mode and selected-branch persistence on
2026-10-21. The additional source-boundary requirement below reaches beyond that
reference record and received its own explicit scope agreement on 2026-10-22.

## Demonstrated requirement

The [94-case foundation matrix](verification/20261022-sketch-intersection-closed-domains.md)
proves a cold circular solid section works, while a cold open section fails with
`Intersection endpoint ancestry is ambiguous.` Under the normal deferred face
measurement policy, ordinary solid archives store native face bindings but empty
edge/vertex bindings. Surface archives already capture all three kinds.

The relevant paths are `make_result`, `persist_surface_topology` and
`bound_face_geometry` in `cpp/modules/kernel_occt/src/occt_kernel.cpp`.
`intersect_face_plane` consumes those bindings with `curve_ancestry=true`.
The missing data cannot be supplied by treating a mesh endpoint as identity,
choosing a nearby edge or replaying source history from hover/Properties.

Inputs are the existing owned face/edge/vertex identities at an explicit source
calculation. Means are the existing common archive and topology-binding arrays.
The output is a source boundary independently usable after native reopening,
including its exact bounded open intersections, without hidden regeneration.

## Proposed bounded change

Whenever a calculated native boundary is persisted with owned topology, capture
its valid edge and vertex bindings alongside its face bindings through the
existing `persist_surface_topology` implementation. Keep the source's authored
parents, operation/fragment identities, original reference packets, sides, shape
and numerical precision unchanged. Reuse the same archive; do not serialize a
second copy of the solid or invent new topology-key forms.

This strengthens the required completeness of existing
`kernel_bindings.faces/edges/vertices` data. It is not merely another field in the
new external-reference record. There is no new sidecar, cache directory, kernel
version, placement solver or legacy migration branch. Missing existing data must
remain an explicit Regenerate requirement until the user deliberately recalculates
that document; do not silently rewrite private projects on Open or tab changes.

The pending intersection command must still consume a source's current calculated
geometry and exact occurrence/history boundary. It must not regenerate a source
to make an incomplete archive look complete. Saved intersection caches continue
to serve inspection without OCCT. Explicit source regeneration updates bindings
and dependent branches through the ordinary one-way dependency contract.

## Factory inventory and conversion scope

A read-only Git-blob inventory at
`35f93be7bd674b4ece40e062c1a82624480a813b` finds:

| Committed config assets | Count |
| --- | ---: |
| All native assets | 1,185 |
| Part documents | 1,034 |
| Parts currently containing cached kernel bindings | 1,030 |
| Drawings with binding keys in display-only packets (all arrays empty) | 137 |
| Assembly start templates | 2 |
| Part/Skeleton start templates | 4 |
| Part-based frame/title-block/symbol files | 12 |

The follow-up content audit finds 204 display packets in those 137 Drawings;
all native archives and face/edge/vertex binding arrays are empty. They require
verification against converted Part sources, not native archive conversion.
Their committed files remain byte-identical. Binding-key presence alone is a
structural inventory, not proof of native geometry or binding completeness. Evidence: `build/sketch-intersection-source-binding-inventory.json`.
The frame/title-block/symbol documents currently contain no such bindings; verify
their ordinary editable Body context rather than inventing solids for them.

After approval, use isolated copies of committed factory data. Calculate, save
and reopen affected native Parts, including every affected FORM definition, and
verify authored history, role Bodies, native reference/side identities, geometry,
mass and Relations. Update derived Drawing snapshots where they embed affected
source data; verify source identity, dimensions and the existing right-hand
sheet/title-block convention. Regenerate all six start templates and create real
new Part/Skeleton/Assembly documents in the GUI to check activation and commands.
Load/validate the complete 1,185-asset set before release. Commit converted factory
data with the contract change. Personal projects, custom libraries, local settings
and independently embedded user definitions are not bulk conversion targets.

## Verification and performance gates

- Replace the current cold-open-section expected rejection with successful exact
  plane/domain equations and stable native endpoint ancestry. Cover warm/cold,
  save/reopen, source dimensional/frame edits and multiple branches.
- Verify closed circles, open lines/curves, tangency, empty/coincident cases,
  disappearing/merging branches, trimmed faces, holes and opposite orientations.
  Follow the approved Sketch dimension/drag, Undo/Redo and actual mouse matrix
  when connecting the new reference mode to the editor.
- Check shared face measurement and every affected archive consumer: ordinary
  solid/surface calculation, Fillet/Chamfer, FORM insertion/dependent operations,
  Assembly occurrences, native reopening and Drawing/reference inspection.
- Measure calculation/serialization time, peak memory and native file size on
  representative small and larger sources before and after. Reuse existing
  archive traversal/storage; do not relax precision or add calculation to hover,
  Properties, Save of current geometry, tab switching or unchanged OK.
- Confirm source shape/history/identities are unchanged, not just matching volume.
  Read-only inspections must retain revision and perform zero body calculations.
- Review all five UI languages and both themes for any new diagnostics or command
  controls. A native Linux run remains a separate host requirement.

An affected stale factory asset, a reference/side regression, or unacceptable
unresolved persistence cost blocks publication. Do not call this extension or
the complete Sketch commands implemented before those gates pass.
