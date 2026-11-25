# CAM authoring, NC inspection and clamping acceptance

Date: 2026-11-25. Windows x64 Release, Qt 6.11.0, OCCT 8.0.1.
This completes the agreed first-stage CAM workflow. It does not implement a
controller postprocessor, pocket/contour machining, turning or 4/5-axis motion.
Windows publication is recorded separately in the release evidence.

## Behavior and lifecycle

The CAM setup owns private tool, machine, operation and clamping drafts. Tool
profiles use exact cylindrical/conical sections around the agreed cutting/shank
interface at Z=0. Ordered operations support Facing and manual linear/XY circular
blocks. Ordering selection is separate from opening an item for editing. Child
OK updates the setup draft; parent Cancel discards it. Unchanged OK retains full
authored precision and creates neither a geometry calculation nor an Undo step.

Machine and tool import use the shared file chooser and configured library
directories. Only the chosen native definition is loaded and copied. Machine
capabilities and explicit tool dimensions/feed/spindle parameters are imported;
free-form manufacturer/material descriptions are not parsed. This first library
adapter creates cylindrical cutting/shank envelopes from the explicit CAM
dimensions. More complex rotational tools can be authored in the profile editor.
Automatic conversion of arbitrary modeled tool solids and workholding-library
placement/import are not implemented. Clamping Sketches and fixture Bodies in
the source Part can be selected directly.

Calculate CAM reads immutable snapshots and runs exact native sweeps privately.
Cancel publishes nothing; publication requires the same runtime, revision and
input identities. A current result opens its NC inspector without recalculation.
The inspector maps selected/stepped lines to actual motion IDs, draws yellow
cutting paths and dashed red rapids, and moves the tool overlay. Its stock toggle
shows the final calculated stock, not frame-by-frame material removal. Generic
NC text export writes the compiled program atomically and is explicitly distinct
from executable controller output.

Contact and jaw-top references use the shared reference controls and independent
inspection eyes. Tree and View selection use the authored Sketch geometry;
available input is white and inspected geometry/normals are azure. Two explicit
side choices orient the contact exclusion and upper boundary. No shared placement
solver or general selection contract was changed.

The contact region, including holes, extends outward beyond the complete tool
and program envelope. Its volume remains fixed while stock changes. Cutter or
shank intersection is a collision. Actual removed stock below the jaw-top plane
and loss of contact area generate separate warnings. Empty/repeated passes do
not repeat material-loss warnings. Reports retain operation, block, tool and
clamping identities; selecting a warning selects its NC motion. Contact area is
geometric evidence, not a force/stiffness certification. Collisions prevent
downstream reuse of the retained stock.

## Native and GUI verification

Eleven CTest targets passed together in 19.72 seconds: CAM program, document,
sweep, simulation, clamping, operations, calculation, machine dialog and editors,
plus FEM condition and pressure regressions. The FEM checks cover the reused
planar-region adapter. Native operation, document, calculation, clamping and
editor suites contain respectively 52, 54, 59, 28 and 48 assertions.

Clamping checks cover both outward sides, reversed upper normals, cutting
above/on/below the upper plane, contact loss, holes, multiple clampings, cutter
and shank/rapid intersections, repeated cuts and translated/Z-rotated frames.
Independent volume/contact-area equations, exact two-sweep-per-motion counts and
unchanged native source packets are checked. Calculation integration additionally
covers a second setup with its frame rotated 180 degrees about X, native
save/reopen, Undo/Redo, changed sources and rejection of stale/unchecked results.
Sketch-only clamping Bodies are accepted without inventing a solid input;
suppressed, removed, rolled-back and broken sources invalidate reuse.

The actual Windows GUI scenario passed 275 checks in 7.855 seconds. It covers
setup/reference editing, whole-Origin placement, no-op and Cancel, machine/tool
library file dialogs, parent-draft ownership, actual View and Tree clamping
selection, independent inspection and side reversal, async calculation and
cancellation, NC stepping/remaining-stock display/export, warning-to-line
identity, all five languages, and mm/in tool-template creation and save/reopen.
Calculation removes the independently expected 200 mm3, retains 200 mm2 contact
and commits one result. Setup editing, NC inspection and text export do not
increase CAD calculation counters or document revision.

Localization coverage/catalog validation passed for cs/en/de/fr/ru. The NC
inspector framebuffer was reviewed; the program viewport consumes additional
height while controls and the short contact report remain compact.

Evidence is under `build/cam-followup-20261125/`: `ctest.log`, `catalog.log`,
`gui/20261125-030539/`, `new-documents/` and `asset-verification.json`.
Wall-clock values describe these synthetic fixtures, not a universal speed claim.

## Persistence, templates and protected data

The approved CAM extension now requires nested CAM schema 2, explicit paired
clamping references/sides and retained clamping reports. Its intent key is
`cam-intent-v2`. The ordinary Part envelope remains format 79: ordinary Body
storage did not change. Missing/old CAM schema and missing side fields are
rejected; no migration or legacy calculation path was added. All required data
is embedded in native documents.

The changed runtime saved/reopened six Part/Skeleton/Assembly start templates
and recalculated/saved/reopened both tool templates. All eight remain byte
identical. Actual GUI new-document creation passed in 41.948 seconds, verifying
active editable contexts and commands. All 1,190 tracked native factory assets
were inventoried: none contains a CAM Body requiring conversion. The packaged
native reader must still load all 1,190 before publication.

The recoverable protected-project backup was hash-verified. All 172 original
files match their prior hashes after testing. Two Drawing copies pass native
roundtrip; seven Part copies and one Assembly copy retain their already
documented unsupported legacy-format failures. No protected original was
rewritten and their modeling/reference behavior is not claimed as verified.

## Limits

Native Linux execution remains unverified. This is a finite matrix of planar
clamping regions and the supported three-axis motion model, not exhaustive
verification of every possible Sketch or machine. Existing Sketch dimension,
dragging and solver behavior is consumed without modification. Workholding
library placement, arbitrary-solid tool conversion, controller postprocessing
and later machining operations remain separate extensions.

Signed Windows 2026112503 passed the packaged and public-download gates; see
[publication evidence](20261125-windows-2503-publication.json).
