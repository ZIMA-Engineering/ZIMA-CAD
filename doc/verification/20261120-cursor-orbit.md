# Cursor orbit and saved camera centers

Status: native and focused Windows GUI checks passed, including the normal
local 2026112002 executable. Portable packaging and publication remain pending.

The user approved the toolbar mode and explicitly approved extending saved
camera data, including start templates, factory assets and protected Toyota
copies. This is part of the unpublished Part 56 / Assembly 38 change.

The three-color Origin-shaped toggle follows Restore View and starts disabled.
With the toggle enabled, middle-button press captures the cursor ray's
intersection with the plane through the current camera center, normal to the
view direction. No model picking or kernel calculation is involved. Press
alone changes no camera state. During rotation the captured world point remains
fixed, including its camera-space depth in Perspective. Zoom and pan keep their
existing values. The center displacement is stored in the camera snapshot.

With the toggle disabled, rotation uses the fitted model center. Switching the
toggle does not jump the camera. Middle/right panning and fly navigation retain
their established behavior. Restore View clears the center displacement while
retaining the selected navigation mode. Standard directions ease it to zero
unless the existing caller requests preserved framing.

Named views store three finite `center_offset` components alongside the eight
existing numbers. The value is a world-space offset from the fitted model
center. View restoration and per-document camera memory carry all eleven values.
The runtime has one strict current record reader; the explicit offline factory
converter supplies zero offsets for preceding records. Drawing still consumes
only the saved orientation and keeps its own sheet scale and position.

The complete 1,072-asset direction-conversion inventory was inspected recursively,
including nested JSON definitions. There are 1,061 named-view storage locations
and no nonempty authored named-view records. Its 1,060 supported native assets
therefore need no authored-view conversion; earlier geometry verification remains
applicable. The changed serializer rewrote and reopened all six start templates.
Actual new-document GUI creation passed in 2.93 s. Evidence is recorded in
`build/rip25-20261120/orbit-named-view-inventory.json`.

All 1,060 supported factory assets subsequently passed exact native readback
with the changed runtime. The read-only verification retained authored history
and cached boundaries and checked every source against its previously verified
SHA-256. Evidence is `orbit-factory/results.json` in the same directory. The
twelve unsupported legacy assets and protected Toyota limitations remain as
recorded in the profile-direction report.

The native named-view suite passed complete Part/Assembly camera persistence,
no-op metadata, invalid inputs, Undo/Redo and nested ownership. The first test
launch overlapped template rewriting and failed to open a template; the run
after template rewriting completed passed. The native document suite also
passed Unicode paths, exact readback, caches and failed-write preservation.

The actual GUI matrix passed in 6.13 s for Part and Assembly: both projections,
successive cursor gestures, fixed screen position and depth, normal model-center
rotation, pan, fly, short click, middle-double-click confirmation, and Restore
View. Named-view saving, animation restoration, GUI/CLI byte parity and native
reopening preserve all eleven values. Navigation leaves document revisions and
dirty state unchanged. Source/catalog coverage and translated controls passed
in cs/en/de/fr/ru. The adjacent RGB icon was visually inspected in the FORM
GUI screenshot. Section GUI checks also passed in 21.12 s, including repeated
Assembly occurrences, camera quarter turns and middle-button confirmation.
The existing Sketch-view memory GUI matrix passed all sixteen Part/Assembly,
front/back and quarter-turn cases, two independent Sketches and fresh-window
native reopening in 118.96 s. It also created Part, Skeleton and Assembly
documents from all six mm/inch start templates and verified their active
editing context and ordinary commands.

Logs are `orbit-named-view-native2.log`, `orbit-native-documents.log`,
`orbit-start-templates.log`, `orbit-translations.log`, and the `direction-gui`
`orbit`, `templates` and `sections` outputs under `build/rip25-20261120/`.
Linux execution remains unverified.

The final local executable passed the orbit/named-view GUI matrix again in
5.49 s and template creation in 2.53 s. Build identity, calculation and binary
hashes are recorded in [Local Windows build 2026112002](20261120-local-2002.md).
