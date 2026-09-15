# Numerical value locks

A lock icon follows each length/angle field in Properties. Locked fields cannot be
overwritten or changed by wheel; direct View editing respects the same lock.
View context menus lock/unlock dimensions. Locked values are black; hover and selection
retain orange and azure. The lock is a separate button after the numerical field
frame and arrows, not an action inside the text editor. Shared styling reserves a
trailing button area; frame, editor, and arrows end before it. Fields remain ordinary
QDoubleSpinBox controls, including reference-table cells. Width calculation counts
the reserved area once, preventing dialog growth or number overlap on toggling.
Document units/precision are included in editor-width calculation.

## Reference assignment

- **Empty row:** locking arms one-time capture of the current value. Selecting a
  plane measures distance from the current Origin, stores the offset, and unlocks
  automatically. Assembly captures after both references are filled; angular mates
  measure the current angle.
- **Populated row:** locking permanently protects the value. Replacing its reference
  adopts current distance/angle to the new reference while retaining the lock.
- **Point and axis:** coincidence moves the Origin onto selected geometry, sets
  zero offset, and locks it automatically. Existing mate solving controls other free directions.
- If the current value cannot be determined, preserve the reference and report an
  error. Clicking again cancels pending one-time capture.

Example: Origin X = 12 mm. Arm capture in an empty row and select plane X = 0 to
fill 12 mm and unlock. Permanently lock that populated row, then replace its plane
with X = 5: fill 7 mm and retain the lock.

## Scope and persistence

Locks cover primitive, Sweep, and construction properties; body/component placement;
Pattern spacing/angle; images; and BOM regions. Drawing View Properties also protects
position/scale. Image width/height locks prevent indirect changes through aspect
ratio. Locked view coordinates constrain manual dragging.

For reference-driven orientation, View RX/RY/RZ dimensions represent local angular
corrections and lock those fields. Absolute angles and corrections have separate
persisted locks.

OK saves values and locks together; Cancel discards the whole proposal. Outside
Properties, View lock toggling is an independent document change with Undo/Redo.
Permanent locks persist; one-time capture is open-dialog state only. Lock-only changes
perform no OCCT solid calculation.

Locks protect manual value editing; solver-driven dependent changes remain possible.
Assembly physical degrees of freedom derive from mates, while manual dragging also
respects coordinate locks. If movement along an inclined axis requires changing locked
X, dragging does not proceed. Measured Drawing dimensions do not drive source Parts;
locking them creates no new geometric constraint.

## Verification

`zima_cpp_numeric_value_locks_contract` checks entry, inline editing, OK/Cancel,
persistence, one-time capture, reference replacement, and coincidence with points/axes.
Assembly tests cover free/inclined translation locks, angle measurement, and persistence.
Body Properties integration checks View linkage for correction-angle locks, Undo/Redo,
and lock saving outside dialogs. Numerical-field tests use 3, 4, 6, 9, and 12 decimals
and larger fonts. Lock tests check no editor/frame overlap, actual clicks, one state
change per click, and constant dialog width after 20 toggles. Windows additionally
runs `zima_cpp_numeric_value_locks_windows_contract` with native Windows style;
baseline uses Fusion. The resulting dialog was also inspected in
`build/cpp-windows-release/Projects/test/numeric-lock-layout.png`.

Assembly plane-offset capture uses signed distance from component Origin, independent
of selected triangulation vertices. It preserves Origin position even when initially
inclined planes need alignment.

## UI wording

Empty-row help describes one-time current-value capture on reference selection and
subsequent unlocking. Once armed, it offers cancellation. Populated rows use
**Lock Value / Unlock Value**. At this stage these strings, errors, and View actions
are available in four application languages; see [localization](LOCALIZATION.md).

The original full run caught Axis Properties expanding to 363 px. Lock reservation
now matches a 22 px button plus 2 px gap, preserving the original 360 px compact-dialog
limit. General dialog tests, all-precision readability, and both lock styles passed
**4/4 in 10.35 s** after repair.

Final Windows Release after this repair passed full **55/55 tests** in 365.35 s
(`build/model-calculation-final-tests.log`).
