# A12 — avoid discarded Symbol leader geometry

A visible Symbol with a leader previously built the placed glyph, discarded that
mesh, then built its local leader glyph. `Placement::viewer_mesh()` now chooses
the correct glyph coordinates before the single mesh call. Direct symbols,
hidden symbols, readable text, leaders, frame handedness, pens and reference
validation retain their existing paths. No persistent cache or invalidation
policy is added; every call still evaluates the current definition and text.

The same Release placement test was run before and after the implementation
change: five samples of 100 leader renders. Mean time was **124.098 ms before,
92.4947 ms after** (25.5% less time in this fixture). The exact-bit geometry
fingerprint, including coordinates, reference keys, colours and sampled annotation
layout fields, remained **7341180701063428579**. This is a symbol-rendering
microbenchmark, not a whole-application speedup claim.

[Before](20260930-symbol-leader-before.txt), [after](20260930-symbol-leader-after.txt).
All four Symbol placement/integration/document/Drawing contracts passed, covering
leader/contact geometry, orientation, variants and native persistence. No new UI
text, font choice, precision, reference identity or container-placement changes.
General parsed-definition caching remains a separate candidate.
