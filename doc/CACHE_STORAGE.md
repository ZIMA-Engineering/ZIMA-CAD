# File-local cache sharing

Part INI version 46 (JSON 70) and Assembly INI version 37 (JSON 49) store
calculated cache data using `zima-shared-cache-v1`. Large beneficial wrappers
use adaptive lossless zlib/base64 compression; small or unhelpful cases retain
the ordinary shared wrapper. There is no external cache store. Previous
Part/Assembly file versions are intentionally unsupported;
create fresh imports when comparing this format with previous builds.

The wrapper contains a root and a table of values. Identical JSON blocks of
at least 256 serialized bytes share one table entry. Blocks are compared by
complete structure, including their child references, not by a hash alone. The
writer uses a recursive fingerprint only to select possible matches and then
checks exact structural equality; it no longer serializes every nested subtree
repeatedly while discovering duplicates. A `{"$zima_cache": N}` record references an earlier table entry.
The reader rejects invalid, forward and cyclic references. References are
storage-local addresses, never persistent topology or occurrence identities.
Expansion mutates the owned decoded tree in place. Only a cache-reference node
is replaced by its pooled value; ordinary JSON nodes retain their allocation.
This avoids millions of redundant self-move assignments in large Part caches
without changing the expanded value or the validation rules.

The native history payload retains the exact calculated operation boundaries,
source fingerprints, stable identities and kernel archives inside the `.prtz`.
For a validated Body graph, the `zima-final-body-result-v2` encoding stores the
self-contained final document packet once. That packet already owns every
Body's immutable local boundary sequence and every derived Body output. The
loader reconstructs the document-level boundary vector from those exact packets,
rebinds its prefix fingerprints and applies the same deterministic non-final
reference compaction as the evaluator. It does not invoke OCCT. The writer uses
this encoding only when every operation maps unambiguously to the final packet;
otherwise it retains the complete boundary-array encoding. Both encodings are
file-local disposable caches, not authored model data.

Viewer packet v2 stores vertex and polyline coordinates in validated little-endian
binary arrays. Consecutive identical triangle identities use run lengths, while
edge/point identities and all sheet-side, display-owner, treatment-side,
endpoint and measured-value metadata remain exact aligned arrays. A Body history
may store a complete final measurement-source list once and earlier exact
prefix lengths only when every source packet and binding equals the corresponding
final prefix. Any unsupported packet falls back to the complete representation.
The reader retains temporary v1 conversion support, but all new calculated
caches are written as v2. The authored Part format remains INI 46 / JSON 70;
no project conversion or sidecar is required.

On reopen, a matching prefix can therefore be restored without recalculating it.
Any changed definition, source geometry, placement, reference choice or kernel
history identity invalidates the affected reuse. A first calculation after an
algorithm update can consequently be cold while subsequent edits reuse the
unchanged prefix. This behavior is intentional correctness validation, not a
separate loading buffer or sidecar cache.

All calculated states, viewer geometry, ancestry, placements and occurrence
paths are restored unchanged before normal model loading. Loading does not
invoke OCCT. Distinct history states remain distinct. Explicit Regenerate
and independent source-document editing retain their existing contracts.

The active OCCT kernel also keeps bounded in-memory copies of source shapes and
complete reference packets for repeated operations. Both use the established
256-entry live-history limit. Entries are exact derived data and may be evicted;
they never replace persisted document information and do not weaken input or
reference validation.

Before persisting the final calculated BRep, the kernel removes only p-curves
whose supporting faces are no longer present. Intermediate operation shapes are
not cleaned because later operations may still consume their representations.
This prevents repeated parameter edits from accumulating obsolete OCCT curve
records while preserving final topology bindings, viewer/reference packets and
the exact save/reopen contract.

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
