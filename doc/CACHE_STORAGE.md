# File-local cache sharing

Part INI version 16 and Assembly INI version 13 store calculated cache data
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
