# External Skeleton limits for Part Extrude

## Ownership and lifecycle

An active Part occurrence may use an original face or datum plane from a
Skeleton Part as its Extrude Up To limit. Other foreign Parts are excluded.
The existing local-Part targets and Assembly-owned subtractive cutters keep
their ownership contracts. A dependency points from the dependent Part to
the Skeleton, never from a Part to its owning Assembly. Existing shared
dependency transactions publish the corresponding occurrence dependency;
cycle checks, Undo/Redo and source identity validation remain in force.

Creation and editing use the ordinary Feature dialog and reference table.
Picking reads already calculated source geometry. The candidate filter builds
its allowed Skeleton occurrence paths once; it performs no file loading or
kernel work per hovered candidate. Reference storage follows only explicitly
persisted Boolean face ancestry, retaining the original occurrence path.
The offered current face remains the View feedback source.

On explicit OK, the kernel captures only the selected native face and its
existing bindings. The native Part stores its source document/file, exact
dependent/source occurrence paths, context Assembly identity, cached viewer
samples and native face. It does not copy the source Part history and requires
no external geometry cache or sidecar. The source file remains an ordinary
native document dependency. Opening, display and inspection consume the saved
calculated data without regenerating the Skeleton.

Explicit Part or owning Assembly regeneration refreshes active external
limits from authoritative open source documents, including unsaved changes;
closed sources are read from native files. Regeneration validates the saved
context and identity instead of silently selecting another occurrence.
An unavailable target rejects the update while preserving the previous
valid Part and repairable source binding.

## Coordinates and exact calculation

Cached viewer samples are in the dependent Part frame. Native face geometry
stays in the source Part frame and uses the persisted occurrence frame chain,
followed by the inverse dependent Body frame. The existing rigid placement
convention applies each inverse translation and inverse Z/Y/X rotation in
order. Body placement changes reproject cached inputs before calculation;
they do not require source regeneration or a different placement solver.

The command consumes the existing ordered Up To native clipping path even
for one external face. Curved faces retain their exact native surfaces;
rendering triangles are preview data. Existing side, reversal, correction
and signed-zero values keep their meaning. Native serialization and the
public Feature CLI carry the same optional `external_source` binding.
The normal definition contains no such field for a local target.

## Windows verification status

- Forty-eight native planar cases passed independent plane and volume
  equations, opposite directions, aligned and differing occurrence rotations,
  Body frames and nested Assembly contexts, corrections `-2`, `-0`, `+0` and `+2`, native identity/cache
  persistence, Undo/Redo and unsaved source-height changes.
- The actual GUI/CLI test passed real face hover, right-button cycling and
  left-button confirmation, foreign Part filtering, draft isolation, Cancel,
  OK, native volume, CLI correction, Undo/Redo, source immutability and native
  persistence.
- The curved-cylinder check matches an independent exact volume integral before
  and after an unsaved source-radius change. Missing source geometry rejects
  regeneration atomically; forged source document identities and dependency
  cycles are rejected.
- Split-face, multiple-target exact preview, datum-plane, source relocation and
  final integration checks remain pending. Template/factory acceptance remains
  pending. This is a development candidate, not a
  release or an exhaustive claim for every target topology.
- Linux verification remains outstanding on a Linux host.
