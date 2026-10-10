# Persisted calculation validation across native platforms

The Windows/Linux FORM discrepancy was traced to reconstructed Arc midpoints.
The same saved definition and OCCT version produced coordinates differing by
one or two floating-point steps through platform sine/cosine implementations.
Replacing just those two midpoint ordinates reproduces the old Linux runtime
fingerprint exactly. Normalizing authored coordinates or meaningful signed zero
would change modeling intent and is not an acceptable remedy.

A native Part now records a SHA-256 proof of its exact persisted definition
beside its calculated boundaries. JSON key order is deterministic; arrays,
numeric values, references, identities, frozen imported geometry and embedded
definitions retain their exact representation. The history entity table is
hashed by stable entity ID: the native reader may collect it in a different
storage order. Actual Body and modeling-order arrays remain order-sensitive.
The cache itself, its proof and
the document display name are excluded. Renaming does not calculate geometry;
independent copies still remap identity through the existing copy writer.
The digest streams the definition without building another large JSON string.

Loading first verifies that proof, then retains the stored calculated geometry,
properties and topology. It binds disposable incremental-reuse keys to the
current platform's reconstructed operations. Owner/reference and history-count
validation remain. No OCCT calculation is performed by loading or rebinding.
Saving calculated data still requires its strict current runtime keys to match
the supplied definition, so edited parameters cannot be saved with stale bodies.

Native Part INI identifies the protocol as
`Document.calculated_definition=zima-part-definition-sha256-v1`; CachedBodies
requires `definition_fingerprint`. Embedded Part JSON uses
`calculation_definition_fingerprint`. Extensions remain `.prtz`, `.asmz` and
`.drwz`; no required sidecar is added. Cached documents without the current proof
are unsupported, in accordance with the project's no-legacy-format policy.
Factory Part/Skeleton templates and calculated FORM/test assets are refreshed
with the current writer. This is one shared C++ implementation for both systems.

## Evidence and remaining native Linux acceptance

Native document tests preserve an actual Linux-calculated seven-boundary
Ventilation Window packet when opened on Windows and through save/reopen.
They reject missing proof, changed precision, changed Sketch geometry and even
a one-step floating-point edit. Renaming is accepted; saving changed parameters
with old calculated results is rejected. See the fixture provenance in
`cpp/tests/fixtures/parity/README.md`.

The GUI template contract creates a new Part and Assembly through the actual
New dialog, saves/reopens them, and checks active editable Body and enabled
normal commands. All five catalogs cover the added digest/validation errors.
Native Linux execution and the reverse Windows-to-Linux direction remain
pending. Follow [the Linux handoff](LINUX_RELEASE_HANDOFF.md); Windows evidence
does not establish Linux acceptance.
