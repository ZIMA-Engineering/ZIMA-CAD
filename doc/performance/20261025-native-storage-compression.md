# Adaptive lossless native storage compression

Status: implemented in the development candidate; factory/template and complete
GUI verification remain pending. This is not a published release.

## Scope and storage contract

The shared C++ native cache codec still pools exactly identical JSON blocks.
It additionally compresses a large pooled JSON payload with zlib level 1 when
the base64 representation would save at least approximately ten percent.
Small and incompressible payloads use the current raw representation. Both are
representations of the same shared-block data model, not parallel modeling or
legacy topology implementations. The document extensions remain unchanged.

The compressed wrapper contains `format`, `compression`, `uncompressed_bytes`
and `data`. The codec verifies base64, the zlib stream checksum, exact decoded
length and complete stream consumption before passing the decoded JSON to the
existing shared-block reader. Truncation, trailing bytes, unknown codecs and
nested compressed wrappers are rejected. Streaming avoids zlib's Windows
32-bit input-size limitation. Decoding does not preallocate the claimed length.

Compression does not change geometry precision, references, signed zero,
parameter identifiers or authored history, and creates no external files.
Assembly serialization uses this same codec for its complete serialized root;
corrupt compressed data must therefore fail loading, never be treated as a
disposable empty cache. Zlib is an explicit dependency in the shared Windows
and Linux manifest. Linux execution has not been verified on this Windows host.

## Measured production-model copy

Windows Release, OCCT 8.0.1, Qt 6.11, isolated copies of the saved ZE0176
Skeleton. The source was guarded and was not overwritten. No compilation or
other verification workload ran during these serial measurements.

| Measurement | Observed result |
| --- | --- |
| Original saved model | 137,707,218 bytes |
| Candidate saved model | 31,346,314 bytes |
| Reduction from original | 77.2% |
| Current codec, raw representation, open | 11,769.7 / 11,633.0 ms |
| Current codec, compressed representation, open | 11,954.5 / 11,739.8 ms |
| Current codec, save after raw open | 25,062.6 / 24,370.3 ms |
| Current codec, save after compressed open | 24,085.7 / 24,694.4 ms |

The same-version raw test input was 138,468,088 bytes. Its decoded cache JSON
and authoring sections were byte-identical to the compressed test input; only
the representation differed. The mean opening difference was approximately
1.2%. These are observations on one model and machine, not general guarantees.
Both save columns use the new compressed writer and do not isolate compression
cost. An additional older-binary comparison observed saves of 23.87/28.40 s
before and 30.34/24.94 s after; the small sample and run-to-run spread do not
establish a reliable save-time improvement.

All four same-version open/save cycles preserved exact decoded cache JSON bytes
and all non-cache authoring data. The older writer omits the `display_owner`
field on 39,734 native edge bindings, while the candidate persists it for
incremental-history restoration. Comparison of that baseline therefore checks
all shared fields exactly and reports these explicit added metadata fields;
it does not claim identical old/new serialized payloads.

Private evidence: `build/loft-audit/compression-performance/timings.json`,
`current-equivalent-timings.json`, `equivalence.json` and native I/O phase logs.

## Focused verification

The storage contract test independently decompresses the wrapper with the
one-shot zlib API and compares the exact pooled JSON with an independent
writer. It exercises consuming and read-only paths, deterministic block order,
large multi-chunk strings, Unicode, integer identifiers beyond exact double
range, positive and negative zero, malformed references, nesting limits and
corrupted compressed input. Existing profile command tests pass repeated Up To
edits, locks, atomic errors, Undo/Redo and native reopen. Assembly contracts
also pass. Full factory conversion, default-template GUI checks, practical
Relations/Family cases and final installed-runtime smoke checks remain delivery
gates; these focused results do not replace them.

The new invalid-compressed-data diagnostic is localized in all five Qt
catalogs (`cs`, `en`, `de`, `fr`, `ru`). Catalog coverage and GUI language checks
are part of the remaining integration verification.
