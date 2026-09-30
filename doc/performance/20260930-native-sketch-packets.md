# A9 — embed Sketch packets without intermediate JSON text

Part native loading already owns a parsed JSON tree, but each Sketch was dumped
to text and parsed again. Saving similarly created Sketch JSON, dumped it to
text and parsed that text back into the containing Part JSON tree.

Sketch now exposes a typed JSON packet entry point and reader. The existing
text entry points delegate to exactly the same encoder and validator. Part
load/save use the packet directly. The on-disk format, mandatory fields, native
extensions and factory templates remain unchanged. Assembly's intentionally
string-valued Sketch field remains unchanged; this is not a format migration.
No validation, reference check or calculated-boundary check is removed.

The 117,707-byte test Sketch includes 128 additional segments, rational and
trimmed curves, a Symbol, Unicode and authored negative zero. Twenty iterations
measured 88.4044 → 39.5927 ms for embedding and 80.6855 → 30.2908 ms for reading.
These are in-process encoding costs, not disk or whole-document load times.
Exact text output and round trips match; negative zero retains its bit pattern.
Both readers reject malformed format, missing fields and a dangling point
reference. Native document, CLI, library and workspace suites cover integration.
Localization review: no user-visible text changes.
