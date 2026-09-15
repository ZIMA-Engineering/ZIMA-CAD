# Body measurement

**Body measurement** (**Měření tělesa** in Czech), beside Measurement in the
View toolbar, creates a named Part history analysis. It reports volume, surface
area, centroid, mass and central mass inertia. Its Origin is automatically placed
at the centroid; three rotation parameters orient its axes. Position is calculated.
The body container's separate **Body properties** dialog continues to edit its
placement and local Origin. Neither its identity nor its placement is changed by
measuring the body. The measured point is labelled **Center of gravity**
(**Těžiště**) in the View and Tree.

## History and interaction

- With an active Body, the feature measures its cumulative solid at the current
  insertion position. The persisted anchor is the preceding history entry ID.
- Without an active Body, it measures available Part results at the current
  Body-history insertion position. Independent Bodies are summed; overlapping
  Bodies are not implicitly fused. Boolean results replace consumed operands.
- The row appears before **Insert Here**, with a **Center of gravity** child.
  Create records at several boundaries to compare centroid movement.
- Regenerate refreshes records from their own boundaries. Later features do not
  affect measurements inside a Body; upstream changes update them. A whole-Part
  measurement after a Body includes that Body's current history.
- Creation and editing use one shared internal property window, showing the
  cached input solid at the saved boundary. The centroid frame appears immediately
  in azure, including when ordinary Origins are hidden. Rotations preview immediately.
  Only **Save** commits one Undo step and inserts or updates the history row.
  **OK**, **Cancel**, closing the window and middle-button double-click close
  without saving pending changes. A short middle click leaves the window open.
  This explicit inspection contract is the same as Measurement.
- Selecting the analysis row or its Center of gravity child highlights that exact
  centroid frame in azure. An empty View click clears ordinary Tree/View selection.
- Context-menu Properties, Hide/Show and Remove also work from the Origin child.
  The visibility checkbox and Hide/Show control the same persisted display state;
  opening the inspector still shows its transient centroid preview.
  Results scroll independently of the fixed Save/OK/Cancel footer, so confirmation
  remains accessible in a short window. Missing anchors or failed input
  calculations produce a red record and unavailable results; the record never
  silently moves to the end of history.

## Quantities

Kernel storage uses mm³ for volume, mm² for area, mm for centroid and mm⁵ for
central volume inertia. UI values follow document length/mass units. Uniform
Part density comes from `MASS_DENSITY` and its declared unit. Without density,
geometric quantities remain available; mass and mass inertia are unavailable.

Mass is density times volume. Tensor coefficients are row-major with conventional
inertia-matrix signs, e.g. `Ixy = -∫xy dm`. All moments are about the centroid.
Origin axes use `Rz × Ry × Rx`; components in those axes are `Rᵀ I R`.
Body placements transform centroid and tensor. Combining Bodies uses the
parallel-axis theorem about their combined centroid.

Initial scope is Parts with uniform density. Heterogeneous Assembly mass
properties are a separate extension. Analysis Origins are display frames,
not feature-placement references. Section area moments (mm⁴), section moduli
W (mm³), and section cuts require a separate cross-section analysis; mass
inertia must not be presented as a section modulus.

## Calculation and native storage

OCCT captures centroid and inertia during explicit solid calculation, retaining
adaptive integration for rational surfaces. Mirrors, patterns and placements
transform cached integrals. Dialog opening, axis editing, CLI reads and View
refresh do not invoke OCCT.

`BodyResult.volume_integrals` persists in calculated packets, including individual
Body boundaries. `PartDocument.body_properties` stores identity, scope, anchor,
rotation, visibility, values and errors. Document-session publication refreshes
records from cached boundary integrals, including evaluated Family Table members.
Undo/Redo retains records and corresponding solid snapshots. No sidecar is needed.

Native versions: Part INI **26** / payload **50**, Assembly INI **22** / payload
**34**. Both tracked start templates are updated. Drawing format is unchanged.
Older Part/Assembly formats are intentionally unsupported under repository policy.

