# Single-write Part save and trimmed rectangle/circle editing

## Scope and contracts

This follow-up removes the automatic full temporary-document readback from
ordinary Part saving, as explicitly requested by the user on 2026-10-14.
The serializer writes the existing native representation once to its temporary
file, checks opening, writing and final stream closure, and only then archives
and replaces the destination. A failed write must preserve the previous
file. A failed open must not remove an unrelated existing temporary path.
Complete save/reopen comparisons belong to development regressions, rather
than every interactive Save.

No native format version, serialized block ordering, precision, geometry side,
container placement, reference identity or supported command is changed.
Assembly and Drawing already use single-write saving and retain their paths.
The implementation remains shared C++; Linux execution is not verified here.
No user-visible labels were added or changed; all five translation catalogs
are checked by the existing coverage contract.

## Trimmed circle defect

`Projects/03.prtz` contains a rectangle with a circle, then trimming into a
rounded profile. Its stored independent T/C/C contact is coincident with an
Arc/segment endpoint. The initial constraint solve succeeds, but the circular
seed lacks endpoint orthogonality for native shared incidence. Subsequent
dimension edits and point manipulation therefore reject feasible changes.
The existing seed now includes this same tangent equation for a native segment
endpoint, as it already does for explicit point-on-line incidence.
Manipulation of an independent persisted contact delegates to its defining
shared endpoint and commits only the fully verified private result. Saved
identities are retained; no migration or arbitrary coordinate merge is added.

New circle rim clicks reuse the explicitly selected native point. Native
endpoint tangent inference uses the existing snap tolerance. Trimming retains
the exact persisted contact point at a surviving boundary, through the existing
point merge, and removes only redundant incidence equations. External and
independent interior contacts continue to own their separate native point.
Removing T retains the point and its independent contacts/shared incidence.

## Finite verification

The native matrix covers 288 combinations of the original saved Sketch,
circle-then-line and line-then-circle trimming, both orientations/contact sides,
both support/point orders, R/D and aligned/axial distances, driving/reference
and locked/unlocked supported states. Reference dimensions cannot be locked.
Each combination performs six successive dimension edits, repeated centre
movement, all native point drags, rejected invalid edits and native reopening.
Independent incidence, H/V, C and T equations are checked after each action.
Anchored/fully constrained points must reject movement atomically.

Real Qt mouse workflows create circles using the upper and lower existing
rectangle endpoints, trim both curves, exercise draft Undo/Redo, save and
reopen. A separate real inline-editor/mouse workflow edits the saved profile's
dimensions repeatedly, exercises Undo/Redo and drags the centre. Dependent native
Extrusions cover all three profile fixtures and 18 dimension changes, verify
analytic rectangle plus circular-segment volume, exact transaction Undo/Redo
and native save/reopen. The slightly non-semicircular original Arc requires
its actual sweep in this independent volume oracle.

Established native and GUI tangent/contact/Arc-axis studies cover neighboring
native and external-support behavior. An initial Arc-axis GUI run passed
0Ă˘â‚¬â€ś25 but failed scenario 26 at a test hover assumption. Replaying 26Ă˘â‚¬â€ś47 passed.
The picker helper now cycles from the actual retained hover candidate, rather
than assuming confirmed-selection clearing resets the hover index; no product
picking behavior is changed. The final complete 48-scenario GUI run passes in 512.98 s.

The 288-case matrix targets this rectangle/circle graph, not every possible
Sketch graph. Arbitrary splines, ellipses and unrelated constraint combinations
are not claimed as exhaustive coverage. Original user documents and settings
are preserved; disposable native copies are used for all writes.

## Measurements

The immutable representative input is the 853,508,060-byte saved `02.prtz`,
SHA-256 `88285d6c2fd49b5a4299bf9454dd4bc5c705212c83214d3f4dad876638488502`.
Serial native IO probes include complete model loading and Save, with
independent exact definition and full calculated-packet digests outside the
measured operations. They do not time GUI animation or thumbnail work.

Baseline: load 28.0064 s, Save 78.8299 s, independent reopen 37.5672 s.
Removing normal save readback: load 26.1830 s, Save 44.0719 s,
independent reopen 35.2890 s. Save uses 44.1% less time (1.79 times as fast).
The 35.895 s baseline temporary readback explains the measured saving; the
actual output write itself is approximately 1.5Ă˘â‚¬â€ś1.6 s. JSON serialization and
pool packing remain substantial costs. These are finite local samples, not a
universal performance guarantee. Opening did not show a reliable improvement. A read-local typed packet reuse
experiment measured load 27.4504 s and reopen 41.9276 s, with unchanged exact
digests, but no useful gain. That experiment was discarded; the final code
retains the established reader. No opening speedup is claimed.

The exact definition digest is
`37a689159680e41ef22afd8e64753cd429caa7fc90acc1e75c4a27a70db4a439`;
the full calculated packet digest is
`025b00c0cf5edcb6f92b0985a3b30dfab9bedb1a29f075be99bb5d9ca5fd9765`.
Output size stays 853,508,060 bytes.

Evidence: `build/02-io-next-baseline.*.log`,
`build/02-single-write-measured.*.log`, `build/03-final-regressions.log`,
`build/03-dependent-gui-tests.log`, `build/03-arc-axis-rerun.log`,
`build/02-single-write-tests.log`, `build/03-final-native-and-axis-tests.log`
and the committed native/GUI fixtures. The final nine selected contracts all
pass (532.36 s total). The preceding complete native Sketcher study and
native/external common-tangent GUI checks pass as recorded above.
The packaging/publisher input suites pass 29 tests with two platform-only skips.
