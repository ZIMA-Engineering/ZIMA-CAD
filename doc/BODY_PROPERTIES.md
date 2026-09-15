# Body properties

**Body properties** (**Vlastnosti tělesa** in Czech), beside Measurement in the
View toolbar, creates a named Part history analysis. It reports volume, surface
area, centroid, mass and central mass inertia. Its Origin is automatically placed
at the centroid; three rotation parameters orient its axes. Position is calculated.

## History and interaction

- With an active Body, the feature measures its cumulative solid at the current
  insertion position. The persisted anchor is the preceding history entry ID.
- Without an active Body, it measures available Part results at the current
  Body-history insertion position. Independent Bodies are summed; overlapping
  Bodies are not implicitly fused. Boolean results replace consumed operands.
- The row appears before **Insert Here**, with an **Origin — centroid** child.
  Create records at several boundaries to compare centroid movement.
- Regenerate refreshes records from their own boundaries. Later features do not
  affect measurements inside a Body; upstream changes update them. A whole-Part
  measurement after a Body includes that Body's current history.
- Creation and editing use one shared internal property window, showing the
  cached input solid at the saved boundary. Rotations preview immediately.
  **OK** commits one Undo step. **Cancel** restores the full scene without
  changing the record. Middle-button double click over View confirms; a short
  middle click does not.
- Context-menu Properties, Hide/Show and Remove also work from the Origin child.
  The visibility checkbox and Hide/Show control the same persisted display state.
  Results scroll independently of the fixed OK/Cancel footer, so confirmation
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