## CLI

The GUI rename does not change the `body_properties.*` command identifiers or
the native record structure. Existing user-assigned record names are retained.

| Command | Arguments |
| --- | --- |
| `body_properties.create` | Optional `name`, `rotation_degrees: [x,y,z]`, `visible`, `document`; uses the insertion position |
| `body_properties.set` | `object`, optional `name`, `rotation_degrees`, `visible`, `document` |
| `body_properties.get` | `object`, optional `document` |
| `body_properties.list` | Optional `document` |
| `body_properties.delete` | `object`, optional `document` |

Results include `volume_mm3`, `area_mm2`, `density_kg_mm3`, `mass_kg`,
`integrals.centroid_mm`, `integrals.central_inertia_mm5` in document axes,
and `inertia_kg_mm2` in the selected Origin axes. Missing values are null.
`body_calculated` is false. GUI and CLI share guarded transactions; stale edits,
identity changes, duplicate names and non-finite parameters cannot commit.

## Verification

`zima_cpp_body_properties_tests` independently checks a 10 × 8 × 6 mm block:
480 mm³, centroid (5,4,3), central volume inertia (4000,5440,6560) mm⁵,
and mass 0.003768 kg at 7850 kg/m³. It also checks subtraction, reflection,
spaced copies, rotated axes, native round trips, cursors, upstream/downstream
edits, missing anchors, unavailable density and atomic Undo/Redo.

The measurement inspector GUI contract exercises creation/editing, Cancel,
middle-button confirmation, Origin preview, tree insertion order and restoration
of downstream geometry after closing Properties.

Acceptance: ten distinct selected contracts pass in
`build/body-properties-tests.log` and `build/body-properties-final-tests.log`.
The final build is `2026091510`; no portable release was published for this change.

Build **2026091511** adds a regression for an active Body: Measurement and Body
properties must remain children of the real Body, before Insert Here, after a
later feature is created. The historical volume remains 6000 mm³ even after a
larger block is added downstream. Actual context-menu interaction verifies
Hide/Show and the centroid overlay; a 260-pixel-high dialog keeps OK accessible.
These checks pass with the other 14 selected contracts in
`build/balloons-acceptance-tests.log`. The centroid-window capture is
`build/balloons-measurement-ui.png.mass.png`.

Build **2026091512** separates Body measurement from Body placement properties.
The inspector regression checks the explicit Save action, no history insertion
on opening, OK, Cancel or middle-button double-click, an immediately confirmed
centroid preview, exact Tree selection and clearing selection on an empty View
click. The Save footer remains accessible at a dialog height of 260 pixels.

Setting `ZIMA_BODY_MEASUREMENT_SOURCE` exercises the same workflow on a temporary
copy of a native Part. The inspected `Projects/01.prtz` has a centroid near
(5.564568, -1.918716, -2.026137) mm, distinct from its Body Origin at (0, 0, 0).
With ordinary Origins hidden, its transient centroid is still shown in azure.
Save creates exactly one record, preserves Body placement and identity, and
survives reopening the saved copy. The original file remains unchanged.
The captures `build/body-measurement-ui.png.source.png` and
`build/body-measurement-ui.png.source-tree.png` were visually inspected.

The five targeted contracts pass: measurement inspector UI (with the source
copy above), shared UI, body properties, measurement commands and translations.
Logs: `build/body-measurement-tests.log` and
`build/body-measurement-translations.log`. GUI, CLI and the affected test targets
build successfully in `build/body-measurement-build.log`.
The additional workspace startup contract now passes in
`build/startup-insertion-tests.log`. Its earlier Assembly insertion failure was
caused by the fixture leaving Sketch Properties unconfirmed after returning from
Sketcher. The fixture now confirms those Properties before inserting a component.
No production insertion or Family Table change was needed. These checks do not
constitute portable-release acceptance.
