# A8 follow-up: avoid copying profile identities while hashing history

The unchanged-prefix checks in incremental calculation and native save/load
repeatedly encode the same profile identifiers. Extrusion and Revolution
encoders wrapped each outer edge/vertex ID list in a temporary nested vector.
This copied every string merely to represent a single group, including the
lists of additional profile regions.

The encoder now passes a one-element, read-only `std::span` to the existing
group encoder. The group count remains one even for an empty list. Order,
string lengths, byte encoding and the fingerprint algorithm are unchanged.
No inputs are cached and no geometry, signed-zero normalization, persistent
format, or dependency invalidation contract changes. The broader reuse of
prefix encodings remains an audit candidate; quadratic prefix traversal is
not eliminated by this limited change.

## Measurement

MSVC Release on Windows, Intel Core Ultra 9 285HX. The focused benchmark uses
alternating Extrusion/Revolution operations, each with an inner loop and an
additional profile region. IDs exceed short-string storage. Times are means
of three complete prefix sweeps, including the empty and clamped prefixes.

| Profile sides | History operations | Before | After |
| ---: | ---: | ---: | ---: |
| 4 | 32 | 1.899 ms | 1.627 ms |
| 4 | 128 | 23.126 ms | 21.243 ms |
| 64 | 32 | 11.792 ms | 9.786 ms |
| 64 | 128 | 188.461 ms | 138.046 ms |

The larger profile case took approximately 27% less hashing time. This is a
synthetic encoding benchmark, not a measured overall regeneration/save gain
on a user document. Every complete before/after fingerprint snapshot matched:
SHA-256 `D78372218C0074905943532E3898234D114F1B368B79747D1FB9427BF4637BDE`.

`zima_cpp_history_fingerprint_benchmark [snapshot-file]` reproduces the test.
Its CTest contract checks original golden fingerprints for independent changes
to all affected identity groups, empty groups, authored negative zero, mesh
deflection, Boolean tolerance and profile direction. No coarser equality or
loss of side information is accepted.

Verification passed: the focused fingerprint contract, the main kernel/native
document contract, derived-copy contract, and all-five-language translation
coverage contract. Windows GUI and CLI rebuilt successfully. The newly added
centroid-plane reproducer failed independently with the reported missing
measurement reference and is handled in a separate correction.

Localization review: no product UI text changes; benchmark diagnostics are
developer output. The root Windows launcher and native format are unchanged.
