# Keyway user-trial repairs

Windows MSVC Release, Qt 6.11.0, OCCT 8.0.1. This supplements the
[keyway follow-up](20261127-mech-keyways-followup.md). Evidence is retained in
`build/mech-trials-20261127`; user documents were tested on isolated copies.

The saved OTVOR example selected the correct internal cylinder and two end
faces. Its cylindrical-angle placement was incorrectly reused as an orientation
driver for the transverse cut Sketch. The placement solver correctly rejects
that combined reference role. The command now retains the angular position
equation and adds a separate orientation-only plane reference to its generated
child. The shared placement solver and persisted user inputs are unchanged.

The exact saved unresolved example failed before this correction and passed
native repair and cold regeneration afterward. The main-window test also opens
that saved unresolved copy, invokes Regenerate, saves it, verifies the original
placement reference bundle, opens Properties and confirms unchanged OK with zero
additional body calculations. Its authored preview is visible and no draft
error remains. Evidence: `user-repair-native.log`, `gui/stdout.log` and
`gui/user-angular-hub-preview.png`.

The complete native keyway suite passes. Added hub placement cases cover
cylindrical and generator-parallel conical bores, angles 0/90/-90/37 degrees,
both axial directions and a 20 mm placement datum. All sixteen cases include
Undo/Redo, cold native reopening and successive support-diameter edits.
Existing eighteen geometric variants and twelve shaft-frame cases also pass.

Preparation failures now use the shared confirmation protocol: first failed OK
keeps a private draft; the second unchanged OK stores an unresolved feature and
closes without another calculation. Changed parameters or source generation
invalidate the acknowledgement. The native matrix checks creation/editing,
Cancel, retained frames, persistence and Undo/Redo. Real-mouse GUI trials cover
an unsupported shaft diameter, missing hub ends, unresolved Tree/save state,
repair through the CSN selector or end-face entry, and successful cold replay.
The general modeling-confirmation suite also passes after its Assembly fixture
was updated to respect the existing prohibition on new Assembly material cuts.

The unrelated auxiliary construction axis is no longer displayed for keyways.
Draft errors appear directly in the panel, and invalid catalogue selection
clears stale dimension text. CSN 02 2507 / 02 2562 is explicitly identified as
the retained historical table. Its numeric rows were not changed or relabeled
as certified agreement with the 2003 editions. All five localization catalogs
and parameter-panel contracts pass. The engineering tables identify the same
standards and provenance.
All twenty pages in the four key/keyway chapters pass the light/dark layout
checks. Figure sizes retain their native aspect ratios and quarter-window
height limit, including the wrapping Woodruff illustration row.

## Remaining limits

- A rounded shaft slot with the first end exactly at the shaft end and a bottom
  fillet can fail in OCCT with `ChFi3d_Builder:only 2 faces`. The successful GUI
  CSN repair fixture uses a 10 mm axial offset. Zero-offset bottom-fillet support
  is not claimed by this report.
- Complex Sheet-from-Body performance remains the measured opportunity recorded
  in the follow-up; this delivery makes no general speedup claim.
- Linux execution and exhaustive GUI combinations are unverified. Native
  mathematical coverage is not a substitute for every mouse interaction.
