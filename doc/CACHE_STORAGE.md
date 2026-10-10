# File-local cache sharing

Part INI version 46 (JSON 70) and Assembly INI version 35 (JSON 47) store calculated cache data
using `zima-shared-cache-v1`. There is no compression and no external cache
store. Previous Part/Assembly file versions are intentionally unsupported;
create fresh imports when comparing this format with previous builds.

The wrapper contains a root and a table of values. Identical JSON blocks of
at least 256 serialized bytes share one table entry. Blocks are compared by
complete serialized content, including their child references, not by a hash
alone. A `{"$zima_cache": N}` record references an earlier table entry.
The reader rejects invalid, forward and cyclic references. References are
storage-local addresses, never persistent topology or occurrence identities.

All calculated states, viewer geometry, ancestry, placements and occurrence
paths are restored unchanged before normal model loading. Loading does not
invoke OCCT. Distinct history states remain distinct. Explicit Regenerate
and independent source-document editing retain their existing contracts.

Assembly occurrence containers retain metadata only. Their authoritative
component data is stored once in the assembly root, rather than duplicated
in each occurrence container. Source Parts and nested Assemblies remain
independent files, and each Assembly retains its calculated snapshot.


## Immutable archives and rigid Body operands

Calculated `BodyResult` packets retain opaque BRep bytes in a
`KernelShapeArchive`. Copies share immutable string storage; a new calculation
replaces the archive rather than mutating another history or Undo snapshot.
Native serialization still emits the complete `kernel_shape` string. The
in-memory ownership change does not introduce a required external cache.

An ordinary local result contains raw BRep bytes. A placed Body may additionally
contain ordered `kernel_placements` (translation and Euler rotation in degrees).
A final Part compound uses `ZIMA-BODY-COMPOUND-V1` in `kernel_shape` and an ordered
`kernel_compound_bodies` list naming its self-contained `body_outputs`. These
are current calculated representations, not legacy migration paths. The loader
validates finite poses and complete, unique operand identities; reference and
viewer geometry remain stored directly. Compound operands are retained even
when the caller omits unrelated history snapshots from serialization.

Only the OCCT calculation/export boundary reads these archives and materializes
the ordered poses and compounds. Opening, picking, preview and property entry
consume persisted viewer/reference data. Assembly geometry tables retain their
existing shared operand addresses instead of duplicating the compound children.

The traversal optimization preserves the existing 256-byte threshold, complete
content comparison, reference order and malformed-reference rejection. Its
regression oracle compares complete encoded bytes against the former copying
traversal, including nested Body packets, Unicode, signed zero and threshold
boundaries. Final large-data performance and dependent acceptance for the rigid
Body follow-up are recorded in [the measured investigation](performance/20261010-step-gauss-shared-source.md).


The same immutable ownership applies to imported topology identities, opaque
archive bindings and each Body's calculated local history. Capture/load builds
complete new arrays before publication. Public readers expose const elements;
replacement and clearing cannot mutate another document or Undo snapshot.
Unchanged pure STEP histories reuse their full local packets only when every
prefix fingerprint matches, no errors exist and no live original-face or solid
operand is requested. Consumers needing those operands keep the established
kernel evaluation and validation path. This avoids both metadata copies and an
unused temporary final-local packet during a rigid Body move.
