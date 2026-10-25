# Historical pressed wing nuts

The factory `05-NUTS/09-WING/01-PRESSED` folder contains six ordinary native
steel Parts, M3, M4, M5, M6, M8 and M10. They use the archived CSN 02 1665
dimensions cross-checked against the historical STN 02 1665 excerpt, page 16.
The source archive and facsimile identities are recorded in
[Historical wing-nut studies](WING_NUT_LIBRARY.md).

R1 is the wing's crown arc. The hub meridian uses the excerpt's R2, called R5
in the combined archived table. The new table uses the excerpt's h for overall
height and p for hub height; the combined archived table remains unchanged.
The upper flank angle omitted by the table is selected as 70 degrees. The
lower concave wing arc is tangent and follows the user's accepted symmetric
M10 root pattern; its exact tangent direction is retained per size.
These nominal CAD choices are documented in all five localized reference pages;
they do not establish a complete manufacturing specification.

## Native construction and properties

One ordinary Body contains three constrained Sketches and six history entries:
wing Extrusion, two-instance circular Pattern, thickness-taper Cut, its Pattern,
curved-hub Revolution and a metric through opening with schematic thread.
The wing's two arcs share one native point and a tangent constraint, without a
duplicate coincidence. A construction tangent controls the lower flank angle.
The hub has a distinct native centerline for the Revolution.

Only the valid derived expressions remain: `mass = model.mass` and stock from
the thread designation and pitch. Removed Fx dimensions are not recreated by
stale dependent Relations. The authored wing, hub, taper and opening dimensions
remain independently editable. Steel density is 7,850 kg/m3; indicative archive
weights are not copied or used to tune geometry/density.

## Periodic seam investigation

The first private candidates could silently lose material during Boolean Add
after successive lower-flank edits. Their final BReps still passed OCCT validity,
so validity alone was insufficient. Independent bounds, volume and mass rejected
them. Changing Boolean tolerances or regularity flags did not resolve that case.

The accepted authoring path places the axisymmetric hub's Sketch at a 90-degree
rotation about Z, using the existing placement command. This puts its periodic
construction seam outside the wing-profile plane while retaining the original
solid geometry. No shared placement implementation, native format, modeling
tolerance or general Boolean algorithm changed.

The exact recorded failing input was copied to a new disposable file before
editing. Rotating only its hub seam restored 143.859893 mm3 against the independent
143.859872 mm3 calculation, including save/reopen. The source bytes remained
unchanged. Evidence is `build/pressed-failing-seam-90-v1/results.json`.
This is an authoring solution for these assets, not a general correction of
every possible OCCT Boolean failure. Earlier private candidates and the
positive-landing experiment remain rejected and are not factory assets.

## Finite Windows verification

- All six baseline Parts contain one connected valid solid with expected seat,
  span, height, bore and crown cylinders. The physical cache agrees with the
  Gauss-Kronrod BRep integral at 1e-10 relative; the adaptive and Gauss-Kronrod
  integrals agree at 2e-8. An independent nominal-section integral retains the
  established one-ppm factory criterion on the final nominal assets.
- All six sizes passed 35 successive edits each: eleven drivers changed twice
  and restored, then a fine thread and the original thread. Every step checks
  Undo/Redo, explicit Regenerate, reference identity, dependent equations,
  calculated mass and native save/reopen: 210 successful edits.
- The existing checker repeatedly drags every authored profile point with
  unlocked and locked dimensions. Native identities and solver equations remain
  valid; locked profiles retain their coordinates.
- Actual mouse editing passed for M3 and M10, three successive crown-radius
  changes each, with Undo/Redo, regeneration, native persistence and an independent
  physical check of the saved GUI result.
- The M8 reference has three associative first-angle views, real axes, seven
  manual dimensions and the native hub radius annotation. The base and five
  localized Drawings passed Regenerate and native Save As/reopen while retaining
  dimension values and exact owner identities.
- Five localized HTML/image pages, A4 selectable-text output, printable bounds
  and the unchanged combined historical table passed the reference tools.

Current conversion and edit receipts are under
`build/pressed-wing-user-pattern-all-v1` and
`build/pressed-wing-user-pattern-all-verify-v4`; drawing, GUI and page receipts
are generated beside them before promotion.
The installer validates their source hashes, GUI results, Drawing identities,
projection directions and thumbnails before adding factory files. It refuses
different existing assets and does not overwrite custom user libraries.

These checks do not certify all possible user edits, every GUI drag or native
Linux execution. Source and assets are shared; Linux verification requires its
host. A new Windows release still requires committed-source and packaged-runtime
acceptance. The four cast M12-M24 studies remain unfinished and uninstalled.
