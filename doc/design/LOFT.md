# Loft implementation record

Status: implemented in the shared native C++ application after Windows 2026102401.
The accepted OCCT 8.0.1 Windows release is 2026102401. Loft is later work.

## Inputs and calculation

Loft owns every input as an ordinary planar 2D Sketch with an independent
parent-local placement. Profiles occupy the upper full-width list; guide Sketches
occupy a separate list below. Clicking a populated field opens its shared Sketch
Properties and Sketcher editor. Clicking the last row creates an owned Sketch.
Both lists exclude Curve3D entirely. The user explicitly selected this ownership
model on 2026-10-24; the earlier standalone-reference prototype is superseded.

List order uses persistent owned feature identities. Reordering preserves the
selected identity and independent inspection. Removing a row deletes that owned
Sketch definition from the transient parent draft. Child OK changes only the
Loft draft; parent OK is the sole model transaction, and parent Cancel discards it.

No whole-3D-curve type dropdown or corner-rounding controls are present.
Provide Solid, Thin and Surface results through the existing terminology.
At least two profiles are required. Profile-only transitions use
BRepOffsetAPI_ThruSections. Guided networks use GeomFill_Gordon in ExactOnly mode;
each profile must intersect every guide in a compatible monotone network.
The current guided algorithm requires at least two guides. Never silently accept
a sampled fallback, missing contacts, extra contours or holes.

The shared exact Sketch/profile packet preserves native lines, arcs, ellipses
and B-splines. Kernel curve joining does not use display polylines. Stable face,
edge and point ancestry derives from authored profiles and roles, not OCCT
enumeration or sorted UUIDs. Thin consumes the established Surface Thicken
implementation. Closed Thin profiles determine inward/outward from the oriented
closed carrier, then discard its end caps before thickening.

## Command lifecycle and scene ownership

Use one PropertiesSubWindow class for creation and later editing. Consume the
existing placement capability and controls; do not change the protected shared
placement solver or confirmation contract.

Creation and Properties install the actual input/rollback boundary before their
first scene publication. Keep the selected feature and Tree edit state.
Reference arming, replacement, clearing, ordering, independent eye inspection,
numeric placement and previews read native/viewer data without an OCCT call.
Use the same authored profile/guide preview generator for the dialog and
ordinary hover/click/double-click inspection. Do not extract a second preview
from calculated result edges or fill faces as selection feedback.

Unchanged OK performs no calculation and creates no Undo transaction. Changed
OK validates, calculates and commits once. Cancel restores the complete unchanged
history and scene. Reference entry ends on a short middle click; middle double
click uses the shared OK mechanism. Both lists keep fixed row heights and
compact indicator/eye columns; surplus height expands their viewports.

## Persistence and protected data

The additive Loft-only native record stores profile/guide ordering and complete
owned Sketch definitions, placement, constraints, value locks and identities.
The ordinary owned-Sketch framing and Properties implementation is shared with
General Surface; the Loft entry points permit no Curve3D data. Existing feature
records and document extensions remain unchanged. No required sidecars or legacy
Loft compatibility paths are introduced. Factory assets contain no Loft records;
verify existing readers and templates before accepting the change.

If existing required contracts must change, use the authorized factory/template
regeneration gates. Never publish stale affected assets. Verify ordinary new
documents from actual templates in the GUI, and library Body activation/editing,
Family, insertion, dependent operations and save/reopen as affected.

The protected ZE0176-TOYOTA-G92K1X production project was inventoried before
persistence changes: 145 files, four native documents. A complete recoverable
copy has verified per-file SHA-256 hashes. Originals are not conversion targets.
Local evidence is build/loft-audit/project-inventory.json. Resolve native
dependencies and verify reopening/affected operations on copies before any
proposed replacement. Preserve custom libraries and edited factory files.

## Verification

See [the finite acceptance record](../verification/20261024-loft.md). The native
and GUI cases verify owned 2D definitions, independent Sketch Properties, both
lists, ordering, removal, eyes, whole Origin, placement, rollback, no-op, ordinary
selection, Thin, native save/reopen and Undo/Redo. Existing General Surface,
translations, native documents, Family, placement, surface and integration tests
are checked separately. No arbitrary-network, exhaustive Sketcher or native Linux
acceptance is implied.
