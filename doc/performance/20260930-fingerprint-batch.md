# A8/A15 — exact batch fingerprints and compiled encoding

Native Part boundary validation and document-copy remapping need every history
prefix. The previous implementation encoded each operation again for each prefix.
The new batch entry point seeds each prefix with its own count and feeds the same
operation bytes to its remaining prefix states. The existing single-prefix API
and byte order remain unchanged, including authored signed zero and source IDs.
Incremental kernel matching still uses its existing early-exit single-prefix path.

The batch uses O(N) additional hash state and encodes each operation once. FNV
byte processing is still quadratic; this is not a linear-time hashing algorithm.
It does not retain a second serialized geometry buffer or change persisted hashes.

Windows Release, alternating single/batch runs in the same executable, five
samples. Times cover all prefixes of each generated extrusion/revolution history.

| Profile sides | Operations | Repeated single calls | Batch |
| ---: | ---: | ---: | ---: |
| 4 | 32 | 1.4665 ms | 0.868 ms |
| 4 | 128 | 18.050 ms | 11.645 ms |
| 64 | 32 | 7.923 ms | 4.461 ms |
| 64 | 128 | 120.507 ms | 73.011 ms |

Every prefix matches the existing encoder. Twenty captured invalidation cases
also match their original golden hashes, including source lists, tolerances,
direction and negative zero. Empty, singleton and clamped-prefix cases remain
covered. No format, native template, geometry precision or UI text changes.

The encoder implementation now belongs to one compiled `zima_kernel_api` static
library source instead of the large shared geometry header. Consumers keep the
same API and receive the implementation through their existing CMake dependency.
This reduces implementation-edit dependency fanout; it adds no runtime DLL.
No whole-build speedup is claimed from concurrent, noisy compiler timings.

A timestamp-only Ninja dry run for the application target scheduled exactly one
C++ compilation, the static-library link and the application link. The source
timestamp was restored without changing content. Log:
`build/fingerprint-dependency-dry-run.log`.
